/* F2 "Spawn enemy" (docs/ENEMIES.md): any enemy type, a short distance in front of the player,
 * facing it, with its own AI; also the realization used by the extra enemies and the randomizer
 * (enemyopt_glue.c) and by the co-op guest for the host's added enemies.
 *
 * A spawn is made the way the room loader makes a room record (room.c bhSetRoom / bhFinishRoom):
 * the type's model chunk list is read from a room file that carries it (the "You appear as"
 * catalogue, pc/src/host/actors.c, plus a few types it leaves out), copied into the room arena
 * (sys->memp, so it lives exactly as long as the room) and realized with bhSetEneMdl, which takes
 * a free enemy slot and shares the textures of a type/model version the room already has
 * (sys->et_lp). Textures the room does not have are uploaded from the source room's texture table
 * and released again with the room's (bhReleaseMainTexture hook). A type without a motion set in
 * this room gets its own (bhSetEneMtn). Then the O_WORK matrices; the type's Init runs on the next
 * enemy tick. No record, so scripts never see it, it has no kill flag and is gone after a room
 * change.
 */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/room.h"
#include "../../decomp/include/ps2/veronica/prog/system.h"
#include "../../decomp/include/ps2/veronica/prog/dread.h"
#include "../../decomp/include/ps2/veronica/prog/eneset.h"
#include "../../decomp/include/ps2/veronica/prog/binfunc.h"
#include "../../decomp/include/ps2/veronica/prog/njplus.h"
#include "../../decomp/include/ps2/veronica/prog/MdlPut.h"
#include "../../decomp/include/ps2/veronica/prog/pwksub.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_texture.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_NaTextureFunction.h"
#include "../src/host/actors.h"
#include "../src/host/cheat.h"
#include "../src/host/enemies.h"
#include "../src/host/variants.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void host_log(const char* fmt, ...);
void _exit(int);
NJS_TEXLIST** pc_et_lp(int id, int ver);   /* room.c: sys->et_lp[id][ver] */
extern unsigned int Ps2_texmemlist_num;
int coop_is_guest(void);
void enemy_extras_frame(void);          /* enemyopt_glue.c */
void variant_spawned(int slot, int id); /* variant_glue.c */
void variant_release(void);
const ActorModel* variant_base(const EnemyVariant* v);
extern int enemy_room_gen;              /* enemyopt_glue.c: room loads so far */

/* types the appearance catalogue leaves out (room file, record, id, model version) */
static const ActorModel extra[] = {
    { "Ant (Small)", 9170, 17, 10, 0 },
    { "Ant (Wall)", 9170, 0, 71, 0 },
    { "Baby Black Widow", 9050, 2, 24, 0 },
    { "Gulp Worm", 3091, 0, 2, 0 },
    { "Alexia (Second Form)", 5810, 0, 13, 0 },
    { "Alexia (Third Form)", 5810, 1, 14, 0 },
    { "Alexia's Baby", 5810, 2, 30, 0 },
    { "Tentacle", 9000, 0, 29, 0 },
    { "Armored Knight (Sword)", 9280, 3, 54, 0 },
    { "Armored Knight (Lance)", 9280, 4, 55, 0 },
    { "Ceiling Trap", 9290, 0, 25, 0 },
    { "Nosferatu's Core", 5800, 1, 53, 0 },
    { "Alfred Ashford (Boss)", 1020, 2, 16, 0 },
    { "Exploding Prop", 80, 5, 0, 0 },
};
#define EXTRA (int)(sizeof(extra) / sizeof(extra[0]))

/* the list: catalogue entries of enemy types (ids up to 30; the cast and scripted NPCs are 31+),
   then the extras */
static short list[ACTOR_MODELS + EXTRA];
static int list_n;
__attribute__((constructor)) static void make_list(void)
{
    int i;
    for (i = 0; i < ACTOR_MODELS; i++)
        if (actor_models[i].id <= 30) list[list_n++] = i;
    for (i = 0; i < EXTRA; i++) list[list_n++] = ACTOR_MODELS + i;
}
const ActorModel* spawn_entry(int i) { return list[i] < ACTOR_MODELS ? &actor_models[list[i]] : &extra[list[i] - ACTOR_MODELS]; }
#define entry spawn_entry
int spawn_base_count(void) { return list_n; }
/* the F2 list: the types' entries, then the enemy variants (custom enemies: built in, then mods/enemies.ini),
   marked "[C] " (docs/ENEMIES.md "Enemy variants") */
int spawn_count(void) { return list_n + variant_count(); }
const char* spawn_name(int i) { return i >= 0 && i < list_n ? entry(i)->name : variant_at(i - list_n) ? variant_at(i - list_n)->ui : ""; }

/* enemy slots a type takes with what its Init spawns (bhSetEnemy without a NULL check in the
   callers; the en*.c Inits counted). What enemies spawn later (moth eggs on the player, a
   tentacle's damage dummy) comes from the SPAWN_SLOTS_FREE kept free. */
