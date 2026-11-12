/* Non-blocking UDP (BSD sockets / Winsock2), see net.h. */
#include "net.h"
#ifndef RECVX_NET_NO_SDL
#include <SDL3/SDL.h>
#else
#include <time.h>                       /* the CLI test client (pc/server/client.c): no SDL */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define BAD_SOCK INVALID_SOCKET
#define closesock closesocket
static void set_nonblock(sock_t s) { u_long on = 1; ioctlsocket(s, FIONBIO, &on); }
static int net_startup(void)
{
    static int done;
    WSADATA wd;
    if (!done && WSAStartup(MAKEWORD(2, 2), &wd) == 0) done = 1;
    return done;
}
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
typedef int sock_t;
#define BAD_SOCK (-1)
#define closesock close
static void set_nonblock(sock_t s) { fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK); }
static int net_startup(void) { return 1; }
#endif

static sock_t sock = BAD_SOCK;
static uint64_t rx_us;                  /* net_rx_us */
static int rx_exact;                    /* net_rx_exact */

#ifdef _WIN32
/* Arrival times on Windows: a thread waits for the socket's FD_READ event and notes when data came;
   it reads nothing (the game thread receives). Winsock posts the event when data arrives and again
   after a recv that leaves data queued, so the thread checks that data is queued, and a note taken
   before the game thread's last empty read (drain_end) is not used. (Winsock's own receive
   timestamps, SIO_TIMESTAMPING from Windows 10 2004, depend on the network driver.) */
static WSAEVENT ts_ev = WSA_INVALID_EVENT;
static HANDLE ts_thread;
static volatile LONG ts_stop;
static uint64_t ts_stamp, drain_end;    /* the thread's note (atomic, 0: none); the game thread's last empty read */
static int drained = 1;                 /* the next datagram is the first after an empty read */
static DWORD WINAPI ts_main(LPVOID unused)
{
    (void)unused;
    while (WaitForSingleObject(ts_ev, INFINITE) == WAIT_OBJECT_0 && !ts_stop) {
        uint64_t t = net_us(), none = 0;   /* before the check: never later than the data's arrival + the check */
        u_long n = 0;
        WSAResetEvent(ts_ev);
        if (ioctlsocket(sock, FIONREAD, &n) == 0 && n > 0)
            __atomic_compare_exchange_n(&ts_stamp, &none, t, 0, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
    }
    return 0;
}
static void ts_start(void)
{
    ts_stop = 0;
    __atomic_store_n(&ts_stamp, 0, __ATOMIC_RELAXED);
    drain_end = 0;
    drained = 1;
    if ((ts_ev = WSACreateEvent()) == WSA_INVALID_EVENT) return;
    if (WSAEventSelect(sock, ts_ev, FD_READ) != 0 || !(ts_thread = CreateThread(NULL, 0, ts_main, NULL, 0, NULL))) {
        WSACloseEvent(ts_ev);
        ts_ev = WSA_INVALID_EVENT;
    }
}
static void ts_end(void)
{
    if (ts_thread) {
        ts_stop = 1;
        WSASetEvent(ts_ev);
        WaitForSingleObject(ts_thread, INFINITE);
        CloseHandle(ts_thread);
        ts_thread = NULL;
    }
    if (ts_ev != WSA_INVALID_EVENT) WSACloseEvent(ts_ev);
    ts_ev = WSA_INVALID_EVENT;
}
#endif

int net_open(int port)
{
    struct sockaddr_in a;
    net_close();
    if (!net_startup()) return 0;
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == BAD_SOCK) return 0;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons((uint16_t)port);
    if (bind(sock, (struct sockaddr*)&a, sizeof(a)) != 0) {
#ifdef _WIN32
        int e = WSAGetLastError(), r = e == WSAEADDRINUSE ? NET_IN_USE : e == WSAEACCES ? NET_DENIED : 0;
#else
        int r = errno == EADDRINUSE ? NET_IN_USE : errno == EACCES ? NET_DENIED : 0;
#endif
        net_close();
        return r;
    }
    set_nonblock(sock);
#ifdef _WIN32
    {   /* an ICMP "port unreachable" (a partner that left) would fail the next recvfrom with WSAECONNRESET,
           every frame while we still send there, and net_recv's caller stops reading at the first failure */
        BOOL off = FALSE;
        DWORD n;
        WSAIoctl(sock, _WSAIOW(IOC_VENDOR, 12) /* SIO_UDP_CONNRESET */, &off, sizeof(off), NULL, 0, &n, NULL, NULL);
    }
    ts_start();
#else
    {   /* the kernel's receive time of every datagram (net_rx_us) */
        int on = 1;
        setsockopt(sock, SOL_SOCKET, SO_TIMESTAMPNS, &on, sizeof(on));
    }
#endif
    return 1;
}

