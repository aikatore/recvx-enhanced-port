/* Code: Veronica's Dreamcast release (docs/IMPORT_DREAMCAST.md): models of the US disc 1 (T-1204N) put into the
   PS2 game's own formats, so the game draws, skins and animates them as its own models, face mask included.
   The Dreamcast room files (R0_SRRC.RDX) have the PS2's layout and compression; what differs:
   - vertex chunks: Ninja's type 41 (position, normal: 24 bytes a vertex after an 8-byte header) where the PS2 has its
     type 51 (njCnkCvVnPs2: a 64-byte header, 32 bytes a vertex, w 1.0); the skin conversion (npSkinConvSub) and the
     VU code read only 51: each list is written again as 51, 64-byte aligned, after the model's data;
   - textures: PVR (twiddled, VQ and small VQ; ARGB1555, RGB565, ARGB4444) where the PS2 has TIM2: each decoded and
     written as an 8-bit TIM2 chunk (impmodel.c imp_tex_chunk), in the list's order (texture k fills texlist[k]);
   - the skin (SKIN) and the face mask (MASK: muscles, face table, mouth shapes) are taken as they are.
   Written as a small PS2-format room file (expanded) holding the one enemy record, which actor_glue.c loads as it
   loads a catalogue model from the disc's room file (load_room_model). */
#include "import.h"
#include "impmodel.h"
#include "../host/lang.h"
#include "../host/fileio.h"
#include "../../include/pc_parse.h"
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* the models offered: the room file and record that carry each on the disc (docs/IMPORT_DREAMCAST.md) */
static const struct DcModel { const char *id, *name, *variant, *room; int rec, type, ver; } models[] = {
    { "STEVE_CINE", "Steve", "Dreamcast Hair", "R0_0040.RDX", 0, 93, 0 },
};
#define NMODELS (int)(sizeof(models) / sizeof(models[0]))
#define ROOM_MAX (12u << 20)    /* the game's room arena */

int dc_model(const char* id, unsigned char* type, unsigned char* ver)
{
    for (int i = 0; i < NMODELS; i++)
        if (!strcmp(models[i].id, id)) { *type = (unsigned char)models[i].type; *ver = (unsigned char)models[i].ver; return 1; }
    return 0;
}

/* ---------------------------------------------------------------- PVR textures */
static uint32_t twiddle(uint32_t x, uint32_t y)   /* (y in the even bits) */
{
    uint32_t r = 0;
    for (int b = 0; b < 11; b++) r |= ((y >> b) & 1) << (2 * b) | ((x >> b) & 1) << (2 * b + 1);
    return r;
}
static void texel(int fmt, uint16_t c, unsigned char* o)
{
    switch (fmt) {
    case 0: o[0] = (unsigned char)(((c >> 10) & 31) * 255 / 31); o[1] = (unsigned char)(((c >> 5) & 31) * 255 / 31); o[2] = (unsigned char)((c & 31) * 255 / 31); o[3] = c & 0x8000 ? 255 : 0; break;
    case 1: o[0] = (unsigned char)(((c >> 11) & 31) * 255 / 31); o[1] = (unsigned char)(((c >> 5) & 63) * 255 / 63); o[2] = (unsigned char)((c & 31) * 255 / 31); o[3] = 255; break;
    default: o[0] = (unsigned char)(((c >> 8) & 15) * 17); o[1] = (unsigned char)(((c >> 4) & 15) * 17); o[2] = (unsigned char)((c & 15) * 17); o[3] = (unsigned char)((c >> 12) * 17); break;
    }
}
static int pow2(int v) { return v >= 8 && v <= 1024 && !(v & (v - 1)); }

