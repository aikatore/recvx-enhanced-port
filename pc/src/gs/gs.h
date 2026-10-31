/*
 * GS (Graphics Synthesizer) emulation for the PC port.
 *
 * The original platform code drives the GS through GIF packets (PATH1 from the
 * VU1 microcode, PATH2 via VIF DIRECT, PATH3 via GIF DMA). We keep that code
 * and emulate the GS here: local memory (textures/CLUTs/transfers) is modeled
 * exactly; rasterization is done by OpenGL at any resolution (gs_gl.c).
 */
#pragma once
#include <stdint.h>

#define GS_MEM_SIZE (4 * 1024 * 1024)

enum { PSMCT32 = 0x00, PSMCT24 = 0x01, PSMCT16 = 0x02, PSMCT16S = 0x0a, PSMT8 = 0x13, PSMT4 = 0x14,
       PSMT8H = 0x1b, PSMT4HL = 0x24, PSMT4HH = 0x2c, PSMZ32 = 0x30, PSMZ24 = 0x31, PSMZ16 = 0x32, PSMZ16S = 0x3a };

typedef struct {
    uint64_t tex0, tex1, tex2, clamp, xyoffset, miptbp1, miptbp2, scissor, alpha, test, fba, frame, zbuf;
} GsContext;

typedef struct {
    float    x, y, z;     /* framebuffer pixel coordinates (after XYOFFSET), z as GS integer depth */
    float    s, t, q;     /* STQ (perspective texture coords) */
    float    u, v;        /* UV texel coords (FST=1) */
    uint8_t  r, g, b, a;
    uint8_t  f;           /* fog coefficient */
} GsVertex;

typedef struct {
    uint64_t prim, prmode, prmodecont;
    GsContext ctx[2];
    uint64_t texclut, scanmsk, texa, fogcol, dimx, dthe, colclamp, pabe, bitbltbuf, trxpos, trxreg, trxdir;
    /* vertex state */
    uint64_t rgbaq;
    uint64_t st, uv, fog;
    GsVertex queue[3]; int qcount;
    /* host->local transfer in progress */
    int      xfer_active; uint32_t xfer_x, xfer_y; uint32_t xfer_pending_bits;
    /* display (from libgraph's DISPFB/DISPLAY setup) */
    uint32_t disp_fbp, disp_fbw, disp_psm, disp_dw, disp_dh;
} GsState;

extern GsState gs;

/* frame interpolation (interp.c) */
#define GS_CLUT_STATE_SIZE (512 * 2 + 8)
void gs_clut_state_save(uint8_t* out);
void gs_clut_state_restore(const uint8_t* in);
extern int gs_replaying;
/* index (+1) of the frame's last event that can change what a draw's texture or render
   target resolves to (texture registers, uploads, native texture binds); set by the stream */
extern uint32_t gs_tex_epoch;
extern uint32_t gs_reg_serial;   /* changes with every register write and state reload (gs_gl.c batching) */
void gs_tex_delete(unsigned int gl_tex);   /* deferred until the next frame (its replays may bind it) */
void gs_replay_begin(void);
extern uint8_t gs_mem[GS_MEM_SIZE];
/* native textures: decode in a private copy of GS memory, leaving gs_mem and the CLUT
   buffer as they were */
extern uint8_t* gs_memp;   /* gs_mem, or the scratch copy during gs_mem_scratch_begin/end */
extern int gs_flush_blocked;   /* while set, gs_flush does nothing (GS writes replayed inside a flush) */
void gs_mem_scratch_begin(void);
void gs_mem_scratch_end(void);

/* gs.c */
void gs_reset(void);
void gs_write_reg(int reg, uint64_t value);        /* game thread: recorded */
void gs_upload_image(const uint8_t* src, uint32_t bytes);
void gs_exec_reg(int reg, uint64_t value);         /* render thread: executed */
void gs_exec_upload(const uint8_t* src, uint32_t bytes);
void gs_exec_download(uint8_t* dst, uint32_t bytes);
void gs_gif_transfer(int path, const uint32_t* data, uint32_t qwc);

/* gs_mem.c: addressing in 32-bit words / bytes */
uint32_t gs_pixel_addr(int psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y); /* returns byte address (x2 for 4-bit: nibble address) */
uint32_t gs_read_pixel(int psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y);
void     gs_write_pixel(int psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, uint32_t value);
void     gs_local_to_local(void);
void     gs_rt_to_mem(uint32_t bp, uint32_t bw, uint32_t psm, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
unsigned int gs_capture_rt(uint32_t fbp, uint32_t fbw, uint32_t w, uint32_t h, int wide);   /* GL texture of the RT area (0,0)-(w,h); wide: widened by hor+ about x w/2 */
void gs_replay_capture(unsigned int exec_tex, uint32_t fbp, uint32_t fbw, uint32_t w, uint32_t h, int wide);   /* in a replay: as gs_capture_rt, bound in place of exec_tex until the replay ends */
/* textures at TBP tbp are movie frame index (pc/src/movie) until the next executed frame */
void     gs_movie_bind(uint32_t tbp, int index);
/* decode a texture (TEX0 + TEXA) to RGBA8, w x h */
void     gs_clut_load(uint64_t tex0);
void     gs_decode_texture(uint64_t tex0, uint64_t texa, uint32_t w, uint32_t h, uint32_t* out);
void     gs_get_palette(uint64_t tex0, uint64_t texa, uint32_t* out, int n);
int      gs_mem_selftest(void);

/* gs_gl.c: rasterization back end */
void gs_draw_primitive(int prim_type, const GsVertex* v, int count, const GsContext* c, uint64_t prim);
void gs_notify_mem_write(uint32_t bp, uint32_t bw, int psm, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
/* exact set of written pages (bit per 8 KB page); changed = 0 if the data was identical */
void gs_notify_pages(const uint64_t* pages, int changed);
void gs_flush(void);
int  gs_batch_two_pass(void);   /* the pending batch draws in two passes */
struct Frame;
void gs_frame_begin(void);
void gs_frame_executed(int interpolate);
uint64_t gs_present(const struct Frame* cur, float t);   /* returns the swap's blocking time (ns) */
int gs_gpu_ready(uint64_t wait_ns);   /* at most the last executed frame or image still on the GPU (waits up to wait_ns) */
void gs_replay_check(const struct Frame* cur);   /* RECVX_REPLAY_CHECK */
