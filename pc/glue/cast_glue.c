/* The cutscene cast randomizer in the game (docs/ACTORS.md "Cutscene cast"; the choices: pc/src/host/cast.c).
 *
 * At the end of every room load (enemyopt_glue.c enemy_room_finished) each room record of a story character
 * (cast_character: Alfred, Steve, Rodrigo, ...) or of a creature only a cutscene shows (cast_creature) is given another model (cast_model; a zombie
 * only a cutscene shows, cast_scene_zombie: a creature's, cast_crowd_model), loaded into a display
 * slot of the appearance system (ACTOR_CAST + k, one per model the room shows; the retargeting of
 * actor_glue.c). Then, while a record has one (cast_n), for the whole room visit (the cutscene and after it, where
 * the character stays in the room); cast_scene only marks the cutscene for the trace:
 *  - eneset.c bhDrawEnemy -> actor_put_enemy -> cast_draw: the model is drawn in the record's place, posed
 *    from its skeleton;
 *  - coop_glue.c coop_control_remote -> actor_double_tick -> cast_tick, every tick before the objects follow
 *    their links: its face takes the record's lip sync and eyes, and the hand pieces and props the script
 *    links to its hands go to the model's hands (as for the cutscene double).
 * Nothing else changes: the record keeps its type, its motions, collision and script. Claire and Chris are never
 * cast: the doubles of the character played are "You appear as"'s (actor_doubles), and the other one's records are
 * given the player's look for that character instead of a pick (coop_look_model; cast.c cast_role). With "Character
 * Models" Per Character (g_appear_per) that part runs with the cast off too: Claire's and Chris's records only.
 */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/face.h"
#include "../src/host/actors.h"
#include "../src/host/cast.h"
#include "../src/host/enemies.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void host_log(const char* fmt, ...);
extern int coop_on;
extern int cast_n;                      /* actor_glue.c: hooks run while > 0 */
unsigned coop_partner_import_id(void);  /* coop_glue.c: the partner's cast_import_id, 0 none */
int coop_look_model(int c);             /* coop_glue.c: the player's look for Claire or Chris as a model, -1 themselves */
int tp_count(void);                     /* teleport.c: reads the room files once (and the voice lines: voice_scan_room) */

#define CSLOTS (ACTOR_SLOTS - ACTOR_CAST)
#define RECS 128
static signed char rec_slot[RECS];      /* record -> cast slot, -1: drawn as itself */
static unsigned char rec_id[RECS], rec_ver[RECS], rec_chr[RECS];   /* what it was when given its model: type, model version, character */
static short slot_model[CSLOTS] = { -1, -1, -1, -1, -1, -1 };
_Static_assert(CSLOTS == 6, "slot_model");
#define HIDDEN_MAX 64
static NJS_CNK_OBJECT* hidden[HIDDEN_MAX];   /* hand pieces and head parts hidden this tick (hands_hide, parts_hide) */
static int hidden_n;
static unsigned char hid_logged[4];   /* (test aid: objects logged hidden in this room) */
static unsigned char lost_logged[RECS / 8];   /* records logged no longer what was given a model (cast_tick) */

/* records whose scene needs their own look */
static const struct { unsigned short room; unsigned char rec; const char* why; } kept[] = {
    { 7030, 1, "Alexia asleep in her ice capsule" },
};

/* the imported models count for the pool: ours, and in co-op only while the partner has the same list */
static int import_count(const EnemyOpts* o)
{
    int n = __atomic_load_n(&actor_import_n, __ATOMIC_ACQUIRE);
    if (!o->cast_imports || !n) return 0;
    if (coop_on && coop_partner_import_id() != cast_import_id()) return 0;
    return n;
}

static int shown(int slot) { return slot >= 0 && slot < CSLOTS && slot_model[slot] >= 0 && actor_shown(ACTOR_CAST + slot); }

/* record k still holds what was given a model */
static int still(const BH_PWORK* ep, int k) { return (ep->flg & 0x1) && ep->id == rec_id[k] && ep->mdlver == rec_ver[k] && shown(rec_slot[k]); }

