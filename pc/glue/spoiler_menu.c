/* The BioRand spoiler log screen (--dev, F8; docs/BIORAND_INGAME.md "Spoiler log"): the pack's log_pl0.txt
 * (biorand_spoiler, pc/src/host/spoiler.c) as a list in the game's style - each room by area with its item spots,
 * the original item's icon and name, then what BioRand put there; or by the door randomizer's segments (their rooms,
 * the keys their locked doors need), or without doors by the parts BioRand placed the key items in. Key items only on
 * a toggle; the room the player is in is marked and shown first.
 *
 * It is a pause: F8 asks bhSysCallEvent for the game's own pause (system.c, as its PAUSE: ss_flg 0x80000000, every
 * control stopped by sp_flg 0, the world drawn as it stands, the music playing on), this screen is drawn in PAUSE's
 * place (bhSysCallScreenSaver mode 10) and the pause ends when it closes (bhSysCallEvent restores the pad and the
 * controls). The play time stands still too. Not in co-op (the partner's game runs on).
 *
 * Nothing is read per open: the log is read once per pack (biorand.c), the item icons are the trade screen's
 * textures (trade_glue.c pc_item_icons: loaded while a user wants them, released after; user ICON_USER), the rows are
 * a static array rebuilt in place. Game thread, except spoiler_key (main thread: F8, Escape). */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/sdfunc.h"
#include "../../decomp/include/ps2/veronica/prog/padman.h"
#include "../../decomp/include/ps2/veronica/prog/item.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_NaDraw.h"
#include "../src/host/biorand.h"
#include "../src/host/spoiler.h"
#include "../src/host/cheat.h"
#include "../src/host/lang.h"
#include "../src/host/lobby.h"
#include "../src/host/menu_mouse.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

void host_log(const char* fmt, ...);
uint64_t gs_frame_count(void);
void pc_ui_text(float x, float y, const char* s, float sc, unsigned int argb);
float pc_ui_text_w(const char* s, float sc);
void pc_ui_rect(float x0, float y0, float x1, float y1, unsigned int argb, float z);
void pc_ui_arrow(float x, float y, int dir, unsigned int argb);
int pc_overlay_begin(int kind);
void pc_overlay_end(void);
int pc_item_icons(int user, int on);
void pc_item_icon(unsigned int w, float x, float y, float sc, unsigned int argb, float z);
unsigned int AdvGetCancelButton(void);
extern int g_dev_tools, coop_on;

enum { BTN_L2 = 0x1, BTN_R2 = 0x2, BTN_L1 = 0x4, BTN_R1 = 0x8, BTN_TRI = 0x10, BTN_SQUARE = 0x80, BTN_SELECT = 0x100,
       BTN_START = 0x800, BTN_UP = 0x1000, BTN_RIGHT = 0x2000, BTN_DOWN = 0x4000, BTN_LEFT = 0x8000 };   /* Pad[] bits */
enum { K_F8 = 1, K_ESC = 2 };
#define ICON_USER 4                     /* pc_item_icons: 1 the trade screen, 2 the weapon shortcuts */
/* layout (game 2D, 640x480) */
#define LX 28.0f                        /* the list's left and right */
#define RX 600.0f
#define LIST_Y 76.0f
#define RH 25.0f                        /* a row: an icon at half size is 24 high */
#define VIS 15
#define HELP_Y 456.0f
#define ICON_SC 0.5f
#define OLD_X (LX + 40)                 /* an item row: the spot, the original's icon and name, the arrow, the new one's */
#define OLD_NAME_X (OLD_X + 72)
#define ARROW_X 316.0f
#define NEW_X 326.0f
#define NEW_NAME_X (NEW_X + 72)
#define TAB_Y 46.0f
#define Z_DIM 1.25f                     /* as the trade screen: in front of the world */
#define Z_UI 1.3f

enum { R_AREA, R_ROOM, R_ITEM, R_SEG, R_NEED, R_PART, R_OTHER, R_INVHEAD, R_INV, R_TEXT };
typedef struct { unsigned char kind; unsigned char sub; short idx; } Row;   /* sub: R_NEED / R_INV the first icon */
#define MAX_ROWS (SP_ITEMS * 2 + SP_ROOMS + 3 * SP_SEGS + 32)
#define PER_LINE 9                      /* icons on an R_NEED / R_INV row */

