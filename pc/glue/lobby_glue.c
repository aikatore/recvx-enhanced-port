/* Co-op lobby (docs/COOP.md "Lobby"): the title menu's MULTIPLAYER entry and its screen.
 *
 * Game thread, called by the title task (adv.c Adv_BioCvTitle; mode 30 is the lobby). All of
 * it is drawn with the game's own means: the title picture and plates (the MULTIPLAYER row is
 * set from letters of the plate texture), the game's font (sys->ef_tlist, ASCII 32..126 as
 * codes 0..94), and the player and actor models, loaded with the stock loaders into buffers of
 * our own and posed, lit and drawn in a small scene of their own in front of the picture.
 * Networking stays in pc/glue/coop_glue.c: the lobby only sets g_coop and requests.
 * The title menu's EXTRA submenu (BIORAND and IMPORT with --dev, BATTLE GAME) is here too; mode 30 is then the
 * BIORAND screen (pc/glue/biorand_menu.c) or the IMPORT screen (pc/glue/import_menu.c) instead of the lobby.
 */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/adv.h"
#include "../../decomp/include/ps2/veronica/prog/adxwrap.h"
#include "../../decomp/include/ps2/veronica/prog/padman.h"
#include "../../decomp/include/ps2/veronica/prog/message.h"
#include "../../decomp/include/ps2/veronica/prog/dread.h"
#include "../../decomp/include/ps2/veronica/prog/Motion.h"
#include "../../decomp/include/ps2/veronica/prog/MdlPut.h"
#include "../../decomp/include/ps2/veronica/prog/objitm.h"
#include "../../decomp/include/ps2/veronica/prog/sdfunc.h"
#include "../../decomp/include/ps2/veronica/prog/effect.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_texture.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_NaTextureFunction.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_NaMatrix.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_NaView.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_NaDraw.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_NinjaCnk.h"
#include "../src/host/coop.h"
#include "../src/host/actors.h"
#include "../src/host/menu_mouse.h"
#include "../src/host/lobby.h"
#include "../src/host/settings.h"
#include "../src/host/enemies.h"
#include "../src/host/cast.h"
#include "../src/host/biorand.h"
#include "../src/net/net.h"
#include "../src/net/rdv_proto.h"
#include "../src/net/coop_proto.h"   /* REC_*: what the host keeps for us */
#include "../src/net/update.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void host_log(const char* fmt, ...);
void bhDispFontEx(NJS_POINT2* pos, int code, unsigned int argb, float pri);
void coop_own_from_save(const void* save);
void bhReturnTitle(void);
int  pc_biorand_menu(void);             /* biorand_menu.c: the BIORAND screen, once per frame; 1 back to the title menu */
void pc_biorand_menu_close(void);
int  pc_import_menu(void);              /* import_menu.c: the IMPORT screen, once per frame; 1 back to the title menu */
void pc_import_menu_close(void);
/* cast_list.c: the cutscene cast's "Custom" list, at its place (as our model page); 1 back */
void cast_list_open(float x0, float x1, float y0, float h, float sc, int rows);
int  cast_list_input(const MouseTake* m, unsigned int press, unsigned int rept, unsigned int ok, unsigned int cancel);
void cast_list_draw(void);
const char* cast_list_help(void);
const char* cast_list_summary(const unsigned char* off, int imports, char* buf, int n);
extern ADV_WORK AdvWork;
extern float FontScaleY;
extern const char PlyFlip[23];

enum { BTN_START = 0x800, BTN_UP = 0x1000, BTN_RIGHT = 0x2000, BTN_DOWN = 0x4000, BTN_LEFT = 0x8000 };   /* Pad[] bits */
#define TITLE_MP 4                      /* ModeCommandId of the MULTIPLAYER row */

/* ---------------------------------------------------------------- text */
#define PRI_TEXT -1.0f
/* 2D depth (njDrawPolygon / njDrawQuadTexture z: the GS depth is -1/z, as a 3D point at view
   distance 1/z): the title picture is at 0.01 (100 units), the models stand 34 units away */
#define Z_DIM 0.012f                    /* behind the models */
#define Z_FRONT 0.3f                    /* in front of them */

static int code_of(char c) { return c > 32 && c < 127 ? c - 32 : 0; }
static float adv(int c) { return c ? FontSz[c] : 11.0f; }   /* the font's space is a full cell */

#define TRACK 0.88f                     /* a little tighter than the game's messages */

static float text_w(const char* s, float sc)
{
    float w = 0;
    for (; *s; s++) w += adv(code_of(*s)) * sc * TRACK;
    return w;
}

/* the game's font, left-aligned at x (bhDispMessage's advance), with a drop shadow */
static int font_ok;                     /* the game's font is loaded (font_ready) */
static int font_state;                  /* lobby_glue loads it: 1 reading, 2 loaded by us */

static void text(float x, float y, const char* s, float sc, unsigned int argb)
{
    float kx = FontScaleX, ky = FontScaleY, kc = FontScaleCR, x0 = x;
    const char* p;
    int pass;
    if (!font_ok) return;
    bhFontScaleSet(sc, sc, sc);
    for (pass = 0; pass < 2; pass++)
        for (p = s, x = x0; *p; p++) {
            int c = code_of(*p);
            NJS_POINT2 pos = { x + (pass ? 0 : 2), y + (pass ? 0 : 2) };
            if (c) bhDispFontEx(&pos, c, pass ? argb : (argb & 0xFF000000) >> 1 & 0xFF000000, PRI_TEXT);
            x += adv(c) * sc * TRACK;
        }
    bhFontScaleSet(kx, ky, kc);
}

static void text_c(float cx, float y, const char* s, float sc, unsigned int argb) { text(cx - text_w(s, sc) / 2, y, s, sc, argb); }

/* at most `w` wide: the words that fit, then the next line (returns the lines drawn) */
static int text_wrap(float x, float y, float w, const char* s, float sc, unsigned int argb, int max_lines)
{
    char line[160];
    int n = 0;
    while (*s && n < max_lines) {
        int len = 0, fit = 0;
        while (s[len] && len < (int)sizeof(line) - 1) {
            memcpy(line, s, len + 1); line[len + 1] = 0;
            if (text_w(line, sc) > w) break;
            len++;
            if (!s[len] || s[len] == ' ') fit = len;
        }
        if (!s[len]) fit = len;
        if (!fit) fit = len ? len : 1;
        memcpy(line, s, fit); line[fit] = 0;
        text(x, y + n * 28 * sc * 1.05f, line, sc, argb);
        n++;
        s += fit;
        while (*s == ' ') s++;
    }
    return n;
}

static void rect(float x0, float y0, float x1, float y1, unsigned int argb, float z)
{
    NJS_POLYGON_VTX v[4] = { { x0, y0, z, argb }, { x0, y1, z, argb }, { x1, y0, z, argb }, { x1, y1, z, argb } };
    njDrawPolygon(v, 4, 1);
}

/* a small arrow head pointing left (dir -1) or right (1), its tip at x */
static void arrow(float x, float y, int dir, unsigned int argb)
{
    NJS_POLYGON_VTX v[3] = { { x, y, 0.3f, argb }, { x - dir * 7.0f, y - 6.0f, 0.3f, argb }, { x - dir * 7.0f, y + 6.0f, 0.3f, argb } };
    njDrawPolygon(v, 3, 1);
}

/* ---------------------------------------------------------------- title menu */
/* The MULTIPLAYER row in the plates' own lettering: each letter cut from a plate of the title
   texture (texture 1: LOAD GAME at v 64, OPTION 128, SOUND MUSEUM 192, EASY 288, NORMAL 320;
   u = the letter's left edge, w its width, measured from the texture's alpha with a pixel each side). */
typedef struct { short u, v, w; } Letter;
static const Letter mp_letters[] = {
    { 312, 64, 25 },  /* M of GAME */
    { 174, 192, 23 }, /* U of SOUND */
    { 160, 64, 20 },  /* L of LOAD */
    { 246, 128, 21 }, /* T of OPTION */
    { 268, 128, 12 }, /* I */
    { 224, 128, 21 }, /* P */
    { 160, 64, 20 },  /* L */
    { 286, 64, 24 },  /* A of GAME */
    { 107, 288, 23 }, /* Y of EASY */
    { 339, 64, 20 },  /* E of GAME */
    { 67, 320, 23 },  /* R of NORMAL */
};
/* BIORAND (--dev, biorand_shown) the same way: B of BATTLE (v 224), I O N of OPTION, R of NORMAL, A of GAME, D of LOAD */
static const Letter br_letters[] = {
    { 146, 224, 20 }, { 268, 128, 12 }, { 201, 128, 22 }, { 67, 320, 23 }, { 286, 64, 24 }, { 304, 128, 22 }, { 226, 64, 23 },
};
/* EXTRA: E X T R A of EXTREAM (v 384, the PAL/JP battle game's plates, unused in the US menu) */
static const Letter ex_letters[] = { { 46, 384, 18 }, { 65, 384, 21 }, { 86, 384, 19 }, { 105, 384, 22 }, { 147, 384, 22 } };
/* IMPORT (--dev): I of OPTION, M of GAME, P O of OPTION, R of NORMAL, T of OPTION */
static const Letter im_letters[] = {
    { 268, 128, 12 }, { 312, 64, 25 }, { 224, 128, 21 }, { 201, 128, 22 }, { 67, 320, 23 }, { 246, 128, 21 },
};
enum { W_MP, W_BR, W_EX, W_IM };
static const struct { const Letter* l; int n; } plate_words[] = {
    { mp_letters, 11 }, { br_letters, 7 }, { ex_letters, 5 }, { im_letters, 6 },
};

static void plate_word(int word, float cx, float y, unsigned int col, unsigned int shadow, float scale)
{
    const Letter* l = plate_words[word].l;
    int i, n = plate_words[word].n;
    float w = 0, x;
    QUAD q;
    for (i = 0; i < n; i++) w += (l[i].w + 1) * scale;
    x = cx - w / 2;
    for (i = 0; i < n; i++) {
        SetQuadPos(x, y, l[i].w * scale, 33.0f * scale, &q);
        SetQuadUv2(l[i].u, l[i].v, l[i].w, 33.0f, 1, &q);
        AdvEasyDrawTextureS(1, col, &q, 0.5f, 1, shadow);
        x += (l[i].w + 1) * scale;
    }
}
static void mp_word(float cx, float y, unsigned int col, unsigned int shadow, float scale) { plate_word(W_MP, cx, y, col, shadow, scale); }
void pc_title_word_br(float cx, float y, unsigned int col, unsigned int shadow) { plate_word(W_BR, cx, y, col, shadow, 1.0f); }   /* biorand_menu.c */
void pc_title_word_import(float cx, float y, unsigned int col, unsigned int shadow) { plate_word(W_IM, cx, y, col, shadow, 1.0f); }   /* import_menu.c */

/* the rows between the logo (to y 235) and the copyright (from y 396): 4 rows as the game's
   own 4-row layout but 16 higher, 5 rows closer together */
static float title_row_y(int i, int n) { return n >= 5 ? 234.0f + 30.0f * i : 256.0f + 32.0f * i; }
static float title_row_h(int n) { return n >= 5 ? 30.0f : 32.0f; }

/* The title menu: LOAD GAME, NEW GAME, OPTION, EXTRA, MULTIPLAYER. EXTRA (ModeCommandId TITLE_EXTRA, which CheckButton
   does nothing for) opens a submenu in level 0 itself, laid out as the difficulty screen: the word EXTRA where MODE
   SELECT is, its rows below: BIORAND (--dev), BATTLE GAME (unlocked, or the setting), IMPORT (--dev). Without any
   EXTRA is not there (the menu as the game's, with MULTIPLAYER). pc_title_mouse runs the submenu before CheckButton
   reads the pad and takes the keys it used (its own cursor: CheckButton moves Cursor[0] off row 0 while there is no
   save); the switch fades the menu out and in again (mode 10, then 8: ex.pending applied on mode 8's first frame). */
#define TITLE_EXTRA 5
enum { XR_BIORAND, XR_BATTLE, XR_IMPORT };
static struct {
    int on, pending, cur, n, main_row;  /* the submenu is shown; becomes shown (1) / hidden (0) at mode 8, -1 none; its
                                           cursor and rows; EXTRA's row in the menu (-1 none) */
    int row[3];
} ex = { .pending = -1, .main_row = -1 };
static float ex_row_y(int i) { return 304.0f + 32.0f * i; }   /* DisplayNewGamePlate's rows; EXTRA at 272 as MODE SELECT */

static int br_screen;                   /* mode 30 is the BioRand screen (biorand_menu.c), not the lobby */
static int im_screen;                   /* ... the IMPORT screen (import_menu.c) */

void pc_title_menu_init(void)
{
    ADV_WORK* ap = &AdvWork;
    int n = 0, i;
    if (pc_battle_unlocked()) ap->ExtraFlag = 1;   /* setting: BATTLE GAME as after a cleared game; the system data's clear flag (ssd_flg 1) stays as it is (its characters: event.c bhCk) */
    biorand_scan();   /* BioRand's pack (the BIORAND screen, the lobby) */
    biorand_game(0);   /* NEW GAME and BATTLE GAME are plain games; START (BIORAND), a save or the lobby's rule turn it on */
    ex.n = 0;
    if (biorand_shown()) ex.row[ex.n++] = XR_BIORAND;
    if (ap->ExtraFlag) ex.row[ex.n++] = XR_BATTLE;
    if (biorand_shown()) ex.row[ex.n++] = XR_IMPORT;   /* (--dev, as BIORAND: held back) */
    ap->ModeCommandId[n++] = 0;
    ap->ModeCommandId[n++] = 1;
    ap->ModeCommandId[n++] = 2;
    ex.main_row = -1;
    if (ex.n) { ex.main_row = n; ap->ModeCommandId[n++] = TITLE_EXTRA; }
    ap->ModeCommandId[n++] = TITLE_MP;
    for (i = 0; i < n; i++) ap->CursorFlag[0][i] = 1;
    ap->CursorMax[0] = n;
    ex.on = 0;
    ex.pending = -1;
    ex.cur = 0;
    if (ap->FromTitle == 2) ap->Cursor[0] = 2;   /* back from OPTION */
    if (ap->FromTitle == 3 && ex.main_row >= 0) {   /* back from BATTLE GAME: into EXTRA, on its row */
        ex.on = 1;
        ap->Cursor[0] = ex.main_row;
        for (i = 0; i < ex.n; i++) if (ex.row[i] == XR_BATTLE) ex.cur = i;
    }
}

