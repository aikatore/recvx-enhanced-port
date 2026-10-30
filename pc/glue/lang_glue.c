/* Languages at run time (docs/LANGUAGES.md, lang.h): which are installed, the settings, and what the game takes
   from the language's folder: the files (audio/adx.c asks pc_lang_file_opened what it opened), the room messages
   (room.c bhSetRoom) with the subtitles of the Subtitles language, the font (message.c bhDispFont, bhGetFontSize:
   cells, widths, a second font for subtitles in another language), the file pages (fileview.c tables; Japanese:
   pictures), the executable's texts (save / load messages, the computer's lines, a few short lines), and the
   letters of our own texts in the game font (glyphs.c). Game thread unless said. */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/effect.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_NaDraw2D.h"
#include "../src/host/lang.h"
#include "../src/host/fileio.h"
#include "../src/import/import.h"
#include "../include/pc_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void host_log(const char* fmt, ...);
extern const unsigned char FontSz[126];
extern unsigned int fstbl[24], fsheader[24];
extern char* rdx_files[205];
extern int rdx_image_data_max;
extern unsigned char SaveLoadMessage[10956];
extern void* comevt_message_tab[8];
extern const EFFECT_INFO ef_info[21];

int g_lang, g_subtitles;

typedef struct Term { int tab, idx; char* s; } Term;
typedef struct LangInfo {
    int ok, subs, nw, fpics, nterm;
    int pageend[6], npageend;        /* the playing manual's hidden pages per stage of the story (PlayPageCheck) */
    char dir[1200];
    unsigned char w[256], ox[256];   /* widths; shifts + 32 (offsets|: Japanese; 0 none) */
    unsigned int fstbl[24], fsheader[24];
    unsigned short* mes[LM_COUNT];   /* the executable's short texts (heap), NULL: the US ones */
    Term* term;                      /* the computer's lines (heap) */
} LangInfo;
static LangInfo info[LANG_COUNT];
static int font_lang;   /* LANG_* of the font the game loaded last (SYSTEM.AFS/1) */
static int mes_lang;    /* ... of the system messages (SYSMES.ALD: the file tables) */
static int room_src;    /* LF_* of the room file read last */
static unsigned char adv_lang[64];   /* LANG_* of each ADV.AFS picture read last (2 the title, 4 OPTION) */

static int parse_list(const char* s, unsigned* out, int max)
{
    int n = 0;
    while (*s && n < max) {
        char* e;
        unsigned long v = strtoul(s, &e, 10);
        if (e == s) break;
        out[n++] = (unsigned)v;
        s = *e == ',' ? e + 1 : e;
    }
    return n;
}
static int unhex(const char* s, unsigned char* out, int max)
{
    int n = 0;
    for (; s[0] && s[1] && n < max; s += 2) {
        unsigned v;
        if (sscanf(s, "%2x", &v) != 1) break;
        out[n++] = (unsigned char)v;
    }
    return n;
}

/* lang.txt's executable lines: "exemes|demo|<u16 hex>", "term|<tab>|<i>|<byte hex>" */
static void exe_line(LangInfo* x, const char* line)
{
    static const char* const names[LM_COUNT] = { "demo", "press", "pause", "retry" };
    unsigned char b[512];
    if (!strncmp(line, "exemes|", 7)) {
        const char* v = strchr(line + 7, '|');
        for (int k = 0; v && k < LM_COUNT; k++) {
            int n;
            if (strncmp(line + 7, names[k], strlen(names[k])) || line + 7 + strlen(names[k]) != v) continue;
            n = unhex(v + 1, b, (int)sizeof(b)) / 2;
            if (n < 1 || n > 200 || !(x->mes[k] = malloc(2 * n))) return;
            for (int i = 0; i < n; i++) x->mes[k][i] = (unsigned short)(b[2 * i] << 8 | b[2 * i + 1]);   /* (written as %04X) */
            if (x->mes[k][n - 1] != 0xFFFF) { free(x->mes[k]); x->mes[k] = NULL; }
        }
    } else if (!strncmp(line, "term|", 5)) {
        int tab, idx, n, k;
        if (sscanf(line + 5, "%d|%d|%n", &tab, &idx, &k) != 2 || tab < 0 || tab >= 8 || idx < 0 || idx >= 64) return;
        n = unhex(line + 5 + k, b, 200);
        Term* t = realloc(x->term, (x->nterm + 1) * sizeof(Term));
        if (!t) return;
        x->term = t;
        if (!(t[x->nterm].s = malloc(n + 1))) return;
        memcpy(t[x->nterm].s, b, n);
        t[x->nterm].s[n] = 0;
        t[x->nterm].tab = tab;
        t[x->nterm++].idx = idx;
    }
}

