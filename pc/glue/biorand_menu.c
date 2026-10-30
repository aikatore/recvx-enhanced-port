/* The BIORAND screen (docs/BIORAND_INGAME.md, S3 and S4b): the title menu's BIORAND row (--dev, lobby_glue.c
 * pc_title_menu_init) opens it as Adv_BioCvTitle mode 30, as MULTIPLAYER opens the lobby. Drawn as the
 * lobby and ENHANCED SETTINGS, with their helpers (pc_ui_*); its dialogs as RETURN TO TITLE's question
 * (options_glue.c pc_ui_dialog). States:
 *   BS_MENU    the generator's pages SEED, DOORS, ITEMS, ENEMIES, EXTRA OPTIONS: BioRand's options
 *              (biorand_opts.h; each change re-encodes the seed string into recvx.ini biorand_seed),
 *              the doors estimate, the item distribution bar, the seed string's copy / paste;
 *              on EXTRA OPTIONS, Choose Models opens the cutscene cast's "Custom" list there (cast_list.c, bm.list) and
 *              RANDOMIZER its sub-page (bm.sub): our enemy options (g_enemy_opts), as the lobby's RANDOMIZER page.
 *              ENEMIES' rows are read-only: BioRand's CLI changes no CVX enemy (its enemy list for the game is empty).
 *   BS_ASK     a Yes/No question over it (A_DOWNLOAD with no CLI: No leaves; A_CHECK; A_NEWER; A_GEN; A_DISK: GENERATE's
 *              warning when the mods folder's disk has less than GEN_WARN_MB free)
 *   BS_WORK    the downloader's thread runs (pc/src/net/biorand_dl.h): its stage, a bar, MB, CANCEL; or the
 *              CLI generates the pack (bm.gen, S5): its Progress: percent and message on the bar
 *   BS_RESULT  a message (newest, installed, generated, an error) and OK
 *   BS_PICK    START's difficulty (NORMAL, EASY, VERY EASY, CANCEL; "Difficulty select" off: NORMAL, not asked): a new
 *              game that plays the generated pack (biorand_game(1); pc_biorand_menu returns 2)
 * A co-op guest whose pack is not the host's opens it from the lobby or the title menu (bm.host, S5b:
 * coop_biorand_offer): it asks to generate the host's seed string (A_HOST), downloads the CLI first when there is none
 * (A_DOWNLOAD), offers the newest CLI when the host's digest differs (A_DIGEST), generates, and returns to where it
 * came from (coop_biorand_reply).
 * Game thread. */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/adv.h"
#include "../../decomp/include/ps2/veronica/prog/padman.h"
#include "../../decomp/include/ps2/veronica/prog/sdfunc.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_NaDraw.h"
#include "../src/host/menu_mouse.h"
#include "../src/host/lang.h"
#include "../src/host/lobby.h"
#include "../src/host/settings.h"
#include "../src/host/bindings.h"
#include "../src/host/enemies.h"
#include "../src/host/cast.h"
#include "../src/host/biorand_opts.h"
#include "../src/net/biorand_dl.h"
#include "../src/host/biorand.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void host_log(const char* fmt, ...);
extern ADV_WORK AdvWork;
void pc_ui_text(float x, float y, const char* s, float sc, unsigned int argb);
float pc_ui_text_w(const char* s, float sc);
int  pc_ui_text_wrap(float x, float y, float w, const char* s, float sc, unsigned int argb, int max_lines);
void pc_ui_rect(float x0, float y0, float x1, float y1, unsigned int argb, float z);
void pc_title_word_br(float cx, float y, unsigned int col, unsigned int shadow);
void pc_ui_dialog(float y0, float y1, float z, const char* l1, const char* l2, float sc2, unsigned int col2, int lines,
                  const char* b0, const char* b1, int sel, int glyphs);   /* options_glue.c */
int pc_ui_dialog_hit(float y1, const char* b0, const char* b1, int glyphs, float x, float y);
void pc_ui_dialog_place(const char* l1, const char* l2, float sc2, int lines, float extra, float* y0, float* y1);
int pc_in_game(void);   /* options_glue.c */
void pc_ui_dialog_btns(float y0, float y1, float z, const char* l1, const char* l2, float sc2, unsigned int col2, int lines,
                       const char* const* b, int n, int sel);
int pc_ui_dialog_btns_hit(float y1, const char* const* b, int n, float x, float y);
/* cast_list.c: the cutscene cast's "Custom" list (the lobby's too); 1 back */
void cast_list_open(float x0, float x1, float y0, float h, float sc, int rows);
int  cast_list_input(const MouseTake* m, unsigned int press, unsigned int rept, unsigned int ok, unsigned int cancel);
void cast_list_draw(void);
const char* cast_list_help(void);
const char* cast_list_summary(const unsigned char* off, int imports, char* buf, int n);
static const char* const diff_btn[] = { "NORMAL", "EASY", "VERY EASY", "CANCEL" };   /* sys->gm_mode 0, 1, 2 (adv.c NewGameDef) */

enum { BTN_L1 = 0x4, BTN_R1 = 0x8, BTN_START = 0x800, BTN_UP = 0x1000, BTN_RIGHT = 0x2000, BTN_DOWN = 0x4000, BTN_LEFT = 0x8000 };
enum { BS_MENU, BS_ASK, BS_WORK, BS_RESULT, BS_PICK };
enum { A_DOWNLOAD, A_CHECK, A_NEWER, A_GEN, A_HOST, A_DIGEST, A_DISK };
#define GEN_WARN_MB 5120     /* GENERATE warns below this much free space (BRDL_GEN_FREE_MB, 4 GB, is what a run takes) */
/* the pages; DOORS..ENEMIES show biorand_opt[]'s groups BOG_DOORS..BOG_ENEMIES (the same numbers) */
enum { PG_SEED, PG_DOORS, PG_ITEMS, PG_ENEMIES, PG_PORT, PG_N };
static const char* const page_names[PG_N] = { "SEED", "DOORS", "ITEMS", "ENEMIES", "EXTRA OPTIONS" };
/* row kinds: K_OPT a biorand_opt[] row, K_SET a settings table row (the cutscene cast, the enemy options) */
enum { K_PAGE, K_OPT, K_RANDOM, K_COPY, K_PASTE, K_GEN, K_START, K_CHECK, K_APPEAR, K_SET, K_CUSTOM, K_RANDPG, K_RSEED, K_BACK };   /* K_CUSTOM: the cast's
    "Custom" list; K_RANDPG: the RANDOMIZER sub-page; K_RSEED: its RANDOM SEED */
static const char* const enemy_why = "BioRand cannot randomize enemies in this game. Use RANDOMIZER on the EXTRA OPTIONS page instead.";
typedef struct { int kind, idx; } Row;
#define MAX_ROWS 30          /* (ROW_Z has room for 30; ITEMS has 27) */

/* depths (lobby_glue.c): the picture 0.01, its dimming 0.012. Each kind of box has its own, so "Smooth menus"
   never pairs one kind with another and slides it there (view_glue.c pc_menu_tag hashes the depth); the
   distribution's slices and swatches one each by what they show, so one that drops out leaves the others
   theirs. All below 0.5: the title's 2D depth is about 131085 * z, clamped at 65534 (from 0.5 on, all one). */
#define Z_DIM 0.012f
#define Z_RULE 0.02f
#define Z_STRIP 0.025f
#define Z_ROW 0.3f
#define Z_BOX 0.303f      /* a dialog (options_glue.c pc_ui_dialog): its frame + 0.01 */
#define Z_BAR 0.32f
#define Z_FILL 0.33f
#define Z_CARET 0.35f
#define Z_DFRAME 0.355f
#define Z_SLICE 0.36f       /* + ID_DZ per slice id (up to 6) */
#define Z_SWFRAME 0.372f    /* + ID_DZ per slice id */
#define Z_SWATCH 0.384f     /* + ID_DZ per slice id */
#define ID_DZ 0.002f
/* a row's arrows and slider: one depth per row and scroll parity (ROW_Z), so they pair with their own row's
   while it stays (not with a wider value's arrow or a track that is not there) and, as the letters, show at
   once where a scroll put them */
