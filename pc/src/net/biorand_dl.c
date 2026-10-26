/* The BioRand CLI download (biorand_dl.h): one background thread per job, no shell. */
#include "biorand_dl.h"
#include "sha256.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#else
#include <sys/stat.h>
#include <sys/statvfs.h>
#endif

void host_log(const char* fmt, ...);
void crash_thread_init(void);
void settings_save_later(void);

#define UA "recvx-enhanced-port"
#define API_MAX (256 << 10)   /* the release JSON is about 7 KB */

char g_biorand_cli_digest[BRDL_DIGEST_CAP];

static SDL_AtomicInt st_state, st_done, st_total, busy, cancel;
static char st_error[160];   /* written before st_state turns BRDL_ERROR (SDL's atomics are full barriers) */
static int job_install;      /* set before the thread starts */
static int job_gen;          /* the CLI runs (gen below); set before the thread starts */
static char gen_seed[96], gen_iso[1100], gen_mods[1100], gen_cfg[1100];
static SDL_SpinLock st_lock;   /* st_msg, st_warn: written by the thread while the UI reads them */
static SDL_AtomicInt st_pct;
static char st_msg[96], st_warn[160];
static int allow_file;       /* test aid: RECVX_BIORAND_API_TEST is a file:// URL */
static char base[1024];      /* the game's folder, with a trailing separator */

static int cancelled(void) { return SDL_GetAtomicInt(&cancel); }

/* 0, the error text kept for the UI (biorand_cli_status) */
static int fail(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    SDL_vsnprintf(st_error, sizeof(st_error), fmt, ap);
    va_end(ap);
    host_log("biorand-cli: %s\n", st_error);
    return 0;
}

static int set_base(void)
{
    const char* b = SDL_getenv("RECVX_BIORAND_CLI_DIR_TEST");
    size_t n;
    if (!b) b = SDL_GetBasePath();   /* the executable's folder, never the current directory */
    if (!b || !(n = strlen(b)) || n > sizeof(base) - 64) return 0;
    SDL_snprintf(base, sizeof(base), "%s%s", b, b[n - 1] == '/' || b[n - 1] == '\\' ? "" : "/");
    return 1;
}

const char* biorand_cli_exe(void)
{
    static char p[1100];   /* set once (the UI's first look, before any job): the worker only reads it */
    if (!p[0] && (base[0] || set_base())) SDL_snprintf(p, sizeof(p), "%s" BRDL_EXE, base);
    return p[0] ? p : NULL;
}

/* a symbolic link or junction: deleted itself, never followed */
static int is_link(const char* path)
{
#ifdef _WIN32
    wchar_t w[1100];
    DWORD a;
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1100)) return 1;
    a = GetFileAttributesW(w);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    struct stat s;
    return lstat(path, &s) == 0 && S_ISLNK(s.st_mode);
#endif
}

static int is_file(const char* path)
{
    SDL_PathInfo i;
    return !is_link(path) && SDL_GetPathInfo(path, &i) && i.type == SDL_PATHTYPE_FILE;
}

int biorand_cli_present(void)
{
    const char* p = biorand_cli_exe();
    return p && is_file(p);
}

/* path, and all below it when it is a real folder (8 levels at most) */
static void rm_tree(const char* path, int depth)
{
    SDL_PathInfo i;
    if (depth < 8 && !is_link(path) && SDL_GetPathInfo(path, &i) && i.type == SDL_PATHTYPE_DIRECTORY) {
        int n = 0, k;
        char** names = SDL_GlobDirectory(path, NULL, 0, &n);
        for (k = 0; names && k < n; k++) {
            char c[1100];
            if (SDL_snprintf(c, sizeof(c), "%s/%s", path, names[k]) < (int)sizeof(c)) rm_tree(c, depth + 1);
        }
        SDL_free(names);
    }
    SDL_RemovePath(path);
}

/* SDL's process: started without a console window on Windows (SDL's background flag: CREATE_NO_WINDOW; on POSIX
   it would detach the child, which then cannot be waited for or killed). out: its stdout and stderr in one pipe.
   cwd: its working folder (NULL: ours). */
