/* Enemy variants in the game (docs/ENEMIES.md "Enemy variants"; the table: pc/src/host/variants.c).
 *
 * A variant is spawned as its base type (spawn_glue.c spawn_at, the note's `variant`) and remembered by
 * its slot. Then, only while a room has one (pc_variant_n), hooks in the decomp apply it:
 *  - eneset.c bhControlEnemy around its step (pc_variant_pre/post): the weapon damage it is about to
 *    take scaled (ep->dam[], which bhEne_CalcDamage sums into total_dam: armour / hp), the player's hit
 *    points the step took scaled (damage), and the steps per frame (speed);
 *  - eneset.c bhDrawEnemy (pc_variant_draw): another model ("You appear as" catalogue) posed from the
 *    base's skeleton every time it is drawn (actor_glue.c retargeting, display slots ACTOR_VARIANT + k,
 *    one per model in the room), with sub-models on its nodes (attach), and the drawn size (scale);
 *  - sdfunc.c RequestEnemySeBasic (pc_variant_sound): sound programs of the base played from another
 *    room's enemy sound bank;
 *  - its motion set: a copy of the base's with motions swapped (motions), set at the spawn;
 *  - motions of another enemy (the donor: its model's skeleton and motion set, read from its room file
 *    as the spawner reads enemies, no textures): while the base plays a mapped motion, and when it dies
 *    (death: the donor's motions in turn, the last held), the donor's skeleton is posed in that motion at
 *    the enemy's place and the base's own model (or `model`) is drawn retargeted from it.
 * Its parts and children (linked to it) count as the variant. Everything is keyed by the variant id,
 * which co-op spawn notes carry, so a guest makes the same enemy.
 */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/MdlPut.h"
#include "../../decomp/include/ps2/veronica/prog/Motion.h"
#include "../../decomp/include/ps2/veronica/prog/binfunc.h"
#include "../../decomp/include/ps2/veronica/prog/dread.h"
#include "../src/host/actors.h"
#include "../src/host/enemies.h"
#include "../src/host/variants.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void host_log(const char* fmt, ...);
void RequestEnemySe(int EnemyNo, NJS_POINT3* pPos, int SeNo);   /* sdfunc.c */
int snd_xbank_load(int key);
extern int enemy_room_gen;              /* enemyopt_glue.c */

#define VSLOTS (ACTOR_ENEMY - ACTOR_VARIANT)
#define VSETS 4                         /* variants with their own motion set in one room */

int pc_variant_n;                       /* variant enemies in this room (+ 1 while "Enemy Models" draws some: enemymdl_glue.c, through
                                           pc_variant_draw; the other hooks find no variant then) */
static unsigned short slot_var[PC_ENE_SLOTS];    /* slot -> variant id (the variant's body) */
static float steps[PC_ENE_SLOTS], dam_left[PC_ENE_SLOTS];
static int death_t[PC_ENE_SLOTS];                /* ticks since it died (flg & 2), -1 alive */
static short vmodel[VSLOTS] = { -1, -1 };   /* catalogue model shown in variant display slot k */
static struct { unsigned short id; MN_WORK set[512]; } mset[VSETS];
static struct { unsigned short id; signed char bank; } vbank[VARIANT_MAX];
static int nbank;
static int pre_hp, hurt_now;
static unsigned char head_logged[PC_ENE_SLOTS];   /* "Enemy Models": a blown-off head's log line once per enemy and room */
static BH_PWORK* pre_pl;

static BH_PWORK* root(BH_PWORK* ep)
{
    int k;
    for (k = 0; k < 3 && ep->lkwkp && (BH_PWORK*)ep->lkwkp >= ene && (BH_PWORK*)ep->lkwkp < ene + PC_ENE_SLOTS; k++) ep = (BH_PWORK*)ep->lkwkp;
    return ep;
}

