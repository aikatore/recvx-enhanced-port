/* Resident Evil: The Umbrella Chronicles (Wii, RBUE08): the playable characters from the user's RVZ disc image
   (docs/IMPORT_UMBRELLA.md). Our own reading of the NW4R formats (BRRES, MDL0, TEX0; the layouts as the Wii
   modding community documents them, no code taken) and the conversion to the port's player model.

   Data/chara/<KEY>.arc is a U8 archive: <KEY>.brres (the model, its textures, animations), expression.exp (a
   small RPN script for helper bones: "a b 2.0 radv" turns bone a by half of bone b's rotation). The model:
   a skeleton of 60..140 bones (a T-pose facing +z, 1 unit = 10 cm as the game's), objects drawn by display
   lists whose vertices are on one bone (stored in its frame) or on an envelope of up to 4 (model space), the
   GX textures, and the blink as a second set of positions ("mepati", an SHP0 morph). */
#include "import.h"
#include "impmodel.h"
#include "../host/lang.h"
#include "../../include/pc_parse.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UC_BONES 256
#define UC_VERTS (1 << 18)       /* a model's vertices (one per triangle corner; the largest player has 24000) */

/* ---------------------------------------------------------------- big-endian reading, bounded */
typedef struct B { const unsigned char* d; uint32_t n; int bad; } B;
static uint32_t U32(B* b, uint32_t o) { if (!pc_range_ok(o, 4, b->n)) { b->bad = 1; return 0; } const unsigned char* p = b->d + o; return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static int32_t S32(B* b, uint32_t o) { return (int32_t)U32(b, o); }
static uint16_t U16(B* b, uint32_t o) { if (!pc_range_ok(o, 2, b->n)) { b->bad = 1; return 0; } return (uint16_t)(b->d[o] << 8 | b->d[o + 1]); }
static uint8_t U8(B* b, uint32_t o) { if (o >= b->n) { b->bad = 1; return 0; } return b->d[o]; }
static float F32(B* b, uint32_t o) { uint32_t v = U32(b, o); float f; memcpy(&f, &v, 4); return isfinite(f) ? f : 0; }
static const char* STR(B* b, uint32_t o) { if (o >= b->n || !memchr(b->d + o, 0, b->n - o)) { b->bad = 1; return ""; } return (const char*)b->d + o; }

/* an NW4R resource dictionary at off: its entries (name, data offset), at most max */
typedef struct Ent { const char* name; uint32_t at; } Ent;
static int dict(B* b, uint32_t off, Ent* e, int max)
{
    uint32_t n = U32(b, off + 4);
    int k = 0;
    for (uint32_t i = 1; i <= n && k < max && !b->bad; i++) {
        uint32_t x = off + 8 + 16 * i;
        e[k].name = STR(b, off + U32(b, x + 8));
        e[k].at = off + U32(b, x + 12);
        k++;
    }
    return b->bad ? 0 : k;
}
static uint32_t find(const Ent* e, int n, const char* name) { for (int i = 0; i < n; i++) if (!strcmp(e[i].name, name)) return e[i].at; return 0; }

/* ---------------------------------------------------------------- GX textures (level 0) to RGBA8 */
static void c565(unsigned v, unsigned char* o) { o[0] = (unsigned char)(((v >> 11) & 31) * 255 / 31); o[1] = (unsigned char)(((v >> 5) & 63) * 255 / 63); o[2] = (unsigned char)((v & 31) * 255 / 31); o[3] = 255; }
static void rgb5a3(unsigned v, unsigned char* o)
{
    if (v & 0x8000) { o[0] = (unsigned char)(((v >> 10) & 31) * 255 / 31); o[1] = (unsigned char)(((v >> 5) & 31) * 255 / 31); o[2] = (unsigned char)((v & 31) * 255 / 31); o[3] = 255; }
    else { o[0] = (unsigned char)(((v >> 8) & 15) * 17); o[1] = (unsigned char)(((v >> 4) & 15) * 17); o[2] = (unsigned char)((v & 15) * 17); o[3] = (unsigned char)(((v >> 12) & 7) * 255 / 7); }
}

/* fmt: 0 I4, 1 I8, 2 IA4, 3 IA8, 4 RGB565, 5 RGB5A3, 6 RGBA8, 14 CMPR (no palettes on this disc) */
static unsigned char* gx_decode(B* b, uint32_t at, int fmt, int w, int h)
{
    static const int bw_[15] = { 8, 8, 8, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 8 }, bh_[15] = { 8, 4, 4, 4, 4, 4, 4, 0, 0, 0, 0, 0, 0, 0, 8 },
                     bpp_[15] = { 4, 8, 8, 16, 16, 16, 32, 0, 0, 0, 0, 0, 0, 0, 4 };
    if (fmt < 0 || fmt > 14 || !bw_[fmt] || w <= 0 || h <= 0 || w > 1024 || h > 1024) return NULL;
    int bw = bw_[fmt], bh = bh_[fmt], W = (w + bw - 1) / bw * bw, H = (h + bh - 1) / bh * bh;
    uint32_t size = (uint32_t)W * H * bpp_[fmt] / 8;
    if (!pc_range_ok(at, size, b->n)) return NULL;
    const unsigned char* s = b->d + at;
    unsigned char* o = calloc((size_t)w * h, 4);
    if (!o) return NULL;
    for (int by = 0; by < H; by += bh)
        for (int bx = 0; bx < W; bx += bw) {
            if (fmt == 14) {   /* 2x2 DXT1 sub-blocks of 4x4, 8 bytes each: two RGB565 colours (c0 <= c1: 3 colours + transparent) */
                for (int sb = 0; sb < 4; sb++, s += 8) {
                    unsigned c0 = s[0] << 8 | s[1], c1 = s[2] << 8 | s[3];
                    unsigned char p[4][4];
                    c565(c0, p[0]); c565(c1, p[1]);
                    for (int j = 0; j < 3; j++) {
                        if (c0 > c1) { p[2][j] = (unsigned char)((2 * p[0][j] + p[1][j]) / 3); p[3][j] = (unsigned char)((p[0][j] + 2 * p[1][j]) / 3); }
                        else { p[2][j] = (unsigned char)((p[0][j] + p[1][j]) / 2); p[3][j] = 0; }
                    }
                    p[2][3] = 255; p[3][3] = c0 > c1 ? 255 : 0;
                    for (int y = 0; y < 4; y++)
                        for (int x = 0; x < 4; x++) {
                            int X = bx + (sb & 1) * 4 + x, Y = by + (sb >> 1) * 4 + y;
                            if (X < w && Y < h) memcpy(o + 4 * ((size_t)Y * w + X), p[(s[4 + y] >> (6 - 2 * x)) & 3], 4);
                        }
                }
                continue;
            }
            const unsigned char* blk = s;
            s += bw * bh * bpp_[fmt] / 8;
            for (int y = 0; y < bh; y++)
                for (int x = 0; x < bw; x++) {
                    int i = y * bw + x, X = bx + x, Y = by + y;
                    unsigned char px[4];
                    switch (fmt) {
                    case 0: { int v = (blk[i / 2] >> (i & 1 ? 0 : 4)) & 15; px[0] = px[1] = px[2] = px[3] = (unsigned char)(v * 17); break; }
                    case 1: px[0] = px[1] = px[2] = px[3] = blk[i]; break;
                    case 2: px[0] = px[1] = px[2] = (unsigned char)((blk[i] & 15) * 17); px[3] = (unsigned char)((blk[i] >> 4) * 17); break;
                    case 3: px[0] = px[1] = px[2] = blk[2 * i + 1]; px[3] = blk[2 * i]; break;
                    case 4: c565(blk[2 * i] << 8 | blk[2 * i + 1], px); break;
                    case 5: rgb5a3(blk[2 * i] << 8 | blk[2 * i + 1], px); break;
                    default: px[3] = blk[2 * i]; px[0] = blk[2 * i + 1]; px[1] = blk[32 + 2 * i]; px[2] = blk[32 + 2 * i + 1]; break;   /* RGBA8: AR, then GB */
                    }
                    if (X < w && Y < h) memcpy(o + 4 * ((size_t)Y * w + X), px, 4);
                }
        }
    return o;
}

/* a picture resized to powers of two (the PS2's textures; PKS1 has a 550x312 one): bilinear */
static unsigned char* pow2(unsigned char* p, int* w, int* h)
{
    int W = 1, H = 1;
    while (W * 2 <= *w || (W * 2 < 1024 && W * 3 / 2 < *w)) W *= 2;   /* (the nearer power of two) */
    while (H * 2 <= *h || (H * 2 < 1024 && H * 3 / 2 < *h)) H *= 2;
    if (W > 1024) W = 1024;
    if (H > 1024) H = 1024;
    if (W == *w && H == *h) return p;
    unsigned char* o = malloc((size_t)W * H * 4);
    if (!o) { free(p); return NULL; }
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float fx = (x + 0.5f) * *w / W - 0.5f, fy = (y + 0.5f) * *h / H - 0.5f;
            int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
            float ax = fx - x0, ay = fy - y0;
            for (int c = 0; c < 4; c++) {
                float v = 0;
                for (int k = 0; k < 4; k++) {
                    int xx = x0 + (k & 1), yy = y0 + (k >> 1);
                    xx = xx < 0 ? 0 : xx >= *w ? *w - 1 : xx; yy = yy < 0 ? 0 : yy >= *h ? *h - 1 : yy;
                    v += p[4 * ((size_t)yy * *w + xx) + c] * ((k & 1) ? ax : 1 - ax) * ((k >> 1) ? ay : 1 - ay);
                }
                o[4 * ((size_t)y * W + x) + c] = (unsigned char)lroundf(v);
            }
        }
    free(p);
    *w = W; *h = H;
    return o;
}

/* ---------------------------------------------------------------- skeletons
   The roles of the converted skeleton (impmodel.c writes them in pre-order: root, pelvis, the spine to the
   head and the face, the arms, the legs), and per skeleton family the bone that takes each (docs "Skeletons";
   "a|b": either name; a name also matches with "0" or "_root_<n>" after it: "R_te0", "kao_root_2"):
   A  _00.._23 under N_ALL (Chris, Jill, Rebecca, Richard, Kenneth; the zombies, Lisa, Ivy; the zombies' jaw _24)
   B  _NN_Sxx (Ada, Carlos, HUNK, Wesker): the same order with a skeleton id; "_06_D05_07_50": driven halfway
   H  numbered as A, the arms and legs further on (the Hunter; its jaw _05)
   C1 Japanese names (Billy; the Tyrant ETY0, its lids mabu_l / mabu_r): kosi, hara, mune, kubi, atama,
      kata / jowan / kawan / te, momo / sune / asikub; C1E the Eliminator's: its legs hang from hara, its spine is koshi
   C2 Japanese names with L_ / R_ (the Licker, Nemesis, the T-103s, the office worker OSIT1): locator2, koshi, hara,
      mune, kubi, kao, sako / kata / hiji / te, momo / hiza / ashi; the jaw ago (OSIT1's mouth: kuchi)
   A family is taken only if its bones hang from each other as the roles do (the arm from the chest, the forearm from
   the upper arm ...): the Hunter's _05 is its jaw, not a clavicle, so family A does not fit it. */
enum { R_ROOT, R_PELVIS, R_SPINE, R_CHEST, R_NECK, R_HEAD, R_RCLAV, R_RSH, R_REL, R_RWR, R_LCLAV, R_LSH, R_LEL, R_LWR,
       R_RTHIGH, R_RKNEE, R_RANKLE, R_LTHIGH, R_LKNEE, R_LANKLE, R_JAW, R_LIDL, R_LIDR, ROLES };