/* A cutscene runs: an event camera shows the room (cam.flg 0x2, which light.c mirrors in st_flg 0x1000000: the
   event lights), the game's cinema mode is on (cb_flg 0x4: the scenes the skip button ends), or an event has hidden
   the player (its double acts). In-engine scenes are shot with event cameras, also the ones the played character
   acts in without the cinema mode (Chris and Rodrigo in 7220); gameplay uses the room's own cameras. So a character
   who stays in the room afterwards (Rodrigo at his desk after the opening) is itself again from the cut back to the
   player's camera on: no shot shows the change. */
static int cast_scene(void) { return sys && ((sys->st_flg & 0x1000000) || (sys->cb_flg & 0x4) || (plp && (plp->stflg & 0x1000000))); }

void cast_release(void)
{
    int k;
    for (k = 0; k < CSLOTS; k++)
        if (slot_model[k] >= 0) { actor_set(ACTOR_CAST + k, -1); slot_model[k] = -1; }
    memset(rec_slot, -1, sizeof(rec_slot));
    cast_n = 0;
    hidden_n = 0;   /* (the room's objects go with the room: nothing to show again) */
    memset(hid_logged, 0, sizeof(hid_logged));
    memset(lost_logged, 0, sizeof(lost_logged));
}

/* enemyopt_glue.c, the end of the room load */
void cast_room(const EnemyOpts* o)
{
    int want[RECS], k, j, n = 0, room = room_file_now(), imports;
    char custom[40];
    unsigned scene;
    if (getenv("RECVX_CAST_TRACE")) {   /* test aid: the game's state at each room load (the same with the cast on and off) */
        const unsigned char* b = (const unsigned char*)sys->ev_flg;
        unsigned h = 2166136261u;
        for (k = 0; k < (int)((const unsigned char*)sys->ply_stflg - b); k++) h = (h ^ b[k]) * 16777619u;   /* story, item, kill, key, map flags and items */
        host_log("cast: room %04d loaded: state %08x, player %.2f %.2f %.2f hp %d\n", room, h, plp ? plp->px : 0.0f, plp ? plp->py : 0.0f, plp ? plp->pz : 0.0f, plp ? plp->hp : 0);
    }
    pc_voice_stop();
    if (o && o->voices) tp_count();   /* "Randomize Voice Lines": the lines are read with the room files, once, on a thread of their own */
    if (!o || !rom || (!o->cast && !g_appear_per)) { cast_release(); return; }   /* ("Per Character": Claire's and Chris's looks without the cast) */
    imports = import_count(o);
    scene = o->cast_assign ? (unsigned)room + 1 : 0;
    for (k = 0; k < rom->ene_n && k < RECS; k++) {
        BH_PWORK* ep = &ene[k];
        int c;
        want[k] = -1;
        if ((c = cast_character(ep->id, ep->mdlver)) < 0) c = cast_creature(room, k, ep->id);   /* (a creature only a cutscene shows) */
        if (c < 0 && cast_scene_zombie(room, k, ep->id)) c = CAST_CROWD;   /* (a zombie only a cutscene shows) */
        if (!(ep->flg & 0x1) || !ep->mlwP || c < 0 || (c >= 2 && !o->cast)) continue;   /* (the character played's doubles too: played()) */
        for (j = 0; j < (int)(sizeof(kept) / sizeof(kept[0])) && !(kept[j].room == room && kept[j].rec == k); j++);
        if (j < (int)(sizeof(kept) / sizeof(kept[0]))) { host_log("cast: room %04d record %d (type %d) stays: %s\n", room, k, ep->id, kept[j].why); continue; }
        want[k] = c < 2 ? coop_look_model(c) : c == CAST_CROWD ? cast_crowd_model((unsigned)o->seed, scene, ep->id, ep->mdlver, o->cast_all, enemy_cast_off())
                : cast_model((unsigned)o->seed, scene, c, o->cast_all, imports, enemy_cast_off());   /* (Claire's, Chris's: never cast) */
        rec_chr[k] = (unsigned char)c;
    }
    cast_release();   /* (normally all went with the last room's textures; their buffer is free again) */
    for (k = 0; k < rom->ene_n && k < RECS; k++) {
        const ActorModel* am;
        const char* name;
        int s = -1;
        if (want[k] < 0) continue;
        am = want[k] >= ACTOR_PART ? NULL : actor_model(want[k]);
        name = am ? am->name : "a player model";
        for (j = 0; j < CSLOTS && s < 0; j++) if (slot_model[j] == want[k]) s = j;
        for (j = 0; j < CSLOTS && s < 0; j++)
            if (slot_model[j] < 0) {
                if (actor_set(ACTOR_CAST + j, want[k]) && actor_rigged(ACTOR_CAST + j)) { slot_model[j] = (short)want[k]; s = j; }
                else { actor_set(ACTOR_CAST + j, -1); break; }
            }
        if (s < 0) {
            host_log("cast: room %04d record %d (type %d model %d): %s %s, drawn as itself\n", room, k, ene[k].id, ene[k].mdlver, name,
                     j >= CSLOTS ? "has no display slot left" : "cannot be loaded or mapped");
            continue;
        }
        rec_slot[k] = (signed char)s;
        rec_id[k] = (unsigned char)ene[k].id;
        rec_ver[k] = ene[k].mdlver;
        if (rec_chr[k] >= CAST_CHARACTERS) actor_cast_hands(ACTOR_CAST + s);   /* (a creature: no hand pieces of the script's) */
        cast_n++;
        n++;
        host_log("cast: room %04d record %d (type %d model %d) -> %s (slot %d)\n", room, k, ene[k].id, ene[k].mdlver, name, s);
    }
    if (o->cast_all == 2) snprintf(custom, sizeof(custom), "custom models: %d checked", cast_custom_count(0, enemy_cast_off(), imports, NULL));
    if (!o->cast) { host_log("cast: room %04d: %d records of Claire or Chris drawn as your look for them (Per Character)\n", room, n); return; }
    host_log("cast: room %04d: %d records drawn as other models in cutscenes (seed %d, %s, %s%s)\n", room, n, o->seed,
             o->cast_assign ? "random per cutscene" : "fixed", o->cast_all == 2 ? custom : o->cast_all ? "all models" : "cutscene models", imports ? ", imported models" : "");
}

