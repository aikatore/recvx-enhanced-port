/* Co-op: the effects of the partner's game (EV_FX, FX_CALL, FX_POOL; docs/COOP.md "Effects"): what our game starts
 * is sent, what the partner's starts is mirrored here, and an effect step on a puppet or the stand-in is undone where it
 * would change our game. Moved out of coop_glue.c; the shared state is in coop_int.h. */
#include "coop_int.h"


/* ---------------------------------------------------------------- per frame */
int fx_open;                     /* effects (EV_FX, below): 1 enemy or player window, 2 a projectile's step */
int fx_live;                     /* the window's effects go out now (else only recorded: fx_resync) */

/* ---------------------------------------------------------------- effects (protocol 12, EV_FX) */
/* The guest's enemies are puppets: their AI does not run there, so nothing it starts besides the
   motion showed - the zombie's vomit (effect 350), acid and spit (256, 258), the moth's powder
   (260), Alexia's fire, the blood of a hit enemy, sparks. And neither side saw the partner's
   muzzle flash, the sparks and splashes where its shots hit, or its grenades and bolts: the shot
   ran only in the shooter's game. Effects are entries of eff[] that bhSetEffect(Tb) starts and the
   caller then adjusts (a texture of its model, a direction, counters); from then on each runs by
   itself in bhControlEffect. So the sender looks at eff[] around the code that starts them - each
   enemy step on the host, our player's step on both sides (bhControlPlayer: the weapon code), and
   the step of our own projectiles (their explosion) - and sends each new entry once as EV_FX: its
   fields, and what it points to as references (an enemy slot, the sender's player, the receiver's
   player, a sub-model's texture). The receiver starts the same entry and lets it run. Not sent:
   entries the receiver makes itself (the muzzle flash and smoke of bhSetGunFire, which the
   receiver calls for the partner's model at the shot mark of its pose, and the effects on our own
   hands), entries with pointers we cannot name or a matrix set by their caller (logged once per
   effect number with RECVX_COOP_TRACE). An entry started from EV_FX only shows: what a
   projectile's or a blast's step does to our enemies and our player is undone, and the blast it
   would start is dropped - the owner's game applies the hit and sends its blast. Enemy attack
   effects (vomit, acid) do hurt the receiver's player where they land: the host's copy can only
   reach the host's player. At most FX_FRAME per frame, none while FX_BACKLOG events wait for the
   partner; more wait (FX_PEND, oldest first) and go in the next frames, dropped after FX_PEND_AGE frames, in another
   room or when FX_PEND wait (they are cosmetic). Before, those over the budget were dropped: Alexia 1's flame attack
   starts 16 fires on her arm (274) and 3 flames a frame (275) at once, so the partner saw 8 of her 16 fires and missed a
   third of the burning blood she bled meanwhile (265: their fires on the floor). */
#define FX_FRAME 8
#define FX_BACKLOG 48
#define FX_PEND 256
#define FX_PEND_AGE 45
#define FX_IN_FRAME 64                  /* the partner's effects started here in one frame (it sends FX_FRAME; more after a stall) ... */
#define FX_IN_MAX 256                   /* ... and alive at once: half of eff[] at most */
static uint32_t fx_was[16];             /* eff[] slots in use when the window opened */
/* the larger state (~190 KB with the saved enemies) on the heap, allocated with the first window or EV_FX */
static struct {
    unsigned char mirror[512];          /* our eff[] slot holds an entry started from the partner's EV_FX */
    unsigned char seen[400];            /* fx_note */
    float arm[PC_ENE_SLOTS][4];                  /* fx_arm: sent */
    int arm_room[PC_ENE_SLOTS];
    BH_PWORK keep_ply, keep_ene[PC_ENE_SLOTS];   /* coop_fx_step */
    Event pend[FX_PEND];                         /* fx_queue: over the frame's budget, oldest at pend_h */
    unsigned int pend_t[FX_PEND];
    short pend_slot[FX_PEND];
    Event rec[512];                              /* fx_resync: what started our eff[] slot (FXS_REC) */
    unsigned char rec_on[512];                   /* FXS_* */
    Event pool[4];                               /* coop_fx_pool: its call, for the entry it makes next (fx_end) */
    int pool_n;
    short arm_fx[PC_ENE_SLOTS][16];              /* fx_puppet: the partner's Alexia's last 16 arm fires here (her work's table) */
    unsigned char arm_fx_n[PC_ENE_SLOTS];
} *fxh;
enum { FXS_REC = 1, FXS_SENT = 2, FXS_FIRE = 4 };   /* rec[] holds its EV_FX; the partner has it; a fire (263) of our burning blood */
#define fx_mirror (fxh->mirror)
static int fx_heap(void) { return fxh || (fxh = calloc(1, sizeof(*fxh))) != NULL; }
unsigned int fx_frame, fx_sent, fx_dropped, fx_stat;   /* this frame's count; totals (stats) */
static unsigned int pend_h, pend_n;

static int fx_hurts(int id) { return id == 13 || id == 16 || id == 127 || id == 130; }   /* bolt, blast, gas, grenade/rocket */
static int fx_gunfire(int id) { return id == 2 || id == 3 || id == 9 || id == 14 || id == 132 || id == 229; }   /* bhSetGunFire's */
static void fx_slots(uint32_t* map)
{
    int i;
    memset(map, 0, 16 * sizeof(uint32_t));
    for (i = 0; i < 512; i++)
        if (eff[i].flg & 0x3) map[i >> 5] |= 1u << (i & 31);
        else fx_mirror[i] = fxh->rec_on[i] = 0;
}
static int fx_new(const uint32_t* map, int i) { return (eff[i].flg & 0x3) && !(map[i >> 5] >> (i & 31) & 1); }

/* the partner sees our room now: what we start goes out (else it is recorded for when it comes: fx_resync) */
static int fx_watching(void) { return have_peer && !echo_delay && have_rx && same_room() && (rx.flags & PF_VISIBLE); }
void fx_begin(int kind)
{
    static int off = -1;
    if (off < 0) off = getenv("RECVX_COOP_FX") && !atoi(getenv("RECVX_COOP_FX"));   /* test aid: RECVX_COOP_FX=0 sends none (bandwidth before/after) */
    fx_open = 0;
    if (off || !have_peer || echo_delay || !fx_heap()) return;
    fx_slots(fx_was);
    fx_open = kind;
    fx_live = fx_watching();
    fxh->pool_n = 0;
}

