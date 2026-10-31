/* SMAA's lookup textures, computed at start instead of shipped (aa.c): what SMAA's own scripts
   Scripts/AreaTex.py and Scripts/SearchTex.py generate (https://github.com/iryoku/smaa, MIT,
   THIRD_PARTY_NOTICES.md), ported to C. Area: only the 160x80 slice SMAA 1x reads (subsample
   offset 0: the orthogonal patterns left, the diagonal ones right), RG8; search: 64x16 R8. The
   bytes equal SMAA's Textures/AreaTex.h (its first 80 rows) and Textures/SearchTex.h
   (tests/test_aa_tables.c checks their hashes). SMAA's copyright and license: aa.c. */
#pragma once
#include <math.h>

#define AA_AREA_W 160
#define AA_AREA_H 80
#define AA_SEARCH_W 64
#define AA_SEARCH_H 16

typedef struct { double x, y; } AaV2;
static AaV2 aa_v2(double x, double y) { AaV2 v = { x, y }; return v; }

/* area under the line p1 -> p2 for the pixel x..x+1 (AreaTex.py areaortho.area) */
static AaV2 aa_area_line(AaV2 p1, AaV2 p2, int x)
{
    double dx = p2.x - p1.x, dy = p2.y - p1.y, x1 = x, x2 = x + 1.0;
    double y1 = p1.y + dy * (x1 - p1.x) / dx, y2 = p1.y + dy * (x2 - p1.x) / dx;
    double ip;
    int inside = (x1 >= p1.x && x1 < p2.x) || (x2 > p1.x && x2 <= p2.x);
    if (!inside) return aa_v2(0, 0);
    if (copysign(1.0, y1) == copysign(1.0, y2) || fabs(y1) < 1e-4 || fabs(y2) < 1e-4) {   /* trapezoid */
        double a = (y1 + y2) / 2.0;
        return a < 0.0 ? aa_v2(fabs(a), 0.0) : aa_v2(0.0, fabs(a));
    } else {   /* two triangles */
        double xc = -p1.y * dx / dy + p1.x;
        double a1 = xc > p1.x ? y1 * modf(xc, &ip) / 2.0 : 0.0;
        double a2 = xc < p2.x ? y2 * (1.0 - modf(xc, &ip)) / 2.0 : 0.0;
        double a = fabs(a1) > fabs(a2) ? a1 : -a2;
        return a < 0.0 ? aa_v2(fabs(a1), fabs(a2)) : aa_v2(fabs(a2), fabs(a1));
    }
}

static AaV2 aa_smooth_sum(double d, AaV2 a1, AaV2 a2)   /* smootharea, summed */
{
    double p = d / 32.0;
    AaV2 b1, b2;
    p = p < 0.0 ? 0.0 : p > 1.0 ? 1.0 : p;
    b1 = aa_v2(sqrt(a1.x * 2.0) * 0.5, sqrt(a1.y * 2.0) * 0.5);
    b2 = aa_v2(sqrt(a2.x * 2.0) * 0.5, sqrt(a2.y * 2.0) * 0.5);
    a1 = aa_v2(b1.x + (a1.x - b1.x) * p, b1.y + (a1.y - b1.y) * p);
    a2 = aa_v2(b2.x + (a2.x - b2.x) * p, b2.y + (a2.y - b2.y) * p);
    return aa_v2(a1.x + a2.x, a1.y + a2.y);
}

