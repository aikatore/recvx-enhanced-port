/* GAME SETUP (--new-menu; docs/MENUS.md, the proposal's §2 and §8): what a run is. NEW GAME opens it in title mode 30 (as
 * BIORAND and the lobby: lobby_glue.c pc_lobby), EXTRA > BIORAND too (Game Type BioRand, on the BIORAND category); in a
 * game the pause menu shows the same categories (pause_glue.c, the OPTION task). The row screen is ui_rows.c; this file
 * is what GAME SETUP adds to it:
 *  - where it is shown (g_setup_ctx: NEW GAME, in game, the lobby's host and guest for P3), which categories and rows
 *    each shows and which are read-only there ("Set when the game started.", "Set by the host.");
 *  - its rows that are not one settings-table row (menu_layout.h ML_VAR: Game Type, Difficulty, Preset, You Look Like,
 *    Claire and Chris, Random From, Story Characters, Cutscene Monsters, ...; ML_BIO: BioRand's generator options, kept
 *    here and in recvx.ini's biorand_seed as the BIORAND screen keeps them; ML_ACTION: START GAME, RANDOM SEED, ...);
 *  - OVERVIEW's ACTIVE MODIFIERS, the previews (CHARACTERS' "IN CUTSCENES THIS GAME", START GAME's status, BioRand's
 *    seed string, doors estimate and item distribution), the LOAD GAME note's line (save_glue.c);
 *  - the mode 30 screen: the title picture darkened, NEW GAME's plate, the rows; START GAME (Standard: the game starts;
 *    BioRand: as installed, or BioRand's dialogs first, biorand_menu.c pc_biorand_flow: download, generate, start);
 *    the 3D look picker (the lobby's model page, pc_lobby_models).
 * Every value lives where it did (g_enemy_opts, g_config, g_appear_*, g_coop.chr, g_biorand_*): the old menus show the
 * same. Game thread. */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/adv.h"
#include "../../decomp/include/ps2/veronica/prog/padman.h"
#include "../../decomp/include/ps2/veronica/prog/sdfunc.h"
#include "../src/host/settings.h"
#include "../src/host/menu_layout.h"
#include "../src/host/lang.h"
#include "../src/host/lobby.h"
#include "../src/host/enemies.h"
#include "../src/host/cast.h"
#include "../src/host/actors.h"
#include "../src/host/coop.h"
#include "../src/host/biorand.h"
#include "../src/host/biorand_opts.h"
#include "../src/net/biorand_dl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern ADV_WORK AdvWork;
void host_log(const char* fmt, ...);
void pc_ui_text(float x, float y, const char* s, float sc, unsigned int argb);
float pc_ui_text_w(const char* s, float sc);
void pc_ui_rect(float x0, float y0, float x1, float y1, unsigned int argb, float z);
int pc_ui_text_wrap(float x, float y, float w, const char* s, float sc, unsigned int argb, int max_lines);
void pc_title_word_newgame(float cx, float y, unsigned int col, unsigned int shadow);   /* lobby_glue.c */
void pc_lobby_models(const Setting* s, int c);   /* lobby_glue.c: the 3D model page for a look row */
void pc_biorand_flow_open(int what, int d);      /* biorand_menu.c: its dialogs for START GAME (1) or the update check (2) */
int  pc_biorand_flow(void);
void pc_biorand_menu_close(void);
void overlay_request(int which);                 /* overlay.cpp: an F-key window (4: F9, import) */
int  pc_newgame_difficulty(int set);             /* host.c: recvx.ini newgame_difficulty (set -1: read only) */
int  pc_cfg_no_ink_ribbon(void);
int  pc_cfg_extra_objects(void);

#define C_GOLD 0xFFD6B668u
#define C_HELP 0xFFC8C8C2u
#define C_GREY 0xFF9A9A94u

int g_setup_ctx = SC_NEWGAME;
static struct {
    int open;            /* the NEW GAME screen is up (mode 30) */
    int type;            /* Game Type: 0 Standard, 1 BioRand (the next game's; not saved: NEW GAME starts Standard) */
    int flow;            /* BioRand's dialogs are up (pc_biorand_flow) */
    int pasting;         /* PASTE SEED STRING asked the clipboard */
    int bio_loaded;
    BioOpts bio;         /* the generator's options (recvx.ini biorand_seed) */
    BioOpts game_bio;    /* in a BioRand game: the pack's (its seed string), read-only */
    char game_str[BIORAND_SEED_CAP];
    int keep_chr, keep_own[2], kept;   /* You Look Like Original: the chosen looks, back with Chosen Model (this run) */
} gs;

static int ctx(void) { return g_setup_ctx >= SC_LOBBY_HOST ? g_setup_ctx : pc_in_game() ? SC_INGAME : SC_NEWGAME; }
static const Setting* row(const char* label) { int k = menu_setting(label); return k >= 0 ? &g_settings[k] : NULL; }
static int get_of(const char* label) { const Setting* s = row(label); return s ? settings_get(s) : 0; }
static void set_of(const char* label, int v) { const Setting* s = row(label); if (s && !settings_locked(s)) settings_set(s, v); }
static int imports_n(void) { return __atomic_load_n(&actor_import_n, __ATOMIC_ACQUIRE); }

