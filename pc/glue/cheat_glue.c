/* F2 cheat menu: applied on the game thread once per frame (at the frame hand-off), so the
   game never sees its state change mid-frame. */
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/room.h"
#include "../../decomp/include/ps2/veronica/prog/player.h"
#include "../../decomp/include/ps2/veronica/prog/effect.h"
#include "../../decomp/include/ps2/veronica/prog/sub1.h"
#include "../../decomp/include/ps2/veronica/prog/pwksub.h"
#include "../../decomp/include/ps2/veronica/prog/item.h"
#include "../../decomp/include/ps2/veronica/prog/hitchk.h"
void bhSetMessage(int mes_typ, int mes_idx);   /* message.h (its macros.h clashes with this file's headers) */
#include "../src/host/cheat.h"
#include "../src/host/coop.h"
#include "../src/net/coop_proto.h"
#include "pc_rules.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

CheatState g_cheat = { .give_count = 1 };

/* infinite ammo: every shot goes through bhCheckBullet/bhCountBullet (weapon.c), which then take
   the path of the game's own unlimited weapons (no count check, no decrement) */
int pc_inf_ammo(void) { return g_cheat.inf_ammo; }

/* noclip: player.c bhControlPlayer skips the enemy push (flg 0x8) and the wall check (0x10,
   which also does slopes) for the returned flags. Walking never changes the height otherwise
   (no gravity; height comes from the room's floor levels), so the player stays at its height.
   Mode 2 also moves up/down (pad_noclip_vertical) and keeps the floor number (triggers, walls
   per floor) in step. Turning noclip off after flying puts the player back on the floor level
   it took off from (the height levels say nothing about where there is ground, so a level
   between is no safer); walls push it out as usual - in the room it took off in (another room's
   level means nothing here: there it stays where it is). Only our player: co-op runs the partner
   through plp too. */
int pad_noclip_vertical(void);
int pc_in_play(const BH_PWORK* p, int scope);   /* reload_glue.c */
unsigned int pc_noclip(void)
{
    static int flew, base, room;   /* base: floor number at take-off, in that room */
    int mode = g_cheat.noclip, v;
    if (plp != &ply) return 0;
    if (mode == 2 && plp->mode0 == 1 && (v = pad_noclip_vertical())) {
        if (!flew) { flew = 1; base = plp->flr_no; room = coop_room_key(); }
        plp->py += v;   /* a unit per frame; the player is about 17 tall */
        bhSetFloorNum(plp);
    }
    if (!mode && flew) {
        if (room == coop_room_key()) {
            plp->py = rom->grand[base + 2];
            bhSetFloorNum(plp);
        }
        flew = 0;
    }
    return mode ? 0x18 : 0;
}

/* Unlock all: the game opens a lock in one of two ways, both kept, with the key taken as carried only there.
   - A use spot is a floor record of type 0 listing up to four items (prm0..3): standing in it, the item screen's
     Use (sub1.c ItemUse/Use_05) sets sb_id and cb_flg 0x400, and the room script, which runs on in the item
     screen (st_flg 0x8), tests "on floor N, item X used" and plays the unlock. Here the examine press in such a
     spot uses the spot's key item instead (pc_unlock_examine): the next scd1 sees exactly that state. When the
     script does nothing with it (a spot already done), the press examines as usual, one frame later.
   - A door with a question ("Will you use the Silver Key?") is chosen by the script's PlItemCheck: scd0 sets
     the door's message by it, scd1 opens the door on "Yes" when it still holds. pc_unlock_held passes such a
     check for a key item (itemdata type 0x40: no weapon, ammo, herb, ribbon or lighter) that has a use spot in
     this room, as pc_rules.h pc_unlock_rule says.
   No item is added; the scripts' PlItemLost/PlItemLostEx of a key the player does not have find nothing. */
void host_log(const char* fmt, ...);
int Event_exec(unsigned int task_level, unsigned int evt_id);   /* event.c */
static int key_item(int id) { return id > 0 && id < 156 && (itemdata[id].type & 0x40); }
static int carried(int id)
{
    for (int c = sys->ply_id * 16 + 1; c < sys->ply_id * 16 + 12; c++)   /* as bhPlItemCheck */
        if ((unsigned char)(sys->itm[c] >> 16) == id) return 1;
    return 0;
}

static int has_spot(int item)
{
    for (int i = 0; i < rom->flr_n; i++) {
        const ATR_WORK* z = &rom->flrp[i];
        if (z->type == 0 && (z->prm0 == item || z->prm1 == item || z->prm2 == item || z->prm3 == item)) return 1;
    }
    return 0;
}

/* Switched in a room, the cheat changes at once what the entry script (scd0) decided by such a check,
   instead of at the next entry: the check's block (the IfelCk it is a test of) is noted as scd0 runs
   it, with what its parts write. Blocks whose then-part and optional else part only set rm_flg bits
   and the room's trigger and floor records (EtcAtariSet(2), FlrAtariSet(2)) qualify: the door's
   message and the bit scd1 asks for before it opens the door. Nothing else of scd0 runs again
   (enemies, items, cutscenes, music). On a switch the block's tests are evaluated again (the game's
   own handlers, in place); when they now choose the other part, its writes are made, and what only
   the part taken before wrote goes back to the value from before the block. A block whose records
   changed since (its door was opened meanwhile) is left as it is. */
#define SITES 16
#define SITE_W 12
enum { W_RM, W_ETC, W_ETC_ON, W_FLR, W_FLR_ON };
typedef struct { unsigned int attr, prm; unsigned char type; } Val;
typedef struct {
    unsigned int ifel, then0, els, end;   /* offsets in the event block: IfelCk, its then-part, the else part (0: none), the end */
    int part, n;                          /* the part that ran last (1 then, 0 else; -1: left alone) */
    struct { unsigned char kind, ix, set[2]; Val pre, val[2]; } w[SITE_W];
} Site;
static Site site[SITES];
static int nsite, recording, evaluating, unlock_applied;

