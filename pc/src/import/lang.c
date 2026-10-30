/* The language import (docs/LANGUAGES.md): from the player's own disc image of another release the text of its
   languages, written to <import_dir>/<source>/<code>/ under the US disc's names:
     SYSMES.ALD                      item names and system messages in the US order (European: rebuilt, the
                                     language's file pages appended; lang.txt "fsheader" points at them)
     SYSTEM.AFS/1 (/2)               the effect textures with the language's font (the computer's font)
     ADV.AFS/n MRY.AFS/n ITEM1.AFS/n ITEM2.AFS/n   the pictures with writing (entries that differ from English)
     ROOMS.MES                       every room's message table (by room index; bhSetRoom takes it)
     SAVELOAD.MES                    the executable's save / load messages (adv.c SaveLoadMessage), if they differ
     lang.txt                        written last: the language, the font's widths, the file tables, the
                                     executable's other texts
   Sources (lang_discs[]): the European disc (French, German, Spanish; its English is the US text), the US disc
   with the Russian fan translation by Brosko (told from the clean US disc by its patched font), the Japanese disc.
   The Japanese text's glyph codes are moved up by LANG_JA_BASE (lang.h): codes below it are the US font's, as the
   US engine writes them (digits, times), drawn with the Japanese font's look-alikes. */
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
extern unsigned int wallpaper[24];               /* decomp fileview.c: the files' backgrounds (the same in every release) */
extern void* comevt_message_tab[8];              /* decomp sp_evt.c: the computer events' texts */
char g_lang_us_iso[1024];                        /* the US image (lang_extract reads its SYSMES.ALD; host.c sets it) */

const char* const lang_codes[LANG_COUNT] = { "en", "fr", "de", "es", "ja", "ru" };
const char* const lang_names[LANG_COUNT] = { "English", "Fran\xC3\xA7" "ais", "Deutsch", "Espa\xC3\xB1ol",
                                             "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", "\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9" };

/* a disc's languages: its folder, the digit of its file names, its index in the executable's tables */
typedef struct LangDisc { int lang; const char* dir; int digit; int exe; } LangDisc;
enum { LS_EU, LS_RU, LS_JA };   /* the European disc; a disc laid out as the US one (one language at its root) */
typedef struct LangSource {
    int src, kind;           /* import_sources[], LS_* */
    const char* exe;         /* the executable (SYSTEM.CNF names it) */
    const char* refuse;      /* another disc */
    const char* eng_dir;     /* the European disc's English (its pictures are compared with it) */
    int eng_digit;
    int n;
    LangDisc langs[3];
} LangSource;
static const LangSource lang_discs[] = {
    { IMPORT_SLES50306, LS_EU, "SLES_503.06", "Use a European Resident Evil Code: Veronica X disc (%s).", "ENG", 1,
      3, { { LANG_FR, "FRA", 2, 1 }, { LANG_DE, "GER", 5, 4 }, { LANG_ES, "SPA", 4, 3 } } },
    { IMPORT_SLUS20184RU, LS_RU, "SLUS_201.84", "Use the US disc with the Russian fan translation by Brosko (%s).", NULL, 0,
      1, { { LANG_RU, "", 0, 0 } } },
    { IMPORT_SLPM65022, LS_JA, "SLPM_650.22", "Use a Japanese Biohazard Code: Veronica Kanzenban disc (%s).", NULL, 0,
      1, { { LANG_JA, "", 0, 0 } } },
};

#define MES_FILES 280            /* the first file page in the system messages (every release) */
#define MES_MAX 4096
#define ROOMS_MAX 512
#define STEPS (1 + 5 + 205 + 1)  /* per language: messages, archives, rooms, executable */

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

/* the Japanese glyph codes moved up by LANG_JA_BASE (in place; the operands of 0xFF02 / 0xFF03 stay) */
static void ja_codes(uint16_t* t, uint32_t len)
{
    for (uint32_t i = 0; i + 1 < len; i++) {
        if (t[i] < 0xFE00) t[i] = (uint16_t)(t[i] + LANG_JA_BASE);
        else if (t[i] == 0xFF02 || t[i] == 0xFF03) i++;
    }
}
static void ja_mes(Mes* m) { for (int i = 0; i < m->n; i++) ja_codes((uint16_t*)m->t[i], m->len[i]); }

