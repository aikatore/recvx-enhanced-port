/* The BIORAND screen (docs/BIORAND_INGAME.md, S3): the title menu's BIORAND row (--dev, lobby_glue.c
 * pc_title_menu_init) opens it as Adv_BioCvTitle mode 30, as MULTIPLAYER opens the lobby. Drawn as the
 * lobby with its helpers (pc_ui_*). States:
 *   BS_MENU    the page: CHECK FOR A NEW BIORAND CLI, BACK (S4 puts the generator here)
 *   BS_ASK     a Yes/No question over it (A_DOWNLOAD with no CLI: No leaves; A_CHECK; A_NEWER)
 *   BS_WORK    the downloader's thread runs (pc/src/net/biorand_dl.h): its stage, a bar, MB, CANCEL
 *   BS_RESULT  a message (newest, installed, an error) and OK
 * Game thread. */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/adv.h"
#include "../../decomp/include/ps2/veronica/prog/padman.h"
#include "../../decomp/include/ps2/veronica/prog/sdfunc.h"
#include "../src/host/menu_mouse.h"
#include "../src/net/biorand_dl.h"
#include <stdio.h>
#include <string.h>

void host_log(const char* fmt, ...);
extern ADV_WORK AdvWork;
void pc_ui_text(float x, float y, const char* s, float sc, unsigned int argb);
float pc_ui_text_w(const char* s, float sc);
int  pc_ui_text_wrap(float x, float y, float w, const char* s, float sc, unsigned int argb, int max_lines);
void pc_ui_rect(float x0, float y0, float x1, float y1, unsigned int argb, float z);
void pc_title_word_br(float cx, float y, unsigned int col, unsigned int shadow);

enum { BTN_START = 0x800, BTN_UP = 0x1000, BTN_RIGHT = 0x2000, BTN_DOWN = 0x4000, BTN_LEFT = 0x8000 };
enum { BS_MENU, BS_ASK, BS_WORK, BS_RESULT };
enum { A_DOWNLOAD, A_CHECK, A_NEWER };
enum { R_CHECK, R_BACK, R_N };

/* depths (lobby_glue.c): the picture 0.01, its dimming 0.012; each kind of box its own, so "Smooth menus"
   never pairs a dialog with a row's highlight (view_glue.c pc_menu_tag hashes the depth) */
#define Z_DIM 0.012f
#define Z_ROW 0.3f
#define Z_BOX 0.31f
#define Z_BAR 0.32f
#define Z_FILL 0.33f
#define Z_BTN 0.34f

#define ROW_X 48.0f
#define ROW_Y0 104.0f
#define ROW_H 25.0f
#define ROW_SC 0.62f
#define BOX_X0 100.0f
#define BOX_X1 540.0f
#define BOX_Y0 150.0f
#define BOX_Y1 330.0f
#define BTN_Y 290.0f
#define BTN_W 100.0f

static struct {
    int open, state, ask, row, sel;     /* sel: BS_ASK 0 Yes 1 No */
    int install, cancelling, have;      /* the job installs (else checks); CANCEL pressed; the CLI is there */
    char msg[192];
    int err;
    MouseTake mt;
} bm;

static const char* const ask_text[] = {
    "BioRand is not installed. Download the BioRand CLI (nightly) from GitHub? About 7 MB. It is checked against GitHub's checksum.",
    "Check GitHub for a new BioRand CLI?",
    "A newer BioRand CLI is available. Download it? About 7 MB. It is checked against GitHub's checksum.",
};

static void ask(int a) { bm.state = BS_ASK; bm.ask = a; bm.sel = 1; }   /* No first */

static void result(const char* m, int err)
{
    snprintf(bm.msg, sizeof(bm.msg), "%s", m);
    bm.err = err;
    bm.state = BS_RESULT;
}

/* back to the page, or out when there is no CLI to show it for */
static int to_menu(void)
{
    bm.have = biorand_cli_present();
    bm.state = BS_MENU;
    return !bm.have;
}

static void work(int install)
{
    BrdlStatus s;
    bm.install = install;
    bm.cancelling = 0;
    if ((install ? biorand_cli_install() : biorand_cli_check_update())) { bm.state = BS_WORK; return; }
    biorand_cli_status(&s);
    result(s.state == BRDL_ERROR ? s.error : "The last download is still ending. Try again in a moment.", 1);
}

/* BS_WORK: the thread's end */
static int work_end(void)
{
    BrdlStatus s;
    biorand_cli_status(&s);
    if (s.state < BRDL_INSTALLED) return 0;
    host_log("biorand menu: %s ended in state %d\n", bm.install ? "install" : "check", s.state);
    switch (s.state) {
    case BRDL_INSTALLED: bm.have = 1; result("The BioRand CLI is installed.", 0); break;
    case BRDL_CURRENT: result("You have the newest BioRand CLI.", 0); break;
    case BRDL_NEWER: ask(A_NEWER); break;
    case BRDL_ERROR: result(s.error, 1); break;
    default: return to_menu();   /* cancelled */
    }
    return 0;
}

