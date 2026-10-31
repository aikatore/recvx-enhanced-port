/*
 * libpad on the host: a DualShock 2 in pressure-sensitive mode on port 0,
 * fed by the first SDL gamepad and/or the keyboard. Rumble via SDL.
 */
#include "../ps2_platform.h"
#include "../host/tps.h"
#include "../host/lobby.h"
#include "../host/bindings.h"
#include "../host/cheat.h"
#include <SDL3/SDL.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* DS2 digital bits (active low in the report) */
enum {
    PAD_SELECT = 1 << 0, PAD_L3 = 1 << 1, PAD_R3 = 1 << 2, PAD_START = 1 << 3,
    PAD_UP = 1 << 4, PAD_RIGHT = 1 << 5, PAD_DOWN = 1 << 6, PAD_LEFT = 1 << 7,
    PAD_L2 = 1 << 8, PAD_R2 = 1 << 9, PAD_L1 = 1 << 10, PAD_R1 = 1 << 11,
    PAD_TRIANGLE = 1 << 12, PAD_CIRCLE = 1 << 13, PAD_CROSS = 1 << 14, PAD_SQUARE = 1 << 15
};

static SDL_Gamepad* gamepad;

/* Test automation: RECVX_INPUT="<frame>:<buttons>,..." holds <buttons> ('+'-separated names,
   or '-' for none) from that presented game frame until the next entry. Example: "200:start,205:-". */
uint64_t gs_frame_count(void);
static uint32_t scripted_buttons(void)
{
    static const struct { const char* n; uint32_t b; } names[] = {
        { "select", 1 << 0 }, { "l3", 1 << 1 }, { "r3", 1 << 2 }, { "start", 1 << 3 }, { "up", 1 << 4 }, { "right", 1 << 5 },
        { "down", 1 << 6 }, { "left", 1 << 7 }, { "l2", 1 << 8 }, { "r2", 1 << 9 }, { "l1", 1 << 10 }, { "r1", 1 << 11 },
        { "triangle", 1 << 12 }, { "circle", 1 << 13 }, { "cross", 1 << 14 }, { "square", 1 << 15 } };
    static const char* script; static int checked;
    uint64_t now = gs_frame_count();
    uint32_t cur = 0;
    const char* p;
    if (!checked++) script = SDL_getenv("RECVX_INPUT");
    if (!script) return 0;
    for (p = script; *p;) {
        char* e;
        unsigned long at = strtoul(p, &e, 10);
        uint32_t b = 0;
        if (*e != ':') break;
        p = e + 1;
        while (*p && *p != ',') {
            size_t n = strcspn(p, "+,");
            for (size_t k = 0; k < sizeof(names) / sizeof(names[0]); k++)
                if (strlen(names[k].n) == n && !strncmp(p, names[k].n, n)) b |= names[k].b;
            p += n;
            if (*p == '+') p++;
        }
        if (*p == ',') p++;
        if (at <= now) cur = b; else break;
    }
    return cur;
}
static int input_enabled = 1;

void pad_set_game_input_enabled(int on) { input_enabled = on; }
int pad_game_input_enabled(void) { return input_enabled; }

/* the gamepad of the input source (bindings.c): with "any" the open one stays while connected,
   else the first; a chosen one (or keyboard/mouse only: none) is looked up again when a gamepad
   was added or removed or the setting changed. Only the open pad sends events (captures) and
   rumbles. */
static SDL_AtomicInt pads_changed = { 1 };
void pad_devices_changed(void) { SDL_SetAtomicInt(&pads_changed, 1); }   /* main thread: GAMEPAD_ADDED/REMOVED */
static void ensure_gamepad(void)
{
    static int source = -1, pad = -1;
    SDL_JoystickID want;
    int ok = gamepad && SDL_GamepadConnected(gamepad);
    if (ok && g_input_source != INPUT_KBM && !g_input_pad) return;
    if (!SDL_SetAtomicInt(&pads_changed, 0) && source == g_input_source && pad == g_input_pad && ok == (gamepad != NULL)) return;
    source = g_input_source; pad = g_input_pad;
    want = bind_pick_pad();
    if (ok && SDL_GetGamepadID(gamepad) == want) return;
    if (gamepad) { SDL_CloseGamepad(gamepad); gamepad = NULL; }
    if (want) gamepad = SDL_OpenGamepad(want);
    bind_set_gamepad(gamepad);
}

