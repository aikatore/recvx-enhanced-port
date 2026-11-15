/* The new menu's character grid (pc/src/host/textgrid.h): its keys, the cursor's moves, what each field takes, JOIN's
   address or room code, the lobby's READY note. */
#include "../src/host/textgrid.h"
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

int main(void)
{
    int r = 0, c = 0, seen[128] = { 0 };
    /* every character a field may need is on the grid (letters both cases with SHIFT) */
    for (int i = 0; i < TG_ROWS - 1; i++)
        for (int j = 0; j < TG_COLS; j++) { seen[tg_key(i, j, 0)] = 1; seen[tg_key(i, j, 1)] = 1; }
    for (int k = 'A'; k <= 'Z'; k++) CHECK(seen[k] && seen[k + 32]);
    for (int k = '0'; k <= '9'; k++) CHECK(seen[k]);
    CHECK(seen['.'] && seen[':'] && seen['-'] && seen['_'] && seen['#']);
    CHECK(tg_key(0, 0, 0) == 'A' && tg_key(0, 0, 1) == 'a' && tg_key(2, 10, 1) == '.');
    CHECK(tg_key(4, 0, 0) == TG_SHIFT && tg_key(4, 3, 0) == TG_SPACE && tg_key(4, 7, 0) == TG_DEL && tg_key(4, 9, 0) == TG_PASTE && tg_key(4, 12, 0) == TG_OK);
    /* moves wrap; the last row's wide keys are one stop each */
    tg_move(&r, &c, 0, -1); CHECK(r == 0 && c == TG_COLS - 1);
    tg_move(&r, &c, -1, 0); CHECK(r == TG_ROWS - 1 && c == 10);   /* (onto OK's first cell) */
    tg_move(&r, &c, 0, 1); CHECK(c == 0);                         /* OK -> SHIFT (wraps) */
    tg_move(&r, &c, 0, 1); CHECK(c == 2);                         /* SHIFT -> SPACE */
    tg_move(&r, &c, 0, 1); CHECK(c == 6);
    tg_move(&r, &c, 0, 1); CHECK(c == 8);
    tg_move(&r, &c, 0, 1); CHECK(c == 10);
    tg_move(&r, &c, 0, -1); CHECK(c == 8);
    tg_move(&r, &c, 0, -1); tg_move(&r, &c, 0, -1); tg_move(&r, &c, 0, -1); CHECK(c == 0);
    tg_move(&r, &c, 0, -1); CHECK(c == 10);                       /* SHIFT -> OK (wraps) */
    tg_move(&r, &c, 1, 0); CHECK(r == 0 && c == 10);
    /* each field's characters (the old lobby's rules) */
    CHECK(tg_allowed(TGK_DIGITS, '7') && !tg_allowed(TGK_DIGITS, 'a'));
    CHECK(tg_allowed(TGK_JOIN, '#') && !tg_allowed(TGK_SERVER, '#') && tg_allowed(TGK_SERVER, ':') && !tg_allowed(TGK_JOIN, ' '));
    CHECK(tg_allowed(TGK_PW, '&') && !tg_allowed(TGK_PW, ' '));
    CHECK(tg_allowed(TGK_NAME, ' ') && tg_allowed(TGK_NAME, '?') && !tg_allowed(TGK_NAME, ':') && !tg_allowed(TGK_NAME, 0));
    /* JOIN's field: an address or a room code */
    CHECK(tg_is_address("192.168.1.5:7140#ABCD2345") && tg_is_address("host:7140") && !tg_is_address("K7PQ-2MXY") && !tg_is_address("k7pq2mxy"));
    /* the READY note */
    CHECK(mp_note_ready(MP_READY "Items saved by host") && !mp_note_ready("Items saved by host") && !mp_note_ready(NULL));
    printf("textgrid: ok\n");
    return 0;
}
