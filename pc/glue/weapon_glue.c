/* Weapon shortcuts (docs/USER_GUIDE.md "Weapon shortcuts", docs/ARCHITECTURE.md "Input"): eight slots per
 * character, each a weapon's item id (pc/src/host/weapons.h), so they survive the bag being sorted anew.
 *
 * Swapping: a shortcut key (1-8), or the d-pad while the Weapon Shortcuts button is held (the wheel, slots
 * 1-4 up/right/down/left, L1 / R1 the second page with 5-8), equips that weapon through the inventory's own
 * WeaponSet, as the quick knife does (knife_glue.c): equipped slot, weapon number, weapon file and model,
 * ammo display; the co-op partner sees it through the player state it gets each frame. Only at rest (the
 * quick knife's rule: in play, weapon down, standing, walking, running or idle; not aiming, reloading,
 * climbing, grabbed, in an event, door or menu); a press up to half a second before that waits for it.
 * Missing from the bag or already in the hand: nothing.
 *
 * Assigning: in the inventory, a shortcut key on the weapon under the cursor assigns it (again: clears
 * it); the Weapon Shortcuts button opens their screen, drawn as the co-op trade screen (trade_glue.c): the
 * bag on the left, the eight slots on the right.
 *
 * Game thread: wpn_pad from bhSetPad (pad.c, after trade_pad), wpn_draw from bhMainSequence (game.c, the
 * wheel), wpn_menu_draw from bhSysCallItemselect (system.c, after StatusMain: the screen, the inventory's
 * slot marks). pc/src/input/pad.c decides per pad report whether the wheel is up (wpn_hold_ok), and while it
 * is the d-pad and L1 / R1 read as up for everything else (bindings.c). */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/sub1.h"
#include "../../decomp/include/ps2/veronica/prog/item.h"
#include "../../decomp/include/ps2/veronica/prog/flag.h"
#include "../../decomp/include/ps2/veronica/prog/adv.h"
#include "../../decomp/include/ps2/veronica/prog/sdfunc.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_NaDraw.h"
#include "../src/host/weapons.h"
#include "../src/host/bindings.h"
#include "../src/host/cheat.h"
#include "../src/host/coop.h"
#include "../src/host/lang.h"
#include "../src/host/menu_mouse.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void host_log(const char* fmt, ...);
void pc_ui_text(float x, float y, const char* s, float sc, unsigned int argb);
float pc_ui_text_w(const char* s, float sc);
void pc_ui_rect(float x0, float y0, float x1, float y1, unsigned int argb, float z);
int pc_overlay_begin(int kind);
void pc_overlay_end(void);
int pc_in_play(const BH_PWORK* p, int scope);   /* reload_glue.c */
int pc_weapon_at_rest(void);                    /* knife_glue.c */
int knife_busy(void);
int pc_trade_screen(void);                      /* trade_glue.c */
int pc_item_icons(int user, int on);
void pc_item_icon(unsigned int w, float x, float y, float sc, unsigned int argb);
int pc_weapon_shortcuts_on(void);               /* host.c: the setting */
void settings_save_later(void);
extern PARTS parts_06b[24], parts_09b[8];
extern ADV_WORK AdvWork;

#define ID(w) ((int)((w) >> 16 & 0xFF))         /* item id without the ammo kind (WeaponSet's switch) */
#define Z_DIM 30.0f                             /* as the trade screen: the dimming, then ours in front */
#define Z_UI 30.5f
#define Z_CUR 30.6f                             /* the cursor and picks: their own depth, so Smooth menus never */
#define Z_PICK 30.7f                            /* pairs them with the cells' frames (view_glue.c pc_menu_tag) */
#define ICON_USER 2                             /* pc_item_icons */

static unsigned int* inv(void) { return &sys->itm[(sys->ply_id & 3) * 16]; }
static int bag_n(void) { return (sys->gm_flg & 0x8000000) ? 11 : 9; }   /* [1] the lighter's slot, [2..] the grid */
static int cells(void) { return (sys->gm_flg & 0x8000000) ? 10 : 8; }
static int chr(void) { return sys->ply_id & 3; }
static int weapon(int id) { return id > 0 && id < 156 && (itemdata[id].type & 0x1); }   /* equipable (ItemCommand's EQUIP) */
static const char* name(int id) { return coop_item_name((unsigned int)id << 16); }

