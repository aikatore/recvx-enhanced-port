/* Control bindings (pc/src/input/bindings.c, docs/ARCHITECTURE.md "Input"): which keyboard keys,
   mouse buttons and gamepad inputs press the DualShock 2 buttons the game reads (pad.c), the
   third-person keys (tps_input.c) and the port's hotkeys. The game's own key type (KEY CONFIG
   A-D) then turns the DS2 buttons into game actions, so a row is named after what its button
   does under the current key type. Two sets: keyboard/mouse and gamepad, two slots per row.
   Edited by the F1 overlay (main thread) and the native OPTION menu (game thread). */
#ifndef RECVX_BINDINGS_H
#define RECVX_BINDINGS_H
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

enum {
    /* DS2 buttons, in the bit order of the pad report (pad.c PAD_*) */
    B_SELECT, B_L3, B_R3, B_START, B_UP, B_RIGHT, B_DOWN, B_LEFT,
    B_L2, B_R2, B_L1, B_R1, B_TRIANGLE, B_CIRCLE, B_CROSS, B_SQUARE,
    B_QUICKTURN,                       /* back, then the run button (the game's quick turn) */
    B_QUICKKNIFE,                      /* hold: knife drawn and readied, the weapon before back on release (knife_glue.c) */
    B_TRADE,                           /* co-op: trade request / accept with the partner next to you (coop_glue.c, trade_glue.c) */
    B_RELOAD,                          /* reload the weapon from the inventory's ammo, as on an empty trigger pull (reload_glue.c); R, Aim + Triangle */
    B_FREEAIM,                         /* fixed cameras: aim as the aim button, without the game's turn to a target (pad.c, modern_glue.c); E, LT */
    B_WPN_HOLD,                        /* hold: the weapon shortcuts on the d-pad (weapon_glue.c); in the inventory: their screen. C, R3 */
    B_WPN1, B_WPN2, B_WPN3, B_WPN4, B_WPN5, B_WPN6, B_WPN7, B_WPN8,   /* equip the weapon of shortcut n (weapon_glue.c); 1-8 */
    B_WPN_PAGE,                        /* the wheel's other page (5-8 / 1-4) while it is up; E, RB. Shares with any game or
                                          third-person row: the wheel takes it while up, those have it otherwise */
    /* third person: keyboard and mouse; the gamepad set has its own (modern scheme, g_tps.pad_modern:
       run .. quick turn, knife, options, recenter; the classic scheme drives it through the DS2 rows) */
    B_TPS_FWD, B_TPS_BACK, B_TPS_LEFT, B_TPS_RIGHT, B_TPS_RUN, B_TPS_ACTION, B_TPS_CANCEL,
    B_TPS_STATUS, B_TPS_MAP, B_TPS_AIM, B_TPS_FIRE, B_TPS_RELOAD, B_TPS_QUICKTURN, B_TPS_KNIFE, B_TPS_OPTIONS, B_TPS_RECENTER,
    /* port hotkeys (keyboard) */
    B_TPS_TOGGLE, B_SETTINGS, B_CHEATS, B_COOP, B_IMPORT, B_FULLSCREEN, B_NOCLIP_UP, B_NOCLIP_DOWN,
    B_COUNT
};
enum { BIND_KB, BIND_PAD };            /* device sets */
enum { BG_GAME, BG_TPS, BG_HOTKEY };   /* row groups */
#define BIND_SLOTS 2
/* codes: keyboard set 1..511 SDL scancode, BIND_MOUSE + n mouse button n; gamepad set
   1 + SDL_GamepadButton, BIND_AXIS + axis * 2 + (0 positive, 1 negative); 0 = none */
#define BIND_MOUSE 512
#define BIND_AXIS 64

typedef struct BindRow { const char* ini; const char* name; int group; int pad_ok; } BindRow;   /* pad_ok: 0 keyboard only, 1 both sets, 2 gamepad only */
extern const BindRow bind_rows[B_COUNT];
extern const int bind_order[B_COUNT];  /* display order */

int  bind_get(int row, int dev, int slot);
void bind_set(int row, int dev, int slot, int code);   /* a code another row of the same context has moves there (swap); a reload row shares it */
void bind_reset(int dev);                               /* defaults of one set */
int  bind_conflict(int row, int dev);                   /* another row of the same context shares a code: that row + 1, else 0 */
int  bind_row_shown(int row, int dev);                  /* the row exists for this device */
extern int g_dev_mode;                                   /* BioRand and the importers shown (bindings.c: on) */
const char* bind_code_name(int dev, int code, char* buf, int n);   /* for menus, by the connected pad's type */
const char* bind_row_label(int row, char* buf, int n);  /* game rows: the actions of the button under the key type */
const char* bind_pad_desc(char* buf, int n);            /* the connected gamepad (its name in buf), or "No gamepad"; any thread */
const char* bind_swapped(void);                         /* the row the last bind_set moved a code from, or NULL */

