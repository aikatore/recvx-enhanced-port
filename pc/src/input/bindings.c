/* Control bindings: keyboard/mouse and gamepad -> DS2 buttons, third-person keys, hotkeys.
   docs/ARCHITECTURE.md "Input". The defaults are the port's fixed layout from before (pad.c,
   tps_input.c, host.c, overlay.cpp), so nothing changes until a binding is edited. */
#include "../host/lang.h"
#include "../host/bindings.h"
#include "../host/tps.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint64_t gs_frame_count(void);
void host_log(const char* fmt, ...);

#define SC(x) SDL_SCANCODE_##x
#define MB(n) (BIND_MOUSE + (n))
#define GB(x) (1 + SDL_GAMEPAD_BUTTON_##x)
#define GA(x, neg) (BIND_AXIS + SDL_GAMEPAD_AXIS_##x * 2 + (neg))

const BindRow bind_rows[B_COUNT] = {
    [B_SELECT] = { "select", "Select", BG_GAME, 1 },   [B_L3] = { "l3", "L3", BG_GAME, 1 },
    [B_R3] = { "r3", "R3", BG_GAME, 1 },               [B_START] = { "start", "Start", BG_GAME, 1 },
    [B_UP] = { "up", "Up", BG_GAME, 1 },               [B_RIGHT] = { "right", "Right", BG_GAME, 1 },
    [B_DOWN] = { "down", "Down", BG_GAME, 1 },         [B_LEFT] = { "left", "Left", BG_GAME, 1 },
    [B_L2] = { "l2", "L2", BG_GAME, 1 },               [B_R2] = { "r2", "R2", BG_GAME, 1 },
    [B_L1] = { "l1", "L1", BG_GAME, 1 },               [B_R1] = { "r1", "R1", BG_GAME, 1 },
    [B_TRIANGLE] = { "triangle", "Triangle", BG_GAME, 1 }, [B_CIRCLE] = { "circle", "Circle", BG_GAME, 1 },
    [B_CROSS] = { "cross", "Cross", BG_GAME, 1 },      [B_SQUARE] = { "square", "Square", BG_GAME, 1 },
    [B_QUICKTURN] = { "quickturn", "Quick Turn", BG_GAME, 1 },
    [B_QUICKKNIFE] = { "quickknife", "Quick Knife (Hold)", BG_GAME, 1 },
    [B_TRADE] = { "trade", "Trade (Co-op)", BG_GAME, 1 },
    [B_RELOAD] = { "reload", "Reload", BG_GAME, 1 },
    [B_FREEAIM] = { "freeaim", "Free Aim", BG_GAME, 1 },
    [B_TPS_FWD] = { "tps_forward", "Forward", BG_TPS, 0 },   [B_TPS_BACK] = { "tps_back", "Back", BG_TPS, 0 },
    [B_TPS_LEFT] = { "tps_left", "Turn Left", BG_TPS, 0 },   [B_TPS_RIGHT] = { "tps_right", "Turn Right", BG_TPS, 0 },
    [B_TPS_RUN] = { "tps_run", "Run", BG_TPS, 1 },           [B_TPS_ACTION] = { "tps_action", "Action / Confirm", BG_TPS, 1 },
    [B_TPS_CANCEL] = { "tps_cancel", "Cancel", BG_TPS, 1 },  [B_TPS_STATUS] = { "tps_inventory", "Inventory", BG_TPS, 1 },
    [B_TPS_MAP] = { "tps_map", "Map", BG_TPS, 1 },           [B_TPS_AIM] = { "tps_aim", "Aim (Hold)", BG_TPS, 1 },
    [B_TPS_FIRE] = { "tps_fire", "Fire", BG_TPS, 1 },        [B_TPS_RELOAD] = { "tps_reload", "Reload", BG_TPS, 1 },
    [B_TPS_QUICKTURN] = { "tps_quickturn", "Quick Turn", BG_TPS, 1 },
    [B_TPS_KNIFE] = { "tps_knife", "Quick Knife (Hold)", BG_TPS, 2 },
    [B_TPS_OPTIONS] = { "tps_options", "Options", BG_TPS, 2 },
    [B_TPS_RECENTER] = { "tps_recenter", "Center Camera", BG_TPS, 2 },
    [B_TPS_TOGGLE] = { "tps_toggle", "Toggle Third Person", BG_HOTKEY, 0 },
    [B_SETTINGS] = { "settings", "Settings Window", BG_HOTKEY, 0 },
    [B_CHEATS] = { "cheats", "Cheat Menu", BG_HOTKEY, 0 },
    [B_COOP] = { "coop", "Co-op Window", BG_HOTKEY, 0 },
    [B_IMPORT] = { "import", "Import Window", BG_HOTKEY, 0 },
    [B_FULLSCREEN] = { "fullscreen", "Fullscreen", BG_HOTKEY, 0 },
    [B_NOCLIP_UP] = { "noclip_up", "Fly Up", BG_HOTKEY, 0 },
    [B_NOCLIP_DOWN] = { "noclip_down", "Fly Down", BG_HOTKEY, 0 },
};

const int bind_order[B_COUNT] = {
    B_UP, B_DOWN, B_LEFT, B_RIGHT, B_CROSS, B_SQUARE, B_CIRCLE, B_TRIANGLE, B_R1, B_L1, B_R2, B_L2,
    B_START, B_SELECT, B_R3, B_L3, B_FREEAIM, B_QUICKTURN, B_QUICKKNIFE, B_TRADE, B_RELOAD,
    B_TPS_FWD, B_TPS_BACK, B_TPS_LEFT, B_TPS_RIGHT, B_TPS_RUN, B_TPS_ACTION, B_TPS_CANCEL, B_TPS_STATUS,
    B_TPS_MAP, B_TPS_AIM, B_TPS_FIRE, B_TPS_RELOAD, B_TPS_QUICKTURN, B_TPS_KNIFE, B_TPS_OPTIONS, B_TPS_RECENTER,
    B_TPS_TOGGLE, B_SETTINGS, B_CHEATS, B_COOP, B_IMPORT, B_FULLSCREEN, B_NOCLIP_UP, B_NOCLIP_DOWN,
};

