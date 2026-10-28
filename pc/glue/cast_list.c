/* The cutscene cast's "Custom" list (docs/ACTORS.md "Cutscene cast"): the "All models" entries in the menus' order
 * (cast.h cast_custom_list; the imported ones too with "Include Imported Models"), each with a check box, SELECT ALL
 * and DESELECT ALL above them, BACK below. Drawn as the lobby's model page (lobby_glue.c pick_*), with its helpers
 * (pc_ui_*), by the lobby's CUTSCENE CAST page and the BIORAND screen's EXTRA OPTIONS page, each at its own place.
 * It edits cast_off (ours: recvx.ini cast_custom_off; a co-op host's go to the guest, coop_glue.c). Game thread. */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/sdfunc.h"
#include "../src/host/actors.h"
#include "../src/host/cast.h"
#include "../src/host/enemies.h"
#include "../src/host/lobby.h"
#include "../src/host/menu_mouse.h"
#include "../src/host/settings.h"
#include "../src/host/lang.h"
#include <stdio.h>
#include <string.h>

void pc_ui_text(float x, float y, const char* s, float sc, unsigned int argb);
float pc_ui_text_w(const char* s, float sc);
void pc_ui_rect(float x0, float y0, float x1, float y1, unsigned int argb, float z);

enum { BTN_L1 = 0x4, BTN_R1 = 0x8, BTN_UP = 0x1000, BTN_RIGHT = 0x2000, BTN_DOWN = 0x4000, BTN_LEFT = 0x8000 };   /* Pad[] bits */
#define Z 0.3f
#define BAR 7.0f                        /* the scroll bar's width, at the right edge */
#define GAP 6.0f
#define NONE_X 124.0f                   /* DESELECT ALL, right of SELECT ALL */
/* the cursor: C_ALL, C_NONE (one row), the entries C_LIST + k, then BACK (C_LIST + n) */
enum { C_ALL, C_NONE, C_LIST };

static short pos[CAST_CUSTOM_MAX];
static int model[CAST_CUSTOM_MAX];
static struct { int n, cur, top, rows; float x0, x1, y0, h, sc; } cl;

static int imports(void) { return g_enemy_opts.cast_imports ? __atomic_load_n(&actor_import_n, __ATOMIC_ACQUIRE) : 0; }
static void refresh(void) { cl.n = cast_custom_list(imports(), pos, model, CAST_CUSTOM_MAX); }
static float list_y(void) { return cl.y0 + cl.h + GAP; }
static float back_y(void) { return list_y() + cl.rows * cl.h + GAP; }
static int max_top(void) { return cl.n > cl.rows ? cl.n - cl.rows : 0; }

void cast_list_open(float x0, float x1, float y0, float h, float sc, int rows)
{
    memset(&cl, 0, sizeof(cl));
    cl.x0 = x0; cl.x1 = x1; cl.y0 = y0; cl.h = h; cl.sc = sc; cl.rows = rows;
    refresh();
    cl.cur = cl.n ? C_LIST : C_ALL;
    CallSystemSe(0, 3);
}

/* the first shown entry moved by d (clamped), an entry cursor kept on the page */
static void scroll(int d)
{
    int was = cl.cur, k;
    cl.top += d;
    if (cl.top > max_top()) cl.top = max_top();
    if (cl.top < 0) cl.top = 0;
    k = cl.cur - C_LIST;
    if (k >= 0 && k < cl.n) cl.cur = C_LIST + (k < cl.top ? cl.top : k >= cl.top + cl.rows ? cl.top + cl.rows - 1 : k);
    if (cl.cur != was) CallSystemSe(0, 2);
}

/* the cursor to c (wrapping, or clamped to the entries); the page follows it */
static void to(int c, int wrap)
{
    int last = C_LIST + cl.n, k;
    if (wrap) c = (c + last + 1) % (last + 1);
    else c = c < C_LIST ? C_LIST : c >= last ? last - 1 : c;
    if (c != cl.cur) CallSystemSe(0, 2);
    cl.cur = c;
    k = c - C_LIST;
    if (k >= 0 && k < cl.n && k < cl.top) cl.top = k;
    if (k >= 0 && k < cl.n && k >= cl.top + cl.rows) cl.top = k - cl.rows + 1;
    scroll(0);
}

static void all(int off)
{
    for (int k = 0; k < cl.n; k++) cast_off_put(cast_off, pos[k], off);
    settings_save_later();
    CallSystemSe(0, 3);
}

