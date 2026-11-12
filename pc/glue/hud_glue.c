/* The HUD (docs/THIRDPERSON.md "HUD"), the game's side: each frame what the overlay draws (overlay.cpp hud_draw) -
 * whether it shows, the health and condition, the equipped weapon, its loaded rounds and the reload ammo in the bag -
 * into g_hud (tps.h). Game thread, frame hand-off (libgraph.c). Nothing of the game is changed.
 *
 * Shown in play only: in a room with the player in control, hit or grabbed (not in an event, a cinema, a door, a
 * menu, a message, the item box, a trade, dead), by "HUD": Auto while the third-person camera is the view, On also
 * with the fixed cameras. The condition is the status screen's (sub1.c, its ECG): Fine from 120 health, Caution from
 * 60, Caution (orange) from 30, then Danger; poisoned (stflg 0x280000) over them all. The rounds are the equipped
 * slot's count as the inventory shows it (sub1.c BulletSet: a count, a percent, unlimited); the spare ammo the bag's
 * stacks the game reloads that weapon from (sub1.c bhSearchBullet: an ammo item's combination with the weapon, by
 * its kind of rounds for the grenade launcher and the bow gun); that kind of rounds also colours both counts.
 *
 * The weapon's picture is its inventory icon (ITEM AFS file 145, textures 3 and 4, as the trade screen draws them,
 * trade_glue.c), read once and turned into RGBA for the overlay's own texture: the game's texture memory is not used. */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/item.h"
#include "../src/host/tps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void host_log(const char* fmt, ...);
uint64_t gs_frame_count(void);
int pc_in_game(void);
int pc_inf_ammo(void);
int pc_trade_screen(void);
int pc_wpn_screen(void);
int pc_item_icon_file(unsigned char* buf, int cap, unsigned char** chunk);   /* trade_glue.c */
void pc_item_icon_cell(int id, int* tex, int* u, int* v);
extern int tps_cam_active;

TpsHud g_hud;

/* ---------------------------------------------------------------- the icons */
#define ICON_W 512
#define ICON_H 1024                    /* the two 512x512 textures, one above the other */
static unsigned int* icon_px;
static int icon_state;                 /* 0 not read, 1 ready, -1 failed */

const unsigned int* hud_icons(int* w, int* h)
{
    if (__atomic_load_n(&icon_state, __ATOMIC_ACQUIRE) != 1) return NULL;
    *w = ICON_W; *h = ICON_H;
    return icon_px;
}

/* a TIM2 of 8-bit indices with a 256-colour CLUT (32-bit, the GS's CSM1 order) at t, into out (ICON_W wide). The icons
   are drawn on the inventory's dark blue (no red to speak of, no green): those colours are left out. */
static int tim2_rgba(const TIM2_PICTUREHEADER_EX* t, unsigned int* out)
{
    const unsigned char* img = (const unsigned char*)t + 256;
    const unsigned int* clut = (const unsigned int*)(img + t->ImageSize);
    unsigned int pal[256];
    int x, y, i;
    if (t->FormatId != 1 || t->ImageType != 5 || t->ImageWidth != ICON_W || t->ImageHeight > ICON_H / 2 || t->ClutColors < 256) return 0;
    for (i = 0; i < 256; i++) {
        unsigned int c = clut[(i & 0xE7) | (i & 0x08) << 1 | (i & 0x10) >> 1], a = c >> 24;   /* CSM1: entries 8..15, 16..23 swapped */
        unsigned int r = c & 0xFF, g = c >> 8 & 0xFF, b = c >> 16 & 0xFF;
        if (r <= 24 && g <= 8 && b >= 32 && b <= 112) a = 0;
        pal[i] = (a >= 0x80 ? 255u : a * 2) << 24 | (c & 0xFFFFFF);   /* the GS's alpha: 0x80 opaque */
    }
    for (y = 0; y < t->ImageHeight; y++)
        for (x = 0; x < ICON_W; x++) out[y * ICON_W + x] = pal[img[y * ICON_W + x]];
    return 1;
}