static SDL_Process* spawn(const char* const* args, SDL_Environment* env, int out, const char* cwd)
{
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_Process* pr;
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void*)args);
    if (cwd) SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, cwd);
    if (env) SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, env);
    if (out) {
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
        SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    }
#ifdef _WIN32
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
#endif
    pr = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    return pr;
}

/* its exit code once SDL_WaitProcess said it ended. SDL reports 0 for every background process, so on Windows it
   is asked from the system (SDL's handle keeps the process object, so the pid is still this process's); -4 when the
   system cannot tell (never taken for success). */
static int exit_code(SDL_Process* pr, int code)
{
#ifdef _WIN32
    DWORD c;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                           (DWORD)SDL_GetNumberProperty(SDL_GetProcessProperties(pr), SDL_PROP_PROCESS_PID_NUMBER, 0));
    code = -4;
    if (h) {
        if (GetExitCodeProcess(h, &c)) code = (int)c;
        CloseHandle(h);
    }
    if (code == -4) host_log("biorand-cli: the exit code is unknown (Windows error %lu)\n", GetLastError());
#else
    (void)pr;
#endif
    return code;
}

/* args (argv, no shell; in the folder cwd, NULL ours) to their end: the exit code; -1 not started, -2 cancelled,
   -3 `watch` grew past max. While it runs, st_done follows the size of the file `watch`. */
static int run(const char* const* args, const char* cwd, const char* watch, Sint64 max)
{
    SDL_Process* pr = spawn(args, NULL, 0, cwd);
    int code = -1;
    if (!pr) return -1;
    for (;;) {
        SDL_PathInfo i;
        if (SDL_WaitProcess(pr, false, &code)) { code = exit_code(pr, code); break; }
        if (watch && SDL_GetPathInfo(watch, &i)) {
            SDL_SetAtomicInt(&st_done, (int)SDL_min(i.size, max));
            if ((Sint64)i.size > max) code = -3;
        }
        if (cancelled()) code = -2;
        if (code < -1) {
            SDL_KillProcess(pr, true);
            SDL_WaitProcess(pr, true, NULL);
            break;
        }
        SDL_Delay(100);
    }
    SDL_DestroyProcess(pr);
    return code;
}

