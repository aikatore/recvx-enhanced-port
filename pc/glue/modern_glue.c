/* Modern controls for the fixed cameras (docs/USER_GUIDE.md "Modern controls"), as the
   alternate controls of the RE HD remasters: the left stick or the d-pad points where the
   character walks on screen. Each frame the direction becomes the game's own input - forward
   (and run when the stick is pushed far) - and the character turns towards it after the mode
   handler, so the player state machine, its motions, doors, stairs and pushing stay the game's.
   Aiming, the quick-turn row and third person keep the original input. Combined (the default)
   leaves the gamepad's d-pad to the game, Classic tank controls; the stick wins over it, the
   arrow keys stay modern. Game thread; the direction math is pc/src/input/modern.c. */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/player.h"
#include "../../decomp/include/ps2/veronica/prog/playpch.h"
#include "../src/host/modern.h"
#include "../src/host/tps.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

int pad_move_dir(float* x, float* y, int* dpad);   /* pad.c: screen direction (x right, y up), 1 = analog; dpad: the gamepad's d-pad */
int pc_in_play(const BH_PWORK* p, int scope);   /* reload_glue.c */
int pad_quickturn_held(void);
int pad_free_aim(void);
int pc_trade_screen(void);
uint64_t gs_frame_count(void);
void tps_aim_walk_fixed(int ok, float md, float m);   /* tps_glue.c: the walk under the aim, its legs and footsteps */

#define RUN_PUSH 0.7f                   /* the stick pushed this far runs */
#define MOVE_REBASE 10                  /* game frames after a special move in which a cut is not held */

static ModernHold hold;
static uint64_t move_end;               /* the last frame of a special move (stairs, ladder, climbing over, pushing) */
static float target;                    /* the angle to turn to this frame */
static int steer;                       /* modern_pad asked modern_player to turn */
static float walk_m, walk_dir;          /* "Move While Aiming (Fixed Cameras)": this frame's walk (0..1), its direction (as ay) */
static int walk_tank;                   /* ... or along the facing: 1 forward, -1 back (Classic) */

/* normal play: our player in control; no event, door, menu, first person or attract demo (reload_glue.c) */
static int in_play(void) { return pc_in_play(plp, 0); }

static void trace(float x, float y, int analog, int held_old, unsigned pad)
{
    static int on = -1;
    const float* m = (const float*)cam.mtx;
    float px, py, pz;
    if (on < 0) on = getenv("RECVX_MODERN_TRACE") != NULL;
    if (!on) return;
    /* the player's place on screen, x right, y up (camera space: x right, y down, z ahead) */
    px = plp->px * m[0] + plp->py * m[4] + plp->pz * m[8] + m[12];
    py = plp->px * m[1] + plp->py * m[5] + plp->pz * m[9] + m[13];
    pz = plp->px * m[2] + plp->py * m[6] + plp->pz * m[10] + m[14];
    printf("modern: frame %u dir %.2f %.2f%s cut %d%s target %.1f ay %.1f mode %d/%d/%d pad %x at (%.1f %.1f) view (%.3f %.3f) cam (%.0f %.0f %.0f) ay %.0f az %.0f\n",
           (unsigned)gs_frame_count(), x, y, analog ? " analog" : "", cam.ncut, held_old ? " (held)" : "",
           target * (360.0f / 65536.0f), (short)plp->ay * (360.0f / 65536.0f), plp->mode1, plp->mode2, plp->mode3, pad,
           plp->px, plp->pz, pz != 0 ? px / pz : 0, pz != 0 ? -py / pz : 0, cam.px, cam.py, cam.pz, cam.ay * (360.0f / 65536.0f), cam.az * (360.0f / 65536.0f));
}

/* "Move While Aiming (Fixed Cameras)" (docs/USER_GUIDE.md): the weapon raised (not lowering it, nor the scope), firing,
   slashing or reloading, in normal play; no hit or grab */
static int aim_walk_ok(void)
{
    return g_tps.aim_walk_fixed && !tps_on && sys && plp == &ply && in_play() && plp->mode1 == 1 && plp->mode2 != 68 && plp->mode2 != 71 &&
           !(plp->flg & 6) && !pc_trade_screen();
}

/* the directions walk under the aim (modern_player, tps_glue.c aim_walk), so they no longer turn or tilt it, except
   with run held: then the original aim controls (turn, aim up / down). Classic (and Combined's d-pad): up / down walk
   forward / back along the facing, left / right still turn. Modern: towards the direction on screen, the facing kept */
