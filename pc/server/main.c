/* recvx-server: the co-op rendezvous and relay server (docs/SERVER.md). UDP, IPv4, no disk
   state, runs as any user. Settings: defaults < config file < RECVX_SERVER_PORT < command line;
   SIGHUP reads the file again (limits, log level, stats interval; port, listen, max_rooms and
   control need a restart). With a control socket, `--take-over` replaces the running process
   without dropping a session and `--drain` stops it once its rooms are gone (docs/SERVER.md
   "Updating without dropping sessions"). Linux. */
#define _GNU_SOURCE   /* pipe2, accept4, struct ucred */
#include "server.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#ifndef RECVX_RELEASE
#define RECVX_RELEASE RECVX_VERSION
#endif
#define CTL_TIMEOUT_MS 10000

static int socks[SRV_LISTEN_MAX], nsocks;
static int ctl = -1, conn = -1;     /* the control socket (listening); the one connection being read */
static char conn_buf[64];
static int conn_len;
static uint64_t conn_ms;
static pid_t conn_pid;
static struct stat ctl_file;        /* the socket file this process made or took: removed at exit if it still is that one */
static volatile sig_atomic_t quit, reload, drain;
static double cpu_base[2];          /* the CPU time of the processes this one took over from: the counter goes on */

/* the control protocol (one connection, lines): "drain" -> "draining, N rooms open";
   "takeover" -> CtlHdr with the UDP sockets and the control socket (SCM_RIGHTS), the state blob
   (CtlHdr.len bytes), then the new process answers "ok", "incompatible" or "invalid", and to
   "ok" the old one says "bye" and exits: only then does the new one serve */
typedef struct CtlHdr { char magic[4]; uint32_t nudp, len; char version[52]; } CtlHdr;   /* "RXHO" */

static uint64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void send_to(const RdvAddr* to, const uint8_t* buf, int n)
{
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    memcpy(&a.sin_addr, to->ip, 4);
    a.sin_port = htons(to->port);
    /* non-blocking: a full send buffer drops this datagram (counted) instead of stalling every room */
    if (sendto(socks[to->via < nsocks ? to->via : 0], buf, n, MSG_DONTWAIT, (struct sockaddr*)&a, sizeof(a)) != n) srv_proc.send_failed++;
}

/* the core's log buffer to stdout without blocking: a socket (the journal) with MSG_DONTWAIT,
   anything else only when poll says it is writable, at most PIPE_BUF at a time; what does not go
   now waits in the buffer (bounded: the core drops and counts lines when it is full) */
