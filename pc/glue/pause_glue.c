/* The new menu in the game's OPTION task (docs/MENUS.md): at the title OPTION opens SETTINGS (level 4, the
 * row screen ui_rows.c), in a game it opens the PAUSE menu in level 0's place. Called from adv.c (Adv_GameOptionScreen,
 * DisplayOptionPlateLevel0, DisplayOptionBg, the sub-screens' way back) and options_glue.c (level 4, closing).
 * With Legacy Menu (or --legacy-menu) none of this runs: pc_new_menu_on() is 0 and the old OPTION screen is unchanged.
 *
 * PAUSE: RESUME, Camera (Fixed / Over Shoulder, the F5 setting), SETTINGS, GAME SETUP (this game's rules: the same row
 * screen), CO-OP (in a session: the F3 window), SPOILER LOG (the F8 screen, a BioRand game outside co-op), CHEATS AND
 * TOOLS (the same row screen; Advanced Tools opens the F2 window), RETURN TO TITLE (the old question). Select / View
 * (Backspace) opens it as it opened OPTION; with the Modern third-person controller layout its options button (Start)
 * opens and closes it; Cancel, Select or the controller's Start resume. Co-op does not pause: the header says so.
 * The background is the OPTION picture darkened (the task has pushed the room's textures, so no game frame is left).
 * Game thread. */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/adv.h"
#include "../../decomp/include/ps2/veronica/prog/padman.h"
#include "../../decomp/include/ps2/veronica/prog/sdfunc.h"
#include "../src/host/settings.h"
#include "../src/host/menu_layout.h"
#include "../src/host/bindings.h"
#include "../src/host/menu_mouse.h"
#include "../src/host/lang.h"
#include "../src/host/tps.h"
#include "../src/host/coop.h"
#include "../src/host/biorand.h"
#include "../src/host/enemies.h"
#include "../src/host/cast.h"
#include <stdio.h>
#include <string.h>

extern ADV_WORK AdvWork;
extern QUAD Qtex[16];
void host_log(const char* fmt, ...);
void pc_ui_text(float x, float y, const char* s, float sc, unsigned int argb);
float pc_ui_text_w(const char* s, float sc);
int pc_ui_text_wrap(float x, float y, float w, const char* s, float sc, unsigned int argb, int max_lines);
int pc_ui_text_wrap_r(float x, float y, float w, const char* s, float sc, unsigned int argb, int max_lines);
void pc_ui_rect(float x0, float y0, float x1, float y1, unsigned int argb, float z);
void pc_ui_arrow(float x, float y, int dir, unsigned int argb);
void pc_tex_keep_original(int texno);
int pc_options_quit(int port);           /* options_glue.c: RETURN TO TITLE's question */
void pc_options_quit_open(void);
void pc_options_quit_draw(void);
int pc_options_quit_up(void);
void spoiler_key(int k);                 /* spoiler_menu.c: 1 = F8 */
void overlay_request(int which);         /* overlay.cpp: open an F-key window (2 F2, 3 F3, 4 F9) */

enum { P_UP = 0x1000, P_RIGHT = 0x2000, P_DOWN = 0x4000, P_LEFT = 0x8000, P_START = 0x800, P_SELECT = 0x100 };   /* Pad[] bits */
enum { SC_ESC = 41 };
enum { AFTER_SPOILER = 8, AFTER_TRADE = 9 };
void coop_trade_menu(void);              /* coop_glue.c: PAUSE > CO-OP > TRADE ITEMS, the trade key once we can */

int pc_new_menu_on(void) { return g_new_menu; }

static int opt_held = 1;   /* the Modern controller's options button was down last frame (1 when OPTION opens) */
static int after;          /* what opens once OPTION has closed: an F-key window, or the spoiler log */
static int esc_prev;
static MouseTake mt;
static int rtl;
static float mx(float x, float w) { return rtl ? 640 - x - w : x; }
static void text_m(float x, float y, const char* s, float sc, unsigned int col) { pc_ui_text(mx(x, pc_ui_text_w(s, sc)), y, s, sc, col); }

