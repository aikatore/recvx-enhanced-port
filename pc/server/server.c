/* recvx-server core (server.h, docs/SERVER.md). Single-threaded; all state in memory. */
#include "server.h"
#include "monocypher.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void (*srv_code_rng)(uint8_t out[RDV_CODE_LEN]);
FILE* srv_log;
SrvProc srv_proc;
int srv_state_format = SRV_STATE_FORMAT;

typedef struct Member {
    int on;
    RdvAddr pub, lan;
    uint8_t pk[32], token[16];
    uint8_t rejoin[16];         /* joiner: its rejoin tag (zero: none); the same player joining again takes this place */
    uint64_t last_ms;          /* the last authenticated sign of life */
    double bytes, dgrams;       /* relay token buckets (start empty) */
    uint64_t fill_ms;
    uint64_t rx_top;            /* RELAY counters: the highest seen and a 64-entry window below it */
    uint64_t rx_win;
    int ax;                     /* its address's entry in addrs[] + 1 (0: look it up) */
} Member;

typedef struct Room {
    uint32_t id;                /* slot | generation << 16; 0 = free */
    char code[RDV_CODE_LEN + 1];
    uint64_t req_id;            /* the host's CREATE request id: a retry gets this room again */
    uint8_t flags, salt[16], verifier[32];
    uint32_t member_seq;        /* counts joiners: the host's keepalive says which one it knows */
    uint64_t throttle_log_ms;
    uint64_t opened_ms, last_relay_ms;
    uint64_t relay_bytes, relay_mark;   /* relayed in this room (metrics: the top rooms by rate) */
    double relay_rate;
    uint8_t relayed, relaying;  /* this session went through the relay; it does now (verbose log) */
    uint64_t pw_fail_start, pw_last_ms;  /* wrong passwords for this room within a minute (whatever the address), the last one */
    int pw_fails;
    Member m[2];                /* 0 host, 1 joiner */
} Room;

typedef struct Recent { char code[RDV_CODE_LEN + 1]; uint64_t until; } Recent;   /* closed codes in their grace period */

/* Two tables per source address (docs/SERVER.md "Amplification and flood protection"):
   * Pre: flood control for everything that is not RELAY, before any check (HELLO comes from
     anywhere, spoofed or not). Direct-mapped, a collision overwrites: all it guards is how many
     replies an address gets, and the global requests_total_per_sec caps that too.
   * Addr: addresses that proved they receive our replies (a valid cookie or token): failed joins,
     the relay budget. Open addressing over PROBE slots; an entry is only replaced when it has been
     idle for 2 minutes (by then every budget in it has refilled anyway), so neither a collision
     nor a flood of new addresses can give an address its failures or budget back. A full
     neighbourhood refuses the newcomer instead (fail closed, counted as busy). */
typedef struct Pre { uint8_t ip[16]; int used; double tokens; uint64_t last_ms, log_ms; } Pre;
typedef struct Addr {
    uint8_t ip[16];
    int used, fails, creates;
    uint64_t seen_ms, fail_start, log_ms, create_start;
    double rbytes, rdgrams;     /* the address's relay budget: outlives its rooms and memberships */
    uint64_t rfill;
} Addr;
#define PRE_N 4096
#define ADDR_N 8192
#define PROBE 16
#define IDLE_MS 120000
#define ROOM_PW_FREE 10         /* wrong passwords per room and minute before its backoff */
#define ROOM_PW_GAP_MS 2000     /* then: an address that failed a join waits this long after the room's last wrong one */

static SrvConfig cfg;
static SrvSend out;
static Room* rooms;
static Recent* recent;
static int nrecent;
static Pre* pres;
static Addr* addrs;
static double all_req;          /* requests_total_per_sec */
static uint64_t all_req_fill, all_fail_start, all_log_ms;
static int all_fails;           /* failed_joins_total_per_min */
static SrvStats st;
static uint8_t secret[32];
static uint16_t gen;
static double total_bytes, total_dgrams;
static uint64_t total_fill, total_dfill;
static uint64_t rate_ms, rate_req, rate_in, rate_out;   /* the rates: counters at the last 10-s mark */
static double log_tokens;
static uint64_t log_fill, log_held, total_log_ms, full_log_ms;
static int draining;            /* srv_drain: no new rooms */
static uint64_t drain_log_ms;

/* the defaults, and why: docs/SERVER.md "Configuration" */
void srv_defaults(SrvConfig* c)
{
    memset(c, 0, sizeof(*c));
    c->port = RDV_DEFAULT_PORT;
    c->max_rooms = 1024;
    c->rooms_per_ip = 4;
    c->req_per_sec = 20;
    c->joins_per_min = 5;
    c->creates_per_min = 6;     /* a game opens one room per lobby; a loop of them fills the code grace table */
    c->req_total_per_sec = 5000;    /* ~5000 games starting at once; a flood from many (spoofed) addresses stops here */
    c->joins_total_per_min = 600;   /* guessing with many addresses: 600 a minute against 2^40 codes */
    c->relay_kbps = 512;    /* per player: ~3x the measured worst case (docs/SERVER.md "Sizing the relay") */
    c->relay_pps = 600;     /* per player: ~3x the measured worst case */
    c->ip_kbps = 1024;      /* per address: two busy players behind one router, or ~6 hosts */
    c->ip_pps = 1200;
    c->total_kbps = 25600;  /* 25 MB/s (~210 Mbit/s): ~150 busy relayed rooms; raise it for a bigger line */
    c->total_pps = 50000;
    c->room_timeout_s = 30;
    c->member_timeout_s = 10;   /* games send a keepalive every 3 s: a crashed joiner's place is free after 10 s */
    c->grace_s = 120;
    c->verbose = 0;
    c->stats_s = 300;
}

void srv_reconfig(const SrvConfig* c)
{
    int keep = cfg.max_rooms;
    cfg = *c;
    cfg.max_rooms = keep;
}

int srv_init(const SrvConfig* c, SrvSend send)
{
    srv_free();
    cfg = *c;
    if (cfg.max_rooms < 1) cfg.max_rooms = 1;
    if (cfg.max_rooms > 65535) cfg.max_rooms = 65535;
    out = send;
    memset(&st, 0, sizeof(st));
    total_bytes = total_dgrams = 0;
    total_fill = total_dfill = 0;
    rate_ms = rate_req = rate_in = rate_out = 0;
    log_tokens = 200;
    log_fill = log_held = 0;
    draining = 0;
    drain_log_ms = 0;
    all_req = cfg.req_total_per_sec;
    all_req_fill = all_fail_start = all_log_ms = 0;
    all_fails = 0;
    rooms = calloc(cfg.max_rooms, sizeof(Room));
    recent = calloc(cfg.max_rooms * 4, sizeof(Recent));
    pres = calloc(PRE_N, sizeof(Pre));
    addrs = calloc(ADDR_N, sizeof(Addr));
    nrecent = 0;
    return rooms && recent && pres && addrs && rdv_random(secret, sizeof(secret)) && rdv_random(&gen, sizeof(gen));
}

void srv_free(void)
{
    if (rooms) crypto_wipe(rooms, cfg.max_rooms * sizeof(Room));   /* verifiers, tokens */
    crypto_wipe(secret, sizeof(secret));
    free(rooms); free(recent); free(pres); free(addrs);
    rooms = NULL; recent = NULL; pres = NULL; addrs = NULL;
}

const SrvStats* srv_stats(void) { return &st; }

/* ---------------------------------------------------------------- helpers */
static int same(const RdvAddr* a, const RdvAddr* b) { return a->fam == b->fam && a->port == b->port && !memcmp(a->ip, b->ip, 16); }
static int same_ip(const RdvAddr* a, const RdvAddr* b) { return a->fam == b->fam && !memcmp(a->ip, b->ip, 16); }

