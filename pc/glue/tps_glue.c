/* Third-person mode (docs/THIRDPERSON.md): an over-the-shoulder camera that replaces the
   room's camera cuts during normal play. The cut system keeps running underneath (doors,
   events, lights and fog per cut); only the view written into `cam` before
   bhControlCamera() is ours. All hooks run on the game thread and only while tps_on. */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_NaView.h"
#include "../../decomp/include/ps2/veronica/prog/pwksub.h"
#include "../../decomp/include/ps2/veronica/prog/player.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_NaMatrix.h"
#include "../../decomp/include/ps2/veronica/prog/light.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_NinjaCnk.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_NaFog.h"
#include "../../decomp/include/ps2/veronica/prog/playpch.h"
#include "../src/host/tps.h"
#include "../src/host/modern.h"
#include "../src/host/bindings.h"
#include "../src/host/menu_mouse.h"
#include "pc_rules.h"

TpsConfig g_tps = { .fov = 75, .aim_fov = 65, .dist = 140, .aim_dist = 100, .height = 165, .shoulder = 35,
                    .sens_x = 100, .sens_y = 100, .aim_sens = 70, .crosshair = 1, .precise = 1, .atmos = 1, .key = 62 /* F5 */,
                    .pad_modern = 1, .pad_sens_x = 100, .pad_sens_y = 100, .pad_aim_sens = 50, .pad_dead_move = 20, .pad_dead_look = 15 };
int tps_on;            /* hooks active (changes only at the frame hand-off) */
int tps_cam_active;    /* our camera is the view of the current frame */

extern float fNaViwClipNear, fNaViwClipFar;
void bhCalcModel(BH_PWORK* ewP);   /* MdlPut.h */
void actor_aim_offset(const BH_PWORK* p, float* out);   /* actor_glue.c */
int actor_aim_hand(const BH_PWORK* p, float* hand);
float actor_cam_scale(void);
uint64_t gs_frame_count(void); /* the presented frame (RECVX_INPUT, frame dumps) */
int reload_at_rest(void);      /* reload_glue.c */
int pc_in_play(const BH_PWORK* p, int scope);
int pc_in_game(void);           /* options_glue.c */
int pc_trade_screen(void);      /* trade_glue.c */
int pc_options_up(void);        /* options_glue.c */

static float yaw, pitch;          /* camera, game angle units (65536 = 360 deg); pitch > 0 looks up */
static float mouse_dx, mouse_dy;  /* this frame's mouse motion, pixels */
static int mouse_moved;           /* the mouse moved this frame (pc_input_active) */
static float clip_near, clip_far; /* the game's clip range while we draw with ours */
static int trace;                 /* RECVX_TPS_TRACE=1: log the free aim */
static NJS_POINT3 aim_t;          /* free aim: the crosshair target (tps_calc_model) */
static int aim_e;                 /* free aim: the enemy under the crosshair, -1: a wall or nothing */
static int aim_h;                 /* free aim: the aim height bit given to the game (at_flg 4 up, 8 down), 0: none */
static float bend_a;   /* the last free-aim bend (radians per node), 0: none */
static float aim_corr;            /* free aim with a model: the drawn weapon's pitch minus the hand's (radians), eased */
static int no_cam_walls;          /* test aid (RECVX_TPS_ORBIT): the eye goes through walls */
static unsigned move_keys;        /* camera A (g_tps.free_cam): the WASD keys held, walked relative to the camera */
static int pad_steer;             /* the game rows (arrows, gamepad) press a direction: tank controls, the camera follows */
/* gamepad, modern scheme (docs/THIRDPERSON.md "Gamepad") */
static int pad_cam;               /* the gamepad was used last: free camera on the right stick, the left stick walks */
static float stick_yaw, stick_pitch;   /* this frame's right stick as camera turn, game units (consumed like the mouse) */
static int stick_moved;           /* the right stick moved this frame (pc_input_active) */
static int pad_move;              /* the left stick walks this frame: tps_player turns towards pad_target */
static float pad_target;
static int recenter;              /* game frames left of the camera easing behind the character */

/* a camera in `cam`: position, angles, view vector, eye */
typedef struct { float px, py, pz, vx, vy, vz, wpx, wpy, wpz; int ax, ay, az; float ln; int pers; } View;
static View view_now;   /* ours as bhControlCamera derived it */
static View cut_cam;    /* the cut's own camera, running underneath ours (cut_cam_ok) */
static int cut_cam_ok;

static void view_get(View* v)
{
    v->px = cam.px; v->py = cam.py; v->pz = cam.pz;
    v->vx = cam.vx; v->vy = cam.vy; v->vz = cam.vz;
    v->wpx = cam.wpx; v->wpy = cam.wpy; v->wpz = cam.wpz;
    v->ax = cam.ax; v->ay = cam.ay; v->az = cam.az;
    v->ln = cam.ln; v->pers = cam.pers;
}

static void view_put(const View* v)
{
    cam.px = v->px; cam.py = v->py; cam.pz = v->pz;
    cam.vx = v->vx; cam.vy = v->vy; cam.vz = v->vz;
    cam.wpx = v->wpx; cam.wpy = v->wpy; cam.wpz = v->wpz;
    cam.ax = v->ax; cam.ay = v->ay; cam.az = v->az;
    cam.ln = v->ln; cam.pers = v->pers;
}

#define DEG(a) ((a) * (65536.0f / 360.0f))
#define TURN_PER_PIXEL DEG(0.12f)   /* mouse at 100% sensitivity */
#define RAD(a) ((a) * (3.14159265f / 32768.0f))
#define STICK_YAW DEG(6.0f)         /* right stick fully pushed at 100%: 180 degrees a second */
#define STICK_PITCH DEG(4.0f)       /* 120 degrees a second */
#define RUN_PUSH 0.7f               /* the left stick pushed this far runs (as modern_glue.c) */

/* the game's lock-on instead of free aim ("Game auto-aim"); with the gamepad it is the gentle aim assist */
static int lock_aim(void) { return g_tps.auto_aim && !pad_cam; }

/* the sniper rifle's or linear launcher's scope is up (bhCPM2_act_scp sets st_flg 0x800000 until the cut is
   set up again after it; also its reload, mode2 70, in between): our camera is off, the gamepad keeps its
   third-person rows (pad_play) */
static int in_scope(void) { return sys && plp && plp->mode0 == 1 && plp->mode1 == 1 && (plp->mode2 == 71 || (sys->st_flg & 0x800000)); }

static float wrap(float a)
{
    a = fmodf(a, 65536.0f);
    return a > 32768.0f ? a - 65536.0f : a < -32768.0f ? a + 65536.0f : a;
}

/* normal play: no event or fixed camera, first person, scope, door transition or event.
   Not sp_flg 0x40 (the cut camera may move): messages, examine texts, pick-up prompts, the
   fade to/from the inventory, map and options clear it, which only freezes the cut camera
   (bhCheckCut does nothing) - the view stays ours, so we stay active and hold still (frozen);
   dropping out there showed our view with the cut's hidden room parts and clip range. */
static int camera_allowed(void)
{
    return sys && plp && plp->mlwP && !(sys->st_flg & 0x800001) &&
           !(sys->gm_flg & 0x49) && !(sys->cb_flg & 0x5) && plp->mode0 != 7;
}

static void atmos_off(void);
static int nearest_enemy(float* oy, float* op, int* walled);

/* player.c bhControlPlayer, before the mode handler (and when third person stops): the weapon
   modes pick motions by the aim height, so they keep the straight one free aim plays */
void tps_player_mode(void)
{
    if (aim_h && plp && (plp->at_flg & 0xE) == aim_h) plp->at_flg = (plp->at_flg & ~0xE) | 2;
    aim_h = 0;
}

static int dual_e;   /* (dual weapons, below) */
extern int enemy_room_gen;   /* enemyopt_glue.c: room loads so far */
static void deactivate(void)
{
    dual_e = -1;
    atmos_off();
    tps_player_mode();
    if (!tps_cam_active) return;
    tps_cam_active = g_tps.cam_active = g_tps.aiming = cut_cam_ok = 0;
    g_tps.pad_play = g_tps.pad_play && in_scope();   /* (a held aim trigger stays held into the scope) */
    /* the room camera resumes: the game's own re-initialisation of the current cut */
    if (sys && !(sys->st_flg & 0x1)) sys->gm_flg |= 0x800;
}

/* system.c bhExitGame (cheat_game_exit): the game ends (the title, a game over, a load). tps_camera, which
   switches our camera off, only runs in a game, so the ended game's camera stayed "active": the gamepad's
   third-person rows kept the buttons they use (Cross, d-pad down, the shoulders, ...) in the title menu, and
   the next game started with the old camera instead of behind the character */
void tps_game_exit(void)
{
    dual_e = -1;
    aim_h = 0;
    recenter = 0;
    tps_cam_active = g_tps.cam_active = g_tps.aiming = g_tps.pad_play = cut_cam_ok = 0;
}

/* the weapon raised; not the reload of a lowered weapon (reload_glue.c), which keeps the walking camera */
static int aiming(void) { return tps_cam_active && plp->mode0 == 1 && plp->mode1 == 1 && !reload_at_rest(); }

static float cur_dist, cur_fov;   /* eased towards the walk/aim values */

/* The point the camera follows: the player's position, or the drawn body during the game's own
   moves (mode2 14..22: stairs, climbing on or off a ledge, pushing). Stairs and ledges leave
   plp->p where the move started and add the motion's travel at once: the stairs every two steps
   (12 units on, 8 up or down), a ledge climb at its end; the model's root (owP[0]) carries it
   smoothly. The camera follows the root less its offset from the followed point when the move
   started, the height eased (the root bobs with each step and dips as the character crouches),
   and afterwards eases back onto plp->p. */
static NJS_POINT3 fol;     /* followed point (view) */
static float fol_base[3];  /* root minus followed point when the move started */
static int fol_move;       /* 1: in a move, 2: easing back after it, 0: on plp->p */
#define FOL_EASE 0.35f

static void follow(void)
{
    const float* r = plp->mlwP->owP->mtx;   /* the root as drawn this tick */
    float* f = &fol.x;
    const float* p = &plp->px;
    int j;
    if (plp->mode0 == 1 && plp->mode1 == 0 && plp->mode2 > 13) {
        if (fol_move != 1)
            for (j = 0; j < 3; j++) fol_base[j] = r[12 + j] - f[j];
        fol_move = 1;
        fol.x = r[12] - fol_base[0];
        fol.z = r[14] - fol_base[2];
        fol.y += (r[13] - fol_base[1] - fol.y) * FOL_EASE;
        return;
    }
    if (fol_move) {
        float d = 0;
        for (j = 0; j < 3; j++) { f[j] += (p[j] - f[j]) * FOL_EASE; d += fabsf(p[j] - f[j]); }
        fol_move = d > 0.01f ? 2 : 0;
    }
    if (!fol_move) fol = *(const NJS_POINT3*)p;
}

/* b moved towards a so that it stays 1.5 units in front of the first wall between them (the
   game's bullet ray test; its hit globals are preserved) */
static void clear_of_walls(const NJS_POINT3* a, NJS_POINT3* b)
{
    NJS_LINE line;
    NJS_POINT3 apos = sys->apos;
    ATR_WORK* ahtp = sys->ahtp;
    float total, len;
    line.px = a->x; line.py = a->y; line.pz = a->z;
    line.vx = b->x - a->x; line.vy = b->y - a->y; line.vz = b->z - a->z;
    total = sqrtf(line.vx * line.vx + line.vy * line.vy + line.vz * line.vz);
    if (total > 0.01f && bhCheckL2Wall(&line, 0x400, &len)) {
        float k = (len - 1.5f) / total;
        if (k < 0) k = 0;
        b->x = a->x + line.vx * k; b->y = a->y + line.vy * k; b->z = a->z + line.vz * k;
    }
    sys->apos = apos; sys->ahtp = ahtp;
}

/* camera for the current yaw/pitch: dir = viewing direction, sh = the shoulder point (the aim
   ray starts there), eye = behind it on the ray through it. Walls pull a camera shoulder point
   in sideways (only if the shoulder itself is in a wall) and slide the eye forward along the
   ray; `sh` stays the wall-free one, so camera collision never moves the aim or the facing.
   Depends on the player's position only (the followed point: follow). */
static void view(NJS_POINT3* eye, NJS_POINT3* dir, NJS_POINT3* sh)
{
    float s = sinf(RAD(yaw)), c = cosf(RAD(yaw)), sp = sinf(RAD(pitch)), cp = cosf(RAD(pitch));
    float k = actor_cam_scale();   /* "You appear as" a taller or shorter model: its height (1: the character's) */
    float side = (g_tps.left ? -g_tps.shoulder : g_tps.shoulder) / 10.0f * k;
    const NJS_POINT3* o = fol_move ? &fol : (const NJS_POINT3*)&plp->px;
    NJS_POINT3 at, csh;

    /* forward (-sin, 0, -cos), right (cos, 0, -sin) */
    dir->x = -s * cp; dir->y = sp; dir->z = -c * cp;
    at.x = o->x; at.y = o->y + g_tps.height / 10.0f * k; at.z = o->z;
    sh->x = at.x + c * side; sh->y = at.y; sh->z = at.z - s * side;
    csh = *sh;
    clear_of_walls(&at, &csh);
    eye->x = csh.x - dir->x * cur_dist; eye->y = csh.y - dir->y * cur_dist; eye->z = csh.z - dir->z * cur_dist;
    if (!no_cam_walls) clear_of_walls(&csh, eye);
}