int spawn_slots_needed(int id)
{
    switch (id) {
    case 1: return 10;    /* zombie: link, head, 2 arms, 2 legs, cap + worm or bomb + scope, parent */
    case 2: return 19;    /* Gulp Worm: 2 parts, 16 segments */
    case 3: return 25;    /* Black Widow: leader, 8 children, 15 babies */
    case 6: return 16;    /* moth: leader, 14 children */
    case 13: return 12;   /* Alexia 2: 10 tentacles, body part */
    case 5: return 2;     /* Hunter: helper */
    case 17: case 23: return 3;   /* Monster Steve's arms, Giant Black Widow's children */
    }
    return 1;
}

/* textures we uploaded (copies of the texlists: the originals sit in the arena, which the next
   room overwrites), released with the room's */
#define TEX_LISTS 256
#define TEX_NAMES 4096
static NJS_TEXLIST tl[TEX_LISTS];
static NJS_TEXNAME tn[TEX_NAMES];
static int tl_n, tn_n;
static int spawned[PC_ENE_SLOTS];                /* slots we spawned into (id + 1), for the trace */
static int room_key = -1;              /* the room the textures belong to */

static void release_textures(void)
{
    int i;
    for (i = 0; i < tl_n; i++) njReleaseTexture(&tl[i]);
    if (tl_n) host_log("spawn: released %d texture lists\n", tl_n);
    tl_n = tn_n = 0;
    memset(spawned, 0, sizeof(spawned));
}

void cast_release(void);   /* cast_glue.c */
void spawn_release(void) { variant_release(); cast_release(); release_textures(); }   /* ps2_texture.c bhReleaseMainTexture; also the enemy variants' and the cutscene cast's models */

void spawn_textures_released(void) { tl_n = tn_n = 0; memset(spawned, 0, sizeof(spawned)); }   /* njReleaseTextureAll */

/* a copy of an uploaded texlist, released with the room (also for the randomizer: the texlists of
   the record a new type replaced) */
void spawn_keep_texlist(const NJS_TEXLIST* l)
{
    if (tl_n >= TEX_LISTS || tn_n + (int)l->nbTexture > TEX_NAMES) { host_log("spawn: texture list table full\n"); return; }
    memcpy(&tn[tn_n], l->textures, l->nbTexture * sizeof(NJS_TEXNAME));
    tl[tl_n].textures = &tn[tn_n];
    tl[tl_n++].nbTexture = l->nbTexture;
    tn_n += l->nbTexture;
    room_key = room_file_now();
}

static int empty_set(const MN_WORK* m)
{
    int i;
    for (i = 0; m && i < 512; i++)
        if (m[i].datP) return 0;
    return 1;
}

/* texture entries and heap left over for the room's own uses (effects); the heap margin also covers the
   largest model's textures (654 KB), whose size is only known after the upload (a full heap is exit(0)) */
#define TEX_KEEP 16
#define HEAP_KEEP (1 << 20)
#define TEX_HARD 4                      /* SPAWN_DICTATED: the host's spawns, no reserve (a guest that refused them */
#define HEAP_HARD (704 << 10)           /*   on its own reserve had other enemies than the host); the largest upload */
static const char* tex_full(int entries, int heap)
{
    static char m[96];
    snprintf(m, sizeof(m), "Not enough texture memory (%d available textures, %d KB free).", entries, heap >> 10);
    return m;
}

/* The spawned types' chunk lists (model, motions; realized in place). A room's own sit in the room
   arena, which the map, file and option screens also borrow: they copy the room's textures above
   sys->memp (adv.c AdvPushRoomTexture, map.c). So the copies go to a pool of their own, emptied
   when the next room is set up (room.c bhSetRoom), and the arena keeps that reserve. */
#define POOL_SIZE (32 << 20)
static unsigned char pool[POOL_SIZE] __attribute__((aligned(64)));
static int pool_n;
static void sound_release(void);
void spawn_room_setup(void) { pool_n = 0; pc_enemy_added = 0; sound_release(); }

/* a copy at the source's alignment (mod 64) */
static unsigned char* pool_copy(const unsigned char* src, int len)
{
    int at = ((pool_n + 63) & ~63) + (int)((unsigned int)src & 63);
    if (at + len > POOL_SIZE) return NULL;
    memcpy(pool + at, src, len);
    pool_n = at + len;
    return pool + at;
}

/* what the screens copy above sys->memp: the room model's textures (bhCopyTexmem2MainmemSub);
   while they are not all uploaded yet (the room load), the room file's texture section */
static int room_tex_push(void)
{
    const NJS_TEXLIST* t = rom->mdl.texP;
    int i, n = 0;
    for (i = 0; t && i < (int)t->nbTexture; i++) {
        const NJS_TEXMEMLIST* m = (const NJS_TEXMEMLIST*)t->textures[i].texaddr;
        if ((unsigned int)m < (unsigned int)Ps2_tex_info || (unsigned int)m >= (unsigned int)(Ps2_tex_info + Ps2_texmemlist_num) || !m->texinfo.texsurface.TextureSize)
            return (int)sys->rdtsz - (int)((const RDT_WORK*)sys->rdtp)->hed04;
        n += m->texinfo.texsurface.TextureSize;
    }
    return n;
}

