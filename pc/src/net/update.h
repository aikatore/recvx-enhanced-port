/* Update check and install (docs/USER_GUIDE.md "Updating", docs/ARCHITECTURE.md "Update check"): once per start a
   background thread asks GitHub's API for the project's releases (one HTTPS GET, up to 3 tries 0 / 5 / 30 s apart)
   and, if that fails, the project's update server for the newest version number (one plain HTTP GET, as often);
   nothing else is sent, nothing is kept. The title shows the answer below the installed version (lobby_glue.c
   title_version); with "Ask to Install" and GitHub's answer, the title offers that release's archive for this build
   (update_menu.c), which update_install downloads, checks against GitHub's SHA-256 and installs over the program's
   files in the executable's folder. The pure parts below are checked by pc/tests/test_update.c. */
#ifndef RECVX_UPDATE_H
#define RECVX_UPDATE_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "biorand_dl.h"   /* the release JSON reader (brj_*, brdl_find_asset) */

/* The project's update server, http:// only. It will become a domain name: the host is resolved
   (getaddrinfo), so a move changes only this line. Test aid: RECVX_UPDATE_URL_TEST replaces it. */
#define RECVX_UPDATE_URL "http://85.214.181.87:1979/"

/* GitHub's release list, newest first (pre-releases too; drafts are not listed without a login); 5 of them, so a
   later fix release of an older line does not hide the newest. Test aid: RECVX_UPDATE_GITHUB_TEST replaces it (a
   file:/// URL there also allows file:// for the archive, Linux curl only). */
#define RECVX_UPDATE_GITHUB "https://api.github.com/repos/aikatore/recvx-enhanced-port/releases?per_page=5"
#define RECVX_UPDATE_PAGE "https://github.com/aikatore/recvx-enhanced-port/releases/tag/"   /* + the tag: the browser fallback */
/* this build's archive: recvx-enhanced-port-<version><UPD_TAIL>, one folder of that name (without the extension) with
   UPD_EXE in it; the -debug archives and the server's never match */
#ifdef _WIN32
#define UPD_TAIL ".zip"
#define UPD_PLATFORM "-windows-x86"
#define UPD_EXE "recvx.exe"
#else
#define UPD_TAIL ".tar.gz"
#define UPD_PLATFORM "-linux-i686"
#define UPD_EXE "recvx"
#endif

enum { UPD_OFF, UPD_CHECKING, UPD_UNKNOWN, UPD_KNOWN };
enum { UPD_SET_OFF, UPD_SET_ASK, UPD_SET_NOTIFY };   /* g_config.update_check (ini update_check: the old 1, "On", is Ask) */
extern char g_update_skip[32];              /* ini update_skip: SKIP THIS VERSION's version ("" none) */
void update_start(void);                    /* main thread: at start, and the setting's applier (one check per run) */
int  update_newest(char* out, int cap);     /* any thread: UPD_*; UPD_KNOWN fills out ("v0.7.0") */
const char* update_installed(void);         /* "v0.6.9": RECVX_VERSION's release part (test aid: RECVX_UPDATE_FAKE_VERSION) */
/* game thread, at the title menu: 1 once per run when the dialog should ask (Ask to Install, a newer release on
   GitHub with this build's archive, not skipped); its version in ver */
int  update_offer(char* ver, int cap);
int  update_install(void);                  /* starts the download and install of the offered release; 0 if one runs */
void update_install_cancel(void);
void update_install_status(BrdlStatus* s);  /* BRDL_API (starting), _DOWNLOAD, _VERIFY, _EXTRACT, then _INSTALLED, _ERROR, _CANCELLED */
const char* update_page(void);              /* the offered release's page (the browser fallback) */
void update_open_page(void);                /* any thread: that page in the browser (on the main thread) */
void update_restart(void);                  /* any thread: quit, and start the installed executable again (host.c) */
void update_relaunch(int argc, char** argv, const char* cwd);   /* main thread, at quit: does it if asked */

/* "http://host[:port][/path]": host (letters, digits, '.', '-'), port (80) and path ("/"); 0 if malformed or not http:// */
static inline int upd_parse_url(const char* url, char* host, int hcap, int* port, char* path, int pcap)
{
    const char* h = url + 7;
    size_t n = strcspn(h, ":/"), i;
    if (strncmp(url, "http://", 7) || !n || n >= (size_t)hcap) return 0;
    for (i = 0; i < n; i++)
        if (!((h[i] >= 'a' && h[i] <= 'z') || (h[i] >= 'A' && h[i] <= 'Z') || (h[i] >= '0' && h[i] <= '9') || h[i] == '.' || h[i] == '-')) return 0;
    memcpy(host, h, n);
    host[n] = 0;
    *port = 80;
    h += n;
    if (*h == ':') {
        long v = 0;
        for (h++, n = 0; *h >= '0' && *h <= '9' && n < 6; h++, n++) v = v * 10 + (*h - '0');
        if (!n || v < 1 || v > 65535 || (*h && *h != '/')) return 0;
        *port = (int)v;
    }
    for (i = 0; h[i]; i++) if (h[i] <= ' ' || h[i] >= 127) return 0;   /* (it goes into the request line) */
    return snprintf(path, pcap, "%s", *h ? h : "/") < pcap;
}

/* "v<digits>.<digits>.<digits>", optionally "-..." or "+..." of up to 15 letters, digits, '.', '-', '+'
   (e.g. v0.7.0-rc1): 1 and the three numbers */