static Row rows[MAX_ROWS];
static struct {
    int open, closing;                  /* open: the screen is up; closing: closed, the pause ends at the next bhSysCallEvent */
    int view, keys;                     /* view 0 by area, 1 by segment (or part); keys: key items only */
    int top, n;                         /* the first row shown, the rows */
    int state;                          /* SPOILER_*, or -1 no BioRand game */
    int here;                           /* the player's room (its code), -1 */
    int stv;                            /* the play time at the open */
    unsigned int frames;                /* drawn while open (the log) */
    const Spoiler* sp;
    MouseTake mt;
} sm;
static int keys_req, req_age;           /* spoiler_key's, the frames an F8 waited for a pause */
static void set_view(int v);
static void toggle_keys(void);

/* ---------------------------------------------------------------- texts */
/* id's name: the game's in its language (cheat_glue.c read_names), else BioRand's split at its capitals */
static const char* item_name(int id, const char* br, char* buf, int n)
{
    int k = 0;
    if (id == 0) return tr("(nothing)");
    if (id > 0 && id < CHEAT_ITEMS && g_cheat.names[id][0]) return g_cheat.names[id];
    if (!br[0]) return "?";
    for (const char* s = br; *s && k < n - 2; s++) {
        if (s > br && *s >= 'A' && *s <= 'Z' && s[-1] >= 'a' && s[-1] <= 'z') buf[k++] = ' ';
        buf[k++] = *s;
    }
    buf[k] = 0;
    return buf;
}

/* "Cell block (start)" (area view: without its area's "Prison: "), else "Room 11" */
static const char* room_name(unsigned short code, int short_name, char* buf, int n)
{
    const char* nm = tp_place_name(spoiler_room_stage(code), spoiler_room_room(code)), *c;
    if (!nm) snprintf(buf, n, tr("Room %d"), spoiler_room_room(code));
    else snprintf(buf, n, "%s", short_name && (c = strstr(nm, ": ")) ? c + 2 : nm);
    return buf;
}

/* ---------------------------------------------------------------- the rows */
static int shown(const SpItem* it) { return !sm.keys || it->key; }
static void add(int kind, int idx, int sub)
{
    if (sm.n >= MAX_ROWS) return;
    rows[sm.n].kind = (unsigned char)kind;
    rows[sm.n].sub = (unsigned char)sub;
    rows[sm.n++].idx = (short)idx;
}

/* the room's shown items (of part p, -2 any): its row and theirs; 0 none shown */
static int add_room(const Spoiler* sp, int r, int p)
{
    int i, any = 0;
    for (i = 0; i < sp->nitem; i++)
        if (sp->item[i].room == sp->room[r].code && shown(&sp->item[i]) && (p == -2 || (sp->item[i].key && sp->item[i].part == p))) {
            if (!any++) add(R_ROOM, r, 0);
            add(R_ITEM, i, 0);
        }
    return any;
}

static int has(const Spoiler* sp, int r, int p)
{
    for (int i = 0; i < sp->nitem; i++)
        if (sp->item[i].room == sp->room[r].code && shown(&sp->item[i]) && (p == -2 || (sp->item[i].key && sp->item[i].part == p))) return 1;
    return 0;
}

static void build(void)
{
    const Spoiler* sp = sm.sp;
    int r, s, k, stage = -99;
    sm.n = 0;
    if (sm.state != SPOILER_OK) { add(R_TEXT, sm.state, 0); return; }
    if (!sp->nitem) { add(R_TEXT, 100, 0); return; }
    if (sm.view == 0) {
        if (sp->ninv && !sm.keys) {
            add(R_INVHEAD, 0, 0);
            for (k = 0; k < sp->ninv; k += PER_LINE) add(R_INV, 0, k);
        }
        for (r = 0; r < sp->nroom; r++) {
            if (!has(sp, r, -2)) continue;
            if (spoiler_room_stage(sp->room[r].code) != stage) add(R_AREA, stage = spoiler_room_stage(sp->room[r].code), 0);
            add_room(sp, r, -2);
        }
    } else if (spoiler_has_doors(sp)) {
        for (s = 0; s <= sp->nseg; s++) {   /* (nseg: the rooms of no segment) */
            int head = 0;
            for (r = 0; r < sp->nroom; r++) {
                if ((s < sp->nseg ? sp->room[r].seg != s : sp->room[r].seg >= 0) || !has(sp, r, -2)) continue;
                if (!head++) {
                    add(s < sp->nseg ? R_SEG : R_OTHER, s, 0);
                    for (k = 0; s < sp->nseg && k < sp->seg[s].nneed; k += PER_LINE) add(R_NEED, s, k);
                }
                add_room(sp, r, -2);
            }
        }
    } else {
        add(R_TEXT, 101, 0);
        for (s = 0; s < sp->parts; s++) {
            add(R_PART, s, 0);
            for (r = 0; r < sp->nroom; r++) add_room(sp, r, s);
        }
    }
}

