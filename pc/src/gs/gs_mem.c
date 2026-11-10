/*
 * GS local memory: 4 MB, organised in 8 KB pages / 256-byte blocks with the
 * format-specific block and column swizzles of the GS.
 */
#include "gs.h"
#include "textures.h"
#include "stream.h"
#include "../ps2_platform.h"
#include <string.h>

uint8_t gs_mem[GS_MEM_SIZE] __attribute__((aligned(64)));
/* the memory the functions below work on: gs_mem, or a scratch buffer while a native texture
   is decoded (gs_mem_scratch_begin/end) */
uint8_t* gs_memp = gs_mem;
static uint8_t gs_mem_scratch[GS_MEM_SIZE] __attribute__((aligned(64)));
static uint8_t clut_saved[GS_CLUT_STATE_SIZE];

void gs_mem_scratch_begin(void) { gs_memp = gs_mem_scratch; gs_clut_state_save(clut_saved); }
void gs_mem_scratch_end(void) { gs_memp = gs_mem; gs_clut_state_restore(clut_saved); }

/* ---- block arrangement inside a page ---- */
static const uint8_t blockTable32[4][8] = {
    { 0, 1, 4, 5, 16, 17, 20, 21 }, { 2, 3, 6, 7, 18, 19, 22, 23 },
    { 8, 9, 12, 13, 24, 25, 28, 29 }, { 10, 11, 14, 15, 26, 27, 30, 31 } };
static const uint8_t blockTableZ32[4][8] = {
    { 24, 25, 28, 29, 8, 9, 12, 13 }, { 26, 27, 30, 31, 10, 11, 14, 15 },
    { 16, 17, 20, 21, 0, 1, 4, 5 }, { 18, 19, 22, 23, 2, 3, 6, 7 } };
static const uint8_t blockTable16[8][4] = {
    { 0, 2, 8, 10 }, { 1, 3, 9, 11 }, { 4, 6, 12, 14 }, { 5, 7, 13, 15 },
    { 16, 18, 24, 26 }, { 17, 19, 25, 27 }, { 20, 22, 28, 30 }, { 21, 23, 29, 31 } };
static const uint8_t blockTable16S[8][4] = {
    { 0, 2, 16, 18 }, { 1, 3, 17, 19 }, { 8, 10, 24, 26 }, { 9, 11, 25, 27 },
    { 4, 6, 20, 22 }, { 5, 7, 21, 23 }, { 12, 14, 28, 30 }, { 13, 15, 29, 31 } };
static const uint8_t blockTableZ16[8][4] = {
    { 24, 26, 16, 18 }, { 25, 27, 17, 19 }, { 28, 30, 20, 22 }, { 29, 31, 21, 23 },
    { 8, 10, 0, 2 }, { 9, 11, 1, 3 }, { 12, 14, 4, 6 }, { 13, 15, 5, 7 } };
static const uint8_t blockTableZ16S[8][4] = {
    { 24, 26, 8, 10 }, { 25, 27, 9, 11 }, { 16, 18, 0, 2 }, { 17, 19, 1, 3 },
    { 28, 30, 12, 14 }, { 29, 31, 13, 15 }, { 20, 22, 4, 6 }, { 21, 23, 5, 7 } };
static const uint8_t blockTable8[4][8] = {
    { 0, 1, 4, 5, 16, 17, 20, 21 }, { 2, 3, 6, 7, 18, 19, 22, 23 },
    { 8, 9, 12, 13, 24, 25, 28, 29 }, { 10, 11, 14, 15, 26, 27, 30, 31 } };
static const uint8_t blockTable4[8][4] = {
    { 0, 2, 8, 10 }, { 1, 3, 9, 11 }, { 4, 6, 12, 14 }, { 5, 7, 13, 15 },
    { 16, 18, 24, 26 }, { 17, 19, 25, 27 }, { 20, 22, 28, 30 }, { 21, 23, 29, 31 } };

/* ---- pixel arrangement inside a block ---- */
static const uint8_t columnTable32[8][8] = {
    { 0, 1, 4, 5, 8, 9, 12, 13 }, { 2, 3, 6, 7, 10, 11, 14, 15 },
    { 16, 17, 20, 21, 24, 25, 28, 29 }, { 18, 19, 22, 23, 26, 27, 30, 31 },
    { 32, 33, 36, 37, 40, 41, 44, 45 }, { 34, 35, 38, 39, 42, 43, 46, 47 },
    { 48, 49, 52, 53, 56, 57, 60, 61 }, { 50, 51, 54, 55, 58, 59, 62, 63 } };
