/* The rendezvous client state machine (pc/src/net/rendezvous.c) against the real server core, on a
   fake network and clock: deterministic. The partner is played by the test with rdv_proto.c's
   functions, so it can alter, reorder and replay what the client sees. Covers docs/SERVER.md
   "Security": INTRO validation and epochs, the encryption policy (no downgrade), counters never
   restarting under the same keys, member replacement, relay frames, punching and the late switch
   from the relay to a direct path, server restart, DNS on a thread with a deadline, no randomness. */
#include "../server/server.h"
#include "../src/net/rendezvous.h"
#include "monocypher.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

static int fails;
#define CHECK(c) do { if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---------------------------------------------------------------- the fake network */
static uint32_t clk = 1000;
static int quiet = 1;
const char* tr(const char* en) { return en; }   /* (host/tr.c: the game's language) */
void host_log(const char* fmt, ...) { va_list a; if (quiet) return; va_start(a, fmt); vprintf(fmt, a); va_end(a); }
uint32_t net_ms(void) { return clk; }
int net_local_ip(char* out, int cap) { (void)out; (void)cap; return 0; }
int net_local_port(void) { return 40000; }
int net_resolve(const char* host, int port, NetAddr* out) { (void)host; (void)port; (void)out; return 0; }
const char* net_addr_str(const NetAddr* a)
{
    static char s[2][32];
    static int k;
    uint32_t ip = ntohl(a->ip);
    k ^= 1;
    snprintf(s[k], sizeof(s[k]), "%u.%u.%u.%u:%u", ip >> 24, ip >> 16 & 255, ip >> 8 & 255, ip & 255, ntohs(a->port));
    return s[k];
}
struct NetResolve { int state; NetAddr a; };
static struct NetResolve dns_job;
static int dns_starts;
NetResolve* net_resolve_start(const char* host, int port) { (void)host; dns_job.state = 0; dns_job.a.port = htons((uint16_t)port); dns_starts++; return &dns_job; }
int net_resolve_poll(NetResolve* r, NetAddr* out) { if (r->state > 0) *out = r->a; return r->state; }
void net_resolve_cancel(NetResolve* r) { (void)r; }

typedef struct { NetAddr to; uint8_t b[RDV_DGRAM_MAX + 64]; int n; } Pkt;
static Pkt cli_out[512];        /* what the client sent */
static int ncli;
int net_send(const NetAddr* to, const void* buf, int len)
{
    if (ncli < 512 && len <= (int)sizeof(cli_out[0].b)) { cli_out[ncli].to = *to; memcpy(cli_out[ncli].b, buf, len); cli_out[ncli].n = len; ncli++; }
    return 1;
}

static Pkt srv_out[512];        /* what the server sent */
static int nsrv;
static void srv_send(const RdvAddr* to, const uint8_t* b, int n)
{
    if (nsrv < 512) { memcpy(&srv_out[nsrv].to.ip, to->ip, 4); srv_out[nsrv].to.port = htons(to->port); memcpy(srv_out[nsrv].b, b, n); srv_out[nsrv].n = n; nsrv++; }
}

static NetAddr na(uint32_t ip, int port) { NetAddr a; a.ip = htonl(ip); a.port = htons((uint16_t)port); return a; }
static RdvAddr ra(const NetAddr* a) { RdvAddr r; memset(&r, 0, sizeof(r)); r.fam = 4; memcpy(r.ip, &a->ip, 4); r.port = ntohs(a->port); return r; }
static int same(const NetAddr* a, const NetAddr* b) { return a->ip == b->ip && a->port == b->port; }

static NetAddr SRV, CLI, PEER, PEER2;
static int drop_to_client_type;   /* a CVXS type the network holds back on the way to the client (0: none) */
static uint8_t held[RDV_DGRAM_MAX];
static int held_n;
static int drop_punches;          /* punches between client and partner are lost */

