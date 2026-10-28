/* Languages at run time (docs/LANGUAGES.md, lang.h): which are installed, the setting, and what the game takes
   from the language's folder: the files (audio/adx.c asks pc_lang_file_opened what it opened), the room messages
   (room.c bhSetRoom), the font widths (message.c bhGetFontSize), the file pages (fileview.c fstbl / fsheader),
   and the letters of our own texts in the game font. Game thread unless said. */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
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

int g_lang, g_subtitles = 1;

typedef struct LangInfo {
    int ok, subs, nw;
    char dir[1100];
    unsigned char w[256];
    unsigned int fstbl[24], fsheader[24];
} LangInfo;
static LangInfo info[LANG_COUNT];
static int font_lang;   /* LANG_* of the font the game loaded last (SYSTEM.AFS/1) */
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
            char base[1100], p[1300], line[2048];
            unsigned v[256];
            int nf = 0, nh = 0;
            FILE* f;
            if (x->ok) continue;
            import_source_dir(s, base, sizeof(base));
            snprintf(x->dir, sizeof(x->dir), "%s/%s", base, lang_codes[l]);
            snprintf(p, sizeof(p), "%s/lang.txt", x->dir);
            if (!(f = pc_fopen(p, "r"))) continue;
            while (fgets(line, sizeof(line), f)) {
                char* c = strpbrk(line, "\r\n");
                if (c) *c = 0;
                if (!strncmp(line, "widths|", 7)) {
                    x->nw = parse_list(line + 7, v, 256);
                    for (int i = 0; i < x->nw; i++) x->w[i] = (unsigned char)(v[i] > 28 ? 28 : v[i]);
                } else if (!strncmp(line, "fstbl|", 6)) nf = parse_list(line + 6, x->fstbl, 24);
                else if (!strncmp(line, "fsheader|", 9)) nh = parse_list(line + 9, x->fsheader, 24);
                else if (!strncmp(line, "subtitles|", 10)) x->subs = atoi(line + 10) > 0;
            }
            fclose(f);
            x->ok = x->nw >= 108 && nf == 24 && nh == 24;
            if (!x->ok) host_log("lang: %s is incomplete: not offered\n", p);
        }
    }
    memcpy(info, fresh, sizeof(info));   /* (the menus read it; the game thread takes a language at a game's start) */
    for (int l = 1; l < LANG_COUNT; l++) if (info[l].ok) host_log("lang: %s installed (%s)\n", lang_names[l], info[l].dir);
}

int lang_installed(int l) { return l >= 0 && l < LANG_COUNT && info[l].ok; }
int lang_has_subtitles(int l) { return lang_installed(l) && info[l].subs; }
const char* lang_dir(int l) { return l > 0 && lang_installed(l) ? info[l].dir : NULL; }
int lang_font(void) { return font_lang; }
int lang_picture(int adv) { return adv >= 0 && adv < 64 ? adv_lang[adv] : LANG_EN; }
static int data_lang(void) { return lang_installed(g_lang) ? g_lang : LANG_EN; }

/* ---------------------------------------------------------------- files */
static int strcasecmp_(const char* a, const char* b)
{
    for (; *a && (*a | 32) == (*b | 32); a++, b++) {}
    return (*a | 32) - (*b | 32);
}

/* SYSMES.ALD: the file tables to its pages; SYSTEM.AFS/1: the font's language */
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
    } else if (!strcasecmp_(name, "SYSMES.ALD")) {
        if (!saved) { memcpy(us_fstbl, fstbl, sizeof(us_fstbl)); memcpy(us_fsheader, fsheader, sizeof(us_fsheader)); saved = 1; }
        memcpy(fstbl, l ? info[l].fstbl : us_fstbl, sizeof(us_fstbl));
        memcpy(fsheader, l ? info[l].fsheader : us_fsheader, sizeof(us_fsheader));
    }
}

/* the language of a file the game is about to read: its folder (NULL: the disc's) */
const char* pc_lang_file_dir(void) { return lang_dir(data_lang()); }

/* ---------------------------------------------------------------- the font */
float pc_font_width(unsigned short code)
{
    const LangInfo* x = &info[font_lang];
    if (!font_lang || !x->ok) return -1;
    return code < x->nw ? x->w[code] : 0;
}
float pc_glyph_w(int code)
{
    float w = pc_font_width((unsigned short)code);
    if (w >= 0) return w;
    return code >= 0 && code < 126 ? FontSz[code] : 0;
}

