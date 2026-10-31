/* Weapon shortcuts (pc/src/input/weapons.c, the game side in pc/glue/weapon_glue.c; docs/ARCHITECTURE.md
   "Input"): eight slots per character holding a weapon's item id, so a bag sorted anew keeps them. Slots
   1-4 are the d-pad's up, right, down, left while the Weapon Shortcuts button is held, 5-8 the same on its
   second page (L1 / R1); the keyboard has a key per slot (1-8). Stored in recvx.ini, not in the save:
   they are a player's controls, as the bindings, and survive any save or a new game. */
#ifndef RECVX_WEAPONS_H
#define RECVX_WEAPONS_H
#ifdef __cplusplus
extern "C" {
#endif

#define WPN_SLOTS 8
#define WPN_CHARS 4                    /* sys->ply_id: Claire, Chris, Steve, Wesker (Battle Game) */
extern int g_wpn_slots[WPN_CHARS][WPN_SLOTS];   /* item ids (itemdata index, no ammo kind), 0 none */

int  wpn_slot_of(int chr, int id);     /* the slot holding item id, -1 none */
void wpn_assign(int chr, int slot, int id);   /* id 0 clears; a weapon has one slot: its old one is cleared */
int  wpn_bag_find(const unsigned int* inv, int n, int id);   /* the bag slot (1..n) of item id (first), 0 none */

/* recvx.ini: "weapon_slots_claire = 9 5 0 0 0 0 0 0" */
int  wpn_ini_line(const char* key, const char* value);   /* 1 = a weapon slots line */
void wpn_ini_write(void* FILE_ptr);

#ifdef __cplusplus
}
#endif
#endif
