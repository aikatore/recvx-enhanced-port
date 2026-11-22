/* The cutscene cast's "Custom" list (docs/ACTORS.md "Cutscene cast"): the "All models" entries in the menus' order
 * (cast.h cast_custom_list; the imported ones of the games checked), each with a check box, SELECT ALL and DESELECT ALL
 * above them, BACK below; above the entries a check box per imported game with models (cast_list_games: Umbrella Chronicles,
 * Outbreak File 2, Code: Veronica (Dreamcast); actors.h g_import_hidden, the setting IMPORT's "Show in Model Lists", GAME
 * SETUP > CHARACTERS and the look picker show too): only a checked game's models are listed and in the random pools. Drawn as the lobby's model page (lobby_glue.c pick_*), with its helpers
 * (pc_ui_*), by the lobby's CUTSCENE CAST page and the BIORAND screen's RANDOMIZER page, each at its own place.
 * It edits cast_off (ours: recvx.ini cast_custom_off; a co-op host's go to the guest, coop_glue.c), or, opened for
 * "Randomize Character Model" (the RANDOMIZER page's CHARACTER MODEL, list 1), its own list cast_appear_off (recvx.ini
 * appear_custom_off; its "All models" entries), or for the cast's "Enemy Models" (list 2)
 * its list g_enemy_opts.cast_enemy_off (recvx.ini enemy_custom_off; the monsters, the zombies and the characters, sent with the host's
 * enemy options). Game thread. */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/sdfunc.h"
#include "../src/host/actors.h"
#include "../src/host/biorand_opts.h"
#include "../src/host/cast.h"
#include "../src/host/enemies.h"
#include "../src/host/lobby.h"
#include "../src/host/menu_mouse.h"
#include "../src/host/settings.h"
#include "../src/host/lang.h"
#include "../src/import/import.h"
#include <stdio.h>
#include <string.h>

void pc_ui_text(float x, float y, const char* s, float sc, unsigned int argb);
float pc_ui_text_w(const char* s, float sc);
void pc_ui_rect(float x0, float y0, float x1, float y1, unsigned int argb, float z);
void host_log(const char* fmt, ...);

enum { BTN_L1 = 0x4, BTN_R1 = 0x8, BTN_TRI = 0x10, BTN_UP = 0x1000, BTN_RIGHT = 0x2000, BTN_DOWN = 0x4000, BTN_LEFT = 0x8000 };   /* Pad[] bits */
#define Z 0.3f
#define BAR 7.0f                        /* the scroll bar's width, at the right edge */
#define GAP 6.0f
#define NONE_X 124.0f                   /* DESELECT ALL, right of SELECT ALL */
/* the cursor: C_ALL, C_NONE (one row), the rows C_LIST + r (the imported games' check boxes r < g, then entry r - g), then BACK
   (C_LIST + g + n) */
enum { C_ALL, C_NONE, C_LIST };

/* ---------------------------------------------------------------- the imported games' check boxes (also the look picker's,
   lobby_glue.c, and GAME SETUP > CHARACTERS', setup_glue.c SV_GAME_*): one setting, actors.h g_import_hidden (ini
   import_<key>_shown), unchecked at first */
static const int game_order[] = { IMPORT_UMBRELLA, IMPORT_OUTBREAK2, IMPORT_DREAMCAST };
#define GAMES ((int)(sizeof(game_order) / sizeof(game_order[0])))
int cast_list_games(int* src)
{
    unsigned have = actor_import_sources();
    int n = 0;
    for (int i = 0; i < GAMES; i++) if ((have >> game_order[i]) & 1) src[n++] = game_order[i];
    return n;
}
const char* cast_list_game_name(int s) { return s == IMPORT_UMBRELLA ? "Umbrella Chronicles" : s == IMPORT_OUTBREAK2 ? "Outbreak File 2" : "Code: Veronica (Dreamcast)"; }
int cast_list_game_on(int s) { return s >= 0 && s < 32 && !((g_import_hidden >> s) & 1); }
void cast_list_game_set(int s, int on)
{
    if (s < 0 || s >= IMPORT_SOURCES) return;
    if (on) g_import_hidden &= ~(1u << s); else g_import_hidden |= 1u << s;
    settings_save_later();
    host_log("lists: imported game %s %s\n", import_sources[s].key, on ? "checked: its models listed and in the random pools" : "unchecked");
}

/* a check box at x, y (a row's top) */
static void box(float x, float y, int on, unsigned int col)
{
    pc_ui_rect(x, y + 3, x + 12, y + 15, col, Z);
    pc_ui_rect(x + 1, y + 4, x + 11, y + 14, 0xFF202024, Z + 0.001f);
    if (on) pc_ui_rect(x + 3, y + 6, x + 9, y + 12, 0xFFFFE080, Z + 0.002f);
}