static void flush_log(void)
{
    int n;
    const char* p = srv_log_pending(&n);
    while (n > 0) {
        ssize_t w = send(1, p, n, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (w < 0 && errno == ENOTSOCK) {
            struct pollfd q = { 1, POLLOUT, 0 };
            if (poll(&q, 1, 0) != 1 || !(q.revents & POLLOUT)) return;
            w = write(1, p, n < 4096 ? n : 4096);
        }
        if (w <= 0) return;
        srv_log_done((int)w);
        p = srv_log_pending(&n);
    }
}

static void usage(FILE* f)
{
    fprintf(f, "recvx-server " RECVX_VERSION ": co-op rendezvous and relay server (docs/SERVER.md)\n"
               "usage: recvx-server [--config PATH] [-v] [--KEY VALUE ...]\n"
               "       recvx-server --take-over [--config PATH] [--KEY VALUE ...]\n"
               "       recvx-server --drain [--config PATH]\n"
               "  --config PATH              the config file (default %s if it exists)\n"
               "  -v, --verbose              log requests, joins and relay changes too\n"
               "  --take-over                replace the server running on the control socket: its sockets, rooms\n"
               "                             and counters move to this process, no session drops. Returns once done:\n"
               "                             0 = this version runs now, 3 = incompatible (the old one goes on),\n"
               "                             2 = configuration errors, 1 = any other failure (the old one goes on)\n"
               "  --drain                    tell the server on the control socket to open no new rooms and to\n"
               "                             stop once the last room closed (also: SIGUSR1)\n"
               "  -h, --help                 this text\n"
               "  --version                  print the version\n"
               "Every config key is also a flag (flags override the file):\n", SRV_CONFIG_PATH);
    srv_config_help(f);
}

static void on_signal(int s)
{
    if (s == SIGHUP) reload = 1;
    else if (s == SIGUSR1) drain = 1;
    else quit = 1;
}

/* defaults, the file, the environment, the flags; the number of errors (printed) */
static int load(SrvConfig* c, int argc, char** argv, const char** path)
{
    const char* env = getenv("RECVX_SERVER_PORT");
    char msg[160];
    int i, errs = 0, warn = 0;
    srv_defaults(c);
    *path = NULL;
    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--config") && i + 1 < argc) *path = argv[++i];
        else if (!strncmp(argv[i], "--config=", 9)) *path = argv[i] + 9;
    if (*path) errs += srv_config_file(c, *path, &warn, stderr) != 0;   /* named: it must be there and valid */
    else if (access(SRV_CONFIG_PATH, F_OK) == 0) {
        *path = SRV_CONFIG_PATH;
        errs += srv_config_file(c, *path, &warn, stderr) != 0;
    }
    if (env && srv_config_set(c, "port", env, msg, sizeof(msg)) != 1) { fprintf(stderr, "RECVX_SERVER_PORT: %s\n", msg); errs++; }
    for (i = 1; i < argc; i++) {
        char key[64];
        const char* a = argv[i];
        const char* val = NULL;
        int r;
        if (!strcmp(a, "--config")) { i++; continue; }
        if (!strncmp(a, "--config=", 9) || !strcmp(a, "--take-over") || !strcmp(a, "--drain")) continue;
        if (!strcmp(a, "-v")) { c->verbose = 1; continue; }
        if (strncmp(a, "--", 2) || !a[2]) { fprintf(stderr, "unknown argument '%s' (--help)\n", a); errs++; continue; }
        snprintf(key, sizeof(key), "%.*s", (int)strcspn(a + 2, "="), a + 2);
        if (a[2 + strlen(key)] == '=') val = a + 3 + strlen(key);
        else if (!srv_config_is_bool(key)) {
            if (i + 1 >= argc) { fprintf(stderr, "%s: a value is missing\n", a); errs++; continue; }
            val = argv[++i];
        }
        r = srv_config_set(c, key, val, msg, sizeof(msg));
        if (r < 0) { fprintf(stderr, "unknown option %s (--help)\n", a); errs++; }   /* a typo on the command line: refuse */
        else if (!r) { fprintf(stderr, "%s\n", msg); errs++; }
    }
    return errs;
}

static int open_sockets(const SrvConfig* c)
{
    int i, n = c->nlisten ? c->nlisten : 1;
    for (i = 0; i < n; i++) {
        struct sockaddr_in a;
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        if (c->nlisten) memcpy(&a.sin_addr, c->listen[i], 4);
        a.sin_port = htons((uint16_t)c->port);
        socks[i] = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_UDP);
        if (socks[i] >= 0) fcntl(socks[i], F_SETFL, fcntl(socks[i], F_GETFL) | O_NONBLOCK);
        if (socks[i] < 0 || bind(socks[i], (struct sockaddr*)&a, sizeof(a)) != 0) {
            fprintf(stderr, "cannot listen on %s:%d (UDP): %s\n", inet_ntoa(a.sin_addr), c->port, strerror(errno));
            return 0;
        }
        nsocks = i + 1;
    }
    return 1;
}

static const char* listen_str(const SrvConfig* c)
{
    static char s[SRV_LISTEN_MAX * 24];
    int i, k = 0;
    if (!c->nlisten) snprintf(s, sizeof(s), "0.0.0.0:%d", c->port);
    for (i = 0; i < c->nlisten; i++)
        k += snprintf(s + k, sizeof(s) - k, "%s%u.%u.%u.%u:%d", i ? ", " : "", c->listen[i][0], c->listen[i][1], c->listen[i][2], c->listen[i][3], c->port);
    return s;
}

static void describe(const SrvConfig* c, const char* path, const char* what)
{
    char total[64], stats[32];
    snprintf(total, sizeof(total), "%d KB/s and %d datagrams/s", c->total_kbps, c->total_pps);   /* 0: no limit */
    snprintf(stats, sizeof(stats), c->stats_s ? "every %d s" : "off", c->stats_s);
    srv_logf("recvx-server " RECVX_VERSION ": %s: protocol %d, config %s, listening on %s, up to %d rooms (%d per address), relay per player %d KB/s and %d "
             "datagrams/s, per address %d KB/s and %d datagrams/s, all relayed traffic %s (0 = no limit), %d requests/s and %d failed "
             "joins/min per address, room time-out %d s, member time-out %d s, code grace %d s, log %s, metrics line %s, control socket %s",
             what, RDV_VERSION, path ? path : "none", listen_str(c), c->max_rooms, c->rooms_per_ip, c->relay_kbps, c->relay_pps, c->ip_kbps,
             c->ip_pps, total, c->req_per_sec, c->joins_per_min, c->room_timeout_s, c->member_timeout_s, c->grace_s, c->verbose ? "verbose" : "normal", stats,
             c->control[0] ? c->control : "none");
}

