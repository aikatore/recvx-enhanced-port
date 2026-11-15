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
 * given the player's look for that character instead of a pick (coop_look_model; cast.c cast_role), only with
 * separate looks for Claire and Chris ("Separate Looks" / Per Character, or this game's random looks per character:
 * pc_appear_separate); with one look for both the other one is themselves (the menu rework's D20; before, the cast on gave them
 * the one look too). That part runs with the cast off too, and so do the creatures and scene zombies when "Cutscene
 * Monsters" is chosen without the cast (enemies.h enemy_cast_creatures).
 */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/face.h"
#include "../../decomp/include/ps2/veronica/prog/adxwrap.h"
#include "../src/host/actors.h"
#include "../src/host/cast.h"
#include "../src/host/enemies.h"
#include "../src/host/fileio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void host_log(const char* fmt, ...);
extern int coop_on;
extern int cast_n;                      /* actor_glue.c: hooks run while > 0 */
unsigned coop_partner_import_id(void);  /* coop_glue.c: the partner's cast_import_id, 0 none */
int coop_look_model(int c);             /* coop_glue.c: the player's look for Claire or Chris as a model, -1 themselves */
int pc_appear_separate(void);           /* biorand_glue.c: Claire and Chris have looks of their own */
static void voice_end(const char* by);    /* "Randomize Voice Lines": the line's end (below) */
static void voice_room(void);
static void plan_want(const EnemyOpts* o);   /* the voice plan (below) */
static int plan_ready(const EnemyOpts* o);
static int plan_find(int room, int ofs, int vid, int chr, const VoiceLine** l);
extern unsigned char* bhScePtr;          /* event.c */
extern BH_SCEWORK* bhCetask;
static struct { const BH_SCEWORK *task, *skip; const unsigned char* end; int rec; } pre;   /* "Randomize Voice Lines": the last LipSet run
    no line before claims (pc_voice_lip; skip: the task whose next LipSet is its line's) */

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

/* the model record k (type id, model version ver) of room file room is given (cast_room; the --voice-plan export reads the
   records from the room file): -1 none (drawn as itself); *c its character or creature (-1 none); *why set where kept[]
   keeps it */
static int cast_want(const EnemyOpts* o, int room, int k, int id, int ver, int imports, int* c, const char** why)
{
    unsigned scene = o->cast_assign ? (unsigned)room + 1 : 0;
    int j;
    if ((*c = cast_character(id, ver)) < 0) *c = cast_creature(room, k, id);   /* (a creature only a cutscene shows) */
    if (*c < 0 && cast_scene_zombie(room, k, id)) *c = CAST_CROWD;   /* (a zombie only a cutscene shows) */
    if (*c < 0 || (*c >= 2 && *c < CAST_CHARACTERS && !o->cast) || (*c >= CAST_CHARACTERS && !enemy_cast_creatures(o))) return -1;
    for (j = 0; j < (int)(sizeof(kept) / sizeof(kept[0])); j++)
        if (kept[j].room == room && kept[j].rec == k) { *why = kept[j].why; return -1; }
    return *c < 2 ? coop_look_model(*c) : *c == CAST_CROWD ? cast_crowd_model((unsigned)o->seed, scene, id, ver, o->cast_all, enemy_cast_off(), o->cast_enemy, o->cast_enemy_off)
           : cast_model((unsigned)o->seed, scene, *c, o->cast_all, imports, enemy_cast_off(), o->cast_enemy, o->cast_enemy_off);   /* (Claire's, Chris's: never cast) */
}

