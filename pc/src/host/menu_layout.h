/* The new menu's tree (the default; Menu Style Legacy or --legacy-menu: the old menus; docs/MENUS.md): which screen, category and group each row of the
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

extern int g_new_menu;     /* the new menus are up: OPTION opens SETTINGS, in a game the PAUSE menu (menu_choice_apply) */
extern int g_legacy_menu;  /* ini legacy_menu (1: Menu Style Legacy, the old OPTION, ENHANCED SETTINGS, lobby, BIORAND; written with menu_style) */
extern int g_menu_style;   /* ini menu_style, the setting "Menu Style": 0 Legacy, 1 Standard, 2 Redesigned (only with --dev) */
extern int g_menu_cli;     /* host.c: -1 none, 0 --legacy-menu, 1 --new-menu (over the ini until the setting is changed) */
void menu_choice_apply(void);   /* g_new_menu from the two: at the start and on the title menu (lobby_glue.c pc_title_mouse),
                                   never inside the OPTION task or a screen of ours, which read g_new_menu as they go */
int menu_redesigned(void);      /* Menu Style Redesigned is in effect: --dev, the new menus, menu_style 2 (docs/MENUS.md "Menu Style");
                                   read live by the game screens' overlay (menu_glue.c), so it applies on a screen's next open */

enum { MS_SETTINGS, MS_SETUP, MS_CHEATS, MS_IMPORT, MS_COUNT };   /* screens: OPTION > SETTINGS, GAME SETUP, PAUSE > CHEATS AND TOOLS, EXTRA > IMPORT */

enum {   /* categories; a sub-page (Advanced, the binding pages) has its parent category */
    MC_GAMEPLAY, MC_CAMERA, MC_CONTROLS, MC_DISPLAY, MC_GRAPHICS, MC_AUDIO, MC_LANGUAGE, MC_COOP,
    MC_CAMERA_ADV, MC_PAD_ADV, MC_GRAPHICS_ADV, MC_BIND_KB, MC_BIND_PAD,
    MC_OVERVIEW, MC_RULES, MC_ENEMIES, MC_CHARACTERS, MC_BIORAND, MC_COOP_RULES, MC_CHAR_ADV, MC_BIO_POOL,
    MC_CHEATS,
    MC_IMPORT_CHARS, MC_IMPORT_LANGS, MC_IMPORT_SAVES,   /* EXTRA > IMPORT (import_menu.c draws them; IMPORT_CAT_* order) */
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
    ML_TEXT,       /* a co-op text (arg: 0 player name, 1 co-op server); typed on the character grid */
    ML_RETIRED,    /* a table row the new menu no longer shows (ref: its label) */
    ML_MERGED,     /* a table row shown as part of an ML_VAR row (ref: its label; Model Selection in Story Characters) */
    ML_VAR,        /* a GAME SETUP row over the game's variables, not one table row (arg: SV_*; setup_glue.c) */
    ML_BIO,        /* a BioRand generator option (ref: its biorand_opt key; setup_glue.c keeps the options) */
    ML_ACTION,     /* a GAME SETUP button (arg: SA_*): START GAME, RANDOM SEED, COPY SEED STRING, ... */
    ML_INFO,       /* lines of text, not selectable (arg: SI_*): OVERVIEW's ACTIVE MODIFIERS */
    ML_IMPORT,     /* opens the IMPORT screen in place on category arg (MC_IMPORT_*): Import Languages, Import Characters */
    ML_GIVE,       /* PAUSE > CHEATS AND TOOLS: Give Item's list of items with icons (give_list.c) */
};
enum { SV_GAME_TYPE, SV_DIFFICULTY, SV_PRESET, SV_LOOK, SV_LOOK_MODE, SV_RANDOM_FROM, SV_RANDOM_IMPORTS, SV_STORY, SV_MONSTERS,
       SV_START_FROM, SV_SHOW_PICKS };   /* (SV_START_FROM: the co-op lobby's, g_coop.start; SV_SHOW_PICKS: D17, in game) */