void pc_tex_keep_original(int texno);

/* DisplayNewGamePlate's hook: the game draws the difficulty screen itself (0) */
int pc_newgame_plates(int cursor, float fade, int flag) { (void)cursor; (void)fade; (void)flag; return 0; }

/* DisplayGameModePlate with our rows (the game's 3 or 4 rows moved up to make room), or the EXTRA submenu */
void pc_title_plates(int cursor, int n, float fade, int flag)
{
    static const float v[4] = { 64.0f, 96.0f, 128.0f, 224.0f };   /* ModeCommandId 0..3: LOAD GAME, NEW GAME, OPTION, BATTLE GAME */
    ADV_WORK* ap = &AdvWork;
    int i;
    pc_tex_keep_original(1);   /* our words are cut from it: no HD replacement (texture_glue.c) */
    if (ex.on) {
        plate_word(W_EX, 320.0f, 272.0f, ((unsigned int)fade << 24) | 0xFFFFFF, fade, 1.0f);   /* as MODE SELECT */
        n = ex.n;
    }
    for (i = 0; i < n; i++) {
        unsigned int alpha = fade, col = 0x7F7F7F;
        int id = ex.on ? -1 - ex.row[i] : ap->ModeCommandId[i];
        float y = ex.on ? ex_row_y(i) : title_row_y(i, n);
        if (i == (ex.on ? ex.cur : cursor)) { alpha += (unsigned int)ap->FlushCount2; col = ap->FontBaseColor; }
        alpha <<= 24;
        if (flag == 0) alpha = 0xFF000000;
        if (id == TITLE_MP) mp_word(320.0f, y, alpha | col, fade, 1.0f);
        else if (id == TITLE_EXTRA) plate_word(W_EX, 320.0f, y, alpha | col, fade, 1.0f);
        else if (id == -1 - XR_BIORAND) plate_word(W_BR, 320.0f, y, alpha | col, fade, 1.0f);
        else if (id == -1 - XR_IMPORT) plate_word(W_IM, 320.0f, y, alpha | col, fade, 1.0f);
        else {
            QUAD q;
            SetQuadPos(64.0f, y, 513.0f, 33.0f, &q);
            SetQuadUv2(0, v[id == -1 - XR_BATTLE ? 3 : id & 3], 513.0f, 33.0f, 1, &q);
            AdvEasyDrawTextureS(1, alpha | col, &q, 0.5f, 1, fade);
        }
    }
    FlushPlate();
    DisplayTitleBg();
}

/* ---------------------------------------------------------------- mouse */
static MouseTake mt;                    /* the pointer (game 2D px) and what it did */

static void title_version(void);

/* Adv_BioCvTitle, every frame before its mode's work */
void pc_title_mouse_active(int on)
{
    if (ex.pending >= 0 && AdvWork.Mode == 8) { ex.on = ex.pending; ex.pending = -1; }   /* EXTRA in or out, as the menu fades in */
    g_menu_mouse.active = on;
    title_version();
}

void menu_mouse_take(MouseTake* t)
{
    int x = g_menu_mouse.x, y = g_menu_mouse.y;
    t->left = __atomic_exchange_n(&g_menu_mouse.left, 0, __ATOMIC_RELAXED);
    t->right = __atomic_exchange_n(&g_menu_mouse.right, 0, __ATOMIC_RELAXED);
    t->wheel = __atomic_exchange_n(&g_menu_mouse.wheel, 0, __ATOMIC_RELAXED);
    __atomic_exchange_n(&g_menu_mouse.left_up, 0, __ATOMIC_RELAXED);
    t->moved = (x != t->px || y != t->py) && t->seen;
    t->px = x; t->py = y; t->seen = 1;
    t->x = x / 16.0f; t->y = y / 16.0f;
    if (!menu_mouse_on()) t->left = t->right = t->wheel = t->moved = 0;
}

/* the menu fades out, then in again with (1) or without (0) the EXTRA submenu */
static void ex_switch(int on)
{
    ADV_WORK* ap = &AdvWork;
    ex.pending = on;
    if (!on) ap->Cursor[0] = ex.main_row;
    ap->NextMode = 8;
    ap->Mode = 10;
}

/* mode 30 instead of the lobby: a screen of ours (br_screen, im_screen), as CheckButton opens MULTIPLAYER's */
static void to_screen(int* screen)
{
    ADV_WORK* ap = &AdvWork;
    ap->LastLevel = 0;
    ap->NextMode = 30;
    ap->Mode = 10;
    *screen = 1;
}

/* the EXTRA submenu (level 0 while ex.on): up/down (wrapping, as the difficulty screen), hover, confirm or a click, cancel
   or a right click back to the menu; the keys it reads are taken from the pad so CheckButton does nothing with them */
static void extra_input(void)
{
    ADV_WORK* ap = &AdvWork;
    unsigned int ok = BTN_START | AdvGetOkButton(), cancel = AdvGetCancelButton();
    unsigned int press = Pad[ap->PortId].press, rept = Pad[ap->PortId].Rept;
    int hit = -1, i;
    for (i = 0; i < ex.n; i++)
        if (mt.x >= 140 && mt.x < 500 && mt.y >= ex_row_y(i) && mt.y < ex_row_y(i) + 32) hit = i;
    Pad[ap->PortId].press &= ~(ok | cancel | BTN_UP | BTN_DOWN);
    Pad[ap->PortId].Rept &= ~(BTN_UP | BTN_DOWN);
    if (hit >= 0 && (mt.moved || mt.left) && hit != ex.cur) { ex.cur = hit; CallSystemSe(0, 2); }
    if ((mt.left && hit >= 0) || (press & ok)) {
        switch (ex.row[ex.cur]) {
        case XR_BIORAND: to_screen(&br_screen); CallSystemSe(0, 3); break;
        case XR_IMPORT: to_screen(&im_screen); CallSystemSe(0, 3); break;
        default:   /* BATTLE GAME, as CheckButton's ModeCommandId 3 */
            ap->AppMode = 1;
            ap->FromTitle = 3;
            ap->LastLevel = 0;
            sys->gm_mode = 3;
            TitleCall(ap->PortId, 6);
            break;
        }
    } else if (mt.right || (press & cancel)) { ex_switch(0); CallSystemSe(0, 0); }
    else if ((rept & (BTN_UP | BTN_DOWN)) && ex.n > 1) {
        ex.cur = (ex.cur + ((rept & BTN_UP) ? ex.n - 1 : 1)) % ex.n;
        CallSystemSe(0, 2);
    }
}

/* CheckButton (before it reads the pad): hovering a row moves the cursor to it, a click on it
   confirms (as Start), a right click cancels. Level 0 the game modes (or EXTRA's), 1 the difficulty,
   2 the extra games (not reached in the US menu). */
void pc_title_mouse(int level, int flag)
{
    ADV_WORK* ap = &AdvWork;
    int hit = -1, i, n = ap->CursorMax[level];
    menu_mouse_take(&mt);
    if (level == 0 && coop_biorand_offer(NULL)) {   /* a co-op guest (F3, --coop-join): the host's BioRand seed, offered on the
                                                       BIORAND screen as from its row (the lobby opens it itself: pc_lobby) */
        to_screen(&br_screen);
        return;
    }
    if (level == 0 && ex.on) { extra_input(); return; }
    for (i = 0; i < n; i++) {
        float x0, y0, w;
        if (level == 0) { x0 = 140; w = 360; y0 = title_row_y(i, n); }
        else if (level == 1) { x0 = 224; w = 193; y0 = 304.0f + 32.0f * i; }   /* DisplayNewGamePlate, DisplayExtraGamePlate */
        else { x0 = 64.0f + 256.0f * i; w = 256; y0 = 352; }
        if (mt.x >= x0 && mt.x < x0 + w && mt.y >= y0 && mt.y < y0 + (level == 0 ? title_row_h(n) : 32)) hit = i;
    }
    if (hit >= 0 && (!ap->CursorFlag[level][hit] || (level == 0 && hit == 0 && !flag))) hit = -1;   /* not selectable now */
    if (hit >= 0 && (mt.moved || mt.left) && hit != ap->Cursor[level]) {
        ap->Cursor[level] = hit;
        CallSystemSe(0, 2);
    }
    if (mt.left && hit >= 0) Pad[ap->PortId].press |= BTN_START;
    else if (mt.right) Pad[ap->PortId].press |= AdvGetCancelButton();
    if (level == 0 && ap->ModeCommandId[(int)ap->Cursor[0]] == TITLE_EXTRA && (Pad[ap->PortId].press & (BTN_START | AdvGetOkButton()))) {
        Pad[ap->PortId].press &= ~(BTN_START | AdvGetOkButton());   /* EXTRA: its submenu */
        ex.cur = 0;
        ex_switch(1);
        CallSystemSe(0, 3);
    }
}

/* ---------------------------------------------------------------- previews */
/* Two models: ours (slot 0) and the partner's (slot 1), each a player part file loaded with
   bhReadPlayerData into buffers of its own (as coop_glue.c does for the partner in game) and,
   for an enemy/NPC model, the display actor of the same slot (actor_glue.c) posed from it. The
   actor slots are free at the title (co-op uses them only in game). The bodies have no hands: in
   game they are the weapon's pieces linked to hand nodes 9 and 13 (sys->obwp[0..1]), unarmed those
   of weapon file 0 (bhReadWeaponData). Here the pieces of the character the model is (appear_voice:
   the cast their own, other men Chris's, women Claire's), on a display actor where its hands are
   (actor_pose moves the hand nodes there) and it has none of its own (the story cast). */
typedef struct Preview {
    BH_PWORK w;
    O_WRK obj[3];                       /* [0], [1]: right/left hand, [2]: Claire's hair / Wesker's piece, as sys->obwp[0..2] */
    int chr, file, hands;               /* "You appear as" shown; part file - 10 loaded; whose hands (-1 none) */
    int tex_gone;
    int fail_chr, fails;                /* a choice whose part file could not be read: tried again after a pause */
    unsigned int fail_at;
    unsigned char exw[128], hdw[128];
} Preview;
static Preview pv[2] = { { .chr = -1, .file = -1, .hands = -1, .fail_chr = -1 }, { .chr = -1, .file = -1, .hands = -1, .fail_chr = -1 } };
static struct {
    unsigned char mdl[131072], bmt[393216], mth[12288], zmt[8192], etc[32768];
    unsigned char wr[32768], wl[32768], wmt[65536];   /* the hands (sys->wrmdlp, wlmdlp, plwmtp) */
    unsigned char hair0[0x4000], hair3[0x1000];
} pvb[2] __attribute__((aligned(64)));
static unsigned char pv_file[1 << 21] __attribute__((aligned(64)));

static void pv_hands_drop(Preview* p)
{
    int i;
    for (i = 0; i < 2; i++) {
        ML_WORK* m = p->obj[i].mlwP;
        if (!p->tex_gone && m && m->texP && (m->flg & 0x200)) njReleaseTexture(m->texP);
        memset(&p->obj[i], 0, sizeof(p->obj[i]));
    }
    p->hands = -1;
}

static void pv_release(Preview* p, int k)
{
    int i;
    if (!p->tex_gone)
        for (i = 0; i < p->w.mdl_n; i++)
            if (p->w.mdl[i].texP) njReleaseTexture(p->w.mdl[i].texP);
    pv_hands_drop(p);
    if (!p->tex_gone && p->w.mdl_n) bhGarbageTexture(NULL, 0);
    actor_set(k, -1);
    memset(&p->w, 0, sizeof(p->w));
    memset(p->obj, 0, sizeof(p->obj));
    p->chr = p->file = -1;
    p->tex_gone = 0;
}

static void pv_failed(Preview* p, int chr)
{
    p->fails = chr == p->fail_chr ? p->fails + 1 : 1;
    p->fail_chr = chr;
    p->fail_at = net_ms();
}

/* the hand pieces of player character c (weapon file 0's, as the game loads them unarmed) on slot k's model */
static void pv_hands(Preview* p, int k, int c)
{
    int i;
    if (c == p->hands) return;
    pv_hands_drop(p);
    bhGarbageTexture(NULL, 0);
    for (i = 0; i < 2; i++) p->obj[i].mlwP = p->obj[i].mdl;   /* (the loader releases this one's textures: none) */
    if (!afs_read(sys->sys_partid, c * 30 + 20, pv_file, sizeof(pv_file))) { host_log("lobby: cannot read weapon file %d\n", c * 30 + 20); return; }
    player_weapon_read(&p->w, p->obj, 0, pv_file, pvb[k].wr, pvb[k].wl, pvb[k].wmt);   /* (actor_glue.c) */
    p->hands = c;
}