static void look(float sens, float stick_sens)
{
    pitch += (g_tps.invert_y ? mouse_dy : -mouse_dy) * TURN_PER_PIXEL * g_tps.sens_y / 100.0f * sens + stick_pitch * stick_sens;
    mouse_dy = stick_pitch = 0;
    if (pitch > DEG(60.0f)) pitch = DEG(60.0f);
    if (pitch < DEG(-60.0f)) pitch = DEG(-60.0f);
}

/* game.c bhMainSequence, before bhControlCamera() */
void tps_camera(void)
{
    NJS_POINT3 eye, dir, sh;
    int aim;

    if (!camera_allowed()) { deactivate(); return; }
    if (!tps_cam_active) { view_get(&cut_cam); cut_cam_ok = 1; yaw = plp->ay; pitch = DEG(-8.0f); cur_dist = g_tps.dist / 10.0f; cur_fov = (float)g_tps.fov; fol_move = 0; fol = *(NJS_POINT3*)&plp->px; }
    follow();
    tps_cam_active = g_tps.cam_active = 1;

    aim = aiming() && !lock_aim();
    g_tps.aiming = aiming();
    if (!(sys->sp_flg & 0x40)) mouse_dx = mouse_dy = stick_yaw = stick_pitch = 0;   /* frozen (above): no mouse look */
    if (recenter > 0 && !aiming()) {   /* gamepad: the camera eases behind the character */
        recenter--;
        yaw = wrap(yaw + wrap(plp->ay - yaw) * 0.35f);
        pitch += (DEG(-8.0f) - pitch) * 0.35f;
    }
    if ((g_tps.free_cam || pad_cam) && !aiming()) {   /* camera A, the gamepad: mouse / right stick orbit the camera (aiming: tps_player) */
        yaw = wrap(yaw - mouse_dx * TURN_PER_PIXEL * g_tps.sens_x / 100.0f + stick_yaw);
        mouse_dx = stick_yaw = 0;
        if (pad_steer) yaw = wrap(yaw + wrap(plp->ay - yaw) * 0.4f);
    } else if (!aim) yaw = wrap(yaw + wrap(plp->ay - yaw) * 0.4f);   /* free aim: the mouse steers the camera */
    look(1.0f, 1.0f);
    cur_dist += ((aiming() ? g_tps.aim_dist : g_tps.dist) / 10.0f * actor_cam_scale() - cur_dist) * 0.35f;
    cur_fov += ((float)(aiming() ? g_tps.aim_fov : g_tps.fov) - cur_fov) * 0.35f;

    {   /* test aid RECVX_TPS_ORBIT=degrees: the view only turned about the player (side views for
           contact sheets); the aim keeps the unturned view */
        static float orbit = -1;
        float y0 = yaw;
        if (orbit < 0) no_cam_walls = (orbit = getenv("RECVX_TPS_ORBIT") ? DEG(atof(getenv("RECVX_TPS_ORBIT"))) : 0) != 0;
        yaw += orbit;
        view(&eye, &dir, &sh);
        cam.ay = (short)(int)-yaw;
        yaw = y0;
    }
    cam.px = eye.x; cam.py = eye.y; cam.pz = eye.z;
    cam.ln = 0;
    cam.ax = (short)(int)-pitch;
    cam.az = 0;
    cam.pers = (int)DEG(cur_fov);
    if (trace)
        printf("tps: cam %c frame %u yaw %.1f pitch %.1f | ay %.1f at (%.1f %.1f) mode %d/%d/%d keys %x | p %.2f %.2f %.2f root %.2f %.2f %.2f follow %d %.2f %.2f %.2f eye %.2f %.2f %.2f\n",
               pad_cam ? 'P' : g_tps.free_cam ? 'A' : 'B', (unsigned)gs_frame_count(), yaw * (360.0f / 65536.0f), pitch * (360.0f / 65536.0f),
               (short)plp->ay * (360.0f / 65536.0f), plp->px, plp->pz, plp->mode0, plp->mode1, plp->mode2, move_keys, plp->px, plp->py, plp->pz,
               plp->mlwP->owP->mtx[12], plp->mlwP->owP->mtx[13], plp->mlwP->owP->mtx[14], fol_move, fol.x, fol.y, fol.z, eye.x, eye.y, eye.z);

    /* the room's near plane can be far out (up to 22 units); the camera is close */
    clip_near = fNaViwClipNear; clip_far = fNaViwClipFar;
    if (clip_near != -1.0f || clip_far != -20000.0f) njClipZ(-1.0f, -20000.0f);
}

/* view depth of (x, y, z) (the fog's view z) for a camera, its view matrix as bhControlCamera
   builds it: T(0,0,-ln) Rz Rx Ry T(-(p + of)) */
static float view_depth(const View* v, float x, float y, float z)
{
    NJS_MATRIX m;
    NJS_POINT3 p = { x, y, z }, q;
    njUnitMatrix(&m);
    njTranslate(&m, 0, 0, -v->ln);
    njRotateZ(&m, v->az); njRotateX(&m, v->ax); njRotateY(&m, v->ay);
    njTranslate(&m, -(v->px + cam.ofx), -(v->py + cam.ofy), -(v->pz + cam.ofz));
    njCalcPoint(&m, &p, &q);
    return -q.z;
}

/* game.c bhMainSequence, after the frame is drawn */
void tps_after_draw(void)
{
    if (!tps_cam_active) return;
    if (clip_near != fNaViwClipNear || clip_far != fNaViwClipFar) njClipZ(clip_near, clip_far);
    view_get(&view_now);
    if (cut_cam_ok) view_put(&cut_cam);   /* bhCheckCut moves the cut camera on from its own state */
}

/* game.c bhMainSequence, after bhCheckCut: the cut system (still running underneath) moves
   its own camera in `cam` - position and angles of a tracking cut every frame, the view
   vector on a cut change (bhSetCut), the eye for cuts with distance fog
   (bhControlActiveCamera). It runs on from its own state (tps_after_draw), as with the fixed
   cameras; kept in cut_cam, it is where the fixed camera would be (fog, "Light and fog").
   The next frame's game logic runs before tps_camera and reads `cam`: effects offset towards
   the camera (gore sprites, explosions, splashes: cam.v*, cam.wp*), 3D sound (cam.ay). Put
   ours back while our camera stays (not when this frame switched to an event or fixed camera). */
void tps_after_cut(void)
{
    if (!tps_cam_active || !camera_allowed()) return;
    view_get(&cut_cam);
    {   /* its view vector and eye for the new position and angles, as bhControlCamera */
        NJS_MATRIX m;
        NJS_VECTOR f = { 0, 0, -1.0f };
        NJS_POINT3 e = { 0, 0, -cut_cam.ln };
        njUnitMatrix(&m);
        njTranslate(&m, cut_cam.px + cam.ofx, cut_cam.py + cam.ofy, cut_cam.pz + cam.ofz);
        njRotateY(&m, cut_cam.ay); njRotateX(&m, cut_cam.ax); njRotateZ(&m, cut_cam.az);
        njCalcPoint(&m, &e, (NJS_POINT3*)&cut_cam.wpx);
        njCalcVector(&m, &f, (NJS_VECTOR*)&cut_cam.vx);
        cut_cam.vx = -cut_cam.vx; cut_cam.vy = -cut_cam.vy;
    }
    cut_cam_ok = 1;
    sys->gm_flg &= ~0x20;   /* "the view cut" (bhSetCut): ours did not; it wipes blood off the lens (effect 301) */
    view_put(&view_now);
}

/* ---- Light and fog (docs/THIRDPERSON.md "Light and fog") ----
   Each cut carries its own fog (on/off, colour, range, optionally shifted by the camera's
   distance from the cut camera, flg 0x40/0x80), light set (hidlgt: which of lights 4.. are on)
   and screen filter (fil_no/fil_rt). With the fixed cameras a change hides in the cut; with
   ours it pops while walking. "Smooth" (g_tps.atmos) keeps the game's per-cut values as the
   target and fades to them: changes in a frame where the cut changed fade, others (scripts,
   muzzle flash, lightning, a linked actor dying) apply at once, as in the game. */
#define FADE (1.0f / 24.0f)   /* light/filter weights and fog density: 0 -> 1 in 24 game frames (0.8 s) */
#define EASE 0.08f             /* fog range and colour, per game frame (95% in 36 frames) */
#define MAXL 64                /* lights per room: 44 at most [data] */
extern float fNaFogTbl[128];   /* ps2_NaFog.c */

static int atmos_reset = 1, atmos_trace = -1, last_cut = -1;
static float lw[MAXL];                      /* weight of each room light */
static unsigned char lon[MAXL];             /* on (flg & 3 == 3) in the previous frame */
static struct { float a, n, f, dn, df, c[3]; int on; unsigned col; } fog;   /* shown: density, range (view depth), easing left, colour; on, col: last target */
static float filw[4], fila[4];              /* screen filters (effects 90..92): weight, alpha */

static float step(float w, int on) { w += on ? FADE : -FADE; return w < 0 ? 0 : w > 1 ? 1 : w; }

/* RECVX_ATMOS_TRACE=1: one line per game frame - the cut, the game's fog (on, colour, range as
   set, the cut's own range, camera-relative bits), the filter, the cut's under-water
   distortion (cam.flg 0x20) and whether it is drawn (gm_flg 0x100), the lights on, the light slots
   as drawn, the player's view depth in the view drawn and from the cut camera underneath ours;
   with our fog also the fog shown (density, colour, range in view depth), with "smooth" the filters */
static void atmos_log(int smooth_on)
{
    char buf[640];
    int len = 0;
    CAM_WRK* ci = &rom->cutp[cam.ncut].cam[cam.camver];
    View now;
    for (int i = 4; i < rom->lgt_n && i < MAXL && len < 200; i++)
        if ((rom->lgtp[i].flg & 3) == 3) len += snprintf(buf + len, sizeof buf - len, " %d", i);
    len += snprintf(buf + len, sizeof buf - len, " | dir %.2f %.2f %.2f pts", (sys->st_flg & 0x10000) ? sys->lg_r : 0,
                    (sys->st_flg & 0x10000) ? sys->lg_g : 0, (sys->st_flg & 0x10000) ? sys->lg_b : 0);
    for (int i = 0; i < sys->lg_pnt && i < 3; i++)
        len += snprintf(buf + len, sizeof buf - len, " (%.0f %.0f %.0f: %.2f %.2f %.2f)", sys->lg_ptb[i].ps.x, sys->lg_ptb[i].ps.y,
                        sys->lg_ptb[i].ps.z, sys->lg_ptb[i].cr, sys->lg_ptb[i].cg, sys->lg_ptb[i].cb);
    view_get(&now);
    printf("atmos: frame %u room %d/%d cut %d.%d pos %.1f %.1f tps %d smooth %d | game fog %d %06x %.1f %.1f cut %.1f %.1f rel %x"
           " | fil %d %d %02x | eff110 %d drawn %d | lights%s | depth %.1f cut camera %.1f",
           (unsigned)gs_frame_count(), sys->stg_no, sys->rom_no, cam.ncut, cam.camver, plp->px, plp->pz, tps_cam_active, smooth_on,
           (ci->flg & 8) != 0, cam.fog_col & 0xFFFFFF, cam.fog_nr, cam.fog_fr, ci->fog_nr, ci->fog_fr, ci->flg & 0xC0,
           (cam.flg & 8) != 0, sys->fil_no, sys->fil_rt & 0xFF, (cam.flg & 0x20) != 0, (sys->gm_flg & 0x100) != 0, buf,
           view_depth(&now, plp->px, plp->py + 10.0f, plp->pz), view_depth(cut_cam_ok ? &cut_cam : &now, plp->px, plp->py + 10.0f, plp->pz));
    if (last_cut >= 0)
        printf(" | shown fog %.2f %02x%02x%02x %.1f %.1f", fog.a, (int)(fog.c[0] + 0.5f), (int)(fog.c[1] + 0.5f), (int)(fog.c[2] + 0.5f), fog.n, fog.f);
    if (smooth_on) printf(" fil %.2f %.2f %.2f", filw[0], filw[1], filw[2]);
    printf("\n");
}

/* fog amount 0..1 at 50 units, a typical distance in view, for fog linear in view depth from n to f */
static float fog_amount(float n, float f)
{
    float a = (50.0f - n) / fmaxf(f - n, 1.0f);
    return a < 0 ? 0 : a > 1 ? 1 : a;
}

/* The fog as the fixed camera shows it. Fog grows with view depth, and the cut's fixed camera
   stands far off (30 to 115 units from the player in the rooms measured), ours close behind the player
   (about 14): the same fog left the player and the scenery around them far clearer. The range
   is moved towards our camera by the player's depth from the cut camera (cut_cam, running
   underneath) less the depth from ours, so the player and what is around them get the fog the
   fixed camera shows, what is farther more. Divided by the Fog intensity scale, which
   stretches all depths (njCalcFogPower): with the option the player's fog matches too.
   In view depth: n near, f far (the cut's fog_nr/fog_fr negated; njGenerateFogTable3 starts
   the fog at the camera when the near value is not in front of it). */
static void fog_range(float nr, float fr, float* n, float* f)
{
    float x = plp->px, y = plp->py + 10.0f, z = plp->pz, d;   /* about the body's middle */
    View ours;
    view_get(&ours);   /* this frame's (bhControlCamera) */
    d = (fmaxf(view_depth(&cut_cam, x, y, z), 0) - view_depth(&ours, x, y, z)) / pc_fog_scale();
    *n = (fr / 256.0f > nr ? -nr : 0) - d;
    *f = -fr - d;
    if (*f < 1) *f = 1;
    if (*n > *f - 1) *n = *f - 1;
}

