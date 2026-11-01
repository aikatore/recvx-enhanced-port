/* ctest "lang": every translation of our own texts (src/host/tr_table.inc) shows with its language's game font
   (src/host/glyphs.c): no letter the font lacks (the Japanese font has the kana and only the kanji the game uses,
   the Russian one the Cyrillic letters, the European one its accents, the Arabic one the letters' joined forms).
   Lists the texts and letters otherwise. Arabic (in display order, lang_shape): the printf placeholders are filled
   left to right on screen, so they must come out in the English order. */
#include "../src/host/lang.h"
#include <stdio.h>
#include <string.h>

typedef struct TrRow { const char* en; const char* t[LANG_COUNT - 1]; } TrRow;
static const TrRow rows[] = {
#include "../src/host/tr_table.inc"
};

static const char* const codes[LANG_COUNT] = { "en", "fr", "de", "es", "ja", "ru", "tr", "pt-BR", "ar" };
int g_lang;                                        /* (lang_glue.c) */
int lang_installed(int l) { return l >= 0 && l < LANG_COUNT; }

/* the conversions of the printf placeholders of s, in order ("%d", "%.4s": 'd' 's'; %% not) */
static void conversions(const char* s, char* out, int n)
{
    int k = 0;
    for (; *s && k < n - 1; s++) {
        if (*s != '%') continue;
        s++;
        while (*s && strchr("-+ #0123456789.*hlLqjzt", *s)) s++;
        if (*s && strchr("diouxXeEfFgGaAcspn", *s)) out[k++] = *s;   /* (a "%" in a sentence, "100% keeps": none) */
        if (!*s) break;
    }
    out[k] = 0;
}

static int check(const char* got, const char* want, const char* what)
{
    if (!strcmp(got, want)) return 0;
    printf("%s: \"%s\", expected \"%s\"\n", what, got, want);
    return 1;
}

int main(void)
{
    static char sh[4096];
    int bad = 0;
    for (int l = 1; l < LANG_COUNT; l++)
        for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
            char miss[256], a[64], b[64];
            const char* t = rows[i].t[l - 1];
            if (!t || !*t) continue;
            lang_missing(l, t, miss, sizeof(miss));
            if (miss[0]) { printf("%s: [%s] %s\n    (%s)\n", codes[l], miss, t, rows[i].en); bad++; }
            if (l != LANG_AR) continue;
            conversions(rows[i].en, a, sizeof(a));
            conversions(lang_shape(t, sh, sizeof(sh)), b, sizeof(b));
            if (strcmp(a, b)) { printf("%s: placeholders %s, the English %s: %s\n    (%s)\n", codes[l], b, a, t, rows[i].en); bad++; }
        }
    if (bad) printf("%d translations use letters their game font lacks or other placeholders: reword them (docs/LANGUAGES.md \"Letters\")\n", bad);
    /* the shaping and the display order (glyphs.c lang_shape): joined forms, lam-alef, the line reversed, Latin and
       placeholders left to right, brackets mirrored, lines kept in order */
    bad += check(lang_shape("\xD8\xA7\xD9\x84\xD8\xB9\xD8\xB1\xD8\xA8\xD9\x8A\xD8\xA9", sh, sizeof(sh)),   /* العربية (lang.c lang_names) */
                 "\xEF\xBA\x94\xEF\xBB\xB4\xEF\xBA\x91\xEF\xBA\xAE\xEF\xBB\x8C\xEF\xBB\x9F\xEF\xBA\x8D", "lang_shape(al-arabiyya)");
    bad += check(lang_shape("\xD9\x84\xD8\xA7 F1 (%d)", sh, sizeof(sh)), "F1 (%d) \xEF\xBB\xBB", "lang_shape(la F1 (%d))");   /* لا */
    bad += check(lang_shape("\xD9\x85\xD9\x86 %d x %d\n\xD9\x84\xD8\xA7", sh, sizeof(sh)), "%d x %d \xEF\xBB\xA6\xEF\xBB\xA3\n\xEF\xBB\xBB", "lang_shape(two lines)");   /* من, لا */
    bad += check(lang_shape("English (%s) stays", sh, sizeof(sh)), "English (%s) stays", "lang_shape(no Arabic)");
    /* tr() reads the table from the executable's copy of the file (src/host/tr.c): the same rows as compiled here */
    tr_init();
    for (g_lang = 1; g_lang < LANG_COUNT; g_lang++)
        for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
            const char* t = rows[i].t[g_lang - 1];
            const char* want = t && *t ? t : rows[i].en;
            if (g_lang == LANG_AR && t && *t) want = lang_shape(t, sh, sizeof(sh));
            if (strcmp(tr(rows[i].en), want)) { printf("%s: tr(\"%s\") is \"%s\", the table says \"%s\"\n", codes[g_lang], rows[i].en, tr(rows[i].en), want); bad++; }
        }
    return bad != 0;
}