void net_close(void)
{
    net_flush();
#ifdef _WIN32
    ts_end();
#endif
    if (sock != BAD_SOCK) closesock(sock);
    sock = BAD_SOCK;
}

int net_resolve(const char* host, int port, NetAddr* out)
{
    struct addrinfo hints, *res = NULL;
    if (!net_startup()) return 0;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return 0;
    out->ip = ((struct sockaddr_in*)res->ai_addr)->sin_addr.s_addr;
    out->port = htons((uint16_t)port);
    freeaddrinfo(res);
    return 1;
}

struct NetResolve {
    char host[128];
    int port, state, refs;      /* state 0 busy, 1 ok, -1 failed (atomic); refs: the thread and the caller */
    NetAddr addr;
};

static void resolve_release(NetResolve* r) { if (__atomic_sub_fetch(&r->refs, 1, __ATOMIC_ACQ_REL) == 0) free(r); }

#ifdef _WIN32
static DWORD WINAPI resolve_thread(LPVOID p)
#else
static void* resolve_thread(void* p)
#endif
{
    NetResolve* r = p;
    NetAddr a = { 0, 0 };
    int ok = net_resolve(r->host, r->port, &a);
    r->addr = a;
    __atomic_store_n(&r->state, ok ? 1 : -1, __ATOMIC_RELEASE);
    resolve_release(r);
    return 0;
}

NetResolve* net_resolve_start(const char* host, int port)
{
    NetResolve* r = calloc(1, sizeof(*r));
    if (!r || !net_startup()) { free(r); return NULL; }
    snprintf(r->host, sizeof(r->host), "%s", host);
    r->port = port;
    r->refs = 2;
#ifdef _WIN32
    {
        HANDLE t = CreateThread(NULL, 0, resolve_thread, r, 0, NULL);
        if (!t) { free(r); return NULL; }
        CloseHandle(t);
    }
#else
    {
        pthread_t t;
        pthread_attr_t at;
        pthread_attr_init(&at);
        pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&t, &at, resolve_thread, r)) { pthread_attr_destroy(&at); free(r); return NULL; }
        pthread_attr_destroy(&at);
    }
#endif
    return r;
}

int net_resolve_poll(NetResolve* r, NetAddr* out)
{
    int s = __atomic_load_n(&r->state, __ATOMIC_ACQUIRE);
    if (!s) return 0;
    if (s > 0) *out = r->addr;
    resolve_release(r);
    return s;
}

void net_resolve_cancel(NetResolve* r) { if (r) resolve_release(r); }

/* ready to write (wr) or read within ms */
static int sock_wait(sock_t s, int wr, int ms)
{
    fd_set f, e;
    struct timeval tv = { ms / 1000, ms % 1000 * 1000 };
    FD_ZERO(&f);
    FD_ZERO(&e);
    FD_SET(s, &f);
    FD_SET(s, &e);   /* (Windows reports a failed connect here) */
    return select((int)s + 1, wr ? NULL : &f, wr ? &f : NULL, &e, &tv) > 0;
}

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0   /* (Windows has no SIGPIPE) */
#endif

int net_tcp_fetch(const NetAddr* to, const char* req, int len, char* buf, int cap, int connect_ms, int total_ms, const char** why)
{
    struct sockaddr_in a;
    uint32_t t0 = net_ms();
    int n = 0, r, err = 0;
    socklen_t el = sizeof(err);
    sock_t s;
    *why = NULL;
    if (!net_startup() || (s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == BAD_SOCK) { *why = "no socket"; return -1; }
    set_nonblock(s);
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = to->ip;
    a.sin_port = to->port;
    if (connect(s, (struct sockaddr*)&a, sizeof(a)) != 0) {
#ifdef _WIN32
        int pending = WSAGetLastError() == WSAEWOULDBLOCK;
#else
        int pending = errno == EINPROGRESS;
#endif
        if (!pending) *why = "cannot connect";
        else if (!sock_wait(s, 1, connect_ms)) *why = "connect timed out";
        else if (getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&err, &el) != 0 || err) *why = "connection refused or unreachable";
    }
    if (!*why && send(s, req, len, MSG_NOSIGNAL) != len) *why = "cannot send";
    while (!*why && n < cap) {
        int left = total_ms - (int)(net_ms() - t0);
        if (left <= 0 || !sock_wait(s, 0, left)) { if (!n) *why = "no answer in time"; break; }
        if ((r = recv(s, buf + n, cap - n, 0)) <= 0) { if (r < 0 && !n) *why = "connection reset"; break; }
        n += r;
    }
    closesock(s);
    return *why ? -1 : n;
}

