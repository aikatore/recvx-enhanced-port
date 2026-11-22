/* Dreamcast GD-ROM images (docs/IMPORT_DREAMCAST.md "Disc images"): the user data of a sector by its disc address
   (LBA; the high-density area starts at 45000) from
   - a CUE sheet with one BIN file per track (Redump): a file's first sector is the next free address of its area
     (0, or 45000 after "REM HIGH-DENSITY AREA"), MODE1/2352, MODE2/2352 or MODE1/2048 sectors,
   - a GDI sheet (TOSEC, Dreamcast emulators): each track's address, type, sector size and file,
   - a MAME CHD (version 5, chdman createcd; TOSEC's .CD1 files are such images renamed): its hunk map (Huffman coded),
     the CD codecs cdlz (LZMA) and cdzl (deflate) of the sector data, the track list of its GD-ROM metadata; the
     subcode and the ECC are not needed and not decoded. cdfl (FLAC) is chosen for audio tracks only and refused.
   Our own reading of the formats MAME documents in its sources (chd.cpp, cdrom.cpp, chdcodec.cpp); the LZMA and
   deflate decoders below are written from the published formats (the LZMA SDK's specification, RFC 1951).
   Then ISO 9660 in the high-density area (sector 45016): files by path. */
#include "import.h"
#include "../host/fileio.h"
#include "../../include/pc_parse.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#define GD_HD 45000u
#define GD_TRACKS 99
#define CD_FRAME 2352u
#define CHD_FRAME 2448u       /* a CD frame in a CHD: the sector and 96 bytes of subcode */

typedef struct GdTrack {
    uint32_t lba, n;          /* first address, sectors */
    int data;                 /* a data track */
    int file;                 /* CUE / GDI: its file (files[]); CHD: -1 */
    int64_t off;              /* CUE / GDI: byte offset of its first sector in the file; CHD: its first frame */
    uint32_t ssize, user;     /* bytes per sector, offset of the user data in it */
} GdTrack;

struct GdDisc {
    int ntrack;
    GdTrack t[GD_TRACKS];
    int nfile;
    FILE* f[GD_TRACKS];
    /* CHD */
    FILE* chd;
    uint32_t hunkbytes, nhunk;
    uint32_t comp[4];
    uint8_t* map;             /* per hunk: type, length (24 bits), offset (48 bits) */
    uint32_t cached;          /* the hunk in hunk (+1; 0 none) */
    uint8_t *hunk, *src, *sec;  /* the decoded hunk, its compressed bytes, its sectors without subcode */
};

static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint64_t be48(const uint8_t* p) { return (uint64_t)be32(p) << 16 | (uint32_t)(p[4] << 8 | p[5]); }
static uint64_t be64(const uint8_t* p) { return (uint64_t)be32(p) << 32 | be32(p + 4); }

/* ---------------------------------------------------------------- LZMA (raw stream, known output size) */
typedef struct { const uint8_t *in, *end; uint32_t range, code; int bad; } Rc;
static uint8_t rc_in(Rc* r) { if (r->in < r->end) return *r->in++; r->bad = 1; return 0; }
static void rc_norm(Rc* r) { if (r->range < (1u << 24)) { r->range <<= 8; r->code = r->code << 8 | rc_in(r); } }
static int rc_bit(Rc* r, uint16_t* p)
{
    uint32_t bound = (r->range >> 11) * *p;
    int b;
    if (r->code < bound) { *p += (2048 - *p) >> 5; r->range = bound; b = 0; }
    else { *p -= *p >> 5; r->code -= bound; r->range -= bound; b = 1; }
    rc_norm(r);
    return b;
}
static uint32_t rc_tree(Rc* r, uint16_t* p, int bits)
{
    uint32_t m = 1;
    for (int i = 0; i < bits; i++) m = m << 1 | (uint32_t)rc_bit(r, &p[m]);
    return m - (1u << bits);
}
static uint32_t rc_rtree(Rc* r, uint16_t* p, int bits)
{
    uint32_t m = 1, s = 0;
    for (int i = 0; i < bits; i++) { int b = rc_bit(r, &p[m]); m = m << 1 | (uint32_t)b; s |= (uint32_t)b << i; }
    return s;
}
typedef struct { uint16_t choice, choice2, low[16][8], mid[16][8], high[256]; } LzLen;
static uint32_t lz_len(Rc* r, LzLen* l, int ps)
{
    if (!rc_bit(r, &l->choice)) return rc_tree(r, l->low[ps], 3);
    if (!rc_bit(r, &l->choice2)) return 8 + rc_tree(r, l->mid[ps], 3);
    return 16 + rc_tree(r, l->high, 8);
}
typedef struct {
    uint16_t match[12 << 4], rep[12], g0[12], g1[12], g2[12], rep0long[12 << 4], slot[4][64], pos[115], align[16];
    LzLen len, replen;
    uint16_t lit[0x300 << 3];    /* lc 3, lp 0 */
} LzProbs;

