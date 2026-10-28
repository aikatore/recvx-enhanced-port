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

/* the setting (ini language, LANG_*; English when the language is not installed) and Subtitles (ini subtitles) */
extern int g_lang, g_subtitles;
void lang_scan(void);                 /* which languages are installed (at start, after an import or a delete) */
int  lang_installed(int l);           /* its folder is complete (English: always) */
int  lang_has_subtitles(int l);       /* its rooms have cutscene subtitles */
const char* lang_dir(int l);          /* its folder, NULL for English */
int  lang_font(void);                 /* the language whose font is loaded (the game font's glyphs and widths) */
int  lang_picture(int adv);           /* ... of the ADV.AFS picture read last (2: the title's menu plates, 4: OPTION's) */

/* our texts in the language of the setting (the English text when there is none); tr_init sorts the table (start) */
const char* tr(const char* en);
void tr_init(void);
/* UTF-8 text in the game font: the next glyph code of *s (advances it; -1 at the end; 0 a space). Accented
   letters use the European font's codes while it is loaded (lang_font()), else the plain letter. */
int  pc_glyph_next(const char** s);
float pc_glyph_w(int code);           /* a glyph's advance at scale 1 (bhGetFontSize's) */
/* the game's text (u16 glyph codes, 0xFFFF end) as UTF-8 for the overlay, with the loaded font's letters */
void pc_glyphs_utf8(const unsigned short* g, char* out, int n);

/* game thread hooks (decomp, TARGET_PC; audio/adx.c; libgraph.c) */
enum { LF_DISC, LF_LANG, LF_MODS, LF_BIORAND };     /* where a file the game opens comes from */
const char* pc_lang_file_dir(void);                 /* the folder of the language's files (NULL: none) */
void pc_lang_file_opened(const char* name, int src); /* a file the game opens (by name or "ARCHIVE.AFS/n"), from LF_* */
float pc_font_width(unsigned short code);           /* the language font's width, < 0: the game's own */
unsigned int* pc_lang_room_mes(unsigned int* mesp);  /* bhSetRoom: the room's message table in the language */
void lang_frame(void);                              /* every frame: Subtitles changed in a room */

#ifdef __cplusplus
}
#endif
