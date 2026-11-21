/* Mouse in the inventory (status screen) and its sub-screens, the item box, the map and a
   message's choices (the save and load screens: save_glue.c).
   The mouse drives the menus' own state: hovering moves the game's cursor (listcsr_0/2,
   maincsr) the way its d-pad code does, clicks become the game's confirm/cancel pad bits,
   so keyboard and gamepad keep working unchanged. Game thread: menu_pad from bhSetPad
   (pad.c), menu_examine from ItemModelCheck (itemview.c). docs/ARCHITECTURE.md "Input".
   Menu Style Redesigned (--dev; docs/MENUS.md "Menu Style / Redesigned"): drag and drop, tooltips and the shortcut
   strip over the same screens (below "Redesigned"; menu_overlay_draw from weapon_glue.c wpn_menu_draw). */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/sub1.h"
#include "../../decomp/include/ps2/veronica/prog/item.h"
#include "../../decomp/include/ps2/veronica/prog/sdfunc.h"
#include "../src/host/menu_mouse.h"
#include "../src/host/menu_layout.h"
#include "../src/host/coop.h"
#include "../src/host/lang.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern PARTS parts_06b[24], parts_09b[8], parts_20b[8];
extern NJS_TEXANIM* testset[23];
int pad_game_input_enabled(void);
int pc_trade_screen(void);
int pc_save_screen(void);
int pc_wpn_screen(void);
uint64_t gs_frame_count(void);

enum { UP = 0x1, DOWN = 0x2, CONFIRM = 0xB00 /* like Cross, as tps_pad */, CANCEL = 0x1000, STATUS = 0x4000 };
enum { NONE, STATUS_SCREEN, MAP_SCREEN, QUESTION };
enum { TOPBAR = 100 };   /* hit ids: 1 the "standard" slot above the grid (pip[1]), 2..11 grid cells (listcsr_2), TOPBAR + n top bar button n */

static float mx, my, dmx, dmy;   /* cursor (game 2D px) and its motion this frame while the left button is held */
static int left, right, left_up, wheel, drag;
static int last_hit, pending, settle, trace, clicked;   /* clicked: frames since a click, counting down */
static unsigned next_pad;        /* bits for the next frame */
static int held;                 /* the left button is down (main thread's g_menu_mouse.held, this frame) */

static int screen(void)
{
    if (!sys || (sys->ss_flg & 0x400000)) return NONE;   /* no demo */
    if (sys->ts_flg & 0x80) {   /* sub-screen task */
        if (!(sys->ts_flg & 0x200)) return STATUS_SCREEN;   /* bhSysCallItemselect runs StatusMain */
        if (!(sys->ts_flg & 0x400)) return MAP_SCREEN;
    }
    /* a message's choices (the typewriter's "use an ink ribbon?"), while the message shows (st_flg 0x200):
       a script that ends it otherwise (bhMesDispEndSet, as the Gold Luger room's switch panel when
       cancelled) leaves 0x8000 and mes_seln set until the next message */
    if ((sys->st_flg & 0x8200) == 0x8200 && sys->mes_seln > 0) return QUESTION;
    return NONE;
}

/* the cursor is on sprite p as StatusMain/SpriteSet2D draw it (pos + cen_pos, texanim size) */
static int over(const PARTS* p)
{
    const NJS_TEXANIM* a;
    float x, y;
    if (!(p->atr & 0x20) || p->anim < 0) return 0;   /* not drawn */
    a = &testset[p->parts_num][(int)p->anim];
    x = floorf(p->pos[0] + cen_pos[p->cen_no][0]) - a->cx; y = floorf(p->pos[1] + cen_pos[p->cen_no][1]) - a->cy;
    return mx >= x && mx < x + a->sx && my >= y && my < y + a->sy;
}

/* inventory cell under the cursor as the cursor's listcsr_2 (CursorSet: the 76x56 frame of cell
   n is at (20 + n%2 * 68, 4 + (n/2 - 1) * 48) + cen_pos[5], the standard slot's (pip[1]) at
   (slot1_x, -76) (< 0: not reachable); cells are 68x48 inside), 0 = none */