static void proc_info(void)
{
    struct rusage u;
    FILE* f;
    long pages = 0, rss = 0;
    srv_proc.uptime_s = (now_ms() - srv_proc.start_ms) / 1000.0;   /* the service's, across --take-over */
    if (getrusage(RUSAGE_SELF, &u) == 0) {
        srv_proc.cpu_user_s = cpu_base[0] + u.ru_utime.tv_sec + u.ru_utime.tv_usec / 1e6;
        srv_proc.cpu_sys_s = cpu_base[1] + u.ru_stime.tv_sec + u.ru_stime.tv_usec / 1e6;
        srv_proc.rss_kb = u.ru_maxrss;   /* the peak, if /proc is not there */
    }
    if ((f = fopen("/proc/self/statm", "r"))) {
        if (fscanf(f, "%ld %ld", &pages, &rss) == 2) srv_proc.rss_kb = rss * (sysconf(_SC_PAGESIZE) / 1024);
        fclose(f);
    }
}

/* ---------------------------------------------------------------- the control socket */
static int wait_fd(int fd, short ev, uint64_t deadline)   /* 1: ready (or hung up), 0: time-out */
{
    for (;;) {
        struct pollfd p = { fd, ev, 0 };
        uint64_t now = now_ms();
        int r;
        if (now >= deadline) return 0;
        r = poll(&p, 1, (int)(deadline - now));
        if (r > 0) return 1;
        if (r < 0 && errno != EINTR) return 0;
    }
}

static int write_all(int fd, const void* p, size_t n, uint64_t deadline)
{
    const char* c = p;
    while (n) {
        ssize_t w = send(fd, c, n, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (w > 0) { c += w; n -= w; continue; }
        if (w < 0 && errno != EAGAIN && errno != EINTR) return 0;
        if (!wait_fd(fd, POLLOUT, deadline)) return 0;
    }
    return 1;
}

static int read_all(int fd, void* p, size_t n, uint64_t deadline)
{
    char* c = p;
    while (n) {
        ssize_t r = recv(fd, c, n, MSG_DONTWAIT);
        if (r > 0) { c += r; n -= r; continue; }
        if (r == 0 || (errno != EAGAIN && errno != EINTR)) return 0;
        if (!wait_fd(fd, POLLIN, deadline)) return 0;
    }
    return 1;
}

static int read_line(int fd, char* out, int cap, uint64_t deadline)   /* without the newline; 0: none */
{
    int n = 0;
    while (n < cap - 1) {
        if (!read_all(fd, out + n, 1, deadline)) return 0;
        if (out[n] == '\n') { out[n] = 0; return 1; }
        n++;
    }
    return 0;
}

static int unix_addr(const char* path, struct sockaddr_un* a)
{
    memset(a, 0, sizeof(*a));
    a->sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(a->sun_path)) return 0;
    memcpy(a->sun_path, path, strlen(path));
    return 1;
}

/* systemd's notify protocol, without libsystemd: a datagram to $NOTIFY_SOCKET. barrier: then
   waits (up to 5 s) until systemd has handled it (BARRIER=1 with a pipe that it closes) */
static void notify(const char* msg, int barrier)
{
    const char* e = getenv("NOTIFY_SOCKET");
    struct sockaddr_un a;
    socklen_t al;
    int fd, p[2];
    if (!e || (e[0] != '/' && e[0] != '@') || !unix_addr(e, &a)) return;
    if (e[0] == '@') a.sun_path[0] = 0;   /* an abstract socket */
    al = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + strlen(e));
    if ((fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0)) < 0) return;
    if (sendto(fd, msg, strlen(msg), MSG_NOSIGNAL, (struct sockaddr*)&a, al) < 0) srv_logf("systemd notify (%s): %s", msg, strerror(errno));
    else if (barrier && pipe2(p, O_CLOEXEC) == 0) {
        char cb[CMSG_SPACE(sizeof(int))];
        struct iovec v = { (void*)"BARRIER=1", 9 };
        struct msghdr h;
        struct cmsghdr* cm;
        int ok;
        memset(&h, 0, sizeof(h));
        memset(cb, 0, sizeof(cb));
        h.msg_name = &a;
        h.msg_namelen = al;
        h.msg_iov = &v;
        h.msg_iovlen = 1;
        h.msg_control = cb;
        h.msg_controllen = sizeof(cb);
        cm = CMSG_FIRSTHDR(&h);
        cm->cmsg_level = SOL_SOCKET;
        cm->cmsg_type = SCM_RIGHTS;
        cm->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(cm), &p[1], sizeof(int));
        ok = sendmsg(fd, &h, MSG_NOSIGNAL) >= 0;
        close(p[1]);
        if (ok) wait_fd(p[0], POLLIN, now_ms() + 5000);   /* POLLHUP: systemd closed its end */
        close(p[0]);
    }
    close(fd);
}