/* "You appear as" chr (g_coop.chr) for preview slot k; `played`: the character "as played" means */
static void pv_set(int k, int chr, int played)
{
    Preview* p = &pv[k];
    int file = chr == 0 ? played : chr <= 4 ? chr - 1 : chr == 5 ? 4 : played & 3, i, ok = 1;
    if (chr == p->chr) return;
    if (chr < 0) { pv_release(p, k); return; }
    if (chr == p->fail_chr && net_ms() - p->fail_at < (1000u << (p->fails < 5 ? p->fails : 5))) return;   /* 2, 4 .. 32 s */
    if (file != p->file) {
        pv_release(p, k);
        if (!afs_read(sys->sys_partid, file + 10, pv_file, sizeof(pv_file))) { host_log("lobby: cannot read part file %d\n", file + 10); pv_failed(p, chr); return; }
        memset(&pvb[k], 0, sizeof(pvb[k]) - sizeof(pvb[k].hair0) - sizeof(pvb[k].hair3));
        player_model_read(&p->w, pv_file, pvb[k].mdl, pvb[k].bmt, pvb[k].mth, pvb[k].zmt);   /* (actor_glue.c) */
        for (i = 0; i < p->w.mdl_n; i++) ok &= p->w.mdl[i].owP != NULL;
        if (!p->w.mdl_n || !ok) { host_log("lobby: part file %d did not load\n", file + 10); pv_release(p, k); pv_failed(p, chr); return; }
        p->w.flg = 0x119;
        p->w.mdflg = 0x20;
        p->w.exp0 = p->exw; p->w.exp1 = p->hdw; p->w.exp3 = pvb[k].etc;
        p->w.mtx = (float(*)[16])p->w.mtxbuf;
        p->w.sx = p->w.sy = p->w.sz = p->w.sxb = p->w.syb = p->w.szb = 1.0f;
        p->w.mtn_tp = (unsigned char*)PlyFlip;
        p->w.mlwP->owP[7].flg |= 0x8;   /* as bhSetPlayer */
        p->w.mlwP->owP[11].flg |= 0x8;
        p->w.mtn_no = p->w.mnwP && p->w.mnwP[42].datP ? 42 : 0;   /* standing (PlMtnAct, as bhSetPlayer) */
        p->file = file;
    }
    p->chr = chr;
    p->fail_chr = -1;
    p->fails = 0;
    if (actor_set(k, chr >= COOP_CHR_ACTOR ? chr - COOP_CHR_ACTOR : -1)) p->obj[2].flg = 0;   /* no hair on another model */
    else player_hair(p->obj, &p->w, file & 3, pvb[k].hair0, pvb[k].hair3);   /* Claire's hair simulates in work of its own */
    pv_hands(p, k, appear_voice(chr) >= 0 ? appear_voice(chr) : file & 3);
    host_log("lobby: preview %d shows %s\n", k, chr >= COOP_CHR_ACTOR ? appear_name(chr) : "a player model");
}

static void pv_pose(int k, float x, int ay)
{
    Preview* p = &pv[k];
    O_WRK* op = &p->obj[2];
    int i;
    if (p->file < 0) return;
    p->w.px = x; p->w.py = 0; p->w.pz = 0;
    p->w.ax = p->w.az = 0; p->w.ay = ay;
    if (p->w.mnwP && p->w.mnwP[p->w.mtn_no].datP) bhSetMotion(&p->w, 65536, 0, NULL);
    bhCalcModel(&p->w);
    if (op->flg & 0x1) {
        njCalcPoint(&p->w.mlwP->owP[op->lkono].mtx, (NJS_POINT3*)&op->lox, (NJS_POINT3*)&op->px);
        if (op->id == 1211) bhObjClpn(op);
        bhCalcModel((BH_PWORK*)op);
    }
    if (actor_shown(k)) actor_pose(k, &p->w);   /* (moves hand nodes 9 and 13 to the model's hands) */
    for (i = 0; i < 2; i++)
        if ((p->obj[i].flg & 0x1) && p->obj[i].mlwP && p->obj[i].mlwP->objP) bhCalcModel((BH_PWORK*)&p->obj[i]);
}

static void pv_draw(int k)
{
    Preview* p = &pv[k];
    O_WRK* op;
    if (p->file < 0) return;
    if (actor_shown(k)) actor_draw(k);
    else bhPutModel(&p->w);
    for (op = p->obj; op < p->obj + 3; op++)   /* hands (on a display actor where it has none), hair (not there) */
        if ((op->flg & 0x1) && op->mlwP && op->mlwP->objP && (!actor_shown(k) || actor_hand_free(k, op->lkono))) bhPutModel((BH_PWORK*)op);
}

/* the model shown in slot k (the display actor, or the player model) */
static BH_PWORK* pv_shown(int k) { return actor_shown(k) && actor_work(k) ? actor_work(k) : &pv[k].w; }

/* the top of the posed model (world y), 0 when nothing is shown */
static float pv_top(int k)
{
    BH_PWORK* w = pv_shown(k);
    ML_WORK* m = w->mlwP;   /* the model bhPutModel draws */
    float top = 0;
    int i;
    if (pv[k].file < 0 || !m || !m->owP || !m->objP) return 0;
    for (i = 0; i < (int)m->obj_num; i++) if (m->owP[i].mtx[13] > top) top = m->owP[i].mtx[13];
    return top;
}

/* above the model's top: where its name goes (game 2D px) */
static NJS_POINT2 pv_head(int k)
{
    NJS_POINT3 h = { pv[k].w.px, pv_top(k) + 2.0f, pv[k].w.pz };
    NJS_POINT2 s;
    njProjectScreen((NJS_MATRIX*)cam.mtx, &h, &s);
    s.x -= 1728.0f; s.y -= 1808.0f;
    return s;
}

/* the scene: the camera looks along -z at models standing at the origin's height, lit as in a
   studio (studio_light); cam.mtx is the view while it is drawn */
/* the camera stands back for a tall model: the taller one of both spans y 110..370 */
static float cam_z = 34.0f;
#define CAM_Y (cam_z * 0.26f)
#define CAM_Z cam_z
static NJS_MATRIX view;
static float (*cam_keep)[16];           /* the game's view (none before a game ran) */

static float screen_to_world_x(float sx) { return (sx - 320.0f) * CAM_Z / 500.0f / 1.174f; }

/* a studio light: an ambient, a warm key from the upper left front, a cool fill from the right and a
   rim from behind above, as point lights far off (no falloff). The light count is set: the boot
   value (6) is more than the chunk lighting masks (ps2_NinjaCnk.c njCnkCvVnPs2: mask[0..4]), which
   left the models lit by the ambient alone (the game sets its count in each room: light.c). No fog:
   the title has none (adv.c) */
static void studio_light(void)
{
    static const float pos[3][3] = { { -200, 250, 250 }, { 250, 60, 200 }, { 60, 250, -300 } };
    static const float c[3][3] = { { 0.85f, 0.82f, 0.76f }, { 0.28f, 0.31f, 0.38f }, { 0.35f, 0.35f, 0.38f } }, amb = 0.3f;
    int i;
    njCnkSetEasyMultiLight(3);
    njCnkSetSimpleMultiLight(3);
    njCnkSetEasyMultiAmbient(amb, amb, amb * 1.05f);
    njCnkSetSimpleMultiAmbient(amb, amb, amb * 1.05f);
    for (i = 0; i < 3; i++) {
        njCnkSetEasyMultiLightPoint(i + 1, pos[i][0], pos[i][1], pos[i][2]);
        njCnkSetEasyMultiLightRange(i + 1, 4000.0f, 4000.0f);
        njCnkSetEasyMultiLightColor(i + 1, c[i][0], c[i][1], c[i][2]);
        njCnkSetSimpleMultiLightPoint(i + 1, pos[i][0], pos[i][1], pos[i][2]);
        njCnkSetSimpleMultiLightRange(i + 1, 4000.0f, 4000.0f);
        njCnkSetSimpleMultiLightColor(i + 1, c[i][0], c[i][1], c[i][2]);
    }
    /* the single light models (draw_tp 2, 3): the key */
    njCnkSetEasyLightColor(c[0][0], c[0][1], c[0][2]);
    njCnkSetEasyLightIntensity(1.0f, amb);
    njCnkSetEasyLight(0.5f, -0.6f, -0.62f);
    njCnkSetSimpleLightColor(c[0][0], c[0][1], c[0][2]);
    njCnkSetSimpleLightIntensity(1.0f, amb);
    njCnkSetSimpleLight(0.5f, -0.6f, -0.62f);
}

static void scene_begin(void)
{
    NJS_SCREEN sc = { 500.0f, 640.0f, 480.0f, 320.0f, 240.0f };
    cam_keep = cam.mtx;
    cam.mtx = (float(*)[16])&view;   /* bhPutModel draws with cam.mtx */
    njSetScreen(&sc);
    njSetAspect(1.174f, 1.0f);
    njSetMatrix(NULL, cmat);
    njTranslate(NULL, 0, -CAM_Y, -CAM_Z);
    njGetMatrix(cam.mtx);
    njControl3D(0);
    studio_light();
    njSetMatrix(NULL, cam.mtx);
    njCnkSetEasyMultiLightMatrices();
    njCnkSetSimpleMultiLightMatrices();
}

static void scene_end(void)
{
    cam.mtx = cam_keep;
    if (cam.mtx) njSetMatrix(NULL, cam.mtx);
}

/* njReleaseTextureAll took everything (ps2_NaTextureFunction.c) */
void pc_lobby_textures_released(void)
{
    int k;
    actor_textures_released();
    font_state = 0;   /* the game's own: whoever loads it next releases it */
    for (k = 0; k < 2; k++) {   /* forget them; the lobby loads them again when it is still open */
        pv[k].tex_gone = 1;
        pv_release(&pv[k], k);
    }
}

/* ---------------------------------------------------------------- saves */
typedef struct SaveInfo { int slot, ply_id, stg, rom, gm_mode, count; } SaveInfo;
static SaveInfo* saves;   /* the valid ones by slot (lobby.h: the card's 0..14 or native save number - 1) */
static int n_saves;
static unsigned char save_buf[LOBBY_SAVE_BYTES];   /* the host's chosen save, read when the game starts */

/* read when the lobby opens (and when a chosen save cannot be read): no save is written at the title */
static void scan_saves(void)
{
    static unsigned char b[LOBBY_SAVE_BYTES];
    LobbySave* l;
    int i, n = lobby_save_list(&l);
    free(saves);
    saves = n ? malloc(sizeof(SaveInfo) * n) : NULL;   /* n <= SAVESTORE_MAX */
    n_saves = 0;
    for (i = 0; saves && i < n; i++) {
        const SAVEFILE* sf = (const SAVEFILE*)b;
        SaveInfo* s = &saves[n_saves];
        if (!lobby_read_listed(&l[i], b)) continue;
        s->slot = l[i].slot; s->ply_id = sf->ply_id & 3; s->stg = sf->stg_no; s->rom = sf->rom_no; s->gm_mode = sf->gm_mode; s->count = sf->save_ct;
        n_saves++;
    }
    free(l);
}

/* the index in saves of a slot, -1 none */
static int save_at(int slot)
{
    int lo = 0, hi = n_saves - 1;
    while (lo <= hi) {
        int m = lo + (hi - lo) / 2;
        if (saves[m].slot == slot) return m;
        if (saves[m].slot < slot) lo = m + 1; else hi = m - 1;
    }
    return -1;
}

static const char* chr_names[] = { "Claire", "Chris", "Steve", "Wesker" };

static void save_label(char* out, int n, int slot)
{
    int i = save_at(slot);
    if (i < 0) { snprintf(out, n, "Save %02d", slot + 1); return; }
    snprintf(out, n, "Save %02d: %s %d-%02d", slot + 1, chr_names[saves[i].ply_id], saves[i].stg, saves[i].rom);
}

/* the choices as lists: host START WITH: 0..2 new game, 3 + slot; joining ITEMS FROM: saved with the host, new, slot */
static int start_valid(int v) { return v >= 0 && (v < 3 || save_at(v - 3) >= 0); }
static int items_valid(int v) { return v == COOP_ITEMS_HOST || v == COOP_ITEMS_NEW || (v >= 0 && save_at(v) >= 0); }

/* the next choice in that list: the fixed ones first .. first + nfixed - 1, then base + each save's slot */
static int step_choice(int v, int dir, int first, int nfixed, int base)
{
    int n = nfixed + n_saves, k = v - base >= 0 ? save_at(v - base) : -1;
    int i = v >= first && v < first + nfixed ? v - first : k >= 0 ? nfixed + k : dir > 0 ? -1 : n;
    i = (i + dir + n) % n;
    return i < nfixed ? first + i : base + saves[i - nfixed].slot;
}

static void start_label(char* out, int n, int v)
{
    static const char* diff[] = { "New Game: Normal", "New Game: Easy", "New Game: Very Easy" };
    if (v < 3) snprintf(out, n, "%s", diff[v < 0 ? 0 : v]);
    else save_label(out, n, v - 3);
}

static void items_label(char* out, int n, int v)
{
    if (v == COOP_ITEMS_HOST)   /* the host says whether it has them once we are in its lobby */
        snprintf(out, n, "Saved with the host%s", g_coop.state == COOP_OFF || g_coop.host || g_coop.peer_rec < 0 ? ""
                 : g_coop.peer_rec == REC_HAVE ? " (available)" : g_coop.peer_rec == REC_OFF ? " (host: off)" : " (none)");
    else if (v < 0) snprintf(out, n, "New inventory");
    else save_label(out, n, v);
}

/* ---------------------------------------------------------------- font */
/* The title runs without the game's font (sys->ef_tlist: the boot's warning screen releases all
   textures; the option menu loads it for itself, as here). Loaded while the lobby is open. */
static unsigned char font_file[3 << 20] __attribute__((aligned(64)));

static void font_load(void)
{
    int n;
    if (sys->ss_flg & 0x40) return;
    n = GetInsideFileSize(sys->sys_partid, 1);
    if (n <= 0 || n > (int)sizeof(font_file)) { host_log("lobby: font file of %d bytes\n", n); return; }
    RequestReadInsideFile(sys->sys_partid, 1, font_file);
    font_state = 1;
}

static int font_ready(void)
{
    if (font_state == 1 && GetReadFileStatus() == 0) {
        bhSetFontTexture(font_file);
        font_state = 2;
    }
    return (sys->ss_flg & 0x40) != 0;
}

static void font_release(void)
{
    if (font_state == 2 && (sys->ss_flg & 0x40)) {
        njReleaseTexture(&sys->ef_tlist);
        bhGarbageTexture(NULL, 0);
        sys->ss_flg &= ~0x40;
    }
    font_state = 0;
}

/* The installed and the newest version at the bottom right of the title's 4:3 picture, below
   the copyright, in the game's font (title_version): Adv_BioCvTitle modes 4..17 (PRESS START, the menus and
   the title's fades; not the lobby, 30). The font as the lobby has it, its file read at once (the
   game's file reader stays free) and kept; released when the title ends (lobby_close). */
static void title_font(void)
{
    static int have;   /* font_file holds the font: 1; -1 it cannot be read */
    if (!(sys->ss_flg & 0x40) && !font_state) {
        if (!have) have = afs_read(sys->sys_partid, 1, font_file, sizeof(font_file)) ? 1 : -1;
        if (have < 0) return;
        bhSetFontTexture(font_file);
        font_state = 2;
    }
    font_ok = (sys->ss_flg & 0x40) != 0;
}

