/* ENHANCED SETTINGS in the game's OPTION menu (docs/ARCHITECTURE.md "Settings"): an eighth item on
 * the option screen, in DEFAULT's place, set in the screen's own lettering, opens pages of the port's settings drawn
 * like the co-op lobby (the game's font, the option screen's background and title, its cursor
 * sounds). Game thread, from adv.c (Adv_GameOptionScreen and DisplayOptionPlate*): option
 * level 4 is ours. Values come from the shared settings table (pc/src/host/settings.c) and the
 * control bindings (pc/src/input/bindings.c), the same ones the F1 overlay edits. In a game a
 * ninth item, RETURN TO TITLE in EXIT's place, asks first and then returns to the title. EXIT is at
 * the bottom right.
 */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/adv.h"
#include "../../decomp/include/ps2/veronica/prog/padman.h"
#include "../../decomp/include/ps2/veronica/prog/sdfunc.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_NaDraw.h"
#include "../../decomp/include/ps2/veronica/prog/vibman.h"
#include "../src/host/settings.h"
#include "../src/host/bindings.h"
#include "../src/host/menu_mouse.h"
#include "../src/host/tps.h"
#include "../src/host/lang.h"
#include <stdio.h>
#include <string.h>

extern ADV_WORK AdvWork;
extern QUAD Qtex[16];
extern unsigned int* pad_type[4];
extern OPTION OptionDef[];
extern int coop_on;                    /* pc/src/host/coop.h */
void host_log(const char* fmt, ...);
void pc_ui_text(float x, float y, const char* s, float sc, unsigned int argb);
float pc_ui_text_w(const char* s, float sc);
int pc_ui_text_wrap(float x, float y, float w, const char* s, float sc, unsigned int argb, int max_lines);
void pc_ui_rect(float x0, float y0, float x1, float y1, unsigned int argb, float z);
void pc_ui_arrow(float x, float y, int dir, unsigned int argb);
int bind_any_down(void);

enum { P_UP = 0x1000, P_RIGHT = 0x2000, P_DOWN = 0x4000, P_LEFT = 0x8000, P_START = 0x800, P_SELECT = 0x100, P_L1 = 0x4, P_R1 = 0x8 };   /* Pad[] bits */

/* ---------------------------------------------------------------- glue for the host */
int pc_in_game(void) { return sys && (sys->tk_flg & 0x80); }
int pc_keytype(void) { return sys ? sys->keytype & 3 : 0; }

/* the key type's DS2 buttons for virtual pad bit v (pad.c bhSetPad), in pad report bits */
int pc_keytype_mask(int v)
{
    unsigned int n;
    if (!sys || v < 0 || v > 17) return -1;
    n = pad_type[sys->keytype & 3][v];
    return (int)(((n & 0xFF) << 8) | ((n >> 8) & 0xFF));
}

/* ---------------------------------------------------------------- mouse */
static MouseTake mt;                    /* the pointer (game 2D px) and what it did */


static unsigned int ok_button(void) { return (sys->keytype & 3) < 3 ? AdvGetOkButton() : 0xC0; }
static unsigned int cancel_button(void) { return (sys->keytype & 3) < 3 ? AdvGetCancelButton() : 0x20; }

/* Adv_GameOptionScreen, every frame: the OS cursor shows while the option screen is up */
static int options_up;
void pc_options_frame(void) { g_menu_mouse.active = 1; options_up = 1; }
/* tps_frame, once per game frame: the option screen ran in this frame (taken, so nothing is left over when it ends) */
int pc_options_up(void) { int r = options_up; options_up = 0; return r; }
void pc_tex_keep_original(int texno);   /* texture_glue.c: our entries are cut from the option textures */

/* ---------------------------------------------------------------- level 0: ENHANCED SETTINGS, RETURN TO TITLE, EXIT
   The rows (OptionDef index), 32 px apart from y 160, the labels at x 32, the values right-aligned at ROW_RIGHT:
     AUDIO SETUP (0) .. VIBRATION (4)
     ENHANCED SETTINGS (7)           DEFAULT's place (DEFAULT, 5, is not offered)
     RETURN TO TITLE (8)    EXIT (6)  EXIT's place (in a game only; at the title empty); EXIT at the bottom right
   RETURN TO TITLE asks first (below). ENHANCED SETTINGS and RETURN TO TITLE are set in the row labels'
   own letters (texture 0: AUDIO SETUP at v 0, KEY CONFIG 32, SCREEN SETUP 64, DEFAULT 160, EXIT 192,
   SETUP 224, ON / OFF 320; the only H is in the narrower key config caption CHANGE AIM at 256, drawn
   wider), EXIT is its own label cut to the word.
   u = left edge, w = width (measured from the texture), dw = drawn width. */
#define ROW_RIGHT 584.0f
#define ROW_W 288.0f                  /* a label's width (the texture's rows) */
typedef struct { short u, v, w, dw; } Glyph;
#define G_A { 0, 0, 18, 18 }          /* AUDIO */
#define G_O { 71, 0, 19, 19 }
#define G_C { 70, 32, 17, 17 }        /* KEY CONFIG */
#define G_G { 158, 32, 19, 19 }
#define G_Y { 37, 32, 20, 20 }
#define G_R { 34, 64, 20, 20 }        /* SCREEN */
#define G_N { 88, 64, 19, 19 }
#define G_D { 0, 160, 18, 18 }        /* DEFAULT */
#define G_L { 89, 160, 16, 16 }
#define G_I { 38, 192, 11, 11 }       /* EXIT */
#define G_S { 0, 224, 15, 15 }        /* SETUP */
#define G_E { 16, 224, 16, 16 }
#define G_T { 33, 224, 18, 18 }
#define G_U { 52, 224, 20, 20 }
#define G_H { 302, 256, 13, 18 }      /* CHANGE */
#define G_SP { -1, 0, 12, 12 }
#define N_OF(a) (int)(sizeof(a) / sizeof(a[0]))
static const Glyph w_enhanced[] = { G_E, G_N, G_H, G_A, G_N, G_C, G_E, G_D, G_SP, G_S, G_E, G_T, G_T, G_I, G_N, G_G, G_S };
static const Glyph w_title[] = { G_R, G_E, G_T, G_U, G_R, G_N, G_SP, G_T, G_O, G_SP, G_T, G_I, G_T, G_L, G_E };
static const Glyph w_yes[] = { G_Y, G_E, G_S };
static const Glyph w_no[] = { { 65, 320, 19, 19 }, { 44, 320, 19, 19 } };   /* ON's letters, as ON / OFF */
static const Glyph w_exit[] = { { 0, 192, 68, 68 } };