/* ---------------------------------------------------------------- BioRand's options */
static BioOpts* bio(void)
{
    if (ctx() == SC_INGAME && biorand_active()) {   /* the game's: its pack's seed string */
        const char* ps = biorand_seed_string();
        if (strcmp(ps, gs.game_str)) {
            snprintf(gs.game_str, sizeof(gs.game_str), "%s", ps);
            if (!ps[0] || biorand_seed_decode(ps, &gs.game_bio)) biorand_opts_default(&gs.game_bio);
        }
        return &gs.game_bio;
    }
    if (!gs.bio_loaded) { biorand_opts_load(&gs.bio); gs.bio_loaded = 1; }
    return &gs.bio;
}
static void bio_saved(void) { biorand_opts_save(&gs.bio); }
static const char* bio_string(char* buf, int n) { biorand_seed_encode(bio(), buf, (size_t)n); return buf; }
/* the installed pack is this setup's (START GAME plays it as it is) */
static int bio_ready(void) { char t[BIORAND_SEED_CAP]; return biorand_pack() && !strcmp(biorand_seed_string(), bio_string(t, sizeof(t))); }
static int bio_at(const MenuRow* m) { return m->kind == ML_BIO ? biorand_opt_find(m->ref) : -1; }

/* ---------------------------------------------------------------- the looks (YOU) */
static int look_original(void) { return g_appear_per ? appear_choice(g_coop.chr, 0) == 0 && appear_choice(g_coop.chr, 1) == 0 : g_coop.chr == 0; }
static int look_now(void)   /* You Look Like: 0 Original, 1 Chosen Model, 2 Random */
{
    if (g_biorand_appear || pc_biorand_appear(0) >= 0) return 2;
    return look_original() ? 0 : 1;
}
static void look_set(int v)
{
    if (v != 2 && pc_in_game()) pc_biorand_appear_drop();   /* (this game's random picks go, as choosing a look in game does) */
    if (v == 0) {
        if (!look_original()) { gs.keep_chr = g_coop.chr; gs.keep_own[0] = g_appear_own[0]; gs.keep_own[1] = g_appear_own[1]; gs.kept = 1; }
        g_biorand_appear = 0;
        g_coop.chr = 0;
        g_appear_own[0] = g_appear_own[1] = -1;
    } else if (v == 1) {
        g_biorand_appear = 0;
        if (look_original()) {
            if (gs.kept) { g_coop.chr = gs.keep_chr; g_appear_own[0] = gs.keep_own[0]; g_appear_own[1] = gs.keep_own[1]; }
            if (look_original()) { g_coop.chr = 5; g_appear_own[0] = g_appear_own[1] = -1; }   /* (a first look: Claire's second costume, as the proposal's mockups) */
        }
    } else {
        g_biorand_appear = 1 + g_appear_per;
        if (pc_in_game()) pc_biorand_appear_new();   /* (picked now, as a new game would: from this game's seed) */
    }
    settings_save_later();
    host_log("setup: you look like %s\n", v == 0 ? "the original" : v == 1 ? appear_name(appear_choice(g_coop.chr, 0)) : "a random look");
}
static int look_mode(void) { return g_biorand_appear ? g_biorand_appear == 2 : g_appear_per; }
static void look_mode_set(int v)
{
    set_of("Character Models", v);
    if (g_biorand_appear) g_biorand_appear = 1 + v;
    settings_save_later();
}
/* a look's short name ("Wesker", "random") */
static const char* look_name(int c, char* buf, int n)
{
    int o = pc_biorand_appear(c);
    if (o < 0 && look_now() == 2) snprintf(buf, n, "%s", tr("a random look"));
    else snprintf(buf, n, "%s", tr(appear_name(o >= 0 ? o : appear_choice(g_coop.chr, c))));
    return buf;
}

/* ---------------------------------------------------------------- the cast (STORY CHARACTERS, MONSTERS) */
static int story_now(void) { return g_enemy_opts.cast ? 1 + g_enemy_opts.cast_all : 0; }   /* Original, Swapped, Any Model, Custom */
static void story_set(int v)
{
    set_of("Randomize Cutscene Cast", v > 0);
    if (v > 0) set_of("Model Selection", v - 1);
}
enum { MV_ORIG, MV_ANY, MV_LIKE, MV_CUSTOM };   /* Cutscene Monsters' names (menu_layout.c) */
static int mon_ids(int cast, int* id)   /* the values shown: "Like Story Characters" only with the cast */
{
    int n = 0;
    id[n++] = MV_ORIG; id[n++] = MV_ANY;
    if (cast) id[n++] = MV_LIKE;
    id[n++] = MV_CUSTOM;
    return n;
}
static int mon_id(int cast, int enemy, int alone)   /* what cast_enemy (and the flag without the cast) shows as */
{
    if (cast) return enemy == CAST_ENEMY_OFF ? MV_ORIG : enemy == CAST_ENEMY_ALL ? MV_ANY : enemy == CAST_ENEMY_SAME ? MV_LIKE : MV_CUSTOM;
    if (!alone) return MV_ORIG;
    return enemy == CAST_ENEMY_ALL ? MV_ANY : enemy == CAST_ENEMY_CUSTOM ? MV_CUSTOM : MV_ORIG;
}
static int mon_pos(int id) { int ids[4], n = mon_ids(g_enemy_opts.cast, ids); for (int i = 0; i < n; i++) if (ids[i] == id) return i; return 0; }
static void mon_set(int pos)
{
    int ids[4], n = mon_ids(g_enemy_opts.cast, ids), id = ids[pos < 0 ? 0 : pos >= n ? n - 1 : pos];
    static const int enemy[4] = { CAST_ENEMY_OFF, CAST_ENEMY_ALL, CAST_ENEMY_SAME, CAST_ENEMY_CUSTOM };
    if (g_enemy_opts.cast) set_of("Enemy Models", enemy[id]);
    else {
        enemy_cast_alone_set(&g_enemy_opts, id != MV_ORIG);
        if (id != MV_ORIG) set_of("Enemy Models", enemy[id]);
    }
    settings_save_later();   /* (the flag: recvx.ini cast_monsters_alone) */
}