/* <import_dir>/<source>/<code>/lang.txt of the sources that give languages */
void lang_scan(void)
{
    LangInfo fresh[LANG_COUNT];
    memset(fresh, 0, sizeof(fresh));
    fresh[LANG_EN].ok = 1;
    for (int s = 0; s < IMPORT_SOURCES; s++) {
        if (import_sources[s].cat != IMPORT_CAT_LANGUAGES) continue;
        for (int l = 1; l < LANG_COUNT; l++) {
            LangInfo* x = &fresh[l];
            char base[1100], p[1300], line[4096];   /* (dir: base + "/xx") */
            unsigned v[256];
            int nf = 0, nh = 0;
            FILE* f;
            if (x->ok) continue;
            import_source_dir(s, base, sizeof(base));
            snprintf(x->dir, sizeof(x->dir), "%s/%.3s", base, lang_codes[l]);
            snprintf(p, sizeof(p), "%s/lang.txt", x->dir);
            if (!(f = pc_fopen(p, "r"))) continue;
            while (fgets(line, sizeof(line), f)) {
                char* c = strpbrk(line, "\r\n");
                if (c) *c = 0;
                if (!strncmp(line, "widths|", 7)) {
                    x->nw = parse_list(line + 7, v, 256);
                    for (int i = 0; i < x->nw; i++) x->w[i] = (unsigned char)(v[i] > 28 ? 28 : v[i]);
                } else if (!strncmp(line, "offsets|", 8)) {
                    int n = parse_list(line + 8, v, 256);
                    for (int i = 0; i < n; i++) x->ox[i] = (unsigned char)(v[i] < 64 ? v[i] : 32);
                } else if (!strncmp(line, "fstbl|", 6)) nf = parse_list(line + 6, x->fstbl, 24);
                else if (!strncmp(line, "fsheader|", 9)) nh = parse_list(line + 9, x->fsheader, 24);
                else if (!strncmp(line, "subtitles|", 10)) x->subs = atoi(line + 10) > 0;
                else if (!strncmp(line, "fpics|", 6)) x->fpics = atoi(line + 6);
                else if (!strncmp(line, "pageend|", 8)) { x->npageend = parse_list(line + 8, v, 6); for (int i = 0; i < x->npageend; i++) x->pageend[i] = (int)v[i]; }
                else exe_line(x, line);
            }
            fclose(f);
            x->ok = (x->nw >= 108 || l == LANG_JA) && nf == 24 && nh == 24;   /* (the Japanese font: one width, below) */
            if (!x->ok) host_log("lang: %s is incomplete: not offered\n", p);
        }
    }
    /* ponytail: the old lists' texts stay allocated (the game may still show one); a few KB per import or delete */
    memcpy(info, fresh, sizeof(info));   /* (the menus read it; the game thread takes a language at a game's start) */
    for (int l = 1; l < LANG_COUNT; l++) if (info[l].ok) host_log("lang: %s installed (%s)\n", lang_names[l], info[l].dir);
}

int lang_installed(int l) { return l >= 0 && l < LANG_COUNT && info[l].ok; }
int lang_has_subtitles(int l) { return lang_installed(l) && info[l].subs; }
int lang_sub(void)
{
    int l = g_subtitles == 1 ? g_lang : g_subtitles - 2;
    return g_subtitles && lang_has_subtitles(l) ? l : -1;
}
const char* lang_dir(int l) { return l > 0 && lang_installed(l) ? info[l].dir : NULL; }
int lang_picture(int adv) { return adv >= 0 && adv < 64 ? adv_lang[adv] : LANG_EN; }
int lang_eu_picture(int adv) { int l = lang_picture(adv); return l == LANG_FR || l == LANG_DE || l == LANG_ES; }
static int data_lang(void) { return lang_installed(g_lang) ? g_lang : LANG_EN; }

/* ---------------------------------------------------------------- the executable's texts */
static unsigned char* us_saveload;   /* the US save / load messages (heap) */
static char** us_term[8];            /* the computer's US lines (heap copies of the pointer arrays) */
static const int term_n[8] = { 27, 19, 17, 23, 20, 0, 0, 4 };