static int grid_cell(float slot1_x)
{
    float x = mx - cen_pos[parts_09b->cen_no][0] - 4, y = my - cen_pos[parts_09b->cen_no][1] - 4;
    int max = (sys->gm_flg & 0x8000000) ? 10 : 8, col, row;
    if (slot1_x >= 0 && x >= slot1_x && x < slot1_x + 68 && y >= -76 && y < -76 + 48) return 1;
    if (x < 20 || x >= 20 + 2 * 68 || y < 4) return 0;
    col = (int)((x - 20) / 68); row = (int)((y - 4) / 48);
    return 2 + row * 2 + col <= max + 1 ? 2 + row * 2 + col : 0;
}

/* the cursor to cell n: two-cell items come first (ItemSort), one per row; listcsr_0 is the
   item's index in pip as CursorMove keeps it */
static void grid_select(S_WORK* st, int n)
{
    int sakai = 2, rows, i;
    while (sakai < 12 && (itemdata[(st->pip[sakai] >> 16) & 0xFF].type & 0x100)) sakai++;
    rows = sakai - 2;
    i = n == 1 ? 1 : n / 2 - 1 < rows ? n / 2 + 1 : n - rows;
    if (i != (int)st->listcsr_0) CallSystemSe(0, 2);
    st->listcsr_0 = i;
    st->listcsr_2 = n;
}

/* hover: a new target under the cursor is applied once the screen takes input (ok) */
static int hover(int hit, int ok)
{
    if (hit != last_hit) { last_hit = hit; pending = hit; }
    if (settle || !ok || !pending) return 0;
    hit = pending; pending = 0;
    return hit;
}

static int topbar(void)
{
    for (int b = 0; b < 4; b++) if (over(&parts_00b[11 + b])) return TOPBAR + b;
    return 0;
}

/* wheel notches as d-pad presses, one every other frame (each must be a new press) */
static unsigned wheel_press(void)
{
    static int gap;
    if (gap) { gap = 0; return 0; }
    if (!wheel) return 0;
    gap = 1;
    if (wheel > 0) { wheel--; return UP; }
    wheel++;
    return DOWN;
}

static unsigned fallback(void)
{
    return (left ? CONFIRM : 0) | (right ? CANCEL : 0) | wheel_press();
}

/* the item grid of the status screen (CursorMove) and of the combine step (ItemCombination) */
static unsigned grid(S_WORK* st, int ok, float slot1_x, unsigned back)
{
    int hit = grid_cell(slot1_x), h = hover(hit, ok);
    if (h) grid_select(st, h);
    wheel = 0;
    if (left && hit && ok) { grid_select(st, hit); return CONFIRM; }
    return right ? back : 0;
}

/* ---------------------------------------------------------------- Menu Style Redesigned (--dev): drag and drop
   An item dragged with the left button (6 px or more) and dropped becomes the game's own presses, a step per frame once
   the screen takes input (mac): on another cell COMBINE (the cell, Confirm, COMBINE, the target, Confirm), a weapon on
   EQUIPMENT its EQUIP (Confirm, EQUIP), in the item box a cell on a row of the box list or a row on a cell the box's
   exchange (the cell, Confirm, the list scrolled to the row, Confirm), a weapon on a slot of the shortcut strip
   (weapon_glue.c) the shortcut. A press without a drag is a click, taken on the release. Nothing here writes an item:
   the game's ItemCommand / ItemCombination / ItemBox decide, refuse and say so as for the pad. */
