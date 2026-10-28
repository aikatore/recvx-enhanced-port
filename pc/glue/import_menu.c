/* The IMPORT screen (docs/IMPORT_OUTBREAK.md "IMPORT screen"): the title menu's EXTRA > IMPORT (--dev, lobby_glue.c)
 * opens it as Adv_BioCvTitle mode 30, as BIORAND opens its screen. The in-game version of the F9 window
 * (overlay.cpp import_window), drawn as the BIORAND screen with the same helpers (pc_ui_*) and dialogs (options_glue.c
 * pc_ui_dialog). Pages, each a list of rows:
 *   P_CATS   what can be imported: CHARACTERS, LANGUAGES (cats[], IMPORT_CAT_*)
 *   P_GAMES  the games a category reads (import_sources[] of that category)
 *   P_OPTS   one game: the groups (characters: Playable Characters, NPCs: IMPORT_OFFERED), the disc image (the system's
 *            file dialog, lobby_io.c; ini import_<key>_iso), START IMPORT, DELETE, and what is imported (languages:
 *            lang.h, docs/LANGUAGES.md)
 * States: IS_MENU the page; IS_WORK the extraction's thread (import_start: g_import_progress) with a bar; IS_RESULT a
 * message and OK; IS_ASK DELETE's Yes/No. Game thread. */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/adv.h"
#include "../../decomp/include/ps2/veronica/prog/padman.h"
#include "../../decomp/include/ps2/veronica/prog/sdfunc.h"
#include "../src/host/menu_mouse.h"
#include "../src/host/lobby.h"
#include "../src/host/settings.h"
#include "../src/import/import.h"
#include "../src/host/lang.h"
#include "../src/net/biorand_dl.h"
#include <stdio.h>
#include <string.h>

void host_log(const char* fmt, ...);
extern ADV_WORK AdvWork;
void pc_ui_text(float x, float y, const char* s, float sc, unsigned int argb);
float pc_ui_text_w(const char* s, float sc);
int  pc_ui_text_wrap(float x, float y, float w, const char* s, float sc, unsigned int argb, int max_lines);
void pc_ui_rect(float x0, float y0, float x1, float y1, unsigned int argb, float z);
void pc_ui_arrow(float x, float y, int dir, unsigned int argb);
void pc_title_word_import(float cx, float y, unsigned int col, unsigned int shadow);
void pc_ui_dialog(float y0, float y1, float z, const char* l1, const char* l2, float sc2, unsigned int col2, int lines,
                  const char* b0, const char* b1, int sel, int glyphs);
int pc_ui_dialog_hit(float y1, const char* b0, const char* b1, int glyphs, float x, float y);
void pc_ui_dialog_place(const char* l1, const char* l2, float sc2, int lines, float extra, float* y0, float* y1);

enum { BTN_START = 0x800, BTN_UP = 0x1000, BTN_RIGHT = 0x2000, BTN_DOWN = 0x4000, BTN_LEFT = 0x8000 };
enum { P_CATS, P_GAMES, P_OPTS };
enum { IS_MENU, IS_ASK, IS_WORK, IS_RESULT };
enum { K_CAT, K_GAME, K_PLAYERS, K_NPCS, K_DISC, K_START, K_DELETE, K_BACK };
typedef struct { int kind, idx; } Row;

/* the categories (IMPORT_CAT_*) */
static const struct { const char* name; const char* help; } cats[] = {
    { "CHARACTERS", "Character models of other games, to play as (Character Model) and for the cutscene cast." },
    { "LANGUAGES", "The game's text in other languages, from your own disc of another release (Language)." },
};
#define N_CATS (int)(sizeof(cats) / sizeof(cats[0]))

/* (the BIORAND screen's layout and depths, biorand_menu.c) */
#define Z_DIM 0.012f
#define Z_RULE 0.02f
#define Z_STRIP 0.025f
#define Z_ROW 0.3f
#define Z_BOX 0.303f
#define Z_BAR 0.32f
#define Z_FILL 0.33f
#define ROW_X 48.0f
#define VAL_X 372.0f
#define PAGE_Y 70.0f
#define ROW_Y0 102.0f
#define ROW_H 21.0f
#define INFO_Y 314.0f
#define ROW_SC 0.55f
#define SMALL_SC 0.45f
#define BOX_X0 112.0f
#define BOX_X1 528.0f
#define WORK_TEXT 34.0f
#define WORK_BAR 30.0f