#define R_BODY R_JAW   /* (the roles every family has; the face's after them) */
static const signed char role_parent[ROLES] = { -1, 0, 1, 2, 3, 4, 3, 6, 7, 8, 3, 10, 11, 12, 1, 14, 15, 1, 17, 18, 5, 5, 5 };
enum { FAM_NONE, FAM_A, FAM_B, FAM_H, FAM_C1, FAM_C1E, FAM_C2, FAMS };
static const char* const fam_names[FAMS][ROLES] = {
    { 0 },
    { "N_ALL", "_00", "_01", "_02", "_03", "_04", "_05", "_07", "_08", "_09", "_10", "_12", "_13", "_14", "_16", "_17", "_18", "_20", "_21", "_22", "_24" },
    { "N_ALL", "_00", "_01", "_02", "_03", "_04", "_05_", "_07_", "_08_", "_10_", "_11_", "_13_", "_14_", "_16_", "_18_", "_19_", "_20_", "_22_", "_23_", "_24_" },
    { "N_ALL", "_00", "_01", "_02", "_03", "_04", "_07", "_09", "_10", "_11", "_14", "_16", "_17", "_18", "_22", "_23", "_24", "_26", "_27", "_28", "_05" },
    { "trans|N_ALL", "kosi", "hara|kara", "mune", "kubi", "atama", "kata_r", "jowan_r|jyowan_r", "kawan_r|hiji_r", "te_r", "kata_l", "jowan_l|jyowan_l",
      "kawan_l|hiji_l", "te_l", "momo_r", "sune_r|hiza_r", "asikub_r|asi_r", "momo_l", "sune_l|hiza_l", "asikub_l|asi_l", "ago", "mabu_l", "mabu_r" },
    { "trans", "hara", "koshi", "mune", "kubi", "kao", "kata_r", "jouwan_r", "kawan_r", "tekubi_r", "kata_l", "jouwan_l", "kawan_l", "tekubi_l",
      "momo_r", "sune_r", "asikubi_r", "momo_l", "sune_l", "asikubi_l", "ago" },
    { "locator2", "koshi", "hara", "mune", "kubi", "kao", "R_sako", "R_kata", "R_hiji", "R_te", "L_sako", "L_kata", "L_hiji", "L_te",
      "R_momo", "R_hiza", "R_ashi", "L_momo", "L_hiza", "L_ashi", "ago|kuchi" },
};
/* a bone's name against one name of a table (its first l characters): "_05_" (family B) matches "_05_S11" and "_05_s11",
   not "_06_D05_07_50"; loose: also with "0" or "_root_<n>" after it */
static int name_is(const char* n, const char* pat, size_t l, int loose)
{
    if (pat[l - 1] == '_') return !SDL_strncasecmp(n, pat, l) && (n[l] == 'S' || n[l] == 's');
    if (SDL_strncasecmp(n, pat, l)) return 0;
    return !n[l] || (loose && ((n[l] == '0' && !n[l + 1]) || !SDL_strncasecmp(n + l, "_root_", 6)));
}

typedef struct UBone { const char* name; int parent, index; float W[12]; } UBone;   /* W: world rest, 3x4 rows */
typedef struct Uc {
    int nb; UBone bone[UC_BONES];
    int family;                  /* FAM_* */
    int core[ROLES];             /* role -> bone (the face's: -1 where the model has none) */
    int nradv; struct { int a, b; float f; } radv[32];   /* expression.exp: bone a takes f of bone b's rotation */
    unsigned char* face;         /* per vertex: on a face texture or in the blink morph (the jaw's region) */
    int nface;
    unsigned char* swrap;        /* per strip its texture's wrap where it is not clamped: 1 u repeats, 2 u mirrors, 4 v repeats, 8 v mirrors */
    int nswrap;
    char tname[IMP_TEX][32];     /* the textures' names */
    int held, outside;           /* meshes left out: held in a hand (the weapons); reaching far out of the body (the Licker's tongue) */
    FILE* dump;                  /* test aid (RECVX_IMPORT_MESHES): per object what became of it */
} Uc;

static int find_bone(const Uc* u, const char* pat)   /* the first bone of the names "a|b|..." (exact names first) */
{
    for (int loose = 0; loose < 2; loose++)
        for (const char* a = pat; *a;) {
            size_t l = strcspn(a, "|");
            for (int i = 0; i < u->nb; i++) if (name_is(u->bone[i].name, a, l, loose)) return i;
            a += l + (a[l] == '|');
        }
    return -1;
}
static int is_above(const Uc* u, int a, int b)   /* bone a is an ancestor of bone b */
{
    for (int i = u->bone[b].parent, n = 0; i >= 0 && n < UC_BONES; i = u->bone[i].parent, n++) if (i == a) return 1;
    return 0;
}

static int family(Uc* u)
{
    static const int order[] = { FAM_B, FAM_A, FAM_H, FAM_C1, FAM_C1E, FAM_C2 };
    for (size_t f = 0; f < sizeof(order) / sizeof(order[0]); f++) {
        int fam = order[f], ok = 1;
        for (int r = 0; r < ROLES; r++) u->core[r] = fam_names[fam][r] ? find_bone(u, fam_names[fam][r]) : -1;
        for (int r = 0; r < R_BODY && ok; r++) ok = u->core[r] >= 0;
        for (int r = R_RCLAV; r < R_BODY && ok; r++) ok = is_above(u, u->core[role_parent[r]], u->core[r]);   /* (the limbs as the roles hang) */
        if (!ok || !is_above(u, u->core[R_NECK], u->core[R_HEAD])) continue;
        for (int r = R_JAW; r < ROLES; r++)   /* the face's bones: on the head */
            if (u->core[r] >= 0 && !is_above(u, u->core[R_HEAD], u->core[r])) u->core[r] = -1;
        for (int r = R_RCLAV; r <= R_LANKLE; r++) {   /* the character's right at -x: names the other way round are swapped */
            int o = r <= R_RWR ? r + 4 : r >= R_RTHIGH && r <= R_RANKLE ? r + 3 : -1;
            if (o >= 0 && u->bone[u->core[r]].W[3] > u->bone[u->core[o]].W[3]) { int t = u->core[r]; u->core[r] = u->core[o]; u->core[o] = t; }
        }
        if (u->core[R_LIDL] >= 0 && u->core[R_LIDR] >= 0 && u->bone[u->core[R_LIDL]].W[3] < u->bone[u->core[R_LIDR]].W[3]) {
            int t = u->core[R_LIDL]; u->core[R_LIDL] = u->core[R_LIDR]; u->core[R_LIDR] = t;
        }
        return u->family = fam;
    }
    for (int r = 0; r < ROLES; r++) u->core[r] = -1;
    return u->family = FAM_NONE;
}

/* "a b 2.0 radv": bone a turns by 1 / 2.0 of bone b's rotation (the arms' twist bones) */
static void read_exp(Uc* u, const unsigned char* d, uint32_t n)
{
    char tok[4][24];
    int nt = 0;
    for (uint32_t i = 0; i < n;) {
        while (i < n && (d[i] <= ' ' || d[i] == '{' || d[i] == '}')) i++;
        uint32_t s = i;
        while (i < n && d[i] > ' ' && d[i] != '{' && d[i] != '}') i++;
        if (i == s) break;
        char t[24];
        snprintf(t, sizeof(t), "%.*s", (int)(i - s), (const char*)d + s);
        if (!strcmp(t, "radv") && nt >= 3 && u->nradv < 32) {
            int a = atoi(tok[(nt - 3) & 3]), b = atoi(tok[(nt - 2) & 3]);
            float r = (float)atof(tok[(nt - 1) & 3]);
            if (a >= 0 && a < u->nb && b >= 0 && b < u->nb && r >= 1.0f) {
                u->radv[u->nradv].a = a; u->radv[u->nradv].b = b; u->radv[u->nradv].f = 1.0f / r;
                u->nradv++;
            }
        }
        memcpy(tok[nt & 3], t, sizeof(t));
        nt++;
    }
}

/* a source bone's weight, as shares of the roles (up to 4) */
typedef struct Dist { int n, t[4]; float f[4]; } Dist;
static void dist_add(Dist* d, const Dist* s, float f)
{
    for (int i = 0; i < s->n; i++) {
        int k = 0;
        while (k < d->n && d->t[k] != s->t[i]) k++;
        if (k == d->n) {
            if (d->n == 4) {   /* (the smallest gives way) */
                int m = 0;
                for (int j = 1; j < 4; j++) if (d->f[j] < d->f[m]) m = j;
                if (d->f[m] >= s->f[i] * f) continue;
                k = m;
            } else d->n++;
            d->t[k] = s->t[i]; d->f[k] = 0;
        }
        d->f[k] += s->f[i] * f;
    }
}
static int numbered(const Uc* u, int num)   /* family B: the bone "_<num>" or "_<num>_S.." */
{
    char p[8];
    snprintf(p, sizeof(p), "_%02d", num);
    for (int i = 0; i < u->nb; i++) if (!strncmp(u->bone[i].name, p, 3) && (!u->bone[i].name[3] || u->bone[i].name[3] == '_')) return i;
    return -1;
}
static Dist dist_of(const Uc* u, int i, int depth)
{
    Dist d = { 0 }, a, b;
    for (int r = 0; r < ROLES; r++) if (i >= 0 && u->core[r] == i) { d.n = 1; d.t[0] = r; d.f[0] = 1; return d; }
    if (i < 0 || depth > 64) { d.n = 1; d.t[0] = R_PELVIS; d.f[0] = 1; return d; }   /* (outside the skeleton: the pelvis) */
    int par = u->bone[i].parent, x, y, pct;
    for (int k = 0; k < u->nradv; k++)   /* a helper bone turning by a part of another's rotation: its weight split so */
        if (u->radv[k].a == i) {
            a = dist_of(u, u->radv[k].b, depth + 1); b = dist_of(u, par, depth + 1);
            dist_add(&d, &a, u->radv[k].f); dist_add(&d, &b, 1 - u->radv[k].f);
            return d;
        }
    const char* n = u->bone[i].name;
    if (u->family == FAM_B && sscanf(n, "_%*d_D%2d_%2d_%d", &x, &y, &pct) == 3 && numbered(u, x) >= 0 && numbered(u, y) >= 0 && pct > 0 && pct <= 100) {
        a = dist_of(u, numbered(u, y), depth + 1); b = dist_of(u, numbered(u, x), depth + 1);   /* (driven between bones x and y) */
        dist_add(&d, &a, pct / 100.0f); dist_add(&d, &b, 1 - pct / 100.0f);
        return d;
    }
    if (u->family == FAM_C1 && !SDL_strncasecmp(n, "kenko_", 6)) {   /* the upper arm's twist beside jowan (the shoulder): half way */
        a = dist_of(u, u->core[u->bone[i].W[3] < 0 ? R_RSH : R_LSH], depth + 1); b = dist_of(u, par, depth + 1);
        dist_add(&d, &a, 0.5f); dist_add(&d, &b, 0.5f);
        return d;
    }
    return par < 0 ? dist_of(u, -1, depth + 1) : dist_of(u, par, depth + 1);   /* the nearest kept bone above (toes, fingers, the hips' pivot) */
}

/* ---------------------------------------------------------------- reading the model (MDL0) */
static int grow(ImpModel* m, int nv, int nidx, int ns, int* cv, int* ci, int* cs)
{
    void* q;
    if (nv > *cv) {
        int c = *cv ? *cv : 4096;
        while (c < nv) c *= 2;
#define GROW(f, sz) do { if (!(q = realloc(m->f, (sz) * (size_t)c))) return 0; m->f = q; } while (0)
        GROW(pos, 12); GROW(nrm, 12); GROW(uv, 8); GROW(jnt, 4); GROW(wgt, 16); GROW(morph, 12);
#undef GROW
        *cv = c;
    }
    if (nidx > *ci) { int c = *ci ? *ci : 8192; while (c < nidx) c *= 2; if (!(q = realloc(m->idx, sizeof(int) * (size_t)c))) return 0; m->idx = q; *ci = c; }
    if (ns > *cs) { int c = *cs ? *cs : 1024; while (c < ns) c *= 2; if (!(q = realloc(m->strip, sizeof(ImpStrip) * (size_t)c))) return 0; m->strip = q; *cs = c; }
    return 1;
}