/* sx: the letters' width (not the gaps) scaled */
static float word_w(const Glyph* g, int n, float sx)
{
    float w = 0;
    for (int i = 0; i < n; i++) w += g[i].dw * sx + 1;
    return w;
}

static void word(const Glyph* g, int n, float x, float y, unsigned int col, float z, float sx)
{
    QUAD* q = &Qtex[15];
    pc_tex_keep_original(0);
    for (int i = 0; i < n; i++) {
        if (g[i].u >= 0) {
            SetQuadPos(x, y, g[i].dw * sx, 32.0f, q);
            SetQuadUv2(g[i].u, g[i].v, g[i].w, 32.0f, 0, q);
            AdvEasyDrawTexture(0, col, q, z, 1);
        }
        x += g[i].dw * sx + 1;
    }
}

/* OPTION's picture in another language (lang.h): other letters on its plates, so our words are set in the font */
static int foreign(void) { return lang_picture(4) != LANG_EN; }
static unsigned int ps2_argb(unsigned int c)   /* a plate's colour (0x80 full) for the font */
{
    unsigned int r = 0;
    for (int i = 0; i < 32; i += 8) { unsigned int k = (c >> i & 0xFF) * 2; r |= (k > 255 ? 255 : k) << i; }
    return r;
}
static float font_word_sc(const char* t, float w) { float n = pc_ui_text_w(t, 1); return n > w ? w / n : 1; }
static void font_word(const char* t, float x, float y, unsigned int col, float w)
{
    pc_ui_text(x, y + 3, t, font_word_sc(t, w), ps2_argb(col));
    AdvEasySetTextureList(0);   /* the screen's textures again (the font set its own) */
}

static unsigned int pulse(void) { return ((unsigned int)AdvWork.FlushCount2 + 255) << 24 | 0xFFFFFF; }
static unsigned int row_col(int i) { return OptionDef[i].SelectFlag ? pulse() : 0x7FAFAFAF; }   /* as the other rows */
/* ENHANCED SETTINGS is 13 px wider than a label: its letters a little narrower, so it ends where the labels do */
static float enh_sx(void) { return (ROW_W - N_OF(w_enhanced)) / (word_w(w_enhanced, N_OF(w_enhanced), 1) - N_OF(w_enhanced)); }
static float exit_x(void) { return ROW_RIGHT - (foreign() ? pc_ui_text_w(tr("EXIT"), 1) : word_w(w_exit, 1, 1)); }   /* right-aligned with the values */

/* the cursor's next row from idx (adv.c DisplayOptionPlateLevel0; dir 0 up, 1 down, 2 left, 3 right; vib:
   VIBRATION can be chosen): up/down along the labels, wrapping, EXIT the last at the title; in a game left/right
   between RETURN TO TITLE and EXIT, and from EXIT up to ENHANCED SETTINGS, down to the top */
int pc_options_move(int idx, int dir, int vib)
{
    static const int col[] = { 0, 1, 2, 3, 4, 7, 8 };
    int game = pc_in_game(), n = N_OF(col), k;
    if (dir >= 2) return game && idx == (dir == 3 ? 8 : 6) ? 14 - idx : idx;
    if (game && idx == 6) return dir ? 0 : 7;
    if (idx == 6) idx = 8;   /* at the title EXIT ends the list */
    for (k = 0; k < n && col[k] != idx; k++) ;
    if (k == n) k = 5;   /* (DEFAULT: as ENHANCED SETTINGS) */
    do k = (k + (dir ? 1 : n - 1)) % n; while (col[k] == 4 && !vib);
    return col[k] == 8 && !game ? 6 : col[k];
}

/* ---------------------------------------------------------------- the question box
   RETURN TO TITLE's question (below) and the BioRand screen's questions, messages and progress
   (biorand_menu.c) share this look: a dark box in the option screen's window frame, DLG_X0..DLG_X1 and
   y0..y1; the question (l1) in white, the detail under it (l2: centred lines, at most `lines`) in col2;
   the answers on a row 48 above the bottom, b0 left of the middle and b1 right of it (b1 NULL: b0 alone,
   centred), the chosen one (sel) pulsing as the option rows, the other dim. glyphs: YES and NO in the
   option screen's own letters (its texture list current), else every answer in the font (the title).
   z: the box, its frame just in front (z + 0.01). */
#define DLG_X0 112.0f
#define DLG_X1 528.0f
#define DLG_BTN_SC 0.95f
static float dlg_btn_w(const char* b, int glyphs)
{
    if (glyphs && !foreign() && !strcmp(b, "YES")) return word_w(w_yes, 3, 1);
    if (glyphs && !foreign() && !strcmp(b, "NO")) return word_w(w_no, 2, 1);
    return pc_ui_text_w(tr(b), DLG_BTN_SC);
}
static float dlg_btn_x(int i, const char* b0, const char* b1, int glyphs)
{
    if (!b1) return 320 - dlg_btn_w(b0, glyphs) / 2;
    return i ? 320 + 56 : 320 - 56 - dlg_btn_w(b0, glyphs);
}

