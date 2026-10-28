/* The catalogue of pc/src/host/actors.h: where each enemy / NPC model is on the disc. Room and
   record were picked by a census over all room files (the smallest room holding the model and,
   for a model with its own motions, its type's motion set; docs/ACTORS.md "You appear as"). */
#include "actors.h"
#include <stddef.h>

#define MA ANIM_MALE    /* a man's (Chris's while Claire is played) */
#define FE ANIM_FEMALE  /* a woman's (Claire's while a man is played) */
#define I ANIM_IDLE     /* its own idle motion */

int g_appear_anim = 1;
int g_appear_face = 0;
int g_appear_cutscene = 1;

const ActorModel actor_models[] = {
    /* the story cast, moved by the player's motions (a man's or a woman's, section 8) */
    { "Claire (Cutscene Model)", 3150, 0, 91, 0, FE },
    { "Chris (Cutscene Model)", 9180, 2, 92, 0, MA },
    { "Chris (event model)", 9320, 1, 42, 0, MA },
    { "Steve (Cutscene Model)", 40, 0, 93, 0, MA },
    { "Steve (event model)", 1140, 0, 43, 0, MA },
    { "Wesker (Cutscene Model)", 9360, 2, 97, 0, MA },
    { "Wesker (event model)", 1002, 2, 49, 0, MA },
    { "Wesker (event model 2)", 8030, 0, 47, 0, MA },
    { "Alfred Ashford", 1020, 1, 94, 0, MA },
    { "Alfred Ashford 2", 3240, 2, 96, 0, MA },
    { "Alfred Ashford (Wounded)", 9180, 1, 96, 1, MA },
    { "Alfred Ashford (event model)", 7030, 2, 44, 0, MA },
    { "Alexia Ashford", 9150, 2, 95, 0, FE },
    { "Rodrigo Juan Raval", 0, 1, 98, 0, MA },
    { "Hazmat Suit Man", 3130, 2, 99, 0, MA },
    { "Alexia (First Form)", 5820, 1, 12, 0, FE },
    /* creatures: humanoids moved by the player's motions (a man's), the others playing their own idle */
    { "Nosferatu", 5800, 0, 15, 0, MA },
    { "Tyrant", 5840, 0, 19, 0, MA },
    { "Tyrant 2", 5010, 0, 19, 1, MA },
    { "Monster Steve", 9280, 0, 17, 0, MA },
    { "Hunter", 8010, 0, 5, 0, MA },
    { "Hunter (Sweeper)", 9130, 0, 5, 1, MA },
    { "Bandersnatch", 2000, 0, 9, 0, MA },
    { "Bandersnatch 2", 3130, 0, 9, 1, MA },
    { "Cerberus", 3020, 2, 4, 0, I },
    { "Anatomist Zombie", 110, 4, 26, 0, MA },
    { "Albinoid (Adult)", 7100, 0, 22, 0, I },
    { "Albinoid (Infant)", 3120, 0, 21, 0, I },
    { "Black Widow", 7080, 0, 3, 0, I },
    { "Giant Black Widow", 9050, 0, 23, 0, I },
    { "Moth", 6030, 3, 6, 0, I },
    { "Bat", 130, 0, 7, 0, I },
    { "Fish", 5510, 0, 8, 0, I },
    { "Spotter", 7081, 0, 11, 0, I },
    { "Ant", 9170, 12, 20, 0, I },
    { "Zombie: Prisoner", 5520, 0, 1, 0, MA },
    { "Zombie: Prisoner 2", 150, 0, 1, 1, MA },
    { "Zombie: Prisoner 3", 5520, 1, 1, 31, MA },
    { "Zombie: Soldier", 1070, 0, 1, 2, MA },
    { "Zombie: Soldier 2", 1070, 1, 1, 3, MA },
    { "Zombie: Soldier 3", 9120, 1, 1, 4, MA },
    { "Zombie: Soldier 4", 9310, 1, 1, 21, MA },
    { "Zombie: Soldier 5", 5680, 3, 1, 32, MA },
    { "Zombie: Soldier 6", 5680, 0, 1, 33, MA },
    { "Zombie: Soldier 7", 9310, 0, 1, 34, MA },
    { "Zombie: Lab Coat", 3200, 0, 1, 5, MA },
    { "Zombie: Lab Coat 2", 5520, 3, 1, 6, MA },
    { "Zombie: Worker", 3200, 2, 1, 7, MA },
    { "Zombie: Worker 2", 1100, 2, 1, 8, MA },
    { "Zombie: Frozen", 3060, 1, 1, 9, MA },
    { "Zombie: Frozen 2", 3060, 0, 1, 10, MA },
    { "Zombie: Base Staff", 6020, 1, 1, 35, MA },
    { "Zombie: Base Staff 2", 9020, 0, 1, 36, MA },
    { "Zombie: Base Staff 3", 5620, 1, 1, 42, MA },
    { "Zombie: Civilian", 9190, 1, 1, 37, MA },
    { "Zombie: Civilian 2", 9120, 0, 1, 38, MA },
    { "Zombie: Hazmat Suit", 3140, 7, 1, 39, MA },
    { "Zombie: Dark Uniform", 5700, 0, 1, 40, MA },
    { "Zombie: Dark Uniform 2", 8000, 3, 1, 41, MA },
    /* scripted zombies without motions of their own */
    { "Zombie: Prisoner 4", 3000, 2, 51, 0, MA },
    { "Zombie: Bandaged", 6040, 4, 52, 0, MA },
    { "Zombie: Bandaged 2", 9050, 1, 52, 1, MA },
};
_Static_assert(sizeof(actor_models) / sizeof(actor_models[0]) == ACTOR_MODELS, "ACTOR_MODELS");

