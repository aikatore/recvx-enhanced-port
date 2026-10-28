/* Co-op through the server, client side (rendezvous.h, docs/SERVER.md "Connection flow" and "Security"). */
#include "rendezvous.h"
#include "rdv_proto.h"
#include "monocypher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

void host_log(const char* fmt, ...);

const NetAddr rdv_peer = { 0, 0xFFFF };

#define RETRY_MS 500                    /* HELLO / CREATE / JOIN until answered */
#define GIVE_UP_MS 10000
#define RESOLVE_MS 5000                 /* DNS for the server's name */
#define DNS_CACHE_MS 600000
#define KA_WAIT_MS 1000                 /* host keepalive while no partner was introduced */
#define KA_MS 3000                      /* keepalive in a session (the server frees a joiner's place after 10 s, a host's room after 30 s) */
#define LOST_WAIT_MS 5000               /* no keepalive answer for this long: the server lost the room */
#define LOST_MS 35000                   /* ... in a session */
#define ORPHAN_IDLE_MS 10000            /* host: the direct session of a lost room is over */
#define PUNCH_EVERY_MS 50
#define PUNCH_MORE_MS 10000             /* after the relay switch: punching goes on, slower */

static RdvConfig cfg;
static int st, relay, heard, confirmed, path_lan, have_ses, e2e, pw_used, n_cand, lan_cand, attempt, keys_ok, orphan, flags;
static uint32_t t_start, t_sent, t_ka, t_ack, t_intro, t_punch, t_rx, relay_ctr;
static uint32_t t_unans, t_frame;       /* the first keepalive since the server's last answer (0: none); the last rdv_frame */
static uint32_t nonce, room_id, member_seq;
static uint32_t kick_seq;               /* host: the member we removed (its KICK goes again while the server still shows it) */
static uint64_t req_id;
static uint8_t cookie[16], token[16], sk[32], pk[32], salt[16], pw_key[32], peer_pk[32];
static RdvAddr my_pub;
static char room[RDV_CODE_LEN + 1], status[200], err[200];
static RdvSession ses;
static NetAddr path, cand[4];
static NetResolve* dns;
static char dns_name[128];
static struct { char name[128]; NetAddr a; uint32_t at; int ok; } dns_cache;

static const char* fmt_code(const char* c)
{
    static char s[12];
    snprintf(s, sizeof(s), "%.4s-%.4s", c, c + 4);
    return s;
}

static int same(const NetAddr* a, const NetAddr* b) { return a->ip == b->ip && a->port == b->port; }
static void to_rdv(const NetAddr* a, RdvAddr* r) { memset(r, 0, sizeof(*r)); r->fam = 4; memcpy(r->ip, &a->ip, 4); r->port = ntohs(a->port); }
static int from_rdv(const RdvAddr* r, NetAddr* a) { if (r->fam != 4) return 0; memcpy(&a->ip, r->ip, 4); a->port = htons(r->port); return 1; }

static void send_msg(const NetAddr* to, RdvMsg* m)
{
    uint8_t b[256];
    int n = rdv_build(b, sizeof(b), m);
    if (n) net_send(to, b, n);
}

static void forget_secrets(void)
{
    crypto_wipe(&ses, sizeof(ses));
    crypto_wipe(sk, 32);
    crypto_wipe(pw_key, 32);
    crypto_wipe(cfg.password, sizeof(cfg.password));
    crypto_wipe(token, 16);
    have_ses = 0;
}

static void send_close(void)
{
    RdvMsg m;
    memset(&m, 0, sizeof(m));
    m.type = RDV_CLOSE;
    m.room_id = room_id;
    memcpy(m.token, token, 16);
    send_msg(&cfg.server, &m);
    send_msg(&cfg.server, &m);
    crypto_wipe(m.token, 16);
}

static void fail(const char* why)
{
    if (room_id && st != RDV_ST_FAILED) send_close();   /* the server need not wait for the time-out */
    room_id = 0;
    snprintf(err, sizeof(err), "%s", why);
    snprintf(status, sizeof(status), "%s", why);
    st = RDV_ST_FAILED;
    forget_secrets();
    host_log("rdv: %s\n", why);
}

