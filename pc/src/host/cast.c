/* The cutscene cast randomizer's choices (cast.h; docs/ACTORS.md "Cutscene cast"). */
#include "cast.h"
#include "actors.h"
#include <string.h>

/* the characters' cutscene models (a face mask: their faces move with the lip sync), in character order */
static const signed char cine[CAST_CHARACTERS] = { 0, 1, 3, 5, 8, 12, 13 };

int cast_character(int id, int ver)
{
    (void)ver;
    switch (id) {
    case 91: return 0;                       /* Claire */
    case 92: case 42: return 1;              /* Chris */
    case 93: case 43: return 2;              /* Steve */
    case 97: case 49: case 47: return 3;     /* Wesker */
    case 94: case 96: case 44: return 4;     /* Alfred (16, his boss type, is not cast) */
    case 95: return 5;                       /* Alexia (12..14, her forms, are not) */
    case 98: return 6;                       /* Rodrigo */
    default: return -1;
    }
}

static int listed(void) { return __atomic_load_n(&actor_import_n, __ATOMIC_ACQUIRE); }   /* (the main thread appends) */
static int imports_n(int imports) { return imports > 0 && imports <= listed() ? imports : 0; }

/* a zombie look (the zombies' 27 looks, the scripted ones too; not the anatomist): in "All models" they are one entry,
   else they were most of the pool and most characters became zombies */
static int zombie(int i) { return actor_models[i].id == 1 || actor_models[i].id == 51 || actor_models[i].id == 52; }

int cast_zombie_look(unsigned seed, unsigned scene, int c)
{
    unsigned h = (seed * 2654435761u) ^ (scene * 40503u + (unsigned)c * 0x9E3779B9u + 0x7F4A7C15u);
    int i, n = 0, k;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    for (i = 0; i < ACTOR_MODELS; i++) n += zombie(i);
    k = n ? (int)(h % (unsigned)n) : -1;
    for (i = 0; i < ACTOR_MODELS && k >= 0; i++)
        if (zombie(i) && k-- == 0) return i;
    return -1;
}

/* Claire's and Chris's looks (their cutscene models, their player files, Claire's second costume) are never picked: they
   are the characters the story is played as, whose look "You appear as" decides. Picked for someone else they read as
   the player character: Steve at the end of Claire's part (6100, just before the switch to Chris) shown as Chris's
   cutscene model looked like Chris's part starting with a random Chris instead of the chosen model. */
static int protagonist(int m) { return m == cine[0] || m == cine[1] || m == ACTOR_PART + 0 || m == ACTOR_PART + 1 || m == ACTOR_PART + 4; }

int cast_pool(int all, int imports, int* out, int max)
{
    int n = 0, i;
    if (!all) {
        for (i = 0; i < CAST_CHARACTERS && n < max; i++) if (!protagonist(cine[i])) out[n++] = cine[i];
    } else {   /* every humanoid look the menus list, the player files too; not the wounded Alfred (a special state) */
        for (i = 0; i < 5 && n < max; i++) if (!protagonist(ACTOR_PART + i)) out[n++] = ACTOR_PART + i;
        for (i = 0; i < ACTOR_MODELS && n < max; i++)
            if (actor_models[i].anim != ANIM_IDLE && appear_canon(COOP_CHR_ACTOR + i) == COOP_CHR_ACTOR + i && i != 10 && i != 14 && !zombie(i) && !protagonist(i)) out[n++] = i;   /* (14 the man in the hazmat suit: no moving mouth) */
        if (n < max) out[n++] = CAST_ZOMBIES;   /* the zombie looks: one entry (cast_model picks the look) */
    }
    for (i = 0; i < imports_n(imports) && n < max; i++) out[n++] = ACTOR_MODELS + i;
    return n;
}

/* the story character a model shows (a pick of the same one is no change), -1 */
static int model_char(int m)
{
    static const signed char part[5] = { 0, 1, 2, 3, 0 };   /* player files: Claire, Chris, Steve, Wesker, Claire's second costume */
    if (m >= ACTOR_PART && m < ACTOR_PART + 5) return part[m - ACTOR_PART];
    for (int c = 0; c < CAST_CHARACTERS; c++) if (cine[c] == m) return c;
    return -1;
}