static const int defaults[2][B_COUNT][BIND_SLOTS] = {
    [BIND_KB] = {
        [B_SELECT] = { SC(BACKSPACE) }, [B_START] = { SC(RETURN) },
        [B_UP] = { SC(UP) }, [B_RIGHT] = { SC(RIGHT) }, [B_DOWN] = { SC(DOWN) }, [B_LEFT] = { SC(LEFT) },
        [B_L2] = { SC(1) }, [B_R2] = { SC(2) }, [B_L1] = { SC(Q) }, [B_R1] = { SC(W) },
        [B_TRIANGLE] = { SC(S) }, [B_CIRCLE] = { SC(X) }, [B_CROSS] = { SC(Z) }, [B_SQUARE] = { SC(A) },
        [B_TPS_FWD] = { SC(W) }, [B_TPS_BACK] = { SC(S) }, [B_TPS_LEFT] = { SC(A) }, [B_TPS_RIGHT] = { SC(D) },
        [B_TPS_RUN] = { SC(LSHIFT), SC(RSHIFT) }, [B_TPS_ACTION] = { SC(E) }, [B_TPS_CANCEL] = { SC(Q) },
        [B_TPS_STATUS] = { SC(TAB) }, [B_TPS_MAP] = { SC(M) }, [B_TPS_AIM] = { MB(SDL_BUTTON_RIGHT) },
        [B_TPS_FIRE] = { MB(SDL_BUTTON_LEFT) }, [B_TPS_RELOAD] = { SC(R) },
        [B_QUICKKNIFE] = { SC(SPACE) }, [B_TRADE] = { SC(T) }, [B_RELOAD] = { SC(R) },   /* R: no game key in any key type */
        [B_FREEAIM] = { SC(E) },   /* next to aim (W); E is third-person action, which takes it over there */
        [B_TPS_TOGGLE] = { SC(F5) }, [B_SETTINGS] = { SC(F1) }, [B_CHEATS] = { SC(F2) }, [B_COOP] = { SC(F3) }, [B_IMPORT] = { SC(F9) },
        [B_FULLSCREEN] = { SC(F11) }, [B_NOCLIP_UP] = { SC(PAGEUP) }, [B_NOCLIP_DOWN] = { SC(F), SC(PAGEDOWN) },
    },
    [BIND_PAD] = {
        /* L3 is no game action under any key type (R3 is the inventory in A-C): the left stick click trades */
        [B_SELECT] = { GB(BACK) }, [B_TRADE] = { GB(LEFT_STICK) }, [B_R3] = { GB(RIGHT_STICK) }, [B_START] = { GB(START) },
        [B_UP] = { GB(DPAD_UP) }, [B_RIGHT] = { GB(DPAD_RIGHT) }, [B_DOWN] = { GB(DPAD_DOWN) }, [B_LEFT] = { GB(DPAD_LEFT) },
        /* L2 is no game action in key types A-C (OPTION in D), R2 in none: the left trigger aims without auto-aim, the
           right trigger is the quick knife */
        [B_FREEAIM] = { GA(LEFT_TRIGGER, 0) },
        [B_L1] = { GB(LEFT_SHOULDER) }, [B_R1] = { GB(RIGHT_SHOULDER) },
        [B_TRIANGLE] = { GB(NORTH) }, [B_CIRCLE] = { GB(EAST) }, [B_CROSS] = { GB(SOUTH) }, [B_SQUARE] = { GB(WEST) },
        [B_QUICKKNIFE] = { GA(RIGHT_TRIGGER, 0) },
        /* reload: Aim + Triangle (shared with Triangle: bind_held). With the weapon raised Triangle does nothing in
           any key type (map in A-C, options in D: system.c opens neither while st_flg 0x4, the weapon modes) */
        [B_RELOAD] = { GB(NORTH) },
        /* third person, modern scheme (as the RE remakes, the run button where type A has it; reload on the right
           shoulder as in RE4 and the RE4 remake's type C, the quick turn on the d-pad's back) */
        [B_TPS_AIM] = { GA(LEFT_TRIGGER, 0) }, [B_TPS_FIRE] = { GA(RIGHT_TRIGGER, 0) }, [B_TPS_ACTION] = { GB(SOUTH) },
        [B_TPS_CANCEL] = { GB(EAST) }, [B_TPS_RUN] = { GB(WEST) }, [B_TPS_STATUS] = { GB(NORTH) }, [B_TPS_MAP] = { GB(BACK) },
        [B_TPS_RELOAD] = { GB(RIGHT_SHOULDER) }, [B_TPS_QUICKTURN] = { GB(DPAD_DOWN) }, [B_TPS_KNIFE] = { GB(LEFT_SHOULDER) }, [B_TPS_RECENTER] = { GB(RIGHT_STICK) },
        /* the options: Select (type A-C) is the map here and L2 (type D) aim, so OPTION was out of reach; Start, the
           game's second inventory button (Y has it), opens them */
        [B_TPS_OPTIONS] = { GB(START) },
    },
};

static int binds[2][B_COUNT][BIND_SLOTS];
static int loaded;
static const char* swapped;

static void init(void)
{
    if (loaded) return;
    loaded = 1;
    memcpy(binds, defaults, sizeof(binds));
}

/* the toggle key's first slot is g_tps.key (ini "tps_key", F1's "Change toggle key") */
int bind_get(int row, int dev, int slot)
{
    init();
    if (row == B_TPS_TOGGLE && dev == BIND_KB && slot == 0) return g_tps.key;
    return binds[dev][row][slot];
}

static void put(int row, int dev, int slot, int code)
{
    if (row == B_TPS_TOGGLE && dev == BIND_KB && slot == 0) g_tps.key = code;
    else binds[dev][row][slot] = code;
}

/* rows that must not share a code: the game's and the third-person keys may (third person
   takes the keys it uses over, pad.c), hotkeys share with neither */
static int same_context(int a, int b)
{
    int ga = bind_rows[a].group, gb = bind_rows[b].group;
    return !((ga == BG_GAME && gb == BG_TPS) || (ga == BG_TPS && gb == BG_GAME));
}

static int is_reload(int row) { return row == B_RELOAD || row == B_TPS_RELOAD; }

/* a reload row may share a code with another row of its group: then it is a combination, Aim + that
   button (bind_held) */
static int may_share(int a, int b)
{
    return (is_reload(a) || is_reload(b)) && bind_rows[a].group != BG_HOTKEY && bind_rows[b].group != BG_HOTKEY;
}

void bind_set(int row, int dev, int slot, int code)
{
    int old = bind_get(row, dev, slot);
    init();
    swapped = NULL;
    if (code) {
        if (bind_get(row, dev, !slot) == code) put(row, dev, !slot, 0);
        for (int r = 0; r < B_COUNT; r++)
            for (int s = 0; s < BIND_SLOTS; s++)
                if (r != row && same_context(r, row) && !may_share(r, row) && bind_get(r, dev, s) == code) {
                    put(r, dev, s, old);   /* swap */
                    swapped = bind_rows[r].name;
                }
    }
    put(row, dev, slot, code);
}

const char* bind_swapped(void) { return swapped; }

void bind_reset(int dev)
{
    init();
    memcpy(binds[dev], defaults[dev], sizeof(binds[dev]));
    if (dev == BIND_KB) g_tps.key = defaults[BIND_KB][B_TPS_TOGGLE][0];
}

int bind_conflict(int row, int dev)
{
    for (int s = 0; s < BIND_SLOTS; s++) {
        int c = bind_get(row, dev, s);
        if (!c) continue;
        for (int r = 0; r < B_COUNT; r++)
            if (r != row && same_context(r, row) && !may_share(r, row) && bind_row_shown(r, dev))
                for (int t = 0; t < BIND_SLOTS; t++)
                    if (bind_get(r, dev, t) == c) return r + 1;
    }
    return 0;
}

int g_dev_mode = 1;   /* BioRand, the importers (EXTRA, F9): on for everyone since 2026-10-30 (was --dev); 0 hides them again */
int bind_row_shown(int row, int dev)
{
    if (row == B_IMPORT && !g_dev_mode) return 0;
    return dev == BIND_KB ? bind_rows[row].pad_ok != 2 : bind_rows[row].pad_ok != 0;
}

