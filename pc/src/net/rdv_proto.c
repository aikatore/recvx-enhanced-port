/* Co-op server protocol: messages, room codes, keys and the end-to-end encryption (rdv_proto.h). */
#ifdef _WIN32
#define _CRT_RAND_S                     /* rand_s: RtlGenRandom */
#else
#include <errno.h>
#include <sys/random.h>
#endif
#include "rdv_proto.h"
#include "monocypher.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- little-endian packing */
typedef struct { uint8_t* p; int n, cap, bad; } W;
static void w8(W* w, unsigned v) { if (w->n + 1 > w->cap) w->bad = 1; else w->p[w->n++] = (uint8_t)v; }
static void w16(W* w, unsigned v) { w8(w, v); w8(w, v >> 8); }
static void w32(W* w, uint32_t v) { w16(w, v); w16(w, v >> 16); }
static void w64(W* w, uint64_t v) { w32(w, (uint32_t)v); w32(w, (uint32_t)(v >> 32)); }
static void wb(W* w, const void* s, int n) { if (w->n + n > w->cap) w->bad = 1; else { memcpy(w->p + w->n, s, n); w->n += n; } }
static void waddr(W* w, const RdvAddr* a) { w8(w, a->fam); w16(w, a->port); wb(w, a->ip, 16); }

typedef struct { const uint8_t* p; int n, at; } R;
static unsigned r8(R* r) { return r->at < r->n ? r->p[r->at++] : (r->at++, 0); }
static unsigned r16(R* r) { unsigned v = r8(r); return v | r8(r) << 8; }
static uint32_t r32(R* r) { uint32_t v = r16(r); return v | (uint32_t)r16(r) << 16; }
static uint64_t r64(R* r) { uint64_t v = r32(r); return v | (uint64_t)r32(r) << 32; }
static void rb(R* r, void* d, int n) { if (r->at + n <= r->n) memcpy(d, r->p + r->at, n); else memset(d, 0, n); r->at += n; }
static int raddr(R* r, RdvAddr* a) { a->fam = r8(r); a->port = r16(r); rb(r, a->ip, 16); return a->fam == 0 || a->fam == 4 || a->fam == 6; }

/* body sizes after the 8-byte header; -1: variable */
static const int body[RDV_TYPES] = { 0, -1, 39, 124, 36, 124, 96, 21, -1, -1, 20, 19, -1, 24, -1 };

int rdv_test_fail;