/* the control socket: created with mode 0600 (umask while binding), so only this user (and root)
   can connect, and connections from another uid are refused too. A socket file left by a server
   that died (nobody accepts) is removed; a live one, or anything that is not a socket, is not. */
static int ctl_open(const char* path)
{
    struct sockaddr_un a;
    struct stat s;
    mode_t old;
    int fd, r, err;
    if (!unix_addr(path, &a)) return -1;
    if (lstat(path, &s) == 0) {
        if (!S_ISSOCK(s.st_mode)) { fprintf(stderr, "control %s: exists and is not a socket\n", path); return -1; }
        fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        r = fd >= 0 ? connect(fd, (struct sockaddr*)&a, sizeof(a)) : -1;
        err = errno;
        if (fd >= 0) close(fd);
        if (r == 0 || err != ECONNREFUSED) {
            fprintf(stderr, "control %s: %s (a recvx-server runs there? --take-over replaces it)\n", path, r == 0 ? "in use" : strerror(err));
            return -1;
        }
        if (unlink(path) != 0) { fprintf(stderr, "control %s: cannot remove the stale socket: %s\n", path, strerror(errno)); return -1; }
    } else if (errno != ENOENT) { fprintf(stderr, "control %s: %s\n", path, strerror(errno)); return -1; }
    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    old = umask(0177);
    r = fd >= 0 ? bind(fd, (struct sockaddr*)&a, sizeof(a)) : -1;
    umask(old);
    if (r != 0 || listen(fd, 4) != 0 || lstat(path, &ctl_file) != 0) {
        fprintf(stderr, "control %s: %s\n", path, strerror(errno));
        if (fd >= 0) close(fd);
        return -1;
    }
    return fd;
}

static void ctl_close(const char* path)   /* at exit: the socket file, if it is still the one this process has */
{
    struct stat s;
    if (ctl < 0) return;
    close(ctl);
    if (lstat(path, &s) == 0 && s.st_dev == ctl_file.st_dev && s.st_ino == ctl_file.st_ino) unlink(path);
}

static int ctl_connect(const char* path, pid_t* pid)   /* --take-over, --drain: to the running server */
{
    struct sockaddr_un a;
    struct ucred cr;
    socklen_t cl = sizeof(cr);
    struct timeval tv = { CTL_TIMEOUT_MS / 1000, 0 };
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0 || !unix_addr(path, &a) || connect(fd, (struct sockaddr*)&a, sizeof(a)) != 0) {
        fprintf(stderr, "control %s: %s (no recvx-server with this control socket runs)\n", path, strerror(errno));
        if (fd >= 0) close(fd);
        return -1;
    }
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cr, &cl) != 0 || cr.uid != geteuid()) {
        fprintf(stderr, "control %s: the process there runs as another user\n", path);
        close(fd);
        return -1;
    }
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (pid) *pid = cr.pid;
    return fd;
}

static void conn_close(void)
{
    if (conn >= 0) close(conn);
    conn = -1;
}

static void ctl_accept(void)
{
    struct ucred cr;
    socklen_t cl = sizeof(cr);
    int fd = accept4(ctl, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd < 0) return;
    if (conn >= 0 || getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cr, &cl) != 0 || cr.uid != geteuid()) {
        if (conn >= 0) send(fd, "busy\n", 5, MSG_DONTWAIT | MSG_NOSIGNAL);
        close(fd);
        return;
    }
    conn = fd;
    conn_len = 0;
    conn_ms = now_ms();
    conn_pid = cr.pid;
}

/* "takeover": the sockets and the state to the new process. The UDP sockets are not read from
   here on (what arrives waits in the kernel's buffer for whichever process goes on). 1: the new
   process took over (this one exits), 0: it did not (this one goes on) */
