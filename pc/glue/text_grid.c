/* The new menu's character grid (--new-menu; docs/MENUS.md "Text fields", the proposal's §1.5): one box for every text
 * field a controller has to fill (JOIN's Room Code or Address and Password, the host's Room Password, Player Name,
 * Co-op Server, the seeds). A-Z (SHIFT: a-z), 0-9, . - : and a few signs, SPACE, DELETE, PASTE, OK. The controller moves
 * on the grid and types the cell; the keyboard types as well (lobby_io.c's key ring, Ctrl+V pastes), the mouse clicks
 * cells. Triangle (Delete, Backspace) deletes the last character, Start (Enter) takes the text, Cancel (Esc, a right
 * click) leaves it as it was. Its cells and rules are pc/src/host/textgrid.h. The host draws nothing over it but its
 * background: the font's text would show through the box. Game thread. */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/adv.h"
#include "../../decomp/include/ps2/veronica/prog/padman.h"
#include "../../decomp/include/ps2/veronica/prog/sdfunc.h"
#include "../src/host/menu_layout.h"
#include "../src/host/menu_mouse.h"
#include "../src/host/lobby.h"
#include "../src/host/lang.h"
#include "../src/host/textgrid.h"
#include "../src/net/rdv_proto.h"
#include <stdio.h>
#include <string.h>

void host_log(const char* fmt, ...);
void pc_ui_text(float x, float y, const char* s, float sc, unsigned int argb);
float pc_ui_text_w(const char* s, float sc);
void pc_ui_rect(float x0, float y0, float x1, float y1, unsigned int argb, float z);

enum { P_UP = 0x1000, P_RIGHT = 0x2000, P_DOWN = 0x4000, P_LEFT = 0x8000, P_START = 0x800, P_TRI = 0x10 };   /* Pad[] bits */
#define X0 70.0f
#define CW 38.4f                        /* a cell */
#define CH 26.0f
#define Y0 132.0f                       /* the box's top */
#define GY (Y0 + 64.0f)                 /* the grid's */
#define Y1 (GY + TG_ROWS * CH + 34.0f)
#define Z 0.42f

static struct {
    int on, kind, max, masked, lower, r, c, pasting;
    char title[64], buf[128], keep[128];
    unsigned int blink;
} tg;
static MouseTake mt;

void tg_open(const char* title, const char* text, int max, int kind, int masked)
{
    memset(&tg, 0, sizeof(tg));
    tg.on = 1;
    tg.kind = kind;
    tg.max = max < (int)sizeof(tg.buf) - 1 ? max : (int)sizeof(tg.buf) - 1;
    tg.masked = masked;
    snprintf(tg.title, sizeof(tg.title), "%s", tr(title));
    snprintf(tg.buf, sizeof(tg.buf), "%.*s", tg.max, text ? text : "");
    tg.lower = kind == TGK_SERVER || kind == TGK_NAME;   /* (names and server names: lower case after the first letter is the usual) */
    if (kind == TGK_DIGITS) tg.r = 2;
    g_lobby_io.tail = g_lobby_io.head;   /* (nothing typed before counts) */
    g_lobby_io.text_on = 1;
    menu_mouse_take(&mt);
    CallSystemSe(0, 3);
    host_log("menu: text grid for %s\n", title);
}

int tg_active(void) { return tg.on; }
const char* tg_text(void) { return tg.buf; }

void tg_close(void)
{
    g_lobby_io.text_on = 0;
    rdv_wipe(tg.buf, sizeof(tg.buf));   /* (a password's copy) */
    rdv_wipe(tg.keep, sizeof(tg.keep));
    tg.on = 0;
}

static void put(int k)
{
    int n = (int)strlen(tg.buf);
    if (!tg_allowed(tg.kind, k) || n >= tg.max) { CallSystemSe(0, 1); return; }
    tg.buf[n] = (char)k;
    tg.buf[n + 1] = 0;
}
static void del(void) { int n = (int)strlen(tg.buf); if (n) tg.buf[n - 1] = 0; }

