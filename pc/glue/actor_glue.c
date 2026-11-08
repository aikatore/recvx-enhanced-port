/* Any model as a player's appearance ("You appear as", docs/ACTORS.md "You appear as"; catalogue in
 * pc/src/host/actors.c).
 *
 * A model is read from the room file that carries it (or a player part file), into buffers of
 * our own, and drawn as a "display actor" in place of a source actor: our player, the co-op
 * partner, or in cutscenes the event double of the character played (the story cast's NPC
 * models that act for the player in in-engine cutscenes). The source stays what the game
 * animates, collides with and links weapons and effects to; only its drawing is replaced.
 *
 * Every tick the display actor is posed from the source:
 *  - humanoid rigs (all bind angles 0, a T-pose like the player's): a node map found at load
 *    from the rest skeleton (pelvis, spine, chest, neck, head, clavicle / upper arm / forearm /
 *    hand, hip / knee / ankle / toe) and, per mapped node, the source's final world rotation
 *    (aim pitch and all) times a rest correction that turns the model's rest bone direction onto
 *    the source's. Positions come from the model's own bone offsets; nodes without a map follow
 *    their parent rigidly (fingers, claws, face, tentacles). The weapon hands of the source are
 *    moved to the model's hands. Limbs are mapped by where the player's joints sit along each
 *    chain, the root's height follows the source's in proportion and the feet stand as high as its (docs/ACTORS.md
 *    "Retargeting"; RECVX_RETARGET=0: the previous mapping).
 *  - everything else: its own motion set's idle motion, looped (or its rest pose).
 * Textures go to the texture heap (shared by TIM2 index with a model the room already shows) and
 * are released when the model changes.
 */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/system.h"
#include "../../decomp/include/ps2/veronica/prog/dread.h"
#include "../../decomp/include/ps2/veronica/prog/binfunc.h"
#include "../../decomp/include/ps2/veronica/prog/njplus.h"
#include "../../decomp/include/ps2/veronica/prog/expand.h"
#include "../../decomp/include/ps2/veronica/prog/Motion.h"
#include "../../decomp/include/ps2/veronica/prog/MdlPut.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_texture.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_NaTextureFunction.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_dvd_image.h"
void njUnitMatrix(NJS_MATRIX* m);
void njRotateXYZ(NJS_MATRIX* m, Angle angx, Angle angy, Angle angz);
#include "../../decomp/include/ps2/veronica/prog/face.h"
#include "../../decomp/include/ps2/veronica/prog/pwksub.h"
#include "../../decomp/include/ps2/veronica/prog/objitm.h"
#include "../src/host/actors.h"
#include "../src/host/tps.h"
#include "../src/host/placer.h"
#include "../src/import/impmodel.h"
#include "../include/pc_parse.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void host_log(const char* fmt, ...);
FILE* pc_fopen(const char* utf8_path, const char* mode);   /* host/fileio.h */
void bhArmIkMdk(BH_PWORK* ewP, int bas_no, NJS_POINT3* effP, int rot);   /* playpch.c */
Sint32 ADXF_ReadNw32(ADXF adxf, Sint32 nsct, void* buf);   /* PC: synchronous */
extern NJS_TEXMEMLIST* Ps2_tex_info;
extern unsigned int Ps2_texmemlist_num;
extern void *Ps2_now_free, *Ps2_free_last;
extern ETTY_WORK lkmtab[2];             /* player.c: Claire's hair (1211), Wesker's piece (1212) */

#define KEEP_SIZE (1536 * 1024)         /* model + motion chunks (largest in the catalogue: 0.2 + 0.96 MB) */
#define OW_MAX 256                      /* O_WORKs of all sub-models */
#define NODES 64                        /* nodes of a mapped body (largest: 45) */
#define TEX_MARGIN 32                   /* texture entries left free for the rooms to come */
#define HEAP_MARGIN (2 << 20)           /* texture heap bytes left free */
#define MAGIC_MASK 0x4B53414D           /* "MASK": face mask data of the cast (lip sync; the cutscene double's face, face_init) */

/* a humanoid skeleton: player node (0..21) -> node, parents, rest positions (model space) */
typedef struct Rig {
    signed char map[22];
    signed char rot[22];                /* the node whose rotation turns role k's bone (rig_rot): map[k], or past a zero-length child */
    signed char par[NODES];
    float rest[NODES][3];
    int n, ok;
} Rig;

typedef struct Disguise {
    O_WORK ow[OW_MAX];                  /* first: 16-byte aligned */
    MN_WORK mth[512];
    unsigned char* keep;                /* the model's chunk list, then its type's motion list: keep_big's (KEEP_SIZE), or
                                           a cast slot's piece of cast_mem (keep_fit) */
    int keep_cap;
    BH_PWORK w;                         /* the display actor */
    int model, fail, tex_gone, posed, mapped;   /* mapped: the last pose came from the source's skeleton */
    int face;                           /* faces in its face mask (face_init; 0 none): lip sync mirrored from the double */
    int lip, lip_max;                   /* the double's lip data (a line) and its highest phoneme */
    signed char src_hand[2];            /* the last pose's source hand nodes (right, left; -1 none) */
    signed char src_head[2];            /* and its neck and head nodes (-1 none) */
    NJS_TEXLIST* tex[16];               /* texlists we uploaded */
    int tex_n;
    Rig rig;                            /* rig.ok: posed from the source's skeleton */
    const CvxmSkin* skin;               /* an imported model (.cvxm): its vertices skinned every pose (imp_skin) */
    float* vtx;                         /* its vertex chunk's data (position, normal per vertex) */
    int nv;
    float hand_dir[3];                  /* its left hand: wrist to fingertips at rest (the two-handed grip) */
    signed char fbone[IMP_FACES - 1];   /* its face bones (IMP_FACE_* - 1: lids, jaw), -1 none */
    int blink, blink_wait;              /* the blink: step 0..7 of it (-1 none), ticks since the last */
    float lid_travel[2];                /* how far each upper lid goes down to close (imp_face) */
    float talk;                         /* the jaw (degrees) from the player's voice, imp_face */
    unsigned int seed;                  /* (its own random numbers: the game's rand() stays the game's) */
    struct {   /* the player characters' cutscene models: the head part the scripts link to them (Claire's
                  ponytail, Wesker's sunglasses), from the character's part file (load_head) */
        O_WORK ow[16] __attribute__((aligned(16)));
        unsigned char keep[8192] __attribute__((aligned(64)));   /* its skin and model chunks */
        unsigned char exp0[0x4000] __attribute__((aligned(32))), exp3[0x1000] __attribute__((aligned(32)));   /* the ponytail's work (bhObjClpn) */
        BH_PWORK w;                     /* sub-model 4 as bhReadPlayerData has it (player_hair reads it there) */
        O_WRK obj[3];                   /* [2]: the head part, linked to node 5 of the display actor */
        int on;
    } hp;
} Disguise;
static Disguise dg[ACTOR_SLOTS] __attribute__((aligned(64)));
/* the cast slots' and the double's own hands (actor_cast_hands): a player character's unarmed pieces, as the lobby preview's */
#define HAND_MDL 4096                   /* one piece's model (the four characters': 2.4..2.8 KB) */
static struct {
    O_WRK obj[2];                       /* right, left: linked to the display actor's hand nodes */
    O_WORK ow[2][4] __attribute__((aligned(16)));
    unsigned char mdl[2][HAND_MDL] __attribute__((aligned(64)));
    int on, tried, chr;                 /* chr: whose pieces (0 Claire, 1 Chris, 2 Steve, 3 Wesker) */
} ch[ACTOR_SLOTS] __attribute__((aligned(64)));   /* (by slot: the double's and the cast's used) */
/* The model buffers: our player's, the partner's, the double's and the variants' 1.5 MB each; the cast slots share
   one buffer, each taking what its model needs (no motions: a cast model is always posed from its source), given back
   when all of them are unloaded (the room's end). All of it inside the PS2 address window (ps2/memory.c). */
static unsigned char keep_big[ACTOR_CAST][KEEP_SIZE] __attribute__((aligned(64)));
#define CAST_MEM (3 << 20)              /* the largest models: imported ones (1.1..1.35 MB); the cast's own 0.1..0.2 MB */
static unsigned char cast_mem[CAST_MEM] __attribute__((aligned(64)));
static int cast_used;

/* d->keep holds `need` bytes (a cast slot: a piece of cast_mem, kept for the slot's reloads) */
static int keep_fit(Disguise* d, int need)
{
    if (d < &dg[ACTOR_CAST]) return need <= KEEP_SIZE;
    if (d->keep && need <= d->keep_cap) return 1;
    need = (need + 63) & ~63;
    if (need > CAST_MEM - cast_used) return 0;
    d->keep = cast_mem + cast_used;
    d->keep_cap = need;
    cast_used += need;
    return 1;
}

/* every cast slot unloaded: cast_mem free again */
static void cast_mem_reset(void)
{
    int i;
    for (i = ACTOR_CAST; i < ACTOR_SLOTS; i++) if (dg[i].model >= 0) return;
    for (i = ACTOR_CAST; i < ACTOR_SLOTS; i++) { dg[i].keep = NULL; dg[i].keep_cap = 0; }
    cast_used = 0;
}
static unsigned char pack[6 << 20] __attribute__((aligned(64)));
static unsigned char room[12 << 20] __attribute__((aligned(64)));   /* the game's arena size: any room it loads fits */
int actor_on;                           /* a display actor is shown (game.c hooks) */
static int dbl_tick;                    /* actor_double_tick is posing the double (its hands go to the model's); 2: a second
                                           double of the tick, which takes the ponytail as the first one swung it */
static BH_PWORK* posed_src[ACTOR_SLOTS];   /* the double's and the cast's slots: the source their current pose was taken from this tick */
#define dbl_posed posed_src[ACTOR_DOUBLE]
static unsigned int posed_t[ACTOR_SLOTS];   /* the cast's: the tick (actor_ticks) of that pose */
static unsigned int actor_ticks;        /* game ticks (actor_hands_back) */
static void face_init(Disguise* d, int idx);
static unsigned int k_seed;             /* imported models' blink seeds */
/* the retargeting of docs/ACTORS.md "Retargeting" (limb roles along each chain, the root's height
   scaled, the aim solved on the drawn weapon, the camera scaled); RECVX_RETARGET=0: the previous one */
int actor_retarget = 1;
/* the cutscene cast's hands (cast_glue.c hands_hide, the turn above); test aid RECVX_CAST_HANDS=0: as before (the record's
   pieces kept everywhere, moved to the model's hands without its turn) */
int cast_hands = 1;
int cast_parts = 1;                     /* cast_glue.c parts_hide; test aid RECVX_CAST_PARTS=0: the head parts kept */

/* the player's chains: the node whose position gives a node's bone direction (-1: a chain end) */
static const signed char dir_child[22] = { -1, 2, 3, 4, 5, -1, 7, 8, 9, -1, 11, 12, 13, -1, 15, 16, 17, -1, 19, 20, 21, -1 };

/* the story cast's event models that act for the character played (sys->ply_id) in cutscenes */
static const unsigned char doubles[4][3] = { { 91 }, { 92, 42 }, { 93, 43 }, { 97, 49, 47 } };

static unsigned int rd32(const unsigned char* p) { unsigned int v; memcpy(&v, p, 4); return v; }

/* ---------------------------------------------------------------- humanoid rig */
static void rig_child_list(const Rig* r, int p, int* out, int* n)
{
    int i;
    *n = 0;
    for (i = 0; i < r->n; i++)
        if (r->par[i] == p) out[(*n)++] = i;
}

/* the next joint of a chain at `c`: the child going furthest in `axis` (1 = y down, 0 = x out); a
   child at c's own position (a zero-length bone: Rodrigo's and the hazmat man's upper arms, many
   toes) is passed through, so the chain goes on below it (the node follows its parent rigidly) */
static int rig_next(const Rig* r, int c, int axis, int sign)
{
    int ch[NODES], nc, i, best = -1, z = -1;
    rig_child_list(r, c, ch, &nc);
    for (i = 0; i < nc; i++) {
        float d = (r->rest[ch[i]][axis] - r->rest[c][axis]) * sign, e = 0;
        int k;
        if (d > 0.3f && (best < 0 || d > (r->rest[ch[best]][axis] - r->rest[c][axis]) * sign)) best = i;
        for (k = 0; k < 3; k++) e += fabsf(r->rest[ch[i]][k] - r->rest[c][k]);
        if (e < 0.05f) z = ch[i];
    }
    return best >= 0 ? ch[best] : z >= 0 ? rig_next(r, z, axis, sign) : -1;
}

/* along a chain from `c` */
static int rig_chain(const Rig* r, int c, int axis, int sign, int* out, int max)
{
    int n = 0;
    for (; c >= 0 && n < max; c = rig_next(r, c, axis, sign)) out[n++] = c;
    return n;
}

static float rig_dist(const Rig* r, int a, int b)
{
    float d = 0;
    int k;
    for (k = 0; k < 3; k++) d += (r->rest[a][k] - r->rest[b][k]) * (r->rest[a][k] - r->rest[b][k]);
    return sqrtf(d);
}

/* how far below node c its subtree reaches */
static float rig_reach(const Rig* r, int c)
{
    float m = 0;
    int i, p;
    for (i = c + 1; i < r->n; i++) {   /* (pre-order: the subtree follows c) */
        for (p = r->par[i]; p >= 0 && p != c; p = r->par[p]);
        if (p == c && rig_dist(r, i, c) > m) m = rig_dist(r, i, c);
    }
    return m;
}

/* An arm from its first node: along the child going furthest out (rig_next), ending at the hand: a
   node without children, or with two or more whose subtrees are short against the arm (fingers,
   claws; a spike on a forearm is not the hand: the arm goes on past it) */
static int rig_arm(const Rig* r, int c, int sign, int* out, int max)
{
    int n = 0;
    for (; c >= 0 && n < max; c = rig_next(r, c, 0, sign)) {
        int ch[NODES], nc, i, real = 0;
        out[n++] = c;
        rig_child_list(r, c, ch, &nc);
        for (i = 0; i < nc; i++) real += rig_dist(r, ch[i], c) >= 0.05f;
        if (real >= 2 && rig_reach(r, c) < 0.35f * rig_dist(r, c, out[0])) break;
    }
    return n;
}

/* Of the first `lim` nodes of chain c[0..n-1] (hanging off `from`, ending at `end`), the one nearest
   to fraction f of the chain's length from `from` to `end`; a tie goes to the later node (past a
   zero-length bone) */
static int rig_role(const Rig* r, int from, const int* c, int n, int lim, float f, int end)
{
    float len[16], tot = 0, d;
    int i, j, prev = from, best = -1;
    if (n > 15) n = 15;
    for (i = 0; i <= n; i++) {
        int node = i < n ? c[i] : end;
        for (d = 0, j = 0; j < 3; j++) d += (r->rest[node][j] - r->rest[prev][j]) * (r->rest[node][j] - r->rest[prev][j]);
        len[i] = tot += sqrtf(d);
        prev = node;
    }
    if (lim > n) lim = n;
    if (lim <= 0) return -1;
    if (tot < 1e-3f) return c[0];
    for (i = 0; i < lim; i++)
        if (best < 0 || fabsf(len[i] / tot - f) <= fabsf(len[best] / tot - f)) best = i;
    return c[best];
}

static void rig_rot(Rig* r);

/* Node roles from the rest skeleton of a chunk object array (pre-order, child/sibling): a T-pose
   with all bind angles 0 is required of a model (`bind`); a source's node offsets are its rest
   pose whatever its current angles. Returns rig->ok. */