/* a choice taken: 1 leave the screen */
static int choose(int yes)
{
    CallSystemSe(0, yes ? 3 : 0);
    if (bm.state == BS_ASK) {
        if (yes) { work(bm.ask != A_CHECK); return 0; }
        return bm.ask == A_DOWNLOAD ? 1 : to_menu();
    }
    if (bm.state == BS_RESULT) return to_menu();
    if (bm.state == BS_WORK) { if (!bm.cancelling) biorand_cli_cancel(); bm.cancelling = 1; return 0; }
    if (!yes || bm.row == R_BACK) return 1;
    ask(A_CHECK);
    return 0;
}

/* the dialog's buttons: BS_ASK YES and NO, else one (CANCEL, OK) */
static int buttons(void) { return bm.state == BS_ASK ? 2 : 1; }
static float btn_x(int i) { return buttons() == 2 ? 320.0f + (i ? 20.0f : -20.0f - BTN_W) : 320.0f - BTN_W / 2; }
static int btn_at(float x, float y)
{
    int i;
    for (i = 0; i < buttons(); i++)
        if (x >= btn_x(i) && x < btn_x(i) + BTN_W && y >= BTN_Y - 4 && y < BTN_Y + 22) return i;
    return -1;
}
static int row_at(float x, float y)
{
    int r = (int)((y - ROW_Y0 + 3) / ROW_H);
    return x >= ROW_X - 8 && x < 600 && y >= ROW_Y0 - 3 && r >= 0 && r < R_N ? r : -1;
}

/* 1 leave */
static int input(void)
{
    ADV_WORK* ap = &AdvWork;
    unsigned int press = Pad[ap->PortId].press, rept = Pad[ap->PortId].Rept;
    unsigned int ok = AdvGetOkButton() | BTN_START, cancel = AdvGetCancelButton();
    MouseTake* m = &bm.mt;
    int hit;
    menu_mouse_take(m);
    if (press || rept || m->left || m->right || m->moved || bm.state == BS_WORK) StopAdvScreenSaver(0);   /* (a download is no idle screen) */
    if (bm.state == BS_MENU) {
        hit = row_at(m->x, m->y);
        if (hit >= 0 && (m->moved || m->left) && hit != bm.row) { bm.row = hit; CallSystemSe(0, 2); }
        if (m->left && hit >= 0) return choose(1);
        if (m->right || (press & cancel)) return choose(0);   /* (back, whichever row) */
        if (rept & (BTN_UP | BTN_DOWN)) { bm.row = (bm.row + (rept & BTN_UP ? R_N - 1 : 1)) % R_N; CallSystemSe(0, 2); }
        else if (press & ok) return choose(1);
        return 0;
    }
    hit = btn_at(m->x, m->y);
    if (bm.state == BS_ASK) {
        if (hit >= 0 && (m->moved || m->left) && hit != bm.sel) { bm.sel = hit; CallSystemSe(0, 2); }
        if (m->left && hit >= 0) return choose(!bm.sel);
        if (m->right || (press & cancel)) return choose(0);
        if (rept & (BTN_LEFT | BTN_RIGHT | BTN_UP | BTN_DOWN)) { bm.sel ^= 1; CallSystemSe(0, 2); }
        else if (press & ok) return choose(!bm.sel);
        return 0;
    }
    if ((m->left && hit >= 0) || m->right || (press & (ok | cancel))) return choose(bm.state == BS_RESULT);   /* CANCEL, OK */
    return 0;
}

static void box(void)
{
    pc_ui_rect(BOX_X0, BOX_Y0, BOX_X1, BOX_Y1, 0xF0181820, Z_BOX);
}

static void button(int i, const char* s, int sel)
{
    float x = btn_x(i);
    if (sel) pc_ui_rect(x, BTN_Y - 4, x + BTN_W, BTN_Y + 22, 0x50FFFFFF, Z_BTN);
    pc_ui_text(x + (BTN_W - pc_ui_text_w(s, ROW_SC)) / 2, BTN_Y, s, ROW_SC, sel ? 0xFFFFFFFF : 0xFFB0B0B0);
}

