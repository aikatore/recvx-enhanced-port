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
#define CAST_CREATURES 4    /* then the creatures cast as characters of their own (cast_creature): Nosferatu, the Tyrant, the Hunter, Steve's father */
#define CAST_ALL (CAST_CHARACTERS + CAST_CREATURES)

/* the story character (0..7) a record of type id, model version ver is, -1: none (Alfred's and
   Alexia's boss types, props, enemies) */
int cast_character(int id, int ver);
/* the creature (CAST_CHARACTERS..CAST_ALL - 1) record rec of type id in room file room is, -1 none: only the records that
   exist just for a cutscene (docs/ACTORS.md "Creatures"), never an enemy that is fought */
int cast_creature(int room, int rec, int id);
/* the zombies only cutscenes show (docs/ACTORS.md "Scene zombies"): 1 for record rec (type id) of room file room. They
   are cast as CAST_CROWD (after the creatures, outside cast_model's turn), each zombie look a kind of its own */
int cast_scene_zombie(int room, int rec, int id);
#define CAST_CROWD CAST_ALL
/* the name of character or creature c ("Rodrigo", "Nosferatu", CAST_CROWD "Zombie") */
const char* cast_name(int c);
/* how a record of story character c (cast_character) is drawn while character ply (sys->ply_id & 3: 0 Claire, 1 Chris,
   2 Steve, 3 Wesker; -1 none) is played, pending (the one a story switch the script has set makes played, -1) is about
   to be and partner (co-op: the partner's, -1) is: Claire and Chris are never cast: the one played is the game's double
   (its look: "Character Model", actor_glue.c actor_doubles), the other one gets the player's look for that character
   in a cast slot (CAST_LOOK). Steve and Wesker played (or about to be, or the partner's) are the game's doubles;
   everyone else is cast (CAST_PICK), the creatures too. */
enum { CAST_OWN, CAST_LOOK, CAST_PICK };
int cast_role(int c, int ply, int pending, int partner);
/* the models a character may be drawn as (all: 0 "Cutscene models", 1 "All models", 2 "Custom": the "All models"
   entries whose bit in off is clear; imports: the imported models 0..imports-1 too), as actor indices (catalogue
   entries, ACTOR_PART + 4 Claire's second costume (the other player files have no face mask: their characters' cutscene
   models stand for them), imports ACTOR_MODELS + k, CAST_ZOMBIES for every zombie look together); the count */
int cast_pool(int all, int imports, int* out, int max, const unsigned char* off);
#define CAST_ZOMBIES 10100   /* cast_pool's one entry for all the zombie looks ("All models"); past ACTOR_PART + 9 */
/* "Enemy Models" (EnemyOpts.cast_enemy; docs/ACTORS.md "Enemy models"): what the creatures (cast_creature) and the scene
   zombies (cast_scene_zombie) are drawn as. SAME: as before it, the creatures from the characters' pool (Model Selection),
   the scene zombies from its monsters; ALL: both from the monsters of "All models" and the zombie looks (the
   enemies' list's monsters, list 2 below); CUSTOM: the checked ones of that list, its characters too (EnemyOpts.cast_enemy_off);
   OFF: themselves */
enum { CAST_ENEMY_SAME, CAST_ENEMY_ALL, CAST_ENEMY_CUSTOM, CAST_ENEMY_OFF };
#define CAST_ENEMY_BYTES 4   /* EnemyOpts.cast_enemy_off: a bit per entry of the enemies' list (17 now: test_cast checks it fits) */

/* "Custom" (docs/ACTORS.md "Cutscene cast"): a bit per "All models" entry, by its position in cast_pool(1, every
   import): the built-in entries, then import k. Set: not checked (so a new import starts checked). "Randomize
   Character Model" has a "Custom" list of its own (list 1 below; cast_appear_off): its "All models" entries, the same way;
   so has "Enemy Models" (list 2; EnemyOpts.cast_enemy_off): the monsters and the zombies' entry, then the characters of
   the cast's list (their cutscene models, Claire's second costume), no imports. */