/* capture ("press a key"): main-thread events; any thread starts/polls */
void bind_capture_start(int dev);
int  bind_capture_poll(void);          /* -1 waiting, -2 cancelled (Esc, time-out), -3 clear (Delete), else the code */
int  bind_capture_left_ms(void);
int  bind_event(const void* sdl_event); /* main thread, first: 1 = consumed by a capture */

/* state, game thread */
int  bind_held(int row, int dev, const bool* keys, int mouse);  /* any slot of the row is down (a reload combination's press counts only for the reload) */
int  bind_combo(int row, int dev, int slot);                    /* a reload slot another row of its group shares: it reloads as Aim + that button */
void bind_combo_frame(int on, const bool* keys, int mouse);     /* each pad report, first: which reload combinations were pressed with aim held */
int  bind_pad_raw(int row);                                     /* the gamepad holds an input of the row, whichever set reads it now */
int  bind_hotkey(const void* sdl_event, int row);               /* a key-down event of this hotkey */
void* bind_gamepad(void);              /* the open SDL_Gamepad (pad.c): game thread only */
void bind_set_gamepad(void* gamepad);  /* game thread: pad.c opened or closed it */
const bool* bind_keys(void);           /* SDL keyboard state plus scripted keys (RECVX_KEYS) */
void bind_latch_event(const void* sdl_event);              /* main thread: a key or button change, for bind_latch_frame */
const bool* bind_latch_frame(int on, const bool* keys);    /* pad report: on (a menu, Smooth menus), keys and buttons that changed since the last report at least once each */
void bind_latch_end(void);                                 /* the report's device reads are done */
int  pc_menu_smooth(void);             /* view_glue.c: Smooth menus, and a menu is up */
int  bind_quickturn(int which, int held);   /* which 0 game, 1 third person: 0 off, 1 hold back, 2 back + run (pad.c) */
int  pad_quick_knife(void);            /* the quick-knife row is held (pad.c) */
int  pad_reload(void);                 /* a reload row is held (pad.c) */
int  pad_report_bits(void);            /* the DS2 buttons held in the last pad report (pad.c) */
int  pad_free_aim(void);               /* the free-aim row raised the weapon in the last pad report (pad.c) */
int  pad_kb_start(void);               /* Start's keyboard binding is down in the last pad report (pad.c) */
/* weapon shortcuts (pc/glue/weapon_glue.c), game thread. While the wheel is up (the hold row held in play) its
   button, the d-pad rows and the page row read as up for every other row: the d-pad picks a weapon, not a step */
extern int g_wpn_overlay;              /* set by pad.c for each pad report */
int  bind_held_through(int row, int dev, const bool* keys, int mouse);   /* bind_held, also what the wheel takes */
int  pad_wpn_hold(void);               /* the hold row is held (pad.c) */
int  pad_wpn_dirs(void);               /* through the wheel: up 1, right 2, down 4, left 8, the page row 16 (pad.c) */
int  pad_wpn_key(int n);               /* shortcut row n (0..7) is held (pad.c) */

/* Input source of this window (F1 Controls, ENHANCED SETTINGS -> CONTROLS; ini input_source, input_pad;
   docs/USER_GUIDE.md "Input source (per window)"). Auto: keyboard/mouse and the gamepad, while focused. Gamepad only:
   keyboard/mouse no longer play (hotkeys, F1/F2/F3 windows and lobby text fields still work) and
   the gamepad keeps working in the background. */
enum { INPUT_AUTO, INPUT_KBM, INPUT_PAD };
extern int g_input_source;             /* INPUT_* */
extern int g_input_pad;                /* 0 any gamepad, n: the one SDL calls player n - 1 (XInput slot, PS/Switch player light) */
int  input_accept(int source, int want_pad, int dev, int player);   /* pure: may a device of set dev (a pad: its player index) play */
unsigned bind_pick_pad(void);          /* the SDL_JoystickID of the gamepad this window reads, 0 none */
const char* bind_pad_choice(int want, char* buf, int n);   /* "Any", "2: <name>" (setting value) */
void bind_apply_source(void);          /* background gamepad events on for Gamepad only (any time, no restart) */

/* recvx.ini (config.c): "key_<row>[_2] = <key name>", "pad_<row>[_2] = <gamepad input>" */
int  bind_ini_line(const char* key, const char* value);        /* 1 = a binding line */
void bind_ini_write(void* FILE_ptr);
void bind_test_frame(void);            /* main thread, per pumped frame: RECVX_KEYS / RECVX_VPAD */

/* game thread glue (pc/glue/options_glue.c): the key type's mask of virtual bit `vbit` (0..17)
   in DS2 report bits, -1 = no game running */
int  pc_keytype_mask(int vbit);
int  pc_keytype(void);

#ifdef __cplusplus
}
#endif
#endif