/* how record k is drawn now (cast.c cast_role), read when it is drawn: the character played now (a story switch changes
   sys->ply_id after the room load: the end of bhFinishRoom); the one a switch the script has set (PlayerChangeSet:
   cb_flg 0x80, cng_pid) is about to make played, so the scene around the switch already counts it; and in co-op the
   partner's (its own part file), whom a scene of ours may show while its game plays it. Claire's and Chris's records
   have the player's look for them as their model (cast_room), shown unless they are the character played (then the
   double shows it, actor_put_enemy), whether or not the game has switched to them yet (Chris in 7240 and 7220 reached
   without 6100's switch: the game still plays Claire). */
int coop_partner_ply(void);             /* coop_glue.c: -1 none */
static int role(int k) { return cast_role(rec_chr[k], sys->ply_id & 3, (sys->cb_flg & 0x80) ? sys->cng_pid & 3 : -1, coop_partner_ply()); }
static int played(int k) { return role(k) == CAST_OWN; }

/* record k is drawn as its model now */
static int as_model(const BH_PWORK* ep, int k) { return k >= 0 && k < RECS && rec_slot[k] >= 0 && !played(k) && still(ep, k); }

/* The record's hand pieces. The cast's cutscene models have no hands: the room script links a hand piece (the
   character's own gloved fist, open hand or hand holding something) to the record's hand node and swaps it per
   gesture, and actor_act puts those nodes where the model's hands are, turned as them. A model with hands of its own
   (a player model, a creature, a zombie, an imported model) showed the record's piece over or beside its own hand;
   one without that hand at all (the Bandersnatch's left) showed it floating at the wrist. There the record's pieces are
   hidden while it is drawn as the model (each piece's mesh, NJD_EVAL_HIDE, as actor_weapon_hands does for the weapons'
   hand pieces), and shown again at the next tick. Only one-node pieces are hands alone: a piece of more nodes holds a
   prop in the same mesh (Claire's hand with the lighter, Rodrigo's with what he hands over) and stays, so the prop does
   not go with it; items the script links to the hand are other objects and stay too. */

/* actor_double_tick, every tick before cast_tick: the pieces and head parts hidden last tick shown again */
void cast_show(void)
{
    while (hidden_n) hidden[--hidden_n]->evalflags &= ~0x8u;
}

