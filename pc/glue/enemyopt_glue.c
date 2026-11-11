/* Enemy options (docs/ENEMIES.md "Enemy options"): (a) extra enemies (copies of the room's records, or kinds it lacks
 * under the randomizer's Types per Room),
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
#include "../src/host/placer.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void host_log(const char* fmt, ...);
int coop_is_guest(void);
const char* spawn_add(const ActorModel* am, const SpawnNote* n, int want, int* slot_out);
const char* spawn_at(const ActorModel* am, const SpawnNote* n, int want, int mode, int* slot_out);
const ActorModel* spawn_entry(int i);
int spawn_base_count(void);
const char* spawn_refused(int id);
const ActorModel* variant_base(const EnemyVariant* v);   /* variant_glue.c */
void variant_room(void);
void cast_room(const EnemyOpts* o);   /* cast_glue.c */
void spawn_keep_texlist(const NJS_TEXLIST* l);
void bhCheckEneWorkNum(void);   /* eneset.h */
void bhStFlg(unsigned int* flg, unsigned int bit);   /* flag.h */
int bhCkFlg(unsigned int* flg, unsigned int bit);
int bhCheckFloorNum(float py);   /* pwksub.h */

EnemyOpts g_enemy_opts = { .variety = 1, .events = 1, .cast_enemy = CAST_ENEMY_ALL };   /* settings (F1, recvx.ini) */
_Static_assert(sizeof(g_enemy_opts.cast_enemy_off) == CAST_ENEMY_BYTES, "EnemyOpts.cast_enemy_off");
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
    if ((e = getenv("RECVX_ENEMY_TYPES"))) g_enemy_opts.types = atoi(e);   /* (RECVX_ENEMY_RATES: enemy_rates_load) */
    if ((e = getenv("RECVX_CAST"))) g_enemy_opts.cast = atoi(e) != 0;   /* the cutscene cast: 0/1, then ":all" / ":random" / ":imports" */
    if (e && strstr(e, "all")) g_enemy_opts.cast_all = 1;
    if (e && strstr(e, "custom")) g_enemy_opts.cast_all = 2;   /* (the models: recvx.ini cast_custom_off) */
    if (e && strstr(e, "random")) g_enemy_opts.cast_assign = 1;
    if (e && strstr(e, "imports")) g_enemy_opts.cast_imports = 1;
    if ((e = getenv("RECVX_CAST_ENEMY"))) g_enemy_opts.cast_enemy = atoi(e) & 3;   /* "Enemy Models": 0 Same as Cast, 1 All Enemies, 2 Custom, 3 Off */
    if ((e = getenv("RECVX_VOICES"))) g_enemy_opts.voices = atoi(e) != 0;   /* "Randomize Voice Lines" */
}

const EnemyOpts* enemy_opts(void)
{
    static const EnemyOpts none;
    if (coop_is_guest()) return have_host_opts ? &host_opts : &none;
    return &g_enemy_opts;
}