/* the language's save / load messages and computer lines into the decomp's tables (the US ones back for another) */
static void exe_texts(int l)
{
    unsigned char* d = NULL;
    long n = 0;
    if (!us_saveload && (us_saveload = malloc(sizeof(SaveLoadMessage)))) memcpy(us_saveload, SaveLoadMessage, sizeof(SaveLoadMessage));
    if (l && lang_dir(l)) {
        char p[1300];
        FILE* f;
        snprintf(p, sizeof(p), "%s/SAVELOAD.MES", lang_dir(l));
        if ((f = pc_fopen(p, "rb"))) {
            if (!fseek(f, 0, SEEK_END) && (n = ftell(f)) > 8 && n <= (long)sizeof(SaveLoadMessage) && !fseek(f, 0, SEEK_SET) && (d = malloc(n)) &&
                fread(d, 1, n, f) == (size_t)n && pc_rd32(d) == 45 && pc_rd32(d + 4) == 4 + 45 * 4) {}
            else { free(d); d = NULL; }
            fclose(f);
        }
    }
    if (us_saveload) memcpy(SaveLoadMessage, d ? d : us_saveload, d ? (size_t)n : sizeof(SaveLoadMessage));
    free(d);
    for (int t = 0; t < 8; t++) {
        char** a = (char**)comevt_message_tab[t];
        if (!term_n[t]) continue;
        if (!us_term[t] && (us_term[t] = malloc(term_n[t] * sizeof(char*)))) memcpy(us_term[t], a, term_n[t] * sizeof(char*));
        if (us_term[t]) memcpy(a, us_term[t], term_n[t] * sizeof(char*));
    }
    for (int i = 0; i < info[l].nterm; i++) {
        const Term* t = &info[l].term[i];
        if (term_n[t->tab] > t->idx) ((char**)comevt_message_tab[t->tab])[t->idx] = t->s;
    }
}

const unsigned short* pc_lang_mes(int which, const unsigned short* us)
{
    return which >= 0 && which < LM_COUNT && info[font_lang].mes[which] ? info[font_lang].mes[which] : us;
}

/* ---------------------------------------------------------------- files */
static int strcasecmp_(const char* a, const char* b)
{
    for (; *a && (*a | 32) == (*b | 32); a++, b++) {}
    return (*a | 32) - (*b | 32);
}

/* SYSMES.ALD: the file tables to its pages; SYSTEM.AFS/1: the font's language (and the executable's texts) */
void pc_lang_file_opened(const char* name, int src)
{
    static unsigned int us_fstbl[24], us_fsheader[24];
    static int saved;
    int l = src == LF_LANG ? data_lang() : LANG_EN;
    size_t n = strlen(name);
    if (n > 4 && !strcasecmp_(name + n - 4, ".RDX")) room_src = src;
    else if (!strncmp(name, "ADV.AFS/", 8) && atoi(name + 8) >= 0 && atoi(name + 8) < 64) adv_lang[atoi(name + 8)] = (unsigned char)l;
    else if (!strcasecmp_(name, "SYSTEM.AFS/1")) {
        if (font_lang != l) host_log("lang: the font of %s\n", lang_names[l]);
        font_lang = l;
        exe_texts(l);
    } else if (!strcasecmp_(name, "SYSMES.ALD")) {
        if (!saved) { memcpy(us_fstbl, fstbl, sizeof(us_fstbl)); memcpy(us_fsheader, fsheader, sizeof(us_fsheader)); saved = 1; }
        memcpy(fstbl, l ? info[l].fstbl : us_fstbl, sizeof(us_fstbl));
        memcpy(fsheader, l ? info[l].fsheader : us_fsheader, sizeof(us_fsheader));
        mes_lang = l;
    }
}

/* the language of a file the game is about to read: its folder (NULL: the disc's) */
const char* pc_lang_file_dir(void) { return lang_dir(data_lang()); }

/* ---------------------------------------------------------------- textures of our own
   A TIM2 in memory as one texture of a list of ours: drawn as a loaded texture is (Ps2TexLoad takes its TIM2 from
   pSurface), outside the texture heap, so nothing releases it (the US plates, the subtitle font, a file page). Outside
   the address window, in memory the DMA reads as host memory (memory.c ps2_dma_alloc). */
int adx_afs_locate(int ptid, int fid, uint32_t* lsn, uint32_t* size);
int cdvd_read_sectors(uint32_t lsn, uint32_t count, void* dst);
int Ps2CheckTextureAlpha(void* pp);
void pc_tex_keep_original(int texno);
extern int PatId[4];
extern unsigned int Ps2_current_texbreak;
void* ps2_dma_alloc(int slot, uint32_t len);
enum { DMA_PLATES, DMA_FONT, DMA_FPAGE, DMA_FONT2 };

static void tex_set(NJS_TEXNAME* name, NJS_TEXMEMLIST* mem, void* tim)
{
    TIM2_PICTUREHEADER_EX* t = tim;
    t->PictFormat = 0;   /* as SetPvrInfo and Ps2TextureMalloc */
    mem->tspparambuffer = Ps2CheckTextureAlpha(tim);
    mem->texinfo.texsurface.pSurface = (Uint32*)tim;
    mem->texinfo.texsurface.nWidth = t->ImageWidth;
    mem->texinfo.texsurface.nHeight = t->ImageHeight;
    name->texaddr = (Uint32)(uintptr_t)mem;
    Ps2_current_texbreak = 1;   /* (the upload cache goes by address) */
}