#define Z_ARROW 0.395f
#define Z_TRACK 0.425f
#define Z_TRACKFILL 0.455f
#define ROW_Z(base, i) ((base) + ((bm.top & 1) * MAX_ROWS + (i)) * 0.0005f)

#define ROW_X 48.0f
#define VAL_X 372.0f        /* the values' "<" */
#define PAGE_Y 70.0f
#define ROW_Y0 102.0f
#define ROW_H 21.0f
#define VISIBLE 10          /* rows below the page row; more scroll */
#define INFO_Y 314.0f       /* below the rows: the seed string, the estimate or a distribution */
#define ROW_SC 0.55f
#define SMALL_SC 0.45f
#define LEGEND_SC 0.42f
#define SL_X0 436.0f        /* a slider's track */
#define SL_X1 528.0f
#define BAR_X0 48.0f        /* a distribution bar */
#define BAR_X1 592.0f
#define BOX_X0 112.0f       /* a dialog: RETURN TO TITLE's box (options_glue.c pc_ui_dialog) */
#define BOX_X1 528.0f
#define WORK_TEXT 34.0f     /* the progress: room for two lines of its text */
#define WORK_BAR 30.0f      /* and the bar with its numbers */

static struct {
    int open, state, ask, row, sel;     /* row: rows[]; sel: BS_ASK 0 Yes 1 No */
    int install, cancelling, have;      /* the job installs (else checks); CANCEL pressed; the CLI is there */
    int gen, gen_seed;                  /* the job is the CLI generating (seed number gen_seed) */
    int free_mb;                        /* free space in the mods folder (-1 unknown), at the last look */
    char msg[320];
    int err;
    MouseTake mt;
    int page, top, seed_i;              /* seed_i: the Seed row of biorand_opt[] */
    int list;                           /* the cutscene cast's "Custom" list is up (cast_list.c) */
    int sub;                            /* EXTRA OPTIONS' RANDOMIZER sub-page is up */
    int edit, caret, pasting, blink;    /* the seed number is typed; the clipboard was asked for (lobby_io.c) */
    const Setting* eds;                 /* the seed typed: this setting's (the enemy seed), NULL BioRand's */
    char buf[12];
    char say[192];
    int say_t, say_err;
    BioOpts o;                          /* the options, from and to recvx.ini biorand_seed */
    char str[BIORAND_SEED_CAP];         /* their seed string */
    int host, warned, done;             /* the host's seed (co-op guest; bm.o and bm.str are its, not saved); the CLI
                                           digest warning was asked; it was generated */
    char dig[16];                       /* the host's CLI digest, first hex digits ("" unknown) */
} bm;

static Row rows[MAX_ROWS];
static int nrows;

static const char* const ask_text[] = {
    "BioRand is not installed. Download it from GitHub? About 7 MB; the download will be verified.",
    "Check GitHub for a BioRand update?",
    "A BioRand update is available. Download it? About 7 MB; the download will be verified.",
    NULL,   /* A_GEN: gen_text() */
    NULL,   /* A_HOST: gen_text() */
    "Your BioRand version differs from the host's. Download the latest version first? The host should update too. No uses your installed version.",
    NULL,   /* A_DISK: disk_text() */
};

static void ask(int a) { bm.state = BS_ASK; bm.ask = a; bm.sel = 1; }   /* No first */

static void result(const char* m, int err)
{
    snprintf(bm.msg, sizeof(bm.msg), "%s", m);
    bm.err = err;
    bm.state = BS_RESULT;
}

static void look_mods(void) { bm.free_mb = biorand_mods_free_mb(); }

/* back to the page, or out when there is no CLI to show it for (or it was the host's seed: back to the lobby) */
static int to_menu(void)
{
    look_mods();
    bm.have = biorand_cli_present();
    bm.state = BS_MENU;
    return !bm.have || bm.host;
}

static void say(const char* m, int err);

/* GENERATE's question: what it does, the memory it takes */
static const char* gen_text(void)
{
    static char t[400];
    char h[64];
    if (bm.host) snprintf(h, sizeof(h), tr("Generate the host's BioRand seed %d"), bm.o.Seed);   /* (A_HOST) */
    else snprintf(h, sizeof(h), tr("Generate BioRand seed %d"), bm.o.Seed);
    snprintf(t, sizeof(t), tr("%s? This replaces your generated game and may briefly use about 6 GB of memory."), h);
    return t;
}

/* the free space (looked at on GENERATE's OK, look_mods): not enough is an error, close to it a warning; unknown neither */
static const char* disk_why(void)
{
    static char t[160];
    if (bm.free_mb < 0 || bm.free_mb >= BRDL_GEN_FREE_MB) return NULL;
    snprintf(t, sizeof(t), tr("Not enough disk space to generate. BioRand needs about %d GB free in the mods folder while it runs; "
             "%.1f GB are free."), BRDL_GEN_FREE_MB / 1024, bm.free_mb / 1024.0);
    return t;
}
static int disk_low(void) { return bm.free_mb >= 0 && bm.free_mb < GEN_WARN_MB; }
static const char* disk_text(void)
{
    static char t[200];
    snprintf(t, sizeof(t), tr("Disk space is low: %.1f GB free in the mods folder. BioRand needs about %d GB while it runs. "
             "Generate anyway?"), bm.free_mb / 1024.0, BRDL_GEN_FREE_MB / 1024);
    return t;
}

/* GENERATE's Yes: the CLI starts (biorand.c biorand_generate) */
static void gen_start(void)
{
    int r = biorand_generate(bm.str);
    bm.gen = 1;
    bm.gen_seed = bm.o.Seed;
    bm.cancelling = 0;
    if (r > 0) {
        bm.state = BS_WORK;
        if (bm.host) coop_biorand_reply(1);   /* (the host's lobby shows it) */
        host_log("biorand menu: generating %s%s\n", bm.str, bm.host ? " (the co-op host's)" : "");
    } else result(r < 0 ? "Cannot save BioRand settings. Check that your save folder is writable." : "Download is finishing. Try again shortly.", 1);
}

/* the generation's end: the pack looked at again; done, BioRand on. The host's seed: back to the lobby (1), which
   compares the pack again; our own setting stays (the session follows the host's). */
static int gen_end(const BrdlStatus* s)
{
    char m[320];
    biorand_scan();   /* (also after an error or cancel: the old pack may be gone) */
    if (bm.host && s->state != BRDL_ERROR) {
        bm.done = s->state == BRDL_GENERATED;
        host_log("biorand menu: the host's seed %s; the pack says seed %d\n", bm.done ? "generated" : "cancelled", biorand_seed());
        return 1;
    }
    if (s->state == BRDL_GENERATED) {
        host_log("biorand menu: generated %s; the pack says seed %d\n", bm.str, biorand_seed());
        snprintf(m, sizeof(m), tr("BioRand seed %d is ready. Choose START to play it.%s%s"), bm.gen_seed,
                 s->warn[0] ? " Warning: " : "", s->warn);
        result(m, 0);
    } else if (s->state == BRDL_ERROR) result(s->error, 1);
    else { to_menu(); say("Generation cancelled.", 0); }
    return 0;
}

static const char* row_why(const Row* r);
/* the host's seed (bm.host): the CLI first, the digest's question once, then GENERATE's checks and the CLI */
static int host_next(void)
{
    const Row g = { K_GEN, 0 };
    const char* ours = strncmp(g_biorand_cli_digest, "sha256:", 7) ? "" : g_biorand_cli_digest + 7;
    if (!bm.have) { ask(A_DOWNLOAD); return 0; }
    if (!bm.warned && bm.dig[0] && strncmp(ours, bm.dig, strlen(bm.dig))) {
        bm.warned = 1;
        host_log("biorand menu: our CLI %.12s is not the host's %s\n", ours[0] ? ours : "(unknown)", bm.dig);
        ask(A_DIGEST);
        return 0;
    }
    look_mods();
    if (row_why(&g)) result(row_why(&g), 1);
    else if (disk_why()) result(disk_why(), 1);
    else if (disk_low()) ask(A_DISK);
    else gen_start();
    return 0;
}

