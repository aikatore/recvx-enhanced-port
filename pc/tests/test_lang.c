/* ctest "lang": every translation of our own texts (src/host/tr_table.inc) shows with its language's game font
   (src/host/glyphs.c): no letter the font lacks (the Japanese font has the kana and only the kanji the game uses,
   the Russian one the Cyrillic letters, the European one its accents). Lists the texts and letters otherwise. */
#include "../src/host/lang.h"
#include <stdio.h>
#include <string.h>

typedef struct TrRow { const char* en; const char* t[LANG_COUNT - 1]; } TrRow;
static const TrRow rows[] = {
#include "../src/host/tr_table.inc"
};

static const char* const codes[LANG_COUNT] = { "en", "fr", "de", "es", "ja", "ru", "tr", "pt-BR" };

int main(void)
{
    int bad = 0;
    for (int l = 1; l < LANG_COUNT; l++)
        for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
            char miss[256];
            const char* t = rows[i].t[l - 1];
            if (!t || !*t) continue;
            lang_missing(l, t, miss, sizeof(miss));
            if (!miss[0]) continue;
            printf("%s: [%s] %s\n    (%s)\n", codes[l], miss, t, rows[i].en);
            bad++;
        }
    if (bad) printf("%d translations use letters their game font lacks: reword them (docs/LANGUAGES.md \"Letters\")\n", bad);
    return bad != 0;
}
