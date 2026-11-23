/* Co-op: enemy snapshots and puppets (docs/COOP.md "Enemy playout"): the snapshot buffers and playout, what we send
 * (build_snapshots, snap_prioritise) and how the partner's are shown (apply_snapshots), and the enemy step hooks
 * (coop_enemy_begin/end) that run an enemy here or step it as a puppet. Moved out of coop_glue.c; the shared state is
 * in coop_int.h. */
#include "coop_int.h"

/* The other side's snapshots, kept per slot by its frame (coop_proto.h CoopSnapHist), and shown frame by frame at a
   steady cadence (CoopPlayout, docs/COOP.md "Enemy playout"): every slot from the same frame P of the sender, each
   datagram's snapshots whenever it came. Before, the newest frame's datagrams formed the set and a datagram of a newer
   frame emptied it: a zombie's halves (in other datagrams) showed different frames, and a datagram with only events
   (a grab's state) left the set empty for that frame, so every enemy ran its own AI here - the grab that refused the
   host's handover. */
/* The session's larger buffers (~1.4 MB) live on the heap from the session's first frame to its end (stop): static, they
   took that much of the PS2 address window (ARCHITECTURE.md "Address window"; Linux had ~0.5 MB left). NULL out of a
   session. */
struct CoopBufs* bufs;
CoopSnapHist* hist;
CoopSnapTx* stx;
CoopSnapRx* srx;
static EneSnap (*run_snap)[2];
uint32_t hist_applied[PC_ENE_SLOTS];       /* the sender frame of the snapshot applied last */
uint32_t shown_seq[PC_ENE_SLOTS];          /* the frame shown last, its position and turn as shown (the blend back) */
static float shown_p[PC_ENE_SLOTS][3];
static int32_t shown_ay[PC_ENE_SLOTS];
CoopPlayout playout;
int snap_room = -1;              /* the partner's room (key | its case + 1 << 16) of the snapshots kept */
static struct { unsigned int at[5], frames, held, skipped; int behind; uint32_t last; } play_st;   /* RECVX_COOP_STATS */
void bufs_free(void)
{
    free(bufs);
    bufs = NULL; hist = NULL; stx = NULL; srx = NULL; run_snap = NULL;
}
int bufs_get(void)
{
    if (bufs) return 1;
    if (!(bufs = calloc(1, sizeof(*bufs)))) return 0;
    hist = bufs->hist; stx = &bufs->stx; srx = &bufs->srx; run_snap = bufs->run_snap;   /* (zero: as reset) */
    return 1;
}
/* A puppet held for a while (a network outage, no datagram for 1 s or 3 s: its runner played on) is eased from where it stood
   to the runner's state as it comes again (`ease_*`, about a frame per OUTAGE_EASE units, its pose blended in by the motion's own
   interpolation) instead of jumping there in one frame (before: up to 28 units). Only within the room load it was shown in. */
#define OUTAGE_EASE 0.6f                /* units per frame */
#define OUTAGE_EASE_MAX 150.0f          /* farther: it went elsewhere (a room script put it there), shown there at once */
static unsigned int shown_local[PC_ENE_SLOTS];   /* our frame the slot was shown last (shown_p) */
static unsigned char ease_n[PC_ENE_SLOTS], ease_len[PC_ENE_SLOTS];
static float ease_d[PC_ENE_SLOTS][3];
static int32_t ease_a[PC_ENE_SLOTS];
void snaps_reset(void)
{
    if (hist) memset(hist, 0, PC_ENE_SLOTS * sizeof(*hist));
    memset(hist_applied, 0, sizeof(hist_applied)); memset(shown_seq, 0, sizeof(shown_seq));
    memset(shown_local, 0, sizeof(shown_local)); memset(ease_n, 0, sizeof(ease_n));
    coop_playout_reset(&playout);
}
void snaps_lost(void)   /* an outage: what the puppets show stays where it is, eased on when the partner is back */
{
    unsigned int was[PC_ENE_SLOTS];
    memcpy(was, shown_local, sizeof(was));
    snaps_reset();
    memcpy(shown_local, was, sizeof(was));
}
static uint32_t snap_neck[PC_ENE_SLOTS];         /* EneSnap.neck of the slot (SNAP_NECK in snap_root) */
int proxy_ok;                    /* host: the partner is in our room, its model loaded */
unsigned char tgt[PC_ENE_SLOTS];          /* host: the enemy targets the partner */
static CoopAtk atk[PC_ENE_SLOTS][2];             /* host, the loop guard (coop_atk_note): attacks each enemy started on [0] our player, [1] the partner */
unsigned int enemy_frame;
BH_PWORK* swap_plp;
BH_PWORK ply2_save;
unsigned char exp_save[256];
static PAD_ACT vib_save[20];            /* the rumbles before a step on the partner: what it starts is undone (yours only) */
int swap_pid = -1;
void owners_sync(void)
{
    int i;
    /* a zombie's helper parts (ids 31..38: its link, head, arms, legs, cap) while linked go with their body: its runner
       runs them (their hit capsules follow its body there; a limb shot off comes off in the game that runs the body).
       Before (phase 3) they stayed with the host, following its puppet of a body the guest ran. A part shot off
       (unlinked) stays with the game that ran it then. */
    /* (protocol 56) every linked part goes with its body (a Black Widow's legs, a Hunter's helper): the room's runner leaves
       and hands the bodies over, their parts stood still in the other game without a runner */
    for (i = 0; sys && i < (int)sys->ewk_n && i < PC_ENE_SLOTS; i++) {
        BH_PWORK* ep = &ene[i];
        BH_PWORK* r;
        if ((ep->flg & 0x81) != 0x81 || take_at[i] || giving[i]) continue;
        r = pc_root_in(ep, ene, PC_ENE_SLOTS);
        if (r != ep && (r->flg & 0x1)) owned[i] = owned[r - ene];
    }
    for (i = 0; i < PC_ENE_SLOTS; i++) {
        uint32_t P = playout.P;
        int k;
        if (owned[i] == owned_was[i]) continue;
        owned_was[i] = owned[i];
        hist_applied[i] = 0; shown_seq[i] = 0; shown_local[i] = 0; ease_n[i] = 0; own_seed[i] = 0; own_win[i] = 0;
        /* (protocol 56) the partner's kept frames of the slot go at every change of hands: when we start running it all of
           them, when we stop all before the partner's first frame of its run (EV_ENEOWN 6 names it; else everything that came
           so far). Before, frames of the partner's copy from before the hand-over could be shown after it (a teleport there
           and back). The codec bases stay: both sides keep them, the records coded against them are right whoever runs it */
        if (we_run(i)) memset(&hist[i], 0, sizeof(hist[i]));
        else {
            uint32_t from = keep_from[i] ? keep_from[i] : playout.newest + 1;
            for (k = 0; k < COOP_HIST; k++)
                if (hist[i].seq[k] && (int32_t)(hist[i].seq[k] - from) < 0) hist[i].seq[k] = 0;
        }
        keep_from[i] = 0;
        if (we_run(i) || !sys || i >= (int)sys->ewk_n || !(ene[i].flg & 0x1) || !playout.started || !P || frame_no - run_q[i][0] > 2) continue;
        own_win[i] = 1; win_st[2]++;
        for (k = 1; k >= 0; k--) {   /* the older one a frame before */
            /* (protocol 57) a grab we handed over (own_grab: grab_out) holds the zombie where it grabbed: seeded still (the newest
               state twice), not carried on at its lunge's speed - when the victim's game refuses the grab (another zombie holds
               it already) ours runs it on from where it stood. Before, it was carried on for a few frames and stepped back
               3 to 4 units when the refusal came */
            EneSnap e = run_snap[i][own_grab[i] ? 0 : k];
            uint32_t at = P - (run_q[i][0] - run_q[i][k]);
            if (k && run_q[i][1] + 1 != run_q[i][0]) continue;
            if (e.mtn_seq) e.mtn_seq = at - (run_q[i][k] - e.mtn_seq);   /* (the anchor in the partner's frames) */
            coop_hist_put(&hist[i], at, &e);
        }
        own_seed[i] = P;
    }
}

/* host: the partner held by an enemy whose hold plays a sequence on its victim (seq_hold): the Tyrant's blow
   takes the player over (mode 5, his motions, en19.c bhEne19_PlyMoveMain) until the victim's motion has run
   (bhEne_PlyActionMain), Alexia's first form burns it (mode 6, en12.c bhEne12_NG00). Run on the stand-in, which
   is put back after every step, such a hold never ended: the Tyrant waited for good with his victim "held" and
   harmed nobody again, Alexia stood still. The hold runs on this copy of the stand-in instead, its motion
   advanced as the player's own (bhSetMotion's frame step), until the enemy lets go; what it does to the copy
   goes to the partner's game as hits (and death), as other enemies' attacks. One at a time. */
struct coop_ghost_s ghost = { .slot = -1 };   /* key: the room load (enemy_room_gen) */
/* A frame without enemy steps (loading, menus) may have replaced the room's enemies: forget
   the retargeting. */
void enemy_epoch(void)
{
    if (frame_no - enemy_frame > 1) { memset(tgt, 0, sizeof(tgt)); memset(atk, 0, sizeof(atk)); }
}