static int raw_send(const NetAddr* to, const void* buf, int len)
{
    struct sockaddr_in a;
    if (sock == BAD_SOCK) return 0;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = to->ip;
    a.sin_port = to->port;
    return sendto(sock, (const char*)buf, len, 0, (struct sockaddr*)&a, sizeof(a)) == len;
}

#ifndef RECVX_NET_NO_SDL
/* Test aid RECVX_COOP_NETSIM="delay_ms,jitter_ms,loss_pct" (docs/COOP.md "Test aids"): a network path in our send
   path, so two instances on one PC (Linux or Windows) see delay, jitter and loss without a proxy. Every datagram we
   send waits delay + uniform(-jitter, +jitter) ms but never leaves before the one sent before it (a path keeps a
   burst's order, as the measurements' udp_proxy.py), loss_pct % are dropped; a thread sends them when due. Each
   player's own direction: set it on both. Through a server: our leg to it. */
#define SIM_N 1024
static struct {
    SDL_Mutex* mu;
    int on, head, n;                    /* on: -1 not read yet */
    double delay, jit, loss;
    uint64_t last;                      /* the previous datagram's departure (us) */
    uint32_t rng;
    struct { uint64_t due; NetAddr to; int len; unsigned char b[1500]; }* q;
} sim = { .on = -1, .rng = 0x9E3779B9u };
static double sim_rand(void)   /* [0, 1) */
{
    sim.rng ^= sim.rng << 13; sim.rng ^= sim.rng >> 17; sim.rng ^= sim.rng << 5;
    return (sim.rng >> 8) / 16777216.0;
}
static int SDLCALL sim_main(void* unused)
{
    (void)unused;
    for (;;) {
        unsigned char b[1500];
        NetAddr to;
        int len = 0;
        SDL_LockMutex(sim.mu);
        if (sim.n && sim.q[sim.head].due <= net_us()) {
            to = sim.q[sim.head].to; len = sim.q[sim.head].len;
            memcpy(b, sim.q[sim.head].b, len);
            sim.head = (sim.head + 1) % SIM_N; sim.n--;
        }
        SDL_UnlockMutex(sim.mu);
        if (len) raw_send(&to, b, len);
        else SDL_DelayNS(250000);
    }
    return 0;
}
static int sim_send(const NetAddr* to, const void* buf, int len)
{
    uint64_t t;
    double d;
    if (sim.on < 0) {
        const char* e = getenv("RECVX_COOP_NETSIM");
        sim.on = e && sscanf(e, "%lf,%lf,%lf", &sim.delay, &sim.jit, &sim.loss) >= 1 && (sim.q = calloc(SIM_N, sizeof(*sim.q))) &&
                 (sim.mu = SDL_CreateMutex()) != NULL;
        if (sim.on) {
            SDL_Thread* th = SDL_CreateThread(sim_main, "netsim", NULL);
            if (th) SDL_DetachThread(th); else sim.on = 0;
            fprintf(stderr, "net: test aid RECVX_COOP_NETSIM: %.1f ms +-%.1f, %.1f %% loss on what we send\n", sim.delay, sim.jit, sim.loss);
        }
    }
    if (!sim.on) return raw_send(to, buf, len);
    if (sim_rand() * 100.0 < sim.loss || len > (int)sizeof(sim.q->b)) return 1;   /* lost on the way */
    d = (sim.delay + (sim_rand() * 2.0 - 1.0) * sim.jit) * 1000.0;
    t = net_us() + (uint64_t)(d > 0 ? d : 0);
    SDL_LockMutex(sim.mu);
    if (t < sim.last) t = sim.last;
    sim.last = t;
    if (sim.n < SIM_N) {
        int k = (sim.head + sim.n++) % SIM_N;
        sim.q[k].due = t; sim.q[k].to = *to; sim.q[k].len = len;
        memcpy(sim.q[k].b, buf, len);
    }
    SDL_UnlockMutex(sim.mu);
    return 1;
}
int net_send(const NetAddr* to, const void* buf, int len) { return sim_send(to, buf, len); }
void net_flush(void)   /* what waits goes now (a goodbye at exit, the socket closing) */
{
    if (sim.on <= 0) return;
    SDL_LockMutex(sim.mu);
    for (; sim.n; sim.n--, sim.head = (sim.head + 1) % SIM_N) raw_send(&sim.q[sim.head].to, sim.q[sim.head].b, sim.q[sim.head].len);
    SDL_UnlockMutex(sim.mu);
}
#else
int net_send(const NetAddr* to, const void* buf, int len) { return raw_send(to, buf, len); }
void net_flush(void) {}
#endif

