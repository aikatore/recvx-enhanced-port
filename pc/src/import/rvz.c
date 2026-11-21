/* Wii disc images in Dolphin's RVZ format (docs/IMPORT_UMBRELLA.md "Disc format"): the game partition's files
   by path, and the files of a U8 archive. Our own reading of the layout Dolphin documents (docs/WiaAndRvz.md
   in its repository); only what a retail disc stored with zstd (or without compression) needs.

   RVZ: a header (0x48 bytes), a second header (the compression, the chunk size, the disc header's first 0x80
   bytes, where the partition, raw-data and group tables are), the tables (the raw-data and group tables
   compressed). Partition data is kept decrypted and without its hashes: a group holds chunk / 0x8000 sectors
   of 0x7C00 bytes each, after a list of hash exceptions that is skipped. A group may be "packed": runs of
   data and of junk (the disc's padding, given as a seed; read here as zeros: no file lies in it). */
#include "import.h"
#include "../host/fileio.h"
#include "../../include/pc_parse.h"
#include <SDL3/SDL.h>
#include <zstd.h>
#include <stdlib.h>
#include <string.h>

#define SEC 0x8000u
#define SECD 0x7C00u
#define GROUP_MAX (16u << 20)   /* a group's data, decompressed (a chunk is 2 MiB at most for partition data) */

typedef struct { uint32_t first, n, group, ngroups; } RvzPart;   /* a partition's data entry (sectors) */
typedef struct { uint64_t off, size; uint32_t group, ngroups; } RvzRaw;
typedef struct { uint64_t off; uint32_t size, packed; int z; } RvzGroup;

struct RvzDisc {
    FILE* f;
    unsigned char dhead[0x80];
    uint32_t comp, chunk;
    int npart; RvzPart part[2][2];   /* partitions (at most 2: update, game) x their two data entries */
    int nraw; RvzRaw* raw;
    uint32_t ngroup; RvzGroup* group;
    ZSTD_DCtx* z;
    uint32_t cached;                 /* the group in buf (+1; 0 none) */
    unsigned char* buf; uint32_t blen, bcap;
    int game;                        /* the game partition's index */
    unsigned char* fst; uint32_t fstn;
};

