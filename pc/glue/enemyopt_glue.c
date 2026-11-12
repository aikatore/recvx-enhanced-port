/* Enemy options (docs/ENEMIES.md "Enemy options"): (a) extra enemies (anywhere in a room with enemies, away from its doors;
 * of the randomizer's kinds, or copies of the room's records), (b) rearranged placements, (c) the enemy randomizer. Off by default; with everything off nothing
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
int bhCheckCutArea(float px, float pz, char flr_no);   /* cut.h */
int bhCheckCutAreaInnerTriangle(float px, float pz, CUT_WRK* ct);

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

/* what the room script does with each record (pc/tools/enemies.py's census takes every LoadWork as an event's, as rec_any) */
enum { REC_EVENT = 1, REC_SHOWN = 2, REC_HP = 4,
       REC_WORK = 8,     /* a LoadWork of it with only ops any type takes alike after it (work_op, generic_op) */
       REC_PLACED = 16 }; /* ... and a positioning among them (rec_spot) */
static unsigned char rec_flags[128];    /* on this visit (scan_script) */
static unsigned char rec_any[128];      /* in any code of the room's scripts (co-op ownership: enemy_rec_scripted) */
static float rec_spot[128][3];          /* REC_PLACED: where the script puts it (CommonCtr 7) */
static const ActorModel* rand_src[128];   /* randomized records: where their new type comes from */
static unsigned short rand_var[128];    /* and which enemy variant they became (0: none) */
static unsigned char moved_rec[128];    /* records "Rearranged Enemies" moved (RECVX_ENEMY_FLOORS) */
static int all_dead_check;              /* the script counts kills (DieEventCk): "all dead" doors */
static int rm_flg_used;                 /* the script tests or sets rm_flg bits 29..31 (= 0x4, 0x2, 0x1), which some types write */
static unsigned char etc_door[256];     /* triggers the script makes doors (EtcAtariSet2 type 0; a SetDoorCall where the player is at
                                           it) or item spots (ItmSetCk): 1 door, 2 item */
static unsigned char flg_cleared[2][128];   /* ev_flg / ky_flg bits a script of the room clears (or toggles) */
static unsigned char ev_started[256];   /* events an EvtOn starts: 1 somewhere, | 2 from code that runs on this visit */
static int scan_gen = -1;               /* the room load the scan is of */
int coop_op_cond(int op);               /* net/coop_proto.c */

/* ops that act on the work a LoadWork loaded (event.c: bhCetask->work) */
static int work_op(int op) { return op == 43 || op == 72 || op == 85 || op == 100 || op == 102 || op == 103 || op == 105 || op == 133 || op == 150; }
/* ... one that any enemy type takes alike (event.c Common_controll / Sub_controll): CommonCtr position (7), angle (11), sign
   flags (12, 13), collision flags (23, 41), hittable (46 1), bhCalcModel (47); SubCtr AI start (128), task reset (139), modes
   0 or 1 with type 0 (143, 147: an Init again). Anything else is the type's own: room motions (CommonCtr 24, 25, 34, 36),
   node offsets, a type number or state (RM_1121's climb: SubCtr 147 type 10, then 9), the Spotter's light (SubCtr 148) */
static int generic_op(const unsigned char* p)
{
    if (p[0] == 105) return p[1] == 7 || p[1] == 11 || p[1] == 12 || p[1] == 13 || p[1] == 23 || p[1] == 41 || p[1] == 47 || (p[1] == 46 && p[2]);
    if (p[0] != 103) return 0;
    if (p[1] == 128 || p[1] == 139) return 1;
    return (p[1] == 143 || p[1] == 147) && p[2] <= 1 && !p[3] && !p[4] && !p[5] && (p[1] == 143 || !p[6]);
}
/* an IfelCk at p whose then-part cannot run on this visit: one of its tests wants an ev_flg / ky_flg bit clear that is set
   now and that no script of the room clears */
static int dead_if(const unsigned char* p)
{
    const unsigned char* q;
    int len, b;
    for (q = p + 2; coop_op_cond(*q) && (len = coop_insn_len(q)) > 0; q += len) {
        b = (q[2] | q[3] << 8) & 0x3FF;
        if (q[0] == 4 && (q[1] == 1 || q[1] == 2) && q[5] == 1 && !(flg_cleared[q[1] - 1][b >> 3] >> (b & 7) & 1) &&
            bhCkFlg(q[1] == 1 ? sys->ev_flg : sys->ky_flg, (unsigned)b)) return 1;
    }
    return 0;
}

/* pass 0: every instruction (the flags the scripts clear, the events started anywhere); 1: the code that runs on this visit
   (dead_if's then-parts skipped, and the events only those start), to find the events it starts; 2: as 1, the records */
static void walk(int pass)
{
    unsigned char* base = (unsigned char*)rom->evtp;
    unsigned int* tab = (unsigned int*)base;
    unsigned int offs[256], first, o, end;
    unsigned char* rf = pass ? rec_flags : rec_any;
    int n = 0, i, j;
    first = tab[0];
    for (i = 0; (unsigned)i * 4 < first && i < 256; i++) {
        offs[n++] = tab[i];
        if (tab[i] && tab[i] < first) first = tab[i];
    }
    for (i = 0; i < n; i++) {
        unsigned int ends[64];
        int depth = 0, cur = -1, at_etc = -1;   /* cur: the record a LoadWork loaded; at_etc: a test "the player is at trigger n" */
        unsigned char sg[3] = { 0 };
        if (pass && i >= 2 && ev_started[i - 2] == 1) continue;   /* (started only from code that does not run) */
        for (end = 0x7FFFFFFF, j = 0; j < n; j++)
            if (offs[j] > offs[i] && offs[j] < end) end = offs[j];
        for (o = offs[i]; o < end && o < (unsigned)sys->rdtsz;) {
            unsigned char* p = base + o;
            int len = coop_insn_len(p), op = p[0];
            while (depth && ends[depth - 1] <= o) depth--;
            if (len <= 0) break;
            if (cur >= 0 && work_op(op) && pass != 1) {
                if (!generic_op(p)) rf[cur] |= REC_EVENT;
                else if (op == 105 && p[1] == 12) memcpy(sg, p + 2, 3);
                else if (op == 105 && p[1] == 7) {
                    rf[cur] |= REC_PLACED;
                    for (j = 0; j < 3; j++) rec_spot[cur][j] = (float)(p[2 + 2 * j] | p[3 + 2 * j] << 8) / (sg[j] ? -100.0f : 100.0f);
                }
            }
            if (pass != 1) switch (op) {
            case 101: case 104: case 181: cur = p[1] == 1 ? p[2] & 127 : -1; if (cur >= 0) rf[cur] |= REC_WORK; break;   /* LoadWork, 2, Ex */
            case 45: case 48: case 83: case 125: rf[p[1] & 127] |= REC_EVENT; break;   /* motion pauses, links, positioning */
            case 81: rf[p[2] & 127] |= REC_EVENT; break;
            case 84: rf[p[1] & 127] |= REC_EVENT; rf[p[2] & 127] |= REC_EVENT; break;
            case 142: case 143: case 154: case 165: case 204: rf[p[1] & 127] |= REC_EVENT; break;   /* zombie/face/render/look ops read the type's work */
            case 144: rf[p[1] & 127] |= REC_EVENT; break;
            case 121: rf[p[2] & 127] |= REC_EVENT; break;   /* EneSeOn */
            case 7: if (p[2] == 10) rf[p[1] & 127] |= REC_HP; break;   /* CmpW on the HP */
            case 9: if (p[1] >= 11 && p[1] <= 15) rf[p[1] - 11] |= REC_HP; break;   /* SvW */
            case 153: case 208: rf[p[1] & 127] |= REC_HP; break;   /* EnemyHpUp, EneLifeSetM */
            case 36: if (p[1] == 1) rf[p[2] & 127] |= REC_SHOWN; break;   /* InitModelSet */
            }
            switch (op) {   /* (all code in pass 0: "all dead" doors, rm_flg bits and door triggers anywhere count) */
            case 20: ev_started[p[3]] |= pass ? 2 : 1; break;   /* EvtOn */
            case 33: all_dead_check = 1; break;   /* DieEventCk */
            case 37: if (p[8] == 0) etc_door[p[1]] = 1; break;   /* EtcAtariSet2 etc := type 0 */
            case 35: if (!etc_door[p[4]]) etc_door[p[4]] = 2; break;   /* ItmSetCk item, etc */
            case 51: if (at_etc >= 0) etc_door[at_etc] = 1; break;   /* SetDoorCall, the player at a trigger */
            case 4: case 5: {   /* Ck / Set rm_flg (bit n: mask 0x80000000 >> n), ev_flg, ky_flg; Ck "the player is at trigger n" */
                int b = (p[2] | p[3] << 8) & 0x3FF;
                if (p[1] == 4 && b >= 29 && b <= 31) rm_flg_used = 1;
                if (op == 5 && p[5] && (p[1] == 1 || p[1] == 2)) flg_cleared[p[1] - 1][b >> 3] |= (unsigned char)(1 << (b & 7));
                if (op == 4 && p[1] == 10 && b == 23) at_etc = p[4];
                break;
            }
            case 1:   /* IfelCk; one that cannot pass on this visit: on to its else part or behind its Endif, as the interpreter goes */
                at_etc = -1;
                if (pass && dead_if(p)) {
                    unsigned int e = o + p[1];   /* (the ElseCk before an else part: that part's end, as case 2) */
                    if (depth < 64) ends[depth++] = base[e] == 2 ? e + base[e + 1] : o + p[1] + 1;
                    o += 2 + p[1];
                    continue;
                }
                if (depth < 64) ends[depth++] = o + p[1] + 1;
                break;
            case 2: if (depth) depth--; if (depth < 64) ends[depth++] = o + p[1]; break;   /* ElseCk */
            case 3: if (depth) depth--; break;   /* Endif */
            }
            o += len;
            if (!depth && (op == 0 || (i >= 2 && op == 255))) break;
        }
    }
}