int net_recv(NetAddr* from, void* buf, int cap)
{
    struct sockaddr_in a;
    int n;
    if (sock == BAD_SOCK) return -1;
#ifdef _WIN32
    {
        int al = sizeof(a);
        uint64_t s;
        int e, tries = 0;
        /* a datagram too big for buf (WSAEMSGSIZE: its rest is discarded) or an ICMP error for an
           earlier send (WSAECONNRESET / WSAENETRESET): skip it, the next one may be ours */
        while ((n = recvfrom(sock, (char*)buf, cap, 0, (struct sockaddr*)&a, &al)) < 0 && ++tries < 64 &&
               ((e = WSAGetLastError()) == WSAEMSGSIZE || e == WSAECONNRESET || e == WSAENETRESET))
            al = sizeof(a);
        if (n < 0) {   /* nothing queued (or a failure): what the thread notes from now on is new */
            drain_end = net_us();
            __atomic_store_n(&ts_stamp, 0, __ATOMIC_RELEASE);
            drained = 1;
            return -1;
        }
        s = drained ? __atomic_exchange_n(&ts_stamp, 0, __ATOMIC_ACQUIRE) : 0;
        rx_us = s && s >= drain_end ? s : net_us();
        rx_exact = s && s >= drain_end;
        drained = 0;
    }
#else
    {
        struct timespec ts = { 0, 0 };
        union { char b[CMSG_SPACE(sizeof(struct timespec))]; struct cmsghdr align; } ctl;
        struct iovec io = { buf, (size_t)cap };
        struct msghdr mh;
        struct cmsghdr* c;
        int tries = 0;
        memset(&mh, 0, sizeof(mh));
        mh.msg_name = &a;
        mh.msg_namelen = sizeof(a);
        mh.msg_iov = &io;
        mh.msg_iovlen = 1;
        mh.msg_control = ctl.b;
        mh.msg_controllen = sizeof(ctl.b);
        /* an ICMP error for an earlier send (ECONNREFUSED: a peer not yet listening): skip it */
        while ((n = (int)recvmsg(sock, &mh, 0)) < 0 && ++tries < 64 && (errno == ECONNREFUSED || errno == EHOSTUNREACH || errno == ENETUNREACH))
            mh.msg_namelen = sizeof(a), mh.msg_controllen = sizeof(ctl.b);
        if (n < 0) return -1;   /* EWOULDBLOCK */
        rx_us = net_us();
        rx_exact = 0;
        for (c = CMSG_FIRSTHDR(&mh); c; c = CMSG_NXTHDR(&mh, c))
            if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_TIMESTAMPNS) memcpy(&ts, CMSG_DATA(c), sizeof(ts));
        if (ts.tv_sec) {   /* a wall-clock time: how long ago, taken off our monotonic clock */
            struct timespec now;
            int64_t ago;
            clock_gettime(CLOCK_REALTIME, &now);
            ago = ((int64_t)now.tv_sec - ts.tv_sec) * 1000000 + (now.tv_nsec - ts.tv_nsec) / 1000;
            if ((rx_exact = ago >= 0 && ago < 60000000)) rx_us -= (uint64_t)ago;   /* (a slow frame waits seconds; else the clock was set: when it was read) */
        }
    }
#endif
    from->ip = a.sin_addr.s_addr;
    from->port = a.sin_port;
    return n;
}

uint64_t net_rx_us(void) { return rx_us; }
int net_rx_exact(void) { return rx_exact; }