/* ---------------------------------------------------------------- names */
static const char* pad_ini[SDL_GAMEPAD_BUTTON_COUNT] = {
    "south", "east", "west", "north", "back", "guide", "start", "leftstick", "rightstick", "leftshoulder",
    "rightshoulder", "dpup", "dpdown", "dpleft", "dpright", "misc1", "paddle1", "paddle2", "paddle3", "paddle4",
    "touchpad", "misc2", "misc3", "misc4", "misc5", "misc6" };
static const char* axis_ini[SDL_GAMEPAD_AXIS_COUNT] = { "leftx", "lefty", "rightx", "righty", "lefttrigger", "righttrigger" };
static const char* mouse_names[] = { "", "Left", "Middle", "Right", "X1", "X2" };

/* the ini spelling: keyboard as SDL names keys, gamepad positional (south = Cross / A) */
static const char* ini_name(int dev, int code, char* buf, int n)
{
    if (!code) return "none";
    if (dev == BIND_KB) {
        if (code >= BIND_MOUSE) { snprintf(buf, n, "Mouse %s", code - BIND_MOUSE < 6 ? mouse_names[code - BIND_MOUSE] : "?"); return buf; }
        snprintf(buf, n, "%s", SDL_GetScancodeName((SDL_Scancode)code));
        return buf[0] ? buf : "none";
    }
    if (code >= BIND_AXIS) {
        int a = (code - BIND_AXIS) / 2;
        snprintf(buf, n, "%c%s", (code - BIND_AXIS) & 1 ? '-' : '+', a < SDL_GAMEPAD_AXIS_COUNT ? axis_ini[a] : "?");
        return buf;
    }
    return code - 1 < SDL_GAMEPAD_BUTTON_COUNT ? pad_ini[code - 1] : "none";
}

static int ini_code(int dev, const char* s)
{
    if (!*s || !SDL_strcasecmp(s, "none")) return 0;
    if (dev == BIND_KB) {
        if (!SDL_strncasecmp(s, "Mouse ", 6)) {
            for (int i = 1; i < 6; i++) if (!SDL_strcasecmp(s + 6, mouse_names[i])) return BIND_MOUSE + i;
            return -1;
        }
        SDL_Scancode sc = SDL_GetScancodeFromName(s);
        return sc == SDL_SCANCODE_UNKNOWN ? -1 : (int)sc;
    }
    if (*s == '+' || *s == '-')
        for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; a++)
            if (!SDL_strcasecmp(s + 1, axis_ini[a])) return BIND_AXIS + a * 2 + (*s == '-');
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++)
        if (!SDL_strcasecmp(s, pad_ini[b])) return 1 + b;
    return -1;
}

/* The open gamepad belongs to the game thread (pad.c opens, reads and closes it). Other threads
   (the main thread's events and F1) get a copy of what they show: its id, type and name. */
static SDL_Gamepad* gamepad;
static struct { SDL_JoystickID id; SDL_GamepadType type; char name[96]; } pad_info = { 0, SDL_GAMEPAD_TYPE_STANDARD, "" };
static SDL_SpinLock pad_info_lock;
void* bind_gamepad(void) { return gamepad; }
void bind_set_gamepad(void* g)
{
    SDL_JoystickID id = g ? SDL_GetGamepadID((SDL_Gamepad*)g) : 0;
    SDL_GamepadType t = g ? SDL_GetGamepadType((SDL_Gamepad*)g) : SDL_GAMEPAD_TYPE_STANDARD;
    const char* nm = g ? SDL_GetGamepadName((SDL_Gamepad*)g) : NULL;
    char name[96];
    SDL_snprintf(name, sizeof(name), "%s", nm ? nm : "Gamepad");
    gamepad = (SDL_Gamepad*)g;
    SDL_LockSpinlock(&pad_info_lock);
    pad_info.id = id; pad_info.type = t;
    memcpy(pad_info.name, name, sizeof(name));
    SDL_UnlockSpinlock(&pad_info_lock);
}
/* any thread: the open gamepad's id (0: none), type, and name in name (n bytes) if not NULL */
static SDL_JoystickID pad_info_get(SDL_GamepadType* type, char* name, int n)
{
    SDL_JoystickID id;
    SDL_LockSpinlock(&pad_info_lock);
    id = pad_info.id;
    if (type) *type = pad_info.type;
    if (name) SDL_strlcpy(name, pad_info.name, n);
    SDL_UnlockSpinlock(&pad_info_lock);
    return id;
}
static SDL_GamepadType pad_type(void) { SDL_GamepadType t; pad_info_get(&t, NULL, 0); return t; }

/* ---------------------------------------------------------------- input source */
int g_input_source, g_input_pad;

int input_accept(int source, int want, int dev, int player)
{
    if (dev == BIND_KB) return source != INPUT_PAD;
    return source != INPUT_KBM && (!want || player == want - 1);
}

/* SDL numbers gamepads as players: the XInput slot, else the lowest free number when the pad
   appears (also after a reconnect, so a pad keeps its number while the others stay plugged in) */
unsigned bind_pick_pad(void)
{
    int n = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&n);
    SDL_JoystickID r = 0;
    for (int i = 0; ids && i < n && !r; i++)
        if (input_accept(g_input_source, g_input_pad, BIND_PAD, SDL_GetGamepadPlayerIndexForID(ids[i]))) r = ids[i];
    SDL_free(ids);
    return r;
}

const char* bind_pad_choice(int want, char* buf, int n)
{
    int cnt = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&cnt);
    const char* name = tr("not connected");
    if (!want) snprintf(buf, n, "%s", tr("Any"));
    else {
        for (int i = 0; ids && i < cnt; i++)
            if (SDL_GetGamepadPlayerIndexForID(ids[i]) == want - 1) name = SDL_GetGamepadNameForID(ids[i]) ? SDL_GetGamepadNameForID(ids[i]) : tr("Gamepad");
        snprintf(buf, n, "%d: %.24s", want, name);
    }
    SDL_free(ids);
    return buf;
}

void bind_apply_source(void)
{
    /* SDL drops joystick input while none of our windows has keyboard focus unless this is set;
       an SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS environment variable wins over it */
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, g_input_source == INPUT_PAD ? "1" : "0");
}