static int max_top(void) { return sm.n > VIS - 1 ? sm.n - (VIS - 1) : 0; }   /* (VIS - 1: a pinned heading takes a row) */
static void scroll_to(int t) { sm.top = t < 0 ? 0 : t > max_top() ? max_top() : t; }
static int head(int k) { return k == R_AREA || k == R_SEG || k == R_PART || k == R_OTHER || k == R_INVHEAD; }

/* the row of the player's room (any of its story cases when not this one), -1 */
static int here_row(void)
{
    int i, near = -1;
    for (i = 0; i < sm.n; i++) {
        unsigned short c;
        if (rows[i].kind != R_ROOM) continue;
        c = sm.sp->room[rows[i].idx].code;
        if (c == sm.here) return i;
        if (near < 0 && (c & 0xFFF0) == (sm.here & 0xFFF0)) near = i;
    }
    return near;
}

static void jump_here(void)
{
    int i = here_row();
    if (i >= 0) scroll_to(i > 0 && head(rows[i - 1].kind) ? i - 1 : i);
}

static void rebuild(int keep_here)
{
    build();
    if (keep_here) jump_here();
    else scroll_to(sm.top);
}

/* ---------------------------------------------------------------- open, close */
static int tex_used(void)   /* the game's texture entries in use (the log: the icons come and go) */
{
    int n = 0;
    for (int i = 0; i < PC_TEX_ENTRIES; i++) n += tbuf[i].count != 0;
    return n;
}

static void open(void)
{
    sm.sp = NULL;
    sm.state = -1;
    if (biorand_active()) sm.sp = biorand_spoiler(&sm.state);
    sm.here = (sys->stg_no + 1) << 12 | (sys->rom_no & 0xFF) << 4 | (sys->rcase & 0xF);
    sm.stv = sys->stv_tm;
    sm.top = 0;
    sm.open = 1;
    memset(&sm.mt, 0, sizeof(sm.mt));
    rebuild(1);
    pc_item_icons(ICON_USER, 1);
    CallSystemSe(0, 3);
    sm.frames = 0;
    host_log("spoiler: open in %04X (%d rows); play time %d, player at %.1f %.1f motion %u frame %d; %d texture entries in use\n", sm.here, sm.n,
             sys->stv_tm, plp->px, plp->pz, plp->mtn_no, plp->frm_no, tex_used());
}

static void close_screen(void)
{
    if (!sm.open) return;
    sm.open = 0;
    sm.closing = 1;
    host_log("spoiler: closed after %u frames; play time %d, player at %.1f %.1f motion %u frame %d\n", sm.frames, sys->stv_tm, plp->px, plp->pz,
             plp->mtn_no, plp->frm_no);   /* (the play time is put back below: sync.c may have counted this frame) */
    sys->stv_tm = sm.stv;
    pc_item_icons(ICON_USER, 0);
    g_menu_mouse.active = 0;
    g_lobby_io.list_on = 0;
    CallSystemSe(0, 0);
}

void spoiler_key(int k) { __atomic_or_fetch(&keys_req, k == 1 ? K_F8 : K_ESC, __ATOMIC_ACQ_REL); }   /* main thread */

int pc_spoiler_want(void)
{
    if (!(__atomic_load_n(&keys_req, __ATOMIC_ACQUIRE) & K_F8)) return 0;
    __atomic_and_fetch(&keys_req, ~K_F8, __ATOMIC_ACQ_REL);
    if (coop_on) host_log("spoiler: not in co-op (the partner's game runs on)\n");
    if (!g_dev_tools || coop_on || sm.open || sm.closing) return 0;
    open();
    return 1;
}

