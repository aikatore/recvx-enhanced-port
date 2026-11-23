/* "Show Randomized Item Models" (GAME SETUP > BIORAND, ini biorand_item_models, default 1; docs/BIORAND_INGAME.md "Item models (2026-11-21)"):
 * in a BioRand game an item spot whose pickup BioRand changed (its record's type, docs/modding/ROOM_FORMAT.md "Generated
 * pickups"; the id stays the model shown) shows the new item's ground model instead, where some room has one.
 *
 * The models come from the catalogue biorand.c reads once per run (biorand_item_model: per item id the MLB of its first
 * floor record in the room files and that record's texture list). At the end of the room load (enemyopt_glue.c
 * enemy_room_finished, after bhFinishRoom) each changed spot gets a copy of it, realized as room.c bhSetItmMdl does, its
 * textures uploaded (item textures carry their own CLUT: no palette bank of the room is touched) and its O_WORKs from
 * the room arena. An item no floor record has (gunpowder) shows its display model (display_model: the same mesh). The
 * spot moves up or down so the new model rests where the original did (rest_shift). Only the model (and that height) is
 * swapped: id, type, amount and the pickup stay the record's.
 *
 * Lifetime: the copy lives in the spawn pool (spawn_glue.c, emptied when the next room is set up, as the room arena is
 * overwritten), its O_WORKs in the room arena, its texture list kept by spawn_keep_texlist (released with the room's,
 * bhReleaseMainTexture -> spawn_release). Before that release, and on njReleaseTextureAll, every swapped spot gets its
 * original ML_WORK back (itemmdl_release), so the room's own release frees the original's textures exactly as without the
 * setting, and nothing points at the copy any more. The new model's O_WORK array has at least the original's node count
 * (a script or link may name a node of the original). Game thread.
 */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/item.h"
#include "../../decomp/include/ps2/veronica/prog/binfunc.h"
#include "../../decomp/include/ps2/veronica/prog/dread.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_texture.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_NaTextureFunction.h"
#include "../src/host/actors.h"
#include "../src/host/biorand.h"
#include "../include/pc_rules.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void host_log(const char* fmt, ...);
unsigned char* spawn_pool_alloc(int len, int mis);   /* spawn_glue.c */
int spawn_keep_texlist(const NJS_TEXLIST* l);
int spawn_room_left(void);
Sint32 ADXF_ReadNw32(ADXF adxf, Sint32 nsct, void* buf);   /* PC: synchronous */
int spawn_pool_owns(const void* p);
int spawn_texlist_kept(const NJS_TEXLIST* l);

#define SPOTS 32                        /* sys->itwp records a room uses (coop_glue.c ITWP_N; retail max 13) */
#define TEX_KEEP 16                     /* texture entries and heap left for the room's own uses, as spawn_glue.c */
#define HEAP_KEEP (1 << 20)
#define ROOM_KEEP (512 << 10)

static struct { int spot; ML_WORK orig; float dy; } swapped[SPOTS];
static int nswapped;

/* the swapped spots' own models back (bhReleaseMainTexture before the room's item textures, njReleaseTextureAll) */
void itemmdl_release(void)
{
    for (int k = 0; k < nswapped; k++) {
        O_WRK* op = &sys->itwp[swapped[k].spot];
        op->mdl[0] = swapped[k].orig;
        op->mlwP = &op->mdl[0];
    }
    nswapped = 0;
}

void itemmdl_room_setup(void) { nswapped = 0; }   /* room.c bhSetRoom (spawn_room_setup): the records are set up anew */

/* every frame (biorand_glue.c biorand_frame, before the frame is drawn): a swapped spot whose copy or texture list is no
   longer held (never expected: the release puts the originals back first) logs and shows its original again */
void itemmdl_check(void)
{
    static int logged;
    for (int k = 0; k < nswapped; k++) {
        const ML_WORK* m = &sys->itwp[swapped[k].spot].mdl[0];
        if (spawn_pool_owns(m->datP) && (!m->texP || spawn_texlist_kept(m->texP))) continue;
        if (logged++ < 8) host_log("items: spot %d's model or textures were released while shown: its original is back\n", swapped[k].spot);
        sys->itwp[swapped[k].spot].mdl[0] = swapped[k].orig;
        sys->itwp[swapped[k].spot].mlwP = &sys->itwp[swapped[k].spot].mdl[0];
        sys->itwp[swapped[k].spot].py -= swapped[k].dy;
        swapped[k--] = swapped[--nswapped];
    }
}

/* The model's vertical extent (*lo, *hi) in its own space: chunk vertex lists 51 (ROOM_FORMAT.md) and 41 of every node, the
   nodes' translations and scales applied (ponytail: their rotations not; item nodes are rarely rotated). 0: none found. */
