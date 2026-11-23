/* WHAT'S NEW (docs/MENUS.md): the changelog this build carries (CHANGELOG.md, src/host/changelog.h), from a small
 * button in the title menu's top right corner (top left in Arabic): a click on it, or Triangle (its key: S), whose icon
 * the button shows (Button Icons). A gold dot on it until it is opened in this version (ini whatsnew_seen). The screen is
 * title mode 30 (lobby_glue.c pc_lobby) in the row screen's look: the panel, the breadcrumb (the version at the top of
 * the list), the text, a scroll bar and the button bar. The version this build is (its section, or "Unreleased" in a
 * build after a release) comes first, the earlier ones after a separator. Up / Down a line, Left / Right or Page Up /
 * Down a page, L1 / R1 a version, Home / End, the wheel, the scroll bar dragged; Cancel, Triangle, Esc or a right click
 * go back. The changelog stays English (the game's font, our own bold and bullets); its labels are translated.
 * Same screen with the new menu and Menu Style Legacy. Game thread. */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/adv.h"
#include "../../decomp/include/ps2/veronica/prog/padman.h"
#include "../../decomp/include/ps2/veronica/prog/sdfunc.h"
#include "../src/host/menu_mouse.h"
#include "../src/host/menu_layout.h"
#include "../src/host/bindings.h"
#include "../src/host/settings.h"
#include "../src/host/lang.h"
#include "../src/host/changelog.h"
#include "../src/net/update.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void host_log(const char* fmt, ...);
extern ADV_WORK AdvWork;
void pc_ui_text(float x, float y, const char* s, float sc, unsigned int argb);
float pc_ui_text_w(const char* s, float sc);
void pc_ui_rect(float x0, float y0, float x1, float y1, unsigned int argb, float z);
void pc_ui_raw(int on);   /* lobby_glue.c: texts drawn as they are (the changelog is English) */
void ui_dot(float x, float y);   /* ui_rows.c */

enum { P_L1 = 0x4, P_R1 = 0x8, P_TRI = 0x10, P_SELECT = 0x100, P_UP = 0x1000, P_RIGHT = 0x2000, P_DOWN = 0x4000, P_LEFT = 0x8000 };   /* Pad[] bits */
enum { SC_ESC = 41, SC_HOME = 74, SC_PGUP = 75, SC_END = 77, SC_PGDN = 78 };   /* SDL scancodes (bind_keys) */
enum { D_SEP = 9 };                     /* a display line's kind past CL_*: EARLIER VERSIONS */

/* layout (640 x 480, ui_rows.c's panel) */
#define TX0 24.0f                       /* the text */
#define TX1 604.0f
#define VY0 70.0f
#define VY1 444.0f
#define BAR_X 619.0f                    /* the scroll bar */
#define Y_BAR 458.0f
#define SC_TEXT 0.45f
#define LINE_H 14.0f
#define Z_DIM 0.012f
#define C_GOLD 0xFFD6B668u
#define C_ROW 0xFFA0A09Bu
#define C_TEXT 0xFFB0B0AAu
#define C_BOLD 0xFFFFFFFFu
#define C_HEAD 0xFFE6DFCAu
#define C_THIS 0xFF70D070u

typedef struct DLine {
    const char* s;                      /* its text (CL_BOLD toggles), n bytes */
    unsigned short n;
    unsigned char kind, bold, bullet;   /* bold at its start; the first line of a bullet */
    float x, sc, pad, h, y;             /* pad: space above it; y: its top in the list */
    const char* label;                  /* the version shown at the top of the list (breadcrumb), English */
} DLine;

static struct {
    int open, parsed, wrapped_font;     /* wrapped_font: the font the lines were measured with (+1) */
    ClLine* cl; int ncl; char* buf;
    DLine* d; int nd, cap;
    int top, max_top, drag;
    unsigned char keys[80];
    int hover;                          /* the title's button: under the pointer */
    float bx0, bx1, by0, by1;           /* ... where it is (left to right; 0 not drawn) */
    float hx[4][2]; int hact[4], nh;    /* the bar's hints: x ranges, what they do */
    MouseTake mt;
} wn;

static const char* new_version(void) { return strcmp(g_whatsnew_seen, update_installed()) ? update_installed() : NULL; }

