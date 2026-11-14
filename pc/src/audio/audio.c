/*
 * Audio output (SDL3). Each ADX stream is an SDL_AudioStream bound to the output
 * device; SDL mixes them. Streams decode on demand on SDL's audio thread, reading
 * the disc image through their own file handle.
 *
 * CRI ADX (type 3): 18-byte frames per channel (BE16 scale + 32 4-bit samples),
 * channels interleaved per frame, 2nd-order prediction with coefficients derived
 * from the header's high-pass cutoff. Versions 3/4 carry loop points.
 */
#include "../ps2_platform.h"
#include "audio.h"
#include "snd.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define MAX_VOICES 8
#define SECTOR 2048

typedef struct {
    int used;
    SDL_AudioStream* stream;
    FILE* f;
    uint64_t base;               /* byte offset of the ADX file in the image */
    uint32_t size;
    /* header */
    uint32_t channels, rate, total, data_ofs, frame_bytes;
    int loop;
    uint32_t loop_start_sample, loop_start_byte, loop_end_sample;
    int16_t coef1, coef2;
    /* decoder state */
    uint32_t pos_byte, decoded;  /* next frame offset (relative to file), samples decoded */
    int32_t hist1[8], hist2[8];
    int ended;
    int vol_cdb;
} AdxVoice;

static SDL_AudioDeviceID dev;
static AdxVoice voices[MAX_VOICES];
static int mono_out;
/* The game thread starts/stops voices and the movie stream; the main thread applies the player
   volumes (F1, settings_poll). This lock covers a stream's publication (used / movie_stream),
   its gain and its removal, so the main thread never sets the gain of a destroyed stream. */
static SDL_Mutex* lock;

static uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }

void audio_init(void)
{
    SDL_AudioSpec spec = { SDL_AUDIO_S16, 2, 48000 };
    lock = SDL_CreateMutex();   /* before the game thread starts */
    dev = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if (!dev) host_log("audio: cannot open output device: %s (continuing silently)\n", SDL_GetError());
    snd_synth_init(dev);
    audio_apply_volumes();
}

void audio_set_mono(int mono) { mono_out = mono; }

static float gain_of(int vol_cdb)
{
    return vol_cdb <= -999 ? 0.0f : powf(10.0f, (float)vol_cdb / 200.0f) * g_config.music_volume / 100.0f;
}

static SDL_AudioStream* movie_stream;
static float movie_gain = 1.0f;

int audio_movie_open(int rate)
{
    SDL_AudioSpec src = { SDL_AUDIO_S16, 2, rate }, dst = { SDL_AUDIO_S16, 2, 48000 };
    SDL_AudioStream* s;
    if (!dev) return 0;
    s = SDL_CreateAudioStream(&src, &dst);
    SDL_LockMutex(lock);
    movie_stream = s;
    if (s) SDL_SetAudioStreamGain(s, movie_gain * g_config.music_volume / 100.0f);
    SDL_UnlockMutex(lock);
    return s != NULL;
}
void audio_movie_put(const int16_t* s, size_t frames) { if (movie_stream) SDL_PutAudioStreamData(movie_stream, s, (int)(frames * 4)); }
int audio_movie_queued(void) { return movie_stream ? SDL_GetAudioStreamQueued(movie_stream) : 0; }
void audio_movie_pause(int paused)
{
    if (!movie_stream) return;
    if (paused) SDL_UnbindAudioStream(movie_stream); else SDL_BindAudioStream(dev, movie_stream);
}
void audio_movie_volume(float gain)
{
    SDL_LockMutex(lock);
    movie_gain = gain;
    if (movie_stream) SDL_SetAudioStreamGain(movie_stream, movie_gain * g_config.music_volume / 100.0f);
    SDL_UnlockMutex(lock);
}
float audio_movie_gain(void) { return movie_gain * g_config.music_volume / 100.0f; }
void audio_movie_close(void)
{
    SDL_AudioStream* s;
    SDL_LockMutex(lock);
    s = movie_stream;
    movie_stream = NULL;
    SDL_UnlockMutex(lock);
    if (!s) return;
    SDL_UnbindAudioStream(s);
    SDL_DestroyAudioStream(s);
}

/* main thread (F1, settings_poll) */
void audio_apply_volumes(void)
{
    SDL_LockMutex(lock);
    if (movie_stream) SDL_SetAudioStreamGain(movie_stream, movie_gain * g_config.music_volume / 100.0f);
    for (int i = 0; i < MAX_VOICES; i++) if (voices[i].used) SDL_SetAudioStreamGain(voices[i].stream, gain_of(voices[i].vol_cdb));
    SDL_UnlockMutex(lock);
    snd_synth_set_volume(g_config.sfx_volume / 100.0f);
}