static void work(int install)
{
    BrdlStatus s;
    bm.install = install;
    bm.gen = 0;
    bm.cancelling = 0;
    if ((install ? biorand_cli_install() : biorand_cli_check_update())) { bm.state = BS_WORK; return; }
    biorand_cli_status(&s);
    result(s.state == BRDL_ERROR ? s.error : "Download is finishing. Try again shortly.", 1);
}

/* BS_WORK: the thread's end */
static int work_end(void)
{
    BrdlStatus s;
    biorand_cli_status(&s);
    if (s.state < BRDL_INSTALLED) return 0;
    host_log("biorand menu: %s ended in state %d\n", bm.gen ? "generate" : bm.install ? "install" : "check", s.state);
    if (bm.gen) return gen_end(&s);
    switch (s.state) {
    case BRDL_INSTALLED:
        bm.have = 1;
        if (bm.host) return host_next();
        result("BioRand installed.", 0);
        break;
    case BRDL_CURRENT: result("BioRand is up to date.", 0); break;
    case BRDL_NEWER: ask(A_NEWER); break;
    case BRDL_ERROR: result(s.error, 1); break;
    default: return to_menu();   /* cancelled */
    }
    return 0;
}

/* a dialog's choice: 1 leave the screen */
static int choose(int yes)
{
    CallSystemSe(0, yes ? 3 : 0);
    if (bm.state == BS_ASK) {
        if (yes && bm.ask == A_GEN && disk_low()) { ask(A_DISK); return 0; }
        if (yes && (bm.ask == A_GEN || bm.ask == A_DISK)) { gen_start(); return 0; }
        if (bm.ask == A_HOST) return yes ? host_next() : to_menu();
        if (!yes && bm.ask == A_DIGEST) return host_next();   /* with our CLI */
        if (yes) { work(bm.ask != A_CHECK); return 0; }
        return bm.ask == A_DOWNLOAD ? 1 : to_menu();
    }
    if (bm.state == BS_RESULT) return to_menu();
    if (!bm.cancelling) biorand_cli_cancel();   /* BS_WORK */
    bm.cancelling = 1;
    return 0;
}

/* ---------------------------------------------------------------- the pages' rows */
static void add(int kind, int idx) { if (nrows < MAX_ROWS) rows[nrows++] = (Row){ kind, idx }; }
/* a settings table row (F1 and ENHANCED SETTINGS edit the same), if the menus list it now */
static void add_set(const int* var)
{
    const Setting* s = settings_of(var);
    if (s && (!s->shown || s->shown())) add(K_SET, (int)(s - g_settings));
}

static void build_rows(void)
{
    nrows = 0;
    add(K_PAGE, 0);
    if (bm.page == PG_SEED) {
        add(K_OPT, bm.seed_i);
        add(K_RANDOM, 0);
        add(K_COPY, 0);
        add(K_PASTE, 0);
        add(K_GEN, 0);
        add(K_START, 0);
        add(K_CHECK, 0);
    } else if (bm.page == PG_PORT && bm.sub) {   /* the lobby's RANDOMIZER page (lobby_glue.c step 4) without the cast */
        add_set(&g_enemy_opts.rand);
        if (g_enemy_opts.rand) { add_set(&g_enemy_opts.variety); add_set(&g_enemy_opts.events); }   /* only the randomizer has them */
        add_set(&g_enemy_opts.seed);
        add(K_RSEED, 0);
        add_set(&g_enemy_opts.extra);
        add_set(&g_enemy_opts.place);
    } else if (bm.page == PG_PORT) {
        add(K_RANDPG, 0);   /* (row 1: sub_open) */
        add(K_APPEAR, 0);
        add_set(&g_enemy_opts.cast);
        add_set(&g_enemy_opts.cast_all);
        if (g_enemy_opts.cast && g_enemy_opts.cast_all == 2) add(K_CUSTOM, 0);   /* (after Model Selection) */
        add_set(&g_enemy_opts.cast_assign);
        add_set(&g_enemy_opts.cast_imports);
    } else
        for (int i = 0; i < biorand_opt_n; i++)
            if (biorand_opt[i].group == bm.page) add(K_OPT, i);
    add(K_BACK, 0);
    if (bm.row >= nrows) bm.row = nrows - 1;
}

static int is_slider(int i) { return biorand_opt[i].type == BO_RATIO || (biorand_opt[i].type == BO_RANGE && biorand_opt[i].low); }
static int has_value(const Row* r) { return r->kind == K_PAGE || r->kind == K_OPT || r->kind == K_APPEAR || r->kind == K_SET; }

static int depth(int i)
{
    int d = 0;
    for (int p = biorand_opt_find(biorand_opt[i].parent); p >= 0; p = biorand_opt_find(biorand_opt[p].parent)) d++;
    return d;
}

/* why the row cannot change now (greyed, read-only), or NULL */
static const char* row_why(const Row* r)
{
    static char why[96];
    switch (r->kind) {
    case K_GEN:
        if (!bm.have) return "Install BioRand first.";
        if (!biorand_iso_known()) return "Start the game with a disc image first.";
        if (pc_in_game()) return "Only at the title screen.";
        if (coop_biorand() >= 0 && !bm.host) return "Only the co-op host can generate here.";   /* (the host's own seed: S5b) */
        return NULL;
    case K_START:
        if (!biorand_pack()) return "Generate a BioRand game first.";
        if (coop_biorand() >= 0) return "Not while joined to a co-op host.";
        return NULL;
    case K_SET: return settings_locked(&g_settings[r->idx]);
    case K_OPT: {
        const char* l = biorand_opt[r->idx].group == BOG_ENEMIES ? enemy_why : biorand_opt_locked(&bm.o, r->idx);
        if (l) return l;
        if (biorand_opt_active(&bm.o, r->idx)) return NULL;
        for (int p = biorand_opt_find(biorand_opt[r->idx].parent); p >= 0; p = biorand_opt_find(biorand_opt[p].parent))
            if (!biorand_opt_get(&bm.o, p)) { snprintf(why, sizeof(why), tr("Turn on \"%s\" first."), biorand_opt[p].label); break; }
        return why;
    }
    }
    return NULL;
}

static void say(const char* m, int err)
{
    snprintf(bm.say, sizeof(bm.say), "%s", m);
    bm.say_t = 240;
    bm.say_err = err;
}

static void refuse(const char* why) { CallSystemSe(0, 1); say(why, 1); }

/* the options changed: recvx.ini (biorand_opts_save) and the string shown */
static void saved(const char* what, int v)
{
    biorand_opts_save(&bm.o);
    snprintf(bm.str, sizeof(bm.str), "%s", g_biorand_seed);
    host_log("biorand menu: %s = %d, seed string %s\n", what, v, bm.str);
}

static void set_opt(int i, int v)
{
    biorand_opt_set(&bm.o, i, v);
    saved(biorand_opt[i].key, biorand_opt_get(&bm.o, i));
}

static void set_page(int p)
{
    bm.sub = 0;
    bm.page = (p + PG_N) % PG_N;
    bm.row = 0;
    bm.top = 1;
    bm.say_t = 0;   /* (a message is about the row it came from) */
    CallSystemSe(0, 2);
}

static void move_sel(int dir)
{
    bm.row = ((bm.row + dir) % nrows + nrows) % nrows;
    bm.say_t = 0;
    CallSystemSe(0, 2);
}

/* EXTRA OPTIONS' RANDOMIZER sub-page opens (1) or closes (0); either way on row 1 (RANDOMIZER / its first) */
static void sub_open(int on)
{
    bm.sub = on;
    bm.row = 1;
    bm.top = 1;
    bm.say_t = 0;
    CallSystemSe(0, on ? 3 : 0);
    host_log("biorand menu: randomizer page %s\n", on ? "open" : "closed");
}