/* the Modern controller's options button (third person, in a game) was pressed: it closes OPTION as it opened it */
static int options_button(void)
{
    int opt = bind_pad_raw(B_TPS_OPTIONS), r = opt && !opt_held && g_tps.on && g_tps.pad_modern && pc_in_game();
    opt_held = opt;
    return r;
}
static int esc_edge(void)
{
    const bool* k = bind_keys();
    int down = k && k[SC_ESC], r = down && !esc_prev;
    esc_prev = down;
    return r;
}

/* Adv_GameOptionScreen, after ResetOptionMenuParam(0): the level OPTION starts at (0: level 0, the pause menu in a game) */
static int pause_sel;
static int coop_page, coop_sel, coop_show, coop_leave;   /* PAUSE > CO-OP: up, its row, Show Addresses, LEAVE pressed once */
int pc_new_menu_entry(void)
{
    if (!g_new_menu) return 0;
    opt_held = 1;
    after = 0;
    esc_prev = 1;
    pause_sel = 0;
    coop_page = coop_show = coop_leave = 0;
    ui_device(1, 0);   /* (the button bar: the device that opened OPTION) */
    if (pc_in_game()) { host_log("menu: pause\n"); return 0; }
    ui_rows_open(MS_SETTINGS, "OPTION");
    host_log("menu: settings\n");
    return 4;
}

/* the OPTION sub-screens (KEY CONFIG, SCREEN SETUP, MONITOR ADJUSTMENT) go back to: our SETTINGS (4), else level 0 */
int pc_options_back_level(void) { return g_new_menu ? 4 : 0; }

/* the OPTION plate word on top, smaller (as the proposal's mockups) */
static void title_plate(void)
{
    QUAD* q = &Qtex[1];
    SetQuadPos(141.0f, 2.0f, 358.0f, 38.0f, q);
    SetQuadUv2(0, 320.0f, 448.0f, 48.0f, 2, q);
    pc_tex_keep_original(1);
    AdvEasyDrawTexture(1, -1, q, 0.05f, 1);
    FlushPlate();
}

/* level 4 (options_glue.c pc_options_pc): the row screen; returns the next level */
int pc_new_menu_level4(int port, int flag)
{
    int r;
    rtl = lang_rtl_text();
    if (flag && options_button()) { CallSystemSe(0, 0); AdvWork.OptFadeType = 0; return -1; }
    title_plate();
    r = ui_rows_run(port, flag);
    if (r == UR_BACK) { AdvWork.OptFadeType = 0; return pc_in_game() ? 0 : -1; }
    if (r == UR_CLOSE) { AdvWork.OptFadeType = 0; return -1; }
    if (r > UR_LEVEL && r <= UR_LEVEL + 3) {
        AdvWork.OptIndex = r - UR_LEVEL;   /* (KEY CONFIG edits OptionDef[OptIndex]) */
        AdvWork.OptFadeType = 0;
        host_log("menu: option level %d\n", r - UR_LEVEL);
        return r - UR_LEVEL;
    }
    return 4;
}

/* Adv_GameOptionScreen ends (options_glue.c pc_options_close): the chosen actions, then what was to open */
void pc_new_menu_closed(void)
{
    int w;
    if (!g_new_menu) return;
    ui_rows_close();
    w = ui_rows_window();
    if (!w) w = after;
    after = 0;
    opt_held = 1;
    if (w == AFTER_TRADE) { coop_trade_menu(); w = 0; host_log("menu: TRADE ITEMS (the trade key)\n"); }
    if (w == AFTER_SPOILER) spoiler_key(1);
    else if (w) overlay_request(w);
    if (w) host_log("menu: opens %s\n", w == AFTER_SPOILER ? "the spoiler log" : w == 2 ? "the cheats window" : w == 3 ? "the co-op window" : "the import window");
}