static struct {
    int open, state, page, row, sel;    /* sel: IS_ASK 0 Yes 1 No */
    int cat, src, what;                 /* the category, the game (import_sources[]), the groups to extract */
    int picking;                        /* the file dialog is open (lobby_io.c) */
    int players, npcs, have;            /* what the game's manifest lists as playable (IMPORT_OFFERED groups); a manifest is there */
    char when[64];
    char msg[320], say[192];
    int err, say_t, say_err;
    MouseTake mt;
} im;

static Row rows[8];
static int nrows;
static ImportItem items[1024];

static void add(int kind, int idx) { rows[nrows++] = (Row){ kind, idx }; }

/* a language source: how many of its languages are installed (lang.h); -1 a character source */
static int langs(void)
{
    char d[1100], e[1200];
    int n = 0;
    if (import_sources[im.src].cat != IMPORT_CAT_LANGUAGES) return -1;
    import_source_dir(im.src, d, sizeof(d));
    for (int l = 1; l < LANG_COUNT; l++) {
        snprintf(e, sizeof(e), "%s/%s", d, lang_codes[l]);
        n += lang_installed(l) && lang_dir(l) && !strcmp(lang_dir(l), e);
    }
    return n;
}
/* their names, "Fran\xC3\xA7ais, Deutsch" */
static const char* lang_list(char* t, int n)
{
    char d[1100], e[1200];
    t[0] = 0;
    import_source_dir(im.src, d, sizeof(d));
    for (int l = 1; l < LANG_COUNT; l++) {
        snprintf(e, sizeof(e), "%s/%s", d, lang_codes[l]);
        if (lang_installed(l) && lang_dir(l) && !strcmp(lang_dir(l), e)) snprintf(t + strlen(t), n - strlen(t), "%s%s", t[0] ? ", " : "", lang_names[l]);
    }
    return t;
}

static void build_rows(void)
{
    nrows = 0;
    if (im.page == P_CATS) for (int i = 0; i < N_CATS; i++) add(K_CAT, i);
    else if (im.page == P_GAMES) { for (int i = 0; i < IMPORT_SOURCES; i++) if (import_sources[i].cat == im.cat) add(K_GAME, i); }
    else {
        if (langs() < 0 && (IMPORT_OFFERED & IMPORT_PLAYERS)) add(K_PLAYERS, 0);
        if (langs() < 0 && (IMPORT_OFFERED & IMPORT_NPCS)) add(K_NPCS, 0);
        add(K_DISC, 0);
        add(K_START, 0);
        add(K_DELETE, 0);
    }
    add(K_BACK, 0);
    if (im.row >= nrows) im.row = nrows - 1;
}

/* the game's manifest: how many of each group can be played */
static void look(void)
{
    int n = import_results(im.src, items, 1024, im.when, sizeof(im.when));
    im.players = im.npcs = 0;
    for (int i = 0; i < n; i++) {
        if (!items[i].player) continue;
        if (items[i].group == IMPORT_GROUP_PLAYERS && (IMPORT_OFFERED & IMPORT_PLAYERS)) im.players++;
        if (items[i].group == IMPORT_GROUP_NPCS && (IMPORT_OFFERED & IMPORT_NPCS)) im.npcs++;
    }
    im.have = n > 0;
}

static const char* counts(char* t, int n)
{
    if (im.players && im.npcs) snprintf(t, n, tr("%d playable characters and %d NPCs can be played."), im.players, im.npcs);
    else if (im.players || im.npcs) snprintf(t, n, tr("%d %s can be played."), im.players + im.npcs, im.players ? "playable characters" : "NPCs");
    else snprintf(t, n, tr("No models can be played."));
    return t;
}

static void say(const char* m, int err) { snprintf(im.say, sizeof(im.say), "%s", m); im.say_t = 240; im.say_err = err; }
static void refuse(const char* why) { CallSystemSe(0, 1); say(why, 1); }
static void result(const char* m, int err)   /* (the importer's texts may name "File #2": the font's '#' is no '#') */
{
    int n = 0;
    for (; *m && n < (int)sizeof(im.msg) - 1; m++) if (*m != '#') im.msg[n++] = *m;
    im.msg[n] = 0;
    im.err = err;
    im.state = IS_RESULT;
}
static int running(void) { return __atomic_load_n(&g_import_progress.running, __ATOMIC_ACQUIRE); }

