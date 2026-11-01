/* Update check and install (update.h): the check on one background thread per run (GitHub, else our server, up to 3
   attempts each), the install on another when the player asks for it. */
#include "update.h"
#include "net.h"
#include "../ps2_platform.h"
#include <SDL3/SDL.h>
#ifndef _WIN32   /* (not windows.h: ps2_platform.h's PS2 names clash with it; biorand_dl.c has the Windows parts) */
#include <sys/stat.h>
#include <unistd.h>
#endif

void host_log(const char* fmt, ...);

#define API_MAX (512 << 10)   /* GitHub's answer: about 20 KB a release */
#define MAX_FILES 64          /* files in the archive (0.7.1: 8) */

char g_update_skip[32];

static SDL_AtomicInt state = { UPD_UNKNOWN };   /* UPD_CHECKING while the thread runs, then KNOWN or UNKNOWN */
static char newest[32];                         /* written before state turns UPD_KNOWN */
static BrdlAsset asset;                         /* GitHub's archive for this build (url "": none, or our server answered); as newest */
static SDL_AtomicInt started;
static BrdlJob cj = { .log = "update", .english = 1, .too_big = "the answer is too long" };   /* the check's (before tr_init) */

const char* update_installed(void)
{
    static char s[24];
    const char* v = SDL_getenv("RECVX_UPDATE_FAKE_VERSION");   /* test aid: the running build is this version */
    if (!v) v = RECVX_VERSION;
    else if (*v == 'v') v++;
    if (!s[0]) snprintf(s, sizeof(s), "v%.*s", (int)strcspn(v, " "), v);
    return s;
}

static int env_off(void) { const char* e = SDL_getenv("RECVX_UPDATE_CHECK"); return e && !strcmp(e, "0"); }

/* one request to GitHub's API: 1 and newest (and asset) from its release list */
static int ask_github(void)
{
    const char* url = SDL_getenv("RECVX_UPDATE_GITHUB_TEST");
#ifdef _WIN32
    const char* dir = SDL_getenv("TEMP");
#else
    const char* dir = SDL_getenv("TMPDIR");
#endif
    char file[1100];
    char* body = NULL;
    size_t len = 0;
    int ok;
    if (!url) url = RECVX_UPDATE_GITHUB;
    cj.allow_file = !strncmp(url, "file:///", 8);
    if (!brdl_url_ok(url, cj.allow_file)) { host_log("update: cannot use %s\n", url); return 0; }
    if (!dir || !*dir) dir = SDL_getenv("TMP");
#ifndef _WIN32
    if (!dir || !*dir) dir = "/tmp";
#endif
    if (!dir) return 0;
    /* the answer in a file of the system's temporary folder (the game's folder may be read-only), one per process */
    SDL_snprintf(file, sizeof(file), "%s/recvx-update-%llu.json", dir, (unsigned long long)SDL_GetCurrentThreadID());
    if (brdl_fetch(&cj, url, file, API_MAX, 1)) body = SDL_LoadFile(file, &len);
    SDL_RemovePath(file);
    if (!body) return 0;   /* (brdl_fetch logged why) */
    ok = upd_pick_release(body, (int)SDL_min(len, API_MAX), UPD_PLATFORM, UPD_TAIL, cj.allow_file, newest, sizeof(newest), &asset);
    if (!ok) host_log("update: GitHub's answer is not a release list%s\n", strstr(body, "rate limit") ? " (rate limit)" : "");
    else if (!asset.url[0]) host_log("update: release %s has no usable recvx-enhanced-port-%s" UPD_PLATFORM UPD_TAIL "\n", newest, newest + 1);
    SDL_free(body);
    return ok;
}

/* one request to the project's server: 1 and newest */
static int ask_server(void)
{
    const char* url = SDL_getenv("RECVX_UPDATE_URL_TEST");
    char host[128], hp[140], path[256], req[512], buf[4096];
    const char* why = NULL;
    NetAddr a;
    int port, len, n;
    if (!url) url = RECVX_UPDATE_URL;
    if (!upd_parse_url(url, host, sizeof(host), &port, path, sizeof(path))) {
        host_log("update: cannot use %s (only http://host[:port]/path)\n", url);
        return 0;
    }
    snprintf(hp, sizeof(hp), port == 80 ? "%s" : "%s:%d", host, port);
    len = snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: recvx-enhanced-port/%s\r\n\r\n", path, hp, update_installed() + 1);
    asset.url[0] = 0;
    if (!net_resolve(host, port, &a)) why = "cannot resolve the server's name";
    else if ((n = net_tcp_fetch(&a, req, len, buf, sizeof(buf), 5000, 10000, &why)) >= 0 && !upd_parse_response(buf, n, newest, sizeof(newest)))
        why = "the answer is not a version number";
    if (why) host_log("update: %s\n", why);
    return !why;
}