/* the fog table for fog linear in view depth from n to f (n < 0: fog already at the camera),
   weighted by a; njGenerateFogTable3 sets the density for f. Entry i is the depth f / m,
   m = 2^(i/16) * (16 + i%16) / 16 (njCalcFogPower: index from density / depth) */
static void fog_table(float n, float f, float a)
{
    njGenerateFogTable3(rom->fog, -n, -f);
    for (int i = 0; i < 128; i++) {
        float m = (float)(1 << (i >> 4)) * (16 + (i & 15)) / 16.0f, k = (f / m - n) / (f - n);
        fNaFogTbl[i] = 255.0f - a * 255.0f * (k < 0 ? 0 : k > 1 ? 1 : k);   /* 255 = no fog */
    }
}

static void atmos_fog(int cut_changed)
{
    CAM_WRK* ci = &rom->cutp[cam.ncut].cam[cam.camver];
    int on = (ci->flg & 0x8) && !(sys->gm_flg & 0x200000), snap;
    unsigned col = ((sys->st_flg & 0x100000) ? sys->fog_col : cam.fog_col) & 0xFFFFFF;
    float c[3] = { (float)(col >> 16 & 0xFF), (float)(col >> 8 & 0xFF), (float)(col & 0xFF) }, n, f;

    /* the range the game set for the cut camera (cam.fog_*: the cut's, for flg 0x40/0x80 shifted
       by the cut camera's eye, bhControlActiveCamera), seen from ours */
    fog_range(cam.fog_nr, cam.fog_fr, &n, &f);
    snap = atmos_reset || (!cut_changed && (on != fog.on || col != fog.col));
    fog.a = snap ? on : step(fog.a, on);
    if (snap || (on && fog.a <= FADE)) {   /* at once, or fading in from no fog */
        fog.dn = fog.df = 0;
        for (int i = 0; i < 3; i++) fog.c[i] = c[i];
    } else if (on) {
        /* a new cut: the range eases from the one shown (the difference fades out) */
        if (cut_changed) { fog.dn = fog.n - n; fog.df = fog.f - f; }
        /* the colour follows the denser fog: towards a denser one it changes faster, towards a
           thinner one slower (else a thin bright fog and a dense dark one mix to a grey haze) */
        float k = EASE * fog_amount(n, f) / fmaxf(fog_amount(fog.n, fog.f), 0.01f);
        k = k < EASE / 4 ? EASE / 4 : k > EASE * 4 ? EASE * 4 : k;
        fog.dn -= fog.dn * EASE; fog.df -= fog.df * EASE;
        for (int i = 0; i < 3; i++) fog.c[i] += (c[i] - fog.c[i]) * k;
    }
    if (on || snap) { fog.n = n + fog.dn; fog.f = f + fog.df; }   /* fading out: the last range */
    fog.on = on; fog.col = col;

    if (fog.a <= 0) { njFogDisable(); return; }
    njSetFogColor((unsigned)(fog.c[0] + 0.5f) << 16 | (unsigned)(fog.c[1] + 0.5f) << 8 | (unsigned)(fog.c[2] + 0.5f));
    fog_table(fog.n, fog.f, fog.a);
    njSetFogTable(rom->fog);
    njFogEnable();
}

/* the light slots as bhControlLight fills them (one directional, three point lights; the
   lights' values of this frame are in lp->w*), each light scaled by its weight. A light that
   fades in waits for a free point-light slot, so slots never change hands mid-fade.
   The target is what the game shows: the first directional light that is on and the first three
   point lights that are on. A light that is on behind those gets no weight: RM_1020 (palace main
   hall) keeps the red alarm light of RM_1021 on behind the room's own directional light, and
   blending the directional lights by weight let it through during each fade. */
static void atmos_lights(int cut_changed)
{
    LGT_WORK* lp = rom->lgtp;
    LPTB* lptb = sys->lg_ptb;
    int n = rom->lgt_n < MAXL ? rom->lgt_n : MAXL, used = 0, lct = 2, i, dirs = 0, pts = 0;
    float dw = 0, dv[3] = { 0, 0, 0 }, dc[3] = { 0, 0, 0 }, l;
    int wait = 0;
    unsigned char on[MAXL];

    for (i = 0; i < n; i++) {   /* bhControlLight's choice (its on-screen test of flg 0x10 aside) */
        on[i] = (lp[i].flg & 3) == 3;
        if (on[i] && lp[i].lsrc == 2) on[i] = !dirs++;
        else if (on[i] && lp[i].lsrc == 4) on[i] = pts++ < 3;
    }
    /* a point light waits for a slot: the ones going out fade twice as fast, so the
       dip in between stays short */
    for (i = 0; i < n; i++) if (lp[i].lsrc == 4 && lw[i] > 0) used++;
    for (i = 4; i < n; i++) if (lp[i].lsrc == 4 && on[i] && lw[i] == 0 && used >= 3) wait = 1;
    used = 0;
    for (i = 0; i < n; i++) {
        if (atmos_reset || i < 4 || (on[i] != lon[i] && !cut_changed)) lw[i] = on[i];   /* 0..3 are global (muzzle flash, ...) */
        else if (on[i] && lw[i] > 0) lw[i] = step(lw[i], 1);
        else if (!on[i]) lw[i] = lw[i] > (wait ? 2 : 1) * FADE ? lw[i] - (wait ? 2 : 1) * FADE : 0;
        lon[i] = on[i];
        if (lp[i].lsrc == 4 && lw[i] > 0) used++;
    }
    for (i = 4; i < n; i++)   /* fading in: a point light only into a free slot */
        if (lon[i] && lw[i] == 0 && (lp[i].lsrc != 4 || used++ < 3)) lw[i] = FADE;

    for (i = 0; i < n; i++, lp++) {
        float w = lw[i];
        if (w <= 0) continue;
        if (lp->lsrc == 2) {   /* the outgoing and the incoming directional light cross-fade */
            dw += w;
            dv[0] += w * lp->wvx; dv[1] += w * lp->wvy; dv[2] += w * lp->wvz;
            dc[0] += w * lp->wr; dc[1] += w * lp->wg; dc[2] += w * lp->wb;
        } else if (lp->lsrc == 4 && lct < 5) {
            njCnkSetEasyMultiLightPoint(lct, lp->wpx, lp->wpy, lp->wpz);
            njCnkSetEasyMultiLightRange(lct, lp->wnr, lp->wfr);
            njCnkSetEasyMultiLightColor(lct, lp->wr * w, lp->wg * w, lp->wb * w);
            njCnkSetEasyMultiLightSwitch(lct, 1);
            njCnkSetSimpleMultiLightPoint(lct, lp->wpx, lp->wpy, lp->wpz);
            njCnkSetSimpleMultiLightRange(lct, lp->wnr, lp->wfr);
            njCnkSetSimpleMultiLightColor(lct, lp->wr * w, lp->wg * w, lp->wb * w);
            njCnkSetSimpleMultiLightSwitch(lct, 1);
            lptb->ps.x = lp->wpx; lptb->ps.y = lp->wpy; lptb->ps.z = lp->wpz;
            lptb->nr = lp->wnr; lptb->fr = lp->wfr;
            lptb->cr = lp->wr * w; lptb->cg = lp->wg * w; lptb->cb = lp->wb * w;
            lptb++; lct++;
        }
    }
    sys->lg_pnt = lct - 2;
    for (i = lct; i <= 4; i++) { njCnkSetEasyMultiLightSwitch(i, 0); njCnkSetSimpleMultiLightSwitch(i, 0); }
    l = sqrtf(dv[0] * dv[0] + dv[1] * dv[1] + dv[2] * dv[2]);
    if (dw > 0 && l > 0.0001f) {
        float k = dw > 1 ? 1 / dw : 1;   /* one light at full weight at most; less while one fades in from none */
        sys->st_flg |= 0x10000;
        sys->lg_vx = dv[0] / l; sys->lg_vy = dv[1] / l; sys->lg_vz = dv[2] / l;
        sys->lg_r = dc[0] * k; sys->lg_g = dc[1] * k; sys->lg_b = dc[2] * k;
    } else {
        sys->st_flg &= ~0x10000;
        sys->lg_vx = sys->lg_vy = sys->lg_vz = sys->lg_r = sys->lg_g = sys->lg_b = 0;
    }
    njCnkSetEasyMultiLight(lct - 1);
    njCnkSetSimpleMultiLight(lct - 1);
    bhSetLight();   /* the directional light, the colours, the view-space matrices */
}

/* game.c bhMainSequence, after bhControlLight (before drawing) */
void tps_atmos(void)
{
    int cut, own, smooth_on;
    if (atmos_trace < 0) atmos_trace = getenv("RECVX_ATMOS_TRACE") != NULL;
    if (!sys || !rom || !plp) return;
    /* our camera with the room's lights (not an event's, not the inventory's) */
    own = tps_cam_active && !(sys->gm_flg & 0x80) && !(sys->st_flg & 0x1000000) && !(cam.flg & 0x2);
    smooth_on = g_tps.atmos && own;
    if (own) {
        cut = cam.ncut | cam.camver << 8;
        if (!smooth_on) atmos_reset = 1;   /* per cut: the fog at once; lights and filters the game's */
        atmos_fog(cut != last_cut);
        if (smooth_on) {
            atmos_lights(cut != last_cut);
            atmos_reset = 0;
        }
        last_cut = cut;
    } else {
        atmos_off();
    }
    if (atmos_trace) atmos_log(smooth_on);
}

/* back to the game's own light and fog (the lights are set from scratch every frame) */
static void atmos_off(void)
{
    if (last_cut < 0) return;
    atmos_reset = 1;
    last_cut = -1;
    if (sys) sys->gm_flg |= 0x10;   /* the game sets its fog again (sync.c bhControlEOR) */
}

/* effsub1.c bhEffFil (screen filters, effects 90..92), after its set-up (mode0 0): with
   "smooth" the filter fades in and out (alpha) instead of switching with the cut.
   Returns 1 if handled here. */
int tps_filter(O_WRK* op)
{
    static const float uv[4][8] = { { 0, 0, 1, 0, 0, 1, 1, 1 }, { 0, 1, 0, 0, 1, 1, 1, 0 },   /* fil_rt & 3, as bhEffFil */
                                    { 1, 1, 0, 1, 1, 0, 0, 0 }, { 1, 0, 1, 1, 0, 0, 0, 1 } };
    int k = op->id - 90, on = (cam.flg & 0x8) && sys->fil_no == k, a;
    float half;
    if (k < 0 || k > 3) return 0;
    if (atmos_reset) {   /* not smooth now: the game's state, which a fade starts from later */
        filw[k] = on;
        fila[k] = (sys->fil_rt & 0xF0) | 0xF;
        return 0;
    }
    filw[k] = step(filw[k], on);
    if (on) {
        for (int i = 0; i < 4; i++) { op->tv[i].u = uv[sys->fil_rt & 3][i * 2]; op->tv[i].v = uv[sys->fil_rt & 3][i * 2 + 1]; }
        fila[k] += (((sys->fil_rt & 0xF0) | 0xF) - fila[k]) * FADE;
    }
    if (filw[k] <= 0 || pc_screen_filter_mode() == 2) {
        op->flg |= 0x1000000;
        op->mode0 = 1;
        return 1;
    }
    a = (int)(fila[k] * filw[k] + 0.5f);
    op->tv[3].col = op->tv[2].col = op->tv[1].col = op->tv[0].col = (unsigned)a << 24 | 0xFFFFFF;
    half = 320.0f * (pc_screen_filter_mode() == 1 ? pc_hor_scale() : 1.0f);
    op->tv[0].x = op->tv[2].x = 320.0f - half;
    op->tv[1].x = op->tv[3].x = 320.0f + half;
    op->mode0 = 2;
    op->flg &= ~0x1000000;
    sys->ef_trs2d[sys->ef_trs2dn++] = op;
    return 1;
}

/* Our camera is this frame's view: also asked before tps_camera takes over in this frame
   (effects run first). effsub1.c bhEff110 asks it for the under-water view of a cut (cut flag
   0x400 -> cam.flg 0x20: the full-screen distortion of the submarine dock RM_4030/RM_8050,
   whose cuts 0, 2 and 5 look in through the windows from the water). It belongs to the fixed
   camera's place; ours is always in the room's air (there is no diving), so it stays off. */
int tps_own_view(void)
{
    return tps_on && camera_allowed();
}

/* system.c bhSysCallScreenSaver: the game's idle check only reads the PS2 pad (p1per), so the
   input added on top of it - third-person keys and mouse, quick knife, inventory mouse, trade
   screen, all ending up in sys->pad_on - would let the 6-minute idle dimming start during play
   and never end it (only a pad button or pausing did, not a door). Attract demo input is not
   the player's. */
int pc_input_active(void)
{
    return (sys->pad_on && !(sys->ss_flg & 0x400000)) || (tps_on && (mouse_moved || stick_moved));
}

/* the gamepad, modern scheme: which device played last, the right stick as camera turn, the left
   stick as forward (and run) towards its direction seen from the camera (tps_player turns the
   character). Pulled back and run pressed within QT_WAIT frames: the game's quick turn (the
   turn on the spot waits for it that long). A quick turn or recenter swings the camera behind
   the character; the stick keeps the camera it was given in until it turns or is let go
   (modern_hold, as over the fixed cameras' cuts), so a held direction keeps walking that way.
   Returns the quick-turn gesture's hold. */
#define QT_WAIT 3
static ModernHold hold;
static int swings;                /* quick turns and recenters (modern_hold's "cut") */