int same_room(void) { return have_rx && (rx.flags & PF_IN_GAME) && partner_here(); }
/* host: our examine - a trigger's message or close-up (hitchk.c: st_flg 0x2000 / 0x80) - stops the game's enemies
   (sp_flg 0x2 off until it closes): in single player the world waits for the one player reading. The partner in our room
   plays on, and our enemies stood still in its game (its puppets of ours), its shots on them taking effect only when we
   closed the text. While the partner plays in our room (a target: proxy_ok) our enemies run during our examine (not in a
   door, a cutscene, the trade screen); our player counts as busy for them meanwhile, as one paused: no enemy attacks it
   (enemy_begin). */
int coop_enemies_run(void)
{
    return coop_on && !echo_delay && sys && plp && (sys->st_flg & 0x2080) && !(sys->cb_flg & 0x5) && !(sys->gm_flg & 0x9) && !frozen &&
           proxy_ok && same_room();   /* (the guest's too: it runs enemies) */
}
static int examining;                   /* host: this enemy step runs although our examine paused the enemies */

/* an event task works on this enemy (LoadWork): it runs here, as in a cutscene */
/* test aid RECVX_COOP_SCENE="hostframe:slot:frames" (host): from that frame our game stops its enemies for `frames` frames as a
   scene does (own_frame: they go to the partner's game after PAUSE_LEAVE), and in the frame it hands `slot` over the scene's
   event takes that one (the take-back goes in the same datagram as the hand-over: pc/tools/coop_scene_check.sh) */
struct coop_aid_scene_s aid_scene = { 0, 0, -1, 0 };
int aid_scene_on(void)
{
    static int rd;
    if (!rd++ && getenv("RECVX_COOP_SCENE") && sscanf(getenv("RECVX_COOP_SCENE"), "%u:%d:%u", &aid_scene.f, &aid_scene.slot, &aid_scene.n) == 3)
        host_log("coop: test: a scene at frame %u for %u frames, its event drives enemy %d once we gave it\n", aid_scene.f, aid_scene.n, aid_scene.slot);
    return is_host && aid_scene.slot >= 0 && frame_no >= aid_scene.f && frame_no < aid_scene.f + aid_scene.n;
}
int event_driven(const BH_PWORK* ep)
{
    int i;
    if (aid_scene.gave && ep == &ene[aid_scene.slot] && aid_scene_on()) return 1;
    for (i = 0; i < 16; i++)
        if (bhEtask[i].status && bhEtask[i].work == ep) return 1;
    return 0;
}

/* client: this enemy holds our player (it plays the enemy's motion set; of zombies, which share a
   set, the one with the hold bit). Such an enemy keeps running here until it lets go, whatever
   made it run here (no snapshots for a while: the host in a menu, a time-out, our cutscene, an
   event). Before, it became a puppet again mid-grab: its AI stopped, so nothing ever released
   the player - stuck in the grab motion, then dragged along by the puppet's snapshots.
   Helper parts (ids 31..38: a zombie's limbs, Monster Steve's arms) share their parent's set but
   hold nothing: counted as holders, they kept every orphaned zombie grab "held".
   A zombie's hold bit counts only while its steps run here: a puppet's is the stale one of its last step here (Phase 3:
   a zombie that ran here and went to the partner's game, e.g. with its grab of the partner). Counted, it ran here too
   (both games) while another zombie of the set held our player. */
int grips_us(BH_PWORK* ep)
{
    return !(ep->id >= 31 && ep->id <= 38) && plp && plp->mnwP && plp->mnwP != plp->mnwPb && plp->mnwP != sys->rmthp && plp->mnwP == ep->mnwP &&
           (ep->id != 1 || (holding(ep) && step_ran[ep - ene]));
}

/* a zombie's upper half (en01.c: a linked id 1 whose exp0 0x40 has bit 31; mlwP its body's model) */
static int zombie_half(const BH_PWORK* ep)
{
    const BH_PWORK* body = (const BH_PWORK*)ep->lkwkp;
    return ep->id == 1 && ep->exp0 && (*(int*)(ep->exp0 + 0x40) & 0x80000000) && ene_slot(ep->lkwkp) < PC_ENE_SLOTS && body->mlwP &&
           ep->mlwP >= body->mdl && ep->mlwP < body->mdl + 16 && ep->mlwP->obj_num > 8;
}
/* the node a motion's travel moves the enemy by instead (bhEne_GetTranslateMtn: its x/z put back to 0 after the motion
   step): the root of a model of its own, node 8 for a zombie's upper half; NULL none */
static NJS_CNK_OBJECT* travel_node(BH_PWORK* ep)
{
    if (!ep->mlwP || !ep->mlwP->objP) return NULL;
    if (zombie_half(ep)) return &ep->mlwP->objP[8];
    return ep->mlwP >= ep->mdl && ep->mlwP < ep->mdl + 16 ? ep->mlwP->objP : NULL;
}
static unsigned char snap_root[PC_ENE_SLOTS];     /* the other side's EneSnap.root of the slot (or ours, frozen) */
static int root_flag(BH_PWORK* ep)                /* EneSnap.root of our enemy */
{
    NJS_CNK_OBJECT* o = travel_node(ep);
    return o && !o->pos[0] && !o->pos[2] ? SNAP_ROOT_STILL : 0;
}
static void root_still(BH_PWORK* ep)
{
    NJS_CNK_OBJECT* o = (snap_root[ep - ene] & SNAP_ROOT_STILL) ? travel_node(ep) : NULL;
    if (o) o->pos[0] = o->pos[2] = 0.0f;
}
/* puppet: the other side's motion and frame, our joints and matrices. Only for an enemy that
   owns its model: helper enemies (e.g. a zombie's parts, ids 31..38) point mlwP at their
   parent's model and must not animate it. */
static int puppet_step(BH_PWORK* ep)
{
    BH_PWORK* body = (BH_PWORK*)ep->lkwkp;
    fx_puppet(ep);
    /* A zombie is two works on one model (en01.c): the body poses all 18 nodes with its motion, then its upper half
       poses nodes 8.. (the arms, the head) with a motion of its own - the reach, the hit reactions (mtn_no2: the body's + 200) - and computes the body's matrices
       (bhEne01_SetMotion, bhEne01_CalcEnemy). The puppet's upper half was never posed: the guest's zombies showed the
       body motion's upper tracks (arms hanging while the host's reached, arms up or twisted in hit reactions). */
    if (zombie_half(ep)) {
        ML_WORK* m = ep->mlwP;
        if (mtn_ok(ep, ep->mnwP, ep->mtn_no, ep->frm_no) && ep->mnwP[ep->mtn_no].obj_num <= m->obj_num - 8) {
            m->objP += 8; m->owP += 8; m->obj_num -= 8;
            bhSetMotion(ep, 0, ep->mtn_md, ep->mtn_tp);
            m->obj_num += 8; m->owP -= 8; m->objP -= 8;
            root_still(ep);
        }
        if ((snap_root[ep - ene] & SNAP_NECK) && m->obj_num > 11) {   /* its head turned to its target (bhEne01_SearchNeck, protocol 45) */
            m->objP[11].ang[1] = snap_neck[ep - ene] & 0xFFFF;
            m->objP[8].ang[1] = snap_neck[ep - ene] >> 16;
        }
        bhCalcModel(body);
        if (!(ep->flg & 0x80)) {   /* let go of its body (bhEne01_CalcEnemy): its own nodes from its own place */
            m->objP += 8; m->owP += 8; m->obj_num -= 8;
            bhCalcModel(ep);
            m->obj_num += 8; m->owP -= 8; m->objP -= 8;
        }
        return 1;
    }
    if (ep->mlwP < ep->mdl || ep->mlwP >= ep->mdl + 16) return 1;
    /* (a set has 512 motions; a linked part with a model of its own - a zombie's cap or bomb -
       shares its parent's set, whose motions have more tracks than the part has nodes: playing
       one writes past its nodes, which crashed the guest; it follows its link) */
    if (ep->mnwP && ep->mtn_no < (unsigned int)set_left(ep, ep->mnwP) && ep->mnwP[ep->mtn_no].datP && ep->frm_no >= 0 &&
        (ep->frm_no >> 16) < (int)ep->mnwP[ep->mtn_no].frm_num && !(ep->flg & 0x80) &&
        ep->mnwP[ep->mtn_no].obj_num <= ep->mlwP->obj_num) {
        bhSetMotion(ep, 0, ep->mtn_md, ep->mtn_tp);
        root_still(ep);   /* (else its travel showed twice: the host's position has it already) */
    }
    bhCalcModel(ep);
    return 1;
}

/* being hit or held by an enemy (flg 4; the partner's from its pose), or dying */
int bhCdirCheck(int my_ang, int trg_ang);   /* zonzon.c */
int ply_busy(const BH_PWORK* pl) { return (pl->flg & 4) || pl->mode0 == 4 || pl->mode0 == 6 || pl->hp < 0; }
/* host: a hit sent to the partner shows in its pose (flg 4, its reaction) a round trip later; until then the stand-in,
   put back after every step, was free again at the next step and an enemy whose blow lasts several frames hit it
   in each of them (Alexia's babies three times at once, the Gulp Worm's swing). As the hit player in single player,
   the stand-in is busy (flg 4) for the enemies' steps meanwhile. */
#define HIT_BUSY 15
static unsigned int hit_until;
/* the enemies whose hold is a sequence on the victim (ghost): the Tyrant, Alexia's first form, the Gulp Worm (its
   swallow and throw, en02.c bhEne02_PlayerControl) */
