/* --dev cutscene log, the game's side (docs/DEV_TOOLS.md; the list: pc/src/host/devcine.c, shown by overlay.cpp).
 *
 * A scene plays while a movie does (pc_movie_name) or, in the room, while the cast's cutscene test holds (cast_glue.c
 * cast_scene: the cinema mode cb_flg 0x4, an event camera st_flg 0x1000000, the player hidden by an event). Which
 * event it is: event.c tells us which event each task level runs (Event_init -> pc_dev_event_start) and which task's
 * script sets the camera or cinema mode or starts a movie (bhCamSet, bhCineSet, bhMovieStart -> pc_dev_event_op);
 * an entry lists those as "level:event", as RECVX_TEST_EVENT takes them. A scene no op names (the player hidden by
 * other means) shows the task started last with a "?". Half a second without a scene ends one; a new room or movie
 * starts the next (connected scenes). Off without --dev: the hooks return at once.
 */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../src/host/actors.h"
#include "../src/host/enemies.h"
#include "../src/host/devcine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int g_dev_tools;
extern BH_SCEWORK bhEtask[16];
extern BH_SCEWORK* bhCetask;
int room_file_now(void);                /* enemyopt_glue.c */
int cast_describe(char* buf, int n);    /* cast_glue.c */
const char* pc_movie_name(void);        /* movie_glue.c */
void host_log(const char* fmt, ...);

#define QUIET 15                        /* game frames without a scene that end one */
static int lvl_ev[16] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };   /* the event each task level runs */
static unsigned lvl_at[16], tick;       /* when it started (devcine_frame's count) */
static short touched[8];                /* level << 8 | event the ops named since the last frame */
static int touched_n, movie_ev = -1;    /* movie_ev: the event of the last MovieStart */

void pc_dev_event_start(int level, unsigned int id)
{
    if (!g_dev_tools || level < 0 || level >= 16) return;   /* (co-op's replay task is no level) */
    lvl_ev[level] = (int)(id & 0xFF);
    lvl_at[level] = tick;
}

void pc_dev_event_op(int movie)
{
    int l, v, k;
    if (!g_dev_tools || !bhCetask) return;
    l = (int)(bhCetask - bhEtask);
    if (l < 0 || l >= 16 || !bhEtask[l].status || lvl_ev[l] < 0) return;   /* (the room's scd scripts run outside the tasks) */
    v = l << 8 | lvl_ev[l];
    if (movie) { movie_ev = v; return; }
    for (k = 0; k < touched_n && touched[k] != v; k++);
    if (k == touched_n && touched_n < 8) touched[touched_n++] = (short)v;
}

/* "ev 3:7, 2:12" (a guess: "ev 3:7?") */
static void ev_text(char* what, int n, const short* ev, int k, int guess)
{
    int i, len = snprintf(what, (size_t)n, "ev");
    for (i = 0; i < k && len < n - 1; i++) len += snprintf(what + len, (size_t)(n - len), "%s %d:%d%s", i ? "," : "", ev[i] >> 8, ev[i] & 0xFF, guess ? "?" : "");
    if (!k) snprintf(what, (size_t)n, "ev ?");
}

/* the cast, the double and the player as shown now */
static void describe(char* out, int n)
{
    const EnemyOpts* o = enemy_opts();
    char sw[200];
    int len = 0, m;
    out[0] = 0;
    if (cast_describe(sw, sizeof sw) > 0 && o)
        len += snprintf(out + len, (size_t)(n - len), "cast seed %d %s (scene %d): %s", o->seed, o->cast_assign ? "per cutscene" : "per character",
                        o->cast_assign ? room_file_now() + 1 : 0, sw);
    if (len < n - 1 && actor_double_acting() && (m = actor_shown_model(ACTOR_DOUBLE)) >= 0)
        len += snprintf(out + len, (size_t)(n - len), "%sdouble -> %s", len ? "; " : "", actor_model(m) ? actor_model(m)->name : "a player file");
    if (len < n - 1 && (m = actor_shown_model(0)) >= 0)
        snprintf(out + len, (size_t)(n - len), "%splayer -> %s", len ? "; " : "", actor_model(m) ? actor_model(m)->name : "a player file");
}