static int pad_sticks(unsigned int* pad)
{
    static int run_was, qt_hold, back_wait;
    float lx, ly, rx, ry, m;
    int stick = g_tps.pad_play && tps_pad_sticks(&lx, &ly, &rx, &ry), run;
    if (tps_pad_touched()) pad_cam = g_tps.pad_modern;
    else if (mouse_moved || tps_kbm_touched()) pad_cam = 0;
    if (!g_tps.pad_modern) pad_cam = 0;
    stick_yaw = stick_pitch = 0;
    stick_moved = pad_move = 0;
    {   /* the right stick turns the camera (Modern) and aims. Classic keeps the game's buttons, whose turn while aiming
           cannot move free aim's crosshair (the shot follows the crosshair), so there the right stick (free in every key
           type: the game reads only the left one) aims too. In both, while aiming freely, the left stick and the d-pad
           also move the crosshair, as they turn and tilt the original aim; not in the scope, which the game turns by
           them itself. */
        int aim_free = plp->mode0 == 1 && plp->mode1 == 1 && !lock_aim() && pc_in_play(plp, 1);
        if (g_tps.pad_modern ? stick && pad_cam : aim_free && tps_pad_sticks(&lx, &ly, &rx, &ry)) {
            float ax = lx, ay = ly, dx, dy;
            if (modern_stick(&rx, &ry, g_tps.pad_dead_look / 100.0f, 2.0f) > 0) recenter = 0, stick_moved = 1;
            if (aim_free && tps_cam_active) {
                modern_stick(&ax, &ay, g_tps.pad_dead_move / 100.0f, 2.0f);
                tps_pad_dpad(&dx, &dy);
                rx += ax + dx; ry += ay + dy;
                rx = rx > 1 ? 1 : rx < -1 ? -1 : rx;
                ry = ry > 1 ? 1 : ry < -1 ? -1 : ry;
                stick_moved |= rx != 0 || ry != 0;
            }
            stick_yaw = -rx * STICK_YAW * g_tps.pad_sens_x / 100.0f;
            stick_pitch = (g_tps.pad_invert_y ? -ry : ry) * STICK_PITCH * g_tps.pad_sens_y / 100.0f;
        }
    }
    if (!stick || !pad_cam) { run_was = qt_hold = back_wait = hold.held = 0; return 0; }
    m = modern_stick(&lx, &ly, g_tps.pad_dead_move / 100.0f, 1.0f);
    run = bind_held(B_TPS_RUN, BIND_PAD, bind_keys(), 0);
    if (m > 0 && plp->mode0 == 1) {   /* (hits, grabs: the game's own stick directions) */
        float cx = cosf(RAD(yaw)), cz = -sinf(RAD(yaw)), d;   /* the camera's right axis on the ground */
        int free_turn, back;
        modern_hold(&hold, atan2f(lx, ly) * (32768.0f / 3.14159265f), swings, &cx, &cz);
        pad_target = modern_target(cx, cz, lx, ly);
        d = modern_wrap(pad_target - (short)plp->ay);
        free_turn = plp->mode1 == 0 && plp->mode2 <= 13 && plp->mode2 != 12 && !(plp->flg & 0x100000);
        back = free_turn && fabsf(d) > DEG(100.0f);
        back_wait = back ? back_wait + 1 : 0;
        if (back && run && !run_was && back_wait <= QT_WAIT + 1) { qt_hold = 10; back_wait = QT_WAIT + 1; }   /* pulled back + run */
        *pad &= ~0x6F;   /* the stick's (and the d-pad's) directions and aim up/down */
        if (tps_cam_active && plp->mode1 == 0 && !qt_hold && !(back && back_wait <= QT_WAIT && !run)) {
            if (!free_turn) {   /* stairs, steps, pushing, ...: on while it points ahead */
                if (fabsf(d) < DEG(90.0f)) *pad |= 0x1;
            } else {
                if (back) *pad |= d > 0 ? 0x4 : 0x8;   /* turn on the spot first */
                else *pad |= 0x1;
                if (m >= RUN_PUSH) *pad |= 0x400;
                pad_move = 1;
            }
        }
    } else hold.held = back_wait = 0;
    run_was = run;
    if (trace) {
        float oy = 0, op = 0;
        int walled = 0, e = nearest_enemy(&oy, &op, &walled);
        printf("tps: pad frame %u left %.2f %.2f (%.2f) right %.2f %.2f -> target %.1f ay %.1f yaw %.1f pitch %.1f quickturn %d mode %d/%d/%d"
               " at (%.1f %.1f) nearest enemy %s%.1f %.1f deg off\n",
               (unsigned)gs_frame_count(), lx, ly, m, rx, ry, pad_target * (360.0f / 65536.0f), (short)plp->ay * (360.0f / 65536.0f),
               yaw * (360.0f / 65536.0f), pitch * (360.0f / 65536.0f), qt_hold, plp->mode1, plp->mode2, plp->mode3, plp->px, plp->pz,
               !e ? "none " : walled ? "(behind a wall) " : "", oy * (360.0f / 65536.0f), op * (360.0f / 65536.0f));
    }
    return qt_hold > 0 ? qt_hold-- : 0;
}

/* the Modern gamepad outside third-person play: its rows give the game rows their buttons back, so the
   inventory (Y) and the map (View) it opened did not close with the same button, as the game's do with
   theirs. A press of the row's button made while that screen is up closes it (the game's status / map bit). */
static unsigned toggle_closes(void)
{
    static int prev[2];
    static const int row[2] = { B_TPS_STATUS, B_TPS_MAP };
    unsigned r = 0;
    int scr = (sys->ts_flg & 0x80) ? !(sys->ts_flg & 0x200) ? 0 : !(sys->ts_flg & 0x400) ? 1 : -1 : -1;
    for (int i = 0; i < 2; i++) {   /* (the press that opened it began in play: no new press) */
        int now = bind_pad_raw(row[i]);
        if (now && !prev[i] && scr == i && g_tps.pad_modern && !g_tps.pad_play) r |= i ? 0x2000 : 0x4000;
        prev[i] = now;
    }
    return r;
}

/* pad.c bhSetPad, before pad_ps is derived: the keyboard/mouse layout as the game's
   virtual pad bits (independent of the controller key type) */
unsigned int tps_pad(unsigned int pad)
{
    unsigned k;
    int scope;
    if (sys->ss_flg & 0x400000) return pad;   /* attract demo playback */
    pad |= toggle_closes();
    k = tps_input_keys(plp && pad_sticks(&pad));
    pad_steer = (pad & 0xF) != 0 && !pad_cam;   /* (the gamepad: its stick, or a d-pad turn: the camera stays) */
    scope = in_scope();
    {   /* the scope: the recenter row (right stick click) switches the zoom (the game's "change aim" bit 0x80,
           held: L1, the quick knife's button here); run (Shift) zooms while held */
        static unsigned was;
        static int zoom;
        if (scope && (k & TPS_K_RECENTER) && !(was & TPS_K_RECENTER)) zoom = !zoom;
        if (!scope) zoom = 0;
        was = k;
        if (scope && (zoom || (k & TPS_K_RUN))) pad |= 0x80;
    }
    {   /* the gamepad: recenter, and a quick turn swings the camera round with the character (not in the scope) */
        static unsigned was;
        unsigned now = pad_cam && !scope ? k & (TPS_K_RECENTER | TPS_K_QUICKTURN) : 0;
        if (now & ~was) { recenter = 20; swings++; }
        was = now;
    }
    move_keys = 0;
    if (g_tps.free_cam && tps_cam_active && plp && plp->mode1 == 0 && !(k & TPS_K_QUICKTURN)) {
        /* camera A: any direction walks forward, tps_player turns the character towards it */
        move_keys = k & (TPS_K_FWD | TPS_K_BACK | TPS_K_LEFT | TPS_K_RIGHT);
        k &= ~move_keys;
        if (!!(move_keys & TPS_K_FWD) != !!(move_keys & TPS_K_BACK) || !!(move_keys & TPS_K_LEFT) != !!(move_keys & TPS_K_RIGHT))
            pad |= 0x1;
    }
    if (k & TPS_K_FWD) pad |= 0x21;   /* as the d-pad: also aim up/down (game auto-aim) */
    if (k & TPS_K_BACK) pad |= 0x42;
    if (k & TPS_K_LEFT) pad |= 0x4;
    if (k & TPS_K_RIGHT) pad |= 0x8;
    if (k & TPS_K_AIM) pad |= 0x10;
    if (k & TPS_K_RUN) pad |= 0x400;
    if (k & (TPS_K_ACTION | TPS_K_FIRE)) pad |= 0xB00;   /* fire/examine/confirm, like Cross */
    if (k & TPS_K_CANCEL) pad |= 0x1000;
    if (k & TPS_K_MAP) pad |= 0x2000;
    if (k & TPS_K_STATUS) pad |= 0x4000;
    if (k & TPS_K_OPTIONS) pad |= 0x8000;   /* (the gamepad's, Modern) */
    /* free aim: always the straight aim pose; tps_calc_model bends the upper body to the pitch */
    if (g_tps.aiming && !lock_aim() && plp && plp->mode1 == 1) pad &= ~0x63;
    if (pad & 0x1) pad &= ~0x42;   /* as bhSetPad: forward wins, right wins */
    if (pad & 0x8) pad &= ~0x4;
    return pad;
}

/* distance along the ray p + t*d (|d| = 1) to the capsule, or -1 if it misses */
static float ray_capsule(const NJS_POINT3* p, const NJS_POINT3* d, const NJS_CAPSULE* c)
{
    float best = -1.0f;
    for (int i = 0; i <= 8; i++) {   /* ponytail: 9 spheres along the capsule, exact enough for aiming */
        float f = i / 8.0f, r = c->r + 0.5f;
        float cx = c->c1.x + (c->c2.x - c->c1.x) * f - p->x, cy = c->c1.y + (c->c2.y - c->c1.y) * f - p->y,
              cz = c->c1.z + (c->c2.z - c->c1.z) * f - p->z;
        float t = cx * d->x + cy * d->y + cz * d->z, q = cx * cx + cy * cy + cz * cz - t * t;
        if (t > 0 && q < r * r && (best < 0 || t < best)) best = t;
    }
    return best;
}

static int aimable(const BH_PWORK* e) { return (e->flg & 0x1) && (e->flg & 0x20) && !(e->flg & 0x2) && !(e->stflg & 0x1000000); }

/* ---------------------------------------------------------------- "Precise hits" (docs/THIRDPERSON.md)
   The game hits an enemy when a capsule along the shot, 2 units wide plus 0.2..0.4 per unit of distance
   (the shotgun 0.3, up to 40), touches the enemy's one hit capsule (watr, the whole body and more):
   a shot beside the enemy hits it. With this option the player's hitscan shots in third person (and
   in its scope) hit an enemy only where the ray meets its body: the joints' capsules and spheres of the
   enemy's table (cpcl: head, torso, arms, legs on its bones as posed this frame; the game's own, used
   for the sniper rifle and the bow gun), else its hit capsule as a ray. The hit's joint is the volume's,
   so the game's damage per body part (a zombie's head, arms, legs) follows the shot. */
#define HEAD_DAMAGE 150       /* a hit on the head joint does this percent of its damage (head_joint) */
#define SHOTGUN_R0 0.3f       /* the shotgun's cone: this wider at the muzzle, */
#define SHOTGUN_K 0.06f       /* plus this per unit of distance (3.4 degrees either side), */
#define SHOTGUN_MAX 3.0f      /* at most this */
unsigned char tps_hit_precise[PC_ENE_SLOTS];   /* per enemy slot: its last gun hit was precise, as weapon + 1 (0: not; coop_glue.c) */
static float xr[7];                   /* the crosshair's ray (aim_at_crosshair): from, direction, length to its wall */
static int dual_yaw;                  /* dual weapons, precise hits: the yaw of the gun turned to another enemy (dual_aim) */
static int ticks, xr_tick = -1;      /* tps_player calls; the one in which it was cast (a shot in the mode handler before the next: the same) */

int pc_precise_hits(void)
{
    return tps_on && g_tps.precise && plp && (tps_cam_active ? !lock_aim() : in_scope());
}

/* the head's joint of enemy type id, -1: none or one that keeps its own rules: Nosferatu (its weak spot),
   Alexia 2 and 3, the Gulp Worm (its own +30% on the head), the Hunter (head and chest are one volume) */
static int head_joint(int id)
{
    switch (id) {
    case 1: return 11;    /* zombie */
    case 26: return 13;   /* anatomist */
    case 4: return 5;     /* Cerberus */
    case 9: return 4;     /* Bandersnatch */
    case 12: return 5;    /* Alexia 1 */
    case 17: return 5;    /* Steve */
    case 19: return 5;    /* Tyrant */
    }
    return -1;
}

/* volume *c of enemy e's table (advanced past it): capsule a-b of radius r (a sphere: a == b) on the bones
   (bhCheckCapCol2Capsule's reading), its joint */
static int volume(const BH_PWORK* e, const CPCL** c, float* a, float* b, float* r)
{
    const CPCL* v = (*c)++;
    const float* m = e->mlwP->owP[v->jnt_a].mtx;
    int k;
    *r = 0.1f * v->cap_r;
    if (v->jnt_a == v->jnt_b) {   /* a sphere: the next row is its offset in the joint's frame */
        const CPCL* o = (*c)++;
        for (k = 0; k < 3; k++) a[k] = b[k] = 0.1f * (o->jnt_a * m[k] + o->jnt_b * m[4 + k] + o->cap_r * m[8 + k]) + m[12 + k];
    } else
        for (k = 0; k < 3; k++) a[k] = m[12 + k], b[k] = e->mlwP->owP[v->jnt_b].mtx[12 + k];
    return v->jnt_a;
}