/* game s's row at y (x0 .. right; sel: under the cursor; first: the page's or the games' first row, which names the group) */
void cast_list_game_draw(float x0, float right, float y, float h, float sc, int s, int sel, int first)
{
    const char* nm = cast_list_game_name(s);
    int on = cast_list_game_on(s);
    unsigned int col = sel ? 0xFFFFFFFF : on ? 0xFFD0D0D0 : 0xFF808080;
    float lim = right, w, k;
    if (first) {
        float gw = pc_ui_text_w("Imported Games", 0.35f);
        pc_ui_text(right - gw - 2, y + 4, "Imported Games", 0.35f, 0xFFC8A860);
        lim = right - gw - 10;
    }
    if (sel) pc_ui_rect(x0 - 8, y - 2, right, y + h - 3, 0x50FFFFFF, Z);
    box(x0, y, on, col);
    w = pc_ui_text_w(nm, sc);
    k = w > lim - x0 - 20 ? sc * (lim - x0 - 20) / w : sc;
    pc_ui_text(x0 + 20, y + (sc - k) * 14, nm, k, col);
}
const char* cast_list_game_help(void)
{
    return "Check a game to list its imported characters and use them for random looks and cutscenes. In co-op, both players need the same games checked.";
}

/* ---------------------------------------------------------------- the list */
static short pos[CAST_CUSTOM_MAX];
static int model[CAST_CUSTOM_MAX];
static struct { int n, g, src[GAMES], cur, top, rows, list; float x0, x1, y0, h, sc; } cl;   /* list: 0 the cast's, 1 "Randomize Character Model"'s,
    2 "Enemy Models"'; g: the games' rows above the n entries (src, none for list 2: no imports) */

static int imports(void) { return cl.list != 2 ? __atomic_load_n(&actor_import_n, __ATOMIC_ACQUIRE) : 0; }   /* (the checked games' only: cast_custom_list) */
static unsigned char* off(void) { return cl.list == 2 ? g_enemy_opts.cast_enemy_off : cl.list ? cast_appear_off : cast_off; }
int cast_list_kind(void) { return cl.list; }
static void refresh(void) { cl.g = imports() ? cast_list_games(cl.src) : 0; cl.n = cast_custom_list(cl.list, imports(), pos, model, CAST_CUSTOM_MAX); }
static int rows_n(void) { return cl.g + cl.n; }
static float list_y(void) { return cl.y0 + cl.h + GAP; }
static float back_y(void) { return list_y() + cl.rows * cl.h + GAP; }
static int max_top(void) { return rows_n() > cl.rows ? rows_n() - cl.rows : 0; }

static void scroll(int d);
void cast_list_open(int list, float x0, float x1, float y0, float h, float sc, int rows)
{
    memset(&cl, 0, sizeof(cl));
    cl.list = list;
    cl.x0 = x0; cl.x1 = x1; cl.y0 = y0; cl.h = h; cl.sc = sc; cl.rows = rows;
    refresh();
    cl.cur = cl.n ? C_LIST + cl.g : cl.g ? C_LIST : C_ALL;   /* (the first entry, the games above it in view) */
    scroll(0);
    CallSystemSe(0, 3);
}

/* the first shown row moved by d (clamped), a row cursor kept on the page */
static void scroll(int d)
{
    int was = cl.cur, k;
    cl.top += d;
    if (cl.top > max_top()) cl.top = max_top();
    if (cl.top < 0) cl.top = 0;
    k = cl.cur - C_LIST;
    if (k >= 0 && k < rows_n()) cl.cur = C_LIST + (k < cl.top ? cl.top : k >= cl.top + cl.rows ? cl.top + cl.rows - 1 : k);
    if (cl.cur != was) CallSystemSe(0, 2);
}

/* the cursor to c (wrapping, or clamped to the rows); the page follows it */
static void to(int c, int wrap)
{
    int lo = C_ALL, last = C_LIST + rows_n(), k;
    if (wrap) c = lo + ((c - lo) % (last - lo + 1) + (last - lo + 1)) % (last - lo + 1);
    else c = c < C_LIST ? C_LIST : c >= last ? last - 1 : c;
    if (c != cl.cur) CallSystemSe(0, 2);
    cl.cur = c;
    k = c - C_LIST;
    if (k >= 0 && k < rows_n() && k < cl.top) cl.top = k;
    if (k >= 0 && k < rows_n() && k >= cl.top + cl.rows) cl.top = k - cl.rows + 1;
    scroll(0);
}

