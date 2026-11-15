/* The new menu's row screen (docs/MENUS.md, menu_layout.h): the category list on the left, the rows on the
 * right, help and a value preview below, a button bar that follows the device used last. One screen for SETTINGS, GAME
 * SETUP and CHEATS AND TOOLS; the rows come from the layout table (pc/src/host/menu_layout.c) over the settings table
 * (settings.c), so it stores no values. It draws no background or title: its host does (pause_glue.c in the game's
 * OPTION task; title mode 30 later), so any category can be shown in either.
 *
 * Input, the same everywhere: the cursor is always on the rows; Up/Down choose, Left/Right change, Confirm opens or runs,
 * Cancel (or Esc, a right click) goes back, L1/R1 (Q/W, a click on the list) switch the category, holding Triangle
 * (Delete) puts the row back to its default. A gold dot marks a row that differs from its default; each screen
 * remembers its category and row. Sub-pages (Advanced, the binding pages, the lists, a shortcut's home) replace the
 * rows and the breadcrumb grows. Right to left (Arabic) the whole layout is mirrored (mx).
 * Game thread. */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/adv.h"
#include "../../decomp/include/ps2/veronica/prog/padman.h"
#include "../../decomp/include/ps2/veronica/prog/sdfunc.h"
#include "../../decomp/include/ps2/veronica/prog/sdc.h"
#include "../../decomp/include/ps2/veronica/prog/vibman.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_NaDraw.h"
#include "../src/host/settings.h"
#include "../src/host/menu_layout.h"
#include "../src/host/bindings.h"
#include "../src/host/menu_mouse.h"
#include "../src/host/lang.h"
#include "../src/host/lobby.h"
#include "../src/host/enemies.h"
#include "../src/host/cast.h"
#include "../src/host/actors.h"
#include "../src/host/coop.h"
#include "../src/host/biorand_opts.h"
#include "../src/host/textgrid.h"
#include "../src/net/net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <ctype.h>

extern ADV_WORK AdvWork;
extern OPTION OptionDef[];
void host_log(const char* fmt, ...);
void pc_ui_text(float x, float y, const char* s, float sc, unsigned int argb);
float pc_ui_text_w(const char* s, float sc);
int pc_ui_text_wrap(float x, float y, float w, const char* s, float sc, unsigned int argb, int max_lines);
int pc_ui_text_wrap_r(float x, float y, float w, const char* s, float sc, unsigned int argb, int max_lines);
void pc_ui_rect(float x0, float y0, float x1, float y1, unsigned int argb, float z);
void pc_ui_arrow(float x, float y, int dir, unsigned int argb);
void pc_ui_dialog_place(const char* l1, const char* l2, float sc2, int lines, float extra, float* y0, float* y1);
void pc_ui_dialog(float y0, float y1, float z, const char* l1, const char* l2, float sc2, unsigned int col2, int lines,
                  const char* b0, const char* b1, int sel, int glyphs);
int pc_ui_dialog_hit(float y1, const char* b0, const char* b1, int glyphs, float x, float y);
int bind_any_down(void);
int pad_last_pad(void);
void cast_list_open(int appear, float x0, float x1, float y0, float h, float sc, int rows);
int  cast_list_input(const MouseTake* m, unsigned int press, unsigned int rept, unsigned int ok, unsigned int cancel);
void cast_list_draw(void);
const char* cast_list_help(void);
const char* cast_list_summary(int appear, const unsigned char* off, int imports, char* buf, int n);
void rate_list_open(int ro, float x0, float x1, float y0, float h, float sc, int rows);
int  rate_list_input(const MouseTake* m, unsigned int press, unsigned int rept, unsigned int ok, unsigned int cancel);
void rate_list_draw(void);
const char* rate_list_help(void);
const char* rate_list_summary(const unsigned char* rate, char* buf, int n);
void give_list_open(float x0, float x1, float y0, float h, int rows);   /* give_list.c: PAUSE > CHEATS AND TOOLS > Give Item */
int  give_list_input(const MouseTake* m, unsigned int press, unsigned int rept, unsigned int ok, unsigned int cancel);
void give_list_draw(void);
const char* give_list_help(void);
void give_list_release(void);
uint64_t SDL_GetTicks(void);

enum { P_UP = 0x1000, P_RIGHT = 0x2000, P_DOWN = 0x4000, P_LEFT = 0x8000, P_START = 0x800, P_SELECT = 0x100, P_L1 = 0x4, P_R1 = 0x8,
       P_TRI = 0x10, P_CIRCLE = 0x20, P_CROSS = 0x40, P_SQUARE = 0x80 };   /* Pad[] bits */
enum { SC_F = 9, SC_ESC = 41, SC_SLASH = 56, SC_HOME = 74, SC_PGUP = 75, SC_DEL = 76, SC_END = 77, SC_PGDN = 78, SC_LCTRL = 224, SC_RCTRL = 228 };   /* SDL scancodes (bind_keys) */

/* ---------------------------------------------------------------- layout (640 x 480) */
#define CAT_X0 20.0f                    /* the category list */
#define CAT_X1 148.0f
#define CAT_H 21.0f
#define ROWS_X0 158.0f                  /* the rows */
#define ROWS_X1 618.0f
#define X_LABEL 172.0f
#define X_VALUE 500.0f                  /* a value between arrows, at most VALUE_W */
#define VALUE_W 104.0f
#define Y0 68.0f
#define ROW_H 20.0f
#define VIS 16
#define Y_HELP 392.0f
#define X_PREVIEW 412.0f
#define Y_BAR 458.0f
#define SC_ROW 0.55f
#define SC_CAT 0.52f
#define Z_PANEL 0.02f
#define Z_HI 0.3f
#define C_ROW 0xFFA0A09Bu               /* the proposal's colours (from today's screens) */
#define C_GOLD 0xFFD6B668u
#define C_HELP 0xFFC8C8C2u
#define C_LOCK 0xFFFFB050u
#define C_DIM 0xFF686868u

/* ---------------------------------------------------------------- rows */
enum { R_ITEM, R_GROUP, R_BIND, R_BIND_RESET, R_RATES, R_CUSTOM, R_RESET_CAT, R_INFO };
typedef struct Row { int kind; const MenuRow* m; int set; int idx; } Row;   /* idx: R_GROUP's text index, R_BIND's bind row, R_CUSTOM's list
    (cast_list.c: 0 Story Character Looks, 1 Your Random Looks, 2 Monster Looks), R_INFO's line */
#define MAX_ROWS 96
static Row rows[MAX_ROWS];
static int nrows;

#define DEPTH 4
static struct {
    int screen;                       /* the screen opened (MS_*) */
    const char* root;                 /* the breadcrumb's first word */
    int cat;                          /* the category shown */
    int stack[DEPTH], depth;          /* the categories under a sub-page, to go back to */
    int sel[MC_COUNT], top[MC_COUNT]; /* per category: the row (remembered while the game runs) */
    int last[MS_COUNT];               /* per screen: its last top category (-1 none yet) */
    int capture, wait_release;        /* a binding waiting for a key: its row + 1 */
    int slot;                         /* the binding slot chosen */
    int list;                         /* 1 the cast's Custom list, 2 the enemy rates, 3 Give Item's items */
    int confirm, confirm_yes;         /* RESET CATEGORY's question */
    int hold;                         /* hold-to-default: ms the button went down, 0 up */
    int held_done;
    char msg[160]; int msg_t;
    unsigned char pending[256];       /* actions that run when OPTION closes */
    int any_pending;
    int window;                       /* an F-key window to open once OPTION closes */
    int pad;                          /* the button bar shows the controller's buttons */
    unsigned char keys_prev[80];      /* Esc, Delete, Page Up/Down, Home, End: edges */
    int ok, cancel, def;              /* this frame's buttons */
    const MenuRow* go;                /* ui_rows_goto: the row to put the cursor on when the rows are built */
    int picker;                       /* the host has the 3D look picker (UR_PICK) */
    const Setting* pick;              /* UR_PICK's row */
    const MenuRow* edit;              /* a text typed on the character grid (text_grid.c): a seed's row, Player Name's, Co-op Server's */
    int only;                         /* ui_rows_only: the one category listed, -1 all */
    int imp;                          /* the IMPORT screen is shown in place (import_menu.c; Import Languages, ...) */
    int keep, keep_old, keep_sel;     /* "Keep these settings?" after a display change: its row + 1, the value before, REVERT chosen */
    uint64_t keep_t;
    int search, typing;               /* SETTINGS' search (Ctrl+F, /): on; the query is being typed */
    char query[32];
    int search_sel, search_top;       /* the category's row before the search */
} st = { .last = { -1, -1, -1, -1 }, .only = -1 };

void import_menu_open(int cat, const char* root);   /* import_menu.c: the IMPORT screen on category cat (MC_IMPORT_*) */
int  import_menu_run(int port, int flag);           /* ... per frame, drawn on the host's background: 1 left */
void import_menu_shut(void);

static MouseTake mt;
static int rtl;
static float mx(float x, float w) { return rtl ? 640 - x - w : x; }
static void text_m(float x, float y, const char* s, float sc, unsigned int col) { pc_ui_text(mx(x, pc_ui_text_w(s, sc)), y, s, sc, col); }
static void rect_m(float x0, float y0, float x1, float y1, unsigned int col, float z) { pc_ui_rect(mx(x0, x1 - x0), y0, mx(x0, x1 - x0) + x1 - x0, y1, col, z); }
static unsigned int pulse(void) { return ((unsigned int)AdvWork.FlushCount2 + 255) << 24 | 0xFFFFFF; }
static void text_fit(float x, float y, const char* s, float sc, float w, unsigned int col)
{
    float tw = pc_ui_text_w(s, sc), k = tw > w ? w / tw : 1.0f;
    pc_ui_text(mx(x, tw * k), y + (1.0f - k) * 9.0f, s, sc * k, col);
}
static void say(const char* m) { snprintf(st.msg, sizeof(st.msg), "%s", tr(m)); st.msg_t = 150; }

static int setting_of(const MenuRow* m)   /* cached menu_setting(m->ref) */
{
    static short map[512];
    static int done;
    if (!done) { for (int i = 0; i < menu_rows_n && i < 512; i++) map[i] = (short)menu_setting(menu_rows[i].ref); done = 1; }
    return map[m - menu_rows];
}
static const Setting* row_set(const Row* r) { return r->kind == R_ITEM && r->set >= 0 ? &g_settings[r->set] : NULL; }
static int top_cat(int c) { while (menu_cats[c].parent >= 0) c = menu_cats[c].parent; return c; }
static int is_bind_cat(int c) { return c == MC_BIND_KB || c == MC_BIND_PAD; }
static int heading(const Row* r) { return r->kind == R_GROUP || r->kind == R_INFO; }   /* (never selected) */
static int in_setup(const MenuRow* m) { return m && menu_cats[m->cat].screen == MS_SETUP; }   /* GAME SETUP's rows: setup_glue.c has a say */
static int virt(const MenuRow* m) { return m->kind == ML_VAR || m->kind == ML_BIO; }   /* a GAME SETUP value of its own (setup_get) */
/* why the row cannot change here and now (GAME SETUP's reasons first: read-only in game or for a co-op guest), else NULL */
static const char* lock_of(const Row* r)
{
    const Setting* s = row_set(r);
    const char* l = r->kind == R_ITEM && in_setup(r->m) ? setup_row_lock(r->m) : NULL;
    return l ? tr(l) : s ? settings_locked(s) : NULL;   /* (GAME SETUP's reasons are English: tr here) */
}
/* a seed row: Confirm types a number */
static int seed_row(const Row* r)
{
    const Setting* s = row_set(r);
    return r->kind == R_ITEM && ((r->m->kind == ML_BIO && !strcmp(r->m->ref, "Seed")) || (s && s->val == &g_enemy_opts.seed));
}

/* ---------------------------------------------------------------- the game's own OPTION rows and the merged ones */
static int vib_unit(void) { return CheckVibrationUnit(AdvWork.PortId * 6 + 2) != 0; }
static int vib_get(void) { int k = menu_setting("Vibration Strength"); return sys->vibration && k >= 0 ? settings_get(&g_settings[k]) : 0; }
static void vib_set(const Setting* s, int v)
{
    if (v > 0) settings_set(s, v);
    sys->vibration = v > 0;
    OptionDef[4].Item = v <= 0;
    SetUseVibrationUnit(v > 0);
    if (v > 0) StartVibrationEx(AdvWork.PortId, 0);   /* (as OPTION > VIBRATION ON: a buzz) */
}
static void output_set(int v) { SetSoundModeEx(v, 0); AdvWork.SoundMode = v; OptionDef[0].Item = v; }

static int preset_get(void)   /* 0 Original Look, 1 Enhanced, 2 Custom */
{
    int orig = 1, enh = 1;
    for (int i = 0; i < menu_preset_n; i++) {
        int k = menu_setting(menu_preset[i].ref), v = k >= 0 ? settings_get(&g_settings[k]) : 0, d;
        if (k < 0) continue;
        orig &= v == menu_preset[i].value;
        enh &= menu_default(k, &d) && v == d;
    }
    return orig ? 0 : enh ? 1 : 2;
}
static void preset_set(int p)
{
    for (int i = 0; i < menu_preset_n; i++) {
        int k = menu_setting(menu_preset[i].ref), d;
        if (k < 0 || settings_locked(&g_settings[k])) continue;
        if (p == 0) settings_set(&g_settings[k], menu_preset[i].value);
        else if (menu_default(k, &d)) settings_set(&g_settings[k], d);
    }
}

static const char* text_value(int which) { return which ? (g_coop.server[0] ? coop_shown(g_coop.server) : tr("None")) : g_coop.name; }