/* the test's partner (a second game, played by hand) */
typedef struct { NetAddr addr; uint8_t sk[32], pk[32], other_pk[32], cookie[16], token[16]; RdvSession ses; uint32_t room_id, ctr; int have; } Peer;
static Peer p1, p2;
static int peer_got;              /* game datagrams the partner received from the client */
static uint8_t peer_last[RDV_DGRAM_MAX];
static int peer_last_n;

static void deliver_to_peer(Peer* p, const uint8_t* b, int n)
{
    uint8_t o[RDV_DGRAM_MAX];
    RdvMsg m;
    int k;
    if (n >= 4 && !memcmp(b, "CVXS", 4) && rdv_parse(b, n, &m) == RDV_RELAY) { b = m.payload; n = m.payload_len; }
    else if (n >= 4 && !memcmp(b, "CVXS", 4)) return;   /* punches: the test looks at them itself */
    if (!p->have || (k = rdv_open(&p->ses, o, sizeof(o), b, n)) <= 0) return;
    peer_got++;
    memcpy(peer_last, o, k);
    peer_last_n = k;
}

/* everything in flight, until nothing moves: client <-> server, server -> the partners */
static void pump(void)
{
    int moved = 1, i;
    while (moved) {
        Pkt c[512], s[512];
        int nc = ncli, ns = nsrv;
        moved = 0;
        memcpy(c, cli_out, nc * sizeof(Pkt)); ncli = 0;
        memcpy(s, srv_out, ns * sizeof(Pkt)); nsrv = 0;
        for (i = 0; i < nc; i++) {
            moved = 1;
            if (same(&c[i].to, &SRV)) { RdvAddr f = ra(&CLI); srv_input(&f, c[i].b, c[i].n, clk); }
            else if (drop_punches && c[i].n == 8 + 21 && !memcmp(c[i].b, "CVXS", 4)) continue;
            else if (same(&c[i].to, &p1.addr)) deliver_to_peer(&p1, c[i].b, c[i].n);
            else if (same(&c[i].to, &p2.addr)) deliver_to_peer(&p2, c[i].b, c[i].n);
        }
        for (i = 0; i < ns; i++) {
            moved = 1;
            if (same(&s[i].to, &CLI)) {
                NetAddr from = SRV;
                uint8_t b[RDV_DGRAM_MAX + 64];
                if (drop_to_client_type && s[i].n > 6 && s[i].b[6] == drop_to_client_type) { memcpy(held, s[i].b, s[i].n); held_n = s[i].n; continue; }
                memcpy(b, s[i].b, s[i].n);
                rdv_input(&from, b, s[i].n, sizeof(b));
            } else if (same(&s[i].to, &p1.addr)) deliver_to_peer(&p1, s[i].b, s[i].n);
            else if (same(&s[i].to, &p2.addr)) deliver_to_peer(&p2, s[i].b, s[i].n);
        }
    }
}

static void run(uint32_t ms)   /* time passes, frame by frame (33 ms) */
{
    uint32_t end = clk + ms;
    while ((int32_t)(end - clk) > 0) { clk += 33; srv_tick(clk); rdv_frame(); pump(); }
}

/* one request from the partner to the server, its reply */
static int peer_ask(Peer* p, const RdvMsg* q, RdvMsg* r)
{
    uint8_t b[RDV_DGRAM_MAX];
    RdvAddr f = ra(&p->addr);
    int n = rdv_build(b, sizeof(b), q), i, t = 0;
    srv_input(&f, b, n, clk);
    for (i = 0; i < nsrv; i++)
        if (same(&srv_out[i].to, &p->addr) && !t) t = rdv_parse(srv_out[i].b, srv_out[i].n, r);
    {   /* the rest (an INTRO for the client) stays in flight */
        int k = 0;
        for (i = 0; i < nsrv; i++) if (!same(&srv_out[i].to, &p->addr)) srv_out[k++] = srv_out[i];
        nsrv = k;
    }
    return t;
}

