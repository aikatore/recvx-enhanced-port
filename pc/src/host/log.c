/*
 * --log[=file]: everything written to stdout/stderr (the game's own printf diagnostics and
 * the port's messages) is copied to a log file, each line prefixed with the time since start.
 * A fatal signal (segfault, abort) appends a backtrace to the log before the process dies.
 * Always, also without --log: a crash writes recvx-crash.log (the current directory, where --log puts
 * recvx.log): the version, the signal or exception, the backtrace and the port's last messages
 * (host_log keeps the last LAST_N in memory), a few KB players can attach to a bug report.
 * Linux: each thread that may crash calls crash_thread_init for an alternate signal stack of its own
 * (a stack overflow has none left to run the handler on); the handler writes the signal, the address
 * and the thread with write() alone first, then the backtrace and the last messages.
 */
#ifndef _WIN32
#define _GNU_SOURCE   /* REG_EIP */
#endif
#ifdef _WIN32
#define GetThreadId win_GetThreadId   /* the PS2 kernel shim declares its own */
#define CreateThread win_CreateThread
#include <windows.h>
#include <io.h>
#undef GetThreadId
#undef CreateThread
#endif
#include "../ps2_platform.h"
#include <SDL3/SDL.h>
#include "coop.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LAST_N 48                       /* host_log lines kept for the crash report */
#define LAST_W 200
static char last[LAST_N][LAST_W];
static SDL_AtomicInt last_n;
static const char crash_path[] = "recvx-crash.log";
static char crash_head[128];

/* host.c host_log: every message, also with --quiet (best effort across threads: each takes a slot of its own);
   IPv4 addresses masked, so the report can be posted publicly (pc/src/net/net.c); the co-op server's kept */
const char* net_mask_addrs(const char* text, const char* const* hosts, const char* const* keep, char* out, int cap);
void log_keep(const char* fmt, va_list ap)
{
    char t[LAST_W], srv[sizeof(g_coop.server)];
    const char* keep[] = { srv, NULL };
    int i = SDL_AddAtomicInt(&last_n, 1) % LAST_N;
    memcpy(srv, (const void*)g_coop.server, sizeof(srv));   /* another thread may be editing it: a bounded copy */
    srv[sizeof(srv) - 1] = 0;
    SDL_vsnprintf(t, sizeof(t), fmt, ap);
    net_mask_addrs(t, NULL, keep, last[i], LAST_W);
}

/* test aid, game thread: RECVX_TEST_CRASH="stack" (a stack overflow) or "null" (a write through a null
   pointer) at frame hand-off n ("stack:n", default 100), to check the crash report */
static __attribute__((noinline)) int overflow(volatile char* p)
{
    volatile char b[4096];
    b[0] = *p;
    return b[0] == 42 ? 0 : overflow(b) + b[1];   /* (never 42: c is 0) */
}
void crash_test(unsigned hand)
{
    static const char* how = (const char*)-1;
    const char* at;
    if (how == (const char*)-1) how = SDL_getenv("RECVX_TEST_CRASH");
    if (!how || hand < ((at = SDL_strchr(how, ':')) ? (unsigned)SDL_atoi(at + 1) : 100u)) return;
    host_log("test: crash (%s) at hand-off %u\n", how, hand);
    if (!SDL_strncmp(how, "stack", 5)) { volatile char c = 0; overflow(&c); }
    *(volatile int*)(uintptr_t)SDL_atoi("0") = 1;
}

/* the kept lines, oldest first, through `out` (async-signal-safe on Linux: write) */
static void last_dump(void (*out)(void*, const char*, size_t), void* ud)
{
    int n = SDL_GetAtomicInt(&last_n), k;
    out(ud, "\nlast messages:\n", 16);
    for (k = n > LAST_N ? n - LAST_N : 0; k < n; k++) {
        const char* l = last[k % LAST_N];
        size_t len = strlen(l);
        out(ud, l, len);
        if (!len || l[len - 1] != '\n') out(ud, "\n", 1);
    }
}
#ifndef _WIN32
#include <execinfo.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>

static int log_fd = -1, console_fd = 2, pipe_rd = -1;
static uint64_t t0;

static void write_all(int fd, const char* p, size_t n)
{
    while (n) { ssize_t k = write(fd, p, n); if (k <= 0) return; p += k; n -= (size_t)k; }
}

