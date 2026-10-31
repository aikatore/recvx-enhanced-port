/* Fuzz target for the co-op server protocol (docs/SERVER.md "Security"), built with AddressSanitizer
   and UBSan (ctest rdv_fuzz). LLVMFuzzerTestOneInput works with libFuzzer (clang -fsanitize=fuzzer
   -DRDV_LIBFUZZER); without it, main() runs its own mutation loop over seeds of every message type.
   The first input byte picks the target:
     0  stateless: rdv_parse, rebuild and parse again (same type), rdv_open, rdv_relay_ok
     1  stateful server: a sequence of datagrams from 8 addresses into srv_input, time passing
     2  stateful client: rendezvous.c (host or joiner) fed datagrams from the server and a partner */
#include "../server/server.h"
#include "../src/net/rendezvous.h"
#include "monocypher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>

/* the client's network and clock (rendezvous.c), all fake */
static uint32_t clk = 1000;
const char* tr(const char* en) { return en; }   /* (host/tr.c: the game's language) */
void host_log(const char* fmt, ...) { (void)fmt; }
uint32_t net_ms(void) { return clk; }
int net_local_ip(char* out, int cap) { (void)out; (void)cap; return 0; }
int net_local_port(void) { return 40000; }
int net_resolve(const char* h, int p, NetAddr* o) { (void)h; (void)p; (void)o; return 0; }
const char* net_addr_str(const NetAddr* a) { (void)a; return "x"; }
NetResolve* net_resolve_start(const char* h, int p) { (void)h; (void)p; return NULL; }
int net_resolve_poll(NetResolve* r, NetAddr* o) { (void)r; (void)o; return -1; }
void net_resolve_cancel(NetResolve* r) { (void)r; }
int net_send(const NetAddr* to, const void* b, int n) { (void)to; (void)b; return n > 0 && n <= RDV_DGRAM_MAX + RDV_RELAY_HDR; }
static void srv_send(const RdvAddr* to, const uint8_t* b, int n) { (void)to; (void)b; if (n > RDV_DGRAM_MAX) abort(); }

static void stateless(const uint8_t* d, int n)
{
    RdvMsg m, m2;
    uint8_t b[RDV_DGRAM_MAX], o[RDV_DGRAM_MAX], tok[16] = { 1 };
    static RdvSession s;
    int t = rdv_parse(d, n, &m), k;
    if (t > 0) {
        if (t == RDV_RELAY || t == RDV_STATUS) { if (m.payload < d || m.payload + m.payload_len != d + n) abort(); }
        k = rdv_build(b, sizeof(b), &m);
        if (k && rdv_parse(b, k, &m2) != t) abort();   /* what parses rebuilds into the same type */
    }
    rdv_relay_ok(d, n, tok);
    rdv_open(&s, o, sizeof(o), d, n);
}

static void server(const uint8_t* d, int n)
{
    static int init;
    if (!init) {
        SrvConfig c;
        srv_defaults(&c);
        c.max_rooms = 8;
        srv_log = fopen("/dev/null", "w");
        srv_init(&c, srv_send);
        init = 1;
    }
    while (n >= 3) {   /* [address and time byte][length, 2 bytes LE][datagram] ... */
        RdvAddr a;
        int len = d[1] | d[2] << 8, who = d[0] & 7;
        memset(&a, 0, sizeof(a));
        a.fam = 4; a.ip[0] = who == 7 ? 127 : 10; a.ip[3] = who; a.port = 1000 + who;
        clk += d[0] >> 3;
        d += 3; n -= 3;
        if (len > n) len = n;
        if (len >= RDV_HDR + 16 && (d[6] == RDV_CREATE || d[6] == RDV_JOIN) && len <= RDV_DGRAM_MAX) {   /* a valid cookie: deeper */
            uint8_t b[RDV_DGRAM_MAX];
            memcpy(b, d, len);
            srv_test_cookie(&a, clk, b + RDV_HDR);
            srv_input(&a, b, len, clk);
        } else srv_input(&a, d, len, clk);
        srv_tick(clk);
        d += len; n -= len;
    }
}

