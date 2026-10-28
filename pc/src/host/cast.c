/* The cutscene cast randomizer's choices (cast.h; docs/ACTORS.md "Cutscene cast"). */
#include "cast.h"
#include "actors.h"
#include <stdio.h>
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

/* "All models" without the imports: every humanoid look the menus list, the player files too; not the wounded Alfred
   (a special state) */
static int builtins(int* out)
{
    int n = 0, i;
    for (i = 0; i < 5; i++) if (!protagonist(ACTOR_PART + i)) out[n++] = ACTOR_PART + i;
    for (i = 0; i < ACTOR_MODELS; i++)
        if (actor_models[i].anim != ANIM_IDLE && appear_canon(COOP_CHR_ACTOR + i) == COOP_CHR_ACTOR + i && i != 10 && i != 14 && !zombie(i) && !protagonist(i)) out[n++] = i;   /* (14 the man in the hazmat suit: no moving mouth) */
    out[n++] = CAST_ZOMBIES;   /* the zombie looks: one entry (cast_model picks the look) */
    return n;
}
#define BUILTIN_MAX (5 + ACTOR_MODELS + 1)

int cast_pool(int all, int imports, int* out, int max, const unsigned char* off)
{
    int b[BUILTIN_MAX], n = 0, i, k;
    if (!all) {
        for (i = 0; i < CAST_CHARACTERS && n < max; i++) if (!protagonist(cine[i])) out[n++] = cine[i];
    } else
        for (i = 0, k = builtins(b); i < k && n < max; i++) out[n++] = b[i];
    for (i = 0; i < imports_n(imports) && n < max; i++) out[n++] = ACTOR_MODELS + i;
    if (all == 2) {   /* "Custom": the checked ones (the positions are "All models"'s) */
        for (i = k = 0; i < n; i++) if (!cast_off_at(off, i)) out[k++] = out[i];
        n = k;
    }
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

int cast_model(unsigned seed, unsigned scene, int c, int all, int imports, const unsigned char* off)
{
    int n = cast_pool(all, imports, pool, POOL_MAX, off), i, k, t;
    if (c < 0 || c >= CAST_CHARACTERS || n < 1) return -1;   /* ("Custom" with nothing checked: nobody changes) */
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
                if (pick[t] >= 0 && model_char(pool[k]) != t && model_char(pool[pick[t]]) != i) { pick[i] = pick[t]; pick[t] = k; used[k] = 1; }
        }
        if (pick[i] < 0) {   /* the pool used up (more characters than models): a new round over it */
            for (k = 0; k < n && used[k]; k++);
            if (k == n) memset(used, 0, (size_t)n);
            for (k = 0; k < n && pick[i] < 0; k++) if (!used[k] && model_char(pool[k]) != i) pick[i] = k;
        }
        for (k = 0; k < n && pick[i] < 0; k++) if (model_char(pool[k]) != i) pick[i] = k;
        if (pick[i] >= 0) used[pick[i]] = 1;   /* (none: a "Custom" pool of only its own looks; it stays itself) */
    }
    if (pick[c] < 0) return -1;
    return pool[pick[c]] == CAST_ZOMBIES ? cast_zombie_look(seed, scene, c) : pool[pick[c]];   /* (the zombie entry: which look, from the same seed and scene) */
}

/* the "You appear as" chr of model m (a player file, a catalogue entry, an import; not CAST_ZOMBIES) */
static int chr_of(int m)
{
    if (m >= ACTOR_PART) return 1 + m - ACTOR_PART;                         /* player file n: chr 1 + n */
    if (m >= ACTOR_MODELS) return COOP_CHR_IMPORT + m - ACTOR_MODELS;
    return appear_canon(COOP_CHR_ACTOR + m);
}