/* a game's title in the game's font, whose '#' is the biohazard sign: without it ("Outbreak File 2") */
static const char* title_of(int src)
{
    static char t[2][96];
    static int k;
    char* o = t[k ^= 1];
    int n = 0;
    for (const char* s = import_sources[src].title; *s && n < 95; s++) if (*s != '#') o[n++] = *s;
    o[n] = 0;
    return o;
}

static const char* disc_name(void)
{
    const char *p = g_import.iso[im.src], *s;
    if (!p[0]) return "Not chosen";
    for (s = p; *s; s++) if (*s == '/' || *s == '\\') p = s + 1;
    return p;
}

/* why a row cannot be used now, or NULL */
static const char* row_why(const Row* r)
{
    switch (r->kind) {
    case K_PLAYERS: case K_NPCS: case K_DISC: return running() ? "An import is running." : NULL;
    case K_START:
        if (running()) return "An import is running.";
        if (!g_import.iso[im.src][0]) return "Choose the disc image first.";
        if (langs() < 0 && !(im.what & IMPORT_OFFERED)) return "Turn on Playable Characters or NPCs first.";
        return NULL;
    case K_DELETE:
        if (running()) return "An import is running.";
        return import_present(im.src) ? NULL : "Nothing is imported.";
    }
    return NULL;
}

static void set_page(int p)
{
    im.page = p;
    im.row = 0;
    im.say_t = 0;
    if (p == P_OPTS) look();
}

static void back(void)
{
    CallSystemSe(0, 0);
    set_page(im.page == P_OPTS ? P_GAMES : P_CATS);
    if (im.page == P_GAMES) for (int i = 0; i < im.src; i++) im.row += import_sources[i].cat == im.cat;   /* (on the game's row) */
    if (im.page == P_CATS) im.row = im.cat;
}

static void toggle(const Row* r)
{
    int bit = r->kind == K_PLAYERS ? IMPORT_PLAYERS : IMPORT_NPCS;
    if (row_why(r)) { refuse(row_why(r)); return; }
    im.what ^= bit;
    CallSystemSe(0, 2);
}

static void start(void)
{
    if (!import_start(im.src, im.what & IMPORT_OFFERED)) { result("An import is already running.", 1); return; }
    im.state = IS_WORK;
    host_log("import menu: %s, groups %d, from %s\n", import_sources[im.src].key, im.what & IMPORT_OFFERED, g_import.iso[im.src]);
}

/* DELETE's Yes: the game's folder, then the list asked again (it only grows while the game runs: actors.h) */
static void delete_imports(void)
{
    char d[1100];
    import_source_dir(im.src, d, sizeof(d));
    if (!brdl_rm_tree(d)) { result("Some imported files could not be deleted. Check the folder's access rights.", 1); return; }
    host_log("import menu: deleted %s\n", d);
    look();
    if (langs() >= 0) {   /* (the main thread lists the languages again: host.c) */
        __atomic_store_n(&g_import_reload, 1, __ATOMIC_RELEASE);
        result("Imported languages deleted. The game's text is English from the next screen.", 0);
        return;
    }
    result("Imported models deleted. They stay in the model lists until you restart the game; choosing one there "
           "shows the character you play instead.", 0);
}

/* IS_WORK: the thread's end */
static void work_end(void)
{
    char t[160];
    if (running()) return;
    look();
    __atomic_store_n(&g_import_reload, 1, __ATOMIC_RELEASE);   /* the new models into Character Model (main thread, host.c) */
    host_log("import menu: ended %d: %s\n", g_import_progress.result, g_import_progress.phase);
    if (g_import_progress.result < 0) { result(g_import_progress.phase, 1); return; }
    if (langs() >= 0) {   /* (lang_scan runs on the main thread before the menu reads the list again) */
        snprintf(t, sizeof(t), "%s", g_import_progress.result > 0 ? "Some languages could not be imported (see recvx.log)."
                 : "The languages are installed. Choose one under Language on the GAMEPLAY page of ENHANCED SETTINGS.");
        result(t, g_import_progress.result > 0);
        return;
    }
    counts(t, sizeof(t));
    if (g_import_progress.result > 0) snprintf(t + strlen(t), sizeof(t) - strlen(t), tr(" %d models failed (see recvx.log)."), g_import_progress.result);
    result(t, 0);
}