/* up to 3 attempts, 0 / 5 / 30 s apart */
static int tries(int (*ask)(void), const char* who)
{
    static const int wait_s[3] = { 0, 5, 30 };
    for (int i = 0; i < 3; i++) {
        SDL_Delay(wait_s[i] * 1000);
        if (!g_config.update_check) return 0;   /* turned off meanwhile */
        if (ask()) {
            host_log("update: newest version %s from %s (installed %s)\n", newest, who, update_installed());
            return 1;
        }
        host_log("update: %s, attempt %d of 3 failed\n", who, i + 1);
    }
    return 0;
}

static int SDLCALL check(void* ud)
{
    int ok;
    (void)ud;
    crash_thread_init();
    ok = tries(ask_github, "GitHub") || tries(ask_server, "the update server");
    if (!ok) host_log("update: no answer; not asked again until the next start\n");
    SDL_SetAtomicInt(&state, ok ? UPD_KNOWN : UPD_UNKNOWN);
    return 0;
}

static int set_paths(void);
static int restore_missing(void);
static char tmp[1100];   /* <game folder>recvx-update: the download, the unpacked archive, the old files */

void update_start(void)
{
    static int once;
    SDL_Thread* t;
    /* the last install's folder: its old files (Windows: the old executable, which ran then).
       ponytail: a second copy of the game started from the same folder during an install would delete it under it;
       a lock file if two copies in one folder ever install at once. */
    if (!once++ && set_paths() && SDL_GetPathInfo(tmp, NULL) && restore_missing()) {
        host_log("update: deleting %s\n", tmp);
        brdl_rm_tree(tmp);
    }
    if (!g_config.update_check || env_off() || SDL_getenv("RECVX_UPDATE_FAKE") || !SDL_CompareAndSwapAtomicInt(&started, 0, 1)) return;
    SDL_SetAtomicInt(&state, UPD_CHECKING);
    if ((t = SDL_CreateThread(check, "update", NULL))) SDL_DetachThread(t);   /* detached: exit does not wait for it */
    else SDL_SetAtomicInt(&state, UPD_UNKNOWN);
}

int update_newest(char* out, int cap)
{
    const char* fake = SDL_getenv("RECVX_UPDATE_FAKE");
    int s;
    if (fake) { SDL_strlcpy(out, fake, cap); return UPD_KNOWN; }
    if (!g_config.update_check || env_off()) return UPD_OFF;
    if ((s = SDL_GetAtomicInt(&state)) == UPD_KNOWN) SDL_strlcpy(out, newest, cap);   /* (SDL's atomics are full barriers) */
    return s;
}

int update_offer(char* ver, int cap)
{
    static int offered;
    if (offered || g_config.update_check != UPD_SET_ASK || env_off() || SDL_GetAtomicInt(&state) != UPD_KNOWN || !asset.url[0] ||
        !upd_newer(newest, update_installed()) || (g_update_skip[0] && !upd_newer(newest, g_update_skip)))
        return 0;
    offered = 1;
    SDL_strlcpy(ver, newest, cap);
    host_log("update: offering %s\n", newest);
    return 1;
}

const char* update_page(void)
{
    static char p[128];
    SDL_snprintf(p, sizeof(p), RECVX_UPDATE_PAGE "%s", newest);
    return p;
}

static void SDLCALL open_page(void* ud)
{
    (void)ud;
    if (!SDL_OpenURL(update_page())) host_log("update: cannot open %s (%s)\n", update_page(), SDL_GetError());
}
void update_open_page(void)
{
    host_log("update: opening %s\n", update_page());
    SDL_RunOnMainThread(open_page, NULL, false);   /* (SDL_OpenURL is the main thread's; it runs at its next event poll) */
}