/* ---------------------------------------------------------------- PAUSE */
enum { I_RESUME, I_CAMERA, I_SETTINGS, I_SETUP, I_COOP, I_SPOILER, I_CHEATS, I_TITLE, I_N };
static const char* const items[I_N] = { "RESUME", "Camera", "SETTINGS", "GAME SETUP", "CO-OP", "SPOILER LOG", "CHEATS AND TOOLS", "RETURN TO TITLE" };
static const char* const item_help[I_N] = {
    "Back to the game.",
    "Switch between the fixed cameras and the over-the-shoulder camera (F5).",
    "How you like to play: gameplay, camera, controls, display, graphics, audio, language and co-op.",
    "This game's rules: enemies, characters and co-op rules. Changes apply from the next room.",
    "Your co-op session: partner and connection, trading, addresses, leaving.",
    "Where BioRand put the items, room by room (F8).",
    "Cheats, items, enemies, teleport and the advanced tools window (F2).",
    "Leave this game for the title screen.",
};
#define PX 38.0f
#define PY0 96.0f
#define PDY 27.0f
#define PSC 0.72f
#define PANEL_X0 314.0f
#define PANEL_X1 616.0f
#define C_ROW 0xFFA0A09Bu
#define C_GOLD 0xFFD6B668u
#define C_HELP 0xFFC8C8C2u
#define C_LOCK 0xFFFFB050u

static int item_shown(int i) { return i != I_COOP || coop_on; }
static const char* item_locked(int i)
{
    if (i == I_SPOILER && coop_on) return "Not in co-op: your partner's game runs on.";
    if (i == I_SPOILER && !biorand_active()) return "Only in a BioRand game.";
    return NULL;
}
static float item_y(int i)
{
    int k = 0;
    for (int j = 0; j < i; j++) k += item_shown(j);
    return PY0 + k * PDY + (i == I_TITLE ? 14 : 0);
}
static int camera_row(void) { return menu_setting("Over-the-shoulder Camera"); }
static const char* item_text(int i, char* buf, int n)
{
    if (i != I_CAMERA) return items[i];
    snprintf(buf, n, tr("Camera: %s"), tr(g_tps.on ? "Over Shoulder" : "Fixed"));
    return buf;
}

static void camera_toggle(void)
{
    int k = camera_row();
    const Setting* s = k >= 0 ? &g_settings[k] : NULL;
    if (!s || settings_locked(s)) { CallSystemSe(0, 1); return; }
    settings_set(s, !settings_get(s));
    CallSystemSe(0, 2);
    host_log("menu: camera %s\n", g_tps.on ? "third person" : "fixed");
}

static void move(int dir)
{
    do pause_sel = (pause_sel + dir + I_N) % I_N; while (!item_shown(pause_sel));
    CallSystemSe(0, 2);
}

/* returns the next OPTION level */
static int pick(int i)
{
    ADV_WORK* ap = &AdvWork;
    if (item_locked(i)) { CallSystemSe(0, 1); return 0; }
    ap->OptFadeType = 0;
    switch (i) {
    case I_RESUME: CallSystemSe(0, 0); return -1;
    case I_CAMERA: camera_toggle(); return 0;
    case I_SETTINGS: CallSystemSe(0, 3); ui_rows_open(MS_SETTINGS, "PAUSE"); host_log("menu: settings\n"); return 4;
    case I_SETUP: CallSystemSe(0, 3); ui_rows_open(MS_SETUP, "PAUSE"); host_log("menu: game setup\n"); return 4;
    case I_CHEATS: CallSystemSe(0, 3); ui_rows_open(MS_CHEATS, "PAUSE"); host_log("menu: cheats and tools\n"); return 4;
    case I_COOP: CallSystemSe(0, 3); coop_page = 1; coop_sel = 0; coop_leave = 0; host_log("menu: co-op\n"); return 0;
    case I_SPOILER: CallSystemSe(0, 3); after = AFTER_SPOILER; return -1;
    case I_TITLE: CallSystemSe(0, 3); pc_options_quit_open(); return 0;
    }
    return 0;
}

static int item_at(float x, float y)
{
    if (x < PX - 10 || x > PANEL_X0 - 50) return -1;
    for (int i = 0; i < I_N; i++)
        if (item_shown(i) && y >= item_y(i) - 3 && y < item_y(i) + PDY - 3) return i;
    return -1;
}

/* ---------------------------------------------------------------- PAUSE > CO-OP (the proposal's §1.1, instead of the F3 window):
   TRADE ITEMS (the trade key once OPTION has closed), Show Addresses (with Hide IP Addresses: not saved), REMOVE PARTNER
   (host), LEAVE (a guest) or END SESSION (the host; both ask with a second press), Co-op Window (F3) for the rest; on the
   right the partner, ping and the connection. Co-op does not pause. */