/* confirm on a row: 1 leave the screen */
static int activate(const Row* r)
{
    const char* why = row_why(r);
    switch (r->kind) {
    case K_CAT: CallSystemSe(0, 3); im.cat = r->idx; set_page(P_GAMES); return 0;   /* (cats[] in IMPORT_CAT_* order) */
    case K_GAME: CallSystemSe(0, 3); im.src = r->idx; set_page(P_OPTS); return 0;
    case K_PLAYERS: case K_NPCS: toggle(r); return 0;
    case K_DISC:
        if (why) { refuse(why); return 0; }
        snprintf(g_lobby_io.file, sizeof(g_lobby_io.file), "%s", g_import.iso[im.src]);
        __atomic_store_n(&g_lobby_io.file_req, 1, __ATOMIC_RELEASE);   /* the main thread opens the dialog */
        im.picking = 1;
        CallSystemSe(0, 3);
        return 0;
    case K_START:
        if (why) { refuse(why); return 0; }
        CallSystemSe(0, 3);
        start();
        return 0;
    case K_DELETE:
        if (why) { refuse(why); return 0; }
        CallSystemSe(0, 3);
        im.state = IS_ASK;
        im.sel = 1;   /* No first */
        return 0;
    default:
        if (im.page == P_CATS) { CallSystemSe(0, 0); return 1; }
        back();
        return 0;
    }
}

static float row_y(int i) { return ROW_Y0 + i * ROW_H; }
static int row_at(float x, float y)
{
    if (x < 32 || x >= 608) return -1;
    for (int i = 0; i < nrows; i++)
        if (y >= row_y(i) - 3 && y < row_y(i) + ROW_H - 3) return i;
    return -1;
}

static const char* ask_text(void)
{
    static char t[240];
    char d[1100];
    import_source_dir(im.src, d, sizeof(d));
    snprintf(t, sizeof(t), langs() >= 0 ? tr("Delete the languages imported from %s? This deletes the folder %s. You can import them again from your disc.")
             : tr("Delete the models imported from %s? This deletes the folder %s. You can import them again from your disc."), title_of(im.src), d);
    return t;
}
static const char* result_head(void) { return im.err ? "IMPORT - ERROR" : "IMPORT"; }
static void dlg_box(float* y0, float* y1)
{
    if (im.state == IS_ASK) pc_ui_dialog_place(NULL, ask_text(), 0.55f, 5, 0, y0, y1);
    else if (im.state == IS_RESULT) pc_ui_dialog_place(result_head(), im.msg, 0.55f, 4, 0, y0, y1);
    else pc_ui_dialog_place("", NULL, 0.55f, 0, WORK_TEXT + WORK_BAR, y0, y1);
}

