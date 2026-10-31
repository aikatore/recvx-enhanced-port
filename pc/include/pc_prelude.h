/*
 * Force-included (-include) into every decomp translation unit of the PC build.
 *
 * The decomp targets MWCC/ee-gcc for the PS2 EE, where:
 *   - long is 64 bits (GS/GIF packet code relies on it; see override_katana.h)
 *   - 128-bit integer types exist
 * Host system headers are pulled in first, then 'long' is widened so the
 * decomp sees the same data model it was written for.
 */
#pragma once
#ifndef TARGET_PC
#define TARGET_PC 1
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <limits.h>
#include <ctype.h>
#include <errno.h>
#include <assert.h>

/* CRI xpt: pre-empt the EE-only mode(TI) typedefs. */
#define _TYPEDEF_Uint64
#define _TYPEDEF_Sint64
#define _TYPEDEF_Uint128
#define _TYPEDEF_Sint128
typedef uint64_t Uint64;
typedef int64_t  Sint64;
typedef struct { uint32_t w[4]; } __attribute__((aligned(16))) Uint128;
typedef Uint128 Sint128;

/* EE 128-bit ints only appear in movie/DMA structs.
   ponytail: keyword macro, revisit if a 64-bit target needs real __int128 */
#define __int128 Uint128

/* decomp function names that collide with libc symbols */
#define index recvx_index
#ifdef _WIN32
/* The game assumes RAND_MAX 0x7fffffff (macros.h: njRandom() = -rand() / 2^31); UCRT's is
   0x7fff. Use glibc's generator instead: same range and the same sequence as the Linux build. */
int recvx_rand(void);
void recvx_srand(unsigned seed);
#define rand recvx_rand
#define srand recvx_srand
#endif

/* MWCC cast-as-lvalue idioms (see macros.h) */
static inline void* ps2_postinc(void** pp, size_t n) { void* p = *pp; *pp = (char*)p + n; return p; }
#define PS2_POSTINC(T, X) (*(T*)ps2_postinc((void**)&(X), sizeof(T)))
#define PS2_PTRADD(X, n) ((X) = (void*)((char*)(X) + (n)))

/* hor+ widescreen: horizontal extent of the view relative to 4:3 (1.0 = original) */
float pc_hor_scale(void);
float pc_clip_guard(void);
int   pc_is_room_object(const void* obj);   /* part of the current room's model */
int   pc_margin_part(const void* obj);      /* hor+, fixed camera: a room part its cut hides is drawn (view_glue.c) */
int   pc_margin_children(const void* obj);   /* before a node's children: a part left out takes what it holds along */
void  pc_margin_children_end(int n);       /* after them: pc_margin_children's result */
int   pc_model_clip(const void* center, float r);   /* third person, hor+: model sphere test (view_glue.c) */
/* rendering rework: chunk-model strips drawn natively (pc/glue/native_glue.c) */
int   pc_native_3d(void);
struct VU1_STRIP_BUF;
struct VU1_PRIM_BUF;
unsigned int pc_native_prim3d(unsigned int prim, const struct VU1_PRIM_BUF* in, int n, int deferred, float* zavg);
unsigned int pc_native_prim(unsigned int prim, const unsigned int* dp, int n, int clip_3d_on, int deferred, float* last_sz);
unsigned int pc_native_prim_raw(unsigned int prim, const unsigned int* dp, int n, int clip_3d_on, float* last_sz);
void  Ps2AddPrim3DExNative(unsigned long long prim, void* dp, unsigned int num, int mode);   /* 0 Ex, 1 Ex1P, 2 ExI */
#define NATIVE_MARKER_REG 0x7F
#define STREAM_TAG_REG 0x7E   /* gs/native.h */
/* native textures (pc/glue/texture_glue.c) */
int   pc_native_tex(void);
unsigned int pc_tex_begin(const void* tim2, const void* phead, unsigned int image_adr, unsigned int clut_adr, int* is_new);
void  pc_tex_end(unsigned int id, unsigned int image_adr, unsigned int clut_adr, int is_new);
int   pc_native_rtex(void);   /* StoreRenderTex on the GPU (RECVX_NATIVE_RTEX, default on) */
void  pc_rtex_capture(void* tim2, void* image, unsigned int w, unsigned int h);
extern int pc_rtex_wide;      /* set before StoreRenderTex: capture the hor+ widened view (the full-screen render texture) */
int   pc_rtex_bind(const void* image, unsigned int image_adr, unsigned int clut_adr);
void  pc_native_strip(const unsigned short* idx_uv, int n, const struct VU1_STRIP_BUF* vbuf, unsigned int prim,
                      int color_mode, int single, float uv_scale);   /* enhanced mode: clip volume enlarged into the GS guard band */