/* lc 3, lp 0, pb 2 (chdman's level 9 settings); 1 if exactly outn bytes were decoded */
int gd_lzma(const uint8_t* in, size_t inn, uint8_t* out, size_t outn)
{
    LzProbs pr;                  /* (14 KB on the import thread's stack) */
    uint16_t* all = (uint16_t*)&pr;
    Rc r = { in, in + inn, 0xFFFFFFFFu, 0, 0 };
    uint32_t rep0 = 0, rep1 = 0, rep2 = 0, rep3 = 0, state = 0;
    size_t o = 0;
    for (size_t i = 0; i < sizeof(pr) / 2; i++) all[i] = 1024;
    if (rc_in(&r) != 0) return 0;
    for (int i = 0; i < 4; i++) r.code = r.code << 8 | rc_in(&r);
    while (o < outn && !r.bad) {
        int ps = (int)(o & 3);
        if (!rc_bit(&r, &pr.match[state << 4 | ps])) {   /* a literal */
            uint8_t prev = o ? out[o - 1] : 0;
            uint16_t* p = &pr.lit[0x300 * (prev >> 5)];
            uint32_t s = 1;
            if (state >= 7) {
                if (rep0 >= o) return 0;
                uint32_t mb = out[o - rep0 - 1];
                do {
                    uint32_t bit = (mb >> 7) & 1, b;
                    mb <<= 1;
                    b = (uint32_t)rc_bit(&r, &p[((1 + bit) << 8) + s]);
                    s = s << 1 | b;
                    if (b != bit) break;
                } while (s < 0x100);
            }
            while (s < 0x100) s = s << 1 | (uint32_t)rc_bit(&r, &p[s]);
            out[o++] = (uint8_t)s;
            state = state < 4 ? 0 : state < 10 ? state - 3 : state - 6;
            continue;
        }
        uint32_t len;
        if (rc_bit(&r, &pr.rep[state])) {
            if (!o) return 0;
            if (!rc_bit(&r, &pr.g0[state])) {
                if (!rc_bit(&r, &pr.rep0long[state << 4 | ps])) {   /* a short rep: one byte */
                    state = state < 7 ? 9 : 11;
                    if (rep0 >= o) return 0;
                    out[o] = out[o - rep0 - 1];
                    o++;
                    continue;
                }
            } else {
                uint32_t d;
                if (!rc_bit(&r, &pr.g1[state])) d = rep1;
                else {
                    if (!rc_bit(&r, &pr.g2[state])) d = rep2;
                    else { d = rep3; rep3 = rep2; }
                    rep2 = rep1;
                }
                rep1 = rep0; rep0 = d;
            }
            len = lz_len(&r, &pr.replen, ps);
            state = state < 7 ? 8 : 11;
        } else {
            rep3 = rep2; rep2 = rep1; rep1 = rep0;
            len = lz_len(&r, &pr.len, ps);
            state = state < 7 ? 7 : 10;
            uint32_t slot = rc_tree(&r, pr.slot[len < 3 ? len : 3], 6);
            if (slot < 4) rep0 = slot;
            else {
                int nd = (int)(slot >> 1) - 1;
                rep0 = (2 | (slot & 1)) << nd;
                if (slot < 14) rep0 += rc_rtree(&r, pr.pos + rep0 - slot, nd);
                else {
                    uint32_t v = 0;
                    for (int i = 0; i < nd - 4; i++) {
                        r.range >>= 1;
                        r.code -= r.range;
                        uint32_t t = 0 - (r.code >> 31);
                        r.code += r.range & t;
                        rc_norm(&r);
                        v = v << 1 | (t + 1);
                    }
                    rep0 += v << 4;
                    rep0 += rc_rtree(&r, pr.align, 4);
                }
                if (rep0 == 0xFFFFFFFFu) break;   /* the end mark */
            }
            if (rep0 >= o) return 0;
        }
        len += 2;
        for (; len && o < outn; len--, o++) out[o] = out[o - rep0 - 1];
    }
    return o == outn && !r.bad;
}