/* 1 leave */
static int input(void)
{
    ADV_WORK* ap = &AdvWork;
    unsigned int press = Pad[ap->PortId].press, rept = Pad[ap->PortId].Rept;
    unsigned int ok = AdvGetOkButton() | BTN_START, cancel = AdvGetCancelButton();
    MouseTake* m = &im.mt;
    int hit;
    float y0, y1;
    menu_mouse_take(m);
    if (press || rept || m->left || m->right || m->moved || m->wheel || im.state == IS_WORK) StopAdvScreenSaver(0);
    if (im.state == IS_WORK) return 0;   /* (the extraction cannot be cancelled) */
    if (im.state == IS_MENU) {
        const Row* r;
        hit = row_at(m->x, m->y);
        if (hit >= 0 && (m->moved || m->left) && hit != im.row) { im.row = hit; im.say_t = 0; CallSystemSe(0, 2); }
        if (m->wheel) { im.row = ((im.row - m->wheel) % nrows + nrows) % nrows; CallSystemSe(0, 2); return 0; }
        r = &rows[im.row];
        if (m->left && hit >= 0) {
            if ((r->kind == K_PLAYERS || r->kind == K_NPCS) && m->x >= VAL_X - 10) { toggle(r); return 0; }
            return activate(r);
        }
        if (m->right || (press & cancel)) {
            if (im.page == P_CATS) { CallSystemSe(0, 0); return 1; }
            back();
            return 0;
        }
        if (rept & BTN_UP) { im.row = (im.row + nrows - 1) % nrows; im.say_t = 0; CallSystemSe(0, 2); }
        else if (rept & BTN_DOWN) { im.row = (im.row + 1) % nrows; im.say_t = 0; CallSystemSe(0, 2); }
        else if ((rept & (BTN_LEFT | BTN_RIGHT)) && (r->kind == K_PLAYERS || r->kind == K_NPCS)) toggle(r);
        else if (press & ok) return activate(r);
        return 0;
    }
    dlg_box(&y0, &y1);
    if (im.state == IS_ASK) {
        hit = pc_ui_dialog_hit(y1, "YES", "NO", 0, m->x, m->y);
        if (hit >= 0 && (m->moved || m->left) && hit != im.sel) { im.sel = hit; CallSystemSe(0, 2); }
        if ((m->left && hit >= 0) || (press & ok)) {
            CallSystemSe(0, im.sel ? 0 : 3);
            if (im.sel) im.state = IS_MENU; else delete_imports();
        } else if (m->right || (press & cancel)) { CallSystemSe(0, 0); im.state = IS_MENU; }
        else if (rept & (BTN_LEFT | BTN_RIGHT | BTN_UP | BTN_DOWN)) { im.sel ^= 1; CallSystemSe(0, 2); }
        return 0;
    }
    hit = pc_ui_dialog_hit(y1, "OK", NULL, 0, m->x, m->y);   /* IS_RESULT */
    if ((m->left && hit >= 0) || m->right || (press & (ok | cancel))) { CallSystemSe(0, 0); im.state = IS_MENU; }
    return 0;
}

/* ---------------------------------------------------------------- drawing */
static const char* row_help(const Row* r)
{
    switch (r->kind) {
    case K_CAT: return cats[r->idx].help;
    case K_GAME: return import_sources[r->idx].cat == IMPORT_CAT_LANGUAGES ? "Import the languages of your own disc image of this release."
                                                                       : "Import from your own disc image of this game.";
    case K_PLAYERS: return "The playable characters and their costumes.";
    case K_NPCS: return "The other people of the game. Some have no names; they are listed by their file.";
    case K_DISC: return "Choose your disc image (.iso) in a file dialog.";
    case K_START: return langs() >= 0 ? "Import every language of the disc image. This takes a few seconds."
                                       : "Import the chosen groups from the disc image. This takes a few seconds to a minute.";
    case K_DELETE: return langs() >= 0 ? "Delete the imported languages from the mods folder." : "Delete the imported models of this game from the mods folder.";
    default: return im.page == P_CATS ? "Back to the title menu (also cancel or a right click)." : "Back (also cancel or a right click).";
    }
}

static void draw_row(int i)
{
    const Row* r = &rows[i];
    int sel = im.state == IS_MENU && i == im.row, live = !row_why(r);
    float y = row_y(i);
    unsigned int col = !live ? (sel ? 0xFF909090 : 0xFF686868) : sel ? 0xFFFFFFFF : 0xFFB0B0B0;
    static const char* const names[] = { "", "", "Playable Characters", "NPCs", "Disc Image", "START IMPORT", "DELETE IMPORTED MODELS", "BACK" };
    const char* label = r->kind == K_DELETE && langs() >= 0 ? "DELETE IMPORTED LANGUAGES" : names[r->kind];
    const char* v = NULL;
    if (sel) pc_ui_rect(ROW_X - 8, y - 3, 600, y + ROW_H - 4, 0x50FFFFFF, Z_ROW);
    switch (r->kind) {
    case K_CAT: pc_ui_text(ROW_X, y, cats[r->idx].name, ROW_SC, col); return;
    case K_GAME: pc_ui_text(ROW_X, y, title_of(r->idx), ROW_SC, col); return;
    case K_PLAYERS: v = (im.what & IMPORT_PLAYERS) ? "On" : "Off"; break;
    case K_NPCS: v = (im.what & IMPORT_NPCS) ? "On" : "Off"; break;
    case K_DISC: v = disc_name(); break;
    }
    pc_ui_text(ROW_X, y, tr(label), ROW_SC, col);
    if (r->kind == K_DISC) {   /* the file's name, its end cut when it is long */
        char c[256];
        int n = snprintf(c, sizeof(c), "%s", v);
        while (n > 4 && pc_ui_text_w(c, 0.5f) > 600 - VAL_X) { n--; strcpy(c + n - 3, "..."); c[n] = 0; }
        pc_ui_text(VAL_X, y + 1, c, 0.5f, col);
    } else if (v) {
        float w = pc_ui_text_w(v, ROW_SC);
        if (live) { pc_ui_arrow(VAL_X, y + 9, -1, col); pc_ui_arrow(VAL_X + 14 + w + 12, y + 9, 1, col); }
        pc_ui_text(VAL_X + 12, y, v, ROW_SC, col);
    }
}