/* the first `max` TIM2 of a texture stream (bhSetMemPvpTexture's: 32-byte chunk heads, code and size) of n bytes */
static int tim_chunks(unsigned char* d, uint32_t n, unsigned char** tims, int max)
{
    int k = 0;
    for (uint32_t o = 0; k < max && pc_range_ok(o, 32, n);) {
        uint32_t code = pc_rd32(d + o), sz = pc_rd32(d + o + 4);
        if (code == 0xFFFFFFFFu || !pc_range_ok(o + 32ull, sz, n)) break;
        if (!memcmp(d + o, "TIM2", 4) && sz > 0x40 && !memcmp(d + o + 32, "TIM2", 4) && pc_rd32(d + o + 32 + 16) + 128ull <= sz + 128ull) tims[k++] = d + o + 32;
        o += 32 + sz;
    }
    return k;
}

/* bytes [off, off + n) of the disc file at sector lsn */
static int disc_read(uint32_t lsn, uint32_t off, void* dst, uint32_t n)
{
    unsigned char sec[2048];
    for (uint32_t k; n; dst = (char*)dst + k, off += k, n -= k) {
        k = 2048 - off % 2048 < n ? 2048 - off % 2048 : n;
        if (!cdvd_read_sectors(lsn + off / 2048, 1, sec)) return 0;
        memcpy(dst, sec + off % 2048, k);
    }
    return 1;
}

/* ---------------------------------------------------------------- the font */
/* the colours a font's textures show in the game: bhSetMemPvpTexture copies each texture's CLUT into the palette
   banks (palbuf) its PLI chunk names, and the font is drawn with its bank's colours, which a later texture of the
   file can overwrite (the Russian fan translation's: its font's own CLUT is not what the game shows). Ours are drawn
   with their own CLUT: the bank's colours go there (where ClutCopy takes them from). */
void ClutCopy(void* data, void* org_data);
void ClutCopy256(void* data, void* org_data);
static void font_colours(unsigned char* d, uint32_t n, unsigned char** tims, int nt)
{
    unsigned int bank[64 * 16] = { 0 }, pal[64];
    int npal = 0, fb[4] = { -1, -1, -1, -1 };
    for (uint32_t o = 0; pc_range_ok(o, 32, n);) {
        uint32_t code = pc_rd32(d + o), sz = pc_rd32(d + o + 4);
        unsigned char* ap = d + o + 32;
        if (code == 0xFFFFFFFFu || !pc_range_ok(o + 32ull, sz, n)) break;
        if (!memcmp(d + o, "PLI", 4) && sz >= 4) {
            npal = (int)pc_rd32(ap);
            if (npal > 64 || !pc_range_ok(4, 8ull * npal, sz)) npal = 0;
            for (int i = 0; i < npal; i++) pal[i] = pc_rd32(ap + 4 + 8 * i + 4);
        } else if (!memcmp(d + o, "TIM2", 4) && sz >= 0xD0) {
            TIM2_PICTUREHEADER_EX* t = (TIM2_PICTUREHEADER_EX*)ap;
            int ct = t->ImageType == 5 ? 256 : 16, step = ct == 256 ? 1024 : 128;
            uint64_t c = 256ull + t->ImageSize;   /* (the CLUT where the game takes it from) */
            if (!pc_range_ok(c, t->ClutSize, sz)) { o += 32 + sz; continue; }
            Ps2CheckTextureAlpha(ap);   /* (as the game, before the copy: the colours' alpha) */
            for (int i = 0; t->PictFormat >= 5 && t->PictFormat < 9 && i < npal; i++, c += step) {
                int bk = pal[i] >> 8 & 0xFF, at = (int)(pal[i] >> 16) + (bk & 0x3F) * 16;
                if (!pc_range_ok(c, step, sz)) break;
                if (bk == 255 || at + ct > 64 * 16) continue;   /* (a bank given at load: not one of the fonts') */
                if (ct == 256) ClutCopy256(bank + at, ap + c); else ClutCopy(bank + at, ap + c);
                for (int k = 0; k < nt; k++) if (ap == tims[k] && !i) fb[k] = bk & 0x3F;
            }
        }
        o += 32 + sz;
    }
    for (int k = 0; k < nt; k++) {
        TIM2_PICTUREHEADER_EX* t = (TIM2_PICTUREHEADER_EX*)tims[k];
        unsigned int* c = (unsigned int*)(tims[k] + 256 + t->ImageSize);
        if (fb[k] < 0 || t->ImageType != 4) continue;
        for (int i = 0; i < 8; i++) { c[i] = bank[fb[k] * 16 + i]; c[16 + i] = bank[fb[k] * 16 + 8 + i]; }
    }
}

/* fonts of other languages than the loaded one's: a subtitle's in another language, and our texts' right after the
   Language setting changed (the game loads that font with the next screen). The first textures of the language's
   SYSTEM.AFS/1 (as bhSetFontTexture finds them), outside the address window; two kept (the subtitles' and our
   texts' languages), each in its own DMA slot. */