/* rumble: whether the open gamepad has motors (the game then finds a DualShock's actuators:
   OPTION's VIBRATION can be chosen); off at exit (SDL stops a rumble only while it pumps events) */
static int pad_can_rumble(void)
{
    ensure_gamepad();
    return gamepad && SDL_GetBooleanProperty(SDL_GetGamepadProperties(gamepad), SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false);
}
static void rumble_off(void) { if (gamepad) SDL_RumbleGamepad(gamepad, 0, 0, 0); }

int tps_mouse_buttons(void);

/* the DS2 bits of the run button under the game's key type (quick turn) */
static uint32_t run_bits(void)
{
    int m = pc_keytype_mask(10);
    return m > 0 ? (uint32_t)(m & -m) : 0;
}

/* quick turn: the game turns when run is pressed while walking back (player.c), so the
   binding holds back and presses run from the third game frame on */
int bind_quickturn(int which, int held)
{
    static uint64_t since[2];   /* first frame held + 1 */
    uint64_t now = gs_frame_count();
    if (!held) { since[which] = 0; return 0; }
    if (!since[which]) since[which] = now + 1;
    return now + 1 >= since[which] + 2 ? 2 : 1;
}

static uint8_t axis8(Sint16 v) { int x = (v + 32768) >> 8; return (uint8_t)(x < 0 ? 0 : x > 255 ? 255 : x); }

/* modern controls (pc/glue/modern_glue.c), game thread: this frame's direction on screen (x
   right, y up, length up to 1) from the left stick past its dead zone, else from the d-pad rows
   (arrow keys); whether it is the gamepad's d-pad (or the script's), not the arrow keys (`kb`:
   the rows held on the keyboard; Combined plays the d-pad as Classic); and whether the
   quick-turn row is held */
static float move_x, move_y;
static int move_analog, move_dpad, quickturn_on, free_aim_on;
int pad_move_dir(float* x, float* y, int* dpad) { *x = move_x; *y = move_y; *dpad = move_dpad; return move_analog; }
int pad_quickturn_held(void) { return quickturn_on; }
int pad_free_aim(void) { return free_aim_on; }
int reload_combo_ok(void);   /* reload_glue.c: our player in normal play */
int wpn_hold_ok(void);       /* weapon_glue.c: the shortcut wheel may come up */
static void set_move_dir(uint32_t b, uint32_t kb, Sint16 sx, Sint16 sy)
{
    float x = sx / 32767.0f, y = -sy / 32767.0f, m = sqrtf(x * x + y * y);
    const float dead = 0.25f;
    move_analog = m > dead;
    move_dpad = 0;
    if (move_analog) {   /* rescaled from the dead zone on */
        float k = ((m > 1 ? 1 : m) - dead) / (1 - dead) / m;
        move_x = x * k; move_y = y * k;
        return;
    }
    move_dpad = !(kb & (PAD_UP | PAD_RIGHT | PAD_DOWN | PAD_LEFT));
    move_x = (float)(!!(b & PAD_RIGHT) - !!(b & PAD_LEFT));
    move_y = (float)(!!(b & PAD_UP) - !!(b & PAD_DOWN));
    if (move_x != 0 && move_y != 0) { move_x *= 0.70710678f; move_y *= 0.70710678f; }
}

/* Start's keyboard binding (Enter) is down in this report (OPTION takes it as confirm too, options_glue.c) */
static int kb_start;
int pad_kb_start(void) { return kb_start; }
/* the DS2 buttons of the last report (the game's Pad[].on also has stick directions, at rest too) */
static uint32_t report_bits;
int pad_report_bits(void) { return (int)report_bits; }

