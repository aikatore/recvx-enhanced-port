/* The language import (docs/LANGUAGES.md): from the player's own image of the European release (SLES-50306)
   the text of French, German and Spanish, written to <import_dir>/sles50306/<code>/ under the US disc's names:
     SYSMES.ALD                      item names and system messages, rebuilt in the US order; the language's file
                                     pages appended (lang.txt "fsheader" points at them)
     SYSTEM.AFS/1                    the effect textures with the language's font
     ADV.AFS/n MRY.AFS/n ITEM1.AFS/n ITEM2.AFS/n   the pictures with writing (entries that differ from English)
     ROOMS.MES                       every room's message table (by room index; bhSetRoom takes it)
     lang.txt                        written last: the language, the font's widths and the file tables
   The disc's European English is the US text without subtitles: not written. Sources for later (Japanese,
   Russian) are rows of lang_discs[]. */
#include "import.h"
#include "../host/lang.h"
#include "../host/fileio.h"
#include "../../include/pc_parse.h"
#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void host_log(const char* fmt, ...);
extern const unsigned char FontSz[126];          /* decomp message.c: the US font's widths */
extern unsigned int fstbl[24], fsheader[24];     /* decomp fileview.c: the US files' page counts and first pages */
char g_lang_us_iso[1024];                        /* the US image (lang_extract reads its SYSMES.ALD; host.c sets it) */

const char* const lang_codes[LANG_COUNT] = { "en", "fr", "de", "es", "ja", "ru" };
const char* const lang_names[LANG_COUNT] = { "English", "Fran\xC3\xA7" "ais", "Deutsch", "Espa\xC3\xB1ol",
                                             "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", "\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9" };

/* a disc's languages: its folder, the digit of its file names, its index in the executable's tables */
typedef struct LangDisc { int lang; const char* dir; int digit; int exe; } LangDisc;
typedef struct LangSource {
    int src;                 /* import_sources[] */
    const char* exe;         /* the executable (SYSTEM.CNF names it) */
    const char* eng_dir;     /* the disc's English (the pictures are compared with it) */
    int eng_digit;
    int n;
    LangDisc langs[4];
} LangSource;
static const LangSource lang_discs[] = {
    { IMPORT_SLES50306, "SLES_503.06", "ENG", 1, 3, { { LANG_FR, "FRA", 2, 1 }, { LANG_DE, "GER", 5, 4 }, { LANG_ES, "SPA", 4, 3 } } },
};

#define MES_FILES 280            /* the first file page in the system messages (every release) */
#define MES_MAX 4096
#define ROOMS_MAX 512

static ImportProgress* const pg = &g_import_progress;

/* a file of the image, read whole (malloc'd), NULL if missing or above `max` bytes */
static unsigned char* read_file(FILE* f, const char* path, uint32_t max, uint32_t* n)
{
    int64_t off;
    unsigned char* d;
    if (!imp_iso_find(f, path, &off, n) || *n > max || !(d = malloc(*n ? *n : 1))) return NULL;
    if (!imp_read_at(f, off, d, *n)) { free(d); return NULL; }
    return d;
}

static int put(const char* dir, const char* name, const void* d, uint32_t n)
{
    char p[1300];
    FILE* o;
    snprintf(p, sizeof(p), "%s/%s", dir, name);
    if (strchr(name, '/')) {   /* (ADV.AFS/2: the archive's folder) */
        char q[1300];
        snprintf(q, sizeof(q), "%s", p);
        *strrchr(q, '/') = 0;
        SDL_CreateDirectory(q);
    }
    if (!(o = pc_fopen(p, "wb"))) return 0;
    int ok = fwrite(d, 1, n, o) == n;
    return fclose(o) == 0 && ok;
}

/* ---------------------------------------------------------------- messages
   A table: u32 count, count u32 offsets (from the table's start) of u16 texts ending in 0xFFFF. */
typedef struct Mes { const uint16_t* t[MES_MAX]; uint32_t len[MES_MAX]; int n; } Mes;   /* len in u16, with the end */