enum { SA_START, SA_ENEMY_SEED, SA_BIO_RANDOM, SA_BIO_COPY, SA_BIO_PASTE, SA_BIO_CHECK };
enum { SI_MODIFIERS, SI_BIO_ENEMIES };
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
/* GAME SETUP Preset (OVERVIEW): the rows it owns (each set to its default first), then Remix's and Chaos's values */
extern const char* const menu_setup_owned[];
extern const int menu_setup_owned_n;
extern const MenuPreset menu_remix[], menu_chaos[];
extern const int menu_remix_n, menu_chaos_n;

/* defaults: every table row with a variable, as it is before recvx.ini and the command line (main, first) */
void menu_defaults_capture(void);
int  menu_default(int setting, int* v);   /* g_settings index: 1 and its default, 0 unknown (no variable) */
int  menu_setting(const char* label);     /* the g_settings index of a label, -1 none */

/* the row screen (pc/glue/ui_rows.c, game thread) */
enum { UR_STAY, UR_BACK, UR_CLOSE, UR_START, UR_PICK, UR_FLOW, UR_LEVEL };   /* ui_rows_run: UR_START GAME SETUP's START GAME;
    UR_PICK a look row's picker (ui_rows_pick: its setting); UR_FLOW a BioRand dialog (setup_glue.c); UR_LEVEL + n opens OPTION level n (1-3) */
void ui_rows_open(int screen, const char* root);   /* at its remembered category and row; root: the breadcrumb's first word */
void ui_rows_goto(int cat, const MenuRow* m);      /* show category cat, the cursor on row m (NULL: its remembered row) */
void ui_rows_picker(int on);                       /* the host has the 3D look picker (title mode 30): Confirm on a look row returns UR_PICK */
void ui_rows_only(int cat);                        /* list only category cat (-1: the screen's all): a shortcut's home in another host */
const char* ui_rows_value(const MenuRow* m, char* buf, int n);   /* a row's value as the screen shows it ("" none) */
int  ui_pad(void);                                 /* the button bar shows the controller's buttons (the device pressed last) */
const struct Setting* ui_rows_pick(void);          /* UR_PICK's row */
void ui_rows_say(const char* m);                   /* a message on the help line */
int  ui_rows_run(int port, int flag);              /* input (flag) and drawing, every frame */
int  ui_rows_screen(void);
void ui_rows_close(void);                          /* OPTION closed: the actions chosen run now */
int  ui_rows_window(void);                         /* an F-key window to open once OPTION is closed (ML_WINDOW), 0 none; taken */
/* its button bar, for other screens (the pause menu): the device pressed last, a button's name on it, one hint */
enum { UG_OK = 1, UG_BACK, UG_DEFAULT, UG_CAT, UG_CHANGE, UG_START, UG_TRI, UG_SCROLL, UG_PAGE };   /* Triangle (WHAT'S NEW at the title); up / down, a page (WHAT'S NEW) */
void ui_device(unsigned int press, int kb_used);   /* a press this frame (Pad[].press); kb_used: the mouse or a menu key */
const char* ui_glyph(int which, char* buf, int n);   /* the bound button, drawn by ui_hint as icons in Button Icons' style */
float ui_hint(float x, float y, const char* glyph, const char* word, float fill);   /* returns the next x */
float ui_hint_w(const char* glyph, const char* word);   /* its width, without the gap after it */
void ui_frame_plain(const char* root, const char* const* path, int n);   /* the row screen's panel, breadcrumb and bar rule alone */

/* GAME SETUP (pc/glue/setup_glue.c, game thread): where it is shown, and its rows that are not table rows. ui_rows.c asks
   these for every row of a GAME SETUP category (wherever it is shown: NEW GAME, the pause menu, later the lobby). */