/* AreaTex.py areaortho at subsample offset 0 */
static AaV2 aa_area_ortho(int pattern, int left, int right)
{
    double d = left + right + 1, o1 = 0.5, o2 = -0.5;
    AaV2 z = aa_v2(0, 0), m = aa_v2(d / 2.0, 0.0);
    switch (pattern) {
    case 1: return left <= right ? aa_area_line(aa_v2(0.0, o2), m, left) : z;
    case 2: return left >= right ? aa_area_line(m, aa_v2(d, o2), left) : z;
    case 3: return aa_smooth_sum(d, aa_area_line(aa_v2(0.0, o2), m, left), aa_area_line(m, aa_v2(d, o2), left));
    case 4: return left <= right ? aa_area_line(aa_v2(0.0, o1), m, left) : z;
    case 6: case 7: case 14: return aa_area_line(aa_v2(0.0, o1), aa_v2(d, o2), left);
    case 8: return left >= right ? aa_area_line(m, aa_v2(d, o1), left) : z;
    case 9: case 11: case 13: return aa_area_line(aa_v2(0.0, o2), aa_v2(d, o1), left);
    case 12: return aa_smooth_sum(d, aa_area_line(aa_v2(0.0, o1), m, left), aa_area_line(m, aa_v2(d, o1), left));
    default: return z;   /* 0, 5, 10, 15 */
    }
}

/* AreaTex.py areadiag at subsample offset (0, 0): the share of 30x30 samples on each side */
static double aa_diag_area1(AaV2 p1, AaV2 p2, AaV2 p)
{
    int n = 0;
    for (int x = 0; x < 30; x++)
        for (int y = 0; y < 30; y++) {
            double px = p.x + x / 29.0, py = p.y + y / 29.0;
            if (p1.x != p2.x || p1.y != p2.y) {
                double xm = (p1.x + p2.x) / 2.0, ym = (p1.y + p2.y) / 2.0;
                n += (p2.y - p1.y) * (px - xm) + (p1.x - p2.x) * (py - ym) > 0;
            } else n++;
        }
    return n / 900.0;
}
static AaV2 aa_diag_area(AaV2 p1, AaV2 p2, int left)
{
    return aa_v2(1.0 - aa_diag_area1(p1, p2, aa_v2(1.0 + left, 0.0 + left)), aa_diag_area1(p1, p2, aa_v2(1.0 + left, 1.0 + left)));
}
static AaV2 aa_area_diag(int pattern, int left, int right)
{
    /* each pattern: one or two lines (start x, y; end x, y offset by d), averaged */
    static const signed char ln[16][2][4] = {
        { { 1, 1, 1, 1 }, { 1, 0, 1, 0 } }, { { 1, 0, 0, 0 }, { 1, 0, 1, 0 } }, { { 0, 0, 1, 0 }, { 1, 0, 1, 0 } }, { { 1, 0, 1, 0 }, { -1 } },
        { { 1, 1, 0, 0 }, { 1, 1, 1, 0 } }, { { 1, 1, 0, 0 }, { 1, 0, 1, 0 } }, { { 1, 1, 1, 0 }, { -1 } }, { { 1, 1, 1, 0 }, { 1, 0, 1, 0 } },
        { { 0, 0, 1, 1 }, { 1, 0, 1, 1 } }, { { 1, 0, 1, 1 }, { -1 } }, { { 0, 0, 1, 1 }, { 1, 0, 1, 0 } }, { { 1, 0, 1, 1 }, { 1, 0, 1, 0 } },
        { { 1, 1, 1, 1 }, { -1 } }, { { 1, 1, 1, 1 }, { 1, 0, 1, 1 } }, { { 1, 1, 1, 1 }, { 1, 1, 1, 0 } }, { { 1, 1, 1, 1 }, { 1, 0, 1, 0 } } };
    double d = left + right + 1;
    AaV2 a = aa_diag_area(aa_v2(ln[pattern][0][0], ln[pattern][0][1]), aa_v2(ln[pattern][0][2] + d, ln[pattern][0][3] + d), left), b;
    if (ln[pattern][1][0] < 0) return a;
    b = aa_diag_area(aa_v2(ln[pattern][1][0], ln[pattern][1][1]), aa_v2(ln[pattern][1][2] + d, ln[pattern][1][3] + d), left);
    return aa_v2((a.x + b.x) / 2.0, (a.y + b.y) / 2.0);
}

