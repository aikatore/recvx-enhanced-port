/* Writers for imported models: glTF 2.0 binary with PNG textures, and the port's .cvxm player
   model (impmodel.h; docs/IMPORT_OUTBREAK.md "Conversion"). */
#include "impmodel.h"
#include "../host/fileio.h"
#include "../../include/pc_parse.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- growing buffers */
typedef struct Buf { unsigned char* p; size_t n, cap; int bad; } Buf;

static void* put(Buf* b, const void* d, size_t n)
{
    if (b->bad) return NULL;
    if (b->n + n > b->cap) {
        size_t c = b->cap ? b->cap : 4096;
        unsigned char* q;
        while (c < b->n + n) c *= 2;
        if (!(q = realloc(b->p, c))) { b->bad = 1; return NULL; }
        b->p = q; b->cap = c;
    }
    if (d) memcpy(b->p + b->n, d, n); else memset(b->p + b->n, 0, n);
    b->n += n;
    return b->p + b->n - n;
}
static void align(Buf* b, size_t a, int fill) { while (b->n % a) { unsigned char c = (unsigned char)fill; put(b, &c, 1); } }
static void put32(Buf* b, uint32_t v) { put(b, &v, 4); }
static void put16(Buf* b, uint16_t v) { put(b, &v, 2); }
static void putf(Buf* b, float v) { put(b, &v, 4); }
static void pr(Buf* b, const char* fmt, ...)
{
    char t[512];
    va_list a;
    va_start(a, fmt);
    int n = vsnprintf(t, sizeof t, fmt, a);
    va_end(a);
    if (n > 0) put(b, t, (size_t)n < sizeof t ? (size_t)n : sizeof t - 1);
}
static void set32(Buf* b, size_t at, uint32_t v) { if (!b->bad) memcpy(b->p + at, &v, 4); }

static int fail(char* err, int en, const char* fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    if (err && en > 0) vsnprintf(err, en, fmt, a);
    va_end(a);
    return 0;
}

typedef struct Blob { const void* d; size_t n; } Blob;
static int put_blob(FILE* f, void* ud) { const Blob* b = ud; return fwrite(b->d, 1, b->n, f) == b->n ? 0 : -1; }
/* a temporary file renamed over it: the game may be loading the model it replaces */
static int save(const char* path, const void* d, size_t n)
{
    Blob b = { d, n };
    return pc_save_file(path, "wb", put_blob, &b) == 0;
}

void imp_free(ImpModel* m)
{
    free(m->pos); free(m->nrm); free(m->uv); free(m->jnt); free(m->wgt); free(m->strip); free(m->idx);
    for (int i = 0; i < m->ntex; i++) free(m->tim2[i]);
    memset(m, 0, sizeof(*m));
}

/* the strips as triangles (alternating order); degenerate ones left out. fn(a, b, c, tex, ud) */
static int each_tri(const ImpModel* m, void (*fn)(int, int, int, int, void*), void* ud)
{
    int n = 0;
    for (int s = 0; s < m->ns; s++) {
        const int* x = m->idx + m->strip[s].first;
        for (int k = 0; k + 2 < m->strip[s].n; k++) {
            int a = x[k], b = x[k + 1 + (k & 1)], c = x[k + 2 - (k & 1)];
            if (a == b || b == c || a == c) continue;
            if (fn) fn(a, b, c, m->strip[s].tex, ud);
            n++;
        }
    }
    return n;
}
int imp_tris(const ImpModel* m) { return each_tri(m, NULL, NULL); }

/* ---------------------------------------------------------------- TIM2 */
typedef struct Pic {
    uint32_t total, clut, img;
    uint16_t hsize, ncol;
    uint8_t fmt, mip, ctype, itype;
    uint16_t w, h;
    uint64_t tex0, tex1;
    uint32_t regs, texclut;
    const unsigned char *px, *pal;
} Pic;

static int pic(const unsigned char* t, int len, Pic* p)
{
    int at = len >= 16 && t[5] ? 128 : 16;   /* the file header: 16 bytes, or 128 (format id 1: 128-byte aligned) */
    if (len < at + 48 || memcmp(t, "TIM2", 4)) return 0;
    const unsigned char* h = t + at;   /* the first picture */
    memcpy(&p->total, h, 4); memcpy(&p->clut, h + 4, 4); memcpy(&p->img, h + 8, 4);
    memcpy(&p->hsize, h + 12, 2); memcpy(&p->ncol, h + 14, 2);
    p->fmt = h[16]; p->mip = h[17]; p->ctype = h[18]; p->itype = h[19];
    memcpy(&p->w, h + 20, 2); memcpy(&p->h, h + 22, 2);
    memcpy(&p->tex0, h + 24, 8); memcpy(&p->tex1, h + 32, 8); memcpy(&p->regs, h + 40, 4); memcpy(&p->texclut, h + 44, 4);
    if (p->hsize < 48 || !p->w || !p->h || p->mip != 1) return 0;
    if ((uint64_t)at + p->hsize + p->img + p->clut > (uint64_t)len) return 0;
    p->px = h + p->hsize;
    p->pal = p->px + p->img;
    return 1;
}

