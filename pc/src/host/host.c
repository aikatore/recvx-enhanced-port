/*
 * Host entry point: configuration, SDL window/GL context, event pump, and the
 * hand-off into the original game's main() (renamed recvx_game_main).
 */
#include "ps2_platform.h"
#include "../gs/stream.h"
#include "prof.h"
#include "coop.h"
#include "tps.h"
#include "menu_mouse.h"
#include "lobby.h"
#include "bindings.h"
#include "settings.h"
#include "biorand.h"
#include "../import/import.h"
#include "../net/update.h"
void render_loop(void);
#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <execinfo.h>
#include <signal.h>
#include <unistd.h>
#endif

int recvx_game_main(int argc, char* argv[]);
void audio_init(void);
void gs_renderer_init(void);

int pc_screen_filter_mode(void) { return g_config.screen_filter; }
/* "Skip door animations" (decomp system.c bhSysCallDoordemo): 0 the animation, 1 instant (the
   load screen), 2 the animation until a key, mouse button or gamepad button is pressed */
int pc_skip_door_demo(void) { return g_config.skip_doors == 1; }
/* "Fast loading" (decomp system.c bhSysCallMonitor/bhSysCallSndMonitor, sdfunc.c pc_load_sound_pack_now) */
int pc_fast_loading(void) { return g_config.fast_loading; }
static SDL_AtomicInt presses;   /* key, mouse and gamepad button presses (host_pump_events) */
int pc_door_skip_pressed(int start)
{
    static int at;   /* game thread */
    int n = SDL_GetAtomicInt(&presses);
    if (start) at = n;   /* the demo begins: the press that opened the door is older */
    return g_config.skip_doors == 2 && n != at;
}
int pc_intro_pressed(int start)
{
    static int at;   /* game thread */
    int n = SDL_GetAtomicInt(&presses);
    if (start) at = n;   /* a screen begins: older presses (and held keys) do not count */
    return n != at;
}
int pc_quick_knife_on(void) { return g_config.quick_knife; }
int pc_head_blowoff_on(void) { return g_config.head_blowoff; }
int pc_auto_stairs_on(void) { return g_config.auto_stairs; }
int pc_cfg_no_ink_ribbon(void) { return g_config.no_ink_ribbon; }   /* in co-op the host's counts (coop_glue.c pc_no_ink_ribbon) */
/* the game tests a few joints of a model against the screen; with a wide or moving free
   camera that drops models that are partly visible, so third person never culls */
extern int tps_cam_active;
int pc_model_cull(void) { return g_config.model_cull && !tps_cam_active; }
int pc_frame_blend(void) { return g_config.frame_blend < 0 ? !g_config.interpolate : g_config.frame_blend; }
int pc_battle_unlocked(void) { return g_config.battle_unlocked; }
int pc_difficulty_select(void) { return g_config.difficulty_select; }
int pc_skip_intro(void) { return g_config.skip_intro; }
int pc_movie_zoom(void) { return g_config.movie_zoom; }
int pc_menu_smooth_on(void) { return g_config.menu_smooth && g_config.interpolate; }   /* view_glue.c pc_menu_tag */
/* fog at i % of the original: the fog density (njCalcFogPower) x 100 / i, so everything's fog is
   that of something i % as far: 50 % = x 2. 0 %: every distance reads the table's nearest
   entry, 254 or 255 of 255 (no fog) */
float pc_fog_scale(void) { int i = g_config.fog_intensity; return i <= 0 ? 1e6f : 100.0f / i; }
int pc_skip_intro_press_start(void)
{
    static int done;
    if (!g_config.skip_intro || done) return 0;
    done = 1;
    return 1;
}
int pc_config_native_tex(void) { return g_config.native_tex; }
int pc_native_3d(void)
{
    static int env = -2;
    if (env == -2) env = getenv("RECVX_NATIVE3D") ? atoi(getenv("RECVX_NATIVE3D")) != 0 : -1;
    return env >= 0 ? env : g_config.native_3d;
}

/* full-screen 2D overlays: 0 original 4:3, 1 stretched to the (widened) view, 2 off;
   fades always cover the whole view */