/* a cell's key pressed: 1 OK */
static int press_key(int k)
{
    switch (k) {
    case TG_SHIFT: tg.lower ^= 1; CallSystemSe(0, 2); return 0;
    case TG_SPACE: put(' '); return 0;
    case TG_DEL: del(); CallSystemSe(0, 2); return 0;
    case TG_PASTE: g_lobby_io.paste_req = 1; tg.pasting = 1; CallSystemSe(0, 3); return 0;
    case TG_OK: return 1;
    }
    put(k);
    CallSystemSe(0, 2);
    return 0;
}

static float cell_x(int c) { return X0 + c * CW; }
static float cell_y(int r) { return GY + r * CH; }
static int cell_at(float x, float y, int* r, int* c)
{
    if (x < X0 || x >= X0 + TG_COLS * CW || y < GY || y >= GY + TG_ROWS * CH) return 0;
    *r = (int)((y - GY) / CH); *c = (int)((x - X0) / CW);
    if (*r == TG_ROWS - 1) *c = tg_special_first(*c);
    return 1;
}

static void draw(void)
{
    static const char* const sp[] = { "", "a/A", "SPACE", "DELETE", "PASTE", "OK" };
    char shown[130], g[40];
    float x, tw;
    int n = (int)strlen(tg.buf);
    pc_ui_rect(X0 - 14, Y0, 640 - X0 + 14, Y1, 0xF0181818, Z);
    pc_ui_rect(X0 - 14, Y0, 640 - X0 + 14, Y0 + 2, 0xFFD6B668, Z + 0.001f);
    pc_ui_text(X0, Y0 + 8, tg.title, 0.55f, 0xFFD6B668);
    /* the text, its end blinking */
    pc_ui_rect(X0, Y0 + 30, 640 - X0, Y0 + 54, 0xFF000000, Z + 0.001f);
    if (tg.masked) { memset(shown, '*', (size_t)n); shown[n] = 0; } else snprintf(shown, sizeof(shown), "%s", tg.buf);
    tw = pc_ui_text_w(shown, 0.6f);
    x = tw > 640 - 2 * X0 - 16 ? 640 - X0 - 8 - tw : X0 + 6;   /* (a long one: its end) */
    pc_ui_text(x, Y0 + 33, shown, 0.6f, 0xFFFFFFFF);
    if (tg.blink++ & 16) pc_ui_rect(x + tw + 1, Y0 + 34, x + tw + 3, Y0 + 51, 0xFFFFFFFF, Z + 0.003f);
    snprintf(g, sizeof(g), "%d / %d", n, tg.max);
    pc_ui_text(640 - X0 - pc_ui_text_w(g, 0.4f), Y0 + 12, g, 0.4f, 0xFF808080);
    /* the grid */
    for (int r = 0; r < TG_ROWS; r++)
        for (int c = 0; c < TG_COLS; c++) {
            int k = tg_key(r, c, tg.lower), sel, ok;
            float w = CW;
            char t[8];
            const char* s = t;
            if (r == TG_ROWS - 1) { if (c != tg_special_first(c)) continue; w = CW * tg_special_w(c); s = sp[-k]; }
            else { t[0] = (char)k; t[1] = 0; }
            sel = r == tg.r && c == (r == TG_ROWS - 1 ? tg_special_first(tg.c) : tg.c);
            ok = k < 0 || tg_allowed(tg.kind, k);
            pc_ui_rect(cell_x(c) + 2, cell_y(r) + 2, cell_x(c) + w - 2, cell_y(r) + CH - 2, sel ? 0xFFD6B668 : 0xFF3A3A3A, Z + 0.002f);
            pc_ui_text(cell_x(c) + (w - pc_ui_text_w(s, 0.55f)) / 2, cell_y(r) + 4, s, 0.55f, sel ? 0xFF000000 : ok ? 0xFFFFFFFF : 0xFF707070);
        }
    /* what the buttons do */
    x = X0;
    if (ui_pad()) {
        x = ui_hint(x, Y1 - 24, ui_glyph(UG_OK, g, sizeof(g)), "Type", 0);
        x = ui_hint(x, Y1 - 24, ui_glyph(UG_DEFAULT, g, sizeof(g)), "Delete", 0);
        x = ui_hint(x, Y1 - 24, ui_glyph(UG_START, g, sizeof(g)), "OK", 0);
    } else {
        x = ui_hint(x, Y1 - 24, "Enter", "OK", 0);
        x = ui_hint(x, Y1 - 24, "Backspace", "Delete", 0);
        x = ui_hint(x, Y1 - 24, "Ctrl\tV", "Paste", 0);
    }
    ui_hint(x, Y1 - 24, ui_glyph(UG_BACK, g, sizeof(g)), "Cancel", 0);
    AdvEasySetTextureList(0);
}