static void build_report(unsigned char* r)
{
    uint32_t b = 0, kb = 0, qbits = 0;
    Sint16 slx = 0, sly = 0;
    uint8_t lx = 128, ly = 128, rx = 128, ry = 128;
    const bool* k;

    ensure_gamepad();
    kb_start = 0;
    free_aim_on = 0;
    k = bind_latch_frame(pc_menu_smooth(), bind_keys());   /* quick presses in menus (bindings.c) */
    /* the weapon shortcut wheel (weapon_glue.c): up while its button is held in play; then the d-pad picks */
    g_wpn_overlay = 0;
    g_wpn_overlay = input_enabled && !g_lobby_io.text_on && pad_wpn_hold() && wpn_hold_ok();
    bind_combo_frame(input_enabled && !g_lobby_io.text_on, k, tps_mouse_buttons());   /* the reload combination first: it takes its presses from the rows */
    if (input_enabled) {
        int mouse = tps_mouse_buttons(), qt;
        /* keyboard and gamepad through the bindings (bindings.c; by default arrows = d-pad,
           Enter = Start, Backspace = Select, Z/X/A/S = Cross/Circle/Square/Triangle, Q/W = L1/R1,
           1/2 = L2/R2, the gamepad as a DS2); the keyboard is none while the co-op lobby has a
           text field open (it types there) */
        static const bool none[SDL_SCANCODE_COUNT];
        if (g_lobby_io.text_on) k = none;
        for (int i = 0; i < 16; i++)
            if (bind_held(i, BIND_KB, k, mouse)) kb |= 1u << i;
            else if (gamepad && bind_held(i, BIND_PAD, k, 0)) b |= 1u << i;
        b |= kb;
        kb_start = bind_held(B_START, BIND_KB, k, mouse);
        qt = bind_quickturn(0, bind_held(B_QUICKTURN, BIND_KB, k, mouse) || (gamepad && bind_held(B_QUICKTURN, BIND_PAD, k, 0)));
        if (qt) qbits = PAD_DOWN | (qt > 1 ? run_bits() : 0);
        /* free aim (fixed cameras, in play; not while flying, where it flies down): the key type's aim button, and
           modern_player cancels the game's turn to a target */
        if (!g_tps.on && g_cheat.noclip != 2 && (bind_held(B_FREEAIM, BIND_KB, k, mouse) || (gamepad && bind_held(B_FREEAIM, BIND_PAD, k, 0))) &&
            pc_keytype_mask(4) > 0 && reload_combo_ok()) {
            b |= (uint32_t)pc_keytype_mask(4);
            free_aim_on = 1;
        }
        if (gamepad) {
            slx = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX);
            sly = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY);
            lx = axis8(slx);
            ly = axis8(sly);
            rx = axis8(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX));
            ry = axis8(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY));
        }
    }

    bind_latch_end();
    b |= scripted_buttons();
    set_move_dir(b, kb, slx, sly);
    quickturn_on = qbits != 0;
    b |= qbits;
    report_bits = b;
    {   /* RECVX_PAD_TRACE=1: log the DS2 buttons when they change */
        static int trace = -1; static uint32_t last;
        if (trace < 0) trace = SDL_getenv("RECVX_PAD_TRACE") != NULL;
        if (trace && b != last) { host_log("pad: frame %llu buttons %04x\n", (unsigned long long)gs_frame_count(), b); last = b; }
    }
    memset(r, 0, 32);
    r[0] = 0;          /* success */
    r[1] = 0x79;       /* DualShock 2, analog + pressure */
    r[2] = (uint8_t)~(b & 0xFF);
    r[3] = (uint8_t)~(b >> 8);
    r[4] = rx; r[5] = ry; r[6] = lx; r[7] = ly;
    /* pressure: right, left, up, down, triangle, circle, cross, square, L1, R1, L2, R2 */
    {
        static const uint32_t order[12] = { PAD_RIGHT, PAD_LEFT, PAD_UP, PAD_DOWN, PAD_TRIANGLE, PAD_CIRCLE,
                                            PAD_CROSS, PAD_SQUARE, PAD_L1, PAD_R1, PAD_L2, PAD_R2 };
        for (int i = 0; i < 12; i++) r[8 + i] = (b & order[i]) ? 255 : 0;
    }
}