extern unsigned char* bhScePtr;   /* event.c */
extern unsigned int* G_Sp;
extern int bhIfelFlg;
extern unsigned int (*bhScenarioJmpT[256])();

static void note_site(int item);
int pc_unlock_held(int item)
{
    const unsigned int* t = (const unsigned int*)rom->evtp;
    int part, ok;
    if (!key_item(item)) return 0;
    part = pc_script_part(t, (unsigned int)(bhScePtr - (const unsigned char*)t));
    ok = pc_unlock_rule(1, part, *bhScePtr, sys->stg_no, sys->rom_no, has_spot(item));
    if (ok && part == 0 && recording && !evaluating) note_site(item);
    return ok && g_cheat.unlock;
}

static ATR_WORK* atr(int kind, int ix)
{
    int etc = kind == W_ETC || kind == W_ETC_ON;
    if (kind == W_RM || ix >= (etc ? rom->etc_n : rom->flr_n)) return NULL;
    return etc ? &rom->etcp[ix] : &rom->flrp[ix];
}
static Val get(int kind, int ix)
{
    ATR_WORK* a = atr(kind, ix);
    Val v = { 0, 0, 0 };
    if (kind == W_RM) v.attr = sys->rm_flg >> (31 - ix) & 1;
    else if (kind == W_ETC_ON || kind == W_FLR_ON) v.attr = a->flg & 1;
    else { v.attr = a->attr; v.type = a->type; v.prm = a->prm0 | a->prm1 << 8 | a->prm2 << 16 | (unsigned int)a->prm3 << 24; }
    return v;
}
static void put(int kind, int ix, Val v)
{
    ATR_WORK* a = atr(kind, ix);
    if (kind == W_RM) sys->rm_flg = (sys->rm_flg & ~(0x80000000u >> ix)) | v.attr << (31 - ix);
    else if (kind == W_ETC_ON || kind == W_FLR_ON) a->flg = (a->flg & ~1) | v.attr;
    else { a->attr = v.attr; a->type = v.type; a->prm0 = v.prm; a->prm1 = v.prm >> 8; a->prm2 = v.prm >> 16; a->prm3 = v.prm >> 24; }
}
static int same(Val a, Val b) { return a.attr == b.attr && a.prm == b.prm && a.type == b.type; }

/* what part k (1 then, 0 else) of s writes from d + o to d + e; 0: an instruction that does not qualify */
static int parse_part(Site* s, const unsigned char* d, unsigned int o, unsigned int e, int k)
{
    while (o < e) {
        const unsigned char* p = d + o;
        int len = coop_insn_len_raw(p), kind, ix, j;
        Val v = { 0, 0, 0 };
        if (len <= 0 || o + len > e) return 0;
        switch (p[0]) {
        case 5:   /* Set rm_flg bit: set (0) or clear (1) */
            if (p[1] != 4 || (p[2] | p[3] << 8) > 31 || p[5] > 1) return 0;
            kind = W_RM; ix = p[2]; v.attr = !p[5];
            break;
        case 11: case 12:   /* EtcAtariSet, FlrAtariSet: on (0) or off */
            kind = p[0] == 11 ? W_ETC_ON : W_FLR_ON; ix = p[1]; v.attr = !p[2];
            break;
        case 37: case 124: {   /* EtcAtariSet2, FlrAtariSet2: type, attr, prm */
            AtrSet a;
            coop_atr_set(p, &a);
            kind = p[0] == 37 ? W_ETC : W_FLR; ix = a.ix;
            v.attr = a.attr; v.type = a.type; v.prm = a.prm[0] | a.prm[1] << 8 | a.prm[2] << 16 | (unsigned int)a.prm[3] << 24;
            break;
        }
        default: return 0;
        }
        if (kind != W_RM && !atr(kind, ix)) return 0;
        for (j = 0; j < s->n && (s->w[j].kind != kind || s->w[j].ix != ix); j++) {}
        if (j == s->n) {
            if (s->n == SITE_W) return 0;
            s->n++;
            s->w[j].kind = kind; s->w[j].ix = ix;
            s->w[j].pre = get(kind, ix);   /* (as before the block: noted at its tests) */
        }
        s->w[j].set[k] = 1;
        s->w[j].val[k] = v;
        o += len;
    }
    return 1;
}

/* the block's tests hold now */
static int tests_hold(const Site* s)
{
    unsigned char *keep = bhScePtr, *d = (unsigned char*)rom->evtp;
    unsigned int* sp = G_Sp;
    int fl = bhIfelFlg, ok = 1;
    evaluating = 1;
    for (bhScePtr = d + s->ifel + 2; ok && bhScePtr < d + s->then0;) ok = bhScenarioJmpT[*bhScePtr]() != 0;
    evaluating = 0;
    bhScePtr = keep; G_Sp = sp; bhIfelFlg = fl;
    return ok;
}

/* scd0's check of `item` at bhScePtr - 2 (pc_unlock_held): its block. IfelCk n at o jumps to o + 2 + n when a
   test fails (G_Sp - 1): behind its Endif, or into the else part behind an ElseCk m, which ends m further */
static void note_site(int item)
{
    unsigned char* d = (unsigned char*)rom->evtp;
    unsigned int q = (unsigned int)(bhScePtr - 2 - d), f = (unsigned int)((unsigned char*)(G_Sp - 1) - d), o, p;
    Site* s = &site[nsite];
    int i;
    if (nsite == SITES || f <= q + 2 || f - q > 0x200) return;
    for (i = 0; i < nsite; i++)
        if (site[i].ifel < q && site[i].then0 > q) return;   /* (the block's second check) */
    for (o = q; o >= 2 && o + 64 > q; o--) {   /* its IfelCk: tests only from there to the check */
        if (d[o - 2] != 1 || o + d[o - 1] != f) continue;
        for (p = o; p < q && coop_op_cond(d[p]) && coop_insn_len_raw(d + p) > 0; p += coop_insn_len_raw(d + p)) {}
        if (p == q) break;
    }
    if (o < 2 || o + 64 <= q) return;
    memset(s, 0, sizeof(*s));
    s->ifel = o - 2;
    for (p = q; coop_op_cond(d[p]) && coop_insn_len_raw(d + p) > 0; p += coop_insn_len_raw(d + p)) {}
    s->then0 = p;
    if (d[f - 2] == 2) { s->els = f; s->end = f - 2 + d[f - 1]; }
    else if (d[f - 2] == 3 && !d[f - 1]) s->end = f;
    else return;
    if (s->then0 > f - 2 || !parse_part(s, d, s->then0, f - 2, 1) || (s->els && !parse_part(s, d, s->els, s->end, 0))) {
        host_log("unlock: %d/%d entry script block %04x (item %d) does more than set records: not switched in the room\n",
                 sys->stg_no, sys->rom_no, s->ifel, item);
        return;
    }
    s->part = tests_hold(s);
    nsite++;
}