/* ---------------------------------------------------------------- the log
   Normal: rooms opened/closed and limit hits; verbose (V) also requests, joins, the relay starting
   and stopping. Never passwords, verifiers, keys, tokens, payloads or full room codes; addresses
   only in verbose lines and only as /24. All event lines share one budget (20 a second, bursts of
   200), so a flood cannot fill the disk; what was held back is counted and said. */
static char logq[65536];        /* lines waiting for main.c */
static int logq_n;

static void vlog(const char* fmt, va_list a)
{
    char line[1024];
    int n = vsnprintf(line, sizeof(line) - 1, fmt, a);
    if (n < 0) return;
    if (n > (int)sizeof(line) - 2) n = sizeof(line) - 2;
    line[n++] = '\n';
    if (srv_log) { fwrite(line, 1, n, srv_log); fflush(srv_log); return; }
    if (logq_n + n > (int)sizeof(logq)) { st.log_suppressed++; return; }   /* the reader stalls: dropped, counted */
    memcpy(logq + logq_n, line, n);
    logq_n += n;
}

void srv_logf(const char* fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    vlog(fmt, a);
    va_end(a);
}

const char* srv_log_pending(int* n) { *n = logq_n; return logq; }
void srv_log_done(int n)
{
    if (n <= 0) return;
    if (n > logq_n) n = logq_n;
    memmove(logq, logq + n, logq_n - n);
    logq_n -= n;
}

static void ev(int verbose, uint64_t now, const char* fmt, ...)
{
    va_list a;
    if (verbose && !cfg.verbose) return;
    log_tokens += (now - log_fill) * 20 / 1000.0;
    if (log_tokens > 200) log_tokens = 200;
    log_fill = now;
    if (log_tokens < 1) { log_held++; st.log_suppressed++; return; }
    log_tokens -= 1;
    if (log_held) { srv_logf("(%llu log lines held back: too many events)", (unsigned long long)log_held); log_held = 0; }
    va_start(a, fmt);
    vlog(fmt, a);
    va_end(a);
}
#define V(...) ev(1, now, __VA_ARGS__)
#define LOG(...) ev(0, now, __VA_ARGS__)

static const char* anon(const RdvAddr* a)   /* verbose log: /24 only */
{
    static char s[2][32];
    static int k;
    k ^= 1;
    snprintf(s[k], sizeof(s[k]), "%u.%u.%u.x", a->ip[0], a->ip[1], a->ip[2]);
    return s[k];
}
#define WHO(a) cfg.verbose ? " from " : "", cfg.verbose ? anon(a) : ""   /* "%s%s": the /24 in verbose mode only */

static const char* rname(const Room* r)   /* "K7QM-xxxx": enough to tell rooms apart, not to join one */
{
    static char s[2][16];
    static int k;
    k ^= 1;
    snprintf(s[k], sizeof(s[k]), "%.4s-xxxx", r->code);
    return s[k];
}

static void send_msg(const RdvAddr* to, const RdvMsg* m)
{
    uint8_t b[256];
    int n = rdv_build(b, sizeof(b), m);
    if (n) out(to, b, n);
}

static void send_err(const RdvAddr* to, int err, const uint8_t* salt)
{
    RdvMsg m;
    memset(&m, 0, sizeof(m));
    m.type = RDV_ERROR;
    m.err = err;
    if (salt) memcpy(m.salt, salt, 16);
    send_msg(to, &m);
}

/* a keyed hash (the per-run secret): nobody can aim addresses at one slot on purpose */
static uint32_t ip_hash(const RdvAddr* a)
{
    uint8_t h[4];
    crypto_blake2b_keyed(h, 4, secret, 32, a->ip, 16);
    return (uint32_t)h[0] | (uint32_t)h[1] << 8 | (uint32_t)h[2] << 16 | (uint32_t)h[3] << 24;
}

/* a verified address's entry (NULL: the table is full around it - refuse) */
static Addr* addr_of(const RdvAddr* a, uint64_t now)
{
    uint32_t h = ip_hash(a);
    Addr* spare = NULL;
    int i;
    for (i = 0; i < PROBE; i++) {
        Addr* e = &addrs[(h + i) % ADDR_N];
        if (e->used && !memcmp(e->ip, a->ip, 16)) { e->seen_ms = now; return e; }
        if (!spare && (!e->used || now - e->seen_ms > IDLE_MS)) spare = e;
    }
    if (!spare) { st.drop_busy++; return NULL; }
    memset(spare, 0, sizeof(*spare));
    memcpy(spare->ip, a->ip, 16);
    spare->used = 1;
    spare->seen_ms = spare->rfill = now;   /* the relay budget starts empty and fills at its rate */
    return spare;
}

static Addr* member_addr(Member* s, uint64_t now)
{
    if (s->ax && addrs[s->ax - 1].used && !memcmp(addrs[s->ax - 1].ip, s->pub.ip, 16)) { addrs[s->ax - 1].seen_ms = now; return &addrs[s->ax - 1]; }
    {
        Addr* e = addr_of(&s->pub, now);
        s->ax = e ? (int)(e - addrs) + 1 : 0;
        return e;
    }
}

static int rate_ok(const RdvAddr* a, uint64_t now)
{
    Pre* e = &pres[ip_hash(a) % PRE_N];
    if (!e->used || memcmp(e->ip, a->ip, 16)) { memset(e, 0, sizeof(*e)); memcpy(e->ip, a->ip, 16); e->used = 1; e->tokens = 2.0 * cfg.req_per_sec; e->last_ms = now; }
    e->tokens += (now - e->last_ms) * cfg.req_per_sec / 1000.0;
    if (e->tokens > 2.0 * cfg.req_per_sec) e->tokens = 2.0 * cfg.req_per_sec;
    e->last_ms = now;
    all_req += (now - all_req_fill) * cfg.req_total_per_sec / 1000.0;
    if (all_req > cfg.req_total_per_sec) all_req = cfg.req_total_per_sec;
    all_req_fill = now;
    if (e->tokens < 1) {
        st.drop_rate++;
        if (now - e->log_ms >= 10000) { e->log_ms = now; LOG("limit: an address sent over requests_per_sec %d, dropping%s%s", cfg.req_per_sec, WHO(a)); }
        return 0;
    }
    if (all_req < 1) {
        st.drop_rate++;
        if (now - all_log_ms >= 10000) { all_log_ms = now; LOG("limit: all addresses together over requests_total_per_sec %d, dropping", cfg.req_total_per_sec); }
        return 0;
    }
    e->tokens -= 1;
    all_req -= 1;
    return 1;
}

/* failed joins: per address, and from all addresses together */
static int fails_ok(Addr* e, uint64_t now)
{
    if (now - e->fail_start >= 60000) { e->fail_start = now; e->fails = 0; }
    if (now - all_fail_start >= 60000) { all_fail_start = now; all_fails = 0; }
    return e->fails < cfg.joins_per_min && all_fails < cfg.joins_total_per_min;
}
static void fail(Addr* e) { e->fails++; all_fails++; }

static int cookie_ok(const RdvAddr* a, const uint8_t c[16], uint64_t now)
{
    uint8_t k[16];
    uint32_t b = (uint32_t)(now / 60000);
    rdv_cookie(secret, a, b, k);
    if (!crypto_verify16(k, c)) return 1;
    rdv_cookie(secret, a, b - 1, k);
    return !crypto_verify16(k, c);
}

void srv_test_cookie(const RdvAddr* a, uint64_t now, uint8_t out[16]) { rdv_cookie(secret, a, (uint32_t)(now / 60000), out); }