static int handover(void)
{
    CtlHdr h;
    uint8_t* blob;
    uint32_t n;
    int fds[SRV_LISTEN_MAX + 1], i, ok;
    char line[32] = "", cb[CMSG_SPACE(sizeof(fds))];
    uint64_t deadline = now_ms() + CTL_TIMEOUT_MS;
    struct iovec v = { &h, sizeof(h) };
    struct msghdr m;
    struct cmsghdr* cm;
    flush_log();
    if (!srv_state_export(&blob, &n)) {
        srv_logf("handover to pid %d: out of memory, the server goes on", (int)conn_pid);
        write_all(conn, "error\n", 6, deadline);
        return 0;
    }
    memset(&h, 0, sizeof(h));
    memcpy(h.magic, "RXHO", 4);
    h.nudp = (uint32_t)nsocks;
    h.len = n;
    snprintf(h.version, sizeof(h.version), "%s", RECVX_VERSION);
    for (i = 0; i < nsocks; i++) fds[i] = socks[i];
    fds[nsocks] = ctl;
    memset(&m, 0, sizeof(m));
    memset(cb, 0, sizeof(cb));
    m.msg_iov = &v;
    m.msg_iovlen = 1;
    m.msg_control = cb;
    m.msg_controllen = CMSG_SPACE((nsocks + 1) * sizeof(int));
    cm = CMSG_FIRSTHDR(&m);
    cm->cmsg_level = SOL_SOCKET;
    cm->cmsg_type = SCM_RIGHTS;
    cm->cmsg_len = CMSG_LEN((nsocks + 1) * sizeof(int));
    memcpy(CMSG_DATA(cm), fds, (nsocks + 1) * sizeof(int));
    ok = wait_fd(conn, POLLOUT, deadline) && sendmsg(conn, &m, MSG_NOSIGNAL | MSG_DONTWAIT) == (ssize_t)sizeof(h) && write_all(conn, blob, n, deadline) &&
         read_line(conn, line, sizeof(line), deadline);
    explicit_bzero(blob, n);   /* tokens, keys, verifiers */
    free(blob);
    if (ok && !strcmp(line, "ok") && write_all(conn, "bye\n", 4, deadline)) {
        srv_logf("recvx-server: handed over to pid %d (%u rooms, %u KB of state); this process stops", (int)conn_pid, srv_stats()->rooms, n / 1024);
        return 1;
    }
    if (!strcmp(line, "incompatible"))
        srv_logf("recvx-server: pid %d cannot read state format %d (an incompatible version): nothing handed over, the server goes on", (int)conn_pid, srv_state_format);
    else srv_logf("recvx-server: the handover to pid %d failed (%s): the server goes on", (int)conn_pid, line[0] ? "it refused the state" : "no answer");
    return 0;
}

static int ctl_input(void)   /* a command line on the connection; 1: handed over */
{
    ssize_t r = recv(conn, conn_buf + conn_len, sizeof(conn_buf) - 1 - conn_len, MSG_DONTWAIT);
    char* nl;
    int done = 0;
    if (r <= 0) {
        if (r == 0 || (errno != EAGAIN && errno != EINTR)) conn_close();
        return 0;
    }
    conn_len += (int)r;
    conn_buf[conn_len] = 0;
    if (!(nl = strchr(conn_buf, '\n'))) {
        if (conn_len >= (int)sizeof(conn_buf) - 1) conn_close();
        return 0;
    }
    *nl = 0;
    if (!strcmp(conn_buf, "takeover")) done = handover();
    else if (!strcmp(conn_buf, "drain")) {
        char t[64];
        int n = snprintf(t, sizeof(t), "draining, %u rooms open\n", srv_stats()->rooms);
        if (!srv_draining()) srv_logf("recvx-server: draining (asked on the control socket): no new rooms; stops when the last of %u rooms closed", srv_stats()->rooms);
        srv_drain(1);
        write_all(conn, t, n, now_ms() + 1000);
    } else write_all(conn, "unknown command\n", 16, now_ms() + 1000);
    conn_close();
    return done;
}

static int sock_is(int fd, int domain, int type)
{
    int d = 0, t = 0;
    socklen_t l = sizeof(d), l2 = sizeof(t);
    return getsockopt(fd, SOL_SOCKET, SO_DOMAIN, &d, &l) == 0 && getsockopt(fd, SOL_SOCKET, SO_TYPE, &t, &l2) == 0 && d == domain && t == type;
}

/* --take-over (the new process): the running server's sockets and state. 0: taken (the sockets
   in socks[] and ctl, *c's port and listen as they are), 3: a state format this build cannot read,
   1: any other failure. Unless 0, the running server goes on. */
