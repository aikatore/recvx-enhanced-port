/* Co-op host port mapping (net.h, docs/COOP.md "Playing over the internet"): asks the router to
   forward the UDP port to this PC, with UPnP IGD (miniupnpc) or else NAT-PMP (libnatpmp). One
   background thread per hosting session: discovery takes seconds and must not stall the game.
   The mapping has a lease and is renewed while hosting, so a crash leaves it for at most that. */
#include "net.h"
#include <SDL3/SDL.h>
#include <miniupnpc.h>
#include <upnpcommands.h>
#include <upnperrors.h>
#include <natpmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void host_log(const char* fmt, ...);

#define LEASE_S 7200
#define DESC "RE CVX co-op"

typedef struct Job { int port, automatic; SDL_AtomicInt stop; } Job;

static SDL_SpinLock lock;
static Job* cur;                /* the job that owns `st` (NULL once stopped or finished) */
static SDL_Mutex* serial;       /* held by a worker for its whole run: the previous session's worker removes its
                                   mapping before the next one adds one (on the same port it would delete the new one) */
static PortmapStatus st;
static SDL_AtomicInt running;   /* threads still alive (exit waits for their clean-up) */

/* publish, unless the job has been stopped meanwhile */
static void set(Job* j, int state, const char* pub, int pub_port, const char* fmt, ...)
{
    va_list ap;
    char msg[sizeof(st.msg)];
    int mine;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    SDL_LockSpinlock(&lock);
    if ((mine = j == cur)) {
        st.state = state;
        if (pub) SDL_strlcpy(st.public_ip, pub, sizeof(st.public_ip));
        if (pub_port) st.public_port = pub_port;
        SDL_strlcpy(st.msg, msg, sizeof(st.msg));
    }
    SDL_UnlockSpinlock(&lock);
    if (mine) host_log("portmap: %s\n", msg);
}

/* private, CGNAT (100.64/10) or unset: the internet cannot reach it */
static int unroutable(const char* ip)
{
    unsigned a, b;
    if (sscanf(ip, "%u.%u", &a, &b) != 2) return 1;
    return a == 0 || a == 10 || a == 127 || (a == 172 && b >= 16 && b < 32) || (a == 192 && b == 168) ||
           (a == 100 && b >= 64 && b < 128) || (a == 169 && b == 254);
}

/* sleep until the job is stopped or `ms` have passed */
static int wait_stop(Job* j, int ms)
{
    for (; ms > 0 && !SDL_GetAtomicInt(&j->stop); ms -= 100) SDL_Delay(100);
    return SDL_GetAtomicInt(&j->stop);
}

static const char* cgnat_hint(const char* wan)
{
    static char s[256];
    snprintf(s, sizeof(s), "Your router's address %s is not public. Direct internet joins may fail; try Via Server.", wan);
    return s;
}

/* 1 mapped (or refused: said why), 0 no UPnP router, 2 the router allows only permanent mappings */
static int try_upnp(Job* j, const char* lan_fallback)
{
    struct UPNPUrls urls = { 0 };   /* FreeUPNPUrls also runs when UPNP_GetValidIGD failed early (-1) */
    struct IGDdatas data;
    char lan[64] = "", wan[64] = "", port[8], lease[16];
    int err = 0, igd, r;
    struct UPNPDev* devs = upnpDiscover(2000, NULL, NULL, 0, 0, 2, &err);
    igd = devs ? UPNP_GetValidIGD(devs, &urls, &data, lan, sizeof(lan), wan, sizeof(wan)) : UPNP_NO_IGD;
    freeUPNPDevlist(devs);
    if (igd != UPNP_CONNECTED_IGD && igd != UPNP_PRIVATEIP_IGD) {
        if (igd != UPNP_NO_IGD) FreeUPNPUrls(&urls);
        return 0;
    }
    if (!lan[0]) SDL_strlcpy(lan, lan_fallback, sizeof(lan));
    SDL_LockSpinlock(&lock);
    if (j == cur) SDL_strlcpy(st.local_ip, lan, sizeof(st.local_ip));
    SDL_UnlockSpinlock(&lock);
    snprintf(port, sizeof(port), "%d", j->port);
    snprintf(lease, sizeof(lease), "%d", LEASE_S);
    for (;;) {
        r = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype, port, port, lan, DESC, "UDP", NULL, lease);
        /* permanent leases only (OnlyPermanentLeasesSupported): not taken - a crash or power cut would
           leave the port forwarded to this PC for good; NAT-PMP next, else by hand */
        if (r == 725) { FreeUPNPUrls(&urls); return 2; }
        if (r != UPNPCOMMAND_SUCCESS) {
            set(j, PM_FAILED, NULL, 0, "Could not open port %s (error %d: %s). Forward it to %s in your router settings, or try Via Server.",
                port, r, strupnperror(r) ? strupnperror(r) : "?", lan);
            break;
        }
        if (UPNP_GetExternalIPAddress(urls.controlURL, data.first.servicetype, wan) != UPNPCOMMAND_SUCCESS || !wan[0])
            SDL_strlcpy(wan, "?", sizeof(wan));
        if (unroutable(wan)) set(j, PM_FAILED, NULL, 0, "Port %s forwarded to %s, but %s", port, lan, cgnat_hint(wan));
        else set(j, PM_OK, wan, j->port, "Port %s forwarded to %s while hosting.", port, lan);
        if (wait_stop(j, LEASE_S * 1000 / 2)) {
            r = UPNP_DeletePortMapping(urls.controlURL, data.first.servicetype, port, "UDP", NULL);
            host_log("portmap: UPnP mapping of port %s removed (%d)\n", port, r);
            break;
        }
    }
    FreeUPNPUrls(&urls);
    return 1;
}

