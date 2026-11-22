/* ctest "emodel": "Enemy Models" (src/host/enemymdl.c): categories, the choice list, the picks (deterministic, from the
   group, a few models per room, neighbours differ), a hidden import source, the recvx.ini keys. */
#include "../src/host/enemymdl.h"
#include "../src/import/import.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void add(const char* name, const char* file, int src)
{
    int k = actor_import_n;
    actor_imports[k].name = name;
    actor_import_file[k] = file;
    actor_import_src[k] = (unsigned char)src;
    actor_import_group[k] = IMPORT_GROUP_NPCS;
    actor_import_n = k + 1;
}

static int find(const char* name)
{
    for (int i = 0; i < ACTOR_MODELS; i++) if (!strcmp(actor_models[i].name, name)) return i;
    return -1;
}

static const char* got_key;
static char got_val[128];
static void line(const char* key, const char* val, void* ud) { (void)ud; got_key = key; snprintf(got_val, sizeof(got_val), "%s", val); }

int main(void)
{
    static int m[ACTOR_MODELS + ACTOR_IMPORT_MAX], ch[4096];
    int n, i, seen[8] = { 0 }, nemesis, steve = find("Steve (Cutscene Model)"), nb;
    char b[96];
    add("Zombie", "/m/umbrella/EZ00/EZ00.cvxm", IMPORT_UMBRELLA);
    add("Zombie", "/m/umbrella/EZ01/EZ01.cvxm", IMPORT_UMBRELLA);
    add("Zombie", "/m/umbrella/EZ02/EZ02.cvxm", IMPORT_UMBRELLA);
    add("Zombie", "/m/umbrella/EZ03/EZ03.cvxm", IMPORT_UMBRELLA);
    add("Nemesis", "/m/umbrella/ENS1/ENS1.cvxm", IMPORT_UMBRELLA);
    add("Chris Redfield", "/m/umbrella/PCR1/PCR1.cvxm", IMPORT_UMBRELLA);
    add("Kevin Ryman", "/m/outbreak2/HN00/HN00.cvxm", IMPORT_OUTBREAK2);
    nemesis = ACTOR_MODELS + 4;

    /* categories */
    CHECK(emodel_cat(1, 7) == EM_ZOMBIES && emodel_cat(1, 42) == EM_ZOMBIES);
    CHECK(emodel_cat(5, 0) == EM_HUNTER && emodel_cat(5, 1) == EM_SWEEPER && emodel_cat(19, 1) == EM_TYRANT);
    CHECK(emodel_cat(4, 0) < 0 && emodel_cat(91, 0) < 0 && emodel_cat(15, 0) < 0);   /* (Cerberus, a story character, Nosferatu) */

    /* groups */
    n = emodel_members(EM_RANDOM_UC_ZOMBIE, m, 4096);
    CHECK(n == 4);
    for (i = 0; i < n; i++) CHECK(m[i] >= ACTOR_MODELS && m[i] < ACTOR_MODELS + 4);
    CHECK(emodel_members(EM_RANDOM_UC_MONSTER, m, 4096) == 1 && m[0] == nemesis);
    CHECK(emodel_members(EM_RANDOM_UC_CHAR, m, 4096) == 1 && m[0] == ACTOR_MODELS + 5);
    CHECK(emodel_members(EM_RANDOM_OUTBREAK, m, 4096) == 1);
    n = emodel_members(EM_RANDOM_CVX_ZOMBIE, m, 4096);
    CHECK(n >= 24);
    for (i = 0; i < n; i++) CHECK(actor_models[m[i]].id == 1 || actor_models[m[i]].id >= 51);
    CHECK(emodel_members(EM_RANDOM_ANY_ZOMBIE, m, 4096) == n + 4);
    n = emodel_members(EM_RANDOM_CVX_MONSTER, m, 4096);
    for (i = 0; i < n; i++) CHECK(actor_models[m[i]].anim != ANIM_IDLE && actor_models[m[i]].id < 31 && actor_models[m[i]].id != 1);
    CHECK(n >= 7);   /* Alexia 1, Nosferatu, Tyrant, Monster Steve, Hunter, Sweeper, Bandersnatch, anatomist (not Tyrant 2's twin) */

    /* the list: Original, the random groups, the catalogue's posable models, the imports */
    n = emodel_choices(ch, 4096);
    CHECK(ch[0] == EM_ORIGINAL && ch[1] == EM_RANDOM_CVX_ZOMBIE && ch[n - 1] == EM_IMPORT + 6);
    for (i = 0; i < n; i++) CHECK(emodel_valid(ch[i]) && (ch[i] < EM_MODEL || ch[i] >= EM_IMPORT || actor_models[ch[i] - EM_MODEL].anim != ANIM_IDLE));
    CHECK(!strcmp(emodel_choice_name(EM_IMPORT + 4, b, sizeof(b)), "Nemesis"));
    CHECK(!emodel_valid(EM_RANDOM_END) && !emodel_valid(EM_MODEL + ACTOR_MODELS) && !emodel_valid(-1));

    /* picks: a single model always; a group deterministic, from it, at most EM_ROOM_PICKS per room, neighbours differ */
    CHECK(emodel_pick(EM_IMPORT + 4, 1, 3060, 0, 7) == nemesis);
    CHECK(emodel_pick(EM_MODEL + steve, 1, 3060, 0, 7) == steve);
    CHECK(emodel_pick(EM_ORIGINAL, 1, 3060, 0, 7) < 0);
    for (int room = 0; room < 400; room += 10) {
        int diff[EM_ROOM_PICKS + 2], nd = 0;
        for (int s = 0; s < 30; s++) {
            int p = emodel_pick(EM_RANDOM_UC_ZOMBIE, 77, room, EM_ZOMBIES, s), j;
            CHECK(p == emodel_pick(EM_RANDOM_UC_ZOMBIE, 77, room, EM_ZOMBIES, s));
            CHECK(p >= ACTOR_MODELS && p < ACTOR_MODELS + 4);
            if (s) CHECK(p != emodel_pick(EM_RANDOM_UC_ZOMBIE, 77, room, EM_ZOMBIES, s - 1));
            for (j = 0; j < nd && diff[j] != p; j++);
            if (j == nd && nd < EM_ROOM_PICKS + 2) diff[nd++] = p;
            seen[p - ACTOR_MODELS] = 1;
        }
        CHECK(nd == EM_ROOM_PICKS);
    }
    CHECK(seen[0] && seen[1] && seen[2] && seen[3]);   /* every member over the rooms */

    /* a hidden source: its models leave the groups and the list; a choice of one shows the original */
    g_import_hidden = 1u << IMPORT_UMBRELLA;
    CHECK(emodel_members(EM_RANDOM_UC_ZOMBIE, m, 4096) == 0);
    CHECK(emodel_pick(EM_IMPORT + 4, 1, 3060, 0, 7) < 0);
    nb = emodel_choices(ch, 4096);
    CHECK(nb == n - 6 - 3);   /* (6 imports, 3 groups with no member left: UC zombies, monsters, characters) */
    g_import_hidden = 0;

    /* recvx.ini */
    g_emodel[EM_ZOMBIES] = EM_RANDOM_UC_ZOMBIE;
    g_emodel[EM_HUNTER] = EM_IMPORT + 4;
    g_emodel[EM_TYRANT] = EM_MODEL + steve;
    emodel_ini_write(line, NULL);
    CHECK(!strcmp(got_key, "enemy_model_tyrant") && !strcmp(got_val, "Steve (Cutscene Model)"));
    memset(g_emodel, 0, sizeof(g_emodel));
    CHECK(emodel_ini_line("enemy_model_zombies", "random:uc_zombies") && g_emodel[EM_ZOMBIES] == EM_RANDOM_UC_ZOMBIE);
    CHECK(emodel_ini_line("enemy_model_hunter", "import:ENS1") && g_emodel[EM_HUNTER] == EM_IMPORT + 4);
    CHECK(emodel_ini_line("enemy_model_tyrant", "Steve (Cutscene Model)") && g_emodel[EM_TYRANT] == EM_MODEL + steve);
    CHECK(emodel_ini_line("enemy_model_sweeper", "import:GONE") && g_emodel[EM_SWEEPER] == EM_ORIGINAL);
    CHECK(!emodel_ini_line("enemy_random", "1"));
    printf(fails ? "emodel: %d failures\n" : "emodel: ok\n", fails);
    return fails != 0;
}