static int rig_build(Rig* r, const NJS_CNK_OBJECT* o, int n, int bind)
{
    int st[NODES], sp = 0, cur = -1, i, k, ch[NODES], nc, legs = 0, chest = -1, node, c[8], m;
    memset(r, 0, sizeof(*r));
    memset(r->map, -1, sizeof(r->map));
    if (n < 10 || n > NODES) return 0;
    r->n = n;
    for (i = 0; i < n; i++) {   /* parents and rest positions (bhCalcTree's walk) */
        if (bind && (o[i].ang[0] || o[i].ang[1] || o[i].ang[2])) return 0;
        r->par[i] = cur;
        for (k = 0; k < 3; k++) r->rest[i][k] = o[i].pos[k] + (cur >= 0 ? r->rest[cur][k] : 0);
        if (o[i].sibling) st[sp++] = cur;
        cur = o[i].child ? i : sp ? st[--sp] : -1;
        if (sp >= NODES - 1) return 0;
    }
    r->map[0] = 0;
    r->map[1] = 1;
    /* legs: chains going down from the pelvis */
    rig_child_list(r, 1, ch, &nc);
    for (i = 0; i < nc; i++) {
        int base;
        if (r->rest[ch[i]][1] > r->rest[1][1] - 0.5f) continue;
        base = r->rest[ch[i]][0] > r->rest[1][0] ? 14 : 18;   /* (the root may carry the world position) */
        if (r->map[base] >= 0) continue;
        m = rig_chain(r, ch[i], 1, -1, c, actor_retarget ? 8 : 4);
        if (m > 4) {   /* more joints than hip, knee, ankle, toe (Monster Steve): the ends, and the knee at mid-leg */
            int kk = rig_role(r, c[0], c + 1, m - 3, m - 3, 0.5f, c[m - 2]);
            c[1] = kk; c[2] = c[m - 2]; c[3] = c[m - 1]; m = 4;
        }
        for (k = 0; k < m; k++) r->map[base + k] = c[k];
        legs++;
    }
    /* spine: up from the pelvis to the first node with a sideways child (the chest) */
    for (i = 0; i < nc && chest < 0; i++) {
        int s[8], ns = 0;
        if (r->rest[ch[i]][1] < r->rest[1][1] - 0.5f) continue;
        for (node = ch[i]; node >= 0 && ns < 8;) {
            int cc[NODES], ncc, j, up = -1, side = 0;
            s[ns++] = node;
            rig_child_list(r, node, cc, &ncc);
            for (j = 0; j < ncc; j++) {
                float dx = fabsf(r->rest[cc[j]][0] - r->rest[node][0]);
                if (dx > 0.15f) side = 1;
                else if (up < 0 && r->rest[cc[j]][1] >= r->rest[node][1]) up = cc[j];
            }
            if (side) { chest = node; break; }
            node = up;
        }
        if (chest < 0) continue;
        r->map[3] = chest;
        r->map[2] = ns > 1 ? s[ns - 2] : -1;
    }
    if (chest < 0 || legs < 2) return 0;
    /* neck and head: up from the chest; arms: sideways from the chest or the spine below it */
    rig_child_list(r, chest, ch, &nc);
    for (i = 0; i < nc; i++)
        if (fabsf(r->rest[ch[i]][0] - r->rest[chest][0]) <= 0.15f && r->rest[ch[i]][1] > r->rest[chest][1] && r->map[4] < 0) {
            int cc[NODES], ncc;
            m = rig_chain(r, ch[i], 1, 1, c, 2);
            r->map[4] = c[0];
            rig_child_list(r, c[0], cc, &ncc);
            if (m > 1) r->map[5] = c[1];
            else if (ncc == 1) r->map[5] = cc[0];   /* a head hanging in front of the neck (hunter) */
        }
    for (node = chest; node > 1; node = r->par[node]) {
        rig_child_list(r, node, ch, &nc);
        for (i = 0; i < nc; i++) {
            float dx = r->rest[ch[i]][0] - r->rest[node][0];
            int base = dx > 0 ? 6 : 10, j = 0, e;
            if (fabsf(dx) <= 0.15f || r->map[base + 1] >= 0 || r->map[base] >= 0 || r->map[base + 3] >= 0) continue;
            m = actor_retarget ? rig_arm(r, ch[i], dx > 0 ? 1 : -1, c, 8) : rig_chain(r, ch[i], 0, dx > 0 ? 1 : -1, c, 5);
            e = c[m - 1];
            if (actor_retarget && m < 2) continue;   /* a stub (the Bandersnatch's other shoulder) */
            /* an "arm" that ends far above or below its root is a tentacle, a tail or a wing */
            if (fabsf(r->rest[e][1] - r->rest[ch[i]][1]) > fabsf(r->rest[e][0] - r->rest[ch[i]][0])) continue;
            if (actor_retarget) {   /* the hand at the chain's end, the others where the player has them along it */
                static const float frac[3] = { 0.03f, 0.35f, 0.68f };   /* clavicle, shoulder, elbow (Claire's and Chris's arms) */
                int lim = m - 1;
                r->map[base + 3] = e;
                for (k = 2; k >= 0 && lim > 0; k--) {
                    int t = rig_role(r, node, c, m - 1, lim, frac[k], e);
                    r->map[base + k] = t;
                    for (lim = 0; c[lim] != t; lim++);
                }
                continue;
            }
            if (sqrtf(dx * dx + powf(r->rest[ch[i]][1] - r->rest[node][1], 2)) < 1.0f) r->map[base] = c[j++];   /* clavicle */
            for (k = 1; k < 4 && j < m; k++) r->map[base + k] = c[j++];
        }
    }
    rig_rot(r);
    return r->ok = 1;
}

/* The node whose world rotation turns the bone of role k (towards the next mapped joint of its chain): the mapped
   node itself, or the deepest node on the way to that joint lying at the mapped node's own position (a zero-length
   bone). Rodrigo's and the hazmat man's shoulders are two nodes at one place: the arm's chain (rig_next) passes
   through the second, whose rotation is the upper arm's; the first barely turns. Taken from the first, the model's
   upper arm stayed near its rest pose (raised, a T-pose) while his hung down or lay on the table. */
static void rig_rot(Rig* r)
{
    int k;
    for (k = 0; k < 22; k++) {
        int a = r->map[k], b = dir_child[k] >= 0 ? r->map[(int)dir_child[k]] : -1, p, best = -1;
        r->rot[k] = (signed char)a;
        if (a < 0 || b < 0) continue;
        for (p = r->par[b]; p >= 0 && p != a; p = r->par[p])
            if (best < 0 && rig_dist(r, p, a) < 0.05f) best = p;
        if (p == a && best >= 0) r->rot[k] = (signed char)best;
    }
}

/* rotation (row-vector convention, NJS: p' = p * M) taking direction u onto direction v */
static void rot_from_to(const float* u, const float* v, float* m /* [9] */)
{
    float a[3], b[3], c[3], d, la = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]), lb = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]), k;
    int i, j;
    static const float id[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    memcpy(m, id, sizeof(id));
    if (la < 1e-4f || lb < 1e-4f) return;
    for (i = 0; i < 3; i++) { a[i] = u[i] / la; b[i] = v[i] / lb; }
    c[0] = a[1] * b[2] - a[2] * b[1]; c[1] = a[2] * b[0] - a[0] * b[2]; c[2] = a[0] * b[1] - a[1] * b[0];
    d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    if (d < -0.999f) return;   /* opposite: leave it (no rest pose of the catalogue needs it) */
    k = 1.0f / (1.0f + d);
    {   /* column form M = I + [c]x + [c]x^2 / (1 + d); the row form is its transpose */
        float x[9] = { 0, -c[2], c[1], c[2], 0, -c[0], -c[1], c[0], 0 }, x2[9] = { 0 };
        for (i = 0; i < 3; i++)
            for (j = 0; j < 3; j++) x2[i * 3 + j] = x[i * 3] * x[j] + x[i * 3 + 1] * x[3 + j] + x[i * 3 + 2] * x[6 + j];
        for (i = 0; i < 3; i++)
            for (j = 0; j < 3; j++) m[j * 3 + i] = id[i * 3 + j] + x[i * 3 + j] + x2[i * 3 + j] * k;
    }
}

/* Ninja angles (ax, ay, az) of a row-vector rotation r[9] as njRotateXYZ builds it (Rx Ry Rz) */
static void euler_xyz(const float* r, Sint32* ang)
{
    float sy = -r[2], cy = sqrtf(r[0] * r[0] + r[1] * r[1]);
    const float k = 32768.0f / 3.14159265f;
    ang[1] = (Sint32)lroundf(atan2f(sy, cy) * k);
    if (cy > 1e-4f) {
        ang[0] = (Sint32)lroundf(atan2f(r[5], r[8]) * k);
        ang[2] = (Sint32)lroundf(atan2f(r[1], r[0]) * k);
    } else {   /* gimbal lock: all of it in x */
        ang[0] = (Sint32)lroundf(atan2f(-r[7], r[4]) * k);
        ang[2] = 0;
    }
}

static void mul3(const float* a, const float* b, float* out)   /* out = a * b (3x3, row vectors) */
{
    float t[9];
    int j, c;
    for (j = 0; j < 3; j++)
        for (c = 0; c < 3; c++) t[j * 3 + c] = a[j * 3] * b[c] + a[j * 3 + 1] * b[3 + c] + a[j * 3 + 2] * b[6 + c];
    memcpy(out, t, sizeof(t));
}

/* world matrices of all nodes from their world rotations: positions from the model's own bone
   offsets (the root: at `root`), so every bone keeps its length and stays on its parent */
static void place(ML_WORK* m, const Rig* tr, float (*rot)[9], const float* dm, const float* root)
{
    int i, j;
    for (i = 0; i < tr->n; i++) {
        float* w = m->owP[i].mtx;
        int p = tr->par[i];
        for (j = 0; j < 3; j++) { w[j] = rot[i][j]; w[4 + j] = rot[i][3 + j]; w[8 + j] = rot[i][6 + j]; }
        w[3] = w[7] = w[11] = 0;
        w[15] = 1;
        if (i == 0 && root) memcpy(&w[12], root, 12);
        else {
            const float* pw = p >= 0 ? m->owP[p].mtx : dm;
            const float* off = m->objP[i].pos;
            for (j = 0; j < 3; j++) w[12 + j] = off[0] * pw[j] + off[1] * pw[4 + j] + off[2] * pw[8 + j] + pw[12 + j];
        }
    }
}

/* Two-bone IK on the model's own arm (upper arm u, forearm f, hand h; its own bone lengths): the
   hand to t, the elbow kept in the plane it bends in. The rotations of u and f are turned. */
static void arm_ik(ML_WORK* m, float (*rot)[9], int u, int f, int h, const float* t)
{
    const float *S = &m->owP[u].mtx[12], *E = &m->owP[f].mtx[12], *H = &m->owP[h].mtx[12];
    float se[3], eh[3], st[3], l1 = 0, l2 = 0, d = 0, k = 0, pole[3], pl = 0, e2[3], h1[3], t2[3], d1[9], d2[9], ca, sa;
    int j;
    for (j = 0; j < 3; j++) { se[j] = E[j] - S[j]; eh[j] = H[j] - E[j]; st[j] = t[j] - S[j]; }
    for (j = 0; j < 3; j++) { l1 += se[j] * se[j]; l2 += eh[j] * eh[j]; d += st[j] * st[j]; }
    l1 = sqrtf(l1); l2 = sqrtf(l2); d = sqrtf(d);
    if (l1 < 0.1f || l2 < 0.1f || d < 1e-3f) return;
    for (j = 0; j < 3; j++) { st[j] /= d; k += se[j] * st[j]; }
    for (j = 0; j < 3; j++) { pole[j] = se[j] - st[j] * k; pl += pole[j] * pole[j]; }
    if ((pl = sqrtf(pl)) < 1e-3f) return;   /* a straight arm: no bend plane */
    if (d > (l1 + l2) * 0.999f) d = (l1 + l2) * 0.999f;
    if (d < fabsf(l1 - l2) * 1.01f + 0.01f) d = fabsf(l1 - l2) * 1.01f + 0.01f;
    ca = (l1 * l1 + d * d - l2 * l2) / (2 * l1 * d);
    sa = sqrtf(fmaxf(0, 1 - ca * ca));
    for (j = 0; j < 3; j++) e2[j] = l1 * (ca * st[j] + sa * pole[j] / pl);   /* the new elbow, from S */
    rot_from_to(se, e2, d1);
    for (j = 0; j < 3; j++) h1[j] = eh[0] * d1[j] + eh[1] * d1[3 + j] + eh[2] * d1[6 + j];   /* forearm after d1 */
    for (j = 0; j < 3; j++) t2[j] = st[j] * d - e2[j];
    rot_from_to(h1, t2, d2);
    mul3(rot[u], d1, rot[u]);
    mul3(rot[f], d1, rot[f]);
    mul3(rot[f], d2, rot[f]);
}

/* an unmapped node's world rotation: rigidly with its parent; with a face mask (face_init) turned
   by its own angles, which the mask sets (jaw, tongue, eyes) */
static void follow(const Disguise* d, float (*rot)[9], int i, int p)
{
    NJS_MATRIX l;
    float r[9];
    int j;
    const Sint32* a = d->w.mlwP->objP[i].ang;
    if (!d->face || !(a[0] | a[1] | a[2])) { memcpy(rot[i], rot[p], sizeof(rot[i])); return; }
    njUnitMatrix(&l);
    njRotateXYZ(&l, a[0], a[1], a[2]);
    for (j = 0; j < 9; j++) r[j] = ((float*)l)[(j / 3) * 4 + j % 3];
    mul3(r, rot[p], rot[i]);
}

