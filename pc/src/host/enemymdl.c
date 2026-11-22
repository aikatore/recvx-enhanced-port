/* "Enemy Models": categories, choices and picks (enemymdl.h; docs/ENEMIES.md "Enemy models"). No game state. */
#include "enemymdl.h"
#include "actors.h"
#include "../import/import.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_emodel[EM_CATS];

static const struct { const char *name, *key; unsigned char id; signed char ver; } cats[EM_CATS] = {
    [EM_ZOMBIES] = { "Zombies", "zombies", 1, -1 },          /* every look (model version) */
    [EM_ANATOMIST] = { "Anatomist Zombie", "anatomist", 26, -1 },
    [EM_HUNTER] = { "Hunter", "hunter", 5, -1 },              /* (model version 1: the Sweeper) */
    [EM_SWEEPER] = { "Sweeper", "sweeper", 5, 1 },
    [EM_BANDERSNATCH] = { "Bandersnatch", "bandersnatch", 9, -1 },
    [EM_TYRANT] = { "Tyrant", "tyrant", 19, -1 },
};
static const char* const rnames[EM_RANDOM_END] = {
    NULL, "Random Code: Veronica Zombie", "Random Umbrella Chronicles Zombie", "Random Zombie (Any Game)", "Random Code: Veronica Monster",
    "Random Umbrella Chronicles Monster", "Random Code: Veronica Character", "Random Umbrella Chronicles Character", "Random Outbreak Character",
    "Random Model (Any)" };
static const char* const rkeys[EM_RANDOM_END] = { NULL, "cvx_zombies", "uc_zombies", "zombies", "cvx_monsters", "uc_monsters", "cvx_characters",
                                                  "uc_characters", "outbreak", "any" };

int emodel_cat(int id, int mdlver)
{
    int c;
    if (id == 5) return mdlver == 1 ? EM_SWEEPER : EM_HUNTER;
    for (c = 0; c < EM_CATS; c++) if (cats[c].id == id) return c;
    return -1;
}

const char* emodel_cat_name(int c) { return c >= 0 && c < EM_CATS ? cats[c].name : ""; }

int emodel_any(const int* v)
{
    for (int c = 0; c < EM_CATS; c++) if (v[c]) return 1;
    return 0;
}

static int imports(void) { return __atomic_load_n(&actor_import_n, __ATOMIC_ACQUIRE); }

/* catalogue entry i can stand in for an enemy: posed from a skeleton (not a creature with its own idle), listed (not an event
   double's twin) */
static int cat_ok(int i) { return actor_models[i].anim != ANIM_IDLE && appear_canon(COOP_CHR_ACTOR + i) == COOP_CHR_ACTOR + i; }
static int cvx_zombie(int i) { return actor_models[i].id == 1 || actor_models[i].id == 51 || actor_models[i].id == 52; }
static int cvx_char(int i) { return actor_models[i].id >= 31 && !cvx_zombie(i); }

/* import k's folder name ("EZ00": .../<id>/<id>.cvxm) */
static const char* imp_id(int k, char* buf, int n)
{
    const char* f = actor_import_file[k];
    const char* s = f ? strrchr(f, '/') : NULL;
    int l = s ? (int)strlen(s + 1) : 0;
    if (l > 5 && !strcmp(s + 1 + l - 5, ".cvxm")) l -= 5;
    snprintf(buf, (size_t)n, "%.*s", l, s ? s + 1 : "");
    return buf;
}
static int imp_ok(int k) { return actor_imports[k].name && actor_import_shown(k); }
static int uc(int k) { return actor_import_src[k] == IMPORT_UMBRELLA; }
static int uc_zombie(int k) { char b[64]; return uc(k) && !strncmp(imp_id(k, b, sizeof(b)), "EZ", 2); }
static int uc_monster(int k) { char b[64]; return uc(k) && imp_id(k, b, sizeof(b))[0] == 'E' && !uc_zombie(k); }

static int member(int g, int idx)
{
    int k = idx - ACTOR_MODELS;
    if (idx < ACTOR_MODELS) {
        if (!cat_ok(idx)) return 0;
        switch (g) {
        case EM_RANDOM_CVX_ZOMBIE: case EM_RANDOM_ANY_ZOMBIE: return cvx_zombie(idx);
        case EM_RANDOM_CVX_MONSTER: return !cvx_zombie(idx) && !cvx_char(idx);
        case EM_RANDOM_CVX_CHAR: return cvx_char(idx);
        case EM_RANDOM_ANY: return 1;
        }
        return 0;
    }
    if (!imp_ok(k)) return 0;
    switch (g) {
    case EM_RANDOM_UC_ZOMBIE: case EM_RANDOM_ANY_ZOMBIE: return uc_zombie(k);
    case EM_RANDOM_UC_MONSTER: return uc_monster(k);
    case EM_RANDOM_UC_CHAR: return uc(k) && !uc_zombie(k) && !uc_monster(k);
    case EM_RANDOM_OUTBREAK: return actor_import_src[k] == IMPORT_OUTBREAK2;
    case EM_RANDOM_ANY: return 1;
    }
    return 0;
}

