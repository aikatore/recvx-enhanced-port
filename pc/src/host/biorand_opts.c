/* BioRand's Code: Veronica options and seed string (biorand_opts.h, docs/BIORAND_INGAME.md).
   From BioRand 3.3.2 (github.com/biorand/classic, commit c8df994): RandoConfig.cs (the string,
   the defaults), biorand/MainWindow.xaml(.cs) (labels, help texts, what greys what, the pies and the
   estimate), RECV/ReCvItemHelper.cs and ReCvEnemyHelper.cs (the weapon and enemy lists),
   ReCvRandomiser.Generate (the fields CVX forces off). */
#include "biorand_opts.h"
#include <stdio.h>
#include <string.h>

void host_log(const char* fmt, ...);
void settings_save_later(void);

char g_biorand_seed[BIORAND_SEED_CAP];

/* ---------------------------------------------------------------- the seed string */
/* RandoConfig.ToString: fields packed low bit first into 5-bit characters of this alphabet; the
   literals 'R' and '-' sit between them. Arrays are one field per entry, entry 0 first. */
static const char ABC[] = "0123456789ABCEFGHJKLMNPQRTUVWXYZ";

typedef struct { char lit; unsigned char bits, n; short off; } Field;
#define F(f, b) { 0, b, 1, offsetof(BioOpts, f) }
#define A(f, b) { 0, b, sizeof(((BioOpts*)0)->f) / sizeof(int), offsetof(BioOpts, f) }
#define Z(b) { 0, b, 1, -1 }   /* unused bits: written 0, ignored on reading */
#define L(c) { c, 0, 0, -1 }
static const Field layout[] = {
    L('R'), F(Version, 5), F(Game, 5), F(GameVariant, 5), L('-'), F(Seed, 20), L('-'),
    F(Segmented, 1), F(RandomDoors, 1), F(RandomNPCs, 1), F(RandomEnemies, 1), F(RandomItems, 1),
    F(RandomBgm, 1), F(ShuffleItems, 1), F(AlternativeRoutes, 1), F(IncludeDocuments, 1), F(ChangePlayer, 1),
    F(RatioAmmo, 5), F(RatioHealth, 5), F(RatioInkRibbons, 5), F(RatioGunpowder, 5),
    A(EnabledWeapons, 1),
    F(AmmoQuantity, 3), F(EnemyDifficulty, 2), F(EnemyRooms, 3), F(HiddenKeyItems, 1), Z(1),
    F(AreaCount, 2), F(AreaSize, 3),
    F(Weapon0, 3), F(Weapon1, 3), Z(3), F(PrioritiseCutscenes, 1),
    F(Player0, 7), F(Player1, 7), F(SwapCharacters, 1),
    F(RandomInventory, 1), F(RandomEnemyPlacement, 1), F(AllowEnemiesAnyRoom, 1), Z(2),
    F(RandomCutscenes, 1), F(AllowAnyVoice, 1), F(ReduceSilences, 1), F(RandomEvents, 1), F(RandomEnemySkins, 1),
    A(EnemyRatios, 3), F(EnemyQuantity, 4),
    A(EnabledNPCs, 1), A(EnabledBGMs, 1), A(EnabledEnemySkins, 1),
};

static int* at(BioOpts* o, int off) { return (int*)((char*)o + off); }

static int digit(char c)
{
    const char* p;
    if (c >= 'a' && c <= 'z') c -= 32;
    return c && (p = strchr(ABC, c)) ? (int)(p - ABC) : -1;
}

int biorand_seed_encode(const BioOpts* o, char* out, size_t n)
{
    unsigned cur = 0;
    int nb = 0;
    size_t k = 0;
    if (n <= BIORAND_SEED_LEN) return -1;
    for (size_t f = 0; f < sizeof(layout) / sizeof(layout[0]); f++) {
        const Field* d = &layout[f];
        if (d->lit) { out[k++] = d->lit; continue; }
        for (int i = 0; i < d->n; i++) {
            unsigned v = d->off < 0 ? 0 : (unsigned)at((BioOpts*)o, d->off)[i];
            cur |= (v & ((1u << d->bits) - 1)) << nb;
            nb += d->bits;
            for (; nb >= 5; nb -= 5, cur >>= 5) out[k++] = ABC[cur & 31];
        }
    }
    out[k] = 0;   /* 290 bits: nothing left over */
    return 0;
}