/* ---------------------------------------------------------------- deflate (raw, known output size) */
typedef struct { const uint8_t* in; size_t n, p; uint32_t buf; int cnt, bad; } Inf;
typedef struct { short count[16], sym[288]; } Huff;
static int inf_bits(Inf* s, int need)
{
    uint32_t v = s->buf;
    while (s->cnt < need) {
        if (s->p >= s->n) { s->bad = 1; return 0; }
        v |= (uint32_t)s->in[s->p++] << s->cnt;
        s->cnt += 8;
    }
    s->buf = v >> need;
    s->cnt -= need;
    return (int)(v & ((1u << need) - 1));
}
static int inf_decode(Inf* s, const Huff* h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        code |= inf_bits(s, 1);
        int count = h->count[len];
        if (code - count < first) return h->sym[index + code - first];
        index += count; first += count; first <<= 1; code <<= 1;
        if (s->bad) break;
    }
    s->bad = 1;
    return 0;
}
static int inf_build(Huff* h, const short* len, int n)
{
    short offs[16];
    int left = 1;
    memset(h->count, 0, sizeof(h->count));
    for (int i = 0; i < n; i++) h->count[len[i]]++;
    if (h->count[0] == n) return 0;
    for (int l = 1; l < 16; l++) { left <<= 1; left -= h->count[l]; if (left < 0) return -1; }
    offs[1] = 0;
    for (int l = 1; l < 15; l++) offs[l + 1] = (short)(offs[l] + h->count[l]);
    for (int i = 0; i < n; i++) if (len[i]) h->sym[offs[len[i]]++] = (short)i;
    return left;
}
int gd_inflate(const uint8_t* in, size_t inn, uint8_t* out, size_t outn)
{
    static const short lbase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
    static const short lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
    static const short dbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
    static const short dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
    static const unsigned char order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    Inf s = { in, inn, 0, 0, 0, 0 };
    Huff lh, dh;
    size_t o = 0;
    int last;
    do {
        last = inf_bits(&s, 1);
        int type = inf_bits(&s, 2);
        if (type == 0) {   /* stored */
            s.buf = 0; s.cnt = 0;
            if (s.p + 4 > s.n) return 0;
            unsigned len = s.in[s.p] | s.in[s.p + 1] << 8;
            if ((len ^ 0xFFFF) != (unsigned)(s.in[s.p + 2] | s.in[s.p + 3] << 8)) return 0;
            s.p += 4;
            if (s.p + len > s.n || o + len > outn) return 0;
            memcpy(out + o, s.in + s.p, len);
            s.p += len; o += len;
            continue;
        }
        short ln[320];
        if (type == 1) {
            int i = 0;
            for (; i < 144; i++) ln[i] = 8;
            for (; i < 256; i++) ln[i] = 9;
            for (; i < 280; i++) ln[i] = 7;
            for (; i < 288; i++) ln[i] = 8;
            inf_build(&lh, ln, 288);
            for (i = 0; i < 30; i++) ln[i] = 5;
            inf_build(&dh, ln, 30);
        } else if (type == 2) {
            int nlen = inf_bits(&s, 5) + 257, ndist = inf_bits(&s, 5) + 1, ncode = inf_bits(&s, 4) + 4, i;
            if (nlen > 286 || ndist > 30) return 0;
            for (i = 0; i < 19; i++) ln[order[i]] = (short)(i < ncode ? inf_bits(&s, 3) : 0);
            if (inf_build(&lh, ln, 19) != 0) return 0;
            for (i = 0; i < nlen + ndist && !s.bad;) {
                int sym = inf_decode(&s, &lh), rep = 0;
                short v = 0;
                if (sym < 16) { ln[i++] = (short)sym; continue; }
                if (sym == 16) { if (!i) return 0; v = ln[i - 1]; rep = 3 + inf_bits(&s, 2); }
                else if (sym == 17) rep = 3 + inf_bits(&s, 3);
                else rep = 11 + inf_bits(&s, 7);
                if (i + rep > nlen + ndist) return 0;
                while (rep--) ln[i++] = v;
            }
            if (s.bad || ln[256] == 0) return 0;
            if (inf_build(&lh, ln, nlen) < 0 || (inf_build(&lh, ln, nlen) > 0 && nlen - lh.count[0] != 1)) return 0;
            if (inf_build(&dh, ln + nlen, ndist) < 0) return 0;
        } else return 0;
        for (;;) {   /* the codes */
            int sym = inf_decode(&s, &lh);
            if (s.bad) return 0;
            if (sym < 256) { if (o >= outn) return 0; out[o++] = (uint8_t)sym; continue; }
            if (sym == 256) break;
            sym -= 257;
            if (sym >= 29) return 0;
            int len = lbase[sym] + inf_bits(&s, lext[sym]);
            int ds = inf_decode(&s, &dh);
            if (ds >= 30 || s.bad) return 0;
            size_t dist = (size_t)(dbase[ds] + inf_bits(&s, dext[ds]));
            if (dist > o || o + (size_t)len > outn) return 0;
            for (; len; len--, o++) out[o] = out[o - dist];
        }
    } while (!last && !s.bad);
    return o == outn && !s.bad;
}