extern int cast_hands;                  /* actor_glue.c: 0 (test aid RECVX_CAST_HANDS=0): the record's pieces kept */
static void hands_hide(BH_PWORK* ep, int who)
{
    int side, node, o;
    if (!cast_hands) return;
    for (side = 0; side < 2; side++) {
        if (!actor_own_hand(who, side, &node)) continue;   /* (no hand of its own: the record's piece is the model's hand) */
        for (o = 4; o < 32; o++) {   /* (0..3: the player's weapons and head part) */
            O_WRK* op = &sys->obwp[o];
            NJS_CNK_OBJECT* m = op->mlwP && op->mlwP->objP ? &op->mlwP->objP[0] : NULL;
            if (!(op->flg & 0x1) || !(op->flg & 0x80) || op->lkwkp != (unsigned char*)ep || op->lkono != node || !m || op->mlwP->obj_num != 1) continue;
            if ((m->evalflags & 0x8) || hidden_n == HIDDEN_MAX) continue;   /* (hidden already: by the game, or a piece shared by two records) */
            m->evalflags |= 0x8;
            hidden[hidden_n++] = m;
            if (getenv("RECVX_CAST_TRACE") && !(hid_logged[o >> 3] & (1 << (o & 7)))) {   /* test aid: once per object and room */
                hid_logged[o >> 3] |= (unsigned char)(1 << (o & 7));
                host_log("cast: room %04d record %d (type %d): its hand piece (object %d, id %d) on node %d hidden: the model has %s\n", room_file_now(),
                         (int)(ep - ene), ep->id, o, op->id, node, actor_own_hand(who, side, &node) == 2 ? "no such hand" : "a hand of its own");
            }
        }
    }
}

/* The record's own parts on its head: Claire's ponytail (object id 1200, 4 nodes, on head node 5 of her cutscene
   record; the player's head part, sys->obwp[2], when a script links it there) and Wesker's sunglasses (id 1127 on his
   event model 47 in 8030) are objects of their own, linked to the head, and kept swinging behind or sitting on the
   model shown in the record's place. Hidden as the hand pieces are (every node's mesh, NJD_EVAL_HIDE) while the
   record is drawn as its model, shown again at the next tick (the record drawn as itself). The other objects linked
   to a record sit on its hands. The same for the double of the character played drawn as the player's look
   (cast_double_parts). Kept on a player file of the record's own character (chr): it has no head part of its own, the
   record's is its. From object o: 2, the double 3 (the player's head part there is the look's: coop_glue.c
   self_cine_hair). */
extern int cast_parts;                  /* actor_glue.c: 0 (test aid RECVX_CAST_PARTS=0): the parts kept */
static void parts_hide(BH_PWORK* ep, int who, int chr, int o)
{
    int i, m = actor_shown_model(who);
    if (!cast_parts || (m >= ACTOR_PART && ((m - ACTOR_PART) & 3) == chr)) return;
    for (; o < 32; o++) {   /* (0, 1: the player's weapons) */
        O_WRK* op = &sys->obwp[o];
        if (!(op->flg & 0x1) || !(op->flg & 0x80) || op->lkwkp != (unsigned char*)ep || !actor_src_head(who, op->lkono) || !op->mlwP || !op->mlwP->objP) continue;
        for (i = 0; i < (int)op->mlwP->obj_num && hidden_n < HIDDEN_MAX; i++) {
            NJS_CNK_OBJECT* m = &op->mlwP->objP[i];
            if (m->evalflags & 0x8) continue;   /* (hidden already) */
            m->evalflags |= 0x8;
            hidden[hidden_n++] = m;
        }
        if (getenv("RECVX_CAST_TRACE") && !(hid_logged[o >> 3] & (1 << (o & 7)))) {   /* test aid: once per object and room */
            hid_logged[o >> 3] |= (unsigned char)(1 << (o & 7));
            host_log("cast: room %04d record %d (type %d): its part (object %d, id %d, %d nodes) on head node %d hidden\n", room_file_now(), (int)(ep - ene), ep->id, o, op->id,
                     (int)op->mlwP->obj_num, op->lkono);
        }
    }
}