static int seq_hold(const BH_PWORK* ep) { return ep->id == 19 || ep->id == 12 || ep->id == 2; }
/* host: a grab handed to the partner's game is running (also in the round trip before its pose shows it), or the
   partner held by a ghost hold */
static int partner_held(void)
{
    int k;
    if (ghost.slot >= 0 && (!(ene[ghost.slot].flg & 0x1) || ghost.key != enemy_room_gen)) ghost.slot = -1;   /* gone with its room */
    if (ghost.slot >= 0) return 1;
    for (k = 0; k < PC_ENE_SLOTS; k++)
        if (own_grab[k] && !we_run(k)) return 1;   /* (Phase 3: an enemy it runs between attacks holds nobody) */
    return 0;
}
/* the frame step of bhSetMotion (Motion.c) for a copy that has no model of its own to pose */
static void ghost_motion(BH_PWORK* pw)
{
    int n;
    if (!pw->mnwP || pw->mtn_no >= 512) return;
    n = (int)pw->mnwP[pw->mtn_no].frm_num << 16;
    if (!pw->mnwP[pw->mtn_no].datP || n <= 0) return;
    if ((pw->mtn_md & 0x1) && pw->frm_mode) pw->frm_no -= pw->mtn_add;
    else pw->frm_no += pw->mtn_add;
    if (pw->mtn_md & 0x1) {
        if (pw->frm_no <= 0 || pw->frm_no >= n - 65536) pw->frm_mode = ~pw->frm_mode;
    } else if (pw->frm_no < 0) pw->frm_no += n;
    else if (pw->frm_no >= n) pw->frm_no -= n;
}
static void ghost_drop(const char* why)
{
    BH_PWORK* ep = &ene[ghost.slot];
    /* Alexia's burning embrace has no way out (the player is dead, the game over follows): with our player alive
       she takes up the fight again */
    if (ep->id == 12 && ep->mode0 == 2 && (ep->flg & 0x1) && !(ep->flg & 0x2)) {
        *(int*)&ep->mode0 = 1;
        ep->mtn_no = 0; ep->frm_no = 0;
        ep->flg |= 0x40;   /* her body pushes players again (bhEne12_Init; the embrace took it away) */
    }
    host_log("coop: enemy %d (id %d) lets go of the partner (%s)\n", ghost.slot, ep->id, why);
    ghost.slot = -1;
}

/* A grab of the partner's stand-in by a Cerberus (en04) or an anatomist (en26) goes to the partner as a hit (only zombies' grabs
   are handed over); the enemy plays its grab on to its end as in single player, but its steps meanwhile act on whichever
   player plp is: its hold (anatomist NG00: 30 at frames 25 and 60; Cerberus NG00: 8) and its link (bhEne04_PlayerLink puts that
   player in its mouth) would go to our own player once the partner was out of its reach (the target rule lets go of a victim
   farther than COOP_TGT_KEEP). So such a grab keeps the stand-in as its victim to its end (its hold's damage goes to the
   partner as hits, as before), and with the partner gone it lets go (grab_let_go). The Bandersnatch (en09) lets go at once. */
static unsigned char conv_grab[PC_ENE_SLOTS];
static void grab_let_go(BH_PWORK* ep)
{
    if (!ep->exp0) return;
    if (ep->id == 9) {   /* as its NG00's end: its hold bits (0x04400000) and the link its step set on the stand-in (PlyDG00/01:
                            0x200000, 0x800000) stayed, bhEne09_PlayerLink dragged our own player to its hand every step and its
                            NG01 set our player's modes at its end */
        *(int*)(ep->exp0 + 0x18) &= ~(0x04000000 | 0x800000 | 0x400000 | 0x200000 | 0x100);
        ep->mode0 = 1; ep->mode1 = ep->mode2 = ep->mode3 = 0;
        ep->flg |= 0x60;   /* (hittable, the player collides with it: as its NG00 and its missed grab) */
        ep->flg2 &= ~1;
    } else if (ep->id == 4) {   /* as its NG00's end (en04.c): its control and link bits (exp0 0x10: 0x40000, 0x80000; NG01's 0x4000000) */
        *(int*)(ep->exp0 + 0x10) = (*(int*)(ep->exp0 + 0x10) & ~(0x4000000 | 0x80000 | 0x40000)) | 0x2100000;
        ep->mode0 = 1; ep->mode1 = 0; ep->mode2 = 5; ep->mode3 = 0;
        ep->mtn_add = 0;
        ep->flg |= 0x60;
    } else if (ep->id == 26) {   /* as its NG00's release ends (en26.c): its control bit (exp0 0x40 0x20000) and the hold */
        *(int*)(ep->exp0 + 0x40) = (*(int*)(ep->exp0 + 0x40) & ~(0x20000 | 0x80000 | 0x3000000 | 0x100 | 0xF)) | 1;
        ep->mode0 = 1; ep->mode1 = 1; ep->mode2 = 2; ep->mode3 = 0;
        ep->mtn_add = 0;
        ep->flg |= 0x60;
        ep->flg2 &= ~1;
    }
}

static int etr_lim;                     /* RECVX_ENEMY_TRACE (enemy_trace) */
uint64_t gs_frame_count(void);
static int h_mode0 = -1, h_flg4;        /* our player's mode before the enemy step (attacks on it); flg 4 set for the step */
/* The shooter's aim height (at_flg 2 straight, 4 up, 8 down: free aim sets it from the shot, tps_glue.c).
   Enemies read it from plp in their damage code - lying zombies, the anatomist, knocked-down Hunters,
   Cerberus and Bandersnatch in some states take no damage from a shot aimed straight (en01.c
   bhEne01_DmgCheck: plp->at_flg & 2) - so for the enemy's step plp carries the aim of who shot it:
   the partner's from its EV_ENE_HIT (hit_aim, until that enemy's next step), else ours (when plp is the
   partner's stand-in, which has no aim of its own). */
unsigned char hit_aim[PC_ENE_SLOTS];      /* 0x80 | the partner's aim height of a hit that came in */
static BH_PWORK* aim_pl;                /* the player whose aim height the step borrows, and what it had */
static int aim_was, aim_set;
static void aim_for_step(BH_PWORK* ep, int i)
{
    int a = hit_aim[i] ? hit_aim[i] & 0xE : swap_plp ? swap_plp->at_flg & 0xE : -1;
    hit_aim[i] = 0;
    if (a < 0 || !plp || (plp->at_flg & 0xE) == a) return;
    aim_pl = plp;
    aim_was = plp->at_flg;
    plp->at_flg = aim_set = (plp->at_flg & ~0xE) | a;
}
static void aim_restore(void)
{
    if (aim_pl && aim_pl->at_flg == aim_set) aim_pl->at_flg = aim_was;   /* unless the step changed it (a grab clears it) */
    aim_pl = NULL;
}
/* host: whether an enemy's attacks reach this free player now: within COOP_TGT_TOUCH and inside its front
   quarter (ikou3: the turn it would still need, 0 within 45 degrees) */
