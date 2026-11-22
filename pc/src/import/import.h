/* Importing assets from other games' discs (F9 window; docs/IMPORT_OUTBREAK.md) and other releases of this
   game (languages; docs/LANGUAGES.md). The user's own disc image is read in place; what is extracted goes to
   <dir>/<source>/ (default mods/import), never into the repository. Sources: Resident Evil Outbreak File #2
   (SLUS-20984, characters), Resident Evil: The Umbrella Chronicles (RBUE08, characters), Code: Veronica's Dreamcast release
   (T-1204N, Steve's cutscene model; docs/IMPORT_DREAMCAST.md); languages from Resident Evil Code: Veronica X European (SLES-50306), the US disc with
   the Russian fan translation by Brosko (SLUS-20184), Biohazard Code: Veronica Kanzenban (SLPM-65022), the European
   disc with the Turkish fan translation by Patriots Çeviri (SLES-50306), the US disc with the Brazilian Portuguese
   fan translation by gledson999 (SLUS-20184), the European disc with the Arabic fan translation by Abdulaziz Al-Amri
   (SLES-50306), the Japanese disc with the Korean fan translation (SLPM-65022) and the European disc with the Italian
   fan translation by KimeraVX (SLES-50306). */
#pragma once
#include <stdint.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif

enum { IMPORT_OUTBREAK2, IMPORT_SLES50306, IMPORT_SLUS20184RU, IMPORT_SLPM65022, IMPORT_SLES50306TR, IMPORT_SLUS20184PT, IMPORT_SLES50306AR,
       IMPORT_SLPM65022KO, IMPORT_SLES50306IT, IMPORT_UMBRELLA, IMPORT_DREAMCAST, IMPORT_SOURCES };   /* (new ones at the end: g_import_hidden's bits) */
enum { IMPORT_CAT_CHARACTERS, IMPORT_CAT_LANGUAGES };   /* what a source gives (the IMPORT screen's categories) */

/* the groups of models (F9 lists them by group), and what to extract: bits 1 << group (F9 checkboxes,
   RECVX_IMPORT_WHAT) */
enum { IMPORT_GROUP_PLAYERS, IMPORT_GROUP_CUTSCENE, IMPORT_GROUP_NPCS, IMPORT_GROUP_ENEMIES, IMPORT_GROUPS };
enum { IMPORT_PLAYERS = 1, IMPORT_CUTSCENE = 2, IMPORT_NPCS = 4, IMPORT_ENEMIES = 8, IMPORT_ALL = 15 };
#define IMPORT_VOICES 16   /* not a group of models: Umbrella Chronicles' voice lines (uc_voice.c; ImportSource.offered) */
/* the groups offered for now (Outbreak's; each source has its own, ImportSource.offered): F9's checkboxes and table, what
   import_run extracts and what "You appear as" lists (a manifest of an older run may hold the others: left out). The
   cutscene / high-detail versions and the enemies work the same way; IMPORT_ALL here brings them back. */
#define IMPORT_OFFERED (IMPORT_PLAYERS | IMPORT_NPCS)

typedef struct ImportSettings {
    char dir[1024];                     /* ini import_dir; empty: <mods>/import (RECVX_MODS, default ./mods) */
    char iso[IMPORT_SOURCES][1024];     /* ini import_<key>_iso */
    int dc_steve;                       /* ini import_dreamcast_steve, "Dreamcast Steve": DC_STEVE_* (docs/IMPORT_DREAMCAST.md) */
} ImportSettings;
/* "Dreamcast Steve": Off; Cutscenes (his records drawn as the Dreamcast models: cast_glue.c own_look); Cutscenes and
   Gameplay (also played as the Dreamcast Steve: actor_glue.c pc_part_file) */
enum { DC_STEVE_OFF, DC_STEVE_CUTSCENES, DC_STEVE_GAMEPLAY };
extern ImportSettings g_import;

/* one extracted model (a line of <dir>/<source>/manifest.txt) */
typedef struct ImportItem {
    char id[16];          /* the disc's file name without extension, e.g. P00_00 */
    char name[48];        /* e.g. Kevin Ryman */
    char variant[32];     /* e.g. Costume 1 */
    int ok;               /* glTF written */
    int nodes, meshes, verts, tris, textures;
    int player;           /* 1: converted for "You appear as" (<id>.cvxm), 2: male, female in `female`; 3: in the game's own
                             format, a room file holding it (<id>.rdx, dc.c) */
    int female;
    int group;            /* IMPORT_GROUP_* (from the id) */
    char note[160];       /* failure reason, or what was left out */
} ImportItem;

typedef struct ImportSource {
    const char* key;      /* folder and ini name (import_<key>_iso): outbreak2 */
    const char* title;    /* Resident Evil Outbreak File #2 */
    const char* serial;   /* SLUS-20984 (checked in SYSTEM.CNF) */
    int cat;              /* IMPORT_CAT_* */
    const char* help;     /* the IMPORT screen's line for it (NULL: the category's) */
    const char* need;     /* its DISC line, a format with the serial (NULL: the category's) */
    int offered;          /* a character source: the groups offered (IMPORT_* bits: its checkboxes, what is extracted and listed) */
    const char* ext;      /* the disc image's file types for the file dialogs: "rvz", "cue;gdi;chd;cd1" (NULL: iso) */
    const char* groups[IMPORT_GROUPS];   /* its groups' headings in the model lists (actors.c appear_group) */
} ImportSource;
extern const ImportSource import_sources[IMPORT_SOURCES];

/* progress of a running extraction (written by its thread, read by the overlay) */
typedef struct ImportProgress {
    volatile int running, done, total, failed;
    volatile int result;  /* the last run's import_run result, set before running drops to 0 */
    char phase[160];      /* what is being done (always 0-terminated); after a run its summary or why the disc is unusable */
} ImportProgress;
extern ImportProgress g_import_progress;