/* ---------------------------------------------------------------- Preset (OVERVIEW) */
static int preset_value(int p, int k, int* v)   /* row k's value under preset p (0 Original: its default) */
{
    const MenuPreset* t = p == 1 ? menu_remix : menu_chaos;
    int n = p == 1 ? menu_remix_n : p == 2 ? menu_chaos_n : 0;
    for (int i = 0; i < n; i++) if (menu_setting(t[i].ref) == k) { *v = t[i].value; return 1; }
    return menu_default(k, v);
}
static int preset_now(void)   /* 0 Original, 1 Remix, 2 Chaos, 3 Custom */
{
    for (int p = 0; p < 3; p++) {
        int ok = !enemy_cast_alone(&g_enemy_opts);
        for (int i = 0; i < menu_setup_owned_n && ok; i++) {
            int k = menu_setting(menu_setup_owned[i]), v;
            if (k >= 0 && preset_value(p, k, &v)) ok = settings_get(&g_settings[k]) == v;
        }
        if (ok) return p;
    }
    return 3;
}
static void preset_set(int p)
{
    if (p > 2) return;
    for (int i = 0; i < menu_setup_owned_n; i++) {
        int k = menu_setting(menu_setup_owned[i]), v;
        if (k >= 0 && !settings_locked(&g_settings[k]) && preset_value(p, k, &v)) settings_set(&g_settings[k], v);
    }
    enemy_cast_alone_set(&g_enemy_opts, 0);
    settings_save_later();
    host_log("setup: preset %s\n", p == 0 ? "Original" : p == 1 ? "Remix" : "Chaos");
}

/* ---------------------------------------------------------------- where it is shown, what is read-only */
int setup_cat_shown(int c)
{
    switch (c) {
    case MC_BIORAND: return ctx() == SC_INGAME ? biorand_active() : gs.type;
    case MC_COOP_RULES: return ctx() != SC_NEWGAME;   /* (the lobby's; in game, as P1 had it) */
    }
    return 1;
}

int setup_row_shown(const MenuRow* m, int shown)
{
    const char* r = m->ref ? m->ref : "";
    int look = look_now();
    if (m->kind == ML_SHORTCUT) return 1;
    if (m->kind == ML_ACTION && m->arg == SA_START) return ctx() == SC_NEWGAME || ctx() == SC_LOBBY_HOST;
    if (m->kind == ML_VAR)
        switch (m->arg) {
        case SV_RANDOM_FROM: return look == 2;
        case SV_RANDOM_IMPORTS: return look == 2 && imports_n() > 0;
        }
    if (m->kind != ML_SET) return shown;
    if (!strcmp(r, "Character Model")) return look == 1 && !g_appear_per;
    if (!strcmp(r, "Claire's Model") || !strcmp(r, "Chris's Model")) return look == 1 && g_appear_per;
    if (!strcmp(r, "Custom Cutscene Model")) return look != 0;
    return shown;
}

const char* setup_row_lock(const MenuRow* m)
{
    static char why[128];
    int c = ctx(), i;
    if (c == SC_LOBBY_GUEST && m->cat != MC_CHAR_ADV && !(m->cat == MC_CHARACTERS && m->kind == ML_VAR && m->arg <= SV_RANDOM_IMPORTS)
        && !(m->ref && (!strcmp(m->ref, "Character Model") || !strcmp(m->ref, "Claire's Model") || !strcmp(m->ref, "Chris's Model")
                        || !strcmp(m->ref, "Custom Cutscene Model") || !strcmp(m->ref, "Talking Mouths")))
        && m->kind != ML_WINDOW && !(m->kind == ML_ACTION && m->arg == SA_BIO_COPY))
        return "Set by the host.";   /* (P3: the guest's YOU and Extra Mouth Movement stay its own) */
    if (c == SC_INGAME && ((m->kind == ML_VAR && (m->arg == SV_GAME_TYPE || m->arg == SV_DIFFICULTY)) || m->kind == ML_BIO
                           || (m->kind == ML_ACTION && (m->arg == SA_BIO_RANDOM || m->arg == SA_BIO_PASTE || m->arg == SA_BIO_CHECK))))
        return "Set when the game started.";
    if (m->ref && !strcmp(m->ref, "Talking Mouths") && !g_enemy_opts.voices) return "Turn on Voices Follow the Look first.";
    if (m->ref && !g_enemy_opts.rand && (!strcmp(m->ref, "Enemy Selection") || !strcmp(m->ref, "Types per Room") || !strcmp(m->ref, "Randomize Event Enemies")))
        return "Turn on Enemy Randomizer first.";
    if (m->kind == ML_VAR && (m->arg == SV_STORY || m->arg == SV_MONSTERS)) return settings_locked(row(m->arg == SV_STORY ? "Randomize Cutscene Cast" : "Enemy Models"));
    if ((i = bio_at(m)) >= 0) {
        const char* l = biorand_opt_locked(bio(), i);
        if (l) return l;
        if (biorand_opt_active(bio(), i)) return NULL;
        for (int p = biorand_opt_find(biorand_opt[i].parent); p >= 0; p = biorand_opt_find(biorand_opt[p].parent))
            if (!biorand_opt_get(bio(), p)) {
                int k;
                const char* label = biorand_opt[p].label;
                for (k = 0; k < menu_rows_n; k++) if (menu_rows[k].kind == ML_BIO && !strcmp(menu_rows[k].ref, biorand_opt[p].key) && menu_rows[k].label) label = menu_rows[k].label;
                snprintf(why, sizeof(why), tr("Turn on %s first."), tr(label));
                return why;
            }
    }
    return NULL;
}