static void draw_work(void)
{
    static const char* const stage[] = { "", "Looking up the newest BioRand CLI on GitHub...", "Downloading the BioRand CLI...",
                                         "Checking the download (SHA-256)...", "Installing..." };
    BrdlStatus s;
    char line[64];
    float f = 0;
    biorand_cli_status(&s);
    box();
    pc_ui_text(BOX_X0 + 20, BOX_Y0 + 16, bm.install ? "BIORAND CLI - DOWNLOAD" : "BIORAND CLI - CHECK FOR AN UPDATE", 0.5f, 0xFFFFE080);
    pc_ui_text_wrap(BOX_X0 + 20, BOX_Y0 + 48, BOX_X1 - BOX_X0 - 40, bm.cancelling ? "Cancelling..." : s.state < BRDL_INSTALLED ? stage[s.state] : "",
                    0.55f, 0xFFFFFFFF, 2);
    if (bm.install) {
        if (s.state >= BRDL_VERIFY) f = 1;
        else if (s.total > 0) f = s.done >= s.total ? 1.0f : (float)s.done / s.total;
        {   /* the done part and the rest side by side; at half pixels: the renderer's rows of a long flat quad's
                       two triangles can differ by one (a step in an opaque bar), at most the last row this way */
            float x = BOX_X0 + 20 + (BOX_X1 - BOX_X0 - 40) * f;
            if (f > 0) pc_ui_rect(BOX_X0 + 20, 228.5f, x, 244.5f, 0xFFFFE080, Z_FILL);
            if (f < 1) pc_ui_rect(x, 228.5f, BOX_X1 - 20, 244.5f, 0xFF303038, Z_BAR);
        }
        if (s.total > 0) {
            snprintf(line, sizeof(line), "%.1f / %.1f MB", s.done / 1048576.0, s.total / 1048576.0);
            pc_ui_text(BOX_X1 - 20 - pc_ui_text_w(line, 0.5f), 252, line, 0.5f, 0xFFC8C8C8);
        }
    }
    button(0, "CANCEL", 1);
}

static void draw_page(void)
{
    static const char* const rows[R_N] = { "CHECK FOR A NEW BIORAND CLI", "BACK" };
    static const char* const help[R_N] = { "Asks GitHub whether a newer BioRand CLI (the nightly of biorand/classic) is out, and offers to download it.",
                                           "Back to the title menu." };
    int r;
    pc_ui_text(ROW_X - 4, 74, "RANDOMIZER", 0.5f, 0xFFFFE080);
    if (!bm.have) return;
    for (r = 0; r < R_N; r++) {
        float y = ROW_Y0 + r * ROW_H;
        int sel = bm.state == BS_MENU && r == bm.row;
        if (sel) pc_ui_rect(ROW_X - 8, y - 2, 600, y + ROW_H - 4, 0x50FFFFFF, Z_ROW);
        pc_ui_text(ROW_X, y, rows[r], ROW_SC, sel ? 0xFFFFFFFF : 0xFFB0B0B0);
    }
    if (bm.state == BS_MENU) pc_ui_text_wrap(ROW_X, 180, 540, "The BioRand generator comes here in a later version.", 0.5f, 0xFFA0A0A0, 2);   /* (text is drawn over any box) */
    pc_ui_rect(0, 384, 640, 480, 0x60000000, Z_ROW);
    if (bm.state == BS_MENU) pc_ui_text_wrap(24, 390, 584, help[bm.row], 0.5f, 0xFFA0D0FF, 2);
}

void pc_biorand_menu_close(void)
{
    if (!bm.open) return;
    if (bm.state == BS_WORK) biorand_cli_cancel();   /* the title ends (a soft reset): the download with it */
    bm.open = 0;
    host_log("biorand menu: closed\n");
}

/* Adv_BioCvTitle mode 30 while the BioRand screen is up (lobby_glue.c pc_lobby): 1 back to the title menu */
int pc_biorand_menu(void)
{
    int r = 0;
    if (!bm.open) {
        memset(&bm, 0, sizeof(bm));
        bm.open = 1;
        if (!(bm.have = biorand_cli_present())) ask(A_DOWNLOAD);
        host_log("biorand menu: open, the CLI is %s\n", bm.have ? "there" : "missing");
    }
    if (bm.state == BS_WORK) r = work_end();
    if (!r) r = input();
    AdvEasySetTextureList(0);
    AdvDwawOnePictureEx(0, 0xFFFFFFFF);   /* the title picture, darkened, as the lobby */
    pc_ui_rect(0, 0, 640, 480, 0xB0000000, Z_DIM);
    pc_ui_rect(24, 64, 616, 384, 0x70000000, Z_DIM + 0.0005f);
    pc_title_word_br(320.0f, 16.0f, 0xFFFFFFFF, 192);
    draw_page();
    if (bm.state == BS_ASK) {
        box();
        pc_ui_text_wrap(BOX_X0 + 20, BOX_Y0 + 20, BOX_X1 - BOX_X0 - 40, ask_text[bm.ask], 0.55f, 0xFFFFFFFF, 5);
        button(0, "YES", bm.sel == 0);
        button(1, "NO", bm.sel == 1);
    } else if (bm.state == BS_WORK) draw_work();
    else if (bm.state == BS_RESULT) {
        box();
        pc_ui_text(BOX_X0 + 20, BOX_Y0 + 16, bm.err ? "BIORAND CLI - ERROR" : "BIORAND CLI", 0.5f, 0xFFFFE080);
        pc_ui_text_wrap(BOX_X0 + 20, BOX_Y0 + 48, BOX_X1 - BOX_X0 - 40, bm.msg, 0.55f, bm.err ? 0xFFFFC060 : 0xFFFFFFFF, 4);
        button(0, "OK", 1);
    }
    AdvEasySetTextureList(0);   /* the title's texture list again (the font set its own) */
    if (r) pc_biorand_menu_close();
    return r;
}
