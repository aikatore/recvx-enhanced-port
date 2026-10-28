/* Importing assets from other games' discs (F9 window; docs/IMPORT_OUTBREAK.md) and other releases of this
   game (languages; docs/LANGUAGES.md). The user's own disc image is read in place; what is extracted goes to
   <dir>/<source>/ (default mods/import), never into the repository. Sources: Resident Evil Outbreak File #2
   (SLUS-20984, characters), Resident Evil Code: Veronica X European (SLES-50306, languages). */
#pragma once
#include <stdint.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif

enum { IMPORT_OUTBREAK2, IMPORT_SLES50306, IMPORT_SOURCES };
enum { IMPORT_CAT_CHARACTERS, IMPORT_CAT_LANGUAGES };   /* what a source gives (the IMPORT screen's categories) */

/* the groups of models (F9 lists them by group), and what to extract: bits 1 << group (F9 checkboxes,
   RECVX_IMPORT_WHAT) */
enum { IMPORT_GROUP_PLAYERS, IMPORT_GROUP_CUTSCENE, IMPORT_GROUP_NPCS, IMPORT_GROUP_ENEMIES, IMPORT_GROUPS };
enum { IMPORT_PLAYERS = 1, IMPORT_CUTSCENE = 2, IMPORT_NPCS = 4, IMPORT_ENEMIES = 8, IMPORT_ALL = 15 };
/* the groups offered for now: F9's checkboxes and table, what import_run extracts and what "You appear
   as" lists (a manifest of an older run may hold the others: left out). The cutscene / high-detail
   versions and the enemies work the same way; IMPORT_ALL here brings them back. */
#define IMPORT_OFFERED (IMPORT_PLAYERS | IMPORT_NPCS)

typedef struct ImportSettings {
    char dir[1024];                     /* ini import_dir; empty: <mods>/import (RECVX_MODS, default ./mods) */
    char iso[IMPORT_SOURCES][1024];     /* ini import_<key>_iso */
} ImportSettings;
extern ImportSettings g_import;

/* one extracted model (a line of <dir>/<source>/manifest.txt) */
typedef struct ImportItem {
    char id[16];          /* the disc's file name without extension, e.g. P00_00 */
    char name[48];        /* e.g. Kevin Ryman */
    char variant[32];     /* e.g. Costume 1 */
    int ok;               /* glTF written */
    int nodes, meshes, verts, tris, textures;
    int player;           /* 1: converted for "You appear as" (<id>.cvxm), 2: male, female in `female` */
    int female;
    int group;            /* IMPORT_GROUP_* (from the id) */
    char note[160];       /* failure reason, or what was left out */
} ImportItem;

typedef struct ImportSource {
    const char* key;      /* folder and ini name (import_<key>_iso): outbreak2 */
    const char* title;    /* Resident Evil Outbreak File #2 */
    const char* serial;   /* SLUS-20984 (checked in SYSTEM.CNF) */
    int cat;              /* IMPORT_CAT_* */
} ImportSource;
extern const ImportSource import_sources[IMPORT_SOURCES];

/* progress of a running extraction (written by its thread, read by the overlay) */
typedef struct ImportProgress {
    volatile int running, done, total, failed;
    volatile int result;  /* the last run's import_run result, set before running drops to 0 */
    char phase[160];      /* what is being done (always 0-terminated); after a run its summary or why the disc is unusable */
} ImportProgress;
extern ImportProgress g_import_progress;

/* where `src`'s files go; whether that folder is there */
void import_source_dir(int src, char* out, int n);
int  import_present(int src);

/* the results of the last extraction of `src` (read from its manifest; a language source has none: lang.h) */
int import_results(int src, ImportItem* out, int max, char* when, int wn);

/* runs the extraction of `src` (`what`: IMPORT_* bits) on a thread of its own; 0 if one is running */
int import_start(int src, int what);
/* the same, synchronously (RECVX_IMPORT=outbreak2 headless runs); returns the number of failures, -1 if the disc is unusable */
int import_run(int src, int what);

/* "You appear as": publishes the converted player models of all sources' manifests to
   actors.c (actor_imports). Main thread; at start (config_load) and after an extraction. */
void import_registry_load(void);
/* any thread: 1 asks the main thread for import_registry_load (host.c, every frame; the IMPORT screen's runs) */
extern volatile int g_import_reload;

/* source-specific extractors (ob2.c, lang.c) */
int ob2_group(const char* id);   /* IMPORT_GROUP_* of a disc file name, -1: not a character model */
int ob2_extract(const char* iso, const char* outdir, int what, ImportItem* items, int max, int* n);
int lang_extract(int src, const char* iso, const char* outdir);   /* languages: how many failed, -1 the disc is unusable */

/* disc reading (ob2.c): a file of the ISO 9660 image (`path` with '/' for folders, any case); bytes at an offset;
   the files of an AFS archive at `base` (malloc'd; names from its name table or "#i"; an entry outside: size 0) */
typedef struct AfsFile { int64_t off; uint32_t size; char name[32]; } AfsFile;
int imp_iso_find(FILE* f, const char* path, int64_t* off, uint32_t* size);
int64_t imp_read_at(FILE* f, int64_t off, void* buf, size_t n);
AfsFile* imp_afs_list(FILE* f, int64_t base, uint64_t size, int* count);

#ifdef __cplusplus
}
#endif