static int SDLCALL tee_thread(void* ud)
{
    char buf[4096], stamp[32];
    int at_line_start = 1;
    (void)ud;
    for (;;) {
        ssize_t n = read(pipe_rd, buf, sizeof(buf));
        if (n <= 0) break;
        write_all(console_fd, buf, (size_t)n);
        for (ssize_t i = 0; i < n;) {
            ssize_t j = i;
            if (at_line_start) {
                double t = (SDL_GetTicksNS() - t0) / 1e9;
                int len = SDL_snprintf(stamp, sizeof(stamp), "[%9.3f] ", t);
                write_all(log_fd, stamp, (size_t)len);
                at_line_start = 0;
            }
            while (j < n && buf[j] != '\n') j++;
            if (j < n) { j++; at_line_start = 1; }
            write_all(log_fd, buf + i, (size_t)(j - i));
            i = j;
        }
    }
    return 0;
}

static void out_fd(void* fd, const char* p, size_t n) { write_all(*(int*)fd, p, n); }

/* async-signal-safe text: s, or v in base 10 / 16, appended at p */
static char* put_s(char* p, const char* s) { while (*s) *p++ = *s++; return p; }
static char* put_u(char* p, unsigned long v, unsigned base)
{
    char t[24];
    int n = 0;
    do t[n++] = "0123456789abcdef"[v % base]; while (v /= base);
    while (n) *p++ = t[--n];
    return p;
}

