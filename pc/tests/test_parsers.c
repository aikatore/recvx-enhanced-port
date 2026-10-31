/* Checked file reading (pc/include/pc_parse.h): crafted inputs for each case the parsers refuse, and
   the PRS decoder against the game's own (decomp expand.c, the C version it replaces) on generated
   streams. No game data; also run under ASan / UBSan (-fsanitize=address,undefined). */
#include "../include/pc_parse.h"
#include "../src/net/coop_proto.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); fails++; } } while (0)
static void put32(uint8_t* p, uint32_t v) { memcpy(p, &v, 4); }
static void put16(uint8_t* p, uint16_t v) { memcpy(p, &v, 2); }

/* ---------------------------------------------------------------- PRS */
/* decomp expand.c: the C version of Expand() by Clownacy the PC build had (verified equivalent to the asm) */
typedef struct { const unsigned char* source; unsigned char descriptor_field, descriptor_bits_remaining; } RefState;
static void ref_refresh(RefState* s) { s->descriptor_field = *s->source++; s->descriptor_bits_remaining = 8; }
static int ref_bit(RefState* s)
{
    int bit;
    if (--s->descriptor_bits_remaining == 0) ref_refresh(s);
    bit = s->descriptor_field & 1;
    s->descriptor_field >>= 1;
    return bit;
}
static int ref_expand(const char* src, unsigned char* d)
{
    unsigned char* destination = d;
    RefState state;
    state.source = (const unsigned char*)src;
    ref_refresh(&state);
    ++state.descriptor_bits_remaining;
    for (;;) {
        if (ref_bit(&state)) *destination++ = *state.source++;
        else {
            unsigned int length = 0;
            int offset;
            if (!ref_bit(&state)) {
                if (ref_bit(&state)) length += 2;
                if (ref_bit(&state)) ++length;
                ++length;
                offset = -0x100 + *state.source++;
            } else {
                const unsigned int lower = *state.source++, upper = *state.source++, whole = lower | (upper << 8);
                if (whole == 0) break;
                offset = -0x2000 + (whole >> 3);
                length = whole & 7;
                if (length != 0) ++length;
                else length = *state.source++;
            }
            do { *destination = destination[offset]; ++destination; } while (length-- != 0);
        }
    }
    return (int)(destination - d);
}

/* a PRS writer: descriptor bits LSB first, a new descriptor byte where the decoder will read it */
typedef struct { uint8_t* p; uint32_t n, slot; int cnt; } W;
static void wbit(W* w, int b)
{
    if (w->cnt == 8) { w->slot = w->n++; w->p[w->slot] = 0; w->cnt = 0; }
    if (b) w->p[w->slot] |= (uint8_t)(1 << w->cnt);
    w->cnt++;
}
static uint32_t rng = 12345;
static uint32_t rnd(uint32_t n) { rng = rng * 1103515245u + 12345u; return (rng >> 8) % n; }

/* a random stream of literals and copies of all three kinds (into s), its output into out: sizes */
static void gen(uint8_t* s, uint32_t* sn, uint8_t* out, uint32_t* on, uint32_t ops)
{
    W w = { s, 1, 0, 0 };
    uint32_t o = 0;
    s[0] = 0;
    for (uint32_t k = 0; k < ops; k++) {
        uint32_t kind = o ? rnd(4) : 0, dist, cnt, v;
        if (kind == 0) { uint8_t b = (uint8_t)rnd(4); wbit(&w, 1); s[w.n++] = b; out[o++] = b; continue; }
        if (kind == 1) {   /* short: 2..5 from up to 256 back */
            dist = 1 + rnd(o < 256 ? o : 256); cnt = 2 + rnd(4);
            wbit(&w, 0); wbit(&w, 0); wbit(&w, (cnt - 2) >> 1); wbit(&w, (cnt - 2) & 1);
            s[w.n++] = (uint8_t)(256 - dist);
        } else if (kind == 2) {   /* long: 3..9 from up to 8192 back */
            dist = 1 + rnd(o < 8192 ? o : 8192); cnt = 3 + rnd(7);
            v = (8192 - dist) << 3 | (cnt - 2);
            wbit(&w, 0); wbit(&w, 1); s[w.n++] = (uint8_t)v; s[w.n++] = (uint8_t)(v >> 8);
        } else {   /* with a count byte: 1..256 */
            dist = 1 + rnd(o < 8191 ? o : 8191); cnt = 1 + rnd(256);
            v = (8192 - dist) << 3;
            wbit(&w, 0); wbit(&w, 1); s[w.n++] = (uint8_t)v; s[w.n++] = (uint8_t)(v >> 8); s[w.n++] = (uint8_t)(cnt - 1);
        }
        for (uint32_t i = 0; i < cnt; i++, o++) out[o] = out[o - dist];
    }
    wbit(&w, 0); wbit(&w, 1); s[w.n++] = 0; s[w.n++] = 0;   /* the end */
    *sn = w.n; *on = o;
}

