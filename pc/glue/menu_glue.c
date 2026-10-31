/* Mouse in the inventory (status screen) and its sub-screens, the item box, the map and a
   message's choices (the save and load screens: save_glue.c).
   The mouse drives the menus' own state: hovering moves the game's cursor (listcsr_0/2,
   maincsr) the way its d-pad code does, clicks become the game's confirm/cancel pad bits,
   so keyboard and gamepad keep working unchanged. Game thread: menu_pad from bhSetPad
   (pad.c), menu_examine from ItemModelCheck (itemview.c). docs/ARCHITECTURE.md "Input". */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/sub1.h"
#include "../../decomp/include/ps2/veronica/prog/item.h"
#include "../../decomp/include/ps2/veronica/prog/sdfunc.h"
#include "../src/host/menu_mouse.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

extern PARTS parts_06b[24], parts_09b[8];
extern NJS_TEXANIM* testset[23];
int pad_game_input_enabled(void);
int pc_trade_screen(void);
int pc_save_screen(void);
int pc_wpn_screen(void);

enum { UP = 0x1, DOWN = 0x2, CONFIRM = 0xB00 /* like Cross, as tps_pad */, CANCEL = 0x1000, STATUS = 0x4000 };
enum { NONE, STATUS_SCREEN, MAP_SCREEN, QUESTION };
enum { TOPBAR = 100 };   /* hit ids: 1 the "standard" slot above the grid (pip[1]), 2..11 grid cells (listcsr_2), TOPBAR + n top bar button n */

static float mx, my, dmx, dmy;   /* cursor (game 2D px) and its motion this frame while the left button is held */
static int left, right, left_up, wheel, drag;
static int last_hit, pending, settle, trace, clicked;   /* clicked: frames since a click, counting down */
static unsigned next_pad;        /* bits for the next frame */

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
        return grid(st, ok, 88, STATUS);   /* right: close, as the status button */
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
        return grid(st, !(swork.statusflg & 0x40), (sys->cb_flg & 0x180000) ? 4 : -1, CANCEL);
    if (st->subscreenmode == 4 && st->testmode == 2) {   /* item box: the box list scrolls */
        unsigned r = (left ? CONFIRM : 0) | (right ? CANCEL : 0);
        return r | (st->number == 0 && !(sys->st_flg & 0x200) ? wheel_press() : 0);
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
    int scr = menu_mouse_on() ? screen() : NONE, x, y, held;
    unsigned add;

    if (!was) trace = getenv("RECVX_MENU_TRACE") != NULL;
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
