/*
 * GIF packet parser and GS register file / vertex queue.
 */
#include "../host/prof.h"
#include "gs.h"
#include "stream.h"
#include "native.h"
#include "textures.h"
#include "../ps2_platform.h"
#include <string.h>
#include <stdio.h>

GsState gs;
uint32_t gs_reg_serial;

void gs_reset(void)
{
    memset(&gs, 0, sizeof(gs));
    gs_reg_serial++;
    gs.prmodecont = 1;
}

static float u2f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

/* ---------------- vertex kick / primitive assembly ---------------- */

static const GsContext* cur_ctx(void)
{
    uint64_t p = gs.prmodecont & 1 ? gs.prim : gs.prmode;
    return &gs.ctx[(p >> 9) & 1];
}

static uint64_t cur_prim_attrs(void)
{
    /* PRMODECONT.AC=0: attributes come from PRMODE, primitive type from PRIM */
    return (gs.prmodecont & 1) ? gs.prim : ((gs.prmode & ~7ull) | (gs.prim & 7));
}

static void make_vertex(GsVertex* v, uint32_t x, uint32_t y, uint32_t z, uint32_t f)
{
    const GsContext* c = cur_ctx();
    uint32_t ofx = c->xyoffset & 0xFFFF, ofy = (c->xyoffset >> 32) & 0xFFFF;
    v->x = ((float)x - (float)ofx) / 16.0f;
    v->y = ((float)y - (float)ofy) / 16.0f;
    v->z = (float)z;
    v->r = gs.rgbaq & 0xFF; v->g = (gs.rgbaq >> 8) & 0xFF; v->b = (gs.rgbaq >> 16) & 0xFF; v->a = (gs.rgbaq >> 24) & 0xFF;
    v->q = u2f((uint32_t)(gs.rgbaq >> 32));
    v->s = u2f((uint32_t)gs.st); v->t = u2f((uint32_t)(gs.st >> 32));
    v->u = (float)(gs.uv & 0x3FFF) / 16.0f; v->v = (float)((gs.uv >> 16) & 0x3FFF) / 16.0f;
    v->f = (uint8_t)f;
}

/* native raw primitives (native.h): the GS vertex kick for given register values */
void gs_draw_raw(unsigned int prim, const NativeRawVertex* in, int n)
{
    const GsContext* c = &gs.ctx[(prim >> 9) & 1];
    prim &= 0x7FF;   /* (NATIVE_RAW_3D) */
    uint32_t ofx = c->xyoffset & 0xFFFF, ofy = (c->xyoffset >> 32) & 0xFFFF;
    int type = (int)(prim & 7), q = 0;
    GsVertex qv[3];
    for (int i = 0; i < n; i++) {
        const NativeRawVertex* p = &in[i];
        GsVertex v;
        v.x = ((float)p->x - (float)ofx) / 16.0f; v.y = ((float)p->y - (float)ofy) / 16.0f; v.z = (float)p->z;
        v.r = p->r; v.g = p->g; v.b = p->b; v.a = p->a;
        v.s = p->s; v.t = p->t; v.q = p->q;
        v.u = (float)p->u / 16.0f; v.v = (float)p->v / 16.0f;
        v.f = p->f;
        switch (type) {
        case 0: if (!p->adc) gs_draw_primitive(0, &v, 1, c, prim); break;
        case 1: case 6:
            qv[q++] = v;
            if (q == 2) { if (!p->adc) gs_draw_primitive(type, qv, 2, c, prim); q = 0; }
            break;
        case 2:
            qv[q++] = v;
            if (q == 2) { if (!p->adc) gs_draw_primitive(1, qv, 2, c, prim); qv[0] = qv[1]; q = 1; }
            break;
        default:   /* triangles take the native triangle path */
            break;
        }
    }
}