static void test_prs(void)
{
    enum { CAP = 1 << 20 };
    uint8_t *s = malloc(CAP), *want = malloc(CAP), *ref = malloc(CAP), *got = malloc(CAP);
    uint32_t sn, on;
    for (int t = 0; t < 300; t++) {   /* the same output as the game's decoder, and as generated */
        gen(s, &sn, want, &on, 1 + rnd(3000));
        memset(ref, 0, CAP); memset(got, 0, CAP);
        CHECK(ref_expand((const char*)s, ref) == (int)on);
        CHECK(pc_prs_decode(s, sn, got, CAP) == (int32_t)on);
        CHECK(!memcmp(ref, want, on) && !memcmp(got, want, on));
    }
    {   /* in place, as the room loader: the packed copy at the top of the arena it expands into */
        uint8_t *a = malloc(CAP), *b = malloc(CAP);
        gen(s, &sn, want, &on, 5000);
        memset(a, 0x5A, CAP);
        memcpy(a + CAP - sn, s, sn);
        memcpy(b, a, CAP);
        CHECK(ref_expand((const char*)a + CAP - sn, a) == (int)on);
        CHECK(pc_prs_decode(b + CAP - sn, sn, b, CAP) == (int32_t)on);
        CHECK(!memcmp(a, b, CAP));
        free(a); free(b);
    }
    {   /* overrun: the output does not fit; nothing written past the room given */
        uint8_t* small = malloc(64 + 16);
        gen(s, &sn, want, &on, 400);
        CHECK(on > 64);
        memset(small, 0xEE, 80);
        CHECK(pc_prs_decode(s, sn, small, 64) == PC_PRS_FULL);
        CHECK(small[64] == 0xEE && small[79] == 0xEE);
        free(small);
    }
    {   /* a copy from before the output's start: the first op, and one reaching 1 byte too far */
        uint8_t first[] = { 0x02, 0x00, 0x00 };   /* bits 0, 1: a long copy; w = 0 would end, so: */
        uint32_t v = (8192 - 1) << 3 | 1;
        first[1] = (uint8_t)v; first[2] = (uint8_t)(v >> 8);
        CHECK(pc_prs_decode(first, sizeof(first), got, CAP) == PC_PRS_BROKEN);
        W w = { s, 1, 0, 0 };
        s[0] = 0;
        wbit(&w, 1); s[w.n++] = 7;                                      /* 1 byte out */
        wbit(&w, 0); wbit(&w, 0); wbit(&w, 0); wbit(&w, 0); s[w.n++] = 256 - 2;   /* from 2 back */
        wbit(&w, 0); wbit(&w, 1); s[w.n++] = 0; s[w.n++] = 0;
        CHECK(pc_prs_decode(s, w.n, got, CAP) == PC_PRS_BROKEN);
    }
    {   /* the source cut anywhere: refused, never read past its end (a buffer of exactly its size) */
        gen(s, &sn, want, &on, 300);
        for (uint32_t cut = 0; cut < sn; cut += 1 + sn / 97) {
            uint8_t* c = malloc(cut ? cut : 1);
            memcpy(c, s, cut);
            CHECK(pc_prs_decode(c, cut, got, CAP) == PC_PRS_BROKEN);
            free(c);
        }
    }
    {   /* the growing version: from a 16-byte start */
        long cap = 16, r;
        unsigned char* out = malloc(16);
        gen(s, &sn, want, &on, 3000);
        r = pc_prs_expand(s, sn, &out, &cap);
        CHECK(r == (long)on && cap >= r && !memcmp(out, want, on));
        free(out);
    }
    free(s); free(want); free(ref); free(got);
}