#ifdef _WIN32
/* WinHTTP: the system's TLS and certificate store; redirects are followed (never https to http) */
static int fetch(const char* url, const char* file, Sint64 max, int api)
{
    wchar_t w[1024], host[256];
    URL_COMPONENTS uc;
    HINTERNET s = NULL, c = NULL, r = NULL;
    SDL_IOStream* f = NULL;
    DWORD status = 0, len = sizeof(status), got, proto = 0x800 | 0x2000;   /* TLS 1.2 and 1.3 */
    Sint64 total = 0;
    char buf[16384];
    int ok = 0;
    if (!MultiByteToWideChar(CP_UTF8, 0, url, -1, w, 1024)) return fail("the URL is too long");
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.dwUrlPathLength = (DWORD)-1;   /* a pointer into w: the path with its query, to the end */
    if (!WinHttpCrackUrl(w, 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) return fail("cannot use the URL %s", url);
    if (!(s = WinHttpOpen(L"" UA, 4 /* WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, 8.1+ */, NULL, NULL, 0)) &&
        !(s = WinHttpOpen(L"" UA, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0))) return fail("WinHTTP is not available (%lu)", GetLastError());
    if (!WinHttpSetOption(s, WINHTTP_OPTION_SECURE_PROTOCOLS, &proto, sizeof(proto))) {
        proto = 0x800;   /* Windows before TLS 1.3 */
        WinHttpSetOption(s, WINHTTP_OPTION_SECURE_PROTOCOLS, &proto, sizeof(proto));
    }
    WinHttpSetTimeouts(s, 0, 20000, 30000, 60000);
    if (!(c = WinHttpConnect(s, host, uc.nPort, 0)) ||
        !(r = WinHttpOpenRequest(c, L"GET", uc.lpszUrlPath, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)) ||
        !WinHttpSendRequest(r, api ? L"Accept: application/vnd.github+json" : WINHTTP_NO_ADDITIONAL_HEADERS, (DWORD)-1, NULL, 0, 0, 0) ||
        !WinHttpReceiveResponse(r, NULL)) {
        fail("no connection to GitHub (WinHTTP error %lu)", GetLastError());
        goto out;
    }
    WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &len, NULL);
    if (status != 200 && !api) { fail("GitHub answered HTTP %lu", status); goto out; }   /* the API's error body is read: its message */
    if (!(f = SDL_IOFromFile(file, "wb"))) { fail("cannot write %s", file); goto out; }
    for (;;) {
        if (cancelled()) { fail("cancelled"); goto out; }
        if (!WinHttpReadData(r, buf, sizeof(buf), &got)) { fail("the download broke off (WinHTTP error %lu)", GetLastError()); goto out; }
        if (!got) break;
        if ((total += got) > max) { fail("the file is larger than expected"); goto out; }
        if (SDL_WriteIO(f, buf, got) != got) { fail("cannot write %s (disk full?)", file); goto out; }
        SDL_SetAtomicInt(&st_done, (int)total);
    }
    ok = 1;
out:
    if (f && !SDL_CloseIO(f) && ok) ok = fail("cannot write %s", file);
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return ok;
}
#else
/* the system's curl, else wget: their TLS and certificates, https only (file:// for the test aid) */
static int fetch(const char* url, const char* file, Sint64 max, int api)
{
    char mx[24];
    int r;
    SDL_snprintf(mx, sizeof(mx), "%lld", (long long)max);
    {
        const char* curl[] = { "curl", api ? "-sSL" : "-fsSL", "--proto", allow_file ? "=https,file" : "=https", "--proto-redir", "=https",
                               "--tlsv1.2", "--max-redirs", "5", "--connect-timeout", "20", "--speed-time", "60", "--speed-limit", "1",
                               "--max-filesize", mx, "-A", UA, "-H", api ? "Accept: application/vnd.github+json" : "Accept: */*",
                               "-o", file, "--", url, NULL };
        if ((r = run(curl, NULL, file, max)) == 0) return 1;
        switch (r) {
        case -1: break;   /* no curl */
        case -2: return fail("cancelled");
        case -3: case 63: return fail("the file is larger than expected");
        case 6: return fail("cannot find GitHub's server (no internet connection?)");
        case 7: return fail("cannot connect to GitHub");
        case 22: return fail("GitHub answered with an error (HTTP 4xx/5xx)");
        case 28: return fail("the connection timed out");
        case 35: case 60: return fail("the secure connection failed (TLS or certificate)");
        default: return fail("curl failed (exit code %d)", r);
        }
    }
    if (allow_file) return fail("file:// needs curl");
    {
        char hdr[64];
        const char* wget[] = { "wget", "-q", "--https-only", "--max-redirect=5", "--timeout=60", "--tries=2", "-U", UA,
                               hdr, api ? "--content-on-error" : "-q", "-O", file, "--", url, NULL };
        SDL_snprintf(hdr, sizeof(hdr), "--header=Accept: %s", api ? "application/vnd.github+json" : "*/*");
        r = run(wget, NULL, file, max);
        if (r == 0 || (api && r == 8)) return 1;   /* 8: an HTTP error; the API's body tells which */
        switch (r) {
        case -1: return fail("cannot download: install curl or wget");
        case -2: return fail("cancelled");
        case -3: return fail("the file is larger than expected");
        case 4: return fail("cannot connect to GitHub (no internet connection?)");
        case 5: return fail("the secure connection failed (TLS or certificate)");
        case 8: return fail("GitHub answered with an error (HTTP 4xx/5xx)");
        default: return fail("wget failed (exit code %d)", r);
        }
    }
}
#endif

static int sha256_file(const char* path, char hex[65])
{
    SDL_IOStream* f = SDL_IOFromFile(path, "rb");
    Sha256 s;
    uint8_t d[32], buf[65536];
    size_t n;
    if (!f) return 0;
    sha256_init(&s);
    while ((n = SDL_ReadIO(f, buf, sizeof(buf))) > 0 && !cancelled()) sha256_update(&s, buf, n);
    SDL_CloseIO(f);
    sha256_final(&s, d);
    sha256_hex(d, hex);
    return !cancelled();
}