int setup_custom_list(const MenuRow* m)
{
    if (m->kind != ML_VAR) return -1;
    switch (m->arg) {
    case SV_STORY: return story_now() == 3 ? 0 : -1;
    case SV_RANDOM_FROM: return g_biorand_appear_pool == 2 ? 1 : -1;
    case SV_MONSTERS: return mon_id(g_enemy_opts.cast, g_enemy_opts.cast_enemy, enemy_cast_alone(&g_enemy_opts)) == MV_CUSTOM ? 2 : -1;
    }
    return -1;
}

/* ---------------------------------------------------------------- ML_VAR and ML_BIO rows */
int setup_choice(const MenuRow* m, int* n)
{
    int i = bio_at(m), ids[4];
    if (i >= 0) {
        const BioOpt* d = &biorand_opt[i];
        *n = d->type == BO_BOOL ? 2 : d->type == BO_ENUM ? d->hi - d->lo + 1 : 0;
        return *n > 0;
    }
    switch (m->arg) {
    case SV_GAME_TYPE: case SV_LOOK_MODE: case SV_RANDOM_IMPORTS: *n = 2; return 1;
    case SV_DIFFICULTY: case SV_LOOK: case SV_RANDOM_FROM: *n = 3; return 1;
    case SV_PRESET: case SV_STORY: *n = 4; return 1;
    case SV_MONSTERS: *n = mon_ids(g_enemy_opts.cast, ids); return 1;
    }
    *n = 0;
    return 0;
}

void setup_range(const MenuRow* m, int* lo, int* hi)
{
    int i = bio_at(m), n;
    if (i >= 0) { *lo = biorand_opt[i].lo; *hi = biorand_opt[i].hi; return; }
    setup_choice(m, &n);
    *lo = 0; *hi = n - 1;
}

int setup_get(const MenuRow* m)
{
    int i = bio_at(m);
    if (i >= 0) return biorand_opt_get(bio(), i);
    switch (m->arg) {
    case SV_GAME_TYPE: return ctx() == SC_INGAME ? biorand_active() : gs.type;
    case SV_DIFFICULTY: return ctx() == SC_INGAME ? (sys->gm_mode <= 2 ? sys->gm_mode : 0) : pc_newgame_difficulty(-1);
    case SV_PRESET: return preset_now();
    case SV_LOOK: return look_now();
    case SV_LOOK_MODE: return look_mode();
    case SV_RANDOM_FROM: return g_biorand_appear_pool;
    case SV_RANDOM_IMPORTS: return g_biorand_appear_imports > 0;
    case SV_STORY: return story_now();
    case SV_MONSTERS: return mon_pos(mon_id(g_enemy_opts.cast, g_enemy_opts.cast_enemy, enemy_cast_alone(&g_enemy_opts)));
    }
    return 0;
}

void setup_set(const MenuRow* m, int v)
{
    int i = bio_at(m);
    if (i >= 0) {
        biorand_opt_set(&gs.bio, i, v);
        bio_saved();
        host_log("setup: BioRand %s = %d, seed string %s\n", biorand_opt[i].key, biorand_opt_get(&gs.bio, i), g_biorand_seed);
        return;
    }
    switch (m->arg) {
    case SV_GAME_TYPE: gs.type = v != 0; break;
    case SV_DIFFICULTY: pc_newgame_difficulty(v < 0 ? 0 : v > 2 ? 2 : v); settings_save_later(); break;
    case SV_PRESET: preset_set(v); break;
    case SV_LOOK: look_set(v); break;
    case SV_LOOK_MODE: look_mode_set(v); break;
    case SV_RANDOM_FROM: g_biorand_appear_pool = v; settings_save_later(); break;
    case SV_RANDOM_IMPORTS: g_biorand_appear_imports = v; settings_save_later(); break;
    case SV_STORY: story_set(v); break;
    case SV_MONSTERS: mon_set(v); break;
    }
}

void setup_step(const MenuRow* m, int dir)
{
    int n, lo, hi, v = setup_get(m);
    if (setup_choice(m, &n)) {
        if (m->kind == ML_VAR && m->arg == SV_PRESET) v = v == 3 ? (dir > 0 ? 0 : 2) : (v + dir + 3) % 3;   /* (Custom is not chosen: it is what is left) */
        else v = (v + dir + n) % n;
    } else {
        setup_range(m, &lo, &hi);
        v += dir;
        if (v < lo || v > hi) return;   /* (a number stops at its ends) */
    }
    setup_set(m, v);
}

const char* setup_name(const MenuRow* m, int v, char* buf, int n)
{
    int i = bio_at(m), ids[4];
    if (i >= 0) {
        const BioOpt* d = &biorand_opt[i];
        if (d->type == BO_BOOL) snprintf(buf, n, "%s", tr(v ? "On" : "Off"));
        else if (d->type == BO_ENUM) snprintf(buf, n, "%s", tr(d->names[v - d->lo]));
        else if (!d->low) snprintf(buf, n, "%d", v);   /* (the seed) */
        else if (d->low[0] >= '0' && d->low[0] <= '9') snprintf(buf, n, "%d", atoi(d->low) + v - d->lo);   /* (Number of Segments: 1..4) */
        else if (v == d->lo) snprintf(buf, n, "%s", tr(d->low));
        else if (v == d->hi) snprintf(buf, n, "%s", tr(d->high));
        else snprintf(buf, n, "%d / %d", v - d->lo, d->hi - d->lo);
        return buf;
    }
    if (m->arg == SV_MONSTERS) { mon_ids(g_enemy_opts.cast, ids); v = ids[v < 0 ? 0 : v > 3 ? 3 : v]; }
    if (!m->names || v < 0) { snprintf(buf, n, "%d", v); return buf; }
    snprintf(buf, n, "%s", tr(m->names[v]));
    return buf;
}

