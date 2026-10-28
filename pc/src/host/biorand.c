/* BioRand packs: the folder, its seed, whether the loader serves it (biorand.h). Any thread; biorand_scan at
   the start and on the title menu (lobby_glue.c), the rest reads its result. */
#include "../ps2_platform.h"
#include "biorand.h"
#include "settings.h"
#include "../../include/pc_parse.h"
#include "../net/biorand_dl.h"
#include "biorand_opts.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct {
    int pack, seed;
    unsigned int id;
    char text[24];
    char str[BIORAND_SEED_LEN + 1];   /* the log's whole seed string when it is one we can decode, else "" */
} br = { .seed = -1 };

static const char* mods_dir(void) { return SDL_getenv("RECVX_MODS") ? SDL_getenv("RECVX_MODS") : "mods"; }

static unsigned int fnv(unsigned int h, const void* p, size_t n)
{
    const unsigned char* b = p;
    while (n--) h = (h ^ *b++) * 16777619u;
    return h;
}

/* The identity: log_pl0.txt (the seed line with every option, the spoiler of every item and door) and the room
   files' names and sizes, not their bytes (ponytail: a pack is ~1.4 GB; hash the contents if two packs ever share
   a log). */
extern int g_dev_mode;   /* --dev (bindings.c): BioRand is a developer option for now */
int biorand_shown(void) { return g_dev_mode; }

void biorand_scan(void)
{
    char dir[1100], path[1200], line[256];
    char** list;
    int n = 0, i, rooms = 0, seed = -1, log = 0;
    unsigned int id = 2166136261u, sum = 0;
    FILE* f;
    if (!g_dev_mode) {   /* without --dev: no pack, so nothing of BioRand is shown or read */
        br.pack = 0, br.seed = -1, br.id = 0, br.text[0] = br.str[0] = 0;
        return;
    }
    snprintf(dir, sizeof(dir), "%s/biorand", mods_dir());
    if ((list = SDL_GlobDirectory(dir, "RM_*.RDX", SDL_GLOB_CASEINSENSITIVE, &n))) {
        for (i = 0; i < n; i++) {
            SDL_PathInfo pi;
            snprintf(path, sizeof(path), "%s/%s", dir, list[i]);
            if (!SDL_GetPathInfo(path, &pi)) continue;
            sum += fnv(fnv(2166136261u, list[i], strlen(list[i])), &pi.size, sizeof(pi.size));   /* in any order */
            rooms++;
        }
        SDL_free(list);
    }
    br.str[0] = 0;
    snprintf(path, sizeof(path), "%s/log_pl0.txt", dir);
    if ((f = pc_fopen(path, "rb"))) {
        size_t k;
        unsigned char buf[4096];
        log = 1;
        while (fgets(line, sizeof(line), f)) {   /* "    Seed: R840-0KR3-T4HH..." (line 2; BaseRandomiser.GenerateRdts) */
            const char* l = line + strspn(line, " \t");
            if (!strncmp(l, "Seed: ", 6)) {
                BioOpts o;
                seed = pc_biorand_seed(l + 6);
                l += 6;
                snprintf(br.str, sizeof(br.str), "%.*s", (int)strcspn(l, " \t\r\n"), l);
                if (strlen(br.str) != BIORAND_SEED_LEN || biorand_seed_decode(br.str, &o)) br.str[0] = 0;   /* (co-op sends it) */
                break;
            }
        }
        rewind(f);
        while ((k = fread(buf, 1, sizeof(buf), f)) > 0) id = fnv(id, buf, k);
        fclose(f);
    }
    id = fnv(id, &sum, sizeof(sum));
    br.seed = rooms || log ? seed : -1;
    br.id = rooms || log ? (id ? id : 1) : 0;
    if (br.seed >= 0) snprintf(br.text, sizeof(br.text), "seed %d", br.seed);
    else br.text[0] = 0;
    br.pack = rooms || log;
    if (br.pack) host_log("biorand: pack in %s: %d room files, %s (id %08x); BioRand %s\n", dir, rooms, br.seed >= 0 ? br.text : "seed unknown",
                          br.id, g_config.biorand ? "on" : "off");
}

int biorand_pack(void) { return br.pack; }
int biorand_seed(void) { return br.seed; }
unsigned int biorand_id(void) { return br.id; }
const char* biorand_seed_text(void) { return br.text; }
const char* biorand_seed_string(void) { return br.str; }