/* tmp/BRDL_ASSET into tmp/x. tar runs in tmp with relative, ASCII-only arguments: Windows' bsdtar reads its command
   line in the ANSI code page, so a game folder with other characters (or a long one) would not reach it intact. */
static int extract(const char* tmp)
{
    int r;
#ifdef _WIN32
    /* Windows 10 1803+ bsdtar reads zip. A 32-bit process sees SysWOW64 as System32 (both hold a tar.exe);
       Sysnative is the real System32. */
    static const char* sub[] = { "System32", "Sysnative", "SysWOW64" };
    const char* root = SDL_getenv("SystemRoot");
    char tar[MAX_PATH] = "";
    int i;
    for (i = 0; i < 3 && !tar[0]; i++) {
        SDL_snprintf(tar, sizeof(tar), "%s\\%s\\tar.exe", root ? root : "C:\\Windows", sub[i]);
        if (!SDL_GetPathInfo(tar, NULL)) tar[0] = 0;
    }
    if (!tar[0]) return fail("Windows' tar.exe was not found (it comes with Windows 10 1803 and later)");
    {
        const char* args[] = { tar, "-xf", BRDL_ASSET, "-C", "x", NULL };
        r = run(args, tmp, NULL, 0);
    }
#else
    const char* args[] = { "tar", "-xzf", BRDL_ASSET, "-C", "x", NULL };
    r = run(args, tmp, NULL, 0);
#endif
    if (r == -1) return fail("cannot start tar to unpack the archive");
    if (r == -2) return fail("cancelled");
    if (r) return fail("tar could not unpack the archive (exit code %d)", r);
    return 1;
}

/* src renamed over dst. On Windows a fresh file may still be open in a virus scanner (Defender scans what tar
   wrote), so a failure is retried for up to 5 s. */
static int move_file(const char* src, const char* dst)
{
    bool ok = SDL_RenamePath(src, dst);
#ifdef _WIN32
    for (int i = 0; !ok && i < 20 && !cancelled(); i++) {
        SDL_Delay(250);
        ok = SDL_RenamePath(src, dst);
    }
#endif
    if (!ok) host_log("biorand-cli: cannot move %s to %s (%s)\n", src, dst, SDL_GetError());
    return ok;
}

