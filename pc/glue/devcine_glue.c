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
#include "../src/host/cast.h"
#include "../src/host/cheat.h"
#include "../src/host/coop.h"
#include "../src/host/fileio.h"
#include <stdarg.h>
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
int Event_exec(unsigned int task_level, unsigned int evt_id);   /* event.c */
extern unsigned char* bhScePtr;
extern unsigned int* G_Sp;
extern int bhIfelFlg;
extern unsigned int (*bhScenarioJmpT[256])();
static void play_frame(void);           /* the scene list's PLAY (below) */

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
/* a scene ends: its line goes to the log file too */
static void ended(void)
{
    DevCine* l = malloc(sizeof(DevCine) * DC_MAX);
    char line[512];
    int n = l ? devcine_snapshot(l, NULL) : 0;
    devcine_end();
    if (n) { devcine_format(&l[n - 1], 1, line, sizeof line); host_log("devcine: logged %s\n", line); }
    free(l);
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
    play_frame();
    if ((mv = pc_movie_name())) { snprintf(name, sizeof name, "%s", mv); if (strchr(name, ';')) *strchr(name, ';') = 0; }
    bits = (mv ? 1 : 0) | ((sys->cb_flg & 0x4) ? 2 : 0) | ((sys->st_flg & 0x1000000) ? 4 : 0) | (plp && (plp->stflg & 0x1000000) ? 8 : 0);
    if (mv) now = DC_FMV;
    else if (rom && (bits & 14)) now = DC_EVENT;
    r = room_file_now();
    if (open && now && (now != kind || r != room || strcmp(name, movie))) { scene_end(); open = 0; }   /* the next scene */
    if (open && !now && ++quiet >= QUIET) { scene_end(); open = 0; }
    if (open && now && (now != kind || r != room || strcmp(name, movie))) { ended(); open = 0; }   /* the next scene */
    if (open && !now && ++quiet >= QUIET) { ended(); open = 0; }
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

/* ---------------------------------------------------------------- the scene list (devcine.h DevScene; F6 "Scenes") */
static DevScene* scan_list;
static int scan_n, scan_cap, scan_unused, scan_rooms;

/* a record type's name for the list: the story characters, else the "You appear as" catalogue's */
static const char* who(int id, int ver)
{
    static const char* const chr[CAST_CHARACTERS] = { "Claire", "Chris", "Steve", "Wesker", "Alfred", "Alexia", "Rodrigo" };
    int c = cast_character(id, ver), i;
    if (c >= 0) return chr[c];
    if (id == 1 || id == 51 || id == 52) return "Zombies";
    for (i = 0; i < ACTOR_MODELS && !(actor_models[i].id == id && actor_models[i].mdlver == ver); i++) {}
    if (i == ACTOR_MODELS) for (i = 0; i < ACTOR_MODELS && actor_models[i].id != id; i++) {}
    return i < ACTOR_MODELS ? actor_models[i].name : NULL;
}

/* teleport.c's scan (its thread, with --dev): each room file as read, then the end */
void devcat_scan_room(int room, const unsigned char* d, int n)
{
    int u = 0;
    if (scan_n + 256 > scan_cap) {
        DevScene* p = realloc(scan_list, sizeof(DevScene) * (size_t)(scan_cap + 1024));
        if (!p) return;
        scan_list = p; scan_cap += 1024;
    }
    scan_n += devcat_room(room, d, n, who, scan_list + scan_n, 256, &u);
    scan_unused += u;
    scan_rooms++;
}
static int by_room(const void* a, const void* b)
{
    const DevScene *x = a, *y = b;
    return x->room != y->room ? x->room - y->room : x->from != y->from ? x->from - y->from : (x->ev[0] > y->ev[0]) - (x->ev[0] < y->ev[0]);
}
void devcat_scan_done(void)
{
    if (!scan_list) return;
    qsort(scan_list, (size_t)scan_n, sizeof(DevScene), by_room);
    devcat_publish(scan_list, scan_n, scan_unused);
    host_log("devcine: %d scenes in %d room files (%d cutscene events no script starts left out)\n", scan_n, scan_rooms, scan_unused);
}

/* the list as text, one scene per line (F6 Scenes "Export list", RECVX_SCENE_LIST); the scenes written, -1 error */
int devcat_write(const char* path)
{
    char line[512], nm[96];
    int i, n = devcat_count();
    FILE* f = n ? pc_fopen(path, "w") : NULL;
    if (!f) return -1;
    fprintf(f, "# recvx cutscene list: %d scenes, read from the disc (and the mods folder); F6 > Scenes plays one\n", n);
    for (i = 0; i < n; i++) {
        const DevScene* s = devcat_get(i);
        devcat_line(s, tp_room_name(tp_find(s->room / 1000, s->room / 10 % 100), 0, nm, sizeof nm), line, sizeof line);
        fprintf(f, "%s\n", line);
    }
    return fclose(f) == 0 ? n : -1;
}

static void say(const char* fmt, ...)
{
    char t[200];
    va_list a;
    va_start(a, fmt);
    vsnprintf(t, sizeof t, fmt, a);
    va_end(a);
    devcat_say(t);
    host_log("devcine: %s\n", t);
}

static int in_room(void) { return sys->tk_flg && (sys->sp_flg & 0x2) && !(sys->cb_flg & 0x1) && !(sys->gm_flg & 0x9); }

/* the room script's part that starts scene s, run as its scripts run (bhScenarioCheck), on a copy ending in End: the
   flags it sets, the cinema, the EvtOn (the tests of a block inside it are the game's own) */
static void run_block(const DevScene* s)
{
    unsigned char *keep = bhScePtr, *p = malloc(s->blen + 2);
    unsigned int* sp = G_Sp;
    int fl = bhIfelFlg;
    BH_SCEWORK* ct = bhCetask;
    if (!p) return;
    memcpy(p, (unsigned char*)rom->evtp + s->block, s->blen);
    p[s->blen] = p[s->blen + 1] = 0;
    bhScePtr = p; bhIfelFlg = 0; bhCetask = bhEtask;
    for (;;) {
        while (bhScenarioJmpT[*bhScePtr]() != 0) {}
        if (bhIfelFlg <= 0) break;
        bhScePtr = (unsigned char*)--G_Sp;
        bhIfelFlg--;
    }
    bhScePtr = keep; G_Sp = sp; bhIfelFlg = fl; bhCetask = ct;
    free(p);
}

static int play_i = -1, play_wait, play_in;

static void play_start(int i, int chr)
{
    const DevScene* s = devcat_get(i);
    int stg, battle, want, k;
    if (!s) return;
    stg = s->room / 1000;
    battle = stg == 5 && s->room / 10 % 100 >= 50;
    if (coop_on) { say("Not in co-op."); return; }
    if (!in_room() || !plp) { say("Start or load a game first (and finish going through the door)."); return; }
    if (pc_movie_name()) { say("Wait for the movie to end."); return; }
    if (battle != (sys->gm_mode == 3)) { say(battle ? "A Battle Game room: play it from the Battle Game." : "Not in the Battle Game."); return; }
    for (k = 0; k < s->nflag; k++) {   /* the block's story flag tests made to hold: the room is entered as before the scene */
        int set = s->flag[k].set, bit = s->flag[k].bit;
        unsigned int* w = set == 1 ? sys->ev_flg : set == 2 ? sys->ky_flg : set == 3 ? sys->ed_flg : set == 7 ? sys->it_flg : sys->mp_flg;
        unsigned m = 0x80000000u >> (bit & 31);
        if (bit >= 256 * (set == 8 ? 1 : set == 2 || set == 7 ? 2 : 4)) continue;
        if (s->flag[k].on) w[bit >> 5] |= m; else w[bit >> 5] &= ~m;
    }
    /* Auto: the block's test, else the part's (Chris's 7..9; there Claire's scenes, as 9280, show her and not him) */
    want = chr == 1 ? 0 : chr == 2 ? 1 : chr == 3 || battle ? sys->ply_id : s->ply < 4 ? s->ply :
           stg >= 7 && stg <= 9 && !(strstr(s->who, "Claire") && !strstr(s->who, "Chris")) ? 1 : 0;
    if (want != sys->ply_id) { sys->cng_pid = want; sys->cb_flg |= 0x80; }   /* as script op PlayerChangeSet (RECVX_PLAYER) */
    cheat_room_warp(stg, s->room / 10 % 100, 0, s->room % 10);
    play_i = i; play_wait = 0; play_in = 0;
    say("RM_%04d ev %d:%d: teleporting as %s (%d story flags set)...", s->room, s->ev[0] >> 8, s->ev[0] & 0xFF, devcine_who(want), s->nflag);
}

/* every game frame (devcine_frame): a PLAY taken, then, once in the room, its scene started */
static void play_frame(void)
{
    static const char *tl = (const char*)-1, *list;
    static int listed;
    int chr = 0, i = devcat_take(&chr), k;
    const DevScene* s;
    if (tl == (const char*)-1) {   /* test aids: RECVX_TEST_SCENE="frame:RM_1020:1:1[:chr],..." (frame: devcine_frame's count; chr
                                      as devcat_play's), RECVX_SCENE_LIST=path (the list once read, with the save menu's names) */
        tl = getenv("RECVX_TEST_SCENE");
        list = getenv("RECVX_SCENE_LIST");
    }
    if ((tl || (list && !listed)) && in_room()) tp_count();   /* (in a game: the first call starts the scan) */
    if (list && !listed && devcat_count() && g_cheat.rooms[1][0]) { listed = 1; host_log("devcine: the list -> %s: %d\n", list, devcat_write(list)); }
    if (tl && devcat_count() && i < 0) {
        int f, r, l, e, n = 0;
        if (sscanf(tl, "%d:RM_%d:%d:%d%n", &f, &r, &l, &e, &n) == 4 && (int)tick >= f) {
            int c = tl[n] == ':' ? atoi(tl + n + 1) : 0;
            for (k = 0; k < devcat_count() && !(devcat_get(k)->room == r && devcat_get(k)->ev[0] == (l << 8 | e)); k++) {}
            if (k < devcat_count()) { i = k; chr = c; }
            else say("test: no scene RM_%04d ev %d:%d in the list", r, l, e);
            tl = strchr(tl, ',') ? strchr(tl, ',') + 1 : NULL;
        } else if (n == 0 && sscanf(tl, "%d", &f) != 1) tl = NULL;
    }
    if (i >= 0) play_start(i, chr);
    if (play_i < 0 || !(s = devcat_get(play_i))) return;
    if (room_file_now() != s->room || !in_room()) {
        play_in = 0;
        if (++play_wait > 900) { say("RM_%04d: the room was not reached.", s->room); play_i = -1; }
        return;
    }
    if (++play_in < 4) return;   /* (its entry script ran; the room's own script may start the scene itself) */
    play_i = -1;
    for (k = 0; k < s->nev; k++) {
        int l = s->ev[k] >> 8;
        if (l < 16 && bhEtask[l].status && lvl_ev[l] == (s->ev[k] & 0xFF)) break;
    }
    if (k < s->nev) { say("RM_%04d ev %d:%d: started by the room's own script.", s->room, s->ev[k] >> 8, s->ev[k] & 0xFF); return; }
    if (s->blen && rom && rom->evtp) run_block(s);
    else for (k = 0; k < s->nev; k++) Event_exec((unsigned)(s->ev[k] >> 8), (unsigned)(s->ev[k] & 0xFF));
    say("RM_%04d ev %d:%d: started (%s).", s->room, s->ev[0] >> 8, s->ev[0] & 0xFF, s->blen ? "the script's block that starts it" : "its events");
}
