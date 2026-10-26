/* The BioRand CLI download (docs/BIORAND_INGAME.md, S2): asks GitHub's API for the nightly release of
   biorand/classic, downloads this platform's archive over HTTPS (Windows: WinHTTP; Linux: a curl
   child process, else wget), checks its size and SHA-256 against the API's digest, unpacks it with the
   system's tar (no shell) and puts biorand-cli[.exe] and biorand-cli.LICENSE next to the game's
   executable. Everything runs on one background thread; the UI polls biorand_cli_status().
   It writes only into the game's folder (SDL_GetBasePath): the two files, and the temporary folder
   biorand-dl/ (the API answer, the archive, the unpacked files), which it deletes afterwards.
   The pure parts below are checked by pc/tests/test_biorand_dl.c.
   Test aids: RECVX_BIORAND_API_TEST replaces the API URL; a file:// URL there also allows file://
   for the asset (Linux curl only). RECVX_BIORAND_CLI_DIR_TEST replaces the game's folder. */
#ifndef RECVX_BIORAND_DL_H
#define RECVX_BIORAND_DL_H
#include <string.h>

#define BRDL_API_URL "https://api.github.com/repos/biorand/classic/releases/tags/nightly"
#ifdef _WIN32
#define BRDL_ASSET "biorand-cli-win-x64.zip"
#define BRDL_EXE "biorand-cli.exe"
#else
#define BRDL_ASSET "biorand-cli-linux-x64.tar.gz"
#define BRDL_EXE "biorand-cli"
#endif
#define BRDL_MAX_SIZE (100 << 20)   /* a bigger archive is refused (the nightly is about 8 MB) */
#define BRDL_DIGEST_CAP 72          /* "sha256:" + 64 hex + NUL */

enum {
    BRDL_IDLE,                                                   /* nothing started yet */
    BRDL_API, BRDL_DOWNLOAD, BRDL_VERIFY, BRDL_EXTRACT,          /* busy (DOWNLOAD: done / total bytes) */
    BRDL_INSTALLED,                                              /* install: done */
    BRDL_CURRENT, BRDL_NEWER,                                    /* check: the installed CLI is the newest / a newer one (or none) exists */
    BRDL_ERROR, BRDL_CANCELLED,                                  /* error: the text */
};
typedef struct { int state, done, total; char error[160]; } BrdlStatus;

extern char g_biorand_cli_digest[BRDL_DIGEST_CAP];   /* ini biorand_cli_digest: the installed archive's digest, "" unknown */
const char* biorand_cli_exe(void);   /* <game folder>/biorand-cli[.exe], whether or not it is there; NULL if the folder is unknown */
int  biorand_cli_present(void);      /* that file is there */
int  biorand_cli_check_update(void); /* starts the thread: the API only, then BRDL_CURRENT or BRDL_NEWER; 0 if one runs already */
int  biorand_cli_install(void);      /* starts the thread: API, download, verify, extract, install, then BRDL_INSTALLED; 0 if one runs */
void biorand_cli_cancel(void);       /* the running thread stops (kills curl/wget/tar) and ends BRDL_CANCELLED */
void biorand_cli_status(BrdlStatus* s);   /* any thread; error only in BRDL_ERROR */

/* https://<host>/... with host api.github.com, github.com or <name>.githubusercontent.com (lower case,
   no port, no user), and a path of printable ASCII without '\\' or '"'. allow_file (the test aid): file:///... too. */
static inline int brdl_url_ok(const char* u, int allow_file)
{
    static const char gh[] = ".githubusercontent.com";
    size_t n, i, g = sizeof(gh) - 1;
    if (allow_file && !strncmp(u, "file:///", 8)) u += 7;
    else {
        if (strncmp(u, "https://", 8)) return 0;
        n = strcspn(u += 8, "/");
        for (i = 0; i < n; i++)
            if (!((u[i] >= 'a' && u[i] <= 'z') || (u[i] >= '0' && u[i] <= '9') || u[i] == '.' || u[i] == '-')) return 0;
        if (!((n == 14 && !strncmp(u, "api.github.com", n)) || (n == 10 && !strncmp(u, "github.com", n)) ||
              (n > g && !strncmp(u + n - g, gh, g) && u[n - g - 1] != '.' && u[0] != '.')))
            return 0;
        u += n;
    }
    for (; *u; u++)
        if (*u <= ' ' || *u >= 127 || *u == '\\' || *u == '"') return 0;
    return 1;
}

