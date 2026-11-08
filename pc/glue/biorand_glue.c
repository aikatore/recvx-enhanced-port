/* BioRand packs in game (pc/src/host/biorand.h, docs/USER_GUIDE.md "Randomizer (BioRand)"). Its room files carry
   the randomized items the engine reads (docs/modding/ROOM_FORMAT.md "Enemies, objects, items"): an item record's
   `type` is the item the pickup gives (its `id` stays the model), `wrk_no` its amount (decomp hitchk.c, sub1.c
   GetItem); the start inventory is a block of room 0/0's entry script under "ev_flg 1 clear" (C7 145 -> X,
   C7 0 -> X, C7 0xFF X n for the STANDARD slot, C9 counts; event.c). Here: rifle ammo stacking, the save words,
   the load screen's question, a co-op guest's start inventory, the sound of a scene's cast shot and a weapon the
   scripts put in the hand. Game thread. */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/item.h"
#include "../../decomp/include/ps2/veronica/prog/flag.h"
#include "../../decomp/include/ps2/veronica/prog/player.h"
#include "../../decomp/include/ps2/veronica/prog/sdfunc.h"
#include "../src/host/biorand.h"
#include "../src/host/lang.h"
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

int pc_in_game(void);   /* options_glue.c */
static void shot_frame(void);
void spoiler_tick(void);   /* spoiler_menu.c */

/* "Rifle ammo stacks" (GAMEPLAY), always while BioRand is on (biorand_rifle_stack): the stack flag of the rifle and
   assault rifle bullets (item.c itemdata 40, 42) */
void biorand_frame(void)
{
    unsigned short on = biorand_rifle_stack() ? 0x200 : 0;
    itemdata[40].type = (itemdata[40].type & ~0x200) | on;
    itemdata[42].type = (itemdata[42].type & ~0x200) | on;
    /* Nosferatu's poison (stflg 0x200000, en15.c PoisonAttack; herbs don't cure it) ends with him (ed_flg 205) in a
       BioRand game. The story cures it as Chris in 9/15: RM_6100 sets ev_flg 307 while Claire is poisoned, and there
       Chris brings the Serum (PlayerPoison2Cr clears Claire's saved ply_stflg[0]). With doors on BioRand takes the
       switch to Chris out of RM_6100 and 9/15 out of the game: Claire played on poisoned for good. As BioRand's own
       PS2 patch with doors on (DisableNosferatuPoison), she is then as if never poisoned, 307 too (9/15, if reached,
       plays the unpoisoned scene). Each game cures its own player; 307 reaches the co-op partner as any world flag.
       Also mends a save made poisoned. */
    if (pc_in_game() && biorand_active() && bhCkFlg(sys->ed_flg, 205) && ((plp->stflg | sys->ply_stflg[0]) & 0x200000)) {
        plp->stflg &= ~0x200000;
        sys->ply_stflg[0] &= ~0x200000;
        bhCrFlg(sys->ev_flg, 307);
        host_log("biorand: Nosferatu is dead: Claire's poison cured (ev_flg 307 cleared)\n");
    }
    shot_frame();
    spoiler_tick();   /* the --dev spoiler log screen (spoiler_menu.c) */
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

/* sub1.c ItemBoxChange: BioRand places key items anywhere, so Claire may box the Security Card/File and Crystal, and take key items out of the box in the Antarctic mansion study (9/26) */
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
    if (seed == 0xFFFFF) snprintf(ask[0], sizeof(ask[0]), tr("Save: different BioRand game."));
    else snprintf(ask[0], sizeof(ask[0]), tr("Save: BioRand seed %d."), seed);
    if (!biorand_pack()) snprintf(ask[1], sizeof(ask[1]), tr("No BioRand game is generated."));
    else if (seed == cur) snprintf(ask[1], sizeof(ask[1]), tr("Generated game: different options."));
    else if (cur >= 0) snprintf(ask[1], sizeof(ask[1]), tr("Generated game: seed %d."), cur);
    else snprintf(ask[1], sizeof(ask[1]), tr("Generated game differs."));
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
   from the pack's seed number (cast_appear_pick: the pool its "Model Selection" chooses, g_biorand_appear_pool, "Custom"
   its own list cast_appear_off; imports with its "Include Imported Models"; "Custom" with none checked: no pick): one for the whole game (1, "Same for Both"), or (2, "Per Character") one for Claire, another
   for Chris and a third for Steve's section of Claire's part. They are this game's, not the user's: save word
   BR_APPEAR holds 'A' << 24 | Claire's chr (the one look), BR_APPEAR2 'A' << 24 | Steve's chr << 12 | Chris's (chrs
   < 4096) with "Per Character", else 0, so the game's saves keep them and loading one brings them back (the region is
   loaded whole), whatever the setting says now; a new game without it writes 0. A save of a version with one look
   has no BR_APPEAR2: the same look for all, as it was. recvx.ini's coop_char (the user's own choice) never changes.
   They count while BioRand is on and we are not a co-op guest (a guest's look stays its own; it has the host's
   state, words and all); choosing a look in game drops them. */
