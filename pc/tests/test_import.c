/* Imports (pc/src/import, docs/IMPORT_OUTBREAK.md): the SLD decoder, TIM2 decoding (the CSM1
   palette order), the two writers on a small synthetic humanoid (no disc data), and the "You
   appear as" registry growing append-only. */
#include "../src/import/impmodel.h"
#include "../src/import/import.h"
#include "../src/host/actors.h"
#include <SDL3/SDL.h>
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
const char* tr(const char* en) { return en; }   /* (host/tr.c) */
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
    for (int i = 0; i < n; i++) ns += t[i] ? 4 : 2;
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
        memcpy(m + o, t[i] ? s : s + 1, t[i] ? 4 : 2);
        o += t[i] ? 4 : 2;
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
        unsigned char *ub = ald(us, 311, &nus), *lb = ald(l, 311, &n), *o = lang_sysmes(ub, nus, lb, &n, fl, hl, ho);
        CHECK(o);
        CHECK(ald_mes(o, 0) == 2000 && ald_mes(o, 279) == 2279);          /* the same up to the files */
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
        m.uv[i][0] = 2.25f + 0.5f * (i & 1); m.uv[i][1] = 0.5f * (i >> 1);   /* (u past 1: moved back by whole tiles) */
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
    printf("import: ok\n");
    return 0;
}