/* a table row's value as shown: its own, or on a co-op guest's GAME SETUP the host's */
static int shown_get(const Row* r)
{
    int v;
    if (in_setup(r->m) && setup_host_value(r->m, r->set, &v)) return v;
    return settings_get(row_set(r));
}

/* the value shown (buf) or NULL for none */
static const char* value_text(const Row* r, char* buf, int n)
{
    const MenuRow* m = r->m;
    const Setting* s = row_set(r);
    switch (m->kind) {
    case ML_VAR: case ML_BIO: return setup_name(m, setup_get(m), buf, n);
    case ML_SHORTCUT:
        if (setup_value(m, buf, n)) return buf;
        /* fall through */
    case ML_SET:
        if (!s || (s->flags & SET_ACTION)) return NULL;
        if (m->flags & MLF_VIBRATION) {
            int v = vib_get();
            if (v) snprintf(buf, n, "%d%%", v); else snprintf(buf, n, "%s", tr("Off"));
            return buf;
        }
        if (m->names && !s->name) { int v = shown_get(r) - s->lo; snprintf(buf, n, "%s", tr(m->names[v < 0 ? 0 : v])); return buf; }
        return settings_value_of(s, shown_get(r), buf, n);
    case ML_SCREEN:
        if (m->arg != 1) return NULL;
        snprintf(buf, n, tr("Type %c"), 'A' + (sys->keytype & 3));
        return buf;
    case ML_OUTPUT: snprintf(buf, n, "%s", tr(GetSoundMode() ? "Mono" : "Stereo")); return buf;
    case ML_PRESET: { static const char* const p[] = { "Original Look", "Enhanced", "Custom" }; snprintf(buf, n, "%s", tr(p[preset_get()])); return buf; }
    case ML_TEXT: snprintf(buf, n, "%s", text_value(m->arg)); return buf;
    case ML_PAGE: {
        int k = 0;
        if (is_bind_cat(m->arg)) return NULL;
        for (int i = 0; i < menu_rows_n; i++) k += menu_rows[i].cat == m->arg && menu_rows[i].kind != ML_RETIRED;
        snprintf(buf, n, tr(k == 1 ? "%d setting" : "%d settings"), k);
        return buf;
    }
    }
    return NULL;
}

/* changed from its default (the gold dot), and putting it back */
static int changed(const Row* r)
{
    const Setting* s = row_set(r);
    int d;
    if (r->kind != R_ITEM) return 0;
    switch (r->m->kind) {
    case ML_VAR: case ML_BIO: return setup_default(r->m, &d) && setup_get(r->m) != d;
    case ML_SET: case ML_MERGED:
        if (!s || !menu_default(r->set, &d)) return 0;
        if (r->m->flags & MLF_VIBRATION) return vib_get() != d;
        return shown_get(r) != d;
    case ML_SCREEN: return r->m->arg == 1 && (sys->keytype & 3) != 0;
    case ML_OUTPUT: return GetSoundMode() != 0;
    case ML_PRESET: return preset_get() != 1;
    }
    return 0;
}

/* 1 reset, 0 nothing to reset, -1 locked (st.msg says why) */
static int reset_row(const Row* r)
{
    const Setting* s = row_set(r);
    int d;
    if (!changed(r)) return 0;
    if (lock_of(r)) { say(lock_of(r)); return -1; }
    switch (r->m->kind) {
    case ML_VAR: case ML_BIO: if (!setup_default(r->m, &d)) return 0; setup_set(r->m, d); return 1;
    case ML_SET: case ML_MERGED:
        if (!menu_default(r->set, &d)) return 0;
        if (r->m->flags & MLF_VIBRATION) { settings_set(s, d); if (vib_unit()) vib_set(s, d); }
        else settings_set(s, d);
        return 1;
    case ML_SCREEN: sys->keytype = 0; OptionDef[1].Item = 0; return 1;
    case ML_OUTPUT: output_set(0); return 1;
    case ML_PRESET: preset_set(1); return 1;
    }
    return 0;
}

static int row_shown(const MenuRow* m, int set)
{
    int on = 1;
    if (m->kind == ML_RETIRED || m->kind == ML_MERGED) return 0;
    if (set >= 0 && g_settings[set].shown && !g_settings[set].shown() && !(m->flags & MLF_ALWAYS) && m->kind != ML_SHORTCUT) on = 0;   /* (a shortcut
        shows its home, whatever its row's table says: Your Character with Separate Looks) */
    return in_setup(m) ? setup_row_shown(m, on) : on;
}

/* ---------------------------------------------------------------- SETTINGS' search (proposal §1.5, D8): Ctrl+F or /, then type;
   every SETTINGS row whose words (its label, its old name, its help, its value) hold the text, under its category */
static int has(const char* s, const char* q)   /* q in s, any case (ASCII) */
{
    size_t n = strlen(q);
    if (!s || !n) return !n;
    for (; *s; s++) {
        size_t i = 0;
        while (i < n && s[i] && tolower((unsigned char)s[i]) == tolower((unsigned char)q[i])) i++;
        if (i == n) return 1;
    }
    return 0;
}
static int search_hit(const Row* r)
{
    const MenuRow* m = r->m;
    const Setting* s = row_set(r);
    char v[160], q[40];
    const char* p = st.query;
    int n = 0;
    while (*p == ' ') p++;
    n = snprintf(q, sizeof(q), "%s", p);
    while (n > 0 && q[n - 1] == ' ') q[--n] = 0;
    if (!q[0]) return 0;
    if (has(tr(m->label ? m->label : m->ref ? m->ref : ""), q) || has(m->ref, q) || has(m->label, q)) return 1;
    if (has(m->help, q) || (m->help && has(tr(m->help), q)) || (s && (has(s->help, q) || (s->help && has(tr(s->help), q))))) return 1;
    return value_text(r, v, sizeof(v)) && has(v, q);
}
static void search_rows(void)
{
    int last = -1;
    for (int i = 0; i < menu_rows_n && nrows < MAX_ROWS - 2; i++) {
        const MenuRow* m = &menu_rows[i];
        Row r = { R_ITEM, m, setting_of(m), 0 };
        if (menu_cats[top_cat(m->cat)].screen != MS_SETTINGS || !row_shown(m, r.set) || !search_hit(&r)) continue;
        if (m->cat != last) { rows[nrows++] = (Row){ R_GROUP, NULL, -1, 200 + m->cat }; last = m->cat; }
        rows[nrows++] = r;
    }
    if (!nrows) rows[nrows++] = (Row){ R_GROUP, NULL, -1, 199 };
}
static void search_begin(void)
{
    if (!st.search) { st.search_sel = st.sel[st.cat]; st.search_top = st.top[st.cat]; st.query[0] = 0; }
    st.search = st.typing = 1;
    st.sel[st.cat] = st.top[st.cat] = 0;
    g_lobby_io.tail = g_lobby_io.head;   /* (nothing typed before counts) */
    g_lobby_io.text_on = 1;
    CallSystemSe(0, 3);
}
static void search_end(void)
{
    if (!st.search) return;
    if (st.typing) g_lobby_io.text_on = 0;
    st.search = st.typing = 0;
    st.sel[st.cat] = st.search_sel; st.top[st.cat] = st.search_top;
}
static void search_typed(void)   /* the keys typed (lobby_io.c's ring): the text, Enter or Down to the results, Esc closes */
{
    while (st.typing && g_lobby_io.tail != __atomic_load_n(&g_lobby_io.head, __ATOMIC_ACQUIRE)) {
        int k = g_lobby_io.keys[g_lobby_io.tail & 63], n = (int)strlen(st.query);
        g_lobby_io.tail++;
        if (k == LK_ESC) { search_end(); CallSystemSe(0, 0); return; }
        if (k == LK_ENTER || k == LK_DOWN) { st.typing = 0; g_lobby_io.text_on = 0; CallSystemSe(0, 3); return; }
        if (k == LK_BACK) { if (n) st.query[n - 1] = 0; continue; }
        if (k >= 32 && k < 127 && n < (int)sizeof(st.query) - 1) { st.query[n] = (char)k; st.query[n + 1] = 0; st.sel[st.cat] = st.top[st.cat] = 0; }
    }
}

static void build_rows(void)
{
    const char* g = NULL;   /* a heading on a hidden row: over the next shown one */
    nrows = 0;
    if (st.search && !st.depth) search_rows();
    else if (is_bind_cat(st.cat)) {
        int dev = st.cat == MC_BIND_PAD, last = -1;
        for (int i = 0; i < B_COUNT && nrows < MAX_ROWS - 2; i++) {
            int r = bind_order[i], grp = bind_rows[r].group;
            if (!bind_row_shown(r, dev)) continue;
            if (grp != last) { rows[nrows++] = (Row){ R_GROUP, NULL, -1, 100 + grp }; last = grp; }
            rows[nrows++] = (Row){ R_BIND, NULL, -1, r };
        }
        rows[nrows++] = (Row){ R_BIND_RESET, NULL, -1, 0 };
    } else {
        for (int i = 0; i < menu_rows_n && nrows < MAX_ROWS - 4; i++) {
            const MenuRow* m = &menu_rows[i];
            int set = setting_of(m);
            if (m->cat != st.cat) continue;
            if (m->group) g = m->group;
            if (!row_shown(m, set)) continue;
            if (g) { rows[nrows++] = (Row){ R_GROUP, m, -1, i }; g = NULL; }
            if (m->kind == ML_INFO) {   /* its lines, not selectable */
                unsigned int col;
                int k, n = setup_info(m, -1, NULL, 0, &col);
                for (k = 0; k < n && nrows < MAX_ROWS - 4; k++) rows[nrows++] = (Row){ R_INFO, m, -1, k };
                continue;
            }
            rows[nrows++] = (Row){ R_ITEM, m, set, 0 };
            if (in_setup(m) && setup_custom_list(m) >= 0) rows[nrows++] = (Row){ R_CUSTOM, m, -1, setup_custom_list(m) };   /* "Custom": its list's row under it */
            if (m->kind == ML_SET && set >= 0 && g_settings[set].val == &g_enemy_opts.variety && g_enemy_opts.variety == 2) rows[nrows++] = (Row){ R_RATES, m, -1, 0 };
        }
        if (menu_cats[st.cat].parent < 0 && menu_cats[st.cat].screen != MS_CHEATS && st.cat != MC_OVERVIEW) rows[nrows++] = (Row){ R_RESET_CAT, NULL, -1, 0 };
    }
    if (st.go) {   /* ui_rows_goto's row */
        for (int i = 0; i < nrows; i++) if (rows[i].kind == R_ITEM && rows[i].m == st.go) { st.sel[st.cat] = i; st.top[st.cat] = 0; }
        st.go = NULL;
    }
    if (st.sel[st.cat] >= nrows) st.sel[st.cat] = nrows - 1;
    if (st.sel[st.cat] < 0) st.sel[st.cat] = 0;
    while (st.sel[st.cat] < nrows - 1 && heading(&rows[st.sel[st.cat]])) st.sel[st.cat]++;
    while (st.sel[st.cat] > 0 && heading(&rows[st.sel[st.cat]])) st.sel[st.cat]--;   /* (none after it: the one before) */
}

#define SEL st.sel[st.cat]
#define TOP st.top[st.cat]

static void scroll_to_sel(void)
{
    int want = SEL;
    if (want > 0 && heading(&rows[want - 1])) want--;   /* with its heading */
    if (want < TOP) TOP = want;
    if (SEL >= TOP + VIS) TOP = SEL - VIS + 1;
    if (TOP > nrows - VIS) TOP = nrows - VIS;
    if (TOP < 0) TOP = 0;
}

static void move_sel(int dir, int sound)
{
    for (int k = 0; k < nrows; k++) {
        SEL = (SEL + dir + nrows) % nrows;
        if (!heading(&rows[SEL])) break;
    }
    if (sound) CallSystemSe(0, 2);
}

/* ---------------------------------------------------------------- categories, sub-pages */
static int cat_listed(int c)
{
    if (st.only >= 0) return c == st.only;
    return menu_cats[c].screen == st.screen && menu_cats[c].parent < 0 && (st.screen != MS_SETUP || setup_cat_shown(c));
}

static void set_cat(int c)
{
    st.cat = c;
    st.msg_t = 0;
    if (menu_cats[c].parent < 0 && menu_cats[c].screen == st.screen && !st.depth) st.last[st.screen] = c;
    st.list = 0;
}

static void next_cat(int dir)
{
    int c = st.cat;
    if (st.depth) { CallSystemSe(0, 1); return; }   /* (a sub-page: Back first) */
    do c = (c + dir + MC_COUNT) % MC_COUNT; while (!cat_listed(c));
    set_cat(c);
    CallSystemSe(0, 2);
}

static void enter(int c)
{
    if (st.depth >= DEPTH) return;
    st.stack[st.depth++] = st.cat;
    st.cat = c;
    st.msg_t = 0;
    st.sel[c] = 0; st.top[c] = 0;
    st.list = 0;
    CallSystemSe(0, 3);
}

/* 1: left the screen */
static int back(void)
{
    CallSystemSe(0, 0);
    if (!st.depth) return 1;
    st.cat = st.stack[--st.depth];
    st.msg_t = 0;
    return 0;
}

