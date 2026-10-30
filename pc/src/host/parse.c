/* Checked reading of files (pc/include/pc_parse.h). Pure: no game state. */
#include "../../include/pc_parse.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- PRS */
static int32_t prs_decode(const uint8_t* s, uint32_t n, uint8_t* d, uint32_t cap, int lax)
{
    uint32_t i = 0, o = 0;
    unsigned field, left, b;
    if (n > INT32_MAX || cap > INT32_MAX) return PC_PRS_BROKEN;
#define NEED(k) do { if (n - i < (k)) return lax ? (int32_t)o : PC_PRS_BROKEN; } while (0)
    /* a descriptor bit; a new descriptor byte when they run out (Expand_GetDescriptorBit) */
#define BIT() (--left ? 0 : (field = s[i++], left = 8), b = field & 1, field >>= 1, b)
#define REFILL() do { if (left == 1) NEED(1); } while (0)
    NEED(1);
    field = s[i++];
    left = 9;   /* (Expand's extra count: the first byte's 8 bits) */
    for (;;) {
        uint32_t len, dist, w;
        REFILL();
        if (BIT()) {   /* a literal */
            NEED(1);
            if (o == cap) return PC_PRS_FULL;
            d[o++] = s[i++];
            continue;
        }
        REFILL();
        if (!BIT()) {   /* short copy: 2..5 bytes from up to 256 back */
            REFILL();
            len = BIT() ? 2 : 0;
            REFILL();
            len += BIT() + 1;
            NEED(1);
            dist = 0x100 - s[i++];
        } else {   /* long copy: 3..9 bytes, or 1..256 with a count byte, from up to 8192 back; 0: the end */
            NEED(2);
            w = s[i] | (uint32_t)s[i + 1] << 8;
            i += 2;
            if (!w) return (int32_t)o;
            dist = 0x2000 - (w >> 3);
            len = w & 7;
            if (len) len++;
            else { NEED(1); len = s[i++]; }
        }
        if (dist > o) return PC_PRS_BROKEN;   /* before the output's start */
        if (len + 1 > cap - o) return PC_PRS_FULL;
        for (uint32_t k = 0; k <= len; k++, o++) d[o] = d[o - dist];
    }
#undef NEED
#undef BIT
#undef REFILL
}
int32_t pc_prs_decode(const uint8_t* s, uint32_t n, uint8_t* d, uint32_t cap) { return prs_decode(s, n, d, cap, 0); }

long pc_prs_expand_ex(const unsigned char* s, long n, unsigned char** out, long* cap, int lax)
{
    if (n < 0 || n > INT32_MAX || *cap <= 0) return -1;
    for (;;) {
        int32_t r = prs_decode(s, (uint32_t)n, *out, (uint32_t)*cap, lax);
        unsigned char* p;
        if (r != PC_PRS_FULL) return r < 0 ? -1 : r;
        if ((unsigned long)*cap >= PC_PRS_MAX || !(p = realloc(*out, (size_t)*cap * 2))) return -1;
        *out = p;
        *cap *= 2;
    }
}
long pc_prs_expand(const unsigned char* s, long n, unsigned char** out, long* cap) { return pc_prs_expand_ex(s, n, out, cap, 0); }


/* ---------------------------------------------------------------- AFS */
int pc_afs_count(const uint8_t* h, uint64_t size)
{
    uint32_t n;
    if (size < 8 || memcmp(h, "AFS", 3)) return -1;
    n = pc_rd32(h + 4);
    return n <= PC_AFS_MAX && pc_range_ok(8, (uint64_t)n * 8, size) ? (int)n : -1;
}

int pc_afs_entry(const uint8_t* t, uint32_t i, uint64_t size, uint32_t* off, uint32_t* len)
{
    *off = pc_rd32(t + 8 + 8 * (size_t)i);
    *len = pc_rd32(t + 12 + 8 * (size_t)i);
    return pc_range_ok(*off, *len, size);
}

/* ---------------------------------------------------------------- .cvxm */
#define CVXM_MAGIC 0x4D495852u   /* "RXIM" (impmodel.h) */
#define CVXM_SKIN 44             /* sizeof(CvxmSkin) */
#define OBJ 0x34                 /* NJS_CNK_OBJECT: evalflags, model, pos, ang, scl, child (44), sibling (48) */
#define NONE 0xFFFFFFFFu

static int no(char* why, int wn, const char* fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    if (why && wn > 0) vsnprintf(why, (size_t)wn, fmt, a);
    va_end(a);
    return 0;
}

/* a model's polygon chunks (ps2_NinjaCnk.c pCnkFuncTbl: each handler's own length) up to the end
   chunk, in b[0, len): strips of the UV kinds (Uvn, Uvh: in batches of 60) with indices below nv,
   texture ids below ntex. Not used by the .cvxm writer and refused: vertex and polygon-list chunks,
   plain strips (a 200-vertex buffer), strips with user data, types past the table. */