/* what the player reads on the pad: by the connected pad's family */
const char* bind_code_name(int dev, int code, char* buf, int n)
{
    SDL_GamepadType t = pad_type();
    int ps = t == SDL_GAMEPAD_TYPE_PS3 || t == SDL_GAMEPAD_TYPE_PS4 || t == SDL_GAMEPAD_TYPE_PS5;
    int xb = t == SDL_GAMEPAD_TYPE_XBOX360 || t == SDL_GAMEPAD_TYPE_XBOXONE;
    int nin = t >= SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO && t <= SDL_GAMEPAD_TYPE_GAMECUBE;
    const char* s = NULL;
    if (!code) return "-";
    if (dev == BIND_KB) {
        if (code >= BIND_MOUSE && code - BIND_MOUSE < 6) snprintf(buf, n, tr("Mouse %s"), tr(mouse_names[code - BIND_MOUSE]));
        else ini_name(dev, code, buf, n);
        return buf;
    }
    if (code >= BIND_AXIS) {
        int a = (code - BIND_AXIS) / 2, neg = (code - BIND_AXIS) & 1;
        if (a == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) s = ps ? "L2" : xb ? "LT" : nin ? "ZL" : "Left trigger";
        else if (a == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) s = ps ? "R2" : xb ? "RT" : nin ? "ZR" : "Right trigger";
        else {
            static const char* dir[4][2] = { { "right", "left" }, { "down", "up" }, { "right", "left" }, { "down", "up" } };
            char st[24];
            snprintf(st, sizeof(st), "%s stick %s", a < 2 ? "Left" : "Right", dir[a & 3][neg]);
            snprintf(buf, n, "%s", tr(st));   /* ("Left stick up": a row each) */
            return buf;
        }
        snprintf(buf, n, "%s", tr(s));
        return buf;
    }
    switch (code - 1) {
    case SDL_GAMEPAD_BUTTON_SOUTH: case SDL_GAMEPAD_BUTTON_EAST: case SDL_GAMEPAD_BUTTON_WEST: case SDL_GAMEPAD_BUTTON_NORTH: {
        static const char* lab[] = { "", "A", "B", "X", "Y", "Cross", "Circle", "Square", "Triangle" };
        static const char* pos[] = { "South", "East", "West", "North" };
        SDL_GamepadButtonLabel l = SDL_GetGamepadButtonLabelForType(t, (SDL_GamepadButton)(code - 1));
        s = l > 0 && l < 9 ? lab[l] : pos[code - 1];
        break;
    }
    case SDL_GAMEPAD_BUTTON_BACK: s = t == SDL_GAMEPAD_TYPE_PS3 ? "Select" : t == SDL_GAMEPAD_TYPE_PS4 ? "Share" : t == SDL_GAMEPAD_TYPE_PS5 ? "Create"
                                     : t == SDL_GAMEPAD_TYPE_XBOXONE ? "View" : nin ? "-" : "Back"; break;
    case SDL_GAMEPAD_BUTTON_START: s = t == SDL_GAMEPAD_TYPE_PS4 || t == SDL_GAMEPAD_TYPE_PS5 ? "Options" : t == SDL_GAMEPAD_TYPE_XBOXONE ? "Menu" : nin ? "+" : "Start"; break;
    case SDL_GAMEPAD_BUTTON_GUIDE: s = ps ? "PS" : xb ? "Xbox" : nin ? "Home" : "Guide"; break;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: s = ps ? "L3" : xb ? "LS" : "Left stick press"; break;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: s = ps ? "R3" : xb ? "RS" : "Right stick press"; break;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: s = ps ? "L1" : xb ? "LB" : nin ? "L" : "Left shoulder"; break;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: s = ps ? "R1" : xb ? "RB" : nin ? "R" : "Right shoulder"; break;
    case SDL_GAMEPAD_BUTTON_DPAD_UP: s = "D-pad up"; break;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: s = "D-pad down"; break;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: s = "D-pad left"; break;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: s = "D-pad right"; break;
    case SDL_GAMEPAD_BUTTON_MISC1: s = ps ? "Mic" : xb ? "Share" : nin ? "Capture" : "Misc"; break;
    case SDL_GAMEPAD_BUTTON_TOUCHPAD: s = "Touchpad"; break;
    default: snprintf(buf, n, "%s", ini_name(dev, code, buf, n)); return buf;
    }
    snprintf(buf, n, "%s", tr(s));
    return buf;
}

const char* bind_pad_desc(char* d, int n)
{
    if (g_input_source == INPUT_KBM) return tr("Gamepads disabled. Change Input Devices to use one.");
    if (pad_info_get(NULL, d, n)) return d;
    if (g_input_pad) { snprintf(d, n, tr("Gamepad %d not connected"), g_input_pad); return d; }
    return tr("No gamepad connected");
}

/* game rows: what the DS2 button does under the game's key type (pad.c bhSetPad's virtual
   bits; 0x10000 skip and 0x20000 pause are left out, they come with others) */
const char* bind_row_label(int row, char* buf, int n)
{
    static const char* vnames[16] = { "Forward", "Back", "Turn Left", "Turn Right", "Aim", "Aim Up", "Aim Down",
                                      "Change Aim", "Fire", "Examine", "Run", "Confirm", "Cancel", "Map", "Inventory", "Options" };
    int len = 0;
    if (row >= 16) { snprintf(buf, n, "%s", tr(bind_rows[row].name)); return buf; }
    buf[0] = 0;
    for (int v = 0; v < 16; v++) {
        int m = pc_keytype_mask(v);
        if (m < 0) m = 0;
        if (v == 9 && (pc_keytype_mask(8) & (1 << row))) continue;   /* examine goes with fire */
        if (m & (1 << row)) len += snprintf(buf + len, len < n ? n - len : 0, "%s%s", len ? " / " : "", tr(vnames[v]));
    }
    if (!len) len = snprintf(buf, n, "%s", tr("Not used"));
    snprintf(buf + len, len < n ? n - len : 0, " (%s)", tr(bind_rows[row].name));
    return buf;
}

/* ---------------------------------------------------------------- recvx.ini */
int bind_ini_line(const char* key, const char* value)
{
    static int knife_seen, trade_seen, reload_seen, combo_seen, version;   /* the file has quick-knife / trade / reload lines (written before the third-person rows) */
    int dev, slot = 0, code;
    char k[64];
    /* a file from before the reload combination (no bindings_version line, written first): the reload rows' "none",
       their default then, gets today's defaults (R, Aim + Triangle) */
    if (!strcmp(key, "bindings_version")) { combo_seen = 1; version = atoi(value); return 1; }
    /* a file from before free aim (bindings_version < 3): the left trigger, L2's default then, is free aim's now */
    if (version < 3 && !strcmp(key, "pad_l2") && !SDL_strcasecmp(value, "+lefttrigger")) return 1;
    if (!combo_seen && (!strcmp(key, "key_reload") || !strcmp(key, "pad_reload")) && !SDL_strcasecmp(value, "none")) return 1;
    if (!strncmp(key, "key_quickknife", 14)) knife_seen = 1;
    /* a file from before the quick knife: Space was third-person action's default second key, now the quick knife's */
    if (!knife_seen && !strcmp(key, "key_tps_action_2") && !SDL_strcasecmp(value, "Space")) return 1;
    if (!knife_seen && !strcmp(key, "pad_r2") && !SDL_strcasecmp(value, "+righttrigger")) return 1;   /* likewise R2's right trigger */
    /* a file from before the trade request (no key_trade line; key_ lines come first): the left stick click,
       L3's default then, is the trade request's now */
    if (!strncmp(key, "key_trade", 9)) trade_seen = 1;
    if (!trade_seen && !strcmp(key, "pad_l3") && !SDL_strcasecmp(value, "leftstick")) return 1;
    /* a file from before the reload key (key_tps_reload comes before the hotkeys and every pad_ line): R was noclip fly
       up's first key and the right shoulder the third-person quick turn's, both the reload's now */
    if (!strncmp(key, "key_tps_reload", 14)) reload_seen = 1;
    if (!reload_seen && !strcmp(key, "key_noclip_up") && !SDL_strcasecmp(value, "R")) { init(); put(B_NOCLIP_UP, BIND_KB, 0, 0); return 1; }
    if (!reload_seen && !strcmp(key, "pad_tps_quickturn") && !SDL_strcasecmp(value, "rightshoulder")) return 1;
    if (!strncmp(key, "key_", 4)) dev = BIND_KB;
    else if (!strncmp(key, "pad_", 4)) dev = BIND_PAD;
    else return 0;
    snprintf(k, sizeof(k), "%s", key + 4);
    if (strlen(k) > 2 && !strcmp(k + strlen(k) - 2, "_2")) { slot = 1; k[strlen(k) - 2] = 0; }
    for (int r = 0; r < B_COUNT; r++) {
        if (strcmp(k, bind_rows[r].ini) || !bind_row_shown(r, dev)) continue;
        code = ini_code(dev, value);
        if (code < 0) { host_log("recvx.ini: %s: unknown %s \"%s\"\n", key, dev ? "gamepad input" : "key", value); return 1; }
        init();
        put(r, dev, slot, code);
        /* (and a key such a file gave another row keeps it: free aim's default gives way) */
        if (version < 3 && code && r != B_FREEAIM && same_context(r, B_FREEAIM) && !may_share(r, B_FREEAIM) && bind_get(B_FREEAIM, dev, 0) == code)
            put(B_FREEAIM, dev, 0, 0);
        return 1;
    }
    return 0;
}

