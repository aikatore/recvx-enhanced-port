/*
 * Movie player. A thread of its own streams the PSS from the disc image (own file handle) or a file,
 * keeps the PCM soundtrack queued ahead on the output device and decodes the video into a
 * ring of YUV 4:2:0 frames, so neither the game thread nor the render thread decodes.
 *
 * The game thread (pc/glue/movie_glue.c) picks the frame to show by the sound's clock; the
 * render thread (gs_gl.c gs_movie_bind) takes it from the ring, uploads the three planes once
 * and converts them to RGB on the GPU.
 */
#include "../ps2_platform.h"
#include "../audio/audio.h"
#include "../host/fileio.h"
#include "movie.h"
#include "mpeg2.h"
#include "pss.h"
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

#define RING 8                 /* decoded frames ahead of the one shown */
#define AUDIO_AHEAD 0.5        /* seconds of sound kept queued */
#define CHUNK (16 * 2048)

static struct {
    SDL_Mutex* m;               /* the ring, the audio counters */
    SDL_Thread* th;
    SDL_AtomicInt quit;
    FILE* f;
    Pss* pss;
    Mpeg2* dec;
    uint8_t* es; size_t es_len, es_cap;   /* video elementary stream not yet decoded */
    uint32_t lsn, size, off;
    /* ring: frame i is in slot[i % RING]; produced = frames decoded, consumed = the lowest
       frame still wanted (the one the render thread showed last) */
    uint8_t* slot[RING]; uint32_t serial[RING];
    int w, h, stride, produced, consumed, eof;
    int audio_rate, audio_open, paused; uint64_t audio_put;   /* paused: movie_pause's, also before the sound opens */
    /* statistics (RECVX_MOVIE_TRACE) */
    uint64_t dec_ns, dec_max_ns, wait_ns; int late;
    double sumsq;   /* the sound handed to the device, squared, at the stream's gain then */
} mv;
static uint32_t serial_next = 1;   /* frames ever decoded: a frame's identity for the render thread */

static void on_frame(const Mpeg2Frame* f, void* user)
{
    size_t ys = (size_t)f->y_stride * f->height, cs = (size_t)f->c_stride * (f->height / 2);
    uint8_t* s;
    (void)user;
    for (;;) {   /* room in the ring (the last frames at the end of the file come in one call) */
        int room;
        SDL_LockMutex(mv.m);
        room = mv.produced - mv.consumed < RING;
        SDL_UnlockMutex(mv.m);
        if (room || SDL_GetAtomicInt(&mv.quit)) break;
        {
            uint64_t w0 = SDL_GetTicksNS();
            SDL_DelayNS(2000000);
            mv.wait_ns += SDL_GetTicksNS() - w0;   /* not decode time */
        }
    }
    if (SDL_GetAtomicInt(&mv.quit)) return;
    if (!mv.w) {
        mv.w = f->width; mv.h = f->height; mv.stride = f->y_stride;
        for (int i = 0; i < RING; i++) mv.slot[i] = malloc(ys + 2 * cs);
    }
    if (f->width != mv.w || f->height != mv.h || f->y_stride != mv.stride) return;   /* one size per movie */
    s = mv.slot[mv.produced % RING];
    memcpy(s, f->y, ys);
    memcpy(s + ys, f->cb, cs);
    memcpy(s + ys + cs, f->cr, cs);
    SDL_LockMutex(mv.m);
    mv.serial[mv.produced % RING] = serial_next++;
    mv.produced++;
    SDL_UnlockMutex(mv.m);
}

static void on_video(void* user, const uint8_t* d, size_t n)
{
    (void)user;
    if (mv.es_len + n > mv.es_cap) { mv.es_cap = (mv.es_len + n) * 2; mv.es = realloc(mv.es, mv.es_cap); }
    memcpy(mv.es + mv.es_len, d, n);
    mv.es_len += n;
}