typedef struct { int lang, n; NJS_TEXNAME name[4]; NJS_TEXMEMLIST mem[4]; NJS_TEXLIST list; } XFont;
static XFont xf[2] = { { -1 }, { -1 } };
static int xf_last;   /* the one used last (a third language replaces the other) */

static XFont* other_font(int l)
{
    char p[1300];
    FILE* f;
    long n = 0;
    unsigned char *b = NULL, *dp, *tims[4];
    uint32_t sz;
    int k = xf[0].lang == l ? 0 : xf[1].lang == l ? 1 : -1;
    XFont* x;
    if (k >= 0) { xf_last = k; return xf[k].n ? &xf[k] : NULL; }
    x = &xf[xf_last ^= 1];
    memset(x, 0, sizeof(*x));
    x->lang = l;
    if (!lang_dir(l)) return NULL;
    snprintf(p, sizeof(p), "%s/SYSTEM.AFS/1", lang_dir(l));
    if ((f = pc_fopen(p, "rb"))) {
        if (fseek(f, 0, SEEK_END) || (n = ftell(f)) < 64 || n > (16 << 20) || fseek(f, 0, SEEK_SET) ||
            !(b = ps2_dma_alloc(xf_last ? DMA_FONT2 : DMA_FONT, (uint32_t)n)) || fread(b, 1, n, f) != (size_t)n) n = 0;
        fclose(f);
    }
    if (n) {   /* (page aligned as the game's buffer: the stream is aligned to 32 bytes from it) */
        dp = b + 4;
        sz = pc_rd32(b);
        for (int i = 0; ef_info[i].flg != (unsigned)-1 && dp < b + n; i++)
            if (ef_info[i].flg & 1) { dp += sz; if (dp + 4 > b + n) break; sz = pc_rd32(dp); dp += 4; }
        dp = (unsigned char*)(((uintptr_t)dp + 31) & ~(uintptr_t)31);
        if (dp < b + n) x->n = tim_chunks(dp, (uint32_t)(b + n - dp), tims, l == LANG_JA ? 4 : 1);
        if (x->n) font_colours(dp, (uint32_t)(b + n - dp), tims, x->n);
    }
    if (!x->n) { host_log("lang: the font of %s cannot be read (%s)\n", lang_names[l], p); return NULL; }
    for (int i = 0; i < x->n; i++) tex_set(&x->name[i], &x->mem[i], tims[i]);
    x->list.textures = x->name;
    x->list.nbTexture = x->n;
    {
        extern NJS_TEXLIST* Ps2_current_texlist;
        NJS_TEXLIST* was = Ps2_current_texlist;
        njSetTexture(&x->list);
        for (int i = 0; i < x->n; i++) pc_tex_keep_original(i);   /* (its own letters, also with an HD pack's font) */
        if (was) njSetTexture(was);
    }
    host_log("lang: the font of %s besides the loaded one (%d textures)\n", lang_names[l], x->n);
    return x;
}

/* the font our texts are drawn in: the setting's language's (tr), also while the loaded one is still another's (the
   Language setting changed; the game loads its font with the next screen). English: the loaded one (every font has
   its letters); one that cannot be read: the loaded one (its letters, else '?') */
static int text_font(void)
{
    int l = data_lang();
    return l == font_lang || l == LANG_EN || !other_font(l) ? font_lang : l;
}
int lang_font(void) { return text_font(); }

/* a glyph's advance in language l's font */
static float width_of(int l, int code)
{
    if (l == LANG_JA) return code < LANG_JA_BASE && code < info[l].nw ? info[l].w[code] : code == 0 ? 14 : lang_cell(l, code) >= 0 ? 28 : 0;
    if (l == LANG_EN) return code < 108 ? FontSz[code] : 0;
    return code < info[l].nw ? info[l].w[code] : 0;
}
static float space_of(int l) { return l == LANG_JA ? 28 : 14; }

/* a code of another language's font (lang.h LANG_OTHER): its language, *code its own code; -1 none */
static int other_of(int* code)
{
    int l = *code >= LANG_OTHER_BASE && *code < 0xFE00 ? (*code - LANG_OTHER_BASE) >> 12 : LANG_COUNT;
    if (l >= LANG_COUNT) return -1;
    *code &= 0xFFF;
    return l;
}

float pc_font_width(unsigned short code)
{
    int c = code, l = other_of(&c);
    if (l >= 0) return c == LANG_OTHER_SPACE ? space_of(l) : width_of(l, c);
    if (!font_lang || !info[font_lang].ok) return -1;
    return width_of(font_lang, code);
}
float pc_font_space(void) { return space_of(font_lang); }
float pc_glyph_w(int code)
{
    float w = pc_font_width((unsigned short)code);
    if (w >= 0) return w;
    return code >= 0 && code < 126 ? FontSz[code] : 0;
}