static void edit_end(int accept);
static void keep_end(int keep);
static void search_end(void);
void ui_rows_open(int screen, const char* root)
{
    st.screen = screen;
    st.root = root;
    st.depth = 0;
    if (st.last[screen] < 0) {
        for (int c = 0; c < MC_COUNT; c++) if (cat_listed(c)) { st.last[screen] = c; break; }
    }
    if (!cat_listed(st.last[screen])) for (int c = 0; c < MC_COUNT; c++) if (cat_listed(c)) { st.last[screen] = c; break; }   /* (one not shown in this context) */
    st.cat = st.last[screen];
    st.capture = st.wait_release = st.list = st.confirm = 0;
    if (st.edit) edit_end(0);
    if (st.imp) { import_menu_shut(); st.imp = 0; }
    if (st.keep) keep_end(0);
    search_end();
    st.hold = 0; st.held_done = 1;   /* (a button still down from the screen before) */
    st.msg_t = 0;
}
int ui_rows_screen(void) { return st.screen; }
void ui_rows_goto(int cat, const MenuRow* m)
{
    st.depth = 0;
    set_cat(cat);
    st.go = m;
}
void ui_rows_picker(int on) { st.picker = on; }
const Setting* ui_rows_pick(void) { return st.pick; }
void ui_rows_say(const char* m) { say(m); }
void ui_rows_only(int cat) { st.only = cat; }
int ui_pad(void) { return st.pad; }
const char* ui_rows_value(const MenuRow* m, char* buf, int n)
{
    Row r = { R_ITEM, m, setting_of(m), 0 };
    const char* v = value_text(&r, buf, n);
    if (!v) buf[0] = 0;
    return buf;
}

/* ---------------------------------------------------------------- a text typed on the character grid (text_grid.c): a seed
   (the Enemy Randomizer Seed, BioRand's Seed), Player Name, Co-op Server */
static void edit_begin(const Row* r)
{
    char hi[16];
    int lo, top;
    st.edit = r->m;
    if (r->m->kind == ML_TEXT) {   /* (a name: edited as it is) */
        if (r->m->arg) tg_open("Co-op Server", (const char*)g_coop.server, (int)sizeof(g_coop.server) - 1, TGK_SERVER, g_coop.hide_ip && net_numeric_host((const char*)g_coop.server));
        else tg_open("Player Name", g_coop.name, COOP_NAME_MAX - 1, TGK_NAME, 0);
        return;
    }
    if (r->m->kind == ML_BIO) setup_range(r->m, &lo, &top); else top = settings_of(&g_enemy_opts.seed)->hi;
    /* a seed: a new number (empty: OK keeps the old one); as many digits as the highest, set clamps */
    tg_open(r->m->kind == ML_BIO ? "Seed" : "Enemy Randomizer Seed", "", snprintf(hi, sizeof(hi), "%d", top), TGK_DIGITS, 0);
}
static void edit_end(int accept)
{
    const char* t = tg_text();
    if (accept && st.edit->kind == ML_TEXT && st.edit->arg) snprintf(g_coop.server, sizeof(g_coop.server), "%s", t);   /* (empty: no server) */
    else if (accept && st.edit->kind == ML_TEXT) {
        while (*t == ' ') t++;
        if (*t) snprintf(g_coop.name, sizeof(g_coop.name), "%.*s", (int)sizeof(g_coop.name) - 1, t);
    } else if (accept && t[0]) {
        int v = (int)strtol(t, NULL, 10);
        if (st.edit->kind == ML_BIO) setup_set(st.edit, v);
        else settings_set(settings_of(&g_enemy_opts.seed), v);
    }
    if (accept) settings_save_later();
    tg_close();
    st.edit = NULL;
}


/* ---------------------------------------------------------------- "Keep these settings?" (proposal §3.1): a new display mode
   or fullscreen resolution goes back by itself after KEEP_MS unless KEEP is chosen (a mode the display cannot show) */
#define KEEP_MS 10000
static int display_row(const Setting* s) { return s && (!strcmp(s->label, "Display Mode") || !strcmp(s->label, "Fullscreen Resolution")); }
static void step_set(const Row* r, int dir)   /* a table row's left / right (or Confirm on a choice) */
{
    const Setting* s = row_set(r);
    int old = settings_get(s);
    settings_step(s, dir);
    if (display_row(s) && settings_get(s) != old) {
        st.keep = r->set + 1; st.keep_old = old; st.keep_sel = 1; st.keep_t = SDL_GetTicks();
        host_log("menu: %s changed, keep or revert\n", s->label);
    }
}
static void keep_end(int keep)
{
    const Setting* s = &g_settings[st.keep - 1];
    if (!keep) settings_set(s, st.keep_old);
    settings_save_later();
    host_log("menu: %s %s\n", s->label, keep ? "kept" : "reverted");
    say(keep ? "The new display settings are kept." : "The display settings went back.");
    st.keep = 0;
}
static int keep_left(void) { int64_t l = KEEP_MS - (int64_t)(SDL_GetTicks() - st.keep_t); return l < 0 ? 0 : (int)((l + 999) / 1000); }
static const char* keep_text(char* buf, int n) { snprintf(buf, n, tr("Reverting in %d s."), keep_left()); return buf; }

/* ---------------------------------------------------------------- changing and running */
static void change(const Row* r, int dir)
{
    const Setting* s = row_set(r);
    if (r->kind == R_BIND) { st.slot = (dir > 0) != rtl; CallSystemSe(0, 2); return; }
    if (r->kind != R_ITEM) return;
    switch (r->m->kind) {
    case ML_VAR: case ML_BIO:
        if (lock_of(r)) { CallSystemSe(0, 1); say(lock_of(r)); return; }
        setup_step(r->m, dir);
        CallSystemSe(0, 2);
        return;
    case ML_SET:
        if (!s || (s->flags & SET_ACTION)) return;
        if (lock_of(r)) { CallSystemSe(0, 1); say(lock_of(r)); return; }
        if (r->m->flags & MLF_VIBRATION) {
            int v = vib_get() + dir * s->step;
            if (!vib_unit()) { CallSystemSe(0, 1); say("No controller with vibration is connected."); return; }
            vib_set(s, v < 0 ? 0 : v > s->hi ? s->hi : v);
        } else step_set(r, dir);
        CallSystemSe(0, 2);
        return;
    case ML_OUTPUT: output_set(!GetSoundMode()); CallSystemSe(0, 2); return;
    case ML_PRESET: { int p = preset_get(); preset_set(p == 2 ? (dir > 0 ? 0 : 1) : !p); CallSystemSe(0, 2); return; }
    default: return;
    }
}

static void capture_end(int code)
{
    int row = st.capture - 1, dev = st.cat == MC_BIND_PAD;
    char nm[64];
    st.capture = 0;
    st.wait_release = 1;
    if (code == -2) { CallSystemSe(0, 0); say("Not changed"); return; }
    bind_set(row, dev, st.slot, code == -3 ? 0 : code);
    settings_save_later();
    CallSystemSe(0, 3);
    if (bind_swapped()) { snprintf(nm, sizeof(nm), tr("Swapped with %s"), bind_swapped()); say(nm); }
    else if (bind_combo(row, dev, st.slot)) say("Hold Aim and press this button to reload. Without Aim, it keeps its other action.");
}

/* Confirm; returns UR_* */
static int activate(const Row* r)
{
    const Setting* s = row_set(r);
    switch (r->kind) {
    case R_BIND: st.capture = r->idx + 1; bind_capture_start(st.cat == MC_BIND_PAD); CallSystemSe(0, 3); return UR_STAY;
    case R_BIND_RESET:
        bind_reset(st.cat == MC_BIND_PAD);
        settings_save_later();
        say(st.cat == MC_BIND_PAD ? "Controller bindings restored." : "Keyboard bindings restored.");
        CallSystemSe(0, 3);
        return UR_STAY;
    case R_RATES: case R_CUSTOM: {
        float y = Y0 + 16;
        if (r->kind == R_CUSTOM && lock_of(&(Row){ R_ITEM, r->m, setting_of(r->m), 0 })) { CallSystemSe(0, 1); say(lock_of(&(Row){ R_ITEM, r->m, setting_of(r->m), 0 })); return UR_STAY; }
        st.list = r->kind == R_RATES ? 2 : 1;
        if (st.list == 2) rate_list_open(0, X_LABEL - 8, ROWS_X1, y, ROW_H, SC_ROW, VIS - 2);
        else cast_list_open(r->idx, X_LABEL - 8, ROWS_X1, y, ROW_H, SC_ROW, VIS - 3);
        CallSystemSe(0, 3);
        return UR_STAY;
    }
    case R_RESET_CAT: st.confirm = 1; st.confirm_yes = 0; CallSystemSe(0, 3); return UR_STAY;
    case R_ITEM: break;
    default: return UR_STAY;
    }
    if (seed_row(r)) {
        if (lock_of(r)) { CallSystemSe(0, 1); say(lock_of(r)); }
        else edit_begin(r);
        return UR_STAY;
    }
    switch (r->m->kind) {
    case ML_PAGE: enter(r->m->arg); return UR_STAY;
    case ML_SHORTCUT: case ML_IMPORT:
        if (menu_cats[r->m->arg].screen == MS_IMPORT) {   /* EXTRA > IMPORT in place: its breadcrumb goes on from ours */
            char root[160];
            int n = snprintf(root, sizeof(root), "%s", st.root);
            for (int i = 0; i < st.depth; i++) n += snprintf(root + n, sizeof(root) - n, ">%s", menu_cats[st.stack[i]].name);
            snprintf(root + n, sizeof(root) - n, ">%s", menu_cats[st.cat].name);
            import_menu_open(r->m->arg, root);
            st.imp = 1;
            CallSystemSe(0, 3);
            return UR_STAY;
        }
        enter(r->m->arg);
        return UR_STAY;
    case ML_ACTION:
        if (lock_of(r)) { CallSystemSe(0, 1); say(lock_of(r)); return UR_STAY; }
        return setup_action(r->m);
    case ML_VAR: case ML_BIO: {
        int n;
        if (setup_choice(r->m, &n)) change(r, 1);
        else CallSystemSe(0, 1);   /* (a number: left / right) */
        return UR_STAY;
    }
    case ML_SCREEN: CallSystemSe(0, 3); return UR_LEVEL + r->m->arg;
    case ML_TEXT:   /* the character grid (the co-op server: not while a session uses it) */
        if (r->m->arg && g_coop.state != COOP_OFF) { CallSystemSe(0, 1); say("Not during a co-op session."); return UR_STAY; }
        edit_begin(r);
        return UR_STAY;
    case ML_WINDOW: st.window = r->m->arg; CallSystemSe(0, 3); return UR_CLOSE;
    case ML_GIVE: st.list = 3; give_list_open(X_LABEL - 8, ROWS_X1, Y0 + 16, 24, 11); CallSystemSe(0, 3); return UR_STAY;
    case ML_OUTPUT: case ML_PRESET: change(r, 1); return UR_STAY;
    case ML_SET:
        if (!s) return UR_STAY;
        if (settings_locked(s)) { CallSystemSe(0, 1); say(settings_locked(s)); return UR_STAY; }
        if (s->flags & SET_ACTION) {
            if (s->flags & SET_NOW) settings_act(s);
            else if (r->set < (int)sizeof(st.pending)) { st.pending[r->set] = 1; st.any_pending = 1; say(pc_in_game() ? "Runs when you resume." : "Applies when you leave this menu."); }
            CallSystemSe(0, 3);
            return UR_STAY;
        }
        if (r->m->flags & MLF_VIBRATION) { CallSystemSe(0, 1); return UR_STAY; }
        if (s->count && st.picker) { st.pick = s; CallSystemSe(0, 3); return UR_PICK; }   /* a look row: the host's 3D picker */
        if (settings_is_bool(s) || s->names || s->name) { step_set(r, 1); CallSystemSe(0, 2); }
        else CallSystemSe(0, 1);   /* numbers: left/right */
        return UR_STAY;
    }
    return UR_STAY;
}

static void reset_category(void)
{
    int n = 0, locked = 0;
    for (int i = menu_rows_n - 1; i >= 0; i--) {   /* (last first: a row another one turns on comes after it, "Turn on ... first") */
        const MenuRow* m = &menu_rows[i];
        Row r = { R_ITEM, m, setting_of(m), 0 };
        if (m->kind == ML_RETIRED || m->kind == ML_PRESET || top_cat(m->cat) != st.cat) continue;
        if (in_setup(m) && !setup_row_shown(m, 1) && m->kind != ML_MERGED) continue;   /* (GAME SETUP: what this context does not show) */
        { int k = reset_row(&r); n += k > 0; locked += k < 0; }
    }
    settings_save_later();
    if (locked) say("Some settings cannot change now and were kept.");
    else say(n ? "Category reset to defaults." : "Nothing to reset: every setting is at its default.");
}

/* ---------------------------------------------------------------- the button bar (also the pause menu's) */
#define UG_SEARCH 10   /* (the search's hint: SETTINGS only) */
typedef struct { const char* glyph; const char* word; int action; } Hint;
static Hint hints[5];
static int nhints;
static float hint_x0[5], hint_x1[5];

/* The hints' buttons as icons, drawn from geometry at the screen's resolution with the game's font (no picture files;
   generic shapes and letters). A glyph string, left to right: GL_PAD + a byte is a gamepad input by its binding code
   (bindings.h: 1 + SDL_GamepadButton, BIND_AXIS + axis * 2 + negative), or GP_DPAD_LR; positional, so its label is the
   style's (bind_pad_style: the bottom button is Xbox's A, PlayStation's cross, Switch's B, as SDL reports Switch pads);
   GL_MOUSE + a byte mouse button n; any other text is a key cap ('\t' between caps). */
#define GL_PAD '\x01'
#define GL_MOUSE '\x02'
enum { GP_SOUTH = 1, GP_EAST, GP_WEST, GP_NORTH, GP_BACK, GP_GUIDE, GP_START, GP_LSTICK, GP_RSTICK, GP_LB, GP_RB,
       GP_UP, GP_DOWN, GP_LEFT, GP_RIGHT, GP_AXIS = BIND_AXIS, GP_LT = BIND_AXIS + 8, GP_RT = BIND_AXIS + 10, GP_DPAD_LR = 0x70 };
