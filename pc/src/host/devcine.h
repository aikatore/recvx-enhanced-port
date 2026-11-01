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

#ifdef __cplusplus
}
#endif
#endif
