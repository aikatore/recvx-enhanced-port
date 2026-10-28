/* Resident Evil Outbreak File #2 (SLUS-20984): character models from the user's disc image
   (docs/IMPORT_OUTBREAK.md "Disc format"). Our own reading of the formats; the structure names
   follow the public notes of the Outbreak modding community (Fothsid's libthanatos/OBTool, GPL,
   used as a reference only, no code taken).

   Disc: ISO 9660; NETBIO00.DAT is an AFS archive whose first file, romdata_usa.afs, is an AFS of
   the common data: P<chr>_<costume>.nbd the playable characters, N*.NBD / HN*.NBD NPCs, E*.NBD
   enemies. An NBD: a table of (type, offset, size, count) entries: TEX (SLD-compressed TIM2
   pictures), AMO (model: meshes, materials, texture list) and AHI (node hierarchy), SDW (shadow). */
#include "import.h"
#include "impmodel.h"
#include "../host/fileio.h"
#include "../../include/pc_parse.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTOR 2048
#define NBD_VERTS (1 << 20)      /* a model's vertices and strip indices, at most (far above Outbreak's) */
#define NBD_INDICES (1 << 24)

int64_t imp_read_at(FILE* f, int64_t off, void* buf, size_t n)
{
    if (fseeko(f, (off_t)off, SEEK_SET) != 0) return 0;
    return fread(buf, 1, n, f) == n;
}
static uint32_t le32(const unsigned char* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* ---------------------------------------------------------------- ISO 9660
   `path`: a file of the root, or folders and the file with '/' between them (ENG/SYSMES1.ALD); any case */
int imp_iso_find(FILE* f, const char* path, int64_t* off, uint32_t* size)
{
    unsigned char pvd[SECTOR], *dir;
    if (!imp_read_at(f, 16 * SECTOR, pvd, SECTOR) || pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5)) return 0;
    uint32_t lba = le32(pvd + 156 + 2), len = le32(pvd + 156 + 10);
    for (;;) {
        const char* slash = strchr(path, '/');
        size_t want = slash ? (size_t)(slash - path) : strlen(path);
        int found = 0;
        if (len > (1 << 20) || !(dir = malloc(len))) return 0;
        if (imp_read_at(f, (int64_t)lba * SECTOR, dir, len))
            for (uint32_t i = 0; i < len && !found;) {
                int l = dir[i];
                if (!l) { i = (i / SECTOR + 1) * SECTOR; continue; }
                if (i + l > len || l < 34) break;
                int nl = dir[i + 32];
                if (nl && 33 + nl <= l) {
                    char n[64] = { 0 };
                    memcpy(n, dir + i + 33, nl < 63 ? nl : 63);
                    char* sc = strchr(n, ';');
                    if (sc) *sc = 0;
                    if (strlen(n) == want && !SDL_strncasecmp(n, path, want) && !(dir[i + 25] & 2) == !slash) {
                        lba = le32(dir + i + 2);
                        len = le32(dir + i + 10);
                        found = 1;
                    }
                }
                i += l;
            }
        free(dir);
        if (!found) return 0;
        if (!slash) { *off = (int64_t)lba * SECTOR; *size = len; return 1; }
        path = slash + 1;
    }
}

/* ---------------------------------------------------------------- AFS */

/* the files of the AFS archive at `base` (`size` bytes; malloc'd), with their names if it has a name
   table; an entry outside the archive gets size 0 (not read) */
AfsFile* imp_afs_list(FILE* f, int64_t base, uint64_t size, int* count)
{
    unsigned char h[8];
    int c;
    if (!imp_read_at(f, base, h, 8) || memcmp(h, "AFS", 4) || (c = pc_afs_count(h, size)) <= 0 || !pc_range_ok(8, (uint64_t)c * 8 + 8, size)) return NULL;
    uint32_t n = (uint32_t)c;
    unsigned char* t = malloc((size_t)(n + 1) * 8 + 8);
    AfsFile* out = calloc(n, sizeof(*out));
    if (!t || !out || !imp_read_at(f, base, t, (size_t)(n + 1) * 8 + 8)) { free(t); free(out); return NULL; }
    uint32_t first = 0xFFFFFFFF;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t o, l;
        if (!pc_afs_entry(t, i, size, &o, &l)) o = l = 0;
        out[i].off = base + o;
        out[i].size = l;
        if (o && o < first) first = o;
    }
    uint32_t noff = le32(t + 8 + 8 * n), nsz = le32(t + 8 + 8 * n + 4);
    unsigned char p[8];
    if ((nsz != n * 48 || !noff) && first != 0xFFFFFFFF && first >= 16 && imp_read_at(f, base + first - 8, p, 8)) { noff = le32(p); nsz = le32(p + 4); }
    unsigned char* nt = nsz == n * 48 && noff && pc_range_ok(noff, nsz, size) ? malloc(nsz) : NULL;
    if (nt && imp_read_at(f, base + noff, nt, nsz))
        for (uint32_t i = 0; i < n; i++) memcpy(out[i].name, nt + 48 * i, 31);
    else
        for (uint32_t i = 0; i < n; i++) snprintf(out[i].name, sizeof(out[i].name), "#%u", i);
    free(nt);
    free(t);
    *count = (int)n;
    return out;
}