/* decode one frame per channel (32 samples) into out (interleaved s16) */
static int decode_frames(AdxVoice* v, int16_t* out)
{
    uint8_t buf[8 * 18];
    uint32_t n = v->channels * v->frame_bytes;
    if (v->loop && v->decoded >= v->loop_end_sample) { v->pos_byte = v->loop_start_byte; v->decoded = v->loop_start_sample; }
    if (!v->loop && v->decoded >= v->total) return 0;
    if (v->pos_byte + n > v->size) return 0;
    if (fseeko(v->f, (off_t)(v->base + v->pos_byte), SEEK_SET) != 0 || fread(buf, 1, n, v->f) != n) return 0;
    v->pos_byte += n;
    for (uint32_t ch = 0; ch < v->channels; ch++) {
        const uint8_t* fr = buf + ch * v->frame_bytes;
        int32_t scale = (int16_t)be16(fr) + 1;
        for (int i = 0; i < 32; i++) {
            int nib = (fr[2 + i / 2] >> ((i & 1) ? 0 : 4)) & 15;
            int32_t s;
            if (nib & 8) nib -= 16;
            s = nib * scale + ((v->coef1 * v->hist1[ch]) >> 12) + ((v->coef2 * v->hist2[ch]) >> 12);
            if (s > 32767) s = 32767; else if (s < -32768) s = -32768;
            v->hist2[ch] = v->hist1[ch]; v->hist1[ch] = s;
            out[i * 2 + 0 + (v->channels > 1 ? ch : 0)] = (int16_t)s;
            if (v->channels == 1) out[i * 2 + 1] = (int16_t)s;
        }
    }
    {   /* clip the last frame at the end of the file */
        int samples = 32;
        if (!v->loop && v->decoded + 32 > v->total) samples = (int)(v->total - v->decoded);
        if (v->loop && v->decoded + 32 > v->loop_end_sample) samples = (int)(v->loop_end_sample - v->decoded);
        v->decoded += (uint32_t)samples;
        if (mono_out) for (int i = 0; i < samples; i++) { int16_t m = (int16_t)((out[i * 2] + out[i * 2 + 1]) / 2); out[i * 2] = out[i * 2 + 1] = m; }
        return samples;
    }
}

static void SDLCALL feed(void* userdata, SDL_AudioStream* stream, int additional, int total)
{
    AdxVoice* v = userdata;
    int16_t pcm[64 * 32 * 2];
    (void)total;
    while (additional > 0 && !v->ended) {
        int got = 0;
        while (got < 64 * 32 - 32) {
            int n = decode_frames(v, pcm + got * 2);
            if (n <= 0) { v->ended = 1; break; }
            got += n;
        }
        if (got) SDL_PutAudioStreamData(stream, pcm, got * 4);
        additional -= got * 4;
    }
    if (v->ended) SDL_FlushAudioStream(stream);
}

/* v reads the ADX file of `size` bytes at byte `base` of the open file f: its header parsed, at its first frame; 0 not one
   decode_frames reads */
static int adx_open(AdxVoice* v, FILE* f, uint64_t base, uint32_t size)
{
    uint8_t hdr[SECTOR] = { 0 };
    memset(v, 0, sizeof(*v));
    v->f = f; v->base = base; v->size = size;
    if (fseeko(v->f, (off_t)v->base, SEEK_SET) != 0 || fread(hdr, 1, SECTOR, v->f) < 0x34 || hdr[0] != 0x80) return 0;
    v->data_ofs = be16(hdr + 2) + 4;
    v->frame_bytes = hdr[5] ? hdr[5] : 18;
    v->channels = hdr[7] ? hdr[7] : 1;
    v->rate = be32(hdr + 8);
    v->total = be32(hdr + 12);
    {
        double cutoff = be16(hdr + 16), a, b, c;
        a = M_SQRT2 - cos(2.0 * M_PI * cutoff / v->rate);
        b = M_SQRT2 - 1.0;
        c = (a - sqrt((a + b) * (a - b))) / b;
        v->coef1 = (int16_t)(c * 8192.0);
        v->coef2 = (int16_t)(c * c * -4096.0);
    }
    if (hdr[0x12] == 3 && be32(hdr + 0x18)) {
        v->loop = 1; v->loop_start_sample = be32(hdr + 0x1C); v->loop_start_byte = be32(hdr + 0x20); v->loop_end_sample = be32(hdr + 0x24);
    } else if (hdr[0x12] == 4 && be32(hdr + 0x24)) {
        v->loop = 1; v->loop_start_sample = be32(hdr + 0x28); v->loop_start_byte = be32(hdr + 0x2C); v->loop_end_sample = be32(hdr + 0x30);
    }
    if (v->channels > 2 || v->frame_bytes != 18) {   /* decode_frames: 2 channels of 18-byte frames */
        host_log("adx: %u channels of %u-byte frames not supported\n", v->channels, v->frame_bytes);
        return 0;
    }
    v->pos_byte = v->data_ofs;
    return 1;
}