/* ---------------------------------------------------------------- install */
static BrdlJob ij = { .log = "update", .too_big = "Download is larger than expected. Try again later." };
static SDL_AtomicInt ist, ibusy;   /* BRDL_* */
static char base[1024];            /* the executable's folder, with a trailing separator */
static char exe[1100];             /* the running executable (whatever its name) */
static char top[1200], arc[1200];  /* the archive's folder, unpacked; the archive */
typedef struct { char rel[200]; int had, placed; } UpdFile;   /* had: the old file is in tmp/old (2: hard-linked there) */
static UpdFile* files;
static int nfiles;

static int set_paths(void)
{
    char* s;
    if (exe[0]) return 1;
    if (!brdl_exe_path(exe, sizeof(exe))) return exe[0] = 0;
    if (!(s = strrchr(exe, '\\'))) s = strrchr(exe, '/');
    if (!s || s - exe + 1 >= (int)sizeof(base)) return exe[0] = 0;
    SDL_snprintf(base, sizeof(base), "%.*s", (int)(s - exe + 1), exe);
    SDL_snprintf(tmp, sizeof(tmp), "%srecvx-update", base);
    return 1;
}

/* f's place in the archive, in the game's folder (the executable: the running one's path) and in tmp/old */
static void where(const UpdFile* f, char* src, char* dst, char* bak)
{
    SDL_snprintf(src, 1200, "%s/%s", top, f->rel);
    if (!strcmp(f->rel, UPD_EXE)) SDL_strlcpy(dst, exe, 1200);
    else SDL_snprintf(dst, 1200, "%s%s", base, f->rel);
    SDL_snprintf(bak, 1200, "%s/old/%s", tmp, f->rel);
}

/* the folder path is in (its last part cut off) */
static void make_parent(const char* path)
{
    char d[1200];
    char* s;
    SDL_strlcpy(d, path, sizeof(d));
    if ((s = strrchr(d, '/')) || (s = strrchr(d, '\\'))) { *s = 0; SDL_CreateDirectory(d); }   /* (our parts are '/'-joined) */
}

/* the files of the archive's folder dir (rel below it) that are installed (upd_installable): into files[] */
static int collect(const char* dir, const char* rel, int depth)
{
    int n = 0, ok = 1;
    char** names = SDL_GlobDirectory(dir, NULL, 0, &n);
    if (!names) return brdl_fail(&ij, "Cannot read the download. Try downloading again.");
    for (int k = 0; k < n && ok; k++) {
        char p[1200], r[200];
        SDL_PathInfo i;
        if (brdl_nested(names[k])) continue;   /* (each folder is looked at here, links not followed) */
        if (SDL_snprintf(p, sizeof(p), "%s/%s", dir, names[k]) >= (int)sizeof(p) ||
            SDL_snprintf(r, sizeof(r), "%s%s%s", rel, *rel ? "/" : "", names[k]) >= (int)sizeof(r) || brdl_is_link(p) || !SDL_GetPathInfo(p, &i)) {
            host_log("update: not installed: %s\n", p);
            continue;
        }
        if (i.type == SDL_PATHTYPE_DIRECTORY) ok = depth < 8 ? collect(p, r, depth + 1) : brdl_fail(&ij, "The download is not a game release.");
        else if (i.type != SDL_PATHTYPE_FILE || !upd_installable(r)) host_log("update: not installed: %s\n", r);
        else if (nfiles == MAX_FILES) ok = brdl_fail(&ij, "The download is not a game release.");
        else SDL_strlcpy(files[nfiles++].rel, r, sizeof(files[0].rel));
    }
    SDL_free(names);
    return ok;
}

/* the old file out of the way: hard-linked into tmp/old (Linux: the new one then replaces it in one rename, so the
   path is never missing), else moved there (Windows: a running .exe can be renamed but not overwritten). 0 failed */
static int backup(const char* dst, const char* bak)
{
    make_parent(bak);
#ifndef _WIN32
    if (!link(dst, bak)) return 2;
#endif
    return brdl_move(&ij, dst, bak);
}