static void lan_addr(RdvAddr* r)
{
    char ip[48];
    NetAddr a = { 0, 0 };
    memset(r, 0, sizeof(*r));
    if (net_local_ip(ip, sizeof(ip)) && net_resolve(ip, net_local_port(), &a)) to_rdv(&a, r);   /* a numeric address: no DNS */
}

static void send_request(void)
{
    RdvMsg m;
    memset(&m, 0, sizeof(m));
    memcpy(m.cookie, cookie, 16);
    memcpy(m.pk, pk, 32);
    lan_addr(&m.lan);
    if (cfg.host) {
        m.type = RDV_CREATE;
        m.req_id = req_id;
        m.flags = flags;
        memcpy(m.salt, salt, 16);
        if (pw_used) rdv_verifier(pw_key, m.verifier);
    } else {
        m.type = RDV_JOIN;
        memcpy(m.code, cfg.code, RDV_CODE_LEN);
        memcpy(m.rejoin, cfg.rejoin, 16);
        if (pw_used) {
            uint8_t v[32];
            rdv_verifier(pw_key, v);
            rdv_proof(v, cookie, cfg.code, pk, m.proof);
            crypto_wipe(v, 32);
            m.flags = RDV_F_PW;
        }
    }
    send_msg(&cfg.server, &m);
    crypto_wipe(m.verifier, 32);
    t_sent = net_ms();
}

static void send_keepalive(void)
{
    RdvMsg m;
    memset(&m, 0, sizeof(m));
    m.type = RDV_KEEPALIVE;
    m.room_id = room_id;
    memcpy(m.token, token, 16);
    m.seq = member_seq;
    send_msg(&cfg.server, &m);
    t_ka = net_ms();
    if (!t_unans || (int)(t_unans - t_ack) <= 0) t_unans = t_ka | 1;
}

/* the server has not answered for `ms` and a keepalive has waited 2 s for its answer. Silence alone
   proves nothing: a stalled game thread (loading, a debugger) sent no keepalive meanwhile, and the
   timers run on its frames (review: a 5 s stall opened a new room with a new code) */
static int server_lost(uint32_t now, uint32_t ms)
{
    return now - t_ack > ms && t_unans && (int)(t_unans - t_ack) > 0 && now - t_unans >= 2000;
}

static void send_punch(const NetAddr* to)
{
    uint8_t b[64];
    RdvMsg m;
    int n;
    memset(&m, 0, sizeof(m));
    m.type = RDV_PUNCH;
    m.flags = heard;
    m.room_id = room_id;
    m.attempt = attempt & 0xFF;
    n = rdv_build(b, sizeof(b), &m);
    rdv_punch_mac(ses.punch, b, n - 16, b + n - 16);
    net_send(to, b, n);
}

/* a fresh identity for this registration: key pair, nonce, request id. 0: no randomness (failed) */
static int fresh_keys(void)
{
    if (!rdv_random(sk, 32) || !rdv_random(&nonce, sizeof(nonce)) || !rdv_random(&req_id, sizeof(req_id))) {
        fail("Cannot secure the connection. Try restarting the game.");
        return 0;
    }
    crypto_x25519_public_key(pk, sk);
    return 1;
}

/* from the top: HELLO, CREATE / JOIN (also after the server lost our room) */
static void begin_hello(void)
{
    st = RDV_ST_HELLO;
    t_start = net_ms();
    t_sent = t_start - RETRY_MS;
    snprintf(status, sizeof(status), "Contacting co-op server %s...", net_addr_str(&cfg.server));
}

/* the server name: "host" or "host:port"; 1 with host and port split off */
static int split_server(const char* s, char* host, int cap, int* port)
{
    char* colon;
    while (*s == ' ') s++;
    snprintf(host, cap, "%s", s);
    *port = RDV_DEFAULT_PORT;
    if ((colon = strrchr(host, ':'))) {
        char* end;
        long p = strtol(colon + 1, &end, 10);
        if (*end || p <= 0 || p > 65535) return 0;
        *port = (int)p;
        *colon = 0;
    }
    return host[0] != 0;
}