static void on_audio(void* user, const int16_t* s, size_t frames, int rate)
{
    (void)user;
    SDL_LockMutex(mv.m);
    if (!mv.audio_rate) {   /* (may come after the game started the movie: its pause state, not paused) */
        mv.audio_rate = rate;
        if ((mv.audio_open = audio_movie_open(rate))) audio_movie_pause(mv.paused);
    }
    if (!mv.audio_open) { SDL_UnlockMutex(mv.m); return; }
    audio_movie_put(s, frames);
    mv.audio_put += frames;
    {
        double g = audio_movie_gain(), q = 0;
        for (size_t i = 0; i < frames * 2; i++) q += (double)s[i] * s[i];
        mv.sumsq += q * g * g;
    }
    SDL_UnlockMutex(mv.m);
}

/* read and demultiplex the next chunk of the file */
static void demux(void)
{
    static uint8_t chunk[CHUNK];
    uint32_t n = mv.size - mv.off < CHUNK ? mv.size - mv.off : CHUNK;
    if (fseeko(mv.f, (off_t)mv.lsn * 2048 + mv.off, SEEK_SET) != 0 || fread(chunk, 1, n, mv.f) != n) { mv.off = mv.size; return; }
    mv.off += n;
    if (!pss_feed(mv.pss, chunk, n)) mv.off = mv.size;
}

static double audio_queued_s(void) { return audio_movie_queued() / 4.0 / mv.audio_rate; }

/* one unit of work; 0 when there is nothing to do */
static int step(void)
{
    int room, done = mv.off >= mv.size;
    size_t used;
    unsigned before;
    if (mv.eof) return 0;
    if (!done && mv.audio_open && audio_queued_s() < AUDIO_AHEAD) { demux(); return 1; }   /* sound first */
    SDL_LockMutex(mv.m);
    room = mv.produced - mv.consumed < RING;
    before = (unsigned)mv.produced;
    SDL_UnlockMutex(mv.m);
    if (!room) return 0;
    {
        uint64_t t0 = SDL_GetTicksNS(), dt;
        used = mpeg2_decode_frame(mv.dec, mv.es, mv.es_len, done);
        dt = SDL_GetTicksNS() - t0 - mv.wait_ns;
        mv.wait_ns = 0;
        memmove(mv.es, mv.es + used, mv.es_len - used);
        mv.es_len -= used;
        SDL_LockMutex(mv.m);
        if ((unsigned)mv.produced != before) { mv.dec_ns += dt; if (dt > mv.dec_max_ns) mv.dec_max_ns = dt; }
        else if (done) mv.eof = 1;                 /* everything decoded */
        SDL_UnlockMutex(mv.m);
        if ((unsigned)mv.produced == before && !done) demux();   /* no whole picture yet */
    }
    return 1;
}

static int SDLCALL run(void* user)
{
    (void)user;
    crash_thread_init();
    while (!SDL_GetAtomicInt(&mv.quit))
        if (!step()) SDL_DelayNS(2000000);
    return 0;
}

int movie_open(FILE* f, uint32_t lsn, uint32_t size)
{
    PssSink sink = { on_video, on_audio, NULL };
    movie_close();
    if (!mv.m) mv.m = SDL_CreateMutex();
    SDL_LockMutex(mv.m);   /* the previous movie's frames (the render thread may be reading one) */
    for (int i = 0; i < RING; i++) { free(mv.slot[i]); mv.slot[i] = NULL; }
    mv.w = mv.h = mv.stride = mv.produced = mv.consumed = mv.eof = 0;
    mv.dec_ns = mv.dec_max_ns = 0; mv.late = 0;
    mv.paused = 1;   /* until the game plays it (movie_glue.c readMpeg) */
    SDL_UnlockMutex(mv.m);
    mv.f = f ? f : pc_fopen(g_config.iso_path, "rb");
    if (!mv.f) return 0;
    mv.lsn = lsn; mv.size = size;
    mv.pss = pss_create(&sink);
    mv.dec = mpeg2_create(on_frame, NULL);
    while (!mv.produced && step()) {}   /* the first frame (and the sound's format) before playing */
    if (!(mv.th = SDL_CreateThread(run, "movie", NULL))) {   /* no more frames: the movie ends after those decoded */
        host_log("movie: cannot create the decoding thread (%s)\n", SDL_GetError());
        SDL_LockMutex(mv.m); mv.eof = 1; SDL_UnlockMutex(mv.m);
    }
    return mv.produced > 0;
}