/* the ray p + t d (|d| = 1) against enemy e's body, each volume wider by the shotgun's cone at its distance
   (cone): the t where it enters (-1: a miss), *jnt its joint (the hit capsule: the joint nearest the point,
   as the game picks it), *no its entry in the table (-1: the hit capsule) */
static float body_ray(const BH_PWORK* e, const float* p, const float* d, int cone, int* jnt, int* no)
{
    const CPCL* c = e->cpcl;
    float best = -1, a[3], b[3], r, t, m;
    int j, n;
    *no = -1;
    if (!e->mlwP) return -1;
    for (n = 0; c && c->cap_r; n++) {
        j = volume(e, &c, a, b, &r);
        if (cone) {
            float x = (a[0] + b[0]) / 2 - p[0], y = (a[1] + b[1]) / 2 - p[1], z = (a[2] + b[2]) / 2 - p[2], w = SHOTGUN_R0 + SHOTGUN_K * sqrtf(x * x + y * y + z * z);
            r += w < SHOTGUN_MAX ? w : SHOTGUN_MAX;
        }
        t = pc_ray_capsule(p, d, a, b, r, &m);
        if (t >= 0 && (best < 0 || t < best)) best = t, *jnt = j, *no = n;
    }
    if (!e->cpcl) {
        const NJS_CAPSULE* w = &e->watr;
        r = w->r;
        if (cone) {
            float x = (w->c1.x + w->c2.x) / 2 - p[0], y = (w->c1.y + w->c2.y) / 2 - p[1], z = (w->c1.z + w->c2.z) / 2 - p[2], s = SHOTGUN_R0 + SHOTGUN_K * sqrtf(x * x + y * y + z * z);
            r += s < SHOTGUN_MAX ? s : SHOTGUN_MAX;
        }
        if ((best = pc_ray_capsule(p, d, &w->c1.x, &w->c2.x, r, &m)) >= 0) {
            float q = 0, h[3] = { p[0] + d[0] * best, p[1] + d[1] * best, p[2] + d[2] * best };
            *jnt = 0;
            for (j = 1; j < (int)e->mlwP->obj_num; j++) {
                const float* o = e->mlwP->owP[j].mtx;
                float l = (h[0] - o[12]) * (h[0] - o[12]) + (h[1] - o[13]) * (h[1] - o[13]) + (h[2] - o[14]) * (h[2] - o[14]);
                if (!*jnt || l < q) *jnt = j, q = l;
            }
        }
    }
    return best;
}

/* weapon.c bhCheckGunAtari: the shot against enemy pp: the joint hit, -1 a miss; hp where the ray enters.
   With our camera the ray is the crosshair's (from the shoulder point to its wall, aim_at_crosshair the tick
   before: the game's ay / wax were turned to it), as seen; the game's ray (c1 -> c2, its cone ignored) starts
   lower and beside it, at the character's chest, and crossed the crosshair's only at its target: aimed just
   beside an enemy in front of a far wall, it went through the enemy. In the scope the game's ray is the
   view's. The volume's entry goes to cpcl_no as the game's test sets it (Nosferatu's core). */
int pc_precise_hit(BH_PWORK* pp, int wpn, const float* c1, const float* c2, float* hp)
{
    float p[3], d[3], l, t;
    int j = -1, no, k, x = tps_cam_active && xr_tick == ticks;   /* x: the crosshair's ray */
    if (dual_e >= 0 && abs((short)(plp->way - dual_yaw)) < DEG(1.0f)) x = 0;   /* the dual gun turned to another enemy (dual_aim): its own ray */
    if (x)
        for (k = 0, l = xr[6]; k < 3; k++) p[k] = xr[k], d[k] = xr[3 + k];
    else {
        for (k = 0; k < 3; k++) p[k] = c1[k], d[k] = c2[k] - c1[k];
        if ((l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2])) <= 0) return -1;
        for (k = 0; k < 3; k++) d[k] /= l;
    }
    t = body_ray(pp, p, d, 0, &j, &no);
    if (t < 0 && wpn == 11) t = body_ray(pp, p, d, 1, &j, &no);   /* the shotgun: the part under the crosshair, else in its cone */
    if (t < 0 || t > l) return -1;
    if (no >= 0) pp->cpcl_no = no;
    for (k = 0; k < 3; k++) hp[k] = p[k] + d[k] * t;
    if (trace) printf("tps: PRECISE wpn %d hits enemy %d (type %d) joint %d volume %d %.1f away along the %s ray, %.2f above its feet\n", wpn, (int)(pp - ene), pp->id, j, no, t,
                      x ? "crosshair's" : "game's", hp[1] - pp->py);
    return j;
}

/* bhCheckGunAtari after a gun hit added to enemy pp's damage on joint jnt (d0 before): whether it was a
   precise hit (with the weapon), and a precise hit on the head does HEAD_DAMAGE percent */
void pc_precise_dam(BH_PWORK* pp, int jnt, int d0, int precise, int wpn)
{
    int i = (int)(pp - ene);
    if (i >= 0 && i < PC_ENE_SLOTS) tps_hit_precise[i] = precise ? wpn + 1 : 0;
    if (precise && jnt == head_joint(pp->id)) pp->dam[jnt] = d0 + (pp->dam[jnt] - d0) * HEAD_DAMAGE / 100;
    if (trace) printf("tps: HIT wpn %d enemy %d (type %d, hp %d) joint %d damage %d%s, its state %d modes %d/%d, %.1f above its feet\n", wpn, i, pp->id,
                      pp->hp, jnt, pp->dam[jnt] - d0, !precise ? " (game's test)" : jnt == head_joint(pp->id) ? " (precise, head)" : " (precise)",
                      pp->type, pp->mode0, pp->mode2, pp->dpy - pp->py);
}

/* en01.c bhEne01_CheckExpHead: the zombie's hit being judged was precise (a head burst only on the head) */
int pc_precise_head(BH_PWORK* pp)
{
    int i = (int)(pp - ene);
    return i >= 0 && i < PC_ENE_SLOTS && tps_hit_precise[i] && tps_hit_precise[i] == pp->wpnr_no + 1;
}

/* the enemy whose middle is nearest the crosshair by angle: its offset (target minus crosshair,
   game units) and whether a wall is in between; 0 = none */