static int numeric_ip(const char* h, int port, NetAddr* out)
{
    unsigned a, b, c, d;
    char x;
    if (sscanf(h, "%u.%u.%u.%u%c", &a, &b, &c, &d, &x) != 4 || a > 255 || b > 255 || c > 255 || d > 255) return 0;
    out->ip = htonl(a << 24 | b << 16 | c << 8 | d);
    out->port = htons((uint16_t)port);
    return 1;
}

void rdv_start(const RdvConfig* c)
{
    rdv_stop();
    memset(&ses, 0, sizeof(ses));
    cfg = *c;
    if (cfg.punch_ms <= 0) cfg.punch_ms = 2000;
    relay = heard = confirmed = path_lan = have_ses = e2e = pw_used = n_cand = attempt = keys_ok = orphan = 0;
    lan_cand = -1;
    room_id = member_seq = relay_ctr = kick_seq = t_unans = 0;
    room[0] = err[0] = 0;
    if (!fresh_keys()) return;
    if (cfg.host && cfg.password[0]) {   /* the host's password key, once (Argon2id); the password itself is not kept */
        if (!rdv_random(salt, 16) || !rdv_pw_key(cfg.password, salt, pw_key)) { fail("Cannot secure the room password. Try restarting the game."); return; }
        pw_used = 1;
        crypto_wipe(cfg.password, sizeof(cfg.password));
    }
    /* the host's encryption policy, enforced here and never taken from the server: a password
       room is always encrypted (docs/SERVER.md "End-to-end encryption") */
    flags = cfg.host ? ((cfg.e2e || pw_used) ? RDV_F_E2E : 0) | (pw_used ? RDV_F_PW : 0) : 0;
    host_log("rdv: %s %s via %s\n", cfg.host ? "creating a room" : "joining room", cfg.host ? "" : fmt_code(cfg.code),
             cfg.server_name[0] ? cfg.server_name : net_addr_str(&cfg.server));
    if (cfg.server_name[0]) {   /* a name: resolved on a thread (the cache, or a numeric address, at once) */
        char host[128];
        int port;
        uint32_t now = net_ms();
        if (!split_server(cfg.server_name, host, sizeof(host), &port)) { fail("Invalid co-op server address. Use a name or name:port."); return; }
        if (numeric_ip(host, port, &cfg.server)) { begin_hello(); return; }
        if (dns_cache.ok && !strcmp(dns_cache.name, cfg.server_name) && now - dns_cache.at < DNS_CACHE_MS) { cfg.server = dns_cache.a; begin_hello(); return; }
        snprintf(dns_name, sizeof(dns_name), "%s", cfg.server_name);
        if (!(dns = net_resolve_start(host, port))) { fail("Cannot look up the co-op server. Try restarting the game."); return; }
        st = RDV_ST_RESOLVE;
        t_start = now;
        snprintf(status, sizeof(status), "Looking up co-op server %s...", host);
        return;
    }
    begin_hello();
}

void rdv_stop(void)
{
    if (st != RDV_ST_OFF && room_id && st != RDV_ST_FAILED) send_close();
    if (dns) { net_resolve_cancel(dns); dns = NULL; }
    st = RDV_ST_OFF;
    forget_secrets();
    room_id = 0;
    room[0] = 0;
    status[0] = 0;
}

static void send_kick(void)
{
    RdvMsg m;
    memset(&m, 0, sizeof(m));
    m.type = RDV_KICK;
    m.room_id = room_id;
    memcpy(m.token, token, 16);
    m.seq = kick_seq;
    send_msg(&cfg.server, &m);
    crypto_wipe(m.token, 16);
}

/* host: "Remove stuck player". The server frees the partner's place (the room and its code stay);
   here the session with it ends - its keys go, so nothing of it is decrypted or relayed any more -
   and we wait for the next introduction as after opening the room. */
