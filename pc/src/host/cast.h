/* The cutscene cast randomizer (docs/ACTORS.md "Cutscene cast"): which room records are the story
   characters and which "You appear as" model each one is drawn as during cutscenes. Pure functions
   over the catalogue (actors.c), so ctest "cast" checks the picks; the game side (drawing, loading,
   the hand pieces, faces) is pc/glue/cast_glue.c. Visual only: records, scripts and logic are never
   touched. */
#ifndef RECVX_CAST_H
#define RECVX_CAST_H
#ifdef __cplusplus
extern "C" {
#endif

#define CAST_CHARACTERS 7   /* Claire, Chris, Steve, Wesker, Alfred, Alexia, Rodrigo (not the man in the hazmat suit: his mouth does not move) */

/* the story character (0..7) a record of type id, model version ver is, -1: none (Alfred's and
   Alexia's boss types, props, enemies) */
int cast_character(int id, int ver);
/* the models a character may be drawn as (all: "All models"; imports: the imported models
   0..imports-1 too), as actor indices (catalogue entries, ACTOR_PART + n player files, imports
   ACTOR_MODELS + k, CAST_ZOMBIES for every zombie look together); the count */
int cast_pool(int all, int imports, int* out, int max);
#define CAST_ZOMBIES 10100   /* cast_pool's one entry for all the zombie looks ("All models"); past ACTOR_PART + 9 */
/* the zombie look (catalogue entry) character c gets when it draws the zombie entry: from (seed, scene, c) */
int cast_zombie_look(unsigned seed, unsigned scene, int c);
/* What character c is drawn as: the actor index, -1 none. scene: 0 for "Fixed per character" (the
   same in every room), else the room file + 1 ("Random per cutscene"). All eight characters are
   assigned at once from a shuffle of the pool seeded by (seed, scene), each in turn the next model not
   taken and not its own: different characters get different models while the pool has enough. */
int cast_model(unsigned seed, unsigned scene, int c, int all, int imports);
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
