/* BioRand packs in game (pc/src/host/biorand.h, docs/USER_GUIDE.md "Randomizer (BioRand)"). Its room files carry
   the randomized items the engine reads (docs/modding/ROOM_FORMAT.md "Enemies, objects, items"): an item record's
   `type` is the item the pickup gives (its `id` stays the model), `wrk_no` its amount (decomp hitchk.c, sub1.c
   GetItem); the start inventory is a block of room 0/0's entry script under "ev_flg 1 clear" (C7 145 -> X,
   C7 0 -> X, C7 0xFF X n for the STANDARD slot, C9 counts; event.c). Here: rifle ammo stacking, the save words,
   the load screen's question and a co-op guest's start inventory. Game thread. */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/item.h"
#include "../src/host/biorand.h"
#include "../src/host/biorand_opts.h"
#include "../src/host/actors.h"
#include "../src/host/cast.h"
#include "../src/host/enemies.h"
#include "../src/host/savestore.h"
#include "../include/pc_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void host_log(const char* fmt, ...);

/* "Rifle ammo stacks" (GAMEPLAY), always while BioRand is on (biorand_rifle_stack): the stack flag of the rifle and
   assault rifle bullets (item.c itemdata 40, 42) */
void biorand_frame(void)
{
    unsigned short on = biorand_rifle_stack() ? 0x200 : 0;
    itemdata[40].type = (itemdata[40].type & ~0x200) | on;
    itemdata[42].type = (itemdata[42].type & ~0x200) | on;
}

/* ---------------------------------------------------------------- the save
   Two save-region words no game code uses (AllItemInit clears them; the item boxes end at itm[255], co-op's words
   are 256..287 and 320..359): BR_W 'B' << 24 | seed (0xFFFFF unknown) while BioRand is on, else 0; BR_W + 1 the
   pack's id. Older builds keep them as they are; their saves have 0 (no BioRand). */
#define BR_W 288
#define BR_ON 0x42000000u
#define BR_OFFSET ((int)((char*)&((SYS_WORK*)0)->itm[BR_W] - (char*)&((SYS_WORK*)0)->version))

static void words(unsigned int* w)
{
    int on = biorand_active(), seed = biorand_seed();
    w[0] = on ? BR_ON | (seed >= 0 ? (unsigned int)seed : 0xFFFFFu) : 0;
    w[1] = on ? biorand_id() : 0;
}

/* the game state: this game's (a new game: coop_glue.c pc_quick_start; a co-op guest's start) */
void pc_biorand_stamp(void)
{
    if (sys) words(&sys->itm[BR_W]);
}

/* sub1.c ItemBoxChange: BioRand places the Security Card/File and Crystal anywhere, so Claire may box them */
int pc_biorand_box_any(void) { return biorand_active(); }

/* ps2_LoadScreen.c and the lobby: the save just read (card bytes from sys->version) decides this game's BioRand state
   (biorand_game): a plain save plays without BioRand, a save of the pack there with it. A BioRand save whose pack is
   not there (another seed or options, or none): 1, the question's lines; loaded anyway, it plays the pack there, or
   without BioRand when there is none (pc_biorand_save_accept). */
static char ask[3][64];
int pc_biorand_save_differs(const void* save)
{
    unsigned int w[2];
    int seed, cur = biorand_seed();
    memcpy(w, (const char*)save + BR_OFFSET, sizeof(w));
    if (!biorand_shown() || (w[0] & 0xFF000000u) != BR_ON) { biorand_game(0); return 0; }   /* (BioRand only with --dev) */
    if (biorand_pack() && w[1] == biorand_id()) { biorand_game(1); return 0; }
    seed = (int)(w[0] & 0xFFFFF);
    if (seed == 0xFFFFF) snprintf(ask[0], sizeof(ask[0]), "Save: different BioRand game.");
    else snprintf(ask[0], sizeof(ask[0]), "Save: BioRand seed %d.", seed);
    if (!biorand_pack()) snprintf(ask[1], sizeof(ask[1]), "No BioRand game is generated.");
    else if (seed == cur) snprintf(ask[1], sizeof(ask[1]), "Generated game: different options.");
    else if (cur >= 0) snprintf(ask[1], sizeof(ask[1]), "Generated game: seed %d.", cur);
    else snprintf(ask[1], sizeof(ask[1]), "Generated game differs.");
    snprintf(ask[2], sizeof(ask[2]), biorand_pack() ? "Items and doors will differ. Load anyway?" : "Load it without BioRand?");
    host_log("biorand: %s %s %s\n", ask[0], ask[1], ask[2]);
    return 1;
}

const char* pc_biorand_ask(int i) { return i >= 0 && i < 3 ? ask[i] : ""; }

