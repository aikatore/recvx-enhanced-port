/* Per-frame timing (exclusive time per category), for --profile and the F1 overlay. */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* game thread: logic (default), vu0, vu1, gif recording, queue (waiting for the render
   thread), wait (vblank pacing); render thread: exec (GS emulation + GL), interp (replays),
   present (swap/vsync), idle (waiting for a frame, default) */
enum { PROF_LOGIC, PROF_VU0, PROF_VU1, PROF_GIF, PROF_QUEUE, PROF_WAIT,
       PROF_EXEC, PROF_INTERP, PROF_PRESENT, PROF_RIDLE, PROF_COUNT };
extern const char* const prof_names[PROF_COUNT];

typedef struct { float ms[PROF_COUNT]; float total; } ProfFrame;

extern int prof_enabled;          /* off: push/pop cost one branch */
extern int g_perf_graph;          /* the new menu's Performance Graph: the panel on screen (overlay.cpp) */
void prof_thread(int base_cat);   /* category for time outside any push on this thread */
void prof_push(int cat);          /* time is charged to the innermost category */
void prof_pop(void);
void prof_frame_end(void);        /* game thread: a game frame was submitted */
void prof_open_csv(const char* path);
int  prof_history(ProfFrame* out, int n);   /* last n frames, oldest first */

#ifdef __cplusplus
}
#endif