/* the variant ep belongs to (its own, or its parent's), NULL */
static const EnemyVariant* of(BH_PWORK* ep)
{
    const EnemyVariant* v;
    BH_PWORK* r;
    if (ep < ene || ep >= ene + PC_ENE_SLOTS) return NULL;
    r = root(ep);
    if (!slot_var[r - ene] || !(r->flg & 0x1)) return NULL;   /* (a body keeps its slot until the room is left) */
    v = variant_by_id(slot_var[r - ene]);
    return v && v->type == r->id ? v : NULL;
}

static int model_slot(int model)
{
    int k, at = -1;
    for (k = 0; k < VSLOTS; k++) {
        if (vmodel[k] >= 0 && !actor_shown(ACTOR_VARIANT + k)) vmodel[k] = -1;   /* njReleaseTextureAll unloaded it (game over, the title) */
        if (vmodel[k] == model && model >= 0) at = k;
    }
    return at;
}

/* the "You appear as" catalogue entry of v's base (its own look, drawn posed from a donor), -1 */
static int self_model(const EnemyVariant* v)
{
    int i;
    for (i = 0; i < ACTOR_MODELS; i++)
        if (actor_models[i].id == v->type && actor_models[i].mdlver == v->mdlver) return i;
    return -1;
}

/* the model v is drawn as when it is not drawn as its base: `model`, else its own for the donor's motions */
static int drawn_model(const EnemyVariant* v) { return v->model >= 0 ? v->model : v->donor_type ? self_model(v) : -1; }

/* ---------------------------------------------------------------- the donor
   Another enemy's skeleton (its first sub-model's nodes) and motion set, from the room file the spawner reads it
   from (actor_find), realized in a buffer of our own; no textures (it is never drawn). One donor at a time,
   kept until another is needed. */
#define DONOR_SIZE (2 << 20)
static unsigned char dbuf[DONOR_SIZE] __attribute__((aligned(64)));
static int donor_key = -1;
static O_WORK dow[64] __attribute__((aligned(16)));
static MN_WORK dmth[512];
static BH_PWORK dw;

const ActorModel* spawn_entry(int i);
int spawn_base_count(void);
static const ActorModel* entry_of(int type, int ver)
{
    int i;
    for (i = 0; i < spawn_base_count(); i++)
        if (spawn_entry(i)->id == type && spawn_entry(i)->mdlver == ver) return spawn_entry(i);
    return NULL;
}

static int donor_load(int type, int ver)
{
    const ActorModel* am = entry_of(type, ver);
    ActorData a;
    unsigned char *p, *mtn;
    int key = type << 8 | ver, mofs, nofs, dt, i;
    if (key == donor_key) return 1;
    donor_key = -1;
    if (!am || !actor_find(am, &a) || !a.mtn) return 0;
    mofs = (int)((unsigned int)a.mdl & 63);   /* (the data's alignment kept) */
    nofs = ((mofs + a.mlen + 63) & ~63) + (int)((unsigned int)a.mtn & 63);
    if (nofs + a.nlen > DONOR_SIZE) return 0;
    memcpy(dbuf + mofs, a.mdl, a.mlen);
    memcpy(dbuf + nofs, a.mtn, a.nlen);
    memset(&dw, 0, sizeof(dw));
    memset(dmth, 0, sizeof(dmth));
    for (p = dbuf + mofs; (dt = *(int*)p) != -1; p += 4 + (dt > 0 ? dt : 0))   /* its first model (not a face mask, not skin) */
        if (dt > 0 && *(int*)(p + 4) != 0x4B53414D && *(int*)(p + 4) != SKIN_MAGIC) { bhMlbBinRealize(p + 4, &dw.mdl[0]); break; }
    if (!dw.mdl[0].objP || dw.mdl[0].obj_num > 64) return 0;
    dw.mdl[0].owP = dow;
    dw.mlwP = &dw.mdl[0];
    dw.mdl_n = 1;
    dw.mtx = (float(*)[16])dw.mtxbuf;
    dw.flg = 0x1 | 0x1000;   /* bhCalcModel: the base matrix as set (the enemy's) */
    for (i = 0, mtn = dbuf + nofs; (dt = *(int*)mtn) != -1 && i < 512; i++, mtn += 4 + dt)
        if (dt) bhMnbBinRealize(mtn + 4, &dmth[i]);
    dw.mnwP = dw.mnwPb = dmth;
    donor_key = key;
    host_log("variant: donor %s (type %d model %d) loaded: %d nodes, %d KB of motions\n", am->name, type, ver, (int)dw.mdl[0].obj_num, a.nlen >> 10);
    return 1;
}