enum { C_TRADE, C_SHOW, C_KICK, C_LEAVE, C_WINDOW, C_BACK, C_N };
static int coop_shown_row(int i)
{
    if (i == C_SHOW) return g_coop.hide_ip;
    if (i == C_KICK) return g_coop.host && (g_coop.state == COOP_CONNECTED || g_coop.state == COOP_LOST);
    return 1;
}
static const char* coop_label(int i)
{
    static const char* const l[C_N] = { "TRADE ITEMS", "Show Addresses", "REMOVE PARTNER", "", "Co-op Window (F3)", "BACK" };
    if (i == C_LEAVE) return coop_leave ? (g_coop.host ? "END SESSION: PRESS AGAIN" : "LEAVE: PRESS AGAIN") : g_coop.host ? "END SESSION" : "LEAVE";
    return l[i];
}
static const char* const coop_help[C_N] = {
    "Trade with your partner (also the trade key): stand next to each other, then choose what to give and take.",
    "Hide IP Addresses is on: show the addresses here until you leave the pause menu.",
    "Remove your partner and reopen their place. They can rejoin with the same code.",
    "",
    "The co-op window (F3): every co-op option, with the controller too. Opens when you leave this menu.",
    "Back to the pause menu.",
};
static float coop_y(int i) { int k = 0; for (int j = 0; j < i; j++) k += coop_shown_row(j); return PY0 + k * PDY + (i == C_BACK ? 14 : 0); }
static void coop_move(int dir) { do coop_sel = (coop_sel + dir + C_N) % C_N; while (!coop_shown_row(coop_sel)); CallSystemSe(0, 2); }
static int coop_pick(int i)
{
    ADV_WORK* ap = &AdvWork;
    ap->OptFadeType = 0;
    if (i != C_LEAVE) coop_leave = 0;
    switch (i) {
    case C_TRADE: CallSystemSe(0, 3); after = AFTER_TRADE; return -1;
    case C_SHOW: coop_show ^= 1; CallSystemSe(0, 2); return 0;
    case C_KICK: g_coop.request = COOP_REQ_KICK; CallSystemSe(0, 0); host_log("menu: co-op: remove partner\n"); return 0;
    case C_LEAVE:
        if (!coop_leave) { coop_leave = 1; CallSystemSe(0, 1); return 0; }
        g_coop.request = COOP_REQ_STOP;
        CallSystemSe(0, 0);
        host_log("menu: co-op: %s\n", g_coop.host ? "end session" : "leave");
        coop_page = 0;
        return -1;
    case C_WINDOW: CallSystemSe(0, 3); after = 3; return -1;
    default: CallSystemSe(0, 0); coop_page = 0; return 0;
    }
}
static int coop_at(float x, float y)
{
    if (x < PX - 10 || x > PANEL_X0 - 50) return -1;
    for (int i = 0; i < C_N; i++) if (coop_shown_row(i) && y >= coop_y(i) - 3 && y < coop_y(i) + PDY - 3) return i;
    return -1;
}
static int coop_input(unsigned int press, unsigned int rept, unsigned int ok, unsigned int cancel, int esc, float x)
{
    int hit = coop_at(x, mt.y);
    if (!coop_shown_row(coop_sel)) coop_move(1);
    if (hit >= 0 && (mt.moved || mt.left) && hit != coop_sel) { coop_sel = hit; coop_leave = 0; CallSystemSe(0, 2); }
    if (mt.left && hit >= 0) return coop_pick(hit);
    if (mt.right || esc || (press & (cancel | P_SELECT))) return coop_pick(C_BACK);
    if (rept & P_UP) { coop_move(-1); coop_leave = 0; }
    else if (rept & P_DOWN) { coop_move(1); coop_leave = 0; }
    else if ((press & (P_LEFT | P_RIGHT)) && coop_sel == C_SHOW) coop_pick(C_SHOW);
    else if (press & (ok | P_START)) return coop_pick(coop_sel);
    return 0;
}
static void coop_info(float y)
{
    char t[200];
    text_m(PANEL_X0 + 10, y, "CO-OP", 0.55f, C_GOLD);
    y += 20;
    snprintf(t, sizeof(t), tr("Partner: %s"), g_coop.state == COOP_CONNECTED ? (g_coop.peer_name[0] ? g_coop.peer_name : tr("your partner")) : tr("none"));
    text_m(PANEL_X0 + 16, y, t, 0.48f, 0xFFFFFFFF); y += 16;
    if (g_coop.state == COOP_CONNECTED) { snprintf(t, sizeof(t), tr("Ping: %d ms"), g_coop.ping_ms); text_m(PANEL_X0 + 16, y, t, 0.48f, 0xFFFFFFFF); y += 16; }
    snprintf(t, sizeof(t), "%s", tr(g_coop.host ? "You host this game." : "You joined this game."));
    text_m(PANEL_X0 + 16, y, t, 0.46f, C_HELP); y += 18;
    text_m(PANEL_X0 + 10, y, "CONNECTION", 0.5f, C_GOLD); y += 18;
    y += 14 * (rtl ? pc_ui_text_wrap_r : pc_ui_text_wrap)(PANEL_X0 + 16, y, PANEL_X1 - PANEL_X0 - 26,
                                                        g_coop.status[0] ? (coop_show ? g_coop.status : coop_shown(g_coop.status)) : tr("Connected."), 0.44f, C_HELP, 4);
    if (g_coop.host && g_coop.room[0]) { snprintf(t, sizeof(t), tr("Room code: %.4s-%.4s"), g_coop.room, g_coop.room + 4); text_m(PANEL_X0 + 16, y + 4, t, 0.46f, 0xFFFFFFFF); }
}