/* SYSMES.ALD: u32 n, the item names' table (n bytes), u32, the system messages' table */
typedef struct Ald { Mes items, sys; } Ald;
static int ald_read(const unsigned char* d, uint32_t size, Ald* a)
{
    uint32_t n;
    if (size < 8 || (n = pc_rd32(d)) > size - 8 || (n & 3)) return 0;
    return mes_read(d + 4, n, &a->items) && mes_read(d + 8 + n, size - 8 - n, &a->sys);
}
static unsigned char* ald_write(const Mes* items, const Mes* sys, uint32_t* n)
{
    uint32_t ni = mes_bytes(items), ns = mes_bytes(sys);
    unsigned char* o = calloc(1, 8 + ni + ns);
    if (!o) return NULL;
    memcpy(o, &ni, 4);
    mes_write(items, o + 4);
    memcpy(o + 4 + ni, &ns, 4);
    mes_write(sys, o + 8 + ni);
    *n = 8 + ni + ns;
    return o;
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

/* the language's SYSMES.ALD (l: n bytes) in the US order (us: nus bytes), malloc'd (*n bytes); its file tables (fl, hl)
   moved to the appended pages in hl_out; NULL: a layout this does not know (import.h) */
unsigned char* lang_sysmes(const unsigned char* us, uint32_t nus, const unsigned char* l, uint32_t* n,
                           const unsigned* fl, const unsigned* hl, unsigned* hl_out)
{
    Ald *a = malloc(sizeof(Ald)), *b = malloc(sizeof(Ald));   /* (heap: the address window, docs/ARCHITECTURE.md) */
    Mes* sys = malloc(sizeof(Mes));
    unsigned char* o = NULL;
    if (a && b && sys && ald_read(us, nus, a) && ald_read(l, *n, b) && sysmes_rebuild(a, b, fl, hl, sys, hl_out)) o = ald_write(&b->items, sys, n);
    free(a); free(b); free(sys);
    return o;
}

/* the Japanese SYSMES.ALD (l, *n bytes; its codes moved up in place) in the US order: its messages up to the files,
   its load question (right after them) at the US one's place; the US English elsewhere (the files are pictures:
   lang.txt "fpics"; the US disc's later memory card block has no Japanese counterpart). NULL: unknown layout */
static unsigned char* ja_sysmes(const unsigned char* us, uint32_t nus, unsigned char* l, uint32_t* n)
{
    Ald *a = malloc(sizeof(Ald)), *b = malloc(sizeof(Ald));
    Mes* sys = malloc(sizeof(Mes));
    unsigned char* o = NULL;
    int tu = MES_FILES + fsheader[23] + fstbl[23] + 1;
    if (a && b && sys && ald_read(us, nus, a) && ald_read(l, *n, b) && a->items.n == b->items.n && b->sys.n >= MES_FILES + 2 && tu + 2 <= a->sys.n) {
        ja_mes(&b->items);
        ja_mes(&b->sys);
        *sys = a->sys;
        for (int i = 0; i < MES_FILES; i++) { sys->t[i] = b->sys.t[i]; sys->len[i] = b->sys.len[i]; }
        for (int i = 0; i < 2; i++) { sys->t[tu + i] = b->sys.t[MES_FILES + i]; sys->len[tu + i] = b->sys.len[MES_FILES + i]; }
        o = ald_write(&b->items, sys, n);
    }
    free(a); free(b); free(sys);
    return o;
}

/* ---------------------------------------------------------------- the executable */
/* `len` bytes at virtual address va (its loadable segments), NULL outside */
static const unsigned char* exe_va(const unsigned char* x, uint32_t n, uint32_t va, uint32_t len)
{
    uint32_t ph, phn;
    if (n < 0x34 || memcmp(x, "\x7F" "ELF", 4)) return NULL;
    ph = pc_rd32(x + 0x1C);
    phn = pc_rd16(x + 0x2C);
    for (uint32_t i = 0; i < phn && i < 16 && pc_range_ok(ph + 32ull * i, 32, n); i++) {
        const unsigned char* p = x + ph + 32 * i;
        uint32_t off = pc_rd32(p + 4), v = pc_rd32(p + 8), fs = pc_rd32(p + 16);
        if (pc_rd32(p) == 1 && va >= v && va - v < fs && len <= fs - (va - v) && pc_range_ok((uint64_t)off + (va - v), len, n))
            return x + off + (va - v);
    }
    return NULL;
}
/* the address an instruction pair lui / addiu at va, va + d forms (the patch moved some texts) */
static uint32_t exe_hilo(const unsigned char* x, uint32_t n, uint32_t va, uint32_t d)
{
    const unsigned char *a = exe_va(x, n, va, 4), *b = exe_va(x, n, va + d, 4);
    if (!a || !b || pc_rd32(a) >> 26 != 15 || pc_rd32(b) >> 26 != 9) return 0;
    return (pc_rd32(a) << 16) + (uint32_t)(int32_t)(int16_t)pc_rd32(b);
}

/* bhGetFontSize: "andi v1,a0,0xffff; slti v0,v1,N; ...; lui v0,hi; addiu v0,v0,lo": its glyph count N and table */
static int exe_font(const unsigned char* x, uint32_t n, unsigned* count, const unsigned char** table)
{
    for (uint32_t i = 0; i + 40 <= n; i += 4) {
        if (pc_rd32(x + i) != 0x3083FFFFu || pc_rd32(x + i + 4) >> 16 != 0x2862) continue;
        for (uint32_t k = 8; k < 36; k += 4) {
            uint32_t a = pc_rd32(x + i + k), b = pc_rd32(x + i + k + 4);
            if (a >> 16 != 0x3C02 || b >> 16 != 0x2442) continue;
            *count = pc_rd32(x + i + 4) & 0xFFFF;
            *table = *count <= 256 ? exe_va(x, n, (a << 16) + (uint32_t)(int32_t)(int16_t)b, *count) : NULL;
            return *table != NULL;
        }
    }
    return 0;
}

/* the European font's widths: right after FontSz (the English one, its code 95 changed) */
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
/* the file tables right before the backgrounds' table (wallpaper: fstbl, fsheader, wallpaper, 0x60 bytes each) */
static int exe_files_before_wallpaper(const unsigned char* x, uint32_t n, unsigned* fl, unsigned* hl)
{
    unsigned char wp[0x60];
    for (int i = 0; i < 24; i++) memcpy(wp + 4 * i, &wallpaper[i], 4);
    for (uint32_t i = 0xC0; i + 0x60 <= n; i += 4) {
        if (memcmp(x + i, wp, sizeof(wp))) continue;
        for (int f = 0; f < 24; f++) {
            fl[f] = pc_rd32(x + i - 0xC0 + 4 * f);
            hl[f] = pc_rd32(x + i - 0x60 + 4 * f);
            if (fl[f] > 15 || (f && hl[f] != hl[f - 1] + fl[f - 1] + 1) || (!f && hl[f])) return 0;
        }
        return 1;
    }
    return 0;
}

/* the save / load messages (adv.c SaveLoadMessage: 45 messages, the first right after the offsets): *len bytes */
static const unsigned char* exe_saveload(const unsigned char* x, uint32_t n, uint32_t* len)
{
    for (uint32_t i = 0; i + 4 + 45 * 4 <= n; i += 4) {
        uint32_t o, end = 0;
        int k;
        if (pc_rd32(x + i) != 45 || pc_rd32(x + i + 4) != 4 + 45 * 4) continue;
        for (k = 0; k < 45; k++) {
            o = pc_rd32(x + i + 4 + 4 * k);
            if (o < end || (o & 1) || !pc_range_ok((uint64_t)i + o, 2, n)) break;
            for (end = o; i + end + 1 < n && pc_rd16(x + i + end) != 0xFFFF && end < 0x4000; end += 2) {}
            if (end >= 0x4000 || i + end + 1 >= n) break;
            end += 2;
        }
        if (k < 45 || end > 10956) continue;   /* (the decomp's array: 10956 bytes) */
        *len = end;
        return x + i;
    }
    return NULL;
}

/* a u16 text (0xFFFF end, at most 64 codes) at va as "name|hex" (lang.txt "exemes") */
static void exe_mes_line(char* out, size_t on, const char* name, const unsigned char* x, uint32_t n, uint32_t va)
{
    const unsigned char* p = va ? exe_va(x, n, va, 2) : NULL;
    size_t k = (size_t)snprintf(out, on, "exemes|%s|", name);
    for (int i = 0; p && i < 64 && exe_va(x, n, va + 2 * i, 2); i++) {
        uint16_t c = pc_rd16(exe_va(x, n, va + 2 * i, 2));
        k += (size_t)snprintf(out + k, on > k ? on - k : 0, "%04X", c);
        if (c == 0xFFFF) return;
    }
    out[0] = 0;   /* (not found, or no end) */
}

/* ---------------------------------------------------------------- archives */
/* the entries [lo, hi] of the language's archive (fl: lng) that differ from the English one (fe: eng); ja: the
   Japanese title picture spliced into the US one (below) */
static int pictures(FILE* fe, const char* eng, FILE* fl, const char* lng, const char* dir, const char* us_name, int lo, int hi, int ja, int* written)
{
    int64_t oe, ol;
    uint32_t se, sl;
    int ne = 0, nl = 0, ok = 1;
    AfsFile *ae = imp_iso_find(fe, eng, &oe, &se) ? imp_afs_list(fe, oe, se, &ne) : NULL;
    AfsFile *al = imp_iso_find(fl, lng, &ol, &sl) ? imp_afs_list(fl, ol, sl, &nl) : NULL;
    if (!ae || !al || ne != nl) { free(ae); free(al); return 0; }
    for (int i = lo; i < nl && i <= hi && ok; i++) {
        unsigned char *a = NULL, *b = NULL;
        if (al[i].size > (32u << 20) || ae[i].size > (32u << 20)) { ok = 0; break; }
        if (al[i].size == ae[i].size) {
            a = malloc(ae[i].size + 1); b = malloc(al[i].size + 1);
            ok = a && b && imp_read_at(fe, ae[i].off, a, ae[i].size) && imp_read_at(fl, al[i].off, b, al[i].size);
            if (ok && !memcmp(a, b, al[i].size)) { free(a); free(b); continue; }
        } else {
            b = malloc(al[i].size + 1);
            ok = b && imp_read_at(fl, al[i].off, b, al[i].size);
        }
        if (ok) {
            char name[64];
            if (ja && a && !strcmp(us_name, "ADV.AFS") && i == 2 && pc_rd32(a) == 0x20 && !memcmp(a, b, 0x20) && pc_rd32(a + 4) < al[i].size) {
                /* the Japanese title: its picture (resource 0) in the US file, whose plates (resource 2) the title's
                   layout reads (the Japanese plates are English, laid out otherwise) */
                memcpy(a, b, pc_rd32(a + 4));
                memcpy(b, a, al[i].size);
            }
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
static int rooms(FILE* f, const char* dir, const char* rdx, const char* lname, int ja, int* subs)
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
        /* (lax: the Russian rooms end without PRS's end mark) */
        if ((xn = pc_prs_expand_ex(pk, a[i].size, &ex, &cap, 1)) < 0x200) continue;   /* (not a room: no messages) */
        hed = pc_rd32(ex + 16);
        if (!pc_range_ok(hed, 0xB4, (uint64_t)xn)) continue;
        mp = pc_rd32(ex + hed + 0x38);
        mn = pc_rd32(ex + hed + 0xB0);
        if (!mp || !mn || mp >= (uint32_t)xn) continue;
        if (!(m = malloc(sizeof(Mes))) || !mes_read(ex + mp, (uint32_t)xn - mp, m) || (uint32_t)m->n != mn) { free(m); continue; }
        if (ja) ja_mes(m);
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
typedef struct LangTxt {
    int lang; const char* src; const char* iso; const unsigned char* w; int nw; const unsigned* fl; const unsigned* hl; int subs;
    int fpics;               /* the files are pictures from this entry of ITEM?.AFS on (Japanese), 0 text */
    const char* extra;       /* more lines (the executable's texts), or NULL */
} LangTxt;
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
    if (t->fpics) fprintf(o, "fpics|%d\npageend|0,1,2,3,4,5\n", t->fpics);   /* (the Japanese PlayPageCheck's) */
    if (t->extra) fputs(t->extra, o);
    return ferror(o) ? -1 : 0;
}
static void folder(const LangDisc* ld, const char* outdir, char* dir, size_t dn, char* txt, size_t tn)
{
    snprintf(dir, dn, "%s/%s", outdir, lang_codes[ld->lang]);
    SDL_CreateDirectory(dir);
    snprintf(txt, tn, "%s/lang.txt", dir);
    remove(txt);   /* (the folder is complete only once lang.txt is written again) */
}

static int one_lang(FILE* f, const LangSource* s, const LangDisc* ld, const char* outdir, const char* iso,
                    const unsigned char* us, uint32_t nus, const unsigned char* exe, uint32_t nexe, const unsigned char* w, int nw)
{
    char dir[1100], path[64], txt[1200];
    const char* lname = lang_names[ld->lang];
    unsigned fl[24], hl[24], hl2[24];
    unsigned char* d;
    uint32_t n;
    int pics = 0, subs = 0, ok;
    folder(ld, outdir, dir, sizeof(dir), txt, sizeof(txt));
    snprintf(pg->phase, sizeof(pg->phase), tr("%s: messages"), lname);
    snprintf(path, sizeof(path), "%s/SYSMES%d.ALD", ld->dir, ld->digit);
    if (!exe_files(exe, nexe, ld->exe, fl, hl)) { host_log("lang: %s: the file tables are not in %s\n", lname, s->exe); return 0; }
    if (!(d = read_file(f, path, 1u << 20, &n))) { host_log("lang: %s: %s missing\n", lname, path); return 0; }
    {
        unsigned char* b = lang_sysmes(us, nus, d, &n, fl, hl, hl2);
        ok = b && put(dir, "SYSMES.ALD", b, n);
        free(b);
    }
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
            ok = pictures(f, e, f, g, dir, arch[i][1], i ? 0 : 1, i ? 1 << 30 : 1, 0, &pics);   /* (SYSTEM.AFS/0 is the system messages: SYSMES.ALD) */
            pg->done++;
        }
    }
    if (!ok) { host_log("lang: %s: the pictures could not be read or written\n", lname); return 0; }
    snprintf(path, sizeof(path), "%s/RDX_LNK%d.AFS", ld->dir, ld->digit);
    if (!rooms(f, dir, path, lname, 0, &subs)) { host_log("lang: %s: the rooms could not be read or written\n", lname); return 0; }
    LangTxt t = { ld->lang, import_sources[s->src].key, iso, w, nw, fl, hl2, subs, 0, NULL };
    if (pc_save_file(txt, "w", write_txt, &t)) { host_log("lang: cannot write %s\n", txt); return 0; }
    host_log("lang: %s installed in %s (%d pictures, %d subtitles)\n", lname, dir, pics, subs);
    return 1;
}

/* the executable's other texts of the Russian translation (lang.txt "exemes", "term"; docs/LANGUAGES.md): the
   demo, PAL start and pause lines, the game over question (pad.c, system.c, gameover.c; at the US addresses or where
   the patched code points), the computer events' lines (sp_evt.c, rewritten in place) */
static char* ru_texts(const unsigned char* x, uint32_t n)
{
    static const int tabs[][2] = { { 0, 27 }, { 1, 19 }, { 2, 17 }, { 3, 23 }, { 4, 20 }, { 7, 4 } };
    size_t cap = 64 << 10, k = 0;
    char* o = malloc(cap);
    char line[600];
    if (!o) return NULL;
    o[0] = 0;
#define ADD(s) do { size_t l_ = strlen(s); if (l_ && k + l_ + 2 < cap) { memcpy(o + k, s, l_); k += l_; o[k++] = '\n'; o[k] = 0; } } while (0)
    exe_mes_line(line, sizeof(line), "demo", x, n, 0x3557E0); ADD(line);                          /* pdm_mes */
    exe_mes_line(line, sizeof(line), "press", x, n, exe_hilo(x, n, 0x131630, 4)); ADD(line);       /* pdm_palmes (bhSetPad) */
    exe_mes_line(line, sizeof(line), "pause", x, n, exe_hilo(x, n, 0x136E80, 8)); ADD(line);       /* pause_mes (bhSysCallScreenSaver) */
    exe_mes_line(line, sizeof(line), "retry", x, n, 0x35D180); ADD(line);                         /* cnt_mes */
    const unsigned char* tab = exe_va(x, n, 0x3356E0, 32);   /* comevt_message_tab */
    for (int t = 0; tab && t < (int)(sizeof(tabs) / sizeof(tabs[0])); t++) {
        const unsigned char* arr = exe_va(x, n, pc_rd32(tab + 4 * tabs[t][0]), 4u * tabs[t][1]);
        char** us = (char**)comevt_message_tab[tabs[t][0]];
        for (int i = 0; arr && i < tabs[t][1]; i++) {
            const unsigned char* s = exe_va(x, n, pc_rd32(arr + 4 * i), 1);
            size_t l = 0, q;
            while (s && l < 200 && exe_va(x, n, pc_rd32(arr + 4 * i) + l, 1) && s[l]) l++;
            if (!s || l >= 200 || (l == strlen(us[i]) && !memcmp(s, us[i], l))) continue;
            q = (size_t)snprintf(line, sizeof(line), "term|%d|%d|", tabs[t][0], i);
            for (size_t c = 0; c < l; c++) q += (size_t)snprintf(line + q, sizeof(line) - q, "%02X", s[c]);
            ADD(line);
        }
    }
#undef ADD
    return o;
}

/* a disc laid out as the US one, with one language at its root (the Russian translation, the Japanese release):
   its files compared with the US disc's (uf) */
static int one_disc(FILE* f, FILE* uf, const LangSource* s, const char* outdir, const char* iso,
                    const unsigned char* us, uint32_t nus, const unsigned char* exe, uint32_t nexe)
{
    const LangDisc* ld = &s->langs[0];
    const char* lname = lang_names[ld->lang];
    char dir[1100], txt[1200];
    unsigned char *d, w[256];
    unsigned fl[24], hl[24], count = 0;
    const unsigned char *table = NULL, *sl;
    uint32_t n, nsl = 0;
    int pics = 0, subs = 0, nw = 0, ok, ja = s->kind == LS_JA;
    char* extra = NULL;
    folder(ld, outdir, dir, sizeof(dir), txt, sizeof(txt));
    snprintf(pg->phase, sizeof(pg->phase), tr("%s: messages"), lname);
    memcpy(fl, fstbl, sizeof(fl));
    memcpy(hl, fsheader, sizeof(hl));
    if (ja && !exe_files_before_wallpaper(exe, nexe, fl, hl)) { host_log("lang: %s: the file tables are not in %s\n", lname, s->exe); return 0; }
    if (!ja) {   /* the patched font's widths (bhGetFontSize) */
        if (!exe_font(exe, nexe, &count, &table) || count <= 126 || count > 256) { host_log("lang: %s: the font's widths are not in %s\n", lname, s->exe); return 0; }
        memcpy(w, table, count);
        nw = (int)count;
    }
    if (!(d = read_file(f, "SYSMES.ALD", 1u << 20, &n))) { host_log("lang: %s: SYSMES.ALD missing\n", lname); return 0; }
    if (ja) {
        unsigned char* b = ja_sysmes(us, nus, d, &n);
        ok = b && put(dir, "SYSMES.ALD", b, n);
        free(b);
    } else {   /* the US layout (checked) */
        Ald *a = malloc(sizeof(Ald)), *b = malloc(sizeof(Ald));
        ok = a && b && ald_read(us, nus, a) && ald_read(d, n, b) && a->items.n == b->items.n && a->sys.n == b->sys.n && put(dir, "SYSMES.ALD", d, n);
        free(a); free(b);
    }
    free(d);
    if (!ok) { host_log("lang: %s: SYSMES.ALD has an unknown layout\n", lname); return 0; }
    pg->done++;
    snprintf(pg->phase, sizeof(pg->phase), tr("%s: pictures"), lname);
    {   /* the font, the effect and computer pictures (SYSTEM.AFS from 1; 0 is the system messages), the pictures */
        static const char* const arch[] = { "SYSTEM.AFS", "ADV.AFS", "MRY.AFS", "ITEM1.AFS", "ITEM2.AFS" };
        for (int i = 0; i < 5 && ok; i++) {
            ok = pictures(uf, arch[i], f, arch[i], dir, arch[i], i ? 0 : 1, 1 << 30, ja, &pics);
            pg->done++;
        }
    }
    if (!ok) { host_log("lang: %s: the pictures could not be read or written\n", lname); return 0; }
    if (!rooms(f, dir, "RDX_LNK.AFS", lname, ja, &subs)) { host_log("lang: %s: the rooms could not be read or written\n", lname); return 0; }
    snprintf(pg->phase, sizeof(pg->phase), tr("%s: messages"), lname);
    if ((sl = exe_saveload(exe, nexe, &nsl))) {   /* the save / load messages */
        unsigned char* c = malloc(nsl);
        if (c) {
            memcpy(c, sl, nsl);
            if (ja) {
                Mes* m = malloc(sizeof(Mes));
                if (m && mes_read(c, nsl, m)) ja_mes(m);
                free(m);
            }
            if (!put(dir, "SAVELOAD.MES", c, nsl)) host_log("lang: %s: SAVELOAD.MES not written\n", lname);
            free(c);
        }
    } else host_log("lang: %s: the save messages are not in %s\n", lname, s->exe);
    if (!ja) extra = ru_texts(exe, nexe);
    pg->done++;
    LangTxt t = { ld->lang, import_sources[s->src].key, iso, w, nw, fl, hl, subs, ja ? 160 : 0, extra };
    ok = !pc_save_file(txt, "w", write_txt, &t);
    free(extra);
    if (!ok) { host_log("lang: cannot write %s\n", txt); return 0; }
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
    int nw = 0, fails = 0;
    Ald* us = NULL;   /* (heap: the address window) */
    for (size_t i = 0; i < sizeof(lang_discs) / sizeof(lang_discs[0]); i++) if (lang_discs[i].src == src) s = &lang_discs[i];
    if (!s) return -1;
    if (!(f = pc_fopen(iso, "rb"))) { snprintf(pg->phase, sizeof(pg->phase), tr("Cannot open the disc image: %s"), iso); return -1; }
    if (!imp_iso_find(f, "SYSTEM.CNF", &cnf_off, &ncnf) || !imp_read_at(f, cnf_off, cnf, ncnf < 127 ? ncnf : 127) || !strstr(cnf, s->exe)) {
        snprintf(pg->phase, sizeof(pg->phase), tr(s->refuse), import_sources[src].serial);
        fclose(f);
        return -1;
    }
    exe = read_file(f, s->exe, 32u << 20, &nexe);
    if (exe && s->kind == LS_RU) {   /* the clean US disc: its font has the US glyph count (FontSz's 108) */
        unsigned count = 0;
        const unsigned char* table;
        if (exe_font(exe, nexe, &count, &table) && count == 108) {
            snprintf(pg->phase, sizeof(pg->phase), "%s", tr("This is the US disc without the Russian translation."));
            free(exe); fclose(f);
            return -1;
        }
    }
    uf = pc_fopen(g_lang_us_iso, "rb");
    us = malloc(sizeof(Ald));
    usd = uf ? read_file(uf, "SYSMES.ALD", 1u << 20, &nus) : NULL;
    if (s->kind == LS_EU && uf) { fclose(uf); uf = NULL; }
    if (!exe || (s->kind == LS_EU && !(nw = exe_widths(exe, nexe, w))) || !usd || !us || !ald_read(usd, nus, us)) {
        snprintf(pg->phase, sizeof(pg->phase), "%s", !usd || !us ? tr("The US disc image could not be read.") : tr("Required game data not found. Check the selected disc image."));
        free(exe); free(usd); free(us); fclose(f);
        if (uf) fclose(uf);
        return -1;
    }
    pg->total = s->n * STEPS;
    for (int i = 0; i < s->n; i++) {
        int ok = s->kind == LS_EU ? one_lang(f, s, &s->langs[i], outdir, iso, usd, nus, exe, nexe, w, nw)
                                  : one_disc(f, uf, s, outdir, iso, usd, nus, exe, nexe);
        fails += !ok;
        pg->done = (i + 1) * STEPS;
    }
    free(exe); free(usd); free(us); fclose(f);
    if (uf) fclose(uf);
    snprintf(pg->phase, sizeof(pg->phase), tr("%d of %d languages imported."), s->n - fails, s->n);
    return fails;
}