/* the donor posed in its motion m at frame frm (16.16, clamped to the motion) at enemy r's place; NULL: none */
static BH_PWORK* donor_pose(const BH_PWORK* r, int m, int frm)
{
    int last;
    if (m < 0 || m >= 512 || !dmth[m].datP || dmth[m].obj_num > dw.mdl[0].obj_num) return NULL;
    last = ((int)dmth[m].frm_num - 1) << 16;
    dw.mtn_no = (unsigned int)m;
    dw.frm_no = frm < 0 ? 0 : frm > last ? last : frm;
    dw.hokan_count = 0;
    dw.mtn_md = 0;
    bhSetMotion(&dw, 0, 0, NULL);
    dw.px = r->px; dw.py = r->py; dw.pz = r->pz;
    dw.ax = r->ax; dw.ay = r->ay; dw.az = r->az;
    dw.draw_tp = r->draw_tp;
    memcpy(dw.mtxbuf, r->mtx, sizeof(float) * 16);
    bhCalcModel(&dw);
    return &dw;
}

/* what enemy r's body shows from the donor now: its death (in turn, the last one's end held), or a mapped motion at
   the base's phase; NULL: its own */
static BH_PWORK* donor_now(const EnemyVariant* v, BH_PWORK* r)
{
    int s = (int)(r - ene), i;
    if (!v->donor_type || donor_key != (v->donor_type << 8 | v->donor_mdlver)) return NULL;
    if (v->death_n && (r->flg & 0x2)) {
        int t = death_t[s] < 0 ? 0 : death_t[s];
        for (i = 0; i < v->death_n; i++) {
            int m = v->death[i], len = m >= 0 && m < 512 && dmth[m].datP ? (int)dmth[m].frm_num - 1 : 0;
            if (t < len || i == v->death_n - 1) return donor_pose(r, m, t << 16);
            t -= len;
        }
    }
    if (r->mnwP == r->mnwPb && r->mnwP)
        for (i = 0; i < v->dmotion_n; i++)
            if (r->mtn_no == (unsigned int)v->dmotion[i][0]) {
                int m = v->dmotion[i][1], own = (int)r->mnwP[r->mtn_no].frm_num;
                long f = own > 0 && m < 512 ? (long)r->frm_no * dmth[m].frm_num / own : r->frm_no;   /* (long: 64 bits, pc_prelude.h) */
                return donor_pose(r, m, (int)f);
            }
    return NULL;
}

/* room load (enemyopt_glue.c enemy_room_finished, before the randomizer) */
void variant_room(void)
{
    memset(slot_var, 0, sizeof(slot_var));
    memset(head_logged, 0, sizeof(head_logged));
    memset(mset, 0, sizeof(mset));
    nbank = 0;
    pc_variant_n = 0;
}

/* the models go with the room's textures (spawn_glue.c spawn_release: bhReleaseMainTexture) */
void variant_release(void)
{
    int k;
    for (k = 0; k < VSLOTS; k++)
        if (vmodel[k] >= 0) { actor_set(ACTOR_VARIANT + k, -1); vmodel[k] = -1; }
}

