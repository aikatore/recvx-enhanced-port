/* Third-person mode (docs/THIRDPERSON.md): settings shared between the overlay/host (main
   thread), the input layer and the game thread (pc/glue/tps_glue.c). */
#ifndef RECVX_TPS_H
#define RECVX_TPS_H
#ifdef __cplusplus
extern "C" {
#endif

typedef struct TpsConfig {
    volatile int on;          /* user toggle (ini "tps", --third-person, toggle key) */
    int fov, aim_fov;         /* degrees, horizontal over the 4:3 picture (hor+ widens it) */
    int dist, aim_dist;       /* camera distance behind the shoulder, tenths of game units */
    int height;               /* pivot height above the player's feet, tenths */
    int shoulder;             /* sideways offset, tenths */
    int left;                 /* 1 = over the left shoulder */
    int sens_x, sens_y;       /* mouse sensitivity, percent */
    int aim_sens;             /* sensitivity while aiming, percent of the above */
    int invert_y, crosshair;
    int auto_aim;             /* 1 = keep the game's lock-on when raising the weapon */
    int precise;              /* "Precise hits": free-aim shots hit only the enemies' bodies (tps_glue.c pc_precise_hits) */
    int aim_walk;             /* "Move While Aiming": walk slowly while aiming or reloading, the legs walking under the aim (tps_glue.c) */
    int hit_react;            /* "Locational Hit Reactions": a zombie hit on the head, an arm or a leg flinches there (tps_glue.c, drawn only) */
    int headshots;            /* "Headshots & Piercing": more head damage from the rifles, killing head hits burst a zombie's head,
                                 the sniper rifle pierces (tps_glue.c; co-op: the host's, pc_headshots) */
    int atmos;                /* light and fog: 1 = smooth (fades between the cuts' settings), 0 = per cut */
    int free_cam;             /* mouse/keyboard camera: 0 = B (behind the character, the mouse turns it),
                                 1 = A (free: the mouse orbits the camera, WASD walk relative to it) */
    int key;                  /* SDL scancode of the toggle key */
    /* gamepad: 0 classic (tank controls through the DS2 rows, camera behind), 1 modern (twin-stick:
       left stick camera-relative, right stick camera / aim, its own bindings) */
    int pad_modern;
    int pad_sens_x, pad_sens_y;   /* right stick camera speed, percent (100: 180 / 120 degrees a second) */
    int pad_aim_sens;             /* right stick while aiming, percent of the above */
    int pad_invert_y;
    int pad_dead_move, pad_dead_look;   /* dead zones of the left / right stick, percent */
    int hud;                  /* "HUD": 0 Auto (with this camera only), 1 On (also the fixed cameras), 2 Off (hud_glue.c) */
    int hud_side;             /* "HUD Position": 0 the shoulder's side (fixed cameras: right), 1 left, 2 right */
    int hud_style;            /* "HUD Style": 0 Circle (a disc, a health arc), 1 Minimal (a health bar, the counts) */
    /* published by the game thread */
    volatile int cam_active;  /* the third-person camera drew the last frame */
    volatile int aiming;      /* weapon raised (crosshair) */
    volatile int pad_play;    /* modern gamepad scheme in third-person play: the gamepad's third-person
                                 rows play instead of its game rows (bindings.c) */
    volatile int kb_play;     /* third person in a game, no OPTION screen up: the keyboard's third-person keys
                                 take theirs over from the game rows (bindings.c); the title menus and OPTION
                                 read the DS2 buttons, so there Q/W/A/S are L1/R1/Square/Triangle again */
} TpsConfig;

extern TpsConfig g_tps;

/* The HUD (pc/glue/hud_glue.c, docs/THIRDPERSON.md "HUD"): what it shows, published by the game thread each frame,
   drawn by the overlay (overlay.cpp hud_draw). A frame's fields may mix with the next one's: harmless. */
enum { HUD_FINE, HUD_CAUTION, HUD_CAUTION2, HUD_DANGER, HUD_POISON };   /* the status screen's conditions (sub1.c) */
enum { HUD_NONE = -1, HUD_INF = -2 };                                   /* rounds / spare: not shown, unlimited */
typedef struct TpsHud {
    volatile int on;          /* in play, shown by the setting */
    volatile int left;        /* on the left */
    volatile float fade;      /* the screen fade over it, 0..1 */
    volatile int hp, hp_max, cond;
    volatile int icon_x, icon_y, icon_w, icon_h;   /* its icon cell in the icon picture (hud_icons), pixels; icon_w 0: none */
    volatile int rounds, rounds_pct;       /* loaded: a count, HUD_NONE / HUD_INF; pct: in percent (the inventory's) */
    volatile int spare, spare_pct;         /* the reload ammo in the bag, the same */
} TpsHud;
extern TpsHud g_hud;
/* the item icons as one RGBA picture (both textures, one above the other) once the game thread has read them, else
   NULL; the overlay makes its texture from it once (render thread) */
const unsigned int* hud_icons(int* w, int* h);
void hud_frame(void);                         /* game thread, frame hand-off */

/* "Move While Aiming": legs walking under the weapon motion (tps_glue.c tps_legs): the walk motion of the base set,
   its frame (16.16), the hips' turn off the facing (game units), the weight blended in (0: none) */
typedef struct TpsLegs { int mtn, frm, yaw; float w; int on; } TpsLegs;   /* (on: walking this tick, w eases towards it) */
extern TpsLegs tps_legs_me, tps_legs_p2;

/* input (pc/src/input/tps_input.c), called on the game thread */
enum { TPS_K_FWD = 1, TPS_K_BACK = 2, TPS_K_LEFT = 4, TPS_K_RIGHT = 8, TPS_K_RUN = 16, TPS_K_ACTION = 32,
       TPS_K_STATUS = 64, TPS_K_MAP = 128, TPS_K_AIM = 256, TPS_K_FIRE = 512, TPS_K_CANCEL = 1024,
       TPS_K_QUICKTURN = 2048 /* with BACK (and RUN): the quick-turn row */, TPS_K_RECENTER = 4096,
       TPS_K_OPTIONS = 8192 };
unsigned tps_input_keys(int quickturn);       /* quickturn: also hold the quick turn (gamepad gesture) */
void tps_take_mouse(float* dx, float* dy);   /* pixels moved since the last call */
int tps_pad_sticks(float* lx, float* ly, float* rx, float* ry);   /* the gamepad's sticks, -1..1, y up; 0 = none */
void tps_pad_dpad(float* x, float* y);       /* the gamepad's d-pad: -1, 0 or 1 each, y up */
int tps_pad_touched(void);                   /* any gamepad button or stick in use */
int tps_kbm_touched(void);                   /* any key or mouse button down (the mouse motion: tps_take_mouse) */
/* main thread */
void tps_input_event(const void* sdl_event);
void tps_update_mouse_mode(void* window, int overlay_open);
int  tps_mouse_captured(void);                /* relative mode as tps_update_mouse_mode last set it */
void tps_frame(void);                         /* game thread, frame hand-off */

#ifdef __cplusplus
}
#endif
#endif
