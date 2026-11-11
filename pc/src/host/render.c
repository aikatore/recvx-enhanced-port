/*
 * Render thread (the main thread: it owns the window, OpenGL and the event pump).
 *
 * Game frames arrive from the game thread as recorded GS streams (gs/stream.c). Each is
 * executed once (the real image) and then presented in step with the display: with
 * interpolation, every display refresh during the following game-frame period shows the
 * frame replayed at t = elapsed / period between the previous and the new frame, so any
 * refresh rate gets evenly spaced in-between images; game logic stays at its original rate.
 */
#include "../ps2_platform.h"
#include "../gs/gs.h"
#include "../gs/stream.h"
#include "prof.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>

uint64_t gs_frame_count(void);

extern SDL_Window* g_window;

static double display_hz(void)
{
    static double test_hz = -1;   /* RECVX_DISPLAY_HZ (test aid): the refresh rate to assume, e.g. 1000 with vsync off */
    if (test_hz < 0) test_hz = SDL_getenv("RECVX_DISPLAY_HZ") ? SDL_atof(SDL_getenv("RECVX_DISPLAY_HZ")) : 0;
    if (test_hz > 1) return test_hz;
    const SDL_DisplayMode* m = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(g_window));
    return m && m->refresh_rate > 1.0f ? m->refresh_rate : 60.0;
}

/* images per second: the display's refresh, or the "Frame rate limit" below it */
static double present_hz(void)
{
    static const int fps_limits[] = { 0, 30, 60, 90, 120, 144 };   /* settings.c names */
    double hz = display_hz();
    int lim = g_config.fps_limit > 0 && g_config.fps_limit < 6 ? fps_limits[g_config.fps_limit] : 0;
    return lim && lim < hz ? lim : hz;
}

double render_present_hz(void) { return present_hz(); }   /* (gs_gl.c drs_update) */

void render_loop(void)
{
    Frame *prev = NULL, *cur = NULL;
    uint64_t arrived = 0, pace_until = 0;
    double swap_avg = 0.0;   /* how long the buffer swap blocks, smoothed */
    double period = 1001.0 / 30000.0 * 1e9;   /* game frame period, measured below */
    int interp = 0, shown = 0;   /* refreshes presented since the frame arrived */
    int check_replay = SDL_getenv("RECVX_REPLAY_CHECK") && SDL_atoi(SDL_getenv("RECVX_REPLAY_CHECK"));
    float fixed_t = SDL_getenv("RECVX_INTERP_T") ? (float)SDL_atof(SDL_getenv("RECVX_INTERP_T")) : 0.0f;   /* test aid: first refresh shows t */
    FILE* trace = SDL_getenv("RECVX_TRACE_PRESENT") ? fopen(SDL_getenv("RECVX_TRACE_PRESENT"), "w") : NULL;   /* test aid */
    prof_thread(PROF_RIDLE);
    for (;;) {
        Frame* f;
        uint64_t now;
        host_pump_events();
        /* nothing to show yet: wait for the game; otherwise take what has arrived, waiting for
           it until the next refresh is due (a frame arriving meanwhile is executed at once) */
        now = SDL_GetTicksNS();
        f = stream_pop(!cur ? 20 : pace_until > now ? (int)((pace_until - now) / 1000000) : 0);   /* the rest: SDL_DelayPrecise */
        if (f) {
            prof_push(PROF_EXEC);
            gs_frame_begin();
            stream_exec(f);
            prof_pop();
            if (stream_is_partial(f)) { stream_release(f); continue; }   /* download sync point */
            now = SDL_GetTicksNS();
            if (arrived) {
                double d = (double)(now - arrived);
                if (d > 10e6 && d < 100e6) period += 0.1 * (d - period);
            }
            arrived = now;
            shown = 0;
            prof_push(PROF_INTERP);
            interp = g_config.interpolate && stream_match(cur, f);
            prof_pop();
            gs_frame_executed(interp);
            if (interp && check_replay) gs_replay_check(f);
            stream_release(prev);
            prev = cur;
            cur = f;
        }
        if (!cur) continue;
        now = SDL_GetTicksNS();
        if (pace_until > now) {   /* the refresh is not due yet: a frame that arrived is executed, its
                                     image waits for the refresh like any other (shown at once, it cut
                                     the previous image short); woken early (ms timeout): the rest */
            SDL_DelayPrecise(pace_until - now);
            if (!f) continue;
            now = SDL_GetTicksNS();
        }
        {
            /* With a display rate that is a whole multiple of the game's (60/120/240 Hz), step by
               refreshes: t = 1/n, 2/n, ... 1, evenly spaced however the game thread's frame
               delivery jitters against vsync. Otherwise by time since arrival. */
            double n = period * 1e-9 * present_hz();
            uint64_t tick = (uint64_t)(1e9 / present_hz());
            float t;
            uint64_t swap;
            if (!interp) t = 1.0f;
            else if (fixed_t > 0.0f) t = shown ? 1.0f : fixed_t;
            else if (g_config.vsync && n > 1.5 && fabs(n - floor(n + 0.5)) < 0.1) t = (float)((shown + 1) / floor(n + 0.5));
            else t = (float)((double)(now - arrived) / period);
            /* the next game frame is already waiting (the game runs below its rate): an
               in-between image of this one would only delay it (a whole replay of the scene) */
            if (t < 1.0f && fixed_t <= 0.0f && stream_queued()) continue;
            /* the GPU is still two frames or images behind most of a refresh later (a heavy
               scene at a high resolution and rate): this refresh gets no in-between image.
               Replays queued up behind each other would hold up the next game frame's execution,
               and with it the game thread (one frame in flight): the game would slow down. */
            if (t < 1.0f && fixed_t <= 0.0f && !gs_gpu_ready(tick / 8 * 7)) {
                shown++;
                pace_until = pace_until && pace_until + tick > now ? pace_until + tick : now + tick;
                continue;
            }
            {   /* the name tag moves with the 3D */
                float x, y;
                int fl = stream_point_at(interp ? prev : NULL, cur, t, &x, &y);
                overlay_point(fl, x, y);            }
            swap = gs_present(cur, t);
            shown++;
            if (trace) fprintf(trace, "%llu %.3f %llu %llu\n", (unsigned long long)gs_frame_count(), t, (unsigned long long)(now / 1000),
                               (unsigned long long)(swap / 1000));
            /* pace to the display refresh instead of spinning: without vsync, and with vsync when
               the swap does not wait (the offscreen driver, some compositors): presenting as fast
               as the GPU allows starved the frames themselves (RM_1310 headless: 197 -> 37 ms a
               game frame in vanilla mode). Whether the swap waits is its own duration: one that
               waits blocks for part of a refresh on average. Presents start one refresh apart
               (the wait is the stream_pop above), so every image, in-between or not, is shown
               for one refresh. */
            {   /* on a fixed grid (a late present does not shift the following ones) */
                swap_avg += ((double)swap - swap_avg) / 8;
                if (g_config.vsync && swap_avg >= tick / 8 && present_hz() == display_hz()) pace_until = 0;   /* limited: always paced */
                else pace_until = pace_until && pace_until + tick > now ? pace_until + tick : now + tick;
            }
        }
    }
}