/* spawn_glue.c spawn_at: slot `slot` now holds variant `id` (its base type realized there) */
void variant_spawned(int slot, int id)
{
    const EnemyVariant* v = variant_by_id(id);
    BH_PWORK* ep = &ene[slot];
    const char* look = "its base's model";
    int k, i, dm = v ? drawn_model(v) : -1;
    if (!v || ep->id != v->type) {
        host_log("variant: slot %d: %s %d, it plays as its base type %d\n", slot, v ? "not the base type of variant" : "unknown variant", id, ep->id);
        return;
    }
    slot_var[slot] = (unsigned short)id;
    steps[slot] = dam_left[slot] = 0;
    death_t[slot] = -1;
    pc_variant_n++;
    if (v->donor_type && !donor_load(v->donor_type, v->donor_mdlver))
        host_log("variant: %s: donor type %d model %d not loaded: its own motions only\n", v->name, v->donor_type, v->donor_mdlver);
    if (dm >= 0) {
        if ((k = model_slot(dm)) < 0)
            for (k = 0; k < VSLOTS && vmodel[k] >= 0; k++);
        if (k == VSLOTS) look = "no display slot left: its base's model";
        else if (vmodel[k] == dm) look = actor_models[dm].name;
        else if (actor_set(ACTOR_VARIANT + k, dm) && actor_rigged(ACTOR_VARIANT + k)) { vmodel[k] = (short)dm; look = actor_models[dm].name; }
        else { actor_set(ACTOR_VARIANT + k, -1); look = "its model could not be loaded or mapped: its base's"; }
        if (v->model < 0 && k < VSLOTS && vmodel[k] == dm) look = "its own (posed from the donor for the donor's motions)";
    }
    if (v->motion_n && ep->mnwPb) {   /* its own copy of the set, motions swapped */
        for (k = 0; k < VSETS && mset[k].id && mset[k].id != id; k++);
        if (k < VSETS) {
            if (!mset[k].id) {
                memcpy(mset[k].set, ep->mnwPb, sizeof(mset[k].set));
                for (i = 0; i < v->motion_n; i++) mset[k].set[v->motion[i][0]] = ep->mnwPb[v->motion[i][1]];
                mset[k].id = (unsigned short)id;
            }
            ep->mnwP = ep->mnwPb = mset[k].set;
        } else host_log("variant: %s: no motion set left, the base's motions\n", v->name);
    }
    if ((v->sound_n || v->hurt) && v->sound_room) {
        for (k = 0; k < nbank && vbank[k].id != id; k++);
        if (k == nbank && nbank < VARIANT_MAX) {
            vbank[nbank].id = (unsigned short)id;
            vbank[nbank++].bank = (signed char)snd_xbank_load(v->sound_room);
        }
    }
    host_log("variant: slot %d is %s (variant %d, type %d model %d): drawn as %s, hp x%.2f, armour x%.2f, damage x%.2f, speed x%.2f, scale x%.2f\n",
             slot, v->name, id, ep->id, ep->mdlver, look, v->hp, v->armour, v->damage, v->speed, v->scale);
}

/* ---------------------------------------------------------------- around its step */
int pc_variant_pre(BH_PWORK* ep, int slot)
{
    const EnemyVariant* v = of(ep);
    int runs = 1, i;
    pre_pl = NULL;
    if (!v) return 1;
    if ((ep->flg & 0x4) && (v->armour != 1.0f || v->hp != 1.0f) && ep->mlwP) {   /* the hit it is about to take */
        float f = v->armour / v->hp;
        for (i = 1; i < (int)ep->mlwP->obj_num && i < 64; i++)
            if (ep->dam[i] > 0) {
                float x = ep->dam[i] * f + dam_left[slot];
                int d = (int)x;
                dam_left[slot] = x - d;
                ep->dam[i] = d > 0 || f == 0 ? d : 1;   /* (a joint hit stays a hit for the combo counts) */
            }
    }
    if ((ep->flg & 0x4) && v->hurt && ep->hp >= 0 && ep == root(ep)) {   /* its own hurt cry (a bank 3 voice, as en17.c's) */
        for (i = 0; i < nbank && vbank[i].id != v->id; i++);
        if (i < nbank && vbank[i].bank > 0) {
            hurt_now = vbank[i].bank;
            RequestEnemySe(slot, (NJS_POINT3*)&ep->px, 0x1002300 | v->hurt);
            hurt_now = 0;
        }
    }
    if (v->speed != 1.0f) {
        steps[slot] += v->speed;
        runs = (int)steps[slot];
        if (!runs && (ep->flg & 0x4)) runs = 1;   /* a hit is not skipped */
        steps[slot] -= runs;
    }
    if (v->damage != 1.0f && plp) { pre_pl = plp; pre_hp = plp->hp; }
    return runs;
}