/* a version, its "."s at half advance (the font's "." is as wide as a digit: v0.6.9, not v0. 6. 9);
   draw 0: only the width */
static float ver_text(float x, float y, const char* s, float sc, unsigned int argb, int draw)
{
    float x0 = x, dot = text_w(".", sc);
    char c[2] = { 0 };
    for (; *s; s++) {
        c[0] = *s;
        if (draw) text(*s == '.' ? x - dot * 0.25f : x, y, c, sc, argb);
        x += *s == '.' ? dot * 0.5f : text_w(c, sc);
    }
    return x - x0;
}

/* Installed and newest version (net/update.h) below the copyright, right-aligned in the 4:3
   picture's corner: labels and versions each in a column. The installed one is red when a newer
   one is out; the newest "..." (asking), "unknown" (no answer) or "off" in grey. */
#define VER_SC 0.5f
static void title_version(void)
{
    static const char* const label[2] = { "Game Version:", "Latest Version:" };
    ADV_WORK* ap = &AdvWork;
    unsigned int a = (unsigned int)(255.0f * (1.0f - ap->FadeRate) * (1.0f - ap->SaverRate)) << 24;   /* under the title's fade and screen saver */
    char newest[32];
    const char* val[2];
    unsigned int col[2];
    int st, i;
    float lw, w0, w1, x;
    if (ap->Mode < 4 || ap->Mode > 17 || a < 8u << 24) return;
    title_font();
    st = update_newest(newest, sizeof(newest));
    val[0] = update_installed();
    val[1] = st == UPD_KNOWN ? newest : st == UPD_OFF ? "off" : st == UPD_CHECKING ? "..." : "unknown";
    col[0] = st == UPD_KNOWN && upd_newer(newest, val[0]) ? 0xE04040 : 0x60D060;
    col[1] = st == UPD_KNOWN ? 0x60D060 : 0xA0A0A0;
    lw = text_w(label[0], VER_SC) + text_w("  ", VER_SC);   /* the longer label, then the versions' column */
    w0 = ver_text(0, 0, val[0], VER_SC, 0, 0);
    w1 = ver_text(0, 0, val[1], VER_SC, 0, 0);
    {   /* the column as wide as the widest it can show ("unknown"), so the block never moves when the answer comes */
        float wu = ver_text(0, 0, "unknown", VER_SC, 0, 0), wm = w0 > w1 ? w0 : w1;
        x = 632 - lw - (wm > wu ? wm : wu);
    }
    for (i = 0; i < 2; i++) {
        float y = 445 + i * 14;
        text(x, y, label[i], VER_SC, a | 0xC8C8C8);
        ver_text(x + lw, y, val[i], VER_SC, a | col[i], 1);
    }
    AdvEasySetTextureList(0);   /* the title's texture list again */
}

/* ---------------------------------------------------------------- the lobby */
/* In steps (docs/COOP.md "Lobby"): 1 how to connect (Direct / Via server) and Host or Join,
   2 only that choice's rows plus the player and game rows, 3 the game rules (host: edits them,
   guest: the host's, read-only), with their pages 4 enemies and 5 extra events, step 4's page 7 the
   cutscene cast (its page 8 the "Custom" models, cast_list.c), and step 2's page 6 the model list. BACK goes one step back (from a page: to its entry); from step 1 it leaves the lobby.
   The rows in display order; a gap where the group changes. */
enum { R_MODE, R_HOSTSTEP, R_JOINSTEP,
       R_SERVER, R_PORT, R_ADDR, R_PW, R_RELAY,
       R_NAME, R_CHAR,
       R_START, R_ITEMS, R_FOLLOW, R_RULES,
       R_BIORAND, R_LOOT, R_IMP, R_TFREEZE, R_SKIPS, R_RIBBON, R_GOVER, R_ENEMYPG, R_EVENTPG,
       R_RAND, R_VARIETY, R_EVENTS, R_SEED, R_RSEED, R_EXTRA, R_PLACE, R_CASTPG, R_CAST, R_CASTPOOL, R_CASTCUSTOM, R_CASTASSIGN, R_CASTIMP, R_QUICK, R_DETECTOR,
       R_HOST, R_GO, R_COPY, R_RCOPY, R_KICK, R_STOP, R_JOIN,
       R_BACK, R_N };
static const unsigned char row_group[R_N] = { 0, 1, 1, 0, 0, 0, 0, 0, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 7, 7, 4, 4, 4, 4, 4, 8, 8, 9, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 6 };
_Static_assert(sizeof(row_group) == R_N, "row_group");
enum { F_NONE, F_NAME, F_ADDR, F_PORT, F_SEED, F_SERVER, F_PW };

static struct {
    int open, row, role;                /* role 0 host, 1 join */
    int step;                           /* 1 connection and role, 2 its options, 3 game rules, 4 / 5 its pages, 6 the models, 7 the cutscene cast (from 4),
                                           8 its "Custom" models (from 7) */
    int pick, top;                      /* the model page: the cursor's and the first shown list position (appear_at) */
    int mode;                           /* 0 direct (P2P), 1 via a co-op server (g_coop.conn Automatic or Via server) */
    int edit;                           /* F_* being edited */
    char buf[128];
    int caret, max;
    int spin;
    int want_chr, chr_stable;           /* debounce while the model choice is cycled */
    int items_sent;                     /* the joining items handed to coop_glue.c */
    char msg[96];
    int msg_t;
} lb = { .items_sent = -2 };

static int session_on(void) { return g_coop.state != COOP_OFF; }
static int hosting(void) { return session_on() && g_coop.host; }
static int joining(void) { return session_on() && !g_coop.host; }

/* the enemy options the rules show: joining, the host's (EV_EOPTS) */
static const EnemyOpts* shown_eo(void) { return lb.role ? enemy_opts() : &g_enemy_opts; }

static int row_visible(int r)
{
    int host = lb.role == 0, srv = lb.mode;
    if (r == R_BACK) return 1;
    if (lb.step == 6 || lb.step == 8) return 0;   /* the model page and "Custom" draw their lists themselves (draw_models, cast_list.c) */
    if (lb.step == 1) return r == R_MODE || r == R_HOSTSTEP || r == R_JOINSTEP;
    if (r == R_VARIETY || r == R_EVENTS) return lb.step == 4 && shown_eo()->rand;   /* only the randomizer has them */
    if (r == R_BIORAND) return lb.step == 3 && (host ? biorand_pack() : coop_biorand() == 1);   /* the host has a pack; a guest sees it while it is on */
    if (lb.step == 3) return r == R_GOVER || r == R_ENEMYPG || r == R_EVENTPG || ((r == R_LOOT || r == R_TFREEZE || r == R_SKIPS || r == R_RIBBON) && host) ||
                             (r == R_IMP && host && !g_coop.loot);   /* only shared loot has it */
    if (lb.step == 4) return (r >= R_RAND && r <= R_PLACE && (r != R_RSEED || host)) || r == R_CASTPG;
    if (lb.step == 7)   /* the cutscene cast: its model and assignment rows while it is on; the imported models' while we have some */
        return r == R_CAST || ((r == R_CASTPOOL || r == R_CASTASSIGN || r == R_CASTIMP || r == R_CASTCUSTOM) && shown_eo()->cast &&
                               (r != R_CASTIMP || __atomic_load_n(&actor_import_n, __ATOMIC_ACQUIRE) > 0) && (r != R_CASTCUSTOM || shown_eo()->cast_all == 2));
    if (lb.step == 5) return (r == R_QUICK && !biorand_next()) || r == R_DETECTOR;   /* BioRand has its own start inventory */
    switch (r) {
    case R_PORT: return host && !srv;
    case R_SERVER: case R_PW: case R_RELAY: return srv;   /* the co-op server (docs/SERVER.md) */
    case R_ADDR: case R_ITEMS: case R_FOLLOW: case R_JOIN: return !host;
    case R_NAME: case R_CHAR: return 1;
    case R_START: return host;
    case R_RULES: return host || g_coop.state == COOP_CONNECTED;   /* guest: the host's, once known */
    case R_HOST: return host && !session_on();
    case R_GO: case R_STOP: return host && hosting();
    case R_COPY: return host && hosting() && !srv;
    case R_RCOPY: return host && hosting() && srv;
    case R_KICK: return host && hosting() && (g_coop.state == COOP_CONNECTED || g_coop.state == COOP_LOST);   /* a partner is there (or was, silent) */
    default: return 0;
    }
}

/* shown, but not to be changed now */
static int row_locked(int r)
{
    switch (r) {
    case R_MODE: case R_PORT: case R_LOOT: case R_IMP: case R_ADDR: case R_RELAY: case R_SERVER: case R_PW: return session_on();
    case R_HOSTSTEP: return joining();
    case R_JOINSTEP: return hosting();
    case R_GOVER: case R_QUICK: case R_DETECTOR: case R_EXTRA: case R_PLACE: case R_RAND: case R_VARIETY: case R_EVENTS: case R_SEED: case R_BIORAND:
    case R_CAST: case R_CASTPOOL: case R_CASTCUSTOM: case R_CASTASSIGN: case R_CASTIMP: return lb.role != 0;   /* the host's choice */
    case R_RCOPY: return !g_coop.room[0];
    default: return 0;
    }
}

static int row_selectable(int r) { return row_visible(r) && !row_locked(r); }

static int first_row(void)
{
    int r;
    for (r = 0; r < R_N && !row_selectable(r); r++);
    return r < R_N ? r : R_BACK;
}

/* a settings table entry by its label (one not edited through a variable the glue sees: g_config) */
static const Setting* lset(const char* label)
{
    int i;
    for (i = 0; i < g_settings_n; i++) if (!strcmp(g_settings[i].label, label)) return &g_settings[i];
    return NULL;
}

static void say(const char* m) { snprintf(lb.msg, sizeof(lb.msg), "%s", m); lb.msg_t = 150; }

/* the enemy options' entries of the settings table (F1 and the OPTION pages edit the same) */
static const Setting* eset(const int* var)
{
    int i;
    for (i = 0; i < g_settings_n; i++) if (g_settings[i].val == var) return &g_settings[i];
    return NULL;
}

static int allowed(int field, int c)
{
    if (field == F_PORT || field == F_SEED) return c >= '0' && c <= '9';
    if (field == F_ADDR || field == F_SERVER) return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == ':' || c == '-' || (c == '#' && field == F_ADDR);
    if (field == F_PW) return c > 32 && c < 127;
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || strchr(" -_.!?'()+", c);
}

static const char* cycle_set(int field)
{
    return field == F_PORT || field == F_SEED ? "0123456789" : field == F_ADDR || field == F_SERVER ? "0123456789.:abcdefghijklmnopqrstuvwxyz-#ABCDEFGHJKLMNPQRSTUVWXYZ"
                                                          : " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.!?'";
}

static void edit_begin(int field)
{
    lb.edit = field;
    if (field == F_NAME) { snprintf(lb.buf, sizeof(lb.buf), "%s", g_coop.name); lb.max = COOP_NAME_MAX - 1; }
    else if (field == F_ADDR) { lb.buf[0] = 0; lb.max = 79; }   /* starts empty; left empty or cancelled, the old address stays */
    else if (field == F_SEED) { snprintf(lb.buf, sizeof(lb.buf), "%d", g_enemy_opts.seed); lb.max = 5; }
    else if (field == F_SERVER) { snprintf(lb.buf, sizeof(lb.buf), "%s", g_coop.server); lb.max = 79; }
    else if (field == F_PW) { snprintf(lb.buf, sizeof(lb.buf), "%s", g_coop.password); lb.max = 40; }
    else { snprintf(lb.buf, sizeof(lb.buf), "%d", g_coop.port); lb.max = 5; }
    lb.caret = (int)strlen(lb.buf);
    g_lobby_io.tail = g_lobby_io.head;   /* nothing typed before counts */
    g_lobby_io.text_on = 1;
    CallSystemSe(0, 3);
}

static void edit_end(int accept)
{
    if (accept) {
        if (lb.edit == F_NAME) {
            char* s = lb.buf;
            while (*s == ' ') s++;
            if (*s) { snprintf(g_coop.name, sizeof(g_coop.name), "%.*s", (int)sizeof(g_coop.name) - 1, s); }
        } else if (lb.edit == F_ADDR) {
            if (lb.buf[0]) snprintf(g_coop.join, sizeof(g_coop.join), "%s", lb.buf);
        } else if (lb.edit == F_SERVER) {
            snprintf(g_coop.server, sizeof(g_coop.server), "%s", lb.buf);   /* empty: no server */
        } else if (lb.edit == F_PW) {
            snprintf(g_coop.password, sizeof(g_coop.password), "%.63s", lb.buf);
        } else if (lb.edit == F_SEED) {
            if (lb.buf[0]) settings_set(eset(&g_enemy_opts.seed), atoi(lb.buf));   /* clamped to the table's range */
        } else {
            int port = atoi(lb.buf);
            if (port >= 1 && port <= 65535) g_coop.port = port;
        }
        lobby_settings_changed();
    }
    if (lb.edit == F_PW) rdv_wipe(lb.buf, sizeof(lb.buf));   /* the edit copy of the password */
    lb.edit = F_NONE;
    g_lobby_io.text_on = 0;
    CallSystemSe(0, accept ? 3 : 0);
}

static void edit_insert(int c)
{
    int n = (int)strlen(lb.buf);
    if (n >= lb.max || !allowed(lb.edit, c)) return;
    memmove(lb.buf + lb.caret + 1, lb.buf + lb.caret, n - lb.caret + 1);
    lb.buf[lb.caret++] = (char)c;
}