static void aa_area_table(unsigned char* out)   /* AA_AREA_W x AA_AREA_H, RG */
{
    static const unsigned char eo[16][2] = { { 0, 0 }, { 3, 0 }, { 0, 3 }, { 3, 3 }, { 1, 0 }, { 4, 0 }, { 1, 3 }, { 4, 3 },
                                             { 0, 1 }, { 3, 1 }, { 0, 4 }, { 3, 4 }, { 1, 1 }, { 4, 1 }, { 1, 4 }, { 4, 4 } };
    static const unsigned char ed[16][2] = { { 0, 0 }, { 1, 0 }, { 0, 2 }, { 1, 2 }, { 2, 0 }, { 3, 0 }, { 2, 2 }, { 3, 2 },
                                             { 0, 1 }, { 1, 1 }, { 0, 3 }, { 1, 3 }, { 2, 1 }, { 3, 1 }, { 2, 3 }, { 3, 3 } };
    for (int i = 0; i < AA_AREA_W * AA_AREA_H * 2; i++) out[i] = 0;
    for (int p = 0; p < 16; p++) {
        for (int l = 0; l < 16; l++)
            for (int r = 0; r < 16; r++) {   /* distances stored at their square roots */
                AaV2 a = aa_area_ortho(p, l * l, r * r);
                unsigned char* o = out + ((r + 16 * eo[p][1]) * AA_AREA_W + l + 16 * eo[p][0]) * 2;
                o[0] = (unsigned char)(int)(255.0 * a.x); o[1] = (unsigned char)(int)(255.0 * a.y);
            }
        for (int l = 0; l < 20; l++)
            for (int r = 0; r < 20; r++) {
                AaV2 a = aa_area_diag(p, l, r);
                unsigned char* o = out + ((r + 20 * ed[p][1]) * AA_AREA_W + 80 + l + 20 * ed[p][0]) * 2;
                o[0] = (unsigned char)(int)(255.0 * a.x); o[1] = (unsigned char)(int)(255.0 * a.y);
            }
    }
}

static void aa_search_table(unsigned char* out)   /* AA_SEARCH_W x AA_SEARCH_H */
{
    /* SearchTex.py: for a bilinear fetch of two edge pairs (texcoord k/32, k = which edges), how
       far the search went too far to the left (first 33 columns) or right (the next 33) */
    unsigned char img[33][66] = { { 0 } };
    signed char key[33];
    for (int k = 0; k < 33; k++) key[k] = -1;
    for (int e = 0; e < 16; e++) {   /* bilinear() of edges e (bit 3 = e[0]) */
        double a = (e >> 3 & 1) + ((e >> 2 & 1) - (e >> 3 & 1)) * 0.75, b = (e >> 1 & 1) + ((e & 1) - (e >> 1 & 1)) * 0.75;
        double v = a + (b - a) * 0.875;
        for (int k = 0; k < 33; k++) if (0.03125 * k == v) key[k] = (signed char)e;
    }
    for (int x = 0; x < 33; x++)
        for (int y = 0; y < 33; y++) {
            int l, t, dl = 0, dr = 0;
            if (key[x] < 0 || key[y] < 0) continue;
            l = key[x]; t = key[y];   /* left[i] = bit 3 - i */
#define AA_B(e, i) (((e) >> (3 - (i))) & 1)
            if (AA_B(t, 3)) dl++;
            if (dl == 1 && AA_B(t, 2) && !AA_B(l, 1) && !AA_B(l, 3)) dl++;
            if (AA_B(t, 3) && !AA_B(l, 1) && !AA_B(l, 3)) dr++;
            if (dr == 1 && AA_B(t, 2) && !AA_B(l, 0) && !AA_B(l, 2)) dr++;
#undef AA_B
            img[y][x] = (unsigned char)(127 * dl);
            img[y][33 + x] = (unsigned char)(127 * dr);
        }
    for (int r = 0; r < AA_SEARCH_H; r++)   /* rows 17..32, upside down; columns 0..63 */
        for (int x = 0; x < AA_SEARCH_W; x++) out[r * AA_SEARCH_W + x] = img[32 - r][x];
}
