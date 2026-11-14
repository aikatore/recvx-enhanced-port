/* Audio output (SDL3): ADX stream voices + sound-effect driver mixing. */
#pragma once
#include <stddef.h>
#include <stdint.h>

void     audio_init(void);
void     audio_set_mono(int mono);
void     audio_apply_volumes(void);   /* g_config music/sfx volume */
/* movie soundtrack (interleaved stereo s16) */
int      audio_movie_open(int rate);         /* 0: no output */
void     audio_movie_put(const int16_t* stereo, size_t frames);
void     audio_movie_pause(int paused);
int      audio_movie_queued(void);         /* bytes not yet played (input format) */
void     audio_movie_volume(float gain);   /* the game's input volume, 0..1 */
float    audio_movie_gain(void);           /* the movie stream's gain: input volume x music volume */
void     audio_movie_close(void);
/* an ADX stream of `size` bytes at byte `base` of a file (the disc image, a mods file); a voice id or -1 */
int      audio_stream_start_adx(const char* path, uint64_t base, uint32_t size, int vol_cdb);
int      audio_adx_levels(const char* path, uint64_t base, uint32_t size, unsigned char* out, int max);   /* loudness per 1/30 s */
void     audio_stream_stop(int voice);
void     audio_stream_pause(int voice, int paused);
void     audio_stream_volume(int voice, int vol_cdb);
uint64_t audio_stream_position(int voice);
int      audio_stream_finished(int voice);
int      adx_afs_locate(int ptid, int fid, uint32_t* lsn, uint32_t* size);