int setup_default(const MenuRow* m, int* d)
{
    int i = bio_at(m), v;
    if (i >= 0) { *d = biorand_opt[i].def; return strcmp(m->ref, "Seed") != 0; }   /* (the seed has none) */
    switch (m->arg) {
    case SV_GAME_TYPE: case SV_DIFFICULTY: case SV_PRESET: case SV_LOOK_MODE: case SV_RANDOM_IMPORTS: *d = 0; return 1;
    case SV_RANDOM_FROM: *d = 1; return 1;   /* (biorand_opts.c: All Models, as before) */
    case SV_STORY: { int c = 0, a = 0; menu_default(menu_setting("Randomize Cutscene Cast"), &c); menu_default(menu_setting("Model Selection"), &a); *d = c ? 1 + a : 0; return 1; }
    case SV_MONSTERS: if (!menu_default(menu_setting("Enemy Models"), &v)) return 0; *d = mon_pos(mon_id(g_enemy_opts.cast, v, 0)); return 1;
    }
    return 0;   /* (You Look Like: your own, no default) */
}

const char* setup_label(const MenuRow* m) { int i = bio_at(m); return i >= 0 ? biorand_opt[i].label : m->label ? m->label : ""; }
const char* setup_help(const MenuRow* m)
{
    static char t[400];
    int i = bio_at(m);
    if (m->help) return m->help;
    if (i < 0) return NULL;
    snprintf(t, sizeof(t), "%s", tr(biorand_opt[i].help ? biorand_opt[i].help : "Choose a starting weapon type: None, Random, Sidearm, Primary or Powerful."));
    for (char* p = t; *p; p++) if (*p == '\n') *p = ' ';
    if (!strcmp(m->ref, "Seed")) snprintf(t + strlen(t), sizeof(t) - strlen(t), " %s", tr("Confirm: type a number."));
    return t;
}

/* ---------------------------------------------------------------- actions */
static int random_seed(int hi)
{
    static unsigned x;
    x = x * 1664525u + 1013904223u + (unsigned)time(NULL) * 2654435761u + (unsigned)clock();
    return (int)((x >> 4) % (hi + 1u));
}

static int start_game(void)
{
    int d = pc_newgame_difficulty(-1);
    if (ctx() != SC_NEWGAME) return UR_STAY;   /* (the lobby's START comes with P3) */
    if (gs.type && !bio_ready()) {   /* BioRand's dialogs: generate (and download it first), then start */
        bio_saved();
        gs.flow = 1;
        pc_biorand_flow_open(1, d);
        return UR_FLOW;
    }
    biorand_game(gs.type);
    sys->gm_mode = d;
    host_log("setup: START GAME: %s, difficulty %d, %s\n", gs.type ? "BioRand (the installed game)" : "Standard", d, gs.type ? biorand_seed_text() : "");
    return UR_START;
}

int setup_action(const MenuRow* m)
{
    char t[160];
    switch (m->arg) {
    case SA_START: return start_game();
    case SA_ENEMY_SEED: {
        const Setting* s = settings_of(&g_enemy_opts.seed);
        settings_set(s, random_seed(s->hi));
        snprintf(t, sizeof(t), tr("New seed: %d"), g_enemy_opts.seed);
        ui_rows_say(t);
        CallSystemSe(0, 3);
        return UR_STAY;
    }
    case SA_BIO_RANDOM:
        biorand_opt_set(&gs.bio, biorand_opt_find("Seed"), random_seed(BIORAND_SEED_MAX));
        bio_saved();
        snprintf(t, sizeof(t), tr("New seed: %d"), gs.bio.Seed);
        ui_rows_say(t);
        CallSystemSe(0, 3);
        return UR_STAY;
    case SA_BIO_COPY: {
        char str[BIORAND_SEED_CAP];
        snprintf(g_lobby_io.clip, sizeof(g_lobby_io.clip), "%s", bio_string(str, sizeof(str)));
        g_lobby_io.clip_req = 1;   /* (lobby_io.c sets the clipboard) */
        snprintf(t, sizeof(t), tr("Seed string copied: %s"), str);
        ui_rows_say(t);
        host_log("setup: copied %s\n", str);
        CallSystemSe(0, 3);
        return UR_STAY;
    }
    case SA_BIO_PASTE: g_lobby_io.paste_req = 1; gs.pasting = 1; CallSystemSe(0, 3); return UR_STAY;
    case SA_BIO_CHECK: gs.flow = 2; pc_biorand_flow_open(2, 0); CallSystemSe(0, 3); return UR_FLOW;
    }
    return UR_STAY;
}

static void paste_end(const char* text)
{
    BioOpts o;
    char m[96];
    const char* why = biorand_seed_decode(text, &o);
    if (why) { ui_rows_say(why); CallSystemSe(0, 1); return; }
    biorand_opts_rules(&o);
    gs.bio = o;
    bio_saved();
    snprintf(m, sizeof(m), tr("Seed %d and options pasted."), o.Seed);
    ui_rows_say(m);
}

