/* "Enemy Models" in the game (docs/ENEMIES.md "Enemy models"; the choices and picks: pc/src/host/enemymdl.c).
 *
 * The enemies you fight drawn as other models, chosen per enemy type. Display only, as an enemy variant's model
 * (variant_glue.c): the enemy stays what the game animates, hits, collides with and links effects to; a display actor of
 * the appearance system (ACTOR_ENEMY + k, one per model the room shows) is posed from its skeleton (actor_glue.c
 * retargeting: the humanoid node maps of both) every time it is drawn, in its place (eneset.c bhDrawEnemy ->
 * variant_glue.c pc_variant_draw -> emodel_draw). Its linked parts (a zombie's upper half, cap, scope, worm) are not drawn
 * while it is the model; parts that fly off (a blown-off head, an arm) are drawn as they are; a blown-off head is hidden
 * on the model (variant_glue.c variant_put). An enemy whose skeleton has no humanoid node map is drawn as itself.
 *
 *  - room load (enemyopt_glue.c enemy_room_finished, after the randomizer and the cutscene cast): every enemy of a
 *    category with a choice gets its model (emodel_pick: per type, or one of a random group's for the room, by seed, room
 *    and slot), each model loaded once into a display slot (6; a seventh: the enemy takes a model of its category already
 *    shown, else stays itself);
 *  - every game frame (spawn_glue.c spawn_frame): enemies that came since (extra enemies, the spawner, a co-op host's
 *    spawn notes) get theirs;
 *  - the room's textures released (spawn_glue.c spawn_release, the door; njReleaseTextureAll): the models go.
 * The choices take effect at the next room load. Co-op: the host's (EV_EMODEL), so both screens show the same.
 */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../src/host/actors.h"
#include "../src/host/enemies.h"
#include "../src/host/enemymdl.h"
#include <stdint.h>
#include <string.h>

void host_log(const char* fmt, ...);
uint64_t SDL_GetTicksNS(void);
int room_file_now(void);                /* enemyopt_glue.c */
int coop_is_guest(void);
int variant_of_slot(int slot);          /* variant_glue.c */
int variant_put(int who, BH_PWORK* ep, BH_PWORK* r);
int cast_slot(const BH_PWORK* ep);      /* cast_glue.c */
extern int cast_n, pc_variant_n;

#define ESLOTS (ACTOR_CAST - ACTOR_ENEMY)

static short smodel[ESLOTS] = { -1, -1, -1, -1, -1, -1 };   /* the model display slot k shows */
static signed char scat[ESLOTS];        /* the category it was loaded for (a slot-less enemy of it takes it) */
static signed char eslot[PC_ENE_SLOTS]; /* enemy slot -> display slot, -1 drawn as itself */
static unsigned short ekey[PC_ENE_SLOTS];   /* (type << 8 | model version) + 1 it was decided for; 0 not yet */
static unsigned char drawn[PC_ENE_SLOTS];   /* its body was drawn as the model last time (its linked parts are not drawn then) */
static int on, room_now;
static int host_v[EM_CATS], have_host;
static short failed[8];                 /* models that did not load this room (not tried again) */
static int nfailed;

const int* emodel_now(void)
{
    static const int none[EM_CATS];
    if (coop_is_guest()) return have_host ? host_v : none;
    return g_emodel;
}

/* coop_glue.c: the host's choices (EV_EMODEL), NULL: our own again (single player) */
void emodel_from_host(const unsigned short* v)
{
    int c;
    have_host = v != NULL;
    for (c = 0; v && c < EM_CATS; c++) host_v[c] = emodel_valid(v[c]) ? v[c] : 0;
    if (v) host_log("enemymdl: the host's enemy models: %d %d %d %d %d %d\n", host_v[0], host_v[1], host_v[2], host_v[3], host_v[4], host_v[5]);
}

static BH_PWORK* root(BH_PWORK* ep)
{
    int k;
    for (k = 0; k < 3 && ep->lkwkp && (BH_PWORK*)ep->lkwkp >= ene && (BH_PWORK*)ep->lkwkp < ene + PC_ENE_SLOTS; k++) ep = (BH_PWORK*)ep->lkwkp;
    return ep;
}

static unsigned short key(const BH_PWORK* ep) { return (unsigned short)(((ep->id << 8) | ep->mdlver) + 1); }

