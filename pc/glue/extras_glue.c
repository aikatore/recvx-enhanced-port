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
 *  - typewriter: a type 3 trigger (attr 0, RM_1080's trigger 6 around it), a wall only on the floor (on a desk the
 *    desk's is there). Its use is RM_1080's script and the trigger's message (attr 0x9c00): with an ink ribbon (or
 *    "Free Typewriter Saves", as bhPlItemCheck) "Use an ink ribbon? Yes / No" (RM_1080 message 0), Yes opens the save
 *    screen (cb_flg 0x200000; the save uses the ribbon as at any typewriter); without one "An old typewriter..."
 *    (message 3). The save made there carries the placement's location text (sys->itm[292..], savestore.h
 *    SAVE_TEXT_ITM), which the save list shows instead of the room's name (save_glue.c); the words are cleared again
 *    whenever no save screen is open, so no other save has them.
 * The item box lid's sound and the typewriter's keys are system sounds that only save rooms' sound banks have: the
 * first extras of those load RM_1080's (snd_sysbank_load).
 * Both triggers are usable only facing the object from its open side (direction bits), so not through a wall.
 * Records are made only with the rule on (off: nothing of the room changes), and switched off when it goes off.
 * Models: the --dev placer's loader and drawing (placer_glue.c: placer_ready, placer_put), drawn after the room's
 * objects and items. Nothing is saved: a room set up again (a load, a door) gets them again.
 */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../src/host/placer.h"
#include "../src/host/savestore.h"
#include "../src/host/cheat.h"
#include "../include/pc_parse.h"
#include <math.h>
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
unsigned int* ItemSearch(unsigned short itemid);   /* sub1.h */
void snd_sysbank_load(void);            /* audio/snd_iop.c */
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
enum { EV_IDLE, EV_ASK, EV_SAY, EV_SAVE };
static int ev, ev_e;                    /* a planter's or typewriter's use (EV_SAVE: the ink ribbon question); ev_e the extra */

static void table(void)
{
    int i;
    const char* add = getenv("RECVX_EXTRAS_LINE");   /* test aid: one more placement, a line as extras.c's */
    if (ex_n >= 0) return;
    for (i = ex_n = 0; i < extras_n() && ex_n < EX_MAX; i++)
        if (extras_get(i, &ex[ex_n], &ex_cases[ex_n], &ex_shape[ex_n])) ex_n++;
        else host_log("extras: placement %d is not readable\n", i);
    if (add && ex_n < EX_MAX && extras_get_line(add, &ex[ex_n], &ex_cases[ex_n], &ex_shape[ex_n])) ex_n++;
}

static int is_tw(int e) { return ex[e].type == placer_type_find("typewriter"); }
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
        flr = bhCheckFloorNum(p->pos[1] + 0.5f);
        y = rom->grand[flr + 2];
        if (is_tw(i) && p->pos[1] - y > 0.5f) nw = 0;   /* on a desk: the desk's wall */
        if (sys->mwal_n + nw > 64 || sys->metc_n >= 64) { host_log("extras: no room for the records of placement %d\n", i); continue; }
        if (s->trig_type == 4 || is_tw(i)) snd_sysbank_load();   /* the lid's sound, the keys */
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
        t->attr = s->trig_attr | extras_facing(p->ang[1], s->use_ay);
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
                 p->pos[0], p->pos[1], p->pos[2], nw ? rom->wal_n + here[here_n].wal[0] : -1, rom->etc_n + here[here_n].etc,
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

/* ---------------------------------------------------------------- the planter (RM_7090 event 60), the typewriter */
/* the texts: the planter's question, "not now", "used" (RM_7090 14..16); the typewriter's question, "no ribbon" (RM_1080 0, 3) */
enum { T_Q, T_N, T_U, T_TW_Q, T_TW_N, T_N_ };
static const struct { short room, mes; } tsrc[T_N_] = { { 7090, 14 }, { 7090, 15 }, { 7090, 16 }, { 1080, 0 }, { 1080, 3 } };
static unsigned int mtxt[T_N_][128];    /* as the game shows them (u16, 0xFFFF end) */
static unsigned short us[T_N_][256];    /* the disc's (US) ones */
static int us_ok;

static int us_texts(void)
{
    unsigned char* scr;
    int len = 0, cap, i, room = 0;
    const unsigned char* rf = NULL;
    unsigned int at = 0, n;
    if (us_ok) return us_ok > 0;
    us_ok = -1;
    for (i = 0; i < T_N_; i++) {
        if (tsrc[i].room != room) {
            room = tsrc[i].room;
            if (!(rf = actor_room_file(room, &len, &scr, &cap)) || !pc_range_ok(pc_rd32(rf + 16), sizeof(ROM_WORK), (unsigned)len)) return 0;
            at = (unsigned int)(uintptr_t)((const ROM_WORK*)(rf + pc_rd32(rf + 16)))->mesp;
            if (at >= (unsigned)len) return 0;
        }
        if (!(n = pc_mes_at(rf + at, (unsigned)len - at, tsrc[i].mes)) || n > 256) return 0;
        memcpy(us[i], rf + at + pc_rd32(rf + at + 4 + 4 * tsrc[i].mes), 2 * n);
    }
    us_ok = 1;
    return 1;
}

