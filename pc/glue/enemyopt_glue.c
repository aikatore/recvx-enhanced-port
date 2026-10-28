/* Enemy options (docs/ENEMIES.md "Enemy options"): (a) extra enemies of types the room already has,
 * (b) rearranged placements, (c) the enemy randomizer. Off by default; with everything off nothing
 * here touches the game.
 *
 * (a) runs on the host (or in single player) once per room load, at the first frame the room runs:
 * the extras are spawned like the F2 spawner's (spawn_glue.c spawn_add) and so reach a co-op guest
 * as added enemies. (b) and (c) change records at the end of the room load (room.c bhFinishRoom),
 * deterministically from the room file and the seed, on both co-op sides alike (the guest uses the
 * host's options), so the slots match and the host's snapshots drive the guest's copies.
 */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/hitchkl.h"
#include "../../decomp/include/ps2/veronica/prog/system.h"
#include "../../decomp/include/ps2/veronica/prog/MdlPut.h"
#include "../../decomp/include/ps2/veronica/prog/hitchk.h"
#include "../src/host/actors.h"
#include "../src/host/cast.h"
#include "../src/host/enemies.h"
#include "../src/host/variants.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void host_log(const char* fmt, ...);
int coop_is_guest(void);
const char* spawn_add(const ActorModel* am, const SpawnNote* n, int* slot_out);
const char* spawn_at(const ActorModel* am, const SpawnNote* n, int want, int mode, int* slot_out);
const ActorModel* spawn_entry(int i);
int spawn_base_count(void);
const char* spawn_refused(int id);
const ActorModel* variant_base(const EnemyVariant* v);   /* variant_glue.c */
void variant_room(void);
void cast_room(const EnemyOpts* o);   /* cast_glue.c */
void spawn_keep_texlist(const NJS_TEXLIST* l);
void bhCheckEneWorkNum(void);   /* eneset.h */

EnemyOpts g_enemy_opts = { .variety = 1, .events = 1 };   /* settings (F1, recvx.ini) */
static EnemyOpts host_opts;             /* a co-op guest's: the host's */
static int have_host_opts;
static unsigned char host_off[CAST_OFF_BYTES];   /* and its "Custom" list's unchecked models (EV_CASTOFF) */
int enemy_room_gen;                     /* room loads so far */

/* test aids: RECVX_ENEMY_EXTRA / _PLACE / _RANDOM / _SEED / _VARIETY / _EVENTS override the settings */
__attribute__((constructor)) static void opts_env(void)
{
    const char* e;
    if ((e = getenv("RECVX_ENEMY_EXTRA"))) g_enemy_opts.extra = atoi(e);
    if ((e = getenv("RECVX_ENEMY_PLACE"))) g_enemy_opts.place = atoi(e);
    if ((e = getenv("RECVX_ENEMY_RANDOM"))) g_enemy_opts.rand = atoi(e);
    if ((e = getenv("RECVX_ENEMY_SEED"))) g_enemy_opts.seed = atoi(e);
    if ((e = getenv("RECVX_ENEMY_VARIETY"))) g_enemy_opts.variety = atoi(e);
    if ((e = getenv("RECVX_ENEMY_EVENTS"))) g_enemy_opts.events = atoi(e);
    if ((e = getenv("RECVX_CAST"))) g_enemy_opts.cast = atoi(e) != 0;   /* the cutscene cast: 0/1, then ":all" / ":random" / ":imports" */
    if (e && strstr(e, "all")) g_enemy_opts.cast_all = 1;
    if (e && strstr(e, "custom")) g_enemy_opts.cast_all = 2;   /* (the models: recvx.ini cast_custom_off) */
    if (e && strstr(e, "random")) g_enemy_opts.cast_assign = 1;
    if (e && strstr(e, "imports")) g_enemy_opts.cast_imports = 1;
}

const EnemyOpts* enemy_opts(void)
{
    static const EnemyOpts none;
    if (coop_is_guest()) return have_host_opts ? &host_opts : &none;
    return &g_enemy_opts;
}

static int kept_n;
void enemy_opts_from_host(const EnemyOpts* o)
{
    have_host_opts = o != NULL;
    if (!o) { kept_n = 0; memset(host_off, 0, sizeof(host_off)); }   /* (enemy_rand_kept, EV_CASTOFF: the host's go with its session) */
    if (o) {
        host_opts = *o;
        host_log("enemies: the host's options: extra %d, placements %d, randomizer %d (%s%s), seed %d, cutscene cast %d (%s, %s%s)\n", o->extra, o->place, o->rand,
                 o->variety ? "wild" : "mild", o->events ? ", event enemies" : "", o->seed, o->cast, o->cast_all == 2 ? "custom models" : o->cast_all ? "all models" : "cutscene models",
                 o->cast_assign ? "random" : "fixed", o->cast_imports ? ", imported models" : "");
    }
}