/* the display actor's world matrices from the source's posed skeleton */
static void retarget(Disguise* d, BH_PWORK* src, const Rig* sr)
{
    ML_WORK* m = d->w.mlwP;
    O_WORK* sow = src->mlwP->owP;
    const Rig* tr = &d->rig;
    const float* dm = (const float*)d->w.mtxbuf;   /* the actor's own matrix (d->w.mtx points here) */
    const float* root = sr->map[0] >= 0 ? &sow[(int)sr->map[0]].mtx[12] : NULL;   /* the root: feet on the source's floor */
    float rot[NODES][9], scaled[3], fs = 1e9f, rs = 1e9f, rt = 1e9f, flr = dm[13], hs = 0, ht = 0;
    int i, k, j, from[NODES];
    if (root && actor_retarget && tr->map[1] >= 0 && sr->map[1] >= 0) {
        /* The root's height above the floor (crouching, kneeling, the walk's bob) scaled by the hips'
           heights: a taller body's legs bend as the source's and reach as far down in proportion.
           The floor is under the source's lowest ankle or toe (its height over the sole in the rest
           pose), not the base matrix: a cutscene record's base matrix often stays at the room's origin
           while its room motion carries it onto a floor 30 or 50 units up, and scaled from there a
           taller model floated (Alexia in 0030 3 units, a Hunter in 1020 2) and a smaller one sank.
           Only the height: room motions move cutscene doubles across the room with the root. */
        hs = sr->rest[(int)sr->map[1]][1] - sr->rest[(int)sr->map[0]][1];
        ht = tr->rest[(int)tr->map[1]][1] - tr->rest[(int)tr->map[0]][1];
        for (k = 16; k <= 21; k++) {   /* the ankles and toes */
            if (k == 18 || k == 19 || tr->map[k] < 0 || sr->map[k] < 0) continue;
            fs = fminf(fs, sow[(int)sr->map[k]].mtx[13]);
            rs = fminf(rs, sr->rest[(int)sr->map[k]][1] - sr->rest[(int)sr->map[0]][1]);
            rt = fminf(rt, tr->rest[(int)tr->map[k]][1] - tr->rest[(int)tr->map[0]][1]);
        }
        if (fs < 1e8f) flr = fs - rs;
        memcpy(scaled, root, sizeof(scaled));
        if (hs > 1.0f && ht > 1.0f) scaled[1] = flr + (root[1] - flr) * ht / hs;
        root = scaled;
    }
    for (i = 0; i < tr->n; i++) from[i] = -1;
    for (k = 0; k < 22; k++) if (tr->map[k] >= 0 && sr->map[k] >= 0) from[(int)tr->map[k]] = k;
    for (i = 0; i < tr->n; i++) {
        int p = tr->par[i];
        k = from[i];
        if (k >= 0) {   /* source world rotation, after the rest correction */
            const float* s = sow[(int)(actor_retarget ? sr->rot[k] : sr->map[k])].mtx;
            float a[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 }, sw[9];
            /* An imported model (d->skin) is skinned to its own T-pose, the same pose as the source's
               rest: it takes the source's rotations unchanged. A correction would turn its rest mesh
               (Claire's clavicles point up to the shoulders, Outbreak's start at them: the shoulders
               went up 60 degrees, the head forward: hunched). */
            if (k > 0 && !d->skin) {   /* the bone towards the chain's next node; a chain end takes its parent bone's */
                int kk = dir_child[k] >= 0 ? k : k - 1, dk = dir_child[kk];
                if (dk >= 0 && tr->map[dk] >= 0 && sr->map[dk] >= 0 && tr->map[kk] >= 0 && sr->map[kk] >= 0) {
                    float u[3], v[3];
                    for (j = 0; j < 3; j++) {
                        u[j] = tr->rest[(int)tr->map[dk]][j] - tr->rest[(int)tr->map[kk]][j];
                        v[j] = sr->rest[(int)sr->map[dk]][j] - sr->rest[(int)sr->map[kk]][j];
                    }
                    rot_from_to(u, v, a);
                }
            }
            for (j = 0; j < 9; j++) sw[j] = s[(j / 3) * 4 + j % 3];
            mul3(a, sw, rot[i]);
        } else if (p >= 0) {
            int s;
            follow(d, rot, i, p);   /* rigidly with its parent */
            for (s = 7; s <= 11; s += 4)   /* a source without that arm (Nosferatu in his straitjacket, 6040): the model's upper arm
                                              hangs down beside its body instead of staying out in its T-pose */
                if (tr->map[s] == i && sr->map[s] < 0 && sr->map[s + 2] < 0 && tr->map[s + 2] >= 0) {
                    float u[3], v[3] = { 0, -1, 0 }, a[9];
                    for (j = 0; j < 3; j++) u[j] = tr->rest[(int)tr->map[s + 2]][j] - tr->rest[i][j];
                    v[0] = u[0] > 0 ? 0.2f : -0.2f;
                    rot_from_to(u, v, a);
                    mul3(a, rot[p], rot[i]);
                }
        } else for (j = 0; j < 9; j++) rot[i][j] = dm[(j / 3) * 4 + j % 3];
    }
    place(m, tr, rot, dm, root);
    if (root == scaled && fs < 1e8f && hs > 1.0f && ht > 1.0f) {
        /* The feet as the source's: the model's lowest ankle or toe as high as the source's, each over its
           own sole (the rest pose's ankle heights), so the model stands where the source stands and its
           feet leave the floor as far as the source's do (a jump, a climb, a kick: matched, not glued to
           the floor). The hips' scale alone missed models whose legs are longer or shorter than their
           hips suggest (the Tyrant for Rodrigo in the opening: its ankles 1.6 units in the floor). Feet of another kind (a claw, a
           tail for a leg) more than half the hip height off: the shift stops there. */
        float ft = 1e9f, shift, lim = ht * 0.5f;
        for (k = 16; k <= 21; k++) if (k != 18 && k != 19 && tr->map[k] >= 0 && sr->map[k] >= 0) ft = fminf(ft, m->owP[(int)tr->map[k]].mtx[13]);
        shift = fs - rs + rt - ft;   /* ponytail: one shift for the whole body; a foot IK would plant each foot on its own */
        if (shift > lim) shift = lim;
        if (shift < -lim) shift = -lim;
        scaled[1] += shift;
        place(m, tr, rot, dm, root);
        if (getenv("RECVX_FEET_TRACE") && !(actor_ticks % 15)) {   /* test aid: the lowest ankle or toe over the base matrix, source and model */
            ft = 1e9f;
            for (k = 16; k <= 21; k++) if (k != 18 && k != 19 && tr->map[k] >= 0 && sr->map[k] >= 0) ft = fminf(ft, m->owP[(int)tr->map[k]].mtx[13]);
            host_log("actor: feet slot %d model %d: source %.2f model %.2f over %.2f (hips %.2f/%.2f, rest feet %.2f/%.2f, shift %.2f)\n", (int)(d - dg), d->model,
                     fs - dm[13], ft - dm[13], dm[13], hs, ht, rs, rt, shift);
        }
    }
    {   /* Both hands on the weapon (two-handed aim and grips): the source's left hand is near its
           right. A model of other proportions has its hands elsewhere (longer arms, wider shoulders),
           so its left arm is solved to keep that hand where the source has it relative to the right
           hand, on the model's own bone lengths. Faded out between 3 and 6 units apart. */
        int u = tr->map[11], f = tr->map[12], h = tr->map[13], r = tr->map[9];
        if (u >= 0 && f >= 0 && h >= 0 && r >= 0 && sr->map[9] >= 0 && sr->map[13] >= 0) {
            const float *sR = &sow[(int)sr->map[9]].mtx[12], *sL = &sow[(int)sr->map[13]].mtx[12];
            float t[3], ds = 0, w;
            for (j = 0; j < 3; j++) ds += (sL[j] - sR[j]) * (sL[j] - sR[j]);
            w = (6.0f - sqrtf(ds)) / 3.0f;
            if (d->skin && sr->map[11] >= 0) {
                /* An imported model: only while the source's left hand reaches forward (a hold or an aim:
                   1.0 units and more in front of its shoulder, none below 0.6). Chris's hands hang 3.4
                   apart in his idle (Claire's 3.75), inside the 6-unit fade: the solve pulled the
                   model's left hand in across its thigh (its shoulders are narrower than Chris's). */
                const float* sS = &sow[(int)sr->map[11]].mtx[12];
                float fz = 0, zl = 0, fwd;
                for (j = 0; j < 3; j++) { fz += (sL[j] - sS[j]) * dm[8 + j]; zl += dm[8 + j] * dm[8 + j]; }
                fwd = zl > 1e-6f ? -fz / sqrtf(zl) : 0;   /* (the actor's z axis points backwards) */
                w *= fminf(1.0f, fmaxf(0.0f, (fwd - 0.6f) / 0.4f));
            }
            if (w > 0) {
                if (w > 1) w = 1;
                float off[3];
                for (j = 0; j < 3; j++) off[j] = sL[j] - sR[j];
                if (d->skin && tr->map[7] >= 0 && sr->map[7] >= 0 && sr->map[11] >= 0) {
                    /* An imported model: the hands' offset in proportion to its shoulders (Outbreak's are a
                       quarter wider than Claire's: unscaled, the left hand was pulled in across the body even
                       with the arms down, which the source holds 3.5 apart) */
                    float ms = rig_dist(tr, tr->map[7], u), ss = rig_dist(sr, sr->map[7], sr->map[11]), k = 1;
                    float g = w * fminf(1.0f, fmaxf(0.0f, (1.5f - sqrtf(ds)) / 0.5f)), y[3], dn = 0, yl = 0, rd[3], rl = 0, hl = 0;
                    if (ms > 0.1f && ss > 0.1f) k = ms / ss;
                    for (j = 0; j < 3; j++) off[j] *= k;
                    /* In a one-gun grip (the handgun's and magnum's: hands up to 1 apart, fading out at 1.5;
                       a long gun's support hand on the fore-end is 2.6..3.8 from the gun hand, two guns
                       2.3..3, and keep the source's offset) the palm under the gun: Code: Veronica's
                       grip pieces reach in from the wrist and curl under the weapon, Outbreak's hand is open and
                       straight (it went on along the left forearm, across and past the gun to the right, or with
                       its wrist pulled back it hid behind the gun hand; with the source's full drop and a quarter
                       hand back it hung low and its fingers reached far past the grip). So the wrist goes 0.6 of
                       the source's drop below the gun hand's wrist, a twentieth of the hand ahead of it along the
                       gun (the right forearm), and the fingers are turned along the gun: the palm right under the
                       gun hand, on the body's centre line (the source's sideways offset is left out). */
                    if (g > 0 && tr->map[8] >= 0) {
                        const float *rh = &m->owP[r].mtx[12], *re = &m->owP[(int)tr->map[8]].mtx[12];
                        for (j = 0; j < 3; j++) { y[j] = dm[4 + j]; yl += y[j] * y[j]; rd[j] = rh[j] - re[j]; rl += rd[j] * rd[j]; hl += d->hand_dir[j] * d->hand_dir[j]; }
                        if (yl > 1e-6f && rl > 1e-6f) {
                            const float c = -0.05f, dk = 0.6f;   /* (tuned against Claire and Chris, all 8 characters: the wrist just ahead of the gun hand's, 0.6 of the source's drop) */
                            yl = sqrtf(yl); rl = sqrtf(rl); hl = sqrtf(hl);
                            for (j = 0; j < 3; j++) dn += off[j] * y[j] / yl;
                            for (j = 0; j < 3; j++) off[j] += g * (dk * dn * y[j] / yl - c * hl * rd[j] / rl - off[j]);
                        }
                    }
                }
                for (j = 0; j < 3; j++) t[j] = m->owP[h].mtx[12 + j] + w * (m->owP[r].mtx[12 + j] + off[j] - m->owP[h].mtx[12 + j]);
                arm_ik(m, rot, u, f, h, t);
                if (d->skin && tr->map[8] >= 0) {
                    /* and in a grip its left hand turned so the fingers point the way the right forearm does,
                       under the gun (they went on along the left forearm, across and past the gun to the right) */
                    float g = w * fminf(1.0f, fmaxf(0.0f, (1.5f - sqrtf(ds)) / 0.5f)), fdir[3], tdir[3], tl = 0, q[9];
                    const float *rh = &m->owP[r].mtx[12], *re = &m->owP[(int)tr->map[8]].mtx[12];
                    for (j = 0; j < 3; j++) { fdir[j] = d->hand_dir[0] * rot[h][j] + d->hand_dir[1] * rot[h][3 + j] + d->hand_dir[2] * rot[h][6 + j]; tdir[j] = rh[j] - re[j]; tl += tdir[j] * tdir[j]; }
                    if (g > 0 && tl > 1e-4f) {
                        tl = sqrtf(tl);
                        for (j = 0; j < 3; j++) tdir[j] = fdir[j] * (1 - g) + tdir[j] / tl * g;
                        rot_from_to(fdir, tdir, q);
                        mul3(rot[h], q, rot[h]);
                    }
                }
                for (i = 0; i < tr->n; i++) if (from[i] < 0 && tr->par[i] >= 0) follow(d, rot, i, tr->par[i]);
                place(m, tr, rot, dm, root);
            }
        }
    }
    /* The node angles as bhCalcTree would take them (local rotation = world x parent world^T): the
       skin (npCalcSkin) blends the vertices near a joint between the node's world matrix and that
       matrix with the node's own angles undone (the parent's rotation at the joint). With the bind
       angles 0 left there, every joint was a hard crease: hands, forearms and shoulders stood off
       the limb they belong to. */
    for (i = 0; i < tr->n; i++) {
        int p = tr->par[i], c;
        float rp[9], l[9];
        if (d->face && from[i] < 0) continue;   /* (the mask's angles stay what they are) */
        for (j = 0; j < 9; j++) rp[j] = p >= 0 ? rot[p][j] : dm[(j / 3) * 4 + j % 3];
        for (j = 0; j < 3; j++)
            for (c = 0; c < 3; c++) l[j * 3 + c] = rot[i][j * 3] * rp[c * 3] + rot[i][j * 3 + 1] * rp[c * 3 + 1] + rot[i][j * 3 + 2] * rp[c * 3 + 2];
        euler_xyz(l, m->objP[i].ang);
    }
}

/* ---------------------------------------------------------------- loading */
/* length of a "u32 size, bytes ... u32 -1" chunk list, 0 if it runs past `end` */
static int list_len(const unsigned char* p, const unsigned char* end)
{
    const unsigned char* q = p;
    for (;;) {
        int n;
        if (q + 4 > end) return 0;
        n = (int)rd32(q);
        q += 4;
        if (n == -1) return q - p;
        if (n < 0 || n > end - q) return 0;
        q += n;
    }
}

static int is_mlb(const unsigned char* c) { return rd32(c) != MAGIC_MASK && rd32(c) != SKIN_MAGIC; }
static int mlb_textured(const unsigned char* c) { return (int)rd32(c + 8) != -1; }   /* MLB header: texlist offset */

/* bhSetRoom's texture table order: per enemy record with a model, per textured sub-model, the
   first (type, model version, ordinal) owns the next entry after the room model's. Returns the
   owner index of (id, ver, k), or -1. */
static int tex_owner(const ETTY_WORK* recs, int n, const unsigned int* mdltab, const unsigned char* end, int id, int ver, int k)
{
    static int seen[1024];
    int i, m = 1, owners = 0, want = (id << 16) | (ver << 8) | k;
    for (i = 0; i < n; i++) {
        const unsigned char* p;
        int t = -1, dt;
        if (!(recs[i].flg & 0x1)) continue;
        p = room + mdltab[m++];
        while (p + 4 <= end && (dt = (int)rd32(p)) != -1) {
            p += 4;
            if (dt >= 16 && is_mlb(p) && mlb_textured(p)) {
                int key = (recs[i].id << 16) | (recs[i].mdlver << 8) | ++t, j;
                for (j = 0; j < owners && seen[j] != key; j++);
                if (j == owners) {
                    if (owners == 1024) return -1;
                    seen[owners++] = key;
                }
                if (key == want) return j;
            }
            p += dt > 0 ? dt : 0;
        }
    }
    return -1;
}

static void unload(Disguise* d)
{
    int i;
    if (!d->tex_gone && d->tex_n) {
        for (i = 0; i < d->tex_n; i++) njReleaseTexture(d->tex[i]);
        bhGarbageTexture(NULL, 0);
    }
    d->tex_n = d->tex_gone = d->posed = d->face = d->lip = d->lip_max = d->hp.on = 0;
    ch[d - dg].on = ch[d - dg].tried = 0;   /* (their textures: in d->tex) */
    posed_src[d - dg] = NULL;
    d->model = -1;
    memset(&d->w, 0, sizeof(d->w));
    d->rig.ok = 0;
    d->skin = NULL;
    d->vtx = NULL;
}

static int free_entries(void)
{
    unsigned int i, n = 0;
    for (i = 0; i < Ps2_texmemlist_num; i++) n += Ps2_tex_info[i].count == 0;
    return n;
}

static int heap_free(void) { return (char*)Ps2_free_last - (char*)Ps2_now_free; }

/* ---------------------------------------------------------------- player part files (actors.h) */
int afs_read(int part, int file, void* buf, int cap)
{
    void* h = ADXF_OpenAfs(part, file);
    int ok;
    if (!h) return 0;
    ok = ADXF_GetFsizeByte(h) > 4096 && ADXF_GetFsizeSct(h) * 2048 <= cap;   /* unused costumes are 11-byte placeholders */
    if (ok) {
        ADXF_ReadNw32(h, ADXF_GetFsizeSct(h), buf);
        ok = ADXF_GetStat(h) == 3;   /* ADXF_STAT_READEND */
    }
    ADXF_Close(h);
    return ok;
}
static int read_file(int part, int file) { return afs_read(part, file, pack, sizeof(pack)); }

/* The stock loader reads from sys->memp and places the O_WORK arrays with bhGetFreeMemory, which
   refuses addresses more than 12.25 MB above freemem, so freemem is moved too. */
void player_model_read(BH_PWORK* who, unsigned char* file, unsigned char* mdl, unsigned char* bmt, unsigned char* mth, unsigned char* zmt)
{
    BH_PWORK* plp_b = plp;
    unsigned char *memp_b = sys->memp, *mdl_b = sys->plmdlp, *bmt_b = sys->plbmtp, *mth_b = sys->plmthp,
                  *zmt_b = sys->plzmtp, *free_b = freemem;
    NJS_POINT3 hd_b = sys->hd_pos;
    unsigned int bmt_size_b = sys->bmt_size;
    plp = who;
    sys->memp = file;
    sys->plmdlp = mdl; sys->plbmtp = bmt; sys->plmthp = mth; sys->plzmtp = zmt;
    freemem = mdl;
    bhReadPlayerData();
    plp = plp_b;
    sys->memp = memp_b;
    sys->plmdlp = mdl_b; sys->plbmtp = bmt_b; sys->plmthp = mth_b; sys->plzmtp = zmt_b;
    freemem = free_b;
    sys->hd_pos = hd_b;
    sys->bmt_size = bmt_size_b;
}

/* bhReadWeaponData for `who` (weapon wpn): the hand objects objs[0..1], the weapon motions into
   who->mnwP[100..] (plp, sys->obwp, sys->memp and the weapon buffers swapped for it and put back) */
void player_weapon_read(BH_PWORK* who, O_WRK* objs, int wpn, unsigned char* file, unsigned char* wr, unsigned char* wl, unsigned char* wmt)
{
    BH_PWORK* plp_b = plp;
    O_WRK* obwp_b = sys->obwp;
    unsigned char *memp_b = sys->memp, *wr_b = sys->wrmdlp, *wl_b = sys->wlmdlp, *wmt_b = sys->plwmtp;
    unsigned int wmt_size_b = sys->wmt_size;
    plp = who;
    who->wpnr_no = wpn;
    sys->obwp = objs;
    sys->memp = file;
    sys->wrmdlp = wr; sys->wlmdlp = wl; sys->plwmtp = wmt;
    bhReadWeaponData();
    plp = plp_b;
    sys->obwp = obwp_b;
    sys->memp = memp_b;
    sys->wrmdlp = wr_b; sys->wlmdlp = wl_b; sys->plwmtp = wmt_b;
    sys->wmt_size = wmt_size_b;
}

/* object objs[2] linked to head node 5 of `who`, set up as bhSetPlayer does for player 1's sys->obwp[2] */
void player_hair(O_WRK* objs, BH_PWORK* who, int chr, unsigned char* exp0, unsigned char* exp3)
{
    O_WRK* obwp_b = sys->obwp;
    O_WRK* op = &objs[2];
    int k = chr == 0 ? 0 : 1, v;
    op->flg = 0;
    if ((chr != 0 && chr != 3) || who->mdl_n <= lkmtab[k].mdlver) return;
    sys->obwp = objs;
    bhSetObject(&lkmtab[k], 2, (unsigned char*)who);
    sys->obwp = obwp_b;
    v = op->mdlver;
    op->lkono = 5;
    op->lox = 0;
    op->loy = chr == 0 ? 1.5869f : 0.85f;
    op->loz = chr == 0 ? 0.7747f : -1.03f;
    if (chr == 0) op->skp[0] = who->skp[v];
    op->mdl[0] = who->mdl[v];
    op->mlwP = &op->mdl[0];
    if (chr == 0 && exp0) {   /* bhObjClpn would take its work from the room heap (plp's: sys->pletcp) */
        op->flg |= 0x100000;
        op->exp0 = exp0;
        op->exp3 = exp3;
    }
}

/* the sub-models of the chunk list in d->keep at `p`, as bhSetEneMdl / bhReadPlayerData realize
   them; own[i]: sub-model i has a texlist of its own; returns the texture count */
static int realize(Disguise* d, unsigned char* p, unsigned char* own)
{
    NJS_TEXLIST* last = NULL;
    int dt, ow = 0, texn = 0;
    for (; (dt = (int)rd32(p)) != -1; p += dt) {
        ML_WORK* m = &d->w.mdl[d->w.mdl_n];
        p += 4;
        if (!dt || d->w.mdl_n == 16) continue;
        if (rd32(p) == MAGIC_MASK) { d->w.mskp = p; continue; }   /* (as room.c bhSetEneMdl; set up only for the double, face_init) */
        if (rd32(p) == SKIN_MAGIC) { d->w.skp[d->w.mdl_n] = (int*)(p + 4); continue; }
        bhMlbBinRealize(p, m);
        if (d->w.skp[d->w.mdl_n]) npSkinConvert(m->objP, d->w.skp[d->w.mdl_n]);
        if (m->texP) { last = m->texP; own[d->w.mdl_n] = 1; texn += m->texP->nbTexture; }
        else m->texP = last;   /* as sys->et_lp: the last textured sub-model's */
        d->w.mbp[d->w.mdl_n] = m->objP;
        d->w.txp[d->w.mdl_n] = m->texP;
        if (ow + (int)m->obj_num > OW_MAX) break;
        m->owP = &d->ow[ow];
        memset(m->owP, 0, m->obj_num * sizeof(O_WORK));
        ow += m->obj_num;
        d->w.mdl_n++;
    }
    return texn;
}

static int tex_room(const char* name, int texn)
{
    int heap;
    bhGarbageTexture(NULL, 0);
    heap = heap_free();
    if (free_entries() >= texn + TEX_MARGIN && heap >= HEAP_MARGIN) return 1;
    host_log("actor: %s: not enough texture memory (%d entries free, %d needed; %d KB heap)\n", name, free_entries(), texn, heap >> 10);
    return 0;
}
void actor_tex_free(int* entries, int* heap) { bhGarbageTexture(NULL, 0); *entries = free_entries(); *heap = heap_free(); }