static int extent_node(const NJS_CNK_OBJECT* o, float y0, float s, float* lo, float* hi, int depth)
{
    int n = 0;
    for (; o && depth < 32; o = o->sibling) {
        float y = y0 + o->pos[1] * s, sc = s * o->scl[1];
        const unsigned char* p = o->model ? (const unsigned char*)o->model->vlist : NULL;
        for (int guard = 0; p && p[0] != 0xFF && guard < 64; guard++) {
            unsigned size = (unsigned)(p[2] | p[3] << 8), count = (unsigned)(p[6] | p[7] << 8);
            unsigned at = p[0] == 51 ? 64 : 8, step = p[0] == 51 ? 32 : 24;   /* (41: x y z nx ny nz, the display models') */
            if ((p[0] == 51 || p[0] == 41) && at + 1ull * step * count <= 4 + 4ull * size)
                for (unsigned v = 0; v < count; v++, n++) {
                    float vy;
                    memcpy(&vy, p + at + step * v + 4, 4);
                    vy = y + vy * sc;
                    if (!n || vy < *lo) *lo = vy;
                    if (!n || vy > *hi) *hi = vy;
                }
            p += 4 + 4 * size;
        }
        n += extent_node(o->child, y, sc, lo, hi, depth + 1);
    }
    return n;
}

/* how far the new model is moved up or down so that it rests where the original did (its lowest point, on the side
   away from the original's bulk: the floor or shelf the spot is on); 0 when either has no vertices or it is odd */
static float rest_shift(const ML_WORK* old, const ML_WORK* nw)
{
    float lo0 = 0, hi0 = 0, lo1 = 0, hi1 = 0, dy;
    if (!old->objP || !nw->objP || !extent_node(old->objP, 0, 1, &lo0, &hi0, 0) || !extent_node(nw->objP, 0, 1, &lo1, &hi1, 0)) return 0;
    dy = hi0 + lo0 < 0 ? hi0 - hi1 : lo0 - lo1;   /* (y grows downward when the original's bulk lies below 0) */
    return dy > -20 && dy < 20 ? dy : 0;
}

/* a room effect or light follows a node of spot i that a model of `nodes` nodes does not have (the spot keeps its own;
   most spots have one on node 0: the glint) */
static int linked(int i, int nodes)
{
    for (int k = 0; k < rom->eff_n; k++) if (rom->effp[k].lkflg == 4 && rom->effp[k].lkno == i && rom->effp[k].lkono >= nodes) return 1;
    for (int k = 0; k < rom->lgt_n; k++) if (rom->lgtp[k].lkflg == 4 && rom->lgtp[k].lkno == i && rom->lgtp[k].lkono >= nodes) return 1;
    return 0;
}

/* spot i shows model m (the catalogue's of item id); NULL done, else why not */
static const char* swap(int i, const BrItemModel* m)
{
    O_WRK* op = &sys->itwp[i];
    ML_WORK nm;
    unsigned char *mlb, *raw = NULL, *tex = NULL;
    int entries, heap, owb = (int)(op->mdl[0].obj_num > 64 ? op->mdl[0].obj_num : 64) * 80 + 16;
    unsigned int nodes;
    if (spawn_room_left() < owb * 2 + ROOM_KEEP) return "not enough room memory";
    actor_tex_free(&entries, &heap);
    if (entries < 8 + TEX_KEEP || heap < HEAP_KEEP) return "not enough texture memory";
    if (m->tex && !(raw = malloc((size_t)m->tlen + 128))) return "out of memory";
    if (!(mlb = spawn_pool_alloc(m->mlen, m->mmis))) { free(raw); return "spawn memory full"; }
    memcpy(mlb, m->mlb, (size_t)m->mlen);
    memset(&nm, 0, sizeof(nm));
    bhMlbBinRealize(mlb, &nm);
    if ((nm.texP != NULL) != (m->tex != NULL) || !nm.objP || (nm.texP && (nm.texP->nbTexture == 0 || nm.texP->nbTexture > 64))) {
        free(raw);
        return "not a model";   /* (the pool bytes go with the room) */
    }
    if (nm.texP) {   /* uploaded from a copy at the list's alignment (bhSetMemPvpTexture writes into its data) */
        int got;
        tex = (unsigned char*)(((size_t)raw + 63) & ~(size_t)63) + m->tmis;
        memcpy(tex, m->tex, (size_t)m->tlen);
        got = bhSetMemPvpTexture(nm.texP, tex, 0);
        free(raw);
        if (got != (int)nm.texP->nbTexture) {   /* (only what was loaded is released) */
            NJS_TEXLIST t = { nm.texP->textures, (unsigned int)(got > 0 ? got : 0) };
            if (got > 0) njReleaseTexture(&t);
            static char why[64];
            snprintf(why, sizeof(why), "its texture list does not fit it (%d of %d)", got, (int)nm.texP->nbTexture);
            return why;
        }
        if (!spawn_keep_texlist(nm.texP)) { njReleaseTexture(nm.texP); return "texture list table full"; }
    }
    swapped[nswapped].spot = i;
    swapped[nswapped].dy = op->ax || op->az ? 0 : rest_shift(&op->mdl[0], &nm);   /* (a tilted spot: as it is) */
    op->py += swapped[nswapped].dy;
    swapped[nswapped++].orig = op->mdl[0];
    nodes = nm.obj_num;
    if (op->mdl[0].obj_num > nm.obj_num) nm.obj_num = op->mdl[0].obj_num;   /* (O_WORKs for every node the original had) */
    sys->memp = bhKeepObjWork(&nm, sys->memp);
    nm.obj_num = nodes;
    nm.flg &= ~0x200;   /* (not the owner of a room texture list: spawn_release frees it) */
    op->mdl[0] = nm;
    op->mlwP = &op->mdl[0];
    return NULL;
}

