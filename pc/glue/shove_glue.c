/* "Break Free Pushes Enemies" (GAME SETUP > RULES, ini break_free; co-op: the host's, OPT2_SHOVE; docs/ENEMIES.md
 * "Breaking free"). Game thread.
 *
 * The game: only a zombie's grab pushes others away. When the player shakes a zombie off (en01.c MV16 mode3 3) the
 * zombie flies back, and every zombie or anatomist (ids 1, 26) it runs into while walking (mode0 1) falls down
 * (en01.c bhEne01_CollCheckPush: mode0 3, mode2 2/3/4). Nothing else reacts, and no other grabber pushes anything,
 * so with mixed enemies the next one grabbed or hit the player at once.
 *
 * With the rule: SHOVE_DELAY frames after a grabber lets our player go alive (pc_shove_release, where each grabber's
 * code ends the hold: the zombie's and anatomist's shove, the Cerberus's, Bandersnatch's and Black Widow's let-go), so
 * with the shaken-off zombie's flight and not when the player stands again (for any other hold: as the player comes
 * out of it, player mode0 4 to anything but death), every enemy within SHOVE_R of the player
 * that walks or stands (mode0 1) is pushed back with one of its own damage reactions, without damage. Only those
 * right at the player (closer than KNOCK_R) get the strong one and go down; the others stagger on their feet and slide
 * back a little (SLIDE_N, SLIDE_V), as the zombies the game's own push only staggers:
 *   zombie (1): stagger (en01 DG02); close: the game's own push (falls back or forward; from the side the stagger)
 *   anatomist (26): stagger (en26 DG02), as the game's push always does
 *   Cerberus (4): its hit knock-back (en04 DG00: slides away, rolls, gets up), its only reaction; no extra slide
 *   Hunter / Sweeper (5): its medium-hit stagger (en05 DG02/DG03, as bhEne05_InitDamage for a hit of 20 to 40);
 *                         close: its heavy hit (DG11 flies back and lies; from behind DG00; at a wall DG09; ledge DG12)
 *   Bandersnatch (9): its light hit (en09 DG02); close: its heavy hit (DG03 from the front, DG04 from behind)
 *   Black Widow (3): its reel (en03 DG01, a hit of 30 to 50); close: its heavy hit (DG03); on the floor only
 * Bosses, enemy variants, the other types, the records the room's scripts act on (enemy_rec_scripted), the dead,
 * the hidden, linked parts and enemies in any other mode (attacking, grabbing, in an event) are left alone.
 * There is no grab immunity afterwards (pc_grab_immune answers no): the push and stagger alone give the player room.
 * Co-op: each game watches its own player. An enemy the partner's game runs gets the push as EV_ENE_HIT with
 * COOP_HIT_SHOVE (forwarded, deferred like a shot), and its runner applies it.
 */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/hitchkl.h"
#include "../src/host/enemies.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SHOVE_R 20.0f       /* reach around the player: a shaken-off zombie flies ~9 units from ~6, its push reaches ~17 */
#define SHOVE_REACH 28.0f   /* ... an attacking enemy's (mode0 2): the Bandersnatch grabs with its arm from ~21 */
#define SHOVE_IMMUNE 75     /* game frames (30 a second) without a new grab once the player stands again: 2.5 s */
#define SHOVE_DELAY 15      /* release to push: the shaken-off zombie flies from its shove's frame 17 (en01.c NG00 mode3 3) */
#define SHOVE_LETGO 54      /* the longest let-go, release to standing (a Cerberus's; a zombie's 36): the partner's
                               immunity comes with its push, before it stands */
#define SHOVE_LATE (SHOVE_LETGO + 45)   /* an enemy busy at the push (its attack, the grabber's let-go) is pushed
                                           when it walks again within 3.3 s */
#define KNOCK_R 6.0f        /* closer than this it goes down: pressed against the player (zombies queue at 6.5-7.5,
                               a Bandersnatch reaches from 6.2) */
#define SLIDE_N 10          /* the others stagger and slide back for 10 frames ... */
#define SLIDE_V 1.2f        /* ... this far the first, less each frame: 6.6 units in all */