static int in_front(BH_PWORK* ep, const BH_PWORK* pl, float d2)
{
    NJS_POINT3 p = { pl->px, pl->py, pl->pz };
    return d2 < COOP_TGT_TOUCH * COOP_TGT_TOUCH && ikou3(ep, &p, 0x2000) == 0;
}
/* bhControlEnemy, around bhJumpEnemy; 1 = the step is done (puppet) */
static int enemy_begin(BH_PWORK* ep);
static int step_stats = -1, step_kind;   /* RECVX_COOP_STATS: the enemy steps' cost, [0] run here [1] puppets */
uint64_t step_t0, step_ns[2];
unsigned int step_cnt[2];
static int has_hit(const BH_PWORK* ep)    /* the weapon code's output on it, not yet taken by its damage code */
{
    int j;
    if ((ep->flg & 0x4) || (ep->flg2 & 0x4) || (ep->comb_flg & 0x70)) return 1;
    for (j = 0; ep->mlwP && j < (int)ep->mlwP->obj_num && j < 64; j++)
        if (ep->dam[j]) return 1;
    return 0;
}
int coop_enemy_begin(BH_PWORK* ep)
{
    int i = ep - ene, r;
    if (step_stats < 0) step_stats = getenv("RECVX_COOP_STATS") != NULL;
    if (step_stats) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); step_t0 = (uint64_t)t.tv_sec * 1000000000u + t.tv_nsec; }
    h_busy = plp && (plp->flg & 4);
    h_stflg = plp ? plp->stflg : 0;
    r = enemy_begin(ep);
    h_ran = !r;
    step_kind = r != 0;
    if (!r && i >= 0 && i < PC_ENE_SLOTS && !echo_delay) {   /* its step runs here */
        aim_for_step(ep, i);
        /* our own shot on an enemy we run (not the partner's EV_ENE_HIT of this frame): it takes it now (Phase 3: the
           shooter's game reacts at once; the logs measure it) */
        if (coop_on && have_peer && ep->mode0 && !(ep->flg & 0x80) && hit_t[i][1] != frame_no + 1 && has_hit(ep)) {
            hit_t[i][0] = frame_no + 1;
            if (trace) fprintf(stderr, "coop: hit local idx=%d hp=%d\n", i, ep->hp);
        }
        /* (protocol 56) handed over and run on here until the partner runs it from the state we sent: a hit now goes to the
           partner (it starts from that state), taken off before the step - each hit applied once */
        if (giving[i] && ep->mode0 && has_hit(ep)) hit_send(i, ep, 0);
    }
    return r;
}
static int enemy_begin(BH_PWORK* ep)
{
    int i = ep - ene;
    if (i < 0 || i >= PC_ENE_SLOTS || echo_delay) return 0;
    enemy_epoch();
    enemy_frame = frame_no;
    /* both players in the trade screen (host setting): no AI step, the pose held (the guest's puppets
       hold the unchanged snapshots); not an enemy's own initialising step */
    if (frozen && ep->mode0) {
        if (owned[i] != is_host) snap_root[i] = root_flag(ep);   /* (one we run: held as its step left it) */
        return puppet_step(ep);
    }
    if (!we_run(i)) {   /* the partner's game runs it: a puppet of its snapshots */
        /* mode0 0: the enemy's own initialising step; an event of ours drives it, or it holds our player: it runs here */
        if (!ep->mode0) return 0;
        if (!is_host && (event_driven(ep) || grips_us(ep) || runs_here(ep))) { if (!seen[i]) { seen[i] = 1; hide_off(i); } return 0; }   /* (shown: it runs here) */
        /* (protocol 56) the runner's state of it has not come yet (we came into its room): hidden and still, not shown at
           the room's start position and then snapped to where it is */
        if (!seen[i]) { hide_on(i, ep); return 1; }
        if (trace && !(ep->flg & 0x80) && frame_no - snap_at[i] == 60)
            fprintf(stderr, "coop: ORPHAN enemy %d (id %d) is a puppet without the partner's state for 60 frames: modes %d %d %d %d flg %x\n", i, ep->id,
                    ep->mode0, ep->mode1, ep->mode2, ep->mode3, ep->flg);
        return puppet_step(ep);
    }
    {   /* we run it and either player may be its target (protocol 56: the guest's too, whoever was in the room first) */
        int held = proxy_ok && partner_held(), cool_p = coop_atk_cooling(&atk[i][1], frame_no), k = ene_slot(ep->lkwkp);
        if (ep->id == 27 && linked_to(ep) == 2 && (!have_rx || !partner_here())) {   /* the partner left: the larva laid on its stand-in goes too */
            ep->flg = 0;
            return 1;
        }
        if (ep->mode0) fx_begin(1);   /* what its step starts goes to the guest (EV_FX); not its initialising step */
        if (ep->id == 12) aid_m12 = *(int*)&ep->mode0;   /* (test aid RECVX_COOP_NO_EMBRACE, coop_enemy_end) */
        h_mode0 = plp->mode0;
        examining = !(sys->sp_flg & 0x2);   /* (coop_enemies_run) */
        if (conv_grab[i] && (ep->mode0 != 2 || !(ep->flg & 0x1))) conv_grab[i] = 0;
        if (conv_grab[i] && !proxy_ok) { grab_let_go(ep); conv_grab[i] = 0; host_log("coop: enemy %d (id %d) lets go: the partner is gone\n", i, ep->id); }
        if (ghost.slot == i && partner_held()) tgt[i] = 1;   /* its hold of the partner runs to its end */
        else if (conv_grab[i]) tgt[i] = 1;   /* its grab of the partner, sent as a hit, plays on there */
        else if (linked_to(ep)) tgt[i] = linked_to(ep) == 2;   /* (the stand-in also while the partner is busy: it keeps its last pose) */
        else if (ep->id == 27 && !ep->mode0) {}   /* a larva's Init: the player its moth laid it on (larva_laid) */
        else if (!proxy_ok || hold_family(ep) || own_grab[i]) tgt[i] = 0;   /* (hold_family: it holds our player; own_grab: its grab of ours) */
        else if ((ep->flg & 0x80) && k < PC_ENE_SLOTS) tgt[i] = tgt[k];   /* a linked part goes for its parent's victim */
        else {
            CoopTgtIn in = { { 0 } };
            BH_PWORK* vic = tgt[i] ? &ply2 : plp;
            float dx1 = ep->px - plp->px, dz1 = ep->pz - plp->pz, dx2 = ep->px - ply2.px, dz2 = ep->pz - ply2.pz;
            in.d[0] = dx1 * dx1 + dz1 * dz1;
            in.d[1] = dx2 * dx2 + dz2 * dz2;
            in.busy[0] = ply_busy(plp) || coop_atk_cooling(&atk[i][0], frame_no) || examining;
            in.busy[1] = held || ply_busy(&ply2) || cool_p;
            in.front[0] = !in.busy[0] && in_front(ep, plp, in.d[0]);
            in.front[1] = !in.busy[1] && in_front(ep, &ply2, in.d[1]);
            /* enemies attack only on their own floor: a player on another one counts as twice as far */
            if (plp->flr_no != ep->flr_no) in.d[0] *= 4.0f;
            if (ply2.flr_no != ep->flr_no) in.d[1] *= 4.0f;
            /* one victim per attack: a zombie holding, or an enemy whose motions its victim plays, keeps it
               (before, a flip mid-grab ran the rest of the grab on the other player: the first left in the
               grab motion, the second given the release); in its grab/bite state it keeps a victim within
               reach. Its hit and death states commit to nobody (before, a staggering enemy kept a victim
               that was not near any more). */
            in.commit = (ep->id == 1 ? ep->exp0 && holding(ep) : ghost.slot == i || ((ep->mode0 >= 2 || seq_hold(ep)) && vic->mnwP == ep->mnwP &&
                         vic->mnwP != vic->mnwPb && vic->mnwP != sys->rmthp) ||
                         (ep->id == 15 && (vic->flg & 4) && vic->hp >= 0)) ? COOP_TGT_HOLD :   /* Nosferatu drives his victim until it is up (en15.c) */
                        ep->mode0 == 2 ? COOP_TGT_ATTACK : COOP_TGT_FREE;
            tgt[i] = coop_pick_target(tgt[i], &in);
        }
        if (tgt[i]) {
            swap_plp = plp;
            ply2_save = ply2;
            memcpy(vib_save, Pad_act, sizeof(vib_save));
            memcpy(exp_save, p2.plexw, 128);
            memcpy(exp_save + 128, p2.plhdw, 128);
            plp = &ply2;
            /* held in the partner's game, or this enemy's loop guard: it sees the partner busy and waits (put
               back after the step) */
            if (held || cool_p || (int)(hit_until - frame_no) > 0) ply2.flg |= 4;
            if (ghost.slot == i) { ghost.was = ghost.pw; plp = &ghost.pw; }   /* its hold of the partner goes on */
            /* the zombie picks its and the player's grab motions by sys->ply_id: the partner's */
            if (own_p3(ep) && rx.mfile >= 0) { swap_pid = sys->ply_id; sys->ply_id = rx.mfile & 3; }   /* (Cerberus, Bandersnatch,
                                                  anatomist: the victim's motions and place by it; the Hunter's beheading by its hp) */
        } else if ((examining || coop_atk_cooling(&atk[i][0], frame_no)) && !(plp->flg & 4)) {
            plp->flg |= 4;   /* its loop guard on our player, or our examine: busy for this step only */
            h_flg4 = 1;
        }
        return 0;
    }
}

int coop_partner_step(void) { return swap_plp != NULL; }