const char* net_addr_str(const NetAddr* a)
{
    static char s[32];
    const unsigned char* b = (const unsigned char*)&a->ip;
    snprintf(s, sizeof(s), "%u.%u.%u.%u:%u", b[0], b[1], b[2], b[3], ntohs(a->port));
    return s;
}

/* the length of an IPv4 address (a.b.c.d, 1-3 digits each) at s, 0 if none */
static int ipv4_at(const char* s)
{
    int n = 0, part, d;
    for (part = 0; part < 4; part++) {
        if (part && s[n++] != '.') return 0;
        for (d = 0; d < 3 && s[n] >= '0' && s[n] <= '9'; d++) n++;
        if (!d) return 0;
    }
    return s[n] >= '0' && s[n] <= '9' ? 0 : n;
}

int net_numeric_host(const char* s)
{
    int k = (int)strcspn(s, ":# ");
    return k > 0 && (int)strspn(s, "0123456789.") >= k;
}

/* part of a host name or address: a kept one stands alone (1.2.3.4 is not the start of 1.2.3.45) */
static int host_ch(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '-'; }

const char* net_mask_addrs(const char* text, const char* const* hosts, const char* const* keep, char* out, int cap)
{
    static const char mask[] = "xxx.xxx.xxx.xxx";   /* the game font draws * as a spade */
    const char *name[4], *kname[4];
    int len[4], klen[4], nn = 0, nk = 0, o = 0, i, k, kept;
    for (; hosts && *hosts && nn < 4; hosts++) {   /* the host part, if it is a name or address with a dot */
        k = (int)strcspn(*hosts, ":# ");
        if (k > 2 && memchr(*hosts, '.', k)) { name[nn] = *hosts; len[nn++] = k; }
    }
    for (; keep && *keep && nk < 4; keep++)   /* the host part, a name (a bare IPv4 address may be a home's: masked as any) */
        if ((k = (int)strcspn(*keep, ":# ")) > 0 && !net_numeric_host(*keep)) { kname[nk] = *keep; klen[nk++] = k; }
    while (*text && o < cap - 1) {
        k = kept = 0;
        if (o == 0 || !host_ch(out[o - 1]))
            for (i = 0; i < nk && !k; i++)
                if (!strncmp(text, kname[i], klen[i]) && !host_ch(text[klen[i]])) k = kept = klen[i];
        for (i = 0; i < nn && !k; i++)
            if (!strncmp(text, name[i], len[i])) k = len[i];
        if (!k && (o == 0 || !(out[o - 1] == '.' || (out[o - 1] >= '0' && out[o - 1] <= '9')))) k = ipv4_at(text);
        if (kept) while (k-- > 0 && o < cap - 1) out[o++] = *text++;
        else if (k) { o += snprintf(out + o, cap - o, "%s", mask); if (o > cap - 1) o = cap - 1; text += k; }
        else out[o++] = *text++;
    }
    out[o] = 0;
    return out;
}

#ifndef RECVX_NET_NO_SDL
uint32_t net_ms(void) { return (uint32_t)SDL_GetTicks(); }
uint64_t net_us(void) { return SDL_GetTicksNS() / 1000; }
#else
uint32_t net_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}
uint64_t net_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000 + (uint64_t)t.tv_nsec / 1000;
}
#endif

int net_local_port(void)
{
    struct sockaddr_in a;
    socklen_t al = sizeof(a);
    if (sock == BAD_SOCK || getsockname(sock, (struct sockaddr*)&a, &al) != 0) return 0;
    return ntohs(a.sin_port);
}

int net_local_ip(char* out, int cap)
{
    /* a connected UDP socket picks the outgoing interface; connect() sends nothing */
    struct sockaddr_in a;
    socklen_t al = sizeof(a);
    sock_t s;
    int ok;
    out[0] = 0;
    if (!net_startup() || (s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == BAD_SOCK) return 0;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(0x08080808);   /* any public address */
    a.sin_port = htons(53);
    ok = connect(s, (struct sockaddr*)&a, sizeof(a)) == 0 && getsockname(s, (struct sockaddr*)&a, &al) == 0 &&
         a.sin_addr.s_addr != 0;
    if (ok) {
        const unsigned char* b = (const unsigned char*)&a.sin_addr.s_addr;
        snprintf(out, cap, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    }
    closesock(s);
    return ok;
}
