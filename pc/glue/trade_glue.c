/* The co-op trade screen (docs/COOP.md "Trade screen"): drawn in the game's style - its font
 * (lobby_glue.c pc_ui_text), its inventory cells and its own item icons - with our inventory on the
 * left and the partner's on the right, and its input: d-pad/stick, confirm, cancel, mouse. The
 * request, the protocol and the swap are coop_glue.c's (g_trade). Game thread: trade_pad from pad.c
 * bhSetPad (after menu_pad), trade_draw from game.c bhMainSequence (after the frame's drawing).
 *
 * The icons are the status screen's (ITEM AFS file 145, textures 3 and 4: 68x48 cells, a two-slot
 * item 136x48, picked as sub1.c ItemSet does). The game loads that file with palettes in shared
 * banks (palbuf), which the room's textures use while the world is drawn behind the screen; the two
 * icon textures are loaded here with their own CLUT instead (PictFormat not 5..8: Ps2TexLoad sends
 * the TIM2's CLUT with it), under global indices of our own, for as long as the screen is open.
 */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/item.h"
#include "../../decomp/include/ps2/veronica/prog/adv.h"
#include "../../decomp/include/ps2/veronica/prog/sdfunc.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_texture.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_NaTextureFunction.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_NaSprite.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_NinjaCnk.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_NaSystem.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_NaDraw.h"
#include "../src/host/coop.h"
#include "../src/host/menu_mouse.h"
#include <stdio.h>
#include <string.h>

void host_log(const char* fmt, ...);
void pc_ui_text(float x, float y, const char* s, float sc, unsigned int argb);
float pc_ui_text_w(const char* s, float sc);
void pc_ui_rect(float x0, float y0, float x1, float y1, unsigned int argb, float z);
void pc_ui_arrow(float x, float y, int dir, unsigned int argb);
int pc_overlay_begin(int kind);
void pc_overlay_end(void);
uint32_t net_ms(void);
Sint32 ADXF_ReadNw32(ADXF adxf, Sint32 nsct, void* buf);   /* PC: synchronous */
extern ADV_WORK AdvWork;

int pc_trade_screen(void) { return coop_on && g_trade.phase == TRADE_OPEN; }

/* ---------------------------------------------------------------- icons */
static unsigned char raw[1100 * 1024] __attribute__((aligned(64)));   /* ITEM AFS file 145, then our two textures */
static NJS_TEXNAME icon_names[2];
static NJS_TEXLIST icons = { icon_names, 2 };
static int icon_state;                  /* 0 not loaded, 1 loaded, -1 failed (names only) */

static int icon_load(void)
{
    ADXF h;
    unsigned char *p = raw, *end, *out = raw, *chunk[5];
    int n = 0, ok, sz;
    h = ADXF_OpenAfs(sys->itm_partid, 145);
    if (!h) return 0;
    ok = ADXF_GetFsizeSct(h) * 2048 <= (int)sizeof(raw);
    if (ok) { ADXF_ReadNw32(h, ADXF_GetFsizeSct(h), raw); ok = ADXF_GetStat(h) == 3; }   /* synchronous on the PC */
    sz = ADXF_GetFsizeByte(h);
    ADXF_Close(h);
    if (!ok) return 0;
    end = raw + sz;
    while (p + 32 <= end && *(int*)p != -1 && n < 5) {   /* chunks: code, size, 24 bytes, data (PLI palettes, TIM2 pictures) */
        int csz = ((int*)p)[1];
        if (csz < 0 || p + 32 + csz > end) return 0;
        if (!memcmp(p, "TIM2", 4)) chunk[n++] = p;
        p += 32 + csz;
    }
    if (n != 5) return 0;
    for (n = 3; n < 5; n++) {   /* textures 3 (items 64..) and 4 (items 0..63), as the status screen numbers them */
        int csz = 32 + ((int*)chunk[n])[1];
        TIM2_PICTUREHEADER_EX* t;
        memmove(out, chunk[n], csz);
        t = (TIM2_PICTUREHEADER_EX*)(out + 32);
        if (t->ImageType != 5 || t->ImageWidth != 512) return 0;
        t->PictFormat = 0;                         /* its own CLUT, no palette bank */
        t->Gindex = 0x03C70E00 + n;                /* not the status screen's */
        out += csz;
    }
    ((int*)out)[0] = -1; ((int*)out)[1] = 0;
    bhGarbageTexture(NULL, 0);
    return bhSetMemPvpTexture(&icons, raw, 0) == 2;
}