/* a glyph's shift left of its place (the Japanese look-alikes: proportional) */
static float shift_of(int l, int code) { return l == LANG_JA && code < LANG_JA_BASE && code < info[l].nw && info[l].ox[code] ? info[l].ox[code] - 32.0f : 0; }

int pc_font_glyph(int code, void** texlist, float* dx)
{
    int l = other_of(&code);
    if (l >= 0) {   /* another language's letter (a subtitle's, our text's): its own font */
        XFont* x = code == LANG_OTHER_SPACE ? NULL : other_font(l);
        if (!x) return -1;
        *texlist = &x->list;
    } else l = font_lang;
    *dx = shift_of(l, code);
    return lang_cell(l, code);
}

int pc_glyph_next(const char** s)
{
    int l = text_font(), c = lang_glyph(l, s);
    return l != font_lang && c > 0 ? LANG_OTHER(l, c) : c;
}

void pc_glyphs_utf8(const unsigned short* g, char* out, int n)
{
    int k = 0;
    for (; *g != 0xFFFF && k < n - 4; g++) {
        unsigned c = *g, u;
        if (c == 0xFF01 || c == 0xFF00) u = ' ';
        else if (c >= 0xFE00) continue;
        else u = lang_char(font_lang, (int)c);
        if (u < 0x80) out[k++] = (char)u;
        else if (u < 0x800) { out[k++] = (char)(0xC0 | u >> 6); out[k++] = (char)(0x80 | (u & 0x3F)); }
        else { out[k++] = (char)(0xE0 | u >> 12); out[k++] = (char)(0x80 | (u >> 6 & 0x3F)); out[k++] = (char)(0x80 | (u & 0x3F)); }
    }
    out[k] = 0;
}

/* ---------------------------------------------------------------- the US plates (lang.h lang_us_plates)
   Resource 2 of the disc's ADV.AFS/<adv> (TIM2, the title's texture 1 or OPTION's texture 0), read past the mods
   and language folders, as a one-texture list of our own. The global index of every language's copy is the same,
   so njLoadTexture would hand back the language's. */
#define US_TIM (257 << 10)   /* the larger one: OPTION's, 1024x512 4-bit, 262528 bytes */
static unsigned char* us_tim;
static NJS_TEXMEMLIST us_mem;
static NJS_TEXNAME us_name;
static NJS_TEXLIST us_list = { &us_name, 1 };
static int us_adv = -1;   /* the picture us_tim holds (-1 none, -2 the disc's cannot be read) */

int lang_us_plates(int adv)
{
    if (lang_picture(adv) == LANG_EN || us_adv == -2) return 0;
    if (us_adv != adv) {
        TIM2_PICTUREHEADER_EX* t;
        uint32_t lsn, size, res[3], lp[2], at, n = 0;
        us_adv = -2;
        if (!us_tim && !(us_tim = ps2_dma_alloc(DMA_PLATES, US_TIM))) return 0;
        t = (TIM2_PICTUREHEADER_EX*)us_tim;
        if (!adx_afs_locate(PatId[3], adv, &lsn, &size) || !disc_read(lsn, 0, res, 12) || res[2] > size - 8 ||
            !disc_read(lsn, res[2], lp, 8) || (at = res[2] + lp[1]) < res[2] || at > size - 0xD0 ||
            !disc_read(lsn, at, us_tim, 0xD0) || memcmp(t->FileId, "TIM2", 4) ||
            (n = t->TotalSize + 128) > US_TIM || n > size - at || !disc_read(lsn, at, us_tim, n)) {
            host_log("lang: the disc's ADV.AFS/%d cannot be read (%u bytes): our words in the language's plates\n", adv, n);
            return 0;
        }
        tex_set(&us_name, &us_mem, us_tim);
        us_adv = adv;
    }
    njSetTexture(&us_list);
    njSetTextureNum(0);
    pc_tex_keep_original(0);   /* the letters as measured, also with an HD pack (texture_glue.c) */
    return 1;
}

/* ---------------------------------------------------------------- file pages as pictures (Japanese)
   ITEM1.AFS / ITEM2.AFS (sys->itm_partid) entry fpics + fsheader[file] + page: a 512 x 512 picture of the page's
   text, as the Japanese release shows them; its first line where the text's first line is (64). */
static struct { int entry, part; NJS_TEXNAME name; NJS_TEXMEMLIST mem; NJS_TEXLIST list; } fpage = { -1, -1 };