/* actor_double_tick: every character shown in a cutscene, posed with its face and hands */
void cast_tick(void)
{
    static int was = -1;
    int k, now = cast_scene();
    if (now != was && getenv("RECVX_CAST_TRACE")) {   /* test aid: where each cast record is when the cutscene starts or ends */
        host_log("cast: %s at game frame %d\n", now ? "a cutscene: the cast as their models" : "gameplay: the cast as themselves", (int)sys->gfrm_ct);
        for (k = 0; k < RECS; k++)
            if (rec_slot[k] >= 0) host_log("cast:  record %d (type %d) at %.1f %.1f %.1f%s\n", k, ene[k].id, ene[k].px, ene[k].py, ene[k].pz, (ene[k].stflg & 0x1000000) ? ", hidden" : "");
    }
    was = now;
    for (k = 0; k < (int)sys->ewk_n && k < RECS; k++) {
        BH_PWORK* ep = &ene[k];
        if (shown(rec_slot[k]) && (ep->flg & 0x1) && !still(ep, k) && !(lost_logged[k >> 3] & (1 << (k & 7)))) {   /* picked, not drawn: a
            record changed after cast_room (the enemy randomizer's other look once ran after it); once per record and room */
            lost_logged[k >> 3] |= (unsigned char)(1 << (k & 7));
            host_log("cast: room %04d record %d: given a model as type %d model %d, now type %d model %d: drawn as itself\n", room_file_now(), k,
                     rec_id[k], rec_ver[k], ep->id, ep->mdlver);
        }
        if (!as_model(ep, k) || (ep->stflg & 0x41000000) || !ep->mlwP) continue;
        actor_act(ACTOR_CAST + rec_slot[k], ep);
        hands_hide(ep, ACTOR_CAST + rec_slot[k]);
        parts_hide(ep, ACTOR_CAST + rec_slot[k], rec_chr[k], 2);
    }
}

/* actor_double_tick: the head parts of the double ep of character pid posed as the player's look hidden (Claire's
   ponytail on her record in 9150, 9320, 3210) */
void cast_double_parts(BH_PWORK* ep, int pid) { parts_hide(ep, ACTOR_DOUBLE, pid, 3); }

/* A part of a creature's body is a record of its own linked to it (flg 0x80, lkwkp, lkono): Nosferatu's heart in 6040
   (type 53, record 5, on his chest node 3, beating, a morphing record) showed on the chest of the model in his place.
   While the creature is drawn as its model, the records linked to it are not drawn (bhDrawEnemy asks cast_draw for
   them, morphing or not; the record still runs, only its drawing is left out). Only for the creatures: a story
   character's linked records could be what it holds. */
static int creature_part(const BH_PWORK* ep)
{
    const BH_PWORK* l = (const BH_PWORK*)ep->lkwkp;
    int k;
    if (!(ep->flg & 0x80) || l < ene || l >= ene + RECS || (const unsigned char*)l != (const unsigned char*)&ene[l - ene]) return 0;
    k = (int)(l - ene);
    return rec_chr[k] >= CAST_CHARACTERS && as_model(l, k);
}

/* actor_put_enemy: 1 = drawn */
int cast_draw(BH_PWORK* ep)
{
    int k = (int)(ep - ene);
    if (creature_part(ep)) return 1;   /* (drawn: as nothing) */
    return as_model(ep, k) && actor_put_as(ACTOR_CAST + rec_slot[k], ep);
}

/* ---------------------------------------------------------------- "Randomize Voice Lines" (cast.h voices.c; docs/ACTORS.md "Voice lines")
   A line's speaker (the record the next LipSet moves the mouth of, else the VoiceOn's record) shown as another story
   character's model (a cast pick, the player's look for Claire or Chris, the double's "Character Model") says a line of
   that character instead (voice_pick: from the seed, the room file and the line), played from that line's disc's archive
   (partitions VOICE_PT + 0/1), and the LipSet after it plays that line's lip entry. The script waits for it as long as for
   its own line (pc_voice_playing): a longer one goes on over what follows (until the next line stops it), a shorter one
   leaves a silence; the scene keeps its timing. Subtitles stay the script's (the original line's text). */
int coop_insn_len_raw(const unsigned char* p);   /* net/coop_proto.c */
int adx_length_ms(int ptid, int fid);            /* audio/adx.c */
int ADXF_LoadPartitionNw(int ptid, char* fname, void* dir, void* ptinfo);
int ADXF_GetPtStat(int ptid);
int actor_double_shown(const BH_PWORK* ep);      /* actor_glue.c */
uint64_t SDL_GetTicks(void);
extern int PatId[4];
#define VOICE_PT 28
static struct {
    int on, rec, ms;                    /* on: 1 an inserted line plays, 2 the original's time is over */
    const unsigned char* lip;           /* the inserted line's lips for the LipSet after it (pc_voice_lip) */
    uint64_t t0;
    int mouth, mroom;                   /* "Talking Mouths": record + 1 whose mouth we started (mouth_add), in room file mroom */
    const void* mexp;                   /* ... its face state then, and the lips */
    const unsigned char* mlip;
} vl;