static uint32_t be32(const unsigned char* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint64_t be64(const unsigned char* p) { return (uint64_t)be32(p) << 32 | be32(p + 4); }
static uint16_t be16(const unsigned char* p) { return (uint16_t)(p[0] << 8 | p[1]); }

/* n compressed bytes into *out (malloc'd; *on bytes): zstd frames, or stored as they are */
static int unpack(RvzDisc* d, const unsigned char* in, size_t n, int z, unsigned char** out, uint32_t* on, uint32_t cap)
{
    if (!z) {
        if (!(*out = malloc(n ? n : 1))) return 0;
        memcpy(*out, in, n);
        *on = (uint32_t)n;
        return 1;
    }
    size_t c = 1 << 18, o = 0;
    unsigned char* b = malloc(c);
    ZSTD_inBuffer ib = { in, n, 0 };
    ZSTD_DCtx_reset(d->z, ZSTD_reset_session_only);
    while (b) {
        ZSTD_outBuffer ob = { b, c, o };
        size_t r = ZSTD_decompressStream(d->z, &ob, &ib);
        o = ob.pos;
        if (ZSTD_isError(r)) break;
        if (!r && ib.pos == ib.size) { *out = b; *on = (uint32_t)o; return 1; }
        if (ob.pos == ob.size) {
            unsigned char* q;
            if (c >= cap || !(q = realloc(b, c * 2))) break;
            b = q; c *= 2;
        } else if (ib.pos == ib.size) break;   /* (a truncated frame) */
    }
    free(b);
    return 0;
}

/* group g's data (exception list skipped for partition data, packing undone): d->buf, d->blen bytes */
static int group(RvzDisc* d, uint32_t g, int part, uint32_t want)
{
    if (d->cached == g + 1) return 1;
    d->cached = 0;
    if (g >= d->ngroup) return 0;
    const RvzGroup* e = &d->group[g];
    unsigned char *raw = NULL, *dec = NULL;
    uint32_t n = 0;
    if (want > GROUP_MAX) return 0;
    if (!e->size) {   /* all zero */
        if (d->bcap < want) { unsigned char* q = realloc(d->buf, want); if (!q) return 0; d->buf = q; d->bcap = want; }
        memset(d->buf, 0, want);
        d->blen = want;
        d->cached = g + 1;
        return 1;
    }
    if (e->size > GROUP_MAX || !(raw = malloc(e->size)) || !imp_read_at(d->f, (int64_t)e->off, raw, e->size) ||
        !unpack(d, raw, e->size, e->z, &dec, &n, GROUP_MAX)) { free(raw); return 0; }
    free(raw);
    uint32_t p = 0;
    if (part) {   /* one exception list (the chunk is at most 2 MiB): u16 count, 22 bytes each */
        if (n < 2) { free(dec); return 0; }
        p = 2 + 22u * be16(dec);
        if (!e->z) p = (p + 3) & ~3u;
        if (p > n) { free(dec); return 0; }
    }
    uint32_t len = n - p;
    unsigned char* src = dec + p;
    if (d->bcap < want || d->bcap < len) {
        uint32_t c = want > len ? want : len;
        unsigned char* q = realloc(d->buf, c);
        if (!q) { free(dec); return 0; }
        d->buf = q; d->bcap = c;
    }
    if (e->packed) {   /* runs: u32 size (bit 31: junk, then its 68-byte seed) and the data */
        uint32_t q = 0, o = 0;
        memset(d->buf, 0, want);
        while (q + 4 <= len && o < want) {
            uint32_t s = be32(src + q);
            q += 4;
            if (s & 0x80000000u) { s &= 0x7FFFFFFFu; o += s < want - o ? s : want - o; q += 68; continue; }
            if (s > len - q) break;
            uint32_t k = s < want - o ? s : want - o;
            memcpy(d->buf + o, src + q, k);
            o += k; q += s;
        }
        d->blen = want;
    } else {
        memcpy(d->buf, src, len);
        d->blen = len;
    }
    free(dec);
    d->cached = g + 1;
    return 1;
}

/* n bytes of partition pi's data at off (decrypted user data, 0x7C00 bytes a sector) */
static int read_part(RvzDisc* d, int pi, uint64_t off, unsigned char* out, uint32_t n)
{
    uint32_t spg = d->chunk / SEC, gbytes = spg * SECD;
    uint32_t first = d->part[pi][0].first;
    while (n) {
        uint64_t sec = first + off / SECD;
        int done = 0;
        for (int k = 0; k < 2 && !done; k++) {
            const RvzPart* e = &d->part[pi][k];
            if (sec < e->first || sec >= (uint64_t)e->first + e->n) continue;
            uint64_t rel = (sec - e->first) * SECD + off % SECD;
            uint32_t g = (uint32_t)(rel / gbytes), at = (uint32_t)(rel % gbytes);
            if (g >= e->ngroups || !group(d, e->group + g, 1, gbytes) || at >= d->blen) return 0;
            uint64_t left = ((uint64_t)e->first + e->n - sec) * SECD - off % SECD;
            uint32_t k2 = d->blen - at;
            if (k2 > n) k2 = n;
            if (k2 > left) k2 = (uint32_t)left;
            memcpy(out, d->buf + at, k2);
            out += k2; off += k2; n -= k2;
            done = 1;
        }
        if (!done) return 0;
    }
    return 1;
}

void rvz_close(RvzDisc* d)
{
    if (!d) return;
    if (d->f) fclose(d->f);
    ZSTD_freeDCtx(d->z);
    free(d->raw); free(d->group); free(d->buf); free(d->fst);
    free(d);
}

RvzDisc* rvz_open(const char* path, char* id, int idn)
{
    RvzDisc* d = calloc(1, sizeof(*d));
    unsigned char h1[0x48], *h2 = NULL, *t = NULL;
    uint32_t tn = 0;
    if (id && idn) id[0] = 0;
    if (!d || !(d->f = pc_fopen(path, "rb")) || !(d->z = ZSTD_createDCtx())) goto bad;
    if (!imp_read_at(d->f, 0, h1, sizeof(h1)) || memcmp(h1, "RVZ\x01", 4)) goto bad;
    uint32_t h2n = be32(h1 + 12);
    if (h2n < 0xDC || h2n > 4096 || !(h2 = malloc(h2n)) || !imp_read_at(d->f, 0x48, h2, h2n)) goto bad;
    d->comp = be32(h2 + 4);
    d->chunk = be32(h2 + 12);
    memcpy(d->dhead, h2 + 16, 0x80);
    if (id && idn) snprintf(id, idn, "%.6s", (const char*)d->dhead);
    if ((d->comp != 0 && d->comp != 5) || d->chunk < SEC || d->chunk % SEC || d->chunk > (2u << 20)) goto bad;   /* (none or zstd) */
    uint32_t np = be32(h2 + 0x90), psz = be32(h2 + 0x94);
    uint64_t poff = be64(h2 + 0x98);
    uint32_t nr = be32(h2 + 0xB4), rsz = be32(h2 + 0xC0), ng = be32(h2 + 0xC4), gsz = be32(h2 + 0xD0);
    uint64_t roff = be64(h2 + 0xB8), goff = be64(h2 + 0xC8);
    if (np > 2 || psz < 48 || nr > 64 || ng > (1u << 20) || rsz > (1u << 20) || gsz > (16u << 20)) goto bad;
    {   /* partitions: a 16-byte key, two data entries (first sector, sectors, first group, groups) */
        unsigned char p[2 * 64];
        if (np && (psz > 64 || !imp_read_at(d->f, (int64_t)poff, p, np * psz))) goto bad;
        for (uint32_t i = 0; i < np; i++)
            for (int k = 0; k < 2; k++) {
                const unsigned char* e = p + i * psz + 16 + 16 * k;
                d->part[i][k] = (RvzPart){ be32(e), be32(e + 4), be32(e + 8), be32(e + 12) };
            }
        d->npart = (int)np;
    }
    unsigned char* raw = malloc(rsz ? rsz : 1);
    if (!raw || !imp_read_at(d->f, (int64_t)roff, raw, rsz) || !unpack(d, raw, rsz, d->comp == 5, &t, &tn, 1u << 20) || tn < nr * 24u) { free(raw); goto bad; }
    free(raw);
    d->raw = calloc(nr ? nr : 1, sizeof(*d->raw));
    if (!d->raw) goto bad;
    for (uint32_t i = 0; i < nr; i++) d->raw[i] = (RvzRaw){ be64(t + 24 * i), be64(t + 24 * i + 8), be32(t + 24 * i + 16), be32(t + 24 * i + 20) };
    d->nraw = (int)nr;
    free(t); t = NULL;
    raw = malloc(gsz ? gsz : 1);
    if (!raw || !imp_read_at(d->f, (int64_t)goff, raw, gsz) || !unpack(d, raw, gsz, d->comp == 5, &t, &tn, 16u << 20) || tn < ng * 12u) { free(raw); goto bad; }
    free(raw);
    d->group = calloc(ng ? ng : 1, sizeof(*d->group));
    if (!d->group) goto bad;
    for (uint32_t i = 0; i < ng; i++) {   /* data offset / 4, size (bit 31: compressed), packed size */
        uint32_t s = be32(t + 12 * i + 4);
        d->group[i] = (RvzGroup){ (uint64_t)be32(t + 12 * i) << 2, s & 0x7FFFFFFFu, be32(t + 12 * i + 8), (s >> 31) & 1 };
    }
    d->ngroup = ng;
    free(t);
    free(h2);
    d->game = d->npart - 1;   /* (the update partition first, then the game) */
    if (d->game < 0) { rvz_close(d); return NULL; }
    {   /* the file system table: its offset and size (both / 4) at 0x424 of the partition's data */
        unsigned char ph[0x440];
        if (!read_part(d, d->game, 0, ph, sizeof(ph))) { rvz_close(d); return NULL; }
        uint64_t fo = (uint64_t)be32(ph + 0x424) << 2;
        uint32_t fs = be32(ph + 0x428) << 2;
        if (fs < 12 || fs > (16u << 20) || !(d->fst = malloc(fs)) || !read_part(d, d->game, fo, d->fst, fs) ||
            (uint64_t)be32(d->fst + 8) * 12 > fs) { rvz_close(d); return NULL; }
        d->fstn = fs;
    }
    return d;
bad:
    free(h2); free(t);
    rvz_close(d);
    return NULL;
}

/* a file of the game partition by its path ("Data/chara/PCR1.arc", any case): malloc'd, *size bytes */
unsigned char* rvz_file(RvzDisc* d, const char* path, uint32_t* size)
{
    const unsigned char* f = d->fst;
    uint32_t n = be32(f + 8), dir_end = n, i = 1;
    const char* names = (const char*)f + n * 12;
    size_t nlen = d->fstn - n * 12;
    while (*path) {
        const char* slash = strchr(path, '/');
        size_t want = slash ? (size_t)(slash - path) : strlen(path);
        int found = 0;
        while (i < dir_end && !found) {
            uint32_t a = be32(f + 12 * i), b = be32(f + 12 * i + 8), no = a & 0xFFFFFF;
            const char* nm = no < nlen ? names + no : "";
            int isdir = a >> 24;
            if (no < nlen && strnlen(nm, nlen - no) == want && !SDL_strncasecmp(nm, path, want) && !isdir == !slash) {
                if (!slash) {
                    uint64_t off = (uint64_t)be32(f + 12 * i + 4) << 2;
                    unsigned char* out;
                    if (b > (256u << 20) || !(out = malloc(b ? b : 1))) return NULL;
                    if (!read_part(d, d->game, off, out, b)) { free(out); return NULL; }
                    *size = b;
                    return out;
                }
                dir_end = b > n ? n : b;
                i++;
                found = 1;
            } else i = isdir && b > i ? b : i + 1;   /* (a folder: past its contents) */
        }
        if (!found) return NULL;
        path = slash + 1;
    }
    return NULL;
}

/* U8 archive (magic 55 AA 38 2D): 12-byte nodes (u8 type | u24 name offset, data offset, size / next); the file
   `name` (its last path part, any case) anywhere in it */
int u8_find(const unsigned char* d, uint32_t n, const char* name, const unsigned char** out, uint32_t* size)
{
    if (n < 0x20 || be32(d) != 0x55AA382Du) return 0;
    uint32_t root = be32(d + 4);
    if (!pc_range_ok(root, 12, n)) return 0;
    uint32_t cnt = be32(d + root + 8);
    if (!cnt || !pc_range_ok(root, (uint64_t)cnt * 12, n)) return 0;
    uint32_t st = root + cnt * 12;
    for (uint32_t i = 1; i < cnt; i++) {
        const unsigned char* e = d + root + 12 * i;
        uint32_t no = be32(e) & 0xFFFFFF, a = be32(e + 4), b = be32(e + 8);
        if (e[0] || !pc_range_ok(st, no, n) || st + no >= n) continue;
        const char* nm = (const char*)d + st + no;
        if (!memchr(nm, 0, n - st - no) || SDL_strcasecmp(nm, name)) continue;
        if (!pc_range_ok(a, b, n)) return 0;
        *out = d + a; *size = b;
        return 1;
    }
    return 0;
}