/* quick knife (pc/glue/knife_glue.c), game thread: its row is held on the keyboard/mouse or the
   gamepad (in third-person play with the modern scheme the gamepad's third-person knife row) */
int pad_quick_knife(void)
{
    const bool* k = bind_keys();
    if (!input_enabled || g_lobby_io.text_on) return 0;
    return bind_held(B_QUICKKNIFE, BIND_KB, k, tps_mouse_buttons()) ||
           (gamepad && (bind_held(B_QUICKKNIFE, BIND_PAD, k, 0) || bind_held(B_TPS_KNIFE, BIND_PAD, k, 0)));
}

/* weapon shortcuts (pc/glue/weapon_glue.c), game thread: the hold row, the wheel's directions and page
   buttons (also while it takes them), the shortcut rows; keyboard/mouse or the gamepad */
static int wpn_row(int row, int through)
{
    const bool* k = bind_keys();
    int m = tps_mouse_buttons();
    if (!input_enabled || g_lobby_io.text_on) return 0;
    if (through) return bind_held_through(row, BIND_KB, k, m) || (gamepad && bind_held_through(row, BIND_PAD, k, 0));
    return bind_held(row, BIND_KB, k, m) || (gamepad && bind_held(row, BIND_PAD, k, 0));
}
int pad_wpn_hold(void) { return wpn_row(B_WPN_HOLD, 0); }
int pad_wpn_key(int n) { return n >= 0 && n < 8 && wpn_row(B_WPN1 + n, 0); }
int pad_wpn_dirs(void)
{
    static const int rows[6] = { B_UP, B_RIGHT, B_DOWN, B_LEFT, B_L1, B_R1 };
    int r = 0;
    for (int i = 0; i < 6; i++) if (wpn_row(rows[i], 1)) r |= 1 << i;
    return r;
}

/* reload (pc/glue/reload_glue.c), game thread: its row is held on the keyboard/mouse or the gamepad, or in
   third person the third-person reload row (keyboard/mouse; the gamepad's in third-person play with the modern scheme);
   a binding shared with another row only as Aim + that button (bindings.c bind_combo) */
int pad_reload(void)
{
    const bool* k = bind_keys();
    int m = tps_mouse_buttons();
    if (!input_enabled || g_lobby_io.text_on) return 0;
    return bind_held(B_RELOAD, BIND_KB, k, m) || (g_tps.on && bind_held(B_TPS_RELOAD, BIND_KB, k, m)) ||
           (gamepad && (bind_held(B_RELOAD, BIND_PAD, k, 0) || bind_held(B_TPS_RELOAD, BIND_PAD, k, 0)));
}

/* co-op trade request (pc/glue/coop_glue.c), game thread: its row is held on the keyboard/mouse or the gamepad */
int pad_trade_held(void)
{
    const bool* k = bind_keys();
    if (!input_enabled || g_lobby_io.text_on) return 0;
    return bind_held(B_TRADE, BIND_KB, k, tps_mouse_buttons()) || (gamepad && bind_held(B_TRADE, BIND_PAD, k, 0));
}

/* noclip cheat with free height (pc/glue/cheat_glue.c), game thread: +1 up, -1 down. Up: the
   "noclip fly up" hotkeys (R, Page Up) or the R2 button (key 2, right trigger); down: "noclip
   fly down" (F, Page Down) or L2 (key 1). R2 is unused by the game's key types,
   L2 only by type D; the stick clicks are not free (R3 = status). The gamepad's right trigger is
   the quick knife's by default, which is off while flying (knife_glue.c), so it flies up too; the
   left trigger, free aim's, likewise down. */