/* confirm on c: 1 BACK */
static int act(int c)
{
    if (c == C_ALL || c == C_NONE) { all(c == C_NONE); return 0; }
    if (c >= C_LIST + cl.n) { CallSystemSe(0, 0); return 1; }
    cast_off_put(cast_off, pos[c - C_LIST], !cast_off_at(cast_off, pos[c - C_LIST]));
    settings_save_later();
    CallSystemSe(0, 3);
    return 0;
}

/* the cursor position under the pointer, -1 none */
static int at(float x, float y)
{
    int i;
    if (x < cl.x0 - 8 || x > cl.x1) return -1;
    if (y >= cl.y0 - 3 && y < cl.y0 + cl.h - 3) return x < cl.x0 + NONE_X - 12 ? C_ALL : x < cl.x0 + NONE_X + 140 ? C_NONE : -1;
    if (y >= back_y() - 3 && y < back_y() + cl.h - 3) return C_LIST + cl.n;
    if (y < list_y() - 3) return -1;
    i = (int)((y - list_y() + 3) / cl.h);
    return i < cl.rows && cl.top + i < cl.n ? C_LIST + cl.top + i : -1;
}

int cast_list_input(const MouseTake* m, unsigned int press, unsigned int rept, unsigned int ok, unsigned int cancel)
{
    int page = cl.rows - 1, hit;
    refresh();
    if (cl.cur > C_LIST + cl.n) cl.cur = C_LIST + cl.n;   /* (the list got shorter) */
    while (g_lobby_io.tail != __atomic_load_n(&g_lobby_io.head, __ATOMIC_ACQUIRE)) {   /* lobby_io.c: list_on */
        int k = g_lobby_io.keys[g_lobby_io.tail & 63];
        g_lobby_io.tail++;
        if (k == LK_PGUP) to(cl.cur - page, 0);
        else if (k == LK_PGDN) to(cl.cur + page, 0);
        else if (k == LK_HOME) to(C_LIST, 0);
        else if (k == LK_END) to(C_LIST + cl.n - 1, 0);
    }
    if (m->wheel) scroll(-3 * m->wheel);   /* > 0: away from the player, up the list */
    if (m->left && cl.n > cl.rows && m->x >= cl.x1 - BAR - 4 && m->x <= cl.x1 + 4 && m->y >= list_y() - 2 && m->y < list_y() + cl.rows * cl.h) {
        scroll((int)((m->y - list_y()) / (cl.rows * cl.h) * (max_top() + 1)) - cl.top);   /* a click on the scroll bar: that part */
        return 0;
    }
    hit = at(m->x, m->y);
    if (hit >= 0 && (m->moved || m->left || m->wheel) && hit != cl.cur) { cl.cur = hit; CallSystemSe(0, 2); }
    if (m->left && hit >= 0) return act(hit);
    if (m->right || (press & cancel)) { CallSystemSe(0, 0); return 1; }
    if (rept & BTN_UP) to(cl.cur == C_LIST ? C_ALL : cl.cur - 1 - (cl.cur == C_NONE), 1);   /* (SELECT ALL and DESELECT ALL: one row) */
    else if (rept & BTN_DOWN) to(cl.cur < C_LIST ? C_LIST : cl.cur + 1, 1);
    else if ((rept & (BTN_LEFT | BTN_RIGHT)) && cl.cur < C_LIST) { cl.cur ^= 1; CallSystemSe(0, 2); }
    else if ((rept & (BTN_L1 | BTN_LEFT)) && cl.cur >= C_LIST) to(cl.cur - page, 0);
    else if ((rept & (BTN_R1 | BTN_RIGHT)) && cl.cur >= C_LIST) to(cl.cur + page, 0);
    else if (press & ok) return act(cl.cur);
    return 0;
}

/* a check box at x, y (a row's top) */
static void box(float x, float y, int on, unsigned int col)
{
    pc_ui_rect(x, y + 3, x + 12, y + 15, col, Z);
    pc_ui_rect(x + 1, y + 4, x + 11, y + 14, 0xFF202024, Z + 0.001f);
    if (on) pc_ui_rect(x + 3, y + 6, x + 9, y + 12, 0xFFFFE080, Z + 0.002f);
}