static const uint8_t columnTable16[8][16] = {
    { 0, 2, 8, 10, 16, 18, 24, 26, 1, 3, 9, 11, 17, 19, 25, 27 },
    { 4, 6, 12, 14, 20, 22, 28, 30, 5, 7, 13, 15, 21, 23, 29, 31 },
    { 32, 34, 40, 42, 48, 50, 56, 58, 33, 35, 41, 43, 49, 51, 57, 59 },
    { 36, 38, 44, 46, 52, 54, 60, 62, 37, 39, 45, 47, 53, 55, 61, 63 },
    { 64, 66, 72, 74, 80, 82, 88, 90, 65, 67, 73, 75, 81, 83, 89, 91 },
    { 68, 70, 76, 78, 84, 86, 92, 94, 69, 71, 77, 79, 85, 87, 93, 95 },
    { 96, 98, 104, 106, 112, 114, 120, 122, 97, 99, 105, 107, 113, 115, 121, 123 },
    { 100, 102, 108, 110, 116, 118, 124, 126, 101, 103, 109, 111, 117, 119, 125, 127 } };
static const uint8_t columnTable8[16][16] = {
    { 0, 4, 16, 20, 32, 36, 48, 52, 2, 6, 18, 22, 34, 38, 50, 54 },
    { 8, 12, 24, 28, 40, 44, 56, 60, 10, 14, 26, 30, 42, 46, 58, 62 },
    { 33, 37, 49, 53, 1, 5, 17, 21, 35, 39, 51, 55, 3, 7, 19, 23 },
    { 41, 45, 57, 61, 9, 13, 25, 29, 43, 47, 59, 63, 11, 15, 27, 31 },
    { 96, 100, 112, 116, 64, 68, 80, 84, 98, 102, 114, 118, 66, 70, 82, 86 },
    { 104, 108, 120, 124, 72, 76, 88, 92, 106, 110, 122, 126, 74, 78, 90, 94 },
    { 65, 69, 81, 85, 97, 101, 113, 117, 67, 71, 83, 87, 99, 103, 115, 119 },
    { 73, 77, 89, 93, 105, 109, 121, 125, 75, 79, 91, 95, 107, 111, 123, 127 },
    { 128, 132, 144, 148, 160, 164, 176, 180, 130, 134, 146, 150, 162, 166, 178, 182 },
    { 136, 140, 152, 156, 168, 172, 184, 188, 138, 142, 154, 158, 170, 174, 186, 190 },
    { 161, 165, 177, 181, 129, 133, 145, 149, 163, 167, 179, 183, 131, 135, 147, 151 },
    { 169, 173, 185, 189, 137, 141, 153, 157, 171, 175, 187, 191, 139, 143, 155, 159 },
    { 224, 228, 240, 244, 192, 196, 208, 212, 226, 230, 242, 246, 194, 198, 210, 214 },
    { 232, 236, 248, 252, 200, 204, 216, 220, 234, 238, 250, 254, 202, 206, 218, 222 },
    { 193, 197, 209, 213, 225, 229, 241, 245, 195, 199, 211, 215, 227, 231, 243, 247 },
    { 201, 205, 217, 221, 233, 237, 249, 253, 203, 207, 219, 223, 235, 239, 251, 255 } };