static void icons_read(void)
{
    enum { CAP = 1100 * 1024 };
    unsigned char *buf = malloc(CAP), *chunk[5];
    int ok = 0;
    icon_px = calloc((size_t)ICON_W * ICON_H, 4);
    if (buf && icon_px && pc_item_icon_file(buf, CAP, chunk))   /* chunk 3: texture 0 (items 64..), chunk 4: texture 1 */
        ok = tim2_rgba((const TIM2_PICTUREHEADER_EX*)(chunk[3] + 32), icon_px) &&
             tim2_rgba((const TIM2_PICTUREHEADER_EX*)(chunk[4] + 32), icon_px + ICON_W * ICON_H / 2);
    free(buf);
    if (!ok) { free(icon_px); icon_px = NULL; }
    host_log("hud: item icons %s\n", ok ? "read" : "not read (no weapon pictures)");
    __atomic_store_n(&icon_state, ok ? 1 : -1, __ATOMIC_RELEASE);
}

/* ---------------------------------------------------------------- the counts */
static int counts(int id) { return itemdata[id].type & 0x1800; }   /* BulletSet: 0x800 a count, 0x1000 a percent, none: no number */

/* a stack's count as the inventory shows it: a percent of the item's most (at least 1 while not empty) */
static int shown(int id, int n)
{
    int p;
    if (!(itemdata[id].type & 0x1000) || itemdata[id].max < 100) return n;
    p = n / (itemdata[id].max / 100);
    return n && !p ? 1 : p;
}

/* ammo item y reloads weapon w (bhSearchBullet's w: the grenade launcher's and the bow gun's by their rounds): one of y's
   combinations is with w and loads it (flags 1, 5: the same rounds; 4 changes the launcher's, 7 makes other arrows) */
static int reloads(int y, int w)
{
    int z, it;
    if (!(itemdata[y].type & 0x2) || itemdata[y].cmb < 0) return 0;
    for (z = combidata[itemdata[y].cmb], it = itemdata[y].cmb + 1; z > 0; z--, it += 3)
        if (combidata[it] == w && ((combidata[it + 2] & 0xF) == 1 || (combidata[it + 2] & 0xF) == 5)) return 1;
    return 0;
}

static int in_play(void)
{
    int m0 = plp->mode0;   /* 1 in control, 2 hit, 4 thrown, 5 held by an enemy (player.c bhCtrPly_mode0) */
    int menu = *(int*)&sys->mn_mode0, mon = *(int*)&sys->mn_md0;   /* (the first) 3: a weapon loading (WeaponSet) */
    return plp == &ply && (sys->sp_flg & 0x40) && !(sys->ss_flg & 0x400000) && !(sys->st_flg & 0x201) && !(sys->gm_flg & 0x49) &&
           !(sys->cb_flg & 0x5) && !(sys->ts_flg & 0x80) && (!menu || sys->mn_mode0 == 3) && (!mon || sys->mn_md0 == 3) && !(plp->stflg & 0x1000000) &&
           (m0 == 1 || m0 == 2 || m0 == 4 || m0 == 5) && plp->hp >= 0 && !pc_trade_screen() && !pc_wpn_screen();
}

