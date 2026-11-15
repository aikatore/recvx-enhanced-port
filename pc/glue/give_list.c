/* PAUSE > CHEATS AND TOOLS > Give Item (--new-menu; docs/MENUS.md): every item with a name, grouped (weapons,
 * ammunition, healing, the rest, key items), each with its icon as the item box shows it, in the rows' place of the
 * row screen (ui_rows.c st.list 3); on the right the chosen item large, its amount and the inventory as it is now.
 * Confirm (or a click) puts it into the inventory at once (cheat_glue.c cheat_give: the menu stays open; full: into
 * the item box, not in co-op), Left/Right the amount (ammunition, a weapon's rounds), L1/R1 the next group.
 *
 * Icons: the trade screen's (trade_glue.c pc_item_icons, user ICON_USER, shared with the spoiler log and the weapon
 * shortcuts): loaded at the list's first open in a pause menu, kept while the menu is up (opening the list again reuses
 * them), released when OPTION closes (give_list_release from ui_rows_close) or with every texture (njReleaseTextureAll).
 * Game thread. RECVX_GIVE_TEST=n (test aid): logs the texture entries in use and the RSS at each open. */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/item.h"
#include "../../decomp/include/ps2/veronica/prog/sdfunc.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_NaDraw.h"
#include "../src/host/cheat.h"
#include "../src/host/coop.h"
#include "../src/host/lang.h"
#include "../src/host/lobby.h"
#include "../src/host/menu_mouse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void host_log(const char* fmt, ...);
void pc_ui_text(float x, float y, const char* s, float sc, unsigned int argb);
float pc_ui_text_w(const char* s, float sc);
int pc_ui_text_wrap(float x, float y, float w, const char* s, float sc, unsigned int argb, int max_lines);
void pc_ui_rect(float x0, float y0, float x1, float y1, unsigned int argb, float z);
void pc_ui_arrow(float x, float y, int dir, unsigned int argb);
int pc_item_icons(int user, int on);
void pc_item_icon(unsigned int w, float x, float y, float sc, unsigned int argb, float z);
void ui_rows_say(const char* m);

enum { BTN_L1 = 0x4, BTN_R1 = 0x8, BTN_UP = 0x1000, BTN_RIGHT = 0x2000, BTN_DOWN = 0x4000, BTN_LEFT = 0x8000 };   /* Pad[] bits */
#define ICON_USER 8                     /* pc_item_icons: 1 trade, 2 weapon shortcuts, 4 spoiler log */
#define BAR 7.0f                        /* the scroll bar, right of the list */
#define GAP 6.0f
#define LIST_W 286.0f                   /* the list; the detail panel right of it */
#define ICON_SC 0.42f                   /* a row's icon: 29x20 (a two-slot item 57x20) */
#define NAME_DX 66.0f
#define ZR(base, i) ((base) + ((gl.top & 1) * 16 + (i)) * 0.0004f)   /* one depth per row and scroll parity ("Smooth menus") */
#define Z_SEL 0.3f
#define Z_ICON 0.36f
#define C_GOLD 0xFFD6B668u
#define C_HELP 0xFFC8C8C2u

enum { G_WEAPON, G_AMMO, G_HEAL, G_OTHER, G_KEY, G_N };
static const char* const group_name[G_N] = { "WEAPONS", "AMMUNITION", "HEALING", "OTHER ITEMS", "KEY ITEMS" };

static struct {
    short row[CHEAT_ITEMS + G_N];       /* an item id, or -1 - group: a heading */
    int n, cur, top, rows;              /* cur: a row, or n (BACK) */
    int amount;                         /* the chosen item's */
    float x0, x1, y0, h;
    int icons;                          /* pc_item_icons said they are there */
} gl;

/* itemdata's type: 0x1 equipable, 0x2 loads a weapon, 0x8 used on the player (F. Aid Spray, the herbs), 0x40 a key item;
   -1: not listed (ids 152..155 are unused weapon slots whose message is no item name; "?" items have none either) */
static int group_of(int id)
{
    int t = itemdata[id].type;
    if (id >= 152 || !g_cheat.names[id][0] || !strcmp(g_cheat.names[id], "?")) return -1;
    if (t & 0x40) return G_KEY;
    if (t & 0x1) return G_WEAPON;
    if ((t & 0x2) && (t & 0x1800)) return G_AMMO;   /* (the Playing Manual is 0x2 without a count) */
    if (t & 0x8) return G_HEAL;
    return G_OTHER;
}
static int heading(int r) { return r < gl.n && gl.row[r] < 0; }
static int item_at_row(int r) { return r >= 0 && r < gl.n && gl.row[r] > 0 ? gl.row[r] : 0; }