static const uint16_t columnTable4[16][32] = {
    { 0, 8, 32, 40, 64, 72, 96, 104, 2, 10, 34, 42, 66, 74, 98, 106, 4, 12, 36, 44, 68, 76, 100, 108, 6, 14, 38, 46, 70, 78, 102, 110 },
    { 16, 24, 48, 56, 80, 88, 112, 120, 18, 26, 50, 58, 82, 90, 114, 122, 20, 28, 52, 60, 84, 92, 116, 124, 22, 30, 54, 62, 86, 94, 118, 126 },
    { 65, 73, 97, 105, 1, 9, 33, 41, 67, 75, 99, 107, 3, 11, 35, 43, 69, 77, 101, 109, 5, 13, 37, 45, 71, 79, 103, 111, 7, 15, 39, 47 },
    { 81, 89, 113, 121, 17, 25, 49, 57, 83, 91, 115, 123, 19, 27, 51, 59, 85, 93, 117, 125, 21, 29, 53, 61, 87, 95, 119, 127, 23, 31, 55, 63 },
    { 192, 200, 224, 232, 128, 136, 160, 168, 194, 202, 226, 234, 130, 138, 162, 170, 196, 204, 228, 236, 132, 140, 164, 172, 198, 206, 230, 238, 134, 142, 166, 174 },
    { 208, 216, 240, 248, 144, 152, 176, 184, 210, 218, 242, 250, 146, 154, 178, 186, 212, 220, 244, 252, 148, 156, 180, 188, 214, 222, 246, 254, 150, 158, 182, 190 },
    { 129, 137, 161, 169, 193, 201, 225, 233, 131, 139, 163, 171, 195, 203, 227, 235, 133, 141, 165, 173, 197, 205, 229, 237, 135, 143, 167, 175, 199, 207, 231, 239 },
    { 145, 153, 177, 185, 209, 217, 241, 249, 147, 155, 179, 187, 211, 219, 243, 251, 149, 157, 181, 189, 213, 221, 245, 253, 151, 159, 183, 191, 215, 223, 247, 255 },
    { 256, 264, 288, 296, 320, 328, 352, 360, 258, 266, 290, 298, 322, 330, 354, 362, 260, 268, 292, 300, 324, 332, 356, 364, 262, 270, 294, 302, 326, 334, 358, 366 },
    { 272, 280, 304, 312, 336, 344, 368, 376, 274, 282, 306, 314, 338, 346, 370, 378, 276, 284, 308, 316, 340, 348, 372, 380, 278, 286, 310, 318, 342, 350, 374, 382 },
    { 321, 329, 353, 361, 257, 265, 289, 297, 323, 331, 355, 363, 259, 267, 291, 299, 325, 333, 357, 365, 261, 269, 293, 301, 327, 335, 359, 367, 263, 271, 295, 303 },
    { 337, 345, 369, 377, 273, 281, 305, 313, 339, 347, 371, 379, 275, 283, 307, 315, 341, 349, 373, 381, 277, 285, 309, 317, 343, 351, 375, 383, 279, 287, 311, 319 },
    { 448, 456, 480, 488, 384, 392, 416, 424, 450, 458, 482, 490, 386, 394, 418, 426, 452, 460, 484, 492, 388, 396, 420, 428, 454, 462, 486, 494, 390, 398, 422, 430 },
    { 464, 472, 496, 504, 400, 408, 432, 440, 466, 474, 498, 506, 402, 410, 434, 442, 468, 476, 500, 508, 404, 412, 436, 444, 470, 478, 502, 510, 406, 414, 438, 446 },
    { 385, 393, 417, 425, 449, 457, 481, 489, 387, 395, 419, 427, 451, 459, 483, 491, 389, 397, 421, 429, 453, 461, 485, 493, 391, 399, 423, 431, 455, 463, 487, 495 },
    { 401, 409, 433, 441, 465, 473, 497, 505, 403, 411, 435, 443, 467, 475, 499, 507, 405, 413, 437, 445, 469, 477, 501, 509, 407, 415, 439, 447, 471, 479, 503, 511 } };

/* per-format addressing (inlined into the transfer loops) */
static inline uint32_t addr32(const uint8_t (*bt)[8], uint32_t bp, uint32_t bw, uint32_t x, uint32_t y)
{
    uint32_t page = (x >> 6) + (y >> 5) * bw, block = bt[(y >> 3) & 3][(x >> 3) & 7];
    return (((bp + page * 32 + block) * 64 + columnTable32[y & 7][x & 7]) * 4) & (GS_MEM_SIZE - 1);
}
static inline uint32_t addr16(const uint8_t (*bt)[4], uint32_t bp, uint32_t bw, uint32_t x, uint32_t y)
{
    uint32_t page = (x >> 6) + (y >> 6) * bw, block = bt[(y >> 3) & 7][(x >> 4) & 3];
    return ((bp + page * 32 + block) * 256 + columnTable16[y & 7][x & 15] * 2) & (GS_MEM_SIZE - 1);
}
static inline uint32_t addr8(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y)
{
    uint32_t page = (x >> 7) + (y >> 6) * (bw >> 1 ? bw >> 1 : 1), block = blockTable8[(y >> 4) & 3][(x >> 4) & 7];
    return ((bp + page * 32 + block) * 256 + columnTable8[y & 15][x & 15]) & (GS_MEM_SIZE - 1);
}
static inline uint32_t addr4(uint32_t bp, uint32_t bw, uint32_t x, uint32_t y)
{
    uint32_t page = (x >> 7) + (y >> 7) * (bw >> 1 ? bw >> 1 : 1), block = blockTable4[(y >> 4) & 7][(x >> 5) & 3];
    return ((bp + page * 32 + block) * 512 + columnTable4[y & 15][x & 31]) & (GS_MEM_SIZE * 2 - 1);
}
static const uint8_t (*bt16(int psm))[4]
{
    return psm == PSMCT16 ? blockTable16 : psm == PSMCT16S ? blockTable16S : psm == PSMZ16 ? blockTableZ16 : blockTableZ16S;
}