/* ---------------------------------------------------------------- what this run is (ACTIVE MODIFIERS, the LOAD note) */
enum { MOD_MAX = 12 };
/* the modifiers in effect, one line each (short: the LOAD GAME note's names); the count */
static int modifiers(char out[MOD_MAX][128], int brief)
{
    char a[64], b[64];
    int n = 0;
    const EnemyOpts* o = &g_enemy_opts;
#define ADD(...) do { if (n < MOD_MAX) snprintf(out[n++], 128, __VA_ARGS__); } while (0)
    if (o->rand) {
        if (brief) ADD("%s", tr("Enemy Randomizer"));
        else ADD("%s: %s%s", tr("Enemy Randomizer"), settings_value(settings_of(&o->variety), a, sizeof(a)), o->events ? tr(", Event Enemies On") : "");
    }
    if (o->extra) { if (brief) ADD("%s", tr("Extra Enemies")); else ADD("%s: %s", tr("Extra Enemies"), settings_value(settings_of(&o->extra), a, sizeof(a))); }
    if (o->place) ADD("%s", tr("Rearranged Enemies"));
    {   /* the looks, in one line */
        char t[128] = "";
        int look = look_now(), story = story_now(), mon = mon_id(o->cast, o->cast_enemy, enemy_cast_alone(o));
        if (look == 2) snprintf(t, sizeof(t), "%s", tr("you random"));
        else if (look == 1 && !g_appear_per) snprintf(t, sizeof(t), tr("you %s"), look_name(0, a, sizeof(a)));
        else if (look == 1) snprintf(t, sizeof(t), tr("you Claire %s, Chris %s"), look_name(0, a, sizeof(a)), look_name(1, b, sizeof(b)));
        if (story) {
            static const char* const st[] = { "", "swapped", "any model", "custom" };
            snprintf(t + strlen(t), sizeof(t) - strlen(t), "%s%s %s%s", t[0] ? "; " : "", tr("story characters"), tr(st[story]), o->cast_assign ? tr(", new each scene") : "");
        }
        if (mon != MV_ORIG) {
            static const char* const mt[] = { "", "any monster", "like story characters", "custom" };
            snprintf(t + strlen(t), sizeof(t) - strlen(t), "%s%s %s", t[0] ? "; " : "", tr("monsters"), tr(mt[mon]));
        }
        if (t[0]) { if (brief) ADD("%s", tr("Looks")); else ADD("%s: %s", tr("Looks"), t); }
    }
    if (o->voices) ADD("%s", tr("Voices Follow the Look"));
    if (pc_cfg_extra_objects()) ADD("%s", tr("Extra Item Boxes and Herbs"));
    if (pc_cfg_no_ink_ribbon()) ADD("%s", tr("Save Without Ink Ribbons"));
    {
        int k = menu_setting("Headshots & Piercing"), d;
        if (k >= 0 && menu_default(k, &d) && settings_get(&g_settings[k]) != d) ADD("%s: %s", tr("Headshots and Piercing"), tr(settings_get(&g_settings[k]) ? "On" : "Off"));
    }
#undef ADD
    return n;
}

const char* setup_summary(char* buf, int n)
{
    char m[MOD_MAX][128];
    int k = modifiers(m, 1);
    buf[0] = 0;
    for (int i = 0; i < k; i++) snprintf(buf + strlen(buf), n - (int)strlen(buf), "%s%s", i ? ", " : "", m[i]);
    return buf;
}

/* OVERVIEW's box: BioRand's line, the modifiers (or none), then the seed and a note */
int setup_info(const MenuRow* m, int line, char* buf, int n, unsigned int* col)
{
    char mods[MOD_MAX][128], lines[MOD_MAX + 4][128];
    int k = 0, nm, i, boxed;
    if (m->arg == SI_BIO_ENEMIES) {
        if (buf) { snprintf(buf, n, "%s", tr("BioRand does not change this game's enemies: see ENEMIES.")); *col = C_GREY; }
        return 1;
    }
    nm = modifiers(mods, 0);
    if (setup_get(&(MenuRow){ MC_OVERVIEW, NULL, ML_VAR, NULL, SV_GAME_TYPE })) {
        const BioOpts* o = bio();
        snprintf(lines[k++], 128, tr("BioRand: seed %d%s%s"), o->Seed, o->RandomDoors ? tr(", Doors") : "", o->RandomItems ? tr(", Items") : "");
    }
    if (!nm && !k) snprintf(lines[k++], 128, "%s", tr("None: the original game."));
    for (i = 0; i < nm && k < MOD_MAX; i++) snprintf(lines[k++], 128, "%s", mods[i]);
    boxed = k;
    /* below the box, grey: BioRand's state or the seed the modifiers use, and when rules can change */
    if (ctx() == SC_NEWGAME && gs.type) snprintf(lines[k++], 128, "%s", tr(bio_ready() ? "BioRand: ready." : "BioRand: will generate at start (about 20 s)."));
    else if (g_enemy_opts.rand || g_enemy_opts.place || g_enemy_opts.extra || g_enemy_opts.cast || look_now() == 2) snprintf(lines[k++], 128, tr("Enemy Randomizer Seed: %d"), g_enemy_opts.seed);
    snprintf(lines[k++], 128, "%s", tr(ctx() == SC_INGAME ? "Rules apply from the next room." : "Rules can also be changed later from the pause menu."));
    if (buf) {
        snprintf(buf, n, "%s", line >= 0 && line < k ? lines[line] : "");
        *col = line < boxed ? 0xFFE8E8E2u : C_GREY;
    }
    return line < 0 ? k : boxed;   /* (a line's call: the boxed lines' count, ui_rows.c draws their box) */
}

/* ---------------------------------------------------------------- the previews */
static void line_at(float x, float y, float w, const char* s, float sc, unsigned int col)
{
    float tw = pc_ui_text_w(s, sc), k = tw > w ? w / tw : 1.0f;
    pc_ui_text(x, y, s, sc * k, col);
}