static Room* room_by_id(uint32_t id)
{
    uint32_t slot = id & 0xFFFF;
    return id && slot < (uint32_t)cfg.max_rooms && rooms[slot].id == id ? &rooms[slot] : NULL;
}

static Room* room_by_code(const char* code)
{
    int i;
    for (i = 0; i < cfg.max_rooms; i++)
        if (rooms[i].id && !strcmp(rooms[i].code, code)) return &rooms[i];
    return NULL;
}

static int code_taken(const char* code, uint64_t now)
{
    int i;
    if (room_by_code(code)) return 1;
    for (i = 0; i < nrecent; i++)
        if (recent[i].until > now && !strcmp(recent[i].code, code)) return 1;
    return 0;
}

static void session_end(Room* r)
{
    if (!r->relayed) st.sessions_direct++;
}

/* codes in their grace period (4 per room). Churn is limited per address (rooms_per_ip_per_min);
   when many addresses together still fill the table, a closed code is not recorded - a late joiner
   of it could then, against 2^40 codes, meet a newer room - instead of refusing every CREATE
   (review: refusing was a denial of service for the whole server). */
static void close_room(Room* r, uint64_t now, const char* why)
{
    int i;
    for (i = 0; i < nrecent && recent[i].until > now; i++) ;   /* a free (expired) entry, else append */
    if (i == nrecent && nrecent < cfg.max_rooms * 4) nrecent++;
    if (i < nrecent) { memcpy(recent[i].code, r->code, sizeof(r->code)); recent[i].until = now + cfg.grace_s * 1000ull; }
    else if (now - full_log_ms >= 10000) { full_log_ms = now; LOG("limit: the code grace table is full (rooms opened and closed from many addresses): closed codes are not kept for code_grace now"); }
    LOG("room %s closed (%s) after %llu s, %llu KB relayed", rname(r), why, (unsigned long long)(now - r->opened_ms) / 1000,
        (unsigned long long)r->relay_bytes / 1024);
    st.rooms--;
    if (r->m[1].on) { st.members--; session_end(r); }
    memset(r, 0, sizeof(*r));
}

static void member_leaves(Room* r, const char* why, uint64_t now)
{
    V("room %s: the joiner left (%s)", rname(r), why);
    session_end(r);
    memset(&r->m[1], 0, sizeof(r->m[1]));
    r->relayed = r->relaying = 0;
    st.members--;
}

static void intro(Room* r, int to)
{
    RdvMsg m;
    const Member* peer = &r->m[!to];
    memset(&m, 0, sizeof(m));
    m.type = RDV_INTRO;
    m.room_id = r->id;
    m.seq = r->member_seq;
    m.role = to;
    m.flags = r->flags;
    memcpy(m.token, r->m[to].token, 16);
    m.pub = peer->pub;
    m.lan = peer->lan;
    memcpy(m.pk, peer->pk, 32);
    send_msg(&r->m[to].pub, &m);
}

/* ---------------------------------------------------------------- requests */
static void on_create(const RdvAddr* from, const RdvMsg* q, uint64_t now)
{
    RdvMsg m;
    Room* r = NULL;
    uint8_t tok[16];
    int i, n = 0, slot = -1, tries;
    Addr* e = addr_of(from, now);
    if (!e) { send_err(from, RDV_E_SERVER_FULL, NULL); return; }
    for (i = 0; i < cfg.max_rooms; i++) {
        if (!rooms[i].id) { if (slot < 0) slot = i; continue; }
        if (same(&rooms[i].m[0].pub, from)) r = &rooms[i];
        else if (same_ip(&rooms[i].m[0].pub, from)) n++;
    }
    if (r && r->req_id == q->req_id) { V("room %s: CREATE retried", rname(r)); goto reply; }   /* a retry: the same room */
    if (draining) {   /* the game says "The co-op server is full or busy": the player tries again in a moment */
        if (now - drain_log_ms >= 10000) { drain_log_ms = now; LOG("draining: a new room refused (%u still open)", st.rooms); }
        send_err(from, RDV_E_SERVER_FULL, NULL);
        return;
    }
    if (now - e->create_start >= 60000) { e->create_start = now; e->creates = 0; }
    if (e->creates >= cfg.creates_per_min) {
        st.drop_rate++;
        if (now - e->log_ms >= 10000) { e->log_ms = now; LOG("limit: an address opened rooms_per_ip_per_min %d rooms within a minute, no more%s%s", cfg.creates_per_min, WHO(from)); }
        send_err(from, RDV_E_RATE, NULL);
        return;
    }
    if (r) { close_room(r, now, "the host made a new one"); if (slot < 0) slot = (int)(r - rooms); r = NULL; }
    if (n >= cfg.rooms_per_ip) {
        if (now - e->log_ms >= 10000) { e->log_ms = now; LOG("limit: an address has rooms_per_ip %d rooms open, no more%s%s", n, WHO(from)); }
        send_err(from, RDV_E_TOO_MANY, NULL);
        return;
    }
    if (slot < 0) {
        if (now - full_log_ms >= 10000) { full_log_ms = now; LOG("limit: all max_rooms %d rooms in use, CREATE refused", cfg.max_rooms); }
        send_err(from, RDV_E_SERVER_FULL, NULL);
        return;
    }
    if (!rdv_random(tok, 16)) { st.drop_busy++; LOG("error: no randomness, CREATE refused"); send_err(from, RDV_E_SERVER_FULL, NULL); return; }
    r = &rooms[slot];
    memset(r, 0, sizeof(*r));
    for (tries = 0; tries < 64; tries++) {   /* unique among open rooms and codes in their grace period */
        uint8_t rnd[RDV_CODE_LEN];
        if (srv_code_rng) srv_code_rng(rnd);
        else if (!rdv_random(rnd, sizeof(rnd))) { tries = 64; break; }   /* fail closed: no predictable code */
        rdv_code_from(rnd, r->code);
        if (!code_taken(r->code, now)) break;
        st.code_retries++;
    }
    if (tries == 64) { memset(r, 0, sizeof(*r)); st.drop_busy++; LOG("no free room code after 64 draws (or no randomness), CREATE refused"); send_err(from, RDV_E_SERVER_FULL, NULL); return; }
    if (!++gen) gen = 1;
    e->creates++;
    r->id = (uint32_t)slot | (uint32_t)gen << 16;
    r->req_id = q->req_id;
    r->flags = q->flags;
    memcpy(r->salt, q->salt, 16);
    memcpy(r->verifier, q->verifier, 32);
    r->m[0].on = 1;
    r->m[0].pub = *from;
    r->m[0].lan = q->lan;
    r->m[0].last_ms = r->m[0].fill_ms = now;
    r->opened_ms = now;
    memcpy(r->m[0].pk, q->pk, 32);
    memcpy(r->m[0].token, tok, 16);
    crypto_wipe(tok, 16);
    st.rooms++;
    st.created++;
    LOG("room %s opened%s%s%s", rname(r), (r->flags & RDV_F_PW) ? " (password)" : "", WHO(from));
reply:
    memset(&m, 0, sizeof(m));
    m.type = RDV_ROOM;
    m.req_id = r->req_id;
    m.room_id = r->id;
    memcpy(m.code, r->code, sizeof(m.code));
    memcpy(m.token, r->m[0].token, 16);
    send_msg(from, &m);
}

