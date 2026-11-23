/* Co-op: what coop_glue.c, coop_look.c, coop_own.c, coop_snap.c and coop_fx.c share (private to them; split out of
 * coop_glue.c as a pure move). First the headers all of them include, then the constants and types they share, then
 * the session state and functions one of them defines and the others use (hidden: not exported from the binary). */
#ifndef COOP_INT_H
#define COOP_INT_H
#include "../../decomp/include/ps2/veronica/prog/types.h"
#include "../../decomp/include/ps2/veronica/prog/main.h"
#include "../../decomp/include/ps2/veronica/prog/dread.h"
#include "../../decomp/include/ps2/veronica/prog/Motion.h"
#include "../../decomp/include/ps2/veronica/prog/MdlPut.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_texture.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_NaTextureFunction.h"
#include "../../decomp/include/ps2/veronica/prog/room.h"
#include "../../decomp/include/ps2/veronica/prog/item.h"
#include "../../decomp/include/ps2/veronica/prog/sub1.h"
#include "../../decomp/include/ps2/veronica/prog/objitm.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_NaMatrix.h"
int ikou3(BH_PWORK* epw, NJS_POINT3* pos, int add_dir);   /* zonzon.c (its header redefines libc macros) */
#include "../../decomp/include/ps2/veronica/prog/sdfunc.h"
#include "../../decomp/include/ps2/veronica/prog/hitchk.h"
#include "../../decomp/include/ps2/veronica/prog/player.h"
#include "../../decomp/include/ps2/veronica/prog/light.h"
#include "../../decomp/include/ps2/veronica/prog/effect.h"
#include "../../decomp/include/ps2/veronica/prog/playpch.h"
#include "../../decomp/include/ps2/veronica/prog/effsub3.h"
#include "../../decomp/include/ps2/veronica/prog/pwksub.h"
#include "../../decomp/include/ps2/veronica/prog/ps2_sg_pdvib.h"
#include "../src/host/coop.h"
#include "../src/host/lang.h"
#include "../src/host/menu_mouse.h"
#include "../src/host/lobby.h"
#include "../src/host/cheat.h"
#include "../src/host/bindings.h"
#include "../src/host/actors.h"
#include "../src/import/import.h"
#include "../src/host/tps.h"
#include "../src/host/cast.h"
#include "../src/host/enemies.h"
#include "../src/host/enemymdl.h"
#include "../src/host/biorand.h"
#include "../src/host/biorand_opts.h"
#include "../src/net/biorand_dl.h"
#include "../src/net/net.h"
#include "../src/net/coop_proto.h"
#include "pc_rules.h"
#include "../src/net/rendezvous.h"
#include "../src/net/rdv_proto.h"
#include "../src/gs/stream.h"
#include <time.h>
extern char* rdx_files[205];            /* ps2_dvd_image.c: the room files */
extern int rdx_image_data_max;
extern int enemy_room_gen;              /* enemyopt_glue.c: room loads so far */
extern int pose_trace_mark;             /* pose_trace.c (test aid RECVX_POSE_TRACE) */
extern unsigned char tps_hit_precise[PC_ENE_SLOTS];   /* tps_glue.c: an enemy's last gun hit was a precise one (weapon + 1) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include <time.h>

void host_log(const char* fmt, ...);
Sint32 ADXF_ReadNw32(ADXF adxf, Sint32 nsct, void* buf);   /* PC: synchronous */
extern const char PlyFlip[23];
extern const int WpnAngTab[21][3];
extern NJS_POINT3 PlayerPos;            /* sdfunc.c: where CallPlayerVoice plays */