static int kept_n;
static const char* variety_name(int v) { return v == 2 ? "custom" : v ? "wild" : "mild"; }
static char rates_ini[512];   /* recvx.ini enemy_rates (below: enemy_rates_ini) */
static const char* rates_line(const unsigned char* rate, char* t);
void enemy_opts_from_host(const EnemyOpts* o)
{
    have_host_opts = o != NULL;
    if (!o) { kept_n = 0; memset(host_off, 0, sizeof(host_off)); }   /* (enemy_rand_kept, EV_CASTOFF: the host's go with its session) */
    if (o) {
        char rl[sizeof(rates_ini)];
        host_opts = *o;
        host_log("enemies: the host's options: extra %d, placements %d, randomizer %d (%s, types per room %d%s, rates %s), seed %d, cutscene cast %d (%s, %s%s, enemy models %d, "
                 "unchecked %02x%02x%02x), voice lines %d\n",
                 o->extra, o->place, o->rand, variety_name(o->variety), o->types, o->events ? ", event enemies" : "", rates_line(o->rate, rl), o->seed, o->cast,
                 o->cast_all == 2 ? "custom models" : o->cast_all ? "all models" : "cutscene models", o->cast_assign ? "random" : "fixed", o->cast_imports ? ", imported models" : "",
                 o->cast_enemy, o->cast_enemy_off[2], o->cast_enemy_off[1], o->cast_enemy_off[0], o->voices);
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
int room_file_now(void) { return sys->stg_no * 1000 + sys->rom_no * 10 + sys->rcase; }

/* what the room script does with each record (pc/tools/enemies.py classifies the same way) */
enum { REC_EVENT = 1, REC_SHOWN = 2, REC_HP = 4 };
static unsigned char rec_flags[128];
static short rec_kill[128];             /* each record's kill bit as the entry script hides it by (EneSetCk; -1: none, it comes back on every visit) */
static const ActorModel* rand_src[128];   /* randomized records: where their new type comes from */
static unsigned short rand_var[128];    /* and which enemy variant they became (0: none) */
static unsigned char moved_rec[128];    /* records "Rearranged Enemies" moved (RECVX_ENEMY_FLOORS) */
static int all_dead_check;              /* the script counts kills (DieEventCk): "all dead" doors */
static int rm_flg_used;                 /* the script tests or sets rm_flg bits 29..31 (= 0x4, 0x2, 0x1), which some types write */

static void scan_script(void)
{
    unsigned char* base = (unsigned char*)rom->evtp;
    unsigned int* tab = (unsigned int*)base;
    unsigned int offs[256], first, o, end;
    int n = 0, i, j;
    memset(rec_flags, 0, sizeof(rec_flags));
    memset(rec_kill, 0xFF, sizeof(rec_kill));
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
            case 34: if (rec_kill[p[1] & 127] < 0) rec_kill[p[1] & 127] = (short)((p[2] | p[3] << 8) & 0x1FF); break;   /* EneSetCk rec, bit */
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

static int near_entry(float x, float z)
{
    int i;
    for (i = 0; i < rom->pos_n; i++) {
        float dx = x - rom->posp[i].px, dz = z - rom->posp[i].pz;
        if (dx * dx + dz * dz < 40.0f * 40.0f) return 1;
    }
    return 0;
}

/* the ground under (x, z) seen from 10 units above y (hitchk.c bhGetGroundPosition: the highest box top, slope or floor
   slab at most that high; -1000: none, as on the lowest floor of a room without floor slabs); *hit (if not NULL): the box
   it is the top of, NULL for none */
static float ground_at(float x, float y, float z, const ATR_WORK** hit)
{
    NJS_POINT3 q = { x, y + 10.0f, z };
    ATR_WORK* htp = sys->htp;
    float g = bhGetGroundPosition(&q);
    if (hit) *hit = sys->htp;
    sys->htp = htp;
    return g;
}
/* a slope (type 6 box: stairs, ramps), where an enemy's wall check pushes it along the slope at its own height (seen with a
   Cerberus) */
static int slope(const ATR_WORK* hp) { return hp && hp->type == 6; }

/* the record stands on the ground there (as find_spot wants a walker's spot; slopes too: the game put it there) */
static int on_ground(const BH_PWORK* ep)
{
    float g = ground_at(ep->px, ep->py, ep->pz, NULL);
    return g <= -999.0f || fabsf(g - ep->py) <= 1.0f;
}

/* no drop within 6 units of a walker's spot along x and z: ground there no more than a unit below y (none only where the
   source had none). Nearer an edge, a Hunter put on RM_2011's top balcony leapt off it and stayed at the balcony's height,
   and a Cerberus on RM_9310's platform was pushed off it. `rise`: no ground more than a unit above y there either (a crate,
   a step), for an extra of another kind than its record (extra_kind): a Cerberus put by a crate in RM_2031 stood inside it
   the next frame. */
static int no_drop(float x, float y, float z, float g0, int rise)
{
    static const signed char d[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    int i;
    for (i = 0; i < 4; i++) {
        float g = ground_at(x + 6.0f * d[i][0], y, z + 6.0f * d[i][1], NULL);
        if (g > -999.0f ? g < y - 1.0f || (rise && g > y + 1.0f) : g0 > -999.0f) return 0;
    }
    return 1;
}

/* a free spot near (x, z) on floor `flr` at height y: no wall between it and the spot (plus a body's width beyond), away
   from the entry positions; 1 = found (*ox, *oy, *oz). A walker (`walk`) must stand there: ground under the spot within a
   unit of y on the same floor number and not a slope (*oy: that ground), or none where the source has none either (the
   lowest floor of a room without floor slabs), and no drop near it; spots over a pool, a pit or stairs, at or past a
   ledge or in a crate are skipped. A flier keeps y. Only the room file and the draws decide it (not where the player
   stands), so a room gets the same spots on every visit; a skipped spot uses the draws it used before. */
static int find_spot(float x, float y, float z, int flr, int walk, float rmin, float rmax, float* ox, float* oy, float* oz)
{
    int k, start = rnd() & 7;
    float g0 = walk ? ground_at(x, y, z, NULL) : 0;
    const ATR_WORK* hp;
    for (k = 0; k < 16; k++) {
        float a = (float)((start + k * 3) & 15) * (6.2831853f / 16.0f);
        float r = rmin + (rmax - rmin) * (float)(rnd() & 1023) / 1023.0f;
        float tx = x + r * sinf(a), tz = z + r * cosf(a), g = -1000.0f;
        NJS_POINT3 p1 = { x, y + 10.0f, z }, p2 = { x + (r + 8.0f) * sinf(a), y + 10.0f, z + (r + 8.0f) * cosf(a) };
        if (near_entry(tx, tz)) continue;
        if (bhCollisionCheckLine2(&p1, &p2, 0x4400, (char)flr)) continue;
        if (walk && ((g = ground_at(tx, y, tz, &hp)) > -999.0f ? slope(hp) || fabsf(g - y) > 1.0f || bhCheckFloorNum(g + 0.5f) != bhCheckFloorNum(y + 0.5f)
                                                          : g0 > -999.0f)) continue;
        if (walk && !no_drop(tx, y, tz, g0, 0)) continue;
        *ox = tx; *oy = g > -999.0f ? g : y; *oz = tz;
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- (b) rearranged placements */
static int flier_record(int id) { return id == 7 || id == 11; }   /* bats and Spotters: they hover where they are placed */
static int roams(int id) { return id == 1 || id == 3 || id == 4 || id == 5 || id == 7 || id == 9 || id == 26; }

static void place(const EnemyOpts* o)
{
    int k, cand[128], nc = 0, moved = 0;
    for (k = 0; k < rom->ene_n && k < 128; k++)
        if ((ene[k].flg & 0x1) && !(rec_flags[k] & (REC_EVENT | REC_SHOWN)) && roams(ene[k].id)) cand[nc++] = k;
    for (k = 0; k < nc; k++) {
        BH_PWORK* ep = &ene[cand[k]];
        ETTY_WORK* r = &rom->enep[cand[k]];
        float x = ep->px, z = ep->pz, nx, ny, nz;
        rnd_seed((unsigned)o->seed, (unsigned)room_file_now() * 131u + (unsigned)cand[k] + 0x5000u);
        if (nc > 1 && (rnd() & 1)) {   /* start from another record's spot on the same floor */
            BH_PWORK* q = &ene[cand[rnd() % nc]];
            if (q->flr_no == ep->flr_no && fabsf(q->py - ep->py) < 1.0f) { x = q->px; z = q->pz; }
        }
        if (!find_spot(x, ep->py, z, ep->flr_no, !flier_record(ep->id), 8.0f, 30.0f, &nx, &ny, &nz)) continue;
        ep->px = ep->pxb = r->px = nx;
        ep->py = ep->pyb = r->py = ny;
        ep->pz = ep->pzb = r->pz = nz;
        ep->ay = r->ay = (short)(rnd() & 0xFFFF);
        if (ep->id == 4 && ep->type) ep->type = r->type = 0;   /* a Cerberus waiting in its wake-up area would wait forever */
        bhCalcModel(ep);
        moved_rec[cand[k]] = 1;
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
   unused ones in this room first). Weights per variety (0 Mild, 1 Wild; 2 Custom: EnemyOpts.rate); 0: not in that variety. */
enum { K_BIG = 1,       /* needs open space around the record (not in corridors or small rooms) */
       K_FLIER = 2,     /* only for flier records (bats, Spotters), which hover where they are placed */
       K_RMFLG = 4,     /* its code writes rm_flg bits 0..2 (Tyrant clears them every frame): not where the script uses them */
       K_NOCOUNT = 8,   /* not where the doors count the kills: it may leave the room, fly out of reach or take too long */
       K_HEAVY = 16 };  /* an enemy variant marked heavy (boss-like): one per room */
/* look: the model version it is (-1: every look of the type). The Hunter and the Sweeper are two kinds in every variety
   (Types per Room counts them as two); their Wild and Mild weights add up to the single Hunter kind's before the split. */
static const struct { unsigned char id, w[2], flags; signed char look; const char* name; } kinds[] = {   /* (name: "Custom"'s list) */
    { 1, { 5, 5 }, 0, -1, "Zombie" },                       /* 24 looks */
    { 4, { 4, 4 }, 0, -1, "Cerberus" },
    { 9, { 4, 4 }, 0, -1, "Bandersnatch" },
    { 5, { 2, 2 }, K_NOCOUNT, 0, "Hunter" },                /* may leap out of a room */
    { 5, { 1, 2 }, K_NOCOUNT, 1, "Sweeper" },               /* (with the Hunter 3 / 4, the old single kind's) */
    { 3, { 2, 3 }, K_BIG | K_NOCOUNT, -1, "Black Widow" },  /* (with its brood 25 slots; climbs walls) */
    { 26, { 2, 2 }, 0, -1, "Anatomist Zombie" },
    { 19, { 0, 2 }, K_BIG | K_RMFLG | K_NOCOUNT, -1, "Tyrant" },   /* both looks */
    { 23, { 0, 1 }, K_BIG | K_RMFLG | K_NOCOUNT, -1, "Giant Black Widow" },
    { 7, { 0, 3 }, K_FLIER | K_RMFLG | K_NOCOUNT, -1, "Bat" },
    { 11, { 0, 2 }, K_FLIER | K_RMFLG | K_NOCOUNT, -1, "Spotter" },
};
#define KINDS (int)(sizeof(kinds) / sizeof(kinds[0]))
_Static_assert(KINDS < ENEMY_RATE_N && KINDS <= 32, "EnemyOpts.rate, randomize's off mask");

/* "Custom"'s rates (enemies.h): EnemyOpts.rate[i] for kinds[i], then rate[KINDS + j] for variant_at(j), listed when the
   variant has a randomizer weight. ponytail: variants past ENEMY_RATE_N - KINDS (21) have no rate and "Custom" never places
   them; more needs a co-op event of their own (EnemyOpts fills Event's union). */
static int vrate(int j) { return j < ENEMY_RATE_N - KINDS ? KINDS + j : -1; }
static int listed(int j) { const EnemyVariant* v = variant_at(j); return v && (v->wild || v->mild) && vrate(j) >= 0; }
int enemy_rate_count(void)
{
    int n = KINDS;
    for (int j = 0; j < variant_count(); j++) n += listed(j);
    return n;
}
int enemy_rate_at(int k)
{
    if (k < KINDS) return k;
    k -= KINDS;
    for (int j = 0; j < variant_count(); j++) if (listed(j) && !k--) return KINDS + j;
    return -1;
}
const char* enemy_rate_name(int i) { return i < KINDS ? kinds[i].name : variant_at(i - KINDS) ? variant_at(i - KINDS)->name : ""; }
void enemy_rate_preset(unsigned char* rate, int variety)
{
    int w = variety != 0;
    memset(rate, 0, ENEMY_RATE_N);
    for (int i = 0; i < KINDS; i++) rate[i] = kinds[i].w[w];
    for (int j = 0; j < variant_count() && vrate(j) >= 0; j++) {
        const EnemyVariant* v = variant_at(j);
        int r = w ? v->wild : v->heavy ? 0 : v->mild;   /* (as make_kinds) */
        rate[vrate(j)] = (unsigned char)(r > ENEMY_RATE_MAX ? ENEMY_RATE_MAX : r);
    }
}

/* recvx.ini enemy_rates: "id:rate" per base kind (its type id; "id.look:rate" for one look: 5.0 Hunter, 5.1 Sweeper; a pair
   without the look, as before they were split, sets every look's), "vID:rate" per variant; a kind the line lacks (new, or a
   mods file changed) keeps the preset of the file's Enemy Selection. Read before the variants are loaded, applied after. */
static int rates_ready;
void enemy_rates_ini(const char* text) { snprintf(rates_ini, sizeof(rates_ini), "%s", text); }
static void rates_apply(const char* t)
{
    int id, look, r, n, i, v;
    while (t && *t) {
        v = *t == 'v';
        look = -1;
        if (sscanf(t + v, "%d.%d:%d%n", &id, &look, &r, &n) != 3 && (look = -1, sscanf(t + v, "%d:%d%n", &id, &r, &n) != 2)) break;
        t += v + n;
        t += strspn(t, " ");
        if (r < 0 || r > ENEMY_RATE_MAX) continue;
        for (i = 0; !v && i < KINDS; i++) if (kinds[i].id == id && (look < 0 || kinds[i].look == look)) g_enemy_opts.rate[i] = (unsigned char)r;
        for (i = 0; v && i < variant_count() && vrate(i) >= 0; i++) if (variant_at(i)->id == id) g_enemy_opts.rate[vrate(i)] = (unsigned char)r;
    }
}
void enemy_rates_load(void)
{
    enemy_rate_preset(g_enemy_opts.rate, g_enemy_opts.variety);   /* (Custom without a line: Wild's) */
    rates_apply(rates_ini);
    rates_apply(getenv("RECVX_ENEMY_RATES"));   /* test aid, as the ini line */
    rates_ready = 1;
}
/* into t (sizeof(rates_ini) bytes): its own buffer for each caller, as the game thread logs the host's rates
   while config_save (the main thread) writes ours */
static const char* rates_line(const unsigned char* rate, char* t)
{
    int n = 0;
    for (int i = 0; i < KINDS; i++)
        n += kinds[i].look < 0 ? snprintf(t + n, sizeof(rates_ini) - n, "%s%d:%d", n ? " " : "", kinds[i].id, rate[i])
                               : snprintf(t + n, sizeof(rates_ini) - n, "%s%d.%d:%d", n ? " " : "", kinds[i].id, kinds[i].look, rate[i]);
    for (int j = 0; j < variant_count() && vrate(j) >= 0 && n < (int)sizeof(rates_ini) - 16; j++)
        if (listed(j)) n += snprintf(t + n, sizeof(rates_ini) - n, " v%d:%d", variant_at(j)->id, rate[vrate(j)]);
    return t;
}
const char* enemy_rates_text(void)   /* (saved before the load: the file's) */
{
    static char t[sizeof(rates_ini)];
    return rates_ready ? rates_line(g_enemy_opts.rate, t) : rates_ini;
}
/* the kinds a room's records can become: those above, then the enemy variants with a randomizer weight
   (docs/ENEMIES.md "Enemy variants"): a variant takes its base kind's rules (another type's: not where doors
   count kills, nor where the script uses rm_flg bits), plus big, heavy and the stages it may appear in. The
   same on both co-op sides as long as both have the same variants (built in; mods/enemies.ini). */
typedef struct { unsigned char id, w[2], flags, cost; signed char ri, look; const EnemyVariant* v; } Kind;   /* ri: its rate (-1: none) */
static int make_kinds(Kind* kd)
{
    int n = 0, i, j;
    for (i = 0; i < KINDS; i++) { kd[n].id = kinds[i].id; kd[n].w[0] = kinds[i].w[0]; kd[n].w[1] = kinds[i].w[1]; kd[n].flags = kinds[i].flags;
                                  kd[n].cost = (unsigned char)slot_cost(kinds[i].id); kd[n].ri = (signed char)i; kd[n].look = kinds[i].look; kd[n++].v = NULL; }
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
        kd[n].ri = (signed char)vrate(i);
        kd[n++].v = v;
    }
    return n;
}
static int ground_record(int id) { return id == 1 || id == 3 || id == 4 || id == 5 || id == 9; }
static int look_ok(const ActorModel* m, int id) { return m->id == id && !(id == 9 && m->mdlver == 1); }   /* (Bandersnatch 2 is an event's arm) */
static int kind_of(const Kind* kt, int id, int ver) { for (int i = 0; i < KINDS; i++) if (kt[i].id == id && (kt[i].look < 0 || kt[i].look == ver)) return i; return -1; }

/* directions (of 8) with no wall within `len` units of (x, z) on floor flr, at waist height */
static int open_dirs(float x, float y, float z, int flr, float len)
{
    int k, n = 0;
    for (k = 0; k < 8; k++) {
        float a = (float)k * (6.2831853f / 8.0f);
        NJS_POINT3 p1 = { x, y + 10.0f, z }, p2 = { x + len * sinf(a), y + 10.0f, z + len * cosf(a) };
        n += !bhCollisionCheckLine2(&p1, &p2, 0x4400, (char)flr);
    }
    return n;
}

/* kind i's weight for a record of type id at (x, y, z) on floor flr, with `cost` slots taken by the rest of the room and
   `heavy` heavy variants in it, `counted` where the doors count its kill (0: it does not fit there); *big: open_dirs
   there, -1 until a big kind needs it */
static int kind_w(const EnemyOpts* o, const Kind* kt, int i, int id, float x, float y, float z, int flr, int cost, int heavy, int counted, int* big)
{
    int f = kt[i].flags, w = o->variety != 2 ? kt[i].w[o->variety != 0] : kt[i].ri >= 0 ? o->rate[kt[i].ri] : 0;
    if (!(f & K_FLIER) != !flier_record(id)) return 0;
    if ((f & K_NOCOUNT) && counted) return 0;
    if ((f & K_RMFLG) && rm_flg_used) return 0;
    if ((f & K_HEAVY) && heavy) return 0;
    if (cost + kt[i].cost > PC_ENE_SLOTS - SPAWN_SLOTS_FREE) return 0;
    if (w && (f & K_BIG)) {
        if (*big < 0) *big = open_dirs(x, y, z, flr, 35.0f);
        if (*big < 5) return 0;
    }
    return w;
}

/* a look of type `type` (model `look`, -1 any) for a record of type `id` model `ver`: not the record's own, nor one of the
   kinds in `off` (bits of kt indices), unused in this room if any is left */
static int look_fits(const Kind* kt, const ActorModel* m, int type, int look, unsigned off, int id, int ver)
{
    int kd = kind_of(kt, m->id, m->mdlver);
    return look_ok(m, type) && (look < 0 || m->mdlver == look) && !(kd >= 0 && (off >> kd & 1)) && !(m->id == id && m->mdlver == ver);
}
static const ActorModel* pick_look(const Kind* kt, int type, int look, unsigned off, int id, int ver, const unsigned char* used)
{
    int j, n = 0, pass, pick;
    for (pass = 0; pass < 2 && !n; pass++)
        for (j = 0; j < spawn_base_count(); j++)
            n += look_fits(kt, spawn_entry(j), type, look, off, id, ver) && (pass || !used[j]);
    if (!n) return NULL;
    pick = (int)(rnd() % (unsigned)n);
    for (j = 0; j < spawn_base_count(); j++)
        if (look_fits(kt, spawn_entry(j), type, look, off, id, ver) && (pass > 1 || !used[j]) && pick-- == 0) return spawn_entry(j);
    return NULL;
}

/* Records change at the room load (docs/ENEMIES.md "Randomizer"): free ones (and, with "event enemies", those the
   script only shows or hides) to another kind; those an event drives (and those whose HP the script reads) to another
   look of their own type, so the event's motions, AI states and HP tests still fit. Bosses and their arenas, NPCs,
   water and wall types, props stay. Every record keeps its slot and kill bit; any refusal keeps the original.
   The choices depend only on the options, the room file and its collision (the same on both co-op sides).
   "Custom" draws each kind by its rate alone (Wild and Mild also take every unused kind first). "Types per Room" (o->types),
   counting the kinds of the records that stay first, then each record's in turn: in Wild and Custom a target, so while the
   room has fewer kinds a record draws among the kinds it lacks (by weight, of those that fit there); once it has that many
   (in Mild: a limit only), a record becomes one of them if one fits there, else another that fits (a bat can only become a
   flier). The room's kinds, slots and heavy variants are kept for its extra enemies (extra_kind). */
static void in_set(unsigned char* set, int* n, int kd) { if (kd >= 0 && !set[kd]) { set[kd] = 1; ++*n; } }
static struct { int gen, nset, cost, heavy; unsigned char set[KINDS + VARIANT_MAX]; } rk = { -1 };   /* randomize's result for room load gen */
static void rk_keep(const unsigned char* set, int nset, int cost, int heavy)
{
    rk.gen = enemy_room_gen; rk.nset = nset; rk.cost = cost; rk.heavy = heavy;
    memcpy(rk.set, set, sizeof(rk.set));
}

static void randomize(const EnemyOpts* o)
{
    int k, j, cost = 0, made = 0, nc = 0, guest = coop_is_guest(), room = room_file_now(), wild = o->variety != 0, custom = o->variety == 2;
    Kind kt[KINDS + VARIANT_MAX];
    int nk = make_kinds(kt), kused[KINDS + VARIANT_MAX] = { 0 }, room_kinds[2] = { -1, -1 }, heavy = 0, nset = 0;
    unsigned char held[128] = { 0 }, used[256] = { 0 }, cls[128] = { 0 }, set[KINDS + VARIANT_MAX] = { 0 };   /* cls: 1 another kind, 2 another look */
    char line[600];
    int ln = 0;
    unsigned off = 0;   /* Custom: the base kinds at Off (an event's record takes no look of those either) */
    for (k = 0; custom && k < KINDS; k++) off |= (unsigned)!o->rate[k] << k;
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
        if (cls[k] == 1 && ground_record(ep->id) && !on_ground(ep)) cls[k] = 2;   /* placed off the ground on purpose (RM_6090's Black Widow
                                                                                    under the floor): only another look of its type */
        nc += cls[k] != 0;
    }
    for (k = 0; k < rom->ene_n && k < 128; k++)   /* Types per Room: the kinds of the records that keep theirs */
        if ((ene[k].flg & 0x1) && cls[k] != 1) in_set(set, &nset, kind_of(kt, ene[k].id, ene[k].mdlver));
    if (!nc) { rk_keep(set, nset, cost, heavy); return; }
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
            unsigned eoff = off;
            for (i = 0; o->types && nset >= o->types && i < KINDS; i++) eoff |= (unsigned)!set[i] << i;   /* Types per Room, the room full: its kinds only */
            if ((kd = kind_of(kt, save.id, save.mdlver)) >= 0 && (m = pick_look(kt, save.id, -1, eoff & ~(1u << kd), save.id, save.mdlver, used)))
                kd = kind_of(kt, m->id, m->mdlver);   /* (a Hunter's look can be the Sweeper's kind; Custom: not while it is Off, nor past Types per Room) */
        } else {
            int w[KINDS + VARIANT_MAX], t = 0, fresh = 0, r, big = -1, mine = 0, mset = 0, target = o->types && wild && nset < o->types;
            for (i = 0; i < nk; i++) {
                w[i] = kind_w(o, kt, i, save.id, save.px, save.py, save.pz, save.flr_no, cost - slot_cost(save.id), heavy, all_dead_check, &big);
                mine |= w[i] && (i == room_kinds[0] || i == room_kinds[1]);
            }
            for (i = 0; i < nk; i++) {
                if (mine && i != room_kinds[0] && i != room_kinds[1]) w[i] = 0;   /* Mild: the room's two kinds, if one fits */
                mset |= w[i] && set[i];
            }
            for (i = 0; i < nk; i++) {
                if (o->types && nset >= o->types && mset && !set[i]) w[i] = 0;   /* Types per Room: the room's kinds, if one fits */
                fresh |= w[i] && (target ? !set[i] : !kused[i] && !custom);
            }
            for (i = 0; i < nk; i++) {   /* each kind once while unused ones are left (Types per Room's target: one the room lacks) */
                if (fresh && (target ? set[i] : kused[i])) w[i] = 0;
                t += w[i];
            }
            if (t) {
                for (r = (int)(rnd() % (unsigned)t), kd = 0; kd < nk && r >= w[kd]; kd++) r -= w[kd];
                if ((v = kt[kd].v)) m = variant_base(v);   /* an enemy variant: its base's entry */
                else m = pick_look(kt, kt[kd].id, kt[kd].look, 0, save.id, save.mdlver, used);
            }
        }
        if (!m) {   /* its own kind drawn, without another look (Cerberus, bat), or nothing else fits */
            if (kd >= 0 && kd < nk) kused[kd] = 1;
            in_set(set, &nset, kind_of(kt, save.id, save.mdlver));
            host_log("enemies: record %d stays type %d model %d (%s)\n", k, save.id, save.mdlver, cls[k] == 2 ? "event enemy or off the ground, no other look of its type"
                     : kd >= 0 && kd < nk && kt[kd].id == save.id ? "its own kind drawn" : "nothing else fits");
            continue;
        }
        for (j = 0; j < spawn_base_count() && spawn_entry(j) != m; j++);
        used[j] = 1;
        kused[kd] = 1;
        kcost = kd >= 0 && kd < nk ? kt[kd].cost : slot_cost(m->id);
        if (held[k]) {   /* as the host's: the original */
            in_set(set, &nset, kind_of(kt, save.id, save.mdlver));
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
            if (!guest) coop_rand_kept(k, &kn), in_set(set, &nset, kind_of(kt, save.id, save.mdlver));   /* the guest keeps it too */
            else cost += kcost - slot_cost(save.id), heavy += v && v->heavy, in_set(set, &nset, kd);   /* the host's budget and kinds (it made the
                                                                                                         change), so the next draws stay its */
            continue;
        }
        in_set(set, &nset, kd);
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
    rk_keep(set, nset, cost, heavy);
    for (k = 0; k < rom->ene_n && k < 128 && ln < (int)sizeof(line) - 16; k++)
        if ((ene[k].flg & 0x1) && ene[k].id < 31) ln += snprintf(line + ln, sizeof(line) - ln, " %d:%d.%d", k, ene[k].id, ene[k].mdlver);
    host_log("enemies: room %04d: %d of %d records randomized (seed %d, %s%s, %d kinds, limit %d%s), now%s\n", room, made, nc, o->seed, variety_name(o->variety),
             o->events ? ", event enemies" : "", nset, o->types, o->types && nset > o->types ? ": over it, records that could only be another kind" : "", line);
}

/* co-op ownership (coop_glue.c own_movable): a record the room's scripts act on - LoadWork, motion pauses, links,
   positioning, the type's work, its sounds (REC_EVENT) or its HP (REC_HP) - is never run by the guest's game: a script
   step on the host would act on a puppet and the guest's next snapshot undo it. Scanned once per room load (the scan
   reads the script only; also without the enemy options). */
int enemy_rec_scripted(int slot)
{
    static int gen = -1;
    if (!rom || !sys || slot < 0 || slot >= rom->ene_n || slot >= 128) return 0;
    if (gen != enemy_room_gen) { scan_script(); gen = enemy_room_gen; }
    return (rec_flags[slot] & (REC_EVENT | REC_HP)) != 0;
}

/* ---------------------------------------------------------------- the room load (room.c bhFinishRoom) */
void enemy_room_finished(void)
{
    const EnemyOpts* o;
    enemy_room_gen++;
    variant_room();   /* (the slots' enemy variants) */
    extras_room();    /* "Extra Item Boxes and Herbs": their walls and triggers (extras_glue.c) */
    if (!rom || !sys) return;
    o = enemy_opts();
    if (o->place || o->rand || o->extra) {
        scan_script();
        memset(rand_src, 0, sizeof(rand_src));
        memset(rand_var, 0, sizeof(rand_var));
        memset(moved_rec, 0, sizeof(moved_rec));
        if (o->rand) randomize(o);
        if (o->place) place(o);
    }
    cast_room(o);   /* the cutscene cast's models (cast_glue.c; it touches no record), after the randomizer: a record it gives another
                       look (an event's zombie: "Randomize Event Enemies") is no longer what the cast saw, and was drawn as itself */
}

/* ---------------------------------------------------------------- (a) extra enemies */
/* types that roam freely and die like any enemy */
static int extra_type(int id) { return id == 1 || id == 3 || id == 4 || id == 5 || id == 9 || id == 22 || id == 26; }

/* Types per Room in Wild and Custom (randomize): while the room has fewer kinds, an extra enemy at (x, y, z) on floor flr
   takes one it lacks, by weight among those that fit there (as a walker: no fliers; slots, rm_flg, open space; "all dead"
   doors count no extra), and the room counts it; NULL: none fits, or no target: the extra copies its record. Draws after
   its spot's. */
static const ActorModel* extra_kind(const EnemyOpts* o, float x, float y, float z, int flr, unsigned short* var)
{
    static const unsigned char none[256];   /* (pick_look's looks used: any) */
    Kind kt[KINDS + VARIANT_MAX];
    int nk, w[KINDS + VARIANT_MAX], t = 0, i, r, big = -1;
    const ActorModel* m;
    if (!o->rand || !o->types || !o->variety || rk.gen != enemy_room_gen || rk.nset >= o->types) return NULL;
    nk = make_kinds(kt);
    for (i = 0; i < nk; i++) t += w[i] = rk.set[i] ? 0 : kind_w(o, kt, i, 1, x, y, z, flr, rk.cost, rk.heavy, 0, &big);
    if (!t) return NULL;
    for (r = (int)(rnd() % (unsigned)t), i = 0; r >= w[i]; i++) r -= w[i];
    if (!(m = kt[i].v ? variant_base(kt[i].v) : pick_look(kt, kt[i].id, kt[i].look, 0, -1, -1, none))) return NULL;
    *var = kt[i].v ? kt[i].v->id : 0;
    in_set(rk.set, &rk.nset, i);
    rk.heavy += kt[i].v && kt[i].v->heavy;
    rk.cost += kt[i].cost;
    return m;
}

static struct { unsigned char slot, id; short bit; } xtra[ENEMY_EXTRA_MAX];   /* the room's extra enemies and their dead flags (-1: none) */
static int xtra_n, xtra_gen = -1;

/* Test aid RECVX_ENEMY_KILL=n[:r|:x]: from n game frames into each room, every 20 frames each living enemy of the room's
   records (ids 1..30, in its AI, shown) and its extra enemies takes a heavy hit as from the player's weapon (weapon.c
   bhCheckGunAtari: hit flag, joint, damage); ":r" only the records, ":x" only the extras. Logs the living ones at n,
   each death (flg & 2: what the script's DieCk turns into its kill bit) and, at the next room, how many died. */
static void kill_test(void)
{
    static int n = -2, only, gen = -1, t, hit[PC_ENE_SLOTS], died[PC_ENE_SLOTS], room;
    int k, i, h = 0, d = 0, slots[128 + ENEMY_EXTRA_MAX], ns = 0, nrec;
    char line[700];
    if (n == -2) {
        const char* e = getenv("RECVX_ENEMY_KILL");
        n = e ? atoi(e) : -1;
        only = e && strchr(e, ':') ? strchr(e, ':')[1] : 0;
    }
    if (n < 0 || !sys || !plp || !rom || (sys->gm_flg & 0x9) || (sys->cb_flg & 0x1)) return;
    if (gen != enemy_room_gen) {
        for (k = 0; k < PC_ENE_SLOTS; k++) { h += hit[k]; d += died[k]; }
        if (h) printf("test: room %04d: %d of %d enemies hit died\n", room, d, h);
        gen = enemy_room_gen; t = 0; room = room_file_now();
        memset(hit, 0, sizeof(hit)); memset(died, 0, sizeof(died));
    }
    if (++t < n) return;
    for (k = 0; k < rom->ene_n && k < 128; k++) slots[ns++] = k;
    nrec = ns;
    for (i = 0; xtra_gen == enemy_room_gen && i < xtra_n; i++) slots[ns++] = xtra[i].slot;
    if (t == n) {   /* who is there */
        int ln = 0, bits = 0;
        for (i = 0; i < 32; i++) bits += __builtin_popcount(sys->ed_flg[i]);
        ln = snprintf(line, sizeof(line), "test: room %04d (%d ed_flg bits set) living enemies at frame %d:", room, bits, n);
        for (i = 0; i < ns && ln < (int)sizeof(line) - 24; i++) {
            const BH_PWORK* ep = &ene[slots[i]];
            if ((ep->flg & 0x3) == 0x1 && !(ep->stflg & 0x1000000) && ep->id >= 1 && ep->id <= 30)
                ln += snprintf(line + ln, sizeof(line) - ln, " %s%d:%d", i >= nrec ? "extra@" : "", slots[i], ep->id);
        }
        printf("%s\n", line);
    }
    for (i = 0; i < ns; i++) {
        BH_PWORK* ep = &ene[k = slots[i]];
        int extra = i >= nrec;
        if (!(ep->flg & 0x1) || ep->id < 1 || ep->id > 30 || (only == 'r' && extra) || (only == 'x' && !extra)) continue;
        if ((ep->flg & 0x2) && hit[k] && !died[k]) { died[k] = 1; printf("test: room %04d %s %d (type %d) died after %d frames\n", room, extra ? "extra in slot" : "record", k, ep->id, t - n); }
        if ((ep->flg & 0x2) || (ep->stflg & 0x1000000) || ep->mode0 == 5 || (t - n) % 20) continue;
        hit[k] = 1;
        ep->flg |= 0x4;
        ep->dpx = ep->px; ep->dpy = ep->py + 10.0f; ep->dpz = ep->pz;
        ep->wpnr_no = plp->wpnr_no >= 2 ? plp->wpnr_no : 2;   /* (no weapon or the lighter: the knife's, weapon 2; 0 and 1 never
                                                                    hit in the game, their rows in the enemies' damage tables are
                                                                    empty: a Bandersnatch hit by one does not die, en09.c) */
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

/* Test aid RECVX_ENEMY_FLOORS=n: n game frames into each room (2: the frame after the extras came), every enemy (ids 1..30)
   of the room's records and its extras is logged with its floor, the ground under it (ground_at, as find_spot tests a
   spot) and the bottom of the box that ground is the top of, and what the options did to it: moved (Rearranged), random (another type or look), extra, orig (other: a part an
   enemy's Init put in a slot of its own). */
static void floor_test(void)
{
    static int n = -2, gen = -1, t;
    int k, i;
    if (n == -2) n = getenv("RECVX_ENEMY_FLOORS") ? atoi(getenv("RECVX_ENEMY_FLOORS")) : -1;
    if (n < 0 || !sys || !plp || !rom || !(sys->tk_flg & 0x80) || !(sys->sp_flg & 0x2) || (sys->gm_flg & 0x9) || (sys->cb_flg & 0x1)) return;
    if (gen != enemy_room_gen) { gen = enemy_room_gen; t = 0; }
    if (++t != n) return;
    for (k = 0; k < (int)sys->ewk_n && k < PC_ENE_SLOTS; k++) {
        const BH_PWORK* ep = &ene[k];
        const char* what = "other";
        const ATR_WORK* hp;
        float g;
        if (!(ep->flg & 0x1) || ep->id < 1 || ep->id > 30) continue;
        if (k < rom->ene_n && k < 128) what = moved_rec[k] ? "moved" : rand_src[k] ? "random" : "orig";
        for (i = 0; xtra_gen == enemy_room_gen && i < xtra_n; i++) if (xtra[i].slot == k) what = "extra";
        g = ground_at(ep->px, ep->py, ep->pz, &hp);
        printf("test: floors room %04d slot %d %s id %d from %d flr %d pos %.2f %.2f %.2f ground %.2f bottom %.2f level %.2f%s%s\n", room_file_now(), k,
               what, ep->id, k < rom->ene_n ? rom->enep[k].id : ep->id, ep->flr_no, ep->px, ep->py, ep->pz, g,
               !hp ? g : hp->type == 7 ? hp->py + hp->h : hp->py, rom->grand[bhCheckFloorNum(ep->py + 0.5f) + 2], slope(hp) ? " slope" : "",
               ep->stflg & 0x1000000 ? " hidden" : "");
    }
}

/* An extra enemy is remembered dead like the record it copies: ed_flg EXTRA_DEAD + the kill bit the room's entry script
   hides that record by (EneSetCk), set when the extra dies, and it stays away while that is set. EneSetCk tests only
   bits 0..511 (retail uses 0..357, rooms built with pc/tools up to 511), so 512..1023 are free; they are saved and
   shared in co-op as the other ed_flg bits. An extra of a record the script does not hide when dead (no EneSetCk, as
   the Battle Game's) comes back with the room, as that record does. */
#define EXTRA_DEAD 512

/* guest: the host's extra enemies of this room as the host's notes realize them, with their dead flags. Before, only the
   host set one: an extra the guest killed while the host was elsewhere came back for both on the host's next visit
   (in single player it stays dead). */
void enemy_extra_note(int slot, const SpawnNote* n)
{
    int k;
    if (!n->dead || n->dead < EXTRA_DEAD || n->dead >= 32 * 32) return;
    if (xtra_gen != enemy_room_gen) { xtra_gen = enemy_room_gen; xtra_n = 0; }
    for (k = 0; k < xtra_n; k++) if (xtra[k].slot == slot) return;
    if (xtra_n < ENEMY_EXTRA_MAX) { xtra[xtra_n].slot = (unsigned char)slot; xtra[xtra_n].id = n->id; xtra[xtra_n++].bit = (short)n->dead; }
}

void enemy_extras_frame(void)
{
    static int done_gen = -1;
    const EnemyOpts* o;
    int cand[128], nc = 0, k, made = 0, room, base;
    kill_test();
    headshot_test();
    floor_test();
    if (!sys || !plp || !rom) return;
    if (xtra_gen == enemy_room_gen)   /* (also a guest's: one that dies while the host is elsewhere, the guest's game runs it) */
        for (k = 0; k < xtra_n; k++) {
            const BH_PWORK* ep = &ene[xtra[k].slot];
            if (xtra[k].bit >= 0 && (ep->flg & 0x3) == 0x3 && ep->id == xtra[k].id && !bhCkFlg(sys->ed_flg, xtra[k].bit)) {
                bhStFlg(sys->ed_flg, xtra[k].bit);
                host_log("enemies: the extra enemy in slot %d died: ed_flg %d set\n", xtra[k].slot, xtra[k].bit);
            }
        }
    if (done_gen == enemy_room_gen) return;
    o = enemy_opts();
    if (!o->extra || coop_is_guest()) { done_gen = enemy_room_gen; return; }
    if (!(sys->tk_flg & 0x80) || !(sys->sp_flg & 0x2) || (sys->gm_flg & 0x9) || (sys->cb_flg & 0x1)) return;   /* the room runs */
    done_gen = xtra_gen = enemy_room_gen;
    xtra_n = 0;
    room = room_file_now();
    for (base = 0; base < PC_ENE_SLOTS && (ene[base].flg & 0x1); base++);
    /* the same extras on every visit: the room file alone picks the candidates (dead records too: their extras have
       flags of their own) and each extra has its own draws, so one that stays dead moves no other */
    for (k = 0; k < rom->ene_n && k < 128; k++)
        if ((ene[k].flg & 0x1) && extra_type(ene[k].id) && !rec_flags[k]) cand[nc++] = k;
    rnd_seed((unsigned)o->seed, (unsigned)room * 7u + 1u);
    for (k = nc - 1; k > 0; k--) { int j = rnd() % (k + 1), t = cand[k]; cand[k] = cand[j]; cand[j] = t; }   /* shuffle */
    for (k = 0; k < nc && made < o->extra; k++) {
        const ETTY_WORK* r = &rom->enep[cand[k]];
        ActorModel am = { "extra enemy", (unsigned short)room, (unsigned char)cand[k], (unsigned char)r->id, r->mdlver, 0, 0 };
        SpawnNote n = { 0 };
        const char* why;
        const ActorModel* m;
        int bit = rec_kill[cand[k]] >= 0 && rec_kill[cand[k]] < EXTRA_DEAD ? EXTRA_DEAD + rec_kill[cand[k]] : -1, slot = -1, want;
        float dx, dz;
        if (rand_src[cand[k]]) { am = *rand_src[cand[k]]; n.variant = rand_var[cand[k]]; }   /* a randomized record: a copy of its new type (and variant) */
        rnd_seed((unsigned)o->seed, (unsigned)room * 7u + 2u + (unsigned)cand[k] * 7919u);
        n.flr = r->flr_no; n.ay = (short)(rnd() & 0xFFFF);   /* (from the record, not where its enemy walked to) */
        n.dead = (unsigned short)(bit >= 0 ? bit : 0);
        if (!find_spot(r->px, r->py, r->pz, r->flr_no, 1, 15.0f, 35.0f, &n.px, &n.py, &n.pz)) continue;
        made++;
        if (no_drop(n.px, n.py, n.pz, ground_at(r->px, r->py, r->pz, NULL), 1) && (m = extra_kind(o, n.px, n.py, n.pz, r->flr_no, &n.variant)))
            am = *m;   /* (before the dead test: the same kinds on every visit) */
        else rk.cost += slot_cost(am.id);
        n.room = am.room; n.rec = am.rec; n.id = am.id; n.mdlver = am.mdlver;
        /* each extra in its own slot on every visit, the dead ones' left free: a co-op guest that stayed in the room has
           the host's earlier ones in these slots (before, the next one took a dead one's slot: the guest's dead extra came
           alive again as the host's other one, and the guest's own copy of that one ran on with no host enemy behind it) */
        want = base + made - 1;
        if (want >= PC_ENE_SLOTS || (ene[want].flg & 0x1)) want = -1;
        dx = n.px - plp->px; dz = n.pz - plp->pz;
        if (bit >= 0 && bhCkFlg(sys->ed_flg, bit)) why = "dead (killed before)";
        else if (dx * dx + dz * dz < 40.0f * 40.0f) why = "not now: the player stands there (a loaded save, a retry)";
        else if (!(why = spawn_add(&am, &n, want, &slot)) && xtra_n < ENEMY_EXTRA_MAX) {
            xtra[xtra_n].slot = (unsigned char)slot; xtra[xtra_n].id = (unsigned char)am.id; xtra[xtra_n++].bit = (short)bit;
        }
        host_log("enemies: extra %s (type %d model %d, from record %d, dead flag %d) at %.0f %.0f: %s\n", am.name, am.id, am.mdlver, cand[k], bit,
                 n.px, n.pz, why ? why : "spawned");
        if (why && (strstr(why, "slots") || strstr(why, "memory"))) break;
    }
    if (nc) host_log("enemies: room %04d: %d extra enemies (of %d eligible records, option %d)\n", room, made, nc, o->extra);
}