/* a PVRT chunk (p, n bytes from "PVRT") as RGBA8 (malloc'd); NULL: a format not read (palettized, YUV, ...) */
unsigned char* dc_pvr_rgba(const unsigned char* p, uint32_t n, int* W, int* H, char* why, int wn)
{
    if (n < 16 || memcmp(p, "PVRT", 4)) { snprintf(why, wn, "not a PVR texture"); return NULL; }
    uint32_t len = pc_rd32(p + 4);
    int fmt = p[8], dat = p[9], w = pc_rd16(p + 12), h = pc_rd16(p + 14);
    if (len < 8 || len - 8 > n - 16 || fmt > 2 || !pow2(w) || !pow2(h)) { snprintf(why, wn, "PVR pixel format %d, data format %d, %dx%d not read", fmt, dat, w, h); return NULL; }
    const unsigned char* d = p + 16;
    uint32_t dn = len - 8, need;
    unsigned char* o = malloc((size_t)w * h * 4);
    if (!o) return NULL;
    int vq = dat == 3 || dat == 4 || dat == 0x10 || dat == 0x11;
    if (vq) {   /* a codebook of 2x2 blocks, then the blocks' indices twiddled (mipmaps: the largest level last) */
        int cb = 256;
        if (w != h) { free(o); snprintf(why, wn, "a VQ texture that is not square (%dx%d)", w, h); return NULL; }
        if (dat == 0x10) cb = w <= 16 ? 16 : w == 32 ? 32 : w == 64 ? 128 : 256;
        if (dat == 0x11) cb = w <= 16 ? 16 : w == 32 ? 64 : 256;
        need = (uint32_t)cb * 8 + (uint32_t)(w / 2) * (h / 2);
        if (dn < need) { free(o); snprintf(why, wn, "a VQ texture's data is short"); return NULL; }
        const unsigned char* idx = d + dn - (uint32_t)(w / 2) * (h / 2);
        for (int y = 0; y < h; y += 2)
            for (int x = 0; x < w; x += 2) {
                const unsigned char* e = d + 8 * idx[twiddle((uint32_t)x / 2, (uint32_t)y / 2)];
                for (int k = 0; k < 4; k++) texel(fmt, pc_rd16(e + 2 * k), o + 4 * ((size_t)(y + (k & 1)) * w + x + (k >> 1)));
            }
    } else if (dat == 1 || dat == 2 || dat == 0x12 || dat == 9 || dat == 0x0D) {   /* twiddled (square, mipmaps), rectangle, twiddled rectangle */
        int m = w < h ? w : h;
        need = (uint32_t)w * h * 2;
        if (dn < need || ((dat == 1 || dat == 2 || dat == 0x12) && w != h)) { free(o); snprintf(why, wn, "a PVR texture's data is short"); return NULL; }
        const unsigned char* px = d + dn - need;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                uint32_t i = dat == 9 ? (uint32_t)(y * w + x) : (uint32_t)((w > h ? x / m : y / m) * m * m) + twiddle((uint32_t)(x % m), (uint32_t)(y % m));
                texel(fmt, pc_rd16(px + 2 * i), o + 4 * ((size_t)y * w + x));
            }
    } else { free(o); snprintf(why, wn, "PVR data format %d not read", dat); return NULL; }
    *W = w; *H = h;
    return o;
}

/* ---------------------------------------------------------------- the output */
typedef struct { unsigned char* p; size_t n, cap; int bad; } Out;
static size_t put(Out* b, const void* d, size_t n)
{
    size_t at = b->n;
    if (b->bad) return at;
    if (b->n + n > b->cap) {
        size_t c = b->cap ? b->cap : 1 << 20;
        while (c < b->n + n) c *= 2;
        unsigned char* q = realloc(b->p, c);
        if (!q) { b->bad = 1; return at; }
        b->p = q; b->cap = c;
    }
    if (d) memcpy(b->p + b->n, d, n); else memset(b->p + b->n, 0, n);
    b->n += n;
    return at;
}
static void put32(Out* b, uint32_t v) { put(b, &v, 4); }
static void set32(Out* b, size_t at, uint32_t v) { if (!b->bad) memcpy(b->p + at, &v, 4); }
static void pad_to(Out* b, size_t mod, size_t rem) { while (!b->bad && b->n % mod != rem) put(b, NULL, 1); }

/* an MLB (p, n bytes) with every type 41 vertex list written again as type 51 after its data, 64-byte aligned in the
   output (the MLB's data starts on a 64-byte boundary there: base); the chunk's bytes appended to b. 0: a damaged MLB */