static unsigned int pulse(unsigned int rgb) { return ((unsigned int)AdvWork.FlushCount2 / 2 + 128) << 24 | rgb; }
static void text_c(float cx, float y, const char* s, float sc, unsigned int argb) { pc_ui_text(cx - pc_ui_text_w(s, sc) / 2, y, s, sc, argb); }
/* s made smaller (to 3/4) to fit w, then cut at a whole letter; left at x, or centred on x */
static void text_fit(float x, float y, const char* s, float sc, float w, unsigned int argb, int centred)
{
    char b[96];
    float k = sc;
    int n;
    s = tr(s);
    while (k > sc * 0.75f && pc_ui_text_w(s, k) > w) k -= 0.01f;
    snprintf(b, sizeof(b), "%s", s);
    for (n = (int)strlen(b); n > 0 && pc_ui_text_w(b, k) > w;) {
        while (n > 0 && (b[n - 1] & 0xC0) == 0x80) n--;   /* (a UTF-8 letter's continuation bytes) */
        b[--n > 0 ? n : 0] = 0;
    }
    pc_ui_text(centred ? x - pc_ui_text_w(b, k) / 2 : x, y, b, k, argb);
}
static void frame(float x0, float y0, float x1, float y1, float t, unsigned int argb, float z)
{
    pc_ui_rect(x0, y0, x1, y0 + t, argb, z);
    pc_ui_rect(x0, y1 - t, x1, y1, argb, z);
    pc_ui_rect(x0, y0, x0 + t, y1, argb, z);
    pc_ui_rect(x1 - t, y0, x1, y1, argb, z);
}
/* the name of the first binding of row on the device in use (the gamepad's when one is open) */
static const char* key_name(int row, char* b, int n)
{
    int dev = bind_gamepad() ? BIND_PAD : BIND_KB, c = bind_get(row, dev, 0);
    if (!c) c = bind_get(row, dev, 1);
    return bind_code_name(dev, c, b, n);
}

/* ---------------------------------------------------------------- swapping */
static int page, pending = -1, pending_t, wheel;   /* wheel: up this frame (for wpn_draw) */
static unsigned int was_dirs, was_keys, was_hold;

/* pad.c, each pad report: the wheel may come up (our player in play with the weapon down; not while the
   quick knife has the hand, the trade screen is open or noclip flies) */
int wpn_hold_ok(void)
{
    if (!pc_weapon_shortcuts_on() || !sys || !plp || knife_busy() || pc_trade_screen() || g_cheat.noclip == 2) return 0;
    if (pc_in_play(plp, 0) && plp->mode1 == 0) return 1;
    /* up already: it stays while the weapon it equipped loads (WeaponSet's mn_mode0 3, the monitor's mn_md0 3) */
    return wheel && plp == &ply && plp->mode0 == 1 && plp->mode1 == 0 && (*(int*)&sys->mn_mode0 == 3 || sys->mn_md0 == 3);
}

static void request(int slot, const char* how)
{
    int id = g_wpn_slots[chr()][slot], at;
    unsigned int* iv = inv();
    at = wpn_bag_find(iv, bag_n(), id);
    if (!id || !at || (iv[0] && ID(iv[iv[0]]) == id)) {
        host_log("weapons: shortcut %d (%s): %s\n", slot + 1, how, !id ? "empty" : !at ? "not in the bag" : "already in the hand");
        return;
    }
    pending = slot;
    pending_t = 15;   /* game frames: a press just before the weapon is down counts */
}

static void equip(int slot)
{
    unsigned int* iv = inv();
    int id = g_wpn_slots[chr()][slot], at = wpn_bag_find(iv, bag_n(), id);
    if (!at || (iv[0] && ID(iv[iv[0]]) == id)) return;   /* gone or in the hand meanwhile */
    host_log("weapons: shortcut %d: %s (item %d, bag slot %d, count %u) for item %d\n", slot + 1, name(id), id, at, iv[at] & 0xFFFF,
             iv[0] ? ID(iv[iv[0]]) : 0);
    /* the sniper rifle's scope state (ev_flg 74) goes through the command list's sprite (WeaponSet reads
       parts_06b[17].anim, ItemCommand sets it from the flag): kept as it is */
    if (id == 10) parts_06b[17].anim = bhCkFlg(sys->ev_flg, 74) ? 12 : 11;
    swork.pip = iv;
    WeaponSet(at, at);   /* as the inventory: weapon number, equipped slot, weapon file load, motion reset */
}