/* (sys->rdtp: the room file, which room_tex_push reads while the room's textures are not all up; it is
   still NULL when a guest that died rejoins and the host's EV_SPAWN arrives before its first room load -
   that crashed the guest) */
/* in a room that runs, and the system monitor only runs the game (mn_mode 0): its other modes read a file into
   sys->memp and take it a frame later (a weapon change: bhSysCallMonitor mode 3, bhReadWeaponData), and a spawn
   then wrote its model there - the weapon's motions came out of the enemy's data (crash at the next aim, or in
   bhReadWeaponData itself: a co-op guest that changed weapons as the host's added enemy arrived) */
static int ready(void)
{
    return sys && plp && rom && sys->rdtp && (sys->tk_flg & 0x80) && (sys->sp_flg & 0x2) && !(sys->gm_flg & 0x9) && !(sys->cb_flg & 0x1) &&
           !*(int*)&sys->mn_mode0;
}

/* Sounds: enemies play their sound ids in SE bank 3, which the room's sound pack fills with its own
   types' sounds only, and ids collide between types (a Hunter's played zombie sounds). A type the room
   file has no record of gets the bank 3 of its source room (or of another room with the type) as an
   extra bank (pc/src/audio/snd_iop.c), released with the room; none to be had: its sounds are silent. */
int snd_xbank_load(int key);
void snd_xbank_release(void);
int pc_enemy_sound_type(int slot);      /* eneset.c */
static signed char sound_bank[100];     /* type -> extra bank, -1 none, 0 the room's */

static void sound_release(void) { memset(sound_bank, 0, sizeof(sound_bank)); snd_xbank_release(); }

int spawn_sound_bank(int enemy_no)      /* sdfunc.c RequestEnemySeBasic */
{
    int id = pc_enemy_sound_type(enemy_no);
    return id >= 0 && id < 100 ? sound_bank[id] : 0;
}

static void sound_load(const ActorModel* am)
{
    int k, b;
    if (am->id >= 100 || sound_bank[am->id]) return;
    for (k = 0; k < rom->ene_n; k++)
        if (rom->enep[k].id == am->id) return;   /* the room's own type: its bank has the sounds */
    b = snd_xbank_load(am->room);
    for (k = 0; !b && k < list_n; k++)   /* a source room without a bank 3 (Bandersnatch 2): another room of the type */
        if (entry(k)->id == am->id && entry(k)->room != am->room) b = snd_xbank_load(entry(k)->room);
    sound_bank[am->id] = (signed char)(b ? b : -1);
    if (b) host_log("spawn: type %d plays its sounds from extra bank %d\n", am->id, b);
    else host_log("spawn: type %d: no sound bank found, its sounds are silent\n", am->id);
}

/* Effect textures: enemy effects draw with the global effect textures (ids 0..29), except a few that
   take a room effect's (ids 30..99, 400+), which only rooms with such an effect record upload
   (room.c bhSetRoom): Alexia's fire, burning blood and fire spray (effsub2.c bhEff_E12_*, 80) and the
   Gulp Worm's sand (bhEff_E02_SandDust, 82 + (variant != 0)). Elsewhere sys->ef_tn[id] is 0 and they
   drew effect texture 0 (kanji). The source room's entry is uploaded as the room's own are: appended
   to the effect texlist and counted in ef_extn, so bhReleaseMainTexture releases it with them.
   Not found or no room: effect.c draws nothing with it (pc_fx_tex_missing). */
static int fx_tex_id(int id, int variant) { return id == 12 ? 80 : id == 2 ? 82 + (variant != 0) : 0; }

static void fx_load(const ActorModel* am, int variant)
{
    int fx = fx_tex_id(am->id, variant), n;
    const unsigned char* t;
    if (!fx || sys->ef_tn[fx]) return;   /* none needed, or the room has it */
    t = actor_fx_tex(am->room, fx);
    if (!t || sys->ef_ct + 16 > 450 || sys->ef_ct != sys->ef_ctb + sys->ef_extn || sys->ef_tlist.textures != sys->ef_tex) {
        host_log("spawn: type %d: effect texture %d %s, those effects are not drawn\n", am->id, fx, t ? "does not fit" : "not found");
        return;
    }
    n = bhSetMemPvpTexture(&sys->ef_tlist, (unsigned char*)t, sys->ef_ct);
    sys->ef_extn += n;
    sys->ef_tn[fx] = sys->ef_ct;
    sys->ef_ct += n;
    sys->ef_tlist.nbTexture = sys->ef_ct;
    sys->ef_pbkb[fx] = *(unsigned char*)(sys->ef_tlist.textures[sys->ef_tn[fx]].texaddr + 4);
    host_log("spawn: type %d: effect texture %d from RM_%04d (%d textures)\n", am->id, fx, am->room, n);
}

