/* Pure decisions of the PC hooks, kept apart so pc/tests/test_rules.c can check them without the game.
   Needs the decomp's types (types.h) before it. */
#pragma once
#include "../src/gs/native.h"
#include "../../decomp/include/ps2/veronica/prog/item.h"
#include <math.h>

/* ---------------------------------------------------------------- added enemies (decomp eneset.c) */
typedef void (*PcEnemyProc)(BH_PWORK*);

/* a part's or child's parent enemy: up to 3 links, each into the enemy array ene0[n] */
static inline BH_PWORK* pc_root_in(BH_PWORK* ep, BH_PWORK* ene0, int n)
{
    int k;
    for (k = 0; k < 3 && ep->lkwkp && (BH_PWORK*)ep->lkwkp >= ene0 && (BH_PWORK*)ep->lkwkp < ene0 + n; k++) ep = (BH_PWORK*)ep->lkwkp;
    return ep;
}

/* ep is the holder `by`'s family: its part or child, or the leader of by's type (an unlinked enemy of
   the same id the type made itself, owner: slot -> creator type + 1) */
static inline int pc_family_in(BH_PWORK* ep, BH_PWORK* by, BH_PWORK* ene0, int n, const unsigned char* owner)
{
    BH_PWORK* r = pc_root_in(by, ene0, n);
    return pc_root_in(ep, ene0, n) == r || (!ep->lkwkp && ep >= ene0 && ep < ene0 + n && ep->id == r->id && owner[ep - ene0] == r->id + 1);
}

/* the function a step of enemy id runs in a room with added enemies: a part or child (ids 31..38; o:
   its creator type + 1, 0 none) made before the room got them (retail) the shared one as it was then
   (snap), another its creator type's own (helper: that type's functions), else f */
static inline PcEnemyProc pc_enemy_proc(PcEnemyProc f, int id, int o, int retail, const PcEnemyProc* snap, const PcEnemyProc* helper)
{
    if (id < 31 || id > 38 || !o) return f;
    if (retail) return snap[id - 31] ? snap[id - 31] : f;
    return helper[id - 31] ? helper[id - 31] : f;
}

/* ---------------------------------------------------------------- dual weapons (tps_glue.c) */
#define PC_DUAL_YAW (45.0f * (65536.0f / 360.0f))   /* (as tps_glue.c DEG) */
#define PC_DUAL_PITCH (15.0f * (65536.0f / 360.0f))

/* enemy i for the other arm, by its yaw dy off the crosshair direction (> 0: left) and its pitch dp
   off the aim's (65536 per turn): 0 not (the crosshair's own enemy aim_e, or outside the cone), 1 the
   right arm, 2 the left. Last tick's (prev) keeps it in a cone 15 % wider. */
static inline int pc_dual_arm(int i, int aim_e, int prev, float dy, float dp)
{
    float w = i == prev ? 1.15f : 1.0f;
    if (i == aim_e || fabsf(dy) > PC_DUAL_YAW * w || fabsf(dp) > PC_DUAL_PITCH * w) return 0;
    return dy > 0 ? 2 : 1;
}

/* ---------------------------------------------------------------- precise hits (tps_glue.c) */
/* the ray p + t * d (|d| = 1, t >= 0) against the capsule a-b of radius r (a == b: a sphere): the t where
   it enters (0 when p is inside), -1 if it misses; *mid the t on the ray nearest the capsule's axis (a
   point inside it when it hits). Exact: the capsule is a cylinder and two end spheres, entered at the
   first of the three. */