static int take_over(SrvConfig* c)
{
    CtlHdr h;
    pid_t old = 0;
    int fd, i, r, nfd = 0, rc = 1, fds[SRV_LISTEN_MAX + 1];
    uint8_t* blob = NULL;
    uint32_t fmt = 0;
    char line[16], cb[CMSG_SPACE(sizeof(fds)) + 64];
    uint64_t deadline = now_ms() + CTL_TIMEOUT_MS;
    struct iovec v = { &h, sizeof(h) };
    struct msghdr m;
    struct cmsghdr* cm;
    struct sockaddr_in sa[SRV_LISTEN_MAX];
    if (!c->control[0]) { fprintf(stderr, "--take-over: no control socket (control = PATH in the config, or --control PATH)\n"); return 1; }
    if ((fd = ctl_connect(c->control, &old)) < 0) return 1;
    memset(&h, 0, sizeof(h));
    memset(&m, 0, sizeof(m));
    m.msg_iov = &v;
    m.msg_iovlen = 1;
    m.msg_control = cb;
    m.msg_controllen = sizeof(cb);
    if (!write_all(fd, "takeover\n", 9, deadline)) { fprintf(stderr, "--take-over: the server closed the connection\n"); goto out; }
    r = (int)recvmsg(fd, &m, MSG_WAITALL | MSG_CMSG_CLOEXEC);
    for (cm = CMSG_FIRSTHDR(&m); cm; cm = CMSG_NXTHDR(&m, cm))
        if (cm->cmsg_level == SOL_SOCKET && cm->cmsg_type == SCM_RIGHTS)
            for (i = 0; i < (int)((cm->cmsg_len - CMSG_LEN(0)) / sizeof(int)); i++) {
                int f;
                memcpy(&f, CMSG_DATA(cm) + i * sizeof(int), sizeof(int));
                if (nfd < SRV_LISTEN_MAX + 1) fds[nfd++] = f;
                else close(f);
            }
    if (r == 5 && !memcmp(&h, "busy\n", 5)) { fprintf(stderr, "--take-over: the server is busy with another control connection, try again\n"); goto out; }
    if (r != (int)sizeof(h) || (m.msg_flags & MSG_CTRUNC) || memcmp(h.magic, "RXHO", 4) || h.nudp < 1 || h.nudp > SRV_LISTEN_MAX ||
        nfd != (int)h.nudp + 1 || h.len < 8 || h.len > SRV_STATE_MAX) {
        fprintf(stderr, "--take-over: not a recvx-server handover (%d bytes, %d descriptors)\n", r, nfd);
        goto out;
    }
    h.version[sizeof(h.version) - 1] = 0;
    for (i = 0; h.version[i]; i++) if (h.version[i] < 32 || h.version[i] > 126) h.version[i] = '?';
    for (i = 0; i < (int)h.nudp; i++) {   /* bound IPv4 UDP sockets on one port, and a listening unix socket */
        socklen_t l = sizeof(sa[i]);
        if (!sock_is(fds[i], AF_INET, SOCK_DGRAM) || getsockname(fds[i], (struct sockaddr*)&sa[i], &l) != 0 || sa[i].sin_family != AF_INET ||
            !sa[i].sin_port || sa[i].sin_port != sa[0].sin_port) { fprintf(stderr, "--take-over: descriptor %d is not the server's UDP socket\n", i); goto out; }
    }
    {
        int lis = 0;
        socklen_t l = sizeof(lis);
        if (!sock_is(fds[h.nudp], AF_UNIX, SOCK_STREAM) || getsockopt(fds[h.nudp], SOL_SOCKET, SO_ACCEPTCONN, &lis, &l) != 0 || !lis) {
            fprintf(stderr, "--take-over: the last descriptor is not the control socket\n");
            goto out;
        }
    }
    if (!(blob = malloc(h.len)) || !read_all(fd, blob, h.len, deadline)) { fprintf(stderr, "--take-over: the state did not arrive (%u bytes)\n", h.len); goto out; }
    memcpy(&fmt, blob + 4, 4);
    if (!srv_init(c, send_to)) { fprintf(stderr, "--take-over: out of memory or no randomness\n"); goto out; }
    r = srv_state_import(blob, h.len, c, (int)h.nudp);
    if (r == 1) {
        fprintf(stderr, "--take-over: the running server (%s, pid %d) has state format %u, this build reads %d: nothing taken over, it goes on\n", h.version,
                (int)old, fmt, srv_state_format);
        write_all(fd, "incompatible\n", 13, deadline);
        rc = 3;
        goto out;
    }
    if (r) { fprintf(stderr, "--take-over: the running server's state is not valid: nothing taken over, it goes on\n"); write_all(fd, "invalid\n", 8, deadline); goto out; }
    {
        char t[32];
        snprintf(t, sizeof(t), "MAINPID=%d", (int)getpid());
        notify(t, 1);   /* systemd must know before the old main process exits */
        if (!write_all(fd, "ok\n", 3, deadline) || !read_line(fd, line, sizeof(line), deadline) || strcmp(line, "bye")) {
            snprintf(t, sizeof(t), "MAINPID=%d", (int)old);
            notify(t, 0);
            fprintf(stderr, "--take-over: the running server did not hand over (time-out?): nothing taken over\n");
            goto out;
        }
    }
    for (i = 0; i < (int)h.nudp; i++) socks[i] = fds[i];
    nsocks = (int)h.nudp;
    ctl = fds[h.nudp];
    nfd = 0;
    if (lstat(c->control, &ctl_file) != 0) memset(&ctl_file, 0, sizeof(ctl_file));
    {   /* the sockets as they are bound: port and listen keep their values until a restart */
        SrvConfig was = *c;
        c->port = ntohs(sa[0].sin_port);
        c->nlisten = nsocks == 1 && !sa[0].sin_addr.s_addr ? 0 : nsocks;
        memset(c->listen, 0, sizeof(c->listen));
        for (i = 0; i < c->nlisten; i++) memcpy(c->listen[i], &sa[i].sin_addr, 4);
        if (was.port != c->port || was.nlisten != c->nlisten || memcmp(was.listen, c->listen, sizeof(c->listen)))
            srv_logf("recvx-server: port and listen keep the running server's values until a restart");
    }
    cpu_base[0] = srv_proc.cpu_user_s;
    cpu_base[1] = srv_proc.cpu_sys_s;
    if (srv_proc.start_ms > now_ms()) srv_proc.start_ms = now_ms();   /* (a start ahead of the clock: invalid) */
    srv_proc.handovers++;
    notify("READY=1", 0);
    srv_logf("recvx-server: took over from pid %d (%s): %u rooms, state format %u", (int)old, h.version, srv_stats()->rooms, fmt);
    rc = 0;
out:
    for (i = 0; i < nfd; i++) close(fds[i]);
    if (blob) { explicit_bzero(blob, h.len); free(blob); }
    close(fd);
    return rc;
}