/* effect.c: effect `id` draws with a room effect texture this room has not uploaded (then it is
   drawn as nothing; logged once per effect id) */
int pc_fx_tex_missing(int id, int tex_id)
{
    static unsigned char logged[450];
    if (!((tex_id >= 30 && tex_id < 100) || (tex_id >= 400 && tex_id < 450)) || sys->ef_tn[tex_id]) return 0;
    if (id < 450 && !logged[id]) { logged[id] = 1; host_log("effect %d: room effect texture %d not in this room, not drawn\n", id, tex_id); }
    return 1;
}

/* undo bhSetEneMdl: the slot and the type's texture slots it took */
static void unrealize(BH_PWORK* ep)
{
    int j, v;
    for (j = 0; j < ep->mdl_n; j++)
        if (ep->mdl[j].flg & 0x200)
            for (v = 0; v < 16; v++)
                if (pc_et_lp(ep->id, ep->mdlver)[v] == ep->mdl[j].texP) pc_et_lp(ep->id, ep->mdlver)[v] = NULL;
    ep->flg = 0;
    bhCheckEneWorkNum();
}

/* types that cannot run outside their own setting (NULL: fine) */
const char* spawn_refused(int id)
{
    if (id == 24 || id == 30)   /* their code reads the parent that spawns them (lkwkp): NULL alone */
        return id == 24 ? "Spawn a Black Widow to create this enemy." : "This enemy is created by Alexia's third form.";
    if (id == 16)   /* corrupted memory outside his room in a test (crash in the audio mixer and the weapon loader) */
        return "Alfred can only appear in his boss arena.";
    if (id == 13 || id == 14)   /* the second form reads its partner (exp0 + 112), set up in the final fight: NULL alone */
        return "These Alexia forms need their original boss arena.";
    return NULL;
}

/* Realize `am` the way the room loader realizes a record, at `n` (position, angle, floor, variant),
   into slot `want` (-1: the first free one; else free slots below it are skipped). `mode`:
   SPAWN_LOADING from the room load (the randomizer), no readiness checks, its new textures released
   with the room record it replaces (bhReleaseMainTexture); SPAWN_DICTATED the host's (a co-op guest):
   no texture reserve, only the upload's own need. NULL or why not. */