int rdv_parse(const uint8_t* b, int n, RdvMsg* m)
{
    R r = { b, n, 0 };
    char code[RDV_CODE_LEN + 1];
    memset(m, 0, sizeof(*m));
    if (n < RDV_HDR || n > RDV_DGRAM_MAX) return RDV_BAD_SIZE;
    if (r32(&r) != RDV_MAGIC) return RDV_BAD_HDR;
    m->version = r16(&r);
    m->type = r8(&r);
    m->flags = r8(&r);
    if (m->version != RDV_VERSION) return RDV_BAD_VERSION;
    if (m->type < RDV_HELLO || m->type >= RDV_TYPES) return RDV_BAD_TYPE;
    if (body[m->type] >= 0 && n != RDV_HDR + body[m->type]) return RDV_BAD_SIZE;
    switch (m->type) {
    case RDV_HELLO:
        if (n < RDV_HELLO_MIN) return RDV_BAD_SIZE;
        m->nonce = r32(&r);
        break;
    case RDV_WELCOME:
        m->nonce = r32(&r); rb(&r, m->cookie, 16);
        if (!raddr(&r, &m->pub)) return RDV_BAD_FIELD;
        break;
    case RDV_CREATE:
        rb(&r, m->cookie, 16); m->req_id = r64(&r); m->flags = r8(&r);
        rb(&r, m->salt, 16); rb(&r, m->verifier, 32); rb(&r, m->pk, 32);
        if (!raddr(&r, &m->lan) || (m->flags & ~(RDV_F_PW | RDV_F_E2E))) return RDV_BAD_FIELD;
        break;
    case RDV_ROOM:
        m->req_id = r64(&r); m->room_id = r32(&r); rb(&r, code, RDV_CODE_LEN); rb(&r, m->token, 16);
        code[RDV_CODE_LEN] = 0;
        if (!rdv_code_norm(code, m->code) || strcmp(code, m->code)) return RDV_BAD_FIELD;
        break;
    case RDV_JOIN:
        rb(&r, m->cookie, 16); rb(&r, code, RDV_CODE_LEN); m->flags = r8(&r); rb(&r, m->proof, 32); rb(&r, m->pk, 32);
        code[RDV_CODE_LEN] = 0;
        if (!raddr(&r, &m->lan) || (m->flags & ~RDV_F_PW)) return RDV_BAD_FIELD;
        rb(&r, m->rejoin, 16);
        if (!rdv_code_norm(code, m->code) || strcmp(code, m->code)) return RDV_BAD_FIELD;
        break;
    case RDV_INTRO:
        m->room_id = r32(&r); m->seq = r32(&r); m->role = r8(&r); m->flags = r8(&r); rb(&r, m->token, 16);
        if (!raddr(&r, &m->pub) || !raddr(&r, &m->lan) || m->role > 1 || (m->flags & ~(RDV_F_PW | RDV_F_E2E))) return RDV_BAD_FIELD;
        rb(&r, m->pk, 32);
        break;
    case RDV_PUNCH:
        m->room_id = r32(&r); m->attempt = r8(&r); rb(&r, m->mac, 16);
        if (m->flags & ~1) return RDV_BAD_FIELD;   /* flag 1: we have heard you */
        break;
    case RDV_RELAY:
        if (n <= RDV_RELAY_HDR) return RDV_BAD_SIZE;
        m->room_id = r32(&r); m->seq = r32(&r); rb(&r, m->mac, 8);
        m->payload = b + RDV_RELAY_HDR;
        m->payload_len = n - RDV_RELAY_HDR;
        break;
    case RDV_KEEPALIVE:   /* request 32 bytes (token, member sequence), reply 13 (whether a partner is in) */
        m->room_id = r32(&r);
        if (n == RDV_HDR + 24) { rb(&r, m->token, 16); m->seq = r32(&r); }
        else if (n == RDV_HDR + 5) { m->has_member = r8(&r); if (m->has_member > 1) return RDV_BAD_FIELD; m->flags |= 0x80; }
        else return RDV_BAD_SIZE;
        break;
    case RDV_CLOSE:
        m->room_id = r32(&r); rb(&r, m->token, 16);
        break;
    case RDV_KICK:   /* the host removes its partner: room, host token, the member sequence it means */
        m->room_id = r32(&r); rb(&r, m->token, 16); m->seq = r32(&r);
        break;
    case RDV_PING:   /* the server's probe 8 bytes (room id, probe id), the member's answer 28 (+ token, hold time) */
        m->room_id = r32(&r); m->seq = r32(&r);
        if (n == RDV_HDR + 28) { rb(&r, m->token, 16); m->hold_us = r32(&r); }
        else if (n == RDV_HDR + 8) m->flags |= 0x80;
        else return RDV_BAD_SIZE;
        break;
    case RDV_ERROR:
        m->err = r8(&r); m->version = r16(&r); rb(&r, m->salt, 16);
        if (m->err < RDV_E_VERSION || m->err > RDV_E_TOO_MANY) return RDV_BAD_FIELD;
        break;
    case RDV_STATUS:
        m->payload = b + RDV_HDR;
        m->payload_len = n - RDV_HDR;
        break;
    }
    return m->type;
}

