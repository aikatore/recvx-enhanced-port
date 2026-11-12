/* GS memory (pc/src/gs/gs_mem.c): the fast paths give what the per-pixel reads give. Texture
   decodes of every format (random GS memory, CLUT, TEX0, TEXA, sizes) against gs_read_pixel per
   pixel, and CLUT loads through the offset table against gs_read_pixel per entry. */
#include "../src/gs/gs_mem.c"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

GsState gs;
static int flushes;
void gs_flush(void) { flushes++; }
void gs_notify_mem_write(uint32_t bp, uint32_t bw, int psm, uint32_t x, uint32_t y, uint32_t w, uint32_t h) { (void)bp; (void)bw; (void)psm; (void)x; (void)y; (void)w; (void)h; }
void gs_notify_pages(const uint64_t* pages, int changed) { (void)pages; (void)changed; }
void gs_rt_to_mem(uint32_t bp, uint32_t bw, uint32_t psm, uint32_t x, uint32_t y, uint32_t w, uint32_t h) { (void)bp; (void)bw; (void)psm; (void)x; (void)y; (void)w; (void)h; }
void ntex_before_access(uint32_t p0, uint32_t p1) { (void)p0; (void)p1; }
int ntex_pending(uint32_t p0, uint32_t p1) { (void)p0; (void)p1; return 0; }
int gs_ab_off(int bit) { (void)bit; return 0; }
void stream_note_upload(const uint64_t* pages) { (void)pages; }

static uint32_t rng = 12345;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* gs_decode_texture before its fast paths */
static void decode_ref(uint64_t tex0, uint64_t texa, uint32_t w, uint32_t h, uint32_t* out)
{
    uint32_t tbp = tex0 & 0x3FFF, tbw = (tex0 >> 14) & 0x3F, psm = (tex0 >> 20) & 0x3F, tcc = (tex0 >> 34) & 1, pal[256];
    int indexed = (psm == PSMT8 || psm == PSMT4 || psm == PSMT8H || psm == PSMT4HL || psm == PSMT4HH);
    if (indexed) gs_get_palette(tex0, texa, pal, (psm == PSMT8 || psm == PSMT8H) ? 256 : 16);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            uint32_t v = gs_read_pixel((int)psm, tbp, tbw, x, y);
            out[y * w + x] = indexed ? pal[v] : to_rgba((int)psm, v, texa, (int)tcc);
        }
}

int main(void)
{
    static const int psms[] = { PSMCT32, PSMCT24, PSMCT16, PSMCT16S, PSMT8, PSMT4, PSMT8H, PSMT4HL, PSMT4HH };
    static uint32_t a[256 * 256], b[256 * 256];
    CHECK(gs_mem_selftest());
    for (size_t i = 0; i < GS_MEM_SIZE / 4; i++) { uint32_t v = rnd(); memcpy(gs_mem + i * 4, &v, 4); }
    for (int i = 0; i < 512; i++) clut_buf[i] = (uint16_t)rnd();
    for (int n = 0; n < 600; n++) {
        int psm = psms[n % 9];
        uint32_t w = 1u << (rnd() % 9), h = 1u << (rnd() % 9);
        uint64_t tex0 = (rnd() & 0x3FFF) | (uint64_t)(rnd() % 64) << 14 | (uint64_t)psm << 20 | (uint64_t)(rnd() & 1) << 34 |
                        (uint64_t)(rnd() % 3 == 0 ? 0 : rnd() % 2 ? 2 : 10) << 51 | (uint64_t)(rnd() & 31) << 56;
        uint64_t texa = rnd() | (uint64_t)rnd() << 32;
        decode_ref(tex0, texa, w, h, a);
        gs_decode_texture(tex0, texa, w, h, b);
        if (memcmp(a, b, (size_t)w * h * 4)) { printf("FAIL decode psm %d %ux%u tex0 %016llx\n", psm, w, h, (unsigned long long)tex0); return 1; }
    }
    for (int n = 0; n < 200; n++) {   /* rectangles as gs_read_pixel reads them */
        static const int rp[] = { PSMCT32, PSMCT24, PSMCT16, PSMCT16S, PSMZ16, PSMT8 };
        int psm = rp[n % 6];
        uint32_t bp = rnd() & 0x3FFF, bw = rnd() % 64, w = 1 + rnd() % 200, h = 1 + rnd() % 200;
        gs_read_rect(psm, bp, bw, w, h, b);
        for (uint32_t y = 0; y < h; y++)
            for (uint32_t x = 0; x < w; x++)
                if (b[y * w + x] != gs_read_pixel(psm, bp, bw, x, y)) {
                    printf("FAIL rect psm %d bp %u bw %u %ux%u at %u,%u: %08x, pixel %08x addr %06x\n", psm, bp, bw, w, h, x, y,
                           b[y * w + x], gs_read_pixel(psm, bp, bw, x, y), gs_pixel_addr(psm, bp, bw, x, y));
                    return 1;
                }
    }
    for (int n = 0; n < 2000; n++) {   /* CLD 1 loads: entries at CSA from CBP; the batch flushed only if the CLUT changes */
        static const uint32_t cpsms[] = { 0, 2, 10 };
        uint16_t want[512];
        uint32_t psm = n & 1 ? PSMT8 : PSMT4, cbp = rnd() & 0x3FFF, cpsm = cpsms[rnd() % 3], csa = rnd() & 31, entries = psm == PSMT8 ? 256 : 16;
        memcpy(want, clut_buf, sizeof(want));
        for (uint32_t i = 0; i < entries; i++) {
            uint32_t x, y;
            clut_pos((int)entries, i, &x, &y);
            if (cpsm == 0) {
                uint32_t c = gs_read_pixel(PSMCT32, cbp, 1, x, y), e = (csa * 16 + i) & 255;
                want[e] = (uint16_t)c; want[256 + e] = (uint16_t)(c >> 16);
            } else want[(csa * 16 + i) & 511] = (uint16_t)gs_read_pixel(cpsm == 2 ? PSMCT16 : PSMCT16S, cbp, 1, x, y);
        }
        int changed = memcmp(want, clut_buf, sizeof(want)) != 0;
        uint64_t tex0 = (uint64_t)psm << 20 | (uint64_t)cbp << 37 | (uint64_t)cpsm << 51 | (uint64_t)csa << 56 | 1ull << 61;
        flushes = 0;
        gs_clut_load(tex0);
        if (memcmp(want, clut_buf, sizeof(want))) { printf("FAIL clut cbp %u cpsm %u csa %u\n", cbp, cpsm, csa); return 1; }
        CHECK(flushes == changed);
        gs_clut_load(tex0);   /* the same CLUT again */
        CHECK(flushes == changed && !memcmp(want, clut_buf, sizeof(want)));
    }
    printf("ok\n");
    return 0;
}