const unsigned char* enemy_cast_off(void) { return coop_is_guest() ? host_off : cast_off; }

void enemy_cast_off_from_host(int at, int n, const unsigned char* bits)
{
    if (at < 0 || n < 0 || at + n > (int)sizeof(host_off)) return;   /* (coop_check_event checked it) */
    memcpy(host_off + at, bits, (size_t)n);
}

/* ---------------------------------------------------------------- helpers */
static unsigned int rng;
static unsigned int rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }   /* xorshift32 */
static void rnd_seed(unsigned int a, unsigned int b)
{
    rng = a * 2654435761u ^ (b + 0x9E3779B9u);
    if (!rng) rng = 1;
    rnd(); rnd();
}
static int spot_player;                 /* find_spot avoids the player too (at run time, not at the room load) */
int room_file_now(void) { return sys->stg_no * 1000 + sys->rom_no * 10 + sys->rcase; }

/* what the room script does with each record (pc/tools/enemies.py classifies the same way) */
enum { REC_EVENT = 1, REC_SHOWN = 2, REC_HP = 4 };
static unsigned char rec_flags[128];
static const ActorModel* rand_src[128];   /* randomized records: where their new type comes from */
static unsigned short rand_var[128];    /* and which enemy variant they became (0: none) */
static int all_dead_check;              /* the script counts kills (DieEventCk): "all dead" doors */
static int rm_flg_used;                 /* the script tests or sets rm_flg bits 29..31 (= 0x4, 0x2, 0x1), which some types write */

static void scan_script(void)
{
    unsigned char* base = (unsigned char*)rom->evtp;
    unsigned int* tab = (unsigned int*)base;
    unsigned int offs[256], first, o, end;
    int n = 0, i, j;
    memset(rec_flags, 0, sizeof(rec_flags));
    all_dead_check = rm_flg_used = 0;
    if (!base) return;
    first = tab[0];
    for (i = 0; (unsigned)i * 4 < first && i < 256; i++) {
        offs[n++] = tab[i];
        if (tab[i] && tab[i] < first) first = tab[i];
    }
    for (i = 0; i < n; i++) {
        unsigned int ends[64];
        int depth = 0;
        for (end = 0x7FFFFFFF, j = 0; j < n; j++)
            if (offs[j] > offs[i] && offs[j] < end) end = offs[j];
        for (o = offs[i]; o < end && o < (unsigned)sys->rdtsz;) {
            unsigned char* p = base + o;
            int len = coop_insn_len(p), op = p[0];
            while (depth && ends[depth - 1] <= o) depth--;
            if (len <= 0) break;
            switch (op) {
            case 101: case 104: case 181: if (p[1] == 1) rec_flags[p[2] & 127] |= REC_EVENT; break;   /* LoadWork, 2, Ex */
            case 45: case 48: case 83: case 125: rec_flags[p[1] & 127] |= REC_EVENT; break;   /* motion pauses, links, positioning */
            case 81: rec_flags[p[2] & 127] |= REC_EVENT; break;
            case 84: rec_flags[p[1] & 127] |= REC_EVENT; rec_flags[p[2] & 127] |= REC_EVENT; break;
            case 142: case 143: case 154: case 165: case 204: rec_flags[p[1] & 127] |= REC_EVENT; break;   /* zombie/face/render/look ops read the type's work */
            case 144: rec_flags[p[1] & 127] |= REC_EVENT; break;
            case 121: rec_flags[p[2] & 127] |= REC_EVENT; break;   /* EneSeOn */
            case 7: if (p[2] == 10) rec_flags[p[1] & 127] |= REC_HP; break;   /* CmpW on the HP */
            case 9: if (p[1] >= 11 && p[1] <= 15) rec_flags[p[1] - 11] |= REC_HP; break;   /* SvW */
            case 153: case 208: rec_flags[p[1] & 127] |= REC_HP; break;   /* EnemyHpUp, EneLifeSetM */
            case 36: if (p[1] == 1) rec_flags[p[2] & 127] |= REC_SHOWN; break;   /* InitModelSet */
            case 33: all_dead_check = 1; break;   /* DieEventCk */
            case 4: case 5: {   /* Ck / Set rm_flg (bit n: mask 0x80000000 >> n) */
                int b = (p[2] | p[3] << 8) & 0x3FF;
                if (p[1] == 4 && b >= 29 && b <= 31) rm_flg_used = 1;
                break;
            }
            case 1: if (depth < 64) ends[depth++] = o + p[1] + 1; break;   /* IfelCk */
            case 2: if (depth) depth--; if (depth < 64) ends[depth++] = o + p[1]; break;   /* ElseCk */
            case 3: if (depth) depth--; break;   /* Endif */
            }
            o += len;
            if (!depth && (op == 0 || (i >= 2 && op == 255))) break;
        }
    }
}

