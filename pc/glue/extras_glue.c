/* "Extra Item Boxes and Herbs" (GAMEPLAY setting, ini extra_objects; co-op: the host's, OPT2_EXTRA_OBJECTS;
 * docs/DEV_PLACEMENTS.md "Extra item boxes and herbs"): item boxes and blue herb planters in the rooms of
 * pc/src/host/extras.c, working as the game's own. Game thread.
 *
 * At each room's set-up (room.c bhFinishRoom, via enemyopt_glue.c) the room's extras get records of the game's own
 * kinds after the room's (the movable objects' extra walls and triggers, sys->mwalp / sys->metcp), so the player,
 * enemies and shots collide with them and the action button finds them as any trigger:
 *  - item box: a box wall (RM_1080's, height 9) and in front a type 4 trigger with attr 0x2 whose prm0 is a spare
 *    object slot past the room's objects (sys->obwp[rom->obj_n + k]): hitchk.c bhCheckExmAtari then holds the player
 *    and sets that slot's type 100, and every frame (pc_extras_scd1) objitm.c bhObjItmBox runs on it as on a real lid:
 *    the lid swings open with its sound, the item box screen opens (cb_flg 0x40000), the player is let go. The drawn
 *    lid takes the slot's angle. No spare slot (a room with 64 objects): prm0 0xFF, the screen opens without the lid.
 *  - planter: a box wall (height 2) and a type 3 trigger (attr 0, as RM_7090's), whose use runs here what the
 *    retail rooms' script event does (RM_7090 event 60, RM_6030 event 30): poisoned (plp->stflg 0x80000) "Use blue
 *    herb?" Yes / No, Yes clears the poison and says "You've used the blue herb."; not poisoned "... I don't need to
 *    use this now." Nothing is counted: it never runs out. The texts are RM_7090's messages 14..16 in the game's
 *    language (lang_glue.c pc_lang_room_text, the imported language's room messages), else the disc's (US).
 * Both triggers are usable only facing the object from its open side (direction bits), so not through a wall.
 * Records are made only with the rule on (off: nothing of the room changes), and switched off when it goes off.
 * Models: the --dev placer's loader and drawing (placer_glue.c: placer_ready, placer_put), drawn after the room's
 * objects and items. Nothing is saved: a room set up again (a load, a door) gets them again.
 */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../src/host/placer.h"
#include "../include/pc_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void host_log(const char* fmt, ...);
const unsigned char* actor_room_file(int rm, int* len, unsigned char** scratch, int* cap);   /* actor_glue.c */
const unsigned short* pc_lang_room_text(const char* room, unsigned int i, unsigned int* len);   /* lang_glue.c */
int pc_extra_objects(void);             /* coop_glue.c: the setting, in co-op the host's */
void coop_test_press(unsigned int bits);   /* coop_glue.c: test aid, pad bits for the next read */
int bhCheckFloorNum(float py);          /* pwksub.h */
void bhObjItmBox(O_WRK* op);            /* objitm.h */
extern int enemy_room_gen;              /* enemyopt_glue.c: room set-ups so far */
uint64_t gs_frame_count(void);

#define OBW_N (39936 / (int)sizeof(O_WRK))   /* sys->obwp's slots (objitm.c bhInitObjItm) */
enum { EX_MAX = 32, HERE_MAX = 8, WAL_MAX = 3 };

static Placement ex[EX_MAX];
static unsigned ex_cases[EX_MAX];
static const ExtraShape* ex_shape[EX_MAX];
static int ex_n = -1;

static struct { int e, nwal, wal[WAL_MAX], etc, slot; } here[HERE_MAX];
static int here_n, here_gen = -1, made;   /* made: this room's records exist */
enum { EV_IDLE, EV_ASK, EV_SAY };
static int ev;                          /* a planter's use */

static void table(void)
{
    int i;
    if (ex_n >= 0) return;
    for (i = ex_n = 0; i < extras_n() && ex_n < EX_MAX; i++)
        if (extras_get(i, &ex[ex_n], &ex_cases[ex_n], &ex_shape[ex_n])) ex_n++;
        else host_log("extras: placement %d is not readable\n", i);
}

static ATR_WORK* wal(int k) { return &sys->mwalp[k]; }
static ATR_WORK* etc(int k) { return &sys->metcp[k]; }