typedef struct Arr { int index, comps, fmt, div, stride, cnt; uint32_t data; const char* name; } Arr;
static int read_arr(B* b, uint32_t o, const char* name, int kind, Arr* a)   /* kind 0 positions, 1 normals, 2 UVs */
{
    static const int comps[3][3] = { { 2, 3, 0 }, { 3, 9, 3 }, { 1, 2, 0 } };
    uint32_t ct = U32(b, o + 0x14);
    a->data = o + U32(b, o + 8); a->index = (int)U32(b, o + 0x10); a->fmt = (int)U32(b, o + 0x18);
    a->div = U8(b, o + 0x1C); a->stride = U8(b, o + 0x1D); a->cnt = U16(b, o + 0x1E); a->name = name;
    a->comps = ct < 3 ? comps[kind][ct] : 0;
    return !b->bad && a->comps && a->fmt <= 4 && pc_range_ok(a->data, (uint64_t)a->cnt * a->stride, b->n);
}
static void arr_get(B* b, const Arr* a, int i, float* out)   /* element i (positions: z 0 for XY arrays; normals: the first 3 of 9) */
{
    static const int sz[5] = { 1, 1, 2, 2, 4 };
    int n = a->comps == 9 ? 3 : a->comps;
    out[0] = out[1] = out[2] = 0;
    if (i < 0 || i >= a->cnt) return;
    for (int k = 0; k < n; k++) {
        uint32_t p = a->data + (uint32_t)i * a->stride + k * sz[a->fmt];
        float v;
        switch (a->fmt) {
        case 0: v = U8(b, p); break;
        case 1: v = (int8_t)U8(b, p); break;
        case 2: v = U16(b, p); break;
        case 3: v = (int16_t)U16(b, p); break;
        default: v = F32(b, p); break;
        }
        out[k] = a->fmt == 4 ? v : v / (float)(1 << a->div);
    }
}

/* the colour map among a material's layers: environment, sphere and specular maps and indirect (_ind) maps
   come first in many of them. ponytail: by name, as the reference extractor; the TEV stages would tell for sure */
static int not_diffuse(const char* t)
{
    char l[64];
    size_t n = strlen(t);
    for (size_t i = 0; i < sizeof(l); i++) l[i] = (char)SDL_tolower((unsigned char)(i < n ? t[i] : 0));
    l[63] = 0;
    n = strlen(l);
    return strstr(l, "spec") || strstr(l, "sphere") || strstr(l, "light") || (n >= 4 && !strcmp(l + n - 4, "_ind")) || (n >= 3 && !strcmp(l + n - 3, "ref")) ||
           !strncmp(l, "se_", 3) || strstr(l, "_se_") || (!strncmp(l, "spe", 3) && l[3] >= '0' && l[3] <= '9') || strstr(l, "env") || strstr(l, "matcap") || strstr(l, "toon");
}

/* the meshes that are not part of the body: the hands' other poses (hand_l1, hand_r2: the gun grip, half open),
   the knife and the Samurai Edge of the model's own weapon set */
static int variant(const char* v)
{
    static const char* const pre[] = { "knife", "samurai", "edge", "gun", "k_knife", "weapon", "wpn" };
    if (!SDL_strncasecmp(v, "hand_", 5)) { const char* p = v + 5; if (*p == 'l' || *p == 'r' || *p == 'L' || *p == 'R') p++; return *p >= '2' && *p <= '9';   /* (1, the gun grip, is kept: hand_pose) */ }
    for (size_t i = 0; i < sizeof(pre) / sizeof(pre[0]); i++) if (!SDL_strncasecmp(v, pre[i], strlen(pre[i]))) return 1;
    return 0;
}
/* the hands' relaxed (0) and gun-grip (1) poses are both kept: IMP_HAND_* of a visibility bone hand_r0 / r1 / l0 / l1 */
static int hand_pose(const char* v)
{
    if (SDL_strncasecmp(v, "hand_", 5) || (v[6] != '0' && v[6] != '1') || v[7]) return IMP_HAND_NONE;
    int r = v[5] == 'r' || v[5] == 'R', l = v[5] == 'l' || v[5] == 'L';
    return r ? (v[6] == '0' ? IMP_HAND_R_OPEN : IMP_HAND_R_GRIP) : l ? (v[6] == '0' ? IMP_HAND_L_OPEN : IMP_HAND_L_GRIP) : IMP_HAND_NONE;
}

static void xform(const float* W, const float* p, float* o, int dir)   /* o = W p (dir: the rotation only) */
{
    for (int r = 0; r < 3; r++) o[r] = W[4 * r] * p[0] + W[4 * r + 1] * p[1] + W[4 * r + 2] * p[2] + (dir ? 0 : W[4 * r + 3]);
}

static const char* suffix(const char* n)   /* after the last "__" (a material's name), or all of it */
{
    const char* s = n;
    for (const char* p = strstr(n, "__"); p; p = strstr(p + 1, "__")) s = p + 2;
    return s;
}

#define ENT_MAX 2048
typedef struct Mat { uint32_t at; int index, xlu, nl; struct { const char* tex; int slot, ws, wt; } l[8]; } Mat;

