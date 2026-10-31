/* Weapon shortcuts: the slots and their ini lines (weapons.h). Any thread reads them; the game
   thread changes them (pc/glue/weapon_glue.c), the main thread writes recvx.ini (plain ints). */
#include "../host/weapons.h"
#include <stdio.h>
#include <string.h>

int g_wpn_slots[WPN_CHARS][WPN_SLOTS];

static const char* const chr_keys[WPN_CHARS] = { "claire", "chris", "steve", "wesker" };

int wpn_slot_of(int chr, int id)
{
    if (chr < 0 || chr >= WPN_CHARS || !id) return -1;
    for (int s = 0; s < WPN_SLOTS; s++) if (g_wpn_slots[chr][s] == id) return s;
    return -1;
}

void wpn_assign(int chr, int slot, int id)
{
    int old;
    if (chr < 0 || chr >= WPN_CHARS || slot < 0 || slot >= WPN_SLOTS) return;
    if ((old = wpn_slot_of(chr, id)) >= 0) g_wpn_slots[chr][old] = 0;
    g_wpn_slots[chr][slot] = id;
}

/* inv: the character's 16 words (word 0 the equipped slot, 1 the lighter's, then the grid) */
int wpn_bag_find(const unsigned int* inv, int n, int id)
{
    for (int i = 1; i <= n && id; i++) if ((int)(inv[i] >> 16 & 0xFF) == id) return i;
    return 0;
}

int wpn_ini_line(const char* key, const char* value)
{
    for (int c = 0; c < WPN_CHARS; c++) {
        char k[32];
        int v[WPN_SLOTS] = { 0 };
        snprintf(k, sizeof(k), "weapon_slots_%s", chr_keys[c]);
        if (strcmp(key, k)) continue;
        sscanf(value, "%d %d %d %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
        for (int s = 0; s < WPN_SLOTS; s++) g_wpn_slots[c][s] = 0;
        for (int s = 0; s < WPN_SLOTS; s++) if (v[s] > 0 && v[s] < 256) wpn_assign(c, s, v[s]);
        return 1;
    }
    return 0;
}

void wpn_ini_write(void* fp)
{
    FILE* f = (FILE*)fp;
    for (int c = 0; c < WPN_CHARS; c++) {
        fprintf(f, "weapon_slots_%s =", chr_keys[c]);
        for (int s = 0; s < WPN_SLOTS; s++) fprintf(f, " %d", g_wpn_slots[c][s]);
        fprintf(f, "\n");
    }
}