/* how loud the ADX file (as audio_stream_start_adx takes it) is in each 1/30 s from its start, up to max of them: the mean
   |sample| of its first channel, 0..255 for 0..1/4 of full scale (speech in the voice files: about 40..110, a pause under 12);
   the count, -1 unreadable (the voice lines' mouths: cast_glue.c line_lips) */
int audio_adx_levels(const char* path, uint64_t base, uint32_t size, unsigned char* out, int max)
{
    AdxVoice v;
    int16_t pcm[64];
    int n = 0, got, k, per;
    uint64_t sum = 0, cnt = 0;
    FILE* f = pc_fopen(path, "rb");
    if (!f) return -1;
    if (!adx_open(&v, f, base, size) || v.loop || v.rate < 30) { fclose(f); return -1; }
    per = (int)(v.rate / 30);
    while (n < max && (got = decode_frames(&v, pcm)) > 0)
        for (k = 0; k < got; k++) {
            sum += (uint64_t)(pcm[k * 2] < 0 ? -pcm[k * 2] : pcm[k * 2]);
            if (++cnt == (uint64_t)per || (k == got - 1 && v.decoded >= v.total)) {
                uint64_t l = sum * 4 * 255 / cnt / 32768;
                if (n < max) out[n++] = (unsigned char)(l > 255 ? 255 : l);
                sum = cnt = 0;
            }
        }
    fclose(f);
    return n;
}

int audio_stream_start_adx(const char* path, uint64_t base, uint32_t size, int vol_cdb)
{
    AdxVoice* v = NULL;
    FILE* f;
    int id = -1;
    for (int i = 0; i < MAX_VOICES; i++) if (!voices[i].used) { v = &voices[i]; id = i; break; }
    if (!v || !dev) return -1;
    if (!(f = pc_fopen(path, "rb"))) return -1;
    if (!adx_open(v, f, base, size)) { fclose(f); return -1; }
    {
        SDL_AudioSpec src = { SDL_AUDIO_S16, 2, (int)v->rate };
        SDL_AudioSpec dst = { SDL_AUDIO_S16, 2, 48000 };
        v->stream = SDL_CreateAudioStream(&src, &dst);
    }
    if (!v->stream) { fclose(v->f); return -1; }
    v->vol_cdb = vol_cdb;
    SDL_SetAudioStreamGetCallback(v->stream, feed, v);
    SDL_LockMutex(lock);
    SDL_SetAudioStreamGain(v->stream, gain_of(vol_cdb));
    v->used = 1;
    SDL_UnlockMutex(lock);
    SDL_BindAudioStream(dev, v->stream);
    return id;
}

void audio_stream_stop(int id)
{
    AdxVoice* v = &voices[id];
    if (id < 0 || id >= MAX_VOICES || !v->used) return;
    SDL_LockMutex(lock);
    v->used = 0;   /* unpublished: the main thread no longer touches it */
    SDL_UnlockMutex(lock);
    SDL_UnbindAudioStream(v->stream);
    SDL_DestroyAudioStream(v->stream);
    fclose(v->f);
}

void audio_stream_pause(int id, int paused) { (void)id; (void)paused; /* ADXT_Pause is empty on the retail build */ }

void audio_stream_volume(int id, int vol_cdb)
{
    if (id < 0 || id >= MAX_VOICES || !voices[id].used) return;
    SDL_LockMutex(lock);
    voices[id].vol_cdb = vol_cdb;
    SDL_SetAudioStreamGain(voices[id].stream, gain_of(vol_cdb));
    SDL_UnlockMutex(lock);
}

/* samples actually played = decoded - still queued (converted back to the source rate) */
uint64_t audio_stream_position(int id)
{
    AdxVoice* v = &voices[id];
    int queued;
    uint64_t pos;
    if (id < 0 || id >= MAX_VOICES || !v->used) return 0;
    SDL_LockAudioStream(v->stream);
    queued = SDL_GetAudioStreamQueued(v->stream) / 4;
    pos = v->decoded > (uint32_t)queued ? v->decoded - (uint32_t)queued : 0;
    SDL_UnlockAudioStream(v->stream);
    return pos;
}

int audio_stream_finished(int id)
{
    AdxVoice* v = &voices[id];
    int done;
    if (id < 0 || id >= MAX_VOICES || !v->used) return 1;
    SDL_LockAudioStream(v->stream);
    done = v->ended && SDL_GetAudioStreamAvailable(v->stream) == 0;
    SDL_UnlockAudioStream(v->stream);
    return done;
}