static int read_mdl0(B* b, uint32_t mo, const Ent* tex, int ntexe, Uc* u, ImpModel* m, char* err, int en)
{
    uint32_t ver = U32(b, mo + 8);
    int nsec = ver <= 9 ? 11 : 14, cv = 0, ci = 0, cs = 0, ok = 0;
    int sec_bones = 1, sec_pos = 2, sec_nrm = 3, sec_uv = 5, sec_mats = ver >= 10 ? 8 : 6, sec_objs = sec_mats + 2, sec_texl = sec_mats + 3;
    Ent* e = malloc(sizeof(Ent) * ENT_MAX);
    Arr *pa = calloc(ENT_MAX, sizeof(Arr)), *na = calloc(ENT_MAX, sizeof(Arr)), *ua = calloc(ENT_MAX, sizeof(Arr));
    Mat* mats = calloc(512, sizeof(Mat));
    int npa = 0, nna = 0, nua = 0, nmat = 0, n, ndraw = 0;
    int *mtx_node = NULL, nmtx = 0, idx2b[UC_BONES], *envn = NULL;
    struct { int mid; float w; } (*env)[8] = NULL;
    struct { int mat, obj, bone; } draw[1024];
    char used_pos[ENT_MAX] = { 0 };
    short morph_of[ENT_MAX];   /* per position array the blink's (unused) array, -1 none */
    if (!e || !pa || !na || !ua || !mats) goto out;
    if (ver < 8 || ver > 11) { snprintf(err, en, "Model version %u is not known.", ver); goto out; }
#define SEC(k) (S32(b, mo + 16 + 4 * (k)) ? mo + (uint32_t)S32(b, mo + 16 + 4 * (k)) : 0)
    /* bones: index, the world rest matrix, the parent (an offset from the bone) */
    n = SEC(sec_bones) ? dict(b, SEC(sec_bones), e, ENT_MAX) : 0;
    if (n < 2 || n > UC_BONES || n > IMP_NODES) { snprintf(err, en, "%d bones: not a character.", n); goto out; }
    uint32_t boff[UC_BONES];
    for (int i = 0; i < n; i++) {
        UBone* x = &u->bone[i];
        boff[i] = e[i].at;
        x->name = e[i].name; x->index = (int)U32(b, e[i].at + 0xC);
        for (int k = 0; k < 12; k++) x->W[k] = F32(b, e[i].at + 0x70 + 4 * k);
    }
    for (int i = 0; i < UC_BONES; i++) idx2b[i] = -1;
    for (int i = 0; i < n; i++) {
        int32_t po = S32(b, e[i].at + 0x5C);
        u->bone[i].parent = -1;
        for (int k = 0; k < n && po; k++) if (boff[k] == e[i].at + (uint32_t)po) u->bone[i].parent = k;
        if (u->bone[i].index >= 0 && u->bone[i].index < UC_BONES) idx2b[u->bone[i].index] = i;
    }
    u->nb = n;
    family(u);
    {   /* the matrix ids: bone index, -1 an envelope */
        uint32_t info = mo + 16 + (uint32_t)nsec * 4 + 4, tab = info + U32(b, info + 0x24);
        nmtx = (int)U32(b, tab);
        if (b->bad || nmtx <= 0 || nmtx > 4096 || !(mtx_node = calloc(nmtx, sizeof(int))) || !(envn = calloc(nmtx, sizeof(int))) || !(env = calloc(nmtx, sizeof(*env)))) goto out;
        for (int i = 0; i < nmtx; i++) mtx_node[i] = S32(b, tab + 4 + 4 * i);
    }
    /* the definitions: envelopes (3), draw calls (4: material, object, visibility bone) */
    n = SEC(0) ? dict(b, SEC(0), e, ENT_MAX) : 0;
    for (int i = 0; i < n && !b->bad; i++) {
        uint32_t p = e[i].at;
        for (int steps = 0; steps < 1 << 20 && !b->bad; steps++) {
            int op = U8(b, p);
            if (op == 1) break;
            if (op == 0) p++;
            else if (op == 2 || op == 5 || op == 6) p += 5;
            else if (op == 3) {
                int mid = U16(b, p + 1), k = U8(b, p + 3);
                for (int j = 0; j < k; j++)
                    if (mid < nmtx && j < 8) { env[mid][j].mid = U16(b, p + 4 + 6 * j); env[mid][j].w = F32(b, p + 6 + 6 * j); envn[mid] = j + 1; }
                p += 4 + 6 * (uint32_t)k;
            } else if (op == 4) {
                if (ndraw < 1024) { draw[ndraw].mat = U16(b, p + 1); draw[ndraw].obj = U16(b, p + 3); draw[ndraw].bone = U16(b, p + 5); ndraw++; }
                p += 8;
            } else { snprintf(err, en, "Unknown model definition %02x.", op); goto out; }
        }
    }
    /* vertex arrays */
    n = SEC(sec_pos) ? dict(b, SEC(sec_pos), e, ENT_MAX) : 0;
    for (int i = 0; i < n; i++) if (read_arr(b, e[i].at, e[i].name, 0, &pa[npa])) npa++;
    n = SEC(sec_nrm) ? dict(b, SEC(sec_nrm), e, ENT_MAX) : 0;
    for (int i = 0; i < n; i++) if (read_arr(b, e[i].at, e[i].name, 1, &na[nna])) nna++;
    n = SEC(sec_uv) ? dict(b, SEC(sec_uv), e, ENT_MAX) : 0;
    for (int i = 0; i < n; i++) if (read_arr(b, e[i].at, e[i].name, 2, &ua[nua])) nua++;
    b->bad = 0;
    /* materials and their texture layers (through the texture links) */
    n = SEC(sec_mats) ? dict(b, SEC(sec_mats), e, 512) : 0;
    for (int i = 0; i < n; i++) { mats[i].at = e[i].at; mats[i].index = (int)U32(b, e[i].at + 0xC); mats[i].xlu = (U32(b, e[i].at + 0x10) >> 31) & 1; }
    nmat = n;
    n = SEC(sec_texl) ? dict(b, SEC(sec_texl), e, ENT_MAX) : 0;
    for (int i = 0; i < n && !b->bad; i++) {
        uint32_t lo = e[i].at, cnt = U32(b, lo);
        for (uint32_t k = 0; k < cnt && k < 256; k++) {
            uint32_t ma = lo + (uint32_t)S32(b, lo + 4 + 8 * k), L = lo + (uint32_t)S32(b, lo + 8 + 8 * k);
            for (int j = 0; j < nmat; j++)
                if (mats[j].at == ma && mats[j].nl < 8) {
                    int q = mats[j].nl++;
                    mats[j].l[q].tex = STR(b, L + (uint32_t)S32(b, L)); mats[j].l[q].slot = (int)U32(b, L + 0x10);
                    mats[j].l[q].ws = (int)U32(b, L + 0x18); mats[j].l[q].wt = (int)U32(b, L + 0x1C);
                }
        }
    }
    /* objects */
    n = SEC(sec_objs) ? dict(b, SEC(sec_objs), e, ENT_MAX) : 0;
    for (int i = 0; i < n; i++) {   /* (the position arrays in use: the others are the blink's) */
        int pid = (int16_t)U16(b, e[i].at + 0x48);
        for (int k = 0; k < npa; k++) if (pa[k].index == pid) used_pos[k] = 1;
    }
    /* the blink: each position array no object uses goes with a used one of its length (the same material suffix first).
       ponytail: paired by length, as the reference extractor; the SHP0 animation's vertex sets would name them for sure */
    for (int k = 0; k < npa; k++) morph_of[k] = -1;
    for (int i = 0; i < npa; i++) {
        int j = -1;
        if (used_pos[i]) continue;
        for (int pass = 0; pass < 2 && j < 0; pass++)
            for (int k = 0; k < npa && j < 0; k++)
                if (used_pos[k] && pa[k].cnt == pa[i].cnt && (pass || !strcmp(suffix(pa[k].name), suffix(pa[i].name)))) j = k;
        if (j >= 0) morph_of[j] = (short)i;
    }
    for (int oi = 0; oi < n && !b->bad; oi++) {
        uint32_t o = e[oi].at;
        int node = S32(b, o + 8), oidx = (int)U32(b, o + 0x3C), ids[12];
        uint64_t vcd = U32(b, o + 0xC) | (uint64_t)U32(b, o + 0x10) << 17;
        uint32_t prim = o + 0x24 + (uint32_t)S32(b, o + 0x2C), end = prim + U32(b, o + 0x28);
        for (int k = 0; k < 12; k++) ids[k] = (int16_t)U16(b, o + 0x48 + 2 * k);
        int dr = -1;
        for (int k = 0; k < ndraw && dr < 0; k++) if (draw[k].obj == oidx) dr = k;
        if (dr < 0) { if (u->dump) fprintf(u->dump, "%s - - 0 nodraw\n", e[oi].name); continue; }
        const Mat* mat = NULL;
        for (int k = 0; k < nmat && !mat; k++) if (mats[k].index == draw[dr].mat) mat = &mats[k];
        int vb = draw[dr].bone < UC_BONES ? idx2b[draw[dr].bone] : -1;
        const char* vis = vb >= 0 ? u->bone[vb].name : "";
        if (!mat || !mat->nl || variant(vis)) {   /* (a material without layers: an environment shell) */
            if (u->dump) fprintf(u->dump, "%s %s - 0 %s\n", e[oi].name, vis, !mat || !mat->nl ? "shell" : "variant");
            continue;
        }
        /* the colour map: its texture slot */
        int L = -1, tslot = -1;
        for (int pass = 0; pass < 2 && L < 0; pass++)
            for (int k = 0; k < mat->nl && L < 0; k++)
                if (find(tex, ntexe, mat->l[k].tex) && (pass || !not_diffuse(mat->l[k].tex))) L = k;
        if (L >= 0) {
            for (int k = 0; k < m->ntex && tslot < 0; k++) if (!strcmp(u->tname[k], mat->l[L].tex)) tslot = k;
            if (tslot < 0 && m->ntex < IMP_TEX) { tslot = m->ntex++; snprintf(u->tname[tslot], sizeof(u->tname[tslot]), "%s", mat->l[L].tex); }
        }
        const Arr *P = NULL, *N = NULL, *T = NULL, *MT = NULL;
        for (int k = 0; k < npa; k++) if (pa[k].index == ids[0]) P = &pa[k];
        for (int k = 0; k < nna; k++) if (ids[1] >= 0 && na[k].index == ids[1]) N = &na[k];
        for (int k = 0; k < nua; k++) if (ids[4] >= 0 && ua[k].index == ids[4]) T = &ua[k];
        if (!P) continue;
        if (morph_of[P - pa] >= 0) MT = &pa[morph_of[P - pa]];
        int has_pnmi = vcd & 1, ntmi = 0, fmt[12];
        for (int k = 0; k < 8; k++) ntmi += (vcd >> (1 + k)) & 1;
        for (int k = 0; k < 4; k++) fmt[k] = (int)((vcd >> (9 + 2 * k)) & 3);
        for (int k = 0; k < 8; k++) fmt[4 + k] = (int)((vcd >> (17 + 2 * k)) & 3);
        int slot[16], nv0 = m->nv, ns0 = m->ns, ni0 = m->nidx, face = tslot >= 0 && (SDL_strcasestr(u->tname[tslot], "face") || SDL_strcasestr(u->tname[tslot], "lip"));   /* (Jill's lips: a mesh of their own) */
        for (int k = 0; k < 16; k++) slot[k] = node;
        uint32_t p = prim;
        while (p < end && !b->bad) {
            int op = U8(b, p);
            if (op == 0) { p++; continue; }
            if (op == 0x20 || op == 0x28 || op == 0x30 || op == 0x38) { if (op == 0x20) slot[((U16(b, p + 3) & 0xFFF) / 12) & 15] = U16(b, p + 1); p += 5; continue; }
            if (op == 0x08) { p += 6; continue; }
            if (op == 0x10) { p += 5 + 4 * ((uint32_t)U16(b, p + 1) + 1); continue; }
            if (op == 0x61) { p += 5; continue; }
            if (!(op & 0x80)) { snprintf(err, en, "Unknown display list command %02x.", op); goto out; }
            int kind = op & 0xF8, cnt = U16(b, p + 1), base = m->nv;
            p += 3;
            if (m->nv + cnt > UC_VERTS || !grow(m, m->nv + cnt, m->nidx + cnt * 3, m->ns + cnt, &cv, &ci, &cs)) { snprintf(err, en, "Too many vertices."); goto out; }
            for (int v = 0; v < cnt && !b->bad; v++) {
                int mid = node, vi[12] = { 0 };
                if (has_pnmi) mid = slot[(U8(b, p) / 3) & 15], p++;
                p += (uint32_t)ntmi;
                for (int k = 0; k < 12; k++) {
                    if (!fmt[k]) continue;
                    if (fmt[k] == 1) { snprintf(err, en, "Direct vertex data is not supported."); goto out; }
                    if (fmt[k] == 2) vi[k] = U8(b, p++); else { vi[k] = U16(b, p); p += 2; }
                }
                float pp[3], nn[3] = { 0, 1, 0 }, t[3] = { 0 }, mt[3];
                const float* M = NULL;
                int x = m->nv++;
                memset(m->jnt[x], 0, 4); memset(m->wgt[x], 0, 16);
                if (mid >= 0 && mid < nmtx && envn[mid]) {   /* an envelope: model space, up to 4 of its largest weights */
                    for (int k = 0; k < envn[mid]; k++) {
                        int em = env[mid][k].mid, bi = em < nmtx && mtx_node[em] >= 0 && mtx_node[em] < UC_BONES ? idx2b[mtx_node[em]] : -1;
                        float w = env[mid][k].w;
                        if (bi < 0 || w <= 0) continue;
                        int s = 0;
                        for (int j = 1; j < 4; j++) if (m->wgt[x][j] < m->wgt[x][s]) s = j;
                        if (w > m->wgt[x][s]) { m->wgt[x][s] = w; m->jnt[x][s] = (unsigned char)bi; }
                    }
                    float s = m->wgt[x][0] + m->wgt[x][1] + m->wgt[x][2] + m->wgt[x][3];
                    for (int j = 0; j < 4; j++) m->wgt[x][j] = s > 0 ? m->wgt[x][j] / s : j == 0;
                } else {   /* on one bone: in its frame */
                    int bi = mid >= 0 && mid < nmtx && mtx_node[mid] >= 0 && mtx_node[mid] < UC_BONES ? idx2b[mtx_node[mid]] : -1;
                    if (bi < 0) bi = 0;
                    M = u->bone[bi].W;
                    m->jnt[x][0] = (unsigned char)bi; m->wgt[x][0] = 1;
                }
                arr_get(b, P, vi[0], pp);
                if (N) arr_get(b, N, vi[1], nn);
                if (T) arr_get(b, T, vi[4], t);
                if (MT) arr_get(b, MT, vi[0], mt);
                if (M) {
                    float q[3];
                    xform(M, pp, q, 0); memcpy(pp, q, 12);
                    xform(M, nn, q, 1); memcpy(nn, q, 12);
                    if (MT) { xform(M, mt, q, 0); memcpy(mt, q, 12); }
                }
                float l = sqrtf(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
                for (int k = 0; k < 3; k++) { m->pos[x][k] = pp[k]; m->nrm[x][k] = l > 0 ? nn[k] / l : k == 1; m->morph[x][k] = MT ? mt[k] - pp[k] : 0; }
                m->uv[x][0] = t[0]; m->uv[x][1] = t[1];
            }
            /* the primitives as strips (impmodel.c each_tri's order; both sides are drawn) */
#define STRIP(a, c) do { ImpStrip* s_ = &m->strip[m->ns++]; s_->first = m->nidx; s_->n = (c); s_->tex = tslot; s_->clamp = L >= 0 && !mat->l[L].ws && !mat->l[L].wt; \
                          for (int q_ = 0; q_ < (c); q_++) m->idx[m->nidx++] = (a)[q_]; } while (0)
            if (kind == 0x98 && cnt >= 3) {
                ImpStrip* s_ = &m->strip[m->ns++];
                s_->first = m->nidx; s_->n = cnt; s_->tex = tslot; s_->clamp = L >= 0 && !mat->l[L].ws && !mat->l[L].wt;
                for (int q = 0; q < cnt; q++) m->idx[m->nidx++] = base + q;
            } else if (kind == 0x90) for (int q = 0; q + 2 < cnt; q += 3) { int t3[3] = { base + q, base + q + 1, base + q + 2 }; STRIP(t3, 3); }
            else if (kind == 0xA0) for (int q = 1; q + 1 < cnt; q++) { int t3[3] = { base, base + q, base + q + 1 }; STRIP(t3, 3); }
            else if (kind == 0x80 || kind == 0x88)
                for (int q = 0; q + 3 < cnt; q += 4) { int t3[3] = { base + q, base + q + 1, base + q + 2 }, t4[3] = { base + q, base + q + 2, base + q + 3 }; STRIP(t3, 3); STRIP(t4, 3); }
#undef STRIP
        }
        /* a weapon held in a hand (Ada's and Carlos's pistols, HUNK's gun, Jill's stun gun): every vertex on a wrist alone, past
           it, flat (under 0.6 high: a T-pose hand with its thumb is 0.8..1); not the hands themselves (Kenneth's are not named hand_) */
        if (m->nv > nv0 && u->family != FAM_NONE) {
            int on = m->jnt[nv0][0], all = 1;
            float ax = 0, y0 = 1e9f, y1 = -1e9f;
            for (int v = nv0; v < m->nv && all; v++) {
                all = m->wgt[v][0] > 0.999f && m->jnt[v][0] == on;
                ax += fabsf(m->pos[v][0]); y0 = fminf(y0, m->pos[v][1]); y1 = fmaxf(y1, m->pos[v][1]);
            }
            if (all && SDL_strncasecmp(vis, "hand_", 5) && (on == u->core[R_RWR] || on == u->core[R_LWR]) && y1 - y0 < 0.6f &&
                ax / (m->nv - nv0) > fabsf(u->bone[on].W[3]) + 0.3f) {
                if (u->dump) fprintf(u->dump, "%s %s %s %d held\n", e[oi].name, vis, tslot >= 0 ? u->tname[tslot] : "-", m->nv - nv0);
                m->nv = nv0; m->ns = ns0; m->nidx = ni0; u->held++;
                continue;
            }
        }
        static const char* const loose[] = { "Tongue", "syo_", "cube1", "it_root" };
        int lp = 0;
        for (size_t k = 0; k < sizeof(loose) / sizeof(loose[0]); k++) lp |= !SDL_strncasecmp(e[oi].name, loose[k], strlen(loose[k])) || !SDL_strncasecmp(vis, loose[k], strlen(loose[k]));
        if (lp && m->nv > nv0 && u->family != FAM_NONE) {   /* loose parts reaching far out of the body (5 units past its joints'
            box): the Licker's tongue stretched out 2.5 m, Lisa Trevor's chains, Nemesis's rocket launcher; they would hang in
            the air off the player. (By name: the Tyrant ETY0's claw and Ivy's leaves reach as far and are the body.) */
            float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f }, far = 0;
            for (int r = 1; r < R_BODY; r++)
                for (int c = 0; c < 3; c++) { lo[c] = fminf(lo[c], u->bone[u->core[r]].W[4 * c + 3]); hi[c] = fmaxf(hi[c], u->bone[u->core[r]].W[4 * c + 3]); }
            for (int v = nv0; v < m->nv; v++)
                for (int c = 0; c < 3; c++) far = fmaxf(far, fmaxf(lo[c] - m->pos[v][c], m->pos[v][c] - hi[c]));
            if (far > 5.0f) {
                if (u->dump) fprintf(u->dump, "%s %s %s %d outside %.1f\n", e[oi].name, vis, tslot >= 0 ? u->tname[tslot] : "-", m->nv - nv0, far);
                m->nv = nv0; m->ns = ns0; m->nidx = ni0; u->outside++;
                continue;
            }
        }
        if (m->ns > u->nswrap) {   /* the strips' wrap (cut_tiles) */
            unsigned char* q = realloc(u->swrap, (size_t)m->ns + 1024);
            if (!q) goto out;
            u->swrap = q;
            u->nswrap = m->ns + 1024;
        }
        for (int s = ns0; s < m->ns; s++) {
            int ws = L >= 0 ? mat->l[L].ws : 0, wt = L >= 0 ? mat->l[L].wt : 0;
            u->swrap[s] = (unsigned char)((ws == 1 ? 1 : ws == 2 ? 3 : 0) | (wt == 1 ? 4 : wt == 2 ? 12 : 0));
        }
        m->nmesh++;
        if (u->dump) fprintf(u->dump, "%s %s %s %d body %d%s\n", e[oi].name, vis, tslot >= 0 ? u->tname[tslot] : "-", m->nv - nv0,
                             MT ? 1 : 0, L >= 0 && !mat->l[L].ws && !mat->l[L].wt ? "" : " repeat");
        int hp = hand_pose(vis);
        if ((face || MT || hp) && m->nv > nv0) {   /* bit 0: the jaw's region (the face's own texture or the blink's meshes);
                                                     bits 1..3: the hand pose (IMP_HAND_*) */
            unsigned char* q = realloc(u->face, (size_t)m->nv);
            if (!q) goto out;
            u->face = q;
            if (u->nface < nv0) memset(u->face + u->nface, 0, (size_t)(nv0 - u->nface));
            memset(u->face + nv0, (face || MT) | hp << 1, (size_t)(m->nv - nv0));
            u->nface = m->nv;
        }
    }
    if (u->nface < m->nv) {
        unsigned char* q = realloc(u->face, (size_t)(m->nv ? m->nv : 1));
        if (!q) goto out;
        u->face = q;
        memset(u->face + u->nface, 0, (size_t)(m->nv - u->nface));
        u->nface = m->nv;
    }
    ok = !b->bad && m->nv > 0;
    if (!ok && !err[0]) snprintf(err, en, b->bad ? "Model data is damaged." : "No usable model found.");