static int pause_input(int port)
{
    unsigned int press = Pad[port].press, rept = Pad[port].Rept;
    unsigned int ok = (sys->keytype & 3) < 3 ? AdvGetOkButton() : 0xC0, cancel = (sys->keytype & 3) < 3 ? AdvGetCancelButton() : 0x20;
    int esc = esc_edge(), hit;
    float x;
    menu_mouse_take(&mt);
    x = mx(mt.x, 0);
    ui_device(press, mt.moved || mt.left || mt.right || esc);
    if (options_button()) { CallSystemSe(0, 0); AdvWork.OptFadeType = 0; return -1; }
    if (coop_page && !coop_on) coop_page = 0;   /* (the session ended meanwhile) */
    if (coop_page) return coop_input(press, rept, ok, cancel, esc, x);
    hit = item_at(x, mt.y);
    if (hit >= 0 && (mt.moved || mt.left) && hit != pause_sel) { pause_sel = hit; CallSystemSe(0, 2); }
    if (mt.left && hit >= 0) return pick(hit);
    if (mt.right || esc || (press & (cancel | P_SELECT)) || ((press & P_START) && !pad_kb_start())) return pick(I_RESUME);
    if (rept & P_UP) move(-1);
    else if (rept & P_DOWN) move(1);
    else if ((press & (P_LEFT | P_RIGHT)) && pause_sel == I_CAMERA) camera_toggle();
    else if (press & (ok | P_START)) return pick(pause_sel);
    return 0;
}