enum { PC_OVL_CINEMA, PC_OVL_FADE, PC_OVL_EFFECT };   /* as in pc_prelude.h */
static int overlay_stretch;
int pc_overlay_begin(int kind)
{
    int mode = kind == PC_OVL_CINEMA ? g_config.cinema_bars : kind == PC_OVL_EFFECT ? g_config.screen_effects : 1;
    overlay_stretch = mode == 1;
    return mode != 2;   /* Off: the caller skips the layer, or (the oil pressure puzzle) draws it 4:3 */
}
void pc_overlay_end(void) { overlay_stretch = 0; }
int pc_overlay_wide(void) { return overlay_stretch; }
float pc_overlay_x(float x)
{
    extern float pc_hor_scale(void);
    return overlay_stretch ? 320.0f + (x - 320.0f) * pc_hor_scale() : x;
}

HostConfig g_config = { .rifle_stack = 1, .window_w = 1280, .window_h = 960, .borderless_extra = 1,.vsync = 1, .log_level = 1, .music_volume = 100, .sfx_volume = 100, .rumble = 100, .interpolate = 1, .menu_smooth = 1, .frame_blend = 0, .aspect = 2, .show_fps = 1, .show_ping = 1,
                       .screen_filter = 1, .cinema_bars = 1, .screen_effects = 1, .native_3d = 1, .model_cull = 1, .native_tex = 1, .hd_textures = 1, .hd_textures_ui = 1, .quick_knife = 1, .head_blowoff = 1,
                       .skip_doors = 1, .fast_loading = 1, .auto_stairs = 1, .skip_intro = 1, .battle_unlocked = 1, .difficulty_select = 1, .contrast = 100, .gamma = 10, .fog_intensity = 100, .update_check = 1 };   /* first-start defaults; recvx.ini overrides */
SDL_Window* g_window;
SDL_GLContext g_gl;

void host_log(const char* fmt, ...)
{
    extern void log_keep(const char* fmt, va_list ap);
    va_list ap;
    va_start(ap, fmt);
    log_keep(fmt, ap);   /* the last messages, for the crash report (log.c) */
    va_end(ap);
    if (g_config.log_level <= 0) return;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

void host_fatal(const char* fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fprintf(stderr, "fatal: %s\n", buf);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Code: Veronica X", buf, g_window);
    exit(1);
}

void host_pump_events(void)
{
    SDL_Event ev;
    bind_test_frame();
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_EVENT_GAMEPAD_ADDED || ev.type == SDL_EVENT_GAMEPAD_REMOVED) { extern void pad_devices_changed(void); pad_devices_changed(); }
        if (bind_event(&ev)) continue;   /* a "press a key" of the settings */
        if (overlay_event(&ev)) continue;
        bind_latch_event(&ev);
        if ((ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) || ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
            SDL_AddAtomicInt(&presses, 1);
        tps_input_event(&ev);
        menu_mouse_event(&ev);
        lobby_io_event(&ev);
        switch (ev.type) {
        case SDL_EVENT_QUIT:
            exit(0);
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:   /* moved to another monitor: its modes for Fullscreen resolution */
        case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:   /* the desktop resized: a borderless window covers it again */
            settings_display_modes();
            break;
        case SDL_EVENT_KEY_DOWN:
            if (bind_hotkey(&ev, B_FULLSCREEN)) {
                g_config.fullscreen = !g_config.fullscreen;   /* of the kind chosen last (Display mode) */
                settings_apply_display();
                settings_save_later();
            }
            if (bind_hotkey(&ev, B_TPS_TOGGLE) && !g_lobby_io.text_on) {
                g_tps.on = !g_tps.on;
                settings_save_later();
            }
            break;
        }
    }
    settings_poll();
    lobby_io_frame(g_window);
    if (__atomic_exchange_n(&g_import_reload, 0, __ATOMIC_ACQ_REL)) import_registry_load();   /* (the IMPORT screen's extraction) */
}

#ifndef _WIN32
/* debugging aid: RECVX_WATCHDOG=<seconds> prints a backtrace and exits (headless runs) */
static void watchdog(int sig)
{
    void* frames[64];
    int n = backtrace(frames, 64);
    (void)sig;
    fprintf(stderr, "watchdog: backtrace\n");
    backtrace_symbols_fd(frames, n, 2);
    if (getenv("RECVX_PROFILE")) exit(3);   /* run atexit: profile dump */
    _exit(3);
}
#endif