#define C_EDGE 0xFF8C8C86u              /* an icon's rim */
#define C_BODY 0xFF2C2C2Cu
#define C_MARK 0xFFF0F0EAu              /* a d-pad arm, a stick's arrow, a mouse button */
#define Z_IC 0.45f                      /* layers: rim, body, symbol, its hole; in front of the panels (the character grid: 0.42) */
#define Z_ICD 0.002f

static void poly(const float* p, int n, unsigned int col, float z)   /* convex, n <= 40 points, as strips (njDrawPolygon: <= 21) */
{
    NJS_POLYGON_VTX v[20];
    if (n > 20) {
        float b[42];
        int h = n / 2;
        poly(p, h + 1, col, z);
        memcpy(b, p + 2 * h, sizeof(float) * 2 * (n - h));
        b[2 * (n - h)] = p[0];
        b[2 * (n - h) + 1] = p[1];
        poly(b, n - h + 1, col, z);
        return;
    }
    for (int k = 0, lo = 1, hi = n - 1; k < n; k++) {   /* 0, 1, n-1, 2, n-2, ...: a strip over a convex polygon */
        int i = !k ? 0 : k & 1 ? lo++ : hi--;
        v[k] = (NJS_POLYGON_VTX){ p[2 * i], p[2 * i + 1], z, col };
    }
    njDrawPolygon(v, n, 1);
}
static void disc(float cx, float cy, float r, unsigned int col, float z)
{
    float p[56];
    for (int i = 0; i < 28; i++) { p[2 * i] = cx + r * cosf(i * 6.2831853f / 28); p[2 * i + 1] = cy + r * sinf(i * 6.2831853f / 28); }
    poly(p, 28, col, z);
}
static void rrect(float x0, float y0, float x1, float y1, float rt, float rb, unsigned int col, float z)   /* top corners rt, bottom rb */
{
    static const float c[5] = { 0, 0.38268343f, 0.70710678f, 0.92387953f, 1 };
    const float cx[4] = { x1 - rt, x1 - rb, x0 + rb, x0 + rt }, cy[4] = { y0 + rt, y1 - rb, y1 - rb, y0 + rt }, r[4] = { rt, rb, rb, rt };
    float p[40];
    for (int q = 0, n = 0; q < 4; q++)   /* clockwise from the top right corner's top */
        for (int i = 0; i < 5; i++, n++) {
            float s = c[i], k = c[4 - i];   /* sin, cos of i * 22.5 degrees */
            float dx[4] = { s, k, -s, -k }, dy[4] = { -k, s, k, -s };
            p[2 * n] = cx[q] + r[q] * dx[q];
            p[2 * n + 1] = cy[q] + r[q] * dy[q];
        }
    poly(p, 20, col, z);
}
static void bar(float x0, float y0, float x1, float y1, float w, unsigned int col, float z)   /* a line w thick */
{
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy), nx = -dy / l * w / 2, ny = dx / l * w / 2;
    float p[8] = { x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny };
    poly(p, 4, col, z);
}
static void tri(float cx, float cy, float r, int dir, unsigned int col, float z)   /* an arrow head: dir 0 up, 1 right, 2 down, 3 left */
{
    static const float u[4][6] = { { 0, -1, 0.9f, 0.6f, -0.9f, 0.6f }, { 1, 0, -0.6f, 0.9f, -0.6f, -0.9f }, { 0, 1, -0.9f, -0.6f, 0.9f, -0.6f },
                                   { -1, 0, 0.6f, -0.9f, 0.6f, 0.9f } };
    float p[6];
    for (int i = 0; i < 6; i++) p[i] = (i & 1 ? cy : cx) + r * u[dir][i];
    poly(p, 3, col, z);
}
void pc_ui_text_flat(float x, float y, const char* s, float sc, unsigned int argb);   /* lobby_glue.c: no shadow */
static void ic_text(float cx, float y, const char* s, float sc, unsigned int col) { pc_ui_text_flat(cx - pc_ui_text_w(s, sc) / 2, y, s, sc, col); }
static void ring(float cx, float cy, float r, unsigned int body) { disc(cx, cy, r, C_EDGE, Z_IC); disc(cx, cy, r - 0.9f, body, Z_IC + Z_ICD); }

static float ic_face(int code, float x, float y, int draw)   /* A B X Y, cross circle square triangle */
{
    static const char xb[] = "ABXY", sw[] = "BAYX";
    static const unsigned int xc[] = { 0xFF5BAE3Cu, 0xFFD5443Au, 0xFF3478CFu, 0xFFE4B024u };
    int pos = code - GP_SOUTH, style = bind_pad_style();
    float cx = x + 7.5f, cy = y + 7.5f, z = Z_IC + 2 * Z_ICD;
    char l[2] = { 0, 0 };
    if (!draw) return 15;
    if (style == BPS_PS) {
        ring(cx, cy, 7.5f, 0xFF222222u);
        switch (pos) {
        case 0: bar(cx - 3.3f, cy - 3.3f, cx + 3.3f, cy + 3.3f, 1.5f, 0xFF8DB2F0u, z); bar(cx - 3.3f, cy + 3.3f, cx + 3.3f, cy - 3.3f, 1.5f, 0xFF8DB2F0u, z); break;
        case 1: disc(cx, cy, 4.0f, 0xFFF07A78u, z); disc(cx, cy, 2.6f, 0xFF222222u, z + Z_ICD); break;
        case 2: rrect(cx - 3.4f, cy - 3.4f, cx + 3.4f, cy + 3.4f, 0, 0, 0xFFE89CD0u, z); rrect(cx - 2.0f, cy - 2.0f, cx + 2.0f, cy + 2.0f, 0, 0, 0xFF222222u, z + Z_ICD); break;
        default: {
            float o[6] = { cx, cy - 4.4f, cx + 4.3f, cy + 3.0f, cx - 4.3f, cy + 3.0f }, in[6];
            for (int i = 0; i < 6; i++) in[i] = (i & 1 ? cy + 0.53f : cx) + (o[i] - (i & 1 ? cy + 0.53f : cx)) * 0.45f;
            poly(o, 3, 0xFF5CD0A8u, z);
            poly(in, 3, 0xFF222222u, z + Z_ICD);
        }
        }
        return 15;
    }
    if (style == BPS_SWITCH) { ring(cx, cy, 7.5f, C_BODY); l[0] = sw[pos]; ic_text(cx - 0.4f, y + 2.5f, l, 0.42f, 0xFFFFFFFF); return 15; }
    disc(cx, cy, 7.5f, 0xFF161616u, Z_IC);
    disc(cx, cy, 6.7f, xc[pos], Z_IC + Z_ICD);
    l[0] = xb[pos];
    ic_text(cx - 0.4f, y + 2.5f, l, 0.42f, 0xFFFFFFFFu);   /* (centred on the disc; white on all four, as on the pad) */
    return 15;
}

static float ic_shoulder(int code, float x, float y, int draw)   /* LB RB / L1 R1 / L R; LT RT / L2 R2 / ZL ZR */
{
    static const char* lab[3][4] = { { "LB", "RB", "LT", "RT" }, { "L1", "R1", "L2", "R2" }, { "L", "R", "ZL", "ZR" } };
    int trig = code == GP_LT || code == GP_RT, right = code == GP_RB || code == GP_RT;
    const char* s = lab[bind_pad_style()][trig * 2 + right];
    float w = pc_ui_text_w(s, 0.36f) + 9;
    if (w < 18) w = 18;
    if (!draw) return w;
    if (trig) { rrect(x, y, x + w, y + 15, 6.5f, 1.5f, C_EDGE, Z_IC); rrect(x + 0.9f, y + 0.9f, x + w - 0.9f, y + 14.1f, 5.6f, 0.8f, C_BODY, Z_IC + Z_ICD); }
    else { rrect(x, y + 2, x + w, y + 13.5f, 4.5f, 1.5f, C_EDGE, Z_IC); rrect(x + 0.9f, y + 2.9f, x + w - 0.9f, y + 12.6f, 3.6f, 0.8f, C_BODY, Z_IC + Z_ICD); }
    ic_text(x + w / 2, y + (trig ? 2.6f : 2.2f), s, 0.36f, 0xFFFFFFFF);
    return w;
}

static float ic_menu(int code, float x, float y, int draw)   /* View / Menu, Share (Create) / Options, minus / plus */
{
    int style = bind_pad_style(), start = code == GP_START;
    float z = Z_IC + 2 * Z_ICD, cx, cy = y + 7.5f;
    if (style == BPS_PS) {
        if (!draw) return 11;
        cx = x + 5.5f;
        rrect(x, y + 0.5f, x + 11, y + 14.5f, 3.5f, 3.5f, C_EDGE, Z_IC);
        rrect(x + 0.9f, y + 1.4f, x + 10.1f, y + 13.6f, 2.6f, 2.6f, C_BODY, Z_IC + Z_ICD);
        for (int i = -1; i <= 1; i++)
            if (start) bar(cx - 2.6f, cy + i * 2.6f, cx + 2.6f, cy + i * 2.6f, 1.1f, C_MARK, z);
            else bar(cx + i * 2.2f, cy - 2.8f, cx + i * 2.2f, cy + 2.8f, 1.1f, C_MARK, z);
        return 11;
    }
    if (!draw) return 15;
    cx = x + 7.5f;
    ring(cx, cy, 7.5f, C_BODY);
    if (style == BPS_SWITCH) {
        bar(cx - 3.5f, cy, cx + 3.5f, cy, 1.5f, C_MARK, z);
        if (start) bar(cx, cy - 3.5f, cx, cy + 3.5f, 1.5f, C_MARK, z);
    } else if (start) {
        for (int i = -1; i <= 1; i++) bar(cx - 3.2f, cy + i * 2.4f, cx + 3.2f, cy + i * 2.4f, 1.1f, C_MARK, z);
    } else {   /* two overlapping windows */
        rrect(cx - 3.6f, cy - 3.0f, cx + 1.4f, cy + 1.2f, 0, 0, C_MARK, z);
        rrect(cx - 2.7f, cy - 2.1f, cx + 0.5f, cy + 0.3f, 0, 0, C_BODY, z + Z_ICD);
        rrect(cx - 1.4f, cy - 1.2f, cx + 3.6f, cy + 3.0f, 0, 0, C_MARK, z + 2 * Z_ICD);
        rrect(cx - 0.5f, cy - 0.3f, cx + 2.7f, cy + 2.1f, 0, 0, C_BODY, z + 3 * Z_ICD);
    }
    return 15;
}

static float ic_dpad(int code, float x, float y, int draw)   /* the cross, the arm(s) pressed marked */
{
    float cx = x + 7.5f, cy = y + 7.5f, z = Z_IC + 2 * Z_ICD;
    if (!draw) return 15;
    rrect(cx - 2.8f, y, cx + 2.8f, y + 15, 1, 1, C_EDGE, Z_IC);
    rrect(x, cy - 2.8f, x + 15, cy + 2.8f, 1, 1, C_EDGE, Z_IC);
    rrect(cx - 1.9f, y + 0.9f, cx + 1.9f, y + 14.1f, 0.6f, 0.6f, C_BODY, Z_IC + Z_ICD);
    rrect(x + 0.9f, cy - 1.9f, x + 14.1f, cy + 1.9f, 0.6f, 0.6f, C_BODY, Z_IC + Z_ICD);
    if (code == GP_UP) tri(cx, y + 3.6f, 2.0f, 0, C_MARK, z);
    if (code == GP_DOWN) tri(cx, y + 11.4f, 2.0f, 2, C_MARK, z);
    if (code == GP_LEFT || code == GP_DPAD_LR) tri(x + 3.6f, cy, 2.0f, 3, C_MARK, z);
    if (code == GP_RIGHT || code == GP_DPAD_LR) tri(x + 11.4f, cy, 2.0f, 1, C_MARK, z);
    return 15;
}

static float ic_stick(int code, float x, float y, int draw)   /* a stick: pressed (an inner ring) or pushed one way (an arrow) */
{
    int a = (code - GP_AXIS) / 2, left = code == GP_LSTICK || (code >= GP_AXIS && a < 2);
    float cx = x + 7.5f, cy = y + 7.5f, z = Z_IC + 2 * Z_ICD;
    if (!draw) return 15;
    ring(cx, cy, 7.5f, C_BODY);
    if (code < GP_AXIS) { disc(cx, cy, 5.2f, C_EDGE, z); disc(cx, cy, 4.4f, C_BODY, z + Z_ICD); }
    else {
        static const int dir[4][2] = { { 1, 3 }, { 2, 0 }, { 1, 3 }, { 2, 0 } };   /* (axis, negative): right left, down up */
        int d = dir[a & 3][(code - GP_AXIS) & 1];
        tri(cx + (d == 1 ? 5.0f : d == 3 ? -5.0f : 0), cy + (d == 2 ? 5.0f : d == 0 ? -5.0f : 0), 1.7f, d, C_MARK, z);
    }
    ic_text(cx, y + 2.2f, left ? "L" : "R", 0.36f, 0xFFFFFFFF);
    return 15;
}