/* ---------------------------------------------------------------- AFS */
static void test_afs(void)
{
    uint8_t t[8 + 3 * 8] = { 'A', 'F', 'S', 0 };
    uint32_t off, len;
    put32(t + 4, 0x40000000u);   /* 8 + count * 8 wraps to 8 in 32 bits */
    CHECK(pc_afs_count(t, 1u << 30) < 0);
    CHECK(pc_afs_count(t, UINT64_MAX) < 0);   /* (above PC_AFS_MAX however large the archive) */
    put32(t + 4, 3);
    CHECK(pc_afs_count(t, 8 + 3 * 8 - 1) < 0);   /* the table does not fit the archive */
    CHECK(pc_afs_count(t, 4096) == 3);
    t[0] = 'X';
    CHECK(pc_afs_count(t, 4096) < 0);
    put32(t + 8, 2048); put32(t + 12, 100);                      /* inside */
    put32(t + 16, 4000); put32(t + 20, 200);                     /* past the end */
    put32(t + 24, 0xFFFFFF00u); put32(t + 28, 0x200);            /* offset + size wraps */
    CHECK(pc_afs_entry(t, 0, 4096, &off, &len) && off == 2048 && len == 100);
    CHECK(!pc_afs_entry(t, 1, 4096, &off, &len));
    CHECK(!pc_afs_entry(t, 2, 4096, &off, &len));
}

/* ---------------------------------------------------------------- .cvxm */
#define BODY 80   /* the MLB's offsets count from here: 60 (chunk size) + 4, its 16-byte header */
enum { TL = 0, NAMES = 8, NAME = 20, OBJS = 32, MODEL = 144, VL = 176, PL = 336, CSZ = 16 + 382, TOFF = 512, SOFF = 912, SIZE = SOFF + 3 * 44 };
/* the smallest model the loader takes: 2 objects, the pelvis's mesh of 3 vertices and one strip, a 4-bit 4x4 texture */
static void mk_cvxm(uint8_t* f)
{
    uint8_t* b = f + BODY;
    float r = -1.0f;
    memset(f, 0, SIZE);
    memcpy(f, "RXIM", 4); put32(f + 4, 3); put32(f + 8, 60); put32(f + 12, TOFF); put32(f + 16, SOFF); put32(f + 20, 3); put32(f + 24, 2);
    memset(f + 28, 255, 5);
    put32(f + 60, CSZ);
    memcpy(f + 64, "MDL\xbc", 4); put16(f + 68, 16); put16(f + 70, 2); put32(f + 72, TL); put32(f + 76, OBJS);
    put32(b + TL, NAMES); put32(b + TL + 4, 1);
    put32(b + NAMES, NAME);
    memcpy(b + NAME, "imp00", 6);
    put32(b + OBJS + 4, 0xFFFFFFFFu); put32(b + OBJS + 44, OBJS + 0x34); put32(b + OBJS + 48, 0xFFFFFFFFu);
    put32(b + OBJS + 0x34 + 4, MODEL); put32(b + OBJS + 0x34 + 44, 0xFFFFFFFFu); put32(b + OBJS + 0x34 + 48, 0xFFFFFFFFu);
    put32(b + MODEL, VL); put32(b + MODEL + 4, PL); memcpy(b + MODEL + 20, &r, 4);
    b[VL] = 51; put16(b + VL + 2, (60 + 3 * 32) / 4); put16(b + VL + 6, 3);
    {   /* material, texture 0, one strip (Uvh) of the 3 vertices, the end */
        static const uint8_t pl[46] = { 0x13, 0x25, 4, 0, 1, 2, 3, 4, 5, 6, 7, 8, 0x08, 0x04, 0x00, 0x40,
            0x42, 0x12, 11, 0, 1, 0, 3, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0xFF, 0, 0, 0 };
        memcpy(b + PL, pl, sizeof(pl));
    }
    put32(f + 64 + CSZ, 0xFFFFFFFFu);
    {   /* the TIM2 chunk: header 0x100 (the picture header at 0x80), 8 bytes of pixels, a 16-entry CLUT */
        uint8_t* a = f + TOFF + 32;
        put32(f + TOFF, 0x324D4954u); put32(f + TOFF + 4, 0x100 + 8 + 64);
        put32(a + 0x84, 64); put32(a + 0x88, 8); a[0x93] = 4; put16(a + 0x94, 4); put16(a + 0x96, 4);
        put32(a + 0x100 + 8 + 64, 0xFFFFFFFFu);
    }
    for (int v = 0; v < 3; v++) { float w = 1; f[SOFF + 44 * v + 24] = 1; memcpy(f + SOFF + 44 * v + 28, &w, 4); }
}