void hud_frame(void)
{
    const unsigned int* iv;
    unsigned int w;
    int id, kind, r, wpn, i, tex, u, v, cells, any = 0, spare = 0, inf;
    int on = g_tps.hud != 2 && pc_in_game() && plp && (g_tps.hud == 1 || (g_tps.on && tps_cam_active)) && in_play();
    static int trace = -1, was;
    if (trace < 0) trace = getenv("RECVX_HUD_TRACE") != NULL;   /* test aid: when it shows or hides, and what it shows */
    if (trace && !on && was) printf("hud: hidden at frame %u\n", (unsigned)gs_frame_count());
    was = on;
    if (!on) { g_hud.on = 0; return; }
    if (!icon_state) icons_read();
    iv = &sys->itm[(sys->ply_id & 3) * 16];
    w = iv[0] && iv[0] < 12 ? iv[iv[0]] : 0;
    id = (w >> 16) & 0xFF;
    kind = (w >> 16) & 0xFF00;
    g_hud.left = g_tps.hud_side == 1 || (g_tps.hud_side == 0 && g_tps.left && tps_cam_active);
    g_hud.fade = sys->fade_an <= 0 ? 0.0f : sys->fade_an >= 255 ? 1.0f : sys->fade_an / 255.0f;
    g_hud.hp = plp->hp;
    g_hud.hp_max = sys->gm_mode == 2 ? 320 : 160;   /* a full heal (sub1.c, system.c) */
    g_hud.cond = (plp->stflg & 0x280000) ? HUD_POISON : plp->hp >= 120 ? HUD_FINE : plp->hp >= 60 ? HUD_CAUTION : plp->hp >= 30 ? HUD_CAUTION2 : HUD_DANGER;
    if (id && icon_state == 1) {
        pc_item_icon_cell(id, &tex, &u, &v);
        g_hud.icon_x = u * 2; g_hud.icon_y = v * 2 + tex * ICON_H / 2;   /* (the 0..255 units of a 512x512 texture) */
        g_hud.icon_h = 48;
    }
    g_hud.icon_w = id && icon_state == 1 ? (itemdata[id].type & 0x100 ? 136 : 68) : 0;   /* (set once: the overlay may be reading) */
    inf = (kind & 0x800) || (plp->at_flg & 0x40000000) || pc_inf_ammo();   /* bhCheckBullet's unlimited */
    g_hud.rounds = !id || !counts(id) ? HUD_NONE : inf ? HUD_INF : shown(id, (int)(w & 0xFFFF));
    g_hud.rounds_pct = (itemdata[id].type & 0x1000) != 0;
    r = kind & 0x7000;   /* one bit per round type (sub1.c): 0x1000 acid 152, 0x2000 flame 153, 0x4000 gas 154 */
    wpn = id == 6 ? (r == 0x1000 ? 152 : r == 0x2000 ? 153 : r == 0x4000 ? 154 : id) : id == 7 && r == 0x2000 ? 155 : id;
    g_hud.round_kind = wpn >= 152 && wpn <= 155 ? wpn - 151 : 0;
    cells = (sys->gm_flg & 0x8000000) ? 10 : 8;   /* the side pack */
    for (i = 2; i < 2 + cells && g_hud.rounds >= 0; i++) {
        int y = (iv[i] >> 16) & 0xFF;
        if (y && reloads(y, wpn)) spare += shown(y, (int)(iv[i] & 0xFFFF));
    }
    for (i = 1; i < 156 && !any && g_hud.rounds >= 0; i++) any = reloads(i, wpn);   /* none at all (the Gold Lugers, the launchers): no number */
    g_hud.spare = any ? spare : HUD_NONE;
    g_hud.spare_pct = g_hud.rounds_pct;   /* (the percent weapons' ammo is in percent too) */
    if (trace) {
        static int last[9];
        int now[9] = { g_hud.on, g_hud.left, g_hud.hp, g_hud.cond, id, g_hud.rounds, g_hud.spare, g_hud.icon_w, g_hud.round_kind };
        if (memcmp(now, last, sizeof(now)))
            printf("hud: shown at frame %u: %s, health %d/%d condition %d, item %d (icon %dx%d), rounds %d%s (kind %d), spare %d\n", (unsigned)gs_frame_count(),
                   g_hud.left ? "left" : "right", g_hud.hp, g_hud.hp_max, g_hud.cond, id, g_hud.icon_w, g_hud.icon_h, g_hud.rounds, g_hud.rounds_pct ? "%" : "", g_hud.round_kind,
                   g_hud.spare);
        memcpy(last, now, sizeof(now));
    }
    g_hud.on = 1;
}