static inline float pc_ray_sphere(const float* p, const float* d, const float* c, float r)
{
    float o[3] = { p[0] - c[0], p[1] - c[1], p[2] - c[2] };
    float b = o[0] * d[0] + o[1] * d[1] + o[2] * d[2], q = o[0] * o[0] + o[1] * o[1] + o[2] * o[2] - r * r, h = b * b - q;
    if (q <= 0) return 0;
    return h < 0 || b > 0 ? -1 : -b - sqrtf(h);
}
static inline float pc_ray_capsule(const float* p, const float* d, const float* a, const float* b, float r, float* mid)
{
    float ba[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, oa[3] = { p[0] - a[0], p[1] - a[1], p[2] - a[2] };
    float bb = ba[0] * ba[0] + ba[1] * ba[1] + ba[2] * ba[2], bd = ba[0] * d[0] + ba[1] * d[1] + ba[2] * d[2];
    float bo = ba[0] * oa[0] + ba[1] * oa[1] + ba[2] * oa[2], od = oa[0] * d[0] + oa[1] * d[1] + oa[2] * d[2];
    float k = bb - bd * bd, u, t, ta, tb;   /* k: |ba|^2 sin^2 of the angle between ray and axis (0: a sphere or parallel) */
    /* the point a + u ba nearest the ray's line (u clamped to the segment; the distance is convex in u),
       then the ray's point nearest that */
    u = k > 1e-6f * bb ? (bo - bd * od) / k : 0;
    u = u < 0 ? 0 : u > 1 ? 1 : u;
    t = u * bd - od;
    *mid = t < 0 ? 0 : t;
    ta = pc_ray_sphere(p, d, a, r);
    tb = pc_ray_sphere(p, d, b, r);
    t = ta < 0 || (tb >= 0 && tb < ta) ? tb : ta;
    if (k > 1e-6f * bb) {   /* the cylinder: |oa + t d|^2 - ((oa + t d) . ba)^2 / bb = r^2 */
        float B = bb * od - bo * bd, C = bb * (oa[0] * oa[0] + oa[1] * oa[1] + oa[2] * oa[2]) - bo * bo - r * r * bb, h = B * B - k * C;
        if (C <= 0 && bo >= 0 && bo <= bb) return 0;   /* inside it */
        if (h >= 0) {
            float tc = (-B - sqrtf(h)) / k, y = bo + tc * bd;
            if (tc >= 0 && y > 0 && y < bb && (t < 0 || tc < t)) t = tc;
        }
    }
    return t;
}

/* dual weapons with precise hits: the second gun turns to an enemy only if its own ray, turned to it, meets
   that enemy's body (te: where, -1 not) before any other enemy's (tn, -1 none) and before a wall (tw, -1 none) */
static inline int pc_dual_take(float te, float tn, float tw)
{
    return te >= 0 && (tn < 0 || te <= tn) && (tw < 0 || te <= tw);
}

/* ---------------------------------------------------------------- music on a warp (cheat_glue.c) */
/* after the entry script of a room reached without a door, with the music that played before it
   (was, was_vol) stopped as a door does: the new music request. The same music asked for again plays
   on (as PlayBgm2; its volume eased if that changed), -1: none asked, the old one fades out (StopBgm
   400) as at a door; otherwise the request as it is (other music starts as usual). */
static inline int pc_warp_bgm(int req, int cur, int vol, int was, int was_vol, int playing)
{
    if ((req & 0x8) && cur == was) return vol != was_vol ? 0x2 : 0;
    if (cur < 0 && was >= 0 && playing) return -1;
    return req;
}

/* the same for an ambience slot (as CallBackGroundSe2; -1: StopBackGroundSeEx 400) */
static inline int pc_warp_bgse(int req, int cur, int was)
{
    if ((req & 0x2) && cur == was) return 0;
    if (cur < 0 && was >= 0) return -1;
    return req;
}

/* ---------------------------------------------------------------- "Unlock all" (cheat_glue.c) */
/* the part of a room's event block t (u32 offsets of scd0, scd1, the events; the first code offset
   ends the table) at offset ofs: 0 scd0, 1 scd1, 2 an event (they follow scd0 and scd1) */
static inline int pc_script_part(const unsigned int* t, unsigned int ofs)
{
    unsigned int first = t[0], lo = ~0u, i;
    for (i = 0; i * 4 < first; i++)
        if (t[i] && t[i] < first) first = t[i];
    for (i = 2; i * 4 < first; i++)
        if (t[i] && t[i] < lo) lo = t[i];
    if (ofs >= lo) return 2;
    return t[1] > t[0] ? ofs >= t[1] : ofs < t[0];
}

/* a PlItemCheck of a key item (key) in script part `part`, followed by op `next`, in room stg/rom with
   a use spot for the key (spot), passes for the cheat: in scd0/scd1 only (event tasks check items to
   give or take them), not when it guards a model change (36 InitModelSet: RM_3160 hides its zombies
   once the Emblem Card is carried), never in 0/9 (the metal detector checks every metal item there) */
static inline int pc_unlock_rule(int key, int part, int next, int stg, int rom, int spot)
{
    return key && part < 2 && next != 36 && !(stg == 0 && rom == 9) && spot;
}

/* ---------------------------------------------------------------- hor+ margins (native_glue.c) */
/* is a hidden room part drawn in the margins (view_glue.c pc_margin_part) left out? v: its n vertices
   in view space; kx * x + ox * z is the GS screen x times z, so each test is against a plane through
   the camera; zn: the near plane; pl: the player's view x, z.
   Mode 3 (its sphere reaches the camera or the line to the player): unless all vertices lie on one
   side of the 4:3 view, widened to take in the player (PC_PLAYER_HALF either side of its spot), or
   behind the near plane. Their hull then misses that view at every depth, so the part neither ends in
   a straight edge at the 4:3 border nor crosses the line to the player.
   Mode 2 (in front of the player): when a vertex lies in front of the camera inside the 4:3 screen;
   cut there, it would end in a straight edge at the 4:3 border. */
#define PC_PLAYER_HALF 8.0f   /* the player's collision radius (O_WRK ar) is 3.5 */
static inline int pc_margin_rule(int mode, const VU1_STRIP_BUF* v, int n, float kx, float ox, float zn, const float* pl)
{
    if (mode == 3) {
        float x0 = NATIVE_43_X0, x1 = NATIVE_43_X1, pz = pl[1];
        int left = 1, right = 1, behind = 1;
        if (pz > zn) {
            float xp = ox + kx * pl[0] / pz, pad = kx * PC_PLAYER_HALF / pz;
            if (xp - pad < x0) x0 = xp - pad;
            if (xp + pad > x1) x1 = xp + pad;
        }
        for (int i = 0; i < n; i++) {
            float x = ox * v[i].fVz + kx * v[i].fVx;
            if (x > x0 * v[i].fVz) left = 0;
            if (x < x1 * v[i].fVz) right = 0;
            if (v[i].fVz > zn) behind = 0;
        }
        return !(left || right || behind);
    }
    if (mode != 2) return 0;
    for (int i = 0; i < n; i++) {
        float x = ox * v[i].fVz + kx * v[i].fVx;   /* screen x times Vz */
        if (v[i].fVz > zn && x > NATIVE_43_X0 * v[i].fVz && x < NATIVE_43_X1 * v[i].fVz) return 1;
    }
    return 0;
}

/* A hidden part left out of the margins takes along the hidden parts below it in the room's tree
   whose bounding sphere lies inside its box (view space: x, y, z low, then high): what it holds,
   as the dolls on the shelves of a cabinet (2/5). A sphere is loose around its model, so it may
   reach out by a quarter of its radius: the ship in the niche of 1/5's corner wall stood in the
   margin without its niche. Room trees also group parts beside each other (1/0: a flower pot
   under a terrace part; 1/7: pillars along a wall), which stay. */
static inline void pc_margin_bounds(const VU1_STRIP_BUF* v, int n, float* box)
{
    box[0] = box[1] = box[2] = 1e30f;
    box[3] = box[4] = box[5] = -1e30f;
    for (int i = 0; i < n; i++) {
        const float p[3] = { v[i].fVx, v[i].fVy, v[i].fVz };
        for (int k = 0; k < 3; k++) {
            if (p[k] < box[k]) box[k] = p[k];
            if (p[k] > box[3 + k]) box[3 + k] = p[k];
        }
    }
}
static inline int pc_margin_inside(const float* c, float r, const float* box)
{
    r *= 0.75f;   /* a quarter of its radius may reach out */
    for (int k = 0; k < 3; k++)
        if (c[k] - r < box[k] || c[k] + r > box[3 + k]) return 0;
    return 1;
}

/* ---------------------------------------------------------------- inventory blocks (coop_glue.c: trades, item box) */
/* An inventory block as the game keeps it (sys->itm[ply_id * 16]): word 0 the equipped word's index (0 none,
   1 the lighter), word 1 the lighter's slot, words 2.. the grid (max 8 cells, 10 with the side pack). A word is
   id << 16 | count; the count can be 0 (the Combat Knife, an empty gun, Claire's Gold Lugers / sub machine guns
   as picked up): the item is there all the same. items: itemdata (type 0x100 two cells, 0x200 stackable). A
   two-slot item is one word over two cells; the game keeps them first (sub1.c ItemSort), one per row. */
static inline int pc_inv_two(const ITEM_WORK* items, unsigned int w) { return (items[(w >> 16) & 0xFF].type & 0x100) != 0; }

static inline int pc_inv_room(const unsigned int* b, int max, const ITEM_WORK* items)   /* free cells */
{
    int i, room = max;
    for (i = 2; i < 2 + max; i++) room -= (b[i] >> 16) & 0xFF ? (pc_inv_two(items, b[i]) ? 2 : 1) : 0;
    return room;
}

/* sub1.c ItemSort: two-slot items first, the rest in order; the equipped index follows its word */
static inline void pc_inv_sort(unsigned int* b, const ITEM_WORK* items)
{
    unsigned int out[10];
    int n = 0, eq = -1, i, pass;
    for (pass = 0; pass < 2; pass++)
        for (i = 2; i < 12; i++)
            if ((b[i] >> 16) & 0xFF && pc_inv_two(items, b[i]) == !pass) {
                if ((int)b[0] == i) eq = 2 + n;
                out[n++] = b[i];
            }
    for (i = 0; i < 10; i++) b[2 + i] = i < n ? out[i] : 0;
    if (b[0] >= 2) b[0] = eq >= 0 ? eq : 0;
}

/* an inventory word (id << 16 | count, flags kept) in as a pickup puts it: onto stacks of the same
   stackable item (up to 999), the rest into the first free word (a two-slot item needs two free
   cells, as GetItem asks, and goes to the front as ItemSort puts it); all or none. A word that does not
   go onto stacks whole - also one with count 0 - takes a free word of its own (else nothing changes, 0). */
static inline int pc_inv_add(unsigned int* b, int max, unsigned int entry, const ITEM_WORK* items)
{
    int id = (entry >> 16) & 0xFF, left = entry & 0xFFFF, fr = -1, space = 0, own, i;
    if (id <= 0 || id >= 156) return 0;
    for (i = 2; i < 2 + max; i++) {
        if (!((b[i] >> 16) & 0xFF) && fr < 0) fr = i;
        if ((items[id].type & 0x200) && b[i] >> 16 == entry >> 16) space += 999 - (int)(b[i] & 0xFFFF);
    }
    own = !left || left > space;
    if (own && (pc_inv_room(b, max, items) < (pc_inv_two(items, entry) ? 2 : 1) || fr < 0)) return 0;
    for (i = 2; i < 2 + max && left && (items[id].type & 0x200); i++)
        if (b[i] >> 16 == entry >> 16) {
            int c = b[i] & 0xFFFF, put = 999 - c < left ? 999 - c : left;
            b[i] += put;
            left -= put;
        }
    if (own) b[fr] = (entry & 0xFFFF0000) | left;
    if (own && pc_inv_two(items, entry)) pc_inv_sort(b, items);
    return 1;
}

/* `count` (0: all) of the item in word s out; returns its word, 0 if not allowed (empty, equipped,
   a two-slot item unless `two`) */
static inline unsigned int pc_inv_take(unsigned int* b, int max, int s, int count, int two, const ITEM_WORK* items)
{
    unsigned int e;
    int id, have, i;
    if (s < 2 || s >= 2 + max) return 0;
    e = b[s];
    id = (e >> 16) & 0xFF;
    have = e & 0xFFFF;
    if (!id || id >= 156 || s == (int)b[0] || (pc_inv_two(items, e) && !two)) return 0;
    if ((items[id].type & 0x200) && count > 0 && count < have) {
        b[s] = (e & 0xFFFF0000) | (have - count);
        return (e & 0xFFFF0000) | count;
    }
    for (i = s; i < 1 + max; i++) b[i] = b[i + 1];   /* the words stay packed, as bhPlItemLost leaves them */
    b[1 + max] = 0;
    if (b[0] > (unsigned int)s) b[0]--;
    return e;
}

/* sub1.c ItemBoxChange / GetItem: a special-slot item (`standard`: the lighter, Claire's lockpick too) goes into word 1
   only while that is empty. Claire can hold both (the lockpick from a pickup or the randomizer lies in a cell, an
   event puts it into the slot); the game wrote it over the item there, which was lost. Else it takes a cell like
   any other item. */
static inline int pc_to_special(const unsigned int* b, int id, int std0, int std1)
{
    return (id == std0 || id == std1) && !((b[1] >> 16) & 0xFF);
}

/* ---------------------------------------------------------------- BioRand (biorand_glue.c, coop_glue.c; decomp hitchk.c) */
/* the item a floor pickup gives: a generated room's item record names it in `type` (its `id` stays the model);
   retail leaves type 0 (docs/modding/ROOM_FORMAT.md "Enemies, objects, items") */
static inline int pc_pickup_item(int type, int id) { return type > 0 && type < 156 ? type : id; }
