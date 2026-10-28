/* Our own texts in the language of the setting (lang.h tr(); docs/LANGUAGES.md "Our texts"): a table keyed by the
   English text (tr_table.inc: French, German, Spanish, Japanese, Russian; UTF-8). A text without a row, or a
   language without a column (""), stays English. ImGui labels keep their "##id" (looked up without it). */
#include "lang.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct TrRow { const char* en; const char* t[LANG_COUNT - 1]; } TrRow;
static TrRow rows[] = {
#include "tr_table.inc"
};
#define N_ROWS (sizeof(rows) / sizeof(rows[0]))

static int cmp_row(const void* a, const void* b) { return strcmp(((const TrRow*)a)->en, ((const TrRow*)b)->en); }
static int sorted;
void tr_init(void) { if (!sorted) { qsort(rows, N_ROWS, sizeof(rows[0]), cmp_row); sorted = 1; } }   /* main thread, at start */

/* the row of `en` (n bytes of it), or NULL */
static const TrRow* find(const char* en, size_t n)
{
    size_t lo = 0, hi = sorted ? N_ROWS : 0;
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