void rdv_kick(void)
{
    if (!cfg.host || !room_id || st < RDV_ST_PUNCHING || st == RDV_ST_FAILED) return;
    kick_seq = member_seq;
    send_kick();
    send_kick();
    crypto_wipe(&ses, sizeof(ses));
    have_ses = keys_ok = confirmed = heard = relay = orphan = 0;
    n_cand = 0;
    st = RDV_ST_WAITING;
    t_ack = net_ms();
    t_ka = t_ack - KA_WAIT_MS;
    snprintf(status, sizeof(status), "Room %s is open.", fmt_code(room));
    host_log("rdv: partner (member %u) removed from room %s\n", kick_seq, fmt_code(room));
}

/* the host's room is gone from the server (restarted, or it timed us out): a new registration
   with a new key pair and a new code; the lobby / F3 show the new code (rdv_room) */
static void reregister(void)
{
    host_log("rdv: the co-op server lost room %s (restarted?): opening a new one\n", fmt_code(room));
    crypto_wipe(&ses, sizeof(ses));
    have_ses = keys_ok = orphan = confirmed = heard = relay = 0;
    room_id = member_seq = relay_ctr = kick_seq = 0;
    room[0] = 0;
    if (!fresh_keys()) return;
    begin_hello();
    snprintf(status, sizeof(status), "Server lost your room. Opening a new one...");
}

static void set_path_status(void)
{
    snprintf(status, sizeof(status), "%s%s%s%s", st == RDV_ST_DIRECT ? (path_lan ? "Connected directly (local network)" : "Connected directly")
             : st == RDV_ST_RELAY ? "Connected via server relay" : "Connecting to partner...",
             !e2e ? "" : keys_ok ? ", encrypted, check code " : ", encrypted (securing connection...)", e2e && keys_ok ? ses.verify : "",
             orphan ? " - the co-op server lost the room" : "");
}

static void confirm(const NetAddr* a)
{
    if (confirmed || cfg.force_relay) return;
    confirmed = 1;
    path = *a;
    path_lan = lan_cand >= 0 && same(a, &cand[lan_cand]);
    relay = 0;
    st = RDV_ST_DIRECT;
    heard = 1;
    send_punch(a);   /* one "I hear you" back: the partner may still wait for it (it gets no answer) */
    set_path_status();
    host_log("rdv: direct path to the partner %s%s after %u ms\n", net_addr_str(a), path_lan ? " (LAN)" : "", net_ms() - t_intro);
}

/* the partner proved it has the same keys (a punch MAC or an authenticated datagram): both sides
   saw the same introduction, and with a password the same password */
static void keys_confirmed(void)
{
    t_rx = net_ms();
    if (keys_ok) return;
    keys_ok = 1;
    set_path_status();
    host_log("rdv: the partner's keys match (introduction%s confirmed)\n", pw_used ? " and password" : "");
}

/* INTRO. The host checks that it is meant for it (its room, its token, the host role, a state that
   expects one) and takes only a newer member sequence (the epoch), at most EPOCH_STEP ahead: a
   repeat changes nothing, an older, too new or altered one is dropped, and every accepted one gets
   keys bound to its epoch, so counters never start again under the same keys. The joiner takes the first one. Encryption follows the
   local policy, not the INTRO's flags (docs/SERVER.md "End-to-end encryption"). */
