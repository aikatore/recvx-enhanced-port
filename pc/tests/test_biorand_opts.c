/* BioRand's CVX options and seed string (pc/src/host/biorand_opts.h).
     test_biorand_opts                 the golden strings, a sweep of every row, malformed strings,
                                       the distributions, the estimate, the ini load
     test_biorand_opts --random N S    N seed strings of random option sets (xorshift seed S), one per
                                       line, for the manual check against biorand-cli (not a ctest) */
#include "../src/host/biorand_opts.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, saves, logs;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

void host_log(const char* fmt, ...) { va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); logs++; }
void settings_save_later(void) { saves++; }

/* S1: biorand-cli -s 12345 with no saved seed (new RandoConfig, Game 4) */
#define GOLD1 "R840-T1C0-XXHH4HZZZMEZ9H0007JZZZZZZZ5ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ"
/* S1: defaults + the CVX rules + these, echoed by the CLI and equal to its log seed */
#define GOLD2 "R840-K9YC-V4ZH0HNAXQE9CH0007HRQ0UZZZLZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ"

static void gold2(BioOpts* o)
{
    static const int ratios[6] = { 0, 7, 3, 1, 0, 2 };
    biorand_opts_default(o);
    biorand_opts_rules(o);
    o->Seed = 424242; o->RandomDoors = 1; o->AreaCount = 1; o->AreaSize = 2; o->RatioAmmo = 31; o->RatioInkRibbons = 0;
    for (int i = 0; i < 6; i++) o->EnemyRatios[i] = ratios[i];
    for (int i = 0; i < 12; i++) o->EnabledWeapons[i] = !(i & 1);
    o->Weapon0 = 4; o->EnemyQuantity = 9; o->AmmoQuantity = 7;
}

/* encode, decode, compare, encode again */
static int roundtrip(const BioOpts* o)
{
    char a[BIORAND_SEED_CAP], b[BIORAND_SEED_CAP];
    BioOpts d;
    if (biorand_seed_encode(o, a, sizeof(a)) || strlen(a) != BIORAND_SEED_LEN) return 0;
    if (biorand_seed_decode(a, &d)) return 0;
    if (memcmp(&d, o, sizeof(d))) return 0;
    return !biorand_seed_encode(&d, b, sizeof(b)) && !strcmp(a, b);
}

static int rejected(const char* s)
{
    BioOpts o;
    memset(&o, 0x5a, sizeof(o));
    const char* why = biorand_seed_decode(s, &o);
    for (size_t i = 0; i < sizeof(o); i++) if (((unsigned char*)&o)[i] != 0x5a) return 0;   /* untouched */
    return why && *why;
}

