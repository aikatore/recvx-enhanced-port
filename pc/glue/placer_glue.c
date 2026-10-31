/* --dev object placer, game thread (docs/DEV_PLACEMENTS.md; list and gizmo input: pc/src/host/overlay.cpp).
 *
 * Each catalogue model (pc/src/host/placer.c) is read once from its room file (actor_room_file) into
 * a buffer of our own inside the address window and kept for the session; its textures stay loaded
 * until njReleaseTextureAll (then everything is read again when next needed):
 *  - an object or item record: its model chunk (MLB, an object's optionally behind its SKIN data),
 *    realized as room.c bhSetObjMdl / bhSetItmMdl do, with its texture list uploaded whole;
 *  - room model nodes (the item box's body, the typewriter): the nodes with their subtrees copied
 *    out of the room model (objects, models, vertex and polygon lists), each root put where the room
 *    has it, the whole moved so the origin is the bottom centre of its extent; of the room's texture
 *    list only the pictures it uses are uploaded (its polygon lists renumbered to them).
 * The entries of the room shown are drawn after the room's own objects and items (game.c
 * bhAllDrawModel) with the items' lighting; nothing else of the game sees them. The selected entry's
 * gizmo (axes and rotation rings) is projected with the frame's camera for the overlay.
 */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/system.h"
#include "../../decomp/include/ps2/veronica/prog/binfunc.h"
#include "../../decomp/include/ps2/veronica/prog/njplus.h"
#include "../../decomp/include/ps2/veronica/prog/MdlPut.h"
#include "../../decomp/include/ps2/veronica/prog/pwksub.h"
#include "../../decomp/include/ps2/veronica/prog/hitchk.h"
#include "../../decomp/include/ps2/veronica/prog/light.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_texture.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_NaTextureFunction.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_NaMatrix.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_NinjaCnk.h"
#include "../src/host/placer.h"
#include "../src/host/bindings.h"
#include "../include/pc_parse.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void host_log(const char* fmt, ...);
const unsigned char* actor_room_file(int rm, int* len, unsigned char** scratch, int* cap);   /* actor_glue.c */
void actor_tex_free(int* entries, int* heap);

#define KEEP (160 << 10)                /* all models with their work (the eight: 109 KB); inside the address window */
#define NODES 24
#define TEXN 16
#define TEX_MARGIN 32                   /* texture entries and heap left for the rooms to come (as actor_glue.c) */
#define HEAP_MARGIN (2 << 20)

typedef struct Part {
    O_WORK ow[NODES];                   /* (16-byte aligned: the part is carved at 64) */
    O_WRK w;
    NJS_TEXLIST tl;                     /* room model nodes: the room's pictures they use */
    NJS_TEXNAME tn[TEXN];
} Part;
static Part* part[PL_TYPES][2];         /* [1]: drawn with [0], linked to its node 0 (the item box's lid) */
static unsigned char keep[KEEP] __attribute__((aligned(64)));
static int keep_used;

static unsigned int rd32(const unsigned char* p) { unsigned int v; memcpy(&v, p, 4); return v; }
static unsigned short rd16(const unsigned char* p) { unsigned short v; memcpy(&v, p, 2); return v; }
static float rdf(const unsigned char* p) { float v; memcpy(&v, p, 4); return v; }

/* n bytes of keep at an address that is `mod` past a multiple of 64 (data keeps its alignment) */
static unsigned char* keep_take(int n, int mod)
{
    int at = ((keep_used + 63) & ~63) + mod;
    if (at + n > KEEP) return NULL;
    keep_used = at + n;
    return keep + at;
}

/* ---------------------------------------------------------------- the source room file */
static const unsigned char* rf;         /* expanded, rlen bytes (actor_glue.c's buffer) */
static int rlen;
static unsigned char* scratch;
static int scratch_cap;
static const char* why;                 /* the reason a load failed */

static int ok_range(unsigned int off, unsigned int len) { return pc_range_ok(off, len, (unsigned int)rlen); }

/* the data from `off` to the next offset any table of the file names (a model chunk's length) */
static int chunk_len(unsigned int off)
{
    const RDT_WORK* h = (const RDT_WORK*)rf;
    const ROM_WORK* rw = (const ROM_WORK*)(rf + h->hed00);
    unsigned int best = (unsigned int)rlen, i, n, v;
    unsigned int heads[4] = { h->hed01, h->hed02, h->hed03, h->hed04 };
    for (i = 0; i < 4; i++) if (heads[i] > off && heads[i] < best) best = heads[i];
    n = 1 + (unsigned int)rw->ene_n + (unsigned int)(rw->obj_n > 4 ? rw->obj_n - 4 : 0) + (unsigned int)rw->itm_n;   /* hed01 */
    for (i = 0; i < n && ok_range(h->hed01 + 4 * i, 4); i++) if ((v = rd32(rf + h->hed01 + 4 * i)) > off && v < best) best = v;
    for (i = 0; i < (unsigned int)rw->ene_n + 3 && ok_range(h->hed02 + 4 * i, 4); i++)   /* hed02: motions, mouth and lip data */
        if ((v = rd32(rf + h->hed02 + 4 * i)) > off && v < best) best = v;
    if (ok_range(h->hed04, 4))
        for (i = 0, n = rd32(rf + h->hed04); i < n && ok_range(h->hed04 + 4 + 4 * i, 4); i++)
            if ((v = rd32(rf + h->hed04 + 4 + 4 * i)) > off && v < best) best = v;
    return (int)(best - off);
}

