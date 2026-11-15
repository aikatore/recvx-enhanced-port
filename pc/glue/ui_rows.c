/* The new menu's row screen (--new-menu; docs/MENUS.md, menu_layout.h): the category list on the left, the rows on the
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
#include <stdio.h>
#include <string.h>
#include <stdint.h>

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
uint64_t SDL_GetTicks(void);

enum { P_UP = 0x1000, P_RIGHT = 0x2000, P_DOWN = 0x4000, P_LEFT = 0x8000, P_START = 0x800, P_SELECT = 0x100, P_L1 = 0x4, P_R1 = 0x8,
       P_TRI = 0x10, P_CIRCLE = 0x20, P_CROSS = 0x40, P_SQUARE = 0x80 };   /* Pad[] bits */
enum { SC_ESC = 41, SC_HOME = 74, SC_PGUP = 75, SC_DEL = 76, SC_END = 77, SC_PGDN = 78 };   /* SDL scancodes (bind_keys) */

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
enum { R_ITEM, R_GROUP, R_BIND, R_BIND_RESET, R_RATES, R_CUSTOM, R_RESET_CAT };
typedef struct Row { int kind; const MenuRow* m; int set; int idx; } Row;   /* idx: R_GROUP's text index, R_BIND's bind row, R_CUSTOM's list */
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
    int list;                         /* 1 the cast's Custom list, 2 the enemy rates */
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
} st = { .last = { -1, -1, -1 } };

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
static int heading(const Row* r) { return r->kind == R_GROUP; }

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

static const char* text_value(int which) { return which ? (g_coop.server[0] ? g_coop.server : tr("None")) : g_coop.name; }