/* the display slot record k is drawn by now (ACTOR_CAST + its cast slot, ACTOR_DOUBLE), -1 itself */
static int shown_slot(int k)
{
    if (cast_n && as_model(&ene[k], k)) return ACTOR_CAST + rec_slot[k];
    return actor_double_shown(&ene[k]) >= 0 ? ACTOR_DOUBLE : -1;
}

/* the model record k is drawn as now, -1 itself */
static int shown_model(int k)
{
    int who = shown_slot(k);
    return who < 0 ? -1 : actor_shown_model(who);
}

/* "Talking Mouths" (g_voice_mouths; docs/ACTORS.md "Voice lines"): an inserted line in place of one whose script moves no
   mouth (no LipSet after it) moves the speaker's mouth with the inserted line's own lips (they came with it from its scene,
   so they fit its sound), where the speaker is sure (voice_sure: the VoiceOn's record, in a script that names its
   speakers), the record is drawn as a model with a mouth (actor_talks) and its mouth is not moving already (the scene's
   lips stay). It sets the record's lip state as LipSet and LipStart do; every model shown in its place takes it from there
   (actor_glue.c face_step, imp_face). The next line, VoiceOff, a skip and the script's own LipSet for the record end it
   (mouth_stop). Only the inserted lines for now: the game's own lines without lips could call it too, with lips of their
   own making. NULL: started, else why not. */
static const char* mouth_add(int sp, int vid, const unsigned char* lip)
{
    BH_PWORK* ep = &ene[sp];
    int who = shown_slot(sp);
    if (!g_voice_mouths) return "Talking Mouths is off";
    if (!voice_sure(room_file_now(), vid, sp)) return "the speaker is not sure";
    if (who < 0 || !(ep->flg & 0x1) || (ep->stflg & 0x41000000) || !ep->mlwP) return "its model is not drawn";
    if (!actor_talks(who)) return "its model has no mouth";
    if (!ep->mskp || !ep->exp0 || !ep->exp2) return "the record has no face";
    if (((int*)ep->exp0)[0] & 0x4) return "its mouth moves already";
    ((int*)ep->exp0)[268] = (int)(uintptr_t)lip;   /* (bhSetLip's) */
    ((LIP_WORK*)ep->exp2)->flag = 0;
    ((int*)ep->exp0)[0] |= 0x4;   /* (LipStart's: bhControlMask plays the entry) */
    vl.mouth = sp + 1; vl.mroom = room_file_now(); vl.mexp = ep->exp0; vl.mlip = lip;
    return NULL;
}

static void mouth_stop(void)
{
    BH_PWORK* ep = vl.mouth ? &ene[vl.mouth - 1] : NULL;
    vl.mouth = 0;
    if (!ep || vl.mroom != room_file_now() || ep->exp0 != vl.mexp || !ep->exp2 || ((LIP_WORK*)ep->exp2)->top != vl.mlip) return;   /* (another room, other lips) */
    ((int*)ep->exp0)[0] &= ~0x4;
    ((LIP_WORK*)ep->exp2)->flag = 0;
}

/* A speaker shown as an enemy model (a zombie look, a creature: voice_creature) makes that enemy's sounds instead: its
   type's SE bank 3 from the disc as an extra bank (snd_xbank_load, released with the room), the programs found there
   chained over the original line's time (voice_creature_seq, from the seed and the line as voice_pick), each started by
   pc_voice_tick on SE slot 10 (the shell casings', idle in cutscenes), unpositioned and at full volume as every line on
   the disc is (mode 1). No ADX plays; the script waits the original line's time (pc_voice_playing); the mouth moves
   with the original line's lips where the model has a face. */
int  snd_xbank_load(int key);                    /* audio/snd_iop.c */
int  snd_xbank_prog_ms(int xb, int tq);          /* audio/snd_synth.c */
void ExPlaySe(SND_REQ* r);                       /* sdc.c */
void StopSe(unsigned int slot);
void StopVoice(int fade);                        /* sdfunc.c */
extern SND_REQ RequestInfo;
extern int pc_se_xbank;                          /* ps2_sg_sd.c: the extra bank of the next request */
#define CR_SLOT 10
#define CR_MAX 16
static struct { int n, next, xb; unsigned char prog[CR_MAX]; int at[CR_MAX]; } cr;