/* event.c bhInitEvent: a room's entry script is about to run */
void pc_unlock_room(void)
{
    nsite = 0;
    recording = 1;
    unlock_applied = g_cheat.unlock;
}

/* at the hand-off: the cheat was switched in this room */
static void unlock_switch(void)
{
    int i, j, want;
    if (g_cheat.unlock == unlock_applied || recording) return;
    if (!sys->tk_flg || !(sys->sp_flg & 0x2) || (sys->cb_flg & 0x1) || (sys->gm_flg & 0x9) || !rom || !rom->evtp) return;   /* in a room, not leaving it */
    unlock_applied = g_cheat.unlock;
    for (i = 0; i < nsite; i++) {
        Site* s = &site[i];
        if (s->part < 0 || (want = tests_hold(s)) == s->part) continue;
        for (j = 0; j < s->n && (!s->w[j].set[s->part] || same(get(s->w[j].kind, s->w[j].ix), s->w[j].val[s->part])); j++) {}
        if (j < s->n) {
            host_log("unlock: %d/%d entry script block %04x changed since: left as it is\n", sys->stg_no, sys->rom_no, s->ifel);
            s->part = -1;
            continue;
        }
        for (j = 0; j < s->n; j++)
            if (s->w[j].set[want]) put(s->w[j].kind, s->w[j].ix, s->w[j].val[want]);
            else if (s->w[j].set[s->part]) put(s->w[j].kind, s->w[j].ix, s->w[j].pre);
        host_log("unlock: %d/%d entry script block %04x now takes its %s part\n", sys->stg_no, sys->rom_no, s->ifel, want ? "then" : "else");
        s->part = want;
    }
}

static int unlock_item, unlock_sb, unlock_live, unlock_hit;
int pc_unlock_examine(void)
{
    static unsigned char turn;   /* a spot for several items (the proofs, the jewels): each press the next */
    unsigned char ids[4];
    int n = 0;
    if (!g_cheat.unlock || plp != &ply || !(sys->cb_flg & 0x200) || sys->flr_idx >= (unsigned int)rom->flr_n) return 0;
    {
        const ATR_WORK* z = &rom->flrp[sys->flr_idx];
        const unsigned char p[4] = { z->prm0, z->prm1, z->prm2, z->prm3 };
        if (z->type != 0) return 0;
        for (int i = 0; i < 4; i++)
            if (key_item(p[i]) && !carried(p[i])) ids[n++] = p[i];
    }
    if (!n) return 0;   /* carried: the item screen uses it as usual */
    unlock_item = ids[turn++ % n];
    return 1;
}

/* event.c bhUseItemCheck, bhCmpB: a script tests the item used (sb_id) and it is ours. It is taken when the
   rest of that test holds too (the tests after it: the item screen is up, a floor, a flag). What else the
   frame's scripts change says nothing: many change flags every frame. */
void pc_unlock_item_tested(void)
{
    unsigned char* keep = bhScePtr;
    int ok = 1;
    if (!unlock_live || evaluating || sys->sb_id != unlock_item) return;
    evaluating = 1;
    while (ok && coop_op_cond(*bhScePtr)) {
        unsigned char op = *bhScePtr;
        if (op == 4 || op == 6 || op == 7 || op == 16 || op == 17) ok = bhScenarioJmpT[op]() != 0;   /* Ck CmpB CmpW UseItemCheck PlItemCheck */
        else if (coop_insn_len_raw(bhScePtr) > 0) bhScePtr += coop_insn_len_raw(bhScePtr);   /* (others, e.g. RandTest, not run twice: as holding) */
        else break;
    }
    evaluating = 0;
    bhScePtr = keep;
    unlock_hit |= ok;
}

void pc_unlock_scd1(int after)
{
    static int st8;
    recording = 0;   /* (the entry script is done) */
    if (!unlock_item) return;
    if (!after) {   /* the frame after the item screen's Use */
        unlock_sb = sys->sb_id;
        sys->sb_id = unlock_item;
        sys->cb_flg |= 0x400;
        st8 = !(sys->st_flg & 0x8);
        sys->st_flg |= 0x8;
        unlock_live = 1;
        unlock_hit = 0;
        return;
    }
    unlock_live = 0;
    if (st8) sys->st_flg &= ~0x8;
    sys->cb_flg &= ~0x400;   /* StatusMain clears it on its next frame */
    host_log("unlock: item %d used in %d/%d, %s\n", unlock_item, sys->stg_no, sys->rom_no, unlock_hit ? "taken" : "nothing reacts");
    unlock_item = 0;
    if (!unlock_hit) {   /* nothing here wants it: the press examines */
        sys->sb_id = unlock_sb;
        if (plp == &ply) bhCheckExmAtari(plp);
    }
}

/* item and room names from the game's own message data: glyph = ASCII - 32, 0xFF01 = space. Built when the data
   appears or moves (the system load sets sys->mes_ip twice: the file, then its table), not every frame:
   the overlay (render thread) reads them. A name that changes is published whole: its first character
   last, so a reader sees the old name, an empty one (skipped) or the new one. */
