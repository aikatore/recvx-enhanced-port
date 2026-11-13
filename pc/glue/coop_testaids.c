/* Co-op test aids of coop_frame (headless runs, docs/COOP.md "Test aids"), apart from the session
 * code: each reads its environment variable once and does nothing without it. coop_glue.c calls
 * coop_test_aids once per frame, at its start, with what they need of the session (CoopTest).
 * Frames are the host's (test_clock: the guest takes them from the host's pose).
 */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../src/host/coop.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

void host_log(const char* fmt, ...);
void bhCheckMothEgg(void);   /* player.c (its header clashes with libc's macros here) */

void coop_test_aids(CoopTest* t)
{
    {   /* test aid: RECVX_COOP_REJOIN="frame[,frame...]" joins again (a new session, as Disconnect + Join) */
        static const char* rj = (const char*)-1;
        if (rj == (const char*)-1) rj = getenv("RECVX_COOP_REJOIN");
        if (rj && *rj && !t->host && t->on && t->clock >= (unsigned)atoi(rj)) { rj = strchr(rj, ',') ? strchr(rj, ',') + 1 : NULL; t->req = COOP_REQ_JOIN; host_log("coop: test: joining again\n"); }
    }
    {   /* test aid: RECVX_COOP_KICK="frame" presses "Remove stuck player" at that frame (host) */
        static const char* kk = (const char*)-1;
        if (kk == (const char*)-1) kk = getenv("RECVX_COOP_KICK");
        if (kk && *kk && t->host && t->on && t->frame >= (unsigned)atoi(kk)) { kk = NULL; t->req = COOP_REQ_KICK; host_log("coop: test: remove the partner\n"); }
    }
    {   /* test aid: RECVX_COOP_BYE_GRABBED="n" (guest): Disconnect (a goodbye) n frames into a zombie grab our game runs */
        static const char* bg = (const char*)-1;
        static int in;
        int k, held = 0;
        if (bg == (const char*)-1) bg = getenv("RECVX_COOP_BYE_GRABBED");
        for (k = 0; bg && !t->host && t->on && k < PC_ENE_SLOTS; k++) held |= t->owned[k];
        in = held ? in + 1 : 0;
        if (bg && in > atoi(bg)) { bg = NULL; t->req = COOP_REQ_STOP; host_log("coop: test: disconnect while grabbed\n"); }
    }
    if (!t->on || t->echo) return;
    {   /* test aid: RECVX_COOP_KILL="hostframe:slot[:damage][,...]": a lethal hit (or one of that damage) on our enemy in
           that slot, as a shot leaves it (a puppet's goes to the side running it as EV_ENE_HIT, like our weapon's) */
        static const char* kl = (const char*)-1;
        unsigned int f;
        int k, n, dam = 1000, m;
        if (kl == (const char*)-1) kl = getenv("RECVX_COOP_KILL");
        if (kl && sys && plp && t->in_game && sscanf(kl, "%u:%d%n", &f, &k, &n) == 2 && t->clock >= f) {
            if (kl[n] == ':' && sscanf(kl + n + 1, "%d%n", &dam, &m) == 1) n += m + 1;
            if (dam != 1000) host_log("coop: test: hit %d on enemy %d (%s) at host frame %u\n", dam, k, k >= 0 && k < PC_ENE_SLOTS && t->owned[k] != t->host ? "we run it" : "a puppet", t->clock);
            if (k >= 0 && k < PC_ENE_SLOTS && (ene[k].flg & 0x1) && ene[k].mlwP) {
                ene[k].dam[1] += dam;   /* (bhEne_CalcDamage skips joint 0) */
                ene[k].flg |= 0x4;
                ene[k].djnt_no = 1;
                ene[k].wpnr_no = 3;      /* the handgun */
                ene[k].dpx = ene[k].px; ene[k].dpy = ene[k].py + 10; ene[k].dpz = ene[k].pz;
            }
            if (dam == 1000) host_log("coop: test: lethal hit on enemy %d (hp %d, owned %d) at host frame %u\n", k, k >= 0 && k < PC_ENE_SLOTS ? ene[k].hp : 0, k >= 0 && k < PC_ENE_SLOTS ? t->owned[k] : 0, t->clock);
            kl = kl[n] == ',' ? kl + n + 1 : NULL;
        }
    }
    {   /* test aid: RECVX_COOP_ALIVE="hostframe[,...]": logs the room's living, shown enemies (slot:type; not linked parts),
           its ed_flg bits and rm_flg, e.g. that a kill stayed dead, or a kill-count door opened */
        static const char* al = (const char*)-1;
        unsigned int f;
        int k, n, bits = 0;
        char line[600];
        if (al == (const char*)-1) al = getenv("RECVX_COOP_ALIVE");
        if (al && sys && plp && t->in_game && sscanf(al, "%u%n", &f, &n) == 1 && t->clock >= f) {
            int ln = snprintf(line, sizeof(line), "coop: test: in %d/%d at host frame %u, ed_flg", sys->stg_no, sys->rom_no, t->clock);
            for (k = 0; k < 32; k++) bits += __builtin_popcount(sys->ed_flg[k]);
            ln += snprintf(line + ln, sizeof(line) - ln, " %d bits, rm_flg %08x, alive:", bits, sys->rm_flg);
            for (k = 0; k < (int)sys->ewk_n && k < PC_ENE_SLOTS && ln < (int)sizeof(line) - 16; k++)
                if ((ene[k].flg & 0x83) == 0x1 && !(ene[k].stflg & 0x1000000) && ene[k].id >= 1 && ene[k].id <= 30)
                    ln += snprintf(line + ln, sizeof(line) - ln, " %d:%d", k, ene[k].id);
            host_log("%s\n", line);
            al = al[n] == ',' ? al + n + 1 : NULL;
        }
    }
    {   int coop_enemy_waiting(int i);   /* coop_glue.c */
        /* test aid: RECVX_COOP_ENEPOS="hostframe[,...]": the room's enemies (not linked parts) with where they stand, their
           HP and mode, and whether they are dead or hidden (slot:type.model@x,z hp mode0 [D][H]): pc/tools/coop_handin_check.sh
           compares the two sides */
        static const char* ep = (const char*)-1;
        unsigned int f;
        int k, n;
        char line[1200];
        if (ep == (const char*)-1) ep = getenv("RECVX_COOP_ENEPOS");
        if (ep && sys && plp && t->in_game && sscanf(ep, "%u%n", &f, &n) == 1 && t->clock >= f) {
            int ln = snprintf(line, sizeof(line), "coop: test: enemies in %d/%d at host frame %u:", sys->stg_no, sys->rom_no, t->clock);
            for (k = 0; k < (int)sys->ewk_n && k < PC_ENE_SLOTS && ln < (int)sizeof(line) - 48; k++)
                if ((ene[k].flg & 0x81) == 0x1 && ene[k].id >= 1 && ene[k].id <= 30)
                    ln += snprintf(line + ln, sizeof(line) - ln, " %d:%d.%d@%.0f,%.0f,%d,%d%s%s%s", k, ene[k].id, ene[k].mdlver, ene[k].px, ene[k].pz, ene[k].hp, ene[k].mode0,
                                   ene[k].flg & 0x2 ? ",D" : "", (ene[k].stflg & 0x1000000) || coop_enemy_waiting(k) ? ",H" : "",
                                   t->owned[k] ? ",G" : "");   /* (H: hidden, or waiting for the runner's state; G: the guest's game runs it) */
            host_log("%s\n", line);
            ep = ep[n] == ',' ? ep + n + 1 : NULL;
        }
    }
    {   /* test aid: RECVX_COOP_WARP="hostframe:x:z[,...]": both players at once (host frames) */
        static const char* wp = (const char*)-1;
        unsigned int f;
        float x, z;
        int n;
        if (wp == (const char*)-1) wp = getenv("RECVX_COOP_WARP");
        if (wp && sys && plp && t->in_game && sscanf(wp, "%u:%f:%f%n", &f, &x, &z, &n) == 3 && t->clock >= f) {
            t->move(x, z);
            host_log("coop: test: warp to (%.1f %.1f) at host frame %u\n", x, z, t->clock);
            wp = wp[n] == ',' ? wp + n + 1 : NULL;
        }
    }
    {   /* test aid: RECVX_COOP_GOTO="hostframe:x:z:angle[,...]": our player alone to x/z, facing angle (0..65535) */
        static const char* gt = (const char*)-1;
        unsigned int f;
        float x, z;
        int a, n;
        if (gt == (const char*)-1) gt = getenv("RECVX_COOP_GOTO");
        if (gt && sys && plp && t->in_game && sscanf(gt, "%u:%f:%f:%d%n", &f, &x, &z, &a, &n) == 4 && t->clock >= f) {
            t->move(x, z);
            plp->ay = a;
            host_log("coop: test: to (%.1f %.1f) facing %d at host frame %u\n", x, z, a, t->clock);
            gt = gt[n] == ',' ? gt + n + 1 : NULL;
        }
    }
    {   /* test aid: RECVX_COOP_PAD="hostframe:hex[,...]": these game pad bits pressed for one frame (pad.c bhSetPad: 0xB00
           confirm/Cross, 0x10000 the cutscene skip button under every key type) */
        static const char* pd = (const char*)-1;
        unsigned int f, bits;
        int n;
        if (pd == (const char*)-1) pd = getenv("RECVX_COOP_PAD");
        if (pd && sys && sscanf(pd, "%u:%x%n", &f, &bits, &n) == 2 && t->clock >= f) {
            t->pad |= bits;
            host_log("coop: test: pad %x at host frame %u\n", bits, t->clock);
            pd = pd[n] == ',' ? pd + n + 1 : NULL;
        }
    }
    {   /* test aid: RECVX_COOP_VOMIT="hostframe,...": the zombie nearest the partner vomits (en01 MV09, effect 350) */
        static const char* vomit = (const char*)-1;
        unsigned int vf;
        int i, best = -1;
        float bd = 1e9f;
        if (vomit == (const char*)-1) vomit = getenv("RECVX_COOP_VOMIT");
        if (vomit && t->host && sys && t->proxy_ok && sscanf(vomit, "%u", &vf) == 1 && t->frame >= vf) {
            for (i = 0; i < (int)sys->ewk_n && i < PC_ENE_SLOTS; i++) {
                float dx = ene[i].px - t->partner->px, dz = ene[i].pz - t->partner->pz;
                if ((ene[i].flg & 0x1) && ene[i].id == 1 && ene[i].type < 16 && (0xFC33 >> ene[i].type & 1) && ene[i].mode0 == 1 && !(ene[i].flg & 0x80) && ene[i].exp0 && !t->owned[i] && dx * dx + dz * dz < bd) { bd = dx * dx + dz * dz; best = i; }
            }
            if (best >= 0) {
                ene[best].mode1 = 0; ene[best].mode2 = 9; ene[best].mode3 = 0;
                host_log("coop: test: enemy %d vomits (%.0f units from the partner)\n", best, sqrtf(bd));
            }
            vomit = strchr(vomit, ',') ? strchr(vomit, ',') + 1 : NULL;
        }
    }
    {   /* test aid: RECVX_COOP_CRAWL="hostframe,...": the host's walking zombies become crawlers, as en01's
           knock-down makes one with hp below 15 (DG case 3: type 2, or 3 from type 1; idle, then the chase) */
        static const char* cr = (const char*)-1;
        unsigned int cf;
        int i, n = 0;
        if (cr == (const char*)-1) cr = getenv("RECVX_COOP_CRAWL");
        if (cr && t->host && sys && t->in_game && sscanf(cr, "%u", &cf) == 1 && t->frame >= cf) {
            for (i = 0; i < (int)sys->ewk_n && i < PC_ENE_SLOTS; i++) {
                BH_PWORK* ep = &ene[i];
                BH_PWORK* c = (BH_PWORK*)ep->exp1;
                if (!(ep->flg & 0x1) || ep->id != 1 || (ep->flg & 0x80) || !ep->exp0 || ep->mode0 != 1 || ep->hp < 0 || t->owned[i] ||
                    ep->type == 2 || ep->type == 3 || ep->type == 6 || ep->type == 7 || ep->type == 9) continue;
                *(int*)&ep->mode0 = 0x00000101;   /* mode0 1, mode1 1, mode2 0, mode3 0 */
                ep->flg2 &= ~1;
                *(int*)(ep->exp0 + 0x40) &= ~0x240240;
                *(int*)(ep->exp0 + 0x48) = 0;
                *(int*)(ep->exp0 + 0x44) &= ~0x10;   /* (a type 11's "stays put": MV10 would never start the crawl) */
                ep->type = ep->type == 1 ? 3 : 2;
                ep->mtn_add = 0;
                if (ep->hp > 15) ep->hp = 15;
                if (c >= ene && c < ene + PC_ENE_SLOTS) { *(int*)&c->mode0 = *(int*)&ep->mode0; c->mtn_add = 0; c->type = ep->type; }
                n++;
            }
            host_log("coop: test: %d zombies crawl now (host frame %u)\n", n, t->frame);
            cr = strchr(cr, ',') ? strchr(cr, ',') + 1 : NULL;
        }
    }
    {   /* test aid: RECVX_COOP_BUSYLOG=n: every n frames our player's busy flag (flg 4: no inventory, no examining), its
           mode and motion, and whether the status screen is open (st_flg 8) */
        static int bl = -1;
        if (bl < 0) bl = getenv("RECVX_COOP_BUSYLOG") ? atoi(getenv("RECVX_COOP_BUSYLOG")) : 0;
        if (bl > 0 && sys && plp && t->frame % bl == 0) {
            int k, nl = 0, lm = -1;   /* the moth's larvae (en27): how many, the last one's mode2 mode3 and on whom (1xx us, 2xx the partner) */
            for (k = 0; k < (int)sys->ewk_n && k < PC_ENE_SLOTS; k++)
                if ((ene[k].flg & 0x1) && ene[k].id == 27 && ++nl) lm = ene[k].mode2 * 10 + ene[k].mode3 + (ene[k].lkwkp == (unsigned char*)plp ? 100 : ene[k].lkwkp == (unsigned char*)t->partner ? 200 : 0);
            host_log("coop: test: busy %d (flg %x stflg %x) mode %d.%d.%d.%d motion %u own set %d hp %d, status screen %d, in game %d, larvae %d (%d) at host frame %u\n", (plp->flg & 4) != 0, plp->flg, plp->stflg,
                     plp->mode0, plp->mode1, plp->mode2, plp->mode3, plp->mtn_no, plp->mnwP == plp->mnwPb, plp->hp, (sys->st_flg & 0x8) != 0, t->in_game, nl, lm, t->clock);
        }
    }
    {   /* test aid: RECVX_COOP_STALE="hostframe,...": our player's busy flag set as an enemy's attack sets it (flg 0x10004,
           stflg 0x10000), with no enemy to clear it (the hold watchdog's test) */
        static const char* sb = (const char*)-1;
        if (sb == (const char*)-1) sb = getenv("RECVX_COOP_STALE");
        if (sb && sys && plp && t->in_game && plp->mode0 == 1 && t->clock >= (unsigned)atoi(sb)) {
            plp->flg |= 0x10004;
            plp->stflg |= 0x10000;
            host_log("coop: test: our player busy with no enemy at host frame %u\n", t->clock);
            sb = strchr(sb, ',') ? strchr(sb, ',') + 1 : NULL;
        }
    }
    {   /* test aid: RECVX_COOP_LARVA="hostframe,...": a moth's larva on our player (its stflg bit and the en27 enemy, as
           player.c bhCheckMothEgg makes the ones carried into a room); RECVX_COOP_HATCH="hostframe,...": the larvae on our
           player hatch now (en27 BR00 on its floor or timer) */
        static const char* lv = (const char*)-1;
        static const char* ht = (const char*)-1;
        int k;
        if (lv == (const char*)-1) { lv = getenv("RECVX_COOP_LARVA"); ht = getenv("RECVX_COOP_HATCH"); }
        if (lv && sys && plp && t->in_game && t->clock >= (unsigned)atoi(lv)) {
            if (!(plp->stflg & 0x38000000)) { plp->stflg |= 0x8000000; bhCheckMothEgg(); }
            host_log("coop: test: a larva on our player at host frame %u\n", t->clock);
            lv = strchr(lv, ',') ? strchr(lv, ',') + 1 : NULL;
        }
        if (ht && sys && plp && t->in_game && t->clock >= (unsigned)atoi(ht) && !(plp->flg & 0x4) && !(plp->stflg & 0x30)) {   /* (as BR00: not while held) */
            for (k = 0; k < (int)sys->ewk_n && k < PC_ENE_SLOTS; k++)
                if ((ene[k].flg & 0x1) && ene[k].id == 27 && ene[k].lkwkp == (unsigned char*)plp && ene[k].mode0 == 1 && ene[k].mode2 == 0) { ene[k].mode2 = 1; ene[k].mode3 = 0; }
            host_log("coop: test: our larvae hatch at host frame %u\n", t->clock);
            ht = strchr(ht, ',') ? strchr(ht, ',') + 1 : NULL;
        }
    }
    {   /* test aid: RECVX_TRADE_TRACE: the room's enemies every 10 frames (the trade screen's freeze) */
        static int tt = -1;
        if (tt < 0) tt = getenv("RECVX_TRADE_TRACE") != NULL;
        if (tt && sys && plp && t->frame % 10 == 0 && sys->ewk_n) {
            unsigned int h = 0;
            int i, f, n = 0, near_i = 0;
            float nd = 1e9f;
            for (i = 0; i < (int)sys->ewk_n && i < PC_ENE_SLOTS; i++)
                if (ene[i].flg & 0x1) {
                    unsigned int v[5] = { 0, 0, (unsigned int)ene[i].mtn_no, (unsigned int)ene[i].frm_no, (unsigned int)ene[i].ay };
                    float dx = ene[i].px - plp->px, dz = ene[i].pz - plp->pz;
                    memcpy(&v[0], &ene[i].px, 4); memcpy(&v[1], &ene[i].pz, 4);
                    for (n++, f = 0; f < 5; f++) h = h * 31 + v[f];
                    if (ene[i].id == 1 && dx * dx + dz * dz < nd) { nd = dx * dx + dz * dz; near_i = i; }
                }
            if (t->frame % 300 == 0)
                for (i = 0; i < (int)sys->ewk_n && i < PC_ENE_SLOTS; i++)
                    if ((ene[i].flg & 0x1) && ene[i].id < 31) host_log("coop: trade trace: enemy %d id %d at (%.1f %.1f) mode %d\n", i, ene[i].id, ene[i].px, ene[i].pz, ene[i].mode0);
            host_log("coop: trade trace: frozen %d, %d enemies, state hash %08x, nearest zombie %d at (%.2f %.2f) %.1f away, motion %d frame %d, us (%.1f %.1f) hp %d mode %d\n",
                     t->frozen, n, h, near_i, ene[near_i].px, ene[near_i].pz, sqrtf(nd), ene[near_i].mtn_no, ene[near_i].frm_no >> 16, plp->px, plp->pz, plp->hp, plp->mode0);
        }
    }
}