static void play_frame(unsigned int key_press)
{
    unsigned int dirs = (unsigned int)pad_wpn_dirs(), press = dirs & ~was_dirs;
    int n;
    was_dirs = dirs;
    wheel = g_wpn_overlay;
    if (wheel) {
        if (press & 0x30) { page ^= 1; CallSystemSe(0, 2); }
        for (n = 0; n < 4; n++) if (press & 1u << n) request(page * 4 + n, "d-pad");
    } else page = 0;
    if (pc_weapon_shortcuts_on() && pc_in_play(plp, 0))
        for (n = 0; n < WPN_SLOTS; n++) if (key_press & 1u << n) request(n, "key");
    if (key_press && pc_weapon_shortcuts_on() && !pc_in_play(plp, 0) && !(sys->ts_flg & 0x80))
        host_log("weapons: shortcut key ignored (not in play: event, door, menu or first person)\n");
    if (pending < 0) return;
    if (!pc_weapon_shortcuts_on() || !pc_in_play(plp, 0) || --pending_t < 0) {
        host_log("weapons: shortcut %d dropped (%s)\n", pending + 1, pending_t < 0 ? "not at rest in time" : "no longer in play");
        pending = -1;
    } else if (pc_weapon_at_rest() && !knife_busy()) {
        equip(pending);
        pending = -1;
    }
}

void wpn_game_exit(void) { pending = -1; page = 0; wheel = 0; }

/* ---------------------------------------------------------------- the inventory */
static int inventory(void)   /* the item list (CursorMove), taking input */
{
    S_WORK* st = &swork;
    return sys && !(sys->ss_flg & 0x400000) && (sys->ts_flg & 0x80) && !(sys->ts_flg & 0x200) && st->subscreenmode == 2 && st->mode == 1 &&
           st->testmode == 1 && !(st->statusflg & 0x60) && st->pip;
}

static char note[96];
static int note_t;   /* game frames left */
static void say(const char* s) { snprintf(note, sizeof(note), "%s", s); note_t = 75; }

static void assign(int slot, int id)
{
    char b[96];
    if (g_wpn_slots[chr()][slot] == id) {
        wpn_assign(chr(), slot, 0);
        snprintf(b, sizeof(b), tr("Shortcut %d cleared"), slot + 1);
    } else {
        wpn_assign(chr(), slot, id);
        snprintf(b, sizeof(b), tr("%s: shortcut %d"), name(id), slot + 1);
    }
    host_log("weapons: %s (character %d: %d %d %d %d %d %d %d %d)\n", b, chr(), g_wpn_slots[chr()][0], g_wpn_slots[chr()][1], g_wpn_slots[chr()][2],
             g_wpn_slots[chr()][3], g_wpn_slots[chr()][4], g_wpn_slots[chr()][5], g_wpn_slots[chr()][6], g_wpn_slots[chr()][7]);
    say(b);
    CallSystemSe(0, 3);
    settings_save_later();
}

/* ---------------------------------------------------------------- the screen (layout as the trade screen) */
#define GX 72.0f                        /* the bag */
#define GY 96.0f
#define CW 68.0f
#define CH 48.0f
#define LX 300.0f                       /* the slots */
#define LX1 600.0f
#define LY 96.0f
#define LH 30.0f
#define LGAP 16.0f                      /* between slots 4 and 5 (the second page) */
#define OK_Y 392.0f                     /* CLOSE, as wide as its word */
#define OK_X0 (320.0f - ok_w())
#define OK_X1 (320.0f + ok_w())
enum { SIDE_BAG, SIDE_SLOTS, SIDE_CLOSE };

static struct {
    int open, side, cur, pick_cell, pick_slot;   /* picks: -1 none */
    MouseTake mouse;
    unsigned int prev, mask;
} ms;

int pc_wpn_screen(void) { return ms.open; }

static float ok_w(void) { float w = pc_ui_text_w("CLOSE", 0.6f) / 2 + 10; return w > 36 ? w : 36; }
int pc_ui_text_wrap(float x, float y, float w, const char* s, float sc, unsigned int argb, int max_lines);
static float row_y(int s) { return LY + s * LH + (s >= 4 ? LGAP : 0); }
static float cell_x(int c) { return GX + (c & 1) * CW; }
static float cell_y(int c) { return GY + (c >> 1) * CH; }
static unsigned int cell_word(int c, const signed char* w) { return c >= 0 && c < cells() && w[c] >= 0 ? inv()[2 + w[c]] : 0; }

