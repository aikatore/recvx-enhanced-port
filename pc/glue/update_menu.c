/* The update dialog (pc/src/net/update.h; docs/USER_GUIDE.md "Updating"): when the version check found a newer
 * release on GitHub with this build's archive, "Check For Updates" is Ask to Install and that version is not skipped,
 * the title menu opens it once per run as Adv_BioCvTitle mode 30 (lobby_glue.c pc_title_mouse, as the BIORAND
 * screen): the title picture darkened under RETURN TO TITLE's box (options_glue.c pc_ui_dialog). States:
 *   U_ASK   the versions; INSTALL, LATER (preselected) or SKIP THIS VERSION (recvx.ini update_skip: not asked
 *           again until a newer one is out)
 *   U_WORK  the download, its check and the install (update_install): the stage, a bar, MB, CANCEL
 *   U_DONE  RESTART NOW (update_restart: the new executable with the same arguments) or LATER
 *   U_FAIL  the error (the old files are kept): OPEN RELEASE PAGE (the browser) or CLOSE
 * Game thread. */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/adv.h"
#include "../../decomp/include/ps2/veronica/prog/padman.h"
#include "../../decomp/include/ps2/veronica/prog/sdfunc.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_NaDraw.h"
#include "../src/host/menu_mouse.h"
#include "../src/host/lang.h"
#include "../src/host/settings.h"
#include "../src/net/update.h"
#include <stdio.h>
#include <string.h>

void host_log(const char* fmt, ...);
extern ADV_WORK AdvWork;
void pc_ui_text(float x, float y, const char* s, float sc, unsigned int argb);
float pc_ui_text_w(const char* s, float sc);
void pc_ui_rect(float x0, float y0, float x1, float y1, unsigned int argb, float z);
void pc_ui_dialog_place(const char* l1, const char* l2, float sc2, int lines, float extra, float* y0, float* y1);   /* options_glue.c */
void pc_ui_dialog_btns(float y0, float y1, float z, const char* l1, const char* l2, float sc2, unsigned int col2, int lines,
                       const char* const* b, int n, int sel);
int pc_ui_dialog_btns_hit(float y1, const char* const* b, int n, float x, float y);

enum { BTN_START = 0x800, BTN_UP = 0x1000, BTN_RIGHT = 0x2000, BTN_DOWN = 0x4000, BTN_LEFT = 0x8000 };
enum { U_ASK, U_WORK, U_DONE, U_FAIL };

/* (the BIORAND screen's depths and progress layout, biorand_menu.c) */
#define Z_DIM 0.012f
#define Z_BOX 0.303f
#define Z_BAR 0.32f
#define Z_FILL 0.33f
#define BOX_X0 112.0f
#define BOX_X1 528.0f
#define WORK_TEXT 34.0f
#define WORK_BAR 30.0f

static struct {
    int open, state, sel, cancelling;
    char ver[32];                       /* the offered version ("v0.7.2") */
    char msg[600];
    MouseTake mt;
} um;

/* lobby_glue.c pc_title_mouse, at the title menu: 1 when the dialog is to open (update_offer) */
int pc_update_menu_offer(void) { return update_offer(um.ver, sizeof(um.ver)); }

/* the state's answers, translated (pc_ui_dialog_btns: made smaller when they do not fit); their number in *n */
static const char* const* btns(int* n)
{
    static const char* const en[4][3] = { { "INSTALL", "LATER", "SKIP THIS VERSION" }, { "CANCEL" }, { "RESTART NOW", "LATER" },
                                          { "OPEN RELEASE PAGE", "CLOSE" } };
    static const char* b[3];
    *n = um.state == U_ASK ? 3 : um.state == U_WORK ? 1 : 2;
    for (int i = 0; i < *n; i++) b[i] = tr(en[um.state][i]);
    return b;
}

static const char* head(void)
{
    return um.state == U_DONE ? "UPDATE INSTALLED" : um.state == U_FAIL ? "UPDATE FAILED" : "GAME UPDATE";
}

static const char* text(void)
{
    BrdlStatus s;
    if (um.state == U_ASK)
        snprintf(um.msg, sizeof(um.msg), tr("A new version is available: %s (you have %s). Download and install it now?"),
                 um.ver + 1, update_installed() + 1);
    else if (um.state == U_DONE) snprintf(um.msg, sizeof(um.msg), tr("Version %s is installed. Restart the game now to use it?"), um.ver + 1);
    else if (um.state == U_WORK) {
        update_install_status(&s);
        if (um.cancelling) return "Cancelling...";
        if (s.state == BRDL_VERIFY) return "Verifying download...";
        if (s.state == BRDL_EXTRACT) return "Installing...";
        snprintf(um.msg, sizeof(um.msg), tr("Downloading version %s..."), um.ver + 1);
    }
    return um.msg;   /* U_FAIL: set by work_end */
}

static void box(float* y0, float* y1)
{
    if (um.state == U_WORK) pc_ui_dialog_place(head(), NULL, 0.55f, 0, WORK_TEXT + WORK_BAR, y0, y1);
    else pc_ui_dialog_place(head(), text(), 0.55f, 5, 0, y0, y1);
}

/* U_WORK: the install's end */
static int work_end(void)
{
    BrdlStatus s;
    update_install_status(&s);
    if (s.state < BRDL_INSTALLED) return 0;
    host_log("update menu: install ended in state %d\n", s.state);
    if (s.state == BRDL_CANCELLED) return 1;   /* back to the title menu */
    um.sel = 0;
    if (s.state == BRDL_INSTALLED) um.state = U_DONE;
    else {
        snprintf(um.msg, sizeof(um.msg), tr("%s Open the release page in your browser to download it yourself?"), s.error);
        um.state = U_FAIL;
    }
    return 0;
}