/* THIS GAME: what this run is (its difficulty, BioRand, the rules that differ from the original game) */
static void this_game(float y)
{
    static const char* const diff[] = { "Normal", "Easy", "Very Easy" };
    static const char* const rows[] = { "Enemy Randomizer", "Enemy Selection", "Extra Enemies", "Rearranged Enemies", "Extra Item Boxes and Herbs",
                                        "Free Typewriter Saves", "Randomize Cutscene Cast", "Randomize Voice Lines", "Headshots & Piercing" };
    char t[160], v[96];
    int n = 0;
    text_m(PANEL_X0 + 10, y, "THIS GAME", 0.55f, C_GOLD);
    y += 20;
    snprintf(t, sizeof(t), tr("Difficulty: %s"), tr(diff[sys->gm_mode >= 0 && sys->gm_mode < 3 ? sys->gm_mode : 0]));
    text_m(PANEL_X0 + 16, y, t, 0.48f, 0xFFFFFFFF); y += 16;
    if (biorand_active()) { text_m(PANEL_X0 + 16, y, "BioRand game", 0.48f, 0xFFFFFFFF); y += 16; }
    if (coop_on) { snprintf(t, sizeof(t), tr("Co-op with %s"), g_coop.peer_name[0] ? g_coop.peer_name : tr("your partner")); text_m(PANEL_X0 + 16, y, t, 0.48f, 0xFFFFFFFF); y += 16; }
    for (int i = 0; i < (int)(sizeof(rows) / sizeof(rows[0])) && n < 6; i++) {
        int k = menu_setting(rows[i]), d;
        const MenuRow* m = NULL;
        if (k < 0 || !menu_default(k, &d) || settings_get(&g_settings[k]) == d) continue;
        if (i == 1 && !g_enemy_opts.rand) continue;   /* (the pool only with the randomizer) */
        for (int j = 0; j < menu_rows_n; j++) if (menu_rows[j].kind == ML_SET && !strcmp(menu_rows[j].ref, rows[i])) m = &menu_rows[j];
        if (m && m->names && !g_settings[k].name) snprintf(v, sizeof(v), "%s", tr(m->names[settings_get(&g_settings[k]) - g_settings[k].lo]));
        else settings_value(&g_settings[k], v, sizeof(v));
        snprintf(t, sizeof(t), "%s: %s", tr(m && m->label ? m->label : rows[i]), v);
        text_m(PANEL_X0 + 16, y, t, 0.46f, C_HELP); y += 15; n++;
    }
    if (!n) { text_m(PANEL_X0 + 16, y, "No modifiers: the original game.", 0.46f, C_HELP); y += 15; }
    y += 10;
    {   /* the keyboard's shortcuts to the same places */
        static const struct { int row; const char* what; } keys[] = { { B_TPS_TOGGLE, "Camera" }, { B_SPOILER, "Spoiler Log" }, { B_CHEATS, "Cheats" },
                                                                        { B_SETTINGS, "Settings Window" }, { B_COOP, "Co-op" }, { B_IMPORT, "Import" } };
        float x = PANEL_X0 + 16;
        text_m(PANEL_X0 + 10, y, "KEYBOARD SHORTCUTS", 0.55f, C_GOLD);
        y += 20;
        for (int i = 0; i < (int)(sizeof(keys) / sizeof(keys[0])); i++) {
            char k[32];
            bind_code_name(BIND_KB, bind_get(keys[i].row, BIND_KB, 0), k, sizeof(k));
            if (x + ui_hint_w(k, keys[i].what) > PANEL_X1 - 8) { x = PANEL_X0 + 16; y += 20; }
            x = ui_hint(x, y, k, keys[i].what, 0);
        }
    }
}

