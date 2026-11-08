/* BioRand's spoiler log (<mods>/biorand/log_pl0.txt) read into what the --dev spoiler screen shows
   (pc/glue/spoiler_menu.c, docs/BIORAND_INGAME.md "Spoiler log"): each item spot, its original item and what BioRand
   put there, the door randomizer's segments with the rooms and keys of each, the start inventory and any enemy line.
   No game data: the parser is plain C (ctest spoiler). Rooms are BioRand's codes 0xSRRV (as the log writes them,
   "A1A0": stage + 1, room in hex, variant), the game's file RM_<stage><room, decimal><variant>. */
#ifndef RECVX_SPOILER_H
#define RECVX_SPOILER_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

enum { SP_ITEMS = 640, SP_ROOMS = 256, SP_SEGS = 8, SP_NEED = 24, SP_INV = 16, SP_NAME = 28 };

typedef struct SpItem {
    unsigned short room;                /* 0xSRRV */
    unsigned char spot;                 /* the item's id in the room (the log's "A160:6") */
    unsigned char key;                  /* 1 a key item placement ("Placing key item") */
    signed char part;                   /* key items: the checkpoint part (0..), else -1 */
    short old_id, new_id;               /* item ids (the game's), -1 unknown (then the name below) */
    unsigned short old_n, new_n;        /* counts */
    char old_name[SP_NAME], new_name[SP_NAME];   /* BioRand's names ("GreenHerb"), "" unknown original */
} SpItem;

typedef struct SpRoom {
    unsigned short code;
    signed char seg;                    /* its door segment, -1 none */
    char enemy[SP_NAME];                /* "shall have ..." / "with enemy: ...", "" none */
} SpRoom;

typedef struct SpSeg {
    unsigned short from, to;            /* its start and end rooms */
    int nneed;
    short need[SP_NEED];                /* the item ids its locked doors ask for ([EmblemCard] ...), each once */
} SpSeg;

typedef struct Spoiler {
    char version[40], seed[80], player[24], scenario[8];
    int nitem, nroom, nseg, ninv, parts;   /* parts: key item checkpoints + 1 */
    int truncated;                      /* more lines than the arrays hold (the rest is left out) */
    SpItem item[SP_ITEMS];
    SpRoom room[SP_ROOMS];              /* by code */
    SpSeg seg[SP_SEGS];
    struct { short id; unsigned short n; char name[SP_NAME]; } inv[SP_INV];
} Spoiler;

/* text (n bytes, not 0-terminated) into *sp: 0 read, -1 not a BioRand log (nothing in *sp) */
int spoiler_parse(const char* text, size_t n, Spoiler* sp);
int spoiler_item_id(const char* name);          /* BioRand's item name -> id, -1 unknown */
const char* spoiler_item_name(int id);          /* id -> BioRand's name, "" unknown */
int spoiler_room_stage(unsigned short code);    /* the game's stage, room and variant of a code */
int spoiler_room_room(unsigned short code);
int spoiler_room_variant(unsigned short code);
const SpRoom* spoiler_room(const Spoiler* sp, unsigned short code);   /* NULL none */
int spoiler_has_doors(const Spoiler* sp);       /* the door randomizer's segments are listed */

#ifdef __cplusplus
}
#endif
#endif