const char* spawn_at(const ActorModel* am, const SpawnNote* n, int want, int mode, int* slot_out)
{
    const int loading = mode & SPAWN_LOADING, tkeep = mode & SPAWN_DICTATED ? TEX_HARD : TEX_KEEP,
              hkeep = mode & SPAWN_DICTATED ? HEAP_HARD : HEAP_KEEP;
    static char msg[160];
    ActorData a;
    ETTY_WORK rec;
    BH_PWORK* ep;
    unsigned char *mdl, *p, textured[16] = { 0 }, marked[PC_ENE_SLOTS];
    int k, slot = -1, free_n = 0, j, t, texn = 0, lists, dt, need, tex_free, heap_free, nm = 0, pool0 = pool_n;
    const char* how;

    if (!loading && !ready()) return "Finish the menu, scene or door transition first.";
    if (spawn_refused(am->id)) return spawn_refused(am->id);
    for (k = 0; k < PC_ENE_SLOTS; k++)   /* (helper code: each type's parts run its own, eneset.c pc_call_enemy) */
        if (!(ene[k].flg & 0x1)) { free_n++; if (slot < 0) slot = k; }
    if (want >= 0) {
        if (want >= PC_ENE_SLOTS || (ene[want].flg & 0x1)) { snprintf(msg, sizeof(msg), "Enemy position %d is already occupied.", want); return msg; }
        slot = want;
    }
    if (free_n < spawn_slots_needed(am->id) + SPAWN_SLOTS_FREE) {
        snprintf(msg, sizeof(msg), "Not enough enemy capacity: %d free; %s needs %d, with %d reserved.", free_n, am->name, spawn_slots_needed(am->id),
                 SPAWN_SLOTS_FREE);
        return msg;
    }
    if (!actor_find(am, &a)) return "Enemy data not found on the disc.";
    need = 12288 + 64 * 1024 + 256 * 1024 + room_tex_push() + 512 * 1024;   /* MN_WORKs, O_WORKs, the AI's work, the screens' copy, margin */
    if (sys->endp - sys->memp < need) {
        snprintf(msg, sizeof(msg), "Not enough room memory: %d KB free, %d KB needed.", (int)(sys->endp - sys->memp) >> 10, need >> 10);
        return msg;
    }
    if (POOL_SIZE - pool_n < a.mlen + a.nlen + 128) {
        snprintf(msg, sizeof(msg), "Not enough spawn memory: %d KB free, %d KB needed.", (POOL_SIZE - pool_n) >> 10, (a.mlen + a.nlen + 128) >> 10);
        return msg;
    }
    actor_tex_free(&tex_free, &heap_free);
    if (tex_free < tkeep || heap_free < hkeep) return tex_full(tex_free, heap_free);
    for (p = (unsigned char*)a.mdl, j = 0; (dt = *(int*)p) != -1; p += 4 + (dt > 0 ? dt : 0))   /* which sub-models bring a texlist */
        if (dt > 0 && *(int*)(p + 4) != 0x4B53414D && *(int*)(p + 4) != SKIN_MAGIC && j < 16) textured[j++] = *(int*)(p + 4 + 8) != -1;
    if (!j) return "This enemy cannot appear on its own.";
    if (!(mdl = pool_copy(a.mdl, a.mlen))) return "Not enough memory to spawn another enemy.";

    memcpy(&rec, a.rec, sizeof(rec));
    rec.type = n->type;
    rec.px = n->px; rec.py = n->py; rec.pz = n->pz;
    rec.ay = n->ay;
    rec.flr_no = n->flr;
    for (k = 0; k < slot; k++)   /* bhSetEnemy takes the first free slot: make that `slot` */
        if (!(ene[k].flg & 0x1)) { ene[k].flg |= 0x1; marked[nm++] = k; }
    bhSetEneMdl(mdl, &rec, rec.mdlver, slot);
    while (nm) ene[marked[--nm]].flg &= ~0x1;
    bhCheckEneWorkNum();
    ep = &ene[slot];
    for (j = t = 0, lists = 0; j < ep->mdl_n; j++) {   /* textures this room does not have yet */
        if (!textured[j]) continue;
        if ((ep->mdl[j].flg & 0x200) && t < a.ntex && a.tex[t] && ep->mdl[j].texP) { texn += ep->mdl[j].texP->nbTexture; lists++; }
        t++;
    }
    if (texn && (tex_free < texn + tkeep || (!loading && (tl_n + lists > TEX_LISTS || tn_n + texn > TEX_NAMES)))) {
        unrealize(ep);
        pool_n = pool0;
        return tex_full(tex_free, heap_free);
    }
    for (j = t = 0; j < ep->mdl_n; j++) {
        NJS_TEXLIST* l = ep->mdl[j].texP;
        if (!textured[j]) continue;
        if ((ep->mdl[j].flg & 0x200) && t < a.ntex && a.tex[t] && l) {
            bhSetMemPvpTexture(l, (unsigned char*)a.tex[t], 0);
            if (!loading) spawn_keep_texlist(l);
        }
        t++;
    }
    if (sys->emtp[am->id] && !empty_set(sys->emtp[am->id])) {
        ep->mnwP = ep->mnwPb = sys->emtp[am->id];
        how = "the room's motions";
    } else if (a.mtn && (p = pool_copy(a.mtn, a.nlen))) {
        bhSetEneMtn(p, ep, am->id);   /* 512 MN_WORKs from sys->memp, sys->emtp[id] */
        how = "motions loaded";
    } else {                          /* none (as bhFinishRoom): an empty set */
        if (!sys->emtp[am->id]) sys->emtp[am->id] = (MN_WORK*)bhGetFreeMemory(512 * sizeof(MN_WORK), 0);
        ep->mnwP = ep->mnwPb = sys->emtp[am->id];
        how = "no motions";
    }
    for (j = 0; j < ep->mdl_n; j++) sys->memp = bhKeepObjWork(&ep->mdl[j], sys->memp);
    bhCalcModel(ep);
    sound_load(am);
    fx_load(am, rec.type);
    spawned[slot] = am->id + 1;
    pc_enemy_mark_added();
    if (slot_out) *slot_out = slot;
    if (n->variant) variant_spawned(slot, n->variant);
    host_log("spawn: %s, type %d model %d variant %d from RM_%04d record %d, slot %d at %.1f %.1f %.1f, %d textures uploaded, "
             "%s (model %d KB, motions %d KB), %d KB room memory and %d KB spawn memory left, %d slots in use\n", am->name, am->id, am->mdlver, rec.type, am->room,
             am->rec, slot, rec.px, rec.py, rec.pz, texn, how, a.mlen >> 10, a.nlen >> 10, (int)(sys->endp - sys->memp) >> 10, (POOL_SIZE - pool_n) >> 10, (int)sys->ewk_n);
    return NULL;
}

/* ---------------------------------------------------------------- enemies added to the room (co-op) */
/* Everything that exists here but not in the room file (the F2 spawner, extra enemies): the host
   sends each to the guest, which realizes it into the same slot; a guest entering the room later
   or rejoining gets all of them again (spawn_replay). */
#define ADDED 64
static SpawnNote added[ADDED];
static unsigned char added_slot[ADDED];
static int added_n, added_gen = -1;

static void added_room(void) { if (added_gen != enemy_room_gen) { added_gen = enemy_room_gen; added_n = 0; } }