int imp_tim2_rgba(const unsigned char* t, int len, int* W, int* H, unsigned char** out)
{
    Pic p;
    int bpp[] = { 0, 16, 24, 32, 4, 8 };
    if (!pic(t, len, &p) || p.itype < 1 || p.itype > 5) return 0;
    uint64_t need = ((uint64_t)p.w * p.h * bpp[p.itype] + 7) / 8;
    if (need > p.img) return 0;
    if (p.itype >= 4 && ((p.ctype & 0x3f) != 3 || p.clut < (p.itype == 4 ? 64u : 1024u))) return 0;   /* (a 32-bit CLUT) */
    unsigned char* o = malloc((size_t)p.w * p.h * 4);
    if (!o) return 0;
    for (int y = 0; y < p.h; y++)
        for (int x = 0; x < p.w; x++) {
            size_t i = (size_t)y * p.w + x;
            const unsigned char* c;
            unsigned char px[4];
            if (p.itype == 5) {
                int k = p.px[i];
                if (!(p.ctype & 0x80)) k = (k & ~0x18) | ((k & 0x08) << 1) | ((k & 0x10) >> 1);   /* CSM1: entries 8..15 and 16..23 of every 32 swapped */
                c = p.pal + 4 * k;
            } else if (p.itype == 4) c = p.pal + 4 * ((p.px[i / 2] >> (4 * (i & 1))) & 15);
            else if (p.itype == 3) c = p.px + 4 * i;
            else if (p.itype == 2) { memcpy(px, p.px + 3 * i, 3); px[3] = 0x80; c = px; }
            else {
                unsigned v = p.px[2 * i] | p.px[2 * i + 1] << 8;
                px[0] = (unsigned char)((v & 31) << 3); px[1] = (unsigned char)(((v >> 5) & 31) << 3);
                px[2] = (unsigned char)(((v >> 10) & 31) << 3); px[3] = v & 0x8000 ? 0x80 : 0;
                c = px;
            }
            memcpy(o + 4 * i, c, 3);
            o[4 * i + 3] = c[3] >= 0x80 ? 255 : (unsigned char)(c[3] * 2);   /* PS2 alpha: 0x80 = opaque */
        }
    *W = p.w; *H = p.h; *out = o;
    return 1;
}

void imp_tim2_alpha_255(unsigned char* t, int len)
{
    Pic p;
    if (!pic(t, len, &p) || p.itype < 4 || p.itype > 5 || (p.ctype & 0x3f) != 3) return;
    for (unsigned char* a = t + (p.pal - t) + 3; a < p.pal + p.clut; a += 4) *a = (unsigned char)((*a * 128 + 127) / 255);
}

/* a texture as PNG bytes (into b), or written to `path` */
static int png(const ImpModel* m, int k, const char* path, Buf* b)
{
    int w, h, ok = 0;
    unsigned char* px;
    if (k < 0 || k >= m->ntex || !imp_tim2_rgba(m->tim2[k], m->tim2_len[k], &w, &h, &px)) return 0;
    SDL_Surface* s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_ABGR8888, px, w * 4);
    if (s && path) ok = SDL_SavePNG(s, path);
    else if (s) {
        SDL_IOStream* io = SDL_IOFromDynamicMem();
        if (io && SDL_SavePNG_IO(s, io, false)) {
            Sint64 n = SDL_GetIOSize(io);
            void* d = SDL_GetPointerProperty(SDL_GetIOProperties(io), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, NULL);
            ok = d && n > 0 && put(b, d, (size_t)n);
        }
        if (io) SDL_CloseIO(io);
    }
    SDL_DestroySurface(s);
    free(px);
    return ok;
}
int imp_write_png(const ImpModel* m, int k, const char* path) { return png(m, k, path, NULL); }

/* ---------------------------------------------------------------- rest pose */
/* column-major 4x4 (glTF) world rest matrices */
static void mat_from(const ImpNode* n, float* l)
{
    float x = n->q[0], y = n->q[1], z = n->q[2], w = n->q[3];
    float c[16] = { 1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 0,
                    2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 0,
                    2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y), 0,
                    n->t[0], n->t[1], n->t[2], 1 };
    memcpy(l, c, sizeof(c));
}
static void mul(const float* a, const float* b, float* o)   /* o = a * b (column-major) */
{
    float t[16];
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++) t[c * 4 + r] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] + a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
    memcpy(o, t, sizeof(t));
}
void imp_world(const ImpModel* m, float (*W)[16])
{
    char done[IMP_NODES] = { 0 };
    for (int pass = 0, left = m->nn; left && pass <= m->nn; pass++)   /* (a parent may come after its child) */
        for (int i = 0; i < m->nn; i++) {
            int p = m->node[i].parent;
            float l[16];
            if (done[i] || (p >= 0 && !done[p] && pass < m->nn)) continue;
            mat_from(&m->node[i], l);
            if (p >= 0 && done[p]) mul(W[p], l, W[i]);
            else memcpy(W[i], l, sizeof(l));
            done[i] = 1;
            left--;
        }
}
#define world imp_world