/* the amount: the number the item's word carries (KazuSet shows it: weapons' rounds, ammunition), else 1 */
static int counted(int id) { return (itemdata[id].type & 0x1800) != 0; }
static int pct(int id) { return (itemdata[id].type & 0x1000) && itemdata[id].max > 0; }
static int amount_min(int id) { return (itemdata[id].type & 0x1) ? 0 : 1; }
static int amount_max(int id)
{
    int m = itemdata[id].max;
    if (!counted(id)) return 1;
    if (itemdata[id].type & 0x200) return 999;   /* (stacks) */
    return m > 0 ? m : 1;
}
static int amount_default(int id) { int m = itemdata[id].max; return !counted(id) ? 1 : m > 0 ? (m > 999 ? 999 : m) : 1; }
static const char* amount_text(int id, int v, char* b, int n)
{
    if (pct(id)) snprintf(b, n, "%d%%", (v * 100 + itemdata[id].max / 2) / itemdata[id].max);
    else snprintf(b, n, "%d", v);
    return b;
}

static void build(void)
{
    gl.n = 0;
    for (int g = 0; g < G_N; g++) {
        int head = gl.n;
        gl.row[gl.n++] = (short)(-1 - g);
        for (int id = 1; id < CHEAT_ITEMS; id++)
            if (group_of(id) == g) gl.row[gl.n++] = (short)id;
        if (gl.n == head + 1) gl.n--;   /* (an empty group: no heading) */
    }
}

static int max_top(void) { return gl.n > gl.rows ? gl.n - gl.rows : 0; }
static float back_y(void) { return gl.y0 + gl.rows * gl.h + GAP; }
static float list_x1(void) { return gl.x0 + LIST_W; }
static float panel_x0(void) { return list_x1() + BAR + 10; }

static void follow(void)
{
    int want = gl.cur > 0 && heading(gl.cur - 1) ? gl.cur - 1 : gl.cur;   /* (with its heading) */
    if (gl.cur < gl.n && want < gl.top) gl.top = want;
    if (gl.cur < gl.n && gl.cur >= gl.top + gl.rows) gl.top = gl.cur - gl.rows + 1;
    if (gl.top > max_top()) gl.top = max_top();
    if (gl.top < 0) gl.top = 0;
}

/* the cursor to row c (dir: the way past headings; wrap: through BACK to the other end, else clamped) */
static void to(int c, int dir, int wrap)
{
    int was = gl.cur, id;
    if (!gl.n) { gl.cur = 0; return; }
    if (wrap) c = (c % (gl.n + 1) + gl.n + 1) % (gl.n + 1);
    else c = c < 0 ? 0 : c >= gl.n ? gl.n - 1 : c;
    while (c < gl.n && heading(c)) {
        c += dir < 0 ? -1 : 1;
        if (c < 0) c = wrap ? gl.n : 1;
    }
    if (c > gl.n) c = gl.n;
    gl.cur = c;
    if (c != was) {
        CallSystemSe(0, 2);
        if ((id = item_at_row(c))) gl.amount = amount_default(id);
    }
    follow();
}

static void group_jump(int dir)   /* L1 / R1: the previous / next group's first item */
{
    int c = gl.cur >= gl.n ? gl.n - 1 : gl.cur, h, k;
    if (gl.n < 2) return;
    for (h = c; h > 0 && !heading(h); h--) ;   /* this group's heading */
    if (dir > 0) { for (k = c + 1; k < gl.n && !heading(k); k++) ; if (k >= gl.n) k = 0; }
    else if (c > h + 1) k = h;   /* not on its first item: to that */
    else { for (k = h - 1; k > 0 && !heading(k); k--) ; if (h == 0) for (k = gl.n - 1; k > 0 && !heading(k); k--) ; }
    to(k + 1, 1, 0);
    gl.top = k;   /* (its heading on top) */
    follow();
}

static void amount_step(int dir)
{
    int id = item_at_row(gl.cur), v, step;
    if (!id || amount_min(id) == amount_max(id)) { CallSystemSe(0, 1); return; }
    step = pct(id) ? (itemdata[id].max + 9) / 10 : 1;
    v = gl.amount + dir * step;
    v = v < amount_min(id) ? amount_min(id) : v > amount_max(id) ? amount_max(id) : v;
    if (v == gl.amount) return;
    gl.amount = v;
    CallSystemSe(0, 2);
}

