/* BioRand CLI download (pc/src/net/biorand_dl.h).
     test_biorand_dl            SHA-256 vectors, the release JSON scan, the URL allowlist
     test_biorand_dl DIR        Linux: the whole install from a made-up release over file:// (curl, tar), in DIR
     test_biorand_dl DIR ARCHIVE    the same with a real archive (biorand-cli-linux-x64.tar.gz)
     test_biorand_dl --live DIR     the real nightly from GitHub, installed into DIR (not a ctest) */
#include "../src/net/biorand_dl.h"
#include "../src/net/sha256.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int fails, saves;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

const char* tr(const char* en) { return en; }   /* (host/tr.c: the game's language) */
void host_log(const char* fmt, ...) { va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }
void crash_thread_init(void) {}
void settings_save_later(void) { saves++; }

static int sha(const char* s, size_t n, size_t step, const char* want)
{
    Sha256 c;
    uint8_t d[32];
    char hex[65];
    size_t i;
    sha256_init(&c);
    for (i = 0; i < n; i += step) sha256_update(&c, s + i, n - i < step ? n - i : step);
    sha256_final(&c, d);
    sha256_hex(d, hex);
    return !strcmp(hex, want);
}

#define LINUX "biorand-cli-linux-x64.tar.gz"
#define DIG "sha256:8465e828a86a6a52e750cdd141630492f5cf96489ac544e6f601293e6bb63a8c"
#define URL "https://github.com/biorand/classic/releases/download/nightly/" LINUX
#define UPLOADER "\"uploader\":{\"login\":\"github-actions[bot]\",\"name\":\"" LINUX "\",\"size\":1}"
#define ASSET(name, rest) "{\"url\":\"https://api.github.com/x\",\"id\":1,\"name\":\"" name "\",\"label\":\"\"," UPLOADER "," rest "}"
#define GOOD_REST "\"content_type\":\"application/gzip\",\"state\":\"uploaded\",\"size\":7719279,\"digest\":\"" DIG "\",\"download_count\":3,\"browser_download_url\":\"" URL "\""
#define REL(assets) "{\"url\":\"https://api.github.com/repos/biorand/classic/releases/1\",\"name\":\"nightly\",\"draft\":false,\"author\":{\"login\":\"x\",\"id\":2}," \
                    "\"assets\":[" assets "],\"body\":\"\\\"name\\\":\\\"" LINUX "\\\" {[\"}"

static int scan(const char* json, const char* name, int want)
{
    BrdlAsset a;
    char msg[100];
    memset(&a, 0, sizeof(a));
    return brdl_find_asset(json, (int)strlen(json), name, 0, &a, msg, sizeof(msg)) == want;
}