enum { SRC_NONE, SRC_CELL, SRC_ROW, NOROW = 99 };
enum { MAC_NONE, MAC_COMBINE, MAC_EQUIP, MAC_EXCHANGE };
static struct { int src, at, dragging; unsigned word; float px, py; } dd;   /* at: the cell, or the box row (-3..3 from the chosen one) */
static struct { int kind, step, a, b, t, gap; } mac;
static int tip_cell;                   /* the cell or box row (+ 100) under the pointer, for the tooltip (0 none) */
int wpn_strip_hit(float x, float y);   /* weapon_glue.c */
void wpn_strip_drop(int slot, int id);
void wpn_strip_draw(int drag, int hot);
void pc_ui_text(float x, float y, const char* s, float sc, unsigned int argb);
float pc_ui_text_w(const char* s, float sc);
void pc_ui_rect(float x0, float y0, float x1, float y1, unsigned int argb, float z);
int pc_item_icons(int user, int on);
void pc_item_icon(unsigned int w, float x, float y, float sc, unsigned int argb, float z);
#define ICON_USER 16                   /* pc_item_icons: 1 trade, 2 weapon shortcuts, 4 spoiler log, 8 give list */
static int icons_held;

static int redesigned(void) { return menu_redesigned() && g_menu_mouse.active; }
static int weapon_id(unsigned w) { int id = (w >> 16) & 0xFF; return id && id < 156 && (itemdata[id].type & 0x1) ? id : 0; }

/* the item word in cell n (listcsr_2 numbering: 1 the standard slot, 2.. the grid), as grid_select finds its index */
static unsigned cell_word(S_WORK* st, int n)
{
    int sakai = 2;
    if (n < 1 || !st->pip) return 0;
    while (sakai < 12 && (itemdata[(st->pip[sakai] >> 16) & 0xFF].type & 0x100)) sakai++;
    return st->pip[n == 1 ? 1 : n / 2 - 1 < sakai - 2 ? n / 2 + 1 : n - (sakai - 2)];
}

/* EQUIPMENT, the box left of the standard slot (as grid_cell's coordinates) */
static int over_equip(void)
{
    float x = mx - cen_pos[parts_09b->cen_no][0] - 4, y = my - cen_pos[parts_09b->cen_no][1] - 4;
    return x >= -88 && x < 60 && y >= -76 && y < -28;
}

/* the item box's list: the row under the cursor, -3..3 from the chosen one (ItemBoxIconSet: seven rows of 38 at
   parts_20b's places, the chosen one fourth), NOROW none; its word */
static int box_row(void)
{
    float* c = cen_pos[10];
    for (int i = 0; i < 7; i++) {
        float y = parts_20b[i].pos[1] + c[1];
        if (!(parts_20b[i].atr & 0x20)) continue;
        if (mx >= c[0] + 8 && mx < c[0] + 380 && my >= y - 19 && my < y + 19) return i - 3;
    }
    return NOROW;
}
static unsigned box_word(S_WORK* st, int d)
{
    int mask = (sys->gm_flg & 0x8000000) && (sys->cb_flg & 0x180000) ? 0x1F : 0x7F;
    return d == NOROW || !st->bxp ? 0 : st->bxp[(st->ipsdisp + d) & mask];
}

static void start(int kind, int a, int b)
{
    mac.kind = kind; mac.step = 0; mac.a = a; mac.b = b; mac.t = 0; mac.gap = 0;
    if (trace) printf("menu: drag: %s %d -> %d\n", kind == MAC_COMBINE ? "combine" : kind == MAC_EQUIP ? "equip" : "exchange", a, b);
}

/* the drop: what it does where (0 nothing), for the macro and the label under the dragged icon */
static int drop_action(S_WORK* st, int* a, int* b)
{
    int cell = grid_cell(st->subscreenmode == 4 ? ((sys->cb_flg & 0x180000) ? 4 : -1) : 88), row, slot;
    if (st->subscreenmode == 2) {
        if (dd.src != SRC_CELL) return 0;
        if (cell && cell != dd.at && cell_word(st, cell) && cell_word(st, cell) != dd.word) { *a = dd.at; *b = cell; return MAC_COMBINE; }
        if (weapon_id(dd.word) && over_equip()) { *a = dd.at; return MAC_EQUIP; }
        if (weapon_id(dd.word) && (slot = wpn_strip_hit(mx, my)) >= 0) { *a = slot; return 100 + slot; }
        return 0;
    }
    row = box_row();
    if (dd.src == SRC_CELL && row != NOROW) { *a = dd.at; *b = row; return MAC_EXCHANGE; }
    if (dd.src == SRC_ROW && cell) { *a = cell; *b = dd.at; return MAC_EXCHANGE; }
    return 0;
}