int rdv_build(uint8_t* b, int cap, const RdvMsg* m)
{
    W w = { b, 0, cap < RDV_DGRAM_MAX ? cap : RDV_DGRAM_MAX, 0 };
    w32(&w, RDV_MAGIC); w16(&w, RDV_VERSION); w8(&w, m->type); w8(&w, m->type == RDV_PUNCH ? m->flags : 0);
    switch (m->type) {
    case RDV_HELLO: w32(&w, m->nonce); while (w.n < RDV_HELLO_MIN && !w.bad) w8(&w, 0); break;
    case RDV_WELCOME: w32(&w, m->nonce); wb(&w, m->cookie, 16); waddr(&w, &m->pub); break;
    case RDV_CREATE:
        wb(&w, m->cookie, 16); w64(&w, m->req_id); w8(&w, m->flags); wb(&w, m->salt, 16); wb(&w, m->verifier, 32);
        wb(&w, m->pk, 32); waddr(&w, &m->lan);
        break;
    case RDV_ROOM: w64(&w, m->req_id); w32(&w, m->room_id); wb(&w, m->code, RDV_CODE_LEN); wb(&w, m->token, 16); break;
    case RDV_JOIN: wb(&w, m->cookie, 16); wb(&w, m->code, RDV_CODE_LEN); w8(&w, m->flags); wb(&w, m->proof, 32); wb(&w, m->pk, 32); waddr(&w, &m->lan); wb(&w, m->rejoin, 16); break;
    case RDV_INTRO:
        w32(&w, m->room_id); w32(&w, m->seq); w8(&w, m->role); w8(&w, m->flags); wb(&w, m->token, 16);
        waddr(&w, &m->pub); waddr(&w, &m->lan); wb(&w, m->pk, 32);
        break;
    case RDV_PUNCH: w32(&w, m->room_id); w8(&w, m->attempt); wb(&w, m->mac, 16); break;
    case RDV_RELAY: w32(&w, m->room_id); w32(&w, m->seq); wb(&w, m->mac, 8); wb(&w, m->payload, m->payload_len); break;
    case RDV_KEEPALIVE:
        w32(&w, m->room_id);
        if (m->flags & 0x80) w8(&w, m->has_member);
        else { wb(&w, m->token, 16); w32(&w, m->seq); }
        break;
    case RDV_CLOSE: w32(&w, m->room_id); wb(&w, m->token, 16); break;
    case RDV_KICK: w32(&w, m->room_id); wb(&w, m->token, 16); w32(&w, m->seq); break;
    case RDV_PING:
        w32(&w, m->room_id); w32(&w, m->seq);
        if (!(m->flags & 0x80)) { wb(&w, m->token, 16); w32(&w, m->hold_us); }
        break;
    case RDV_ERROR: w8(&w, m->err); w16(&w, RDV_VERSION); wb(&w, m->salt, 16); break;
    case RDV_STATUS: wb(&w, m->payload, m->payload_len); break;
    default: return 0;
    }
    return w.bad ? 0 : w.n;
}

/* ---------------------------------------------------------------- room codes */
int rdv_code_norm(const char* in, char out[RDV_CODE_LEN + 1])
{
    int n = 0;
    for (; *in; in++) {
        int c = toupper((unsigned char)*in);
        if (c == '-' || c == ' ') continue;
        if (n == RDV_CODE_LEN || !strchr(RDV_CODE_CHARS, c)) return out[0] = 0, 0;
        out[n++] = (char)c;
    }
    out[n] = 0;
    return n == RDV_CODE_LEN;
}

void rdv_code_from(const uint8_t rnd[RDV_CODE_LEN], char out[RDV_CODE_LEN + 1])
{
    int i;
    for (i = 0; i < RDV_CODE_LEN; i++) out[i] = RDV_CODE_CHARS[rnd[i] & 31];
    out[i] = 0;
}

void rdv_wipe(void* p, int n) { crypto_wipe(p, n); }
int rdv_same16(const uint8_t a[16], const uint8_t b[16]) { return !crypto_verify16(a, b); }

int rdv_random(void* out, int n)
{
    uint8_t* p = out;
    int all = n;
    if (rdv_test_fail & 1) goto fail;
#ifdef _WIN32
    while (n > 0) {
        unsigned v;
        if (rand_s(&v)) goto fail;
        memcpy(p, &v, n < 4 ? n : 4);
        p += 4; n -= 4;
    }
#else
    while (n > 0) {
        ssize_t k = getrandom(p, n, 0);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) goto fail;
        p += k; n -= (int)k;
    }
#endif
    return 1;
fail:
    crypto_wipe(out, all);   /* nothing half random */
    return 0;
}

/* ---------------------------------------------------------------- keys */
void rdv_cookie(const uint8_t secret[32], const RdvAddr* a, uint32_t bucket, uint8_t out[16])
{
    uint8_t m[RDV_ADDR_LEN + 4];
    W w = { m, 0, sizeof(m), 0 };
    waddr(&w, a);
    w32(&w, bucket);
    crypto_blake2b_keyed(out, 16, secret, 32, m, sizeof(m));
}