#define MAX_MTN (int)(sizeof(p2.plmth) / sizeof(MN_WORK))
#define ENE_EXP0 0x204                  /* the largest exp0 sent (the Tyrant's FW_WORK, en19 bhEne19_Init; own_types) */
#define ESTATE_ENE (int)(sizeof(BH_PWORK) + ENE_EXP0)
#define ESTATE_PLY (2 * ESTATE_ENE)
#define ESTATE_SIZE (ESTATE_PLY + (int)sizeof(BH_PWORK))
/* (protocol 56) EV_ESTATE carries them packed (coop_zpack: the zero words left out); the received transfer is unpacked into
   estate[] once, when an event that names it is applied (estate_take) */
#define ESTATE_Z (2 + ESTATE_SIZE / 32 + 1 + ESTATE_SIZE)

/* Protocol 48: the types whose state can change hands (docs/COOP.md "Engaged-player ownership": which types and why the
   others stay with the host); protocol 54: every type the hand-in takes over (docs/COOP.md "The hand-in"). exp0: the size
   of the type's work (its Init's bhEne_CallocWork); keep: byte ranges [from, to) of it that stay the receiver's own - its
   pointers (other enemies, its children, eff[] entries, tables) and what belongs to its own game's player, which its own
   Init and steps set in this room; atr: words holding an ATR_WORK* of the room's collision (the wall or floor it stands
   on), which go as their table and index (atr_enc) and become the receiver's same record. p3: Shared Enemy Control
   hands it over between attacks (own_movable); the others change hands only at the hand-in. Before (protocol 47) every
   hand-over copied the zombie's 0xB0 bytes less a 0x24-byte pointer head, which only the zombie has. */
typedef struct { unsigned char id, p3; unsigned short exp0, keep[6][2], atr[3]; unsigned int rmf; } OwnType;   /* p3 2: a boss (rmf below) */
void pc_enemy_unhold(BH_PWORK* ep);
extern BH_SCEWORK bhEtask[16];
extern BH_SCEWORK* bhCetask;            /* event.c: the event task running now */
void emodel_from_host(const unsigned short* v);   /* enemymdl_glue.c */
struct coop_p2_s {
    unsigned char plmdl[131072], lmmdl[32768], wrmdl[32768], wlmdl[32768];
    unsigned char plmth[12288], plbmt[393216], plwmt[65536], plzmt[8192];
    unsigned char plexw[128], plhdw[128], pletc[32768];
};
struct coop_p2sink_s {                         /* the motions of a part file loaded only for its mesh */
    unsigned char bmt[393216], mth[12288], zmt[8192];
};
struct CoopBufs {
    CoopSnapHist hist[PC_ENE_SLOTS];
    CoopSnapTx stx;                     /* protocol 45: our snapshots as coded, the partner's acknowledgements */
    CoopSnapRx srx;                     /* ... and the partner's as taken */
    EneSnap run_snap[PC_ENE_SLOTS][2];  /* our last two snapshots of a slot we run (owners_sync) */
    EneSnap rx_sn[COOP_ENE_SLOTS], tx_snaps[PC_ENE_SLOTS];   /* receive(), send_frame() */
    Event rb[COOP_RING];                /* protocol 56: the partner's events that came ahead of a gap, by id % COOP_RING (id 0: none) */
};
struct coop_hold_s { int by, id, room, said; unsigned int seen, orphan; };
struct coop_gease_s { int slot, n, len; float dx, dz; };
struct coop_safe_s { float x, y, z; int key, ok, held; };
struct coop_ghost_s { BH_PWORK pw, was; int slot, key; unsigned int t; };
struct coop_aid_scene_s { unsigned int f, n; int slot, gave; };
struct coop_em_in_s { int have; Event e; unsigned mine, host; };

