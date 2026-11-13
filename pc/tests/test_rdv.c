/* Co-op server protocol and server core (pc/src/net/rdv_proto.c, pc/server/server.c):
   message round trips, malformed input, a fuzz loop, the end-to-end encryption, and the room
   rules - unique server-assigned codes, a forced code collision, unknown codes, hijack attempts,
   CREATE retries, code reuse only after the grace period, passwords, relay, limits; the config
   file and flags (valid, invalid, unknown keys, overrides), the metrics, STATUS and the log. */
#include "../server/server.h"
#include "monocypher.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---------------------------------------------------------------- a fake network */
typedef struct { RdvAddr to; uint8_t b[RDV_DGRAM_MAX]; int n; } Sent;
static Sent sent[256];
static int nsent;
static void cap(const RdvAddr* to, const uint8_t* b, int n)
{
    if (nsent < 256) { sent[nsent].to = *to; memcpy(sent[nsent].b, b, n); sent[nsent].n = n; nsent++; }
}
static RdvAddr A(int last, int port) { RdvAddr a; memset(&a, 0, sizeof(a)); a.fam = 4; a.ip[0] = 198; a.ip[1] = 51; a.ip[2] = 100; a.ip[3] = last; a.port = port; return a; }
static uint64_t now = 1000000;

/* send one message, return the reply parsed (type or 0 when none) */
static int ask(const RdvAddr* from, const RdvMsg* q, RdvMsg* reply)
{
    uint8_t b[RDV_DGRAM_MAX];
    int n = rdv_build(b, sizeof(b), q), k;
    nsent = 0;
    srv_input(from, b, n, now);
    for (k = 0; k < nsent; k++)
        if (!memcmp(&sent[k].to, from, sizeof(*from))) return rdv_parse(sent[k].b, sent[k].n, reply);
    return 0;
}

static void hello(const RdvAddr* a, uint8_t cookie[16])
{
    RdvMsg q = { .type = RDV_HELLO, .nonce = 77 }, r;
    CHECK(ask(a, &q, &r) == RDV_WELCOME && r.nonce == 77 && r.pub.port == a->port);
    memcpy(cookie, r.cookie, 16);
}

static int create(const RdvAddr* a, uint64_t req, const char* pw, RdvMsg* room)
{
    RdvMsg q = { .type = RDV_CREATE, .req_id = req, .flags = RDV_F_E2E };
    hello(a, q.cookie);
    q.pk[0] = 1;
    if (pw) {
        uint8_t k[32];
        q.flags |= RDV_F_PW;
        memset(q.salt, 9, 16);
        rdv_pw_key(pw, q.salt, k);
        rdv_verifier(k, q.verifier);
    }
    return ask(a, &q, room);
}

static int join(const RdvAddr* a, const char* code, const char* pw, RdvMsg* r)
{
    RdvMsg q = { .type = RDV_JOIN };
    int t;
    hello(a, q.cookie);
    memcpy(q.code, code, RDV_CODE_LEN + 1);
    q.pk[0] = 2;
    t = ask(a, &q, r);
    if (t == RDV_ERROR && r->err == RDV_E_NEED_PW && pw) {
        uint8_t k[32], v[32];
        rdv_pw_key(pw, r->salt, k);
        rdv_verifier(k, v);
        rdv_proof(v, q.cookie, q.code, q.pk, q.proof);
        q.flags = RDV_F_PW;
        t = ask(a, &q, r);
    }
    return t;
}

/* a RELAY frame as a member sends it: a new counter each time, the MAC with its token */
static int relay(uint8_t* b, uint32_t room_id, const uint8_t token[16], const char* payload)
{
    static uint32_t ctr;
    int n = (int)strlen(payload);
    memcpy(b + RDV_RELAY_HDR, payload, n);
    return rdv_relay_seal(b, n, room_id, ++ctr, token);
}

static int fixed_rng_calls;
static void fixed_rng(uint8_t out[RDV_CODE_LEN]) { memset(out, fixed_rng_calls++ < 3 ? 0 : fixed_rng_calls, RDV_CODE_LEN); }   /* "AAAAAAAA" thrice */

static void test_messages(void)
{
    RdvMsg m, p;
    uint8_t b[RDV_DGRAM_MAX];
    int t, n;
    for (t = RDV_HELLO; t < RDV_TYPES; t++) {
        memset(&m, 0, sizeof(m));
        m.type = t;
        strcpy(m.code, "ABCD2345");
        m.room_id = 0x12345678; m.req_id = 0x1122334455667788ull; m.nonce = 5; m.err = RDV_E_FULL;
        m.pub.fam = 4; m.pub.port = 7140; m.lan.fam = 4;
        if (t == RDV_RELAY || t == RDV_STATUS) { m.payload = (const uint8_t*)"COOPdata"; m.payload_len = 8; }
        n = rdv_build(b, sizeof(b), &m);
        CHECK(n > 0);
        CHECK(rdv_parse(b, n, &p) == t);
        if (t == RDV_ROOM || t == RDV_JOIN) CHECK(!strcmp(p.code, "ABCD2345"));
        if (t == RDV_ROOM) CHECK(p.req_id == m.req_id && p.room_id == m.room_id);
        if (t != RDV_HELLO && t != RDV_RELAY && t != RDV_STATUS) {
            CHECK(rdv_parse(b, n - 1, &p) < 0);    /* one byte short */
            b[n] = 0;
            CHECK(rdv_parse(b, n + 1, &p) < 0);    /* one byte long */
        }
        b[0] ^= 1; CHECK(rdv_parse(b, n, &p) == RDV_BAD_HDR); b[0] ^= 1;
        b[4] = RDV_VERSION + 1; CHECK(rdv_parse(b, n, &p) == RDV_BAD_VERSION); b[4] = RDV_VERSION;
        b[4] = 1; CHECK(rdv_parse(b, n, &p) == RDV_BAD_VERSION); b[4] = RDV_VERSION;   /* protocol 1 clients */
        {   /* every buffer too small: 0, and no hang (HELLO's padding loop) */
            uint8_t s[RDV_DGRAM_MAX];
            int cap2;
            for (cap2 = 0; cap2 < n; cap2++) CHECK(rdv_build(s, cap2, &m) == 0);
            CHECK(rdv_build(s, n, &m) == n);
        }
    }
    m.type = RDV_HELLO; n = rdv_build(b, sizeof(b), &m);
    CHECK(n == RDV_HELLO_MIN && rdv_parse(b, 63, &p) == RDV_BAD_SIZE);
    b[6] = 0; CHECK(rdv_parse(b, n, &p) == RDV_BAD_TYPE);
    b[6] = RDV_TYPES; CHECK(rdv_parse(b, n, &p) == RDV_BAD_TYPE);
    memset(&m, 0, sizeof(m)); m.type = RDV_JOIN; strcpy(m.code, "ABCD2345"); n = rdv_build(b, sizeof(b), &m);
    b[8 + 16] = 'I'; CHECK(rdv_parse(b, n, &p) == RDV_BAD_FIELD);   /* not in the alphabet */
    b[8 + 16] = 'a'; CHECK(rdv_parse(b, n, &p) == RDV_BAD_FIELD);   /* lower case on the wire */
    b[8 + 16] = 'A'; b[8 + 16 + 8 + 1 + 64] = 5; CHECK(rdv_parse(b, n, &p) == RDV_BAD_FIELD);   /* address family */
    CHECK(rdv_parse(b, RDV_DGRAM_MAX + 1, &p) == RDV_BAD_SIZE);
    {
        char c[9];
        CHECK(rdv_code_norm("abcd-2345", c) && !strcmp(c, "ABCD2345"));
        CHECK(!rdv_code_norm("ABCD234", c) && !rdv_code_norm("ABCD23450", c) && !rdv_code_norm("ABCD-O345", c));
    }
}