/* host or single player: spawn and keep it in the room's list; NULL or why not */
const char* spawn_add(const ActorModel* am, const SpawnNote* n, int* slot_out)
{
    int slot;
    const char* r = spawn_at(am, n, -1, 0, &slot);
    if (r) return r;
    added_room();
    if (added_n < ADDED) { added[added_n] = *n; added_slot[added_n++] = (unsigned char)slot; }
    coop_spawn_note(slot, n);
    if (slot_out) *slot_out = slot;
    return NULL;
}

void spawn_replay(void)
{
    int i;
    added_room();
    for (i = 0; i < added_n; i++) coop_spawn_note(added_slot[i], &added[i]);
    if (added_n) host_log("spawn: %d added enemies sent to the partner (it entered the room)\n", added_n);
}

/* guest: the host's added enemy `n` into slot `slot` of room stg/rom, now */
static void note_apply(int slot, const SpawnNote* n, int stg, int rom_no)
{
    ActorModel am = { "partner's enemy", n->room, n->rec, n->id, n->mdlver, 0, 0 };
    const char* r;
    int first;
    if (stg != sys->stg_no || rom_no != sys->rom_no) return;   /* another room's: it went with that room */
    if (slot < PC_ENE_SLOTS && (ene[slot].flg & 0x1) && ene[slot].id == n->id) return;   /* already here (a replay) */
    for (first = 0; first < PC_ENE_SLOTS && (ene[first].flg & 0x1); first++);
    r = spawn_at(&am, n, slot, SPAWN_DICTATED, NULL);
    host_log("spawn: partner's enemy type %d model %d into slot %d (first free here: %d): %s\n", n->id, n->mdlver, slot, first, r ? r : "done");
}

/* Those that came while our room did not run (a door, an event, a menu) wait here and are placed
   from coop_frame once it runs again (spawn_pending), in order; the event stream goes on meanwhile
   (before, the stream stopped behind such a spawn for up to 30 s). Dropped after PEND_FRAMES. */
#define PEND 64
#define PEND_FRAMES 900
static struct { SpawnNote n; int slot, stg, rom, t; } pend[PEND];
static int pend_n;

void spawn_pending(int run)
{
    int k;
    if (!run || !sys || !plp || (sys->tk_flg & 0x8)) { pend_n = 0; return; }   /* session over, title: superseded by the next join */
    if (!pend_n) return;
    if (!ready()) {
        for (k = 0; k < pend_n && ++pend[k].t < PEND_FRAMES; k++);
        if (k == pend_n) return;
        host_log("spawn: %d partner's enemies dropped (not in a room for too long)\n", pend_n);
        pend_n = 0;
        return;
    }
    for (k = 0; k < pend_n; k++) note_apply(pend[k].slot, &pend[k].n, pend[k].stg, pend[k].rom);
    pend_n = 0;
}

/* guest: an EV_SPAWN of the host (always taken: 1) */
int spawn_note_apply(int slot, const SpawnNote* n, int stg, int rom_no)
{
    if (n->pad == SPAWN_NOTE_KEPT) { enemy_rand_kept(n->room, slot, n->id); return 1; }   /* (also at the title: before the game state) */
    if (!sys || !plp || (sys->tk_flg & 0x8)) return 1;   /* title: superseded by the next join */
    if (!ready() || pend_n) {
        if (pend_n == PEND) { host_log("spawn: too many partner's enemies waiting: the oldest dropped\n"); memmove(pend, pend + 1, sizeof(pend[0]) * --pend_n); }
        pend[pend_n].n = *n; pend[pend_n].slot = slot; pend[pend_n].stg = stg; pend[pend_n].rom = rom_no; pend[pend_n++].t = 0;
        if (!ready()) host_log("spawn: partner's enemy for slot %d waits until our room runs (%d waiting)\n", slot, pend_n);
        if (ready()) spawn_pending(1);   /* (in order: the waiting ones first) */
        return 1;
    }
    note_apply(slot, n, stg, rom_no);
    return 1;
}

/* the F2 entry `i` at distance `dist` in front of the player, facing it */
static const char* spawn(int i, float dist, int* slot_out)
{
    static char msg[160];
    const ActorModel* am;
    const EnemyVariant* v = i >= list_n ? variant_at(i - list_n) : NULL;
    SpawnNote n = { 0 };
    const char* r;
    if (i < 0 || (i >= list_n && !v)) return "Enemy unavailable.";
    if (v && !(am = variant_base(v))) return "This enemy variant is unavailable.";
    if (!v) am = entry(i);
    if (!ready()) return "Finish the current action before spawning an enemy.";
    if (coop_is_guest()) return "Only the co-op host can spawn enemies.";
    n.room = am->room; n.rec = am->rec; n.id = am->id; n.mdlver = am->mdlver;
    n.variant = v ? v->id : 0;
    /* variant 0, the free-roaming one: others wait for the room's enemy floor areas (Cerberus 1, 2),
       lie, crawl or eat (zombie 2..15) or are set pieces; ant C, tentacle and core keep theirs */
    if (am->id == 71 || am->id == 29 || am->id == 53) {
        ActorData a;
        if (actor_find(am, &a)) n.type = ((const ETTY_WORK*)a.rec)->type;
    }
    n.px = plp->px - dist * njSin(plp->ay);
    n.pz = plp->pz - dist * njCos(plp->ay);
    n.py = plp->py;
    n.ay = (short)((plp->ay + 0x8000) & 0xFFFF);
    n.flr = plp->flr_no;
    if ((r = spawn_add(am, &n, slot_out))) return r;
    snprintf(msg, sizeof(msg), "Spawned %s.", v ? v->name : am->name);
    return msg;
}