#pragma GCC visibility push(hidden)   /* the names below are this module's own (not exported from the binary) */
extern struct coop_p2_s p2;
extern unsigned char p2file[1 << 21];
extern struct coop_p2sink_s p2sink;
extern unsigned char p2hair_exp0[0x4000], p2hair_exp3[0x1000];
extern BH_PWORK ply2;
extern O_WRK p2wpn[2];
extern O_WRK p2obj[3];
extern ML_WORK no_model;
extern int p2_chr, p2_costume, p2_mfile, p2_wpn, p2_fail, p2_show, p2_drawn;
extern int p2_busy, p2_seen_room;
extern int p2_seen_gen;
extern float p2_pitch;
extern int frozen;
extern float self_bone[22][3], p2_bone[22][3];
extern int p2_other_mesh;
extern int p2_mdl_kept;
void save_bones(ML_WORK* m, float (*bone)[3]);
void keep_bones(ML_WORK* m, float (*bone)[3]);
extern int have_peer, is_host, have_rx, tx_in_game, trace, echo_delay;
extern Pose rx, tx, echo_ring[256];
unsigned coop_partner_import_id(void);
extern unsigned int frame_no, rx_at, join_at, join_frame, join_hinted;
int room_ready(void);
int my_case(void);
int partner_here(void);
unsigned int test_clock(void);
void release_textures(void);
void forget(void);
extern int self_mesh, self_fail, self_tex_gone;
void self_restore(void);
void self_actor(int idx);
int my_chr(void);
int self_loaded(void);
int self_played(void);
void self_frame(void);
extern int fx_open;
extern int fx_live;
int local_visible(void);
void lighter_light(int on);
extern CoopRing ring;
extern struct CoopBufs * bufs;
extern CoopSnapHist* hist;
extern CoopSnapTx* stx;
extern CoopSnapRx* srx;
extern uint32_t hist_applied[PC_ENE_SLOTS];
extern uint32_t shown_seq[PC_ENE_SLOTS];
extern CoopPlayout playout;
extern int snap_room;
void bufs_free(void);
int bufs_get(void);
void snaps_reset(void);
void snaps_lost(void);
extern int proxy_ok;
extern unsigned char tgt[PC_ENE_SLOTS];
extern unsigned int enemy_frame;
extern BH_PWORK* swap_plp;
extern BH_PWORK ply2_save;
extern unsigned char exp_save[256];
extern int swap_pid;
extern unsigned char owned[PC_ENE_SLOTS], owned_child[PC_ENE_SLOTS], estate[ESTATE_SIZE], estate_out[ESTATE_SIZE];
extern unsigned char estate_z[ESTATE_Z], estate_zin[ESTATE_Z];
extern unsigned short estate_tid;
extern unsigned short own_wait[PC_ENE_SLOTS];
extern unsigned int snap_at[PC_ENE_SLOTS];
extern unsigned int pose_seq[PC_ENE_SLOTS];
extern unsigned char step_ran[PC_ENE_SLOTS];
extern unsigned char gave_grab[PC_ENE_SLOTS];
extern CoopXfer estate_x;
extern int aid_m12;
extern unsigned short xfer_tid;
extern int owned_room;
extern int ev_room;
extern int peer_opts;
extern unsigned char own_grab[PC_ENE_SLOTS];
extern unsigned int own_ms[PC_ENE_SLOTS];
extern unsigned int hit_t[PC_ENE_SLOTS][2];
extern unsigned int p3_n[8];
int p3_env(void);
int p3_on(void);
int we_run(int i);
void own_set(int i, int m);
extern unsigned char giving[PC_ENE_SLOTS];
extern uint32_t take_at[PC_ENE_SLOTS];
extern uint32_t keep_from[PC_ENE_SLOTS];
extern unsigned char seen[PC_ENE_SLOTS];
extern unsigned char hid[PC_ENE_SLOTS];
extern unsigned char evd[PC_ENE_SLOTS];
extern int run_room;
extern unsigned int load_frame;
extern uint32_t own_seed[PC_ENE_SLOTS];
extern unsigned char own_win[PC_ENE_SLOTS];
extern unsigned char owned_was[PC_ENE_SLOTS];
extern uint32_t run_q[PC_ENE_SLOTS][2];
extern unsigned int win_st[5];
void owners_sync(void);
int ene_slot(void* p);
short mnw_of(BH_PWORK* ep);
void mnw_set(BH_PWORK* ep, int m);
signed char mdl_of(BH_PWORK* ep);
int set_left(const BH_PWORK* ep, const MN_WORK* set);
int mtn_ok(const BH_PWORK* ep, const MN_WORK* set, unsigned int mtn, int frm);
int finite3(float x, float y, float z);
const OwnType* own_type(const BH_PWORK* ep);
int own_p3(const BH_PWORK* ep);
int own_child(const BH_PWORK* ep);
int child_here(int i, int c);
int ene_len(const BH_PWORK* ep, int c);
int ene_modes_ok(const BH_PWORK* ep, int type, const unsigned char* m, const unsigned char* x0);
int ene_unpack(BH_PWORK* ep, const unsigned char* in, int mdl, int mnw);
void ptr_scan(const void* p, int n, const char* what, int idx);
int estate_take(int tid, int n);
int holding(BH_PWORK* ep);
extern struct coop_hold_s hold;
int hold_family(BH_PWORK* ep);
int linked_to(const BH_PWORK* ep);
int runs_here(BH_PWORK* ep);
extern unsigned int h_stflg;
extern int h_busy, h_ran;
void larva_laid(unsigned int bits, int partner);
void own_timeout(void);
void hide_on(int i, BH_PWORK* ep);
void hide_off(int i);
extern int defer_n;
void defer_hit(const Event* e);
void own_ours(int i, int c);
void own_takeover(const char* why);
void own_reset(int put);
void owned_drop(void);
void grab_out(int i, BH_PWORK* ep);
extern struct coop_gease_s gease;
void grab_ease(BH_PWORK* ep);
void grab_in(const Event* e);
void move_player(float x, float z);
void player_let_go(void);
extern struct coop_safe_s safe;
void grab_failsafe(void);
void hold_watch(void);
void grab_check(void);
void queue_event(Event* e);
extern struct coop_ghost_s ghost;
void enemy_epoch(void);
int same_room(void);
extern struct coop_aid_scene_s aid_scene;
int aid_scene_on(void);
int event_driven(const BH_PWORK* ep);
int grips_us(BH_PWORK* ep);
int ply_busy(const BH_PWORK* pl);
extern unsigned char hit_aim[PC_ENE_SLOTS];
extern uint64_t step_t0, step_ns[2];
extern unsigned int step_cnt[2];
void own_apply(const Event* e);
void own_room_msg(const Event* e);
void own_frame(void);
extern unsigned int fx_frame, fx_sent, fx_dropped, fx_stat;
void fx_begin(int kind);
void fx_end(BH_PWORK* owner, int player);
void fx_tick(void);
void fx_arm(BH_PWORK* ep);
void fx_puppet(BH_PWORK* ep);
void fx_live_trace(void);
int apply_fx(const Event* e);
int build_snapshots(EneSnap* out);
int hit_send(int i, BH_PWORK* ep, int keep);
void capture_hits(void);
void hit_apply(const Event* e);
extern unsigned int gone_mask[(PC_ENE_SLOTS + 31) / 32];
int gone_has(int k);
void hin_host_frame(void);
void hin_apply(const Event* e);
void gone_frame(void);
int apply_gone(const Event* e);
void gone_host_frame(void);
extern unsigned int anc_n, anc_ok, anc_far, anc_far_ok;
extern int anc_stats;
void apply_snapshots(void);
extern unsigned int my_sid, peer_sid, blob_end;
int in_session_game(void);
extern EnemyOpts eopts_sent;
extern unsigned int eopts_sid;
void castoff_sync(void);
void emodel_sync(void);
extern struct coop_em_in_s em_in;
void emodel_guest(int force);
void eopts_send(void);
extern int peer_opts;
extern float snap_prio[PC_ENE_SLOTS];
extern unsigned char snap_grp[PC_ENE_SLOTS];
void snap_prioritise(EneSnap* s, int ns);
#pragma GCC visibility pop
#endif