/* the answer at game pixel (x, y) of a box ending at y1: 0 b0, 1 b1, -1 none */
int pc_ui_dialog_hit(float y1, const char* b0, const char* b1, int glyphs, float x, float y)
{
    int hit = -1;
    if (y >= y1 - 52 && y < y1 - 12)
        for (int i = 0; i < (b1 ? 2 : 1); i++) {
            float bx = dlg_btn_x(i, b0, b1, glyphs);
            if (x >= bx - 12 && x < bx + dlg_btn_w(i ? b1 : b0, glyphs) + 12) hit = i;
        }
    return hit;
}

/* l2: the words that fit in w per line (as pc_ui_text_wrap), each line centred; the lines (draw 0: only counted) */
static int text_wrap_c(int draw, float y, float w, const char* s, float sc, unsigned int argb, int max_lines)
{
    char line[160];
    int n;
    for (n = 0; *s && n < max_lines; n++) {
        int len = 0, fit = 0;
        while (s[len] && len < (int)sizeof(line) - 1) {
            memcpy(line, s, len + 1); line[len + 1] = 0;
            if (pc_ui_text_w(line, sc) > w) break;
            len++;
            if (!s[len] || s[len] == ' ') fit = len;
        }
        if (!s[len]) fit = len;
        if (!fit) fit = len ? len : 1;
        memcpy(line, s, fit); line[fit] = 0;
        if (draw) pc_ui_text(320 - pc_ui_text_w(line, sc) / 2, y + n * 28 * sc * 1.05f, line, sc, argb);
        s += fit;
        while (*s == ' ') s++;
    }
    return n;
}

/* a box for these texts, centred on the screen: l1, l2's lines (at most `lines`), `extra` (a bar) and the answers
   (RETURN TO TITLE's proportions) */
void pc_ui_dialog_place(const char* l1, const char* l2, float sc2, int lines, float extra, float* y0, float* y1)
{
    int n = l2 ? text_wrap_c(0, 0, DLG_X1 - DLG_X0 - 40, tr(l2), sc2, 0, lines) : 0;
    float h = (l1 ? 54 : 22) + n * 28 * sc2 * 1.05f + extra + 64;
    *y0 = (float)(int)(240 - h / 2);
    *y1 = *y0 + (float)(int)(h + 0.5f);
}

static void dlg_frame(float y0, float y1, float z)
{
    NJS_POINT3 tl = { DLG_X0, y0, z + 0.01f }, br = { DLG_X1, y1, z + 0.01f };
    pc_ui_rect(DLG_X0, y0, DLG_X1, y1, 0xE0000000, z);
    AdvEasyDrawWindow(&tl, &br, 0xFFAFAFAF, 0x3F5F5F5F);
}

static void dlg_texts(float y0, const char* l1, const char* l2, float sc2, unsigned int col2, int lines)
{
    if (l1) { l1 = tr(l1); pc_ui_text(320 - pc_ui_text_w(l1, 0.7f) / 2, y0 + 22, l1, 0.7f, 0xFFFFFFFF); }
    if (l2) text_wrap_c(1, l1 ? y0 + 54 : y0 + 22, DLG_X1 - DLG_X0 - 40, tr(l2), sc2, col2, lines);
    AdvEasySetTextureList(0);   /* the screen's textures again (the font set its own) */
}

void pc_ui_dialog(float y0, float y1, float z, const char* l1, const char* l2, float sc2, unsigned int col2, int lines,
                  const char* b0, const char* b1, int sel, int glyphs)
{
    dlg_frame(y0, y1, z);
    for (int i = 0; i < (b1 ? 2 : 1); i++) {
        const char* b = i ? b1 : b0;
        float x = dlg_btn_x(i, b0, b1, glyphs);
        unsigned int col = i == sel ? pulse() : 0x7FAFAFAF;
        if (glyphs && !foreign() && !strcmp(b, "YES")) word(w_yes, 3, x, y1 - 48, col, 0.3f, 1);
        else if (glyphs && !foreign() && !strcmp(b, "NO")) word(w_no, 2, x, y1 - 48, col, 0.3f, 1);
        else pc_ui_text(x, y1 - 44, tr(b), DLG_BTN_SC, glyphs ? ps2_argb(col) : col);
    }
    dlg_texts(y0, l1, l2, sc2, col2, lines);
}

/* the same box with n answers in the font (the BIORAND screen's difficulty: NORMAL, EASY, VERY EASY, CANCEL), spread
   evenly on the answer row, smaller when they do not fit; btns_x: answer i's left edge, its width in *w */
#define DLG_GAP 36.0f
static float btns_sc(const char* const* b, int n)
{
    float w = 0, room = DLG_X1 - DLG_X0 - 40 - DLG_GAP * (n - 1);
    for (int i = 0; i < n; i++) w += pc_ui_text_w(b[i], DLG_BTN_SC);
    return w > room ? DLG_BTN_SC * room / w : DLG_BTN_SC;
}
static float btns_x(const char* const* b, int n, int i, float sc, float* w)
{
    float all = DLG_GAP * (n - 1), x;
    for (int k = 0; k < n; k++) all += pc_ui_text_w(b[k], sc);
    x = 320 - all / 2;
    for (int k = 0; k < i; k++) x += pc_ui_text_w(b[k], sc) + DLG_GAP;
    *w = pc_ui_text_w(b[i], sc);
    return x;
}