/* ---------------------------------------------------------------- CHD */
typedef struct { const uint8_t* b; size_t n, p; } Bits;
static uint32_t bits_rd(Bits* s, int n)
{
    uint32_t v = 0;
    for (int i = 0; i < n; i++, s->p++) v = v << 1 | (s->p >> 3 < s->n ? (s->b[s->p >> 3] >> (7 - (s->p & 7))) & 1 : 0);
    return v;
}
typedef struct { uint8_t len[16]; uint32_t code[16]; } MapHuff;   /* 16 codes, at most 8 bits (MAME's map coder) */
static int maphuff_read(MapHuff* h, Bits* s)
{
    int n = 0;
    while (n < 16) {
        uint32_t v = bits_rd(s, 4);
        if (v != 1) h->len[n++] = (uint8_t)v;
        else if ((v = bits_rd(s, 4)) == 1) h->len[n++] = 1;
        else {
            uint32_t rep = bits_rd(s, 4) + 3;
            if (n + rep > 16) return 0;
            while (rep--) h->len[n++] = (uint8_t)v;
        }
    }
    uint32_t hist[33] = { 0 }, start[33] = { 0 }, cur = 0;
    for (int i = 0; i < 16; i++) { if (h->len[i] > 8) return 0; hist[h->len[i]]++; }
    for (int l = 32; l > 0; l--) {   /* MAME's canonical order: the longest codes first */
        uint32_t next = (cur + hist[l]) >> 1;
        if (l != 1 && next * 2 != cur + hist[l]) return 0;
        start[l] = cur;
        cur = next;
    }
    for (int i = 0; i < 16; i++) if (h->len[i]) h->code[i] = start[h->len[i]]++;
    return 1;
}
static int maphuff_get(const MapHuff* h, Bits* s)
{
    uint32_t v = 0;
    for (int l = 1; l <= 8; l++) {
        v = v << 1 | bits_rd(s, 1);
        for (int i = 0; i < 16; i++) if (h->len[i] == l && h->code[i] == v) return i;
    }
    return -1;
}