/* CHARACTERS: who looks like whom in this game's cutscenes */
static void who_is_who(float x, float y, float w)
{
    char a[96], b[96], t[200];
    const EnemyOpts* o = &g_enemy_opts;
    int look = look_now(), mon = mon_id(o->cast, o->cast_enemy, enemy_cast_alone(o)), sep = look == 2 ? look_mode() : g_appear_per, row = 0;
    static const char* const st[] = { "original", "swapped", "any model", "custom looks" };
    static const char* const mt[] = { "original", "any monster", "like story characters", "custom" };
    pc_ui_text(x, y, tr("IN CUTSCENES THIS GAME"), 0.46f, C_GOLD);
    y += 16;
    if (look == 0 || !g_appear_cutscene) line_at(x, y + 11 * row++, w, tr("Claire and Chris: original"), 0.4f, C_HELP);
    else if (!sep) {
        snprintf(t, sizeof(t), tr("Played: your look (%s)"), look_name(0, a, sizeof(a)));
        line_at(x, y + 11 * row++, w, t, 0.4f, C_HELP);
        line_at(x, y + 11 * row++, w, tr("Claire or Chris not played: original"), 0.4f, C_HELP);
    } else {
        snprintf(t, sizeof(t), tr("Claire: your look (%s)"), look_name(0, a, sizeof(a)));
        line_at(x, y + 11 * row++, w, t, 0.4f, C_HELP);
        snprintf(t, sizeof(t), tr("Chris: your look (%s)"), look_name(1, b, sizeof(b)));
        line_at(x, y + 11 * row++, w, t, 0.4f, C_HELP);
    }
    snprintf(t, sizeof(t), tr("Others: %s%s"), tr(st[story_now()]), story_now() ? tr(o->cast_assign ? ", new each scene" : ", whole game") : "");
    line_at(x, y + 11 * row++, w, t, 0.4f, C_HELP);
    snprintf(t, sizeof(t), tr("Monsters: %s; voices %s"), tr(mt[mon]), tr(o->voices ? "follow the look" : "original"));
    line_at(x, y + 11 * row++, w, t, 0.4f, C_HELP);
}

int setup_preview(const MenuRow* m, float x, float y, float w)
{
    char t[200], s[BIORAND_SEED_CAP];
    int top = m->cat;
    while (menu_cats[top].parent >= 0) top = menu_cats[top].parent;
    if (top == MC_CHARACTERS || (m->kind == ML_SHORTCUT && m->arg == MC_CHARACTERS)) { who_is_who(x, y, w); return 1; }
    if (m->kind == ML_ACTION && m->arg == SA_START) {
        char mods[MOD_MAX][128];
        int n = modifiers(mods, 1);
        static const char* const diff[] = { "Normal", "Easy", "Very Easy" };
        int ready = !gs.type || bio_ready();
        pc_ui_text(x, y, tr(ready ? "READY" : "BIORAND: GENERATES FIRST"), 0.46f, C_GOLD);
        snprintf(t, sizeof(t), tr("%s game, %s"), tr(gs.type ? "BioRand" : "Standard"), tr(diff[pc_newgame_difficulty(-1) % 3]));
        line_at(x, y + 16, w, t, 0.42f, C_HELP);
        snprintf(t, sizeof(t), tr(n == 1 ? "%d modifier active" : "%d modifiers active"), n);
        line_at(x, y + 29, w, n ? t : tr("No modifiers"), 0.42f, C_HELP);
        return 1;
    }
    if (top != MC_BIORAND) return 0;
    if (m->kind == ML_ACTION && m->arg == SA_BIO_CHECK) {
        pc_ui_text(x, y, tr("BIORAND PROGRAM"), 0.46f, C_GOLD);
        line_at(x, y + 16, w, tr(biorand_cli_present() ? "Installed." : "Not installed: START GAME downloads it."), 0.42f, C_HELP);
        return 1;
    }
    if (m->cat == MC_BIO_POOL || (m->ref && (!strcmp(m->ref, "RandomItems") || !strcmp(m->ref, "AlternativeRoutes") || !strcmp(m->ref, "HiddenKeyItems")))
        || (m->kind == ML_PAGE && m->arg == MC_BIO_POOL)) {   /* ITEMS: the distribution */
        BioSlice sl[5];
        const BioOpts* o = bio();
        float bx = x, bw = w;
        pc_ui_text(x, y, tr("ITEM DISTRIBUTION"), 0.46f, C_GOLD);
        if (!o->RandomItems) { line_at(x, y + 16, w, tr("Items are not randomized."), 0.42f, C_HELP); return 1; }
        if (o->ShuffleItems) { line_at(x, y + 16, w, tr("Custom item pool is off: the game's own items are shuffled."), 0.42f, C_HELP); return 1; }
        {
            int n = biorand_item_dist(o, sl), k;
            float lx = x, ly = y + 32;
            for (k = 0; k < n; k++) {
                float x1 = k == n - 1 ? x + w : bx + bw * (float)sl[k].pct / 100.0f;
                if (x1 - bx >= 1.0f) pc_ui_rect(bx, y + 17, x1, y + 27, 0xFF000000u | sl[k].rgb, 0.33f + k * 0.002f);
                bx = x1; bw = w;
            }
            for (k = 0; k < n; k++) {
                snprintf(t, sizeof(t), "%s %d%%", tr(sl[k].name), (int)(sl[k].pct + 0.5));
                if (lx + pc_ui_text_w(t, 0.36f) + 12 > x + w) { lx = x; ly += 11; }
                pc_ui_rect(lx, ly + 2, lx + 7, ly + 9, 0xFF000000u | sl[k].rgb, 0.34f + k * 0.002f);
                pc_ui_text(lx + 9, ly, t, 0.36f, C_HELP);
                lx += pc_ui_text_w(t, 0.36f) + 18;
            }
        }
        return 1;
    }
    if (m->ref && (!strcmp(m->ref, "RandomDoors") || !strcmp(m->ref, "AreaCount") || !strcmp(m->ref, "AreaSize") || !strcmp(m->ref, "PrioritiseCutscenes"))) {
        pc_ui_text(x, y, tr("ESTIMATED PLAY TIME"), 0.46f, C_GOLD);
        biorand_doors_estimate(bio(), t, sizeof(t));
        line_at(x, y + 16, w, t[0] ? t : tr("Doors are not randomized."), 0.42f, C_HELP);
        return 1;
    }
    /* SEED (and the rest): the seed string and what START GAME does with it */
    pc_ui_text(x, y, tr("SEED STRING"), 0.46f, C_GOLD);
    bio_string(s, sizeof(s));
    snprintf(t, sizeof(t), "%.34s", s);   /* (68 characters: two lines) */
    line_at(x, y + 15, w, t, 0.42f, 0xFFFFFFFF);
    line_at(x, y + 27, w, strlen(s) > 34 ? s + 34 : "", 0.42f, 0xFFFFFFFF);
    line_at(x, y + 41, w, tr(ctx() == SC_INGAME ? "This game's BioRand options." : bio_ready() ? "Status: ready (installed)." : "Status: will generate at start."), 0.4f, C_HELP);
    return 1;
}