int pc_ui_dialog_btns_hit(float y1, const char* const* b, int n, float x, float y)
{
    float sc = btns_sc(b, n), w;
    if (y >= y1 - 52 && y < y1 - 12)
        for (int i = 0; i < n; i++) {
            float bx = btns_x(b, n, i, sc, &w);
            if (x >= bx - DLG_GAP / 2 && x < bx + w + DLG_GAP / 2) return i;
        }
    return -1;
}

void pc_ui_dialog_btns(float y0, float y1, float z, const char* l1, const char* l2, float sc2, unsigned int col2, int lines,
                       const char* const* b, int n, int sel)
{
    float sc = btns_sc(b, n), w;
    dlg_frame(y0, y1, z);
    for (int i = 0; i < n; i++)
        pc_ui_text(btns_x(b, n, i, sc, &w), y1 - 44 + (DLG_BTN_SC - sc) * 9.0f, b[i], sc, i == sel ? pulse() : 0x7FAFAFAF);
    dlg_texts(y0, l1, l2, sc2, col2, lines);
}

/* RETURN TO TITLE's question, over the option screen in the game's window: NO preselected; left/right
   (or hover) choose, confirm or a click answers, cancel or a right click is NO. YES leaves the option
   screen as EXIT does (system data saved if changed); system.c bhSysCallOption then returns to the
   title as the soft reset does, and co-op ends the session there (coop_glue.c, "back at the title"). */
static struct { int open, yes, go; } quit;
#define Q_Y0 180.0f
#define Q_Y1 316.0f

void pc_options_quit_open(void) { quit.open = 1; quit.yes = 0; }
int pc_options_quit_title(void) { int go = quit.go; quit.go = 0; return go; }

int pc_options_quit(int port)
{
    unsigned int press = Pad[port].press;
    int hit;
    if (!quit.open) return 0;
    menu_mouse_take(&mt);
    hit = pc_ui_dialog_hit(Q_Y1, "YES", "NO", 1, mt.x, mt.y);
    if (hit >= 0) hit = !hit;   /* (quit.yes: 1 YES, the left one) */
    if (hit >= 0 && (mt.moved || mt.left) && hit != quit.yes) { quit.yes = hit; CallSystemSe(0, 2); }
    if (press & (P_LEFT | P_RIGHT)) { quit.yes ^= 1; CallSystemSe(0, 2); }
    else if ((mt.left && hit >= 0) || (press & (ok_button() | P_START))) {
        quit.open = 0;
        CallSystemSe(0, quit.yes ? 3 : 0);
        if (quit.yes) { quit.go = 1; host_log("options: return to the title\n"); return -1; }
    } else if (mt.right || (press & (cancel_button() | P_SELECT))) { quit.open = 0; CallSystemSe(0, 0); }
    return 1;
}

static void quit_draw(void)
{
    pc_ui_dialog(Q_Y0, Q_Y1, 0.19f, "Return to the title screen?",
                 coop_on ? "Unsaved progress will be lost. The co-op session ends." : "Unsaved progress will be lost.",
                 coop_on ? 0.5f : 0.6f, 0xFFC0C0C0, 1, "YES", "NO", !quit.yes, 1);
}

/* DisplayOptionPlateLevel0, with the other rows: drawn as they are (dim, or pulsing when selected) */
void pc_options_rows0(void)
{
    if (foreign()) {
        font_word(tr("ENHANCED SETTINGS"), 32.0f, 320.0f, row_col(7), ROW_W);
        if (pc_in_game()) font_word(tr("RETURN TO TITLE"), 32.0f, 352.0f, row_col(8), ROW_W - 40);
        font_word(tr("EXIT"), exit_x(), 352.0f, row_col(6), 200);
    } else {
        word(w_enhanced, N_OF(w_enhanced), 32.0f, 320.0f, row_col(7), 0.05f, enh_sx());
        if (pc_in_game()) word(w_title, N_OF(w_title), 32.0f, 352.0f, row_col(8), 0.05f, 1);
        word(w_exit, 1, exit_x(), 352.0f, row_col(6), 0.05f, 1);
    }
    if (quit.open) quit_draw();
}

/* DisplayOptionPlateLevel0 before it reads the pad: hovering a row selects it, a click confirms
   it (on STEREO/MONO, ON/OFF: picks that value), a right click leaves the option screen. Start's
   key on the keyboard (Enter) confirms as well, as in RETURN TO TITLE's question and ENHANCED
   SETTINGS (the gamepad's Start does nothing here, as on the PS2) */
static int opt_held = 1;   /* the Modern gamepad's options button (below) was down last frame; 1 when OPTION opens */
void pc_options_mouse0(int port)
{
    ADV_WORK* ap = &AdvWork;
    int hit = -1, opt = bind_pad_raw(B_TPS_OPTIONS);
    /* third person with the Modern gamepad: its options button (Start) opened OPTION in the game, and a new press
       leaves it, as View / Back opens and leaves it with the game's buttons */
    if (opt && !opt_held && g_tps.on && g_tps.pad_modern && pc_in_game()) Pad[port].press |= P_SELECT;
    opt_held = opt;
    if ((Pad[port].press & P_START) && pad_kb_start()) Pad[port].press |= ok_button();
    menu_mouse_take(&mt);
    if (mt.x >= 32 && mt.x < 608 && mt.y >= 160 && mt.y < 384) {   /* rows 32 px apart, their values on the right */
        hit = (int)(mt.y - 160) / 32;
        if (hit == 5) hit = mt.x < 320 ? 7 : -1;   /* ENHANCED SETTINGS */
        else if (hit == 6) hit = mt.x >= exit_x() - 4 ? 6 : mt.x < 320 && pc_in_game() ? 8 : -1;   /* RETURN TO TITLE, EXIT */
        else if (mt.x >= 320 && hit != 0 && hit != 4 && hit != 1) hit = -1;
        if (hit == 4 && !CheckVibrationUnit(port * 6 + 2)) hit = -1;   /* VIBRATION without a vibration unit: not selectable */
    }
    if (hit >= 0 && (mt.moved || mt.left) && hit != ap->OptIndex) { ap->OptIndex = hit; CallSystemSe(0, 2); }
    if (mt.left && hit >= 0) {
        if ((hit == 0 || hit == 4) && mt.x >= 336) Pad[port].press |= mt.x < 472 ? P_LEFT : P_RIGHT;   /* STEREO / MONO, ON / OFF */
        else Pad[port].press |= ok_button();
    } else if (mt.right) Pad[port].press |= cancel_button();
}