static void peer_new(Peer* p, NetAddr a)
{
    memset(p, 0, sizeof(*p));
    p->addr = a;
    p->sk[0] = (uint8_t)a.port; p->sk[1] = 77;
    crypto_x25519_public_key(p->pk, p->sk);
}

static int peer_hello(Peer* p)
{
    RdvMsg q = { .type = RDV_HELLO }, r;
    if (peer_ask(p, &q, &r) != RDV_WELCOME) return 0;
    memcpy(p->cookie, r.cookie, 16);
    return 1;
}

/* the partner joins room `code`; with its INTRO it derives the session the client must match */
static int peer_join(Peer* p, const char* code, const char* pw)
{
    RdvMsg q = { .type = RDV_JOIN }, r;
    uint8_t pwk[32];
    int t;
    if (!peer_hello(p)) return 0;
    memcpy(q.cookie, p->cookie, 16);
    memcpy(q.code, code, RDV_CODE_LEN + 1);
    memcpy(q.pk, p->pk, 32);
    t = peer_ask(p, &q, &r);
    if (t == RDV_ERROR && r.err == RDV_E_NEED_PW && pw) {
        uint8_t v[32];
        rdv_pw_key(pw, r.salt, pwk);
        rdv_verifier(pwk, v);
        rdv_proof(v, q.cookie, q.code, q.pk, q.proof);
        q.flags = RDV_F_PW;
        t = peer_ask(p, &q, &r);
    }
    if (t != RDV_INTRO) return t;
    {
        RdvIntro in = { r.room_id, r.seq, r.flags, "" };
        memcpy(in.code, code, RDV_CODE_LEN + 1);
        rdv_session(&p->ses, 0, p->sk, r.pk, p->pk, (r.flags & RDV_F_PW) ? pwk : NULL, &in);
    }
    memcpy(p->token, r.token, 16);
    memcpy(p->other_pk, r.pk, 32);
    p->room_id = r.room_id;
    p->have = 1;
    return RDV_INTRO;
}

/* the partner hosts a room (flags RDV_F_E2E or 0, password or not); the code into `code` */
static int peer_host(Peer* p, int flags, const char* pw, char* code, uint8_t pwk[32])
{
    RdvMsg q = { .type = RDV_CREATE, .req_id = 99 }, r;
    if (!peer_hello(p)) return 0;
    memcpy(q.cookie, p->cookie, 16);
    memcpy(q.pk, p->pk, 32);
    q.flags = flags;
    if (pw) { q.flags |= RDV_F_PW; memset(q.salt, 5, 16); rdv_pw_key(pw, q.salt, pwk); rdv_verifier(pwk, q.verifier); }
    if (peer_ask(p, &q, &r) != RDV_ROOM) return 0;
    memcpy(code, r.code, RDV_CODE_LEN + 1);
    memcpy(p->token, r.token, 16);
    p->room_id = r.room_id;
    return 1;
}

/* a game datagram from the partner to the client: direct (sealed) or relayed (+ the frame) */
static int peer_send(Peer* p, int relayed, const char* text)
{
    uint8_t b[RDV_DGRAM_MAX + 64], o[RDV_DGRAM_MAX];
    int n = rdv_seal(&p->ses, b + RDV_RELAY_HDR, RDV_DGRAM_MAX, text, (int)strlen(text) + 1), k;
    NetAddr from = p->addr;
    if (relayed) {   /* through the server, which checks the frame */
        RdvAddr f = ra(&p->addr);
        n = rdv_relay_seal(b, n, p->room_id, ++p->ctr, p->token);
        srv_input(&f, b, n, clk);
        pump();
        return -2;
    }
    memmove(b, b + RDV_RELAY_HDR, n);
    k = rdv_input(&from, b, n, sizeof(b));
    if (k > 0) { memcpy(o, b, k); CHECK(!strcmp((char*)o, text) && same(&from, &rdv_peer)); }
    return k;
}