static BH_PWORK* fx_me(void) { return swap_plp ? swap_plp : plp; }   /* an enemy step may have plp on the proxy */
static int fx_ref_out(const void* p)   /* a pointer as a reference for the partner; -1: none of ours */
{
    const BH_PWORK* w = p;
    if (!p) return FX_NONE;
    if (w >= ene && w < ene + PC_ENE_SLOTS) return (const char*)w == (const char*)&ene[w - ene] ? (int)(w - ene) : -1;
    if (w == fx_me()) return FX_SENDER;
    if (w == &ply2 || w == &ghost.pw) return FX_RECEIVER;   /* (ghost: the copy of the partner's stand-in a boss's hold plays on - Alexia's embrace sets it alight) */
    return -1;
}
static BH_PWORK* fx_ref_in(int r)      /* a partner's reference as our work, NULL: not here */
{
    if (r < PC_ENE_SLOTS) return r < (int)sys->ewk_n && (ene[r].flg & 0x1) ? &ene[r] : NULL;
    if (r == FX_SENDER) return p2_chr >= 0 && p2_seen_room == coop_room_key() && ply2.mlwP ? &ply2 : NULL;
    return r == FX_RECEIVER ? plp : NULL;
}
static int fx_mdl(const BH_PWORK* o, const void* p, int tex)   /* p is o's mdl[k] (or its texture): k; -2 not */
{
    int k;
    if (!p) return -1;
    for (k = 0; o && k < 16 && k < o->mdl_n; k++)
        if (tex ? p == (const void*)o->mdl[k].texP : p == (const void*)&o->mdl[k]) return k;
    return -2;
}

static void fx_note(int id, const char* why)
{
    if (trace && id < 400 && !fxh->seen[id]++) fprintf(stderr, "coop: fx %d not sent: %s\n", id, why);
}

static int fx_frame_left(void) { return fx_frame != frame_no ? (fx_frame = frame_no, fx_stat = 0, 1) : fx_stat < FX_FRAME; }
static int fx_budget(void) { return fx_frame_left() && ring.next - ring.head <= FX_BACKLOG; }
static void fx_out(Event* e, int slot)
{
    if (slot >= 0) fxh->rec_on[slot] |= FXS_SENT;
    queue_event(e);
    fx_stat++;
    fx_sent++;
    if (trace) fprintf(stderr, "coop: fx %d out (follows %d)\n", e->u.fx.id, e->idx);
}
/* the waiting ones first (each frame, coop_control_remote, and before a new one) */
static void fx_flush(void)
{
    while (fxh && pend_n && fx_budget()) {
        Event* e = &fxh->pend[pend_h];
        unsigned int t = fxh->pend_t[pend_h];
        int slot = fxh->pend_slot[pend_h];
        pend_h = (pend_h + 1) % FX_PEND;
        pend_n--;
        if (frame_no - t > FX_PEND_AGE || !sys || e->stg != sys->stg_no || e->rom != sys->rom_no || !have_peer || !same_room()) {
            fx_dropped++;
            if (trace) fprintf(stderr, "coop: fx %d dropped (waited %u frames)\n", e->u.fx.id, frame_no - t);
        } else fx_out(e, slot);
    }
}
static void fx_queue(Event* e, int slot)   /* slot: the eff[] entry it starts (-1: none or several) */
{
    fx_flush();
    if (!pend_n && fx_budget()) { fx_out(e, slot); return; }
    if (!fxh || pend_n == FX_PEND) {
        fx_dropped++;
        if (trace) fprintf(stderr, "coop: fx %d dropped (%d waiting)\n", e->u.fx.id, (int)pend_n);
        return;
    }
    fxh->pend[(pend_h + pend_n) % FX_PEND] = *e;
    fxh->pend_t[(pend_h + pend_n) % FX_PEND] = frame_no;
    fxh->pend_slot[(pend_h + pend_n) % FX_PEND] = (short)slot;
    pend_n++;
}

/* effsub3.c bhSetEffParticle(Mk2): blood and debris - sets of three eff[] entries pointing at each
   other and at a drawing function - go as the call itself */
void coop_fx_particle(BH_PWORK* ewP, int lnk, const void* offP, const void* dirP, unsigned int col, unsigned int dst, int typ, int mk2)
{
    Event e;
    int ref = fx_ref_out(ewP);
    if (!fx_open || !fx_live || ref < 0 || (!ewP && !offP) || !dirP || typ < 0 || typ >= FX_PTCL_TYPES || lnk < 0 || lnk >= 64) return;
    memset(&e, 0, sizeof(e));
    e.type = EV_FX;
    coop_fx_put(&e.idx, &e.u.fxp.refs, FXR_IDX, ref);
    e.stg = sys->stg_no;
    e.rom = sys->rom_no;
    e.u.fxp.id = FX_PARTICLE;
    e.u.fxp.typ = typ; e.u.fxp.mk2 = mk2; e.u.fxp.lnk = lnk;
    e.u.fxp.col = col; e.u.fxp.dst = dst;
    if (offP) memcpy(e.u.fxp.off, offP, sizeof(e.u.fxp.off));
    memcpy(e.u.fxp.dir, dirP, sizeof(e.u.fxp.dir));
    if (!coop_check_event(&e, is_host)) fx_queue(&e, -1);
}

/* (protocol 57) effsub3.c bhSetBloodPoolLnk: a blood pool (effect 300: two eff[] entries, the second its vertex work) goes as
   the call itself (its table's values, the offset added to the position). Before, it was "work not of the enemy that started
   it": a pool under a dying enemy showed only in the game that ran it */