/* levels 1-3 (KEY CONFIG, SCREEN SETUP, MONITOR ADJUSTMENT): a right click goes back */
void pc_options_mouse_back(int port, int level)
{
    menu_mouse_take(&mt);
    if (mt.right) Pad[port].press |= level == 2 || level == 3 ? 0x10 : P_START;
}

/* ---------------------------------------------------------------- the pages */
enum { K_PAGE, K_SET, K_DEVICE, K_BIND, K_HEAD, K_GROUP, K_RESET, K_BACK };   /* K_HEAD, K_GROUP: headings */
typedef struct Row { int kind, idx; } Row;

#define MAX_ROWS 80
#define VISIBLE 9                         /* rows shown below the page row; more scroll */
#define X_LABEL 44.0f
#define X_VALUE 360.0f
#define Y_ROW0 184.0f
#define ROW_H 21.0f
#define Y_HELP (Y_ROW0 + VISIBLE * ROW_H + 6)   /* the help lines, below a rule */
#define HELP_LINES 3
#define Y_BOTTOM (Y_HELP + HELP_LINES * 14 + 4)
#define SC_ROW 0.55f
#define Z_PANEL 0.02f
#define Z_HI 0.3f

static struct {
    int page, sel, top, dev, slot;
    int capture;                 /* waiting for a key: the bind row + 1 */
    int wait_release;            /* after a capture: no input until everything is let go */
    char msg[96];
    int msg_t;
    unsigned char pending[256];  /* actions to run when the menu closes */
    int any_pending;
} st;

static Row rows[MAX_ROWS];
static int nrows;

static const char* const heads[] = { "", "THIRD PERSON: KEYBOARD AND MOUSE", "HOTKEYS" };

/* every row of the page that is shown now (what F1 shows of a page, this shows too), headings first */
static void page_rows(void)
{
    for (int i = 0; i < g_settings_n && nrows < MAX_ROWS - 3; i++)
        if (g_settings[i].page == st.page && (!g_settings[i].shown || g_settings[i].shown())) {
            if (g_settings[i].group) rows[nrows++] = (Row){ K_GROUP, i };
            rows[nrows++] = (Row){ K_SET, i };
        }
}

static void build_rows(void)
{
    nrows = 0;
    rows[nrows++] = (Row){ K_PAGE, 0 };
    page_rows();
    if (st.page == SP_CONTROLS) {   /* then the bindings */
        int last = -1;
        rows[nrows++] = (Row){ K_DEVICE, 0 };
        for (int i = 0; i < B_COUNT && nrows < MAX_ROWS - 3; i++) {
            int r = bind_order[i], g = bind_rows[r].group;
            if (!bind_row_shown(r, st.dev)) continue;
            if (g != last) { rows[nrows++] = (Row){ K_HEAD, g }; last = g; }
            rows[nrows++] = (Row){ K_BIND, r };
        }
        rows[nrows++] = (Row){ K_RESET, 0 };
    }
    rows[nrows++] = (Row){ K_BACK, 0 };
    if (st.sel >= nrows) st.sel = nrows - 1;
}

/* what switches the page (L1 / R1) on the device in use: the gamepad's names (LB / RB on an Xbox pad), else the keys */
static const char* page_keys(char* buf, int n)
{
    char a[32], b[32];
    int dev = bind_gamepad() ? BIND_PAD : BIND_KB;
    bind_code_name(dev, bind_get(B_L1, dev, 0), a, sizeof(a));
    bind_code_name(dev, bind_get(B_R1, dev, 0), b, sizeof(b));
    snprintf(buf, n, "%s/%s", a, b);
    return buf;
}

static void say(const char* m) { snprintf(st.msg, sizeof(st.msg), "%s", m); st.msg_t = 120; }

static void move_sel(int dir)
{
    for (int k = 0; k < nrows; k++) {
        st.sel = (st.sel + dir + nrows) % nrows;
        if (rows[st.sel].kind != K_HEAD && rows[st.sel].kind != K_GROUP) break;
    }
    CallSystemSe(0, 2);
}

static void set_page(int p)
{
    st.page = (p + SP_COUNT) % SP_COUNT;
    st.sel = 0; st.top = 0;
    CallSystemSe(0, 2);
}

static const Setting* row_set(const Row* r) { return r->kind == K_SET ? &g_settings[r->idx] : NULL; }

/* left/right or a click on an arrow */
static void change(const Row* r, int dir)
{
    const Setting* s = row_set(r);
    switch (r->kind) {
    case K_PAGE: set_page(st.page + dir); return;
    case K_DEVICE: st.dev ^= 1; st.slot = 0; CallSystemSe(0, 2); return;
    case K_BIND: st.slot = dir > 0; CallSystemSe(0, 2); return;
    case K_SET:
        if (s->flags & SET_ACTION) return;
        if (settings_locked(s)) { CallSystemSe(0, 1); say(settings_locked(s)); return; }
        settings_step(s, dir);
        CallSystemSe(0, 2);
        return;
    }
}

