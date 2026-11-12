/* Enemy options (docs/ENEMIES.md "Enemy options"): extra enemies, rearranged placements, randomizer;
   shared with the co-op glue (the host's options and its added enemies go to the guest). */
#ifndef RECVX_ENEMIES_H
#define RECVX_ENEMIES_H
#ifdef __cplusplus
extern "C" {
#endif

#define ENEMY_EXTRA_MAX 8       /* "Extra Enemies": up to this many per room, each with a dead flag of its own (enemy_xdead_word) */
#define ENEMY_XDEAD_WORDS 41    /* the extras' dead flags: save-region words (sys->itm 302..319, 360..382), a bit per room and extra */
#define ENEMY_XDEAD_BASE 1024   /* SpawnNote.dead of an extra enemy: this + its dead flag's bit */
#define ENEMY_TYPES_MAX 8       /* "Types per Room": 1..8, 0 no limit */
#define ENEMY_RATE_MAX 10       /* a kind's rate under "Custom": 0 (Off) .. 10 */
#define ENEMY_RATE_N 32         /* EnemyOpts.rate: the base kinds (enemy_rate_*), then the enemy variants by variant_at index
                                   (EnemyOpts fills Event's union, coop_proto.h: 88 bytes) */
typedef struct EnemyOpts {
    int extra;                  /* 0 off, 1..ENEMY_EXTRA_MAX: up to that many extra enemies per room */
    int place;                  /* 1: rearranged placements */
    int rand;                   /* 1: enemy randomizer */
    int seed;                   /* placements and randomizer (0..2^31-1) */
    int variety;                /* randomizer: 0 Mild (two common kinds per room), 1 Wild (every kind that fits; default),
                                   2 Custom (each kind by its rate[], as BioRand's ratio sliders) */
    int events;                 /* randomizer: 1 also event enemies (default) */
    int cast;                   /* cutscene cast randomizer (cast.h): 1 the story characters drawn as other models in cutscenes */
    int cast_all;               /* its characters: 0 from the cutscene models (faces move), 1 from all models, 2 "Custom":
                                   the models checked in its list (cast.h cast_off; co-op: the host's, EV_CASTOFF) */
    int cast_assign;            /* 0 fixed per character (the seed), 1 random per cutscene (seed and room file) */
    int cast_imports;           /* 1: imported models too (co-op: only while both sides have the same, cast_import_id) */
    int voices;                 /* 1: "Randomize Voice Lines": a role shown as another story character's model says that one's lines (cast.h) */
    int types;                  /* randomizer: at most this many different kinds in a room (0: no limit) */
    unsigned char rate[ENEMY_RATE_N];   /* "Custom": each kind's weight, 0..ENEMY_RATE_MAX (recvx.ini enemy_rates); picking Wild or
                                           Mild presets them (enemy_rate_preset) */
    int cast_enemy;             /* the cutscene cast's "Enemy Models" (cast.h CAST_ENEMY_*): what its creatures and scene zombies become */
    unsigned char cast_enemy_off[4];    /* ... its "Custom" list's unchecked entries (cast.h CAST_ENEMY_BYTES; recvx.ini enemy_custom_off) */
} EnemyOpts;
extern EnemyOpts g_enemy_opts;  /* our settings (F1, recvx.ini) */

/* "Custom"'s list (enemyopt_glue.c): every kind the randomizer can place, the base kinds then the enemy variants with a
   randomizer weight; i is the kind's index into EnemyOpts.rate */
int  enemy_rate_count(void);
int  enemy_rate_at(int k);                      /* the list's k-th kind: its rate index */
const char* enemy_rate_name(int i);             /* English (the menus translate it) */
void enemy_rate_preset(unsigned char* rate, int variety);   /* Wild's (1) or Mild's (0) weights */
void enemy_rates_ini(const char* text);         /* config.c: the recvx.ini line, applied by enemy_rates_load */
void enemy_rates_load(void);                    /* at start, once the variants are known (host.c) */
const char* enemy_rates_text(void);             /* the recvx.ini line ("1:5 4:4 ... v1:1": type ids, v + variant ids) */

/* an enemy that exists on the host but not in the room file: where its data is and where it stands */
typedef struct SpawnNote {
    unsigned short room;        /* source room file RM_<room>.RDX and its record */
    unsigned char rec, id, mdlver, type;
    signed char flr;
    unsigned char pad;          /* 0: an added enemy. The randomizer result of a room load (protocol 52, the guest's too since
                                   53; enemyopt_glue.c follow_host): SPAWN_NOTE_DONE, room file `room` (stg * 1000 + rom * 10 + case), `rec` notes
                                   follow; then SPAWN_NOTE_RAND each: record `idx` became this (room, rec: its entry; id,
                                   mdlver, type, variant) */
    float px, py, pz;
    short ay;
    unsigned short variant;     /* an enemy variant's id (pc/src/host/variants.h; 0: the type itself) */
    unsigned short dead;        /* an extra enemy's dead flag (ENEMY_XDEAD_BASE + its bit; 0: none): the guest sets it too when
                                   the enemy dies in its game */
} SpawnNote;
#define SPAWN_NOTE_RAND 2
#define SPAWN_NOTE_DONE 3
#define SPAWN_NOTE_XDONE 4          /* (protocol 53, guest -> host) the guest's extra enemies of its load of room file `room`:
                                       the `rec` notes before it (pad 0, idx the slot), or SPAWN_XDONE_NONE: the host's, not its own */
#define SPAWN_XDONE_NONE 255

/* spawn_at's `mode` (pc/glue/spawn_glue.c) */
#define SPAWN_LOADING 1             /* from the room load (the randomizer): no readiness checks */
#define SPAWN_DICTATED 2            /* the host's (a co-op guest follows it): no texture reserve, only what the upload needs */

/* enemy slots the spawner and the randomizer leave free for what enemies spawn at run time (moth
   eggs, damage dummies; bhSetEnemy has no NULL check in their callers) */
#define SPAWN_SLOTS_FREE 6
int  spawn_slots_needed(int id);                /* a type and the enemies its Init spawns */

/* game thread */
const EnemyOpts* enemy_opts(void);              /* in effect: ours, or the host's on a co-op guest */
int  room_file_now(void);                       /* the room file we are in: RM_<stg * 1000 + rom * 10 + case> */
void enemy_opts_from_host(const EnemyOpts* o);  /* co-op guest (NULL: none yet / session over) */
const unsigned char* enemy_cast_off(void);      /* "Custom"'s unchecked models (cast.h) in effect: ours, or the host's on a co-op guest */
void enemy_cast_off_from_host(int at, int n, const unsigned char* bits);   /* co-op guest: bytes at..at+n-1 of the host's */
int  spawn_note_apply(int slot, const SpawnNote* n, int stg, int rom);   /* guest: placed now or once the room runs (1) */
void spawn_pending(int run);                    /* guest, every co-op frame (0: the session ended, forget them) */
void enemy_rand_result_begin(int room, int count, unsigned int hash);   /* the partner's randomizer result for that room file
                                                   (SPAWN_NOTE_DONE), hash: its room's records (enemy_room_hash) */
int enemy_room_differs(void);                   /* the partner's last load of this room file had other records (another file) */
unsigned int enemy_room_hash(void);             /* this room load's records as the room file has them (protocol 54: the players'
                                                   room files differ when theirs differ) */
void enemy_rand_result_add(int rec, const SpawnNote* n);   /* ... one record it changed (SPAWN_NOTE_RAND) */
void coop_rand_result(int room, const unsigned char* rec, const SpawnNote* n, int count, unsigned int hash);   /* coop_glue.c, host: this room load's */
void enemy_extra_note(int slot, const SpawnNote* n);   /* guest: the host's added enemy in that slot (its dead flag) */
unsigned int* enemy_xdead_word(int i);          /* word i (< ENEMY_XDEAD_WORDS) of the extras' dead flags (co-op: EV_FLAGS 120 + i) */
int  enemy_rec_scripted(int slot);           /* the room's scripts act on that record (co-op: never the guest's to run) */
void spawn_replay(void);                        /* host: every added enemy of the room again (coop_spawn_note) */
void coop_spawn_note(int slot, const SpawnNote* n);   /* coop_glue.c: host -> guest, if it is in our room; guest -> host: its own extras */
void coop_extras_done(int room, int count);     /* coop_glue.c, guest: its extras of this load sent (SPAWN_NOTE_XDONE) */
void coop_extra_corpse(int slot);               /* coop_glue.c, host: gx_follow made an extra the guest killed (dead and hidden
                                                   until the guest hands its corpse in) */
int  coop_partner_in_room(void);                /* coop_glue.c: the co-op partner plays in our room (its copy of it runs) */
void enemy_xres_note(int slot, const SpawnNote* n);   /* host: one of the guest's own extras (then SPAWN_NOTE_XDONE) */
void enemy_xres_done(int room, int count);      /* host: the guest's extras of its load of that room file */
void enemy_partner_forget(void);                /* a new co-op partner session: its randomizer results and extras go */
int  coop_insn_len(const unsigned char* p);     /* coop_glue.c: a room script instruction's length (0: unknown) */

#ifdef __cplusplus
}
#endif
#endif