void coop_enemy_end(BH_PWORK* ep)
{
    int i = ep - ene, swapped = swap_plp != NULL;
    static int no_emb = -1;
    if (i < 0 || i >= PC_ENE_SLOTS) return;
    /* test aid RECVX_COOP_NO_EMBRACE=1 (both sides): Alexia 1's embrace (en12 BR00 -> NG00) kills its victim whatever its health
       (the dead flag at its frame 60; in single player the game over follows), about every 320 frames in a fight at close
       range: her step that starts it is undone (her modes as before it, the victim's busy flag off), so the boss tests run on */
    if (no_emb < 0) no_emb = getenv("RECVX_COOP_NO_EMBRACE") && atoi(getenv("RECVX_COOP_NO_EMBRACE"));
    if (no_emb && ep->id == 12 && we_run(i) && ep->mode0 == 2 && !ep->mode1 && !ep->mode2 && !ep->mode3 && aid_m12 >= 0 && (aid_m12 & 0xFF) != 2) {
        *(int*)&ep->mode0 = aid_m12;
        if (plp) { plp->flg &= ~0x10004u; plp->stflg &= ~0x10000u; }
    }
    aid_m12 = -1;
    /* (protocol 56) one the partner runs whose state has not come: hidden right after its Init, before this frame draws it (an
       extra coming up at the room's first frame stood one frame at its spot) */
    if (!seen[i] && !we_run(i) && ep->mode0 && run_room >= 0 && !echo_delay && (is_host || !event_driven(ep))) hide_on(i, ep);   /* (protocol 57: the
                                                    host's event does not run it before it takes it back - its copy stood a frame where
                                                    its record put it at the host's entry: a boss's part 100 units off) */
    if (fx_open == 1) { fx_end(ep, 0); if (fx_live) fx_arm(ep); }
    aim_restore();
    if (i == gease.slot && gease.n > 0 && !swapped && plp) grab_ease(ep);
    if (swap_plp) {
        int on = plp == &ghost.pw;
        BH_PWORK* a = on ? &ghost.was : &ply2_save;   /* the victim before the step and after it */
        BH_PWORK* v = on ? &ghost.pw : &ply2;
        /* a grab it started (seq_hold: also the Tyrant's takeover, mode 5, and Alexia's embrace, mode 6), its hit
           reaction, the dead flag it set (Alexia's embrace, the Tyrant's last blow: in single player the game over) */
        int dh = a->hp - v->hp, react = v->mode0 == 2 && a->mode0 != 2, kill = (v->flg & 2) && !(a->flg & 2) && ply2_save.hp >= 0;
        int grab = seq_hold(ep) ? !on && v->mode0 >= 4 && v->mode0 <= 6 && (a->mode0 < 4 || a->mode0 > 6) :
                   (v->mode0 == 4 || (ep->id == 4 && v->mode0 == 6)) && a->mode0 != 4 && a->mode0 != 6;   /* (Cerberus's NG01 holds in mode 6) */
        if (swap_pid >= 0) { sys->ply_id = swap_pid; swap_pid = -1; }
        if (etr_lim > 0 && (grab || dh > 0 || react || kill))
            fprintf(stderr, "atk g=%u s=%d id=%d ty=%d to=P %s dh=%d m=%d.%d.%d.%d\n", (unsigned)gs_frame_count(), i, ep->id, ep->type, kill ? "kill" : grab ? "grab" : "hit", dh,
                    ep->mode0, ep->mode1, ep->mode2, ep->mode3);
        if ((grab || react) && coop_atk_note(&atk[i][1], frame_no))
            host_log("coop: loop guard: enemy %d (id %d) started %d attacks on the partner within %u frames: it leaves the partner alone for %u frames\n",
                     i, ep->id, COOP_LOOP_N, COOP_LOOP_WIN, COOP_LOOP_COOL);
        if (grab && ep->id == 1 && !(ep->flg & 0x80) && ep->exp0 && holding(ep) && have_peer && giving[i]) {
            /* (protocol 57) handed to the partner's game, its answer not here yet: the partner runs it from the state we sent
               and its own step grabs its player itself; ours lets go (our copy shows the partner's from its answer on). Before,
               the grab went over as well (EV_GRAB): the partner took the zombie twice and both games ran it (jumps of 40 units
               with Shared Enemy Control) */
            pc_enemy_unhold(ep);
            host_log("coop: enemy %d grabs the partner while its hand-over is under way: the partner's game runs it, ours lets go\n", i);
        } else if (grab && ep->id == 1 && !(ep->flg & 0x80) && ep->exp0 && holding(ep) && have_peer)
            grab_out(i, ep);   /* the client's game runs the grab */
        else if (dh > 0 || react || grab || kill) {
            Event e = { 0 };
            e.type = EV_PLY_HIT;
            e.idx = i;
            e.u.ph.hp_delta = dh > 0 ? dh : 0;
            /* its own death there; not for a grab sent as a hit: its real damage only (a second grab of a partner already held
               in its game, the pose not here yet under loss, was sent as its death) */
            if (kill && !grab && e.u.ph.hp_delta <= ply2_save.hp) e.u.ph.hp_delta = ply2_save.hp + 1 > 1000 ? 1000 : ply2_save.hp + 1;
            e.u.ph.mode[0] = v->mode0; e.u.ph.mode[1] = v->mode1; e.u.ph.mode[2] = v->mode2; e.u.ph.mode[3] = v->mode3;
            e.u.ph.flg_set = v->flg & ~a->flg;
            e.u.ph.exp1_set = *(int*)p2.plhdw & ~*(int*)(exp_save + 128);
            e.u.ph.dv[0] = v->dvx; e.u.ph.dv[1] = v->dvy; e.u.ph.dv[2] = v->dvz;
            if (grab) {   /* a grab the partner's game does not run (only zombies' are handed over): a hit there */
                e.u.ph.mode[0] = 2; e.u.ph.mode[1] = bhCdirCheck(v->ay, ep->ay) == 0 ? 0 : 1; e.u.ph.mode[2] = 1; e.u.ph.mode[3] = 0;
                e.u.ph.flg_set |= 4;
                if (ep->id == 4) ep->ct0 = 0;   /* Cerberus lets go after its first bite instead of biting the air for 2 s */
                if (ep->id == 9) grab_let_go(ep);
                else if (ep->id == 4 || ep->id == 26) conv_grab[i] = 1;   /* (its grab plays on, on the partner's stand-in: conv_grab) */
                host_log("coop: enemy %d (id %d) grabs the partner: sent as a hit\n", i, ep->id);
            }
            if (kill) host_log("coop: enemy %d (id %d) kills the partner\n", i, ep->id);
            queue_event(&e);
            hit_until = frame_no + HIT_BUSY;
        }
        if (grab && seq_hold(ep) && ghost.slot < 0) {   /* its hold goes on, on a copy (ghost) */
            ghost.pw = *v;
            ghost.slot = i;
            ghost.key = enemy_room_gen;
            ghost.t = frame_no;
        } else if (on) {
            ghost_motion(&ghost.pw);
            if (etr_lim > 0 && !(frame_no % 15))   /* RECVX_ENEMY_TRACE: the held copy */
                fprintf(stderr, "ghost s=%d m=%d.%d.%d.%d mtn=%u f=%d add=%d own=%d flg=%x hp=%d\n", i, ghost.pw.mode0, ghost.pw.mode1, ghost.pw.mode2, ghost.pw.mode3,
                        ghost.pw.mtn_no, ghost.pw.frm_no >> 16, ghost.pw.mtn_add, ghost.pw.mnwP == ghost.pw.mnwPb, ghost.pw.flg, ghost.pw.hp);
            if (ghost.pw.mnwP == ghost.pw.mnwPb && (ghost.pw.mode0 < 4 || ghost.pw.mode0 > 6)) ghost_drop("let go");
            else if (ep->id == 12 && (ep->mode0 != 2 || ep->mode3 >= 2)) ghost_drop("the embrace is over");
            else if ((ghost.pw.flg & 2) && !ghost.pw.mtn_add) ghost_drop("dead");
            else if (ep->flg & 0x2) ghost_drop("it died");
            else if (frame_no - ghost.t > 900) ghost_drop("30 s");
        }
        larva_laid(ply2.stflg & ~a->stflg & 0x38000000, 1);
        /* everything else (grabs, being moved, motions) is undone: the partner's own game
           decides what its player does */
        ply2 = ply2_save;
        memcpy(Pad_act, vib_save, sizeof(vib_save));
        memcpy(p2.plexw, exp_save, 128);
        memcpy(p2.plhdw, exp_save + 128, 128);
        plp = swap_plp;
        swap_plp = NULL;
    } else if (h_mode0 >= 0) {   /* its step on our player: a hit reaction or a grab it started (the host's, Phase 3 the guest's too) */
        if (h_flg4) plp->flg &= ~4;
        if ((plp->mode0 == 2 || plp->mode0 == 4) && h_mode0 != 2 && h_mode0 != 4 && h_mode0 != 6 && coop_atk_note(&atk[i][0], frame_no))
            host_log("coop: loop guard: enemy %d (id %d) started %d attacks on our player within %u frames: it leaves us alone for %u frames\n",
                     i, ep->id, COOP_LOOP_N, COOP_LOOP_WIN, COOP_LOOP_COOL);
    }
    h_mode0 = -1;
    h_flg4 = 0;
    if (h_ran && !swapped && plp) {   /* its step ran on our player */
        larva_laid(plp->stflg & ~h_stflg & 0x38000000, 0);
        if (!h_busy && (plp->flg & 4)) { hold.by = i; hold.id = ep->id; hold.room = coop_room_key(); hold.said = 0; }
        if (hold_family(ep)) hold.seen = frame_no;
    }
    h_ran = 0;
    if (step_t0) {   /* test aid RECVX_COOP_STATS: the cost of the enemy steps (run here / puppets) */
        struct timespec t;
        clock_gettime(CLOCK_MONOTONIC, &t);
        step_ns[step_kind] += (uint64_t)t.tv_sec * 1000000000u + t.tv_nsec - step_t0;
        step_cnt[step_kind]++;
        step_t0 = 0;
    }
}

/* Frozen-enemy watchdog, a safety net (bhControlEnemy, every enemy step, also in single player): an
   enemy alive whose modes, motion, frame, position and hp have not changed for WATCH_STEPS steps
   (5 s) is logged (and every 30 s it stays so). A zombie that runs here and is stuck in its grab
   (mode0 2: every grab state moves on by its motion) is put back to standing (then the chase), and
   our player let go if it plays that zombie's grab. Not a zombie lying after a push-off (mode0 1,
   MV04, motion stopped, the hold bit still set): it waits, legitimately, until no other zombie is
   getting up (bhEne01_CmnFlgCheck). The repair only in co-op or a room with added enemies (pc_enemy_added):
   in single player a retail room behaves as retail (the log stays).
   A puppet is the other side's to fix (the host takes a grab back after OWN_LOST, own_timeout). */
#define WATCH_STEPS 150
static struct { int sig[6], still; } watch[PC_ENE_SLOTS];
/* Test aid RECVX_ENEMY_TRACE=dist: one line per step of every living enemy (not parts) within dist
   units (10 = 1 m) of our player or the partner: its type, target (H our player, h ours because the
   partner is no target now, P the partner's stand-in, - single player / guest), distances, modes,
   motion, hp.mode0 of both players, whether each counts as busy for the target rule, a zombie's exp0
   flag word (0x20000 holding), run here or puppet, the enemy's and our position. Only reads.
   Attacks on the partner's stand-in: "atk" lines (coop_enemy_end). */