static void capture_begin(int bind_row)
{
    st.capture = bind_row + 1;
    bind_capture_start(st.dev);
    CallSystemSe(0, 3);
}

/* 0 stay, 1 back to the option screen */
static int activate(const Row* r)
{
    const Setting* s = row_set(r);
    switch (r->kind) {
    case K_PAGE: set_page(st.page + 1); return 0;
    case K_DEVICE: change(r, 1); return 0;
    case K_BIND: capture_begin(r->idx); return 0;
    case K_RESET:
        bind_reset(st.dev);
        settings_save_later();
        say(st.dev ? "Gamepad bindings restored." : "Keyboard bindings restored.");
        CallSystemSe(0, 3);
        return 0;
    case K_BACK: CallSystemSe(0, 0); return 1;
    case K_SET:
        if (settings_locked(s)) { CallSystemSe(0, 1); say(settings_locked(s)); return 0; }
        if (s->flags & SET_ACTION) {
            if (s->flags & SET_NOW) settings_act(s);
            else { st.pending[r->idx] = 1; st.any_pending = 1; say("Applies when you leave this menu."); }
            CallSystemSe(0, 3);
            return 0;
        }
        if (settings_is_bool(s) || s->names || s->name) { settings_step(s, 1); CallSystemSe(0, 2); }
        else CallSystemSe(0, 1);   /* numbers: left/right */
        return 0;
    }
    return 0;
}

static void capture_end(int code)
{
    int row = st.capture - 1;
    char nm[64];
    st.capture = 0;
    st.wait_release = 1;
    if (code == -2) { CallSystemSe(0, 0); say("Not changed"); return; }
    bind_set(row, st.dev, st.slot, code == -3 ? 0 : code);
    settings_save_later();
    CallSystemSe(0, 3);
    if (bind_swapped()) { snprintf(nm, sizeof(nm), "Swapped with %s", bind_swapped()); say(nm); }
    else if (bind_combo(row, st.dev, st.slot)) say("Hold Aim and press this button to reload. Without Aim, it keeps its other action.");
    host_log("options: %s %s slot %d = %s\n", st.dev ? "pad" : "key", bind_rows[row].ini, st.slot + 1, bind_code_name(st.dev, code < 0 ? 0 : code, nm, sizeof(nm)));
}

/* ---------------------------------------------------------------- layout */
static float row_y(int i) { return Y_ROW0 + (i - st.top) * ROW_H; }

static int row_at(float x, float y)
{
    if (x < 30 || x > 610) return -1;
    if (y >= 150 && y < 174) return 0;   /* the page row */
    for (int i = st.top; i < nrows && i < st.top + VISIBLE; i++)
        if (i > 0 && y >= row_y(i) - 3 && y < row_y(i) + ROW_H - 3) return rows[i].kind == K_HEAD || rows[i].kind == K_GROUP ? -1 : i;
    return -1;
}

static void scroll_to_sel(void)
{
    int want = st.sel;
    if (want > 1 && (rows[want - 1].kind == K_HEAD || rows[want - 1].kind == K_GROUP)) want--;   /* with its heading */
    if (st.top < 1) st.top = 1;
    if (want > 0 && want < st.top) st.top = want;
    if (st.sel >= st.top + VISIBLE) st.top = st.sel - VISIBLE + 1;
    if (st.top > nrows - VISIBLE) st.top = nrows - VISIBLE;
    if (st.top < 1) st.top = 1;
}

/* text no wider than w (long names get narrower) */
static void text_fit(float x, float y, const char* s, float sc, float w, unsigned int col)
{
    float tw = pc_ui_text_w(s, sc), k = tw > w ? w / tw : 1.0f;
    pc_ui_text(x, y + (1.0f - k) * 9.0f, s, sc * k, col);
}