static int rss_kb(void)
{
#ifdef __linux__
    int pages = 0, rss = 0;
    FILE* f = fopen("/proc/self/statm", "r");
    if (f) { if (fscanf(f, "%d %d", &pages, &rss) != 2) rss = 0; fclose(f); }
    return rss * 4;
#else
    return 0;
#endif
}
static int tex_used(void)   /* the game's texture entries in use */
{
    int n = 0;
    for (int i = 0; i < PC_TEX_ENTRIES; i++) n += tbuf[i].count != 0;
    return n;
}

void give_list_open(float x0, float x1, float y0, float h, int rows)
{
    static int opens;
    int keep = gl.cur, keep_top = gl.top;
    gl.x0 = x0; gl.x1 = x1; gl.y0 = y0; gl.h = h; gl.rows = rows;
    build();
    gl.cur = gl.top = 0;
    if (keep > 0 && keep < gl.n && !heading(keep)) { gl.cur = keep; gl.top = keep_top; }   /* (where it was) */
    else to(0, 1, 0);
    if (item_at_row(gl.cur)) gl.amount = amount_default(item_at_row(gl.cur));
    follow();
    gl.icons = pc_item_icons(ICON_USER, 1);
    opens++;
    host_log("give: list open (%d rows, icons %s)\n", gl.n, gl.icons ? "on" : "off");
    if (getenv("RECVX_GIVE_TEST") && opens == 1)
        for (int k = 0; k < gl.n; k++) host_log("give: test: row %d: %s\n", k, gl.row[k] < 0 ? group_name[-1 - gl.row[k]] : g_cheat.names[gl.row[k]]);
    if (getenv("RECVX_GIVE_TEST")) host_log("give: test: open %d: %d texture entries in use, rss %d kB\n", opens, tex_used(), rss_kb());
}

/* OPTION closed (ui_rows_close): the icons go unless another screen uses them */
void give_list_release(void) { pc_item_icons(ICON_USER, 0); gl.icons = 0; }

static void give(void)
{
    int id = item_at_row(gl.cur), r;
    char b[160], a[16];
    if (!id) return;
    r = cheat_give(id, gl.amount, 1);
    if (r == CHEAT_GIVE_FULL) {
        CallSystemSe(0, 1);
        ui_rows_say(coop_on ? "Inventory full. Make room first." : "Inventory and item box are full.");
        host_log("give: %d (%s) x%d: no room\n", id, g_cheat.names[id], gl.amount);
        return;
    }
    if (r == CHEAT_GIVE_BOX) snprintf(b, sizeof(b), tr("Inventory full: %s went into the item box."), g_cheat.names[id]);
    else if (counted(id)) snprintf(b, sizeof(b), tr("Added: %s x%s"), g_cheat.names[id], amount_text(id, gl.amount, a, sizeof(a)));
    else snprintf(b, sizeof(b), tr("Added: %s"), g_cheat.names[id]);
    CallSystemSe(0, 3);
    ui_rows_say(b);
    host_log("give: %d (%s) x%d: %s\n", id, g_cheat.names[id], gl.amount, r == CHEAT_GIVE_BOX ? "item box" : "inventory");
}

/* the row under the pointer (n: BACK), -1 none; the panel's amount arrows: -2 / -3 */
static float amount_y(void) { return gl.y0 + 82; }
static int at(float x, float y)
{
    int i;
    if (item_at_row(gl.cur) && y >= amount_y() - 4 && y < amount_y() + 18) {
        float px = panel_x0(), w = gl.x1 - px;
        if (x >= px && x < px + 22) return -2;
        if (x >= px + w - 22 && x < gl.x1 + 4) return -3;
    }
    if (x < gl.x0 - 8 || x > list_x1()) return -1;
    if (y >= back_y() - 3 && y < back_y() + gl.h - 3) return gl.n;
    if (y < gl.y0 - 3) return -1;
    i = (int)((y - gl.y0 + 3) / gl.h);
    return i < gl.rows && gl.top + i < gl.n && !heading(gl.top + i) ? gl.top + i : -1;
}