/* the value shown (buf) or NULL for none */
static const char* value_text(const Row* r, char* buf, int n)
{
    const MenuRow* m = r->m;
    const Setting* s = row_set(r);
    switch (m->kind) {
    case ML_SET: case ML_SHORTCUT:
        if (!s || (s->flags & SET_ACTION)) return NULL;
        if (m->flags & MLF_VIBRATION) {
            int v = vib_get();
            if (v) snprintf(buf, n, "%d%%", v); else snprintf(buf, n, "%s", tr("Off"));
            return buf;
        }
        if (m->names && !s->name) { int v = settings_get(s) - s->lo; snprintf(buf, n, "%s", tr(m->names[v < 0 ? 0 : v])); return buf; }
        return settings_value(s, buf, n);
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
    case ML_SET:
        if (!s || !menu_default(r->set, &d)) return 0;
        if (r->m->flags & MLF_VIBRATION) return vib_get() != d;
        return settings_get(s) != d;
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
    if (s && settings_locked(s)) { say(settings_locked(s)); return -1; }
    switch (r->m->kind) {
    case ML_SET:
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
    if (m->kind == ML_RETIRED) return 0;
    if (set >= 0 && g_settings[set].shown && !g_settings[set].shown() && !(m->flags & MLF_ALWAYS)) return 0;
    return 1;
}

static void build_rows(void)
{
    const char* g = NULL;   /* a heading on a hidden row: over the next shown one */
    nrows = 0;
    if (is_bind_cat(st.cat)) {
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
            rows[nrows++] = (Row){ R_ITEM, m, set, 0 };
            if (m->kind != ML_SET || set < 0) continue;   /* "Custom": its list's row under it */
            if (g_settings[set].val == &g_enemy_opts.variety && g_enemy_opts.variety == 2) rows[nrows++] = (Row){ R_RATES, m, -1, 0 };
            if (g_settings[set].val == &g_enemy_opts.cast_all && g_enemy_opts.cast_all == 2) rows[nrows++] = (Row){ R_CUSTOM, m, -1, 0 };
            if (g_settings[set].val == &g_enemy_opts.cast_enemy && g_enemy_opts.cast_enemy == CAST_ENEMY_CUSTOM) rows[nrows++] = (Row){ R_CUSTOM, m, -1, 2 };
        }
        if (menu_cats[st.cat].parent < 0 && menu_cats[st.cat].screen != MS_CHEATS) rows[nrows++] = (Row){ R_RESET_CAT, NULL, -1, 0 };
    }
    if (st.sel[st.cat] >= nrows) st.sel[st.cat] = nrows - 1;
    if (st.sel[st.cat] < 0) st.sel[st.cat] = 0;
    while (st.sel[st.cat] < nrows - 1 && heading(&rows[st.sel[st.cat]])) st.sel[st.cat]++;
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
static int cat_listed(int c) { return menu_cats[c].screen == st.screen && menu_cats[c].parent < 0; }

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

void ui_rows_open(int screen, const char* root)
{
    st.screen = screen;
    st.root = root;
    st.depth = 0;
    if (st.last[screen] < 0) {
        for (int c = 0; c < MC_COUNT; c++) if (cat_listed(c)) { st.last[screen] = c; break; }
    }
    st.cat = st.last[screen];
    st.capture = st.wait_release = st.list = st.confirm = 0;
    st.hold = 0; st.held_done = 1;   /* (a button still down from the screen before) */
    st.msg_t = 0;
}
int ui_rows_screen(void) { return st.screen; }

/* ---------------------------------------------------------------- changing and running */
static void change(const Row* r, int dir)
{
    const Setting* s = row_set(r);
    if (r->kind == R_BIND) { st.slot = (dir > 0) != rtl; CallSystemSe(0, 2); return; }
    if (r->kind != R_ITEM) return;
    switch (r->m->kind) {
    case ML_SET:
        if (!s || (s->flags & SET_ACTION)) return;
        if (settings_locked(s)) { CallSystemSe(0, 1); say(settings_locked(s)); return; }
        if (r->m->flags & MLF_VIBRATION) {
            int v = vib_get() + dir * s->step;
            if (!vib_unit()) { CallSystemSe(0, 1); say("No controller with vibration is connected."); return; }
            vib_set(s, v < 0 ? 0 : v > s->hi ? s->hi : v);
        } else settings_step(s, dir);
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
    switch (r->m->kind) {
    case ML_PAGE: enter(r->m->arg); return UR_STAY;
    case ML_SHORTCUT: enter(r->m->arg); return UR_STAY;
    case ML_SCREEN: CallSystemSe(0, 3); return UR_LEVEL + r->m->arg;
    case ML_WINDOW: case ML_TEXT: st.window = r->m->kind == ML_TEXT ? 3 : r->m->arg; CallSystemSe(0, 3); return UR_CLOSE;
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
        if (settings_is_bool(s) || s->names || s->name) { settings_step(s, 1); CallSystemSe(0, 2); }
        else CallSystemSe(0, 1);   /* numbers: left/right */
        return UR_STAY;
    }
    return UR_STAY;
}

static void reset_category(void)
{
    int n = 0, locked = 0;
    for (int i = 0; i < menu_rows_n; i++) {
        const MenuRow* m = &menu_rows[i];
        Row r = { R_ITEM, m, setting_of(m), 0 };
        if (m->kind == ML_RETIRED || m->kind == ML_PRESET || top_cat(m->cat) != st.cat) continue;
        { int k = reset_row(&r); n += k > 0; locked += k < 0; }
    }
    settings_save_later();
    if (locked) say("Some settings cannot change now and were kept.");
    else say(n ? "Category reset to defaults." : "Nothing to reset: every setting is at its default.");
}

/* ---------------------------------------------------------------- the button bar (also the pause menu's) */
typedef struct { const char* glyph; const char* word; int action; } Hint;
static Hint hints[4];
static int nhints;
static float hint_x0[4], hint_x1[4];

static const char* pad_glyph(unsigned int mask, char* buf, int n)
{
    static const struct { unsigned int bit; int row; } m[] = { { P_CROSS, B_CROSS }, { P_CIRCLE, B_CIRCLE }, { P_SQUARE, B_SQUARE }, { P_TRI, B_TRIANGLE },
                                                              { P_L1, B_L1 }, { P_R1, B_R1 } };
    for (int i = 0; i < (int)(sizeof(m) / sizeof(m[0])); i++)
        if (mask & m[i].bit) return bind_code_name(BIND_PAD, bind_get(m[i].row, BIND_PAD, 0), buf, n);
    return "?";
}

/* the buttons of this key type: Confirm, Cancel, and the one that holds for Default: Triangle, the only face button no key
   type uses in menus (Square confirms in types A, C and D, cancels in B) */
static void buttons(void)
{
    st.ok = (sys->keytype & 3) < 3 ? AdvGetOkButton() : 0xC0;
    st.cancel = (sys->keytype & 3) < 3 ? AdvGetCancelButton() : 0x20;
    st.def = P_TRI;
}

/* the device used last: a press says which (pad.c), the mouse or a menu key means the keyboard */
void ui_device(unsigned int press, int kb_used)
{
    if (press) st.pad = pad_last_pad();
    if (kb_used) st.pad = 0;
}

const char* ui_glyph(int which, char* buf, int n)
{
    char a[32], b[32];
    buttons();
    switch (which) {
    case UG_OK:
        if (st.pad) return pad_glyph(st.ok, buf, n);
        bind_code_name(BIND_KB, bind_get(B_START, BIND_KB, 0), buf, n);
        return strcmp(buf, "Return") ? buf : "Enter";
    case UG_BACK: return st.pad ? pad_glyph(st.cancel, buf, n) : "Esc";
    case UG_DEFAULT: return st.pad ? pad_glyph(st.def, buf, n) : "Del";
    case UG_CAT:
        if (st.pad) snprintf(buf, n, "%s %s", pad_glyph(P_L1, a, sizeof(a)), pad_glyph(P_R1, b, sizeof(b)));
        else snprintf(buf, n, "%s %s", bind_code_name(BIND_KB, bind_get(B_L1, BIND_KB, 0), a, sizeof(a)), bind_code_name(BIND_KB, bind_get(B_R1, BIND_KB, 0), b, sizeof(b)));
        return buf;
    case UG_CHANGE:
        if (st.pad) return tr("D-pad");
        snprintf(buf, n, "%s %s", bind_code_name(BIND_KB, bind_get(B_LEFT, BIND_KB, 0), a, sizeof(a)), bind_code_name(BIND_KB, bind_get(B_RIGHT, BIND_KB, 0), b, sizeof(b)));
        return buf;
    }
    return "";
}

/* one hint at x (left to right): the button on a plate, its word; fill: how far a hold has got (0..1); returns the next x */
float ui_hint(float x, float y, const char* glyph, const char* word, float fill)
{
    float gw = pc_ui_text_w(glyph, 0.42f) + 10, ww = pc_ui_text_w(word, 0.5f);
    if (fill > 0) rect_m(x, y, x + gw * (fill > 1 ? 1 : fill), y + 15, C_GOLD, Z_HI + 0.02f);
    rect_m(x, y, x + gw, y + 15, 0xFF505050, Z_HI + 0.01f);
    text_m(x + 5, y + 1, glyph, 0.42f, 0xFFFFFFFF);
    text_m(x + gw + 5, y - 1, word, 0.5f, C_HELP);
    return x + gw + 5 + ww + 18;
}

static void bar_build(void)
{
    static char g[4][40];
    nhints = 0;
    if (st.capture) return;
    hints[nhints++] = (Hint){ ui_glyph(UG_OK, g[0], 40), "Select", UG_OK };
    hints[nhints++] = (Hint){ ui_glyph(UG_BACK, g[1], 40), "Back", UG_BACK };
    if (!st.list) hints[nhints++] = (Hint){ ui_glyph(UG_DEFAULT, g[2], 40), st.pad ? "Hold: Default" : "Default", UG_DEFAULT };
    if (!st.list && !st.depth) hints[nhints++] = (Hint){ ui_glyph(UG_CAT, g[3], 40), "Category", UG_CAT };
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

static const char* row_label(const Row* r, char* buf, int n)
{
    const MenuRow* m = r->m;
    switch (r->kind) {
    case R_BIND: return bind_row_label(r->idx, buf, n);
    case R_BIND_RESET: return "Reset Bindings";
    case R_RATES: return "Enemy Rates";
    case R_CUSTOM: return r->idx ? "Monster Looks" : "Story Character Looks";
    case R_RESET_CAT: return "Reset Category";
    case R_GROUP:
        if (!m) {   /* a binding group (idx 100 + BG_*) */
            if (r->idx - 100 == BG_GAME) { snprintf(buf, n, tr("GAME (CONTROL TYPE %c)"), 'A' + (sys->keytype & 3)); return buf; }
            return r->idx - 100 == BG_TPS ? (st.cat == MC_BIND_PAD ? "THIRD PERSON: MODERN CONTROLLER" : "THIRD PERSON: KEYBOARD AND MOUSE") : "HOTKEYS";
        }
        return m->group;
    }
    if (m->label) return m->label;
    if (m->ref) return m->ref;
    return "";
}

static void draw_row(int i, int sel)
{
    const Row* r = &rows[i];
    const Setting* s = row_set(r);
    const char* lock = s ? settings_locked(s) : NULL;
    float y = row_y(i);
    unsigned int col = sel ? 0xFFFFFFFF : C_ROW;
    char label[256], val[160];
    const char* v;
    if (lock) col = sel ? 0xFF9A9A9A : C_DIM;
    if (r->kind == R_GROUP) { text_m(ROWS_X0 + 4, y + 2, row_label(r, label, sizeof(label)), 0.5f, C_GOLD); return; }
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
    {   /* (a restart mark after the label, before it right to left) */
        const char* l = tr(row_label(r, val, sizeof(val))), *mark = s && (s->flags & SET_RESTART) ? tr("(restart)") : "";
        if (rtl) snprintf(label, sizeof(label), "%s%s%s", mark, *mark ? " " : "", l);
        else snprintf(label, sizeof(label), "%s%s%s", l, *mark ? " " : "", mark);
    }
    text_fit(X_LABEL, y, label, SC_ROW, X_VALUE - X_LABEL - 16, col);
    if (r->kind == R_RATES || r->kind == R_CUSTOM) {
        if (r->kind == R_RATES) rate_list_summary(g_enemy_opts.rate, val, sizeof(val));
        else if (r->idx) cast_list_summary(2, g_enemy_opts.cast_enemy_off, 0, val, sizeof(val));
        else cast_list_summary(0, cast_off, g_enemy_opts.cast_imports ? __atomic_load_n(&actor_import_n, __ATOMIC_ACQUIRE) : 0, val, sizeof(val));
        text_fit(X_VALUE, y, val, SC_ROW, VALUE_W, col);
        pc_ui_arrow(mx(ROWS_X1 - 6, 0), y + 9, rtl ? -1 : 1, col);
        return;
    }
    if (r->kind != R_ITEM) return;
    if (s && (s->flags & SET_ACTION)) {
        if (r->set < (int)sizeof(st.pending) && st.pending[r->set]) text_m(X_VALUE, y + 1, pc_in_game() ? "(on resume)" : "(on leaving menu)", 0.48f, C_GOLD);
        return;
    }
    v = value_text(r, val, sizeof(val));
    switch (r->m->kind) {
    case ML_PAGE: case ML_SHORTCUT: case ML_SCREEN: case ML_WINDOW: case ML_TEXT:   /* opens something: an arrow at the end */
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
static void draw_preview(const Row* r)
{
    const Setting* s = row_set(r);
    char a[96], b[96], t[160];
    int d, has_d;
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
        int hi = settings_hi(s), cur = settings_get(s);
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
        int lo = s->lo, hi = settings_hi(s), v = settings_get(s);
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
    } else if (st.msg_t > 0) { st.msg_t--; snprintf(t, sizeof(t), "%s", st.msg); col = 0xFFFFE080; }
    else if (st.list) { snprintf(t, sizeof(t), "%s", tr(st.list == 2 ? rate_list_help() : cast_list_help())); name[0] = 0; }
    else switch (r->kind) {
    case R_BIND: {
        int dev = st.cat == MC_BIND_PAD, c = bind_conflict(r->idx, dev);
        if (c) { snprintf(t, sizeof(t), tr("Also assigned to %s. Change either binding to avoid a conflict."), tr(bind_rows[c - 1].name)); col = 0xFFFF8080; }
        else snprintf(t, sizeof(t), tr("Confirm: rebind. Left/right: choose a binding (%s)."), bind_code_name(dev, bind_get(r->idx, dev, st.slot), nm, sizeof(nm)));
        break;
    }
    case R_BIND_RESET: snprintf(t, sizeof(t), "%s", tr(st.cat == MC_BIND_PAD ? "Restore the default controller bindings." : "Restore the default keyboard and mouse bindings.")); break;
    case R_RATES: snprintf(t, sizeof(t), "%s", tr("Set how often each enemy type is chosen.")); break;
    case R_CUSTOM: snprintf(t, sizeof(t), "%s", tr(r->idx ? "Choose the enemies the monsters can appear as." : "Choose the models the characters can appear as.")); break;
    case R_RESET_CAT: snprintf(t, sizeof(t), tr("Put every setting of %s back to its default."), tr(menu_cats[st.cat].name)); break;
    case R_ITEM: {
        const char* lock = s ? settings_locked(s) : NULL;
        const char* help = r->m->help ? r->m->help : s ? s->help : NULL;
        char ap[160] = "";
        if (s && (s->flags & SET_ACTION) && !lock && !(s->flags & SET_NOW))
            snprintf(ap, sizeof(ap), "%s%s", help ? " " : "", tr(pc_in_game() ? "Runs when you resume." : "Applies when you leave this menu."));
        if (r->m->kind == ML_SCREEN && r->m->arg == 1 && pc_in_game()) lock = NULL;
        if (rtl) snprintf(t, sizeof(t), "%s%s%s%s%s", ap, help ? tr(help) : "", s && (s->flags & SET_RESTART) ? tr("Restart needed. ") : "", lock ? " " : "", lock ? lock : "");
        else snprintf(t, sizeof(t), "%s%s%s%s%s", lock ? lock : "", lock ? " " : "", s && (s->flags & SET_RESTART) ? tr("Restart needed. ") : "", help ? tr(help) : "", ap);
        if (lock) col = C_LOCK;
        break;
    }
    }
    if (name[0]) text_m(CAT_X0, Y_HELP - 2, name, 0.55f, 0xFFFFFFFF);
    (rtl ? pc_ui_text_wrap_r : pc_ui_text_wrap)(rtl ? 640 - CAT_X0 - w : CAT_X0, Y_HELP + 16, w, t, 0.42f, col, 3);
    if (!st.capture && !st.list) draw_preview(r);
}

static void draw(void)
{
    char t[256];
    int at = 0;
    /* the panel: darkened under everything, rules, the category list's divider */
    pc_ui_rect(0, 62, 640, 480, 0xB8000000, Z_PANEL);
    rect_m(CAT_X0, 60, ROWS_X1 + 2, 61, 0x80FFFFFF, Z_PANEL + 0.001f);
    rect_m(CAT_X0, Y_HELP - 6, ROWS_X1 + 2, Y_HELP - 5, 0x80FFFFFF, Z_PANEL + 0.001f);
    rect_m(CAT_X0, Y_BAR - 6, ROWS_X1 + 2, Y_BAR - 5, 0x80FFFFFF, Z_PANEL + 0.001f);
    rect_m(CAT_X1 + 4, 66, CAT_X1 + 5, Y_HELP - 12, 0x60FFFFFF, Z_PANEL + 0.001f);
    /* the breadcrumb: root, category, sub-page, with arrows between */
    {
        int path[DEPTH + 2], n = 0;
        float x = CAT_X0;
        for (int i = 0; i < st.depth; i++) path[n++] = st.stack[i];
        path[n++] = st.cat;
        text_m(x, 44, st.root, 0.45f, C_ROW);
        x += pc_ui_text_w(st.root, 0.45f);
        for (int i = 0; i < n; i++) {
            pc_ui_arrow(mx(x + 12, 0), 51, rtl ? -1 : 1, C_ROW);
            x += 20;
            text_m(x, 44, menu_cats[path[i]].name, 0.45f, C_ROW);
            x += pc_ui_text_w(menu_cats[path[i]].name, 0.45f);
        }
    }
    /* the category list: this screen's top categories; the one shown (or its parent) on a plate with a gold edge */
    for (int c = 0; c < MC_COUNT; c++) {
        float y;
        if (!cat_listed(c)) continue;
        y = Y0 + at++ * CAT_H;
        if (c == (st.depth ? st.stack[0] : st.cat) && menu_cats[st.cat].screen == st.screen) {
            rect_m(CAT_X0, y - 2, CAT_X1, y + CAT_H - 3, 0x40FFFFFF, Z_HI);
            rect_m(CAT_X0, y - 2, CAT_X0 + 3, y + CAT_H - 3, C_GOLD, Z_HI + 0.01f);
            text_fit(CAT_X0 + 9, y, tr(menu_cats[c].name), SC_CAT, CAT_X1 - CAT_X0 - 12, 0xFFFFFFFF);
        } else text_fit(CAT_X0 + 9, y, tr(menu_cats[c].name), SC_CAT, CAT_X1 - CAT_X0 - 12, C_ROW);
    }
    if (at > 1) {   /* what switches them */
        char a[24], b[24];
        if (st.pad) snprintf(t, sizeof(t), "%s / %s", pad_glyph(P_L1, a, sizeof(a)), pad_glyph(P_R1, b, sizeof(b)));
        else snprintf(t, sizeof(t), "%s / %s", bind_code_name(BIND_KB, bind_get(B_L1, BIND_KB, 0), a, sizeof(a)), bind_code_name(BIND_KB, bind_get(B_R1, BIND_KB, 0), b, sizeof(b)));
        text_m(CAT_X0 + 9, Y_HELP - 30, t, 0.45f, 0xFF808080);
    }
    if (st.confirm) ;   /* (the question alone: the font's text would show through its box) */
    else if (st.list) {   /* a list in the rows' place, its name above */
        const char* h = st.list == 2 ? "ENEMY RATES" : rows[SEL].idx ? "MONSTER LOOKS" : "STORY CHARACTER LOOKS";
        text_m(ROWS_X0 + 4, Y0 - 2, h, 0.5f, C_GOLD);
        if (st.list == 2) rate_list_draw(); else cast_list_draw();
    } else {
        for (int i = TOP; i < nrows && i < TOP + VIS; i++) draw_row(i, i == SEL);
        if (nrows > VIS) {   /* the scroll bar */
            float h = (Y_HELP - 12 - Y0) * VIS / nrows, y = Y0 + (Y_HELP - 12 - Y0 - h) * TOP / (nrows - VIS);
            rect_m(ROWS_X1 + 1, Y0, ROWS_X1 + 3, Y_HELP - 12, 0x40FFFFFF, Z_HI);
            rect_m(ROWS_X1 + 1, y, ROWS_X1 + 3, y + h, 0xC0FFFFFF, Z_HI + 0.01f);
        }
    }
    if (!st.confirm) draw_help();
    bar_build();
    bar_draw();
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
static int cat_at(float x, float y)
{
    int at = 0;
    if (x < CAT_X0 || x > CAT_X1) return -1;
    for (int c = 0; c < MC_COUNT; c++) {
        float cy;
        if (!cat_listed(c)) continue;
        cy = Y0 + at++ * CAT_H;
        if (y >= cy - 2 && y < cy + CAT_H - 2) return c;
    }
    return -1;
}

static int defaultable(const Row* r) { return r->kind == R_ITEM && r->m->kind != ML_PAGE && r->m->kind != ML_SHORTCUT; }

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
    if (st.list) {   /* its BACK, cancel or a right click: the rows again */
        if (esc) press |= cancel;
        if ((st.list == 2 ? rate_list_input : cast_list_input)(&mt, press, rept, ok, cancel)) st.list = 0;
        return UR_STAY;
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
        if (hit >= 0) {
            const Row* r = &rows[hit];
            const Setting* rs = row_set(r);
            int arrows = r->kind == R_ITEM && ((r->m->kind == ML_SET && rs && !(rs->flags & SET_ACTION)) || r->m->kind == ML_OUTPUT || r->m->kind == ML_PRESET);
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
    rtl = lang_rtl_text();
    buttons();
    g_lobby_io.list_on = st.list;   /* (Page Up / Down, Home, End in the lists) */
    build_rows();
    if (flag) r = input(port);
    else { menu_mouse_take(&mt); for (int k = 0; k < 80; k++) st.keys_prev[k] = 0; }
    build_rows();
    scroll_to_sel();
    draw();
    if (r != UR_STAY) { st.capture = 0; st.wait_release = 0; st.list = 0; }
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
    st.confirm = 0;
}

int ui_rows_window(void) { int w = st.window; st.window = 0; return w; }