/* Where the catalogue-format model `am` is in its room file: the file is read and expanded into
   `room` (kept until another room file is requested; nothing is realized in it), and `out` gets the
   record, its model chunk list, its type's motion list (the first record of the type that has one,
   as bhFinishRoom) and the texture data of each textured sub-model in chunk order. Also used by the
   enemy spawner (spawn_glue.c). */
static const unsigned char* end;        /* of the room file in `room` */

/* the tables actor_find, tex_owner and actor_fx_tex read, inside the n bytes of the room file */
static int room_ok(uint32_t n)
{
    const RDT_WORK* hdr = (const RDT_WORK*)room;
    const ROM_WORK* rw;
    const ETTY_WORK* recs;
    uint32_t i, k = 1, ene, cnt;
    if (n < sizeof(RDT_WORK) || hdr->ver < 1.72f || !pc_range_ok(hdr->hed00, sizeof(ROM_WORK), n)) return 0;
    rw = (const ROM_WORK*)(room + hdr->hed00);
    ene = (uint32_t)rw->ene_n;
    if (!pc_range_ok((uint32_t)rw->enep, (uint64_t)ene * sizeof(ETTY_WORK), n) ||
        !pc_range_ok((uint32_t)rw->effp, (uint64_t)(uint32_t)rw->eff_n * sizeof(EF_WRK), n) ||
        !pc_range_ok(hdr->hed02, (uint64_t)ene * 4, n) || !pc_range_ok(hdr->hed04, 4, n))
        return 0;
    recs = (const ETTY_WORK*)(room + (uint32_t)rw->enep);
    for (i = 0; i < ene; i++) {
        if (rd32(room + hdr->hed02 + 4 * i) >= n) return 0;
        if (!(recs[i].flg & 0x1)) continue;
        if (!pc_range_ok(hdr->hed01, 4 * ((uint64_t)k + 1), n) || rd32(room + hdr->hed01 + 4 * k) >= n) return 0;
        k++;
    }
    cnt = rd32(room + hdr->hed04);   /* the texture table: count, then the entries (actor_find reads them from + 8, actor_fx_tex from + 4) */
    if (!pc_range_ok(hdr->hed04, 4 + 4 * (uint64_t)cnt, n)) return 0;
    for (i = 0; i < cnt; i++) if (rd32(room + hdr->hed04 + 4 + 4 * i) >= n) return 0;
    return 1;
}

static int load_rdx(int rm)
{
    static int loaded = -1;
    char name[16];
    int f, len;
    if (loaded == rm) return 1;
    loaded = -1;
    snprintf(name, sizeof(name), "RM_%04d.RDX", rm);
    for (f = 0; f < rdx_image_data_max && strcmp(rdx_files[f], name); f++);
    if (f == rdx_image_data_max || !read_file(17, f)) return 0;
    if ((len = pc_prs_decode(pack, sizeof(pack), room, sizeof(room))) < 0 || !room_ok((uint32_t)len)) {
        host_log("actor: %s %s\n", name, len == PC_PRS_FULL ? "does not fit when expanded" : "is damaged");
        return 0;
    }
    end = room + len;
    loaded = rm;
    return 1;
}

/* the room file rm read and expanded, *len bytes (the --dev object placer, placer_glue.c); valid, as `pack`
   (*scratch, *cap bytes free: the packed file is done with), until another room file or part file is read here */
const unsigned char* actor_room_file(int rm, int* len, unsigned char** scratch, int* cap)
{
    if (!load_rdx(rm)) return NULL;
    *len = (int)(end - room);
    *scratch = pack;
    *cap = (int)sizeof(pack);
    return room;
}

/* system.c: the room file `name` the game read (to the top of [memp, endp)) could not be expanded
   (err: pc_prs_decode's): if it was the mods folder's, the disc's is read there and expanded instead.
   Its size, or err */
int pc_mod_file_size(const char* name);   /* adx.c */
int pc_room_from_disc(const char* name, unsigned char* memp, unsigned char* endp, int err)
{
    char up[16];
    unsigned char* p;
    void* h;
    int f, i, sct, ok;
    for (i = 0; name[i] && i < 15; i++) up[i] = (char)(name[i] >= 'a' && name[i] <= 'z' ? name[i] - 32 : name[i]);
    up[i] = 0;
    if (pc_mod_file_size(up) < 0) return err;   /* (the disc's itself) */
    host_log("mods: %s %s: the disc's is used\n", up, err == PC_PRS_FULL ? "does not fit the room arena when expanded" : "is damaged");
    for (f = 0; f < rdx_image_data_max && strcmp(rdx_files[f], up); f++);
    if (f == rdx_image_data_max || !(h = ADXF_OpenAfs(17, f))) return err;   /* (a room the disc does not have) */
    sct = ADXF_GetFsizeSct(h);
    p = (unsigned char*)((uintptr_t)(endp - (size_t)sct * 2048) & ~(uintptr_t)63);
    ok = sct > 0 && (size_t)sct * 2048 + 64 <= (size_t)(endp - memp) && p >= memp;
    if (ok) {
        ADXF_ReadNw32(h, sct, p);
        ok = ADXF_GetStat(h) == 3;   /* ADXF_STAT_READEND */
    }
    ADXF_Close(h);
    return ok ? pc_prs_decode(p, (uint32_t)(endp - p), memp, (uint32_t)(endp - memp)) : err;
}

int actor_find(const ActorModel* am, ActorData* out)
{
    RDT_WORK* hdr = (RDT_WORK*)room;
    const ROM_WORK* rw;
    const ETTY_WORK* recs;
    const unsigned int* mdltab;
    const unsigned char* p;
    int n, i, k = 1, dt;
    char name[16];

    memset(out, 0, sizeof(*out));
    snprintf(name, sizeof(name), "RM_%04d.RDX", am->room);
    if (!load_rdx(am->room)) return 0;
    rw = (const ROM_WORK*)(room + hdr->hed00);
    recs = (const ETTY_WORK*)(room + (unsigned int)rw->enep);
    n = rw->ene_n;
    if (am->rec >= n || recs[am->rec].id != am->id || recs[am->rec].mdlver != am->mdlver || !(recs[am->rec].flg & 0x1)) {
        host_log("actor: %s: record %d of %s is not type %d version %d\n", am->name, am->rec, name, am->id, am->mdlver);
        return 0;
    }
    out->rec = &recs[am->rec];
    mdltab = (const unsigned int*)(room + hdr->hed01);
    for (i = 0; i < am->rec; i++) k += recs[i].flg & 0x1;
    out->mdl = room + mdltab[k];
    out->mlen = list_len(out->mdl, end);
    {   /* the type's motion set: the first record of the type that has one (bhFinishRoom) */
        const unsigned int* mtntab = (const unsigned int*)(room + hdr->hed02);
        for (i = 0; i < n && !(recs[i].id == am->id && mtntab[i]); i++);
        if (i < n) { out->mtn = room + mtntab[i]; out->nlen = list_len(out->mtn, end); }
    }
    if (!out->mlen || (out->mtn && !out->nlen)) { host_log("actor: %s: bad chunk list in %s\n", am->name, name); return 0; }
    /* the entries of the room's texture table the game would upload them from */
    for (p = out->mdl; (dt = (int)rd32(p)) != -1 && out->ntex < 16; p += 4 + (dt > 0 ? dt : 0))
        if (dt >= 16 && is_mlb(p + 4) && mlb_textured(p + 4)) {
            const unsigned char* texdat = room + hdr->hed04;
            int o = tex_owner(recs, n, mdltab, end, am->id, am->mdlver, out->ntex);
            out->tex[out->ntex++] = o < 0 || o + 1 >= (int)rd32(texdat) ? NULL : room + rd32(texdat + 8 + 4 * o);
        }
    return 1;
}

/* The texture data room file `rm` has for room effect `fx` (ids 30..99 and 400+; NULL: none).
   room.c bhSetRoom uploads one texture table entry per such id, in effect record order, after the
   room model's, enemies', objects' and items': the table's last entries. Valid as actor_find's. */
const unsigned char* actor_fx_tex(int rm, int fx)
{
    const RDT_WORK* hdr = (const RDT_WORK*)room;
    const ROM_WORK* rw;
    const EF_WRK* e;
    const unsigned char* texdat;
    int ids[64], m = 0, i, j, k = -1, cnt;
    if (!load_rdx(rm)) return NULL;
    rw = (const ROM_WORK*)(room + hdr->hed00);
    e = (const EF_WRK*)(room + (unsigned int)rw->effp);
    for (i = 0; i < rw->eff_n && m < 64; i++)
        if ((e[i].id >= 30 && e[i].id < 100) || e[i].id >= 400) {
            for (j = 0; j < m && ids[j] != e[i].id; j++);
            if (j < m) continue;
            if (e[i].id == fx) k = m;
            ids[m++] = e[i].id;
        }
    texdat = room + hdr->hed04;
    cnt = (int)rd32(texdat);
    return k < 0 || cnt <= m ? NULL : room + rd32(texdat + 4 + 4 * (cnt - m + k));
}

/* catalogue model idx from its room file */
static int load_room_model(Disguise* d, int idx)
{
    const ActorModel* am = &actor_models[idx];
    ActorData a;
    unsigned char* p;
    int i, mofs, nofs = 0, t = -1, texn, dt;
    unsigned char own[16] = { 0 };

    if (!actor_find(am, &a)) return 0;
    mofs = (int)((unsigned int)a.mdl & 63);   /* keep the data's alignment */
    if (d >= &dg[ACTOR_CAST]) a.mtn = NULL, a.nlen = 0;   /* (posed from its source only) */
    if (a.mtn) nofs = ((mofs + a.mlen + 63) & ~63) + (int)((unsigned int)a.mtn & 63);
    if (!keep_fit(d, mofs + a.mlen) || !keep_fit(d, nofs + a.nlen)) { host_log("actor: %s does not fit\n", am->name); return 0; }

    unload(d);
    memcpy(d->keep + mofs, a.mdl, a.mlen);
    if (a.mtn) memcpy(d->keep + nofs, a.mtn, a.nlen);
    texn = realize(d, d->keep + mofs, own);
    if (!d->w.mdl_n || !d->w.mdl[0].objP || !d->w.mdl[0].owP || !tex_room(am->name, texn)) return 0;
    for (i = 0; i < d->w.mdl_n; i++) {
        if (!own[i]) continue;
        if (++t >= a.ntex || !a.tex[t]) { host_log("actor: %s: no texture entry for sub-model %d\n", am->name, i); continue; }
        bhSetMemPvpTexture(d->w.mdl[i].texP, (unsigned char*)a.tex[t], 0);
        d->w.mdl[i].flg |= 0x200;
        d->tex[d->tex_n++] = d->w.mdl[i].texP;
    }
    if (a.mtn)   /* the type's motions (bhSetEneMtn) */
        for (i = 0, p = d->keep + nofs; (dt = (int)rd32(p)) != -1 && i < 512; i++, p += dt) {
            p += 4;
            if (dt) bhMnbBinRealize(p, &d->mth[i]);
        }
    return 1;
}

/* the model of player part file `file` (dread.c bhReadPlayerData's layout) */
static int load_part_model(Disguise* d, int file)
{
    unsigned char *p, own[16] = { 0 };
    int i, size, texn;
    char name[24];
    snprintf(name, sizeof(name), "part file %d", file);
    if (!read_file(sys->sys_partid, file) || (size = (int)rd32(pack)) <= 0 || size > KEEP_SIZE || !keep_fit(d, size)) return 0;
    unload(d);
    memcpy(d->keep, pack + 4, size);
    texn = realize(d, d->keep, own);
    if (!d->w.mdl_n || !d->w.mdl[0].owP || !tex_room(name, texn)) return 0;
    p = pack + 4 + size;
    for (i = 0; i < 2; i++) {   /* motions, z translation */
        if (p + 4 > pack + sizeof(pack) || rd32(p) > (unsigned)(pack + sizeof(pack) - p - 4)) return 1;   /* (no textures) */
        p += 4 + rd32(p);
    }
    for (i = 0; i < d->w.mdl_n; i++) {
        unsigned int n;
        if (!own[i]) continue;
        if (p + 4 > pack + sizeof(pack)) break;
        n = rd32(p);
        p += 4;
        if (n & 0x80000000) { p = pack + ((p - pack + 31) & ~31); n &= ~0x80000000; }
        if (n > (unsigned)(pack + sizeof(pack) - p)) break;
        bhSetMemPvpTexture(d->w.mdl[i].texP, p, 0);
        d->w.mdl[i].flg |= 0x200;
        d->tex[d->tex_n++] = d->w.mdl[i].texP;
        p += n;
    }
    return 1;
}

/* The head part of player character chr (0 Claire's ponytail, 3 Wesker's sunglasses) for a cutscene model
   of that character, which has none of its own (the scripts link the player's sys->obwp[2] to the
   double): sub-model 4 of part file 10 + chr, its skin and its texture only (Claire 1.4 KB + 128 KB
   texture, Wesker 2.3 KB + 5 KB), set up as bhSetPlayer does and linked to node 5 of the display actor.
   The ponytail simulates in work of its own (no game memory, docs/ACTORS.md "Head parts"). */
static void load_head(Disguise* d, int chr)
{
    unsigned char *p, *end, *skin = NULL, *c = NULL;
    int dt, i = 0, tex = 0, size, slen = 0, clen = 0;
    unsigned int n = 0;
    O_WRK* op = &d->hp.obj[2];
    ML_WORK* m = &d->hp.w.mdl[4];
    if (!read_file(sys->sys_partid, 10 + chr) || (size = (int)rd32(pack)) <= 0 || size > (int)sizeof(pack) - 4) return;
    end = pack + 4 + size;
    for (p = pack + 4; p + 4 <= end && (dt = (int)rd32(p)) != -1 && i <= 4; p += dt) {   /* (as bhReadPlayerData) */
        p += 4;
        if (!dt) continue;
        if (rd32(p) == SKIN_MAGIC) { skin = p; slen = dt; continue; }   /* for the next sub-model */
        if (i == 4) { c = p; clen = dt; break; }
        tex += mlb_textured(p);   /* texture blocks before ours */
        skin = NULL;
        slen = 0;
        i++;
    }
    if (!c || !mlb_textured(c)) { host_log("actor: head part of part file %d: no sub-model 4\n", 10 + chr); return; }
    if (slen > 2048 - 64 || 2048 + 64 + clen > (int)sizeof(d->hp.keep)) {   /* skin at keep + 0..63, model at keep + 2048 + 0..63 */
        host_log("actor: head part of part file %d does not fit (skin %d, model %d bytes)\n", 10 + chr, slen, clen);
        return;
    }
    if (d->tex_n >= (int)(sizeof(d->tex) / sizeof(d->tex[0]))) { host_log("actor: head part of part file %d: no texture slot left\n", 10 + chr); return; }
    memset(&d->hp.w, 0, sizeof(d->hp.w));
    if (skin) d->hp.w.skp[4] = (int*)((unsigned char*)memcpy(d->hp.keep + ((skin - pack) & 63), skin, slen) + 4);   /* (the data's alignment kept) */
    p = memcpy(d->hp.keep + 2048 + ((c - pack) & 63), c, clen);
    bhMlbBinRealize(p, m);
    if (!m->objP || m->obj_num > 16 || !m->texP || !tex_room("head part", 1)) { host_log("actor: head part of part file %d not loaded (%d nodes)\n", 10 + chr, m->objP ? (int)m->obj_num : -1); return; }
    if (d->hp.w.skp[4]) npSkinConvert(m->objP, d->hp.w.skp[4]);
    m->owP = d->hp.ow;
    memset(d->hp.ow, 0, sizeof(d->hp.ow));
    for (p = end, i = 0; i < 2; i++) {   /* motions, z translation */
        if (p + 4 > pack + sizeof(pack) || rd32(p) > (unsigned)(pack + sizeof(pack) - p - 4)) return;
        p += 4 + rd32(p);
    }
    for (i = 0; i <= tex; i++) {   /* the texture blocks of the textured sub-models, ours last */
        if (p + 4 > pack + sizeof(pack)) return;
        n = rd32(p);
        p += 4;
        if (n & 0x80000000) { p = pack + ((p - pack + 31) & ~31); n &= ~0x80000000; }
        if (n > (unsigned)(pack + sizeof(pack) - p)) return;
        if (i < tex) p += n;
    }
    bhSetMemPvpTexture(m->texP, p, 0);
    m->flg |= 0x200;
    d->tex[d->tex_n++] = m->texP;
    d->hp.w.mdl_n = 5;
    player_hair(d->hp.obj, &d->hp.w, chr, d->hp.exp0, d->hp.exp3);
    op->lkwkp = (unsigned char*)&d->w;
    d->hp.on = op->flg & 0x1;
    host_log("actor: head part of part file %d: %d nodes, %d bytes, texture %d bytes\n", 10 + chr, (int)m->obj_num, clen + slen, (int)n);
}

/* the head part a pose has put at the display actor's head: the ponytail swings once per tick (step) */
static void head_pose(Disguise* d, int step)
{
    O_WRK* op = &d->hp.obj[2];
    if (!d->hp.on || !d->mapped) return;
    njCalcPoint(&d->w.mlwP->owP[5].mtx, (NJS_POINT3*)&op->lox, (NJS_POINT3*)&op->px);
    if (step && op->id == 1211) bhObjClpn(op);
    bhCalcModel((BH_PWORK*)op);
}

static void head_draw(Disguise* d)
{
    O_WRK* op = &d->hp.obj[2];
    if (d->hp.on && d->mapped && (op->flg & 0x1) && !(op->mdflg & 0x1)) bhPutModel((BH_PWORK*)op);
}