/* one NAT-PMP request/response, 3 s at most (the library's own retries go on for minutes) */
static int natpmp_ask(Job* j, natpmp_t* n, natpmpresp_t* resp, int map, int lifetime)
{
    int r, ms;
    r = map ? sendnewportmappingrequest(n, NATPMP_PROTOCOL_UDP, (uint16_t)j->port, (uint16_t)j->port, lifetime)
            : sendpublicaddressrequest(n);
    if (r < 0) return r;
    for (ms = 0; ms < 3000; ms += 50) {
        if ((r = readnatpmpresponseorretry(n, resp)) != NATPMP_TRYAGAIN) return r;
        SDL_Delay(50);
    }
    return NATPMP_TRYAGAIN;
}

static int try_natpmp(Job* j)
{
    natpmp_t n;
    natpmpresp_t resp;
    char wan[48];
    const unsigned char* b;
    int pub;
    if (initnatpmp(&n, 0, 0) < 0) return 0;
    if (natpmp_ask(j, &n, &resp, 0, 0) < 0) { closenatpmp(&n); return 0; }
    b = (const unsigned char*)&resp.pnu.publicaddress.addr;
    snprintf(wan, sizeof(wan), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    for (;;) {
        if (natpmp_ask(j, &n, &resp, 1, LEASE_S) < 0) {
            set(j, PM_FAILED, NULL, 0, "Could not open port %d. Set router forwarding manually, or try Via Server.", j->port);
            break;
        }
        pub = resp.pnu.newportmapping.mappedpublicport;
        if (unroutable(wan)) set(j, PM_FAILED, NULL, 0, "Port %d forwarded, but %s", j->port, cgnat_hint(wan));
        else set(j, PM_OK, wan, pub, "Host port %d opened as internet port %d while hosting.", j->port, pub);
        if (wait_stop(j, LEASE_S * 1000 / 2)) {
            natpmp_ask(j, &n, &resp, 1, 0);   /* lifetime 0 = delete */
            host_log("portmap: NAT-PMP mapping of port %d removed\n", j->port);
            break;
        }
    }
    closenatpmp(&n);
    return 1;
}

/* only here is a job freed, after it has left `cur` (portmap_stop writes cur->stop under the lock) */
static void job_done(Job* j)
{
    SDL_LockSpinlock(&lock);
    if (cur == j) cur = NULL;   /* its status stays until the next stop/start */
    SDL_UnlockSpinlock(&lock);
    free(j);
    SDL_AddAtomicInt(&running, -1);
}

static int SDLCALL thread(void* ud)
{
    Job* j = (Job*)ud;
    char lan[48];
    int up;
    SDL_LockMutex(serial);
    if (SDL_GetAtomicInt(&j->stop)) { SDL_UnlockMutex(serial); job_done(j); return 0; }   /* replaced while waiting */
    net_local_ip(lan, sizeof(lan));
    SDL_LockSpinlock(&lock);
    if (j == cur) SDL_strlcpy(st.local_ip, lan, sizeof(st.local_ip));
    SDL_UnlockSpinlock(&lock);
    if (!j->automatic)
        set(j, PM_OFF, NULL, 0, "Automatic forwarding is off. Forward port %d to %s in your router settings, or try Via Server.",
            j->port, lan[0] ? lan : "this PC");
    else if ((up = try_upnp(j, lan)) != 1 && !SDL_GetAtomicInt(&j->stop) && !try_natpmp(j))
        set(j, PM_FAILED, NULL, 0, up == 2 ? "Router only supports permanent forwarding. Forward port %d to %s manually, or try Via Server." : "Automatic forwarding unavailable. Forward port %d to %s manually, or try Via Server.", j->port, lan[0] ? lan : "this PC");
    SDL_UnlockMutex(serial);
    job_done(j);
    return 0;
}

void portmap_stop(void)
{
    SDL_LockSpinlock(&lock);
    if (cur) SDL_SetAtomicInt(&cur->stop, 1);   /* the thread removes the mapping and frees the job */
    cur = NULL;
    SDL_zero(st);
    SDL_UnlockSpinlock(&lock);
}

/* at exit: give the threads a moment to remove their mappings (the lease covers the rest) */
static void portmap_exit(void)
{
    int ms;
    portmap_stop();
    for (ms = 0; ms < 3000 && SDL_GetAtomicInt(&running) > 0; ms += 20) SDL_Delay(20);
}

void portmap_start(int port, int automatic)
{
    static int registered;
    SDL_Thread* t;
    Job* j = (Job*)calloc(1, sizeof(*j));
    portmap_stop();
    if (!j) return;
    j->port = port;
    j->automatic = automatic;
    if (!registered) { atexit(portmap_exit); serial = SDL_CreateMutex(); registered = 1; }
    SDL_LockSpinlock(&lock);
    cur = j;
    st.state = automatic ? PM_BUSY : PM_OFF;
    SDL_strlcpy(st.msg, automatic ? "Checking router for port forwarding..." : "", sizeof(st.msg));
    SDL_UnlockSpinlock(&lock);
    SDL_AddAtomicInt(&running, 1);
    if (!(t = SDL_CreateThread(thread, "portmap", j))) {
        set(j, PM_FAILED, NULL, 0, "Could not set up port forwarding: %s. Try Via Server.", SDL_GetError());
        job_done(j);
        return;
    }
    SDL_DetachThread(t);
}

void portmap_status(PortmapStatus* out)
{
    SDL_LockSpinlock(&lock);
    *out = st;
    SDL_UnlockSpinlock(&lock);
}
