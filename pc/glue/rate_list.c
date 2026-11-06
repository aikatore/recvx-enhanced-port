/* The enemy randomizer's "Custom" rates (docs/ENEMIES.md "Randomizer"): every kind it can place (enemies.h
 * enemy_rate_*), each with a slider Off, 1..10 (as BioRand's enemy ratio sliders) and its share of the draws, BACK
 * below. Drawn as the cutscene cast's "Custom" list (cast_list.c), with the menus' helpers (pc_ui_*), by the lobby's
 * RANDOMIZER page and the BIORAND screen's RANDOMIZER page, each at its own place. It edits g_enemy_opts.rate (recvx.ini
 * enemy_rates; a co-op host's go to the guest with its other enemy options, EV_EOPTS); read-only, it shows those in
 * effect (a co-op guest: the host's). Game thread. */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/sdfunc.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_NaDraw.h"
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
void host_log(const char* fmt, ...);

enum { BTN_L1 = 0x4, BTN_R1 = 0x8, BTN_UP = 0x1000, BTN_RIGHT = 0x2000, BTN_DOWN = 0x4000, BTN_LEFT = 0x8000 };   /* Pad[] bits */
#define BAR 7.0f                        /* the scroll bar's width, at the right edge */
#define GAP 6.0f
#define TRACK_W 84.0f                   /* a slider's track */
#define VAL_W 74.0f                     /* right of it: the value and the share */
/* depths: one per kind of box, row and scroll parity, so "Smooth menus" pairs each with its own (biorand_menu.c ROW_Z) */
#define ZR(base, i) ((base) + ((rl.top & 1) * 16 + (i)) * 0.0004f)
#define Z_SEL 0.3f
#define Z_TRACK 0.32f
#define Z_FILL 0.34f
#define Z_ARROW 0.36f

static struct { int n, cur, top, rows, ro; float x0, x1, y0, h, sc; } rl;   /* cur: an entry, or n (BACK) */

static unsigned char* rates(void) { return rl.ro ? (unsigned char*)enemy_opts()->rate : g_enemy_opts.rate; }
static float back_y(void) { return rl.y0 + rl.rows * rl.h + GAP; }
static int max_top(void) { return rl.n > rl.rows ? rl.n - rl.rows : 0; }
static float right(void) { return rl.n > rl.rows ? rl.x1 - BAR - 6 : rl.x1; }
static float track_x0(void) { return right() - VAL_W - TRACK_W; }

static void follow(void)
{
    if (rl.top > max_top()) rl.top = max_top();
    if (rl.top < 0) rl.top = 0;
    if (rl.cur < rl.n && rl.cur < rl.top) rl.top = rl.cur;
    if (rl.cur < rl.n && rl.cur >= rl.top + rl.rows) rl.top = rl.cur - rl.rows + 1;
}

void rate_list_open(int ro, float x0, float x1, float y0, float h, float sc, int rows)
{
    memset(&rl, 0, sizeof(rl));
    rl.ro = ro;
    rl.x0 = x0; rl.x1 = x1; rl.y0 = y0; rl.h = h; rl.sc = sc; rl.rows = rows;
    rl.n = enemy_rate_count();
    CallSystemSe(0, 3);
}

/* the cursor to c (wrapping through BACK, or clamped to the entries) */
static void to(int c, int wrap)
{
    if (wrap) c = (c % (rl.n + 1) + rl.n + 1) % (rl.n + 1);
    else c = c < 0 ? 0 : c >= rl.n ? rl.n - 1 : c;
    if (c != rl.cur) CallSystemSe(0, 2);
    rl.cur = c;
    follow();
}

/* entry k's rate to v (clamped; wrap: past the ends to the other) */
static void set(int k, int v, int wrap)
{
    unsigned char* r = &g_enemy_opts.rate[enemy_rate_at(k)];
    if (rl.ro) { CallSystemSe(0, 1); return; }
    if (wrap) v = (v + ENEMY_RATE_MAX + 1) % (ENEMY_RATE_MAX + 1);
    v = v < 0 ? 0 : v > ENEMY_RATE_MAX ? ENEMY_RATE_MAX : v;
    if (v == *r) return;
    *r = (unsigned char)v;
    settings_save_later();
    host_log("enemy rates: %s = %d\n", enemy_rate_name(enemy_rate_at(k)), v);
    CallSystemSe(0, 2);
}

