/* Quick knife (docs/ARCHITECTURE.md "Input"): while its key is held the knife is equipped and
   readied as if chosen in the inventory and aimed, so the fire button slashes; on release the
   weapon equipped before comes back once the knife is lowered again. Everything goes through
   the inventory's own WeaponSet (equipped slot sys->itm[ply*16], weapon number, the weapon
   file load, motions), so ammo, aiming, firing, the third-person pose and the co-op pose of
   the knife are the game's. Game thread: knife_pad from bhSetPad (pad.c), after tps_pad. */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/sub1.h"
#include "../src/host/cheat.h"
#include "../src/host/bindings.h"   /* g_wpn_overlay */
#include <stdlib.h>

int pad_quick_knife(void);
int pc_in_play(const BH_PWORK* p, int scope);   /* reload_glue.c */
int pc_quick_knife_on(void);   /* the option (host.c) */

#define KNIFE 8                         /* item 8 "Combat Knife", weapon 2 (WpnTab flag 0x40000000: the knife's hit check) */
#define ID(v) ((v) >> 16 & 0x7FF)       /* item id without the ammo kind and unlimited bits */

static int state;                       /* 0 idle, 1 held, 2 released: the weapon before comes back when the knife is down */
static int own;                         /* we equipped it (else it was equipped already: nothing to give back) */
static int knife_slot, prev_slot, prev_id;

static unsigned int* inv(void) { return &sys->itm[sys->ply_id * 16]; }

/* normal play: our player in control; no event, door, menu, first person or attract demo (reload_glue.c) */
static int in_play(void) { return pc_in_play(plp, 0); }

/* weapon down and standing, turning, walking, running or stepping back (not the quick turn,
   stairs, pushing, ...), or in the idle animation (mode2 13, after 10 s standing: flg 0x10000 is
   its pad lock, which the aim button breaks as from standing, bhCPM0_action) */
static int at_rest(void)
{
    return in_play() && plp->mode1 == 0 && ((plp->mode2 <= 11 && !(plp->flg & 0x10000)) || plp->mode2 == 13);
}
int pc_weapon_at_rest(void) { return sys && plp && at_rest(); }   /* also the weapon shortcuts' (weapon_glue.c) */

/* the knife is drawn by its key or about to be given back: the weapon shortcuts wait (weapon_glue.c) */
int knife_busy(void) { return state || (pc_quick_knife_on() && g_cheat.noclip != 2 && pad_quick_knife()); }
int knife_drawn(void) { return state != 0; }   /* (the wheel, once up, minds only this) */

static void equip(int slot)
{
    swork.pip = inv();
    WeaponSet(slot, slot);   /* as the inventory: weapon number, equipped slot, weapon file load */
}

static void give_back(void)
{
    unsigned int* iv = inv();
    int i, n = (sys->gm_flg & 0x8000000) ? 11 : 9, to = 0;
    if (!prev_id) to = knife_slot;   /* nothing before: the knife's slot again unequips it (WeaponSet) */
    else if (ID(iv[prev_slot]) == prev_id) to = prev_slot;
    else
        for (i = 1; i <= n && !to; i++) if (ID(iv[i]) == prev_id) to = i;   /* moved */
    host_log("knife: back to %s (slot %d, item %d, count %d)\n", !prev_id ? "no weapon" : to ? "the weapon before" : "nothing (it is gone), the knife stays",
             to, to && prev_id ? ID(iv[to]) : 0, to && prev_id ? iv[to] & 0xFFFF : 0);
    if (to) equip(to);
    state = 0;
}

/* RECVX_KNIFE_TRACE=1 (test aid): state, player mode and the room's enemy health every 15 frames
   while the key is held. RECVX_KNIFE_DROP=1: drawing the knife empties the previous weapon's
   slot (as if it were used up or thrown away meanwhile). */
static int knife_drop(void)
{
    static int on = -1;
    if (on < 0) on = getenv("RECVX_KNIFE_DROP") != NULL;
    return on;
}

static void trace(void)
{
    static int on = -1, n;
    float hp = 0;
    unsigned int* iv = inv();
    if (on < 0) on = getenv("RECVX_KNIFE_TRACE") != NULL;
    if (!on || n++ % 15) return;
    for (int i = 0; i < sys->ewk_n; i++) if (ene[i].flg & 1) hp += ene[i].hp;
    host_log("knife: trace state %d play %d rest %d mode %d/%d/%d/%d weapon %d inv %x %x %x %x enemy hp %.0f\n", state, in_play(), at_rest(), plp->mode0,
             plp->mode1, plp->mode2, plp->mode3, plp->wpnr_no, iv[0], iv[1], iv[2], iv[3], hp);
}

void knife_game_exit(void) { state = 0; }

/* pad.c bhSetPad, before pad_ps is derived (virtual bits) */
unsigned int knife_pad(unsigned int pad)
{
    unsigned int* iv;
    int held, i, n;
    if (!sys || !plp) return pad;
    iv = inv();
    if (state && (plp->mode0 == 3 || plp->hp <= 0 ||iv[0] != (unsigned int)knife_slot || ID(iv[knife_slot]) != KNIFE)) {
        host_log("knife: weapon changed elsewhere, nothing to give back\n");   /* inventory, script, game over */
        state = 0;
    }
    held = pc_quick_knife_on() && g_cheat.noclip != 2 && !g_wpn_overlay && pad_quick_knife();   /* not while the weapon wheel is up */
    if (held) trace();
    if (held && state == 2) state = 1;
    if (held && !state && at_rest()) {
        n = (sys->gm_flg & 0x8000000) ? 11 : 9;   /* [1] the separate slot (lighter), [2..] the grid */
        for (i = 1, knife_slot = 0; i <= n && !knife_slot; i++) if (ID(iv[i]) == KNIFE) knife_slot = i;
        if (knife_slot) {
            state = 1;
            own = iv[0] != (unsigned int)knife_slot;
            prev_slot = iv[0];
            prev_id = prev_slot ? ID(iv[prev_slot]) : 0;
            host_log("knife: drawn from slot %d (before: slot %d, item %d, count %d)\n", knife_slot, prev_slot, prev_id, prev_id ? iv[prev_slot] & 0xFFFF : 0);
            if (own) equip(knife_slot);
            if (own && prev_id && knife_drop()) iv[prev_slot] = 0;
        }
    }
    if (state == 1 && !held) {
        state = own ? 2 : 0;
        host_log("knife: released in mode %d/%d%s\n", plp->mode1, plp->mode2, own ? "" : " (it was equipped before: it stays)");
    }
    if (state == 2 && at_rest()) give_back();   /* after the swing: the game lowers the knife first */
    if (state == 1 && in_play()) pad |= 0x10;   /* readied: fire slashes */
    return pad;
}
