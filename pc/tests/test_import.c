/* Imports (pc/src/import, docs/IMPORT_OUTBREAK.md): the SLD decoder, TIM2 decoding (the CSM1
   palette order), the two writers on a small synthetic humanoid (no disc data), and the "You
   appear as" registry growing append-only. */
#include "../src/import/impmodel.h"
#include "../include/pc_parse.h"
#include "../src/import/import.h"
#include "../src/host/actors.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

int ob2_sld(const unsigned char* src, int n, unsigned char* dst, int cap);
int ob2_read_nbd(const unsigned char* d, uint32_t n, ImpModel* m, char* err, int en);
void host_log(const char* fmt, ...) { (void)fmt; }
const unsigned char FontSz[126];                 /* (the decomp's, for import/lang.c) */
unsigned int fstbl[24], fsheader[24];            /* (set by the languages' test) */
unsigned int wallpaper[24];                      /* (the decomp's, for import/lang.c) */
void* comevt_message_tab[8];
const char* tr(const char* en) { return en; }   /* (host/tr.c) */
void voice_import_load(const char* text) { (void)text; }   /* (host/voices.c) */
static uint32_t rd(const unsigned char* p) { uint32_t v; memcpy(&v, p, 4); return v; }

/* an 8-bit 16x16 TIM2 (16-byte header): pixel i has index i, palette entry k is (k, 0, 0, 0x80) */
static unsigned char* tim2(int* len)
{
    int n = 16 + 48 + 256 + 1024;
    unsigned char* t = calloc(1, n);
    uint32_t total = 48 + 256 + 1024, clut = 1024, img = 256;
    uint16_t hs = 48, nc = 256, w = 16, h = 16;
    memcpy(t, "TIM2", 4); t[4] = 4; t[6] = 1;
    memcpy(t + 16, &total, 4); memcpy(t + 20, &clut, 4); memcpy(t + 24, &img, 4); memcpy(t + 28, &hs, 2); memcpy(t + 30, &nc, 2);
    t[33] = 1; t[34] = 3; t[35] = 5; memcpy(t + 36, &w, 2); memcpy(t + 38, &h, 2);
    for (int i = 0; i < 256; i++) { t[64 + i] = (unsigned char)i; t[64 + 256 + 4 * i] = (unsigned char)i; t[64 + 256 + 4 * i + 3] = 0x80; }
    *len = n;
    return t;
}

/* an ALD of 2 item names and the messages t[0..n) (u16 texts; 0 = empty), as lang_sysmes reads it */
static unsigned char* ald(const uint16_t* t, int n, uint32_t* size)
{
    uint32_t ni = 4 + 4 * 2 + 4 * 2, ns = 4 + 4 * n, o, k;
    static const uint16_t take[] = { 0xFF03, 0xFFFF, 33, 0xFFFF };   /* t[i] 0xFF03: "<the item's name>A" */
    for (int i = 0; i < n; i++) ns += t[i] == 0xFF03 ? 8 : t[i] ? 4 : 2;
    unsigned char* b = calloc(1, 8 + ni + ns);
    uint16_t name[2] = { 33, 0xFFFF };
    memcpy(b, &ni, 4);
    k = 2; memcpy(b + 4, &k, 4);
    for (k = 0; k < 2; k++) { o = 12 + 4 * k; memcpy(b + 4 + 4 + 4 * k, &o, 4); memcpy(b + 4 + o, name, 4); }
    memcpy(b + 4 + ni, &ns, 4);
    unsigned char* m = b + 8 + ni;
    k = (uint32_t)n; memcpy(m, &k, 4);
    o = 4 + 4 * n;
    for (int i = 0; i < n; i++) {
        uint16_t s[2] = { t[i], 0xFFFF };
        memcpy(m + 4 + 4 * i, &o, 4);
        memcpy(m + o, t[i] == 0xFF03 ? take : t[i] ? s : s + 1, t[i] == 0xFF03 ? 8 : t[i] ? 4 : 2);
        o += t[i] == 0xFF03 ? 8 : t[i] ? 4 : 2;
    }
    *size = 8 + ni + ns;
    return b;
}
/* message i of an ALD's system messages: its first code (0xFFFF: empty) */
static uint16_t ald_mes(const unsigned char* b, int i)
{
    uint32_t ni = rd(b), o = rd(b + 8 + ni + 4 + 4 * i);
    uint16_t c;
    memcpy(&c, b + 8 + ni + o, 2);
    return c;
}