/* ---------------------------------------------------------------- SLD (the textures' compression)
   16-bit words. A flag word gives the kind of the next 16 items, highest bit first: 0 a literal
   word; 1 a copy word c: count = c >> 11 (0: the count follows), distance back = c & 0x7ff
   (0: that many zero words). Returns the output size in bytes, -1 if the input is broken. */
int ob2_sld(const unsigned char* src, int n, unsigned char* dst, int cap)
{
    int sn = n / 2, dn = cap / 2, i = 0, o = 0, bits = 0;   /* (16-bit words at any alignment) */
#define S(k) pc_rd16(src + 2 * (k))
    unsigned flag = 0;
    while (i < sn) {
        if (!bits) { flag = S(i); i++; bits = 16; if (i >= sn) break; }
        if (flag & 0x8000) {
            unsigned c = S(i), cnt = c >> 11, dist = c & 0x7ff;
            i++;
            if (!cnt) { if (i >= sn) return -1; cnt = S(i); i++; }
            if (!cnt && !dist) break;   /* the end marker */
            if (dist > (unsigned)o) return -1;
            for (unsigned k = 0; k < cnt; k++, o++) {
                if (o >= dn) return -1;
                if (dist) memmove(dst + 2 * o, dst + 2 * (o - dist), 2);
                else memset(dst + 2 * o, 0, 2);
            }
        } else {
            if (o >= dn) return -1;
            memcpy(dst + 2 * o, src + 2 * i, 2);
            o++; i++;
        }
        flag <<= 1;
        bits--;
    }
    return o * 2;
#undef S
}

/* ---------------------------------------------------------------- NBD */
typedef struct Rd { const unsigned char* d; uint32_t n; int bad; } Rd;
static uint32_t u32(Rd* r, uint32_t o) { if (o > r->n || r->n - o < 4) { r->bad = 1; return 0; } return le32(r->d + o); }
static float f32(Rd* r, uint32_t o) { uint32_t v = u32(r, o); float f; memcpy(&f, &v, 4); return isfinite(f) ? f : 0; }

/* chunk header: type, count, size (including the 12-byte header) */
#define T_MODEL 1
#define T_MESHLIST 2
#define T_MESH 4
#define T_PRIMS 5
#define T_MATLIST 9
#define T_TEXLIST 0xA
#define T_MATREF 0x50000
#define T_STRIPMAT 0x60000
#define T_VERT 0x70000
#define T_NORM 0x80000
#define T_UV 0xA0000
#define T_WEIGHT 0xC0000
#define T_JOINTS 0x100000
#define T_HIER 0xC0000000u

typedef struct Mesh { uint32_t at, end; } Mesh;
typedef struct Amo {
    int nmesh; Mesh mesh[64];
    int nmat; int mattex[256];   /* per material its first texture-list entry, -1 none */
    int ntexl; int texl[64];     /* texture list: the NBD's texture slot */
} Amo;

static void euler_quat(float x, float y, float z, float* q)   /* rotation x, then y, then z */
{
    float cx = cosf(x / 2), sx = sinf(x / 2), cy = cosf(y / 2), sy = sinf(y / 2), cz = cosf(z / 2), sz = sinf(z / 2);
    q[0] = sx * cy * cz - cx * sy * sz;
    q[1] = cx * sy * cz + sx * cy * sz;
    q[2] = cx * cy * sz - sx * sy * cz;
    q[3] = cx * cy * cz + sx * sy * sz;
}

static int read_ahi(Rd* r, uint32_t o, ImpModel* m, int* meshnode)
{
    uint32_t cnt = u32(r, o + 4), rs = u32(r, o + 12 + 8);
    if (u32(r, o) != T_HIER || r->bad || cnt < 2 || cnt - 1 > IMP_NODES) return 0;
    uint32_t p = o + 12 + rs;
    m->nn = (int)cnt - 1;
    for (int i = 0; i < m->nn; i++, p += u32(r, p + 8)) {
        uint32_t type = u32(r, p), b = p + 12;
        ImpNode* n = &m->node[i];
        int id = (int)u32(r, b), par = (int)u32(r, b + 4);
        if (id != i || par >= m->nn || par < -1) return 0;
        n->parent = par;
        for (int k = 0; k < 3; k++) n->t[k] = f32(r, b + 16 + 32 + 4 * k);
        float e[3];
        for (int k = 0; k < 3; k++) e[k] = f32(r, b + 16 + 16 + 4 * k) * (type == 0x40000003 ? -1.0f : 1.0f);   /* (a joint's is stored negated) */
        euler_quat(e[0], e[1], e[2], n->q);
        meshnode[i] = (int)u32(r, b + 64);
        n->keep = (int)u32(r, b + 68);   /* (the group for now: decided by the caller) */
        if (u32(r, p + 8) < 12 + 72) return 0;
    }
    for (int i = 0; i < m->nn; i++) {   /* no cycles: every chain of parents ends at a root */
        int a = i, k = 0;
        while (a >= 0 && k++ <= m->nn) a = m->node[a].parent;
        if (a >= 0) return 0;
    }
    return !r->bad;
}