/* the item list (CursorMove) and the item box's bag side, Redesigned: the press picks, a drag moves, the release drops or clicks */
static unsigned dd_grid(S_WORK* st, int ok, float slot1_x, unsigned back)
{
    int hit = grid_cell(slot1_x), row = st->subscreenmode == 4 ? box_row() : NOROW, h, a = 0, b = 0, k;
    unsigned r = 0;
    if (!dd.dragging && (h = hover(hit, ok))) grid_select(st, h);
    tip_cell = !dd.src && hit ? hit : !dd.src && row != NOROW && box_word(st, row) ? 100 + row : 0;
    wheel = 0;
    if (left && ok) {
        dd.src = hit && cell_word(st, hit) ? SRC_CELL : row != NOROW && box_word(st, row) ? SRC_ROW : SRC_NONE;
        dd.at = dd.src == SRC_CELL ? hit : row;
        dd.word = dd.src == SRC_CELL ? cell_word(st, hit) : box_word(st, row);
        dd.px = mx; dd.py = my; dd.dragging = 0;
        if (hit) grid_select(st, hit);
    }
    if (dd.src && (held & 1) && !dd.dragging && fabsf(mx - dd.px) + fabsf(my - dd.py) >= 6) dd.dragging = 1;
    if (left_up) {
        if (dd.dragging && ok && (k = drop_action(st, &a, &b))) {
            if (k >= 100) wpn_strip_drop(a, weapon_id(dd.word));
            else start(k, a, b);
        } else if (!dd.dragging && hit && ok && hit == dd.at && dd.src == SRC_CELL) {
            grid_select(st, hit);
            r = CONFIRM;
        } else if (!dd.dragging && hit && ok && !dd.src) {   /* (an empty cell: as before, the game says what it says) */
            grid_select(st, hit);
            r = CONFIRM;
        }
        dd.src = dd.dragging = 0;
        return r;
    }
    if (right) { dd.src = dd.dragging = 0; return back; }
    return 0;
}

/* the macro's next press, once the state it waits for takes input (0 meanwhile); presses one frame apart at least */
static unsigned macro(S_WORK* st)
{
    if (++mac.t > 150) {
        printf("menu: drag: gave up waiting (sub %d mode %d test %d)\n", st->subscreenmode, st->mode, st->testmode);
        mac.kind = 0;
        return 0;
    }
    if (mac.gap) { mac.gap--; return 0; }
    mac.gap = 1;
    if (mac.kind == MAC_EXCHANGE) {
        if (mac.step == 0) {
            if (st->subscreenmode != 4 || st->testmode != 1 || (swork.statusflg & 0x40)) { mac.gap = 0; return 0; }
            grid_select(st, mac.a); mac.step = 1;
            return CONFIRM;
        }
        if (st->subscreenmode != 4 || st->testmode != 2 || (sys->st_flg & 0x200) || st->number || st->wait) { mac.gap = 0; return 0; }
        if (mac.b) {   /* a row up or down, as the wheel steps the list (menu_glue's box list) */
            st->number = mac.b < 0 ? 38 : -38;
            mac.b += mac.b < 0 ? 1 : -1;
            return st->number > 0 ? UP : DOWN;
        }
        mac.kind = 0;
        return CONFIRM;
    }
    if (mac.step == 0) {
        if (st->subscreenmode != 2 || st->mode != 1 || st->testmode != 1 || (swork.statusflg & 0x60)) { mac.gap = 0; return 0; }
        grid_select(st, mac.a); mac.step = 1;
        return CONFIRM;
    }
    if (mac.step == 1) {
        if (st->mode != 4 || st->testmode != 1 || (swork.statusflg & 0x802000)) { mac.gap = 0; return 0; }
        if (mac.kind == MAC_EQUIP && parts_06b[13].anim != 7) { mac.kind = 0; return CANCEL; }   /* (not EQUIP: not a weapon) */
        st->maincsr = mac.kind == MAC_EQUIP ? 0 : 2;   /* EQUIP / COMBINE (ItemCommand) */
        mac.step = 2;
        if (mac.kind == MAC_EQUIP) mac.kind = 0;
        return CONFIRM;
    }
    if (st->mode != 4 || st->testmode != 2 || st->subcsr != 1 || (sys->st_flg & 0x200)) { mac.gap = 0; return 0; }
    grid_select(st, mac.b);
    mac.kind = 0;
    return CONFIRM;
}