/* after its step (a co-op puppet's too: its death plays from the dead flag the host sends) */
void pc_variant_post(BH_PWORK* ep, int slot)
{
    const EnemyVariant* v = of(ep);
    BH_PWORK* pl = pre_pl;
    pre_pl = NULL;
    if (!v) return;
    if (v->death_n && ep == root(ep)) death_t[slot] = (ep->flg & 0x2) ? death_t[slot] + 1 : -1;   /* the ticks of its death */
    if (pl && pl == plp && plp->hp < pre_hp && plp->hp >= 0) {   /* (a blow the game made fatal stays fatal) */
        int hp = pre_hp - (int)lroundf((pre_hp - plp->hp) * v->damage);
        plp->hp = hp < 0 ? -1 : hp;
    }
}

/* the decomp asks: ep (or the enemy it belongs to) is an enemy variant (en17.c: Monster Steve's killable ways) */
int pc_variant_is(BH_PWORK* ep) { return of(ep) != NULL; }

/* ---------------------------------------------------------------- drawing */
/* its node matrices scaled by s about (x, y, z), or back (1 / s) */
static void scale_nodes(ML_WORK* m, float s, float x, float y, float z)
{
    int i, j;
    for (i = 0; m && m->owP && i < (int)m->obj_num; i++) {
        float* w = m->owP[i].mtx;
        for (j = 0; j < 12; j++) if ((j & 3) != 3) w[j] *= s;
        w[12] = x + (w[12] - x) * s; w[13] = y + (w[13] - y) * s; w[14] = z + (w[14] - z) * s;
    }
}

/* sub-model `sub` of display work w drawn on its node `node` (as Monster Steve's arms, linked enemies) */
static void attach_draw(BH_PWORK* w, int sub, int node)
{
    static BH_PWORK a;
    ML_WORK* m = w->mlwP;
    if (sub >= w->mdl_n || !w->mdl[sub].objP || !w->mdl[sub].owP || node >= (int)m->obj_num) return;
    memset(&a, 0, sizeof(a));
    a.flg = 0x1;
    a.mdl[0] = w->mdl[sub];
    a.mlwP = &a.mdl[0];
    a.mtx = (float(*)[16])a.mtxbuf;
    a.draw_tp = w->draw_tp;
    memcpy(a.mtxbuf, m->owP[node].mtx, sizeof(float) * 16);
    bhCalcTree((NJS_MATRIX*)a.mtx, a.mlwP);
    bhPutModel(&a);
}

/* ---------------------------------------------------------------- head blow-offs
   en01.c destroys a zombie's head (bhEne01_DmgCheckType00 -> exp0 + 0x78 = -1, the burst effect 356 at
   the neck; bhEne01_CutHead) but every zombie look is one skinned mesh on node 1, and nothing hides the
   head: the PS2's skin cutter npCutSkin is empty. Drawn here without it: the rotation of its head node
   (11, the node the game's head damage and look-at use) and of the nodes below it is zeroed while it is
   drawn, so the vertices the head bone carries meet at the head joint and the neck closes into a stump
   (the same for every look: all share the 18-node zombie skeleton). A variant drawn as another model:
   that model's head role (actor rig). The game's state is untouched (the matrices are put back after
   the draw); off (setting "Head blow-offs"): as the original. */