static void publish(char* out, int size, const unsigned int* tab, int i)
{
    unsigned int ofs = tab[i + 1];
    const unsigned short* dp = (const unsigned short*)((const char*)tab + ofs);
    char name[32] = { 0 };
    int n = 0;
    if (ofs < 0x40000)   /* sysmes.ald is 87 KB */
        for (; n < size - 1 && *dp != 0xFFFF; dp++) name[n++] = *dp == 0xFF01 ? ' ' : *dp < 95 ? (char)(*dp + 32) : '?';
    while (n && name[n - 1] == ' ') name[--n] = 0;   /* "Starting Part 2 " */
    if (!memcmp(name, out, size)) return;
    __atomic_store_n(&out[0], 0, __ATOMIC_RELEASE);
    memcpy(out + 1, name + 1, size - 1);
    __atomic_store_n(&out[0], name[0], __ATOMIC_RELEASE);
}

static void read_names(void)
{
    static const unsigned int* built;
    if (sys->mes_ip == built) return;
    built = sys->mes_ip;
    for (int id = 0; id < CHEAT_ITEMS; id++) publish(g_cheat.names[id], sizeof(g_cheat.names[id]), sys->mes_ip, id);
    /* the save menu's room names (the teleport lists, pc/src/host/teleport.c): message 215 + RoomNameSwitch of the second table */
    for (int k = 1; k < 19 && sys->mes_sp && sys->mes_sp[0] > 233; k++) publish(g_cheat.rooms[k], sizeof(g_cheat.rooms[k]), sys->mes_sp, 215 + k);
}

/* Music on a room change without a door. The room scripts turn their music and ambience off as
   the player uses a door to a room without them (BgmOff2, BGSeOff2), and the next room's entry
   script (scd0) asks for its own (BgmOn2, BGSeOn2), which plays on when it is already playing.
   With no door the first half is missing, and the music went on in rooms without their own.
   So around the next room's entry script (sdfunc.c Com_StartInitScript, Com_FinishInitScript):
   before it, the music and ambience are off as such a door leaves them; after it, what the
   script asked for again plays on, and the rest fades out as at a door (Com_ExecRoomFadeOut). */
extern int CurrentBgmNo, CurrentBgmVolume, ReqFadeBgmNo, CurrentBgSeNo[2], ReqFadeBgSe[2];
extern unsigned int AdxPlayFlag[2];
void StopBgm(int FadeOutRate);   /* sdfunc.h (its macros.h clashes here) */
void StopBgm2();
void StopBackGroundSeEx(unsigned int SlotNo, short Timer);
void StopBackGroundSe2(unsigned int SlotNo);
static int warp_sound, warp_bgm, warp_vol, warp_bgse[2];

void cheat_warp_sound(int after)
{
    int i, r;
    if (!warp_sound) return;
    if (!after) {
        warp_bgm = CurrentBgmNo; warp_vol = CurrentBgmVolume;
        StopBgm2();
        for (i = 0; i < 2; i++) { warp_bgse[i] = CurrentBgSeNo[i]; StopBackGroundSe2(i); }
        return;
    }
    warp_sound = 0;
    if ((r = pc_warp_bgm(ReqFadeBgmNo, CurrentBgmNo, CurrentBgmVolume, warp_bgm, warp_vol, AdxPlayFlag[0] != 0)) < 0) StopBgm(400);
    else ReqFadeBgmNo = r;
    for (i = 0; i < 2; i++)
        if ((r = pc_warp_bgse(ReqFadeBgSe[i], CurrentBgSeNo[i], warp_bgse[i])) < 0) StopBackGroundSeEx(i, 400);
        else ReqFadeBgSe[i] = r;
}

/* a warp that never reached its room's entry script (the game ended in the transition: game over, the
   title, a co-op session closing) must not make the next ordinary door take the warp's music rule; a
   quick knife drawn when the game ended gives nothing back in the next one (knife_glue.c); the third-person
   camera of the game that ended no longer counts as on (tps_glue.c) */
void cheat_game_exit(void) { warp_sound = 0; nsite = recording = 0; knife_game_exit(); tps_game_exit(); }

/* the game's own door transition (door type 255: no animation), for the teleport, the co-op
   follow and test aids. An enemy grab plays the player on the enemy's motion set (en01sub.c);
   the new room frees it, and the room change would animate from it (crash in bhSetMotion), so
   take the player's own set back first, as the game does when a grab ends.
   The room file is RM_<stage><room><case>: a door's script picks the case (RoomCaseNo) before it calls the
   door, a warp has none of its own. It loads `rcase` (the co-op host's), or our case as it is, as the room
   has it; else the room's first case. A case only another room has (the prison yard 0/3 has two, 0/10 one)
   was a file the loader waited for for good: stuck in the load screen. */
int GetFileSize(char* FileName);   /* gdlib.c: 0 when neither the disc nor the mods folder has it */
static int room_case_exists(int stg, int rom, int c)
{
    char name[16];
    snprintf(name, sizeof(name), "rm_%1d%02d%1d.rdx", stg, rom, c);   /* as system.c's room load names it */
    return c >= 0 && c <= 9 && GetFileSize(name) != 0;
}
void cheat_room_warp(int stg, int rom, int pos, int rcase)
{
    int c;
    if (rcase < 0) rcase = sys->rcase;
    for (c = 0; c <= 9 && !room_case_exists(stg, rom, rcase); c++)
        if (room_case_exists(stg, rom, c)) { host_log("warp: room %d/%d has no case %d: case %d\n", stg, rom, rcase, c); rcase = c; }
    sys->rcase = (char)rcase;
    plp->mnwP = plp->mnwPb;
    warp_sound = 1;
    bhSetDoorDemo(0, stg, rom, pos, 255);
}

static void teleport(int stg, int rom, int pos, int rcase)
{
    static const char* pl = (const char*)-1;
    static int nth;
    if (pl == (const char*)-1) pl = getenv("RECVX_PLAYER");
    nth++;
    /* test aid: the first teleport (RECVX_PLAYER="n:k": the k-th) switches the character as script op bhPlayerChangeSet does */
    if (pl && (!strchr(pl, ':') || nth >= atoi(strchr(pl, ':') + 1))) {
        sys->cng_pid = atoi(pl) & 3;
        sys->cb_flg |= 0x80;
        pl = NULL;
    }
    cheat_room_warp(stg, rom, pos, rcase);
}