int   pc_screen_filter_mode(void);
int   pc_skip_door_demo(void);       /* "Skip door animations" Instant: the load screen, no demo */
int   pc_door_skip_pressed(int start);   /* Skippable: a press since the demo began (start: it begins now) */
int   pc_intro_pressed(int start);       /* boot logos, intro movie, ending and credits: a press since the screen began (start: it begins now) */
int   pc_movie_vblanks_left(int frames); /* the playing movie of that many pictures: vblanks until it would end by itself */
int   pc_fast_loading(void);         /* "Fast loading": room sound packs and texture lists at once */
/* mods folder (pc/src/audio/adx.c, docs/modding/README.md "The mods folder"): size of <mods>/name or -1; ADXF handle or NULL */
int   pc_mod_file_size(const char* name);
void* pc_mod_open(const char* name);
/* languages (pc/glue/lang_glue.c, docs/LANGUAGES.md): the language font's width of a glyph (< 0: the game's own);
   bhSetRoom's message table in the language */
float pc_font_width(unsigned short code);
float pc_font_space(void);      /* a space's (0xFF01) advance at scale 1 */
int   pc_font_glyph(int code, void** texlist, float* dx);   /* bhDispFont: the cell to draw (-1 none); *texlist another font's; *dx its shift */
const unsigned short* pc_lang_mes(int which, const unsigned short* us);   /* the executable's short texts (lang.h LM_*) */
int   pc_lang_file_page(float x, float y, int file, int page);   /* a file page as the language's picture (1 drawn) */
int   pc_lang_page_end(int us);   /* PlayPageCheck: the playing manual's hidden pages in the language's layout */
unsigned int* pc_lang_room_mes(unsigned int* mesp);
unsigned int* pc_lang_sys_mes(unsigned int* sp);   /* the system messages read (SYSMES.ALD): whole, the US ones where not */
int   lang_picture(int adv);    /* the language of the ADV.AFS picture read last (0: English) */
int   lang_eu_picture(int adv); /* ... is a European one (its title layout) */
int   pc_model_cull(void);      /* actors/objects off-screen are skipped (bhCheckClipModel/Point) */
int   pc_frame_blend(void);     /* PS2_swap blends the new frame 75% over the previous one (motion trail) */
unsigned int pc_noclip(void);   /* F2 cheat: player flags whose enemy/wall push bhControlPlayer skips */
/* plp's frame and blend count as its bhSetMotion in bhControlPlayer takes them (it then advances and
   decrements them), -1 none this tick, and the motion and set they belong to: the gender-matched copy and
   the co-op partner play these (actor_glue.c) */
extern int pc_plp_frm, pc_plp_hokan;
extern unsigned int pc_plp_mtn;
extern void* pc_plp_set;
int   pc_inf_ammo(void);      /* F2 cheat (pc/glue/cheat_glue.c): bhCheckBullet/bhCountBullet as for an unlimited weapon */
int   pc_auto_stairs(void);   /* option (pc/glue/stairs_glue.c): bhControlPlayer without an action press: 1 = examine as for one (walked into stairs) */
/* third-person option "Precise hits" (pc/glue/tps_glue.c): weapon.c bhCheckGunAtari tests the player's hitscan shot (on: 1)
   against the enemies' bodies (shot c1 -> c2: the joint hit or -1, hp the point), each gun hit tells the enemy's damage
   (d0 before it) whether it was precise; en01.c bhEne01_CheckExpHead: 1 = the zombie's hit was precise */
