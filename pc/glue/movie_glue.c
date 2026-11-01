/*
 * Replacement for ps2_MovieFunc.c (libmpeg + IPU + IOP audio streaming). Compiled with the
 * decomp's types.
 *
 * The PSS is played by pc/src/movie (a thread of its own reads, demultiplexes and decodes it
 * and queues the sound). Each game frame picks the frame to show and draws it with the game's
 * own vbrank_draw() sprite (copied from ps2_MovieFunc.c), so size, position and the screen
 * fades drawn over it are as on the PS2; the sprite's texture is the decoded frame, which the
 * render thread uploads and converts to RGB on the GPU (stream_movie, gs_gl.c).
 * Sofdec status goes PREP -> PLAYING -> PLAYEND. Frames advance one per two vblanks (29.97 Hz,
 * every second NTSC field) for an even cadence, and are pulled back to the sound's clock when
 * the two differ by a frame or more.
 */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_MovieFunc.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_MovieWork.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_dummy.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_loadtim2.h"

int    movie_open(FILE* f, uint32_t lsn, uint32_t size);
FILE*  pc_mod_fopen(const char* name, uint32_t* size);
void   movie_close(void);
void   movie_pause(int paused);
int    movie_frames_total(void);
double movie_clock(double* queued);
void   movie_size(int* w, int* h);
void   movie_stats(int* frames, double* dec_avg_ms, double* dec_max_ms, int* late);
int    movie_sound(int* rate, double* seconds, double* rms, int* paused);
unsigned short snd_synth_midi_active(void);
extern int CurrentBgmNo, CurrentBgSeNo[2];
extern unsigned int AdxPlayFlag[2];
void   stream_movie(uint32_t tbp, int index);
void   audio_movie_volume(float gain);
float  audio_movie_gain(void);
uint64_t ps2_vblank_count(void);
uint64_t SDL_GetTicksNS(void);

#define MOVIE_STAT_PREP    1
#define MOVIE_STAT_PLAYING 2
#define MOVIE_STAT_PLAYEND 3

int movie_draw;
StrFile infile;
VoBuf voBuf;
u_long128 test_tag[1400] __attribute__((aligned(64)));
RMI_WORK rmi;
MDSIZE_WORK mdSize;
u_long128 new_tags[64] __attribute__((aligned(64)));
static int __image_w__;
static int __image_h__;

static uint64_t start_vblank, paused_at, paused_total;
static int skip_movies, skip_frames, frame_shown = -1, clock_ofs;
static FILE* trace;   /* RECVX_MOVIE_TRACE (test aid): one line per game frame of a movie */

/* the trace's sound summary: the movie's own and the room's (BGM, background SE, MIDI ports) */
static void trace_sound(const char* when)
{
    int rate, paused, open;
    double s, rms;
    open = movie_sound(&rate, &s, &rms, &paused);
    fprintf(trace, "# %s: movie sound %s (%d Hz, %.2f s handed over, paused %d, gain %.2f, RMS at its gain %.0f); "
            "bgm %d (adx %u), background se %d %d, midi ports %#x\n", when, open ? "open" : rate ? "no output" : "none yet",
            rate, s, paused, audio_movie_gain(), rms, CurrentBgmNo, AdxPlayFlag[0], CurrentBgSeNo[0], CurrentBgSeNo[1],
            snd_synth_midi_active());
    fflush(trace);
}