static void box(float x0, float y0, float x1, float y1, float t, unsigned argb, float z)
{
    pc_ui_rect(x0, y0, x1, y0 + t, argb, z);
    pc_ui_rect(x0, y1 - t, x1, y1, argb, z + 0.0006f);
    pc_ui_rect(x0, y0, x0 + t, y1, argb, z + 0.0012f);
    pc_ui_rect(x1 - t, y0, x1, y1, argb, z + 0.0018f);
}

/* weapon_glue.c wpn_menu_draw (bhSysCallItemselect, after StatusMain): the strip, the tooltip, the dragged item. 2D depth
   above weapon_glue's (its highest is 0.988 + 3 sides) and below the font (1) */
void menu_overlay_draw(void)
{
    S_WORK* st = &swork;
    int a = 0, b = 0, k = 0, hot = -1, icons;
    char t[128], amt[16];
    if (!menu_redesigned() || (sys->ts_flg & 0x200)) return;
    icons = pc_item_icons(ICON_USER, icons_held = 1);
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 6);
    if (dd.dragging && g_menu_mouse.active) k = drop_action(st, &a, &b);
    if (k >= 100) hot = a;
    if (st->subscreenmode == 2 && (st->mode == 1 || st->mode == 2 || st->mode == 4) && st->pip)
        wpn_strip_draw(dd.dragging && g_menu_mouse.active ? weapon_id(dd.word) : 0, hot);
    if (!g_menu_mouse.active || pc_wpn_screen()) return;   /* (the Weapon Shortcuts screen has the mouse) */
    if (dd.dragging) {   /* the item at the pointer, what a drop does there */
        int two = ((itemdata[(dd.word >> 16) & 0xFF].type & 0x100) != 0);
        const char* what = k == MAC_COMBINE ? tr("Combine") : k == MAC_EQUIP ? tr("Equip") : k == MAC_EXCHANGE ? tr("Exchange") : "";
        if (icons) pc_item_icon(dd.word, mx - (two ? 34 : 17) * 1.2f, my - 12 * 1.2f, 0.6f, 0xC0FFFFFF, 0.996f);
        if (k >= 100) snprintf(t, sizeof(t), tr("Shortcut %d"), a + 1);
        else snprintf(t, sizeof(t), "%s", what);
        if (*t) {
            float w = pc_ui_text_w(t, 0.5f), x = lang_rtl_text() ? mx - 14 - w : mx + 14;
            pc_ui_rect(x - 4, my + 14, x + w + 4, my + 34, 0xD0000000, 0.993f);
            pc_ui_text(x, my + 15, t, 0.5f, 0xFFFFE080);
        }
        if (k == MAC_COMBINE || (k == MAC_EXCHANGE && st->subscreenmode == 4)) {   /* the target cell, or the bag cell of a row's drop */
            int c = k == MAC_COMBINE ? b : a;
            float* cen = cen_pos[parts_09b->cen_no];
            if (c >= 2) box(cen[0] + 24 + ((c - 2) & 1) * 68, cen[1] + 8 + ((c - 2) >> 1) * 48, cen[0] + 92 + ((c - 2) & 1) * 68,
                            cen[1] + 56 + ((c - 2) >> 1) * 48, 2, 0xFFFFE060, 0.990f);
        }
        return;
    }
    if (tip_cell && !mac.kind) {   /* the tooltip: the hovered item's name and amount */
        unsigned w = tip_cell >= 90 ? box_word(st, tip_cell - 100) : cell_word(st, tip_cell);
        int id = (w >> 16) & 0xFF;
        float tw, x, y = my + 18;
        if (!id || (tip_cell < 90 && (int)st->listcsr_2 != tip_cell)) return;   /* (the game's cursor elsewhere: keys moved it) */
        amt[0] = 0;
        if (itemdata[id].type & 0x3800) snprintf(amt, sizeof(amt), (w & 0x8000000) ? "INF" : "x%u", w & 0xFFFF);   /* KazuSet: the counted ones */
        if (!*amt) snprintf(t, sizeof(t), "%s", coop_item_name(w));
        else if (lang_rtl_text()) snprintf(t, sizeof(t), "%s  %s", amt, coop_item_name(w));   /* the name reads from the right */
        else snprintf(t, sizeof(t), "%s  %s", coop_item_name(w), amt);
        tw = pc_ui_text_w(t, 0.5f);
        x = lang_rtl_text() ? mx - 10 - tw : mx + 14;
        if (x + tw > 632) x = 632 - tw;
        if (x < 8) x = 8;
        if (y + 22 > 472) y = my - 30;
        pc_ui_rect(x - 5, y - 2, x + tw + 5, y + 20, 0xFF6080A0, 0.993f);   /* an outline, the panel over it */
        pc_ui_rect(x - 4, y - 1, x + tw + 4, y + 19, 0xFF000000, 0.994f);
        pc_ui_text(x, y, t, 0.5f, 0xFFFFFFFF);
    }
}