/* Testing aid for headless runs, keyed to game frames since start (frames in which the game
   task advanced sys->gfrm_ct, which restarts per room):
   RECVX_TELEPORT="frame:stage:room:pos[:case],delay:stage:room:pos,..." (later entries: frames after
   the previous teleport; case: the room case to load, else ours as it is) and RECVX_EQUIP="frame:item:count[,item:count...]" (puts a weapon item
   in the first slot and equips it with the inventory's WeaponSet; the item may carry its ammo
   kind, e.g. 4102 = 0x1006 grenade launcher with acid rounds; further entries fill the next
   slots; after a ';' another "frame:item:count" equips again later: a weapon switch).
   RECVX_WARP="stage:room:frame:x:z:angle,..." puts the player at x/z facing `angle` once it has
   been `frame` game frames in that room (independent of how the room was reached, e.g. a
   co-op join).
   RECVX_INF_AMMO=1 turns the infinite ammo cheat on, RECVX_UNLOCK=1 "Unlock all" (RECVX_UNLOCK="frame:0/1,..." switches
   it at those game frames); RECVX_NOCLIP="frame:mode,..." sets the
   noclip mode (0/1/2) at that game frame; it also logs the position every 15 game frames and any jump
   of more than 2 units in one tick ("test: JUMP").
   RECVX_EFFECT="frame:effno:type:mode1" spawns effect effno at the player at that game frame
   and sets its mode1 two frames later (e.g. "60:110:0:1": the full-screen distortion of
   bhEff110, which reads the frame buffer back every frame - StoreRenderTex).
   RECVX_PLAYER=n ("n:k": the k-th) : the first RECVX_TELEPORT also switches the character played to n (0 Claire,
   1 Chris, 2 Steve, 3 Wesker) as the story's character switch does.
   RECVX_APPEAR="frame:chr,..." sets "You appear as" (g_coop.chr, the ini's coop_char) at that
   game frame.
   RECVX_SKIP_CINE=n skips each cutscene (as its skip button: cb_flg 0x10000000) after n game frames.
   RECVX_FLAGS="set:bit,..." sets these story flags at the first game frame (set as in the room
   scripts: 1 ev_flg, 2 ky_flg, 3 ed_flg, 7 it_flg, 8 mp_flg; e.g. "1:183,1:184"). */