const char* biorand_seed_decode(const char* in, BioOpts* o)
{
    static char why[96];
    static const char* const games[] = { "no game", "RE1", "RE2", "RE3" };
    BioOpts t;
    const char* s = in ? in : "";
    size_t len, k = 0;
    unsigned cur = 0;
    int nb = 0, v;
    while (*s == ' ' || *s == '\t') s++;
    for (len = strlen(s); len && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n'); len--) {}
    if (!len) return "The seed string is empty.";
    if (len < 4 || (s[0] != 'R' && s[0] != 'r') || digit(s[1]) < 0 || digit(s[2]) < 0 || digit(s[3]) < 0)
        return "This is not a BioRand seed string (they look like R840-XXXX-...).";
    if ((v = digit(s[1])) != BIORAND_VERSION) {
        snprintf(why, sizeof(why), "Seed version %d is not supported: this game reads version %d.", v, BIORAND_VERSION);
        return why;
    }
    if ((v = digit(s[2])) != BIORAND_GAME_CVX) {
        snprintf(why, sizeof(why), "This seed string is for %s, not Code: Veronica.", v < 4 ? games[v] : "an unknown game");
        return why;
    }
    if (len != BIORAND_SEED_LEN) {
        snprintf(why, sizeof(why), "The seed string has %d characters; it needs %d.", (int)len, BIORAND_SEED_LEN);
        return why;
    }
    for (size_t i = 1; i < len; i++)
        if (i == 4 || i == 9 ? s[i] != '-' : digit(s[i]) < 0) {
            snprintf(why, sizeof(why), "The seed string has a wrong character at position %d.", (int)i + 1);
            return why;
        }
    memset(&t, 0, sizeof(t));
    for (size_t f = 0; f < sizeof(layout) / sizeof(layout[0]); f++) {
        const Field* d = &layout[f];
        if (d->lit) { k++; continue; }
        for (int i = 0; i < d->n; i++) {
            for (; nb < d->bits; nb += 5) cur |= (unsigned)digit(s[k++]) << nb;
            if (d->off >= 0) at(&t, d->off)[i] = (int)(cur & ((1u << d->bits) - 1));
            cur >>= d->bits;
            nb -= d->bits;
        }
    }
    if (t.GameVariant > 1) t.GameVariant = 1;   /* FromString: Math.Min(variant, 1) */
    *o = t;
    return NULL;
}

void biorand_opts_default(BioOpts* o)
{
    memset(o, 0, sizeof(*o));
    o->Version = BIORAND_VERSION;
    o->Game = BIORAND_GAME_CVX;
    o->Segmented = o->RandomNPCs = o->RandomEnemies = o->RandomItems = o->RandomBgm = 1;
    o->AlternativeRoutes = o->IncludeDocuments = o->ChangePlayer = o->HiddenKeyItems = 1;
    o->RatioAmmo = o->RatioHealth = o->RatioGunpowder = 16;
    o->RatioInkRibbons = 4;
    o->AmmoQuantity = 4;
    o->EnemyDifficulty = 2;
    o->EnemyRooms = 5;
    o->AreaCount = 3;
    o->AreaSize = 7;
    o->Weapon0 = o->Weapon1 = 1;
    o->PrioritiseCutscenes = o->RandomInventory = o->RandomEnemyPlacement = o->AllowEnemiesAnyRoom = 1;
    o->RandomCutscenes = o->RandomEnemySkins = 1;
    o->EnemyQuantity = 2;
    /* RandoConfig's arrays start empty; the writer fills missing entries with 1 (ratios 7) */
    for (int i = 0; i < 15; i++) o->EnabledWeapons[i] = 1;
    for (int i = 0; i < 12; i++) o->EnemyRatios[i] = 7;
    for (int i = 0; i < 100; i++) o->EnabledNPCs[i] = 1;
    for (int i = 0; i < 20; i++) o->EnabledBGMs[i] = 1;
    for (int i = 0; i < 35; i++) o->EnabledEnemySkins[i] = 1;
}

void biorand_opts_rules(BioOpts* o)
{
    o->Version = BIORAND_VERSION;
    o->Game = BIORAND_GAME_CVX;
    o->GameVariant = 0;
    /* ReCvRandomiser.Generate / DisableUnavailableCosmeticFeatures turn these off for CVX */
    o->ChangePlayer = o->Player0 = o->Player1 = o->SwapCharacters = 0;
    o->RandomNPCs = o->RandomCutscenes = o->RandomEvents = o->RandomBgm = o->IncludeDocuments = 0;
    /* the GUI's lists are CVX's 12 weapons and 6 enemies; the writer fills the rest */
    for (int i = 12; i < 15; i++) o->EnabledWeapons[i] = 1;
    for (int i = 6; i < 12; i++) o->EnemyRatios[i] = 7;
    if (o->RandomDoors) o->RandomItems = o->Segmented = 1, o->ShuffleItems = 0;   /* MainWindow.UpdateConfig */
}

/* ---------------------------------------------------------------- the rows */
const char* const biorand_groups[BOG_COUNT] = { "Seed", "Doors", "Items", "Enemies" };

static const char* const weapon_kinds[] = { "None", "Random", "Sidearm", "Primary", "Powerful" };
static const char* const enemy_names[6] = { "Bat", "Zombie", "Hunter", "Bandersnatch", "Zombie Dog", "Tyrant" };
static const unsigned enemy_rgb[6] = { 0x000000, 0xD3D3D3, 0xCD5C5C, 0x00FFFF, 0x000000, 0x808080 };   /* Black LightGray IndianRed Cyan Black Gray */

#define ROW(key, g, t, field, lo, hi, def, fl, par, label, help, low, high, names) \
    { key, g, t, (int)offsetof(BioOpts, field), lo, hi, def, fl, par, label, help, low, high, names }
#define FLAG(g, field, def, fl, par, label, help) ROW(#field, g, BO_BOOL, field, 0, 1, def, fl, par, label, help, NULL, NULL, NULL)
#define RANGE(g, t, field, hi, def, par, label, help, low, high) ROW(#field, g, t, field, 0, hi, def, 0, par, label, help, low, high, NULL)
#define WEAPON(i, name) ROW("EnabledWeapons[" #i "]", BOG_ITEMS, BO_BOOL, EnabledWeapons[i], 0, 1, 1, 0, "ShuffleItems", name, \
    "When off, this weapon is left out of the item pool.", NULL, NULL, NULL)