/* the cells a cell's item covers (a two-slot item: both) */
static void span(int c, const signed char* w, int* a, int* b)
{
    *a = *b = c;
    if (w[c] >= 0) { if (c > 0 && w[c - 1] == w[c]) *a = c - 1; if (c + 1 < cells() && w[c + 1] == w[c]) *b = c + 1; }
}

static void open_screen(void)
{
    S_WORK* st = &swork;
    memset(&ms, 0, sizeof(ms));
    ms.open = 1;
    ms.pick_cell = ms.pick_slot = -1;
    ms.cur = st->listcsr_2 >= 2 && st->listcsr_2 < 2 + (unsigned int)cells() ? (int)st->listcsr_2 - 2 : 0;   /* the inventory's cursor */
    menu_mouse_take(&ms.mouse);   /* (what the mouse did before) */
    note_t = 0;
    CallSystemSe(0, 3);
    host_log("weapons: shortcut screen open\n");
}

static void close_screen(void)
{
    ms.open = 0;
    pc_item_icons(ICON_USER, 0);
    CallSystemSe(0, 1);
    host_log("weapons: shortcut screen closed\n");
}

static void cursor(int side, int c)
{
    if (side == ms.side && c == ms.cur) return;
    ms.side = side; ms.cur = c;
    CallSystemSe(0, 2);
}

static void move(int dx, int dy)
{
    signed char w[10];
    int c = ms.cur, n = cells();
    coop_trade_layout(&inv()[2], n, w);
    if (ms.side == SIDE_CLOSE) {
        if (dy < 0) cursor(SIDE_SLOTS, WPN_SLOTS - 1);
        return;
    }
    if (ms.side == SIDE_SLOTS) {
        if (dx < 0) cursor(SIDE_BAG, (c < 4 ? c : c - 4) * 2 + 1 < n ? (c < 4 ? c : c - 4) * 2 + 1 : n - 1);
        else if (dy > 0) cursor(c + 1 < WPN_SLOTS ? SIDE_SLOTS : SIDE_CLOSE, c + 1 < WPN_SLOTS ? c + 1 : 0);
        else if (dy < 0 && c > 0) cursor(SIDE_SLOTS, c - 1);
        return;
    }
    if (dx) {
        int t = c + dx;
        if (w[c] >= 0 && (t >> 1) == (c >> 1) && t >= 0 && w[t] == w[c]) t += dx;   /* over a two-slot item's other half */
        if (dx > 0 && (t >> 1) != (c >> 1)) { cursor(SIDE_SLOTS, (c >> 1) < 4 ? c >> 1 : 3); return; }
        if (t < 0 || (t >> 1) != (c >> 1)) return;
        c = t;
    }
    if (dy) c += 2 * dy;
    if (dy > 0 && c >= n) { cursor(SIDE_CLOSE, 0); return; }
    if (c >= 0 && c < n) cursor(SIDE_BAG, c);
}

/* confirm on the cursor: a weapon or a slot is picked; with the other already picked, it is assigned */
static void activate(void)
{
    signed char w[10];
    int id;
    coop_trade_layout(&inv()[2], cells(), w);
    if (ms.side == SIDE_CLOSE) { close_screen(); return; }
    if (ms.side == SIDE_BAG) {
        id = ID(cell_word(ms.cur, w));
        if (!weapon(id)) { CallSystemSe(0, 1); say(tr("Only weapons can have a shortcut")); return; }
        if (ms.pick_slot >= 0) { assign(ms.pick_slot, id); ms.pick_slot = ms.pick_cell = -1; return; }
        ms.pick_cell = ms.pick_cell == ms.cur ? -1 : ms.cur;
        CallSystemSe(0, 3);
        return;
    }
    if (ms.pick_cell >= 0 && weapon(id = ID(cell_word(ms.pick_cell, w)))) { assign(ms.cur, id); ms.pick_slot = ms.pick_cell = -1; return; }
    ms.pick_slot = ms.pick_slot == ms.cur ? -1 : ms.cur;
    CallSystemSe(0, 3);
}