static int mes_read(const unsigned char* d, uint32_t size, Mes* m)
{
    uint32_t n;
    if (size < 4 || (n = pc_rd32(d)) > MES_MAX || !pc_range_ok(4, 4ull * n, size)) return 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t o = pc_rd32(d + 4 + 4 * i), k = 0;
        if ((o & 1) || o >= size) return 0;
        while (o + 2 * k + 1 < size && pc_rd16(d + o + 2 * k) != 0xFFFF) k++;
        if (o + 2 * k + 1 >= size) return 0;
        m->t[i] = (const uint16_t*)(d + o);
        m->len[i] = k + 1;
    }
    m->n = (int)n;
    return 1;
}
static uint32_t mes_bytes(const Mes* m)
{
    uint32_t b = 4 + 4 * m->n;
    for (int i = 0; i < m->n; i++) b += 2 * m->len[i];
    return b;
}
static void mes_write(const Mes* m, unsigned char* out)
{
    uint32_t o = 4 + 4 * m->n;
    memcpy(out, &(uint32_t){ (uint32_t)m->n }, 4);
    for (int i = 0; i < m->n; i++) {
        memcpy(out + 4 + 4 * i, &o, 4);
        memcpy(out + o, m->t[i], 2 * m->len[i]);
        o += 2 * m->len[i];
    }
}
static int visible(const uint16_t* t)   /* a glyph or an item name (0xFF02's operand is a wait) */
{
    for (; *t != 0xFFFF; t++) {
        if (*t < 0xFE00 || *t == 0xFF03) return 1;
        if (*t == 0xFF02 && t[1] != 0xFFFF) t++;
    }
    return 0;
}
static const uint16_t empty_mes[1] = { 0xFFFF };

/* SYSMES.ALD: u32 n, the item names' table (n bytes), u32, the system messages' table */
typedef struct Ald { Mes items, sys; } Ald;
static int ald_read(const unsigned char* d, uint32_t size, Ald* a)
{
    uint32_t n;
    if (size < 8 || (n = pc_rd32(d)) > size - 8 || (n & 3)) return 0;
    return mes_read(d + 4, n, &a->items) && mes_read(d + 8 + n, size - 8 - n, &a->sys);
}

/* the language's system messages in the US order (docs/LANGUAGES.md "System messages"): the same up to the
   files; the files page by page (a page the language lacks: empty); after the files the load question (2) and
   the memory card block (from the end); the language's whole file region appended for its own tables */
static int sysmes_rebuild(const Ald* us, const Ald* l, const unsigned* fl, const unsigned* hl, Mes* out, unsigned* hl_out)
{
    int nu = us->sys.n, nl = l->sys.n, tu = MES_FILES + fsheader[23] + fstbl[23] + 1, tl = MES_FILES + hl[23] + fl[23] + 1;
    int card = tu + 2, k;
    while (card < nu && !visible(us->sys.t[card])) card++;
    k = nu - card;   /* the memory card block: the US disc's last k messages */
    if (us->items.n != l->items.n || tl + 2 > nl - k || card >= nu || tu > nu || nu + tl - MES_FILES > MES_MAX) return 0;
    out->n = nu + tl - MES_FILES;
    for (int i = 0; i < out->n; i++) { out->t[i] = empty_mes; out->len[i] = 1; }
#define TAKE(o, i) (out->t[o] = l->sys.t[i], out->len[o] = l->sys.len[i])
    for (int i = 0; i < MES_FILES; i++) TAKE(i, i);
    for (int f = 0; f < 24; f++)
        for (unsigned p = 0; p <= fstbl[f] && p <= fl[f]; p++) TAKE(MES_FILES + fsheader[f] + p, MES_FILES + hl[f] + p);
    TAKE(tu, tl); TAKE(tu + 1, tl + 1);
    for (int i = 0; i < k; i++) TAKE(nu - k + i, nl - k + i);
    for (int i = MES_FILES; i < tl; i++) TAKE(nu - MES_FILES + i, i);
#undef TAKE
    for (int f = 0; f < 24; f++) hl_out[f] = nu - MES_FILES + hl[f];
    return 1;
}

static int sysmes_write(const char* dir, const Ald* l, const Mes* sys)
{
    uint32_t ni = mes_bytes(&l->items), ns = mes_bytes(sys), n = 8 + ni + ns;
    unsigned char* b = calloc(1, n);
    int ok;
    if (!b) return 0;
    memcpy(b, &ni, 4);
    mes_write(&l->items, b + 4);
    memcpy(b + 4 + ni, &ns, 4);
    mes_write(sys, b + 8 + ni);
    ok = put(dir, "SYSMES.ALD", b, n);
    free(b);
    return ok;
}