static int chd_open(GdDisc* d, FILE* f, char* err, int en)
{
    uint8_t h[124];
    if (fread(h, 1, sizeof(h), f) != sizeof(h) || memcmp(h, "MComprHD", 8) || be32(h + 12) != 5) {
        snprintf(err, en, "only CHD version 5 images are read");
        return 0;
    }
    for (int i = 0; i < 4; i++) d->comp[i] = be32(h + 16 + 4 * i);
    uint64_t logical = be64(h + 32), mapoff = be64(h + 40), meta = be64(h + 48);
    d->hunkbytes = be32(h + 56);
    for (int i = 0; i < 20; i++) if (h[104 + i]) { snprintf(err, en, "a CHD with a parent image is not read"); return 0; }
    if (be32(h + 60) != CHD_FRAME || !d->hunkbytes || d->hunkbytes % CHD_FRAME || d->hunkbytes > (1u << 20) || logical / d->hunkbytes > (1u << 24)) {
        snprintf(err, en, "not a CD image (CHD)");
        return 0;
    }
    d->nhunk = (uint32_t)((logical + d->hunkbytes - 1) / d->hunkbytes);
    d->chd = f;
    if (!(d->map = malloc((size_t)d->nhunk * 12)) || !(d->hunk = malloc(d->hunkbytes)) || !(d->src = malloc(d->hunkbytes)) || !(d->sec = malloc(d->hunkbytes))) return 0;
    if (!d->comp[0]) {   /* uncompressed: a u32 per hunk, its offset / hunkbytes */
        uint8_t* raw = malloc((size_t)d->nhunk * 4);
        if (!raw || fseeko(f, (off_t)mapoff, SEEK_SET) || fread(raw, 4, d->nhunk, f) != d->nhunk) { free(raw); return 0; }
        for (uint32_t i = 0; i < d->nhunk; i++) {
            uint8_t* m = d->map + 12 * i;
            uint64_t o = (uint64_t)be32(raw + 4 * i) * d->hunkbytes;
            m[0] = o ? 4 : 15;   /* (15: zeros) */
            m[1] = (uint8_t)(d->hunkbytes >> 16); m[2] = (uint8_t)(d->hunkbytes >> 8); m[3] = (uint8_t)d->hunkbytes;
            for (int k = 0; k < 6; k++) m[4 + k] = (uint8_t)(o >> (40 - 8 * k));
        }
        free(raw);
    } else {
        uint8_t mh[16];
        if (fseeko(f, (off_t)mapoff, SEEK_SET) || fread(mh, 1, 16, f) != 16) return 0;
        uint32_t mapbytes = be32(mh);
        uint64_t cur = be48(mh + 4);
        int lb = mh[12], sb = mh[13];
        uint8_t* cm = mapbytes < (64u << 20) ? malloc(mapbytes ? mapbytes : 1) : NULL;
        if (!cm || fread(cm, 1, mapbytes, f) != mapbytes) { free(cm); snprintf(err, en, "the CHD's hunk map cannot be read"); return 0; }
        Bits s = { cm, mapbytes, 0 };
        MapHuff hf;
        int ok = maphuff_read(&hf, &s), last = 0;
        uint32_t rep = 0;
        for (uint32_t i = 0; i < d->nhunk && ok; i++) {   /* the types: run-length coded */
            int v;
            if (rep) { d->map[12 * i] = (uint8_t)last; rep--; continue; }
            if ((v = maphuff_get(&hf, &s)) < 0) { ok = 0; break; }
            if (v == 7) { d->map[12 * i] = (uint8_t)last; rep = 2 + (uint32_t)maphuff_get(&hf, &s); }
            else if (v == 8) { d->map[12 * i] = (uint8_t)last; rep = 2 + 16 + ((uint32_t)maphuff_get(&hf, &s) << 4); rep += (uint32_t)maphuff_get(&hf, &s); }
            else d->map[12 * i] = (uint8_t)(last = v);
        }
        uint64_t lastself = 0;
        for (uint32_t i = 0; i < d->nhunk && ok; i++) {   /* lengths and offsets */
            uint8_t* m = d->map + 12 * i;
            uint64_t o = cur;
            uint32_t len = 0;
            switch (m[0]) {
            case 0: case 1: case 2: case 3: len = bits_rd(&s, lb); cur += len; bits_rd(&s, 16); break;
            case 4: len = d->hunkbytes; cur += len; bits_rd(&s, 16); break;
            case 5: o = lastself = bits_rd(&s, sb); break;
            case 10: lastself++; /* fall through */
            case 9: m[0] = 5; o = lastself; break;
            default: ok = 0; snprintf(err, en, "a CHD with a parent image is not read"); break;   /* (6, 11..13: the parent's) */
            }
            m[1] = (uint8_t)(len >> 16); m[2] = (uint8_t)(len >> 8); m[3] = (uint8_t)len;
            for (int k = 0; k < 6; k++) m[4 + k] = (uint8_t)(o >> (40 - 8 * k));
        }
        free(cm);
        if (!ok) { if (!err[0]) snprintf(err, en, "the CHD's hunk map is damaged"); return 0; }
    }
    /* the tracks (GD-ROM: CHGD; CD: CHT2, CHTR): consecutive in the image, each padded to 4 frames; on a GD-ROM track 3
       starts the high-density area. A pregap stored with the track (PGTYPE V...) comes first. */
    uint32_t lba = 0, frame = 0;
    int gd = 0;
    while (meta && d->ntrack < GD_TRACKS) {
        uint8_t mh[16];
        char txt[256], type[32] = "", pgtype[32] = "";
        int tk = 0, frames = 0, pregap = 0, pad = 0, postgap = 0;
        if (fseeko(f, (off_t)meta, SEEK_SET) || fread(mh, 1, 16, f) != 16) break;
        uint32_t len = (uint32_t)mh[5] << 16 | mh[6] << 8 | mh[7];
        meta = be64(mh + 8);
        if (memcmp(mh, "CHGD", 4) && memcmp(mh, "CHT2", 4) && memcmp(mh, "CHTR", 4)) continue;
        if (len >= sizeof(txt) || fread(txt, 1, len, f) != len) break;
        txt[len] = 0;
        gd |= !memcmp(mh, "CHGD", 4);
        if (!memcmp(mh, "CHGD", 4)) sscanf(txt, "TRACK:%d TYPE:%31s SUBTYPE:%*s FRAMES:%d PAD:%d PREGAP:%d PGTYPE:%31s PGSUB:%*s POSTGAP:%d", &tk, type, &frames, &pad, &pregap, pgtype, &postgap);
        else if (!memcmp(mh, "CHT2", 4)) sscanf(txt, "TRACK:%d TYPE:%31s SUBTYPE:%*s FRAMES:%d PREGAP:%d PGTYPE:%31s PGSUB:%*s POSTGAP:%d", &tk, type, &frames, &pregap, pgtype, &postgap);
        else sscanf(txt, "TRACK:%d TYPE:%31s SUBTYPE:%*s FRAMES:%d", &tk, type, &frames);
        if (frames <= 0) continue;
        GdTrack* t = &d->t[d->ntrack++];
        if (gd && tk == 3 && lba < GD_HD) lba = GD_HD;
        int skip = pgtype[0] == 'V' && pregap > 0 && pregap < frames ? pregap : 0;
        t->lba = lba + (uint32_t)skip; t->n = (uint32_t)(frames - skip);
        t->file = -1; t->off = frame + skip;
        t->data = strcmp(type, "AUDIO") != 0;
        t->ssize = CD_FRAME;
        t->user = !strncmp(type, "MODE2", 5) ? 24 : !strcmp(type, "MODE1") ? 0 : 16;   /* (MODE1: 2048-byte sectors) */
        if (!strcmp(type, "MODE1")) t->ssize = 2048;
        lba += (uint32_t)frames;
        frame += ((uint32_t)frames + 3) & ~3u;
    }
    if (!d->ntrack) { snprintf(err, en, "the CHD has no CD track list"); return 0; }
    return 1;
}