int pc_head_blowoff_on(void);           /* host.c */
#define HEAD_NODES 128   /* (an imported head may carry many nodes: hair, jaw, eyes) */
static float head_rot[HEAD_NODES][9];
static float* head_mtx[HEAD_NODES];
static int head_n;

static void head_hide_node(ML_WORK* m, NJS_CNK_OBJECT* o, int top)
{
    int k = (int)(o - m->objP), j;
    if (k < 0 || k >= (int)m->obj_num || head_n >= HEAD_NODES) return;
    head_mtx[head_n] = m->owP[k].mtx;
    for (j = 0; j < 9; j++) { head_rot[head_n][j] = m->owP[k].mtx[j + j / 3]; m->owP[k].mtx[j + j / 3] = 0.0f; }
    head_n++;
    if (o->child) head_hide_node(m, o->child, 0);
    if (!top && o->sibling) head_hide_node(m, o->sibling, 0);
}

static void head_hide(ML_WORK* m, int node)
{
    head_n = 0;
    if (m && m->objP && m->owP && node >= 0 && node < (int)m->obj_num) head_hide_node(m, &m->objP[node], 1);
}

static void head_show(void)
{
    int i, j;
    for (i = 0; i < head_n; i++)
        for (j = 0; j < 9; j++) head_mtx[i][j + j / 3] = head_rot[i][j];
    head_n = 0;
}

/* r (a zombie's body) has lost its head, and it is not the head flying off (en01b.c: the head part, exp0 + 0x10, in its mode 3) */
static int headless(BH_PWORK* r)
{
    BH_PWORK* part;
    if (!pc_head_blowoff_on() || r < ene || r >= ene + PC_ENE_SLOTS || r->id != 1 || (r->flg & 0x80) || !r->exp0 || *(int*)(r->exp0 + 0x78) != -1) return 0;
    part = *(BH_PWORK**)(r->exp0 + 0x10);
    return !(part >= ene && part < ene + PC_ENE_SLOTS && part->mode0 == 3);
}

/* MdlPut.c bhPutModel, around drawing ep: hide 1 = its head hidden for the draw (1 returned: the draw is
   wrapped), 0 = put back */
extern int actor_fp_nohead;             /* tps_glue.c tps_fp_hide: first person, around our player's draw */
int actor_neck_node(int who);           /* actor_glue.c */
int pc_zombie_head(BH_PWORK* ep, int hide)
{
    if (!hide) { head_show(); return 0; }
    if (actor_fp_nohead && plp && (ep == plp || ep == actor_work(0))) {   /* first person: our own neck and head, whichever
        path draws us (the game's model, "You appear as" another, a fallback to the game's): the view is inside them */
        head_hide(ep->mlwP, ep == plp ? 4 : actor_neck_node(0));
        return head_n > 0;
    }
    if (ep < ene || ep >= ene + PC_ENE_SLOTS || ep->id != 1 || !headless((ep->flg & 0x80) ? (BH_PWORK*)ep->lkwkp : ep) ||
        !ep->mlwP || ep->mlwP->obj_num <= 11)
        return 0;
    head_hide(ep->mlwP, 11);
    return head_n > 0;
}

/* "Enemy Models" (enemymdl_glue.c): enemy r (a body, not a part) drawn as display slot who's model, posed from it; its blown-off head
   hidden on the model as for a variant. 0: not drawn (no humanoid node map for r's skeleton): r is drawn itself */
int variant_put(int who, BH_PWORK* ep, BH_PWORK* r)
{
    BH_PWORK* w;
    actor_pose(who, r);
    if (!actor_mapped(who) || !(w = actor_work(who))) return 0;
    w->draw_tp = ep->draw_tp;
    if (headless(r)) {
        head_hide(w->mlwP, actor_head_node(who));
        if (!head_logged[r - ene]) { head_logged[r - ene] = 1; host_log("enemymdl: slot %d lost its head: the model's head node %d and below hidden\n", (int)(r - ene), actor_head_node(who)); }
    }
    bhPutModel(w);
    head_show();
    actor_extras_draw(who, r);
    return 1;
}