struct BH_PWORK;
int   pc_precise_hits(void);
int   pc_precise_hit(struct BH_PWORK* pp, int wpn, const float* c1, const float* c2, float* hp);
void  pc_precise_dam(struct BH_PWORK* pp, int jnt, int d0, int precise, int wpn);
int   pc_precise_head(struct BH_PWORK* pp);
/* F2 cheat "Unlock all" (pc/glue/cheat_glue.c): bhPlItemCheck passes for a key at its lock; bhControlPlayer's examine
   press in a key's use spot (1: taken) and bhControlEvent around scd1 (0 before, 1 after) use the key there;
   bhUseItemCheck / bhCmpB found the key used tested; bhInitEvent: a room's entry script is about to run */
int   pc_unlock_held(int item);
int   pc_unlock_examine(void);
void  pc_unlock_scd1(int after);
void  pc_unlock_item_tested(void);
void  pc_unlock_room(void);
void  cheat_game_exit(void);   /* system.c bhExitGame: the game ends (continue, title, load); cheat state of that game goes */
float pc_fog_scale(void);              /* option: njCalcFogPower's fog density x this (ps2_NaFog.c), 1 = original */
int   pc_movie_zoom(void);               /* option: movies zoomed until their letterbox bars leave the screen */
int   pc_skip_intro(void);               /* option: boot straight to the title menu */
int   pc_skip_intro_press_start(void);   /* ... and past "press start", once */     /* option: room transitions without the door/stairs animation */   /* camera filter/vignette: 0 original, 1 stretched, 2 off */
/* full-screen 2D overlays (cinematic bars, fades, screen effects) in hor+ widescreen:
   begin returns 0 if the overlay is switched off; inside, 2D polygon x is stretched if set */
enum { PC_OVL_CINEMA, PC_OVL_FADE, PC_OVL_EFFECT };
int   pc_overlay_begin(int kind);
void  pc_overlay_end(void);
float pc_overlay_x(float x);
int   pc_overlay_wide(void);   /* inside a stretched overlay */
/* hor+ widescreen: while on, 2D (Ps2AddPrim with clip_3d_on 0) is clipped to the 4:3 screen,
   except stretched overlays and full-width fills (native_glue.c) */
void  pc_menu_clip(int on);
/* "Smooth menus" on and a menu task up (view_glue.c): ps2_NaSystem.c Ps2SwapDBuff hands the finished
   frame over before its wait for the second VBLANK */