int pc_spoiler_up(void) { return sm.open || sm.closing; }
int pc_spoiler_done(void)
{
    if (!sm.closing) return 0;
    sm.closing = 0;
    sys->stv_tm = sm.stv;   /* (the vertical blanks since the close) */
    return 1;
}

/* every frame (biorand_frame): an F8 that found no pause to take is dropped; a pause that ended otherwise (a soft
   reset) takes the screen with it */
void spoiler_tick(void)
{
    static int st_from = -1, st_n, st_t;   /* test aid RECVX_SPOILER_STRESS="f:n": from presented frame f, n opens and closes */
    if (st_from == -1) {
        const char* e = getenv("RECVX_SPOILER_STRESS");
        st_from = e && sscanf(e, "%d:%d", &st_from, &st_n) == 2 ? st_from : -2;
    }
    if (st_n > 0 && st_from >= 0 && gs_frame_count() >= (uint64_t)st_from && ++st_t % 6 == 0) {   /* F8 every 6 frames: open, close */
        if (sm.open) {   /* a bit of each: a scroll, every 5th the other view, every 7th the key items */
            scroll_to(sm.top + st_n % 40);
            if (st_n % 5 == 0) set_view(!sm.view);
            if (st_n % 7 == 0) toggle_keys();
            if (--st_n == 0) host_log("spoiler: stress done\n");
        }
        spoiler_key(1);
    }
    if (!sm.open && !sm.closing) {
        int k = __atomic_load_n(&keys_req, __ATOMIC_ACQUIRE);
        if (k & K_ESC) __atomic_and_fetch(&keys_req, ~K_ESC, __ATOMIC_ACQ_REL);
        if (!(k & K_F8)) req_age = 0;
        else if (++req_age > 3) { __atomic_and_fetch(&keys_req, ~K_F8, __ATOMIC_ACQ_REL); req_age = 0; }
        return;
    }
    if (!(sys->ss_flg & 0x80000000)) {
        close_screen();
        sm.closing = 0;
    }
}

/* ---------------------------------------------------------------- input */
static void group(int dir)
{
    int i = sm.top + dir;
    while (i >= 0 && i < sm.n && !head(rows[i].kind)) i += dir;
    if (i >= 0 && i < sm.n) { scroll_to(i); CallSystemSe(0, 2); }
    else if (dir > 0) scroll_to(max_top());
    else scroll_to(0);
}

static int in(float x, float y, float x0, float y0, float x1, float y1) { return x >= x0 && x < x1 && y >= y0 && y < y1; }
static float tab_x(int t) { return LX + t * 150; }
static float keys_x(void) { return RX - pc_ui_text_w("KEY ITEMS ONLY", 0.5f) - 20; }
static float close_x(void) { return RX - pc_ui_text_w("CLOSE", 0.5f) - 12; }

static void set_view(int v) { if (v != sm.view) { sm.view = v; sm.top = 0; rebuild(sm.view == 0); CallSystemSe(0, 2); } }
static void toggle_keys(void) { sm.keys = !sm.keys; sm.top = 0; rebuild(sm.view == 0); CallSystemSe(0, 2); }