/* the European font's letters (codes 95..149, docs/LANGUAGES.md "Text"); the US font has none of them */
static const struct { unsigned short u, code; char plain; } accents[] = {
    { 0xC0, 95, 'A' }, { 0xC1, 96, 'A' }, { 0xC2, 97, 'A' }, { 0xC4, 98, 'A' }, { 0xC3, 0, 'A' }, { 0xC5, 0, 'A' },
    { 0xC8, 100, 'E' }, { 0xC9, 101, 'E' }, { 0xCA, 102, 'E' }, { 0xCB, 103, 'E' },
    { 0xCC, 104, 'I' }, { 0xCD, 105, 'I' }, { 0xCE, 106, 'I' }, { 0xCF, 107, 'I' },
    { 0xD2, 108, 'O' }, { 0xD3, 109, 'O' }, { 0xD4, 110, 'O' }, { 0xD6, 111, 'O' }, { 0xD5, 0, 'O' },
    { 0xD9, 112, 'U' }, { 0xDA, 113, 'U' }, { 0xDB, 114, 'U' }, { 0xDC, 115, 'U' },
    { 0xE0, 116, 'a' }, { 0xE1, 117, 'a' }, { 0xE2, 118, 'a' }, { 0xE4, 119, 'a' }, { 0xE3, 0, 'a' }, { 0xE5, 0, 'a' },
    { 0xDF, 120, 's' }, { 0xE8, 121, 'e' }, { 0xE9, 122, 'e' }, { 0xEA, 123, 'e' }, { 0xEB, 124, 'e' },
    { 0xEC, 125, 'i' }, { 0xED, 126, 'i' }, { 0xEE, 127, 'i' }, { 0xEF, 128, 'i' },
    { 0xF2, 129, 'o' }, { 0xF3, 130, 'o' }, { 0xF4, 131, 'o' }, { 0xF6, 132, 'o' }, { 0xF5, 0, 'o' },
    { 0xF9, 133, 'u' }, { 0xFA, 134, 'u' }, { 0xFB, 135, 'u' }, { 0xFC, 136, 'u' },
    { 0xC7, 137, 'C' }, { 0xE7, 138, 'c' }, { 0xD1, 139, 'N' }, { 0xF1, 140, 'n' }, { 0xAA, 141, 'a' },
    { 0xA1, 142, '!' }, { 0xBF, 143, '?' }, { 0xE6, 144, 'a' }, { 0x152, 145, 'O' }, { 0x153, 146, 'o' },
    { 0xAE, 147, 0 }, { 0x2122, 148, 0 }, { 0xB0, 149, 0 }, { 0xAB, 0, '"' }, { 0xBB, 0, '"' }, { 0x2019, 0, '\'' },
    { 0x2026, 0, '.' }, { 0x2013, 0, '-' }, { 0x2014, 0, '-' }, { 0xA0, 0, ' ' },
};

static unsigned utf8_next(const char** s)
{
    const unsigned char* p = (const unsigned char*)*s;
    unsigned c = *p++;
    int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    if (c >= 0x80 && !n) c = '?';
    else if (n) c &= 0x3F >> n;
    for (; n && (*p & 0xC0) == 0x80; n--) c = c << 6 | (*p++ & 0x3F);
    if (n) c = '?';
    *s = (const char*)p;
    return c;
}

int pc_glyph_next(const char** s)
{
    unsigned c;
    if (!**s) return -1;
    c = utf8_next(s);
    if (c == ' ') return 0;
    if (c > 32 && c < 127) return (int)c - 32;
    for (size_t i = 0; i < sizeof(accents) / sizeof(accents[0]); i++) {
        if (accents[i].u != c) continue;
        if (font_lang && accents[i].code) return accents[i].code;
        if (accents[i].plain) return accents[i].plain == ' ' ? 0 : accents[i].plain - 32;
        return c == 0xAE && !font_lang ? 95 : '?' - 32;   /* (the US font's ® is 95) */
    }
    return '?' - 32;
}

void pc_glyphs_utf8(const unsigned short* g, char* out, int n)
{
    int k = 0;
    for (; *g != 0xFFFF && k < n - 4; g++) {
        unsigned c = *g, u = '?';
        if (c == 0xFF01 || c == 0xFF00) u = ' ';
        else if (c >= 0xFE00) continue;
        else if (c < 95) u = c + 32;
        else if (!font_lang && c == 95) u = 0xAE;
        else if (font_lang) for (size_t i = 0; i < sizeof(accents) / sizeof(accents[0]); i++) if (accents[i].code == c) u = accents[i].u;
        if (u < 0x80) out[k++] = (char)u;
        else if (u < 0x800) { out[k++] = (char)(0xC0 | u >> 6); out[k++] = (char)(0x80 | (u & 0x3F)); }
        else { out[k++] = (char)(0xE0 | u >> 12); out[k++] = (char)(0x80 | (u >> 6 & 0x3F)); out[k++] = (char)(0x80 | (u & 0x3F)); }
    }
    out[k] = 0;
}