static void all(int v)
{
    for (int k = 0; k < cl.n; k++) cast_off_put(off(), pos[k], v);
    settings_save_later();
    CallSystemSe(0, 3);
}

/* confirm on c: 1 BACK */
static int act(int c)
{
    int r = c - C_LIST;
    if (c == C_ALL || c == C_NONE) { all(c == C_NONE); return 0; }
    if (r >= rows_n()) { CallSystemSe(0, 0); return 1; }
    if (r < cl.g) {   /* a game: its models into the list and the pools, or out */
        cast_list_game_set(cl.src[r], !cast_list_game_on(cl.src[r]));
        CallSystemSe(0, 3);
        refresh();
        scroll(0);
        return 0;
    }
    cast_off_put(off(), pos[r - cl.g], !cast_off_at(off(), pos[r - cl.g]));
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
    if (y >= back_y() - 3 && y < back_y() + cl.h - 3) return C_LIST + rows_n();
    if (y < list_y() - 3) return -1;
    i = (int)((y - list_y() + 3) / cl.h);
    return i < cl.rows && cl.top + i < rows_n() ? C_LIST + cl.top + i : -1;
}

int cast_list_input(const MouseTake* m, unsigned int press, unsigned int rept, unsigned int ok, unsigned int cancel)
{
    int page = cl.rows - 1, hit;
    refresh();
    if (cl.cur > C_LIST + rows_n()) cl.cur = C_LIST + rows_n();   /* (the list got shorter) */
    while (g_lobby_io.tail != __atomic_load_n(&g_lobby_io.head, __ATOMIC_ACQUIRE)) {   /* lobby_io.c: list_on */
        int k = g_lobby_io.keys[g_lobby_io.tail & 63];
        g_lobby_io.tail++;
        if (k == LK_PGUP) to(cl.cur - page, 0);
        else if (k == LK_PGDN) to(cl.cur + page, 0);
        else if (k == LK_HOME) to(C_LIST, 0);
        else if (k == LK_END) to(C_LIST + rows_n() - 1, 0);
    }
    if (m->wheel) scroll(-3 * m->wheel);   /* > 0: away from the player, up the list */
    if (m->left && rows_n() > cl.rows && m->x >= cl.x1 - BAR - 4 && m->x <= cl.x1 + 4 && m->y >= list_y() - 2 && m->y < list_y() + cl.rows * cl.h) {
        scroll((int)((m->y - list_y()) / (cl.rows * cl.h) * (max_top() + 1)) - cl.top);   /* a click on the scroll bar: that part */
        return 0;
    }
    hit = at(m->x, m->y);
    if (hit >= 0 && (m->moved || m->left || m->wheel) && hit != cl.cur) { cl.cur = hit; CallSystemSe(0, 2); }
    if (m->left && hit >= 0) return act(hit);
    if (m->right || (press & cancel)) { CallSystemSe(0, 0); return 1; }
    if ((press & BTN_TRI) && imports() && actor_import_sources()) {   /* every imported game out of the list or back (actors.h) */
        appear_sources_toggle();
        settings_save_later();
        CallSystemSe(0, 3);
        refresh();
        if (cl.cur > C_LIST + rows_n()) cl.cur = C_LIST + rows_n();
        scroll(0);
        return 0;
    }
    if (rept & BTN_UP) to(cl.cur == C_LIST ? C_ALL : cl.cur - 1 - (cl.cur == C_NONE), 1);   /* (SELECT ALL and DESELECT ALL: one row) */
    else if (rept & BTN_DOWN) to(cl.cur < C_LIST ? C_LIST : cl.cur + 1, 1);
    else if ((rept & (BTN_LEFT | BTN_RIGHT)) && cl.cur < C_LIST) { cl.cur ^= 1; CallSystemSe(0, 2); }
    else if ((rept & (BTN_L1 | BTN_LEFT)) && cl.cur >= C_LIST) to(cl.cur - page, 0);
    else if ((rept & (BTN_R1 | BTN_RIGHT)) && cl.cur >= C_LIST) to(cl.cur + page, 0);
    else if (press & ok) return act(cl.cur);
    return 0;
}