int   pc_menu_smooth(void);
/* enhanced frame interpolation: GS output between begin and end belongs to this mesh */
void  pc_mesh_begin(const void* model);
void  pc_mesh_end(void);
void  pc_mesh_strip(const void* strip);   /* within a mesh: the following output is this strip */
void  pc_margin_next(int mode);   /* the next mesh is drawn only outside the 4:3 screen (native 3D, hor+); mode: view_glue.c pc_margin_mesh */
int   pc_margin_reaches_43(const struct VU1_STRIP_BUF* v, int n);   /* such a mesh would cover the view (native_glue.c): left out */
void  pc_effect_tag(const void* op);      /* effect.c: the following output is this effect (O_WRK*, NULL: none) */
unsigned long long pc_effect_poly(void); /* ps2_dummy.c Ps2AddOT: STREAM_TAG_REG value for an effect polygon (0: none) */
/* online co-op (pc/glue/coop_glue.c, docs/COOP.md): the hooks are only called while coop_on */
extern int coop_on;
void  coop_control_remote(void);
void  coop_draw_remote(void);
void  coop_textures_released(void);
void  coop_player_unload(void);                /* system.c bhSysCallMonitor: the player's textures go, another character loads (story switch, Battle Game rooms) */
struct BH_PWORK;
int   coop_enemy_begin(struct BH_PWORK* ep);   /* 1 = step done (client puppet) */
int   coop_enemies_run(void);                  /* eneset.c bhControlEnemy: the host's enemies run although our examine paused them */
void  coop_enemy_end(struct BH_PWORK* ep);
int   coop_partner_step(void);                 /* 1 while an enemy's step runs against the partner's stand-in (plp) */
void  coop_scd1(void);                         /* event.c bhControlEvent: runs the room's per-frame script */
void  coop_fx_player(void);                    /* game.c, before bhControlPlayer: our step's effects to the partner */
int   coop_fx_step(int i, int after);          /* effect.c bhControlEffect, around effect i's step (1 before: call after) */
void  coop_item_granted(int item);            /* sub1.c GetItem: an event gave us this item without asking */
void  coop_item_taken(int bit, int etc);      /* event.c bhItmCk: we took the item of trigger etc, its it_flg bit */
void  coop_item_late(int bit);                /* event.c bhItmCk: we took an item whose it_flg bit was set already (the partner was first) */
int   coop_examine(int etc);                  /* hitchk.c bhCheckExmAtari: we examine trigger etc; 1: the partner is using it (not now) */
int   coop_replayed_event(void);              /* event.c bhEvtComSet: the running event is one of the partner's, replayed (no terminal) */
void  coop_scd0(int after);                   /* event.c bhInitEvent: before (0) and after (1) the room's entry script (a solved flag kept) */
void  coop_retry_point(void);                 /* room.c bhPushGameData: our continue point moved (the host keeps the guest's items with it) */
int   pc_no_ink_ribbon(void);                 /* typewriters save without an ink ribbon (setting; in co-op the host's rule) */
int   pc_detector_skipped(void);              /* co-op rule "Metal detector: Skipped" (pc/glue/detector_glue.c): event.c bhPlItemCheck in 0/9 finds no metal */
void  pc_detector_scd1(void);                 /* ... event.c bhControlEvent before scd1: the empty security boxes rest */
void  pc_quick_start(void);                   /* co-op rule "Quick start" (pc/glue/coop_glue.c): system.c bhSysCallMonitor after a new game's AllItemInit */
int   pc_biorand_save_differs(const void* save);   /* BioRand (pc/glue/biorand_glue.c): ps2_LoadScreen.c, a save made with another pack asks */
void  pc_biorand_save_accept(void* save);          /* ... loaded anyway */
void  pc_biorand_load_text(void);                  /* ... the question's lines */
int   pc_biorand_box_any(void);                /* ... sub1.c ItemBoxChange: Claire may box the Security Card/File and Crystal in a BioRand game */
int   pc_battle_unlocked(void);               /* host.c: setting "Battle Game unlocked" (event.c bhCk: every Battle Game character) */
int   pc_difficulty_select(void);             /* host.c: setting "Difficulty select" (adv.c CheckButton: NEW GAME opens the difficulty menu) */
void  coop_fx_particle(struct BH_PWORK* ewP, int lnk, const void* offP, const void* dirP, unsigned int col, unsigned int dst, int typ, int mk2);   /* effsub3.c bhSetEffParticle(Mk2) */
/* "You appear as" (pc/glue/actor_glue.c): another model drawn in place of the player / of the
   played character's cutscene double; appear_on: in use without co-op (coop_control_remote runs) */
extern int actor_on, appear_on;
int   actor_put_player(void);                 /* 1 = drawn */
int   actor_put_enemy(struct BH_PWORK* ep);   /* 1 = drawn */
/* enemy variants (pc/glue/variant_glue.c, docs/ENEMIES.md "Enemy variants"): pc_variant_n variant enemies in
   this room (0: none of these is called) */