/* An imported player model (pc/src/import, .cvxm: impmodel.h): its chunk list and textures as the
   game's, one mesh on the pelvis whose vertices imp_skin moves with the skeleton. */
_Static_assert(sizeof(CvxmSkin) == 44, "pc_cvxm_check's skin entry");
static int load_import_model(Disguise* d, int k)
{
    const char* path = actor_import_file[k];
    unsigned char own[16] = { 0 };
    FILE* f = pc_fopen(path, "rb");
    int size = 0, texn;
    PcCvxm cv;
    char why[96];
    if (f && !fseek(f, 0, SEEK_END)) size = (int)ftell(f);
    if (!f || size < 64 || size > KEEP_SIZE || !keep_fit(d, size) || fseek(f, 0, SEEK_SET)) {
        if (f) fclose(f);
        host_log("actor: %s cannot be read (%d bytes)\n", path, size);
        return 0;
    }
    unload(d);   /* (the buffer is the shown model's until here) */
    if (fread(d->keep, 1, size, f) != (size_t)size) size = 0;
    fclose(f);
    if (!size) { host_log("actor: %s cannot be read\n", path); return 0; }
    /* everything realize, the texture upload, the draw and imp_skin use (pc_parse.h); imp_skin writes
       the checked vertex range only */
    if (!pc_cvxm_check(d->keep, (uint32_t)size, &cv, why, sizeof(why))) {
        host_log("actor: %s is not usable: %s\n", path, why);
        return 0;
    }
    if (rd32(d->keep + 4) < 4)   /* (before version 4: Outbreak's CLUT alpha, 0xFF opaque; here 0x80 is: hair blended at up to twice its colour) */
        for (uint32_t q = rd32(d->keep + 12); rd32(d->keep + q) == 0x324D4954u; q += 32 + rd32(d->keep + q + 4))
            imp_tim2_alpha_255(d->keep + q + 32, (int)rd32(d->keep + q + 4));
    texn = realize(d, d->keep + 60, own);
    if (d->w.mdl_n != 1 || !tex_room(actor_imports[k].name, texn)) return 0;
    if (own[0]) {
        bhSetMemPvpTexture(d->w.mdl[0].texP, d->keep + rd32(d->keep + 12), 0);
        d->w.mdl[0].flg |= 0x200;
        d->tex[d->tex_n++] = d->w.mdl[0].texP;
    }
    d->skin = (const CvxmSkin*)(d->keep + cv.skin);
    d->vtx = (float*)(d->keep + cv.vtx);
    d->nv = (int)cv.nv;
    for (int k = 0; k < IMP_FACES - 1; k++)   /* (version 1: no face bones) */
        d->fbone[k] = rd32(d->keep + 4) >= 2 && d->keep[28 + k] < d->w.mdl[0].obj_num ? (signed char)d->keep[28 + k] : -1;
    d->blink = -1;
    d->blink_wait = 0;
    for (int e = 0; e < 2; e++) {   /* how far each lid closes (version 4, ob2.c lid_falloff: from its margin to the bottom of
        the eye's opening). Versions 2 and 3 took the crease above the lid for the lid (the brows went down, the eyes
        stayed open) and knew no opening: no blink until imported again */
        float t = 0;
        if (rd32(d->keep + 4) >= 4) memcpy(&t, d->keep + 36 + 4 * e, 4);
        d->lid_travel[e] = t > 0 && t < 0.3f ? t : 0;
        if (getenv("RECVX_IMPORT_POSE")) host_log("import: lid %d travel %.3f\n", e, d->lid_travel[e]);   /* (test aid) */
    }
    d->seed = 0x2545F491u * (unsigned)(k_seed++ + 1);
    return 1;
}

/* An imported model's face (its face bones, which otherwise follow the head): it blinks as the
   player characters do (player.c: after 30 ticks a 3% chance per tick, then 8 ticks: lids half,
   closed, half, open). Its jaw:
   - standing in for a cutscene double: the double's face mask's (MASK_WORK param.jawang, set by
     the lip sync, bhControlMask). The cast's masks talk mostly with muscles (lip shapes) and turn
     the jaw only 0..5 degrees; the Outbreak face has the jaw alone, so what is above the resting
     1.5 degrees is turned 2.5 times as far (4.9 -> 10 degrees).
   - the player in the game: the game has no mouth motion there (the player files have no face
     mask; their voice calls are sounds without lip data), so the jaw follows the loudness of the
     player's voice (SE port 4, snd_synth_voice_level): opens at once, closes over a few ticks. */
uint64_t gs_frame_count(void);   /* (test aid) */
float snd_synth_voice_level(void);   /* (snd.h) */
static void imp_face(Disguise* d, const BH_PWORK* src, int step)
{
    static const unsigned char tab[8] = { 0, 0, 1, 1, 2, 2, 1, 0 };   /* PlyEyeTab: 0 open, 1 half, 2 closed */
    O_WORK* ow = d->w.mlwP->owP;
    float shut = 0, jaw = 0;
    int k;
    if (step) {
        d->seed = d->seed * 1103515245u + 12345u;
        if (d->blink < 0 && ++d->blink_wait > 30 && (d->seed >> 16) % 100 >= 97) d->blink = 0, d->blink_wait = 0;
        else if (d->blink >= 0 && ++d->blink > 7) d->blink = -1;
    }
    if (d->blink >= 0) shut = tab[d->blink] / 2.0f;
    if (getenv("RECVX_IMPORT_BLINK")) shut = (float)atof(getenv("RECVX_IMPORT_BLINK"));   /* test aid: the lids held (0 open, 0.5 half, 1 closed) */
    if (src && src->mskp && src->exp1 && (d == &dg[ACTOR_DOUBLE] || d >= &dg[ACTOR_CAST])) {   /* (the double, the cast) */
        jaw = ((const MASK_WORK*)src->exp1)->param.jawang;   /* (degrees) */
        if (jaw > 1.5f) jaw += 1.5f * (jaw - 1.5f);
    } else if (d == &dg[0]) {
        if (step) {   /* ponytail: the mean level of everything on port 4 (co-op: the partner's voice there too) */
            float v = snd_synth_voice_level() * 90.0f;   /* (degrees: a scream peaks at ~0.18 -> the 14 degree cap, a grunt ~0.03 -> 3) */
            d->talk = v > d->talk ? v : d->talk * 0.6f;
            if (d->talk > 14.0f) d->talk = 14.0f;
            if (getenv("RECVX_IMPORT_POSE") && d->talk > 0.05f) host_log("import: voice jaw %.1f (level %.4f) frame %d\n", d->talk, v / 90.0f, (int)gs_frame_count());   /* (test aid) */
        }
        jaw = d->talk;
    }
    if (getenv("RECVX_IMPORT_JAW")) jaw = (float)atof(getenv("RECVX_IMPORT_JAW"));   /* test aid: the jaw open this far (degrees) */
    if (getenv("RECVX_IMPORT_POSE") && step && d == &dg[ACTOR_DOUBLE] && jaw != 0) host_log("import: double's jaw %.1f frame %d\n", jaw, (int)gs_frame_count());   /* (test aid) */
    jaw *= 3.14159265f / 180.0f;
    for (k = 0; k < 2; k++) {   /* the lid down along the head's up axis until its margin meets the bottom of the eye's
        opening (the crease above it and the brow stay) */
        int up = d->fbone[2 * k], c;
        if (up < 0 || shut <= 0) continue;
        for (c = 0; c < 3; c++) ow[up].mtx[12 + c] -= shut * d->lid_travel[k] * ow[up].mtx[4 + c];
    }
    if (d->fbone[4] >= 0 && jaw != 0) {   /* the jaw turned about the head's x axis at its pivot (the chin down and back) */
        float* m = ow[(int)d->fbone[4]].mtx, r[3][3], cs = cosf(jaw), sn = sinf(jaw);
        int c;
        for (c = 0; c < 3; c++) {   /* rows: x stays, y and z turn (row vectors: Rx(-jaw) x the head's rotation) */
            r[0][c] = m[c];
            r[1][c] = cs * m[4 + c] - sn * m[8 + c];
            r[2][c] = sn * m[4 + c] + cs * m[8 + c];
        }
        for (c = 0; c < 3; c++) { m[c] = r[0][c]; m[4 + c] = r[1][c]; m[8 + c] = r[2][c]; }
    }
}

/* An imported model's vertices for this pose: linear blend skinning from the node matrices (each
   bind orientation is the identity, so a node's bind matrix is a translation to its rest position),
   into the frame of the pelvis (node 1), which draws them. */
static void imp_skin(Disguise* d)
{
    const O_WORK* ow = d->w.mlwP->owP;
    const float* m1 = ow[1].mtx;
    int n = d->rig.n, i, j, c;
    if (!d->skin || !d->rig.ok) return;
    for (i = 0; i < d->nv; i++) {
        const CvxmSkin* s = &d->skin[i];
        float p[3] = { 0 }, q[3] = { 0 }, l[3], *o = d->vtx + 8 * i;
        for (j = 0; j < 4 && s->w[j] > 0; j++) {
            int k = s->node[j] < n ? s->node[j] : 1;
            const float* m = ow[k].mtx;
            float r[3];
            for (c = 0; c < 3; c++) r[c] = s->pos[c] - d->rig.rest[k][c];
            for (c = 0; c < 3; c++) {   /* (row vectors: p * M) */
                p[c] += s->w[j] * (r[0] * m[c] + r[1] * m[4 + c] + r[2] * m[8 + c] + m[12 + c]);
                q[c] += s->w[j] * (s->nrm[0] * m[c] + s->nrm[1] * m[4 + c] + s->nrm[2] * m[8 + c]);
            }
        }
        for (c = 0; c < 3; c++) l[c] = p[c] - m1[12 + c];
        for (c = 0; c < 3; c++) {   /* times the pelvis's rotation transposed (orthonormal: its inverse) */
            o[c] = l[0] * m1[c * 4] + l[1] * m1[c * 4 + 1] + l[2] * m1[c * 4 + 2];
            o[4 + c] = q[0] * m1[c * 4] + q[1] * m1[c * 4 + 1] + q[2] * m1[c * 4 + 2];
        }
    }
    if (getenv("RECVX_IMPORT_POSE") && d == &dg[0]) {   /* test aid: per pose (the last of a game frame counts) the node matrices
        in the frame of the actor's base matrix (pc/tools/import_check.py --pose renders the model in them) */
        static FILE* f;
        const float* b = (const float*)d->w.mtxbuf;
        if (!f) f = fopen(getenv("RECVX_IMPORT_POSE"), "w");
        if (f) {
            fprintf(f, "%d %d %d %d", (int)sys->gfrm_ct, (int)plp->mtn_no, (int)(plp->frm_no >> 16), n);
            for (i = 0; i < n; i++) {
                const float* m = ow[i].mtx;
                for (j = 0; j < 4; j++)   /* rows: x, y, z axes and the position, each in the base's axes */
                    for (c = 0; c < 3; c++) {
                        float v[3] = { m[j * 4], m[j * 4 + 1], m[j * 4 + 2] };
                        if (j == 3) for (int k = 0; k < 3; k++) v[k] -= b[12 + k];
                        fprintf(f, " %.4f", v[0] * b[c * 4] + v[1] * b[c * 4 + 1] + v[2] * b[c * 4 + 2]);
                    }
            }
            fputc('\n', f);
            fflush(f);
        }
    }
}

static int load(Disguise* d, int idx)
{
    int heap = heap_free(), anim;
    const char* name = idx >= ACTOR_PART ? "part file" : actor_model(idx)->name;
    memset(d->mth, 0, sizeof(d->mth));
    if (!(idx >= ACTOR_PART ? load_part_model(d, idx - ACTOR_PART + 10) : idx >= ACTOR_MODELS ? load_import_model(d, idx - ACTOR_MODELS) : load_room_model(d, idx))) {
        unload(d);
        return 0;
    }
    d->w.flg = 0x1;
    d->w.mdflg = 0x20;
    d->w.mlwP = d->w.mdl;
    d->w.mtx = (float(*)[16])d->w.mtxbuf;
    d->w.sx = d->w.sy = d->w.sz = d->w.sxb = d->w.syb = d->w.szb = 1.0f;
    d->w.mnwP = d->w.mnwPb = d->mth;
    d->w.mtn_add = 65536;
    anim = idx >= ACTOR_PART || actor_model(idx)->anim;
    if (anim) rig_build(&d->rig, d->w.mdl[0].objP, d->w.mdl[0].obj_num, 1);
    memset(d->hand_dir, 0, sizeof(d->hand_dir));
    if (d->skin && d->rig.ok && d->rig.map[13] >= 0) {   /* an imported model's left hand: towards its farthest vertex from the wrist */
        int i, c, hn = d->rig.map[13];
        float best = 0;
        for (i = 0; i < d->nv; i++) {
            float e = 0, v[3];
            if (d->skin[i].node[0] != hn || d->skin[i].w[0] < 0.5f) continue;
            for (c = 0; c < 3; c++) { v[c] = d->skin[i].pos[c] - d->rig.rest[hn][c]; e += v[c] * v[c]; }
            if (e > best) { best = e; for (c = 0; c < 3; c++) d->hand_dir[c] = v[c]; }
        }
    }
    host_log("actor: %s loaded (%d sub-models, %d nodes, %d KB texture heap), %s; map", name, d->w.mdl_n,
             d->w.mdl[0].obj_num, (heap - heap_free()) >> 10, d->rig.ok ? "player animations" : "own idle motion");
    if (d->rig.ok) { int k; for (k = 0; k < 22; k++) host_log(" %d", d->rig.map[k]); }
    host_log("\n");
    if (idx < ACTOR_MODELS && d->rig.ok && d->rig.map[5] == 5) {   /* (bhObjClpn reads node 5) */
        int k;
        for (k = 0; k < 3; k++)
            if (doubles[0][k] == actor_models[idx].id) load_head(d, 0);
            else if (doubles[3][k] == actor_models[idx].id) load_head(d, 3);
    }
    if (getenv("RECVX_RIG_DUMP")) {   /* test aid: the rest skeleton (node, parent, model-space position, bind angles) */
        Rig r;
        const NJS_CNK_OBJECT* o = d->w.mdl[0].objP;
        int i;
        rig_build(&r, o, d->w.mdl[0].obj_num, 0);
        for (i = 0; i < r.n; i++)
            host_log("actor:  node %2d par %2d rest %7.2f %7.2f %7.2f ang %6d %6d %6d%s\n", i, r.par[i], r.rest[i][0], r.rest[i][1], r.rest[i][2],
                     (int)o[i].ang[0], (int)o[i].ang[1], (int)o[i].ang[2], o[i].model ? " mesh" : "");
    }
    return 1;
}

/* ---------------------------------------------------------------- gender-matched motions
   A model of the other sex than the character played moves with the motions of a player
   character of its own sex (appear_motion_chr: Chris's for a man while Claire is played,
   Claire's for a woman while a man is played). A hidden copy of the source's skeleton (its node
   offsets) plays, every tick, the motion of that character equivalent to the source's: the same
   number in the base and weapon sets (the numbers are the same for all characters: PlMtnAct,
   PlMtnWpn), the other character's motion of the enemy's set while grabbed
   (appear_grab_motion), and the source's own motion where the other set has none (room acting,
   motions only Claire has); at the source's phase (frame scaled by the motions' lengths),
   blending when the source blends, mirrored when it is, the head looking where the source's
   looks and, for our player, the third-person aim bend. The display actor is posed from the copy
   instead of from the source; a part-file mesh on the source itself is drawn with the copy's node
   matrices (actor_put_body). The source stays what the game moves, collides and hits with. */
typedef struct Shadow {
    unsigned char base[400 << 10] __attribute__((aligned(64)));   /* the motion chunk list of the part file (bhReadPlayerData: 384 KB) */
    unsigned char wpn[96 << 10] __attribute__((aligned(64)));     /* of the weapon file (bhReadWeaponData: 64 KB) */
    O_WORK ow[22] __attribute__((aligned(16)));
    NJS_CNK_OBJECT obj[22];
    MN_WORK mth[512], base_mth[512];    /* the set; the base motions alone (a weapon change starts from them) */
    BH_PWORK w;
    int chr, fail, wpn_no;              /* the motion set loaded (ply_id, -1 none), one that failed, the weapon's */
    int mesh, motion;                   /* actor_source */
    float pitch;                        /* actor_source: the source's free-aim bend */
    BH_PWORK *src, *drv;                /* actor_source's source; this tick's pose (NULL: the source's own) */
    NJS_CNK_OBJECT* of;                 /* the source's objects the copy was made from */
    int body;                           /* the source's own mesh is drawn with our matrices */
} Shadow;
static Shadow sh[2] __attribute__((aligned(64)));
static int redraw;                      /* actor_put_player: pose again from this tick's copy */

/* motions of a chunk list into mth[first..] (bhReadPlayerData / bhReadWeaponData) */
static void realize_motions(MN_WORK* mth, int first, unsigned char* p)
{
    int i, dt;
    for (i = first; (dt = (int)rd32(p)) != -1 && i < 512; i++, p += dt) {
        p += 4;
        if (dt) bhMnbBinRealize(p, &mth[i]);
    }
}

/* the motion set of player character chr (part file 10 + chr), with the weapon motions of weapon
   wpn (file wpn + chr * 30 + 20) over slots 100.. as the game loads them; for another weapon of the
   same character only the weapon file is read (the base motions are kept realized in base_mth) */