static unsigned status_screen(void)
{
    S_WORK* st = &swork;
    int hit, h, key = st->subscreenmode << 16 | st->mode << 8 | st->testmode;
    static int prev_key;
    if (key != prev_key) {   /* another menu state: a hover target of the old one is void; after a
                                click the target under the cursor applies in the new state */
        prev_key = key;
        pending = 0;
        if (clicked) last_hit = -1;
        dd.src = dd.dragging = 0;
    }
    if (st->subscreenmode == 2 && st->mode == 1 && st->testmode == 1) {   /* items (CursorMove) */
        int ok = !(swork.statusflg & 0x60);
        if (left && ok && (hit = topbar())) {   /* to the top bar as the d-pad goes there, then confirm */
            parts_09b->atr &= ~0x20;
            st->mode = 2; st->testmode = 0; st->maincsr = hit - TOPBAR;
            st->keywait = st->boxwait = 0;
            next_pad = CONFIRM;
            return 0;
        }
        return redesigned() ? dd_grid(st, ok, 88, STATUS) : grid(st, ok, 88, STATUS);   /* right: close, as the status button */
    }
    if (st->subscreenmode == 2 && st->mode == 2 && st->testmode == 1) {   /* top bar (MainCommand) */
        int ok = st->wait == 0 && !(swork.statusflg & 0x60);
        wheel = 0;
        hit = topbar();
        if ((h = hover(hit, ok)) && h != TOPBAR + st->maincsr) {
            st->maincsr = h - TOPBAR;
            CallSystemSe(0, 2);
            sys->st_flg &= ~0x200;   /* as the d-pad: the help line follows */
        }
        if (left && ok && (hit || grid_cell(88))) { st->maincsr = hit ? hit - TOPBAR : 3; return CONFIRM; }   /* a cell: back to the items */
        return right ? CANCEL : 0;
    }
    if (st->subscreenmode == 2 && st->mode == 4 && st->testmode == 1) {   /* use/equip, examine, combine (ItemCommand) */
        int ok = !(swork.statusflg & 0x802000);
        wheel = 0;
        hit = 0;
        for (int i = 0; i < 5; i++) if (over(&parts_06b[13 + i])) hit = 1 + i;
        if ((h = hover(hit, ok)) && h - 1 != st->maincsr) { st->maincsr = h - 1; CallSystemSe(0, 2); }
        if (left && ok) { if (hit) st->maincsr = hit - 1; return hit ? CONFIRM : CANCEL; }   /* outside: close the list */
        return right ? CANCEL : 0;
    }
    if (st->subscreenmode == 2 && st->mode == 4 && st->testmode == 2 && st->subcsr == 1)   /* combine with (ItemCombination) */
        return grid(st, !(sys->st_flg & 0x200), 88, CANCEL);
    if (st->subscreenmode == 2 && st->mode == 8 && st->testmode == 2) {   /* examine (ItemModelCheck): drag rotates, wheel zooms */
        unsigned r = (left_up && drag < 6 ? CONFIRM : 0) | (right ? CANCEL : 0);
        return r;
    }
    if (st->subscreenmode == 4 && st->testmode == 1)   /* item box: the inventory side */
        return (redesigned() ? dd_grid : grid)(st, !(swork.statusflg & 0x40), (sys->cb_flg & 0x180000) ? 4 : -1, CANCEL);
    if (st->subscreenmode == 4 && st->testmode == 2) {   /* item box: the box list scrolls */
        unsigned r = (left ? CONFIRM : 0) | (right ? CANCEL : 0);
        if (wheel && !r && st->number == 0 && !(sys->st_flg & 0x200)) {
            /* a notch: the list slides its row in one frame and steps in the next (ItemBox: slide speed 38 / row
               height 38; the d-pad's 19 slides two frames and steps on the third), so a quick turn no longer
               trails behind */
            st->number = wheel > 0 ? 38 : -38;
            r = wheel > 0 ? UP : DOWN;   /* (its arrow lights up) */
            wheel += wheel > 0 ? -1 : 1;
        }
        return r;
    }
    return fallback();   /* file view, pick-up dialog, ... */
}