#define CAST_CUSTOM_MAX (5 + ACTOR_MODELS + 1 + ACTOR_IMPORT_MAX)
#define CAST_OFF_BYTES ((CAST_CUSTOM_MAX + 7) / 8)
extern unsigned char cast_off[CAST_OFF_BYTES];   /* ours (recvx.ini cast_custom_off; co-op: the host's go to the guest) */
extern unsigned char cast_appear_off[CAST_OFF_BYTES];   /* "Randomize Character Model"'s (recvx.ini appear_custom_off; never sent) */
static inline int cast_off_at(const unsigned char* off, int j) { return off && j >= 0 && j < CAST_CUSTOM_MAX && ((off[j >> 3] >> (j & 7)) & 1); }
static inline void cast_off_put(unsigned char* off, int j, int v) { if (j >= 0 && j < CAST_CUSTOM_MAX) off[j >> 3] = (unsigned char)((off[j >> 3] & ~(1 << (j & 7))) | (!!v << (j & 7))); }
/* the "All models" entries in the menus' order (the "You appear as" list's, the zombies' entry where their looks
   start; Claire's, Chris's, Steve's and Wesker's cutscene models, its last, first): each one's position (the bit) and
   model; the count. list: 0 the cast's list, 1 "Randomize Character Model"'s (as cast_appear_pick's pool), 2 "Enemy
   Models"'. imports: as cast_pool's (list 2: none) */
int cast_custom_list(int list, int imports, short* pos, int* model, int max);
/* how entry m is shown (its name; the list's group) and its key in recvx.ini (a name; an import: "import:" its folder) */
const char* cast_entry_name(int m);
const char* cast_entry_group(int m);
const char* cast_entry_key(int m, char* buf, int n);
/* recvx.ini: the entry of key unchecked in list's bits off (cast_off, cast_appear_off, EnemyOpts.cast_enemy_off; imports:
   those listed now), 0 unknown (ignored); every unchecked one's line */
int  cast_off_key(int list, unsigned char* off, const char* key);
void cast_off_write(int list, const unsigned char* off, void (*line)(const char* key, void* ud), void* ud);
/* the cast's list as it starts (recvx.ini without cast_custom_set: config.c): only the models with voice lines checked
   (voice_model_chr: every look of Claire, Chris, Steve, Wesker, Alfred, Alexia and Rodrigo); the zombies, the creatures,
   the characters without lines and the imports listed now unchecked */
void cast_off_default(void);
/* the enemies' list as it starts (recvx.ini without enemy_custom_set: config.c): its characters unchecked in eoff, the
   monsters as they are */
void cast_enemy_default(unsigned char* eoff);
/* the checked ones of list (bits off; imports: as cast_pool's) and its length */
int  cast_custom_count(int list, const unsigned char* off, int imports, int* total);
/* the zombie look (catalogue entry) character c gets when it draws the zombie entry: from (seed, scene, c) */
int cast_zombie_look(unsigned seed, unsigned scene, int c);
/* What character c (a creature too) is drawn as: the actor index, -1 none. scene: 0 for "Fixed per character" (the
   same in every room), else the room file + 1 ("Random per cutscene"). The characters but Claire and Chris (-1: never cast),
   then the creatures, are assigned at once from a shuffle of the pool seeded by (seed, scene), each in turn the next model
   not taken and not its own: different characters get different models while the pool has enough. */
int cast_model(unsigned seed, unsigned scene, int c, int all, int imports, const unsigned char* off, int enemy, const unsigned char* eoff);   /* off: cast_pool's;
   enemy, eoff: "Enemy Models" (CAST_ENEMY_*, its list's bits): other than SAME the creatures take their turn over a shuffle of
   its pool instead, after the characters' (who then have their pool to themselves) */
/* What a scene zombie of type id, model version ver is drawn as (actor index, -1 none): a creature of "All models" (the
   Tyrant, a Bandersnatch, ...) or another zombie look, whatever all is ("Custom": the checked ones of those; never an
   import). One per look (the kind) and (seed, scene); the looks of a scene in turn over a shuffle, so they differ while
   the pool has enough. */
int cast_crowd_model(unsigned seed, unsigned scene, int id, int ver, int all, const unsigned char* off, int enemy, const unsigned char* eoff);   /* (enemy
   other than SAME: its pool instead of the Model Selection's) */
/* identity of the imported models list (names, groups, order): co-op uses them only when both sides
   have the same; 0 with none */