static int cvxm_ok(const uint8_t* f, uint32_t n)
{
    PcCvxm cv;
    char why[96];
    int r = pc_cvxm_check(f, n, &cv, why, sizeof(why));
    if (getenv("PARSERS_VERBOSE")) printf("cvxm: %s\n", r ? "ok" : why);
    return r;
}

static void test_cvxm(void)
{
    uint8_t* f = malloc(SIZE), *b = f + BODY;
    PcCvxm cv;
    char why[96];
    mk_cvxm(f);
    CHECK(pc_cvxm_check(f, SIZE, &cv, why, sizeof(why)) && cv.vtx == BODY + VL + 64 && cv.skin == SOFF && cv.nv == 3);
    /* the review's case: the vertex list near the end of the buffer, its vertex count matching */
    mk_cvxm(f); put32(b + MODEL, 304); memcpy(b + 304, b + VL, 64); CHECK(!cvxm_ok(f, SIZE));
    mk_cvxm(f); put16(b + VL + 6, 4); put32(f + 20, 4); CHECK(!cvxm_ok(f, SIZE));   /* 4 vertices declared, 3 there */
    mk_cvxm(f); put32(f + 20, 2); CHECK(!cvxm_ok(f, SIZE));                         /* skin and mesh disagree */
    mk_cvxm(f); put32(b + OBJS + 0x34 + 4, 376); CHECK(!cvxm_ok(f, SIZE));  /* model past the MLB */
    mk_cvxm(f); put32(b + OBJS + 0x34 + 4, 0xFFFFFFF0u); CHECK(!cvxm_ok(f, SIZE));   /* (offset wraps) */
    mk_cvxm(f); put32(b + OBJS + 0x34 + 44, OBJS); CHECK(!cvxm_ok(f, SIZE));         /* a link back: a cycle */
    mk_cvxm(f); put32(b + OBJS + 44, OBJS + 0x34 + 1); CHECK(!cvxm_ok(f, SIZE));     /* a link into an object */
    mk_cvxm(f); put16(f + 70, 65); CHECK(!cvxm_ok(f, SIZE));                         /* too many objects */
    mk_cvxm(f); put32(f + 76, 332); CHECK(!cvxm_ok(f, SIZE));            /* object table past the MLB */
    mk_cvxm(f); put16(b + PL + 24, 3); CHECK(!cvxm_ok(f, SIZE));                     /* strip index 3 of 3 vertices */
    mk_cvxm(f); put16(b + PL + 22, 2); CHECK(!cvxm_ok(f, SIZE));                     /* a strip of 2 */
    mk_cvxm(f); put16(b + PL + 22, 0x7FFF); CHECK(!cvxm_ok(f, SIZE));                /* a strip past the MLB */
    mk_cvxm(f); b[PL + 42] = 0x38; CHECK(!cvxm_ok(f, SIZE));                         /* a polygon-list chunk (CoP3) */
    mk_cvxm(f); b[PL + 42] = 200; CHECK(!cvxm_ok(f, SIZE));                          /* past the handler table */
    mk_cvxm(f); put16(b + PL + 14, 0x4001); CHECK(!cvxm_ok(f, SIZE));                /* texture 1 of 1 */
    mk_cvxm(f); put32(b + TL + 4, 17); CHECK(!cvxm_ok(f, SIZE));                     /* 17 textures */
    mk_cvxm(f); put32(b + NAMES, CSZ - 16 - 3); memset(b + CSZ - 16 - 3, 'x', 3); CHECK(!cvxm_ok(f, SIZE));   /* a name without its end */
    mk_cvxm(f); { float r = 1; memcpy(b + MODEL + 20, &r, 4); } CHECK(!cvxm_ok(f, SIZE));   /* not to be relocated */
    mk_cvxm(f); put32(f + 60, SIZE); CHECK(!cvxm_ok(f, SIZE));                       /* chunk list past the file */
    mk_cvxm(f); put32(f + 60, 0xFFFFFFF0u); CHECK(!cvxm_ok(f, SIZE));
    mk_cvxm(f); put32(f + 16, SOFF + 4); CHECK(!cvxm_ok(f, SIZE));                  /* skin past the file */
    mk_cvxm(f); put32(f + 20, 0x40000000u); CHECK(!cvxm_ok(f, SIZE));                /* nv x 44 wraps in 32 bits */
    mk_cvxm(f); put32(f + TOFF + 4, SIZE); CHECK(!cvxm_ok(f, SIZE));                 /* TIM2 chunk past the file */
    mk_cvxm(f); f[TOFF + 32 + 0x93] = 3; CHECK(!cvxm_ok(f, SIZE));                   /* 32-bit: exit(0) in the loader */
    mk_cvxm(f); put16(f + TOFF + 32 + 0x94, 64); CHECK(!cvxm_ok(f, SIZE));           /* 64 x 4 pixels in 8 bytes */
    mk_cvxm(f); put32(f + TOFF, 0x00494C50u); CHECK(!cvxm_ok(f, SIZE));              /* a PLI chunk (palnum into pal[64]) */
    mk_cvxm(f); put32(f + 12, 100); CHECK(!cvxm_ok(f, SIZE));                        /* texture block inside the model */
    {   /* a second TIM2 for the one texture-list entry (bhSetMemPvpTexture writes an entry per picture) */
        uint8_t* g = calloc(1, SIZE + 400);
        mk_cvxm(g);
        memcpy(g + TOFF + 32 + 328, g + TOFF, 32 + 328);
        put32(g + TOFF + 2 * (32 + 328), 0xFFFFFFFFu);
        put32(g + 16, TOFF + 2 * (32 + 328) + 32);
        CHECK(!cvxm_ok(g, SIZE + 400));
        free(g);
    }
    for (uint32_t n = 0; n < SIZE; n += 7) { uint8_t* c = malloc(n + 1); mk_cvxm(f); memcpy(c, f, n); CHECK(!cvxm_ok(c, n)); free(c); }   /* cut short */
    free(f);
}