#define BR_APPEAR 290
#define BR_APPEAR2 291
#define BR_APPEAR_ON 0x41000000u

void pc_biorand_appear_new(void)
{
    int chr[3], k, seed = biorand_seed(), imports = g_biorand_appear_imports > 0 ? __atomic_load_n(&actor_import_n, __ATOMIC_ACQUIRE) : 0;
    if (!sys) return;
    sys->itm[BR_APPEAR] = sys->itm[BR_APPEAR2] = 0;
    if (!g_biorand_appear || !biorand_active() || coop_biorand() >= 0 || seed < 0) return;
    for (k = 0; k < (g_biorand_appear == 2 ? 3 : 1); k++) chr[k] = cast_appear_pick((unsigned)seed, g_biorand_appear_pool, imports, cast_appear_off, k);   /* (different ones) */
    if (!chr[0]) { host_log("biorand: randomize played as: custom models, none checked: no pick\n"); return; }
    sys->itm[BR_APPEAR] = BR_APPEAR_ON | (unsigned int)chr[0];
    if (g_biorand_appear == 2) {
        sys->itm[BR_APPEAR2] = BR_APPEAR_ON | (unsigned int)chr[2] << 12 | (unsigned int)chr[1];
        host_log("biorand: randomize played as, per character: seed %d -> Claire %s (chr %d), Chris %s (chr %d), Steve %s (chr %d), this game only\n", seed,
                 appear_name(chr[0]), chr[0], appear_name(chr[1]), chr[1], appear_name(chr[2]), chr[2]);
    } else host_log("biorand: randomize played as: seed %d -> %s (chr %d, this game only)\n", seed, appear_name(chr[0]), chr[0]);
}

int pc_biorand_appear(int ply)
{
    unsigned int w;
    int chr;
    if (!sys || !pc_in_game() || !biorand_active() || coop_biorand() >= 0) return -1;
    w = sys->itm[BR_APPEAR];
    chr = (int)(w & 0xFFFFFF);
    if ((w & 0xFF000000u) != BR_APPEAR_ON || chr < 1) return -1;
    if (ply < 0) ply = sys->ply_id & 3;
    w = sys->itm[BR_APPEAR2];
    if (ply && (w & 0xFF000000u) == BR_APPEAR_ON && (int)(w >> (ply == 2 ? 12 : 0) & 0xFFF) > 0) chr = (int)(w >> (ply == 2 ? 12 : 0) & 0xFFF);   /* Chris's (Wesker: his), Steve's */
    return chr <= COOP_CHR_MAX || chr - COOP_CHR_IMPORT < __atomic_load_n(&actor_import_n, __ATOMIC_ACQUIRE) ? chr : -1;   /* (an import no longer listed: the user's) */
}