static void scripted(unsigned int* inv)
{
    static const char *tp = (const char*)-1, *eq, *wp, *ef, *nc, *ap, *ul;
    static int ef_slot = -1, ef_frame, nclog;
    float x, z;
    static int last, clock, prev, room, in_room;
    int f, a, b, c, n;
    if (tp == (const char*)-1) {
        tp = getenv("RECVX_TELEPORT"); eq = getenv("RECVX_EQUIP"); wp = getenv("RECVX_WARP"); ef = getenv("RECVX_EFFECT");
        if (getenv("RECVX_INF_AMMO")) g_cheat.inf_ammo = 1;
        ul = getenv("RECVX_UNLOCK");
        if (ul && !strchr(ul, ':')) { g_cheat.unlock = atoi(ul) != 0; ul = NULL; }
        nc = getenv("RECVX_NOCLIP");
        nclog = nc != NULL;
        ap = getenv("RECVX_APPEAR");
    }
    if (nclog && sys->gfrm_ct != prev && clock % 15 == 0)   /* with RECVX_NOCLIP: the path taken */
        printf("test: game frame %d room %d/%d pos %.1f %.1f %.1f floor %d noclip %d\n", clock, sys->stg_no, sys->rom_no, plp->px, plp->py,
               plp->pz, plp->flr_no, g_cheat.noclip);
    if (nclog) {   /* and any jump of more than 2 units in one tick (the player moved by code, not by walking) */
        static float lx, lz;
        static int lroom = -1;
        if (lroom == coop_room_key() && (plp->px - lx) * (plp->px - lx) + (plp->pz - lz) * (plp->pz - lz) > 4.0f)
            printf("test: JUMP at game frame %d room %d/%d from %.1f %.1f to %.1f %.1f %.1f floor %d\n", clock, sys->stg_no, sys->rom_no, lx, lz,
                   plp->px, plp->py, plp->pz, plp->flr_no);
        lx = plp->px; lz = plp->pz; lroom = coop_room_key();
    }
    if (nc && sscanf(nc, "%d:%d%n", &f, &a, &n) == 2 && clock >= f) {
        printf("test: noclip %d at game frame %d (%.1f %.1f %.1f)\n", a, clock, plp->px, plp->py, plp->pz);
        g_cheat.noclip = a;
        nc = nc[n] == ',' ? nc + n + 1 : NULL;
    }
    if (wp && sscanf(wp, "%d:%d:%d:%f:%f:%d%n", &a, &b, &f, &x, &z, &c, &n) == 6 && sys->stg_no == a && sys->rom_no == b &&
        in_room >= f && !(sys->gm_flg & 0x9) && !(sys->cb_flg & 0x1)) {
        printf("test: warp to (%.1f %.1f) in %d/%d\n", x, z, a, b);
        {   /* the player and its collision points move together: no wall check across the jump */
            EXP_WORK* e = (EXP_WORK*)plp->exp0;
            float dx = x - plp->px, dz = z - plp->pz;
            plp->px += dx; plp->pxb += dx; plp->pz += dz; plp->pzb += dz; plp->ay = c;
            plp->flg &= ~0xC0000;   /* not placed by the legs of the last pose (bhFixPosition) */
            if (e) { e->fpx += dx; e->bpx += dx; e->bpxb += dx; e->spx += dx; e->plx += dx; e->fpz += dz; e->bpz += dz; e->bpzb += dz; e->spz += dz; e->plz += dz; }
        }
        wp = wp[n] == ',' ? wp + n + 1 : NULL;
    }
    {
        static const char* fl = (const char*)-1;
        if (fl == (const char*)-1) fl = getenv("RECVX_FLAGS");
        for (; fl && sscanf(fl, "%d:%d%n", &a, &b, &n) == 2; fl = fl[n] == ',' ? fl + n + 1 : NULL) {
            unsigned int* w = a == 1 ? sys->ev_flg : a == 2 ? sys->ky_flg : a == 3 ? sys->ed_flg : a == 7 ? sys->it_flg : a == 8 ? sys->mp_flg : NULL;
            if (w && b >= 0 && b < 256 * (a == 8 ? 1 : a == 2 || a == 7 ? 2 : 4)) { w[b >> 5] |= 0x80000000u >> (b & 31); printf("test: flag %d:%d set\n", a, b); }
        }
    }
    {
        static const char* sk = (const char*)-1;
        static int cine;
        if (sk == (const char*)-1) sk = getenv("RECVX_SKIP_CINE");
        if (sk && (sys->cb_flg & 0x4) && !(sys->st_flg & 0x1C040008)) {
            if (sys->gfrm_ct != prev && ++cine == atoi(sk)) { sys->cb_flg |= 0x10000000; printf("test: cutscene skipped in %d/%d\n", sys->stg_no, sys->rom_no); }
        } else cine = 0;
    }
    if (coop_room_key() != room) { room = coop_room_key(); in_room = 0; }
    if (sys->gfrm_ct != prev) { prev = sys->gfrm_ct; clock++; in_room++; }
    if (tp && sscanf(tp, "%d:%d:%d:%d%n", &f, &a, &b, &c, &n) == 4 && clock >= f + last &&
        !(sys->gm_flg & 0x9) && !(sys->cb_flg & 0x1)) {
        int rc = -1, m = 0;
        if (tp[n] == ':' && sscanf(tp + n, ":%d%n", &rc, &m) == 1) n += m;   /* an optional room case */
        printf("test: teleport to %d/%d/%d case %d at game frame %d\n", a, b, c, rc, clock);
        teleport(a, b, c, rc);
        last = clock;
        tp = tp[n] == ',' ? tp + n + 1 : NULL;
    }
    {   /* RECVX_TP_NAME="frame:entry:room name,...": the teleport lists' room of that name (as the menus show it,
           e.g. "Prison: Obs. R"), then the menus' Teleport; frames as RECVX_TELEPORT. RECVX_TP_LIST=1 logs the lists. */
        static const char *tn = (const char*)-1, *tl;
        char want[64], nm[96], en[96];
        if (tn == (const char*)-1) { tn = getenv("RECVX_TP_NAME"); tl = getenv("RECVX_TP_LIST"); }
        if (tl && tp_count() && g_cheat.rooms[1][0]) {   /* with the game's names */
            for (int i = 0; i < tp_count(); i++) {
                const TpRoom* r = tp_room(i);
                printf("rooms: %d/%02d %s | %s | %s", r->stg, r->rom, r->file, tp_stage_name(r->stg), tp_room_name(i, 0, nm, sizeof(nm)));
                for (int p = 0; p < tp_entries(i); p++) printf(" | %s", tp_entry_name(i, p, en, sizeof(en)));
                printf("\n");
            }
            tl = NULL;
        }
        if (tn && tp_count() && sscanf(tn, "%d:%d:%n", &f, &c, &n) == 2 && clock >= f + last && !(sys->gm_flg & 0x9) && !(sys->cb_flg & 0x1)) {
            int i, len = (int)strcspn(tn + n, ",");
            snprintf(want, sizeof(want), "%.*s", len, tn + n);
            for (i = 0; i < tp_count() && strcmp(tp_room_name(i, 0, nm, sizeof(nm)), want); i++) {}
            if (i < tp_count()) {
                g_cheat.tp_stg = tp_room(i)->stg; g_cheat.tp_rom = tp_room(i)->rom; g_cheat.tp_pos = c;
                __atomic_or_fetch(&g_cheat.request, CHEAT_TELEPORT, __ATOMIC_ACQ_REL);
                printf("test: teleport to '%s' (%d/%d, %s) at game frame %d\n", want, g_cheat.tp_stg, g_cheat.tp_rom, tp_entry_name(i, c, en, sizeof(en)), clock);
            } else printf("test: no room named '%s' in the teleport list\n", want);
            last = clock;
            tn = tn[n + len] == ',' ? tn + n + len + 1 : NULL;
        }
    }
    if (ef && sscanf(ef, "%d:%d:%d:%d", &f, &a, &b, &c) == 4) {
        if (ef_slot < 0 && clock >= f) {
            POINT pt = { plp->px, plp->py, plp->pz, 0, 0, 0 };
            ef_slot = bhSetEffectEvt(a, &pt, (unsigned short)b, 0, 0);
            ef_frame = clock;
            printf("test: effect %d type %d -> slot %d at game frame %d\n", a, b, ef_slot, clock);
            if (ef_slot < 0) ef = NULL;
        } else if (ef_slot >= 0 && clock >= ef_frame + 2) {
            eff[ef_slot].mode1 = (unsigned char)c;
            ef = NULL;
        }
    }
    {   /* RECVX_EFF_MODE1="f:id:type:mode1,...": at game frame f, mode1 of every live effect of that id and type
           (e.g. "200:263:0:1": the fires laid out in Alexia's hall burn, as in her fight) */
        static const char* em = (const char*)-1;
        if (em == (const char*)-1) em = getenv("RECVX_EFF_MODE1");
        while (em && sscanf(em, "%d:%d:%d:%d%n", &f, &a, &b, &c, &n) == 4 && clock >= f) {
            int hit = 0;
            for (int i = 0; i < 512; i++)
                if ((eff[i].flg & 1) && eff[i].id == a && eff[i].type == b) { eff[i].mode1 = (unsigned char)c; hit++; }
            printf("test: effect %d type %d: mode1 %d on %d at game frame %d\n", a, b, c, hit, clock);
            em = em[n] == ',' ? em + n + 1 : NULL;
        }
    }
    {   /* RECVX_RETRY="frame,...": a retry point here (as the scripts' ReTryPointSet): the game over's continue resumes in this room */
        static const char* rt = (const char*)-1;
        if (rt == (const char*)-1) rt = getenv("RECVX_RETRY");
        if (rt && *rt && clock >= atoi(rt) && !(sys->cb_flg & 0x1)) {
            bhPushGameData();
            printf("test: retry point in %d/%d at game frame %d\n", sys->stg_no, sys->rom_no, clock);
            rt = strchr(rt, ',') ? strchr(rt, ',') + 1 : NULL;
        }
    }
    {   /* RECVX_TEST_TYPEWRITER="frame,...": the typewriter's save screen at that game frame (as its ink ribbon question's yes) */
        static const char* tw = (const char*)-1;
        if (tw == (const char*)-1) tw = getenv("RECVX_TEST_TYPEWRITER");
        if (tw && *tw && clock >= atoi(tw) && !(sys->cb_flg & 0x1)) {
            sys->cb_flg |= 0x200000;
            printf("test: typewriter in %d/%d at game frame %d\n", sys->stg_no, sys->rom_no, clock);
            tw = strchr(tw, ',') ? strchr(tw, ',') + 1 : NULL;
        }
    }
    {   /* RECVX_TEST_COMPUTER="frame:n,...": computer terminal n at that game frame (as the room scripts' EvtComSet n) */
        static const char* ct = (const char*)-1;
        if (ct == (const char*)-1) ct = getenv("RECVX_TEST_COMPUTER");
        if (ct && sscanf(ct, "%d:%d%n", &f, &a, &n) == 2 && clock >= f && !(sys->cb_flg & 0x1)) {
            sys->com_num = a;
            sys->cb_flg |= 0x400000;
            printf("test: computer %d in %d/%d at game frame %d\n", a, sys->stg_no, sys->rom_no, clock);
            ct = ct[n] == ',' ? ct + n + 1 : NULL;
        }
    }
    {   /* RECVX_TEST_EVENT="frame:level:ev,...": the room script's event ev at that task level from that game frame (as its
           EvtOn: a cutscene without walking to its trigger; entries of the same frame start together, as a script starts them) */
        static const char* te = (const char*)-1;
        int lv;
        if (te == (const char*)-1) te = getenv("RECVX_TEST_EVENT");
        while (te && sscanf(te, "%d:%d:%d%n", &f, &lv, &a, &n) == 3 && clock >= f && !(sys->cb_flg & 0x1)) {
            Event_exec((unsigned)lv, (unsigned)a);
            printf("test: event %d at task level %d in %d/%d at game frame %d\n", a, lv, sys->stg_no, sys->rom_no, clock);
            te = te[n] == ',' ? te + n + 1 : NULL;
        }
    }
    {   /* RECVX_TEST_ROOMFLAGS="frame:mask,...": rm_flg |= mask at that game frame (bit n of the scripts is 0x80000000 >> n):
           a room script's local step without its input (1/05's switch panel solved: C, E and "decide", 14800000) */
        static const char* rf = (const char*)-1;
        unsigned int m;
        if (rf == (const char*)-1) rf = getenv("RECVX_TEST_ROOMFLAGS");
        if (rf && sscanf(rf, "%d:%x%n", &f, &m, &n) == 2 && clock >= f) {
            sys->rm_flg |= m;
            printf("test: room flags |= %08x in %d/%d at game frame %d\n", m, sys->stg_no, sys->rom_no, clock);
            rf = rf[n] == ',' ? rf + n + 1 : NULL;
        }
    }
    {   /* RECVX_TEST_EXAMINE="frame:etc,...": the action button on room trigger etc at that game frame, wherever the player
           stands, as bhCheckExmAtari takes a type 3 trigger: the scripts' "at trigger" test and its message (co-op: unless
           the partner is using it) */
        static const char* ex = (const char*)-1;
        if (ex == (const char*)-1) ex = getenv("RECVX_TEST_EXAMINE");
        if (ex && sscanf(ex, "%d:%d%n", &f, &a, &n) == 2 && clock >= f && !(sys->cb_flg & 0x1)) {
            if (coop_on && coop_examine(a))
                printf("test: examine trigger %d in %d/%d at game frame %d: the partner is using it\n", a, sys->stg_no, sys->rom_no, clock);
            else {
                sys->etc_idx = a;
                sys->cb_flg |= 0x100;
                if (a < rom->etc_n && rom->etcp[a].type == 3 && (rom->etcp[a].attr & 0x8000)) {
                    bhSetMessage(0, rom->etcp[a].prm1);
                    sys->pad_onb = sys->pad_on; sys->pad_psb = sys->pad_ps; sys->pad_oldb = sys->pad_old;
                    sys->pad_ps = 0;
                    sys->cb_flg |= 0x20;
                    sys->st_flg |= 0x2004;
                }
                printf("test: examine trigger %d in %d/%d at game frame %d\n", a, sys->stg_no, sys->rom_no, clock);
            }
            ex = ex[n] == ',' ? ex + n + 1 : NULL;
        }
    }
    {   /* RECVX_HP="frame:hp,...": our health set at that game frame (a quick game over) */
        static const char* hp = (const char*)-1;
        if (hp == (const char*)-1) hp = getenv("RECVX_HP");
        if (hp && sscanf(hp, "%d:%d%n", &f, &a, &n) == 2 && clock >= f) {
            plp->hp = a;
            printf("test: health %d at game frame %d\n", a, clock);
            hp = hp[n] == ',' ? hp + n + 1 : NULL;
        }
    }
    if (ul && sscanf(ul, "%d:%d%n", &f, &a, &n) == 2 && clock >= f) {
        printf("test: unlock all %d at game frame %d in %d/%d\n", a, clock, sys->stg_no, sys->rom_no);
        g_cheat.unlock = a != 0;
        ul = ul[n] == ',' ? ul + n + 1 : NULL;
    }
    if (ap && sscanf(ap, "%d:%d%n", &f, &a, &n) == 2 && clock >= f) {
        printf("test: appear as %d at game frame %d\n", a, clock);
        g_coop.chr = a;
        ap = ap[n] == ',' ? ap + n + 1 : NULL;
    }
    spawn_frame(clock);   /* RECVX_SPAWN (pc/glue/spawn_glue.c) */
    /* in play, weapon down (mode1 0), on our own motion set, as the inventory and the quick knife switch: a switch
       mid-shot, in a grab or in a scene (room motions) realized the weapon motions into another set or left the old
       weapon's motion on the new one (crash in bhSetMotion) */
    if (eq && sscanf(eq, "%d:%d:%d%n", &f, &a, &b, &n) == 3 && clock >= f && sys->mn_mode0 == 0 && pc_in_play(plp, 0) &&
        plp->mnwP == plp->mnwPb && plp->mode1 == 0) {
        printf("test: equip item %d at game frame %d\n", a, clock);
        inv[2] = (unsigned int)a << 16 | b;
        inv[0] = 0;
        plp->wpnr_no = 0;
        swork.pip = inv;
        WeaponSet(2, 2);   /* weapon number from the item (incl. its ammo kind), slot, model load */
        for (eq += n, c = 3; c < 12 && sscanf(eq, ",%d:%d%n", &a, &b, &n) == 2; eq += n) inv[c++] = (unsigned int)a << 16 | b;
        eq = *eq == ';' ? eq + 1 : NULL;   /* "...;frame:item:count": equip another later (a weapon switch) */
    }
}