static void input(void)
{
    unsigned int press = Pad[CurrentPortId].press, rept = Pad[CurrentPortId].Rept;
    unsigned int cancel = sys->keytype < 3 ? AdvGetCancelButton() : 0x40;
    MouseTake* m = &sm.mt;
    int k = __atomic_exchange_n(&keys_req, 0, __ATOMIC_ACQ_REL);
    g_menu_mouse.active = 1;   /* the cursor shows (pc_in_play is off in a pause) */
    g_lobby_io.list_on = 1;    /* Page Up / Down, Home, End (lobby_io.c) */
    menu_mouse_take(m);
    if (k || m->right || (press & (cancel | BTN_START))) { close_screen(); return; }
    while (g_lobby_io.tail != __atomic_load_n(&g_lobby_io.head, __ATOMIC_ACQUIRE)) {
        int key = g_lobby_io.keys[g_lobby_io.tail & 63];
        g_lobby_io.tail++;
        if (key == LK_PGUP) scroll_to(sm.top - VIS);
        else if (key == LK_PGDN) scroll_to(sm.top + VIS);
        else if (key == LK_HOME) scroll_to(0);
        else if (key == LK_END) scroll_to(max_top());
    }
    if (m->wheel) scroll_to(sm.top - 3 * m->wheel);
    if (m->left) {
        if (in(m->x, m->y, tab_x(0) - 6, TAB_Y - 4, tab_x(0) + 140, TAB_Y + 18)) set_view(0);
        else if (in(m->x, m->y, tab_x(1) - 6, TAB_Y - 4, tab_x(1) + 140, TAB_Y + 18)) set_view(1);
        else if (in(m->x, m->y, keys_x() - 4, TAB_Y - 4, RX + 6, TAB_Y + 18)) toggle_keys();
        else if (in(m->x, m->y, close_x() - 6, HELP_Y - 4, RX + 10, HELP_Y + 18)) close_screen();
        else if (sm.n > VIS && in(m->x, m->y, RX + 6, LIST_Y, RX + 20, LIST_Y + VIS * RH))   /* the scroll bar: there */
            scroll_to((int)((m->y - LIST_Y) / (VIS * RH) * sm.n) - VIS / 2);
    }
    if (rept & BTN_UP) scroll_to(sm.top - 1);
    else if (rept & BTN_DOWN) scroll_to(sm.top + 1);
    else if (rept & (BTN_LEFT | BTN_L2)) scroll_to(sm.top - VIS);
    else if (rept & (BTN_RIGHT | BTN_R2)) scroll_to(sm.top + VIS);
    else if (press & BTN_L1) group(-1);
    else if (press & BTN_R1) group(1);
    else if (press & BTN_TRI) set_view(!sm.view);
    else if (press & BTN_SQUARE) toggle_keys();
    else if (press & BTN_SELECT) { jump_here(); CallSystemSe(0, 2); }
}

/* ---------------------------------------------------------------- drawing */
static void text_fit(float x, float y, const char* s, float sc, float w, unsigned int argb)
{
    float tw = pc_ui_text_w(s, sc);
    if (tw > w && tw > 0) { float s2 = sc * w / tw; pc_ui_text(x, y + (sc - s2) * 12, s, s2, argb); }
    else pc_ui_text(x, y, s, sc, argb);
}

/* item id's icon at x, y (half size), else a frame with "?"; its width */
static float icon(int id, float x, float y)
{
    if (id > 0 && id < CHEAT_ITEMS) {
        pc_item_icon((unsigned int)id << 16, x, y, ICON_SC, 0xFFFFFFFF, 1.0f);
        return itemdata[id].type & 0x100 ? 68.0f : 34.0f;
    }
    pc_ui_rect(x + 4, y + 2, x + 30, y + 22, 0x60404040, Z_UI);
    pc_ui_text(x + 12, y + 5, id ? "?" : "-", 0.45f, 0xFF808080);
    return 34.0f;
}

static void item_text(int id, const char* br, int n, float x, float y, float w, unsigned int col)
{
    char nb[SP_NAME + 8], t[96];
    const char* nm = item_name(id, br, nb, sizeof(nb));
    if (n > 1) snprintf(t, sizeof(t), "%s x%d", nm, n);
    else snprintf(t, sizeof(t), "%s", nm);
    text_fit(x, y + 5, t, 0.48f, w, col);
}