void pc_biorand_appear_drop(void)
{
    if (pc_biorand_appear(0) < 0) return;
    sys->itm[BR_APPEAR] = sys->itm[BR_APPEAR2] = 0;
    host_log("biorand: randomize played as: a look chosen in game; this game's picks dropped\n");
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

/* ---------------------------------------------------------------- a cast member's shot in a scene
   A scene's cast member fires with WeaponSeCall 1 (event.c bhWeaponSeCall; retail: Claire's three shots in RM_0030,
   Steve's scene at the main gate). The shot sounds with the player's weapon (WpnTab seno0, from the arms sound bank,
   SE bank 1), which the scene sets first with ArmsItemChange (RM_0030: the handgun, at the end none). BioRand's packs
   (3.3.2 on, "Steve must not change the player's weapon") take both changes out: the player keeps their own weapon, so
   the shots were silent (the knife or no weapon: no sound) or another gun's, as on a PS2 with such a pack. Here, in a
   room whose scripts change no weapon, the event that has such a shot loads the shot weapon's arms bank while it runs
   and the shot plays that weapon's sound; afterwards the player's bank is loaded again. The player's weapon stays. */
extern BH_SCEWORK bhEtask[16];
static int shot_w = -1, shot_lvl = -1, shot_bank;   /* the scene's weapon (WpnTab) and its task level; shot_bank: we loaded its bank */
static const void* shot_evtp;

static int arms_bank(int w) { return w >= 0 && w < 23 ? WpnTab[w].snd_wpno : -1; }

/* the room's scripts (rom->evtp: u32 offsets of scd0, scd1 and the events): their count */
static unsigned int scripts(void)
{
    const unsigned int* tab = (const unsigned int*)rom->evtp;
    unsigned int first = tab[0], k;
    for (k = 0; k * 4 < first && k < 1024; k++) if (tab[k] && tab[k] < first) first = tab[k];
    return first / 4;
}

/* the first op (sub: its first operand, -1 any) of the room's script at o, up to the next script's start (the last one:
   to its End / EvtEnd); -1 none */
static int find_op(unsigned int o, int op, int sub)
{
    const unsigned char* d = (const unsigned char*)rom->evtp;
    const unsigned int* tab = (const unsigned int*)rom->evtp;
    unsigned int lim = 0, k, ns = scripts();
    int n;
    for (k = 0; k < ns; k++) if (tab[k] > o && (!lim || tab[k] < lim)) lim = tab[k];
    for (k = 0; k < 8192 && (!lim || o < lim) && (n = coop_insn_len_raw(d + o)) > 0; k++, o += (unsigned int)n) {
        if (d[o] == op && (sub < 0 || d[o + 1] == sub)) return (int)o;
        if (!lim && (d[o] == 0 || d[o] == 255)) break;
    }
    return -1;
}

/* event.c Event_init: task level runs the event at p */
void pc_cast_shot_event(int level, const unsigned char* p)
{
    const unsigned int* tab;
    unsigned int k, ns;
    int at;
    if (level < 0 || level >= 16 || !rom || !rom->evtp) return;   /* (co-op's replay task is no level) */
    if (level == shot_lvl) shot_w = -1;
    if ((at = find_op((unsigned int)(p - (const unsigned char*)rom->evtp), 73, 1)) < 0) return;   /* WeaponSeCall 1 */
    at = ((const unsigned char*)rom->evtp)[at + 5];
    if (at >= 23 || !WpnTab[at].seno0) return;
    tab = (const unsigned int*)rom->evtp;
    for (k = 0, ns = scripts(); k < ns; k++) if (tab[k] && find_op(tab[k], 39, -1) >= 0) return;   /* ArmsItemChange: the scene arms itself */
    shot_w = at, shot_lvl = level, shot_evtp = rom->evtp;
    host_log("cast shot: task level %d fires weapon %d, the room sets no weapon: its arms bank %d for the scene\n", level, at, arms_bank(at));
}

/* event.c bhWeaponSeCall 1: the sound of the cast's shot with weapon w */
int pc_cast_shot_se(int w)
{
    if (shot_bank && shot_w >= 0 && w >= 0 && w < 23 && arms_bank(w) == arms_bank(shot_w)) return WpnTab[w].seno0;
    return WpnTab[plp->wpnr_no].seno0;
}

/* ---------------------------------------------------------------- a weapon the script puts in the hand
   ArmsItemChange w (event.c bhArmsItemChange) sets the weapon number whatever the bag holds. Retail does it where the
   player has the weapon by then: RM_6060, picking up the sniper rifle before Nosferatu (it_flg 276: then ArmsItemSet
   3 puts its bag slot in the hand); Steve's sub machine gun in 3180 (his own bag's); the lighter in 0000 (its
   PlItemCheck). BioRand puts another item on the sniper rifle's spot, the pickup still sets it_flg 276, and the
   player held a sniper rifle not in the bag (the bag slot in the hand stays the old weapon's, so its rounds). With
   BioRand a weapon the player's bag (words 1..11) does not hold is not put in the hand: the weapon held stays (its
   files and sounds unchanged). 1: skip. */
int pc_biorand_arms_skip(int w)
{
    /* the item of each weapon number (sub1.c WeaponSet, the other way round; 3 also 131, its other handgun) */
    static const unsigned char item[23] = { 0, 55, 8, 5, 9, 10, 32, 33, 34, 142, 7, 4, 2, 3, 6, 6, 6, 6, 11, 7, 1, 0, 0 };
    const unsigned int* bag;
    int i, id;
    if (w <= 0 || w >= 23 || !item[w] || !sys || !biorand_active()) return 0;
    bag = &sys->itm[sys->ply_id * 16];
    for (i = 1; i < 12; i++)
        if ((id = (bag[i] >> 16) & 0xFF) == item[w] || (w == 3 && id == 131)) return 0;
    host_log("biorand: the script's weapon %d is not in the bag: the hand keeps weapon %d\n", w, plp ? plp->wpnr_no : -1);
    return 1;
}

/* every frame: the scene's arms bank while its event runs, the player's after it; one sound bank request at a time
   (the room's loading waits for ours as for its own) */
static void shot_frame(void)
{
    int b;
    if (shot_w >= 0 && (!pc_in_game() || !rom || rom->evtp != shot_evtp || !bhEtask[shot_lvl].status)) shot_w = -1;
    if (!pc_in_game() || !plp || CheckTransEndSoundBank()) return;
    if (shot_w >= 0 && !shot_bank && arms_bank(shot_w) != arms_bank(plp->wpnr_no)) b = arms_bank(shot_w), shot_bank = 1;
    else if (shot_w < 0 && shot_bank) b = arms_bank(plp->wpnr_no), shot_bank = 0;
    else return;
    host_log("cast shot: arms bank %d (%s)\n", b, shot_bank ? "the scene's weapon" : "the player's again");
    if (b >= 0) RequestArmsSoundBank(b);
}