static void test_fuzz(void)
{
    uint8_t b[RDV_DGRAM_MAX + 1];
    RdvMsg m;
    unsigned seed = 1;
    int i, k, ok = 0;
    for (i = 0; i < 300000; i++) {
        int n = 0, t;
        seed = seed * 1103515245 + 12345;
        if (i & 1) {   /* a valid message, mutated */
            memset(&m, 0, sizeof(m));
            m.type = 1 + (seed >> 16) % (RDV_TYPES - 1);
            strcpy(m.code, "ABCD2345");
            m.payload = b + 600; m.payload_len = 1 + (seed >> 8) % 500;
            n = rdv_build(b, sizeof(b), &m);
            for (k = 0; k < 1 + (int)(seed >> 28); k++) { seed = seed * 1103515245 + 12345; if (n) b[(seed >> 8) % n] ^= 1 << ((seed >> 4) & 7); }
            if ((seed >> 20) & 1 && n > 1) n -= 1 + (seed >> 9) % n;
        } else {
            n = (seed >> 16) % (RDV_DGRAM_MAX + 2);
            for (k = 0; k < n; k++) { seed = seed * 1103515245 + 12345; b[k] = seed >> 16; }
            if (n >= 8 && (seed & 1)) { b[0] = 'C'; b[1] = 'V'; b[2] = 'X'; b[3] = 'S'; b[4] = RDV_VERSION; b[5] = 0; }
        }
        t = rdv_parse(b, n < 0 ? 0 : n, &m);
        if (t > 0) {
            ok++;
            CHECK(t < RDV_TYPES && n >= RDV_HDR && n <= RDV_DGRAM_MAX);
            if (t == RDV_RELAY || t == RDV_STATUS) CHECK(m.payload >= b && m.payload + m.payload_len == b + n);
            if (t == RDV_JOIN || t == RDV_ROOM) CHECK(strlen(m.code) == RDV_CODE_LEN);
        }
        nsent = 0;
        {
            RdvAddr src = { 4, 1000 + (i & 7), { 10, 0, 0, i & 7 } };
            srv_input(&src, b, n < 0 ? 0 : n, now + i);   /* the server must survive it */
            for (k = 0; k < nsent; k++) {   /* no amplification: nothing bigger than the request goes to its (unverified) source */
                CHECK(!memcmp(&sent[k].to, &src, sizeof(src)));   /* and nothing to anyone else (no room has members here) */
                CHECK(sent[k].n <= n);
            }
        }
    }
    CHECK(ok > 1000);
}

static void test_crypto(void)
{
    uint8_t hs[32], hp[32], js[32], jp[32], pw[32], salt[16] = { 1 }, c[RDV_DGRAM_MAX], o[RDV_DGRAM_MAX];
    RdvSession h, j, x;
    const char* msg = "COOP game datagram";
    int n, i;
    for (i = 0; i < 32; i++) { hs[i] = i + 1; js[i] = 100 + i; }
    crypto_x25519_public_key(hp, hs);
    crypto_x25519_public_key(jp, js);
    RdvIntro in = { 0x10001, 1, RDV_F_PW | RDV_F_E2E, "ABCD2345" }, in2;
    rdv_pw_key("secret", salt, pw);
    CHECK(rdv_session(&h, 1, hs, hp, jp, pw, &in));
    CHECK(rdv_session(&j, 0, js, hp, jp, pw, &in));
    CHECK(!memcmp(h.tx, j.rx, 32) && !memcmp(h.rx, j.tx, 32) && !memcmp(h.punch, j.punch, 32) && !strcmp(h.code, j.code));
    CHECK(memcmp(h.tx, h.rx, 32));
    n = rdv_seal(&h, c, sizeof(c), msg, (int)strlen(msg) + 1);
    CHECK(n == (int)strlen(msg) + 1 + RDV_E2E_OVER);
    CHECK(rdv_open(&j, o, sizeof(o), c, n) == (int)strlen(msg) + 1 && !strcmp((char*)o, msg));
    CHECK(rdv_open(&j, o, sizeof(o), c, n) < 0);                  /* replayed */
    CHECK(rdv_open(&h, o, sizeof(o), c, n) < 0);                  /* own direction */
    n = rdv_seal(&h, c, sizeof(c), msg, 5);
    c[14] ^= 1; CHECK(rdv_open(&j, o, sizeof(o), c, n) < 0); c[14] ^= 1;   /* tampered */
    c[5] ^= 1; CHECK(rdv_open(&j, o, sizeof(o), c, n) < 0); c[5] ^= 1;     /* counter tampered */
    CHECK(rdv_open(&j, o, sizeof(o), c, n) == 5);
    rdv_session(&x, 0, js, hp, jp, NULL, &in);                     /* without the password: other keys */
    CHECK(memcmp(x.rx, j.rx, 32));
    /* the verification code the players compare: the same on both sides, another with other keys
       (a swapped public key, a server in the middle) */
    CHECK(strlen(h.verify) == 6 && !strcmp(h.verify, j.verify) && strcmp(x.verify, j.verify));
    rdv_session(&x, 0, js, hp, hp, pw, &in); CHECK(strcmp(x.verify, j.verify));
    /* the keys are bound to the introduction: another epoch, room, flag or code - other keys */
    in2 = in; in2.epoch = 2; rdv_session(&x, 0, js, hp, jp, pw, &in2); CHECK(memcmp(x.rx, j.rx, 32) && memcmp(x.punch, j.punch, 32));
    in2 = in; in2.room_id ^= 1; rdv_session(&x, 0, js, hp, jp, pw, &in2); CHECK(memcmp(x.rx, j.rx, 32));
    in2 = in; in2.flags &= ~RDV_F_E2E; rdv_session(&x, 0, js, hp, jp, pw, &in2); CHECK(memcmp(x.rx, j.rx, 32));
    in2 = in; in2.code[0] = 'Z'; rdv_session(&x, 0, js, hp, jp, pw, &in2); CHECK(memcmp(x.rx, j.rx, 32));
    {   /* a low-order peer key (all zero): refused, nothing derived */
        uint8_t zero[32] = { 0 };
        CHECK(!rdv_session(&x, 1, hs, hp, zero, NULL, &in) && x.tx_ctr == 0);
    }
    /* the password KDF fails closed: no zero key */
    rdv_test_fail = 2; memset(o, 0xAA, 32); CHECK(!rdv_pw_key("secret", salt, o)); rdv_test_fail = 0;
    {
        static const uint8_t z[32];
        CHECK(!crypto_verify32(o, z));   /* wiped, and reported */
        rdv_test_fail = 1; memset(o, 0xAA, 32); CHECK(!rdv_random(o, 32) && !crypto_verify32(o, z)); rdv_test_fail = 0;
    }
    /* RELAY frames: the MAC covers header, counter and payload */
    {
        uint8_t tok[16] = { 7 }, f[64];
        int n2 = relay(f, 0x10001, tok, "CVXEdata");
        CHECK(n2 == RDV_RELAY_HDR + 8 && rdv_relay_ok(f, n2, tok));
        f[RDV_RELAY_HDR + 2] ^= 1; CHECK(!rdv_relay_ok(f, n2, tok)); f[RDV_RELAY_HDR + 2] ^= 1;
        f[12] ^= 1; CHECK(!rdv_relay_ok(f, n2, tok)); f[12] ^= 1;     /* the counter */
        f[9] ^= 1; CHECK(!rdv_relay_ok(f, n2, tok)); f[9] ^= 1;       /* the room */
        tok[0] = 8; CHECK(!rdv_relay_ok(f, n2, tok));
    }
    {   /* out of order within the window is fine, each once */
        uint8_t d[5][64];
        int dn[5];
        for (i = 0; i < 5; i++) dn[i] = rdv_seal(&h, d[i], 64, "abc", 3);
        CHECK(rdv_open(&j, o, sizeof(o), d[4], dn[4]) == 3);
        CHECK(rdv_open(&j, o, sizeof(o), d[1], dn[1]) == 3);
        CHECK(rdv_open(&j, o, sizeof(o), d[1], dn[1]) < 0);
        for (i = 0; i < 100; i++) rdv_seal(&h, c, sizeof(c), "x", 1);
        n = rdv_seal(&h, c, sizeof(c), "x", 1);
        CHECK(rdv_open(&j, o, sizeof(o), c, n) == 1);
        CHECK(rdv_open(&j, o, sizeof(o), d[2], dn[2]) < 0);        /* older than the window */
    }
    CHECK(rdv_seal(&h, c, 40, msg, 20) == 0);                       /* does not fit */
}