#define EPOCH_STEP 64   /* epochs accepted: ours + 1 .. ours + EPOCH_STEP (no far-future epoch that blocks the real next ones) */
static void on_intro(const RdvMsg* m)
{
    NetAddr pub = { 0, 0 }, lan;
    RdvIntro in;
    RdvSession s;
    int same_nat, use_pw, enc;
    if (cfg.host) {
        if (m->role != 0 || st < RDV_ST_WAITING || st == RDV_ST_FAILED || m->room_id != room_id || crypto_verify16(m->token, token)) return;
        if (have_ses && m->seq == member_seq) return;   /* a repeat (the server resends it): state kept */
        if (m->seq <= member_seq) { host_log("rdv: an old introduction (member %u, we have %u) dropped\n", m->seq, member_seq); return; }
        if (m->seq - member_seq > EPOCH_STEP) { host_log("rdv: an introduction too far ahead (member %u, we have %u) dropped\n", m->seq, member_seq); return; }
        if (!(m->flags & RDV_F_E2E) != !(flags & RDV_F_E2E) || !(m->flags & RDV_F_PW) != !(flags & RDV_F_PW))
            host_log("rdv: the introduction's flags differ from ours (altered?): ours apply\n");
        use_pw = pw_used;
        enc = (flags & RDV_F_E2E) != 0;
        in.flags = flags;
    } else {
        if (m->role != 1 || st != RDV_ST_REQUEST) return;
        room_id = m->room_id;   /* we are in the room now: a refusal below leaves it (CLOSE) */
        memcpy(token, m->token, 16);
        enc = (m->flags & RDV_F_E2E) != 0;
        if (!enc && (cfg.e2e || cfg.password[0] || pw_used)) {   /* a plaintext session only when this side allows one */
            fail("Host connection is not encrypted. Ask the host to update the game.");
            return;
        }
        if ((cfg.password[0] || pw_used) && !(m->flags & RDV_F_PW)) {   /* we expect a password room: not some other room */
            fail("This room has no password. Clear PASSWORD to join.");
            return;
        }
        use_pw = (m->flags & RDV_F_PW) && pw_used;
        in.flags = m->flags;
    }
    in.room_id = m->room_id;
    in.epoch = m->seq;
    memcpy(in.code, cfg.host ? room : cfg.code, sizeof(in.code));
    if (!(cfg.host ? rdv_session(&s, 1, sk, pk, m->pk, use_pw ? pw_key : NULL, &in) : rdv_session(&s, 0, sk, m->pk, pk, use_pw ? pw_key : NULL, &in))) {
        host_log("rdv: an introduction with an unusable key dropped\n");
        crypto_wipe(&s, sizeof(s));
        return;
    }
    ses = s;
    crypto_wipe(&s, sizeof(s));
    room_id = m->room_id;
    member_seq = m->seq;
    memcpy(peer_pk, m->pk, 32);
    if (!cfg.host) memcpy(token, m->token, 16);
    e2e = enc;
    have_ses = 1;
    keys_ok = heard = confirmed = path_lan = 0;
    if (!cfg.host) relay_ctr = 0;   /* the server's replay window is per token: the joiner's is new, the host's stays */
    n_cand = 0;
    lan_cand = -1;
    t_ack = t_rx = net_ms();
    if (from_rdv(&m->pub, &pub)) {
        same_nat = m->pub.fam == my_pub.fam && !memcmp(m->pub.ip, my_pub.ip, 16);
        if (same_nat && from_rdv(&m->lan, &lan) && lan.ip) { lan_cand = n_cand; cand[n_cand++] = lan; }   /* behind the same router */
        cand[n_cand++] = pub;
        if (!same_nat && ntohs(pub.port) < 65534) {   /* sequential port allocation */
            NetAddr p = pub;
            p.port = htons(ntohs(pub.port) + 1); cand[n_cand++] = p;
            p.port = htons(ntohs(pub.port) + 2); cand[n_cand++] = p;
        }
    }
    relay = cfg.force_relay;
    st = relay ? RDV_ST_RELAY : RDV_ST_PUNCHING;
    t_intro = net_ms();
    t_punch = t_intro - PUNCH_EVERY_MS;
    set_path_status();
    host_log("rdv: partner introduced (member %u, public %s, %d candidates)%s%s\n", member_seq, net_addr_str(&pub), n_cand,
             relay ? ", relay forced" : "", e2e ? ", end-to-end encrypted" : "");
}