static int ctl_drain(const SrvConfig* c)   /* --drain */
{
    char line[96];
    int fd;
    if (!c->control[0]) { fprintf(stderr, "--drain: no control socket (control = PATH in the config, or --control PATH; or send SIGUSR1)\n"); return 1; }
    if ((fd = ctl_connect(c->control, NULL)) < 0) return 1;
    if (!write_all(fd, "drain\n", 6, now_ms() + CTL_TIMEOUT_MS) || !read_line(fd, line, sizeof(line), now_ms() + CTL_TIMEOUT_MS)) {
        fprintf(stderr, "--drain: no answer\n");
        close(fd);
        return 1;
    }
    close(fd);
    puts(line);
    return strncmp(line, "draining", 8) != 0;
}

int main(int argc, char** argv)
{
    SrvConfig c;
    const char* path;
    int i, takeover = 0, drain_cmd = 0, handed = 0;
    uint64_t start = now_ms(), last_tick = 0, last_stats;
    struct timespec wall;
    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { usage(stdout); return 0; }
        else if (!strcmp(argv[i], "--version")) { puts("recvx-server " RECVX_VERSION); return 0; }
        else if (!strcmp(argv[i], "--take-over")) takeover = 1;
        else if (!strcmp(argv[i], "--drain")) drain_cmd = 1;
    if (load(&c, argc, argv, &path)) { fprintf(stderr, "recvx-server: not started (configuration errors above)\n"); return 2; }
    signal(SIGPIPE, SIG_IGN);
    if (getenv("RECVX_SERVER_TEST_STATE_FORMAT")) srv_state_format = atoi(getenv("RECVX_SERVER_TEST_STATE_FORMAT"));   /* test_handover.sh */
    if (drain_cmd) return ctl_drain(&c);
    if (takeover) {
        /* the launcher waits for the result and returns it (systemd's ExecReload must end); the
           server goes on in the child, in the same service */
        int p[2], rc;
        unsigned char res = 1;
        pid_t pid;
        if (pipe2(p, O_CLOEXEC) != 0 || (pid = fork()) < 0) { perror("recvx-server --take-over"); return 1; }
        if (pid > 0) {
            close(p[1]);
            while (read(p[0], &res, 1) < 0 && errno == EINTR) ;
            return res;
        }
        close(p[0]);
        setsid();
        if ((rc = take_over(&c))) { res = (unsigned char)rc; if (write(p[1], &res, 1) < 0) {} return rc; }
        res = 0;
        if (write(p[1], &res, 1) < 0) {}
        close(p[1]);
    } else {
        if (!srv_init(&c, send_to)) { fprintf(stderr, "out of memory or no randomness\n"); return 1; }
        if (!open_sockets(&c)) return 1;
        if (c.control[0] && (ctl = ctl_open(c.control)) < 0) return 1;
    }
    clock_gettime(CLOCK_REALTIME, &wall);
    srv_proc.version = RECVX_RELEASE;
    srv_proc.process_started_at = (double)wall.tv_sec;
    if (!takeover) { srv_proc.started_at = (double)wall.tv_sec; srv_proc.start_ms = start; }   /* else: from the state */
    srv_proc.handover_capable = ctl >= 0;
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGHUP, on_signal);
    signal(SIGUSR1, on_signal);
    describe(&c, path, takeover ? "took over" : "started");
    if (!takeover) notify("READY=1", 0);
    flush_log();
    proc_info();
    last_stats = now_ms();
    while (!quit) {
        struct pollfd p[SRV_LISTEN_MAX + 2];
        uint64_t now;
        int np = nsocks;
        for (i = 0; i < nsocks; i++) { p[i].fd = socks[i]; p[i].events = POLLIN; p[i].revents = 0; }
        if (ctl >= 0) { p[np].fd = conn >= 0 ? conn : ctl; p[np].events = POLLIN; p[np].revents = 0; np++; }   /* one connection at a time */
        if (poll(p, np, 1000) > 0) {
            for (i = 0; i < nsocks; i++) {
                int k;
                if (!(p[i].revents & POLLIN)) continue;
                for (k = 0; k < 256; k++) {   /* a batch, then the timers */
                    uint8_t buf[RDV_DGRAM_MAX + 1];
                    struct sockaddr_in f;
                    socklen_t fl = sizeof(f);
                    RdvAddr from;
                    int n = (int)recvfrom(socks[i], buf, sizeof(buf), MSG_DONTWAIT, (struct sockaddr*)&f, &fl);
                    if (n < 0) break;
                    memset(&from, 0, sizeof(from));
                    from.fam = 4;
                    memcpy(from.ip, &f.sin_addr, 4);
                    from.port = ntohs(f.sin_port);
                    from.via = (uint8_t)i;
                    srv_input(&from, buf, n > RDV_DGRAM_MAX ? RDV_DGRAM_MAX + 1 : n, now_ms());
                }
            }
            if (np > nsocks && p[nsocks].revents) {
                if (conn >= 0) { if (ctl_input()) { handed = 1; break; } }
                else ctl_accept();
            }
        }
        now = now_ms();
        if (conn >= 0 && now - conn_ms > CTL_TIMEOUT_MS) conn_close();   /* a silent client does not hold the socket */
        if (now - last_tick >= 1000) { proc_info(); srv_tick(now); last_tick = now; }
        if (c.stats_s > 0 && now - last_stats >= (uint64_t)c.stats_s * 1000) {
            char t[2048];
            srv_summary(t, sizeof(t));
            srv_logf("stats: %s", t);
            last_stats = now;
        }
        if (drain) {
            drain = 0;
            if (!srv_draining()) srv_logf("recvx-server: draining (SIGUSR1): no new rooms; stops when the last of %u rooms closed", srv_stats()->rooms);
            srv_drain(1);
        }
        if (srv_draining() && !srv_stats()->rooms) { srv_logf("recvx-server: drained, the last room closed"); break; }
        flush_log();   /* between batches: never inside packet processing */
        if (reload) {   /* SIGHUP: the file again; a bad file keeps the running settings */
            SrvConfig n;
            const char* np2;
            reload = 0;
            if (load(&n, argc, argv, &np2)) { srv_logf("recvx-server: reload failed (errors on stderr), the settings stay as they were"); continue; }
            if (n.port != c.port || n.nlisten != c.nlisten || memcmp(n.listen, c.listen, sizeof(c.listen)) || n.max_rooms != c.max_rooms || strcmp(n.control, c.control))
                srv_logf("recvx-server: port, listen, max_rooms and control keep their values until a restart");
            n.port = c.port; n.nlisten = c.nlisten; memcpy(n.listen, c.listen, sizeof(c.listen)); n.max_rooms = c.max_rooms;
            memcpy(n.control, c.control, sizeof(c.control));
            c = n;
            srv_reconfig(&c);
            describe(&c, np2, "reloaded");
        }
    }
    if (!handed) ctl_close(c.control);   /* handed over: the socket file is the new process's now */
    srv_logf(handed ? "recvx-server: stopped (handed over)" : "recvx-server: stopped");
    flush_log();
    srv_free();
    return 0;
}