static void draw_page(void)
{
    static const char* const heads[] = { "IMPORT FROM OTHER GAMES", NULL, NULL };
    const char* head = im.page == P_GAMES ? tr(cats[im.cat].name) : heads[im.page] ? tr(heads[im.page]) : title_of(im.src);
    char t[320];
    unsigned int col = 0xFFA0D0FF;
    pc_ui_text(320 - pc_ui_text_w(head, 0.72f) / 2, PAGE_Y - 2, head, 0.72f, 0xFFE8E0C0);
    if (im.state != IS_MENU) return;   /* (text is drawn over any box: a dialog shows the heading only) */
    pc_ui_rect(40, 94.5f, 600, 95.5f, 0x60FFFFFF, Z_RULE);
    for (int i = 0; i < nrows; i++) draw_row(i);
    pc_ui_rect(40, INFO_Y - 4.5f, 600, INFO_Y - 3.5f, 0x60FFFFFF, Z_RULE);
    if (im.page == P_OPTS && langs() >= 0) {
        char c[160];
        snprintf(t, sizeof(t), tr("Needs a clean European disc image of %s: %s."), "Resident Evil Code: Veronica X", import_sources[im.src].serial);
        pc_ui_text(ROW_X - 4, INFO_Y, tr("DISC"), SMALL_SC, 0xFFFFE080);
        pc_ui_text(ROW_X - 4, INFO_Y + 16, t, SMALL_SC, 0xFFFFFFFF);
        pc_ui_text(ROW_X - 4, INFO_Y + 36, tr("IMPORTED"), SMALL_SC, 0xFFFFE080);
        if (langs() > 0) snprintf(t, sizeof(t), tr("Installed: %s."), lang_list(c, sizeof(c)));
        else snprintf(t, sizeof(t), "%s", tr("Nothing imported yet."));
        pc_ui_text(ROW_X - 4, INFO_Y + 52, t, SMALL_SC, 0xFFFFFFFF);
    } else if (im.page == P_OPTS) {
        snprintf(t, sizeof(t), tr("Needs a clean US disc image of %s: %s."), title_of(im.src), import_sources[im.src].serial);
        pc_ui_text(ROW_X - 4, INFO_Y, tr("DISC"), SMALL_SC, 0xFFFFE080);
        pc_ui_text(ROW_X - 4, INFO_Y + 16, t, SMALL_SC, 0xFFFFFFFF);
        pc_ui_text(ROW_X - 4, INFO_Y + 36, tr("IMPORTED"), SMALL_SC, 0xFFFFE080);
        if (im.have) {
            char c[96];
            snprintf(t, sizeof(t), "%s%s%s", counts(c, sizeof(c)), im.when[0] ? tr(" On ") : "", im.when);
        } else snprintf(t, sizeof(t), tr("Nothing imported yet."));
        pc_ui_text(ROW_X - 4, INFO_Y + 52, t, SMALL_SC, 0xFFFFFFFF);
    } else pc_ui_text_wrap(ROW_X - 4, INFO_Y, 552, "Import content from your own discs of other games. It is saved to the mods folder; "
                           "nothing is downloaded.", SMALL_SC, 0xFFC0C0C0, 3);
    if (im.say_t > 0) { snprintf(t, sizeof(t), "%s", im.say); col = im.say_err ? 0xFFFFC060 : 0xFFFFE080; }
    else {
        const char* why = row_why(&rows[im.row]);
        snprintf(t, sizeof(t), "%s%s%s", why ? tr(why) : "", why ? " " : "", tr(row_help(&rows[im.row])));
        if (why) col = 0xFFFFC060;
    }
    pc_ui_text_wrap(24, 390, 592, t, 0.5f, col, 5);
}