static int read_amo(Rd* r, uint32_t o, Amo* a)
{
    uint32_t cnt = u32(r, o + 4), p = o + 12;
    memset(a, 0, sizeof(*a));
    if (u32(r, o) != T_MODEL) return 0;
    for (uint32_t c = 0; c < cnt && !r->bad; c++) {
        uint32_t t = u32(r, p), n = u32(r, p + 4), sz = u32(r, p + 8), q = p + 12;
        if (sz < 12 || !pc_range_ok(p, sz, r->n)) return 0;
        if (t == T_MESHLIST)
            for (uint32_t k = 0; k < n && a->nmesh < 64; k++) {
                uint32_t ms = u32(r, q + 8);
                if (ms < 12 || !pc_range_ok(q, ms, r->n) || u32(r, q) != T_MESH) return 0;
                a->mesh[a->nmesh].at = q; a->mesh[a->nmesh++].end = q + ms;
                q += ms;
            }
        else if (t == T_MATLIST)
            for (uint32_t k = 0; k < n && a->nmat < 256; k++) {
                uint32_t ms = u32(r, q + 8), nt = u32(r, q + 12 + 52);
                a->mattex[a->nmat++] = nt ? (int)u32(r, q + 12 + 56 + 200) : -1;
                if (ms < 12 || !pc_range_ok(q, ms, r->n)) return 0;
                q += ms;
            }
        else if (t == T_TEXLIST)
            for (uint32_t k = 0; k < n && a->ntexl < 64; k++) {
                uint32_t ms = u32(r, q + 8);
                a->texl[a->ntexl++] = (int)u32(r, q + 12);
                if (ms < 12 || !pc_range_ok(q, ms, r->n)) return 0;
                q += ms;
            }
        p += sz;
    }
    return !r->bad;
}

/* append mesh `me` of the AMO to the model (weights to its joints; a mesh without weights hangs
   on the node that names it, its points in that node's frame) */