/* ---------------------------------------------------------------- the executable's tables */
/* the widths of the font with accents: right after FontSz (the English one, its code 95 changed) */
static int exe_widths(const unsigned char* x, uint32_t n, unsigned char* w)
{
    for (uint32_t i = 0; i + 128 + 162 <= n; i++) {
        if (x[i] != FontSz[0] || memcmp(x + i, FontSz, 95) || memcmp(x + i + 96, FontSz + 96, 126 - 96)) continue;
        for (int k = 0; k < 162; k++) if (x[i + 128 + k] < 6 || x[i + 128 + k] > 28) return 0;
        memcpy(w, x + i + 128, 162);
        return 162;
    }
    return 0;
}
/* the file tables of language index k (English 0): five pairs of fstbl, fsheader, 0xC0 bytes apart */
static int exe_files(const unsigned char* x, uint32_t n, int k, unsigned* fl, unsigned* hl)
{
    unsigned char us[0xC0];
    for (int i = 0; i < 24; i++) { memcpy(us + 4 * i, &fstbl[i], 4); memcpy(us + 0x60 + 4 * i, &fsheader[i], 4); }
    for (uint32_t i = 0; i + 5 * 0xC0 <= n; i += 4) {
        if (memcmp(x + i, us, sizeof(us))) continue;
        const unsigned char* t = x + i + k * 0xC0;
        for (int f = 0; f < 24; f++) {
            fl[f] = pc_rd32(t + 4 * f);
            hl[f] = pc_rd32(t + 0x60 + 4 * f);
            if (fl[f] > 15 || (f && hl[f] != hl[f - 1] + fl[f - 1] + 1) || (!f && hl[f])) return 0;
        }
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- archives */
/* the entries of the language's archive that differ from the disc's English one (only: just that entry, -1 all) */
static int pictures(FILE* f, const char* dir, const char* us_name, const char* eng, const char* lng, int only, int* written)
{
    int64_t oe, ol;
    uint32_t se, sl;
    int ne = 0, nl = 0, ok = 1;
    AfsFile *ae = imp_iso_find(f, eng, &oe, &se) ? imp_afs_list(f, oe, se, &ne) : NULL;
    AfsFile *al = imp_iso_find(f, lng, &ol, &sl) ? imp_afs_list(f, ol, sl, &nl) : NULL;
    if (!ae || !al || ne != nl) { free(ae); free(al); return 0; }
    for (int i = 0; i < nl && ok; i++) {
        unsigned char *a = NULL, *b = NULL;
        if (only >= 0 && i != only) continue;
        if (al[i].size > (32u << 20) || ae[i].size > (32u << 20)) { ok = 0; break; }
        if (al[i].size == ae[i].size) {
            a = malloc(ae[i].size + 1); b = malloc(al[i].size + 1);
            ok = a && b && imp_read_at(f, ae[i].off, a, ae[i].size) && imp_read_at(f, al[i].off, b, al[i].size);
            if (ok && !memcmp(a, b, al[i].size)) { free(a); free(b); continue; }
        } else {
            b = malloc(al[i].size + 1);
            ok = b && imp_read_at(f, al[i].off, b, al[i].size);
        }
        if (ok) {
            char name[64];
            snprintf(name, sizeof(name), "%s/%d", us_name, i);
            ok = put(dir, name, b, al[i].size);
            (*written)++;
        }
        free(a); free(b);
    }
    free(ae); free(al);
    return ok;
}

/* ROOMS.MES: "RMES", u32 1, u32 n; n x (u32 offset, u32 size) by room index (0 size: none); the tables */
static int rooms(FILE* f, const char* dir, const char* rdx, const char* lname, int* subs)
{
    int64_t off;
    uint32_t size;
    int n = 0, ok = 1;
    AfsFile* a = imp_iso_find(f, rdx, &off, &size) ? imp_afs_list(f, off, size, &n) : NULL;
    unsigned char *pk = NULL, *ex = NULL, *outb = NULL;
    long cap = 0;
    uint32_t used, room = 0;
    if (!a || n <= 0 || n > ROOMS_MAX || !(ex = malloc(cap = 16 << 20))) { free(a); return 0; }
    used = 12 + 8 * n;
    outb = calloc(1, used);
    memcpy(outb, "RMES\1\0\0\0", 8);
    memcpy(outb + 8, &n, 4);
    for (int i = 0; i < n && ok && outb; i++) {
        uint32_t hed, mp, mn, end;
        long xn;
        Mes* m;
        snprintf(pg->phase, sizeof(pg->phase), tr("%s: room messages"), lname);
        pg->done++;
        if (!a[i].size) continue;
        if (a[i].size > (64u << 20) || !(pk = realloc(pk, a[i].size)) || !imp_read_at(f, a[i].off, pk, a[i].size)) { ok = 0; break; }
        if ((xn = pc_prs_expand(pk, a[i].size, &ex, &cap)) < 0x200) continue;   /* (not a room: no messages) */
        hed = pc_rd32(ex + 16);
        if (!pc_range_ok(hed, 0xB4, (uint64_t)xn)) continue;
        mp = pc_rd32(ex + hed + 0x38);
        mn = pc_rd32(ex + hed + 0xB0);
        if (!mp || !mn || mp >= (uint32_t)xn) continue;
        if (!(m = malloc(sizeof(Mes))) || !mes_read(ex + mp, (uint32_t)xn - mp, m) || (uint32_t)m->n != mn) { free(m); continue; }
        for (int k = 0; k < m->n; k++) *subs += m->t[k][0] == 0xFE00 && visible(m->t[k]);
        end = mes_bytes(m);
        unsigned char* g = realloc(outb, used + end + 3);
        if (!g) { free(m); ok = 0; break; }
        outb = g;
        mes_write(m, outb + used);
        memcpy(outb + 12 + 8 * i, &used, 4);
        memcpy(outb + 16 + 8 * i, &end, 4);
        used = (used + end + 3) & ~3u;
        room++;
        free(m);
    }
    ok = ok && outb && room && put(dir, "ROOMS.MES", outb, used);
    host_log("lang: %s: messages of %u rooms\n", lname, room);
    free(a); free(pk); free(ex); free(outb);
    return ok;
}

/* ---------------------------------------------------------------- the import */
typedef struct LangTxt { int lang; const char* src; const char* iso; const unsigned char* w; int nw; const unsigned* fl; const unsigned* hl; int subs; } LangTxt;
static int write_txt(FILE* o, void* ud)
{
    const LangTxt* t = ud;
    char when[64];
    time_t now = time(NULL);
    strftime(when, sizeof(when), "%Y-%m-%d %H:%M", localtime(&now));
    fprintf(o, "# recvx language 1\nlang|%s\nsource|%s\niso|%s\nwhen|%s\nsubtitles|%d\nwidths|", lang_codes[t->lang], t->src, t->iso, when, t->subs);
    for (int i = 0; i < t->nw; i++) fprintf(o, "%s%d", i ? "," : "", t->w[i]);
    fprintf(o, "\nfstbl|");
    for (int i = 0; i < 24; i++) fprintf(o, "%s%u", i ? "," : "", t->fl[i]);
    fprintf(o, "\nfsheader|");
    for (int i = 0; i < 24; i++) fprintf(o, "%s%u", i ? "," : "", t->hl[i]);
    fprintf(o, "\n");
    return ferror(o) ? -1 : 0;
}

static int one_lang(FILE* f, const LangSource* s, const LangDisc* ld, const char* outdir, const char* iso,
                    const Ald* us, const unsigned char* exe, uint32_t nexe, const unsigned char* w, int nw)
{
    static Mes sysout;
    static Ald l;
    char dir[1100], path[64], txt[1200];
    const char* lname = lang_names[ld->lang];
    unsigned fl[24], hl[24], hl2[24];
    unsigned char* d;
    uint32_t n;
    int pics = 0, subs = 0, ok;
    snprintf(dir, sizeof(dir), "%s/%s", outdir, lang_codes[ld->lang]);
    SDL_CreateDirectory(dir);
    snprintf(txt, sizeof(txt), "%s/lang.txt", dir);
    remove(txt);   /* (the folder is complete only once lang.txt is written again) */
    snprintf(pg->phase, sizeof(pg->phase), tr("%s: messages"), lname);
    snprintf(path, sizeof(path), "%s/SYSMES%d.ALD", ld->dir, ld->digit);
    if (!exe_files(exe, nexe, ld->exe, fl, hl)) { host_log("lang: %s: the file tables are not in %s\n", lname, s->exe); return 0; }
    if (!(d = read_file(f, path, 1u << 20, &n))) { host_log("lang: %s: %s missing\n", lname, path); return 0; }
    ok = ald_read(d, n, &l) && sysmes_rebuild(us, &l, fl, hl, &sysout, hl2) && sysmes_write(dir, &l, &sysout);
    free(d);
    if (!ok) { host_log("lang: %s: %s has an unknown layout\n", lname, path); return 0; }
    pg->done++;
    snprintf(pg->phase, sizeof(pg->phase), tr("%s: pictures"), lname);
    {   /* the font and the effect pictures (SYSTEM?.AFS/1), then the archives' pictures with writing */
        static const char* const arch[][2] = { { "SYSTEM", "SYSTEM.AFS" }, { "ADV", "ADV.AFS" }, { "MRY", "MRY.AFS" }, { "ITEM1", "ITEM1.AFS" }, { "ITEM2", "ITEM2.AFS" } };
        for (int i = 0; i < 5 && ok; i++) {
            char e[64], g[64];
            snprintf(e, sizeof(e), "%s/%s%d.AFS", s->eng_dir, arch[i][0], s->eng_digit);
            snprintf(g, sizeof(g), "%s/%s%d.AFS", ld->dir, arch[i][0], ld->digit);
            ok = pictures(f, dir, arch[i][1], e, g, i ? -1 : 1, &pics);   /* (SYSTEM.AFS/0 is the system messages: SYSMES.ALD) */
            pg->done++;
        }
    }
    if (!ok) { host_log("lang: %s: the pictures could not be read or written\n", lname); return 0; }
    snprintf(path, sizeof(path), "%s/RDX_LNK%d.AFS", ld->dir, ld->digit);
    if (!rooms(f, dir, path, lname, &subs)) { host_log("lang: %s: the rooms could not be read or written\n", lname); return 0; }
    LangTxt t = { ld->lang, import_sources[s->src].key, iso, w, nw, fl, hl2, subs };
    if (pc_save_file(txt, "w", write_txt, &t)) { host_log("lang: cannot write %s\n", txt); return 0; }
    host_log("lang: %s installed in %s (%d pictures, %d subtitles)\n", lname, dir, pics, subs);
    return 1;
}

int lang_extract(int src, const char* iso, const char* outdir)
{
    const LangSource* s = NULL;
    FILE *f, *uf;
    char cnf[128] = { 0 };
    unsigned char *exe = NULL, *usd = NULL, w[162];
    uint32_t nexe = 0, nus = 0, ncnf;
    int64_t cnf_off;
    int nw, fails = 0;
    static Ald us;
    for (size_t i = 0; i < sizeof(lang_discs) / sizeof(lang_discs[0]); i++) if (lang_discs[i].src == src) s = &lang_discs[i];
    if (!s) return -1;
    if (!(f = pc_fopen(iso, "rb"))) { snprintf(pg->phase, sizeof(pg->phase), tr("Cannot open the disc image: %s"), iso); return -1; }
    if (!imp_iso_find(f, "SYSTEM.CNF", &cnf_off, &ncnf) || !imp_read_at(f, cnf_off, cnf, ncnf < 127 ? ncnf : 127) || !strstr(cnf, s->exe)) {
        snprintf(pg->phase, sizeof(pg->phase), tr("Use a European Resident Evil Code: Veronica X disc (%s)."), import_sources[src].serial);
        fclose(f);
        return -1;
    }
    exe = read_file(f, s->exe, 32u << 20, &nexe);
    uf = pc_fopen(g_lang_us_iso, "rb");
    usd = uf ? read_file(uf, "SYSMES.ALD", 1u << 20, &nus) : NULL;
    if (uf) fclose(uf);
    if (!exe || !(nw = exe_widths(exe, nexe, w)) || !usd || !ald_read(usd, nus, &us)) {
        snprintf(pg->phase, sizeof(pg->phase), "%s", !usd || !us.sys.n ? tr("The US disc image could not be read.") : tr("Required game data not found. Check the selected disc image."));
        free(exe); free(usd); fclose(f);
        return -1;
    }
    pg->total = s->n * (1 + 5 + 205);
    for (int i = 0; i < s->n; i++) {
        if (!one_lang(f, s, &s->langs[i], outdir, iso, &us, exe, nexe, w, nw)) fails++;
        pg->done = (i + 1) * (1 + 5 + 205);
    }
    free(exe); free(usd); fclose(f);
    snprintf(pg->phase, sizeof(pg->phase), tr("%d of %d languages imported."), s->n - fails, s->n);
    return fails;
}