static void draw_row(int i, float y)
{
    const Spoiler* sp = sm.sp;
    const Row* r = &rows[i];
    char b[160], nb[96];
    int k;
    switch (r->kind) {
    case R_TEXT: {
        const char* s = r->idx == -1 ? "No BioRand game: start one from EXTRA, BIORAND." :
                        r->idx == SPOILER_MISSING ? "This BioRand pack has no spoiler log (log_pl0.txt)." :
                        r->idx == SPOILER_BROKEN ? "The spoiler log could not be read: it is not a BioRand log." :
                        r->idx == 100 ? "The spoiler log lists no item spots." :
                        r->idx == 101 ? "Doors not randomized: the key items by the parts BioRand placed them in." : "";
        text_fit(LX, y + 5, s, 0.5f, RX - LX, r->idx == 101 ? 0xFFA0A0A0 : 0xFFF0C060);
        break;
    }
    case R_AREA: case R_SEG: case R_PART: case R_OTHER: case R_INVHEAD: {
        unsigned int col = r->kind == R_AREA ? 0xFFFFE080 : r->kind == R_INVHEAD ? 0xFFA0E0A0 : 0xFF90D0FF;
        pc_ui_rect(LX - 6, y + 1, RX, y + RH - 2, 0x70304868, Z_UI);
        if (r->kind == R_SEG) {   /* "SEGMENT 2", then where it starts and ends */
            char a[64];
            snprintf(b, sizeof(b), tr("SEGMENT %d"), r->idx + 1);
            text_fit(LX, y + 4, b, 0.55f, 124, col);
            snprintf(nb, sizeof(nb), "%s  -  %s", room_name(sp->seg[r->idx].from, 0, a, sizeof(a)), room_name(sp->seg[r->idx].to, 0, b, sizeof(b)));
            text_fit(LX + 130, y + 6, nb, 0.42f, RX - LX - 140, 0xFFB0C8E0);
            break;
        }
        if (r->kind == R_AREA) snprintf(b, sizeof(b), "%s", tp_stage_name(r->idx));
        else if (r->kind == R_PART) snprintf(b, sizeof(b), tr("KEY ITEMS, PART %d"), r->idx + 1);
        else snprintf(b, sizeof(b), "%s", r->kind == R_OTHER ? "OTHER ROOMS" : "START INVENTORY");
        text_fit(LX, y + 4, b, 0.55f, RX - LX - 8, col);   /* (pc_ui_text translates) */
        break;
    }
    case R_NEED: {
        float x = LX + 130;
        if (!r->sub) text_fit(LX + 8, y + 6, "Locks need:", 0.45f, 116, 0xFFB0B0B0);
        for (k = r->sub; k < sp->seg[r->idx].nneed && k < r->sub + PER_LINE; k++) x += icon(sp->seg[r->idx].need[k], x, y) + 4;
        break;
    }
    case R_INV: {
        float x = LX + 8;
        for (k = r->sub; k < sp->ninv && k < r->sub + PER_LINE; k++) {
            x += icon(sp->inv[k].id, x, y);
            if (sp->inv[k].n > 1) { snprintf(b, sizeof(b), "%d", sp->inv[k].n); pc_ui_text(x + 1, y + 8, b, 0.4f, 0xFF60F060); x += pc_ui_text_w(b, 0.4f) + 2; }
            x += 10;
        }
        break;
    }
    case R_ROOM: {
        const SpRoom* rm = &sp->room[r->idx];
        int here = rm->code == sm.here || i == here_row();
        float w;
        room_name(rm->code, sm.view == 0, nb, sizeof(nb));
        snprintf(b, sizeof(b), "RM_%d%02d%d  %04X", spoiler_room_stage(rm->code), spoiler_room_room(rm->code), spoiler_room_variant(rm->code), rm->code);
        w = pc_ui_text_w(b, 0.4f);
        if (here) pc_ui_rect(LX - 2, y + 2, RX, y + RH - 3, 0x5040C060, Z_UI);
        text_fit(LX + 6, y + 5, nb, 0.52f, RX - LX - w - 150, here ? 0xFF80FF80 : 0xFFFFFFFF);
        pc_ui_text(RX - w - 4, y + 7, b, 0.4f, 0xFF9090A0);
        if (here) text_fit(RX - w - 140, y + 7, "(you are here)", 0.4f, 130, 0xFF80FF80);
        else if (rm->enemy[0]) { snprintf(b, sizeof(b), tr("Enemy: %s"), rm->enemy); text_fit(RX - w - 140, y + 7, b, 0.4f, 130, 0xFFE09090); }
        break;
    }
    case R_ITEM: {
        const SpItem* it = &sp->item[r->idx];
        unsigned int col = it->key ? 0xFFFFE060 : 0xFFE0E0E0;
        if (it->key) pc_ui_rect(OLD_X - 4, y + 1, RX, y + RH - 2, 0x30FFD040, Z_UI);
        snprintf(b, sizeof(b), "%d", it->spot);
        pc_ui_text(LX + 14, y + 7, b, 0.4f, 0xFF8090A0);
        icon(it->old_id, OLD_X, y);
        item_text(it->old_id, it->old_name, it->old_n, OLD_NAME_X, y, ARROW_X - OLD_NAME_X - 14, 0xFFA8A8A8);
        pc_ui_rect(ARROW_X - 14, y + 11.5f, ARROW_X - 4, y + 13.5f, col, Z_UI);
        pc_ui_arrow(ARROW_X, y + 12.5f, 1, col);
        icon(it->new_id, NEW_X, y);
        item_text(it->new_id, it->new_name, it->new_n, NEW_NAME_X, y, RX - NEW_NAME_X - 4, col);
        break;
    }
    }
}