extern int pc_variant_n;
int   pc_variant_pre(struct BH_PWORK* ep, int slot);    /* eneset.c bhControlEnemy, before ep's step: how many steps (speed) */
void  pc_variant_post(struct BH_PWORK* ep, int slot);   /* after them (also for a co-op puppet) */
int   pc_variant_is(struct BH_PWORK* ep);               /* ep is (part of) an enemy variant (en17.c: Mutated Steve) */
int   pc_variant_draw(struct BH_PWORK* ep);             /* eneset.c bhDrawEnemy, in place of bhPutModel(ep): 1 = drawn */
int   pc_variant_sound(int enemy_no, int se, signed char* bank);   /* sdfunc.c RequestEnemySeBasic: the sound to request */
void  actor_hands_back(void);                 /* game.c, before bhControlPlayer: the source's own head and hands back */
/* PS2 budgets raised on the PC (docs/ENEMIES.md "Budgets on the PC"): the texture heap (the EE copies of the
   TIM2s the GS loads from; 10 MB on the PS2) and its entry table (256). Vanilla rooms use the same
   first entries and bytes as before; only what used to exit(0) now fits. */
#define PC_TEX_HEAP_SIZE (64 << 20)
#define PC_TEX_ENTRIES 1024
/* Enemy slots (ene[], 128 on the PS2; docs/ENEMIES.md "Budgets on the PC"): slots 0..254, so a slot
   still fits the byte-sized fields that keep one (ETTY_WORK.wrk_no, ATR_WORK.prm3, the co-op packets),
   255 being "none". bhSetEnemy takes the first free one, so vanilla rooms use the same slots. Enemy
   sound requests of an effect (zonzon1.c, 127 on the PS2) use index PC_ENE_SLOTS. */
#define PC_ENE_SLOTS 255
/* The room arena (freemem: persistent player data, then the room file and what it allocates;
   12.25 MB block on the PS2, ~11.7 MB of it arena) grows by this much (docs/modding/ROOM_FORMAT.md "Limits and budgets"). The
   buffers above it (njplus, vertex buffers) keep their sizes and move up; allocations from the
   bottom keep their offsets, so vanilla rooms are laid out as before and only the packed room
   file is read higher up. Costs address space only (bss, pages never touched stay free), but
   inside the PS2 address window: the whole image must end below 0x40000000 (ps2/memory.c). */
#define PC_ARENA_EXTRA (64 << 20)
/* The vertex work buffer above it (vwbmemp, njInit3D: 64-byte entries, one per vertex of the node
   being drawn): 5120 entries fit on the PS2 although njInit3D is told 8192; the PC adds the rest. */
#define PC_VERTEX_EXTRA ((8192 - 5120) * 64)
/* Ordering table of translucent/sorted primitives per frame (ps2_dummy.c Ps2AddOT): 8192 entries
   and a 1.75 MB packet buffer on the PS2 (a native-path entry takes 128 bytes of it), unchecked. */
#define PC_OT_ENTRIES 32768
#define PC_PBUFF_SIZE (4608 << 10)
/* ROM_WORK.flg bit that rdxbuild sets in "budget": "pc" rooms (retail rooms use bits 0..1): the
   player (and every actor through bhCheckWallEx) stands on type-6 slope walls, its y read from
   the slope at its x/z (hitchk.c pc_slope_ground); the camera and the co-op partner follow that
   y. Retail rooms never have it: unchanged. docs/modding/ROOM_FORMAT.md "Floors and heights". */
