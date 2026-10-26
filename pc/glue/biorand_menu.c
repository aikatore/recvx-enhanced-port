/* The BIORAND screen (docs/BIORAND_INGAME.md, S3 and S4b): the title menu's BIORAND row (--dev, lobby_glue.c
 * pc_title_menu_init) opens it as Adv_BioCvTitle mode 30, as MULTIPLAYER opens the lobby. Drawn as the
 * lobby and ENHANCED SETTINGS, with their helpers (pc_ui_*). States:
 *   BS_MENU    the generator's pages SEED, DOORS, ITEMS, ENEMIES, PORT EXTRAS: BioRand's options
 *              (biorand_opts.h; each change re-encodes the seed string into recvx.ini biorand_seed),
 *              the doors estimate, the item and enemy distribution bars, the seed string's copy / paste
 *   BS_ASK     a Yes/No question over it (A_DOWNLOAD with no CLI: No leaves; A_CHECK; A_NEWER; A_GEN)
 *   BS_WORK    the downloader's thread runs (pc/src/net/biorand_dl.h): its stage, a bar, MB, CANCEL; or the
 *              CLI generates the pack (bm.gen, S5): its Progress: percent and message on the bar
 *   BS_RESULT  a message (newest, installed, generated, an error) and OK
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
#include "../src/host/lobby.h"
#include "../src/host/settings.h"
#include "../src/host/bindings.h"
#include "../src/host/enemies.h"
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
int pc_in_game(void);   /* options_glue.c */

enum { BTN_L1 = 0x4, BTN_R1 = 0x8, BTN_START = 0x800, BTN_UP = 0x1000, BTN_RIGHT = 0x2000, BTN_DOWN = 0x4000, BTN_LEFT = 0x8000 };
enum { BS_MENU, BS_ASK, BS_WORK, BS_RESULT };
enum { A_DOWNLOAD, A_CHECK, A_NEWER, A_GEN, A_HOST, A_DIGEST };
/* the pages; DOORS..ENEMIES show biorand_opt[]'s groups BOG_DOORS..BOG_ENEMIES (the same numbers) */
enum { PG_SEED, PG_DOORS, PG_ITEMS, PG_ENEMIES, PG_PORT, PG_N };
static const char* const page_names[PG_N] = { "SEED", "DOORS", "ITEMS", "ENEMIES", "PORT EXTRAS" };
/* row kinds: K_OPT a biorand_opt[] row, K_SET a settings table row (the cutscene cast) */
enum { K_PAGE, K_OPT, K_RANDOM, K_COPY, K_PASTE, K_GEN, K_CHECK, K_APPEAR, K_SET, K_BACK };
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
#define Z_BOX 0.31f
#define Z_BAR 0.32f
#define Z_FILL 0.33f
#define Z_BTN 0.34f
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
#define BTN_SC 0.62f
#define BOX_X0 100.0f
#define BOX_X1 540.0f
#define BOX_Y0 150.0f
#define BOX_Y1 330.0f
#define BTN_Y 290.0f
#define BTN_W 100.0f