/* an object chunk's MLB (bhSetObjMdl: "MDL", or u32 skin size, u32, SKIN data, MLB) */
static const unsigned char* obj_mlb(const unsigned char* c) { return memcmp(c, "MDL", 3) ? c + rd32(c) + 8 : c; }
static int mlb_textured(const unsigned char* m) { return (int)rd32(m + 8) != -1; }

/* ---------------------------------------------------------------- textures */
static int tex_room(int texn)
{
    int entries, heap;
    actor_tex_free(&entries, &heap);
    if (entries >= texn + TEX_MARGIN && heap >= HEAP_MARGIN) return 1;
    why = "not enough texture memory left";
    return 0;
}

/* the TIM2 chunks of a texture list, one picture each (the lists of these sources have no PLI) */
static const unsigned char* tim_at(unsigned int list, int k)
{
    unsigned int p = list;
    for (;;) {
        if (!ok_range(p, 32) || memcmp(rf + p, "TIM2", 4)) return NULL;
        if (!k--) return ok_range(p + 32, rd32(rf + p + 4)) ? rf + p : NULL;
        p += 32 + rd32(rf + p + 4);
    }
}

/* ---------------------------------------------------------------- room model nodes */
#define RAW(i) (objs + (i) * 0x34)      /* NJS_CNK_OBJECT on the disc: offsets, -1 none */
static const unsigned char* objs;
static int nobj, obj_off;
static int raw_idx(int off) { return (off - obj_off) / 0x34; }
static int subtree(int i)
{
    int n = 1, c = (int)rd32(RAW(i) + 44);
    while (c != -1) { int k = raw_idx(c); n += subtree(k); c = (int)rd32(RAW(k) + 48); }
    return n;
}

/* chunk list lengths: vertex lists end with a 4-byte 255, polygon lists with a 2-byte one */
static int vlist_len(const unsigned char* p, const unsigned char* end)
{
    const unsigned char* q = p;
    while (q + 4 <= end) {
        if (q[0] == 255) return (int)(q + 4 - p);
        q += 4 + 4 * rd16(q + 2);
    }
    return -1;
}
static int plist_len(const unsigned char* p, const unsigned char* end, int* mapn, unsigned short* ids)
{
    const unsigned char* q = p;
    while (q + 2 <= end) {
        int t = q[0];
        if (t == 255) return (int)(q + 2 - p);
        if (t < 8) q += 2;
        else if (t < 16) {   /* texture id */
            int id = rd16(q + 2) & 0x1FFF, k;
            for (k = 0; k < *mapn && ids[k] != id; k++);
            if (k == *mapn) { if (k == TEXN) return -1; ids[(*mapn)++] = (unsigned short)id; }
            q += 4;
        } else q += 4 + 2 * rd16(q + 2);
    }
    return -1;
}
static void plist_renumber(unsigned char* p, const unsigned short* ids, int idn)
{
    for (;;) {
        int t = p[0];
        if (t == 255) return;
        if (t < 8) p += 2;
        else if (t < 16) {
            unsigned short v = rd16(p + 2);
            int k;
            for (k = 0; k < idn && ids[k] != (v & 0x1FFF); k++);
            v = (unsigned short)((v & ~0x1FFF) | k);
            memcpy(p + 2, &v, 2);
            p += 4;
        } else p += 4 + 2 * rd16(p + 2);
    }
}

/* the nodes d->rec[] of the room model (mlb), with their subtrees, as part p's model; *org: the first
   root's place in the room */