#define PC_ROOM_SLOPES 0x100
struct BH_PWORK;
int   pc_slope_ground(struct BH_PWORK* pw, void* pos, float ah);   /* hitchk.c: 1 = on a slope, pos y + floor set */
void  host_fatal(const char* fmt, ...);
int   pc_prs_decode(const unsigned char* s, uint32_t n, unsigned char* d, uint32_t cap);   /* pc_parse.h: bounded Expand */
int   pc_room_from_disc(const char* name, unsigned char* memp, unsigned char* endp, int err);   /* actor_glue.c */
void  host_log(const char* fmt, ...);
void  pc_et_lp_clear(void);   /* room.c: sys->et_lp rows of model versions >= 16 */
/* F2 enemy spawner (pc/glue/spawn_glue.c): its textures go with the room's / with everything */
void  spawn_release(void);
void  spawn_textures_released(void);
void  spawn_room_setup(void);      /* room.c bhSetRoom: the spawned types' data go with the room */
extern int pc_enemy_added;         /* eneset.c: the room got enemies its file does not have (helper code per type) */
void  pc_enemy_mark_added(void);   /* eneset.c: sets it, the shared helper functions as they are then kept for the room's earlier children */
extern int pc_enemy_refused;       /* eneset.c: the enemy's code runs on a player another enemy holds (its changes are undone) */
void  enemy_room_finished(void);   /* room.c bhFinishRoom: enemy options (pc/glue/enemyopt_glue.c) */

/* title menu MULTIPLAYER entry and the co-op lobby screen (pc/glue/lobby_glue.c) */
void  pc_title_menu_init(void);                  /* Adv_BioCvTitle mode 0: the MULTIPLAYER row (--dev: and BIORAND) */
void  pc_title_plates(int cursor, int n, float fade, int flag);   /* DisplayGameModePlate */
int   pc_newgame_plates(int cursor, float fade, int flag);   /* DisplayNewGamePlate with the BIORAND row (1: drawn) */
void  pc_title_mouse(int level, int flag);       /* CheckButton: hover moves the cursor, a click confirms */
void  pc_title_mouse_active(int on);             /* a title menu or the lobby is up: the OS cursor is shown */
int   pc_lobby(void);                            /* Adv_BioCvTitle mode 30 (or the BIORAND screen, biorand_menu.c): 1 back, 2 new game, 3 load the chosen save */
void  pc_lobby_close(void);                      /* the title ends */
void  pc_lobby_load_save(void);                  /* bhSysCallFirstmovie return code 8 */
void  pc_lobby_textures_released(void);          /* njReleaseTextureAll */
void  pc_title_leave(void);                      /* co-op: the host's game is joined from the title */
void  pc_adv_in_game(void);                      /* co-op: the guest is in a game now (AdvWork.AppMode 1) */

/* ENHANCED SETTINGS in the OPTION menu (pc/glue/options_glue.c): option level 4 */
void  pc_options_frame(void);                    /* Adv_GameOptionScreen, every frame */
void  pc_options_close(void);                    /* Adv_GameOptionScreen ends */
void  pc_options_rows0(void);                    /* DisplayOptionPlateLevel0: ENHANCED SETTINGS, RETURN TO TITLE, EXIT, the question */
int   pc_options_move(int idx, int dir, int vib);/* DisplayOptionPlateLevel0: the cursor's next row (dir 0 up, 1 down, 2 left, 3 right) */
int   pc_options_quit(int port);                 /* DisplayOptionPlateLevel0: RETURN TO TITLE's question (0 not up, 1 up, -1 YES) */
void  pc_options_quit_open(void);                /* DisplayOptionPlateLevel0: RETURN TO TITLE confirmed: ask */
int   pc_options_quit_title(void);               /* bhSysCallOption: YES was chosen (once) */
void  pc_options_mouse0(int port);               /* DisplayOptionPlateLevel0: hover, click */
void  pc_options_mouse_back(int port, int level);/* levels 1-3: right click goes back */
int   pc_options_pc(int port, int flag);         /* DisplayOptionPlate level 4 */

/* native saves (pc/glue/save_glue.c, docs/ARCHITECTURE.md "Save system"): the save and load
   screens list save files instead of memory card slots and skip the card screens */