static void vertex_kick(uint32_t x, uint32_t y, uint32_t z, uint32_t f, int draw)
{
    int type = gs.prim & 7;
    uint64_t attrs = cur_prim_attrs();
    GsVertex v;
    make_vertex(&v, x, y, z, f);

    switch (type) {
    case 0: /* point */
        if (draw) gs_draw_primitive(0, &v, 1, cur_ctx(), attrs);
        break;
    case 1: /* line */
    case 6: /* sprite */
        gs.queue[gs.qcount++] = v;
        if (gs.qcount == 2) {
            if (draw) gs_draw_primitive(type, gs.queue, 2, cur_ctx(), attrs);
            gs.qcount = 0;
        }
        break;
    case 2: /* line strip */
        gs.queue[gs.qcount++] = v;
        if (gs.qcount == 2) {
            if (draw) gs_draw_primitive(1, gs.queue, 2, cur_ctx(), attrs);
            gs.queue[0] = gs.queue[1]; gs.qcount = 1;
        }
        break;
    case 3: /* triangle */
        gs.queue[gs.qcount++] = v;
        if (gs.qcount == 3) {
            if (draw) gs_draw_primitive(3, gs.queue, 3, cur_ctx(), attrs);
            gs.qcount = 0;
        }
        break;
    case 4: /* triangle strip */
        gs.queue[gs.qcount++] = v;
        if (gs.qcount == 3) {
            if (draw) gs_draw_primitive(3, gs.queue, 3, cur_ctx(), attrs);
            gs.queue[0] = gs.queue[1]; gs.queue[1] = gs.queue[2]; gs.qcount = 2;
        }
        break;
    case 5: /* triangle fan */
        gs.queue[gs.qcount++] = v;
        if (gs.qcount == 3) {
            if (draw) gs_draw_primitive(3, gs.queue, 3, cur_ctx(), attrs);
            gs.queue[1] = gs.queue[2]; gs.qcount = 2;
        }
        break;
    default:
        gs.qcount = 0;
        break;
    }
}

/* ---------------- register writes ---------------- */

/* game thread: recorded for the render thread */
void gs_write_reg(int reg, uint64_t v)
{
    if (reg == NATIVE_MARKER_REG) { native_marker(v); return; }   /* native 3D in GS packet order */
    if (reg == STREAM_TAG_REG) { stream_tag_last((uint32_t)v, (uint32_t)(v >> 32)); return; }
    stream_reg(reg, v);
}
void gs_upload_image(const uint8_t* src, uint32_t bytes) { stream_image(src, bytes); }

/* A write of a draw-state register ends the batch, unless it writes the value the register
   has (the game rewrites the same TEST, ALPHA, TEX1, ... before most primitives): the batch
   goes on, the same primitives in the same order (a two-pass alpha-test batch in groups whose
   passes do not interleave: gs_batch_rewrite). */
static void flush_for(uint64_t old, uint64_t v) { if (old != v) gs_flush(); else gs_batch_rewrite(); }