int bhSetBloodPoolLnk(BH_PWORK* ewP, NJS_POINT3* posP, int ay, BP_WORK* tabP, int pal_bnk);
OR_WORK* rySetEffBlood(NJS_MATRIX* mtxP, NJS_POINT3* posP, NJS_POINT3* dirP, int typ_no);    /* effsub3.c */
OR_WORK* rySetEffBlood2(NJS_MATRIX* mtxP, NJS_POINT3* posP, NJS_POINT3* dirP, int typ_no);
void bhEff_SetPtcl(BH_PWORK* epw, NJS_POINT3* pos, int tex_id);                             /* effsub4.c */
void bhEff_SetPtcl2(BH_PWORK* epw, NJS_POINT3* pos, NJS_MATRIX* mtx);
void bhEff_SetPtcl2V(BH_PWORK* epw, NJS_POINT3* pos, NJS_POINT3* dv, int wcnt);
void pc_en19_claw(BH_PWORK* ewP, int node, int col, int tim, float src_x, float dst_x);   /* en19.c (TARGET_PC) */
void pc_en19_shake(BH_PWORK* ewP, int row);
void pc_en19_first(BH_PWORK* ewP);
void coop_fx_pool(BH_PWORK* ewP, const void* posP, int ay, const void* tabP, int bank)
{
    const BP_WORK* t = tabP;
    const float* p = posP;
    Event e;
    int ref = fx_ref_out(ewP), k;
    if (!fx_open || ref < 0 || !p || !t) return;
    memset(&e, 0, sizeof(e));
    e.type = EV_FX;
    e.stg = sys->stg_no;
    e.rom = sys->rom_no;
    coop_fx_put(&e.idx, &e.u.fxb.refs, FXR_IDX, ref);
    e.u.fxb.id = FX_POOL;
    e.u.fxb.bank = (uint8_t)bank;
    e.u.fxb.ay = ay;
    e.u.fxb.srd_dir = t->srd_dir; e.u.fxb.srd_pos = t->srd_pos; e.u.fxb.bld_spd = t->bld_spd;
    for (k = 0; k < 5; k++) { e.u.fxb.srt_spd[k] = t->srt_spd[k]; e.u.fxb.srt_dir[k] = t->srt_dir[k]; }
    e.u.fxb.pos[0] = p[0] + t->off_pos.x; e.u.fxb.pos[1] = p[1] + t->off_pos.y; e.u.fxb.pos[2] = p[2] + t->off_pos.z;
    if (coop_check_event(&e, is_host)) fx_note(300, "pool out of range");
    else if (fxh->pool_n < 4) fxh->pool[fxh->pool_n++] = e;   /* (sent and recorded with the entry it makes: fx_end) */
}

/* (protocol 58) effect starters that keep a node's matrix (OR_WORK effects: their state overlays the entry's fields, so they
   cannot go as an entry) or take a work: their call (pc_prelude.h FXN_*, decomp hooks at their start), with the node as the
   enemy slot and its index. Before, these showed in the runner's game only: the Tyrant's blood where he was hit (309, 311),
   his claw planes (307) and camera shakes (310), Monster Steve's dust and sparks (365, 366). */
static int fx_node(const void* mtxP, int* slot)   /* a node's matrix of one of our enemies or a player: its index (-1: none) */
{
    int i, k;
    for (i = -2; i < (int)sys->ewk_n && i < PC_ENE_SLOTS; i++) {
        const BH_PWORK* w = i == -2 ? fx_me() : i == -1 ? (swap_plp ? plp : &ply2) : &ene[i];   /* (an enemy's step on the stand-in: plp) */
        const ML_WORK* m = w->mlwP;
        if ((i >= 0 && !(w->flg & 0x1)) || !m || !m->owP) continue;
        for (k = 0; k < (int)m->obj_num && k < 64; k++)
            if (mtxP == (const void*)&m->owP[k].mtx) { *slot = i == -2 ? FX_SENDER : i == -1 ? FX_RECEIVER : i; return k; }
    }
    return -1;
}
void coop_fx_call(int fn, BH_PWORK* ewP, const void* mtxP, int a, int b, const void* v0, const void* v1)
{
    Event e;
    int slot = 255, node = 0, n0 = fn == FXN_CLAW ? 2 : 3;
    if (!fx_open || !fx_live || !sys) return;
    if (mtxP) {
        if ((node = fx_node(mtxP, &slot)) < 0 || (ewP && fx_ref_out(ewP) != slot)) return;
    } else if (ewP && ((slot = fx_ref_out(ewP)) < 0 || slot >= PC_ENE_SLOTS)) return;
    memset(&e, 0, sizeof(e));
    e.type = EV_FX; e.stg = sys->stg_no; e.rom = sys->rom_no;
    coop_fx_put(&e.idx, &e.u.fxc.refs, FXR_IDX, slot);
    e.u.fxc.id = FX_CALL; e.u.fxc.fn = (uint8_t)fn; e.u.fxc.node = (uint8_t)node;
    e.u.fxc.a = a; e.u.fxc.b = (int16_t)b;
    if (v0) memcpy(e.u.fxc.v, v0, n0 * sizeof(float));
    if (v1) memcpy(e.u.fxc.v + 3, v1, 3 * sizeof(float));
    if (!coop_check_event(&e, is_host)) fx_queue(&e, -1);
    else if (trace) fprintf(stderr, "coop: fx call %d not sent: fields out of range\n", fn);
}