unsigned cast_import_id(void);
/* "Randomize Played as" (BioRand, docs/BIORAND_INGAME.md S5a): the "You appear as" chr of character c (0 Claire,
   1 Chris, 2 Steve in his section of Claire's part) for BioRand seed number seed, from it alone: an entry of the pool
   all ("Model Selection": 0 the cutscene models but Claire's and Chris's, 1 "All models" with Steve's and Wesker's
   player files too, 2 "Custom": those of 1 whose bit in off is clear; zombies one entry, then a look of them; imports
   0..imports-1 too); the three characters' entries differ while the pool has three. Claire's is the one look of the
   versions before. 0: none (an empty "Custom" pool). */
int cast_appear_pick(unsigned seed, int all, int imports, const unsigned char* off, int c);

/* "Randomize Voice Lines" (voices.c; docs/ACTORS.md "Voice lines"): a role shown as another story character's model
   says random lines of that character, with their mouth movements. The catalogue: every cutscene line a LipSet gives
   to a story character, per character, from the room files (voice_scan_room on the teleport list's thread, then
   voice_scan_done; read-only after that). insn: the script instructions' lengths (net/coop_proto.c coop_insn_len_raw). */
typedef struct VoiceLine {
    unsigned short room, vid;   /* the room file it is said in; its file in the disc's voice archive */
    unsigned char disc, chr;    /* 1 VOICE1.AFS (stages 0..5), 2 VOICE2.AFS; the character (cast_character) */
    unsigned short lip_n;       /* its lip entry: pairs phoneme 0..5, time; 0xFF last (face.c bhSetLip) */
    const unsigned char* lip;
} VoiceLine;
/* the LipSet after a VoiceOn (p: the script after it; before another VoiceOn, End or EvtEnd, within 16 instructions):
   its lip entry, *rec its record; -1 none */
int voice_lipset(const unsigned char* p, int n, int (*insn)(const unsigned char*), int* rec);
int voice_model_chr(int m);   /* the story character (cast_character) model m ("You appear as" actor index) is, -1: none (no lines) */
void voice_scan_room(int room, const unsigned char* d, int n, int (*insn)(const unsigned char*));   /* an expanded room file */
void voice_scan_done(void);
int voice_count(int chr);     /* lines of character chr, -1: the catalogue is not read yet */
const VoiceLine* voice_line(int chr, int i);
/* the line of character chr said in place of file vid of disc's archive in room file room for seed (NULL: chr has none) */
const VoiceLine* voice_pick(unsigned seed, int room, int disc, int vid, int chr);
/* "Talking Mouths" (ini voice_mouths, default 1; each screen's own): an inserted line in place of one without a LipSet
   moves the speaker's mouth with its lips, where the speaker is sure. voice_sure: in room file room, file vid's VoiceOn
   names record rec, no LipSet follows, and every line of that script with a LipSet moves the mouth of the record it
   names (its scripter put the speaker there); voice_sure_count: how many, -1 not read yet */
extern int g_voice_mouths;
int voice_sure(int room, int vid, int rec);
int voice_sure_count(void);
/* An enemy model (catalogue entry m: a zombie look, a creature) makes its own sounds in place of the line instead
   (docs/ACTORS.md "Voice lines"): the room file whose sound pack's enemy bank (SE bank 3) has them and their programs
   (prog: max >= 44), place alt 0, 1, 2 the next one to look in when the programs are not in that bank (a zombie: its
   look's moans in its room, any moans there, RM_0030's); the count, 0 none (not an enemy, or one without sounds). */
int voice_creature(int m, int alt, int* room, unsigned char* prog, int max);
/* the sounds said in place of file vid of disc's archive in room file room for seed, a line of ms milliseconds: of n
   sounds (n <= 64) of len[i] ms, pick[k] the k-th to play at at[k] ms from the line's start, until the line's time is
   filled (one at least, none starting in its last 250 ms): each a random one of those that fit in the time left (else
   the shortest), not the one before again; the count (<= max) */
int voice_creature_seq(unsigned seed, int room, int disc, int vid, const int* len, int n, int ms, unsigned char* pick, int* at, int max);

#ifdef __cplusplus
}
#endif
#endif