static struct {
    int open, state, ask, row, sel;     /* row: rows[]; sel: BS_ASK 0 Yes 1 No */
    int install, cancelling, have;      /* the job installs (else checks); CANCEL pressed; the CLI is there */
    int gen, gen_seed;                  /* the job is the CLI generating (seed number gen_seed) */
    int free_mb;                        /* free space in the mods folder (-1 unknown), at the last look */
    char msg[320];
    int err;
    MouseTake mt;
    int page, top, seed_i;              /* seed_i: the Seed row of biorand_opt[] */
    int edit, caret, pasting, blink;    /* the seed number is typed; the clipboard was asked for (lobby_io.c) */
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
    "BioRand is not installed. Download the BioRand CLI (nightly) from GitHub? About 7 MB. It is checked against GitHub's checksum.",
    "Check GitHub for a new BioRand CLI?",
    "A newer BioRand CLI is available. Download it? About 7 MB. It is checked against GitHub's checksum.",
    NULL,   /* A_GEN: gen_text() */
    NULL,   /* A_HOST: gen_text() */
    "Your BioRand CLI is not the host's build, so your pack may differ from the host's. Download the newest BioRand CLI first? "
    "(The host should have the newest too.) No generates with yours.",
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

/* GENERATE's question: what it does, the memory it takes, the free space */
static const char* gen_text(void)
{
    static char t[400];
    char f[40] = "";
    char h[64];
    if (bm.free_mb >= 0) snprintf(f, sizeof(f), " Free space: %.1f GB.", bm.free_mb / 1024.0);
    if (bm.host) snprintf(h, sizeof(h), "The host plays BioRand seed %d. Generate it now", bm.o.Seed);   /* (A_HOST) */
    else snprintf(h, sizeof(h), "Generate BioRand seed %d", bm.o.Seed);
    snprintf(t, sizeof(t), "%s into mods/biorand? It replaces the pack there. BioRand may use several GB of memory "
             "(about 6 GB) for a short time.%s", h, f);
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
    } else result(r < 0 ? "Cannot write BioRand's settings.json into the save folder." : "The last download is still ending. Try again in a moment.", 1);
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
        biorand_enable();
        host_log("biorand menu: generated %s; the pack says seed %d; BioRand on\n", bm.str, biorand_seed());
        snprintf(m, sizeof(m), "BioRand seed %d generated. BIORAND is on: start a NEW GAME.%s%s", bm.gen_seed,
                 s->warn[0] ? " BioRand warned: " : "", s->warn);
        result(m, 0);
    } else if (s->state == BRDL_ERROR) result(s->error, 1);
    else { to_menu(); say("Generating cancelled.", 0); }
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
    result(s.state == BRDL_ERROR ? s.error : "The last download is still ending. Try again in a moment.", 1);
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
        result("The BioRand CLI is installed.", 0);
        break;
    case BRDL_CURRENT: result("You have the newest BioRand CLI.", 0); break;
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
        if (yes && bm.ask == A_GEN) { gen_start(); return 0; }
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
/* the cutscene cast's rows of the settings table (F1 and ENHANCED SETTINGS edit the same) */
static const Setting* cast_row(int k)
{
    const int* v[4] = { &g_enemy_opts.cast, &g_enemy_opts.cast_all, &g_enemy_opts.cast_assign, &g_enemy_opts.cast_imports };
    for (int i = 0; i < g_settings_n; i++)
        if (g_settings[i].val == v[k]) return &g_settings[i];
    return NULL;
}