static int plist_ok(const uint8_t* b, uint32_t len, uint32_t p, uint32_t nv, uint32_t ntex)
{
    static const uint8_t cm[7] = { 4, 4, 8, 4, 8, 8, 12 };   /* material chunks 17..23: D, A, DA, S, DS, AS, DAS */
    for (;;) {
        uint32_t t, sz, adv;
        if (!pc_range_ok(p, 2, len)) return 0;
        t = b[p];
        if (t == 255) return 1;
        if (t == 0) { p += 2; continue; }
        if (!pc_range_ok(p, 4, len)) return 0;
        sz = pc_rd16(b + p + 2);
        if (t <= 3 || t == 8) {
            if (t == 8 && (sz & 0xFFF) >= ntex) return 0;   /* njSetTextureNumSys: an entry of the texture list */
            adv = 4;
        } else if (t >= 17 && t <= 23) adv = 4 + cm[t - 17];
        else if (t <= 31 || (t >= 59 && t <= 63) || (t >= 67 && t <= 75)) adv = 4 + 2 * sz;
        else if (t == 65 || t == 66) {
            uint32_t q = p + 4, hd, ns;
            if (!pc_range_ok(q, 2, len)) return 0;
            hd = pc_rd16(b + q);
            ns = hd & 0x3FFF;
            if (hd >> 14) return 0;
            q += 2;
            while (ns--) {
                uint32_t n;
                if (!pc_range_ok(q, 2, len)) return 0;
                n = pc_rd16(b + q);
                if (n & 0x8000) n = 0x10000 - n;   /* (negative: the other winding) */
                q += 2;
                if (n < 3 || !pc_range_ok(q, 6 * n, len)) return 0;
                for (uint32_t k = 0; k < n; k++) if (pc_rd16(b + q + 6 * k) >= nv) return 0;
                q += 6 * n;
            }
            adv = q - p;
        } else return 0;
        if (!pc_range_ok(p, adv, len)) return 0;
        p += adv;
    }
}