const char* setup_value(const MenuRow* m, char* buf, int n)
{
    char a[96], b[96];
    if (m->kind != ML_SHORTCUT || m->arg != MC_CHARACTERS) return NULL;
    switch (look_now()) {
    case 0: snprintf(buf, n, "%s", tr("Original")); break;
    case 2: snprintf(buf, n, "%s", tr("Random")); break;
    default:
        if (!g_appear_per) snprintf(buf, n, "%s", look_name(0, a, sizeof(a)));
        else snprintf(buf, n, tr("Claire: %s, Chris: %s"), look_name(0, a, sizeof(a)), look_name(1, b, sizeof(b)));
    }
    return buf;
}

/* ---------------------------------------------------------------- the NEW GAME screen (title mode 30) */
/* NEW GAME (biorand 0) or EXTRA > BIORAND (1, Game Type BioRand on its category) */
void pc_game_setup_open(int biorand)
{
    int i;
    gs.open = 1;
    gs.flow = gs.pasting = 0;
    gs.bio_loaded = 0;
    if (biorand) gs.type = 1;
    g_setup_ctx = SC_NEWGAME;
    ui_rows_open(MS_SETUP, "NEW GAME");
    ui_rows_picker(1);
    for (i = 0; i < menu_rows_n && !(menu_rows[i].kind == ML_ACTION && menu_rows[i].arg == SA_START); i++) {}
    if (biorand) ui_rows_goto(MC_BIORAND, NULL);
    else ui_rows_goto(MC_OVERVIEW, i < menu_rows_n ? &menu_rows[i] : NULL);   /* (the cursor on START GAME) */
    host_log("setup: GAME SETUP open (%s)\n", biorand ? "EXTRA > BIORAND" : "NEW GAME");
}

void pc_game_setup_close(void)
{
    if (!gs.open) return;
    if (gs.flow) pc_biorand_menu_close();
    ui_rows_close();
    ui_rows_picker(0);
    gs.open = gs.flow = gs.pasting = 0;
    host_log("setup: GAME SETUP closed\n");
}

/* once per frame: 0 stay, 1 back to the title menu, 2 START's new game (sys->gm_mode set; BioRand on or off for it) */
int pc_game_setup(void)
{
    int r = 0, w;
    if (gs.pasting && __atomic_load_n(&g_lobby_io.paste_req, __ATOMIC_ACQUIRE) == 2) {
        gs.pasting = 0;
        g_lobby_io.paste_req = 0;
        paste_end(g_lobby_io.paste);
    }
    StopAdvScreenSaver(0);   /* (a setup screen is no idle screen) */
    AdvEasySetTextureList(0);
    AdvDwawOnePictureEx(0, 0xFFFFFFFF);   /* the title picture, darkened, as BIORAND and the lobby */
    pc_ui_rect(0, 0, 640, 480, 0x90000000, 0.012f);
    pc_title_word_newgame(320.0f, 6.0f, 0xFFFFFFFF, 192);
    if (gs.flow) {   /* BioRand's dialogs over the darkened picture (the font's text would show through their boxes: no rows) */
        r = pc_biorand_flow();
        if (r == 1) { gs.flow = 0; gs.bio_loaded = 0; return 0; }   /* (back to the rows; the options as they are now) */
        if (r == 2) { gs.flow = 0; pc_game_setup_close(); host_log("setup: START GAME: BioRand generated, the game starts\n"); return 2; }
        return 0;
    }
    switch (ui_rows_run(AdvWork.PortId, 1)) {
    case UR_BACK: r = 1; break;
    case UR_START: r = 2; break;
    case UR_PICK: {
        const Setting* s = ui_rows_pick();
        if (s) pc_lobby_models(s, !strcmp(s->label, "Chris's Model"));
        break;
    }
    case UR_CLOSE: if ((w = ui_rows_window())) overlay_request(w); break;   /* (an F-key window over this screen) */
    }
    if (r) pc_game_setup_close();
    return r;
}
