/* --dev cutscene log (docs/DEV_TOOLS.md): which cutscene plays (room, the script's events, the movie, the cast's
   swaps) and the last ones, for reporting where a scene's models need a change. The game thread (pc/glue/devcine_glue.c)
   opens and updates entries; the overlay (render thread, overlay.cpp) shows, copies, exports and clears them. A short
   spin lock guards the list (a copy of 32 entries per changed frame). Nothing runs without --dev. */
#ifndef RECVX_DEVCINE_H
#define RECVX_DEVCINE_H
#ifdef __cplusplus
extern "C" {
#endif

enum { DC_MAX = 32, DC_EVENT = 1, DC_FMV = 2 };

typedef struct DevCine {
    unsigned no;                        /* 1, 2, ... this session (counts on after a clear) */
    int kind;                           /* DC_EVENT: in-engine, DC_FMV: a movie */
    int room;                           /* room file: stage * 1000 + room * 10 + case (RM_%04d) */
    int ply;                            /* sys->ply_id at its start */
    int frames;                         /* game frames it has run */
    int repeat;                         /* played again right after itself: 1 + the repeats */
    char when[12];                      /* wall clock at its start, HH:MM:SS */
    char key[24];                       /* what makes it the same scene again: the first event "3:7" or the movie */
    char what[64];                      /* the events (task level:event id) or "MV_005.PSS (ev 3:7)" */
    char flags[48];                     /* what made it a cutscene: movie, cinema, event camera, player hidden */
    char cast[240];                     /* the cast's swaps, the double's and the player's models */
} DevCine;

extern int g_devcine_show;              /* the small overlay at the top left (ini dev_cutscene_overlay, default 1) */

/* game thread */
void devcine_begin(const DevCine* e);   /* a scene starts (no, repeat and when are filled in); the same key as the last: repeat */
void devcine_update(int frames, const char* what, const char* flags, const char* cast);   /* the open one */
void devcine_end(void);
/* any thread */
int  devcine_snapshot(DevCine* out, int* open);   /* oldest first, up to DC_MAX; *open: the last one is playing */
void devcine_clear(void);                /* the history goes; a scene playing now stays */
const char* devcine_who(int ply);       /* "Claire" */
/* one entry: short (the overlay: no cast, time or flags) or full (copy, export: everything that identifies it) */
int  devcine_format(const DevCine* e, int full, char* buf, int n);
#define DEVCINE_TEXT (DC_MAX * 512)    /* room for devcine_text */
int  devcine_text(char* buf, int n);    /* every entry, full, one per line; the length */
int  devcine_export(const char* path);  /* appended with a header line; entries written, -1 error */

/* The scene list (F6 "Scenes"): every cutscene of the room files, read from the player's disc (and the mods folder) with
   the teleport list's scan (teleport.c), nothing of it in the repository. A scene is a set of events a room script
   starts together (the EvtOn of one if-block) where one of them is a cutscene (cinema mode CineSet 0/3, an event camera
   CamSet 0, a movie MovieStart, the player hidden InitModelSet 0 0 0: the log's test); events those start are part of
   it. The skip's endings (blocks testing cb_flg bit 3) are not scenes. devcine_glue.c plays one (devcat_play). */
enum { DS_EV = 16, DS_FLAGS = 8, DS_MV = 4 };
typedef struct DevScene {
    short room;                         /* RM_%04d */
    short from;                         /* the script that starts it: 0 room entry (scd0), 1 every frame (scd1), 2 + k event k */
    short ev[DS_EV];                    /* level << 8 | event, in the script's order */
    unsigned char nev, nmv, nflag, ply; /* ply: the character the block tests for (CmpB ply_id ==), 0xFF any */
    unsigned char mv[DS_MV];            /* movies (MV_%03u.PSS) */
    struct { unsigned char set, on; unsigned short bit; } flag[DS_FLAGS];   /* the block's story flag tests (set 1 ev_flg, 2 ky_flg,
                                           3 ed_flg, 7 it_flg, 8 mp_flg; on: wanted set), made to hold before the room is entered */
    unsigned int block, blen;           /* the then-part (or else part) that starts it, from the event block's start; 0 none
                                           (EvtOn at the script's top level): only its events are started */
    unsigned short shots, voices, lines;   /* event cameras (CamSet 0), voice lines (VoiceOn), messages shown */
    char who[64];                       /* the records its events load or show: "Alfred, Claire" */
    char text[80];                      /* its first message with letters (the disc's text) */
} DevScene;
typedef const char* (*DevWho)(int id, int ver);   /* a record type's name, NULL: not listed */
/* the scenes of the expanded room file d (n bytes) of RM_<room>, appended to out (up to max); the count. *unused: cutscene
   events no script starts (left out) */
int  devcat_room(int room, const unsigned char* d, int n, DevWho who, DevScene* out, int max, int* unused);
/* our own name for the main scenes, NULL: none (then devcat_line describes it) */
const char* devcat_title(const DevScene* s);
/* "RM_1020 <room> | <name> | ev 1:1, 2:2 | MV_005 | Alfred | 18 shots, 14 lines, 9 voice" */
int  devcat_line(const DevScene* s, const char* room_name, char* buf, int n);
int  devcat_short(const DevScene* s);   /* a camera shot or two, nothing said, no one: hidden by "Hide short shots" */
/* the list, built once (teleport.c), then read-only */
void devcat_publish(DevScene* list, int n, int unused);
int  devcat_count(void);                /* 0 until read */
const DevScene* devcat_get(int i);
int  devcat_unused(void);
/* playing one (game thread, devcine_glue.c): chr 0 the part's character (the block's test, else Chris in stages 7..9,
   Claire before), 1 Claire, 2 Chris, 3 as now */
void devcat_play(int i, int chr);
int  devcat_take(int* chr);             /* game thread: the request, -1 none */
void devcat_say(const char* text);      /* game thread: what it did */
void devcat_status(char* buf, int n);   /* what the last PLAY did */
int  devcat_write(const char* path);    /* devcine_glue.c: the list as text, a scene per line; the scenes written, -1 error */

#ifdef __cplusplus
}
#endif
#endif