static void test_server(void)
{
    SrvConfig c;
    RdvMsg r, r2, q;
    RdvAddr host = A(1, 5000), host2 = A(2, 5000), joiner = A(3, 6000), evil = A(66, 6666);
    char codes[200][9];
    int i, k;
    srv_defaults(&c);
    c.max_rooms = 256;
    c.rooms_per_ip = 1000;
    c.req_per_sec = 100000;
    c.joins_per_min = 1000;
    c.relay_pps = 200;   /* the burst test below */
    CHECK(srv_init(&c, cap));

    /* many creates from different hosts "at once" (same millisecond): all codes different */
    for (i = 0; i < 200; i++) {
        RdvAddr a = A(10 + i % 200, 7000 + i);
        CHECK(create(&a, i + 1, NULL, &r) == RDV_ROOM);
        memcpy(codes[i], r.code, 9);
        for (k = 0; k < i; k++) CHECK(strcmp(codes[k], codes[i]));
    }
    CHECK(srv_stats()->rooms == 200);
    srv_init(&c, cap);

    /* a forced collision: the rng gives AAAAAAAA three times; the second room retries */
    fixed_rng_calls = 0;
    srv_code_rng = fixed_rng;
    CHECK(create(&host, 1, NULL, &r) == RDV_ROOM && !strcmp(r.code, "AAAAAAAA"));
    CHECK(create(&host2, 1, NULL, &r2) == RDV_ROOM && strcmp(r2.code, "AAAAAAAA"));
    CHECK(srv_stats()->code_retries == 2);
    srv_code_rng = NULL;

    /* a duplicate CREATE (retry after a lost ROOM): the same room, same code and token */
    CHECK(create(&host, 1, NULL, &r2) == RDV_ROOM && !strcmp(r2.code, r.code) && r2.room_id == r.room_id && !memcmp(r2.token, r.token, 16));
    CHECK(srv_stats()->rooms == 2);
    /* a new request id from the same address: a new room, the old one closed */
    CHECK(create(&host, 2, NULL, &r2) == RDV_ROOM && strcmp(r2.code, r.code));
    CHECK(srv_stats()->rooms == 2);
    CHECK(join(&joiner, r.code, NULL, &q) == RDV_ERROR && q.err == RDV_E_NO_ROOM);   /* the old code is gone */
    r = r2;

    /* unknown code */
    CHECK(join(&joiner, "ZZZZ2222", NULL, &q) == RDV_ERROR && q.err == RDV_E_NO_ROOM);
    /* a stale cookie */
    {
        RdvMsg j = { .type = RDV_JOIN };
        memcpy(j.code, r.code, 9);
        CHECK(ask(&joiner, &j, &q) == RDV_ERROR && q.err == RDV_E_COOKIE);
    }

    /* join: INTRO to both */
    {
        RdvMsg j = { .type = RDV_JOIN };
        uint8_t b[RDV_DGRAM_MAX];
        int n, to_host = 0, to_joiner = 0;
        hello(&joiner, j.cookie);
        memcpy(j.code, r.code, 9);
        j.pk[0] = 2;
        n = rdv_build(b, sizeof(b), &j);
        nsent = 0;
        srv_input(&joiner, b, n, now);
        for (k = 0; k < nsent; k++) {
            RdvMsg m;
            CHECK(rdv_parse(sent[k].b, sent[k].n, &m) == RDV_INTRO);
            if (!memcmp(&sent[k].to, &host, sizeof(host))) { to_host++; CHECK(m.role == 0 && m.pk[0] == 2 && m.pub.port == 6000 && !memcmp(m.token, r.token, 16)); }
            if (!memcmp(&sent[k].to, &joiner, sizeof(joiner))) { to_joiner++; CHECK(m.role == 1 && m.pk[0] == 1 && m.pub.port == 5000 && m.room_id == r.room_id); q = m; }
        }
        CHECK(to_host == 1 && to_joiner == 1);
        /* a retried JOIN: the INTRO again, to the joiner only */
        CHECK(ask(&joiner, &j, &r2) == RDV_INTRO && nsent == 1 && !memcmp(r2.token, q.token, 16));
    }
    /* full */
    CHECK(join(&evil, r.code, NULL, &r2) == RDV_ERROR && r2.err == RDV_E_FULL);

    /* hijack: another address with the host's token, the joiner's token for the host's side, a wrong room id */
    {
        RdvMsg ka = { .type = RDV_KEEPALIVE, .room_id = r.room_id };
        uint64_t hj = srv_stats()->drop_foreign;
        memcpy(ka.token, r.token, 16);
        CHECK(ask(&evil, &ka, &r2) == 0);
        ka.type = RDV_CLOSE;
        CHECK(ask(&evil, &ka, &r2) == 0);
        memcpy(ka.token, q.token, 16);   /* the joiner's token from the host's address */
        CHECK(ask(&host, &ka, &r2) == 0);
        CHECK(srv_stats()->drop_foreign == hj + 3 && srv_stats()->rooms == 2);
        /* the host's own keepalive: an ack with "partner in" (it knows member 1) */
        ka.type = RDV_KEEPALIVE; ka.seq = 1;
        memcpy(ka.token, r.token, 16);
        CHECK(ask(&host, &ka, &r2) == RDV_KEEPALIVE && r2.has_member == 1);
        ka.seq = 0;   /* it missed the INTRO: it comes again */
        CHECK(ask(&host, &ka, &r2) == RDV_INTRO && r2.role == 0);
        /* a second CREATE from another address cannot take this room's code */
        CHECK(create(&evil, 1, NULL, &r2) == RDV_ROOM && strcmp(r2.code, r.code));
    }

    /* relay: between the two members only, each frame with its sender's MAC and a new counter */
    {
        uint8_t b[64];
        int n;
        now += 1000;   /* the members' budgets start empty and fill at their rate */
        n = relay(b, r.room_id, r.token, "CVXEpayload");
        nsent = 0; srv_input(&host, b, n, now);
        CHECK(nsent == 1 && !memcmp(&sent[0].to, &joiner, sizeof(joiner)) && sent[0].n == n && !memcmp(sent[0].b, b, n));
        n = relay(b, r.room_id, q.token, "CVXEpayload");
        nsent = 0; srv_input(&joiner, b, n, now);
        CHECK(nsent == 1 && !memcmp(&sent[0].to, &host, sizeof(host)));
        nsent = 0; srv_input(&joiner, b, n, now);   /* the same frame again: a replay */
        CHECK(nsent == 0 && srv_stats()->drop_replay == 1);
        n = relay(b, r.room_id, r.token, "CVXEpayload");
        nsent = 0; srv_input(&evil, b, n, now);     /* a third address, even with the host's token */
        CHECK(nsent == 0);
        n = relay(b, r.room_id, q.token, "CVXEpayload");
        nsent = 0; srv_input(&host, b, n, now);     /* the joiner's frame from the host's address: not the host's MAC */
        CHECK(nsent == 0 && srv_stats()->drop_relay_auth == 1);
        n = relay(b, r.room_id, q.token, "CVXEpayload");
        b[RDV_RELAY_HDR + 3] ^= 1;
        nsent = 0; srv_input(&joiner, b, n, now);   /* altered on the way */
        CHECK(nsent == 0 && srv_stats()->drop_relay_auth == 2);
        /* soft cap: 200 datagrams/s here; a burst of 400 gets ~200 through, the rest dropped and counted */
        for (i = 0, k = 0; i < 400; i++) { n = relay(b, r.room_id, r.token, "CVXEpayload"); nsent = 0; srv_input(&host, b, n, now + 1); k += nsent; }
        CHECK(k >= 190 && k <= 210 && srv_stats()->drop_pps >= 190 && srv_stats()->drop_bw == 0);
    }

    /* the joiner leaves: the room takes a new one; the host closes: the code is gone, and kept back */
    {
        RdvMsg cl = { .type = RDV_CLOSE, .room_id = r.room_id };
        uint64_t fg = srv_stats()->drop_foreign;
        memcpy(cl.token, q.token, 16);
        ask(&joiner, &cl, &r2);
        ask(&joiner, &cl, &r2);   /* the game's second CLOSE: its place is gone already, no foreign drop */
        CHECK(srv_stats()->drop_foreign == fg && srv_stats()->members == 0);
        CHECK(join(&evil, r.code, NULL, &r2) == RDV_INTRO);
        memcpy(cl.token, r.token, 16);
        ask(&host, &cl, &r2);
        ask(&host, &cl, &r2);     /* ... and the room's */
        CHECK(srv_stats()->drop_foreign == fg);
        CHECK(join(&joiner, r.code, NULL, &r2) == RDV_ERROR && r2.err == RDV_E_NO_ROOM);
    }

    /* code reuse after expiry: a room times out; its code is not handed out during the grace
       period (forced rng), afterwards it may be */
    srv_init(&c, cap);
    fixed_rng_calls = 0;
    srv_code_rng = fixed_rng;
    CHECK(create(&host, 1, NULL, &r) == RDV_ROOM && !strcmp(r.code, "AAAAAAAA"));
    now += 31000;
    srv_tick(now);
    CHECK(srv_stats()->rooms == 0 && srv_stats()->expired == 1);
    CHECK(join(&joiner, "AAAAAAAA", NULL, &r2) == RDV_ERROR && r2.err == RDV_E_NO_ROOM);
    CHECK(create(&host2, 1, NULL, &r2) == RDV_ROOM && strcmp(r2.code, "AAAAAAAA"));   /* in the grace period: another code */
    now += 121000;
    srv_tick(now);
    fixed_rng_calls = 0;
    CHECK(create(&host, 7, NULL, &r2) == RDV_ROOM && !strcmp(r2.code, "AAAAAAAA"));   /* after it: reused */
    srv_code_rng = NULL;

    /* password */
    srv_init(&c, cap);
    CHECK(create(&host, 1, "hunter2", &r) == RDV_ROOM);
    CHECK(join(&joiner, r.code, NULL, &r2) == RDV_ERROR && r2.err == RDV_E_NEED_PW);
    CHECK(join(&joiner, r.code, "wrong", &r2) == RDV_ERROR && r2.err == RDV_E_BAD_PW);
    CHECK(join(&joiner, r.code, "hunter2", &r2) == RDV_INTRO && (r2.flags & RDV_F_PW));

    /* limits: failed joins per minute, rooms per address, requests per second */
    c.joins_per_min = 5; c.rooms_per_ip = 2; c.req_per_sec = 20;
    srv_init(&c, cap);
    for (i = 0; i < 5; i++) CHECK(join(&evil, "ZZZZ2222", NULL, &r2) == RDV_ERROR && r2.err == RDV_E_NO_ROOM);
    CHECK(join(&evil, "ZZZZ2222", NULL, &r2) == RDV_ERROR && r2.err == RDV_E_RATE);
    now += 60000;
    CHECK(create(&host, 1, NULL, &r) == RDV_ROOM);
    { RdvAddr h2 = host; h2.port++; CHECK(create(&h2, 1, NULL, &r) == RDV_ROOM); h2.port++; CHECK(create(&h2, 1, NULL, &r) == RDV_ERROR && r.err == RDV_E_TOO_MANY); }
    {
        RdvMsg h = { .type = RDV_HELLO };
        for (i = k = 0; i < 100; i++) k += ask(&joiner, &h, &r2) == RDV_WELCOME;
        CHECK(k >= 35 && k <= 45);   /* a burst of 2 s worth */
    }
    srv_free();
}