static int etr_lim = -1;
static void enemy_trace(BH_PWORK* ep, int i, int ran)
{
    int lim = etr_lim < 0 ? (etr_lim = getenv("RECVX_ENEMY_TRACE") ? atoi(getenv("RECVX_ENEMY_TRACE")) : 0) : etr_lim;
    float dh, dp = -1.0f;
    if (!lim || !sys || !plp || (ep->flg & 0x80) || !ep->mode0 || ep->hp < 0 || ep->id >= 31) return;
    dh = sqrtf((ep->px - plp->px) * (ep->px - plp->px) + (ep->pz - plp->pz) * (ep->pz - plp->pz));
    if (coop_on && p2_seen_room == coop_room_key())
        dp = sqrtf((ep->px - ply2.px) * (ep->px - ply2.px) + (ep->pz - ply2.pz) * (ep->pz - ply2.pz));
    if (dh > lim && (dp < 0 || dp > lim)) return;
    fprintf(stderr, "etr g=%u s=%d id=%d ty=%d t=%c dH=%.1f dP=%.1f m=%d.%d.%d.%d mtn=%u f=%d hp=%d pH=%d.%d pP=%d.%d bH=%d bP=%d x=%x %s e=%.0f,%.0f h=%.0f,%.0f fl=%x st=%x age=%u\n", (unsigned)gs_frame_count(), i, ep->id, ep->type,
            !coop_on || !we_run(i) ? '-' : tgt[i] ? 'P' : proxy_ok ? 'H' : 'h', dh, dp, ep->mode0, ep->mode1, ep->mode2, ep->mode3, ep->mtn_no, ep->frm_no >> 16,
            ep->hp, plp->hp, plp->mode0, dp < 0 ? 0 : ply2.hp, dp < 0 ? 0 : ply2.mode0, ply_busy(plp), dp < 0 ? 0 : ply_busy(&ply2) || partner_held(),
            ep->id == 1 && ep->exp0 ? *(int*)(ep->exp0 + 0x40) : 0, ran ? "run" : "pup", ep->px, ep->pz, plp->px, plp->pz, ep->flg, ep->stflg, ran || is_host ? 0 : frame_no - snap_at[i]);
}

void pc_enemy_watch(BH_PWORK* ep, int i, int ran)
{
    int s[6] = { *(int*)&ep->mode0, (int)ep->mtn_no, ep->frm_no, 0, 0, ep->hp };
    memcpy(&s[3], &ep->px, 4); memcpy(&s[4], &ep->pz, 4);
    if (i < 0 || i >= PC_ENE_SLOTS) return;
    step_ran[i] = ran && !gave_grab[i];   /* (grab_out: from now on its steps are the partner's) */
    if (!ran || we_run(i)) gave_grab[i] = 0;
    enemy_trace(ep, i, ran);
    if (frozen) { watch[i].still = 0; return; }   /* the trade screen holds it */
    if (memcmp(s, watch[i].sig, sizeof(s))) { memcpy(watch[i].sig, s, sizeof(s)); watch[i].still = 0; return; }
    if (++watch[i].still < WATCH_STEPS || (watch[i].still - WATCH_STEPS) % 900 || (ep->flg & 0x80) || !ep->mode0 || ep->hp < 0 || (!ep->hp && ep->id != 1)) return;   /* a zombie at hp 0 is alive; helpers (hp 0) are not watched */
    host_log("watchdog: enemy %d (id %d) still for %d steps: modes %d %d %d %d, motion %u frame %d add %d, hp %d, flg %x flg2 %x, hold %d, %s%s\n",
             i, ep->id, watch[i].still, ep->mode0, ep->mode1, ep->mode2, ep->mode3, ep->mtn_no, ep->frm_no >> 16, ep->mtn_add, ep->hp,
             ep->flg, ep->flg2, ep->id == 1 && holding(ep), ran ? "runs here" : "puppet here",
             !coop_on || echo_delay ? "" : is_host ? (owned[i] ? " (host: the partner runs it)" : " (host)") : (owned[i] ? " (guest: ours)" : " (guest)"));
    if (!ran || ep->id != 1 || ep->mode0 != 2 || event_driven(ep)) return;
    if (!coop_on && !pc_enemy_added) return;   /* single player in a retail room: as retail (logged only) */
    if (plp && holding(ep) && plp->mnwP == ep->mnwP && plp->mnwP != plp->mnwPb && (plp->mode0 == 4 || plp->mode0 == 6)) player_let_go();
    pc_enemy_unhold(ep);
    host_log("watchdog: enemy %d was stuck in its grab: back to standing\n", i);
}

static void snap_of(EneSnap* s, BH_PWORK* ep, int i)
{
    memset(s, 0, sizeof(*s));
    s->idx = i;
    s->mode[0] = ep->mode0; s->mode[1] = ep->mode1; s->mode[2] = ep->mode2; s->mode[3] = ep->mode3;
    s->mdl = mdl_of(ep);
    s->mnw = mnw_of(ep);
    s->flg = ep->flg; s->flg2 = ep->flg2; s->stflg = ep->stflg; s->mdflg = ep->mdflg;
    s->px = ep->px; s->py = ep->py; s->pz = ep->pz;
    s->ax = ep->ax; s->ay = ep->ay; s->az = ep->az;
    s->mtn_no = ep->mtn_no; s->frm_no = ep->frm_no; s->mtn_md = ep->mtn_md;
    s->hokan_count = (int16_t)ep->hokan_count; s->hokan_rate = ep->hokan_rate; s->hp = ep->hp;
    s->id = ep->id < 100 ? ep->id : 0;
    s->root = (ep->flg & 0x1) ? root_flag(ep) : 0;
    s->link = (ep->flg & 0x80) && ene_slot(ep->lkwkp) < PC_ENE_SLOTS ? ene_slot(ep->lkwkp) + 1 : 0;   /* (protocol 55) */
    memcpy(s->watr, &ep->watr, sizeof(s->watr));
}

/* At the hand-off: snapshots of the enemy slots of the room that we run (the host's; the guest's: its grabs and, with
   OPT2_OWNERSHIP, the enemies engaged with its player). Protocol 47: every active slot every frame (the byte budget and
   its priority decide in send_frame; an unchanged slot codes to 3 bytes, so leaving them out no longer pays), free
   ones a few per frame (a slot that went free there goes free here). Each with the motion's anchor and a zombie's
   neck. */
int build_snapshots(EneSnap* out)
{
    static int free_rr;
    static uint32_t a_seq[PC_ENE_SLOTS], a_seen[PC_ENE_SLOTS];   /* the motion anchors (EneSnap.mtn_seq): set, last checked */
    static int a_frm[PC_ENE_SLOTS], a_mtn[PC_ENE_SLOTS], a_add[PC_ENE_SLOTS], a_gadd[PC_ENE_SLOTS], a_last[PC_ENE_SLOTS];   /* (the speed as
                                           played: 0 for a motion that does not advance - a zombie's parts; the game's; its last frame) */
    int i, k, cnt = 0, n = 0, win[PC_ENE_SLOTS], free_idx[4], free_n = 0, m = sys->ewk_n < PC_ENE_SLOTS ? sys->ewk_n : PC_ENE_SLOTS;
    /* not during a door transition: the room number is already the next one while the
       enemy slots still hold the old room's */
    /* (protocol 56) also with the partner elsewhere: a partner coming into our room has our state of its enemies at once (its
       copies wait hidden for it) */
    if (!tx_in_game || (sys->cb_flg & 0x1) || echo_delay || !room_ready() || run_room != (coop_room_key() | my_case() << 16)) return 0;
    for (i = 0; i < m; i++)
        if (owned[i] != is_host && (ene[i].flg & 0x1)) win[cnt++] = i;
    for (k = 0; k < m && free_n < 4; k++) {
        i = (free_rr + k) % m;
        if (owned[i] != is_host && !(ene[i].flg & 0x1)) { free_idx[free_n++] = i; cnt++; }
    }
    if (m) free_rr = (free_rr + k) % m;
    for (k = 0; k < cnt; k++) {
        BH_PWORK* ep = &ene[i = k < cnt - free_n ? win[k] : free_idx[k - (cnt - free_n)]];
        EneSnap* s = &out[n++];
        snap_of(s, ep, i);
        if ((ep->flg & 0x1) && zombie_half(ep) && ep->mlwP->obj_num > 11) {
            s->root |= SNAP_NECK;
            s->neck = (uint32_t)(ep->mlwP->objP[11].ang[1] & 0xFFFF) | (uint32_t)(ep->mlwP->objP[8].ang[1] & 0xFFFF) << 16;
        }
        {   /* the anchor holds while frm_no = a_frm + frames * speed (modulo the motion's length); the speed sent (mtn_add)
               is the game's, or 0 when the motion stood still since the last frame (a zombie's parts never step theirs) */
            int64_t len = mtn_ok(ep, ep->mnwP, ep->mtn_no, 0) ? (int64_t)ep->mnwP[ep->mtn_no].frm_num << 16 : 0;
            int64_t f = a_frm[i] + (int64_t)(tx.seq - a_seq[i]) * a_add[i];
            int cont = a_seen[i] + 1 == tx.seq && a_mtn[i] == (int)ep->mtn_no + 1;
            if (len > 0) f = (f % len + len) % len;
            if (!cont || a_gadd[i] != ep->mtn_add || (ep->mtn_md & 0x1) || f != ep->frm_no) {
                a_seq[i] = tx.seq; a_frm[i] = ep->frm_no; a_mtn[i] = (int)ep->mtn_no + 1; a_gadd[i] = ep->mtn_add;
                a_add[i] = cont && ep->frm_no == a_last[i] ? 0 : ep->mtn_add;
            }
            a_seen[i] = tx.seq; a_last[i] = ep->frm_no;
            s->mtn_seq = a_seq[i]; s->mtn_frm0 = a_frm[i]; s->mtn_add = a_add[i];
        }
        run_snap[i][1] = run_snap[i][0]; run_q[i][1] = run_q[i][0];   /* (owners_sync: seeds at a hand-over) */
        run_snap[i][0] = *s; run_q[i][0] = tx.seq;
    }
    return n;
}

