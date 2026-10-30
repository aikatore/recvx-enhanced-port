/* Internal interfaces of the PC platform layer (not seen by decomp code). */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "ps2sdk_shim.h"
#include "ps2_hw.h"
#include "host/fileio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* host (pc/src/host) */
enum { ASPECT_4_3, ASPECT_STRETCH, ASPECT_WIDE };
typedef struct {
    char iso_path[1024];
    char save_dir[1024];
    int  window_w, window_h;
    int  fullscreen;         /* F11 flips it; the kind: fullscreen_exclusive */
    int  fullscreen_exclusive;   /* 0 borderless (the desktop's size), 1 exclusive (a display mode, settings.c) */
    int  fullscreen_w, fullscreen_h, fullscreen_hz;   /* exclusive: the mode (Hz rounded); 0 = the desktop's */
    int  borderless_extra;   /* Windows borderless: pixels the window reaches past the monitor (settings.c), 0 = exactly it */
    int  vsync;
    int  fps_limit;          /* presents per second: 0 display rate, else index into render.c fps_limits (30/60/90/120/144) */
    int  log_level;
    /* user settings (F1 overlay, saved in <save_dir>/recvx.ini) */
    int  scale;              /* internal resolution multiplier, 0 = auto */
    int  aspect;             /* ASPECT_* */
    int  show_fps;
    int  show_ping;                 /* co-op: the ping next to the frame rate while connected (ini show_ping) */
    int  music_volume;       /* 0-100 */
    int  sfx_volume;         /* 0-100 */
    int  rumble;             /* gamepad vibration strength 0-100 % (input/pad.c scePadSetActDirect) */
    int  interpolate;        /* enhanced mode: display-rate in-between frames of the 30 FPS game */
    int  menu_smooth;        /* menus' 2D tagged for in-between frames too (view_glue.c pc_menu_tag) */
    int  frame_blend;        /* PS2_swap's 25% previous-frame blend (motion trail): 1 on, 0 off, -1 auto (off in enhanced) */
    int  screen_filter;      /* camera filter (vignette): 0 original 4:3, 1 stretched to the view, 2 off */
    int  cinema_bars;        /* cutscene letterbox bars: same modes */
    int  fog_intensity;      /* fog at this % of the original, 0..100 (pc_fog_scale); 100 original, 0 none */
    int  movie_zoom;         /* movies scaled evenly until the letterbox bars leave the screen (movie_glue.c vbrank_draw) */
    int  screen_effects;     /* full-screen effect overlays (noise/scanline layers): same modes */
    int  brightness;         /* picture calibration in the present pass (gs_gl.c draw_display): -50..50, 0 neutral */
    int  contrast;           /* 50..150 %, 100 neutral */
    int  gamma;              /* tenths, 5..20, 10 neutral */
    int  skip_doors;         /* door/stairs/ladder animations: 0 played, 1 instant (load screen), 2 played, a press skips */
    int  fast_loading;       /* room loads: sound packs and texture lists at once instead of spread over frames (pc_fast_loading) */
    int  quick_knife;        /* holding the quick-knife key draws the knife (pc/glue/knife_glue.c) */
    int  auto_stairs;        /* walking into stairs climbs them without the action button (pc/glue/stairs_glue.c) */
    int  head_blowoff;       /* a zombie whose head the game destroyed is drawn without it (pc/glue/variant_glue.c pc_zombie_head) */
    int  model_cull;         /* skip actors/objects whose test points are off-screen (as the PS2) */
    int  skip_intro;         /* boot straight into the title menu */
    int  battle_unlocked;    /* the title menu offers BATTLE GAME without a cleared game (pc/glue/lobby_glue.c) */
    int  difficulty_select;  /* NEW GAME asks for the difficulty as the PAL/JP versions (adv.c CheckButton) */
    int  biorand;            /* BioRand: the pack in <mods>/biorand is served (pc/glue/biorand_glue.c); changed at the title only */
    int  rifle_stack;        /* rifle and assault rifle bullets stack as the other ammo (always while BioRand is on) */
    int  no_ink_ribbon;      /* typewriters save without an ink ribbon and use none (in co-op the host's; pc_no_ink_ribbon) */
    int  save_system;        /* 0 native save files (<save_dir>/saves, default), 1 the memory card (<save_dir>/card0) */
    int  native_3d;
    int  native_tex;         /* rendering rework: TIM2 textures uploaded once, bound afterwards (pc/glue/texture_glue.c) */          /* rendering rework: chunk models drawn by the GPU (pc/glue/native_glue.c) */         /* room transitions (doors, stairs, ladders) without their animation */
    int  hd_textures;        /* PCSX2 HD texture pack from mods/textures or hd_textures_dir (pc/src/gs/hdpack.c) */
    int  hd_textures_font;   /* the pack may replace the game's font pages too (default 0: the original font) */
    int  hd_textures_ui;     /* the pack's replacements that differ from the original (redesigned menus) are used too (default 1) */
    char hd_textures_dir[1024];   /* ini hd_textures_dir, --hd-textures DIR: the pack folder instead of mods/textures */
    int  update_check;       /* ask the project's update server for the newest version (pc/src/net/update.c) */
} HostConfig;
extern HostConfig g_config;
void config_load(void);
void config_save(void);

/* overlay (host/overlay.cpp) */
void overlay_init(void* window, void* gl_context);
int  overlay_event(const void* sdl_event);   /* 1 if the overlay consumed it */
void overlay_render(void);
void overlay_point(int flags, float x, float y);   /* render thread, before overlay_render: the shown frame's stream_point */

/* gs_gl.c */
float gs_wanted_scale(void);
void  gs_set_scale(float scale);
void  gs_update_widescreen(void);   /* apply g_config.aspect / window aspect (between frames) */
float gs_picture(float c);          /* a 0..1 grey through brightness/contrast/gamma, as the present shader */
void host_pump_events(void);
void host_present(void);
void host_fatal(const char* fmt, ...);
void host_log(const char* fmt, ...);
void crash_thread_init(void);   /* log.c: first thing in a thread that may crash (the crash report's signal stack) */

/* kernel.c */
void ps2_poll_interrupts(void);
void ps2_wait_vblank(void);
uint64_t ps2_vblank_count(void);

/* memory.c */
#define PS2_SPR_BASE 0x70000000u
#define PS2_SPR_SIZE 0x4000u
void  ps2_memory_init(void);
void* ps2_dma_ptr(uint32_t addr);
void  ps2_dma_host(int slot, const void* p, uint32_t len);   /* a heap block DMA may read (slot 0..3; NULL: none) */

/* dma.c / vif.c / gs */
void ps2_vif1_transfer(const uint32_t* data, uint32_t qwc);
void ps2_gif_transfer(int path, const uint32_t* data, uint32_t qwc);

/* cdvd.c */
int  cdvd_open_image(const char* path);
int  cdvd_read_sectors(uint32_t lsn, uint32_t count, void* dst);
int  cdvd_find_file(const char* path, uint32_t* lsn, uint32_t* size);

#ifdef __cplusplus
}
#endif
void ps2_vif1_transfer_words(const uint32_t* w, uint32_t n);