/* the final state, 0 for BRDL_ERROR */
static int job(void)
{
    const char* api = SDL_getenv("RECVX_BIORAND_API_TEST");
    char tmp[1100], json[1100], arc[1100], x[1100], src[1100], dst[1100], msg[100], hex[65];
    BrdlAsset a;
    SDL_PathInfo info;
    char* body;
    size_t len = 0;
    int r;
    if (!api) api = BRDL_API_URL;
    allow_file = !strncmp(api, "file:///", 8);
    if (!brdl_url_ok(api, allow_file)) return fail("not an allowed URL: %s", api);
    if (!base[0] && !set_base()) return fail("cannot find the game's folder");
    SDL_snprintf(tmp, sizeof(tmp), "%sbiorand-dl", base);
    SDL_snprintf(json, sizeof(json), "%s/release.json", tmp);
    SDL_snprintf(arc, sizeof(arc), "%s/" BRDL_ASSET, tmp);
    SDL_snprintf(x, sizeof(x), "%s/x", tmp);
    rm_tree(tmp, 0);   /* a left-over from a killed run */
    if (!SDL_CreateDirectory(x)) return fail("cannot create %s (is the game's folder writable?)", x);

    /* 1. the release */
    if (!fetch(api, json, API_MAX, 1)) return 0;
    if (!(body = SDL_LoadFile(json, &len))) return fail("cannot read GitHub's answer");
    r = brdl_find_asset(body, (int)SDL_min(len, API_MAX), BRDL_ASSET, allow_file, &a, msg, sizeof(msg));
    SDL_free(body);
    switch (r) {
    case BRJ_OK: break;
    case BRJ_MESSAGE:
        if (!strncmp(msg, "API rate limit exceeded", 23)) return fail("GitHub allows 60 requests per hour; try again later");
        return fail("GitHub: %s", msg);
    case BRJ_NO_ASSET: return fail("the nightly release has no " BRDL_ASSET);
    case BRJ_MALFORMED: return fail("GitHub's answer is not a release");
    default: return fail("the release lists " BRDL_ASSET " with an invalid %s", r == BRJ_BAD_SIZE ? "size" : r == BRJ_BAD_DIGEST ? "digest" : "URL");
    }
    host_log("biorand-cli: nightly %s, %d bytes, %s\n", BRDL_ASSET, a.size, a.digest);
    if (!job_install) return biorand_cli_present() && !strcmp(a.digest, g_biorand_cli_digest) ? BRDL_CURRENT : BRDL_NEWER;

    /* 2. the archive */
    SDL_SetAtomicInt(&st_total, a.size);
    SDL_SetAtomicInt(&st_state, BRDL_DOWNLOAD);
    if (!fetch(a.url, arc, a.size, 0)) return 0;
    if (!SDL_GetPathInfo(arc, &info) || info.size != (Uint64)a.size) return fail("the download is incomplete");
    SDL_SetAtomicInt(&st_done, a.size);   /* (a fast download ends before run() looks at its size) */

    /* 3. its digest */
    SDL_SetAtomicInt(&st_state, BRDL_VERIFY);
    if (!sha256_file(arc, hex)) return fail(cancelled() ? "cancelled" : "cannot read the download");
    if (strcmp(hex, a.digest + 7)) {
        SDL_RemovePath(arc);
        host_log("biorand-cli: SHA-256 %s, expected %s\n", hex, a.digest + 7);
        return fail("the download is damaged (its SHA-256 differs from GitHub's); nothing was installed");
    }

    /* 4. unpacked */
    SDL_SetAtomicInt(&st_state, BRDL_EXTRACT);
    if (!extract(tmp)) return 0;
    SDL_snprintf(src, sizeof(src), "%s/" BRDL_EXE, x);
    if (!is_file(src)) return fail("the archive holds no " BRDL_EXE);

    /* 5. installed: the licence first, the program last (its presence means installed) */
    SDL_snprintf(src, sizeof(src), "%s/LICENSE", x);
    SDL_snprintf(dst, sizeof(dst), "%sbiorand-cli.LICENSE", base);
    if (!is_file(src) || !move_file(src, dst)) host_log("biorand-cli: no LICENSE in the archive\n");
    SDL_snprintf(src, sizeof(src), "%s/" BRDL_EXE, x);
    if (cancelled()) return fail("cancelled");
    if (!move_file(src, biorand_cli_exe())) return fail("cannot replace %s (is it running?)", biorand_cli_exe());
#ifndef _WIN32
    chmod(biorand_cli_exe(), 0755);
#endif
    SDL_strlcpy(g_biorand_cli_digest, a.digest, sizeof(g_biorand_cli_digest));
    settings_save_later();
    host_log("biorand-cli: installed %s\n", biorand_cli_exe());
    return BRDL_INSTALLED;
}

/* ---------------------------------------------------------------- generate (the CLI runs) */
int biorand_free_mb(const char* dir)
{
    unsigned long long mb;
#ifdef _WIN32
    wchar_t w[1100];
    ULARGE_INTEGER fr;
    if (!MultiByteToWideChar(CP_UTF8, 0, dir, -1, w, 1100) || !GetDiskFreeSpaceExW(w, &fr, NULL, NULL)) return -1;
    mb = fr.QuadPart >> 20;
#else
    struct statvfs v;
    if (statvfs(dir, &v)) return -1;
    mb = (unsigned long long)v.f_bavail * v.f_frsize >> 20;
#endif
    return mb > 0x7FFFFFFF ? 0x7FFFFFFF : (int)mb;
}

/* what a run leaves behind in mods: the CLI's own output folders (killed between its writing and its rename) and
   our scratch folder for it; nothing else there is touched */