static void icon_unload(void)
{
    if (icon_state == 1) { njReleaseTexture(&icons); bhGarbageTexture(NULL, 0); }
    icon_state = 0;
}

void trade_textures_released(void) { icon_state = 0; }   /* njReleaseTextureAll freed them */

/* the icon of item word w (sub1.c ItemSet), drawn at x, y */
static void icon(unsigned int w, float x, float y)
{
    static const struct { unsigned char from, to; } remap[] = { { 30, 13 }, { 63, 41 }, { 128, 41 }, { 129, 41 }, { 130, 35 }, { 136, 47 },
                                                                { 138, 48 }, { 142, 128 }, { 143, 49 }, { 144, 102 }, { 145, 133 } };
    int id = (w >> 16) & 0xFF, no = id, item, two = (itemdata[id].type & 0x100) != 0, i;
    NJS_TEXANIM a;
    NJS_SPRITE s;
    NJS_ARGB c = { 1.0f, 1.0f, 1.0f, 1.0f };
    if (icon_state != 1 || !id) return;
    for (i = 0; i < (int)(sizeof(remap) / sizeof(remap[0])); i++) if (remap[i].from == id) no = remap[i].to;
    item = no;
    if (item > 63) no -= 64;
    else if (no && no < 4) no = no * 2 - 1;
    else if (no < 33) { if (no) no += 4; }
    else if (no == 33 || no == 34) no += no - 33 + 4;
    else no += 6;
    memset(&a, 0, sizeof(a));
    a.sx = two ? 136 : 68; a.sy = 48;
    a.u1 = (no % 7) * 34; a.v1 = (no / 7) * 24;
    a.u2 = a.u1 + (two ? 68 : 34); a.v2 = a.v1 + 24;
    a.texid = item < 64 ? 1 : 0;
    memset(&s, 0, sizeof(s));
    s.p.x = x; s.p.y = y; s.p.z = -1.0f;
    s.sx = s.sy = 1.0f;
    s.tlist = &icons;
    s.tanim = &a;
    njSetConstantMaterial(&c);
    njSetTexture(&icons);
    njDrawSprite2D(&s, 0, -1.0f, 0x20 | 0x2);
}

/* ---------------------------------------------------------------- layout (game 2D: 640x480) */
#define GX0 72.0f                       /* our grid; the partner's at GX1 */
#define GX1 432.0f
#define GY 96.0f
#define CW 68.0f
#define CH 48.0f
#define Z_DIM 1.25f                     /* as the game's screen dimming: in front of the world */
#define Z_UI 1.3f
#define BTN_Y 372.0f                    /* our CONFIRM and CANCEL buttons / the partner's state */
#define OK_X0 (GX0 - 10.0f)
#define OK_X1 (GX0 + CW - 2.0f)
#define NO_X0 (GX0 + CW + 2.0f)
#define NO_X1 (GX0 + 2 * CW + 10.0f)
#define WANT_Y 58.0f                    /* the partner's request (a button: offer it), ours below */
#define LEAVE_X0 284.0f
#define LEAVE_X1 356.0f
#define LEAVE_Y 316.0f

static float cell_x(int side, int c) { return (side ? GX1 : GX0) + (c & 1) * CW; }
static float cell_y(int c) { return GY + (c >> 1) * CH; }

static void frame(float x0, float y0, float x1, float y1, float t, unsigned int argb)
{
    pc_ui_rect(x0, y0, x1, y0 + t, argb, Z_UI);
    pc_ui_rect(x0, y1 - t, x1, y1, argb, Z_UI);
    pc_ui_rect(x0, y0, x0 + t, y1, argb, Z_UI);
    pc_ui_rect(x1 - t, y0, x1, y1, argb, Z_UI);
}

static void text_c(float cx, float y, const char* s, float sc, unsigned int argb) { pc_ui_text(cx - pc_ui_text_w(s, sc) / 2, y, s, sc, argb); }