int main(int argc, char** argv)
{
    const char* dir = argc > 1 ? argv[1] : ".";
    char p[1024];
    /* SLD: literals 1, 2; a copy of 3 words from 2 back; 2 zero words; the end */
    uint16_t in[] = { 0x3800, 1, 2, (3 << 11) | 2, (2 << 11) | 0, 0, 0 }, out[16];   /* flags: lit, lit, copy, copy, copy */
    CHECK(ob2_sld((unsigned char*)in, sizeof(in), (unsigned char*)out, sizeof(out)) == 7 * 2);
    CHECK(out[0] == 1 && out[1] == 2 && out[2] == 1 && out[3] == 2 && out[4] == 1 && out[5] == 0 && out[6] == 0);
    CHECK(ob2_sld((unsigned char*)in, sizeof(in), (unsigned char*)out, 8) < 0);   /* (no room: refused, not overrun) */
    {   /* at an odd address (the words are read and written bytewise) */
        unsigned char ib[sizeof(in) + 1], ob[sizeof(out) + 1];
        memcpy(ib + 1, in, sizeof(in));
        CHECK(ob2_sld(ib + 1, sizeof(in), ob + 1, sizeof(out)) == 7 * 2 && !memcmp(ob + 1, out, 7 * 2));
    }

    {   /* NBD: a 16-byte file "TEX" with a 256-byte entry table: refused before its table is looked at
           (an allocation of exactly 16 bytes: ASan sees any read past it) */
        ImpModel nm;
        char err[160];
        unsigned char* nbd = calloc(1, 16);
        memcpy(nbd, "TEX", 4); nbd[4] = 0; nbd[5] = 1;   /* hs = 256 */
        CHECK(!ob2_read_nbd(nbd, 16, &nm, err, sizeof(err)));
        nbd = realloc(nbd, 48); memset(nbd + 16, 0, 32); nbd[4] = 48; nbd[5] = 0;   /* hs = 48, no AMO / AHI */
        CHECK(!ob2_read_nbd(nbd, 48, &nm, err, sizeof(err)));
        free(nbd);
    }

    {   /* languages (docs/LANGUAGES.md "System messages"): the US layout 280 messages, 24 files of one page (280..303),
           the load question 304, 305, two empty, 3 memory card messages (308..310); the language's file 0 has two pages
           (its files 280..304, the load question 305, 306, one empty, the card 308..310) */
        uint16_t us[311] = { 0 }, l[311] = { 0 };
        unsigned fl[24] = { 1 }, hl[24], ho[24];
        uint32_t nus, n;
        for (int f = 0; f < 24; f++) { fstbl[f] = 0; fsheader[f] = f; hl[f] = f + (f > 0); }
        for (int i = 0; i < 311; i++) { us[i] = i < 306 || i > 307 ? 1000 + i : 0; l[i] = i == 307 ? 0 : 2000 + i; }
        l[157] = us[157] = 0xFF03;   /* "Take the <item>?": 0xFF03 0xFFFF (the current item) is not its end */
        unsigned char *ub = ald(us, 311, &nus), *lb = ald(l, 311, &n), *o = lang_sysmes(ub, nus, lb, &n, fl, hl, ho);
        CHECK(o);
        CHECK(ald_mes(o, 0) == 2000 && ald_mes(o, 279) == 2279 && ald_mes(o, 156) == 2156 && ald_mes(o, 158) == 2158);   /* the same up to the files */
        {   /* the item message whole: its text after the operand too */
            uint32_t ni = rd(o), m = rd(o + 8 + ni + 4 + 4 * 157), m2 = rd(o + 8 + ni + 4 + 4 * 158);
            uint16_t c[4];
            memcpy(c, o + 8 + ni + m, 8);
            CHECK(m2 - m == 8 && c[0] == 0xFF03 && c[1] == 0xFFFF && c[2] == 33 && c[3] == 0xFFFF);
        }
        CHECK(ald_mes(o, 280) == 2280 && ald_mes(o, 281) == 2282 && ald_mes(o, 303) == 2304);   /* page 0 of each file */
        CHECK(ald_mes(o, 304) == 2305 && ald_mes(o, 305) == 2306);        /* the load question */
        CHECK(ald_mes(o, 306) == 0xFFFF && ald_mes(o, 307) == 0xFFFF);
        CHECK(ald_mes(o, 308) == 2308 && ald_mes(o, 310) == 2310);        /* the memory card block, from the end */
        CHECK(ald_mes(o, 311) == 2280 && ald_mes(o, 312) == 2281 && ald_mes(o, 311 + 24) == 2304);   /* its own pages appended */
        CHECK(ho[0] == 31 && ho[1] == 33 && ho[23] == 31 + 24);           /* (311 - 280 + its first pages) */
        fl[23] = 15; n = nus;   /* a layout that does not fit (its files run into the card block): refused */
        CHECK(!lang_sysmes(ub, nus, lb, &n, fl, hl, ho));
        free(ub); free(lb); free(o);
    }

    /* TIM2: CSM1 swaps palette entries 8..15 and 16..23 of every 32 */
    int len, w, h;
    unsigned char *t = tim2(&len), *px;
    CHECK(imp_tim2_rgba(t, len, &w, &h, &px) && w == 16 && h == 16);
    CHECK(px[4 * 8] == 16 && px[4 * 16] == 8 && px[4 * 7] == 7 && px[4 * 24] == 24 && px[3] == 255);
    free(px);
    /* Outbreak's CLUT alpha (0xFF opaque) to the PS2's (0x80 opaque) */
    t[64 + 256 + 3] = 0xFF; t[64 + 256 + 4 + 3] = 0x80; t[64 + 256 + 8 + 3] = 0;
    imp_tim2_alpha_255(t, len);
    CHECK(t[64 + 256 + 3] == 0x80 && t[64 + 256 + 4 + 3] == 0x40 && t[64 + 256 + 8 + 3] == 0);

    /* a humanoid: root, pelvis, spine, chest, neck, head, two arms of 3, two legs of 3, and a finger
       (zero length, merged into its hand) */
    static const struct { int par; float t[3]; int keep; } sk[] = {
        { -1, { 0, 0, 0 }, 1 }, { 0, { 0, 100, 0 }, 1 }, { 1, { 0, 10, 0 }, 1 }, { 2, { 0, 20, 0 }, 1 },
        { 3, { 0, 20, 0 }, 1 }, { 4, { 0, 8, 0 }, 1 },
        { 3, { 10, 15, 0 }, 1 }, { 6, { 25, 0, 0 }, 1 }, { 7, { 25, 0, 0 }, 1 },
        { 3, { -10, 15, 0 }, 1 }, { 9, { -25, 0, 0 }, 1 }, { 10, { -25, 0, 0 }, 1 },
        { 1, { 8, -10, 0 }, 1 }, { 12, { 0, -40, 0 }, 1 }, { 13, { 0, -40, 0 }, 1 },
        { 1, { -8, -10, 0 }, 1 }, { 15, { 0, -40, 0 }, 1 }, { 16, { 0, -40, 0 }, 1 },
        { 8, { 0, 0, 0 }, 0 },
    };
    ImpModel m;
    memset(&m, 0, sizeof(m));
    m.nn = (int)(sizeof(sk) / sizeof(sk[0]));
    for (int i = 0; i < m.nn; i++) { m.node[i].parent = sk[i].par; memcpy(m.node[i].t, sk[i].t, 12); m.node[i].q[3] = 1; m.node[i].keep = sk[i].keep; }
    m.nv = 4;
    m.pos = calloc(4, sizeof(*m.pos)); m.nrm = calloc(4, sizeof(*m.nrm)); m.uv = calloc(4, sizeof(*m.uv));
    m.jnt = calloc(4, sizeof(*m.jnt)); m.wgt = calloc(4, sizeof(*m.wgt));
    for (int i = 0; i < 4; i++) {
        m.pos[i][0] = 30.0f + 5 * (i & 1); m.pos[i][1] = 140.0f + 5 * (i >> 1); m.nrm[i][2] = 1;
        m.uv[i][0] = 2.25f + 0.5f * (i & 1); m.uv[i][1] = 1.97f + 0.12f * (i >> 1);   /* (u past 1: moved back by whole tiles; v across 2) */
        m.jnt[i][0] = 18; m.wgt[i][0] = 0.75f; m.jnt[i][1] = 7; m.wgt[i][1] = 0.25f;   /* the finger: its hand takes it */
    }
    m.ns = 1; m.nidx = 4;
    m.strip = calloc(1, sizeof(ImpStrip)); m.idx = calloc(4, sizeof(int));
    for (int i = 0; i < 4; i++) m.idx[i] = i;
    m.strip[0].n = 4;
    m.ntex = 2; m.tim2[0] = t; m.tim2_len[0] = len; m.tim2[1] = tim2(&m.tim2_len[1]);
    CHECK(imp_tris(&m) == 2);

    char err[160] = "";
    snprintf(p, sizeof(p), "%s/test_import.cvxm", dir);
    ImpFit fit = { 1, { { 6, { 0.5f, -0.25f, 0 } } } };   /* the right arm in by 0.5 and down by 0.25 (game units) */
    CHECK(imp_write_cvxm(&m, p, 0.1f, 0xF00000, &fit, err, sizeof(err)) || (printf("%s\n", err), 0));
    FILE* f = fopen(p, "rb");
    CHECK(f);
    static unsigned char d[1 << 20];
    size_t n = fread(d, 1, sizeof(d), f);
    fclose(f);
    CHECK(rd(d) == CVXM_MAGIC && rd(d + 4) == CVXM_VERSION && rd(d + 8) == 60 && rd(d + 20) == 4 && rd(d + 24) == 18);   /* 18 nodes kept */
    CHECK(!memcmp(d + 64, "MDL", 3) && d[67] == 0xbc && rd(d + 60) + 64 + 4 <= rd(d + 12));
    CHECK(d[28] == 255 && d[32] == 255);   /* no face bones */
    {   /* the texture chunks follow each other (the game's loader steps by 32 + size) up to the end chunk */
        uint32_t q = rd(d + 12), k = 0;
        while (q + 8 <= n && rd(d + q) == 0x324D4954) { CHECK(rd(d + q + 32 + 8) == 0xF00000 + k); q += 32 + rd(d + q + 4); k++; }
        CHECK(k == 2 && rd(d + q) == 0xFFFFFFFF);
    }
    {   /* the strip (CsUvh: index, u, v): u moved back by 2 tiles; v 1.97..2.09 into the tile that holds most of it (0..0.09) */
        static const uint16_t want[9] = { 0, 256, 0, 1, 767, 0, 2, 256, 92 };
        int found = 0;
        for (size_t q = 64; q + sizeof(want) <= n && !found; q += 2) found = !memcmp(d + q, want, sizeof(want));
        CHECK(found);
    }
    {   /* skin: the finger's weight went to its hand, the largest first, normalised */
        const CvxmSkin* s = (const CvxmSkin*)(d + rd(d + 16));
        CHECK(s[0].w[0] > 0.74f && s[0].w[0] < 0.76f && s[0].w[1] > 0.24f && s[0].w[2] == 0 && s[0].node[0] != s[0].node[1]);
        CHECK(s[0].pos[0] < -2.49f && s[0].pos[0] > -2.51f && s[0].pos[1] > 13.74f && s[0].pos[1] < 13.76f);   /* turned (x negated), scaled, moved with its arm */
    }
    snprintf(p, sizeof(p), "%s/test_import.glb", dir);
    CHECK(imp_write_glb(&m, p, "test", 0.01f, err, sizeof(err)) || (printf("%s\n", err), 0));
    f = fopen(p, "rb");
    CHECK(f);
    n = fread(d, 1, sizeof(d), f);
    fclose(f);
    CHECK(rd(d) == 0x46546C67 && rd(d + 4) == 2 && rd(d + 8) == n && rd(d + 16) == 0x4E4F534A);
    CHECK(strstr((char*)d + 20, "\"skins\"") && strstr((char*)d + 20, "\"inverseBindMatrices\""));

    {   /* Umbrella Chronicles (uc.c): a vertex per corner welded (the UVs move to the strip), the blink morph, clamped UVs outside 0..1 moved into the tile (the round 9 rule) */
        float (*pos)[3] = realloc(m.pos, 8 * 12), (*nrm)[3] = realloc(m.nrm, 8 * 12), (*uv)[2] = realloc(m.uv, 8 * 8), (*wg)[4] = realloc(m.wgt, 8 * 16);
        unsigned char (*jn)[4] = realloc(m.jnt, 8 * 4);
        int* ix = realloc(m.idx, 8 * sizeof(int));
        CHECK(pos && nrm && uv && wg && jn && ix);
        m.pos = pos; m.nrm = nrm; m.uv = uv; m.wgt = wg; m.jnt = jn; m.idx = ix;
        m.morph = calloc(8, 12);
        for (int i = 4; i < 8; i++) {   /* the same four vertices again, other UVs */
            memcpy(m.pos[i], m.pos[i - 4], 12); memcpy(m.nrm[i], m.nrm[i - 4], 12); memcpy(m.jnt[i], m.jnt[i - 4], 4); memcpy(m.wgt[i], m.wgt[i - 4], 16);
            m.uv[i][0] = 0.1f * i; m.uv[i][1] = 0.5f;
        }
        m.morph[1][1] = m.morph[5][1] = -0.4f;   /* (the lid: down when closed) */
        m.nv = 8; m.nidx = 8; m.ns = 2;
        ImpStrip* st = realloc(m.strip, 2 * sizeof(ImpStrip));
        CHECK(st);
        m.strip = st;
        for (int i = 0; i < 8; i++) m.idx[i] = i;
        m.strip[0] = (ImpStrip){ 0, 4, 0, 1 }; m.strip[1] = (ImpStrip){ 4, 4, 1, 1 };
        CHECK(imp_weld(&m) == 4 && m.idx[5] == m.idx[1] && m.iuv[5][0] > 0.49f && m.iuv[5][0] < 0.51f && m.iuv[0][0] == 2.25f);
        snprintf(p, sizeof(p), "%s/test_import_uc.cvxm", dir);
        CHECK(imp_write_cvxm(&m, p, 1.0f, 0xE00000, NULL, err, sizeof(err)) || (printf("%s\n", err), 0));
        f = fopen(p, "rb");
        CHECK(f);
        n = fread(d, 1, sizeof(d), f);
        fclose(f);
        PcCvxm cv;
        char why[96];
        CHECK(pc_cvxm_check(d, (uint32_t)n, &cv, why, sizeof(why)) && cv.nv == 4 && cv.nmorph == 1 && rd(d + 4) == 5);
        const CvxmMorph* mo = (const CvxmMorph*)(d + cv.morph);
        CHECK(mo->v == (uint32_t)m.idx[1] && mo->d[1] < -0.39f && mo->d[1] > -0.41f && mo->d[0] == 0);
        const uint16_t want[6] = { (uint16_t)m.idx[0], 256, 0, (uint16_t)m.idx[1], 767, 0 };   /* (clamped and outside 0..1: moved by whole tiles
                                                                                              as Outbreak's, u 2.25 -> 0.25, v 1.97..2.09 into tile 2) */
        int found = 0;
        for (size_t q = 64; q + sizeof(want) <= n && !found; q += 2) found = !memcmp(d + q, want, sizeof(want));
        CHECK(found);
        /* the hand poses: version 6 with a byte per vertex (Outbreak's files, without them, stay version 5: above) */
        CHECK((m.hand = calloc((size_t)m.nv, 1)));
        m.hand[m.nv - 1] = IMP_HAND_R_GRIP;
        CHECK(imp_write_cvxm(&m, p, 1.0f, 0xE00000, NULL, err, sizeof(err)) || (printf("%s\n", err), 0));
        f = fopen(p, "rb");
        CHECK(f);
        n = fread(d, 1, sizeof(d), f);
        fclose(f);
        CHECK(pc_cvxm_check(d, (uint32_t)n, &cv, why, sizeof(why)) && rd(d + 4) == 6 && cv.hand && d[cv.hand + m.nv - 1] == IMP_HAND_R_GRIP && !d[cv.hand]);
        d[cv.hand] = 9;   /* (a pose that is none: refused) */
        CHECK(!pc_cvxm_check(d, (uint32_t)n, &cv, why, sizeof(why)));
        /* a picture as an 8-bit TIM2: up to 256 colours exactly */
        unsigned char rgba[16 * 16 * 4], *back;
        for (int i = 0; i < 256; i++) { rgba[4 * i] = (unsigned char)i; rgba[4 * i + 1] = (unsigned char)(255 - i); rgba[4 * i + 2] = 7; rgba[4 * i + 3] = i < 16 ? 0 : 255; }
        int tl, tw, th;
        unsigned char* q8 = imp_tim2_from_rgba(rgba, 16, 16, &tl);
        CHECK(q8 && imp_tim2_rgba(q8, tl, &tw, &th, &back) && tw == 16 && th == 16);
        int same = 1;
        for (int i = 0; i < 256; i++) same &= !memcmp(back + 4 * i, rgba + 4 * i, 3) && (back[4 * i + 3] == 0) == (i < 16);
        CHECK(same);
        free(back); free(q8);
    }
    imp_free(&m);

    /* the registry: grouped at the first load; a second manifest (another order, one model gone, one
       new) keeps every listed entry where it was, unchanged, and appends the new one */
    {
        static const char* man[2] = {
            "item|N01_00|Nurse||1|1|1|4|2|1|1|1|\nitem|P00_00|Kevin Ryman|Costume 1|1|1|1|4|2|1|1|0|\nitem|E01_00|Zombie||1|1|1|4|2|1|0|0|\n"
            "item|EP00_00|Kevin Ryman|Cutscene, costume 1|1|1|1|4|2|1|1|0|\nitem|HE01_00|Enemy HE01_00|High detail|1|1|1|4|2|1|1|0|\n",   /* (groups not offered: not listed) */
            "item|P01_00|Mark Wilkins||1|1|1|4|2|1|1|0|\nitem|P00_00|Kevin Ryman|Costume 1|1|1|1|4|2|1|1|0|\n",
        };
        const char *kevin, *nurse;
        snprintf(g_import.dir, sizeof(g_import.dir), "%s/test_import_reg", dir);
        import_source_dir(IMPORT_OUTBREAK2, p, sizeof(p));
        SDL_CreateDirectory(p);
        strcat(p, "/manifest.txt");
        for (int r = 0; r < 2; r++) {
            f = fopen(p, "w");
            CHECK(f && fputs(man[r], f) >= 0);
            fclose(f);
            import_registry_load();
            if (r) break;
            CHECK(actor_import_n == 2 && strstr(actor_import_file[0], "/P00_00/P00_00.cvxm") && !strcmp(actor_imports[0].name, "Kevin Ryman (Costume 1)"));
            CHECK(actor_import_group[1] == IMPORT_GROUP_NPCS && actor_imports[1].anim == ANIM_FEMALE);
            kevin = actor_import_file[0]; nurse = actor_imports[1].name;
        }
        CHECK(actor_import_n == 3 && actor_import_file[0] == kevin && actor_imports[1].name == nurse && strstr(actor_import_file[2], "/P01_00/"));
        remove(p);
    }
    {   /* Umbrella Chronicles' voice lines (uc_voice.c): the ADX encoder against audio.c's decoder (a sine sweep back within
           a few percent), the speaker guesses on made-up lines */
        enum { N = 32000 };
        static short x[N];
        size_t size;
        unsigned char* a;
        double err = 0, sig = 0, cc, ca, cb;
        int h1 = 0, h2 = 0, c1, c2;
        for (int i = 0; i < N; i++) x[i] = (short)(12000 * sin(i * (0.01 + i * 1e-6)));
        CHECK((a = uc_adx_encode(x, N, 32000, &size)) && size == 0x24 + (N + 31) / 32 * 18 && a[0] == 0x80 && a[4] == 3 && a[5] == 18 &&
              !memcmp(a + 0x1E, "(c)CRI", 6) && (a[12] << 24 | a[13] << 16 | a[14] << 8 | a[15]) == N && a[18] == 3);
        ca = M_SQRT2 - cos(2.0 * M_PI * 500 / 32000); cb = M_SQRT2 - 1.0; cc = (ca - sqrt((ca + cb) * (ca - cb))) / cb;
        c1 = (int)(cc * 8192.0); c2 = (int)(cc * cc * -4096.0);
        for (int i = 0; i < N; i++) {   /* (audio.c decode_frames) */
            const unsigned char* fr = a + 0x24 + 18 * (i / 32);
            int scale = (int16_t)(fr[0] << 8 | fr[1]) + 1, nib = (fr[2 + i % 32 / 2] >> (i & 1 ? 0 : 4)) & 15, s;
            if (nib & 8) nib -= 16;
            s = nib * scale + ((c1 * h1) >> 12) + ((c2 * h2) >> 12);
            s = s > 32767 ? 32767 : s < -32768 ? -32768 : s;
            h2 = h1; h1 = s;
            err += (double)(s - x[i]) * (s - x[i]); sig += (double)x[i] * x[i];
        }
        CHECK(err < sig * 0.002);   /* (better than 27 dB) */
        free(a);
        static UcVoiceLine l[] = { { "VO_0010001", "Billy! Up there!", .f0 = 250 }, { "VO_0010002", "Right." }, { "VO_0010003", "What now?" },
            { "VO_0010004", "Are you alright, Rebecca?", .f0 = 120 }, { "VO_0010006", "Let's go." }, { "VO_0020001", "Watch your step, princess!" },
            { "VO_0020002", "Don't call me princess!" }, { "VO_0030001", "Hm.", .f0 = 130 }, { "VO_0101001", "Pathetic..." }, { "VO_1010002", "I'm Chris." },
            { "VO_1020003", "Billy's gone." }, { "VO_1030001", "Chris! Jill! Are you OK!?" }, { "VO_1111030", "Sorry Richard. I'm on it!" },
            { "VO_2001001", " " }, { "VO_2311001", "This is war." }, { "VO_civman_zombies", "Zombies!" } };
        uc_voice_guess(l, (int)(sizeof l / sizeof l[0]));
#define WHO(i, w, c, k) (!strcmp(l[i].who, w) && !strcmp(l[i].conf, c) && !strcmp(l[i].kind, k))
        CHECK(WHO(0, "Rebecca", "sure", "line") && WHO(1, "Billy", "guess", "line") && WHO(3, "Billy", "sure", "line"));   /* (calls the other; the next one answers) */
        CHECK(WHO(2, "Rebecca", "guess", "line") && WHO(8, "Wesker", "pair", "line") && WHO(9, "Chris", "sure", "line"));   /* (before a sure line;
            alone) */
        CHECK(WHO(4, "Billy", "weak", "line") && WHO(5, "Billy", "weak", "line") && WHO(6, "Rebecca", "weak", "line"));   /* (two turns on; said to
            a woman; of the speaker) */
        CHECK(WHO(7, "Billy", "weak", "line") && WHO(12, "Rebecca", "weak", "line"));   /* (the pitch nearer Billy's; the other's name) */
        CHECK(WHO(10, "Jill", "weak", "line") && WHO(11, "Jill", "weak", "line") && WHO(13, "Jill", "weak", "grunt") && !strcmp(l[13].scen, "20"));   /* (no
            cue, both named, a shout: the first speaker) */
        CHECK(WHO(14, "-", "none", "radio") && WHO(15, "-", "none", "npc"));
        {   /* the pitch: a 150 Hz tone, silence */
            static short t[32000];
            for (int i = 0; i < 32000; i++) t[i] = (short)(8000 * sin(2 * M_PI * 150 * i / 32000.0) + 3000 * sin(2 * M_PI * 300 * i / 32000.0));
            CHECK(fabsf(uc_voice_f0(t, 32000, 32000) - 150) < 5);
            memset(t, 0, sizeof t);
            CHECK(uc_voice_f0(t, 32000, 32000) == 0 && uc_voice_f0(NULL, 0, 32000) == 0);
        }
#undef WHO
    }

    {   /* Dreamcast (gdrom.c, dc.c): the CHD codecs' LZMA and deflate (streams made with Python's lzma and zlib), a GD-ROM's
           ISO 9660 through a GDI and a CUE sheet (a synthetic high-density track: IP.BIN, the volume descriptor, the root, a
           file), PVR twiddled and VQ textures */
        static const unsigned char lz[] = { 0x00, 0x21, 0x9b, 0xc8, 0x86, 0x6d, 0x7d, 0xf4, 0xc8, 0xf2, 0xeb, 0xf9, 0xea, 0xc2, 0x56, 0x38, 0x35, 0xdd,
            0xf3, 0x78, 0x69, 0x27, 0xd3, 0x84, 0x46, 0x28, 0x3d, 0x26, 0x42, 0xbf, 0x90, 0xb5, 0xfb, 0x99, 0x5a, 0x6e, 0x2d, 0x0b, 0x27, 0x47, 0x97,
            0xff, 0xfe, 0x92, 0x4c, 0x00 };
        static const unsigned char fx[] = { 0x73, 0xce, 0x4f, 0x49, 0xb5, 0x52, 0x08, 0x4b, 0x2d, 0xca, 0xcf, 0xcb, 0x4c, 0x4e, 0x54, 0xc8, 0xcf, 0x53,
            0x28, 0xc9, 0x48, 0x55, 0x70, 0x29, 0x4a, 0x4d, 0xcc, 0x4d, 0x4e, 0x2c, 0x2e, 0xd1, 0x53, 0x70, 0x1e, 0xdc, 0x0a, 0x00 };
        static const unsigned char dy[] = { 0xdd, 0xce, 0x81, 0x01, 0x44, 0x11, 0x08, 0x00, 0xd0, 0xd9, 0x0a, 0x11, 0x11, 0xfd, 0x62, 0xff, 0x4d, 0x6e,
            0x8f, 0x7b, 0x13, 0x3c, 0x68, 0x64, 0xa1, 0xef, 0xce, 0x23, 0x11, 0x72, 0x26, 0xe0, 0x79, 0xde, 0x7a, 0x1a, 0x1c, 0x60, 0x09, 0xd5, 0x15,
            0x40, 0x7d, 0xa7, 0xe2, 0xe9, 0x8d, 0x49, 0x53, 0x20, 0x07, 0x15, 0xbb, 0x96, 0xb3, 0xbd, 0x8f, 0xea, 0xa3, 0xb4, 0x36, 0xef, 0x95, 0x2b,
            0x70, 0x0b, 0xf4, 0x5a, 0x23, 0x5d, 0xee, 0xc8, 0x74, 0x3e, 0xf9, 0x0e, 0xf5, 0x2c, 0x03, 0xca, 0xe3, 0x0e, 0x84, 0x32, 0xcb, 0x60, 0xbf,
            0x7a, 0x9d, 0xa5, 0xea, 0xca, 0x2d, 0x89, 0x64, 0x2e, 0xba, 0xdb, 0x1a, 0x0f, 0x0d, 0x9f, 0x28, 0xdb, 0xa9, 0x83, 0x54, 0x49, 0x9a, 0x7d,
            0xe3, 0x2c, 0x2c, 0x51, 0x92, 0x9a, 0xb8, 0xf7, 0xc5, 0x66, 0xbc, 0x46, 0xdc, 0xe5, 0xa7, 0x54, 0xf8, 0xd3, 0xf7, 0x0f };
        unsigned char text[192], o[400], want[400];
        for (int i = 0; i < 192; i++) text[i] = (unsigned char)"Code: Veronica on the Dreamcast. "[i % 33];
        for (int i = 0; i < 400; i++) want[i] = (unsigned char)((i * i * 7 + (i >> 3)) % 23 + 65);
        CHECK(gd_lzma(lz, sizeof lz, o, 192) && !memcmp(o, text, 192));
        CHECK(!gd_lzma(lz, sizeof lz - 8, o, 192));   /* (cut short: refused) */
        CHECK(gd_inflate(fx, sizeof fx, o, 192) && !memcmp(o, text, 192));
        CHECK(gd_inflate(dy, sizeof dy, o, 400) && !memcmp(o, want, 400));
        CHECK(!gd_inflate(dy, sizeof dy, o, 399));   /* (no room: refused, not overrun) */

        static unsigned char trk[21 * 2352];
        for (int ss = 0; ss < 2; ss++) {   /* 2048-byte sectors (GDI), then raw 2352-byte ones (CUE) */
            int size = ss ? 2352 : 2048, u = ss ? 16 : 0;
            unsigned char* s;
            memset(trk, 0, sizeof trk);
            s = trk + u;                                       /* IP.BIN */
            memcpy(s, "SEGA SEGAKATANA SEGA ENTERPRISES", 32); memcpy(s + 0x30, "U       ", 8); memcpy(s + 0x40, "T1204N    ", 10);
            s = trk + 16 * size + u;                           /* the volume descriptor: the root at 45018 */
            s[0] = 1; memcpy(s + 1, "CD001", 5);
            s[156] = 34; s[156 + 2] = (unsigned char)(45018 & 255); s[156 + 3] = (unsigned char)(45018 >> 8); s[156 + 10] = 0; s[156 + 11] = 8;
            s = trk + 18 * size + u;                           /* the root: ".", "..", R0_0040.RDX;1 at 45020 (5 bytes) */
            s[0] = 34; s[33] = 0; s[34] = 34; s[34 + 33] = 1;
            s[68] = 33 + 13 + 1; s[68 + 2] = (unsigned char)(45020 & 255); s[68 + 3] = (unsigned char)(45020 >> 8); s[68 + 10] = 5; s[68 + 32] = 13;
            memcpy(s + 68 + 33, "R0_0040.RDX;1", 13);
            memcpy(trk + 20 * size + u, "hello", 5);
            s[115] = 33 + 10 + 1; s[115 + 2] = (unsigned char)(45019 & 255); s[115 + 3] = (unsigned char)(45019 >> 8); s[115 + 11] = 16; s[115 + 32] = 10;   /* SYSTEM.AFS at 45019 (4096 bytes):
                entry 1 is the 5 bytes at 2048 (the sector above) */
            memcpy(s + 115 + 33, "SYSTEM.AFS", 10);
            s = trk + 19 * size + u;
            memcpy(s, "AFS", 4); s[4] = 2; s[12] = 8; s[17] = 8; s[20] = 5;   /* (entry 0: 0, 8 bytes; entry 1: 2048, 5) */
            snprintf(p, sizeof p, "%s/dc_track03.bin", dir);
            FILE* f = fopen(p, "wb");
            CHECK(f && fwrite(trk, (size_t)size, 21, f) == 21);
            fclose(f);
            if (!ss) {
                snprintf(p, sizeof p, "%s/dc_test.gdi", dir);
                f = fopen(p, "w");
                fprintf(f, "1\n3 45000 4 2048 dc_track03.bin 0\n");
            } else {
                snprintf(p, sizeof p, "%s/dc_test.cue", dir);
                f = fopen(p, "w");
                fprintf(f, "REM HIGH-DENSITY AREA\nFILE \"dc_track03.bin\" BINARY\n  TRACK 03 MODE1/2352\n    INDEX 01 00:00:00\n");
            }
            fclose(f);
            char err[160];
            GdDisc* d = gd_open(p, err, sizeof err);
            unsigned char sec[2048];
            uint32_t n = 0;
            CHECK(d || (printf("%s\n", err), 0));
            CHECK(gd_read(d, 45000, sec) && !memcmp(sec + 0x40, "T1204N", 6) && !gd_read(d, 45021, sec) && !gd_read(d, 44999, sec));
            unsigned char* fd = gd_file(d, "r0_0040.rdx", &n);
            CHECK(fd && n == 5 && !memcmp(fd, "hello", 5));
            free(fd);
            CHECK(!gd_file(d, "R0_0041.RDX", &n));
            fd = gd_file(d, "SYSTEM.AFS/1", &n);   /* an AFS entry by number */
            CHECK(fd && n == 5 && !memcmp(fd, "hello", 5));
            free(fd);
            CHECK(!gd_file(d, "SYSTEM.AFS/2", &n) && !gd_file(d, "R0_0040.RDX/0", &n));
            gd_close(d);
        }

        {   /* PVR: RGB565 twiddled 8x8 (texel (x, y) at the twiddled index, y in the even bits), ARGB4444 VQ 8x8 */
            unsigned char t[16 + 8 * 8 * 2] = { 0 }, *px;
            int w, h;
            char why[96];
            memcpy(t, "PVRT", 4); t[4] = 8 + 128; t[8] = 1; t[9] = 1; t[12] = 8; t[14] = 8;
            for (int i = 0; i < 64; i++) { t[16 + 2 * i] = (unsigned char)i; t[16 + 2 * i + 1] = 0; }   /* (blue = the index) */
            CHECK((px = dc_pvr_rgba(t, sizeof t, &w, &h, why, sizeof why)) && w == 8 && h == 8);
            CHECK(px[4 * (0 * 8 + 1) + 2] == 2 * 255 / 31 && px[4 * (1 * 8 + 0) + 2] == 1 * 255 / 31 && px[4 * (7 * 8 + 7) + 2] == 255 && px[4 * (7 * 8 + 7) + 1] == 255 / 63 && px[3] == 255);
            free(px);
            static unsigned char v[16 + 2048 + 16];
            memset(v, 0, sizeof v);
            memcpy(v, "PVRT", 4); v[4] = (unsigned char)((8 + 2048 + 16) & 255); v[5] = (unsigned char)((8 + 2048 + 16) >> 8); v[8] = 2; v[9] = 3; v[12] = 8; v[14] = 8;
            v[16 + 8 * 1 + 2 * 3] = 0x00; v[16 + 8 * 1 + 2 * 3 + 1] = 0xF0;   /* codebook entry 1, texel 3 (x 1, y 1): opaque black */
            v[16 + 8 * 1 + 0] = 0x0F; v[16 + 8 * 1 + 1] = 0x00;                 /* its texel 0: transparent blue */
            v[16 + 2048 + 0] = 1;                                              /* block (0, 0) uses entry 1 */
            CHECK((px = dc_pvr_rgba(v, sizeof v, &w, &h, why, sizeof why)) && w == 8 && h == 8);
            CHECK(px[4 * 9 + 3] == 255 && px[4 * 9 + 2] == 0 && px[3] == 0 && px[2] == 255 && px[4 * 2 + 3] == 0);
            free(px);
            t[8] = 5;   /* (palettized: not read) */
            CHECK(!dc_pvr_rgba(t, sizeof t, &w, &h, why, sizeof why));
        }
    }
    printf("import: ok\n");
    return 0;
}