/* RECVX_COOP_STATS (protocol 45): the motion anchors checked against the frames that came */
static struct anc { uint32_t seq, mseq; int frm0, add, mtn; } anc[PC_ENE_SLOTS];
unsigned int anc_n, anc_ok, anc_far, anc_far_ok;
int anc_stats;
/* at the hand-off: the enemies the other side runs (client: the host's; host: the client's grabs) */
/* At the hand-off: the enemies the other side runs (client: the host's; host: the client's grabs), as they were in
   the sender's frame P (coop_playout_frame): each slot's snapshot of P, else its state between or after the
   snapshots kept (coop_hist_at; a lost or late datagram, a slot the sender skipped). What the shown position and turn
   were off by from what we now know of that frame is carried on and halved each frame (the blend back, no snap). */
void apply_snapshots(void)
{
    static unsigned int logged;
    static int stats = -1;
    int k, mismatch = 0, n = sys->ewk_n < PC_ENE_SLOTS ? sys->ewk_n : PC_ENE_SLOTS;
    uint32_t P, from, was;
    owners_sync();
    if (echo_delay || !coop_playout_frame(&playout, net_us(), &P)) return;
    /* (protocol 56) the kept snapshots are of the partner's room (receive: snap_room), which may be ours before the partner's pose
       says it plays in it: it sends them all the time. Not in a door of ours (our enemy slots are the old room's, then
       loading) */
    if (!room_ready() || run_room != (coop_room_key() | my_case() << 16) || (snap_room & 0xFFFF) != coop_room_key() ||
        ((snap_room >> 16) && (snap_room >> 16) - 1 != my_case()))
        return;
    if (stats < 0) stats = getenv("RECVX_COOP_STATS") != NULL;
    if (play_st.frames++) { play_st.held += P == play_st.last; play_st.skipped += P - play_st.last > 1 && P - play_st.last < 100; }
    play_st.last = P;
    play_st.behind += (int32_t)(playout.newest - P);   /* (newest-wins showed the newest) */
    for (k = 0; k < n; k++) {
        EneSnap cur, at_was;
        EneSnap* s = &cur;
        BH_PWORK* ep;
        int ease_pose = 0;
        int at = coop_hist_at(&hist[k], P, hist_applied[k], &cur, &from), t = k, p;
        /* (protocol 55) a zombie's upper half: the one of the same zombie here (child_here), its slot may be another */
        if (at != COOP_AT_NONE && (cur.flg & 0x80) && cur.id == 1 && cur.link && (p = cur.link - 1) < n && ene[p].id == 1 && (ene[p].flg & 0x1) &&
            own_child(&ene[p]) < n && ene[own_child(&ene[p])].lkwkp == (unsigned char*)&ene[p])
            t = own_child(&ene[p]);
        if (we_run(t)) continue;
        if (own_win[t] && own_seed[t] && (int32_t)(from - own_seed[t]) > 0 && at != COOP_AT_NONE) {   /* the new runner's first one shown */
            own_win[t] = 0;
            if (shown_seq[k]) {   /* where we carried it on: from there, halved each frame (the blend back below goes on) */
                float dx = shown_p[k][0] - cur.px, dy = shown_p[k][1] - cur.py, dz = shown_p[k][2] - cur.pz;
                int16_t da = (int16_t)(uint16_t)(shown_ay[k] - cur.ay);
                win_st[3] += (unsigned int)(100 * sqrtf(dx * dx + dz * dz));
                if (cur.mtn_no == (int32_t)ene[t].mtn_no && ene[t].frm_no > cur.frm_no) win_st[4] += (ene[t].frm_no - cur.frm_no) >> 16;
                if (fabsf(dx) + fabsf(dy) + fabsf(dz) < 20.0f) { cur.px += dx * 0.5f; cur.py += dy * 0.5f; cur.pz += dz * 0.5f; }
                if (da && cur.ay >= 0 && cur.ay < 0x10000) cur.ay = (cur.ay + da / 2) & 0xFFFF;
                shown_seq[k] = 0;   /* (no blend back from a seed this frame) */
            }
        }
        if (own_win[t]) win_st[at == COOP_AT_NONE ? 1 : 0]++;
        if (at == COOP_AT_NONE) continue;
        hist_applied[k] = from;
        play_st.at[at]++;
        /* (protocol 57) the guest: an enemy the host runs that an event of ours drives (our own entry scene, bhEtask's work) runs
           here meanwhile; taken in our scene (cinema), it starts from the runner's state (this frame's snapshot) and, when our event lets go, is eased back
           to the runner's (OUTAGE_EASE) from where our event left it. Before, it started from where its record put it (its own
           HP) and jumped to the runner's at the event's end: 101 units (a Sweeper in 1/12), 7 to 25 in a scene that held it */
        if (!is_host && (ene[t].flg & 0x1)) {
            if (!event_driven(&ene[t])) evd[t] = 0;
            else if (!evd[t] && !(sys->cb_flg & 0x4)) {}   /* (only one our scene takes: a fight's event tasks that hold an enemy as
                                                    their work in play - Alexia 2's babies - run it here as before) */
            else if (evd[t]++) {
                if (evd[t] > 2) evd[t] = 2;
                shown_p[k][0] = ene[t].px; shown_p[k][1] = ene[t].py; shown_p[k][2] = ene[t].pz; shown_ay[k] = ene[t].ay;
                shown_local[k] = frame_no - 2; shown_seq[k] = 0; ease_n[k] = 0;
                continue;
            }
        }
        if (!ease_n[k] && shown_local[k] > load_frame &&
            (frame_no - shown_local[k] > 1 || (shown_seq[k] && (int32_t)(P - shown_seq[k]) > COOP_PO_EXTRA))) {   /* held meanwhile (OUTAGE_EASE: not
                                                    shown, or the same frame shown again while nothing came) */
            float dx = shown_p[k][0] - cur.px, dy = shown_p[k][1] - cur.py, dz = shown_p[k][2] - cur.pz, d = sqrtf(dx * dx + dy * dy + dz * dz);
            if (d > 0.5f && d < OUTAGE_EASE_MAX) {
                ease_d[k][0] = dx; ease_d[k][1] = dy; ease_d[k][2] = dz; ease_a[k] = (int16_t)(uint16_t)(shown_ay[k] - cur.ay);
                ease_len[k] = ease_n[k] = (unsigned char)(d / OUTAGE_EASE < 4 ? 4 : d / OUTAGE_EASE > 250 ? 250 : d / OUTAGE_EASE);
                ease_pose = 1;
            }
        }
        if (ease_n[k]) {   /* (in place of the blend back) */
            float f = ease_n[k]-- / (float)ease_len[k];
            cur.px += ease_d[k][0] * f; cur.py += ease_d[k][1] * f; cur.pz += ease_d[k][2] * f;
            if (cur.ay >= 0 && cur.ay < 0x10000) cur.ay = (cur.ay + (int32_t)(ease_a[k] * f)) & 0xFFFF;
        } else if (at != COOP_AT_OLD && shown_seq[k] && (int32_t)(P - shown_seq[k]) > 0 && (int32_t)(P - shown_seq[k]) <= COOP_PO_EXTRA &&
            coop_hist_at(&hist[k], shown_seq[k], 0, &at_was, &was) != COOP_AT_NONE) {   /* the blend back */
            float dx = shown_p[k][0] - at_was.px, dy = shown_p[k][1] - at_was.py, dz = shown_p[k][2] - at_was.pz;
            int16_t da = (int16_t)(uint16_t)(shown_ay[k] - at_was.ay);
            if (fabsf(dx) + fabsf(dy) + fabsf(dz) < 20.0f) { cur.px += dx * 0.5f; cur.py += dy * 0.5f; cur.pz += dz * 0.5f; }
            if (da && cur.ay >= 0 && cur.ay < 0x10000) cur.ay = (cur.ay + da / 2) & 0xFFFF;
        }
        shown_seq[k] = at == COOP_AT_OLD ? 0 : P;
        shown_local[k] = frame_no;
        shown_p[k][0] = cur.px; shown_p[k][1] = cur.py; shown_p[k][2] = cur.pz; shown_ay[k] = cur.ay;
        ep = &ene[t];
        own_wait[t] = 0;
        /* Only enemies set up in our room (bhSetEnemy clears a slot when it takes it; free
           slots keep stale or zero work). mode0 0: its own initialising step (model,
           collision) has not run yet; it must run first, so only wake it like the host's and
           let our loop run that step. Room loads run enemy steps themselves (bhInitEvent), so
           most are initialised before the first snapshot. */
        if (!(ep->flg & 0x1) || (!is_host && ((event_driven(ep) && !evd[t]) || grips_us(ep) || runs_here(ep)))) continue;
        /* the slot layouts differ (an enemy whose Init ran at another moment, a child the other side made, e.g. moths
           and their leader): another type here, or a part linked to a parent there but without one here. Its flags,
           modes and motion would turn a hidden helper without a model into one drawn (bhCalcModel crashed on its NULL
           mlwP, moths in 5/82) or run another type's code with them (the linked flag crashed bhControlEnemy) */
        if (((s->flg & 0x1) && s->id != ep->id) || ((s->flg & 0x80) && !ep->lkwkp)) { mismatch++; continue; }
        if (!is_host && (gone_has(t) || ((ep->flg & 0x80) && gone_has(ene_slot(ep->lkwkp))))) {   /* named dead (EV_GONE) */
            if (!(s->stflg & 0x1000000) && !(s->flg & 0x2)) continue;   /* the host has not hidden it yet */
            gone_mask[t >> 5] &= ~(1u << (t & 31));
        }
        if (!ep->mode0) {   /* (woken as the runner's, never put to sleep or hidden before its Init: a hidden one's Init does not run,
                                and the next zombie's Init reads its work - bhEne01_GetWalkMotion - a crash at a room load, protocol 56
                                sends the snapshots all the time) */
            ep->stflg &= ~(0x41000000u & ~s->stflg);
            continue;
        }
        if (!s->mode[0]) continue;   /* a new enemy there whose Init has not run: ours ran, it would run again (its children twice) */
        seen[t] = 1; hid[t] = 0;   /* (its flags and mdflg are the runner's from here: shown) */
        snap_at[t] = frame_no;
        snap_root[t] = s->root;
        snap_neck[t] = s->neck;
        pose_seq[t] = P;
        mnw_set(ep, s->mnw);
        if (at != COOP_AT_EXACT && s->mtn_seq && !(s->mtn_md & 0x1) && mtn_ok(ep, ep->mnwP, s->mtn_no, 0)) {   /* the anchor's frame (coop_hist_at), in the motion's loop */
            int64_t len = (int64_t)ep->mnwP[s->mtn_no].frm_num << 16;
            if (len > 0) s->frm_no = (int32_t)(((int64_t)s->frm_no % len + len) % len);
        }
        if (at != COOP_AT_EXACT && !mtn_ok(ep, ep->mnwP, s->mtn_no, s->frm_no)) s->frm_no = hist[k].s[from % COOP_HIST].frm_no;   /* (carried past its motion's end) */
        if (s->mdl >= 0 && ep->mdl[s->mdl].objP) ep->mlwP = &ep->mdl[s->mdl];
        if ((int)ep->mtn_no != s->mtn_no) { ep->hokan_count = s->hokan_count ? s->hokan_count + 1 : 0; ep->hokan_rate = s->hokan_rate; }
        if (ease_pose) { ep->hokan_count = 8; ep->hokan_rate = (s->mtn_md & 0x20) ? 0 : 49152; }   /* (its pose too: from the held one) */
        /* (modes our game would call out of its tables with, when it runs the enemy: a door's, a cutscene's local frames,
           a hand-over: not taken; their type is ours, the snapshot carries none) */
        if (!own_type(ep) || ene_modes_ok(ep, ep->type, s->mode, ep->exp0)) { ep->mode0 = s->mode[0]; ep->mode1 = s->mode[1]; ep->mode2 = s->mode[2]; ep->mode3 = s->mode[3]; }
        if (trace && (ep->flg & 0x82) == 0x2 && !(ep->stflg & 0x1000000) && !(s->flg & 0x2) && !(s->stflg & 0x1000000))
            fprintf(stderr, "coop: enemy %d dead here, alive in the host's game: alive again\n", t);
        /* not the host's hit marker (flg & 4): it would come back as a hit of ours */
        ep->flg = s->flg & ~0x4; ep->flg2 = s->flg2 & ~0x4; ep->stflg = s->stflg; ep->mdflg = s->mdflg;
        /* morphing (mdflg 2) draws between the shapes obj_a and obj_b, which only the enemy's own code picks (Alexia's
           tentacles, babies, third form): a puppet that never ran that code has none - the guest crashed drawing it */
        if (!ep->obj_a || !ep->obj_b) ep->mdflg &= ~0x2;
        ep->px = s->px; ep->py = s->py; ep->pz = s->pz;
        ep->ax = s->ax; ep->ay = s->ay; ep->az = s->az;
        ep->mtn_no = s->mtn_no; ep->frm_no = s->frm_no; ep->mtn_md = s->mtn_md; ep->hp = s->hp;
        memcpy(&ep->watr, s->watr, sizeof(s->watr));
        if (anc_stats && at == COOP_AT_EXACT) {   /* RECVX_COOP_STATS: would the slot's last anchor have given this frame (what extrapolation shows)? */
            struct anc* a = &anc[t];
            if (a->seq && a->seq != from && a->mseq == s->mtn_seq && a->mtn == s->mtn_no && !(s->mtn_md & 0x1)) {
                int64_t len = mtn_ok(ep, ep->mnwP, ep->mtn_no, 0) ? (int64_t)ep->mnwP[ep->mtn_no].frm_num << 16 : 0;
                int64_t f = a->frm0 + (int64_t)(from - a->mseq) * a->add, e;
                if (len > 0) f = (f % len + len) % len;
                e = f - s->frm_no;
                if (len > 0 && e > len / 2) e -= len;
                if (len > 0 && e < -len / 2) e += len;
                anc_n++; anc_ok += e >= -256 && e <= 256;
                if (from - a->seq > 1) { anc_far++; anc_far_ok += e >= -256 && e <= 256; }
            }
            a->seq = from; a->mseq = s->mtn_seq; a->frm0 = s->mtn_frm0; a->add = s->mtn_add; a->mtn = s->mtn_no;
        }
    }
    if (mismatch && frame_no - logged > 300) { logged = frame_no; host_log("coop: %d enemy snapshots skipped: the slot holds another kind of enemy here\n", mismatch); }
    if (stats && play_st.frames >= 300) {   /* test aid: how the frames were shown */
        unsigned int t = play_st.at[1] + play_st.at[2] + play_st.at[3] + play_st.at[4];
        host_log("coop: playout: margin %.1f ms, the partner's frame %u shown (its newest %u; %.2f frames behind it on average), %u frames: %u held, %u skipped; slots: exact %.2f%%, between %.2f%%, "
                 "carried on %.2f%%, old %u\n", coop_playout_margin_ms(&playout), P, playout.newest, play_st.behind / (double)play_st.frames, play_st.frames, play_st.held, play_st.skipped,
                 100.0 * play_st.at[1] / (t ? t : 1), 100.0 * play_st.at[2] / (t ? t : 1), 100.0 * play_st.at[3] / (t ? t : 1), play_st.at[4]);
        memset(&play_st, 0, sizeof(play_st));
    }
}
/* protocol 45: the order snapshots are coded in. A group (an enemy and its linked parts, kept together in one datagram)
   gains its weight each frame until sent: 4 near a player (10 m) or targeting the partner, 1 otherwise. Under the
   byte budget every slot goes every frame; over it the stalest near groups go first. */