static void on_join(const RdvAddr* from, const RdvMsg* q, uint64_t now)
{
    Room* r;
    uint8_t p[32], tok[16];
    Addr* e = addr_of(from, now);
    if (!e) { send_err(from, RDV_E_SERVER_FULL, NULL); return; }
    if (!fails_ok(e, now)) {
        st.drop_rate++;
        st.join_failed++;
        if (e->fails < cfg.joins_per_min) {
            if (now - all_log_ms >= 10000) { all_log_ms = now; LOG("limit: all addresses together failed failed_joins_total_per_min %d joins within a minute, refusing joins", cfg.joins_total_per_min); }
        } else if (now - e->log_ms >= 10000) { e->log_ms = now; LOG("limit: an address failed failed_joins_per_min %d joins within a minute, refusing its joins%s%s", cfg.joins_per_min, WHO(from)); }
        send_err(from, RDV_E_RATE, NULL);
        return;
    }
    r = room_by_code(q->code);
    if (!r) { fail(e); st.join_failed++; send_err(from, RDV_E_NO_ROOM, NULL); V("JOIN of an unknown room%s%s", WHO(from)); return; }
    if (r->flags & RDV_F_PW) {
        if (!(q->flags & RDV_F_PW)) { send_err(from, RDV_E_NEED_PW, r->salt); V("room %s: JOIN without the password%s%s", rname(r), WHO(from)); return; }
        if (now - r->pw_fail_start >= 60000) { r->pw_fail_start = now; r->pw_fails = 0; }
        /* soft backoff (docs/SERVER.md "Password guessing backoff"): no lockout. Past ROOM_PW_FREE wrong passwords a
           minute, an address that failed a join this minute waits ROOM_PW_GAP_MS after the room's last
           wrong one; an address without failures is always checked, so guessers from many addresses
           cannot keep a joiner out (each address still has its own failed_joins_per_min, and all
           together failed_joins_total_per_min) */
        if (r->pw_fails >= ROOM_PW_FREE && e->fails && now - r->pw_last_ms < ROOM_PW_GAP_MS) {
            st.drop_rate++;
            st.join_failed++;
            if (now - r->throttle_log_ms >= 10000) { r->throttle_log_ms = now; LOG("limit: room %s: %d wrong passwords within a minute, slowing its guessers", rname(r), r->pw_fails); }
            send_err(from, RDV_E_RATE, NULL);
            return;
        }
        rdv_proof(r->verifier, q->cookie, q->code, q->pk, p);
        if (crypto_verify32(p, q->proof)) { fail(e); r->pw_fails++; r->pw_last_ms = now; st.join_failed++; send_err(from, RDV_E_BAD_PW, r->salt); V("room %s: wrong password%s%s", rname(r), WHO(from)); return; }
    }
    if (r->m[1].on) {
        static const uint8_t none[16];
        if (same(&r->m[1].pub, from) && !memcmp(r->m[1].pk, q->pk, 32)) { r->m[1].last_ms = now; intro(r, 1); V("room %s: JOIN retried", rname(r)); return; }   /* a retry */
        /* the same player again (its game crashed and came back): the rejoin tag only it can make
           (rdv_rejoin_tag: its secret over this room's code) takes its stale place at once */
        if (crypto_verify16(q->rejoin, none) && !crypto_verify16(q->rejoin, r->m[1].rejoin)) {
            st.rejoined++;
            member_leaves(r, "the same player joined again", now);
        } else {
            st.join_failed++;
            send_err(from, RDV_E_FULL, NULL);
            V("room %s: full, a JOIN turned away%s%s", rname(r), WHO(from));
            return;
        }
    }
    if (same(&r->m[0].pub, from)) { st.join_failed++; send_err(from, RDV_E_FULL, NULL); return; }   /* the host itself */
    if (!rdv_random(tok, 16)) { st.drop_busy++; LOG("error: no randomness, JOIN refused"); send_err(from, RDV_E_SERVER_FULL, NULL); return; }
    memset(&r->m[1], 0, sizeof(r->m[1]));
    r->m[1].on = 1;
    r->m[1].pub = *from;
    r->m[1].lan = q->lan;
    r->m[1].last_ms = r->m[1].fill_ms = now;
    r->m[1].ax = (int)(e - addrs) + 1;
    memcpy(r->m[1].pk, q->pk, 32);
    memcpy(r->m[1].rejoin, q->rejoin, 16);
    memcpy(r->m[1].token, tok, 16);
    crypto_wipe(tok, 16);
    r->member_seq++;
    r->relayed = r->relaying = 0;
    st.members++;
    st.joined++;
    V("room %s: a partner joined%s%s", rname(r), WHO(from));
    intro(r, 1);
    intro(r, 0);
}

/* the room and side a token message comes from: only the address that created / joined, with its token */
static Room* owner(const RdvAddr* from, const RdvMsg* q, int* side)
{
    Room* r = room_by_id(q->room_id);
    int k;
    if (r)
        for (k = 0; k < 2; k++)
            if (r->m[k].on && same(&r->m[k].pub, from) && !crypto_verify16(r->m[k].token, q->token)) { *side = k; return r; }
    st.drop_foreign++;
    return NULL;
}

/* "the server still has the room" (flag 0x80), and whether it has a partner */
static void keepalive_reply(const Room* r, const RdvAddr* to)
{
    RdvMsg m;
    memset(&m, 0, sizeof(m));
    m.type = RDV_KEEPALIVE;
    m.flags = 0x80;
    m.room_id = r->id;
    m.has_member = r->m[1].on;
    send_msg(to, &m);
}

static void on_keepalive(const RdvAddr* from, const RdvMsg* q, uint64_t now)
{
    int side;
    Room* r = owner(from, q, &side);
    if (!r) { V("KEEPALIVE with a wrong room or token%s%s, dropped", WHO(from)); return; }
    r->m[side].last_ms = now;
    member_addr(&r->m[side], now);   /* keeps its address entry in use */
    if (side == 0 && r->m[1].on && q->seq != r->member_seq) { intro(r, 0); V("room %s: the host missed its INTRO, sent again", rname(r)); return; }
    keepalive_reply(r, from);
}

static void on_close(const RdvAddr* from, const RdvMsg* q, uint64_t now)
{
    int side;
    Room* r = owner(from, q, &side);
    if (!r) { V("CLOSE with a wrong room or token%s%s, dropped", WHO(from)); return; }
    if (side == 0) { st.closed++; close_room(r, now, "closed by the host"); }
    else member_leaves(r, "closed", now);
}

/* KICK: the host removes its partner (a stuck or crashed player): the place is free at once, the
   room and its code stay. Only the member the host means (its sequence), so a late copy cannot
   remove the next one. */
static void on_kick(const RdvAddr* from, const RdvMsg* q, uint64_t now)
{
    int side;
    Room* r = owner(from, q, &side);
    if (!r || side != 0) { V("KICK with a wrong room or token%s%s, dropped", WHO(from)); return; }
    r->m[0].last_ms = now;
    if (r->m[1].on && q->seq == r->member_seq) { st.kicked++; member_leaves(r, "removed by the host", now); }
    keepalive_reply(r, from);   /* the answer: as a keepalive's */
}

static double bucket(double v, uint64_t* at, uint64_t now, double rate, double cap)
{
    v += (now - *at) * rate / 1000.0;
    *at = now;
    return v > cap ? cap : v;
}