/* keyboard (typed text) and pad (up/down change the character at the caret) */
static void edit_input(unsigned int press, unsigned int rept, unsigned int ok, unsigned int cancel)
{
    while (g_lobby_io.tail != __atomic_load_n(&g_lobby_io.head, __ATOMIC_ACQUIRE)) {
        int k = g_lobby_io.keys[g_lobby_io.tail & 63], n = (int)strlen(lb.buf);
        g_lobby_io.tail++;
        if (k == LK_ENTER) { edit_end(1); return; }
        if (k == LK_ESC) { edit_end(0); return; }
        if (k == LK_BACK && lb.caret > 0) { memmove(lb.buf + lb.caret - 1, lb.buf + lb.caret, n - lb.caret + 1); lb.caret--; }
        else if (k == LK_DEL && lb.caret < n) memmove(lb.buf + lb.caret, lb.buf + lb.caret + 1, n - lb.caret);
        else if (k == LK_LEFT && lb.caret > 0) lb.caret--;
        else if (k == LK_RIGHT && lb.caret < n) lb.caret++;
        else if (k == LK_HOME) lb.caret = 0;
        else if (k == LK_END) lb.caret = n;
        else if (k >= 32 && k < 127) edit_insert(k);
    }
    if (press & ok) { edit_end(1); return; }
    if (press & cancel) { edit_end(0); return; }
    if (rept & (BTN_UP | BTN_DOWN)) {
        const char* set = cycle_set(lb.edit);
        const char* at;
        int n = (int)strlen(set), i;
        if (lb.caret == (int)strlen(lb.buf)) { if ((int)strlen(lb.buf) >= lb.max) return; lb.buf[lb.caret] = set[0]; lb.buf[lb.caret + 1] = 0; }
        at = strchr(set, lb.buf[lb.caret]);
        i = at ? (int)(at - set) : 0;
        i = (i + ((rept & BTN_UP) ? 1 : n - 1)) % n;
        lb.buf[lb.caret] = set[i];
        CallSystemSe(0, 2);
    }
    if ((rept & BTN_LEFT) && lb.caret > 0) lb.caret--;
    if ((rept & BTN_RIGHT) && lb.caret < (int)strlen(lb.buf)) lb.caret++;
}

static void change(int r, int dir)
{
    switch (r) {
    case R_MODE: lb.mode ^= 1; g_coop.conn = lb.mode ? COOP_CONN_AUTO : COOP_CONN_DIRECT; break;   /* the server's default: direct first */
    case R_RELAY: g_coop.conn = g_coop.conn == COOP_CONN_SERVER ? COOP_CONN_AUTO : COOP_CONN_SERVER; break;
    case R_LOOT: g_coop.loot ^= 1; break;
    case R_TFREEZE: g_coop.trade_freeze ^= 1; break;
    case R_IMP: g_coop.important ^= 1; break;
    case R_SKIPS: g_coop.skip_sync ^= 1; break;
    case R_GOVER: g_coop.gameover_both ^= 1; break;
    case R_QUICK: g_coop.quick_start ^= 1; break;
    case R_BIORAND: if (!biorand_toggle()) { say(biorand_locked()); CallSystemSe(0, 1); return; } break;
    case R_DETECTOR: g_coop.skip_detector ^= 1; break;
    case R_RIBBON: settings_step(lset("Free Typewriter Saves"), dir); break;   /* the GAMEPLAY setting (g_config) */
    case R_START: g_coop.start = step_choice(g_coop.start, dir, 0, 3, 3); break;
    case R_ITEMS: g_coop.items = step_choice(g_coop.items, dir, COOP_ITEMS_HOST, 2, 0); break;
    case R_FOLLOW: g_coop.follow ^= 1; break;   /* the joining player's own choice, also during a session */
    case R_EXTRA: settings_step(eset(&g_enemy_opts.extra), dir); break;
    case R_PLACE: settings_step(eset(&g_enemy_opts.place), dir); break;
    case R_RAND: settings_step(eset(&g_enemy_opts.rand), dir); break;
    case R_VARIETY: settings_step(eset(&g_enemy_opts.variety), dir); break;
    case R_EVENTS: settings_step(eset(&g_enemy_opts.events), dir); break;
    case R_CAST: settings_step(eset(&g_enemy_opts.cast), dir); break;
    case R_CASTPOOL: settings_step(eset(&g_enemy_opts.cast_all), dir); break;
    case R_CASTASSIGN: settings_step(eset(&g_enemy_opts.cast_assign), dir); break;
    case R_CASTIMP: settings_step(eset(&g_enemy_opts.cast_imports), dir); break;
    default: return;
    }
    CallSystemSe(0, 2);
    lobby_settings_changed();
}

static int has_value(int r)
{
    return r == R_MODE || r == R_RELAY || r == R_BIORAND || r == R_LOOT || r == R_IMP || r == R_TFREEZE || r == R_SKIPS || r == R_RIBBON || r == R_GOVER || r == R_QUICK || r == R_DETECTOR || r == R_START || r == R_ITEMS || r == R_FOLLOW ||
           r == R_EXTRA || r == R_PLACE || r == R_RAND || r == R_VARIETY || r == R_EVENTS || r == R_CAST || r == R_CASTPOOL || r == R_CASTASSIGN || r == R_CASTIMP;
}

/* the joining player's items for coop_glue.c (own inventory on join) */
static void hand_items(void)
{
    int v = g_coop.items, ok = coop_items_apply();
    lb.items_sent = v;
    host_log("lobby: joining with %s\n", v == COOP_ITEMS_HOST ? "the items saved with the host (new-game items if it has none)"
                                        : ok ? "the items of a save" : "the new-game items");
    if (ok) host_log("lobby: items from save file %d\n", v + 1);
}

/* the host's join address for the partner: the public one once the router mapped the port */
static void join_address(char* out, int n)
{
    PortmapStatus pm;
    portmap_status(&pm);
    if (pm.public_ip[0]) snprintf(out, n, "%s:%d#%s", pm.public_ip, pm.public_port, g_coop.code);
    else snprintf(out, n, "%s:%d#%s", pm.local_ip[0] ? pm.local_ip : "127.0.0.1", g_coop.port, g_coop.code);
}

static void step_to(int s)
{
    lb.step = s;
    lb.row = first_row();
    host_log("lobby: step %d (%s, %s)\n", s, lb.role ? "join" : "host", lb.mode ? "via server" : "direct");
    CallSystemSe(0, 3);
}

/* one step back; from the first, out of the lobby (1) */
static int back(void)
{
    CallSystemSe(0, 0);
    if (lb.step <= 1) return 1;
    if (lb.step == 6) { lb.step = 2; lb.row = R_CHAR; }
    else if (lb.step == 8) { lb.step = 7; lb.row = R_CASTCUSTOM; }
    else if (lb.step == 7) { lb.step = 4; lb.row = R_CASTPG; }
    else if (lb.step > 3) { lb.row = lb.step == 4 ? R_ENEMYPG : R_EVENTPG; lb.step = 3; }
    else { lb.step--; lb.row = lb.step == 2 ? R_RULES : lb.role ? R_JOINSTEP : R_HOSTSTEP; }
    if (!row_selectable(lb.row)) lb.row = first_row();
    host_log("lobby: back to step %d\n", lb.step);
    return 0;
}

/* via a server needs its address before opening or joining */
static int need_server(void)
{
    if (!lb.mode || g_coop.server[0]) return 0;
    say("Enter the co-op server address first.");
    CallSystemSe(0, 1);
    lb.row = R_SERVER;
    return 1;
}

static void pick_open(void);   /* the model page (below) */
static void custom_open(void);

/* 0 stay, 1 back to the menu, 2 new game, 3 the chosen save */
static int activate(int r)
{
    switch (r) {
    case R_HOSTSTEP: lb.role = 0; step_to(2); return 0;
    case R_JOINSTEP: lb.role = 1; step_to(2); return 0;
    case R_RULES: step_to(3); return 0;
    case R_ENEMYPG: step_to(4); return 0;
    case R_CASTPG: step_to(7); return 0;
    case R_CASTCUSTOM: custom_open(); return 0;
    case R_EVENTPG: step_to(5); return 0;
    case R_CHAR: pick_open(); return 0;
    case R_NAME: edit_begin(F_NAME); return 0;
    case R_PORT: edit_begin(F_PORT); return 0;
    case R_SEED: edit_begin(F_SEED); return 0;
    case R_RSEED: {
        const Setting* st = eset(&g_enemy_opts.seed);
        char m[48];
        settings_set(st, (int)((net_ms() * 2654435761u >> 8) % (unsigned)(st->hi + 1)));
        snprintf(m, sizeof(m), "New seed: %d", g_enemy_opts.seed);
        say(m);
        CallSystemSe(0, 3);
        return 0;
    }
    case R_ADDR: edit_begin(F_ADDR); return 0;
    case R_SERVER: edit_begin(F_SERVER); return 0;
    case R_PW: edit_begin(F_PW); return 0;
    case R_RCOPY:
        snprintf(g_lobby_io.clip, sizeof(g_lobby_io.clip), "%.4s-%.4s", g_coop.room, g_coop.room + 4);
        g_lobby_io.clip_req = 1;
        say("Room code copied.");
        CallSystemSe(0, 3);
        return 0;
    case R_HOST:
        if (need_server()) return 0;
        g_coop.request = COOP_REQ_HOST;
        CallSystemSe(0, 3);
        return 0;
    case R_JOIN:
        if (session_on()) { g_coop.request = COOP_REQ_STOP; CallSystemSe(0, 0); return 0; }
        if (need_server()) return 0;
        hand_items();
        g_coop.request = COOP_REQ_JOIN;
        CallSystemSe(0, 3);
        return 0;
    case R_STOP: g_coop.request = COOP_REQ_STOP; CallSystemSe(0, 0); return 0;
    case R_KICK: g_coop.request = COOP_REQ_KICK; say("Partner removed. They can rejoin with the same code."); CallSystemSe(0, 0); return 0;
    case R_COPY:
        join_address(g_lobby_io.clip, sizeof(g_lobby_io.clip));
        g_lobby_io.clip_req = 1;
        say("Join address copied.");
        CallSystemSe(0, 3);
        return 0;
    case R_GO:
        if (g_coop.start >= 3) {
            static int asked = -1;   /* the save BioRand's question was shown for: START again loads it */
            if (!lobby_read_save(g_coop.start - 3, save_buf)) { say("Cannot read that save. Choose another."); CallSystemSe(0, 1); scan_saves(); return 0; }
            if (pc_biorand_save_differs(save_buf)) {   /* (biorand_glue.c, as the load screen asks) */
                if (asked != g_coop.start) {
                    char m[96];
                    asked = g_coop.start;
                    snprintf(m, sizeof(m), "%s %s START again to load.", pc_biorand_ask(0), pc_biorand_ask(1));
                    say(m);
                    CallSystemSe(0, 1);
                    return 0;
                }
                pc_biorand_save_accept(save_buf);
            }
            asked = -1;
            host_log("lobby: starting from save file %d\n", g_coop.start - 2);
            return 3;
        }
        sys->gm_mode = g_coop.start;
        biorand_game(biorand_setting());   /* the rule BIORAND (a pack is checked by biorand_active) */
        host_log("lobby: starting a new game (mode %d)\n", g_coop.start);
        return 2;
    case R_BACK: return back();
    default:
        if (has_value(r)) change(r, 1);
        return 0;
    }
}

/* the rows' layout: a heading, then the rows from ROW_Y0 to the info panel (y 384) */
#define ROW_X 316.0f
#define VAL_X 410.0f
#define HEAD_Y 70.0f
#define ROW_Y0 94.0f
#define ROW_SC 0.62f
#define GAP 8.0f

static int gaps(void)
{
    int r, g = -1, n = 0;
    for (r = 0; r < R_N; r++)
        if (row_visible(r)) { n += g >= 0 && row_group[r] != g; g = row_group[r]; }
    return n;
}

/* 25 px apart, closer when the rows would reach the info panel */
static float row_h(void)
{
    int r, n = 0;
    float h;
    for (r = 0; r < R_N; r++) n += row_visible(r);
    h = (382.0f - ROW_Y0 - gaps() * GAP) / n;
    return h < 25.0f ? h : 25.0f;
}

static float row_y(int row)
{
    int r, g = -1, n = 0, k = 0;
    for (r = 0; r < row; r++)
        if (row_visible(r)) { k += g >= 0 && row_group[r] != g; g = row_group[r]; n++; }
    k += g >= 0 && row_group[row] != g;
    return ROW_Y0 + n * row_h() + k * GAP;
}

static int row_at(float x, float y)
{
    int r;
    if (x < ROW_X - 8 || x > 636) return -1;
    for (r = 0; r < R_N; r++) {
        float ry = row_y(r);
        if (row_visible(r) && y >= ry - 3 && y < ry + row_h() - 3) return r;
    }
    return -1;
}

static void row_move(int dir)
{
    int i = lb.row, k;
    for (k = 0; k < R_N; k++) {
        i = (i + dir + R_N) % R_N;
        if (row_selectable(i)) { lb.row = i; CallSystemSe(0, 2); return; }
    }
}

static const char* heading(void)
{
    static char h[64];
    if (lb.step == 1) return "HOW DO YOU WANT TO PLAY?";
    if (lb.step == 3) return lb.role ? "THE HOST'S GAME RULES" : "GAME RULES";
    if (lb.step == 6) return "CHARACTER MODEL";
    if (lb.step == 7) return lb.role ? "HOST RULES - CUTSCENE CAST" : "GAME RULES - CUTSCENE CAST";
    if (lb.step == 8) return "GAME RULES - CUSTOM MODELS";
    if (lb.step > 3) return lb.step == 4 ? (lb.role ? "HOST RULES - RANDOMIZER" : "GAME RULES - RANDOMIZER")
                                         : (lb.role ? "HOST RULES - EXTRA EVENTS" : "GAME RULES - EXTRA EVENTS");
    snprintf(h, sizeof(h), "%s - %s", lb.role ? "JOIN A GAME" : "HOST A GAME", lb.mode ? "VIA SERVER" : "DIRECT");
    return h;
}

