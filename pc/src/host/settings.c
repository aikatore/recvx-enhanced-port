/* The settings table shared by the native OPTION menu (ENHANCED SETTINGS) and the F1 overlay.
   docs/ARCHITECTURE.md "Settings": to add a setting, add its variable's ini key in config.c and
   one entry here; both menus show it (F1 on the CO-OP and EXTRAS pages only with SET_F1). Rows
   are in menu order, .group starts a heading. */
#include "../ps2_platform.h"
#include "settings.h"
#include "coop.h"
#include "actors.h"
#include "tps.h"
#include "cheat.h"
#include "menu_mouse.h"
#include "bindings.h"
#include "modern.h"
#include "enemies.h"
#include "lobby.h"
#include "savestore.h"
#include "biorand.h"
#include "../net/update.h"
#include "../audio/audio.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern SDL_Window* g_window;

const char* const settings_pages[SP_COUNT] = { "DISPLAY", "GAMEPLAY", "CONTROLS", "THIRD PERSON", "AUDIO", "APPEARANCE", "CO-OP", "EXTRAS" };

/* ---------------------------------------------------------------- appliers (main thread) */
/* Display mode, F11, the start. Exclusive: SDL fullscreen with a display mode (that display switches
   to it, and SDL's VIDEO_MINIMIZE_ON_FOCUS_LOSS "auto" minimises it on Alt+Tab, restoring the
   desktop). Borderless under Linux: SDL fullscreen with no mode (the window manager's fullscreen at
   the desktop's size). Borderless under Windows: not SDL's fullscreen, which there is a WS_POPUP
   window exactly covering the monitor, the shape the OpenGL drivers promote to their own
   exclusive-like fullscreen (a black flash on Alt+Tab, nothing shown over the game): it behaved as
   exclusive. Instead a plain borderless window borderless_extra pixels (default 1) bigger than the
   monitor, towards an edge with no other monitor, which the drivers leave to the desktop
   compositor. SDL declares the process per-monitor DPI aware (v2) in SDL_Init, so display bounds
   and window rects are all pixels there. Leaving fullscreen of either kind, the window goes back
   where and how big it was. */
static bool bl_on, bl_max;   /* the borderless window is up; the window was maximised before it */
static SDL_Rect bl_rect;     /* the window before it */

static bool borderless_window(void)   /* the env: Linux tests of the Windows path */
{
    return !SDL_strcmp(SDL_GetCurrentVideoDriver(), "windows") || SDL_getenv("RECVX_BORDERLESS_WINDOW");
}
#ifdef _WIN32
/* dwmapi's DwmSetWindowAttribute without windows.h: Windows 11's rounded corners and window border
   off while borderless, as SDL does for its fullscreen (an error, harmless, before Windows 11) */