/* render thread: the GS register file */
void gs_exec_reg(int reg, uint64_t v)
{
    gs_reg_serial++;
    switch (reg) {
    case 0x00: gs.prim = v & 0x7FF; gs.qcount = 0; break;
    case 0x01: gs.rgbaq = v; break;
    case 0x02: gs.st = v; break;
    case 0x03: gs.uv = v; break;
    case 0x04: vertex_kick(v & 0xFFFF, (v >> 16) & 0xFFFF, (v >> 32) & 0xFFFFFF, (v >> 56) & 0xFF, 1); break;
    case 0x05: vertex_kick(v & 0xFFFF, (v >> 16) & 0xFFFF, (uint32_t)(v >> 32), (gs.fog >> 56) & 0xFF, 1); break;
    case 0x0c: vertex_kick(v & 0xFFFF, (v >> 16) & 0xFFFF, (v >> 32) & 0xFFFFFF, (v >> 56) & 0xFF, 0); break;
    case 0x0d: vertex_kick(v & 0xFFFF, (v >> 16) & 0xFFFF, (uint32_t)(v >> 32), (gs.fog >> 56) & 0xFF, 0); break;
    case 0x06: case 0x07:
        flush_for(gs.ctx[reg - 6].tex0, v);   /* a CLUT load flushes itself */
        gs.ctx[reg - 6].tex0 = v;
        if (!gs_replaying) gs_clut_load(v);   /* a replay's textures are the executed frame's (no CLUT reads) */
        break;
    case 0x08: case 0x09: flush_for(gs.ctx[reg - 8].clamp, v); gs.ctx[reg - 8].clamp = v; break;
    case 0x0a: gs.fog = v; break;
    case 0x14: case 0x15: flush_for(gs.ctx[reg - 0x14].tex1, v); gs.ctx[reg - 0x14].tex1 = v; break;
    case 0x16: case 0x17: {
        /* TEX2: updates PSM and CLUT fields of TEX0 */
        GsContext* c = &gs.ctx[reg - 0x16];
        const uint64_t mask = (0x3Full << 20) | (0x3FFFull << 37) | (0xFull << 51) | (1ull << 55) | (0x1Full << 56) | (7ull << 61);
        gs_flush();
        c->tex0 = (c->tex0 & ~mask) | (v & mask);
        if (!gs_replaying) gs_clut_load(c->tex0);
        break;
    }
    case 0x18: case 0x19: gs.ctx[reg - 0x18].xyoffset = v; break;
    case 0x1a: gs.prmodecont = v & 1; break;
    case 0x1b: gs.prmode = v & 0x7F8; break;
    case 0x1c: gs.texclut = v; break;
    case 0x22: gs.scanmsk = v; break;
    case 0x34: case 0x35: gs.ctx[reg - 0x34].miptbp1 = v; break;
    case 0x36: case 0x37: gs.ctx[reg - 0x36].miptbp2 = v; break;
    case 0x3b: flush_for(gs.texa, v); gs.texa = v; break;
    case 0x3d: flush_for(gs.fogcol, v); gs.fogcol = v; break;
    case 0x3f: break; /* TEXFLUSH */
    case 0x40: case 0x41: flush_for(gs.ctx[reg - 0x40].scissor, v); gs.ctx[reg - 0x40].scissor = v; break;
    case 0x42: case 0x43: flush_for(gs.ctx[reg - 0x42].alpha, v); gs.ctx[reg - 0x42].alpha = v; break;
    case 0x44: gs.dimx = v; break;
    case 0x45: gs.dthe = v; break;
    case 0x46: flush_for(gs.colclamp, v); gs.colclamp = v; break;
    case 0x47: case 0x48: flush_for(gs.ctx[reg - 0x47].test, v); gs.ctx[reg - 0x47].test = v; break;
    case 0x49: flush_for(gs.pabe, v); gs.pabe = v; break;
    case 0x4a: case 0x4b: flush_for(gs.ctx[reg - 0x4a].fba, v); gs.ctx[reg - 0x4a].fba = v; break;
    case 0x4c: case 0x4d: flush_for(gs.ctx[reg - 0x4c].frame, v); gs.ctx[reg - 0x4c].frame = v; break;
    case 0x4e: case 0x4f: flush_for(gs.ctx[reg - 0x4e].zbuf, v); gs.ctx[reg - 0x4e].zbuf = v; break;
    case 0x50: gs.bitbltbuf = v; break;
    case 0x51: gs.trxpos = v; break;
    case 0x52: gs.trxreg = v; break;
    case 0x53:
        gs.trxdir = v & 3;
        gs.xfer_x = gs.xfer_y = 0; gs.xfer_pending_bits = 0;
        if (gs.trxdir == 0) {
            uint32_t dbp = (gs.bitbltbuf >> 32) & 0x3FFF, dbw = (gs.bitbltbuf >> 48) & 0x3F, dpsm = (gs.bitbltbuf >> 56) & 0x3F;
            uint32_t x1 = ((gs.trxpos >> 32) & 0x7FF) + (gs.trxreg & 0xFFF), y1 = ((gs.trxpos >> 48) & 0x7FF) + ((gs.trxreg >> 32) & 0xFFF);
            uint32_t last = x1 && y1 ? gs_pixel_addr((int)dpsm, dbp, dbw ? dbw : 1, x1 - 1, y1 - 1) : 0;
            ntex_before_access(dbp / 32, (dpsm == PSMT4 ? last >> 1 : last) / 8192);   /* a partly overwritten slot keeps the rest */
            gs_flush(); gs.xfer_active = 1;
        }
        else if (gs.trxdir == 2) { ntex_before_access(0, 511); gs_local_to_local(); }
        else gs.xfer_active = 0;
        break;
    case 0x54: /* HWREG: image data via A+D */
        gs_exec_upload((const uint8_t*)&v, 8);
        break;
    case 0x60: case 0x61: case 0x62: break; /* SIGNAL/FINISH/LABEL */
    default:
        break;
    }
}

/* ---------------- GIF ---------------- */

static float gif_q = 1.0f;   /* Q of the last packed ST (parser state, game thread) */