static void on_error(const RdvMsg* m)
{
    static const char* msgs[] = { "", "", "Room not found. Check the code.", "This room needs a password", "Wrong password",
                                  "The room is full", "Too many attempts. Wait a minute.", "", "The co-op server is full or busy",
                                  "Too many rooms from your address" };
    char b[120];
    switch (m->err) {
    case RDV_E_COOKIE: st = RDV_ST_HELLO; t_sent = net_ms() - RETRY_MS; return;   /* a new cookie */
    case RDV_E_NEED_PW:
    case RDV_E_BAD_PW:
        if (m->err == RDV_E_NEED_PW && !cfg.host && cfg.password[0] && !pw_used) {
            memcpy(salt, m->salt, 16);
            if (!rdv_pw_key(cfg.password, salt, pw_key)) { fail("Not enough memory to secure the password. Try restarting."); return; }
            pw_used = 1;
            crypto_wipe(cfg.password, sizeof(cfg.password));
            send_request();
            return;
        }
        fail(m->err == RDV_E_NEED_PW ? "Enter the room password in PASSWORD." : msgs[m->err]);
        return;
    case RDV_E_VERSION:
        snprintf(b, sizeof(b), "Server compatibility version: %d. Game: %d. Update the %s.", m->version, RDV_VERSION,
                 m->version > RDV_VERSION ? "game" : "server");
        fail(b);
        return;
    default: fail(msgs[m->err]); return;
    }
}

/* a game datagram from the partner (relayed or direct): decrypted into buf */
static int game_in(unsigned char* buf, int cap, const uint8_t* d, int n)
{
    static uint8_t tmp[RDV_DGRAM_MAX];
    if (!have_ses) return -1;
    if (e2e) {
        int k = rdv_open(&ses, tmp, sizeof(tmp), d, n);
        if (k <= 0 || k > cap) return -1;
        memcpy(buf, tmp, k);
        keys_confirmed();
        return k;
    }
    if (n < 4 || memcmp(d, "COOP", 4) || n > cap) return -1;
    memmove(buf, d, n);
    t_rx = net_ms();
    return n;
}