static int shadow_load(Shadow* s, int chr, int wpn)
{
    unsigned char* p;
    unsigned int n;
    int k;
    if (chr != s->chr) {
        s->chr = -1;
        memset(s->base_mth, 0, sizeof(s->base_mth));
        if (!read_file(sys->sys_partid, 10 + chr)) return 0;
        p = pack + 4 + rd32(pack);   /* past the model block */
        n = rd32(p);
        if (n > sizeof(s->base) || p + 4 + n > pack + sizeof(pack) || !list_len(p + 4, p + 4 + n)) return 0;
        memcpy(s->base, p + 4, n);
        realize_motions(s->base_mth, 0, s->base);   /* (in place: realized once) */
        s->chr = chr;
    }
    memcpy(s->mth, s->base_mth, sizeof(s->mth));
    s->wpn_no = wpn;
    if (wpn < 0 || wpn >= 30 || !read_file(sys->sys_partid, wpn + chr * 30 + 20)) return 1;
    for (p = pack, k = 0; k < 4; k++) {   /* right model, its texture, left model, its texture */
        n = rd32(p);
        p += 4;
        if ((k & 1) && (n & 0x80000000)) { p = pack + ((p - pack + 63) & ~63); n &= ~0x80000000; }
        if (n > (unsigned)(pack + sizeof(pack) - p)) return 1;
        p += n;
    }
    n = rd32(p);
    if (n && n <= sizeof(s->wpn) && n <= (unsigned)(pack + sizeof(pack) - p - 4) && list_len(p + 4, p + 4 + n)) {
        memcpy(s->wpn, p + 4, n);
        realize_motions(s->mth, 100, s->wpn);
    }
    return 1;
}

int pc_plp_frm = -1, pc_plp_hokan;
unsigned int pc_plp_mtn;
void* pc_plp_set;

/* our player's frame recorded by bhControlPlayer still belongs to its motion: enemy code that runs after
   it may start another one (a grab, Alexia's burn), whose frm_no is then the one to play */
static int frame_kept(const BH_PWORK* p)
{
    return p == plp && pc_plp_frm >= 0 && p->mtn_no == pc_plp_mtn && (void*)p->mnwP == pc_plp_set;
}

/* the frame p shows: our player's from before its bhSetMotion advanced plp->frm_no (player.c
   bhControlPlayer, pc_plp_frm), so a copy playing it is not a frame ahead and does not show a motion's
   first frame when the game leaves it on its last (the twitch after a shot); the partner's as received */
int actor_frame(const BH_PWORK* p)
{
    return frame_kept(p) ? pc_plp_frm : p->frm_no;
}

/* the blend count p's bhSetMotion took this tick (it decrements it): our player's as recorded, so a blend
   the game starts within a motion (the end of raising a weapon) is copied too, or as an enemy just set it;
   else one more than left */
static int frame_hokan(const BH_PWORK* p)
{
    if (p == plp && pc_plp_frm >= 0) return frame_kept(p) ? pc_plp_hokan : p->hokan_count;
    return p->hokan_count ? p->hokan_count + 1 : 0;
}

/* the copy posed in character s->chr's motion equivalent to src's (character `from`), or NULL */
static BH_PWORK* shadow_step(Shadow* s, BH_PWORK* src, int from)
{
    ML_WORK* sm = src->mlwP;
    MN_WORK *set = src->mnwP, *own;
    int mtn = src->mtn_no, i, k;
    long frm = actor_frame(src);   /* (after its advance a motion's last frame has wrapped to its first) */
    if (!set || mtn < 0 || mtn >= 512 || !set[mtn].datP || set[mtn].obj_num > 22 || sm->obj_num < 22) return NULL;
    own = &set[mtn];
    if (set == src->mnwPb) {   /* base and weapon motions: the same number */
        if (s->mth[mtn].datP && s->mth[mtn].obj_num <= 22) set = s->mth;
    } else {   /* grabbed: the player plays the enemy's set */
        for (i = 0; i < (int)sys->ewk_n && i < PC_ENE_SLOTS; i++)
            if ((ene[i].flg & 0x1) && (ene[i].mnwP == set || ene[i].mnwPb == set)) {
                k = appear_grab_motion(ene[i].id, mtn, from, s->chr);
                if (k >= 0 && set[k].datP && set[k].obj_num <= 22) mtn = k;
                break;
            }
    }
    if (&set[mtn] != own && own->frm_num) frm = frm * set[mtn].frm_num / own->frm_num;   /* the same phase */
    if (frm >= (long)set[mtn].frm_num << 16) frm = ((long)set[mtn].frm_num << 16) - 1;
    if (s->of != sm->objP) {   /* a new source: its hierarchy, a pose without blending */
        memcpy(s->obj, sm->objP, sizeof(s->obj));
        memset(s->ow, 0, sizeof(s->ow));
        memset(&s->w, 0, sizeof(s->w));
        s->w.mdl[0].objP = s->obj;
        s->w.mdl[0].owP = s->ow;
        s->w.mdl[0].obj_num = 22;
        s->w.mlwP = &s->w.mdl[0];
        s->w.mdl_n = 1;
        s->w.mtx = (float(*)[16])s->w.mtxbuf;
        s->w.flg = 0x1000;   /* bhCalcModel: the base matrix as it is (the source's) */
        s->of = sm->objP;
    }
    for (i = 1; i < 22; i++) memcpy(s->obj[i].pos, sm->objP[i].pos, sizeof(s->obj[i].pos));   /* the source's bones */
    s->w.hokan_count = s->w.mnwP ? frame_hokan(src) : 0;   /* blending as the source's this tick */
    s->w.hokan_rate = src->hokan_rate;
    s->w.mnwP = set;
    s->w.mtn_no = mtn;
    s->w.frm_no = (int)frm;
    bhSetMotion(&s->w, 0, src->mtn_md, src->mtn_tp);
    for (i = 4; i <= 5; i++) memcpy(s->obj[i].ang, sm->objP[i].ang, sizeof(s->obj[i].ang));   /* neck and head look as the source's (bhControlPlayerHead) */
    memcpy(s->w.mtxbuf, src->mtx, sizeof(float) * 16);
    bhCalcModel(&s->w);
    if ((sm->owP[7].flg & 0x2) && (sm->owP[9].flg & 0x2)) {   /* the source's arm is held by the lighter's IK (player.c
        bhControlPlayer, coop_glue.c lighter_arm), which the other set's motions do not have: the same IK on the
        copy, the arm towards a point in front of its chest, as the game does for the character played */
        NJS_POINT3 eff1 = { from == 0 ? 1.8f : 2.4f, -3.0f, from == 0 ? -3.0f : -3.5f }, eff0;
        BH_PWORK* keep = plp;
        njCalcPoint(&s->ow[4].mtx, &eff1, &eff0);
        plp = &s->w;   /* bhArmIkMdk reads the model's nodes through plp */
        bhArmIkMdk(&s->w, 6, &eff0, 35498);
        plp = keep;
        s->obj[9].ang[1] = -910;
        bhCalcModel(&s->w);
    }
    {   /* "Move While Aiming": the source's legs walking under its motion (tps_glue.c), in this set's walk */
        const TpsLegs* l = src == plp ? &tps_legs_me : &tps_legs_p2;
        if (l->w > 0 && l->mtn >= 0 && l->mtn < 100 && s->mth[l->mtn].datP && s->mth[l->mtn].obj_num <= 22) tps_legs(&s->w, s->mth, l);
    }
    tps_bend(&s->w, s->pitch);
    return &s->w;
}

void actor_source(int who, BH_PWORK* src, int mesh, int motion, float pitch)
{
    if (who < 0 || who > 1) return;
    sh[who].src = src;
    sh[who].mesh = mesh;
    sh[who].motion = motion;
    sh[who].pitch = pitch;
}

/* this tick's gender-matched pose of slot who's source, or NULL */
static BH_PWORK* shadow(int who, BH_PWORK* src)
{
    Shadow* s = &sh[who];
    Disguise* d = &dg[who];
    int anim, chr, k;
    s->body = 0;
    if (s->src != src || s->motion < 0 || s->motion > 3) return NULL;   /* no actor_source this tick (a lobby preview) */
    s->src = NULL;
    if (d->model >= 0) anim = !d->rig.ok ? ANIM_IDLE : d->model >= ACTOR_PART ? appear_part_anim(d->model - ACTOR_PART) : actor_model(d->model)->anim;
    else anim = s->mesh >= 0 && s->mesh <= 3 ? appear_part_anim(s->mesh) : ANIM_IDLE;
    chr = appear_motion_chr(anim, s->motion);
    if (chr < 0 || chr == s->fail) return NULL;
    if ((chr != s->chr || src->wpnr_no != s->wpn_no) && !shadow_load(s, chr, src->wpnr_no)) {
        s->fail = chr;
        host_log("actor: the motions of part file %d cannot be read\n", 10 + chr);
        return NULL;
    }
    if (!shadow_step(s, src, s->motion)) return NULL;
    if (d->model < 0) {   /* a part-file mesh on the source: what is linked to its head and hands goes where they are drawn */
        for (k = 5; k <= 13; k += 4) memcpy(src->mlwP->owP[k].mtx, s->ow[k].mtx, sizeof(s->ow[k].mtx));
        s->body = 1;
        actor_on = 1;
    }
    return &s->w;
}

/* src drawn (bhPutModel), with the node matrices of its gender-matched pose if it has one */
int actor_put_body(int who, BH_PWORK* src)
{
    static float keep[22][16];
    static Sint32 ang[22][3];
    Shadow* s = &sh[who & 1];
    O_WORK* ow = src->mlwP ? src->mlwP->owP : NULL;
    NJS_CNK_OBJECT* o;
    int i;
    if (!s->body || !ow || s->of != src->mlwP->objP || src->mlwP->obj_num < 22) { bhPutModel(src); return 1; }
    o = src->mlwP->objP;
    for (i = 0; i < 22; i++) {   /* the matrices and the angles (the skin blends joints with them, npCalcSkin) */
        memcpy(keep[i], ow[i].mtx, sizeof(keep[i])); memcpy(ow[i].mtx, s->ow[i].mtx, sizeof(keep[i]));
        memcpy(ang[i], o[i].ang, sizeof(ang[i])); memcpy(o[i].ang, s->obj[i].ang, sizeof(ang[i]));
    }
    bhPutModel(src);
    for (i = 0; i < 22; i++) { memcpy(ow[i].mtx, keep[i], sizeof(keep[i])); memcpy(o[i].ang, ang[i], sizeof(ang[i])); }
    return 1;
}

/* ---------------------------------------------------------------- the source's head and hands
   What the game links to the source's head and hands (weapons, the head part, the lighter flame,
   the muzzle flash) is drawn where the model has them: for this tick's effects, objects and drawing
   the source's nodes 5, 9 and 13 carry the model's (actor_pose, shadow). Before the next tick's
   player control the game's own come back (actor_hands_back, game.c): bhControlPlayer reads the hand
   for the shot (the wall test of bhCheckGunAtari starts at the muzzle, owP[9]), the knife and sounds,
   so a taller model shot higher than the crosshair.
   Put back only while the skeleton is still the one they were saved from (the chest, node 3, which we never move): a
   script that moves the player and computes its skeleton again (9100: Chris hidden and put elsewhere for the
   Spotter scene; a hidden player's skeleton is not computed again by bhControlPlayer) has the game's own nodes
   there. Put back over them, the head stayed where the player had stood: in a Spotter's searchlight there
   (bhEff_E11_CheckCollisionPlayer tests the player's nodes), the room's "spotted" event cut into the scene. */
static struct { BH_PWORK* src; O_WORK* ow; float mtx[3][16], chest[16]; } real[2];
static float aim_ofs[3];   /* our weapon hand as drawn minus the real one, in the frame of the real chest (node 3) */
static float aim_rot[9];   /* the drawn weapon hand's rotation in that frame (the gender-matched pose's hand turns otherwise) */
static int aim_set;        /* aim_rot is set */

static void hands_save(int who, BH_PWORK* src)
{
    int i;
    if (real[who].src == src && real[who].ow == src->mlwP->owP && !memcmp(real[who].chest, real[who].ow[3].mtx, sizeof(real[who].chest)))
        return;   /* saved this tick (and not computed again since) */
    if (src->mlwP->obj_num < 14) return;
    real[who].src = src;
    real[who].ow = src->mlwP->owP;
    for (i = 0; i < 3; i++) memcpy(real[who].mtx[i], real[who].ow[5 + 4 * i].mtx, sizeof(real[who].mtx[i]));
    memcpy(real[who].chest, real[who].ow[3].mtx, sizeof(real[who].chest));
}

static void hands_back(int who)
{
    O_WORK* ow = real[who].ow;
    int i, j;
    if (!real[who].src) return;
    if (real[who].src->mlwP && ow == real[who].src->mlwP->owP && !memcmp(real[who].chest, ow[3].mtx, sizeof(real[who].chest))) {
        if (who == 0 && real[0].src == plp) {   /* local = world x R^T, R: the chest's rows */
            float dl[3];
            for (j = 0; j < 3; j++) dl[j] = ow[9].mtx[12 + j] - real[0].mtx[1][12 + j];
            for (j = 0; j < 3; j++) aim_ofs[j] = dl[0] * ow[3].mtx[j * 4] + dl[1] * ow[3].mtx[j * 4 + 1] + dl[2] * ow[3].mtx[j * 4 + 2];
            for (i = 0; i < 3; i++)   /* hand rows x chest^T */
                for (j = 0; j < 3; j++)
                    aim_rot[i * 3 + j] = ow[9].mtx[i * 4] * ow[3].mtx[j * 4] + ow[9].mtx[i * 4 + 1] * ow[3].mtx[j * 4 + 1] + ow[9].mtx[i * 4 + 2] * ow[3].mtx[j * 4 + 2];
            aim_set = 1;
        }
        for (i = 0; i < 3; i++) memcpy(ow[5 + 4 * i].mtx, real[who].mtx[i], sizeof(real[who].mtx[i]));
    }
    real[who].src = NULL;
}

/* game.c, before bhControlPlayer */
void actor_hands_back(void)
{
    actor_ticks++;
    pc_plp_frm = -1;   /* (set again by this tick's bhControlPlayer) */
    if (!real[0].src) aim_ofs[0] = aim_ofs[1] = aim_ofs[2] = 0, aim_set = 0;
    hands_back(0);
    hands_back(1);
}

void tps_trace_hands(const float* drawn, const float* real);
static void trace_hands(int who, BH_PWORK* src)
{
    if (who == 0 && !redraw && real[0].src == src && src == plp) tps_trace_hands(src->mlwP->owP[9].mtx, real[0].mtx[1]);
}

/* tps_glue.c (free aim): where p's weapon hand is drawn relative to its own, in world space for p's
   current chest; 0 without a model */
void actor_aim_offset(const BH_PWORK* p, float* out)
{
    const float* r = p->mlwP->owP[3].mtx;
    int j;
    for (j = 0; j < 3; j++) out[j] = p == plp && actor_on ? aim_ofs[0] * r[j] + aim_ofs[1] * r[4 + j] + aim_ofs[2] * r[8 + j] : 0;
}

/* tps_glue.c (free aim, with the retargeting): the weapon hand as it will be drawn, for p's current
   (bent) skeleton: hand[16] a node matrix (rows: axes, origin). 0 when no model is shown. */
int actor_aim_hand(const BH_PWORK* p, float* hand)
{
    const float* r = p->mlwP->owP[3].mtx;
    int i, j;
    if (!actor_retarget || p != plp || !actor_on || !aim_set) return 0;
    memset(hand, 0, 16 * sizeof(float));
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++) hand[i * 4 + j] = aim_rot[i * 3] * r[j] + aim_rot[i * 3 + 1] * r[4 + j] + aim_rot[i * 3 + 2] * r[8 + j];
    actor_aim_offset(p, &hand[12]);
    for (j = 0; j < 3; j++) hand[12 + j] += p->mlwP->owP[9].mtx[12 + j];
    hand[15] = 1;
    return 1;
}

/* tps_glue.c: the over-the-shoulder camera's height, shoulder offset and distance for our model,
   relative to the character's (its head's height over the source's: a Tyrant's 1.4; 1 without a model
   or with the previous retargeting) */
float actor_cam_scale(void)
{
    const Disguise* d = &dg[0];
    const NJS_CNK_OBJECT* o;
    int k, i;
    float hs = 0, ht, s;
    if (!actor_retarget || d->model < 0 || !d->rig.ok || !plp || !plp->mlwP || plp->mlwP->obj_num < 6) return 1;
    k = d->rig.map[5] >= 0 ? d->rig.map[5] : d->rig.map[4];
    if (k < 0) return 1;
    ht = d->rig.rest[k][1] - d->rig.rest[0][1];
    for (o = plp->mlwP->objP, i = 1; i <= 5; i++) hs += o[i].pos[1];   /* the player's chain 1..5 (pelvis .. head) */
    if (hs < 5 || ht < 3) return 1;
    s = ht / hs;
    return s < 0.8f ? 0.8f : s > 1.8f ? 1.8f : s;
}

/* ---------------------------------------------------------------- interface */
int actor_set(int who, int idx)
{
    Disguise* d = &dg[who];
    if (idx == d->model) return idx >= 0;
    if (idx >= 0 && idx == d->fail) return 0;
    unload(d);
    actor_on = 0;
    for (who = 0; who < ACTOR_SLOTS; who++) actor_on |= dg[who].model >= 0;
    actor_on |= sh[0].body | sh[1].body;
    if (d >= &dg[ACTOR_CAST]) cast_mem_reset();   /* (all cast slots unloaded: their buffer is free again) */
    if (idx < 0 || (idx >= ACTOR_MODELS && idx < ACTOR_PART && !actor_model(idx)) || idx > ACTOR_PART + 9) return 0;
    if (!load(d, idx)) {
        d->fail = idx;
        host_log("actor: model %d cannot be shown\n", idx);
        return 0;
    }
    d->model = idx;
    d->fail = -1;
    actor_on = 1;
    if ((d == &dg[ACTOR_DOUBLE] && g_appear_face) || d >= &dg[ACTOR_CAST]) face_init(d, idx);   /* the cast: always (a model with a face mask) */
    return 1;
}