/* At the room load (the ev_flg / ky_flg bits as they are then: code they rule out is not this visit's): the records' uses
   on this visit (rec_flags: the randomizer) and in any code (rec_any: placements, co-op ownership), the "all dead" doors,
   the rm_flg bits, the triggers that are doors or item spots. ponytail: a flag that changes between the co-op host's load
   of the room and the guest's could classify a record differently on the two sides; the flags this reads are set on
   earlier visits (RM_1121's 168: its climb), long synced by then. */
static void scan_script(void)
{
    unsigned char was[256];
    int k;
    scan_gen = enemy_room_gen;
    memset(rec_flags, 0, sizeof(rec_flags));
    memset(rec_any, 0, sizeof(rec_any));
    memset(etc_door, 0, sizeof(etc_door));
    memset(flg_cleared, 0, sizeof(flg_cleared));
    memset(ev_started, 0, sizeof(ev_started));
    all_dead_check = rm_flg_used = 0;
    if (!rom->evtp) return;
    walk(0);
    for (k = 0; k < 8; k++) { memcpy(was, ev_started, sizeof(was)); walk(1); if (!memcmp(was, ev_started, sizeof(was))) break; }
    walk(2);
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
/* the player's body at (x, y, z) on floor flr touches no wall as the player's own wall check sees it (player.c: bhCheckWallEx
   with PlyInfo's radius and height, the taller characters'; flg 0x100 as the walking player has it, not 0x400 nor 0x4000: the
   odd wall types and the lips along ledges count; stflg 0: the walls only enemies pass, attr 0x4, count too, and the check
   touches nothing); o: where the step comes from (NULL: standing there) */
static int player_fits(float x, float y, float z, int flr, const NJS_POINT3* o)
{
    static BH_PWORK pw;
    NJS_POINT3 np = { x, y, z }, op = o ? *o : np;
    pw.flg = 0x100;
    pw.stflg = 0;
    pw.flr_no = (char)flr;
    pw.px = x; pw.py = y; pw.pz = z;
    return !bhCheckWallEx(&pw, &np, &op, 3.5f, 17.5f);
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
   a step), for the extra enemies (extra_grid): a Cerberus put by a crate in RM_2031 stood inside it
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
        if ((ene[k].flg & 0x1) && !(rec_any[k] & (REC_EVENT | REC_SHOWN | REC_WORK)) && roams(ene[k].id)) cand[nc++] = k;
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
        kd[n].look = -1;
        kd[n++].v = v;
    }
    return n;
}
static int ground_record(int id) { return id == 1 || id == 3 || id == 4 || id == 5 || id == 9; }
static int look_ok(const ActorModel* m, int id) { return m->id == id && !(id == 9 && m->mdlver == 1); }   /* (Bandersnatch 2 is an event's arm) */
static int kind_of(const Kind* kt, int id, int ver) { for (int i = 0; i < KINDS; i++) if (kt[i].id == id && (kt[i].look < 0 || kt[i].look == ver)) return i; return -1; }

/* directions (of 8) with no wall within `len` units of (x, z) on floor flr, `h` units above y (`mask` bhCollisionCheckLine2's:
   0x4400 skips the odd wall types, the knee-high lips along ledges and the line walls) */
static int open_dirs_m(float x, float y, float z, int flr, float len, unsigned mask, float h)
{
    int k, n = 0;
    for (k = 0; k < 8; k++) {
        float a = (float)k * (6.2831853f / 8.0f);
        NJS_POINT3 p1 = { x, y + h, z }, p2 = { x + len * sinf(a), y + h, z + len * cosf(a) };
        n += !bhCollisionCheckLine2(&p1, &p2, mask, (char)flr);
    }
    return n;
}
static int open_dirs(float x, float y, float z, int flr, float len) { return open_dirs_m(x, y, z, flr, len, 0x4400, 10.0f); }   /* (waist height) */

static int xdraw;                       /* kind_w for an extra enemy (plan_extras) */
static int flat_around(float x, float y, float z, float r);

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
        if (xdraw && *big >= 5 && !flat_around(x, y, z, 12.0f)) *big = 0;   /* (an extra: level ground 12 units round, flat_around) */
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
   flier). The room's kinds, slots and heavy variants are kept for its extra enemies (rk, plan_extras). */
static void in_set(unsigned char* set, int* n, int kd) { if (kd >= 0 && !set[kd]) { set[kd] = 1; ++*n; } }
/* a room's kinds as randomize leaves them for its extra enemies (rk, of room load gen): the kinds in it (nset of them), the
   slots and heavy variants they take, the kinds drawn (kused) and looks used, Mild's two kinds (-1: none drawn yet) */
typedef struct { int gen, nset, cost, heavy, mild[2]; unsigned char set[KINDS + VARIANT_MAX], kused[KINDS + VARIANT_MAX], used[256]; } RoomKinds;
static RoomKinds rk = { -1 };

/* Mild: the room's two kinds (its own draws) */
static void mild_kinds(const EnemyOpts* o, const Kind* kt, int nk, int* mk)
{
    int j, k, t, r, w;
    mk[0] = mk[1] = -1;
    rnd_seed((unsigned)o->seed, (unsigned)room_file_now() * 977u + 0x3000u);
    for (j = 0; j < 2; j++) {
        for (t = 0, k = 0; k < nk; k++) t += k != mk[0] && !(kt[k].flags & K_NOCOUNT && all_dead_check) ? kt[k].w[0] : 0;
        for (r = (int)(rnd() % (unsigned)t), k = 0; k < nk; k++) {
            w = k != mk[0] && !(kt[k].flags & K_NOCOUNT && all_dead_check) ? kt[k].w[0] : 0;
            if (r < w) break;
            r -= w;
        }
        mk[j] = k;
    }
    host_log("enemies: room %04d: Mild's kinds %d.%d %d.%d\n", room_file_now(), kt[mk[0]].id, kt[mk[0]].look, kt[mk[1]].id, kt[mk[1]].look);
}

/* the kind of a free or shown record of type id model ver at (x, y, z) on floor flr, drawn from the kinds that fit there
   (kind_w; `cost`: the slots the rest of the room takes, `counted`: the doors count its kill) and the room's kinds so far
   (st): Mild's two kinds if one fits; Types per Room's (target: one the room lacks; full: one it has, if one fits); Wild's
   unused kinds first. An extra enemy draws as a record of type 1 (a walker) with model -1 (no own look), not counted.
   -1: none fits; else *mp its look (NULL: none other than the record's own), *vp its enemy variant (NULL: none). */