static int read_mesh(Rd* r, const Mesh* me, const Amo* a, int mi, const int* meshnode, ImpModel* m, float (*W)[16])
{
    uint32_t cnt = u32(r, me->at + 4), p = me->at + 12;
    uint32_t vert = 0, nrm = 0, uv = 0, wgt = 0, jnt = 0, prims = 0, matref = 0, stripmat = 0, nv = 0, nmr = 0, nsm = 0, nj = 0;
    for (uint32_t c = 0; c < cnt && p + 12 <= me->end && !r->bad; c++) {
        uint32_t t = u32(r, p), n = u32(r, p + 4), sz = u32(r, p + 8);
        if (sz < 12 || sz > me->end - p) break;
        if (t == T_VERT) { vert = p + 12; nv = n; }
        else if (t == T_NORM && n == nv) nrm = p + 12;
        else if (t == T_UV && n == nv) uv = p + 12;
        else if (t == T_WEIGHT && n == nv) wgt = p + 12;
        else if (t == T_JOINTS) { jnt = p + 12; nj = n; }
        else if (t == T_PRIMS) prims = p;
        else if (t == T_MATREF) { matref = p + 12; nmr = n; }
        else if (t == T_STRIPMAT) { stripmat = p + 12; nsm = n; }
        p += sz;
    }
    if (!vert || !nv || !prims || nv > 65535 || m->nv > NBD_VERTS - (int)nv || r->bad) return 0;
    if (wgt && jnt && nj == 1 && nv < 40) {   /* the eyeball overlay: a few quads on the head alone (the node
        with the face bones below it), textured with a bare white eyeball; drawn, it hides the irises the face
        texture has painted (the eyes looked blind; when Outbreak shows it is not known). Left out. */
        int head = (int)u32(r, jnt);
        for (int i = 0; i < m->nn; i++)
            if (m->node[i].parent == head && m->node[i].keep == 2) { m->eyes_left_out++; return 1; }
    }
    int base = m->nv, rigid = -1;
    if (!wgt || !jnt)
        for (int i = 0; i < m->nn; i++) if (meshnode[i] == mi) rigid = i;
    void* q;
    if (!(q = realloc(m->pos, sizeof(*m->pos) * (base + nv)))) return 0;
    m->pos = q;
    if (!(q = realloc(m->nrm, sizeof(*m->nrm) * (base + nv)))) return 0;
    m->nrm = q;
    if (!(q = realloc(m->uv, sizeof(*m->uv) * (base + nv)))) return 0;
    m->uv = q;
    if (!(q = realloc(m->jnt, sizeof(*m->jnt) * (base + nv)))) return 0;
    m->jnt = q;
    if (!(q = realloc(m->wgt, sizeof(*m->wgt) * (base + nv)))) return 0;
    m->wgt = q;
    m->nv += nv;
    uint32_t wp = wgt;
    for (uint32_t i = 0; i < nv; i++) {
        float* P = m->pos[base + i], *N = m->nrm[base + i];
        for (int k = 0; k < 3; k++) { P[k] = f32(r, vert + 12 * i + 4 * k); N[k] = nrm ? f32(r, nrm + 12 * i + 4 * k) : 0; }
        m->uv[base + i][0] = uv ? f32(r, uv + 8 * i) : 0;
        m->uv[base + i][1] = uv ? f32(r, uv + 8 * i + 4) : 0;
        memset(m->jnt[base + i], 0, 4);
        memset(m->wgt[base + i], 0, 16);
        if (rigid >= 0) {   /* into model space */
            const float* w = W[rigid];
            float x = P[0], y = P[1], z = P[2], nx = N[0], ny = N[1], nz = N[2];
            for (int k = 0; k < 3; k++) {
                P[k] = w[k] * x + w[4 + k] * y + w[8 + k] * z + w[12 + k];
                N[k] = w[k] * nx + w[4 + k] * ny + w[8 + k] * nz;
            }
            m->jnt[base + i][0] = (unsigned char)rigid;
            m->wgt[base + i][0] = 1;
        } else if (wgt) {   /* (joint index into the mesh's joint list, weight in percent); the 4 largest */
            uint32_t k = u32(r, wp);
            wp += 4;
            for (uint32_t j = 0; j < k && j < 64; j++) {
                uint32_t id = u32(r, wp + 8 * j);
                float w = f32(r, wp + 8 * j + 4) / 100.0f;
                int node = id < nj ? (int)u32(r, jnt + 4 * id) : -1;
                if (node < 0 || node >= m->nn || w <= 0) continue;
                int s = 0;
                for (int x = 1; x < 4; x++) if (m->wgt[base + i][x] < m->wgt[base + i][s]) s = x;
                if (w > m->wgt[base + i][s]) { m->wgt[base + i][s] = w; m->jnt[base + i][s] = (unsigned char)node; }
            }
            wp += 8 * k;
        }
    }
    /* strips: up to two lists (single- and multi-weight); a strip's material by its order over both */
    uint32_t nl = u32(r, prims + 4), lp = prims + 12, si = 0;
    for (uint32_t l = 0; l < nl && l < 2 && !r->bad; l++) {
        uint32_t ns = u32(r, lp + 4), sp = lp + 12, lsz = u32(r, lp + 8);
        if (lsz < 12 || !pc_range_ok(lp, lsz, r->n)) return 0;
        for (uint32_t s = 0; s < ns && !r->bad; s++, si++) {
            uint32_t n = u32(r, sp) & 0x7FFFFFFF;
            sp += 4;
            if (n > 65535 || !pc_range_ok(sp, 4 * n, r->n) || m->nidx > NBD_INDICES - (int)n) return 0;
            if (!(q = realloc(m->idx, sizeof(int) * (m->nidx + n)))) return 0;
    m->idx = q;
            if (!(q = realloc(m->strip, sizeof(ImpStrip) * (m->ns + 1)))) return 0;
    m->strip = q;
            ImpStrip* st = &m->strip[m->ns++];
            st->first = m->nidx; st->n = (int)n; st->tex = -1;
            for (uint32_t k = 0; k < n; k++) {
                uint32_t x = u32(r, sp + 4 * k);
                m->idx[m->nidx++] = x < nv ? base + (int)x : base;
            }
            sp += 4 * n;
            int mr = si < nsm ? (int)u32(r, stripmat + 4 * si) : 0, mat = mr >= 0 && (uint32_t)mr < nmr ? (int)u32(r, matref + 4 * mr) : -1;
            int tl = mat >= 0 && mat < a->nmat ? a->mattex[mat] : -1;
            st->tex = tl >= 0 && tl < a->ntexl ? a->texl[tl] : -1;
            if (st->tex >= m->ntex) st->tex = -1;
        }
        lp += lsz;
    }
    return !r->bad;
}

