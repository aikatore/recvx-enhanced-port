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
#include "../../decomp/include/ps2/veronica/prog/adxwrap.h"
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
static void voice_end(void);             /* "Randomize Voice Lines": the line's end (below) */
static void voice_room(void);
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

/* enemyopt_glue.c, the end of the room load */
void cast_room(const EnemyOpts* o)
{
    int want[RECS], k, j, n = 0, room = room_file_now(), imports;
    char custom[40], enemies[48];
    unsigned scene;
    if (getenv("RECVX_CAST_TRACE")) {   /* test aid: the game's state at each room load (the same with the cast on and off) */
        const unsigned char* b = (const unsigned char*)sys->ev_flg;
        unsigned h = 2166136261u;
        for (k = 0; k < (int)((const unsigned char*)sys->ply_stflg - b); k++) h = (h ^ b[k]) * 16777619u;   /* story, item, kill, key, map flags and items */
        host_log("cast: room %04d loaded: state %08x, player %.2f %.2f %.2f hp %d\n", room, h, plp ? plp->px : 0.0f, plp ? plp->py : 0.0f, plp ? plp->pz : 0.0f, plp ? plp->hp : 0);
    }
    voice_end();   /* (an inserted line going on into this room still fades out at its time) */
    pre.end = NULL;   /* (the last LipSet was the room before's) */
    voice_room();   /* (the lines and sounds said: this room's from now) */
    if (o && o->voices) voices_want();   /* "Randomize Voice Lines": the lines for the room files now (read at the start already, else now) */
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
        want[k] = c < 2 ? coop_look_model(c) : c == CAST_CROWD ? cast_crowd_model((unsigned)o->seed, scene, ep->id, ep->mdlver, o->cast_all, enemy_cast_off(), o->cast_enemy, o->cast_enemy_off)
                : cast_model((unsigned)o->seed, scene, c, o->cast_all, imports, enemy_cast_off(), o->cast_enemy, o->cast_enemy_off);   /* (Claire's, Chris's: never cast) */
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
        actor_cast_hands(ACTOR_CAST + s);   /* (a model without hands: a character's unarmed pieces, hands_hide) */
        cast_n++;
        n++;
        host_log("cast: room %04d record %d (type %d model %d) -> %s (slot %d)\n", room, k, ene[k].id, ene[k].mdlver, name, s);
    }
    if (o->cast_all == 2) snprintf(custom, sizeof(custom), "custom models: %d checked", cast_custom_count(0, enemy_cast_off(), imports, NULL));
    snprintf(enemies, sizeof(enemies), "enemy models: %s", o->cast_enemy == CAST_ENEMY_SAME ? "same as cast" : o->cast_enemy == CAST_ENEMY_ALL ? "all enemies" : o->cast_enemy == CAST_ENEMY_OFF ? "off" : "");
    if (o->cast_enemy == CAST_ENEMY_CUSTOM) snprintf(enemies, sizeof(enemies), "enemy models: %d checked", cast_custom_count(2, o->cast_enemy_off, 0, NULL));
    if (!o->cast) { host_log("cast: room %04d: %d records of Claire or Chris drawn as your look for them (Per Character)\n", room, n); return; }
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
   longer; a shorter one leaves a silence); one still going then (none of that character fits) fades out (vl.cap). The
   scene keeps its timing. Subtitles stay the script's (the original line's text). */