static void row_text(int r, char* label, char* val, int n)
{
    static const char* loots[] = { "Shared", "Individual" };
    const EnemyOpts* eo = shown_eo();
    *val = 0;
    switch (r) {
    case R_MODE: strcpy(label, "CONNECT"); snprintf(val, n, "%s", lb.mode ? "Via server" : "Direct"); break;
    case R_HOSTSTEP: strcpy(label, hosting() ? "HOST A GAME (OPEN)" : "HOST A GAME"); break;
    case R_JOINSTEP: strcpy(label, joining() ? "JOINED LOBBY" : "JOIN A GAME"); break;
    case R_RELAY: strcpy(label, "RELAY"); snprintf(val, n, "%s", g_coop.conn == COOP_CONN_SERVER ? "Always" : "Only if needed"); break;
    case R_NAME: strcpy(label, "NAME"); snprintf(val, n, "%s", lb.edit == F_NAME ? lb.buf : g_coop.name); break;
    case R_CHAR: strcpy(label, "MODEL"); snprintf(val, n, "%s", appear_name(g_coop.chr)); break;
    case R_PORT: strcpy(label, "PORT"); if (lb.edit == F_PORT) snprintf(val, n, "%s", lb.buf); else snprintf(val, n, "%d", g_coop.port); break;
    case R_LOOT: strcpy(label, "LOOT"); snprintf(val, n, "%s", loots[g_coop.loot & 1]); break;
    case R_TFREEZE: strcpy(label, "TRADING"); snprintf(val, n, "%s", g_coop.trade_freeze ? "On" : "Off"); break;
    case R_IMP: strcpy(label, "GEAR"); snprintf(val, n, "%s", g_coop.important ? "For both" : "Shared"); break;
    case R_SKIPS: strcpy(label, "CUTSCENE SKIP"); snprintf(val, n, "%s", g_coop.skip_sync ? "Together" : "Separately"); break;
    case R_GOVER: strcpy(label, "GAME OVER"); snprintf(val, n, "%s", (lb.role ? coop_host_gameover_both() : g_coop.gameover_both) ? "Both" : "Each player"); break;
    case R_BIORAND: strcpy(label, "BIORAND"); snprintf(val, n, (lb.role ? coop_biorand() == 1 : biorand_setting()) && biorand_seed_text()[0] ? "On (%s)" : "%s",
                                                       (lb.role ? coop_biorand() == 1 : biorand_setting()) ? biorand_seed_text()[0] ? biorand_seed_text() : "On" : "Off"); break;
    case R_QUICK: strcpy(label, "QUICK START"); snprintf(val, n, "%s", (lb.role ? coop_quick_start() : g_coop.quick_start) ? "On" : "Off"); break;
    case R_DETECTOR: strcpy(label, "METAL DETECTOR"); snprintf(val, n, "%s", (lb.role ? coop_skip_detector() : g_coop.skip_detector) ? "Skipped" : "Original"); break;
    case R_RIBBON: strcpy(label, "INK RIBBONS"); snprintf(val, n, "%s", settings_get(lset("Free Typewriter Saves")) ? "Not needed" : "Needed"); break;
    case R_START: strcpy(label, "START"); start_label(val, n, g_coop.start); break;
    case R_RULES: strcpy(label, lb.role ? "HOST RULES" : "GAME RULES"); break;
    case R_ENEMYPG: strcpy(label, "RANDOMIZER"); snprintf(val, n, "%s", eo->rand ? "On" : "Off"); break;   /* a page (draw_rows adds its arrow) */
    case R_EVENTPG: strcpy(label, "EXTRA EVENTS"); break;   /* a page of many rules: no summary */
    case R_HOST: strcpy(label, "OPEN THE LOBBY"); break;
    case R_GO: strcpy(label, "START THE GAME"); break;
    case R_STOP: strcpy(label, "CLOSE THE LOBBY"); break;
    case R_KICK: strcpy(label, "REMOVE PARTNER"); break;
    case R_COPY: strcpy(label, "COPY JOIN ADDRESS"); break;
    case R_RCOPY: strcpy(label, g_coop.room[0] ? "COPY ROOM CODE" : "WAITING FOR ROOM CODE"); break;
    case R_ADDR: strcpy(label, lb.mode ? "CODE" : "ADDRESS"); snprintf(val, n, "%.*s", n - 1, lb.edit == F_ADDR ? lb.buf : coop_shown(g_coop.join)); break;
    case R_SERVER: strcpy(label, "SERVER"); snprintf(val, n, "%.*s", n - 1, lb.edit == F_SERVER ? lb.buf : g_coop.server[0] ? coop_shown(g_coop.server) : "-"); break;
    case R_PW: {   /* shown as stars, also while typing */
        int k, len = (int)strlen(lb.edit == F_PW ? lb.buf : g_coop.password);
        strcpy(label, "PASSWORD");
        for (k = 0; k < len && k < n - 1; k++) val[k] = '*';
        val[k] = 0;
        if (!len && lb.edit != F_PW) snprintf(val, n, "-");
        break;
    }
    case R_ITEMS: strcpy(label, "ITEMS"); items_label(val, n, g_coop.items); break;
    case R_FOLLOW: strcpy(label, "FOLLOW"); snprintf(val, n, "%s", g_coop.follow ? "On" : "Off"); break;
    case R_EXTRA: strcpy(label, "EXTRA ENEMIES"); snprintf(val, n, "%s", eset(&g_enemy_opts.extra)->names[eo->extra & 3]); break;
    case R_PLACE: strcpy(label, "LAYOUT"); snprintf(val, n, "%s", eo->place ? "On" : "Off"); break;
    case R_RAND: strcpy(label, "RANDOMIZER"); snprintf(val, n, "%s", eo->rand ? "On" : "Off"); break;
    case R_VARIETY: strcpy(label, "VARIETY"); snprintf(val, n, "%s", eset(&g_enemy_opts.variety)->names[eo->variety & 1]); break;
    case R_EVENTS: strcpy(label, "EVENT ENEMIES"); snprintf(val, n, "%s", eo->events ? "On" : "Off"); break;
    case R_CASTPG: strcpy(label, "CUTSCENE CAST"); snprintf(val, n, "%s", eo->cast ? "On" : "Off"); break;   /* a page */
    case R_CAST: strcpy(label, "RANDOMIZE CAST"); snprintf(val, n, "%s", eo->cast ? "On" : "Off"); break;
    case R_CASTPOOL: strcpy(label, "MODELS"); snprintf(val, n, "%s", eset(&g_enemy_opts.cast_all)->names[(unsigned)eo->cast_all <= 2 ? eo->cast_all : 0]); break;
    case R_CASTCUSTOM:   /* a page: how many are checked (joining: the host's, of our list) */
        strcpy(label, "CHOOSE MODELS");
        cast_list_summary(lb.role ? enemy_cast_off() : cast_off, eo->cast_imports ? __atomic_load_n(&actor_import_n, __ATOMIC_ACQUIRE) : 0, val, n);
        break;
    case R_CASTASSIGN: strcpy(label, "ASSIGN"); snprintf(val, n, "%s", eset(&g_enemy_opts.cast_assign)->names[eo->cast_assign & 1]); break;
    case R_CASTIMP: strcpy(label, "IMPORTED MODELS"); snprintf(val, n, "%s", eo->cast_imports ? "On" : "Off"); break;
    case R_SEED: strcpy(label, "SEED"); if (lb.edit == F_SEED) snprintf(val, n, "%s", lb.buf); else snprintf(val, n, "%d", eo->seed); break;
    case R_RSEED: strcpy(label, "RANDOM SEED"); break;
    case R_JOIN: strcpy(label, session_on() ? "LEAVE" : "JOIN"); break;
    case R_BACK: strcpy(label, "BACK"); break;
    }
}

/* the help line: what the selected row is for */
static const char* row_help(int r)
{
    int host = lb.role == 0;
    switch (r) {
    case R_MODE:
        return lb.mode ? "Join with a room code through a co-op server. Tries a direct connection first, then the server relay."
                       : "Connect to the host's address. The host must allow connections through their router.";
    case R_HOSTSTEP: return "Open a lobby for your partner, then start a new game or load a save.";
    case R_JOINSTEP: return "Enter the address or room code from your host. The host starts the game.";
    case R_SERVER: return "Both players need the same co-op server. Enter its address, with an optional port (default: 7141).";
    case R_PORT: return "Port used to host (default: 7140). Internet partners need router forwarding or a server relay.";
    case R_ADDR:
        return lb.mode ? "Enter the host's room code; case and dash do not matter. Ctrl+V: paste."
                       : "Paste the host's join address, copied with COPY JOIN ADDRESS. Ctrl+V: paste.";
    case R_PW: return host ? "Set an optional room password for your partner to enter. The password is not saved." : "Enter the host's password, or leave blank if the room has none.";
    case R_RELAY: return "Only If Needed tries direct first, then relays through the server. Always uses the server relay.";
    case R_NAME: return "Choose the name your partner sees overhead and in the lobby.";
    case R_CHAR:
        if (lb.step == 6 && lb.pick >= appear_count()) return "Return to the lobby without changing your model.";
        return lb.step == 6 ? "Scroll with up/down, L1/R1, Page Up/Down or the wheel. Confirm: use model; BACK: cancel."
                            : "Choose your appearance. You and your partner both see it.";
    case R_START: return "Choose a new game and difficulty, or one of your saves.";
    case R_ITEMS: return "Join with items saved by the host, a new inventory, or items from one of your saves.";
    case R_FOLLOW: return eset(&g_coop.follow)->help;
    case R_RULES: return host ? "Set loot, trading, cutscene skipping, saving and game over rules. Open the other pages for more rules."
                              : "View the host's game rules, enemy settings and extra events. Only the host can change them.";
    case R_ENEMYPG: return host ? "Set extra enemies, their positions and types, and cutscene models."
                                : "View the host's enemy and cutscene model settings. Only the host can change them.";
    case R_EVENTPG: return host ? "Set quick start and prison metal detector rules." : "View the host's quick start and metal detector rules. Only the host can change them.";
    case R_LOOT: return "Shared divides supplies and shares the item box; Individual gives separate copies and boxes. Keys go to both.";
    case R_TFREEZE: return eset(&g_coop.trade_freeze)->help;
    case R_BIORAND: return host ? "Play your generated BioRand game. Your partner needs the same seed, options and matching game data."
                                : "Play the host's BioRand game with your matching generated game.";
    case R_QUICK: return "Start with the manual read and a knife in both players' inventories. No effect with BioRand.";
    case R_DETECTOR: return "Skipped lets you pass with weapons. Security boxes stay shut unless they hold your items.";
    case R_IMP: return "For Both gives each player weapons and ink ribbons. Shared gives one pickup to divide between you.";
    case R_SKIPS: return "Together skips shared cutscenes when both players choose to skip. Separately lets either player skip alone.";
    case R_RIBBON: return "Not Needed lets both players save at typewriters without using ink ribbons.";
    case R_GOVER: return "Each Player retries alone. Both lets the host retry or return to the title screen for both players.";
    case R_EXTRA: return eset(&g_enemy_opts.extra)->help;
    case R_PLACE: return eset(&g_enemy_opts.place)->help;
    case R_RAND: return eset(&g_enemy_opts.rand)->help;
    case R_VARIETY: return eset(&g_enemy_opts.variety)->help;
    case R_EVENTS: return eset(&g_enemy_opts.events)->help;
    case R_CASTPG: return host ? "Choose how story characters' appearances are randomized in cutscenes."
                               : "View the host's randomized cutscene models. Only the host can change them.";
    case R_CAST: return eset(&g_enemy_opts.cast)->help;
    case R_CASTPOOL: return eset(&g_enemy_opts.cast_all)->help;
    case R_CASTCUSTOM: return host ? "Choose the models the characters can appear as." : "The models the host chose. Only the host can change them.";
    case R_CASTASSIGN: return eset(&g_enemy_opts.cast_assign)->help;
    case R_CASTIMP: return eset(&g_enemy_opts.cast_imports)->help;
    case R_SEED: return eset(&g_enemy_opts.seed)->help;
    case R_RSEED: return "Picks a new random seed.";
    case R_HOST: return lb.mode ? "Open a lobby on the co-op server. Give your partner the room code shown here."
                                : "Open your lobby so a partner can join. You can still change the starting game and rules.";
    case R_GO: return "Start the game. Your partner joins once you can move.";
    case R_COPY: return "Copy the join address for your partner. Uses your public address if available, otherwise your local address.";
    case R_RCOPY: return "Copy your room code. Your partner uses JOIN A GAME > VIA SERVER > ROOM CODE.";
    case R_STOP: return "Close your lobby and disconnect your partner.";
    case R_KICK: return "Remove your partner and reopen their place. They can rejoin with the same address or room code.";
    case R_JOIN: return session_on() ? "Leaves the host's lobby." : "Joins the host's lobby; the host starts the game.";
    case R_BACK:
        return lb.step == 1 ? "Return to the title screen and close any open lobby or co-op session."
             : lb.step == 2 ? "Back to choosing how to connect (an open lobby stays open)." : lb.step == 3 ? "Back to the lobby." : lb.step == 7 ? "Back to the randomizer page." : "Back to the game rules.";
    default: return "";
    }
}

/* ---------------------------------------------------------------- the model page (step 6)
   "You appear as" as a list (docs/COOP.md "Lobby"): the menus' entries (actors.c appear_at, any
   count) PICK_ROWS at a time, then BACK (position appear_count()). The preview shows the entry under
   the cursor; confirm takes it into g_coop.chr (ini coop_char, sent to the partner as before). */
enum { BTN_L1 = 0x4, BTN_R1 = 0x8 };   /* Pad[] bits */
#define PICK_H 22.0f
#define PICK_ROWS 11
#define PICK_BACK_Y (ROW_Y0 + PICK_ROWS * PICK_H + GAP)
#define PICK_BAR_X 627.0f               /* the scroll bar, right of the names */

static int pick_max_top(void) { int n = appear_count(); return n > PICK_ROWS ? n - PICK_ROWS : 0; }

/* the first shown position moved by d (clamped), the cursor kept on the page */
static void pick_scroll(int d)
{
    int n = appear_count(), was = lb.pick;
    lb.top += d;
    if (lb.top > pick_max_top()) lb.top = pick_max_top();
    if (lb.top < 0) lb.top = 0;
    if (lb.pick < n) lb.pick = lb.pick < lb.top ? lb.top : lb.pick >= lb.top + PICK_ROWS ? lb.top + PICK_ROWS - 1 : lb.pick;
    if (lb.pick != was) CallSystemSe(0, 2);
}