static void add(int kind, int idx) { if (nrows < MAX_ROWS) rows[nrows++] = (Row){ kind, idx }; }

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
        add(K_CHECK, 0);
    } else if (bm.page == PG_PORT) {
        add(K_APPEAR, 0);
        for (int k = 0; k < 4; k++) {
            const Setting* s = cast_row(k);
            if (s && (!s->shown || s->shown())) add(K_SET, (int)(s - g_settings));
        }
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
        if (!bm.have) return "The BioRand CLI is not installed.";
        if (!biorand_iso_known()) return "The game's disc image is not known.";
        if (pc_in_game()) return "Only at the title menu.";
        if (coop_biorand() >= 0 && !bm.host) return "Not as a guest in a co-op session.";   /* (the host's own seed: S5b) */
        if (bm.free_mb >= 0 && bm.free_mb < BRDL_GEN_FREE_MB) {
            snprintf(why, sizeof(why), "Needs about %d GB free in the mods folder; it has %.1f GB.", BRDL_GEN_FREE_MB / 1024, bm.free_mb / 1024.0);
            return why;
        }
        return NULL;
    case K_SET: return settings_locked(&g_settings[r->idx]);
    case K_OPT: {
        const char* l = biorand_opt_locked(&bm.o, r->idx);
        if (l) return l;
        if (biorand_opt_active(&bm.o, r->idx)) return NULL;
        for (int p = biorand_opt_find(biorand_opt[r->idx].parent); p >= 0; p = biorand_opt_find(biorand_opt[p].parent))
            if (!biorand_opt_get(&bm.o, p)) { snprintf(why, sizeof(why), "Needs \"%s\" on.", biorand_opt[p].label); break; }
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
static void edit_begin(void)
{
    snprintf(bm.buf, sizeof(bm.buf), "%d", bm.o.Seed);
    bm.caret = (int)strlen(bm.buf);
    bm.edit = 1;
    g_lobby_io.tail = g_lobby_io.head;   /* nothing typed before counts */
    g_lobby_io.text_on = 1;
    CallSystemSe(0, 3);
}

static void edit_end(int accept)
{
    if (accept && bm.buf[0]) set_opt(bm.seed_i, (int)strtol(bm.buf, NULL, 10));   /* (7 digits at most; set clamps) */
    bm.edit = 0;
    g_lobby_io.text_on = 0;
    CallSystemSe(0, accept ? 3 : 0);
}

static void edit_input(unsigned int press, unsigned int rept, unsigned int ok, unsigned int cancel)
{
    int n;
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
        else if (k >= '0' && k <= '9' && n < 7) {
            memmove(bm.buf + bm.caret + 1, bm.buf + bm.caret, n - bm.caret + 1);
            bm.buf[bm.caret++] = (char)k;
        }
    }
    if (press & ok) { edit_end(1); return; }
    if (press & cancel) { edit_end(0); return; }
    n = (int)strlen(bm.buf);
    if (rept & (BTN_UP | BTN_DOWN)) {
        if (bm.caret == n) { if (n >= 7) return; bm.buf[n] = '0'; bm.buf[n + 1] = 0; }   /* a new digit, 0 */
        else bm.buf[bm.caret] = (char)('0' + (bm.buf[bm.caret] - '0' + ((rept & BTN_UP) ? 1 : 9)) % 10);
        CallSystemSe(0, 2);
    }
    if ((rept & BTN_LEFT) && bm.caret > 0) bm.caret--;
    if ((rept & BTN_RIGHT) && bm.caret < n) bm.caret++;
}

static void random_seed(void)
{
    static unsigned x;
    x = x * 1664525u + 1013904223u + (unsigned)time(NULL) * 2654435761u + (unsigned)clock();
    set_opt(bm.seed_i, (int)((x >> 4) % (BIORAND_SEED_MAX + 1u)));
    CallSystemSe(0, 3);
}

static void copy_string(void)
{
    char m[192];
    snprintf(g_lobby_io.clip, sizeof(g_lobby_io.clip), "%s", bm.str);
    g_lobby_io.clip_req = 1;   /* lobby_io.c sets the clipboard */
    snprintf(m, sizeof(m), "Copied to the clipboard: %s", bm.str);
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
    snprintf(m, sizeof(m), "Pasted: seed %d and its options.", t.Seed);
    say(m, 0);
    CallSystemSe(0, 3);
}

/* confirm on a row: 1 leave the screen */
static int activate(const Row* r)
{
    switch (r->kind) {
    case K_PAGE: change(r, 1); return 0;
    case K_OPT:
        if (r->idx == bm.seed_i) edit_begin();
        else if (biorand_opt[r->idx].type == BO_BOOL || biorand_opt[r->idx].type == BO_ENUM) change(r, 1);
        else if (row_why(r)) refuse(row_why(r));
        else CallSystemSe(0, 1);   /* a slider: left/right */
        return 0;
    case K_RANDOM: random_seed(); return 0;
    case K_COPY: copy_string(); return 0;
    case K_PASTE: g_lobby_io.paste_req = 1; bm.pasting = 1; return 0;
    case K_GEN:
        look_mods();   /* (the free space now) */
        if (row_why(r)) refuse(row_why(r));
        else { CallSystemSe(0, 3); ask(A_GEN); }
        return 0;
    case K_CHECK: CallSystemSe(0, 3); ask(A_CHECK); return 0;
    case K_BACK: CallSystemSe(0, 0); return 1;
    default: change(r, 1); return 0;   /* K_APPEAR, K_SET */
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

/* 1 leave */
static int input(void)
{
    ADV_WORK* ap = &AdvWork;
    unsigned int press = Pad[ap->PortId].press, rept = Pad[ap->PortId].Rept;
    unsigned int ok = AdvGetOkButton() | BTN_START, cancel = AdvGetCancelButton();
    MouseTake* m = &bm.mt;
    int hit;
    menu_mouse_take(m);
    if (press || rept || m->left || m->right || m->moved || m->wheel || bm.state == BS_WORK || g_lobby_io.head != g_lobby_io.tail)
        StopAdvScreenSaver(0);   /* (a download is no idle screen) */
    if (bm.state == BS_MENU) {
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
        if (m->right || (press & cancel)) { CallSystemSe(0, 0); return 1; }   /* (back, whichever row) */
        if (press & BTN_L1) set_page(bm.page - 1);
        else if (press & BTN_R1) set_page(bm.page + 1);
        else if (rept & BTN_UP) move_sel(-1);
        else if (rept & BTN_DOWN) move_sel(1);
        else if (rept & BTN_LEFT) change(&rows[bm.row], -1);
        else if (rept & BTN_RIGHT) change(&rows[bm.row], 1);
        else if (press & ok) return activate(&rows[bm.row]);
        return 0;
    }
    hit = -1;
    for (int i = 0; i < (bm.state == BS_ASK ? 2 : 1); i++) {   /* the dialog's buttons */
        float x = bm.state == BS_ASK ? 320.0f + (i ? 20.0f : -20.0f - BTN_W) : 320.0f - BTN_W / 2;
        if (m->x >= x && m->x < x + BTN_W && m->y >= BTN_Y - 4 && m->y < BTN_Y + 22) hit = i;
    }
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

static void draw_row(int i)
{
    const Row* r = &rows[i];
    int sel = bm.state == BS_MENU && i == bm.row, live = !row_why(r);
    float y = i ? row_y(i) : PAGE_Y;
    unsigned int col = !live ? (sel ? 0xFF909090 : 0xFF686868) : sel ? 0xFFFFFFFF : 0xFFB0B0B0;
    static const char* const names[] = { "", "", "RANDOM SEED", "COPY SEED STRING", "PASTE SEED STRING", "GENERATE",
                                         "CHECK FOR A NEW BIORAND CLI", "Randomize Played as", "", "BACK" };
    char v[96];
    if (sel) pc_ui_rect(ROW_X - 8, y - 3, 600, y + ROW_H - 4, bm.edit ? 0x60406080 : 0x50FFFFFF, Z_ROW);
    switch (r->kind) {
    case K_PAGE: {
        char t[80];
        float w = pc_ui_text_w(page_names[bm.page], 0.72f);
        pc_ui_text(320 - w / 2, y - 2, page_names[bm.page], 0.72f, sel ? 0xFFFFFFFF : 0xFFE8E0C0);
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
        if (r->idx == bm.seed_i && bm.edit && sel) {
            char pre[12];
            pc_ui_text(VAL_X + 12, y, bm.buf, ROW_SC, 0xFFFFFFFF);
            snprintf(pre, sizeof(pre), "%.*s", bm.caret, bm.buf);
            if (bm.blink & 16) pc_ui_rect(VAL_X + 12 + pc_ui_text_w(pre, ROW_SC), y, VAL_X + 14 + pc_ui_text_w(pre, ROW_SC), y + 16, 0xFFFFFFFF, Z_CARET);
            return;
        }
        if (d->type == BO_BOOL) snprintf(v, sizeof(v), "%s", val ? "On" : "Off");
        else if (d->type == BO_ENUM) snprintf(v, sizeof(v), "%s", d->names[val - d->lo]);
        else snprintf(v, sizeof(v), "%d", val);
        value(i, y, v, col, live);
        return;
    }
    case K_APPEAR: snprintf(v, sizeof(v), "%s", g_biorand_appear ? "On" : "Off"); break;
    case K_SET: {
        const Setting* s = &g_settings[r->idx];
        text_fit(ROW_X, y, s->label, ROW_SC, VAL_X - 16 - ROW_X, col);
        value(i, y, settings_value(s, v, sizeof(v)), col, live);
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
        pc_ui_text(ROW_X - 4, y + 44, "It holds the seed and every option: the same string gives the same game.", SMALL_SC, 0xFF909090);
        return;
    case PG_DOORS:
        pc_ui_text(ROW_X - 4, y, "ESTIMATED COMPLETION TIME", SMALL_SC, 0xFFFFE080);
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
    case PG_ENEMIES: {
        BioSlice s[6];
        int n, id[6] = { 0 };
        pc_ui_text(ROW_X - 4, y, "ENEMY DISTRIBUTION", SMALL_SC, 0xFFFFE080);
        if (!bm.o.RandomEnemies) note = "Enemies are not randomized.";
        else if (!(n = biorand_enemy_dist(&bm.o, s))) note = "Every enemy is at None.";
        else {
            for (int i = 0, e = 0; i < biorand_opt_n; i++)   /* each enemy's own depth: its place among the ratio rows */
                if (biorand_opt[i].type == BO_RATIO && biorand_opt[i].group == BOG_ENEMIES) {
                    for (int k = 0; k < n; k++)
                        if (!strcmp(biorand_opt[i].label, s[k].name)) id[k] = e;
                    e++;
                }
            dist(s, n, id);
        }
        break;
    }
    case PG_PORT: note = "The port's own options: they are not part of the seed string."; break;
    }
    if (note) pc_ui_text(ROW_X - 4, y + 18, note, ROW_SC, 0xFFA0A0A0);
}

static void help_text(char* t, int n, unsigned int* col)
{
    const Row* r = &rows[bm.row];
    const char* why = row_why(r);
    char k[48];
    *col = 0xFFA0D0FF;
    if (bm.edit) { snprintf(t, n, "Type the seed number (0 to %d); Enter takes it, Esc cancels. Gamepad: up/down change the digit, left/right move.", BIORAND_SEED_MAX); return; }
    if (bm.say_t > 0) { snprintf(t, n, "%s", bm.say); *col = bm.say_err ? 0xFFFFC060 : 0xFFFFE080; return; }
    switch (r->kind) {
    case K_PAGE:
        snprintf(t, n, "Left/right or %s change the page, up/down choose an option. Every change goes into the seed string and is saved at once.", page_keys(k, sizeof(k)));
        return;
    case K_OPT: {
        const BioOpt* d = &biorand_opt[r->idx];
        const char* h = d->help ? d->help : "A weapon of the random starting inventory: None, Random, or a Sidearm, Primary or Powerful one.";
        snprintf(t, n, "%s%s%s%s", why ? why : "", why ? " " : "", h, r->idx == bm.seed_i ? " Confirm: type it. Left/right: one up or down." : "");
        for (char* p = t; *p; p++) if (*p == '\n') *p = ' ';
        if (why) *col = 0xFFFFC060;
        return;
    }
    case K_RANDOM: snprintf(t, n, "A new random seed number. The other options stay."); return;
    case K_COPY: snprintf(t, n, "Copies the seed string to the clipboard. It holds the seed and every option, so others can generate the same game."); return;
    case K_PASTE: snprintf(t, n, "Takes a seed string from the clipboard (from BioRand's own program or from a friend): its seed and every option."); return;
    case K_GEN:
        snprintf(t, n, "%s%sGenerates the randomized game from the seed string into mods/biorand (about 1.4 GB) with the BioRand CLI, and turns BioRand on.",
                 why ? why : "", why ? " " : "");
        if (why) *col = 0xFFFFC060;
        return;
    case K_CHECK: snprintf(t, n, "Asks GitHub whether a newer BioRand CLI (the nightly of biorand/classic) is out, and offers to download it."); return;
    case K_APPEAR:
        snprintf(t, n, "On: each generated seed also picks who you appear as (\"You appear as\"), the same for the same seed. Not part of the seed string.");
        return;
    case K_SET: {
        const Setting* s = &g_settings[r->idx];
        snprintf(t, n, "%s%s%s", why ? why : "", why ? " " : "", s->help ? s->help : "");
        if (why) *col = 0xFFFFC060;
        return;
    }
    default: snprintf(t, n, "Back to the title menu (also cancel or a right click)."); return;
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
    for (int i = bm.top; i < nrows && i < bm.top + VISIBLE; i++) draw_row(i);
    if (bm.top > 1) {   /* more above / below */
        NJS_POLYGON_VTX v[3] = { { 604, 106, 0.3f, 0xFFA0A0A0 }, { 616, 106, 0.3f, 0xFFA0A0A0 }, { 610, 99, 0.3f, 0xFFA0A0A0 } };
        njDrawPolygon(v, 3, 1);
    }
    if (nrows > bm.top + VISIBLE) {
        NJS_POLYGON_VTX v[3] = { { 604, 300, 0.3f, 0xFFA0A0A0 }, { 616, 300, 0.3f, 0xFFA0A0A0 }, { 610, 307, 0.3f, 0xFFA0A0A0 } };
        njDrawPolygon(v, 3, 1);
    }
    draw_info();
    help_text(t, sizeof(t), &col);
    pc_ui_text_wrap(24, 390, 592, t, 0.5f, col, 5);
}

static void box(void)
{
    pc_ui_rect(BOX_X0, BOX_Y0, BOX_X1, BOX_Y1, 0xF0181820, Z_BOX);
}

static float btn_x(int i) { return bm.state == BS_ASK ? 320.0f + (i ? 20.0f : -20.0f - BTN_W) : 320.0f - BTN_W / 2; }

static void button(int i, const char* s, int sel)
{
    float x = btn_x(i);
    if (sel) pc_ui_rect(x, BTN_Y - 4, x + BTN_W, BTN_Y + 22, 0x50FFFFFF, Z_BTN);
    pc_ui_text(x + (BTN_W - pc_ui_text_w(s, BTN_SC)) / 2, BTN_Y, s, BTN_SC, sel ? 0xFFFFFFFF : 0xFFB0B0B0);
}

static void draw_work(void)
{
    static const char* const stage[] = { "", "Looking up the newest BioRand CLI on GitHub...", "Downloading the BioRand CLI...",
                                         "Checking the download (SHA-256)...", "Installing..." };
    BrdlStatus s;
    char line[64];
    float f = 0;
    biorand_cli_status(&s);
    box();
    if (bm.gen) {
        const char* m = s.msg;
        float x;
        if (m[0] == '[' && strstr(m, "] ")) m = strstr(m, "] ") + 2;   /* ("[P1] Randomizing doors": the player's part) */
        snprintf(line, sizeof(line), "BIORAND - GENERATING SEED %d", bm.gen_seed);
        pc_ui_text(BOX_X0 + 20, BOX_Y0 + 16, line, 0.5f, 0xFFFFE080);
        pc_ui_text_wrap(BOX_X0 + 20, BOX_Y0 + 48, BOX_X1 - BOX_X0 - 40, bm.cancelling ? "Cancelling..." : m[0] ? m : "Starting the BioRand CLI...", 0.55f, 0xFFFFFFFF, 2);
        f = s.pct / 100.0f;
        x = BOX_X0 + 20 + (BOX_X1 - BOX_X0 - 40) * f;
        if (f > 0) pc_ui_rect(BOX_X0 + 20, 228.5f, x, 244.5f, 0xFFFFE080, Z_FILL);
        if (f < 1) pc_ui_rect(x, 228.5f, BOX_X1 - 20, 244.5f, 0xFF303038, Z_BAR);
        snprintf(line, sizeof(line), "%d%%", s.pct);
        pc_ui_text(BOX_X1 - 20 - pc_ui_text_w(line, 0.5f), 252, line, 0.5f, 0xFFC8C8C8);
        button(0, "CANCEL", 1);
        return;
    }
    pc_ui_text(BOX_X0 + 20, BOX_Y0 + 16, bm.install ? "BIORAND CLI - DOWNLOAD" : "BIORAND CLI - CHECK FOR AN UPDATE", 0.5f, 0xFFFFE080);
    pc_ui_text_wrap(BOX_X0 + 20, BOX_Y0 + 48, BOX_X1 - BOX_X0 - 40, bm.cancelling ? "Cancelling..." : s.state < BRDL_INSTALLED ? stage[s.state] : "",
                    0.55f, 0xFFFFFFFF, 2);
    if (bm.install) {
        if (s.state >= BRDL_VERIFY) f = 1;
        else if (s.total > 0) f = s.done >= s.total ? 1.0f : (float)s.done / s.total;
        {   /* the done part and the rest side by side; at half pixels: the renderer's rows of a long flat quad's
                       two triangles can differ by one (a step in an opaque bar), at most the last row this way */
            float x = BOX_X0 + 20 + (BOX_X1 - BOX_X0 - 40) * f;
            if (f > 0) pc_ui_rect(BOX_X0 + 20, 228.5f, x, 244.5f, 0xFFFFE080, Z_FILL);
            if (f < 1) pc_ui_rect(x, 228.5f, BOX_X1 - 20, 244.5f, 0xFF303038, Z_BAR);
        }
        if (s.total > 0) {
            snprintf(line, sizeof(line), "%.1f / %.1f MB", s.done / 1048576.0, s.total / 1048576.0);
            pc_ui_text(BOX_X1 - 20 - pc_ui_text_w(line, 0.5f), 252, line, 0.5f, 0xFFC8C8C8);
        }
    }
    button(0, "CANCEL", 1);
}

void pc_biorand_menu_close(void)
{
    if (!bm.open) return;
    if (bm.state == BS_WORK) biorand_cli_cancel();   /* the title ends (a soft reset): the download with it */
    if (bm.host) coop_biorand_reply(bm.done ? 2 : 0);   /* the lobby again: the pack compared, or we leave */
    g_lobby_io.text_on = 0;
    bm.open = 0;
    host_log("biorand menu: closed\n");
}

/* Adv_BioCvTitle mode 30 while the BioRand screen is up (lobby_glue.c pc_lobby): 1 back to the title menu */
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
    if (bm.state == BS_ASK) {
        box();
        pc_ui_text_wrap(BOX_X0 + 20, BOX_Y0 + 20, BOX_X1 - BOX_X0 - 40, bm.ask == A_GEN || bm.ask == A_HOST ? gen_text() : ask_text[bm.ask], 0.55f, 0xFFFFFFFF, 5);
        button(0, "YES", bm.sel == 0);
        button(1, "NO", bm.sel == 1);
    } else if (bm.state == BS_WORK) draw_work();
    else if (bm.state == BS_RESULT) {
        box();
        pc_ui_text(BOX_X0 + 20, BOX_Y0 + 16, bm.gen || bm.host ? (bm.err ? "BIORAND - ERROR" : "BIORAND") : bm.err ? "BIORAND CLI - ERROR" : "BIORAND CLI", 0.5f, 0xFFFFE080);
        pc_ui_text_wrap(BOX_X0 + 20, BOX_Y0 + 48, BOX_X1 - BOX_X0 - 40, bm.msg, 0.55f, bm.err ? 0xFFFFC060 : 0xFFFFFFFF, 4);
        button(0, "OK", 1);
    }
    AdvEasySetTextureList(0);   /* the title's texture list again (the font set its own) */
    if (r) pc_biorand_menu_close();
    return r;
}
