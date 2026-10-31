/* Our own texts in the language of the setting (lang.h tr(); docs/LANGUAGES.md "Our texts"): a table keyed by the
   English text (tr_table.inc: French, German, Spanish, Japanese, Russian, Turkish, Brazilian Portuguese; UTF-8). A
   text without a row, or a language without a column (""), stays English. ImGui labels keep their "##id" (looked up without it).
   The table is not compiled: tr_table.inc's own text is in the executable outside the game's address window
   (docs/ARCHITECTURE.md "Data model and memory"; Linux: section .trtab, linked at 0x40000000; Windows: a resource,
   res/recvx.rc) and tr_init reads its rows into the heap. */
#include "lang.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

typedef struct TrRow { const char* en; const char* t[LANG_COUNT - 1]; } TrRow;
static TrRow* rows;
static size_t n_rows;                   /* set once sorted */

#ifdef _WIN32
static const char* table_text(size_t* n)
{
    HRSRC r = FindResourceA(NULL, "TR_TABLE", MAKEINTRESOURCEA(10));   /* RT_RCDATA */
    HGLOBAL g = r ? LoadResource(NULL, r) : NULL;
    *n = g ? SizeofResource(NULL, r) : 0;
    return g ? (const char*)LockResource(g) : NULL;
}
#else
__asm__(".pushsection .trtab,\"a\"\ntr_table_text:\n.incbin \"" TR_TABLE_INC "\"\ntr_table_end:\n.popsection");
extern const char tr_table_text[], tr_table_end[];
static const char* table_text(size_t* n) { *n = (size_t)(tr_table_end - tr_table_text); return tr_table_text; }
#endif

/* the rows of the text (one a line: { "English", { "French", ... } },), unescaped (\n \" \\) in place */
static size_t parse(char* p, char* end, TrRow* out)
{
    size_t n = 0;
    for (char* e; p < end; p = e + 1) {
        int k = 0;
        if (!(e = memchr(p, '\n', (size_t)(end - p)))) e = end;
        if (*p != '{') continue;   /* (the comment) */
        for (char* q = p; k < LANG_COUNT && (q = memchr(q, '"', (size_t)(e - q))); q++) {
            char* w = ++q;
            if (k) out[n].t[k - 1] = w; else out[n].en = w;
            for (; q < e && *q != '"'; q++) {
                char c = *q;
                if (c == '\\' && q + 1 < e) c = *++q == 'n' ? '\n' : *q;
                *w++ = c;
            }
            *w = 0;   /* (at most where the closing quote was) */
            k++;
        }
        if (k) n++;
    }
    return n;
}

static int cmp_row(const void* a, const void* b) { return strcmp(((const TrRow*)a)->en, ((const TrRow*)b)->en); }
void tr_init(void)   /* main thread, at start */
{
    size_t n, lines = 1;
    const char* src = table_text(&n);
    char* text;
    if (rows || !src || !(text = malloc(n + 1))) return;
    memcpy(text, src, n);
    text[n] = 0;
    for (size_t i = 0; i < n; i++) lines += text[i] == '\n';
    if (!(rows = calloc(lines, sizeof(*rows)))) { free(text); return; }
    n = parse(text, text + n, rows);
    qsort(rows, n, sizeof(rows[0]), cmp_row);
    n_rows = n;
}

/* the row of `en` (n bytes of it), or NULL */
static const TrRow* find(const char* en, size_t n)
{
    size_t lo = 0, hi = n_rows;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int c = strncmp(rows[mid].en, en, n);
        if (!c) c = rows[mid].en[n] ? 1 : 0;
        if (!c) return &rows[mid];
        if (c < 0) lo = mid + 1; else hi = mid;
    }
    return NULL;
}

/* test aid: RECVX_TR_MISSES=file collects the texts asked for that have no row (once each; with a language set) */
static void miss(const char* en)
{
    static const char* path;
    static int checked;
    static unsigned* seen;
    unsigned h = 2166136261u;
    FILE* f;
    if (!checked++ && (path = getenv("RECVX_TR_MISSES"))) seen = calloc(4096, sizeof(*seen));
    if (!path || !seen) return;
    for (const char* p = en; *p; p++) h = (h ^ (unsigned char)*p) * 16777619u;
    if (seen[h & 4095] == h) return;
    seen[h & 4095] = h;
    if ((f = fopen(path, "a"))) { fprintf(f, "%s\n", en); fclose(f); }
}

const char* tr(const char* en)
{
    static char buf[8][512];
    static unsigned k;
    const char* id;
    const TrRow* r;
    int l = lang_installed(g_lang) ? g_lang : LANG_EN;
    if (!l || !en || !*en) return en;
    id = strstr(en, "##");
    r = find(en, id ? (size_t)(id - en) : strlen(en));
    if (!r) miss(en);
    if (!r || !r->t[l - 1] || !*r->t[l - 1]) return en;
    if (!id) return r->t[l - 1];
    {   /* the translation with the label's id ("Language##set3"); a few kept, for the labels of one frame */
        char* o = buf[k++ & 7];
        size_t a = strlen(r->t[l - 1]), b = strlen(id);
        if (a + b >= sizeof(buf[0])) return en;
        memcpy(o, r->t[l - 1], a);
        memcpy(o + a, id, b + 1);
        return o;
    }
}