/* byte address for 32/24/16-bit formats, nibble address for 4-bit formats */
uint32_t gs_pixel_addr(int psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y)
{
    if (bw == 0) bw = 1;
    switch (psm) {
    case PSMCT32: case PSMCT24: case PSMT8H: case PSMT4HL: case PSMT4HH: return addr32(blockTable32, bp, bw, x, y);
    case PSMZ32: case PSMZ24: return addr32(blockTableZ32, bp, bw, x, y);
    case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S: return addr16(bt16(psm), bp, bw, x, y);
    case PSMT8: return addr8(bp, bw, x, y);
    case PSMT4: return addr4(bp, bw, x, y);
    }
    return 0;
}

uint32_t gs_read_pixel(int psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y)
{
    uint32_t a = gs_pixel_addr(psm, bp, bw, x, y), v;
    switch (psm) {
    case PSMCT32: case PSMZ32: memcpy(&v, gs_memp + a, 4); return v;
    case PSMCT24: case PSMZ24: memcpy(&v, gs_memp + a, 4); return v & 0xFFFFFF;
    case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S: { uint16_t h; memcpy(&h, gs_memp + a, 2); return h; }
    case PSMT8: return gs_memp[a];
    case PSMT4: return (gs_memp[a >> 1] >> ((a & 1) * 4)) & 15;
    case PSMT8H: return gs_memp[a + 3];
    case PSMT4HL: return gs_memp[a + 3] & 15;
    case PSMT4HH: return gs_memp[a + 3] >> 4;
    }
    return 0;
}

void gs_write_pixel(int psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, uint32_t v)
{
    uint32_t a = gs_pixel_addr(psm, bp, bw, x, y);
    switch (psm) {
    case PSMCT32: case PSMZ32: memcpy(gs_memp + a, &v, 4); break;
    case PSMCT24: case PSMZ24: gs_memp[a] = v; gs_memp[a + 1] = v >> 8; gs_memp[a + 2] = v >> 16; break;
    case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S: { uint16_t h = (uint16_t)v; memcpy(gs_memp + a, &h, 2); break; }
    case PSMT8: gs_memp[a] = (uint8_t)v; break;
    case PSMT4: { uint8_t* p = &gs_memp[a >> 1]; int sh = (a & 1) * 4; *p = (uint8_t)((*p & ~(15 << sh)) | ((v & 15) << sh)); break; }
    case PSMT8H: gs_memp[a + 3] = (uint8_t)v; break;
    case PSMT4HL: gs_memp[a + 3] = (uint8_t)((gs_memp[a + 3] & 0xF0) | (v & 15)); break;
    case PSMT4HH: gs_memp[a + 3] = (uint8_t)((gs_memp[a + 3] & 0x0F) | ((v & 15) << 4)); break;
    }
}

static int psm_bits(int psm)
{
    switch (psm) {
    case PSMCT32: case PSMZ32: return 32;
    case PSMCT24: case PSMZ24: return 24;
    case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S: return 16;
    case PSMT8: case PSMT8H: return 8;
    default: return 4;
    }
}

/* ---------------- transfers ---------------- */

/* pages written by the current host->local transfer, and whether any byte changed: games
   re-upload the same textures every frame; identical data must not invalidate the cache */
static uint64_t xfer_pages[512 / 64];
static int xfer_changed;

static inline void write_tracked(int psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, uint32_t v)
{
    uint32_t a = gs_pixel_addr(psm, bp, bw, x, y), page = (psm == PSMT4 ? a >> 1 : a) >> 13;
    uint8_t* m;
    xfer_pages[page >> 6] |= 1ull << (page & 63);
    switch (psm) {
    case PSMCT32: case PSMZ32: {
        uint32_t old; m = gs_memp + a; memcpy(&old, m, 4);
        if (old != v) { memcpy(m, &v, 4); xfer_changed = 1; }
        return;
    }
    case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S: {
        uint16_t old, h = (uint16_t)v; m = gs_memp + a; memcpy(&old, m, 2);
        if (old != h) { memcpy(m, &h, 2); xfer_changed = 1; }
        return;
    }
    case PSMT8:
        if (gs_memp[a] != (uint8_t)v) { gs_memp[a] = (uint8_t)v; xfer_changed = 1; }
        return;
    default:
        if (gs_read_pixel(psm, bp, bw, x, y) != v) { gs_write_pixel(psm, bp, bw, x, y, v); xfer_changed = 1; }
        return;
    }
}