/* ---------------------------------------------------------------- HD textures */
static void test_dds(void)
{
    uint8_t d[148 + 64] = { 'D', 'D', 'S', ' ' };
    PcDds x;
    uint32_t w, h;
    put32(d + 12, 1); put32(d + 16, 1); put32(d + 28, 33);   /* 1 x 1 RGBA with 33 levels: shifts by 32 */
    put32(d + 80, 0x40); put32(d + 88, 32); put32(d + 92, 0xFF);
    CHECK(!pc_dds_parse(d, sizeof(d), 128 + 4 * 33, &x));
    put32(d + 28, 2); CHECK(!pc_dds_parse(d, sizeof(d), 260, &x));   /* (1 x 1 has one level) */
    put32(d + 28, 1); CHECK(pc_dds_parse(d, sizeof(d), 132, &x) && x.mips == 1 && pc_dds_level(&x, 0) == 4);
    CHECK(!pc_dds_parse(d, sizeof(d), 131, &x));                       /* level 0 cut short */
    put32(d + 12, 1024); put32(d + 16, 1024); put32(d + 28, 11);
    CHECK(pc_dds_parse(d, sizeof(d), 128 + (4u << 20) * 2, &x) && pc_dds_level(&x, 10) == 4);
    put32(d + 28, 12); CHECK(!pc_dds_parse(d, sizeof(d), 128 + (4u << 20) * 2, &x));
    put32(d + 28, 1); put32(d + 12, 16384); put32(d + 16, 16384);
    CHECK(!pc_dds_parse(d, sizeof(d), 128 + (1ull << 30), &x));        /* over PC_HD_MAX_DIM */
    put32(d + 12, 8192); put32(d + 16, 8192);
    CHECK(pc_dds_parse(d, sizeof(d), 128 + (256ull << 20), &x));       /* 8x the PS2's largest */
    put32(d + 12, 0); CHECK(!pc_dds_parse(d, sizeof(d), 1 << 20, &x));
    put32(d + 12, 4); put32(d + 16, 4); put32(d + 80, 4); memcpy(d + 84, "DX10", 4); put32(d + 128, 98);   /* BC7 */
    CHECK(pc_dds_parse(d, sizeof(d), 148 + 16, &x) && x.block == 16 && x.off == 148);
    CHECK(!pc_dds_parse(d, 140, 148 + 16, &x));                        /* the DX10 header not read */
    put32(d + 128, 2); CHECK(!pc_dds_parse(d, sizeof(d), 148 + 16, &x));   /* a format not used */
    {   /* PNG: IHDR */
        uint8_t p[24] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0, 0, 0, 13, 'I', 'H', 'D', 'R', 0, 0, 0x20, 0, 0, 0, 0x10, 0 };
        CHECK(pc_png_size(p, 24, &w, &h) && w == 8192 && h == 4096);
        CHECK(!pc_png_size(p, 23, &w, &h));
        p[1] = 'X'; CHECK(!pc_png_size(p, 24, &w, &h));
    }
}