static void usage(void)
{
    fprintf(stderr,
        "recvx-enhanced-port " RECVX_VERSION "\n"
        "usage: recvx [options] <game disc image (.iso)>\n"
        "  --save-dir <dir>   settings and memory card directory (default: ./save if it exists,\n"
        "                     else save/ next to the program, which also holds mods/ and logs)\n"
        "  --size WxH         window size (default 1280x960)\n"
        "  --fullscreen       start in fullscreen (borderless, or exclusive if chosen in the settings)\n"
        "  --borderless / --exclusive / --windowed   start in that display mode\n"
        "  --enhanced         display-rate frames (60+ FPS) blended from the 30 FPS game (default)\n"
        "  --vanilla          original 30 FPS presentation\n"
        "  --skip-doors       room transitions (doors, stairs, ladders) without their animation (default;\n"
        "                     skip_doors in recvx.ini: 0 played, 1 instant, 2 played until a key is pressed)\n"
        "  --skip-intro       boot straight into the title menu (default on)\n"
        "  --hd-textures DIR  PCSX2 HD texture pack folder (default mods/textures)\n"
        "  --third-person     over-the-shoulder camera, mouse/keyboard controls (toggle: F5)\n"
        "  --coop-host PORT   online co-op: wait for a partner on this UDP port (F3: co-op window)\n"
        "  --coop-join HOST:PORT#CODE | ROOM  online co-op: join a host, or a room through the co-op server\n"
        "  --coop-server ADDR   co-op server (host or host:port, UDP 7141)  --coop-conn auto|direct|server  --coop-pw PASSWORD\n"
        "  --quiet\n"
        "  --version          print the version and exit\n"
        "  --log[=file]       copy all output to a log file (default recvx.log), crash backtraces included\n"
        "  --profile[=file]   per-frame timing CSV (default recvx_profile.csv): logic, vu0, vu1, gs,\n"
        "                     interp, present, wait; also shown live in the F1 overlay\n"
        "  --profile-samples=file  sampling CPU profiler (summarise with pc/tools/profile.py)\n"
        "F1 opens the settings overlay; settings are saved in <save dir>/recvx.ini (options set there,\n"
        "e.g. skip_doors = 0 or skip_intro = 0, override the defaults).\n"
        "The image can also be given with the RECVX_ISO environment variable. Without one, a file\n"
        "selector asks for it on the first start and recvx.ini remembers it (key iso).\n");
}

static char log_path[1024], profile_path[1024];
static int iso_given;   /* image from the command line / RECVX_ISO: no file selector */
static int save_given;  /* --save-dir */
static char start_dir[1024];   /* where the program was started: relative image paths are taken from here */

static void iso_from(const char* s)
{
    int absolute = s[0] == '/' || s[0] == '\\' || (s[0] && s[1] == ':');
    if (absolute || !start_dir[0]) SDL_strlcpy(g_config.iso_path, s, sizeof(g_config.iso_path));
    else SDL_snprintf(g_config.iso_path, sizeof(g_config.iso_path), "%s%s", start_dir, s);
    iso_given = 1;
}

/* Started from somewhere else (a file manager, a shortcut, another directory): the program's own
   folder holds save/, mods/ and the logs, so the release binaries need no start script. A save
   folder in the current directory or an explicit --save-dir keeps the current directory (several
   instances with their own folders, test setups). */
static void home_dir(void)
{
    const char* base;
    if (save_given || SDL_GetPathInfo("save", NULL)) return;
    if (!(base = SDL_GetBasePath())) return;
    pc_chdir(base);
}