/* cancel: a pick goes back, else a slot under the cursor is cleared, else the screen closes */
static void back(void)
{
    if (ms.pick_cell >= 0 || ms.pick_slot >= 0) { ms.pick_cell = ms.pick_slot = -1; CallSystemSe(0, 1); return; }
    if (ms.side == SIDE_SLOTS && g_wpn_slots[chr()][ms.cur]) { assign(ms.cur, g_wpn_slots[chr()][ms.cur]); return; }   /* (assign again: clears) */
    close_screen();
}

static int hit(float x, float y, int* side)
{
    int c, s;
    if (x >= GX && x < GX + 2 * CW && y >= GY && (c = (int)((y - GY) / CH) * 2 + (int)((x - GX) / CW)) < cells()) { *side = SIDE_BAG; return c; }
    for (s = 0; s < WPN_SLOTS; s++)
        if (x >= LX - 6 && x < LX1 && y >= row_y(s) - 4 && y < row_y(s) + LH - 4) { *side = SIDE_SLOTS; return s; }
    if (x >= OK_X0 && x < OK_X1 && y >= OK_Y - 4 && y < OK_Y + 22) { *side = SIDE_CLOSE; return 0; }
    return -1;
}

/* the screen's input (all of it while open; buttons held when it closes stay off until let go) */
static unsigned int screen_pad(unsigned int pad, int hold_press, unsigned int key_press)
{
    unsigned int press;
    int side, c, n;
    if (!ms.open) {
        if (ms.mask) { ms.mask &= pad; pad &= ~ms.mask; }
        return pad;
    }
    if (!inventory() && !(sys->ts_flg & 0x80)) { close_screen(); return 0; }   /* the inventory went (a co-op game over, an event) */
    press = pad & ~ms.prev;
    ms.prev = ms.mask = pad;
    g_menu_mouse.active = 1;
    menu_mouse_take(&ms.mouse);
    c = hit(ms.mouse.x, ms.mouse.y, &side);
    if (ms.mouse.moved && c >= 0) cursor(side, c);
    if (press & 0x4) move(-1, 0);
    if (press & 0x8) move(1, 0);
    if (press & 0x1) move(0, -1);
    if (press & 0x2) move(0, 1);
    if (ms.mouse.left) { if (c >= 0) { cursor(side, c); activate(); } }
    else if (press & 0xB00) activate();   /* confirm (Cross / the key type's fire) */
    if (ms.mouse.right) { if (c >= 0) cursor(side, c); back(); }
    else if (press & 0x1000) back();
    if (ms.open && (hold_press || (press & 0x4000))) close_screen();   /* the button that opened it, or the inventory's */
    for (n = 0; n < WPN_SLOTS && ms.open; n++)   /* a shortcut key: the weapon under the cursor (or picked) to that slot */
        if (key_press & 1u << n) {
            signed char w[10];
            int id;
            coop_trade_layout(&inv()[2], cells(), w);
            id = ID(cell_word(ms.side == SIDE_BAG ? ms.cur : ms.pick_cell, w));
            if (weapon(id)) { assign(n, id); ms.pick_cell = ms.pick_slot = -1; }
            else CallSystemSe(0, 1);
        }
    if (!ms.open) ms.prev = 0;
    return 0;
}

/* pad.c bhSetPad, after trade_pad */
unsigned int wpn_pad(unsigned int pad)
{
    unsigned int keys = 0, key_press;
    int hold, hold_press, n;
    if (!sys) return pad;
    hold = pad_wpn_hold();
    hold_press = hold && !was_hold;
    was_hold = hold;
    for (n = 0; n < WPN_SLOTS; n++) if (pad_wpn_key(n)) keys |= 1u << n;
    key_press = keys & ~was_keys;
    was_keys = keys;
    if (note_t > 0) note_t--;
    if (ms.open || ms.mask) return screen_pad(pad, hold_press, key_press);
    if (plp) play_frame(key_press);
    else wheel = 0;
    if (pc_weapon_shortcuts_on() && inventory()) {
        int id = ID(swork.pip[swork.listcsr_0 < 12 ? swork.listcsr_0 : 0]);
        if (hold_press) { open_screen(); return 0; }
        for (n = 0; n < WPN_SLOTS; n++)   /* a shortcut key on the weapon under the cursor */
            if (key_press & 1u << n) {
                if (weapon(id)) assign(n, id);
                else { CallSystemSe(0, 1); say(tr("Only weapons can have a shortcut")); }
            }
    }
    return pad;
}