int pc_lang_file_page(float x, float y, int file, int page)
{
    const LangInfo* li = &info[mes_lang];
    int entry;
    if (!mes_lang || !li->fpics || file < 0 || file >= 24) return 0;
    if (page < 0 || page > (int)fstbl[file]) return 1;   /* (no page chosen yet) */
    entry = li->fpics + (int)fsheader[file] + page;
    if (fpage.entry != entry || fpage.part != (int)sys->itm_partid) {
        uint32_t lsn, size;
        unsigned char* tims[1];
        fpage.entry = entry;
        fpage.part = (int)sys->itm_partid;
        fpage.list.nbTexture = 0;
        if (adx_afs_locate((int)sys->itm_partid, entry, &lsn, &size) && size > 64 && size < (2u << 20)) {
            unsigned char* d = ps2_dma_alloc(DMA_FPAGE, size);
            if (d) {
                if (disc_read(lsn, 0, d, size) && tim_chunks(d, size, tims, 1) == 1) {
                    tex_set(&fpage.name, &fpage.mem, tims[0]);
                    fpage.list.textures = &fpage.name;
                    fpage.list.nbTexture = 1;
                }
            }
        }
        if (!fpage.list.nbTexture) host_log("lang: the file page picture %d cannot be read\n", entry);
    }
    if (fpage.list.nbTexture) {
        NJS_POINT2COL p2c;
        NJS_POINT2 p[4];
        NJS_COLOR col[4], uv[4];
        float y0 = y - 77.0f;   /* (the pictures' first line at 141) */
        njTextureFilterMode(0);
        njColorBlendingMode(0, 8);
        njColorBlendingMode(1, 6);
        njSetTexture(&fpage.list);
        njSetTextureNum(0);
        p2c.p = p; p2c.col = col; p2c.tex = uv; p2c.num = 1;
        p[0].x = x; p[0].y = y0; p[1].x = x + 512; p[1].y = y0; p[2].x = x + 512; p[2].y = y0 + 512; p[3].x = x; p[3].y = y0 + 512;
        for (int i = 0; i < 4; i++) col[i].color = 0xFFFFFFFF;
        uv[0].tex.u = 0; uv[0].tex.v = 0; uv[1].tex.u = 256; uv[1].tex.v = 0;
        uv[2].tex.u = 256; uv[2].tex.v = 256; uv[3].tex.u = 0; uv[3].tex.v = 256;
        njDrawPolygon2DM(&p2c, 4, -1.0f, 0x80000060);
    }
    return 1;
}

int pc_lang_page_end(int us)
{
    static const int stage[8] = { 0, 1, 2, -1, 3, -1, 4, 5 };   /* (the US values per stage: 0 1 2 4 6 7) */
    const LangInfo* x = &info[mes_lang];
    return us >= 0 && us < 8 && stage[us] >= 0 && stage[us] < x->npageend ? x->pageend[stage[us]] : us;
}

/* ---------------------------------------------------------------- room messages */
static struct { int lang; unsigned char* d; uint32_t n; } rooms_mes[2] = { { -1 }, { -1 } };   /* the game's and the subtitles' */
static unsigned char* buf[2];   /* the merged tables: the room's, and the one before (a message may still show) */
static uint32_t cap[2];
static int cur, built_sub = -2, built_room = -1;
static unsigned int *built_us, *built_out;

static const unsigned char* room_block(int l, int idx, uint32_t* size)
{
    int k = rooms_mes[0].lang == l ? 0 : rooms_mes[1].lang == l ? 1 : -1;
    if (k < 0) {   /* (the slot not of the game's language) */
        char p[1200];
        FILE* f;
        long n;
        k = rooms_mes[0].lang == data_lang() ? 1 : 0;
        free(rooms_mes[k].d);
        rooms_mes[k].d = NULL; rooms_mes[k].n = 0; rooms_mes[k].lang = l;
        snprintf(p, sizeof(p), "%s/ROOMS.MES", info[l].dir);
        if ((f = pc_fopen(p, "rb"))) {
            if (!fseek(f, 0, SEEK_END) && (n = ftell(f)) > 12 && n < (64 << 20) && !fseek(f, 0, SEEK_SET) && (rooms_mes[k].d = malloc(n))
                && fread(rooms_mes[k].d, 1, n, f) == (size_t)n && !memcmp(rooms_mes[k].d, "RMES", 4)) rooms_mes[k].n = (uint32_t)n;
            fclose(f);
        }
        if (!rooms_mes[k].n) host_log("lang: %s cannot be read\n", p);
    }
    const unsigned char* d = rooms_mes[k].d;
    uint32_t dn = rooms_mes[k].n;
    if (!dn || idx < 0 || (uint32_t)idx >= pc_rd32(d + 8) || !pc_range_ok(12 + 8ull * idx, 8, dn)) return NULL;
    uint32_t o = pc_rd32(d + 12 + 8 * idx), n = pc_rd32(d + 16 + 8 * idx);
    if (!n || !pc_range_ok(o, n, dn)) return NULL;
    *size = n;
    return d + o;
}