static void draw(void)
{
    char b[160];
    int i;
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 6);
    if (pc_overlay_begin(PC_OVL_FADE)) {   /* the whole view, also in widescreen */
        NJS_POLYGON_VTX v[4] = { { 0, 0, Z_DIM, 0xD8000008 }, { 0, 480, Z_DIM, 0xD8000008 }, { 640, 0, Z_DIM, 0xD8000008 }, { 640, 480, Z_DIM, 0xD8000008 } };
        njDrawPolygon(v, 4, 1);
        pc_overlay_end();
    }
    pc_ui_text(LX, 12, "SPOILER LOG", 0.8f, 0xFFFFFFFF);
    if (sm.sp) {
        snprintf(b, sizeof(b), "%s   BioRand %.20s   %s %s", biorand_seed_text(), sm.sp->version, sm.sp->player, sm.sp->scenario);
        text_fit(RX - pc_ui_text_w(b, 0.42f) < 260 ? 260 : RX - pc_ui_text_w(b, 0.42f), 20, b, 0.42f, RX - 260, 0xFF909090);
    }
    for (int t = 0; t < 2; t++) {
        const char* s = t == 0 ? "BY AREA" : sm.sp && !spoiler_has_doors(sm.sp) ? "BY PART" : "BY SEGMENT";
        unsigned int col = sm.view == t ? 0xFFFFFFFF : 0xFF808080;
        pc_ui_text(tab_x(t), TAB_Y, s, 0.55f, col);
        if (sm.view == t) pc_ui_rect(tab_x(t), TAB_Y + 18, tab_x(t) + pc_ui_text_w(s, 0.55f), TAB_Y + 20, 0xFFFFE080, Z_UI);
    }
    {   /* the key items toggle: a box, filled when on */
        float x = keys_x();
        pc_ui_rect(x, TAB_Y + 3, x + 12, TAB_Y + 15, sm.keys ? 0xFFFFE060 : 0xFF404850, Z_UI);
        pc_ui_text(x + 18, TAB_Y + 1, "KEY ITEMS ONLY", 0.5f, sm.keys ? 0xFFFFE060 : 0xFFA0A0A0);
    }
    pc_ui_rect(LX - 6, LIST_Y - 6, RX + 14, LIST_Y - 5, 0x80FFFFFF, Z_UI);
    {   /* the list; scrolled into a group, its heading stays pinned at the top */
        int h = sm.top, slot = 0;
        while (h >= 0 && !head(rows[h].kind)) h--;
        if (h >= 0 && h != sm.top && sm.n) { draw_row(h, LIST_Y); slot = 1; }
        for (i = sm.top; i < sm.n && slot < VIS; i++, slot++) draw_row(i, LIST_Y + slot * RH);
    }
    if (sm.n > VIS) {   /* the scroll bar: the page's share of the list, where it is */
        float y0 = LIST_Y, h = VIS * RH - 2, th = h * VIS / sm.n, ty;
        if (th < 12) th = 12;
        ty = y0 + (h - th) * sm.top / max_top();
        pc_ui_rect(RX + 8, y0, RX + 13, y0 + h, 0x40FFFFFF, Z_UI);
        pc_ui_rect(RX + 8, ty, RX + 13, ty + th, 0xC0FFFFFF, Z_UI);
    }
    pc_ui_rect(LX - 6, HELP_Y - 8, RX + 14, HELP_Y - 7, 0x80FFFFFF, Z_UI);
    text_fit(LX, HELP_Y, "Up/Down: scroll  L1/R1: groups  Triangle: view  Square: key items  Select: this room", 0.4f, close_x() - LX - 16, 0xFFA0A0A0);
    pc_ui_text(close_x(), HELP_Y - 2, "CLOSE", 0.5f, 0xFFFFFFFF);
}

int pc_spoiler_frame(void)
{
    if (!sm.open) return sm.closing;
    sys->stv_tm = sm.stv;   /* (sync.c counts it in the vertical blank) */
    input();
    if (sm.open) { draw(); sm.frames++; }
    return 1;
}