static unsigned rng = 1;
static unsigned xs(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static double dsum(const BioSlice* s, int n) { double t = 0; for (int i = 0; i < n; i++) t += s[i].pct; return t; }

int main(int argc, char** argv)
{
    BioOpts o, d;
    char s[128], t[BIORAND_SEED_CAP];
    BioSlice sl[6];

    if (argc == 4 && !strcmp(argv[1], "--random")) {
        rng = (unsigned)strtoul(argv[3], NULL, 0) | 1;
        for (int k = atoi(argv[2]); k > 0; k--) {
            biorand_opts_default(&o);
            biorand_opts_rules(&o);
            for (int i = 0; i < biorand_opt_n; i++)
                biorand_opt_set(&o, i, biorand_opt[i].lo + (int)(xs() % (unsigned)(biorand_opt[i].hi - biorand_opt[i].lo + 1)));
            biorand_seed_encode(&o, s, sizeof(s));
            printf("%s\n", s);
        }
        return 0;
    }

    /* the golden strings */
    biorand_opts_default(&o);
    o.Seed = 12345;
    CHECK(!biorand_seed_encode(&o, s, sizeof(s)) && !strcmp(s, GOLD1));
    CHECK(!biorand_seed_decode(GOLD1, &d) && !memcmp(&d, &o, sizeof(d)));
    CHECK(roundtrip(&o));
    gold2(&o);
    CHECK(!biorand_seed_encode(&o, s, sizeof(s)) && !strcmp(s, GOLD2));
    CHECK(!biorand_seed_decode(GOLD2, &d) && !memcmp(&d, &o, sizeof(d)));
    CHECK(roundtrip(&o));
    CHECK(biorand_seed_encode(&o, s, BIORAND_SEED_LEN) == -1);   /* no room for the NUL */
    /* GOLD1 with the rules: only the forced bits change (the CLI's log seed of GOLD1) */
    biorand_seed_decode(GOLD1, &o);
    biorand_opts_rules(&o);
    biorand_seed_encode(&o, s, sizeof(s));
    CHECK(!strcmp(s, "R840-T1C0-T4HH4HZZZMEZ9H0007HZZZZZZZ5ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ"));
    /* lower case and surrounding spaces are read as BioRand reads them */
    strcpy(s, "  " GOLD2 " \r\n");
    for (char* c = s; *c; c++) if (*c >= 'A' && *c <= 'Z') *c += 32;
    CHECK(!biorand_seed_decode(s, &d) && !biorand_seed_encode(&d, t, sizeof(t)) && !strcmp(t, GOLD2));
    /* FromString caps GameVariant at 1 (variant 2 is 'Player' in the GUI) */
    strcpy(s, GOLD1);
    s[3] = '2';
    CHECK(!biorand_seed_decode(s, &d) && d.GameVariant == 1);

    /* every row at lo, hi and its default, from the menu's starting point and with doors on */
    for (int doors = 0; doors < 2; doors++)
        for (int i = 0; i < biorand_opt_n; i++) {
            const BioOpt* r = &biorand_opt[i];
            int vals[3] = { r->lo, r->hi, r->def };
            CHECK(r->lo < r->hi && r->def >= r->lo && r->def <= r->hi && r->label && biorand_opt_find(r->key) == i);
            CHECK(!r->parent || biorand_opt[biorand_opt_find(r->parent)].flags & BOF_TOGGLE);
            CHECK(r->type != BO_ENUM || r->names);
            for (int v = 0; v < 3; v++) {
                biorand_opts_default(&o);
                o.RandomDoors = doors;
                biorand_opts_rules(&o);
                if (!doors) CHECK(biorand_opt_get(&o, i) == r->def);   /* the table's defaults are BioRand's */
                biorand_opt_set(&o, i, vals[v]);
                if (!biorand_opt_locked(&o, i)) CHECK(biorand_opt_get(&o, i) == vals[v]);
                CHECK(roundtrip(&o));
            }
            biorand_opt_set(&o, i, r->hi + 5);
            CHECK(biorand_opt_get(&o, i) <= r->hi);
            biorand_opt_set(&o, i, r->lo - 5);
            CHECK(biorand_opt_get(&o, i) >= r->lo);
        }
    /* random option sets */
    for (int k = 0; k < 2000; k++) {
        biorand_opts_default(&o);
        for (int i = 0; i < biorand_opt_n; i++) biorand_opt_set(&o, i, (int)(xs() % 40) - 4);
        CHECK(roundtrip(&o));
    }
    /* every field at all ones decodes and re-encodes (hidden fields too) */
    {
        char all[BIORAND_SEED_CAP] = "R840-ZZZZ-";
        memset(all + 10, 'Z', BIORAND_SEED_LEN - 10);
        all[BIORAND_SEED_LEN] = 0;
        CHECK(!biorand_seed_decode(all, &d) && d.Seed == BIORAND_SEED_MAX && d.EnemyQuantity == 15 && d.Player1 == 127);
        biorand_seed_encode(&d, s, sizeof(s));
        CHECK(!biorand_seed_decode(s, &o) && !memcmp(&o, &d, sizeof(o)));
    }

    /* the visibility rules */
    biorand_opts_default(&o);
    biorand_opts_rules(&o);
    {
        int doors = biorand_opt_find("RandomDoors"), size = biorand_opt_find("AreaSize"), alt = biorand_opt_find("AlternativeRoutes");
        int items = biorand_opt_find("RandomItems"), pool = biorand_opt_find("ShuffleItems"), w0 = biorand_opt_find("Weapon0");
        int gun = biorand_opt_find("RatioGunpowder"), rooms = biorand_opt_find("EnemyRooms"), bat = biorand_opt_find("EnemyRatios[0]");
        CHECK(doors >= 0 && size >= 0 && alt >= 0 && items >= 0 && pool >= 0 && w0 >= 0 && gun >= 0 && rooms >= 0 && bat >= 0);
        CHECK(!biorand_opt_active(&o, size) && biorand_opt_active(&o, alt) && biorand_opt_active(&o, w0) && biorand_opt_active(&o, gun));
        biorand_opt_set(&o, items, 0);
        CHECK(!biorand_opt_active(&o, alt) && !biorand_opt_active(&o, w0) && biorand_opt_active(&o, items));
        biorand_opt_set(&o, items, 1);
        biorand_opt_set(&o, pool, 0);
        CHECK(o.ShuffleItems == 1 && !biorand_opt_active(&o, gun) && !biorand_opt_active(&o, w0));
        biorand_opt_set(&o, items, 0);
        biorand_opt_set(&o, alt, 0);
        biorand_opt_set(&o, doors, 1);   /* doors on: items, segmented and the pool on, those rows locked */
        CHECK(o.RandomItems && o.Segmented && !o.ShuffleItems && biorand_opt_active(&o, size) && biorand_opt_active(&o, gun));
        CHECK(biorand_opt_locked(&o, items) && biorand_opt_locked(&o, alt) && biorand_opt_locked(&o, pool) && !biorand_opt_locked(&o, size));
        biorand_opt_set(&o, alt, 1);
        CHECK(!o.AlternativeRoutes);   /* locked rows keep their value */
        biorand_opt_set(&o, doors, 0);
        CHECK(o.RandomItems && !biorand_opt_locked(&o, alt));   /* forced on stays on, as in the GUI */
        biorand_opt_set(&o, biorand_opt_find("RandomEnemyPlacement"), 0);
        CHECK(!biorand_opt_active(&o, rooms) && biorand_opt_active(&o, bat));
        biorand_opt_set(&o, biorand_opt_find("RandomEnemies"), 0);
        CHECK(!biorand_opt_active(&o, bat));
        CHECK(biorand_opt_find("Nope") == -1 && biorand_opt_find(NULL) == -1);
    }

    /* malformed strings: rejected with a reason, o untouched */
    for (size_t n = 0; n < BIORAND_SEED_LEN; n++) {
        memcpy(s, GOLD2, n);
        s[n] = 0;
        CHECK(rejected(s));
    }
    CHECK(rejected(NULL) && rejected("   ") && rejected(GOLD2 "0") && rejected(GOLD2 "-") && rejected("12345"));
    for (int i = 0; i < BIORAND_SEED_LEN; i++) {
        static const char bad[] = { 'I', 'O', 'S', 'D', '!', ' ', '-', '_', (char)0xC3, 1 };
        for (size_t b = 0; b < sizeof(bad); b++) {
            strcpy(s, GOLD2);
            if (s[i] == bad[b]) continue;
            s[i] = bad[b];
            CHECK(rejected(s));
        }
    }
    {
        static const char games[] = "0123ABZ", vers[] = "0179Z";
        for (const char* g = games; *g; g++) { strcpy(s, GOLD2); s[2] = *g; CHECK(rejected(s)); }
        for (const char* v = vers; *v; v++) { strcpy(s, GOLD2); s[1] = *v; CHECK(rejected(s)); }
        strcpy(s, GOLD2); s[2] = '2';
        CHECK(strstr(biorand_seed_decode(s, &d), "RE2") != NULL);
        CHECK(strstr(biorand_seed_decode("R740-T1C0", &d), "version 7") != NULL);   /* S1's err.sh old version */
        CHECK(strstr(biorand_seed_decode("R840-T1C0", &d), "characters") != NULL);
    }

    /* the distributions */
    biorand_opts_default(&o);
    CHECK(biorand_item_dist(&o, sl) == 5 && fabs(dsum(sl, 5) - 100) < 1e-9);
    CHECK(fabs(sl[0].pct - 12.5) < 1e-9 && fabs(sl[1].pct - 16 * 87.5 / 52) < 1e-9 && fabs(sl[4].pct - 4 * 87.5 / 52) < 1e-9);
    CHECK(!strcmp(sl[0].name, "Keys") && sl[0].rgb == 0xADD8E6 && !strcmp(sl[4].name, "Ink"));
    o.RatioAmmo = o.RatioGunpowder = o.RatioHealth = o.RatioInkRibbons = 0;
    CHECK(biorand_item_dist(&o, sl) == 5 && fabs(sl[0].pct - 100) < 1e-9 && sl[2].pct == 0);
    o.RatioHealth = 31;
    CHECK(biorand_item_dist(&o, sl) == 5 && fabs(sl[0].pct - 12.5) < 1e-9 && fabs(sl[3].pct - 87.5) < 1e-9);
    CHECK(biorand_enemy_dist(&o, sl) == 6 && fabs(sl[3].pct - 100 / 6.0) < 1e-9 && !strcmp(sl[3].name, "Bandersnatch"));
    for (int i = 0; i < 6; i++) o.EnemyRatios[i] = 0;
    CHECK(biorand_enemy_dist(&o, sl) == 0);
    o.EnemyRatios[0] = 7; o.EnemyRatios[11] = 7;   /* slots past CVX's six do not count */
    CHECK(biorand_enemy_dist(&o, sl) == 1 && fabs(sl[0].pct - 100) < 1e-9 && sl[0].rgb == 0);
    for (int k = 0; k < 5000; k++) {
        o.RatioAmmo = xs() % 32; o.RatioGunpowder = xs() % 32; o.RatioHealth = xs() % 32; o.RatioInkRibbons = xs() % 32;
        int n = 0;
        for (int i = 0; i < 6; i++) n += !!(o.EnemyRatios[i] = xs() % 8);
        biorand_item_dist(&o, sl);
        CHECK(fabs(dsum(sl, 5) - 100) < 1e-9);
        CHECK(biorand_enemy_dist(&o, sl) == n && (!n || fabs(dsum(sl, n) - 100) < 1e-9));
    }

    /* the doors estimate (UpdateEstimateCompletion) */
    biorand_opts_default(&o);
    biorand_doors_estimate(&o, s, sizeof(s));
    CHECK(!strcmp(s, ""));
    o.RandomDoors = 1;
    biorand_doors_estimate(&o, s, sizeof(s));
    CHECK(!strcmp(s, "4 to 6 hours per scenario"));
    o.AreaSize = 0; biorand_doors_estimate(&o, s, sizeof(s)); CHECK(!strcmp(s, "15 to 30 minutes per scenario"));
    o.AreaSize = 1; biorand_doors_estimate(&o, s, sizeof(s)); CHECK(!strcmp(s, "30 to 60 minutes per scenario"));
    o.AreaSize = 2; o.AreaCount = 3; biorand_doors_estimate(&o, s, sizeof(s)); CHECK(!strcmp(s, "2 to 3 hours per scenario"));

    /* recvx.ini: biorand_seed */
    g_biorand_seed[0] = 0;
    logs = 0;
    biorand_opts_load(&o);
    biorand_opts_default(&d);
    biorand_opts_rules(&d);
    CHECK(!memcmp(&o, &d, sizeof(o)) && logs == 0);   /* no key: the defaults, quietly */
    strcpy(g_biorand_seed, "R840-T1C0-garbage");
    biorand_opts_load(&o);
    CHECK(!memcmp(&o, &d, sizeof(o)) && logs == 1);   /* invalid: the defaults and a log line */
    strcpy(g_biorand_seed, GOLD2);
    biorand_opts_load(&o);
    gold2(&d);
    CHECK(!memcmp(&o, &d, sizeof(o)));
    o.Seed = 7;
    saves = 0;
    biorand_opts_save(&o);
    CHECK(saves == 1 && !biorand_seed_decode(g_biorand_seed, &d) && d.Seed == 7);

    printf("%s: %d failures (%d rows)\n", fails ? "FAIL" : "ok", fails, biorand_opt_n);
    return fails != 0;
}