/* hunk h decoded into d->hunk (frames of 2448 bytes; the subcode left as zeros) */
static int chd_hunk(GdDisc* d, uint32_t h)
{
    for (int guard = 0; guard < 8; guard++) {
        if (h >= d->nhunk) return 0;
        const uint8_t* m = d->map + 12 * h;
        if (m[0] != 5) break;
        h = (uint32_t)be48(m + 4);   /* a copy of an earlier hunk */
    }
    if (d->cached == h + 1) return 1;
    d->cached = 0;
    const uint8_t* m = d->map + 12 * h;
    uint32_t len = (uint32_t)m[1] << 16 | m[2] << 8 | m[3], frames = d->hunkbytes / CHD_FRAME;
    uint64_t off = be48(m + 4);
    if (m[0] == 15) { memset(d->hunk, 0, d->hunkbytes); d->cached = h + 1; return 1; }
    if (m[0] == 5 || len > d->hunkbytes || fseeko(d->chd, (off_t)off, SEEK_SET) || fread(d->src, 1, len, d->chd) != len) return 0;
    if (m[0] == 4) memcpy(d->hunk, d->src, len);
    else {
        uint32_t c = d->comp[m[0] & 3];
        uint32_t eb = (frames + 7) / 8, cb = d->hunkbytes < 65536 ? 2 : 3, base;
        uint8_t* sec = d->sec;
        if (c != 0x63646C7Au && c != 0x63647A6Cu) return 0;   /* "cdlz", "cdzl" ("cdfl": audio) */
        if (len < eb + cb) return 0;
        base = (uint32_t)d->src[eb] << 8 | d->src[eb + 1];
        if (cb == 3) base = base << 8 | d->src[eb + 2];
        if (base > len - eb - cb) return 0;
        if (!(c == 0x63646C7Au ? gd_lzma(d->src + eb + cb, base, sec, (size_t)frames * CD_FRAME)
                               : gd_inflate(d->src + eb + cb, base, sec, (size_t)frames * CD_FRAME))) return 0;
        memset(d->hunk, 0, d->hunkbytes);
        for (uint32_t k = 0; k < frames; k++) memcpy(d->hunk + k * CHD_FRAME, sec + k * CD_FRAME, CD_FRAME);
    }
    d->cached = h + 1;
    return 1;
}

/* ---------------------------------------------------------------- CUE, GDI */
static int add_file(GdDisc* d, const char* sheet, const char* name)
{
    char p[1400];
    const char* s = strrchr(sheet, '/');
#ifdef _WIN32
    const char* b = strrchr(sheet, '\\');
    if (b && (!s || b > s)) s = b;
#endif
    if (d->nfile == GD_TRACKS) return -1;
    if (s) snprintf(p, sizeof(p), "%.*s/%s", (int)(s - sheet), sheet, name);
    else snprintf(p, sizeof(p), "%s", name);
    if (!(d->f[d->nfile] = pc_fopen(p, "rb"))) return -1;
    return d->nfile++;
}
static int64_t file_size(FILE* f) { return fseeko(f, 0, SEEK_END) ? -1 : (int64_t)ftello(f); }