/* every frame while it is up: 0 on, 1 OK (tg_text), -1 cancelled; drawn */
int tg_run(int port)
{
    unsigned int press = Pad[port].press, rept = Pad[port].Rept;
    unsigned int ok = (sys->keytype & 3) < 3 ? AdvGetOkButton() : 0xC0, cancel = (sys->keytype & 3) < 3 ? AdvGetCancelButton() : 0x20;
    int r = 0, hr, hc, hit;
    if (!tg.on) return -1;
    menu_mouse_take(&mt);
    ui_device(press, mt.moved || mt.left || mt.right || g_lobby_io.head != g_lobby_io.tail);
    if (tg.pasting && __atomic_load_n(&g_lobby_io.paste_req, __ATOMIC_ACQUIRE) == 2) {
        tg.pasting = 0;
        g_lobby_io.paste_req = 0;
        for (const char* p = g_lobby_io.paste; *p && *p != '\n' && *p != '\r'; p++)
            if (tg_allowed(tg.kind, (unsigned char)*p) && (int)strlen(tg.buf) < tg.max) put((unsigned char)*p);
    }
    while (!r && g_lobby_io.tail != __atomic_load_n(&g_lobby_io.head, __ATOMIC_ACQUIRE)) {   /* the keyboard */
        int k = g_lobby_io.keys[g_lobby_io.tail & 63];
        g_lobby_io.tail++;
        if (k == LK_ENTER) r = 1;
        else if (k == LK_ESC) r = -1;
        else if (k == LK_BACK || k == LK_DEL) del();
        else if (k >= 32 && k < 127) put(k);
    }
    hit = cell_at(mt.x, mt.y, &hr, &hc);
    if (!r && hit && (mt.moved || mt.left) && (hr != tg.r || hc != tg.c)) { tg.r = hr; tg.c = hc; if (!mt.left) CallSystemSe(0, 2); }
    if (r) ;
    else if (mt.left && hit) r = press_key(tg_key(tg.r, tg.c, tg.lower));
    else if (mt.right || (press & cancel)) r = -1;
    else if (press & P_START) r = 1;
    else if (press & ok) r = press_key(tg_key(tg.r, tg.c, tg.lower));
    else if (press & P_TRI) { del(); CallSystemSe(0, 2); }
    else if (rept & (P_UP | P_DOWN | P_LEFT | P_RIGHT)) {
        tg_move(&tg.r, &tg.c, (rept & P_UP) ? -1 : (rept & P_DOWN) ? 1 : 0, (rept & P_LEFT) ? -1 : (rept & P_RIGHT) ? 1 : 0);
        CallSystemSe(0, 2);
    }
    if (r) { g_lobby_io.text_on = 0; CallSystemSe(0, r > 0 ? 3 : 0); }
    else draw();
    if (r < 0) tg_close();
    else if (r > 0) tg.on = 0;   /* (the caller takes tg_text, then tg_close wipes it) */
    return r;
}