/* ---------------------------------------------------------------- drawing */
static void dim(void)
{
    if (pc_overlay_begin(PC_OVL_FADE)) {   /* the whole view, also in widescreen */
        NJS_POLYGON_VTX v[4] = { { 0, 0, Z_DIM, 0xB8000000 }, { 0, 480, Z_DIM, 0xB8000000 }, { 640, 0, Z_DIM, 0xB8000000 }, { 640, 480, Z_DIM, 0xB8000000 } };
        njDrawPolygon(v, 4, 1);
        pc_overlay_end();
    }
}

static void note_line(float y)
{
    float w;
    if (note_t <= 0) return;
    w = pc_ui_text_w(note, 0.55f);
    pc_ui_rect(320 - w / 2 - 10, y - 4, 320 + w / 2 + 10, y + 20, 0xC0000000, Z_UI);
    text_c(320, y, note, 0.55f, 0xFFFFFFFF);
}

static void draw_screen(void)
{
    static const char* const dirs[4] = { "Up", "Right", "Down", "Left" };
    signed char w[10];
    unsigned int* iv = inv();
    int icons = pc_item_icons(ICON_USER, 1), c, a, b, s, n = cells();
    char t[96], k[32], p1[32], p2[32];
    unsigned int under;
    coop_trade_layout(&iv[2], n, w);
    dim();
    text_c(320, 24, "WEAPON SHORTCUTS", 1.0f, 0xFFFFFFFF);
    text_c(GX + CW, 64, "BAG", 0.7f, 0xFFFFE080);
    text_c((LX + LX1) / 2, 64, "SHORTCUTS", 0.7f, 0xFF90D0FF);
    /* the bag: cells (a two-slot item one frame over its row), icons, counts, the equipped mark, slot marks */
    for (c = 0; c < n; c++) {
        int two = w[c] >= 0 && c + 1 < n && w[c + 1] == w[c];
        if (w[c] >= 0 && c > 0 && w[c - 1] == w[c]) continue;
        pc_ui_rect(cell_x(c) + 1, cell_y(c) + 1, cell_x(c) + (two ? 2 : 1) * CW - 1, cell_y(c) + CH - 1, 0xC0081830, Z_UI);
        frame(cell_x(c), cell_y(c), cell_x(c) + (two ? 2 : 1) * CW, cell_y(c) + CH, 1, 0xFF405878, Z_UI);
    }
    for (c = 0; c < n; c++) {
        unsigned int wd = cell_word(c, w);
        int id = ID(wd);
        float x = cell_x(c), y = cell_y(c), cw = (itemdata[id].type & 0x100) ? 2 * CW : CW;
        if (!wd || (c > 0 && w[c - 1] == w[c])) continue;
        if (icons) pc_item_icon(wd, x, y, 1.0f, weapon(id) ? 0xFFFFFFFF : 0xFF686868);
        else { snprintf(t, sizeof(t), "%.10s", name(id)); pc_ui_text(x + 4, y + 16, t, 0.4f, weapon(id) ? 0xFFE0E0E0 : 0xFF808080); }
        if (itemdata[id].type & 0x3800) {   /* KazuSet: weapons and ammunition show their count */
            snprintf(t, sizeof(t), (wd & 0x8000000) ? "INF" : "%u", wd & 0xFFFF);
            pc_ui_text(x + cw - pc_ui_text_w(t, 0.5f) - 4, y + CH - 17, t, 0.5f, 0xFF60F060);
        }
        if (w[c] + 2 == (int)iv[0]) pc_ui_text(x + 4, y + 2, "E", 0.5f, 0xFFF0D040);
        if ((s = wpn_slot_of(chr(), id)) >= 0) {
            snprintf(t, sizeof(t), "%d", s + 1);
            pc_ui_rect(x + cw - 16, y + 2, x + cw - 3, y + 17, 0xE0204060, Z_UI);
            pc_ui_text(x + cw - 13, y + 2, t, 0.5f, 0xFF90D0FF);
        }
    }
    if (ms.pick_cell >= 0) { span(ms.pick_cell, w, &a, &b); frame(cell_x(a) - 2, cell_y(a) - 2, cell_x(b) + CW + 2, cell_y(b) + CH + 2, 3, 0xFFFFE060, Z_PICK); }
    if (ms.side == SIDE_BAG) { span(ms.cur, w, &a, &b); frame(cell_x(a) + 4, cell_y(a) + 4, cell_x(b) + CW - 4, cell_y(b) + CH - 4, 2, pulse(0xFFFFFF), Z_CUR); }
    under = ms.side == SIDE_BAG ? cell_word(ms.cur, w) : 0;
    text_c(GX + CW, GY + 5 * CH + 6, under ? name(ID(under)) : "", 0.55f, 0xFFE0E0E0);
    /* the slots: key, d-pad direction (the second four: on the wheel's second page), weapon */
    snprintf(t, sizeof(t), tr("Second page: %s / %s"), key_name(B_L1, p1, sizeof(p1)), key_name(B_R1, p2, sizeof(p2)));
    pc_ui_text(LX, row_y(4) - LGAP - 1, t, 0.4f, 0xFFA0A0A0);
    for (s = 0; s < WPN_SLOTS; s++) {
        int id = g_wpn_slots[chr()][s], have = id && wpn_bag_find(iv, bag_n(), id);
        float y = row_y(s);
        pc_ui_rect(LX - 4, y - 3, LX1, y + LH - 5, (s & 1) ? 0x80102030 : 0x80182840, Z_UI);
        bind_code_name(BIND_KB, bind_get(B_WPN1 + s, BIND_KB, 0), k, sizeof(k));
        pc_ui_text(LX + 2, y + 1, k, 0.55f, 0xFFFFE080);
        text_fit(LX + 34, y + 3, dirs[s & 3], 0.45f, 82, 0xFF90D0FF, 0);
        if (!id) pc_ui_text(LX + 120, y + 1, "-", 0.55f, 0xFF707070);
        else {
            snprintf(t, sizeof(t), "%s%s", name(id), have ? "" : tr(" (not in the bag)"));
            text_fit(LX + 120, y + 1, t, 0.55f, LX1 - LX - 124, !have ? 0xFF808080 : iv[0] && ID(iv[iv[0]]) == id ? 0xFFF0D040 : 0xFFE0E0E0, 0);
        }
        if (s == ms.pick_slot) frame(LX - 6, y - 5, LX1 + 2, y + LH - 3, 3, 0xFFFFE060, Z_PICK);
        if (ms.side == SIDE_SLOTS && s == ms.cur) frame(LX - 3, y - 2, LX1 - 1, y + LH - 6, 2, pulse(0xFFFFFF), Z_CUR);
    }
    frame(OK_X0, OK_Y - 4, OK_X1, OK_Y + 22, 1, 0xFF6080A0, Z_UI);
    text_c(320, OK_Y, "CLOSE", 0.6f, 0xFFFFFFFF);
    if (ms.side == SIDE_CLOSE) frame(OK_X0 + 2, OK_Y - 2, OK_X1 - 2, OK_Y + 20, 2, pulse(0xFFFFFF), Z_CUR);
    note_line(418);
    if (note_t <= 0 && (ms.pick_cell >= 0 || ms.pick_slot >= 0)) text_c(320, 422, ms.pick_cell >= 0 ? "Now choose a shortcut." : "Now choose a weapon.", 0.5f, 0xFFF0C060);
    pc_ui_text_wrap(24, 442, 592, "Confirm: a weapon, then a shortcut. Cancel on a shortcut clears it. 1-8: the weapon under the cursor.", 0.4f, 0xFFA0A0A0, 2);
}