/* 1: BACK */
int give_list_input(const MouseTake* m, unsigned int press, unsigned int rept, unsigned int ok, unsigned int cancel)
{
    int hit, page = gl.rows - 1;
    while (g_lobby_io.tail != __atomic_load_n(&g_lobby_io.head, __ATOMIC_ACQUIRE)) {   /* lobby_io.c: list_on */
        int k = g_lobby_io.keys[g_lobby_io.tail & 63];
        g_lobby_io.tail++;
        if (k == LK_PGUP) to(gl.cur - page, -1, 0);
        else if (k == LK_PGDN) to(gl.cur + page, 1, 0);
        else if (k == LK_HOME) to(0, 1, 0);
        else if (k == LK_END) to(gl.n - 1, -1, 0);
    }
    if (m->wheel) {   /* the page scrolls, three rows a notch; the cursor stays on it */
        gl.top -= m->wheel * 3;
        gl.top = gl.top < 0 ? 0 : gl.top > max_top() ? max_top() : gl.top;
        if (gl.cur < gl.n && gl.cur < gl.top) to(gl.top, 1, 0);
        else if (gl.cur < gl.n && gl.cur >= gl.top + gl.rows) to(gl.top + gl.rows - 1, -1, 0);
        return 0;
    }
    hit = at(m->x, m->y);
    if (hit >= 0 && (m->moved || m->left) && hit != gl.cur) {
        int t = gl.top;
        to(hit, 1, 0);
        gl.top = t;   /* (a click never scrolls) */
    }
    if (m->left && hit == gl.n) { CallSystemSe(0, 0); return 1; }
    if (m->left && hit == -2) { amount_step(-1); return 0; }
    if (m->left && hit == -3) { amount_step(1); return 0; }
    if (m->left && hit >= 0) { give(); return 0; }
    if (m->right || (press & cancel)) { CallSystemSe(0, 0); return 1; }
    if (rept & BTN_UP) to(gl.cur - 1, -1, 1);
    else if (rept & BTN_DOWN) to(gl.cur + 1, 1, 1);
    else if (press & BTN_L1) group_jump(-1);
    else if (press & BTN_R1) group_jump(1);
    else if (rept & BTN_LEFT) amount_step(-1);
    else if (rept & BTN_RIGHT) amount_step(1);
    else if (press & ok) {
        if (gl.cur >= gl.n) { CallSystemSe(0, 0); return 1; }
        give();
    }
    return 0;
}

static void text_fit(float x, float y, const char* s, float sc, float w, unsigned int col)
{
    float tw = pc_ui_text_w(s, sc), k = tw > w && tw > 0 ? w / tw : 1.0f;
    pc_ui_text(x, y + (1.0f - k) * 9.0f, s, sc * k, col);
}

/* the inventory as it is now, two cells a row as the game keeps it (a two-slot item: a whole row) */
static void inventory(float x, float y)
{
    const unsigned int* inv = &sys->itm[(sys->ply_id > 2 ? 0 : sys->ply_id) * 16];
    int slots = (sys->gm_flg & 0x8000000) ? 10 : 8, c = 0;
    float cw = 34 * 1.0f, ch = 24;
    char b[16];
    for (int k = 0; k < slots; k++)
        pc_ui_rect(x + (k & 1) * (cw + 2), y + (k >> 1) * (ch + 2), x + (k & 1) * (cw + 2) + cw, y + (k >> 1) * (ch + 2) + ch, 0xC0102038, 0.33f);
    for (int i = 2; i < 2 + slots && c < slots; i++) {
        int id = (inv[i] >> 16) & 0xFF, two;
        float cx, cy;
        if (!id || id >= CHEAT_ITEMS) continue;
        two = (itemdata[id].type & 0x100) != 0;
        if (two && (c & 1)) c++;
        cx = x + (c & 1) * (cw + 2); cy = y + (c >> 1) * (ch + 2);
        if (gl.icons) pc_item_icon(inv[i], cx, cy, 0.5f, 0xFFFFFFFF, 0.37f + c * 0.0004f);
        if (counted(id)) { amount_text(id, inv[i] & 0xFFFF, b, sizeof(b)); pc_ui_text(cx + (two ? 2 * cw + 2 : cw) - pc_ui_text_w(b, 0.34f) - 1, cy + 12, b, 0.34f, 0xFF80FF80); }
        c += two ? 2 : 1;
    }
}