/* F2 button (game thread) */
const char* spawn_enemy(int i) { const char* r = spawn(i, 30.0f, NULL); host_log("spawn: %s\n", r); return r; }

/* the address space still free for allocations: 16 MB blocks until none is left (then freed) */
static void address_headroom(void)
{
    static void* b[256];
    int n = 0, big = 0;
    size_t sz;
    for (sz = (size_t)1 << 30; sz >= (16u << 20) && !big; sz >>= 1)   /* the largest block, to 16 MB */
        if ((b[0] = malloc(sz))) { big = (int)(sz >> 20); free(b[0]); }
    while (n < 256 && (b[n] = malloc(16u << 20))) n++;
    printf("survey: address space left: %d MB in 16 MB blocks, largest block at least %d MB\n", n * 16, big);
    while (n) free(b[--n]);
}

/* Test aid: RECVX_SPAWN_SURVEY="stg:rom,..." tries every spawner entry in each room and logs
   "survey:" lines (the room's budgets on arrival, each result). Each entry is realized as the F2
   spawner does (spawn_at) and undone before its Init runs (slot, textures, room memory, motion set),
   so each one meets the room as loaded. RECVX_SPAWN_SURVEY_ALL=1: the spawns stay (F2 path), so the
   room shows how many different types fit together. A room not ready within ~50 s is skipped.
   At the end: the free address space, then exit. */
static void survey(void)
{
    static const char* rooms = (const char*)-1;
    static int all, ei, wait, in, sent, ok_n, tried;
    int stg, rno, n = 0, tex, heap, slot = -1, tl0 = tl_n, tn0 = tn_n, pool0 = pool_n;
    unsigned char* memp0 = sys->memp;
    const char* r;
    if (rooms == (const char*)-1) { rooms = getenv("RECVX_SPAWN_SURVEY"); all = getenv("RECVX_SPAWN_SURVEY_ALL") != NULL; }
    if (!rooms) return;
    if (sscanf(rooms, "%d:%d%n", &stg, &rno, &n) != 2) { printf("survey: done\n"); address_headroom(); fflush(NULL); _exit(0); }   /* (no GL teardown under the render thread) */
    if (!sent) {
        if (!ready()) return;
        cheat_room_warp(stg, rno, 0, -1);
        sent = 1; wait = in = 0;
        return;
    }
    if (sys->stg_no != stg || sys->rom_no != rno || !ready() || ++in < 30) {
        if (++wait < 1500) return;
        printf("survey: room %d/%d skipped (not ready)\n", stg, rno);
        ei = list_n;
    } else if (ei < list_n) {
        const ActorModel* am = entry(ei);
        if (ei == 0) {
            actor_tex_free(&tex, &heap);
            printf("survey: room %d/%d (RM_%04d): %d texture entries and %d KB heap free, %d KB room memory free (screens copy %d KB), %d enemy slots in use\n",
                   stg, rno, room_file_now(), tex, heap >> 10, (int)(sys->endp - sys->memp) >> 10, room_tex_push() >> 10,
                   (int)sys->ewk_n);
        }
        if (all) r = spawn(ei, 30.0f, NULL);
        else {
            SpawnNote note = { am->room, am->rec, am->id, am->mdlver, 0, plp->flr_no, 0, plp->px, plp->py, plp->pz + 30.0f, 0, 0 };
            MN_WORK* emtp0 = sys->emtp[am->id];
            r = spawn_at(am, &note, -1, 0, &slot);
            if (!r) {
                r = "spawned (undone)";
                unrealize(&ene[slot]);
                while (tl_n > tl0) njReleaseTexture(&tl[--tl_n]);
                tn_n = tn0;
                bhGarbageTexture(NULL, 0);
                sys->emtp[am->id] = emtp0;
                sys->memp = memp0;
                pool_n = pool0;
                spawned[slot] = 0;
            }
        }
        tried++;
        ok_n += r[0] == 's';
        printf("survey: room %d/%d entry %d %s (type %d model %d): %s\n", stg, rno, ei, am->name, am->id, am->mdlver, r);
        if (++ei < list_n) return;
    }
    actor_tex_free(&tex, &heap);
    printf("survey: room %d/%d at the end: %d texture entries and %d KB heap free, %d KB room memory and %d KB spawn memory free, %d enemy slots in use\n",
           stg, rno, tex, heap >> 10, (int)(sys->endp - sys->memp) >> 10, (POOL_SIZE - pool_n) >> 10, (int)sys->ewk_n);
    printf("survey: %d of %d spawned so far\n", ok_n, tried);
    rooms += n + (rooms[n] == ',');
    ei = 0; sent = 0;
}