/* ---------------------------------------------------------------- config file and flags */
static void test_config(void)
{
    SrvConfig c, d;
    FILE* devnull = fopen("/dev/null", "w");
    FILE* log = devnull ? devnull : stderr;
    char msg[160];
    int w = 0;
    srv_defaults(&d);
    /* valid: every key, comments, blanks, spaces, a BOM, CRLF, a section header, the old names */
    srv_defaults(&c);
    CHECK(srv_config_text(&c, "\xEF\xBB\xBF# comment\r\n\n[server]\nport = 7200\r\n  listen=127.0.0.1, 10.0.0.2  # two\n"
                              "max_rooms = 50\nrooms_per_ip=3\nrequests_per_sec = 9\nfailed_joins_per_min = 4 ; inline\n"
                              "relay_kb_per_sec = 300\nrelay_datagrams_per_sec = 400\nrelay_total_kb_per_sec = 10000\nroom_timeout = 40\n"
                              "relay_address_kb_per_sec = 700\nrelay_address_datagrams_per_sec = 800\nrelay_total_datagrams_per_sec = 0\n"
                              "code_grace = 60\nlog_level = verbose\nstats_interval = 0\nmember_timeout = 12\n", "t1", &w, log) == 0);
    CHECK(c.member_timeout_s == 12);
    CHECK(w == 0 && c.port == 7200 && c.nlisten == 2 && c.listen[1][0] == 10 && c.listen[1][3] == 2 && c.max_rooms == 50 && c.rooms_per_ip == 3);
    CHECK(c.req_per_sec == 9 && c.joins_per_min == 4 && c.relay_kbps == 300 && c.relay_pps == 400 && c.total_kbps == 10000);
    CHECK(c.room_timeout_s == 40 && c.grace_s == 60 && c.verbose == 1 && c.stats_s == 0);
    CHECK(c.ip_kbps == 700 && c.ip_pps == 800 && c.total_pps == 0);
    srv_defaults(&c);
    CHECK(srv_config_text(&c, "bind = 0.0.0.0\nrelay_kbps = 100\nrelay_pps = 50\ntimeout = 20\ngrace = 5\nverbose = yes\n", "t2", &w, log) == 0);
    CHECK(c.nlisten == 0 && c.relay_kbps == 100 && c.relay_pps == 50 && c.room_timeout_s == 20 && c.grace_s == 5 && c.verbose == 1);
    /* unknown keys: a warning each, the rest applies */
    srv_defaults(&c);
    w = 0;
    CHECK(srv_config_text(&c, "prot = 1\nport = 7300\nrelay_kbs = 9\n", "t3", &w, log) == 0 && w == 2 && c.port == 7300 && c.relay_kbps == d.relay_kbps);
    /* invalid values: an error each */
    {
        static const char* bad[] = { "port = 0", "port = 65536", "port = 7x", "port =", "port", "listen = 1.2.3", "listen = 1.2.3.256",
                                     "listen = ::1", "listen = 1.2.3.4 5.6.7.8 1.1.1.1 2.2.2.2 3.3.3.3 4.4.4.4 5.5.5.5 6.6.6.6 7.7.7.7",
                                     "max_rooms = 0", "max_rooms = 70000", "relay_kb_per_sec = 0", "relay_kb_per_sec = -5", "relay_total_kb_per_sec = -1",
                                     "room_timeout = 1", "member_timeout = 5", "code_grace = -1", "relay_address_kb_per_sec = 0", "relay_total_datagrams_per_sec = -1",
                                     "requests_per_sec = 0", "failed_joins_per_min = 0", "rooms_per_ip = 2x", "log_level = loud", "verbose = maybe", "stats_interval = 99999999999999999999", "requests_per_sec = 1.5" };
        int i;
        for (i = 0; i < (int)(sizeof(bad) / sizeof(bad[0])); i++) {
            srv_defaults(&c);
            CHECK(srv_config_text(&c, bad[i], "bad", NULL, log) == 1);
            if (strncmp(bad[i], "listen", 6)) CHECK(c.port == d.port && c.max_rooms == d.max_rooms && c.relay_kbps == d.relay_kbps);
            else CHECK(c.nlisten == 0);   /* a bad list takes none of its addresses */
        }
        srv_defaults(&c);
        CHECK(srv_config_text(&c, "port = 0\nmax_rooms = x\nport = 7400\n", "bad2", NULL, log) == 2 && c.port == 7400);
    }
    /* overrides: the file, then flags (srv_config_set as main.c calls it, flag spelling) */
    srv_defaults(&c);
    CHECK(srv_config_text(&c, "port = 7500\nrelay_kb_per_sec = 100\nlog_level = verbose\n", "t4", NULL, log) == 0);
    CHECK(srv_config_set(&c, "relay-kb-per-sec", "900", msg, sizeof(msg)) == 1 && c.relay_kbps == 900 && c.port == 7500);
    CHECK(srv_config_set(&c, "log-level", "normal", msg, sizeof(msg)) == 1 && c.verbose == 0);
    CHECK(srv_config_set(&c, "verbose", NULL, msg, sizeof(msg)) == 1 && c.verbose == 1 && srv_config_is_bool("verbose") && srv_config_is_bool("debug"));
    CHECK(srv_config_set(&c, "relay-kbps", "33", msg, sizeof(msg)) == 1 && c.relay_kbps == 33);   /* the old flag */
    CHECK(srv_config_set(&c, "nope", "1", msg, sizeof(msg)) == -1 && strstr(msg, "nope"));
    CHECK(srv_config_set(&c, "port", "99999", msg, sizeof(msg)) == 0 && strstr(msg, "range") && c.port == 7500);
    /* the example file shipped next to the server: valid, no unknown keys, the defaults */
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/../server/recvx-server.conf.example", RECVX_TEST_DIR);
        srv_defaults(&c);
        w = 0;
        CHECK(srv_config_file(&c, path, &w, stderr) == 0 && w == 0);
        CHECK(!memcmp(&c, &d, sizeof(c)));
        CHECK(srv_config_file(&c, "/nonexistent/recvx.conf", &w, log) == -1);
    }
    if (devnull) fclose(devnull);
}