static RdvConfig base(int host)
{
    RdvConfig c;
    memset(&c, 0, sizeof(c));
    c.host = host;
    c.e2e = 1;
    snprintf(c.server_name, sizeof(c.server_name), "10.0.0.9");   /* numeric: no DNS */
    return c;
}

/* the INTRO the server last sent to the client (captured by dropping it) */
static int grab_intro(uint8_t* b)
{
    int i;
    if (held_n) { memcpy(b, held, held_n); i = held_n; held_n = 0; return i; }
    for (i = nsrv - 1; i >= 0; i--)
        if (same(&srv_out[i].to, &CLI) && srv_out[i].n > 6 && srv_out[i].b[6] == RDV_INTRO) { memcpy(b, srv_out[i].b, srv_out[i].n); return srv_out[i].n; }
    return 0;
}

static void client_gets(const uint8_t* b, int n)
{
    uint8_t c[RDV_DGRAM_MAX + 64];
    NetAddr from = SRV;
    memcpy(c, b, n);
    rdv_input(&from, c, n, sizeof(c));
}

static void reset_server(void)
{
    SrvConfig c;
    srv_defaults(&c);
    c.req_per_sec = 1000;
    srv_init(&c, srv_send);
    nsrv = ncli = 0;
}

/* ---------------------------------------------------------------- the host */
static void test_host(void)
{
    RdvConfig c = base(1);
    RdvMsg m;
    uint8_t intro[256], b[256], tx_before[32];
    int n, k;
    char room[12];
    reset_server();
    rdv_start(&c);
    run(200);
    CHECK(rdv_state() == RDV_ST_WAITING && strlen(rdv_room()) == 8);
    snprintf(room, sizeof(room), "%s", rdv_room());

    /* a partner joins: the client's INTRO is held back to play with it */
    peer_new(&p1, PEER);
    drop_to_client_type = 0;
    CHECK(peer_join(&p1, room, NULL) == RDV_INTRO);
    n = grab_intro(intro);
    CHECK(n > 0);
    nsrv = 0;
    rdv_parse(intro, n, &m);

    /* alterations that must change nothing: another role, another room, another token */
    memcpy(b, intro, n); b[8 + 8] = 1; client_gets(b, n);                        /* role 1 */
    CHECK(rdv_state() == RDV_ST_WAITING);
    memcpy(b, intro, n); b[8] ^= 1; client_gets(b, n);                           /* room id */
    CHECK(rdv_state() == RDV_ST_WAITING);
    memcpy(b, intro, n); b[8 + 10] ^= 1; client_gets(b, n);                      /* token */
    CHECK(rdv_state() == RDV_ST_WAITING);
    {   /* from another source than the server */
        NetAddr from = PEER;
        memcpy(b, intro, n);
        rdv_input(&from, b, n, sizeof(b));
        CHECK(rdv_state() == RDV_ST_WAITING);
    }
    /* the real one, with the E2E flag cleared on the way: the host encrypts anyway (its own
       policy, and its own flags in the keys: the alteration changes nothing at all) */
    memcpy(b, intro, n); b[8 + 9] &= ~RDV_F_E2E; client_gets(b, n);
    CHECK(rdv_state() == RDV_ST_PUNCHING && rdv_wire_extra() == RDV_E2E_OVER);
    peer_got = 0;
    k = peer_send(&p1, 0, "COOP hello");
    CHECK(k > 0 && rdv_keys_confirmed());
    CHECK(strstr(rdv_status(), rdv_session_state()->verify) && !strcmp(p1.ses.verify, rdv_session_state()->verify));   /* the code to compare by voice */
    CHECK(rdv_state() == RDV_ST_DIRECT);   /* an authenticated datagram straight from the partner: the path (no punch needed) */
    /* counters: the client sends; the server's repeat (same epoch) and an older epoch change nothing */
    rdv_send("COOPa", 6); rdv_send("COOPb", 6);
    pump();
    CHECK(rdv_session_state()->tx_ctr == 2 && peer_got == 2 && !strcmp((char*)peer_last, "COOPb"));
    memcpy(b, intro, n); client_gets(b, n);
    {
        RdvMsg o = m;
        o.seq = 0;
        k = rdv_build(b, sizeof(b), &o); client_gets(b, k);
    }
    CHECK(rdv_session_state()->tx_ctr == 2 && rdv_state() == RDV_ST_DIRECT);
    rdv_send("COOPc", 6);
    pump();
    CHECK(rdv_session_state()->tx_ctr == 3 && peer_got == 3);
    /* the replayed introduction with a newer epoch (the review's attack: the same partner key, the
       counters would start again): new keys bound to the epoch, so no nonce is used twice under a key */
    {
        RdvMsg f = m;
        uint8_t fb[256];
        int fn;
        const RdvSession* s;
        f.seq = m.seq + 1;
        fn = rdv_build(fb, sizeof(fb), &f);
        memcpy(tx_before, rdv_session_state()->tx, 32);
        client_gets(fb, fn);
        s = rdv_session_state();
        CHECK(s && memcmp(s->tx, tx_before, 32) && s->tx_ctr == 0);
        CHECK(peer_send(&p1, 0, "COOP old keys") == 0);   /* the partner's old keys no longer fit (0: not ours, left to a direct session's keys: coop_glue.c CoopDirect) */
        {   /* a partner really introduced at that epoch matches */
            RdvIntro in = { m.room_id, f.seq, m.flags, "" };
            memcpy(in.code, room, sizeof(in.code));
            rdv_session(&p1.ses, 0, p1.sk, p1.other_pk, p1.pk, NULL, &in);
            CHECK(!memcmp(p1.ses.rx, s->tx, 32));
        }
        CHECK(peer_send(&p1, 0, "COOP new epoch") > 0);
        /* a far-future epoch (more than 64 ahead) is dropped, so it cannot block the real next ones */
        memcpy(tx_before, s->tx, 32);
        f.seq = m.seq + 1 + 65;
        fn = rdv_build(fb, sizeof(fb), &f); client_gets(fb, fn);
        CHECK(!memcmp(rdv_session_state()->tx, tx_before, 32) && peer_send(&p1, 0, "COOP still") > 0);
        f.seq = m.seq + 1 + 64;   /* the edge is taken */
        fn = rdv_build(fb, sizeof(fb), &f); client_gets(fb, fn);
        CHECK(memcmp(rdv_session_state()->tx, tx_before, 32) && rdv_session_state()->tx_ctr == 0);
    }
    rdv_stop();

    /* the partner leaves, another one joins: member 2 (server), new keys, works */
    reset_server();
    rdv_start(&c);
    run(200);
    snprintf(room, sizeof(room), "%s", rdv_room());
    peer_new(&p1, PEER);
    CHECK(peer_join(&p1, room, NULL) == RDV_INTRO);
    pump();
    CHECK(peer_send(&p1, 0, "COOP first") > 0);
    {
        RdvMsg cl = { .type = RDV_CLOSE, .room_id = p1.room_id }, r;
        memcpy(cl.token, p1.token, 16);
        peer_ask(&p1, &cl, &r);
        peer_new(&p2, PEER2);
        CHECK(peer_join(&p2, room, NULL) == RDV_INTRO);
        pump();
        CHECK(peer_send(&p2, 0, "COOP second") > 0);
        CHECK(peer_send(&p1, 0, "COOP old") == 0);   /* the old partner's keys are gone (not opened: 0, as above) */
    }
    /* "Remove stuck player": the server frees the place, the session's keys go, the room and its
       code stay; the next partner joins with the same code (protocol 3) */
    rdv_kick();
    pump();
    CHECK(srv_stats()->members == 0 && srv_stats()->kicked == 1 && rdv_state() == RDV_ST_WAITING && !rdv_session_state());
    CHECK(peer_send(&p2, 0, "COOP stuck") <= 0 && !strcmp(rdv_room(), room));
    run(4000);   /* keepalives go on, the room stays */
    CHECK(rdv_state() == RDV_ST_WAITING && srv_stats()->rooms == 1);
    peer_new(&p1, PEER);
    CHECK(peer_join(&p1, room, NULL) == RDV_INTRO);
    pump();
    CHECK(rdv_state() >= RDV_ST_PUNCHING && peer_send(&p1, 0, "COOP after the kick") > 0);
    rdv_stop();
}