/* the installed files back as they were, newest first */
static int lost;   /* an old file could not be put back: tmp stays (the next start tries again, restore_missing) */
static void roll_back(void)
{
    char src[1200], dst[1200], bak[1200];
    for (int i = nfiles; i-- > 0;) {
        UpdFile* f = &files[i];
        where(f, src, dst, bak);
        if (f->had && !brdl_move(&ij, bak, dst)) {
            host_log("update: cannot restore %s; the old file is %s\n", dst, bak);
            lost = 1;
        } else if (!f->had && f->placed) SDL_RemovePath(dst);
        else if (f->had) host_log("update: restored %s\n", dst);
        f->had = f->placed = 0;
    }
}

/* at start, before tmp goes: an old file whose place is empty (the last install stopped half-way: a crash, a failed
   roll_back) back into it. 1 when nothing is left to restore */
static int restore_missing(void)
{
    char src[1200], dst[1200], bak[1200];
    int ok = 1;
    SDL_snprintf(top, sizeof(top), "%s/old", tmp);
    if (!SDL_GetPathInfo(top, NULL)) return 1;
    if (!(files = SDL_calloc(MAX_FILES, sizeof(*files)))) return 0;
    nfiles = 0;
    collect(top, "", 0);
    for (int i = 0; i < nfiles; i++) {
        where(&files[i], src, dst, bak);
        if (SDL_GetPathInfo(dst, NULL)) continue;
        if (brdl_move(&ij, bak, dst)) host_log("update: restored %s\n", dst);
        else ok = 0;
    }
    SDL_free(files);
    files = NULL;
    return ok;
}

/* every file into its place (the old one into tmp/old first); on a failure all back as they were. 0 failed */
static int put(void)
{
    char src[1200], dst[1200], bak[1200], why[200];
    for (int i = 0; i < nfiles; i++) {
        UpdFile* f = &files[i];
        where(f, src, dst, bak);
        if (SDL_GetPathInfo(dst, NULL) && !(f->had = backup(dst, bak))) goto fail;
        make_parent(dst);
        if (!brdl_move(&ij, src, dst)) goto fail;
        f->placed = 1;
    }
#ifndef _WIN32
    chmod(exe, 0755);
#endif
    return 1;
fail:
    SDL_strlcpy(why, SDL_GetError(), sizeof(why));
    roll_back();
    host_log("update: cannot replace %s (%s); the old files were put back\n", dst, why);
    return brdl_fail(&ij, "Cannot replace %s. Your current version was kept.", strncmp(dst, base, strlen(base)) ? dst : dst + strlen(base));   /* (in the game folder: its name there) */
}

static int install(void)
{
    char x[1200], hex[65], name[160];
    SDL_PathInfo info;
    if (!set_paths()) return brdl_fail(&ij, "Cannot find the game folder.");
    SDL_snprintf(name, sizeof(name), "recvx-enhanced-port-%s" UPD_PLATFORM, newest + 1);
    SDL_snprintf(x, sizeof(x), "%s/x", tmp);
    SDL_snprintf(arc, sizeof(arc), "%s/%s" UPD_TAIL, tmp, name);
    SDL_snprintf(top, sizeof(top), "%s/%s", x, name);
    brdl_rm_tree(tmp);   /* a left-over */
    if (!SDL_CreateDirectory(x)) {
        host_log("update: cannot create %s (%s)\n", x, SDL_GetError());
        return brdl_fail(&ij, "The game folder %s is not writable.", base);
    }
    /* 1. the archive, its size and digest as GitHub lists them */
    SDL_SetAtomicInt(&ist, BRDL_DOWNLOAD);
    if (!brdl_fetch(&ij, asset.url, arc, asset.size, 0)) return 0;
    if (!SDL_GetPathInfo(arc, &info) || info.size != (Uint64)asset.size) return brdl_fail(&ij, "Download incomplete. Try again.");
    __atomic_store_n(&ij.done, asset.size, __ATOMIC_RELEASE);
    SDL_SetAtomicInt(&ist, BRDL_VERIFY);
    if (!brdl_sha256_file(&ij, arc, hex)) return brdl_fail(&ij, __atomic_load_n(&ij.cancel, __ATOMIC_ACQUIRE) ? "cancelled" : "Cannot read the download. Try downloading again.");
    if (strcmp(hex, asset.digest + 7)) {
        host_log("update: SHA-256 %s, expected %s\n", hex, asset.digest + 7);
        return brdl_fail(&ij, "Download verification failed. Nothing was installed. Try again.");
    }
    /* 2. unpacked, its files listed */
    SDL_SetAtomicInt(&ist, BRDL_EXTRACT);
    SDL_snprintf(name + strlen(name), sizeof(name) - strlen(name), UPD_TAIL);
    if (!brdl_extract(&ij, tmp, name)) return 0;
    SDL_snprintf(x, sizeof(x), "%s/" UPD_EXE, top);
    if (!brdl_is_file(x)) return brdl_fail(&ij, "The download is not a game release.");
    nfiles = 0;
    if (!collect(top, "", 0)) return 0;
    if (__atomic_load_n(&ij.cancel, __ATOMIC_ACQUIRE)) return brdl_fail(&ij, "cancelled");
    /* 3. in place */
    if (!put()) return 0;
    for (int i = 0; i < nfiles; i++) host_log("update: installed %s\n", files[i].rel);
    host_log("update: %s installed into %s (the old files in %s until the next start)\n", newest, base, tmp);
    return BRDL_INSTALLED;
}