/* a scene ended; test aid RECVX_DEVCINE_LOG=1: its line (as Copy and Export give it) in the log */
static void scene_end(void)
{
    char* t;
    int n;
    devcine_end();
    if (!getenv("RECVX_DEVCINE_LOG") || !(t = malloc(DEVCINE_TEXT))) return;
    n = devcine_text(t, DEVCINE_TEXT);
    if (n > 0 && t[n - 1] == '\n') t[--n] = 0;
    while (n > 0 && t[n - 1] != '\n') n--;
    host_log("devcine: %s\n", t + n);
    free(t);
}

/* every game frame (cheat_frame) */
void devcine_frame(void)
{
    static int open, kind, room, quiet, frames, seen, nev, guess;
    static short ev[6];
    static char movie[16];
    char name[16] = "", what[64], flags[48], cast[240];
    const char* mv;
    int now = 0, r, k, l, bits;
    if (!g_dev_tools || !sys) return;
    tick++;
    if ((mv = pc_movie_name())) { snprintf(name, sizeof name, "%s", mv); if (strchr(name, ';')) *strchr(name, ';') = 0; }
    bits = (mv ? 1 : 0) | ((sys->cb_flg & 0x4) ? 2 : 0) | ((sys->st_flg & 0x1000000) ? 4 : 0) | (plp && (plp->stflg & 0x1000000) ? 8 : 0);
    if (mv) now = DC_FMV;
    else if (rom && (bits & 14)) now = DC_EVENT;
    r = room_file_now();
    if (open && now && (now != kind || r != room || strcmp(name, movie))) { scene_end(); open = 0; }   /* the next scene */
    if (open && !now && ++quiet >= QUIET) { scene_end(); open = 0; }
    if (!open && now) {
        DevCine e;
        memset(&e, 0, sizeof e);
        open = 1; kind = now; room = r; quiet = 0; frames = 0; seen = 0; nev = 0; guess = 0;
        snprintf(movie, sizeof movie, "%s", name);
        if (now == DC_EVENT && !touched_n) {   /* no op named it: the task started last */
            int best = -1;
            for (l = 0; l < 16; l++)
                if (bhEtask[l].status && lvl_ev[l] >= 0 && (best < 0 || lvl_at[l] >= lvl_at[best])) best = l;
            if (best >= 0) { ev[nev++] = (short)(best << 8 | lvl_ev[best]); guess = 1; }
        }
        if (now == DC_FMV && movie_ev >= 0) { ev[nev++] = (short)movie_ev; movie_ev = -1; }
        e.kind = now; e.room = r; e.ply = sys->ply_id & 3;
        if (now == DC_FMV) snprintf(e.key, sizeof e.key, "%s", movie);
        else if (nev || touched_n) snprintf(e.key, sizeof e.key, "%d:%d", (nev ? ev[0] : touched[0]) >> 8, (nev ? ev[0] : touched[0]) & 0xFF);
        devcine_begin(&e);
    }
    if (open && kind == DC_EVENT)
        for (k = 0; k < touched_n; k++) {
            for (l = 0; l < nev && ev[l] != touched[k]; l++);
            if (l == nev && nev < 6) { if (guess) { nev = 0; guess = 0; } ev[nev++] = touched[k]; }   /* (a named one replaces the guess) */
        }
    touched_n = 0;
    if (!open) return;
    if (now) { quiet = 0; frames++; seen |= bits; }
    if (kind == DC_FMV) {
        int len = snprintf(what, sizeof what, "%s", movie);
        if (nev) snprintf(what + len, sizeof what - (size_t)len, " (ev %d:%d)", ev[0] >> 8, ev[0] & 0xFF);
    } else ev_text(what, sizeof what, ev, nev, guess);
    snprintf(flags, sizeof flags, "%s%s%s%s", (seen & 1) ? "+movie" : "", (seen & 2) ? "+cinema" : "", (seen & 4) ? "+event camera" : "", (seen & 8) ? "+player hidden" : "");
    describe(cast, sizeof cast);
    devcine_update(frames, what, flags[0] ? flags + 1 : flags, cast);
}