/* the cursor position under the pointer, -1 none */
static int at(float x, float y)
{
    int i;
    if (x < rl.x0 - 8 || x > rl.x1) return -1;
    if (y >= back_y() - 3 && y < back_y() + rl.h - 3) return rl.n;
    if (y < rl.y0 - 3) return -1;
    i = (int)((y - rl.y0 + 3) / rl.h);
    return i < rl.rows && rl.top + i < rl.n ? rl.top + i : -1;
}

/* 1: BACK */
int rate_list_input(const MouseTake* m, unsigned int press, unsigned int rept, unsigned int ok, unsigned int cancel)
{
    int hit, page = rl.rows - 1;
    rl.n = enemy_rate_count();
    if (rl.cur > rl.n) rl.cur = rl.n;
    while (g_lobby_io.tail != __atomic_load_n(&g_lobby_io.head, __ATOMIC_ACQUIRE)) {   /* lobby_io.c: list_on */
        int k = g_lobby_io.keys[g_lobby_io.tail & 63];
        g_lobby_io.tail++;
        if (k == LK_PGUP) to(rl.cur - page, 0);
        else if (k == LK_PGDN) to(rl.cur + page, 0);
        else if (k == LK_HOME) to(0, 0);
        else if (k == LK_END) to(rl.n - 1, 0);
    }
    if (m->wheel) { to(rl.cur - m->wheel, 0); return 0; }   /* (a notch a row, as the BIORAND screen's) */
    hit = at(m->x, m->y);
    if (hit >= 0 && (m->moved || m->left) && hit != rl.cur) { rl.cur = hit; CallSystemSe(0, 2); }
    if (m->left && hit == rl.n) { CallSystemSe(0, 0); return 1; }
    if (m->left && hit >= 0) {   /* the track: that value; its arrows: a step */
        float t0 = track_x0(), t1 = t0 + TRACK_W;
        int v = rates()[enemy_rate_at(hit)];
        if (m->x >= t0 - 2 && m->x < t1 + 2) set(hit, (int)((m->x - t0) / TRACK_W * ENEMY_RATE_MAX + 0.5f), 0);
        else if (m->x >= t0 - 18 && m->x < t0) set(hit, v - 1, 0);
        else if (m->x >= t1 && m->x < t1 + 18) set(hit, v + 1, 0);
        return 0;
    }
    if (m->right || (press & cancel)) { CallSystemSe(0, 0); return 1; }
    if (rept & BTN_UP) to(rl.cur - 1, 1);
    else if (rept & BTN_DOWN) to(rl.cur + 1, 1);
    else if ((rept & BTN_L1)) to(rl.cur - page, 0);
    else if ((rept & BTN_R1)) to(rl.cur + page, 0);
    else if ((rept & (BTN_LEFT | BTN_RIGHT)) && rl.cur < rl.n) set(rl.cur, rates()[enemy_rate_at(rl.cur)] + ((rept & BTN_LEFT) ? -1 : 1), 0);
    else if (press & ok) {
        if (rl.cur >= rl.n) { CallSystemSe(0, 0); return 1; }
        set(rl.cur, rates()[enemy_rate_at(rl.cur)] + 1, 1);   /* (no left/right: up to 10, then Off) */
    }
    return 0;
}

static void tri(float x, float y, int dir, unsigned int argb, float z)
{
    NJS_POLYGON_VTX v[3] = { { x, y, z, argb }, { x - dir * 7.0f, y - 6.0f, z, argb }, { x - dir * 7.0f, y + 6.0f, z, argb } };
    njDrawPolygon(v, 3, 1);
}