/* ---------------------------------------------------------------- room messages */
static struct { int lang; unsigned char* d; uint32_t n; } rooms_mes = { -1 };
static unsigned char* buf[2];   /* the merged tables: the room's, and the one before (a message may still show) */
static uint32_t cap[2];
static int cur, built_subs, built_room = -1;
static unsigned int *built_us, *built_out;

static const unsigned char* room_block(int l, int idx, uint32_t* size)
{
    if (rooms_mes.lang != l) {
        char p[1200];
        FILE* f;
        long n;
        free(rooms_mes.d);
        rooms_mes.d = NULL; rooms_mes.n = 0; rooms_mes.lang = l;
        snprintf(p, sizeof(p), "%s/ROOMS.MES", info[l].dir);
        if ((f = pc_fopen(p, "rb"))) {
            if (!fseek(f, 0, SEEK_END) && (n = ftell(f)) > 12 && n < (64 << 20) && !fseek(f, 0, SEEK_SET) && (rooms_mes.d = malloc(n))
                && fread(rooms_mes.d, 1, n, f) == (size_t)n && !memcmp(rooms_mes.d, "RMES", 4)) rooms_mes.n = (uint32_t)n;
            fclose(f);
        }
        if (!rooms_mes.n) host_log("lang: %s cannot be read\n", p);
    }
    if (!rooms_mes.n || idx < 0 || (uint32_t)idx >= pc_rd32(rooms_mes.d + 8) || !pc_range_ok(12 + 8ull * idx, 8, rooms_mes.n)) return NULL;
    uint32_t o = pc_rd32(rooms_mes.d + 12 + 8 * idx), n = pc_rd32(rooms_mes.d + 16 + 8 * idx);
    if (!n || !pc_range_ok(o, n, rooms_mes.n)) return NULL;
    *size = n;
    return rooms_mes.d + o;
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
static int shows(const unsigned short* t) { for (; *t != 0xFFFF; t++) if (*t < 0xFE00) return 1; return 0; }

/* the room's table with the language's messages: its message where it has one, a subtitle (the US message shows
   nothing) only with Subtitles on, else the US message */
static unsigned int* build(unsigned int* us, int idx)
{
    int l = data_lang();
    uint32_t size = 0, n = us[0], ln, need, o;
    const unsigned char* lb = l ? room_block(l, idx, &size) : NULL;
    if (!lb || n > 4096 || size < 4 || (ln = pc_rd32(lb)) > 4096 || !pc_range_ok(4, 4ull * ln, size)) return us;
    for (int pass = 0; pass < 2; pass++) {
        unsigned char* out = buf[cur];
        o = 4 + 4 * n;
        for (uint32_t i = 0; i < n; i++) {
            uint32_t ul, ll = 0;
            const unsigned short* u = mes_at((const unsigned char*)us, 0, i, &ul);
            const unsigned short* t = i < ln ? mes_at(lb, size, i, &ll) : NULL;
            if (!t || !shows(t) || (!shows(u) && !g_subtitles)) { t = u; ll = ul; }
            if (pass) { memcpy(out + 4 + 4 * i, &o, 4); memcpy(out + o, t, 2 * ll); }
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
    built_us = us; built_out = (unsigned int*)buf[cur]; built_room = idx; built_subs = g_subtitles;
    cur ^= 1;
    return built_out;
}

unsigned int* pc_lang_room_mes(unsigned int* mesp)
{
    char name[40];
    int idx;
    built_room = -1;
    if (!data_lang() || !mesp || !rom->mes_n || room_src == LF_MODS) return mesp;   /* (a room of the mods folder keeps its own text) */
    snprintf(name, sizeof(name), "RM_%d%02d%d.RDX", sys->stg_no, sys->rom_no, sys->rcase);
    for (idx = 0; idx < rdx_image_data_max && strcmp(rdx_files[idx], name); idx++) {}
    if (idx == rdx_image_data_max || (int)mesp[0] != rom->mes_n) return mesp;
    return build(mesp, idx);
}

/* every frame: Subtitles changed in this room */
void lang_frame(void)
{
    if (built_room >= 0 && built_subs != g_subtitles && rom->mesp == built_out) rom->mesp = build(built_us, built_room);
}
