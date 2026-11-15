/* The new menu's tree (--new-menu, implied by --dev; docs/MENUS.md): which screen, category and group each row of the
   settings table (settings.c g_settings[]) lives in, under which words. Pure data (menu_layout.c) over the table: values,
   ranges, help, locks and apply stay in g_settings[]; a row here names its setting by the table's label (21 rows have no
   variable, so settings_of() cannot find them) and may override the label, the value names and the help without
   touching settings.c, F1 or the old ENHANCED SETTINGS pages. ctest "menu_layout" (tests/check_menu_layout.cmake)
   fails unless every table row is placed exactly once (or listed as retired) and every shortcut names a real row.
   The row screen that shows it is pc/glue/ui_rows.c (game thread); its hosts draw background and title. */
#ifndef RECVX_MENU_LAYOUT_H
#define RECVX_MENU_LAYOUT_H
#ifdef __cplusplus
extern "C" {
#endif

extern int g_new_menu;   /* --new-menu or --dev (host.c): OPTION opens SETTINGS, in a game the PAUSE menu */

enum { MS_SETTINGS, MS_SETUP, MS_CHEATS, MS_COUNT };   /* screens: OPTION > SETTINGS, GAME SETUP, PAUSE > CHEATS AND TOOLS */

enum {   /* categories; a sub-page (Advanced, the binding pages) has its parent category */
    MC_GAMEPLAY, MC_CAMERA, MC_CONTROLS, MC_DISPLAY, MC_GRAPHICS, MC_AUDIO, MC_LANGUAGE, MC_COOP,
    MC_CAMERA_ADV, MC_PAD_ADV, MC_GRAPHICS_ADV, MC_BIND_KB, MC_BIND_PAD,
    MC_RULES, MC_ENEMIES, MC_CHARACTERS, MC_COOP_RULES, MC_CHAR_ADV,
    MC_CHEATS,
    MC_COUNT
};
typedef struct MenuCat { int screen; const char* name; int parent; } MenuCat;   /* parent -1: listed on the left */
extern const MenuCat menu_cats[MC_COUNT];

enum {
    ML_SET,        /* a settings-table row (ref: its label) */
    ML_SHORTCUT,   /* a setting whose home is elsewhere (ref: its label, arg: the home category): its value, Confirm opens the home */
    ML_PAGE,       /* a sub-page (arg: the category) */
    ML_SCREEN,     /* one of the game's OPTION sub-screens (arg: its level, 1 KEY CONFIG, 2 SCREEN SETUP, 3 MONITOR ADJUSTMENT) */
    ML_OUTPUT,     /* the game's AUDIO SETUP (Stereo / Mono, the system save) */
    ML_PRESET,     /* GRAPHICS Preset: Original Look / Enhanced / Custom over menu_preset[] */
    ML_WINDOW,     /* opens an F-key window (arg: 2 cheats F2, 3 co-op F3, 4 import F9) */
    ML_TEXT,       /* a co-op text (arg: 0 player name, 1 co-op server); edited in the F3 window for now */
    ML_RETIRED,    /* a table row the new menu no longer shows (ref: its label) */
};
enum {
    MLF_ALWAYS = 1,     /* shown even when the row's shown() says no (LANGUAGE: Language without an imported one) */
    MLF_VIBRATION = 2,  /* Vibration Strength merged with OPTION > VIBRATION: 0 = the game's vibration off */
};
typedef struct MenuRow {
    int cat;                       /* MC_* */
    const char* group;             /* a heading above this row */
    int kind;                      /* ML_* */
    const char* ref;               /* the settings-table label */
    int arg;
    const char* label;             /* shown instead of ref (NULL: ref) */
    const char* const* names;      /* value names instead of the table's, names[v - lo] */
    const char* help;              /* instead of the table's */
    int flags;                     /* MLF_* */
} MenuRow;
extern const MenuRow menu_rows[];
extern const int menu_rows_n;

typedef struct MenuPreset { const char* ref; int value; } MenuPreset;   /* GRAPHICS Preset "Original Look": the rows it sets */
extern const MenuPreset menu_preset[];
extern const int menu_preset_n;

/* defaults: every table row with a variable, as it is before recvx.ini and the command line (main, first) */
void menu_defaults_capture(void);
int  menu_default(int setting, int* v);   /* g_settings index: 1 and its default, 0 unknown (no variable) */
int  menu_setting(const char* label);     /* the g_settings index of a label, -1 none */

/* the row screen (pc/glue/ui_rows.c, game thread) */
enum { UR_STAY, UR_BACK, UR_CLOSE, UR_LEVEL };   /* ui_rows_run: UR_LEVEL + n opens OPTION level n (1-3) */
void ui_rows_open(int screen, const char* root);   /* at its remembered category and row; root: the breadcrumb's first word */
int  ui_rows_run(int port, int flag);              /* input (flag) and drawing, every frame */
int  ui_rows_screen(void);
void ui_rows_close(void);                          /* OPTION closed: the actions chosen run now */
int  ui_rows_window(void);                         /* an F-key window to open once OPTION is closed (ML_WINDOW), 0 none; taken */
/* its button bar, for other screens (the pause menu): the device pressed last, a button's name on it, one hint */
enum { UG_OK = 1, UG_BACK, UG_DEFAULT, UG_CAT, UG_CHANGE };
void ui_device(unsigned int press, int kb_used);   /* a press this frame (Pad[].press); kb_used: the mouse or a menu key */
const char* ui_glyph(int which, char* buf, int n);
float ui_hint(float x, float y, const char* glyph, const char* word, float fill);   /* returns the next x */

#ifdef __cplusplus
}
#endif
#endif