/* a minimal JSON reader for the API's answer, bounded by e and 16 levels of nesting */
static inline const char* brj_ws(const char* p, const char* e)
{
    while (p < e && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    return p;
}
static inline const char* brj_str(const char* p, const char* e)   /* p at '"': after the closing '"', or NULL */
{
    for (p++; p < e; p++)
        if (*p == '\\') p++;
        else if (*p == '"') return p + 1;
        else if ((unsigned char)*p < 0x20) return NULL;
    return NULL;
}
static inline const char* brj_skip(const char* p, const char* e, int depth)   /* after the value at p, or NULL */
{
    const char* s;
    p = brj_ws(p, e);
    if (p >= e) return NULL;
    if (*p == '"') return brj_str(p, e);
    if (*p == '{' || *p == '[') {
        char close = *p == '{' ? '}' : ']';
        if (depth >= 16) return NULL;
        p = brj_ws(p + 1, e);
        if (p < e && *p == close) return p + 1;
        for (;;) {
            if (close == '}') {
                if (p >= e || *p != '"' || !(p = brj_str(p, e))) return NULL;
                p = brj_ws(p, e);
                if (p >= e || *p++ != ':') return NULL;
            }
            if (!(p = brj_skip(p, e, depth + 1))) return NULL;
            p = brj_ws(p, e);
            if (p >= e) return NULL;
            if (*p == close) return p + 1;
            if (*p++ != ',') return NULL;
            p = brj_ws(p, e);
        }
    }
    for (s = p; p < e && ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'z') || (*p && strchr("-+.E", *p))); p++) {}
    return p > s ? p : NULL;   /* a number, true, false, null */
}
/* the value of key at the top level of the object at o (after whitespace), or NULL; keys compared as written */
static inline const char* brj_get(const char* o, const char* e, const char* key)
{
    size_t k = strlen(key);
    o = brj_ws(o, e);
    if (o >= e || *o != '{' || !brj_skip(o, e, 0)) return NULL;
    for (o = brj_ws(o + 1, e); o < e && *o == '"';) {
        const char* ke = brj_str(o, e);
        const char* v = brj_ws(brj_ws(ke, e) + 1, e);
        if ((size_t)(ke - o) == k + 2 && !strncmp(o + 1, key, k)) return v;
        o = brj_ws(brj_skip(v, e, 1), e);
        if (o >= e || *o != ',') break;
        o = brj_ws(o + 1, e);
    }
    return NULL;
}
/* the string at v (no escapes in it) into out: its length, or -1 if not a plain string under cap */
static inline int brj_plain(const char* v, const char* e, char* out, int cap)
{
    const char* s = v ? brj_str(v, e) : NULL;
    int n;
    if (!s || *v != '"' || memchr(v + 1, '\\', (size_t)(s - v - 2)) || (n = (int)(s - v - 2)) >= cap) return -1;
    memcpy(out, v + 1, (size_t)n);
    out[n] = 0;
    return n;
}

typedef struct { char url[512]; char digest[BRDL_DIGEST_CAP]; int size; } BrdlAsset;
enum { BRJ_OK, BRJ_MALFORMED, BRJ_MESSAGE, BRJ_NO_ASSET, BRJ_BAD_SIZE, BRJ_BAD_DIGEST, BRJ_BAD_URL };

/* the release JSON (len bytes): the asset `name` of assets[] with its size (1..BRDL_MAX_SIZE), digest
   ("sha256:" + 64 lower-case hex) and browser_download_url (brdl_url_ok). BRJ_MESSAGE: GitHub's error
   answer (no assets, a "message": rate limit, not found), its text (printable ASCII) in msg. */
static inline int brdl_find_asset(const char* json, int len, const char* name, int allow_file, BrdlAsset* a, char* msg, int mcap)
{
    const char *e = json + len, *p, *v;
    char nm[128];
    int i;
    msg[0] = 0;
    if (len <= 0 || !(p = brj_skip(json, e, 0)) || brj_ws(p, e) != e || *brj_ws(json, e) != '{') return BRJ_MALFORMED;
    if (!(p = brj_get(json, e, "assets")) || *p != '[') {
        if (!(v = brj_get(json, e, "message")) || *v != '"') return BRJ_MALFORMED;
        for (p = v + 1, i = 0; p < e && *p != '"' && i < mcap - 1; p++)
            if (*p == '\\') { if (++p < e && *p == 'n') msg[i++] = ' '; else if (p < e && *p == '"') msg[i++] = '"'; }
            else if (*p >= ' ' && *p < 127) msg[i++] = *p;
        msg[i] = 0;
        return BRJ_MESSAGE;
    }
    for (p = brj_ws(p + 1, e); p < e && *p == '{'; p = brj_ws(p + 1, e)) {
        if (brj_plain(brj_get(p, e, "name"), e, nm, sizeof(nm)) >= 0 && !strcmp(nm, name)) {
            long sz = 0;
            v = brj_get(p, e, "size");
            for (i = 0; v && v + i < e && v[i] >= '0' && v[i] <= '9' && i < 10; i++) sz = sz * 10 + (v[i] - '0');
            if (!v || !i || i == 10 || (v + i < e && ((v[i] >= '0' && v[i] <= '9') || v[i] == '.' || v[i] == 'e' || v[i] == 'E')) || sz < 1 || sz > BRDL_MAX_SIZE) return BRJ_BAD_SIZE;
            a->size = (int)sz;
            if (brj_plain(brj_get(p, e, "digest"), e, a->digest, sizeof(a->digest)) != 71 || strncmp(a->digest, "sha256:", 7)) return BRJ_BAD_DIGEST;
            for (i = 7; i < 71; i++)
                if (!((a->digest[i] >= '0' && a->digest[i] <= '9') || (a->digest[i] >= 'a' && a->digest[i] <= 'f'))) return BRJ_BAD_DIGEST;
            if (brj_plain(brj_get(p, e, "browser_download_url"), e, a->url, sizeof(a->url)) < 0 || !brdl_url_ok(a->url, allow_file)) return BRJ_BAD_URL;
            return BRJ_OK;
        }
        p = brj_ws(brj_skip(p, e, 1), e);
        if (p >= e || *p != ',') break;
    }
    return BRJ_NO_ASSET;
}

#endif