static float ic_mouse(int n, float x, float y, int draw)   /* the mouse, the button pressed light */
{
    float z = Z_IC + 2 * Z_ICD;
    if (!draw) return 11;
    rrect(x, y, x + 11, y + 15, 5.5f, 4.5f, C_EDGE, Z_IC);
    rrect(x + 0.9f, y + 0.9f, x + 10.1f, y + 14.1f, 4.6f, 3.6f, C_BODY, Z_IC + Z_ICD);
    bar(x + 1, y + 6.2f, x + 10, y + 6.2f, 0.8f, C_EDGE, z);
    bar(x + 5.5f, y + 1, x + 5.5f, y + 6.2f, 0.8f, C_EDGE, z);
    if (n == 1) rrect(x + 1.6f, y + 1.7f, x + 4.8f, y + 5.5f, 1.6f, 0.3f, C_MARK, z + Z_ICD);
    if (n == 3) rrect(x + 6.2f, y + 1.7f, x + 9.4f, y + 5.5f, 1.6f, 0.3f, C_MARK, z + Z_ICD);
    if (n == 2) rrect(x + 4.6f, y + 2.2f, x + 6.4f, y + 5.6f, 0.9f, 0.9f, C_MARK, z + Z_ICD);
    if (n > 3) bar(x + 0.3f, y + (n == 4 ? 9.5f : 6.5f), x + 0.3f, y + (n == 4 ? 12.5f : 9.5f), 1.4f, C_MARK, z + Z_ICD);   /* X1, X2: the side buttons */
    return 11;
}

static float ic_key(const char* s, float x, float y, int draw)   /* a key cap: its name, or an arrow */
{
    static const char* arrows[4] = { "Up", "Right", "Down", "Left" };
    int arrow = -1;
    float w;
    for (int i = 0; i < 4; i++) if (!strcmp(s, arrows[i])) arrow = i;
    w = arrow >= 0 ? 15 : pc_ui_text_w(s, 0.42f) + 10;
    if (w < 15) w = 15;
    if (!draw) return w;
    rrect(x, y, x + w, y + 15, 2.5f, 2.5f, C_EDGE, Z_IC);
    rrect(x + 0.9f, y + 0.8f, x + w - 0.9f, y + 12.8f, 1.8f, 1.8f, 0xFF484846u, Z_IC + Z_ICD);
    if (arrow >= 0) tri(x + 7.5f, y + 6.8f, 3.0f, arrow, 0xFFFFFFFF, Z_IC + 2 * Z_ICD);
    else ic_text(x + w / 2, y - 0.2f, s, 0.42f, 0xFFFFFFFF);
    return w;
}

static float ic_pad(int code, float x, float y, int draw)
{
    char name[40];
    if (code >= GP_SOUTH && code <= GP_NORTH) return ic_face(code, x, y, draw);
    if (code == GP_LB || code == GP_RB || code == GP_LT || code == GP_RT || code == GP_LT + 1 || code == GP_RT + 1)
        return ic_shoulder(code == GP_LT + 1 ? GP_LT : code == GP_RT + 1 ? GP_RT : code, x, y, draw);
    if (code == GP_BACK || code == GP_START) return ic_menu(code, x, y, draw);
    if ((code >= GP_UP && code <= GP_RIGHT) || code == GP_DPAD_LR) return ic_dpad(code, x, y, draw);
    if (code == GP_LSTICK || code == GP_RSTICK || (code >= GP_AXIS && code < GP_LT)) return ic_stick(code, x, y, draw);
    return ic_key(bind_code_name(BIND_PAD, code, name, sizeof(name)), x, y, draw);   /* (Guide, paddles, touchpad: the name) */
}

/* a glyph string at x (left to right, mirrored right to left; draw 0: measure); returns its width */
static float glyph_run(const char* g, float x, float y, int draw)
{
    float w = 0, gap = 0;
    while (*g) {
        float iw;
        if (*g == '\t') { g++; continue; }
        if ((*g == GL_PAD || *g == GL_MOUSE) && g[1]) {
            int pad = *g == GL_PAD, code = (unsigned char)g[1];
            iw = pad ? ic_pad(code, 0, y, 0) : ic_mouse(code, 0, y, 0);
            if (draw && pad) ic_pad(code, mx(x + w + gap, iw), y, 1);
            if (draw && !pad) ic_mouse(code, mx(x + w + gap, iw), y, 1);
            g += 2;
        } else {
            char t[40];
            int n = 0;
            while (g[n] && g[n] != '\t' && g[n] != GL_PAD && g[n] != GL_MOUSE) n++;
            snprintf(t, sizeof(t), "%.*s", n, g);
            g += n;
            iw = ic_key(t, 0, y, 0);
            if (draw) ic_key(t, mx(x + w + gap, iw), y, 1);
        }
        w += gap + iw;
        gap = 3;
    }
    return w;
}

static const char* pad_glyph(unsigned int mask, char* buf, int n)   /* the gamepad input bound to a DS2 button */
{
    static const struct { unsigned int bit; int row; } m[] = { { P_CROSS, B_CROSS }, { P_CIRCLE, B_CIRCLE }, { P_SQUARE, B_SQUARE }, { P_TRI, B_TRIANGLE },
                                                              { P_L1, B_L1 }, { P_R1, B_R1 }, { P_START, B_START } };
    for (int i = 0; i < (int)(sizeof(m) / sizeof(m[0])); i++)
        if (mask & m[i].bit) {
            int c = bind_get(m[i].row, BIND_PAD, 0) ? bind_get(m[i].row, BIND_PAD, 0) : bind_get(m[i].row, BIND_PAD, 1);
            if (c <= 0 || c > 0x7F || n < 3) return "-";
            buf[0] = GL_PAD; buf[1] = (char)c; buf[2] = 0;
            return buf;
        }
    return "?";
}

static const char* kb_glyph(int row, char* buf, int n)   /* the key (or mouse button) bound to a row */
{
    int c = bind_get(row, BIND_KB, 0) ? bind_get(row, BIND_KB, 0) : bind_get(row, BIND_KB, 1);
    const char* s;
    if (c > BIND_MOUSE && c < BIND_MOUSE + 6 && n >= 3) { buf[0] = GL_MOUSE; buf[1] = (char)(c - BIND_MOUSE); buf[2] = 0; return buf; }
    s = bind_code_name(BIND_KB, c, buf, n);
    return !strcmp(s, "Return") ? "Enter" : !strcmp(s, "Escape") ? "Esc" : !strcmp(s, "Delete") ? "Del" : s;
}

/* the buttons of this key type: Confirm, Cancel, and the one that holds for Default: Triangle, the only face button no key
   type uses in menus (Square confirms in types A, C and D, cancels in B) */
static void buttons(void)
{
    st.ok = (sys->keytype & 3) < 3 ? AdvGetOkButton() : 0xC0;
    st.cancel = (sys->keytype & 3) < 3 ? AdvGetCancelButton() : 0x20;
    st.def = P_TRI;
}

/* the device used last: a press says which (pad.c), the mouse or a menu key means the keyboard; Button Icons
   "Keyboard and Mouse": always the keyboard */
void ui_device(unsigned int press, int kb_used)
{
    if (press) st.pad = pad_last_pad();
    if (kb_used || g_button_icons == BI_KBM) st.pad = 0;
}

const char* ui_glyph(int which, char* buf, int n)
{
    char a[32], b[32];
    buttons();
    switch (which) {
    case UG_OK: return st.pad ? pad_glyph(st.ok, buf, n) : kb_glyph(B_START, buf, n);
    case UG_BACK: return st.pad ? pad_glyph(st.cancel, buf, n) : "Esc";
    case UG_DEFAULT: return st.pad ? pad_glyph(st.def, buf, n) : "Del";
    case UG_START: return st.pad ? pad_glyph(P_START, buf, n) : kb_glyph(B_START, buf, n);
    case UG_CAT:
        if (st.pad) snprintf(buf, n, "%s%s", pad_glyph(P_L1, a, sizeof(a)), pad_glyph(P_R1, b, sizeof(b)));
        else snprintf(buf, n, "%s\t%s", kb_glyph(B_L1, a, sizeof(a)), kb_glyph(B_R1, b, sizeof(b)));
        return buf;
    case UG_CHANGE:
        if (st.pad) { snprintf(buf, n, "%c%c", GL_PAD, GP_DPAD_LR); return buf; }
        snprintf(buf, n, "%s\t%s", kb_glyph(B_LEFT, a, sizeof(a)), kb_glyph(B_RIGHT, b, sizeof(b)));
        return buf;
    }
    return "";
}

/* one hint at x (left to right): the button's icons, its word; fill: how far a hold has got (0..1), a bar under the
   icons; returns the next x */
float ui_hint_w(const char* glyph, const char* word) { return glyph_run(glyph, 0, 0, 0) + 5 + pc_ui_text_w(tr_ctx(word, "button hint"), 0.5f); }
float ui_hint(float x, float y, const char* glyph, const char* word, float fill)
{
    rtl = lang_rtl_text();   /* (from right to left in Arabic, whichever screen draws it) */
    word = tr_ctx(word, "button hint");   /* ("Select", "Back": the table's are the button and the walking binding) */
    float gw = glyph_run(glyph, x, y, 0), ww = pc_ui_text_w(word, 0.5f);
    if (fill > 0) {
        rect_m(x, y + 16.5f, x + gw, y + 18.5f, 0xFF505050, Z_HI + 0.01f);
        rect_m(x, y + 16.5f, x + gw * (fill > 1 ? 1 : fill), y + 18.5f, C_GOLD, Z_HI + 0.02f);
    }
    glyph_run(glyph, x, y, 1);
    text_m(x + gw + 5, y - 1, word, 0.5f, C_HELP);
    return x + gw + 5 + ww + 18;
}

static void bar_build(void)
{
    static char g[4][40];
    nhints = 0;
    if (st.capture) return;
    if (st.search && st.typing) {   /* (typing: the keyboard's keys) */
        hints[nhints++] = (Hint){ "Enter", "Results", UG_OK };
        hints[nhints++] = (Hint){ "Esc", "Close Search", UG_BACK };
        return;
    }
    hints[nhints++] = (Hint){ ui_glyph(UG_OK, g[0], 40), "Select", UG_OK };
    hints[nhints++] = (Hint){ ui_glyph(UG_BACK, g[1], 40), st.search && !st.depth ? "Close Search" : "Back", UG_BACK };
    if (st.list == 3) {   /* Give Item: the amount, the groups */
        hints[nhints++] = (Hint){ ui_glyph(UG_CHANGE, g[2], 40), "Amount", UG_CHANGE };
        hints[nhints++] = (Hint){ ui_glyph(UG_CAT, g[3], 40), "Group", UG_CAT };
    }
    if (!st.list) hints[nhints++] = (Hint){ ui_glyph(UG_DEFAULT, g[2], 40), st.pad ? "Hold: Default" : "Default", UG_DEFAULT };
    if (!st.list && !st.depth) hints[nhints++] = (Hint){ ui_glyph(UG_CAT, g[3], 40), "Category", UG_CAT };
    if (!st.list && !st.depth && !st.pad && st.screen == MS_SETTINGS && st.only < 0) hints[nhints++] = (Hint){ "Ctrl\tF", "Search", UG_SEARCH };
}

static void bar_draw(void)
{
    float x = 20;
    for (int i = 0; i < nhints; i++) {
        float fill = hints[i].action == UG_DEFAULT && st.hold && !st.held_done ? (float)(SDL_GetTicks() - (uint64_t)st.hold) / 500.0f : 0;
        hint_x0[i] = x;
        x = ui_hint(x, Y_BAR, hints[i].glyph, hints[i].word, fill);
        hint_x1[i] = x - 12;
    }
}

static int bar_hit(float x, float y)   /* x: left to right */
{
    if (y < Y_BAR - 3 || y > Y_BAR + 18) return 0;
    for (int i = 0; i < nhints; i++) if (x >= hint_x0[i] && x < hint_x1[i]) return hints[i].action;
    return 0;
}

/* ---------------------------------------------------------------- drawing */
static float row_y(int i) { return Y0 + (i - TOP) * ROW_H; }

void ui_dot(float x, float y);
static void dot(float x, float y) { ui_dot(x, y); }
void ui_dot(float x, float y)
{
    float cx = mx(x, 0);
    NJS_POLYGON_VTX v[4] = { { cx, y - 3, Z_HI + 0.03f, C_GOLD }, { cx - 3, y, Z_HI + 0.03f, C_GOLD }, { cx + 3, y, Z_HI + 0.03f, C_GOLD }, { cx, y + 3, Z_HI + 0.03f, C_GOLD } };
    njDrawPolygon(v, 4, 1);
}

/* the marks (proposal §1.4): 1 R, a restart applies it; 2 a turning arrow, it applies from the next room (a run's rule in
   game); 0 none. Drawn as shapes: the game's font has no such signs */
#define X_MARK (X_VALUE - 32)
static int row_mark(const Row* r)
{
    const Setting* s = row_set(r);
    if (r->kind != R_ITEM) return 0;
    if (s && (s->flags & SET_RESTART)) return 1;
    return in_setup(r->m) && setup_row_next_room(r->m) ? 2 : 0;
}
static void mark_draw(float x, float y, int k)   /* x: the mark's left (left to right), y: its row's */
{
    if (k == 1) {
        rect_m(x, y + 2, x + 12, y + 15, 0xFFB06A28, Z_HI + 0.02f);
        text_m(x + 2.5f, y + 0.5f, "R", 0.42f, 0xFFFFFFFF);
        return;
    }
    {   /* a circle of dots open at the top, its arrow head at the top left pointing right (clockwise) */
        float cx = x + 6, cy = y + 9, hx = cx + 5 * cosf(4.19f), hy = cy + 5 * sinf(4.19f);
        for (int i = 0; i <= 10; i++) {
            float a = 5.24f + i * 0.524f, px = cx + 5 * cosf(a), py = cy + 5 * sinf(a);
            rect_m(px - 1, py - 1, px + 1, py + 1, C_GOLD, Z_HI + 0.02f);
        }
        {
            float d = rtl ? -1.0f : 1.0f, ax = mx(hx, 0);
            NJS_POLYGON_VTX v[3] = { { ax + d * 3.5f, hy, Z_HI + 0.03f, C_GOLD }, { ax - d * 1.5f, hy - 3.5f, Z_HI + 0.03f, C_GOLD }, { ax - d * 1.5f, hy + 3.5f, Z_HI + 0.03f, C_GOLD } };
            njDrawPolygon(v, 3, 1);
        }
    }
}