/* relay forced, relay frames, the late switch to a direct path */
static void test_paths(void)
{
    RdvConfig c = base(1);
    char room[12];
    reset_server();
    c.force_relay = 1;
    rdv_start(&c);
    run(200);
    snprintf(room, sizeof(room), "%s", rdv_room());
    peer_new(&p1, PEER);
    CHECK(peer_join(&p1, room, NULL) == RDV_INTRO);
    pump();
    CHECK(rdv_state() == RDV_ST_RELAY);
    peer_got = 0;
    rdv_send("COOPviarelay", 13);   /* the client's frame: the server checks its MAC and counter and forwards */
    pump();
    CHECK(peer_got == 1 && !strcmp((char*)peer_last, "COOPviarelay") && srv_stats()->relay_out_dgrams == 1 && srv_stats()->drop_relay_auth == 0);
    run(100);   /* the partner's relay budget starts empty and fills */
    peer_send(&p1, 1, "COOPback");
    CHECK(srv_stats()->relay_out_dgrams == 2);
    rdv_stop();

    /* Automatic: punches lost for 2.5 s -> the relay; a punch that arrives later -> direct */
    reset_server();
    c.force_relay = 0;
    rdv_start(&c);
    run(200);
    snprintf(room, sizeof(room), "%s", rdv_room());
    peer_new(&p1, PEER);
    CHECK(peer_join(&p1, room, NULL) == RDV_INTRO);
    drop_punches = 1;
    pump();
    run(2500);
    CHECK(rdv_state() == RDV_ST_RELAY);
    drop_punches = 0;
    {   /* the partner's punch with "I hear you", 5 s in */
        uint8_t b[64];
        RdvMsg m = { .type = RDV_PUNCH, .flags = 1, .room_id = p1.room_id, .attempt = 3 };
        NetAddr from = PEER;
        int n = rdv_build(b, sizeof(b), &m);
        run(2500);
        rdv_punch_mac(p1.ses.punch, b, n - 16, b + n - 16);
        rdv_input(&from, b, n, sizeof(b));
        CHECK(rdv_state() == RDV_ST_DIRECT && rdv_keys_confirmed());
        b[n - 1] ^= 1;   /* a forged punch from elsewhere changes nothing */
        from = PEER2;
        rdv_input(&from, b, n, sizeof(b));
        CHECK(rdv_state() == RDV_ST_DIRECT);
    }
    rdv_stop();

    /* a valid punch after both sides stopped punching (2 s + 10 s): a recorded one replayed from
       another address does not move the relayed session there */
    reset_server();
    rdv_start(&c);
    run(200);
    snprintf(room, sizeof(room), "%s", rdv_room());
    peer_new(&p1, PEER);
    CHECK(peer_join(&p1, room, NULL) == RDV_INTRO);
    drop_punches = 1;
    pump();
    run(15000);
    drop_punches = 0;
    {
        uint8_t b[64];
        RdvMsg m = { .type = RDV_PUNCH, .flags = 1, .room_id = p1.room_id, .attempt = 3 };
        NetAddr from = PEER2;
        int n = rdv_build(b, sizeof(b), &m);
        rdv_punch_mac(p1.ses.punch, b, n - 16, b + n - 16);
        rdv_input(&from, b, n, sizeof(b));
        CHECK(rdv_state() == RDV_ST_RELAY);
    }
    rdv_stop();
}