int NitenDir_ck(float hontai_x, float hontai_z, float target_x, float target_z);
int bhCdirCheck(int my_ang, int trg_ang);
int bhEne_DGDirCheck(BH_PWORK* epw);
int bhEne05_CheckLeaningWall(BH_PWORK* epw, int flg);
ATR_WORK* bhEne_CheckEventAtr(int flr_no, float px, float pz, int id, int min, int max);
float njSin(int a);
float njCos(int a);
int coop_shove_rule(void);              /* coop_glue.c: the rule in effect (the host's in co-op) */
int coop_shove(int slot, float x, float z, int marker);   /* coop_glue.c: 1 = sent to the partner's game, which runs it */
int coop_plp_partner(void);             /* coop_glue.c: plp is the partner's stand-in (a step of ours on the partner) */
int coop_runs(int slot);                /* coop_glue.c: our game runs that enemy */
extern int enemy_room_gen;              /* enemyopt_glue.c: room loads so far */

static int frame, mine, partner;        /* game frames; the immunity of our player / the partner's ends at that frame */
static int prev_m0 = -1, gen = -1, released, grab_at, push_at;   /* released: this hold's let-go seen; push_at: its push's frame */
static struct { int n; float dx, dz; } slide[PC_ENE_SLOTS];
static int trace = -1, trace_until;

#define X40(ep) (*(int*)((ep)->exp0 + 0x40))

int pc_shove_on(void) { return coop_shove_rule(); }

/* enemy code (en01, en03, en04, en09, en26), with plp the player it goes for: no new grab on it now */
int pc_grab_immune(void) { return 0; }   /* (no grab immunity after a push: the stagger alone is the window) */

void pc_shove_partner(void) { partner = frame + SHOVE_LETGO + SHOVE_IMMUNE; }   /* coop_glue.c: the partner was let go (it stands again later) */

/* the game's own zombie push (en01.c bhEne01_CollCheckPush), from the point (cx, cz); not knock: its stagger only */
static int push_zombie(BH_PWORK* ep, float cx, float cz, int knock)
{
    int t = ep->type, pa, fhit, ang, r1, r2;
    if (t == 2 || t == 3 || t == 6 || t == 7 || t == 9 || t == 10) return 0;   /* (crawlers, eaters: as the game) */
    if (X40(ep) & (0x400000 | 0x40000 | 0x200)) return 0;
    X40(ep) |= 0x200;
    pa = NitenDir_ck(ep->px, ep->pz, cx, cz);   /* the way a zombie facing the point would stand: the pushing one's */
    fhit = bhCdirCheck((unsigned short)(pa + 0x8000), ep->ay) == 0;
    if (fhit) X40(ep) |= 0x2000; else X40(ep) &= ~0x2000;
    ang = ((unsigned short)NitenDir_ck(ep->px, ep->pz, cx, cz) + ep->ay) & 0xFFFF;   /* (bhEne01_EnemyHitChk, 30 degrees) */
    r2 = (unsigned short)(ep->ay + 5461); r1 = (unsigned short)(ep->ay - 5461);
    if (knock && (r2 >= ang || ang >= r1)) {
        ep->dvx = -njSin(ep->ay); ep->dvz = -njCos(ep->ay); ep->dvy = 0.0f;
        ep->mode2 = ep->id == 26 ? 2 : fhit ? 4 : 3;
    } else ep->mode2 = 2;
    ep->mode0 = 3; ep->mode1 = 0; ep->mode3 = 0;
    return 1;
}

