/* The cutscene cast randomizer in the game (docs/ACTORS.md "Cutscene cast"; the choices: pc/src/host/cast.c).
 *
 * At the end of every room load (enemyopt_glue.c enemy_room_finished) each room record of a story character
 * (cast_character: Alfred, Steve, Rodrigo, ...) is given another model (cast_model), loaded into a display
 * slot of the appearance system (ACTOR_CAST + k, one per model the room shows; the retargeting of
 * actor_glue.c). Then, while a record has one (cast_n), for the whole room visit (the cutscene and after it, where
 * the character stays in the room); cast_scene only marks the cutscene for the trace:
 *  - eneset.c bhDrawEnemy -> actor_put_enemy -> cast_draw: the model is drawn in the record's place, posed
 *    from its skeleton;
 *  - coop_glue.c coop_control_remote -> actor_double_tick -> cast_tick, every tick before the objects follow
 *    their links: its face takes the record's lip sync and eyes, and the hand pieces and props the script
 *    links to its hands go to the model's hands (as for the cutscene double).
 * Nothing else changes: the record keeps its type, its motions, collision and script. The doubles of the
 * character played are "You appear as"'s (played).
 */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
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

#define CSLOTS (ACTOR_SLOTS - ACTOR_CAST)
#define RECS 128
static signed char rec_slot[RECS];      /* record -> cast slot, -1: drawn as itself */
static unsigned char rec_id[RECS], rec_ver[RECS], rec_chr[RECS];   /* what it was when given its model: type, model version, character */
static short slot_model[CSLOTS] = { -1, -1, -1, -1, -1, -1 };
_Static_assert(CSLOTS == 6, "slot_model");
#define HIDDEN_MAX 16
static NJS_CNK_OBJECT* hidden[HIDDEN_MAX];   /* hand pieces hidden this tick (hands_hide) */
static int hidden_n;
static unsigned char hid_logged[4];   /* (test aid: objects logged hidden in this room) */

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
    if (!o || !o->cast || !rom) { cast_release(); return; }
    imports = import_count(o);
    scene = o->cast_assign ? (unsigned)room + 1 : 0;
    for (k = 0; k < rom->ene_n && k < RECS; k++) {
        BH_PWORK* ep = &ene[k];
        int c;
        want[k] = -1;
        if (!(ep->flg & 0x1) || !ep->mlwP || (c = cast_character(ep->id, ep->mdlver)) < 0) continue;   /* (the character played's doubles too: played()) */
        for (j = 0; j < (int)(sizeof(kept) / sizeof(kept[0])) && !(kept[j].room == room && kept[j].rec == k); j++);
        if (j < (int)(sizeof(kept) / sizeof(kept[0]))) { host_log("cast: room %04d record %d (type %d) stays: %s\n", room, k, ep->id, kept[j].why); continue; }
        want[k] = cast_model((unsigned)o->seed, scene, c, o->cast_all, imports, enemy_cast_off());
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
        cast_n++;
        n++;
        host_log("cast: room %04d record %d (type %d model %d) -> %s (slot %d)\n", room, k, ene[k].id, ene[k].mdlver, name, s);
    }
    if (o->cast_all == 2) snprintf(custom, sizeof(custom), "custom models: %d checked", cast_custom_count(enemy_cast_off(), imports, NULL));
    host_log("cast: room %04d: %d records drawn as other models in cutscenes (seed %d, %s, %s%s)\n", room, n, o->seed,
             o->cast_assign ? "random per cutscene" : "fixed", o->cast_all == 2 ? custom : o->cast_all ? "all models" : "cutscene models", imports ? ", imported models" : "");
}

/* a double of a character played: "You appear as" and "Swapped model in cutscenes" decide how it looks. Read when it
   is drawn: the character played now (a story switch changes sys->ply_id after the room load: the end of bhFinishRoom);
   the one a switch the script has set (PlayerChangeSet: cb_flg 0x80, cng_pid) is about to make played, so the scene
   around the switch already counts it; and in co-op the partner's (its own part file), whom a scene of ours may show
   while its game plays it. Claire while Chris is played (and no switch to her is pending) is cast as any character. */
int coop_partner_ply(void);             /* coop_glue.c: -1 none */
static int played(int k)
{
    int c = rec_chr[k];
    return c < 4 && (c == (sys->ply_id & 3) || ((sys->cb_flg & 0x80) && c == (sys->cng_pid & 3)) || c == coop_partner_ply());
}

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

static void hands_show(void)
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
    hands_show();
    for (k = 0; k < (int)sys->ewk_n && k < RECS; k++) {
        BH_PWORK* ep = &ene[k];
        if (!as_model(ep, k) || (ep->stflg & 0x41000000) || !ep->mlwP) continue;
        actor_act(ACTOR_CAST + rec_slot[k], ep);
        hands_hide(ep, ACTOR_CAST + rec_slot[k]);
    }
}

/* actor_put_enemy: 1 = drawn */
int cast_draw(BH_PWORK* ep)
{
    int k = (int)(ep - ene);
    return as_model(ep, k) && actor_put_as(ACTOR_CAST + rec_slot[k], ep);
}