int emodel_draw(BH_PWORK* ep);          /* enemymdl_glue.c */
int pc_variant_draw(BH_PWORK* ep)
{
    const EnemyVariant* v = of(ep);
    BH_PWORK *r, *src;
    int k, i, dm;
    if (!v) return emodel_draw(ep);   /* (not a variant: "Enemy Models"' choice for its type, if any) */
    r = root(ep);
    src = donor_now(v, r);
    dm = src ? drawn_model(v) : v->model;
    if (!src) src = r;
    if (dm >= 0 && (k = model_slot(dm)) >= 0) {
        BH_PWORK* w;
        if (ep != r) return (ep->flg & 0x80) != 0;   /* its body's parts: the model stands in for all of them */
        actor_pose(ACTOR_VARIANT + k, src);
        if (!(w = actor_work(ACTOR_VARIANT + k))) return 0;
        w->draw_tp = ep->draw_tp;
        if (v->scale != 1.0f) scale_nodes(w->mlwP, v->scale, ep->px, ep->py, ep->pz);
        if (headless(r)) head_hide(w->mlwP, actor_head_node(ACTOR_VARIANT + k));   /* a zombie base's blown-off head: the model's head */
        bhPutModel(w);
        head_show();
        for (i = 0; i < v->attach_n; i++) attach_draw(w, v->attach[i][0], v->attach[i][1]);
        return 1;
    }
    if (v->scale == 1.0f) return 0;
    scale_nodes(ep->mlwP, v->scale, r->px, r->py, r->pz);
    bhPutModel(ep);
    scale_nodes(ep->mlwP, 1.0f / v->scale, r->px, r->py, r->pz);
    return 1;
}

/* ---------------------------------------------------------------- sounds */
int pc_variant_sound(int enemy_no, int se, signed char* bank)
{
    static int trace = -1;
    const EnemyVariant* v;
    int k, i;
    if (enemy_no < 0 || enemy_no >= PC_ENE_SLOTS || !(v = of(&ene[enemy_no]))) return se;   /* (PC_ENE_SLOTS: an effect's request) */
    if (trace < 0) trace = getenv("RECVX_TRACE_SND") != NULL;
    if (trace)   /* for sound_map: which of its base's programs it plays when (RECVX_TRACE_SND) */
        host_log("variant: slot %d (%s) motion %d frame %d requests sound %#x (program %d)\n", enemy_no, v->name, ene[enemy_no].mtn_no,
                 ene[enemy_no].frm_no >> 16, se, se & 0xFF);
    if (hurt_now) { *bank = (signed char)hurt_now; return se; }
    if (!v->sound_n) return se;
    for (k = 0; k < nbank && vbank[k].id != v->id; k++);
    if (k == nbank || vbank[k].bank <= 0) return se;
    for (i = 0; i < v->sound_n; i++)
        if ((se & 0xFF) == v->sound[i][0]) { *bank = vbank[k].bank; return (se & ~0xFF) | v->sound[i][1]; }
    return se;
}

/* ---------------------------------------------------------------- the spawner and the randomizer */
/* the spawner entry of v's base (its type and model version), NULL: none */
const ActorModel* spawn_entry(int i);
int spawn_base_count(void);
const ActorModel* variant_base(const EnemyVariant* v)
{
    int i;
    for (i = 0; i < spawn_base_count(); i++)
        if (spawn_entry(i)->id == v->type && spawn_entry(i)->mdlver == v->mdlver) return spawn_entry(i);
    return NULL;
}

int variant_of_slot(int slot) { return slot >= 0 && slot < PC_ENE_SLOTS && (ene[slot].flg & 0x1) ? slot_var[slot] : 0; }