/* ---------------------------------------------------------------- the joiner */
static void test_joiner(void)
{
    RdvConfig c;
    char code[12];
    uint8_t pwk[32];
    /* the host does not encrypt: refused (this game requires encryption) */
    reset_server();
    peer_new(&p1, PEER);
    CHECK(peer_host(&p1, 0, NULL, code, pwk));
    c = base(0);
    memcpy(c.code, code, 9);
    rdv_start(&c);
    run(300);
    CHECK(rdv_error() && strstr(rdv_error(), "not encrypted"));
    CHECK(srv_stats()->members == 0);   /* it left the room at once (CLOSE) */
    rdv_stop();
    /* both sides allow plaintext: works */
    c.e2e = 0;
    rdv_start(&c);
    run(300);
    CHECK(!rdv_error() && (rdv_state() == RDV_ST_PUNCHING || rdv_state() == RDV_ST_RELAY) && rdv_wire_extra() == 0);
    rdv_stop();

    /* a password typed, the room has none (a server could have swapped rooms): refused */
    reset_server();
    CHECK(peer_host(&p1, RDV_F_E2E, NULL, code, pwk));
    c = base(0);
    memcpy(c.code, code, 9);
    snprintf(c.password, sizeof(c.password), "hunter2");
    rdv_start(&c);
    run(300);
    CHECK(rdv_error() && strstr(rdv_error(), "no password"));
    rdv_stop();

    /* a password room whose INTRO lost its E2E flag on the way: refused, never plaintext */
    reset_server();
    CHECK(peer_host(&p1, RDV_F_E2E, "hunter2", code, pwk));
    c = base(0);
    c.e2e = 0;   /* even a joiner that allows plaintext */
    memcpy(c.code, code, 9);
    snprintf(c.password, sizeof(c.password), "hunter2");
    drop_to_client_type = RDV_INTRO;
    rdv_start(&c);
    run(1000);   /* NEED_PW, the proof, ... the INTRO is held back */
    {
        uint8_t b[256];
        int n = grab_intro(b);
        CHECK(n > 0);
        nsrv = 0;
        drop_to_client_type = 0;
        b[8 + 9] &= ~RDV_F_E2E;
        client_gets(b, n);
    }
    CHECK(rdv_error() && strstr(rdv_error(), "not encrypted"));
    rdv_stop();

    /* the right password: the keys match the host's (password-bound) */
    reset_server();
    CHECK(peer_host(&p1, RDV_F_E2E, "hunter2", code, pwk));
    c = base(0);
    memcpy(c.code, code, 9);
    snprintf(c.password, sizeof(c.password), "hunter2");
    rdv_start(&c);
    run(1000);
    {   /* the host's side: its INTRO from the server (role 0) */
        RdvMsg ka = { .type = RDV_KEEPALIVE, .room_id = p1.room_id }, r;
        memcpy(ka.token, p1.token, 16);
        CHECK(peer_ask(&p1, &ka, &r) == RDV_INTRO && r.role == 0);
        {
            RdvIntro in = { r.room_id, r.seq, r.flags, "" };
            memcpy(in.code, code, 9);
            CHECK(rdv_session(&p1.ses, 1, p1.sk, p1.pk, r.pk, pwk, &in));
            p1.have = 1;
        }
        CHECK(peer_send(&p1, 0, "COOP pw") > 0 && rdv_keys_confirmed());
    }
    rdv_stop();
}

