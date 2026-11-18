/* Reload button (docs/USER_GUIDE.md "Reload"). The game reloads only when the trigger is pulled on
   an empty magazine with the weapon raised (player.c bhCPM2_act_wpn, playpch2.c bhCPM2_act_scp):
   bhSearchBullet combines the first ammo in the inventory that reloads the equipped weapon into it,
   as the inventory's combine does (sub1.c Combi_99: up to the weapon's magazine, the rest stays in
   the stack), and mode 70 (bhCPM2_act_rld) plays the reload motion and sounds. The button does
   exactly that whenever the magazine is not full: with the weapon raised (aiming or the scope) as
   the game's own, or lowered wherever the aim button would raise it (standing, walking, running):
   then the weapon comes up straight into
   the reload and goes down after it, as an aimed reload with the aim let go does. Weapons the
   game does not reload by itself (grenade launcher: its rounds are combined in the inventory) and
   unlimited ammo stay as they are. Game thread: player.c bhControlPlayer, right after the mode
   handler, where the game's own switch to mode 70 happens too. A binding shared with another action
   (Aim + button, pc/src/input/bindings.c bind_combo) is held only after a press with aim held, so the
   weapon is up or coming up: a press while it comes up reloads once it is. */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/player.h"
#include "../../decomp/include/ps2/veronica/prog/sub1.h"
#include "../../decomp/include/ps2/veronica/prog/weapon.h"
#include "../../decomp/include/ps2/veronica/prog/item.h"
#include <stdio.h>
#include <stdlib.h>

int pad_reload(void);

static int rest;   /* a reload started with the weapon lowered, the aim not held since */

/* normal play: player p (plp: ours in control) with no event, door, menu, first person (but the scope: scope = 1)
   or attract demo running. Also quick knife (knife_glue.c) and modern controls (modern_glue.c), with scope 0. */
int pc_in_play(const BH_PWORK* p, int scope)
{
    return p == &ply && p->mnwP && p->mlwP && (sys->sp_flg & 0x40) && !(sys->ss_flg & 0x400000) &&
           !(sys->st_flg & (scope ? 0x1 : 0x800001)) && !(sys->gm_flg & (scope ? 0x9 : 0x49)) && !(sys->cb_flg & 0x5) && !(sys->ts_flg & 0x80) &&
           !*(int*)&sys->mn_mode0 && !*(int*)&sys->mn_md0 && p->mode0 == 1;
}

/* a reload binding shared with another action takes a press with aim held only here (pc/src/input/bindings.c
   bind_combo_frame, pad report, game thread): elsewhere (menus, events) the button is the other action's */
int reload_combo_ok(void) { return sys && pc_in_play(&ply, 1); }

/* third person: the reload of a lowered weapon is no aim (camera, crosshair, facing; tps_glue.c) */
int reload_at_rest(void) { return rest; }

void reload_player(void)
{
    static int was;
    int now = pad_reload(), raised, lowered, bn, max;
    unsigned int* iv = swork.pip;   /* the inventory bhCheckBullet / bhSearchBullet read */
    const WPN_TAB* w = &WpnTab[plp->wpnr_no];

    {   /* RECVX_PAD_TRACE=1: the game's virtual pad (pad.c bhSetPad bits: aim 0x10, fire 0x100, map 0x2000) when it changes */
        static int trace = -1;
        static unsigned int last;
        if (trace < 0) trace = getenv("RECVX_PAD_TRACE") != NULL;
        if (trace && plp == &ply && sys->pad_on != last) printf("pad: game pad %05x mode %d/%d\n", last = sys->pad_on, plp->mode1, plp->mode2);
    }
    if (rest && plp == &ply && (plp->mode0 != 1 || plp->mode1 != 1 || (sys->pad_on & 0x10))) rest = 0;
    if (!now || was || !pc_in_play(plp, plp->mode1 == 1 && plp->mode2 == 71)) { was = now; return; }
    /* raised: where the trigger reloads (the aim pose held, the scope ready); lowered: where the aim button raises the
       weapon (bhCPM0_action); not the sniper rifle / linear launcher, whose reload ends in the scope */
    raised = plp->mode1 == 1 && (plp->at_flg & 0x10) && ((plp->mode2 >= 65 && plp->mode2 <= 67 && plp->mode3 == 1) || plp->mode2 == 71);
    lowered = plp->mode1 == 0 && (plp->flg & 0x20000) && !(plp->flg & 0x4000000) && !(plp->stflg & 0x10400) && !pc_scoped(plp->wpnr_no);
    if (!raised && !lowered && plp->mode1 == 1 && (plp->mode2 == 64 || plp->mode2 == 164 || (plp->mode2 >= 65 && plp->mode2 <= 67 && !(plp->at_flg & 0x10))))
        return;   /* pressed while the weapon comes up (Aim + reload at once): once it is */
    was = now;
    if ((!raised && !lowered) || !iv || !iv[0] || (w->flg & 0x40000000) || (plp->wpnr_no == 18 && (sys->ef_flg & 0x2))) return;
    bn = bhCheckBullet();   /* -1: unlimited */
    max = (int)getbulletmax[(iv[iv[0]] >> 16) & 0xFF][sys->gm_mode];
    if (bn < 0 || bn >= max) return;
    if (!bhSearchBullet()) {
        printf("reload: item %d magazine %d/%d, nothing in the inventory reloads it\n", (iv[iv[0]] >> 16) & 0xFF, bn, max);
        return;
    }
    printf("reload: item %d magazine %d -> %d/%d (%s)\n", (iv[iv[0]] >> 16) & 0xFF, bn, iv[iv[0]] & 0xFFFF, max, raised ? "raised" : "lowered");
    if (lowered) {   /* the weapon comes up as bhCPM0_action raises it, straight into the reload (bhCPM2_act_suw ends in the straight aim) */
        plp->flg |= 0x10000;
        plp->stflg |= 0x400;
        plp->at_flg = 2;
        plp->wax = plp->waxp = 0;
        plp->mode1 = 1;
        *(int*)plp->exp1 &= ~0x4;
        *(int*)plp->exp1 |= 0x1E0;
        rest = !(sys->pad_on & 0x10);
    }
    sys->gm_flg &= ~0x40000;   /* as the game's switch: "empty, reload on the next pull" done */
    plp->mode2 = 70;
    plp->mode3 = 0;
}