static void gen_clean(const char* mods)
{
    static const char* const pat[] = { "biorand.tmp-*", "biorand.bak-*" };
    char p[1200];
    for (int k = 0; k < 2; k++) {
        int n = 0;
        char** names = SDL_GlobDirectory(mods, pat[k], 0, &n);
        for (int i = 0; names && i < n; i++)
            if (!strchr(names[i], '/') && !strchr(names[i], '\\') && SDL_snprintf(p, sizeof(p), "%s/%s", mods, names[i]) < (int)sizeof(p)) {
                host_log("biorand-cli: deleting %s\n", p);
                rm_tree(p, 0);
            }
        SDL_free(names);
    }
    SDL_snprintf(p, sizeof(p), "%s/biorand-temp", mods);
    rm_tree(p, 0);
}

static void gen_line(char* l)
{
    const char* t;
    int pct, k = brdl_line(l, &pct, &t);
    if (!k) { host_log("biorand-cli: %s\n", l); return; }
    SDL_LockSpinlock(&st_lock);
    if (k == 'P') { SDL_strlcpy(st_msg, t, sizeof(st_msg)); SDL_SetAtomicInt(&st_pct, pct); }
    else if (k == 'W') SDL_strlcpy(st_warn, t, sizeof(st_warn));
    else SDL_strlcpy(st_error, t, sizeof(st_error));   /* (the UI reads it only once st_state is BRDL_ERROR) */
    SDL_UnlockSpinlock(&st_lock);
    if (k != 'P') host_log("biorand-cli: %s\n", l);
}

static int gen(void)
{
    const char* exe = biorand_cli_exe();
    char out[1200], tmp[1200], line[512];
    const char* args[] = { exe, "-s", gen_seed, "-i", gen_iso, "-o", out, "-f", NULL };
    SDL_Environment* env;
    SDL_Process* pr;
    SDL_IOStream* io;
    int code = -1, n = 0, ended = 0, eof = 0;
    SDL_snprintf(out, sizeof(out), "%s/biorand", gen_mods);
    SDL_snprintf(tmp, sizeof(tmp), "%s/biorand-temp", gen_mods);
    gen_clean(gen_mods);
    if (!exe || !biorand_cli_present()) return fail("the BioRand CLI is not installed");
    if (!SDL_CreateDirectory(tmp)) return fail("cannot create %s", tmp);
    if (!(env = SDL_CreateEnvironment(true))) return fail("cannot start the BioRand CLI (%s)", SDL_GetError());
    SDL_SetEnvironmentVariable(env, "BIORAND_CONFIG", gen_cfg, true);
    SDL_SetEnvironmentVariable(env, "TMPDIR", tmp, true);   /* its scratch (2.4 GB) next to the pack, not in a RAM /tmp */
    SDL_SetEnvironmentVariable(env, "TMP", tmp, true);      /* (Windows: GetTempPath) */
    SDL_SetEnvironmentVariable(env, "TEMP", tmp, true);
    host_log("biorand-cli: %s -s %s -i \"%s\" -o \"%s\" -f (BIORAND_CONFIG %s)\n", exe, gen_seed, gen_iso, out, gen_cfg);
    pr = spawn(args, env, 1, NULL);
    SDL_DestroyEnvironment(env);
    if (!pr) { fail("cannot start the BioRand CLI (%s)", SDL_GetError()); gen_clean(gen_mods); return 0; }
    io = SDL_GetProcessOutput(pr);
    while (!eof || !ended) {
        char buf[1024];
        size_t got = io && !eof ? SDL_ReadIO(io, buf, sizeof(buf)) : 0;
        for (size_t i = 0; i < got; i++) {
            if (buf[i] == '\n' || n == (int)sizeof(line) - 1) {   /* (a longer line is cut) */
                if (n && line[n - 1] == '\r') n--;
                line[n] = 0;
                if (n) gen_line(line);
                n = 0;
                if (buf[i] == '\n') continue;
            }
            line[n++] = buf[i];
        }
        if (!got && (!io || SDL_GetIOStatus(io) != SDL_IO_STATUS_NOT_READY)) eof = 1;
        if (!ended && SDL_WaitProcess(pr, false, &code)) { code = exit_code(pr, code); ended = 1; }
        if (cancelled() && !ended) {
            SDL_KillProcess(pr, true);
            SDL_WaitProcess(pr, true, NULL);
            ended = eof = 1;
            code = -2;
        }
        if (!got) SDL_Delay(30);
    }
    if (n) { line[n] = 0; gen_line(line); }
    SDL_DestroyProcess(pr);
    gen_clean(gen_mods);
    host_log("biorand-cli: exit code %d\n", code);
    if (code == 0) return BRDL_GENERATED;
    SDL_LockSpinlock(&st_lock);
    SDL_strlcpy(line, st_error, sizeof(line));
    SDL_UnlockSpinlock(&st_lock);
    if (code == -2) return fail("cancelled");
    if (strstr(line, "eed version")) return fail("%s Update the game or use an older BioRand CLI.", line);
    if (code == 2 && line[0]) return fail("%s", line);
    if (code == 3) return fail("BioRand failed: %s", line[0] ? line : "no reason given");
    return fail("BioRand ended with exit code %d%s%s", code, line[0] ? ": " : "", line);
}

