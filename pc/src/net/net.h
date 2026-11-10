/* Non-blocking UDP for online co-op (docs/COOP.md). One socket, IPv4, polled once per game
   frame on the game thread; no network thread (on Windows one notes arrival times, net_rx_us). */
#ifndef RECVX_NET_H
#define RECVX_NET_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct NetAddr { uint32_t ip; uint16_t port; } NetAddr;   /* network byte order */

enum { NET_IN_USE = -1, NET_DENIED = -2 };
int  net_open(int port);                                /* bind (0 = any port); 1 on success, else 0 or NET_* */
void net_close(void);
int  net_resolve(const char* host, int port, NetAddr* out);  /* 1 on success (blocks on DNS) */
/* the same on a background thread, so DNS never stalls the game: start (NULL: no thread could be
   started), then poll: 0 still busy, 1 resolved into *out, -1 failed; after a non-zero answer the
   job is gone. cancel: no longer wanted (the thread finishes on its own and frees it). */
typedef struct NetResolve NetResolve;
NetResolve* net_resolve_start(const char* host, int port);
int  net_resolve_poll(NetResolve* r, NetAddr* out);
void net_resolve_cancel(NetResolve* r);
/* TCP, blocking (a background thread's: the update check, update.c): connect within connect_ms,
   send req, read until the peer closes, cap bytes are in or total_ms have passed. The bytes read,
   or -1 with *why saying what failed. */
int  net_tcp_fetch(const NetAddr* to, const char* req, int len, char* buf, int cap, int connect_ms, int total_ms, const char** why);
int  net_send(const NetAddr* to, const void* buf, int len);
void net_flush(void);                                   /* RECVX_COOP_NETSIM: what it holds back goes now */
int  net_recv(NetAddr* from, void* buf, int cap);       /* bytes, or -1 when nothing is queued */
/* when the datagram net_recv returned last arrived (net_us clock): the kernel's receive time on
   Linux (SO_TIMESTAMPNS); on Windows a thread's note of the arrival for the first datagram after an
   empty read (net.c); otherwise when it was read */
uint64_t net_rx_us(void);
int net_rx_exact(void);                                 /* 1: that time is the arrival (kernel or thread note), 0: when it was read */
const char* net_addr_str(const NetAddr* a);             /* "a.b.c.d:port", static buffer */
/* text with every IPv4 address in it, and the host part of each of hosts[] (NULL-ended,
   "host[:port][#code]"; only one with a dot, so a room code stays), as "xxx.xxx.xxx.xxx", ports
   and codes kept; into out (cap bytes), returned. Hide IP addresses: coop.h coop_shown. */
const char* net_mask_addrs(const char* text, const char* const* hosts, char* out, int cap);
uint32_t net_ms(void);                                  /* monotonic milliseconds */
uint64_t net_us(void);                                  /* monotonic microseconds */
int  net_local_ip(char* out, int cap);                  /* this PC's LAN address (route to the internet), no traffic */
int  net_local_port(void);                              /* the open socket's port (host order), 0 when none */

/* Host port mapping on the router (portmap.c): UPnP IGD, NAT-PMP as fallback, on a background
   thread. start/stop never block; the mapping is removed on stop and at exit. */
enum { PM_OFF, PM_BUSY, PM_OK, PM_FAILED };
typedef struct PortmapStatus {
    int state;                  /* PM_* */
    int public_port;            /* the router's external port (NAT-PMP may pick another one) */
    char local_ip[48], public_ip[48];   /* public_ip empty unless a routable address is known */
    char msg[256];
} PortmapStatus;
void portmap_start(int port, int automatic);    /* automatic = 0: only find the local address */
void portmap_stop(void);
void portmap_status(PortmapStatus* out);

#ifdef __cplusplus
}
#endif
#endif