/* the room's records of the extras that stand in this room case */
static void make(void)
{
    int i, j, k, nbox = 0;
    float r[4];
    table();
    made = 1;
    here_n = 0;
    for (i = 0; i < ex_n && here_n < HERE_MAX; i++) {
        const Placement* p = &ex[i];
        const ExtraShape* s = ex_shape[i];
        int slant = extras_slanted(p->ang[1]), nw = slant ? WAL_MAX : 1, flr;
        float y;
        ATR_WORK* t;
        if (p->stg != sys->stg_no || p->rom != sys->rom_no || !(ex_cases[i] >> (sys->rcase & 15) & 1)) continue;
        if (sys->mwal_n + nw > 64 || sys->metc_n >= 64) { host_log("extras: no room for the records of placement %d\n", i); continue; }
        flr = bhCheckFloorNum(p->pos[1] + 0.5f);
        y = rom->grand[flr + 2];
        here[here_n].e = i;
        here[here_n].nwal = nw;
        for (j = 0; j < nw; j++) {   /* a box; turned off the axes, a row of cylinders along it */
            ATR_WORK* w = wal(here[here_n].wal[j] = sys->mwal_n++);
            memset(w, 0, sizeof(*w));
            w->flg = 0x1;
            w->flr_no = (char)flr;
            w->py = y;
            w->h = s->wall_h;
            if (!slant) {
                extras_rect(p, s->wall, r);
                w->type = 0;
                w->px = r[0]; w->pz = r[2]; w->w = r[1] - r[0]; w->d = r[3] - r[2];
            } else {
                float depth = s->wall[3] - s->wall[2], len = s->wall[1] - s->wall[0], c[4];
                c[0] = c[1] = s->wall[0] + depth * 0.5f + (len - depth) * j / (nw - 1);
                c[2] = c[3] = (s->wall[2] + s->wall[3]) * 0.5f;
                extras_rect(p, c, r);
                w->type = 2;
                w->px = r[0]; w->pz = r[2]; w->w = depth * 0.5f;
            }
        }
        t = etc(here[here_n].etc = sys->metc_n++);
        memset(t, 0, sizeof(*t));
        extras_rect(p, s->trig, r);
        t->flg = 0x1;
        t->type = (unsigned char)s->trig_type;
        t->attr = s->trig_attr | extras_facing(p->ang[1], s->use);
        t->flr_no = (char)flr;
        t->px = r[0]; t->py = y; t->pz = r[2]; t->w = r[1] - r[0]; t->d = r[3] - r[2];
        here[here_n].slot = -1;
        if (s->trig_type == 4) {   /* the lid's object slot */
            k = rom->obj_n + nbox++;
            t->prm0 = 0xFF;
            if (k >= 4 && k < OBW_N && k < 255) {
                memset(&sys->obwp[k], 0, sizeof(O_WRK));
                sys->obwp[k].flg = 0x1;
                t->prm0 = (unsigned char)(here[here_n].slot = k);
            }
        }
        host_log("extras: %s in %d/%d case %d at %.1f %.1f %.1f: wall %d, trigger %d%s\n", s->key, sys->stg_no, sys->rom_no, sys->rcase,
                 p->pos[0], p->pos[1], p->pos[2], rom->wal_n + here[here_n].wal[0], rom->etc_n + here[here_n].etc,
                 s->trig_type == 4 && here[here_n].slot < 0 ? " (no lid slot)" : "");
        here_n++;
    }
}

void extras_room(void)
{
    here_n = made = 0;
    ev = EV_IDLE;   /* (a room set up again ends a planter's use: bhFinishRoom set sp_flg) */
    here_gen = enemy_room_gen;
    if (sys && rom && pc_extra_objects()) make();
}

/* ---------------------------------------------------------------- the planter (RM_7090 event 60) */
static unsigned int mtxt[3][128];       /* the three texts as the game shows them (u16, 0xFFFF end) */
static unsigned short us[3][256];       /* the disc's (US) ones */
static int us_ok;

static int us_texts(void)
{
    unsigned char* scr;
    int len, cap, i;
    const unsigned char* rf;
    if (us_ok) return us_ok > 0;
    us_ok = -1;
    if (!(rf = actor_room_file(7090, &len, &scr, &cap)) || !pc_range_ok(pc_rd32(rf + 16), sizeof(ROM_WORK), (unsigned)len)) return 0;
    {
        const ROM_WORK* rw = (const ROM_WORK*)(rf + pc_rd32(rf + 16));
        unsigned int at = (unsigned int)(uintptr_t)rw->mesp, n;
        if (at >= (unsigned)len) return 0;
        for (i = 0; i < 3; i++) {
            if (!(n = pc_mes_at(rf + at, (unsigned)len - at, 14 + i)) || n > 256) return 0;
            memcpy(us[i], rf + at + pc_rd32(rf + at + 4 + 4 * (14 + i)), 2 * n);
        }
    }
    us_ok = 1;
    return 1;
}