/* left/right, or a click on an arrow */
static void change(const Row* r, int dir)
{
    const char* why = row_why(r);
    if (r->kind == K_PAGE) { set_page(bm.page + dir); return; }
    if (!has_value(r)) return;
    if (why) { refuse(why); return; }
    if (r->kind == K_APPEAR) {
        g_biorand_appear ^= 1;
        settings_save_later();
        host_log("biorand menu: randomize played as = %d\n", g_biorand_appear);
    } else if (r->kind == K_SET) settings_step(&g_settings[r->idx], dir);
    else {
        const BioOpt* d = &biorand_opt[r->idx];
        int v = biorand_opt_get(&bm.o, r->idx), n = v + dir;
        if (d->type == BO_BOOL) n = !v;
        else if (d->type == BO_ENUM) n = n > d->hi ? d->lo : n < d->lo ? d->hi : n;
        else if (n < d->lo || n > d->hi) return;   /* at an end */
        set_opt(r->idx, n);
    }
    CallSystemSe(0, 2);
}

/* the seed number typed: digits from the keyboard, or the gamepad's up/down at the caret (as the lobby's fields) */
static int edit_hi(void) { return bm.eds ? bm.eds->hi : BIORAND_SEED_MAX; }

static void edit_begin(const Setting* s)
{
    bm.eds = s;
    snprintf(bm.buf, sizeof(bm.buf), "%d", s ? settings_get(s) : bm.o.Seed);
    bm.caret = (int)strlen(bm.buf);
    bm.edit = 1;
    g_lobby_io.tail = g_lobby_io.head;   /* nothing typed before counts */
    g_lobby_io.text_on = 1;
    CallSystemSe(0, 3);
}

static void edit_end(int accept)
{
    if (accept && bm.buf[0]) {   /* (as many digits as the highest; set clamps) */
        if (bm.eds) settings_set(bm.eds, (int)strtol(bm.buf, NULL, 10));
        else set_opt(bm.seed_i, (int)strtol(bm.buf, NULL, 10));
    }
    bm.edit = 0;
    g_lobby_io.text_on = 0;
    CallSystemSe(0, accept ? 3 : 0);
}

static void edit_input(unsigned int press, unsigned int rept, unsigned int ok, unsigned int cancel)
{
    char hi[12];
    int n, max = snprintf(hi, sizeof(hi), "%d", edit_hi());
    while (g_lobby_io.tail != __atomic_load_n(&g_lobby_io.head, __ATOMIC_ACQUIRE)) {
        int k = g_lobby_io.keys[g_lobby_io.tail & 63];
        n = (int)strlen(bm.buf);
        g_lobby_io.tail++;
        if (k == LK_ENTER) { edit_end(1); return; }
        if (k == LK_ESC) { edit_end(0); return; }
        if (k == LK_BACK && bm.caret > 0) { memmove(bm.buf + bm.caret - 1, bm.buf + bm.caret, n - bm.caret + 1); bm.caret--; }
        else if (k == LK_DEL && bm.caret < n) memmove(bm.buf + bm.caret, bm.buf + bm.caret + 1, n - bm.caret);
        else if (k == LK_LEFT && bm.caret > 0) bm.caret--;
        else if (k == LK_RIGHT && bm.caret < n) bm.caret++;
        else if (k == LK_HOME) bm.caret = 0;
        else if (k == LK_END) bm.caret = n;
        else if (k >= '0' && k <= '9' && n < max) {
            memmove(bm.buf + bm.caret + 1, bm.buf + bm.caret, n - bm.caret + 1);
            bm.buf[bm.caret++] = (char)k;
        }
    }
    if (press & ok) { edit_end(1); return; }
    if (press & cancel) { edit_end(0); return; }
    n = (int)strlen(bm.buf);
    if (rept & (BTN_UP | BTN_DOWN)) {
        if (bm.caret == n) { if (n >= max) return; bm.buf[n] = '0'; bm.buf[n + 1] = 0; }   /* a new digit, 0 */
        else bm.buf[bm.caret] = (char)('0' + (bm.buf[bm.caret] - '0' + ((rept & BTN_UP) ? 1 : 9)) % 10);
        CallSystemSe(0, 2);
    }
    if ((rept & BTN_LEFT) && bm.caret > 0) bm.caret--;
    if ((rept & BTN_RIGHT) && bm.caret < n) bm.caret++;
}

/* a random seed, 0..hi */
static int random_seed(int hi)
{
    static unsigned x;
    x = x * 1664525u + 1013904223u + (unsigned)time(NULL) * 2654435761u + (unsigned)clock();
    CallSystemSe(0, 3);
    return (int)((x >> 4) % (hi + 1u));
}

static void copy_string(void)
{
    char m[192];
    snprintf(g_lobby_io.clip, sizeof(g_lobby_io.clip), "%s", bm.str);
    g_lobby_io.clip_req = 1;   /* lobby_io.c sets the clipboard */
    snprintf(m, sizeof(m), tr("Seed string copied: %s"), bm.str);
    say(m, 0);
    host_log("biorand menu: copied %s\n", bm.str);
    CallSystemSe(0, 3);
}

/* the clipboard's text arrived (lobby_io.c): a seed string's options, or the decoder's reason */
static void paste_end(const char* text)
{
    BioOpts t;
    char m[96];
    const char* why = biorand_seed_decode(text, &t);
    if (why) { refuse(why); host_log("biorand menu: paste refused: %s\n", why); return; }
    biorand_opts_rules(&t);
    bm.o = t;
    saved("paste", t.Seed);
    snprintf(m, sizeof(m), tr("Seed %d and options pasted."), t.Seed);
    say(m, 0);
    CallSystemSe(0, 3);
}

/* START: a new game of difficulty d (sys->gm_mode) that plays the generated pack; 2 (pc_lobby: TitleCall as NEW GAME) */
static int start(int d)
{
    sys->gm_mode = d;
    biorand_game(1);
    host_log("biorand menu: start, difficulty %s, the pack's %s\n", diff_btn[d], biorand_seed_text()[0] ? biorand_seed_text() : "seed unknown");
    return 2;
}

/* confirm on a row: 1 leave the screen, 2 start the game */
static int activate(const Row* r)
{
    switch (r->kind) {
    case K_PAGE: change(r, 1); return 0;
    case K_OPT:
        if (r->idx == bm.seed_i) edit_begin(NULL);
        else if (biorand_opt[r->idx].type == BO_BOOL || biorand_opt[r->idx].type == BO_ENUM) change(r, 1);
        else if (row_why(r)) refuse(row_why(r));
        else CallSystemSe(0, 1);   /* a slider: left/right */
        return 0;
    case K_RANDOM: set_opt(bm.seed_i, random_seed(BIORAND_SEED_MAX)); return 0;
    case K_RSEED: {   /* the enemy seed (the lobby's RANDOM SEED) */
        const Setting* s = settings_of(&g_enemy_opts.seed);
        char m[48];
        settings_set(s, random_seed(s->hi));
        snprintf(m, sizeof(m), tr("New seed: %d"), g_enemy_opts.seed);
        say(m, 0);
        return 0;
    }
    case K_RANDPG: sub_open(1); return 0;
    case K_COPY: copy_string(); return 0;
    case K_PASTE: g_lobby_io.paste_req = 1; bm.pasting = 1; return 0;
    case K_GEN:
        look_mods();   /* (the free space now) */
        if (row_why(r)) refuse(row_why(r));
        else if (disk_why()) { CallSystemSe(0, 1); result(disk_why(), 1); }
        else { CallSystemSe(0, 3); ask(A_GEN); }
        return 0;
    case K_START:
        if (row_why(r)) { refuse(row_why(r)); return 0; }
        CallSystemSe(0, 3);
        if (!pc_difficulty_select()) return start(0);   /* (the setting off: NORMAL, as NEW GAME) */
        bm.state = BS_PICK;
        bm.sel = 0;
        return 0;
    case K_CHECK: CallSystemSe(0, 3); ask(A_CHECK); return 0;
    case K_CUSTOM: bm.list = 1; cast_list_open(ROW_X, 600, ROW_Y0, ROW_H, ROW_SC, 10); host_log("biorand menu: the cutscene cast's custom models\n"); return 0;
    case K_BACK:
        if (bm.sub) { sub_open(0); return 0; }
        CallSystemSe(0, 0);
        return 1;
    case K_SET:
        if (g_settings[r->idx].val == &g_enemy_opts.seed && !row_why(r)) { edit_begin(&g_settings[r->idx]); return 0; }
        change(r, 1);
        return 0;
    default: change(r, 1); return 0;   /* K_APPEAR */
    }
}