/* ---------------------------------------------------------------- BioRand start kit */
enum { HED03 = 0x40, SCRIPT = 0x48 };
static void mk_room(uint8_t* d)
{
    static const uint8_t s[] = { 0x01, 14, 0x04, 0x01, 0x01, 0x00, 0x00, 0x01,   /* IfelCk 14: ev_flg bit 1 clear */
                                 0xC7, 145, 10, 0,                                  /* PlItemChangeM 145 -> 10 */
                                 0xC9, 10, 15, 0,                                   /* PlItemTamaSet 10, 15 */
                                 0x03, 0x00, 0x00 };                                /* Endif, End */
    memset(d, 0, 0x100);
    put32(d + 0x1C, HED03);
    put32(d + HED03, SCRIPT - HED03); put32(d + HED03 + 4, 0x80);
    memcpy(d + SCRIPT, s, sizeof(s));
}

/* ---------------------------------------------------------------- message tables
   The pickup question "Take the <item>?" (system message 157: 0xFF03 0xFFFF, the current item's name, then the
   question and its choices): whole, also its end found past the 0xFFFF operand; the same text cut at that operand
   (as the language import before the fix wrote it, the next message right behind) is not whole in its place. */
static void test_mes(void)
{
    static const uint16_t take[] = { 52, 0xFF00, 0xFF03, 0xFFFF, 31, 0xFF00, 0xFF04, 57, 0xFF04, 46, 0xFFFF };
    static const uint16_t cut[] = { 52, 0xFF00, 0xFF03, 0xFFFF, 57, 0xFFFF };   /* ("Take the <item>" cut, then "Y") */
    uint8_t t[64];
    uint16_t many[40];
    CHECK(pc_mes_len((const uint8_t*)take, 11) == 11);
    CHECK(pc_mes_len((const uint8_t*)take, 10) == 0);
    CHECK(pc_mes_len((const uint8_t*)cut, 4) == 0);
    for (int i = 0; i < 34; i++) many[i] = 0xFF04;
    many[34] = 0xFFFF;
    CHECK(pc_mes_len((const uint8_t*)many + 2 * 18, 17) == 17);   /* 16 choices */
    CHECK(pc_mes_len((const uint8_t*)many, 35) == 0);             /* 34: more than the game keeps */
    /* a table of 2: the whole question, then "Y" */
    put32(t, 2); put32(t + 4, 12); put32(t + 8, 12 + sizeof(take));
    memcpy(t + 12, take, sizeof(take)); memcpy(t + 12 + sizeof(take), cut + 4, 4);
    CHECK(pc_mes_at(t, 12 + sizeof(take) + 4, 0) == 11 && pc_mes_at(t, 12 + sizeof(take) + 4, 1) == 2);
    CHECK(pc_mes_at(t, 12 + sizeof(take) + 4, 2) == 0);
    /* the old import's: the question cut at the operand, "Y" right behind: 0 (not whole in its place) */
    put32(t + 8, 12 + 8); memcpy(t + 12, cut, sizeof(cut));
    CHECK(pc_mes_at(t, 12 + sizeof(cut), 0) == 0 && pc_mes_at(t, 12 + sizeof(cut), 1) == 2);
    CHECK(pc_mes_at(t, 12 + sizeof(cut) - 2, 1) == 0);           /* the table ends before its end */
    put32(t, 1000); CHECK(pc_mes_at(t, 12 + sizeof(cut), 0) == 0);   /* its count past the table */
    put32(t, 2); put32(t + 4, 13); CHECK(pc_mes_at(t, 12 + sizeof(cut), 0) == 0);   /* an odd offset */
}

