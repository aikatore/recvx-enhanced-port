/* PSS movie playback (platform side); driven by pc/glue/movie_glue.c. */
#pragma once
#include <stdint.h>
/* game thread */
#include <stdio.h>
int    movie_open(FILE* f, uint32_t lsn, uint32_t size);   /* f (taken over) or, NULL, the disc image at sector lsn; 1 if at least one frame decodes */
void   movie_close(void);
void   movie_pause(int paused);
int    movie_frames_total(void);                  /* -1 while still decoding */
double movie_clock(double* queued);               /* seconds of sound played (and queued); < 0: no sound */
void   movie_size(int* w, int* h);
void   movie_stats(int* frames, double* dec_avg_ms, double* dec_max_ms, int* late);
/* the sound so far: 1 if its stream is open; its rate, seconds handed to the device, RMS at
   the stream's gain when handed over, and the pause state */
int    movie_sound(int* rate, double* seconds, double* rms, int* paused);
/* render thread: frame `index` (display order, 29.97 Hz; the newest decoded if it is not yet),
   YUV 4:2:0 planes (Y stride x h, then Cb and Cr at stride/2 x h/2); serial identifies the
   frame. NULL: none. A non-NULL result must be released with movie_unlock_frame. */
const uint8_t* movie_lock_frame(int index, uint32_t* serial, int* w, int* h, int* stride);
void   movie_unlock_frame(void);
