/* The changelog the build carries (CHANGELOG.md, embedded as it is: Linux section .trtab, Windows resource CHANGELOG)
   for the title's WHAT'S NEW screen (pc/glue/whatsnew_menu.c, docs/MENUS.md). changelog_parse turns its Markdown into
   lines the game's font can draw: no '&' '*' '#' '<' '>' '=' '@' '[' ']' '`' (pictures or missing there), bold as
   CL_BOLD toggles, links and `code` as plain text, " > " (a menu path) as ", ". Plain C (ctest changelog). */
#ifndef RECVX_CHANGELOG_H
#define RECVX_CHANGELOG_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

enum { CL_VERSION, CL_HEAD, CL_ITEM, CL_TEXT };   /* "## 0.8.0 (date)" / "## Unreleased" (text ""), "### Fixes", "- ...", a paragraph */
#define CL_BOLD '\x01'                             /* in a line's text: bold starts / ends */

typedef struct ClLine {
    unsigned char kind, depth;          /* CL_ITEM: 0 a bullet, 1 a bullet under it */
    const char* text;                   /* in the caller's buffer */
} ClLine;

/* the lines of md (n bytes) into lines[max] (their text in buf, about n + 1 bytes: what does not fit is cut); returns their count */
int changelog_parse(const char* md, size_t n, char* buf, size_t bufn, ClLine* lines, int max);
extern char g_whatsnew_seen[32];        /* ini whatsnew_seen: the version whose WHAT'S NEW was opened ("v0.8.0"; "" none): the button's dot */
/* the embedded CHANGELOG.md (n bytes), NULL none */
const char* changelog_text(size_t* n);

#ifdef __cplusplus
}
#endif
#endif