/* model + textures of an NBD file's first AMO/AHI pair */
int ob2_read_nbd(const unsigned char* d, uint32_t n, ImpModel* m, char* err, int en)
{
    Rd r = { d, n, 0 };
    memset(m, 0, sizeof(*m));
    uint32_t hs = u32(&r, 4);   /* the table: 16 bytes per entry, ends where the textures start; all of it in the file */
    if (r.bad || u32(&r, 0) != 0x00584554u || hs < 48 || hs > 256 || hs > n) { snprintf(err, en, "Unsupported model format."); return 0; }   /* "TEX\0" */
    uint32_t amo = 0, ahi = 0;
    for (uint32_t e = 16; e + 16 <= hs; e += 16) {
        if (u32(&r, e) == 0x004F4D41u && !amo) amo = u32(&r, e + 4);   /* "AMO\0" */
        if (u32(&r, e) == 0x00494841u && !ahi) ahi = u32(&r, e + 4);   /* "AHI\0" */
    }
    if (!amo || !ahi) { snprintf(err, en, "Model data is missing."); return 0; }
    /* textures: count entries of u32 size + SLD data */
    uint32_t tp = u32(&r, 4), tc = u32(&r, 12);
    for (uint32_t k = 0; k < tc && k < IMP_TEX && !r.bad; k++) {
        uint32_t sz = u32(&r, tp);
        tp += 4;
        if (r.bad || sz > n - tp) { r.bad = 1; break; }
        int cap = 8 << 20, got;
        unsigned char* out = malloc(cap);
        if (!out || (got = ob2_sld(d + tp, (int)sz, out, cap)) < 64) { free(out); snprintf(err, en, "Texture %u is damaged.", k); imp_free(m); return 0; }
        unsigned char* fit = realloc(out, got);
        m->tim2[m->ntex] = fit ? fit : out;
        m->tim2_len[m->ntex++] = got;
        tp += sz;
    }
    Amo a;
    int meshnode[IMP_NODES];
    float (*W)[16];
    if (!read_ahi(&r, ahi, m, meshnode)) { snprintf(err, en, "Model structure is damaged."); imp_free(m); return 0; }
    if (!read_amo(&r, amo, &a)) { snprintf(err, en, "Model data is damaged."); imp_free(m); return 0; }
    if (!(W = calloc(m->nn, sizeof(*W)))) { imp_free(m); return 0; }
    imp_world(m, W);
    int meshes = 0;
    for (int i = 0; i < a.nmesh; i++) meshes += read_mesh(&r, &a.mesh[i], &a, i, meshnode, m, W);
    free(W);
    if (!meshes || !m->nv) { snprintf(err, en, "No usable model found."); imp_free(m); return 0; }
    m->nmesh = meshes;
    return 1;
}

/* ---------------------------------------------------------------- the extraction */
/* IMPORT_GROUP_* of a disc file name, -1: not a character model */
int ob2_group(const char* n)
{
    int d1 = n[1] >= '0' && n[1] <= '9', d2 = n[2] >= '0' && n[2] <= '9';
    if (n[0] == 'P' && d1) return IMPORT_GROUP_PLAYERS;
    if ((n[0] == 'E' || n[0] == 'H') && n[1] == 'P' && d2) return IMPORT_GROUP_CUTSCENE;
    if (n[0] == 'N' && d1) return IMPORT_GROUP_NPCS;
    if (n[0] == 'H' && n[1] == 'N' && (d2 || n[2] == '_')) return IMPORT_GROUP_NPCS;
    if (n[0] == 'E' && d1) return IMPORT_GROUP_ENEMIES;
    if (n[0] == 'H' && n[1] == 'E' && d2) return IMPORT_GROUP_ENEMIES;
    return -1;
}

static const char* const cast[8] = { "Kevin Ryman", "Mark Wilkins", "Jim Chapman", "George Hamilton",
                                     "David King", "Alyssa Ashcroft", "Yoko Suzuki", "Cindy Lennox" };

/* the nodes the player model keeps: the body (groups 0 lower, 1 upper body) where it bends; the
   face (2) and fingers (3, 4) and the zero-length pivots (the hips' second node, the face root)
   move with their parent */