int actor_shown(int who) { return dg[who].model >= 0; }
int actor_shown_model(int who) { return dg[who].model; }   /* (the --dev cutscene log) */
int actor_rigged(int who) { return dg[who].model >= 0 && dg[who].rig.ok; }

/* pose slot `who` from `src` (after its bhSetMotion and bhCalcModel), in its gender-matched pose
   if it has one (slots 0, 1: actor_source) */
/* The weapon models (weapon file: sys->obwp[0..1], the partner's copies) are a hand piece, object 0
   (Claire's or Chris's gloved hand holding the grip, 43 vertices; unarmed: the fist alone), with the
   weapon as its child (gun, slide, magazine...). An imported model has its own hands in its skinned
   mesh: the piece showed as a second hand around the grip, beside the model's. Hidden on it (the
   object's NJD_EVAL_HIDE: the weapon below it is still drawn); shown again for any other model. */
void actor_weapon_hands(int who, O_WRK* wpn, int n)
{
    int hide = who >= 0 && who < ACTOR_SLOTS && dg[who].model >= 0 && dg[who].skin && dg[who].mapped;
    for (int i = 0; wpn && i < n; i++) {   /* (sys->obwp is NULL before the first game: co-op started from the command line) */
        NJS_CNK_OBJECT* o = wpn[i].mlwP && wpn[i].mlwP->objP && wpn[i].mlwP->obj_num > 0 ? &wpn[i].mlwP->objP[0] : NULL;
        if (!o || (wpn[i].lkono != 9 && wpn[i].lkono != 13)) continue;
        if (hide) o->evalflags |= 0x8; else o->evalflags &= ~0x8u;   /* (NJD_EVAL_HIDE: MdlPut.c skips the model, not the children) */
    }
}

void actor_pose(int who, BH_PWORK* src)
{
    Disguise* d = &dg[who];
    ML_WORK* m = d->w.mlwP;
    BH_PWORK* drv;
    if (!src->mlwP || !src->mlwP->objP || !src->mlwP->owP || !src->mtx) return;
    if (who < 2) hands_save(who, src);
    if (who < 2 && !redraw) sh[who].drv = shadow(who, src);
    drv = who < 2 && sh[who].drv ? sh[who].drv : src;
    if (d->model < 0) { trace_hands(who, src); return; }
    d->w.px = src->px; d->w.py = src->py; d->w.pz = src->pz;
    d->w.ax = src->ax; d->w.ay = src->ay; d->w.az = src->az;
    d->w.draw_tp = src->draw_tp;
    memcpy(d->w.mtxbuf, src->mtx, sizeof(float) * 16);   /* the source's base matrix (also when linked) */
    d->posed = 1;
    d->mapped = 0;
    if (d->rig.ok) {
        Rig sr;   /* the source's skeleton: from its current node offsets (bind angles are 0); built each pose (a few
                     dozen nodes): a cache keyed by the objects' address kept a stale rig when a room load put another model there */
        rig_build(&sr, drv->mlwP->objP, drv->mlwP->obj_num, 0);   /* (a cast double's face mask context has 31 nodes, 9 of them face) */
        if (sr.ok) {
            static const void* dumped;
            if (getenv("RECVX_RIG_DUMP") && dumped != drv->mlwP->objP) {
                int i;
                dumped = drv->mlwP->objP;
                host_log("actor: source skeleton; map");
                for (i = 0; i < 22; i++) host_log(" %d", sr.map[i]);
                host_log("\n");
                for (i = 0; i < sr.n; i++) host_log("actor:  node %2d par %2d rest %7.2f %7.2f %7.2f\n", i, sr.par[i], sr.rest[i][0], sr.rest[i][1], sr.rest[i][2]);
            }
            retarget(d, drv, &sr);
            if (getenv("RECVX_IMPORT_POSE") && d->skin && who == 0) {   /* test aid: the source's mapped joints (player node order, -1: none), as the pose dump */
                static FILE* f;
                char name[600];
                const float* b = (const float*)d->w.mtxbuf;
                if (!f) { snprintf(name, sizeof(name), "%s.src", getenv("RECVX_IMPORT_POSE")); f = fopen(name, "w"); }
                if (f) {
                    int k, c;
                    fprintf(f, "%d", (int)drv->mtn_no);
                    for (k = 0; k < 22; k++)
                        for (c = 0; c < 3; c++) {
                            const float* w = sr.map[k] >= 0 ? &drv->mlwP->owP[(int)sr.map[k]].mtx[12] : b + 12;
                            fprintf(f, " %.4f", (w[0] - b[12]) * b[c * 4] + (w[1] - b[13]) * b[c * 4 + 1] + (w[2] - b[14]) * b[c * 4 + 2]);
                        }
                    fputc('\n', f);
                    fflush(f);
                }
            }
            if (getenv("RECVX_RIG_DUMP") && who == 0 && !redraw) {   /* the feet over the floor: the source's lowest ankle/toe and the model's */
                float fs = 1e9f, ft = 1e9f;
                int k;
                for (k = 16; k <= 21; k++) {
                    if (k == 18 || k == 19) continue;
                    if (sr.map[k] >= 0) fs = fminf(fs, drv->mlwP->owP[(int)sr.map[k]].mtx[13] - (*src->mtx)[13]);
                    if (d->rig.map[k] >= 0) ft = fminf(ft, m->owP[(int)d->rig.map[k]].mtx[13] - (*src->mtx)[13]);
                }
                host_log("actor: feet motion %d frame %d: source %.2f model %.2f over the floor\n", (int)drv->mtn_no, (int)(drv->frm_no >> 16), fs, ft);
            }
            d->mapped = 1;
            if (who < 2) {   /* the source's weapon hands where the model's hands are, turned as the pose's (the copy has the source's nodes) */
                int k;
                for (k = 9; k <= 13; k += 4)
                    if (d->rig.map[k] >= 0 && sr.map[k] >= 0) {
                        float* h = src->mlwP->owP[(int)sr.map[k]].mtx;
                        if (drv != src) memcpy(h, drv->mlwP->owP[(int)sr.map[k]].mtx, 12 * sizeof(float));
                        memcpy(&h[12], &m->owP[(int)d->rig.map[k]].mtx[12], 12);
                    } else if (actor_retarget && sr.map[k] >= 0) {   /* a model without that hand (Nosferatu's right, the
                        Bandersnatch's left): what the game links to it (a weapon, a gloved hand) shrunk to a point
                        instead of floating where the source's hand is */
                        float* h = src->mlwP->owP[(int)sr.map[k]].mtx;
                        int j;
                        for (j = 0; j < 12; j++) h[j] *= 1e-3f;
                    }
                trace_hands(who, src);
            } else if (dbl_tick) {   /* the cutscene double: what the script links to its hands (the cast's hand
                pieces, props) goes to the model's hands; its own come back with its next bhCalcModel. The cutscene cast:
                turned as the model's hands too, so a piece lies along the model's forearm, not the record's (cast_glue.c
                hides the record's own hand pieces where the model has hands of its own) */
                int k;
                for (k = 9; k <= 13; k += 4)
                    if (d->rig.map[k] >= 0 && sr.map[k] >= 0)
                        memcpy(who >= ACTOR_CAST && cast_hands ? src->mlwP->owP[(int)sr.map[k]].mtx : &src->mlwP->owP[(int)sr.map[k]].mtx[12],
                               who >= ACTOR_CAST && cast_hands ? m->owP[(int)d->rig.map[k]].mtx : &m->owP[(int)d->rig.map[k]].mtx[12],
                               who >= ACTOR_CAST && cast_hands ? 15 * sizeof(float) : 12);
            }
            d->src_hand[0] = sr.map[9];
            d->src_hand[1] = sr.map[13];
            d->src_head[0] = sr.map[4];
            d->src_head[1] = sr.map[5];
            head_pose(d, who == ACTOR_DOUBLE || who >= ACTOR_CAST ? dbl_tick == 1 : !redraw);   /* (one head part, one swing per tick) */
            if (d->skin) imp_face(d, src, who == ACTOR_DOUBLE || who >= ACTOR_CAST ? dbl_tick == 1 : !redraw);
            imp_skin(d);
            return;
        }
    }
    {   /* own idle motion, looped; a motion with more tracks than the model has nodes would write past it */
        int idle = d->model < ACTOR_MODELS ? actor_models[d->model].idle : 0;
        d->w.mtn_no = idle;
        if (d->mth[idle].datP && d->mth[idle].obj_num <= m->obj_num) bhSetMotion(&d->w, 65536, 0, NULL);
        bhCalcTree((NJS_MATRIX*)d->w.mtx, m);
    }
}

void actor_draw(int who)
{
    if (dg[who].model >= 0 && dg[who].posed) { bhPutModel(&dg[who].w); head_draw(&dg[who]); }
}

/* game.c bhAllDrawModel, in place of bhPutModel(plp): 1 = drawn */
int actor_put_player(void)
{
    if (dg[0].model < 0 || !dg[0].posed) return sh[0].body && actor_put_body(0, plp);   /* a part-file mesh in its gender-matched pose */
    if (dg[0].rig.ok) {   /* again from the final matrices (code after bhControlPlayer may bend bones); a gender-matched pose: this tick's */
        hands_back(0);   /* (the game's own hands, which the arm IK reads; moved again) */
        redraw = 1;
        actor_pose(0, plp);
        redraw = 0;
    }
    bhPutModel(&dg[0].w);
    head_draw(&dg[0]);
    return 1;
}

/* eneset.c bhDrawEnemy, in place of bhPutModel(ep): the event double of the character played
   (cutscenes) drawn as the chosen model. 1 = drawn. */
int cast_n;                             /* cast_glue.c: records drawn as other models in this room */
int cast_draw(BH_PWORK* ep);
void cast_tick(void);
void cast_show(void);
void pc_voice_tick(void);               /* cast_glue.c: an enemy model's sounds in place of a line, each at its time */
void cast_double_parts(BH_PWORK* ep, int pid);
static void cast_hands_draw(int who, const BH_PWORK* ep);
int actor_put_enemy(BH_PWORK* ep)
{
    int i, id = ep->id, pid = sys->ply_id & 3;
    if (cast_n && cast_draw(ep)) return 1;   /* the cutscene cast */
    if (dg[ACTOR_DOUBLE].model < 0) return 0;
    for (i = 0; i < 3 && doubles[pid][i] != id; i++);
    if (i == 3 || !id) return 0;
    if (ep != dbl_posed) {   /* (posed from this one last: the same pose, its hands already moved) */
        actor_pose(ACTOR_DOUBLE, ep);
        dbl_posed = dg[ACTOR_DOUBLE].mapped ? ep : NULL;   /* two doubles: each drawn in its own pose */
    }
    if (!dg[ACTOR_DOUBLE].mapped) return 0;   /* only a skeleton-driven model stands in for a cutscene double */
    bhPutModel(&dg[ACTOR_DOUBLE].w);
    head_draw(&dg[ACTOR_DOUBLE]);
    cast_hands_draw(ACTOR_DOUBLE, ep);
    return 1;
}

/* ---------------------------------------------------------------- the cutscene double's face
   The cast's cutscene models (ids 91..99) talk with a face mask (face.c, face_bh.c): a muscle model
   that bends the face mesh, the jaw, tongue and eyes, driven per phoneme by the room's lip data
   (bhSetLip; LIP_WORK: phoneme, time). A cast model shown for the double gets its own mask set up
   (bhInitMask, its allocations in face_mem) and every tick the double's lip state; its own face
   table gives the mouth shapes. The double's expressions (bhSetMask: face sets authored for that
   character's mask) are not taken: the model keeps its neutral face. Option "Talking faces"
   (g_appear_face). */
#define FACE_MEM (128 << 10)            /* per face (a cast model's mask: about 55 KB) */
static unsigned char face_mem[1 + ACTOR_SLOTS - ACTOR_CAST][FACE_MEM] __attribute__((aligned(64)));   /* the double's, then the cast slots' */

/* A rest mesh of the mask (its face, its tongue) on a 64-byte boundary, as bhInitMask places the
   copies it morphs: _fmCnkCalcMuscle / _fmCnkCalcTang read the rest vertices with lq, which drops the
   low 4 address bits. In the room file they are aligned for the room the model was made for; our copy
   of a model from another room's list kept its 8-byte offset, and every moved vertex was read two
   floats early (the face's rim jumped out of the head as a spike). */
static NJS_CNK_MODEL* face_src(NJS_CNK_MODEL* m, int vofs)
{
    NJS_CNK_MODEL* c;
    int n;
    if (!m) return NULL;
    n = (((unsigned int)m->vlist[1] >> 16) * vofs + 128) * 4;   /* (bhInitMask's copy size) */
    if (!((uintptr_t)m->vlist & 15)) return m;
    if (!(c = (NJS_CNK_MODEL*)bhGetFreeMemory(sizeof(NJS_CNK_MODEL), 64))) return NULL;   /* face_mem full */
    *c = *m;
    if (!(c->vlist = (int*)bhGetFreeMemory(n, 64))) return NULL;
    memcpy(c->vlist, m->vlist, n);
    return c;
}

static void face_init(Disguise* d, int idx)
{
    ML_WORK* m = d->w.mlwP;
    MASK_WORK* fm;
    unsigned char *memp = sys->memp, *arena = freemem, *mem = face_mem[d >= &dg[ACTOR_CAST] ? 1 + (d - &dg[ACTOR_CAST]) : 0];
    int used, ok;
    if (!d->w.mskp || !d->rig.ok) return;
    sys->memp = mem;   /* bhGetFreeMemory's limit (freemem + 12.25 MB + PC_ARENA_EXTRA) at its end */
    freemem = (unsigned char*)((uintptr_t)mem + FACE_MEM - 12845056 - PC_ARENA_EXTRA);
    bhInitMask(&d->w);   /* (copies the face mesh, inserts the jaw/tongue/eye pivots: a new object array) */
    fm = (MASK_WORK*)d->w.exp1;   /* (bhGetFreeMemory: NULL when face_mem is full) */
    ok = fm && d->w.exp0 && d->w.exp2 && fm->head;
    if (ok) {
        NJS_CNK_MODEL *src = face_src(fm->src, fm->vofs), *tang = fm->tangsrc ? face_src(fm->tangsrc, fm->vofs) : NULL;   /* the morph reads its rest vertices 16 bytes at a time (lq) */
        ok = src && (tang || !fm->tangsrc);
        if (ok) { fm->src = src; fm->tangsrc = tang; }
    }
    used = sys->memp - mem;
    sys->memp = memp;
    freemem = arena;
    /* bhControlMask writes the jaw, tongue and eye angles to nodes 7, 11, 9, 13 of the new array (the
       cast's layout); the pivots it inserted have the same model as these nodes and no child */
    ok = ok && used <= FACE_MEM && m->obj_num <= NODES && fm->toothsrc && fm->tangorg && fm->eyesrc[0] && fm->eyesrc[1] &&
         m->objP[7].model == fm->toothsrc->model && m->objP[7].child && m->objP[11].model == fm->tangorg->model && m->objP[11].child &&
         m->objP[9].model == fm->eyesrc[0]->model && m->objP[9].child && m->objP[13].model == fm->eyesrc[1]->model && m->objP[13].child &&
         rig_build(&d->rig, m->objP, m->obj_num, 0);
    host_log("actor: face mask %s (%d nodes, %d faces, %d KB)\n", ok ? "set up" : "does not fit, left out", m->obj_num, fm && fm->head ? (int)fm->head->nFace : 0, used >> 10);
    if (ok) {
        d->face = (int)fm->head->nFace;
        d->w.mdflg |= 0x100;   /* npCalcSkinFM: the skin over the array with the pivots, as the cast's */
        /* The mask's own face table holds the mouth shapes (banks of 6 phonemes, bhLipSet), not key
           frames: played as the face (fmCnkSetCurrentFrame) its last shape was taken, a spike out of
           the head. The face at rest is its first: phoneme 0 of bank 0, the closed mouth. */
        fm->fnum = 1;
        ((int*)d->w.exp0)[8] = 1;   /* (what bhControlMask resets the face to) */
    }
    else if (load(d, idx)) d->model = idx;   /* (the objects were changed: again without it; load leaves model -1) */
    else d->fail = idx;
}

/* bank b of our face table (6 mouth shapes, bhLipSet) cannot move the mouth: no jaw angle in any of its faces and the
   same lip muscles (those fmSetLipSyncParam sets, 12..25) in all six. Bank 1 of Wesker's and Alexia's tables is all
   zero, Rodrigo's six times one face: many lines of others use it (Alfred's in 1020), and on those models the mouth
   stayed still */
static int bank_still(const Disguise* d, int b)
{
    const PARAM_WORK* t = (const PARAM_WORK*)((const int*)d->w.exp0)[267] + b * 6;
    for (int f = 0; f < 6; f++) {
        if (*(const int*)&t[f].jawang) return 0;   /* (an angle, as fmCnkSetParamLip reads it) */
        for (int k = 12; k < 26; k++) if (t[f].muscle[k] != t[0].muscle[k]) return 0;
    }
    return 1;
}