static void on_relay(const RdvAddr* from, const uint8_t* buf, int n, const RdvMsg* q, uint64_t now)
{
    Room* r = room_by_id(q->room_id);
    Member* s;
    int k;
    st.relay_in_dgrams++;
    st.relay_in_bytes += n;
    if (!r || !r->m[1].on) { st.drop_unknown_room++; return; }
    k = same(&r->m[0].pub, from) ? 0 : same(&r->m[1].pub, from) ? 1 : -1;
    if (k < 0) { st.drop_foreign++; return; }   /* a third address: dropped */
    s = &r->m[k];
    /* the frame must carry the member's MAC (its token: the partner and anyone who learned the
       room id and the address cannot send in its name) and a counter not seen before; only then
       does it cost the member's budget or count as a sign of life (review finding 5) */
    if (!rdv_relay_ok(buf, n, s->token)) { st.drop_relay_auth++; return; }
    if (rdv_replayed(s->rx_top, s->rx_win, q->seq)) { st.drop_replay++; return; }
    rdv_replay_mark(&s->rx_top, &s->rx_win, q->seq);
    {   /* soft caps, per player, per address (kept across rooms and memberships: leaving and
           joining again does not refill it) and for the server: over one the datagram is dropped
           (the game resends what matters), counted, and logged at most once per 10 s */
        Addr* e = member_addr(s, now);
        uint64_t at = s->fill_ms, eat;
        const char* why = NULL;
        int lim = 0;
        if (!e) return;   /* the address table is full around it: counted as busy */
        eat = e->rfill;
        s->bytes = bucket(s->bytes, &at, now, cfg.relay_kbps * 1024.0, cfg.relay_kbps * 1024.0);
        s->dgrams = bucket(s->dgrams, &s->fill_ms, now, cfg.relay_pps, cfg.relay_pps);
        e->rbytes = bucket(e->rbytes, &eat, now, cfg.ip_kbps * 1024.0, cfg.ip_kbps * 1024.0);
        e->rdgrams = bucket(e->rdgrams, &e->rfill, now, cfg.ip_pps, cfg.ip_pps);
        if (cfg.total_kbps) total_bytes = bucket(total_bytes, &total_fill, now, cfg.total_kbps * 1024.0, cfg.total_kbps * 1024.0);
        if (cfg.total_pps) total_dgrams = bucket(total_dgrams, &total_dfill, now, cfg.total_pps, cfg.total_pps);
        if (s->dgrams < 1) { st.drop_pps++; why = "relay_datagrams_per_sec"; lim = cfg.relay_pps; }
        else if (s->bytes < n) { st.drop_bw++; why = "relay_kb_per_sec"; lim = cfg.relay_kbps; }
        else if (e->rdgrams < 1) { st.drop_ip++; why = "relay_address_datagrams_per_sec"; lim = cfg.ip_pps; }
        else if (e->rbytes < n) { st.drop_ip++; why = "relay_address_kb_per_sec"; lim = cfg.ip_kbps; }
        else if ((cfg.total_pps && total_dgrams < 1) || (cfg.total_kbps && total_bytes < n)) {
            st.drop_total++;
            if (now - total_log_ms >= 10000) {   /* one line for the server, not one per room */
                total_log_ms = now;
                LOG("limit: all relayed traffic over relay_total_kb_per_sec %d / relay_total_datagrams_per_sec %d, dropping", cfg.total_kbps, cfg.total_pps);
            }
            return;
        }
        if (why) {
            if (now - r->throttle_log_ms >= 10000) { r->throttle_log_ms = now; LOG("limit: room %s: the %s over %s %d, dropping", rname(r), k ? "joiner" : "host", why, lim); }
            return;
        }
        s->bytes -= n;
        s->dgrams -= 1;
        e->rbytes -= n;
        e->rdgrams -= 1;
        if (cfg.total_kbps) total_bytes -= n;
        if (cfg.total_pps) total_dgrams -= 1;
    }
    s->last_ms = now;
    if (!r->relayed) { r->relayed = 1; st.sessions_relayed++; }
    if (!r->relaying) { r->relaying = 1; V("room %s: relaying (from the %s%s)", rname(r), k ? "joiner" : "host", r->last_relay_ms ? ", again" : ""); }
    r->last_relay_ms = now;
    r->relay_bytes += n;
    st.relay_out_bytes += n;
    st.relay_out_dgrams++;
    out(&r->m[!k].pub, buf, n);   /* the frame as it came: the receiver strips the header */
}

/* the busiest rooms by relay rate (over the last ~10 s): indexes into rooms[], descending */
#define TOP_N 5
static int top_rooms(int* top)
{
    int i, k, n = 0;
    for (i = 0; i < cfg.max_rooms; i++) {
        if (!rooms[i].id || rooms[i].relay_rate < 1) continue;
        k = n < TOP_N ? n++ : TOP_N;
        for (; k > 0 && rooms[top[k - 1]].relay_rate < rooms[i].relay_rate; k--)
            if (k < TOP_N) top[k] = top[k - 1];
        if (k < TOP_N) top[k] = i;
    }
    return n;
}

/* every metric once: STATUS (Prometheus text) and status-json print this list */
typedef struct Metric { const char* name; const char* key; const char* label; double v; int dec; } Metric;
#define METRICS_MAX 48
static int metrics(Metric* m)
{
    int n = 0;
#define M(nm, k, l, val, d) (m[n].name = nm, m[n].key = k, m[n].label = l, m[n].v = (double)(val), m[n].dec = d, n++)
    M("recvx_uptime_seconds", NULL, NULL, srv_proc.uptime_s, 0);
    M("recvx_cpu_seconds_total", "mode", "user", srv_proc.cpu_user_s, 3);
    M("recvx_cpu_seconds_total", "mode", "system", srv_proc.cpu_sys_s, 3);
    M("recvx_resident_memory_bytes", NULL, NULL, srv_proc.rss_kb * 1024.0, 0);
    M("recvx_rooms", NULL, NULL, st.rooms, 0);
    M("recvx_players", NULL, NULL, st.rooms + st.members, 0);
    M("recvx_rooms_opened_total", NULL, NULL, st.created, 0);
    M("recvx_rooms_closed_total", "why", "host", st.closed, 0);
    M("recvx_rooms_closed_total", "why", "timeout", st.expired, 0);
    M("recvx_joins_total", "result", "ok", st.joined, 0);
    M("recvx_joins_total", "result", "failed", st.join_failed, 0);
    M("recvx_partners_left_total", "why", "rejoined", st.rejoined, 0);
    M("recvx_partners_left_total", "why", "removed", st.kicked, 0);
    M("recvx_partners_left_total", "why", "timeout", st.member_expired, 0);
    M("recvx_sessions_total", "path", "direct", st.sessions_direct, 0);
    M("recvx_sessions_total", "path", "relay", st.sessions_relayed, 0);
    M("recvx_requests_total", NULL, NULL, st.requests, 0);
    M("recvx_requests_per_second", NULL, NULL, st.req_rate, 1);
    M("recvx_relay_datagrams_total", "dir", "in", st.relay_in_dgrams, 0);
    M("recvx_relay_datagrams_total", "dir", "out", st.relay_out_dgrams, 0);
    M("recvx_relay_bytes_total", "dir", "in", st.relay_in_bytes, 0);
    M("recvx_relay_bytes_total", "dir", "out", st.relay_out_bytes, 0);
    M("recvx_relay_bytes_per_second", "dir", "in", st.relay_in_rate, 0);
    M("recvx_relay_bytes_per_second", "dir", "out", st.relay_out_rate, 0);
    M("recvx_drops_total", "reason", "request_rate", st.drop_rate, 0);
    M("recvx_drops_total", "reason", "relay_datagram_rate", st.drop_pps, 0);
    M("recvx_drops_total", "reason", "relay_bandwidth", st.drop_bw, 0);
    M("recvx_drops_total", "reason", "relay_address", st.drop_ip, 0);
    M("recvx_drops_total", "reason", "relay_total", st.drop_total, 0);
    M("recvx_drops_total", "reason", "invalid", st.drop_invalid, 0);
    M("recvx_drops_total", "reason", "unknown_room", st.drop_unknown_room, 0);
    M("recvx_drops_total", "reason", "foreign", st.drop_foreign, 0);
    M("recvx_drops_total", "reason", "relay_auth", st.drop_relay_auth, 0);
    M("recvx_drops_total", "reason", "relay_replay", st.drop_replay, 0);
    M("recvx_drops_total", "reason", "busy", st.drop_busy, 0);
    M("recvx_code_retries_total", NULL, NULL, st.code_retries, 0);
    M("recvx_log_lines_held_back_total", NULL, NULL, st.log_suppressed, 0);
    M("recvx_send_failures_total", NULL, NULL, srv_proc.send_failed, 0);
#undef M
    return n;
}