/* message i of a table (count first, then offsets) of `size` bytes (0: trusted, the game's own); NULL if bad */
static const unsigned short* mes_at(const unsigned char* t, uint32_t size, uint32_t i, uint32_t* len)
{
    uint32_t o = pc_rd32(t + 4 + 4 * i), k = 0;
    if (size && (o & 1 || o >= size)) return NULL;
    while ((!size || o + 2 * k + 1 < size) && pc_rd16(t + o + 2 * k) != 0xFFFF) k++;
    if (size && o + 2 * k + 1 >= size) return NULL;
    *len = k + 1;
    return (const unsigned short*)(t + o);
}
/* a message shows something: a glyph or an item name (0xFF02's operand is a wait, not a glyph) */
static int shows(const unsigned short* t)
{
    for (; *t != 0xFFFF; t++) {
        if (*t < 0xFE00 || *t == 0xFF03) return 1;
        if (*t == 0xFF02 && t[1] != 0xFFFF) t++;
    }
    return 0;
}
static const unsigned char* table_of(const unsigned char* b, uint32_t size, uint32_t* n)
{
    if (!b || size < 4 || (*n = pc_rd32(b)) > 4096 || !pc_range_ok(4, 4ull * *n, size)) return NULL;
    return b;
}

/* the room's table with the language's messages: its message where it has one; where the US message shows nothing
   (a cutscene's subtitle) the Subtitles language's, in its own font (LANG_OTHER) when that is not the loaded
   one, nothing with Subtitles Off */
static unsigned int* build(unsigned int* us, int idx)
{
    int l = data_lang(), s = lang_sub(), other;
    uint32_t size = 0, ssize = 0, n = us[0], ln = 0, sn = 0, need, o;
    const unsigned char *lb, *sb;
    built_us = us; built_out = us; built_room = idx; built_sub = s;
    lb = l ? room_block(l, idx, &size) : NULL;   /* (then its size: not in one expression) */
    lb = table_of(lb, size, &ln);
    sb = s > 0 && s != l ? room_block(s, idx, &ssize) : NULL;
    sb = table_of(sb, ssize, &sn);
    if (s > 0 && s == l) { sb = lb; ssize = size; sn = ln; }
    if ((!lb && !sb) || n > 4096) return us;
    other = sb && s != font_lang;
    if (other && !other_font(s)) sb = NULL;
    for (int pass = 0; pass < 2; pass++) {
        unsigned char* out = buf[cur];
        o = 4 + 4 * n;
        for (uint32_t i = 0; i < n; i++) {
            uint32_t ul, ll = 0;
            const unsigned short* u = mes_at((const unsigned char*)us, 0, i, &ul);
            const unsigned short* t = NULL;
            int conv = 0;
            if (shows(u)) t = lb && i < ln ? mes_at(lb, size, i, &ll) : NULL;
            else if (sb && i < sn) { t = mes_at(sb, ssize, i, &ll); conv = other; }
            if (!t || !shows(t)) { t = u; ll = ul; conv = 0; }
            if (pass) {
                unsigned short* w = (unsigned short*)(out + o);
                memcpy(out + 4 + 4 * i, &o, 4);
                memcpy(w, t, 2 * ll);
                for (uint32_t k = 0; conv && k + 1 < ll; k++) {   /* the subtitle's letters in its own font */
                    if (w[k] == 0xFF01) w[k] = LANG_OTHER(s, LANG_OTHER_SPACE);
                    else if (w[k] < 0xFE00) w[k] = (unsigned short)LANG_OTHER(s, w[k]);
                    else if (w[k] == 0xFF02 || w[k] == 0xFF03) k++;
                }
            }
            o += 2 * ll;
        }
        need = o;
        if (pass) { memcpy(out, &n, 4); break; }
        if (cap[cur] < need) {
            unsigned char* g = realloc(buf[cur], need);
            if (!g) return us;
            buf[cur] = g; cap[cur] = need;
        }
    }
    built_out = (unsigned int*)buf[cur];
    cur ^= 1;
    return built_out;
}

unsigned int* pc_lang_room_mes(unsigned int* mesp)
{
    char name[40];
    int idx;
    built_room = -1;
    if (!mesp || !rom->mes_n || room_src == LF_MODS) return mesp;   /* (a room of the mods folder keeps its own text) */
    snprintf(name, sizeof(name), "RM_%d%02d%d.RDX", sys->stg_no, sys->rom_no, sys->rcase);
    for (idx = 0; idx < rdx_image_data_max && strcmp(rdx_files[idx], name); idx++) {}
    if (idx == rdx_image_data_max || (int)mesp[0] != rom->mes_n) return mesp;
    return build(mesp, idx);
}

/* every frame: Subtitles changed in this room */
void lang_frame(void)
{
    if (built_room >= 0 && built_sub != lang_sub() && rom->mesp == built_out) rom->mesp = build(built_us, built_room);   /* (also from or to the US table) */
}