void give_list_draw(void)
{
    int id = item_at_row(gl.cur);
    float y, px = panel_x0(), pw = gl.x1 - px;
    char b[64];
    for (int k = gl.top; k < gl.n && k < gl.top + gl.rows; k++) {
        int i = k - gl.top, rid = gl.row[k], sel = gl.cur == k;
        y = gl.y0 + i * gl.h;
        if (rid < 0) {   /* a group's heading */
            pc_ui_text(gl.x0 - 4, y + 4, group_name[-1 - rid], 0.48f, C_GOLD);
            pc_ui_rect(gl.x0 - 4, y + gl.h - 4, list_x1(), y + gl.h - 3, 0x40D6B668, ZR(Z_SEL, i));
            continue;
        }
        if (sel) pc_ui_rect(gl.x0 - 8, y - 2, list_x1(), y + gl.h - 3, 0x50FFFFFF, ZR(Z_SEL, i));
        if (gl.icons) pc_item_icon((unsigned int)rid << 16, gl.x0, y - 1, ICON_SC, 0xFFFFFFFF, ZR(Z_ICON, i));
        else pc_ui_rect(gl.x0, y, gl.x0 + 28, y + gl.h - 5, 0x60303038, ZR(Z_ICON, i));
        text_fit(gl.x0 + NAME_DX, y + 2, g_cheat.names[rid], 0.52f, LIST_W - NAME_DX - 44, sel ? 0xFFFFFFFF : 0xFFC8C8C8);
        if (counted(rid)) {
            amount_text(rid, sel ? gl.amount : amount_default(rid), b, sizeof(b));
            pc_ui_text(list_x1() - 6 - pc_ui_text_w(b, 0.44f), y + 4, b, 0.44f, sel ? 0xFF80FF80 : 0xFF70A070);
        }
    }
    if (gl.n > gl.rows) {   /* the scroll bar */
        float y0 = gl.y0 - 2, h = gl.rows * gl.h - 1, th = h * gl.rows / gl.n, ty;
        if (th < 12) th = 12;
        ty = y0 + (h - th) * gl.top / max_top();
        pc_ui_rect(list_x1() + 2, y0, list_x1() + 2 + 5, y0 + h, 0x40FFFFFF, Z_SEL);
        pc_ui_rect(list_x1() + 2, ty, list_x1() + 2 + 5, ty + th, 0xC0FFFFFF, Z_SEL + 0.001f);
    }
    y = back_y();
    if (gl.cur >= gl.n) pc_ui_rect(gl.x0 - 8, y - 2, list_x1(), y + gl.h - 3, 0x50FFFFFF, Z_SEL + 0.02f);
    pc_ui_text(gl.x0, y + 2, "BACK", 0.55f, gl.cur >= gl.n ? 0xFFFFFFFF : 0xFFB0B0B0);

    /* the panel: the item as the inventory shows it, its name, its amount; the inventory */
    pc_ui_rect(px - 4, gl.y0 - 4, gl.x1 + 2, back_y() + gl.h - 3, 0x50000000, 0.25f);
    if (id) {
        int two = (itemdata[id].type & 0x100) != 0;
        float iw = two ? 136 : 68;
        if (gl.icons) pc_item_icon((unsigned int)id << 16, px + (pw - iw) / 2, gl.y0, 1.0f, 0xFFFFFFFF, 0.36f);
        pc_ui_text_wrap(px, gl.y0 + 52, pw - 4, g_cheat.names[id], 0.5f, 0xFFFFFFFF, 2);
        y = amount_y();
        if (counted(id)) {
            int lo = gl.amount > amount_min(id), hi = gl.amount < amount_max(id);
            amount_text(id, gl.amount, b, sizeof(b));
            pc_ui_arrow(px + 8, y + 9, -1, lo ? 0xFFFFFFFF : 0xFF505050);
            pc_ui_arrow(gl.x1 - 8, y + 9, 1, hi ? 0xFFFFFFFF : 0xFF505050);
            pc_ui_text(px + (pw - pc_ui_text_w(b, 0.6f)) / 2, y, b, 0.6f, 0xFF80FF80);
        }
    }
    y = amount_y() + 26;
    pc_ui_text(px, y, "INVENTORY", 0.46f, C_GOLD);
    inventory(px + (pw - 70) / 2, y + 18);
}

const char* give_list_help(void)
{
    if (gl.cur >= gl.n) return "Back to Cheats and Tools.";
    if (!gl.n) return "The item names are not loaded yet.";
    return counted(item_at_row(gl.cur)) ? "Adds the item to your inventory now; the menu stays open. Left/right: the amount."
                                        : "Adds the item to your inventory now; the menu stays open.";
}