_Static_assert(ACTOR_MODELS + ACTOR_IMPORT_MAX <= ACTOR_PART, "ACTOR_PART");
ActorModel actor_imports[ACTOR_IMPORT_MAX];
const char* actor_import_file[ACTOR_IMPORT_MAX];
unsigned char actor_import_group[ACTOR_IMPORT_MAX];
int actor_import_n;

static int imports(void) { return __atomic_load_n(&actor_import_n, __ATOMIC_ACQUIRE); }
static int import_of(int chr) { int k = chr - COOP_CHR_IMPORT; return k >= 0 && k < imports() ? k : -1; }   /* -1: not an import */

const ActorModel* actor_model(int idx)
{
    if (idx >= 0 && idx < ACTOR_MODELS) return &actor_models[idx];
    idx -= ACTOR_MODELS;
    return idx >= 0 && idx < imports() ? &actor_imports[idx] : NULL;
}

/* ---------------------------------------------------------------- gender-matched animations */
int appear_motion_chr(int anim, int played)
{
    if (!g_appear_anim) return -1;
    if (anim == ANIM_MALE && played == 0) return 1;      /* Chris's for a man while Claire is played */
    if (anim == ANIM_FEMALE && played != 0) return 0;    /* Claire's for a woman while a man is played */
    return -1;
}

int appear_part_anim(int file) { return file == 0 || file == 4 ? ANIM_FEMALE : ANIM_MALE; }

/* ---------------------------------------------------------------- voices
   The player's vocal sounds (SE bank 4: hit 1024/1026/1027, death 1025, grabs) come from
   CORE_<ply_id>.SPQ; the disc has one per player character (Claire, Chris, Steve, Wesker, same 4
   programs each). The story cast speaks with its own, other men with Chris's, women with Claire's;
   creatures that play their own idle motion keep the played character's (docs/ACTORS.md "Voices"). */
int appear_voice(int chr)
{
    static const signed char files[COOP_CHR_ACTOR] = { -1, 0, 1, 2, 3, 0 };
    int m = chr - COOP_CHR_ACTOR;
    if (import_of(chr) >= 0) return actor_imports[import_of(chr)].anim == ANIM_FEMALE ? 0 : 1;
    if (chr < 0 || chr > COOP_CHR_MAX) return -1;
    if (m < 0) return files[chr];
    if (m >= 3 && m <= 4) return 2;   /* Steve's doubles */
    if (m >= 5 && m <= 7) return 3;   /* Wesker's */
    if (m == 19) return 2;            /* Monster Steve */
    return actor_models[m].anim == ANIM_MALE ? 1 : actor_models[m].anim == ANIM_FEMALE ? 0 : -1;
}

/* Enemies that grab the player play the player's grab motions from their own set, at a first
   motion per character (ply_id 0..3, from the enemy code's tables: en01.c En01_PlyMtn_OffsetTbl,
   en02.c / en13.c / en23.c mtn[3][8], en03.c 14 * ply_id, en04.c / en09.c *_PlyMtn_OffsetTbl,
   en07.c ply_motion; Wesker's row where the code has none: Chris's). */
static const struct { unsigned char id; short first[4]; } grabs[] = {
    { 1, { 400, 425, 450, 425 } },   /* zombie */
    { 2, { 21, 29, 29, 29 } },       /* gulp worm */
    { 3, { 0, 14, 28, 42 } },        /* black widow */
    { 4, { 100, 105, 100, 105 } },   /* cerberus */
    { 7, { 30, 33, 36, 33 } },       /* bat */
    { 9, { 100, 110, 120, 110 } },   /* bandersnatch */
    { 13, { 10, 16, 16, 16 } },      /* alexia (second form) */
    { 23, { 60, 66, 66, 66 } },      /* giant black widow */
};

int appear_grab_motion(int id, int mtn, int from, int to)
{
    for (int i = 0; i < (int)(sizeof(grabs) / sizeof(grabs[0])); i++)
        if (grabs[i].id == id && from >= 0 && from < 4 && to >= 0 && to < 4) {
            int k = mtn + grabs[i].first[to] - grabs[i].first[from];
            return mtn >= grabs[i].first[from] && k >= 0 && k < 512 ? k : -1;
        }
    return -1;
}