#define P(...) (k += snprintf(o + k, k < cap ? cap - k : 0, __VA_ARGS__))
int srv_status_text(char* o, int cap)
{
    Metric m[METRICS_MAX];
    int k = 0, top[TOP_N], n = top_rooms(top), i, nm = metrics(m);
    for (i = 0; i < nm; i++)
        if (m[i].key) P("%s{%s=\"%s\"} %.*f\n", m[i].name, m[i].key, m[i].label, m[i].dec, m[i].v);
        else P("%s %.*f\n", m[i].name, m[i].dec, m[i].v);
    for (i = 0; i < n; i++) P("recvx_room_relay_bytes_per_second{room=\"%s\"} %.0f\n", rname(&rooms[top[i]]), rooms[top[i]].relay_rate);
    return k < cap ? k : cap - 1;
}

/* a room's id in status-json: a keyed hash of its internal id, nothing of its code */
static void room_hash(const Room* r, char out[13])
{
    uint8_t h[6];
    int i;
    crypto_blake2b_keyed(h, 6, secret, 32, (const uint8_t*)&r->id, 4);
    for (i = 0; i < 6; i++) snprintf(out + 2 * i, 3, "%02x", h[i]);
}

int srv_status_json(char* o, int cap, uint64_t now)
{
    Metric m[METRICS_MAX];
    int k = 0, i, nm = metrics(m), listed = 0, truncated = 0, k2;
    unsigned direct = 0, relay = 0;
    P("{\"version\":\"%s\",\"state_format\":%d,\"started_at\":%.0f,\"uptime_s\":%.0f,\"process_started_at\":%.0f,\"handovers\":%u,\"draining\":%s,"
      "\"handover_capable\":%s,\"rooms\":%u,\"room_list\":[",
      srv_proc.version ? srv_proc.version : "", srv_state_format, srv_proc.started_at, srv_proc.uptime_s, srv_proc.process_started_at,
      srv_proc.handovers, draining ? "true" : "false", srv_proc.handover_capable ? "true" : "false", st.rooms);
    for (i = 0; i < cfg.max_rooms; i++) {
        const Room* r = &rooms[i];
        char id[13];
        if (!r->id) continue;
        if (r->m[1].on) { if (r->relaying) relay++; else direct++; }
        if (k > cap - 4096) { truncated = 1; continue; }   /* keeps room for the rest */
        room_hash(r, id);
        P("%s{\"id\":\"%s\",\"age_s\":%llu,\"players\":%d,\"path\":\"%s\",\"relay_bps\":%.0f}", listed++ ? "," : "", id,
          (unsigned long long)(now - r->opened_ms) / 1000, 1 + (r->m[1].on != 0), !r->m[1].on ? "waiting" : r->relaying ? "relay" : "direct", r->relay_rate);
    }
    P("],\"room_list_truncated\":%s,\"players\":%u,\"sessions\":{\"direct\":%u,\"relay\":%u},\"counters\":{", truncated ? "true" : "false",
      st.rooms + st.members, direct, relay);
    for (i = 0; i < nm; i++) {   /* a labelled metric: an object by label value */
        int first = !i || strcmp(m[i].name, m[i - 1].name), last = i == nm - 1 || strcmp(m[i].name, m[i + 1].name);
        if (first) P("%s\"%s\":%s", i ? "," : "", m[i].name, m[i].key ? "{" : "");
        if (m[i].key) P("%s\"%s\":%.*f%s", first ? "" : ",", m[i].label, m[i].dec, m[i].v, last ? "}" : "");
        else P("%.*f", m[i].dec, m[i].v);
    }
    for (k2 = 0; k2 < 2; k2++) {   /* "drops" by reason: inside counters, and at the top */
        P(k2 ? "}},\"drops\":{" : ",\"drops\":{");
        for (i = 0, listed = 0; i < nm; i++)
            if (!strcmp(m[i].name, "recvx_drops_total")) P("%s\"%s\":%.0f", listed++ ? "," : "", m[i].label, m[i].v);
    }
    P("}}");
    return k < cap ? k : 0;   /* 0: it did not fit (cap is far above everything but the room list) */
}

int srv_summary(char* o, int cap)
{
    int k = 0, top[TOP_N], n = top_rooms(top), i;
    double up = srv_proc.uptime_s > 0 ? srv_proc.uptime_s : 1, cpu = srv_proc.cpu_user_s + srv_proc.cpu_sys_s;
    P("up %.0f s, cpu %.2f s (%.2f%%), rss %.1f MB | rooms %u, players %u | opened %u, closed %u, timed out %u | joins %u ok, %u failed | partners rejoined %u, removed %u, timed out %u | "
      "sessions %u direct, %u relayed | %.1f requests/s | relay in %llu datagrams %.1f MB, out %llu %.1f MB, now %.1f / %.1f KB/s | "
      "drops: request rate %llu, relay rate %llu, bandwidth %llu, address %llu, total %llu, invalid %llu, unknown room %llu, foreign %llu, "
      "relay auth %llu, replay %llu, busy %llu",
      srv_proc.uptime_s, cpu, 100 * cpu / up, srv_proc.rss_kb / 1024.0,
      st.rooms, st.rooms + st.members, st.created, st.closed, st.expired, st.joined, st.join_failed, st.rejoined, st.kicked, st.member_expired, st.sessions_direct, st.sessions_relayed, st.req_rate,
      st.relay_in_dgrams, st.relay_in_bytes / 1048576.0, st.relay_out_dgrams, st.relay_out_bytes / 1048576.0, st.relay_in_rate / 1024, st.relay_out_rate / 1024,
      st.drop_rate, st.drop_pps, st.drop_bw, st.drop_ip, st.drop_total, st.drop_invalid, st.drop_unknown_room, st.drop_foreign,
      st.drop_relay_auth, st.drop_replay, st.drop_busy);
    if (srv_proc.send_failed) P(" | send failures %llu", srv_proc.send_failed);
    if (st.log_suppressed) P(" | log lines held back %llu", st.log_suppressed);
    for (i = 0; i < n; i++) P("%s%s %.1f KB/s", i ? ", " : " | top rooms: ", rname(&rooms[top[i]]), rooms[top[i]].relay_rate / 1024);
    return k < cap ? k : cap - 1;
}
#undef P