static void draw_row(int i, int sel)
{
    const Row* r = &rows[i];
    const Setting* s = row_set(r);
    float y = i ? row_y(i) : 152.0f;
    unsigned int dim = 0xFFA0A0A0, col = sel ? pulse() : dim;
    char label[128], val[128];
    const char* lock = s ? settings_locked(s) : NULL;
    if (lock) col = sel ? 0xFF909090 : 0xFF686868;
    if (sel) pc_ui_rect(36, y - 3, 604, y + ROW_H - 4, st.capture ? 0x50FFC040 : 0x40FFFFFF, Z_HI);
    switch (r->kind) {
    case K_PAGE: {
        char t[48];
        float w;
        snprintf(t, sizeof(t), "%s", settings_pages[st.page]);
        w = pc_ui_text_w(t, 0.72f);
        pc_ui_text(320 - w / 2, y - 2, t, 0.72f, sel ? pulse() : 0xFFE8E0C0);
        pc_ui_arrow(320 - w / 2 - 14, y + 9, -1, col);
        pc_ui_arrow(320 + w / 2 + 14, y + 9, 1, col);
        snprintf(t, sizeof(t), "%d/%d", st.page + 1, SP_COUNT);
        pc_ui_text(560, y + 2, t, 0.45f, 0xFF909090);
        pc_ui_text(48, y + 2, page_keys(t, sizeof(t)), 0.45f, 0xFF909090);
        return;
    }
    case K_GROUP: pc_ui_text(X_LABEL - 6, y + 2, g_settings[r->idx].group, 0.45f, 0xFFE0C080); return;
    case K_HEAD: {
        char t[64];
        if (r->idx == BG_GAME) snprintf(t, sizeof(t), "GAME (CONTROL TYPE %c)", 'A' + pc_keytype());
        else snprintf(t, sizeof(t), "%s", r->idx == BG_TPS && st.dev ? "THIRD PERSON: MODERN GAMEPAD" : heads[r->idx]);
        pc_ui_text(X_LABEL - 6, y + 2, t, 0.45f, 0xFFE0C080);
        return;
    }
    case K_DEVICE:
        pc_ui_text(X_LABEL, y, "DEVICE", SC_ROW, col);
        snprintf(val, sizeof(val), "%s", st.dev ? "Gamepad" : "Keyboard and mouse");
        break;
    case K_RESET: pc_ui_text(X_LABEL, y, "RESET TO DEFAULTS", SC_ROW, col); return;
    case K_BACK: pc_ui_text(X_LABEL, y, "BACK", SC_ROW, col); return;
    case K_BIND: {
        int conflict = bind_conflict(r->idx, st.dev);
        text_fit(X_LABEL, y, bind_row_label(r->idx, label, sizeof(label)), SC_ROW, 300, col);
        for (int k = 0; k < BIND_SLOTS; k++) {
            float x = 360 + k * 124;
            unsigned int c = conflict ? 0xFFFF6060 : k == st.slot && sel ? col : dim;
            const char* nm = st.capture == r->idx + 1 && k == st.slot ? "..." : bind_code_name(st.dev, bind_get(r->idx, st.dev, k), val, sizeof(val));
            char combo[80];
            if (!(st.capture == r->idx + 1 && k == st.slot) && bind_combo(r->idx, st.dev, k)) { snprintf(combo, sizeof(combo), "Aim + %s", nm); nm = combo; }
            if (sel && k == st.slot) pc_ui_rect(x - 4, y - 2, x + 116, y + ROW_H - 5, 0x40FFFFFF, Z_HI + 0.01f);
            text_fit(x, y, nm, SC_ROW, 110, c);
        }
        return;
    }
    case K_SET:
        snprintf(label, sizeof(label), "%s%s", s->label, (s->flags & SET_RESTART) ? " (restart needed)" : "");
        text_fit(X_LABEL, y, label, SC_ROW, 296, col);
        if (s->flags & SET_ACTION) {
            if (st.pending[r->idx]) pc_ui_text(X_VALUE, y, "(on leaving menu)", 0.5f, 0xFFE0C080);
            return;
        }
        settings_value(s, val, sizeof(val));
        break;
    }
    {   /* a value between arrows */
        float w = pc_ui_text_w(val, SC_ROW);
        if (w > 210) w = 210;
        if (!lock) { pc_ui_arrow(X_VALUE - 6, y + 9, -1, col); pc_ui_arrow(X_VALUE + 12 + w + 8, y + 9, 1, col); }
        text_fit(X_VALUE + 6, y, val, SC_ROW, 210, col);
    }
    if (sel && s && (s->flags & SET_CALIB)) {   /* the test pattern, right on the row: drawn into the picture, so calibrated with it */
        float x = 466;
        for (int k = 0; k < 2; k++, x += 5 * 12 + 4 + 6) {
            pc_ui_rect(x, y - 1, x + 5 * 12 + 4, y + ROW_H - 6, k ? 0xFFFFFFFF : 0xFF000000, Z_HI + 0.02f);
            for (int j = 0; j < CALIB_STEPS; j++)
                pc_ui_rect(x + 4 + j * 12, y + 3, x + 12 + j * 12, y + ROW_H - 10, 0xFF000000u | settings_calib[k][j] * 0x010101u, Z_HI + 0.03f);
        }
    }
}

static void draw_help(void)
{
    const Row* r = &rows[st.sel];
    const Setting* s = row_set(r);
    char t[512], nm[64];
    float y = Y_HELP;
    unsigned int col = 0xFFC0C0C0;
    t[0] = 0;
    if (st.capture) {
        snprintf(t, sizeof(t), "Press a %s for %s. Esc: cancel. Delete: clear. %d s left.", st.dev ? "gamepad button" : "key or mouse button",
                 bind_rows[st.capture - 1].name, (bind_capture_left_ms() + 999) / 1000);
        col = 0xFFFFE080;
    } else if (st.msg_t > 0) { st.msg_t--; snprintf(t, sizeof(t), "%s", st.msg); col = 0xFFFFE080; }
    else switch (r->kind) {
    case K_PAGE: snprintf(t, sizeof(t), "Left/right or %s: change page; up/down: choose a setting. Changes save automatically.",
                          page_keys(nm, sizeof(nm))); break;
    case K_DEVICE: snprintf(t, sizeof(t), "%s", st.dev ? bind_pad_desc(nm, sizeof(nm)) : "Choose bindings for game actions, third person and hotkeys. Mouse buttons can be assigned too."); break;
    case K_RESET: snprintf(t, sizeof(t), "Restore default %s bindings.", st.dev ? "gamepad" : "keyboard and mouse"); break;
    case K_BACK: snprintf(t, sizeof(t), "Return to OPTION. Cancel or right-click also goes back."); break;
    case K_BIND: {
        int c = bind_conflict(r->idx, st.dev);
        if (c) { snprintf(t, sizeof(t), "Also assigned to %s. Change either binding to avoid a conflict.", bind_rows[c - 1].name); col = 0xFFFF8080; }
        else if (r->idx == B_QUICKTURN || r->idx == B_TPS_QUICKTURN) snprintf(t, sizeof(t), "Press to turn around quickly.");
        else if (r->idx == B_RELOAD || r->idx == B_TPS_RELOAD)
            snprintf(t, sizeof(t), "Reload using ammo from your inventory. If shared with another action, hold Aim and press the button.");
        else snprintf(t, sizeof(t), "Confirm: rebind. Left/right: choose a binding (%s).", bind_code_name(st.dev, bind_get(r->idx, st.dev, st.slot), nm, sizeof(nm)));
        break;
    }
    case K_SET: {
        const char* lock = settings_locked(s);
        snprintf(t, sizeof(t), "%s%s%s%s", lock ? lock : "", lock ? " " : "", (s->flags & SET_RESTART) ? "Restart needed. " : "", s->help ? s->help : "");
        if (lock) col = 0xFFFFC060;
        if (s->flags & SET_ACTION && !lock && !(s->flags & SET_NOW)) snprintf(t + strlen(t), sizeof(t) - strlen(t), "%sApplies when you leave this menu.", s->help ? " " : "");
        break;
    }
    }
    pc_ui_text_wrap(X_LABEL - 4, y, 560, t, 0.45f, col, HELP_LINES);
}