void pc_voice_tick(void)
{
    int k = cr.next;
    if (k >= cr.n || SDL_GetTicks() - vl.t0 < (uint64_t)cr.at[k]) return;
    cr.next++;   /* (one a tick: after a pause the late ones follow one another) */
    RequestInfo.SlotNo = CR_SLOT;
    RequestInfo.BankNo = 3;
    RequestInfo.ListNo = (char)cr.prog[k];
    RequestInfo.Priority = 0;
    RequestInfo.Pan = 0; RequestInfo.PanDelayTime = 0;
    RequestInfo.Volume = 0; RequestInfo.VolumeDelayTime = 0;
    RequestInfo.Pitch = 0; RequestInfo.PitchDelayTime = 0;
    RequestInfo.FxInput = -1;
    pc_se_xbank = cr.xb;
    ExPlaySe(&RequestInfo);
    pc_se_xbank = 0;
    if (getenv("RECVX_CAST_TRACE")) host_log("voices: record %d sound %d of %d: program %d at %d ms\n", vl.rec, k + 1, cr.n, cr.prog[k], (int)(SDL_GetTicks() - vl.t0));
}

static int creature_line(unsigned seed, int m, int disc, int vid, int sp, int own)
{
    unsigned char cand[64], pick[CR_MAX];
    int len[64], room = 0, n = 0, c, i, k, xb = 0, alt, ms;
    char seq[CR_MAX * 24] = "";   /* (" + 63 (2262 ms)" each) */
    for (alt = 0; !n && (c = voice_creature(m, alt, &room, cand, 64)) > 0; alt++) {   /* (the programs of the bank found) */
        if (!(xb = snd_xbank_load(room))) continue;
        for (i = 0; i < c; i++)
            if ((len[n] = snd_xbank_prog_ms(xb, cand[i])) > 0) cand[n++] = cand[i];
    }
    if (!n) return 0;
    ms = adx_length_ms(PatId[1], vid);
    StopVoice(0);   /* (the line before, as PlayVoiceEx2 stops it; vl, cr cleared) */
    cr.n = voice_creature_seq(seed, room_file_now(), disc, vid, len, n, ms, pick, cr.at, CR_MAX);
    for (k = 0; k < cr.n; k++) {
        cr.prog[k] = cand[pick[k]];
        snprintf(seq + strlen(seq), sizeof seq - strlen(seq), "%s%d (%d ms)", k ? " + " : "", cr.prog[k], len[pick[k]]);
    }
    cr.next = 0;
    cr.xb = xb;
    host_log("voices: room %04d record %d: %s's line %d/%d (%d ms), shown as %s -> its sounds from RM_%04d bank 3: %s\n", room_file_now(), sp,
             cast_name(own), disc, vid, ms, actor_model(m)->name, room, seq);
    vl.on = 1; vl.rec = sp; vl.ms = ms; vl.lip = NULL; vl.t0 = SDL_GetTicks();
    pc_voice_tick();   /* (the first one now) */
    return 1;
}