static void read_log(FILE* f, char* buf, int cap)
{
    size_t n;
    fflush(f);
    rewind(f);
    n = fread(buf, 1, cap - 1, f);
    buf[n] = 0;
}

/* metrics, STATUS (localhost only, whole lines per datagram) and the log (budget, no secrets) */
static void test_metrics(void)
{
    SrvConfig c;
    RdvMsg r, q, st;
    RdvAddr host = A(1, 5000), joiner = A(3, 6000), local = { 4, 9000, { 127, 0, 0, 1 } };
    char logbuf[65536], text[8192];
    FILE* log = tmpfile(), *keep = srv_log;
    int i, n, len = 0;
    uint8_t b[RDV_DGRAM_MAX];
    srv_defaults(&c);
    c.relay_pps = 100;
    CHECK(log != NULL);
    srv_log = log;
    CHECK(srv_init(&c, cap));
    CHECK(create(&host, 1, "pw-secret", &r) == RDV_ROOM);
    CHECK(join(&joiner, r.code, "pw-secret", &q) == RDV_INTRO);
    /* 10 s of relay at 50 datagrams/s each way, then a burst over the cap; four more rooms relay
       a little (the busiest rooms in the metrics) */
    {
        RdvAddr h[4], j[4];
        RdvMsg r2[4];
        int k;
        for (k = 0; k < 4; k++) {
            RdvMsg q2;
            h[k] = A(20 + k, 5000);
            j[k] = A(30 + k, 6000);
            CHECK(create(&h[k], 1, NULL, &r2[k]) == RDV_ROOM && join(&j[k], r2[k].code, NULL, &q2) == RDV_INTRO);
        }
        srv_tick(now);
        for (i = 0; i < 500; i++) {
            now += 20;
            n = relay(b, r.room_id, r.token, "CVXEsecretpayload"); srv_input(&host, b, n, now);
            n = relay(b, r.room_id, q.token, "CVXEsecretpayload"); srv_input(&joiner, b, n, now);
            for (k = 0; k < 4; k++) if (i % (k + 2) == 0) { n = relay(b, r2[k].room_id, r2[k].token, "CVXEsecretpayload"); srv_input(&h[k], b, n, now); }
        }
        srv_tick(now);
        for (i = 0; i < 300; i++) { n = relay(b, r.room_id, r.token, "CVXEsecretpayload"); srv_input(&host, b, n, now); }
    }
    CHECK(srv_stats()->sessions_relayed == 5 && srv_stats()->relay_out_dgrams >= 1000 && srv_stats()->drop_pps > 100);
    CHECK(srv_stats()->relay_in_rate > 2000 && srv_stats()->rooms == 5 && srv_stats()->members == 5 && srv_stats()->joined == 5);
    srv_proc.uptime_s = 12;
    /* STATUS from elsewhere: nothing; from 127.0.0.1: the metrics, in datagrams of whole lines */
    memset(&st, 0, sizeof(st));
    st.type = RDV_STATUS;
    CHECK(ask(&joiner, &st, &q) == 0);
    n = rdv_build(b, sizeof(b), &st);
    nsent = 0;
    srv_input(&local, b, n, now);
    CHECK(nsent >= 2);   /* with five busy rooms the text is longer than one datagram */
    for (i = 0; i < nsent; i++) {
        RdvMsg m;
        CHECK(rdv_parse(sent[i].b, sent[i].n, &m) == RDV_STATUS && m.payload_len > 0 && m.payload[m.payload_len - 1] == '\n');
        memcpy(text + len, m.payload, m.payload_len);
        len += m.payload_len;
    }
    text[len] = 0;
    CHECK(strstr(text, "recvx_rooms 5\n") && strstr(text, "recvx_players 10\n") && strstr(text, "recvx_sessions_total{path=\"relay\"} 5\n"));
    CHECK(strstr(text, "recvx_uptime_seconds 12\n") && strstr(text, "recvx_drops_total{reason=\"relay_datagram_rate\"}"));
    {   /* the busiest rooms, the busiest first */
        char want[64];
        const char* at = strstr(text, "recvx_room_relay_bytes_per_second{room=\"");
        snprintf(want, sizeof(want), "{room=\"%.4s-xxxx\"} 4100\n", r.code);   /* 2 x 50 datagrams of 41 bytes a second */
        CHECK(at && !strncmp(strchr(at, '{'), want, strlen(want)));
        for (i = 0; at; i++) at = strstr(at + 1, "recvx_room_relay_bytes_per_second");
        CHECK(i == 5);
    }
    srv_summary(text, sizeof(text));
    CHECK(strstr(text, "rooms 5, players 10") && strstr(text, "top rooms: "));
    /* a flood of invalid datagrams in verbose mode: the log keeps to its budget */
    c.verbose = 1;
    srv_reconfig(&c);
    for (i = 0; i < 5000; i++) srv_input(&host, (const uint8_t*)"junk", 4, now);
    CHECK(srv_stats()->drop_invalid == 5000 && srv_stats()->log_suppressed >= 4700);
    now += 2000;
    srv_input(&host, (const uint8_t*)"junk", 4, now);
    read_log(log, logbuf, sizeof(logbuf));
    CHECK(strstr(logbuf, "log lines held back") != NULL && strstr(logbuf, "198.51.100.x") != NULL);
    CHECK(strstr(logbuf, "limit: room ") && strstr(logbuf, "relay_datagrams_per_sec 100"));
    /* never secrets: the password, the full room code, the payload, full addresses */
    CHECK(!strstr(logbuf, "pw-secret") && !strstr(logbuf, r.code) && !strstr(logbuf, "secretpayload") && !strstr(logbuf, "198.51.100.1 ") && !strstr(logbuf, "198.51.100.3"));
    /* normal mode: no addresses at all */
    fclose(log);
    log = tmpfile();
    srv_log = log;
    c.verbose = 0;
    CHECK(srv_init(&c, cap));
    now += 60000;
    CHECK(create(&host, 1, NULL, &r) == RDV_ROOM);
    CHECK(join(&joiner, "ZZZZ2222", NULL, &q) == RDV_ERROR);
    CHECK(join(&joiner, r.code, NULL, &q) == RDV_INTRO);   /* a session that never relays: direct */
    now += 31000;
    srv_tick(now);
    read_log(log, logbuf, sizeof(logbuf));
    CHECK(strstr(logbuf, "opened") && strstr(logbuf, "closed (the host timed out)") && !strstr(logbuf, "198.51") && !strstr(logbuf, "unknown room"));
    CHECK(srv_stats()->join_failed == 1 && srv_stats()->expired == 1 && srv_stats()->sessions_direct == 1);
    fclose(log);
    srv_log = keep;
    srv_free();
}