/* ponytail: Argon2id 4 MB x 3 passes (~20 ms, once per create/join on the game thread); raise
   nb_blocks if offline guessing of weak passwords from a stolen verifier matters more */
int rdv_pw_key(const char* password, const uint8_t salt[16], uint8_t out[32])
{
    crypto_argon2_config cfg = { CRYPTO_ARGON2_ID, 4096, 3, 1 };
    crypto_argon2_inputs in = { (const uint8_t*)password, salt, (uint32_t)strlen(password), 16 };
    void* work = rdv_test_fail & 2 ? NULL : malloc(cfg.nb_blocks * 1024);
    crypto_wipe(out, 32);
    if (!work) return 0;   /* never a zero key that looks like a result */
    crypto_argon2(out, 32, work, cfg, in, crypto_argon2_no_extras);   /* wipes its work area */
    free(work);
    return 1;
}

void rdv_verifier(const uint8_t pw_key[32], uint8_t out[32]) { crypto_blake2b_keyed(out, 32, pw_key, 32, (const uint8_t*)"recvx verify", 12); }

void rdv_proof(const uint8_t verifier[32], const uint8_t cookie[16], const char* code, const uint8_t pk[32], uint8_t out[32])
{
    uint8_t m[16 + RDV_CODE_LEN + 32];
    memcpy(m, cookie, 16);
    memcpy(m + 16, code, RDV_CODE_LEN);
    memcpy(m + 16 + RDV_CODE_LEN, pk, 32);
    crypto_blake2b_keyed(out, 32, verifier, 32, m, sizeof(m));
}

void rdv_rejoin_tag(const uint8_t key[32], const char* code, uint8_t out[16])
{
    uint8_t m[15 + RDV_CODE_LEN + 4];
    int n = (int)strlen(code);
    if (n > (int)sizeof(m) - 15) n = (int)sizeof(m) - 15;
    memcpy(m, "recvx rejoin v1", 15);
    memcpy(m + 15, code, n);
    crypto_blake2b_keyed(out, 16, key, 32, m, 15 + n);
}

void rdv_punch_mac(const uint8_t key[32], const uint8_t* msg, int n, uint8_t out[16]) { crypto_blake2b_keyed(out, 16, key, 32, msg, n); }

static void relay_mac(const uint8_t* b, int n, const uint8_t token[16], uint8_t out[8])
{
    crypto_blake2b_ctx c;
    crypto_blake2b_keyed_init(&c, 8, token, 16);
    crypto_blake2b_update(&c, b, 16);   /* header, room id, counter */
    crypto_blake2b_update(&c, b + RDV_RELAY_HDR, n - RDV_RELAY_HDR);
    crypto_blake2b_final(&c, out);
}

int rdv_relay_seal(uint8_t* b, int payload_len, uint32_t room_id, uint32_t ctr, const uint8_t token[16])
{
    W w = { b, 0, RDV_RELAY_HDR, 0 };
    w32(&w, RDV_MAGIC); w16(&w, RDV_VERSION); w8(&w, RDV_RELAY); w8(&w, 0); w32(&w, room_id); w32(&w, ctr);
    relay_mac(b, RDV_RELAY_HDR + payload_len, token, b + 16);
    return RDV_RELAY_HDR + payload_len;
}

int rdv_relay_ok(const uint8_t* b, int n, const uint8_t token[16])
{
    uint8_t mac[8];
    if (n <= RDV_RELAY_HDR) return 0;
    relay_mac(b, n, token, mac);
    {   /* constant time */
        unsigned d = 0;
        int i;
        for (i = 0; i < 8; i++) d |= mac[i] ^ b[16 + i];
        return d == 0;
    }
}