static void keep_body(ImpModel* m)
{
    int jaw = -1;
    for (int i = 0; i < m->nn; i++) {
        ImpNode* n = &m->node[i];
        int group = n->keep;
        float l = fabsf(n->t[0]) + fabsf(n->t[1]) + fabsf(n->t[2]);
        n->keep = n->parent < 0 || (group <= 1 && l > 0.01f);
        n->face = group == 2 && l > 0.01f;   /* (a face bone: its role below) */
        if (n->face && (jaw < 0 || n->t[1] < m->node[jaw].t[1])) jaw = i;
    }
    /* the face bones (lids at the eyes, the jaw below them; all on the face root under the head):
       the lowest is the jaw; per side (x) the higher lid is the upper one. Kept only when all five are found. */
    int lid[2][2] = { { -1, -1 }, { -1, -1 } }, n = 0;
    for (int i = 0; i < m->nn; i++) {
        ImpNode* d = &m->node[i];
        if (!d->face || i == jaw) continue;
        int s = d->t[0] > 0, k = lid[s][0] < 0 ? 0 : 1;
        if (k == 1 && lid[s][1] >= 0) { n = -9; break; }
        lid[s][k] = i;
        n++;
    }
    for (int i = 0; i < m->nn; i++) m->node[i].face = 0;
    if (n != 4 || jaw < 0) return;
    for (int s = 0; s < 2; s++) {   /* (Outbreak x+ is the character's left) */
        int up = m->node[lid[s][0]].t[1] > m->node[lid[s][1]].t[1] ? lid[s][0] : lid[s][1], lo = up == lid[s][0] ? lid[s][1] : lid[s][0];
        m->node[up].face = s ? IMP_FACE_LID_UP_L : IMP_FACE_LID_UP_R;
        m->node[lo].face = s ? IMP_FACE_LID_LO_L : IMP_FACE_LID_LO_R;
    }
    m->node[jaw].face = IMP_FACE_JAW;
    for (int i = 0; i < m->nn; i++) if (m->node[i].face) m->node[i].keep = 1;
}

/* The fit to Code: Veronica's proportions (impmodel.h ImpFit). Outbreak's shoulders are wide and its
   jackets padded: the shoulder joints sit 1.76 units out at a head height of 15.5 (Kevin), Claire's 1.4
   at 15.05. On the human skeleton the arms (from the clavicle, nodes 12 and 16) are moved in by a part of
   the difference to Claire's proportion and lowered a little; the shoulders' vertices follow by their
   weights. Per model (prefix of the file name) the defaults can be changed here. */
static const struct { const char* id; float in, down; } fits[] = {
    { "", 0.8f, 0.30f },        /* everyone: most of the way to Claire's shoulder width, 3 cm lower */
    { "P05", 0.9f, 0.35f },     /* Alyssa: padded jackets */
    { "EP05", 0.9f, 0.35f },
};

/* The upper lids' weights for our blink (actor_glue.c imp_face moves the upper lid bone down). Outbreak
   weights skin up to the brow to that bone, some of it fully (Cindy: up to 3 cm above the lid), and the
   rest of the lid often only by a tenth (its blink must turn that bone differently): moved as they
   were, the forehead went down with the lid. So the weights on the upper lid bone are set from the
   height instead: fully from the lid's margin (the lowest of its vertices with the most weight: the
   middle of the lid) up to 4 mm above it, fading out at 12 mm (Outbreak's model units are
   centimetres); the rest stays on the vertex's other nodes (or goes to the bone's parent, the face
   root, which is the head): the brow and the forehead stay. The travel to close: from the margin down
   to the lower lid's highest vertex within a centimetre to the side of it. */
static void lid_falloff(ImpModel* m)
{
    const float full = 0.4f, band = 0.8f;
    for (int u = 0; u < m->nn; u++) {
        int f = m->node[u].face, side = f == IMP_FACE_LID_UP_L ? 0 : 1, lo = -1, vm = -1;
        if (f != IMP_FACE_LID_UP_L && f != IMP_FACE_LID_UP_R) continue;
        for (int i = 0; i < m->nn; i++) if (m->node[i].face == f + 1) lo = i;
        float wx = 0, ym = 1e9f, yl = -1e9f;
        for (int v = 0; v < m->nv; v++)
            for (int j = 0; j < 4; j++) if (m->jnt[v][j] == u && m->wgt[v][j] > wx) wx = m->wgt[v][j];
        for (int v = 0; v < m->nv; v++)
            for (int j = 0; j < 4; j++)
                if (m->jnt[v][j] == u && m->wgt[v][j] >= 0.95f * wx && m->pos[v][1] < ym) { ym = m->pos[v][1]; vm = v; }
        for (int v = 0; v < m->nv && vm >= 0; v++)
            for (int j = 0; j < 4; j++)
                if (m->jnt[v][j] == lo && m->wgt[v][j] >= 0.5f && fabsf(m->pos[v][0] - m->pos[vm][0]) < 1.0f && m->pos[v][1] > yl) yl = m->pos[v][1];
        m->lid_travel[side] = vm >= 0 && yl > -1e8f && ym > yl ? 0.95f * (ym - yl) : 0;
        for (int v = 0; v < m->nv && vm >= 0; v++)
            for (int j = 0; j < 4; j++) {
                if (m->jnt[v][j] != u || m->wgt[v][j] <= 0) continue;
                float t = fminf(1.0f, fmaxf(0.0f, 1.0f - (m->pos[v][1] - ym - full) / band)), others = 1.0f - m->wgt[v][j];
                int k;
                m->wgt[v][j] = t;
                if (others > 1e-4f) { for (k = 0; k < 4; k++) if (k != j) m->wgt[v][k] *= (1.0f - t) / others; }
                else if (t < 1.0f) {   /* (all of it was on the lid: the rest to the parent, in a free slot) */
                    for (k = 0; k < 4 && (k == j || m->wgt[v][k] > 0); k++) {}
                    if (k < 4) { m->jnt[v][k] = (unsigned char)m->node[u].parent; m->wgt[v][k] = 1.0f - t; }
                    else m->wgt[v][j] = 1.0f;
                }
            }
    }
}