/* a message's choices (message.c: the cursor glyph at mes_sps[i], the answer after it, left/right
   move mes_sel, confirm takes it, cancel does nothing): an answer reaches to the next one on its
   line, at most 160 px */
static unsigned question(void)
{
    int hit = 0, h, i, j, n = sys->mes_seln < 16 ? sys->mes_seln : 16, ok = sys->mes_tim == 0;
    for (i = 0; i < n; i++) {
        float x = sys->mes_sps[i].x, y = sys->mes_sps[i].y, x1 = x + 160;
        for (j = 0; j < n; j++)
            if (sys->mes_sps[j].y == y && sys->mes_sps[j].x > x && sys->mes_sps[j].x < x1) x1 = sys->mes_sps[j].x;
        if (mx >= x - 4 && mx < x1 - 4 && my >= y - 4 && my < y + 28) hit = 1 + i;
    }
    if (settle) last_hit = hit;   /* the pointer resting on an answer as the question comes up does not choose it */
    if ((h = hover(hit, ok)) && h - 1 != sys->mes_sel) { sys->mes_sel = h - 1; sys->mes_fls = 0; CallSystemSe(0, 2); }
    wheel = 0;
    if (left && hit && ok) { sys->mes_sel = hit - 1; return CONFIRM; }
    return 0;
}