/* ---------------------------------------------------------------- glTF */
typedef struct Tri { Buf* idx[IMP_TEX + 1]; } Tri;
static void tri_put(int a, int b, int c, int tex, void* ud)
{
    Tri* t = ud;
    Buf* o = t->idx[tex >= 0 && tex < IMP_TEX ? tex : IMP_TEX];
    put32(o, (uint32_t)a); put32(o, (uint32_t)b); put32(o, (uint32_t)c);
}

typedef struct Gl { Buf js, bin; int views, accs; } Gl;

static int view(Gl* g, const void* d, size_t n, int target)
{
    align(&g->bin, 4, 0);
    size_t at = g->bin.n;
    put(&g->bin, d, n);
    pr(&g->js, "%s{\"buffer\":0,\"byteOffset\":%u,\"byteLength\":%u%s}", g->views ? "," : "", (unsigned)at, (unsigned)n,
       target == 1 ? ",\"target\":34962" : target == 2 ? ",\"target\":34963" : "");
    return g->views++;
}

int imp_write_glb(const ImpModel* m, const char* path, const char* name, float scale, char* err, int en)
{
    Gl g = { 0 };
    Buf acc = { 0 }, data = { 0 }, ibm = { 0 };
    Buf tb[IMP_TEX + 1];
    Tri t;
    float (*W)[16] = calloc(m->nn ? m->nn : 1, sizeof(*W)), mn[3] = { 1e30f, 1e30f, 1e30f }, mx[3] = { -1e30f, -1e30f, -1e30f };
    int pos, nrm, uv, jn, wt, ok = 0, prim = 0, img[IMP_TEX], mat[IMP_TEX + 1], nmat = 0;
    if (!W || !m->nv || !m->nn) { free(W); return fail(err, en, "no geometry"); }
    memset(tb, 0, sizeof(tb));
    for (int k = 0; k <= IMP_TEX; k++) t.idx[k] = &tb[k];
    each_tri(m, tri_put, &t);
    world(m, W);
    for (int i = 0; i < m->nv; i++)
        for (int k = 0; k < 3; k++) { mn[k] = fminf(mn[k], m->pos[i][k]); mx[k] = fmaxf(mx[k], m->pos[i][k]); }

    /* buffer views and accessors (JSON collected in `acc`, views in g.js) */
    pr(&g.js, "\"bufferViews\":[");
    {
        unsigned char (*j4)[4] = malloc((size_t)m->nv * 4);
        float (*w4)[4] = malloc((size_t)m->nv * 16);
        if (!j4 || !w4) { free(j4); free(w4); free(W); return fail(err, en, "out of memory"); }
        for (int i = 0; i < m->nv; i++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += m->wgt[i][k];
            for (int k = 0; k < 4; k++) { j4[i][k] = m->wgt[i][k] > 0 ? m->jnt[i][k] : 0; w4[i][k] = s > 0 ? m->wgt[i][k] / s : k == 0; }
        }
        pos = view(&g, m->pos, (size_t)m->nv * 12, 1);
        nrm = view(&g, m->nrm, (size_t)m->nv * 12, 1);
        uv = view(&g, m->uv, (size_t)m->nv * 8, 1);
        jn = view(&g, j4, (size_t)m->nv * 4, 1);
        wt = view(&g, w4, (size_t)m->nv * 16, 1);
        free(j4); free(w4);
    }
    pr(&acc, "\"accessors\":[");
    pr(&acc, "{\"bufferView\":%d,\"componentType\":5126,\"count\":%d,\"type\":\"VEC3\",\"min\":[%g,%g,%g],\"max\":[%g,%g,%g]}", pos, m->nv, mn[0], mn[1], mn[2], mx[0], mx[1], mx[2]);
    pr(&acc, ",{\"bufferView\":%d,\"componentType\":5126,\"count\":%d,\"type\":\"VEC3\"}", nrm, m->nv);
    pr(&acc, ",{\"bufferView\":%d,\"componentType\":5126,\"count\":%d,\"type\":\"VEC2\"}", uv, m->nv);
    pr(&acc, ",{\"bufferView\":%d,\"componentType\":5121,\"count\":%d,\"type\":\"VEC4\"}", jn, m->nv);
    pr(&acc, ",{\"bufferView\":%d,\"componentType\":5126,\"count\":%d,\"type\":\"VEC4\"}", wt, m->nv);
    g.accs = 5;
    for (int i = 0; i < m->nn; i++) {   /* inverse bind matrices: the inverse of each rigid world rest matrix */
        const float* a = W[i];
        float inv[16] = { a[0], a[4], a[8], 0, a[1], a[5], a[9], 0, a[2], a[6], a[10], 0, 0, 0, 0, 1 };
        for (int r = 0; r < 3; r++) inv[12 + r] = -(inv[r] * a[12] + inv[4 + r] * a[13] + inv[8 + r] * a[14]);
        put(&ibm, inv, sizeof(inv));
    }
    int ibv = view(&g, ibm.p, ibm.n, 0), iba = g.accs++;
    pr(&acc, ",{\"bufferView\":%d,\"componentType\":5126,\"count\":%d,\"type\":\"MAT4\"}", ibv, m->nn);
    int ia[IMP_TEX + 1];
    for (int k = 0; k <= IMP_TEX; k++) {
        ia[k] = -1;
        if (!tb[k].n) continue;
        int v = view(&g, tb[k].p, tb[k].n, 2);
        ia[k] = g.accs++;
        pr(&acc, ",{\"bufferView\":%d,\"componentType\":5125,\"count\":%u,\"type\":\"SCALAR\"}", v, (unsigned)(tb[k].n / 4));
    }
    for (int k = 0; k < m->ntex && k < IMP_TEX; k++) {   /* PNG images */
        Buf p = { 0 };
        img[k] = png(m, k, NULL, &p) ? view(&g, p.p, p.n, 0) : -1;
        free(p.p);
    }
    pr(&g.js, "],");
    pr(&acc, "],");

    /* the document */
    Buf js = { 0 };
    pr(&js, "{\"asset\":{\"version\":\"2.0\",\"generator\":\"recvx-enhanced-port import\"},\"scene\":0,");
    pr(&js, "\"scenes\":[{\"nodes\":[%d]}],\"nodes\":[", m->nn + 1);
    for (int i = 0; i < m->nn; i++) {
        const ImpNode* n = &m->node[i];
        int first = 1;
        pr(&js, "%s{\"name\":\"joint%d\",\"translation\":[%g,%g,%g],\"rotation\":[%g,%g,%g,%g]", i ? "," : "", i,
           n->t[0], n->t[1], n->t[2], n->q[0], n->q[1], n->q[2], n->q[3]);
        for (int c = 0; c < m->nn; c++)
            if (m->node[c].parent == i) { pr(&js, "%s%d", first ? ",\"children\":[" : ",", c); first = 0; }
        pr(&js, "%s}", first ? "" : "]");
    }
    pr(&js, ",{\"name\":\"%s_mesh\",\"mesh\":0,\"skin\":0}", name);
    pr(&js, ",{\"name\":\"%s\",\"scale\":[%g,%g,%g],\"children\":[%d", name, scale, scale, scale, m->nn);
    for (int i = 0; i < m->nn; i++) if (m->node[i].parent < 0) pr(&js, ",%d", i);
    pr(&js, "]}],\"skins\":[{\"inverseBindMatrices\":%d,\"joints\":[", iba);
    for (int i = 0; i < m->nn; i++) pr(&js, "%s%d", i ? "," : "", i);
    pr(&js, "]}],\"meshes\":[{\"name\":\"%s\",\"primitives\":[", name);
    for (int k = 0; k <= IMP_TEX; k++) {
        mat[k] = -1;
        if (ia[k] < 0) continue;
        mat[k] = nmat++;
        pr(&js, "%s{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2,\"JOINTS_0\":3,\"WEIGHTS_0\":4},\"indices\":%d,\"material\":%d}",
           prim++ ? "," : "", ia[k], mat[k]);
    }
    pr(&js, "]}],\"materials\":[");
    for (int k = 0, f = 1; k <= IMP_TEX; k++) {
        if (mat[k] < 0) continue;
        int tex = k < IMP_TEX && k < m->ntex && img[k] >= 0;
        pr(&js, "%s{\"name\":\"tex%d\",\"doubleSided\":true,\"alphaMode\":\"MASK\",\"alphaCutoff\":0.25,\"pbrMetallicRoughness\":{\"metallicFactor\":0,\"roughnessFactor\":1", f ? "" : ",", k);
        if (tex) pr(&js, ",\"baseColorTexture\":{\"index\":%d}", k);
        pr(&js, "}}");
        f = 0;
    }
    pr(&js, "]");
    if (m->ntex) {
        pr(&js, ",\"samplers\":[{\"magFilter\":9729,\"minFilter\":9729,\"wrapS\":10497,\"wrapT\":10497}],\"textures\":[");
        for (int k = 0; k < m->ntex && k < IMP_TEX; k++) pr(&js, "%s{\"sampler\":0,\"source\":%d}", k ? "," : "", k);
        pr(&js, "],\"images\":[");
        for (int k = 0; k < m->ntex && k < IMP_TEX; k++)
            if (img[k] >= 0) pr(&js, "%s{\"bufferView\":%d,\"mimeType\":\"image/png\"}", k ? "," : "", img[k]);
            else pr(&js, "%s{\"uri\":\"data:image/png;base64,\"}", k ? "," : "");   /* (an unsupported picture: an empty image) */
        pr(&js, "]");
    }
    pr(&js, ",");
    put(&js, acc.p, acc.n);
    put(&js, g.js.p, g.js.n);
    align(&g.bin, 4, 0);
    pr(&js, "\"buffers\":[{\"byteLength\":%u}]}", (unsigned)g.bin.n);
    align(&js, 4, ' ');

    /* GLB: header, JSON chunk, BIN chunk */
    if (!js.bad && !g.bin.bad && !acc.bad && !g.js.bad) {
        put32(&data, 0x46546C67); put32(&data, 2); put32(&data, (uint32_t)(12 + 8 + js.n + 8 + g.bin.n));
        put32(&data, (uint32_t)js.n); put32(&data, 0x4E4F534A); put(&data, js.p, js.n);
        put32(&data, (uint32_t)g.bin.n); put32(&data, 0x004E4942); put(&data, g.bin.p, g.bin.n);
        ok = !data.bad && save(path, data.p, data.n);
        if (!ok) fail(err, en, "cannot write %s", path);
    } else fail(err, en, "out of memory");
    for (int k = 0; k <= IMP_TEX; k++) free(tb[k].p);
    free(js.p); free(acc.p); free(g.js.p); free(g.bin.p); free(data.p); free(ibm.p); free(W);
    return ok;
}