int emodel_members(int v, int* out, int max)
{
    int n = 0, i, ni = imports();
    if (v >= EM_IMPORT) { if (v - EM_IMPORT < ni && imp_ok(v - EM_IMPORT) && max > 0) out[n++] = ACTOR_MODELS + v - EM_IMPORT; return n; }
    if (v >= EM_MODEL) { if (v - EM_MODEL < ACTOR_MODELS && cat_ok(v - EM_MODEL) && max > 0) out[n++] = v - EM_MODEL; return n; }
    if (v <= 0 || v >= EM_RANDOM_END) return 0;
    for (i = 0; i < ACTOR_MODELS + ni && n < max; i++) if (member(v, i)) out[n++] = i;
    return n;
}

int emodel_choices(int* out, int max)
{
    int n = 0, g, i, ni = imports(), one;
    if (max > 0) out[n++] = EM_ORIGINAL;
    for (g = 1; g < EM_RANDOM_END && n < max; g++)
        if (emodel_members(g, &one, 1)) out[n++] = g;
    for (i = 0; i < ACTOR_MODELS && n < max; i++) if (cat_ok(i)) out[n++] = EM_MODEL + i;
    for (i = 0; i < ni && n < max; i++) if (imp_ok(i)) out[n++] = EM_IMPORT + i;
    return n;
}

const char* emodel_choice_name(int v, char* buf, int n)
{
    const ActorModel* am;
    if (v == EM_ORIGINAL) return "Original";
    if (v > 0 && v < EM_RANDOM_END) return rnames[v];
    am = v >= EM_IMPORT ? (v - EM_IMPORT < imports() ? &actor_imports[v - EM_IMPORT] : NULL) : v >= EM_MODEL && v - EM_MODEL < ACTOR_MODELS ? &actor_models[v - EM_MODEL] : NULL;
    if (!am || !am->name) return "Original";
    if (v >= EM_IMPORT) { snprintf(buf, (size_t)n, "%s", appear_name(COOP_CHR_IMPORT + v - EM_IMPORT)); return buf; }   /* (with its variant) */
    return am->name;
}

const char* emodel_choice_group(int v)
{
    if (v < EM_MODEL) return v ? "Random" : "";
    if (v >= EM_IMPORT) return v - EM_IMPORT < imports() ? appear_group(COOP_CHR_IMPORT + v - EM_IMPORT) : "";
    return cvx_zombie(v - EM_MODEL) ? "Zombies" : cvx_char(v - EM_MODEL) ? "Characters" : "Monsters";
}

static unsigned mix(unsigned h)
{
    h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
    return h;
}

int emodel_pick(int v, unsigned seed, int room, int c, int slot)
{
    static int m[ACTOR_MODELS + ACTOR_IMPORT_MAX];
    int n = emodel_members(v, m, ACTOR_MODELS + ACTOR_IMPORT_MAX), r, i;
    unsigned h;
    if (!n) return -1;
    if (n == 1) return m[0];
    r = n < EM_ROOM_PICKS ? n : EM_ROOM_PICKS;
    h = mix(seed ^ mix((unsigned)room * 0x9E3779B9u ^ (unsigned)(c + 1) * 0x85EBCA6Bu));
    for (i = 0; i < r; i++) {   /* the room's models: the first r of a shuffle */
        int j = i + (int)((h = mix(h + 0x6D2B79F5u)) % (unsigned)(n - i)), t = m[i];
        m[i] = m[j];
        m[j] = t;
    }
    return m[(h + (unsigned)slot) % (unsigned)r];   /* (neighbouring slots, a room's records: different ones) */
}

/* ---------------------------------------------------------------- recvx.ini */
static const char* key_of(int v, char* buf, int n)
{
    char id[64];
    if (v > 0 && v < EM_RANDOM_END) { snprintf(buf, (size_t)n, "random:%s", rkeys[v]); return buf; }
    if (v >= EM_IMPORT && v - EM_IMPORT < imports()) { snprintf(buf, (size_t)n, "import:%s", imp_id(v - EM_IMPORT, id, sizeof(id))); return buf; }
    if (v >= EM_MODEL && v - EM_MODEL < ACTOR_MODELS) return actor_models[v - EM_MODEL].name;
    return "original";
}

int emodel_ini_line(const char* key, const char* val)
{
    int c, v, ni = imports();
    char b[128];
    if (strncmp(key, "enemy_model_", 12)) return 0;
    for (c = 0; c < EM_CATS && strcmp(key + 12, cats[c].key); c++);
    if (c == EM_CATS) return 1;
    g_emodel[c] = EM_ORIGINAL;
    for (v = 1; v < EM_RANDOM_END; v++) if (!strcmp(val, key_of(v, b, sizeof(b)))) { g_emodel[c] = v; return 1; }
    for (v = 0; v < ACTOR_MODELS; v++) if (!strcmp(val, actor_models[v].name)) { g_emodel[c] = EM_MODEL + v; return 1; }
    for (v = 0; v < ni; v++) if (!strcmp(val, key_of(EM_IMPORT + v, b, sizeof(b)))) { g_emodel[c] = EM_IMPORT + v; return 1; }
    return 1;   /* (an import deleted since: Original) */
}

void emodel_ini_write(void (*line)(const char* key, const char* val, void* ud), void* ud)
{
    char k[64], b[128];
    for (int c = 0; c < EM_CATS; c++)
        if (g_emodel[c]) { snprintf(k, sizeof(k), "enemy_model_%s", cats[c].key); line(k, key_of(g_emodel[c], b, sizeof(b)), ud); }
}