/* one new eff[] entry to the partner (owner: the enemy or player whose step started it) */
static int fx_resyncs(const O_WRK* op) { return (op->id == 265 && op->type == 1) || op->id == 274; }   /* fx_resync: those it can start again */
static void fx_send(O_WRK* op, BH_PWORK* owner, int player)
{
    static const unsigned char zero[sizeof(ML_WORK) * 16];
    Event e;
    int link = fx_ref_out(op->lkwkp);   /* also without the link flag: the vomit keeps its zombie there (its sound) */
    const char* why = NULL;
    if (op->id == FX_PARTICLE || op->id == 300 || op->id == 307 || (op->id >= 309 && op->id <= 311) || op->id == 365 || op->id == 366) return;   /* went as
                                                                         the call (coop_fx_particle, coop_fx_pool, coop_fx_call) */
    memset(&e, 0, sizeof(e));
    e.type = EV_FX;
    e.stg = sys->stg_no;
    e.rom = sys->rom_no;
    if (op->id == FX_BURST) {   /* sparks (bhSetRapEff): its parameter block and particle source */
        e.u.fxr.id = FX_BURST;
        e.u.fxr.nw = FX_RAW_WORDS;
        memcpy(e.u.fxr.w, ((OR_WORK*)op)->free4, sizeof(e.u.fxr.w));
        if (coop_check_event(&e, is_host)) why = "burst out of range";
    } else {
        int own = fx_ref_out(owner), txp = fx_mdl(owner, op->txp[0], 1), mlw = op->id < 400 && op->mlwP == &sys->efm[op->id] ? -1 : fx_mdl(owner, op->mlwP, 0), ex = fx_ref_out(op->exp0);
        const FxRule* rl = coop_fx_rule(op->id);
        int x1 = 0, k;
        if (player && (fx_gunfire(op->id) || link == FX_SENDER)) return;   /* the partner makes these itself */
        /* (protocol 57) exp1 an eff[] entry its owner's work points at (Alexia 1's burning blood: her fire manager, en12 Init
           EXP0_I(0x20)): sent as that word, the receiver's copy of the owner has its own. Before, it went as "pointers": only the
           game that ran her saw her blood */
        if (rl && (rl->refs & FXF_EXP1) && op->exp1 && own >= 0 && own < PC_ENE_SLOTS && owner->exp0)
            for (k = 0; k < FX_X1_MAX && !x1; k++)
                if (*(unsigned char**)(owner->exp0 + 4 * k) == op->exp1) x1 = k + 1;
        if (rl && (rl->refs & FXF_AO))
            for (k = 0; k < 3; k++) { float a = (&op->aox)[k]; e.u.fx.ao[k] = (int16_t)lrintf((a < -1 ? -1 : a > 1 ? 1 : a) * 16384); }
        e.u.fx.x1 = (uint8_t)x1;
        if (op->id >= 400) why = "number";
        else if (link < 0) why = "follows something unknown";
        else if (txp < -1 || mlw < -1 || ex < 0) why = "texture, model or work not of the enemy or player that started it";
        else if (op->mtx != (float(*)[16])op->mtxbuf || op->pvp != op->pv || op->tvp != op->tv || memcmp(op->mtxbuf, zero, sizeof(op->mtxbuf))) why = "matrix set by its caller";
        else if ((op->exp1 && !x1) || op->exp2 || op->exp3 || op->mskp || op->obj_a || op->obj_b || op->mnwP || op->mnwPb || op->mtn_tp || op->objbak || op->func) why = "pointers";
        else if (memcmp(op->mdl, zero, sizeof(op->mdl)) || memcmp(op->skp, zero, sizeof(op->skp)) || memcmp(op->mbp, zero, sizeof(op->mbp)) || memcmp(op->txp + 1, zero, 15 * sizeof(op->txp[0]))) why = "models";
        else if ((op->stflg >> 16) || op->lkono < 0 || op->lkono >= 64 || fabsf(op->sx) >= 127 || fabsf(op->sy) >= 127 || fabsf(op->sz) >= 127) why = "flags or scale";
        coop_fx_put(&e.idx, &e.u.fx.refs, FXR_IDX, link);
        e.u.fx.id = op->id; e.u.fx.type = op->type; e.u.fx.flg = op->flg; e.u.fx.stflg = op->stflg; e.u.fx.tex_id = op->tex_id;
        e.u.fx.mdlver = op->mdlver; e.u.fx.flr_no = op->flr_no; e.u.fx.lkono = op->lkono;
        coop_fx_put(&e.u.fx.owner, &e.u.fx.refs, FXR_OWNER, own < 0 ? FX_NONE : own);
        e.u.fx.mode[0] = op->mode0; e.u.fx.mode[1] = op->mode1; e.u.fx.mode[2] = op->mode2; e.u.fx.mode[3] = op->mode3;
        e.u.fx.txp = txp; e.u.fx.mlw = mlw;
        coop_fx_put(&e.u.fx.exp0, &e.u.fx.refs, FXR_EXP0, ex < 0 ? FX_NONE : ex);
        e.u.fx.ang[0] = op->ax; e.u.fx.ang[1] = op->ay; e.u.fx.ang[2] = op->az;
        e.u.fx.ct[0] = op->ct0; e.u.fx.ct[1] = op->ct1; e.u.fx.ct[2] = op->ct2; e.u.fx.ct[3] = op->ct3;
        e.u.fx.s[0] = (int16_t)(op->sx * 256); e.u.fx.s[1] = (int16_t)(op->sy * 256); e.u.fx.s[2] = (int16_t)(op->sz * 256);
        e.u.fx.p[0] = op->px; e.u.fx.p[1] = op->py; e.u.fx.p[2] = op->pz;
        e.u.fx.o[0] = op->lox; e.u.fx.o[1] = op->loy; e.u.fx.o[2] = op->loz;
        e.u.fx.n[0] = op->xn; e.u.fx.n[1] = op->yn; e.u.fx.n[2] = op->zn;
        if (!why && (txp >= 0 || mlw >= 0) && own < 0) why = "model of no enemy or player";
        if (!why && coop_check_event(&e, is_host)) why = coop_fx_rule(op->id) ? "fields out of range (coop_proto.c fx_rules)" : "not an effect co-op sends (coop_proto.c fx_rules)";
    }
    if (why) { if (fx_live) fx_note(op->id, why); return; }
    if (fx_resyncs(op)) { fxh->rec[op - eff] = e; fxh->rec_on[op - eff] = FXS_REC; }
    if (fx_live) fx_queue(&e, op - eff);
}

void fx_end(BH_PWORK* owner, int player)
{
    int i, k = 0;
    if (!fx_open) return;
    for (i = 0; i < 512; i++)
        if (fx_new(fx_was, i) && !fx_mirror[i]) {
            if (eff[i].id == 300 && (eff[i].flg & 0x200000) && k < fxh->pool_n) {   /* a blood pool's first entry: its call */
                Event e = fxh->pool[k++];
                fxh->rec[i] = e;
                fxh->rec_on[i] = FXS_REC;
                if (fx_live) fx_queue(&e, i);
            } else if (fx_open == 1 || fx_hurts(eff[i].id) || eff[i].id == FX_BURST) fx_send(&eff[i], owner, player);
        }
    fx_open = 0;
}

/* A partner that comes into our room (or back from its menus, a door, a scene: not watching) saw nothing of what started
   meanwhile: EV_FX goes once, as the entry starts. Long effects then were missing for their whole life - Alexia 1's fires
   on the floor (263, up to 400 frames) and on her arm (274, 300), her burning blood (265), the blood pools under the dead
   (300, until the room is left). Our windows record those (rec[], also while the partner is elsewhere), and when it watches
   again the ones it does not have go out again (FXS_SENT: they went while it watched; cleared when it leaves the room):
   the blood with its countdown left, the arm's fires with their delay and life left, a pool as its call; a fire as burning
   blood that ignites at once (its copy burns a fresh life: a fire's own init draws it). */