out:
#undef SEC
    free(e); free(pa); free(na); free(ua); free(mats); free(mtx_node); free(envn); free(env);
    return ok;
}

/* ---------------------------------------------------------------- textures that repeat
   The monsters repeat (and mirror) their textures: one strip of a zombie's coat runs over u -18..+9. Code: Veronica's
   textures clamp, so a strip is drawn within one tile: a strip inside one tile (per repeating axis) is moved into it
   (mirrored for an odd tile of a mirroring axis); one over several is cut, each triangle clipped at the tile edges
   into pieces that each lie in one tile (new vertices on the cut, their weights, normals and blink offsets
   interpolated). Never clamped: no smeared edge texels. */
typedef struct Cut { ImpModel* m; Uc* u; int cv, ci, cs; } Cut;
static int cut_room(Cut* k, int nv, int nidx, int ns)
{
    if (!grow(k->m, nv, nidx, ns, &k->cv, &k->ci, &k->cs)) return 0;
    if (nv > k->u->nface) {
        unsigned char* q = realloc(k->u->face, (size_t)k->cv);
        if (!q) return 0;
        memset(q + k->u->nface, 0, (size_t)(k->cv - k->u->nface));
        k->u->face = q;
        k->u->nface = k->cv;
    }
    return 1;
}
static int cut_vertex(Cut* k, int a, int b, float t)   /* a new vertex between a and b (t of the way to b); -1 out of memory */
{
    ImpModel* m = k->m;
    int x = m->nv;
    if (!cut_room(k, x + 1, m->nidx, m->ns)) return -1;
    m->nv++;
    float w[8] = { 0 }, n = 0;
    int j[8], nj = 0;
    for (int s = 0; s < 2; s++)
        for (int q = 0; q < 4; q++) {
            int v = s ? b : a, i = 0;
            float f = m->wgt[v][q] * (s ? t : 1 - t);
            if (f <= 0) continue;
            while (i < nj && j[i] != m->jnt[v][q]) i++;
            if (i == nj) j[nj++] = m->jnt[v][q];
            w[i] += f;
        }
    memset(m->jnt[x], 0, 4); memset(m->wgt[x], 0, 16);
    for (int q = 0; q < 4; q++) {   /* (the 4 largest) */
        int best = -1;
        for (int i = 0; i < nj; i++) if (w[i] > 0 && (best < 0 || w[i] > w[best])) best = i;
        if (best < 0) break;
        m->jnt[x][q] = (unsigned char)j[best]; m->wgt[x][q] = w[best]; n += w[best]; w[best] = 0;
    }
    for (int q = 0; q < 4; q++) m->wgt[x][q] = n > 0 ? m->wgt[x][q] / n : q == 0;
    float l = 0;
    for (int c = 0; c < 3; c++) {
        m->pos[x][c] = m->pos[a][c] + (m->pos[b][c] - m->pos[a][c]) * t;
        m->nrm[x][c] = m->nrm[a][c] + (m->nrm[b][c] - m->nrm[a][c]) * t;
        m->morph[x][c] = m->morph[a][c] + (m->morph[b][c] - m->morph[a][c]) * t;
        l += m->nrm[x][c] * m->nrm[x][c];
    }
    for (int c = 0; c < 3; c++) m->nrm[x][c] = l > 0 ? m->nrm[x][c] / sqrtf(l) : c == 1;
    for (int c = 0; c < 2; c++) m->uv[x][c] = m->uv[a][c] + (m->uv[b][c] - m->uv[a][c]) * t;
    k->u->face[x] = k->u->face[a];
    return x;
}
/* the polygon p (n corners) clipped to lo <= uv[ax] <= hi (Sutherland-Hodgman, both edges); its corners in o, their count */
static int cut_clip(Cut* k, const int* p, int n, int ax, float lo, float hi, int* o)
{
    int t[16], nt = 0, no = 0;
    for (int e = 0; e < 2; e++) {
        const int* in = e ? t : p;
        int nin = e ? nt : n, *out = e ? o : t, c = 0;
        float lim = e ? hi : lo, sg = e ? -1.0f : 1.0f;   /* inside: sg * (u - lim) >= 0 */
        for (int i = 0; i < nin && c < 14; i++) {
            int a = in[i], b = in[(i + 1) % nin];
            float da = sg * (k->m->uv[a][ax] - lim), db = sg * (k->m->uv[b][ax] - lim);
            if (da >= 0) out[c++] = a;
            if ((da >= 0) != (db >= 0)) {
                int x = cut_vertex(k, a, b, da / (da - db));
                if (x < 0) return -1;
                out[c++] = x;
            }
        }
        if (e) no = c; else nt = c;
    }
    return no;
}
static float tile_local(float v, float t, int mirror)   /* within tile t, 0..1 */
{
    float l = v - t;
    l = l < 0 ? 0 : l > 1 ? 1 : l;
    return mirror && ((int)t & 1) ? 1 - l : l;
}
static int cut_tiles(Uc* u, ImpModel* m)
{
    Cut k = { m, u, m->nv, m->nidx, m->ns };
    int ns = m->ns, cut = 0;
    for (int s = 0; s < ns; s++) {
        int w = s < u->nswrap ? u->swrap[s] : 0, first = m->strip[s].first, n = m->strip[s].n, tex = m->strip[s].tex;
        float lo[2] = { 1e30f, 1e30f }, hi[2] = { -1e30f, -1e30f }, t0[2];
        if (m->strip[s].clamp || !(w & 5) || n < 3) continue;
        for (int i = 0; i < n; i++)
            for (int c = 0; c < 2; c++) { lo[c] = fminf(lo[c], m->uv[m->idx[first + i]][c]); hi[c] = fmaxf(hi[c], m->uv[m->idx[first + i]][c]); }
        int one = 1;
        for (int c = 0; c < 2; c++) {
            t0[c] = floorf(lo[c] + 1e-4f);
            if ((w & (c ? 4 : 1)) && ceilf(hi[c] - 1e-4f) - 1 > t0[c]) one = 0;
        }
        if (one) {   /* in one tile: moved into it (the strip's vertices are its own: before welding) */
            for (int i = 0; i < n; i++) {
                float* t = m->uv[m->idx[first + i]];
                for (int c = 0; c < 2; c++) if (w & (c ? 4 : 1)) t[c] = tile_local(t[c], t0[c], w & (c ? 8 : 2));
            }
            m->strip[s].clamp = 1;
            continue;
        }
        m->strip[s].n = 0;   /* replaced by its pieces */
        cut++;
        for (int i = 0; i + 2 < n; i++) {
            int tri[3] = { m->idx[first + i], m->idx[first + i + 1], m->idx[first + i + 2] }, a[16], b[16];
            if (tri[0] == tri[1] || tri[1] == tri[2] || tri[0] == tri[2]) continue;
            float tl[2], th[2];
            for (int c = 0; c < 2; c++) {
                tl[c] = th[c] = 0;
                if (!(w & (c ? 4 : 1))) continue;
                float l0 = fminf(m->uv[tri[0]][c], fminf(m->uv[tri[1]][c], m->uv[tri[2]][c])), h0 = fmaxf(m->uv[tri[0]][c], fmaxf(m->uv[tri[1]][c], m->uv[tri[2]][c]));
                tl[c] = floorf(l0 + 1e-4f); th[c] = fmaxf(tl[c], ceilf(h0 - 1e-4f) - 1);
            }
            for (float tu = tl[0]; tu <= th[0]; tu++) {
                int na = (w & 1) ? cut_clip(&k, tri, 3, 0, tu, tu + 1, a) : (memcpy(a, tri, sizeof(tri)), 3);
                if (na < 0) return -1;
                for (float tv = tl[1]; tv <= th[1] && na >= 3; tv++) {
                    int nb = (w & 4) ? cut_clip(&k, a, na, 1, tv, tv + 1, b) : (memcpy(b, a, sizeof(int) * (size_t)na), na);
                    if (nb < 0) return -1;
                    if (nb < 3) continue;
                    /* the piece as a strip of its own corners (their UVs in the tile): 0, 1, n-1, 2, n-2, ... (convex) */
                    if (!cut_room(&k, m->nv + nb, m->nidx + nb, m->ns + 1)) return -1;
                    ImpStrip* st = &m->strip[m->ns++];
                    st->first = m->nidx; st->n = nb; st->tex = tex; st->clamp = 1;
                    for (int q = 0, lo_ = 1, hi_ = nb - 1; q < nb; q++) {
                        int src = q == 0 ? b[0] : (q & 1) ? b[lo_++] : b[hi_--], x = m->nv++;
                        memcpy(m->pos[x], m->pos[src], 12); memcpy(m->nrm[x], m->nrm[src], 12); memcpy(m->morph[x], m->morph[src], 12);
                        memcpy(m->jnt[x], m->jnt[src], 4); memcpy(m->wgt[x], m->wgt[src], 16);
                        m->uv[x][0] = (w & 1) ? tile_local(m->uv[src][0], tu, w & 2) : m->uv[src][0];
                        m->uv[x][1] = (w & 4) ? tile_local(m->uv[src][1], tv, w & 8) : m->uv[src][1];
                        u->face[x] = u->face[src];
                        m->idx[m->nidx++] = x;
                    }
                }
            }
        }
    }
    return cut;
}

/* rotation (3x3 rows) to a quaternion (x, y, z, w) */
static void quat(const float* W, float* q)
{
    float m00 = W[0], m11 = W[5], m22 = W[10], tr = m00 + m11 + m22, s;
    if (tr > 0) { s = sqrtf(tr + 1) * 2; q[3] = s / 4; q[0] = (W[9] - W[6]) / s; q[1] = (W[2] - W[8]) / s; q[2] = (W[4] - W[1]) / s; }
    else if (m00 > m11 && m00 > m22) { s = sqrtf(1 + m00 - m11 - m22) * 2; q[3] = (W[9] - W[6]) / s; q[0] = s / 4; q[1] = (W[1] + W[4]) / s; q[2] = (W[2] + W[8]) / s; }
    else if (m11 > m22) { s = sqrtf(1 + m11 - m00 - m22) * 2; q[3] = (W[2] - W[8]) / s; q[0] = (W[1] + W[4]) / s; q[1] = s / 4; q[2] = (W[6] + W[9]) / s; }
    else { s = sqrtf(1 + m22 - m00 - m11) * 2; q[3] = (W[4] - W[1]) / s; q[0] = (W[2] + W[8]) / s; q[1] = (W[6] + W[9]) / s; q[2] = s / 4; }
    float l = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (int k = 0; k < 4; k++) q[k] = l > 0 ? q[k] / l : k == 3;
}