static int cue_open(GdDisc* d, const char* path, FILE* f, char* err, int en)
{
    char line[1200];
    uint32_t lba = 0, flba = 0;
    int file = -1;
    while (fgets(line, sizeof(line), f) && d->ntrack < GD_TRACKS) {
        char* w = line;
        while (isspace((unsigned char)*w)) w++;
        if (!strncmp(w, "REM", 3) && strstr(w, "HIGH-DENSITY")) lba = GD_HD;
        else if (!strncmp(w, "FILE", 4)) {
            char *a = strchr(w, '"'), *b = a ? strrchr(a + 1, '"') : NULL;
            if (!a || !b) { snprintf(err, en, "a FILE line without a quoted name"); return 0; }
            *b = 0;
            if ((file = add_file(d, path, a + 1)) < 0) { snprintf(err, en, "cannot open %s", a + 1); return 0; }
            int64_t n = file_size(d->f[file]);
            if (n < 0) return 0;
            flba = lba;
            lba += (uint32_t)(n / CD_FRAME);   /* (the next file's first address; 2048-byte tracks: below) */
        } else if (!strncmp(w, "TRACK", 5) && file >= 0) {
            char mode[32] = "";
            sscanf(w, "TRACK %*d %31s", mode);
            GdTrack* t = &d->t[d->ntrack++];
            int64_t n = file_size(d->f[file]);
            t->file = file; t->off = 0; t->lba = flba;
            t->data = strcmp(mode, "AUDIO") != 0;
            t->ssize = !strcmp(mode, "MODE1/2048") ? 2048 : CD_FRAME;
            t->user = !strncmp(mode, "MODE2", 5) ? 24 : t->ssize == 2048 ? 0 : 16;
            t->n = (uint32_t)(n / t->ssize);
            if (t->ssize == 2048) lba = flba + t->n;
        }
    }
    if (!d->ntrack) { snprintf(err, en, "the CUE sheet lists no track"); return 0; }
    return 1;
}

static int gdi_open(GdDisc* d, const char* path, FILE* f, char* err, int en)
{
    char line[1200];
    int n = 0;
    if (!fgets(line, sizeof(line), f) || (n = atoi(line)) <= 0 || n > GD_TRACKS) { snprintf(err, en, "not a GDI sheet"); return 0; }
    while (d->ntrack < n && fgets(line, sizeof(line), f)) {
        unsigned tk, lba, type, ss;
        int used = 0;
        char name[1100];
        long long off = 0;
        if (sscanf(line, "%u %u %u %u %n", &tk, &lba, &type, &ss, &used) != 4) continue;
        char* s = line + used;
        if (*s == '"') { char* e = strchr(s + 1, '"'); if (!e) continue; snprintf(name, sizeof(name), "%.*s", (int)(e - s - 1), s + 1); off = atoll(e + 1); }
        else if (sscanf(s, "%1099s %lld", name, &off) < 1) continue;
        GdTrack* t = &d->t[d->ntrack++];
        if ((t->file = add_file(d, path, name)) < 0) { snprintf(err, en, "cannot open %s", name); return 0; }
        t->lba = lba; t->data = type == 4; t->ssize = ss == 2048 ? 2048 : CD_FRAME;
        t->user = t->ssize == 2048 ? 0 : 16;
        t->off = 0;   /* (the last field: an offset GDI writers always leave 0) */
        int64_t sz = file_size(d->f[t->file]);
        t->n = sz > 0 ? (uint32_t)(sz / t->ssize) : 0;
    }
    if (!d->ntrack) { snprintf(err, en, "the GDI sheet lists no track"); return 0; }
    return 1;
}

/* ---------------------------------------------------------------- the disc */
void gd_close(GdDisc* d)
{
    if (!d) return;
    for (int i = 0; i < d->nfile; i++) fclose(d->f[i]);
    if (d->chd) fclose(d->chd);
    free(d->map); free(d->hunk); free(d->src); free(d->sec);
    free(d);
}

