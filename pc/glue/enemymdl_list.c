/* "Enemy Models" (docs/ENEMIES.md "Enemy models"): a row per enemy category (Zombies, Anatomist Zombie, Hunter, ...) with
 * its model between arrows, BACK below. Left / right step through the choices (Original, the random groups, every model),
 * L1 / R1 jump to the previous / next group of them, Confirm steps on. Drawn as the enemy rates' list (rate_list.c), with
 * the menus' helpers (pc_ui_*), by GAME SETUP > CHARACTERS (ui_rows.c) and the old ENHANCED SETTINGS GAMEPLAY page
 * (options_glue.c), each at its own place. It edits g_emodel (recvx.ini enemy_model_<category>; a co-op host's go to the
 * guest, coop_glue.c EV_EMODEL); read-only, it shows those in effect (a co-op guest: the host's). Game thread. */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/sdfunc.h"
#include "../src/host/actors.h"
#include "../src/host/enemymdl.h"
#include "../src/host/lobby.h"
#include "../src/host/menu_mouse.h"
#include "../src/host/settings.h"
#include "../src/host/lang.h"
#include <stdio.h>
#include <string.h>

void pc_ui_text(float x, float y, const char* s, float sc, unsigned int argb);
float pc_ui_text_w(const char* s, float sc);
void pc_ui_rect(float x0, float y0, float x1, float y1, unsigned int argb, float z);
void pc_ui_arrow(float x, float y, int dir, unsigned int argb);
void host_log(const char* fmt, ...);

enum { BTN_L1 = 0x4, BTN_R1 = 0x8, BTN_UP = 0x1000, BTN_RIGHT = 0x2000, BTN_DOWN = 0x4000, BTN_LEFT = 0x8000 };   /* Pad[] bits */
#define GAP 6.0f
#define VAL_X 0.42f                     /* the value's column: this share of the width from the left */
#define Z_SEL 0.3f
#define CHOICES (1 + EM_RANDOM_END + ACTOR_MODELS + ACTOR_IMPORT_MAX)

static struct { int cur, ro; float x0, x1, y0, h, sc; } el;   /* cur: a category, or EM_CATS (BACK) */
static int ch[CHOICES], nch;

static const int* vals(void) { return el.ro ? emodel_now() : g_emodel; }
static float back_y(void) { return el.y0 + EM_CATS * el.h + GAP; }
static float val_x(void) { return el.x0 + (el.x1 - el.x0) * VAL_X; }

void emodel_list_open(int ro, float x0, float x1, float y0, float h, float sc)
{
    memset(&el, 0, sizeof(el));
    el.ro = ro;
    el.x0 = x0; el.x1 = x1; el.y0 = y0; el.h = h; el.sc = sc;
    CallSystemSe(0, 3);
}

/* category c's value stepped by d choices (group: to the first of the next / previous group) */
static void step(int c, int d, int group)
{
    int i, v = g_emodel[c];
    char nb[96];
    if (el.ro) { CallSystemSe(0, 1); return; }
    if ((nch = emodel_choices(ch, CHOICES)) <= 0) return;   /* (Original is always listed: never) */
    for (i = 0; i < nch && ch[i] != v; i++);
    if (i == nch) i = 0;   /* (a value no longer listed: from Original) */
    if (group) {
        const char* g = emodel_choice_group(ch[i]);
        int j = i;
        do j = (j + d + nch) % nch; while (j != i && !strcmp(emodel_choice_group(ch[j]), g));
        if (d < 0) { g = emodel_choice_group(ch[j]); while (j > 0 && !strcmp(emodel_choice_group(ch[j - 1]), g)) j--; }   /* (its first) */
        i = j;
    } else i = (i + d + nch) % nch;
    g_emodel[c] = ch[i];
    settings_save_later();
    host_log("enemymdl: %s = %s\n", emodel_cat_name(c), emodel_choice_name(ch[i], nb, sizeof(nb)));
    CallSystemSe(0, 2);
}

static void to(int c)
{
    c = (c % (EM_CATS + 1) + EM_CATS + 1) % (EM_CATS + 1);
    if (c != el.cur) CallSystemSe(0, 2);
    el.cur = c;
}

static int at(float x, float y)
{
    int i;
    if (x < el.x0 - 8 || x > el.x1) return -1;
    if (y >= back_y() - 3 && y < back_y() + el.h - 3) return EM_CATS;
    if (y < el.y0 - 3) return -1;
    i = (int)((y - el.y0 + 3) / el.h);
    return i < EM_CATS ? i : -1;
}