/* An item no room has on its floor (gunpowder, gunpowder arrows): its display model, the one the status screen shows
   (ITEM AFS file display id - 1, chosen as sub1.c and coop_glue.c grant_model do: the same kind of MLB with its texture
   pack behind it), read from the disc at the room load into a buffer kept for the run (room 0: from the item archive).
   NULL: none. */
static const BrItemModel* display_model(int item)
{
    static BrItemModel dm[160];
    static unsigned char tried[160];
    int disp, n, dt;
    unsigned char *buf, *p;
    ADXF h;
    if (item <= 0 || item >= 156) return NULL;
    if (tried[item]) return dm[item].mlb ? &dm[item] : NULL;
    tried[item] = 1;
    disp = itemidtbl[itemflg[dsptbl[item].hide][6]];
    if (!disp) disp = item;
    if (disp == 131) disp = 5;    /* (sub1.c: the Enhanced Handgun shows the handgun's) */
    if (disp == 136) disp = 33;
    if (!(h = ADXF_OpenAfs(sys->itm_partid, disp - 1))) return NULL;
    n = ADXF_GetFsizeByte(h);
    buf = n > 16 && n < (4 << 20) ? malloc((size_t)ADXF_GetFsizeSct(h) * 2048 + 8) : NULL;
    if (buf) {
        ADXF_ReadNw32(h, ADXF_GetFsizeSct(h), buf);   /* synchronous on the PC */
        if (ADXF_GetStat(h) != 3) { free(buf); buf = NULL; }
    }
    ADXF_Close(h);
    if (!buf) return NULL;
    dt = *(int*)buf;
    p = buf + 4 + dt;
    if (dt < 16 || dt > n - 8 || memcmp(buf + 4, "MDL", 3)) { free(buf); return NULL; }
    p = (*(int*)p & 0x80000000) ? buf + ((p - buf + 4 + 31) & ~31) : p + 4;   /* (system.c's reader; 32-aligned in the file as read there) */
    memset(buf + n, 0xFF, 8);   /* (the pack's end, should it have no code -1) */
    dm[item].mlb = buf + 4; dm[item].mlen = dt; dm[item].mmis = 4;   /* (as read into a 64-aligned buffer) */
    dm[item].tex = p < buf + n ? p : NULL; dm[item].tlen = p < buf + n ? (int)(buf + n + 8 - p) : 0; dm[item].tmis = (int)((p - buf) & 63);
    dm[item].room = -1;   /* (not a room's) */
    return &dm[item];
}

/* the end of the room load (enemyopt_glue.c enemy_room_finished) */
void itemmdl_room(void)
{
    double t0;
    int n, done = 0, kept = 0, none = 0;
    if (nswapped) return;   /* (this load's done: their originals are what itemmdl_release puts back) */
    if (!sys || !rom || !sys->itwp || !rom->itmp || !biorand_item_models_on()) return;
    if (!biorand_item_models_want()) { host_log("items: the item model catalogue is not read yet: this room keeps its models\n"); return; }
    t0 = biorand_ms();
    n = rom->itm_n < SPOTS ? rom->itm_n : SPOTS;
    for (int i = 0; i < n; i++) {
        const ETTY_WORK* r = &rom->itmp[i];
        const O_WRK* op = &sys->itwp[i];
        int give = pc_pickup_item(r->type, r->id);
        const BrItemModel* m;
        const char* why;
        if (give == r->id || !(op->flg & 0x1) || op->mlwP != &op->mdl[0]) continue;
        if (!(m = biorand_item_model(give)) && !(m = display_model(give))) {
            none++;
            host_log("items: spot %d (%d) gives %d: no model of it, kept\n", i, r->id, give);
            continue;
        }
        if (linked(i, m->mlb[6] | m->mlb[7] << 8)) { kept++; host_log("items: spot %d (%d) gives %d: an effect or light follows a node its model lacks, kept\n", i, r->id, give); continue; }
        if ((why = swap(i, m))) { kept++; host_log("items: spot %d (%d) gives %d: %s, kept\n", i, r->id, give, why); continue; }
        done++;
        {
            char src[24];
            snprintf(src, sizeof(src), m->room < 0 ? "its display model" : "from RM_%04d", m->room);
            host_log("items: spot %d (%d) shows %d (%s) at %.0f %.0f floor %d, moved %+.2f to rest as the original\n", i, r->id, give, src, r->px, r->pz,
                     r->flr_no, sys->itwp[i].py - r->py);
        }
    }
    if (done || kept || none)
        host_log("items: %d spots show their new item, %d kept, %d without a model (%.2f ms; %d KB room memory left)\n", done, kept, none,
                 biorand_ms() - t0, spawn_room_left() >> 10);
}