int rdv_session(RdvSession* s, int host, const uint8_t my_sk[32], const uint8_t host_pk[32], const uint8_t join_pk[32],
                const uint8_t* pw_key, const RdvIntro* in)
{
    static const uint8_t zero[32];
    uint8_t ikm[12 + 96 + 17], base[64], k[32];
    crypto_blake2b_ctx c;
    int i;
    memset(s, 0, sizeof(*s));
    memcpy(ikm, "recvx e2e v2", 12);
    crypto_x25519(ikm + 12, my_sk, host ? join_pk : host_pk);
    if (!crypto_verify32(ikm + 12, zero)) { crypto_wipe(ikm, sizeof(ikm)); return 0; }   /* a low-order key: no secret at all */
    memcpy(ikm + 44, host_pk, 32);
    memcpy(ikm + 76, join_pk, 32);
    {   /* the introduction (rdv_proto.h RdvIntro) */
        W w = { ikm + 108, 0, 17, 0 };
        w32(&w, in->room_id); w32(&w, in->epoch); w8(&w, in->flags); wb(&w, in->code, RDV_CODE_LEN);
    }
    if (pw_key) crypto_blake2b_keyed(base, 64, pw_key, 32, ikm, sizeof(ikm));
    else crypto_blake2b(base, 64, ikm, sizeof(ikm));
    memcpy(host ? s->tx : s->rx, base, 32);   /* host to joiner */
    memcpy(host ? s->rx : s->tx, base + 32, 32);
    crypto_blake2b_init(&c, 32);
    crypto_blake2b_update(&c, base, 64);
    crypto_blake2b_update(&c, (const uint8_t*)"punch", 5);
    crypto_blake2b_final(&c, s->punch);
    crypto_blake2b_init(&c, 32);
    crypto_blake2b_update(&c, base, 64);
    crypto_blake2b_update(&c, (const uint8_t*)"code", 4);
    crypto_blake2b_final(&c, k);
    for (i = 0; i < 8; i++) s->code[i] = RDV_CODE_CHARS[k[i] & 31];
    s->code[8] = 0;
    for (i = 0; i < 6; i++) s->verify[i] = RDV_CODE_CHARS[k[8 + i] & 31];   /* other bytes of the same hash */
    s->verify[6] = 0;
    crypto_wipe(ikm, sizeof(ikm));
    crypto_wipe(base, sizeof(base));
    crypto_wipe(k, sizeof(k));
    return 1;
}

static void nonce_of(uint64_t ctr, uint8_t n[24])
{
    int i;
    memset(n, 0, 24);
    for (i = 0; i < 8; i++) n[i] = (uint8_t)(ctr >> (8 * i));
}

int rdv_seal(RdvSession* s, uint8_t* out, int cap, const void* in, int n)
{
    uint64_t ctr = __atomic_add_fetch(&s->tx_ctr, 1, __ATOMIC_RELAXED);   /* the exit handler may send a goodbye */
    uint8_t nonce[24];
    W w = { out, 0, cap, 0 };
    if (n < 0 || 12 + n + 16 > cap) return 0;
    w32(&w, RDV_E2E_MAGIC);
    w64(&w, ctr);
    nonce_of(ctr, nonce);
    crypto_aead_lock(out + 12, out + 12 + n, s->tx, nonce, out, 12, in, n);
    return 12 + n + 16;
}

int rdv_open(RdvSession* s, uint8_t* out, int cap, const uint8_t* in, int n)
{
    R r = { in, n, 0 };
    uint8_t nonce[24];
    uint64_t ctr;
    int len = n - RDV_E2E_OVER;
    if (len < 0 || len > cap || r32(&r) != RDV_E2E_MAGIC) return -1;
    ctr = r64(&r);
    if (rdv_replayed(s->rx_top, s->rx_win, ctr)) return -1;
    nonce_of(ctr, nonce);
    if (crypto_aead_unlock(out, in + 12 + len, s->rx, nonce, in, 12, in + 12, len)) return -1;
    rdv_replay_mark(&s->rx_top, &s->rx_win, ctr);
    return len;
}

int rdv_replayed(uint64_t top, uint64_t win, uint64_t ctr) { return ctr == 0 || (ctr <= top && (top - ctr >= 64 || (win >> (top - ctr) & 1))); }
void rdv_replay_mark(uint64_t* top, uint64_t* win, uint64_t ctr)
{
    if (ctr > *top) {
        uint64_t d = ctr - *top;
        *win = d >= 64 ? 1 : *win << d | 1;
        *top = ctr;
    } else *win |= 1ull << (*top - ctr);
}