static int put_mlb(Out* b, const unsigned char* p, uint32_t n, int* lists)
{
    if (n < 16) return 0;
    uint32_t hs = pc_rd16(p + 4), nobj = pc_rd16(p + 6), obj = pc_rd32(p + 12);
    if (hs < 16 || hs > n || obj == 0xFFFFFFFFu || !pc_range_ok((uint64_t)hs + obj, (uint64_t)nobj * 0x34, n)) return 0;
    size_t at = put(b, p, n), base = at + hs;
    for (uint32_t i = 0; i < nobj && !b->bad; i++) {
        uint32_t mo = pc_rd32(p + hs + obj + 0x34 * i + 4), vl;
        if (mo == 0xFFFFFFFFu) continue;
        if (!pc_range_ok((uint64_t)hs + mo, 8, n)) return 0;
        vl = pc_rd32(p + hs + mo);
        if (vl == 0xFFFFFFFFu) continue;
        if (!pc_range_ok((uint64_t)hs + vl, 8, n)) return 0;
        const unsigned char* v = p + hs + vl;
        if (v[0] == 51) continue;
        if (v[0] != 41) return 0;   /* (Code: Veronica's models: 41 only) */
        uint32_t cnt = pc_rd16(v + 6);
        if ((60 + 32 * cnt) / 4 > 0xFFFF || !pc_range_ok((uint64_t)hs + vl + 8, (uint64_t)cnt * 24, n) || pc_rd16(v + 4)) return 0;   /* (an index offset: none in the game) */
        pad_to(b, 64, 0);
        size_t nv = put(b, NULL, 64);
        uint32_t hdr0 = 51u | (uint32_t)v[1] << 8 | (uint32_t)((60 + 32 * cnt) / 4) << 16, hdr1 = cnt << 16;
        set32(b, nv, hdr0); set32(b, nv + 4, hdr1);
        for (uint32_t k = 0; k < cnt; k++) {
            float f[8], one = 1.0f;
            memcpy(f, v + 8 + 24 * k, 12);
            memcpy(f + 4, v + 8 + 24 * k + 12, 12);
            f[3] = f[7] = one;
            put(b, f, 32);
        }
        put32(b, 0xFF); put32(b, 0);
        if (!b->bad) {   /* the model's vlist offset (from base) */
            uint32_t rel = (uint32_t)(nv - base);
            memcpy(b->p + base + mo, &rel, 4);
        }
        (*lists)++;
    }
    if (!b->bad) {   /* the chunk's size word before it */
        uint32_t sz = (uint32_t)(b->n - at);
        memcpy(b->p + at - 4, &sz, 4);
    }
    return !b->bad;
}

/* the texture table entry room.c gives the record's (type, version) k-th textured sub-model (actor_glue.c tex_owner) */
static int owner(const unsigned char* e, uint32_t n, uint32_t enep, int ene, uint32_t h1, int rec)
{
    int m = 1, owners = 0, want = -1;
    int seen[1024];
    for (int i = 0; i < ene; i++) {
        const unsigned char* r = e + enep + 0x24 * i;
        if (!(pc_rd32(r) & 1)) continue;
        uint32_t p = pc_rd32(e + h1 + 4 * m++);
        int t = -1;
        int32_t dt;
        while (p + 4 <= n && (dt = (int32_t)pc_rd32(e + p)) != -1) {
            p += 4;
            if (dt < 0 || dt > (int32_t)(n - p)) break;
            if (dt >= 16 && memcmp(e + p, "MASK", 4) && memcmp(e + p, "SKIN", 4) && pc_rd32(e + p + 8) != 0xFFFFFFFFu) {
                int key = pc_rd16(r + 4) << 16 | r[9] << 8 | ++t, j;
                for (j = 0; j < owners && seen[j] != key; j++);
                if (j == owners) { if (owners == 1024) return -1; seen[owners++] = key; }
                if (i == rec && want < 0) want = j;   /* (its first textured sub-model; Code: Veronica's cast: one) */
            }
            p += (uint32_t)dt;
        }
    }
    return want;
}