static int fx_fire(Event* e, const O_WRK* op)   /* a fire (263) of our burning blood as that blood, alight at once */
{
    const O_WRK* m = (const O_WRK*)op->exp1;
    int i, k, x1 = 0, own = -1;
    for (i = 0; i < (int)sys->ewk_n && i < PC_ENE_SLOTS && !x1; i++)
        if ((ene[i].flg & 0x1) && ene[i].id == 12 && ene[i].exp0)
            for (k = 0; k < FX_X1_MAX && !x1; k++)
                if (*(const O_WRK**)(ene[i].exp0 + 4 * k) == m) { x1 = k + 1; own = i; }
    if (!x1) return 0;
    memset(e, 0, sizeof(*e));
    e->type = EV_FX; e->stg = sys->stg_no; e->rom = sys->rom_no;
    coop_fx_put(&e->idx, &e->u.fx.refs, FXR_IDX, FX_NONE);
    coop_fx_put(&e->u.fx.owner, &e->u.fx.refs, FXR_OWNER, own);
    coop_fx_put(&e->u.fx.exp0, &e->u.fx.refs, FXR_EXP0, FX_NONE);
    e->u.fx.id = 265; e->u.fx.type = 1; e->u.fx.flg = 1; e->u.fx.tex_id = -1; e->u.fx.txp = e->u.fx.mlw = -1;
    e->u.fx.x1 = (uint8_t)x1;
    for (k = 0; k < 3; k++) {
        float a = (&op->aox)[k];
        e->u.fx.ao[k] = (int16_t)lrintf((a < -1 ? -1 : a > 1 ? 1 : a) * 16384);
        e->u.fx.p[k] = (&op->px)[k] - (op->aoy > 0 ? 0 : 0.5f * a);   /* (en12 FrameLiquid: where its fire stands) */
    }
    e->u.fx.s[0] = e->u.fx.s[2] = 128; e->u.fx.s[1] = 256;
    return 1;
}
static void fx_resync(void)
{
    int i, n = 0;
    for (i = 0; i < 512; i++) {
        O_WRK* op = &eff[i];
        int f = fxh->rec_on[i];
        Event e;
        if (!(op->flg & 0x1) || fx_mirror[i] || (f & FXS_SENT) || !(f & (FXS_REC | FXS_FIRE))) continue;
        if (f & FXS_FIRE) {
            if (op->id != 263 || !op->type || op->mode0 != 2 || !fx_fire(&e, op)) continue;
        } else {
            e = fxh->rec[i];
            if (e.u.fx.id == FX_POOL) { if (op->id != 300) continue; }
            else if (e.u.fx.id != op->id) continue;
            else if (op->id == 265) { if (op->mode0 > 1) continue; if (op->mode0) e.u.fx.ct[0] = op->ct0; }
            else if (op->id == 274) {
                if (op->mode0 > 2) continue;
                if (op->mode0 == 1) e.u.fx.ct[1] = op->ct1;
                else if (op->mode0 == 2) { e.u.fx.ct[1] = 0; e.u.fx.ct[0] = op->ct0; }
            }
        }
        if (coop_check_event(&e, is_host)) continue;
        fx_queue(&e, i);
        n++;
    }
    if (trace && n) fprintf(stderr, "coop: fx resync: %d effects to the partner\n", n);
}
/* each frame, before the frame's datagram */
void fx_tick(void)
{
    static int was;
    int i, w;
    if (!fxh) return;
    if (!have_rx || !partner_here()) for (i = 0; i < 512; i++) fxh->rec_on[i] &= ~FXS_SENT;   /* its copies went with its room */
    w = fx_watching() && in_session_game();
    if (w && !was) fx_resync();
    was = w;
    fx_flush();
}

/* The Bandersnatch (en09) stretches its arm by moving the x offsets of its arm nodes 7..10 in its
   model (objP) during its attacks, outside the motion: a puppet animated from the snapshots kept a
   short arm. The host sends them (FX_ARM) when they change. */
void fx_arm(BH_PWORK* ep)
{
    float (*sent)[4] = fxh->arm;
    int* room = fxh->arm_room;
    int i = ep - ene, k, key = coop_room_key() + 1;
    NJS_CNK_OBJECT* obj;
    Event e;
    if (ep->id != 9 || !ep->mlwP || !ep->mlwP->objP || ep->mlwP->obj_num <= 10) return;
    obj = ep->mlwP->objP + 7;
    for (k = 0; k < 4 && room[i] == key && fabsf(obj[k].pos[0] - sent[i][k]) < 0.01f; k++);
    if (k == 4) return;
    memset(&e, 0, sizeof(e));
    e.type = EV_FX; e.idx = i; e.stg = sys->stg_no; e.rom = sys->rom_no;
    e.u.fxa.id = FX_ARM;
    for (k = 0; k < 4; k++) e.u.fxa.len[k] = sent[i][k] = obj[k].pos[0];
    room[i] = key;
    if (!coop_check_event(&e, is_host)) fx_queue(&e, -1);
}

/* third person, "Locational Hit Reactions" (tps_glue.c): our shot hit enemy slot `slot` there (region rg along d);
   the partner's zombie flinches the same way (FX_REACT, shown by its own setting) */
void tps_react_start(int i, int rg, const float* d);
void coop_react_out(int slot, int rg, const float* d)
{
    Event e;
    if (!coop_on || !have_peer || echo_delay || !sys || !in_session_game() || !same_room()) return;
    memset(&e, 0, sizeof(e));
    e.type = EV_FX; e.idx = slot; e.stg = sys->stg_no; e.rom = sys->rom_no;
    e.u.fxk.id = FX_REACT; e.u.fxk.rg = rg;
    memcpy(e.u.fxk.d, d, sizeof(e.u.fxk.d));
    if (!coop_check_event(&e, is_host)) fx_queue(&e, -1);
}

/* game.c, before bhControlPlayer (coop_control_remote closes it) */
void coop_fx_player(void) { fx_begin(1); }

/* a puppet's step: what its own step would do to the partner's effects on it here. Alexia 1's fires on her arm (274, en12.c
   bhEne12_SetFireBintaEffect 0 at frame 5 of her flame attack, MV03 / MV04: mode0 1, mode2 3 or 4) burn until that motion
   comes round to frame 0, where her step fades the 16 she keeps in her work (act 1: mode0 3). Here they are not in her work
   and her step does not run: they burned their whole 300 frames, 9 s after the partner's Alexia had put them out, and
   filled eff[] in a long fight (the partner's next effects found no free entry). So: when the puppet's frame comes round
   in her attack or as it ends into her walk (MV03 / MV04 at frame 0: mode2 1). */