static unsigned int aim_walk_pad(unsigned int pad)
{
    float x, y, rx, rz;
    int dpad, analog = pad_move_dir(&x, &y, &dpad), cut;
    if (pad & 0x400) { hold.held = 0; return pad; }
    if (!g_modern_on || (g_modern_on == MODERN_COMBINED && dpad)) {
        hold.held = 0;
        walk_tank = !!(pad & 0x1) - !!(pad & 0x2);
        if (!walk_tank) return pad;
        walk_m = analog ? fabsf(y) : 1;
        return pad & ~0x63;
    }
    if (x == 0 && y == 0) { hold.held = 0; return pad; }
    rx = ((const float*)cam.mtx)[0]; rz = ((const float*)cam.mtx)[8];
    cut = cam.ncut | cam.camver << 8;
    modern_hold(&hold, atan2f(x, y) * (32768.0f / 3.14159265f), cut, &rx, &rz);
    walk_dir = modern_target(rx, rz, x, y);
    walk_tank = 0;
    walk_m = sqrtf(x * x + y * y);
    return pad & ~0x6F;
}

/* pad.c bhSetPad, before pad_ps is derived (virtual bits: 0x1 forward, 0x2 back, 0x4/0x8 turn,
   0x20/0x40 aim up/down, 0x400 run) */
unsigned int modern_pad(unsigned int pad)
{
    const float* m = (const float*)cam.mtx;
    float x, y, rx, rz, d;
    int analog, dpad, cut;
    steer = 0;
    walk_m = 0;
    if (m && aim_walk_ok()) return aim_walk_pad(pad);
    if (!g_modern_on || tps_on || !m || !sys || !plp || !in_play() || plp->mode1 != 0) {
        hold.held = 0;   /* aiming: the original input */
        return pad;
    }
    if (pad_quickturn_held()) {   /* the quick-turn row: back, then run, as without; the stick does not walk */
        hold.held = 0;
        return (pad & ~0x6F) | 0x2;
    }
    analog = pad_move_dir(&x, &y, &dpad);
    if ((x == 0 && y == 0) || (g_modern_on == MODERN_COMBINED && dpad)) { hold.held = 0; return pad; }   /* Combined: the d-pad as Classic */

    /* the camera on screen: its right axis (first column of the view matrix) on the ground */
    rx = m[0]; rz = m[8];
    cut = cam.ncut | cam.camver << 8;
    /* a special move steers itself, so a cut during it (or as control returns) is not held: back
       in control, the direction takes the camera on screen now */
    if (plp->mode2 > 13 || plp->mode2 == 12) move_end = gs_frame_count();
    else if (gs_frame_count() - move_end <= MOVE_REBASE && hold.cut != cut) hold.held = 0;
    modern_hold(&hold, atan2f(x, y) * (32768.0f / 3.14159265f), cut, &rx, &rz);
    target = modern_target(rx, rz, x, y);
    d = modern_wrap(target - (short)plp->ay);

    pad &= ~0x6F;
    if (plp->mode2 > 13 || plp->mode2 == 12) {   /* stairs, steps, pushing, ...: on while it points ahead */
        if (fabsf(d) < MODERN_DEG(90.0f)) pad |= 0x1;
    } else {
        if (fabsf(d) > MODERN_DEG(100.0f) && !(plp->flg & 0x100000)) pad |= d > 0 ? 0x4 : 0x8;   /* turn on the spot first */
        else pad |= 0x1;
        if (analog && x * x + y * y >= RUN_PUSH * RUN_PUSH) pad |= 0x400;
        steer = 1;
    }
    trace(x, y, analog, hold.cut != cut, pad);
    return pad;
}

/* player.c bhControlPlayer, after the mode handler: turn towards the direction; not in the
   game's own turns (the quick turn, auto-aim) or special moves. Free aim (the "Aim (no auto-aim)"
   row, pad.c; fixed cameras, any controls): the turn to a target that raising the weapon or
   "change aim" started (flg 0x100000: bhCPM2_act_suw, bhSearchNextEnemy; the dual weapons'
   SetLockOnDirection, whose arms then stay straight) is dropped before it begins, as third
   person does. The shot goes along the facing either way (bhCheckGunAtari). */
void modern_player(void)
{
    int s = steer;
    steer = 0;
    if (plp == &ply && !tps_on)   /* (each tick: the legs blend out after it) */
        tps_aim_walk_fixed(aim_walk_ok(), walk_tank ? (short)plp->ay + (walk_tank < 0 ? 32768.0f : 0) : walk_dir, walk_m);
    if (pad_free_aim() && plp == &ply && plp->mode1 == 1 && (plp->flg & 0x100000)) {
        plp->flg &= ~0x100000;
        plp->stflg &= ~0x800;
        if ((WpnTab[plp->wpnr_no].flg & 0x800) && plp->exp2) ((PP_WORK*)plp->exp2)->arm_dl = ((PP_WORK*)plp->exp2)->arm_dr = 0;
    }
    if (!s || plp != &ply || plp->mode0 != 1 || plp->mode1 != 0 || plp->mode2 > 13 || plp->mode2 == 12 || (plp->flg & 0x100000))
        return;
    plp->ay += (int)modern_turn((short)plp->ay, target);
}