/* model k of the expanded room e (n bytes) as a PS2-format room file into b; 0 with why */
static int convert(const struct DcModel* md, const unsigned char* e, uint32_t n, Out* b, int* lists, int* ntex, char* why, int wn)
{
    if (n < 0x80 + 0x3EC || pc_rd32(e) != 0x41200000u) { snprintf(why, wn, "%s is not a room file", md->room); return 0; }   /* (10.0f) */
    uint32_t h0 = pc_rd32(e + 0x10), h1 = pc_rd32(e + 0x14), h4 = pc_rd32(e + 0x20);
    if (!pc_range_ok(h0, 0x3EC, n)) { snprintf(why, wn, "%s is damaged", md->room); return 0; }
    uint32_t enep = pc_rd32(e + h0 + 8);
    int ene = (int)pc_rd32(e + h0 + 0x80 + 8), k = 1;
    if (ene <= md->rec || ene > 256 || !pc_range_ok(enep, 0x24u * (uint32_t)ene, n) || !pc_range_ok(h1, 4u * (uint32_t)(ene + 1), n) || !pc_range_ok(h4, 4, n)) {
        snprintf(why, wn, "%s is damaged", md->room); return 0;
    }
    const unsigned char* rec = e + enep + 0x24 * md->rec;
    if (!(pc_rd32(rec) & 1) || pc_rd16(rec + 4) != md->type || rec[9] != md->ver) { snprintf(why, wn, "record %d of %s is not type %d", md->rec, md->room, md->type); return 0; }
    for (int i = 0; i < md->rec; i++) k += pc_rd32(e + enep + 0x24 * i) & 1;
    uint32_t list = pc_rd32(e + h1 + 4 * k);
    int o = owner(e, n, enep, ene, h1, md->rec);
    uint32_t tn = pc_rd32(e + h4), tl = o >= 0 && (uint32_t)o + 1 < tn && pc_range_ok(h4, 4 + 4 * (uint64_t)tn, n) ? pc_rd32(e + h4 + 8 + 4 * o) : 0;
    if (!tl || tl >= n) { snprintf(why, wn, "no texture list for record %d of %s", md->rec, md->room); return 0; }

    /* header, room block, the record, the tables (the texture table: the room model's entry, an empty list, then ours) */
    size_t hd = put(b, NULL, 0x80);
    set32(b, hd, 0x41200000u);   /* (ver 10.0) */
    if (!b->bad) memcpy(b->p + hd + 0x60, "DC-IMPORT", 9);
    size_t rb = put(b, NULL, 0x3EC);
    pad_to(b, 4, 0);
    size_t rp = put(b, rec, 0x24);
    set32(b, rb + 8, (uint32_t)rp);
    set32(b, rb + 0x80 + 8, 1);
    size_t t1 = put(b, NULL, 8), t2 = put(b, NULL, 4), t4 = put(b, NULL, 12), none = b->n;
    put32(b, 0xFFFFFFFFu);
    set32(b, hd + 0x10, (uint32_t)rb); set32(b, hd + 0x14, (uint32_t)t1); set32(b, hd + 0x18, (uint32_t)t2); set32(b, hd + 0x20, (uint32_t)t4);
    set32(b, t1, (uint32_t)none);
    set32(b, t4, 2);

    /* the chunk list: each chunk's data aligned as the game's files have it (the MLB's data on 64 bytes, its 51 lists
       too; the mask on 8 mod 64, as the PS2's), the padding at the end of the chunk before */
    uint32_t p = list;
    size_t prev = 0;   /* the size word of the chunk before (0: none, or an empty one: not grown) */
    int first = 1;
    int32_t dt;
    while (p + 4 <= n && (dt = (int32_t)pc_rd32(e + p)) != -1) {
        p += 4;
        if (dt < 0 || dt > (int32_t)(n - p)) { snprintf(why, wn, "a damaged chunk list in %s", md->room); return 0; }
        int mlb = dt >= 16 && memcmp(e + p, "MASK", 4) && memcmp(e + p, "SKIN", 4);
        size_t rem = mlb ? (64 - pc_rd16(e + p + 4) % 64) % 64 : dt >= 4 && !memcmp(e + p, "MASK", 4) ? 8 : 4;
        if (first) { pad_to(b, 64, (rem + 60) % 64); set32(b, t1 + 4, (uint32_t)b->n); first = 0; }
        else if (prev) {
            while ((b->n + 4) % 64 != rem) put(b, NULL, 1);
            set32(b, prev, (uint32_t)(b->n - prev - 4));
        }
        prev = dt ? b->n : 0;
        put32(b, (uint32_t)dt);
        if (mlb) {
            if (!put_mlb(b, e + p, (uint32_t)dt, lists)) { snprintf(why, wn, "a model of record %d of %s is not read", md->rec, md->room); return 0; }
        } else put(b, e + p, (size_t)dt);
        p += (uint32_t)dt;
    }
    if (first) set32(b, t1 + 4, (uint32_t)b->n);
    put32(b, 0xFFFFFFFFu);

    /* the textures: PVR to TIM2, in the list's order; an empty list for the room model's entry */
    pad_to(b, 64, 0);
    set32(b, t4 + 4, (uint32_t)b->n);
    put32(b, 0xFFFFFFFFu); put(b, NULL, 28);
    set32(b, t4 + 8, (uint32_t)b->n);
    for (p = tl; p + 32 <= n && pc_rd32(e + p) != 0xFFFFFFFFu;) {
        uint32_t sz = pc_rd32(e + p + 4);
        if (sz > n - p - 32) { snprintf(why, wn, "a damaged texture list in %s", md->room); return 0; }
        const unsigned char* c = e + p + 32;
        if (!memcmp(e + p, "TPVR", 4)) {
            uint32_t g = sz >= 16 && !memcmp(c, "GBIX", 4) ? pc_rd32(c + 8) : (uint32_t)*ntex, skip = sz >= 16 && !memcmp(c, "GBIX", 4) ? 8 + pc_rd32(c + 4) : 0;
            int w, h, len;
            unsigned char *rgba = skip < sz ? dc_pvr_rgba(c + skip, sz - skip, &w, &h, why, wn) : NULL, *t;
            if (!rgba) { if (!why[0]) snprintf(why, wn, "a texture of %s is not read", md->room); return 0; }
            t = imp_tex_chunk(rgba, w, h, 0x00D00000u | (g & 0xFFFFF), &len);   /* (Gindex: apart from the PS2's and the other imports';
                the low bits kept, which the game's clamp list reads) */
            free(rgba);
            if (!t) { snprintf(why, wn, "out of memory"); return 0; }
            put(b, t, (size_t)len);
            free(t);
            (*ntex)++;
        } else if (memcmp(e + p, "PLI", 4)) { snprintf(why, wn, "a texture chunk of %s is not read", md->room); return 0; }   /* (no palettes in this list) */
        p += 32 + sz;
    }
    put32(b, 0xFFFFFFFFu); put(b, NULL, 28);
    if (b->bad) { snprintf(why, wn, "out of memory"); return 0; }
    return 1;
}