#define ENEMY(i, name) ROW("EnemyRatios[" #i "]", BOG_ENEMIES, BO_RATIO, EnemyRatios[i], 0, 7, 7, 0, "RandomEnemies", name, \
    "How often this enemy is chosen, against the others. Note: Some rooms can only contain certain enemies.", "None", "High", NULL)

const BioOpt biorand_opt[] = {
    RANGE(BOG_SEED, BO_RANGE, Seed, BIORAND_SEED_MAX, 0, NULL, "Seed",
          "The number the randomizer starts from. The same seed and options give the same game.", NULL, NULL),

    FLAG(BOG_DOORS, RandomDoors, 0, BOF_TOGGLE, NULL, "Randomize Doors",
         "Connects the rooms in a new order. Turns on random items, segmented placement and the custom item pool."),
    RANGE(BOG_DOORS, BO_RANGE, AreaCount, 3, 3, "RandomDoors", "Number of Segments",
          "Adjusts how many distinct groups of rooms appear before reaching the end.", "1", "4"),
    RANGE(BOG_DOORS, BO_RANGE, AreaSize, 7, 7, "RandomDoors", "Segment Size",
          "Adjusts how many rooms are in a segment.", "Low", "High"),
    FLAG(BOG_DOORS, PrioritiseCutscenes, 1, 0, "RandomDoors", "Prioritise rooms with cutscenes",
         "If checked, the seed will try to include as many cutscenes as possible."),

    FLAG(BOG_ITEMS, RandomItems, 1, BOF_TOGGLE | BOF_DOORS, NULL, "Randomize Items",
         "Places the key items and the other pickups at random."),
    FLAG(BOG_ITEMS, AlternativeRoutes, 1, BOF_DOORS, "RandomItems", "Allow alternative routes",
         "When checked, key items will be placed in any order. For example, in RE 2, the spade key may be placed in the basement and the heart key in the main hall."),
    FLAG(BOG_ITEMS, Segmented, 1, BOF_DOORS, "RandomItems", "Segmented placement",
         "When checked, restricts keys to the area they are required. For example, in RE 2, lab keys will not appear in the police station."),
    FLAG(BOG_ITEMS, HiddenKeyItems, 1, 0, "RandomItems", "Hidden Key Items",
         "When checked, hidden items, including arrange mode items (USA/EU/PSX) can be key items."),
    FLAG(BOG_ITEMS, ShuffleItems, 1, BOF_TOGGLE | BOF_INVERT | BOF_DOORS, "RandomItems", "Custom item pool",
         "When not checked, shuffles the vanilla pool of items. If checked, all items are replaced with random pickups."),
    RANGE(BOG_ITEMS, BO_RATIO, RatioGunpowder, 31, 16, "ShuffleItems", "Gunpowder probability",
          "Probability of non-key items being gunpowder.", "None", "High"),
    RANGE(BOG_ITEMS, BO_RATIO, RatioAmmo, 31, 16, "ShuffleItems", "Ammo probability",
          "Probability of non-key items being ammunition.", "None", "High"),
    RANGE(BOG_ITEMS, BO_RATIO, RatioHealth, 31, 16, "ShuffleItems", "Health probability",
          "Probability of non-key items being health.", "None", "High"),
    RANGE(BOG_ITEMS, BO_RATIO, RatioInkRibbons, 31, 4, "ShuffleItems", "Ink Ribbon probability",
          "Probability of non-key items being ink ribbons.", "None", "High"),
    RANGE(BOG_ITEMS, BO_RANGE, AmmoQuantity, 7, 4, "ShuffleItems", "Ammo average quantity",
          "Average quantity of ammunition per ammunition pick up.\nHighest setting will allow a maximum of 60 handgun bullets in a pickup, or 30 shotgun shells.",
          "None", "High"),
    FLAG(BOG_ITEMS, RandomInventory, 1, BOF_TOGGLE, "ShuffleItems", "Random inventory",
         "When checked, your initial inventory will be random."),
    ROW("Weapon0", BOG_ITEMS, BO_ENUM, Weapon0, 0, 4, 1, 0, "RandomInventory", "Weapon 1:", NULL, NULL, NULL, weapon_kinds),
    ROW("Weapon1", BOG_ITEMS, BO_ENUM, Weapon1, 0, 4, 1, 0, "RandomInventory", "Weapon 2:", NULL, NULL, NULL, weapon_kinds),
    /* ReCvItemHelper.GetWeaponNames */
    WEAPON(0, "Handgun"), WEAPON(1, "Rocket Launcher"), WEAPON(2, "Assault Rifle"), WEAPON(3, "Sniper Rifle"),
    WEAPON(4, "Shotgun"), WEAPON(5, "Handgun (Glock 17)"), WEAPON(6, "Grenade Launcher"), WEAPON(7, "Bowgun"),
    WEAPON(8, "Magnum"), WEAPON(9, "SMG"), WEAPON(10, "MP100"), WEAPON(11, "Linear Launcher"),

    FLAG(BOG_ENEMIES, RandomEnemies, 1, BOF_TOGGLE, NULL, "Randomize Enemies",
         "Replaces the enemies of each room at random."),
    FLAG(BOG_ENEMIES, RandomEnemyPlacement, 1, BOF_TOGGLE, "RandomEnemies", "Random enemy placements",
         "When checked, enemy quantity and positions will be random."),
    FLAG(BOG_ENEMIES, AllowEnemiesAnyRoom, 1, 0, "RandomEnemyPlacement", "Allow enemies in save rooms",
         "When checked, enemies can be in save rooms."),
    RANGE(BOG_ENEMIES, BO_RANGE, EnemyQuantity, 15, 2, "RandomEnemyPlacement", "Average number of enemies per room",
          "Adjusts the average number of enemies that can occur in a room.", "1", "16"),
    RANGE(BOG_ENEMIES, BO_RANGE, EnemyRooms, 7, 5, "RandomEnemyPlacement", "Rooms with enemies",
          "Adjusts the percentage of all rooms in the game that contain enemies.", "0%", "100%"),
    RANGE(BOG_ENEMIES, BO_RANGE, EnemyDifficulty, 3, 2, "RandomEnemies", "Difficulty",
          "Adjusts the limit of how many difficult enemies can appear in a single room, and the likelyhood of them being aggravated.", "Ez", "Hard"),
    /* ReCvEnemyHelper.GetSelectableEnemies */
    ENEMY(0, "Bat"), ENEMY(1, "Zombie"), ENEMY(2, "Hunter"), ENEMY(3, "Bandersnatch"), ENEMY(4, "Zombie Dog"), ENEMY(5, "Tyrant"),
};
const int biorand_opt_n = sizeof(biorand_opt) / sizeof(biorand_opt[0]);