/* answer i (-1: cancel or a right click): 1 back to the title menu */
static int choose(int i)
{
    CallSystemSe(0, i == 0 && um.state != U_WORK ? 3 : 0);
    switch (um.state) {
    case U_ASK:
        if (i == 0) {
            um.cancelling = 0;
            if (update_install()) { um.state = U_WORK; return 0; }
            return 1;
        }
        if (i == 2) {
            snprintf(g_update_skip, sizeof(g_update_skip), "%s", um.ver);
            settings_save_later();
            host_log("update menu: %s skipped\n", um.ver);
        }
        return 1;
    case U_WORK:
        update_install_cancel();
        um.cancelling = 1;
        return 0;
    case U_DONE:
        if (i == 0) { update_restart(); return 0; }   /* (the window closes) */
        return 1;
    default:
        if (i == 0) update_open_page();
        return 1;
    }
}

/* 1 back to the title menu */
static int input(void)
{
    ADV_WORK* ap = &AdvWork;
    unsigned int press = Pad[ap->PortId].press, rept = Pad[ap->PortId].Rept;
    unsigned int ok = AdvGetOkButton() | BTN_START, cancel = AdvGetCancelButton();
    MouseTake* m = &um.mt;
    int n, hit;
    const char* const* b = btns(&n);
    float y0, y1;
    menu_mouse_take(m);
    if (press || rept || m->left || m->right || m->moved || um.state == U_WORK) StopAdvScreenSaver(0);
    box(&y0, &y1);
    hit = pc_ui_dialog_btns_hit(y1, b, n, m->x, m->y);
    if (um.state == U_WORK && um.cancelling) return 0;
    if (hit >= 0 && (m->moved || m->left) && hit != um.sel) { um.sel = hit; CallSystemSe(0, 2); }
    if (n > 1 && (rept & (BTN_LEFT | BTN_UP))) { um.sel = (um.sel + n - 1) % n; CallSystemSe(0, 2); }
    else if (n > 1 && (rept & (BTN_RIGHT | BTN_DOWN))) { um.sel = (um.sel + 1) % n; CallSystemSe(0, 2); }
    else if ((m->left && hit >= 0) || (press & ok)) return choose(um.sel);
    else if (m->right || (press & cancel)) return choose(-1);
    return 0;
}

static void draw_work(float y0, float y1)
{
    BrdlStatus s;
    char line[64] = "";
    float f = 0, x, yb;
    int n;
    const char* const* b = btns(&n);
    update_install_status(&s);
    pc_ui_dialog_btns(y0, y1, Z_BOX, head(), text(), 0.55f, 0xFFC0C0C0, 2, b, n, um.cancelling ? -1 : 0);
    if (s.state >= BRDL_VERIFY) f = 1;
    else if (s.total > 0) f = s.done >= s.total ? 1.0f : (float)s.done / s.total;
    if (s.total > 0) snprintf(line, sizeof(line), tr("%.1f / %.1f MB"), s.done / 1048576.0, s.total / 1048576.0);
    x = BOX_X0 + 20 + (BOX_X1 - BOX_X0 - 40) * f;   /* as the BIORAND screen's bar: at half pixels */
    yb = y0 + 54 + WORK_TEXT + 2.5f;
    if (f > 0) pc_ui_rect(BOX_X0 + 20, yb, x, yb + 12, 0xFFFFE080, Z_FILL);
    if (f < 1) pc_ui_rect(x, yb, BOX_X1 - 20, yb + 12, 0xFF303038, Z_BAR);
    pc_ui_text(BOX_X1 - 20 - pc_ui_text_w(line, 0.5f), yb + 15.5f, line, 0.5f, 0xFFC8C8C8);
}

void pc_update_menu_close(void)
{
    if (!um.open) return;
    if (um.state == U_WORK) update_install_cancel();   /* the title ends (a soft reset): the download with it */
    um.open = 0;
    host_log("update menu: closed\n");
}

/* Adv_BioCvTitle mode 30 while the dialog is up (lobby_glue.c pc_lobby): 1 back to the title menu */
int pc_update_menu(void)
{
    float y0, y1;
    int r = 0;
    if (!um.open) {
        um.open = 1;
        um.state = U_ASK;
        um.sel = 1;   /* LATER first: a Start pressed through the title does not install */
        um.cancelling = 0;
        host_log("update menu: open, %s offered\n", um.ver);
    }
    if (um.state == U_WORK) r = work_end();
    if (!r) r = input();
    AdvEasySetTextureList(0);
    AdvDwawOnePictureEx(0, 0xFFFFFFFF);   /* the title picture, darkened, as the lobby */
    pc_ui_rect(0, 0, 640, 480, 0xB0000000, Z_DIM);
    box(&y0, &y1);
    if (um.state == U_WORK) draw_work(y0, y1);
    else {
        int n;
        const char* const* b = btns(&n);
        pc_ui_dialog_btns(y0, y1, Z_BOX, head(), text(), 0.55f, um.state == U_FAIL ? 0xFFFFC060 : um.state == U_ASK ? 0xFFFFFFFF : 0xFFC0C0C0, 5, b, n, um.sel);
    }
    AdvEasySetTextureList(0);   /* the title's texture list again (the font set its own) */
    if (r) pc_update_menu_close();
    return r;
}