void initAll(void)
{
    memset(&rmi, 0, sizeof(rmi));
    rmi.uiContFlag = 0x1;
    rmi.iMovieState = MOVIE_STAT_PREP;
    skip_movies = getenv("RECVX_SKIP_MOVIES") != NULL;   /* testing aid: one second per movie */
    frame_shown = -1;
    clock_ofs = 0;
    paused_total = 0; paused_at = 0;
    if (getenv("RECVX_MOVIE")) {   /* test aid: play MV_n instead */
        char name[32];
        sprintf(name, "\\MOVIE\\MV_%03d.PSS;1", atoi(getenv("RECVX_MOVIE")));
        sceCdSearchFile(&infile.fp, name);
    }
    if (getenv("RECVX_MOVIE_TRACE") && !trace) trace = fopen(getenv("RECVX_MOVIE_TRACE"), "w");
    if (!skip_movies) {
        /* the language's movie (Translated Pictures: lang.h pc_lang_file_dir), a mods folder's, or the disc's */
        char name[48];
        uint32_t size = 0;
        FILE* f;
        snprintf(name, sizeof(name), "MOVIE/%s", infile.fp.name);
        if (strchr(name, ';')) *strchr(name, ';') = 0;
        f = infile.fp.name[0] ? pc_mod_fopen(name, &size) : NULL;
        if (!movie_open(f, f ? 0 : infile.fp.lsn, f ? size : infile.fp.size)) printf("movie: cannot decode %s\n", name);
        if (trace) fprintf(trace, "# %s %s, %u bytes\n", name, f ? "from a file" : "from the disc", f ? size : infile.fp.size);
    }
    movie_size(&__image_w__, &__image_h__);
    if (trace) { fprintf(trace, "# movie at sector %u, %u bytes on the disc\n", infile.fp.lsn, infile.fp.size); trace_sound("opened"); }
}

void readMpeg(void)
{
    uint64_t now = ps2_vblank_count(), t0 = SDL_GetTicksNS();
    int index, total;
    double a, queued;

    if (rmi.iMovieState == MOVIE_STAT_PREP) {
        rmi.iMovieState = MOVIE_STAT_PLAYING;
        rmi.iMovieFrame = Ps2_vcount;
        start_vblank = now;
        skip_frames = 0;
        changeInputVolume(0x7FFF);   /* full, as the PS2's audioDecResume: a movie skipped with a fade left it at 0 */
        movie_pause(0);
        if (trace) trace_sound("playing");
    }
    if (rmi.uiContFlag & 2) {   /* paused by the game */
        if (!paused_at) { paused_at = now; movie_pause(1); }
        movie_draw = 1;
        return;
    }
    if (paused_at) { paused_total += now - paused_at; paused_at = 0; movie_pause(0); }

    movie_draw = 1;
    if (skip_movies) {
        /* counted in game frames, not vblanks: a test run then reaches the same game frame
           after the movie however its frames were timed */
        if (++skip_frames >= 30) rmi.iMovieState = MOVIE_STAT_PLAYEND;
        return;
    }
    index = (int)((now - start_vblank - paused_total) / 2);
    a = movie_clock(&queued);
    if (a >= 0) {   /* the sound's position in frames: follow it once a whole frame apart */
        double err = a * 30000.0 / 1001.0 - (index + clock_ofs);
        if (err >= 1.0 || err <= -1.0) clock_ofs += (int)err;
    }
    index += clock_ofs;
    if (index < frame_shown) index = frame_shown;   /* never backwards */
    if (index < 0) index = 0;
    total = movie_frames_total();
    if (trace) {
        fprintf(trace, "%.3f vb=%.0f idx=%d dec_us=%.0f a=%.3f aq=%.3f av_ms=%.1f\n", t0 / 1e9, (double)(now - start_vblank), index,
                (SDL_GetTicksNS() - t0) / 1000.0, a, queued, a >= 0 ? (a - index * 1.001 / 30.0) * 1000 : 0.0);
        fflush(trace);
    }
    if (total >= 0 && index >= total) { rmi.iMovieState = MOVIE_STAT_PLAYEND; return; }
    voBuf.data = NULL;
    frame_shown = index;
}

void termAll(void)
{
    if (trace && !skip_movies) {
        int n, late;
        double avg, mx;
        movie_stats(&n, &avg, &mx, &late);
        fprintf(trace, "# %d frames decoded, %.2f ms average, %.2f ms longest; shown before decoded: %d\n", n, avg, mx, late);
        trace_sound("end");
    }
    movie_close();
    rmi.iMovieState = 0;
}

int pc_movie_on(void) { return rmi.iMovieState != 0; }   /* a movie is set up or playing (view_glue.c pc_menu_smooth) */

/* vblanks until the playing movie of `frames` pictures would end by itself (readMpeg's end test),
   all of it before it plays; a skipped ending adds them to the play time (sdfunc.c) */