int cast_appear_pick(unsigned seed, int imports)
{
    int n = cast_pool(1, imports, pool, POOL_MAX, NULL), m;
    unsigned h = seed * 0x9E3779B1u + 0x632BE5ABu;
    h ^= h >> 16; h *= 0x85EBCA6Bu; h ^= h >> 13; h *= 0xC2B2AE35u; h ^= h >> 16;
    if (!n) return 0;
    m = pool[h % (unsigned)n];
    if (m == CAST_ZOMBIES) m = cast_zombie_look(seed, 0, CAST_CHARACTERS);   /* (as a character of its own) */
    return chr_of(m);
}

/* ---------------------------------------------------------------- "Custom" */
unsigned char cast_off[CAST_OFF_BYTES];

int cast_custom_list(int imports, short* pos, int* model, int max)
{
    int b[BUILTIN_MAX], nb = builtins(b), n = 0, zombies = 0, i, j, k, m;
    for (i = 0; i < appear_count() && n < max; i++) {   /* the built-in ones in the list's order */
        int chr = appear_at(i);
        if (chr >= COOP_CHR_IMPORT && chr != COOP_CHR_CINE) break;
        m = chr >= 1 && chr <= 5 ? ACTOR_PART + chr - 1 : chr >= COOP_CHR_ACTOR && chr <= COOP_CHR_MAX ? chr - COOP_CHR_ACTOR : -1;
        if (m >= 0 && m < ACTOR_MODELS && zombie(m)) { if (zombies++) continue; m = CAST_ZOMBIES; }   /* (at the first zombie look's place) */
        for (j = 0; j < nb && b[j] != m; j++);
        if (j < nb) { pos[n] = (short)j; model[n++] = m; }
    }
    for (k = 0; k < imports_n(imports) && n < max; k++) { pos[n] = (short)(nb + k); model[n++] = ACTOR_MODELS + k; }
    return n;
}

const char* cast_entry_name(int m) { return m == CAST_ZOMBIES ? "Zombies (Random Look)" : appear_name(chr_of(m)); }
const char* cast_entry_group(int m) { return appear_group(m == CAST_ZOMBIES ? COOP_CHR_ACTOR + cast_zombie_look(0, 0, 0) : chr_of(m)); }

const char* cast_entry_key(int m, char* buf, int n)
{
    if (m >= ACTOR_MODELS && m < ACTOR_PART) {   /* an import: its folder (names repeat; .../<id>/<id>.cvxm) */
        const char* f = m - ACTOR_MODELS < listed() ? actor_import_file[m - ACTOR_MODELS] : NULL;
        const char* s = f ? strrchr(f, '/') : NULL;
        int l = s ? (int)strlen(s + 1) : 0;
        if (l > 5 && !strcmp(s + 1 + l - 5, ".cvxm")) l -= 5;
        snprintf(buf, (size_t)n, "import:%.*s", l, s ? s + 1 : "");
        return buf;
    }
    snprintf(buf, (size_t)n, "%s", m == CAST_ZOMBIES ? "Zombies" : appear_name(chr_of(m)));
    return buf;
}

int cast_off_key(const char* key)
{
    int b[BUILTIN_MAX], nb = builtins(b), j, imp = !strncmp(key, "import:", 7), n = imp ? listed() : nb;
    char k[128];
    for (j = 0; j < n; j++)
        if (!strcmp(cast_entry_key(imp ? ACTOR_MODELS + j : b[j], k, sizeof(k)), key)) { cast_off_put(cast_off, imp ? nb + j : j, 1); return 1; }
    return 0;
}

void cast_off_write(void (*line)(const char* key, void* ud), void* ud)
{
    int b[BUILTIN_MAX], nb = builtins(b), j;
    char k[128];
    for (j = 0; j < nb + listed(); j++)
        if (cast_off_at(cast_off, j)) line(cast_entry_key(j < nb ? b[j] : ACTOR_MODELS + j - nb, k, sizeof(k)), ud);
}

int cast_custom_count(const unsigned char* off, int imports, int* total)
{
    int b[BUILTIN_MAX], n = builtins(b) + imports_n(imports), j, on = 0;
    for (j = 0; j < n; j++) on += !cast_off_at(off, j);
    if (total) *total = n;
    return on;
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