/* 1: BACK */
int emodel_list_input(const MouseTake* m, unsigned int press, unsigned int rept, unsigned int ok, unsigned int cancel)
{
    int hit;
    while (g_lobby_io.tail != __atomic_load_n(&g_lobby_io.head, __ATOMIC_ACQUIRE)) g_lobby_io.tail++;   /* (Page Up / Down: nothing to page) */
    if (m->wheel) { to(el.cur - m->wheel); return 0; }
    hit = at(m->x, m->y);
    if (hit >= 0 && (m->moved || m->left) && hit != el.cur) { el.cur = hit; CallSystemSe(0, 2); }
    if (m->left && hit == EM_CATS) { CallSystemSe(0, 0); return 1; }
    if (m->left && hit >= 0) { step(hit, m->x < val_x() + 10 ? -1 : 1, 0); return 0; }   /* (the left arrow, else on) */
    if (m->right || (press & cancel)) { CallSystemSe(0, 0); return 1; }
    if (rept & BTN_UP) to(el.cur - 1);
    else if (rept & BTN_DOWN) to(el.cur + 1);
    else if ((rept & (BTN_L1 | BTN_R1)) && el.cur < EM_CATS) step(el.cur, (rept & BTN_L1) ? -1 : 1, 1);
    else if ((rept & (BTN_LEFT | BTN_RIGHT)) && el.cur < EM_CATS) step(el.cur, (rept & BTN_LEFT) ? -1 : 1, 0);
    else if (press & ok) {
        if (el.cur >= EM_CATS) { CallSystemSe(0, 0); return 1; }
        step(el.cur, 1, 0);
    }
    return 0;
}

void emodel_list_draw(void)
{
    const int* v = vals();
    int c;
    float vx = val_x(), y, vw = el.x1 - vx - 20;
    char nb[96];
    for (c = 0; c < EM_CATS; c++) {
        int sel = el.cur == c;
        unsigned int col = sel ? 0xFFFFFFFF : v[c] ? 0xFFD0D0D0 : 0xFF909090, ac = el.ro ? 0xFF606060 : col;
        const char* nm = tr(emodel_choice_name(v[c], nb, sizeof(nb)));
        float w = pc_ui_text_w(nm, el.sc), sc = w > vw ? el.sc * vw / w : el.sc;
        y = el.y0 + c * el.h;
        if (sel) pc_ui_rect(el.x0 - 8, y - 2, el.x1, y + el.h - 3, 0x50FFFFFF, Z_SEL + c * 0.0004f);
        pc_ui_text(el.x0, y, tr(emodel_cat_name(c)), el.sc, col);
        pc_ui_arrow(vx - 4, y + 9, -1, ac);
        pc_ui_arrow(el.x1 - 6, y + 9, 1, ac);
        pc_ui_text(vx + 8, y + (el.sc - sc) * 14, nm, sc, col);   /* (a long name: narrower) */
    }
    y = back_y();
    if (el.cur >= EM_CATS) pc_ui_rect(el.x0 - 8, y - 2, el.x1, y + el.h - 3, 0x50FFFFFF, Z_SEL + 0.02f);
    pc_ui_text(el.x0, y, tr("BACK"), el.sc, el.cur >= EM_CATS ? 0xFFFFFFFF : 0xFFB0B0B0);
}

const char* emodel_list_help(void)
{
    if (el.cur >= EM_CATS) return el.ro ? "Back to the rows." : "Back. Your choices are saved.";
    if (el.ro) return "The host's enemy models. Only the host can change them.";
    if (el.cur == EM_ZOMBIES)
        return "Every zombie look at once. A random group gives each zombie one of its models. Left/right: model; L1/R1: next group. From the next room.";
    return "What this enemy looks like. It keeps its own moves, hits, grabs and sounds. Left/right: model; L1/R1: next group. From the next room.";
}

/* "Original", "Zombies: Nemesis", "3 changed" */
const char* emodel_list_summary(const int* v, char* buf, int n)
{
    int c, k = 0, last = 0;
    char nb[96];
    for (c = 0; c < EM_CATS; c++) if (v[c]) { k++; last = c; }
    if (!k) snprintf(buf, (size_t)n, "%s", tr("Original"));
    else if (k == 1) snprintf(buf, (size_t)n, "%s: %s", tr(emodel_cat_name(last)), tr(emodel_choice_name(v[last], nb, sizeof(nb))));
    else snprintf(buf, (size_t)n, tr("%d changed"), k);
    return buf;
}