void gs_exec_upload(const uint8_t* src, uint32_t bytes)
{
    uint32_t dbp = (gs.bitbltbuf >> 32) & 0x3FFF, dbw = (gs.bitbltbuf >> 48) & 0x3F, dpsm = (gs.bitbltbuf >> 56) & 0x3F;
    uint32_t dsax = (gs.trxpos >> 32) & 0x7FF, dsay = (gs.trxpos >> 48) & 0x7FF;
    uint32_t rrw = gs.trxreg & 0xFFF, rrh = (gs.trxreg >> 32) & 0xFFF;
    int bits = psm_bits(dpsm);
    uint32_t i = 0;

    if (!gs.xfer_active || rrw == 0) return;
    {   /* fast paths: per-format addressing hoisted out of the pixel loop */
        uint32_t bw = dbw ? dbw : 1;
#define MARK(a_) do { uint32_t pg_ = (a_) >> 13; xfer_pages[pg_ >> 6] |= 1ull << (pg_ & 63); } while (0)
#define ADVANCE() do { if (++gs.xfer_x >= rrw) { gs.xfer_x = 0; gs.xfer_y++; } } while (0)
        if ((dpsm == PSMCT32 || dpsm == PSMZ32) && !(dsax & 7) && !(rrw & 7)) {
            /* whole 8x8 blocks: assemble each 256-byte block in swizzled order, compare and
               store it at once (texture streaming uploads megabytes per frame) */
            const uint8_t (*bt)[8] = dpsm == PSMCT32 ? blockTable32 : blockTableZ32;
            while (gs.xfer_x == 0 && !((dsay + gs.xfer_y) & 7) && rrh - gs.xfer_y >= 8 && bytes - i >= rrw * 32) {
                uint32_t y0 = dsay + gs.xfer_y;
                for (uint32_t bx = 0; bx < rrw; bx += 8) {
                    uint32_t blk[64], a = addr32(bt, dbp, bw, dsax + bx, y0);   /* block base: column 0 */
                    for (uint32_t yy = 0; yy < 8; yy++)
                        for (uint32_t xx = 0; xx < 8; xx++)
                            memcpy(&blk[columnTable32[yy][xx]], src + i + ((size_t)yy * rrw + bx + xx) * 4, 4);
                    if (memcmp(gs_memp + a, blk, 256)) { memcpy(gs_memp + a, blk, 256); xfer_changed = 1; }
                    MARK(a);
                }
                i += rrw * 32;
                gs.xfer_y += 8;
            }
        }
        if (dpsm == PSMCT32 || dpsm == PSMZ32) {
            const uint8_t (*bt)[8] = dpsm == PSMCT32 ? blockTable32 : blockTableZ32;
            while (i + 4 <= bytes && gs.xfer_y < rrh) {
                uint32_t a = addr32(bt, dbp, bw, dsax + gs.xfer_x, dsay + gs.xfer_y);
                if (memcmp(gs_memp + a, src + i, 4)) { memcpy(gs_memp + a, src + i, 4); xfer_changed = 1; }
                MARK(a); i += 4; ADVANCE();
            }
        } else if (dpsm == PSMCT16 || dpsm == PSMCT16S || dpsm == PSMZ16 || dpsm == PSMZ16S) {
            const uint8_t (*bt)[4] = bt16(dpsm);
            while (i + 2 <= bytes && gs.xfer_y < rrh) {
                uint32_t a = addr16(bt, dbp, bw, dsax + gs.xfer_x, dsay + gs.xfer_y);
                if (memcmp(gs_memp + a, src + i, 2)) { memcpy(gs_memp + a, src + i, 2); xfer_changed = 1; }
                MARK(a); i += 2; ADVANCE();
            }
        } else if (dpsm == PSMT8) {
            while (i < bytes && gs.xfer_y < rrh) {
                uint32_t a = addr8(dbp, bw, dsax + gs.xfer_x, dsay + gs.xfer_y);
                if (gs_memp[a] != src[i]) { gs_memp[a] = src[i]; xfer_changed = 1; }
                MARK(a); i++; ADVANCE();
            }
        } else if (dpsm == PSMT4) {
            while (i < bytes && gs.xfer_y < rrh) {
                uint32_t a = addr4(dbp, bw, dsax + gs.xfer_x, dsay + gs.xfer_y), sh = (a & 1) * 4;
                uint8_t v = (uint8_t)((src[i] >> (gs.xfer_pending_bits ? 4 : 0)) & 15), *p = &gs_memp[a >> 1];
                if (gs.xfer_pending_bits) { i++; gs.xfer_pending_bits = 0; } else gs.xfer_pending_bits = 1;
                if (((*p >> sh) & 15) != v) { *p = (uint8_t)((*p & ~(15 << sh)) | (v << sh)); xfer_changed = 1; }
                MARK(a >> 1); ADVANCE();
            }
        }
#undef MARK
#undef ADVANCE
    }
    while (i < bytes && gs.xfer_y < rrh) {
        uint32_t v;
        switch (bits) {
        case 32: if (i + 4 > bytes) return; memcpy(&v, src + i, 4); i += 4; break;
        case 24: if (i + 3 > bytes) return; v = src[i] | (src[i + 1] << 8) | (src[i + 2] << 16); i += 3; break;
        case 16: if (i + 2 > bytes) return; v = src[i] | (src[i + 1] << 8); i += 2; break;
        case 8: v = src[i++]; break;
        default: /* 4-bit: low nibble first */
            v = src[i] >> (gs.xfer_pending_bits ? 4 : 0);
            if (gs.xfer_pending_bits) { i++; gs.xfer_pending_bits = 0; } else gs.xfer_pending_bits = 1;
            v &= 15;
            break;
        }
        write_tracked(dpsm, dbp, dbw, dsax + gs.xfer_x, dsay + gs.xfer_y, v);
        if (++gs.xfer_x >= rrw) { gs.xfer_x = 0; gs.xfer_y++; }
    }
    if (gs.xfer_y >= rrh) {
        gs.xfer_active = 0;
        if (gs_memp == gs_mem) {   /* scratch uploads (texture decoding) touch nothing visible */
            gs_notify_pages(xfer_pages, xfer_changed);
            stream_note_upload(xfer_pages);
        }
        memset(xfer_pages, 0, sizeof(xfer_pages));
        xfer_changed = 0;
    }
}