int rdv_input(NetAddr* from, unsigned char* buf, int n, int cap)
{
    RdvMsg m;
    uint32_t magic;
    int t;
    if (st == RDV_ST_OFF || n < 4) return 0;
    memcpy(&magic, buf, 4);
    if (magic == RDV_MAGIC) {
        if ((t = rdv_parse(buf, n, &m)) < 0) {
            if (t == RDV_BAD_VERSION && same(from, &cfg.server) && st >= RDV_ST_HELLO && st < RDV_ST_WAITING) {
                char b[120];
                snprintf(b, sizeof(b), "Server compatibility version: %d. Game: %d. Use compatible versions.", m.version, RDV_VERSION);
                fail(b);
            }
            return -1;
        }
        if (t == RDV_PUNCH) {
            uint8_t mac[16];
            if (!have_ses || m.room_id != room_id || cfg.no_punch || cfg.force_relay) return -1;   /* "Via server": no direct path, not even one the partner found */
            /* only while either side still punches: a punch recorded earlier and replayed from another
               address later must not move the path there (the MAC has no counter) */
            if (net_ms() - t_intro > (uint32_t)cfg.punch_ms + PUNCH_MORE_MS + 2000) return -1;
            rdv_punch_mac(ses.punch, buf, n - 16, mac);
            if (crypto_verify16(mac, m.mac)) return -1;
            if (!heard) host_log("rdv: heard the partner at %s\n", net_addr_str(from));
            heard = 1;
            keys_confirmed();
            if (m.flags & 1) confirm(from);
            else send_punch(from);   /* it has not heard us yet: an answer it can confirm the path with */
            return -1;
        }
        if (!same(from, &cfg.server) || st == RDV_ST_FAILED || st == RDV_ST_RESOLVE) return -1;
        switch (t) {
        case RDV_WELCOME:
            if (st != RDV_ST_HELLO || m.nonce != nonce) return -1;
            memcpy(cookie, m.cookie, 16);
            my_pub = m.pub;
            st = RDV_ST_REQUEST;
            snprintf(status, sizeof(status), cfg.host ? "Opening a room on the co-op server..." : "Looking for room %s...", fmt_code(cfg.code));
            send_request();
            return -1;
        case RDV_ROOM:
            if (!cfg.host || m.req_id != req_id || st != RDV_ST_REQUEST) return -1;
            room_id = m.room_id;
            memcpy(token, m.token, 16);
            memcpy(room, m.code, sizeof(room));
            st = RDV_ST_WAITING;
            t_ka = t_ack = net_ms();
            snprintf(status, sizeof(status), "Room %s is open.", fmt_code(room));
            host_log("rdv: room %s\n", fmt_code(room));
            return -1;
        case RDV_INTRO:
            if (cfg.host && m.room_id == room_id && !crypto_verify16(m.token, token)) t_ack = net_ms();   /* the server knows the room */
            on_intro(&m);
            return -1;
        case RDV_ERROR: if (st == RDV_ST_HELLO || st == RDV_ST_REQUEST) on_error(&m); return -1;
        case RDV_KEEPALIVE:   /* the server's answer: it still has the room */
            if ((m.flags & 0x80) && room_id && m.room_id == room_id) {
                t_ack = net_ms();
                if (cfg.host && m.has_member && kick_seq && kick_seq == member_seq && st == RDV_ST_WAITING) send_kick();   /* the KICK was lost */
            }
            return -1;
        case RDV_RELAY:
            if (!room_id || m.room_id != room_id || (n = game_in(buf, cap, m.payload, m.payload_len)) < 0) return -1;
            *from = rdv_peer;
            return n;
        default: return -1;
        }
    }
    if (!have_ses) return 0;
    if (magic == RDV_E2E_MAGIC) {   /* direct and authenticated: the partner sends here, so it heard us */
        if (!e2e || (n = game_in(buf, cap, buf, n)) < 0) return 0;   /* not with our keys: maybe a direct joiner's (coop_glue.c CoopDirect) */
        confirm(from);
        *from = rdv_peer;
        return n;
    }
    if (confirmed && same(from, &path) && !memcmp(buf, "COOP", 4)) {   /* unencrypted session: the path only */
        if (e2e) return -1;
        t_rx = net_ms();
        *from = rdv_peer;
        return n;
    }
    return 0;
}

int rdv_send(const void* buf, int n)
{
    static uint8_t b[RDV_DGRAM_MAX + RDV_RELAY_HDR];
    uint8_t* p = b + RDV_RELAY_HDR;
    int k = n;
    if (!have_ses || (st != RDV_ST_DIRECT && st != RDV_ST_RELAY)) return 0;
    if (e2e) { if (!(k = rdv_seal(&ses, p, RDV_DGRAM_MAX - RDV_RELAY_HDR, buf, n))) return 0; }
    else if (n > RDV_DGRAM_MAX - RDV_RELAY_HDR) return 0;
    else memcpy(p, buf, n);
    if (relay) {   /* the RELAY header in front of the payload already in place: counter and MAC with our token */
        if (orphan) return 0;   /* the server lost the room: nothing to relay through */
        return net_send(&cfg.server, b, rdv_relay_seal(b, k, room_id, ++relay_ctr, token));
    }
    return net_send(&path, p, k);
}