/* the file dialogs' filter for src's disc images (SDL_DialogFileFilter): its name, its pattern */
const char* import_filter_name(int src);
const char* import_filter_pattern(int src);

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

/* source-specific extractors (ob2.c, uc.c, lang.c) */
int ob2_group(const char* id);   /* IMPORT_GROUP_* of a disc file name, -1: not a character model */
int uc_group(const char* id);
int uc_extract(const char* rvz, const char* outdir, int what, ImportItem* items, int max, int* n);
int ob2_extract(const char* iso, const char* outdir, int what, ImportItem* items, int max, int* n);
int dc_extract(const char* image, const char* outdir, int what, ImportItem* items, int max, int* n);
int dc_model(const char* id, unsigned char* type, unsigned char* ver);   /* the record type and model version of a Dreamcast model; 0 none, 2
    one standing in for its type's records only (not listed: actors.h ACTOR_GROUP_UNLISTED) */
#define DC_PART_ID "STEVE_PLAYER"   /* the Dreamcast Steve's player part file (dc.c): <dreamcast>/STEVE_PLAYER/STEVE_PLAYER.part */
int dc_part_path(char* out, int n);   /* its path; 1 if it is there (game thread too) */
/* Dreamcast GD-ROM images (gdrom.c): a CUE or GDI sheet or a CHD (v5) image; a sector's 2048 bytes of user data by its
   address (the high-density area from 45000); a file of the ISO 9660 file system of the high-density area by path, or an
   entry of an AFS archive there ("SYSTEM.AFS/12") (malloc'd). gd_open: NULL with why in err */
typedef struct GdDisc GdDisc;
GdDisc* gd_open(const char* path, char* err, int en);
int gd_read(GdDisc* d, uint32_t lba, unsigned char* out);
unsigned char* gd_file(GdDisc* d, const char* path, uint32_t* size);
void gd_close(GdDisc* d);
/* (the CHD codecs' decoders and the PVR decoder, for the tests: 1 if exactly outn bytes came out; RGBA8, malloc'd) */
int gd_lzma(const unsigned char* in, size_t inn, unsigned char* out, size_t outn);       /* raw LZMA, lc 3 lp 0 pb 2 */
int gd_inflate(const unsigned char* in, size_t inn, unsigned char* out, size_t outn);    /* raw deflate */
unsigned char* dc_pvr_rgba(const unsigned char* pvrt, uint32_t n, int* w, int* h, char* why, int wn);
int lang_extract(int src, const char* iso, const char* outdir);   /* languages: how many failed, -1 the disc is unusable */
/* lang.c: a language's SYSMES.ALD (l, *n bytes) in the US order (us, nus bytes; fstbl / fsheader the US file tables), malloc'd
   (*n bytes); its file tables fl, hl moved to its pages appended after the US messages in hl_out; NULL: an unknown layout */
unsigned char* lang_sysmes(const unsigned char* us, uint32_t nus, const unsigned char* l, uint32_t* n,
                           const unsigned* fl, const unsigned* hl, unsigned* hl_out);

/* disc reading (ob2.c): a file of the ISO 9660 image (`path` with '/' for folders, any case); bytes at an offset;
   the files of an AFS archive at `base` (malloc'd; names from its name table or "#i"; an entry outside: size 0) */
typedef struct AfsFile { int64_t off; uint32_t size; char name[32]; } AfsFile;
int imp_iso_find(FILE* f, const char* path, int64_t* off, uint32_t* size);
int64_t imp_read_at(FILE* f, int64_t off, void* buf, size_t n);
AfsFile* imp_afs_list(FILE* f, int64_t base, uint64_t size, int* count);
/* Wii disc images in Dolphin's RVZ format (rvz.c): open (id: the disc's 6-character id, also when it is refused; NULL: not an
   RVZ image of zstd or stored groups), a file of the game partition by path (malloc'd), a file of a U8 archive by name */
typedef struct RvzDisc RvzDisc;
RvzDisc* rvz_open(const char* path, char* id, int idn);
unsigned char* rvz_file(RvzDisc* d, const char* path, uint32_t* size);
void rvz_close(RvzDisc* d);
int u8_find(const unsigned char* d, uint32_t n, const char* name, const unsigned char** out, uint32_t* size);

/* Umbrella Chronicles' voice lines (uc_voice.c; docs/IMPORT_UMBRELLA.md "Voice lines"): <outdir>/voice/<n>.adx, voices.txt,
   voices_text.txt from the open disc; how many failed, -1 none readable. uc_voice_guess: the speaker guesses of n lines (id,
   text, f0 set; sorted by id); uc_voice_f0: a line's median pitch */
int uc_voice_extract(RvzDisc* d, const char* outdir);
typedef struct UcVoiceLine {
    char id[48], text[600];
    const char *who, *conf, *kind;   /* a name or "-"; sure, pair, guess, weak, none; line, grunt, radio, npc */
    char scen[8];                    /* "10", "CV3", "-" */
    int ms, bank, pair;
    float f0;                        /* the median pitch in Hz (uc_voice_f0), 0 unknown */
} UcVoiceLine;
void uc_voice_guess(UcVoiceLine* l, int n);
float uc_voice_f0(const short* x, int n, int rate);
unsigned char* uc_adx_encode(const short* x, int n, int rate, size_t* size);   /* mono 16-bit to a standard ADX file (malloc'd, *size bytes) */
/* the voices.txt of the import (n | id | ...; its path), for the game (host/voices.c voice_import_load) */
void import_voices_path(char* out, int n);

#ifdef __cplusplus
}
#endif