/* a free spot near (x, z) on floor `flr`: no wall between it and the spot (plus a body's width
   beyond), away from the player; 1 = found */
static int near_entry(float x, float z, int player)
{
    int i;
    for (i = 0; i < rom->pos_n; i++) {
        float dx = x - rom->posp[i].px, dz = z - rom->posp[i].pz;
        if (dx * dx + dz * dz < 40.0f * 40.0f) return 1;
    }
    if (player) {
        float dx = x - plp->px, dz = z - plp->pz;
        if (dx * dx + dz * dz < 40.0f * 40.0f) return 1;
    }
    return 0;
}

static int find_spot(float x, float y, float z, int flr, float rmin, float rmax, float* ox, float* oz)
{
    int k, start = rnd() & 7;
    for (k = 0; k < 16; k++) {
        float a = (float)((start + k * 3) & 15) * (6.2831853f / 16.0f);
        float r = rmin + (rmax - rmin) * (float)(rnd() & 1023) / 1023.0f;
        float tx = x + r * sinf(a), tz = z + r * cosf(a);
        NJS_POINT3 p1 = { x, y + 10.0f, z }, p2 = { x + (r + 8.0f) * sinf(a), y + 10.0f, z + (r + 8.0f) * cosf(a) };
        if (near_entry(tx, tz, spot_player)) continue;
        if (bhCollisionCheckLine2(&p1, &p2, 0x4400, (char)flr)) continue;
        *ox = tx; *oz = tz;
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- (b) rearranged placements */
static int roams(int id) { return id == 1 || id == 3 || id == 4 || id == 5 || id == 7 || id == 9 || id == 26; }

static void place(const EnemyOpts* o)
{
    int k, cand[128], nc = 0, moved = 0;
    for (k = 0; k < rom->ene_n && k < 128; k++)
        if ((ene[k].flg & 0x1) && !(rec_flags[k] & (REC_EVENT | REC_SHOWN)) && roams(ene[k].id)) cand[nc++] = k;
    for (k = 0; k < nc; k++) {
        BH_PWORK* ep = &ene[cand[k]];
        ETTY_WORK* r = &rom->enep[cand[k]];
        float x = ep->px, z = ep->pz, nx, nz;
        rnd_seed((unsigned)o->seed, (unsigned)room_file_now() * 131u + (unsigned)cand[k] + 0x5000u);
        if (nc > 1 && (rnd() & 1)) {   /* start from another record's spot on the same floor */
            BH_PWORK* q = &ene[cand[rnd() % nc]];
            if (q->flr_no == ep->flr_no && fabsf(q->py - ep->py) < 1.0f) { x = q->px; z = q->pz; }
        }
        if (!find_spot(x, ep->py, z, ep->flr_no, 8.0f, 30.0f, &nx, &nz)) continue;
        ep->px = ep->pxb = r->px = nx;
        ep->pz = ep->pzb = r->pz = nz;
        ep->ay = r->ay = (short)(rnd() & 0xFFFF);
        if (ep->id == 4 && ep->type) ep->type = r->type = 0;   /* a Cerberus waiting in its wake-up area would wait forever */
        bhCalcModel(ep);
        moved++;
    }
    if (nc) host_log("enemies: room %04d: %d of %d free-roaming enemies placed elsewhere (seed %d)\n", room_file_now(), moved, nc, o->seed);
}

/* ---------------------------------------------------------------- (c) randomizer */
/* A co-op guest randomizes as the host (the same options, seed and room file), but each side refuses a
   record whose type does not fit its own texture memory, and the guest's puppets follow the host's
   slots. So the guest follows the host: its spawns take no texture reserve (SPAWN_DICTATED), and the
   records the host had to keep come as SPAWN_NOTE_KEPT notes (coop_rand_kept) before the guest loads
   the room - with the host there first, the usual case; a guest that was there first logs a
   difference. Kept here until the guest's next load of that room. */
#define KEPT_MAX 64
static struct { short room; unsigned char rec; } kept[KEPT_MAX];

void enemy_rand_kept(int room, int rec, int id)
{
    int k;
    if (rec >= 128) return;
    if (sys && rom && (sys->tk_flg & 0x80) && room == room_file_now() && rec < rom->ene_n && (ene[rec].flg & 0x1) && ene[rec].id != id)
        host_log("enemies: the host kept record %d of room %04d as type %d; here it is type %d (this room was loaded before the host's)\n", rec, room, id, ene[rec].id);
    for (k = 0; k < kept_n && !(kept[k].room == room && kept[k].rec == rec); k++);
    if (k < kept_n) return;
    if (kept_n == KEPT_MAX) memmove(kept, kept + 1, sizeof(kept[0]) * --kept_n);
    kept[kept_n].room = (short)room;
    kept[kept_n++].rec = (unsigned char)rec;
}

/* boss arenas: their scripts wait for the boss's death or drive it; nothing in these rooms changes */
static int boss_arena(int id)
{
    return id == 2 || (id >= 12 && id <= 17) || id == 19 || id == 23 || id == 24 || id == 29 || id == 30 || (id >= 53 && id <= 55);
}
#define slot_cost(id) ((id) < 31 ? spawn_slots_needed(id) : 1)

/* What a record can become: a kind (weighted), then one of its looks (the spawn list's entries of that type,
   unused ones in this room first). Weights per variety (0 Mild, 1 Wild); 0: not in that variety. */
enum { K_BIG = 1,       /* needs open space around the record (not in corridors or small rooms) */
       K_FLIER = 2,     /* only for flier records (bats, Spotters), which hover where they are placed */
       K_RMFLG = 4,     /* its code writes rm_flg bits 0..2 (Tyrant clears them every frame): not where the script uses them */
       K_NOCOUNT = 8,   /* not where the doors count the kills: it may leave the room, fly out of reach or take too long */
       K_HEAVY = 16 };  /* an enemy variant marked heavy (boss-like): one per room */
static const struct { unsigned char id, w[2], flags; } kinds[] = {
    { 1, { 5, 5 }, 0 },                         /* zombie, 24 looks */
    { 4, { 4, 4 }, 0 },                         /* Cerberus */
    { 9, { 4, 4 }, 0 },                         /* Bandersnatch */
    { 5, { 3, 4 }, K_NOCOUNT },                 /* Hunter, Sweeper: may leap out of a room */
    { 3, { 2, 3 }, K_BIG | K_NOCOUNT },         /* Black Widow (with its brood 25 slots; climbs walls) */
    { 26, { 2, 2 }, 0 },                        /* anatomist */
    { 19, { 0, 2 }, K_BIG | K_RMFLG | K_NOCOUNT },   /* Tyrant, both looks */
    { 23, { 0, 1 }, K_BIG | K_RMFLG | K_NOCOUNT },   /* Giant Black Widow */
    { 7, { 0, 3 }, K_FLIER | K_RMFLG | K_NOCOUNT },  /* bat */
    { 11, { 0, 2 }, K_FLIER | K_RMFLG | K_NOCOUNT }, /* Spotter */
};
#define KINDS (int)(sizeof(kinds) / sizeof(kinds[0]))
/* the kinds a room's records can become: those above, then the enemy variants with a randomizer weight
   (docs/ENEMIES.md "Enemy variants"): a variant takes its base kind's rules (another type's: not where doors
   count kills, nor where the script uses rm_flg bits), plus big, heavy and the stages it may appear in. The
   same on both co-op sides as long as both have the same variants (built in; mods/enemies.ini). */
typedef struct { unsigned char id, w[2], flags, cost; const EnemyVariant* v; } Kind;
static int make_kinds(Kind* kd)
{
    int n = 0, i, j;
    for (i = 0; i < KINDS; i++) { kd[n].id = kinds[i].id; kd[n].w[0] = kinds[i].w[0]; kd[n].w[1] = kinds[i].w[1]; kd[n].flags = kinds[i].flags;
                                  kd[n].cost = (unsigned char)slot_cost(kinds[i].id); kd[n++].v = NULL; }
    for (i = 0; i < variant_count(); i++) {
        const EnemyVariant* v = variant_at(i);
        int f = K_NOCOUNT | K_RMFLG;
        if (!v->wild && !v->mild) continue;
        if (!(v->stages & (1 << (sys->stg_no % 10))) || !variant_base(v) || spawn_refused(v->type)) continue;
        for (j = 0; j < KINDS; j++) if (kinds[j].id == v->type) f = kinds[j].flags;
        kd[n].id = v->type;
        kd[n].w[0] = v->heavy ? 0 : v->mild;
        kd[n].w[1] = v->wild;
        kd[n].flags = (unsigned char)(f | (v->big || v->heavy ? K_BIG : 0) | (v->heavy ? K_NOCOUNT | K_HEAVY : 0));
        kd[n].cost = (unsigned char)(v->slots > slot_cost(v->type) ? v->slots : slot_cost(v->type));
        kd[n++].v = v;
    }
    return n;
}
static int ground_record(int id) { return id == 1 || id == 3 || id == 4 || id == 5 || id == 9; }
static int flier_record(int id) { return id == 7 || id == 11; }
static int look_ok(const ActorModel* m, int id) { return m->id == id && !(id == 9 && m->mdlver == 1); }   /* (Bandersnatch 2 is an event's arm) */

/* directions (of 8) with no wall within `len` units of the record, at waist height */
static int open_dirs(const BH_PWORK* ep, float len)
{
    int k, n = 0;
    for (k = 0; k < 8; k++) {
        float a = (float)k * (6.2831853f / 8.0f);
        NJS_POINT3 p1 = { ep->px, ep->py + 10.0f, ep->pz }, p2 = { ep->px + len * sinf(a), ep->py + 10.0f, ep->pz + len * cosf(a) };
        n += !bhCollisionCheckLine2(&p1, &p2, 0x4400, (char)ep->flr_no);
    }
    return n;
}

/* a look of kind `kd` for a record of type `id` model `ver`: not the record's own, unused in this room if any is left */
static const ActorModel* pick_look(int kd, int id, int ver, const unsigned char* used)
{
    int j, n = 0, pass, pick;
    for (pass = 0; pass < 2 && !n; pass++)
        for (j = 0; j < spawn_base_count(); j++) {
            const ActorModel* m = spawn_entry(j);
            n += look_ok(m, kinds[kd].id) && !(m->id == id && m->mdlver == ver) && (pass || !used[j]);
        }
    if (!n) return NULL;
    pick = (int)(rnd() % (unsigned)n);
    for (j = 0; j < spawn_base_count(); j++) {
        const ActorModel* m = spawn_entry(j);
        if (look_ok(m, kinds[kd].id) && !(m->id == id && m->mdlver == ver) && (pass > 1 || !used[j]) && pick-- == 0) return m;
    }
    return NULL;
}

/* Records change at the room load (docs/ENEMIES.md "Randomizer"): free ones (and, with "event enemies", those the
   script only shows or hides) to another kind; those an event drives (and those whose HP the script reads) to another
   look of their own type, so the event's motions, AI states and HP tests still fit. Bosses and their arenas, NPCs,
   water and wall types, props stay. Every record keeps its slot and kill bit; any refusal keeps the original.
   The choices depend only on the options, the room file and its collision (the same on both co-op sides). */
static void randomize(const EnemyOpts* o)
{
    int k, j, cost = 0, made = 0, nc = 0, guest = coop_is_guest(), room = room_file_now(), wild = o->variety != 0;
    Kind kt[KINDS + VARIANT_MAX];
    int nk = make_kinds(kt), kused[KINDS + VARIANT_MAX] = { 0 }, room_kinds[2] = { -1, -1 }, heavy = 0;
    unsigned char held[128] = { 0 }, used[256] = { 0 }, cls[128] = { 0 };   /* cls: 1 another kind, 2 another look */
    char line[600];
    int ln = 0;
    for (k = j = 0; k < kept_n; k++)   /* guest: records the host kept (this room's notes are used up by this load) */
        if (guest && kept[k].room == room) held[kept[k].rec] = 1;
        else kept[j++] = kept[k];
    kept_n = j;
    for (k = 0; k < rom->ene_n && k < 128; k++) {
        BH_PWORK* ep = &ene[k];
        if (!(ep->flg & 0x1)) continue;
        if (boss_arena(ep->id)) { host_log("enemies: room %04d not randomized (boss %d)\n", room, ep->id); return; }
        cost += slot_cost(ep->id);   /* (all records: a change adds its difference, so a room never exceeds its own count) */
        if (!ground_record(ep->id) && !(wild && flier_record(ep->id))) continue;
        if (!rec_flags[k]) cls[k] = 1;
        else if (o->events) cls[k] = rec_flags[k] & (REC_EVENT | REC_HP) ? 2 : 1;
        if (cls[k] == 1 && ep->flr_no < 0) cls[k] = 0;   /* (no floor under it: one hanging somewhere for an event) */
        nc += cls[k] != 0;
    }
    if (!nc) return;
    if (!wild) {   /* Mild: up to two kinds for the room's free records */
        rnd_seed((unsigned)o->seed, (unsigned)room * 977u + 0x3000u);
        for (j = 0; j < 2; j++) {
            int t = 0, r;
            for (k = 0; k < nk; k++) t += k != room_kinds[0] && !(kt[k].flags & K_NOCOUNT && all_dead_check) ? kt[k].w[0] : 0;
            for (r = (int)(rnd() % (unsigned)t), k = 0; k < nk; k++) {
                int w = k != room_kinds[0] && !(kt[k].flags & K_NOCOUNT && all_dead_check) ? kt[k].w[0] : 0;
                if (r < w) break;
                r -= w;
            }
            room_kinds[j] = k;
        }
    }
    for (k = 0; k < rom->ene_n && k < 128; k++) {
        BH_PWORK save = ene[k];
        SpawnNote n = { 0 };
        const ActorModel* m = NULL;
        const EnemyVariant* v = NULL;
        const char* why;
        int i, kd = -1, kcost;
        if (!cls[k]) continue;
        rnd_seed((unsigned)o->seed, (unsigned)room * 977u + 0x3000u + (unsigned)k * 7919u);   /* each record its own draws */
        if (cls[k] == 2) {   /* another look of its type */
            for (kd = 0; kd < KINDS && kinds[kd].id != save.id; kd++);
            if (kd < KINDS) m = pick_look(kd, save.id, save.mdlver, used);
        } else {
            int w[KINDS + VARIANT_MAX], t = 0, fresh = 0, r, big = -1, mine = 0;
            for (i = 0; i < nk; i++) {
                int f = kt[i].flags;
                w[i] = kt[i].w[wild];
                if (!(f & K_FLIER) != !flier_record(save.id)) w[i] = 0;
                if ((f & K_NOCOUNT) && all_dead_check) w[i] = 0;
                if ((f & K_RMFLG) && rm_flg_used) w[i] = 0;
                if ((f & K_HEAVY) && heavy) w[i] = 0;
                if (cost - slot_cost(save.id) + kt[i].cost > PC_ENE_SLOTS - SPAWN_SLOTS_FREE) w[i] = 0;
                if (w[i] && (f & K_BIG)) {
                    if (big < 0) big = open_dirs(&save, 35.0f);
                    if (big < 5) w[i] = 0;
                }
                mine |= w[i] && (i == room_kinds[0] || i == room_kinds[1]);
            }
            for (i = 0; i < nk; i++) {
                if (mine && i != room_kinds[0] && i != room_kinds[1]) w[i] = 0;   /* Mild: the room's two kinds, if one fits */
                fresh |= w[i] && !kused[i];
            }
            for (i = 0; i < nk; i++) {   /* each kind once while unused ones are left */
                if (fresh && kused[i]) w[i] = 0;
                t += w[i];
            }
            if (t) {
                for (r = (int)(rnd() % (unsigned)t), kd = 0; kd < nk && r >= w[kd]; kd++) r -= w[kd];
                if ((v = kt[kd].v)) m = variant_base(v);   /* an enemy variant: its base's entry */
                else m = pick_look(kd, save.id, save.mdlver, used);
            }
        }
        if (!m) {   /* its own kind drawn, without another look (Cerberus, bat), or nothing else fits */
            if (kd >= 0 && kd < nk) kused[kd] = 1;
            host_log("enemies: record %d stays type %d model %d (%s)\n", k, save.id, save.mdlver, cls[k] == 2 ? "event enemy, no other look of its type"
                     : kd >= 0 && kd < nk && kt[kd].id == save.id ? "its own kind drawn" : "nothing else fits");
            continue;
        }
        for (j = 0; j < spawn_base_count() && spawn_entry(j) != m; j++);
        used[j] = 1;
        kused[kd] = 1;
        kcost = kd >= 0 && kd < nk ? kt[kd].cost : slot_cost(m->id);
        if (held[k]) {   /* as the host's: the original */
            host_log("enemies: record %d stays %d (as the host's)\n", k, save.id);
            continue;
        }
        n.room = m->room; n.rec = m->rec; n.id = m->id; n.mdlver = m->mdlver;
        n.type = m->id == save.id ? save.type : 0;   /* the same type keeps its variant (an event's or a lying zombie's) */
        n.variant = v ? v->id : 0;
        n.px = save.px; n.py = save.py; n.pz = save.pz; n.ay = save.ay; n.flr = save.flr_no;
        ene[k].flg = 0;
        why = spawn_at(m, &n, k, guest ? SPAWN_LOADING | SPAWN_DICTATED : SPAWN_LOADING, NULL);
        if (why) {   /* softlock-safe fallback: the original record as it was */
            SpawnNote kn = { (unsigned short)room, (unsigned char)k, (unsigned char)save.id, (unsigned char)save.mdlver, (unsigned char)save.type,
                             save.flr_no, SPAWN_NOTE_KEPT, save.px, save.py, save.pz, (short)save.ay, 0 };
            ene[k] = save;
            bhCheckEneWorkNum();
            host_log("enemies: record %d stays %d (%s)%s\n", k, save.id, why, guest ? "; the host's may differ" : "");
            if (!guest) coop_rand_kept(k, &kn);   /* the guest keeps it too */
            else cost += kcost - slot_cost(save.id), heavy += v && v->heavy;   /* the host's budget (it made the change), so the next draws stay its */
            continue;
        }
        for (i = 0; i < save.mdl_n; i++)   /* the original's textures are released with the room's (the record now holds the new type) */
            if ((save.mdl[i].flg & 0x200) && save.mdl[i].texP) spawn_keep_texlist(save.mdl[i].texP);
        rand_src[k] = m;   /* (the room file still says the old type: extra enemies copy from here) */
        rand_var[k] = v ? v->id : 0;
        cost += kcost - slot_cost(save.id);
        heavy += v && v->heavy;
        made++;
        if (v) host_log("enemies: room %04d record %d: type %d model %d -> %s (variant %d: type %d model %d)\n", room, k, save.id, save.mdlver, v->name, v->id, m->id, m->mdlver);
        else host_log("enemies: room %04d record %d: type %d model %d -> %s (type %d model %d)%s\n", room, k, save.id, save.mdlver, m->name, m->id, m->mdlver,
                      cls[k] == 2 ? ", event enemy: same type" : "");
    }
    for (k = 0; k < rom->ene_n && k < 128 && ln < (int)sizeof(line) - 16; k++)
        if ((ene[k].flg & 0x1) && ene[k].id < 31) ln += snprintf(line + ln, sizeof(line) - ln, " %d:%d.%d", k, ene[k].id, ene[k].mdlver);
    host_log("enemies: room %04d: %d of %d records randomized (seed %d, %s%s), now%s\n", room, made, nc, o->seed, wild ? "wild" : "mild",
             o->events ? ", event enemies" : "", line);
}

/* ---------------------------------------------------------------- the room load (room.c bhFinishRoom) */
void enemy_room_finished(void)
{
    const EnemyOpts* o;
    enemy_room_gen++;
    variant_room();   /* (the slots' enemy variants) */
    if (!rom || !sys) return;
    o = enemy_opts();
    cast_room(o);   /* the cutscene cast's models (cast_glue.c; it touches no record) */
    if (!o->place && !o->rand && !o->extra) return;
    scan_script();
    memset(rand_src, 0, sizeof(rand_src));
    memset(rand_var, 0, sizeof(rand_var));
    spot_player = 0;
    if (o->rand) randomize(o);
    if (o->place) place(o);
    spot_player = 1;
}

/* ---------------------------------------------------------------- (a) extra enemies */
/* types that roam freely and die like any enemy */
static int extra_type(int id) { return id == 1 || id == 3 || id == 4 || id == 5 || id == 9 || id == 22 || id == 26; }

/* Test aid RECVX_ENEMY_KILL=n: from n game frames into each room, every 20 frames each living enemy of the room's
   records (ids 1..30, in its AI, shown) takes a heavy hit as from the player's weapon (weapon.c bhCheckGunAtari: hit
   flag, joint, damage); logs each death (flg & 2: what the script's DieCk turns into its kill bit) and, at the next
   room, how many of the room's enemies died. */
static void kill_test(void)
{
    static int n = -2, gen = -1, t, hit[128], died[128], room;
    int k, h = 0, d = 0;
    if (n == -2) n = getenv("RECVX_ENEMY_KILL") ? atoi(getenv("RECVX_ENEMY_KILL")) : -1;
    if (n < 0 || !sys || !plp || !rom || (sys->gm_flg & 0x9) || (sys->cb_flg & 0x1)) return;
    if (gen != enemy_room_gen) {
        for (k = 0; k < 128; k++) { h += hit[k]; d += died[k]; }
        if (h) printf("test: room %04d: %d of %d enemies hit died\n", room, d, h);
        gen = enemy_room_gen; t = 0; room = room_file_now();
        memset(hit, 0, sizeof(hit)); memset(died, 0, sizeof(died));
    }
    if (++t < n) return;
    for (k = 0; k < rom->ene_n && k < 128; k++) {
        BH_PWORK* ep = &ene[k];
        if (!(ep->flg & 0x1) || ep->id < 1 || ep->id > 30) continue;
        if ((ep->flg & 0x2) && hit[k] && !died[k]) { died[k] = 1; printf("test: room %04d record %d (type %d) died after %d frames\n", room, k, ep->id, t - n); }
        if ((ep->flg & 0x2) || (ep->stflg & 0x1000000) || ep->mode0 == 5 || (t - n) % 20) continue;
        hit[k] = 1;
        ep->flg |= 0x4;
        ep->dpx = ep->px; ep->dpy = ep->py + 10.0f; ep->dpz = ep->pz;
        ep->wpnr_no = plp->wpnr_no;
        ep->djnt_no = ep->mlwP && ep->mlwP->obj_num > 1 ? 1 : 0;
        ep->dam[ep->djnt_no] += 500;
        ep->comb_flg |= 0x10;
    }
}

/* Test aid RECVX_HEADSHOT=n: n game frames into each room, every living zombie in its AI (records and added ones) takes
   one head hit as from the magnum (weapon 6, item 32; en01.c bhEne01_CheckExpHead: joint 11), the hit that blows the head off;
   then its head state (exp0 + 0x78, -1: blown off), hp and modes are logged every 15 frames for 90 frames. */
static void headshot_test(void)
{
    static int n = -2, gen = -1, t;
    int k;
    if (n == -2) n = getenv("RECVX_HEADSHOT") ? atoi(getenv("RECVX_HEADSHOT")) : -1;
    if (n < 0 || !sys || !plp || (sys->gm_flg & 0x9) || (sys->cb_flg & 0x1)) return;
    if (gen != enemy_room_gen) { gen = enemy_room_gen; t = 0; }
    if (++t < n || t > n + 90) return;
    for (k = 0; k < (int)sys->ewk_n && k < PC_ENE_SLOTS; k++) {
        BH_PWORK* ep = &ene[k];
        if (!(ep->flg & 0x1) || ep->id != 1 || (ep->flg & 0x80) || !ep->exp0) continue;
        if (t == n && !(ep->flg & 0x2) && !(ep->stflg & 0x1000000) && ep->mode0 == 1) {
            ep->flg |= 0x4;
            ep->dpx = ep->px; ep->dpy = ep->py + 16.0f; ep->dpz = ep->pz;
            ep->wpnr_no = 6;
            ep->djnt_no = 11;
            ep->dam[11] += 30;
        }
        if ((t - n) % 15 == 0)
            printf("test: headshot room %04d slot %d model %d type %d: head %d hp %d mode %d/%d/%d flg %#x\n", room_file_now(), k, ep->mdlver, ep->type,
                   *(int*)(ep->exp0 + 0x78), ep->hp, ep->mode0, ep->mode1, ep->mode2, ep->flg);
    }
}

void enemy_extras_frame(void)
{
    static int done_gen = -1;
    const EnemyOpts* o;
    int cand[128], nc = 0, k, made = 0;
    kill_test();
    headshot_test();
    if (done_gen == enemy_room_gen || !sys || !plp || !rom) return;
    o = enemy_opts();
    if (!o->extra || coop_is_guest()) { done_gen = enemy_room_gen; return; }
    if (!(sys->tk_flg & 0x80) || !(sys->sp_flg & 0x2) || (sys->gm_flg & 0x9) || (sys->cb_flg & 0x1)) return;   /* the room runs */
    done_gen = enemy_room_gen;
    for (k = 0; k < rom->ene_n && k < 128; k++) {
        BH_PWORK* ep = &ene[k];
        if (!(ep->flg & 0x1) || (ep->flg & 0x2) || (ep->stflg & 0x1000000) || ep->mode0 == 5 || !extra_type(ep->id) || rec_flags[k]) continue;
        cand[nc++] = k;
    }
    rnd_seed((unsigned)o->seed, (unsigned)room_file_now() * 7u + 1u);
    for (k = nc - 1; k > 0; k--) { int j = rnd() % (k + 1), t = cand[k]; cand[k] = cand[j]; cand[j] = t; }   /* shuffle */
    for (k = 0; k < nc && made < o->extra; k++) {
        const ETTY_WORK* r = &rom->enep[cand[k]];
        BH_PWORK* src = &ene[cand[k]];
        ActorModel am = { "extra enemy", (unsigned short)room_file_now(), (unsigned char)cand[k], (unsigned char)r->id, r->mdlver, 0, 0 };
        SpawnNote n = { 0 };
        const char* why;
        if (rand_src[cand[k]]) { am = *rand_src[cand[k]]; n.variant = rand_var[cand[k]]; }   /* a randomized record: a copy of its new type (and variant) */
        n.room = am.room; n.rec = am.rec; n.id = am.id; n.mdlver = am.mdlver;
        n.py = src->py; n.flr = src->flr_no; n.ay = (short)(rnd() & 0xFFFF);
        if (!find_spot(src->px, src->py, src->pz, src->flr_no, 15.0f, 35.0f, &n.px, &n.pz)) continue;
        why = spawn_add(&am, &n, NULL);
        host_log("enemies: extra %s (type %d model %d, from record %d) at %.0f %.0f: %s\n", am.name, am.id, am.mdlver, am.rec, n.px, n.pz, why ? why : "spawned");
        if (!why) made++;
        else if (strstr(why, "slots") || strstr(why, "memory")) break;
    }
    if (nc) host_log("enemies: room %04d: %d extra enemies (of %d eligible records, option %d)\n", room_file_now(), made, nc, o->extra);
}