GdDisc* gd_open(const char* path, char* err, int en)
{
    GdDisc* d = calloc(1, sizeof(GdDisc));
    FILE* f = pc_fopen(path, "rb");
    char head[16] = "";
    int ok = 0;
    err[0] = 0;
    if (!d || !f) { snprintf(err, en, "cannot open the file"); if (f) fclose(f); free(d); return NULL; }
    if (fread(head, 1, sizeof(head), f) == sizeof(head) && !memcmp(head, "MComprHD", 8)) {
        rewind(f);
        ok = chd_open(d, f, err, en);
        if (d->chd != f) fclose(f);   /* (once set, d->chd owns it) */
    } else {
        const char* x = strrchr(path, '.');
        rewind(f);
        if (x && !strcasecmp(x, ".gdi")) ok = gdi_open(d, path, f, err, en);
        else if (x && !strcasecmp(x, ".cue")) ok = cue_open(d, path, f, err, en);
        else snprintf(err, en, "not a CUE or GDI sheet or a CHD image");
        fclose(f);
    }
    if (!ok) { if (!err[0]) snprintf(err, en, "out of memory"); gd_close(d); return NULL; }
    return d;
}

int gd_read(GdDisc* d, uint32_t lba, uint8_t* out)
{
    for (int i = 0; i < d->ntrack; i++) {
        const GdTrack* t = &d->t[i];
        if (!t->data || lba < t->lba || lba - t->lba >= t->n) continue;
        uint32_t k = lba - t->lba;
        if (t->file >= 0) {
            FILE* f = d->f[t->file];
            return !fseeko(f, (off_t)(t->off + (int64_t)k * t->ssize + t->user), SEEK_SET) && fread(out, 1, 2048, f) == 2048;
        }
        uint64_t b = (uint64_t)(t->off + k) * CHD_FRAME;
        if (!chd_hunk(d, (uint32_t)(b / d->hunkbytes))) return 0;
        memcpy(out, d->hunk + b % d->hunkbytes + t->user, 2048);
        return 1;
    }
    return 0;
}

/* `n` bytes from sector lba on (malloc'd) */
static uint8_t* gd_bytes(GdDisc* d, uint32_t lba, uint32_t n)
{
    uint8_t* b = malloc(((size_t)n + 2047) & ~(size_t)2047);
    for (uint32_t k = 0; b && k < (n + 2047) / 2048; k++)
        if (!gd_read(d, lba + k, b + (size_t)k * 2048)) { free(b); return NULL; }
    return b;
}

uint8_t* gd_file(GdDisc* d, const char* path, uint32_t* size)
{
    uint8_t pvd[2048];
    if (!gd_read(d, GD_HD + 16, pvd) || memcmp(pvd + 1, "CD001", 5)) return NULL;
    uint32_t ext = pc_rd32(pvd + 156 + 2), len = pc_rd32(pvd + 156 + 10);
    for (const char* p = path; *p;) {
        const char* e = strchr(p, '/');
        size_t pl = e ? (size_t)(e - p) : strlen(p);
        if (len > (4u << 20)) return NULL;
        uint8_t* dir = gd_bytes(d, ext, len);
        int found = 0;
        for (uint32_t o = 0; dir && o + 33 < len;) {
            uint8_t rl = dir[o];
            if (!rl) { o = (o / 2048 + 1) * 2048; continue; }
            if (o + rl > len || rl < 33 + dir[o + 32]) break;
            const char* nm = (const char*)dir + o + 33;
            size_t nl = dir[o + 32];
            while (nl && (nm[nl - 1] == '.' || nm[nl - 1] == ';' || isdigit((unsigned char)nm[nl - 1])) && memchr(nm, ';', nl)) nl--;   /* (";1") */
            if (nl == pl && !strncasecmp(nm, p, pl)) { ext = pc_rd32(dir + o + 2); len = pc_rd32(dir + o + 10); found = 1 + !(dir[o + 25] & 2); break; }
            o += rl;
        }
        free(dir);
        if (!found) return NULL;
        p = e ? e + 1 : p + pl;
        if (found == 2 && e) {   /* a file with more path: an entry of the AFS archive it is ("SYSTEM.AFS/12") */
            uint32_t k = (uint32_t)strtoul(p, NULL, 10), at = 8 + 8 * k, off;
            uint8_t* toc = len >= 8 && k < 4096 && at + 8 <= len ? gd_bytes(d, ext, at + 8) : NULL;
            int ok = toc && !memcmp(toc, "AFS", 3) && k < pc_rd32(toc + 4);
            off = ok ? pc_rd32(toc + at) : 0;
            ok = ok && !(off % 2048) && off < len && pc_rd32(toc + at + 4) <= len - off;
            if (ok) { ext += off / 2048; len = pc_rd32(toc + at + 4); }
            free(toc);
            if (!ok) return NULL;
            break;
        }
    }
    if (len > (64u << 20)) return NULL;
    *size = len;
    return gd_bytes(d, ext, len);
}