/* relay budgets: leaving and joining again (or a new room) does not refill a player's budget,
   the address's budget holds; the server total; the log queue without srv_log (main.c's) */
static void test_budgets(void)
{
    SrvConfig c;
    RdvMsg r, q;
    RdvAddr host = A(40, 5000), joiner = A(41, 6000);
    uint8_t b[RDV_DGRAM_MAX];
    int i, k, n, through = 0;
    srv_defaults(&c);
    c.relay_pps = 100; c.ip_pps = 150; c.total_pps = 0; c.total_kbps = 0;
    c.rooms_per_ip = 1000; c.req_per_sec = 100000; c.joins_per_min = 1000;
    CHECK(srv_init(&c, cap));
    /* the joiner churns: JOIN, a burst, CLOSE - 20 times in one second. A new membership's budget
       starts empty, so churning gets no more than one steady player (100 a second) */
    CHECK(create(&host, 1, NULL, &r) == RDV_ROOM);
    now += 1000;
    for (k = 0; k < 20; k++) {
        RdvMsg cl = { .type = RDV_CLOSE, .room_id = r.room_id };
        CHECK(join(&joiner, r.code, NULL, &q) == RDV_INTRO);
        now += 50;
        for (i = 0; i < 100; i++) { n = relay(b, r.room_id, q.token, "CVXEx"); nsent = 0; srv_input(&joiner, b, n, now); through += nsent; }
        memcpy(cl.token, q.token, 16);
        ask(&joiner, &cl, &q);
    }
    CHECK(through <= 105 && through >= 95);
    /* three hosts behind one address (three ports), all with partners: the address's 150 a second
       caps them together */
    CHECK(srv_init(&c, cap));
    {
        RdvMsg rr[3];
        RdvAddr hs[3];
        for (k = 0; k < 3; k++) {
            hs[k] = A(70, 5000 + k);
            RdvAddr j = A(71 + k, 6000);
            CHECK(create(&hs[k], 1, NULL, &rr[k]) == RDV_ROOM && join(&j, rr[k].code, NULL, &q) == RDV_INTRO);
        }
        now += 1000;
        for (k = through = 0; k < 3; k++)
            for (i = 0; i < 100; i++) { n = relay(b, rr[k].room_id, rr[k].token, "CVXEx"); nsent = 0; srv_input(&hs[k], b, n, now); through += nsent; }
        CHECK(through == 150 && srv_stats()->drop_ip == 150);
    }
    /* the server total: two rooms from different addresses share 120 datagrams a second */
    c.total_pps = 120;
    CHECK(srv_init(&c, cap));
    {
        RdvMsg rr[2];
        RdvAddr hs[2];
        for (k = 0; k < 2; k++) {
            RdvAddr j = A(60 + k, 6000);
            hs[k] = A(50 + k, 5000);
            CHECK(create(&hs[k], 1, NULL, &rr[k]) == RDV_ROOM && join(&j, rr[k].code, NULL, &q) == RDV_INTRO);
        }
        now += 1000;
        for (k = through = 0; k < 2; k++)
            for (i = 0; i < 100; i++) { n = relay(b, rr[k].room_id, rr[k].token, "CVXEx"); nsent = 0; srv_input(&hs[k], b, n, now); through += nsent; }
        CHECK(through == 120 && srv_stats()->drop_total == 80);
    }
    /* the log queue (srv_log NULL: main.c writes it out between batches): lines in order, bounded */
    {
        FILE* keep = srv_log;
        const char* p;
        int len;
        srv_log = NULL;
        p = srv_log_pending(&len);
        srv_log_done(len);
        srv_logf("one %d", 1);
        srv_logf("two");
        p = srv_log_pending(&len);
        CHECK(len == 10 && !memcmp(p, "one 1\ntwo\n", 10));
        srv_log_done(4);
        p = srv_log_pending(&len);
        CHECK(len == 6 && !memcmp(p, "1\ntwo\n", 6));
        srv_log_done(len);
        for (i = 0; i < 100000; i++) srv_logf("a line that nobody reads");   /* a stalled reader */
        p = srv_log_pending(&len);
        CHECK(len <= 65536 && len > 60000 && srv_stats()->log_suppressed > 90000);
        srv_log_done(len);
        srv_log = keep;
    }
    srv_free();
}

/* review findings 3, 4, 5, 7: the address tables, the global budgets, failing closed, liveness,
   the code grace under churn */
static RdvAddr B(int k) { RdvAddr a; memset(&a, 0, sizeof(a)); a.fam = 4; a.ip[0] = 100; a.ip[1] = 64 + (k >> 16 & 63); a.ip[2] = k >> 8; a.ip[3] = k; a.port = 9000; return a; }

