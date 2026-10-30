/* Languages (docs/LANGUAGES.md): English is the US disc's; the others are imported from the player's own disc
   of another release (pc/src/import/lang.c, IMPORT > LANGUAGES) into <import_dir>/<source>/<code>/. The game
   reads that folder's files instead of the disc's (audio/adx.c), takes its room messages (room.c bhSetRoom),
   its glyph widths (message.c bhGetFontSize) and its file pages (fileview.c tables). Our own texts: tr(). */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

enum { LANG_EN, LANG_FR, LANG_DE, LANG_ES, LANG_JA, LANG_RU, LANG_COUNT };
extern const char* const lang_codes[LANG_COUNT];   /* en fr de es ja ru (folder names, ini language) */
extern const char* const lang_names[LANG_COUNT];   /* in the language itself (UTF-8) */

/* Glyph codes. The Japanese font's cell c is code LANG_JA_BASE + c (the importer moves the Japanese text up);
   the codes below are the US font's, drawn with the Japanese font's look-alikes (digits, times: the US engine
   writes them). A subtitle in another language than the game's (Subtitles) is LANG_SUB_BASE + its code, drawn
   with that language's font; LANG_SUB_BASE + LANG_SUB_SPACE is its space (0xFF01). */
#define LANG_JA_BASE 108
#define LANG_SUB_BASE 0x8000
#define LANG_SUB_SPACE 0x7DFF

/* the setting (ini language, LANG_*; English when the language is not installed) and Subtitles (ini subtitles:
   0 Off, 1 the game language's (older ini files), 2 + LANG_* that language's) */
extern int g_lang, g_subtitles;
void lang_scan(void);                 /* which languages are installed (at start, after an import or a delete) */
int  lang_installed(int l);           /* its folder is complete (English: always) */
int  lang_has_subtitles(int l);       /* its rooms have cutscene subtitles */
int  lang_sub(void);                  /* the subtitles' language (installed, with subtitles), -1 none */
const char* lang_dir(int l);          /* its folder, NULL for English */
int  lang_font(void);                 /* the language whose font is loaded (the game font's glyphs and widths) */
int  lang_picture(int adv);           /* ... of the ADV.AFS picture read last (2: the title's menu plates, 4: OPTION's) */
/* our words (MULTIPLAYER, ENHANCED SETTINGS, YES, ...) are the US plates' letters in every language: with another
   language's picture adv loaded, its US plates (the title's texture 1, OPTION's texture 0) become the current
   texture list, as texture 0, and 1 is returned (then AdvEasySetTextureList(0) again); 0: the screen's list stays */
int  lang_us_plates(int adv);

/* our texts in the language of the setting (the English text when there is none); tr_init sorts the table (start) */
const char* tr(const char* en);
void tr_init(void);
/* UTF-8 text in the game font: the next glyph code of *s (advances it; -1 at the end; 0 a space). Letters use the
   loaded font's codes (lang_font(): the European accents, Cyrillic, kana and kanji), else the plain letter or '?'. */
int  pc_glyph_next(const char** s);
float pc_glyph_w(int code);           /* a glyph's advance at scale 1 (bhGetFontSize's) */
/* language l's font (glyphs.c, pure): the next glyph code of the UTF-8 *s (as pc_glyph_next); the cell a code is
   drawn with (-1 none); the character of a code; the letters missing for the UTF-8 text s, as UTF-8 into out */
int  lang_glyph(int l, const char** s);
int  lang_cell(int l, int code);
unsigned lang_char(int l, int code);
void lang_missing(int l, const char* s, char* out, int n);
/* the game's text (u16 glyph codes, 0xFFFF end) as UTF-8 for the overlay, with the loaded font's letters */
void pc_glyphs_utf8(const unsigned short* g, char* out, int n);

/* game thread hooks (decomp, TARGET_PC; audio/adx.c; libgraph.c) */
enum { LF_DISC, LF_LANG, LF_MODS, LF_BIORAND };     /* where a file the game opens comes from */
const char* pc_lang_file_dir(void);                 /* the folder of the language's files (NULL: none) */
void pc_lang_file_opened(const char* name, int src); /* a file the game opens (by name or "ARCHIVE.AFS/n"), from LF_* */
float pc_font_width(unsigned short code);           /* the language font's width, < 0: the game's own */
float pc_font_space(void);                          /* the advance of a space (0xFF01) at scale 1: 14, Japanese 28 */
int  pc_font_glyph(int code, void** texlist, float* dx);   /* bhDispFont: the cell to draw (-1 none); *texlist another font's; *dx left shift */
const unsigned short* pc_lang_mes(int which, const unsigned short* us);   /* the executable's short texts (LM_*) */
enum { LM_DEMO, LM_PRESS, LM_PAUSE, LM_RETRY, LM_COUNT };
int  pc_lang_file_page(float x, float y, int file, int page);   /* fileview.c: a file page drawn as the language's picture */
int  pc_lang_page_end(int us);                      /* fileview.c PlayPageCheck: the manual's hidden pages (US 0 1 2 4 6 7) */
int  lang_eu_picture(int adv);                      /* the ADV.AFS picture is the European one (its title layout) */
unsigned int* pc_lang_room_mes(unsigned int* mesp);  /* bhSetRoom: the room's message table in the language */
void lang_frame(void);                              /* every frame: Subtitles changed in a room */

#ifdef __cplusplus
}
#endif