int pc_movie_vblanks_left(int frames)
{
    int64_t left;
    if (rmi.iMovieState != MOVIE_STAT_PLAYING) return 2 * (skip_movies ? 30 : frames);
    if (skip_movies) return 2 * (30 - skip_frames);   /* RECVX_SKIP_MOVIES: 30 game frames */
    left = 2 * (int64_t)(frames - clock_ofs) - (int64_t)(ps2_vblank_count() - start_vblank - paused_total);
    return left > 0 ? (int)left : 0;
}

/* the SPU2's sound data input volume, 0..0x7FFF (full); the game's ps2mwPlySetOutVol gives 0x7FFF at 0 dB */
void changeInputVolume(u_int val) { audio_movie_volume((val & 0xFFFF) / 32767.0f); }
int sendToIOP(int dst, u_char* src, int size) { (void)dst; (void)src; (void)size; return 0; }

/* ---- from ps2_MovieFunc.c ---- */

/* the PS2 uploaded the IPU's output to GS memory here (macroblock columns); the render thread
   now takes the frame from the player itself (stream_movie in vbrank_draw) */
void setImageTag(u_int *tags, void *image) { (void)tags; (void)image; }

void vbrank_draw(void)
{
    unsigned long* tag;
    const unsigned long giftag_eop[2] = { SCE_GIF_SET_TAG(0, 1, 0, 0, 0, 1), 0x000000000000000EL };
    int u, v;
    int x, y;
    int w, h;

    if (skip_movies || frame_shown < 0) return;
    stream_movie(14208, frame_shown);   /* the texture TEX0_2 below samples: this frame */
    D2_SyncTag();

    tag = (void*)((u_int)new_tags | UNCBASE);

    u = (__image_w__ - 1) * 16;
    v = (__image_h__ - 1) * 16;

    y = (mdSize.sDispY + 1824) * 16;

    w = mdSize.sWidth * 16;
    h = mdSize.sHeight * 16;
    x = 27648;
    if (pc_movie_zoom() && w > 0 && h > 0) {
        /* option: the sprite scaled evenly about the screen centre until it fills the widened
           view's width or the 480 displayed lines (Y 1808..2288). The streams have no bars of
           their own: the game letterboxes by drawing them 640 x 320 (2:1), so what leaves the
           screen is only black */
        float sw = 640.0f * pc_hor_scale() / mdSize.sWidth, sh = 480.0f / mdSize.sHeight, s = sw < sh ? sw : sh;
        if (s > 1.0f) {
            w = (int)(w * s + 0.5f);
            h = (int)(h * s + 0.5f);
            x = (1728 + 320) * 16 - w / 2;
            y = (1824 + 224) * 16 - h / 2;
        }
    }

    *tag++ = DMAend | 0x9;
    *tag++ = 0;

    *tag++ = giftag_eop[0] | 0x8;
    *tag++ = giftag_eop[1];

    *tag++ = 0;
    *tag++ = SCE_GS_TEXFLUSH;

    *tag++ = SCE_GS_SET_TEX1_1(0, 0, 1, 1, 0, 0, 0);
    *tag++ = SCE_GS_TEX1_2;

    *tag++ = SCE_GS_SET_TEX0_2(14208, 5, SCE_GS_PSMCT32, 9, 9, 0, SCE_GS_DECAL, 0, SCE_GS_PSMCT32, 0, 0, 0);
    *tag++ = SCE_GS_TEX0_2;

    *tag++ = SCE_GS_SET_PRIM(SCE_GS_PRIM_SPRITE, 0, 1, 0, 0, 0, 1, 1, 0);
    *tag++ = SCE_GS_PRIM;

    *tag++ = SCE_GS_SET_UV(8, 8);
    *tag++ = SCE_GS_UV;

    *tag++ = SCE_GS_SET_XYZ2(x, y, 0);
    *tag++ = SCE_GS_XYZ2;

    *tag++ = SCE_GS_SET_UV(u + 8, v + 8);
    *tag++ = SCE_GS_UV;

    *tag++ = SCE_GS_SET_XYZ2(w + x, y + h, 0);
    *tag++ = SCE_GS_XYZ2;

    SyncPath();

    loadImage(new_tags);

    D2_SyncTag();
}

/* the movie set up or playing ("MV_005.PSS;1", as the disc names it), NULL none (the --dev cutscene log) */
const char* pc_movie_name(void) { return rmi.iMovieState && infile.fp.name[0] ? infile.fp.name : NULL; }