/* our setting; a co-op guest follows the host (coop_biorand: the host's choice, checked against our pack) */
int biorand_active(void)
{
    int c;
    if (!br.pack) return 0;
    c = coop_biorand();
    return c >= 0 ? c : g_config.biorand;
}

/* the setting changes at the title only: a game running has its room files */
const char* biorand_locked(void)
{
    if (!br.pack) return "Generate a BioRand game first.";
    if (pc_in_game()) return "Only at the title screen.";
    if (coop_biorand() >= 0) return "The co-op host decides.";
    return NULL;
}

int biorand_setting(void) { return g_config.biorand; }

/* ---------------------------------------------------------------- generating */
/* p made absolute from the current directory (where the game reads it from) */
static void abs_path(const char* p, char* out, int n)
{
    char* cwd;
    if (p[0] == '/' || p[0] == '\\' || (p[0] && p[1] == ':') || !(cwd = SDL_GetCurrentDirectory())) { snprintf(out, n, "%s", p); return; }
    snprintf(out, n, "%s%s", cwd, p[0] == '.' && (p[1] == '/' || p[1] == '\\') ? p + 2 : p);   /* (SDL ends it with a separator) */
    SDL_free(cwd);
}

static void mods_abs(char* out, int n)
{
    abs_path(mods_dir(), out, n);
    SDL_CreateDirectory(out);
}

int biorand_mods_free_mb(void)
{
    char m[1100];
    mods_abs(m, sizeof(m));
    return biorand_free_mb(m);
}

int biorand_iso_known(void) { return g_config.iso_path[0] != 0; }

/* BIORAND_CONFIG: <save>/biorand/settings.json, ours only (BioRand's GUI keeps its own). generateCvPcMod: the PC mod
   folder, not a PS2 image; disableCustomContent: BioRand's custom data folder is always the default config folder,
   so off, one seed gives one pack everywhere. The CLI adds seedCv and lastSelectedGame; rewritten each run. */
int biorand_generate(const char* seed_string)
{
    char dir[1100], cfg[1200], iso[1100], mods[1100];
    const char* test = SDL_getenv("RECVX_BIORAND_ISO_TEST");   /* test aid: another image for the CLI (a bad path) */
    SDL_IOStream* f;
    int ok;
    abs_path(g_config.save_dir, dir, sizeof(dir));
    snprintf(cfg, sizeof(cfg), "%s/biorand", dir);
    SDL_CreateDirectory(cfg);
    snprintf(cfg, sizeof(cfg), "%s/biorand/settings.json", dir);
    f = SDL_IOFromFile(cfg, "wb");
    ok = f && SDL_IOprintf(f, "{\n  \"lastSelectedGame\": 3,\n  \"generateCvPcMod\": true,\n  \"disableCustomContent\": true\n}\n") > 0;
    if (f && !SDL_CloseIO(f)) ok = 0;
    if (!ok) { host_log("biorand: cannot write %s\n", cfg); return -1; }
    abs_path(test ? test : g_config.iso_path, iso, sizeof(iso));
    mods_abs(mods, sizeof(mods));
    return biorand_cli_generate(seed_string, iso, mods, cfg);
}

void biorand_enable(void)
{
    g_config.biorand = 1;
    settings_save_later();
}

int biorand_toggle(void)
{
    if (biorand_locked()) return 0;
    g_config.biorand ^= 1;
    settings_save_later();
    host_log("biorand: %s at the title (%s)\n", g_config.biorand ? "on" : "off", br.seed >= 0 ? br.text : "seed unknown");
    return 1;
}

/* BioRand's PS2 build patches rifle stacking in; its amounts count on it */
int biorand_rifle_stack(void) { return g_config.rifle_stack || biorand_active(); }

void* biorand_room0(int* size)
{
    char path[1200];
    unsigned char *in = NULL, *d = NULL;
    long n, cap = 4 << 20, m = -1;
    FILE* f;
    snprintf(path, sizeof(path), "%s/biorand/RM_0000.RDX", mods_dir());
    if (!(f = pc_fopen(path, "rb"))) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n > 0 && n < (32 << 20) && (in = malloc(n)) && (d = malloc(cap)) && fread(in, 1, n, f) == (size_t)n) m = pc_prs_expand(in, n, &d, &cap);
    fclose(f);
    free(in);
    if (m < 0x80) { free(d); return NULL; }
    *size = (int)m;
    return d;
}
