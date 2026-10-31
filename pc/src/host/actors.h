/* Enemy and NPC models a player can appear as ("You appear as", docs/ACTORS.md "You appear as").
   The catalogue names where each model is found on the disc (room file and enemy record); the
   game thread loads it from there (pc/glue/actor_glue.c). No disc data lives here. */
#ifndef RECVX_ACTORS_H
#define RECVX_ACTORS_H
#ifdef __cplusplus
extern "C" {
#endif

typedef struct ActorModel {
    const char* name;
    unsigned short room;       /* RM_<room>.RDX, e.g. 1140 */
    unsigned char rec;         /* enemy record of the room that carries the model */
    unsigned char id, mdlver;  /* expected record type and model version (checked at load) */
    unsigned char anim;        /* driven by the player's skeleton (if a humanoid node map is found at
                                  load) with the motions of: ANIM_PLAYED the character played,
                                  ANIM_MALE / ANIM_FEMALE a man's (Chris's) / a woman's (Claire's)
                                  when "gender-matched animations" is on; ANIM_IDLE (0): plays its
                                  own motion `idle`, looped */
    unsigned char idle;
} ActorModel;

enum { ANIM_IDLE, ANIM_PLAYED, ANIM_MALE, ANIM_FEMALE };

#define ACTOR_MODELS 62
extern const ActorModel actor_models[];

/* Player models imported from other games' discs (F9, pc/src/import; docs/IMPORT_OUTBREAK.md):
   actor index ACTOR_MODELS + k, "You appear as" chr COOP_CHR_MAX + 1 + k. Filled by
   import_registry_load (main thread, the only writer) append-only: an entry below actor_import_n
   (published last, release) never changes or goes away, so the game thread reads it without a
   lock and k keeps meaning the same model while it is shown (a model extracted again keeps its k). */
#define ACTOR_IMPORT_MAX 2048   /* (Outbreak File #2 has about 300 people) */
extern ActorModel actor_imports[ACTOR_IMPORT_MAX];
extern const char* actor_import_file[ACTOR_IMPORT_MAX];   /* its .cvxm */
extern unsigned char actor_import_group[ACTOR_IMPORT_MAX];   /* IMPORT_GROUP_* (import.h) */
extern int actor_import_n;
const ActorModel* actor_model(int idx);   /* catalogue entry or import idx, NULL if none */

/* "Use gender-matched animations" (ini appear_gender_anim, default 1): a model of the other sex
   than the character played moves with the motions of a player character of its own sex */
extern int g_appear_anim;
/* "Talking faces in cutscenes" (ini appear_cutscene_face, default 0, experimental): the cutscene
   double shown as a cast model moves its own face with the double's lip sync; a player file shown
   there is replaced by the same character's cutscene model (which has a face) */
extern int g_appear_face;
/* "Swapped model in cutscenes" (ini appear_in_cutscenes, default 1): the cutscene doubles of the
   character played are shown as the chosen model; 0: as the game has them (the real Claire or
   Chris with their own hands, faces and voice), the chosen model only while the player is shown */
extern int g_appear_cutscene;
/* the character (ply_id) whose motion set a model with `anim` (ANIM_*) moves with while character
   `played` (0 Claire, 1 Chris, 2 Steve, 3 Wesker) is played: Chris for a man while Claire is
   played, Claire for a woman while a man is played, else -1 (the played character's own) */
int appear_motion_chr(int anim, int played);
int appear_part_anim(int file);       /* ANIM_* of player part file 10 + file (0..4) */
/* the player character (0..3) whose voice bank (CORE_<n>.SPQ) "You appear as" chr speaks with,
   or -1: the played character's own */
int appear_voice(int chr);
/* a grab motion `mtn` of character `from` in the set of enemy type `id` (the player plays the
   enemy's motions while grabbed): the same motion of character `to`, or -1 if not known */
int appear_grab_motion(int id, int mtn, int from, int to);

/* "You appear as" (g_coop.chr): 0 the character played, 1..5 the player files, then these */
#define COOP_CHR_ACTOR 6
#define COOP_CHR_MAX (COOP_CHR_ACTOR + ACTOR_MODELS - 1)
#define COOP_CHR_IMPORT (COOP_CHR_MAX + 1)   /* the imported models after the catalogue */
/* "As played (cutscene)": the played character's cutscene model. A fixed value past every import
   (stored in recvx.ini); never sent to the partner: appear_played resolves it first. */
#define COOP_CHR_CINE 4095
int appear_played(int chr, int ply_id);   /* chr; for COOP_CHR_CINE the cutscene model of ply_id (0..3; else 0, as played) */

/* The list the menus show (F3, the lobby, ENHANCED SETTINGS): one entry per distinct look, "As played"
   and "As played (cutscene)" first, then the player files, the player characters' cutscene models last. Event doubles of a listed look are left
   out of the list only: their chr values stay valid (co-op, recvx.ini) and appear_canon maps them to
   the listed one. */
int appear_count(void);
int appear_at(int i);                 /* the chr listed at position i */
int appear_pos(int chr);              /* the position of chr (a hidden one: its listed twin's) */
int appear_canon(int chr);            /* chr, or the listed twin of a hidden one */
int appear_step(int chr, int dir);    /* the next (dir 1) or previous (-1) listed chr, wrapping */
const char* appear_name(int chr);
const char* appear_group(int chr);    /* the list's group of chr (the lobby's model page shows them as headers) */
/* game thread (pc/glue/actor_glue.c). who: display slot 0 our player, 1 the co-op partner,
   ACTOR_DOUBLE the cutscene double of the character played, ACTOR_VARIANT + k enemy variants. idx: catalogue entry, ACTOR_PART + n
   for player part file 10 + n, or -1. */
#define ACTOR_DOUBLE 2
#define ACTOR_VARIANT 3                 /* 3, 4: enemy variants' models (pc/glue/variant_glue.c), each posed per enemy drawn */
#define ACTOR_CAST 5                    /* 5..10: the cutscene cast's models (pc/glue/cast_glue.c), one per model a room shows */
#define ACTOR_SLOTS (ACTOR_CAST + 6)
#define ACTOR_PART 10000   /* past ACTOR_MODELS + ACTOR_IMPORT_MAX */
int  actor_set(int who, int idx);       /* 1 = shown (a failed entry is not retried) */
int  actor_shown(int who);
int  actor_rigged(int who);             /* its model is posed from a source's skeleton (a humanoid node map was found) */
struct BH_PWORK;
void actor_pose(int who, struct BH_PWORK* src);   /* after src's bhSetMotion and bhCalcModel */
void actor_draw(int who);               /* bhPutModel: the caller sets camera matrix, lights, textures state */
struct BH_PWORK* actor_work(int who);   /* its work (model, node matrices), NULL when none is shown */
void actor_weapon_hands(int who, struct O_WRK* wpn, int n);   /* the weapon models' hand pieces hidden while slot who shows an imported model (it has its own hands) */
int  actor_hand_free(int who, int node);   /* 1: the source's hand node (9, 13) is at the model's hand, which has no mesh of its own */
int  actor_head(int who, float* out);   /* world position of its head node (else its highest node); 0 when none is shown */
int  actor_head_node(int who);          /* its head node (the rig's head role), -1: none found or none shown */
void actor_doubles(int idx);            /* at the hand-off: the appearance for the cutscene doubles */
void actor_double_tick(void);           /* every tick before the objects follow their links: the double's pose, hands, face */
int  actor_double_acting(void);         /* a cutscene double of the character played is shown (a cutscene) */
int  actor_double_kept(void);           /* the doubles stay the game's for the chosen model: the played character's own cutscene model */
/* the cutscene cast (cast_glue.c): source ep drawn as slot who's model. actor_act: every tick before the
   objects follow their links (its face from ep's lip sync, ep's hand nodes at the model's hands, as for the
   double); actor_put_as: in place of bhPutModel(ep), posed from ep unless it was this tick; 1 = drawn */
void actor_act(int who, struct BH_PWORK* ep);
int  actor_put_as(int who, struct BH_PWORK* ep);
int  actor_own_hand(int who, int side, int* node);   /* its model has a hand of its own on side 0 right / 1 left (1; 2: no such hand at all), posed on source node *node */
int  actor_src_head(int who, int node);   /* node is the neck or head of the source slot who was last posed from (as its model) */
/* before actor_pose(who, src) (slots 0, 1): src shows the mesh of player character `mesh` (0..3, a
   part-file mesh on the real skeleton; -1: a catalogue model is shown) and plays the motions of
   character `motion`, its upper body bent by `pitch` (radians: the free-aim bend, tps_bend). Gender-matched
   animations need it; without it src's own motions are used. */
void actor_source(int who, struct BH_PWORK* src, int mesh, int motion, float pitch);
int  actor_put_body(int who, struct BH_PWORK* src);   /* draw src (bhPutModel), in its gender-matched pose if it has one */
int  actor_frame(const struct BH_PWORK* p);   /* the motion frame p shows this tick (our player: before bhSetMotion's advance) */
void actor_textures_released(void);     /* njReleaseTextureAll */

/* game thread: where a catalogue-format model is in its room file (read and expanded into a buffer
   that stays valid until another room file is requested); used by the enemy spawner too */
typedef struct ActorData {
    const void* rec;                    /* its ETTY_WORK record */
    const unsigned char *mdl, *mtn;     /* its model chunk list, its type's motion list (NULL: none) */
    int mlen, nlen, ntex;
    const unsigned char* tex[16];       /* texture data per textured sub-model in chunk order (NULL: not found) */
} ActorData;
int actor_find(const ActorModel* am, ActorData* out);
const unsigned char* actor_fx_tex(int room, int fx);   /* room file `room`'s texture data for room effect fx, NULL: none */
/* game thread: player part files, for co-op (the partner, our own mesh), the lobby's previews and here.
   afs_read: file `file` of AFS partition `part` into buf (cap bytes); 1 = read (more than 4096 bytes:
   unused costumes are 11-byte placeholders). player_model_read: bhReadPlayerData for `who` from part
   file data `file` into the given buffers (plp, sys->memp, the player buffers and freemem swapped for
   it and put back). player_hair: Claire's hair or Wesker's piece objs[2] on `who`'s head node for
   character chr (as bhSetPlayer sets up sys->obwp[2]; none for the others); exp0/exp3: the hair
   simulation's work (NULL: bhObjClpn takes the room heap, for our own player only).
   player_weapon_read: bhReadWeaponData for `who` (wpnr_no = wpn) from weapon file data `file`: the hand
   objects objs[0..1] (as sys->obwp[0..1]; their mlwP must point at a model, whose textures it
   releases), models into wr/wl (32 KB each), motions into wmt (64 KB) and who->mnwP[100..]. */
struct O_WRK;
int  afs_read(int part, int file, void* buf, int cap);
void player_model_read(struct BH_PWORK* who, unsigned char* file, unsigned char* mdl, unsigned char* bmt, unsigned char* mth, unsigned char* zmt);
void player_weapon_read(struct BH_PWORK* who, struct O_WRK* objs, int wpn, unsigned char* file, unsigned char* wr, unsigned char* wl, unsigned char* wmt);
void player_hair(struct O_WRK* objs, struct BH_PWORK* who, int chr, unsigned char* exp0, unsigned char* exp3);
void actor_tex_free(int* entries, int* heap);      /* free texture entries and heap bytes (after compacting) */

#ifdef __cplusplus
}
#endif
#endif