static int draw_kind(const EnemyOpts* o, const Kind* kt, int nk, const RoomKinds* st, int id, int ver, float x, float y, float z, int flr, int cost,
                     int counted, const ActorModel** mp, const EnemyVariant** vp)
{
    int w[KINDS + VARIANT_MAX], t = 0, fresh = 0, r, big = -1, mine = 0, mset = 0, i, kd = -1, wild = o->variety != 0, custom = o->variety == 2;
    int target = o->types && wild && st->nset < o->types;
    *mp = NULL; *vp = NULL;
    for (i = 0; i < nk; i++) {
        w[i] = kind_w(o, kt, i, id, x, y, z, flr, cost, st->heavy, counted, &big);
        mine |= w[i] && (i == st->mild[0] || i == st->mild[1]);
    }
    for (i = 0; i < nk; i++) {
        if (mine && i != st->mild[0] && i != st->mild[1]) w[i] = 0;   /* Mild: the room's two kinds, if one fits */
        mset |= w[i] && st->set[i];
    }
    for (i = 0; i < nk; i++) {
        if (o->types && st->nset >= o->types && mset && !st->set[i]) w[i] = 0;   /* Types per Room: the room's kinds, if one fits */
        fresh |= w[i] && (target ? !st->set[i] : !st->kused[i] && !custom);
    }
    for (i = 0; i < nk; i++) {   /* each kind once while unused ones are left (Types per Room's target: one the room lacks) */
        if (fresh && (target ? st->set[i] : st->kused[i])) w[i] = 0;
        t += w[i];
    }
    if (t) {
        for (r = (int)(rnd() % (unsigned)t), kd = 0; kd < nk && r >= w[kd]; kd++) r -= w[kd];
        if ((*vp = kt[kd].v)) *mp = variant_base(*vp);   /* an enemy variant: its base's entry */
        else *mp = pick_look(kt, kt[kd].id, kt[kd].look, 0, id, ver, st->used);
    }
    return kd;
}

static void randomize(const EnemyOpts* o)
{
    int k, j, made = 0, nc = 0, guest = coop_is_guest(), room = room_file_now(), wild = o->variety != 0, custom = o->variety == 2;
    Kind kt[KINDS + VARIANT_MAX];
    int nk = make_kinds(kt);
    RoomKinds st = { .gen = enemy_room_gen, .mild = { -1, -1 } };
    unsigned char held[128] = { 0 }, cls[128] = { 0 };   /* cls: 1 another kind, 2 another look */
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
        st.cost += slot_cost(ep->id);   /* (all records: a change adds its difference, so a room never exceeds its own count) */
        if (!ground_record(ep->id) && !(wild && flier_record(ep->id))) continue;
        if (ep->id == 9 && ep->mdlver == 1) continue;   /* the Bandersnatch's arm (RM_3130's scene plays its room motions): no other look */
        if (!rec_flags[k]) cls[k] = 1;
        else if (o->events) cls[k] = rec_flags[k] & (REC_EVENT | REC_HP) ? 2 : 1;
        if (cls[k] == 1 && ep->flr_no < 0 && !(rec_flags[k] & REC_PLACED)) cls[k] = 0;   /* (no floor under it: one hanging somewhere for an event,
                                                                                        unless this visit's script puts it somewhere: RM_1121) */
        if (cls[k] == 1 && ground_record(ep->id) && !on_ground(ep)) cls[k] = 2;   /* placed off the ground on purpose (RM_6090's Black Widow
                                                                                    under the floor): only another look of its type */
        nc += cls[k] != 0;
    }
    for (k = 0, ln = 0; k < rom->ene_n && k < 128 && ln < (int)sizeof(line) - 24; k++)   /* (pc/tools: docs/ENEMIES.md "Event enemies per room") */
        if ((ene[k].flg & 0x1) && ene[k].id >= 1 && ene[k].id <= 30 && (rec_flags[k] || cls[k] != 1))
            ln += snprintf(line + ln, sizeof(line) - ln, " %d:%d:%s%s", k, ene[k].id, cls[k] == 1 ? "kind" : cls[k] == 2 ? "look" : "kept",
                           !rec_flags[k] ? "" : rec_flags[k] & (REC_EVENT | REC_HP) ? ",event" : rec_flags[k] & REC_WORK ? ",work" : ",shown");
    if (ln) host_log("enemies: room %04d scripted records:%s\n", room, line);
    ln = 0;
    for (k = 0; k < rom->ene_n && k < 128; k++)   /* Types per Room: the kinds of the records that keep theirs */
        if ((ene[k].flg & 0x1) && cls[k] != 1) in_set(st.set, &st.nset, kind_of(kt, ene[k].id, ene[k].mdlver));
    if (!nc) { rk = st; return; }
    if (!wild) mild_kinds(o, kt, nk, st.mild);   /* Mild: up to two kinds for the room's free records */
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
            for (i = 0; o->types && st.nset >= o->types && i < KINDS; i++) eoff |= (unsigned)!st.set[i] << i;   /* Types per Room, the room full: its kinds only */
            if ((kd = kind_of(kt, save.id, save.mdlver)) >= 0 && (m = pick_look(kt, save.id, -1, eoff & ~(1u << kd), save.id, save.mdlver, st.used)))
                kd = kind_of(kt, m->id, m->mdlver);   /* (a Hunter's look can be the Sweeper's kind; Custom: not while it is Off, nor past Types per Room) */
        } else {   /* (a parked record the script puts somewhere: the kinds that fit there; it is spawned where it is parked) */
            int pk = save.flr_no < 0;
            float sx = pk ? rec_spot[k][0] : save.px, sy = pk ? rec_spot[k][1] : save.py, sz = pk ? rec_spot[k][2] : save.pz;
            kd = draw_kind(o, kt, nk, &st, save.id, save.mdlver, sx, sy, sz, pk ? bhCheckFloorNum(sy + 0.5f) : save.flr_no, st.cost - slot_cost(save.id),
                           all_dead_check, &m, &v);
        }
        if (!m) {   /* its own kind drawn, without another look (Cerberus, bat), or nothing else fits */
            if (kd >= 0 && kd < nk) st.kused[kd] = 1;
            in_set(st.set, &st.nset, kind_of(kt, save.id, save.mdlver));
            host_log("enemies: record %d stays type %d model %d (%s)\n", k, save.id, save.mdlver, cls[k] == 2 ? "event enemy or off the ground, no other look of its type"
                     : kd >= 0 && kd < nk && kt[kd].id == save.id ? "its own kind drawn" : "nothing else fits");
            continue;
        }
        for (j = 0; j < spawn_base_count() && spawn_entry(j) != m; j++);
        st.used[j] = 1;
        st.kused[kd] = 1;
        kcost = kd >= 0 && kd < nk ? kt[kd].cost : slot_cost(m->id);
        if (held[k]) {   /* as the host's: the original */
            in_set(st.set, &st.nset, kind_of(kt, save.id, save.mdlver));
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
            if (!guest) coop_rand_kept(k, &kn), in_set(st.set, &st.nset, kind_of(kt, save.id, save.mdlver));   /* the guest keeps it too */
            else st.cost += kcost - slot_cost(save.id), st.heavy += v && v->heavy, in_set(st.set, &st.nset, kd);   /* the host's budget and kinds (it made the
                                                                                                         change), so the next draws stay its */
            continue;
        }
        in_set(st.set, &st.nset, kd);
        for (i = 0; i < save.mdl_n; i++)   /* the original's textures are released with the room's (the record now holds the new type) */
            if ((save.mdl[i].flg & 0x200) && save.mdl[i].texP) spawn_keep_texlist(save.mdl[i].texP);
        rand_src[k] = m;   /* (the room file still says the old type: extra enemies copy from here) */
        rand_var[k] = v ? v->id : 0;
        st.cost += kcost - slot_cost(save.id);
        st.heavy += v && v->heavy;
        made++;
        if (v) host_log("enemies: room %04d record %d: type %d model %d -> %s (variant %d: type %d model %d)\n", room, k, save.id, save.mdlver, v->name, v->id, m->id, m->mdlver);
        else host_log("enemies: room %04d record %d: type %d model %d -> %s (type %d model %d)%s\n", room, k, save.id, save.mdlver, m->name, m->id, m->mdlver,
                      cls[k] == 2 ? ", event enemy: same type" : "");
    }
    rk = st;
    for (k = 0; k < rom->ene_n && k < 128 && ln < (int)sizeof(line) - 16; k++)
        if ((ene[k].flg & 0x1) && ene[k].id < 31) ln += snprintf(line + ln, sizeof(line) - ln, " %d:%d.%d", k, ene[k].id, ene[k].mdlver);
    host_log("enemies: room %04d: %d of %d records randomized (seed %d, %s%s, %d kinds, limit %d%s), now%s\n", room, made, nc, o->seed, variety_name(o->variety),
             o->events ? ", event enemies" : "", st.nset, o->types, o->types && st.nset > o->types ? ": over it, records that could only be another kind" : "", line);
}

