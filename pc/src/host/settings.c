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
static const char* interp_off(void) { return g_config.interpolate ? NULL : "Needs Display-rate frames."; }
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
    if (!biorand_pack()) snprintf(b, n, "No pack");
    else if (v && biorand_seed_text()[0]) snprintf(b, n, "On (%s)", biorand_seed_text());
    else snprintf(b, n, v ? "On" : "Off");
    return b;
}
static const char* rifle_locked(void) { return biorand_active() ? "On while BioRand is on." : NULL; }
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
    snprintf(b, n, "%d%s", g_cheat.tp_rom, tp_reading() ? " (reading the room list)" : "");
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
static const char* const display_modes[] = { "Windowed", "Borderless fullscreen", "Exclusive fullscreen" };
static const char* const modes3[] = { "Original (4:3)", "Stretch to width", "Off" };
static const char* const fps_limits[] = { "Off (display rate)", "30", "60", "90", "120", "144" };   /* render.c present_hz */
static const char* const aspects[] = { "4:3 (original)", "Stretch to window", "Widescreen (hor+)" };
static const char* const save_systems[] = { "Native files", "Memory card (original)" };
static const char* const loots[] = { "Shared", "Individual" };
static const char* const gameovers[] = { "Each player", "Both" };
static const char* const detectors[] = { "As original", "Skipped" };
static const char* const conns[] = { "Automatic", "Direct (P2P)", "Via server" };
static const char* const atmos_names[] = { "Per cut (original)", "Smooth" };
static const char* const free_cam_names[] = { "B (behind character)", "A (free)" };
static const char* const fixed_cam_names[] = { "Classic (tank)", "Modern (camera-relative)", "Combined (d-pad tank)" };
static const char* const tps_pad_names[] = { "Classic (tank, camera behind)", "Modern (twin-stick)" };
static const char* const noclips[] = { "Off", "Through walls", "Walls + fly" };
static const char* const sources[] = { "Auto", "Keyboard/mouse only", "Gamepad only" };
static const char* const extras[] = { "Off", "Up to 1 per room", "Up to 2 per room", "Up to 3 per room" };
static const char* const varieties[] = { "Mild", "Wild" };
static const char* const cast_pools[] = { "Cutscene models (faces move)", "All models" };
static const char* const cast_assigns[] = { "Fixed per character", "Random per cutscene" };
static int cast_on(void) { return g_enemy_opts.cast != 0; }
static int cast_imports_shown(void) { return cast_on() && __atomic_load_n(&actor_import_n, __ATOMIC_ACQUIRE) > 0; }