static void packed_write(int reg, const uint32_t* q)
{
    uint64_t lo = q[0] | ((uint64_t)q[1] << 32), hi = q[2] | ((uint64_t)q[3] << 32);
    switch (reg) {
    case 0x0: gs_write_reg(0x00, lo); break;
    /* packed forms are converted to the A+D register values, so every write funnels
       through gs_write_reg (frame interpolation records there) */
    case 0x1: { /* RGBAQ: Q comes from the last packed ST */
        uint64_t v = (q[0] & 0xFF) | ((q[1] & 0xFF) << 8) | ((q[2] & 0xFF) << 16) | ((uint64_t)(q[3] & 0xFF) << 24);
        uint32_t qb; memcpy(&qb, &gif_q, 4);
        gs_write_reg(0x01, v | ((uint64_t)qb << 32));
        break;
    }
    case 0x2: memcpy(&gif_q, &q[2], 4); gs_write_reg(0x02, lo); break;
    case 0x3: gs_write_reg(0x03, (q[0] & 0x3FFF) | ((uint64_t)(q[1] & 0x3FFF) << 16)); break;
    case 0x4: { /* XYZF2 / XYZF3 by ADC */
        uint64_t x = q[0] & 0xFFFF, y = q[1] & 0xFFFF, z = (q[2] >> 4) & 0xFFFFFF, f = (q[3] >> 4) & 0xFF;
        gs_write_reg(((q[3] >> 15) & 1) ? 0x0c : 0x04, x | (y << 16) | (z << 32) | (f << 56));
        break;
    }
    case 0x5: { /* XYZ2 / XYZ3 by ADC */
        uint64_t x = q[0] & 0xFFFF, y = q[1] & 0xFFFF, z = q[2];
        gs_write_reg(((q[3] >> 15) & 1) ? 0x0d : 0x05, x | (y << 16) | (z << 32));
        break;
    }
    case 0xa: gs_write_reg(0x0a, (uint64_t)((q[3] >> 4) & 0xFF) << 56); break;
    case 0xe: gs_write_reg((int)(hi & 0xFF), lo); break;
    case 0xf: break;
    default: gs_write_reg(reg, lo); break; /* TEX0/CLAMP/XYZF3/XYZ3 etc. take the low 64 bits */
    }
    (void)hi;
}

static void gif_transfer(int path, const uint32_t* data, uint32_t qwc);

/* GIF is a stream: a tag's data may continue in later DMA transfers. State per path. */
typedef struct {
    int      active;       /* inside a tag's data */
    uint32_t nloop_left;   /* loops (PACKED/REGLIST) or qwords (IMAGE) remaining */
    uint32_t reg;          /* next register index within the loop */
    uint32_t nreg, flg;
    uint64_t regs;
    int      half;         /* REGLIST: 0 = low 64 bits of the qword next, 1 = high */
} GifPath;
static GifPath gif_path[4];

void gs_gif_transfer(int path, const uint32_t* data, uint32_t qwc)
{
    prof_push(PROF_GIF);
    gif_transfer(path, data, qwc);
    prof_pop();
}

static void gif_transfer(int path, const uint32_t* data, uint32_t qwc)
{
    GifPath* g = &gif_path[path & 3];
    const uint32_t* end = data + qwc * 4;
    while (data < end) {
        if (!g->active) {
            uint64_t tlo = data[0] | ((uint64_t)data[1] << 32);
            g->regs = data[2] | ((uint64_t)data[3] << 32);
            g->nloop_left = tlo & 0x7FFF;
            g->flg = (tlo >> 58) & 3;
            g->nreg = (tlo >> 60) & 0xF;
            if (g->nreg == 0) g->nreg = 16;
            g->reg = 0; g->half = 0;
            data += 4;
            if ((tlo >> 46) & 1) gs_write_reg(0x00, (tlo >> 47) & 0x7FF); /* PRE */
            g->active = g->nloop_left != 0;
            continue;
        }
        if (g->flg == 0) { /* PACKED: one qword per register */
            packed_write((int)((g->regs >> (g->reg * 4)) & 0xF), data);
            data += 4;
            if (++g->reg == g->nreg) { g->reg = 0; if (--g->nloop_left == 0) g->active = 0; }
        } else if (g->flg == 1) { /* REGLIST: two 64-bit registers per qword */
            int reg = (int)((g->regs >> (g->reg * 4)) & 0xF);
            const uint32_t* p = data + g->half * 2;
            uint64_t v = p[0] | ((uint64_t)p[1] << 32);
            if (reg != 0xE && reg != 0xF) gs_write_reg(reg, v);
            if (++g->reg == g->nreg) { g->reg = 0; if (--g->nloop_left == 0) g->active = 0; }
            if (g->half) { g->half = 0; data += 4; }
            else if (!g->active) { data += 4; }   /* odd total: the rest of the qword is padding */
            else g->half = 1;
        } else { /* IMAGE */
            uint32_t n = g->nloop_left, avail = (uint32_t)(end - data) / 4;
            if (n > avail) n = avail;
            gs_upload_image((const uint8_t*)data, n * 16);
            data += n * 4;
            g->nloop_left -= n;
            if (g->nloop_left == 0) g->active = 0;
        }
    }
}

void ps2_gif_transfer(int path, const uint32_t* data, uint32_t qwc) { gs_gif_transfer(path, data, qwc); }