/* ---------------------------------------------------------------- the title's button */
#define BTN_SC 0.5f
void pc_whatsnew_button(float fade)
{
    ADV_WORK* ap = &AdvWork;
    float a = fade * (1.0f - ap->FadeRate) * (1.0f - ap->SaverRate), gw, lw, x0, x1 = 630, y0 = 9, y1 = 29, rx0;
    unsigned int al;
    char g[40];
    const char* label = "WHAT'S NEW";
    int rtl = lang_rtl_text();
    if (a > 1) a = 1;
    wn.bx1 = 0;
    if (a < 0.03f) return;
    al = (unsigned int)(a * 255.0f) << 24;
    ui_glyph(UG_TRI, g, sizeof(g));
    gw = ui_hint_w(g, "") - 5;
    lw = pc_ui_text_w(label, BTN_SC);
    x0 = x1 - (7 + gw + 6 + lw + 8);
    wn.bx0 = x0; wn.bx1 = x1; wn.by0 = y0; wn.by1 = y1;
    rx0 = rtl ? 640 - x1 : x0;
    pc_ui_rect(rx0, y0, rx0 + x1 - x0, y1, (unsigned int)(a * (wn.hover ? 0xA0 : 0x90)) << 24 | (wn.hover ? 0x303030 : 0), 0.3f);
    pc_ui_rect(rx0, y0, rx0 + x1 - x0, y0 + 1, (unsigned int)(a * 0x60) << 24 | 0xFFFFFF, 0.301f);
    pc_ui_rect(rx0, y1 - 1, rx0 + x1 - x0, y1, (unsigned int)(a * 0x60) << 24 | 0xFFFFFF, 0.301f);
    if (wn.hover) pc_ui_rect(rtl ? 640 - x0 - 3 : x0, y0, rtl ? 640 - x0 : x0 + 3, y1, al | (C_GOLD & 0xFFFFFF), 0.302f);
    if (a > 0.5f) {   /* (the icon is drawn opaque: shown once the menu is half faded in) */
        ui_hint(x0 + 7, y0 + 3, g, "", 0);
        if (new_version()) ui_dot(x1 - 1, y0 + 1);
    }
    pc_ui_text(rtl ? 640 - (x0 + 7 + gw + 6) - lw : x0 + 7 + gw + 6, y0 + 2, label, BTN_SC, al | ((wn.hover ? C_BOLD : C_ROW) & 0xFFFFFF));
    AdvEasySetTextureList(0);   /* the title's texture list again */
}

/* the title menu (lobby_glue.c pc_title_mouse, level 0): hover, a click or Triangle: 1 open the screen */
int pc_whatsnew_title(const MouseTake* m, unsigned int press)
{
    float x = lang_rtl_text() ? 640 - m->x : m->x;
    int hit = wn.bx1 > 0 && x >= wn.bx0 && x < wn.bx1 && m->y >= wn.by0 && m->y < wn.by1;
    if (hit && !wn.hover && m->moved) CallSystemSe(0, 2);
    wn.hover = hit;
    return (hit && m->left) || (press & P_TRI);
}

/* ---------------------------------------------------------------- the lines */
static void add(const char* s, int n, int kind, int bold, int bullet, float x, float sc, float pad, float h, const char* label)
{
    DLine* l;
    if (wn.nd == wn.cap) {
        DLine* d = realloc(wn.d, sizeof(DLine) * (wn.cap ? wn.cap * 2 : 1024));
        if (!d) return;
        wn.d = d;
        wn.cap = wn.cap ? wn.cap * 2 : 1024;
    }
    l = &wn.d[wn.nd++];
    *l = (DLine){ s, (unsigned short)n, (unsigned char)kind, (unsigned char)bold, (unsigned char)bullet, x, sc, pad, h, 0, label };
}

static float seg_w(const char* s, int n, float sc)   /* (without the bold toggles) */
{
    char t[600];
    int k = 0;
    for (int i = 0; i < n && k < (int)sizeof(t) - 1; i++) if (s[i] != CL_BOLD) t[k++] = s[i];
    t[k] = 0;
    return pc_ui_text_w(t, sc);
}