static void parse_args(int argc, char** argv)
{
    const char* env = SDL_getenv("RECVX_ISO");   /* UTF-8 on Windows too */
    if (env) iso_from(env);
    SDL_strlcpy(g_config.save_dir, "save", sizeof(g_config.save_dir));
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--save-dir") && i + 1 < argc) { SDL_strlcpy(g_config.save_dir, argv[++i], sizeof(g_config.save_dir)); save_given = 1; }
        else if (!strcmp(argv[i], "--size") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &g_config.window_w, &g_config.window_h);
        else if (!strcmp(argv[i], "--fullscreen")) g_config.fullscreen = 1;   /* the kind of recvx.ini (borderless by default) */
        else if (!strcmp(argv[i], "--borderless")) { g_config.fullscreen = 1; g_config.fullscreen_exclusive = 0; }
        else if (!strcmp(argv[i], "--exclusive")) { g_config.fullscreen = 1; g_config.fullscreen_exclusive = 1; }
        else if (!strcmp(argv[i], "--windowed")) g_config.fullscreen = 0;
        else if (!strcmp(argv[i], "--enhanced")) g_config.interpolate = 1;
        else if (!strcmp(argv[i], "--vanilla")) g_config.interpolate = 0;
        else if (!strcmp(argv[i], "--skip-doors")) g_config.skip_doors = 1;
        else if (!strcmp(argv[i], "--skip-intro")) g_config.skip_intro = 1;
        else if (!strcmp(argv[i], "--hd-textures") && i + 1 < argc) { SDL_strlcpy(g_config.hd_textures_dir, argv[++i], sizeof(g_config.hd_textures_dir)); }
        else if (!strcmp(argv[i], "--third-person")) g_tps.on = 1;
        else if (!strcmp(argv[i], "--dev")) g_dev_mode = 1;
        else if (!strcmp(argv[i], "--quiet")) g_config.log_level = 0;
        else if (!strcmp(argv[i], "--coop-host") && i + 1 < argc) { g_coop.port = atoi(argv[++i]); g_coop.request = COOP_REQ_HOST; }
        else if (!strcmp(argv[i], "--coop-join") && i + 1 < argc) { SDL_strlcpy(g_coop.join, argv[++i], sizeof(g_coop.join)); g_coop.request = COOP_REQ_JOIN; }
        else if (!strcmp(argv[i], "--coop-server") && i + 1 < argc) SDL_strlcpy(g_coop.server, argv[++i], sizeof(g_coop.server));
        else if (!strcmp(argv[i], "--coop-pw") && i + 1 < argc) SDL_strlcpy(g_coop.password, argv[++i], sizeof(g_coop.password));
        else if (!strcmp(argv[i], "--coop-conn") && i + 1 < argc) { i++; g_coop.conn = !strcmp(argv[i], "direct") ? COOP_CONN_DIRECT : !strcmp(argv[i], "server") ? COOP_CONN_SERVER : COOP_CONN_AUTO; }
        else if (!strncmp(argv[i], "--log", 5) && (argv[i][5] == 0 || argv[i][5] == '='))
            SDL_strlcpy(log_path, argv[i][5] ? argv[i] + 6 : "recvx.log", sizeof(log_path));
        else if (!strncmp(argv[i], "--profile-samples=", 18)) SDL_setenv_unsafe("RECVX_PROFILE", argv[i] + 18, 1);
        else if (!strncmp(argv[i], "--profile", 9) && (argv[i][9] == 0 || argv[i][9] == '='))
            SDL_strlcpy(profile_path, argv[i][9] ? argv[i] + 10 : "recvx_profile.csv", sizeof(profile_path));
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { usage(); exit(0); }
        else if (!strcmp(argv[i], "--version")) { printf("recvx-enhanced-port " RECVX_VERSION "\n"); exit(0); }
        else if (argv[i][0] != '-') iso_from(argv[i]);
        else { usage(); exit(1); }
    }
}

/* First start (or the remembered image is gone): ask for the disc image, remember it in recvx.ini. */
static void SDLCALL iso_picked(void* done, const char* const* files, int filter)
{
    (void)filter;
    if (!files) host_log("file selector: %s\n", SDL_GetError());
    else if (files[0]) SDL_strlcpy(g_config.iso_path, files[0], sizeof(g_config.iso_path));
    SDL_SetAtomicInt(done, 1);
}

static void pick_iso(void)
{
    static const SDL_DialogFileFilter filters[] = { { "PS2 disc image (*.iso)", "iso" }, { "All files", "*" } };
    static SDL_AtomicInt done;
    SDL_PropertiesID props = SDL_CreateProperties();
    g_config.iso_path[0] = 0;
    SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, (void*)filters);
    SDL_SetNumberProperty(props, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, SDL_arraysize(filters));
    SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING, "Select the Resident Evil Code: Veronica X disc image (SLUS-20184)");
    SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFILE, iso_picked, &done, props);
    SDL_DestroyProperties(props);
    while (!SDL_GetAtomicInt(&done)) { SDL_Event ev; SDL_WaitEventTimeout(&ev, 50); }
}

/* The game (the decomp's main loop, VU, GIF recording) runs on its own thread; the main
   thread renders (host/render.c). */
static int SDLCALL game_thread(void* ud)
{
    char* game_argv[] = { "cdrom0:\\SLUS_201.84;1", NULL };
    (void)ud;
    crash_thread_init();
    prof_thread(PROF_LOGIC);
    { extern void profile_thread(char); profile_thread('g'); }   /* RECVX_PROFILE */
    exit(recvx_game_main(1, game_argv));
}