static int build_piece(const PlacerDef* d, const unsigned char* mlb, unsigned int texlist, Part* p, float* org)
{
    static short par[1024];
    short list[NODES];
    unsigned short ids[TEXN];
    int n = 0, idn = 0, r, i, j, roots = 0;
    const unsigned char* base = mlb + rd16(mlb + 4);
    const unsigned char* end = rf + rlen;
    NJS_CNK_OBJECT* out;
    unsigned char* tex;

    nobj = rd16(mlb + 6);
    obj_off = (int)rd32(mlb + 12);
    objs = base + obj_off;
    if (obj_off < 0 || nobj > 1024 || objs + nobj * 0x34 > end) { why = "bad room model"; return 0; }
    for (i = 0; i < nobj; i++) par[i] = -1;
    for (i = 0; i < nobj; i++)
        for (j = (int)rd32(RAW(i) + 44); j != -1; j = (int)rd32(RAW(raw_idx(j)) + 48)) par[raw_idx(j)] = (short)i;
    for (r = 0; r < 8 && d->rec[r] >= 0; r++) {
        int k = d->rec[r], s = k < nobj ? subtree(k) : NODES + 1;
        if (n + s > NODES) { why = "too many nodes"; return 0; }
        for (j = 0; j < s; j++) list[n++] = (short)(k + j);
        roots++;
    }
    out = (NJS_CNK_OBJECT*)keep_take(n * (int)sizeof(NJS_CNK_OBJECT), 0);
    if (!out) { why = "out of model memory"; return 0; }
    for (i = 0, r = 0; i < n; i++) {
        const unsigned char* o = RAW(list[i]);
        int moff = (int)rd32(o + 4), c = (int)rd32(o + 44), sb = (int)rd32(o + 48), root = 0;
        NJS_CNK_OBJECT* q = &out[i];
        for (j = 0; j < roots; j++) root |= list[i] == d->rec[j];
        q->evalflags = rd32(o);
        for (j = 0; j < 3; j++) { q->pos[j] = rdf(o + 8 + 4 * j); q->ang[j] = (int)rd32(o + 20 + 4 * j); q->scl[j] = rdf(o + 32 + 4 * j); }
        q->child = NULL;
        q->sibling = NULL;
        for (j = 0; j < n; j++) {
            if (c != -1 && list[j] == raw_idx(c)) q->child = &out[j];
            if (!root && sb != -1 && list[j] == raw_idx(sb)) q->sibling = &out[j];
        }
        if (root) {   /* where the room has it: its ancestors' places added (no ancestor may turn) */
            int a;
            for (a = par[list[i]]; a >= 0; a = par[a]) {
                if (rd32(RAW(a) + 20) || rd32(RAW(a) + 24) || rd32(RAW(a) + 28)) { why = "a turned parent node"; return 0; }
                for (j = 0; j < 3; j++) q->pos[j] += rdf(RAW(a) + 8 + 4 * j);
            }
            if (!r++) memcpy(org, q->pos, sizeof(q->pos));
            for (j = i + 1; j < n; j++)
                for (a = 0; a < roots; a++) if (list[j] == d->rec[a] && !q->sibling) q->sibling = &out[j];
        }
        q->model = NULL;
        if (moff != -1) {
            const unsigned char* m = base + moff;
            NJS_CNK_MODEL* nm;
            int vo, po, vl, pl;
            if (m + 24 > end) { why = "bad node"; return 0; }
            vo = (int)rd32(m);
            po = (int)rd32(m + 4);
            vl = vo == -1 ? 0 : vo < 0 ? -1 : vlist_len(base + vo, end);
            pl = po == -1 ? 0 : po < 0 ? -1 : plist_len(base + po, end, &idn, ids);
            if (vl < 0 || pl < 0) { why = "bad node data (or more than 16 pictures)"; return 0; }
            nm = (NJS_CNK_MODEL*)keep_take(24, 0);
            if (!nm) { why = "out of model memory"; return 0; }
            nm->vlist = NULL;
            nm->plist = NULL;
            nm->center.x = rdf(m + 8); nm->center.y = rdf(m + 12); nm->center.z = rdf(m + 16);
            nm->r = fabsf(rdf(m + 20));
            if (vl && (nm->vlist = (Sint32*)keep_take(vl, (int)((uintptr_t)(base + vo) & 63)))) memcpy(nm->vlist, base + vo, vl);
            if (pl && (nm->plist = (Sint16*)keep_take(pl, (int)((uintptr_t)(base + po) & 63)))) memcpy(nm->plist, base + po, pl);
            if ((vl && !nm->vlist) || (pl && !nm->plist)) { why = "out of model memory"; return 0; }
            if (pl) plist_renumber((unsigned char*)nm->plist, ids, idn);
            q->model = nm;
        }
    }
    /* the pictures it uses, in their new order, as a texture list of their own */
    for (i = 0, j = 0; i < idn; i++) {
        const unsigned char* t = tim_at(texlist, ids[i]);
        if (!t) { why = "a picture is not in the room's texture list"; return 0; }
        j += 32 + (int)rd32(t + 4);
    }
    if (j + 32 > scratch_cap) { why = "pictures too large"; return 0; }
    for (i = 0, tex = scratch; i < idn; i++) {
        const unsigned char* t = tim_at(texlist, ids[i]);
        memcpy(tex, t, 32 + rd32(t + 4));
        tex += 32 + rd32(t + 4);
    }
    memset(tex, 0, 32);
    memset(tex, 0xFF, 4);   /* end */
    p->w.mdl[0].flg = mlb[3];
    p->w.mdl[0].obj_num = (unsigned short)n;
    p->w.mdl[0].objP = out;
    npCnkFlatOff(out);   /* as bhSetRoom does for the room model */
    memset(p->tn, 0, sizeof(p->tn));
    p->tl.textures = p->tn;
    p->tl.nbTexture = (unsigned int)idn;
    p->w.mdl[0].texP = idn ? &p->tl : NULL;
    if (idn && !tex_room(idn)) return 0;
    if (idn) { bhSetMemPvpTexture(&p->tl, scratch, 0); p->w.mdl[0].flg |= 0x200; }
    return 1;
}