/* enemy ep (one our game runs, walking or standing) knocked back from (cx, cz) by its own reaction; 1 = done */
static int shove(BH_PWORK* ep, float cx, float cz)
{
    float dx = ep->px - cx, dz = ep->pz - cz, l = sqrtf(dx * dx + dz * dz);
    int front, knock = l < KNOCK_R, i = (int)(ep - ene);
    if (!(ep->flg & 0x1) || (ep->flg & 0x82) || ep->hp < 0 || ep->mode0 != 1 || !ep->exp0 || (ep->stflg & 0x41000000) || (ep->flg2 & 0x1)) return 0;
    if (l > SHOVE_REACH + 8.0f) return 0;   /* (it may have moved since; a co-op partner's view of it lags a little) */
    if ((pc_variant_n && pc_variant_is(ep)) || enemy_rec_scripted((int)(ep - ene))) return 0;
    if (l < 0.01f) { dx = njSin(ep->ay); dz = njCos(ep->ay); l = 1.0f; }
    if (ep->id == 1 || ep->id == 26) {
        if (!push_zombie(ep, cx, cz, knock)) return 0;
        goto slid;
    }
    ep->dvx = dx / l; ep->dvy = 0.0f; ep->dvz = dz / l;   /* the push's way, as a shot's (weapon.c: from the shooter) */
    front = bhEne_DGDirCheck(ep) == 0;                     /* (bhEne_CalcDamage: comb_flg 4) */
    switch (ep->id) {
    case 4: {   /* Cerberus: bhEne04_DmgChk / ChgDmgMode */
        int* x = (int*)(ep->exp0 + 0x10);
        if (ep->type != 0 || (*x & 0x100000)) return 0;   /* (another type: a scripted dog; 0x100000 its lying state) */
        ep->flg |= 0x40;
        if (front) *x |= 0x100; else *x &= ~0x100;
        ep->mode2 = (*x & 0x20000) ? 1 : 0;
        break;
    }
    case 5: {   /* Hunter, Sweeper: bhEne05_InitDamage for a medium hit (close: a heavy one) */
        int a = NitenDir_ck(ep->px, ep->pz, cx, cz);   /* (= its bhArcTan2(e - p): the way away from the point) */
        if (!(ep->flg & 0x2000000)) return 0;          /* on its feet only (not leaping, climbing) */
        if (!knock) ep->mode2 = front ? 3 : 2;
        else if (!front) ep->mode2 = 0;
        else if (bhEne05_CheckLeaningWall(ep, 0)) ep->mode2 = 9;
        else {
            short b = (short)(((a + 0x8000) + 0x2000) & ~0x3FFF);   /* a ledge behind it: its fall (as the game) */
            ATR_WORK* hp = bhEne_CheckEventAtr(ep->flr_no, ep->px - 12.0f * njSin(b), ep->pz - 12.0f * njCos(b), 5, 12, 15);
            ep->mode2 = 11;
            if (hp && b == (short)(-(hp->prm1 & 3) * 0x4000)) { *(ATR_WORK**)(ep->exp0 + 0x74) = hp; ep->mode2 = 12; a = b + 0x8000; }
        }
        if (ep->flg & 0x20000000) { ep->flg &= ~0x4000000; ep->flg |= 0x10; }
        ep->mtn_md &= ~2;
        ep->ayp = a;
        break;
    }
    case 9: {   /* Bandersnatch: bhEne09_DmgCheck / ChgDmgMode act 2 (close: act 3) */
        int* x = (int*)(ep->exp0 + 0x18);
        if (*x & 0x04000000) return 0;   /* (holding a player) */
        ep->flg |= 0x40;
        *x = (*x & ~0x10000000) | 0x08000000;
        if (front) *x |= 0x200; else *x &= ~0x200;
        ep->mode2 = !knock ? 2 : front ? 3 : 4;
        break;
    }
    case 3:   /* Black Widow: bhEne03_DamageInit, a medium hit on the floor (close: a heavy one) */
        if (ep->type != 0 || (ep->flg & 0x80000) || ep->exp0[105] || !*(int*)(ep->exp0 + 96)) return 0;
        ep->mode2 = knock ? 3 : 1;
        break;
    default:
        return 0;
    }
    ep->mode0 = 3; ep->mode1 = 0; ep->mode3 = 0;
    if (ep->id == 4) return 1;
slid:
    if (!knock && i >= 0 && i < PC_ENE_SLOTS) { slide[i].n = SLIDE_N; slide[i].dx = dx / l; slide[i].dz = dz / l; }
    return 1;
}