void srv_input(const RdvAddr* from, const uint8_t* buf, int n, uint64_t now)
{
    RdvMsg q, m;
    int t = rdv_parse(buf, n, &q);
    if (t == RDV_BAD_VERSION && n >= RDV_HDR + 19 && rate_ok(from, now)) { send_err(from, RDV_E_VERSION, NULL); V("a client of protocol %d%s%s", buf[4] | buf[5] << 8, WHO(from)); return; }
    if (t < 0) { st.drop_invalid++; V("an invalid datagram (%d bytes)%s%s, dropped", n, WHO(from)); return; }
    if (t == RDV_RELAY) { on_relay(from, buf, n, &q, now); return; }
    if (!rate_ok(from, now)) return;
    st.requests++;
    switch (t) {
    case RDV_HELLO:
        memset(&m, 0, sizeof(m));
        m.type = RDV_WELCOME;
        m.nonce = q.nonce;
        rdv_cookie(secret, from, (uint32_t)(now / 60000), m.cookie);
        m.pub = *from;
        send_msg(from, &m);
        V("HELLO%s%s", WHO(from));
        return;
    case RDV_CREATE:
    case RDV_JOIN:
        if (!cookie_ok(from, q.cookie, now)) { send_err(from, RDV_E_COOKIE, NULL); V("a stale cookie%s%s", WHO(from)); return; }
        if (t == RDV_CREATE) on_create(from, &q, now); else on_join(from, &q, now);
        return;
    case RDV_KEEPALIVE: if (!(q.flags & 0x80)) on_keepalive(from, &q, now); return;
    case RDV_CLOSE: on_close(from, &q, now); return;
    case RDV_KICK: on_kick(from, &q, now); return;
    case RDV_STATUS:   /* monitoring, from this machine only */
        if (from->fam == 4 && from->ip[0] == 127) {   /* the text in datagrams of whole lines (localhost only: no amplification) */
            uint8_t b[RDV_DGRAM_MAX];
            char t2[4096];
            int len, at = 0;
            V("STATUS from this machine");
            if (q.payload_len == 4 && !memcmp(q.payload, "json", 4)) {   /* status-json: parts "J", index, count (u16 LE) */
                static char js[65536];
                uint8_t part[RDV_DGRAM_MAX - RDV_HDR];
                int jn = srv_status_json(js, sizeof(js), now), per = (int)sizeof(part) - 5, cnt = (jn + per - 1) / per, i;
                for (i = 0; i < cnt; i++) {
                    int len2 = jn - i * per < per ? jn - i * per : per;
                    part[0] = 'J'; part[1] = (uint8_t)i; part[2] = (uint8_t)(i >> 8); part[3] = (uint8_t)cnt; part[4] = (uint8_t)(cnt >> 8);
                    memcpy(part + 5, js + i * per, len2);
                    memset(&m, 0, sizeof(m));
                    m.type = RDV_STATUS;
                    m.payload = part;
                    m.payload_len = 5 + len2;
                    if ((n = rdv_build(b, sizeof(b), &m))) out(from, b, n);
                }
                return;
            }
            len = srv_status_text(t2, sizeof(t2));
            while (at < len) {
                int part = len - at;
                if (part > RDV_DGRAM_MAX - RDV_HDR) {
                    part = RDV_DGRAM_MAX - RDV_HDR;
                    while (part > 1 && t2[at + part - 1] != '\n') part--;
                }
                memset(&m, 0, sizeof(m));
                m.type = RDV_STATUS;
                m.payload = (const uint8_t*)t2 + at;
                m.payload_len = part;
                if ((n = rdv_build(b, sizeof(b), &m))) out(from, b, n);
                at += part;
            }
        }
        return;
    default: st.drop_invalid++; return;   /* server-to-client types, punches */
    }
}

void srv_pause(uint64_t ms, uint64_t now)   /* the time-outs of hosts and joiners move by ms (at most to now) */
{
    int i, k;
    for (i = 0; i < cfg.max_rooms; i++)
        for (k = 0; k < 2 && rooms[i].id; k++) {
            uint64_t* t = &rooms[i].m[k].last_ms;
            *t = *t + ms < now ? *t + ms : now;
        }
}

void srv_tick(uint64_t now)
{
    uint64_t to = cfg.room_timeout_s * 1000ull;
    int i;
    for (i = 0; i < cfg.max_rooms; i++) {
        Room* r = &rooms[i];
        if (!r->id) continue;
        if (now - r->m[0].last_ms > to) { st.expired++; close_room(r, now, "the host timed out"); continue; }
        if (r->m[1].on && now - r->m[1].last_ms > cfg.member_timeout_s * 1000ull) { st.member_expired++; member_leaves(r, "timed out", now); }
        if (r->relaying && now - r->last_relay_ms > 5000) { r->relaying = 0; V("room %s: the relay idle for 5 s (a direct path now, or a pause)", rname(r)); }
    }
    while (nrecent && recent[nrecent - 1].until <= now) nrecent--;
    if (!rate_ms) rate_ms = now;
    if (now - rate_ms >= 10000) {   /* the rates over the last ~10 s */
        double dt = (now - rate_ms) / 1000.0;
        st.req_rate = (st.requests - rate_req) / dt;
        st.relay_in_rate = (st.relay_in_bytes - rate_in) / dt;
        st.relay_out_rate = (st.relay_out_bytes - rate_out) / dt;
        rate_req = st.requests; rate_in = st.relay_in_bytes; rate_out = st.relay_out_bytes;
        for (i = 0; i < cfg.max_rooms; i++)
            if (rooms[i].id) { rooms[i].relay_rate = (rooms[i].relay_bytes - rooms[i].relay_mark) / dt; rooms[i].relay_mark = rooms[i].relay_bytes; }
        rate_ms = now;
    }
}

void srv_drain(int on) { draining = on != 0; }
int srv_draining(void) { return draining; }

/* ---------------------------------------------------------------- the handover state
   One set of functions writes and reads it (Io.wr): the order below is the format. Changing what
   is written means a new SRV_STATE_FORMAT; an import of an older format branches on it. Times are
   CLOCK_MONOTONIC milliseconds, the same for every process on the machine. */
typedef struct Io { uint8_t* b; uint32_t n, cap; int wr, bad; } Io;
static void io(Io* s, void* p, uint32_t n)
{
    if (s->bad || n > s->cap - s->n) { s->bad = 1; if (!s->wr) memset(p, 0, n); return; }
    if (s->wr) memcpy(s->b + s->n, p, n);
    else memcpy(p, s->b + s->n, n);
    s->n += n;
}
#define IO(x) io(s, &(x), sizeof(x))
static int fin(double v) { return v > -1e300 && v < 1e300; }   /* no NaN or infinity: a NaN budget would never run out */

static void io_addr(Io* s, RdvAddr* a) { IO(a->fam); IO(a->port); IO(a->ip); IO(a->via); }
static void io_member(Io* s, Member* m)
{
    IO(m->on); io_addr(s, &m->pub); io_addr(s, &m->lan); IO(m->pk); IO(m->token); IO(m->rejoin);
    IO(m->last_ms); IO(m->bytes); IO(m->dgrams); IO(m->fill_ms); IO(m->rx_top); IO(m->rx_win);
}
static void io_room(Io* s, Room* r)
{
    IO(r->id); IO(r->code); IO(r->req_id); IO(r->flags); IO(r->salt); IO(r->verifier); IO(r->member_seq);
    IO(r->throttle_log_ms); IO(r->opened_ms); IO(r->last_relay_ms); IO(r->relay_bytes); IO(r->relay_mark); IO(r->relay_rate);
    IO(r->relayed); IO(r->relaying); IO(r->pw_fail_start); IO(r->pw_last_ms); IO(r->pw_fails);
    io_member(s, &r->m[0]);
    io_member(s, &r->m[1]);
}
static void io_globals(Io* s)
{
    IO(secret); IO(gen); IO(draining); IO(drain_log_ms);
    IO(st.requests); IO(st.relay_in_dgrams); IO(st.relay_in_bytes); IO(st.relay_out_dgrams); IO(st.relay_out_bytes);
    IO(st.drop_rate); IO(st.drop_pps); IO(st.drop_bw); IO(st.drop_ip); IO(st.drop_total); IO(st.drop_invalid);
    IO(st.drop_unknown_room); IO(st.drop_foreign); IO(st.drop_relay_auth); IO(st.drop_replay); IO(st.drop_busy); IO(st.log_suppressed);
    IO(st.created); IO(st.closed); IO(st.expired); IO(st.joined); IO(st.join_failed); IO(st.code_retries);
    IO(st.rejoined); IO(st.kicked); IO(st.member_expired); IO(st.sessions_direct); IO(st.sessions_relayed);
    IO(st.req_rate); IO(st.relay_in_rate); IO(st.relay_out_rate); IO(srv_proc.send_failed); IO(srv_proc.cpu_user_s); IO(srv_proc.cpu_sys_s);
    IO(srv_proc.started_at); IO(srv_proc.start_ms); IO(srv_proc.handovers);
    IO(all_req); IO(all_req_fill); IO(all_fail_start); IO(all_log_ms); IO(all_fails);
    IO(total_bytes); IO(total_dgrams); IO(total_fill); IO(total_dfill);
    IO(rate_ms); IO(rate_req); IO(rate_in); IO(rate_out);
    IO(log_tokens); IO(log_fill); IO(log_held); IO(total_log_ms); IO(full_log_ms);
}
static void io_pre(Io* s, Pre* e) { IO(e->ip); IO(e->tokens); IO(e->last_ms); IO(e->log_ms); }
static void io_ad(Io* s, Addr* e)
{
    IO(e->ip); IO(e->fails); IO(e->creates); IO(e->seen_ms); IO(e->fail_start); IO(e->log_ms); IO(e->create_start);
    IO(e->rbytes); IO(e->rdgrams); IO(e->rfill);
}