/* the cursor to position i (clamped to the list, or wrapping through BACK); the page follows it */
static void pick_to(int i, int wrap)
{
    int n = appear_count();
    if (wrap) i = (i + n + 1) % (n + 1);
    else i = i < 0 ? 0 : i >= n ? n - 1 : i;
    if (i != lb.pick) CallSystemSe(0, 2);
    lb.pick = i;
    if (i < n && i < lb.top) lb.top = i;
    if (i < n && i >= lb.top + PICK_ROWS) lb.top = i - PICK_ROWS + 1;
    pick_scroll(0);
}

/* the cutscene cast's "Custom" models (step 8): cast_list.c's list where the model page's is */
static void custom_open(void)
{
    lb.step = 8;
    cast_list_open(ROW_X, 634, ROW_Y0, PICK_H, ROW_SC, 10);
    host_log("lobby: the cutscene cast's custom models\n");
}

static void pick_open(void)
{
    lb.step = 6;
    lb.pick = appear_pos(g_coop.chr);   /* the current model, in the middle of the page */
    lb.top = lb.pick - PICK_ROWS / 2;
    pick_scroll(0);
    host_log("lobby: model page (%d models)\n", appear_count());
    CallSystemSe(0, 3);
}

/* confirm: the entry under the cursor (or BACK) */
static void pick_take(void)
{
    if (lb.pick >= appear_count()) { back(); return; }
    g_coop.chr = appear_at(lb.pick);
    lobby_settings_changed();
    host_log("lobby: model %s\n", appear_name(g_coop.chr));
    lb.step = 2;
    lb.row = R_CHAR;
    CallSystemSe(0, 3);
}

/* the position under the pointer (appear_count(): BACK), -1 none */
static int pick_at(float x, float y)
{
    int n = appear_count(), i;
    if (x < ROW_X - 8 || x > 636) return -1;
    if (y >= PICK_BACK_Y - 3 && y < PICK_BACK_Y + PICK_H - 3) return n;
    if (y < ROW_Y0 - 3) return -1;
    i = (int)((y - ROW_Y0 + 3) / PICK_H);
    return i < PICK_ROWS && lb.top + i < n ? lb.top + i : -1;
}

static void pick_input(unsigned int press, unsigned int rept, unsigned int ok, unsigned int cancel)
{
    int n = appear_count(), page = PICK_ROWS - 1, hit;
    while (g_lobby_io.tail != __atomic_load_n(&g_lobby_io.head, __ATOMIC_ACQUIRE)) {   /* lobby_io.c: list_on */
        int k = g_lobby_io.keys[g_lobby_io.tail & 63];
        g_lobby_io.tail++;
        if (k == LK_PGUP) pick_to(lb.pick - page, 0);
        else if (k == LK_PGDN) pick_to(lb.pick + page, 0);
        else if (k == LK_HOME) pick_to(0, 0);
        else if (k == LK_END) pick_to(n - 1, 0);
    }
    if (mt.wheel) pick_scroll(-3 * mt.wheel);   /* > 0: away from the player, up the list */
    if (mt.left && n > PICK_ROWS && mt.x >= PICK_BAR_X - 4 && mt.x <= PICK_BAR_X + 9 && mt.y >= ROW_Y0 - 2 && mt.y < ROW_Y0 + PICK_ROWS * PICK_H) {
        /* a click on the scroll bar: that part of the list (a long list: 300 and more with imports) */
        pick_scroll((int)((mt.y - ROW_Y0) / (PICK_ROWS * PICK_H) * (pick_max_top() + 1)) - lb.top);
        return;
    }
    hit = pick_at(mt.x, mt.y);
    if (hit >= 0 && (mt.moved || mt.left || mt.wheel) && hit != lb.pick) { lb.pick = hit; CallSystemSe(0, 2); }
    if (mt.left && hit >= 0) { pick_take(); return; }
    if (mt.right) { back(); return; }
    if (rept & BTN_UP) pick_to(lb.pick - 1, 1);
    else if (rept & BTN_DOWN) pick_to(lb.pick + 1, 1);
    else if (rept & (BTN_L1 | BTN_LEFT)) pick_to(lb.pick - page, 0);
    else if (rept & (BTN_R1 | BTN_RIGHT)) pick_to(lb.pick + page, 0);
    else if (press & ok) pick_take();
    else if (press & cancel) back();
}

static void draw_models(void)
{
    int n = appear_count(), cur = appear_pos(g_coop.chr), i;
    float right = n > PICK_ROWS ? PICK_BAR_X - 6 : 634;   /* the names stop before the scroll bar */
    char pos[24];
    text(ROW_X - 4, HEAD_Y, heading(), 0.5f, 0xFFFFE080);
    snprintf(pos, sizeof(pos), "%d / %d", (lb.pick < n ? lb.pick : cur) + 1, n);
    text(634 - text_w(pos, 0.45f), HEAD_Y + 2, pos, 0.45f, 0xFFA0A0A0);
    for (i = lb.top; i < n && i < lb.top + PICK_ROWS; i++) {
        float y = ROW_Y0 + (i - lb.top) * PICK_H, w, sc, lim = right;
        const char* nm = appear_name(appear_at(i));
        const char* grp = appear_group(appear_at(i));
        unsigned int col = i == cur ? 0xFFFFE080 : i == lb.pick ? 0xFFFFFFFF : 0xFFB0B0B0;   /* the current one: gold, with an arrow */
        if (i == lb.top || strcmp(grp, appear_group(appear_at(i - 1)))) {   /* a group's first row (and the page's): its name on the right */
            float gw = text_w(grp, 0.35f);
            if (i > lb.top) rect(ROW_X - 8, y - 3, right, y - 2, 0x60FFE080, Z_FRONT);
            text(right - gw - 2, y + 4, grp, 0.35f, 0xFFC8A860);
            lim = right - gw - 10;
        }
        if (i == lb.pick) rect(ROW_X - 8, y - 2, right, y + PICK_H - 3, 0x50FFFFFF, Z_FRONT);
        if (i == cur) arrow(ROW_X + 4, y + 9, 1, col);
        w = text_w(nm, ROW_SC);
        sc = w > lim - ROW_X - 18 ? ROW_SC * (lim - ROW_X - 18) / w : ROW_SC;   /* a long name: narrower */
        text(ROW_X + 12, y + (ROW_SC - sc) * 14, nm, sc, col);
    }
    if (n > PICK_ROWS) {   /* the scroll bar: the page's share of the list, where it is */
        float y0 = ROW_Y0 - 2, h = PICK_ROWS * PICK_H - 1, th = h * PICK_ROWS / n, ty;
        if (th < 12) th = 12;
        ty = y0 + (h - th) * lb.top / pick_max_top();
        rect(PICK_BAR_X, y0, PICK_BAR_X + 5, y0 + h, 0x40FFFFFF, Z_FRONT);
        rect(PICK_BAR_X, ty, PICK_BAR_X + 5, ty + th, 0xC0FFFFFF, Z_FRONT + 0.001f);
    }
    if (lb.pick >= n) rect(ROW_X - 8, PICK_BACK_Y - 2, 634, PICK_BACK_Y + PICK_H - 3, 0x50FFFFFF, Z_FRONT);
    text(ROW_X, PICK_BACK_Y, "BACK", ROW_SC, lb.pick >= n ? 0xFFFFFFFF : 0xFFB0B0B0);
}

/* 0 stay, else activate()'s result */
static int lobby_input(void)
{
    ADV_WORK* ap = &AdvWork;
    unsigned int press = Pad[ap->PortId].press, rept = Pad[ap->PortId].Rept;
    unsigned int ok = AdvGetOkButton() | BTN_START, cancel = AdvGetCancelButton();
    int hit;
    menu_mouse_take(&mt);
    if (press || rept || mt.left || mt.right || mt.wheel || mt.moved || g_lobby_io.head != g_lobby_io.tail) StopAdvScreenSaver(0);
    if (lb.edit) {
        if (mt.left && row_at(mt.x, mt.y) != lb.row) edit_end(1);   /* a click elsewhere takes the text */
        else if (mt.right) edit_end(0);
        else edit_input(press, rept, ok, cancel);
        return 0;
    }
    if (lb.step == 6) { pick_input(press, rept, ok, cancel); return 0; }
    if (lb.step == 8) { if (cast_list_input(&mt, press, rept, ok, cancel)) { lb.step = 7; lb.row = R_CASTCUSTOM; } return 0; }
    g_lobby_io.tail = g_lobby_io.head;
    if (!row_selectable(lb.row)) {   /* the row went (hosting started or stopped): its successor */
        lb.row = lb.row == R_HOST || lb.row == R_KICK ? R_GO : lb.row == R_GO || lb.row == R_STOP || lb.row == R_COPY || lb.row == R_RCOPY ? R_HOST : lb.row;
        if (!row_selectable(lb.row)) lb.row = first_row();
    }
    hit = row_at(mt.x, mt.y);
    if (hit >= 0 && !row_selectable(hit)) hit = -1;
    if (hit >= 0 && (mt.moved || mt.left || mt.wheel) && hit != lb.row) { lb.row = hit; CallSystemSe(0, 2); }
    if (mt.wheel && hit >= 0 && has_value(hit)) change(hit, mt.wheel > 0 ? -1 : 1);
    if (mt.left && hit >= 0) {
        if (!has_value(hit)) return activate(hit);
        change(hit, mt.x < VAL_X + 14 ? -1 : 1);   /* the "<" arrow: back */
    }
    if (mt.right) return back();
    if (rept & BTN_UP) row_move(-1);
    else if (rept & BTN_DOWN) row_move(1);
    else if ((rept & BTN_LEFT) && has_value(lb.row)) change(lb.row, -1);
    else if ((rept & BTN_RIGHT) && has_value(lb.row)) change(lb.row, 1);
    else if (press & ok) return activate(lb.row);
    else if (press & cancel) return back();
    return 0;
}

/* the field being typed shows stars: a password, or an address with Hide IP addresses (coop_shown) */
static int edit_hidden(void) { return lb.edit == F_PW || (g_coop.hide_ip && (lb.edit == F_ADDR || lb.edit == F_SERVER)); }

static void draw_rows(void)
{
    int r;
    char label[64], val[96];
    text(ROW_X - 4, HEAD_Y, heading(), 0.5f, 0xFFFFE080);
    for (r = 0; r < R_N; r++) {
        int sel = r == lb.row, locked = row_locked(r);
        float y = row_y(r);
        unsigned int col = locked ? 0xFF707070 : sel ? 0xFFFFFFFF : 0xFFB0B0B0;
        if (!row_visible(r)) continue;
        row_text(r, label, val, sizeof(val));
        if (sel && edit_hidden()) memset(val, '*', strlen(val));
        if (sel) rect(ROW_X - 8, y - 2, 634, y + row_h() - 4, lb.edit ? 0x60406080 : 0x50FFFFFF, Z_FRONT);
        {   /* a long label next to a value: narrower (EXTRA ENEMIES); a long one alone: to the panel's edge */
            float lw = text_w(label, ROW_SC), room = (has_value(r) || val[0]) ? VAL_X - ROW_X - 8 : 634 - ROW_X - 4,
                  ls = lw > room ? ROW_SC * room / lw : ROW_SC;
            text(ROW_X, y + (ROW_SC - ls) * 14, label, ls, col);
            if (!val[0] && (r == R_ENEMYPG || r == R_EVENTPG || r == R_CASTPG)) arrow(ROW_X + lw * ls / ROW_SC + 16, y + 9, 1, col);   /* a page, no summary: the arrow after its name */
        }
        if (has_value(r)) {
            float w = text_w(val, ROW_SC), sc = w > 190 ? ROW_SC * 190 / w : ROW_SC;   /* long model names: narrower */
            if (sc < ROW_SC) w = 190;
            if (!locked) { arrow(VAL_X, y + 9, -1, col); arrow(VAL_X + 14 + w + 12, y + 9, 1, col); }
            text(VAL_X + 12, y + (ROW_SC - sc) * 14, val, sc, col);
        } else if (val[0] || lb.edit) {
            int page = r == R_ENEMYPG || r == R_EVENTPG || r == R_CASTPG || r == R_CHAR || r == R_CASTCUSTOM;   /* opens a page: an arrow after it */
            float lim = page ? 196.0f : 220.0f, w = text_w(val, ROW_SC), sc = w > lim ? ROW_SC * lim / w : ROW_SC;   /* a long address: narrower */
            text(VAL_X, y + (ROW_SC - sc) * 14, val, sc, col);
            if (page) arrow(VAL_X + (sc < ROW_SC ? lim : w) + 16, y + 9, 1, col);
            if (sel && lb.edit && (lb.spin & 16)) {   /* caret */
                char pre[128];
                snprintf(pre, sizeof(pre), "%.*s", lb.caret, lb.buf);
                if (edit_hidden()) memset(pre, '*', strlen(pre));
                rect(VAL_X + text_w(pre, sc), y, VAL_X + text_w(pre, sc) + 2, y + 18, 0xFFFFFFFF, Z_FRONT + 0.01f);
            }
        }
    }
}

/* under the rows: a message or the help line, the status, and while hosting what the partner needs */
static void draw_info(void)
{
    PortmapStatus pm;
    char line[200];
    float y = 390;
    unsigned int col = 0xFFC8C8C8;
    rect(0, 384, 640, 480, 0x60000000, Z_FRONT);
    if (lb.msg_t > 0) { lb.msg_t--; text(24, y, lb.msg, 0.55f, 0xFFFFE080); y += 17; }
    else y += 15 * text_wrap(24, y, 584, lb.step == 8 ? cast_list_help() : row_help(lb.row), 0.5f, 0xFFA0D0FF, 2);
    /* without a session a status is why (busy port, the host left): highlighted */
    y += 16 * text_wrap(24, y, 592, g_coop.status[0] ? coop_shown(g_coop.status) : "Not connected", 0.52f, !session_on() && g_coop.status[0] ? 0xFFFFC060 : col, 2);
    if (!hosting()) return;
    if (g_coop.room[0]) {
        snprintf(line, sizeof(line), "ROOM CODE %.4s-%.4s (via server)", g_coop.room, g_coop.room + 4);
        text(24, y, line, 0.52f, 0xFFFFFFFF); y += 16;
    }
    portmap_status(&pm);
    if (lb.mode || !(pm.local_ip[0] || pm.msg[0])) return;
    /* the address COPY JOIN ADDRESS gives (the router's port mapping runs as before; its status is only in the log) */
    if (pm.public_ip[0]) snprintf(line, sizeof(line), "ADDRESS %s:%d", pm.public_ip, pm.public_port);
    else snprintf(line, sizeof(line), "ADDRESS %s:%d", pm.local_ip[0] ? pm.local_ip : "?", g_coop.port);
    if (g_coop.code[0]) snprintf(line + strlen(line), sizeof(line) - strlen(line), "   CODE %.4s-%.4s", g_coop.code, g_coop.code + 4);
    text(24, y, coop_shown(line), 0.52f, col); y += 16;
    if (!pm.public_ip[0] && pm.state != PM_BUSY && g_coop.state != COOP_CONNECTED && y < 470)   /* no internet address: direct joins likely fail */
        text(24, y, "If your partner cannot connect, set CONNECT to Via server.", 0.48f, 0xFFA0A0A0);
}