static const unsigned short* text(int k)
{
    unsigned int n;
    const unsigned short* t = pc_lang_room_text("RM_7090.RDX", 14 + k, &n);
    if (!t || n > 256) { t = us[k]; n = 256; }
    memcpy(mtxt[k], t, 2 * n);
    ((unsigned short*)mtxt[k])[255] = 0xFFFF;
    return (const unsigned short*)mtxt[k];
}

/* message.c bhSetMessage with our text */
static void say(int k)
{
    sys->mes_idx = 1;
    sys->mes_dp = (unsigned int*)text(k);
    sys->mes_ct = sys->mes_tim = sys->mes_fls = sys->mes_sel = 0;
    sys->st_flg |= 0x200;
    sys->st_flg &= ~(0x80000 | 0x8000 | 0x4000 | 0x1000 | 0x400 | 0x800);
    sys->cb_flg &= ~(0x20000000 | 0x2000 | 0x1000);
}

static void planter_start(void)
{
    sys->sp_flg &= ~(0x1 | 0x2 | 0x40);   /* sp_flg bits 31, 30, 25 */
    sys->st_flg |= 0x4;                   /* st_flg bit 29 */
    sys->cb_flg |= 0x4 | 0x40;            /* CineSet 3 */
    if (plp->stflg & 0x80000) { say(0); ev = EV_ASK; }   /* poisoned: "Use blue herb?" */
    else { say(1); ev = EV_SAY; }
    host_log("extras: blue herb planter in %d/%d, %s\n", sys->stg_no, sys->rom_no, ev == EV_ASK ? "poisoned: asked" : "not poisoned");
}

static void planter_end(void)
{
    if (sys->cb_flg & 0x40) sys->cine_ap = sys->cine_an = 0;   /* CineSet 1 */
    sys->cb_flg &= ~0x4;
    sys->st_flg &= ~0x4;
    sys->sp_flg = 0xFFFFFFFFu;
    ev = EV_IDLE;
}

static void planter_frame(void)
{
    if (ev == EV_ASK && (sys->cb_flg & 0x1000) && (sys->st_flg & 0x4000)) {   /* the choice was made (message.c) */
        if (sys->mes_sel == 0) {
            plp->stflg &= ~0x80000;
            say(2);
            ev = EV_SAY;
            host_log("extras: blue herb used: cured\n");
        } else planter_end();
    } else if (ev == EV_SAY && (sys->cb_flg & 0x2000)) planter_end();   /* the message closed */
}

/* ---------------------------------------------------------------- test aid */
/* RECVX_EXTRAS="frame:op[:arg],...", game frames: goto:k[:d] (stand at extra k of this room, facing it, d units
   farther), poison (poison:2 Nosferatu's, stflg 0x200000),
   pad:hex (those pad bits pressed for one frame, e.g. b00 the action button / confirm, 8 right), hold:hex:n (for n
   frames, e.g. 21 forward), ok (confirm, only while a message shows), log */