/* pad.c bhSetPad, before pad_ps is derived */
unsigned int menu_pad(unsigned int pad)
{
    static int was, prev_x, prev_y;
    int scr = menu_mouse_on() ? screen() : NONE, x, y;
    unsigned add;

    if (!was) trace = getenv("RECVX_MENU_TRACE") != NULL;
    if (trace && sys) {   /* the menus' states as they change, by presented frame (pc/tools/screens_check.sh: transition lengths) */
        static uint64_t prev = ~(uint64_t)0;
        S_WORK* st = &swork;
        int s = screen();
        uint64_t k = (uint64_t)s << 56 | (uint64_t)st->subscreenmode << 48 | (uint64_t)st->mode << 40 | (uint64_t)st->testmode << 32 |
                     (s == STATUS_SCREEN ? (swork.statusflg & 0x802060) | (sys->st_flg & 0x200) << 8 | (uint64_t)(st->subcsr & 0xF) << 24 : 0);
        if (k != prev)
            printf("menu-state: frame %u screen %d sub %d mode %d test %d subcsr %d statusflg %#x st_flg %#x\n", (unsigned)gs_frame_count(), s,
                   st->subscreenmode, st->mode, st->testmode, st->subcsr, swork.statusflg, sys->st_flg);
        prev = k;
    }
    if (icons_held && (!menu_redesigned() || screen() != STATUS_SCREEN)) { pc_item_icons(ICON_USER, 0); icons_held = 0; }   /* (the overlay's) */
    if (pc_trade_screen() || pc_save_screen() || pc_wpn_screen()) return pad;   /* the co-op trade screen, the save/load screens and the weapon shortcuts' take the mouse (trade_glue.c, save_glue.c, weapon_glue.c) */
    left =__atomic_exchange_n(&g_menu_mouse.left, 0, __ATOMIC_RELAXED);
    right = __atomic_exchange_n(&g_menu_mouse.right, 0, __ATOMIC_RELAXED);
    left_up = __atomic_exchange_n(&g_menu_mouse.left_up, 0, __ATOMIC_RELAXED);
    wheel += __atomic_exchange_n(&g_menu_mouse.wheel, 0, __ATOMIC_RELAXED);
    held = g_menu_mouse.held;
    x = g_menu_mouse.x; y = g_menu_mouse.y;
    g_menu_mouse.active = scr != NONE;
    if (scr == NONE || !pad_game_input_enabled()) {
        was = 0; wheel = 0; next_pad = 0;
        mac.kind = dd.src = dd.dragging = 0;
        return pad;
    }
    if (!was) { settle = 3; last_hit = -1; pending = 0; prev_x = x; prev_y = y; drag = 0; }   /* the cursor may still be on its way in */
    else if (settle) settle--;
    was = 1;
    mx = x / 16.0f; my = y / 16.0f;
    dmx = (held & 1) && !settle ? (x - prev_x) / 16.0f : 0;
    dmy = (held & 1) && !settle ? (y - prev_y) / 16.0f : 0;
    prev_x = x; prev_y = y;
    if (left) drag = 0;
    drag += (int)(fabsf(dmx) + fabsf(dmy));
    if (settle) left = right = left_up = 0;
    if (left || right) clicked = 15; else if (clicked) clicked--;

    tip_cell = 0;
    if (mac.kind) {   /* a drop's presses (Redesigned): the pad and the mouse wait meanwhile */
        add = scr == STATUS_SCREEN ? macro(&swork) : (mac.kind = 0);
        if (trace && add) printf("menu: drag: press %#x (sub %d mode %d test %d)\n", add, swork.subscreenmode, swork.mode, swork.testmode);
        return add;
    }
    add = next_pad; next_pad = 0;
    add |= scr == STATUS_SCREEN ? status_screen() : scr == QUESTION ? question() : fallback();   /* map: wheel changes the floor, right click leaves */
    if (trace && (add || left || right)) {
        S_WORK* st = &swork;
        printf("menu: screen %d sub %#x mode %d test %d csr %u/%u main %d at (%.0f %.0f) hit %d -> pad %#x\n", scr, st->subscreenmode, st->mode,
               st->testmode, st->listcsr_0, st->listcsr_2, st->maincsr, mx, my, last_hit, add);
    }
    return pad | add;
}

/* itemview.c ItemModelCheck, after the d-pad set this frame's rotation (cax/cay) and zoom */
void menu_examine(STCAM_WRK* sc)
{
    if (!g_menu_mouse.active) return;
    sc->cay += (int)(dmx * 91.0f);   /* 0.5 degrees per pixel dragged */
    sc->cax -= (int)(dmy * 91.0f);
    sc->pos_1.z -= wheel * 0.3f;   /* wheel away from the player = closer */
    wheel = 0;
    if (sc->pos_1.z > 3.0f) sc->pos_1.z = 3.0f;
    if (sc->pos_1.z < -3.0f) sc->pos_1.z = -3.0f;
}