int srv_state_export(uint8_t** out_b, uint32_t* out_n)
{
    Io s0, *s = &s0;
    uint32_t fmt = (uint32_t)srv_state_format, mr = cfg.max_rooms, nr = 0, nre = nrecent, np = 0, na = 0, i;
    for (i = 0; i < mr; i++) nr += rooms[i].id != 0;
    for (i = 0; i < PRE_N; i++) np += pres[i].used != 0;
    for (i = 0; i < ADDR_N; i++) na += addrs[i].used != 0;
    memset(&s0, 0, sizeof(s0));
    s->wr = 1;
    s->cap = 512 + sizeof(SrvStats) + nr * (sizeof(Room) + 8) + nre * (sizeof(Recent) + 8) + np * (sizeof(Pre) + 8) + na * (sizeof(Addr) + 8);
    if (!(s->b = malloc(s->cap))) return 0;
    io(s, (void*)"RXST", 4); IO(fmt); IO(mr);
    io_globals(s);
    IO(nr);
    for (i = 0; i < mr; i++) if (rooms[i].id) { IO(i); io_room(s, &rooms[i]); }
    IO(nre);
    for (i = 0; i < nre; i++) { IO(recent[i].code); IO(recent[i].until); }
    IO(np);
    for (i = 0; i < PRE_N; i++) if (pres[i].used) { IO(i); io_pre(s, &pres[i]); }
    IO(na);
    for (i = 0; i < ADDR_N; i++) if (addrs[i].used) { IO(i); io_ad(s, &addrs[i]); }
    io(s, (void*)"RXEN", 4);
    if (s->bad) { crypto_wipe(s->b, s->cap); free(s->b); return 0; }
    *out_b = s->b;
    *out_n = s->n;
    return 1;
}

/* everything an imported room must be (it only ever came from a server, but is checked anyway) */
static int room_ok(Room* r, uint32_t slot, int nsocks)
{
    int k;
    r->code[RDV_CODE_LEN] = 0;
    if (!r->id || (r->id & 0xFFFF) != slot || strlen(r->code) != RDV_CODE_LEN || strspn(r->code, RDV_CODE_CHARS) != RDV_CODE_LEN) return 0;
    if (r->m[0].on != 1 || (r->m[1].on != 0 && r->m[1].on != 1) || r->relayed > 1 || r->relaying > 1 || r->pw_fails < 0 || !fin(r->relay_rate)) return 0;
    for (k = 0; k < 2; k++) {
        Member* m = &r->m[k];
        if (!m->on) { memset(m, 0, sizeof(*m)); continue; }
        if (m->pub.fam != 4 || m->pub.via >= nsocks || (m->lan.fam != 0 && m->lan.fam != 4 && m->lan.fam != 6) || !fin(m->bytes) || !fin(m->dgrams)) return 0;
        m->lan.via = 0;
        m->ax = 0;
    }
    return 1;
}

int srv_state_import(const uint8_t* b, uint32_t n, SrvConfig* c, int nsocks)
{
    Io s0, *s = &s0;
    char magic[4];
    uint32_t fmt, mr, cnt, i, at;
    memset(&s0, 0, sizeof(s0));
    s->b = (uint8_t*)b;
    s->cap = n;
    io(s, magic, 4); IO(fmt);
    if (s->bad || memcmp(magic, "RXST", 4)) return -1;
    if (fmt != (uint32_t)srv_state_format) return 1;   /* (an older format, once there is one, is read here) */
    IO(mr);
    if (s->bad || mr < 1 || mr > 65535) return -1;
    if ((int)mr > cfg.max_rooms) {   /* the running server's rooms keep their slots */
        Room* nr = calloc(mr, sizeof(Room));
        Recent* nre = calloc(mr * 4, sizeof(Recent));
        if (!nr || !nre) { free(nr); free(nre); return -1; }
        free(rooms); free(recent);
        rooms = nr; recent = nre;
        cfg.max_rooms = c->max_rooms = (int)mr;
    }
    io_globals(s);
    if (s->bad || !fin(all_req) || !fin(total_bytes) || !fin(total_dgrams) || !fin(log_tokens) || all_fails < 0 || !(srv_proc.cpu_user_s >= 0) || !(srv_proc.cpu_sys_s >= 0) ||
        !fin(srv_proc.cpu_user_s + srv_proc.cpu_sys_s) || !fin(srv_proc.started_at) || !(st.req_rate >= 0) || !(st.relay_in_rate >= 0) ||
        !(st.relay_out_rate >= 0) || !fin(st.req_rate + st.relay_in_rate + st.relay_out_rate)) return -1;
    draining = draining != 0;
    IO(cnt);
    if (s->bad || cnt > mr) return -1;
    st.rooms = st.members = 0;   /* the gauges: counted again */
    for (i = 0; i < cnt; i++) {
        Room r;
        memset(&r, 0, sizeof(r));
        IO(at); io_room(s, &r);
        if (s->bad || at >= mr || rooms[at].id || !room_ok(&r, at, nsocks)) { crypto_wipe(&r, sizeof(r)); return -1; }
        rooms[at] = r;
        crypto_wipe(&r, sizeof(r));
        st.rooms++;
        st.members += rooms[at].m[1].on;
    }
    IO(cnt);
    if (s->bad || cnt > mr * 4) return -1;
    for (i = 0; i < cnt; i++) { IO(recent[i].code); IO(recent[i].until); recent[i].code[RDV_CODE_LEN] = 0; }
    nrecent = (int)cnt;
    IO(cnt);
    if (s->bad || cnt > PRE_N) return -1;
    for (i = 0; i < cnt; i++) {
        Pre e;
        memset(&e, 0, sizeof(e));
        IO(at); io_pre(s, &e);
        if (s->bad || at >= PRE_N || !fin(e.tokens)) return -1;
        e.used = 1;
        pres[at] = e;
    }
    IO(cnt);
    if (s->bad || cnt > ADDR_N) return -1;
    for (i = 0; i < cnt; i++) {
        Addr e;
        memset(&e, 0, sizeof(e));
        IO(at); io_ad(s, &e);
        if (s->bad || at >= ADDR_N || e.fails < 0 || e.creates < 0 || !fin(e.rbytes) || !fin(e.rdgrams)) return -1;
        e.used = 1;
        addrs[at] = e;
    }
    io(s, magic, 4);
    return !s->bad && s->n == n && !memcmp(magic, "RXEN", 4) ? 0 : -1;
}
#undef IO