static const char* row_label(const Row* r, char* buf, int n)
{
    const MenuRow* m = r->m;
    switch (r->kind) {
    case R_BIND: return bind_row_label(r->idx, buf, n);
    case R_BIND_RESET: return "Reset Bindings";
    case R_RATES: return "Enemy Rates";
    case R_CUSTOM: return r->idx == 2 ? "Monster Looks" : r->idx ? "Your Random Looks" : "Story Character Looks";
    case R_RESET_CAT: return "Reset Category";
    case R_GROUP:
        if (!m && r->idx == 199) return "NO SETTING MATCHES";   /* (the search's) */
        if (!m && r->idx >= 200) {   /* the search's: the category (a sub-page: and its parent's) */
            const MenuCat* c = &menu_cats[r->idx - 200];
            if (c->parent < 0) return c->name;
            snprintf(buf, n, "%s: %s", tr(menu_cats[c->parent].name), tr(c->name));
            return buf;
        }
        if (!m) {   /* a binding group (idx 100 + BG_*) */
            if (r->idx - 100 == BG_GAME) { snprintf(buf, n, tr("GAME (CONTROL TYPE %c)"), 'A' + (sys->keytype & 3)); return buf; }
            return r->idx - 100 == BG_TPS ? (st.cat == MC_BIND_PAD ? "THIRD PERSON: MODERN CONTROLLER" : "THIRD PERSON: KEYBOARD AND MOUSE") : "HOTKEYS";
        }
        return m->group;
    }
    if (m->label) return m->label;
    if (m->kind == ML_BIO) return setup_label(m);
    if (m->ref) return m->ref;
    return "";
}

static void draw_row(int i, int sel)
{
    const Row* r = &rows[i];
    const Setting* s = row_set(r);
    const char* lock = r->kind == R_ITEM || r->kind == R_CUSTOM ? lock_of(&(Row){ R_ITEM, r->m, r->kind == R_ITEM ? r->set : setting_of(r->m), 0 }) : NULL;
    float y = row_y(i);
    unsigned int col = sel ? 0xFFFFFFFF : C_ROW;
    char label[256], val[160];
    const char* v;
    if (lock) col = sel ? 0xFF9A9A9A : C_DIM;
    if (r->kind == R_GROUP) { text_m(ROWS_X0 + 4, y + 2, row_label(r, label, sizeof(label)), 0.5f, C_GOLD); return; }
    if (r->kind == R_INFO) {   /* a line of GAME SETUP's text (OVERVIEW's ACTIVE MODIFIERS: a box behind its lines) */
        unsigned int c = C_HELP;
        int n = setup_info(r->m, r->idx, val, sizeof(val), &c);
        if (r->m->arg == SI_MODIFIERS && r->idx < n) rect_m(ROWS_X0 + 2, y - 2, ROWS_X1, y + ROW_H - 2, 0x30FFFFFF, Z_PANEL + 0.002f);
        text_fit(X_LABEL, y + 1, val, 0.48f, ROWS_X1 - X_LABEL - 8, c);
        return;
    }
    if (sel) rect_m(ROWS_X0, y - 2, ROWS_X1, y + ROW_H - 3, st.capture ? 0x50FFC040 : 0x40FFFFFF, Z_HI);
    if (changed(r)) dot(X_LABEL - 7, y + 8);
    if (r->kind == R_BIND) {
        int dev = st.cat == MC_BIND_PAD, conflict = bind_conflict(r->idx, dev);
        text_fit(X_LABEL, y, bind_row_label(r->idx, label, sizeof(label)), SC_ROW, 236, col);
        for (int k = 0; k < BIND_SLOTS; k++) {
            float x = 416 + k * 102;
            unsigned int c = conflict ? 0xFFFF6060 : k == st.slot && sel ? col : C_ROW;
            const char* nm = st.capture == r->idx + 1 && k == st.slot ? "..." : bind_code_name(dev, bind_get(r->idx, dev, k), val, sizeof(val));
            char combo[80];
            if (!(st.capture == r->idx + 1 && k == st.slot) && bind_combo(r->idx, dev, k)) { snprintf(combo, sizeof(combo), tr("Aim + %s"), nm); nm = combo; }
            if (sel && k == st.slot) rect_m(x - 4, y - 1, x + 96, y + ROW_H - 4, 0x40FFFFFF, Z_HI + 0.01f);
            text_fit(x, y, nm, SC_ROW, 92, c);
        }
        return;
    }
    {   /* a mark before the value: R (applies after a restart) or a turning arrow (applies from the next room) */
        int k = row_mark(r);
        snprintf(label, sizeof(label), "%s", tr(row_label(r, val, sizeof(val))));
        text_fit(X_LABEL, y, label, SC_ROW, X_VALUE - X_LABEL - (k ? 44 : 16), col);
        if (k) mark_draw(X_MARK, y, k);
    }
    if (r->kind == R_RATES || r->kind == R_CUSTOM) {
        if (r->kind == R_RATES) rate_list_summary(enemy_opts()->rate, val, sizeof(val));
        else if (r->idx == 2) cast_list_summary(2, enemy_opts()->cast_enemy_off, 0, val, sizeof(val));
        else if (r->idx) cast_list_summary(1, cast_appear_off, g_biorand_appear_imports > 0 ? __atomic_load_n(&actor_import_n, __ATOMIC_ACQUIRE) : 0, val, sizeof(val));
        else cast_list_summary(0, enemy_cast_off(), enemy_opts()->cast_imports ? __atomic_load_n(&actor_import_n, __ATOMIC_ACQUIRE) : 0, val, sizeof(val));
        text_fit(X_VALUE, y, val, SC_ROW, VALUE_W, col);
        pc_ui_arrow(mx(ROWS_X1 - 6, 0), y + 9, rtl ? -1 : 1, col);
        return;
    }
    if (r->kind != R_ITEM) return;
    if (r->m->kind == ML_ACTION) return;   /* (a button: its label alone) */
    if (s && (s->flags & SET_ACTION) && r->m->kind != ML_SHORTCUT) {
        if (r->set < (int)sizeof(st.pending) && st.pending[r->set]) text_m(X_VALUE, y + 1, pc_in_game() ? "(on resume)" : "(on leaving menu)", 0.48f, C_GOLD);
        return;
    }
    v = value_text(r, val, sizeof(val));
    switch (r->m->kind) {
    case ML_PAGE: case ML_SHORTCUT: case ML_SCREEN: case ML_WINDOW: case ML_TEXT: case ML_IMPORT: case ML_GIVE:   /* opens something: an arrow at the end */
        if (v) text_fit(X_VALUE, y, v, SC_ROW, VALUE_W, col);
        pc_ui_arrow(mx(ROWS_X1 - 6, 0), y + 9, rtl ? -1 : 1, col);
        return;
    }
    if (!v) return;
    {   /* a value between arrows */
        float w = pc_ui_text_w(v, SC_ROW), x0 = X_VALUE - 12, x1;
        if (w > VALUE_W) w = VALUE_W;
        x1 = X_VALUE + w + 13;
        if (!lock) { pc_ui_arrow(mx(rtl ? x1 : x0, 0), y + 9, -1, col); pc_ui_arrow(mx(rtl ? x0 : x1, 0), y + 9, 1, col); }
        text_fit(X_VALUE, y, v, SC_ROW, VALUE_W, col);
    }
}

/* the value preview, right of the help: a choice's values, a number's range and default, the test pattern */
static void draw_virt_preview(const Row* r);
static void draw_preview(const Row* r)
{
    const Setting* s = row_set(r);
    char a[96], b[96], t[160];
    int d, has_d;
    if ((r->kind == R_ITEM || r->kind == R_CUSTOM) && in_setup(r->m) && setup_preview(r->m, rtl ? 640 - ROWS_X1 : X_PREVIEW, Y_HELP, ROWS_X1 - X_PREVIEW)) return;
    if (r->kind == R_ITEM && virt(r->m)) { draw_virt_preview(r); return; }
    if (r->kind != R_ITEM || !s || (s->flags & SET_ACTION) || r->m->kind != ML_SET) {
        if (r->kind == R_ITEM && r->m->kind == ML_PAGE && !is_bind_cat(r->m->arg)) {   /* what the sub-page holds */
            char list[512] = "";
            for (int i = 0; i < menu_rows_n; i++)
                if (menu_rows[i].cat == r->m->arg && menu_rows[i].kind != ML_RETIRED) {
                    const char* l = tr(menu_rows[i].label ? menu_rows[i].label : menu_rows[i].ref);
                    snprintf(list + strlen(list), sizeof(list) - strlen(list), "%s%s", list[0] ? ", " : "", l);
                }
            text_m(X_PREVIEW, Y_HELP, "CONTAINS", 0.48f, C_GOLD);
            (rtl ? pc_ui_text_wrap_r : pc_ui_text_wrap)(rtl ? 640 - ROWS_X1 : X_PREVIEW, Y_HELP + 16, ROWS_X1 - X_PREVIEW, list, 0.42f, C_HELP, 3);
        }
        return;
    }
    has_d = menu_default(r->set, &d);
    if (s->flags & SET_CALIB) {   /* the test pattern: drawn into the picture, so calibrated with it */
        float x = X_PREVIEW;
        text_m(X_PREVIEW, Y_HELP, "TEST PATTERN", 0.48f, C_GOLD);
        for (int k = 0; k < 2; k++, x += 5 * 16 + 8 + 10) {
            rect_m(x, Y_HELP + 18, x + 5 * 16 + 8, Y_HELP + 44, k ? 0xFFFFFFFF : 0xFF000000, Z_HI + 0.02f);
            for (int j = 0; j < CALIB_STEPS; j++)
                rect_m(x + 4 + j * 16, Y_HELP + 22, x + 4 + j * 16 + 12, Y_HELP + 40, 0xFF000000u | settings_calib[k][j] * 0x010101u, Z_HI + 0.03f);
        }
        text_m(X_PREVIEW, Y_HELP + 46, "All squares just visible.", 0.38f, C_HELP);
        return;
    }
    if (settings_is_bool(s) || s->names || (r->m->names && !s->name) || (s->name && settings_hi(s) - s->lo <= 8 && !s->fmt)) {   /* a choice: its values */
        float x = X_PREVIEW, y = Y_HELP + 16;
        int hi = settings_hi(s), cur = shown_get(r);
        text_m(X_PREVIEW, Y_HELP, "VALUES", 0.48f, C_GOLD);
        if (hi - s->lo > 8) return;
        for (int v = s->lo; v <= hi; v++) {
            const char* nm;
            float w;
            if (r->m->names && !s->name) nm = tr(r->m->names[v - s->lo]);
            else if (s->names) nm = tr(s->names[v - s->lo]);
            else if (s->name) nm = s->name(v, a, sizeof(a));
            else nm = tr(v ? "On" : "Off");
            snprintf(t, sizeof(t), "%s", nm);
            w = pc_ui_text_w(t, 0.42f) + (has_d && v == d ? 8 : 0);
            if (x + w > ROWS_X1 && x > X_PREVIEW) { x = X_PREVIEW; y += 13; }
            if (y > Y_HELP + 16 + 13) break;
            if (has_d && v == d) { dot(x + 3, y + 6); x += 8; w -= 8; }
            text_m(x, y, t, 0.42f, v == cur ? 0xFFFFFFFF : 0xFF808080);
            x += w + 10;
        }
        if (has_d) { dot(X_PREVIEW + 3, Y_HELP + 16 + 26 + 6); text_m(X_PREVIEW + 9, Y_HELP + 16 + 26, "Default", 0.36f, 0xFF808080); }
        return;
    }
    {   /* a number: its range, the value and the default */
        int lo = s->lo, hi = settings_hi(s), v = shown_get(r);
        float x0 = X_PREVIEW, x1 = ROWS_X1 - 2, k = hi > lo ? (float)(v - lo) / (hi - lo) : 0, kd;
        if (r->m->flags & MLF_VIBRATION) v = vib_get(), k = hi > lo ? (float)(v - lo) / (hi - lo) : 0;
        text_m(X_PREVIEW, Y_HELP, "RANGE", 0.48f, C_GOLD);
        rect_m(x0, Y_HELP + 20, x1, Y_HELP + 25, 0xFF404040, Z_HI + 0.01f);
        rect_m(x0, Y_HELP + 20, x0 + (x1 - x0) * k, Y_HELP + 25, C_GOLD, Z_HI + 0.02f);
        if (s->name) { s->name(lo, a, sizeof(a)); s->name(hi, b, sizeof(b)); }
        else { snprintf(a, sizeof(a), s->fmt ? s->fmt : "%d", lo); snprintf(b, sizeof(b), s->fmt ? s->fmt : "%d", hi); }
        if (!has_d || d != lo) text_m(x0, Y_HELP + 29, a, 0.4f, C_HELP);   /* (the ends, unless the default is one) */
        else a[0] = 0;
        if (!has_d || d != hi) text_m(x1 - pc_ui_text_w(b, 0.4f), Y_HELP + 29, b, 0.4f, C_HELP);
        else b[0] = 0;
        if (has_d) {
            char dv[64];
            kd = hi > lo ? (float)(d - lo) / (hi - lo) : 0;
            rect_m(x0 + (x1 - x0) * kd - 1, Y_HELP + 16, x0 + (x1 - x0) * kd + 1, Y_HELP + 29, 0xFFFFFFFF, Z_HI + 0.03f);
            if (s->name) s->name(d, dv, sizeof(dv)); else snprintf(dv, sizeof(dv), s->fmt ? s->fmt : "%d", d);
            snprintf(t, sizeof(t), tr("Default %s"), dv);
            {
                float w = pc_ui_text_w(t, 0.4f), x = x0 + (x1 - x0) * kd - w / 2;
                if (x < x0 + pc_ui_text_w(a, 0.4f) + 6) x = x0 + pc_ui_text_w(a, 0.4f) + 6;
                if (x + w > x1 - pc_ui_text_w(b, 0.4f) - 6) x = x1 - pc_ui_text_w(b, 0.4f) - 6 - w;
                text_m(x, Y_HELP + 29, t, 0.4f, C_HELP);
            }
        }
    }
}