static void unit(void)
{
    static char mil[1000000];
    static const char* ok[] = {
        "https://api.github.com/repos/biorand/classic/releases/tags/nightly", URL, "https://api.github.com",
        "https://objects.githubusercontent.com/github-production-release-asset-2e65be/1?X-Amz-Algorithm=AWS4&response-content-disposition=attachment%3B",
        "https://release-assets.githubusercontent.com/github-production-release-asset/1/2?sp=r&sv=2018",
    };
    static const char* bad[] = {
        "http://github.com/x", "https://github.com.evil.com/x", "https://evilgithub.com/x", "https://githubusercontent.com/x",
        "https://.githubusercontent.com/x", "https://a..githubusercontent.com/x", "https://github.com:443/x", "https://user@github.com/x",
        "https://GitHub.com/x", "https://github.com/a b", "https://github.com/a\\b", "https://github.com/a\"b", "https:///x", "https://",
        "file:///etc/passwd", "", "https://evil.com/github.com", "https://evil.com/?https://github.com/", "ftp://github.com/x",
        "https://github.com/\x80", "https://github.com/a\nb", " https://github.com/x",
    };
    BrdlAsset a;
    char msg[100];
    size_t i;
    const char* j;

    /* SHA-256: FIPS 180-4 examples, and the same input fed in pieces across the 64-byte blocks */
    CHECK(sha("", 0, 1, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK(sha("abc", 3, 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    j = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    CHECK(sha(j, strlen(j), strlen(j), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    CHECK(sha(j, strlen(j), 1, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    j = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
    CHECK(sha(j, strlen(j), 7, "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"));
    memset(mil, 'a', sizeof(mil));
    CHECK(sha(mil, sizeof(mil), sizeof(mil), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    CHECK(sha(mil, sizeof(mil), 999, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    CHECK(sha(mil, 55, 55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"));
    CHECK(sha(mil, 64, 64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"));

    /* the URL allowlist */
    for (i = 0; i < SDL_arraysize(ok); i++) CHECK(brdl_url_ok(ok[i], 0));
    for (i = 0; i < SDL_arraysize(bad); i++) if (brdl_url_ok(bad[i], 0)) { printf("FAIL: URL allowed: %s\n", bad[i]); fails++; }
    CHECK(brdl_url_ok("file:///tmp/a.json", 1) && !brdl_url_ok("file://host/a", 1) && !brdl_url_ok("file:///a b", 1));
    CHECK(brdl_url_ok(URL, 1) && !brdl_url_ok("http://github.com/x", 1));

    {   /* the CLI's stderr lines (generate): progress, error, warning, the rest */
        int pct = -1;
        const char* t = NULL;
        CHECK(brdl_line("Progress: 35% [P1] Randomizing items", &pct, &t) == 'P' && pct == 35 && !strcmp(t, "[P1] Randomizing items"));
        CHECK(brdl_line("Progress: 0% Starting", &pct, &t) == 'P' && pct == 0 && !strcmp(t, "Starting"));
        CHECK(brdl_line("Progress: 100% Done", &pct, &t) == 'P' && pct == 100 && !strcmp(t, "Done"));
        CHECK(brdl_line("Progress: 7%", &pct, &t) == 'P' && pct == 7 && !*t);
        pct = -1;
        CHECK(!brdl_line("Progress: 101% x", &pct, &t) && !brdl_line("Progress: 1000% x", &pct, &t) && !brdl_line("Progress: % x", &pct, &t) &&
              !brdl_line("Progress: 5 x", &pct, &t) && !brdl_line("Progress:5% x", &pct, &t) && !brdl_line("progress: 5% x", &pct, &t) && pct == -1);
        CHECK(brdl_line("Error: Input file not found", &pct, &t) == 'E' && !strcmp(t, "Input file not found"));
        CHECK(brdl_line("Warning: player models were not found", &pct, &t) == 'W' && !strcmp(t, "player models were not found"));
        CHECK(!brdl_line("Info: Seed: R840-T1C0", &pct, &t) && !brdl_line("", &pct, &t) && !brdl_line("Error:x", &pct, &t));
    }

    /* the release JSON: valid (the uploader object's "name"/"size" and the body's text are not the asset) */
    j = REL(ASSET("biorand-cli-win-x64.zip", "\"size\":7150735,\"digest\":\"" DIG "\",\"browser_download_url\":\"https://github.com/w.zip\"") ","
            ASSET(LINUX, GOOD_REST) "," ASSET("biorand.zip", GOOD_REST));
    memset(&a, 0, sizeof(a));
    CHECK(brdl_find_asset(j, (int)strlen(j), LINUX, 0, &a, msg, sizeof(msg)) == BRJ_OK && a.size == 7719279 && !strcmp(a.digest, DIG) && !strcmp(a.url, URL));
    CHECK(brdl_find_asset(j, (int)strlen(j), "biorand-cli-win-x64.zip", 0, &a, msg, sizeof(msg)) == BRJ_OK && a.size == 7150735 && !strcmp(a.url, "https://github.com/w.zip"));
    CHECK(scan("  {\n  \"assets\" : [\n  {\n    \"name\" : \"" LINUX "\",\n    \"size\" : 12,\n    \"digest\" : \"" DIG "\",\n"
               "    \"browser_download_url\" : \"" URL "\"\n  }\n  ]\n}\n", LINUX, BRJ_OK));   /* pretty-printed */
    CHECK(scan("{\"browser_download_url\":\"" URL "\",\"digest\":\"" DIG "\",\"size\":5,\"name\":\"x\",\"assets\":[{\"digest\":\"" DIG "\",\"size\":5,\"browser_download_url\":\"" URL "\",\"name\":\"" LINUX "\"}]}", LINUX, BRJ_OK));
    /* the other platform only, none, or the name only in nested objects or strings */
    CHECK(scan(REL(ASSET("biorand-cli-win-x64.zip", GOOD_REST)), LINUX, BRJ_NO_ASSET));
    CHECK(scan(REL(""), LINUX, BRJ_NO_ASSET));
    CHECK(scan(REL(ASSET(LINUX, GOOD_REST)), LINUX ".sig", BRJ_NO_ASSET));
    CHECK(scan("{\"assets\":[[{\"name\":\"" LINUX "\"}],\"" LINUX "\"]}", LINUX, BRJ_NO_ASSET));
    /* bad fields */
    CHECK(scan(REL(ASSET(LINUX, "\"size\":7719279,\"browser_download_url\":\"" URL "\"")), LINUX, BRJ_BAD_DIGEST));   /* no digest */
    CHECK(scan(REL(ASSET(LINUX, "\"size\":7719279,\"digest\":null,\"browser_download_url\":\"" URL "\"")), LINUX, BRJ_BAD_DIGEST));
    CHECK(scan(REL(ASSET(LINUX, "\"size\":1,\"digest\":\"sha256:8465E828a86a6a52e750cdd141630492f5cf96489ac544e6f601293e6bb63a8c\",\"browser_download_url\":\"" URL "\"")), LINUX, BRJ_BAD_DIGEST));
    CHECK(scan(REL(ASSET(LINUX, "\"size\":1,\"digest\":\"sha256:8465e828\",\"browser_download_url\":\"" URL "\"")), LINUX, BRJ_BAD_DIGEST));
    CHECK(scan(REL(ASSET(LINUX, "\"size\":1,\"digest\":\"md5:8465e828a86a6a52e750cdd141630492f5cf96489ac544e6f601293e6bb63a8c\",\"browser_download_url\":\"" URL "\"")), LINUX, BRJ_BAD_DIGEST));
    CHECK(scan(REL(ASSET(LINUX, "\"size\":1,\"digest\":\"sha256:8465e828a86a6a52e750cdd141630492f5cf96489ac544e6f601293e6bb63a8c0\",\"browser_download_url\":\"" URL "\"")), LINUX, BRJ_BAD_DIGEST));
    CHECK(scan(REL(ASSET(LINUX, "\"digest\":\"" DIG "\",\"browser_download_url\":\"" URL "\"")), LINUX, BRJ_BAD_SIZE));   /* no size */
    CHECK(scan(REL(ASSET(LINUX, "\"size\":0,\"digest\":\"" DIG "\",\"browser_download_url\":\"" URL "\"")), LINUX, BRJ_BAD_SIZE));
    CHECK(scan(REL(ASSET(LINUX, "\"size\":-5,\"digest\":\"" DIG "\",\"browser_download_url\":\"" URL "\"")), LINUX, BRJ_BAD_SIZE));
    CHECK(scan(REL(ASSET(LINUX, "\"size\":104857601,\"digest\":\"" DIG "\",\"browser_download_url\":\"" URL "\"")), LINUX, BRJ_BAD_SIZE));
    CHECK(scan(REL(ASSET(LINUX, "\"size\":104857600,\"digest\":\"" DIG "\",\"browser_download_url\":\"" URL "\"")), LINUX, BRJ_OK));
    CHECK(scan(REL(ASSET(LINUX, "\"size\":99999999999,\"digest\":\"" DIG "\",\"browser_download_url\":\"" URL "\"")), LINUX, BRJ_BAD_SIZE));
    CHECK(scan(REL(ASSET(LINUX, "\"size\":1.5,\"digest\":\"" DIG "\",\"browser_download_url\":\"" URL "\"")), LINUX, BRJ_BAD_SIZE));
    CHECK(scan(REL(ASSET(LINUX, "\"size\":\"77\",\"digest\":\"" DIG "\",\"browser_download_url\":\"" URL "\"")), LINUX, BRJ_BAD_SIZE));
    CHECK(scan(REL(ASSET(LINUX, "\"size\":1,\"digest\":\"" DIG "\"")), LINUX, BRJ_BAD_URL));   /* no URL */
    CHECK(scan(REL(ASSET(LINUX, "\"size\":1,\"digest\":\"" DIG "\",\"browser_download_url\":\"http://github.com/x\"")), LINUX, BRJ_BAD_URL));
    CHECK(scan(REL(ASSET(LINUX, "\"size\":1,\"digest\":\"" DIG "\",\"browser_download_url\":\"https://evil.example/x\"")), LINUX, BRJ_BAD_URL));
    CHECK(scan(REL(ASSET(LINUX, "\"size\":1,\"digest\":\"" DIG "\",\"browser_download_url\":\"https://github.com/a\\u0022b\"")), LINUX, BRJ_BAD_URL));
    CHECK(scan(REL(ASSET(LINUX, "\"size\":1,\"digest\":\"" DIG "\",\"browser_download_url\":\"file:///tmp/x\"")), LINUX, BRJ_BAD_URL));
    /* malformed */
    j = REL(ASSET(LINUX, GOOD_REST));
    for (i = 0; i < strlen(j); i++) {   /* every cut is refused, never read past */
        char* cut = SDL_malloc(i + 1);
        memcpy(cut, j, i);
        cut[i] = 0;
        if (brdl_find_asset(cut, (int)i, LINUX, 0, &a, msg, sizeof(msg)) != BRJ_MALFORMED) { printf("FAIL: cut at %d accepted\n", (int)i); fails++; }
        SDL_free(cut);
    }
    CHECK(scan("", LINUX, BRJ_MALFORMED));
    CHECK(scan("[]", LINUX, BRJ_MALFORMED));
    CHECK(scan("{}", LINUX, BRJ_MALFORMED));
    CHECK(scan("{\"assets\":[]} x", LINUX, BRJ_MALFORMED));
    CHECK(scan("{\"assets\":[],}", LINUX, BRJ_MALFORMED));
    CHECK(scan("{\"assets\" [] }", LINUX, BRJ_MALFORMED));
    CHECK(scan("<html><body>502 Bad Gateway</body></html>", LINUX, BRJ_MALFORMED));
    CHECK(scan("{\"a\":[[[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]]]}", LINUX, BRJ_MALFORMED));   /* too deep */
    CHECK(scan("{\"assets\":[{\"name\":\"" LINUX "\x01\"}]}", LINUX, BRJ_MALFORMED));   /* a control character in a string */
    /* GitHub's error answers */
    j = "{\"message\":\"API rate limit exceeded for 192.0.2.1. (But here's the good news: Authenticated requests get a higher rate limit.)\","
        "\"documentation_url\":\"https://docs.github.com/rest/overview/resources-in-the-rest-api#rate-limiting\"}";
    CHECK(brdl_find_asset(j, (int)strlen(j), LINUX, 0, &a, msg, sizeof(msg)) == BRJ_MESSAGE && !strncmp(msg, "API rate limit exceeded for 192.0.2.1.", 38));
    j = "{\"message\":\"Not \\\"Found\\\"\\n\",\"documentation_url\":\"https://docs.github.com/rest\",\"status\":\"404\"}";
    CHECK(brdl_find_asset(j, (int)strlen(j), LINUX, 0, &a, msg, 12) == BRJ_MESSAGE && !strcmp(msg, "Not \"Found\""));
    CHECK(scan("{\"message\":5}", LINUX, BRJ_MALFORMED));
}

/* ------------------------------------------------------------ the install (Linux) */
static char dir[600], game[700];

static int wait_end(void)
{
    BrdlStatus s;
    int ms;
    for (ms = 0; ms < 600000; ms += 20) {
        biorand_cli_status(&s);
        if (s.state >= BRDL_INSTALLED) {
            SDL_Delay(50);   /* the thread's last steps after the state (busy flag) */
            return s.state;
        }
        SDL_Delay(20);
    }
    return -1;
}

static int entries(const char* d)
{
    int n = -1;
    char** g = SDL_GlobDirectory(d, NULL, 0, &n);
    SDL_free(g);
    return g ? n : -1;
}

static void put(const char* path, const char* text)
{
    FILE* f = fopen(path, "wb");
    if (f) { fputs(text, f); fclose(f); }
}

static void release(const char* name, const char* archive, int size, const char* digest)
{
    char p[1100], j[4096];
    snprintf(j, sizeof(j), "{\"name\":\"nightly\",\"assets\":[{\"name\":\"biorand-cli-win-x64.zip\",\"size\":1,\"digest\":\"%s\",\"browser_download_url\":\"https://github.com/w.zip\"},"
             "{\"name\":\"%s\",\"size\":%d,\"digest\":\"%s\",\"browser_download_url\":\"file://%s\"}]}", digest, name, size, digest, archive);
    snprintf(p, sizeof(p), "%s/release.json", dir);
    put(p, j);
}

static int install(const char* real)
{
    char p[1100], q[1100], arc[1100], cmd[3000], dig[80], hex[65], keep[1100];
    BrdlStatus s;
    SDL_PathInfo pi;
    uint8_t d[32];
    Sha256 c;
    size_t len;
    void* data;
    struct stat st;
    snprintf(cmd, sizeof(cmd), "command -v curl >/dev/null && command -v tar >/dev/null");
    if (system(cmd)) { printf("SKIP: no curl or tar\n"); return 77; }
    snprintf(game, sizeof(game), "%s/My Game \xc3\xbc", dir);   /* a game folder with a space and a non-ASCII letter (tar runs in it) */
    snprintf(arc, sizeof(arc), "%s/" LINUX, dir);
    snprintf(cmd, sizeof(cmd), "rm -rf '%s' && mkdir -p '%s/src' '%s' '%s/keep'", dir, dir, game, dir);
    if (system(cmd)) return 1;
    if (real) snprintf(cmd, sizeof(cmd), "cp '%s' '%s'", real, arc);
    else {   /* as the nightly: ./biorand-cli, ./LICENSE, ./README.md; mode 644 so that the chmod shows */
        snprintf(p, sizeof(p), "%s/src/biorand-cli", dir); put(p, "#!/bin/sh\necho test\n");
        snprintf(p, sizeof(p), "%s/src/LICENSE", dir); put(p, "MIT License (test)\n");
        snprintf(p, sizeof(p), "%s/src/README.md", dir); put(p, "readme\n");
        snprintf(cmd, sizeof(cmd), "chmod 644 '%s/src/biorand-cli' && tar -czf '%s' -C '%s/src' .", dir, arc, dir);
    }
    if (system(cmd) || !(data = SDL_LoadFile(arc, &len))) return 1;
    sha256_init(&c); sha256_update(&c, data, len); sha256_final(&c, d); sha256_hex(d, hex);
    SDL_free(data);
    snprintf(dig, sizeof(dig), "sha256:%s", hex);
    release(LINUX, arc, (int)len, dig);
    snprintf(p, sizeof(p), "file://%s/release.json", dir);
    SDL_setenv_unsafe("RECVX_BIORAND_API_TEST", p, 1);
    SDL_setenv_unsafe("RECVX_BIORAND_CLI_DIR_TEST", game, 1);

    /* nothing installed: a newer one */
    CHECK(!biorand_cli_present() && !strcmp(biorand_cli_exe(), strcat(strcpy(q, game), "/biorand-cli")));
    CHECK(biorand_cli_check_update() && wait_end() == BRDL_NEWER && entries(game) == 0);

    /* a left-over temporary folder with a link out of it: removed, the link not followed */
    snprintf(keep, sizeof(keep), "%s/keep/file", dir); put(keep, "keep");
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s/biorand-dl/x' && ln -s '%s/keep' '%s/biorand-dl/x/link'", game, dir, game);
    CHECK(!system(cmd));

    /* the install */
    CHECK(biorand_cli_install());
    CHECK(!biorand_cli_install() && !biorand_cli_check_update());   /* one at a time */
    CHECK(wait_end() == BRDL_INSTALLED);
    biorand_cli_status(&s);
    printf("install: state %d, %d / %d bytes, '%s'\n", s.state, s.done, s.total, s.error);
    CHECK(s.done == (int)len && s.total == (int)len);
    CHECK(biorand_cli_present() && !strcmp(g_biorand_cli_digest, dig) && saves == 1);
    CHECK(stat(biorand_cli_exe(), &st) == 0 && (st.st_mode & 0777) == 0755);
    snprintf(p, sizeof(p), "%s/biorand-cli.LICENSE", game);
    CHECK(SDL_GetPathInfo(p, &pi) && pi.type == SDL_PATHTYPE_FILE && pi.size > 0);
    CHECK(entries(game) == 2);   /* biorand-cli, biorand-cli.LICENSE: no archive, README or biorand-dl/ */
    CHECK(SDL_GetPathInfo(keep, NULL));
    if (!real) {
        size_t n;
        CHECK((data = SDL_LoadFile(biorand_cli_exe(), &n)) && !strcmp(data, "#!/bin/sh\necho test\n"));
        SDL_free(data);
    }

    /* the same digest: current; then a new nightly (another digest): newer */
    CHECK(biorand_cli_check_update() && wait_end() == BRDL_CURRENT);
    g_biorand_cli_digest[5] ^= 1;
    CHECK(biorand_cli_check_update() && wait_end() == BRDL_NEWER);
    g_biorand_cli_digest[5] ^= 1;

    /* a damaged download: refused, the installed CLI untouched, nothing left behind */
    hex[10] = hex[10] == '0' ? '1' : '0';
    snprintf(q, sizeof(q), "sha256:%s", hex);
    release(LINUX, arc, (int)len, q);
    CHECK(biorand_cli_install() && wait_end() == BRDL_ERROR);
    biorand_cli_status(&s);
    printf("mismatch: '%s'\n", s.error);
    CHECK(strstr(s.error, "verification failed") && entries(game) == 2 && !strcmp(g_biorand_cli_digest, dig) && saves == 1);

    /* the size GitHub lists differs */
    release(LINUX, arc, (int)len - 1, dig);
    CHECK(biorand_cli_install() && wait_end() == BRDL_ERROR && entries(game) == 2);

    /* GitHub's rate limit; a missing API answer */
    snprintf(p, sizeof(p), "%s/release.json", dir);
    put(p, "{\"message\":\"API rate limit exceeded for 192.0.2.1.\",\"documentation_url\":\"https://docs.github.com\"}");
    CHECK(biorand_cli_install() && wait_end() == BRDL_ERROR);
    biorand_cli_status(&s);
    printf("rate limit: '%s'\n", s.error);
    CHECK(strstr(s.error, "60 requests per hour") != NULL);
    SDL_setenv_unsafe("RECVX_BIORAND_API_TEST", "file:///nonexistent/release.json", 1);
    CHECK(biorand_cli_check_update() && wait_end() == BRDL_ERROR);
    SDL_setenv_unsafe("RECVX_BIORAND_API_TEST", "http://api.github.com/x", 1);
    CHECK(biorand_cli_check_update() && wait_end() == BRDL_ERROR);

    /* cancelled at once: no temporary folder left, and the old CLI is still there */
    release(LINUX, arc, (int)len, dig);
    snprintf(p, sizeof(p), "file://%s/release.json", dir);
    SDL_setenv_unsafe("RECVX_BIORAND_API_TEST", p, 1);
    CHECK(biorand_cli_install());
    biorand_cli_cancel();
    { int e = wait_end(); CHECK(e == BRDL_CANCELLED || e == BRDL_INSTALLED); printf("cancel: state %d\n", e); }
    CHECK(entries(game) == 2 && biorand_cli_present());

    /* a fresh install where the old one was removed */
    SDL_RemovePath(biorand_cli_exe());
    CHECK(biorand_cli_install() && wait_end() == BRDL_INSTALLED && entries(game) == 2 && biorand_cli_present());
    return fails != 0;
}

static int live(void)
{
    BrdlStatus s;
    int e = -1, last = -1;
    Uint64 t0 = SDL_GetTicks();
    snprintf(game, sizeof(game), "%s", dir);
    SDL_setenv_unsafe("RECVX_BIORAND_CLI_DIR_TEST", game, 1);
    CHECK(biorand_cli_check_update() && (e = wait_end()) == BRDL_NEWER);
    biorand_cli_status(&s);
    printf("live check: state %d '%s'\n", e, s.error);
    CHECK(biorand_cli_install());
    for (;;) {
        biorand_cli_status(&s);
        if (s.state != last || s.state == BRDL_DOWNLOAD) printf("  %5.1f s: state %d, %d / %d\n", (SDL_GetTicks() - t0) / 1000.0, s.state, s.done, s.total);
        last = s.state;
        if (s.state >= BRDL_INSTALLED) break;
        SDL_Delay(500);
    }
    SDL_Delay(50);
    printf("live install: state %d '%s', digest %s, %s present %d\n", s.state, s.error, g_biorand_cli_digest, biorand_cli_exe(), biorand_cli_present());
    CHECK(s.state == BRDL_INSTALLED && biorand_cli_present() && entries(game) == 2);
    CHECK(biorand_cli_check_update() && wait_end() == BRDL_CURRENT);
    return fails != 0;
}

int main(int argc, char** argv)
{
    unit();
    if (argc > 2 && !strcmp(argv[1], "--live")) {
        snprintf(dir, sizeof(dir), "%s", argv[2]);
        return live();
    }
    if (argc > 1) {
        int r;
        snprintf(dir, sizeof(dir), "%s", argv[1]);
        if ((r = install(argc > 2 ? argv[2] : NULL)) == 77) return 77;
    }
    if (fails) printf("%d failures\n", fails);
    else printf("biorand_dl: all passed\n");
    return fails != 0;
}
