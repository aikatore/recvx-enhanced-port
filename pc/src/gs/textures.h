/* Native textures (textures.c): TIM2 definitions recorded once, bound to GS slots. */
#pragma once
#include <stdint.h>

/* Definition ids (pc/glue/texture_glue.c): a slot in the low NTEX_ID_BITS bits, a generation
   above. The game thread has at most NTEX_ID_SLOTS ids alive and gives a slot a new generation
   only after forgetting the previous id (the stream keeps that order), so the render thread's
   table is bounded; a stale id (a replayed frame's) no longer matches its slot. */
#define NTEX_ID_BITS 14
#define NTEX_ID_SLOTS (1u << NTEX_ID_BITS)

/* render thread, from the stream */
/* tags of a definition (game thread, texture_glue.c), for HD texture packs (hdpack.c) */
#define NTEX_FONT 1   /* one of the game's font pages */
#define NTEX_KEEP 2   /* the port cuts its own menu entries from it */
void ntex_define_begin(uint32_t id, uint32_t tbp, uint32_t cbp, uint32_t image_pages, int flags);
int  ntex_defining(void);
void ntex_define_reg(int reg, uint64_t v);
void ntex_define_image(const uint8_t* data, uint32_t len);
void ntex_define_end(void);
void ntex_bind(uint32_t id, uint32_t tbp, uint32_t cbp);
void ntex_note_upload(const uint64_t* pages);   /* GS memory uploads: unbind overlapped slots */
/* GS memory pages page0..page1 are about to be read or written other than through
   ntex_lookup: bound slots there are written into GS memory first */
void ntex_before_access(uint32_t page0, uint32_t page1);
void ntex_forget(uint32_t id);   /* the game thread dropped a definition */
/* definition id is a texture captured from a render target (gs_capture_rt, RGBA16F as the
   render targets); replaces an earlier capture of the same id */
void ntex_define_capture(uint32_t id, unsigned int gl_tex, int w, int h);
unsigned int ntex_capture_tex(uint32_t id);   /* the GL texture of capture id (0: none) */
/* the slot bindings (replays of a frame start from its starting state) */
#define NTEX_STATE_SIZE 12288
void ntex_state_save(void* out);
void ntex_state_restore(const void* in);
/* a texture for TEX0 if its TBP is a bound slot (1; 2: a capture, alpha stored as A/128), else 0 */
int  ntex_lookup(uint64_t tex0, uint64_t texa, unsigned int* tex, int* w, int* h);