/* Outbreak's human skeleton (players, NPCs, some enemies): clavicles 12 / 16 on the chest 11, the
   shoulders 13 / 17 below them, the head 21 on the neck 20 */
static int human_skeleton(const ImpModel* m)
{
    return m->nn >= 36 && m->node[12].parent == 11 && m->node[16].parent == 11 && m->node[13].parent == 12 &&
           m->node[17].parent == 16 && m->node[21].parent == 20;
}

static void ob2_fit(const char* id, const ImpModel* m, ImpFit* fit)
{
    float in = 0, down = 0, (*W)[16];
    size_t best = 0;
    memset(fit, 0, sizeof(*fit));
    for (size_t i = 0; i < sizeof(fits) / sizeof(fits[0]); i++)
        if (!strncmp(id, fits[i].id, strlen(fits[i].id)) && strlen(fits[i].id) >= best) { best = strlen(fits[i].id); in = fits[i].in; down = fits[i].down; }
    if (!human_skeleton(m) || !(W = calloc(m->nn, sizeof(*W)))) return;
    imp_world(m, W);
    float half = (fabsf(W[13][12]) + fabsf(W[17][12])) * 0.5f * 0.1f, head = W[21][13] * 0.1f, target = head * 1.4f / 15.05f;
    float dx = half > target ? in * (half - target) : 0, s12 = W[13][12] > 0 ? 1.0f : -1.0f;
    free(W);
    /* (game space: x turned, so a shoulder at Outbreak's +x moves in towards +x in the game) */
    fit->move[0].node = 12; fit->move[0].d[0] = s12 * dx; fit->move[0].d[1] = -down;
    fit->move[1].node = 16; fit->move[1].d[0] = -s12 * dx; fit->move[1].d[1] = -down;
    fit->n = 2;
}