static unsigned int pulse(unsigned int rgb) { return ((unsigned int)AdvWork.FlushCount2 / 2 + 128) << 24 | rgb; }

/* the cells a mark covers (a two-slot item: both), 0 when none */
static int span(int side, int cell, const signed char* w, int* a, int* b)
{
    int cells = g_trade.cells[side];
    if (cell < 0 || cell >= cells) return 0;
    *a = *b = cell;
    if (w[cell] >= 0) { if (cell > 0 && w[cell - 1] == w[cell]) *a = cell - 1; if (cell + 1 < cells && w[cell + 1] == w[cell]) *b = cell + 1; }
    return 1;
}

/* one side's grid: cells, icons, counts, the equipped mark, the request, the selection, our cursor */
static void grid(int side)
{
    signed char w[10];
    int c, a, b, cells = g_trade.cells[side], sel = g_trade.sel[side];
    unsigned int selcol = g_trade.ready[side] ? 0xFF40E040 : side ? 0xFF40C0FF : 0xFFFFE060;
    coop_trade_layout(g_trade.grid[side], cells, w);
    for (c = 0; c < cells; c++) {   /* the cells first (a two-slot item: one frame over its row), then what is in them */
        int two = w[c] >= 0 && c + 1 < cells && w[c + 1] == w[c];
        if (w[c] >= 0 && c > 0 && w[c - 1] == w[c]) continue;
        pc_ui_rect(cell_x(side, c) + 1, cell_y(c) + 1, cell_x(side, c) + (two ? 2 : 1) * CW - 1, cell_y(c) + CH - 1, 0xC0081830, Z_UI);
        frame(cell_x(side, c), cell_y(c), cell_x(side, c) + (two ? 2 : 1) * CW, cell_y(c) + CH, 1, 0xFF405878);
    }
    for (c = 0; c < cells; c++) {
        float x = cell_x(side, c), y = cell_y(c);
        unsigned int wd = w[c] >= 0 ? g_trade.grid[side][(int)w[c]] : 0;
        int first = w[c] >= 0 && (c == 0 || w[c - 1] != w[c]);
        if (first) {
            int id = (wd >> 16) & 0xFF;
            char b[16];
            if (icon_state == 1) icon(wd, x, y);
            else {   /* no icons (texture memory): the name in the cell */
                snprintf(b, sizeof(b), "%.10s", coop_item_name(wd));
                pc_ui_text(x + 4, y + 16, b, 0.4f, 0xFFE0E0E0);
            }
            if (itemdata[id].type & 0x3800) {   /* KazuSet: weapons and ammunition show their count */
                snprintf(b, sizeof(b), (wd & 0x8000000) ? "INF" : "%u", wd & 0xFFFF);
                pc_ui_text(x + ((itemdata[id].type & 0x100) ? 2 * CW : CW) - pc_ui_text_w(b, 0.5f) - 4, y + CH - 17, b, 0.5f, 0xFF60F060);
            }
            if (w[c] == g_trade.eq[side]) pc_ui_text(x + 4, y + 2, "E", 0.5f, 0xFFF0D040);
        }
    }
    if (span(side, g_trade.want[!side], w, &a, &b)) {   /* requested (ours of theirs on its side, theirs of ours on ours): orange */
        pc_ui_rect(cell_x(side, a) + 1, cell_y(a) + 1, cell_x(side, b) + CW - 1, cell_y(b) + CH - 1, 0x50FF8000, Z_UI);
        frame(cell_x(side, a) + 1, cell_y(a) + 1, cell_x(side, b) + CW - 1, cell_y(b) + CH - 1, 2, 0xFFFF9020);
        pc_ui_text(cell_x(side, b) + CW - pc_ui_text_w(side ? "ASKED" : "WANTED", 0.4f) - 4, cell_y(a) + 3, side ? "ASKED" : "WANTED", 0.4f, 0xFFFFB040);
    }
    if (span(side, sel, w, &a, &b)) frame(cell_x(side, a) - 2, cell_y(a) - 2, cell_x(side, b) + CW + 2, cell_y(b) + CH + 2, 3, selcol);
    if (side == g_trade.cur_side && span(side, g_trade.cur, w, &a, &b))   /* the cursor: a pulsing frame inside the cell */
        frame(cell_x(side, a) + 4, cell_y(a) + 4, cell_x(side, b) + CW - 4, cell_y(b) + CH - 4, 2, pulse(0xFFFFFF));
}