void gs_local_to_local(void)
{
    uint32_t sbp = gs.bitbltbuf & 0x3FFF, sbw = (gs.bitbltbuf >> 16) & 0x3F, spsm = (gs.bitbltbuf >> 24) & 0x3F;
    uint32_t dbp = (gs.bitbltbuf >> 32) & 0x3FFF, dbw = (gs.bitbltbuf >> 48) & 0x3F, dpsm = (gs.bitbltbuf >> 56) & 0x3F;
    uint32_t ssax = gs.trxpos & 0x7FF, ssay = (gs.trxpos >> 16) & 0x7FF;
    uint32_t dsax = (gs.trxpos >> 32) & 0x7FF, dsay = (gs.trxpos >> 48) & 0x7FF;
    uint32_t rrw = gs.trxreg & 0xFFF, rrh = (gs.trxreg >> 32) & 0xFFF;
    gs_flush();
    for (uint32_t y = 0; y < rrh; y++)
        for (uint32_t x = 0; x < rrw; x++)
            gs_write_pixel(dpsm, dbp, dbw, dsax + x, dsay + y, gs_read_pixel(spsm, sbp, sbw, ssax + x, ssay + y));
    gs_notify_mem_write(dbp, dbw, dpsm, dsax, dsay, rrw, rrh);
}

/* local -> host (StoreImage, render thread): rendered pixels live in OpenGL render targets,
   so the region is first read back into GS memory */
void gs_exec_download(uint8_t* dst, uint32_t bytes)
{
    ntex_before_access(0, 511);
    uint32_t sbp = gs.bitbltbuf & 0x3FFF, sbw = (gs.bitbltbuf >> 16) & 0x3F, spsm = (gs.bitbltbuf >> 24) & 0x3F;
    uint32_t ssax = gs.trxpos & 0x7FF, ssay = (gs.trxpos >> 16) & 0x7FF;
    uint32_t rrw = gs.trxreg & 0xFFF, rrh = (gs.trxreg >> 32) & 0xFFF;
    int bits = psm_bits(spsm);
    uint32_t i = 0, x = 0, y = 0;
    gs_flush();
    gs_rt_to_mem(sbp, sbw, spsm, ssax, ssay, rrw, rrh);
    while (rrw && y < rrh && i + (bits >= 8 ? (uint32_t)bits / 8 : 1) <= bytes) {   /* the rectangle, at most the destination */
        uint32_t v = gs_read_pixel(spsm, sbp, sbw, ssax + x, ssay + y);
        if (bits == 32) { memcpy(dst + i, &v, 4); i += 4; }
        else if (bits == 24) { dst[i] = v; dst[i + 1] = v >> 8; dst[i + 2] = v >> 16; i += 3; }
        else if (bits == 16) { dst[i] = v; dst[i + 1] = v >> 8; i += 2; }
        else dst[i++] = (uint8_t)v;
        if (++x >= rrw) { x = 0; y++; }
    }
}

/* ---------------- texture decode ---------------- */

static uint32_t expand16(uint32_t c, uint64_t texa)
{
    uint32_t r = (c & 0x1F) << 3, g = ((c >> 5) & 0x1F) << 3, b = ((c >> 10) & 0x1F) << 3;
    uint32_t ta0 = texa & 0xFF, ta1 = (texa >> 32) & 0xFF, aem = (texa >> 15) & 1;
    uint32_t a = (c & 0x8000) ? ta1 : ((aem && !(c & 0x7FFF)) ? 0 : ta0);
    return r | (g << 8) | (b << 16) | (a << 24);
}