/* loaded anyway: the save goes on with the pack there, or without BioRand (its next save has it) */
void pc_biorand_save_accept(void* save)
{
    unsigned char* d = save;
    unsigned int w[2], sum = 0;
    int i;
    biorand_game(biorand_pack());
    words(w);
    memcpy(d + BR_OFFSET, w, sizeof(w));
    for (i = 0; i < SAVE_DATA_BYTES - 4; i++) sum += d[i];   /* its byte sum, as savestore_card_valid checks it */
    memcpy(d + SAVE_DATA_BYTES - 4, &sum, 4);
}

/* ---------------------------------------------------------------- "Randomize Played as" (docs/BIORAND_INGAME.md S5a)
   A new game with BioRand on and "Randomize Played as" on (g_biorand_appear) is given a "You appear as" look picked
   from the pack's seed number (cast_appear_pick: the cutscene cast's "All models" pool, imports with its "Include
   imported models"). It is this game's, not the user's: save word BR_APPEAR holds 'A' << 24 | chr, so the game's saves
   keep it and loading one brings it back (the region is loaded whole); a new game without it writes 0. recvx.ini's
   coop_char (the user's own choice) never changes. It counts while BioRand is on and we are not a co-op guest (a
   guest's look stays its own; it has the host's state, words and all); choosing a look in game drops it. */
#define BR_APPEAR 290
#define BR_APPEAR_ON 0x41000000u
int pc_in_game(void);   /* options_glue.c */

void pc_biorand_appear_new(void)
{
    int chr, seed = biorand_seed();
    if (!sys) return;
    sys->itm[BR_APPEAR] = 0;
    if (!g_biorand_appear || !biorand_active() || coop_biorand() >= 0 || seed < 0) return;
    chr = cast_appear_pick((unsigned)seed, g_enemy_opts.cast_imports ? __atomic_load_n(&actor_import_n, __ATOMIC_ACQUIRE) : 0);
    sys->itm[BR_APPEAR] = BR_APPEAR_ON | (unsigned int)chr;
    host_log("biorand: randomize played as: seed %d -> %s (chr %d, this game only)\n", seed, appear_name(chr), chr);
}

int pc_biorand_appear(void)
{
    unsigned int w;
    int chr;
    if (!sys || !pc_in_game() || !biorand_active() || coop_biorand() >= 0) return -1;
    w = sys->itm[BR_APPEAR];
    chr = (int)(w & 0xFFFFFF);
    if ((w & 0xFF000000u) != BR_APPEAR_ON || chr < 1) return -1;
    return chr <= COOP_CHR_MAX || chr - COOP_CHR_IMPORT < __atomic_load_n(&actor_import_n, __ATOMIC_ACQUIRE) ? chr : -1;   /* (an import no longer listed: the user's) */
}

void pc_biorand_appear_drop(void)
{
    if (pc_biorand_appear() < 0) return;
    sys->itm[BR_APPEAR] = 0;
    host_log("biorand: randomize played as: a look chosen in game; this game's pick dropped\n");
}

/* the question on the load screen (ps2_LoadScreen.c DispLoadMessageSelect 12), above YES / NO (message 466) */
void pc_ui_text(float x, float y, const char* s, float sc, unsigned int argb);   /* lobby_glue.c: the game's font */
float pc_ui_text_w(const char* s, float sc);
void pc_biorand_load_text(void)
{
    int i;
    for (i = 0; i < 3; i++) pc_ui_text(320 - pc_ui_text_w(ask[i], 0.75f) / 2, 250 + 26 * i, ask[i], 0.75f, 0xFFFFFFFFu);
}

/* ---------------------------------------------------------------- the start inventory
   A co-op guest without a game of its own starts with AllItemInit's kit (coop_glue.c own_restore); with BioRand
   the pack's, as the host's new game got it: the C7/C9 actions of the "ev_flg 1 clear" blocks of room 0/0's entry
   script (scd0), run by the game's own handlers on our inventory. Only when that flag is set in the state we got:
   else our own entry into 0/0 runs the block (it is a new game) and would give it twice. */
int coop_insn_len_raw(const unsigned char* p);   /* net/coop_proto.c */
extern unsigned char* bhScePtr;
extern unsigned int (*bhScenarioJmpT[256])();

void pc_biorand_start_kit(void)
{
    unsigned char *d, *save = bhScePtr;
    unsigned int at[PC_KIT_MAX];
    int size = 0, ops, i;
    if (!sys || !biorand_active() || !(sys->ev_flg[0] & 0x40000000u) || !(d = biorand_room0(&size))) return;
    if ((ops = pc_startkit_scan(d, (unsigned int)size, at, PC_KIT_MAX, coop_insn_len_raw)) < 0) {   /* (checked whole first: all of it or nothing) */
        free(d);
        host_log("biorand: the start inventory of the pack's room 0/0 is damaged: not given\n");
        return;
    }
    for (i = 0; i < ops; i++) { bhScePtr = d + at[i]; bhScenarioJmpT[d[at[i]]](); }
    bhScePtr = save;
    free(d);
    host_log("biorand: start inventory from the pack's room 0/0 (%d actions)\n", ops);
}