void bind_ini_write(void* fp)
{
    FILE* f = (FILE*)fp;
    char b[64];
    fprintf(f, "# controls (F1 -> Controls, OPTION -> ENHANCED SETTINGS -> CONTROLS): key_* keyboard/mouse, pad_* gamepad;\n"
               "# the game rows are DS2 buttons, which the game's KEY CONFIG type turns into actions.\n"
               "# Keys as SDL names them (Z, Return, Left Shift, Mouse Right); gamepad: south/east/west/north\n"
               "# (Xbox A/B/X/Y, PlayStation Cross/Circle/Square/Triangle), back, start, leftshoulder, dpup, ...,\n"
               "# +lefttrigger, -lefty (left stick up). A second binding: <name>_2. none = unbound.\n"
               "# reload may share a button with a game row (third person: a third-person row): Aim + that button.\n"
               "bindings_version = 3\n");
    for (int dev = 0; dev < 2; dev++)
        for (int i = 0; i < B_COUNT; i++) {
            int r = bind_order[i];
            if (!bind_row_shown(r, dev)) continue;
            fprintf(f, "%s_%s = %s\n", dev ? "pad" : "key", bind_rows[r].ini, ini_name(dev, bind_get(r, dev, 0), b, sizeof(b)));
            if (bind_get(r, dev, 1) || defaults[dev][r][1])
                fprintf(f, "%s_%s_2 = %s\n", dev ? "pad" : "key", bind_rows[r].ini, ini_name(dev, bind_get(r, dev, 1), b, sizeof(b)));
        }
}

/* ---------------------------------------------------------------- state */
/* RECVX_KEYS (test aid, below), parsed once by whichever thread comes first */
typedef struct KeyStep { uint64_t at; int click, n; float x, y; short keys[8]; } KeyStep;
static KeyStep steps[256];
static int nsteps;
static volatile int parse_state;   /* 0 not yet, 1 parsing, 2 done */

static void parse_keys(void)
{
    int z = 0;
    const char* p;
    if (__atomic_load_n(&parse_state, __ATOMIC_ACQUIRE) == 2) return;
    if (!__atomic_compare_exchange_n(&parse_state, &z, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(&parse_state, __ATOMIC_ACQUIRE) != 2) SDL_Delay(1);
        return;
    }
    for (p = SDL_getenv("RECVX_KEYS"); p && *p && nsteps < 256;) {
        char* e;
        KeyStep* st = &steps[nsteps];
        st->at = strtoull(p, &e, 10);
        if (*e != ':') break;
        p = e + 1;
        if (*p == '@') {
            st->click = SDL_BUTTON_LEFT;
            st->x = strtof(p + 1, &e);
            st->y = *e == ':' ? strtof(e + 1, &e) : 0;
            if (*e == ':' && e[1] == 'r') { st->click = SDL_BUTTON_RIGHT; e += 2; }
            p = e;
        } else
            while (*p && *p != ',') {
                char name[48];
                int n = (int)strcspn(p, "+,");
                snprintf(name, sizeof(name), "%.*s", n, p);
                for (char* c = name; *c; c++) if (*c == '_') *c = ' ';
                if (strcmp(name, "-")) {
                    int sc = SDL_strncasecmp(name, "Mouse ", 6) ? (int)SDL_GetScancodeFromName(name) : ini_code(BIND_KB, name);   /* Mouse_Right: held */
                    if (sc > 0 && st->n < 8) st->keys[st->n++] = (short)sc;
                    else host_log("RECVX_KEYS: unknown key \"%s\"\n", name);
                }
                p += n + (p[n] == '+');
            }
        nsteps++;
        p += strcspn(p, ",");
        if (*p == ',') p++;
    }
    __atomic_store_n(&parse_state, 2, __ATOMIC_RELEASE);
}

/* the scripted keys held at this frame: the last key step at or before it */
static const KeyStep* key_step(uint64_t now)
{
    const KeyStep* r = NULL;
    for (int i = 0; i < nsteps && steps[i].at <= now; i++) if (!steps[i].click) r = &steps[i];
    return r;
}

/* the keyboard state, plus the scripted keys of this frame (read like RECVX_INPUT) */
const bool* bind_keys(void)
{
    static bool merged[SDL_SCANCODE_COUNT];
    const bool* k = SDL_GetKeyboardState(NULL);
    const KeyStep* st;
    parse_keys();
    if (!nsteps) return k;
    memcpy(merged, k, sizeof(merged));
    if ((st = key_step(gs_frame_count())))
        for (int i = 0; i < st->n; i++) if (st->keys[i] < SDL_SCANCODE_COUNT) merged[st->keys[i]] = 1;
    return merged;
}

/* ---------------------------------------------------------------- quick presses in menus
   The game reads the pad once a game frame (30 a second): a key tapped between two reads, or let go
   and pressed again between them, was never seen. With "Smooth menus", while a menu is up, every
   key and gamepad button change (main thread, in order) is shown to at least one pad report: each
   report (bind_latch_frame) takes at most one queued change per key or button and holds that state
   for the report; the rest waits for the next. Keys without queued changes read as they are. */
typedef struct { short code; unsigned char pad, down; } LatchEv;
static LatchEv latch_q[128];
static int latch_n;
static SDL_SpinLock latch_lock;
static signed char latch_key[SDL_SCANCODE_COUNT], latch_btn[SDL_GAMEPAD_BUTTON_COUNT];   /* this report: -1 as it is, 0 up, 1 down */
static int latch_on;   /* game thread: during the pad report */

static void latch_push(int pad, int code, int down)
{
    SDL_LockSpinlock(&latch_lock);
    if (latch_n < (int)(sizeof(latch_q) / sizeof(latch_q[0]))) latch_q[latch_n++] = (LatchEv){ (short)code, (unsigned char)pad, (unsigned char)down };
    SDL_UnlockSpinlock(&latch_lock);
}