static void test_hardening(void)
{
    SrvConfig c;
    RdvMsg r, q, h = { .type = RDV_HELLO };
    RdvAddr evil = A(66, 6666), host = A(1, 5000), joiner = A(3, 6000);
    uint8_t b[RDV_DGRAM_MAX];
    int i, n;
    srv_defaults(&c);
    c.req_per_sec = 100000; c.req_total_per_sec = 100000000; c.joins_total_per_min = 100000000;
    CHECK(srv_init(&c, cap));
    /* an address with 5 failed joins stays refused: spoofed HELLOs from 5000 other addresses (the
       collision attack) and 9000 real new addresses filling the table do not reset it */
    for (i = 0; i < 5; i++) CHECK(join(&evil, "ZZZZ2222", NULL, &r) == RDV_ERROR && r.err == RDV_E_NO_ROOM);
    for (i = 0; i < 5000; i++) { RdvAddr a = B(i); nsent = 0; n = rdv_build(b, sizeof(b), &h); srv_input(&a, b, n, now); }
    CHECK(join(&evil, "ZZZZ2222", NULL, &r) == RDV_ERROR && r.err == RDV_E_RATE);
    {
        unsigned long long busy = srv_stats()->drop_busy;
        for (i = 0; i < 9000; i++) { RdvAddr a = B(100000 + i); join(&a, "ZZZZ2222", NULL, &r); }
        CHECK(srv_stats()->drop_busy - busy >= 9000 + 1 - 8192);   /* the newcomers that found no slot were refused, */
        CHECK(join(&evil, "ZZZZ2222", NULL, &r) == RDV_ERROR && r.err == RDV_E_RATE);   /* no old entry was thrown out */
    }
    now += 121000;   /* two minutes idle: entries may be reused now (their budgets would have refilled anyway) */
    { RdvAddr a = B(200000); CHECK(join(&a, "ZZZZ2222", NULL, &r) == RDV_ERROR && r.err == RDV_E_NO_ROOM); }

    /* the global guess budget: 10 failed joins a minute from all addresses together */
    c.joins_total_per_min = 10;
    CHECK(srv_init(&c, cap));
    for (i = 0; i < 10; i++) { RdvAddr a = B(300000 + i); CHECK(join(&a, "ZZZZ2222", NULL, &r) == RDV_ERROR && r.err == RDV_E_NO_ROOM); }
    { RdvAddr a = B(300100); CHECK(join(&a, "ZZZZ2222", NULL, &r) == RDV_ERROR && r.err == RDV_E_RATE); }
    now += 60000;
    { RdvAddr a = B(300101); CHECK(join(&a, "ZZZZ2222", NULL, &r) == RDV_ERROR && r.err == RDV_E_NO_ROOM); }

    /* a room's password: no lockout. Past 10 wrong ones a minute, an address that failed waits 2 s
       after the room's last wrong one; a new address is always checked, so the joiner gets in */
    c.joins_total_per_min = 100000000;
    CHECK(srv_init(&c, cap));
    CHECK(create(&host, 1, "hunter2", &r) == RDV_ROOM);
    for (i = 0; i < 11; i++) { RdvAddr a = B(400000 + i); CHECK(join(&a, r.code, "wrong", &q) == RDV_ERROR && q.err == RDV_E_BAD_PW); }
    { RdvAddr a = B(400000); CHECK(join(&a, r.code, "wrong", &q) == RDV_ERROR && q.err == RDV_E_RATE);
      now += 2000; CHECK(join(&a, r.code, "wrong", &q) == RDV_ERROR && q.err == RDV_E_BAD_PW);
      CHECK(join(&a, r.code, "hunter2", &q) == RDV_ERROR && q.err == RDV_E_RATE); }   /* even the right one: it waits */
    CHECK(join(&joiner, r.code, "hunter2", &q) == RDV_INTRO);

    /* liveness: RELAY frames without the member's MAC do not keep it in the room */
    {
        uint8_t bad[16] = { 9 };
        RdvMsg ka = { .type = RDV_KEEPALIVE, .room_id = r.room_id, .seq = 1 };
        memcpy(ka.token, r.token, 16);   /* the host stays */
        for (i = 0; i < 11; i++) { now += 1000; n = relay(b, r.room_id, bad, "CVXEx"); srv_input(&joiner, b, n, now); ask(&host, &ka, &q); srv_tick(now); }
        CHECK(srv_stats()->rooms == 1 && srv_stats()->members == 0 && srv_stats()->drop_relay_auth == 11);   /* member_timeout 10 s */
    }

    /* no randomness: CREATE and JOIN are refused, no room or member with a predictable token */
    CHECK(srv_init(&c, cap));
    rdv_test_fail = 1;
    CHECK(create(&host, 1, NULL, &r) == RDV_ERROR && r.err == RDV_E_SERVER_FULL && srv_stats()->rooms == 0);
    rdv_test_fail = 0;
    CHECK(create(&host, 1, NULL, &r) == RDV_ROOM);
    rdv_test_fail = 1;
    CHECK(join(&joiner, r.code, NULL, &q) == RDV_ERROR && q.err == RDV_E_SERVER_FULL && srv_stats()->members == 0);
    rdv_test_fail = 0;
    CHECK(join(&joiner, r.code, NULL, &q) == RDV_INTRO);

    /* the code grace under churn: 4 rooms -> 16 slots for open rooms and retired codes. Opening
       and closing rooms from many addresses fills them; then closed codes are no longer kept for
       their grace period, but CREATE keeps working (before: every CREATE was refused - one churning
       client held the whole server; test_server checks that a kept code is not handed out) */
    c.max_rooms = 4;
    c.rooms_per_ip = 1000;
    CHECK(srv_init(&c, cap));
    {
        int ok = 0;
        for (i = 0; i < 40; i++) {
            RdvAddr a = B(500000 + i);
            RdvMsg cl = { .type = RDV_CLOSE };
            if (create(&a, 1, NULL, &r) != RDV_ROOM) break;
            ok++;
            cl.room_id = r.room_id;
            memcpy(cl.token, r.token, 16);
            ask(&a, &cl, &q);
        }
        CHECK(ok == 40);
    }

    /* one address opening rooms in a loop: rooms_per_ip_per_min (6); a CREATE retry does not count */
    c.max_rooms = 1024;
    CHECK(srv_init(&c, cap));
    {
        RdvAddr a = B(600000), v = B(600001);
        int ok = 0;
        for (i = 0; i < 10; i++) if (create(&a, 100 + i, NULL, &r) == RDV_ROOM) ok++;
        CHECK(ok == 6 && r.type == RDV_ERROR && r.err == RDV_E_RATE && srv_stats()->rooms == 1);   /* each new one closed the previous */
        CHECK(create(&a, 105, NULL, &r) == RDV_ROOM);   /* the open room's own request again: the same room */
        CHECK(create(&v, 1, NULL, &r) == RDV_ROOM);     /* other addresses are not affected */
        now += 60000;
        CHECK(create(&a, 200, NULL, &r) == RDV_ROOM);
    }

    /* RELAY from a member whose address entry was taken over (idle > 2 min, the table full around
       it): dropped as busy, no crash (the entry was used before its NULL check) */
    c.room_timeout_s = c.member_timeout_s = 600;
    CHECK(srv_init(&c, cap));
    {
        RdvAddr hh = A(201, 1000), jj = A(202, 1000);
        unsigned long long busy;
        CHECK(create(&hh, 1, NULL, &r) == RDV_ROOM);
        CHECK(join(&jj, r.code, NULL, &q) == RDV_INTRO);
        now += 130000;
        for (i = 0; i < 40000; i++) { RdvAddr a = B(700000 + i); join(&a, "ZZZZ2222", NULL, &q); }
        busy = srv_stats()->drop_busy;
        n = relay(b, r.room_id, r.token, "CVXEx");
        srv_input(&hh, b, n, now);
        CHECK(srv_stats()->drop_busy > busy && srv_stats()->relay_out_dgrams == 0);
    }
    srv_free();
}

/* protocol 3: a crashed joiner gets its place back at once (rejoin tag), the host removes a stuck
   partner (KICK), and a silent joiner's place is free after member_timeout (not room_timeout) */
static int join_tag(const RdvAddr* a, const char* code, const uint8_t tag[16], uint8_t pk, RdvMsg* r)
{
    RdvMsg q = { .type = RDV_JOIN };
    hello(a, q.cookie);
    memcpy(q.code, code, RDV_CODE_LEN + 1);
    q.pk[0] = pk;
    if (tag) memcpy(q.rejoin, tag, 16);
    return ask(a, &q, r);
}