/* ... of a GAME SETUP value (setup_get): its values, or a number's range */
static void draw_virt_preview(const Row* r)
{
    const MenuRow* m = r->m;
    char t[96];
    int n, lo, hi, cur = setup_get(m), d, has_d = setup_default(m, &d);
    if (setup_choice(m, &n)) {
        float x = X_PREVIEW, y = Y_HELP + 16;
        text_m(X_PREVIEW, Y_HELP, "VALUES", 0.48f, C_GOLD);
        for (int v = 0; v < n; v++) {
            float w;
            setup_name(m, v, t, sizeof(t));
            w = pc_ui_text_w(t, 0.42f) + (has_d && v == d ? 8 : 0);
            if (x + w > ROWS_X1 && x > X_PREVIEW) { x = X_PREVIEW; y += 13; }
            if (y > Y_HELP + 16 + 13) break;
            if (has_d && v == d) { dot(x + 3, y + 6); x += 8; w -= 8; }
            text_m(x, y, t, 0.42f, v == cur ? 0xFFFFFFFF : 0xFF808080);
            x += w + 10;
        }
        if (has_d) { dot(X_PREVIEW + 3, Y_HELP + 16 + 26 + 6); text_m(X_PREVIEW + 9, Y_HELP + 16 + 26, "Default", 0.36f, 0xFF808080); }
        return;
    }
    setup_range(m, &lo, &hi);
    {
        float x0 = X_PREVIEW, x1 = ROWS_X1 - 2, k = hi > lo ? (float)(cur - lo) / (hi - lo) : 0;
        text_m(X_PREVIEW, Y_HELP, "RANGE", 0.48f, C_GOLD);
        rect_m(x0, Y_HELP + 20, x1, Y_HELP + 25, 0xFF404040, Z_HI + 0.01f);
        rect_m(x0, Y_HELP + 20, x0 + (x1 - x0) * k, Y_HELP + 25, C_GOLD, Z_HI + 0.02f);
        text_m(x0, Y_HELP + 29, setup_name(m, lo, t, sizeof(t)), 0.4f, C_HELP);
        setup_name(m, hi, t, sizeof(t));
        text_m(x1 - pc_ui_text_w(t, 0.4f), Y_HELP + 29, t, 0.4f, C_HELP);
        if (has_d) {
            char dv[64];
            float kd = hi > lo ? (float)(d - lo) / (hi - lo) : 0;
            rect_m(x0 + (x1 - x0) * kd - 1, Y_HELP + 16, x0 + (x1 - x0) * kd + 1, Y_HELP + 29, 0xFFFFFFFF, Z_HI + 0.03f);
            snprintf(t, sizeof(t), tr("Default %s"), setup_name(m, d, dv, sizeof(dv)));
            text_m(x0, Y_HELP + 42, t, 0.4f, C_HELP);
        }
    }
}

/* a help text's path ("GAME SETUP > CHARACTERS") as the game's font can draw it: it has no '>' (a picture there), so
   " > " becomes ", "; in Arabic (display order, lang_shape mirrored it to " < ") " ," */
void ui_no_gt(char* t)
{
    char* p;
    while ((p = strstr(t, " > "))) { p[0] = ','; memmove(p + 1, p + 2, strlen(p + 2) + 1); }
    if (lang_rtl(t)) while ((p = strstr(t, " < "))) { p[1] = ','; memmove(p + 2, p + 3, strlen(p + 3) + 1); }
}

static void draw_help(void)
{
    const Row* r = &rows[SEL];
    const Setting* s = row_set(r);
    char t[640], nm[96], name[256];
    unsigned int col = C_HELP;
    float w = X_PREVIEW - CAT_X0 - 14;
    t[0] = 0;
    snprintf(name, sizeof(name), "%s", tr(row_label(r, nm, sizeof(nm))));
    if (st.capture) {
        snprintf(t, sizeof(t), tr("Press a %s for %s. Esc: cancel. Delete: clear. %d s left."), tr(st.cat == MC_BIND_PAD ? "controller button" : "key or mouse button"),
                 tr(bind_rows[st.capture - 1].name), (bind_capture_left_ms() + 999) / 1000);
        col = 0xFFFFE080;
    } else if (st.search && st.typing) {
        snprintf(name, sizeof(name), "%s", tr("Search"));
        snprintf(t, sizeof(t), "%s", tr("Type a word of a setting's name, help or value: every category's matches are listed. Enter or Down: to the results; Esc: close."));
        col = 0xFFFFE080;
    } else if (st.msg_t > 0) { st.msg_t--; snprintf(t, sizeof(t), "%s", st.msg); col = 0xFFFFE080; }
    else if (st.list) { snprintf(t, sizeof(t), "%s", tr(st.list == 3 ? give_list_help() : st.list == 2 ? rate_list_help() : cast_list_help())); name[0] = 0; }
    else switch (r->kind) {
    case R_BIND: {
        int dev = st.cat == MC_BIND_PAD, c = bind_conflict(r->idx, dev);
        if (c) { snprintf(t, sizeof(t), tr("Also assigned to %s. Change either binding to avoid a conflict."), tr(bind_rows[c - 1].name)); col = 0xFFFF8080; }
        else snprintf(t, sizeof(t), tr("Confirm: rebind. Left/right: choose a binding (%s)."), bind_code_name(dev, bind_get(r->idx, dev, st.slot), nm, sizeof(nm)));
        break;
    }
    case R_BIND_RESET: snprintf(t, sizeof(t), "%s", tr(st.cat == MC_BIND_PAD ? "Restore the default controller bindings." : "Restore the default keyboard and mouse bindings.")); break;
    case R_RATES: snprintf(t, sizeof(t), "%s", tr("Set how often each enemy type is chosen.")); break;
    case R_CUSTOM: snprintf(t, sizeof(t), "%s", tr(r->idx == 2 ? "Choose the monsters cutscene monsters can look like." : r->idx ? "Choose the looks your random look is picked from."
                                                       : "Choose the looks the story characters can have.")); break;
    case R_RESET_CAT: snprintf(t, sizeof(t), tr("Put every setting of %s back to its default."), tr(menu_cats[st.cat].name)); break;
    case R_ITEM: {
        const char* lock = lock_of(r);
        const char* help = r->m->help ? r->m->help : s ? s->help : r->m->kind == ML_BIO ? setup_help(r->m) : NULL;
        char ap[160] = "";
        if (s && (s->flags & SET_ACTION) && !lock && !(s->flags & SET_NOW) && r->m->kind != ML_SHORTCUT)
            snprintf(ap, sizeof(ap), "%s%s", help ? " " : "", tr(pc_in_game() ? "Runs when you resume." : "Applies when you leave this menu."));
        if (r->m->kind == ML_SCREEN && r->m->arg == 1 && pc_in_game()) lock = NULL;
        if (rtl) snprintf(t, sizeof(t), "%s%s%s%s%s", ap, help ? tr(help) : "", row_mark(r) == 1 ? tr("Restart needed. ") : row_mark(r) == 2 && !lock && !has(help, "next room") ? tr("Applies from the next room. ") : "", lock ? " " : "", lock ? lock : "");
        else snprintf(t, sizeof(t), "%s%s%s%s%s", lock ? lock : "", lock ? " " : "", row_mark(r) == 1 ? tr("Restart needed. ") : row_mark(r) == 2 && !lock && !has(help, "next room") ? tr("Applies from the next room. ") : "", help ? tr(help) : "", ap);
        if (lock) col = C_LOCK;
        break;
    }
    }
    ui_no_gt(t);
    if (name[0]) text_m(CAT_X0, Y_HELP - 2, name, 0.55f, 0xFFFFFFFF);
    (rtl ? pc_ui_text_wrap_r : pc_ui_text_wrap)(rtl ? 640 - CAT_X0 - w : CAT_X0, Y_HELP + 16, w, t, 0.42f, col, 3);
    if (!st.capture && !st.list && !(st.search && st.typing)) draw_preview(r);
}

/* the panel (darkened under everything, rules, the category list's divider), the breadcrumb (root's words, ">" between,
   then path's) and the category list: the categories listed(), cur on a plate with a gold edge (-1 none) */
static void frame(const char* root, const char* const* path, int n, int (*listed)(int), int cur)
{
    char t[64];
    int at = 0;
    float x = CAT_X0;
    pc_ui_rect(0, 62, 640, 480, 0xB8000000, Z_PANEL);
    rect_m(CAT_X0, 60, ROWS_X1 + 2, 61, 0x80FFFFFF, Z_PANEL + 0.001f);
    rect_m(CAT_X0, Y_HELP - 6, ROWS_X1 + 2, Y_HELP - 5, 0x80FFFFFF, Z_PANEL + 0.001f);
    rect_m(CAT_X0, Y_BAR - 6, ROWS_X1 + 2, Y_BAR - 5, 0x80FFFFFF, Z_PANEL + 0.001f);
    rect_m(CAT_X1 + 4, 66, CAT_X1 + 5, Y_HELP - 12, 0x60FFFFFF, Z_PANEL + 0.001f);
    for (const char* p = root; *p;) {   /* (a root of several words: "MULTIPLAYER>LOBBY>GAME SETUP") */
        char part[64];
        int len = (int)strcspn(p, ">");
        snprintf(part, sizeof(part), "%.*s", len, p);
        text_m(x, 44, tr(part), 0.45f, C_ROW);
        x += pc_ui_text_w(tr(part), 0.45f);
        p += len;
        if (*p) { p++; pc_ui_arrow(mx(x + 12, 0), 51, rtl ? -1 : 1, C_ROW); x += 20; }
    }
    for (int i = 0; i < n; i++) {
        float w = pc_ui_text_w(tr(path[i]), 0.45f);
        pc_ui_arrow(mx(x + 12, 0), 51, rtl ? -1 : 1, C_ROW);
        x += 20;
        if (x + w > ROWS_X1) { text_fit(x, 44, tr(path[i]), 0.45f, ROWS_X1 - x, C_ROW); break; }   /* (a long last one: scaled to fit) */
        text_m(x, 44, tr(path[i]), 0.45f, C_ROW);
        x += w;
    }
    for (int c = 0; c < MC_COUNT; c++) {
        float y;
        if (!listed(c)) continue;
        y = Y0 + at++ * CAT_H;
        if (c == cur) {
            rect_m(CAT_X0, y - 2, CAT_X1, y + CAT_H - 3, 0x40FFFFFF, Z_HI);
            rect_m(CAT_X0, y - 2, CAT_X0 + 3, y + CAT_H - 3, C_GOLD, Z_HI + 0.01f);
            text_fit(CAT_X0 + 9, y, tr(menu_cats[c].name), SC_CAT, CAT_X1 - CAT_X0 - 12, 0xFFFFFFFF);
        } else text_fit(CAT_X0 + 9, y, tr(menu_cats[c].name), SC_CAT, CAT_X1 - CAT_X0 - 12, C_ROW);
    }
    if (at > 1) glyph_run(ui_glyph(UG_CAT, t, sizeof(t)), CAT_X0 + 9, Y_HELP - 30, 1);   /* what switches them */
}
static int cat_hit(float x, float y, int (*listed)(int))   /* x: left to right */
{
    int at = 0;
    if (x < CAT_X0 || x > CAT_X1) return -1;
    for (int c = 0; c < MC_COUNT; c++) {
        float cy;
        if (!listed(c)) continue;
        cy = Y0 + at++ * CAT_H;
        if (y >= cy - 2 && y < cy + CAT_H - 2) return c;
    }
    return -1;
}
/* ... for the IMPORT screen (import_menu.c; mirrored in Arabic as the rows are) */
static int import_listed(int c) { return menu_cats[c].screen == MS_IMPORT && menu_cats[c].parent < 0; }
void ui_frame_import(const char* root, const char* const* path, int n, int cur) { rtl = lang_rtl_text(); frame(root, path, n, import_listed, cur); }
int  ui_frame_import_cat(float x, float y) { return cat_hit(x, y, import_listed); }   /* x: left to right */