/* ---------------------------------------------------------------- .cvxm (the port's player model) */
#define CV_MAX 64

/* a TIM2 picture as the game's texture chunk: 32-byte chunk header, the 0x80-byte file header of
   Code: Veronica's TIM2 (Gindex at 8), the picture header (format 3: loaded without a palette
   list), the pixels at 0x100 and the CLUT after them (ps2_texture.c bhSetMemPvpTexture) */
static int cv_tex(Buf* b, const unsigned char* t, int len, unsigned gindex, int* alpha)
{
    Pic p;
    if (!pic(t, len, &p) || (p.itype != 4 && p.itype != 5) || (p.ctype & 0x3f) != 3) return 0;
    uint32_t clut = p.itype == 5 ? 1024 : 64;
    if (p.clut < clut || p.img < (uint32_t)p.w * p.h * (p.itype == 5 ? 2 : 1) / 2) return 0;
    *alpha = 0;
    for (uint32_t i = 0; i < (uint32_t)p.w * p.h && !*alpha; i++) {   /* a colour the picture uses that is not opaque */
        int k = p.itype == 5 ? p.px[i] : (p.px[i / 2] >> (4 * (i & 1))) & 15;
        if (p.itype == 5 && !(p.ctype & 0x80)) k = (k & ~0x18) | ((k & 0x08) << 1) | ((k & 0x10) >> 1);
        *alpha = p.pal[4 * k + 3] < 0x80;
    }
    unsigned char h[0x100] = { 0 };
    put32(b, 0x324D4954); put32(b, 0x100 + p.img + clut);
    for (int i = 0; i < 6; i++) put32(b, 0x44434443);   /* (as the game's files: "CDCD" fill) */
    memcpy(h, "TIM2", 4);
    h[4] = 4; h[5] = 1; h[6] = 1;
    memcpy(h + 8, &gindex, 4);
    h[12] = 1; h[13] = 3;
    uint32_t total = 0x80 + p.img + clut;
    uint16_t hs = 0x80, nc = p.itype == 5 ? 256 : 16;
    memcpy(h + 0x80, &total, 4); memcpy(h + 0x84, &clut, 4); memcpy(h + 0x88, &p.img, 4);
    memcpy(h + 0x8C, &hs, 2); memcpy(h + 0x8E, &nc, 2);
    h[0x90] = 3; h[0x91] = 1; h[0x92] = p.ctype; h[0x93] = p.itype;
    memcpy(h + 0x94, &p.w, 2); memcpy(h + 0x96, &p.h, 2);
    memcpy(h + 0x98, &p.tex0, 8); memcpy(h + 0xA0, &p.tex1, 8); memcpy(h + 0xA8, &p.regs, 4); memcpy(h + 0xAC, &p.texclut, 4);
    memcpy(h + 0xB0, "eXt\0\x45\0\0\0\x20\0\0\0", 12);
    put(b, h, sizeof(h));
    put(b, p.px, p.img);
    put(b, p.pal, clut);
    return !b->bad;
}