int coop_insn_len_raw(const unsigned char* p);   /* net/coop_proto.c */
int adx_length_ms(int ptid, int fid);            /* audio/adx.c */
int ADXF_LoadPartitionNw(int ptid, char* fname, void* dir, void* ptinfo);
int ADXF_GetPtStat(int ptid);
int actor_double_shown(const BH_PWORK* ep);      /* actor_glue.c */
uint64_t SDL_GetTicks(void);
extern int PatId[4];
#define VOICE_PT 28
#define VOICE_GRACE_MS 1000          /* an inserted line ends at most this long after the original's end */
#define VOICE_GAP_MS 4000            /* a short line followed by a pause: a line of up to this long in it (voice_gap) */
#define VOICE_FADE 8                 /* ... fading out over this many sound ticks (ExecAdxFadeManager) */
extern unsigned int AdxPlayFlag[2];  /* sdfunc.c */
extern ADXT_INFO AdxTInfo[4];        /* adxwrap.c */
static struct {
    int on, rec, ms;                    /* on: 1 an inserted line plays, 2 the original's time is over */
    const unsigned char* lip;           /* the inserted line's lips for the LipSet after it (pc_voice_lip) */
    uint64_t t0, cap;                   /* cap: when the inserted line on ADX slot 1 fades out (0 none) */
    int fading;                         /* ... it is fading out */
    int mouth, mroom;                   /* "Talking Mouths": record + 1 whose mouth we started (mouth_add), in room file mroom */
    const void* mexp;                   /* ... its face state then, and the lips */
    const unsigned char* mlip;
    int gnext, gpt;                     /* an inserted group (lines said as one): its next part's file, -1 none; its partition */
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
   line's time (pc_voice_playing); the mouth moves with the original line's lips where the model has a face. It ends by
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

static int frm_last[RECS], frm_step[RECS];   /* each record's motion frame (16.16) at the last tick, its step a tick */

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

void pc_voice_tick(void)
{
    uint64_t now = SDL_GetTicks();
    for (int r = 0; rom && r < (int)sys->ewk_n && r < RECS; r++) {   /* (voice_gap: how fast each record's motion runs) */
        int f = ene[r].frm_no, d = f - frm_last[r];
        frm_step[r] = d > 0 && d < 8 << 16 ? d : 0;
        frm_last[r] = f;
    }
    if (vl.cap && now >= vl.cap) {   /* an inserted line longer than the original's time and grace: it fades out */
        vl.cap = 0;
        if (AdxPlayFlag[1]) {
            RequestAdxFadeFunctionEx(1, -1, -127, VOICE_FADE);   /* (StopAdx at the end: ExecAdxFadeManager) */
            vl.fading = 1;
            host_log("voices: the inserted line fades out %d ms after the original's end\n", VOICE_GRACE_MS);
        }
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

/* How long until the task says its next line, at least, in ms (the game's 30 ticks a second): its script from p walked
   as it waits, Sleep's ticks and the While-FrameCheck loops on a record's motion frame (the ticks that motion needs to
   get there at its pace now), up to the next VoiceOn or the event's end; an If, another loop or wait ends the walk
   (the time so far is still a lower bound). From the game's state only: the same on both co-op screens. */
static int voice_gap(const unsigned char* p)
{
    int k, len, ticks = 0;
    for (k = 0; k < 64; k++, p += len) {
        if (*p == 25 || *p == 255 || *p == 0 || (*p >= 1 && *p <= 3)) break;   /* (VoiceOn, EvtEnd, End; If, Else, Endif) */
        if (*p == 0xF8) { ticks += p[2] | p[3] << 8; len = 4; continue; }   /* Sleep: its ticks */
        if (*p == 0xFC) {   /* While FrameCheck(enemy record, half, frame) EvtNext Ewhile: until its motion frame gets there */
            int to = (p[6] | p[7] << 8) << 16 | (p[5] ? 0x8000 : 0), w, t;
            if (p[2] != 0x38 || p[3] != 1 || p[8] != 0xFE || p[9] != 0xFD || !rom || p[4] >= rom->ene_n) break;
            w = rom->enep[p[4]].wrk_no;
            if (w < 0 || w >= RECS || w >= (int)sys->ewk_n || !frm_step[w]) break;
            t = (to - ene[w].frm_no) / frm_step[w];   /* (from now: the frame counts on through the waits before) */
            if (t > ticks) ticks = t;
            len = 10;
            continue;
        }
        if ((len = coop_insn_len_raw(p)) <= 0) break;
    }
    return ticks * 1000 / 30;
}

/* the lines said since the room was loaded (voice_pick: not again while others fit; when all that fit were said, that
   character's start a new round), in the scene's order: the same in every run; a room load starts afresh (cast_room). For the cast's characters only theirs count, so both co-op screens,
   which show Claire and Chris each with its own player's look, still pick alike for them (used_cast) */
static struct { const VoiceLine* l; int cast; } used[128];
static int used_n, used_cast;
static int line_used(const VoiceLine* l)
{
    for (int i = 0; i < used_n; i++) if (used[i].l == l && (used[i].cast || !used_cast)) return 1;
    return 0;
}

/* a line not replaced: the speaker shown as another character (or as an enemy) says nothing of its own; quiet, and the
   script waits the original line's time as for an inserted one */
static int quiet(int sp, int ms)
{
    vl.on = 1; vl.rec = sp; vl.ms = ms; vl.lip = NULL; vl.t0 = SDL_GetTicks();   /* (an inserted line before it plays on: vl.cap) */
    return -1;
}

int pc_voice_on(int vid, int kind, int rec, const unsigned char* next, int* ptid)
{
    static int logged, part[2];
    const EnemyOpts* o = enemy_opts();
    int disc = (sys->ss_flg & 0x1) + 1, sp = -1, own, chr, m, lrec, ms, lip, lms, d, before = 0, fit, gap = 0, sure = 0, grunts, paused = 0;
    const VoiceLine* l;
    const VoiceCurated *cv = voice_curated(disc, vid), *lc;   /* (checked by hand: docs/ACTORS.md "Lines checked by hand") */
    const char* why;
    char from[12];
#define KEEP(why) do { if (getenv("RECVX_CAST_TRACE") || cv) host_log("voices: room %04d line %d/%d kind %d rec %d, speaker %d (%s), shown as %d: stays (%s)\n", room_file_now(), disc, vid, kind, rec, sp, sp >= 0 && sp < RECS ? cast_name(cast_character(ene[sp].id, ene[sp].mdlver)) : "-", m, why); vl.cap = 0; return vid; } while (0)
    voice_end();   /* (an enemy's sound stops at the next line, as PlayVoiceEx2 stops a line) */
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
    if (own < 0 || chr < 0 || chr == own) KEEP(own < 0 ? "not a story character" : chr < 0 ? "shown as a model without lines" : "shown as its own character");
    ms = adx_length_ms(PatId[1], vid);
    if (voice_count(0) < 0) {   /* (the room files are being read: cast_room started it) */
        voices_want();
        if (!logged++) host_log("voices: the lines are not read yet: line %d/%d quiet\n", disc, vid);
        return quiet(sp, ms);
    }
    for (d = 0; d < 2; d++)
        if (!part[d]) {   /* the archives as partitions of their own (the game's 1 is the room's disc's), both for the lengths */
            ADXF_LoadPartitionNw(VOICE_PT + d, d ? (char*)"\\VOICE2.AFS" : (char*)"\\VOICE1.AFS", NULL, NULL);
            part[d] = ADXF_GetPtStat(VOICE_PT + d) == 3 ? 1 : -1;   /* (ADXF_STAT_READEND) */
        }
    if (cv && cv->flags & VOICE_PART) return quiet(sp, ms);   /* (a group's later part: the line in place of its first takes all their time) */
    used_cast = own >= 2;   /* (Claire, Chris: each screen's own look) */
    fit = cv && cv->next != 0xFFFF ? group_ms(disc, vid) : ms;
    grunts = cv && cv->flags & VOICE_GRUNT ? VOICE_PICK_GRUNTS : VOICE_PICK_ANY;   /* (a grunt for a grunt) */
    if (grunts != VOICE_PICK_GRUNTS && ms > 0 && ms < VOICE_SHORT_MS && (gap = voice_gap(next)) - 300 > ms + VOICE_GRACE_MS)   /* a gasp before a pause: a line in it */
        fit = (gap - 300 < VOICE_GAP_MS ? gap - 300 : VOICE_GAP_MS) - VOICE_GRACE_MS, paused = 1;
    if (grunts != VOICE_PICK_GRUNTS && (fit <= 0 || fit >= VOICE_SHORT_MS)) grunts = VOICE_PICK_SPOKEN;   /* (a spoken line for one: never a grunt) */
    if (!(l = voice_pick((unsigned)o->seed, room_file_now(), disc, vid, chr, fit, fit > 0 ? fit + VOICE_GRACE_MS : 0, line_ms, line_used, grunts))) {
        if (grunts != VOICE_PICK_GRUNTS) KEEP("no line picked");
        host_log("voices: room %04d record %d: %s's line %d/%d (%d ms) is a grunt and %s has none: quiet\n", room_file_now(), sp, cast_name(own), disc, vid, ms, cast_name(chr));
        return quiet(sp, ms);
    }
    if (part[l->disc - 1] < 0) KEEP("the archive did not load");
    lms = line_ms(l);
    if (ms > 0 && ms < VOICE_SHORT_MS && lms > fit + VOICE_GRACE_MS) {   /* a gasp or grunt, and the character has no short sound: quiet */
        host_log("voices: room %04d record %d: %s's line %d/%d (%d ms) is a short sound and %s has none: quiet\n", room_file_now(), sp, cast_name(own), disc, vid, ms, cast_name(chr));
        return quiet(sp, ms);
    }
    if (line_used(l)) {   /* (all that fit were said: a new round of that character's, this one first) */
        int i, n = 0;
        for (i = 0; i < used_n; i++) if (used[i].l->chr != l->chr || (used_cast && !used[i].cast)) used[n++] = used[i];
        used_n = n;
    }
    if (used_n < (int)(sizeof used / sizeof used[0])) { used[used_n].l = l; used[used_n++].cast = own >= 2; }   /* (said in this room: not again while others are left) */
    why = lip < 0 ? mouth_add(sp, vid, l->lip, sure) : NULL;   /* (the catalogue lines have lips, but some checked by hand) */
    lc = voice_curated(l->disc, l->vid);
    snprintf(from, sizeof from, l->room != VOICE_NO_ROOM ? "RM_%04d" : "no scene", l->room);
    host_log("voices: room %04d record %d: %s's line %d/%d (%d ms%s%s), shown as %s -> %s's line %d/%d (%d ms%s%s%s) from %s, %s%s%s\n", room_file_now(), sp,
             cast_name(own), disc, vid, ms, sure ? ", speaker checked" : "", grunts == VOICE_PICK_GRUNTS ? ", a grunt" : "",
             m >= ACTOR_PART ? "a player model" : actor_model(m) ? actor_model(m)->name : "?", cast_name(chr), l->disc, l->vid,
             lms, l->grunt ? ", a grunt" : "", lc && lc->next != 0xFFFF ? ", a group" : "",
             ms > 0 && lms > fit + VOICE_GRACE_MS ? ", none shorter: faded out" : paused ? ", in the pause after it" : "", from,
             lip >= 0 ? "with its lips" : sure ? "the speaker checked by hand: " : "no lips in the script (the speaker is the VoiceOn's record): ",
             lip >= 0 ? (l->lip ? "" : " (none: the script's)") : why ? "a still mouth, " : "Talking Mouths: its lips added", why ? why : "");
    vl.on = 1; vl.rec = sp; vl.ms = ms; vl.lip = lip >= 0 ? l->lip : NULL; vl.t0 = SDL_GetTicks();
    vl.gnext = lc && lc->next != 0xFFFF ? lc->next : -1; vl.gpt = VOICE_PT + l->disc - 1;   /* (a group: its parts in order, pc_voice_tick) */
    if (before && l->lip && ene[sp].exp0 && ene[sp].exp2) {   /* (the LipSet ran already: its lips now, afresh) */
        ((int*)ene[sp].exp0)[268] = (int)(uintptr_t)l->lip;
        ((LIP_WORK*)ene[sp].exp2)->flag = 0;
        vl.lip = NULL;
    }
    vl.cap = ms > 0 && lms > fit + VOICE_GRACE_MS ? vl.t0 + (uint64_t)(fit + VOICE_GRACE_MS) : 0;
    *ptid = VOICE_PT + l->disc - 1;
    return l->vid;
#undef KEEP
}

void pc_voice_lip(BH_PWORK* pp, int rec)
{
    if (pre.skip == bhCetask) pre.skip = NULL;   /* (the line before's) */
    else { pre.task = bhCetask; pre.end = bhScePtr; pre.rec = rec; }
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

/* the line's end: the next line, a room change (an inserted line still playing keeps its fade-out time; one fading out
   or past it stops now) */
static void voice_end(void)
{
    if (vl.fading || (vl.cap && SDL_GetTicks() >= vl.cap)) {
        AdxTInfo[1].FadeFunc = 0;
        StopAdx(1);
        AdxPlayFlag[1] = 0;
        vl.cap = 0; vl.fading = 0;
    }
    vl.on = 0;
    if (vl.mouth) mouth_stop();
    if (cr.n) { cr.n = 0; StopSe(CR_SLOT); }   /* (an enemy's sound stops as the line would) */
}

static void voice_room(void) { used_n = 0; cr.last = -1; }

/* StopVoice (sdfunc.c): VoiceOff, a skip, the line before an enemy's sounds; the slot stops or fades as the game asks */
void pc_voice_stop(void)
{
    voice_end();
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