/* ---------------------------------------------------------------- server restart, DNS, randomness */
static void test_restart(void)
{
    RdvConfig c = base(1);
    char before[12];
    reset_server();
    rdv_start(&c);
    run(200);
    snprintf(before, sizeof(before), "%s", rdv_room());
    CHECK(rdv_state() == RDV_ST_WAITING);
    run(10000);
    CHECK(rdv_state() == RDV_ST_WAITING && !strcmp(rdv_room(), before));   /* keepalives answered: nothing happens */
    clk += 8000;   /* the game thread stalls 8 s (a room load): no keepalive went, but the server still has the room */
    run(3000);
    CHECK(rdv_state() == RDV_ST_WAITING && !strcmp(rdv_room(), before));
    reset_server();   /* the server restarts: it knows no room */
    run(5500);
    CHECK(rdv_state() == RDV_ST_WAITING && rdv_room()[0] && strcmp(rdv_room(), before));   /* a new room, a new code */
    CHECK(srv_stats()->rooms == 1);
    /* the new code works */
    snprintf(before, sizeof(before), "%s", rdv_room());
    peer_new(&p1, PEER);
    CHECK(peer_join(&p1, before, NULL) == RDV_INTRO);
    pump();
    CHECK(peer_send(&p1, 0, "COOP after restart") > 0);
    /* a restart during a direct session: it goes on; the host opens a new room when it is over */
    reset_server();
    {
        int i;
        for (i = 0; i < 37; i++) { peer_send(&p1, 0, "COOP still here"); run(1000); }   /* the partner plays on */
    }
    CHECK(rdv_state() == RDV_ST_DIRECT && strstr(rdv_status(), "lost the room"));
    run(11000);   /* the partner went quiet too */
    CHECK(rdv_state() == RDV_ST_WAITING && srv_stats()->rooms == 1);
    rdv_stop();
}