int biorand_opt_find(const char* key)
{
    for (int i = 0; key && i < biorand_opt_n; i++)
        if (!strcmp(biorand_opt[i].key, key)) return i;
    return -1;
}

int biorand_opt_get(const BioOpts* o, int i)
{
    int v = *at((BioOpts*)o, biorand_opt[i].off);
    return biorand_opt[i].flags & BOF_INVERT ? !v : v;
}

void biorand_opt_set(BioOpts* o, int i, int v)
{
    const BioOpt* r = &biorand_opt[i];
    if (biorand_opt_locked(o, i)) return;
    v = v < r->lo ? r->lo : v > r->hi ? r->hi : v;
    *at(o, r->off) = r->flags & BOF_INVERT ? !v : v;
    biorand_opts_rules(o);
}

int biorand_opt_active(const BioOpts* o, int i)
{
    for (int p = biorand_opt_find(biorand_opt[i].parent); p >= 0; p = biorand_opt_find(biorand_opt[p].parent))
        if (!biorand_opt_get(o, p)) return 0;
    return 1;
}

const char* biorand_opt_locked(const BioOpts* o, int i)
{
    return biorand_opt[i].flags & BOF_DOORS && o->RandomDoors ? "Randomize Doors sets this." : NULL;
}

/* ---------------------------------------------------------------- distributions */
int biorand_item_dist(const BioOpts* o, BioSlice out[5])
{
    static const char* const names[5] = { "Keys", "Gunpowder", "Ammo", "Health", "Ink" };
    static const unsigned rgb[5] = { 0xADD8E6, 0x808080, 0xCD5C5C, 0x00FF7F, 0x000000 };   /* LightBlue Gray IndianRed SpringGreen Black */
    int r[5] = { 0, o->RatioGunpowder, o->RatioAmmo, o->RatioHealth, o->RatioInkRibbons };
    int total = r[1] + r[2] + r[3] + r[4];
    double sum = total ? 1.0 : 1 / 8.0;   /* all 0: the GUI's pie is Keys alone */
    for (int i = 0; i < 5; i++) {
        out[i].name = names[i];
        out[i].rgb = rgb[i];
        out[i].pct = (i ? r[i] * (7 / 8.0) / (total ? total : 1) : 1 / 8.0) / sum * 100;
    }
    return 5;
}