static const unsigned short* text(int k)
{
    unsigned int n;
    char file[16];
    const unsigned short* t;
    snprintf(file, sizeof(file), "RM_%04d.RDX", tsrc[k].room);
    t = pc_lang_room_text(file, tsrc[k].mes, &n);
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
    if (plp->stflg & 0x80000) { say(T_Q); ev = EV_ASK; }   /* poisoned: "Use blue herb?" */
    else { say(T_N); ev = EV_SAY; }
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

/* RM_1080: the trigger's message (hitchk.c bhCheckExmAtari, attr 0x8000) and the script's PlItemCheck 31 */
static void typewriter_start(int e)
{
    int ribbon = pc_no_ink_ribbon() || ItemSearch(31);
    say(ribbon ? T_TW_Q : T_TW_N);
    sys->pad_onb = sys->pad_on; sys->pad_psb = sys->pad_ps; sys->pad_oldb = sys->pad_old;
    sys->pad_ps = 0;
    sys->cb_flg |= 0x20;
    sys->st_flg |= 0x2000 | 0x4;
    ev = ribbon ? EV_SAVE : EV_IDLE;
    ev_e = e;
    host_log("extras: typewriter in %d/%d, %s\n", sys->stg_no, sys->rom_no, ribbon ? "asked" : "no ink ribbon");
}

/* the script's answer: Yes (mes_sel 0) opens the save screen; the save gets the placement's location text */
static void typewriter_frame(void)
{
    unsigned char* w = (unsigned char*)&sys->itm[SAVE_TEXT_ITM];
    if (!((sys->cb_flg & 0x1000) && (sys->st_flg & 0x4000))) return;
    sys->cb_flg &= ~0x1000;
    ev = EV_IDLE;
    if (sys->mes_sel != 0) return;
    memset(w, 0, SAVE_TEXT_BYTES);
    if (ex[ev_e].text[0]) { memcpy(w, "LOC=", 4); memcpy(w + 4, ex[ev_e].text, strlen(ex[ev_e].text)); }
    sys->cb_flg |= 0x200000;
    host_log("extras: typewriter: save screen (location text \"%s\")\n", ex[ev_e].text);
}

static void planter_frame(void)
{
    if (ev == EV_ASK && (sys->cb_flg & 0x1000) && (sys->st_flg & 0x4000)) {   /* the choice was made (message.c) */
        if (sys->mes_sel == 0) {
            plp->stflg &= ~0x80000;
            say(T_U);
            ev = EV_SAY;
            host_log("extras: blue herb used: cured\n");
        } else planter_end();
    } else if (ev == EV_SAY && (sys->cb_flg & 0x2000)) planter_end();   /* the message closed */
}

/* ---------------------------------------------------------------- test aid */
static float goto_xz[2];
static int goto_ay, goto_on;   /* goto: the player put there after the frame (extras_load; in bhControlEvent the step after moves it back) */
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
        float spot[4], r[4], far = 0, th = sh->use_ay * 6.2831853f / 65536.0f, back;
        sscanf(s0, ":%*d:%f", &far);
        back = (sh->trig_type == 4 ? 2.0f : 4.5f) + far;   /* the point the trigger tests is that far ahead (bhCheckExmAtari) */
        spot[0] = spot[1] = (sh->trig[0] + sh->trig[1]) * 0.5f + back * sinf(th);   /* the trigger's middle, stepped back */
        spot[2] = spot[3] = (sh->trig[2] + sh->trig[3]) * 0.5f + back * cosf(th);
        extras_rect(p, spot, r);
        plp->flr_no = (char)bhCheckFloorNum(p->pos[1] + 0.5f);   /* (on its floor: an entry upstairs) */
        plp->py = rom->grand[plp->flr_no + 2];
        goto_xz[0] = r[0]; goto_xz[1] = r[2]; goto_ay = (p->ang[1] + sh->use_ay) & 0xFFFF; goto_on = 1;   /* (after the frame: extras_load) */
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
                if (is_tw(here[i].e)) typewriter_start(here[i].e);
                else planter_start();
            }
        }
    }
    if (ev == EV_SAVE) typewriter_frame();
    else if (ev != EV_IDLE) planter_frame();
    if (!(sys->cb_flg & 0x200000) && sys->itm[SAVE_TEXT_ITM]) memset(&sys->itm[SAVE_TEXT_ITM], 0, SAVE_TEXT_BYTES);   /* no save screen */
    test_aid();
}

/* ---------------------------------------------------------------- drawing (placer_draw) */
int extras_here(void) { return made && here_n > 0 && here_gen == enemy_room_gen && pc_extra_objects(); }

/* placer_frame (cheat_frame, after the frame): the models of this room's extras and the planter's texts, loaded
   the first time they are needed (as the placer loads, outside the frame's drawing) */
void extras_load(void)
{
    int i;
    if (goto_on && plp) { cheat_put_player(goto_xz[0], goto_xz[1], goto_ay); goto_on = 0; }
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