/* RECVX_ITEM_TRACE=1: the inventory (equipped index, lighter slot, slots) and the two security
   boxes (sys->itm[192], [224]: the metal detector's trays in 0/9) whenever a word changes, with
   the box screen's cursors (listcsr_0/listcsr_2, ips1); by presented frame, as RECVX_INPUT */
uint64_t gs_frame_count(void);
static void item_trace(const unsigned int* inv)
{
    static int on = -1;
    static unsigned int seen[48];
    unsigned int now[48];
    int c;
    if (on < 0) on = getenv("RECVX_ITEM_TRACE") != NULL;
    if (!on) return;
    if (sys->stg_no == 0 && sys->rom_no == 9) {   /* the detector's script: floor zone, rm_flg bit 2 (metal carried), ev_flg bit 53 (gone off) */
        static unsigned int det = ~0u;
        unsigned int d = sys->flr_idx << 2 | !!(sys->rm_flg & 0x20000000) << 1 | !!(sys->ev_flg[1] & 0x400);
        if (d != det)
            printf("detector: frame %u floor %u metal %u alarm %u\n", (unsigned)gs_frame_count(), d >> 2, d >> 1 & 1, d & 1);
        det = d;
    }
    for (c = 0; c < 16; c++) { now[c] = inv[c]; now[16 + c] = sys->itm[192 + c]; now[32 + c] = sys->itm[224 + c]; }
    if (!memcmp(now, seen, sizeof(now))) return;
    memcpy(seen, now, sizeof(now));
    printf("items: frame %u room %d/%d cursor %u/%u box cursor %d; equipped %u", (unsigned)gs_frame_count(), sys->stg_no, sys->rom_no,
           swork.listcsr_0, swork.listcsr_2, (int)swork.ips1, now[0]);
    for (c = 1; c < 48; c++) {
        if (!(c & 15)) printf("\n  box %d:", c == 16 ? 192 : 224);
        else if (c == 1) printf("\n  inventory:");
        if (now[c] >> 16) printf(" [%d] %s(%u) %u", c & 15, g_cheat.names[(now[c] >> 16) & 0xFF], (now[c] >> 16) & 0xFF, now[c] & 0xFFFF);
        else if (now[c]) printf(" [%d] junk %#x", c & 15, now[c]);
        else if (c < 16) printf(" [%d] -", c);
    }
    printf("\n");
}