static void pause_draw(void)
{
    char t[96], g[40];
    float x;
    pc_ui_rect(0, 0, 640, 480, 0xC8000000, 0.015f);   /* the picture darkened */
    text_m(PX - 4, 24, "PAUSE", 1.35f, 0xFFFFFFFF);
    if (coop_on) text_m(PX, 64, "The game keeps running.", 0.48f, C_LOCK);
    if (pc_options_quit_up()) { AdvEasySetTextureList(0); return; }   /* (the question alone: the font's text would show through its box) */
    if (coop_page) {   /* PAUSE > CO-OP */
        text_m(PX, 80, "PAUSE", 0.45f, C_ROW);   /* (the font has no '>': an arrow) */
        pc_ui_arrow(mx(PX + pc_ui_text_w("PAUSE", 0.45f) + 12, 0), 87, rtl ? -1 : 1, C_ROW);
        text_m(PX + pc_ui_text_w("PAUSE", 0.45f) + 20, 80, "CO-OP", 0.45f, C_ROW);
        for (int i = 0; i < C_N; i++) {
            float y = coop_y(i);
            int sel = i == coop_sel;
            unsigned int col = i == C_LEAVE && coop_leave ? 0xFFFFB050 : sel ? 0xFFFFFFFF : C_ROW;
            if (!coop_shown_row(i)) continue;
            if (sel) {
                pc_ui_rect(mx(PX - 10, 262), y - 3, mx(PX - 10, 262) + 262, y + PDY - 5, 0x40FFFFFF, 0.3f);
                pc_ui_rect(mx(PX - 10, 3), y - 3, mx(PX - 10, 3) + 3, y + PDY - 5, C_GOLD, 0.31f);
            }
            if (i == C_SHOW) { snprintf(t, sizeof(t), "%s: %s", tr(coop_label(i)), tr(coop_show ? "On" : "Off")); text_m(PX, y, t, PSC * 0.85f, col); }
            else text_m(PX, y, coop_label(i), i == C_LEAVE && coop_leave ? PSC * 0.7f : PSC, col);
            if (i == C_WINDOW) pc_ui_arrow(mx(PX + 240, 0), y + 10, rtl ? -1 : 1, col);
        }
        pc_ui_rect(mx(PANEL_X0, PANEL_X1 - PANEL_X0), 92, mx(PANEL_X0, PANEL_X1 - PANEL_X0) + PANEL_X1 - PANEL_X0, 330, 0x90000000, 0.02f);
        coop_info(98);
        pc_ui_rect(20, 420, 620, 421, 0x80FFFFFF, 0.021f);
        (rtl ? pc_ui_text_wrap_r : pc_ui_text_wrap)(20, 426, 600, tr(coop_sel == C_LEAVE ? (g_coop.host ? "End the session: your partner leaves; you play on alone."
                                                                                                     : "Leave the host's game: you play on alone from here.") : coop_help[coop_sel]), 0.45f, C_HELP, 2);
        x = ui_hint(20, 456, ui_glyph(UG_OK, g, sizeof(g)), "Select", 0);
        ui_hint(x, 456, ui_glyph(UG_BACK, g, sizeof(g)), "Back", 0);
        AdvEasySetTextureList(0);
        return;
    }
    for (int i = 0; i < I_N; i++) {
        float y = item_y(i);
        int sel = i == pause_sel;
        unsigned int col = item_locked(i) ? (sel ? 0xFF9A9A9A : 0xFF686868) : sel ? 0xFFFFFFFF : C_ROW;
        if (!item_shown(i)) continue;
        if (sel) {
            pc_ui_rect(mx(PX - 10, 262), y - 3, mx(PX - 10, 262) + 262, y + PDY - 5, 0x40FFFFFF, 0.3f);
            pc_ui_rect(mx(PX - 10, 3), y - 3, mx(PX - 10, 3) + 3, y + PDY - 5, C_GOLD, 0.31f);
        }
        text_m(PX, y, item_text(i, t, sizeof(t)), PSC, col);
        if (i != I_RESUME && i != I_TITLE) pc_ui_arrow(mx(PX + 240, 0), y + 10, rtl ? -1 : 1, col);
    }
    pc_ui_rect(mx(PANEL_X0, PANEL_X1 - PANEL_X0), 92, mx(PANEL_X0, PANEL_X1 - PANEL_X0) + PANEL_X1 - PANEL_X0, 330, 0x90000000, 0.02f);
    this_game(98);
    /* help and the button bar */
    pc_ui_rect(20, 420, 620, 421, 0x80FFFFFF, 0.021f);
    {
        const char* h = item_locked(pause_sel) ? item_locked(pause_sel) : item_help[pause_sel];
        (rtl ? pc_ui_text_wrap_r : pc_ui_text_wrap)(20, 426, 600, tr(h), 0.45f, item_locked(pause_sel) ? C_LOCK : C_HELP, 2);   /* (2: a translation may be longer) */
    }
    x = 20;
    x = ui_hint(x, 456, ui_glyph(UG_OK, g, sizeof(g)), "Select", 0);
    x = ui_hint(x, 456, ui_glyph(UG_BACK, g, sizeof(g)), "Resume", 0);
    if (pause_sel == I_CAMERA) ui_hint(x, 456, ui_glyph(UG_CHANGE, g, sizeof(g)), "Change", 0);
    AdvEasySetTextureList(0);   /* the option screen's textures again (the font set its own) */
}

/* DisplayOptionPlateLevel0 in a game: the pause menu; returns the next level (0 stay) */
int pc_pause(int port, int flag)
{
    int next = 0;
    rtl = lang_rtl_text();
    if (flag) {
        int q = pc_options_quit(port);   /* RETURN TO TITLE's question takes the input while it is up */
        if (q < 0) { AdvWork.OptFadeType = 0; return -1; }
        if (q) flag = 0;
    }
    if (flag) next = pause_input(port);
    else menu_mouse_take(&mt);
    pause_draw();
    pc_options_quit_draw();
    FlushPlate();   /* (the pulse of the chosen answer, as level 0 does each frame) */
    return next;
}