/* the display slot showing model m, loading it into a free one; -1: no slot left, or it cannot be shown */
static int slot_of(int m, int c)
{
    int k, i;
    char nb[96];
    for (k = 0; k < ESLOTS; k++)
        if (smodel[k] >= 0 && !actor_shown(ACTOR_ENEMY + k)) smodel[k] = -1;   /* (njReleaseTextureAll unloaded it: game over, the title) */
    for (k = 0; k < ESLOTS; k++) if (smodel[k] == m) return k;
    for (i = 0; i < nfailed; i++) if (failed[i] == m) return -1;
    for (k = 0; k < ESLOTS && smodel[k] >= 0; k++);
    if (k == ESLOTS) return -1;
    {
        uint64_t t0 = SDL_GetTicksNS();
        const char* name = emodel_choice_name(m < ACTOR_MODELS ? EM_MODEL + m : EM_IMPORT + m - ACTOR_MODELS, nb, sizeof(nb));
        if (actor_set(ACTOR_ENEMY + k, m) && actor_rigged(ACTOR_ENEMY + k)) {
            smodel[k] = (short)m;
            scat[k] = (signed char)c;
            actor_cast_hands(ACTOR_ENEMY + k);   /* (a model without hands of its own: a character's unarmed pieces) */
            host_log("enemymdl: room %04d: %s loaded into slot %d in %.1f ms\n", room_file_now(), name, k, (SDL_GetTicksNS() - t0) / 1e6);
            return k;
        }
        actor_set(ACTOR_ENEMY + k, -1);
        if (nfailed < 8) failed[nfailed++] = (short)m;
        host_log("enemymdl: room %04d: %s cannot be loaded or has no humanoid node map\n", room_file_now(), name);
    }
    return -1;
}

static void decide(int s)
{
    BH_PWORK* ep = &ene[s];
    int c, m, k, v;
    char nb[96];
    ekey[s] = key(ep);
    eslot[s] = -1;
    drawn[s] = 0;
    if (ep != root(ep) || variant_of_slot(s) || (c = emodel_cat(ep->id, ep->mdlver)) < 0 || !(v = emodel_now()[c])) return;
    if ((m = emodel_pick(v, (unsigned)enemy_opts()->seed, room_now, c, s)) < 0) return;
    if ((k = slot_of(m, c)) < 0)   /* no slot left (or not loadable): a model of its category shown already */
        for (k = 0; k < ESLOTS && !(smodel[k] >= 0 && scat[k] == c); k++);
    if (k >= ESLOTS) { host_log("enemymdl: room %04d slot %d (type %d model %d): no model, drawn as itself\n", room_now, s, ep->id, ep->mdlver); return; }
    eslot[s] = (signed char)k;
    host_log("enemymdl: room %04d slot %d (type %d model %d, %s): drawn as %s\n", room_now, s, ep->id, ep->mdlver, emodel_cat_name(c),
             emodel_choice_name(smodel[k] < ACTOR_MODELS ? EM_MODEL + smodel[k] : EM_IMPORT + smodel[k] - ACTOR_MODELS, nb, sizeof(nb)));
}

/* enemyopt_glue.c enemy_room_finished: after the randomizer (a randomized Hunter gets the Hunter's choice) */
void emodel_room(void)
{
    uint64_t t0;
    int s, n = 0;
    memset(ekey, 0, sizeof(ekey));
    memset(eslot, -1, sizeof(eslot));
    nfailed = 0;
    room_now = room_file_now();
    on = emodel_any(emodel_now());
    if (!on) return;
    pc_variant_n++;   /* (the decomp's draw hook: variant_glue.c pc_variant_draw asks us for every enemy not a variant) */
    t0 = SDL_GetTicksNS();
    for (s = 0; s < (int)sys->ewk_n && s < PC_ENE_SLOTS; s++)
        if (ene[s].flg & 0x1) { decide(s); n += eslot[s] >= 0; }
    host_log("enemymdl: room %04d: %d enemies drawn as other models, %.1f ms\n", room_now, n, (SDL_GetTicksNS() - t0) / 1e6);
}

/* spawn_glue.c spawn_frame, every game frame: enemies that came since */
void emodel_frame(void)
{
    int s;
    if (!on || !sys || room_now != room_file_now()) return;
    for (s = 0; s < (int)sys->ewk_n && s < PC_ENE_SLOTS; s++)
        if ((ene[s].flg & 0x1) && ekey[s] != key(&ene[s])) decide(s);
}

/* spawn_glue.c spawn_release: with the room's textures (the door) */
void emodel_release(void)
{
    int k;
    for (k = 0; k < ESLOTS; k++)
        if (smodel[k] >= 0) { actor_set(ACTOR_ENEMY + k, -1); smodel[k] = -1; }
    on = 0;
    memset(ekey, 0, sizeof(ekey));
}

/* variant_glue.c pc_variant_draw for an enemy that is not a variant: 1 drawn (or a part not drawn) */
int emodel_draw(BH_PWORK* ep)
{
    BH_PWORK* r;
    int s, k;
    if (!on || ep < ene || ep >= ene + PC_ENE_SLOTS) return 0;
    r = root(ep);
    s = (int)(r - ene);
    if (ekey[s] != key(r) || (k = eslot[s]) < 0 || smodel[k] < 0 || !actor_shown(ACTOR_ENEMY + k)) return 0;
    if (cast_n && cast_slot(r) >= 0) return 0;   /* (the cutscene cast's: Cutscene Monsters) */
    if (ep != r) return (ep->flg & 0x80) && drawn[s];   /* its linked parts: the model stands in for them */
    drawn[s] = (unsigned char)variant_put(ACTOR_ENEMY + k, ep, r);
    return drawn[s];
}