void bind_latch_event(const void* e)
{
    const SDL_Event* ev = (const SDL_Event*)e;
    if ((ev->type == SDL_EVENT_KEY_DOWN || ev->type == SDL_EVENT_KEY_UP) && !ev->key.repeat && ev->key.scancode < SDL_SCANCODE_COUNT)
        latch_push(0, ev->key.scancode, ev->type == SDL_EVENT_KEY_DOWN);
    else if ((ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || ev->type == SDL_EVENT_GAMEPAD_BUTTON_UP) &&
             ev->gbutton.which == pad_info_get(NULL, NULL, 0) && ev->gbutton.which && ev->gbutton.button < SDL_GAMEPAD_BUTTON_COUNT)
        latch_push(1, ev->gbutton.button, ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
}

const bool* bind_latch_frame(int on, const bool* keys)
{
    static bool merged[SDL_SCANCODE_COUNT];
    int w = 0;
    memset(latch_key, -1, sizeof(latch_key));
    memset(latch_btn, -1, sizeof(latch_btn));
    SDL_LockSpinlock(&latch_lock);
    for (int i = 0; i < latch_n && on; i++) {
        signed char* s = latch_q[i].pad ? &latch_btn[latch_q[i].code] : &latch_key[latch_q[i].code];
        if (*s < 0) *s = (signed char)latch_q[i].down;
        else latch_q[w++] = latch_q[i];   /* a second change of this key: the next report */
    }
    latch_n = w;   /* off: dropped */
    SDL_UnlockSpinlock(&latch_lock);
    latch_on = on;
    if (!on) return keys;
    memcpy(merged, keys, sizeof(merged));
    for (int i = 0; i < SDL_SCANCODE_COUNT; i++) if (latch_key[i] >= 0) merged[i] = latch_key[i];
    return merged;
}
void bind_latch_end(void) { latch_on = 0; }

static int tps_uses(int dev, int code)
{
    for (int r = B_TPS_FWD; r <= B_TPS_RECENTER; r++)
        for (int s = 0; s < BIND_SLOTS; s++)
            if (bind_row_shown(r, dev) && bind_get(r, dev, s) == code) return 1;
    return 0;
}

static int pad_down(int c)
{
    if (c >= BIND_AXIS) {
        int a = (c - BIND_AXIS) / 2, v = SDL_GetGamepadAxis(gamepad, (SDL_GamepadAxis)a);
        int lim = a >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? 8000 : 16000;
        return (c - BIND_AXIS) & 1 ? v < -lim : v > lim;
    }
    if (latch_on && c >= 1 && c <= SDL_GAMEPAD_BUTTON_COUNT && latch_btn[c - 1] >= 0) return latch_btn[c - 1];
    return SDL_GetGamepadButton(gamepad, (SDL_GamepadButton)(c - 1));
}

/* the gamepad: in third-person play with the modern scheme (g_tps.pad_play, tps_glue.c) its
   third-person rows play and take the inputs they use over from the game rows; elsewhere (menus,
   events, doors) the game rows, as the KEY CONFIG type sets them. An input held when that
   changes counts for neither until it is let go (a button that opened the inventory does not
   also press its DS2 button there). Game thread. */
static int pad_row_reads(int row, int c)
{
    static int was;
    static unsigned char stale[BIND_AXIS + 2 * SDL_GAMEPAD_AXIS_COUNT];
    int take = g_tps.pad_play;
    if (take != was) {
        was = take;
        for (int i = 1; i < (int)sizeof(stale); i++)
            stale[i] = (i < 1 + SDL_GAMEPAD_BUTTON_COUNT || i >= BIND_AXIS) && tps_uses(BIND_PAD, i) && pad_down(i);
    }
    if (c < (int)sizeof(stale) && stale[c]) {
        if (pad_down(c)) return 0;
        stale[c] = 0;
    }
    if (bind_rows[row].group == BG_TPS) return take;
    return !(take && tps_uses(BIND_PAD, c));
}

/* code c of the row is down, for that row */
static int code_down(int row, int dev, int c, const bool* keys, int mouse)
{
    if (dev == BIND_KB) {
        if (!input_accept(g_input_source, 0, BIND_KB, 0)) return 0;   /* Gamepad only */
        /* third person takes the keys it uses over (W/A/S/Q by default), in a game outside OPTION (kb_play) */
        if (bind_rows[row].group == BG_GAME && g_tps.kb_play && tps_uses(BIND_KB, c)) return 0;
        return c >= BIND_MOUSE ? (mouse & SDL_BUTTON_MASK(c - BIND_MOUSE)) != 0 : c < SDL_SCANCODE_COUNT && keys[c];
    }
    return gamepad && pad_row_reads(row, c) && pad_down(c);
}

static int raw_held(int row, int dev, const bool* keys, int mouse)
{
    for (int s = 0; s < BIND_SLOTS; s++) {
        int c = bind_get(row, dev, s);
        if (c && code_down(row, dev, c, keys, mouse)) return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- reload combination
   A reload binding that another row of its group also has (bind_combo) reloads only as Aim + that button:
   a press while aim is held, in play (reload_glue.c), belongs to the reload until it is let go, and no
   other row sees it (no one-frame fire, map, knife...), also when aim is let go first. A press without
   aim is the other row's until let go. Aim: the game rows the key type has aim on (R1) and free aim (fixed
   cameras), or in third person the third-person aim. Game thread: bind_combo_frame each pad report (pad.c), before the rows
   are read. */
int reload_combo_ok(void);   /* reload_glue.c: our player in normal play */
static unsigned char combo_mode[2][2][BIND_SLOTS];   /* [B_TPS_RELOAD?][dev][slot]: 0 up, 1 the other row's, 2 the reload's */

static int aim_row(int row, int dev)
{
    int m = pc_keytype_mask(4);
    if (row == B_FREEAIM) return !g_tps.on;
    return row < 16 ? m > 0 && (m & (1 << row)) : row == B_TPS_AIM && (dev == BIND_PAD || g_tps.on);
}

int bind_combo(int row, int dev, int slot)
{
    int c = is_reload(row) ? bind_get(row, dev, slot) : 0, shared = 0;
    for (int r = 0; c && r < B_COUNT; r++) {
        if (r == row || is_reload(r) || bind_rows[r].group != bind_rows[row].group || !bind_row_shown(r, dev)) continue;
        for (int s = 0; s < BIND_SLOTS; s++)
            if (bind_get(r, dev, s) == c) {
                if (aim_row(r, dev)) return 0;   /* on the aim button itself: no combination, it reloads */
                shared = 1;
            }
    }
    return shared;
}

void bind_combo_frame(int on, const bool* keys, int mouse)
{
    int aim = -1;
    for (int i = 0; i < 2; i++)
        for (int dev = 0; dev < 2; dev++)
            for (int s = 0; s < BIND_SLOTS; s++) {
                int row = i ? B_TPS_RELOAD : B_RELOAD, c = bind_get(row, dev, s);
                unsigned char* m = &combo_mode[i][dev][s];
                if (!on || !c || (i && dev == BIND_KB && !g_tps.on) || !bind_combo(row, dev, s) || !code_down(row, dev, c, keys, mouse)) {
                    *m = 0;   /* (the third-person keys play only in third person) */
                    continue;
                }
                if (*m) continue;
                if (aim < 0) {
                    aim = 0;
                    for (int r = 0; r < B_COUNT && !aim; r++)
                        for (int d = 0; d < 2 && !aim; d++)
                            aim = aim_row(r, d) && raw_held(r, d, keys, d == BIND_KB ? mouse : 0);
                    aim = aim && reload_combo_ok();
                }
                *m = aim ? 2 : 1;
            }
}

/* code c of dev is a reload combination's press */
static int combo_taken(int dev, int c)
{
    for (int i = 0; i < 2; i++)
        for (int s = 0; s < BIND_SLOTS; s++)
            if (combo_mode[i][dev][s] == 2 && bind_get(i ? B_TPS_RELOAD : B_RELOAD, dev, s) == c) return 1;
    return 0;
}

int bind_held(int row, int dev, const bool* keys, int mouse)
{
    for (int s = 0; s < BIND_SLOTS; s++) {
        int c = bind_get(row, dev, s);
        if (!c) continue;
        if (is_reload(row) ? bind_combo(row, dev, s) && combo_mode[row == B_TPS_RELOAD][dev][s] != 2 : combo_taken(dev, c)) continue;
        if (code_down(row, dev, c, keys, mouse)) return 1;
    }
    return 0;
}

int bind_pad_raw(int row)
{
    for (int s = 0; s < BIND_SLOTS; s++) {
        int c = bind_get(row, BIND_PAD, s);
        if (c && gamepad && pad_down(c)) return 1;
    }
    return 0;
}

int bind_hotkey(const void* e, int row)
{
    const SDL_Event* ev = (const SDL_Event*)e;
    for (int s = 0; s < BIND_SLOTS; s++) {
        int c = bind_get(row, BIND_KB, s);
        if (!c) continue;
        if (ev->type == SDL_EVENT_KEY_DOWN && !ev->key.repeat && (int)ev->key.scancode == c) return 1;
        if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN && c == BIND_MOUSE + ev->button.button) return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- capture */
#define CAPTURE_MS 5000
static volatile int cap_dev = -1, cap_result = -1;
static Uint64 cap_start;
static bool cap_held[SDL_SCANCODE_COUNT], cap_held_pad[SDL_GAMEPAD_BUTTON_COUNT];   /* down when it started: not this */

void bind_capture_start(int dev)
{
    const bool* k = bind_keys();
    for (int i = 0; i < SDL_SCANCODE_COUNT; i++) cap_held[i] = k[i];
    SDL_JoystickID id = pad_info_get(NULL, NULL, 0);   /* (any thread: SDL looks it up by id, under its lock) */
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++) cap_held_pad[b] = id && SDL_GetGamepadButton(SDL_GetGamepadFromID(id), (SDL_GamepadButton)b);
    cap_start = SDL_GetTicks();
    cap_result = -1;
    cap_dev = dev;
}

int bind_capture_poll(void)
{
    if (cap_dev >= 0 && SDL_GetTicks() - cap_start > CAPTURE_MS) { cap_dev = -1; cap_result = -2; }
    return cap_dev >= 0 ? -1 : cap_result;
}

int bind_capture_left_ms(void)
{
    Uint64 t = SDL_GetTicks() - cap_start;
    return cap_dev < 0 || t > CAPTURE_MS ? 0 : (int)(CAPTURE_MS - t);
}

static void done(int r) { cap_result = r; cap_dev = -1; }

int bind_event(const void* e)
{
    const SDL_Event* ev = (const SDL_Event*)e;
    int dev = cap_dev;
    if (dev < 0) return 0;
    switch (ev->type) {
    case SDL_EVENT_KEY_DOWN:
        if (ev->key.repeat || cap_held[ev->key.scancode]) return 1;
        if (ev->key.scancode == SDL_SCANCODE_ESCAPE) done(-2);
        else if (ev->key.scancode == SDL_SCANCODE_DELETE) done(-3);
        else if (dev == BIND_KB) done(ev->key.scancode);
        return 1;
    case SDL_EVENT_KEY_UP:
        cap_held[ev->key.scancode] = 0;
        return 1;
    case SDL_EVENT_TEXT_INPUT:
        return 1;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        /* not the click that started it (overlay buttons act on release) */
        if (dev == BIND_KB && SDL_GetTicks() - cap_start > 250) done(BIND_MOUSE + ev->button.button);
        return dev == BIND_KB;
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        if (ev->gbutton.button < SDL_GAMEPAD_BUTTON_COUNT) cap_held_pad[ev->gbutton.button] = 0;
        return 1;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        if (dev == BIND_PAD && ev->gbutton.button < SDL_GAMEPAD_BUTTON_COUNT && !cap_held_pad[ev->gbutton.button]) done(1 + ev->gbutton.button);
        return 1;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (dev == BIND_PAD && (ev->gaxis.value > 20000 || (ev->gaxis.value < -20000 && ev->gaxis.axis < SDL_GAMEPAD_AXIS_LEFT_TRIGGER)))
            done(BIND_AXIS + ev->gaxis.axis * 2 + (ev->gaxis.value < 0));
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- test aids (main thread)
   RECVX_KEYS="<frame>:<key>+<key>,<frame>:-,<frame>:@<x>:<y>": hold keyboard keys (SDL names,
   '_' for a space; Mouse_Left .. Mouse_X2 a mouse button, as events) from that presented frame on,
   as key state and key events; @x:y clicks the left mouse button at window pixel x/y (as events:
   the overlay), @x:y:r the right one.
   RECVX_VPAD="<frame>:<input>+<input>,...": a virtual SDL gamepad (RECVX_VPAD_TYPE xbox, ps5 or
   generic; default xbox) holding gamepad inputs in the ini spelling (south, dpup, +lefttrigger),
   or an axis at a value from -1 to 1 (leftx=0.5, lefty=-1: the left stick half right, all up);
   "unplug" disconnects it, "plug" connects it again (hotplug tests); RECVX_VPAD_RUMBLE=0: it has no motors. */
static void push_key(int sc, int down)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.timestamp = SDL_GetTicksNS();
    e.key.scancode = (SDL_Scancode)sc;
    e.key.key = SDL_GetKeyFromScancode((SDL_Scancode)sc, SDL_KMOD_NONE, false);
    e.key.down = down;
    e.key.windowID = SDL_GetWindowID(SDL_GetKeyboardFocus());
    SDL_PushEvent(&e);
}

static void push_click(float x, float y, int down, int button)
{
    extern SDL_Window* g_window;
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.windowID = SDL_GetWindowID(g_window);
    e.motion.x = x; e.motion.y = y;
    SDL_PushEvent(&e);
    if (down < 0) return;
    SDL_zero(e);
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.windowID = SDL_GetWindowID(g_window);
    e.button.button = (Uint8)button;
    e.button.down = down;
    e.button.clicks = 1;
    e.button.x = x; e.button.y = y;
    SDL_PushEvent(&e);
}

static const char* script_entry(const char* p, uint64_t now, const char** body)
{
    char* e;
    unsigned long at = strtoul(p, &e, 10);
    if (*e != ':' || at > now) return NULL;
    *body = e + 1;
    return e + 1 + strcspn(e + 1, ",");
}

/* the same steps as key events (captures, hotkeys, the overlay) and clicks */
static void keys_frame(uint64_t now)
{
    static bool down[BIND_MOUSE + 6];   /* keys, then mouse buttons */
    static int next, click_up;
    static float cx, cy;
    const KeyStep* st;
    parse_keys();
    if (click_up) { push_click(cx, cy, 0, click_up); click_up = 0; }
    for (; next < nsteps && steps[next].at <= now; next++)
        if (steps[next].click) { cx = steps[next].x; cy = steps[next].y; push_click(cx, cy, 1, steps[next].click); click_up = steps[next].click; }
    if (!nsteps) return;
    st = key_step(now);
    for (int i = 0; i < BIND_MOUSE + 6; i++) {
        bool want = 0;
        for (int k = 0; st && k < st->n; k++) want |= st->keys[k] == i;
        if (want != down[i]) { down[i] = want; if (i < BIND_MOUSE) push_key(i, want); else push_click(cx, cy, want, i - BIND_MOUSE); }
    }
}

/* the virtual gamepad has motors (the game's VIBRATION can be chosen): what SDL sends them is logged */
static bool SDLCALL vpad_rumble(void* ud, Uint16 lo, Uint16 hi)
{
    (void)ud;
    host_log("test: virtual gamepad rumble low %u high %u\n", lo, hi);
    return true;
}

static SDL_Joystick* vpad_plug(void)
{
    const char* t = SDL_getenv("RECVX_VPAD_TYPE");
    SDL_VirtualJoystickDesc d;
    SDL_Joystick* joy;
    SDL_INIT_INTERFACE(&d);
    d.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    d.naxes = SDL_GAMEPAD_AXIS_COUNT;
    d.nbuttons = SDL_GAMEPAD_BUTTON_MISC1;   /* the standard ones */
    d.button_mask = (1u << SDL_GAMEPAD_BUTTON_MISC1) - 1;
    d.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    if (!SDL_getenv("RECVX_VPAD_RUMBLE") || SDL_atoi(SDL_getenv("RECVX_VPAD_RUMBLE"))) d.Rumble = vpad_rumble;
    if (t && !strcmp(t, "ps5")) { d.vendor_id = 0x054c; d.product_id = 0x0ce6; d.name = "Virtual DualSense"; }
    else if (t && !strcmp(t, "generic")) d.name = "Virtual gamepad";
    else { d.vendor_id = 0x045e; d.product_id = 0x02ea; d.name = "Virtual Xbox One"; }
    joy = SDL_OpenJoystick(SDL_AttachVirtualJoystick(&d));
    host_log("test: virtual gamepad %s\n", joy ? d.name : SDL_GetError());
    if (joy) SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32768),
             SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768);
    return joy;
}

static void vpad_frame(uint64_t now)
{
    static const char* p; static int chk;
    static SDL_Joystick* joy;
    const char* body;
    const char* end;
    if (!chk++ && (p = SDL_getenv("RECVX_VPAD"))) joy = vpad_plug();
    while (p && *p && (end = script_entry(p, now, &body))) {
        if (!strncmp(body, "unplug", 6) || !strncmp(body, "plug", 4)) {
            if (joy) { SDL_JoystickID id = SDL_GetJoystickID(joy); SDL_CloseJoystick(joy); SDL_DetachVirtualJoystick(id); joy = NULL; host_log("test: virtual gamepad unplugged\n"); }
            if (*body == 'p') joy = vpad_plug();
            p = *end ? end + 1 : end;
            continue;
        }
        if (!joy) { p = *end ? end + 1 : end; continue; }
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_MISC1; b++) SDL_SetJoystickVirtualButton(joy, b, false);
        for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; a++) SDL_SetJoystickVirtualAxis(joy, a, a >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? -32768 : 0);
        while (body < end) {
            char name[32];
            int n = (int)strcspn(body, "+,"), c;
            if (*body == '+' || *body == '-') n = 1 + (int)strcspn(body + 1, "+,");   /* +lefttrigger */
            snprintf(name, sizeof(name), "%.*s", n, body);
            char* eq = strchr(name, '=');
            if (eq) *eq = 0;   /* leftx=0.5: an axis at a value (-1..1) */
            c = eq ? 0 : strcmp(name, "-") ? ini_code(BIND_PAD, name) : 0;
            if (eq && SDL_GetGamepadAxisFromString(name) != SDL_GAMEPAD_AXIS_INVALID)
                SDL_SetJoystickVirtualAxis(joy, SDL_GetGamepadAxisFromString(name), (Sint16)SDL_clamp(SDL_atof(eq + 1) * 32767.0, -32768, 32767));
            else if (eq) host_log("RECVX_VPAD: unknown axis \"%s\"\n", name);
            if (c > 0 && c < BIND_AXIS) SDL_SetJoystickVirtualButton(joy, c - 1, true);
            else if (c >= BIND_AXIS) SDL_SetJoystickVirtualAxis(joy, (c - BIND_AXIS) / 2, (c - BIND_AXIS) & 1 ? -32768 : 32767);
            else if (c < 0) host_log("RECVX_VPAD: unknown input \"%s\"\n", name);
            body += n + (body[n] == '+');
        }
        p = *end ? end + 1 : end;
    }
}