/* ---------------------------------------------------------------- layout */
static float row_y(int i) { return ROW_Y0 + (i - bm.top) * ROW_H; }

static void scroll_to_sel(void)
{
    if (bm.row > 0 && bm.row < bm.top) bm.top = bm.row;
    if (bm.row >= bm.top + VISIBLE) bm.top = bm.row - VISIBLE + 1;
    if (bm.top > nrows - VISIBLE) bm.top = nrows - VISIBLE;
    if (bm.top < 1) bm.top = 1;
}

static int row_at(float x, float y)
{
    if (x < 32 || x >= 608) return -1;
    if (y >= PAGE_Y - 4 && y < PAGE_Y + 20) return 0;
    for (int i = bm.top; i < nrows && i < bm.top + VISIBLE; i++)
        if (y >= row_y(i) - 3 && y < row_y(i) + ROW_H - 3) return i;
    return -1;
}

/* a click on row i at x: its value's arrows (a slider's track: that value), else confirm */
static int click(int i, float x)
{
    const Row* r = &rows[i];
    if (r->kind == K_PAGE) { change(r, x < 320 ? -1 : 1); return 0; }
    if (!has_value(r) || x < VAL_X - 10) return activate(r);
    if (r->kind == K_OPT && is_slider(r->idx)) {
        const BioOpt* d = &biorand_opt[r->idx];
        if (x >= SL_X0 - 2 && x < SL_X1 + 2) {
            int v = d->lo + (int)((x - SL_X0) / (SL_X1 - SL_X0) * (d->hi - d->lo) + 0.5f);
            v = v < d->lo ? d->lo : v > d->hi ? d->hi : v;
            if (row_why(r)) refuse(row_why(r));
            else if (v != biorand_opt_get(&bm.o, r->idx)) { set_opt(r->idx, v); CallSystemSe(0, 2); }
            return 0;
        }
        change(r, x < SL_X0 ? -1 : 1);
        return 0;
    }
    change(r, x < VAL_X + 14 ? -1 : 1);   /* the "<" arrow: back */
    return 0;
}

/* the dialog's texts and box (the box as tall as its text; the progress one fixed, its text changes) */
static const char* ask_msg(void) { return bm.ask == A_GEN || bm.ask == A_HOST ? gen_text() : bm.ask == A_DISK ? disk_text() : ask_text[bm.ask]; }
static const char* result_head(void) { return bm.err ? "BIORAND - ERROR" : "BIORAND"; }
/* START's question: the pack's seed; whether the options shown are not what it was generated with */
static const char* pick_text(void)
{
    static char t[240];
    const char* ps = biorand_seed_string();
    snprintf(t, sizeof(t), tr("Choose the difficulty.%s"), ps[0] && strcmp(ps, bm.str) ? " Your option changes are not generated yet: "
             "this plays the generated game. GENERATE first to play them." : "");
    return t;
}
static const char* pick_head(void)
{
    static char t[48];
    if (biorand_seed() >= 0) snprintf(t, sizeof(t), tr("START BIORAND SEED %d"), biorand_seed());
    else snprintf(t, sizeof(t), tr("START BIORAND"));
    return t;
}
static void dlg_box(float* y0, float* y1)
{
    if (bm.state == BS_ASK) pc_ui_dialog_place(NULL, ask_msg(), 0.55f, 5, 0, y0, y1);
    else if (bm.state == BS_PICK) pc_ui_dialog_place(pick_head(), pick_text(), 0.55f, 4, 0, y0, y1);
    else if (bm.state == BS_RESULT) pc_ui_dialog_place(result_head(), bm.msg, 0.55f, 4, 0, y0, y1);
    else pc_ui_dialog_place("", NULL, 0.55f, 0, WORK_TEXT + WORK_BAR, y0, y1);
}

/* 1 leave, 2 start the game */
static int input(void)
{
    ADV_WORK* ap = &AdvWork;
    unsigned int press = Pad[ap->PortId].press, rept = Pad[ap->PortId].Rept;
    unsigned int ok = AdvGetOkButton() | BTN_START, cancel = AdvGetCancelButton();
    MouseTake* m = &bm.mt;
    int hit;
    float y0, y1;
    menu_mouse_take(m);
    if (press || rept || m->left || m->right || m->moved || m->wheel || bm.state == BS_WORK || g_lobby_io.head != g_lobby_io.tail)
        StopAdvScreenSaver(0);   /* (a download is no idle screen) */
    if (bm.state == BS_MENU) {
        if (bm.list) { if (cast_list_input(m, press, rept, ok, cancel)) bm.list = 0; return 0; }
        if (bm.edit) {
            if (m->left && row_at(m->x, m->y) != bm.row) edit_end(1);   /* a click elsewhere takes the number */
            else if (m->right) edit_end(0);
            else edit_input(press, rept, ok, cancel);
            return 0;
        }
        g_lobby_io.tail = g_lobby_io.head;
        hit = row_at(m->x, m->y);
        if (hit >= 0 && (m->moved || m->left) && hit != bm.row) { bm.row = hit; bm.say_t = 0; CallSystemSe(0, 2); }
        if (m->wheel) { move_sel(-m->wheel); return 0; }   /* as the d-pad, like ENHANCED SETTINGS: a notch a row */
        if (m->left && hit >= 0) return click(hit, m->x);
        if (m->right || (press & cancel)) {   /* (back, whichever row; from the sub-page to EXTRA OPTIONS) */
            if (bm.sub) { sub_open(0); return 0; }
            CallSystemSe(0, 0);
            return 1;
        }
        if (press & BTN_L1) set_page(bm.page - 1);
        else if (press & BTN_R1) set_page(bm.page + 1);
        else if (rept & BTN_UP) move_sel(-1);
        else if (rept & BTN_DOWN) move_sel(1);
        else if (rept & BTN_LEFT) change(&rows[bm.row], -1);
        else if (rept & BTN_RIGHT) change(&rows[bm.row], 1);
        else if (press & ok) return activate(&rows[bm.row]);
        return 0;
    }
    dlg_box(&y0, &y1);
    if (bm.state == BS_PICK) {   /* START's difficulty */
        hit = pc_ui_dialog_btns_hit(y1, diff_btn, 4, m->x, m->y);
        if (hit >= 0 && (m->moved || m->left) && hit != bm.sel) { bm.sel = hit; CallSystemSe(0, 2); }
        if (rept & (BTN_LEFT | BTN_UP)) { bm.sel = (bm.sel + 3) % 4; CallSystemSe(0, 2); }
        else if (rept & (BTN_RIGHT | BTN_DOWN)) { bm.sel = (bm.sel + 1) % 4; CallSystemSe(0, 2); }
        else if ((m->left && hit >= 0) || (press & ok)) {
            if (bm.sel < 3) { CallSystemSe(0, 3); return start(bm.sel); }
            CallSystemSe(0, 0);
            bm.state = BS_MENU;
        } else if (m->right || (press & cancel)) { CallSystemSe(0, 0); bm.state = BS_MENU; }
        return 0;
    }
    hit = bm.state == BS_ASK ? pc_ui_dialog_hit(y1, "YES", "NO", 0, m->x, m->y)   /* the dialog's buttons */
                             : pc_ui_dialog_hit(y1, bm.state == BS_RESULT ? "OK" : "CANCEL", NULL, 0, m->x, m->y);
    if (bm.state == BS_ASK) {
        if (hit >= 0 && (m->moved || m->left) && hit != bm.sel) { bm.sel = hit; CallSystemSe(0, 2); }
        if (m->left && hit >= 0) return choose(!bm.sel);
        if (m->right || (press & cancel)) return choose(0);
        if (rept & (BTN_LEFT | BTN_RIGHT | BTN_UP | BTN_DOWN)) { bm.sel ^= 1; CallSystemSe(0, 2); }
        else if (press & ok) return choose(!bm.sel);
        return 0;
    }
    if ((m->left && hit >= 0) || m->right || (press & (ok | cancel))) return choose(bm.state == BS_RESULT);   /* CANCEL, OK */
    return 0;
}