typedef struct { const unsigned char* p; size_t n; } Blob;
static int put_blob(FILE* f, void* ud) { const Blob* b = ud; return fwrite(b->p, 1, b->n, f) == b->n ? 0 : -1; }

int dc_extract(const char* image, const char* outdir, int what, ImportItem* items, int max, int* count)
{
    ImportProgress* pg = &g_import_progress;
    char err[160];
    unsigned char ip[2048];
    int fails = 0;
    *count = 0;
    GdDisc* d = gd_open(image, err, sizeof(err));
    if (!d) {
        snprintf(pg->phase, sizeof(pg->phase), tr("Cannot read the disc image: it must be a CUE, GDI or CHD image of a Dreamcast disc: %s"), image);
        SDL_Log("import: dreamcast: %s: %s", image, err);
        return -1;
    }
    /* the first sector of the high-density area: the IP.BIN (hardware id, product number, the disc's number, area) */
    if (!gd_read(d, 45000, ip) || memcmp(ip, "SEGA SEGAKATANA", 15) || memcmp(ip + 0x40, "T1204N", 6) || memchr(ip + 0x30, 'U', 8) == NULL) {
        snprintf(pg->phase, sizeof(pg->phase), tr("Use the US Dreamcast disc 1 of Resident Evil Code: Veronica (T-1204N)."));
        gd_close(d);
        return -1;
    }
    pg->total = 0;
    for (int i = 0; i < NMODELS; i++) pg->total += !!(what & IMPORT_PLAYERS);
    unsigned char* room = malloc(ROOM_MAX);
    for (int i = 0; i < NMODELS && *count < max && (what & IMPORT_PLAYERS); i++) {
        const struct DcModel* md = &models[i];
        ImportItem* it = &items[(*count)++];
        uint32_t size = 0;
        int32_t len;
        int lists = 0, ntex = 0;
        Out b = { 0 };
        char dir[1200], path[1300];
        memset(it, 0, sizeof(*it));
        err[0] = 0;
        snprintf(it->id, sizeof(it->id), "%s", md->id);
        snprintf(it->name, sizeof(it->name), "%s", md->name);
        snprintf(it->variant, sizeof(it->variant), "%s", md->variant);
        it->group = IMPORT_GROUP_PLAYERS;
        snprintf(pg->phase, sizeof(pg->phase), "%s (%s) (%s)", it->name, it->variant, md->room);
        unsigned char* f = gd_file(d, md->room, &size);
        if (!f) snprintf(err, sizeof(err), "Cannot read %s from the disc (disc 1 has it).", md->room);
        else if (!room || (len = pc_prs_decode(f, size, room, ROOM_MAX)) < 0) snprintf(err, sizeof(err), "%s cannot be expanded.", md->room);
        else if (convert(md, room, (uint32_t)len, &b, &lists, &ntex, err, sizeof(err))) {
            Blob bl = { b.p, b.n };
            snprintf(dir, sizeof(dir), "%s/%s", outdir, md->id);
            SDL_CreateDirectory(dir);
            snprintf(path, sizeof(path), "%s/%s.rdx", dir, md->id);
            if (pc_save_file(path, "wb", put_blob, &bl)) snprintf(err, sizeof(err), "Cannot write %s.", path);
            else {
                it->ok = 1;
                it->player = 3;
                it->textures = ntex;
                it->meshes = lists;
            }
        }
        free(f);
        free(b.p);
        snprintf(it->note, sizeof(it->note), "%s", err);
        fails += !it->ok;
        pg->failed = fails;
        pg->done++;
    }
    free(room);
    gd_close(d);
    return fails;
}