struct SELECTFILEWINDOW;
struct MEMORYCARDSTATE;
struct SYSLOAD_SCREEN;
struct SYSSAVE_SCREEN;
int   pc_saves_native(int state);                /* the open save/load screen uses native saves; its state 0 takes the setting, -1 asks */
int   pc_save_card(struct MEMORYCARDSTATE* mc);  /* native, instead of ExecuteMemoryCard: its queued read or write done with the save files; 100 */
void  pc_save_list(struct SELECTFILEWINDOW* win, struct MEMORYCARDSTATE* mc, int saving);   /* the window's rows: the saves (+ a new one) */
void  pc_save_move(struct SELECTFILEWINDOW* win, short* cursor, int dir);   /* the cursor up/down through any number of rows */
int   pc_save_header(void);                      /* mcDisplayFileSelectWindow: native, "Load Game" / "Save Game" drawn as its header; 0 memory card */
void  pc_save_mouse(int saving, unsigned int state, int cg, short* cur_x, short* cur_y, short* sel,
                    struct SELECTFILEWINDOW* win, struct MEMORYCARDSTATE* mc);   /* both save systems: the mouse as the screen's cursor and pad bits */
int   pc_sysload_native(struct SYSLOAD_SCREEN* p);   /* the system file: 0 memory card, else the screen's result (1 none, 2 read) */
int   pc_syssave_native(struct SYSSAVE_SCREEN* p);   /* 0 memory card, else the screen's result (1 failed, 2 written) */

/* mouse in the inventory screens (pc/glue/menu_glue.c) */
unsigned int menu_pad(unsigned int pad);
/* the co-op trade screen (pc/glue/trade_glue.c, docs/COOP.md "Trade screen") */
unsigned int trade_pad(unsigned int pad);   /* pad.c bhSetPad, after menu_pad: the screen's input, the player stands still */
void  trade_draw(void);                     /* game.c bhMainSequence, after the frame's drawing */
int   pc_trade_screen(void);                /* the screen is open (menu_pad leaves the mouse to it) */
void  trade_textures_released(void);        /* njReleaseTextureAll */
/* quick knife (pc/glue/knife_glue.c) */
unsigned int knife_pad(unsigned int pad);
void knife_game_exit(void);   /* cheat_game_exit (bhExitGame): the knife's state of that game goes */
void tps_game_exit(void);     /* cheat_game_exit (bhExitGame): the third-person camera of that game goes (tps_glue.c) */
/* modern controls for the fixed cameras (pc/glue/modern_glue.c): nothing while they are off */
unsigned int modern_pad(unsigned int pad);   /* pad.c bhSetPad, before knife_pad */
void  modern_player(void);                    /* player.c bhControlPlayer, after the mode handler */
/* reload button (pc/glue/reload_glue.c) */
void  reload_player(void);                    /* player.c bhControlPlayer, after modern_player */
struct STCAM_WRK;
void  menu_examine(struct STCAM_WRK* sc);

/* third-person mode (pc/glue/tps_glue.c, docs/THIRDPERSON.md): hooks only called while tps_on */
extern int tps_on;
extern int tps_cam_active;   /* the third-person camera is the current view */
void  tps_camera(void);
void  tps_after_draw(void);
void  tps_after_cut(void);
void  tps_atmos(void);
struct O_WRK;
int   tps_filter(struct O_WRK* op);
int   tps_own_view(void);   /* our camera is this frame's view (before tps_camera too) */
unsigned int tps_pad(unsigned int pad);
int   pc_input_active(void);   /* any PC-side input this frame (bhSysCallScreenSaver) */
void  tps_player_mode(void);   /* before the player's mode handler: the straight aim height back (free aim) */
void  tps_player(void);
void  tps_calc_model(void);
struct BH_PWORK;
void  tps_bend(struct BH_PWORK* p, float pitch);   /* the free-aim bend by pitch (radians) on a display copy (actor_glue.c), the partner */
float tps_aim_pitch(void);   /* the pitch of our free-aim bend this tick (radians, 0: none) */
#include "ps2sdk_shim.h"
#include "ps2_hw.h"
#define u_long ps2_u_long
#ifdef __SSE2__
#include <emmintrin.h>   /* for vu_simd.h (vu0.h), which game files include after the define below */
#endif

/* EE data model: long is 64-bit. Must come after every host header. */
#define long long long