/* ---------------------------------------------------------------- drawing */
/* text no wider than w (long ones get narrower) */
static void text_fit(float x, float y, const char* s, float sc, float w, unsigned int col)
{
    float tw = pc_ui_text_w(s, sc), k = tw > w ? w / tw : 1.0f;
    pc_ui_text(x, y + (1.0f - k) * 9.0f, s, sc * k, col);
}

/* what switches the page (L1 / R1) on the device in use, as ENHANCED SETTINGS says it */
static const char* page_keys(char* buf, int n)
{
    char a[32], b[32];
    int dev = bind_gamepad() ? BIND_PAD : BIND_KB;
    bind_code_name(dev, bind_get(B_L1, dev, 0), a, sizeof(a));
    bind_code_name(dev, bind_get(B_R1, dev, 0), b, sizeof(b));
    snprintf(buf, n, "%s/%s", a, b);
    return buf;
}

/* a small arrow head pointing left (dir -1) or right (1), its tip at x, of row i (lobby_glue.c arrow) */
static void arrow(int i, float x, float y, int dir, unsigned int argb)
{
    float z = ROW_Z(Z_ARROW, i);
    NJS_POLYGON_VTX v[3] = { { x, y, z, argb }, { x - dir * 7.0f, y - 6.0f, z, argb }, { x - dir * 7.0f, y + 6.0f, z, argb } };
    njDrawPolygon(v, 3, 1);
}

/* row i's value between arrows */
static void value(int i, float y, const char* v, unsigned int col, int arrows)
{
    float w = pc_ui_text_w(v, ROW_SC);
    if (w > 196) w = 196;
    if (arrows) { arrow(i, VAL_X, y + 9, -1, col); arrow(i, VAL_X + 14 + w + 12, y + 9, 1, col); }
    text_fit(VAL_X + 12, y, v, ROW_SC, 196, col);
}

/* a slider: its ends' names around a small bar (BioRand's GUI shows no number either) */
static void slider(int i, float y, const BioOpt* d, int v, unsigned int col, int sel, int live)
{
    float f = d->hi > d->lo ? (float)(v - d->lo) / (d->hi - d->lo) : 0, xf = SL_X0 + (SL_X1 - SL_X0) * f;
    if (live) { arrow(i, VAL_X, y + 9, -1, col); arrow(i, 596, y + 9, 1, col); }
    pc_ui_text(SL_X0 - 6 - pc_ui_text_w(d->low, SMALL_SC), y + 2, d->low, SMALL_SC, col);
    pc_ui_text(SL_X1 + 6, y + 2, d->high, SMALL_SC, col);
    /* the part up to the value and the rest side by side, at half pixels (S3's download bar) */
    if (f > 0) pc_ui_rect(SL_X0, y + 5.5f, xf, y + 12.5f, !live ? 0xFF505058 : sel ? 0xFFFFE080 : 0xFFB8A868, ROW_Z(Z_TRACKFILL, i));
    if (f < 1) pc_ui_rect(xf, y + 5.5f, SL_X1, y + 12.5f, 0xFF34343C, ROW_Z(Z_TRACK, i));
}

/* the seed being typed, its caret blinking */
static void edit_draw(float y)
{
    char pre[12];
    pc_ui_text(VAL_X + 12, y, bm.buf, ROW_SC, 0xFFFFFFFF);
    snprintf(pre, sizeof(pre), "%.*s", bm.caret, bm.buf);
    if (bm.blink & 16) pc_ui_rect(VAL_X + 12 + pc_ui_text_w(pre, ROW_SC), y, VAL_X + 14 + pc_ui_text_w(pre, ROW_SC), y + 16, 0xFFFFFFFF, Z_CARET);
}

static void draw_row(int i)
{
    const Row* r = &rows[i];
    int sel = bm.state == BS_MENU && i == bm.row, live = !row_why(r);
    float y = i ? row_y(i) : PAGE_Y;
    unsigned int col = !live ? (sel ? 0xFF909090 : 0xFF686868) : sel ? 0xFFFFFFFF : 0xFFB0B0B0;
    static const char* const names[] = { "", "", "RANDOM SEED", "COPY SEED STRING", "PASTE SEED STRING", "GENERATE", "START",
                                         "CHECK FOR UPDATES", "Randomize Character Model", "", "Choose Models", "RANDOMIZER", "RANDOM SEED", "BACK" };
    char v[96];
    if (sel) pc_ui_rect(ROW_X - 8, y - 3, 600, y + ROW_H - 4, bm.edit ? 0x60406080 : 0x50FFFFFF, Z_ROW);
    switch (r->kind) {
    case K_PAGE: {
        char t[80];
        const char* name = bm.sub ? "RANDOMIZER" : page_names[bm.page];
        float w = pc_ui_text_w(name, 0.72f);
        pc_ui_text(320 - w / 2, y - 2, name, 0.72f, sel ? 0xFFFFFFFF : 0xFFE8E0C0);
        arrow(0, 320 - w / 2 - 14, y + 9, -1, col);
        arrow(0, 320 + w / 2 + 14, y + 9, 1, col);
        snprintf(t, sizeof(t), "%d/%d", bm.page + 1, PG_N);
        pc_ui_text(566, y + 2, t, SMALL_SC, 0xFF909090);
        pc_ui_text(ROW_X, y + 2, page_keys(t, sizeof(t)), SMALL_SC, 0xFF909090);
        return;
    }
    case K_OPT: {
        const BioOpt* d = &biorand_opt[r->idx];
        int val = biorand_opt_get(&bm.o, r->idx);
        float x = ROW_X + depth(r->idx) * 12;   /* inside its boxes */
        text_fit(x, y, d->label, ROW_SC, VAL_X - 16 - x, col);
        if (is_slider(r->idx)) { slider(i, y, d, val, col, sel, live); return; }
        if (r->idx == bm.seed_i && bm.edit && sel) { edit_draw(y); return; }
        if (d->type == BO_BOOL) snprintf(v, sizeof(v), "%s", val ? "On" : "Off");
        else if (d->type == BO_ENUM) snprintf(v, sizeof(v), "%s", d->names[val - d->lo]);
        else snprintf(v, sizeof(v), "%d", val);
        value(i, y, v, col, live);
        return;
    }
    case K_APPEAR: snprintf(v, sizeof(v), "%s", g_biorand_appear ? "On" : "Off"); break;
    case K_CUSTOM:   /* how many are checked; confirm opens the list */
        text_fit(ROW_X, y, names[r->kind], ROW_SC, VAL_X - 16 - ROW_X, col);
        text_fit(VAL_X + 12, y, cast_list_summary(cast_off, g_enemy_opts.cast_imports ? __atomic_load_n(&actor_import_n, __ATOMIC_ACQUIRE) : 0, v, sizeof(v)), ROW_SC, 196, col);
        return;
    case K_RANDPG:   /* a page: its summary, an arrow after it (the lobby's page rows) */
        text_fit(ROW_X, y, names[r->kind], ROW_SC, VAL_X - 16 - ROW_X, col);
        text_fit(VAL_X + 12, y, g_enemy_opts.rand ? "On" : "Off", ROW_SC, 196, col);
        arrow(i, VAL_X + 12 + pc_ui_text_w(g_enemy_opts.rand ? "On" : "Off", ROW_SC) + 16, y + 9, 1, col);
        return;
    case K_SET: {
        const Setting* s = &g_settings[r->idx];
        text_fit(ROW_X, y, s->label, ROW_SC, VAL_X - 16 - ROW_X, col);
        if (bm.edit && sel) edit_draw(y);
        else value(i, y, settings_value(s, v, sizeof(v)), col, live);
        return;
    }
    default: pc_ui_text(ROW_X, y, names[r->kind], ROW_SC, col); return;
    }
    text_fit(ROW_X, y, names[r->kind], ROW_SC, VAL_X - 16 - ROW_X, col);
    value(i, y, v, col, live);
}