static int nearest_enemy(float* oy, float* op, int* walled)
{
    NJS_POINT3 eye, dir, sh, apos;
    NJS_LINE line;
    ATR_WORK* ahtp;
    float best = -1, t[3] = { 0, 0, 0 }, len;
    view(&eye, &dir, &sh);
    for (int i = 0; i < sys->ewk_n; i++) {
        const NJS_CAPSULE* c = &ene[i].watr;
        float x = (c->c1.x + c->c2.x) / 2 - sh.x, y = (c->c1.y + c->c2.y) / 2 - sh.y, z = (c->c1.z + c->c2.z) / 2 - sh.z, ey, ep;
        if (!aimable(&ene[i]) || x * x + z * z < 1.0f) continue;
        ey = wrap(atan2f(-x, -z) * (32768.0f / 3.14159265f) - yaw);
        ep = atan2f(y, sqrtf(x * x + z * z)) * (32768.0f / 3.14159265f) - pitch;
        if (best < 0 || ey * ey + ep * ep < best) { best = ey * ey + ep * ep; *oy = ey; *op = ep; t[0] = x; t[1] = y; t[2] = z; }
    }
    if (best < 0) return 0;
    line.px = sh.x; line.py = sh.y; line.pz = sh.z; line.vx = t[0]; line.vy = t[1]; line.vz = t[2];
    apos = sys->apos; ahtp = sys->ahtp;
    *walled = bhCheckL2Wall(&line, 0x400, &len) && len < sqrtf(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
    sys->apos = apos; sys->ahtp = ahtp;
    return 1;
}

/* gentle aim assist (gamepad, "Game auto-aim" on): the stick's turn this frame with the enemy
   nearest the crosshair, if it is within the cone and no wall is in between (modern_assist) */
static void assist(float* dyaw, float* dpitch)
{
    float oy, op;
    int walled;
    if (!nearest_enemy(&oy, &op, &walled) || walled) return;
    modern_assist(oy, op, dyaw, dpitch);
    if (trace) printf("tps: assist enemy %.2f %.2f deg off -> turn %.2f %.2f deg\n", oy * (360.0f / 65536.0f), op * (360.0f / 65536.0f),
                      *dyaw * (360.0f / 65536.0f), *dpitch * (360.0f / 65536.0f));
}

/* free aim: what the crosshair (screen centre) points at - the nearest enemy or wall on the
   view ray - and the character's yaw/pitch so the game's own shot ray (origin
   (px, py + 11, pz) + forward * (3 + r), direction Rx(wax) Ry(ay)) goes through it. The ray is
   cast from the shoulder point, which it passes through wherever walls put the camera. A wall
   closer than AIM_MIN counts as AIM_MIN away: a target next to the character would turn it
   (shoulder offset) and tilt the aim (shot origin below the camera) by tens of degrees. */
#define AIM_MIN 40.0f   /* ponytail: parallax limit, at most ~5 degrees yaw / 8 degrees pitch off the view */
static void aim_at_crosshair(void)
{
    NJS_POINT3 eye, dir, sh, t, o, apos;
    NJS_LINE line;
    ATR_WORK* ahtp;
    float dist = 1000.0f, wall, len, h, r = WpnTab[plp->wpnr_no].r;
    int ay, prec = pc_precise_hits();

    view(&eye, &dir, &sh);
    line.px = sh.x; line.py = sh.y; line.pz = sh.z;
    line.vx = dir.x * dist; line.vy = dir.y * dist; line.vz = dir.z * dist;
    apos = sys->apos; ahtp = sys->ahtp;
    if (bhCheckL2Wall(&line, 0x400, &len)) dist = len;
    sys->apos = apos; sys->ahtp = ahtp;
    wall = dist;
    xr[0] = sh.x; xr[1] = sh.y; xr[2] = sh.z; xr[3] = dir.x; xr[4] = dir.y; xr[5] = dir.z; xr[6] = wall;
    xr_tick = ticks;
    aim_e = -1;
    for (int i = 0; i < sys->ewk_n; i++) {
        BH_PWORK* e = &ene[i];
        float d;
        int j, n;
        if (!aimable(e)) continue;
        d = prec ? body_ray(e, &sh.x, &dir.x, 0, &j, &n) : ray_capsule(&sh, &dir, &e->watr);
        if (d > 0 && d < dist) dist = d, aim_e = i;   /* in front of the shoulder */
    }
    if (dist == wall && dist < AIM_MIN) dist = AIM_MIN;
    t.x = sh.x + dir.x * dist; t.y = sh.y + dir.y * dist; t.z = sh.z + dir.z * dist;

    ay = (int)(atan2f(-(t.x - plp->px), -(t.z - plp->pz)) * (32768.0f / 3.14159265f));
    o.x = plp->px - sinf(RAD(ay)) * (3.0f + r); o.y = plp->py + 11.0f; o.z = plp->pz - cosf(RAD(ay)) * (3.0f + r);
    if (dist == wall) {   /* the crosshair is on a wall: bhCheckGunAtari tests walls from the muzzle (the hand's
                             point wp_fps1.z, owP[9]), about 3 units above o, so a ray aimed from o hit them
                             that much above the crosshair. Aimed from the muzzle as it will be after turning. */
        const float* hm = plp->mlwP->owP[9].mtx;
        float z = WpnTab[plp->wpnr_no].wp_fps1.z, mx = hm[8] * z + hm[12] - plp->px, mz = hm[10] * z + hm[14] - plp->pz;
        float tr = RAD((short)(ay - plp->ay)), c = cosf(tr), s = sinf(tr);
        o.x = plp->px + mx * c + mz * s; o.y = hm[9] * z + hm[13]; o.z = plp->pz - mx * s + mz * c;
    }
    ay = (int)(atan2f(-(t.x - o.x), -(t.z - o.z)) * (32768.0f / 3.14159265f));
    h = sqrtf((t.x - o.x) * (t.x - o.x) + (t.z - o.z) * (t.z - o.z));
    plp->ay += (short)(ay - plp->ay);
    plp->wax = plp->waxp = (short)(int)(atan2f(t.y - o.y, h) * (32768.0f / 3.14159265f));
    /* the knife slashes level at anything not below it: an enemy right in front puts the crosshair
       on its body, well above the hand at that range, which raised the slash */
    if ((WpnTab[plp->wpnr_no].flg & 0x40000000) && plp->wax > 0) plp->wax = plp->waxp = 0;
    aim_t = t;
    /* the game's aim height for what the shot goes at, read by the enemies until the next mode
       handler (tps_player_mode): zombies and the anatomist on the ground, a knocked-down Hunter,
       Cerberus and Bandersnatch in one of their states take no damage from a straight shot; a
       shotgun blows a zombie's head only aimed up. Up/down beyond 22.5 degrees (half the original
       aim's 45), and down at anything below knee height. */
    if (plp->at_flg & 0xE) {
        aim_h = plp->wax > 4096 ? 4 : plp->wax < -4096 || t.y - plp->py < 6.0f ? 8 : 2;
        plp->at_flg = (plp->at_flg & ~0xE) | aim_h;
        if (aim_h == 2) aim_h = 0;
    }
    if (trace) {   /* check: the game's shot direction Rx(wax) Ry(ay) (0, 0, -l) from o reaches t */
        NJS_MATRIX m;
        NJS_VECTOR v, w;
        float l = sqrtf(h * h + (t.y - o.y) * (t.y - o.y)), hp = 0;
        njUnitMatrix(&m);
        njRotateXYZ(&m, plp->wax, plp->ay, 0);
        v.x = v.y = 0; v.z = -l;
        njCalcVector(&m, &v, &w);
        for (int i = 0; i < sys->ewk_n; i++) if (ene[i].flg & 1) hp += ene[i].hp;
        printf("tps: mode %d/%d wpn %d ammo %u: aim yaw %.1f pitch %.1f -> ay %.1f wax %.1f, target %.1f away (aim height %d), shot misses it by %.2f, enemy hp %.0f, eye (%.1f %.1f %.1f)\n",
               plp->mode2, plp->mode3, plp->wpnr_no, sys->itm[sys->ply_id * 16 + (sys->itm[sys->ply_id * 16] & 15)] & 0xFFFF,
               yaw * (360.0f / 65536.0f), pitch * (360.0f / 65536.0f), (short)plp->ay * (360.0f / 65536.0f),
               plp->wax * (360.0f / 65536.0f), dist, plp->at_flg & 0xE,
               sqrtf((o.x + w.x - t.x) * (o.x + w.x - t.x) + (o.y + w.y - t.y) * (o.y + w.y - t.y) + (o.z + w.z - t.z) * (o.z + w.z - t.z)), hp, eye.x, eye.y, eye.z);
    }
}

/* Dual weapons (WpnTab flg 0x800: the Gold Lugers, the sub machine guns; playpch.c): each arm fires along
   ay plus a yaw of its own, the right (hand 9, CheckGunHit at the shot) by arm_dr, the left (hand 13, two
   ticks later) by arm_dl, and PlyPchMain turns each arm by arm_ar / arm_al eased towards them. The game sets
   them only at its lock-on (SetLockOnDirection: raising the weapon, L1), relative to the facing it turns to
   then (between two enemies in front of it, each arm on one). Free aim turns the character to the crosshair
   instead, so the arms stayed turned by those offsets: held outwards, firing past the crosshair at where the
   enemies had been. Free aim sets them every tick: one arm at the crosshair target, the other at the nearest
   other enemy within 45 degrees (yaw) / 15 (pitch) of the crosshair direction and in range, on the side of that arm
   (as the game, which gives the left arm the enemy further left), else at the crosshair target too; each
   yaw from its own muzzle (the shot's origin), within DUAL_YAW. The pitch (wax, the bend) and the aim height
   stay the crosshair target's, so the other enemy must take the same aim height. */
#define DUAL_YAW PC_DUAL_YAW   /* (pc_rules.h pc_dual_arm) */
static int dual_e = -1;   /* the other arm's enemy this tick, -1: none (also the trace's) */

/* the muzzle of the weapon in hand `node` as drawn last tick, turned about the player with its facing since (ay0) */
static void muzzle(int node, int ay0, float* m)
{
    const float* h = plp->mlwP->owP[node].mtx;
    float z = WpnTab[plp->wpnr_no].wp_fps1.z, x = h[8] * z + h[12] - plp->px, y = h[10] * z + h[14] - plp->pz;
    float tr = RAD((short)(plp->ay - ay0)), c = cosf(tr), s = sinf(tr);
    m[0] = plp->px + x * c + y * s; m[1] = h[9] * z + h[13]; m[2] = plp->pz - x * s + y * c;
}

/* an arm's yaw from its muzzle m to t, relative to the facing, within DUAL_YAW */
static int arm_yaw(const float* m, const float* t)
{
    float a = wrap(atan2f(-(t[0] - m[0]), -(t[2] - m[2])) * (32768.0f / 3.14159265f) - (short)plp->ay);
    return (int)(a > DUAL_YAW ? DUAL_YAW : a < -DUAL_YAW ? -DUAL_YAW : a);
}

/* prev: last tick's dual_e when dual_aim ran then (tps_player), else -1 */
static void dual_aim(int ay0, int prev)
{
    PP_WORK* pp = (PP_WORK*)plp->exp2;
    float r[3], l[3], t[3], best = 0;
    int cls = plp->at_flg & 0xE, left = 0, keep = 0, prec = pc_precise_hits();
    muzzle(9, ay0, r);
    muzzle(13, ay0, l);
    for (int i = 0; i < sys->ewk_n; i++) {
        BH_PWORK* e = &ene[i];
        const NJS_CAPSULE* c = &e->watr;
        float x = (c->c1.x + c->c2.x) / 2 - plp->px, y = (c->c1.y + c->c2.y) / 2 - plp->py, z = (c->c1.z + c->c2.z) / 2 - plp->pz;
        float h = sqrtf(x * x + z * z), d = sqrtf(h * h + y * y), dy, p;
        int k, arm;
        NJS_CAPSULE cap;
        NJS_POINT3 apos;
        ATR_WORK* ahtp;
        /* what the game's own search takes (playpch.c bhSearchEnemy) */
        if (!aimable(e) || (e->flg2 & 0x10) || (e->stflg & 0x40000000) ||
            ((e->flg & 0x80) && (((O_WRK*)e->lkwkp)->stflg & 0x1000000)) || h < 1.0f || d > WpnTab[plp->wpnr_no].l || (i != prev && (keep || (best > 0 && d >= best))))
            continue;   /* (the last tick's enemy is kept while it stays in a slightly wider cone: else two at the
                           cone's edge or at the same distance took turns, and the arms swung between them) */
        dy = wrap(atan2f(-x, -z) * (32768.0f / 3.14159265f) - (short)plp->ay);   /* > 0: left of the crosshair */
        p = atan2f(y - 11.0f, h) * (32768.0f / 3.14159265f);
        k = p > 4096 ? 4 : p < -4096 || y < 6.0f ? 8 : 2;   /* its aim height, as aim_at_crosshair */
        if (!(arm = pc_dual_arm(i, aim_e, prev, dy, p - plp->wax)) || (cls && k != cls)) continue;   /* (not the crosshair's enemy) */
        {   /* in sight of the arm's muzzle (the game's test from the player's head) */
            const float* m = arm == 2 ? l : r;
            cap.c1.x = m[0]; cap.c1.y = m[1]; cap.c1.z = m[2];
            cap.c2.x = plp->px + x; cap.c2.y = plp->py + y; cap.c2.z = plp->pz + z;
            cap.r = 0.1f;
            apos = sys->apos; ahtp = sys->ahtp;
            k = bhCheckC2WallN(&cap, 0x40000400);
            sys->apos = apos; sys->ahtp = ahtp;
            if (k) continue;
        }
        if (prec) {   /* precise hits: only an enemy that arm's own shot, turned to it, hits (CheckGunHit: from the muzzle at
                         chest height along Rx(wax) Ry(ay + the arm's yaw)) before another enemy or a wall */
            const float* m = arm == 2 ? l : r;
            float tt[3] = { plp->px + x, plp->py + y, plp->pz + z }, o[3] = { m[0], plp->py + 11.0f, m[2] }, te, tn = -1, tw = -1, tq;
            NJS_MATRIX mx;
            NJS_VECTOR v = { 0, 0, -1 }, dv;
            NJS_LINE line;
            int ya = (short)(plp->ay + arm_yaw(m, tt)), j, n;
            njUnitMatrix(&mx);
            njRotateXYZ(&mx, plp->wax, ya, 0);
            njCalcVector(&mx, &v, &dv);
            if ((te = body_ray(e, o, &dv.x, 0, &j, &n)) < 0) continue;
            for (int q = 0; q < sys->ewk_n; q++)
                if (q != i && aimable(&ene[q]) && (tq = body_ray(&ene[q], o, &dv.x, 0, &j, &n)) >= 0 && (tn < 0 || tq < tn)) tn = tq;
            line.px = o[0]; line.py = o[1]; line.pz = o[2]; line.vx = dv.x * te; line.vy = dv.y * te; line.vz = dv.z * te;
            apos = sys->apos; ahtp = sys->ahtp;
            if (bhCheckL2Wall(&line, 0x400, &tq)) tw = tq;
            sys->apos = apos; sys->ahtp = ahtp;
            if (!pc_dual_take(te, tn, tw)) continue;
            dual_yaw = ya;
        }
        best = d; dual_e = i; left = arm == 2; keep = i == prev;
        t[0] = plp->px + x; t[1] = plp->py + y; t[2] = plp->pz + z;
    }
    if (dual_e < 0) t[0] = aim_t.x, t[1] = aim_t.y, t[2] = aim_t.z;
    pp->arm_dr = arm_yaw(r, left ? &aim_t.x : t);
    pp->arm_dl = arm_yaw(l, left ? t : &aim_t.x);
    pp->count = 3;                         /* PlyPchMain: the arms a third of the way there per tick */
    pp->hed_rate = left || dual_e < 0 ? 65536 : 0;   /* the head follows the arm at the crosshair (PlyPchMain: 65536 the right) */
}

/* RECVX_TPS_TRACE, dual weapons: each arm's shot (the right one when gun_delay is set, the left two ticks
   later) with its yaw, its enemy and the enemies' health */
static void trace_dual(void)
{
    static int last = -1;
    const PP_WORK* pp = (const PP_WORK*)plp->exp2;
    int arm = pp->gun_delay == 1 && last != 1 ? 'R' : pp->gun_delay == -1 && last == 0 ? 'L' : 0;
    char hp[256];
    int n = 0;
    last = pp->gun_delay;
    if (!arm) return;
    for (int i = 0; i < sys->ewk_n && n < (int)sizeof(hp) - 24; i++)
        if ((ene[i].flg & 1) && (ene[i].flg & 0x20)) {   /* health, degrees off the facing (> 0 left), distance */
            const NJS_CAPSULE* c = &ene[i].watr;
            float x = (c->c1.x + c->c2.x) / 2 - plp->px, z = (c->c1.z + c->c2.z) / 2 - plp->pz;
            n += snprintf(hp + n, sizeof(hp) - n, " %d:%d@%.0f/%.0f", i, ene[i].hp,
                          wrap(atan2f(-x, -z) * (32768.0f / 3.14159265f) - (short)plp->ay) * (360.0f / 65536.0f), sqrtf(x * x + z * z));
        }
    printf("tps: DUAL %c shot wpn %d ay %.1f wax %.1f arm_dr %.1f arm_dl %.1f (shown %.1f %.1f) crosshair enemy %d (%s arm) other %d, aim height %d, hp%s\n",
           arm, plp->wpnr_no, (short)plp->ay * (360.0f / 65536.0f), plp->wax * (360.0f / 65536.0f), pp->arm_dr * (360.0f / 65536.0f),
           pp->arm_dl * (360.0f / 65536.0f), pp->arm_ar * (360.0f / 65536.0f), pp->arm_al * (360.0f / 65536.0f), aim_e,
           pp->hed_rate ? "right" : "left", dual_e, plp->at_flg & 0xE, hp);
}

/* how far the barrel of the weapon in hand matrix h (along the hand's x axis from the muzzle
   wp_fps1) passes from the crosshair target, and the angle that makes from the camera */
static float barrel_miss(const float* h, float* deg)
{
    const NJS_POINT3* f = &WpnTab[plp->wpnr_no].wp_fps1;
    float v[3], l = sqrtf(h[0] * h[0] + h[1] * h[1] + h[2] * h[2]), k = 0, q = 0, de = 0;
    int j;
    for (j = 0; j < 3; j++) {
        v[j] = (&aim_t.x)[j] - (f->x * h[j] + f->y * h[4 + j] + f->z * h[8 + j] + h[12 + j]);
        k += v[j] * h[j] / l;
        de += ((&aim_t.x)[j] - (&cam.wpx)[j]) * ((&aim_t.x)[j] - (&cam.wpx)[j]);
    }
    for (j = 0; j < 3; j++) q += (v[j] - h[j] / l * k) * (v[j] - h[j] / l * k);
    *deg = atan2f(sqrtf(q), sqrtf(de)) * (180.0f / 3.14159265f);
    return sqrtf(q);
}

/* RECVX_TPS_TRACE (actor_glue.c, after our model is posed): the weapon as drawn (the model's hand)
   and in the real hand, against the crosshair target */
void tps_trace_hands(const float* drawn, const float* real)
{
    float d0, d1, m0, m1;
    if (!trace || !g_tps.aiming || lock_aim() || plp->mode1 != 1) return;
    m0 = barrel_miss(drawn, &d0);
    m1 = barrel_miss(real, &d1);
    printf("tps: drawn weapon vs crosshair target %.1f away: its barrel passes %.2f off (%.2f deg), the real hand's %.2f (%.2f deg); drawn hand %+.2f above the real\n",
           sqrtf((aim_t.x - plp->px) * (aim_t.x - plp->px) + (aim_t.z - plp->pz) * (aim_t.z - plp->pz)), m0, d0, m1, d1, drawn[13] - real[13]);
}

/* RECVX_TPS_TRACE: a shot fired this tick (the ammo went down), as bhCPM2 cast it: the enemy test
   from o = (px, py + 11, pz) + forward * (3 + r), the wall test from the muzzle at the hand
   (owP[9], still the matrices the fire code read), both along Rx(wax) Ry(ay); where the wall ray
   ends against the crosshair target */
static int lock = -1;   /* RECVX_TPS_TRACE=2: the enemy the game locked on to (tps_player) */
static void trace_shot(void)
{
    static unsigned last = 0xFFFFFFFF;
    unsigned ammo = sys->itm[sys->ply_id * 16 + (sys->itm[sys->ply_id * 16] & 15)] & 0xFFFF;
    int shot = ammo < last && last != 0xFFFFFFFF;
    last = ammo;
    if (!shot) return;
    if (lock >= 0 && lock < sys->ewk_n && ene[lock].cpcl && ene[lock].mlwP && (ene[lock].flg & 3) == 1) {
        /* how far the crosshair's ray (the one this shot was aimed by) passed from that enemy's body: its volume nearest
           the ray, the gap between them (< 0: through it) */
        const BH_PWORK* e = &ene[lock];
        const CPCL* c = e->cpcl;
        float a[3], b[3], r, m, best = 1e9f, q[3], ab[3], u, gap;
        int n, k, bn = -1;
        for (n = 0; c->cap_r; n++) {
            volume(e, &c, a, b, &r);
            pc_ray_capsule(xr, xr + 3, a, b, 0, &m);
            for (k = 0, u = 0, gap = 0; k < 3; k++) q[k] = xr[k] + xr[3 + k] * m - a[k], ab[k] = b[k] - a[k], u += q[k] * ab[k], gap += ab[k] * ab[k];
            u = gap > 0 ? u / gap : 0;
            u = u < 0 ? 0 : u > 1 ? 1 : u;
            for (k = 0, gap = 0; k < 3; k++) gap += (q[k] - ab[k] * u) * (q[k] - ab[k] * u);
            gap = sqrtf(gap) - r;
            if (gap < best) best = gap, bn = n;
        }
        printf("tps: SHOT at enemy %d %.1f away: the crosshair passes %.2f from its body (volume %d)\n", lock,
               sqrtf((e->px - plp->px) * (e->px - plp->px) + (e->pz - plp->pz) * (e->pz - plp->pz)), best, bn);
    }
    {
        const float* h = plp->mlwP->owP[9].mtx;
        float z = WpnTab[plp->wpnr_no].wp_fps1.z, l = WpnTab[plp->wpnr_no].l, r = WpnTab[plp->wpnr_no].r, len, g[3], o[3], w[3], miss = 0, de = 0, ang;
        NJS_MATRIX mx;
        NJS_VECTOR v = { 0, 0, -l }, vv;
        NJS_LINE line;
        NJS_POINT3 apos = sys->apos;
        ATR_WORK* ahtp = sys->ahtp;
        int j, hit;
        for (j = 0; j < 3; j++) g[j] = h[8 + j] * z + h[12 + j];
        o[0] = plp->px - sinf(RAD(plp->ay)) * (3.0f + r); o[1] = plp->py + 11.0f; o[2] = plp->pz - cosf(RAD(plp->ay)) * (3.0f + r);
        njUnitMatrix(&mx);
        njRotateXYZ(&mx, plp->wax, plp->ay, 0);
        njCalcVector(&mx, &v, &vv);
        line.px = g[0]; line.py = g[1]; line.pz = g[2]; line.vx = vv.x; line.vy = vv.y; line.vz = vv.z;
        hit = bhCheckL2Wall(&line, 0x400, &len);
        if (!hit) len = l;
        for (j = 0; j < 3; j++) {
            w[j] = g[j] + (&vv.x)[j] * len / l;
            miss += (w[j] - (&aim_t.x)[j]) * (w[j] - (&aim_t.x)[j]);
            de += ((&aim_t.x)[j] - (&cam.wpx)[j]) * ((&aim_t.x)[j] - (&cam.wpx)[j]);
        }
        sys->apos = apos; sys->ahtp = ahtp;
        miss = sqrtf(miss);
        ang = atan2f(miss, sqrtf(de)) * (180.0f / 3.14159265f);
        printf("tps: SHOT wpn %d muzzle (%.2f %.2f %.2f) = o + (%.2f %.2f %.2f), dir (%.3f %.3f %.3f); wall hit %d at (%.2f %.2f %.2f), "
               "crosshair target (%.2f %.2f %.2f): misses by %.2f (%.2f deg from the camera), height %+.2f\n",
               plp->wpnr_no, g[0], g[1], g[2], g[0] - o[0], g[1] - o[1], g[2] - o[2], vv.x / l, vv.y / l, vv.z / l, hit, w[0], w[1], w[2],
               aim_t.x, aim_t.y, aim_t.z, miss, ang, w[1] - aim_t.y);
    }
}

/* the scope of the sniper rifle and linear launcher (playpch2.c bhCPM2_act_scp, mode2 71: first
   person, our camera off; the game's view is cam.ay = -ay, cam.ax = -wax, and the shot leaves along
   them from the same point). The mouse (and the Modern gamepad's right stick) turns ay and wax as the
   game's stick does, at the aiming sensitivity scaled by the zoom (the field of view shown against the aiming one), wax within the
   game's +-45 degrees. During a shot's recoil (mode3 3) the game sets wax to ct3 plus the kick each
   frame: the mouse moves ct3. */
#define SCOPE_PITCH 8192   /* bhCPM2_act_scp's clamp */

/* test aid (RECVX_TPS_TRACE=2): the point of the locked enemy RECVX_TPS_AIM_AT names, seen from s: "f" along its hit
   capsule (0 c1 .. 1 c2, default 0.5), "cN" the middle of volume N of its table; ":side:up" that many units beside it
   (right of the view from s) and above */
static int aim_aid_on(void) { return trace > 1 && lock >= 0 && lock < sys->ewk_n && (ene[lock].flg & 3) == 1; }
static void aim_aid(const float* s, float* p)
{
    const NJS_CAPSULE* c = &ene[lock].watr;
    const char* at = getenv("RECVX_TPS_AIM_AT");
    float f = at && *at != 'c' ? (float)atof(at) : 0.5f, a[3], b[3], r, side = 0, up = 0, dx, dz, h;
    for (int k = 0; k < 3; k++) p[k] = (&c->c1.x)[k] + ((&c->c2.x)[k] - (&c->c1.x)[k]) * f;
    if (at && *at == 'c' && ene[lock].cpcl && ene[lock].mlwP) {
        const CPCL* v = ene[lock].cpcl;
        for (int n = atoi(at + 1); v->cap_r && n >= 0; n--) {
            volume(&ene[lock], &v, a, b, &r);
            for (int k = 0; k < 3; k++) p[k] = (a[k] + b[k]) / 2;
        }
    }
    if (at && (at = strchr(at, ':'))) {
        side = (float)atof(at + 1);
        if ((at = strchr(at + 1, ':'))) up = (float)atof(at + 1);
    }
    dx = p[0] - s[0]; dz = p[2] - s[2]; h = sqrtf(dx * dx + dz * dz);
    if (h > 0) p[0] -= dz / h * side, p[2] += dx / h * side;
    p[1] += up;
}

static void scope_look(void)
{
    static float fy, fp;   /* the turn below one game unit, kept for the next frame */
    float z = cam.pers / DEG((float)g_tps.aim_fov), k = TURN_PER_PIXEL * g_tps.aim_sens / 100.0f * z;
    int dy, dp, p;
    fy -= mouse_dx * k * g_tps.sens_x / 100.0f;
    fp += (g_tps.invert_y ? mouse_dy : -mouse_dy) * k * g_tps.sens_y / 100.0f;
    /* the gamepad's right stick (pad_sticks: its speed and invert Y) at the stick aiming speed, likewise scaled */
    fy += stick_yaw * g_tps.pad_aim_sens / 100.0f * z;
    fp += stick_pitch * g_tps.pad_aim_sens / 100.0f * z;
    mouse_dx = mouse_dy = stick_yaw = stick_pitch = 0;
    dy = (int)fy; fy -= dy;
    dp = (int)fp; fp -= dp;
    plp->ay += dy;
    p = (plp->mode3 == 3 ? plp->ct3 : plp->wax) + dp;
    p = p > SCOPE_PITCH ? SCOPE_PITCH : p < -SCOPE_PITCH ? -SCOPE_PITCH : p;
    if (plp->mode3 == 3) plp->ct3 = p;
    else plp->wax = plp->waxp = p;
    if (aim_aid_on()) {   /* test aid: the scope's centre (the shot from the head, playpch2.c) on RECVX_TPS_AIM_AT */
        const float* m = plp->mlwP->owP[5].mtx;
        float o[3], t[3];
        for (int k = 0; k < 3; k++) o[k] = 1.5f * m[4 + k] - m[8 + k] + m[12 + k];
        aim_aid(o, t);
        plp->ay = (int)(atan2f(-(t[0] - o[0]), -(t[2] - o[2])) * (32768.0f / 3.14159265f));
        p = (int)(atan2f(t[1] - o[1], sqrtf((t[0] - o[0]) * (t[0] - o[0]) + (t[2] - o[2]) * (t[2] - o[2]))) * (32768.0f / 3.14159265f));
        if (plp->mode3 == 3) plp->ct3 = p;
        else plp->wax = plp->waxp = p;
    }
    if (trace) {   /* and the live enemy nearest the scope's centre: its hit capsule's middle seen from the eye
                      (cam.p*: the scope's camera last drawn), degrees off the centre */
        float best = -1, oy = 0, op = 0, hp = 0;
        int near = -1;
        for (int i = 0; i < sys->ewk_n; i++) {
            const NJS_CAPSULE* c = &ene[i].watr;
            float x = (c->c1.x + c->c2.x) / 2 - cam.px, y = (c->c1.y + c->c2.y) / 2 - cam.py, z = (c->c1.z + c->c2.z) / 2 - cam.pz, ey, ep;
            if (!(ene[i].flg & 1)) continue;
            hp += ene[i].hp;
            ey = wrap(atan2f(-x, -z) * (32768.0f / 3.14159265f) - plp->ay);
            ep = atan2f(y, sqrtf(x * x + z * z)) * (32768.0f / 3.14159265f) - plp->wax;
            if (best < 0 || ey * ey + ep * ep < best) { best = ey * ey + ep * ep; oy = ey; op = ep; near = i; }
        }
        printf("tps: scope frame %u wpn %d mode3 %d ay %.2f wax %.2f fov %.1f (turn %d %d) enemy %d %.2f %.2f deg off, enemy hp %.0f\n",
               (unsigned)gs_frame_count(), plp->wpnr_no, plp->mode3, (short)plp->ay * (360.0f / 65536.0f), plp->wax * (360.0f / 65536.0f),
               cam.pers * (360.0f / 65536.0f), dy, dp, near, oy * (360.0f / 65536.0f), op * (360.0f / 65536.0f), hp);
    }
}

/* player.c bhControlPlayer, right after the mode handler (before the auto-turn, the motion
   and bhCalcModel): mouse X turns the character, or the camera while aiming freely */
void tps_player(void)
{
    static float frac;
    float d;
    static int dual_gen;
    int n, dual_prev = dual_gen == enemy_room_gen ? dual_e : -1;
    dual_e = -1;   /* kept only from one tick of dual_aim to the next in a room: not across a gap or another weapon */
    dual_gen = enemy_room_gen;
    ticks++;   /* (pc_precise_hit: the crosshair's ray is this tick's only if aim_at_crosshair casts it below) */
    if (trace && tps_cam_active && plp->mode1 == 1) trace_shot();
    if (trace) {   /* a zombie's head burst (en01.c: exp0 + 0x78 set to -1) */
        static int head[PC_ENE_SLOTS];
        for (int i = 0; i < sys->ewk_n && i < PC_ENE_SLOTS; i++) {
            int h = ene[i].id == 1 && (ene[i].flg & 1) && ene[i].exp0 ? *(int*)(ene[i].exp0 + 0x78) : 0;
            if (h == -1 && head[i] != -1) printf("tps: HEAD BURST enemy %d (last hit joint %d, weapon %d)\n", i, ene[i].djnt_no, ene[i].wpnr_no);
            head[i] = h;
        }
    }
    /* (the scope's first tick still has our camera: the free aim below points the scope at the crosshair's target) */
    if (!tps_cam_active && plp->mode0 == 1 && plp->mode1 == 1 && plp->mode2 == 71) { scope_look(); return; }
    if (!tps_cam_active || plp->mode0 != 1 || (plp->mode1 == 0 && plp->mode2 > 13) || reload_at_rest()) { frac = 0; return; }
    if (trace && plp->mode1 == 1)
        printf("tps: weapon mode %d/%d motion %d frame %d/%d at (%.1f %.1f) ay %.1f hand y %.1f\n", plp->mode2, plp->mode3, plp->mtn_no, plp->frm_no >> 16,
               plp->mnwP ? plp->mnwP[plp->mtn_no].frm_num : -1, plp->px, plp->pz, (short)plp->ay * (360.0f / 65536.0f), plp->mlwP->owP[9].mtx[13] - plp->py);
    if (plp->mode1 == 1 && !lock_aim()) {
        float dy = stick_yaw * g_tps.pad_aim_sens / 100.0f;
        stick_pitch *= g_tps.pad_aim_sens / 100.0f;
        if (pad_cam && g_tps.auto_aim) assist(&dy, &stick_pitch);
        yaw = wrap(yaw - mouse_dx * TURN_PER_PIXEL * g_tps.sens_x / 100.0f * g_tps.aim_sens / 100.0f + dy);
        mouse_dx = stick_yaw = 0;
        look(g_tps.aim_sens / 100.0f, 1.0f);
        /* test aid (RECVX_TPS_TRACE=2): follow the enemy the game locked on to (lock, file scope: trace_shot) */
        if (plp->flg & 0x100000)   /* (dual weapons: the nearest enemy of their lock-on, playpch.c SetLockOnDirection) */
            lock = (WpnTab[plp->wpnr_no].flg & 0x800) && plp->exp2 ? (int)(((PP_WORK*)plp->exp2)->LckTbl[0] - ene) : plp->src_no;
        if (aim_aid_on()) {   /* at RECVX_TPS_AIM_AT */
            NJS_POINT3 eye, dir, sh;
            float dx, dy, dz, p[3];
            view(&eye, &dir, &sh);
            aim_aid(&sh.x, p);
            dx = p[0] - sh.x; dy = p[1] - sh.y; dz = p[2] - sh.z;
            yaw = atan2f(-dx, -dz) * (32768.0f / 3.14159265f);
            pitch = atan2f(dy, sqrtf(dx * dx + dz * dz)) * (32768.0f / 3.14159265f);
            look(0, 0);   /* within the mouse's pitch range */
        }
        if (plp->flg & 0x100000) {   /* no lock-on turn (bhCPM2_act_suw / next target) */
            plp->flg &= ~0x100000;
            plp->stflg &= ~0x800;
        }
        {
            int ay0 = plp->ay;
            aim_at_crosshair();
            if ((WpnTab[plp->wpnr_no].flg & 0x800) && plp->exp2) {   /* dual weapons; lowering them: the game's */
                if (plp->mode2 != 68) dual_aim(ay0, dual_prev);
                if (trace) trace_dual();
            }
        }
        return;
    }
    if (pad_cam && plp->mode1 == 0) {   /* gamepad: towards the left stick's direction from the camera */
        if (pad_move && plp->mode2 != 12 && !(plp->flg & 0x100000)) plp->ay += (int)modern_turn((short)plp->ay, pad_target);
        pad_move = 0;
        return;
    }
    if (g_tps.free_cam) {
        static int was_aiming;
        int aim = plp->mode1 == 1;
        /* game auto-aim: raised towards the camera unless locked on, then as B */
        if (aim && !was_aiming && !(plp->flg & 0x100000)) plp->ay += (short)((int)yaw - plp->ay);
        was_aiming = aim;
        if (!aim) {   /* walk towards the keys' direction seen from the camera; not during the game's own turns (quick turn) */
            float f = !!(move_keys & TPS_K_FWD) - !!(move_keys & TPS_K_BACK), r = !!(move_keys & TPS_K_RIGHT) - !!(move_keys & TPS_K_LEFT);
            if ((f != 0 || r != 0) && !(plp->flg & 0x100000)) {
                d = wrap(yaw - atan2f(r, f) * (32768.0f / 3.14159265f) - (short)plp->ay);
                plp->ay += (int)(d * 0.3f);   /* 95% in 9 game frames */
            }
            return;
        }
    }
    d = -mouse_dx * TURN_PER_PIXEL * g_tps.sens_x / 100.0f + frac;
    mouse_dx = 0;
    n = (int)d;
    frac = d - n;
    plp->ay += n;
    yaw += n;
}

/* turn a node's world matrix (rows = its axes and origin in world space, as njCalcPoint reads
   them) by angle `a` (radians) about the unit axis u through the point c */
static void turn(float* m, const float* u, const float* c, float a)
{
    float cs = cosf(a), sn = sinf(a);
    for (int r = 0; r < 4; r++) {
        float v[3], k;
        for (int j = 0; j < 3; j++) v[j] = m[r * 4 + j] - (r == 3 ? c[j] : 0);
        k = (u[0] * v[0] + u[1] * v[1] + u[2] * v[2]) * (1 - cs);   /* Rodrigues */
        m[r * 4 + 0] = v[0] * cs + (u[1] * v[2] - u[2] * v[1]) * sn + u[0] * k + (r == 3 ? c[0] : 0);
        m[r * 4 + 1] = v[1] * cs + (u[2] * v[0] - u[0] * v[2]) * sn + u[1] * k + (r == 3 ? c[1] : 0);
        m[r * 4 + 2] = v[2] * cs + (u[0] * v[1] - u[1] * v[0]) * sn + u[2] * k + (r == 3 ? c[2] : 0);
    }
}

/* player.c bhControlPlayer, in place of bhCalcModel(plp): the game has three aim poses
   (straight, 45 degrees up/down). Free aim keeps the straight one and pitches spine and
   chest (nodes 2 and 3, half each; arms, head and weapon follow) about the character's
   sideways axis so that the weapon points at the crosshair target, for every weapon and
   character. The motion state (node angles) is untouched: the two nodes' world matrices are
   turned and the tree below them recomputed from them (O_WORK flg 1). */

/* nodes 2 and 3 of p turned by a each about p's sideways axis, the tree below recomputed */
static void bend(BH_PWORK* p, float a)
{
    O_WORK* ow = p->mlwP->owP;
    float u[3], l, c[3];
    l = sqrtf((*p->mtx)[0] * (*p->mtx)[0] + (*p->mtx)[1] * (*p->mtx)[1] + (*p->mtx)[2] * (*p->mtx)[2]);
    for (int j = 0; j < 3; j++) { u[j] = (*p->mtx)[j] / l; c[j] = ow[2].mtx[12 + j]; }   /* the model's x axis */
    turn(ow[2].mtx, u, c, a);
    turn(ow[3].mtx, u, c, a);
    for (int j = 0; j < 3; j++) c[j] = ow[3].mtx[12 + j];
    turn(ow[3].mtx, u, c, a);
    ow[2].flg |= 1; ow[3].flg |= 1;
    bhCalcModel(p);
    ow[2].flg &= ~1; ow[3].flg &= ~1;
}

void tps_calc_model(void)
{
    static float w;   /* eased in/out when the weapon is raised/lowered */
    O_WORK* ow = plp->mlwP->owP;
    float a, dx, dz, ofs[3];
    int on = tps_cam_active && !lock_aim() && plp->mode0 == 1 && plp->mode1 == 1 && plp->mode2 != 68 && !reload_at_rest();

    bhCalcModel(plp);
    bend_a = 0;
    w += (on - w) * 0.35f;
    if (w < 0.01f || plp->mlwP->obj_num <= 9) { aim_corr = 0; return; }
    /* pitch from the weapon hand of the straight pose to the target (not wax: the shot starts
       lower, at 11 units, and needs a steeper angle for close targets); with "You appear as" the
       hand where the model holds the weapon (a Tyrant's is higher: its weapon pointed over the
       crosshair target) */
    actor_aim_offset(plp, ofs);
    dx = aim_t.x - ow[9].mtx[12] - ofs[0]; dz = aim_t.z - ow[9].mtx[14] - ofs[2];
    a = atan2f(aim_t.y - ow[9].mtx[13] - ofs[1], sqrtf(dx * dx + dz * dz));
    if ((WpnTab[plp->wpnr_no].flg & 0x40000000) && a > 0) a = 0;   /* knife: level at targets above (an enemy
                                                                       right in front raised the slash to its head), still bends down */
    bend_a = a = w * (a > 1.047f ? 1.047f : a < -1.047f ? -1.047f : a) / 2;   /* at most 60 degrees */
    bend(plp, a);
    {   /* "You appear as" another model (the retargeting): the weapon is drawn in the model's hand, which
           the bend swings about the chest as well, farther the taller the model, and in a gender-matched
           pose the hand turns the weapon as that motion does. The pitch is solved on the weapon as it will
           be drawn: its barrel through the crosshair target. The barrel runs from the muzzle (wp_fps1) on to
           wp_fps2 where that point lies beyond it, else along the hand's x axis: the grenade launcher's runs at
           45 degrees to that axis (both its points on the barrel), and solved on the x axis it was held 44
           degrees up (the other weapons' second point is the muzzle, on the x axis or unset).
           actor_aim_hand is the hand as drawn last tick, in the frame of the chest now, so it is solved
           only while the aim pose stands still (weapon raised, mode2 65, no motion blend) and not for the
           knife (no barrel). Raising, shots, recoil and slashes keep the last correction, which is eased:
           solved on a moving hand the upper body jumped back and forth. */
        static int still;   /* ticks the aim pose has stood still (the hand read is last tick's) */
        float h[16], t0 = a * 2 / w, t = t0, goal = aim_corr, pt = t, pe = 1e9f;
        int it, steady;
        still = plp->mode2 == 65 && !plp->hokan_count && !(WpnTab[plp->wpnr_no].flg & 0x40000000) ? still + 1 : 0;
        steady = still >= 2;
        if (!actor_aim_hand(plp, h)) { aim_corr = 0; return; }   /* no model: the hand's pitch */
        const NJS_POINT3 *f = &WpnTab[plp->wpnr_no].wp_fps1, *f2 = &WpnTab[plp->wpnr_no].wp_fps2;
        float ax[3] = { f2->x - f->x, f2->y - f->y, f2->z - f->z }, l = sqrtf(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
        if (l < 0.01f || ax[0] * f->x + ax[1] * f->y + ax[2] * f->z <= 0) ax[0] = 1, ax[1] = ax[2] = 0;   /* no second point
            beyond the muzzle (the same, unset, or behind it: the rocket launcher's back end): the x axis */
        else for (int j = 0; j < 3; j++) ax[j] /= l;
        for (it = 0; steady && it < 4 && (it == 0 || actor_aim_hand(plp, h)); it++) {
            float m[3], b[3], e, sg;
            for (int j = 0; j < 3; j++) {
                m[j] = f->x * h[j] + f->y * h[4 + j] + f->z * h[8 + j] + h[12 + j];
                b[j] = ax[0] * h[j] + ax[1] * h[4 + j] + ax[2] * h[8 + j];
            }
            dx = aim_t.x - m[0]; dz = aim_t.z - m[2];
            sg = dx * b[0] + (aim_t.y - m[1]) * b[1] + dz * b[2] < 0 ? -1.0f : 1.0f;   /* (the barrel's way along the axis) */
            e = atan2f(aim_t.y - m[1], sqrtf(dx * dx + dz * dz)) - atan2f(sg * b[1], sqrtf(b[0] * b[0] + b[2] * b[2]));
            if (fabsf(e) < 0.002f) break;
            if (fabsf(e) >= pe) { t = pt; break; }   /* not converging (a target a few units from the muzzle: each step
                moves the muzzle as much as the line; the solve swung to 50 degrees and the upper body jumped
                when an enemy stepped into the crosshair): the best step so far */
            pe = fabsf(e); pt = t;
            t += e;
            t = t > 1.047f ? 1.047f : t < -1.047f ? -1.047f : t;
            bhCalcModel(plp);   /* (unbent) */
            bend(plp, w * t / 2);
        }
        if (steady) goal = t - t0;
        aim_corr += (goal - aim_corr) * 0.35f;
        t = t0 + aim_corr;
        t = t > 1.047f ? 1.047f : t < -1.047f ? -1.047f : t;
        bhCalcModel(plp);   /* (unbent) */
        bend_a = w * t / 2;
        bend(plp, bend_a);
    }
}

/* the pitch our upper body is bent by this tick (radians, 0: none): the co-op pose sends it */
float tps_aim_pitch(void)
{
    return tps_on ? bend_a * 2 : 0;
}

/* that bend (pitch radians) on another model posed this tick: a display copy of the player (actor_glue.c: a
   gender-matched pose; its base matrix is the player's), the co-op partner with the pitch its pose sent */
void tps_bend(BH_PWORK* p, float pitch)
{
    if (pitch != 0 && p->mlwP->obj_num > 9) bend(p, pitch / 2);
}

/* RECVX_EFF_TRACE=1 (test aid): the live effects at the frame hand-off - id.type, an
   asterisk if queued for drawing this frame, and the view-space z of their position */
static void eff_trace(void)
{
    char buf[2048];
    int len = 0, alive = 0;
    if (!sys || !plp) return;
    O_WRK** lists[] = { sys->ef_pol, sys->ef_mdf, sys->ef_lin, sys->ef_ntx, sys->ef_trs, sys->ef_pnc, sys->ef_opq,
                        sys->ef_thl, sys->ef_ntx2d, sys->ef_trs2d, sys->ef_pnc2d, sys->ef_fnc };
    int* n = &sys->ef_poln;   /* the 12 list counts, in the order of the lists */
    for (int i = 0; i < 512; i++) {
        O_WRK* op = &eff[i];
        int drawn = 0;
        NJS_POINT3 v;
        if (!(op->flg & 1) || op->id == 0) continue;
        for (int l = 0; l < 12 && !drawn; l++)
            for (int k = 0; k < n[l] && !drawn; k++) drawn = lists[l][k] == op;
        njCalcPoint(cam.mtx, (NJS_POINT3*)&op->px, &v);
        alive++;
        if (len < (int)sizeof(buf) - 40)
            len += snprintf(buf + len, sizeof(buf) - len, " %d.%d@%d%s%s(%.0f)", op->id, op->type, i, drawn ? "*" : "",
                            (op->flg & 0x1000000) || (op->stflg & 0x1000000) ? "h" : "", v.z);
    }
    if (alive) printf("eff: gfrm %d cut %d tps %d sp %x:%s\n", sys->gfrm_ct, cam.ncut, tps_cam_active, sys->sp_flg, buf);
}

/* frame hand-off (libgraph.c), game thread */
void tps_frame(void)
{
    static int efftrace = -1;
    if (efftrace < 0) efftrace = getenv("RECVX_EFF_TRACE") != NULL;
    if (efftrace) eff_trace();
    int on = g_tps.on;
    static int init;
    if (!init++) trace = getenv("RECVX_TPS_TRACE") ? atoi(getenv("RECVX_TPS_TRACE")) : 0;
    tps_take_mouse(&mouse_dx, &mouse_dy);
    mouse_moved = mouse_dx != 0 || mouse_dy != 0;
    if (tps_on && !on) deactivate();
    tps_on = on;
    /* the gamepad's third-person rows play while our camera is, the game not frozen (menus,
       messages, pick-ups, their fades), through hits, grabs and weapon loads (a held trigger or
       quick knife stays held) */
    g_tps.kb_play = on && pc_in_game() && !pc_options_up();
    g_tps.pad_play = on && g_tps.pad_modern && (tps_cam_active || in_scope()) && sys && (sys->sp_flg & 0x40) && !(sys->ss_flg & 0x400000);
    /* with or without third person: the player has control, so no menu has the mouse (menu_mouse.c);
       the co-op trade screen is open in play */
    g_menu_mouse.play = pc_in_game() && plp && pc_in_play(plp, 1) && !pc_trade_screen();   /* (the scope too: its mouse aim) */
}