/* an object's or item's model chunk as part p's model, with its texture list (at file offset `list`, 0 none) */
static int build_record(unsigned int off, int is_obj, unsigned int list, Part* p)
{
    int len = chunk_len(off);
    unsigned char* c;
    const unsigned char* m;
    ML_WORK* ml = &p->w.mdl[0];
    if (len <= 16 || !ok_range(off, len)) { why = "bad model chunk"; return 0; }
    if (!(c = keep_take(len, (int)((uintptr_t)(rf + off) & 63)))) { why = "out of model memory"; return 0; }
    memcpy(c, rf + off, len);
    m = is_obj ? obj_mlb(c) : c;
    if (m < c || m + 16 > c + len) { why = "bad model chunk"; return 0; }
    bhMlbBinRealize((void*)m, ml);
    ml->owP = p->ow;   /* (the realizer clears it) */
    if (m != c) { p->w.skp[0] = (int*)(c + 8); npSkinConvert(ml->objP, p->w.skp[0]); }
    if (!ml->objP || ml->obj_num > NODES) { why = "too many nodes"; return 0; }
    if (ml->texP) {
        if (!list || !ok_range(list, 32)) { why = "no texture list"; return 0; }
        if (!tex_room((int)ml->texP->nbTexture)) return 0;
        bhSetMemPvpTexture(ml->texP, (unsigned char*)rf + list, 0);
        ml->flg |= 0x200;
    }
    return 1;
}

static void part_init(Part* p)
{
    memset(p, 0, sizeof(*p));
    p->w.mtx = (void*)p->w.mtxbuf;
    p->w.mlwP = &p->w.mdl[0];
    p->w.mdl[0].owP = p->ow;
    p->w.sx = p->w.sy = p->w.sz = p->w.sxb = p->w.syb = p->w.szb = 1.0f;
    p->w.clp_jno[1] = -1;
}

/* the extent of type t's parts at the origin, unturned (model space after bhCalcModel) */
static void extent(int t, float* lo, float* hi)
{
    int k, i, j, v;
    for (j = 0; j < 3; j++) { lo[j] = 1e9f; hi[j] = -1e9f; }
    for (k = 0; k < 2 && part[t][k]; k++) {
        ML_WORK* ml = &part[t][k]->w.mdl[0];
        if (!k) { part[t][0]->w.px = part[t][0]->w.py = part[t][0]->w.pz = 0; part[t][0]->w.ax = part[t][0]->w.ay = part[t][0]->w.az = 0; }
        bhCalcModel((BH_PWORK*)&part[t][k]->w);
        for (i = 0; i < (int)ml->obj_num; i++) {
            const NJS_CNK_MODEL* m = ml->objP[i].model;
            const unsigned char* q;
            if (!m || !m->vlist) continue;
            for (q = (const unsigned char*)m->vlist; q[0] != 255; q += 4 + 4 * rd16(q + 2)) {
                int first = q[0] == 51 ? 64 : q[0] == 41 ? 8 : 0, stride = q[0] == 51 ? 32 : 24, n = rd16(q + 6);
                if (!first) continue;
                for (v = 0; v < n; v++) {
                    NJS_POINT3 a, b;
                    memcpy(&a, q + first + stride * v, sizeof(a));
                    njCalcPoint((NJS_MATRIX*)&ml->owP[i].mtx, &a, &b);
                    for (j = 0; j < 3; j++) {
                        float c = j == 0 ? b.x : j == 1 ? b.y : b.z;
                        if (c < lo[j]) lo[j] = c;
                        if (c > hi[j]) hi[j] = c;
                    }
                }
            }
        }
    }
    if (lo[0] > hi[0]) for (j = 0; j < 3; j++) lo[j] = hi[j] = 0;
}