void rate_list_draw(void)
{
    const unsigned char* r = rates();
    int k, total = 0;
    float t0 = track_x0(), t1 = t0 + TRACK_W, y;
    char v[16];
    rl.n = enemy_rate_count();
    for (k = 0; k < rl.n; k++) total += r[enemy_rate_at(k)];
    for (k = rl.top; k < rl.n && k < rl.top + rl.rows; k++) {
        int i = k - rl.top, sel = rl.cur == k, rate = r[enemy_rate_at(k)];
        unsigned int col = sel ? 0xFFFFFFFF : rate ? 0xFFD0D0D0 : 0xFF808080, ac = rl.ro ? 0xFF606060 : col;
        const char* nm = enemy_rate_name(enemy_rate_at(k));
        float w = pc_ui_text_w(nm, rl.sc), lim = t0 - 22 - rl.x0, sc = w > lim ? rl.sc * lim / w : rl.sc, f = (float)rate / ENEMY_RATE_MAX;
        y = rl.y0 + i * rl.h;
        if (sel) pc_ui_rect(rl.x0 - 8, y - 2, right(), y + rl.h - 3, 0x50FFFFFF, ZR(Z_SEL, i));
        pc_ui_text(rl.x0, y + (rl.sc - sc) * 14, nm, sc, col);   /* (a long name: narrower) */
        tri(t0 - 10, y + 9, -1, ac, ZR(Z_ARROW, i));
        tri(t1 + 11, y + 9, 1, ac, ZR(Z_ARROW, i));
        if (f > 0) pc_ui_rect(t0, y + 5.5f, t0 + TRACK_W * f, y + 12.5f, sel && !rl.ro ? 0xFFFFE080 : 0xFFB8A868, ZR(Z_FILL, i));
        if (f < 1) pc_ui_rect(t0 + TRACK_W * f, y + 5.5f, t1, y + 12.5f, 0xFF34343C, ZR(Z_TRACK, i));
        if (rate) snprintf(v, sizeof(v), "%d", rate);
        pc_ui_text(t1 + 15, y, rate ? v : "Off", rl.sc * 0.9f, col);
        snprintf(v, sizeof(v), "%d%%", total ? (rate * 100 + total / 2) / total : 0);   /* its share of the draws where every kind fits */
        pc_ui_text(right() - pc_ui_text_w(v, 0.42f), y + 3, v, 0.42f, 0xFF909090);
    }
    if (rl.n > rl.rows) {   /* the scroll bar: the page's share of the list, where it is */
        float y0 = rl.y0 - 2, h = rl.rows * rl.h - 1, th = h * rl.rows / rl.n, ty;
        if (th < 12) th = 12;
        ty = y0 + (h - th) * rl.top / max_top();
        pc_ui_rect(rl.x1 - BAR, y0, rl.x1 - BAR + 5, y0 + h, 0x40FFFFFF, Z_SEL);
        pc_ui_rect(rl.x1 - BAR, ty, rl.x1 - BAR + 5, ty + th, 0xC0FFFFFF, Z_SEL + 0.001f);
    }
    y = back_y();
    if (rl.cur >= rl.n) pc_ui_rect(rl.x0 - 8, y - 2, rl.x1, y + rl.h - 3, 0x50FFFFFF, Z_SEL + 0.02f);
    pc_ui_text(rl.x0, y, "BACK", rl.sc, rl.cur >= rl.n ? 0xFFFFFFFF : 0xFFB0B0B0);
}

const char* rate_list_help(void)
{
    if (rl.cur >= rl.n) return rl.ro ? "Back to the randomizer page." : "Back. Your choices are saved.";
    if (rl.ro) return "The host's rates. Only the host can change them.";
    return "How often this enemy is chosen, against the others. Note: Some rooms can only contain certain enemies.";
}

/* "9 of 11": the kinds with a rate */
const char* rate_list_summary(const unsigned char* rate, char* buf, int n)
{
    int on = 0, total = enemy_rate_count();
    for (int k = 0; k < total; k++) on += rate[enemy_rate_at(k)] != 0;
    snprintf(buf, (size_t)n, tr("%d of %d"), on, total);
    return buf;
}
