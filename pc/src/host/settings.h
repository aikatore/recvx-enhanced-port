/* The port's settings as one table (pc/src/host/settings.c), read by the native OPTION menu's
   ENHANCED SETTINGS pages (pc/glue/options_glue.c, game thread; every row) and by the F1 overlay
   (overlay.cpp settings_imgui; of the CO-OP and EXTRAS pages only the entries flagged SET_F1).
   Both edit the same variables (g_config, g_tps, g_coop, g_cheat, ...); recvx.ini keys stay in
   config.c, and ctest "settings" fails for an ini key without a row here.
   docs/ARCHITECTURE.md "Settings" says how to add one. */
#ifndef RECVX_SETTINGS_H
#define RECVX_SETTINGS_H
#ifdef __cplusplus
extern "C" {
#endif

enum { SP_DISPLAY, SP_GAMEPLAY, SP_CONTROLS, SP_TPS, SP_AUDIO, SP_APPEAR, SP_COOP, SP_EXTRAS, SP_COUNT };   /* in menu order */
extern const char* const settings_pages[SP_COUNT];

enum {
    SET_RESTART = 1,      /* takes effect at the next start */
    SET_F1 = 2,           /* CO-OP / EXTRAS entry also shown in F1 (those pages have their own windows, F3 / F2) */
    SET_ACTION = 4,       /* a command (apply runs it), no value */
    SET_GAME_ONLY = 8,    /* only while a game runs (not at the title) */
    SET_NOW = 16,         /* action: the native menu runs it at once, not when it closes */
    SET_CALIB = 32,       /* picture calibration: the menus show settings_calib while it is chosen */
};
enum { CALIB_STEPS = 5 };
extern const unsigned char settings_calib[2][CALIB_STEPS];   /* grey levels of the squares: [0] on black, [1] on white */

typedef struct Setting {
    int page;                         /* SP_* */
    const char* label;                /* ASCII (the game's font) */
    int* val;                         /* the variable, or get/set */
    int lo, hi, step;                 /* range; lo == 0 && hi == 1 without names = on/off */
    const char* const* names;         /* value names, names[v - lo] */
    const char* fmt;                  /* printf for a number ("%d%%"), when no names */
    int flags;                        /* SET_* */
    const char* help;                 /* one or two sentences (tooltip / help line) */
    void (*apply)(void);              /* main thread, after a change (window, vsync, volumes); SET_ACTION: the action */
    const char* (*locked)(void);      /* why it cannot change now, or NULL */
    int (*get)(void);                 /* instead of val */
    void (*set)(int v);
    const char* (*name)(int v, char* buf, int n);   /* instead of names / fmt */
    int (*count)(void);               /* hi = count() - 1 (lists that load late) */
    const char* group;                /* a heading above this row, in both menus */
    int (*shown)(void);               /* NULL or 1: the menus list the row (0: hidden, e.g. Fullscreen resolution outside exclusive) */
} Setting;

extern const Setting g_settings[];
extern const int g_settings_n;

int  settings_get(const Setting* s);
int  settings_hi(const Setting* s);
const Setting* settings_of(const int* val);     /* the row that edits this variable, or NULL */
void settings_set(const Setting* s, int v);     /* clamped; recvx.ini is written, apply runs on the main thread */
void settings_step(const Setting* s, int dir);  /* left/right: by step, choices wrap */
void settings_act(const Setting* s);            /* SET_ACTION */
int  settings_is_bool(const Setting* s);
const char* settings_value(const Setting* s, char* buf, int n);
const char* settings_locked(const Setting* s);  /* why not now (locked(), SET_GAME_ONLY), or NULL */
void settings_poll(void);                       /* main thread, per pumped frame: appliers; recvx.ini 500 ms after the last change */
void settings_save_later(void);                 /* any thread: recvx.ini is written by settings_poll (or at exit) */
void settings_apply_window(void);               /* main thread: the window size from g_config (windowed) */
void settings_apply_display(void);              /* main thread: windowed / borderless / exclusive from g_config (F11, Display mode) */
void settings_display_modes(void);              /* main thread: reread the window's display's modes, refit a borderless window (start, display change) */

/* game thread glue (options_glue.c) */
int  pc_in_game(void);

#ifdef __cplusplus
}
#endif
#endif