static unsigned int sel_word(int side)
{
    signed char w[10];
    int s = g_trade.sel[side];
    if (s < 0 || s >= g_trade.cells[side]) return 0;
    coop_trade_layout(g_trade.grid[side], g_trade.cells[side], w);
    return w[s] >= 0 ? g_trade.grid[side][(int)w[s]] : 0;
}

static void draw_screen(void)
{
    char b[96];
    unsigned int mine = sel_word(0), theirs = sel_word(1), now = net_ms();
    int side;
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 6);
    if (pc_overlay_begin(PC_OVL_FADE)) {   /* the whole view, also in widescreen */
        NJS_POLYGON_VTX v[4] = { { 0, 0, Z_DIM, 0xB0000000 }, { 0, 480, Z_DIM, 0xB0000000 }, { 640, 0, Z_DIM, 0xB0000000 }, { 640, 480, Z_DIM, 0xB0000000 } };
        njDrawPolygon(v, 4, 1);
        pc_overlay_end();
    }
    text_c(320, 24, "TRADE", 1.0f, 0xFFFFFFFF);
    for (side = 0; side < 2; side++) {
        float cx = (side ? GX1 : GX0) + CW;
        unsigned int w = side ? theirs : mine;
        text_c(cx, 64, side ? g_trade.name : "YOU", 0.7f, side ? 0xFF90D0FF : 0xFFFFE080);
        if (side && g_trade.sel[1] < 0 && !g_trade.grid[1][0] && !g_trade.ready[1]) text_c(cx, GY + 100, "...", 0.8f, 0xFFA0A0A0);
        else grid(side);
        snprintf(b, sizeof(b), "%s", w ? coop_item_name(w) : g_trade.sel[side] >= 0 ? "(nothing)" : "");
        text_c(cx, GY + 5 * CH + 6, b, 0.55f, 0xFFE0E0E0);
        if (side) text_c(cx, BTN_Y, g_trade.ready[1] ? "READY" : "CHOOSING", 0.6f, g_trade.ready[1] ? 0xFF40E040 : 0xFF909090);
        else {
            int can = coop_trade_back_what() != TB_LEAVE;
            frame(OK_X0, BTN_Y - 4, OK_X1, BTN_Y + 22, 1, g_trade.ready[0] ? 0xFF40E040 : 0xFF6080A0);
            text_c((OK_X0 + OK_X1) / 2, BTN_Y + 1, g_trade.ready[0] ? "READY" : "CONFIRM", 0.45f, g_trade.ready[0] ? 0xFF40E040 : 0xFFFFFFFF);
            frame(NO_X0, BTN_Y - 4, NO_X1, BTN_Y + 22, 1, can ? 0xFF6080A0 : 0xFF404850);
            text_c((NO_X0 + NO_X1) / 2, BTN_Y + 1, "CANCEL", 0.45f, can ? 0xFFFFFFFF : 0xFF707070);
        }
    }
    /* requests: the partner's (a button: offer it) and ours */
    if (g_trade.want[1] >= 0) {
        float hw;
        snprintf(b, sizeof(b), "%s requests: %s", g_trade.name, coop_item_name(g_trade.want_w[1]));
        hw = pc_ui_text_w(b, 0.5f) / 2 + 8;
        pc_ui_rect(320 - hw, WANT_Y - 3, 320 + hw, WANT_Y + 15, 0xC0402000, Z_UI);
        frame(320 - hw, WANT_Y - 3, 320 + hw, WANT_Y + 15, 1, pulse(0xFF9020));
        text_c(320, WANT_Y, b, 0.5f, 0xFFFFB040);
    }
    if (g_trade.want[0] >= 0) {
        snprintf(b, sizeof(b), "You request: %s", coop_item_name(g_trade.want_w[0]));
        text_c(320, WANT_Y + 20, b, 0.45f, 0xFFFFB040);
    }
    /* the middle: what goes which way */
    if (mine) { text_c(320, 150, coop_item_name(mine), 0.5f, 0xFFFFE080); pc_ui_arrow(352, 176, 1, 0xFFFFE080); pc_ui_rect(288, 175, 345, 177, 0xFFFFE080, Z_UI); }
    if (theirs) { text_c(320, 206, coop_item_name(theirs), 0.5f, 0xFF90D0FF); pc_ui_arrow(288, 232, -1, 0xFF90D0FF); pc_ui_rect(295, 231, 352, 233, 0xFF90D0FF, Z_UI); }
    if (g_trade.waiting) text_c(320, 262, "Trading...", 0.55f, 0xFFFFFFFF);
    frame(LEAVE_X0, LEAVE_Y - 4, LEAVE_X1, LEAVE_Y + 22, 1, 0xFF6080A0);
    text_c(320, LEAVE_Y, "LEAVE", 0.6f, 0xFFFFFFFF);
    /* the result of the last swap, else why this pair does not swap */
    if (now < g_trade.msg_until) text_c(320, 406, g_trade.msg, 0.55f, 0xFFFFFFFF);
    else if (g_trade.why[0] && (mine || theirs || g_trade.sel[1] < 0)) text_c(320, 406, g_trade.why, 0.55f, 0xFFF0C060);
    else if (g_trade.ready[0] && !g_trade.ready[1]) { snprintf(b, sizeof(b), "Waiting for %s to confirm", g_trade.name); text_c(320, 406, b, 0.55f, 0xFFC0C0C0); }
    {
        static const char* const back[] = { "", "withdraw the request", "take back the offer", "decline the request", "leave" };
        snprintf(b, sizeof(b), "Confirm: offer item, then ready. On %s's side: request item. Cancel: %s", g_trade.name, back[coop_trade_back_what()]);
        text_c(320, 438, b, 0.42f, 0xFFA0A0A0);
    }
}