/* a distribution as one bar of the GUI's colours, its legend below; id: each slice's own depth */
static void dist(const BioSlice* s, int n, const int* id)
{
    float x = BAR_X0, y = INFO_Y + 17, lx = BAR_X0, ly = y + 21;
    char t[48];
    pc_ui_rect(BAR_X0 - 1, y - 0.5f, BAR_X1 + 1, y + 15.5f, 0xFF707078, Z_DFRAME);   /* (black slices show on it) */
    for (int k = 0; k < n; k++) {
        float x1 = k == n - 1 ? BAR_X1 : x + (BAR_X1 - BAR_X0) * (float)s[k].pct / 100.0f;
        if (x1 - x >= 1.5f) pc_ui_rect(x, y + 0.5f, k == n - 1 ? x1 : x1 - 1, y + 14.5f, 0xFF000000u | s[k].rgb, Z_SLICE + id[k] * ID_DZ);
        x = x1;
    }
    for (int k = 0; k < n; k++) {
        float w;
        snprintf(t, sizeof(t), "%s %d%%", s[k].name, (int)(s[k].pct + 0.5));
        w = 13 + pc_ui_text_w(t, LEGEND_SC) + 12;
        if (lx + w > BAR_X1 + 12 && lx > BAR_X0) { lx = BAR_X0; ly += 15; }
        pc_ui_rect(lx - 1, ly + 1.5f, lx + 10, ly + 12.5f, 0xFF707078, Z_SWFRAME + id[k] * ID_DZ);
        pc_ui_rect(lx, ly + 2.5f, lx + 9, ly + 11.5f, 0xFF000000u | s[k].rgb, Z_SWATCH + id[k] * ID_DZ);
        pc_ui_text(lx + 13, ly, t, LEGEND_SC, 0xFFC8C8C8);
        lx += w;
    }
}

/* below the rows: what the page's options add up to */
static void draw_info(void)
{
    static const int item_id[5] = { 0, 1, 2, 3, 4 };
    char t[96];
    const char* note = NULL;
    float y = INFO_Y;
    pc_ui_rect(40, y - 4.5f, 600, y - 3.5f, 0x60FFFFFF, Z_RULE);
    switch (bm.page) {
    case PG_SEED:
        pc_ui_text(ROW_X - 4, y, "SEED STRING", SMALL_SC, 0xFFFFE080);
        text_fit(ROW_X - 4, y + 18, bm.str, 0.62f, BAR_X1 - ROW_X + 4, 0xFFFFFFFF);
        pc_ui_text(ROW_X - 4, y + 44, "Share this string to generate the same seed and BioRand options.", SMALL_SC, 0xFF909090);
        return;
    case PG_DOORS:
        pc_ui_text(ROW_X - 4, y, "ESTIMATED PLAY TIME", SMALL_SC, 0xFFFFE080);
        biorand_doors_estimate(&bm.o, t, sizeof(t));
        if (t[0]) pc_ui_text(ROW_X - 4, y + 18, t, ROW_SC, 0xFFFFFFFF);
        else note = "Doors are not randomized.";
        break;
    case PG_ITEMS: {
        BioSlice s[5];
        pc_ui_text(ROW_X - 4, y, "ITEM DISTRIBUTION", SMALL_SC, 0xFFFFE080);
        if (!bm.o.RandomItems) note = "Items are not randomized.";
        else if (bm.o.ShuffleItems) note = "Custom item pool is off: the game's own items are shuffled.";
        else dist(s, biorand_item_dist(&bm.o, s), item_id);
        break;
    }
    case PG_ENEMIES:   /* (no distribution: BioRand's CLI changes no CVX enemy; the rows are read-only, row_why) */
        pc_ui_text_wrap(ROW_X - 4, y + 4, BAR_X1 - ROW_X + 4, enemy_why, ROW_SC, 0xFFFFE080, 3);
        return;
    case PG_PORT: note = "These extra options are saved separately from the seed string."; break;
    }
    if (note) pc_ui_text(ROW_X - 4, y + 18, note, ROW_SC, 0xFFA0A0A0);
}

static void help_text(char* t, int n, unsigned int* col)
{
    const Row* r = &rows[bm.row];
    const char* why = row_why(r);
    char k[48];
    if (why) why = tr(why);
    *col = 0xFFA0D0FF;
    if (bm.edit) { snprintf(t, n, tr("Type seed 0 to %d. Enter: confirm; Esc: cancel; gamepad: up/down edits, left/right moves."), edit_hi()); return; }
    if (bm.say_t > 0) { snprintf(t, n, "%s", bm.say); *col = bm.say_err ? 0xFFFFC060 : 0xFFFFE080; return; }
    if (bm.list) { snprintf(t, n, "%s", cast_list_help()); return; }
    switch (r->kind) {
    case K_PAGE:
        snprintf(t, n, tr("Left/right or %s: change page; up/down: choose an option. Changes save automatically."), page_keys(k, sizeof(k)));
        return;
    case K_OPT: {
        const BioOpt* d = &biorand_opt[r->idx];
        const char* h = d->help ? d->help : "Choose a starting weapon type: None, Random, Sidearm, Primary or Powerful.";
        snprintf(t, n, "%s%s%s%s", why ? why : "", why ? " " : "", tr(h), r->idx == bm.seed_i ? tr(" Confirm: enter a seed. Left/right: adjust.") : "");
        for (char* p = t; *p; p++) if (*p == '\n') *p = ' ';
        if (why) *col = 0xFFFFC060;
        return;
    }
    case K_RANDOM: snprintf(t, n, tr("Choose a random seed number. Keep the other options.")); return;
    case K_COPY: snprintf(t, n, tr("Copy the seed string to share the seed and BioRand options.")); return;
    case K_PASTE: snprintf(t, n, tr("Paste a seed string to use its seed and BioRand options.")); return;
    case K_GEN:
        snprintf(t, n, tr("%s%sGenerate your BioRand game with these options. START plays it."), why ? why : "", why ? " " : "");
        if (why) *col = 0xFFFFC060;
        return;
    case K_START:
        snprintf(t, n, tr("%s%sStart a new game with your generated BioRand game%s%s. NEW GAME on the title menu always starts "
                 "without BioRand."), why ? why : "", why ? " " : "", biorand_seed() >= 0 ? ", " : "", biorand_seed() >= 0 ? biorand_seed_text() : "");
        if (why) *col = 0xFFFFC060;
        return;
    case K_CHECK: snprintf(t, n, tr("Check GitHub for the latest BioRand generator and offer to download it.")); return;
    case K_APPEAR:
        snprintf(t, n, tr("Choose a character model for each generated seed. Saved separately from the seed string."));
        return;
    case K_CUSTOM: snprintf(t, n, "Choose the models the characters can appear as."); return;
    case K_RANDPG: snprintf(t, n, "%s", tr("Set the enemy randomizer, extra enemies and enemy positions. They also apply to BioRand games.")); return;
    case K_RSEED: snprintf(t, n, "%s", tr("Picks a new random seed.")); return;
    case K_SET: {
        const Setting* s = &g_settings[r->idx];
        snprintf(t, n, "%s%s%s%s", why ? why : "", why ? " " : "", s->help ? tr(s->help) : "",
                 s->val == &g_enemy_opts.seed ? tr(" Confirm: enter a seed. Left/right: adjust.") : "");
        if (why) *col = 0xFFFFC060;
        return;
    }
    default: snprintf(t, n, "%s", tr(bm.sub ? "Back to EXTRA OPTIONS (also cancel or a right click)." : "Back to the title menu (also cancel or a right click).")); return;
    }
}

