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

/* "Move While Aiming": legs walking under the weapon motion (tps_glue.c tps_legs): the walk motion of the base set,
   its frame (16.16), the hips' turn off the facing (game units), the weight blended in (0: none) */
typedef struct TpsLegs { int mtn, frm, yaw; float w; } TpsLegs;
extern TpsLegs tps_legs_me;

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