void fx_puppet(BH_PWORK* ep)
{
    static int was[PC_ENE_SLOTS][2];   /* (by slot: in her flame attack, its frame) */
    int k, i = ep - ene, atk = ep->mode0 == 1 && (ep->mode2 == 3 || ep->mode2 == 4), f = ep->frm_no >> 16;
    if (ep->id == 19) { pc_en19_first(ep); return; }   /* (protocol 58) the Tyrant: what his first step starts (en19.c) */
    if (ep->id != 12 || i < 0 || i >= PC_ENE_SLOTS || !fxh) return;
    k = was[i][0] && f < was[i][1] && (atk || (ep->mode0 == 1 && ep->mode2 == 1));   /* (a hit cuts the attack short: they burn on) */
    was[i][0] = atk;
    was[i][1] = f;
    if (!k) return;
    for (k = 0; k < 16; k++) {   /* (only the last 16: an earlier set her work no longer holds burns its life there too) */
        O_WRK* op = &eff[fxh->arm_fx[i][k]];
        if (fx_mirror[op - eff] && (op->flg & 0x1) && op->id == 274 && op->lkwkp == (const unsigned char*)ep && op->mode0 < 3) op->mode0 = 3;
    }
}

/* test aid RECVX_COOP_FXLIVE=n: every n frames the live eff[] entries per effect number ("id:live/from the partner"), stamped
   with the host's frame, so the two logs compare what each game shows (pc/tools: coopfx); "fire" the burning-blood fires (263,
   type 1/2) whose exp1 is a live fire manager (262): they are drawn by its list (effsub2.c bhEff_E12_FireBurstDraw) */
void fx_live_trace(void)
{
    static int every = -1;
    unsigned short n[402] = { 0 }, m[402] = { 0 };   /* (400, 401: 263 and 265 of a type other than 0 - Alexia's, not the room's) */
    int i, fire = 0, len = 0;
    char buf[2048];
    if (every < 0) every = getenv("RECVX_COOP_FXLIVE") ? atoi(getenv("RECVX_COOP_FXLIVE")) : 0;
    if (every <= 0 || !sys || frame_no % every || !fx_heap()) return;
    for (i = 0; i < 512; i++) {
        const O_WRK* op = &eff[i];
        if (!(op->flg & 0x1) || op->id >= 400) continue;
        n[op->id]++;
        m[op->id] += fx_mirror[i] != 0;
        if ((op->id == 263 || op->id == 265) && op->type) { n[op->id == 263 ? 400 : 401]++; m[op->id == 263 ? 400 : 401] += fx_mirror[i] != 0; }
        if (op->id == 263 && op->type && op->exp1) {
            const O_WRK* f = (const O_WRK*)op->exp1;
            fire += f >= eff && f < eff + 512 && (f->flg & 0x1) && f->id == 262;
        }
    }
    for (i = 0; i < 402 && len < (int)sizeof(buf) - 32; i++)
        if (n[i]) len += snprintf(buf + len, sizeof(buf) - len, " %d:%d/%d", i < 400 ? i : i == 400 ? 1263 : 1265, n[i], m[i]);
    buf[len] = 0;
    fprintf(stderr, "coop: fxlive at host frame %u room %d/%d fire %d:%s\n", test_clock(), sys->stg_no, sys->rom_no, fire, buf);
}

