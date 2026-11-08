/* F2 cheat menu state, shared between the overlay (render thread) and the game thread
   (pc/glue/cheat_glue.c, applied once per game frame). */
#ifndef RECVX_CHEAT_H
#define RECVX_CHEAT_H
#ifdef __cplusplus
extern "C" {
#endif

enum { CHEAT_GIVE = 1, CHEAT_TELEPORT = 2, CHEAT_HEAL = 4, CHEAT_SPAWN = 8, CHEAT_DROP = 16,
       CHEAT_SAVE = 32, CHEAT_BOX = 64,     /* the typewriter's save screen / the item box, wherever the player is */
       CHEAT_LOAD = 128 };                  /* the load screen, as the title's LOAD GAME (not in co-op) */
#define CHEAT_ITEMS 156

typedef struct CheatState {
    /* set by the overlay */
    volatile int inf_health, inf_ammo;
    volatile int noclip;            /* 0 off, 1 through walls at the current height, 2 also up/down */
    volatile int unlock;            /* locks open as if their key were carried (pc/glue/cheat_glue.c) */
    int give_id, give_count, tp_stg, tp_rom, tp_pos;
    int spawn_idx;                  /* enemy spawner list entry (spawn_name) */
    int drop_slot;                  /* CHEAT_DROP: the inventory slot to empty (inv[] index) */
    volatile int request;           /* CHEAT_* bits, cleared by the game thread */
    /* published by the game thread */
    int hp, stg, rom, pos, player, slots;
    unsigned int inv[12];           /* [0] equipped slot index (0 = none), [1] knife slot, [2..] slots: (id << 16) | count */
    const char* result;             /* last action's outcome */
    char names[CHEAT_ITEMS][32];    /* item names from the game's message data, "" until loaded */
    char rooms[19][24];             /* the save menu's room names by RoomNameSwitch (1..18), "" until loaded */
} CheatState;

extern CheatState g_cheat;
void cheat_frame(void);             /* game thread */
void cheat_put_player(float x, float z, int ay);   /* game thread, test aids: the player moved there (its collision points with it) */
void cheat_room_warp(int stg, int rom, int pos, int rcase);   /* game thread: to that room as a door without its animation
                                                                  (rcase: its room case, -1 ours as it is) */
void cheat_warp_sound(int after);  /* sdfunc.c: around the entry script of the room it leads to */
/* enemy spawner (pc/glue/spawn_glue.c): the list (constant, any thread) and the game-thread calls */
int spawn_count(void);
const char* spawn_name(int i);
const char* spawn_enemy(int i);     /* result message */
void spawn_frame(int game_frame);   /* RECVX_SPAWN, trace */

/* teleport destinations (pc/src/host/teleport.c): the rooms on the disc and in the mods folder,
   read on a background thread from the first tp_count(); any thread */
enum { TP_POS = 64 };               /* entries listed per room */
typedef struct TpRoom {
    unsigned char stg, rom, npos;   /* npos: entry positions (the most of its cases) */
    unsigned char custom;           /* only in the mods folder */
    char file[8];                   /* "RM_SRRC" of its first case */
    char tag[32];                   /* custom: the name tag in its header, "" if none */
} TpRoom;
int tp_count(void);                 /* rooms, by stage and room; 0 while reading or if it failed */
int tp_reading(void);
const TpRoom* tp_room(int i);
int tp_find(int stg, int rom);      /* -1: not a room */
int tp_stage_first(int stg, int* n);   /* the stage's first room, *n rooms */
int tp_entries(int i);              /* entries to choose from (1 for a room without any) */
const char* tp_stage_name(int stg);
const char* tp_room_name(int i, int with_file, char* buf, int n);
const char* tp_place_name(int stg, int rom);   /* the name tp_room_name gives a room on the disc (no list needed), NULL none */
const char* tp_entry_name(int i, int pos, char* buf, int n);      /* "Entry 2: from Palace: S.Room" */
const char* tp_entry_sources(int i, int pos, char* buf, int n);   /* every room with a door to it, one per line */

#ifdef __cplusplus
}
#endif
#endif