/* the inventory: each weapon's slot in a corner of its cell (CursorSet: cell n's 68x48 inside at
   (24 + n%2 * 68, 8 + (n/2 - 1) * 48) + cen_pos[5]), the last change, and what opens the screen */
static void draw_inventory(void)
{
    S_WORK* st = &swork;
    signed char w[10];
    float* cen;
    char t[64], k[32];
    int c, s, n = cells();
    if (st->subscreenmode != 2 || (st->mode != 1 && st->mode != 2 && st->mode != 4) || !st->pip) return;
    cen = cen_pos[parts_09b->cen_no];
    coop_trade_layout(&st->pip[2], n, w);
    for (c = 0; c < n; c++) {
        int id = w[c] >= 0 ? ID(st->pip[2 + w[c]]) : 0;
        float x = cen[0] + 24 + (c & 1) * 68, y = cen[1] + 8 + (c >> 1) * 48, cw = (itemdata[id].type & 0x100) ? 136 : 68;
        if (!id || (c > 0 && w[c - 1] == w[c]) || (s = wpn_slot_of(chr(), id)) < 0) continue;
        snprintf(t, sizeof(t), "%d", s + 1);
        pc_ui_rect(x + cw - 16, y + 2, x + cw - 3, y + 17, 0xE0204060, Z_UI);
        pc_ui_text(x + cw - 13, y + 2, t, 0.5f, 0xFF90D0FF);
    }
    note_line(440);
    if (st->mode == 1 && note_t <= 0) {
        snprintf(t, sizeof(t), tr("%s: Weapon Shortcuts"), key_name(B_WPN_HOLD, k, sizeof(k)));
        pc_ui_text(624 - pc_ui_text_w(t, 0.42f), 456, t, 0.42f, 0xFFA0A0A0);
    }
}

