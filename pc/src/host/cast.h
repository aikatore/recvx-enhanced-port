/* The cutscene cast randomizer (docs/ACTORS.md "Cutscene cast"): which room records are the story
   characters and which "You appear as" model each one is drawn as during cutscenes. Pure functions
   over the catalogue (actors.c), so ctest "cast" checks the picks; the game side (drawing, loading,
   the hand pieces, faces) is pc/glue/cast_glue.c. Visual only: records, scripts and logic are never
   touched. */
#ifndef RECVX_CAST_H
#define RECVX_CAST_H
#include "actors.h"
#ifdef __cplusplus
extern "C" {
#endif

#define CAST_CHARACTERS 7   /* Claire, Chris, Steve, Wesker, Alfred, Alexia, Rodrigo (not the man in the hazmat suit: his mouth does not move) */

/* the story character (0..7) a record of type id, model version ver is, -1: none (Alfred's and
   Alexia's boss types, props, enemies) */
int cast_character(int id, int ver);
/* the models a character may be drawn as (all: 0 "Cutscene models", 1 "All models", 2 "Custom": the "All models"
   entries whose bit in off is clear; imports: the imported models 0..imports-1 too), as actor indices (catalogue
   entries, ACTOR_PART + n player files, imports ACTOR_MODELS + k, CAST_ZOMBIES for every zombie look together); the count */
int cast_pool(int all, int imports, int* out, int max, const unsigned char* off);
#define CAST_ZOMBIES 10100   /* cast_pool's one entry for all the zombie looks ("All models"); past ACTOR_PART + 9 */

/* "Custom" (docs/ACTORS.md "Cutscene cast"): a bit per "All models" entry, by its position in cast_pool(1, every
   import): the built-in entries, then import k. Set: not checked (so a new import starts checked). */
#define CAST_CUSTOM_MAX (5 + ACTOR_MODELS + 1 + ACTOR_IMPORT_MAX)
#define CAST_OFF_BYTES ((CAST_CUSTOM_MAX + 7) / 8)
extern unsigned char cast_off[CAST_OFF_BYTES];   /* ours (recvx.ini cast_custom_off; co-op: the host's go to the guest) */
static inline int cast_off_at(const unsigned char* off, int j) { return off && j >= 0 && j < CAST_CUSTOM_MAX && ((off[j >> 3] >> (j & 7)) & 1); }
static inline void cast_off_put(unsigned char* off, int j, int v) { if (j >= 0 && j < CAST_CUSTOM_MAX) off[j >> 3] = (unsigned char)((off[j >> 3] & ~(1 << (j & 7))) | (!!v << (j & 7))); }
/* the "All models" entries in the menus' order (the "You appear as" list's, the zombies' entry where their looks
   start): each one's position (the bit) and model; the count. imports: as cast_pool's */
int cast_custom_list(int imports, short* pos, int* model, int max);
/* how entry m is shown (its name; the list's group) and its key in recvx.ini (a name; an import: "import:" its folder) */
const char* cast_entry_name(int m);
const char* cast_entry_group(int m);
const char* cast_entry_key(int m, char* buf, int n);
/* recvx.ini: the entry of key unchecked (imports: those listed now), 0 unknown (ignored); every unchecked one's line */
int  cast_off_key(const char* key);
void cast_off_write(void (*line)(const char* key, void* ud), void* ud);
/* the checked ones of the list (imports: as cast_pool's) and its length */
int  cast_custom_count(const unsigned char* off, int imports, int* total);
/* the zombie look (catalogue entry) character c gets when it draws the zombie entry: from (seed, scene, c) */
int cast_zombie_look(unsigned seed, unsigned scene, int c);
/* What character c is drawn as: the actor index, -1 none. scene: 0 for "Fixed per character" (the
   same in every room), else the room file + 1 ("Random per cutscene"). All eight characters are
   assigned at once from a shuffle of the pool seeded by (seed, scene), each in turn the next model not
   taken and not its own: different characters get different models while the pool has enough. */
int cast_model(unsigned seed, unsigned scene, int c, int all, int imports, const unsigned char* off);   /* off: cast_pool's */
/* identity of the imported models list (names, groups, order): co-op uses them only when both sides
   have the same; 0 with none */
unsigned cast_import_id(void);
/* "Randomize Played as" (BioRand, docs/BIORAND_INGAME.md S5a): the "You appear as" chr for BioRand seed number
   seed, from it alone: an entry of the "All models" pool (zombies one entry, then a look of them; imports
   0..imports-1 too) */
int cast_appear_pick(unsigned seed, int imports);

#ifdef __cplusplus
}
#endif
#endif