/* ---------------------------------------------------------------- "You appear as" list
   Left out of the menus (docs/ACTORS.md "The list"): event doubles that look like an entry
   that stays. Their chr values keep working; each maps to the one kept. */
#define M(n) (COOP_CHR_ACTOR + (n))   /* chr of catalogue entry n */
static const struct { short hidden, kept; } twins[] = {
    { M(2), 2 }, { M(4), 3 },            /* Chris's and Steve's event models */
    { M(6), 4 }, { M(7), 4 },            /* Wesker's (the doubles wear the glasses as a separate prop) */
    { M(9), M(8) }, { M(11), M(8) },     /* Alfred's other uniforms: the same look */
    { M(18), M(17) },                    /* Tyrant 2: the same with a blood stain */
    { M(23), M(22) },                    /* Bandersnatch 2: only its long arm (an event piece) */
};
/* listed last, after the zombies: the player characters' cutscene models (else each would follow
   the same character's player file) */
static const short tail[] = { M(0), M(1), M(3), M(5) };
#define TAILS ((int)(sizeof(tail) / sizeof(tail[0])))

_Static_assert(COOP_CHR_CINE >= COOP_CHR_IMPORT + ACTOR_IMPORT_MAX, "COOP_CHR_CINE");

int appear_played(int chr, int ply_id)
{
    static const signed char cine[4] = { M(0), M(1), M(3), M(5) };   /* Claire, Chris, Steve, Wesker */
    if (chr != COOP_CHR_CINE) return chr;
    return ply_id >= 0 && ply_id < 4 ? cine[ply_id] : 0;
}

int appear_canon(int chr)
{
    if (import_of(chr) >= 0 || chr == COOP_CHR_CINE) return chr;
    if (chr < 0 || chr > COOP_CHR_MAX) return 0;
    for (int i = 0; i < (int)(sizeof(twins) / sizeof(twins[0])); i++)
        if (chr == twins[i].hidden) return twins[i].kept;
    return chr;
}

static int tail_at(int chr)
{
    for (int i = 0; i < TAILS; i++) if (tail[i] == chr) return i;
    return -1;
}

static int head(int c) { return appear_canon(c) == c && tail_at(c) < 0; }   /* listed before the tail */

/* the list: "As played", "As played (cutscene)", the other heads, the tail, then the imported models */
int appear_count(void)
{
    int n = 1;
    for (int c = 0; c <= COOP_CHR_MAX; c++) n += appear_canon(c) == c;
    return n + imports();
}

int appear_at(int i)
{
    if (i == 1) return COOP_CHR_CINE;
    if (i > 1) i--;
    for (int c = 0; c <= COOP_CHR_MAX; c++)
        if (head(c) && i-- == 0) return c;
    if (i >= 0 && i < TAILS) return tail[i];
    i -= TAILS;
    return i >= 0 && i < imports() ? COOP_CHR_IMPORT + i : 0;
}

int appear_pos(int chr)
{
    int n = 0, t = tail_at(chr = appear_canon(chr)), k = import_of(chr);
    if (chr == COOP_CHR_CINE) return 1;
    for (int c = 0; c < (t >= 0 || k >= 0 ? COOP_CHR_MAX + 1 : chr); c++) n += head(c);
    n += (t >= 0 ? t : 0) + (k >= 0 ? TAILS + k : 0);
    return n + (n > 0);   /* (everything but "As played" after "As played (cutscene)") */
}

int appear_step(int chr, int dir)
{
    int n = appear_count();
    return appear_at((appear_pos(chr) + dir + n) % n);
}

/* in list order: the player files, the story cast, the enemies, the player characters' cutscene
   models (the tail), then the imports by their group */
const char* appear_group(int chr)
{
    static const char* const imp[4] = { "Outbreak Characters", "Outbreak Cutscenes", "Outbreak Supporting Cast", "Outbreak Enemies" };
    int k = import_of(chr), id;
    if (k >= 0) return imp[actor_import_group[k] & 3];
    chr = appear_canon(chr);
    if (chr < COOP_CHR_ACTOR || chr == COOP_CHR_CINE) return "Code: Veronica Characters";
    if (tail_at(chr) >= 0) return "Code: Veronica Cutscenes";
    id = actor_models[chr - COOP_CHR_ACTOR].id;
    return id > 30 && id != 51 && id != 52 ? "Code: Veronica Cast" : "Code: Veronica Enemies";   /* (51, 52: zombies) */
}

const char* appear_name(int chr)
{
    static const char* files[COOP_CHR_ACTOR] = { "Current Character", "Claire", "Chris", "Steve", "Wesker", "Claire (Alternate Costume)" };
    if (import_of(chr) >= 0) return actor_imports[import_of(chr)].name;
    if (chr == COOP_CHR_CINE) return "Current Character (Cutscene)";
    return chr >= 0 && chr < COOP_CHR_ACTOR ? files[chr] : chr <= COOP_CHR_MAX ? actor_models[chr - COOP_CHR_ACTOR].name : "?";
}