static int load_type(int t)
{
    const PlacerDef* d = &placer_defs[t];
    const RDT_WORK* h;
    const ROM_WORK* rw;
    const ETTY_WORK *ene, *obj, *itm;
    unsigned int mdltab, texn, ent[64];
#define LIST(k) (ent[k] < texn ? rd32(rf + h->hed04 + 4 + 4 * ent[k]) : 0u)
    int i, j, ne = 0, nobjs, e = 0, keep0 = keep_used, idx;
    float org[3] = { 0, 0, 0 };
    char name[16];

    why = "cannot read the room file";
    snprintf(name, sizeof(name), "RM_%04d", d->room);
    if (!(rf = actor_room_file(d->room, &rlen, &scratch, &scratch_cap))) goto fail;
    why = "bad room file";
    h = (const RDT_WORK*)rf;
    if (!ok_range(h->hed00, sizeof(ROM_WORK))) goto fail;
    rw = (const ROM_WORK*)(rf + h->hed00);
    if (!ok_range((unsigned int)rw->enep, (unsigned int)rw->ene_n * sizeof(ETTY_WORK)) ||
        !ok_range((unsigned int)rw->objp, (unsigned int)rw->obj_n * sizeof(ETTY_WORK)) ||
        !ok_range((unsigned int)rw->itmp, (unsigned int)rw->itm_n * sizeof(ETTY_WORK)) || rw->obj_n > 64 || !ok_range(h->hed04, 4))
        goto fail;
    ene = (const ETTY_WORK*)(rf + (unsigned int)rw->enep);
    obj = (const ETTY_WORK*)(rf + (unsigned int)rw->objp);
    itm = (const ETTY_WORK*)(rf + (unsigned int)rw->itmp);
    for (i = 0; i < rw->ene_n; i++) ne += ene[i].flg & 0x1;
    if (ne) { why = "the source room has enemies"; goto fail; }   /* (their texture lists would come before the objects') */
    nobjs = rw->obj_n > 4 ? rw->obj_n - 4 : 0;
    mdltab = h->hed01;
    texn = rd32(rf + h->hed04);
    if (!ok_range(mdltab, 4 * (1 + nobjs + rw->itm_n)) || !ok_range(h->hed04 + 4, 4 * texn)) goto fail;
    for (i = 0; i < 1 + nobjs + rw->itm_n; i++) if (!ok_range(rd32(rf + mdltab + 4 * i), 16)) goto fail;
    /* texture table entries (room.c bhSetRoom): the room model's, then per object id and per item id the first
       textured one; ent[k]: object k's (4..), ent[32 + k]: item k's */
    for (i = 0; i < 64; i++) ent[i] = (unsigned int)-1;
    e = mlb_textured(rf + rd32(rf + mdltab));
    for (i = 4; i < rw->obj_n; i++)
        if (mlb_textured(obj_mlb(rf + rd32(rf + mdltab + 4 * (1 + i - 4))))) {
            for (j = 4; j < i && !(obj[j].id == obj[i].id && ent[j] != (unsigned int)-1); j++);
            ent[i] = j < i ? ent[j] : (unsigned int)e++;
        }
    for (i = 0; i < rw->itm_n && i < 32; i++)
        if (mlb_textured(rf + rd32(rf + mdltab + 4 * (1 + nobjs + i)))) {
            for (j = 0; j < i && !(itm[j].id == itm[i].id && ent[32 + j] != (unsigned int)-1); j++);
            ent[32 + i] = j < i ? ent[32 + j] : (unsigned int)e++;
        }

    for (i = 0; i < (d->lid >= 0 ? 2 : 1); i++) {
        if (!(part[t][i] = (Part*)keep_take(sizeof(Part), 0))) { why = "out of model memory"; goto fail; }
        part_init(part[t][i]);
    }
    why = "bad record";
    idx = d->rec[0];
    if (d->kind == PLK_PIECE) {
        if (!mlb_textured(rf + rd32(rf + mdltab)) || !build_piece(d, rf + rd32(rf + mdltab), rd32(rf + h->hed04 + 4), part[t][0], org)) goto fail;
    } else if (d->kind == PLK_OBJ) {
        if (idx < 4 || idx >= rw->obj_n || !build_record(rd32(rf + mdltab + 4 * (1 + idx - 4)), 1, LIST(idx), part[t][0])) goto fail;
    } else {
        if (idx < 0 || idx >= rw->itm_n || idx >= 32 || itm[idx].id != d->item) { why = "the item record is another item"; goto fail; }
        if (!build_record(rd32(rf + mdltab + 4 * (1 + nobjs + idx)), 0, LIST(32 + idx), part[t][0])) goto fail;
    }
    if (d->lid >= 0) {   /* linked to node 0, where the room has it next to the body */
        O_WRK* w = &part[t][1]->w;
        if (d->lid < 4 || d->lid >= rw->obj_n || !build_record(rd32(rf + mdltab + 4 * (1 + d->lid - 4)), 1, LIST(d->lid), part[t][1])) goto fail;
        w->flg = 0x80;
        w->lkwkp = (unsigned char*)&part[t][0]->w;
        w->lkono = 0;
        w->lox = obj[d->lid].px - org[0];
        w->loy = obj[d->lid].py - org[1];
        w->loz = obj[d->lid].pz - org[2];
        w->ax = obj[d->lid].ax; w->ay = obj[d->lid].ay; w->az = obj[d->lid].az;
    }
    extent(t, g_placer.type_min[t], g_placer.type_max[t]);
    if (d->kind == PLK_PIECE) {   /* the origin: the bottom centre of its extent */
        float c[3] = { (g_placer.type_min[t][0] + g_placer.type_max[t][0]) * 0.5f, g_placer.type_min[t][1],
                       (g_placer.type_min[t][2] + g_placer.type_max[t][2]) * 0.5f };
        NJS_CNK_OBJECT* o;
        for (o = part[t][0]->w.mdl[0].objP; o; o = o->sibling)
            for (j = 0; j < 3; j++) o->pos[j] -= c[j];
        for (j = 0; j < 3; j++) { g_placer.type_min[t][j] -= c[j]; g_placer.type_max[t][j] -= c[j]; }
    }
    g_placer.type_state[t] = 1;
    host_log("placer: %s from %s, extent x %.1f..%.1f y %.1f..%.1f z %.1f..%.1f (%d KB of %d used)\n", d->name, name,
             g_placer.type_min[t][0], g_placer.type_max[t][0], g_placer.type_min[t][1], g_placer.type_max[t][1],
             g_placer.type_min[t][2], g_placer.type_max[t][2], keep_used >> 10, KEEP >> 10);
    return 1;
fail:
    part[t][0] = part[t][1] = NULL;
    keep_used = keep0;   /* (textures are uploaded last: nothing to release) */
    g_placer.type_state[t] = -1;
    host_log("placer: %s from %s: %s\n", d->name, name, why);
    g_placer.result = why;
    return 0;
}

