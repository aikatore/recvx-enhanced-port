/* The new menu's on-screen character grid for text fields (--new-menu; docs/MENUS.md "Text fields"): what a controller
   types with (room code, address, password, player name, co-op server, the seeds). Pure logic here (the cells, the
   cursor's moves, what each field takes, the co-op lobby's READY note), tested by pc/tests/test_textgrid.c; the box is
   drawn and driven by pc/glue/text_grid.c. The keyboard types as well while the grid is up, the mouse clicks cells. */
#ifndef RECVX_TEXTGRID_H
#define RECVX_TEXTGRID_H
#include <string.h>

enum { TG_COLS = 13, TG_ROWS = 5 };
/* the last row's keys (negative: not characters) */
enum { TG_SHIFT = -1, TG_SPACE = -2, TG_DEL = -3, TG_PASTE = -4, TG_OK = -5 };
/* what a field takes */
enum { TGK_NAME, TGK_JOIN, TGK_SERVER, TGK_PW, TGK_DIGITS };

static const char tg_chars[4][TG_COLS + 1] = { "ABCDEFGHIJKLM", "NOPQRSTUVWXYZ", "0123456789.-:", "_#!?'()+/,;%~" };   /* (signs the game's font draws as signs: its @ * = & are pictures) */
/* the last row: SHIFT 0-1, SPACE 2-5, DELETE 6-7, PASTE 8-9, OK 10-12 */
static inline int tg_special(int c) { return c < 2 ? TG_SHIFT : c < 6 ? TG_SPACE : c < 8 ? TG_DEL : c < 10 ? TG_PASTE : TG_OK; }
static inline int tg_special_first(int c) { return c < 2 ? 0 : c < 6 ? 2 : c < 8 ? 6 : c < 10 ? 8 : 10; }
static inline int tg_special_w(int c) { return c < 2 ? 2 : c < 6 ? 4 : c < 8 ? 2 : c < 10 ? 2 : 3; }

/* the key of cell (r, c): a character (lower: the letters in lower case) or TG_* */
static inline int tg_key(int r, int c, int lower)
{
    int k;
    if (r < 0 || r >= TG_ROWS || c < 0 || c >= TG_COLS) return 0;
    if (r == TG_ROWS - 1) return tg_special(c);
    k = (unsigned char)tg_chars[r][c];
    return lower && k >= 'A' && k <= 'Z' ? k + 32 : k;
}

/* the cursor moved by (dr, dc), wrapping; on the last row a key wider than a cell is one stop */
static inline void tg_move(int* r, int* c, int dr, int dc)
{
    if (dr) {
        *r = (*r + dr + TG_ROWS) % TG_ROWS;
        if (*r == TG_ROWS - 1) *c = tg_special_first(*c);
    }
    if (dc) {
        if (*r == TG_ROWS - 1) {
            int f = tg_special_first(*c);
            *c = dc > 0 ? f + tg_special_w(*c) : f - 1;
            if (*c >= TG_COLS) *c = 0;
            if (*c < 0) *c = TG_COLS - 1;
            *c = tg_special_first(*c);
        } else *c = (*c + dc + TG_COLS) % TG_COLS;
    }
}

/* the field takes character k (the lobby's rules: lobby_glue.c allowed) */
static inline int tg_allowed(int kind, int k)
{
    if (k < 32 || k > 126) return 0;
    switch (kind) {
    case TGK_DIGITS: return k >= '0' && k <= '9';
    case TGK_JOIN: case TGK_SERVER:
        return (k >= '0' && k <= '9') || (k >= 'a' && k <= 'z') || (k >= 'A' && k <= 'Z') || k == '.' || k == ':' || k == '-' || (k == '#' && kind == TGK_JOIN);
    case TGK_PW: return k > 32;
    default: return (k >= '0' && k <= '9') || (k >= 'a' && k <= 'z') || (k >= 'A' && k <= 'Z') || (k && strchr(" -_.!?'()+", k));
    }
}

/* JOIN's one field (proposal §3.3): with a dot, a colon or a '#' it is an address, else a room code (coop_glue.c start) */
static inline int tg_is_address(const char* s) { return strpbrk(s, ".:#") != NULL; }

/* the co-op lobby's READY (no protocol change): the guest's note (g_coop.note, shown to the host) starts with this */
#define MP_READY "Ready: "
static inline int mp_note_ready(const char* note) { return note && !strncmp(note, MP_READY, sizeof(MP_READY) - 1); }

#endif