/* a paragraph or bullet as lines at most TX1 - x wide (a word at a time) */
static void wrap(const char* s, int kind, float x, float pad, const char* label)
{
    int bold = 0, first = 1;
    while (*s) {
        const char *fit = NULL, *e = s;
        for (;;) {
            const char* we = e;
            while (*we == ' ') we++;
            while (*we && *we != ' ') we++;
            if (fit && seg_w(s, (int)(we - s), SC_TEXT) > TX1 - x) break;
            fit = e = we;
            if (!*we) break;
        }
        add(s, (int)(fit - s), kind, bold, first && kind == CL_ITEM, x, SC_TEXT, first ? pad : 0, LINE_H, label);
        for (const char* p = s; p < fit; p++) bold ^= *p == CL_BOLD;
        first = 0;
        s = fit;
        while (*s == ' ') s++;
    }
}

static const char* version_label(const ClLine* c) { return c->text[0] ? c->text : "Unreleased changes"; }

static void build(void)
{
    int first = -1, sec = 0;
    const char* label = "";
    float y = 0;
    if (!wn.parsed) {
        size_t n;
        const char* md = changelog_text(&n);
        wn.parsed = 1;
        wn.cl = md ? malloc(sizeof(ClLine) * 4096) : NULL;
        wn.buf = md ? malloc(n + 64) : NULL;
        if (wn.cl && wn.buf) wn.ncl = changelog_parse(md, n, wn.buf, n + 64, wn.cl, 4096);
        host_log("whatsnew: %d changelog lines from %u bytes\n", wn.ncl, (unsigned)(md ? n : 0));
    }
    if (wn.wrapped_font == lang_font() + 1) return;
    wn.wrapped_font = lang_font() + 1;
    wn.nd = 0;
    for (int i = 0; i < wn.ncl && first < 0; i++)   /* this build's version: the first with a change (an empty "Unreleased" is not) */
        if (wn.cl[i].kind == CL_VERSION)
            for (int j = i + 1; j < wn.ncl && wn.cl[j].kind != CL_VERSION; j++) if (wn.cl[j].kind == CL_ITEM) { first = i; break; }
    pc_ui_raw(1);
    for (int i = first < 0 ? wn.ncl : first; i < wn.ncl; i++) {
        const ClLine* c = &wn.cl[i];
        switch (c->kind) {
        case CL_VERSION:
            label = version_label(c);
            if (++sec == 2) add("", 0, D_SEP, 0, 0, TX0, 0.5f, 14, 22, label);
            add(c->text, (int)strlen(c->text), CL_VERSION, sec == 1, 0, TX0, 0.62f, sec == 2 ? 2 : sec > 2 ? 12 : 0, 22, label);   /* (bold: this version) */
            break;
        case CL_HEAD: add(c->text, (int)strlen(c->text), CL_HEAD, 0, 0, TX0 + 4, 0.5f, 4, 18, label); break;
        case CL_ITEM: wrap(c->text, CL_ITEM, c->depth ? 58.0f : 40.0f, 3, label); break;
        default: wrap(c->text, CL_TEXT, TX0 + 4, 3, label); break;
        }
    }
    pc_ui_raw(0);
    for (int i = 0; i < wn.nd; i++) { wn.d[i].y = y; y += wn.d[i].pad + wn.d[i].h; }
    wn.max_top = 0;
    for (int i = wn.nd - 1; i >= 0; i--)
        if (y - wn.d[i].y > VY1 - VY0) { wn.max_top = i + 1; break; }
    host_log("whatsnew: %d lines shown, from %s\n", wn.nd, first >= 0 ? version_label(&wn.cl[first]) : "nothing");
}

static float list_h(void) { return wn.nd ? wn.d[wn.nd - 1].y + wn.d[wn.nd - 1].pad + wn.d[wn.nd - 1].h : 0; }

/* ---------------------------------------------------------------- the screen */
static void set_top(int t) { wn.top = t < 0 ? 0 : t > wn.max_top ? wn.max_top : t; }
static void page(int dir)   /* a screen's height on, less a line */
{
    float h = VY1 - VY0 - LINE_H;
    int t = wn.top;
    if (dir > 0) while (t < wn.max_top && wn.d[t + 1].y - wn.d[wn.top].y <= h) t++;
    else while (t > 0 && wn.d[wn.top].y - wn.d[t - 1].y <= h) t--;
    if (t == wn.top) t += dir;
    set_top(t);
}
static void version_step(int dir)   /* to the previous / next version's heading (or the separator above it) */
{
    int t = dir > 0 ? wn.max_top : 0;
    for (int i = 0; i < wn.nd; i++) {
        if (wn.d[i].kind != D_SEP && (wn.d[i].kind != CL_VERSION || (i && wn.d[i - 1].kind == D_SEP))) continue;
        if (dir > 0 && i > wn.top) { t = i; break; }
        if (dir < 0 && i < wn.top) t = i;
    }
    set_top(t);
    CallSystemSe(0, 2);
}
static int key_edge(int sc)
{
    const bool* k = bind_keys();
    int down = k && k[sc], was = wn.keys[sc];
    wn.keys[sc] = (unsigned char)down;
    return down && !was;
}
static int bar_at(float x, float y)   /* the hint's action, 0 none (x: left to right) */
{
    if (y < Y_BAR - 3 || y > Y_BAR + 18) return 0;
    for (int i = 0; i < wn.nh; i++) if (x >= wn.hx[i][0] && x < wn.hx[i][1]) return wn.hact[i];
    return 0;
}