void placer_textures_released(void)
{
    memset(part, 0, sizeof(part));
    keep_used = 0;
    memset(g_placer.type_state, 0, sizeof(g_placer.type_state));
}

/* ---------------------------------------------------------------- per frame */
static int room_ready(void)
{
    return sys && plp && rom && sys->rdtp && (sys->tk_flg & 0x80) && (sys->sp_flg & 0x2) && !(sys->cb_flg & 0x1);
}
static int here(const Placement* p) { return p->used && p->stg == sys->stg_no && p->rom == sys->rom_no; }

/* y of the surface under (x, y, z): the floor level below, or a box top (a desk, a crate) above it
   (the model's lowest point, unturned, goes there: PL_SNAP) */
static float ground(const float* pos)
{
    NJS_POINT3 q = { pos[0], pos[1] + 0.5f, pos[2] };
    ATR_WORK* htp = sys->htp;
    float y = rom->grand[bhCheckFloorNum(q.y) + 2], g = bhGetGroundPosition(&q);
    sys->htp = htp;
    return g > y && g < q.y + 0.01f ? g : y;
}

static void test_aid(void);

void placer_frame(void)
{
    int i, sel;
    if (!g_dev_tools || !sys || !plp) return;
    g_placer.in_room = room_ready();
    if (!g_placer.in_room) return;
    g_placer.stg = sys->stg_no; g_placer.rom = sys->rom_no; g_placer.rcase = sys->rcase; g_placer.ply = sys->ply_id;
    g_placer.pl_pos[0] = plp->px; g_placer.pl_pos[1] = plp->py; g_placer.pl_pos[2] = plp->pz;
    g_placer.pl_ay = plp->ay;
    test_aid();
    for (i = 0; i < PL_MAX; i++) {
        Placement* p = &g_placer.p[i];
        if (!here(p) || p->type < 0 || p->type >= placer_types()) continue;
        if (!g_placer.type_state[p->type]) load_type(p->type);
        p->flr = bhCheckFloorNum(p->pos[1] + 0.5f);
    }
    sel = g_placer.sel;
    if (__atomic_exchange_n(&g_placer.request, 0, __ATOMIC_ACQ_REL) & PL_SNAP) {
        if (sel >= 0 && sel < PL_MAX && here(&g_placer.p[sel])) {
            g_placer.p[sel].pos[1] = ground(g_placer.p[sel].pos) - g_placer.type_min[g_placer.p[sel].type][1];
            g_placer.result = "Snapped to the surface below.";
            g_placer.edited = 1;
        } else g_placer.result = "Select an object in this room first.";
    }
}

/* world point -> game screen (640x480); 0: behind the camera (the side the player is not on) */
static float front;                     /* view z of the player this frame */
static int project(float x, float y, float z, float* out)
{
    NJS_POINT3 w = { x, y, z }, v;
    NJS_POINT2 sc;
    njCalcPoint((NJS_MATRIX*)cam.mtx, &w, &v);
    if (v.z * front <= 0 || fabsf(v.z) < 1.0f) return 0;
    njProjectScreen((NJS_MATRIX*)cam.mtx, &w, &sc);
    out[0] = sc.x - 1728.0f;   /* GS coordinates: the screen's first pixel is at 1728/1808 (coop_glue.c name tag) */
    out[1] = sc.y - 1808.0f;
    return 1;
}