static void draw_page(void)
{
    char t[320];
    unsigned int col;
    if (!bm.have) return;
    draw_row(0);
    if (bm.state != BS_MENU) return;   /* (text is drawn over any box: a dialog shows the page row only) */
    pc_ui_rect(40, 94.5f, 600, 95.5f, 0x60FFFFFF, Z_RULE);
    if (bm.list) cast_list_draw();
    for (int i = bm.top; !bm.list && i < nrows && i < bm.top + VISIBLE; i++) draw_row(i);
    if (bm.top > 1 && !bm.list) {   /* more above / below */
        NJS_POLYGON_VTX v[3] = { { 604, 106, 0.3f, 0xFFA0A0A0 }, { 616, 106, 0.3f, 0xFFA0A0A0 }, { 610, 99, 0.3f, 0xFFA0A0A0 } };
        njDrawPolygon(v, 3, 1);
    }
    if (nrows > bm.top + VISIBLE && !bm.list) {
        NJS_POLYGON_VTX v[3] = { { 604, 300, 0.3f, 0xFFA0A0A0 }, { 616, 300, 0.3f, 0xFFA0A0A0 }, { 610, 307, 0.3f, 0xFFA0A0A0 } };
        njDrawPolygon(v, 3, 1);
    }
    if (!bm.list) draw_info();
    help_text(t, sizeof(t), &col);
    pc_ui_text_wrap(24, 390, 592, t, 0.5f, col, 5);
}

static void draw_work(void)
{
    static const char* const stage[] = { "", "Checking for BioRand updates...", "Downloading BioRand...",
                                         "Verifying download...", "Installing..." };
    BrdlStatus s;
    char line[64];
    float f = 0;
    float y0, y1;
    biorand_cli_status(&s);
    dlg_box(&y0, &y1);
    line[0] = 0;
    if (bm.gen) {
        const char* m = s.msg;
        char head[48];
        if (m[0] == '[' && strstr(m, "] ")) m = strstr(m, "] ") + 2;   /* ("[P1] Randomizing doors": the player's part) */
        snprintf(head, sizeof(head), tr("BIORAND - GENERATING SEED %d"), bm.gen_seed);
        pc_ui_dialog(y0, y1, Z_BOX, head, bm.cancelling ? "Cancelling..." : m[0] ? m : "Starting BioRand...", 0.55f, 0xFFC0C0C0, 2,
                     "CANCEL", NULL, 0, 0);
        f = s.pct / 100.0f;
        snprintf(line, sizeof(line), "%d%%", s.pct);
    } else {
        pc_ui_dialog(y0, y1, Z_BOX, bm.install ? "BIORAND - DOWNLOAD" : "BIORAND - CHECK FOR UPDATES",
                     bm.cancelling ? "Cancelling..." : s.state < BRDL_INSTALLED ? stage[s.state] : "", 0.55f, 0xFFC0C0C0, 2, "CANCEL", NULL, 0, 0);
        if (!bm.install) return;
        if (s.state >= BRDL_VERIFY) f = 1;
        else if (s.total > 0) f = s.done >= s.total ? 1.0f : (float)s.done / s.total;
        if (s.total > 0) snprintf(line, sizeof(line), tr("%.1f / %.1f MB"), s.done / 1048576.0, s.total / 1048576.0);
    }
    {   /* the done part and the rest side by side; at half pixels: the renderer's rows of a long flat quad's
           two triangles can differ by one (a step in an opaque bar), at most the last row this way */
        float x = BOX_X0 + 20 + (BOX_X1 - BOX_X0 - 40) * f;
        float yb = y0 + 54 + WORK_TEXT + 2.5f;
        if (f > 0) pc_ui_rect(BOX_X0 + 20, yb, x, yb + 12, 0xFFFFE080, Z_FILL);
        if (f < 1) pc_ui_rect(x, yb, BOX_X1 - 20, yb + 12, 0xFF303038, Z_BAR);
        pc_ui_text(BOX_X1 - 20 - pc_ui_text_w(line, 0.5f), yb + 15.5f, line, 0.5f, 0xFFC8C8C8);
    }
}

void pc_biorand_menu_close(void)
{
    if (!bm.open) return;
    if (bm.state == BS_WORK) biorand_cli_cancel();   /* the title ends (a soft reset): the download with it */
    if (bm.host) coop_biorand_reply(bm.done ? 2 : 0);   /* the lobby again: the pack compared, or we leave */
    g_lobby_io.text_on = 0;
    g_lobby_io.list_on = 0;
    bm.open = 0;
    host_log("biorand menu: closed\n");
}

/* Adv_BioCvTitle mode 30 while the BioRand screen is up (lobby_glue.c pc_lobby): 1 back to the title menu, 2 START's new
   game (sys->gm_mode set, BioRand on for it) */
int pc_biorand_menu(void)
{
    int r = 0;
    if (!bm.open) {
        const char *dig = "", *hs = coop_biorand_offer(&dig);
        memset(&bm, 0, sizeof(bm));
        bm.open = 1;
        bm.top = 1;
        bm.seed_i = biorand_opt_find("Seed");
        biorand_opts_load(&bm.o);
        biorand_seed_encode(&bm.o, bm.str, sizeof(bm.str));
        if (hs) {   /* the co-op host's seed string, as it came (coop_glue.c decoded it too); recvx.ini keeps ours */
            bm.host = 1;
            biorand_seed_decode(hs, &bm.o);
            snprintf(bm.str, sizeof(bm.str), "%s", hs);
            snprintf(bm.dig, sizeof(bm.dig), "%s", dig);
        }
        g_lobby_io.paste_req = 0;
        look_mods();
        bm.have = biorand_cli_present();
        if (bm.host) ask(A_HOST);
        else if (!bm.have) ask(A_DOWNLOAD);
        host_log("biorand menu: open, the CLI is %s, seed string %s%s\n", bm.have ? "there" : "missing", bm.str, bm.host ? " (the co-op host's)" : "");
    }
    bm.blink++;
    if (bm.say_t > 0) bm.say_t--;
    if (bm.pasting && __atomic_load_n(&g_lobby_io.paste_req, __ATOMIC_ACQUIRE) == 2) {
        bm.pasting = 0;
        g_lobby_io.paste_req = 0;
        paste_end(g_lobby_io.paste);
    }
    g_lobby_io.list_on = bm.list;   /* (Page Up / Down, Home, End) */
    if (bm.state == BS_WORK) r = work_end();
    else if (bm.host && !coop_biorand_offer(NULL)) r = 1;   /* the session ended, or the host's pack is no longer needed */
    build_rows();
    if (!r) r = input();
    build_rows();   /* (a page or a box changed) */
    scroll_to_sel();
    AdvEasySetTextureList(0);
    AdvDwawOnePictureEx(0, 0xFFFFFFFF);   /* the title picture, darkened, as the lobby */
    pc_ui_rect(0, 0, 640, 480, 0xB0000000, Z_DIM);
    pc_ui_rect(24, 64, 616, 384, 0x70000000, Z_DIM + 0.0005f);
    pc_title_word_br(320.0f, 16.0f, 0xFFFFFFFF, 192);
    pc_ui_rect(0, 384, 640, 480, 0x60000000, Z_STRIP);
    draw_page();
    if (bm.state == BS_WORK) draw_work();
    else if (bm.state != BS_MENU) {   /* as RETURN TO TITLE's question */
        float y0, y1;
        dlg_box(&y0, &y1);
        if (bm.state == BS_ASK) pc_ui_dialog(y0, y1, Z_BOX, NULL, ask_msg(), 0.55f, 0xFFFFFFFF, 5, "YES", "NO", bm.sel, 0);
        else if (bm.state == BS_PICK) pc_ui_dialog_btns(y0, y1, Z_BOX, pick_head(), pick_text(), 0.55f, 0xFFC0C0C0, 4, diff_btn, 4, bm.sel);
        else pc_ui_dialog(y0, y1, Z_BOX, result_head(), bm.msg, 0.55f, bm.err ? 0xFFFFC060 : 0xFFC0C0C0, 4, "OK", NULL, 0, 0);
    }
    AdvEasySetTextureList(0);   /* the title's texture list again (the font set its own) */
    if (r) pc_biorand_menu_close();
    return r;
}