/* 1 back */
static int input(void)
{
    ADV_WORK* ap = &AdvWork;
    unsigned int press = Pad[ap->PortId].press, rept = Pad[ap->PortId].Rept;
    int esc = key_edge(SC_ESC), pgup = key_edge(SC_PGUP), pgdn = key_edge(SC_PGDN), home = key_edge(SC_HOME), end = key_edge(SC_END), b;
    MouseTake* m = &wn.mt;
    float x;
    menu_mouse_take(m);
    x = lang_rtl_text() ? 640 - m->x : m->x;
    ui_device(press, m->moved || m->left || m->right || m->wheel || esc || pgup || pgdn || home || end);
    if (press || rept || m->left || m->right || m->moved || m->wheel || pgup || pgdn || home || end) StopAdvScreenSaver(0);
    if (m->right || esc || (press & (AdvGetCancelButton() | P_SELECT | P_TRI))) { CallSystemSe(0, 0); return 1; }
    if (m->left && (b = bar_at(x, m->y))) {
        if (b == UG_BACK) { CallSystemSe(0, 0); return 1; }
        if (b == UG_PAGE) { page(1); CallSystemSe(0, 2); }
        if (b == UG_CAT) version_step(1);
        return 0;
    }
    if (m->left && m->x >= BAR_X - 8 && m->x <= BAR_X + 10 && m->y >= VY0 - 4 && m->y <= VY1 + 4) wn.drag = 1;   /* (the bar: not mirrored) */
    if (wn.drag && !(g_menu_mouse.held & 1) && !m->left) wn.drag = 0;
    if (wn.drag && wn.max_top) {   /* the list follows the pointer along the bar */
        float f = (m->y - VY0) / (VY1 - VY0), want = (f < 0 ? 0 : f > 1 ? 1 : f) * wn.d[wn.max_top].y;
        int t = 0;
        while (t < wn.max_top && wn.d[t].y < want) t++;
        set_top(t);
        return 0;
    }
    if (m->wheel) set_top(wn.top - 3 * m->wheel);
    if (rept & P_UP) set_top(wn.top - 1);
    else if (rept & P_DOWN) set_top(wn.top + 1);
    else if ((rept & P_LEFT) || pgup) { page(-1); CallSystemSe(0, 2); }
    else if ((rept & P_RIGHT) || pgdn) { page(1); CallSystemSe(0, 2); }
    else if (press & P_L1) version_step(-1);
    else if (press & P_R1) version_step(1);
    else if (home) set_top(0);
    else if (end) set_top(wn.max_top);
    return 0;
}

/* a line's text: bold parts brighter */
static void draw_line(const DLine* l, float y)
{
    int bold = l->bold;
    float x = l->x;
    char t[600];
    for (int i = 0; i < l->n;) {
        int k = 0;
        while (i < l->n && l->s[i] != CL_BOLD && k < (int)sizeof(t) - 1) t[k++] = l->s[i++];
        t[k] = 0;
        if (k) { pc_ui_text(x, y, t, l->sc, bold ? C_BOLD : C_TEXT); x += pc_ui_text_w(t, l->sc); }
        if (i < l->n && l->s[i] == CL_BOLD) { bold ^= 1; i++; }
    }
}