static void test_startkit(void)
{
    uint8_t d[0x100];
    uint32_t at[PC_KIT_MAX];
    mk_room(d);
    CHECK(pc_startkit_scan(d, sizeof(d), at, PC_KIT_MAX, coop_insn_len_raw) == 2 && at[0] == SCRIPT + 8 && at[1] == SCRIPT + 12);
    mk_room(d); put32(d + 0x1C, 0xFFFFFFFCu); CHECK(pc_startkit_scan(d, sizeof(d), at, PC_KIT_MAX, coop_insn_len_raw) < 0);   /* hed03 + 8 wraps */
    mk_room(d); put32(d + HED03, 0xFFFFFFF0u); CHECK(pc_startkit_scan(d, sizeof(d), at, PC_KIT_MAX, coop_insn_len_raw) < 0);  /* scd0 past the file */
    mk_room(d); d[SCRIPT + 1] = 12; CHECK(pc_startkit_scan(d, sizeof(d), at, PC_KIT_MAX, coop_insn_len_raw) < 0);   /* the block ends inside PlItemTamaSet */
    mk_room(d); d[SCRIPT + 1] = 4; CHECK(pc_startkit_scan(d, sizeof(d), at, PC_KIT_MAX, coop_insn_len_raw) < 0);    /* ... inside its own header */
    mk_room(d); d[SCRIPT + 1] = 250; CHECK(pc_startkit_scan(d, sizeof(d), at, PC_KIT_MAX, coop_insn_len_raw) < 0);  /* past the script */
    mk_room(d); d[SCRIPT + 10] = 200; CHECK(pc_startkit_scan(d, sizeof(d), at, PC_KIT_MAX, coop_insn_len_raw) < 0); /* item 200 */
    mk_room(d); put16(d + SCRIPT + 14, 1000); CHECK(pc_startkit_scan(d, sizeof(d), at, PC_KIT_MAX, coop_insn_len_raw) < 0);   /* 1000 rounds */
    mk_room(d); d[SCRIPT + 9] = 0xFF; CHECK(pc_startkit_scan(d, sizeof(d), at, PC_KIT_MAX, coop_insn_len_raw) == 2);  /* the STANDARD slot form */
    mk_room(d); CHECK(pc_startkit_scan(d, sizeof(d), at, 1, coop_insn_len_raw) < 0);                                 /* more than max */
    mk_room(d); CHECK(pc_startkit_scan(d, SCRIPT + 12, at, PC_KIT_MAX, coop_insn_len_raw) < 0);                      /* the file cut in the block */
    mk_room(d); CHECK(pc_startkit_scan(d, 0x1F, at, PC_KIT_MAX, coop_insn_len_raw) < 0);
    mk_room(d); d[SCRIPT] = 230; CHECK(pc_startkit_scan(d, sizeof(d), at, PC_KIT_MAX, coop_insn_len_raw) == 0);      /* an op of unknown length ends the scan */
}

int main(void)
{
    test_prs();
    test_afs();
    test_cvxm();
    test_dds();
    test_startkit();
    test_mes();
    if (!fails) printf("parsers: all passed\n");
    return fails != 0;
}