/* game thread, every frame in game: RECVX_SPAWN="gameframe:id[.mdlver][@dist],..." spawns the first
   list entry of type id (and model version) at that game frame (frames since start, as the other
   test aids); "gameframe:v<variant id>[@dist]" an enemy variant. With it set, what was spawned is traced:
   every 30 game frames, and hits/deaths. */
void spawn_frame(int clock)
{
    static const char* env = (const char*)-1;
    static int trace, last_trace, last_hp, ene_hp[PC_ENE_SLOTS], trace_room = -1;
    int f, id, ver = -1, n = 0, k, slot_new = -1, vid = 0;
    float dist = 30.0f;
    if (env == (const char*)-1) trace = (env = getenv("RECVX_SPAWN")) != NULL;
    if (tl_n && room_key != room_file_now() && !(sys->gm_flg & 0x9) && !(sys->cb_flg & 0x1))
        release_textures();   /* a new room loaded without bhReleaseMainTexture (enemy variants' models may be loaded for it) */
    enemy_extras_frame();
    survey();
    if (env && (sscanf(env, "%d:%d%n", &f, &id, &n) == 2 || (sscanf(env, "%d:v%d%n", &f, &id, &n) == 2 && (vid = id))) && clock >= f && ready()) {
        const char* e = env + n;
        int m;
        if (*e == '.' && sscanf(e, ".%d%n", &ver, &m) == 1) e += m;
        if (*e == '@' && sscanf(e, "@%f%n", &dist, &m) == 1) e += m;
        if (vid) for (k = 0; k < variant_count() && variant_at(k)->id != vid; k++) {}
        else for (k = 0; k < list_n && !(entry(k)->id == id && (ver < 0 || entry(k)->mdlver == ver)); k++) {}
        if (vid) printf("test: spawn variant %d at game frame %d: %s\n", vid, clock, k < variant_count() ? spawn(list_n + k, dist, &slot_new) : "no such variant");
        else printf("test: spawn type %d at game frame %d: %s\n", id, clock, k < list_n ? spawn(k, dist, &slot_new) : "no such type");
        if (slot_new >= 0) ene_hp[slot_new] = ene[slot_new].hp;
        if (slot_new >= 0) g_cheat.spawn_idx = vid ? list_n + k : k;   /* F2 shows the entry spawned (screenshots) */
        env = *e == ',' ? e + 1 : NULL;
    }
    if (!trace) return;
    if (ready() && room_file_now() != trace_room) {   /* the budgets a room arrives with */
        int tex, heap;
        trace_room = room_file_now();
        actor_tex_free(&tex, &heap);
        printf("test: game frame %d in RM_%04d: %d texture entries and %d KB heap free, %d KB room memory and %d KB spawn memory free, %d slots in use\n",
               clock, trace_room, tex, heap >> 10, (int)(sys->endp - sys->memp) >> 10, (POOL_SIZE - pool_n) >> 10, (int)sys->ewk_n);
    }
    if (plp->hp < last_hp && last_hp - plp->hp < 400) printf("test: game frame %d player hit, hp %d -> %d\n", clock, last_hp, plp->hp);
    last_hp = plp->hp;
    for (k = 0; k < PC_ENE_SLOTS; k++) {
        BH_PWORK* ep = &ene[k];
        float dx, dz;
        if (!spawned[k] || !(ep->flg & 0x1) || ep->id != spawned[k] - 1) continue;
        dx = ep->px - plp->px; dz = ep->pz - plp->pz;
        if (ep->hp != ene_hp[k] || clock - last_trace >= 30)
            printf("test: game frame %d spawn slot %d id %d mode %d/%d/%d mtn %d frame %d hp %d%s flg %#x stflg %#x at %.0f %.0f %.0f dist %.1f\n",
                   clock, k, ep->id, ep->mode0, ep->mode1, ep->mode2, ep->mtn_no, ep->frm_no >> 16, ep->hp, ep->hp < ene_hp[k] ? " (hit)" : "",
                   ep->flg, ep->stflg, ep->px, ep->py, ep->pz, sqrtf(dx * dx + dz * dz));
        ene_hp[k] = ep->hp;
    }
    if (clock - last_trace >= 30) {
        if (clock / 30 % 3 == 0) {   /* every 90 game frames: all active slots (id, mode0), e.g. to compare co-op instances */
            printf("test: game frame %d room %d/%d slots:", clock, sys->stg_no, sys->rom_no);
            for (k = 0; k < (int)sys->ewk_n && k < PC_ENE_SLOTS; k++)
                if (ene[k].flg & 0x1) printf(" %d:%d/%d", k, ene[k].id, ene[k].mode0);
            printf("\n");
        }
        last_trace = clock;
    }
}