static void draw(void)
{
    const char* title = "WHAT'S NEW";
    const char* path[1] = { wn.nd ? wn.d[wn.top].label : "" };
    char g[4][40];
    float y = VY0, x = 20;
    AdvEasySetTextureList(0);
    AdvDwawOnePictureEx(0, 0xFFFFFFFF);   /* the title picture, darkened, as the other screens */
    pc_ui_rect(0, 0, 640, 480, 0xB0000000, Z_DIM);
    pc_ui_text(320 - pc_ui_text_w(title, 0.95f) / 2, 10, title, 0.95f, 0xFFFFFFFF);
    ui_frame_plain("WHAT'S NEW", path, wn.nd ? 1 : 0);
    if (!wn.nd) pc_ui_text(TX0, VY0, "This build carries no changelog.", SC_TEXT, C_TEXT);
    pc_ui_raw(1);
    for (int i = wn.top; i < wn.nd; i++) {
        const DLine* l = &wn.d[i];
        if (y + l->pad + l->h > VY1 + 2) break;
        y += l->pad;
        if (l->kind == D_SEP) {   /* EARLIER VERSIONS: a rule, the words on it */
            const char* s = tr("EARLIER VERSIONS");
            float w = pc_ui_text_w(s, 0.45f);
            pc_ui_rect(TX0, y + 7, 320 - w / 2 - 8, y + 8, 0x60FFFFFF, 0.3f);
            pc_ui_rect(320 + w / 2 + 8, y + 7, TX1, y + 8, 0x60FFFFFF, 0.3f);
            pc_ui_text(320 - w / 2, y, s, 0.45f, C_ROW);
        } else if (l->kind == CL_VERSION) {
            const char* s = l->n ? l->s : tr("Unreleased changes");
            float w = pc_ui_text_w(s, l->sc);
            pc_ui_text(l->x, y, s, l->sc, C_GOLD);
            if (l->bold) pc_ui_text(l->x + w + 12, y + 4, tr(l->n ? "This version" : "This build"), 0.42f, C_THIS);
            pc_ui_rect(l->x, y + 19, TX1, y + 20, 0x50D6B668, 0.3f);
        } else if (l->kind == CL_HEAD) pc_ui_text(l->x, y, l->s, l->sc, C_HEAD);
        else {
            if (l->bullet) pc_ui_rect(l->x - 10, y + 5, l->x - 6, y + 9, C_GOLD, 0.3f);
            draw_line(l, y);
        }
        y += l->h;
    }
    pc_ui_raw(0);
    if (wn.max_top) {   /* the scroll bar */
        float total = list_h(), view = VY1 - VY0, h = view * view / total, at;
        if (h < 16) h = 16;
        at = (view - h) * wn.d[wn.top].y / wn.d[wn.max_top].y;
        pc_ui_rect(BAR_X, VY0, BAR_X + 2, VY1, 0x40FFFFFF, 0.3f);
        pc_ui_rect(BAR_X - (wn.drag ? 1 : 0), VY0 + at, BAR_X + 2 + (wn.drag ? 1 : 0), VY0 + at + h, 0xC0FFFFFF, 0.31f);
    }
    {   /* the button bar */
        static const char* const words[4] = { "Back", "Scroll", "Page", "Version" };
        static const int act[4] = { UG_BACK, UG_SCROLL, UG_PAGE, UG_CAT };
        wn.nh = 0;
        for (int i = 0; i < 4; i++) {
            wn.hx[i][0] = x;
            x = ui_hint(x, Y_BAR, ui_glyph(act[i], g[i], sizeof(g[i])), words[i], 0);
            wn.hx[i][1] = x - 12;
            wn.hact[i] = act[i];
            wn.nh++;
        }
    }
    AdvEasySetTextureList(0);   /* the title's texture list again (the font set its own) */
}

/* Adv_BioCvTitle mode 30 while WHAT'S NEW is up (lobby_glue.c pc_lobby): 1 back to the title menu */
int pc_whatsnew(void)
{
    int r;
    if (!wn.open) {
        const bool* k = bind_keys();
        build();
        wn.open = 1;
        wn.top = wn.drag = 0;
        for (int i = 0; i < 80; i++) wn.keys[i] = k && k[i];   /* (a key still down from the title menu) */
        menu_mouse_take(&wn.mt);
        if (new_version()) {   /* the button's dot: seen in this version */
            snprintf(g_whatsnew_seen, sizeof(g_whatsnew_seen), "%s", update_installed());
            settings_save_later();
        }
        host_log("whatsnew: open\n");
    }
    r = input();
    draw();
    if (r) { wn.open = 0; host_log("whatsnew: closed\n"); }
    return r;
}

/* lobby_glue.c pc_lobby_close: the title left with the screen up (it opens afresh next time) */
void pc_whatsnew_close(void) { wn.open = 0; }
