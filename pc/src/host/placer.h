/* --dev object placer (docs/DEV_PLACEMENTS.md): models of item boxes, herbs and other objects put
   into rooms by hand in the F2 window, drawn only, and exported for a later gameplay option.
   The list is the overlay's (render thread: pc/src/host/overlay.cpp, file I/O placer.c); the game
   thread (pc/glue/placer_glue.c) loads the models, draws the room's entries and projects the
   selected one's gizmo. No locks: the list is a fixed array, a torn read costs one frame's drawing. */
#ifndef RECVX_PLACER_H
#define RECVX_PLACER_H
#ifdef __cplusplus
extern "C" {
#endif

enum { PL_MAX = 256, PL_RING = 48, PL_TYPES = 16 };
enum { PL_SNAP = 1 };                   /* placer_state.request bits (game thread) */

typedef struct Placement {
    int used;
    int type;                           /* placer_type index */
    int stg, rom, rcase;                /* the room it was placed in (shown in every case of stg/rom) */
    int ply;                            /* sys->ply_id when placed (0 Claire, 1 Chris, 2 Steve, 3 Wesker) */
    float pos[3];                       /* game units, y up */
    int ang[3];                         /* ax, ay, az: 65536 = 360 degrees, njRotateXYZ order */
    int flr;                            /* floor number at pos (bhCheckFloorNum), kept by the game thread */
} Placement;

typedef struct PlacerState {
    /* the overlay's */
    Placement p[PL_MAX];
    volatile int sel;                   /* the selected entry (gizmo), -1 none */
    volatile int request;               /* PL_* bits for the selected entry */
    volatile int rings;                 /* also the pitch and roll rings */
    volatile int gizmo;                 /* the F2 window is open: project the selected entry's gizmo */
    volatile int edited;                /* the game thread changed the list (Snap to Floor, RECVX_PLACE): to be saved */
    /* published by the game thread */
    volatile int in_room;               /* a room is loaded and drawn */
    int stg, rom, rcase, ply;
    float pl_pos[3];                    /* the player */
    int pl_ay;
    const char* result;                 /* last load or snap outcome */
    int type_state[PL_TYPES];           /* 0 not loaded yet, 1 loaded, -1 failed */
    float type_min[PL_TYPES][3], type_max[PL_TYPES][3];   /* the model's extent around its origin (ang 0) */
    /* the selected entry's gizmo in the shown frame, game screen (640x480; 0: not drawn) */
    volatile int gz_ok;
    float gz_len;                       /* axis length, units */
    float gz_o[2], gz_axis[3][2];       /* origin and the tips of +x, +y, +z */
    float gz_ring[3][PL_RING][2];       /* rotation rings about x, y, z: where a point of the model goes at angle 65536*k/PL_RING */
    unsigned char gz_ring_ok[3][PL_RING];   /* in front of the camera */
} PlacerState;

extern PlacerState g_placer;

/* the catalogue (constant, any thread): where each model is on the disc */
enum { PLK_PIECE, PLK_OBJ, PLK_ITEM };
typedef struct PlacerDef {
    const char* key, * name;
    short room;                         /* RM_xxxx.RDX it comes from (a room without enemies: textures follow objects) */
    signed char kind;                   /* PLK_PIECE: nodes of the room model (each with its subtree), PLK_OBJ / PLK_ITEM: a record */
    short rec[8];                       /* the nodes (-1 ends the list) or rec[0] the record */
    short lid;                          /* an object record drawn with it where it stands in that room (the item box's lid), -1 none */
    short item;                         /* the record's item id (PLK_ITEM) or object id (PLK_OBJ, 0 any): checked at load */
} PlacerDef;
extern const PlacerDef placer_defs[];

int placer_types(void);
const char* placer_type_name(int t);    /* "Item Box" */
const char* placer_type_key(int t);     /* "item_box": the export's name */
int placer_type_find(const char* key);  /* -1: unknown */
const char* placer_type_source(int t, char* buf, int n);   /* "RM_1080 room model nodes 50 + object 4" */
int placer_type_item(int t);            /* the inventory item id an item pickup gives, 0 none */

/* game thread */
void placer_frame(void);                /* cheat_frame: loads, requests, floors, test aid RECVX_PLACE */
void placer_draw(void);                 /* game.c bhAllDrawModel, after the room's objects and items */
void placer_textures_released(void);    /* njReleaseTextureAll (actor_textures_released) */

/* render thread (placer.c): <save dir>/dev_placements.ini */
int placer_load(const char* path);      /* entries read, -1 no file */
int placer_save(const char* path);      /* 0 ok */
int placer_format(char* buf, int cap);  /* the whole list in the file format; length */
int placer_parse_line(const char* line, Placement* out);   /* 1: an entry */

#ifdef __cplusplus
}
#endif
#endif