/* our model and, at the title with us, the partner's, with their names */
static void draw_previews(void)
{
    int partner = g_coop.state == COOP_CONNECTED && !g_coop.peer_in_game, k;
    float sx[2] = { partner ? 92.0f : 120.0f, 222.0f };
    int played = g_coop.start >= 3 && lb.role == 0 && start_valid(g_coop.start) ? saves[save_at(g_coop.start - 3)].ply_id : 0;
    /* the chosen model (on the model page the one under the cursor) loads once it has stayed for a
       few frames (scrolling through the list) */
    int chr = lb.step == 6 && lb.pick < appear_count() ? appear_at(lb.pick) : g_coop.chr;
    if (chr != lb.want_chr) { lb.want_chr = chr; lb.chr_stable = 0; }
    if (++lb.chr_stable >= 8 || pv[0].chr < 0) pv_set(0, appear_played(lb.want_chr, played), played);   /* ("As played (cutscene)": that character's cutscene model) */
    pv_set(1, partner && g_coop.peer_lchr >= 0 && g_coop.peer_lchr <= COOP_CHR_MAX ? g_coop.peer_lchr : -1, 0);
    lb.spin += 1;
    {
        float h = pv_top(0) > pv_top(1) ? pv_top(0) : pv_top(1), want = 500.0f / 1.0f * (h + 3.0f) / 260.0f;
        if (want < 30) want = 30;
        if (want > 90) want = 90;
        cam_z = h > 0 ? cam_z + (want - cam_z) * 0.25f : 34.0f;
    }
    scene_begin();
    for (k = 0; k < 2; k++) {
        pv_pose(k, screen_to_world_x(sx[k]), (lb.spin * 160) & 0xFFFF);
        pv_draw(k);
    }
    for (k = 0; k < 2; k++) {
        NJS_POINT2 h;
        const char* nm = k ? (g_coop.peer_name[0] ? g_coop.peer_name : "PARTNER") : g_coop.name;
        if (pv[k].file < 0) continue;
        h = pv_head(k);
        text_c(h.x, h.y - 22, nm, 0.62f, k ? 0xFFFFE0A0 : 0xFFFFFFFF);
        if (k && g_coop.peer_note[0] && g_coop.peer_lobby) text_c(h.x, 354, g_coop.peer_note, 0.45f, 0xFFC0C0C0);
    }
    scene_end();
    if (g_coop.state == COOP_CONNECTED && g_coop.peer_in_game) {
        char t[64];
        snprintf(t, sizeof(t), "%s is playing", g_coop.peer_name[0] ? g_coop.peer_name : "Your partner");
        text_c(232, 210, t, 0.5f, 0xFFFFE0A0);
    } else if (!partner) text_c(232, 210, joining() ? "Waiting for the host" : hosting() ? "Waiting for a partner" : "", 0.5f, 0xFFA0A0A0);
    else if (!g_coop.peer_lobby) text_c(222, 372, "(in the title menu)", 0.45f, 0xFFA0A0A0);
}

static void lobby_open(void)
{
    memset(&lb, 0, sizeof(lb));
    lb.open = 1;
    lb.want_chr = -1;
    lb.items_sent = -2;
    lb.role = joining() ? 1 : 0;
    /* Automatic without a server is direct (docs/COOP.md "Connection modes") */
    lb.mode = !(g_coop.conn == COOP_CONN_DIRECT || (g_coop.conn == COOP_CONN_AUTO && !g_coop.server[0]));
    lb.step = session_on() ? 2 : 1;   /* a session hosted or joined through F3: its options */
    lb.row = first_row();
    scan_saves();
    font_load();
    if (!start_valid(g_coop.start)) g_coop.start = 0;
    if (!items_valid(g_coop.items)) g_coop.items = COOP_ITEMS_HOST;
    g_coop.lobby = 1;
    host_log("lobby: open\n");
}

/* keep: the game starts from the lobby (ours, or the host's joined from it); anything else (BACK,
   cancel, right click, a soft reset) closes the session too: hosting stops, the partner hears it */
static void lobby_close(int keep)
{
    int k;
    if (!lb.open) { font_release(); return; }   /* the title's (title_version) */
    if (!keep && (session_on() || g_coop.request)) {
        g_coop.request = COOP_REQ_STOP;
        host_log("lobby: left, the session closes\n");
    }
    if (lb.edit) edit_end(0);
    for (k = 0; k < 2; k++) pv_release(&pv[k], k);
    font_release();
    lb.open = 0;
    g_lobby_io.list_on = 0;
    g_coop.lobby = 0;
    g_coop.note[0] = 0;
    host_log("lobby: closed\n");
}

void pc_lobby_close(void)
{
    if (br_screen) pc_biorand_menu_close();
    if (im_screen) pc_import_menu_close();
    br_screen = im_screen = 0;
    lobby_close(0);
}

/* Adv_BioCvTitle mode 30, once per frame */
int pc_lobby(void)
{
    int r;
    char note[64];
    if (lb.open && !br_screen && coop_biorand_offer(NULL)) br_screen = 1;   /* a guest: the host's BioRand seed, offered there */
    if (im_screen) {   /* the IMPORT screen (import_menu.c): 1 back to the menu */
        title_font();
        if ((r = pc_import_menu())) im_screen = 0;
        return r;
    }
    if (br_screen) {   /* the BioRand screen (biorand_menu.c): 1 back to the menu, or to the lobby it was offered from */
        title_font();
        if ((r = pc_biorand_menu())) br_screen = 0;
        return lb.open ? 0 : r;
    }
    if (!lb.open) lobby_open();
    if (session_on()) lb.role = !g_coop.host;   /* F3 may host or join meanwhile */
    font_ok = font_ready();
    r = lobby_input();
    g_lobby_io.list_on = lb.step == 6 || lb.step == 8;
    /* what we chose, for the partner's screen; the joining items as they change */
    if (lb.role == 0) { start_label(note, sizeof(note), g_coop.start); snprintf(g_coop.note, sizeof(g_coop.note), "Starts: %.*s", (int)sizeof(g_coop.note) - 9, note); }
    else if (g_coop.items == COOP_ITEMS_HOST) snprintf(g_coop.note, sizeof(g_coop.note), "Items saved by host");
    else if (g_coop.items < 0) snprintf(g_coop.note, sizeof(g_coop.note), "New inventory");
    else { items_label(note, sizeof(note), g_coop.items); snprintf(g_coop.note, sizeof(g_coop.note), "Items: %.*s", (int)sizeof(g_coop.note) - 8, note); }
    if (joining() && lb.items_sent != g_coop.items) hand_items();

    AdvEasySetTextureList(0);
    AdvDwawOnePictureEx(0, 0xFFFFFFFF);   /* the title picture, darkened */
    rect(0, 0, 640, 480, 0xB0000000, Z_DIM);
    rect(304, 64, 640, 384, 0x70000000, Z_DIM + 0.0005f);
    mp_word(320.0f, 16.0f, 0xFFFFFFFF, 192, 1.0f);
    draw_previews();
    if (lb.step == 6) draw_models();
    else if (lb.step == 8) { text(ROW_X - 4, HEAD_Y, heading(), 0.5f, 0xFFFFE080); cast_list_draw(); }
    else draw_rows();
    draw_info();
    AdvEasySetTextureList(0);   /* the title's texture list again (the font and the models set theirs) */
    if (r) lobby_close(r != 1);
    return r;
}

/* bhSysCallFirstmovie, return code 8: the save chosen in the lobby, as LOAD GAME reads it */
void pc_lobby_load_save(void)
{
    memcpy(&sys->version, save_buf, (char*)&sys->save_end - (char*)&sys->version);
}

/* coop_glue.c: the host's game is joined from the title (its menu or the lobby). What the
   title does when it ends (Adv_BioCvTitle mode -1), before the game state is loaded. */
void pc_title_leave(void)
{
    ADV_WORK* ap = &AdvWork;
    lobby_close(1);
    StopVibrationEx();
    AdvEasyReleaseTexture();
    StopAdx(0);
    ap->DepthLevel = 0;
    ap->Mode = 0;
    ap->Active = 0;
    sys->ss_flg &= ~0x20000;
    g_menu_mouse.active = 0;
}

/* coop_glue.c: the guest starts the host's game (from the title, the lobby, the attract demo, a rejoin).
   The title's own game choices set AppMode 1; left at the title's 0, the in-game OPTION screen (Select)
   ended the title's way - AdvEasyReleaseAllTexture, no AdvPopRoomTexture - and the room was drawn
   without its textures (a crash on leaving it). */
void pc_adv_in_game(void) { AdvWork.AppMode = 1; }

/* Test aid (frame hand-off, pc/src/ps2/libgraph.c): RECVX_TEST_SAVE="gameframe:slot[,...]" writes the
   game in progress to memory card save slot `slot` (0..14) as a typewriter save holds it (the
   player's live position, angle, health, weapon and status first, as coop_glue.c send_blob does),
   at that frame of the game task. */
void lobby_frame(void)
{
    static const char* env = (const char*)-1;
    static int clock, prev = -1;
    static unsigned char keep[LOBBY_SAVE_BYTES];
    int f, slot, n = (int)((char*)&sys->save_end - (char*)&sys->version);
    static const char* join = (const char*)-1, *stop = (const char*)-1, *title = (const char*)-1;
    static unsigned int hand;
    hand++;
    { extern void crash_test(unsigned); crash_test(hand); }   /* RECVX_TEST_CRASH (log.c) */
    if (join == (const char*)-1) join = getenv("RECVX_TEST_JOIN");
    if (join && *join && hand >= (unsigned)atoi(join)) {   /* RECVX_TEST_JOIN="n": F3's Join at the n-th frame hand-off */
        host_log("test: join (F3) at hand-off %u\n", hand);
        g_coop.request = COOP_REQ_JOIN;
        join = strchr(join, ',') ? strchr(join, ',') + 1 : NULL;   /* "n,n,..." */
    }
    if (stop == (const char*)-1) stop = getenv("RECVX_TEST_STOP");
    if (stop && *stop && hand >= (unsigned)atoi(stop)) {   /* RECVX_TEST_STOP="n": F3's Disconnect at the n-th hand-off */
        host_log("test: disconnect (F3) at hand-off %u\n", hand);
        g_coop.request = COOP_REQ_STOP;
        stop = strchr(stop, ',') ? strchr(stop, ',') + 1 : NULL;
    }
    if (title == (const char*)-1) title = getenv("RECVX_TEST_TITLE");
    if (title && *title && hand >= (unsigned)atoi(title) && sys && (sys->tk_flg & 0x80)) {   /* RECVX_TEST_TITLE="n": as game over "No" */
        host_log("test: back to the title at hand-off %u\n", hand);
        bhReturnTitle();
        title = NULL;
    }
    if (env == (const char*)-1) env = getenv("RECVX_TEST_SAVE");
    if (!env || !sys || !plp || !(sys->tk_flg & 0x80) || (sys->ss_flg & 0x400000)) return;
    if (sys->gfrm_ct != prev) { prev = sys->gfrm_ct; clock++; }
    if (sscanf(env, "%d:%d", &f, &slot) != 2 || clock < f) return;
    memcpy(keep, &sys->version, n);
    sys->version = 10;
    sys->flr_no = plp->flr_no;
    sys->ply_stflg[(int)sys->ply_id] = plp->stflg & 0x78280000;
    sys->ply_pos.x = plp->px; sys->ply_pos.y = plp->py; sys->ply_pos.z = plp->pz;
    sys->ply_ang = plp->ay;
    sys->ply_wno[(int)sys->ply_id] = plp->wpnr_no;
    sys->ply_hp[(int)sys->ply_id] = plp->hp;
    coop_save_written(slot);   /* as the memory card's save: the host's partner items go with it */
    host_log("test: %s save file %d (stage %d room %d, items %08x %08x %08x)\n", lobby_write_save(slot, &sys->version, n) ? "wrote" : "cannot write",
             slot + 1, sys->stg_no, sys->rom_no, sys->itm[sys->ply_id * 16 + 2], sys->itm[sys->ply_id * 16 + 3], sys->itm[sys->ply_id * 16 + 4]);
    memcpy(&sys->version, keep, n);
    env = strchr(env, ',') ? strchr(env, ',') + 1 : NULL;   /* "f:slot,f:slot,..." */
}

/* the lobby's drawing helpers for the ENHANCED SETTINGS pages of the OPTION menu (pc/glue/options_glue.c) */
void pc_ui_text(float x, float y, const char* s, float sc, unsigned int argb) { font_ok = (sys->ss_flg & 0x40) != 0; text(x, y, s, sc, argb); }
float pc_ui_text_w(const char* s, float sc) { return text_w(s, sc); }
int pc_ui_text_wrap(float x, float y, float w, const char* s, float sc, unsigned int argb, int max_lines)
{
    font_ok = (sys->ss_flg & 0x40) != 0;
    return text_wrap(x, y, w, s, sc, argb, max_lines);
}
void pc_ui_rect(float x0, float y0, float x1, float y1, unsigned int argb, float z) { rect(x0, y0, x1, y1, argb, z); }
void pc_ui_arrow(float x, float y, int dir, unsigned int argb) { arrow(x, y, dir, argb); }