static void gizmo(const Placement* p)
{
    static const float e[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
    float len, lo[3], hi[3], r;
    int k, j, c, best = 0;
    NJS_POINT3 me = { plp->px, plp->py + 10.0f, plp->pz }, v;
    njCalcPoint((NJS_MATRIX*)cam.mtx, &me, &v);
    front = v.z;
    memcpy(lo, g_placer.type_min[p->type], sizeof(lo));
    memcpy(hi, g_placer.type_max[p->type], sizeof(hi));
    r = 0;
    for (j = 0; j < 3; j++) r = fmaxf(r, fmaxf(fabsf(lo[j]), fabsf(hi[j])));
    len = fminf(fmaxf(r * 1.3f, 5.0f), 20.0f);
    g_placer.gz_len = len;
    if (!project(p->pos[0], p->pos[1], p->pos[2], g_placer.gz_o)) { g_placer.gz_ok = 0; return; }
    for (k = 0; k < 3; k++)
        if (!project(p->pos[0] + e[k][0] * len, p->pos[1] + e[k][1] * len, p->pos[2] + e[k][2] * len, g_placer.gz_axis[k]))
            memcpy(g_placer.gz_axis[k], g_placer.gz_o, sizeof(g_placer.gz_o));
    /* ring k: where a point of the model goes as angle k takes every value (the others kept), so a drag
       along it sets that angle whatever the order of the turns; the point: the unit vector that moves most */
    for (k = 0; k < 3; k++) {
        float spread = -1;
        for (c = 0; c < 3; c++) {
            float a[3], b[3], d;
            NJS_MATRIX m;
            int ang[3] = { p->ang[0], p->ang[1], p->ang[2] };
            NJS_POINT3 u = { e[c][0], e[c][1], e[c][2] }, pa, pb;
            ang[k] = 0;
            njUnitMatrix(&m); njRotateXYZ(&m, ang[0], ang[1], ang[2]); njCalcPoint(&m, &u, &pa);
            ang[k] = 16384;
            njUnitMatrix(&m); njRotateXYZ(&m, ang[0], ang[1], ang[2]); njCalcPoint(&m, &u, &pb);
            a[0] = pa.x; a[1] = pa.y; a[2] = pa.z; b[0] = pb.x; b[1] = pb.y; b[2] = pb.z;
            d = (a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]);
            if (d > spread) { spread = d; best = c; }
        }
        for (j = 0; j < PL_RING; j++) {
            NJS_MATRIX m;
            int ang[3] = { p->ang[0], p->ang[1], p->ang[2] };
            NJS_POINT3 u = { e[best][0] * len * 0.8f, e[best][1] * len * 0.8f, e[best][2] * len * 0.8f }, q;
            ang[k] = 65536 * j / PL_RING;
            njUnitMatrix(&m); njRotateXYZ(&m, ang[0], ang[1], ang[2]); njCalcPoint(&m, &u, &q);
            g_placer.gz_ring_ok[k][j] = (unsigned char)project(p->pos[0] + q.x, p->pos[1] + q.y, p->pos[2] + q.z, g_placer.gz_ring[k][j]);
        }
    }
    g_placer.gz_ok = 1;
}

void placer_draw(void)
{
    int i, k, sel;
    if (!g_dev_tools || !rom || !sys || !plp || !cam.mtx) return;
    sel = g_placer.sel;
    if (!(sys->st_flg & 0x20)) bhSetLight();
    njCnkSetEasyMultiAmbient(rom->amb_r[(unsigned char)rom->amb_itm], rom->amb_g[(unsigned char)rom->amb_itm], rom->amb_b[(unsigned char)rom->amb_itm]);
    njCnkSetSimpleMultiAmbient(rom->amb_r[(unsigned char)rom->amb_itm], rom->amb_g[(unsigned char)rom->amb_itm], rom->amb_b[(unsigned char)rom->amb_itm]);
    njControl3D(0x100);
    for (i = 0; i < PL_MAX; i++) {
        const Placement* p = &g_placer.p[i];
        if (!here(p) || p->type < 0 || p->type >= placer_types() || g_placer.type_state[p->type] != 1) continue;
        njSetMatrix(NULL, cam.mtx);   /* the second light as objitm.c bhDrawObjItm gives the items: the room's
                                         directional light (bhSetLight, st_flg 0x10000) or none */
        if (!(sys->st_flg & 0x10000)) {
            njCnkSetEasyMultiLightColor(1, 0, 0, 0);
            njCnkSetEasyMultiLightSwitch(1, 0);
            njCnkSetSimpleMultiLightColor(1, 0, 0, 0);
            njCnkSetSimpleMultiLightSwitch(1, 0);
        } else {
            njCnkSetEasyMultiLightColor(1, sys->lg_r, sys->lg_g, sys->lg_b);
            njCnkSetEasyMultiLightVector(sys->lg_vx, sys->lg_vy, sys->lg_vz);
            njCnkSetEasyMultiLightSwitch(1, 1);
            njCnkSetEasyMultiLightMatrices();
            njCnkSetSimpleMultiLightColor(1, sys->lg_r, sys->lg_g, sys->lg_b);
            njCnkSetSimpleMultiLightVector(sys->lg_vx, sys->lg_vy, sys->lg_vz);
            njCnkSetSimpleMultiLightSwitch(1, 1);
            njCnkSetSimpleMultiLightMatrices();
        }
        for (k = 0; k < 2 && part[p->type][k]; k++) {
            O_WRK* w = &part[p->type][k]->w;
            if (!k) {
                w->px = p->pos[0]; w->py = p->pos[1]; w->pz = p->pos[2];
                w->ax = p->ang[0]; w->ay = p->ang[1]; w->az = p->ang[2];
            }
            bhCalcModel((BH_PWORK*)w);
            bhPutModel((BH_PWORK*)w);
        }
    }
    njControl3D(0x100);
    if (g_placer.gizmo && sel >= 0 && sel < PL_MAX && here(&g_placer.p[sel]) && g_placer.type_state[g_placer.p[sel].type] == 1) gizmo(&g_placer.p[sel]);
    else g_placer.gz_ok = 0;
}