/* the request line (asking, asked) and the last notice, at the top of the view */
static void draw_hint(void)
{
    const char* s = coop_skip_hint[0] ? coop_skip_hint : g_trade.hint[0] ? g_trade.hint : net_ms() < g_trade.msg_until ? g_trade.msg : NULL;
    float w;
    if (!s || !(sys->tk_flg & 0x80)) return;
    w = pc_ui_text_w(s, 0.6f);
    njColorBlendingMode(0, 8);
    njColorBlendingMode(1, 6);
    pc_ui_rect(320 - w / 2 - 10, 20, 320 + w / 2 + 10, 46, 0xA0000000, Z_UI);
    text_c(320, 24, s, 0.6f, g_trade.phase == TRADE_ASKED || s == coop_skip_hint ? pulse(0xFFE060) | 0xC0000000 : 0xFFFFFFFF);
}

void trade_draw(void)
{
    if (!coop_on || g_trade.phase != TRADE_OPEN) {
        if (icon_state) icon_unload();
        if (coop_on) draw_hint();
        return;
    }
    if (!icon_state) {
        icon_state = icon_load() ? 1 : -1;
        host_log("coop: trade screen icons %s\n", icon_state > 0 ? "loaded" : "not loaded (names only)");
    }
    draw_screen();
}

/* ---------------------------------------------------------------- input */
static int hit_cell(float x, float y, int* side)
{
    int col, row, c;
    for (*side = 0; *side < 2; ++*side) {
        float x0 = *side ? GX1 : GX0;
        if (x < x0 || x >= x0 + 2 * CW || y < GY) continue;
        col = (int)((x - x0) / CW); row = (int)((y - GY) / CH);
        c = row * 2 + col;
        return c < g_trade.cells[*side] ? c : -1;
    }
    return -1;
}

static int in(float x, float y, float x0, float y0, float x1, float y1) { return x >= x0 && x < x1 && y >= y0 && y < y1; }