static void test_dns(void)
{
    RdvConfig c = base(1);
    reset_server();
    snprintf(c.server_name, sizeof(c.server_name), "coop.example.org");
    dns_starts = 0;
    rdv_start(&c);
    CHECK(rdv_state() == RDV_ST_RESOLVE && dns_starts == 1);
    run(5100);   /* DNS never answers: a visible deadline */
    CHECK(rdv_error() && strstr(rdv_error(), "Cannot find co-op server"));
    rdv_stop();
    rdv_start(&c);
    run(100);
    dns_job.a = SRV;
    dns_job.state = 1;
    run(200);
    CHECK(rdv_state() == RDV_ST_WAITING);
    rdv_stop();
    rdv_start(&c);   /* the cache: no second lookup */
    CHECK(dns_starts == 2 && rdv_state() == RDV_ST_HELLO);
    run(200);
    CHECK(rdv_state() == RDV_ST_WAITING);
    rdv_stop();
    snprintf(c.server_name, sizeof(c.server_name), "host:99999");
    rdv_start(&c);
    CHECK(rdv_error() != NULL);
    rdv_stop();
}

static void test_no_randomness(void)
{
    RdvConfig c = base(1);
    reset_server();
    rdv_test_fail = 1;
    rdv_start(&c);
    CHECK(rdv_error() && strstr(rdv_error(), "Cannot secure the connection"));
    rdv_stop();
    rdv_test_fail = 2;   /* the password KDF cannot allocate */
    snprintf(c.password, sizeof(c.password), "pw");
    rdv_start(&c);
    CHECK(rdv_error() && strstr(rdv_error(), "secure the room password"));
    rdv_stop();
    rdv_test_fail = 0;
}

int main(int argc, char** argv)
{
    quiet = !(argc > 1 && !strcmp(argv[1], "-v"));
    srv_log = fopen(
#ifdef _WIN32
        "NUL",
#else
        "/dev/null",
#endif
        "w");
    SRV = na(0x0A000009, RDV_DEFAULT_PORT);
    CLI = na(0x0A000001, 40000);
    PEER = na(0x0A000002, 40001);
    PEER2 = na(0x0A000003, 40002);
    test_host();
    test_paths();
    test_joiner();
    test_restart();
    test_dns();
    test_no_randomness();
    srv_free();
    printf(fails ? "test_rdv_client: %d failures\n" : "test_rdv_client: all passed\n", fails);
    return fails != 0;
}