int ob2_extract(const char* isopath, const char* outdir, int what, ImportItem* items, int max, int* count)
{
    FILE* f = pc_fopen(isopath, "rb");
    int64_t off, cnf_off;
    uint32_t size, cnf_size;
    int nfiles = 0, nrom = 0, fails = 0;
    char cnf[128] = { 0 };
    ImportProgress* pg = &g_import_progress;
    *count = 0;
    if (!f) { snprintf(pg->phase, sizeof(pg->phase), "Cannot open disc image: %s", isopath); return -1; }
    if (!imp_iso_find(f, "SYSTEM.CNF", &cnf_off, &cnf_size) || !imp_read_at(f, cnf_off, cnf, cnf_size < 127 ? cnf_size : 127) || !strstr(cnf, "SLUS_209.84")) {
        snprintf(pg->phase, sizeof(pg->phase), "Use a US Resident Evil Outbreak File #2 disc (SLUS-20984).");
        fclose(f);
        return -1;
    }
    AfsFile *top = imp_iso_find(f, "NETBIO00.DAT", &off, &size) ? imp_afs_list(f, off, size, &nfiles) : NULL, *rom = NULL;
    for (int i = 0; top && i < nfiles && !rom; i++)
        if (!SDL_strcasecmp(top[i].name, "romdata_usa.afs")) rom = imp_afs_list(f, top[i].off, top[i].size, &nrom);
    free(top);
    if (!rom) { snprintf(pg->phase, sizeof(pg->phase), "Required game data not found. Check the selected disc image."); fclose(f); return -1; }

    /* what to extract (all character models are in romdata_usa.afs; the scenario archives hold rooms,
       objects and effects only): the players (P<chr>_<costume>.nbd), their cutscene (EP) and high-detail
       (HP) versions, NPCs (N*, high-detail HN*), enemies (E*, high-detail HE*) */
    int* list = malloc(sizeof(int) * nrom), nl = 0;
    for (int i = 0; list && i < nrom; i++) {
        const char* n = rom[i].name;
        const char* dot = strrchr(n, '.');
        if (!dot || SDL_strcasecmp(dot, ".nbd")) continue;
        int g = ob2_group(n);
        if (g >= 0 && (what & (1 << g))) list[nl++] = i;
    }
    pg->total = nl;
    for (int li = 0; li < nl && *count < max; li++) {
        const AfsFile* e = &rom[list[li]];
        ImportItem* it = &items[(*count)++];
        ImpModel m;
        char id[16], dir[1200], path[1300], err[160] = "";
        memset(it, 0, sizeof(*it));
        int idl = (int)(strrchr(e->name, '.') - e->name);
        snprintf(id, sizeof(id), "%.*s", idl < 15 ? idl : 15, e->name);
        for (char* c = id; *c; c++) *c = (char)SDL_toupper((unsigned char)*c);
        snprintf(it->id, sizeof(it->id), "%s", id);
        it->group = ob2_group(e->name);
        {   /* names: the cast for P, EP (event) and HP (high detail); the others by their file */
            const char* q = id[0] == 'P' ? id + 1 : (id[0] == 'E' || id[0] == 'H') && id[1] == 'P' ? id + 2 : NULL;
            int c = q ? atoi(q) : -1, v = q && strchr(q, '_') ? atoi(strchr(q, '_') + 1) : -1;
            if (c >= 0 && c < 8) {
                snprintf(it->name, sizeof(it->name), "%s", cast[c]);
                if (id[0] == 'P') snprintf(it->variant, sizeof(it->variant), "Costume %d", v + 1);
                else if (id[0] == 'E') snprintf(it->variant, sizeof(it->variant), "Cutscene Costume %d", v + 1);
                else snprintf(it->variant, sizeof(it->variant), "High Detail");
                it->female = c >= 5;
            } else {
                snprintf(it->name, sizeof(it->name), "%s %s", it->group == IMPORT_GROUP_ENEMIES ? "Enemy" : "Character", id);
                snprintf(it->variant, sizeof(it->variant), "%s", id[0] == 'H' ? "High Detail" : "");
                it->female = it->group == IMPORT_GROUP_ENEMIES ? -1 : 0;   /* (NPCs: not known; a man's motions) */
            }
        }
        snprintf(pg->phase, sizeof(pg->phase), "%s %s (%s)", it->name, it->variant, e->name);
        unsigned char* data = e->size > 0 && e->size < (64u << 20) ? malloc(e->size) : NULL;
        if (!data || !imp_read_at(f, e->off, data, e->size)) snprintf(err, sizeof(err), "Cannot read %s from the disc.", e->name);
        else if (ob2_read_nbd(data, e->size, &m, err, sizeof(err))) {
            it->nodes = m.nn; it->verts = m.nv; it->tris = imp_tris(&m); it->textures = m.ntex;
            it->meshes = m.nmesh;
            snprintf(dir, sizeof(dir), "%s/%s", outdir, id);
            SDL_CreateDirectory(dir);
            snprintf(path, sizeof(path), "%s/%s.glb", dir, id);
            it->ok = imp_write_glb(&m, path, id, 0.01f, err, sizeof(err));
            for (int k = 0; k < m.ntex; k++) {
                snprintf(path, sizeof(path), "%s/%s_tex%d.png", dir, id, k);
                if (!imp_write_png(&m, k, path) && !err[0]) snprintf(err, sizeof(err), "Texture %d uses an unsupported format.", k);
            }
            /* (people on a skeleton of their own: N08's 24 nodes, HN_70's 74, convert by the body groups too;
               the "*_N" files are hair and face parts of the high-detail models) */
            float lo = 1e9f, hi = -1e9f;
            for (int v = 0; v < m.nv; v++) { lo = fminf(lo, m.pos[v][1]); hi = fmaxf(hi, m.pos[v][1]); }
            int human = it->ok && (human_skeleton(&m) || (it->group != IMPORT_GROUP_ENEMIES && !strstr(id, "_N"))) &&
                        hi - lo > 100.0f;   /* a whole body, at least a metre tall (HE01_56 is a severed arm on the skeleton) */
            if (it->ok && !human && !err[0]) snprintf(err, sizeof(err), "Exported only: incompatible body structure (%d parts).", m.nn);
            if (human && it->female < 0) it->female = 0;   /* an enemy on the human skeleton (as a man) */
            if (human) {   /* "You appear as": every model on the human skeleton */
                char e2[160] = "";
                ImpFit fit;
                keep_body(&m);
                lid_falloff(&m);
                ob2_fit(id, &m, &fit);
                snprintf(path, sizeof(path), "%s/%s.cvxm", dir, id);
                if (imp_write_cvxm(&m, path, 0.1f, 0x00F00000u + 16u * (unsigned)list[li], &fit, e2, sizeof(e2))) it->player = 1;
                else snprintf(err, sizeof(err), "Not playable: %s", e2);
            }
            if (m.eyes_left_out && !err[0]) snprintf(err, sizeof(err), "Eye overlay removed to keep the eyes visible.");
            imp_free(&m);
        }
        free(data);
        snprintf(it->note, sizeof(it->note), "%s", err);
        fails += !it->ok;
        pg->failed = fails;
        pg->done = li + 1;
    }
    free(list);
    free(rom);
    fclose(f);
    return fails;
}