int biorand_enemy_dist(const BioOpts* o, BioSlice out[6])
{
    int total = 0, n = 0;
    for (int i = 0; i < 6; i++) total += o->EnemyRatios[i];
    for (int i = 0; i < 6; i++)
        if (o->EnemyRatios[i]) {
            out[n].name = enemy_names[i];
            out[n].rgb = enemy_rgb[i];
            out[n++].pct = o->EnemyRatios[i] * 100.0 / total;
        }
    return n;
}

void biorand_doors_estimate(const BioOpts* o, char* out, size_t n)
{
    int lo = ((o->AreaCount + 1) * 60 + 30) / 60, hi = ((o->AreaSize + 1) * 45 + 30) / 60;
    if (lo > hi) lo = hi;
    if (hi < lo + 1) hi = lo + 1;
    if (!o->RandomDoors) snprintf(out, n, "%s", "");
    else if (o->AreaSize <= 0) snprintf(out, n, "15 to 30 minutes per scenario");
    else if (o->AreaSize <= 1) snprintf(out, n, "30 to 60 minutes per scenario");
    else snprintf(out, n, "%d to %d hours per scenario", lo, hi);
}

/* ---------------------------------------------------------------- recvx.ini */
void biorand_opts_load(BioOpts* o)
{
    const char* why = g_biorand_seed[0] ? biorand_seed_decode(g_biorand_seed, o) : "";
    if (why) {
        if (*why) host_log("biorand: recvx.ini biorand_seed %s is not usable, the defaults instead: %s\n", g_biorand_seed, why);
        biorand_opts_default(o);
    }
    biorand_opts_rules(o);
}

void biorand_opts_save(const BioOpts* o)
{
    biorand_seed_encode(o, g_biorand_seed, sizeof(g_biorand_seed));
    settings_save_later();
}