int pc_voice_on(int vid, int kind, int rec, const unsigned char* next, int* ptid)
{
    static int logged, part[2];
    const EnemyOpts* o = enemy_opts();
    int disc = (sys->ss_flg & 0x1) + 1, sp = -1, own, chr, m, lrec, ms, lip;
    const VoiceLine* l;
    const char* why;
    pc_voice_stop();   /* (an enemy's sounds stop at the next line, as PlayVoiceEx2 stops a line) */
    if (!o || !o->voices || !rom) return vid;
    if ((lip = voice_lipset(next, 512, coop_insn_len_raw, &lrec)) >= 0) sp = lrec;
    else if (kind == 1) sp = rec;
    if (sp < 0 || sp >= rom->ene_n || sp >= RECS) return vid;
    own = cast_character(ene[sp].id, ene[sp].mdlver);
    m = shown_model(sp);
    chr = voice_model_chr(m);
    if (own >= 0 && chr < 0 && creature_line((unsigned)o->seed, m, disc, vid, sp, own)) return -1;   /* (bhVoiceOn plays nothing) */
    if (own < 0 || chr < 0 || chr == own) return vid;
    if (voice_count(0) < 0) {   /* (the room files are being read: cast_room started it) */
        tp_count();
        if (!logged++) host_log("voices: the lines are not read yet: line %d/%d stays\n", disc, vid);
        return vid;
    }
    if (!(l = voice_pick((unsigned)o->seed, room_file_now(), disc, vid, chr))) return vid;
    if (!part[l->disc - 1]) {   /* the line's archive as a partition of its own (the game's 1 is the room's disc's) */
        ADXF_LoadPartitionNw(VOICE_PT + l->disc - 1, l->disc == 1 ? (char*)"\\VOICE1.AFS" : (char*)"\\VOICE2.AFS", NULL, NULL);
        part[l->disc - 1] = ADXF_GetPtStat(VOICE_PT + l->disc - 1) == 3 ? 1 : -1;   /* (ADXF_STAT_READEND) */
    }
    if (part[l->disc - 1] < 0) return vid;
    ms = adx_length_ms(PatId[1], vid);
    why = lip < 0 ? mouth_add(sp, vid, l->lip) : NULL;
    host_log("voices: room %04d record %d: %s's line %d/%d (%d ms), shown as %s -> %s's line %d/%d (%d ms) from RM_%04d, %s%s%s\n", room_file_now(), sp,
             cast_name(own), disc, vid, ms, m >= ACTOR_PART ? "a player model" : actor_model(m) ? actor_model(m)->name : "?", cast_name(chr), l->disc, l->vid,
             adx_length_ms(VOICE_PT + l->disc - 1, l->vid), l->room, lip >= 0 ? "with its lips" : "no lips in the script (the speaker is the VoiceOn's record): ",
             lip >= 0 ? "" : why ? "a still mouth, " : "Talking Mouths: its lips added", why ? why : "");
    vl.on = 1; vl.rec = sp; vl.ms = ms; vl.lip = lip >= 0 ? l->lip : NULL; vl.t0 = SDL_GetTicks();
    *ptid = VOICE_PT + l->disc - 1;
    return l->vid;
}

void pc_voice_lip(BH_PWORK* pp, int rec)
{
    if (vl.mouth && rec == vl.mouth - 1) mouth_stop();   /* (the script's own lips for that record: they start afresh) */
    if (vl.on != 1 || !vl.lip || rec != vl.rec || !pp->exp0 || !pp->exp2) return;
    ((int*)pp->exp0)[268] = (int)(uintptr_t)vl.lip;   /* (bhSetLip's: the entry LipStart plays) */
    if (getenv("RECVX_CAST_TRACE")) host_log("voices: record %d moves its mouth with the inserted line's lips (bank %d)\n", rec, ((int*)pp->exp0)[4]);
    vl.lip = NULL;
}

int pc_voice_playing(int flag)
{
    if (!vl.on || !vl.ms) return flag;
    if (vl.on == 1 && SDL_GetTicks() - vl.t0 < (uint64_t)vl.ms) return 1;
    vl.on = 2;
    return 0;
}

void pc_voice_stop(void)
{
    vl.on = 0;
    if (vl.mouth) mouth_stop();
    if (cr.n) { cr.n = 0; StopSe(CR_SLOT); }   /* (an enemy's sounds stop as the line would) */
}

/* --dev cutscene log (devcine_glue.c): this room visit's swaps, "r3 Alfred(94)->Tyrant, r5 Rodrigo(98)->Wesker,
   r0 Chris(92)->Hunter (your look)" (Claire's and Chris's: the player's look for them);
   the length */
int cast_describe(char* buf, int n)
{
    int k, len = 0, r;
    buf[0] = 0;
    for (k = 0; k < RECS && len < n - 1; k++) {
        if (!shown(rec_slot[k]) || !still(&ene[k], k)) continue;   /* (also before the first room load: rec_slot still zeros; a record changed since is itself) */
        r = snprintf(buf + len, (size_t)(n - len), "%sr%d %s(%d)->%s%s", len ? ", " : "", k, cast_name(rec_chr[k]), rec_id[k],
                     cast_entry_name(slot_model[rec_slot[k]]),
                     played(k) ? (rec_chr[k] < 2 ? " (played: the double)" : " (played: as itself)") : rec_chr[k] < 2 ? " (your look)" : "");
        len = r < 0 || r >= n - len ? n - 1 : len + r;
    }
    return len;
}
