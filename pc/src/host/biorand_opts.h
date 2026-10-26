/* BioRand's Code: Veronica options (docs/BIORAND_INGAME.md, S4): the config BioRand's seed string
   carries, its encoder and decoder (RandoConfig.ToString / FromString, BioRand 3.3.2), the table of
   rows the in-game generator shows, and the item and enemy distributions it draws as bars.
   The table is the one source (it replaced S1's biorand_options.json); labels and help texts are
   BioRand's where it has them (MIT, Copyright (c) 2022-2026 Ted John; THIRD_PARTY_NOTICES.md).
   Pure code without SDL; checked by pc/tests/test_biorand_opts.c. */
#ifndef RECVX_BIORAND_OPTS_H
#define RECVX_BIORAND_OPTS_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#define BIORAND_SEED_LEN 68        /* "R840-XXXX-" + 58 characters (290 bits) */
#define BIORAND_SEED_CAP 72
#define BIORAND_SEED_MAX 1048575   /* 20 bits */
#define BIORAND_VERSION 8          /* the seed-string version this code reads and writes */
#define BIORAND_GAME_CVX 4

/* Every field of RandoConfig's seed string, in BioRand's names. One int each, so a row is a
   pointer; flags are 0/1. A decoded string keeps every field, the hidden ones too, so it re-encodes
   byte for byte. */
typedef struct BioOpts {
    int Version, Game, GameVariant, Seed;
    int Segmented, RandomDoors, RandomNPCs, RandomEnemies, RandomItems;
    int RandomBgm, ShuffleItems, AlternativeRoutes, IncludeDocuments, ChangePlayer;
    int RatioAmmo, RatioHealth, RatioInkRibbons, RatioGunpowder;   /* 0..31 */
    int EnabledWeapons[15];                                         /* CVX uses 12; 12-14 stay 1 */
    int AmmoQuantity, EnemyDifficulty, EnemyRooms, HiddenKeyItems;
    int AreaCount, AreaSize, Weapon0, Weapon1, PrioritiseCutscenes;
    int Player0, Player1, SwapCharacters;
    int RandomInventory, RandomEnemyPlacement, AllowEnemiesAnyRoom;
    int RandomCutscenes, AllowAnyVoice, ReduceSilences, RandomEvents, RandomEnemySkins;
    int EnemyRatios[12];                                            /* 0..7; CVX uses 6; 6-11 stay 7 */
    int EnemyQuantity;
    int EnabledNPCs[100], EnabledBGMs[20], EnabledEnemySkins[35];
} BioOpts;

/* new RandoConfig { Game = 4 }: what biorand-cli uses for "-s <number>" without a saved seed */
void biorand_opts_default(BioOpts* o);
/* what our menu always writes: the fields CVX forces anyway (Version 8, Game 4, no player change,
   NPCs, cutscenes, events, music or documents; the unused weapon and enemy slots as the GUI writes
   them), and the GUI's door rule (doors on: items on, segmented, custom item pool). Then the CLI's
   log seed equals our string and it prints no warnings. set() runs it after each change. */
void biorand_opts_rules(BioOpts* o);

/* out gets the 68-character string and a NUL; -1 if n is too small. Values are masked to their
   bits, as BioRand does. */
int biorand_seed_encode(const BioOpts* o, char* out, size_t n);
/* NULL and *o filled, or a reason for the screen (o untouched): not a seed string, a version other
   than 8, another game, a wrong length, a character outside BioRand's alphabet. Leading and trailing
   spaces are ignored, lower case is accepted. Values are exactly FromString's (GameVariant capped
   at 1, the unused bits dropped). The format has no checksum. The reason is a static buffer for
   the version and position messages: one thread (the game's). */
const char* biorand_seed_decode(const char* in, BioOpts* o);

/* ---- the rows the generator shows ---- */
enum { BOG_SEED, BOG_DOORS, BOG_ITEMS, BOG_ENEMIES, BOG_COUNT };
extern const char* const biorand_groups[BOG_COUNT];   /* "Seed", "Doors", "Items", "Enemies" */

enum {
    BO_BOOL,    /* 0/1; with BOF_INVERT shown on when the field is 0 */
    BO_RANGE,   /* lo..hi; a slider with the ends named low/high (Seed: a number) */
    BO_ENUM,    /* names[v] */
    BO_RATIO,   /* lo..hi, a share of a distribution bar (biorand_item_dist / biorand_enemy_dist) */
};
enum {
    BOF_INVERT = 1,     /* Custom item pool: on = ShuffleItems 0 */
    BOF_DOORS = 2,      /* locked while Randomize Doors is on (the GUI greys or forces it) */
    BOF_TOGGLE = 4,     /* a box: rows naming it as parent are inactive while it is off */
};

typedef struct BioOpt {
    const char* key;            /* RandoConfig's name, [i] for an array entry */
    int group;                  /* BOG_* */
    int type;                   /* BO_* */
    int off;                    /* the int in BioOpts (byte offset) */
    int lo, hi, def;            /* range; def as BioRand's default (shown value) */
    int flags;                  /* BOF_* */
    const char* parent;         /* the key of the BOF_TOGGLE row that enables it, or NULL */
    const char* label;
    const char* help;           /* NULL: none */
    const char* low, * high;    /* BO_RANGE / BO_RATIO: the slider's ends */
    const char* const* names;   /* BO_ENUM */
} BioOpt;

extern const BioOpt biorand_opt[];
extern const int biorand_opt_n;

int  biorand_opt_find(const char* key);                 /* the row index, or -1 */
int  biorand_opt_get(const BioOpts* o, int i);          /* the shown value (inverted for BOF_INVERT) */
void biorand_opt_set(BioOpts* o, int i, int v);         /* clamped to lo..hi, then biorand_opts_rules */
int  biorand_opt_active(const BioOpts* o, int i);       /* every parent box is on (else the GUI greys it; we may hide it) */
const char* biorand_opt_locked(const BioOpts* o, int i);/* why it cannot change now (doors on: set() ignores it), or NULL */

/* ---- distributions, for the bars ---- */
typedef struct BioSlice { const char* name; unsigned rgb; double pct; } BioSlice;
/* Keys, Gunpowder, Ammo, Health, Ink (MainWindow.UpdateItemPie): Keys 1/8, the other 7/8 split by
   the four ratios; all ratios 0: Keys alone. Always 5 slices (zero ones included), summing to 100. */
int biorand_item_dist(const BioOpts* o, BioSlice out[5]);
/* the 6 CVX enemies with a ratio above 0 (UpdateEnemyPie), in table order, summing to 100; 0 when
   every ratio is 0 */
int biorand_enemy_dist(const BioOpts* o, BioSlice out[6]);
/* the doors group's "Estimate completion time" (UpdateEstimateCompletion), "" with doors off */
void biorand_doors_estimate(const BioOpts* o, char* out, size_t n);

/* ---- recvx.ini: one key, biorand_seed (config.c) ---- */
extern char g_biorand_seed[BIORAND_SEED_CAP];
/* ini biorand_appear: "Randomize Played as" (the BIORAND screen's PORT EXTRAS page; S5 picks the look
   once per generated seed). Not part of the seed string. */
extern int g_biorand_appear;
/* *o from g_biorand_seed, rules applied; empty or invalid: defaults + rules (invalid: a log line) */
void biorand_opts_load(BioOpts* o);
/* g_biorand_seed from *o, and recvx.ini written soon (settings_save_later) */
void biorand_opts_save(const BioOpts* o);

#ifdef __cplusplus
}
#endif
#endif