/* the double's lip sync on our face (coop_control_remote, after the enemies' bhControlMask) */
static void face_step(Disguise* d, const BH_PWORK* ep)
{
    int* e = (int*)d->w.exp0;
    const int* s = (const int*)ep->exp0;
    if (!ep->mskp || !s || !ep->exp2) return;
    e[0] = (e[0] & ~0x4) | (s[0] & 0x4);    /* lip sync running */
    e[4] = s[4];                            /* the phoneme bank (bhLipSet): 6 faces each */
    e[268] = s[268];                        /* the lip data (bhSetLip) */
    *(LIP_WORK*)d->w.exp2 = *(const LIP_WORK*)ep->exp2;
    if (e[268] && e[268] != d->lip) {   /* a new line: its highest phoneme (pairs phoneme, time; 0xFF ends) */
        const unsigned char* p = (const unsigned char*)e[268];
        int k;
        d->lip = e[268];
        for (d->lip_max = k = 0; k < 4096 && p[k] != 0xFF; k += 2) if (p[k] > d->lip_max) d->lip_max = p[k];
    }
    if (e[4] < 0 || e[4] * 6 + d->lip_max >= d->face || bank_still(d, e[4])) e[4] = 0;   /* a bank past our face table or still: our first */
    if (d->lip_max >= d->face || bank_still(d, 0)) { e[0] &= ~0x4; ((LIP_WORK*)d->w.exp2)->flag = 0; }   /* (not even that one: no mouth shapes) */
    bhControlMask(&d->w);
    if (ep->mlwP && ep->mlwP->obj_num > 13 && d->w.mlwP->obj_num > 13) {   /* the eyes look where the double's do (its expression's eye
        angles, which bhControlMask puts on the pivots 9 and 13 of every cast mask; the muscles are its own face's) */
        int k;
        for (k = 0; k < 2; k++) {
            d->w.mlwP->objP[9].ang[k] = ep->mlwP->objP[9].ang[k];
            d->w.mlwP->objP[13].ang[k] = ep->mlwP->objP[13].ang[k];
        }
    }
}

int actor_talks(int who)
{
    const Disguise* d = &dg[who];
    return d->model >= 0 && d->rig.ok && ((d->face > 5 && !bank_still(d, 0)) || (d->skin && d->fbone[4] >= 0));
}

/* ep is a shown cutscene double of the character played */
static int double_acting(const BH_PWORK* ep, int pid)
{
    int j;
    if (!(ep->flg & 0x1) || (ep->stflg & 0x41000000) || !ep->id || !ep->mlwP) return 0;
    for (j = 0; j < 3 && doubles[pid][j] != ep->id; j++);
    return j < 3;
}

/* the model ep is drawn as while it is a cutscene double of the character played, -1 itself (the voice lines,
   cast_glue.c pc_voice_on: the double may be hidden while it talks) */
int actor_double_shown(const BH_PWORK* ep)
{
    int j, pid = sys->ply_id & 3;
    if (dg[ACTOR_DOUBLE].model < 0 || !dg[ACTOR_DOUBLE].rig.ok || !(ep->flg & 0x1) || !ep->id) return -1;
    for (j = 0; j < 3 && doubles[pid][j] != ep->id; j++);
    return j < 3 ? dg[ACTOR_DOUBLE].model : -1;
}

/* a cutscene: a double of the character played is shown (whether as our model or as itself) */
int actor_double_acting(void)
{
    int i;
    for (i = 0; i < (int)sys->ewk_n && i < PC_ENE_SLOTS; i++) if (double_acting(&ene[i], sys->ply_id & 3)) return 1;
    return 0;
}

/* coop_control_remote, every tick after the enemies moved and before the objects follow their links:
   the double of the character played posed as our model, its face and hands with it */
void actor_double_tick(void)
{
    Disguise* d = &dg[ACTOR_DOUBLE];
    int i, pid = sys->ply_id & 3;
    memset(posed_src, 0, sizeof(posed_src));
    cast_show();   /* (the pieces and head parts hidden last tick) */
    pc_voice_tick();
    if (cast_n) cast_tick();   /* the cutscene cast, the same way */
    if (d->model < 0 || !d->rig.ok) return;
    for (i = 0; i < (int)sys->ewk_n && i < PC_ENE_SLOTS; i++) {
        BH_PWORK* ep = &ene[i];
        if (!double_acting(ep, pid)) continue;
        if (d->face) face_step(d, ep);
        dbl_tick = dbl_posed ? 2 : 1;
        actor_pose(ACTOR_DOUBLE, ep);
        dbl_tick = 0;
        if (d->mapped) { dbl_posed = ep; cast_double_parts(ep, pid); }   /* (its head parts: Claire's ponytail) */
    }
}

/* the cutscene cast (cast_glue.c): ep posed as slot who's model as the double is (its face, its hands) */
void actor_act(int who, BH_PWORK* ep)
{
    Disguise* d = &dg[who];
    if (who < ACTOR_CAST || who >= ACTOR_SLOTS || d->model < 0 || !d->rig.ok) return;
    if (d->face) face_step(d, ep);
    if (d->face && ep->mskp && ep->exp1 && d->w.exp1 && getenv("RECVX_CAST_TRACE") && !(actor_ticks % 10))   /* test aid: the faces (jaw angles) */
    {
        const PARAM_WORK *a = &((const MASK_WORK*)ep->exp1)->param, *b = &((const MASK_WORK*)d->w.exp1)->param;
        float ma = 0, mb = 0;
        for (int k = 0; k < 32; k++) { ma += fabsf(a->muscle[k]); mb += fabsf(b->muscle[k]); }
        host_log("cast: tick %u slot %d: source type %d lips %d muscles %.2f jaw %.2f, model lips %d muscles %.2f jaw %.2f\n", actor_ticks, who - ACTOR_CAST, ep->id,
                 (((const int*)ep->exp0)[0] & 4) != 0, ma, a->jawang, (((const int*)d->w.exp0)[0] & 4) != 0, mb, b->jawang);
    }
    dbl_tick = posed_src[who] ? 2 : 1;
    actor_pose(who, ep);
    dbl_tick = 0;
    posed_src[who] = d->mapped ? ep : NULL;
    posed_t[who] = actor_ticks;
}

int actor_put_as(int who, BH_PWORK* ep)
{
    Disguise* d = &dg[who];
    if (who < ACTOR_CAST || who >= ACTOR_SLOTS || d->model < 0) return 0;
    if (ep != posed_src[who] || posed_t[who] != actor_ticks) {   /* (posed from this one this tick: the same pose, its hands already moved) */
        actor_pose(who, ep);
        posed_src[who] = d->mapped ? ep : NULL;
        posed_t[who] = actor_ticks;
    }
    if (!d->mapped) return 0;   /* a source without a humanoid node map is drawn as itself */
    bhPutModel(&d->w);
    head_draw(d);
    cast_hands_draw(who, ep);
    return 1;
}

/* at the hand-off: the model for the cutscene doubles of the character played (appearance: a
   catalogue index, ACTOR_PART + part file - 10, or -1); loaded only while the room has one.
   "Swapped model in cutscenes" off: none, the game draws its doubles (hands, face, all their own) */
static int dbl_kept;
int actor_double_kept(void) { return dbl_kept; }

/* the model character pid's doubles are drawn as for look idx (as actor_doubles takes it), -1 the game's own; *kept: its
   own cutscene model, so the game's (its face, its hands, its head part) */
int actor_double_model(int idx, int pid, int* kept)
{
    static const signed char twin[5] = { 0, 1, 3, 5, -1 };   /* the player files' cutscene models (a face mask): Claire, Chris, Steve, Wesker */
    if (!g_appear_cutscene) idx = -1;
    if (g_appear_face && idx >= ACTOR_PART && idx < ACTOR_PART + 5 && twin[idx - ACTOR_PART] >= 0) idx = twin[idx - ACTOR_PART];
    *kept = idx >= 0 && idx < ACTOR_MODELS && actor_models[idx].id == doubles[pid & 3][0];
    return *kept ? -1 : idx;
}

void actor_doubles(int idx)
{
    int i, j, pid = sys->ply_id & 3, any = 0;
    idx = actor_double_model(idx, pid, &dbl_kept);
    if (idx >= 0)
        for (i = 0; i < (int)sys->ewk_n && i < PC_ENE_SLOTS && !any; i++)
            for (j = 0; j < 3; j++) any |= (ene[i].flg & 0x1) && doubles[pid][j] && ene[i].id == doubles[pid][j];
    if (actor_set(ACTOR_DOUBLE, any ? idx : -1)) actor_cast_hands(ACTOR_DOUBLE);   /* (its hands where the model has none: cast_glue.c hands_hide) */
}

/* njReleaseTextureAll took ours too */
void actor_textures_released(void)
{
    int i;
    for (i = 0; i < ACTOR_SLOTS; i++) {
        dg[i].tex_gone = 1;
        unload(&dg[i]);
        dg[i].fail = -1;
    }
    cast_mem_reset();
    actor_on = 0;
    placer_textures_released();
}

__attribute__((constructor)) static void actor_init(void)
{
    int i;
    for (i = 0; i < ACTOR_SLOTS; i++) dg[i].model = dg[i].fail = -1;
    for (i = 0; i < ACTOR_CAST; i++) { dg[i].keep = keep_big[i]; dg[i].keep_cap = KEEP_SIZE; }
    for (i = 0; i < 2; i++) sh[i].chr = sh[i].fail = sh[i].wpn_no = sh[i].motion = -1;
    if (getenv("RECVX_RETARGET")) actor_retarget = atoi(getenv("RECVX_RETARGET")) != 0;
    if (getenv("RECVX_CAST_HANDS")) cast_hands = atoi(getenv("RECVX_CAST_HANDS")) != 0;
    if (getenv("RECVX_CAST_PARTS")) cast_parts = atoi(getenv("RECVX_CAST_PARTS")) != 0;
}

/* the display actor's work (its model, node matrices after actor_pose), NULL when none is shown */
BH_PWORK* actor_work(int who) { return who >= 0 && who < ACTOR_SLOTS && dg[who].model >= 0 ? &dg[who].w : NULL; }

/* node n of w's model has a mesh of its own: geometry, or skinned vertices (npSkinConvert's records
   in node order: flg, mesh mno, with flg 2 the mesh's vertex table; flg 1: vertices bound to the node) */
static int node_has_mesh(const BH_PWORK* w, int n)
{
    const ML_WORK* m = w->mlwP;
    const int* s = w->skp[m - w->mdl];
    int i, flg = 0;
    if (m->objP[n].model) return 1;
    for (i = 0; s && i <= n; i++) {
        if (m->objP[i].evalflags & 0x20000000) { flg = 0; continue; }
        if ((flg = *s++)) { s++; if (flg & 2) s += 1 + *s; }
    }
    return flg & 1;
}

/* the source's hand node (9, 13) is at the model's hand (actor_pose) and the model has no hand
   there of its own (the story cast's models; zombies and creatures have theirs) */
/* the cutscene cast (cast_glue.c): slot who's last pose put its model's hand `side` (0 right, 1 left) on source node
   *node, and the model has a hand of its own there (a mesh on its hand node, skinned vertices there, or an imported
   model's hands): 1 */
int actor_own_hand(int who, int side, int* node)
{
    Disguise* d = &dg[who];
    int n = d->model >= 0 && d->mapped ? d->rig.map[side ? 13 : 9] : -1;
    *node = d->src_hand[side & 1];
    if (*node < 0 || d->model < 0 || !d->mapped) return 0;
    if (n < 0) return 2;   /* (no such hand at all: the Bandersnatch's left, Nosferatu's right) */
    return d->skin || node_has_mesh(&d->w, n);
}

/* A cast slot's (and the double's) own hands. The cast's cutscene models and the player files have none: the room
   script links the record's hand pieces to its hands, the record's own character's (Wesker's big gloved hands on the
   model in his place). A creature cast as a character (cast_glue.c cast_room) has no pieces at all (Nosferatu in his
   straitjacket, 6040, has no arms; the Tyrant, the Hunter, Steve's father have theirs in their mesh). Such a slot gets
   the unarmed pieces of the character the model is (appear_voice: the cast its own, other men Chris's, women
   Claire's; weapon file c * 30 + 20, as the lobby preview and the game unarmed), linked to its hand nodes; drawn on
   each hand of the model without a mesh of its own while no piece of the script's is shown on that hand
   (cast_hands_draw): cast_glue.c hands_hide hides the record's one-node pieces where they are another character's.
   Loaded once per model (two textures: Steve's and Wesker's 17 KB each, Chris's 65 KB, Claire's 128 KB). */
int actor_cast_hands_chr(int who) { return who >= 0 && who < ACTOR_SLOTS && ch[who].on ? ch[who].chr : -1; }

void actor_cast_hands(int who)
{
    Disguise* d = &dg[who];
    unsigned char* p = pack;
    const unsigned char* end = pack + sizeof(pack);
    int side, c, n, need = 0;
    if ((who < ACTOR_CAST && who != ACTOR_DOUBLE) || who >= ACTOR_SLOTS || d->model < 0 || !d->rig.ok || d->skin || ch[who].tried) return;
    ch[who].tried = 1;
    for (side = 0; side < 2; side++) need |= (n = d->rig.map[side ? 13 : 9]) >= 0 && !node_has_mesh(&d->w, n);
    if (!need) return;   /* (hands of its own: a zombie, a creature) */
    n = d->model - ACTOR_PART;
    c = d->model >= ACTOR_PART ? (n <= 3 ? n : appear_part_anim(n) == ANIM_FEMALE ? 0 : 1) : appear_voice(COOP_CHR_ACTOR + d->model);
    if (c < 0 || c > 3 || !read_file(sys->sys_partid, c * 30 + 20)) { host_log("actor: slot %d: no hands (character %d)\n", who, c); return; }
    for (side = 0; side < 2; side++) {   /* (the file as bhReadWeaponData reads it: per hand its model, then its texture block) */
        O_WRK* op = &ch[who].obj[side];
        ML_WORK* m = &op->mdl[0];
        unsigned int msz = (unsigned)(end - p) >= 8 ? rd32(p) : 0, t;
        if (!msz || msz > HAND_MDL || msz > (unsigned)(end - p) - 8) return;
        memset(op, 0, sizeof(*op));
        memcpy(ch[who].mdl[side], p + 4, msz);
        p += 4 + msz;
        t = rd32(p);
        p += 4;
        if (t & 0x80000000) { p = pack + ((p - pack + 63) & ~63); t &= ~0x80000000u; }
        if (t > (unsigned)(end - p)) return;
        bhMlbBinRealize(ch[who].mdl[side], m);
        if (!m->objP || m->obj_num > 4 || !m->texP || !t || d->tex_n >= (int)(sizeof(d->tex) / sizeof(d->tex[0])) || !tex_room("hands", 1)) {
            host_log("actor: slot %d: hands of character %d not loaded\n", who, c);
            return;
        }
        bhSetMemPvpTexture(m->texP, p, 0);
        m->flg |= 0x200;
        d->tex[d->tex_n++] = m->texP;
        p += t;
        m->owP = ch[who].ow[side];
        memset(m->owP, 0, sizeof(ch[0].ow[0]));
        op->mlwP = m;
        op->flg = 0x81;   /* (as bhSetWeapon: linked) */
        op->id = 1210;
        op->mtx = (void*)op->mtxbuf;
        op->lkwkp = (unsigned char*)&d->w;
        op->lkono = d->rig.map[side ? 13 : 9];
    }
    ch[who].on = 1;
    ch[who].chr = c;
    host_log("actor: slot %d: the unarmed hands of character %d\n", who, c);
}

/* actor_put_as, actor_put_enemy: slot who's own hands where its model has none and record ep shows no piece on that hand */
static void cast_hands_draw(int who, const BH_PWORK* ep)
{
    Disguise* d = &dg[who];
    int side, o;
    if (!ch[who].on || !d->mapped) return;
    for (side = 0; side < 2; side++) {
        O_WRK* op = &ch[who].obj[side];
        int src = d->src_hand[side];
        if (op->lkono < 0 || node_has_mesh(&d->w, op->lkono)) continue;
        for (o = 0; o < 32 && src >= 0; o++) {
            const O_WRK* w = &sys->obwp[o];
            if ((w->flg & 0x81) == 0x81 && w->lkwkp == (const unsigned char*)ep && w->lkono == src && w->mlwP && w->mlwP->objP && !(w->mlwP->objP[0].evalflags & 0x8)) break;
        }
        if (src >= 0 && o < 32) continue;   /* (the script's piece shown: it is the hand, a prop in it too; cast_glue.c) */
        bhCalcModel((BH_PWORK*)op);
        bhPutModel((BH_PWORK*)op);
    }
}

int actor_src_head(int who, int node)
{
    const Disguise* d = &dg[who];
    return node >= 0 && d->model >= 0 && d->mapped && (node == d->src_head[0] || node == d->src_head[1]);
}

int actor_hand_free(int who, int node)
{
    Disguise* d = &dg[who];
    int n = d->model >= 0 && d->mapped ? d->rig.map[node] : -1;
    return n >= 0 && !d->skin && !node_has_mesh(&d->w, n);   /* (an imported model has its hands in its skinned mesh) */
}

int actor_head_node(int who) { return dg[who].model >= 0 && dg[who].rig.ok ? dg[who].rig.map[5] : -1; }

int actor_head(int who, float* out)
{
    Disguise* d = &dg[who];
    ML_WORK* m = d->w.mlwP;
    int i, k = d->rig.ok ? d->rig.map[5] : -1;
    if (d->model < 0 || !d->posed || !m || !m->owP || m->obj_num <= 0) return 0;
    if (k < 0 || k >= (int)m->obj_num)   /* no head found: the node farthest above (y up) its position */
        for (i = k = 0; i < (int)m->obj_num; i++)
            if (m->owP[i].mtx[13] > m->owP[k].mtx[13]) k = i;
    memcpy(out, &m->owP[k].mtx[12], 3 * sizeof(float));
    return 1;
}