static void client(const uint8_t* d, int n)
{
    RdvConfig c;
    memset(&c, 0, sizeof(c));
    c.host = n > 0 && (d[0] & 1);
    c.e2e = 1;
    c.server.ip = htonl(0x0A000009);
    c.server.port = htons(7141);
    memcpy(c.code, "ABCD2345", 9);
    rdv_start(&c);
    while (n >= 3) {
        NetAddr from = c.server;
        uint8_t b[RDV_DGRAM_MAX + 64];
        int len = d[1] | d[2] << 8;
        if (d[0] & 1) from.port = htons(40001);   /* the partner instead of the server */
        clk += d[0] >> 1;
        d += 3; n -= 3;
        if (len > n) len = n;
        if (len > RDV_DGRAM_MAX) len = RDV_DGRAM_MAX;
        memcpy(b, d, len);
        rdv_input(&from, b, len, sizeof(b));
        rdv_frame();
        rdv_send("COOPfuzz", 9);
        d += len; n -= len;
    }
    rdv_stop();
}

int LLVMFuzzerTestOneInput(const uint8_t* d, size_t n)
{
    if (!n || n > 1 << 16) return 0;
    switch (d[0] % 3) {
    case 0: stateless(d + 1, (int)n - 1); break;
    case 1: server(d + 1, (int)n - 1); break;
    default: client(d + 1, (int)n - 1); break;
    }
    return 0;
}

#ifndef RDV_LIBFUZZER
/* seeds: one valid datagram of every type (and a relay frame with a MAC), then random mutations */
int main(int argc, char** argv)
{
    static uint8_t seeds[RDV_TYPES + 1][RDV_DGRAM_MAX];
    int seed_n[RDV_TYPES + 1], ns = 0, t, iters = argc > 1 ? atoi(argv[1]) : 200000, i;
    unsigned r = (unsigned)time(NULL);
    for (t = RDV_HELLO; t < RDV_TYPES; t++) {
        RdvMsg m;
        memset(&m, 0, sizeof(m));
        m.type = t;
        strcpy(m.code, "ABCD2345");
        m.pub.fam = 4; m.lan.fam = 4;
        if (t == RDV_RELAY || t == RDV_STATUS) { m.payload = (const uint8_t*)"COOPpayload"; m.payload_len = 11; }
        if ((seed_n[ns] = rdv_build(seeds[ns], RDV_DGRAM_MAX, &m))) ns++;
    }
    {
        uint8_t tok[16] = { 1 };
        memcpy(seeds[ns] + RDV_RELAY_HDR, "CVXEdata", 8);
        seed_n[ns] = rdv_relay_seal(seeds[ns], 8, 0x10001, 1, tok);
        ns++;
    }
    printf("fuzz_rdv: %d iterations, seed %u\n", iters, r);
    for (i = 0; i < iters; i++) {
        uint8_t in[4096];
        int n = 1, k, parts;
        r = r * 1103515245 + 12345;
        in[0] = (uint8_t)(r >> 16);
        parts = 1 + (r >> 8) % 6;
        for (k = 0; k < parts && n < (int)sizeof(in) - RDV_DGRAM_MAX - 3; k++) {   /* datagrams, mutated seeds or noise */
            int s = (r = r * 1103515245 + 12345) >> 16 & 0xFFFF, len, j;
            uint8_t* p = in + n + 3;
            if (s & 1) { int q = (s >> 1) % ns; len = seed_n[q]; memcpy(p, seeds[q], len); }
            else { len = (s >> 1) % 200; for (j = 0; j < len; j++) p[j] = (uint8_t)((r = r * 1103515245 + 12345) >> 16); }
            for (j = 0; j < 1 + (s >> 12); j++) { r = r * 1103515245 + 12345; if (len) p[(r >> 8) % len] ^= 1 << (r >> 4 & 7); }
            if ((s >> 3 & 7) == 0 && len > 1) len -= 1 + (r >> 9) % len / 2;
            in[n] = (uint8_t)(r >> 20);
            in[n + 1] = (uint8_t)len;
            in[n + 2] = (uint8_t)(len >> 8);
            n += 3 + len;
        }
        LLVMFuzzerTestOneInput(in, n);
    }
    printf("fuzz_rdv: done (server: %u rooms opened, %u joins, %u failed, %llu invalid)\n", srv_stats()->created, srv_stats()->joined,
           srv_stats()->join_failed, srv_stats()->drop_invalid);
    srv_free();
    return 0;
}
#endif