static int SDLCALL worker(void* ud)
{
    char tmp[1100];
    int s;
    (void)ud;
    crash_thread_init();
    if (!(s = job_gen ? gen() : job())) s = BRDL_ERROR;
    if (base[0] && !job_gen) {   /* the temporary folder goes in every case */
        SDL_snprintf(tmp, sizeof(tmp), "%sbiorand-dl", base);
        rm_tree(tmp, 0);
    }
    if (s == BRDL_ERROR && cancelled()) s = BRDL_CANCELLED;
    SDL_SetAtomicInt(&st_state, s);
    SDL_SetAtomicInt(&busy, 0);
    return 0;
}

static int start(int install)
{
    SDL_Thread* t;
    if (!SDL_CompareAndSwapAtomicInt(&busy, 0, 1)) return 0;
    job_install = install;
    job_gen = install == 2;
    SDL_SetAtomicInt(&cancel, 0);
    SDL_SetAtomicInt(&st_done, 0);
    SDL_SetAtomicInt(&st_total, 0);
    SDL_SetAtomicInt(&st_pct, 0);
    SDL_LockSpinlock(&st_lock);
    st_msg[0] = st_warn[0] = st_error[0] = 0;
    SDL_UnlockSpinlock(&st_lock);
    SDL_SetAtomicInt(&st_state, job_gen ? BRDL_GENERATE : BRDL_API);
    if ((t = SDL_CreateThread(worker, "biorand-dl", NULL))) { SDL_DetachThread(t); return 1; }   /* detached: exit does not wait */
    fail("cannot start a thread");
    SDL_SetAtomicInt(&st_state, BRDL_ERROR);
    SDL_SetAtomicInt(&busy, 0);
    return 0;
}

int biorand_cli_check_update(void) { return start(0); }
int biorand_cli_install(void) { return start(1); }

int biorand_cli_generate(const char* seed, const char* iso, const char* mods, const char* cfg)
{
    if (SDL_GetAtomicInt(&busy)) return 0;   /* (start checks again; the strings are the thread's only once it runs) */
    SDL_strlcpy(gen_seed, seed, sizeof(gen_seed));
    SDL_strlcpy(gen_iso, iso, sizeof(gen_iso));
    SDL_strlcpy(gen_mods, mods, sizeof(gen_mods));
    SDL_strlcpy(gen_cfg, cfg, sizeof(gen_cfg));
    return start(2);
}
void biorand_cli_cancel(void) { SDL_SetAtomicInt(&cancel, 1); }

void biorand_cli_status(BrdlStatus* s)
{
    s->state = SDL_GetAtomicInt(&st_state);
    s->done = SDL_GetAtomicInt(&st_done);
    s->total = SDL_GetAtomicInt(&st_total);
    s->pct = SDL_GetAtomicInt(&st_pct);
    SDL_LockSpinlock(&st_lock);
    if (s->state == BRDL_ERROR) SDL_strlcpy(s->error, st_error, sizeof(s->error));
    else s->error[0] = 0;
    SDL_strlcpy(s->msg, st_msg, sizeof(s->msg));
    SDL_strlcpy(s->warn, st_warn, sizeof(s->warn));
    SDL_UnlockSpinlock(&st_lock);
}