/* co-op ownership (coop_glue.c own_movable): a record the room's scripts act on - LoadWork, motion pauses, links,
   positioning, the type's work, its sounds (REC_EVENT) or its HP (REC_HP) - is never run by the guest's game: a script
   step on the host would act on a puppet and the guest's next snapshot undo it. Scanned once per room load (the scan
   reads the script only; also without the enemy options). */
int enemy_rec_scripted(int slot)
{
    if (!rom || !sys || slot < 0 || slot >= rom->ene_n || slot >= 128) return 0;
    if (scan_gen != enemy_room_gen) scan_script();
    return (rec_any[slot] & (REC_EVENT | REC_HP | REC_WORK)) != 0;
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
    scan_script();   /* (it only reads the script: co-op ownership, the test aids) */
    if (o->place || o->rand || o->extra) {
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
/* types an extra enemy copies when the randomizer is off: they roam freely and die like any enemy */
static int extra_type(int id) { return id == 1 || id == 3 || id == 4 || id == 5 || id == 9 || id == 22 || id == 26; }

/* The room files that get extra enemies (docs/ENEMIES.md "Extra enemies"): the retail rooms with enemy records (ids 1..30,
   event ones too) but the boss arenas (boss_arena: their scripts wait for the boss), the Battle Game's (5510..5710: its
   kill counts and timer), the cutscene rooms 8030 (Wesker's monitor) and 9340 (Wesker and Claire), and 9170 (the ant swarm
   of the tiger statue elevator: 76 records). A room with an item box (a save room) gets none either (extra_room).
   Each has eight dead flags of its own: bits index * 8 + extra (xdead). APPEND ONLY: an entry that moves moves the dead
   flags of saves. ponytail: retail rooms only; a mod's or BioRand's room with enemies where retail has none gets no extras
   (a dead flag each needs an entry here). */
static const short xrooms[] = {
    20, 21, 30, 31, 50, 60, 70, 80, 90, 110, 120, 130, 140, 150, 160,
    1000, 1021, 1030, 1060, 1070, 1090, 1100, 1120, 1121,
    2000, 2010, 2011, 2020, 2031,
    3011, 3020, 3030, 3050, 3060, 3070, 3090, 3100, 3120, 3130, 3140, 3160, 3170, 3180, 3200,
    4000, 4001, 4010, 4012, 4020,
    6010, 6020, 6030, 6050, 6070, 6080, 6090,
    7031, 7050, 7060, 7070, 7071, 7080, 7081, 7100, 7110, 7120, 7130, 7140, 7150, 7180, 7181, 7190, 7191, 7200, 7210, 7250,
    8000, 8001, 8010, 8020,
    9010, 9020, 9030, 9060, 9080, 9100, 9110, 9120, 9130, 9190, 9200, 9220, 9290, 9310,
};
#define XROOMS (int)(sizeof(xrooms) / sizeof(xrooms[0]))
_Static_assert(XROOMS * ENEMY_EXTRA_MAX <= ENEMY_XDEAD_WORDS * 32, "the extras' dead flags");

/* The extras' dead flags live in save-region words no game code uses (coop_glue.c's map of sys->itm: 302..319 and
   360..382), so saves, the retry point (bhPushGameData) and a co-op guest's start (EV_BLOB) keep them; co-op sends their
   changes both ways as EV_FLAGS words 120.. (coop_glue.c diff_flags). A new game clears them (sub1.c AllItemInit). */
unsigned int* enemy_xdead_word(int i) { return &sys->itm[i < 18 ? 302 + i : 360 + (i - 18)]; }
static int xdead(int b) { return (int)(*enemy_xdead_word(b >> 5) >> (b & 31) & 1); }
static void xdead_set(int b) { *enemy_xdead_word(b >> 5) |= 1u << (b & 31); }

/* this room gets extra enemies: NULL (*xi its xrooms index), else why not */
static const char* extra_room(int* xi)
{
    int k, n = 0, room = room_file_now();
    for (*xi = 0; *xi < XROOMS && xrooms[*xi] != room; ++*xi);
    for (k = 0; k < rom->ene_n; k++) {
        if (boss_arena(rom->enep[k].id)) return "a boss arena";
        n += rom->enep[k].id >= 1 && rom->enep[k].id <= 30;
    }
    if (!n) return "no enemies";
    for (k = 0; k < rom->etc_n; k++)   /* (type 4 with attr 0x2: a box, 0x4 / 0x8 the special ones; without: an item pickup) */
        if (rom->etcp[k].type == 4 && (rom->etcp[k].attr & 0xE) == 0x2) return "an item box (a save room)";
    return *xi < XROOMS ? NULL : "not a room for extra enemies";
}

/* Where extras may stand: at least XR_DOOR units from every door's trigger (type 0, its box; also one the script makes a
   door) and from every entry position, so nobody coming in meets one at the door; XR_USE from what the player uses (item
   pickups and boxes: type 4; events such as typewriters, puzzles and the doors an event opens: type 3 with an event; the
   item spots of the script's ItmSetCk; the stairs' ends: type 1) and from the items; XR_ENEMY from every record and the
   earlier extras. The examine messages (type 3 without an event: a shelf, a painting) and the climbing areas (type 2) do not
   count. 10 units are a metre. */
#define XR_DOOR 30.0f
#define XR_USE 15.0f
#define XR_ENEMY 15.0f
static float box_d2(const ATR_WORK* b, float x, float z)   /* squared distance to a trigger's box (x..x+w, z..z+d) */
{
    float dx = x < b->px ? b->px - x : x > b->px + b->w ? x - b->px - b->w : 0, dz = z < b->pz ? b->pz - z : z > b->pz + b->d ? z - b->pz - b->d : 0;
    return dx * dx + dz * dz;
}
static const ATR_WORK* etc_at(int i) { return i < rom->etc_n ? &rom->etcp[i] : &sys->metcp[i - rom->etc_n]; }
static int is_door(int i) { return etc_at(i)->type == 0 || (i < rom->etc_n && i < 256 && etc_door[i] == 1); }
/* the radius a trigger keeps extras away (0: none) */
static float etc_r(int i)
{
    const ATR_WORK* t = etc_at(i);
    if (is_door(i)) return XR_DOOR;
    if (t->type == 1 || t->type == 4 || (t->type == 3 && (t->attr & 0x9)) || (i < rom->etc_n && i < 256 && etc_door[i] == 2)) return XR_USE;
    return 0;
}
static int etc_count(void) { return rom->etc_n + sys->metc_n; }
/* the nearest door trigger (*door) and entry position (*entry) to (x, z), in units */
static void door_dist(float x, float z, float* door, float* entry)
{
    int i;
    float d = 1e9f, e = 1e9f;
    for (i = 0; i < etc_count(); i++) if (is_door(i) && box_d2(etc_at(i), x, z) < d) d = box_d2(etc_at(i), x, z);
    for (i = 0; i < rom->pos_n; i++) {
        float dx = x - rom->posp[i].px, dz = z - rom->posp[i].pz;
        if (dx * dx + dz * dz < e) e = dx * dx + dz * dz;
    }
    *door = sqrtf(d); *entry = sqrtf(e);
}
static int near2(float x, float z, float px, float pz, float r) { return (x - px) * (x - px) + (z - pz) * (z - pz) < r * r; }
static int extra_away(float x, float z)   /* (the earlier extras: extra_spot) */
{
    int i;
    for (i = 0; i < etc_count(); i++) {
        float r = etc_r(i);
        if (box_d2(etc_at(i), x, z) < r * r) return 0;
    }
    for (i = 0; i < rom->pos_n; i++) if (near2(x, z, rom->posp[i].px, rom->posp[i].pz, XR_DOOR)) return 0;
    for (i = 0; i < rom->itm_n; i++) if (near2(x, z, rom->itmp[i].px, rom->itmp[i].pz, XR_USE)) return 0;
    for (i = 0; i < rom->ene_n; i++) if (near2(x, z, rom->enep[i].px, rom->enep[i].pz, XR_ENEMY)) return 0;
    return 1;
}

/* The room's walkable floor as a grid (once per room load; extra_spot draws from it): the floor the player can walk to from
   the entry positions (the cells within 8 units of each, with no wall in between) - a flood fill in steps of one cell
   (XG_STEP units; more in big rooms) where the player and an enemy can walk: no wall between the cells' centres
   (bhCollisionCheckLine2 at waist height, every wall type the player's check takes: lips along ledges, line walls), the
   player's body fits in the cell (player_fits), a camera zone shows it (in_view), ground within a unit of the last cell's
   on the same floor and no slope (stairs, ramps), or no ground where the last had none (the lowest floor of a room without
   floor slabs). Before (protocol 50) the records that stand on the ground were anchors too and the body and the camera
   zones did not count: the fill leaked through gaps a body does not pass, over lips, out of the camera zones (RM_0030's
   street: 5500 cells outside it) and into the scenery records stand in for an event. Spots are the walkable cells with
   the 4 cells beside them walkable at their height (xg_inner: a margin from the floor's edges), not in water, away from
   doors, entries, triggers, items and records (extra_away), with no drop nor rising ground within 6 units (no_drop) and no
   wall within 6 units all round at the waist and at the feet (not in an object, nor against a wall or on a line wall: an
   enemy put there is pushed off it). Only the room file and the options decide it (the same on every visit). ponytail:
   one height per cell; where two floors overlap (a balcony over the floor) the first one reached has it. */
#define XG_MAX 16384
#define XG_STEP 4.0f
static struct { int gen, nx, nz, walk, n; float x0, z0, s; } xg = { -1 };
static float xg_y[XG_MAX];               /* a reached cell's ground (or its anchor's height) */
static signed char xg_flr[XG_MAX];
static unsigned char xg_st[XG_MAX];      /* 0 not reached, 1 on the ground, 2 no ground */
static unsigned short xg_spot[XG_MAX];   /* the spot cells (xg.n) */
static float cell_x(int c) { return xg.x0 + ((float)(c % xg.nx) + 0.5f) * xg.s; }
static float cell_z(int c) { return xg.z0 + ((float)(c / xg.nx) + 0.5f) * xg.s; }
/* (x, z) is in a camera zone of floor flr: cut.c bhCheckCutArea's test, of every zone the room file has (also those its
   script turns on later: the same on every visit). Where no camera shows the floor, the player never stands. */
static int in_view(float x, float z, int flr)
{
    int i, j;
    for (i = 0; i < rom->cut_n; i++)
        for (j = 0; j < rom->cutp[i].ctab_n; j++) {
            CUT_WRK* ct = &rom->cutp[i].cuttp[j];
            if (ct->flr_no == flr && (ct->atr_tp ? bhCheckCutAreaInnerTriangle(x, z, ct) : ct->minx <= x && ct->maxx > x && ct->minz <= z && ct->maxz > z)) return 1;
        }
    return 0;
}

/* a step from (x, y, z) on floor flr (gnone: no ground there) into cell c: 1 if the player and an enemy walk there (c
   marked, queued) */
static int xg_step(float x, float y, float z, int flr, int gnone, int c, unsigned short* q, int* tail)
{
    const ATR_WORK* hp;
    float cx = cell_x(c), cz = cell_z(c), g;
    NJS_POINT3 p1 = { x, y + 10.0f, z }, p2 = { cx, y + 10.0f, cz };
    if (xg_st[c]) return 0;
    g = ground_at(cx, y, cz, &hp);
    if (g > -999.0f ? slope(hp) || fabsf(g - y) > 1.0f || bhCheckFloorNum(g + 0.5f) != flr : !gnone) return 0;
    if (bhCollisionCheckLine2(&p1, &p2, 0, (char)flr)) return 0;   /* (every type the player's check takes: the odd ones, an enemy put on a
                                                                     line wall is pushed through it, RM_8010's catwalk; the lips) */
    if (!in_view(cx, cz, flr) || !player_fits(cx, g > -999.0f ? g : y, cz, flr, NULL)) return 0;
    xg_st[c] = g > -999.0f ? 1 : 2;
    xg_y[c] = g > -999.0f ? g : y;
    xg_flr[c] = (signed char)flr;
    q[(*tail)++] = (unsigned short)c;
    return 1;
}
/* the 4 cells beside c were reached on its floor at its height: a spot's margin from the edges of the walkable floor (walls,
   the camera zones' edges, drops) */
static int xg_inner(int c)
{
    int dx, dz, ix = c % xg.nx, iz = c / xg.nx, e;
    for (dz = -1; dz <= 1; dz++)
        for (dx = -1; dx <= 1; dx++) {
            if (dx && dz) continue;
            if (ix + dx < 0 || iz + dz < 0 || ix + dx >= xg.nx || iz + dz >= xg.nz) return 0;
            e = c + dz * xg.nx + dx;
            if (!xg_st[e] || xg_flr[e] != xg_flr[c] || fabsf(xg_y[e] - xg_y[c]) > 1.0f) return 0;
        }
    return 1;
}
static void extra_grid(void)
{
    static unsigned short q[XG_MAX];
    float x1 = -1e9f, z1 = -1e9f;
    int k, head = 0, tail = 0, c;
    if (xg.gen == enemy_room_gen) return;
    xg.gen = enemy_room_gen; xg.n = xg.walk = 0;
    xg.x0 = xg.z0 = 1e9f;
    for (k = 0; k < rom->wal_n; k++) {   /* the room's extent: its walls' */
        const ATR_WORK* w = &rom->walp[k];
        if (w->px < xg.x0) xg.x0 = w->px;
        if (w->pz < xg.z0) xg.z0 = w->pz;
        if (w->px + w->w > x1) x1 = w->px + w->w;
        if (w->pz + w->d > z1) z1 = w->pz + w->d;
    }
    if (x1 <= xg.x0 || z1 <= xg.z0) { xg.nx = xg.nz = 0; return; }
    for (xg.s = XG_STEP; ceilf((x1 - xg.x0) / xg.s) * ceilf((z1 - xg.z0) / xg.s) > XG_MAX; xg.s += 1.0f);
    xg.nx = (int)ceilf((x1 - xg.x0) / xg.s); xg.nz = (int)ceilf((z1 - xg.z0) / xg.s);
    memset(xg_st, 0, sizeof(xg_st));
    for (k = 0; k < rom->pos_n; k++) {   /* the anchors: the entry positions (records stand where the player may never come:
                                            behind the scenery for an event) */
        const ATR_WORK* hp;
        int ix, iz, dx, dz;
        float x = rom->posp[k].px, y = rom->posp[k].py, z = rom->posp[k].pz, g = ground_at(x, y, z, &hp);
        if (g > -999.0f && (fabsf(g - y) > 1.0f || slope(hp))) continue;   /* off the ground (an event's), or on stairs */
        ix = (int)((x - xg.x0) / xg.s); iz = (int)((z - xg.z0) / xg.s);
        if (g > -999.0f) y = g;
        for (dz = -2; dz <= 2; dz++)   /* (the cells round it: by a door, the one it is in may touch the frame) */
            for (dx = -2; dx <= 2; dx++)
                if (ix + dx >= 0 && iz + dz >= 0 && ix + dx < xg.nx && iz + dz < xg.nz)
                    xg_step(x, y, z, bhCheckFloorNum(y + 0.5f), g <= -999.0f, (iz + dz) * xg.nx + ix + dx, q, &tail);
    }
    while (head < tail) {   /* the flood fill */
        int d;
        c = q[head++];
        for (d = 0; d < 4; d++) {
            int ix = c % xg.nx + (d == 0) - (d == 1), iz = c / xg.nx + (d == 2) - (d == 3);
            if (ix >= 0 && iz >= 0 && ix < xg.nx && iz < xg.nz)
                xg_step(cell_x(c), xg_y[c], cell_z(c), xg_flr[c], xg_st[c] == 2, iz * xg.nx + ix, q, &tail);
        }
    }
    xg.walk = tail;
    for (k = 0; k < tail; k++) {   /* in cell order (as reached): the spots */
        float x, z;
        NJS_POINT3 w;
        c = q[k];
        x = cell_x(c); z = cell_z(c);
        w.x = x; w.y = xg_y[c] + 1.0f; w.z = z;
        if (!xg_inner(c) || bhCheckWater(&w)) continue;
        if (extra_away(x, z) && no_drop(x, xg_y[c], z, xg_st[c] == 2 ? -1000.0f : xg_y[c], 1) && open_dirs_m(x, xg_y[c], z, xg_flr[c], 6.0f, 0, 10.0f) == 8 &&
            open_dirs_m(x, xg_y[c], z, xg_flr[c], 6.0f, 0, 0.5f) == 8)   /* (and at the feet: RM_7050's lines a unit high) */
            xg_spot[xg.n++] = (unsigned short)c;
    }
}

/* the ground at 8 points r units around (x, z) within a unit of y (no ground: only where there is none at (x, z) either): a
   big kind's body reaches that far (a Giant Black Widow on RM_1000's raised walk stood over its edge, 8 units up) */
static int flat_around(float x, float y, float z, float r)
{
    int k;
    float g0 = ground_at(x, y, z, NULL);
    for (k = 0; k < 8; k++) {
        float a = (float)k * (6.2831853f / 8.0f), g = ground_at(x + r * sinf(a), y, z + r * cosf(a), NULL);
        if (g > -999.0f ? fabsf(g - y) > 1.0f : g0 > -999.0f) return 0;
    }
    return 1;
}

/* a random spot for an extra enemy (the next draws): one of the room's spots (extra_grid) at least XR_ENEMY units from the
   earlier extras (xs, nx), XS_TRIES draws; 1 = found */
#define XS_TRIES 32
static int extra_spot(const float (*xs)[3], int nx, float* ox, float* oy, float* oz, int* oflr)
{
    int k, i;
    extra_grid();
    for (k = 0; k < XS_TRIES && xg.n; k++) {
        int c = xg_spot[rnd() % (unsigned)xg.n];
        float x = cell_x(c), z = cell_z(c);
        for (i = 0; i < nx && !near2(x, z, xs[i][0], xs[i][2], XR_ENEMY); i++);
        if (i < nx) continue;
        *ox = x; *oy = xg_y[c]; *oz = z; *oflr = xg_flr[c];
        return 1;
    }
    return 0;
}

static struct { unsigned char slot, id, died; short bit; } xtra[ENEMY_EXTRA_MAX];   /* the room's extra enemies, their dead flags (-1: none),
                                                                                     died: seen dead in this room load */
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

/* An extra enemy is remembered dead by a flag of its own: extra i of the room at xrooms index x has bit x * 8 + i of the
   extras' dead flags (xdead), set when it dies, by whichever game runs it (single player, the host, or a co-op guest
   that has the host's extras while the host is elsewhere); it stays away while that is set, its slot left free. Before
   (0.7.x), an extra copied a record and took ed_flg 512 + that record's kill bit; those bits mean nothing now (an old
   save's dead extras come back once). */

/* guest: the host's extra enemies of this room as the host's notes realize them, with their dead flags (SpawnNote.dead:
   ENEMY_XDEAD_BASE + the bit). Before, only the host set one: an extra the guest killed while the host was elsewhere came
   back for both on the host's next visit (in single player it stays dead). */
void enemy_extra_note(int slot, const SpawnNote* n)
{
    int k;
    if (n->dead < ENEMY_XDEAD_BASE || n->dead >= ENEMY_XDEAD_BASE + ENEMY_XDEAD_WORDS * 32) return;
    if (xtra_gen != enemy_room_gen) { xtra_gen = enemy_room_gen; xtra_n = 0; }
    for (k = 0; k < xtra_n; k++) if (xtra[k].slot == slot) return;
    if (xtra_n < ENEMY_EXTRA_MAX) { xtra[xtra_n].slot = (unsigned char)slot; xtra[xtra_n].id = n->id; xtra[xtra_n].died = 0; xtra[xtra_n++].bit = (short)(n->dead - ENEMY_XDEAD_BASE); }
}

/* How many extra enemies the room gets: 0..o->extra, all equally likely, a draw of its own (seed, room file), so the same
   on every visit (and only the host draws it). One fraction per room scaled by the option: a higher option never gives a
   room fewer. */
static int extra_count(const EnemyOpts* o)
{
    unsigned n = (unsigned)(o->extra < ENEMY_EXTRA_MAX ? o->extra : ENEMY_EXTRA_MAX);
    rnd_seed((unsigned)o->seed, (unsigned)room_file_now() * 13u + 0x7F00u);
    return (int)(((rnd() & 0xFFFF) * (n + 1)) >> 16);
}

/* The room's extra enemies as they come on every visit (docs/ENEMIES.md "Extra enemies"): extra i (0..cnt - 1) has
   draws of its own (seed, room file, i) and takes its spot (extra_spot, away from those before it), then its kind: with
   the randomizer as a free walker record at that spot (draw_kind on the room's kinds after the records, rk; it counts
   for the next ones), else a copy of a random record of the extra types. A spot where no kind fits is tried again (up to
   4 spots). Nothing depends on which are dead or where the player is, so the extras before one are the same on every
   visit; the count only cuts the list short (extra i is the same, with the same dead flag, whatever the count). Fills
   x[0..cnt - 1]: 1 = planned (its spot, floor, angle, type, variant), 0 = no spot or no kind (its reason in why[]).
   `dry` (test aid): rk is left as it was. */
typedef struct { int ok; const char* why; ActorModel am; SpawnNote n; } XPlan;
static int plan_extras(const EnemyOpts* o, XPlan* x, int cnt, int dry)
{
    static const ActorModel none = { "extra enemy", 0, 0, 0, 0, 0, 0 };
    Kind kt[KINDS + VARIANT_MAX];
    RoomKinds keep = rk;
    float xs[ENEMY_EXTRA_MAX][3];
    int nk = make_kinds(kt), room = room_file_now(), i, k, src[128], ns = 0, nx = 0, planned = 0, rz = o->rand && rk.gen == enemy_room_gen;
    for (k = 0; k < rom->ene_n && k < 128; k++) if (extra_type(rom->enep[k].id)) src[ns++] = k;
    if (rz && !o->variety && rk.mild[0] < 0) mild_kinds(o, kt, nk, rk.mild);   /* (a room whose records all keep their kind) */
    for (i = 0; i < cnt && i < ENEMY_EXTRA_MAX; i++) {
        SpawnNote* n = &x[i].n;
        int tries, kd = -1, flr = 0;
        memset(&x[i], 0, sizeof(x[i]));
        x[i].am = none;
        rnd_seed((unsigned)o->seed, (unsigned)room * 7u + 0x7000u + (unsigned)i * 7919u);
        n->ay = (short)(rnd() & 0xFFFF);
        for (tries = 0; tries < 4 && !x[i].ok; tries++) {
            const ActorModel* m = NULL;
            const EnemyVariant* v = NULL;
            if (!extra_spot((const float (*)[3])xs, nx, &n->px, &n->py, &n->pz, &flr)) { x[i].why = "no free spot"; break; }
            if (dry) { x[i].ok = 1; break; }   /* (spots only) */
            if (!rz) {   /* a copy of one of the room's records (its own type and model: no upload) */
                const ETTY_WORK* r;
                if (!ns) { x[i].why = "no record to copy (randomizer off)"; break; }
                r = &rom->enep[src[rnd() % (unsigned)ns]];
                x[i].am.room = (unsigned short)room; x[i].am.rec = (unsigned char)(r - rom->enep); x[i].am.id = (unsigned char)r->id; x[i].am.mdlver = r->mdlver;
                x[i].ok = 1;
                break;
            }
            xdraw = 1;   /* (kind_w: the big kinds also need level ground round the spot) */
            kd = draw_kind(o, kt, nk, &rk, 1, -1, n->px, n->py, n->pz, flr, rk.cost, 0, &m, &v);
            xdraw = 0;
            if (!m) { x[i].why = "no kind fits its spot"; continue; }
            if (o->types && rk.nset >= o->types && !rk.set[kd]) { x[i].why = "Types per Room: none of the room's kinds fits its spot"; continue; }   /* (a
                record must become something and may go over; an extra need not: a room of bats gets no walkers past it) */
            x[i].am = *m;
            n->variant = v ? v->id : 0;
            for (k = 0; k < spawn_base_count() && spawn_entry(k) != m; k++);
            if (k < (int)sizeof(rk.used)) rk.used[k] = 1;
            rk.kused[kd] = 1;
            in_set(rk.set, &rk.nset, kd);   /* (before the dead test: the same kinds on every visit) */
            rk.cost += kt[kd].cost;
            rk.heavy += v && v->heavy;
            x[i].ok = 1;
        }
        if (!x[i].ok) continue;
        x[i].why = NULL;
        xs[nx][0] = n->px; xs[nx][1] = n->py; xs[nx++][2] = n->pz;
        n->room = x[i].am.room; n->rec = x[i].am.rec; n->id = x[i].am.id; n->mdlver = x[i].am.mdlver; n->flr = (signed char)flr;
        planned++;
    }
    if (dry) rk = keep;
    return planned;
}

/* Test aid RECVX_ENEMY_SPOTS=seeds (pc/tools/enemy_door_check.sh): at the first frame each room runs, the extras' spots
   for seeds 1..seeds with ENEMY_EXTRA_MAX extras and the options' randomizer off (spots only, no kinds; nothing spawns),
   one line per seed: how many found a spot, and each one's x z and distance to the nearest door trigger and entry position. */
static void spots_test(void)
{
    static int n = -2, gen = -1;
    int s, i, xi, ln;
    char line[600];
    if (n == -2) n = getenv("RECVX_ENEMY_SPOTS") ? atoi(getenv("RECVX_ENEMY_SPOTS")) : -1;
    if (n <= 0 || gen == enemy_room_gen || !sys || !plp || !rom || !(sys->tk_flg & 0x80) || !(sys->sp_flg & 0x2) || (sys->gm_flg & 0x9) || (sys->cb_flg & 0x1)) return;
    gen = enemy_room_gen;
    if (extra_room(&xi)) { printf("test: spots room %04d: none (%s)\n", room_file_now(), extra_room(&xi)); return; }
    for (s = 1; s <= n; s++) {
        EnemyOpts o = { .extra = ENEMY_EXTRA_MAX, .seed = s };
        XPlan x[ENEMY_EXTRA_MAX];
        int got = plan_extras(&o, x, ENEMY_EXTRA_MAX, 1);
        ln = snprintf(line, sizeof(line), "test: spots room %04d seed %d: %d of %d", room_file_now(), s, got, ENEMY_EXTRA_MAX);
        for (i = 0; i < ENEMY_EXTRA_MAX && ln < (int)sizeof(line) - 40; i++) {
            float d, e;
            if (!x[i].ok) continue;
            door_dist(x[i].n.px, x[i].n.pz, &d, &e);
            ln += snprintf(line + ln, sizeof(line) - ln, " %.0f,%.0f,%.0f,%.0f", x[i].n.px, x[i].n.pz, d, e);
        }
        printf("%s\n", line);
    }
    printf("test: spots room %04d: grid %dx%d of %.0f units, %d cells walkable, %d spots\n", room_file_now(), xg.nx, xg.nz, xg.s, xg.walk, xg.n);
}

/* Test aid RECVX_ENEMY_OOB=seeds (pc/tools/enemy_bounds_check.sh): at the first frame each room runs, an out-of-bounds check
   of the extras' spots apart from extra_grid's rules: a flood fill of 2-unit cells from where the player stands and the
   entry positions in a camera zone, by the player's own wall check (bhCheckWallEx with the player's body, from the last
   cell; stairs and ramps too, a step up or down of up to 2 units). Each spot of the grid (every one, and those the
   extras of seeds 1..seeds take) is flagged when that fill does not reach it ("reach"), when it is in no camera zone of
   its floor (bhCheckCutArea: "cam"), when the player's body there touches a wall ("wall") or when it is in water. */
#define OB_MAX 65536
static int ob_nx, ob_nz;
static float ob_x0, ob_z0, ob_s, ob_y[OB_MAX];
static unsigned char ob_st[OB_MAX];
static int ob_flags(float x, float y, float z, int flr)
{
    int f = 0, ix = (int)floorf((x - ob_x0) / ob_s - 0.5f), iz = (int)floorf((z - ob_z0) / ob_s - 0.5f), a, b, r = 0;
    NJS_POINT3 w = { x, y + 1.0f, z };
    for (a = 0; a < 2; a++) for (b = 0; b < 2; b++) {   /* (the fine cells round it) */
        int cx = ix + a, cz = iz + b, c = cz * ob_nx + cx;
        if (cx >= 0 && cz >= 0 && cx < ob_nx && cz < ob_nz && ob_st[c] && fabsf(ob_y[c] - y) <= 2.0f) r = 1;
    }
    if (!r) f |= 1;
    if (bhCheckCutArea(x, z, (char)flr) < 0) f |= 2;
    if (!player_fits(x, y, z, flr, NULL)) f |= 4;
    if (bhCheckWater(&w)) f |= 8;
    return f;
}
static const char* ob_why(int f, char* t)
{
    snprintf(t, 40, "%s%s%s%s", f & 1 ? " reach" : "", f & 2 ? " cam" : "", f & 4 ? " wall" : "", f & 8 ? " water" : "");
    return t;
}
static void oob_test(void)
{
    static int n = -2, gen = -1;
    static int q[OB_MAX];
    int s, i, k, head = 0, tail = 0, xi, tot = 0, nflag = 0, bad[4] = { 0 }, nsp = 0, nbad = 0, f, hist[ENEMY_EXTRA_MAX + 1] = { 0 };
    char t[40];
    if (n == -2) n = getenv("RECVX_ENEMY_OOB") ? atoi(getenv("RECVX_ENEMY_OOB")) : -1;
    if (n < 0 || gen == enemy_room_gen || !sys || !plp || !rom || !(sys->tk_flg & 0x80) || !(sys->sp_flg & 0x2) || (sys->gm_flg & 0x9) || (sys->cb_flg & 0x1)) return;
    gen = enemy_room_gen;
    if (extra_room(&xi)) return;
    extra_grid();
    if (!xg.nx) return;
    for (ob_s = 2.0f; ceilf(xg.nx * xg.s / ob_s) * ceilf(xg.nz * xg.s / ob_s) > OB_MAX; ob_s += 1.0f);
    ob_x0 = xg.x0; ob_z0 = xg.z0; ob_nx = (int)ceilf(xg.nx * xg.s / ob_s); ob_nz = (int)ceilf(xg.nz * xg.s / ob_s);
    memset(ob_st, 0, sizeof(ob_st));
    for (k = -1; k < rom->pos_n; k++) {   /* the anchors: the player (-1), the entries in a camera zone; the cells within 4 units
                                             the player's body fits in with no wall between (an entry by a door touches its frame) */
        float x = k < 0 ? plp->px : rom->posp[k].px, y = k < 0 ? plp->py : rom->posp[k].py, z = k < 0 ? plp->pz : rom->posp[k].pz, g = ground_at(x, y, z, NULL);
        int ix = (int)((x - ob_x0) / ob_s), iz = (int)((z - ob_z0) / ob_s), dx, dz, flr;
        if (g > -999.0f && fabsf(g - y) <= 2.0f) y = g;
        flr = bhCheckFloorNum(y + 0.5f);
        if (k >= 0 && bhCheckCutArea(x, z, (char)flr) < 0) continue;
        for (dz = -2; dz <= 2; dz++)
            for (dx = -2; dx <= 2; dx++) {
                int c = (iz + dz) * ob_nx + ix + dx;
                NJS_POINT3 p1 = { x, y + 1.0f, z }, p2 = { ob_x0 + (ix + dx + 0.5f) * ob_s, y + 1.0f, ob_z0 + (iz + dz + 0.5f) * ob_s };
                if (ix + dx < 0 || iz + dz < 0 || ix + dx >= ob_nx || iz + dz >= ob_nz || ob_st[c] || bhCollisionCheckLine2(&p1, &p2, 0, (char)flr) ||
                    !player_fits(p2.x, y, p2.z, flr, NULL)) continue;
                ob_st[c] = g > -999.0f ? 1 : 2; ob_y[c] = y; q[tail++] = c;
            }
    }
    while (head < tail) {
        int c = q[head++], d;
        float x = ob_x0 + (c % ob_nx + 0.5f) * ob_s, z = ob_z0 + (c / ob_nx + 0.5f) * ob_s, y = ob_y[c];
        NJS_POINT3 o = { x, y, z };
        for (d = 0; d < 4; d++) {
            int ix = c % ob_nx + (d == 0) - (d == 1), iz = c / ob_nx + (d == 2) - (d == 3), e = iz * ob_nx + ix;
            float bx = ob_x0 + (ix + 0.5f) * ob_s, bz = ob_z0 + (iz + 0.5f) * ob_s, g;
            int none;
            if (ix < 0 || iz < 0 || ix >= ob_nx || iz >= ob_nz || ob_st[e]) continue;
            g = ground_at(bx, y, bz, NULL);
            if (g > -999.0f ? fabsf(g - y) > 2.0f : ob_st[c] != 2) continue;
            if ((none = g <= -999.0f)) g = y;
            if (!player_fits(bx, g, bz, bhCheckFloorNum(g + 0.5f), &o)) continue;
            ob_st[e] = none ? 2 : 1; ob_y[e] = g; q[tail++] = e;
        }
    }
    for (i = 0; i < xg.n; i++) {   /* every spot of the grid */
        int c = xg_spot[i];
        f = ob_flags(cell_x(c), xg_y[c], cell_z(c), xg_flr[c]);
        tot++;
        nflag += f != 0;
        for (k = 0; k < 4; k++) bad[k] += f >> k & 1;
        if (f && n == 0) printf("test: oob room %04d cell %.0f %.0f %.0f:%s\n", room_file_now(), cell_x(c), xg_y[c], cell_z(c), ob_why(f, t));
    }
    for (s = 1; s <= n; s++) {   /* the seeds' extras */
        EnemyOpts o = { .extra = ENEMY_EXTRA_MAX, .seed = s };
        XPlan x[ENEMY_EXTRA_MAX];
        plan_extras(&o, x, ENEMY_EXTRA_MAX, 1);
        hist[extra_count(&o)]++;
        for (i = 0; i < ENEMY_EXTRA_MAX; i++) {
            if (!x[i].ok) continue;
            nsp++;
            if (!(f = ob_flags(x[i].n.px, x[i].n.py, x[i].n.pz, x[i].n.flr))) continue;
            nbad++;
            printf("test: oob room %04d seed %d extra %d at %.0f %.0f %.0f flr %d:%s\n", room_file_now(), s, i, x[i].n.px, x[i].n.py, x[i].n.pz, x[i].n.flr, ob_why(f, t));
        }
    }
    printf("test: oob room %04d: %d of %d grid spots flagged (reach %d, cam %d, wall %d, water %d); %d of %d extras' spots of %d seeds; player fill %d cells of %.0f, "
           "grid %d walkable; counts of 8 over the seeds (0..8): %d %d %d %d %d %d %d %d %d\n", room_file_now(), nflag, tot, bad[0], bad[1], bad[2], bad[3], nbad, nsp, n,
           tail, ob_s, xg.walk, hist[0], hist[1], hist[2], hist[3], hist[4], hist[5], hist[6], hist[7], hist[8]);
}

void enemy_extras_frame(void)
{
    static int done_gen = -1;
    const EnemyOpts* o;
    XPlan x[ENEMY_EXTRA_MAX];
    int k, made = 0, planned, room, base, xi, refused = 0, cnt;
    const char* why;
    kill_test();
    headshot_test();
    floor_test();
    spots_test();
    oob_test();
    if (!sys || !plp || !rom) return;
    if (xtra_gen == enemy_room_gen)   /* (also a guest's: one that dies while the host is elsewhere, the guest's game runs it) */
        for (k = 0; k < xtra_n; k++) {
            const BH_PWORK* ep = &ene[xtra[k].slot];
            if (xtra[k].bit >= 0 && !xtra[k].died && (ep->flg & 0x3) == 0x3 && ep->id == xtra[k].id) {
                xtra[k].died = 1;   /* (once: a game over's retry puts the flags back to the retry point's while the dead one still lies here) */
                if (!xdead(xtra[k].bit)) {
                    xdead_set(xtra[k].bit);
                    host_log("enemies: the extra enemy in slot %d died: dead flag %d set\n", xtra[k].slot, xtra[k].bit);
                }
            }
        }
    if (done_gen == enemy_room_gen) return;
    o = enemy_opts();
    if (!o->extra || coop_is_guest()) { done_gen = enemy_room_gen; return; }
    if (!(sys->tk_flg & 0x80) || !(sys->sp_flg & 0x2) || (sys->gm_flg & 0x9) || (sys->cb_flg & 0x1)) return;   /* the room runs */
    done_gen = xtra_gen = enemy_room_gen;
    xtra_n = 0;
    room = room_file_now();
    if ((why = extra_room(&xi))) { host_log("enemies: room %04d: no extra enemies (%s)\n", room, why); return; }
    for (base = 0; base < PC_ENE_SLOTS && (ene[base].flg & 0x1); base++);
    cnt = extra_count(o);
    planned = plan_extras(o, x, cnt, 0);
    for (k = 0; k < cnt; k++) {
        SpawnNote* n = &x[k].n;
        int bit = xi * ENEMY_EXTRA_MAX + k, slot = -1, want;
        float dx, dz, door, entry;
        if (!x[k].ok) { host_log("enemies: extra %d: %s\n", k, x[k].why); continue; }
        n->dead = (unsigned short)(ENEMY_XDEAD_BASE + bit);
        /* each extra in its own slot on every visit, the dead ones' left free: a co-op guest that stayed in the room has
           the host's earlier ones in these slots (before, the next one took a dead one's slot: the guest's dead extra came
           alive again as the host's other one, and the guest's own copy of that one ran on with no host enemy behind it) */
        want = base + k;
        if (want >= PC_ENE_SLOTS || (ene[want].flg & 0x1)) want = -1;
        dx = n->px - plp->px; dz = n->pz - plp->pz;
        door_dist(n->px, n->pz, &door, &entry);
        if (xdead(bit)) why = "dead (killed before)";
        else if (dx * dx + dz * dz < XR_DOOR * XR_DOOR) why = "not now: the player stands there (a loaded save, a retry)";
        else if ((why = spawn_add(&x[k].am, n, want, &slot))) refused = 1;
        else if (xtra_n < ENEMY_EXTRA_MAX) {
            xtra[xtra_n].slot = (unsigned char)slot; xtra[xtra_n].id = (unsigned char)x[k].am.id; xtra[xtra_n].died = 0; xtra[xtra_n++].bit = (short)bit;
            made++;
        }
        host_log("enemies: extra %s (type %d model %d, extra %d, dead flag %d) at %.0f %.0f (door %.0f, entry %.0f): %s\n", x[k].am.name, x[k].am.id, x[k].am.mdlver, k,
                 bit, n->px, n->pz, door, entry, why ? why : "spawned");
        if (refused) break;   /* (slots, memory, textures): the room is full */
    }
    host_log("enemies: room %04d: %d extra enemies spawned, %d planned (option %d: %d here)\n", room, made, planned, o->extra, cnt);
}