static void on_fatal(int sig, siginfo_t* si, void* uc)
{
    static int entered;
    void* frames[64];
    int n, fd;
    long tid = syscall(SYS_gettid);
    char msg[256], *p = msg;
    if (__atomic_exchange_n(&entered, 1, __ATOMIC_ACQ_REL)) for (;;) pause();   /* another thread is writing the report; the process ends with it */
    /* first what write() alone can give: the signal, the address, the thread */
    p = put_s(p, "\nfatal signal "); p = put_u(p, (unsigned)sig, 10);
    p = put_s(p, sig == SIGSEGV ? " (segmentation fault)" : sig == SIGABRT ? " (abort)" : sig == SIGBUS ? " (bus error)" : sig == SIGFPE ? " (arithmetic)" : sig == SIGILL ? " (illegal instruction)" : " (signal)");
    p = put_s(p, ", address 0x"); p = put_u(p, (unsigned long)si->si_addr, 16);
#if defined(__i386__)
    p = put_s(p, ", eip 0x"); p = put_u(p, (unsigned long)((ucontext_t*)uc)->uc_mcontext.gregs[REG_EIP], 16);
#elif defined(__x86_64__)
    p = put_s(p, ", rip 0x"); p = put_u(p, (unsigned long)((ucontext_t*)uc)->uc_mcontext.gregs[REG_RIP], 16);
#else
    (void)uc;
#endif
    p = put_s(p, ", thread "); p = put_u(p, (unsigned long)tid, 10);
    p = put_s(p, tid == getpid() ? " (main)\n" : "\n");
    write_all(console_fd, msg, (size_t)(p - msg));
    if (log_fd >= 0) write_all(log_fd, msg, (size_t)(p - msg));
    if ((fd = open(crash_path, O_WRONLY | O_CREAT | O_TRUNC, 0644)) >= 0) {
        write_all(fd, crash_head, strlen(crash_head));
        write_all(fd, msg + 1, (size_t)(p - msg) - 1);
        fsync(fd);
    }
    /* then the backtrace (backtrace() was primed at start: no library loading here) and the last messages */
    n = backtrace(frames, 64);
    write_all(console_fd, "backtrace:\n", 11);
    backtrace_symbols_fd(frames, n, console_fd);
    if (log_fd >= 0) { write_all(log_fd, "backtrace:\n", 11); backtrace_symbols_fd(frames, n, log_fd); fsync(log_fd); }
    if (fd >= 0) {
        write_all(fd, "backtrace:\n", 11);
        backtrace_symbols_fd(frames, n, fd);
        last_dump(out_fd, &fd);
        fsync(fd);
        close(fd);
        write_all(console_fd, "crash report written to recvx-crash.log\n", 41);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

/* each thread that may crash (the main thread through crash_init): an own stack for the handler, so
   stack overflows are reported too; freed when the thread ends */
static pthread_key_t altstack_key;
static void altstack_free(void* p)
{
    stack_t ss = { .ss_flags = SS_DISABLE };
    sigaltstack(&ss, NULL);
    free(p);
}
void crash_thread_init(void)
{
    stack_t ss = { .ss_size = 64 * 1024 };
    if (!(ss.ss_sp = malloc(ss.ss_size))) return;
    if (sigaltstack(&ss, NULL)) { free(ss.ss_sp); return; }
    pthread_setspecific(altstack_key, ss.ss_sp);
}

/* exit(): what the game printed just before is still in the pipe (the tee thread dies
   with the process) - copy it now and say that the process exited */
static void on_exit_drain(void)
{
    char buf[4096];
    ssize_t n;
    static const char msg[] = "\nprocess exiting (exit() called)\n";
    fflush(stdout); fflush(stderr);
    if (pipe_rd < 0 || log_fd < 0) return;
    fcntl(pipe_rd, F_SETFL, fcntl(pipe_rd, F_GETFL) | O_NONBLOCK);
    while ((n = read(pipe_rd, buf, sizeof(buf))) > 0) { write_all(log_fd, buf, (size_t)n); write_all(console_fd, buf, (size_t)n); }
    write_all(log_fd, msg, sizeof(msg) - 1);
    fsync(log_fd);
}

/* main(), always: the crash report's handler */
void crash_init(const char* version)
{
    struct sigaction sa;
    void* f;
    static const int sigs[] = { SIGSEGV, SIGABRT, SIGFPE, SIGBUS, SIGILL, SIGTRAP };
    SDL_snprintf(crash_head, sizeof(crash_head), "recvx-enhanced-port %s crashed (Linux; addresses: addr2line -f -e recvx)\n", version);
    pthread_key_create(&altstack_key, altstack_free);
    crash_thread_init();
    backtrace(&f, 1);   /* loads what backtrace needs now, not in the handler */
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = on_fatal;
    sa.sa_flags = SA_ONSTACK | SA_SIGINFO;
    for (unsigned i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++) sigaction(sigs[i], &sa, NULL);
}

void log_start(const char* path)
{
    int p[2];
    t0 = SDL_GetTicksNS();
    log_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (log_fd < 0) { fprintf(stderr, "log: cannot write %s\n", path); return; }
    if (pipe(p) != 0) { close(log_fd); log_fd = -1; return; }
    fflush(stdout); fflush(stderr);
    console_fd = dup(2);
    pipe_rd = p[0];
    dup2(p[1], 1);
    dup2(p[1], 2);
    close(p[1]);
    SDL_DetachThread(SDL_CreateThread(tee_thread, "log", NULL));
    atexit(on_exit_drain);
    host_log("log: writing %s\n", path);
}
#else
/* fixed image base (no ASLR): addresses map to functions with i686-w64-mingw32-addr2line -f -e recvx.exe;
   the Windows build keeps frame pointers (pc/CMakeLists.txt), so EBP is the frame chain */
static void out_file(void* f, const char* p, size_t n) { fwrite(p, 1, n, (FILE*)f); }

static LONG WINAPI on_fatal(EXCEPTION_POINTERS* ep)
{
    static LONG entered;
    const CONTEXT* c = ep->ContextRecord;
    uint32_t* fp = (uint32_t*)c->Ebp;
    char msg[512];
    int len;
    if (InterlockedExchange(&entered, 1)) for (;;) Sleep(INFINITE);   /* another thread is writing the report */
    len = snprintf(msg, sizeof(msg), "\nfatal exception 0x%08lX at 0x%08lX (esp 0x%08lX), frame chain:",
                       ep->ExceptionRecord->ExceptionCode, (unsigned long)c->Eip, (unsigned long)c->Esp);
    FILE* f;
    for (int i = 0; i < 32 && fp && !IsBadReadPtr(fp, 8) && len < (int)sizeof(msg) - 16; i++, fp = (uint32_t*)fp[0])
        len += snprintf(msg + len, sizeof(msg) - len, " 0x%08X", fp[1]);
    fprintf(stderr, "%s\n", msg);
    fflush(stderr);
    if ((f = fopen(crash_path, "w"))) {
        fprintf(f, "%s%s\n", crash_head, msg + 1);
        last_dump(out_file, f);
        fclose(f);
        fprintf(stderr, "crash report written to %s\n", crash_path);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void crash_thread_init(void) {}   /* (the filter runs on the faulting thread) */

/* main(), always: the crash report's handler */
void crash_init(const char* version)
{
    snprintf(crash_head, sizeof(crash_head), "recvx-enhanced-port %s crashed (Windows; addresses: i686-w64-mingw32-addr2line -f -e recvx.exe)\n", version);
    SetUnhandledExceptionFilter(on_fatal);
}

void log_start(const char* path)
{
    wchar_t w[1024];   /* the path is UTF-8 (fileio.h) */
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1024) || !_wfreopen(w, L"w", stdout)) return;
    setvbuf(stdout, NULL, _IONBF, 0);
    _dup2(_fileno(stdout), 2);
    host_log("log: writing %s\n", path);
}
#endif
