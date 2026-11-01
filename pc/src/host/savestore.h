/* Native saves (docs/ARCHITECTURE.md "Save system"): one file per save in a folder,
   NNN_<label>.sav = a 64-byte header + the game's save exactly as the memory card holds it
   (SAVEDATA-nn, the SAVEFILE struct: sys->version..save_end and its byte-sum checksum).
   NNN is the save's number (3 digits, more when needed, up to SAVESTORE_NUMBER_MAX); the label (character and
   room) is for people only and never parsed. Any thread; writes go through pc_save_file and
   are serialized by a lock. */
#ifndef RECVX_SAVESTORE_H
#define RECVX_SAVESTORE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define SAVE_DATA_BYTES 0x838     /* sizeof(SAVEFILE), a card save file */
#define SAVE_HEADER_BYTES 64
#define SAVE_CONFIG_BYTES 0x34    /* sizeof(CONFIGFILE), the card's system file BASLUS-20184 (system.dat here) */
#define SAVESTORE_MAX 10000           /* saves listed at most (more are logged and left out) */
#define SAVESTORE_NUMBER_MAX 999999999 /* the highest save number (9 digits) */
#define SAVE_PARTNERS_FMT "%03d.coop-partners.bin"   /* co-op: the host's saved partner inventories of save NNN */

typedef struct SaveEntry {
    int number;
    char file[256];       /* its name in the folder */
    int64_t written;      /* seconds since 1970 */
    int play_time;        /* SAVEFILE.time */
    int save_ct, gm_mode, ply_id, stg_no, rom_no;
} SaveEntry;

/* the valid saves in dir, sorted by number (malloc'd array in *out, free() it, NULL if none); invalid
   files are skipped; at most SAVESTORE_MAX */
int  savestore_list(const char* dir, SaveEntry** out);
/* 1: path is a valid save; its card bytes in data (SAVE_DATA_BYTES) */
int  savestore_read(const char* path, unsigned char* data);
/* writes card bytes as save `number` (0: a new one, the next free number); `replaces`: the file
   name of the save this one overwrites, removed once the new file is in place if its name
   differs. Returns the number written, 0 on error (nothing changed). */
int  savestore_write(const char* dir, int number, const unsigned char* data, const char* replaces);
/* the number a new save gets (names only): the highest + 1, past SAVESTORE_NUMBER_MAX the lowest free one (0: none) */
int  savestore_next(const char* dir);
/* 1 if a file of save `number` exists (any label); its name in file (n bytes) if not NULL */
int  savestore_find(const char* dir, int number, char* file, int n);
/* "NNN_label.sav" with the label made safe for any file system (UTF-8 kept, at most 64 bytes) */
void savestore_filename(char* out, int n, int number, const char* label);
/* "Claire_Prison" (character_room, as the save menu names them) of card bytes */
void savestore_label(char* out, int n, const unsigned char* data);
/* card save bytes are valid and not an empty slot */
int  savestore_card_valid(const unsigned char* data);
/* copies the memory cards' saves (<save_dir>/card0/BASLUS-20184/SAVEDATA-nn, card 2's card1/...
   as slot nn + 15) into dir as native saves (number slot + 1 if free, else the next free one; saves
   already there with the same bytes are skipped) with their co-op partners files
   (<save_dir>/coop-partners-<slot>.bin), and card 1's system file as dir/system.dat if dir has none. The card files are kept. Returns the saves copied. */
int  savestore_import_card(const char* save_dir, const char* dir);

#ifdef __cplusplus
}
#endif
#endif