int pc_cvxm_check(const uint8_t* f, uint32_t n, PcCvxm* out, char* why, int wn)
{
    uint32_t ver, toff, soff, nv, csz, hdr, on, len, oo, to, ntex = 0, ntim = 0, vtx = 0, q;
    const uint8_t *m, *b;
    memset(out, 0, sizeof(*out));
    if (n < 64 || pc_rd32(f) != CVXM_MAGIC) return no(why, wn, "not a .cvxm file");
    ver = pc_rd32(f + 4);
    if (ver < 1 || ver > 3 || pc_rd32(f + 8) != 60) return no(why, wn, "version %u is not known", ver);
    toff = pc_rd32(f + 12); soff = pc_rd32(f + 16); nv = pc_rd32(f + 20);
    /* the chunk list: one MLB, then the end */
    csz = pc_rd32(f + 60);
    if (csz < 16 || !pc_range_ok(64, (uint64_t)csz + 4, n) || pc_rd32(f + 64 + csz) != NONE) return no(why, wn, "bad chunk list");
    m = f + 64;
    hdr = pc_rd16(m + 4); on = pc_rd16(m + 6); to = pc_rd32(m + 8); oo = pc_rd32(m + 12);
    if (memcmp(m, "MDL", 3) || !(m[3] & 0x80) || hdr < 16 || hdr > csz || (64 + hdr) % 16) return no(why, wn, "not a chunk model");
    b = m + hdr;   /* what the MLB's offsets count from */
    len = csz - hdr;
    if (on < 2 || on > PC_CVXM_NODES || oo % 4 || !pc_range_ok(oo, (uint64_t)on * OBJ, len)) return no(why, wn, "bad object table (%u objects)", on);
    if (to != NONE) {   /* NJS_TEXLIST: textures, count; per texture its name's offset, attr, texaddr */
        uint32_t tt;
        if (to % 4 || !pc_range_ok(to, 8, len)) return no(why, wn, "bad texture list");
        tt = pc_rd32(b + to); ntex = pc_rd32(b + to + 4);
        if (ntex > PC_CVXM_TEX || tt % 4 || !pc_range_ok(tt, (uint64_t)ntex * 12, len)) return no(why, wn, "bad texture list");
        for (uint32_t k = 0; k < ntex; k++) {
            uint32_t fn = pc_rd32(b + tt + 12 * k);
            if (fn >= len || !memchr(b + fn, 0, len - fn)) return no(why, wn, "bad texture name");
        }
    }
    for (uint32_t i = 0; i < on; i++) {
        const uint8_t* o = b + oo + OBJ * i;
        uint32_t mo = pc_rd32(o + 4), link[2] = { pc_rd32(o + 44), pc_rd32(o + 48) };
        for (int k = 0; k < 2; k++)   /* child, sibling: a later object (no cycles) */
            if (link[k] != NONE && (link[k] < oo || (link[k] - oo) % OBJ || (link[k] - oo) / OBJ <= i || (link[k] - oo) / OBJ >= on))
                return no(why, wn, "object %u: bad link", i);
        if (mo == NONE) {
            if (i == 1) return no(why, wn, "no mesh on the pelvis");
            continue;
        }
        {   /* NJS_CNK_MODEL: vlist, plist, centre, r (negative: to be relocated) */
            uint32_t vl, pl, cnt = 0;
            float r;
            if (mo % 4 || !pc_range_ok(mo, 24, len)) return no(why, wn, "object %u: bad model", i);
            vl = pc_rd32(b + mo); pl = pc_rd32(b + mo + 4);
            memcpy(&r, b + mo + 20, 4);
            if (!(r < 0)) return no(why, wn, "object %u: bad model", i);
            if (vl != NONE) {   /* vertex chunk 51 (njCnkCvVnPs2): 64-byte header, then 32 bytes per vertex */
                const uint8_t* v;
                if ((hdr + vl) % 16 || !pc_range_ok(vl, 64, len)) return no(why, wn, "object %u: bad vertex chunk", i);
                v = b + vl;
                cnt = pc_rd16(v + 6);
                if (v[0] != 51 || pc_rd16(v + 4) || !cnt || 4 + 4 * (uint32_t)pc_rd16(v + 2) != 64 + 32 * cnt || !pc_range_ok(vl, 64 + 32 * cnt, len))
                    return no(why, wn, "object %u: bad vertex chunk", i);
                if (i == 1) vtx = 64 + hdr + vl + 64;
            }
            if (i == 1 && (vl == NONE || cnt != nv)) return no(why, wn, "the pelvis's mesh has not the %u vertices of the skin", nv);
            if (pl != NONE && !plist_ok(b, len, pl, cnt, ntex)) return no(why, wn, "object %u: bad polygon chunks", i);
        }
    }
    /* the texture block (ps2_texture.c bhSetMemPvpTexture): 32-byte chunk headers (code, size), TIM2
       pictures of 4/8 bits with a 32-bit CLUT (pixels at 0x100), one texture-list entry each, then -1 */
    if (toff < 64 + csz + 4) return no(why, wn, "bad texture block");
    for (q = toff;;) {
        uint32_t sz, a, it, w, h, img, clut, need;
        if (!pc_range_ok(q, 8, n)) return no(why, wn, "bad texture block");
        if (pc_rd32(f + q) == NONE) break;
        sz = pc_rd32(f + q + 4);
        if (pc_rd32(f + q) != 0x324D4954u || sz < 0x100 || !pc_range_ok((uint64_t)q + 32, sz, n)) return no(why, wn, "texture %u: bad chunk", ntim);
        a = q + 32;
        it = f[a + 0x93]; w = pc_rd16(f + a + 0x94); h = pc_rd16(f + a + 0x96);
        img = pc_rd32(f + a + 0x88); clut = pc_rd32(f + a + 0x84);
        need = (w * h * (it == 4 ? 4 : 8) + 7) / 8;
        if ((it != 4 && it != 5) || !w || !h || w > 1024 || h > 1024 || img < need || clut < (it == 4 ? 64u : 1024u) ||
            !pc_range_ok(0x100, (uint64_t)img + clut, sz))
            return no(why, wn, "texture %u: not a 4/8-bit picture of its size", ntim);
        if (++ntim > ntex) return no(why, wn, "%u textures for %u texture list entries", ntim, ntex);
        q = a + sz;
    }
    if (!nv || soff % 4 || !pc_range_ok(soff, (uint64_t)nv * CVXM_SKIN, n)) return no(why, wn, "bad skin array (%u vertices)", nv);
    out->vtx = vtx; out->skin = soff; out->nv = nv;
    return 1;
}