/* system.c bhSysCallItemselect, after StatusMain */
void wpn_menu_draw(void)
{
    if (!pc_weapon_shortcuts_on() || (sys->ts_flg & 0x200)) return;
    pc_item_icons(ICON_USER, ms.open);
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 6);
    if (ms.open) draw_screen();
    else draw_inventory();
}

/* the wheel: the page's four slots around the d-pad, the equipped one framed, a missing one greyed */
#define WX 500.0f
#define WY 372.0f
#define WW 84.0f
#define WH 46.0f
void wpn_draw(void)
{
    static const float off[4][2] = { { 0, -50 }, { 90, 0 }, { 0, 50 }, { -90, 0 } };
    unsigned int* iv;
    int icons, d, any = 0;
    char t[96], k[32], k2[32];
    icons = pc_item_icons(ICON_USER, wheel || ms.open);
    if (!wheel || !plp) return;
    iv = inv();
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 6);
    for (d = 0; d < WPN_SLOTS; d++) any |= g_wpn_slots[chr()][d] != 0;
    for (d = 0; d < 4; d++) {
        int s = page * 4 + d, id = g_wpn_slots[chr()][s], have = id && wpn_bag_find(iv, bag_n(), id);
        int eq = have && iv[0] && ID(iv[iv[0]]) == id, two = (itemdata[id].type & 0x100) != 0;
        float cx = WX + off[d][0], cy = WY + off[d][1];
        pc_ui_rect(cx - WW / 2, cy - WH / 2, cx + WW / 2, cy + WH / 2, 0xB0000810, Z_UI);
        frame(cx - WW / 2, cy - WH / 2, cx + WW / 2, cy + WH / 2, eq ? 2 : 1, eq ? 0xFFF0D040 : 0xFF405878, Z_CUR);
        if (!id) { text_c(cx, cy - 8, "-", 0.6f, 0xFF707070); continue; }
        if (icons) pc_item_icon((unsigned int)id << 16, cx - (two ? 34 : 17), cy - 20, 0.5f, have ? 0xFFFFFFFF : 0xFF484848);
        text_fit(cx, cy + (icons ? 7 : -6), name(id), 0.36f, WW - 6, have ? 0xFFE0E0E0 : 0xFF707070, 1);
    }
    snprintf(t, sizeof(t), "%d-%d", page * 4 + 1, page * 4 + 4);
    text_c(WX, WY - 7, t, 0.45f, 0xFF90D0FF);
    snprintf(t, sizeof(t), "%s/%s: %d-%d", key_name(B_L1, k, sizeof(k)), key_name(B_R1, k2, sizeof(k2)), (!page) * 4 + 1, (!page) * 4 + 4);   /* the other page */
    text_c(WX + 90, WY + 40, t, 0.36f, 0xFFA0A0A0);
    if (!any) {
        float hw;
        snprintf(t, sizeof(t), tr("Assign them in the inventory: %s"), key_name(B_WPN_HOLD, k, sizeof(k)));
        float hx;
        hw = pc_ui_text_w(t, 0.42f) / 2 + 8;
        hx = WX + hw > 632 ? 632 - hw : WX;
        pc_ui_rect(hx - hw, WY - 96, hx + hw, WY - 78, 0xB0000000, Z_UI);
        text_c(hx, WY - 94, t, 0.42f, 0xFFFFE080);
    }
}