/* the model's nodes from the bones' world matrices (the glTF of the whole skeleton) */
static void full_nodes(const Uc* u, ImpModel* m)
{
    m->nn = u->nb;
    for (int i = 0; i < u->nb; i++) {
        const float* W = u->bone[i].W;
        int p = u->bone[i].parent;
        ImpNode* n = &m->node[i];
        float L[12];
        memset(n, 0, sizeof(*n));
        n->parent = p;
        if (p < 0) memcpy(L, W, sizeof(L));
        else {   /* L = P^-1 W (P rigid: its rotation transposed) */
            const float* P = u->bone[p].W;
            for (int r = 0; r < 3; r++) {
                for (int c = 0; c < 3; c++) L[4 * r + c] = P[r] * W[c] + P[4 + r] * W[4 + c] + P[8 + r] * W[8 + c];
                L[4 * r + 3] = P[r] * (W[3] - P[3]) + P[4 + r] * (W[7] - P[7]) + P[8 + r] * (W[11] - P[11]);
            }
        }
        n->t[0] = L[3]; n->t[1] = L[7]; n->t[2] = L[11];
        quat(L, n->q);
    }
}

/* ---------------------------------------------------------------- the conversion */
static float smooth(float e0, float e1, float x) { float t = (x - e0) / (e1 - e0); t = t < 0 ? 0 : t > 1 ? 1 : t; return t * t * (3 - 2 * t); }

/* The player characters have no jaw bone (UC's faces talk by animation of their own): one is made under the head.
   Chin and lower lip go to it, its pivot below the ears: on the face's meshes (its texture, the blink's meshes),
   below the mouth's line (a third of the way from the chin up to the eyes: the eyes from the blink morph, else
   half way up the head), in front of the pivot, softly at the edges. Lips only: there is no mouth inside. */
static int make_jaw(ImpModel* r, const unsigned char* face, float rest[ROLES][3])
{
    float yc = 1e9f, ye = 0, top = -1e9f, n = 0, zf = -1e9f;
    for (int v = 0; v < r->nv; v++) {
        float h = 0;
        for (int k = 0; k < 4; k++) if (r->jnt[v][k] == R_HEAD) h += r->wgt[v][k];
        if (h < 0.5f) continue;
        top = fmaxf(top, r->pos[v][1]);
        if (!(face[v] & 1)) continue;
        if (fabsf(r->pos[v][0]) < 0.3f) { yc = fminf(yc, r->pos[v][1]); zf = fmaxf(zf, r->pos[v][2]); }
        if (r->morph && fabsf(r->morph[v][0]) + fabsf(r->morph[v][1]) + fabsf(r->morph[v][2]) > 1e-3f) { ye += r->pos[v][1]; n++; }
    }
    if (yc > 1e8f || zf < rest[R_HEAD][2]) return 0;
    ye = n > 0 ? ye / n : (yc + top) / 2;
    float L = ye - yc, ym = yc + 0.36f * L, zp = rest[R_HEAD][2];
    if (L < 0.3f || L > 3.0f) return 0;
    rest[R_JAW][0] = 0; rest[R_JAW][1] = yc + 0.75f * L; rest[R_JAW][2] = zp;
    int moved = 0;
    for (int v = 0; v < r->nv; v++) {
        if (!(face[v] & 1)) continue;
        const float* p = r->pos[v];
        float j = smooth(ym + 0.03f * L, ym - 0.08f * L, p[1]) * smooth(zp + 0.1f * L, zp + 0.5f * L, p[2]);
        if (j <= 0) continue;
        for (int k = 0; k < 4; k++)
            if (r->jnt[v][k] == R_HEAD && r->wgt[v][k] > 0) {   /* (the head's share moves to the jaw: the neck keeps its own) */
                float w = r->wgt[v][k] * j;
                r->wgt[v][k] -= w;
                int s = -1;
                for (int q = 0; q < 4 && s < 0; q++) if (r->wgt[v][q] <= 0) s = q;
                if (s < 0) s = k, w += r->wgt[v][k], r->wgt[v][k] = 0;
                r->jnt[v][s] = R_JAW; r->wgt[v][s] = w;
                moved++;
                break;
            }
    }
    return moved;
}

/* The fit to Code: Veronica's shoulders (impmodel.h ImpFit; ob2.c ob2_fit): the arms from the clavicles moved in by a part
   of the difference to Claire's width-to-height proportion (shoulder joints 1.4 out at a head height of 15.05; Chris's the
   same). UC's sit at the game's height already (Chris: 0.52 below the neck, CVX's Chris 0.56): not lowered. */
static const struct { const char* id; float in, down; } fits[] = {
    { "", 0.8f, 0.0f },
    /* the monsters keep their build (moved in, the Hunter's arms ran through its chest; Nemesis's, the Tyrants' shoulders are
       their shape); the zombies, Lisa and the office worker are people */
    { "EHT", 0, 0 }, { "ELK", 0, 0 }, { "ENS", 0, 0 }, { "ETY", 0, 0 }, { "EEL", 0, 0 }, { "EIV", 0, 0 },
};

static void uc_fit(const char* id, float rest[ROLES][3], ImpFit* fit)
{
    float in = 0, down = 0;
    size_t best = 0;
    memset(fit, 0, sizeof(*fit));
    for (size_t i = 0; i < sizeof(fits) / sizeof(fits[0]); i++)
        if (!strncmp(id, fits[i].id, strlen(fits[i].id)) && strlen(fits[i].id) >= best) { best = strlen(fits[i].id); in = fits[i].in; down = fits[i].down; }
    float half = (fabsf(rest[R_RSH][0]) + fabsf(rest[R_LSH][0])) * 0.5f, target = rest[R_HEAD][1] * 1.4f / 15.05f;
    float dx = half > target ? in * (half - target) : 0;
    dx = fminf(dx, fminf(fabsf(rest[R_RCLAV][0] - rest[R_CHEST][0]), fabsf(rest[R_LCLAV][0] - rest[R_CHEST][0])) - 0.2f);   /* (the clavicles stay
        0.2 out: the rig takes a sideways child of the chest for an arm only from 0.15) */
    if (dx < 0) dx = 0;
    for (int s = 0; s < 2; s++) {   /* (game space: x turned) */
        int clav = s ? R_LCLAV : R_RCLAV;
        fit->move[s].node = clav;
        fit->move[s].d[0] = (rest[clav + 1][0] > 0 ? 1.0f : -1.0f) * dx;
        fit->move[s].d[1] = -down;
    }
    fit->n = 2;
}

/* v = M u (M 3x3 rows): the rotation turning direction u onto v (the shortest: no twist about either) */
static void rot_from_to(const float* u, const float* v, float* M)
{
    float a[3], b[3], la = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]), lb = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    for (int c = 0; c < 9; c++) M[c] = c % 4 == 0;
    if (la < 1e-6f || lb < 1e-6f) return;
    for (int c = 0; c < 3; c++) { a[c] = u[c] / la; b[c] = v[c] / lb; }
    float x[3] = { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] }, cs = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    if (cs < -0.9999f) return;   /* (opposite: not a limb of a T-pose) */
    float k = 1.0f / (1.0f + cs);   /* Rodrigues: I + [x] + [x]^2 / (1 + cos) */
    M[0] = cs + x[0] * x[0] * k;  M[1] = x[0] * x[1] * k - x[2]; M[2] = x[0] * x[2] * k + x[1];
    M[3] = x[1] * x[0] * k + x[2]; M[4] = cs + x[1] * x[1] * k;  M[5] = x[1] * x[2] * k - x[0];
    M[6] = x[2] * x[0] * k - x[1]; M[7] = x[2] * x[1] * k + x[0]; M[8] = cs + x[2] * x[2] * k;
}
static void mv3(const float* M, const float* p, float* o) { for (int r = 0; r < 3; r++) o[r] = M[3 * r] * p[0] + M[3 * r + 1] * p[1] + M[3 * r + 2] * p[2]; }

/* The limbs turned to Code: Veronica's rest pose. Imported models take the player's world rotations unchanged (they are
   skinned to their own rest pose, actor_glue.c retarget), so a limb that lies otherwise at rest lies otherwise in every
   pose: UC's are straight (arms level, legs plumb), the game's upper arms hang 4 degrees, its thighs lean 4 degrees in
   and 5 back and its shins 6 back. Measured in game on the pose dumps (Chris, Jill: every limb 4..6.5 degrees off, the
   feet 0.5 wider apart and 0.8 ahead of the player's). So the mesh is posed: upper arm, forearm and hand, thigh and shin
   turned onto the game's bone directions (the shortest turn: no twist), the feet moved along without turning (flat on the
   floor as before), by the vertices' weights. Monsters whose arms are not level at rest come out in the same pose. */
static const float cvx_limb[4][3] = {   /* Claire's rest skeleton (RECVX_RIG_DUMP; the hidden copy moving a man has her
                                           offsets too), right side, in UC's frame (the character's right at -x, +z forward) */
    { -2.35f, -0.17f, -0.04f },   /* shoulder to elbow */
    { -2.27f, 0.04f, 0.15f },     /* elbow to wrist */
    { 0.30f, -4.25f, -0.38f },    /* hip to knee */
    { 0.08f, -4.17f, -0.47f },    /* knee to ankle */
};
static void repose(ImpModel* r, float rest[ROLES][3])
{
    static const int chain[4][4] = { { R_RSH, R_REL, R_RWR, 0 }, { R_LSH, R_LEL, R_LWR, 0 }, { R_RTHIGH, R_RKNEE, R_RANKLE, 2 }, { R_LTHIGH, R_LKNEE, R_LANKLE, 2 } };
    float R[ROLES][9], to[ROLES][3];
    for (int k = 0; k < ROLES; k++) { for (int c = 0; c < 9; c++) R[k][c] = c % 4 == 0; memcpy(to[k], rest[k], 12); }
    for (int i = 0; i < 4; i++) {
        const int *ch = chain[i], left = i & 1;
        float d1[3], d2[3], u1[3], u2[3], w[3], A[9], B[9];
        for (int c = 0; c < 3; c++) {
            d1[c] = cvx_limb[ch[3]][c] * (left && !c ? -1 : 1); d2[c] = cvx_limb[ch[3] + 1][c] * (left && !c ? -1 : 1);
            u1[c] = rest[ch[1]][c] - rest[ch[0]][c]; u2[c] = rest[ch[2]][c] - rest[ch[1]][c];
        }
        rot_from_to(u1, d1, R[ch[0]]);
        mv3(R[ch[0]], u1, w);
        for (int c = 0; c < 3; c++) to[ch[1]][c] = to[ch[0]][c] + w[c];
        mv3(R[ch[0]], u2, w);
        rot_from_to(w, d2, A);
        for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) B[3 * a + b] = A[3 * a] * R[ch[0]][b] + A[3 * a + 1] * R[ch[0]][3 + b] + A[3 * a + 2] * R[ch[0]][6 + b];
        memcpy(R[ch[1]], B, sizeof(B));
        mv3(B, u2, w);
        for (int c = 0; c < 3; c++) to[ch[2]][c] = to[ch[1]][c] + w[c];
        if (!ch[3]) memcpy(R[ch[2]], B, sizeof(B));   /* (the hand along the forearm; the foot keeps its turn) */
    }
    for (int v = 0; v < r->nv; v++) {
        float p[3] = { 0 }, n[3] = { 0 }, mo[3] = { 0 }, q[3], o[3];
        for (int j = 0; j < 4; j++) {
            float wt = r->wgt[v][j];
            int k = r->jnt[v][j];
            if (wt <= 0 || k >= ROLES) continue;
            for (int c = 0; c < 3; c++) q[c] = r->pos[v][c] - rest[k][c];
            mv3(R[k], q, o);
            for (int c = 0; c < 3; c++) p[c] += wt * (o[c] + to[k][c]);
            mv3(R[k], r->nrm[v], o);
            for (int c = 0; c < 3; c++) n[c] += wt * o[c];
            if (r->morph) { mv3(R[k], r->morph[v], o); for (int c = 0; c < 3; c++) mo[c] += wt * o[c]; }
        }
        float l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        for (int c = 0; c < 3; c++) { r->pos[v][c] = p[c]; r->nrm[v][c] = l > 0 ? n[c] / l : c == 1; if (r->morph) r->morph[v][c] = mo[c]; }
    }
    memcpy(rest, to, sizeof(to));
}