int imp_write_cvxm(const ImpModel* m, const char* path, float scale, unsigned gindex, const ImpFit* fit, char* err, int en)
{
    int map[IMP_NODES], kept[CV_MAX], par[CV_MAX], nk = 0, order[CV_MAX], no = 0, ok = 0;
    float (*W)[16] = calloc(m->nn ? m->nn : 1, sizeof(*W)), rest[CV_MAX][3];
    Buf f = { 0 }, mlb = { 0 }, plist = { 0 };
    int alpha[IMP_TEX] = { 0 }, texok[IMP_TEX] = { 0 };
    CvxmSkin* sk = NULL;
    if (!W) return fail(err, en, "out of memory");
    world(m, W);

    /* the kept skeleton: a merged node's weights go to its nearest kept ancestor */
    for (int i = 0; i < m->nn; i++) {
        int a = i;
        while (a >= 0 && !m->node[a].keep) a = m->node[a].parent;
        if (a < 0) { free(W); return fail(err, en, "node %d has no kept ancestor", i); }
        if (a == i) {
            if (nk == CV_MAX) { free(W); return fail(err, en, "more than %d nodes", CV_MAX); }
            kept[nk] = i; map[i] = nk++;
        } else map[i] = -1;
    }
    for (int i = 0; i < m->nn; i++) if (map[i] < 0) { int a = m->node[i].parent; while (!m->node[a].keep) a = m->node[a].parent; map[i] = map[a]; }
    for (int k = 0; k < nk; k++) {
        int a = m->node[kept[k]].parent;
        par[k] = a < 0 ? -1 : map[a];
    }
    /* pre-order, children by index: the root, the first kept node below it (the pelvis), ... */
    {
        int st[CV_MAX], sp = 0;
        for (int k = nk - 1; k >= 0; k--) if (par[k] < 0) st[sp++] = k;
        while (sp && no < CV_MAX) {
            int k = st[--sp];
            order[no++] = k;
            for (int c = nk - 1; c >= 0; c--) if (par[c] == k && sp < CV_MAX) st[sp++] = c;
        }
    }
    if (no != nk || nk < 10) { free(W); return fail(err, en, "%d nodes kept, not a humanoid skeleton", nk); }
    int slot[CV_MAX];   /* kept index -> position in the object array */
    for (int i = 0; i < no; i++) slot[order[i]] = i;
    for (int i = 0; i < no; i++) {   /* game space: turned 180 degrees about y, scaled */
        const float* w = W[kept[order[i]]];
        rest[i][0] = -w[12] * scale; rest[i][1] = w[13] * scale; rest[i][2] = -w[14] * scale;
    }
    float shift[CV_MAX][3];   /* the fit: per kept node its own move plus its ancestors' */
    memset(shift, 0, sizeof(shift));
    for (int f = 0; fit && f < fit->n && f < IMP_MOVES; f++) {
        int a = fit->move[f].node;
        if (a < 0 || a >= m->nn) continue;
        int top = slot[map[a]];
        for (int i = 0; i < no; i++) {   /* (i is below top when top is on its parent chain) */
            int q = i;
            while (q >= 0 && q != top) q = par[order[q]] >= 0 ? slot[par[order[q]]] : -1;
            if (q == top) for (int c = 0; c < 3; c++) shift[i][c] += fit->move[f].d[c];
        }
    }
    for (int i = 0; i < no; i++) for (int c = 0; c < 3; c++) rest[i][c] += shift[i][c];

    /* skin: per vertex up to 4 (node, weight) of the kept skeleton */
    if (!(sk = calloc(m->nv, sizeof(*sk)))) { free(W); return fail(err, en, "out of memory"); }
    for (int v = 0; v < m->nv; v++) {
        float acc[CV_MAX] = { 0 }, s = 0;
        for (int k = 0; k < 4; k++)
            if (m->wgt[v][k] > 0 && m->jnt[v][k] < m->nn) acc[slot[map[m->jnt[v][k]]]] += m->wgt[v][k];
        for (int k = 0; k < 4; k++) {
            int best = -1;
            for (int j = 0; j < no; j++) if (acc[j] > 0 && (best < 0 || acc[j] > acc[best])) best = j;
            if (best < 0) break;
            sk[v].node[k] = (unsigned char)best; sk[v].w[k] = acc[best]; s += acc[best]; acc[best] = 0;
        }
        if (s <= 0) { sk[v].node[0] = 1; sk[v].w[0] = s = 1; }   /* (unweighted: the pelvis) */
        for (int k = 0; k < 4; k++) sk[v].w[k] /= s;
        sk[v].pos[0] = -m->pos[v][0] * scale; sk[v].pos[1] = m->pos[v][1] * scale; sk[v].pos[2] = -m->pos[v][2] * scale;
        for (int k = 0; k < 4; k++) for (int c = 0; c < 3; c++) sk[v].pos[c] += sk[v].w[k] * shift[sk[v].node[k]][c];
        sk[v].nrm[0] = -m->nrm[v][0]; sk[v].nrm[1] = m->nrm[v][1]; sk[v].nrm[2] = -m->nrm[v][2];
    }

    /* the MLB body (offsets relative to it; it starts 80 bytes into the file, 16-aligned) */
    int ntex = m->ntex < IMP_TEX ? m->ntex : IMP_TEX;
    put32(&mlb, 8); put32(&mlb, (uint32_t)ntex);   /* NJS_TEXLIST */
    size_t names = mlb.n;
    for (int k = 0; k < ntex; k++) { put32(&mlb, 0); put32(&mlb, 0); put32(&mlb, 0); }
    for (int k = 0; k < ntex; k++) { set32(&mlb, names + 12 * k, (uint32_t)mlb.n); pr(&mlb, "imp%02d", k); put(&mlb, "", 1); }
    align(&mlb, 16, 0);
    size_t objs = mlb.n;
    put(&mlb, NULL, (size_t)no * 0x34);
    align(&mlb, 16, 0);
    size_t model = mlb.n;
    put(&mlb, NULL, 24);
    align(&mlb, 64, 0);
    size_t vlist = mlb.n;
    {   /* vertex chunk 51 (njCnkCvVnPs2): a 64-byte header, then per vertex position and normal (w = 1),
           the rest pose in the pelvis's frame; actor_glue.c rewrites them every frame */
        uint32_t words = (60 + 32 * (uint32_t)m->nv) / 4;
        if (words > 0xFFFF || m->nv > 8000) { free(W); free(sk); free(mlb.p); return fail(err, en, "%d vertices: too many for one chunk", m->nv); }
        unsigned char hd[64] = { 51, 0 };
        uint16_t sz = (uint16_t)words, ofs = 0, nv = (uint16_t)m->nv;
        memcpy(hd + 2, &sz, 2); memcpy(hd + 4, &ofs, 2); memcpy(hd + 6, &nv, 2);
        put(&mlb, hd, 64);
        for (int v = 0; v < m->nv; v++) {
            for (int k = 0; k < 3; k++) putf(&mlb, sk[v].pos[k] - rest[1][k]);
            putf(&mlb, 1);
            for (int k = 0; k < 3; k++) putf(&mlb, sk[v].nrm[k]);
            putf(&mlb, 1);
        }
    }
    size_t pl = mlb.n;
    /* polygons: per texture a material (diffuse, ambient grey as the game's player), the texture and
       its strips (CsUvh: index, u, v; 1023 = 1.0; both sides drawn). Strips that show see-through
       texels (hair strands, lashes) come last, alpha-blended, after all opaque ones: drawn first they
       would hide what lies behind them (the face under the fringe). */
    unsigned char* see = calloc(m->ns ? m->ns : 1, 1);
    for (int k = 0; k < ntex; k++) {
        Buf tmp = { 0 };
        int w, h;
        unsigned char* px;
        texok[k] = cv_tex(&tmp, m->tim2[k], m->tim2_len[k], gindex + k, &alpha[k]);
        free(tmp.p);
        if (!alpha[k] || !see || !imp_tim2_rgba(m->tim2[k], m->tim2_len[k], &w, &h, &px)) continue;
        for (int s = 0; s < m->ns; s++) {   /* the texels under the strip's UV box, in its tile */
            const ImpStrip* st = &m->strip[s];
            if (st->tex != k) continue;
            float u0 = 1e30f, v0 = 1e30f, u1 = -1e30f, v1 = -1e30f;
            for (int i = 0; i < st->n; i++) {
                const float* t = m->uv[m->idx[st->first + i]];
                u0 = fminf(u0, t[0]); u1 = fmaxf(u1, t[0]); v0 = fminf(v0, t[1]); v1 = fmaxf(v1, t[1]);
            }
            int x0 = (int)floorf(u0 * w), x1 = (int)ceilf(u1 * w), y0 = (int)floorf(v0 * h), y1 = (int)ceilf(v1 * h);
            if (x1 - x0 > w) x1 = x0 + w;
            if (y1 - y0 > h) y1 = y0 + h;
            for (int y = y0; y < y1 && !see[s]; y++)
                for (int x = x0; x < x1 && !see[s]; x++) see[s] = px[4 * (((y % h + h) % h) * w + (x % w + w) % w) + 3] < (m->see_cut ? m->see_cut : 255);
        }
        free(px);
    }
    for (int pass = 0; pass < 2; pass++)
    for (int k = -1; k < ntex; k++) {
        int nstrip = 0;
        for (int s = 0; s < m->ns; s++) nstrip += m->strip[s].tex == k && m->strip[s].n >= 3 && (see && see[s]) == pass;
        if (!nstrip) continue;
        static const unsigned char mat[12] = { 0x13, 0x25, 4, 0, 0x7f, 0x7f, 0x7f, 0xff, 0x7f, 0x7f, 0x7f, 0xff };
        put(&plist, mat, sizeof(mat));
        if (k >= 0) { put(&plist, "\x08\x04", 2); put16(&plist, (uint16_t)(0x4000 | k)); }
        int s = 0;
        while (s < m->ns) {   /* one chunk of strips, at most ~60000 shorts */
            size_t head = plist.n;
            uint16_t cnt = 0;
            put(&plist, NULL, 6);
            for (; s < m->ns && plist.n - head < 120000 && cnt < 0x3FFF; s++) {
                const ImpStrip* st = &m->strip[s];
                if (st->tex != k || st->n < 3 || st->n > 32767 || (see && see[s]) != pass) continue;
                float fu = 1e30f, fv = 1e30f;
                for (int i = 0; i < st->n; i++) { fu = fminf(fu, m->uv[m->idx[st->first + i]][0]); fv = fminf(fv, m->uv[m->idx[st->first + i]][1]); }
                fu = floorf(fu); fv = floorf(fv);   /* the strip moved by whole tiles (the textures repeat) */
                put16(&plist, (uint16_t)st->n);
                for (int i = 0; i < st->n; i++) {
                    int x = m->idx[st->first + i];
                    float u = (m->uv[x][0] - fu) * 1023.0f + 0.5f, v = (m->uv[x][1] - fv) * 1023.0f + 0.5f;
                    put16(&plist, (uint16_t)x);
                    put16(&plist, (uint16_t)(u < 0 ? 0 : u > 65535 ? 65535 : u));
                    put16(&plist, (uint16_t)(v < 0 ? 0 : v > 65535 ? 65535 : v));
                }
                cnt++;
            }
            if (!cnt) { plist.n = head; continue; }
            uint16_t words = (uint16_t)((plist.n - head - 4) / 2);
            plist.p[head] = 0x42;   /* NJD_CS_UVH */
            plist.p[head + 1] = 0x12 | (pass ? 0x08 : 0);   /* ignore specular, double side (+ use alpha) */
            memcpy(plist.p + head + 2, &words, 2);
            memcpy(plist.p + head + 4, &cnt, 2);
        }
    }
    free(see);
    put(&plist, "\xFF\0\0\0", 4);
    put(&mlb, plist.p, plist.n);
    free(plist.p);
    {   /* the model on the pelvis (node 1): centre and radius for the view test, generous (it is skinned) */
        float ext = 0;
        for (int v = 0; v < m->nv; v++) for (int k = 0; k < 3; k++) ext = fmaxf(ext, fabsf(sk[v].pos[k] - rest[1][k]));
        int32_t vo = (int32_t)vlist, po = (int32_t)pl;
        float c[4] = { 0, 0, 0, -(ext * 1.5f + 1.0f) };   /* (negative: bhCnkBinRealize relocates) */
        if (!mlb.bad) { memcpy(mlb.p + model, &vo, 4); memcpy(mlb.p + model + 4, &po, 4); memcpy(mlb.p + model + 8, c, 16); }
    }
    for (int i = 0; i < no && !mlb.bad; i++) {   /* NJS_CNK_OBJECT: evalflags, model, pos, ang, scl, child, sibling */
        unsigned char* o = mlb.p + objs + 0x34 * i;
        int k = order[i], child = -1, sib = -1, p = par[k];
        /* (pre-order: the first later node of the same parent is the next sibling) */
        for (int j = i + 1; j < no && child < 0; j++) if (par[order[j]] == k) child = j;
        for (int j = i + 1; j < no && sib < 0; j++) if (par[order[j]] == p) sib = j;
        uint32_t ev = i == 1 ? 0x6 : (child < 0 ? 0x1e : 0xe) | (i == 0 ? 0x1 : 0);
        int32_t mo = i == 1 ? (int32_t)model : -1, co = child >= 0 ? (int32_t)(objs + 0x34 * child) : -1, so = sib >= 0 ? (int32_t)(objs + 0x34 * sib) : -1;
        float pos[3], scl[3] = { 1, 1, 1 };
        for (int j = 0; j < 3; j++) pos[j] = rest[i][j] - (p >= 0 ? rest[slot[p]][j] : 0);
        memcpy(o, &ev, 4); memcpy(o + 4, &mo, 4); memcpy(o + 8, pos, 12); memset(o + 20, 0, 12); memcpy(o + 32, scl, 12);
        memcpy(o + 44, &co, 4); memcpy(o + 48, &so, 4);
    }

    /* the file */
    put32(&f, CVXM_MAGIC); put32(&f, CVXM_VERSION); put32(&f, 60); put32(&f, 0); put32(&f, 0); put32(&f, (uint32_t)m->nv); put32(&f, (uint32_t)no);
    {   /* the face bones: their places in the object array */
        unsigned char face[IMP_FACES - 1];
        memset(face, 255, sizeof(face));
        for (int i = 0; i < no; i++) {
            int r = m->node[kept[order[i]]].face;
            if (r > 0 && r < IMP_FACES) face[r - 1] = (unsigned char)i;
        }
        put(&f, face, sizeof(face));
    }
    align(&f, 36, 0);
    for (int k = 0; k < 2; k++) { float t = m->lid_travel[k] * scale; uint32_t u; memcpy(&u, &t, 4); put32(&f, u); }
    align(&f, 60, 0);
    put32(&f, (uint32_t)(16 + mlb.n));
    {   /* MLB header: "MDL", status 0xbc (a chunk model, as the game's), header size, objects, texlist, objects */
        unsigned char h[16] = { 'M', 'D', 'L', 0xbc, 16, 0 };
        uint16_t on = (uint16_t)no;
        int32_t to = ntex ? 0 : -1, oo = (int32_t)objs;
        memcpy(h + 6, &on, 2); memcpy(h + 8, &to, 4); memcpy(h + 12, &oo, 4);
        put(&f, h, 16);
    }
    put(&f, mlb.p, mlb.n);
    put32(&f, 0xFFFFFFFF);
    align(&f, 64, 0);
    set32(&f, 12, (uint32_t)f.n);
    for (int k = 0; k < ntex; k++) {
        int a;
        if (!texok[k] || !cv_tex(&f, m->tim2[k], m->tim2_len[k], gindex + k, &a)) {
            free(W); free(sk); free(mlb.p); free(f.p);
            return fail(err, en, "texture %d is not a 4/8-bit picture with a 32-bit palette", k);
        }   /* (chunks follow each other without padding: the loader steps by 32 + size) */
    }
    put32(&f, 0xFFFFFFFF); put32(&f, 0); put(&f, NULL, 24);
    align(&f, 16, 0);
    set32(&f, 16, (uint32_t)f.n);
    put(&f, sk, sizeof(*sk) * (size_t)m->nv);
    PcCvxm cv;
    char why[96];
    if (f.bad || mlb.bad) fail(err, en, "out of memory");
    else if (f.n > UINT32_MAX || !pc_cvxm_check(f.p, (uint32_t)f.n, &cv, why, sizeof(why))) fail(err, en, "not loadable: %s", why);   /* (as the game will check it) */
    else if (!(ok = save(path, f.p, f.n))) fail(err, en, "cannot write %s", path);
    free(W); free(sk); free(mlb.p); free(f.p);
    return ok;
}
