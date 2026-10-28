/* Our own texts in the language of the setting (lang.h tr(); docs/LANGUAGES.md "Our texts"): a table keyed by the
   English text. A text without a row, or a language without a column, stays English. */
#include "lang.h"
#include <SDL3/SDL.h>
#include <string.h>

const char* tr(const char* en)
{
    return en;
}
