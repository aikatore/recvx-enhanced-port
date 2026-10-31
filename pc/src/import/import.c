/* Imports from other games' discs: the F9 window's extraction thread, its manifest, and the
   converted player models for "You appear as" (import.h; docs/IMPORT_OUTBREAK.md). */
#include "import.h"
#include "../host/lang.h"
#include "../host/actors.h"
#include "../host/fileio.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void host_log(const char* fmt, ...);

ImportSettings g_import;
ImportProgress g_import_progress;
volatile int g_import_reload;
const ImportSource import_sources[IMPORT_SOURCES] = {
    { "outbreak2", "Resident Evil Outbreak File #2", "SLUS-20984", IMPORT_CAT_CHARACTERS, NULL, NULL },
    { "sles50306", "Resident Evil Code: Veronica X (Europe)", "SLES-50306", IMPORT_CAT_LANGUAGES, NULL, NULL },
    { "slus20184ru", "Resident Evil Code: Veronica X (Russian fan translation)", "SLUS-20184", IMPORT_CAT_LANGUAGES,
      "Russian fan translation by Brosko. Import it from your own disc image of the US release with the patch.",
      "Needs the US disc image with the Russian fan translation by Brosko: %s." },
    { "slpm65022", "Biohazard Code: Veronica Kanzenban (Japan)", "SLPM-65022", IMPORT_CAT_LANGUAGES,
      "Japanese, with cutscene subtitles. Import it from your own disc image of the Japanese release.",
      "Needs a Japanese disc image of Biohazard Code: Veronica Kanzenban: %s." },
    { "sles50306tr", "Resident Evil Code: Veronica X (Turkish fan translation)", "SLES-50306", IMPORT_CAT_LANGUAGES,
      "Turkish fan translation by Patriots \xC3\x87" "eviri, with cutscene subtitles. Import it from your own disc image of the European release (SLES-50306) with the Turkish patch.",
      "Needs the European disc image with the Turkish fan translation by Patriots \xC3\x87" "eviri: %s." },
    { "slus20184pt", "Resident Evil Code: Veronica X (Brazilian Portuguese fan translation)", "SLUS-20184", IMPORT_CAT_LANGUAGES,
      "Brazilian Portuguese fan translation by gledson999. Import it from your own disc image of the US release (SLUS-20184) with the patch.",
      "Needs the US disc image with the Brazilian Portuguese fan translation by gledson999: %s." },
};

#define ITEMS_MAX 1024

/* ini import_dir, else <mods>/import (RECVX_MODS, default ./mods) */
static void source_dir(int src, char* out, int n)
{
    const char* mods = SDL_getenv("RECVX_MODS") ? SDL_getenv("RECVX_MODS") : "mods";
    if (g_import.dir[0]) snprintf(out, n, "%s/%s", g_import.dir, import_sources[src].key);
    else snprintf(out, n, "%s/import/%s", mods, import_sources[src].key);
}
void import_source_dir(int src, char* out, int n) { source_dir(src, out, n); }
int import_present(int src)
{
    char d[1100];
    source_dir(src, d, sizeof(d));
    return SDL_GetPathInfo(d, NULL);
}

/* <dir>/<source>/manifest.txt: a header, then one line per model:
   item|id|name|variant|ok|nodes|meshes|verts|tris|textures|player|female|note */
typedef struct Save { int src, n; const ImportItem* it; const char* iso; } Save;

static int write_manifest(FILE* f, void* ud)
{
    const Save* s = ud;
    char when[64];
    time_t t = time(NULL);
    strftime(when, sizeof(when), "%Y-%m-%d %H:%M", localtime(&t));
    fprintf(f, "# recvx import manifest 1: %s (%s)\nsource|%s\niso|%s\nwhen|%s\n", import_sources[s->src].title,
            import_sources[s->src].serial, import_sources[s->src].key, s->iso, when);
    for (int i = 0; i < s->n; i++) {
        const ImportItem* x = &s->it[i];
        fprintf(f, "item|%s|%s|%s|%d|%d|%d|%d|%d|%d|%d|%d|%s\n", x->id, x->name, x->variant, x->ok, x->nodes, x->meshes,
                x->verts, x->tris, x->textures, x->player, x->female, x->note);
    }
    return ferror(f) ? -1 : 0;
}

int import_results(int src, ImportItem* out, int max, char* when, int wn)
{
    char p[1100], line[512];
    int n = 0;
    source_dir(src, p, sizeof(p));
    SDL_strlcat(p, "/manifest.txt", sizeof(p));
    FILE* f = import_sources[src].cat == IMPORT_CAT_CHARACTERS ? pc_fopen(p, "r") : NULL;
    if (when && wn) when[0] = 0;
    if (!f) return 0;
    while (fgets(line, sizeof(line), f) && n < max) {
        char* c = strpbrk(line, "\r\n");
        if (c) *c = 0;
        if (!strncmp(line, "when|", 5) && when) snprintf(when, wn, "%s", line + 5);
        if (strncmp(line, "item|", 5)) continue;
        char* fld[13] = { 0 };
        int k = 0;
        for (char* s = line + 5; s && k < 13; k++) { fld[k] = s; s = strchr(s, '|'); if (s) *s++ = 0; }
        if (k < 12) continue;
        ImportItem* x = &out[n++];
        memset(x, 0, sizeof(*x));
        snprintf(x->id, sizeof(x->id), "%s", fld[0]);
        snprintf(x->name, sizeof(x->name), "%s", fld[1]);
        snprintf(x->variant, sizeof(x->variant), "%s", fld[2]);
        x->ok = atoi(fld[3]); x->nodes = atoi(fld[4]); x->meshes = atoi(fld[5]); x->verts = atoi(fld[6]);
        x->tris = atoi(fld[7]); x->textures = atoi(fld[8]); x->player = atoi(fld[9]); x->female = atoi(fld[10]);
        snprintf(x->note, sizeof(x->note), "%s", fld[11] ? fld[11] : "");
        x->group = ob2_group(x->id);   /* (one source so far) */
    }
    fclose(f);
    return n;
}