/* ... or, attacking or reeling (mode0 2, 3: the grabber letting go too), as soon as it walks again within SHOVE_LATE frames
   (pend): a Bandersnatch's grab ends standing next to the player, ready to grab again. Its runner keeps it. 1 = done now */
static struct { int until; float x, z; } pend[PC_ENE_SLOTS];
int pc_shove_apply(BH_PWORK* ep, float cx, float cz)
{
    int i = (int)(ep - ene);
    if (shove(ep, cx, cz)) return 1;
    if ((ep->mode0 == 2 || ep->mode0 == 3) && i >= 0 && i < PC_ENE_SLOTS) { pend[i].until = frame + SHOVE_LATE; pend[i].x = cx; pend[i].z = cz; }
    return 0;
}

static void break_free(void)
{
    float cx = plp->px, cz = plp->pz;
    int i, n = 0, sent = 0, best = -1;
    float bd = 1e9f;
    mine = frame + SHOVE_IMMUNE;
    for (i = 0; i < (int)sys->ewk_n && i < PC_ENE_SLOTS; i++) {
        BH_PWORK* ep = &ene[i];
        float dx = ep->px - cx, dz = ep->pz - cz, d = sqrtf(dx * dx + dz * dz);
        NJS_POINT3 a, b;
        int m = ep->mode0, ok;
        if (!(ep->flg & 0x1) || (ep->flg & 0x82) || ep->hp < 0 || (ep->stflg & 0x41000000)) continue;
        if (coop_on && !coop_runs(i) && d < bd) { bd = d; best = i; }
        if (d > (m == 2 ? SHOVE_REACH : SHOVE_R) || fabsf(ep->py - plp->py) > 8.0f) continue;
        a.x = cx; a.y = plp->py + 10.0f; a.z = cz; b.x = ep->px; b.y = a.y; b.z = ep->pz;
        if (bhCollisionCheckLine(&a, &b)) continue;   /* (not through a wall) */
        if (coop_on && coop_shove(i, cx, cz, 0)) { sent++; best = -2; host_log("breakfree: enemy %d (id %d) dist %.1f: to the partner's game\n", i, ep->id, d); continue; }
        ok = pc_shove_apply(ep, cx, cz);
        n += ok;
        host_log("breakfree: enemy %d (id %d type %d) dist %.1f mode %d -> %d/%d%s\n", i, ep->id, ep->type, d, m, ep->mode0, ep->mode2, ok ? "" : " (left alone)");
    }
    if (best >= 0) coop_shove(best, cx, cz, 1);   /* (co-op: the partner learns of our immunity) */
    host_log("breakfree: room %d/%d at %.0f %.0f: %d enemies pushed, %d sent\n", sys->stg_no, sys->rom_no, cx, cz, n, sent);
}

/* the grab code of en01, en01b, en03, en04, en09, en26, the frame the grabber lets go of plp alive: the push goes out
   SHOVE_DELAY frames later, as the shaken-off zombie flies (not when the player stands again, 36 to 54 frames later) */
void pc_shove_release(void)
{
    int e;
    if (!plp || !sys || plp->hp < 0 || released || (coop_on && coop_plp_partner())) return;   /* (co-op: the partner's game) */
    released = 1; push_at = frame + SHOVE_DELAY;
    e = (int)sys->enow;
    if (trace > 0) printf("test: shove frame %d released by enemy %d (id %d), %d frames into the hold\n", frame, e,
                          e >= 0 && e < PC_ENE_SLOTS ? ene[e].id : -1, frame - grab_at);
}

static void push_now(void)
{
    push_at = 0;
    if (trace > 0) printf("test: shove frame %d broke free (player mode %d/%d, hp %d), rule %s\n", frame, plp->mode0, plp->mode3, plp->hp, pc_shove_on() ? "on" : "off");
    if (pc_shove_on()) break_free();
    trace_until = frame + 150;
}