static void test_rejoin(void)
{
    SrvConfig c;
    RdvMsg room, r, intro1;
    RdvAddr host = A(1, 5000), j1 = A(3, 6000), j2 = A(3, 6001), evil = A(66, 6666);
    uint8_t key[32], key2[32], tag[16], tag2[16], zero[16] = { 0 };
    srv_defaults(&c);
    c.req_per_sec = 100000; c.joins_per_min = 1000;
    CHECK(c.member_timeout_s == 10 && c.room_timeout_s == 30);
    CHECK(srv_init(&c, cap));
    memset(key, 7, 32); memset(key2, 8, 32);
    CHECK(create(&host, 1, NULL, &room) == RDV_ROOM);
    rdv_rejoin_tag(key, room.code, tag);
    rdv_rejoin_tag(key2, room.code, tag2);
    {   /* the tag depends on the key and the code */
        uint8_t t3[16];
        rdv_rejoin_tag(key, "ZZZZ2222", t3);
        CHECK(memcmp(tag, tag2, 16) && memcmp(tag, t3, 16));
    }
    CHECK(join_tag(&j1, room.code, tag, 2, &intro1) == RDV_INTRO && intro1.seq == 1);
    /* another player (other tag, no tag, a zero tag) is turned away while the place is taken */
    CHECK(join_tag(&evil, room.code, tag2, 3, &r) == RDV_ERROR && r.err == RDV_E_FULL);
    CHECK(join_tag(&evil, room.code, NULL, 3, &r) == RDV_ERROR && r.err == RDV_E_FULL);
    /* the same player from a new address (its game crashed and started again): the place is its at once */
    CHECK(join_tag(&j2, room.code, tag, 4, &r) == RDV_INTRO && r.seq == 2 && memcmp(r.token, intro1.token, 16));
    CHECK(srv_stats()->rejoined == 1 && srv_stats()->members == 1);
    {   /* the old member's token no longer works */
        RdvMsg ka = { .type = RDV_KEEPALIVE, .room_id = intro1.room_id, .seq = 1 };
        memcpy(ka.token, intro1.token, 16);
        CHECK(ask(&j1, &ka, &r) == 0);
    }
    /* KICK: only the host, with its token and the member it means */
    {
        RdvMsg k = { .type = RDV_KICK, .room_id = room.room_id, .seq = 2 };
        uint64_t foreign = srv_stats()->drop_foreign;
        memcpy(k.token, room.token, 16);
        CHECK(ask(&evil, &k, &r) == 0 && srv_stats()->drop_foreign == foreign + 1);   /* another address */
        k.seq = 1;   /* the member before: changes nothing */
        memcpy(k.token, room.token, 16);
        CHECK(ask(&host, &k, &r) == RDV_KEEPALIVE && r.has_member == 1 && srv_stats()->members == 1);
        k.seq = 2;
        CHECK(ask(&host, &k, &r) == RDV_KEEPALIVE && r.has_member == 0 && srv_stats()->members == 0 && srv_stats()->kicked == 1);
        CHECK(ask(&host, &k, &r) == RDV_KEEPALIVE && r.has_member == 0 && srv_stats()->kicked == 1);   /* a copy: nothing more */
    }
    /* the room and its code stay: anyone joins with the same code */
    CHECK(join_tag(&evil, room.code, zero, 5, &r) == RDV_INTRO && r.seq == 3);
    {   /* a member without a tag: another zero tag is not "the same player" */
        RdvMsg r2;
        CHECK(join_tag(&j2, room.code, zero, 9, &r2) == RDV_ERROR && r2.err == RDV_E_FULL);
    }
    /* a joiner's KICK (its own token) is not the host's */
    {
        RdvMsg k = { .type = RDV_KICK, .room_id = room.room_id, .seq = 3 };
        memcpy(k.token, r.token, 16);
        CHECK(ask(&evil, &k, &r) == 0 && srv_stats()->members == 1);
    }
    /* member_timeout: the joiner (zero tag: no rejoin) falls silent; the host keeps its keepalives */
    {
        RdvMsg ka = { .type = RDV_KEEPALIVE, .room_id = room.room_id, .seq = 3 };
        int s;
        memcpy(ka.token, room.token, 16);
        for (s = 0; s < 12; s++) { now += 1000; srv_tick(now); CHECK(ask(&host, &ka, &r) == RDV_KEEPALIVE); }
        CHECK(srv_stats()->members == 0 && srv_stats()->member_expired == 1 && srv_stats()->rooms == 1);
        CHECK(join_tag(&j1, room.code, tag, 6, &r) == RDV_INTRO && r.seq == 4);
        CHECK(join_tag(&j2, room.code, zero, 7, &r) == RDV_ERROR && r.err == RDV_E_FULL);   /* no tag: not this player */
    }
    srv_free();
}

/* the handover state (main.c --take-over): a round trip keeps the room, its tokens, code and the
   counters; every truncation and a wrong socket count are refused, another format is
   "incompatible", flipped bytes never crash; status-json carries no code */
static void test_state(void)
{
    SrvConfig c;
    RdvMsg room, in, r, k = { .type = RDV_KEEPALIVE };
    RdvAddr h = A(1, 5000), j = A(2, 6000);
    uint8_t *blob, *bad, b[RDV_DGRAM_MAX];
    uint32_t n, i;
    static char js[65536];
    srv_defaults(&c);
    c.max_rooms = 8;
    CHECK(srv_init(&c, cap));
    CHECK(create(&h, 1, "pw", &room) == RDV_ROOM);
    CHECK(join(&j, room.code, "pw", &in) == RDV_INTRO);
    CHECK(srv_state_export(&blob, &n) && n > 100);
    c.max_rooms = 4;   /* a smaller max_rooms in the new config: the running server's is kept */
    CHECK(srv_init(&c, cap));
    CHECK(srv_state_import(blob, n, &c, 1) == 0 && c.max_rooms == 8);
    CHECK(srv_stats()->rooms == 1 && srv_stats()->members == 1 && srv_stats()->created == 1 && srv_stats()->joined == 1);
    k.room_id = room.room_id;
    k.seq = in.seq;
    memcpy(k.token, room.token, 16);
    CHECK(ask(&h, &k, &r) == RDV_KEEPALIVE && r.has_member);   /* the host's token still works */
    nsent = 0;
    now += 1000;   /* the relay budgets start empty */
    srv_input(&j, b, relay(b, room.room_id, in.token, "after"), now);
    CHECK(nsent == 1 && !memcmp(&sent[0].to, &h, sizeof(h)));   /* the joiner's relay frames reach the host */
    CHECK(srv_status_json(js, sizeof(js), now) > 0 && !strstr(js, room.code) && strstr(js, "\"rooms\":1,") && strstr(js, "\"path\":\"relay\""));
    srv_pause(1000000, now);   /* a pause (a handover, UDP not read) never moves a time-out past now ... */
    now += 9000; srv_tick(now);
    CHECK(srv_stats()->members == 1);
    srv_pause(9000, now);      /* ... and is not the joiner's silence: 18 s, 9 of them paused */
    now += 9000; srv_tick(now);
    CHECK(srv_stats()->members == 1);
    now += 2000; srv_tick(now);
    CHECK(srv_stats()->members == 0 && srv_stats()->rooms == 1);
    for (i = 0; i < n; i += 1 + (i > 64) * 7) { CHECK(srv_init(&c, cap)); CHECK(srv_state_import(blob, i, &c, 1) != 0); }
    CHECK(srv_init(&c, cap));
    CHECK(srv_state_import(blob, n, &c, 0) == -1);   /* an address on a socket that did not come along */
    bad = malloc(n);
    memcpy(bad, blob, n);
    bad[4] ^= 0x40;
    CHECK(srv_init(&c, cap));
    CHECK(srv_state_import(bad, n, &c, 1) == 1);
    for (i = 0; i < n; i++) {
        memcpy(bad, blob, n);
        bad[i] ^= 0x5A;
        CHECK(srv_init(&c, cap));
        srv_state_import(bad, n, &c, 1);   /* any answer, but no crash */
    }
    free(bad);
    free(blob);
    {   /* a rate that is not a finite number >= 0 (NaN in status-json until the next tick) is not valid */
        double* rates[] = { &((SrvStats*)srv_stats())->req_rate, &((SrvStats*)srv_stats())->relay_in_rate, &((SrvStats*)srv_stats())->relay_out_rate };
        for (i = 0; i < 6; i++) {
            CHECK(srv_init(&c, cap));
            *rates[i % 3] = i < 3 ? NAN : -1;
            CHECK(srv_state_export(&blob, &n));
            CHECK(srv_init(&c, cap));
            CHECK(srv_state_import(blob, n, &c, 1) == -1);
            free(blob);
        }
    }
}

int main(void)
{
    test_messages();
    test_crypto();
    srv_log = fopen("/dev/null", "w");   /* the floods below log (NULL elsewhere: stdout) */
    test_server();
    test_config();
    test_metrics();
    test_budgets();
    test_hardening();
    test_rejoin();
    test_state();
    {
        SrvConfig c;
        srv_defaults(&c);
        c.max_rooms = 16;
        srv_init(&c, cap);
    }
    test_fuzz();
    srv_free();
    printf(fails ? "test_rdv: %d failures\n" : "test_rdv: all passed\n", fails);
    return fails != 0;
}