int import_run(int src, int what)
{
    static ImportItem items[ITEMS_MAX];
    char dir[1100], p[1200];
    int n = 0, r;
    ImportProgress* pg = &g_import_progress;
    pg->done = pg->total = pg->failed = 0;
    snprintf(pg->phase, sizeof(pg->phase), tr("Reading disc..."));
    source_dir(src, dir, sizeof(dir));
    SDL_CreateDirectory(dir);   /* (creates the parents too) */
    if (import_sources[src].cat == IMPORT_CAT_LANGUAGES) {
        r = lang_extract(src, g_import.iso[src], dir);
        host_log("import: %s: %s\n", import_sources[src].key, pg->phase);
        return r;
    }
    r = ob2_extract(g_import.iso[src], dir, what & IMPORT_OFFERED, items, ITEMS_MAX, &n);
    if (r < 0) { host_log("import: %s: %s\n", import_sources[src].key, pg->phase); return -1; }
    Save s = { src, n, items, g_import.iso[src] };
    snprintf(p, sizeof(p), "%s/manifest.txt", dir);
    if (pc_save_file(p, "w", write_manifest, &s)) host_log("import: cannot write %s\n", p);
    int players = 0;
    for (int i = 0; i < n; i++) {
        players += items[i].player;
        if (!items[i].ok || items[i].note[0]) host_log("import: %s %s: %s\n", items[i].id, items[i].ok ? "written" : "FAILED", items[i].note);
    }
    snprintf(pg->phase, sizeof(pg->phase), tr("Imported %d of %d models; %d playable models."), n - r, n, players);
    host_log("import: %s: %d models extracted to %s, %d failed, %d usable as player models\n", import_sources[src].key, n - r, dir, r, players);
    return r;
}

static int run_src, run_what;
static int SDLCALL run_thread(void* ud)
{
    (void)ud;
    g_import_progress.result = import_run(run_src, run_what);
    __atomic_store_n(&g_import_progress.running, 0, __ATOMIC_RELEASE);
    return 0;
}

int import_start(int src, int what)
{
    if (__atomic_load_n(&g_import_progress.running, __ATOMIC_ACQUIRE)) return 0;
    run_src = src; run_what = what;
    g_import_progress.running = 1;
    SDL_Thread* t = SDL_CreateThread(run_thread, "import", NULL);
    if (!t) { g_import_progress.running = 0; return 0; }
    SDL_DetachThread(t);
    return 1;
}

/* ---------------------------------------------------------------- "You appear as" */
void import_registry_load(void)
{
    static ImportItem items[ITEMS_MAX];
    int k = actor_import_n, full = 0;   /* (we are the only writer) */
    for (int src = 0; src < IMPORT_SOURCES; src++) {
        char dir[1100];
        int n = import_results(src, items, ITEMS_MAX, NULL, 0);
        source_dir(src, dir, sizeof(dir));
        for (int g = 0; g < IMPORT_GROUPS; g++)   /* grouped (the manifest is in disc order): the lobby's list shows the groups */
        for (int i = 0; i < n; i++) {
            char file[1200], name[96];
            int j;
            if (!items[i].player || (items[i].group > 0 ? items[i].group : 0) != g || !(IMPORT_OFFERED & (1 << g))) continue;
            SDL_snprintf(file, sizeof(file), "%s/%s/%s.cvxm", dir, items[i].id, items[i].id);
            for (j = 0; j < k && strcmp(actor_import_file[j], file); j++) {}
            if (j < k) continue;   /* listed already: it keeps its place (a later extraction appends) */
            if (k == ACTOR_IMPORT_MAX) { full++; continue; }
            if (items[i].variant[0]) SDL_snprintf(name, sizeof(name), "%s (%s)", items[i].name, items[i].variant);
            else SDL_snprintf(name, sizeof(name), "%s", items[i].name);
            char *f = SDL_strdup(file), *nm = SDL_strdup(name);   /* never freed: the game may show it any time */
            if (!f || !nm) { SDL_free(f); SDL_free(nm); full++; continue; }
            actor_import_file[k] = f;
            actor_import_group[k] = (unsigned char)(items[i].group >= 0 ? items[i].group : 0);
            actor_imports[k] = (ActorModel){ .name = nm, .anim = items[i].female ? ANIM_FEMALE : ANIM_MALE };
            k++;
        }
    }
    if (full) host_log("import: the list of imported models is full (%d): %d more not listed\n", ACTOR_IMPORT_MAX, full);
    if (k != actor_import_n) host_log("import: %d imported player models listed\n", k);
    __atomic_store_n(&actor_import_n, k, __ATOMIC_RELEASE);
}