/* eneset.c bhControlEnemy, every game frame the enemies run (before their steps) */
void pc_shove_frame(void)
{
    int m0, i;
    frame++;
    if (!plp || !sys) return;
    m0 = plp->mode0;
    if (gen != enemy_room_gen) {   /* a room (re)loaded (a door, a retry): nothing carries over */
        gen = enemy_room_gen; prev_m0 = -1; mine = partner = push_at = 0;
        memset(pend, 0, sizeof(pend)); memset(slide, 0, sizeof(slide));
    }
    if (trace < 0) trace = getenv("RECVX_SHOVE_TRACE") != NULL;
    if (m0 == 4 && prev_m0 != 4) { released = 0; push_at = 0; grab_at = frame; }
    if (trace && m0 == 4 && prev_m0 != 4)
        printf("test: shove frame %d grabbed (hp %d)%s, %d frames after the last break free\n", frame, plp->hp, pc_grab_immune() ? " WHILE IMMUNE" : "", mine ? frame - (mine - SHOVE_IMMUNE) : -1);
    if (push_at && frame >= push_at) {
        if (plp->hp >= 0) push_now();
        push_at = 0;
    }
    if (prev_m0 == 4 && m0 != 4 && m0 != 6 && m0 != 3 && plp->hp >= 0) {
        if (trace) printf("test: shove frame %d player free (mode %d, hp %d)%s\n", frame, m0, plp->hp, released ? "" : ", no release seen");
        if (!released || push_at) { released = 1; push_now(); }   /* (a hold without the hook, or one the partner's game ran) */
        if (pc_shove_on()) mine = frame + SHOVE_IMMUNE;   /* (the immunity counts from standing again) */
    }
    prev_m0 = m0;
    for (i = 0; i < (int)sys->ewk_n && i < PC_ENE_SLOTS; i++) {
        BH_PWORK* ep = &ene[i];
        int m;
        if (pend[i].until <= frame) continue;
        if ((coop_on && !coop_runs(i)) || !(ep->flg & 0x1) || ep->hp < 0 || (ep->mode0 != 1 && ep->mode0 != 2 && ep->mode0 != 3)) { pend[i].until = 0; continue; }
        if (ep->mode0 != 1) continue;
        pend[i].until = 0;
        m = ep->mode2;
        if (shove(ep, pend[i].x, pend[i].z)) host_log("breakfree: enemy %d (id %d) pushed now that it walks again: mode 1/%d -> %d/%d\n", i, ep->id, m, ep->mode0, ep->mode2);
    }
    for (i = 0; i < (int)sys->ewk_n && i < PC_ENE_SLOTS; i++) {   /* the staggered ones slide back */
        BH_PWORK* ep = &ene[i];
        NJS_POINT3 a, b;
        float v = SLIDE_V * slide[i].n / SLIDE_N;
        if (slide[i].n <= 0) continue;
        if ((coop_on && !coop_runs(i)) || !(ep->flg & 0x1) || ep->mode0 != 3) { slide[i].n = 0; continue; }
        a.x = ep->px; a.y = ep->py + 10.0f; a.z = ep->pz;
        b.x = ep->px + slide[i].dx * (v + ep->ar); b.y = a.y; b.z = ep->pz + slide[i].dz * (v + ep->ar);
        if (bhCollisionCheckLine(&a, &b)) { slide[i].n = 0; continue; }   /* (a wall behind it: it stops there) */
        ep->px += slide[i].dx * v; ep->pz += slide[i].dz * v;
        slide[i].n--;
    }
    if (trace && frame <= trace_until && frame % 5 == 0) {
        printf("test: shove frame %d player mode %d hp %d at %.0f %.0f:", frame, m0, plp->hp, plp->px, plp->pz);
        for (i = 0; i < (int)sys->ewk_n && i < PC_ENE_SLOTS; i++) {
            BH_PWORK* ep = &ene[i];
            float dx = ep->px - plp->px, dz = ep->pz - plp->pz;
            if ((ep->flg & 0x81) == 0x1 && sqrtf(dx * dx + dz * dz) < 60.0f)
                printf(" [%d id %d mode %d/%d dist %.1f]", i, ep->id, ep->mode0, ep->mode2, sqrtf(dx * dx + dz * dz));
        }
        printf("\n");
    }
}