/* Coat tails and skirts on bones of their own that hang from the hips (Wesker's coat in PWS2: 12 pairs; the office worker's
   skirt) were merged into the pelvis: rigid, the thighs ran through them. A vertex's share on them goes to the thigh on its
   side as far down as it hangs: none at the hips, 0.6 from the knees down, between the legs half on each (as Outbreak's
   flaps_to_thighs, softened: a coat's front swings with the thigh, its middle with both) */
static int tail_bone(const Uc* u, const Dist* d, int i)
{
    if (d[i].n != 1 || d[i].t[0] != R_PELVIS) return 0;
    for (int r = 0; r < ROLES; r++) if (u->core[r] >= 0 && (u->core[r] == i || is_above(u, i, u->core[r]))) return 0;
    return 1;
}

/* the model on the converted skeleton (ROLES nodes): weights summed per role (twist bones split), the face */
static int reduce(Uc* u, const ImpModel* m, ImpModel* r, char* err, int en)
{
    float rest[ROLES][3];
    Dist d[UC_BONES];
    unsigned char tail[UC_BONES];
    memset(r, 0, sizeof(*r));
    for (int i = 0; i < u->nb; i++) {
        d[i] = dist_of(u, i, 0);
        if (d[i].n == 1 && d[i].t[0] == R_ROOT && i != u->core[R_ROOT]) {   /* hung from the root beside the body (the Licker's
            tongue): the nearest joint's */
            float bd = 1e30f;
            for (int k = 1; k < ROLES; k++) {
                float e = 0;
                if (u->core[k] < 0) continue;
                for (int c = 0; c < 3; c++) { float q = u->bone[i].W[4 * c + 3] - u->bone[u->core[k]].W[4 * c + 3]; e += q * q; }
                if (e < bd) { bd = e; d[i].t[0] = k; }
            }
        }
    }
    for (int i = 0; i < u->nb; i++) tail[i] = (unsigned char)tail_bone(u, d, i);
    for (int k = 0; k < ROLES; k++) for (int c = 0; c < 3; c++) rest[k][c] = u->bone[u->core[k >= R_BODY && u->core[k] < 0 ? R_HEAD : k]].W[4 * c + 3];
    rest[R_ROOT][0] = rest[R_ROOT][1] = rest[R_ROOT][2] = 0;
    {   /* a pelvis bone left at the floor (the zombies EZ33, EZ38: _00 at 0, its hips pivot _15 at 11.4): the pivot the thighs
           hang from takes its place (the rig found no arms on a spine rising from the floor: they stayed in their T-pose) */
        int hp = u->bone[u->core[R_RTHIGH]].parent;
        if (hp >= 0 && u->bone[hp].W[7] - rest[R_PELVIS][1] > 2.0f) for (int c = 0; c < 3; c++) rest[R_PELVIS][c] = u->bone[hp].W[4 * c + 3];
    }
    for (int s = 0; s < 2; s++) {   /* the clavicles at least 0.2 out from the chest (family B's start 0.11 out: the rig takes a
        sideways child of the chest for an arm only from 0.15) */
        int c = s ? R_LCLAV : R_RCLAV;
        float dx = rest[c][0] - rest[R_CHEST][0];
        if (fabsf(dx) < 0.2f) rest[c][0] = rest[R_CHEST][0] + (dx < 0 || (dx == 0 && !s) ? -0.2f : 0.2f);
    }
    r->nv = m->nv;
    if (!(r->pos = malloc(12 * (size_t)m->nv)) || !(r->nrm = malloc(12 * (size_t)m->nv)) || !(r->uv = malloc(8 * (size_t)m->nv)) ||
        !(r->jnt = calloc((size_t)m->nv, 4)) || !(r->wgt = calloc((size_t)m->nv, 16)) || !(r->morph = malloc(12 * (size_t)m->nv)) ||
        !(r->idx = malloc(sizeof(int) * (size_t)(m->nidx ? m->nidx : 1))) || !(r->strip = malloc(sizeof(ImpStrip) * (size_t)(m->ns ? m->ns : 1)))) {
        snprintf(err, en, "Out of memory.");
        return 0;
    }
    memcpy(r->pos, m->pos, 12 * (size_t)m->nv); memcpy(r->nrm, m->nrm, 12 * (size_t)m->nv); memcpy(r->uv, m->uv, 8 * (size_t)m->nv);
    memcpy(r->morph, m->morph, 12 * (size_t)m->nv);
    memcpy(r->idx, m->idx, sizeof(int) * (size_t)m->nidx); memcpy(r->strip, m->strip, sizeof(ImpStrip) * (size_t)m->ns);
    r->nidx = m->nidx; r->ns = m->ns; r->nmesh = m->nmesh;
    float hip_y = (rest[R_RTHIGH][1] + rest[R_LTHIGH][1]) / 2, knee_y = (rest[R_RKNEE][1] + rest[R_LKNEE][1]) / 2;
    float hip_x = fmaxf(0.2f, (fabsf(rest[R_RTHIGH][0]) + fabsf(rest[R_LTHIGH][0])) / 2);
    for (int v = 0; v < m->nv; v++) {
        float acc[ROLES] = { 0 }, s = 0, tw = 0;
        for (int k = 0; k < 4; k++) {
            if (m->wgt[v][k] <= 0 || m->jnt[v][k] >= u->nb) continue;
            const Dist* x = &d[m->jnt[v][k]];
            for (int j = 0; j < x->n; j++) acc[x->t[j]] += m->wgt[v][k] * x->f[j];
            if (tail[m->jnt[v][k]]) tw += m->wgt[v][k];
        }
        if (tw > 0) {   /* (coat tails, skirts: tail_bone) */
            float mv = fminf(acc[R_PELVIS], tw * 0.6f * smooth(hip_y, knee_y, m->pos[v][1])), sr = smooth(0.5f * hip_x, -0.5f * hip_x, m->pos[v][0]);
            acc[R_PELVIS] -= mv; acc[R_RTHIGH] += mv * sr; acc[R_LTHIGH] += mv * (1 - sr);
        }
        for (int k = 0; k < 4; k++) {
            int best = -1;
            for (int j = 0; j < ROLES; j++) if (acc[j] > 1e-4f && (best < 0 || acc[j] > acc[best])) best = j;
            if (best < 0) break;
            r->jnt[v][k] = (unsigned char)best; r->wgt[v][k] = acc[best]; s += acc[best]; acc[best] = 0;
        }
        if (s <= 0) { r->jnt[v][0] = R_PELVIS; r->wgt[v][0] = s = 1; }
        for (int k = 0; k < 4; k++) r->wgt[v][k] /= s;
    }
    {   /* the hand poses: a side with both its open and its gun-grip hand (hand_pose) */
        int have = 0;
        for (int v = 0; v < r->nv; v++) have |= 1 << (u->face[v] >> 1);
        for (int s = 0; s < 2; s++)
            if ((have >> (IMP_HAND_R_OPEN + 2 * s) & 3) == 3 && !r->hand && !(r->hand = calloc((size_t)r->nv, 1))) { snprintf(err, en, "Out of memory."); return 0; }
        for (int v = 0; r->hand && v < r->nv; v++) {
            int t = u->face[v] >> 1, s = t >= IMP_HAND_L_OPEN;
            r->hand[v] = (unsigned char)(t && (have >> (IMP_HAND_R_OPEN + 2 * s) & 3) == 3 ? t : 0);
        }
    }
    /* the face: a jaw bone of the model's own (the monsters', the office worker's mouth) turns as the game's angle; the players
       get one made (make_jaw) */
    int jaw = u->core[R_JAW] >= 0 || make_jaw(r, u->face, rest), lids = 0;
    /* (closed lips stretch: Outbreak's angles, made for a mouth that opens, tear them at 14 degrees. A monster's own jaw opens wide
       mouths: at the game's angles the Licker gaped through every line of a cutscene; half of them) */
    r->jaw_gain = u->core[R_JAW] >= 0 ? 0.5f : 0.6f;
    for (int e = 0; e < 2; e++) {   /* lid bones (the Tyrant ETY0's mabu_l / mabu_r): down by 2.5 times the height of what is on them (a thin flap
        over the eye: 0.06 high on ETY0; UC turns it, here it slides) */
        float y0 = 1e9f, y1 = -1e9f;
        if (u->core[R_LIDL + e] < 0) continue;
        for (int v = 0; v < r->nv; v++)
            for (int k = 0; k < 4; k++) if (r->jnt[v][k] == R_LIDL + e && r->wgt[v][k] >= 0.5f) { y0 = fminf(y0, r->pos[v][1]); y1 = fmaxf(y1, r->pos[v][1]); }
        if (y1 > y0) { r->lid_travel[e] = fminf(0.29f, 2.5f * (y1 - y0)); lids = 1; }   /* (left, right: the character's left at +x, as Outbreak's) */
    }
    repose(r, rest);
    r->nn = lids ? ROLES : jaw ? R_JAW + 1 : R_JAW;
    for (int k = 0; k < r->nn; k++) {
        ImpNode* n = &r->node[k];
        int p = role_parent[k];
        n->parent = p; n->keep = 1;
        n->face = k == R_JAW && jaw ? IMP_FACE_JAW : k == R_LIDL && lids ? IMP_FACE_LID_L : k == R_LIDR && lids ? IMP_FACE_LID_R : 0;
        for (int c = 0; c < 3; c++) n->t[c] = rest[k][c] - (p >= 0 ? rest[p][c] : 0);
        n->q[3] = 1;
    }
    return 1;
}