static void test_aid(void)
{
    static const char* s = (const char*)-1;
    static int clock, hold_n;
    static unsigned hold;
    const char* s0;
    char op[16];
    int f, n = 0, a = 0, e, k;
    if (s == (const char*)-1) s = getenv("RECVX_EXTRAS");
    if (hold_n > 0 && hold_n--) coop_test_press(hold);
    if (!s || !*s) return;
    clock++;
    if (sscanf(s, "%d:%15[a-z]%n", &f, op, &n) != 2 || clock < f) return;
    s += n;
    s0 = s;
    if (!strcmp(op, "pad")) sscanf(s, ":%x", (unsigned*)&a);
    else if (!strcmp(op, "hold")) sscanf(s, ":%x:%d", &hold, &hold_n);
    else sscanf(s, ":%d", &a);
    s = strchr(s, ',') ? strchr(s, ',') + 1 : NULL;
    for (e = 0, k = a; e < (table(), ex_n); e++)   /* goto's: this room's k-th placement (also with the rule off) */
        if (ex[e].stg == sys->stg_no && ex[e].rom == sys->rom_no && (ex_cases[e] >> (sys->rcase & 15) & 1) && !k--) break;
    if (!strcmp(op, "goto") && a >= 0 && e < ex_n) {
        const Placement* p = &ex[e];
        const ExtraShape* sh = ex_shape[e];
        float spot[4], r[4], far = 0;
        sscanf(s0, ":%*d:%f", &far);
        spot[0] = spot[1] = 0;
        spot[2] = spot[3] = sh->use > 0 ? sh->wall[3] + 2.5f + far : sh->wall[2] - 2.0f - far;
        extras_rect(p, spot, r);
        plp->px = r[0]; plp->pz = r[2];
        plp->flr_no = (char)bhCheckFloorNum(p->pos[1] + 0.5f);   /* (on its floor: an entry upstairs) */
        plp->py = rom->grand[plp->flr_no + 2];
        plp->ay = (p->ang[1] + (sh->use > 0 ? 0 : 0x8000)) & 0xFFFF;
    } else if (!strcmp(op, "poison")) plp->stflg |= a == 2 ? 0x200000 : 0x80000;
    else if (!strcmp(op, "pad")) coop_test_press((unsigned)a);
    else if (!strcmp(op, "ok") && (sys->st_flg & 0x200)) coop_test_press(0xB00);   /* only a message's confirm */
    printf("test: extras %s %d at game frame %d (presented %u) in %d/%d case %d: %d here%s, player %.1f %.1f %.1f ay %d, %spoisoned, box screen %d, planter %d (gm %08x cb %08x st %08x sp %08x)\n", op, a,
           clock, (unsigned)gs_frame_count(), sys->stg_no, sys->rom_no, sys->rcase, here_n, made ? "" : " (none made)", plp->px, plp->py, plp->pz, plp->ay,
           plp->stflg & 0x80000 ? "" : "not ", (sys->cb_flg & 0x40000) != 0, ev, sys->gm_flg, sys->cb_flg, sys->st_flg, sys->sp_flg);
    fflush(stdout);
}

/* ---------------------------------------------------------------- every game frame (event.c bhControlEvent, before scd1) */
void pc_extras_scd1(void)
{
    int on, i, j;
    if (!sys || !rom || !plp) return;
    on = pc_extra_objects();
    if (here_gen != enemy_room_gen) { here_n = made = 0; here_gen = enemy_room_gen; }
    if (on && !made) make();   /* (the rule came on in this room) */
    if (made) {
        for (i = 0; i < here_n; i++) {
            for (j = 0; j < here[i].nwal; j++) wal(here[i].wal[j])->flg = on ? 0x1 : 0;
            etc(here[i].etc)->flg = on ? 0x1 : 0;
            if (here[i].slot >= 0 && sys->obwp[here[i].slot].type == 100) bhObjItmBox(&sys->obwp[here[i].slot]);   /* the lid */
            if (on && ex_shape[here[i].e]->trig_type == 3 && ev == EV_IDLE && (sys->cb_flg & 0x100) &&
                sys->etc_idx == (unsigned)(rom->etc_n + here[i].etc) && us_texts()) {   /* the player used it */
                sys->cb_flg &= ~0x100;
                planter_start();
            }
        }
    }
    if (ev != EV_IDLE) planter_frame();
    test_aid();
}

/* ---------------------------------------------------------------- drawing (placer_draw) */
int extras_here(void) { return made && here_n > 0 && here_gen == enemy_room_gen && pc_extra_objects(); }

/* placer_frame (cheat_frame, after the frame): the models of this room's extras and the planter's texts, loaded
   the first time they are needed (as the placer loads, outside the frame's drawing) */
void extras_load(void)
{
    int i;
    if (!extras_here()) return;
    for (i = 0; i < here_n; i++) {
        if (ex_shape[here[i].e]->trig_type == 3) us_texts();
        placer_ready(ex[here[i].e].type);
    }
}

int extras_draw(void)
{
    int i, n = 0;
    for (i = 0; i < here_n; i++) {
        const Placement* p = &ex[here[i].e];
        placer_put(p->type, p->pos, p->ang, here[i].slot >= 0 ? sys->obwp[here[i].slot].ax : 0);
        n++;
    }
    return n;
}

int extras_covers(const Placement* q)
{
    int i;
    for (i = 0; i < here_n; i++) {
        const Placement* p = &ex[here[i].e];
        if (p->type == q->type && (p->pos[0] - q->pos[0]) * (p->pos[0] - q->pos[0]) + (p->pos[2] - q->pos[2]) * (p->pos[2] - q->pos[2]) < 0.01f) return 1;
    }
    return 0;
}
