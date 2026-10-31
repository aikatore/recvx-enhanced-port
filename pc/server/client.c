/* recvx-client: a command-line co-op client for testing a server without the game. It runs the
   game's own client code (pc/src/net/rendezvous.c, net.c) and plays a fake game stream: a
   datagram per 33 ms frame with a timestamp, answered by the partner, so it measures the round
   trip over whatever path the rendezvous chose.
     recvx-client SERVER host [options]            prints the room code, waits for a partner
     recvx-client SERVER join CODE [options]
     recvx-client 127.0.0.1[:PORT] status          the server's metrics (only from the server machine)
   options: --pw PASSWORD, --relay (Via server), --no-punch (punches dropped: relay fallback),
   --loss PERCENT (received datagrams dropped at random: loss without tc/netem), --secs N,
   --size BYTES (game datagram size, default 1164), --dgrams N (datagrams per frame, default 1:
   load), --plain (no end-to-end encryption: the host does not encrypt, the joiner accepts a host
   that does not; without it a joiner refuses a plaintext host).
   Exit 0 when the partner's answers came back. */
#include "../src/net/rendezvous.h"
#include "../src/net/rdv_proto.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

const char* tr(const char* en) { return en; }   /* (host/tr.c: the game's language) */
void host_log(const char* fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    printf("[%6u] ", net_ms() % 1000000);
    vprintf(fmt, a);
    va_end(a);
    fflush(stdout);
}

/* STATUS: the metrics text, in one or more datagrams of whole lines */
static int status(const char* server)
{
    NetAddr srv, from;
    RdvMsg m;
    uint8_t b[RDV_DGRAM_MAX + 1];
    uint32_t start;
    int n, got = 0;
    struct timespec tick = { 0, 5000000 };
    if (net_open(0) != 1 || !rdv_server_parse(server, &srv)) { fprintf(stderr, "no socket, or cannot resolve %s\n", server); return 1; }
    memset(&m, 0, sizeof(m));
    m.type = RDV_STATUS;
    n = rdv_build(b, sizeof(b), &m);
    net_send(&srv, b, n);
    for (start = net_ms(); net_ms() - start < 500; nanosleep(&tick, NULL))
        while ((n = net_recv(&from, b, sizeof(b))) >= 0)
            if (rdv_parse(b, n, &m) == RDV_STATUS) { fwrite(m.payload, 1, m.payload_len, stdout); got++; start = net_ms() - 400; }
    if (!got) fprintf(stderr, "no answer from %s (STATUS is answered only from the server machine: 127.0.0.1)\n", server);
    return !got;
}

int main(int argc, char** argv)
{
    RdvConfig c;
    int i, secs = 15, loss = 0, size = 1164, pongs = 0, pings = 0, dgrams = 1;
    unsigned rtt_sum = 0, rtt_max = 0, rx_dgrams = 0;
    uint32_t start, last_print;
    struct timespec tick = { 0, 1000000 };   /* 1 ms: the round trip is measured, not the frame */
    uint32_t next_frame = 0;
    memset(&c, 0, sizeof(c));
    c.e2e = 1;
    if (argc == 3 && !strcmp(argv[2], "status")) return status(argv[1]);
    if (argc < 3 || (strcmp(argv[2], "host") && strcmp(argv[2], "join")) || (!strcmp(argv[2], "join") && argc < 4)) {
        fprintf(stderr, "usage: recvx-client SERVER[:PORT] host|join CODE [--pw P] [--relay] [--no-punch] [--loss PCT] [--secs N] [--size B] [--dgrams N] [--plain]\n"
                        "       recvx-client 127.0.0.1[:PORT] status\n");
        return 2;
    }
    c.host = !strcmp(argv[2], "host");
    i = 3;
    if (!c.host && !rdv_code_norm(argv[i++], c.code)) { fprintf(stderr, "not a room code: %s\n", argv[3]); return 2; }
    for (; i < argc; i++) {
        if (!strcmp(argv[i], "--pw") && i + 1 < argc) snprintf(c.password, sizeof(c.password), "%s", argv[++i]);
        else if (!strcmp(argv[i], "--relay")) c.force_relay = 1;
        else if (!strcmp(argv[i], "--no-punch")) c.no_punch = 1;
        else if (!strcmp(argv[i], "--plain")) c.e2e = 0;
        else if (!strcmp(argv[i], "--loss") && i + 1 < argc) loss = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--secs") && i + 1 < argc) secs = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) size = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dgrams") && i + 1 < argc) dgrams = atoi(argv[++i]);
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    if (size < 16 || size > 1200 - RDV_RELAY_HDR - RDV_E2E_OVER) size = 1200 - RDV_RELAY_HDR - RDV_E2E_OVER;
    if (net_open(0) != 1 || !rdv_server_parse(argv[1], &c.server)) { fprintf(stderr, "no socket, or cannot resolve %s\n", argv[1]); return 1; }
    srand((unsigned)time(NULL) ^ (unsigned)net_local_port());
    rdv_start(&c);
    start = last_print = net_ms();
    while (net_ms() - start < (uint32_t)secs * 1000) {
        unsigned char buf[1600], out[1200];
        NetAddr from;
        int n;
        while ((n = net_recv(&from, buf, sizeof(buf))) >= 0) {
            if (loss && rand() % 100 < loss) continue;
            if ((n = rdv_input(&from, buf, n, sizeof(buf))) <= 0) continue;
            rx_dgrams++;
            if (n >= 12 && buf[4] == 1) {   /* a ping: back with the same time */
                buf[4] = 2;
                rdv_send(buf, n);
            } else if (n >= 12 && buf[4] == 2) {
                uint32_t t, rtt;
                memcpy(&t, buf + 8, 4);
                rtt = net_ms() - t;
                pongs++;
                rtt_sum += rtt;
                if (rtt > rtt_max) rtt_max = rtt;
            }
        }
        if ((int32_t)(net_ms() - next_frame) < 0) { nanosleep(&tick, NULL); continue; }
        next_frame = net_ms() + 33;   /* a game frame */
        rdv_frame();
        if (rdv_error()) { printf("FAILED: %s\n", rdv_error()); return 1; }
        if (rdv_state() == RDV_ST_DIRECT || rdv_state() == RDV_ST_RELAY) {
            uint32_t t = net_ms();
            memset(out, 0, size);
            memcpy(out, "COOP", 4);
            out[4] = 1;
            memcpy(out + 8, &t, 4);
            for (i = 0; i < dgrams; i++) pings += rdv_send(out, size);
        }
        if (net_ms() - last_print >= 2000) {
            last_print = net_ms();
            printf("[%6u] %s%s%s | pings %d, answers %d, rtt avg %u ms max %u\n", net_ms() % 1000000, rdv_status(),
                   rdv_room()[0] ? ", room " : "", rdv_room(), pings, pongs, pongs ? rtt_sum / pongs : 0, rtt_max);
            fflush(stdout);
        }
    }
    printf("RESULT path=%s pings=%d answers=%d received=%u rtt_avg=%u rtt_max=%u wire_bytes_per_dgram=%d\n", rdv_path()[0] ? rdv_path() : "none",
           pings, pongs, rx_dgrams, pongs ? rtt_sum / pongs : 0, rtt_max, size + rdv_wire_extra());
    rdv_stop();
    return pongs > 0 ? 0 : 1;
}