/* ---------------------------------------------------------------- HD textures */
int pc_dds_parse(const uint8_t* d, size_t avail, uint64_t n, PcDds* x)
{
    uint32_t fourcc, flags, legal = 1;
    memset(x, 0, sizeof(*x));
    if (avail < 128 || n < 128 || memcmp(d, "DDS ", 4)) return 0;
    memcpy(&x->h, d + 12, 4); memcpy(&x->w, d + 16, 4); memcpy(&x->mips, d + 28, 4); memcpy(&flags, d + 80, 4); memcpy(&fourcc, d + 84, 4);
    if (!x->mips) x->mips = 1;
    x->off = 128;
    if ((flags & 4) && !memcmp(&fourcc, "DX10", 4)) { if (avail < 148 || n < 148) return 0; memcpy(&x->dxgi, d + 128, 4); x->off = 148; }
    else if (flags & 4) x->dxgi = !memcmp(&fourcc, "DXT1", 4) ? 71 : !memcmp(&fourcc, "DXT3", 4) ? 74 : !memcmp(&fourcc, "DXT5", 4) ? 77 : 0;
    else if (flags & 0x40) { uint32_t bpp, rmask; memcpy(&bpp, d + 88, 4); memcpy(&rmask, d + 92, 4); if (bpp == 32) x->dxgi = rmask == 0xFF ? 28 : 87; }
    switch (x->dxgi) {
    case 71: case 72: x->block = 8; break;
    case 74: case 75: case 77: case 78: case 98: case 99: x->block = 16; break;
    case 28: case 29: break;
    case 87: case 88: case 91: x->bgra = 1; break;
    default: return 0;
    }
    if (!x->w || !x->h || x->w > PC_HD_MAX_DIM || x->h > PC_HD_MAX_DIM) return 0;
    while ((x->w | x->h) >> legal) legal++;   /* the full chain: 1 + log2 of the larger side */
    return x->mips <= legal && pc_range_ok(x->off, pc_dds_level(x, 0), n);
}

uint64_t pc_dds_level(const PcDds* x, uint32_t k)
{
    uint64_t w = k < 32 && x->w >> k ? x->w >> k : 1, h = k < 32 && x->h >> k ? x->h >> k : 1;
    return x->block ? ((w + 3) / 4) * ((h + 3) / 4) * x->block : w * h * 4;
}

int pc_png_size(const uint8_t* d, size_t n, uint32_t* w, uint32_t* h)
{
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    if (n < 24 || memcmp(d, sig, 8) || memcmp(d + 12, "IHDR", 4)) return 0;
    *w = (uint32_t)d[16] << 24 | d[17] << 16 | d[18] << 8 | d[19];
    *h = (uint32_t)d[20] << 24 | d[21] << 16 | d[22] << 8 | d[23];
    return 1;
}

/* ---------------------------------------------------------------- BioRand start kit */
/* the instruction at p, `avail` bytes left: its length, 0 an unknown op, -1 operands past the end
   (raw: reads p[0], and p[1] of the controller ops 100..105) */
static int insn_len(int (*raw)(const uint8_t*), const uint8_t* p, uint32_t avail)
{
    int len;
    if (!avail || (avail < 2 && p[0] >= 100 && p[0] <= 105)) return -1;
    len = raw(p);
    return len <= 0 ? 0 : (uint32_t)len <= avail ? len : -1;
}

int pc_startkit_scan(const uint8_t* d, uint32_t n, uint32_t* at, int max, int (*insn)(const uint8_t*))
{
    static const uint8_t ck[6] = { 0x04, 0x01, 0x01, 0x00, 0x00, 0x01 };   /* Ck ev_flg bit 1 is clear */
    uint32_t hed03, scd0, scd1, o, end;
    int k = 0, len;
    if (n < 0x20) return -1;
    hed03 = pc_rd32(d + 0x1C);   /* RDT_WORK hed03: the room scripts' table (scd0: the entry script) */
    if (!pc_range_ok(hed03, 8, n)) return -1;
    scd0 = pc_rd32(d + hed03); scd1 = pc_rd32(d + hed03 + 4);
    if (scd0 > n - hed03) return -1;
    o = hed03 + scd0;
    end = scd1 > scd0 && scd1 <= n - hed03 ? hed03 + scd1 : n;
    while (end - o >= 8 && d[o]) {   /* to its End (as before: an op of unknown length ends the scan) */
        if ((len = insn_len(insn, d + o, end - o)) <= 0) return len ? -1 : k;
        if (d[o] == 0x01 && !memcmp(d + o + 2, ck, 6)) {   /* IfelCk n: the block to o + 2 + n */
            uint32_t p = o + 8, stop = o + 2 + d[o + 1];
            if (stop < p || stop > end) return -1;
            while (p < stop) {
                const uint8_t* a = d + p;
                if ((len = insn_len(insn, a, stop - p)) <= 0) return -1;
                if (a[0] == 0xC7 || a[0] == 0xC9) {   /* PlItemChangeM old new [count], PlItemTamaSet item u16 count (event.c) */
                    if (a[0] == 0xC7 ? (a[1] >= 156 && a[1] != 0xFF) || a[2] >= 156 : a[1] >= 156 || pc_rd16(a + 2) > 999) return -1;
                    if (k == max) return -1;
                    at[k++] = p;
                }
                p += (uint32_t)len;
            }
            len = (int)(stop - o);   /* past the block */
        }
        o += (uint32_t)len;
    }
    return k;
}