/* enemyopt_glue.c, the end of the room load */
void cast_room(const EnemyOpts* o)
{
    int want[RECS], k, j, n = 0, room = room_file_now(), imports;
    char custom[40], enemies[48];
    if (getenv("RECVX_CAST_TRACE")) {   /* test aid: the game's state at each room load (the same with the cast on and off) */
        const unsigned char* b = (const unsigned char*)sys->ev_flg;
        unsigned h = 2166136261u;
        for (k = 0; k < (int)((const unsigned char*)sys->ply_stflg - b); k++) h = (h ^ b[k]) * 16777619u;   /* story, item, kill, key, map flags and items */
        host_log("cast: room %04d loaded: state %08x, player %.2f %.2f %.2f hp %d\n", room, h, plp ? plp->px : 0.0f, plp ? plp->py : 0.0f, plp ? plp->pz : 0.0f, plp ? plp->hp : 0);
    }
    voice_end(NULL);   /* (an inserted line going on into this room still fades out at its time) */
    pre.end = NULL;   /* (the last LipSet was the room before's) */
    voice_room();   /* (the sounds said: this room's from now) */
    if (o && o->voices && voices_want()) plan_want(o);   /* "Randomize Voice Lines": the lines for the room files now (read at the start
        already, else now), the plan for them */
    if (!o || !rom || (!o->cast && !pc_appear_separate() && !enemy_cast_creatures(o))) { cast_release(); return; }   /* (separate looks: Claire's and
        Chris's without the cast; "Cutscene Monsters" alone: the creatures') */
    imports = import_count(o);
    for (k = 0; k < rom->ene_n && k < RECS; k++) {
        BH_PWORK* ep = &ene[k];
        const char* why = NULL;
        int c;
        want[k] = -1;
        if (!(ep->flg & 0x1) || !ep->mlwP) continue;   /* (the character played's doubles too: played()) */
        want[k] = cast_want(o, room, k, ep->id, ep->mdlver, imports, &c, &why);
        if (why) host_log("cast: room %04d record %d (type %d) stays: %s\n", room, k, ep->id, why);
        if (c >= 0) rec_chr[k] = (unsigned char)c;
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
        actor_cast_hands(ACTOR_CAST + s);   /* (a model without hands: a character's unarmed pieces, hands_hide) */
        cast_n++;
        n++;
        host_log("cast: room %04d record %d (type %d model %d) -> %s (slot %d)\n", room, k, ene[k].id, ene[k].mdlver, name, s);
    }
    if (o->cast_all == 2) snprintf(custom, sizeof(custom), "custom models: %d checked", cast_custom_count(0, enemy_cast_off(), imports, NULL));
    snprintf(enemies, sizeof(enemies), "enemy models: %s", o->cast_enemy == CAST_ENEMY_SAME ? "same as cast" : o->cast_enemy == CAST_ENEMY_ALL ? "all enemies" : o->cast_enemy == CAST_ENEMY_OFF ? "off" : "");
    if (o->cast_enemy == CAST_ENEMY_CUSTOM) snprintf(enemies, sizeof(enemies), "enemy models: %d checked", cast_custom_count(2, o->cast_enemy_off, 0, NULL));
    if (!o->cast) { host_log("cast: room %04d: %d records drawn as your look for Claire or Chris (separate looks) or as other monsters (%s, without the cast)\n", room, n, enemies); return; }
    host_log("cast: room %04d: %d records drawn as other models in cutscenes (seed %d, %s, %s%s, %s)\n", room, n, o->seed,
             o->cast_assign ? "random per cutscene" : "fixed", o->cast_all == 2 ? custom : o->cast_all ? "all models" : "cutscene models", imports ? ", imported models" : "", enemies);
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
   (a creature, a zombie, an imported model) showed the record's piece over or beside its own hand; one without that
   hand at all (the Bandersnatch's left) showed it floating at the wrist; one without hands (a cutscene model, a player
   file) showed the record's character's hands (Wesker's big gloved ones on Steve). There the record's pieces are
   hidden while it is drawn as the model (each piece's mesh, NJD_EVAL_HIDE, as actor_weapon_hands does for the weapons'
   hand pieces), and shown again at the next tick; a model without hands has the unarmed pieces of the character it is
   instead (actor_glue.c actor_cast_hands, cast_hands_draw). They stay where the model is the record's own character
   (Claire's costume for Claire: the same hands) or its hands did not load. Only one-node pieces are hands alone: a piece
   of more nodes holds a prop in the same mesh (Claire's hand with the lighter, Rodrigo's with what he hands over) and
   stays, so the prop does not go with it; items the script links to the hand are other objects and stay too. The same
   for the double of the character played drawn as the player's look (cast_double_parts). */

/* actor_double_tick, every tick before cast_tick: the pieces and head parts hidden last tick shown again */
void cast_show(void)
{
    while (hidden_n) hidden[--hidden_n]->evalflags &= ~0x8u;
}

extern int cast_hands;                  /* actor_glue.c: 0 (test aid RECVX_CAST_HANDS=0): the record's pieces kept */
static void hands_hide(BH_PWORK* ep, int who, int chr)
{
    int side, node, o, own, h = actor_cast_hands_chr(who);
    if (!cast_hands) return;
    for (side = 0; side < 2; side++) {
        if (!(own = actor_own_hand(who, side, &node)) && (h < 0 || h == chr)) continue;   /* (no hand of its own, none of ours or the
            record's character's: the record's piece is the model's hand) */
        for (o = 4; o < 32; o++) {   /* (0..3: the player's weapons and head part) */
            O_WRK* op = &sys->obwp[o];
            NJS_CNK_OBJECT* m = op->mlwP && op->mlwP->objP ? &op->mlwP->objP[0] : NULL;
            if (!(op->flg & 0x1) || !(op->flg & 0x80) || op->lkwkp != (unsigned char*)ep || op->lkono != node || !m) continue;
            if (op->mlwP->obj_num != 1) {   /* (a prop in its mesh: kept) */
                if (getenv("RECVX_CAST_TRACE") && !(hid_logged[o >> 3] & (1 << (o & 7)))) {
                    hid_logged[o >> 3] |= (unsigned char)(1 << (o & 7));
                    host_log("cast: room %04d record %d (type %d): its hand piece (object %d, id %d, %d nodes) on node %d kept: a prop in it\n", room_file_now(),
                             (int)(ep - ene), ep->id, o, op->id, (int)op->mlwP->obj_num, node);
                }
                continue;
            }
            if ((m->evalflags & 0x8) || hidden_n == HIDDEN_MAX) continue;   /* (hidden already: by the game, or a piece shared by two records) */
            m->evalflags |= 0x8;
            hidden[hidden_n++] = m;
            if (getenv("RECVX_CAST_TRACE") && !(hid_logged[o >> 3] & (1 << (o & 7)))) {   /* test aid: once per object and room */
                hid_logged[o >> 3] |= (unsigned char)(1 << (o & 7));
                host_log("cast: room %04d record %d (type %d): its hand piece (object %d, id %d) on node %d hidden: the model has %s\n", room_file_now(),
                         (int)(ep - ene), ep->id, o, op->id, node, own == 2 ? "no such hand" : own ? "a hand of its own" : h == 0 ? "Claire's hands" : h == 1 ? "Chris's hands"
                         : h == 2 ? "Steve's hands" : "Wesker's hands");
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
        hands_hide(ep, ACTOR_CAST + rec_slot[k], rec_chr[k]);
        parts_hide(ep, ACTOR_CAST + rec_slot[k], rec_chr[k], 2);
    }
}

/* actor_double_tick: the head parts and hand pieces of the double ep of character pid posed as the player's look hidden
   (Claire's ponytail on her record in 9150, 9320, 3210; her gloved hands) */
void cast_double_parts(BH_PWORK* ep, int pid)
{
    parts_hide(ep, ACTOR_DOUBLE, pid, 3);
    hands_hide(ep, ACTOR_DOUBLE, pid);
}

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

/* actor_glue.c actor_drawn_for (the afterimages, trail_glue.c): the display slot record ep is drawn as now, -1 itself */
int cast_slot(const BH_PWORK* ep) { return ep >= ene && ep < ene + RECS && as_model(ep, (int)(ep - ene)) ? ACTOR_CAST + rec_slot[ep - ene] : -1; }

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
   its own line (pc_voice_playing), and the line is one that ends by then or soon after (voice_pick: at most VOICE_GRACE_MS
   longer; a shorter one leaves a silence) while the character has one out of its cooldown; else a longer one, cut at the
   original's end with a short fade (vl.cap). The scene keeps its timing. Subtitles stay the script's (the original
   line's text). */
int coop_insn_len_raw(const unsigned char* p);   /* net/coop_proto.c */
int adx_length_ms(int ptid, int fid);            /* audio/adx.c */
int ADXF_LoadPartitionNw(int ptid, char* fname, void* dir, void* ptinfo);
int ADXF_GetPtStat(int ptid);
int actor_double_shown(const BH_PWORK* ep);      /* actor_glue.c */
uint64_t SDL_GetTicks(void);
extern int PatId[4];
#define VOICE_PT 28
#define VOICE_FADE 7                 /* a cut line fades out over this many sound ticks + 1 (ExecAdxFadeManager, one a game frame,
                                        VsyncFunc at the swap: VOICE_FADE_MS) */
extern unsigned int AdxPlayFlag[2];  /* sdfunc.c */
extern ADXT_INFO AdxTInfo[4];        /* adxwrap.c */
static struct {
    int on, rec, ms;                    /* on: 1 an inserted line plays, 2 the original's time is over */
    const unsigned char* lip;           /* the inserted line's lips for the LipSet after it (pc_voice_lip) */
    uint64_t t0, cap;                   /* cap: when the inserted line on ADX slot 1 fades out (0 none) */
    int fading;                         /* ... it is fading out */
    uint64_t iend, fend;                /* when the inserted line (all of a group's) ends by itself (0: none plays), its fade-out */
    int mouth, mroom;                   /* "Talking Mouths": record + 1 whose mouth we started (mouth_add), in room file mroom */
    const void* mexp;                   /* ... its face state then, and the lips */
    const unsigned char* mlip;
    int gnext, gpt;                     /* an inserted group (lines said as one): its next part's file, -1 none; its partition */
    int part;                           /* a later part of a group said as one is on: the line in place of its first plays on */
    int snd;                           /* what sounds in the line's place: 0 nothing (quiet), 1 the inserted line, 2 an enemy's sounds */
    unsigned char hush[RECS];           /* the records whose lips came with the line replaced (the speaker's, a LipSet in the
                                           original's time): pc_voice_tick holds their mouths shut but the speaker's while snd sounds */
} vl = { .gnext = -1 };

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
static const char* mouth_add(int sp, int vid, const unsigned char* lip, int sure)
{
    BH_PWORK* ep = &ene[sp];
    int who = shown_slot(sp);
    if (!g_voice_mouths) return "Talking Mouths is off";
    if (!lip) return "the inserted line has no lips";
    if (!sure && !voice_sure(room_file_now(), vid, sp)) return "the speaker is not sure";
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

/* A speaker shown as an enemy model (a zombie look, a creature: voice_creature) makes one of that enemy's sounds instead
   of each line: its type's SE bank 3 from the disc as an extra bank (snd_xbank_load, released with the room), a few
   programs found there (voice_creature_plan, from the seed and the line as voice_pick: one for a short line, filling it;
   up to three a short gap apart in half of a longer one), on SE slot 10 (the shell casings', idle in cutscenes),
   unpositioned and at full volume as every line on the disc is (mode 1). No ADX plays; the script waits the original
   line's time (pc_voice_playing); the mouth moves with the original line's lips while they sound, where the model has a face. It ends by
   itself, at the next line, StopVoice, a room change, or VOICE_GRACE_MS after the original's end (cr.cap: a longer one
   where none of its sounds is short enough). */
int  snd_xbank_load(int key);                    /* audio/snd_iop.c */
int  snd_xbank_prog_ms(int xb, int tq);          /* audio/snd_synth.c */
void ExPlaySe(SND_REQ* r);                       /* sdc.c */
void StopSe(unsigned int slot);
void StopVoice(int fade);                        /* sdfunc.c */
void PlayVoiceEx2(int PatId, int VoiceNo, NJS_POINT3* pPos, int Mode, int FadeInRate, int PauseFlag);
extern SND_REQ RequestInfo;
extern int pc_se_xbank;                          /* ps2_sg_sd.c: the extra bank of the next request */
#define CR_SLOT 10
static struct { int n, last, last_m, xb, k, seq_n, seq[3], len[3]; uint64_t end, cap; } cr;   /* n: its sounds play (the one
    now until end), cut at cap (0 none); the program played last and its model; the bank, the sounds (seq), the one now (k) */

static void cr_play(int prog)
{
    RequestInfo.SlotNo = CR_SLOT;
    RequestInfo.BankNo = 3;
    RequestInfo.ListNo = (char)prog;
    RequestInfo.Priority = 0;
    RequestInfo.Pan = 0; RequestInfo.PanDelayTime = 0;
    RequestInfo.Volume = 0; RequestInfo.VolumeDelayTime = 0;
    RequestInfo.Pitch = 0; RequestInfo.PitchDelayTime = 0;
    RequestInfo.FxInput = -1;
    pc_se_xbank = cr.xb;
    ExPlaySe(&RequestInfo);
    pc_se_xbank = 0;
}

/* test aid (RECVX_MOUTH_TRACE=1; 2 every face): each tick a mouth moves or a line is on, per such record whether its lips run, the jaw
   (degrees, the last face computed), and what sounds: the voice slot, a creature's sound, the line's state */
uint64_t gs_frame_count(void);
static void mouth_trace(void)
{
    int k;
    for (k = 0; k < RECS && rom && k < rom->ene_n; k++) {
        const BH_PWORK* ep = &ene[k];
        int lips;
        if (!ep->mskp || !ep->exp0 || !ep->exp1 || !ep->exp2) continue;
        lips = (((const int*)ep->exp0)[0] & 0x4) != 0;
        if (!lips && !(vl.on && k == vl.rec) && atoi(getenv("RECVX_MOUTH_TRACE")) < 2) continue;   /* (2: every face) */
        host_log("mouth: frame %d rec %d lips %d jaw %.1f | voice %d fading %d creature %d line %d%s\n", (int)gs_frame_count(), k, lips,
                 ((const MASK_WORK*)ep->exp1)->param.jawang, AdxPlayFlag[1] != 0, vl.fading, cr.n && SDL_GetTicks() < cr.end,
                 vl.on && k == vl.rec ? vl.on : 0, vl.on && vl.hush[k] ? " hushed" : "");
    }
}

void pc_voice_tick(void)
{
    uint64_t now = SDL_GetTicks();
    if (vl.cap && now >= vl.cap) {   /* an inserted line longer than the original's time (and grace): faded out there */
        if (AdxPlayFlag[1]) {
            RequestAdxFadeFunctionEx(1, -1, -127, VOICE_FADE);   /* (StopAdx at the end: ExecAdxFadeManager) */
            vl.fading = 1; vl.fend = now + VOICE_FADE_MS;
            host_log("voices: the inserted line is cut %d ms before its end (%d ms late), fading out over %d ms\n", (int)(vl.iend - now), (int)(now - vl.cap), VOICE_FADE_MS);
        }
        vl.cap = 0; vl.gnext = -1;   /* (nor does a group's next part start after it) */
    }
    if (vl.gnext >= 0 && !AdxPlayFlag[1] && !vl.fading) {   /* an inserted group: its next part when one ends */
        const VoiceCurated* c = voice_curated(vl.gpt - VOICE_PT + 1, vl.gnext);
        PlayVoiceEx2(vl.gpt, vl.gnext, NULL, 1, 0, 0);
        vl.gnext = c && c->next != 0xFFFF ? c->next : -1;
    }
    if (cr.n && cr.cap && now >= cr.cap) { StopSe(CR_SLOT); cr.n = 0; host_log("voices: the sound stops %d ms after the original's end\n", VOICE_GRACE_MS); }
    else if (cr.n && now >= cr.end + (cr.k + 1 < cr.seq_n ? VOICE_CR_GAP_MS : 0)) {   /* (it has ended by itself: the next one after a gap) */
        if (++cr.k < cr.seq_n) { cr_play(cr.seq[cr.k]); cr.end = now + (uint64_t)cr.len[cr.k]; }
        else cr.n = 0;
    }
    if (vl.on) {   /* a mouth moves only with what sounds: never with a line the scene no longer says (the original's lips past
        a shorter line's end, in place of a quiet one, a second speaker's in the same file), nor past a cut line's fade */
        int sounds = vl.snd == 1 ? AdxPlayFlag[1] && !vl.fading : vl.snd == 2 ? cr.n : 0, k;
        for (k = 0; k < RECS && rom && k < rom->ene_n; k++)
            if (vl.hush[k] && (k != vl.rec || !sounds) && ene[k].mskp && ene[k].exp0 && ene[k].exp2 && ((int*)ene[k].exp0)[0] & 0x4) {
                ((int*)ene[k].exp0)[0] &= ~0x4;   /* (as a lip entry's end: bhControlMask) */
                ((LIP_WORK*)ene[k].exp2)->flag = 0;
            }
    }
    if (getenv("RECVX_MOUTH_TRACE")) mouth_trace();
}

static int creature_line(unsigned seed, int m, int disc, int vid, int sp, int own)
{
    unsigned char cand[64];
    int len[64], room = 0, n = 0, c, i, xb = 0, alt, ms, k, tot, at;
    char list[96];
    for (alt = 0; !n && (c = voice_creature(m, alt, &room, cand, 64)) > 0; alt++) {   /* (the programs of the bank found) */
        if (!(xb = snd_xbank_load(room))) continue;
        for (i = 0; i < c; i++)
            if ((len[n] = snd_xbank_prog_ms(xb, cand[i])) > 0) cand[n++] = cand[i];   /* (a looping sample has no length: never) */
    }
    if (!n) return 0;
    ms = adx_length_ms(PatId[1], vid);
    StopVoice(0);   /* (the line before, as PlayVoiceEx2 stops it; vl, cr cleared) */
    for (k = 0; k < n && cand[k] != cr.last; k++);   /* (the sound before, of this model in this room) */
    cr.seq_n = voice_creature_plan(seed, room_file_now(), disc, vid, len, n, ms, VOICE_GRACE_MS, cr.last_m == m && k < n ? k : -1, cr.seq, 3);
    for (i = 0, tot = 0, at = 0; i < cr.seq_n; i++) {   /* (programs and lengths; the log's list) */
        cr.len[i] = len[cr.seq[i]]; cr.seq[i] = cand[cr.seq[i]];
        tot += (i ? VOICE_CR_GAP_MS : 0) + cr.len[i];
        at += snprintf(list + at, sizeof list - (size_t)at, "%s%d (%d ms)", i ? ", " : "", cr.seq[i], cr.len[i]);
    }
    cr.last = cr.seq[cr.seq_n - 1]; cr.last_m = m;
    vl.on = 1; vl.rec = sp; vl.ms = ms; vl.lip = NULL; vl.t0 = SDL_GetTicks();   /* (the script waits the original's time) */
    vl.snd = 2; vl.hush[sp] = 1;
    if (ms > 0 && ms < VOICE_SHORT_MS && cr.len[0] > ms + VOICE_GRACE_MS) {   /* a gasp, and none of its sounds is as short: quiet */
        host_log("voices: room %04d record %d: %s's line %d/%d (%d ms) is a short sound and %s has none: quiet\n", room_file_now(), sp, cast_name(own), disc, vid, ms, actor_model(m)->name);
        return 1;
    }
    host_log("voices: room %04d record %d: %s's line %d/%d (%d ms), shown as %s -> %d of its sounds from RM_%04d bank 3: %s%s\n", room_file_now(), sp,
             cast_name(own), disc, vid, ms, actor_model(m)->name, cr.seq_n, room, list, ms > 0 && tot > ms + VOICE_GRACE_MS ? ", none shorter: cut" : "");
    cr.xb = xb; cr.k = 0;
    cr_play(cr.seq[0]);
    cr.n = 1;
    cr.end = vl.t0 + (uint64_t)cr.len[0];
    cr.cap = ms > 0 && tot > ms + VOICE_GRACE_MS ? vl.t0 + (uint64_t)(ms + VOICE_GRACE_MS) : 0;
    return 1;
}


/* a file's length, read once */
static int file_ms(int disc, int vid)
{
    static short len[2][2048];   /* (ms / 8 + 1, 0 not read; the longest line is 28 s) */
    int d = disc - 1;
    if (d < 0 || d > 1 || vid < 0 || vid >= 2048) return 0;
    if (!len[d][vid]) len[d][vid] = (short)(adx_length_ms(VOICE_PT + d, vid) / 8 + 1);
    return (len[d][vid] - 1) * 8;
}

/* a line's length, read once; a group's (lines said as one, voice_curated) all its parts' */
static int group_ms(int disc, int vid)
{
    const VoiceCurated* c;
    int ms = file_ms(disc, vid), k;
    for (k = 0; k < 8 && (c = voice_curated(disc, vid)) && c->next != 0xFFFF; k++) ms += file_ms(disc, vid = c->next);
    return ms;
}
static int line_ms(const VoiceLine* l) { return group_ms(l->disc, l->vid); }   /* (voice_pick) */

/* the voice archives as partitions of their own (the game's 1 is the room's disc's), both for the lengths: 1 loaded, -1 not */
static int part[2];
static void voice_parts(void)
{
    for (int d = 0; d < 2; d++)
        if (!part[d]) {
            ADXF_LoadPartitionNw(VOICE_PT + d, d ? (char*)"\\VOICE2.AFS" : (char*)"\\VOICE1.AFS", NULL, NULL);
            part[d] = ADXF_GetPtStat(VOICE_PT + d) == 3 ? 1 : -1;   /* (ADXF_STAT_READEND) */
        }
}

/* the lips for line l as its sound goes: those of its scene (they came with it), else made from its sound once, when it is
   first picked (voice_lips_make over adx_levels: a line no scene moves a mouth with; a group, all its parts, which the
   scene's lips of its first do not cover); NULL none. A file of a few hundred KB decoded, under a millisecond or two;
   kept for the game (a few KB) */
int adx_levels(int ptid, int fid, unsigned char* out, int max);   /* audio/adx.c */
static const unsigned char* line_lips(const VoiceLine* l)
{
    static unsigned char* made[2][2048];   /* (disc, file) */
    static unsigned char lv[30 * 60];      /* (a minute: the longest line is 28 s) */
    unsigned char out[sizeof lv * 2 + 1];  /* (a pair per tick at most) */
    const VoiceCurated* c = voice_curated(l->disc, l->vid);
    int d = l->disc - 1, n = 0, m, k, vid = l->vid;
    uint64_t t0 = SDL_GetTicks();
    if ((l->lip && !(c && c->next != 0xFFFF)) || d < 0 || d > 1 || l->vid >= 2048) return l->lip;
    if (made[d][l->vid]) return made[d][l->vid];
    for (k = 0; k < 8 && (m = adx_levels(VOICE_PT + d, vid, lv + n, (int)sizeof lv - n)) > 0; k++) {   /* (a group's parts, in turn) */
        n += m;
        if (!(c = voice_curated(l->disc, vid)) || c->next == 0xFFFF) break;
        vid = c->next;
    }
    if (!(m = voice_lips_make(lv, n, out, (int)sizeof out)) || !(made[d][l->vid] = malloc((size_t)m))) return l->lip;
    memcpy(made[d][l->vid], out, (size_t)m);
    host_log("voices: line %d/%d's lips made from its sound (%d ms, %d bytes, %d ms to make)\n", l->disc, l->vid, n * 1000 / 30, m, (int)(SDL_GetTicks() - t0));
    return made[d][l->vid];
}

/* A group (lines said as one, the voice tool's joined set: voice_curated) as the original: the line in place of its first part
   fills all their time and plays on through the later parts, which stay quiet, while they follow one another (gpart: the next
   one wanted; a part said elsewhere, not right after the one before, is a line of its own). bhVoiceOn stops the line before
   (StopVoice) before it asks pc_voice_on: for the part wanted, StopAdx stops nothing while the slot's flag is clear (keep; put
   back in pc_voice_on). */
static unsigned short gpart = 0xFFFF;
static struct { int on; unsigned flag, play; } keep;
static int keep_playing(void)
{
    const unsigned char* p = bhScePtr;   /* (in bhVoiceOn: just after the VoiceOn, 8 bytes) */
    const EnemyOpts* o = enemy_opts();
    if (gpart == 0xFFFF || !p || !o || !o->voices || p[-8] != 25 || (p[-6] | p[-5] << 8) != gpart) return 0;
    keep.on = 1; keep.flag = AdxTInfo[1].Flag; keep.play = AdxPlayFlag[1];
    AdxTInfo[1].Flag = 0;
    return 1;
}

/* a line not replaced: the speaker shown as another character (or as an enemy) says nothing of its own; quiet, and the
   script waits the original line's time as for an inserted one */
static int quiet(int sp, int ms)
{
    vl.on = 1; vl.rec = sp; vl.ms = ms; vl.lip = NULL; vl.t0 = SDL_GetTicks();   /* (an inserted line before it plays on: vl.cap) */
    vl.snd = 0; vl.hush[sp] = 1;
    return -1;
}

int pc_voice_on(int vid, int kind, int rec, const unsigned char* next, int* ptid)
{
    static int logged, unplanned;
    const EnemyOpts* o = enemy_opts();
    int disc = (sys->ss_flg & 0x1) + 1, sp = -1, own, chr, m, lrec, ms, lip, lms, before = 0, fit, sure = 0;
    VoiceChoice ch;
    const VoiceLine* l;
    const VoiceCurated *cv = voice_curated(disc, vid), *lc;   /* (checked by hand: docs/ACTORS.md "Lines checked by hand") */
    const char* why;
    const unsigned char* lips;
    char from[12], cut[64], here[48] = "";
    unsigned short want = gpart;   /* (the next part of a group whose first one was just said) */
    int gn = vl.gnext, full, pl = -1, gap = -1, ofs = rom && rom->evtp ? (int)(next - 8 - (const unsigned char*)rom->evtp) : -1;
#define KEEP(why) do { if (getenv("RECVX_CAST_TRACE") || cv) host_log("voices: room %04d line %d/%d kind %d rec %d, speaker %d (%s), shown as %d: stays (%s)\n", room_file_now(), disc, vid, kind, rec, sp, sp >= 0 && sp < RECS ? cast_name(cast_character(ene[sp].id, ene[sp].mdlver)) : "-", m, why); vl.cap = 0; return vid; } while (0)
    if (keep.on) { AdxTInfo[1].Flag = keep.flag; AdxPlayFlag[1] = keep.play; keep.on = 0; }   /* (keep_playing) */
    if (ofs >= 0 && !getenv("RECVX_VOICE_WHOLE")) pl = voice_played_ms(room_file_now(), ofs, vid, &gap);   /* (bhScePtr; test aid: RECVX_VOICE_WHOLE=1 as before, the whole files) */
    if (getenv("RECVX_VOICE_TIME"))   /* test aid: each line's start and pc_voice_stop's (game frames as ms at 30 a second), the table's time for it */
        host_log("voicetime: %u on room %04d ofs %d line %d/%d (%d ms) played %d\n", (unsigned)(gs_frame_count() * 1000 / 30), room_file_now(),
                 rom && rom->evtp ? (int)(next - 8 - (const unsigned char*)rom->evtp) : -1, disc, vid, adx_length_ms(PatId[1], vid), pl);
    gpart = 0xFFFF;
    voice_end(want != 0xFFFF && vid == want ? NULL : "the next line");   /* (an enemy's sound stops at the next line, as PlayVoiceEx2 stops a line) */
    vl.gnext = -1;   /* (an inserted group starts no more parts) */
    m = -1;
    if (!o || !o->voices || !rom) return vid;
    if ((lip = voice_lipset(next, 512, coop_insn_len_raw, &lrec)) >= 0) sp = lrec;
    else if (pre.task == bhCetask && pre.end && pre.end < next - 8 && next - 8 - pre.end <= 16 && voice_lipset_before(pre.end, next - 8, coop_insn_len_raw)) { sp = pre.rec; lip = 0; before = 1; }   /* (its mouth started first) */
    else if (kind == 1) sp = rec;
    else if (!kind) sp = voice_said_by(room_file_now(), vid);
    pre.end = NULL;
    pre.skip = lip >= 0 && !before ? bhCetask : NULL;   /* (at the player: the record the script's other such lines name) */
    if (cv && cv->flags & VOICE_CONFIRMED) {   /* the speaker checked by hand: that character's record (the LipSet's mouth stays the script's) */
        if (cv->chr < 0) KEEP("none of the cast says it, checked by hand");
        if (sp < 0 || sp >= rom->ene_n || sp >= RECS || cast_character(ene[sp].id, ene[sp].mdlver) != cv->chr) {
            for (sp = 0; sp < rom->ene_n && sp < RECS && cast_character(ene[sp].id, ene[sp].mdlver) != cv->chr; sp++);
            lip = -1; before = 0;
            if (sp >= rom->ene_n || sp >= RECS) { sp = -1; KEEP("its speaker, checked by hand, is not in the room"); }
        }
        sure = 1;
    }
    if (sp < 0 || sp >= rom->ene_n || sp >= RECS) KEEP("no speaker");
    own = cast_character(ene[sp].id, ene[sp].mdlver);
    m = shown_model(sp);
    chr = voice_model_chr(m);
    if (own >= 0 && chr < 0 && creature_line((unsigned)o->seed, m, disc, vid, sp, own)) return -1;   /* (bhVoiceOn plays nothing) */
    if (own >= 0 && chr == own) KEEP("shown as its own character");
    if (own < 0 || chr < 0) KEEP(own < 0 ? "not a story character" : "shown as a model without lines");
    ms = adx_length_ms(PatId[1], vid);
    if (voice_count(0) < 0 || !plan_ready(o)) {   /* (the room files are being read: cast_room started it; the plan: the next room load makes it) */
        voices_want();
        if (!logged++) host_log("voices: the lines %s: line %d/%d quiet\n", voice_count(0) < 0 ? "are not read yet" : "have no plan for these options yet", disc, vid);
        return quiet(sp, ms);
    }
    voice_parts();
    if (cv && cv->flags & VOICE_PART && vid == want) {   /* (a group's later part: the line in place of its first takes all their time) */
        gpart = cv->next; vl.gnext = gn;
        host_log("voices: room %04d record %d: line %d/%d (%d ms), a later part of a group said as one: quiet, the line in place of its first plays on\n", room_file_now(), sp, disc, vid, ms);
        quiet(sp, ms);
        vl.snd = vl.iend ? 1 : 0;   /* (the mouth moves on while that line sounds: its lips, which this part's LipSet does not restart) */
        vl.part = 1;
        return -1;
    }
    gpart = cv && cv->next != 0xFFFF && !(cv->flags & VOICE_PART) ? cv->next : 0xFFFF;
    full = gpart != 0xFFFF ? group_ms(disc, vid) : ms;
    if (own >= 2 && plan_find(room_file_now(), ofs, vid, chr, &l))   /* (the plan's line: the same in every playthrough of the seed) */
        ch = voice_choose_as(l, ms, full, pl, gap, cv && cv->flags & VOICE_GRUNT, line_ms);
    else {   /* (Claire's, Chris's records, each screen's own look; a line the plan does not know: a mod's, the speaker shown otherwise
        than planned): from the seed and the line alone */
        if (own >= 2 && unplanned++ < 32) host_log("voices: room %04d line %d/%d at %d, shown as %s: not in the plan\n", room_file_now(), disc, vid, ofs, cast_name(chr));
        ch = voice_choose((unsigned)o->seed, room_file_now(), disc, vid, chr, ms, full, pl, gap, cv && cv->flags & VOICE_GRUNT, line_ms, NULL);
    }
    if (ch.l && part[ch.l->disc - 1] < 0) { gpart = 0xFFFF; KEEP("the archive did not load"); }
    if (!ch.l) {   /* (not the original's voice) */
        host_log("voices: room %04d record %d: %s's line %d/%d (%d ms) shown as %s: quiet (%s)\n", room_file_now(), sp, cast_name(own), disc, vid, ms, cast_name(chr), ch.why);
        return quiet(sp, ms);
    }
    l = ch.l; fit = ch.fit; lms = ch.lms;
    lips = line_lips(l);
    why = lip < 0 ? mouth_add(sp, vid, lips, sure) : NULL;
    lc = voice_curated(l->disc, l->vid);
    snprintf(from, sizeof from, l->room != VOICE_NO_ROOM ? "RM_%04d" : "no scene", l->room);
    cut[0] = 0;
    if (pl > 0) snprintf(here, sizeof here, ", %d ms here, %d to the next", pl, gap);   /* (measured: until the next line, or a group's span) */
    if (ch.cut == VOICE_CUT_FADE) snprintf(cut, sizeof cut, ", faded out by %d ms, when the next line starts", ch.stop);   /* (it would cut it hard) */
    else if (ch.cut == VOICE_CUT_GRACE) snprintf(cut, sizeof cut, ", none out of the cooldown ends in time: cut at %d ms", fit);
    host_log("voices: room %04d record %d: %s's line %d/%d (%d ms%s%s%s), shown as %s -> %s's line %d/%d (%d ms%s%s%s) from %s, %s%s%s\n", room_file_now(), sp,
             cast_name(own), disc, vid, ms, here, sure ? ", speaker checked" : "", ch.grunts == VOICE_PICK_GRUNTS ? ", a grunt" : "",
             m >= ACTOR_PART ? "a player model" : actor_model(m) ? actor_model(m)->name : "?", cast_name(chr), l->disc, l->vid,
             lms, l->grunt ? ", a grunt" : "", lc && lc->next != 0xFFFF ? ", a group" : "",
             cut, from,
             lip >= 0 ? "with its lips" : sure ? "the speaker checked by hand: " : "no lips in the script (the speaker is the VoiceOn's record): ",
             lip >= 0 ? (!lips ? " (none: the script's)" : lips != l->lip ? " (made from its sound)" : "") : why ? "a still mouth, " : "Talking Mouths: its lips added", why ? why : "");
    vl.on = 1; vl.rec = sp; vl.ms = ms; vl.lip = lip >= 0 ? lips : NULL; vl.t0 = SDL_GetTicks();
    vl.snd = 1; vl.hush[sp] = 1;
    vl.gnext = lc && lc->next != 0xFFFF ? lc->next : -1; vl.gpt = VOICE_PT + l->disc - 1;   /* (a group: its parts in order, pc_voice_tick) */
    if (before && lips && ene[sp].exp0 && ene[sp].exp2) {   /* (the LipSet ran already: its lips now, afresh) */
        ((int*)ene[sp].exp0)[268] = (int)(uintptr_t)lips;
        ((LIP_WORK*)ene[sp].exp2)->flag = 0;
        vl.lip = NULL;
    }
    vl.iend = vl.t0 + (uint64_t)lms;
    vl.cap = !ch.cut ? 0 : vl.t0 + (uint64_t)(ch.cut == VOICE_CUT_FADE ? (ch.stop > VOICE_FADE_MS ? ch.stop - VOICE_FADE_MS : 0) : ch.stop);   /* (a longer line: faded out at the original's
        end, pc_voice_tick; before the next line, which would stop it at once) */
    *ptid = VOICE_PT + l->disc - 1;
    return l->vid;
#undef KEEP
}

/* The voice plan (docs/ACTORS.md "Voice plan"): what Randomize Voice Lines makes of every cutscene line with the seed and the
   cast options in effect, made once over the room files' scripts in story order as far as the numbers give it (the stages
   0..9, in each its room files and their scripts in turn), as pc_voice_on would decide each (voice_choose, the lines said
   before in that order: the cooldown, the groups, the played lengths). The game says the plan's line (plan_want, plan_find), so
   a seed says the same lines in a scene whatever was played before it, replays, loads and co-op too; recvx --voice-plan
   writes the same plan as JSON. The speaker's look as cast_room gives it (plan_room: cast_want over the room file's records,
   the kept ones, the display slots in record order), every record taken as there at the room load, no Steve or Wesker played
   (no scene with a line is played as them). Claire's and Chris's records show each screen's own look for them, so theirs
   are picked from the seed and the line alone and are not in the history (the plan is the same on both co-op screens). */
typedef struct {
    int room, m, chr, ms, pl, chose;   /* m: the model its speaker is drawn as, -1 itself (why: kept), chr its character; ms, pl the
                                          original's length, its played length (-1 unknown); chose: voice_choose made ch */
    const char *why, *kind, *res;      /* res: the choice's reason, as the log's */
    VoiceChoice ch;
    VoiceOnAt v;
} PlanLine;
static struct { PlanLine* l; int n, cap; } plan;
static VoiceSaid plan_said;
static int plan_age(const VoiceLine* l) { return voice_said_age(&plan_said, l); }
static unsigned plan_rd32(const unsigned char* d, long n, long o) { return o >= 0 && o <= n - 4 ? d[o] | d[o + 1] << 8 | d[o + 2] << 16 | (unsigned)d[o + 3] << 24 : 0; }
static void plan_room(int room, const unsigned char* d, int n, void* ud)
{
    const EnemyOpts* o = ud;
    VoiceOnAt v[256];
    int c = voice_room_lines(room, d, n, coop_insn_len_raw, v, 256), i, k, j, ch, want[RECS], slot[RECS], sm[CSLOTS], ns = 0;
    const char* why[RECS];
    long rm = plan_rd32(d, n, 0x10), ep = plan_rd32(d, n, rm + 2 * 4);
    int nr = (int)plan_rd32(d, n, rm + 0x80 + 2 * 4);
    if (plan.n + c > plan.cap) {
        PlanLine* q = realloc(plan.l, sizeof(PlanLine) * (size_t)(plan.n + c + 1024));
        if (!q) return;
        plan.l = q; plan.cap = plan.n + c + 1024;
    }
    for (k = 0; k < RECS; k++) {   /* every record given a model as cast_room gives it (cast_want; the room file's records, all
        taken as there at the room load), then the display slots in record order as cast_room fills them */
        long r = ep + k * 0x24L;   /* (ETTY_WORK: type at 4, model version at 9) */
        why[k] = NULL; want[k] = slot[k] = -1;
        if (k >= nr || !ep || r + 0x24 > n) continue;
        want[k] = cast_want(o, room, k, d[r + 4] | d[r + 5] << 8, d[r + 9], import_count(o), &ch, &why[k]);
        if (want[k] < 0) continue;
        for (j = 0; j < ns && sm[j] != want[k]; j++);
        if (j == ns && ns < CSLOTS) sm[ns++] = want[k];
        if (j < ns) slot[k] = j; else why[k] = "no display slot left";
    }
    for (i = 0; i < c; i++) {
        PlanLine* p = &plan.l[plan.n++];
        int sp = v[i].sp;
        memset(p, 0, sizeof *p);
        p->room = room; p->v = v[i]; p->m = -1;
        if (v[i].own < 0 || sp < 0 || sp >= RECS) continue;
        if (v[i].own < 2) p->m = coop_look_model(v[i].own);   /* (Claire's, Chris's records: the player's look; played, the double's, the same) */
        else if (slot[sp] >= 0) p->m = want[sp];
        else p->why = why[sp];
    }
}
static int plan_cmp(const void* a, const void* b)
{
    const PlanLine *x = a, *y = b;
    if (x->room != y->room) return x->room - y->room;
    if (x->v.script != y->v.script) return x->v.script - y->v.script;
    return x->v.ofs - y->v.ofs;
}

/* plan.l: every line and its choice; -1 the room files cannot be read */
static int plan_make(const EnemyOpts* o)
{
    int i, k, gpart_ = 0xFFFF;
    voice_parts();   /* (file_ms) */
    plan.n = 0; plan_said.n = 0;
    if (room_each(plan_room, (void*)o) <= 0) return -1;
    qsort(plan.l, (size_t)plan.n, sizeof(PlanLine), plan_cmp);
    for (i = 0; i < plan.n; i++) {
        PlanLine* p = &plan.l[i];
        const VoiceOnAt* v = &p->v;
        const VoiceCurated* cv = voice_curated(v->disc, v->vid);
        int first = !i || p->room != plan.l[i - 1].room || v->script != plan.l[i - 1].v.script, gap, full;
        unsigned short want = first ? 0xFFFF : (unsigned short)gpart_;   /* (a script: a scene; the lines said go on across them, as across room loads) */
        p->ms = adx_length_ms(VOICE_PT + v->disc - 1, v->vid); p->pl = voice_played_ms(p->room, v->ofs, v->vid, &gap); p->chr = -1;
        p->kind = "original";
        gpart_ = 0xFFFF;
        if (v->own == -2) p->res = "none of the cast says it, checked by hand";
        else if (v->own == -3) p->res = "its speaker, checked by hand, is not in the room";
        else if (v->sp < 0) p->res = "no speaker";
        else if (v->own < 0) p->res = "not a story character";
        else {
            int chr = p->chr = voice_model_chr(p->m);
            if (chr < 0 && p->m >= 0 && voice_creature(p->m, 0, &k, (unsigned char[64]){ 0 }, 64) > 0) { p->kind = "enemy"; p->res = "shown as an enemy: its sounds"; }
            else if (chr == v->own) {
                for (k = 0; v->own >= 2 && k < voice_count(chr); k++)
                    if (voice_line(chr, k)->disc == v->disc && voice_line(chr, k)->vid == v->vid) { voice_said_add(&plan_said, voice_line(chr, k)); break; }
                p->res = "shown as its own character";
            } else if (chr < 0) p->res = p->m >= 0 ? "shown as a model without lines" : p->why ? p->why : "drawn as itself";
            else if (cv && cv->flags & VOICE_PART && v->vid == want) { gpart_ = cv->next; p->kind = "group_silent"; p->res = "a later part of a group: the line in place of its first plays on"; }
            else {
                gpart_ = cv && cv->next != 0xFFFF && !(cv->flags & VOICE_PART) ? cv->next : 0xFFFF;
                full = gpart_ != 0xFFFF ? group_ms(v->disc, v->vid) : p->ms;
                p->ch = voice_choose((unsigned)o->seed, p->room, v->disc, v->vid, chr, p->ms, full, p->pl, gap, cv && cv->flags & VOICE_GRUNT, line_ms,
                                     v->own >= 2 ? plan_age : NULL);
                p->chose = 1;
                if (p->ch.l && v->own >= 2) voice_said_add(&plan_said, p->ch.l);
                if (p->ch.l) { p->kind = p->ch.l->grunt ? "grunt" : "line"; p->res = p->ch.grunts == VOICE_PICK_GRUNTS ? "a grunt for a grunt" : "picked"; }
                else { p->kind = "quiet"; p->res = p->ch.why; }
            }
        }
    }
    return plan.n;
}

/* the game's: the lines of story characters but Claire and Chris the plan chose for, by room file and offset (pc_voice_on),
   made for the inputs of key (plan_key); cached in <save_dir>/voices.plan (ids only: the room file, the offset, the line,
   the character it is said as, the line in its place, 0 quiet) */
typedef struct { int room, ofs, vid, chr; const VoiceLine* l; } PlanPick;
static PlanPick* picks;
static int picks_n;
static unsigned picks_key;
static int pick_cmp(const void* a, const void* b)
{
    const PlanPick *x = a, *y = b;
    return x->room != y->room ? x->room - y->room : x->ofs - y->ofs;
}
static int plan_find(int room, int ofs, int vid, int chr, const VoiceLine** l)
{
    PlanPick k = { room, ofs, 0, 0, NULL };
    const PlanPick* p = picks ? bsearch(&k, picks, (size_t)picks_n, sizeof k, pick_cmp) : NULL;
    if (!p || p->vid != vid || p->chr != chr) return 0;   /* (another line there, or the speaker shown as another character than planned) */
    *l = p->l;
    return 1;
}

/* what the plan depends on: the catalogue's key (the room files, the lines checked by hand), the played lengths, the seed
   and the cast options in effect (a co-op guest's: the host's); 0 none yet (the catalogue not read) */
#define PLAN_FORMAT 2   /* (bump with a change to what the plan picks; 2: the looks and monsters of D20, coop_look_model, cast_want) */
static unsigned plan_key(const EnemyOpts* o)
{
    int imports = import_count(o);
    unsigned v[10] = { voices_cat_key(), voice_played_hash(), PLAN_FORMAT, (unsigned)o->seed, (unsigned)o->cast, (unsigned)o->cast_all,
                       (unsigned)o->cast_assign, (unsigned)o->cast_enemy, (unsigned)imports, imports ? cast_import_id() : 0u }, h = 2166136261u;
    const unsigned char* b;
    size_t i;
    if (!v[0]) return 0;
    for (b = (const unsigned char*)v, i = 0; i < sizeof v; i++) h = (h ^ b[i]) * 16777619u;
    for (b = o->cast_enemy_off, i = 0; i < sizeof o->cast_enemy_off; i++) h = (h ^ b[i]) * 16777619u;
    for (b = enemy_cast_off(), i = 0; i < CAST_OFF_BYTES; i++) h = (h ^ b[i]) * 16777619u;
    return h ? h : 1;
}

static int plan_ready(const EnemyOpts* o) { return picks_key && picks_key == plan_key(o); }

#define PLAN_REC 14   /* the file: "RVXP", 1, the key, the count; then each: room u16, offset u32, line u16, character, disc of the line in its place (0 quiet), its file u16, 0 */
static void plan_path(char* p, size_t n) { voices_cache_path(p, n, "voices.plan"); }
static int plan_fill(FILE* f, void* ud)
{
    unsigned char h[16] = "RVXP", r[PLAN_REC];
    unsigned key = *(const unsigned*)ud, n = (unsigned)picks_n;
    int i;
    for (i = 0; i < 4; i++) { h[4 + i] = (unsigned char)(1u >> 8 * i); h[8 + i] = (unsigned char)(key >> 8 * i); h[12 + i] = (unsigned char)(n >> 8 * i); }
    if (fwrite(h, 1, sizeof h, f) != sizeof h) return -1;
    for (i = 0; i < picks_n; i++) {
        const PlanPick* p = &picks[i];
        unsigned pv = p->l ? p->l->vid : 0;
        r[0] = (unsigned char)p->room; r[1] = (unsigned char)(p->room >> 8);
        r[2] = (unsigned char)p->ofs; r[3] = (unsigned char)(p->ofs >> 8); r[4] = (unsigned char)(p->ofs >> 16); r[5] = (unsigned char)(p->ofs >> 24);
        r[6] = (unsigned char)p->vid; r[7] = (unsigned char)(p->vid >> 8); r[8] = (unsigned char)p->chr; r[9] = p->l ? p->l->disc : 0;
        r[10] = (unsigned char)pv; r[11] = (unsigned char)(pv >> 8); r[12] = r[13] = 0;
        if (fwrite(r, 1, PLAN_REC, f) != PLAN_REC) return -1;
    }
    return 0;
}
static int plan_load(unsigned key)
{
    char path[1100];
    unsigned char h[16], r[PLAN_REC];
    unsigned n;
    int i, k, ok = 1;
    PlanPick* q;
    FILE* f;
    plan_path(path, sizeof path);
    if (!(f = pc_fopen(path, "rb"))) return 0;
    n = fread(h, 1, sizeof h, f) == sizeof h ? h[12] | h[13] << 8 | h[14] << 16 | (unsigned)h[15] << 24 : 0;
    if (!n || n > 65536 || memcmp(h, "RVXP\1\0\0\0", 8) || (unsigned)(h[8] | h[9] << 8 | h[10] << 16 | (unsigned)h[11] << 24) != key ||
        !(q = calloc(n, sizeof *q))) { fclose(f); return 0; }
    for (i = 0; ok && i < (int)n; i++) {
        PlanPick* p = &q[i];
        int disc, pv;
        if (fread(r, 1, PLAN_REC, f) != PLAN_REC) { ok = 0; break; }
        p->room = r[0] | r[1] << 8; p->ofs = (int)(r[2] | r[3] << 8 | r[4] << 16 | (unsigned)r[5] << 24); p->vid = r[6] | r[7] << 8; p->chr = r[8];
        disc = r[9]; pv = r[10] | r[11] << 8;
        for (k = 0; disc && k < voice_count(p->chr) && !p->l; k++)   /* (the line in the catalogue now: the key is its) */
            if (voice_line(p->chr, k)->disc == disc && voice_line(p->chr, k)->vid == pv) p->l = voice_line(p->chr, k);
        if ((disc && !p->l) || (i && pick_cmp(&q[i - 1], p) >= 0)) ok = 0;
    }
    ok = ok && fgetc(f) == EOF;
    fclose(f);
    if (!ok) { free(q); return 0; }
    free(picks); picks = q; picks_n = (int)n;
    return 1;
}

/* cast_room (a room load, game thread): the plan for the options in effect now, from voices.plan or made (a second or two,
   once per seed and options) */
static void plan_want(const EnemyOpts* o)
{
    uint64_t t0 = SDL_GetTicks();
    unsigned key = plan_key(o);
    int i, n = 0;
    char path[1100];
    PlanPick* q;
    if (!key || key == picks_key) return;   /* (the catalogue not read yet: the next room load) */
    if (plan_load(key)) { picks_key = key; host_log("voices: the plan for seed %d from voices.plan, %d lines (%u ms)\n", o->seed, picks_n, (unsigned)(SDL_GetTicks() - t0)); return; }
    if (plan_make(o) < 0) { host_log("voices: cannot read the room files for the plan\n"); return; }
    for (i = 0; i < plan.n; i++) n += plan.l[i].chose && plan.l[i].v.own >= 2;
    if (!(q = calloc((size_t)n + 1, sizeof *q))) return;
    for (i = 0, n = 0; i < plan.n; i++)
        if (plan.l[i].chose && plan.l[i].v.own >= 2) q[n++] = (PlanPick){ plan.l[i].room, plan.l[i].v.ofs, plan.l[i].v.vid, plan.l[i].chr, plan.l[i].ch.l };
    qsort(q, (size_t)n, sizeof *q, pick_cmp);
    free(picks); picks = q; picks_n = n; picks_key = key;
    plan_path(path, sizeof path);
    if (pc_save_file(path, "wb", plan_fill, &key)) host_log("voices: cannot write %s\n", path);
    host_log("voices: the plan for seed %d made: %d lines, %d chosen (%u ms)\n", o->seed, plan.n, n, (unsigned)(SDL_GetTicks() - t0));
}

static void plan_script(FILE* f, int s) { if (s < 2) fprintf(f, "\"scd%d\"", s); else fprintf(f, "\"event %d\"", s - 2); }
static void plan_scan(int room, const unsigned char* d, int n, void* ud) { (void)ud; voice_scan_room(room, d, n, coop_insn_len_raw); }

/* recvx --voice-plan SEED --out PATH (cast.h voice_plan_export, headless): the plan with that seed and the cast options of
   recvx.ini as JSON */
int voice_plan_export(int seed, const char* path)
{
    EnemyOpts o = *enemy_opts();
    FILE* f;
    int i, scenes = 0, order = 0, head = -1, part_k = 0, expect = -1;
    o.seed = seed; o.voices = 1;
    if (room_each(plan_scan, NULL) <= 0) { host_log("voice plan: cannot read the room files\n"); return -1; }
    voice_scan_done();   /* (the catalogue, as voices_want's scan, without voices.cache) */
    if (plan_make(&o) < 0) { host_log("voice plan: cannot read the room files\n"); return -1; }
    if (!(f = pc_fopen(path, "w"))) { host_log("voice plan: cannot write %s\n", path); return -1; }
    fprintf(f, "{\"version\":2,\"seed\":%d,\"options\":{\"voices\":1,\"cast\":%d,\"cast_assign\":%d,\"cast_all\":%d,\"cast_enemy\":%d,\"mouths\":%d},\"scenes\":[",
            seed, o.cast, o.cast_assign, o.cast_all, o.cast_enemy, g_voice_mouths);
    for (i = 0; i < plan.n; i++) {
        const PlanLine* p = &plan.l[i];
        const VoiceOnAt* v = &p->v;
        const VoiceCurated* cv = voice_curated(v->disc, v->vid);
        const VoiceChoice* ch = &p->ch;
        int first = !i || p->room != plan.l[i - 1].room || v->script != plan.l[i - 1].v.script, m = p->m, chr = p->chr, ms = p->ms, pl = p->pl;
        if (first) {
            fprintf(f, "%s{\"room\":\"RM_%04d\",\"script\":", scenes++ ? "]}," : "", p->room);
            plan_script(f, v->script);
            fprintf(f, ",\"disc\":%d,\"lines\":[", v->disc);
            order = 0; head = -1;
        }
        if (cv && cv->next != 0xFFFF && !(cv->flags & VOICE_PART)) { head = v->vid; part_k = 0; expect = cv->next; }   /* (the group: its
            first part's file, while its parts follow it here) */
        else if (head >= 0 && v->vid == expect) { part_k++; expect = cv && cv->next != 0xFFFF ? cv->next : -1; }
        else head = -1;
        fprintf(f, "%s{\"disc\":%d,\"vid\":%d,\"speaker\":", order ? "," : "", v->disc, v->vid);
        if (v->own >= 0) fprintf(f, "\"%s\"", cast_name(v->own)); else fprintf(f, "null");
        if (m < 0) fprintf(f, ",\"drawn_as\":null");   /* (the character of the model it is drawn as; a model of none: its name) */
        else fprintf(f, ",\"drawn_as\":\"%s\"", chr >= 0 ? cast_name(chr) : m >= ACTOR_PART ? "a player model" : actor_model(m) ? actor_model(m)->name : "?");
        fprintf(f, ",\"order\":%d,\"orig_ms\":%d,\"played_ms\":%d,\"played_known\":%s,\"group\":", order++, ms, pl >= 0 ? pl : ms, pl >= 0 ? "true" : "false");
        if (head >= 0) fprintf(f, "%d", head); else fprintf(f, "null");
        fprintf(f, ",\"group_part\":%d,\"repl\":{\"kind\":\"%s\",", head >= 0 ? part_k : 0, p->kind);
        if (ch->l) fprintf(f, "\"disc\":%d,\"vid\":%d,\"speaker_of_line\":\"%s\",\"ms\":%d,", ch->l->disc, ch->l->vid, cast_name(ch->l->chr), ch->lms);
        else if (!strcmp(p->kind, "original")) fprintf(f, "\"disc\":%d,\"vid\":%d,\"speaker_of_line\":%s%s%s,\"ms\":%d,", v->disc, v->vid,
                                                       v->own >= 0 ? "\"" : "", v->own >= 0 ? cast_name(v->own) : "null", v->own >= 0 ? "\"" : "", ms);
        else fprintf(f, "\"disc\":null,\"vid\":null,\"speaker_of_line\":null,\"ms\":null,");
        if (ch->cut) fprintf(f, "\"cut_ms\":%d,", ch->stop);   /* (faded out by then: the next line's start; past the grace, from then) */
        else fprintf(f, "\"cut_ms\":null,");
        fprintf(f, "\"fade\":%s,\"why\":\"%s\"}}", ch->cut ? "true" : "false", p->res ? p->res : "");
    }
    fprintf(f, "%s]}\n", scenes ? "]}" : "");
    fclose(f);
    host_log("voice plan: seed %d, %d lines in %d scripts -> %s\n", seed, plan.n, scenes, path);
    return 0;
}

void pc_voice_lip(BH_PWORK* pp, int rec)
{
    if (pre.skip == bhCetask) pre.skip = NULL;   /* (the line before's) */
    else { pre.task = bhCetask; pre.end = bhScePtr; pre.rec = rec; }
    if (vl.mouth && rec == vl.mouth - 1 && !vl.part) mouth_stop();   /* (the script's own lips for that record: they start afresh) */
    if (pp >= ene && pp < ene + RECS) vl.hush[pp - ene] = vl.on == 1;   /* (in the original's time: its lips; later another line's) */
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

/* the line's end: the next line, a room change (an inserted line still playing keeps its fade-out time; one fading out
   or past it stops now) */
static void voice_end(const char* by)   /* by: what stops an inserted line still playing (NULL: nothing, it plays on) */
{
    uint64_t now = SDL_GetTicks();
    if (by && vl.iend && now < vl.iend && AdxPlayFlag[1]) {
        if (!vl.fading) host_log("voices: the inserted line is cut hard by %s %d ms before its end\n", by, (int)(vl.iend - now));
        else if (now < vl.fend) host_log("voices: the inserted line's fade-out is cut by %s %d ms before its end\n", by, (int)(vl.fend - now));
    }
    if (by) vl.iend = 0;
    if (vl.fading || (vl.cap && now >= vl.cap)) {
        AdxTInfo[1].FadeFunc = 0;
        StopAdx(1);
        AdxPlayFlag[1] = 0;
        vl.cap = 0; vl.fading = 0;
    }
    vl.on = 0; vl.part = 0;
    memset(vl.hush, 0, sizeof vl.hush);
    if (vl.mouth && by) mouth_stop();   /* (a group's next part: the line in place of it plays on, its mouth too) */
    if (cr.n) { cr.n = 0; StopSe(CR_SLOT); }   /* (an enemy's sound stops as the line would) */
}

static void voice_room(void) { cr.last = -1; }

/* StopVoice (sdfunc.c): VoiceOff, a skip, the line before an enemy's sounds; the slot stops or fades as the game asks */
void pc_voice_stop(void)
{
    const unsigned char* p = bhScePtr;
    if (keep_playing()) return;   /* (the next part of a group said as one: the line in place of it plays on) */
    if (getenv("RECVX_VOICE_TIME")) host_log("voicetime: %u stop\n", (unsigned)(gs_frame_count() * 1000 / 30));
    voice_end(vl.iend && p && p[-8] == 25 ? "the next line" : "StopVoice");   /* (bhVoiceOn's: just after the VoiceOn) */
    vl.cap = 0;
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