void cast_list_draw(void)
{
    int k, on = cast_custom_count(cast_off, imports(), NULL);
    float right = cl.n > cl.rows ? cl.x1 - BAR - 6 : cl.x1, y;
    char t[48];
    refresh();
    for (k = C_ALL; k <= C_NONE; k++) {   /* SELECT ALL, DESELECT ALL */
        float x = cl.x0 + (k == C_NONE ? NONE_X : 0);
        const char* s = k == C_NONE ? "DESELECT ALL" : "SELECT ALL";
        if (cl.cur == k) pc_ui_rect(x - 8, cl.y0 - 2, x + pc_ui_text_w(s, cl.sc) + 8, cl.y0 + cl.h - 3, 0x50FFFFFF, Z);
        pc_ui_text(x, cl.y0, s, cl.sc, cl.cur == k ? 0xFFFFFFFF : 0xFFB0B0B0);
    }
    snprintf(t, sizeof(t), "%d/%d", on, cl.n);
    pc_ui_text(cl.x1 - pc_ui_text_w(t, 0.45f), cl.y0 + 2, t, 0.45f, on ? 0xFFA0A0A0 : 0xFFFFC060);
    for (k = cl.top; k < cl.n && k < cl.top + cl.rows; k++) {
        const char* nm = cast_entry_name(model[k]);
        const char* grp = cast_entry_group(model[k]);
        int sel = cl.cur == C_LIST + k, chk = !cast_off_at(cast_off, pos[k]);
        unsigned int col = sel ? 0xFFFFFFFF : chk ? 0xFFD0D0D0 : 0xFF808080;
        float lim = right, w, sc;
        y = list_y() + (k - cl.top) * cl.h;
        if (k == cl.top || strcmp(grp, cast_entry_group(model[k - 1]))) {   /* a group's first row (and the page's): its name on the right */
            float gw = pc_ui_text_w(grp, 0.35f);
            if (k > cl.top) pc_ui_rect(cl.x0 - 8, y - 3, right, y - 2, 0x60FFE080, Z);
            pc_ui_text(right - gw - 2, y + 4, grp, 0.35f, 0xFFC8A860);
            lim = right - gw - 10;
        }
        if (sel) pc_ui_rect(cl.x0 - 8, y - 2, right, y + cl.h - 3, 0x50FFFFFF, Z);
        box(cl.x0, y, chk, col);
        w = pc_ui_text_w(nm, cl.sc);
        sc = w > lim - cl.x0 - 20 ? cl.sc * (lim - cl.x0 - 20) / w : cl.sc;   /* a long name: narrower */
        pc_ui_text(cl.x0 + 20, y + (cl.sc - sc) * 14, nm, sc, col);
    }
    if (cl.n > cl.rows) {   /* the scroll bar: the page's share of the list, where it is */
        float y0 = list_y() - 2, h = cl.rows * cl.h - 1, th = h * cl.rows / cl.n, ty;
        if (th < 12) th = 12;
        ty = y0 + (h - th) * cl.top / max_top();
        pc_ui_rect(cl.x1 - BAR, y0, cl.x1 - BAR + 5, y0 + h, 0x40FFFFFF, Z);
        pc_ui_rect(cl.x1 - BAR, ty, cl.x1 - BAR + 5, ty + th, 0xC0FFFFFF, Z + 0.001f);
    }
    y = back_y();
    if (cl.cur >= C_LIST + cl.n) pc_ui_rect(cl.x0 - 8, y - 2, cl.x1, y + cl.h - 3, 0x50FFFFFF, Z);
    pc_ui_text(cl.x0, y, "BACK", cl.sc, cl.cur >= C_LIST + cl.n ? 0xFFFFFFFF : 0xFFB0B0B0);
}

const char* cast_list_help(void)
{
    if (cl.cur == C_ALL) return "Check every model in the list.";
    if (cl.cur == C_NONE) return "Uncheck every model. With none checked, the characters keep their own looks.";
    if (cl.cur >= C_LIST + cl.n) return "Back. Your choices are saved.";
    return "Characters appear only as the models you check. Confirm or click: check or uncheck. Scroll with L1/R1 or the wheel.";
}

/* "12 of 25": the checked ones of off */
const char* cast_list_summary(const unsigned char* off, int imports_n, char* buf, int n)
{
    int total, on = cast_custom_count(off, imports_n, &total);
    snprintf(buf, (size_t)n, tr("%d of %d"), on, total);
    return buf;
}
