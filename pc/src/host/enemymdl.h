/* "Enemy Models" (GAME SETUP > CHARACTERS > MONSTERS YOU FIGHT; docs/ENEMIES.md "Enemy models"): the enemies you fight
   drawn as other models, chosen per enemy type (the zombies as one category). Purely visual: the enemy keeps its type,
   AI, motions, hit capsules, sounds and effects; a display actor of the appearance system (pc/glue/actor_glue.c) is
   posed from its skeleton every time it is drawn (pc/glue/enemymdl_glue.c). This file: the categories, the choices and
   the picks (no game state; ctest emodel). */
#ifndef RECVX_ENEMYMDL_H
#define RECVX_ENEMYMDL_H
#include "actors.h"
#ifdef __cplusplus
extern "C" {
#endif

enum { EM_ZOMBIES, EM_ANATOMIST, EM_HUNTER, EM_SWEEPER, EM_BANDERSNATCH, EM_TYRANT, EM_CATS };

/* a choice (g_emodel[c], recvx.ini enemy_model_<key>, co-op EV_EMODEL): */
#define EM_ORIGINAL 0
enum { EM_RANDOM_CVX_ZOMBIE = 1, EM_RANDOM_UC_ZOMBIE, EM_RANDOM_ANY_ZOMBIE, EM_RANDOM_CVX_MONSTER, EM_RANDOM_UC_MONSTER,
       EM_RANDOM_CVX_CHAR, EM_RANDOM_UC_CHAR, EM_RANDOM_OUTBREAK, EM_RANDOM_ANY, EM_RANDOM_END };   /* each enemy one of a group */
#define EM_MODEL 64                     /* + a catalogue entry (actors.h actor_models) */
#define EM_IMPORT 4096                  /* + import k (actor_imports) */
#define EM_ROOM_PICKS 3                 /* a random group: at most this many different models in one room */

extern int g_emodel[EM_CATS];           /* our choices */
const int* emodel_now(void);            /* the choices in effect: ours, a co-op guest's the host's (pc/glue/enemymdl_glue.c) */
int  emodel_any(const int* v);          /* any category not Original */
/* a value that names a choice (the wire's check, coop_proto.c; an import may be missing here) */
static inline int emodel_valid(int v)
{
    return v >= 0 && (v < EM_RANDOM_END || (v >= EM_MODEL && v < EM_MODEL + ACTOR_MODELS) || (v >= EM_IMPORT && v < EM_IMPORT + ACTOR_IMPORT_MAX));
}

/* (co-op EV_EMODEL, protocol 62) import k's identity across players: its source and folder name hashed, 0 none; the shown
   import with that key, -1 none (its game not imported or unchecked here) */
unsigned emodel_import_key(int k);
int  emodel_import_find(unsigned key);
/* a random group that can draw imports (its members depend on the player's imports) */
static inline int emodel_group_imports(int v)
{
    return v > EM_ORIGINAL && v < EM_RANDOM_END && v != EM_RANDOM_CVX_ZOMBIE && v != EM_RANDOM_CVX_MONSTER && v != EM_RANDOM_CVX_CHAR;
}
int  emodel_cat(int id, int mdlver);    /* the category of enemy type id (model version mdlver), -1 none */
const char* emodel_cat_name(int c);     /* "Zombies" (English; the menus translate) */
/* the choices of a row in list order: Original, the random groups with a member, every listed humanoid catalogue model, every
   shown import; the count. A value no longer listed (its import's source hidden) is not there: emodel_pick shows the original */
int  emodel_choices(int* out, int max);
const char* emodel_choice_name(int v, char* buf, int n);   /* "Random Umbrella Chronicles Zombie", "Nemesis" */
const char* emodel_choice_group(int v); /* its heading in the list ("Random", "Code: Veronica", the import's) */
int  emodel_members(int v, int* out, int max);   /* the models (actor index: catalogue entry, ACTOR_MODELS + k) a choice draws from */
/* the model enemy `slot` of category c in room file `room` is drawn as, -1 its own. A random group: EM_ROOM_PICKS of its
   members for the room (seed, room, c), each enemy one of them (seed, room, slot): the same on every visit and on both co-op
   screens */
int  emodel_pick(int v, unsigned seed, int room, int c, int slot);
/* recvx.ini: "enemy_model_zombies = import:EZ00" (keys: original, random:<group>, a catalogue name, import:<folder>) */
int  emodel_ini_line(const char* key, const char* val);   /* 1: it was one of ours (after the imports are listed) */
void emodel_ini_write(void (*line)(const char* key, const char* val, void* ud), void* ud);

#ifdef __cplusplus
}
#endif
#endif