static uint32_t to_rgba(int psm, uint32_t v, uint64_t texa, int tcc)
{
    switch (psm) {
    case PSMCT32: case PSMZ32: return v;
    case PSMCT24: case PSMZ24: {
        uint32_t aem = (texa >> 15) & 1, ta0 = texa & 0xFF;
        uint32_t a = (aem && !(v & 0xFFFFFF)) ? 0 : ta0;
        return (v & 0xFFFFFF) | (a << 24);
    }
    default: return expand16(v, texa);
    }
    (void)tcc;
}

/* ---------------- CLUT buffer (GS internal, 512 x 16 bit) ---------------- */
static uint16_t clut_buf[512];
static uint32_t cbp0, cbp1;

/* memory position of CLUT entry i in CSM1 storage */
void gs_clut_state_save(uint8_t* out)
{
    memcpy(out, clut_buf, sizeof(clut_buf));
    memcpy(out + sizeof(clut_buf), &cbp0, 4);
    memcpy(out + sizeof(clut_buf) + 4, &cbp1, 4);
}
void gs_clut_state_restore(const uint8_t* in)
{
    memcpy(clut_buf, in, sizeof(clut_buf));
    memcpy(&cbp0, in + sizeof(clut_buf), 4);
    memcpy(&cbp1, in + sizeof(clut_buf) + 4, 4);
}

static void clut_pos(int entries, uint32_t i, uint32_t* x, uint32_t* y)
{
    if (entries == 16) { *x = i & 7; *y = i >> 3; return; }
    *x = (i & 7) | ((i & 0x10) >> 1);
    *y = ((i & 0x08) >> 3) | ((i & 0xE0) >> 4);
}

/* CLUT entry i's byte address at CBP 0 (CT32, CT16, CT16S; 16 or 256 entries): every format's
   address is bp * 256 + its offset at bp 0 (gs_mem_selftest checks it against gs_pixel_addr) */
static uint16_t clut_off[3][2][256];
static void clut_off_init(void)
{
    static const int fmt[3] = { PSMCT32, PSMCT16, PSMCT16S };
    for (int f = 0; f < 3; f++)
        for (int k = 0; k < 2; k++)
            for (uint32_t i = 0; i < (k ? 256u : 16u); i++) {
                uint32_t x, y;
                clut_pos(k ? 256 : 16, i, &x, &y);
                clut_off[f][k][i] = (uint16_t)gs_pixel_addr(fmt[f], 0, 1, x, y);
            }
}

/* Handle the CLD field of a TEX0 write. */
void gs_clut_load(uint64_t tex0)
{
    uint32_t psm = (tex0 >> 20) & 0x3F, cbp = (tex0 >> 37) & 0x3FFF, cpsm = (tex0 >> 51) & 0xF;
    uint32_t csa = (tex0 >> 56) & 0x1F, cld = (tex0 >> 61) & 7;
    int entries, load = 0;
    if (psm == PSMT8 || psm == PSMT8H) entries = 256;
    else if (psm == PSMT4 || psm == PSMT4HL || psm == PSMT4HH) entries = 16;
    else return;
    switch (cld) {
    case 1: load = 1; break;
    case 2: load = 1; cbp0 = cbp; break;
    case 3: load = 1; cbp1 = cbp; break;
    case 4: load = cbp != cbp0; cbp0 = cbp; break;
    case 5: load = cbp != cbp1; cbp1 = cbp; break;
    }
    if (!load) return;
    gs_flush();
    ntex_before_access(cbp / 32, cbp / 32 + 1);
    {   /* gs_read_pixel(fmt, cbp, 1, clut_pos(i)) through the offset table: the address is cbp * 256 + clut_off */
        const uint16_t* off = clut_off[cpsm == 0 ? 0 : cpsm == 2 ? 1 : 2][entries == 256];
        uint32_t base = cbp * 256;
        if (!clut_off[0][1][1]) clut_off_init();   /* (entry 1 is 4 bytes in) */
        if (cpsm == 0)
            for (uint32_t i = 0; i < (uint32_t)entries; i++) {
                uint32_t c, e = (csa * 16 + i) & 255;
                memcpy(&c, gs_memp + ((base + off[i]) & (GS_MEM_SIZE - 1)), 4);
                clut_buf[e] = (uint16_t)c; clut_buf[256 + e] = (uint16_t)(c >> 16);
            }
        else
            for (uint32_t i = 0; i < (uint32_t)entries; i++)
                memcpy(&clut_buf[(csa * 16 + i) & 511], gs_memp + ((base + off[i]) & (GS_MEM_SIZE - 1)), 2);
    }
}