#define V(x) ((int*)&(x))
const Setting g_settings[] = {
    { SP_DISPLAY, "Display mode", NULL, 0, 2, 1, display_modes, .get = dmode_get, .set = dmode_set, .apply = settings_apply_display, .group = "WINDOW",
      .help = "Borderless: a window covering the screen, quick to Alt+Tab. Exclusive: the screen switches to the Fullscreen resolution. F11: windowed and back." },
    { SP_DISPLAY, "Fullscreen resolution", NULL, 0, 0, 1, .get = fsres_get, .set = fsres_set, .name = fsres_name, .count = fsres_n, .shown = exclusive_on, .apply = settings_apply_display,
      .help = "Exclusive fullscreen: the screen's resolution and refresh rate. Desktop: as the desktop." },
    { SP_DISPLAY, "Window size", NULL, 0, 5, 1, .get = size_get, .set = size_set, .name = size_name, .locked = fullscreen_on, .apply = settings_apply_window },
    { SP_DISPLAY, "VSync", V(g_config.vsync), 0, 1, 1, .apply = apply_vsync },
    { SP_DISPLAY, "Frame rate limit", V(g_config.fps_limit), 0, 5, 1, fps_limits, .help = "Caps the images shown per second (enhanced in-between frames included) below the display rate, e.g. 60 on a 175 Hz monitor. The game itself always runs at 30." },
    { SP_DISPLAY, "Show frame rate", V(g_config.show_fps), 0, 1, 1 },
    { SP_DISPLAY, "Aspect ratio", V(g_config.aspect), 0, 2, 1, aspects, .help = "Widescreen: 3D fills the window, 2D and menus stay 4:3 in the middle.", .group = "PICTURE" },
    { SP_DISPLAY, "Internal resolution", V(g_config.scale), 0, 8, 1, .name = scale_name, .help = "The resolution the game is drawn at; Auto follows the window." },
    { SP_DISPLAY, "Display-rate frames", V(g_config.interpolate), 0, 1, 1, .help = "In-between frames blended from the last two game frames. The game still runs at 30 FPS; half a frame of latency." },
    { SP_DISPLAY, "Smooth menus", V(g_config.menu_smooth), 0, 1, 1, .locked = interp_off,
      .help = "Menus at the display rate too (windows slide, the cursor glides), a frame quicker to react, quick taps never missed. Off: 30 FPS menus." },
    { SP_DISPLAY, "Frame blend (trail)", V(g_config.frame_blend), -1, 1, 1, onoff_auto, .help = "As on the PS2: each frame mixed over the previous one. Auto: on in vanilla, off with display-rate frames." },
    { SP_DISPLAY, "Cutscene bars", V(g_config.cinema_bars), 0, 2, 1, modes3, .help = "Original keeps them 4:3 in widescreen; fades always fill the screen.", .group = "WIDESCREEN" },
    { SP_DISPLAY, "Camera vignette", V(g_config.screen_filter), 0, 2, 1, modes3, .help = "Original keeps it 4:3 in widescreen." },
    { SP_DISPLAY, "Screen effects", V(g_config.screen_effects), 0, 2, 1, modes3, .help = "Full-screen effect layers and the scope's tint. Original keeps them 4:3 in widescreen. The oil pressure puzzle's screen stays with Off (4:3)." },
    { SP_DISPLAY, "Movie zoom", V(g_config.movie_zoom), 0, 1, 1, .help = "Widescreen: movies grow, without stretching, until their black bars leave the screen (or the picture fills the width)." },
    { SP_DISPLAY, "Fog intensity", V(g_config.fog_intensity), 0, 100, 5, .fmt = "%d%%", .group = "FOG AND BRIGHTNESS",
      .help = "The fog and the black fog of dark rooms: 100% as on the PS2, 50% pushes it twice as far away, 0% removes it." },
    { SP_DISPLAY, "Brightness", V(g_config.brightness), -50, 50, 1, .fmt = "%+d", .flags = SET_CALIB,
      .help = "Lifts or lowers the dark end of the game's picture. Black strip: the left square should be barely visible." },
    { SP_DISPLAY, "Contrast", V(g_config.contrast), 50, 150, 1, .fmt = "%d%%", .flags = SET_CALIB,
      .help = "Spreads or narrows the tones around mid-grey. White strip: the right square should be barely visible." },
    { SP_DISPLAY, "Gamma", V(g_config.gamma), 5, 20, 1, .name = tenths, .flags = SET_CALIB,
      .help = "Higher brightens the mid-tones, lower darkens them; black and white stay." },
    { SP_DISPLAY, "Reset picture calibration", NULL, .flags = SET_ACTION | SET_NOW, .apply = picture_reset,
      .help = "Brightness, contrast and gamma back to neutral: the original picture." },
    { SP_DISPLAY, "Native 3D renderer", V(g_config.native_3d), 0, 1, 1, .help = "3D models projected and clipped by the GPU instead of the emulated VU1 and GS.", .group = "RENDERER" },
    { SP_DISPLAY, "Native textures", V(g_config.native_tex), 0, 1, 1, .help = "Textures uploaded once and bound afterwards instead of re-uploaded on every switch." },
    { SP_DISPLAY, "Cull off-screen models", V(g_config.model_cull), 0, 1, 1, .help = "As on the PS2. Off: characters, enemies and objects no longer pop at the screen edges (the room itself is not affected). Third person never culls." },
    { SP_DISPLAY, "HD textures", V(g_config.hd_textures), 0, 1, 1, .flags = SET_RESTART,
      .help = "A PCSX2 HD texture pack in mods/textures (as PCSX2 ships it) replaces the textures it covers. Off: the original textures." },
    { SP_DISPLAY, "HD textures: font", V(g_config.hd_textures_font), 0, 1, 1,
      .help = "The pack may replace the game's font (messages, item names and descriptions). Off: the original font. Takes effect for text loaded afterwards." },
    { SP_DISPLAY, "HD textures: UI", V(g_config.hd_textures_ui), 0, 1, 1,
      .help = "Also use replacements that show something else than the original, such as redesigned menus, title and inventory. Off: only replacements of the original picture. The port's own menu entries keep the original letters." },

    { SP_GAMEPLAY, "Skip intro", V(g_config.skip_intro), 0, 1, 1, .flags = SET_RESTART, .help = "Boot straight into the title menu: no warning screen, logos or intro movie. Off: any button skips each of them.", .group = "GENERAL" },
    { SP_GAMEPLAY, "Check for updates", V(g_config.update_check), 0, 1, 1, .apply = update_start,
      .help = "Once at start asks the project's update server for the newest version number, shown on the title screen. Nothing else is sent. Off: no network at all." },
    { SP_GAMEPLAY, "Skip door animations", V(g_config.skip_doors), 0, 2, 1, (const char* const[]){ "Off", "Instant", "Skippable" },
      .help = "Door, stairs and ladder animations. Off: always played. Instant: the game's load screen instead. Skippable: played, any key or button ends it." },
    { SP_GAMEPLAY, "Fast loading", V(g_config.fast_loading), 0, 1, 1,
      .help = "Room changes, new game and loading a save without the waits the PS2 needed for its disc and sound chip. Off: as the original. In co-op each player's own." },
    { SP_GAMEPLAY, "Battle Game unlocked", V(g_config.battle_unlocked), 0, 1, 1,
      .help = "The title menu offers BATTLE GAME without a finished playthrough, with all five characters. Saves and records stay as they are; off: as the original." },
    { SP_GAMEPLAY, "Difficulty select", V(g_config.difficulty_select), 0, 1, 1,
      .help = "NEW GAME asks for the difficulty, as the European and Japanese versions do: Normal, Easy (supplies in the item box, more ammo per pickup) or Very Easy (also twice the health, stronger healing, weaker enemies). Off: as the US original, always Normal." },
    { SP_GAMEPLAY, "Quick knife", V(g_config.quick_knife), 0, 1, 1,
      .help = "Hold the quick-knife key (Space, right trigger; see CONTROLS) to draw the knife without the inventory; release it for the weapon before." },
    { SP_GAMEPLAY, "Auto-climb stairs", V(g_config.auto_stairs), 0, 1, 1,
      .help = "Walking or running into a staircase climbs it without the action button. Steps onto or off a ledge, doors and items still need the button." },
    { SP_GAMEPLAY, "Head blow-offs", V(g_config.head_blowoff), 0, 1, 1, .help = "Enables head blow-offs on zombies." },
    { SP_GAMEPLAY, "Rifle ammo stacks", V(g_config.rifle_stack), 0, 1, 1, .locked = rifle_locked,
      .help = "Rifle and assault rifle bullets stack in one slot as the other ammo does. Off: each pickup takes a slot, as the original." },
    { SP_GAMEPLAY, "Save system", V(g_config.save_system), 0, 1, 1, save_systems,
      .help = "Native files: each save is its own file in the saves folder, as many as you like, no memory card screens. Memory card: the original 15 slots. From the next save screen.", .group = "SAVES" },
    { SP_GAMEPLAY, "Import memory card saves", NULL, .flags = SET_ACTION, .apply = import_card,
      .help = "Copies the memory card's saves into the saves folder as new files (ones already there are skipped). The card keeps them." },
    { SP_GAMEPLAY, "No ink ribbons needed", V(g_config.no_ink_ribbon), 0, 1, 1,
      .help = "Typewriters save without an ink ribbon and use none up. In co-op the host's setting counts (lobby GAME RULES)." },
    { SP_GAMEPLAY, "BioRand", V(g_config.biorand), 0, 1, 1, .name = biorand_name, .locked = biorand_locked, .shown = biorand_shown, .group = "RANDOMIZER",
      .help = "Plays the randomizer pack BioRand generated into mods/biorand (items, start inventory and doors from its seed). Off: the normal game, the folder is ignored. Changed at the title menu (also NEW GAME's difficulty screen)." },
    { SP_GAMEPLAY, "Extra enemies", V(g_enemy_opts.extra), 0, 3, 1, extras, .help = "More enemies of the types a room already has (also in single player). In co-op the host's setting counts. Next room.", .group = "ENEMIES" },
    { SP_GAMEPLAY, "Rearranged enemies", V(g_enemy_opts.place), 0, 1, 1, .help = "Free-roaming enemies start elsewhere in their room (from the seed). Story, cutscene and boss enemies stay. Next room." },
    { SP_GAMEPLAY, "Enemy randomizer", V(g_enemy_opts.rand), 0, 1, 1, .help = "Enemies become other types (from the seed), from anywhere in the game, within each room's limits. Bosses stay. Next room." },
    { SP_GAMEPLAY, "Randomizer variety", V(g_enemy_opts.variety), 0, 1, 1, varieties,
      .help = "Wild: every kind that fits the room, a different one for each enemy where possible, Tyrants and bats included. Mild: two common kinds per room. Next room." },
    { SP_GAMEPLAY, "Randomize event enemies", V(g_enemy_opts.events), 0, 1, 1,
      .help = "Enemies that rooms show later or that events drive change too: the ones events animate get another look of their own kind, so the scenes still play. Next room." },
    { SP_GAMEPLAY, "Enemy seed", V(g_enemy_opts.seed), 0, 99999, 1, .fmt = "%d", .help = "Placements, randomizer and cutscene cast: the same seed gives the same rooms. In co-op both use the host's." },
    { SP_GAMEPLAY, "Randomize cutscene cast", V(g_enemy_opts.cast), 0, 1, 1, .group = "CUTSCENE CAST",
      .help = "In cutscenes the story characters (Alfred, Steve, ...) are shown as other models, from the enemy seed. Only the look changes. Next room." },
    { SP_GAMEPLAY, "Character models", V(g_enemy_opts.cast_all), 0, 1, 1, cast_pools, .shown = cast_on,
      .help = "Cutscene models: the characters' cutscene models, whose mouths and eyes move with the lines. All models: any character, creature or zombie model and the player models; most of them keep a still face." },
    { SP_GAMEPLAY, "Assignment", V(g_enemy_opts.cast_assign), 0, 1, 1, cast_assigns, .shown = cast_on,
      .help = "Fixed: each character is always shown as the same model, e.g. Steve as Alfred in every cutscene. Random: every cutscene room picks again. The same seed gives the same picks." },
    { SP_GAMEPLAY, "Include imported models", V(g_enemy_opts.cast_imports), 0, 1, 1, .shown = cast_imports_shown,
      .help = "The models in the mods folder's import list join the character models. In co-op only when both players have the same ones." },

    { SP_CONTROLS, "Input source", V(g_input_source), 0, 2, 1, sources, .apply = bind_apply_source,
      .help = "Which devices play in this window. Auto: keyboard/mouse and gamepad, while the window has focus. Keyboard/mouse "
              "only: no gamepads. Gamepad only: no keys or mouse (hotkeys still work), and the gamepad works without focus: two "
              "windows side by side for local co-op." },
    { SP_CONTROLS, "Gamepad", V(g_input_pad), 0, 4, 1, .name = bind_pad_choice,
      .help = "Any: the first one connected. 1-4: the gamepad the system numbers so (Xbox: its slot / light, PlayStation and "
              "Switch pads show it on their player lights); give each window its own number for local co-op." },
    { SP_CONTROLS, "Vibration strength", V(g_config.rumble), 0, 100, 10, .fmt = "%d%%",
      .help = "How strongly the gamepad rumbles. Turn vibration on or off in the game's OPTION menu (VIBRATION), which a gamepad with rumble makes available." },
    { SP_CONTROLS, "Mouse in menus", V(g_menu_mouse.on), 0, 1, 1, .help = "Inventory, item box, map, files and these menus: point, left click selects, right click goes back." },
    { SP_CONTROLS, "Fixed camera controls", V(g_modern_on), 0, 2, 1, fixed_cam_names,
      .help = "Modern: the left stick or the d-pad (arrow keys) walks where it points on screen, pushed far or with run it runs; "
              "aiming and the quick turn as Classic. Combined: the stick and arrow keys as Modern, the gamepad's d-pad as Classic "
              "(tank). Not in third person." },

    { SP_TPS, "Over-the-shoulder camera", V(g_tps.on), 0, 1, 1, .help = "Third person: mouse/keyboard or twin-stick gamepad controls and free aim. Events, doors and menus keep the original camera.", .group = "CAMERA" },
    { SP_TPS, "Mouse/keyboard camera", V(g_tps.free_cam), 0, 1, 1, free_cam_names,
      .help = "B: the camera stays behind the character, the mouse turns it. A: the mouse turns the camera around the character, WASD walk towards their direction on screen; aiming turns the character to the camera. Not the KEY CONFIG type." },
    { SP_TPS, "Field of view", V(g_tps.fov), 40, 120, 5, .fmt = "%d deg", .help = "Horizontal, over the 4:3 picture; widescreen shows more at the sides." },
    { SP_TPS, "Field of view aiming", V(g_tps.aim_fov), 30, 120, 5, .fmt = "%d deg" },
    { SP_TPS, "Camera distance", V(g_tps.dist), 0, 400, 5, .name = tenths },
    { SP_TPS, "Camera distance aiming", V(g_tps.aim_dist), 0, 400, 5, .name = tenths },
    { SP_TPS, "Camera height", V(g_tps.height), 0, 300, 5, .name = tenths },
    { SP_TPS, "Shoulder offset", V(g_tps.shoulder), 0, 150, 5, .name = tenths },
    { SP_TPS, "Left shoulder", V(g_tps.left), 0, 1, 1 },
    { SP_TPS, "Light and fog", V(g_tps.atmos), 0, 1, 1, atmos_names,
      .help = "Smooth: lights, fog and screen filter fade over a second when the room's camera cut changes, fog measured from your camera. Per cut: they switch with the cut, as the fixed cameras do." },
    { SP_TPS, "Mouse sensitivity X", V(g_tps.sens_x), 5, 400, 5, .fmt = "%d%%", .group = "MOUSE AND AIM" },
    { SP_TPS, "Mouse sensitivity Y", V(g_tps.sens_y), 5, 400, 5, .fmt = "%d%%" },
    { SP_TPS, "Aiming sensitivity", V(g_tps.aim_sens), 10, 200, 5, .fmt = "%d%%", .help = "Mouse sensitivity while aiming, relative to the above." },
    { SP_TPS, "Invert Y", V(g_tps.invert_y), 0, 1, 1 },
    { SP_TPS, "Crosshair", V(g_tps.crosshair), 0, 1, 1 },
    { SP_TPS, "Game auto-aim", V(g_tps.auto_aim), 0, 1, 1, .help = "Off: free aim, the shot goes where the crosshair points. On: the game's lock-on; with the gamepad (Modern) a gentle aim assist instead." },
    { SP_TPS, "Precise hits", V(g_tps.precise), 0, 1, 1,
      .help = "On: a free-aim shot hits an enemy only where it meets its body, a head hit does more damage, and a zombie's head bursts only from a hit on the head. Off: the game's wide hit test, which also hits beside an enemy." },
    { SP_TPS, "Gamepad controls", V(g_tps.pad_modern), 0, 1, 1, tps_pad_names, .group = "GAMEPAD",
      .help = "Modern: the left stick walks where it points seen from the camera, the right stick turns the camera and aims; "
              "own bindings (CONTROLS, gamepad, Third person). Classic: tank controls through the game's buttons, the camera behind." },
    { SP_TPS, "Stick camera speed X", V(g_tps.pad_sens_x), 10, 300, 5, .fmt = "%d%%", .help = "Right stick fully pushed at 100%: 180 degrees a second." },
    { SP_TPS, "Stick camera speed Y", V(g_tps.pad_sens_y), 10, 300, 5, .fmt = "%d%%" },
    { SP_TPS, "Stick aiming speed", V(g_tps.pad_aim_sens), 10, 200, 5, .fmt = "%d%%", .help = "Right stick while aiming, relative to the above." },
    { SP_TPS, "Stick invert Y", V(g_tps.pad_invert_y), 0, 1, 1 },
    { SP_TPS, "Left stick dead zone", V(g_tps.pad_dead_move), 0, 50, 5, .fmt = "%d%%" },
    { SP_TPS, "Right stick dead zone", V(g_tps.pad_dead_look), 0, 50, 5, .fmt = "%d%%" },

    { SP_AUDIO, "Music and voice", V(g_config.music_volume), 0, 100, 5, .fmt = "%d%%", .apply = apply_volume },
    { SP_AUDIO, "Sound effects", V(g_config.sfx_volume), 0, 100, 5, .fmt = "%d%%", .apply = apply_volume },

    { SP_APPEAR, "You appear as", NULL, 0, 0, 1, .get = chr_get, .set = chr_set, .name = chr_name, .count = appear_count, .help = "Your model, also in single player and cutscenes; in co-op your partner sees it too." },
    { SP_APPEAR, "Use gender-matched animations", V(g_appear_anim), 0, 1, 1, .help = "A man shown while you play Claire walks, aims and fights with Chris's motions; a woman shown while you play a man, with Claire's." },
    { SP_APPEAR, "Swapped model in cutscenes", V(g_appear_cutscene), 0, 1, 1, .help = "Off: cutscenes show the real Claire or Chris, with their own hands, faces and voice; the swapped model is back when you are in control again." },
    { SP_APPEAR, "Talking faces in cutscenes (experimental)", V(g_appear_face), 0, 1, 1, .help = "The swapped model moves its mouth with the lines, with the story cast's own faces (a player file shown there becomes the same character's cutscene model). From the next room with a cutscene on." },

    { SP_COOP, "Partner's name tag", V(g_coop.nametag), 0, 1, 1, .help = "The partner's name above its head, or at the screen edge when it is out of view.", .group = "YOUR CHOICES" },
    { SP_COOP, "Show ping", V(g_config.show_ping), 0, 1, 1, .flags = SET_F1, .help = "In co-op, while connected: the round trip to the partner next to the frame rate." },
    { SP_COOP, "Hide IP addresses", V(g_coop.hide_ip), 0, 1, 1, .flags = SET_F1,
      .help = "For streaming and screenshots: addresses in the lobby and F3 show as xxx.xxx.xxx.xxx, address fields as stars. Copying and joining still use the real ones." },
    { SP_COOP, "Follow host (joining player)", V(g_coop.follow), 0, 1, 1, .help = "The joining player's choice. On: you go through every door the host takes. Off: you stay and may explore other rooms; story cutscenes and character switches still bring you to the host." },
    { SP_COOP, "Loot (host)", V(g_coop.loot), 0, 1, 1, loots, .locked = in_session, .help = "Shared: each item lies there once, whoever takes it has it (split the supplies). Individual: each player takes their own copy. Key items go to both.", .group = "GAME RULES (HOST)" },
    { SP_COOP, "Important items for both (host)", V(g_coop.important), 0, 1, 1, .locked = in_session,
      .help = "Shared loot: weapons and ink ribbons lie there for each player (taking one leaves the partner's). Off: they are one pickup like ammo and herbs." },
    { SP_COOP, "Synchronized cutscene skip (host)", V(g_coop.skip_sync), 0, 1, 1,
      .help = "A cutscene both of you watch is skipped once both pressed skip; the partner sees that you want to." },
    { SP_COOP, "Game over (host)", V(g_coop.gameover_both), 0, 1, 1, gameovers,
      .help = "Each player: whoever dies retries alone. Both: one game over is both players' and the host answers Retry for both - Yes takes both back to the last continue point with the items each had there, No takes both to the title." },
    { SP_COOP, "Metal detector (host)", V(g_coop.skip_detector), 0, 1, 1, detectors,
      .help = "Skipped: the prison's metal detector lets you through with weapons and its security boxes stay shut (one holding your items still opens)." },
    { SP_COOP, "Quick start (host)", V(g_coop.quick_start), 0, 1, 1,
      .help = "A new game starts with the manual already read (it stays in FILE) and the combat knife in the bag instead of on the table, for both of you." },
    { SP_COOP, "Freeze enemies while trading (host)", V(g_coop.trade_freeze), 0, 1, 1, .help = "While both players are in the trade screen the room's enemies stand still (the host's setting counts)." },
    { SP_COOP, "Remember partner inventories (host)", V(g_coop.remember), 0, 1, 1, .flags = SET_F1,
      .help = "Keep each partner's items when it leaves, kept with your saves: it can join again with them (ITEMS: Saved with the host), and gets them back after its game crashed." },
    { SP_COOP, "Connection", V(g_coop.conn), 0, 2, 1, conns, .locked = in_session,
      .help = "Automatic: with a co-op server set (F3 or the lobby), hosting also opens a room and joins by room code try a direct path first, then the server's relay; without a server, as Direct. Direct: address and join code only. Via server: always through the relay.", .group = "CONNECTION" },
    { SP_COOP, "Host port", V(g_coop.port), 1, 65535, 1, .fmt = "%d", .locked = in_session, .help = "Hosting and joining: MULTIPLAYER in the title menu, or F3." },
    { SP_COOP, "Open port (UPnP)", V(g_coop.upnp), 0, 1, 1, .locked = in_session, .help = "While you host, ask the router to forward the port so a friend can join over the internet." },

    { SP_EXTRAS, "Infinite health", V(g_cheat.inf_health), 0, 1, 1, .group = "CHEATS" },
    { SP_EXTRAS, "Infinite ammo", V(g_cheat.inf_ammo), 0, 1, 1 },
    { SP_EXTRAS, "Noclip", V(g_cheat.noclip), 0, 2, 1, noclips, .help = "Walls and enemies no longer block you. Fly: Page Up up, F or Page Down down, or R2/L2 on the gamepad." },
    { SP_EXTRAS, "Unlock all", V(g_cheat.unlock), 0, 1, 1,
      .help = "Examining a locked door, card reader or key slot uses its key there, as if you carried it. Switching it on or off counts at once, also in the room you are in. Nothing is added to the inventory." },
    { SP_EXTRAS, "Heal now", NULL, .flags = SET_ACTION | SET_GAME_ONLY, .apply = heal },
    { SP_EXTRAS, "Item", NULL, 1, CHEAT_ITEMS - 1, 1, .get = item_get, .set = item_set, .name = item_name, .group = "ITEMS" },
    { SP_EXTRAS, "Count", V(g_cheat.give_count), 1, 999, 1, .fmt = "%d" },
    { SP_EXTRAS, "Add to inventory", NULL, .flags = SET_ACTION | SET_GAME_ONLY, .apply = give },
    { SP_EXTRAS, "Enemy", NULL, 0, 0, 1, .get = spawn_get, .set = spawn_set, .name = spawn_nm, .count = spawn_count,
      .help = "Bosses and scripted enemies may not work outside their room; spawned enemies are gone after leaving it.", .group = "SPAWN" },
    { SP_EXTRAS, "Spawn in front of me", NULL, .flags = SET_ACTION | SET_GAME_ONLY, .apply = spawn },
    { SP_EXTRAS, "Teleport location", V(g_cheat.tp_stg), 0, 9, 1, .set = tp_stage_set, .name = tp_stage_nm,
      .help = "Every room on the disc and in the mods folder, by its save menu name where the game has one. Uses the game's door transition; a room may not work in every story state.", .group = "TELEPORT" },
    { SP_EXTRAS, "Teleport room", NULL, 0, 99, 1, .get = tp_room_get, .set = tp_room_set, .name = tp_room_nm, .count = tp_room_n },
    { SP_EXTRAS, "Teleport entry", V(g_cheat.tp_pos), 0, 31, 1, .name = tp_pos_nm, .count = tp_pos_n,
      .help = "Where you arrive: the room's entry points, by the door that leads there." },
    { SP_EXTRAS, "Current room", NULL, .flags = SET_ACTION | SET_GAME_ONLY | SET_NOW, .apply = here, .help = "Fills in the room you are in." },
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
    if (!r && (s->flags & SET_GAME_ONLY) && !pc_in_game()) r = "In game only.";
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