__declspec(dllimport) long __stdcall DwmSetWindowAttribute(void* hwnd, unsigned long attr, const void* val, unsigned long size);
static void dwm_square(bool on)
{
    void* hwnd = SDL_GetPointerProperty(SDL_GetWindowProperties(g_window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    unsigned int corner = on ? 1 : 0, border = on ? 0xFFFFFFFEu : 0xFFFFFFFFu;   /* DWMWCP_DONOTROUND / DEFAULT, DWMWA_COLOR_NONE / DEFAULT */
    if (!hwnd) return;
    DwmSetWindowAttribute(hwnd, 33, &corner, sizeof(corner));   /* DWMWA_WINDOW_CORNER_PREFERENCE */
    DwmSetWindowAttribute(hwnd, 34, &border, sizeof(border));   /* DWMWA_BORDER_COLOR */
}
#else
static void dwm_square(bool on) { (void)on; }
#endif
static bool display_at(const SDL_Rect* strip)   /* a display overlaps this strip */
{
    int n = 0;
    bool hit = false;
    SDL_DisplayID* ds = SDL_GetDisplays(&n);
    for (int i = 0; i < n && !hit; i++) {
        SDL_Rect r;
        hit = SDL_GetDisplayBounds(ds[i], &r) && SDL_HasRectIntersection(strip, &r);
    }
    SDL_free(ds);
    return hit;
}
static void borderless_enter(void)
{
    SDL_Rect b;
    int e = g_config.borderless_extra;
    if (!SDL_GetDisplayBounds(SDL_GetDisplayForWindow(g_window), &b)) { host_log("display: %s\n", SDL_GetError()); return; }
    if (!bl_on) {   /* the window to come back to */
        bl_max = (SDL_GetWindowFlags(g_window) & SDL_WINDOW_MAXIMIZED) != 0;
        if (bl_max) { SDL_RestoreWindow(g_window); SDL_SyncWindow(g_window); }
        SDL_GetWindowPosition(g_window, &bl_rect.x, &bl_rect.y);
        SDL_GetWindowSize(g_window, &bl_rect.w, &bl_rect.h);
    }
    bl_on = true;
    if (e) {   /* past the bottom, else the right, the top, the left edge; all next to a monitor: the bottom */
        const SDL_Rect strip[4] = { { b.x, b.y + b.h, b.w, e }, { b.x + b.w, b.y, e, b.h }, { b.x, b.y - e, b.w, e }, { b.x - e, b.y, e, b.h } };
        int i = 0;
        while (i < 4 && display_at(&strip[i])) i++;
        if (i == 4) i = 0;
        if (i & 1) b.w += e; else b.h += e;
        if (i == 2) b.y -= e;
        if (i == 3) b.x -= e;
    }
    SDL_SetHint("SDL_BORDERLESS_WINDOWED_STYLE", "0");   /* Windows: a plain WS_POPUP, no caption style the taskbar stays above */
    SDL_SetWindowResizable(g_window, false);
    SDL_SetWindowBordered(g_window, false);
    SDL_SetWindowPosition(g_window, b.x, b.y);
    SDL_SetWindowSize(g_window, b.w, b.h);
    SDL_SyncWindow(g_window);
    dwm_square(true);
}
static void borderless_leave(void)
{
    if (!bl_on) return;
    bl_on = false;
    dwm_square(false);
    SDL_SetWindowBordered(g_window, true);
    SDL_SetWindowResizable(g_window, true);
    SDL_SetWindowSize(g_window, bl_rect.w, bl_rect.h);
    SDL_SetWindowPosition(g_window, bl_rect.x, bl_rect.y);
    if (bl_max) SDL_MaximizeWindow(g_window);
    SDL_SyncWindow(g_window);
}

void settings_apply_display(void)
{
    SDL_DisplayID d = SDL_GetDisplayForWindow(g_window);
    SDL_DisplayMode m;
    int excl = g_config.fullscreen_exclusive && g_config.fullscreen;
    bool bl;
    if (excl) {   /* the chosen mode, or the closest this display has; 0: the desktop's */
        const SDL_DisplayMode* desk = SDL_GetDesktopDisplayMode(d);
        int w = g_config.fullscreen_w, h = g_config.fullscreen_h;
        float hz = w ? (float)g_config.fullscreen_hz : 0.0f;   /* 0: the desktop's refresh */
        if (!w && desk) { w = desk->w; h = desk->h; }
        excl = w && SDL_GetClosestFullscreenDisplayMode(d, w, h, hz, false, &m);
        if (!excl) host_log("display: no %dx%d mode for exclusive fullscreen (%s), borderless instead\n", w, h, SDL_GetError());
    }
    bl = g_config.fullscreen && !excl && borderless_window();
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, excl ? "auto" : "0");   /* SDL's "auto" spares borderless already; said outright */
    if (!bl) borderless_leave();   /* first, so exclusive keeps the bordered window as the one to come back to */
    if (g_config.fullscreen && !bl) {
        if (!SDL_SetWindowFullscreenMode(g_window, excl ? &m : NULL)) host_log("display: %s\n", SDL_GetError());
        if (!SDL_SetWindowFullscreen(g_window, true)) host_log("display: fullscreen: %s\n", SDL_GetError());
    } else {   /* out of SDL's fullscreen, then no mode: the display has the desktop's mode back */
        if (!SDL_SetWindowFullscreen(g_window, false)) host_log("display: fullscreen: %s\n", SDL_GetError());
        if (!SDL_SetWindowFullscreenMode(g_window, NULL)) host_log("display: %s\n", SDL_GetError());
        SDL_SyncWindow(g_window);
    }
    if (bl) borderless_enter();
    {
        int x, y, w, h;
        char kind[64];
        SDL_GetWindowPosition(g_window, &x, &y);
        SDL_GetWindowSize(g_window, &w, &h);
        if (excl) snprintf(kind, sizeof(kind), "exclusive fullscreen %d x %d @ %.2f Hz", m.w, m.h, m.refresh_rate);
        else snprintf(kind, sizeof(kind), "%s", !g_config.fullscreen ? "windowed" : bl ? "borderless window" : "borderless fullscreen");
        host_log("display: %s, window %d,%d %d x %d\n", kind, x, y, w, h);
    }
}
void settings_apply_window(void) { if (!g_config.fullscreen) SDL_SetWindowSize(g_window, g_config.window_w, g_config.window_h); }
static void apply_vsync(void) { SDL_GL_SetSwapInterval(g_config.vsync ? 1 : 0); }
static void apply_volume(void) { audio_apply_volumes(); }

/* ---------------------------------------------------------------- special values */
static const struct { int w, h; } sizes[] = { { 1280, 960 }, { 1440, 1080 }, { 1920, 1080 }, { 2560, 1440 }, { 3440, 1440 }, { 3840, 2160 } };
static int size_get(void)
{
    for (int i = 0; i < (int)SDL_arraysize(sizes); i++)
        if (sizes[i].w == g_config.window_w && sizes[i].h == g_config.window_h) return i;
    return -1;
}
static void size_set(int v) { if (v >= 0 && v < (int)SDL_arraysize(sizes)) { g_config.window_w = sizes[v].w; g_config.window_h = sizes[v].h; } }
static const char* size_name(int v, char* b, int n)
{
    if (v < 0) snprintf(b, n, "%d x %d", g_config.window_w, g_config.window_h);
    else snprintf(b, n, "%d x %d", sizes[v].w, sizes[v].h);
    return b;
}
static const char* interp_off(void) { return g_config.interpolate ? NULL : "Turn on Smoother Motion."; }
static const char* fullscreen_on(void) { return g_config.fullscreen ? "Only in windowed mode." : NULL; }
/* Display mode: windowed / borderless / exclusive over fullscreen + fullscreen_exclusive (F11 flips
   only the first, so it returns to the kind chosen last) */
static int dmode_get(void) { return !g_config.fullscreen ? 0 : g_config.fullscreen_exclusive ? 2 : 1; }
static void dmode_set(int v) { g_config.fullscreen = v > 0; if (v > 0) g_config.fullscreen_exclusive = v == 2; }
/* Fullscreen resolution: "Desktop", then the modes of the window's display (filled on the main
   thread, settings_display_modes; SDL's order: largest first, the highest refresh first) */
static struct { int w, h, hz; } fs_modes[128];
static int fs_n;
void settings_display_modes(void)
{
    int n = 0;
    SDL_DisplayMode** m = SDL_GetFullscreenDisplayModes(SDL_GetDisplayForWindow(g_window), &n);
    fs_n = 0;   /* ponytail: the game thread's menu may read this while a display change refills it; one odd row name at worst */
    for (int i = 0; m && i < n && fs_n < (int)SDL_arraysize(fs_modes); i++) {
        int hz = (int)(m[i]->refresh_rate + 0.5f), dup = m[i]->pixel_density > 1.0f;
        for (int k = 0; k < fs_n; k++)   /* the same in another pixel format, or 59.94 next to 60 */
            dup |= fs_modes[k].w == m[i]->w && fs_modes[k].h == m[i]->h && fs_modes[k].hz == hz;
        if (dup) continue;
        fs_modes[fs_n].w = m[i]->w; fs_modes[fs_n].h = m[i]->h; fs_modes[fs_n].hz = hz;
        fs_n++;
    }
    SDL_free(m);
    if (bl_on) borderless_enter();   /* the borderless window moved (Win+Shift+arrow) or its desktop resized: cover that display */
}
static int fsres_n(void) { return fs_n + 1; }
static int fsres_get(void)
{
    if (!g_config.fullscreen_w) return 0;
    for (int i = 0; i < fs_n; i++)
        if (fs_modes[i].w == g_config.fullscreen_w && fs_modes[i].h == g_config.fullscreen_h && fs_modes[i].hz == g_config.fullscreen_hz) return i + 1;
    return -1;   /* not a mode of this display: SDL takes the closest */
}
static void fsres_set(int v)
{
    if (v <= 0 || v > fs_n) g_config.fullscreen_w = g_config.fullscreen_h = g_config.fullscreen_hz = 0;
    else { g_config.fullscreen_w = fs_modes[v - 1].w; g_config.fullscreen_h = fs_modes[v - 1].h; g_config.fullscreen_hz = fs_modes[v - 1].hz; }
}
static const char* fsres_name(int v, char* b, int n)
{
    if (v == 0) snprintf(b, n, "Desktop");
    else if (v > 0 && v <= fs_n) snprintf(b, n, "%d x %d, %d Hz", fs_modes[v - 1].w, fs_modes[v - 1].h, fs_modes[v - 1].hz);
    else snprintf(b, n, "%d x %d, %d Hz", g_config.fullscreen_w, g_config.fullscreen_h, g_config.fullscreen_hz);
    return b;
}
static int exclusive_on(void) { return g_config.fullscreen && g_config.fullscreen_exclusive; }
static const char* scale_name(int v, char* b, int n)
{
    if (v) snprintf(b, n, "%dx (%d x %d)", v, 640 * v, 448 * v);
    else snprintf(b, n, "Auto (%dx)", (int)gs_wanted_scale());
    return b;
}
static const char* tenths(int v, char* b, int n) { snprintf(b, n, "%d.%d", v / 10, v % 10); return b; }
/* picture calibration (applied in gs_gl.c draw_display); the test pattern both menus show
   while a SET_CALIB row is chosen: squares on a black and on a white strip, the faint ones at the ends */
const unsigned char settings_calib[2][CALIB_STEPS] = { { 5, 10, 16, 24, 32 }, { 223, 231, 239, 245, 250 } };
static void picture_reset(void) { g_config.brightness = 0; g_config.contrast = 100; g_config.gamma = 10; settings_save_later(); }
static const char* biorand_name(int v, char* b, int n)
{
    if (!biorand_pack()) snprintf(b, n, "No Generated Game");
    else if (v && biorand_seed_text()[0]) snprintf(b, n, "On (%s)", biorand_seed_text());
    else snprintf(b, n, v ? "On" : "Off");
    return b;
}
static const char* rifle_locked(void) { return biorand_active() ? "Required by BioRand." : NULL; }
/* "You appear as": positions in the menus' list (actors.c, without event doubles) */
static int chr_get(void) { int o = pc_biorand_appear(); return appear_pos(o >= 0 ? o : g_coop.chr); }   /* (BioRand's pick of this game shown) */
static void chr_set(int v) { g_coop.chr = appear_at(v); pc_biorand_appear_drop(); }
static const char* chr_name(int v, char* b, int n) { snprintf(b, n, "%s", appear_name(appear_at(v))); return b; }
/* saves: the memory card's copied into the native folder (as the first native start does) */
static void import_card(void)
{
    int n = savestore_import_card(g_config.save_dir, save_native_dir());
    host_log("saves: imported %d memory card save%s into %s (the card keeps them)\n", n, n == 1 ? "" : "s", save_native_dir());
}
static const char* in_session(void) { return g_coop.state != COOP_OFF ? "Not during a co-op session." : NULL; }

/* cheats (F2): the request bits are taken by the game thread at the frame hand-off */
static void heal(void) { __atomic_or_fetch(&g_cheat.request, CHEAT_HEAL, __ATOMIC_ACQ_REL); }
static void give(void) { if (g_cheat.give_id) __atomic_or_fetch(&g_cheat.request, CHEAT_GIVE, __ATOMIC_ACQ_REL); }
static void spawn(void) { __atomic_or_fetch(&g_cheat.request, CHEAT_SPAWN, __ATOMIC_ACQ_REL); }
static void teleport(void) { __atomic_or_fetch(&g_cheat.request, CHEAT_TELEPORT, __ATOMIC_ACQ_REL); }
static void here(void) { g_cheat.tp_stg = g_cheat.stg; g_cheat.tp_rom = g_cheat.rom; g_cheat.tp_pos = g_cheat.pos; }
/* teleport rows by name (pc/src/host/teleport.c); the plain numbers until the room list is read */
static const char* tp_stage_nm(int v, char* b, int n) { snprintf(b, n, "%s", tp_stage_name(v)); return b; }
static void tp_stage_set(int v)
{
    int k, f = tp_stage_first(v, &k);
    g_cheat.tp_stg = v;
    if (k) g_cheat.tp_rom = tp_room(f)->rom;
    g_cheat.tp_pos = 0;
}
static int tp_room_get(void)
{
    int k, f = tp_stage_first(g_cheat.tp_stg, &k), i = tp_find(g_cheat.tp_stg, g_cheat.tp_rom);
    return !k ? g_cheat.tp_rom : i >= 0 ? i - f : -1;
}
static void tp_room_set(int v)
{
    int k, f = tp_stage_first(g_cheat.tp_stg, &k);
    g_cheat.tp_rom = k ? tp_room(f + v)->rom : v;
    g_cheat.tp_pos = 0;
}
static int tp_room_n(void) { int k; tp_stage_first(g_cheat.tp_stg, &k); return k ? k : 100; }
/* the value field is narrow: room names without their area ("Prison: "), which the location row
   shows ("Cutscene: " stays). area_len: the length of s's area prefix, 0 if none */
static int area_len(const char* s) { const char* c = strstr(s, ": "); return c && strncmp(s, "Cutscene", 8) ? (int)(c - s) + 2 : 0; }
static void cut(char* s, int k) { memmove(s, s + k, strlen(s + k) + 1); }
static const char* tp_room_nm(int v, char* b, int n)
{
    int k, f = tp_stage_first(g_cheat.tp_stg, &k);
    if (k && v >= 0 && v < k) { cut(b, area_len(tp_room_name(f + v, 0, b, n))); return b; }
    snprintf(b, n, "%d%s", g_cheat.tp_rom, tp_reading() ? " (loading...)" : "");
    return b;
}
static int tp_pos_n(void) { int i = tp_find(g_cheat.tp_stg, g_cheat.tp_rom); return i >= 0 ? tp_entries(i) : 32; }
static const char* tp_pos_nm(int v, char* b, int n)
{
    int i = tp_find(g_cheat.tp_stg, g_cheat.tp_rom);
    if (i >= 0) {   /* "Entry 1: from Prison: Graveyard": the area left out when a room of this location has it */
        char room[96], *from = strstr(tp_entry_name(i, v, b, n), "from ");
        int k, f = tp_stage_first(g_cheat.tp_stg, &k), a = from ? area_len(from + 5) : 0;
        for (int j = 0; a && j < k; j++)
            if (!strncmp(tp_room_name(f + j, 0, room, sizeof(room)), from + 5, a)) { cut(from + 5, a); break; }
        return b;
    }
    snprintf(b, n, "%d", v);
    return b;
}
static int item_get(void) { return g_cheat.give_id; }
static void item_set(int v)   /* on to the next id with a name, in the direction of the step */
{
    int dir = v < g_cheat.give_id ? -1 : 1;
    for (int i = 0; i < CHEAT_ITEMS && !g_cheat.names[v][0]; i++) v = v + dir < 1 ? CHEAT_ITEMS - 1 : v + dir >= CHEAT_ITEMS ? 1 : v + dir;
    g_cheat.give_id = v;
}
static const char* item_name(int v, char* b, int n)
{
    snprintf(b, n, "%d %s", v, v > 0 && v < CHEAT_ITEMS && g_cheat.names[v][0] ? g_cheat.names[v] : "-");
    return b;
}
static int spawn_get(void) { return g_cheat.spawn_idx; }
static void spawn_set(int v) { g_cheat.spawn_idx = v; }
static const char* spawn_nm(int v, char* b, int n) { snprintf(b, n, "%s", spawn_name(v)); return b; }

static const char* const onoff_auto[] = { "Auto", "Off", "On" };
static const char* const display_modes[] = { "Windowed", "Borderless Fullscreen", "Exclusive Fullscreen" };
static const char* const modes3[] = { "Original (4:3)", "Full Width", "Off" };
static const char* const fps_limits[] = { "Display Rate", "30", "60", "90", "120", "144" };   /* render.c present_hz */
static const char* const aspects[] = { "Original (4:3)", "Stretch", "Widescreen" };
static const char* const save_systems[] = { "Separate Files", "Memory Card" };
static const char* const loots[] = { "Shared", "Individual" };
static const char* const gameovers[] = { "Each Player", "Both" };
static const char* const detectors[] = { "Original", "Skipped" };
static const char* const conns[] = { "Automatic", "Direct", "Server Relay" };
static const char* const atmos_names[] = { "Original", "Smooth" };
static const char* const free_cam_names[] = { "Behind Character", "Free Camera" };
static const char* const fixed_cam_names[] = { "Classic", "Modern", "Combined" };
static const char* const tps_pad_names[] = { "Classic", "Modern" };
static const char* const noclips[] = { "Off", "Walls", "Walls and Flight" };
static const char* const sources[] = { "Auto", "Keyboard and Mouse", "Gamepad Only" };
static const char* const extras[] = { "Off", "Up to 1", "Up to 2", "Up to 3" };
static const char* const varieties[] = { "Mild", "Wild" };
static const char* const cast_pools[] = { "Cutscene Models", "All Models" };
static const char* const cast_assigns[] = { "Per Character", "Per Cutscene" };
static int cast_on(void) { return g_enemy_opts.cast != 0; }
static int cast_imports_shown(void) { return cast_on() && __atomic_load_n(&actor_import_n, __ATOMIC_ACQUIRE) > 0; }

#define V(x) ((int*)&(x))
const Setting g_settings[] = {
    { SP_DISPLAY, "Display Mode", NULL, 0, 2, 1, display_modes, .get = dmode_get, .set = dmode_set, .apply = settings_apply_display, .group = "WINDOW",
      .help = "Choose windowed, borderless or exclusive fullscreen. Press F11 to switch to windowed and back." },
    { SP_DISPLAY, "Fullscreen Resolution", NULL, 0, 0, 1, .get = fsres_get, .set = fsres_set, .name = fsres_name, .count = fsres_n, .shown = exclusive_on, .apply = settings_apply_display,
      .help = "Set the resolution and refresh rate for exclusive fullscreen. Desktop uses your desktop settings." },
    { SP_DISPLAY, "Window Size", NULL, 0, 5, 1, .get = size_get, .set = size_set, .name = size_name, .locked = fullscreen_on, .apply = settings_apply_window },
    { SP_DISPLAY, "VSync", V(g_config.vsync), 0, 1, 1, .apply = apply_vsync },
    { SP_DISPLAY, "Frame Rate Limit", V(g_config.fps_limit), 0, 5, 1, fps_limits, .help = "Limit the displayed frame rate. Game speed stays the same." },
    { SP_DISPLAY, "Show Frame Rate", V(g_config.show_fps), 0, 1, 1 },
    { SP_DISPLAY, "Aspect Ratio", V(g_config.aspect), 0, 2, 1, aspects, .help = "Widescreen shows more at the sides. Menus and 2D artwork stay centered at 4:3.", .group = "PICTURE" },
    { SP_DISPLAY, "Internal Resolution", V(g_config.scale), 0, 8, 1, .name = scale_name, .help = "Set how sharply the game is drawn. Auto adjusts to your window size." },
    { SP_DISPLAY, "Smoother Motion", V(g_config.interpolate), 0, 1, 1, .help = "Smooth motion between game frames. Adds a small input delay." },
    { SP_DISPLAY, "Smooth Menus", V(g_config.menu_smooth), 0, 1, 1, .locked = interp_off,
      .help = "Smooth menu movement and improve menu response. Off keeps the original menu frame rate." },
    { SP_DISPLAY, "Motion Trails", V(g_config.frame_blend), -1, 1, 1, onoff_auto, .help = "Add the original motion trails. Auto turns them off when Smoother Motion is on." },
    { SP_DISPLAY, "Cutscene Bars", V(g_config.cinema_bars), 0, 2, 1, modes3, .help = "Keep cutscene bars at 4:3, extend them across the screen, or hide them. Fades always fill the screen.", .group = "WIDESCREEN" },
    { SP_DISPLAY, "Camera Vignette", V(g_config.screen_filter), 0, 2, 1, modes3, .help = "Keep darkened camera edges at 4:3, extend them across the screen, or hide them." },
    { SP_DISPLAY, "Screen Effects", V(g_config.screen_effects), 0, 2, 1, modes3, .help = "Keep overlays at 4:3, extend them, or hide them. The oil pressure puzzle remains visible." },
    { SP_DISPLAY, "Movie Zoom", V(g_config.movie_zoom), 0, 1, 1, .help = "Enlarge movies to reduce black bars in widescreen without stretching the picture." },
    { SP_DISPLAY, "Fog Intensity", V(g_config.fog_intensity), 0, 100, 5, .fmt = "%d%%", .group = "FOG AND PICTURE",
      .help = "Reduce fog to see farther, including in dark rooms. 100% keeps the original fog; 0% removes it." },
    { SP_DISPLAY, "Brightness", V(g_config.brightness), -50, 50, 1, .fmt = "%+d", .flags = SET_CALIB,
      .help = "Adjust dark areas. The left square on the black strip should be barely visible." },
    { SP_DISPLAY, "Contrast", V(g_config.contrast), 50, 150, 1, .fmt = "%d%%", .flags = SET_CALIB,
      .help = "Adjust the difference between light and dark. The right square on the white strip should be barely visible." },
    { SP_DISPLAY, "Gamma", V(g_config.gamma), 5, 20, 1, .name = tenths, .flags = SET_CALIB,
      .help = "Brighten or darken midtones while keeping black and white unchanged." },
    { SP_DISPLAY, "Reset Picture Calibration", NULL, .flags = SET_ACTION | SET_NOW, .apply = picture_reset,
      .help = "Restore the original brightness, contrast and gamma." },
    { SP_DISPLAY, "Hardware 3D Rendering", V(g_config.native_3d), 0, 1, 1, .help = "Use hardware rendering for 3D models.", .group = "RENDERER" },
    { SP_DISPLAY, "Texture Caching", V(g_config.native_tex), 0, 1, 1, .help = "Reuse textures already loaded by your graphics card." },
    { SP_DISPLAY, "Hide Edge Models", V(g_config.model_cull), 0, 1, 1, .help = "Hide models near screen edges, as in the original; Off keeps them visible. No effect in third person." },
    { SP_DISPLAY, "HD Textures", V(g_config.hd_textures), 0, 1, 1, .flags = SET_RESTART,
      .help = "Use installed high-resolution textures. Off uses the original textures." },
    { SP_DISPLAY, "HD Font", V(g_config.hd_textures_font), 0, 1, 1,
      .help = "Use the texture pack's font. Applies to text loaded after this change." },
    { SP_DISPLAY, "HD Menus", V(g_config.hd_textures_ui), 0, 1, 1,
      .help = "Allow redesigned menus and artwork from the texture pack. Off keeps replacements faithful to the original." },

    { SP_GAMEPLAY, "Skip Intro", V(g_config.skip_intro), 0, 1, 1, .flags = SET_RESTART, .help = "Go straight to the title screen. Off lets you skip each logo and movie with a button press.", .group = "GENERAL" },
    { SP_GAMEPLAY, "Check For Updates", V(g_config.update_check), 0, 1, 1, .apply = update_start,
      .help = "Check for a newer game version at startup and show it on the title screen." },
    { SP_GAMEPLAY, "Skip Door Animations", V(g_config.skip_doors), 0, 2, 1, (const char* const[]){ "Off", "Instant", "Skippable" },
      .help = "Choose full transitions, instant loading, or animations you can skip with any key or button." },
    { SP_GAMEPLAY, "Fast Loading", V(g_config.fast_loading), 0, 1, 1,
      .help = "Shorten loading waits. Each co-op player chooses separately." },
    { SP_GAMEPLAY, "Battle Game Unlocked", V(g_config.battle_unlocked), 0, 1, 1,
      .help = "Unlock Battle Game and all five characters without completing the story." },
    { SP_GAMEPLAY, "Difficulty Select", V(g_config.difficulty_select), 0, 1, 1,
      .help = "Choose Normal, Easy or Very Easy when starting a new game. Off always starts on Normal." },
    { SP_GAMEPLAY, "Quick Knife", V(g_config.quick_knife), 0, 1, 1,
      .help = "Hold Quick Knife to draw your knife. Release it to return to your previous weapon." },
    { SP_GAMEPLAY, "Auto-climb Stairs", V(g_config.auto_stairs), 0, 1, 1,
      .help = "Climb stairs by moving into them. Ledges, doors and items still need the action button." },
    { SP_GAMEPLAY, "Zombie Decapitation", V(g_config.head_blowoff), 0, 1, 1, .help = "Allow zombie heads to burst." },
    { SP_GAMEPLAY, "Stack Rifle Ammo", V(g_config.rifle_stack), 0, 1, 1, .locked = rifle_locked,
      .help = "Combine rifle and assault rifle ammo in your inventory. Off keeps each pickup separate." },
    { SP_GAMEPLAY, "Save System", V(g_config.save_system), 0, 1, 1, save_systems,
      .help = "Separate Files allows unlimited saves; Memory Card uses the original 15 saves. Applies next time you save.", .group = "SAVES" },
    { SP_GAMEPLAY, "Import Memory Card Saves", NULL, .flags = SET_ACTION, .apply = import_card,
      .help = "Copy memory card saves to separate files. Existing copies are skipped; the card keeps its saves." },
    { SP_GAMEPLAY, "Free Typewriter Saves", V(g_config.no_ink_ribbon), 0, 1, 1,
      .help = "Save at typewriters without using ink ribbons. In co-op, the host decides." },
    { SP_GAMEPLAY, "BioRand", V(g_config.biorand), 0, 1, 1, .name = biorand_name, .locked = biorand_locked, .shown = biorand_shown, .group = "RANDOMIZER",
      .help = "Play your generated BioRand game. Change this at the title screen." },
    { SP_GAMEPLAY, "Extra Enemies", V(g_enemy_opts.extra), 0, 3, 1, extras, .help = "Add more of a room's existing enemies from the next room. In co-op, the host decides.", .group = "ENEMIES" },
    { SP_GAMEPLAY, "Rearranged Enemies", V(g_enemy_opts.place), 0, 1, 1, .help = "Move roaming enemies to new starting positions; story and boss enemies stay. Applies next room." },
    { SP_GAMEPLAY, "Enemy Randomizer", V(g_enemy_opts.rand), 0, 1, 1, .help = "Replace enemies with other types that fit the room; bosses stay. Applies next room." },
    { SP_GAMEPLAY, "Randomizer Variety", V(g_enemy_opts.variety), 0, 1, 1, varieties,
      .help = "Mild uses two common types per room; Wild adds variety, including Tyrants and bats. Applies next room." },
    { SP_GAMEPLAY, "Randomize Event Enemies", V(g_enemy_opts.events), 0, 1, 1,
      .help = "Change event enemies too; those needed for scenes only change appearance. Applies next room." },
    { SP_GAMEPLAY, "Enemy Seed", V(g_enemy_opts.seed), 0, 99999, 1, .fmt = "%d", .help = "Use the same seed for repeatable enemy and cast choices. In co-op, both players use the host's seed." },
    { SP_GAMEPLAY, "Randomize Cutscene Cast", V(g_enemy_opts.cast), 0, 1, 1, .group = "CUTSCENE CAST",
      .help = "Change story characters' appearance in cutscenes using the enemy seed. Applies next room." },
    { SP_GAMEPLAY, "Model Selection", V(g_enemy_opts.cast_all), 0, 1, 1, cast_pools, .shown = cast_on,
      .help = "Cutscene Models have animated faces. All Models includes creatures and others with still faces." },
    { SP_GAMEPLAY, "Model Assignment", V(g_enemy_opts.cast_assign), 0, 1, 1, cast_assigns, .shown = cast_on,
      .help = "Keep one model for each character, or choose again for each cutscene room. The seed fixes the choices." },
    { SP_GAMEPLAY, "Include Imported Models", V(g_enemy_opts.cast_imports), 0, 1, 1, .shown = cast_imports_shown,
      .help = "Include imported characters. In co-op, both players need the same models." },

    { SP_CONTROLS, "Input Devices", V(g_input_source), 0, 2, 1, sources, .apply = bind_apply_source,
      .help = "Choose active devices. Gamepad Only works in the background for local co-op; hotkeys still work." },
    { SP_CONTROLS, "Gamepad", V(g_input_pad), 0, 4, 1, .name = bind_pad_choice,
      .help = "Any uses the first connected gamepad. Choose a player number for each window in local co-op." },
    { SP_CONTROLS, "Vibration Strength", V(g_config.rumble), 0, 100, 10, .fmt = "%d%%",
      .help = "Adjust vibration strength. Turn vibration on or off in OPTION > VIBRATION." },
    { SP_CONTROLS, "Mouse In Menus", V(g_menu_mouse.on), 0, 1, 1, .help = "Point and left-click to select in menus. Right-click to go back." },
    { SP_CONTROLS, "Fixed Camera Controls", V(g_modern_on), 0, 2, 1, fixed_cam_names,
      .help = "Modern follows screen direction. Combined uses Modern movement with Classic gamepad D-pad controls." },

    { SP_TPS, "Over-the-shoulder Camera", V(g_tps.on), 0, 1, 1, .help = "Play with a controllable camera and free aim. Events, doors and menus keep their original cameras.", .group = "CAMERA" },
    { SP_TPS, "Mouse Camera Style", V(g_tps.free_cam), 0, 1, 1, free_cam_names,
      .help = "Behind Character turns you with the mouse. Free Camera lets the camera turn independently." },
    { SP_TPS, "Field Of View", V(g_tps.fov), 40, 120, 5, .fmt = "%d degrees", .help = "Set the horizontal view at 4:3. Widescreen shows more at the sides." },
    { SP_TPS, "Aiming Field of View", V(g_tps.aim_fov), 30, 120, 5, .fmt = "%d degrees" },
    { SP_TPS, "Camera Distance", V(g_tps.dist), 0, 400, 5, .name = tenths },
    { SP_TPS, "Aiming Camera Distance", V(g_tps.aim_dist), 0, 400, 5, .name = tenths },
    { SP_TPS, "Camera Height", V(g_tps.height), 0, 300, 5, .name = tenths },
    { SP_TPS, "Shoulder Offset", V(g_tps.shoulder), 0, 150, 5, .name = tenths },
    { SP_TPS, "Left Shoulder", V(g_tps.left), 0, 1, 1 },
    { SP_TPS, "Lighting and Fog", V(g_tps.atmos), 0, 1, 1, atmos_names,
      .help = "Smooth fades lighting, fog and camera shading between views. Original changes them with each fixed camera." },
    { SP_TPS, "Horizontal Mouse Sensitivity", V(g_tps.sens_x), 5, 400, 5, .fmt = "%d%%", .group = "MOUSE AND AIM" },
    { SP_TPS, "Vertical Mouse Sensitivity", V(g_tps.sens_y), 5, 400, 5, .fmt = "%d%%" },
    { SP_TPS, "Aiming Sensitivity", V(g_tps.aim_sens), 10, 200, 5, .fmt = "%d%%", .help = "Adjust mouse sensitivity while aiming, relative to normal sensitivity." },
    { SP_TPS, "Invert Mouse Y", V(g_tps.invert_y), 0, 1, 1 },
    { SP_TPS, "Crosshair", V(g_tps.crosshair), 0, 1, 1 },
    { SP_TPS, "Aim Assist", V(g_tps.auto_aim), 0, 1, 1, .help = "Use lock-on aim. Modern gamepad controls use gentle aim assistance instead. Off gives full free aim." },
    { SP_TPS, "Precise Shots", V(g_tps.precise), 0, 1, 1,
      .help = "Shots must hit the enemy's body. Headshots deal more damage; zombie heads only burst on a headshot." },
    { SP_TPS, "Gamepad Controls", V(g_tps.pad_modern), 0, 1, 1, tps_pad_names, .group = "GAMEPAD",
      .help = "Modern uses the left stick to move and the right to look and aim. Classic uses tank controls." },
    { SP_TPS, "Horizontal Camera Speed", V(g_tps.pad_sens_x), 10, 300, 5, .fmt = "%d%%", .help = "Set horizontal camera speed. At 100%, a full stick turn covers 180 degrees per second." },
    { SP_TPS, "Vertical Camera Speed", V(g_tps.pad_sens_y), 10, 300, 5, .fmt = "%d%%" },
    { SP_TPS, "Stick Aiming Speed", V(g_tps.pad_aim_sens), 10, 200, 5, .fmt = "%d%%", .help = "Adjust right stick speed while aiming, relative to normal camera speed." },
    { SP_TPS, "Invert Stick Y", V(g_tps.pad_invert_y), 0, 1, 1 },
    { SP_TPS, "Movement Dead Zone", V(g_tps.pad_dead_move), 0, 50, 5, .fmt = "%d%%" },
    { SP_TPS, "Camera Dead Zone", V(g_tps.pad_dead_look), 0, 50, 5, .fmt = "%d%%" },

    { SP_AUDIO, "Music And Voice", V(g_config.music_volume), 0, 100, 5, .fmt = "%d%%", .apply = apply_volume },
    { SP_AUDIO, "Sound Effects", V(g_config.sfx_volume), 0, 100, 5, .fmt = "%d%%", .apply = apply_volume },

    { SP_APPEAR, "Character Model", NULL, 0, 0, 1, .get = chr_get, .set = chr_set, .name = chr_name, .count = appear_count, .help = "Choose your character's appearance. Your co-op partner sees it too." },
    { SP_APPEAR, "Match Model Animations", V(g_appear_anim), 0, 1, 1, .help = "Use Chris's movements for male models and Claire's for female models." },
    { SP_APPEAR, "Custom Cutscene Model", V(g_appear_cutscene), 0, 1, 1, .help = "Use your chosen model in cutscenes. Off shows the original story character." },
    { SP_APPEAR, "Animated Faces (Experimental)", V(g_appear_face), 0, 1, 1, .help = "Animate your chosen model's face in cutscenes when available. Applies next room." },

    { SP_COOP, "Partner Name Tag", V(g_coop.nametag), 0, 1, 1, .help = "Show your partner's name overhead, or at the screen edge when nearby but out of view.", .group = "PERSONAL SETTINGS" },
    { SP_COOP, "Show Ping", V(g_config.show_ping), 0, 1, 1, .flags = SET_F1, .help = "Show your co-op connection delay beside the frame rate while connected." },
    { SP_COOP, "Hide IP Addresses", V(g_coop.hide_ip), 0, 1, 1, .flags = SET_F1,
      .help = "Hide addresses on screen. Copying an address and joining still work." },
    { SP_COOP, "Follow Host", V(g_coop.follow), 0, 1, 1, .help = "Follow the host through doors. Off allows separate exploration; story scenes can still reunite you." },
    { SP_COOP, "Loot Sharing", V(g_coop.loot), 0, 1, 1, loots, .locked = in_session, .help = "Shared divides supplies between you. Individual gives each player a copy. Key items go to both.", .group = "HOST RULES" },
    { SP_COOP, "Duplicate Weapons and Ribbons", V(g_coop.important), 0, 1, 1, .locked = in_session,
      .help = "With Shared loot, give each player their own weapons and ink ribbons." },
    { SP_COOP, "Skip Cutscenes Together", V(g_coop.skip_sync), 0, 1, 1,
      .help = "Skip shared cutscenes only after both players choose to skip. The host decides." },
    { SP_COOP, "Game Over Rule", V(g_coop.gameover_both), 0, 1, 1, gameovers,
      .help = "Each Player retries alone. Both lets the host retry for both players, or return both to the title screen." },
    { SP_COOP, "Metal Detector", V(g_coop.skip_detector), 0, 1, 1, detectors,
      .help = "Skipped lets you pass with weapons. Security boxes stay shut unless they hold your items." },
    { SP_COOP, "Quick Start", V(g_coop.quick_start), 0, 1, 1,
      .help = "Start with the manual read and a knife in both players' inventories. No effect with BioRand." },
    { SP_COOP, "Pause Enemies During Trades", V(g_coop.trade_freeze), 0, 1, 1, .help = "Pause enemies while both players are on the trade screen. The host decides." },
    { SP_COOP, "Remember Partner Items", V(g_coop.remember), 0, 1, 1, .flags = SET_F1,
      .help = "Keep a partner's items for their next visit. After a crash, their items are restored automatically." },
    { SP_COOP, "Connection", V(g_coop.conn), 0, 2, 1, conns, .locked = in_session,
      .help = "Automatic tries direct, then a server if set. Direct uses an address; Server Relay uses a room code.", .group = "CONNECTION" },
    { SP_COOP, "Host Port", V(g_coop.port), 1, 65535, 1, .fmt = "%d", .locked = in_session, .help = "Set your hosting port. Open MULTIPLAYER at the title screen or press F3 to host or join." },
    { SP_COOP, "Automatic Port Forwarding", V(g_coop.upnp), 0, 1, 1, .locked = in_session, .help = "Ask your router to allow internet connections while you host." },

    { SP_EXTRAS, "Infinite Health", V(g_cheat.inf_health), 0, 1, 1, .group = "CHEATS" },
    { SP_EXTRAS, "Infinite Ammo", V(g_cheat.inf_ammo), 0, 1, 1 },
    { SP_EXTRAS, "Pass Through Walls", V(g_cheat.noclip), 0, 2, 1, noclips, .help = "Move through walls and enemies. Flight: Page Up or R2 rises; F, Page Down or L2 descends." },
    { SP_EXTRAS, "Unlock With No Key", V(g_cheat.unlock), 0, 1, 1,
      .help = "Open key locks without carrying the key. Story locks stay. No items are added." },
    { SP_EXTRAS, "Heal Now", NULL, .flags = SET_ACTION | SET_GAME_ONLY, .apply = heal },
    { SP_EXTRAS, "Item", NULL, 1, CHEAT_ITEMS - 1, 1, .get = item_get, .set = item_set, .name = item_name, .group = "ITEMS" },
    { SP_EXTRAS, "Quantity", V(g_cheat.give_count), 1, 999, 1, .fmt = "%d" },
    { SP_EXTRAS, "Add To Inventory", NULL, .flags = SET_ACTION | SET_GAME_ONLY, .apply = give },
    { SP_EXTRAS, "Enemy", NULL, 0, 0, 1, .get = spawn_get, .set = spawn_set, .name = spawn_nm, .count = spawn_count,
      .help = "Spawned enemies disappear when you leave. Bosses and scripted enemies may fail outside their own rooms.", .group = "SPAWN" },
    { SP_EXTRAS, "Spawn Ahead", NULL, .flags = SET_ACTION | SET_GAME_ONLY, .apply = spawn },
    { SP_EXTRAS, "Location", V(g_cheat.tp_stg), 0, 9, 1, .set = tp_stage_set, .name = tp_stage_nm,
      .help = "Choose a destination. Some rooms may not work at your current point in the story.", .group = "TELEPORT" },
    { SP_EXTRAS, "Room", NULL, 0, 99, 1, .get = tp_room_get, .set = tp_room_set, .name = tp_room_nm, .count = tp_room_n },
    { SP_EXTRAS, "Arrival Point", V(g_cheat.tp_pos), 0, 31, 1, .name = tp_pos_nm, .count = tp_pos_n,
      .help = "Choose which doorway you arrive at." },
    { SP_EXTRAS, "Use Current Room", NULL, .flags = SET_ACTION | SET_GAME_ONLY | SET_NOW, .apply = here, .help = "Select your current room and arrival point." },
    { SP_EXTRAS, "Teleport", NULL, .flags = SET_ACTION | SET_GAME_ONLY, .apply = teleport },
};
const int g_settings_n = (int)SDL_arraysize(g_settings);

/* ---------------------------------------------------------------- access
   Both menus write the variables directly (aligned ints: a reader sees the old or the new
   value). What must not race is the hand-off: the writer's store happens before its flag
   (release), settings_poll takes the flag (acquire) before it applies and saves, so the main
   thread always sees the value that raised the flag. */
static unsigned char dirty[256];
static int dirty_any;

int settings_get(const Setting* s) { return s->get ? s->get() : s->val ? *s->val : 0; }
int settings_hi(const Setting* s) { return s->count ? s->count() - 1 : s->hi; }
int settings_is_bool(const Setting* s) { return !(s->flags & SET_ACTION) && s->lo == 0 && s->hi == 1 && !s->names && !s->name && !s->fmt; }

void settings_set(const Setting* s, int v)
{
    int hi = settings_hi(s), i = (int)(s - g_settings);
    if (v < s->lo) v = s->lo;
    if (v > hi) v = hi;
    if (v == settings_get(s)) return;
    if (s->set) s->set(v); else if (s->val) *s->val = v;
    if (i >= 0 && i < (int)sizeof(dirty)) __atomic_store_n(&dirty[i], 1, __ATOMIC_RELEASE);
    __atomic_store_n(&dirty_any, 1, __ATOMIC_RELEASE);
}

void settings_step(const Setting* s, int dir)
{
    int v = settings_get(s), hi = settings_hi(s), st = s->step > 0 ? s->step : 1;
    int choice = s->names || (s->name && s->name != tenths) || settings_is_bool(s);   /* wrap around */
    if (s->flags & SET_ACTION) return;
    if (v < s->lo) v = s->lo - (dir > 0 ? st : 0);   /* not a listed value (window size) */
    v = (v / st + dir) * st;   /* onto the step grid */
    if (choice && v > hi) v = s->lo;
    if (choice && v < s->lo) v = hi;
    settings_set(s, v);
}

void settings_act(const Setting* s) { if (s->apply) s->apply(); }

const char* settings_value(const Setting* s, char* b, int n)
{
    int v = settings_get(s);
    if (s->flags & SET_ACTION) b[0] = 0;
    else if (s->name) s->name(v, b, n);
    else if (s->names) snprintf(b, n, "%s", s->names[v - s->lo]);
    else if (s->fmt) snprintf(b, n, s->fmt, v);
    else snprintf(b, n, "%s", v ? "On" : "Off");
    return b;
}

const char* settings_locked(const Setting* s)
{
    const char* r = s->locked ? s->locked() : NULL;
    if (!r && (s->flags & SET_GAME_ONLY) && !pc_in_game()) r = "Start or load a game first.";
    return r;
}

void settings_save_later(void) { __atomic_store_n(&dirty_any, 1, __ATOMIC_RELEASE); }

/* recvx.ini is written SAVE_DELAY_MS after the last change (a slider drag changes the value
   every frame), and at exit if that is still due */
#define SAVE_DELAY_MS 500
static int save_pending;
static void save_at_exit(void) { if (__atomic_exchange_n(&save_pending, 0, __ATOMIC_ACQ_REL)) config_save(); }

/* main thread, once per pumped frame: changes from either menu are applied here (F1 also
   applies its own widgets at once) and recvx.ini is written once they stop. This and the exit
   are the only places config_save() runs once the game thread is up. */
void settings_poll(void)
{
    static int stress = -1, n;
    static uint64_t save_at;
    if (stress < 0) { stress = SDL_getenv("RECVX_STRESS_SETTINGS") != NULL; atexit(save_at_exit); }
    if (stress)   /* test aid: both volumes change every pumped frame (applied and saved below) */
        for (int i = 0; i < g_settings_n; i++)
            if (g_settings[i].val == &g_config.music_volume || g_settings[i].val == &g_config.sfx_volume)
                settings_set(&g_settings[i], ++n * 37 % 101);
    if (__atomic_exchange_n(&dirty_any, 0, __ATOMIC_ACQ_REL)) {
        for (int i = 0; i < g_settings_n && i < (int)sizeof(dirty); i++)
            if (__atomic_exchange_n(&dirty[i], 0, __ATOMIC_ACQ_REL))
                if (g_settings[i].apply && !(g_settings[i].flags & SET_ACTION)) g_settings[i].apply();
        save_at = SDL_GetTicks() + SAVE_DELAY_MS;
        __atomic_store_n(&save_pending, 1, __ATOMIC_RELEASE);
    }
    if (save_at && SDL_GetTicks() >= save_at) {
        save_at = 0;
        save_at_exit();
    }
}