static uint32_t clut_lookup(uint64_t tex0, uint64_t texa, uint32_t idx)
{
    uint32_t cpsm = (tex0 >> 51) & 0xF, csa = (tex0 >> 56) & 0x1F;
    if (cpsm == 0) {
        uint32_t e = (csa * 16 + idx) & 255;
        return clut_buf[e] | ((uint32_t)clut_buf[256 + e] << 16);
    }
    return expand16(clut_buf[(csa * 16 + idx) & 511], texa);
}

void gs_get_palette(uint64_t tex0, uint64_t texa, uint32_t* out, int n)
{
    for (int i = 0; i < n; i++) out[i] = clut_lookup(tex0, texa, (uint32_t)i);
}

void gs_decode_texture(uint64_t tex0, uint64_t texa, uint32_t w, uint32_t h, uint32_t* out)
{
    uint32_t tbp = tex0 & 0x3FFF, tbw = (tex0 >> 14) & 0x3F, psm = (tex0 >> 20) & 0x3F, tcc = (tex0 >> 34) & 1;
    uint32_t pal[256];
    int indexed = (psm == PSMT8 || psm == PSMT4 || psm == PSMT8H || psm == PSMT4HL || psm == PSMT4HH);
    if (indexed) {
        int n = (psm == PSMT8 || psm == PSMT8H) ? 256 : 16;
        for (int i = 0; i < n; i++) pal[i] = clut_lookup(tex0, texa, (uint32_t)i);
    }
    {   /* the common formats with their addressing hoisted out of the pixel loop (gs_read_pixel per
           pixel below: the same reads; pc/tests/test_gsmem.c compares the two) */
        uint32_t bw = tbw ? tbw : 1;
        switch (psm) {
        case PSMT8:
            for (uint32_t y = 0; y < h; y++, out += w)
                for (uint32_t x = 0; x < w; x++) out[x] = pal[gs_memp[addr8(tbp, bw, x, y)]];
            return;
        case PSMT4:
            for (uint32_t y = 0; y < h; y++, out += w)
                for (uint32_t x = 0; x < w; x++) {
                    uint32_t a = addr4(tbp, bw, x, y);
                    out[x] = pal[(gs_memp[a >> 1] >> ((a & 1) * 4)) & 15];
                }
            return;
        case PSMCT32:
            for (uint32_t y = 0; y < h; y++, out += w)
                for (uint32_t x = 0; x < w; x++) memcpy(&out[x], gs_memp + addr32(blockTable32, tbp, bw, x, y), 4);
            return;
        case PSMCT16: case PSMCT16S: {
            const uint8_t (*bt)[4] = bt16((int)psm);
            for (uint32_t y = 0; y < h; y++, out += w)
                for (uint32_t x = 0; x < w; x++) {
                    uint16_t v;
                    memcpy(&v, gs_memp + addr16(bt, tbp, bw, x, y), 2);
                    out[x] = expand16(v, texa);
                }
            return;
        }
        }
    }
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            uint32_t v = gs_read_pixel(psm, tbp, tbw, x, y);
            uint32_t c = indexed ? pal[v] : to_rgba(psm, v, texa, tcc);
            out[y * w + x] = c;
        }
}

/* ---------------- self test: swizzle tables must be permutations ---------------- */
int gs_mem_selftest(void)
{
    static uint8_t seen[512];
    memset(seen, 0, sizeof(seen));
    for (int y = 0; y < 16; y++) for (int x = 0; x < 32; x++) { if (seen[columnTable4[y][x]]++) return 0; }
    memset(seen, 0, sizeof(seen));
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) { if (seen[columnTable8[y][x]]++) return 0; }
    memset(seen, 0, sizeof(seen));
    for (int y = 0; y < 8; y++) for (int x = 0; x < 16; x++) { if (seen[columnTable16[y][x]]++) return 0; }
    memset(seen, 0, sizeof(seen));
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) { if (seen[columnTable32[y][x]]++) return 0; }
    clut_off_init();   /* and the CLUT offsets are gs_pixel_addr's, at any CBP */
    {
        static const int fmt[3] = { PSMCT32, PSMCT16, PSMCT16S };
        static const uint32_t cbps[] = { 0, 1, 31, 32, 8191, 16380, 16383 };
        for (int f = 0; f < 3; f++)
            for (int k = 0; k < 2; k++)
                for (uint32_t i = 0; i < (k ? 256u : 16u); i++)
                    for (size_t b = 0; b < sizeof(cbps) / sizeof(cbps[0]); b++) {
                        uint32_t x, y;
                        clut_pos(k ? 256 : 16, i, &x, &y);
                        if (((cbps[b] * 256 + clut_off[f][k][i]) & (GS_MEM_SIZE - 1)) != gs_pixel_addr(fmt[f], cbps[b], 1, x, y)) return 0;
                    }
    }
    return 1;
}