/* ---------------------------------------------------------------- test aid */
/* RECVX_PLACE="frame:op[:args],...", game frames counted from the first in a room:
   add:type[:dist] (in front of the player, selected), sel:i, move:dx:dy:dz, rot:ax:ay:az (degrees), snap, del,
   rings:0/1 (the pitch and roll rings), log (the entries in this room; the gizmo's place while the F2 window shows it: RECVX_SHOW_OVERLAY=2) */
static void test_aid(void)
{
    static const char* s = (const char*)-1;
    static int clock;
    char op[16], arg[48];
    int f, n = 0;
    if (s == (const char*)-1) s = getenv("RECVX_PLACE");
    if (!s || !*s) return;
    clock++;
    if (sscanf(s, "%d:%15[a-z]%n", &f, op, &n) != 2 || clock < f) return;
    s += n;
    arg[0] = 0;
    if (*s == ':') sscanf(s + 1, "%47[^,]", arg);
    s = strchr(s, ',') ? strchr(s, ',') + 1 : NULL;
    if (!strcmp(op, "add")) {
        char key[32] = "";
        float dist = 12.0f;
        int t, i;
        sscanf(arg, "%31[^:]:%f", key, &dist);
        if ((t = placer_type_find(key)) < 0) { printf("test: placer: no type %s\n", key); return; }
        for (i = 0; i < PL_MAX && g_placer.p[i].used; i++);
        if (i == PL_MAX) return;
        memset(&g_placer.p[i], 0, sizeof(Placement));
        g_placer.p[i].type = t;
        g_placer.p[i].stg = sys->stg_no; g_placer.p[i].rom = sys->rom_no; g_placer.p[i].rcase = sys->rcase; g_placer.p[i].ply = sys->ply_id;
        g_placer.p[i].pos[0] = plp->px - dist * njSin(plp->ay);
        g_placer.p[i].pos[1] = plp->py;
        g_placer.p[i].pos[2] = plp->pz - dist * njCos(plp->ay);
        g_placer.p[i].ang[1] = (plp->ay + 0x8000) & 0xFFFF;   /* facing the player */
        g_placer.p[i].used = 1;
        g_placer.sel = i;
    } else if (!strcmp(op, "sel")) g_placer.sel = atoi(arg);
    else if (!strcmp(op, "rings")) g_placer.rings = atoi(arg);
    else if (g_placer.sel >= 0 && g_placer.sel < PL_MAX && g_placer.p[g_placer.sel].used) {
        Placement* p = &g_placer.p[g_placer.sel];
        float a[3] = { 0, 0, 0 };
        sscanf(arg, "%f:%f:%f", &a[0], &a[1], &a[2]);
        if (!strcmp(op, "move")) for (n = 0; n < 3; n++) p->pos[n] += a[n];
        else if (!strcmp(op, "rot")) for (n = 0; n < 3; n++) p->ang[n] = (p->ang[n] + (int)lroundf(a[n] * 65536.0f / 360.0f)) & 0xFFFF;
        else if (!strcmp(op, "snap")) g_placer.request |= PL_SNAP;
        else if (!strcmp(op, "del")) { p->used = 0; g_placer.sel = -1; }
    }
    if (strcmp(op, "log")) g_placer.edited = 1;
    if (!strcmp(op, "log") && g_placer.gz_ok)
        printf("test: placer gizmo at %.0f %.0f, x tip %.0f %.0f, y tip %.0f %.0f, z tip %.0f %.0f (length %.1f)\n", g_placer.gz_o[0], g_placer.gz_o[1],
               g_placer.gz_axis[0][0], g_placer.gz_axis[0][1], g_placer.gz_axis[1][0], g_placer.gz_axis[1][1], g_placer.gz_axis[2][0],
               g_placer.gz_axis[2][1], g_placer.gz_len);
    if (!strcmp(op, "log"))
        for (n = 0; n < PL_MAX; n++)
            if (here(&g_placer.p[n]))
                printf("test: placer %d %s at %.2f %.2f %.2f ang %d %d %d floor %d%s\n", n, placer_type_key(g_placer.p[n].type),
                       g_placer.p[n].pos[0], g_placer.p[n].pos[1], g_placer.p[n].pos[2], g_placer.p[n].ang[0], g_placer.p[n].ang[1],
                       g_placer.p[n].ang[2], g_placer.p[n].flr, g_placer.type_state[g_placer.p[n].type] == 1 ? "" : " (not loaded)");
    printf("test: placer %s %s at game frame %d\n", op, arg, clock);
}