int main(int argc, char** argv)
{
    setvbuf(stdout, NULL, _IONBF, 0); /* the game's printf diagnostics */
    argv = pc_utf8_argv(argc, argv);   /* paths are UTF-8 everywhere (fileio.h) */
    { char* cwd = SDL_GetCurrentDirectory(); if (cwd) { SDL_strlcpy(start_dir, cwd, sizeof(start_dir)); SDL_free(cwd); } }
    parse_args(argc, argv);
    home_dir();
    { extern void crash_init(const char*); crash_init(RECVX_VERSION); }   /* recvx-crash.log on a crash, with or without --log */
    if (log_path[0]) { extern void log_start(const char*); log_start(log_path); }
    host_log("recvx-enhanced-port " RECVX_VERSION "\n");
    if (profile_path[0]) { extern void prof_open_csv(const char*); prof_open_csv(profile_path); }
#ifndef _WIN32
    if (getenv("RECVX_WATCHDOG")) { signal(SIGALRM, watchdog); alarm((unsigned)atoi(getenv("RECVX_WATCHDOG"))); }
    { extern void profile_start(void); profile_start(); }
#endif
    config_load();
    parse_args(argc, argv);   /* command line overrides the saved settings */
    { extern void variants_load(void); variants_load(); }   /* mods/enemies.ini: custom enemies (variants.h) */
    biorand_scan();   /* mods/biorand: BioRand's pack (biorand.h) */
    if (SDL_getenv("RECVX_IMPORT")) {   /* headless import (docs/IMPORT_OUTBREAK.md): RECVX_IMPORT=outbreak2, then exit */
        int src = -1, what = SDL_getenv("RECVX_IMPORT_WHAT") ? atoi(SDL_getenv("RECVX_IMPORT_WHAT")) : IMPORT_PLAYERS;
        for (int i = 0; i < IMPORT_SOURCES; i++) if (!strcmp(SDL_getenv("RECVX_IMPORT"), import_sources[i].key)) src = i;
        if (src < 0) host_fatal("RECVX_IMPORT: unknown source '%s' (outbreak2)", SDL_getenv("RECVX_IMPORT"));
        if (SDL_getenv("RECVX_IMPORT_ISO")) SDL_strlcpy(g_import.iso[src], SDL_getenv("RECVX_IMPORT_ISO"), sizeof(g_import.iso[src]));
        exit(import_run(src, what) == 0 ? 0 : 1);
    }
    update_start();   /* the newest version for the title, on a thread of its own (net/update.h) */
    ps2_memory_init();

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD))
        host_fatal("Cannot start the game: %s", SDL_GetError());
    bind_apply_source();   /* Gamepad only: gamepads also in the background */

    if (!iso_given && (!g_config.iso_path[0] || !cdvd_open_image(g_config.iso_path))) {
        pick_iso();
        if (!g_config.iso_path[0]) { usage(); host_fatal("Select a game disc image, or start recvx with the disc image path."); }
        if (!cdvd_open_image(g_config.iso_path)) host_fatal("Cannot open game disc image: %s", g_config.iso_path);
        config_save();
    } else {
        if (!g_config.iso_path[0]) { usage(); host_fatal("Select a game disc image first."); }
        if (!cdvd_open_image(g_config.iso_path)) host_fatal("Cannot open game disc image: %s", g_config.iso_path);
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    g_window = SDL_CreateWindow("Resident Evil Code: Veronica X", g_config.window_w, g_config.window_h,
                                SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
    if (!g_window) host_fatal("Cannot open the game window: %s", SDL_GetError());
    settings_display_modes();
    settings_apply_display();   /* hidden: fullscreen (and its mode) are taken when it is shown */
    SDL_ShowWindow(g_window);
    g_gl = SDL_GL_CreateContext(g_window);
    if (!g_gl) host_fatal("Cannot start graphics: %s", SDL_GetError());
    SDL_GL_SetSwapInterval(g_config.vsync ? 1 : 0);
    gs_renderer_init();
    overlay_init(g_window, g_gl);
    audio_init();

    stream_init();
    if (!SDL_CreateThread(game_thread, "game", NULL)) host_fatal("Cannot start the game: %s", SDL_GetError());
    render_loop();   /* the main thread renders and presents; never returns */
    return 0;
}