static void draw(void)
{
    QUAD* q = &Qtex[1];
    SetQuadPos(96.0f, 96.0f, 448.0f, 48.0f, q);   /* the OPTION title, as level 0 */
    SetQuadUv2(0, 320.0f, 448.0f, 48.0f, 2, q);
    pc_tex_keep_original(1);
    AdvEasyDrawTexture(1, -1, q, 0.05f, 1);
    FlushPlate();
    pc_ui_rect(28, 146, 612, Y_BOTTOM, 0xA0000000, Z_PANEL);
    pc_ui_rect(36, 176, 604, 177, 0x60FFFFFF, Z_PANEL + 0.001f);
    pc_ui_rect(36, Y_HELP - 4, 604, Y_HELP - 3, 0x60FFFFFF, Z_PANEL + 0.001f);
    draw_row(0, st.sel == 0);
    for (int i = st.top; i < nrows && i < st.top + VISIBLE; i++) draw_row(i, i == st.sel);
    if (st.top > 1) {   /* more above / below */
        NJS_POLYGON_VTX v[3] = { { 594, 187, 0.3f, 0xFFA0A0A0 }, { 606, 187, 0.3f, 0xFFA0A0A0 }, { 600, 180, 0.3f, 0xFFA0A0A0 } };
        njDrawPolygon(v, 3, 1);
    }
    if (nrows > st.top + VISIBLE) {
        NJS_POLYGON_VTX v[3] = { { 594, Y_HELP - 14, 0.3f, 0xFFA0A0A0 }, { 606, Y_HELP - 14, 0.3f, 0xFFA0A0A0 }, { 600, Y_HELP - 7, 0.3f, 0xFFA0A0A0 } };
        njDrawPolygon(v, 3, 1);
    }
    draw_help();
    AdvEasySetTextureList(0);   /* the option screen's textures again (the font set its own) */
}

/* 0 stay, 1 back */
static int input(int port)
{
    unsigned int press = Pad[port].press, rept = Pad[port].Rept;
    unsigned int ok = ok_button() | P_START, cancel = cancel_button() | P_SELECT;
    int hit;
    menu_mouse_take(&mt);
    if (st.capture) {
        int c = bind_capture_poll();
        if (c != -1) capture_end(c);
        return 0;
    }
    if (st.wait_release) {
        /* also the pad report this frame's Rept came from: a stick let go after it was read would repeat once more */
        if (bind_any_down() || pad_report_bits() || g_menu_mouse.held) return 0;
        st.wait_release = 0;
    }
    hit = row_at(mt.x, mt.y);
    if (hit >= 0 && (mt.moved || mt.left) && hit != st.sel) { st.sel = hit; CallSystemSe(0, 2); }
    if (rows[st.sel].kind == K_BIND && hit == st.sel && (mt.moved || mt.left)) st.slot = mt.x >= 360 + 120;
    if (mt.wheel) move_sel(mt.wheel > 0 ? -1 : 1);   /* as the d-pad, like the game's menus */
    if (mt.left && hit >= 0) {
        const Row* r = &rows[hit];
        if (r->kind == K_PAGE) change(r, mt.x < 320 ? -1 : 1);
        else if ((r->kind == K_SET && !(g_settings[r->idx].flags & SET_ACTION)) || r->kind == K_DEVICE) {
            if (mt.x >= X_VALUE - 16) change(r, mt.x < X_VALUE + 4 ? -1 : 1);   /* the "<" arrow: back */
            else activate(r);
        } else return activate(r);
        return 0;
    }
    if (mt.right) { CallSystemSe(0, 0); return 1; }
    if (press & P_L1) set_page(st.page - 1);
    else if (press & P_R1) set_page(st.page + 1);
    else if (rept & P_UP) move_sel(-1);
    else if (rept & P_DOWN) move_sel(1);
    else if (rept & P_LEFT) change(&rows[st.sel], -1);
    else if (rept & P_RIGHT) change(&rows[st.sel], 1);
    else if (press & ok) return activate(&rows[st.sel]);
    else if (press & cancel) { CallSystemSe(0, 0); return 1; }
    return 0;
}

/* DisplayOptionPlate level 4: returns the next level (4 stay, 0 back) */
int pc_options_pc(int port, int flag)
{
    int back = 0;
    build_rows();
    if (flag) back = input(port);
    build_rows();
    scroll_to_sel();
    draw();
    if (back) { st.capture = 0; st.wait_release = 0; }
    return back ? 0 : 4;
}

/* Adv_GameOptionScreen ends: the actions chosen run now (the game is back in control), the
   cursor goes */
void pc_options_close(void)
{
    quit.open = 0;
    opt_held = 1;
    if (st.any_pending)
        for (int i = 0; i < g_settings_n && i < (int)sizeof(st.pending); i++)
            if (st.pending[i]) { st.pending[i] = 0; settings_act(&g_settings[i]); }
    st.any_pending = 0;
    st.capture = 0;
    g_menu_mouse.active = 0;
}