static int SDLCALL install_thread(void* ud)
{
    char d[1200];
    int s;
    (void)ud;
    crash_thread_init();
    files = SDL_calloc(MAX_FILES, sizeof(*files));
    s = files ? install() : brdl_fail(&ij, "Out of memory.");
    if (!s) s = __atomic_load_n(&ij.cancel, __ATOMIC_ACQUIRE) ? BRDL_CANCELLED : BRDL_ERROR;
    SDL_free(files);
    files = NULL;
    SDL_snprintf(d, sizeof(d), "%s/x", tmp);   /* the download goes; the old files stay until the next start */
    if (tmp[0]) { brdl_rm_tree(d); SDL_RemovePath(arc); }
    if (s != BRDL_INSTALLED && tmp[0] && !lost) brdl_rm_tree(tmp);
    SDL_SetAtomicInt(&ist, s);
    SDL_SetAtomicInt(&ibusy, 0);
    return 0;
}

int update_install(void)
{
    SDL_Thread* t;
    if (!asset.url[0] || !SDL_CompareAndSwapAtomicInt(&ibusy, 0, 1)) return 0;
    ij.allow_file = cj.allow_file;
    ij.error[0] = 0;
    __atomic_store_n(&ij.cancel, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&ij.done, 0, __ATOMIC_RELEASE);
    SDL_SetAtomicInt(&ist, BRDL_API);
    host_log("update: installing %s from %s (%d bytes, %s)\n", newest, asset.url, asset.size, asset.digest);
    if ((t = SDL_CreateThread(install_thread, "update-install", NULL))) { SDL_DetachThread(t); return 1; }
    brdl_fail(&ij, "Cannot start the download. Try restarting the game.");
    SDL_SetAtomicInt(&ist, BRDL_ERROR);
    SDL_SetAtomicInt(&ibusy, 0);
    return 1;
}

void update_install_cancel(void) { __atomic_store_n(&ij.cancel, 1, __ATOMIC_RELEASE); }

void update_install_status(BrdlStatus* s)
{
    memset(s, 0, sizeof(*s));
    s->state = SDL_GetAtomicInt(&ist);
    s->done = __atomic_load_n(&ij.done, __ATOMIC_ACQUIRE);
    s->total = asset.size;
    if (s->state == BRDL_ERROR) SDL_strlcpy(s->error, ij.error, sizeof(s->error));
}

/* ---------------------------------------------------------------- restart */
static SDL_AtomicInt restart;

void update_restart(void)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_QUIT;
    SDL_SetAtomicInt(&restart, 1);
    SDL_PushEvent(&e);
}

void update_relaunch(int argc, char** argv, const char* cwd)
{
    const char* args[64];
    SDL_PropertiesID props;
    SDL_Process* pr;
    int n = 0;
    if (!SDL_GetAtomicInt(&restart) || !exe[0]) return;
    config_save();   /* (the new process reads it) */
    args[n++] = exe;
    for (int i = 1; i < argc && n < 63; i++) args[n++] = argv[i];
    args[n] = NULL;
    props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, (void*)args);
    if (cwd && *cwd) SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, cwd);   /* (relative paths as before) */
    pr = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (pr) host_log("update: restarted as %s\n", exe);
    else host_log("update: cannot start %s (%s)\n", exe, SDL_GetError());
}