/* ---------------------------------------------------------------- the extraction */
static const struct { const char* key; const char* name; const char* variant; int female; } players[] = {
    { "PCR1", "Chris Redfield", "S.T.A.R.S.", 0 }, { "PCR2", "Chris Redfield", "Umbrella's End", 0 },
    { "PJL1", "Jill Valentine", "S.T.A.R.S.", 1 }, { "PJL2", "Jill Valentine", "Raccoon City", 1 }, { "PJL3", "Jill Valentine", "Umbrella's End", 1 },
    { "PRB1", "Rebecca Chambers", "", 1 }, { "PRC1", "Richard Aiken", "", 0 }, { "PKS1", "Kenneth J. Sullivan", "", 0 },
    { "PBI1", "Billy Coen", "", 0 }, { "PCA1", "Carlos Oliveira", "", 0 }, { "PAD1", "Ada Wong", "Costume 1", 1 }, { "PAD2", "Ada Wong", "Costume 2", 1 },
    { "PHK1", "HUNK", "", 0 }, { "PWS1", "Albert Wesker", "S.T.A.R.S.", 0 }, { "PWS2", "Albert Wesker", "Umbrella's End", 0 },
    { "PWS3", "Albert Wesker", "S.T.A.R.S., Wounded", 0 },
    /* the monsters and others (IMPORT_GROUP_NPCS: "Monsters and Others"): humanoid, on a known skeleton. Left out: the
       dogs, crows, spiders, plants and other shapes (no player motion fits them), ENS2..5 (Nemesis's later forms: 28000
       vertices), placeholders */
    { "EZ00", "Zombie", "1", 0 }, { "EZ01", "Zombie", "2", 0 }, { "EZ02", "Zombie", "3", 0 }, { "EZ03", "Zombie", "4", 0 },
    { "EZ04", "Zombie", "5", 0 }, { "EZ05", "Zombie", "6", 0 }, { "EZ06", "Zombie", "7", 0 }, { "EZ07", "Zombie", "8", 0 },
    { "EZ08", "Zombie", "9", 0 }, { "EZ09", "Zombie", "10", 0 }, { "EZ10", "Zombie", "11", 0 }, { "EZ11", "Zombie", "12", 0 },
    { "EZ12", "Zombie", "13", 0 }, { "EZ13", "Zombie", "14", 0 }, { "EZ14", "Zombie", "15", 0 }, { "EZ15", "Zombie", "16", 0 },
    { "EZ16", "Zombie", "17", 0 }, { "EZ17", "Zombie", "18", 0 }, { "EZ18", "Zombie", "19", 0 }, { "EZ19", "Zombie", "20", 0 },
    { "EZ20", "Zombie", "21", 0 }, { "EZ21", "Zombie", "22", 0 }, { "EZ26", "Zombie", "23", 0 }, { "EZ27", "Zombie", "24", 0 },
    { "EZ28", "Zombie", "25", 0 }, { "EZ29", "Zombie", "26", 0 }, { "EZ30", "Zombie", "27", 0 }, { "EZ31", "Zombie", "28", 0 },
    { "EZ32", "Zombie", "29", 0 }, { "EZ33", "Zombie", "30", 0 }, { "EZ34", "Zombie", "31", 0 }, { "EZ35", "Zombie", "32", 0 },
    { "EZ36", "Zombie", "33", 0 }, { "EZ37", "Zombie", "34", 0 }, { "EZ38", "Zombie", "35", 0 },
    { "EHT1", "Hunter", "", 0 }, { "ELK1", "Licker", "", 0 }, { "ENS1", "Nemesis", "", 0 },
    { "ETY0", "Tyrant", "T-002", 0 }, { "ETY5", "Tyrant", "T-103", 0 }, { "ETY6", "Tyrant", "T-103, 2", 0 }, { "ETY7", "Tyrant", "T-103, 3", 0 },
    { "ELT1", "Lisa Trevor", "", 1 }, { "EEL0", "Eliminator", "", 0 }, { "EIV1", "Ivy", "", 0 }, { "OSIT1", "Office Worker", "", 1 },
};
#define NPLAYERS (int)(sizeof(players) / sizeof(players[0]))

int uc_group(const char* id) { return id[0] == 'P' ? IMPORT_GROUP_PLAYERS : id[0] == 'E' || id[0] == 'O' ? IMPORT_GROUP_NPCS : -1; }

/* one model: <KEY>.arc's <KEY>.brres (+ expression.exp) into dir: the glTF of the whole skeleton, its textures as PNG,
   the .cvxm */
static int convert(const unsigned char* arc, uint32_t an, const char* key, const char* dir, unsigned gindex, ImportItem* it, char* err, int en)
{
    char name[32], path[1300];
    const unsigned char *br, *ex;
    uint32_t bn, xn;
    int ok = 0;
    Uc* u = calloc(1, sizeof(Uc));
    ImpModel m, r;
    Ent tex[256], top[16], mdl[8];
    memset(&m, 0, sizeof(m)); memset(&r, 0, sizeof(r));
    snprintf(name, sizeof(name), "%s.brres", key);
    if (!u || !u8_find(arc, an, name, &br, &bn)) { snprintf(err, en, "The model is missing from %s.arc.", key); free(u); return 0; }
    B b = { br, bn, 0 };
    if (bn < 16 || memcmp(br, "bres", 4)) { snprintf(err, en, "Unsupported model format."); free(u); return 0; }
    int nt = dict(&b, U16(&b, 12) + 8, top, 16), ntex = 0, nmdl = 0;
    for (int i = 0; i < nt; i++) {
        if (!strcmp(top[i].name, "3DModels(NW4R)")) nmdl = dict(&b, top[i].at, mdl, 8);
        if (!strcmp(top[i].name, "Textures(NW4R)")) ntex = dict(&b, top[i].at, tex, 256);
    }
    b.bad = 0;
    if (!nmdl) { snprintf(err, en, "No model in %s.", name); free(u); return 0; }
    if (SDL_getenv("RECVX_IMPORT_MESHES")) {   /* test aid: <dir>/<KEY>.meshes.txt */
        snprintf(path, sizeof(path), "%s/%s.meshes.txt", dir, key);
        u->dump = fopen(path, "w");
    }
    int rd = read_mdl0(&b, mdl[0].at, tex, ntex, u, &m, err, en);
    if (u->dump) fclose(u->dump);
    if (!rd) goto out;
    it->meshes = m.nmesh;
    if (u8_find(arc, an, "expression.exp", &ex, &xn)) read_exp(u, ex, xn);
    for (int k = 0; k < m.ntex; k++) {   /* GX to RGBA, powers of two, 8 bits */
        uint32_t to = find(tex, ntex, u->tname[k]);
        int w = U16(&b, to + 0x1C), h = U16(&b, to + 0x1E), len;
        unsigned char* px = to ? gx_decode(&b, to + (uint32_t)S32(&b, to + 0x10), (int)U32(&b, to + 0x20), w, h) : NULL;
        if (px) px = pow2(px, &w, &h);
        if (!px || !(m.tim2[k] = imp_tim2_from_rgba(px, w, h, &len))) { free(px); snprintf(err, en, "Texture %s uses an unsupported format.", u->tname[k]); goto out; }
        m.tim2_len[k] = len;
        free(px);
        if (SDL_getenv("RECVX_IMPORT_MESHES")) {   /* (test aid: the textures' names by slot) */
            FILE* f;
            snprintf(path, sizeof(path), "%s/%s.meshes.txt", dir, key);
            if ((f = fopen(path, "a"))) { fprintf(f, "tex %d %s\n", k, u->tname[k]); fclose(f); }
        }
    }
    full_nodes(u, &m);
    it->nodes = m.nn; it->textures = m.ntex; it->tris = imp_tris(&m);
    snprintf(path, sizeof(path), "%s/%s.glb", dir, key);
    if (!imp_write_glb(&m, path, key, 0.1f, err, en)) goto out;
    for (int k = 0; k < m.ntex; k++) {
        snprintf(path, sizeof(path), "%s/%s_tex%d.png", dir, key, k);
        imp_write_png(&m, k, path);
    }
    it->ok = 1;
    it->verts = m.nv;
    if (u->family == FAM_NONE) { snprintf(err, en, "Exported only: skeleton not known (%d bones).", u->nb); goto out; }
    if (cut_tiles(u, &m) < 0) { snprintf(err, en, "Out of memory."); goto out; }
    if (!reduce(u, &m, &r, err, en)) goto out;
    imp_weld(&r);
    it->verts = r.nv;
    for (int k = 0; k < m.ntex; k++) { r.tim2[k] = m.tim2[k]; r.tim2_len[k] = m.tim2_len[k]; m.tim2[k] = NULL; }
    r.ntex = m.ntex; m.ntex = 0;
    for (;;) {   /* what the game reads must fit the model buffer (actor_glue.c KEEP_SIZE, 1.5 MB; the cast's three of them in 3 MB):
        the textures at most 1 MB (Nemesis has six of 512x512), the largest halved until they are (the glTF keeps them whole) */
        int big = -1, w, h, len;
        long total = 0, most = 0;
        unsigned char *px, *q;
        for (int k = 0; k < r.ntex; k++) {
            if (!imp_tim2_rgba(r.tim2[k], r.tim2_len[k], &w, &h, &px)) continue;
            free(px);
            total += (long)w * h;
            if ((long)w * h > most && w > 64 && h > 64) { most = (long)w * h; big = k; }
        }
        if (total <= 1L << 20 || big < 0 || !imp_tim2_rgba(r.tim2[big], r.tim2_len[big], &w, &h, &px)) break;
        if (!(q = malloc((size_t)w * h))) { free(px); break; }   /* (w / 2 x h / 2 x 4 bytes) */
        for (int y = 0; y < h / 2; y++)   /* (a 2x2 box) */
            for (int x = 0; x < w / 2; x++)
                for (int c = 0; c < 4; c++)
                    q[4 * (y * (w / 2) + x) + c] = (unsigned char)((px[4 * ((2 * y) * w + 2 * x) + c] + px[4 * ((2 * y) * w + 2 * x + 1) + c] +
                                                                    px[4 * ((2 * y + 1) * w + 2 * x) + c] + px[4 * ((2 * y + 1) * w + 2 * x + 1) + c] + 2) / 4);
        free(px);
        unsigned char* t = imp_tim2_from_rgba(q, w / 2, h / 2, &len);
        free(q);
        if (!t) break;
        free(r.tim2[big]); r.tim2[big] = t; r.tim2_len[big] = len;
    }
    {
        float rest[ROLES][3];
        float (*W)[16] = calloc(r.nn, sizeof(*W));
        ImpFit fit;
        char e2[160] = "";
        if (!W) goto out;
        imp_world(&r, W);
        for (int k = 0; k < r.nn; k++) for (int c = 0; c < 3; c++) rest[k][c] = W[k][12 + c];
        free(W);
        uc_fit(key, rest, &fit);
        snprintf(path, sizeof(path), "%s/%s.cvxm", dir, key);
        if (imp_write_cvxm(&r, path, 1.0f, gindex, &fit, e2, sizeof(e2))) it->player = 1;
        else snprintf(err, en, "Not playable: %s", e2);
        if (it->player && u->held) snprintf(err, en, "Weapons held in the hands left out (%d).", u->held);
        if (it->player && u->outside && !err[0]) snprintf(err, en, "Parts reaching far out of the body left out (%d).", u->outside);
        if (it->player && r.nn <= R_JAW && !err[0]) snprintf(err, en, "No face found for the jaw.");
    }
    ok = 1;
out:
    imp_free(&m); imp_free(&r);
    if (u) free(u->face), free(u->swrap);
    free(u);
    return ok;
}

int uc_extract(const char* iso, const char* outdir, int what, ImportItem* items, int max, int* count)
{
    ImportProgress* pg = &g_import_progress;
    char id[16];
    int fails = 0;
    *count = 0;
    RvzDisc* d = rvz_open(iso, id, sizeof(id));
    if (!d || strcmp(id, "RBUE08")) {
        if (d || id[0]) snprintf(pg->phase, sizeof(pg->phase), tr("Use a US Resident Evil: The Umbrella Chronicles disc image (RBUE08)."));
        else snprintf(pg->phase, sizeof(pg->phase), tr("Cannot read the disc image: it must be an RVZ image (Dolphin): %s"), iso);
        rvz_close(d);
        return -1;
    }
    pg->total = 0;
    for (int i = 0; i < NPLAYERS; i++) pg->total += (what >> uc_group(players[i].key)) & 1;
    for (int i = 0; i < NPLAYERS && *count < max; i++) {
        if (!((what >> uc_group(players[i].key)) & 1)) continue;
        ImportItem* it = &items[(*count)++];
        char path[1200], dir[1200], err[160] = "";
        uint32_t n;
        memset(it, 0, sizeof(*it));
        snprintf(it->id, sizeof(it->id), "%s", players[i].key);
        snprintf(it->name, sizeof(it->name), "%s", players[i].name);
        snprintf(it->variant, sizeof(it->variant), "%s", players[i].variant);
        it->female = players[i].female;
        it->group = uc_group(players[i].key);
        snprintf(pg->phase, sizeof(pg->phase), "%s%s%s%s (%s)", it->name, it->variant[0] ? " (" : "", it->variant, it->variant[0] ? ")" : "", it->id);
        snprintf(path, sizeof(path), "Data/chara/%s.arc", players[i].key);
        unsigned char* arc = rvz_file(d, path, &n);
        if (!arc) snprintf(err, sizeof(err), "Cannot read Data/chara/%s.arc from the disc.", players[i].key);
        else {
            snprintf(dir, sizeof(dir), "%s/%s", outdir, it->id);
            SDL_CreateDirectory(dir);
            convert(arc, n, players[i].key, dir, 0x00E00000u + 16u * (unsigned)i, it, err, sizeof(err));
        }
        free(arc);
        snprintf(it->note, sizeof(it->note), "%s", err);
        fails += !it->ok;
        pg->failed = fails;
        pg->done++;
    }
    if (what & IMPORT_VOICES) uc_voice_extract(d, outdir);   /* (its failures logged: not the models') */
    rvz_close(d);
    return fails;
}