void cheat_frame(void)
{
    unsigned int* inv;
    int max_hp, slots, i, req;

    if (!sys || !plp) return;
    inv = &sys->itm[sys->ply_id * 16];
    max_hp = sys->gm_mode == 2 ? 320 : 160;
    slots = (sys->gm_flg & 0x8000000) ? 10 : 8;
    if (sys->mes_ip) read_names();
    item_trace(inv);
    if (sys->tk_flg && (sys->sp_flg & 0x2)) scripted(inv);   /* in game */
    unlock_switch();

    g_cheat.hp = plp->hp;
    g_cheat.stg = sys->stg_no; g_cheat.rom = sys->rom_no; g_cheat.pos = sys->pos_no;
    g_cheat.player = sys->ply_id;
    g_cheat.slots = slots;
    for (i = 0; i < 12; i++) g_cheat.inv[i] = inv[i];

    if (g_cheat.inf_health && plp->hp < max_hp) plp->hp = max_hp;

    req = __atomic_exchange_n(&g_cheat.request, 0, __ATOMIC_ACQ_REL);
    if (!req) return;
    if (req & CHEAT_HEAL) { plp->hp = max_hp; g_cheat.result = "Health restored."; }
    if (req & CHEAT_GIVE) {
        g_cheat.result = "Inventory full.";
        for (i = 2; i < 2 + slots; i++)
            if (!(inv[i] >> 16)) {
                inv[i] = (unsigned int)g_cheat.give_id << 16 | (g_cheat.give_count & 0xFFFF);
                g_cheat.result = "Item added.";
                break;
            }
    }
    if (req & CHEAT_SPAWN) g_cheat.result = spawn_enemy(g_cheat.spawn_idx);
    if (req & CHEAT_TELEPORT) {
        /* the game's own door transition (door type 255 skips the animation), under the
           conditions the door trigger checks (hitchk.c) */
        if ((sys->gm_flg & 0x9) || (sys->cb_flg & 0x1)) g_cheat.result = "Cannot teleport now. Finish the current action first.";
        else {
            teleport(g_cheat.tp_stg, g_cheat.tp_rom, g_cheat.tp_pos, -1);
            g_cheat.result = "Teleporting...";
        }
    }
}