int pad_noclip_vertical(void)
{
    const bool* k = bind_keys();
    uint32_t b = scripted_buttons();
    int v = 0;
    if (input_enabled) {
        if (bind_held(B_NOCLIP_UP, BIND_KB, k, 0) || bind_held(B_R2, BIND_KB, k, 0) ||
            (gamepad && (bind_held(B_R2, BIND_PAD, k, 0) || bind_held(B_QUICKKNIFE, BIND_PAD, k, 0)))) b |= PAD_R2;
        if (bind_held(B_NOCLIP_DOWN, BIND_KB, k, 0) || bind_held(B_L2, BIND_KB, k, 0) ||
            (gamepad && (bind_held(B_L2, BIND_PAD, k, 0) || bind_held(B_FREEAIM, BIND_PAD, k, 0)))) b |= PAD_L2;
    }
    if (b & PAD_R2) v++;
    if (b & PAD_L2) v--;
    return v;
}

int scePadInit(int mode) { (void)mode; return 1; }
int scePadPortOpen(int port, int slot, u_long128* addr) { (void)port; (void)slot; (void)addr; return 1; }
int scePadPortClose(int port, int slot) { (void)port; (void)slot; return 1; }
int scePadGetState(int port, int slot) { (void)slot; return port == 0 ? 6 /* stable */ : 0 /* disconnected */; }
int scePadGetReqState(int port, int slot) { (void)port; (void)slot; return 0; /* complete */ }
int scePadInfoMode(int port, int slot, int term, int offs)
{
    (void)port; (void)slot; (void)offs;
    switch (term) {
    case 1: return 7;   /* InfoModeCurID: DualShock */
    case 2: return pad_can_rumble() ? 7 : 0;   /* InfoModeCurExID: DualShock with motors (ps2_sg_pdvib.c pdVibMxIsReady), else none */
    case 3: return 0;
    case 4: return offs == -1 ? 2 : (offs == 0 ? 4 : 7);
    }
    return 0;
}
int scePadSetMainMode(int port, int slot, int offs, int lock) { (void)port; (void)slot; (void)offs; (void)lock; return 1; }
int scePadInfoPressMode(int port, int slot) { (void)port; (void)slot; return 1; }
int scePadEnterPressMode(int port, int slot) { (void)port; (void)slot; return 1; }
int scePadSetActAlign(int port, int slot, const unsigned char* data) { (void)port; (void)slot; (void)data; return 1; }

/* Ps2_pad_actuater, every game frame: the small motor (on/off) on the gamepad's high-frequency one,
   the large (0-255) on the low-frequency one, at Vibration strength; none while the window has no
   focus (unless Gamepad only, which plays in the background). Each refresh lasts 100 ms, so a
   rumble ends by itself when the game stops sending. RECVX_RUMBLE_TRACE=1 logs each change. */
int scePadSetActDirect(int port, int slot, const unsigned char* data)
{
    extern SDL_Window* g_window;
    static int trace = -1, atexit_set;
    static Uint64 last = 1;
    Uint16 lo = 0, hi = 0;
    (void)slot;
    if (port != 0) return 1;
    if (gamepad && (g_input_source == INPUT_PAD || (SDL_GetWindowFlags(g_window) & SDL_WINDOW_INPUT_FOCUS))) {
        lo = (Uint16)(data[1] * 257 * g_config.rumble / 100);
        hi = (Uint16)(data[0] ? 0xC000 * g_config.rumble / 100 : 0);
    }
    if (trace < 0) trace = SDL_getenv("RECVX_RUMBLE_TRACE") != NULL;
    if (trace && ((Uint64)data[0] << 48 | (Uint64)data[1] << 32 | (Uint64)lo << 16 | hi) != last) {
        last = (Uint64)data[0] << 48 | (Uint64)data[1] << 32 | (Uint64)lo << 16 | hi;
        host_log("rumble: frame %llu motors small %d large %d -> %s low %u high %u\n", (unsigned long long)gs_frame_count(),
                 data[0], data[1], gamepad ? "gamepad" : "no gamepad", lo, hi);
    }
    if (gamepad) {
        if (!atexit_set++) atexit(rumble_off);
        SDL_RumbleGamepad(gamepad, lo, hi, 100);
    }
    return 1;
}

int scePadRead(int port, int slot, unsigned char* rdata)
{
    (void)slot;
    if (port != 0) { memset(rdata, 0xFF, 32); rdata[0] = 0xFF; return 0; }
    build_report(rdata);
    return 32;
}