float snap_prio[PC_ENE_SLOTS];
unsigned char snap_grp[PC_ENE_SLOTS];
static int snap_order(const void* a, const void* b)
{
    const EneSnap* x = a;
    const EneSnap* y = b;
    int gx = snap_grp[x->idx], gy = snap_grp[y->idx];
    if (snap_prio[gx] != snap_prio[gy]) return snap_prio[gx] < snap_prio[gy] ? 1 : -1;
    return gx != gy ? gx - gy : x->idx - y->idx;
}
void snap_prioritise(EneSnap* s, int ns)
{
    int k;
    for (k = 0; k < ns; k++) {
        BH_PWORK* ep = &ene[s[k].idx];
        int g = (ep->flg & 0x80) && ene_slot(ep->lkwkp) < PC_ENE_SLOTS ? ene_slot(ep->lkwkp) : s[k].idx;
        snap_grp[s[k].idx] = (unsigned char)g;
    }
    for (k = 0; k < ns; k++) {
        BH_PWORK* ep = &ene[s[k].idx];
        float d0, d1;
        if (snap_grp[s[k].idx] != s[k].idx) continue;   /* (the group's weight once, by its leader) */
        d0 = plp ? (ep->px - plp->px) * (ep->px - plp->px) + (ep->pz - plp->pz) * (ep->pz - plp->pz) : 1e12f;
        d1 = (ep->px - ply2.px) * (ep->px - ply2.px) + (ep->pz - ply2.pz) * (ep->pz - ply2.pz);
        snap_prio[s[k].idx] += (ep->flg & 0x1) && (d0 < 1e4f || d1 < 1e4f || (is_host && tgt[s[k].idx])) ? 4.0f : 1.0f;
    }
    qsort(s, ns, sizeof(*s), snap_order);
}