void cast_list_draw(void)
{
    int r, on;
    float right, y;
    char t[48];
    refresh();
    on = cast_custom_count(cl.list, off(), imports(), NULL);
    right = rows_n() > cl.rows ? cl.x1 - BAR - 6 : cl.x1;
    snprintf(t, sizeof(t), "%d/%d", on, cl.n);
    for (r = C_ALL; r <= C_NONE; r++) {   /* SELECT ALL, DESELECT ALL */
        float x = cl.x0 + (r == C_NONE ? NONE_X : 0);
        const char* s = r == C_NONE ? "DESELECT ALL" : "SELECT ALL";
        if (cl.cur == r) pc_ui_rect(x - 8, cl.y0 - 2, x + pc_ui_text_w(s, cl.sc) + 8, cl.y0 + cl.h - 3, 0x50FFFFFF, Z);
        pc_ui_text(x, cl.y0, s, cl.sc, cl.cur == r ? 0xFFFFFFFF : 0xFFB0B0B0);
    }
    pc_ui_text(cl.x1 - pc_ui_text_w(t, 0.45f), cl.y0 + 2, t, 0.45f, on ? 0xFFA0A0A0 : 0xFFFFC060);
    for (r = cl.top; r < rows_n() && r < cl.top + cl.rows; r++) {
        int k = r - cl.g;
        y = list_y() + (r - cl.top) * cl.h;
        if (k < 0) { cast_list_game_draw(cl.x0, right, y, cl.h, cl.sc, cl.src[r], cl.cur == C_LIST + r, r == cl.top || r == 0); continue; }
        const char* nm = cast_entry_name(model[k]);
        const char* grp = cast_entry_group(model[k]);
        int sel = cl.cur == C_LIST + r, chk = !cast_off_at(off(), pos[k]);
        unsigned int col = sel ? 0xFFFFFFFF : chk ? 0xFFD0D0D0 : 0xFF808080;
        float lim = right, w, sc;
        if (r == cl.top || k == 0 || strcmp(grp, cast_entry_group(model[k - 1]))) {   /* a group's first row (and the page's): its name on the right */
            float gw = pc_ui_text_w(grp, 0.35f);
            if (r > cl.top) pc_ui_rect(cl.x0 - 8, y - 3, right, y - 2, 0x60FFE080, Z);
            pc_ui_text(right - gw - 2, y + 4, grp, 0.35f, 0xFFC8A860);
            lim = right - gw - 10;
        }
        if (sel) pc_ui_rect(cl.x0 - 8, y - 2, right, y + cl.h - 3, 0x50FFFFFF, Z);
        box(cl.x0, y, chk, col);
        w = pc_ui_text_w(nm, cl.sc);
        sc = w > lim - cl.x0 - 20 ? cl.sc * (lim - cl.x0 - 20) / w : cl.sc;   /* a long name: narrower */
        pc_ui_text(cl.x0 + 20, y + (cl.sc - sc) * 14, nm, sc, col);
    }
    if (rows_n() > cl.rows) {   /* the scroll bar: the page's share of the list, where it is */
        float y0 = list_y() - 2, h = cl.rows * cl.h - 1, th = h * cl.rows / rows_n(), ty;
        if (th < 12) th = 12;
        ty = y0 + (h - th) * cl.top / max_top();
        pc_ui_rect(cl.x1 - BAR, y0, cl.x1 - BAR + 5, y0 + h, 0x40FFFFFF, Z);
        pc_ui_rect(cl.x1 - BAR, ty, cl.x1 - BAR + 5, ty + th, 0xC0FFFFFF, Z + 0.001f);
    }
    y = back_y();
    if (cl.cur >= C_LIST + rows_n()) pc_ui_rect(cl.x0 - 8, y - 2, cl.x1, y + cl.h - 3, 0x50FFFFFF, Z);
    pc_ui_text(cl.x0, y, "BACK", cl.sc, cl.cur >= C_LIST + rows_n() ? 0xFFFFFFFF : 0xFFB0B0B0);
}

const char* cast_list_help(void)
{
    if (cl.cur == C_ALL) return "Check every model in the list.";
    if (cl.cur == C_NONE) return cl.list == 2 ? "Uncheck every enemy. With none checked, the monsters keep their own looks."
                                              : "Uncheck every model. With none checked, the characters keep their own looks.";
    if (cl.cur >= C_LIST + rows_n()) return "Back. Your choices are saved.";
    if (cl.cur < C_LIST + cl.g) return cast_list_game_help();
    if (cl.list == 2) return "Monsters seen only in cutscenes appear only as the enemies you check. Confirm or click: check or uncheck.";
    if (imports() && actor_import_sources())
        return "Characters appear only as the models you check. Confirm or click: check or uncheck. Triangle: hide or show the imported models.";
    return "Characters appear only as the models you check. Confirm or click: check or uncheck. Scroll with L1/R1 or the wheel.";
}

/* "12 of 25": the checked ones of off (list 0, 1, 2 as cast_list_open's) */
const char* cast_list_summary(int list, const unsigned char* off, int imports_n, char* buf, int n)
{
    int total, on = cast_custom_count(list, off, imports_n, &total);
    snprintf(buf, (size_t)n, tr("%d of %d"), on, total);
    return buf;
}