static void draw_work(void)
{
    ImportProgress* pg = &g_import_progress;
    char line[48] = "";
    float y0, y1, f = pg->total > 0 ? (float)pg->done / pg->total : 0, x, yb;
    dlg_box(&y0, &y1);
    pc_ui_dialog(y0, y1, Z_BOX, langs() >= 0 ? "IMPORTING LANGUAGES" : "IMPORTING CHARACTERS", pg->phase[0] ? pg->phase : "Reading the disc...",
                 0.55f, 0xFFC0C0C0, 2, "", NULL, -1, 0);
    if (f > 1) f = 1;
    if (pg->total > 0) snprintf(line, sizeof(line), "%d / %d", pg->done, pg->total);
    x = BOX_X0 + 20 + (BOX_X1 - BOX_X0 - 40) * f;   /* as the BIORAND screen's bar: at half pixels */
    yb = y0 + 54 + WORK_TEXT + 2.5f;
    if (f > 0) pc_ui_rect(BOX_X0 + 20, yb, x, yb + 12, 0xFFFFE080, Z_FILL);
    if (f < 1) pc_ui_rect(x, yb, BOX_X1 - 20, yb + 12, 0xFF303038, Z_BAR);
    pc_ui_text(BOX_X1 - 20 - pc_ui_text_w(line, 0.5f), yb + 15.5f, line, 0.5f, 0xFFC8C8C8);
}

void pc_import_menu_close(void)
{
    if (!im.open) return;
    im.open = 0;
    host_log("import menu: closed\n");   /* (a running extraction goes on; F9 and the next visit show it) */
}

/* Adv_BioCvTitle mode 30 while the IMPORT screen is up (lobby_glue.c pc_lobby): 1 back to the title menu */
int pc_import_menu(void)
{
    int r = 0;
    if (!im.open) {
        memset(&im, 0, sizeof(im));
        im.open = 1;
        im.what = IMPORT_OFFERED;
        if (running()) { im.page = P_OPTS; im.state = IS_WORK; look(); }
        host_log("import menu: open\n");
    }
    if (im.say_t > 0) im.say_t--;
    if (im.picking) {   /* the file dialog's answer (lobby_io.c) */
        int f = __atomic_load_n(&g_lobby_io.file_req, __ATOMIC_ACQUIRE);
        if (f >= 3) {
            if (f == 3) {
                snprintf(g_import.iso[im.src], sizeof(g_import.iso[im.src]), "%s", g_lobby_io.file);
                settings_save_later();
                host_log("import menu: disc image %s\n", g_import.iso[im.src]);
            }
            g_lobby_io.file_req = 0;
            im.picking = 0;
        }
    }
    if (im.state == IS_WORK) work_end();
    build_rows();
    if (!im.picking) r = input();
    build_rows();
    AdvEasySetTextureList(0);
    AdvDwawOnePictureEx(0, 0xFFFFFFFF);   /* the title picture, darkened, as the lobby */
    pc_ui_rect(0, 0, 640, 480, 0xB0000000, Z_DIM);
    pc_ui_rect(24, 64, 616, 384, 0x70000000, Z_DIM + 0.0005f);
    pc_title_word_import(320.0f, 16.0f, 0xFFFFFFFF, 192);
    pc_ui_rect(0, 384, 640, 480, 0x60000000, Z_STRIP);
    draw_page();
    if (im.state == IS_WORK) draw_work();
    else if (im.state != IS_MENU) {
        float y0, y1;
        dlg_box(&y0, &y1);
        if (im.state == IS_ASK) pc_ui_dialog(y0, y1, Z_BOX, NULL, ask_text(), 0.55f, 0xFFFFFFFF, 5, "YES", "NO", im.sel, 0);
        else pc_ui_dialog(y0, y1, Z_BOX, result_head(), im.msg, 0.55f, im.err ? 0xFFFFC060 : 0xFFC0C0C0, 4, "OK", NULL, 0, 0);
    }
    AdvEasySetTextureList(0);   /* the title's texture list again (the font set its own) */
    if (r) pc_import_menu_close();
    return r;
}