static void draw(void)
{
    char t[256];
    {
        const char* path[DEPTH + 2];
        char q[64];
        int n = 0;
        if (st.search) {   /* SEARCH: the text (a cursor while it is typed), no category on its plate */
            snprintf(q, sizeof(q), "%s %s%s", tr("SEARCH:"), st.query, st.typing && (SDL_GetTicks() / 400 & 1) ? "_" : "");
            path[n++] = q;
        }
        for (int i = 0; i < st.depth && !st.search; i++) path[n++] = menu_cats[st.stack[i]].name;
        if (!st.search || st.depth) path[n++] = menu_cats[st.cat].name;
        frame(st.root, path, n, cat_listed, menu_cats[st.cat].screen == st.screen && !st.search ? (st.depth ? st.stack[0] : st.cat) : -1);
    }
    if (st.edit) {   /* the character grid draws itself over this (text_grid.c): the help line alone */
        char nm[96];
        text_m(CAT_X0, Y_HELP - 2, tr(row_label(&rows[SEL], nm, sizeof(nm))), 0.55f, 0xFFFFFFFF);
        (rtl ? pc_ui_text_wrap_r : pc_ui_text_wrap)(rtl ? 640 - ROWS_X1 : CAT_X0, Y_HELP + 16, ROWS_X1 - CAT_X0,
            tr("Choose characters on the grid, or type them. Start or Enter: OK; Cancel or Esc: leave it as it was."), 0.42f, 0xFFFFE080, 3);
        AdvEasySetTextureList(0);
        return;
    }
    if (st.confirm || st.keep) ;   /* (the question alone: the font's text would show through its box) */
    else if (st.list) {   /* a list in the rows' place, its name above */
        const char* h = st.list == 3 ? "GIVE ITEM" : st.list == 2 ? "ENEMY RATES" : rows[SEL].idx == 2 ? "MONSTER LOOKS" : rows[SEL].idx ? "YOUR RANDOM LOOKS" : "STORY CHARACTER LOOKS";
        text_m(ROWS_X0 + 4, Y0 - 2, h, 0.5f, C_GOLD);
        if (st.list == 3) give_list_draw(); else if (st.list == 2) rate_list_draw(); else cast_list_draw();
    } else {
        for (int i = TOP; i < nrows && i < TOP + VIS; i++) draw_row(i, i == SEL);
        if (nrows > VIS) {   /* the scroll bar */
            float h = (Y_HELP - 12 - Y0) * VIS / nrows, y = Y0 + (Y_HELP - 12 - Y0 - h) * TOP / (nrows - VIS);
            rect_m(ROWS_X1 + 1, Y0, ROWS_X1 + 3, Y_HELP - 12, 0x40FFFFFF, Z_HI);
            rect_m(ROWS_X1 + 1, y, ROWS_X1 + 3, y + h, 0xC0FFFFFF, Z_HI + 0.01f);
        }
    }
    if (!st.confirm && !st.keep) draw_help();
    bar_build();
    bar_draw();
    if (st.keep) {
        float y0, y1;
        char k[96];
        pc_ui_dialog_place("Keep these settings?", keep_text(k, sizeof(k)), 0.5f, 1, 0, &y0, &y1);
        pc_ui_dialog(y0, y1, 0.4f, "Keep these settings?", keep_text(k, sizeof(k)), 0.5f, C_HELP, 1, "KEEP", "REVERT", st.keep_sel, 0);
    }
    if (st.confirm) {
        float y0, y1;
        snprintf(t, sizeof(t), tr("Reset %s?"), tr(menu_cats[st.cat].name));
        pc_ui_dialog_place(t, "Every setting of this category goes back to its default.", 0.5f, 2, 0, &y0, &y1);
        pc_ui_dialog(y0, y1, 0.4f, t, "Every setting of this category goes back to its default.", 0.5f, C_HELP, 2, "YES", "NO", !st.confirm_yes, 0);
    }
    AdvEasySetTextureList(0);   /* the host's textures again (the font set its own) */
}

/* ---------------------------------------------------------------- input */
static int key_edge(int sc)
{
    const bool* k = bind_keys();
    int down = k && k[sc], was = st.keys_prev[sc];
    st.keys_prev[sc] = (unsigned char)down;
    return down && !was;
}

static int row_at(float x, float y)   /* x: left to right */
{
    if (x < ROWS_X0 || x > ROWS_X1 + 4) return -1;
    for (int i = TOP; i < nrows && i < TOP + VIS; i++)
        if (y >= row_y(i) - 2 && y < row_y(i) + ROW_H - 2) return heading(&rows[i]) ? -1 : i;
    return -1;
}
static int cat_at(float x, float y) { return cat_hit(x, y, cat_listed); }

static int defaultable(const Row* r) { return r->kind == R_ITEM && r->m->kind != ML_PAGE && r->m->kind != ML_SHORTCUT && r->m->kind != ML_ACTION && r->m->kind != ML_IMPORT; }

static void do_default(const Row* r)
{
    int k = defaultable(r) ? reset_row(r) : 0;
    if (k > 0) { settings_save_later(); CallSystemSe(0, 3); say("Back to its default."); }
    else CallSystemSe(0, 1);
}

static int input(int port)
{
    unsigned int press = Pad[port].press, rept = Pad[port].Rept, on = Pad[port].on;
    unsigned int ok = st.ok | P_START, cancel = st.cancel | P_SELECT;
    int esc = key_edge(SC_ESC), del = key_edge(SC_DEL), pgup = key_edge(SC_PGUP), pgdn = key_edge(SC_PGDN), home = key_edge(SC_HOME), end = key_edge(SC_END);
    int slash = key_edge(SC_SLASH), find = key_edge(SC_F), ctrl = bind_keys() && (bind_keys()[SC_LCTRL] || bind_keys()[SC_RCTRL]);
    int hit, c;
    float x;
    menu_mouse_take(&mt);
    x = mx(mt.x, 0);
    ui_device(press, mt.moved || mt.left || mt.right || mt.wheel || esc || del || pgup || pgdn);
    if (st.capture) {
        int code = bind_capture_poll();
        if (code != -1) capture_end(code);
        return UR_STAY;
    }
    if (st.wait_release) {
        if (bind_any_down() || pad_report_bits() || g_menu_mouse.held) return UR_STAY;
        st.wait_release = 0;
    }
    if (st.confirm) {   /* RESET CATEGORY's question: NO first */
        float y0, y1;
        char t[128];
        snprintf(t, sizeof(t), tr("Reset %s?"), tr(menu_cats[st.cat].name));
        pc_ui_dialog_place(t, "Every setting of this category goes back to its default.", 0.5f, 2, 0, &y0, &y1);
        hit = pc_ui_dialog_hit(y1, "YES", "NO", 0, mt.x, mt.y);
        if (hit >= 0) hit = !hit;
        if (hit >= 0 && (mt.moved || mt.left) && hit != st.confirm_yes) { st.confirm_yes = hit; CallSystemSe(0, 2); }
        if (press & (P_LEFT | P_RIGHT)) { st.confirm_yes ^= 1; CallSystemSe(0, 2); }
        else if ((mt.left && hit >= 0) || (press & ok)) { st.confirm = 0; CallSystemSe(0, st.confirm_yes ? 3 : 0); if (st.confirm_yes) reset_category(); }
        else if (mt.right || esc || (press & cancel)) { st.confirm = 0; CallSystemSe(0, 0); }
        return UR_STAY;
    }
    if (st.keep) {   /* "Keep these settings?": REVERT first; Cancel reverts */
        float y0, y1;
        char t[96];
        pc_ui_dialog_place("Keep these settings?", keep_text(t, sizeof(t)), 0.5f, 1, 0, &y0, &y1);
        hit = pc_ui_dialog_hit(y1, "KEEP", "REVERT", 0, mt.x, mt.y);
        if (hit >= 0 && (mt.moved || mt.left) && hit != st.keep_sel) { st.keep_sel = hit; CallSystemSe(0, 2); }
        if (press & (P_LEFT | P_RIGHT)) { st.keep_sel ^= 1; CallSystemSe(0, 2); }
        else if ((mt.left && hit >= 0) || (press & ok)) { CallSystemSe(0, st.keep_sel ? 0 : 3); keep_end(!st.keep_sel); }
        else if (mt.right || esc || (press & cancel)) { CallSystemSe(0, 0); keep_end(0); }
        return UR_STAY;
    }
    if (st.list) {   /* its BACK, cancel or a right click: the rows again */
        if (esc) press |= cancel;
        if ((st.list == 3 ? give_list_input : st.list == 2 ? rate_list_input : cast_list_input)(&mt, press, rept, ok, cancel)) st.list = 0;
        return UR_STAY;
    }
    if (st.search && st.typing) {   /* typing the search (the keyboard's ring); a click on a result goes to it */
        search_typed();
        if (!(st.search && st.typing)) return UR_STAY;
        hit = row_at(x, mt.y);
        c = mt.left ? bar_hit(x, mt.y) : 0;
        if ((mt.left && hit >= 0) || c == UG_OK) { st.typing = 0; g_lobby_io.text_on = 0; if (hit >= 0) SEL = hit; CallSystemSe(0, 2); }
        else if (mt.right || c == UG_BACK) { search_end(); CallSystemSe(0, 0); }
        return UR_STAY;
    }
    if (st.screen == MS_SETTINGS && st.only < 0 && !st.depth && (slash || (find && ctrl))) { search_begin(); return UR_STAY; }
    if (st.search && !st.depth) {   /* the results: Back closes the search, a category switch too */
        if (esc || mt.right || (press & cancel)) { search_end(); CallSystemSe(0, 0); return UR_STAY; }
        if (press & (P_L1 | P_R1)) search_end();
    }
    /* hold Triangle for half a second, or Delete: the row's default */
    if (on & st.def) {
        if (!st.hold) st.hold = (int)SDL_GetTicks() | 1;
        if (!st.held_done && SDL_GetTicks() - (uint64_t)st.hold >= 500) { st.held_done = 1; do_default(&rows[SEL]); }
    } else st.hold = 0, st.held_done = 0;
    if (del) { do_default(&rows[SEL]); return UR_STAY; }
    /* the mouse */
    hit = row_at(x, mt.y);
    if (hit >= 0 && (mt.moved || mt.left) && hit != SEL) { SEL = hit; CallSystemSe(0, 2); }
    if (rows[SEL].kind == R_BIND && hit == SEL && (mt.moved || mt.left)) st.slot = x >= 416 + 98;
    if (mt.wheel) move_sel(mt.wheel > 0 ? -1 : 1, 1);
    if (mt.left) {
        int b = bar_hit(x, mt.y);
        if ((c = cat_at(x, mt.y)) >= 0) {
            if (c != st.cat || st.depth) { st.depth = 0; set_cat(c); CallSystemSe(0, 2); }
            return UR_STAY;
        }
        if (b == UG_OK) return activate(&rows[SEL]);
        if (b == UG_BACK) return back() ? UR_BACK : UR_STAY;
        if (b == UG_DEFAULT) { do_default(&rows[SEL]); return UR_STAY; }
        if (b == UG_CAT) { next_cat(1); return UR_STAY; }
        if (b == UG_SEARCH) { search_begin(); return UR_STAY; }
        if (hit >= 0) {
            const Row* r = &rows[hit];
            const Setting* rs = row_set(r);
            int arrows = r->kind == R_ITEM && ((r->m->kind == ML_SET && rs && !(rs->flags & SET_ACTION) && !(rs->count && st.picker)) || r->m->kind == ML_OUTPUT || r->m->kind == ML_PRESET
                                               || virt(r->m)) && !seed_row(r);
            if (arrows && x >= X_VALUE - 16) change(r, x < X_VALUE + 4 ? -1 : 1);
            else return activate(r);
        }
        return UR_STAY;
    }
    if (mt.right || esc) return back() ? UR_BACK : UR_STAY;
    if (press & P_L1) next_cat(-1);
    else if (press & P_R1) next_cat(1);
    else if (rept & P_UP) move_sel(-1, 1);
    else if (rept & P_DOWN) move_sel(1, 1);
    else if (pgup || home) { for (int k = 0; k < (home ? nrows : VIS - 1) && SEL > 0; k++) move_sel(-1, 0); if (heading(&rows[SEL])) move_sel(1, 0); CallSystemSe(0, 2); }
    else if (pgdn || end) { for (int k = 0; k < (end ? nrows : VIS - 1) && SEL < nrows - 1; k++) move_sel(1, 0); CallSystemSe(0, 2); }
    else if (rept & P_LEFT) change(&rows[SEL], -1);
    else if (rept & P_RIGHT) change(&rows[SEL], 1);
    else if (press & ok) return activate(&rows[SEL]);
    else if (press & cancel) return back() ? UR_BACK : UR_STAY;
    return UR_STAY;
}

int ui_rows_run(int port, int flag)
{
    int r = UR_STAY;
    buttons();
    if (st.imp) {   /* the IMPORT screen in place (its own rows, help and bar; not mirrored) */
        if (import_menu_run(port, flag)) {
            const bool* k = bind_keys();
            st.imp = 0;
            st.hold = 0; st.held_done = 1;
            for (int i = 0; i < 80; i++) st.keys_prev[i] = k && k[i];   /* (its Esc is no Back here too) */
        }
        AdvEasySetTextureList(0);
        return UR_STAY;
    }
    rtl = lang_rtl_text();
    if (st.edit) {   /* the character grid over the panel (it takes the input; the rows' text would show through its box) */
        int k;
        draw();
        if (flag && (k = tg_run(port))) edit_end(k > 0);
        return UR_STAY;
    }
    g_lobby_io.list_on = st.list;   /* (Page Up / Down, Home, End in the lists) */
    if (st.keep && !keep_left()) keep_end(0);   /* (no answer: the display mode may not show anything) */
    build_rows();
    if (flag) r = input(port);
    else { menu_mouse_take(&mt); for (int k = 0; k < 80; k++) st.keys_prev[k] = 0; }
    build_rows();
    scroll_to_sel();
    draw();
    if (r != UR_STAY && r != UR_PICK && r != UR_FLOW) { st.capture = 0; st.wait_release = 0; st.list = 0; if (st.edit) edit_end(0); }
    if (!st.list) g_lobby_io.list_on = 0;
    if (r == UR_BACK) host_log("menu: back from %s\n", menu_cats[st.cat].name);
    return r;
}

void ui_rows_close(void)
{
    if (st.any_pending)
        for (int i = 0; i < g_settings_n && i < (int)sizeof(st.pending); i++)
            if (st.pending[i]) { st.pending[i] = 0; settings_act(&g_settings[i]); }
    st.any_pending = 0;
    st.capture = 0;
    st.list = 0;
    give_list_release();   /* (the item icons, if Give Item loaded them) */
    st.confirm = 0;
    if (st.edit) edit_end(0);
    if (st.imp) { import_menu_shut(); st.imp = 0; }
    if (st.keep) keep_end(0);
    search_end();
}

int ui_rows_window(void) { int w = st.window; st.window = 0; return w; }