/* the decoded frames stay until the next movie: the frame drawn as the movie ends is
   executed by the render thread after this */
void movie_close(void)
{
    if (mv.th) { SDL_SetAtomicInt(&mv.quit, 1); SDL_WaitThread(mv.th, NULL); mv.th = NULL; }
    SDL_SetAtomicInt(&mv.quit, 0);
    if (mv.f) fclose(mv.f);
    if (mv.audio_open) audio_movie_close();
    mpeg2_destroy(mv.dec);
    pss_destroy(mv.pss);
    free(mv.es);
    if (mv.m) SDL_LockMutex(mv.m);
    mv.f = NULL; mv.dec = NULL; mv.pss = NULL;
    mv.es = NULL; mv.es_len = mv.es_cap = 0;
    mv.lsn = mv.size = mv.off = 0;
    mv.audio_rate = mv.audio_open = 0; mv.audio_put = 0; mv.sumsq = 0;
    if (mv.m) SDL_UnlockMutex(mv.m);
}

void movie_pause(int paused)
{
    if (!mv.m) return;
    SDL_LockMutex(mv.m);
    mv.paused = paused;
    if (mv.audio_open) audio_movie_pause(paused);
    SDL_UnlockMutex(mv.m);
}

int movie_frames_total(void)
{
    int n;
    if (!mv.m) return 0;
    SDL_LockMutex(mv.m);
    n = !mv.dec ? 0 : mv.eof ? mv.produced : -1;
    SDL_UnlockMutex(mv.m);
    return n;
}

double movie_clock(double* queued)
{
    double t = -1, q = 0;
    if (!mv.m) return -1;
    SDL_LockMutex(mv.m);
    if (mv.audio_open) {
        q = audio_movie_queued() / 4.0 / mv.audio_rate;
        t = (mv.audio_put - q * mv.audio_rate) / mv.audio_rate;
        if (q <= 0 && mv.off >= mv.size) t = -1;   /* the sound has ended */
    }
    SDL_UnlockMutex(mv.m);
    if (queued) *queued = q;
    return t;
}

void movie_size(int* w, int* h) { *w = mv.w; *h = mv.h; }

const uint8_t* movie_lock_frame(int index, uint32_t* serial, int* w, int* h, int* stride)
{
    int i;
    if (!mv.m) return NULL;
    SDL_LockMutex(mv.m);
    i = index < mv.produced ? index : mv.produced - 1;   /* not decoded yet: the newest */
    if (i < 0) { SDL_UnlockMutex(mv.m); return NULL; }
    if (i < index) mv.late++;
    if (i < mv.consumed) i = mv.consumed;   /* older slots may be refilled (frames only go forward) */
    if (i > mv.consumed) mv.consumed = i;
    *serial = mv.serial[i % RING];
    *w = mv.w; *h = mv.h; *stride = mv.stride;
    return mv.slot[i % RING];   /* unlocked by movie_unlock_frame */
}

void movie_unlock_frame(void) { SDL_UnlockMutex(mv.m); }

void movie_stats(int* frames, double* dec_avg_ms, double* dec_max_ms, int* late)
{
    SDL_LockMutex(mv.m);
    *frames = mv.produced;
    *dec_avg_ms = mv.produced ? mv.dec_ns / 1e6 / mv.produced : 0;
    *dec_max_ms = mv.dec_max_ns / 1e6;
    *late = mv.late;
    SDL_UnlockMutex(mv.m);
}

int movie_sound(int* rate, double* seconds, double* rms, int* paused)
{
    int open;
    *rate = *paused = 0; *seconds = *rms = 0;
    if (!mv.m) return 0;
    SDL_LockMutex(mv.m);
    open = mv.audio_open;
    *rate = mv.audio_rate;
    *seconds = mv.audio_rate ? (double)mv.audio_put / mv.audio_rate : 0;
    *rms = mv.audio_put ? SDL_sqrt(mv.sumsq / (mv.audio_put * 2.0)) : 0;
    *paused = mv.paused;
    SDL_UnlockMutex(mv.m);
    return open;
}