void rdv_frame(void)
{
    uint32_t now = net_ms();
    int k;
    if (now - t_frame > 1000) t_unans = 0;   /* a stall: what was sent before it waits afresh */
    t_frame = now;
    switch (st) {
    case RDV_ST_RESOLVE: {
        int r = net_resolve_poll(dns, &cfg.server);
        if (r > 0) {
            dns = NULL;
            snprintf(dns_cache.name, sizeof(dns_cache.name), "%s", dns_name);
            dns_cache.a = cfg.server;
            dns_cache.at = now;
            dns_cache.ok = 1;
            begin_hello();
        } else if (r < 0 || now - t_start > RESOLVE_MS) {
            char b[200];
            if (!r) net_resolve_cancel(dns);
            dns = NULL;
            snprintf(b, sizeof(b), "Cannot find co-op server %s%s. Check the address.", dns_name, r ? "" : " (lookup timed out)");
            fail(b);
        }
        return;
    }
    case RDV_ST_HELLO:
    case RDV_ST_REQUEST:
        if (now - t_start > GIVE_UP_MS) {
            char b[160];
            snprintf(b, sizeof(b), "No reply from co-op server %s. Check port %d and your connection.%s", net_addr_str(&cfg.server),
                     ntohs(cfg.server.port), cfg.host ? " - direct joins still work" : "");
            fail(b);
            return;
        }
        if (now - t_sent < RETRY_MS) return;
        if (st == RDV_ST_REQUEST) { send_request(); return; }
        {
            RdvMsg m;
            memset(&m, 0, sizeof(m));
            m.type = RDV_HELLO;
            m.nonce = nonce;
            send_msg(&cfg.server, &m);
            t_sent = now;
        }
        return;
    case RDV_ST_WAITING:
        if (server_lost(now, LOST_WAIT_MS)) { reregister(); return; }   /* the server forgot the room */
        if (now - t_ka >= KA_WAIT_MS) send_keepalive();
        return;
    case RDV_ST_PUNCHING:
    case RDV_ST_RELAY:
    case RDV_ST_DIRECT:
        if (!orphan && server_lost(now, LOST_MS)) {   /* the server lost the room (restarted?) */
            if (st == RDV_ST_DIRECT) {   /* the direct session needs no server: it goes on */
                orphan = 1;
                set_path_status();
                host_log("rdv: the co-op server lost the room; the direct session goes on\n");
            } else if (cfg.host) { reregister(); return; }   /* a relayed session is over: a new room for the next partner */
            else { fail("Server lost the room. Your relayed session has ended."); return; }
        }
        if (orphan) {
            if (cfg.host && now - t_rx > ORPHAN_IDLE_MS) reregister();   /* the partner is gone too: a new room */
            return;
        }
        if (now - t_ka >= KA_MS) send_keepalive();
        if (confirmed || cfg.force_relay) return;
        if (st == RDV_ST_PUNCHING && now - t_intro >= (uint32_t)cfg.punch_ms) {
            relay = 1;
            st = RDV_ST_RELAY;
            set_path_status();
            host_log("rdv: no direct path after %d ms: using the relay\n", cfg.punch_ms);
        }
        if (now - t_intro > (uint32_t)cfg.punch_ms + PUNCH_MORE_MS) return;
        if (now - t_punch < (uint32_t)(relay ? 10 * PUNCH_EVERY_MS : PUNCH_EVERY_MS)) return;
        t_punch = now;
        attempt++;
        for (k = 0; k < n_cand; k++) send_punch(&cand[k]);
        return;
    }
}

int rdv_active(void) { return st != RDV_ST_OFF; }
int rdv_state(void) { return st; }
int rdv_overhead(void) { return st == RDV_ST_OFF ? 0 : RDV_RELAY_HDR + RDV_E2E_OVER; }
int rdv_wire_extra(void) { return have_ses ? (e2e ? RDV_E2E_OVER : 0) + (relay ? RDV_RELAY_HDR : 0) : 0; }
const char* rdv_status(void) { return status; }
const char* rdv_room(void) { return room; }
const char* rdv_path(void) { return st == RDV_ST_DIRECT ? (path_lan ? "direct (LAN)" : "direct") : st == RDV_ST_RELAY ? "relay" : ""; }
const char* rdv_error(void) { return st == RDV_ST_FAILED ? err : NULL; }
int rdv_keys_confirmed(void) { return have_ses && keys_ok; }
const RdvSession* rdv_session_state(void) { return have_ses ? &ses : NULL; }

int rdv_server_parse(const char* s, NetAddr* out)
{
    char host[128];
    int port;
    return split_server(s, host, sizeof(host), &port) && net_resolve(host, port, out);
}

int rdv_server_valid(const char* s)
{
    char host[128];
    int port;
    return split_server(s, host, sizeof(host), &port);
}