/* the partner's EV_FX: the same entry here, shown only (fx_mirror) */
int apply_fx(const Event* e)
{
    const BH_PWORK *lk, *own, *ex;
    EF_WORK ef;
    NJS_POINT3 off;
    O_WRK *op, *x1p = NULL;
    int k, n, ix = coop_fx_get(e->idx, e->u.fx.refs, FXR_IDX), owner = coop_fx_get(e->u.fx.owner, e->u.fx.refs, FXR_OWNER),
        exp0 = coop_fx_get(e->u.fx.exp0, e->u.fx.refs, FXR_EXP0);   /* (the generic entry's; a particle call has its own refs) */
    if (!sys || !rom || e->stg != sys->stg_no || e->rom != sys->rom_no || !in_session_game() || (sys->cb_flg & 0x1) || !fx_heap()) return 1;   /* another room: gone */
    if (e->u.fx.id != FX_ARM && e->u.fx.id != FX_REACT) {   /* bounded: the partner's effects never take most of eff[] (ours: shots, blasts) - they are cosmetic */
        static unsigned int in_frame, in_n;
        if (in_frame != frame_no) { in_frame = frame_no; in_n = 0; }
        if (in_n >= FX_IN_FRAME) return 1;
        for (k = n = 0; k < 512; k++) n += fx_mirror[k] && (eff[k].flg & 0x3);
        if (n >= FX_IN_MAX) return 1;
        in_n++;
    }
    if (e->u.fx.id == FX_BURST) {
        OR_WORK* o = bhSetRapEff(FX_BURST, (void*)e->u.fxr.w, FX_RAW_WORDS);
        if (o) fx_mirror[(O_WRK*)o - eff] = 1;
        return 1;
    }
    if (e->u.fx.id == FX_REACT) {
        BH_PWORK* w = fx_ref_in(e->idx);
        if (w && w->id == 1) tps_react_start(e->idx, e->u.fxk.rg, e->u.fxk.d);
        return 1;
    }
    if (e->u.fx.id == FX_ARM) {
        BH_PWORK* w = fx_ref_in(e->idx);
        if (w && w->id == 9 && w->mlwP && w->mlwP->objP && w->mlwP->obj_num > 10)
            for (k = 0; k < 4; k++) w->mlwP->objP[7 + k].pos[0] = e->u.fxa.len[k];
        return 1;
    }
    if (e->u.fx.id == FX_POOL) {   /* (protocol 57) a blood pool: the call again here */
        int pix = coop_fx_get(e->idx, e->u.fxb.refs, FXR_IDX);
        BH_PWORK* w = pix == FX_NONE ? NULL : fx_ref_in(pix);
        BP_WORK t;
        NJS_POINT3 p3;
        uint32_t was[16];
        if (pix != FX_NONE && !w) return 1;
        memset(&t, 0, sizeof(t));
        t.srd_dir = e->u.fxb.srd_dir; t.srd_pos = e->u.fxb.srd_pos; t.bld_spd = e->u.fxb.bld_spd;
        for (k = 0; k < 5; k++) { t.srt_spd[k] = e->u.fxb.srt_spd[k]; t.srt_dir[k] = e->u.fxb.srt_dir[k]; }
        memcpy(&p3, e->u.fxb.pos, sizeof(p3));
        fx_slots(was);
        bhSetBloodPoolLnk(w, &p3, e->u.fxb.ay, &t, e->u.fxb.bank);
        for (k = 0; k < 512; k++) if (fx_new(was, k)) fx_mirror[k] = 1;
        if (trace) fprintf(stderr, "coop: fx %d in (blood pool, follows %d)\n", FX_POOL, pix);
        return 1;
    }
    if (e->u.fx.id == FX_CALL) {   /* (protocol 58) the call again here, with our enemy's node */
        BH_PWORK* w = e->idx == 255 && !e->u.fxc.refs ? NULL : fx_ref_in(coop_fx_get(e->idx, e->u.fxc.refs, FXR_IDX));
        ML_WORK* m = w ? w->mlwP : NULL;
        int fn = e->u.fxc.fn, node = e->u.fxc.node, a = e->u.fxc.a;
        NJS_POINT3 p, d;
        uint32_t was[16];
        if (fn != FXN_PTCL2V && (!w || !m || !m->owP || node >= (int)m->obj_num)) return 1;
        if ((fn == FXN_CLAW || fn == FXN_SHAKE) && (w->id != 19 || !w->exp0 || (fn == FXN_CLAW && !m->texP))) return 1;   /* a Tyrant's */
        if (fn == FXN_PTCL && (!m->texP || a >= (int)m->texP->nbTexture)) return 1;
        memcpy(&p, e->u.fxc.v, sizeof(p));
        memcpy(&d, e->u.fxc.v + 3, sizeof(d));
        fx_slots(was);
        switch (fn) {
        case FXN_BLOOD: rySetEffBlood(&m->owP[node].mtx, &p, &d, a); break;
        case FXN_BLOOD2: rySetEffBlood2(&m->owP[node].mtx, &p, &d, a); break;
        case FXN_PTCL: bhEff_SetPtcl(w, &p, a); break;
        case FXN_PTCL2: bhEff_SetPtcl2(w, &p, &m->owP[node].mtx); break;
        case FXN_PTCL2V: bhEff_SetPtcl2V(plp, &p, &d, a); break;
        case FXN_CLAW: pc_en19_claw(w, node, a, e->u.fxc.b, e->u.fxc.v[0], e->u.fxc.v[1]); break;
        case FXN_SHAKE: pc_en19_shake(w, a); break;
        }
        for (k = 0; k < 512; k++) if (fx_new(was, k)) fx_mirror[k] = 1;
        if (trace) fprintf(stderr, "coop: fx %d in (call %d, enemy %d node %d)\n", FX_CALL, fn, e->idx, node);
        return 1;
    }
    if (e->u.fx.id == FX_PARTICLE) {
        int pix = coop_fx_get(e->idx, e->u.fxp.refs, FXR_IDX);
        BH_PWORK* w = pix == FX_NONE ? NULL : fx_ref_in(pix);
        NJS_POINT3 o3, d3;
        uint32_t was[16];
        if (pix != FX_NONE && (!w || !w->mlwP || !w->mlwP->owP || e->u.fxp.lnk >= (int)w->mlwP->obj_num)) return 1;
        memcpy(&o3, e->u.fxp.off, sizeof(o3));
        memcpy(&d3, e->u.fxp.dir, sizeof(d3));
        fx_slots(was);
        if (e->u.fxp.mk2) bhSetEffParticleMk2(w, e->u.fxp.lnk, &o3, &d3, e->u.fxp.col, e->u.fxp.dst, e->u.fxp.typ);
        else bhSetEffParticle(w, e->u.fxp.lnk, &o3, &d3, e->u.fxp.col, e->u.fxp.typ);
        for (k = 0; k < 512; k++) if (fx_new(was, k)) fx_mirror[k] = 1;
        if (trace) fprintf(stderr, "coop: fx %d in (particles %d, follows %d)\n", FX_PARTICLE, e->u.fxp.typ, pix);
        return 1;
    }
    /* (coop_check_event checked it against its FxRule: the effect, its numbers, which references it has;
       here they must name works that are here, with the nodes, models and textures it will index) */
    lk = ix == FX_NONE ? NULL : fx_ref_in(ix);
    own = owner == FX_NONE ? NULL : fx_ref_in(owner);
    ex = exp0 == FX_NONE ? NULL : fx_ref_in(exp0);
    if ((ix != FX_NONE && !lk) || (exp0 != FX_NONE && !ex)) return 1;
    if ((e->u.fx.txp >= 0 || e->u.fx.mlw >= 0) && !own) return 1;
    if ((e->u.fx.txp >= 0 && (e->u.fx.txp >= own->mdl_n || !own->mdl[e->u.fx.txp].texP || e->u.fx.tex_id >= (int)own->mdl[e->u.fx.txp].texP->nbTexture)) ||
        (e->u.fx.mlw >= 0 && (e->u.fx.mlw >= own->mdl_n || !own->mdl[e->u.fx.mlw].objP || e->u.fx.mdlver >= own->mdl[e->u.fx.mlw].obj_num))) return 1;
    if (lk && (!lk->mlwP || !lk->mlwP->owP || e->u.fx.lkono >= (int)lk->mlwP->obj_num)) return 1;   /* (with or without flg 0x80: steps read its nodes) */
    if ((e->u.fx.id == 260 && (owner >= PC_ENE_SLOTS || own->id != 6 || e->u.fx.txp != 1 || own->mdl[1].texP->nbTexture < 5)) ||   /* the moth's powder cycles textures 1..4 */
        (e->u.fx.id == 356 && (ix >= PC_ENE_SLOTS || lk->id != 1 || lk->mlwP->obj_num <= 10)) ||   /* a zombie's head (its node 10) */
        (e->u.fx.id == 397 && (exp0 >= PC_ENE_SLOTS || ex->id != 15 || !ex->exp0)) ||   /* the en15 whose poison it is (its exp0 is read and written) */
        (e->u.fx.x1 && (owner >= PC_ENE_SLOTS || !own || own->id != 12 || !own->exp0))) return 1;   /* (protocol 57) Alexia 1's work: her fire manager */
    if (e->u.fx.x1) {   /* an eff[] entry of ours, alive */
        x1p = *(O_WRK**)(own->exp0 + 4 * (e->u.fx.x1 - 1));
        if (x1p < eff || x1p >= eff + 512 || (const char*)x1p != (const char*)&eff[x1p - eff] || !(x1p->flg & 0x1)) return 1;
    }
    memset(&ef, 0, sizeof(ef));
    ef.flg = e->u.fx.flg;
    ef.id = e->u.fx.id; ef.type = e->u.fx.type; ef.flr_no = e->u.fx.flr_no; ef.mdlver = e->u.fx.mdlver;
    ef.px = e->u.fx.p[0]; ef.py = e->u.fx.p[1]; ef.pz = e->u.fx.p[2];
    ef.sx = e->u.fx.s[0] / 256.0f; ef.sy = e->u.fx.s[1] / 256.0f; ef.sz = e->u.fx.s[2] / 256.0f;
    ef.ax = e->u.fx.ang[0]; ef.ay = e->u.fx.ang[1];
    off.x = e->u.fx.o[0]; off.y = e->u.fx.o[1]; off.z = e->u.fx.o[2];
    if ((k = bhSetEffectTb(&ef, &off, (e->u.fx.flg & 0x80) ? (unsigned char*)lk : NULL, e->u.fx.lkono)) < 0) {
        if (trace) fprintf(stderr, "coop: fx %d not shown here: eff[] full\n", e->u.fx.id);
        return 1;
    }
    op = &eff[k];
    op->lkwkp = (unsigned char*)lk;
    op->ax = e->u.fx.ang[0]; op->ay = e->u.fx.ang[1]; op->az = e->u.fx.ang[2];
    op->stflg = e->u.fx.stflg; op->tex_id = e->u.fx.tex_id;   /* (modes stay 0: its own init runs here) */
    op->xn = e->u.fx.n[0]; op->yn = e->u.fx.n[1]; op->zn = e->u.fx.n[2];
    op->ct0 = e->u.fx.ct[0]; op->ct1 = e->u.fx.ct[1]; op->ct2 = e->u.fx.ct[2]; op->ct3 = e->u.fx.ct[3];
    if (e->u.fx.txp >= 0) op->txp[0] = own->mdl[e->u.fx.txp].texP;
    if (e->u.fx.mlw >= 0) op->mlwP = (ML_WORK*)&own->mdl[e->u.fx.mlw];
    op->exp0 = (unsigned char*)ex;
    if (x1p) op->exp1 = (unsigned char*)x1p;
    if (op->id == 274 && owner < PC_ENE_SLOTS && own && own->id == 12) fxh->arm_fx[owner][fxh->arm_fx_n[owner]++ & 15] = (short)k;   /* (fx_puppet) */
    if (coop_fx_rule(op->id) && (coop_fx_rule(op->id)->refs & FXF_AO)) { op->aox = e->u.fx.ao[0] / 16384.0f; op->aoy = e->u.fx.ao[1] / 16384.0f; op->aoz = e->u.fx.ao[2] / 16384.0f; }
    fx_mirror[k] = 1;
    if (op->id == 356 && lk && lk->id == 1 && lk->exp0 && !(lk->flg & 0x80))   /* en01.c's head burst: the zombie's head is gone here too */
        *(int*)(lk->exp0 + 0x78) = -1;                                          /* (its exp0 is not in the snapshots; "Head blow-offs") */
    if (owner < PC_ENE_SLOTS) {   /* its sounds from the bank of the enemy type that made it (effect.c, eneset.c) */
        extern short pc_eff_owner[512][2];
        int pc_enemy_sound_type(int slot);
        pc_eff_owner[k][0] = (short)pc_enemy_sound_type(owner);
        pc_eff_owner[k][1] = op->id + 1;
    }
    if (trace) fprintf(stderr, "coop: fx %d in (slot %d, follows %d)\n", op->id, k, ix);
    return 1;
}