enum { SC_NEWGAME, SC_INGAME, SC_LOBBY_HOST, SC_LOBBY_GUEST };   /* the context: the lobby's for P3 (a guest reads only) */
extern int g_setup_ctx;
int  setup_cat_shown(int c);
int  setup_row_shown(const MenuRow* m, int shown);   /* shown: what the table says */
const char* setup_row_lock(const MenuRow* m);        /* why it cannot change here and now, else NULL */
int  setup_row_next_room(const MenuRow* m);          /* in a game: a run's rule that applies from the next room (its mark) */
int  setup_custom_list(const MenuRow* m);            /* the Custom list under row m now (cast_list.c 0, 1, 2), -1 none */
/* ML_VAR and ML_BIO rows */
int  setup_get(const MenuRow* m);
void setup_step(const MenuRow* m, int dir);          /* left / right (choices wrap, numbers stop) */
void setup_set(const MenuRow* m, int v);
int  setup_choice(const MenuRow* m, int* n);         /* 1: names 0..n-1; 0: a number (setup_range) */
void setup_range(const MenuRow* m, int* lo, int* hi);
const char* setup_name(const MenuRow* m, int v, char* buf, int n);
int  setup_default(const MenuRow* m, int* d);
const char* setup_label(const MenuRow* m);
const char* setup_help(const MenuRow* m);
int  setup_action(const MenuRow* m);                 /* ML_ACTION: UR_* */
int  setup_info(const MenuRow* m, int line, char* buf, int n, unsigned int* col);   /* ML_INFO: its lines (the count); line's text */
int  setup_preview(const MenuRow* m, float x, float y, float w);   /* 1: the row's preview drawn (CHARACTERS' who-is-who, OVERVIEW, BIORAND) */
const char* setup_value(const MenuRow* m, char* buf, int n);   /* a shortcut's own value (Your Character), else NULL */
const char* setup_summary(char* buf, int n);         /* the modifiers in one line (LOAD GAME's note), "" none */
int  setup_host_value(const MenuRow* m, int set, int* v);   /* a co-op guest's view: the host's value of a table row (1), else 0 */
/* the co-op lobby (lobby_glue.c hosts GAME SETUP in title mode 30 while the session stays open) */
void setup_lobby_open(int view, int cat, int only);  /* view 0 the host's, 1 a guest's (read-only, YOU its own); cat -1 the last */
int  pc_setup_lobby(void);                           /* per frame: 0 stay, 1 back, 2 START GAME pressed, 3 the game starts (BioRand generated) */
void setup_lobby_close(void);                        /* ... closed by its host (the lobby closes, the session ended) */
int  setup_lobby_start(int d);                       /* a new game of difficulty d: 2 start now, 1 BioRand's dialogs first */
int  setup_flow_frame(void);                         /* ... those dialogs: 0 on, 1 over, 2 the game starts */
void setup_lobby_type(int on);                       /* the host's Game Type in the lobby (1 BioRand) */
int  setup_lobby_bio(void);
int  setup_lobby_bio_ready(void);                    /* the installed BioRand game is this setup's */
void setup_track(void);                              /* a guest, per frame: notes what the host changes */
const char* setup_host_changed(void);                /* ... "Extra Enemies: Up to 3" for a few seconds, else NULL */
int  setup_lobby_lines(char out[][128], int max, int view, char* brief, int nb);   /* the lobby's GAME SETUP box; brief: "Normal, 3 changes" */

/* the character grid for text fields (pc/glue/text_grid.c; its cells and rules src/host/textgrid.h, kind TGK_*) */
void tg_open(const char* title, const char* text, int max, int kind, int masked);
int  tg_run(int port);                               /* per frame: 0 on (drawn), 1 OK (tg_text), -1 cancelled */
int  tg_active(void);
const char* tg_text(void);
void tg_close(void);                                 /* after OK: the text taken (a password's copy wiped) */

#ifdef __cplusplus
}
#endif
#endif