/* the cursor: the d-pad/stick within a grid, over a two-slot item's other half, across to the other side */
static void move(int dx, int dy)
{
    signed char w[10];
    int side = g_trade.cur_side, c = g_trade.cur;
    coop_trade_layout(g_trade.grid[side], g_trade.cells[side], w);
    if (dx) {
        int t = c + dx;
        if (w[c] >= 0 && (t >> 1) == (c >> 1) && w[t] == w[c]) t += dx;
        if (t < 0 || (t >> 1) != (c >> 1)) {   /* the grid's edge: the other side's facing column, same row */
            if ((dx > 0) == side) return;
            side = !side;
            t = (c & ~1) + !side;
            if (t >= g_trade.cells[side]) t = g_trade.cells[side] - 2 + !side;
        }
        c = t;
    }
    if (dy) c += 2 * dy;
    if (c < 0 || c >= g_trade.cells[side]) return;
    coop_trade_cursor(side, c);
}

/* pad.c bhSetPad (after menu_pad): while the screen is open its input is the screen's and the
   player stands still; afterwards the buttons still held stay off until let go */
unsigned int trade_test_pad;            /* test aid (RECVX_COOP_TSCREEN P<hex>): presses for the next frame */
unsigned int trade_pad(unsigned int pad)
{
    static unsigned int prev, mask, last_mouse = ~0u;
    unsigned int press;
    int left, right, c, side;
    float mx, my;
    pad = coop_skip_pad(pad);   /* co-op: the skip button in a cutscene both play asks first (coop_glue.c) */
    if (!pc_trade_screen()) {
        if (mask) { mask &= pad; pad &= ~mask; }
        prev = 0;
        return pad;
    }
    press = (pad & ~prev) | trade_test_pad;
    trade_test_pad = 0;
    prev = mask = pad;
    g_menu_mouse.active = 1;
    left = __atomic_exchange_n(&g_menu_mouse.left, 0, __ATOMIC_RELAXED);
    right = __atomic_exchange_n(&g_menu_mouse.right, 0, __ATOMIC_RELAXED);
    __atomic_exchange_n(&g_menu_mouse.left_up, 0, __ATOMIC_RELAXED);
    __atomic_exchange_n(&g_menu_mouse.wheel, 0, __ATOMIC_RELAXED);
    if (!menu_mouse_on()) left = right = 0;
    mx = g_menu_mouse.x / 16.0f; my = g_menu_mouse.y / 16.0f;
    c = hit_cell(mx, my, &side);
    /* hover only moves the cursor (and only when the mouse moved): what is offered stays */
    if (menu_mouse_on() && (unsigned int)(g_menu_mouse.x ^ g_menu_mouse.y << 16) != last_mouse && last_mouse != ~0u && c >= 0) coop_trade_cursor(side, c);
    last_mouse = (unsigned int)(g_menu_mouse.x ^ g_menu_mouse.y << 16);
    if (press & 0x4) move(-1, 0);
    if (press & 0x8) move(1, 0);
    if (press & 0x1) move(0, -1);
    if (press & 0x2) move(0, 1);
    if (press & 0x4000) coop_trade_cursor(!g_trade.cur_side, g_trade.cur < g_trade.cells[!g_trade.cur_side] ? g_trade.cur : 0);   /* status (Tab): the other side */
    if (left) {
        char b[96];
        float hw = 0;
        if (g_trade.want[1] >= 0) { snprintf(b, sizeof(b), "%s requests: %s", g_trade.name, coop_item_name(g_trade.want_w[1])); hw = pc_ui_text_w(b, 0.5f) / 2 + 8; }
        if (c >= 0) { coop_trade_cursor(side, c); coop_trade_activate(); }
        else if (in(mx, my, OK_X0, BTN_Y - 4, OK_X1, BTN_Y + 22)) coop_trade_ready();
        else if (in(mx, my, NO_X0, BTN_Y - 4, NO_X1, BTN_Y + 22)) coop_trade_back(0);
        else if (in(mx, my, LEAVE_X0, LEAVE_Y - 4, LEAVE_X1, LEAVE_Y + 22)) coop_trade_leave();
        else if (hw > 0 && in(mx, my, 320 - hw, WANT_Y - 3, 320 + hw, WANT_Y + 15)) coop_trade_offer_wanted();
    }
    if (press & 0xB00) coop_trade_activate();   /* confirm (Cross / the key type's fire) */
    if ((press & 0x1000) || right) coop_trade_back(1);   /* cancel: one step back (request, offer, decline), then leave */
    return 0;
}