/* effect.c bhControlEffect, around an entry's step (0 before: 1 = call again after it). A
   projectile or blast started from the partner's EV_FX: its hits on our enemies and our player
   are undone and the blasts it starts dropped (the partner sends its own). One of ours: its blast
   goes to the partner. */
#define fx_keep_ply (fxh->keep_ply)
#define fx_keep_ene (fxh->keep_ene)
static uint32_t fx_step_was[16];
static int fx_step_kind, fx_step_n, fx_step_i;
int coop_fx_step(int i, int after)
{
    O_WRK* op = &eff[i];
    int k;
    if (!after) {
        fx_step_kind = 0;
        if (op->id == 265 && fxh && (fxh->rec_on[i] & FXS_REC) && op->type && op->mode0 == 1 && !op->ct0) {   /* our burning blood ignites: its fire */
            fx_slots(fx_step_was);
            fx_step_i = i;
            return fx_step_kind = 3;
        }
        if (!fx_hurts(op->id) || echo_delay || !fxh) return 0;   /* (no heap: nothing mirrored yet) */
        if (fx_mirror[i]) {
            fx_step_n = sys->ewk_n < PC_ENE_SLOTS ? sys->ewk_n : PC_ENE_SLOTS;
            fx_keep_ply = *plp;
            memcpy(fx_keep_ene, ene, fx_step_n * sizeof(BH_PWORK));
            fx_slots(fx_step_was);
            fx_step_kind = 1;
            return 1;
        }
        fx_begin(2);
        return fx_open ? (fx_step_kind = 2) : 0;
    }
    if (fx_step_kind == 1) {
        *plp = fx_keep_ply;
        memcpy(ene, fx_keep_ene, fx_step_n * sizeof(BH_PWORK));
        for (k = 0; k < 512; k++)
            if (fx_new(fx_step_was, k)) { if (fx_hurts(eff[k].id)) eff[k].flg = 0; else fx_mirror[k] = 1; }
    } else if (fx_step_kind == 2) fx_end(plp, 0);
    else if (fx_step_kind == 3)   /* (the partner has it if it has the blood: its copy lit its own) */
        for (k = 0; k < 512; k++)
            if (fx_new(fx_step_was, k) && eff[k].id == 263) fxh->rec_on[k] = FXS_FIRE | (fxh->rec_on[fx_step_i] & FXS_SENT);
    fx_step_kind = 0;
    return 0;
}