static unsigned rng;
static unsigned rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }   /* xorshift32, as the enemy randomizer's */

#define POOL_MAX (ACTOR_MODELS + 5 + ACTOR_IMPORT_MAX)
static int pool[POOL_MAX], pick[CAST_CHARACTERS];
static unsigned char used[POOL_MAX];

int cast_model(unsigned seed, unsigned scene, int c, int all, int imports)
{
    int n = cast_pool(all, imports, pool, POOL_MAX), i, k, t;
    if (c < 0 || c >= CAST_CHARACTERS || n < 2) return -1;
    rng = (seed * 2654435761u) ^ (scene * 4u + 0x9E3779B9u);
    if (!rng) rng = 1;
    rnd(); rnd();
    for (i = n - 1; i > 0; i--) { k = (int)(rnd() % (unsigned)(i + 1)); t = pool[i]; pool[i] = pool[k]; pool[k] = t; }   /* Fisher-Yates */
    memset(used, 0, (size_t)n);
    /* every character in turn (all of them, so each answer is the same whichever is asked): the first model of the
       shuffle not taken and not its own; when only its own are left, it trades with an earlier character whose model
       is not its own and who may take one of those left; with the pool used up, the first not its own again */
    for (i = 0; i < CAST_CHARACTERS; i++) {
        pick[i] = -1;
        for (k = 0; k < n && pick[i] < 0; k++) if (!used[k] && model_char(pool[k]) != i) pick[i] = k;
        for (k = 0; k < n && pick[i] < 0; k++) {
            if (used[k]) continue;   /* a model left (its own): to an earlier character it suits */
            for (t = 0; t < i && pick[i] < 0; t++)
                if (model_char(pool[k]) != t && model_char(pool[pick[t]]) != i) { pick[i] = pick[t]; pick[t] = k; used[k] = 1; }
        }
        if (pick[i] < 0) {   /* the pool used up (more characters than models): a new round over it */
            for (k = 0; k < n && used[k]; k++);
            if (k == n) memset(used, 0, (size_t)n);
            for (k = 0; k < n && pick[i] < 0; k++) if (!used[k] && model_char(pool[k]) != i) pick[i] = k;
        }
        for (k = 0; k < n && pick[i] < 0; k++) if (model_char(pool[k]) != i) pick[i] = k;
        if (pick[i] < 0) return -1;
        used[pick[i]] = 1;
    }
    return pool[pick[c]] == CAST_ZOMBIES ? cast_zombie_look(seed, scene, c) : pool[pick[c]];   /* (the zombie entry: which look, from the same seed and scene) */
}

int cast_appear_pick(unsigned seed, int imports)
{
    int n = cast_pool(1, imports, pool, POOL_MAX), m;
    unsigned h = seed * 0x9E3779B1u + 0x632BE5ABu;
    h ^= h >> 16; h *= 0x85EBCA6Bu; h ^= h >> 13; h *= 0xC2B2AE35u; h ^= h >> 16;
    if (!n) return 0;
    m = pool[h % (unsigned)n];
    if (m == CAST_ZOMBIES) m = cast_zombie_look(seed, 0, CAST_CHARACTERS);   /* (as a character of its own) */
    if (m >= ACTOR_PART) return 1 + m - ACTOR_PART;                         /* player file n: chr 1 + n */
    if (m >= ACTOR_MODELS) return COOP_CHR_IMPORT + m - ACTOR_MODELS;
    return appear_canon(COOP_CHR_ACTOR + m);
}

unsigned cast_import_id(void)
{
    static int last = -1;
    static unsigned id;
    unsigned h = 2166136261u;
    int n = listed();
    if (n == last) return id;   /* (append-only: the count names the list) */
    last = n;
    if (!n) return id = 0;
    for (int k = 0; k < n; k++) {
        for (const char* s = actor_imports[k].name; s && *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
        h = (h ^ (0x100u | actor_import_group[k])) * 16777619u;
    }
    return id = h ? h : 1;
}