static inline int upd_parse_version(const char* s, unsigned n[3])
{
    int i, d;
    if (*s++ != 'v') return 0;
    for (i = 0; i < 3; i++) {
        if (i && *s++ != '.') return 0;
        for (n[i] = 0, d = 0; *s >= '0' && *s <= '9'; s++, d++) {
            if (d == 9) return 0;
            n[i] = n[i] * 10 + (unsigned)(*s - '0');
        }
        if (!d) return 0;
    }
    if (*s && *s != '-' && *s != '+') return 0;
    for (i = 0; s[i]; i++)
        if (i >= 16 || !((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= '0' && s[i] <= '9') || strchr(".-+", s[i])))
            return 0;
    return 1;
}

/* newest is a later release than installed (part by part, numerically: v0.6.10 > v0.6.9; a suffix
   does not count); 0 when either is not a version */
static inline int upd_newer(const char* newest, const char* installed)
{
    unsigned a[3], b[3];
    int i;
    if (!upd_parse_version(newest, a) || !upd_parse_version(installed, b)) return 0;
    for (i = 0; i < 3; i++) if (a[i] != b[i]) return a[i] > b[i];
    return 0;
}

/* an HTTP response (len bytes): status 200 and a body that is, trimmed, a version
   (upd_parse_version): 1 and the version in ver; else 0 (an error page, junk, a cut answer) */
static inline int upd_parse_response(const char* r, int len, char* ver, int cap)
{
    unsigned v[3];
    const char *e = r + len, *b = NULL, *p;
    if (len < 12 || strncmp(r, "HTTP/1.", 7) || r[8] != ' ' || strncmp(r + 9, "200", 3) || (len > 12 && r[12] != ' ' && r[12] != '\r' && r[12] != '\n')) return 0;
    for (p = r; p < e - 1 && !b; p++)
        if (p[0] == '\n' && p[1] == '\n') b = p + 2;
        else if (p[0] == '\n' && p[1] == '\r' && p + 2 < e && p[2] == '\n') b = p + 3;
    if (!b) return 0;
    while (b < e && (*b == ' ' || *b == '\t' || *b == '\r' || *b == '\n')) b++;
    while (e > b && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) e--;
    if (e - b >= cap || memchr(b, 0, (size_t)(e - b))) return 0;
    memcpy(ver, b, (size_t)(e - b));
    ver[e - b] = 0;
    if (upd_parse_version(ver, v)) return 1;
    ver[0] = 0;
    return 0;
}

/* GitHub's release list (len bytes, a JSON array): the highest version among the releases that are not drafts and
   whose tag_name is a version, into ver; that release's asset `recvx-enhanced-port-<ver without v><platform><tail>`
   into a (a->url "" when it has none or it is not usable: brdl_find_asset). 0 when it is not such a list (an error
   object such as the rate limit's, junk, a cut answer) or holds no such release. */
static inline int upd_pick_release(const char* json, int len, const char* platform, const char* tail, int allow_file,
                                   char* ver, int cap, BrdlAsset* a)
{
    const char *e = json + len, *p = brj_ws(json, e), *best = NULL, *best_e = NULL;
    char tag[32], name[128], msg[8];
    unsigned n[3];
    a->url[0] = 0;
    if (len <= 0 || p >= e || *p != '[' || !brj_skip(p, e, 0)) return 0;
    for (p = brj_ws(p + 1, e); p < e && *p == '{';) {
        const char* q = brj_skip(p, e, 1);
        const char* d = brj_get(p, e, "draft");
        if (!q) return 0;
        if (d && !strncmp(d, "false", 5) && brj_plain(brj_get(p, e, "tag_name"), e, tag, sizeof(tag)) > 0 &&
            upd_parse_version(tag, n) && (int)strlen(tag) < cap && (!best || upd_newer(tag, ver))) {
            strcpy(ver, tag);
            best = p;
            best_e = q;
        }
        p = brj_ws(q, e);
        if (p >= e || *p != ',') break;
        p = brj_ws(p + 1, e);
    }
    if (!best) return 0;
    snprintf(name, sizeof(name), "recvx-enhanced-port-%s%s%s", ver + 1, platform, tail);
    if (brdl_find_asset(best, (int)(best_e - best), name, allow_file, a, msg, sizeof(msg)) != BRJ_OK) a->url[0] = 0;
    return 1;
}

/* a file of the archive (rel: its path below the archive's folder, '/'-separated) the installer may put into the
   game's folder: a file at the top (but no recvx.ini), anything below docs/, a README.txt below mods/ (never the
   player's mods); never save/, never "." / ".." / an empty part, a '\\' or a ':' (a path out of the folder) */
static inline int upd_installable(const char* rel)
{
    const char *p, *last = rel;
    if (!*rel || strchr(rel, '\\') || strchr(rel, ':')) return 0;
    for (p = rel;; p++)
        if (!*p || *p == '/') {
            size_t n = (size_t)(p - last);
            if (!n || (n == 1 && last[0] == '.') || (n == 2 && !strncmp(last, "..", 2))) return 0;
            if (!*p) break;
            last = p + 1;
        }
    if (!strchr(rel, '/')) return strcmp(rel, "recvx.ini") != 0;
    if (!strncmp(rel, "docs/", 5)) return 1;
    return !strncmp(rel, "mods/", 5) && !strcmp(last, "README.txt");
}

#endif