/* RECVX_UNFOCUS="<frame>:1,<frame>:0": hide the window (SDL drops its keyboard focus, as when another
   window is clicked), show it again (focus back) */
static void focus_frame(uint64_t now)
{
    extern SDL_Window* g_window;
    static const char* p; static int chk;
    const char *body, *end;
    if (!chk++) p = SDL_getenv("RECVX_UNFOCUS");
    while (p && *p && (end = script_entry(p, now, &body))) {
        if (*body == '1') SDL_HideWindow(g_window); else SDL_ShowWindow(g_window);
        host_log("test: frame %llu window %s, keyboard focus %s\n", (unsigned long long)now, *body == '1' ? "hidden" : "shown",
                 SDL_GetKeyboardFocus() ? "here" : "none");
        p = *end ? end + 1 : end;
    }
}

void bind_test_frame(void)
{
    static long long stop = -2;
    uint64_t now = gs_frame_count();
    if (stop == -2) stop = SDL_getenv("RECVX_EXIT_FRAME") ? SDL_atoi(SDL_getenv("RECVX_EXIT_FRAME")) : -1;
    if (stop >= 0 && (long long)now >= stop) { host_log("test: exit at frame %llu\n", (unsigned long long)now); exit(0); }   /* RECVX_EXIT_FRAME */
    keys_frame(now);
    vpad_frame(now);
    focus_frame(now);
}

/* any key or gamepad button down (the native menu waits for this after a capture) */
int bind_any_down(void)
{
    const bool* k = bind_keys();
    for (int i = 1; i < SDL_SCANCODE_COUNT; i++) if (k[i]) return 1;
    if (gamepad) {
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++) if (SDL_GetGamepadButton(gamepad, (SDL_GamepadButton)b)) return 1;
        for (int c = BIND_AXIS; c < BIND_AXIS + 2 * SDL_GAMEPAD_AXIS_COUNT; c++) if (pad_down(c)) return 1;   /* triggers and sticks too: a trigger just bound is still held */
    }
    return 0;
}
