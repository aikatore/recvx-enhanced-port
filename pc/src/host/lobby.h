/* Co-op lobby screen (pc/glue/lobby_glue.c, game thread) and its host side
   (pc/src/host/lobby_io.c, main thread): text entry from the keyboard, the clipboard, the
   memory card's save files. docs/COOP.md "Lobby". */
#ifndef RECVX_LOBBY_H
#define RECVX_LOBBY_H
#ifdef __cplusplus
extern "C" {
#endif

/* keys for a text field, besides the characters 32..126 */
enum { LK_ENTER = 256, LK_ESC, LK_BACK, LK_DEL, LK_LEFT, LK_RIGHT, LK_HOME, LK_END, LK_PGUP, LK_PGDN };

typedef struct LobbyIO {
    volatile int text_on;           /* game thread: a text field is edited (the keyboard types, it is no pad) */
    volatile int list_on;           /* game thread: a list is shown (Page Up / Down, Home, End come as keys) */
    volatile unsigned int head, tail;   /* key ring: the main thread writes at head, the game thread reads at tail */
    volatile int keys[64];
    char clip[128];                 /* game thread: text for the clipboard ... */
    volatile int clip_req;          /* ... taken by the main thread */
    volatile int paste_req;         /* game thread sets 1; the main thread puts the clipboard's text in paste and sets 2 */
    char paste[160];
} LobbyIO;
extern LobbyIO g_lobby_io;

/* main thread */
void lobby_io_event(const void* sdl_event);
void lobby_io_frame(void* window);      /* per presented frame: text input on/off, clipboard, test script */

/* The save system (ini save_system, docs/USER_GUIDE.md "Saves"): native files (default) or the
   memory card. Any thread. */
int  save_native(void);
const char* save_native_dir(void);      /* <save_dir>/saves; the first use creates it and copies the card's saves in */
/* any thread: save `slot` of the active save system, the game's SAVEFILE (the save region
   sys->version..save_end, then a byte-sum checksum). Slots are the memory card's 0..14
   (<save_dir>/card0/BASLUS-20184/SAVEDATA-nn), or native save number - 1 (a memory card slot's
   save keeps its number when copied over; sparse, up to SAVESTORE_NUMBER_MAX - 1). read: 1 = valid. */
#define LOBBY_SAVE_BYTES 0x838
typedef struct LobbySave { int slot; char file[256]; } LobbySave;
/* the saves there are, by slot (malloc'd in *out, free() it): the card's 15 slots (empty ones too),
   or the valid native save files (at most SAVESTORE_MAX) */
int  lobby_save_list(LobbySave** out);
int  lobby_read_listed(const LobbySave* s, unsigned char* out);
int  lobby_read_save(int slot, unsigned char* out);
int  lobby_write_save(int slot, const void* region, int size);   /* test aid: region + checksum; 1 = written */
/* co-op, any thread: the host's saved partner inventories that go with save `slot`
   (<save_dir>/coop-partners-nn.bin, native: <save_dir>/saves/NNN.coop-partners.bin).
   read: bytes read (0 none); write: n 0 removes the file; 1 = done */
int  lobby_read_partners(int slot, void* buf, int cap);
int  lobby_write_partners(int slot, const void* buf, int n);
void lobby_frame(void);                 /* game thread, frame hand-off: test aids */
void lobby_settings_changed(void);      /* game thread: write recvx.ini */

#ifdef __cplusplus
}
#endif
#endif
