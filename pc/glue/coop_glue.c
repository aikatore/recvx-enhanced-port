/* Online co-op (docs/COOP.md "Architecture").
 *
 * Each instance sends its player's pose once per game frame (UDP, at the frame hand-off) and
 * draws the partner as a second player model in the same room. The partner is a puppet: its
 * model is loaded with the game's own loaders (bhReadPlayerData, bhReadWeaponData) by swapping
 * the player context (plp and the sys player buffers) to the player-2 work below, and animated
 * with the stock bhSetMotion/bhCalcModel/bhPutModel from the received motion number and frame.
 * No game state other than the player-2 work is touched; with co-op off nothing here runs.
 */
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
#include "../src/host/cast.h"
#include "../src/host/enemies.h"
#include "../src/host/biorand.h"
#include "../src/host/biorand_opts.h"
#include "../src/net/biorand_dl.h"
#include "../src/net/net.h"
#include "../src/net/coop_proto.h"
#include "pc_rules.h"
#include "../src/net/rendezvous.h"
#include "../src/net/rdv_proto.h"
#include "../src/gs/stream.h"
extern char* rdx_files[205];             /* ps2_dvd_image.c: the room files */
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

CoopState g_coop = { .port = COOP_DEFAULT_PORT, .join = "127.0.0.1:7140", .follow = 1, .upnp = 1, .nametag = 1, .items = COOP_ITEMS_HOST, .server = COOP_DEFAULT_SERVER, .trade_freeze = 1, .remember = 1, .peer_rec = -1,
                    .important = 1, .skip_sync = 1, .skip_detector = 1 };
int coop_on;
int appear_on;                          /* co-op off, "You appear as" in use: our-player hooks only */

/* ---------------------------------------------------------------- wire format */
/* pc/src/net/coop_proto.h: every packet is a pose (the version and the partner's character arrive
   continuously; no separate HELLO), followed by `nev` events (reliable and in order: resent until
   acknowledged) and, from the host, `nene` enemy snapshots, spread over datagrams of at most
   COOP_DGRAM_MAX bytes. Everything received passes coop_check_* before it changes anything. */
#define MAX_SNAP 128                    /* snapshots of a frame as received (over its datagrams) */

/* ---------------------------------------------------------------- player 2 work */
/* Same buffers and sizes as bhInitPlayer gives player 1, in the same order (the loaders write
   O_WORK arrays behind the model data). Static, so they lie in the PS2 address window. */
static struct {
    unsigned char plmdl[131072], lmmdl[32768], wrmdl[32768], wlmdl[32768];
    unsigned char plmth[12288], plbmt[393216], plwmt[65536], plzmt[8192];
    unsigned char plexw[128], plhdw[128], pletc[32768];
} p2 __attribute__((aligned(64)));
static unsigned char p2file[1 << 21] __attribute__((aligned(64)));   /* one part file as read from disc */
static struct {                         /* the motions of a part file loaded only for its mesh */
    unsigned char bmt[393216], mth[12288], zmt[8192];
} p2sink __attribute__((aligned(64)));
static unsigned char p2hair_exp0[0x4000] __attribute__((aligned(64))), p2hair_exp3[0x1000] __attribute__((aligned(64)));
static BH_PWORK ply2;
static O_WRK p2wpn[2];                  /* right/left hand weapon, as sys->obwp[0..1] for player 1 */
static O_WRK p2obj[3];                  /* [2]: hair / Wesker's piece, as sys->obwp[2] for player 1 */
static ML_WORK no_model;                /* stands in for a weapon slot that has none yet */
static int p2_chr = -1, p2_costume, p2_mfile = -1, p2_wpn = -1, p2_fail = -1, p2_show, p2_drawn;
static int p2_busy, p2_seen_room = -1;  /* the partner is busy (BUSY_*); the room it was last shown in */
static float p2_pitch;                  /* the partner's aim pitch shown (radians, Pose.pflg) */
static int frozen;                      /* both players are in the trade screen: the enemies stand still (coop_trade_frozen) */

#define MAX_MTN (int)(sizeof(p2.plmth) / sizeof(MN_WORK))

/* a mesh's own bone offsets (node positions 1..21), put back where motions or scripts authored
   for another character wrote theirs */
static float self_bone[22][3], p2_bone[22][3];
static int p2_other_mesh;
static int p2_mdl_kept;                /* load_player: sub-models of the motion file kept under another mesh (their textures) */

static void save_bones(ML_WORK* m, float (*bone)[3])
{
    int i;
    for (i = 1; i < (int)m->obj_num && i < 22; i++) memcpy(bone[i], m->objP[i].pos, sizeof(bone[i]));
}

static void keep_bones(ML_WORK* m, float (*bone)[3])
{
    int i;
    for (i = 1; i < (int)m->obj_num && i < 22; i++) memcpy(m->objP[i].pos, bone[i], sizeof(bone[i]));
}

/* ---------------------------------------------------------------- session */
static NetAddr peer;
static int have_peer, is_host, have_rx, tx_in_game, trace = -1, echo_delay;
static Pose rx, tx, echo_ring[256];
static PoseExt rx_ext, tx_ext;
extern int cast_n;                      /* actor_glue.c: cutscene cast records drawn as other models */
unsigned coop_partner_import_id(void) { return coop_on && have_rx ? rx_ext.imp_id : 0; }   /* (cast_glue.c) */
int coop_partner_ply(void) { return coop_on && have_rx && rx.mfile >= 0 ? rx.mfile & 3 : -1; }   /* (cast_glue.c) the character the partner plays, -1 */
static unsigned int frame_no, rx_at, join_at, join_frame, join_hinted;
/* ping: the partner's clock in the first datagram of its newest frame here and when that arrived
   (net_rx_us: the network's arrival, not our frame's); the round trip smoothed (ms, < 0: none yet) */
static uint32_t echo_t;
static uint64_t echo_arr;
static double ping_f = -1;
int coop_room_key(void) { return sys->stg_no * 256 + sys->rom_no; }
/* the partner's pose is from our room: the same stage and room, and the same room case (a room's cases are
   different room files: other enemies, objects, script); a build that does not send its case: any */
static int my_case(void)   /* the room loaded (bhFinishRoom keeps it in the _b fields); in a door to another room, the next one's */
{
    return sys->stg_no == sys->stg_nob && sys->rom_no == sys->rom_nob ? sys->rcase_b : sys->rcase;
}
static int partner_here(void) { return rx.stg == sys->stg_no && rx.rom == sys->rom_no && (!rx.rcase || rx.rcase - 1 == my_case()); }
static int rx_room(void) { return (rx.stg * 256 + (unsigned char)rx.rom) | rx.rcase << 16; }   /* the partner's room and case (follow) */
/* test aids count the host's frames (the client takes them from its pose): both act together */
static unsigned int test_clock(void) { return is_host ? frame_no : have_rx ? rx.seq : 0; }

static int read_part(int file)   /* into p2file (actor_glue.c afs_read) */
{
    int ok = afs_read(sys->sys_partid, file, p2file, sizeof(p2file));
    if (!ok) host_log("coop: cannot read part file %d\n", file);
    return ok;
}

static void release_textures(void)
{
    int i;
    for (i = 0; i < ply2.mdl_n || i < p2_mdl_kept; i++)
        if (ply2.mdl[i].texP) njReleaseTexture(ply2.mdl[i].texP);
    for (i = 0; i < 2; i++)
        if (p2wpn[i].mlwP && p2wpn[i].mlwP != &no_model && p2wpn[i].mlwP->texP) njReleaseTexture(p2wpn[i].mlwP->texP);
    bhGarbageTexture(NULL, 0);
    actor_set(1, -1);
}

static void forget(void)
{
    int i, k;
    /* effects that follow the partner's work or use its model or textures (EV_FX: an enemy's effect on the partner,
       FX_SENDER) end with it: the next step or drawing read ply2's model, zeroed here (a model change: "You appear
       as", the character switch) */
    for (i = 0; i < 512; i++) {
        O_WRK* op = &eff[i];
        int ours = (op->flg & 1) && (op->lkwkp == (unsigned char*)&ply2 || op->exp0 == (unsigned char*)&ply2 ||
                                     ((char*)op->mlwP >= (char*)ply2.mdl && (char*)op->mlwP < (char*)(ply2.mdl + 16)));
        for (k = 0; (op->flg & 1) && !ours && k < ply2.mdl_n && k < 16; k++) ours = ply2.mdl[k].texP && op->txp[0] == ply2.mdl[k].texP;
        if (ours) { op->flg = 0; host_log("coop: effect %d on the partner ended with its model\n", op->id); }
    }
    memset(&ply2, 0, sizeof(ply2));
    memset(p2wpn, 0, sizeof(p2wpn));
    memset(p2obj, 0, sizeof(p2obj));
    p2_chr = p2_mfile = p2_wpn = -1;
    p2_mdl_kept = 0;
}

/* A player model from p2file (actor_glue.c player_model_read) */
static void read_player(BH_PWORK* who, unsigned char* mdl, unsigned char* bmt, unsigned char* mth, unsigned char* zmt)
{
    player_model_read(who, p2file, mdl, bmt, mth, zmt);
}

/* Claire's hair (id 1211, simulated by bhObjClpn) or Wesker's piece (1212, rigid) for a model of
   character `chr` (actor_glue.c player_hair); the partner's hair simulates in work of its own */
static void setup_hair(O_WRK* objs, BH_PWORK* who, int chr)
{
    player_hair(objs, who, chr, who != plp ? p2hair_exp0 : NULL, p2hair_exp3);
}

/* Mesh of part file `chr + costume*4` with the motions of part file `mfile` (the sender's own
   character: its motion numbers index that set). A different mesh is loaded over the first
   file's model, its motions into a sink (docs/ACTORS.md "Option A"). chr 4..:
   catalogue model chr - 4 (pc/src/host/actors.h), drawn over part file `mfile`'s model. */
static int load_player(int chr, int costume, int mfile)
{
    int i, ok = 1, mesh = chr >= 4 ? mfile : chr + costume * 4;

    if (p2_chr >= 0) release_textures();
    forget();
    if (!read_part(mfile + 10)) return 0;
    memset(&p2, 0, sizeof(p2));
    read_player(&ply2, p2.plmdl, p2.plbmt, p2.plmth, p2.plzmt);
    if (ply2.mdl_n && mesh != mfile) {
        /* the mesh goes behind the motion file's model, so the sub-models only that file has stay, with their
           textures, as self_load keeps them for us: Claire's 5..7, the moth egg (en27 takes plp->mdl[6..7] and
           plp->skp[7] of the player it is laid on; over the first model they were another file's bytes) */
        BH_PWORK own = ply2;
        int end = 0, keep;
        for (i = 0; i < own.mdl_n; i++)
            if (own.mdl[i].owP && (unsigned char*)(own.mdl[i].owP + own.mdl[i].obj_num) - p2.plmdl > end)
                end = (unsigned char*)(own.mdl[i].owP + own.mdl[i].obj_num) - p2.plmdl;
        end = (end + 63) & ~63;
        memset(ply2.skp, 0, sizeof(ply2.skp));   /* the loader leaves stale skin pointers */
        if (!read_part(mesh + 10)) { release_textures(); forget(); return 0; }
        keep = end + *(unsigned int*)p2file + 8192 <= sizeof(p2.plmdl);   /* (its model block and O_WORKs; the disc's files fit) */
        if (!keep) { release_textures(); end = 0; }
        read_player(&ply2, p2.plmdl + end, p2sink.bmt, p2sink.mth, p2sink.zmt);
        for (i = 0; keep && i < own.mdl_n; i++)
            if (i < ply2.mdl_n) { if (own.mdl[i].texP) njReleaseTexture(own.mdl[i].texP); }
            else { ply2.mdl[i] = own.mdl[i]; ply2.skp[i] = own.skp[i]; ply2.mbp[i] = own.mbp[i]; ply2.txp[i] = own.txp[i]; }
        if (keep) { bhGarbageTexture(NULL, 0); p2_mdl_kept = own.mdl_n; }
        ply2.mnwP = ply2.mnwPb = (MN_WORK*)p2.plmth;
    }
    p2_other_mesh = ply2.mdl_n && mesh != mfile;
    if (p2_other_mesh) save_bones(&ply2.mdl[0], p2_bone);

    for (i = 0; i < ply2.mdl_n; i++) ok &= ply2.mdl[i].owP != NULL;
    if (!ply2.mdl_n || !ok) { host_log("coop: player model %d/%d did not load\n", chr, costume); release_textures(); forget(); return 0; }

    /* the parts of bhSetPlayer the drawing path depends on */
    ply2.flg = 0x119;
    ply2.mdflg = 0x20;
    ply2.exp0 = p2.plexw; ply2.exp1 = p2.plhdw; ply2.exp3 = p2.pletc;
    ply2.mtx = (float(*)[16])ply2.mtxbuf;
    ply2.sx = ply2.sy = ply2.sz = ply2.sxb = ply2.syb = ply2.szb = 1.0f;
    ply2.mtn_tp = (unsigned char*)PlyFlip;
    ply2.mlwP->owP[7].flg |= 0x8;
    ply2.mlwP->owP[11].flg |= 0x8;
    setup_hair(p2obj, &ply2, chr);
    if (chr >= 4) actor_set(1, chr - 4);   /* on failure the partner shows its own character */
    p2_chr = chr;
    p2_costume = costume;
    p2_mfile = mfile;
    host_log("coop: loaded player-2 model %d costume %d (%d parts) with the motions of part file %d\n", chr, costume, ply2.mdl_n, mfile + 10);
    return 1;
}

/* Weapon models and the weapon motion set (motions 100+), also present for "no weapon". */
static int load_weapon(int wpn)
{
    int i;

    p2_wpn = wpn;   /* also on failure: no retry every frame */
    if (wpn < 0 || wpn > 20 || !read_part(wpn + (p2_mfile & 3) * 30 + 20)) return 0;   /* the sender's character's weapon file */
    for (i = 0; i < 2; i++)
        if (!p2wpn[i].mlwP) p2wpn[i].mlwP = &no_model;   /* the loader releases the old weapon's textures */
    for (i = 100; i < MAX_MTN; i++) ply2.mnwPb[i].datP = NULL;

    ply2.mnwP = ply2.mnwPb;   /* the weapon motions go to entries 100+ of the base set */
    player_weapon_read(&ply2, p2wpn, wpn, p2file, p2.wrmdl, p2.wlmdl, p2.plwmt);   /* (actor_glue.c) */
    memset(&no_model, 0, sizeof(no_model));
    return 1;
}

/* ---------------------------------------------------------------- our own model (M7) */
/* "You appear as" also changes our own player's mesh while co-op is on: the chosen part file's
   model is loaded into a buffer of its own (the motions into the sink), so the original model
   data in sys->plmdlp stays intact and is put back by copying the saved ML_WORKs. Our motions,
   weapon files, PlyInfo and voice stay the character's. The original's textures stay loaded
   while the mesh is shown. Sub-models the new file lacks (Claire's 5..7: the moth egg, en27,
   takes mdl[6], skp[7]) keep the original's. When the game reloads the player (game start, load,
   continue, story switch) the model no longer points into our buffer; it is applied again at
   the next frame in control. */
static unsigned char self_mdl[131072] __attribute__((aligned(64)));
static struct {
    ML_WORK mdl[16];
    int* skp[16];
    NJS_CNK_OBJECT* mbp[16];
    NJS_TEXLIST* txp[16];
    int mdl_n, mlw;
} self_orig;
static int self_mesh = -1, self_fail = -1, self_tex_gone;
static int self_cine;                   /* self_cine_hair: the character's own head part is up for a cutscene double */
/* What a cutscene leaves on the played character's model, set up for that character: scripts
   re-attach the head part sys->obwp[2] with its offsets (ObjLinkSetPly; Claire's ponytail at
   0, 1.58, 0.77, behind the head: Wesker's sunglasses ended up there) and a few room motions
   carry node positions (bone lengths). For another mesh both are put back every tick. */
static struct { NJS_CNK_OBJECT* objP; unsigned char* lkwkp; int lkono; float lox, loy, loz; } self_part;

static int self_ours(void) { return plp->mdl[0].objP && (unsigned char*)plp->mdl[0].objP >= self_mdl && (unsigned char*)plp->mdl[0].objP < self_mdl + sizeof(self_mdl); }

static void self_release(ML_WORK* mdl, int n)
{
    int i;
    if (self_tex_gone) return;   /* njReleaseTextureAll took them */
    for (i = 0; i < n; i++)
        if (mdl[i].texP) njReleaseTexture(mdl[i].texP);
    bhGarbageTexture(NULL, 0);
}

static void self_put_back(void);

/* back to the character's own mesh (or forget ours when the game has reloaded the player) */
static void self_restore(void)
{
    if (self_mesh < 0) return;
    if (self_ours()) {
        self_release(plp->mdl, plp->mdl_n);
        self_put_back();
        setup_hair(sys->obwp, plp, sys->ply_id);
        self_cine = 0;
    }
    /* else the game has loaded the player again: after the game ended (coop_textures_released) or a
       character switch (coop_player_unload put the original back first). The original's texture lists
       were in the buffer the new load overwrote: releasing them crashed (story switch, Battle Game). */
    self_mesh = -1;
}

/* system.c bhSysCallMonitor: the game releases the player's textures and loads another character */
void coop_player_unload(void) { self_restore(); }

/* The player's node matrices (O_WORK) are last tick's pose: bhFixPosition (motions that plant a foot,
   plp->flg 0x40000) moves the player by the difference between them and the new pose. A model swapped
   in must carry that pose, or the player jumps: fresh O_WORKs are zero (to the room's origin, out of
   the map), the original's were last computed when it was swapped out (back to where the player
   stood then, another room's coordinates after a door). Also no blend from the T-pose. */
static void self_carry_pose(const ML_WORK* from)
{
    ML_WORK* to = plp->mlwP;
    int i, n = from->obj_num < to->obj_num ? from->obj_num : to->obj_num;
    if (!from->objP || !to->objP || !from->owP || !to->owP || from->objP == to->objP) return;
    memcpy(to->objP[0].pos, from->objP[0].pos, sizeof(to->objP[0].pos));
    for (i = 0; i < n && i < 22; i++) memcpy(to->objP[i].ang, from->objP[i].ang, sizeof(to->objP[i].ang));
    bhCalcTree((NJS_MATRIX*)plp->mtx, to);
}

/* called with the original model in place (self_mesh < 0) */
static void self_put_back(void)
{
    ML_WORK cur = *plp->mlwP;
    int i;
    for (i = 0; i < 16; i++) { plp->mdl[i] = self_orig.mdl[i]; plp->skp[i] = self_orig.skp[i]; plp->mbp[i] = self_orig.mbp[i]; plp->txp[i] = self_orig.txp[i]; }
    plp->mdl_n = self_orig.mdl_n;
    plp->mlwP = &plp->mdl[self_orig.mlw];
    self_carry_pose(&cur);
}

static int self_load(int mesh)
{
    MN_WORK *mnw = plp->mnwP, *mnwb = plp->mnwPb;
    int i, mlw = plp->mlwP - plp->mdl, ok = 1;
    if (!read_part(mesh + 10)) return 0;
    memcpy(self_orig.mdl, plp->mdl, sizeof(self_orig.mdl));
    memcpy(self_orig.skp, plp->skp, sizeof(self_orig.skp));
    memcpy(self_orig.mbp, plp->mbp, sizeof(self_orig.mbp));
    memcpy(self_orig.txp, plp->txp, sizeof(self_orig.txp));
    self_orig.mdl_n = plp->mdl_n;
    self_orig.mlw = mlw >= 0 && mlw < 16 ? mlw : 0;
    self_tex_gone = 0;
    memset(plp->skp, 0, sizeof(plp->skp));   /* the loader would apply stale skin pointers */
    read_player(plp, self_mdl, p2sink.bmt, p2sink.mth, p2sink.zmt);
    plp->mnwP = mnw; plp->mnwPb = mnwb;
    for (i = 0; i < plp->mdl_n; i++) ok &= plp->mdl[i].owP != NULL;
    if (!plp->mdl_n || !ok) {   /* keep the original */
        self_release(plp->mdl, plp->mdl_n);
        self_put_back();
        return 0;
    }
    for (i = plp->mdl_n; i < self_orig.mdl_n; i++) {   /* sub-models only the original has (moth egg) */
        plp->mdl[i] = self_orig.mdl[i]; plp->skp[i] = self_orig.skp[i]; plp->mbp[i] = self_orig.mbp[i]; plp->txp[i] = self_orig.txp[i];
    }
    plp->mlwP = &plp->mdl[0];
    plp->mlwP->owP[7].flg |= 0x8;   /* as bhSetPlayer */
    plp->mlwP->owP[11].flg |= 0x8;
    self_carry_pose(&self_orig.mdl[self_orig.mlw]);
    setup_hair(sys->obwp, plp, mesh & 3);
    self_cine = 0;
    {
        O_WRK* op = &sys->obwp[2];
        self_part.objP = (op->flg & 0x1) ? op->mdl[0].objP : NULL;
        self_part.lkwkp = op->lkwkp; self_part.lkono = op->lkono;
        self_part.lox = op->lox; self_part.loy = op->loy; self_part.loz = op->loz;
    }
    save_bones(&plp->mdl[0], self_bone);
    self_mesh = mesh;
    host_log("coop: our own model is now part file %d\n", mesh + 10);
    return 1;
}

/* An enemy/NPC model shown for us (pc/glue/actor_glue.c) instead of the player's: the head part
   (ponytail, sunglasses) goes while it is shown, and comes back as bhSetPlayer sets it up. */
static void self_actor(int idx)
{
    int was = actor_shown(0);
    if (actor_set(0, idx)) { if (!self_cine) sys->obwp[2].flg = 0; }
    else if (was) { setup_hair(sys->obwp, plp, self_mesh >= 0 ? self_mesh & 3 : sys->ply_id); self_cine = 0; }
    actor_weapon_hands(0, sys->obwp, 2);   /* (the weapon's hand piece back when no imported model is shown) */
}

/* "Swapped model in cutscenes" off (g_appear_cutscene), or the doubles kept for the played character's
   own cutscene model (actor_double_kept): while a cutscene double of the character played acts, sys->obwp[2] is the character's own head part (Claire's ponytail: the scripts link it to the
   double, ObjLinkSet), from its own model; when the double is gone, ours again. Where the script
   linked it (work, node, x offset) is kept. Every tick before the objects follow their links. */
extern ETTY_WORK lkmtab[2];             /* player.c: the head parts (both sub-model 4) */
static void self_cine_hair(void)
{
    O_WRK* op = &sys->obwp[2];
    int mesh = self_mesh >= 0 && self_ours(), want = (!g_appear_cutscene || actor_double_kept()) && (actor_shown(0) || mesh) && actor_double_acting();
    int v = lkmtab[0].mdlver, n = plp->mdl_n, ono = op->lkono, *s = plp->skp[v];
    unsigned char* lk = op->lkwkp;
    float lox = op->lox;
    ML_WORK m;
    if (want == self_cine) return;
    self_cine = want;
    m = plp->mdl[v];
    if (want && mesh) {   /* the head part of the model the game loaded (self_orig) */
        plp->mdl[v] = self_orig.mdl[v]; plp->skp[v] = self_orig.skp[v]; plp->mdl_n = self_orig.mdl_n;
    }
    if (want || mesh) setup_hair(sys->obwp, plp, want ? sys->ply_id : self_mesh & 3);   /* (as bhSetPlayer / self_load) */
    else op->flg = 0;   /* (as self_actor) */
    plp->mdl[v] = m; plp->skp[v] = s; plp->mdl_n = n;
    op->lkwkp = lk; op->lkono = ono; op->lox = lox;
    if (want && op->id == 1211) {   /* the ponytail's work: the player's, as bhObjClpn takes it linked to plp
                                       (linked to the double it took it from sys->memp, under a pending disc read) */
        op->flg |= 0x100000;
        op->exp0 = &sys->pletcp[4096];
        op->exp3 = sys->pletcp;
    }
}

/* every tick after bhControlPlayer (before the objects follow their links) */
static void self_keep(void)
{
    O_WRK* op = &sys->obwp[2];
    self_cine_hair();
    if (actor_shown(0) && !self_cine) op->flg &= ~0x1;   /* also when the game set up the player again (story switch) */
    actor_source(0, plp, self_mesh >= 0 && self_ours() ? self_mesh & 3 : actor_shown(0) ? -1 : sys->ply_id, sys->ply_id, tps_aim_pitch());   /* gender-matched animations */
    actor_pose(0, plp);
    actor_weapon_hands(0, sys->obwp, 2);   /* an imported model: its own hands on the weapon, not the piece's */
    if (self_mesh < 0 || !self_ours()) return;
    keep_bones(&plp->mdl[0], self_bone);
    if (self_part.objP && op->mdl[0].objP == self_part.objP) {
        op->lkwkp = self_part.lkwkp; op->lkono = self_part.lkono;
        op->lox = self_part.lox; op->loy = self_part.loy; op->loz = self_part.loz;
        op->flg |= 0x80;
    }
}

/* "You appear as" for the character played now ("As played (cutscene)": its cutscene model); BioRand's "Randomize
   Played as" pick of this game first (biorand_glue.c) */
static int my_chr(void)
{
    int o = pc_biorand_appear();
    return appear_played(o >= 0 ? o : g_coop.chr, sys ? sys->ply_id : 0);
}

/* at the hand-off */
static void self_frame(void)
{
    int own = sys->ply_id + (sys->costume & 3) * 4, want, chr = my_chr();
    if (!plp || !plp->mdl_n) return;
    if (self_mesh >= 0 && !self_ours()) self_restore();   /* the game reloaded the player */
    if (tx_in_game)   /* in cutscenes the character played is acted by its event double */
        actor_doubles(!chr ? -1 : chr >= COOP_CHR_ACTOR ? chr - COOP_CHR_ACTOR :
                      (chr == 5 ? 4 : chr - 1) == own ? -1 : ACTOR_PART + (chr == 5 ? 4 : chr - 1));
    if (!tx_in_game || (sys->cb_flg & 0x1)) return;
    self_actor(chr >= COOP_CHR_ACTOR ? chr - COOP_CHR_ACTOR : -1);
    want = !chr || chr >= COOP_CHR_ACTOR ? -1 : chr == 5 ? 4 : chr - 1;
    if (want == own) want = -1;
    if (want == self_mesh || (want >= 0 && want == self_fail)) return;   /* (-1 is no failure: back to the original) */
    self_restore();
    self_fail = -1;
    if (want >= 0 && !self_load(want)) { self_fail = want; host_log("coop: part file %d cannot be our model\n", want + 10); }
}

void coop_textures_released(void)
{
    forget();   /* njReleaseTextureAll freed them; the model is reloaded when next needed */
    actor_textures_released();
    self_tex_gone = 1;
    self_cine = 0;
    /* our mesh ends with the game too: the character's own model data back in the player work (its
       head part comes with the next game's bhSetPlayer). Left in place, the session's end at the title
       (back from a game, OPEN THE LOBBY or BACK there, F3) put it back itself and set up the head part
       in sys->obwp, NULL there: the crash. */
    if (self_mesh >= 0 && self_ours()) self_put_back();
    self_mesh = -1;
    /* the game ended (bhExitGame: continue, title, load): our player is not in a game until
       coop_control_remote runs again. Set in this frame before the exit, it made the hand-off load
       the appearance and write sys->obwp[2] of the zeroed game state (NULL): the crash on continue. */
    tx_in_game = 0;
}

/* ---------------------------------------------------------------- per frame */
static int fx_open;                     /* effects (EV_FX, below): 1 enemy or player window, 2 a projectile's step */
static void fx_begin(int kind);
static void fx_end(BH_PWORK* owner, int player);
static void fx_arm(BH_PWORK* ep);
static int local_visible(void)
{
    /* the game hides its own player (events), a door transition or an event is running */
    return (sys->pt_flg & 0x1) && !(plp->stflg & 0x1000000) && !(sys->cb_flg & 0x5) && !(sys->gm_flg & 0x9);
}

/* The partner's weapon sounds. Each gun has an arms bank of its own (ARMS_<snd_wpno>.SPQ; the shot
   is its program 5, WpnTab[].seno0 0x105), and the game loads only ours into SE bank 1 - so before,
   the partner's shot played our gun's sound. The partner's bank is read from the disc (sound AFS,
   key snd_wpno | 0x4000, as RequestArmsSoundBank) when its weapon changes, and the sound driver keeps
   it beside ours (snd_partner_arms): arms programs SND_PARTNER_TQ.. play from it. 1: loaded. */
#define SND_PARTNER_TQ 0x40   /* pc/src/audio/snd.h (its SDL headers do not mix with the decomp's) */
void snd_partner_arms(const void* hd, unsigned int hd_size, const void* bd, unsigned int bd_size);
static int partner_arms(int no)
{
    static int have = -2, ok;
    ADXF h;
    unsigned char* buf;
    int id, n, i;
    if (no == have) return ok;
    have = no;
    ok = 0;
    if (no < 0 || PatId[2] < 0 || (id = SearchAfsInsideFileId(no | 0x4000)) < 0 || !(h = ADXF_OpenAfs(PatId[2], id))) return 0;
    n = ADXF_GetFsizeByte(h);
    buf = n > 0 && n < (8 << 20) ? malloc((size_t)ADXF_GetFsizeSct(h) * 2048) : NULL;
    if (buf) ADXF_ReadNw32(h, ADXF_GetFsizeSct(h), buf);
    if (buf && ADXF_GetStat(h) == 3)   /* SPQ: {offset, size, type, bank} entries up to offset 0; type 2 a shot bank */
        for (i = 0; !ok && (i + 1) * 16 <= n; i++) {
            const int* e = (const int*)buf + i * 4;
            const unsigned int* b = (const unsigned int*)(buf + e[0]);
            if (e[0] <= 0) break;
            if (e[2] != 2 || e[0] > n - 16 || e[1] < 16 || e[1] > n - e[0] || (e[0] & 3)) continue;
            if (b[0] > (unsigned)e[1] || b[1] > (unsigned)e[1] - b[0] || b[2] > (unsigned)e[1] || b[3] > (unsigned)e[1] - b[2] || !b[1] || !b[3]) continue;
            snd_partner_arms((const unsigned char*)b + b[0], b[1], (const unsigned char*)b + b[2], b[3]);
            ok = 1;
        }
    host_log("coop: the partner's weapon sounds (ARMS_%03d): %s\n", no, ok ? "loaded" : "not found");
    free(buf);
    ADXF_Close(h);
    return ok;
}

/* The partner's sounds, once per received pose. The sender's stflg carries this frame's
   footstep (0x200) and shot (0x100) marks (bhControlPlayer clears both each frame). Its voice:
   what its game voiced (Pose.voice_n/voice, protocol 18: hits, death, grabs, events), in the
   voice of the character it appears as (pc_player_voice). Shots: its weapon bank (partner_arms). */
static int partner_voice;               /* (pc_player_voice) */
static void partner_sounds(void)
{
    static unsigned int seq;
    static unsigned char voice_n;
    NJS_POINT3 pos = { ply2.px, ply2.py, ply2.pz };
    int fresh = rx.seq - seq < 10;      /* not after a gap (another room, busy): no stale voice */
    if (rx.seq == seq) return;
    seq = rx.seq;
    if (rx.voice_n != voice_n) {
        NJS_POINT3 keep = PlayerPos;
        voice_n = rx.voice_n;
        PlayerPos = pos;
        partner_voice = 1;
        if (fresh) CallPlayerVoice(0x400 | rx.voice);
        partner_voice = 0;
        PlayerPos = keep;
        if (trace && fresh) fprintf(stderr, "coop: partner voice %d\n", 0x400 | rx.voice);
    }
    if (rx.mset != MSET_OWN) return;   /* events, room motions, grabs: their own sounds */
    if (rx.stflg & 0x200) {
        int m = rx.mtn_no, run = m == 4 || m == 6 || m == 7 || m == 8;   /* PlMtnAct column 3: running */
        CallPlayerFootStepSeEx(bhCheckFloorSound(&ply2, ply2.flr_no, ply2.px, ply2.pz), run, 1, 1, &pos);
        if (trace) fprintf(stderr, "coop: partner footstep (%s)\n", run ? "run" : "walk");
    }
    if ((rx.stflg & 0x100) && rx.wpnr_no >= 0 && rx.wpnr_no < 23) {
        int se = WpnTab[rx.wpnr_no].seno0, bank = WpnTab[rx.wpnr_no].snd_wpno;
        if (se && (se & 0xFF) < SND_PARTNER_TQ && partner_arms(bank)) se += SND_PARTNER_TQ;   /* from the partner's bank */
        else if (plp->wpnr_no < 0 || plp->wpnr_no >= 23 || WpnTab[plp->wpnr_no].snd_wpno != bank) se = 0;   /* not loaded: none rather than ours */
        if (se) CallPlayerWeaponSeEx((NJS_POINT3*)&ply2.mlwP->owP[9].mtx[12], se, 0);
        if (trace) fprintf(stderr, "coop: partner shot (weapon %d, bank %d, se %#x)\n", rx.wpnr_no, bank, se);
        if (rx.wpnr_no == p2_wpn && ply2.mlwP->obj_num > 9) {
            /* the muzzle flash and smoke as the shooter's game makes them (bhSetGunFire), from the
               partner's hand; its room light and the pad vibration are the shooter's own (they
               follow our player), so not here */
            LGT_WORK keep;
            unsigned int st = ply2.stflg;
            int lit = rom && rom->lgt_n > 0;
            if (lit) keep = rom->lgtp[0];
            ply2.stflg &= ~0x40000000u;
            bhSetGunFire(&ply2, rx.wpnr_no, 9, 0, ply2.ay);
            ply2.stflg = st;
            if (lit) rom->lgtp[0] = keep;
        }
    }
}

/* The lighter (weapon 1: item 55 used from the STANDARD slot). The game holds it up with an arm IK
   after the motion (player.c bhControlPlayer: bhArmIkMdk on arm node 6 towards a point in front of
   the chest node 4, the hand node 9 turned), shows a flame effect at the hand (effect 9 on node 9)
   and switches on room light 1 linked to the hand (lgttab[1], a flickering point light). The
   partner gets the same from its pose (weapon 1; the sender's character for the arm offsets): the
   arm IK on ply2, a flame of its own on ply2's hand and light slot 3 (reserved in every room's
   table, switched on by no game code) at ply2's hand - our own lighter keeps slot 1. */
static O_WRK* p2_flame;
static int p2_light;

static void lighter_arm(void)
{
    BH_PWORK* keep = plp;
    NJS_POINT3 eff1 = { (rx.mfile & 3) == 0 ? 1.8f : 2.4f, -3.0f, (rx.mfile & 3) == 0 ? -3.0f : -3.5f }, eff0;
    if (ply2.mlwP->obj_num < 10) return;
    njCalcPoint(&ply2.mlwP->owP[4].mtx, &eff1, &eff0);
    plp = &ply2;   /* bhArmIkMdk reads the model's nodes through plp */
    bhArmIkMdk(&ply2, 6, &eff0, 35498);
    plp = keep;
    ply2.mlwP->objP[9].ang[1] = -910;
    ply2.mlwP->owP[7].flg |= 0x2; ply2.mlwP->owP[8].flg |= 0x2; ply2.mlwP->owP[9].flg |= 0x2;
}

/* at the hand-off: the partner's flame and light while it holds the lighter in our room, drawn */
static void lighter_light(int on)
{
    int alive = p2_flame && (p2_flame->flg & 0x3) && p2_flame->id == 9 && p2_flame->lkwkp == (unsigned char*)&ply2;
    LGT_WORK* lp = rom && rom->lgt_n > 3 ? &rom->lgtp[3] : NULL;
    if (!on) {
        if (alive) p2_flame->flg = 0;
        if (p2_light && lp) lp->flg &= ~0x2;
        p2_flame = NULL;
        p2_light = 0;
        return;
    }
    if (!alive) {   /* the room load clears the effects */
        EF_WORK ef = WpnEffTab[1][0];
        int k;
        ef.type = 1;   /* type 2 follows our own player's lighter (plp->flg 0x1000000): this one we end ourselves */
        k = bhSetEffectTb(&ef, (NJS_POINT3*)&WpnTab[1].wp_fps1, (unsigned char*)&ply2, 9);
        p2_flame = k >= 0 ? &eff[k] : NULL;
    }
    if (lp) {
        if (!p2_light || lp->lkflg) {
            bhSetLightTab(&lgttab[1], 3);
            lp->lkflg = 0;   /* placed here every frame: the light code links only to plp, enemies, objects, items, effects */
            p2_light = 1;
        }
        njCalcPoint(&ply2.mlwP->owP[9].mtx, (NJS_POINT3*)&lp->lx, (NJS_POINT3*)&lp->px);
        lp->flg |= 0x3;
    }
}

static int set_left(const BH_PWORK* ep, const MN_WORK* set);
void coop_control_remote(void)
{
    MN_WORK* set;
    int i, mtn;

    if (fx_open == 1) fx_end(plp, 1);   /* the effects our step started (coop_fx_player) */
    if (!plp || !plp->mnwP) return;
    self_keep();
    actor_double_tick();   /* the cutscene double as our model (its hands before the objects follow them) */
    /* our pose for the partner (sent at the frame hand-off) */
    tx.flags = PF_IN_GAME | (local_visible() ? PF_VISIBLE : 0) | (plp->mnwP == sys->rmthp ? PF_ROOM_MTN : 0);
    tx.mset = plp->mnwP == plp->mnwPb ? MSET_OWN : plp->mnwP == sys->rmthp ? MSET_ROOM : MSET_NONE;
    for (i = 0; tx.mset == MSET_NONE && i < (int)sys->ewk_n && i < PC_ENE_SLOTS; i++)
        if ((ene[i].flg & 0x1) && ene[i].mnwP == plp->mnwP) tx.mset = i;   /* grabbed: the player plays the enemy's set */
    tx.mfile = sys->ply_id + (sys->costume & 3) * 4;
    i = my_chr();   /* ("As played (cutscene)": sent as the cutscene model) */
    tx.chr = !i || i >= COOP_CHR_IMPORT ? sys->ply_id : i == 5 ? 0 : i >= COOP_CHR_ACTOR ? 4 + i - COOP_CHR_ACTOR : i - 1;   /* (an imported model, which the partner may not have: shown as played) */
    tx.costume = !i ? sys->costume & 3 : i == 5;
    tx.stg = sys->stg_no; tx.rom = sys->rom_no; tx.pos = sys->pos_no;
    tx.rcase = (unsigned)my_case() <= 9 ? (uint8_t)(my_case() + 1) : 0;   /* (sys->rcase alone: a door's script picks the next room's before it leaves) */
    tx.px = plp->px; tx.py = plp->py; tx.pz = plp->pz;
    tx.ax = plp->ax; tx.ay = plp->ay; tx.az = plp->az;
    tx.mtn_no = plp->mtn_no; tx.frm_no = actor_frame(plp); tx.mtn_md = plp->mtn_md;   /* the frame shown, not the next one */
    tx.hokan_count = plp->hokan_count; tx.hokan_rate = plp->hokan_rate;
    tx.wpnr_no = plp->wpnr_no; tx.hp = plp->hp;
    tx.mode0 = plp->mode0; tx.mode1 = plp->mode1;
    tx.stflg = plp->stflg;
    tx_in_game = 1;
    if (trace) {   /* per room: enemy work pointers, to compare the memory layout across instances */
        static int last_room = -1, wait;
        if (coop_room_key() != last_room) { last_room = coop_room_key(); wait = 60; }
        if (wait && !--wait) {
            fprintf(stderr, "coop: room %d/%d gfrm=%d memp=%p ewk_n=%d\n", sys->stg_no, sys->rom_no, sys->gfrm_ct, sys->memp, sys->ewk_n);
            for (i = 0; i < (int)sys->ewk_n; i++)
                if (ene[i].flg & 1)
                    fprintf(stderr, "coop:  ene %d id=%d hp=%d dead=%d hidden=%d mlwP=%p objP=%p mnwP=%p mnwPb=%p exp0=%p exp1=%p lk=%p\n", i, ene[i].id,
                            ene[i].hp, (ene[i].flg & 2) != 0, (ene[i].stflg & 0x1000000) != 0,
                            ene[i].mlwP, ene[i].mlwP ? ene[i].mlwP->objP : 0, ene[i].mnwP, ene[i].mnwPb, ene[i].exp0, ene[i].exp1, ene[i].lkwkp);
            {   /* the session state, to compare across instances (M3) */
                unsigned int h = 0, *inv = &sys->itm[sys->ply_id * 16];
                for (i = 0; i < 120; i++) h = h * 31 + (i >= 32 && i < 64 ? 0 : sys->ev_flg[i]);   /* progress words only */
                fprintf(stderr, "coop:  progress flags hash %08x, inventory %08x %08x %08x %08x %08x %08x\n", h, inv[1], inv[2], inv[3], inv[4], inv[5], inv[6]);
                for (h = 0, i = 0; i < rom->wal_n; i++) h = h * 31 + (rom->walp[i].flg & 1);
                for (i = 0; i < rom->etc_n; i++) h = h * 31 + (rom->etcp[i].flg & 1);
                for (i = 0; i < rom->flr_n; i++) h = h * 31 + (rom->flrp[i].flg & 1);
                fprintf(stderr, "coop:  collision hash %08x (walls, triggers, floors on)\n", h);
            }
        }
    }

    /* the partner. One that is busy (cutscene, menu, item box: M8) and was shown in this room
       stays, frozen in its last pose: it is drawn only (enemies do not target it, proxy_ok) */
    if (!have_rx || !local_visible()) return;
    if (!partner_here()) { p2_seen_room = -1; return; }
    p2_busy = (rx.flags & (PF_IN_GAME | PF_VISIBLE)) != (PF_IN_GAME | PF_VISIBLE) ? rx.busy : rx.busy == BUSY_TRADE ? BUSY_TRADE : 0;   /* trading: drawn live, no target */
    if ((rx.flags & (PF_IN_GAME | PF_VISIBLE)) != (PF_IN_GAME | PF_VISIBLE) && (!p2_busy || p2_seen_room != coop_room_key() || p2_chr < 0)) return;
    if (rx.chr < 0 || rx.chr > 3 + ACTOR_MODELS || rx.costume < 0 || rx.costume > 3 || rx.mfile < 0 || rx.mfile > 15) return;
    if (rx.chr != p2_chr || rx.costume != p2_costume || rx.mfile != p2_mfile) {
        int key = (rx.chr * 4 + rx.costume) * 16 + rx.mfile;
        if (p2_fail == key) return;
        if (!load_player(rx.chr, rx.costume, rx.mfile)) { p2_fail = key; return; }
        p2_fail = -1;
    }
    if ((!p2_busy || p2_busy == BUSY_TRADE) && rx.wpnr_no != p2_wpn) {
        load_weapon(rx.wpnr_no);
        if (rx.wpnr_no >= 0 && rx.wpnr_no < 23 && WpnTab[rx.wpnr_no].seno0) partner_arms(WpnTab[rx.wpnr_no].snd_wpno);   /* before its first shot */
    }
    if (p2_busy && p2_busy != BUSY_TRADE) { set = ply2.mnwP; mtn = ply2.mtn_no; goto pose; }   /* the last pose */

    set = rx.mset == MSET_OWN ? ply2.mnwPb : rx.mset == MSET_ROOM ? sys->rmthp :
          rx.mset >= 0 && rx.mset < (int)sys->ewk_n && rx.mset < PC_ENE_SLOTS && (ene[rx.mset].flg & 0x1) ? ene[rx.mset].mnwP : NULL;
    mtn = rx.mtn_no;
    /* the partner's motion numbers index its own sets; never trust them blindly (room and
       enemy sets have 512 entries) */
    if (!set || mtn < 0 || mtn >= (set == ply2.mnwPb ? MAX_MTN : set_left(rx.mset >= 0 ? &ene[rx.mset] : NULL, set)) || !set[mtn].datP || rx.frm_no < 0 || (rx.frm_no >> 16) >= (int)set[mtn].frm_num) return;
    if (mtn != (int)ply2.mtn_no || set != ply2.mnwP) {
        /* a motion change blends from the current pose like on the sender (which already
           took one blend step for this frame) */
        ply2.hokan_count = rx.hokan_count ? rx.hokan_count + 1 : 0;
        ply2.hokan_rate = rx.hokan_rate;
    }
    p2_seen_room = coop_room_key();
    ply2.mnwP = set;
    ply2.mtn_no = mtn;
    ply2.frm_no = rx.frm_no;
    ply2.mtn_md = rx.mtn_md;
    ply2.px = rx.px; ply2.py = rx.py; ply2.pz = rx.pz;
    ply2.ax = rx.ax; ply2.ay = rx.ay; ply2.az = rx.az;
    ply2.draw_tp = plp->draw_tp;
    /* its own floor (enemies attack only on theirs: with ours a partner on another level of the room was
       never attacked); from a build without it, the floor of its height on slopes, else ours */
    ply2.flr_no = rx.flr ? rx.flr - 3 : rom->flg & PC_ROOM_SLOPES ? bhCheckFloorNum(rx.py) : plp->flr_no;
    /* what enemies read when ply2 stands in for the partner (host): also whether its game has it hit or
       held (flg 2/4: other enemies wait, one attack at a time, as for our player) */
    ply2.hp = rx.hp; ply2.stflg = rx.stflg; ply2.mode0 = rx.mode0; ply2.mode1 = rx.mode1;
    ply2.flg = (ply2.flg & ~6u) | (rx.pflg & 6);
    /* its aim pitch: the upper body bent as its third-person free aim bends it (the fixed cameras' aim up/down is
       in its motion), eased over the poses; straight from a build without it */
    p2_pitch += (coop_pitch_get(rx.pflg) * COOP_PITCH_STEP - p2_pitch) * 0.5f;
pose:
    if (!set) return;
    if (p2_other_mesh) keep_bones(&ply2.mdl[0], p2_bone);
    bhSetMotion(&ply2, 0, ply2.mtn_md, ply2.mtn_tp);
    if (p2_wpn == 1) lighter_arm();
    bhCalcModel(&ply2);
    tps_bend(&ply2, p2_pitch);   /* (arms, head, weapons and the muzzle flash follow) */
    /* its hit capsule, as player.c sets ours every frame: most enemies' attacks (dogs, Hunters, Tyrant,
       Bandersnatch, Nosferatu, the zombie's lunge) test it - left at 0,0,0 they all missed the stand-in */
    ply2.watr.c1.x = ply2.mlwP->owP[4].mtx[12]; ply2.watr.c1.y = ply2.mlwP->owP[4].mtx[13]; ply2.watr.c1.z = ply2.mlwP->owP[4].mtx[14];
    ply2.watr.c2.x = ply2.mlwP->owP->mtx[12]; ply2.watr.c2.y = ply2.mlwP->owP->mtx[13]; ply2.watr.c2.z = ply2.mlwP->owP->mtx[14];
    ply2.watr.r = 2.5f;
    actor_source(1, &ply2, p2_chr < 4 ? p2_chr : -1, p2_mfile & 3, p2_pitch);   /* gender-matched animations */
    actor_pose(1, &ply2);
    actor_weapon_hands(1, p2wpn, 2);
    for (i = 0; i < 2; i++) {
        O_WRK* op = &p2wpn[i];
        if (!(op->flg & 0x1) || op->mlwP == &no_model || !op->mlwP || !op->mlwP->objP) continue;
        if (op->lkono == 9) {   /* bhObjWpn: weapon angle in the hand depends on aiming */
            int held = rx.mode1 == 0 || rx.mode0 == 7;
            op->ax = held ? WpnAngTab[op->type][0] : 0;
            op->ay = held ? WpnAngTab[op->type][1] : 0;
            op->az = held ? WpnAngTab[op->type][2] : 0;
        }
        bhCalcModel((BH_PWORK*)op);
    }
    if (p2obj[2].flg & 0x1) {   /* hair: as the object loop does for player 1's */
        O_WRK* op = &p2obj[2];
        njCalcPoint(&ply2.mlwP->owP[op->lkono].mtx, (NJS_POINT3*)&op->lox, (NJS_POINT3*)&op->px);
        if (op->id == 1211) bhObjClpn(op);
        bhCalcModel((BH_PWORK*)op);
    }
    if (!p2_busy) partner_sounds();
    p2_show = 1;
}

/* the name tag above the partner's head node: TAG_ABOVE units for a player (head node about
   TAG_HEAD_Y above the feet), in proportion for a taller model */
#define TAG_ABOVE 4.0f
#define TAG_HEAD_Y 16.0f
void coop_draw_remote(void)
{
    int i;
    if (!p2_show) return;
    pose_trace_mark = 2;   /* test aid RECVX_POSE_TRACE: the partner's draw (file.partner) */
    if (actor_shown(1)) actor_draw(1);
    else actor_put_body(1, &ply2);
    pose_trace_mark = 0;
    for (i = 0; i < 2; i++) {
        O_WRK* op = &p2wpn[i];
        if ((op->flg & 0x1) && op->mlwP != &no_model && op->mlwP && op->mlwP->objP && !(op->mdflg & 0x1)) bhPutModel((BH_PWORK*)op);
    }
    if ((p2obj[2].flg & 0x1) && p2obj[2].mlwP->objP && !(p2obj[2].mdflg & 0x1)) bhPutModel((BH_PWORK*)&p2obj[2]);
    p2_drawn = 1;
    {   /* above its head the overlay writes the partner's name and what it is busy with (name tag);
           the point goes with this frame (stream_point), so in-between frames move it with the 3D */
        float* m = (float*)&ply2.mlwP->owP[5].mtx, hp[3], h, z;
        int behind;
        NJS_POINT3 head = { m[12], m[13], m[14] }, v;
        NJS_POINT2 sc;
        if (actor_shown(1) && actor_head(1, hp)) { head.x = hp[0]; head.y = hp[1]; head.z = hp[2]; }   /* a catalogue model's own head */
        h = fabsf(head.y - ply2.py);
        head.y += (head.y < ply2.py ? -TAG_ABOVE : TAG_ABOVE) * (h > TAG_HEAD_Y ? h / TAG_HEAD_Y : 1.0f);   /* y is up: the head is above py */
        njProjectScreen((NJS_MATRIX*)cam.mtx, &head, &sc);   /* the camera: world -> view */
        njCalcPoint((NJS_MATRIX*)cam.mtx, &head, &v);
        z = v.z;
        {   /* in front of the camera: on the side of our own player (always in view) */
            NJS_POINT3 me = { plp->px, plp->py, plp->pz }, vm;
            njCalcPoint((NJS_MATRIX*)cam.mtx, &me, &vm);
            v.z = vm.z < 0 ? v.z : -v.z;   /* as if "in front" were view z < 0 */
        }
        behind = v.z > -1.0f;
        /* GS coordinates: the screen's first pixel is at 1728/1808 */
        stream_point(sc.x - 1728.0f, sc.y - 1808.0f, z, behind ? STREAM_PT_BEHIND : STREAM_PT_FRONT);
        if (trace && frame_no % 150 == 0) fprintf(stderr, "coop: partner tag (busy %d), head (%.1f %.1f %.1f) view (%.1f %.1f %.1f) at screen %.0f %.0f%s dist %.1f py %.1f\n", p2_busy, head.x, head.y, head.z, v.x, v.y, v.z, sc.x - 1728.0f, sc.y - 1808.0f, behind ? " behind" : "", _nj_screen_.dist, ply2.py);
        g_coop.busy_kind = p2_busy;
    }
}

/* ---------------------------------------------------------------- shared enemies (M2) */
/* The host runs the enemies; per enemy, the nearer player is its target: enemy code reads
   `plp`, so for an enemy that targets the partner, plp is pointed at ply2 (driven by the
   partner's pose) for that enemy's step, and what the enemy did to ply2 is forwarded and
   undone. The client does not run its enemies' AI (except their first, initialising step):
   they are puppets of the host's snapshots, animated locally, and our shots on them go to the
   host as the weapon code's output, applied there once. Only explicit fields are synced: the
   enemy work layout matched across instances in tests, but it depends on the load history. */
static CoopRing ring = { .head = 1, .next = 1 };   /* our events until acknowledged (coop_proto.c) */
static unsigned int in_last;            /* the partner's last event applied here */
static EneSnap snap[MAX_SNAP];
static int snap_n;
static int proxy_ok;                    /* host: the partner is in our room, its model loaded */
static unsigned char tgt[PC_ENE_SLOTS];          /* host: the enemy targets the partner */
static CoopAtk atk[PC_ENE_SLOTS][2];             /* host, the loop guard (coop_atk_note): attacks each enemy started on [0] our player, [1] the partner */
static unsigned int enemy_frame;
static BH_PWORK* swap_plp;
static BH_PWORK ply2_save;
static unsigned char exp_save[256];
static PAD_ACT vib_save[20];            /* the rumbles before a step on the partner: what it starts is undone (yours only) */
static int swap_pid = -1;

/* Voices (sdfunc.c CallPlayerVoice, docs/ACTORS.md "Voices"): a player vocal sound (SE bank 4,
   0x400 + program) in the voice of the character shown (appear_voice). Ours from "You appear as",
   and it goes to the partner (Pose.voice); the partner's while partner_sounds plays it. Returns the
   extra bank: 0 = the game's bank 4 (CORE_<ply_id>) when that is the voice, else that CORE bank
   (snd_xbank_load); -1 = none: a host enemy's step that targets the partner (plp is ply2) - what
   happens to the partner, and its voice, is up to the partner's own game (its grab, its hit). */
int snd_xbank_load(int key);
static int voice_of(int chr, int played) { int v = appear_voice(chr); return v < 0 ? played : v; }
int pc_player_voice(int se)
{
    int own = sys->ply_id, v;
    if (swap_plp && plp == &ply2) return -1;
    if (partner_voice)
        v = voice_of(rx.chr >= 4 ? COOP_CHR_ACTOR + rx.chr - 4 : rx.chr + 1, rx.mfile & 3);   /* (pose chr 0..3: the player files) */
    else {
        v = !g_appear_cutscene && actor_double_acting() ? own : voice_of(my_chr(), own);   /* "Swapped model in cutscenes" off: the double's own */
        tx.voice = (unsigned char)se;
        tx.voice_n++;
    }
    return v == own ? 0 : snd_xbank_load(0xFFF0 | v);
}

/* Grabs (M7). A host enemy that grabs the proxy hands the grab to the client: its state (and
   its linked child's) goes over, the client's game runs that enemy with its own player - the
   grab motions, the struggle, the damage, the release - and the host shows it as a puppet of
   the client's snapshots, until the client hands it back with its state. Regular zombies
   (en01) only; other grabs on the proxy are cancelled as before. owned[i]: the client runs
   slot i (host: for the client; client: ourselves). */
#define ENE_EXP0 0xB0                   /* en01's exp0 (bhEne01_Init); its first 0x24 bytes are pointers */
#define ESTATE_ENE (int)(sizeof(BH_PWORK) + ENE_EXP0)
#define ESTATE_PLY (2 * ESTATE_ENE)
#define ESTATE_SIZE (ESTATE_PLY + (int)sizeof(BH_PWORK))
/* the wire carries these structs raw (the 32-bit game's layout; ene_fields' offsets): a 64-bit build
   would be another wire format, and a set of 512 motions is room.c's 12288 bytes */
_Static_assert(sizeof(BH_PWORK) == 1408 && ESTATE_SIZE <= 64 * (int)sizeof(((CoopXfer*)0)->have), "EV_ESTATE: BH_PWORK as the 32-bit game has it");
_Static_assert(12288 / sizeof(MN_WORK) == COOP_MNW_MAX, "an enemy's motion set: 512 entries");
static unsigned char owned[PC_ENE_SLOTS], owned_child[PC_ENE_SLOTS], estate[ESTATE_SIZE], estate_out[ESTATE_SIZE];   /* received / being sent */
static unsigned short own_wait[PC_ENE_SLOTS];    /* host: the guest's frames in our room without a snapshot of a grab it runs */
static unsigned int snap_at[PC_ENE_SLOTS];       /* guest: our frame at which the slot last took the host's state */
static CoopXfer estate_x;               /* EV_ESTATE chunks into estate[] */
static unsigned short xfer_tid;         /* our last transfer id (blob, estate) */
static void queue_event(Event* e);
static int same_room(void);
static int in_session_game(void);
static int owned_room = -1;
void pc_enemy_holds(BH_PWORK* pl, BH_PWORK* by);   /* eneset.c */

static int ene_slot(void* p) { int k = (BH_PWORK*)p - ene; return p && k >= 0 && k < PC_ENE_SLOTS ? k : 255; }   /* 255: none (wire) */
_Static_assert(PC_ENE_SLOTS == COOP_ENE_SLOTS, "enemy slots: the game's and the protocol's");
/* motion sets travel as an entry offset from the enemy's own set (0..511), or "room set", or "keep" */
static short mnw_of(BH_PWORK* ep)
{
    int d = ep->mnwP - ep->mnwPb;
    return ep->mnwP == sys->rmthp ? MNW_ROOM : ep->mnwP && ep->mnwPb && d >= 0 && d < COOP_MNW_MAX ? d : MNW_KEEP;
}
static void mnw_set(BH_PWORK* ep, int m)
{
    if (m == MNW_ROOM) { if (sys->rmthp) ep->mnwP = sys->rmthp; }
    else if (m >= 0 && m < COOP_MNW_MAX && ep->mnwPb) ep->mnwP = ep->mnwPb + m;
}
static signed char mdl_of(BH_PWORK* ep) { int k = ep->mlwP - ep->mdl; return ep->mlwP && k >= 0 && k < 16 ? k : -1; }
/* entries from `set` to the end of the 512-entry set it lies in: an enemy's own (12288 bytes from
   mnwPb, room.c) or the room's; a set can start mid-way (mnwP = mnwPb + offset) */
static int set_left(const BH_PWORK* ep, const MN_WORK* set)
{
    if (ep && ep->mnwPb && set >= ep->mnwPb && set < ep->mnwPb + COOP_MNW_MAX) return COOP_MNW_MAX - (int)(set - ep->mnwPb);
    if (sys->rmthp && set >= sys->rmthp && set < sys->rmthp + COOP_MNW_MAX) return COOP_MNW_MAX - (int)(set - sys->rmthp);
    return COOP_MNW_MAX;
}
/* motion `mtn` of set `set` (enemy `ep`'s, the room's, or ours) exists and has frame `frm` */
static int mtn_ok(const BH_PWORK* ep, const MN_WORK* set, unsigned int mtn, int frm)
{
    return set && mtn < (unsigned int)set_left(ep, set) && set[mtn].datP && frm >= 0 && (frm >> 16) < (int)set[mtn].frm_num;
}
/* NaN and infinities fail the comparison too */
static int finite3(float x, float y, float z)
{
    return fabsf(x) < 1e6f && fabsf(y) < 1e6f && fabsf(z) < 1e6f;
}

static void ene_pack(BH_PWORK* ep, unsigned char* out)
{
    memcpy(out, ep, sizeof(BH_PWORK));
    if (ep->exp0) memcpy(out + sizeof(BH_PWORK), ep->exp0, ENE_EXP0);
}

/* The sender's enemy state over ours, by an allowlist of BH_PWORK fields: the state the enemy's
   code runs on - modes, position, angles, collision sizes, flags, motion state, counters, damage
   and combo state (whole runs of plain values) - and its exp0 work behind the pointers it starts
   with. Never taken: identity (id, type, model version and count), every pointer (models, motion
   sets, links, work areas, matrices), the slot index, the link node, and the active/linked bits.
   0 when the state is not plausible: nothing is changed then. */
#define FLD(a, b) { offsetof(BH_PWORK, a), offsetof(BH_PWORK, b) }   /* [a, b) */
static const struct { unsigned short from, to; } ene_fields[] = {
    FLD(flr_no, mdlver), FLD(param, mdl_n),          /* floor; param, modes .. draw_tp (no id/type/mdlver/mdl_n) */
    FLD(shp_ct, mnwP),                               /* shp_ct, clp_jno, lok_jno */
    FLD(mtn_attr, mtn_tp),                           /* motion state */
    FLD(axp, idx_ct), FLD(mtn_chk, cpcl),            /* .. ct0-3, weapons, at_flg, hit capsule (no slot index) */
    FLD(wax, kdnp), FLD(kdnidx, dan_ap),             /* angles, hp, damage, hit data, sounds */
    { offsetof(BH_PWORK, comb_flg), sizeof(BH_PWORK) },   /* combo state, total damage */
};
static int ene_unpack(BH_PWORK* ep, const unsigned char* in, int mdl, int mnw)
{
    const BH_PWORK* r = (const BH_PWORK*)in;
    const MN_WORK* set = mnw >= 0 && mnw < COOP_MNW_MAX && ep->mnwPb ? ep->mnwPb + mnw : mnw == MNW_ROOM ? sys->rmthp : ep->mnwP;
    unsigned int k;
    if (!finite3(r->px, r->py, r->pz) || !finite3(r->pxb, r->pyb, r->pzb) || r->hp < -10000 || r->hp > 100000 ||
        (mdl >= 0 && (mdl >= 16 || !ep->mdl[mdl].objP)) || !mtn_ok(ep, set, r->mtn_no, r->frm_no))
        return 0;
    for (k = 0; k < sizeof(ene_fields) / sizeof(ene_fields[0]); k++)
        memcpy((char*)ep + ene_fields[k].from, in + ene_fields[k].from, ene_fields[k].to - ene_fields[k].from);
    ep->flg = (r->flg & ~0x81u) | (ep->flg & 0x81u);
    if (mdl >= 0) ep->mlwP = &ep->mdl[mdl];
    mnw_set(ep, mnw);
    if (ep->exp0) memcpy(ep->exp0 + 0x24, in + sizeof(BH_PWORK) + 0x24, ENE_EXP0 - 0x24);   /* the first 0x24 bytes: pointers */
    return 1;
}
#undef FLD

/* our grab state to the partner: the EV_ESTATE chunks of one transfer, whose id goes with EV_GRAB/UNGRAB */
static unsigned short send_estate(int n)
{
    int off;
    if (!++xfer_tid) xfer_tid = 1;
    for (off = 0; off < n; off += 64) {
        Event e = { 0 };
        e.type = EV_ESTATE;
        e.u.blob.tid = xfer_tid;
        e.u.blob.total = n;
        e.u.blob.off = off;
        e.u.blob.len = n - off < 64 ? n - off : 64;
        memcpy(e.u.blob.data, estate_out + off, e.u.blob.len);
        queue_event(&e);
    }
    return xfer_tid;
}

/* an en01 grab hold: set when the grab starts, cleared by every release (~0x24080, ~0xA4080) */
static int holding(BH_PWORK* ep) { return ep->exp0 && (*(int*)(ep->exp0 + 0x40) & 0x20000); }

/* Our player's busy flag (flg 4, with 0x10000) is an enemy's attack or grab under way: the inventory, the map,
   examining, doors and pickups wait until the enemy that set it clears it (bhCheckSubTask, bhControlPlayer). In
   single player that enemy always does. In co-op its steps could stop running against our player first: the
   host gave a busy player's enemy to the free partner (coop_pick_target: "a busy target is left for the free
   player"; a zombie's grab start or its counter-grab after a knife hit sets the flag before the grab plays),
   and the guest's own enemies became the host's puppets again when the host came back from a menu or into the
   room. The release then ran on the partner's stand-in, or never: our player walked on but could open nothing
   until a room change cleared the flag (a teleport, bhInitRoomChangePlayer). So the enemy whose step set it
   (hold.by), its parts and its type's leader (the moth's and the Black Widow's grab is played by the leader)
   keep our player: the host's go for it, the guest's run here, until the flag clears. hold_watch: the safety
   net. */
static struct { int by, id, room, said; unsigned int seen, orphan; } hold = { -1, 0, -1, 0, 0, 0 };
static int hold_family(BH_PWORK* ep)
{
    BH_PWORK* h;
    if (hold.by < 0 || !plp || !(plp->flg & 4)) return 0;
    h = &ene[hold.by];
    if (!(h->flg & 0x1) || h->id != hold.id) return 0;
    return pc_root_in(ep, ene, PC_ENE_SLOTS) == pc_root_in(h, ene, PC_ENE_SLOTS) || (!ep->lkwkp && ep->id == h->id && (h->id == 3 || h->id == 6));
}
/* linked to a player (lkwkp): the moth's larva rides on the player it was laid on (en27, lkwkp = plp at its
   Init) and plays that player's motions; it goes for that player only. Before, it was retargeted like any
   enemy: the host it held counted as busy, the larva went for the free partner standing by and threw itself
   off the stand-in, while the host stayed in its throw-off motion for good. 1 ours, 2 the partner's */
static int linked_to(const BH_PWORK* ep) { return !plp ? 0 : ep->lkwkp == (unsigned char*)(swap_plp ? swap_plp : plp) ? 1 : ep->lkwkp == (unsigned char*)&ply2 ? 2 : 0; }
/* guest: an enemy of ours that holds our player or rides on it keeps running here (as grips_us) */
static int runs_here(BH_PWORK* ep)
{
    if (linked_to(ep) == 1) return 1;
    if (!hold_family(ep)) return 0;
    if (!hold.said++) host_log("coop: enemy %d (id %d) holds our player: it runs here until it lets go\n", (int)(ep - ene), ep->id);
    return 1;
}
static unsigned int h_stflg;            /* our player's stflg before the step (a larva laid) */
static int h_busy, h_ran;               /* our player's busy flag before the step; the step ran here */
/* the step laid larvae on a player (stflg 0x8000000 << type, en06 NG00): they go for that player from their
   Init (it links them to plp), whatever the slot targeted before */
static void larva_laid(unsigned int bits, int partner)
{
    int k, t, n = 0;
    for (k = 0; partner && bits && k < (int)sys->ewk_n && k < PC_ENE_SLOTS; k++) n += (ene[k].flg & 0x1) && ene[k].id == 27 && linked_to(&ene[k]) == 2;
    for (k = 0; bits && k < (int)sys->ewk_n && k < PC_ENE_SLOTS; k++)
        if ((ene[k].flg & 0x1) && ene[k].id == 27 && !ene[k].mode0 && (t = ene[k].type) < 3 && (bits >> t & 0x8000000)) {
            tgt[k] = partner;
            /* the stand-in's larva bits are the partner's pose (its game has none): at most three, as the bits allow */
            if (partner && n++ >= 3) ene[k].flg = 0;
        }
}

/* neither side has the grab's state any more (the client left, timed out, rejected it), or the
   grab was refused here (eneset.c's one-holder rule) or froze (the watchdog): the enemy goes back
   to standing (en01 MV00: idle motion, then the chase), released, as at the end of a grab. Its
   linked lower body runs the same modes (en01 copies them at every change): it goes along. */
static void unhold(BH_PWORK* ep, int dead)
{
    if (!ep->exp0 || ep->id != 1) return;
    *(int*)(ep->exp0 + 0x40) &= ~0xA4080;
    if (!dead) {
        *(int*)&ep->mode0 = 1;
        ep->flg2 &= ~1;
        ep->flg |= 0x40;   /* the player collides with it again (the grab cleared it, its end sets it) */
    }
    if (ene_slot(ep->exp1) < PC_ENE_SLOTS && (((BH_PWORK*)ep->exp1)->flg & 0x80)) unhold((BH_PWORK*)ep->exp1, dead);
}
/* A dead one only lets go, its death goes on: put back to standing, a crawler killed by the player's
   stomp at the end of its leg grab (en01 NG02 sets hp -1 while the player still plays the stomp) walked
   again with hp -1 and grabbed again - one grab after another, for good (the failsafe released it, the
   grab started again, the failsafe released it ...). */
void pc_enemy_unhold(BH_PWORK* ep) { unhold(ep, ep->hp < 0); }

/* host: a grab the guest runs comes back here when the guest, sending, has not sent a snapshot of it
   for OWN_LOST of its frames (~10 s): its game never took the grab, lost it, or is not running it (the
   guest died in it and sits at the game over). Without this, a grab whose hand-back never came stayed
   the guest's here - a frozen puppet - for good. (A guest in another room in game: owned_drop.) */
#define OWN_LOST 300
static void own_timeout(void)
{
    static unsigned int seq;
    int i, c;
    if (!is_host || !sys || !have_rx || rx.seq == seq) return;
    seq = rx.seq;
    for (i = 0; i < PC_ENE_SLOTS; i++) {
        if (!owned[i] || (ene[i].flg & 0x80) || ++own_wait[i] < OWN_LOST) continue;
        c = owned_child[i];
        owned[i] = 0; owned_child[i] = 255; own_wait[i] = 0;
        if (c < PC_ENE_SLOTS) owned[c] = 0;
        if (ene[i].flg & 0x1) pc_enemy_unhold(&ene[i]);
        host_log("coop: enemy %d: no word from the partner's grab for %d of its frames, ours again: released\n", i, OWN_LOST);
    }
}

/* host: drop what the client runs (it left the room, timed out, or the session ended) */
static void owned_drop(void)
{
    int i, n = 0, here = sys && owned_room == coop_room_key();
    for (i = 0; i < PC_ENE_SLOTS; i++)
        if (owned[i]) { if (is_host && here && (ene[i].flg & 0x1)) { pc_enemy_unhold(&ene[i]); n++; } owned[i] = 0; }
    if (n) host_log("coop: %d enemies of the partner's grabs are ours again: released\n", n);
    memset(owned_child, 255, sizeof(owned_child));
    owned_room = -1;
}

/* host, after the step of an enemy targeting the proxy that started a grab on it */
static void grab_out(int i, BH_PWORK* ep)
{
    BH_PWORK* a = &ply2_save;
    int c = ene_slot(ep->exp1);
    Event e = { 0 };
    memset(estate_out, 0, sizeof(estate_out));
    ene_pack(ep, estate_out);
    if (c < PC_ENE_SLOTS) ene_pack(&ene[c], estate_out + ESTATE_ENE);
    memcpy(estate_out + ESTATE_PLY, &ply2, sizeof(BH_PWORK));
    e.u.gr.tid = send_estate(ESTATE_SIZE);
    e.type = EV_GRAB;
    e.idx = i;
    e.u.gr.child = c;
    e.u.gr.state = 1;
    e.u.gr.pset = ply2.mnwP == ep->mnwP;
    e.u.gr.mdl = mdl_of(ep); e.u.gr.mnw = mnw_of(ep);
    e.u.gr.cmdl = -1; e.u.gr.cmnw = MNW_KEEP;
    if (c < PC_ENE_SLOTS) { e.u.gr.cmdl = mdl_of(&ene[c]); e.u.gr.cmnw = mnw_of(&ene[c]); }
    e.u.gr.flg_b = a->flg; e.u.gr.stflg_b = a->stflg; e.u.gr.flg2_b = a->flg2;
    e.u.gr.exp1_b = *(int*)(exp_save + 128); e.u.gr.exp1_a = *(int*)p2.plhdw;
    queue_event(&e);
    owned[i] = 1;
    owned_child[i] = c;
    own_wait[i] = 0;
    if (c < PC_ENE_SLOTS) owned[c] = 1;
    owned_room = coop_room_key();
    host_log("coop: enemy %d grabs the partner, handed to the partner's game\n", i);
}

/* client: give a grab back (state: our enemy's state goes along). c: the grab's linked child - the
   one the host named for a grab refused here: a protocol 11 host ends the grab only with the child
   it handed over, and before, a refusal sent none (255), so such a zombie stayed the guest's on the
   host, the host's here, and frozen on both */
static void grab_back(int i, int state, int c)
{
    Event e = { 0 };
    e.u.gr.mdl = e.u.gr.cmdl = -1;
    e.u.gr.mnw = e.u.gr.cmnw = MNW_KEEP;
    if (state) {
        memset(estate_out, 0, sizeof(estate_out));
        ene_pack(&ene[i], estate_out);
        if (c < PC_ENE_SLOTS) ene_pack(&ene[c], estate_out + ESTATE_ENE);
        e.u.gr.tid = send_estate(ESTATE_PLY);
        e.u.gr.mdl = mdl_of(&ene[i]); e.u.gr.mnw = mnw_of(&ene[i]);
        if (c < PC_ENE_SLOTS) { e.u.gr.cmdl = mdl_of(&ene[c]); e.u.gr.cmnw = mnw_of(&ene[c]); }
    }
    e.type = EV_UNGRAB;
    e.idx = i;
    e.u.gr.child = c;
    e.u.gr.state = state;
    queue_event(&e);
    if (i < PC_ENE_SLOTS) owned[i] = 0;
    if (c < PC_ENE_SLOTS) owned[c] = 0;
    host_log("coop: the grab of enemy %d ends, handed back%s\n", i, state ? "" : " without its state");
}

/* a wall between p and q at waist height (the game's bullet ray test; its hit globals are preserved): a
   position taken from the partner's game must not put our player through one (the wall check only pushes
   out of walls the player overlaps) */
static int wall_between(const BH_PWORK* p, const BH_PWORK* q)
{
    NJS_LINE line;
    NJS_POINT3 apos = sys->apos;
    ATR_WORK* ahtp = sys->ahtp;
    float len;
    int hit;
    line.px = p->px; line.py = p->py + 10.0f; line.pz = p->pz;
    line.vx = q->px - p->px; line.vy = q->py - p->py; line.vz = q->pz - p->pz;
    hit = (line.vx != 0 || line.vz != 0) && bhCheckL2Wall(&line, 0x400, &len);
    sys->apos = apos; sys->ahtp = ahtp;
    return hit;
}

/* client: the host's enemy grabbed us. Taken only as a whole: the transfer the grab names is
   complete, the slot holds a zombie here (en01, the only grab handed over) with the same linked
   child, the state is plausible and its position not behind a wall; otherwise the host gets the
   grab back and lets go. */
static void grab_in(const Event* e)
{
    const BH_PWORK* a = (const BH_PWORK*)(estate + ESTATE_PLY);
    const MN_WORK* set;
    BH_PWORK* ep;
    int i = e->idx, c;
    unsigned int x;
    if (i >= PC_ENE_SLOTS || i >= (int)sys->ewk_n || e->stg != sys->stg_no || e->rom != sys->rom_no || !(ene[i].flg & 0x1) ||
        !tx_in_game || (sys->cb_flg & 0x5) || plp->mode0 == 3 || plp->mode0 == 4 || plp->mode0 == 6 || plp->hp < 0 || owned[i] ||
        (a->px - plp->px) * (a->px - plp->px) + (a->pz - plp->pz) * (a->pz - plp->pz) > 10.0f * 10.0f) {   /* late: we moved on */
        host_log("coop: enemy %d grabs us, but not now (room %d, active %d, in game %d, cb %x, mode %d, hp %d, owned %d, %.1f m off)\n", i, e->stg == sys->stg_no && e->rom == sys->rom_no, i < PC_ENE_SLOTS ? ene[i].flg & 1 : 0, tx_in_game, sys->cb_flg & 5, plp->mode0, plp->hp, i < PC_ENE_SLOTS ? owned[i] : 0, sqrtf((a->px - plp->px) * (a->px - plp->px) + (a->pz - plp->pz) * (a->pz - plp->pz)) / 10);
        grab_back(i, 0, e->u.gr.child);
        return;
    }
    ep = &ene[i];
    c = ene_slot(ep->exp1);
    set = !e->u.gr.pset ? plp->mnwPb : e->u.gr.mnw >= 0 && e->u.gr.mnw < COOP_MNW_MAX && ep->mnwPb ? ep->mnwPb + e->u.gr.mnw
                                     : e->u.gr.mnw == MNW_ROOM ? sys->rmthp : ep->mnwP;
    if (!estate_x.done || estate_x.tid != e->u.gr.tid || estate_x.total != ESTATE_SIZE || ep->id != 1 || !ep->exp0 ||
        c != e->u.gr.child || (c < PC_ENE_SLOTS && !(ene[c].flg & 0x1)) || !finite3(a->px, a->py, a->pz) || !mtn_ok(ep, set, a->mtn_no, a->frm_no) ||
        wall_between(plp, a) || !ene_unpack(ep, estate, e->u.gr.mdl, e->u.gr.mnw)) {
        host_log("coop: enemy %d grabs us, but its state does not fit here\n", i);
        grab_back(i, 0, e->u.gr.child);
        return;
    }
    if (c < PC_ENE_SLOTS && ene_unpack(&ene[c], estate + ESTATE_ENE, e->u.gr.cmdl, e->u.gr.cmnw)) owned[c] = 1;
    owned[i] = 1;
    owned_room = coop_room_key();
    /* our player as the grab's start left the proxy: modes, motion, position, and the bits
       the enemy changed */
    *(int*)&plp->mode0 = *(int*)&a->mode0;
    plp->ct0 = a->ct0; plp->ct1 = a->ct1; plp->ct2 = a->ct2; plp->ct3 = a->ct3;
    plp->mtn_no = a->mtn_no; plp->frm_no = a->frm_no; plp->mtn_add = a->mtn_add; plp->mtn_md = a->mtn_md;
    plp->hokan_count = a->hokan_count; plp->hokan_rate = a->hokan_rate;
    plp->px = a->px; plp->py = a->py; plp->pz = a->pz;
    plp->ax = a->ax; plp->ay = a->ay; plp->az = a->az; plp->ayp = a->ayp;
    plp->at_flg = a->at_flg;
    plp->flg = (plp->flg | (a->flg & ~e->u.gr.flg_b)) & ~(e->u.gr.flg_b & ~a->flg);
    /* the grab's start (en01 bhEne01_PlyDG00/01) let go of the planted foot (0x40000, bhFixPosition) on the proxy,
       which never has it: ours, set by an aimed weapon or the knife, moved us by our last pose's foot every frame
       of the grab - away from the zombie, into walls and through them */
    plp->flg = (plp->flg & ~0x40000) | (a->flg & 0x40000);
    plp->stflg = (plp->stflg | (a->stflg & ~e->u.gr.stflg_b)) & ~(e->u.gr.stflg_b & ~a->stflg);
    plp->flg2 = (plp->flg2 | (a->flg2 & ~e->u.gr.flg2_b)) & ~(e->u.gr.flg2_b & ~a->flg2);
    x = *(unsigned int*)plp->exp1;
    *(unsigned int*)plp->exp1 = (x | (e->u.gr.exp1_a & ~e->u.gr.exp1_b)) & ~(e->u.gr.exp1_b & ~e->u.gr.exp1_a);
    plp->mnwP = (MN_WORK*)set;
    pc_enemy_holds(plp, ep);   /* eneset.c: with added enemies one enemy at a time holds a player - this one */
    hold.by = i; hold.id = ep->id; hold.room = coop_room_key(); hold.seen = frame_no; hold.said = 1;
    host_log("coop: enemy %d grabs us\n", i);
}

/* client, at the hand-off: a failsafe for our player in a grab state with no living enemy here
   holding it (the holder died, left, or stopped running here): after half a second it is let
   go as en01 lets go (after 5 s when the holder is a dying zombie that still holds: a crawler's leg
   grab ends with the player stomping it dead and lets go at the end of the stomp). Also guards the position: when a grab ends, a player more than 2.5 m from
   where it was last in normal control is put back there (a grab only pushes a little).
   The host runs it too (without the position guard): its player on an enemy's motion set with no
   enemy holding it whose steps run against the host's player (not the partner's stand-in, not a grab
   the partner's game runs) - what an enemy that changed its victim mid-grab left behind. */
/* our player to x/z, its collision points along (as cheat_glue.c's test warp) */
static void move_player(float x, float z)
{
    EXP_WORK* e = (EXP_WORK*)plp->exp0;
    float dx = x - plp->px, dz = z - plp->pz;
    plp->px += dx; plp->pxb += dx; plp->pz += dz; plp->pzb += dz;
    plp->flg &= ~0xC0000;
    if (e) { e->fpx += dx; e->bpx += dx; e->bpxb += dx; e->spx += dx; e->plx += dx; e->fpz += dz; e->bpz += dz; e->bpzb += dz; e->spz += dz; e->plz += dz; }
}

static int grips_us(BH_PWORK* ep);
static int event_driven(const BH_PWORK* ep);
/* our player out of a grab, as en01 releases it */
static void player_let_go(void)
{
    sys->pad_on &= ~0xF;
    plp->flg &= ~0x10004;
    plp->flg |= 8;
    plp->stflg &= ~0x50000;
    plp->at_flg = 0;
    plp->mnwP = plp->mnwPb;
    *(int*)&plp->mode0 = 1;
}
/* where our player last was in normal control (grab_failsafe's position guard), in which room of
   which game: a point of another room - kept over a door, a teleport, a regroup, a game restart or a
   session end - is no place to put the player back to (that put it outside the map) */
static struct { float x, y, z; int key, ok, held; } safe;

static void grab_failsafe(void)
{
    static int orphan;
    int i, held = 0, dying = 0, grabbed;
    static const char* env = (const char*)-1;
    unsigned int f;
    if (env == (const char*)-1) env = getenv("RECVX_COOP_ORPHAN");   /* test aid "frame": a grab with no holder, 3 m off */
    if (env && sscanf(env, "%u", &f) == 1 && test_clock() >= f && tx_in_game && plp->mode0 == 1)
        for (i = 0; i < (int)sys->ewk_n && i < PC_ENE_SLOTS; i++)
            if ((ene[i].flg & 0x1) && ene[i].mnwP && ene[i].id == 1 && !holding(&ene[i])) {
                host_log("coop: test: our player put in a grab of enemy %d that does not hold it\n", i);
                plp->mnwP = ene[i].mnwP; plp->mode0 = 4; move_player(plp->px, plp->pz + 30.0f);
                env = NULL;
                break;
            }
    if (safe.ok && (safe.key != coop_room_key() || (sys->tk_flg & 0x28))) memset(&safe, 0, sizeof(safe));   /* another room, a (re)start */
    if (!tx_in_game || (sys->cb_flg & 0x5) || plp->hp < 0) { orphan = 0; return; }
    /* the host: only a grab on an enemy's motion set (a grab one of its enemies runs on our player's own
       set is left alone) */
    grabbed = (!is_host && (plp->mode0 == 4 || plp->mode0 == 6)) || (plp->mnwP != plp->mnwPb && plp->mnwP != sys->rmthp);
    if (!grabbed) {
        if (safe.held && safe.ok && !is_host) {   /* (the guest's guard: puppets' positions dragged it) */
            float dx = plp->px - safe.x, dz = plp->pz - safe.z;
            if (dx * dx + dz * dz > 25.0f * 25.0f) {
                host_log("coop: after a grab we were %.0f units away: back to (%.1f %.1f)\n", sqrtf(dx * dx + dz * dz), safe.x, safe.z);
                move_player(safe.x, safe.z);
                plp->py = safe.y;
            }
        }
        safe.held = orphan = 0;
        if (plp->mode0 == 1) { safe.x = plp->px; safe.y = plp->py; safe.z = plp->pz; safe.key = coop_room_key(); safe.ok = 1; }
        return;
    }
    safe.held = 1;
    for (i = 0; i < (int)sys->ewk_n && i < PC_ENE_SLOTS; i++)
        /* (a zombie walking or standing - its Move, mode0 1 - holds nobody, whatever its hold bit says: a guest held by one zombie
           and hit meanwhile by another's vomit, which went on when the host left mid-attack, was knocked out of the grab onto
           the holder's motion set, where its frame stood still, and the holder walked off with the hold bit set: the player
           stayed there for good) */
        if ((ene[i].flg & 0x1) && (grips_us(&ene[i]) || linked_to(&ene[i]) == 1) && (!is_host || (!tgt[i] && !owned[i])) &&
            !(ene[i].id == 1 && ene[i].mode0 == 1 && !linked_to(&ene[i]))) {   /* on the host, a holder whose steps run on the partner's stand-in or the partner's game does not hold us; a larva holds us on our own motions */
            if (ene[i].hp >= 0) held = 1;   /* hp 0 is alive: en01 sets a grabbing zombie shot below 0 to 0 */
            else dying = 1;   /* a crawler the player stomps to death (NG02): it lets go when the stomp ends */
        }
    if (held || event_driven(plp) || (plp->mnwP == plp->mnwPb && plp->mode0 != 4 && plp->mode0 != 6)) { orphan = 0; return; }
    if (++orphan < (dying ? 150 : 15)) return;
    host_log("coop: our player was left in a grab (mode %d, no enemy holding it): let go\n", plp->mode0);
    for (i = 0; i < PC_ENE_SLOTS; i++)
        if (owned[i] && !(ene[i].flg & 0x80))
            host_log("coop:   our grab of enemy %d: hp %d, hold %d, modes %d %d %d %d, motion %u, our set %s its set (own %d)\n", i, ene[i].hp, holding(&ene[i]),
                     ene[i].mode0, ene[i].mode1, ene[i].mode2, ene[i].mode3, ene[i].mtn_no, plp->mnwP == ene[i].mnwP ? "is" : "is not", plp->mnwP == plp->mnwPb);
    player_let_go();
    for (i = 0; i < PC_ENE_SLOTS; i++)
        if (ene[i].exp0 && ene[i].id == 1 && (ene[i].flg & 0x1) && holding(&ene[i]) && (!is_host || (!tgt[i] && !owned[i]))) pc_enemy_unhold(&ene[i]);
    orphan = 0;
}

/* Safety net for our player's busy flag (hold, above), both sides: set while our player is in normal control
   (or held on its own motions, as the larva holds it) with no enemy of the holder's family running against it
   for a second, it is cleared after HOLD_ORPHAN frames, as the enemies clear it, and logged with who set it. */
#define HOLD_ORPHAN 60
static void hold_watch(void)
{
    BH_PWORK* h = hold.by >= 0 ? &ene[hold.by] : NULL;
    int own = (plp->mode0 == 4 || plp->mode0 == 6) && plp->mnwP == plp->mnwPb;
    if (hold.room != coop_room_key() || !(plp->flg & 4)) { hold.by = -1; hold.room = coop_room_key(); h = NULL; }   /* (let go: the next flag is someone else's) */
    if (!tx_in_game || (sys->cb_flg & 0x5) || plp->hp < 0 || frozen || !(plp->flg & 4) || (plp->mode0 != 1 && !own) || event_driven(plp) ||
        (h && hold_family(h) && frame_no - hold.seen < 30)) { hold.orphan = 0; return; }
    if (++hold.orphan < HOLD_ORPHAN) return;
    host_log("coop: watchdog: our player was left busy (flg %x stflg %x, modes %d %d %d %d, motion %u) for %d frames, set by %s %d (id %d, %s, last step on us %u frames ago%s): cleared\n",
             plp->flg, plp->stflg, plp->mode0, plp->mode1, plp->mode2, plp->mode3, plp->mtn_no, HOLD_ORPHAN, h ? "enemy" : "no enemy", hold.by, h ? h->id : -1,
             !h ? "-" : !(h->flg & 0x1) || h->id != hold.id ? "gone" : h->hp < 0 ? "dead" : "alive", frame_no - hold.seen,
             !h || !is_host ? "" : tgt[hold.by] ? ", targets the partner" : owned[hold.by] ? ", the partner runs it" : "");
    if (own) player_let_go();
    else { plp->flg &= ~0x10004; plp->stflg &= ~0x10000; }
    hold.by = -1;
    hold.orphan = 0;
}

/* client, at the hand-off: a grab we run is over when the enemy lets go (or is gone) */
static void grab_check(void)
{
    int i;
    for (i = 0; i < PC_ENE_SLOTS; i++) {
        BH_PWORK* ep = &ene[i];
        if (!owned[i] || ep->flg & 0x80) continue;   /* children go with their parent */
        if (owned_room != coop_room_key() || i >= (int)sys->ewk_n || !(ep->flg & 0x1)) grab_back(i, 0, ene_slot(ep->exp1));
        else if (!holding(ep) && plp->mnwP != ep->mnwP && tx_in_game) grab_back(i, 1, ene_slot(ep->exp1));
    }
}

/* The ring holds every event the partner has not acknowledged; nothing is dropped from it. When it
   is full (a partner that stopped acknowledging for a long time), what follows cannot be ordered
   behind it: the frame's further events are not queued and the frame ends with a resync (resync():
   a new session id, both streams start again, the host's game state goes over) - deliberate and the
   same on both sides, instead of a stream that waits forever for an event that was dropped. */
static void queue_event(Event* e)
{
    int was = ring.overflow;
    e->stg = sys->stg_no;
    e->rom = sys->rom_no;
    if (!coop_ring_push(&ring, e)) {
        if (!was) host_log("coop: %d events not acknowledged by the partner: resynchronising\n", COOP_RING);
        return;
    }
    if (trace) fprintf(stderr, "coop: event %u out type=%d idx=%d\n", e->id, e->type, e->idx);
}

/* host: the partner held by an enemy whose hold plays a sequence on its victim (seq_hold): the Tyrant's blow
   takes the player over (mode 5, his motions, en19.c bhEne19_PlyMoveMain) until the victim's motion has run
   (bhEne_PlyActionMain), Alexia's first form burns it (mode 6, en12.c bhEne12_NG00). Run on the stand-in, which
   is put back after every step, such a hold never ended: the Tyrant waited for good with his victim "held" and
   harmed nobody again, Alexia stood still. The hold runs on this copy of the stand-in instead, its motion
   advanced as the player's own (bhSetMotion's frame step), until the enemy lets go; what it does to the copy
   goes to the partner's game as hits (and death), as other enemies' attacks. One at a time. */
static struct { BH_PWORK pw, was; int slot, key; unsigned int t; } ghost = { .slot = -1 };   /* key: the room load (enemy_room_gen) */
/* A frame without enemy steps (loading, menus) may have replaced the room's enemies: forget
   the retargeting. */
static void enemy_epoch(void)
{
    if (frame_no - enemy_frame > 1) { memset(tgt, 0, sizeof(tgt)); memset(atk, 0, sizeof(atk)); }
}

static int same_room(void) { return have_rx && (rx.flags & PF_IN_GAME) && partner_here(); }
/* not in a door transition, nor in a cutscene of ours (M8): its event drives the enemies and waits
   for their motions (FrameCheck), which a puppet only shows as snapshots let it - they run here
   then and take the host's state again afterwards */
static int puppets(void) { return !echo_delay && !is_host && same_room() && snap_n > 0 && !(sys->cb_flg & 0x5); }
/* host: our examine - a trigger's message or close-up (hitchk.c: st_flg 0x2000 / 0x80) - stops the game's enemies
   (sp_flg 0x2 off until it closes): in single player the world waits for the one player reading. The partner in our room
   plays on, and our enemies stood still in its game (its puppets of ours), its shots on them taking effect only when we
   closed the text. While the partner plays in our room (a target: proxy_ok) our enemies run during our examine (not in a
   door, a cutscene, the trade screen); our player counts as busy for them meanwhile, as one paused: no enemy attacks it
   (enemy_begin). */
int coop_enemies_run(void)
{
    return is_host && !echo_delay && sys && plp && (sys->st_flg & 0x2080) && !(sys->cb_flg & 0x5) && !(sys->gm_flg & 0x9) && !frozen &&
           proxy_ok && same_room();
}
static int examining;                   /* host: this enemy step runs although our examine paused the enemies */

/* an event task works on this enemy (LoadWork): it runs here, as in a cutscene */
extern BH_SCEWORK bhEtask[16];
extern BH_SCEWORK* bhCetask;            /* event.c: the event task running now */
static int event_driven(const BH_PWORK* ep)
{
    int i;
    for (i = 0; i < 16; i++)
        if (bhEtask[i].status && bhEtask[i].work == ep) return 1;
    return 0;
}

/* client: this enemy holds our player (it plays the enemy's motion set; of zombies, which share a
   set, the one with the hold bit). Such an enemy keeps running here until it lets go, whatever
   made it run here (no snapshots for a while: the host in a menu, a time-out, our cutscene, an
   event). Before, it became a puppet again mid-grab: its AI stopped, so nothing ever released
   the player - stuck in the grab motion, then dragged along by the puppet's snapshots.
   Helper parts (ids 31..38: a zombie's limbs, Monster Steve's arms) share their parent's set but
   hold nothing: counted as holders, they kept every orphaned zombie grab "held". */
static int grips_us(BH_PWORK* ep)
{
    return !(ep->id >= 31 && ep->id <= 38) && plp && plp->mnwP && plp->mnwP != plp->mnwPb && plp->mnwP != sys->rmthp && plp->mnwP == ep->mnwP &&
           (ep->id != 1 || holding(ep));
}

/* puppet: the other side's motion and frame, our joints and matrices. Only for an enemy that
   owns its model: helper enemies (e.g. a zombie's parts, ids 31..38) point mlwP at their
   parent's model and must not animate it. */
static int puppet_step(BH_PWORK* ep)
{
    if (ep->mlwP < ep->mdl || ep->mlwP >= ep->mdl + 16) return 1;
    /* (a set has 512 motions; a linked part with a model of its own - a zombie's cap or bomb -
       shares its parent's set, whose motions have more tracks than the part has nodes: playing
       one writes past its nodes, which crashed the guest; it follows its link) */
    if (ep->mnwP && ep->mtn_no < (unsigned int)set_left(ep, ep->mnwP) && ep->mnwP[ep->mtn_no].datP && ep->frm_no >= 0 &&
        (ep->frm_no >> 16) < (int)ep->mnwP[ep->mtn_no].frm_num && !(ep->flg & 0x80) &&
        ep->mnwP[ep->mtn_no].obj_num <= ep->mlwP->obj_num)
        bhSetMotion(ep, 0, ep->mtn_md, ep->mtn_tp);
    bhCalcModel(ep);
    return 1;
}

/* being hit or held by an enemy (flg 4; the partner's from its pose), or dying */
int bhCdirCheck(int my_ang, int trg_ang);   /* zonzon.c */
static int ply_busy(const BH_PWORK* pl) { return (pl->flg & 4) || pl->mode0 == 4 || pl->mode0 == 6 || pl->hp < 0; }
/* host: a hit sent to the partner shows in its pose (flg 4, its reaction) a round trip later; until then the stand-in,
   put back after every step, was free again at the next step and an enemy whose blow lasts several frames hit it
   in each of them (Alexia's babies three times at once, the Gulp Worm's swing). As the hit player in single player,
   the stand-in is busy (flg 4) for the enemies' steps meanwhile. */
#define HIT_BUSY 15
static unsigned int hit_until;
/* the enemies whose hold is a sequence on the victim (ghost): the Tyrant, Alexia's first form, the Gulp Worm (its
   swallow and throw, en02.c bhEne02_PlayerControl) */
static int seq_hold(const BH_PWORK* ep) { return ep->id == 19 || ep->id == 12 || ep->id == 2; }
/* host: a grab handed to the partner's game is running (also in the round trip before its pose shows it), or the
   partner held by a ghost hold */
static int partner_held(void)
{
    int k;
    if (ghost.slot >= 0 && (!(ene[ghost.slot].flg & 0x1) || ghost.key != enemy_room_gen)) ghost.slot = -1;   /* gone with its room */
    if (ghost.slot >= 0) return 1;
    for (k = 0; k < PC_ENE_SLOTS; k++)
        if (owned[k]) return 1;
    return 0;
}
/* the frame step of bhSetMotion (Motion.c) for a copy that has no model of its own to pose */
static void ghost_motion(BH_PWORK* pw)
{
    int n;
    if (!pw->mnwP || pw->mtn_no >= 512) return;
    n = (int)pw->mnwP[pw->mtn_no].frm_num << 16;
    if (!pw->mnwP[pw->mtn_no].datP || n <= 0) return;
    if ((pw->mtn_md & 0x1) && pw->frm_mode) pw->frm_no -= pw->mtn_add;
    else pw->frm_no += pw->mtn_add;
    if (pw->mtn_md & 0x1) {
        if (pw->frm_no <= 0 || pw->frm_no >= n - 65536) pw->frm_mode = ~pw->frm_mode;
    } else if (pw->frm_no < 0) pw->frm_no += n;
    else if (pw->frm_no >= n) pw->frm_no -= n;
}
static void ghost_drop(const char* why)
{
    BH_PWORK* ep = &ene[ghost.slot];
    /* Alexia's burning embrace has no way out (the player is dead, the game over follows): with our player alive
       she takes up the fight again */
    if (ep->id == 12 && ep->mode0 == 2 && (ep->flg & 0x1) && !(ep->flg & 0x2)) {
        *(int*)&ep->mode0 = 1;
        ep->mtn_no = 0; ep->frm_no = 0;
        ep->flg |= 0x40;   /* her body pushes players again (bhEne12_Init; the embrace took it away) */
    }
    host_log("coop: enemy %d (id %d) lets go of the partner (%s)\n", ghost.slot, ep->id, why);
    ghost.slot = -1;
}

static int etr_lim;                     /* RECVX_ENEMY_TRACE (enemy_trace) */
uint64_t gs_frame_count(void);
static int h_mode0 = -1, h_flg4;        /* our player's mode before the enemy step (attacks on it); flg 4 set for the step */
/* The shooter's aim height (at_flg 2 straight, 4 up, 8 down: free aim sets it from the shot, tps_glue.c).
   Enemies read it from plp in their damage code - lying zombies, the anatomist, knocked-down Hunters,
   Cerberus and Bandersnatch in some states take no damage from a shot aimed straight (en01.c
   bhEne01_DmgCheck: plp->at_flg & 2) - so for the enemy's step plp carries the aim of who shot it:
   the partner's from its EV_ENE_HIT (hit_aim, until that enemy's next step), else ours (when plp is the
   partner's stand-in, which has no aim of its own). */
static unsigned char hit_aim[PC_ENE_SLOTS];      /* 0x80 | the partner's aim height of a hit that came in */
static BH_PWORK* aim_pl;                /* the player whose aim height the step borrows, and what it had */
static int aim_was, aim_set;
static void aim_for_step(BH_PWORK* ep, int i)
{
    int a = hit_aim[i] ? hit_aim[i] & 0xE : swap_plp ? swap_plp->at_flg & 0xE : -1;
    hit_aim[i] = 0;
    if (a < 0 || !plp || (plp->at_flg & 0xE) == a) return;
    aim_pl = plp;
    aim_was = plp->at_flg;
    plp->at_flg = aim_set = (plp->at_flg & ~0xE) | a;
}
static void aim_restore(void)
{
    if (aim_pl && aim_pl->at_flg == aim_set) aim_pl->at_flg = aim_was;   /* unless the step changed it (a grab clears it) */
    aim_pl = NULL;
}
/* host: whether an enemy's attacks reach this free player now: within COOP_TGT_TOUCH and inside its front
   quarter (ikou3: the turn it would still need, 0 within 45 degrees) */
static int in_front(BH_PWORK* ep, const BH_PWORK* pl, float d2)
{
    NJS_POINT3 p = { pl->px, pl->py, pl->pz };
    return d2 < COOP_TGT_TOUCH * COOP_TGT_TOUCH && ikou3(ep, &p, 0x2000) == 0;
}
/* bhControlEnemy, around bhJumpEnemy; 1 = the step is done (puppet) */
static int enemy_begin(BH_PWORK* ep);
int coop_enemy_begin(BH_PWORK* ep)
{
    int i = ep - ene, r;
    h_busy = plp && (plp->flg & 4);
    h_stflg = plp ? plp->stflg : 0;
    r = enemy_begin(ep);
    h_ran = !r;
    if (!r && i >= 0 && i < PC_ENE_SLOTS && !echo_delay) aim_for_step(ep, i);   /* its step runs here */
    return r;
}
static int enemy_begin(BH_PWORK* ep)
{
    int i = ep - ene;
    if (i < 0 || i >= PC_ENE_SLOTS || echo_delay) return 0;
    enemy_epoch();
    enemy_frame = frame_no;
    /* both players in the trade screen (host setting): no AI step, the pose held (the guest's puppets
       hold the unchanged snapshots); not an enemy's own initialising step */
    if (frozen && ep->mode0) return puppet_step(ep);
    if (owned[i]) return is_host ? puppet_step(ep) : 0;   /* a grab the client runs */
    if (is_host) {
        int held = proxy_ok && partner_held(), cool_p = coop_atk_cooling(&atk[i][1], frame_no), k = ene_slot(ep->lkwkp);
        if (ep->id == 27 && linked_to(ep) == 2 && (!have_rx || !partner_here())) {   /* the partner left: the larva laid on its stand-in goes too */
            ep->flg = 0;
            return 1;
        }
        if (ep->mode0) fx_begin(1);   /* what its step starts goes to the guest (EV_FX); not its initialising step */
        h_mode0 = plp->mode0;
        examining = !(sys->sp_flg & 0x2);   /* (coop_enemies_run) */
        if (ghost.slot == i && partner_held()) tgt[i] = 1;   /* its hold of the partner runs to its end */
        else if (linked_to(ep)) tgt[i] = linked_to(ep) == 2;   /* (the stand-in also while the partner is busy: it keeps its last pose) */
        else if (ep->id == 27 && !ep->mode0) {}   /* a larva's Init: the player its moth laid it on (larva_laid) */
        else if (!proxy_ok || hold_family(ep)) tgt[i] = 0;   /* (hold_family: it holds our player) */
        else if ((ep->flg & 0x80) && k < PC_ENE_SLOTS) tgt[i] = tgt[k];   /* a linked part goes for its parent's victim */
        else {
            CoopTgtIn in = { { 0 } };
            BH_PWORK* vic = tgt[i] ? &ply2 : plp;
            float dx1 = ep->px - plp->px, dz1 = ep->pz - plp->pz, dx2 = ep->px - ply2.px, dz2 = ep->pz - ply2.pz;
            in.d[0] = dx1 * dx1 + dz1 * dz1;
            in.d[1] = dx2 * dx2 + dz2 * dz2;
            in.busy[0] = ply_busy(plp) || coop_atk_cooling(&atk[i][0], frame_no) || examining;
            in.busy[1] = held || ply_busy(&ply2) || cool_p;
            in.front[0] = !in.busy[0] && in_front(ep, plp, in.d[0]);
            in.front[1] = !in.busy[1] && in_front(ep, &ply2, in.d[1]);
            /* enemies attack only on their own floor: a player on another one counts as twice as far */
            if (plp->flr_no != ep->flr_no) in.d[0] *= 4.0f;
            if (ply2.flr_no != ep->flr_no) in.d[1] *= 4.0f;
            /* one victim per attack: a zombie holding, or an enemy whose motions its victim plays, keeps it
               (before, a flip mid-grab ran the rest of the grab on the other player: the first left in the
               grab motion, the second given the release); in its grab/bite state it keeps a victim within
               reach. Its hit and death states commit to nobody (before, a staggering enemy kept a victim
               that was not near any more). */
            in.commit = (ep->id == 1 ? ep->exp0 && holding(ep) : ghost.slot == i || ((ep->mode0 >= 2 || seq_hold(ep)) && vic->mnwP == ep->mnwP &&
                         vic->mnwP != vic->mnwPb && vic->mnwP != sys->rmthp) ||
                         (ep->id == 15 && (vic->flg & 4) && vic->hp >= 0)) ? COOP_TGT_HOLD :   /* Nosferatu drives his victim until it is up (en15.c) */
                        ep->mode0 == 2 ? COOP_TGT_ATTACK : COOP_TGT_FREE;
            tgt[i] = coop_pick_target(tgt[i], &in);
        }
        if (tgt[i]) {
            swap_plp = plp;
            ply2_save = ply2;
            memcpy(vib_save, Pad_act, sizeof(vib_save));
            memcpy(exp_save, p2.plexw, 128);
            memcpy(exp_save + 128, p2.plhdw, 128);
            plp = &ply2;
            /* held in the partner's game, or this enemy's loop guard: it sees the partner busy and waits (put
               back after the step) */
            if (held || cool_p || (int)(hit_until - frame_no) > 0) ply2.flg |= 4;
            if (ghost.slot == i) { ghost.was = ghost.pw; plp = &ghost.pw; }   /* its hold of the partner goes on */
            /* the zombie picks its and the player's grab motions by sys->ply_id: the partner's */
            if (ep->id == 1 && rx.mfile >= 0) { swap_pid = sys->ply_id; sys->ply_id = rx.mfile & 3; }
        } else if ((examining || coop_atk_cooling(&atk[i][0], frame_no)) && !(plp->flg & 4)) {
            plp->flg |= 4;   /* its loop guard on our player, or our examine: busy for this step only */
            h_flg4 = 1;
        }
        return 0;
    }
    if (!puppets() || !ep->mode0 || event_driven(ep) || grips_us(ep) || runs_here(ep)) return 0;   /* mode0 0: the enemy's own initialising step */
    if (trace && !(ep->flg & 0x80) && frame_no - snap_at[i] == 60)
        fprintf(stderr, "coop: ORPHAN enemy %d (id %d) is a puppet without the host's state for 60 frames: modes %d %d %d %d flg %x\n", i, ep->id,
                ep->mode0, ep->mode1, ep->mode2, ep->mode3, ep->flg);
    return puppet_step(ep);
}

int coop_partner_step(void) { return swap_plp != NULL; }

void coop_enemy_end(BH_PWORK* ep)
{
    int i = ep - ene, swapped = swap_plp != NULL;
    if (i < 0 || i >= PC_ENE_SLOTS) return;
    if (fx_open == 1) { fx_end(ep, 0); fx_arm(ep); }
    aim_restore();
    if (swap_plp) {
        int on = plp == &ghost.pw;
        BH_PWORK* a = on ? &ghost.was : &ply2_save;   /* the victim before the step and after it */
        BH_PWORK* v = on ? &ghost.pw : &ply2;
        /* a grab it started (seq_hold: also the Tyrant's takeover, mode 5, and Alexia's embrace, mode 6), its hit
           reaction, the dead flag it set (Alexia's embrace, the Tyrant's last blow: in single player the game over) */
        int dh = a->hp - v->hp, react = v->mode0 == 2 && a->mode0 != 2, kill = (v->flg & 2) && !(a->flg & 2) && ply2_save.hp >= 0;
        int grab = seq_hold(ep) ? !on && v->mode0 >= 4 && v->mode0 <= 6 && (a->mode0 < 4 || a->mode0 > 6) : v->mode0 == 4 && a->mode0 != 4 && a->mode0 != 6;
        if (swap_pid >= 0) { sys->ply_id = swap_pid; swap_pid = -1; }
        if (etr_lim > 0 && (grab || dh > 0 || react || kill))
            fprintf(stderr, "atk g=%u s=%d id=%d ty=%d to=P %s dh=%d m=%d.%d.%d.%d\n", (unsigned)gs_frame_count(), i, ep->id, ep->type, kill ? "kill" : grab ? "grab" : "hit", dh,
                    ep->mode0, ep->mode1, ep->mode2, ep->mode3);
        if ((grab || react) && coop_atk_note(&atk[i][1], frame_no))
            host_log("coop: loop guard: enemy %d (id %d) started %d attacks on the partner within %u frames: it leaves the partner alone for %u frames\n",
                     i, ep->id, COOP_LOOP_N, COOP_LOOP_WIN, COOP_LOOP_COOL);
        if (grab && ep->id == 1 && !(ep->flg & 0x80) && ep->exp0 && holding(ep) && have_peer)
            grab_out(i, ep);   /* the client's game runs the grab */
        else if (dh > 0 || react || grab || kill) {
            Event e = { 0 };
            e.type = EV_PLY_HIT;
            e.idx = i;
            e.u.ph.hp_delta = dh > 0 ? dh : 0;
            if (kill && e.u.ph.hp_delta <= ply2_save.hp) e.u.ph.hp_delta = ply2_save.hp + 1 > 1000 ? 1000 : ply2_save.hp + 1;   /* its own death there */
            e.u.ph.mode[0] = v->mode0; e.u.ph.mode[1] = v->mode1; e.u.ph.mode[2] = v->mode2; e.u.ph.mode[3] = v->mode3;
            e.u.ph.flg_set = v->flg & ~a->flg;
            e.u.ph.exp1_set = *(int*)p2.plhdw & ~*(int*)(exp_save + 128);
            e.u.ph.dv[0] = v->dvx; e.u.ph.dv[1] = v->dvy; e.u.ph.dv[2] = v->dvz;
            if (grab) {   /* a grab the partner's game does not run (only zombies' are handed over): a hit there */
                e.u.ph.mode[0] = 2; e.u.ph.mode[1] = bhCdirCheck(v->ay, ep->ay) == 0 ? 0 : 1; e.u.ph.mode[2] = 1; e.u.ph.mode[3] = 0;
                e.u.ph.flg_set |= 4;
                if (ep->id == 4) ep->ct0 = 0;   /* Cerberus lets go after its first bite instead of biting the air for 2 s */
                host_log("coop: enemy %d (id %d) grabs the partner: sent as a hit\n", i, ep->id);
            }
            if (kill) host_log("coop: enemy %d (id %d) kills the partner\n", i, ep->id);
            queue_event(&e);
            hit_until = frame_no + HIT_BUSY;
        }
        if (grab && seq_hold(ep) && ghost.slot < 0) {   /* its hold goes on, on a copy (ghost) */
            ghost.pw = *v;
            ghost.slot = i;
            ghost.key = enemy_room_gen;
            ghost.t = frame_no;
        } else if (on) {
            ghost_motion(&ghost.pw);
            if (etr_lim > 0 && !(frame_no % 15))   /* RECVX_ENEMY_TRACE: the held copy */
                fprintf(stderr, "ghost s=%d m=%d.%d.%d.%d mtn=%u f=%d add=%d own=%d flg=%x hp=%d\n", i, ghost.pw.mode0, ghost.pw.mode1, ghost.pw.mode2, ghost.pw.mode3,
                        ghost.pw.mtn_no, ghost.pw.frm_no >> 16, ghost.pw.mtn_add, ghost.pw.mnwP == ghost.pw.mnwPb, ghost.pw.flg, ghost.pw.hp);
            if (ghost.pw.mnwP == ghost.pw.mnwPb && (ghost.pw.mode0 < 4 || ghost.pw.mode0 > 6)) ghost_drop("let go");
            else if (ep->id == 12 && (ep->mode0 != 2 || ep->mode3 >= 2)) ghost_drop("the embrace is over");
            else if ((ghost.pw.flg & 2) && !ghost.pw.mtn_add) ghost_drop("dead");
            else if (ep->flg & 0x2) ghost_drop("it died");
            else if (frame_no - ghost.t > 900) ghost_drop("30 s");
        }
        larva_laid(ply2.stflg & ~a->stflg & 0x38000000, 1);
        /* everything else (grabs, being moved, motions) is undone: the partner's own game
           decides what its player does */
        ply2 = ply2_save;
        memcpy(Pad_act, vib_save, sizeof(vib_save));
        memcpy(p2.plexw, exp_save, 128);
        memcpy(p2.plhdw, exp_save + 128, 128);
        plp = swap_plp;
        swap_plp = NULL;
    } else if (is_host && h_mode0 >= 0) {   /* its step on our player: a hit reaction or a grab it started */
        if (h_flg4) plp->flg &= ~4;
        if ((plp->mode0 == 2 || plp->mode0 == 4) && h_mode0 != 2 && h_mode0 != 4 && h_mode0 != 6 && coop_atk_note(&atk[i][0], frame_no))
            host_log("coop: loop guard: enemy %d (id %d) started %d attacks on our player within %u frames: it leaves us alone for %u frames\n",
                     i, ep->id, COOP_LOOP_N, COOP_LOOP_WIN, COOP_LOOP_COOL);
    }
    h_mode0 = -1;
    h_flg4 = 0;
    if (h_ran && !swapped && plp) {   /* its step ran on our player */
        larva_laid(plp->stflg & ~h_stflg & 0x38000000, 0);
        if (!h_busy && (plp->flg & 4)) { hold.by = i; hold.id = ep->id; hold.room = coop_room_key(); hold.said = 0; }
        if (hold_family(ep)) hold.seen = frame_no;
    }
    h_ran = 0;
}

/* ---------------------------------------------------------------- effects (protocol 12, EV_FX) */
/* The guest's enemies are puppets: their AI does not run there, so nothing it starts besides the
   motion showed - the zombie's vomit (effect 350), acid and spit (256, 258), the moth's powder
   (260), Alexia's fire, the blood of a hit enemy, sparks. And neither side saw the partner's
   muzzle flash, the sparks and splashes where its shots hit, or its grenades and bolts: the shot
   ran only in the shooter's game. Effects are entries of eff[] that bhSetEffect(Tb) starts and the
   caller then adjusts (a texture of its model, a direction, counters); from then on each runs by
   itself in bhControlEffect. So the sender looks at eff[] around the code that starts them - each
   enemy step on the host, our player's step on both sides (bhControlPlayer: the weapon code), and
   the step of our own projectiles (their explosion) - and sends each new entry once as EV_FX: its
   fields, and what it points to as references (an enemy slot, the sender's player, the receiver's
   player, a sub-model's texture). The receiver starts the same entry and lets it run. Not sent:
   entries the receiver makes itself (the muzzle flash and smoke of bhSetGunFire, which the
   receiver calls for the partner's model at the shot mark of its pose, and the effects on our own
   hands), entries with pointers we cannot name or a matrix set by their caller (logged once per
   effect number with RECVX_COOP_TRACE). An entry started from EV_FX only shows: what a
   projectile's or a blast's step does to our enemies and our player is undone, and the blast it
   would start is dropped - the owner's game applies the hit and sends its blast. Enemy attack
   effects (vomit, acid) do hurt the receiver's player where they land: the host's copy can only
   reach the host's player. At most FX_FRAME per frame, none while FX_BACKLOG events wait for the
   partner (they are cosmetic: a busy link drops them). */
#define FX_FRAME 8
#define FX_BACKLOG 48
#define FX_IN_FRAME 64                  /* the partner's effects started here in one frame (it sends FX_FRAME; more after a stall) ... */
#define FX_IN_MAX 256                   /* ... and alive at once: half of eff[] at most */
static uint32_t fx_was[16];             /* eff[] slots in use when the window opened */
/* the larger state (~190 KB with the saved enemies) on the heap, allocated with the first window or EV_FX */
static struct {
    unsigned char mirror[512];          /* our eff[] slot holds an entry started from the partner's EV_FX */
    unsigned char seen[400];            /* fx_note */
    float arm[PC_ENE_SLOTS][4];                  /* fx_arm: sent */
    int arm_room[PC_ENE_SLOTS];
    BH_PWORK keep_ply, keep_ene[PC_ENE_SLOTS];   /* coop_fx_step */
} *fxh;
#define fx_mirror (fxh->mirror)
static int fx_heap(void) { return fxh || (fxh = calloc(1, sizeof(*fxh))) != NULL; }
static unsigned int fx_frame, fx_sent, fx_dropped, fx_stat;   /* this frame's count; totals (stats) */

static int fx_hurts(int id) { return id == 13 || id == 16 || id == 127 || id == 130; }   /* bolt, blast, gas, grenade/rocket */
static int fx_gunfire(int id) { return id == 2 || id == 3 || id == 9 || id == 14 || id == 132 || id == 229; }   /* bhSetGunFire's */
static void fx_slots(uint32_t* map)
{
    int i;
    memset(map, 0, 16 * sizeof(uint32_t));
    for (i = 0; i < 512; i++)
        if (eff[i].flg & 0x3) map[i >> 5] |= 1u << (i & 31);
        else fx_mirror[i] = 0;
}
static int fx_new(const uint32_t* map, int i) { return (eff[i].flg & 0x3) && !(map[i >> 5] >> (i & 31) & 1); }

static void fx_begin(int kind)
{
    static int off = -1;
    if (off < 0) off = getenv("RECVX_COOP_FX") && !atoi(getenv("RECVX_COOP_FX"));   /* test aid: RECVX_COOP_FX=0 sends none (bandwidth before/after) */
    fx_open = 0;
    if (off || !have_peer || echo_delay || !have_rx || !same_room() || !(rx.flags & PF_VISIBLE) || !fx_heap()) return;
    fx_slots(fx_was);
    fx_open = kind;
}

static BH_PWORK* fx_me(void) { return swap_plp ? swap_plp : plp; }   /* an enemy step may have plp on the proxy */
static int fx_ref_out(const void* p)   /* a pointer as a reference for the partner; -1: none of ours */
{
    const BH_PWORK* w = p;
    if (!p) return FX_NONE;
    if (w >= ene && w < ene + PC_ENE_SLOTS) return (const char*)w == (const char*)&ene[w - ene] ? (int)(w - ene) : -1;
    if (w == fx_me()) return FX_SENDER;
    if (w == &ply2) return FX_RECEIVER;
    return -1;
}
static BH_PWORK* fx_ref_in(int r)      /* a partner's reference as our work, NULL: not here */
{
    if (r < PC_ENE_SLOTS) return r < (int)sys->ewk_n && (ene[r].flg & 0x1) ? &ene[r] : NULL;
    if (r == FX_SENDER) return p2_chr >= 0 && p2_seen_room == coop_room_key() && ply2.mlwP ? &ply2 : NULL;
    return r == FX_RECEIVER ? plp : NULL;
}
static int fx_mdl(const BH_PWORK* o, const void* p, int tex)   /* p is o's mdl[k] (or its texture): k; -2 not */
{
    int k;
    if (!p) return -1;
    for (k = 0; o && k < 16 && k < o->mdl_n; k++)
        if (tex ? p == (const void*)o->mdl[k].texP : p == (const void*)&o->mdl[k]) return k;
    return -2;
}

static void fx_note(int id, const char* why)
{
    if (trace && id < 400 && !fxh->seen[id]++) fprintf(stderr, "coop: fx %d not sent: %s\n", id, why);
}

static void fx_queue(Event* e)
{
    if (fx_frame != frame_no) { fx_frame = frame_no; fx_stat = 0; }
    if (fx_stat >= FX_FRAME || ring.next - ring.head > FX_BACKLOG) { fx_dropped++; return; }
    queue_event(e);
    fx_stat++;
    fx_sent++;
    if (trace) fprintf(stderr, "coop: fx %d out (follows %d)\n", e->u.fx.id, e->idx);
}

/* effsub3.c bhSetEffParticle(Mk2): blood and debris - sets of three eff[] entries pointing at each
   other and at a drawing function - go as the call itself */
void coop_fx_particle(BH_PWORK* ewP, int lnk, const void* offP, const void* dirP, unsigned int col, unsigned int dst, int typ, int mk2)
{
    Event e;
    int ref = fx_ref_out(ewP);
    if (!fx_open || ref < 0 || (!ewP && !offP) || !dirP || typ < 0 || typ >= FX_PTCL_TYPES || lnk < 0 || lnk >= 64) return;
    memset(&e, 0, sizeof(e));
    e.type = EV_FX;
    coop_fx_put(&e.idx, &e.u.fxp.refs, FXR_IDX, ref);
    e.stg = sys->stg_no;
    e.rom = sys->rom_no;
    e.u.fxp.id = FX_PARTICLE;
    e.u.fxp.typ = typ; e.u.fxp.mk2 = mk2; e.u.fxp.lnk = lnk;
    e.u.fxp.col = col; e.u.fxp.dst = dst;
    if (offP) memcpy(e.u.fxp.off, offP, sizeof(e.u.fxp.off));
    memcpy(e.u.fxp.dir, dirP, sizeof(e.u.fxp.dir));
    if (!coop_check_event(&e, is_host)) fx_queue(&e);
}

/* one new eff[] entry to the partner (owner: the enemy or player whose step started it) */
static void fx_send(O_WRK* op, BH_PWORK* owner, int player)
{
    static const unsigned char zero[sizeof(ML_WORK) * 16];
    Event e;
    int link = fx_ref_out(op->lkwkp);   /* also without the link flag: the vomit keeps its zombie there (its sound) */
    const char* why = NULL;
    if (op->id == FX_PARTICLE || op->id == 301) return;   /* went as the call (coop_fx_particle) */
    memset(&e, 0, sizeof(e));
    e.type = EV_FX;
    e.stg = sys->stg_no;
    e.rom = sys->rom_no;
    if (op->id == FX_BURST) {   /* sparks (bhSetRapEff): its parameter block and particle source */
        e.u.fxr.id = FX_BURST;
        e.u.fxr.nw = FX_RAW_WORDS;
        memcpy(e.u.fxr.w, ((OR_WORK*)op)->free4, sizeof(e.u.fxr.w));
        if (coop_check_event(&e, is_host)) why = "burst out of range";
    } else {
        int own = fx_ref_out(owner), txp = fx_mdl(owner, op->txp[0], 1), mlw = op->id < 400 && op->mlwP == &sys->efm[op->id] ? -1 : fx_mdl(owner, op->mlwP, 0), ex = fx_ref_out(op->exp0);
        if (player && (fx_gunfire(op->id) || link == FX_SENDER)) return;   /* the partner makes these itself */
        if (op->id >= 400) why = "number";
        else if (link < 0) why = "follows something unknown";
        else if (txp < -1 || mlw < -1 || ex < 0) why = "texture, model or work not of the enemy or player that started it";
        else if (op->mtx != (float(*)[16])op->mtxbuf || op->pvp != op->pv || op->tvp != op->tv || memcmp(op->mtxbuf, zero, sizeof(op->mtxbuf))) why = "matrix set by its caller";
        else if (op->exp1 || op->exp2 || op->exp3 || op->mskp || op->obj_a || op->obj_b || op->mnwP || op->mnwPb || op->mtn_tp || op->objbak || op->func) why = "pointers";
        else if (memcmp(op->mdl, zero, sizeof(op->mdl)) || memcmp(op->skp, zero, sizeof(op->skp)) || memcmp(op->mbp, zero, sizeof(op->mbp)) || memcmp(op->txp + 1, zero, 15 * sizeof(op->txp[0]))) why = "models";
        else if ((op->stflg >> 16) || op->lkono < 0 || op->lkono >= 64 || fabsf(op->sx) >= 127 || fabsf(op->sy) >= 127 || fabsf(op->sz) >= 127) why = "flags or scale";
        coop_fx_put(&e.idx, &e.u.fx.refs, FXR_IDX, link);
        e.u.fx.id = op->id; e.u.fx.type = op->type; e.u.fx.flg = op->flg; e.u.fx.stflg = op->stflg; e.u.fx.tex_id = op->tex_id;
        e.u.fx.mdlver = op->mdlver; e.u.fx.flr_no = op->flr_no; e.u.fx.lkono = op->lkono;
        coop_fx_put(&e.u.fx.owner, &e.u.fx.refs, FXR_OWNER, own < 0 ? FX_NONE : own);
        e.u.fx.mode[0] = op->mode0; e.u.fx.mode[1] = op->mode1; e.u.fx.mode[2] = op->mode2; e.u.fx.mode[3] = op->mode3;
        e.u.fx.txp = txp; e.u.fx.mlw = mlw;
        coop_fx_put(&e.u.fx.exp0, &e.u.fx.refs, FXR_EXP0, ex < 0 ? FX_NONE : ex);
        e.u.fx.ang[0] = op->ax; e.u.fx.ang[1] = op->ay; e.u.fx.ang[2] = op->az;
        e.u.fx.ct[0] = op->ct0; e.u.fx.ct[1] = op->ct1; e.u.fx.ct[2] = op->ct2; e.u.fx.ct[3] = op->ct3;
        e.u.fx.s[0] = (int16_t)(op->sx * 256); e.u.fx.s[1] = (int16_t)(op->sy * 256); e.u.fx.s[2] = (int16_t)(op->sz * 256);
        e.u.fx.p[0] = op->px; e.u.fx.p[1] = op->py; e.u.fx.p[2] = op->pz;
        e.u.fx.o[0] = op->lox; e.u.fx.o[1] = op->loy; e.u.fx.o[2] = op->loz;
        e.u.fx.n[0] = op->xn; e.u.fx.n[1] = op->yn; e.u.fx.n[2] = op->zn;
        if (!why && (txp >= 0 || mlw >= 0) && own < 0) why = "model of no enemy or player";
        if (!why && coop_check_event(&e, is_host)) why = coop_fx_rule(op->id) ? "fields out of range (coop_proto.c fx_rules)" : "not an effect co-op sends (coop_proto.c fx_rules)";
    }
    if (why) { fx_note(op->id, why); return; }
    fx_queue(&e);
}

static void fx_end(BH_PWORK* owner, int player)
{
    int i;
    if (!fx_open) return;
    for (i = 0; i < 512; i++)
        if (fx_new(fx_was, i) && !fx_mirror[i] && (fx_open == 1 || fx_hurts(eff[i].id) || eff[i].id == FX_BURST)) fx_send(&eff[i], owner, player);
    fx_open = 0;
}

/* The Bandersnatch (en09) stretches its arm by moving the x offsets of its arm nodes 7..10 in its
   model (objP) during its attacks, outside the motion: a puppet animated from the snapshots kept a
   short arm. The host sends them (FX_ARM) when they change. */
static void fx_arm(BH_PWORK* ep)
{
    float (*sent)[4] = fxh->arm;
    int* room = fxh->arm_room;
    int i = ep - ene, k, key = coop_room_key() + 1;
    NJS_CNK_OBJECT* obj;
    Event e;
    if (ep->id != 9 || !ep->mlwP || !ep->mlwP->objP || ep->mlwP->obj_num <= 10) return;
    obj = ep->mlwP->objP + 7;
    for (k = 0; k < 4 && room[i] == key && fabsf(obj[k].pos[0] - sent[i][k]) < 0.01f; k++);
    if (k == 4) return;
    memset(&e, 0, sizeof(e));
    e.type = EV_FX; e.idx = i; e.stg = sys->stg_no; e.rom = sys->rom_no;
    e.u.fxa.id = FX_ARM;
    for (k = 0; k < 4; k++) e.u.fxa.len[k] = sent[i][k] = obj[k].pos[0];
    room[i] = key;
    if (!coop_check_event(&e, is_host)) fx_queue(&e);
}

/* game.c, before bhControlPlayer (coop_control_remote closes it) */
void coop_fx_player(void) { fx_begin(1); }

/* the partner's EV_FX: the same entry here, shown only (fx_mirror) */
static int apply_fx(const Event* e)
{
    const BH_PWORK *lk, *own, *ex;
    EF_WORK ef;
    NJS_POINT3 off;
    O_WRK* op;
    int k, n, ix = coop_fx_get(e->idx, e->u.fx.refs, FXR_IDX), owner = coop_fx_get(e->u.fx.owner, e->u.fx.refs, FXR_OWNER),
        exp0 = coop_fx_get(e->u.fx.exp0, e->u.fx.refs, FXR_EXP0);   /* (the generic entry's; a particle call has its own refs) */
    if (!sys || !rom || e->stg != sys->stg_no || e->rom != sys->rom_no || !in_session_game() || (sys->cb_flg & 0x1) || !fx_heap()) return 1;   /* another room: gone */
    if (e->u.fx.id != FX_ARM) {   /* bounded: the partner's effects never take most of eff[] (ours: shots, blasts) - they are cosmetic */
        static unsigned int in_frame, in_n;
        if (in_frame != frame_no) { in_frame = frame_no; in_n = 0; }
        if (in_n >= FX_IN_FRAME) return 1;
        for (k = n = 0; k < 512; k++) n += fx_mirror[k] && (eff[k].flg & 0x3);
        if (n >= FX_IN_MAX) return 1;
        in_n++;
    }
    if (e->u.fx.id == FX_BURST) {
        OR_WORK* o = bhSetRapEff(FX_BURST, (void*)e->u.fxr.w, FX_RAW_WORDS);
        if (o) fx_mirror[(O_WRK*)o - eff] = 1;
        return 1;
    }
    if (e->u.fx.id == FX_ARM) {
        BH_PWORK* w = fx_ref_in(e->idx);
        if (w && w->id == 9 && w->mlwP && w->mlwP->objP && w->mlwP->obj_num > 10)
            for (k = 0; k < 4; k++) w->mlwP->objP[7 + k].pos[0] = e->u.fxa.len[k];
        return 1;
    }
    if (e->u.fx.id == FX_PARTICLE) {
        int pix = coop_fx_get(e->idx, e->u.fxp.refs, FXR_IDX);
        BH_PWORK* w = pix == FX_NONE ? NULL : fx_ref_in(pix);
        NJS_POINT3 o3, d3;
        uint32_t was[16];
        if (pix != FX_NONE && (!w || !w->mlwP || !w->mlwP->owP || e->u.fxp.lnk >= (int)w->mlwP->obj_num)) return 1;
        memcpy(&o3, e->u.fxp.off, sizeof(o3));
        memcpy(&d3, e->u.fxp.dir, sizeof(d3));
        fx_slots(was);
        if (e->u.fxp.mk2) bhSetEffParticleMk2(w, e->u.fxp.lnk, &o3, &d3, e->u.fxp.col, e->u.fxp.dst, e->u.fxp.typ);
        else bhSetEffParticle(w, e->u.fxp.lnk, &o3, &d3, e->u.fxp.col, e->u.fxp.typ);
        for (k = 0; k < 512; k++) if (fx_new(was, k)) fx_mirror[k] = 1;
        if (trace) fprintf(stderr, "coop: fx %d in (particles %d, follows %d)\n", FX_PARTICLE, e->u.fxp.typ, pix);
        return 1;
    }
    /* (coop_check_event checked it against its FxRule: the effect, its numbers, which references it has;
       here they must name works that are here, with the nodes, models and textures it will index) */
    lk = ix == FX_NONE ? NULL : fx_ref_in(ix);
    own = owner == FX_NONE ? NULL : fx_ref_in(owner);
    ex = exp0 == FX_NONE ? NULL : fx_ref_in(exp0);
    if ((ix != FX_NONE && !lk) || (exp0 != FX_NONE && !ex)) return 1;
    if ((e->u.fx.txp >= 0 || e->u.fx.mlw >= 0) && !own) return 1;
    if ((e->u.fx.txp >= 0 && (e->u.fx.txp >= own->mdl_n || !own->mdl[e->u.fx.txp].texP || e->u.fx.tex_id >= (int)own->mdl[e->u.fx.txp].texP->nbTexture)) ||
        (e->u.fx.mlw >= 0 && (e->u.fx.mlw >= own->mdl_n || !own->mdl[e->u.fx.mlw].objP || e->u.fx.mdlver >= own->mdl[e->u.fx.mlw].obj_num))) return 1;
    if (lk && (!lk->mlwP || !lk->mlwP->owP || e->u.fx.lkono >= (int)lk->mlwP->obj_num)) return 1;   /* (with or without flg 0x80: steps read its nodes) */
    if ((e->u.fx.id == 260 && (owner >= PC_ENE_SLOTS || own->id != 6 || e->u.fx.txp != 1 || own->mdl[1].texP->nbTexture < 5)) ||   /* the moth's powder cycles textures 1..4 */
        (e->u.fx.id == 356 && (ix >= PC_ENE_SLOTS || lk->id != 1 || lk->mlwP->obj_num <= 10)) ||   /* a zombie's head (its node 10) */
        (e->u.fx.id == 397 && (exp0 >= PC_ENE_SLOTS || ex->id != 15 || !ex->exp0))) return 1;   /* the en15 whose poison it is (its exp0 is read and written) */
    memset(&ef, 0, sizeof(ef));
    ef.flg = e->u.fx.flg;
    ef.id = e->u.fx.id; ef.type = e->u.fx.type; ef.flr_no = e->u.fx.flr_no; ef.mdlver = e->u.fx.mdlver;
    ef.px = e->u.fx.p[0]; ef.py = e->u.fx.p[1]; ef.pz = e->u.fx.p[2];
    ef.sx = e->u.fx.s[0] / 256.0f; ef.sy = e->u.fx.s[1] / 256.0f; ef.sz = e->u.fx.s[2] / 256.0f;
    ef.ax = e->u.fx.ang[0]; ef.ay = e->u.fx.ang[1];
    off.x = e->u.fx.o[0]; off.y = e->u.fx.o[1]; off.z = e->u.fx.o[2];
    if ((k = bhSetEffectTb(&ef, &off, (e->u.fx.flg & 0x80) ? (unsigned char*)lk : NULL, e->u.fx.lkono)) < 0) return 1;
    op = &eff[k];
    op->lkwkp = (unsigned char*)lk;
    op->ax = e->u.fx.ang[0]; op->ay = e->u.fx.ang[1]; op->az = e->u.fx.ang[2];
    op->stflg = e->u.fx.stflg; op->tex_id = e->u.fx.tex_id;   /* (modes stay 0: its own init runs here) */
    op->xn = e->u.fx.n[0]; op->yn = e->u.fx.n[1]; op->zn = e->u.fx.n[2];
    op->ct0 = e->u.fx.ct[0]; op->ct1 = e->u.fx.ct[1]; op->ct2 = e->u.fx.ct[2]; op->ct3 = e->u.fx.ct[3];
    if (e->u.fx.txp >= 0) op->txp[0] = own->mdl[e->u.fx.txp].texP;
    if (e->u.fx.mlw >= 0) op->mlwP = (ML_WORK*)&own->mdl[e->u.fx.mlw];
    op->exp0 = (unsigned char*)ex;
    fx_mirror[k] = 1;
    if (op->id == 356 && lk && lk->id == 1 && lk->exp0 && !(lk->flg & 0x80))   /* en01.c's head burst: the zombie's head is gone here too */
        *(int*)(lk->exp0 + 0x78) = -1;                                          /* (its exp0 is not in the snapshots; "Head blow-offs") */
    if (owner < PC_ENE_SLOTS) {   /* its sounds from the bank of the enemy type that made it (effect.c, eneset.c) */
        extern short pc_eff_owner[512][2];
        int pc_enemy_sound_type(int slot);
        pc_eff_owner[k][0] = (short)pc_enemy_sound_type(owner);
        pc_eff_owner[k][1] = op->id + 1;
    }
    if (trace) fprintf(stderr, "coop: fx %d in (slot %d, follows %d)\n", op->id, k, ix);
    return 1;
}

/* effect.c bhControlEffect, around an entry's step (0 before: 1 = call again after it). A
   projectile or blast started from the partner's EV_FX: its hits on our enemies and our player
   are undone and the blasts it starts dropped (the partner sends its own). One of ours: its blast
   goes to the partner. */
#define fx_keep_ply (fxh->keep_ply)
#define fx_keep_ene (fxh->keep_ene)
static uint32_t fx_step_was[16];
static int fx_step_kind, fx_step_n;
int coop_fx_step(int i, int after)
{
    O_WRK* op = &eff[i];
    int k;
    if (!after) {
        fx_step_kind = 0;
        if (!fx_hurts(op->id) || echo_delay || !fxh) return 0;   /* (no heap: nothing mirrored yet) */
        if (fx_mirror[i]) {
            fx_step_n = sys->ewk_n < PC_ENE_SLOTS ? sys->ewk_n : PC_ENE_SLOTS;
            fx_keep_ply = *plp;
            memcpy(fx_keep_ene, ene, fx_step_n * sizeof(BH_PWORK));
            fx_slots(fx_step_was);
            fx_step_kind = 1;
            return 1;
        }
        fx_begin(2);
        return fx_open ? (fx_step_kind = 2) : 0;
    }
    if (fx_step_kind == 1) {
        *plp = fx_keep_ply;
        memcpy(ene, fx_keep_ene, fx_step_n * sizeof(BH_PWORK));
        for (k = 0; k < 512; k++)
            if (fx_new(fx_step_was, k)) { if (fx_hurts(eff[k].id)) eff[k].flg = 0; else fx_mirror[k] = 1; }
    } else if (fx_step_kind == 2) fx_end(plp, 0);
    fx_step_kind = 0;
    return 0;
}

/* Frozen-enemy watchdog, a safety net (bhControlEnemy, every enemy step, also in single player): an
   enemy alive whose modes, motion, frame, position and hp have not changed for WATCH_STEPS steps
   (5 s) is logged (and every 30 s it stays so). A zombie that runs here and is stuck in its grab
   (mode0 2: every grab state moves on by its motion) is put back to standing (then the chase), and
   our player let go if it plays that zombie's grab. Not a zombie lying after a push-off (mode0 1,
   MV04, motion stopped, the hold bit still set): it waits, legitimately, until no other zombie is
   getting up (bhEne01_CmnFlgCheck). The repair only in co-op or a room with added enemies (pc_enemy_added):
   in single player a retail room behaves as retail (the log stays).
   A puppet is the other side's to fix (the host takes a grab back after OWN_LOST, own_timeout). */
#define WATCH_STEPS 150
static struct { int sig[6], still; } watch[PC_ENE_SLOTS];
/* Test aid RECVX_ENEMY_TRACE=dist: one line per step of every living enemy (not parts) within dist
   units (10 = 1 m) of our player or the partner: its type, target (H our player, h ours because the
   partner is no target now, P the partner's stand-in, - single player / guest), distances, modes,
   motion, hp.mode0 of both players, whether each counts as busy for the target rule, a zombie's exp0
   flag word (0x20000 holding), run here or puppet, the enemy's and our position. Only reads.
   Attacks on the partner's stand-in: "atk" lines (coop_enemy_end). */
static int etr_lim = -1;
static void enemy_trace(BH_PWORK* ep, int i, int ran)
{
    int lim = etr_lim < 0 ? (etr_lim = getenv("RECVX_ENEMY_TRACE") ? atoi(getenv("RECVX_ENEMY_TRACE")) : 0) : etr_lim;
    float dh, dp = -1.0f;
    if (!lim || !sys || !plp || (ep->flg & 0x80) || !ep->mode0 || ep->hp < 0 || ep->id >= 31) return;
    dh = sqrtf((ep->px - plp->px) * (ep->px - plp->px) + (ep->pz - plp->pz) * (ep->pz - plp->pz));
    if (coop_on && p2_seen_room == coop_room_key())
        dp = sqrtf((ep->px - ply2.px) * (ep->px - ply2.px) + (ep->pz - ply2.pz) * (ep->pz - ply2.pz));
    if (dh > lim && (dp < 0 || dp > lim)) return;
    fprintf(stderr, "etr g=%u s=%d id=%d ty=%d t=%c dH=%.1f dP=%.1f m=%d.%d.%d.%d mtn=%u f=%d hp=%d pH=%d.%d pP=%d.%d bH=%d bP=%d x=%x %s e=%.0f,%.0f h=%.0f,%.0f fl=%x st=%x age=%u\n", (unsigned)gs_frame_count(), i, ep->id, ep->type,
            !coop_on || !is_host ? '-' : tgt[i] ? 'P' : proxy_ok ? 'H' : 'h', dh, dp, ep->mode0, ep->mode1, ep->mode2, ep->mode3, ep->mtn_no, ep->frm_no >> 16,
            ep->hp, plp->hp, plp->mode0, dp < 0 ? 0 : ply2.hp, dp < 0 ? 0 : ply2.mode0, ply_busy(plp), dp < 0 ? 0 : ply_busy(&ply2) || partner_held(),
            ep->id == 1 && ep->exp0 ? *(int*)(ep->exp0 + 0x40) : 0, ran ? "run" : "pup", ep->px, ep->pz, plp->px, plp->pz, ep->flg, ep->stflg, ran || is_host ? 0 : frame_no - snap_at[i]);
}

void pc_enemy_watch(BH_PWORK* ep, int i, int ran)
{
    int s[6] = { *(int*)&ep->mode0, (int)ep->mtn_no, ep->frm_no, 0, 0, ep->hp };
    memcpy(&s[3], &ep->px, 4); memcpy(&s[4], &ep->pz, 4);
    if (i < 0 || i >= PC_ENE_SLOTS) return;
    enemy_trace(ep, i, ran);
    if (frozen) { watch[i].still = 0; return; }   /* the trade screen holds it */
    if (memcmp(s, watch[i].sig, sizeof(s))) { memcpy(watch[i].sig, s, sizeof(s)); watch[i].still = 0; return; }
    if (++watch[i].still < WATCH_STEPS || (watch[i].still - WATCH_STEPS) % 900 || (ep->flg & 0x80) || !ep->mode0 || ep->hp < 0 || (!ep->hp && ep->id != 1)) return;   /* a zombie at hp 0 is alive; helpers (hp 0) are not watched */
    host_log("watchdog: enemy %d (id %d) still for %d steps: modes %d %d %d %d, motion %u frame %d add %d, hp %d, flg %x flg2 %x, hold %d, %s%s\n",
             i, ep->id, watch[i].still, ep->mode0, ep->mode1, ep->mode2, ep->mode3, ep->mtn_no, ep->frm_no >> 16, ep->mtn_add, ep->hp,
             ep->flg, ep->flg2, ep->id == 1 && holding(ep), ran ? "runs here" : "puppet here",
             !coop_on || echo_delay ? "" : is_host ? (owned[i] ? " (host: the partner runs it)" : " (host)") : (owned[i] ? " (guest: ours)" : " (guest)"));
    if (!ran || ep->id != 1 || ep->mode0 != 2 || event_driven(ep)) return;
    if (!coop_on && !pc_enemy_added) return;   /* single player in a retail room: as retail (logged only) */
    if (plp && holding(ep) && plp->mnwP == ep->mnwP && plp->mnwP != plp->mnwPb && (plp->mode0 == 4 || plp->mode0 == 6)) player_let_go();
    pc_enemy_unhold(ep);
    host_log("watchdog: enemy %d was stuck in its grab: back to standing\n", i);
}

/* host, at the hand-off: snapshot of every enemy slot of the room */
static int build_snapshots(EneSnap* out)
{
    static int rr, free_rr;
    int i, k, cnt = 0, n = 0, idx[PC_ENE_SLOTS], win[COOP_SNAP_FRAME], free_idx[4], free_n = 0;
    /* not during a door transition: the room number is already the next one while the
       enemy slots still hold the old room's */
    if (!tx_in_game || (sys->cb_flg & 0x1) || !same_room() || echo_delay) return 0;   /* the client runs the enemies of its own room (M5) */
    /* the host sends what it runs, the client its grabs: the active slots every frame (more than
       a frame holds - added enemies: a moving window), free ones a few per frame (a slot that went
       free there goes free here) */
    for (i = 0; i < (int)sys->ewk_n && i < PC_ENE_SLOTS; i++)
        if (owned[i] != is_host && (ene[i].flg & 0x1)) idx[cnt++] = i;
    rr = cnt > COOP_SNAP_FRAME ? (rr + COOP_SNAP_FRAME) % cnt : 0;
    for (k = 0; k < cnt && k < COOP_SNAP_FRAME; k++) win[k] = idx[(rr + k) % cnt];
    cnt = k;
    for (k = 0; k < (int)sys->ewk_n && k < PC_ENE_SLOTS && cnt < COOP_SNAP_FRAME && free_n < 4; k++) {
        i = (free_rr + k) % (sys->ewk_n < PC_ENE_SLOTS ? sys->ewk_n : PC_ENE_SLOTS);
        if (owned[i] != is_host && !(ene[i].flg & 0x1)) { free_idx[free_n++] = i; cnt++; }
    }
    if (sys->ewk_n) free_rr = (free_rr + k) % (sys->ewk_n < PC_ENE_SLOTS ? sys->ewk_n : PC_ENE_SLOTS);
    for (k = 0; k < cnt; k++) {
        BH_PWORK* ep = &ene[i = k < cnt - free_n ? win[k] : free_idx[k - (cnt - free_n)]];
        EneSnap* s;
        s = &out[n++];
        s->idx = i;
        s->mode[0] = ep->mode0; s->mode[1] = ep->mode1; s->mode[2] = ep->mode2; s->mode[3] = ep->mode3;
        s->mdl = mdl_of(ep);
        s->mnw = mnw_of(ep);
        s->flg = ep->flg; s->flg2 = ep->flg2; s->stflg = ep->stflg; s->mdflg = ep->mdflg;
        s->px = ep->px; s->py = ep->py; s->pz = ep->pz;
        s->ax = ep->ax; s->ay = ep->ay; s->az = ep->az;
        s->mtn_no = ep->mtn_no; s->frm_no = ep->frm_no; s->mtn_md = ep->mtn_md;
        s->hokan_count = (int16_t)ep->hokan_count; s->hokan_rate = ep->hokan_rate; s->hp = ep->hp;
        s->id = ep->id < 100 ? ep->id : 0;
        memcpy(s->watr, &ep->watr, sizeof(s->watr));
    }
    return n;
}

/* at the hand-off: our hits on puppets go to the side that runs them (and are cleared here:
   the puppets do not run their own damage code) */
static void capture_hits(void)
{
    int i, j, n;
    if (is_host ? !same_room() : !puppets()) return;
    for (i = 0; i < (int)sys->ewk_n && i < PC_ENE_SLOTS; i++) {
        BH_PWORK* ep = &ene[i];
        Event e = { 0 };
        if (!(ep->flg & 0x1) || !ep->mode0 || owned[i] != is_host) continue;
        for (j = n = 0; ep->mlwP && j < ep->mlwP->obj_num && j < 64; j++)
            if (ep->dam[j] && n < 6) { e.u.eh.jnt[n] = j; e.u.eh.dam[n++] = ep->dam[j]; ep->dam[j] = 0; }
        if (!n && !(ep->flg & 0x4) && !(ep->flg2 & 0x4) && !(ep->comb_flg & 0x70)) continue;
        for (; n < 6; n++) e.u.eh.jnt[n] = -1;
        e.type = EV_ENE_HIT;
        e.idx = i;
        e.u.eh.flg = (ep->flg & 0x4) | (plp->at_flg & 0xE) << 8 | COOP_HIT_AIM;   /* with the shooter's aim height */
        if (pc_precise_head(ep)) e.u.eh.flg |= COOP_HIT_PRECISE;   /* and whether its "Precise hits" judged it (tps_glue.c) */
        e.u.eh.flg2 = ep->flg2 & 0x4; e.u.eh.comb_flg = ep->comb_flg & 0x70;
        e.u.eh.djnt_no = ep->djnt_no; e.u.eh.wpnr_no = ep->wpnr_no;
        e.u.eh.dp[0] = ep->dpx; e.u.eh.dp[1] = ep->dpy; e.u.eh.dp[2] = ep->dpz;
        e.u.eh.dv[0] = ep->dvx; e.u.eh.dv[1] = ep->dvy; e.u.eh.dv[2] = ep->dvz;
        e.u.eh.dax = ep->dax; e.u.eh.day = ep->day;
        ep->flg &= ~0x4; ep->flg2 &= ~0x4; ep->comb_flg &= ~0x70;
        queue_event(&e);
    }
}

/* ---------------------------------------------------------------- enemies killed while the guest ran them (protocol 15, EV_GONE)
   While the host is in another room (or busy: menu, item box) the guest's game runs that room's enemies.
   When the host's enemies take over again (the host comes in, or back from its menu), the guest's enemies
   become puppets of the host's - and the host's copies of the ones the guest killed meanwhile were alive:
   a fresh room load (the room's records without a kill flag, about 1 in 5 hostile ones, and the extra
   enemies come back with every load) or never hit. So they stood up again in front of the guest. Now the
   guest names its dead enemies at that moment (EV_GONE), keeps them dead meanwhile, and the host hides its
   copies the way the room script hides a killed record (EneSetCk) and marks them dead, so a kill flag the
   record has is set by the host's own script too. Within one visit only: once both have left the room,
   it is loaded the game's way again. */
static unsigned int gone_mask[(PC_ENE_SLOTS + 31) / 32];       /* guest: slots we named dead; the host's state does not revive them */
static int gone_t;                      /* guest: frames left to wait for the host to hide them */
static struct { int key, t, n; unsigned char slot[PC_ENE_SLOTS], id[PC_ENE_SLOTS], ver[PC_ENE_SLOTS]; } gone_in;   /* host: the guest's, until done */
static int gone_has(int k) { return k >= 0 && k < PC_ENE_SLOTS && (gone_mask[k >> 5] >> (k & 31) & 1); }

static int gone_dead(const BH_PWORK* ep)   /* a killed enemy (not a linked part, not hidden), any slot */
{
    return (ep->flg & 0x83) == 0x3 && !(ep->stflg & 0x1000000) && ep->mode0 && ep->id <= 90;
}

/* guest, every frame before the snapshots: at the moment the host's enemies take over ours */
static void gone_frame(void)
{
    static int was;
    int now = puppets(), k, n = 0;
    Event e = { 0 };
    if (now && !was) {
        memset(gone_mask, 0, sizeof(gone_mask));
        e.type = EV_GONE;
        e.idx = sys->rcase;
        for (k = 0; k < (int)sys->ewk_n && k < PC_ENE_SLOTS; k++) {
            if (!(ene[k].flg & 0x1) || !gone_dead(&ene[k]) || owned[k]) continue;
            gone_mask[k >> 5] |= 1u << (k & 31);
            e.u.gn.slot[e.u.gn.n] = k;
            e.u.gn.ver[e.u.gn.n] = ene[k].mdlver;
            e.u.gn.id[e.u.gn.n++] = ene[k].id;
            n++;
            if (e.u.gn.n == COOP_GONE_MAX) { queue_event(&e); e.u.gn.n = 0; }
        }
        if (e.u.gn.n) queue_event(&e);
        if (n) host_log("coop: the host's enemies take over: %d of ours died meanwhile, the host hides its copies\n", n);
        gone_t = n ? 150 : 0;
    }
    if (!now || (gone_t && !--gone_t)) memset(gone_mask, 0, sizeof(gone_mask));
    was = now;
}

static int apply_gone(const Event* e)
{
    int k;
    if (!is_host || !sys) return 1;
    k = (e->stg * 100 + e->rom) * 10 + e->idx;
    if (k != gone_in.key) gone_in.n = 0;
    gone_in.key = k;
    gone_in.t = 300;   /* the room may still be loading, extra enemies come at its first frame */
    for (k = 0; k < e->u.gn.n && gone_in.n < PC_ENE_SLOTS; k++) {
        gone_in.slot[gone_in.n] = e->u.gn.slot[k];
        gone_in.ver[gone_in.n] = e->u.gn.ver[k];
        gone_in.id[gone_in.n++] = e->u.gn.id[k];
    }
    return 1;
}

/* host, every frame: hide what the guest named, once our room runs */
static void gone_host_frame(void)
{
    int k, j, hid = 0;
    if (!gone_in.n) return;
    if (!--gone_in.t || gone_in.key != room_file_now()) { gone_in.n = 0; return; }
    if (!rom || !(sys->sp_flg & 0x2) || (sys->gm_flg & 0x9) || (sys->cb_flg & 0x1)) return;   /* a door, loading */
    for (k = j = 0; k < gone_in.n; k++) {
        BH_PWORK* ep = &ene[gone_in.slot[k]];
        int s = gone_in.slot[k];
        if (s < (int)sys->ewk_n && (ep->flg & 0x1) && ep->id == gone_in.id[k] && ep->mdlver == gone_in.ver[k] && ep->mode0 && !(ep->flg & 0x80) && !owned[s]) {
            if (!(ep->flg & 0x2) && !(ep->stflg & 0x1000000) && !event_driven(ep)) {
                ep->stflg |= 0x1000000;   /* as EneSetCk; its linked parts go with it */
                ep->flg |= 0x2;           /* dead: the record's DieCk sets its kill flag */
                hid++;
            }
            continue;
        }
        gone_in.slot[j] = gone_in.slot[k]; gone_in.ver[j] = gone_in.ver[k]; gone_in.id[j++] = gone_in.id[k];   /* not there yet (extra enemies): later */
    }
    gone_in.n = j;
    if (hid) host_log("coop: %d enemies the partner killed while we were away hidden here\n", hid);
}

/* at the hand-off: the enemies the other side runs (client: the host's; host: the client's grabs) */
static void apply_snapshots(void)
{
    static unsigned int logged;
    int k, mismatch = 0;
    if (is_host ? !same_room() : !puppets()) return;
    for (k = 0; k < snap_n; k++) {
        EneSnap* s = &snap[k];
        BH_PWORK* ep;
        if (s->idx >= sys->ewk_n || s->idx >= PC_ENE_SLOTS || owned[s->idx] != is_host) continue;
        ep = &ene[s->idx];
        own_wait[s->idx] = 0;
        /* Only enemies set up in our room (bhSetEnemy clears a slot when it takes it; free
           slots keep stale or zero work). mode0 0: its own initialising step (model,
           collision) has not run yet; it must run first, so only wake it like the host's and
           let our loop run that step. Room loads run enemy steps themselves (bhInitEvent), so
           most are initialised before the first snapshot. */
        if (!(ep->flg & 0x1) || (!is_host && (event_driven(ep) || grips_us(ep) || runs_here(ep)))) continue;
        /* the slot layouts differ (an enemy whose Init ran at another moment, a child the other side made, e.g. moths
           and their leader): another type here, or a part linked to a parent there but without one here. Its flags,
           modes and motion would turn a hidden helper without a model into one drawn (bhCalcModel crashed on its NULL
           mlwP, moths in 5/82) or run another type's code with them (the linked flag crashed bhControlEnemy) */
        if (((s->flg & 0x1) && s->id != ep->id) || ((s->flg & 0x80) && !ep->lkwkp)) { mismatch++; continue; }
        if (!is_host && (gone_has(s->idx) || ((ep->flg & 0x80) && gone_has(ene_slot(ep->lkwkp))))) {   /* named dead (EV_GONE) */
            if (!(s->stflg & 0x1000000) && !(s->flg & 0x2)) continue;   /* the host has not hidden it yet */
            if (s->idx < PC_ENE_SLOTS) gone_mask[s->idx >> 5] &= ~(1u << (s->idx & 31));
        }
        if (!ep->mode0) {
            ep->stflg = (ep->stflg & ~0x41000000) | (s->stflg & 0x41000000);
            continue;
        }
        if (!s->mode[0]) continue;   /* a new enemy there whose Init has not run: ours ran, it would run again (its children twice) */
        snap_at[s->idx] = frame_no;
        mnw_set(ep, s->mnw);
        if (s->mdl >= 0 && ep->mdl[s->mdl].objP) ep->mlwP = &ep->mdl[s->mdl];
        if ((int)ep->mtn_no != s->mtn_no) { ep->hokan_count = s->hokan_count ? s->hokan_count + 1 : 0; ep->hokan_rate = s->hokan_rate; }
        ep->mode0 = s->mode[0]; ep->mode1 = s->mode[1]; ep->mode2 = s->mode[2]; ep->mode3 = s->mode[3];
        if (trace && (ep->flg & 0x82) == 0x2 && !(ep->stflg & 0x1000000) && !(s->flg & 0x2) && !(s->stflg & 0x1000000))
            fprintf(stderr, "coop: enemy %d dead here, alive in the host's game: alive again\n", s->idx);
        /* not the host's hit marker (flg & 4): it would come back as a hit of ours */
        ep->flg = s->flg & ~0x4; ep->flg2 = s->flg2 & ~0x4; ep->stflg = s->stflg; ep->mdflg = s->mdflg;
        /* morphing (mdflg 2) draws between the shapes obj_a and obj_b, which only the enemy's own code picks (Alexia's
           tentacles, babies, third form): a puppet that never ran that code has none - the guest crashed drawing it */
        if (!ep->obj_a || !ep->obj_b) ep->mdflg &= ~0x2;
        ep->px = s->px; ep->py = s->py; ep->pz = s->pz;
        ep->ax = s->ax; ep->ay = s->ay; ep->az = s->az;
        ep->mtn_no = s->mtn_no; ep->frm_no = s->frm_no; ep->mtn_md = s->mtn_md; ep->hp = s->hp;
        memcpy(&ep->watr, s->watr, sizeof(s->watr));
    }
    if (mismatch && frame_no - logged > 300) { logged = frame_no; host_log("coop: %d enemy snapshots skipped: the slot holds another kind of enemy here\n", mismatch); }
}

/* ---------------------------------------------------------------- session (M3) */
/* The client plays the host's game: it starts from the host's save region (the bytes a save
   file holds), sent as EV_BLOB chunks and loaded the way "continue" after a game over does
   (bhPopGameData). From then on both sides send the changes of the flag block as EV_FLAGS
   (bits set and cleared, so a flag the game clears again stays consistent); the host wins
   when both change the same word at once. A flag set by taking an item also sends
   EV_PICKUP: the partner's copy in the room disappears (and with shared loot the partner gets
   the item too only if it is one for both: for_both). When the
   host's room changes, the client follows through the game's own door transition. */
#define SAVE_SIZE ((int)((char*)&sys->save_end - (char*)&sys->version))
#define NFLAG 120                       /* ev_flg it_flg ic_flg ed_flg ky_flg mp_flg: 0x90..0x270 */
#define IT_FLG 32                       /* first it_flg word in the block */
#define ITWP_N (int)(39936 / sizeof(O_WRK))   /* sys->itwp, as bhFirstGameStart allocates it */
/* Shared loot, "important items for both" (OPT_IMPORTANT, important_kinds below): each player's own pickups of
   those items live in save-region words no game code uses (the item boxes end at sys->itm[255], GRANT0 below
   starts at 320), so they go wherever the player's items go: its saves, the host's game state,
   EV_OWN and the host's saved partner inventories. IMP_SPOTS: the it_flg bits of important item spots, as learnt
   when one was taken in a session; IMP_TAKEN: which of them this player took (its it_flg bits there). Those bits
   are left out of EV_FLAGS both ways, and a guest loading the host's game state keeps its own (imp_restore). */
#define IMP_SPOTS 256                   /* 16 words: it_flg bits */
#define IMP_TAKEN 272                   /* 16 words */
#define GRANT0 320                      /* GRANT_N entries of 5 words: flr << 24 | item << 16 | count, room, x y z (grant_frame;
                                           x's high half: GRANT_RET, a returned box item's word top byte) */
#define GRANT_N 8
/* sys->itm as a whole (save region 0x270, 384 words; decomp sub1.c AllItemInit clears all of it):
     0..63     the four characters' inventories (16 words each: word 0 the equipped slot, then item words)
     64..191   the item box (sub1.c st->bxp; BOX0 below)
     192..223  the metal detector's tray (cb_flg 0x80000); 224..255 the other special box (0x100000; 224 also
               keeps a count in its low byte, sub1.c 4139 / 6636)
     256..287  IMP_SPOTS, IMP_TAKEN: it_flg bitmaps (any bits)
     288, 289  BioRand: its marker and seed, the pack's id (biorand_glue.c BR_W: any value)
     290       BioRand "Randomize Played as": its marker and the look (biorand_glue.c BR_APPEAR: any value)
     320..359  GRANT0: pending event items (word 0 of each entry an item word, its floor in bits 24..31)
     291..319, 360..382  unused (any value: no code reads them)
     383       the files read (fileview.c: a bit per file, 24 files, so bits 16..23 too)
   Only the item words are items: the game indexes item tables with their bits 16..23. */
static int own_copies(void);
static void imp_restore(const unsigned int* host);
static unsigned char blob[0x900], blob_in[0x900];   /* the game state to load; as it arrives */
_Static_assert(offsetof(SYS_WORK, save_end) - offsetof(SYS_WORK, version) == 0x834 && 0x834 <= sizeof(blob), "EV_BLOB: the save region as the 32-bit game has it");
static CoopXfer blob_x;

/* the host's save region names a room this build has, a character, a difficulty, items and
   weapons the game knows (it indexes tables with them when it loads) */
/* a room file of this stage and room exists (case < 0: any case) */
static int room_exists(int stg, int rom_no, int rcase)
{
    char name[16];
    int f, c;
    if (stg < 0 || stg > 9 || rom_no < 0 || rom_no > 99) return 0;
    for (c = rcase < 0 ? 0 : rcase; c <= (rcase < 0 ? 9 : rcase); c++) {
        snprintf(name, sizeof(name), "RM_%d%02d%d.RDX", stg, rom_no, c);
        for (f = 0; f < rdx_image_data_max && strcmp(rdx_files[f], name); f++) ;
        if (f < rdx_image_data_max) return 1;
    }
    return 0;
}

/* NULL: fine; else what is wrong (the log, the guest's message) */
static const char* blob_valid(const unsigned char* b)
{
#define BF(f) (*(const __typeof__(sys->f)*)(b + ((const char*)&sys->f - (const char*)&sys->version)))
    int i;
    if (BF(version) != 10 || (unsigned char)BF(ply_id) > 3 || BF(stg_no) < 0 || BF(stg_no) > 9 || BF(rom_no) < 0 || BF(rcase) < 0 ||
        BF(rcase) > 9 || BF(pos_no) < 0 || BF(gm_mode) < 0 || BF(gm_mode) > 2 || !finite3(BF(ply_pos).x, BF(ply_pos).y, BF(ply_pos).z))
        return "incompatible save data";
    if (!room_exists(BF(stg_no), BF(rom_no), BF(rcase))) return "room unavailable";
    for (i = 0; i < 4; i++)
        if (BF(ply_wno)[i] < 0 || BF(ply_wno)[i] >= 23) return "invalid weapon data";
    for (i = 0; i < 256; i++)   /* the inventories and boxes (the layout above); the words after them are no items */
        if (((BF(itm)[i] >> 16) & 0xFF) >= 156) return "invalid item data";
    for (i = 0; i < GRANT_N; i++) {
        unsigned int w = BF(itm)[GRANT0 + i * 5];
        if (((w >> 16) & 0xFF) >= 156 || (w >> 24) > 63) return "invalid event item data";
    }
    return NULL;
#undef BF
}
static unsigned int last[NFLAG];        /* the flag block as the partner knows it */
static int own_live, own_sent_ok;       /* guest: we play in the host's game of this session; our items went to it (EV_OWN) */
static unsigned int my_sid, peer_sid, blob_end;   /* blob_end: host, id of the last chunk sent */
static int blob_due, need_blob, pop_pending, popping, follow_pending, host_room = -1;
/* guest: the host's BioRand seed string and CLI digest (EV_BRSEED, protocol 30), and the offer to generate its pack
   (biorand_from_host, the BIORAND screen): BR_NONE; BR_ASK and BR_GEN hold the join (no game state is loaded) while
   the screen asks and the CLI runs; BR_DONE: generated (or not), the next mismatch leaves */
enum { BR_NONE, BR_ASK, BR_GEN, BR_DONE };
static char br_str[COOP_BR_STR + 1], br_dig[COOP_BR_DIG + 1];
static int br_offer, br_got;
static unsigned int br_wait;            /* when a mismatch was first seen before EV_BRSEED came (net_ms, 0 none) */
static unsigned char regroups, seen_regroup;   /* host: regroups so far; client: the last one acted on */

static unsigned int* flag_block(void) { return sys->ev_flg; }
static int in_session_game(void) { return (sys->tk_flg & 0x80) && !(sys->ss_flg & 0x400000); }   /* game task, not the attract demo */

/* "Game over: Both" (OPT_GAMEOVER_BOTH, docs/COOP.md "Game over"): one game over is both players'; the host's CONTINUE / NO
   decides. Each game sends PF_GAME_OVER while its game over runs. The host keeps the guest's items (EV_OWN) as they were at
   its continue point (bhPushGameData -> coop_retry_point) and puts them into the game state its CONTINUE sends (EV_BLOB
   pad 2), so both start from the same point with what each had there. EV_OWN's tid names the last game state the guest
   got: the guest's items from before a shared continue reached it are not taken as its current ones. */
static unsigned char own_done[COOP_OWN_SIZE], retry_own[COOP_OWN_SIZE];   /* host: the guest's items now; at our continue point */
static int own_done_ok, retry_ok;
static int go_was, go_cont, go_want, go_seen, go_held;   /* our game over ran; it ended in CONTINUE (host); the partner's
                                                            started ours; its PF_GAME_OVER; we wait for the host's choice */
static unsigned short cont_tid, own_tid;   /* host: the transfer of our last shared continue; guest: the last game state we got */
static int go_running(void) { return (sys->tk_flg & 0x4000) && !(sys->ts_flg & 0x4000); }   /* gameover.c's task runs */
static int rule(int opt);
static void world_reset(void);
static void trade_reset(void);
static void ts_reset(void);
static void grant_forget(void);

/* a new partner session (or ours restarted): both event streams start again from id 1 */
static void session_reset(void)
{
    if (is_host) owned_drop(); else memset(owned, 0, sizeof(owned));
    own_live = own_sent_ok = 0;
    own_done_ok = retry_ok = go_want = go_seen = 0; cont_tid = 0;   /* a new partner session: its items come again */
    in_last = 0; coop_ring_reset(&ring); blob_end = 0; snap_n = 0;
    coop_xfer_init(&blob_x, blob_in, sizeof(blob_in));   /* half a transfer of the old session is no use */
    coop_xfer_init(&estate_x, estate, sizeof(estate));
    if (is_host) blob_due = 1; else need_blob = 1;
    world_reset();
    trade_reset();   /* after the stream reset: an open offer of ours asks again */
    ts_reset();      /* the trade screen closes; a swap of ours on its way asks again */
}

/* client (M8): our own items, item box, health, weapon and status - kept when the host's game
   state is loaded (joining, a host restart, a story switch). Taken at every frame in a game
   (our own before joining, the host's after). A partner who joins from the title without a
   game of its own starts with the new-game inventory of the session's game mode. */
static struct { unsigned int itm[384], stflg[4]; char wno[4]; short hp[4]; } own;
static int own_valid, own_fixed;   /* own_fixed: a source was given (coop_own_source), no snapshots */
static int own_cont;               /* our session ended while we played in the host's game, which goes on here (until the title) */
static int own_hold;               /* our own game restarted (continue after a game over): `own` keeps the items we died with */
/* saved partner inventories (protocol 16, "saved partner inventories" below), the guest's side */
static int blob_rec, want_rec;          /* the host's game state on its way carries our saved items; we requested them */
static char note_msg[256];              /* "Restored your saved items ...": shown with the status line for a while */
static unsigned int note_until;
static void note(const char* m) { snprintf(note_msg, sizeof(note_msg), "%s", tr(m)); note_until = net_ms() + 30000; host_log("coop: %s\n", m); }

/* The inventory source for the next join: a save region (the bytes a save file holds, from
   sys->version; e.g. a save slot chosen in a lobby). NULL: our current game state (default). */
void coop_own_source(const void* save_region)
{
    const unsigned char* b = save_region;
#define OFS(f) ((const char*)&sys->f - (const char*)&sys->version)
    own_fixed = save_region != NULL;
    if (!b) return;
    memcpy(own.itm, b + OFS(itm), sizeof(own.itm));
    memcpy(own.stflg, b + OFS(ply_stflg), sizeof(own.stflg));
    memcpy(own.wno, b + OFS(ply_wno), sizeof(own.wno));
    memcpy(own.hp, b + OFS(ply_hp), sizeof(own.hp));
#undef OFS
    own_valid = 1;
}

static void own_snapshot(void)
{
    int id = sys->ply_id & 3;
    memcpy(own.itm, sys->itm, sizeof(own.itm));
    memcpy(own.stflg, sys->ply_stflg, sizeof(own.stflg));
    memcpy(own.wno, sys->ply_wno, sizeof(own.wno));
    memcpy(own.hp, sys->ply_hp, sizeof(own.hp));
    own.stflg[id] = plp->stflg & 0x78280000;   /* the live values, as bhPushGameData takes them */
    own.wno[id] = plp->wpnr_no;
    own.hp[id] = plp->hp;
    own_valid = 1;
}

/* the lobby (pc/glue/lobby_glue.c): the joining player's items from a save region
   (coop_own_source), or the new-game items (NULL) */
void coop_own_from_save(const void* save)
{
    coop_own_source(save);
    if (!save) { own_fixed = 1; own_valid = 0; }
}

/* Co-op rule "Quick start" (OPT_QUICK_START, protocol 23, docs/COOP.md "Quick start"): a new-game inventory as if
   the player had read the manual and taken the knife in the first room (0/0). Reading the manual (item 145, FILE
   entry 0, which the new game already lists) takes it out of the bag and sets ic_flg[15] bit 31 (fileview.c); the
   Combat Knife on 0/0's table is item trigger 7 with it_flg bit 120 (its scd1 ItmCk; scd0's ItmSetCk leaves the spot
   empty), taken as bhItmCk takes it. The knife goes where the manual was; a bag without the manual is no new game's.
   With "important items for both" the spot is an important one (coop_item_taken): ours taken, a partner whose items
   came from elsewhere still finds its own knife there. */
#define QS_MANUAL 0x910001u             /* item 145, one: as AllItemInit hands it out */
#define QS_KNIFE 0x80000u               /* item 8, count 0: as GetItem hands it out (getbulletmax) */
#define QS_KNIFE_BIT 120
static void quick_start(void)
{
    unsigned int* inv = &sys->itm[(sys->ply_id & 3) * 16], m = 0x80000000u >> (QS_KNIFE_BIT & 31);
    int i, w = QS_KNIFE_BIT >> 5;
    for (i = 2; i < 10 && inv[i] != QS_MANUAL; i++);   /* the bag: inv[2..9] */
    if (i == 10) return;
    inv[i] = QS_KNIFE;
    sys->ic_flg[15] |= 0x80000000u;
    sys->it_flg[w] |= m;
    sys->ic_flg[w] &= ~m;
    if (own_copies()) { sys->itm[IMP_SPOTS + w] |= m; sys->itm[IMP_TAKEN + w] |= m; }
    host_log("coop: quick start: the manual read, the knife in the bag (its spot in 0/0 taken)\n");
}

/* fileview.c FileExit01: the first FILE read of the manual takes it out of the bag (the original's "manual glitch"
   rests on that: the equipped slot is not moved along, so it points at the item after the weapon). "First" is
   ic_flg[15] bit 31, in the item flags that shared loot and a join's state give both players: the partner's read set
   ours, and our manual stayed. Our manual still in our bag is "not read" (in a single-player game the bit is never
   set while it is there, so nothing changes there). */
int pc_manual_in_bag(void)
{
    const unsigned int* inv = &sys->itm[(sys->ply_id & 3) * 16];
    int i;
    for (i = 1; i < 12 && inv[i] != QS_MANUAL; i++);
    return i < 12;
}

/* system.c bhSysCallMonitor: the host's new game has its items (AllItemInit); the guest's: own_restore */
void pc_quick_start(void)
{
    pc_biorand_stamp();   /* the new game's BioRand state into the save words (biorand_glue.c) */
    pc_biorand_appear_new();   /* and "Randomize Played as" */
    if (coop_on && is_host && rule(OPT_QUICK_START)) quick_start();
}

/* after bhPopGameData put the host's state in place (the game reads it at the restart) */
static void own_restore(void)
{
    unsigned int box[160], ic = sys->ic_flg[15], spots[16];
    /* our saved items came with the state (the host's record of us): requested, or a rejoin after
       our game ended without a goodbye - not over the game we are playing (a resync) */
    int i, rec = blob_rec == 2 || (blob_rec && (want_rec || own_fixed || !own_valid));   /* 2: a shared game over's continue */
    memcpy(box, &sys->itm[64], sizeof(box));   /* the host's: with shared loot, the one box both use */
    memcpy(spots, &sys->itm[IMP_SPOTS], sizeof(spots));   /* the important item spots the host's game knows */
    own_fixed = 0;   /* a given source applies to this join; from now on our game state */
    if (rec) note(blob_rec == 2 ? "Game over for both: back at the host's continue point, with the items you had there"
                                : "Restored your saved items from the host");   /* already in place: the host put them into the state */
    else if (own_valid) {
        memcpy(sys->itm, own.itm, sizeof(own.itm));
        memcpy(sys->ply_stflg, own.stflg, sizeof(own.stflg));
        memcpy(sys->ply_wno, own.wno, sizeof(own.wno));
        memcpy(sys->ply_hp, own.hp, sizeof(own.hp));
        host_log("coop: keeping our own inventory (hp %d)\n", own.hp[sys->ply_id & 3]);
        if (want_rec) note("No saved items at the host - you keep your current items");   /* joined again from our game (own_cont) */
    } else {
        AllItemInit();
        sys->ic_flg[15] = ic;   /* AllItemInit clears one "examined" bit */
        if (rule(OPT_QUICK_START)) quick_start();
        pc_biorand_start_kit();   /* BioRand: the pack's start inventory, as the host's new game had it */
        for (i = 0; i < 4; i++) {   /* as a new game (system.c) */
            sys->ply_hp[i] = sys->gm_mode == 2 ? 320 : 160;
            sys->ply_stflg[i] = 0x40000000;
            sys->ply_wno[i] = 0;
        }
        host_log("coop: no game of our own: starting with the new-game inventory\n");
        if (want_rec) note(rx_ext.rec == REC_OFF ? "The host keeps no saved items - starting with a new inventory"
                                                 : "No saved items at the host - starting with a new inventory");
    }
    for (i = 0; i < 4; i++)   /* we died (joining again after the game over): full health, as a new game - else we died again at once, in a loop */
        if (sys->ply_hp[i] < 0) sys->ply_hp[i] = sys->gm_mode == 2 ? 320 : 160;
    if (!(rx.opts & OPT_INDIVIDUAL)) memcpy(&sys->itm[64], box, sizeof(box));
    if (own_copies()) imp_restore(spots);
    pc_biorand_stamp();   /* our saves of this game: the session's BioRand state */
    blob_rec = want_rec = 0;   /* this join's; later states (a story switch, a host restart) keep our own */
}

/* host: our enemy options to the guest (docs/ENEMIES.md "Enemy options"): the placements and the
   randomizer change records at the room load on both sides alike */
static EnemyOpts eopts_sent;
static unsigned int eopts_sid;          /* the partner session they went to */
/* and the cutscene cast's "Custom" list (cast.h cast_off, EV_CASTOFF, protocol 31): each chunk that changed, all of
   it to a new partner; before the options that use it */
_Static_assert(CAST_OFF_BYTES <= COOP_CASTOFF_MAX, "EV_CASTOFF");
static unsigned char castoff_sent[CAST_OFF_BYTES];
static unsigned int castoff_sid;
static void castoff_sync(void)
{
    int at, n;
    for (at = 0; at < CAST_OFF_BYTES; at += COOP_CASTOFF_CHUNK) {
        Event e = { 0 };
        n = CAST_OFF_BYTES - at < COOP_CASTOFF_CHUNK ? CAST_OFF_BYTES - at : COOP_CASTOFF_CHUNK;
        if (castoff_sid == peer_sid && !memcmp(castoff_sent + at, cast_off + at, n)) continue;
        e.type = EV_CASTOFF;
        e.u.co.at = (uint16_t)at;
        e.u.co.n = (uint8_t)n;
        memcpy(e.u.co.bits, cast_off + at, n);
        memcpy(castoff_sent + at, e.u.co.bits, n);
        queue_event(&e);
    }
    castoff_sid = peer_sid;
}
static void eopts_send(void)
{
    Event e = { 0 };
    castoff_sync();
    e.type = EV_EOPTS;
    e.u.eo = g_enemy_opts;
    queue_event(&e);
    eopts_sent = g_enemy_opts;
    eopts_sid = peer_sid;
}

/* host: the records this room load's randomizer had to keep (enemyopt_glue.c), for the guest (SPAWN_NOTE_KEPT):
   sent once it is in game and in control (an older build would hold its event stream while its room does not
   run), and with every game state we send (a guest that joins loads the room after it) */
static struct { SpawnNote n[32]; unsigned char rec[32]; int count, gen; unsigned int sent_sid; } kept_out;
void coop_rand_kept(int rec, const SpawnNote* n)
{
    if (!coop_on || !is_host || echo_delay) return;
    if (kept_out.gen != enemy_room_gen) { kept_out.gen = enemy_room_gen; kept_out.count = 0; kept_out.sent_sid = 0; }
    if (kept_out.count < 32) { kept_out.rec[kept_out.count] = (unsigned char)rec; kept_out.n[kept_out.count++] = *n; }
}
static void kept_send(void)
{
    int k;
    if (kept_out.gen != enemy_room_gen || !kept_out.count) return;
    for (k = 0; k < kept_out.count; k++) {
        Event e = { 0 };
        e.type = EV_SPAWN;
        e.idx = kept_out.rec[k];
        e.u.sp = kept_out.n[k];
        queue_event(&e);
    }
    kept_out.sent_sid = peer_sid;
    host_log("coop: %d records the randomizer kept here sent to the partner\n", kept_out.count);
}

/* host: an enemy that is not in the room file (spawn_glue.c) to a guest in our room */
void coop_spawn_note(int slot, const SpawnNote* n)
{
    Event e = { 0 };
    if (!coop_on || !is_host || echo_delay || !have_peer || !same_room()) return;
    e.type = EV_SPAWN;
    e.idx = (unsigned char)slot;
    e.u.sp = *n;
    queue_event(&e);
}

/* host, while it plays BioRand: its pack's seed string and its CLI's digest (EV_BRSEED, protocol 30) to a new partner
   and whenever they change, so a guest without the pack can generate it (biorand_from_host). Sent before the
   extension that says br is read: in the same datagram its events come first. */
static void brseed_sync(void)
{
    static Event sent;
    static unsigned int sid;
    Event e = { 0 };
    const char* s = biorand_seed_string();
    if (!biorand_next()) return;
    e.type = EV_BRSEED;
    memcpy(e.u.br.str, s, strlen(s) < COOP_BR_STR ? strlen(s) : COOP_BR_STR);   /* (68 or 0) */
    if (!strncmp(g_biorand_cli_digest, "sha256:", 7) && strlen(g_biorand_cli_digest) >= 7 + COOP_BR_DIG)
        memcpy(e.u.br.dig, g_biorand_cli_digest + 7, COOP_BR_DIG);
    if (sid == peer_sid && !memcmp(&sent.u.br, &e.u.br, sizeof(e.u.br))) return;
    sent = e;
    sid = peer_sid;
    if (coop_check_event(&e, 1)) { host_log("coop: our BioRand seed string or CLI digest is not one to send: %s\n", s); return; }
    queue_event(&e);
    host_log("coop: our BioRand seed string %s sent to the partner\n", s[0] ? s : "(unknown)");
}

/* host, every frame: the options go out to a new partner (the guest's lobby shows them) and again
   whenever they change (the lobby, F1, OPTION; they take effect at the next room load); a guest that (re)enters our room gets the room's
   added enemies again (it ignores those it has) */
static void enemy_sync(void)
{
    static int was;
    int now;
    if (!is_host || !have_peer || echo_delay) { was = 0; return; }
    castoff_sync();
    if (eopts_sid != peer_sid || memcmp(&eopts_sent, &g_enemy_opts, sizeof(eopts_sent))) eopts_send();
    brseed_sync();
    if (kept_out.sent_sid != peer_sid && have_rx && (rx.flags & (PF_IN_GAME | PF_VISIBLE)) == (PF_IN_GAME | PF_VISIBLE)) kept_send();
    now = same_room() && tx_in_game && !(sys->gm_flg & 0x9) && !(sys->cb_flg & 0x1);
    if (now && !was) spawn_replay();
    was = now;
}

static int blob_record(unsigned char* b);
static void own_put(unsigned char* b, const unsigned char* o);
static void inv_log(const char* tag);
/* host: the save region as a save at this moment holds it (bhPushGameData without writing
   the retry copy) */
static void send_blob(void)
{
    static unsigned char keep[0x900];
    int n = SAVE_SIZE, off, rec;
    memcpy(keep, &sys->version, n);
    sys->version = 10;
    sys->flr_no = plp->flr_no;
    sys->ply_stflg[(int)sys->ply_id] = plp->stflg & 0x78280000;
    sys->ply_pos.x = plp->px; sys->ply_pos.y = plp->py; sys->ply_pos.z = plp->pz;
    sys->ply_ang = plp->ay;
    sys->ply_wno[(int)sys->ply_id] = plp->wpnr_no;
    sys->ply_hp[(int)sys->ply_id] = plp->hp;
    memcpy(blob, &sys->version, n);
    memcpy(&sys->version, keep, n);
    rec = blob_record(blob);   /* a partner's saved items in place of ours (saved partner inventories) */
    if (go_cont && rule(OPT_GAMEOVER_BOTH) && retry_ok) {   /* our CONTINUE of a shared game over: the guest's items as at this point */
        own_put(blob, retry_own);
        memcpy(own_done, retry_own, COOP_OWN_SIZE);   /* what it has once it loaded this state */
        rec = 2;
        host_log("coop: game over for both: the partner continues with its items as at our continue point\n");
    }
    go_cont = 0;
    eopts_send();   /* the guest loads its first room with the host's enemy options */
    kept_send();    /* and the records our randomizer kept in it */
    if (!++xfer_tid) xfer_tid = 1;
    if (rec == 2) cont_tid = xfer_tid;
    for (off = 0; off < n; off += 64) {
        Event e = { 0 };
        e.type = EV_BLOB;
        e.u.blob.tid = xfer_tid;
        e.u.blob.total = n;
        e.u.blob.off = off;
        e.u.blob.len = n - off < 64 ? n - off : 64;
        e.u.blob.pad = (unsigned char)rec;
        memcpy(e.u.blob.data, blob + off, e.u.blob.len);
        queue_event(&e);
    }
    blob_end = ring.next - 1;
    memcpy(last, flag_block(), sizeof(last));
    blob_due = 0;
    host_log("coop: sent the game state (stage %d room %d)\n", sys->stg_no, sys->rom_no);
}

/* client: start the host's game, as "continue" does from the copy made at room entry */
static void load_blob(void)
{
    short retry = *(short*)(blob + ((char*)&sys->retry_ct - (char*)&sys->version));
    memcpy(freemem, blob, SAVE_SIZE);
    /* from the attract demo (it runs as a game): leave it, or the pad stays on the recording */
    if (sys->ss_flg & 0x400000) sys->keytype = sys->pdm_keytpb;
    sys->ss_flg &= ~0xC00000;
    if (sys->tk_flg & 0x8) pc_title_leave();   /* from the title (its menu or the lobby): as the title ends */
    if (go_running()) sys->ts_flg |= 0x4000;   /* from our game over (the host's CONTINUE of a shared one): as its continue */
    own_hold = go_held = 0;
    pc_adv_in_game();   /* the option screen's app mode: in a game (the title's own game choices set it) */
    bhPopGameData();
    sys->retry_ct = retry;   /* not a retry */
    own_restore();
    own_live = 1;   /* from now on our items go to the host (EV_OWN) */
    inv_log("joined");
    memcpy(last, flag_block(), sizeof(last));
    pop_pending = 0;
    popping = 1;
    follow_pending = 0;
    host_room = rx_room();
    seen_regroup = rx.regroup;   /* we start in the host's room */
    host_log("coop: joined the host's game (stage %d room %d)\n", sys->stg_no, sys->rom_no);
}

/* ---------------------------------------------------------------- saved partner inventories, rejoin (protocol 16)
   docs/COOP.md "Saved partner inventories". Each installation has a player id and a secret
   (recvx.ini, config.c). A guest sends its id and rejoin tag (rdv_rejoin_tag: its secret over the
   session's code - the host's join code, or the room code through a server) in its PoseExt and,
   once it plays in our game, its own items (`own`: item words, status, weapons, health) as EV_OWN:
   the 64-byte chunks that changed, at most once a second. The host keeps them per player id
   ("records", while "Remember partner inventories" is on) and writes them next to its save when it
   saves (coop-partners-nn.bin, lobby_io.c); a game started from that save has them again, a new
   game has none. Kept with the save, not written as they change: after the host reloads a save, a
   key item the partner took later lies in the room again - a newer record would have it twice.
   A guest that chose "saved with the host", or comes back after its game ended without a goodbye,
   gets its record inside the host's game state (EV_BLOB chunks with pad 1) instead of the items it
   brought. The same id and tag from a new address while the stale session is still there replace
   that session at once (takeover); through a server, the server does that (protocol 3).
   Trust: the records are the host's data, and the host could hand anyone anything anyway. A
   player id is seen only by the hosts it played with; whoever knows it and the join code could
   claim those items at that host - fine among friends. Taking over a session needs the tag. */
_Static_assert(sizeof(own) == COOP_OWN_SIZE, "EV_OWN carries `own` as it is");
#define REC_MAX 16
#define REC_MAGIC 0x50564352u           /* "RCVP", the file */
typedef struct Rec { unsigned char pid[16]; char name[16]; unsigned char own[COOP_OWN_SIZE]; } Rec;
static Rec recs[REC_MAX];
static int rec_n;
static int title_seen = 1;              /* we were at the title: the next game takes the records of the save it came from */
static unsigned char id_pid[16], id_tag[16], crash_pid[16];   /* host: the partner session's player id and tag; a partner gone without a goodbye */
static int id_ok, id_rejoin, crash_ok, rec_done;
static char id_name[COOP_NAME_MAX];
static unsigned char own_in[COOP_OWN_SIZE];   /* host: the guest's items as its EV_OWN chunks came */
static unsigned char own_sent[COOP_OWN_SIZE], my_tag[16];   /* guest: what went to the host; our rejoin tag for this session */
static unsigned int own_sent_at;
static int items_applied = 99;          /* guest: the g_coop.items coop_items_apply used */
static void peer_text(char* out, const char* in, int n);

static const unsigned char zeros16[16];
#define same16 rdv_same16   /* constant time */
#define zero16(b) same16(b, zeros16)
static const char* pid_str(const unsigned char* pid) { static char t[12]; snprintf(t, sizeof(t), "%02x%02x%02x%02x", pid[0], pid[1], pid[2], pid[3]); return t; }
static Rec* rec_find(const unsigned char* pid)
{
    int i;
    if (zero16(pid)) return NULL;
    for (i = 0; i < rec_n; i++) if (same16(recs[i].pid, pid)) return &recs[i];
    return NULL;
}

/* host: we keep saved items of this player for the game it will play: in a game its records; at
   the title (the lobby) those of the save the lobby's START will load, none for a new game */
static int rec_file_slot = -1;           /* rec_have's copy of a partners file: the save slot it is of (-1: none) */
static int rec_have(const unsigned char* pid)
{
    static unsigned char f[12 + sizeof(recs)];
    static int n;
    unsigned int h[3];
    int i;
    if (!title_seen) return rec_find(pid) != NULL;
    if (g_coop.start < 3) return 0;
    if (rec_file_slot != g_coop.start - 3) {   /* the file of that save, read once (coop_save_written: again) */
        int slot = rec_file_slot = g_coop.start - 3;
        n = lobby_read_partners(slot, f, sizeof(f));
        memcpy(h, f, 12);
        if (n < 12 || h[0] != REC_MAGIC || h[1] != 1 || h[2] > REC_MAX || n != 12 + (int)(h[2] * sizeof(Rec))) n = 0;
        else n = (int)h[2];
    }
    for (i = 0; i < n && !zero16(pid); i++)
        if (same16(f + 12 + i * sizeof(Rec) + offsetof(Rec, pid), pid)) return 1;
    return 0;
}

/* host: a partner session's first PoseExt: who it is */
static void partner_identity(void)
{
    memcpy(id_pid, rx_ext.pid, 16);
    memcpy(id_tag, rx_ext.tag, 16);
    peer_text(id_name, rx_ext.name, sizeof(id_name));
    id_ok = 1;
    id_rejoin = crash_ok && !zero16(id_pid) && same16(crash_pid, id_pid);
    crash_ok = 0;
    host_log("coop: partner %s is player %s%s; %s\n", id_name, pid_str(id_pid), id_rejoin ? ", back after its session ended without a goodbye" : "",
             !g_coop.remember ? "we keep no saved items (Remember partner inventories is off)" : rec_have(id_pid) ? "we have its saved items" : "no saved items of it yet");
}

static void escrow_home(void);
static void escrow_taken(unsigned int w);
/* host: the partner session ended; without a goodbye (crash, time-out, removed, replaced) its
   player's next session is a rejoin: it gets its saved items whatever it chose. The grabs its game
   ran are ours again (a zombie holding the guest stayed a frozen puppet after a goodbye, a kick or
   a time-out). */
static void partner_gone(int bye)
{
    if (id_ok && !bye && !zero16(id_pid)) { memcpy(crash_pid, id_pid, 16); crash_ok = 1; }
    id_ok = id_rejoin = rec_done = 0;
    memset(own_in, 0, sizeof(own_in));   /* no chunk of this partner's items can end up in the next one's record */
    owned_drop();
    escrow_home();
}

/* host, send_blob: the partner's saved items in place of ours in its first game state of the
   session; 1 when they are in */
static int blob_record(unsigned char* b)
{
    Rec* r;
    char m[256];
    if (!id_ok || rec_done || !g_coop.remember) return 0;
    rec_done = 1;
    if (!(rx_ext.items == ITEMS_HOST || id_rejoin) || !(r = rec_find(id_pid))) return 0;
    own_put(b, r->own);
    if (id_name[0]) snprintf(m, sizeof(m), tr("Restored %s's saved items"), id_name);
    else snprintf(m, sizeof(m), tr("Restored the partner's saved items"));
    note(m);
    return 1;
}

/* host: a guest's items (EV_OWN bytes) in place of ours in a game state we send it */
static void own_put(unsigned char* b, const unsigned char* o)
{
#define BO(f) ((char*)&sys->f - (char*)&sys->version)
#define OO(f) offsetof(__typeof__(own), f)
    unsigned int box[160];
    memcpy(box, b + BO(itm) + 64 * 4, sizeof(box));
    memcpy(b + BO(itm), o + OO(itm), sizeof(own.itm));
    {   /* the important item spots both games know (the guest keeps its own pickups of them: imp_restore) */
        unsigned int* spots = (unsigned int*)(b + BO(itm)) + IMP_SPOTS;
        int k;
        for (k = 0; k < 16; k++) spots[k] |= sys->itm[IMP_SPOTS + k];
    }
    if (!g_coop.loot) memcpy(b + BO(itm) + 64 * 4, box, sizeof(box));   /* shared loot: one item box, ours */
    memcpy(b + BO(ply_stflg), o + OO(stflg), sizeof(own.stflg));
    memcpy(b + BO(ply_wno), o + OO(wno), sizeof(own.wno));
    memcpy(b + BO(ply_hp), o + OO(hp), sizeof(own.hp));
#undef BO
#undef OO
}

/* room.c bhPushGameData (host): our continue point moved - a room's retry point, a save, a game (re)start. The guest's
   items as they are now go with it; the push of our own CONTINUE's restart is the same point, it keeps them. */
void coop_retry_point(void)
{
    if (!is_host || echo_delay || go_cont) return;
    retry_ok = own_done_ok;
    if (retry_ok) memcpy(retry_own, own_done, COOP_OWN_SIZE);
    if (trace) fprintf(stderr, "coop: continue point in %d/%d (the partner's items %s)\n", sys->stg_no, sys->rom_no, retry_ok ? "kept with it" : "not known yet");
}

/* host: EV_OWN, a chunk of the guest's items; a change's last chunk makes them its record */
static void apply_own(const Event* e)
{
    Rec* r;
    memcpy(own_in + e->u.blob.off, e->u.blob.data, e->u.blob.len);
    if (e->u.blob.pad && (!cont_tid || (short)(e->u.blob.tid - cont_tid) >= 0)) {   /* not from before our last shared continue reached it */
        memcpy(own_done, own_in, COOP_OWN_SIZE);
        own_done_ok = 1;
        if (!retry_ok) { memcpy(retry_own, own_in, COOP_OWN_SIZE); retry_ok = 1; }   /* it joined after our continue point: as it joined */
    }
    if (!e->u.blob.pad || !id_ok || zero16(id_pid) || !g_coop.remember) return;
    if (!(r = rec_find(id_pid))) {
        if (rec_n == REC_MAX) { memmove(recs, recs + 1, sizeof(Rec) * (REC_MAX - 1)); rec_n--; }   /* ponytail: the oldest player out; 16 partners per playthrough */
        r = &recs[rec_n++];
        memcpy(r->pid, id_pid, 16);
        host_log("coop: keeping %s's items (player %s)\n", id_name, pid_str(id_pid));
    }
    memcpy(r->name, id_name, sizeof(r->name));
    memcpy(r->own, own_in, COOP_OWN_SIZE);
    if (trace) fprintf(stderr, "coop: %s's saved items updated\n", id_name);
}

/* guest: our items to the host when they changed (only the chunks that did), at most once a second */
static void own_send(void)
{
    const unsigned char* b = (const unsigned char*)&own;
    int off, last = -1;
    if (!own_live || !own_valid || own_fixed || frame_no - own_sent_at < 30) return;
    for (off = 0; off < COOP_OWN_SIZE; off += 64)
        if (!own_sent_ok || memcmp(b + off, own_sent + off, COOP_OWN_SIZE - off < 64 ? COOP_OWN_SIZE - off : 64)) last = off;
    if (last < 0) return;
    for (off = 0; off <= last; off += 64) {
        Event e = { 0 };
        int len = COOP_OWN_SIZE - off < 64 ? COOP_OWN_SIZE - off : 64;
        if (own_sent_ok && off < last && !memcmp(b + off, own_sent + off, len)) continue;
        e.type = EV_OWN;
        e.u.blob.tid = own_tid;   /* the game state these items go with (a shared continue replaces them) */
        e.u.blob.total = COOP_OWN_SIZE;
        e.u.blob.off = off;
        e.u.blob.len = len;
        e.u.blob.pad = off == last;   /* the change is complete with this one */
        memcpy(e.u.blob.data, b + off, len);
        queue_event(&e);
    }
    memcpy(own_sent, b, COOP_OWN_SIZE);
    own_sent_ok = 1;
    own_sent_at = frame_no;
}

/* the joining inventory (g_coop.items): a save's items, the new-game items, our game in progress;
   "saved with the host" starts as the new-game items until the host's state brings the record */
int coop_items_apply(void)
{
    static unsigned char b[LOBBY_SAVE_BYTES];
    int v = g_coop.items, ok = 0;
    items_applied = v;
    want_rec = v == COOP_ITEMS_HOST;
    if (v >= 0 && (ok = lobby_read_save(v, b))) coop_own_source(b);
    /* our game: its snapshots. "Saved with the host" from the game we played in the host's session (we lost the
       connection, the host crashed, we left): the host's record if it has one, else these items, not a new inventory
       (the host's game started again from a save without our record had us start Chris's part from scratch) */
    else if ((v == COOP_ITEMS_CURRENT || (v == COOP_ITEMS_HOST && own_cont)) && sys && in_session_game()) coop_own_source(NULL);
    else coop_own_from_save(NULL);
    return ok;
}
static int items_wire(void)
{
    int v = items_applied;
    return v == COOP_ITEMS_HOST ? ITEMS_HOST : v == COOP_ITEMS_CURRENT ? ITEMS_CURRENT : v >= 0 ? ITEMS_SAVE : ITEMS_NEW;
}

/* the memory card wrote save `slot` (mc.c): our partners' items go next to it; none removes a
   stale file of the save that was there */
static void records_resolve(void);
void coop_save_written(int slot)
{
    static unsigned char f[12 + sizeof(recs)];
    unsigned int h[3] = { REC_MAGIC, 1, 0 };
    records_resolve();   /* a game we never hosted in: the records it came with go on to the new save */
    h[2] = (unsigned int)rec_n;
    memcpy(f, h, 12);
    memcpy(f + 12, recs, rec_n * sizeof(Rec));
    rec_file_slot = -1;   /* rec_have reads it again */
    if (!lobby_write_partners(slot, f, rec_n ? 12 + rec_n * (int)sizeof(Rec) : 0)) host_log("coop: cannot write the partners' saved items of save %d\n", slot + 1);
    else if (rec_n) host_log("coop: the saved items of %d partner%s written with save %d\n", rec_n, rec_n == 1 ? "" : "s", slot + 1);
}

/* a game starts after the title: the records of the save it was loaded from (a save with a partners
   file whose item words and room are the game's), else none (a new game). What the game started
   with is kept here; the files are read once we host (records_resolve), never in single player. */
static unsigned int game_itm[384];
static __typeof__(((SYS_WORK*)0)->stg_no) game_stg;
static __typeof__(((SYS_WORK*)0)->rom_no) game_rom;
static int rec_pending;
static void records_for_game(void)
{
    memcpy(game_itm, sys->itm, sizeof(game_itm));
    game_stg = sys->stg_no;
    game_rom = sys->rom_no;
    rec_n = 0;
    rec_pending = 1;
}
static void records_resolve(void)
{
    static unsigned char b[LOBBY_SAVE_BYTES], f[12 + sizeof(recs)];
    const int itm = (int)((char*)&sys->itm - (char*)&sys->version), stg = (int)((char*)&sys->stg_no - (char*)&sys->version),
              rno = (int)((char*)&sys->rom_no - (char*)&sys->version);
    unsigned int h[3];
    LobbySave* l;
    int i, slot, n, saves;
    if (!rec_pending) return;
    rec_pending = 0;
    for (i = 0, saves = lobby_save_list(&l); i < saves; i++) {
        slot = l[i].slot;
        if ((n = lobby_read_partners(slot, f, sizeof(f))) < 12 || !lobby_read_listed(&l[i], b)) continue;
        if (memcmp(b + itm, game_itm, sizeof(game_itm)) || memcmp(b + stg, &game_stg, sizeof(game_stg)) || memcmp(b + rno, &game_rom, sizeof(game_rom))) continue;
        memcpy(h, f, 12);
        if (h[0] != REC_MAGIC || h[1] != 1 || h[2] > REC_MAX || n != 12 + (int)(h[2] * sizeof(Rec))) { host_log("coop: the partners file of save %d is not valid: ignored\n", slot + 1); break; }
        rec_n = (int)h[2];
        memcpy(recs, f + 12, rec_n * sizeof(Rec));
        host_log("coop: save %d: the saved items of %d partner%s\n", slot + 1, rec_n, rec_n == 1 ? "" : "s");
        break;
    }
    free(l);
}

/* Loot (M4). "Item" flags are it_flg (taken) and ic_flg (examined, not taken) - block words
   IT_FLG..IT_FLG+31; everything else (ev_flg story, ed_flg enemies/doors, ky_flg locks,
   mp_flg maps) is "progress" and always shared. With individual loot the item words are not
   synced, so each player takes their own copy; key items (itemdata type 0x40 and landing in
   the inventory - files have that type too but are not inventory items) stay shared: their
   pickup goes out as EV_PICKUP carrying the spot's it_flg bit. */
/* the host's options as its last pose had them: kept through a stalled link (3..15 s without a pose,
   have_rx 0), where "shared" sent our item flags and pickups to the host - the host's copies went */
static int peer_opts = -1;
static int individual(void) { return is_host ? g_coop.loot : peer_opts >= 0 && (peer_opts & OPT_INDIVIDUAL); }
static int key_item(int id) { return id > 0 && id < 156 && (itemdata[id].type & 0x40); }
static int item_word(int w) { return w >= IT_FLG && w < IT_FLG + 32; }

/* World flags that hold one player's state, not the world's: each game keeps its own (left out of EV_FLAGS both
   ways and out of the compared hash). The Command room's security monitor (3/12) shows the bio-experiment room
   through its camera by taking the player into that room (3/14), hidden, with ev_flg 218 set; the camera's zoom
   sets 65, 66 or 72 for the script there; back in 3/12 a block on 218 (with the room flag the monitor set clear)
   ends the view and clears 218. Shared, the partner's 3/12 ran that block as soon as 218 came: it cleared 218
   for both, and the player looking through the camera stood in the bio-experiment room in person, past its code
   door. */
static unsigned int own_bits(int w)
{
    return w == 2 ? 0x40000000u | 0x20000000u | 0x00800000u   /* ev_flg 65, 66, 72: what the camera zoomed in on */
         : w == 6 ? 0x20u : 0;                                /* ev_flg 218: looking through the camera */
}

/* The host's game rules of protocol 19 (Pose.opts) as this session has them: the host's settings; the guest
   follows the host's last pose (OPT_IMPORTANT, OPT_SKIP_SYNC) */
static int rule(int opt)
{
    if (is_host) return opt == OPT_IMPORTANT ? g_coop.important : opt == OPT_SKIP_SYNC ? g_coop.skip_sync : opt == OPT_GAMEOVER_BOTH ? g_coop.gameover_both :
                        opt == OPT_SKIP_DETECTOR ? g_coop.skip_detector : opt == OPT_QUICK_START && g_coop.quick_start && !biorand_next();
    return peer_opts >= 0 && (peer_opts & opt);
}

/* the host's rule "Metal detector: Skipped" (OPT_SKIP_DETECTOR, protocol 22): pc/glue/detector_glue.c in a session, the
   guest's lobby (the host's value) */
int coop_skip_detector(void) { return rule(OPT_SKIP_DETECTOR); }
int coop_quick_start(void) { return rule(OPT_QUICK_START); }   /* "Quick start" (protocol 23): the guest's lobby */

/* event.c bhPlItemCheck, ps2_McSaveFile.c mcWriteStartSaveFile: typewriters save without an ink ribbon and use none.
   The setting (single player, and the host); a guest plays by the host's rule once it knows it. */
int pc_cfg_no_ink_ribbon(void);         /* host.c: the GAMEPLAY setting */
int pc_no_ink_ribbon(void)
{
    if (coop_on && !is_host && !echo_delay && peer_opts >= 0) return (peer_opts & OPT_NO_RIBBON) != 0;
    return pc_cfg_no_ink_ribbon();
}

/* Shared loot, "important items for both" (OPT_IMPORTANT, docs/COOP.md "Loot modes"): the item kinds each player
   takes for itself, as individual loot does for every item - one taking it leaves the partner's copy lying. Without
   the rule they are pool items as any other (for_both); key items go to both in both modes (story progress, a
   used-up key goes for both); files and maps are progress flags. */
static const struct { const char* kind; unsigned int type_bits; int id; } important_kinds[] = {
    { "weapons (guns, launchers, bow gun, knife)", 0x1, 0 },   /* itemdata[].type 0x1: the game's weapon items */
    { "ink ribbons",                               0,   31 },
};
static int important_item(int id)
{
    unsigned int k;
    if (id <= 0 || id >= 156) return 0;
    for (k = 0; k < sizeof(important_kinds) / sizeof(important_kinds[0]); k++)
        if ((itemdata[id].type & important_kinds[k].type_bits) || id == important_kinds[k].id) return 1;
    return 0;
}
static int own_copies(void) { return !individual() && rule(OPT_IMPORTANT); }
#define LIGHTER 55                      /* the STANDARD slot's lighter: the story hands it out */
/* Shared loot is one pool (docs/COOP.md "Loot modes"): an item is the taker's alone, its spot is gone for both.
   Reaching the partner as well: key items (story progress) and the lighter; with "important items for both"
   weapons and ink ribbons (each player's own copy: taken from the floor by each, from an event given to both). */
static int for_both(int id) { return key_item(id) || id == LIGHTER || (own_copies() && important_item(id)); }
static unsigned int imp_spots(int w) { return sys->itm[IMP_SPOTS + ((w - IT_FLG) & 15)]; }   /* of an item word (it_flg or ic_flg) */

/* event.c bhItmCk: we took the item of trigger `etc`, its it_flg bit `bit` (0..511) */
void coop_item_taken(int bit, int etc)
{
    int id;
    if (!sys || !rom || !sys->itwp || etc >= rom->etc_n || rom->etcp[etc].prm0 >= ITWP_N || !own_copies()) return;
    id = pc_pickup_item(sys->itwp[rom->etcp[etc].prm0].type, sys->itwp[rom->etcp[etc].prm0].id);   /* (BioRand: the record's type) */
    if (!important_item(id == 130 ? 35 : id)) return;   /* (130 is 35, as GetItem) */
    sys->itm[IMP_SPOTS + (bit >> 5)] |= 0x80000000u >> (bit & 31);
    host_log("coop: took item %d (flag %d): important items for both, the partner's copy stays\n", id, bit);
}

/* guest, after the host's game state was loaded and our items put back (own_restore): the important item spots as
   we took them, the others as the host's game has them. `host`: the state's IMP_SPOTS (the host's, and those of our
   saved record with it) */
static void imp_restore(const unsigned int* host)
{
    int k, spots = 0, taken = 0;
    for (k = 0; k < 16; k++) {
        unsigned int m = sys->itm[IMP_SPOTS + k] | host[k], t = sys->itm[IMP_TAKEN + k] & m;
        spots += __builtin_popcount(m);
        taken += __builtin_popcount(t);
        sys->itm[IMP_SPOTS + k] = m;
        sys->itm[IMP_TAKEN + k] = t;
        sys->it_flg[k] = (sys->it_flg[k] & ~m) | t;
        sys->ic_flg[k] &= ~(m & ~t);   /* not examined either */
    }
    if (spots) host_log("coop: important items for both: %d spots known, %d of them taken by us\n", spots, taken);
}

/* the partner took item `item`: the game's own GetItem adds it like a pickup of ours would
   (stacking ammo); 0 when a full inventory has no room for it */
static int give_item(int item)
{
    static S_WORK st;
    unsigned int* inv = &sys->itm[sys->ply_id * 16];
    unsigned int sf = swork.statusflg, cb = sys->cb_flg;
    int sb = sys->sb_id, max = (sys->gm_flg & 0x8000000) ? 10 : 8, room = max, i, id;
    if (item <= 0 || item >= 156) return 0;
    for (i = 0; i < max; i++) {   /* the capacity check of GetItem's first step */
        id = (inv[i + 2] >> 16) & 0xFF;
        room -= (itemdata[id].type & 0x100) ? 2 : id != 0;
        if (id == item && (itemdata[item].type & 0x200) && (inv[i + 2] & 0xFFFF) < 999) room = max;
    }
    if (room < ((itemdata[item].type & 0x100) ? 2 : 1)) { host_log("coop: no room for the partner's item %d, it stays in the room\n", item); return 0; }
    memset(&st, 0, sizeof(st));
    st.pip = inv;
    st.testmode = 5;   /* no dialog step, only the add */
    swork.statusflg &= ~0x100;
    sys->cb_flg = (cb | 0x800) & ~(0x1000000 | 0x400 | 0x8000);
    sys->sb_id = item;
    {   /* the count of a floor pickup of ours (hitchk.c, BioRand) is not this item's: kept for our own pickup */
        extern unsigned char override_pickup_quantity;
        unsigned char q = override_pickup_quantity;
        override_pickup_quantity = 0;
        GetItem(&st);
        override_pickup_quantity = q;
    }
    swork.statusflg = sf;
    sys->cb_flg = cb;
    sys->sb_id = sb;
    host_log("coop: got the partner's item %d\n", item);
    return 1;
}

/* the partner used up key item `item`: ours goes the way bhPlItemLost (the event command
   that takes a used key) removes it */
static unsigned char keys_had[156];     /* key items in our inventory at the last in-game frame */

static void take_item(int item)
{
    unsigned int* inv = &sys->itm[sys->ply_id * 16];
    int i;
    for (i = 2; i < 12; i++)
        if (((inv[i] >> 16) & 0xFF) == (unsigned)item) {
            inv[i] = 0;
            if (inv[0] && i <= (int)inv[0]) inv[0] = inv[0] == (unsigned)i ? 0 : inv[0] - 1;
            for (; i < 12; i++) inv[i] = inv[i + 1];
            if (keys_had[item]) keys_had[item]--;   /* not our own use: no echo */
            host_log("coop: the partner used up item %d, ours is gone too\n", item);
            return;
        }
}

/* both, at the hand-off of a frame in game: changed flag words go to the partner (not the
   item words with individual loot); a newly set item flag is a pickup when an item object of
   the examined spot was hidden at about the same time; a key item that leaves our inventory
   around a progress flag change was used up */
static unsigned int tick, flag_at, hide_at, prog_at, lost_at;   /* in-game frames of the last such change */
static unsigned int it_new[16];         /* it_flg bits we set recently (the pickup's bit) */
static unsigned int left_behind[16];    /* it_flg bits of partner pickups we had no room for */
/* Shared loot, both take one pool item (docs/COOP.md "Loot modes"): exactly one keeps it.
   - The partner's flag came while our "Will you take it?" was open: our game adds the item and sets no flag
     (coop_item_late): ours goes back.
   - Both games set the flag (within a round trip): the host's pickup stands (host authority, as the flag words).
     Host: `raced` are the it_flg bits the guest set that mask_host_changes dropped (we had taken them since the
     guest's base), or that we had set but not sent yet; the guest's EV_PICKUP of such a spot goes back as undo.
     Guest: `mine` remembers our recent pickups by spot, so an undo takes exactly that item back out; the host's
     flag for a spot we had set but not sent yet means the same at once.
   What goes back leaves the inventory once we are in control (`back`, not inside a menu or the status screen). */
static unsigned int raced[16];
static struct { short bit; unsigned char item; } mine[8];
static unsigned int mine_n;
static struct { unsigned char item, why; } back[8];   /* why: BACK_FIRST, BACK_EVENT */
static int back_n;
enum { BACK_FIRST, BACK_EVENT };
static void give_back(int item, int why) { if (back_n < 8) { back[back_n].item = item; back[back_n++].why = why; } }
static uint64_t hid_prev;
static int hid_room = -1, hide_obj = -1, lost_item;

static int event_gave(int item);
static int inv_remove(unsigned int w);
static const char* item_name(unsigned int entry);
static void trade_msg(const char* fmt, unsigned int entry);

/* event.c bhItmCk: we said "yes" to an item whose spot flag the partner's pickup had set meanwhile (its EV_FLAGS
   came while our "Will you take it?" was open): the game added ours anyway and sets no flag, so no pickup of ours
   is seen. Shared loot: the partner was first, a pool item goes back out. Key items stay (the partner's EV_PICKUP
   gives one to both anyway), as does an event's item (its flag came from the partner's copy of the event). */
void coop_item_late(int bit)
{
    int item = sys ? (sys->sb_id == 130 ? 35 : sys->sb_id) : 0;
    if (item <= 0 || item >= 156 || individual() || for_both(item) || event_gave(item)) return;
    give_back(item, BACK_FIRST);
    host_log("coop: took item %d of flag %d, but the partner had taken it first: ours goes back\n", item, bit);
}

static void gate_hold(int w, unsigned int* set, unsigned int* clr);
static unsigned int latch_bits(int w);
static void used_mark(int w, unsigned int bits);
static void rb_mark(int w, unsigned int bits);
static void gate_ours(int w, unsigned int bits);
static void gate_release(void);
static void gate_drop(void);

static void diff_flags(void)
{
    unsigned int* f = flag_block();
    unsigned int* inv = &sys->itm[sys->ply_id * 16];
    unsigned char keys[156] = { 0 };
    Event e = { 0 };
    int w, i, ind = individual(), own = own_copies();
    e.type = EV_FLAGS;
    e.u.fl.base = in_last;
    tick++;
    if (own) for (w = 0; w < 16; w++) sys->itm[IMP_TAKEN + w] = sys->it_flg[w] & sys->itm[IMP_SPOTS + w];   /* goes with our items */
    if (tick - flag_at >= 30) memset(it_new, 0, sizeof(it_new));
    for (w = 0; w < NFLAG; w++) {
        unsigned int set = f[w] & ~last[w], clr = last[w] & ~f[w];
        if (!(set | clr)) continue;
        last[w] = f[w];
        gate_ours(w, set | clr);
        if (w >= IT_FLG && w < IT_FLG + 16 && set) { flag_at = tick; it_new[w - IT_FLG] |= set; }
        if (!item_word(w)) prog_at = tick;
        if (trace && w >= 64 && w < 96) fprintf(stderr, "coop: ed_flg word %d set %08x clr %08x (ours, room %d/%d)\n", w - 64, set, clr, sys->stg_no, sys->rom_no);
        if (!have_peer || (ind && item_word(w))) continue;
        if (own && item_word(w) && !((set | clr) & ~imp_spots(w))) continue;   /* only our own important item spots changed */
        if (own && item_word(w)) { set &= ~imp_spots(w); clr &= ~imp_spots(w); }
        if (!((set | clr) & ~own_bits(w))) continue;   /* only a player's own state changed */
        set &= ~own_bits(w); clr &= ~own_bits(w);
        e.u.fl.word[e.u.fl.n] = w; e.u.fl.set[e.u.fl.n] = set; e.u.fl.clr[e.u.fl.n] = clr;
        if (++e.u.fl.n == 6) { queue_event(&e); e.u.fl.n = 0; }
    }
    if (e.u.fl.n) queue_event(&e);

    /* a pickup (bhItmCk) sets the item flag and hides the item object of the examined spot
       (seen up to a few frames apart); an item flag set any other way is only a flag */
    if (sys->itwp) {
        uint64_t hid = 0, now;
        int room = coop_room_key(), k;
        for (k = 0; k < 64 && k < ITWP_N; k++)
            if (sys->itwp[k].stflg & 0x1000000) hid |= (uint64_t)1 << k;
        now = hid & ~hid_prev;
        if (room == hid_room && now) { hide_at = tick; hide_obj = __builtin_ctzll(now); }
        hid_prev = hid; hid_room = room;
    }
    if (flag_at && hide_at && tick - flag_at < 30 && tick - hide_at < 30 && have_peer &&
        sys->etc_idx < (unsigned)rom->etc_n && rom->etcp[sys->etc_idx].prm0 == hide_obj) {
        Event p = { 0 };
        int item = sys->sb_id == 130 ? 35 : sys->sb_id;   /* as GetItem */
        for (i = 0; i < 10 && item > 0 && item < 156 && ((inv[i + 2] >> 16) & 0xFF) != (unsigned)item; i++) ;
        p.type = EV_PICKUP;
        p.u.pk.item = i < 10 ? sys->sb_id : 0;   /* 0: not an inventory item (a file) */
        p.u.pk.etc = sys->etc_idx;
        p.u.pk.etc_on = rom->etcp[sys->etc_idx].flg & 0x1;
        p.u.pk.bit = -1;
        for (w = 0; w < 16; w++)
            if (it_new[w]) { p.u.pk.bit = w * 32 + __builtin_clz(it_new[w]); break; }
        if (!is_host && !ind && p.u.pk.bit >= 0 && item > 0 && item < 156 && i < 10 && !for_both(item) && !event_gave(item)) {   /* the host may have been first */
            mine[mine_n & 7].bit = p.u.pk.bit;
            mine[mine_n++ & 7].item = item;
        }
        if (p.u.pk.bit >= 0 && (left_behind[p.u.pk.bit >> 5] & (0x80000000u >> (p.u.pk.bit & 31)))) {
            left_behind[p.u.pk.bit >> 5] &= ~(0x80000000u >> (p.u.pk.bit & 31));   /* the partner has its copy */
            host_log("coop: took item %d (etc %d), the one the partner left for us\n", p.u.pk.item, sys->etc_idx);
        } else if (ind && !key_item(p.u.pk.item)) {
            host_log("coop: took item %d (etc %d), individual loot\n", p.u.pk.item, sys->etc_idx);
        } else if (own && (p.u.pk.bit >= 0 ? (sys->itm[IMP_SPOTS + (p.u.pk.bit >> 5)] << (p.u.pk.bit & 31)) >> 31 : important_item(p.u.pk.item))) {
            host_log("coop: took item %d (etc %d), our own copy (important items for both)\n", p.u.pk.item, sys->etc_idx);
        } else {
            if (p.u.pk.item && event_gave(p.u.pk.item)) {   /* an event's item: it goes as EV_GRANT, to the floor */
                host_log("coop: item %d came from an event: the partner gets its spot only\n", p.u.pk.item);
                p.u.pk.item = 0;
            } else if (p.u.pk.item && !for_both(item)) {   /* shared loot: one pool, the item is ours alone */
                host_log("coop: item %d is ours alone (shared loot): the partner's spot goes\n", item);
                p.u.pk.item = 0;
            }
            queue_event(&p);
            host_log("coop: took item %d (etc %d, flag %d)\n", p.u.pk.item, sys->etc_idx, p.u.pk.bit);
        }
        flag_at = hide_at = 0;
        memset(it_new, 0, sizeof(it_new));
    }

    /* a used key: the event that opens the lock takes it (bhPlItemLost) and sets progress flags */
    for (i = 2; i < 12; i++) {
        int id = (inv[i] >> 16) & 0xFF;
        if (key_item(id) && keys[id] < 255) keys[id]++;
    }
    for (i = 0; i < 156; i++)
        if (keys[i] < keys_had[i]) { lost_item = i; lost_at = tick; }
    memcpy(keys_had, keys, sizeof(keys));
    if (lost_at && prog_at && tick - lost_at < 30 && tick - prog_at < 30 && have_peer) {
        Event l = { 0 };
        l.type = EV_LOST;
        l.u.lost.item = lost_item;
        queue_event(&l);
        host_log("coop: used up item %d\n", lost_item);
        lost_at = 0;
    }
}

/* host: drop the parts of a client change that the client made without having seen ours */
static void mask_host_changes(Event* c)
{
    unsigned int id = c->u.fl.base + 1;
    int i, j;
    if ((int)(ring.next - id) > COOP_RING) id = ring.next - COOP_RING;
    for (; (int)(id - ring.next) < 0; id++) {   /* also those acknowledged since: the ring keeps them until overwritten */
        const Event* h = &ring.ev[id % COOP_RING];
        if (h->id != id || h->type != EV_FLAGS) continue;
        for (i = 0; i < h->u.fl.n; i++)
            for (j = 0; j < c->u.fl.n; j++)
                if (h->u.fl.word[i] == c->u.fl.word[j]) {
                    c->u.fl.set[j] &= ~(h->u.fl.set[i] | h->u.fl.clr[i]);
                    c->u.fl.clr[j] &= ~(h->u.fl.set[i] | h->u.fl.clr[i]);
                }
    }
}

static const char* bye_reason;          /* the session ends (the partner said goodbye, or why we leave): the message, handled in coop_frame */
static int bye_code;                    /* our goodbye's reason: COOP_REJ_BIORAND (biorand_from_host), COOP_REJ_STATE (the host's game state) */
/* returns 0 when the event cannot be applied yet (the stream waits for it) */
static int apply_session_event(const Event* e)
{
    int live = is_host ? in_session_game() && !blob_due : !need_blob;
    if (e->type == EV_BLOB) {   /* the chunks of one transfer into blob_in; complete and valid: it is the game state */
        if (is_host || e->u.blob.total != SAVE_SIZE) return 1;
        if (coop_xfer_chunk(&blob_x, e->u.blob.tid, e->u.blob.total, e->u.blob.off, e->u.blob.len, e->u.blob.data) == 1) {
            const char* bad = blob_valid(blob_in);
            if (bad) {   /* the host would only send the same again: the session ends, and both sides say why */
                static char why[160];
                snprintf(why, sizeof(why), tr("Cannot load the host's game (%s). Disconnected."), tr(bad));
                host_log("coop: the host's game state is not valid here: %s\n", bad);
                coop_xfer_reset(&blob_x);
                if (!bye_reason) { bye_reason = why; bye_code = COOP_REJ_STATE; }
                return 1;
            }
            memcpy(blob, blob_in, SAVE_SIZE);
            blob_rec = e->u.blob.pad;   /* its items are our saved ones (2: as at the host's continue point) */
            own_tid = e->u.blob.tid;
            coop_xfer_reset(&blob_x);
            pop_pending = 1;
            need_blob = 0;
        }
        return 1;
    }
    if (e->type == EV_OWN) { if (is_host) apply_own(e); return 1; }   /* the guest's items: whatever the game does */
    if (!live) {   /* before our game state matched the partner's: superseded by a blob */
        if (trace && e->type == EV_FLAGS) fprintf(stderr, "coop: the partner's flag change %u dropped (not live)\n", e->id);
        return 1;
    }
    if (e->type == EV_FLAGS) {
        Event c = *e;
        unsigned int* f = flag_block();
        int j;
        if (is_host) {
            if ((int)(c.u.fl.base - blob_end) < 0) {   /* taken before our last blob */
                if (trace) fprintf(stderr, "coop: the partner's flag change %u dropped (before our blob)\n", e->id);
                return 1;
            }
            mask_host_changes(&c);
            for (j = 0; j < c.u.fl.n && j < 6; j++)   /* item spots we took since the guest's base: both took them */
                if (c.u.fl.word[j] >= IT_FLG && c.u.fl.word[j] < IT_FLG + 16 && !individual())
                    raced[c.u.fl.word[j] - IT_FLG] |= e->u.fl.set[j] & ~c.u.fl.set[j];
        }
        for (j = 0; j < c.u.fl.n && j < 6; j++) {
            int w = c.u.fl.word[j];
            if (w >= NFLAG) continue;
            if (trace && w >= 64 && w < 96)
                fprintf(stderr, "coop: ed_flg word %d set %08x clr %08x (the partner's, as sent %08x %08x)\n", w - 64, c.u.fl.set[j], c.u.fl.clr[j], e->u.fl.set[j], e->u.fl.clr[j]);
            if (is_host && item_word(w) && individual()) continue;   /* individual loot: the guest's item words are its own */
            if (item_word(w) && own_copies()) { c.u.fl.set[j] &= ~imp_spots(w); c.u.fl.clr[j] &= ~imp_spots(w); }   /* ours (important items) */
            c.u.fl.set[j] &= ~own_bits(w); c.u.fl.clr[j] &= ~own_bits(w);   /* our own state (a camera view) */
            if (w >= IT_FLG && w < IT_FLG + 16 && !individual() && (c.u.fl.set[j] & f[w] & ~last[w])) {   /* we took that spot too, not sent yet */
                int item = sys->sb_id == 130 ? 35 : sys->sb_id;
                if (is_host) raced[w - IT_FLG] |= c.u.fl.set[j] & f[w] & ~last[w];
                else if (item > 0 && item < 156 && !for_both(item) && !event_gave(item)) {
                    give_back(item, BACK_FIRST);
                    host_log("coop: the host took item %d of flag word %d as we did: ours goes back\n", item, w);
                }
            }
            c.u.fl.set[j] &= ~latch_bits(w); c.u.fl.clr[j] &= ~latch_bits(w);   /* a loop of this room: ours */
            gate_hold(w, &c.u.fl.set[j], &c.u.fl.clr[j]);   /* a gate of the scene we are in: when it ends */
            rb_mark(w, f[w] ^ ((f[w] | c.u.fl.set[j]) & ~c.u.fl.clr[j]));
            f[w] = (f[w] | c.u.fl.set[j]) & ~c.u.fl.clr[j];
            last[w] = (last[w] | c.u.fl.set[j]) & ~c.u.fl.clr[j];
        }
        return 1;
    }
    if (e->type == EV_LOST) {
        if (!tx_in_game) return 0;
        if (key_item(e->u.lost.item)) take_item(e->u.lost.item);
    }
    if (e->type == EV_PICKUP) {
        unsigned int* f = flag_block();
        int b = e->u.pk.bit, w = IT_FLG + (b >> 5), i;
        unsigned int m = 0x80000000u >> (b & 31);
        if (!tx_in_game) return 0;   /* menus, loading: later */
        if (e->u.pk.undo) {   /* from the host: it took spot b first, our pickup of it goes back */
            for (i = 0; i < 8; i++)
                if (mine[i].item && mine[i].bit == b) {
                    host_log("coop: the host took item %d (flag %d) as we did: ours goes back\n", mine[i].item, b);
                    give_back(mine[i].item, BACK_FIRST);
                    mine[i].item = 0;
                    return 1;
                }
            return 1;
        }
        if (is_host && b >= 0 && b < 512 && (raced[b >> 5] & m)) {   /* both took it: ours stands */
            Event u = { 0 };
            raced[b >> 5] &= ~m;
            if (e->u.pk.item) { host_log("coop: both took item %d (flag %d): each keeps its own\n", e->u.pk.item, b); return 1; }   /* for both anyway */
            u.type = EV_PICKUP; u.u.pk.undo = 1; u.u.pk.bit = b;
            queue_event(&u);
            host_log("coop: both took the spot of flag %d: ours stands, the partner's goes back\n", b);
            return 1;
        }
        if (is_host && individual() && e->u.pk.item && !key_item(e->u.pk.item)) {   /* individual loot: only key items are shared */
            host_log("coop: the partner's pickup of item %d ignored (individual loot)\n", e->u.pk.item);
            return 1;
        }
        if (e->u.pk.item && !give_item(e->u.pk.item)) {
            /* no room: the item stays in our room (flag cleared here only, so the partner
               keeps it set; our later pickup of it is not passed on) */
            if (b >= 0 && b < 512) { f[w] &= ~m; last[w] &= ~m; left_behind[b >> 5] |= m; }
            return 1;
        }
        if (b >= 0 && b < 512) {   /* the spot's flag (with individual loot only this one is synced) */
            f[w] |= m; last[w] |= m;
            f[w + 16] &= ~m; last[w + 16] &= ~m;   /* ic_flg: no longer "examined, not taken" (as bhItmCk) */
        }
        if (e->stg == sys->stg_no && e->rom == sys->rom_no && e->u.pk.etc < rom->etc_n) {
            ATR_WORK* a = &rom->etcp[e->u.pk.etc];
            if (sys->itwp && a->prm0 < ITWP_N) sys->itwp[a->prm0].stflg |= 0x1000000;   /* as bhItmCk */
            if (!e->u.pk.etc_on) a->flg &= ~0x1;
        }
    }
    return 1;
}

/* client: follow the host into its room through a door transition without the animation -
   on each room change of the host with "Follow host" on, and always on a regroup */
static void follow(void)
{
    static int view;   /* the host looks through the Command room's camera (own_bits): it is not in 3/14 in person */
    int room;
    if (!have_rx || !(rx.flags & PF_IN_GAME)) return;
    room = rx_room();
    if (rx.regroup != seen_regroup) { seen_regroup = rx.regroup; follow_pending = 2; }
    if (room != host_room) {   /* 3/12 to 3/14 directly: only the monitor's camera (3/14's door leads to 3/13) */
        view = (host_room & 0xFFFF) == (3 * 256 + 12) && (room & 0xFFFF) == (3 * 256 + 14);
        host_room = room;
        if (!follow_pending) follow_pending = 1;
        if (view) host_log("coop: the host looks through the camera of 3/12: not followed\n");
    }
    if (follow_pending == 1 && (!g_coop.follow || view)) follow_pending = 0;   /* "Follow host" off (also when switched off meanwhile) */
    if (!follow_pending || view) return;   /* a regroup waits until the host is back */
    if (partner_here()) { follow_pending = 0; return; }
    if (!tx_in_game || (sys->gm_flg & 0x9) || (sys->cb_flg & 0x1)) return;   /* as the door trigger */
    if (plp->mode0 == 4 || plp->mode0 == 6) return;   /* not out of a grab */
    if (!room_exists(rx.stg, rx.rom, -1) || rx.pos < 0 || rx.pos >= 60) {   /* a room this build does not have: stay */
        host_log("coop: the host's room %d/%d entry %d does not exist here: not following\n", rx.stg, rx.rom, rx.pos);
        follow_pending = 0;
        return;
    }
    host_log("coop: %s the host to stage %d room %d\n", follow_pending == 2 ? "regroup: pulled to" : "following", rx.stg, rx.rom);
    cheat_room_warp(rx.stg, rx.rom, (unsigned char)rx.pos, rx.rcase - 1);   /* its room case (-1: not sent, ours) */
    follow_pending = 0;
}

/* host: a story cutscene ending, or the story switching the character (cng_pid), pulls the client
   into our room (also with "Follow host" off); the switch itself also restarts the client from our
   state. A story cutscene: cinema mode (cb_flg 4, CineSet) for STORY_CINE frames or more. Shorter
   cinemas are the room's mechanisms - a lever, a shutter, an item close-up, a camera pan as a room
   is entered (600 cinema events in the retail scripts, most of them such): they pull nobody. Before,
   every cinema pulled at its start and again at its end, so a host walking into a room with an
   entry pan pulled a client that had "Follow host" off. */
#define STORY_CINE 450                  /* game frames: 15 s */
static void regroup_check(void)
{
    static int cine_was, cng_was = -1, pid_was = -1, cine_view;
    static unsigned int cine_at;
    int cine = (sys->cb_flg & 0x4) != 0;   /* cinema: bhCineSet */
    if (sys->tk_flg & 0x20) { cng_was = pid_was = -1; cine_was = cine; cine_at = frame_no; }   /* a game (re)start sends a blob anyway */
    if (cine && !cine_was) { cine_at = frame_no; cine_view = 0; }
    if (cine && (flag_block()[6] & own_bits(6))) cine_view = 1;   /* looking through the 3/12 camera (own_bits) */
    if (!cine && cine_was) {
        if (cine_view) host_log("coop: the camera view ended after %u s: no regroup\n", (frame_no - cine_at) / 30);
        else if (frame_no - cine_at >= STORY_CINE) { regroups++; host_log("coop: regroup (a cutscene of %u s ended)\n", (frame_no - cine_at) / 30); }
        else host_log("coop: a cutscene of %u s ended: no regroup (shorter than a story scene)\n", (frame_no - cine_at) / 30);
    }
    if (cng_was >= 0 && sys->cng_pid != cng_was) { regroups++; host_log("coop: regroup (character switch %d)\n", sys->cng_pid); }
    if (pid_was >= 0 && sys->ply_id != pid_was) { blob_due = 1; host_log("coop: now playing character %d\n", sys->ply_id); }
    cine_was = cine; cng_was = sys->cng_pid; pid_was = sys->ply_id;
}

/* at the hand-off, after receive() */
static void session(void)
{
    int game = in_session_game();
    if ((sys->tk_flg & 0x28) || popping || pop_pending) {   /* the inventory is replaced: not a used key */
        memset(keys_had, 0, sizeof(keys_had));
        memset(left_behind, 0, sizeof(left_behind));
        memset(raced, 0, sizeof(raced));
        memset(mine, 0, sizeof(mine));
        back_n = 0;
        lost_at = prog_at = 0;
        gate_drop();   /* the flag block is replaced too */
        grant_forget();
    }
    if (is_host) {
        if (game) regroup_check();
        if (sys->tk_flg & 0x20) blob_due = 1;   /* a game (re)starts: new game, load, continue */
        if (have_rx && (rx.flags & PF_WANT_BLOB) && (int)(blob_end - ring.head) < 0) blob_due = 1;   /* none in flight */
        if (blob_due) {
            if (have_peer && have_rx && id_ok && game && tx_in_game && local_visible()) send_blob();   /* id_ok: who it is decides its items */
            return;
        }
    } else {
        if (popping && tx_in_game) popping = 0;
        if ((sys->tk_flg & 0x20) && own_live && !popping && !pop_pending) own_hold = 1;   /* our own CONTINUE restored our continue
            point's items, but the host's game goes on: we take back the items we died with (before, whichever came first) */
        if (tx_in_game && in_session_game() && !popping && !pop_pending && !own_fixed && !own_hold) { own_snapshot(); own_send(); }
        if ((sys->tk_flg & 0x28) && !popping && !pop_pending) need_blob = 1;   /* title, or a game (re)start of our own */
        if (br_offer == BR_ASK || br_offer == BR_GEN) pop_pending = 0;   /* generating the host's BioRand pack: no game yet (a
            state that came meanwhile would be old: asked for again once it is done) */
        /* load at the title or while we are in control (not from a menu, an event, a door) */
        if (pop_pending && ((sys->tk_flg & 0x8) || (tx_in_game && local_visible()) || (go_running() && rule(OPT_GAMEOVER_BOTH)))) load_blob();
        if (need_blob || pop_pending || popping) return;
        follow();
    }
    if (tx_in_game && game) { gate_release(); diff_flags(); }
}

/* "Game over: Both" (state above own_restore): the partner's game over starts ours once we are in control (not in
   a menu, a cutscene, a door; as the scripts' bhGameOverSet). The guest's screen stays before CONTINUE / NO: the host's
   answer counts for both - its CONTINUE's game state (EV_BLOB pad 2) is loaded straight from the game over, its NO ends
   the session at the title, and gameover_ended takes the guest there too. Edges, not levels: a guest still in its game
   over while the host's continued game runs again starts nothing there. */
void bhReturnTitle(void);
static void gameover_frame(void)
{
    int on = have_rx && (rx.flags & PF_GAME_OVER), both = rule(OPT_GAMEOVER_BOTH);
    if (go_running()) { go_was = 1; go_want = 0; tx.flags |= PF_GAME_OVER; }
    else if (go_was) { go_was = 0; go_cont = (sys->tk_flg & 0x20) != 0; go_held = 0; }   /* CONTINUE restarts the game, NO goes to the title */
    if (!have_peer) go_cont = 0;
    if (have_rx) {
        if (on && !go_seen && both) go_want = 1;   /* the partner's game over started */
        if (!on || !both) go_want = 0;            /* or it is over again (continued) */
        go_seen = on;
    }
    if (go_want && in_session_game() && tx_in_game && local_visible() && plp->hp >= 0 && (is_host || (!need_blob && !pop_pending && !popping))) {
        host_log("coop: game over for both: the partner's game is over, ours too\n");
        sys->ts_flg &= ~0x4000;
        *(int*)&sys->gov_md0 = 0;   /* gov_md0, gov_md1 */
        go_want = 0;
    }
    if (!is_host && both && go_running() && sys->gov_md0 == 2 && sys->gov_md1 == 1 && sys->gov_ct >= 48) {
        sys->gov_ct = 47;   /* gameover.c asks CONTINUE / NO at 50: the host's answer counts */
        if (!go_held) note("Game over for both: the host answers Retry for both of you");
        go_held = 1;
    }
}

/* guest, the session ends: a shared game over that waited for the host (or the host's game over we were to follow)
   goes to the title, as the host's NO did there (a host that left or was lost cannot answer either) */
static void gameover_ended(void)
{
    if (is_host || !rule(OPT_GAMEOVER_BOTH) || !(go_held || go_want || (rx.flags & PF_GAME_OVER)) || !(sys && (sys->tk_flg & 0x80))) return;
    host_log("coop: game over for both: the host's game ended, back to the title\n");
    bhReturnTitle();
}

/* the lobby: the host's "Game over" rule as this session has it */
int coop_host_gameover_both(void) { return rule(OPT_GAMEOVER_BOTH); }

/* ---------------------------------------------------------------- world events (M8) */
/* The room script changes the world in blocks of its per-frame part (scd1) that test a world
   flag (ev_flg, ky_flg, ed_flg, mp_flg: synced) and flip it in their then-part, directly or in
   an event they start: "IfelCk; Ck ev_flg 104 clear; player on floor 0; EvtOn 0..7; Set ev_flg
   104 set; Endif" - zombies rise, a lever moves, the metal detector releases. The flag makes
   such a block fire once per change. Its other conditions (on a floor or trigger, "Yes" chosen,
   an item used, an item flag set) hold only where the player did it, so before M8 the partner
   got the flag alone, and its own script then never ran the block: no cutscene, no spawns, no
   environment change until it re-entered the room (scd0 rebuilds the room from the flags).

   Each instance runs scd1 traced (coop_scd1, from bhControlEvent). When a block fires - its gate
   flag had the "before" value and the then-part executed the flip - the actions it executed (not
   its conditions or flow ops) go to the partner, which replays them after its own scd1 of the
   next frame, in the same room only, unless its gate flag has the "after" value already: then it
   fired the block itself (both players did it at the same time) or got it before. So each
   instance runs each block once, without an arbiter round trip or lockstep: the player who
   triggers it sees it at once, the partner one way trip later. A partner in another room gets
   the flags only (its scd0 builds the room from them when it enters). Item flags are no gates:
   pickups follow the loot mode (M3/M4). Blocks are found by a sweep of the room script at room
   entry (instruction lengths as pc/tools/rdx.py derives them). */
/* instruction lengths, tests and flow ops: pc/src/net/coop_proto.c (also used to check what a
   partner sends) */
#define insn_len coop_insn_len_raw
#define is_cond coop_op_cond
#define is_flow coop_op_flow

extern unsigned char* bhScePtr;
extern unsigned int* G_Sp;
extern int bhIfelFlg;
extern unsigned short* bhScd2Ptr;
extern unsigned int (*bhScenarioJmpT[256])();

static int world_off(int set) { return set == 1 ? 0 : set == 3 ? 64 : set == 2 ? 96 : set == 8 ? 112 : -1; }   /* in the flag block */
static int flag_val(const unsigned int* block, int set, int bit) { return (block[world_off(set) + (bit >> 5)] >> (31 - (bit & 31))) & 1; }
static int u16at(const unsigned char* p) { return p[0] | p[1] << 8; }

#define MAX_REG 64
#define TRIG_OPS COOP_TRIG_OPS
typedef struct Region {
    unsigned short start, then0, then1;     /* IfelCk; its then-part [then0, then1) */
    unsigned short bit;                     /* gate: flag set/bit and its value before */
    unsigned char set, val, nf;
    unsigned short flip[12];                /* Set / EvtOn ops in the then-part that flip the gate */
    unsigned int fired_at;
} Region;
static Region reg[MAX_REG];
static int nreg;
static unsigned int reg_sig;
static void* reg_evtp;
static unsigned short evflip[256][8];      /* world flags each event sets: set << 12 | mode << 10 | bit */
static AtrSet aset[128];                   /* the room records' type/attr/prm the script writes (EV_ATR takes only these) */
static int naset;
static unsigned char nevflip[256];
static unsigned char evdoor[256];          /* the event goes through a door (SetDoorCall), itself or by an event it starts */
static unsigned short evtop[256][8];       /* world flags each event sets outside its blocks (always): as evflip */
static unsigned char nevtop[256];
static struct { unsigned char set; unsigned short bit; } latch[8];   /* gates of this room's own loops (see analyze) */
static int nlatch;
static unsigned short trace_buf[4096];
static int ntrace;
static unsigned int pre_flags[NFLAG];
static struct { int on, n, held; unsigned int id, at; Event ev; unsigned short ops[TRIG_OPS]; } trig;   /* the partner's block, to replay (at: arrived) */
static unsigned short trig_ops[TRIG_OPS];
static unsigned int trig_done;

static unsigned int scd_sig(void) { return (unsigned int)(size_t)rom->evtp->scd1 ^ ((unsigned int)(size_t)rom->evtp->evd << 11) ^ ((unsigned int)sys->rcase << 28); }

static void gate_drop(void);
static void world_reset(void) { trig.on = 0; trig_done = 0; gate_drop(); }

typedef struct Blk { unsigned int ifel, then0, then1, end; int nc; unsigned char cset[8], cval[8]; unsigned short cbit[8]; } Blk;

/* as the interpreter runs it: IfelCk n jumps to o + 2 + n when a test fails - behind its Endif,
   or into the else part behind an ElseCk m at o + n, which ends at o + n + m */
static void open_block(const unsigned char* d, unsigned int o, Blk* b)
{
    unsigned int p = o + 2;
    b->ifel = o;
    b->then1 = o + 2 + d[o + 1];
    b->end = d[b->then1 - 2] == 2 ? b->then1 - 2 + d[b->then1 - 1] : b->then1;
    b->nc = 0;
    while (is_cond(d[p])) {
        if (d[p] == 4 && b->nc < 8) { b->cset[b->nc] = d[p + 1]; b->cbit[b->nc] = u16at(d + p + 2) & 0x3FF; b->cval[b->nc++] = !d[p + 5]; }
        p += insn_len(d + p);
    }
    b->then0 = p;
}

/* the instruction at o (Set, or EvtOn of an event that sets it) gives flag set/bit the value !val */
static int flips(const unsigned char* d, unsigned int o, int set, int bit, int val)
{
    int k;
    if (world_off(set) < 0) return 0;
    if (d[o] == 5) return d[o + 1] == set && (u16at(d + o + 2) & 0x3FF) == bit && (d[o + 5] == 2 || d[o + 5] == val);
    for (k = 0; k < nevflip[d[o + 3]]; k++) {
        unsigned int f = evflip[d[o + 3]][k];
        if ((int)(f >> 12) == set && (int)(f & 0x3FF) == bit && ((f >> 10 & 3) == 2 || (int)(f >> 10 & 3) == val)) return 1;
    }
    return 0;
}

/* the end of the code that starts at `off`: the next entry of the script table, at most 32 KB */
static unsigned int code_end(const unsigned int* tab, int n, unsigned int off)
{
    unsigned int e = off + 0x8000;
    int i;
    for (i = 0; i < n; i++)
        if (tab[i] > off && tab[i] < e) e = tab[i];
    return e;
}

static void add_region(const Blk* b, int c, unsigned int o)
{
    int i;
    Region* r = NULL;
    for (i = 0; i < nreg; i++)
        if (reg[i].start == b->ifel) r = &reg[i];
    if (!r) {
        if (nreg == MAX_REG || b->then1 > 0xFFFF) return;
        r = &reg[nreg++];
        memset(r, 0, sizeof(*r));
        r->start = b->ifel; r->then0 = b->then0; r->then1 = b->then1;
        r->set = b->cset[c]; r->bit = b->cbit[c]; r->val = b->cval[c];
        r->fired_at = (unsigned int)-1000;
    }
    for (i = 0; i < r->nf; i++)
        if (r->flip[i] == o) return;
    if (r->nf < 12) r->flip[r->nf++] = o;
}

/* ---- enemies a room script wakes when a player steps on a spot: both players count
   Some rooms keep enemies waiting, not hittable, until a player steps on a floor record or walks into a
   trigger. 0/8 entered by its first door: its zombies stand with CommonCtr 46 0 (not hittable), and scd1 has
   "IfelCk; Ck rm_flg 3 set; Ck player is at floor 6; LoadWork enemy 0; CommonCtr 46 1 (hittable);
   SubCtr 143 1 0 0 0 (to its chase, type 0); ...; FlrAtariSet 6 off; Set rm_flg 3 clear; Endif" (likewise
   0/14, 1/2, 1/3, 1/7, 2/3, 3/5, 3/7, 5/52, 5/61, 5/65, 5/67, 6/7). The test reads the player of the game that runs
   the script, and the host's game runs the enemies: a guest who walked there woke only its own puppets,
   which the host's state put back at once - the enemies stood there, shots went through them, until the
   host walked by. Such a spot - every scd1 block that tests it has no else part and does nothing but this: it
   loads enemy works and controls them, shows enemy records, starts events that do only that, sets room flags,
   turns its floor off (coop_proto.c coop_wake_scan) - counts the partner too, in both games: while the partner
   (in our room, in control, not hit or held) stands on it and our player stands on no such record, our scd1 runs
   with the floor or trigger test set as if ours stood there (cb_flg 0x200 / 0x100, flr_idx / etc_idx), restored
   right after. Both games run the
   block once, whoever steps there first (the guest's run on its puppets is put back by the next snapshot,
   and leaves its enemies' type and room flags as the host's). */
#define MAX_WAKE 16
static CoopWake wake[MAX_WAKE];
static int nwake;
static unsigned int wake_said;          /* the spots logged in this room (bit per entry) */
static void wake_scan(const unsigned char* d, const unsigned int* tab, int ntab, int nev)
{
    static uint32_t evs[256], eve[256];
    unsigned int end = code_end(tab, ntab, tab[1]);
    int i;
    for (i = 0; i < nev; i++) {
        evs[i] = tab[i + 2];
        eve[i] = code_end(tab, ntab, tab[i + 2]);
        if (eve[i] > end) end = eve[i];
    }
    nwake = coop_wake_scan(d, (int)end, (int)tab[1], (int)code_end(tab, ntab, tab[1]), evs, eve, nev, wake, MAX_WAKE);
    wake_said = 0;
    for (i = 0; i < nwake; i++) host_log("coop: room %d/%d %s %d wakes enemies: the partner stepping there counts too\n", sys->stg_no, sys->rom_no, wake[i].etc ? "trigger" : "floor", wake[i].ix);
}
/* before our scd1: the partner stands on a wake spot (as hitchk.c tests our player: the record on, its floor,
   inside it, and a floor's facing limits); 1 = the test set for this run, *keep: what to put back */
static int wake_partner(unsigned int* keep)
{
    int i, ang = (rx.ay + 8192) & 0xC000, flr = rx.flr - 3;
    if (!nwake || echo_delay || !have_rx || (rx.flags & (PF_IN_GAME | PF_VISIBLE)) != (PF_IN_GAME | PF_VISIBLE) || rx.busy || !rx.flr ||
        (rx.pflg & 6) || !partner_here() || (sys->cb_flg & 0x5) || rom->evtp != reg_evtp || scd_sig() != reg_sig) return 0;
    for (i = 0; i < nwake; i++) {
        int ix = wake[i].ix, n = wake[i].etc ? rom->etc_n : rom->flr_n;
        ATR_WORK* a = ix < n ? (wake[i].etc ? &rom->etcp[ix] : &rom->flrp[ix]) : ix < n + (wake[i].etc ? sys->metc_n : sys->mflr_n) ?
                      (wake[i].etc ? &sys->metcp[ix - n] : &sys->mflrp[ix - n]) : NULL;
        if (!a || !(a->flg & 0x1) || a->flr_no != flr || (sys->cb_flg & (wake[i].etc ? 0x100 : 0x200)) ||
            rx.px < a->px || rx.px > a->px + a->w || rx.pz < a->pz || rx.pz > a->pz + a->d) continue;
        if (!wake[i].etc && (a->type != 0 || (!(a->attr & 0x1) && (rx.py < a->py || rx.py > a->py + a->h)) ||
                             (ang == 0x8000 && (a->attr & 0x400)) || (ang == 0x4000 && (a->attr & 0x800)) ||
                             (ang == 0 && (a->attr & 0x1000)) || (ang == 0xC000 && (a->attr & 0x2000)))) continue;
        keep[0] = sys->cb_flg & 0x300; keep[1] = sys->flr_idx; keep[2] = sys->etc_idx;
        sys->cb_flg |= wake[i].etc ? 0x100 : 0x200;
        if (wake[i].etc) sys->etc_idx = ix; else sys->flr_idx = ix;
        if (!(wake_said >> i & 1)) host_log("coop: the partner stands on %s %d of %d/%d: the enemies it wakes wake here too\n",
                                            wake[i].etc ? "trigger" : "floor", ix, sys->stg_no, sys->rom_no);
        wake_said |= 1u << i;
        return 1;
    }
    return 0;
}

/* at room entry: the world event blocks of scd1 */
static void analyze(void)
{
    const unsigned char* d = (const unsigned char*)rom->evtp;
    const unsigned int* tab = (const unsigned int*)rom->evtp;
    unsigned int first = tab[0], o, lim;
    int n, k, ntab, nev, ns = 0, i, j;
    Blk st[32];
    nreg = naset = 0;
    reg_evtp = rom->evtp;
    reg_sig = scd_sig();
    for (k = 0; k * 4 < (int)first && k < 1024; k++)
        if (tab[k] && tab[k] < first) first = tab[k];
    ntab = first / 4;
    nev = ntab > 2 ? ntab - 2 : 0;
    if (nev > 256) nev = 256;
    for (k = 0; k < nev; k++) {   /* the world flags each event sets */
        int ne = 0;
        unsigned int ends[32];
        nevflip[k] = nevtop[k] = 0;
        evdoor[k] = 0;
        o = tab[k + 2];
        lim = code_end(tab, ntab, o);
        while (o < lim && (n = insn_len(d + o)) > 0) {
            while (ne && ends[ne - 1] <= o) ne--;
            if (d[o] == 1 && ne < 32) { Blk b; open_block(d, o, &b); ends[ne++] = b.end; }
            if (d[o] == 51) evdoor[k] = 1;
            if (naset < 128 && coop_atr_set(d + o, &aset[naset])) naset++;
            if (d[o] == 5 && world_off(d[o + 1]) >= 0 && d[o + 5] <= 2 && nevflip[k] < 8)
                evflip[k][nevflip[k]++] = d[o + 1] << 12 | d[o + 5] << 10 | (u16at(d + o + 2) & 0x3FF);
            if (d[o] == 5 && !ne && world_off(d[o + 1]) >= 0 && d[o + 5] <= 1 && nevtop[k] < 8)
                evtop[k][nevtop[k]++] = d[o + 1] << 12 | d[o + 5] << 10 | (u16at(d + o + 2) & 0x3FF);
            o += n;
            if (!ne && (d[o - n] == 0 || d[o - n] == 255)) break;
        }
    }
    for (; k < 256; k++) nevflip[k] = nevtop[k] = evdoor[k] = 0;
    for (j = 0; j < 3; j++)   /* events started by door events' starters: three levels deep */
        for (k = 0; k < nev; k++) {
            o = tab[k + 2];
            lim = code_end(tab, ntab, o);
            for (; !evdoor[k] && o < lim && (n = insn_len(d + o)) > 0; o += n) {
                if (d[o] == 20 && evdoor[d[o + 3]]) evdoor[k] = 1;
                if (d[o] == 255 || d[o] == 0) break;
            }
        }
    o = tab[1];
    lim = code_end(tab, ntab, o);
    while (o < lim && (n = insn_len(d + o)) > 0) {
        while (ns && st[ns - 1].end <= o) ns--;
        if (naset < 128 && coop_atr_set(d + o, &aset[naset])) naset++;
        if (d[o] == 1 && ns < 32) open_block(d, o, &st[ns++]);
        else if (d[o] == 5 || d[o] == 20)
            for (i = 0; i < ns; i++)
                if (o >= st[i].then0 && o < st[i].then1)
                    for (j = 0; j < st[i].nc; j++)
                        if (flips(d, o, st[i].cset[j], st[i].cbit[j], st[i].cval[j])) add_region(&st[i], j, o);
        o += n;
        if (!ns && d[o - n] == 0) break;
    }
    /* A block whose own event sets its gate back is a loop of this room, not a world event: 4/1 case 1 (the
       airport hangar) starts its ambient motion (objects 5 and 14) by ev_flg 249 clear -> set, and the motion's
       event clears it again at its end, every 7 s. As a world event each game sent it every round, so a
       world event window was always open: the room was never rebuilt from the partner's flags (the bridge
       the partner lowered stayed up here), and each round's records went to the partner - the bridge raised
       meanwhile included, placed back where it was a moment before (raised, it stayed down there, but could
       be crossed). Its gate (only at the then-part's top level: the event always runs, and always resets it)
       is no world flag while we are in the room: each game runs its own loop (apply_flags leaves it alone). */
    nlatch = 0;
    for (i = 0; i < nreg; i++) {
        Region* r = &reg[i];
        int loop = 0;
        for (o = r->then0; o < r->then1 && !loop && (n = insn_len(d + o)) > 0; o += n) {
            if (d[o] == 1) { Blk b; open_block(d, o, &b); n = (int)(b.end - o); continue; }
            if (d[o] == 20)
                for (k = 0; k < nevtop[d[o + 3]]; k++) {
                    unsigned int f = evtop[d[o + 3]][k];
                    if ((int)(f >> 12) == r->set && (int)(f & 0x3FF) == r->bit && (int)(f >> 10 & 3) == !r->val) loop = 1;
                }
        }
        if (!loop) continue;
        host_log("coop: block %04x (gate flag %d/%d) is a loop of this room: its own event sets the gate back, no world event\n", r->start, r->set, r->bit);
        if (nlatch < 8) { latch[nlatch].set = r->set; latch[nlatch++].bit = r->bit; }
        reg[i--] = reg[--nreg];
    }
    if (trace)
        for (i = 0; i < nreg; i++)
            fprintf(stderr, "coop: world block %04x..%04x gate set %d bit %d %s, %d flips\n", reg[i].start, reg[i].then1,
                    reg[i].set, reg[i].bit, reg[i].val ? "set" : "clear", reg[i].nf);
    host_log("coop: room %d/%d case %d: %d world event blocks\n", sys->stg_no, sys->rom_no, sys->rcase, nreg);
    wake_scan(d, tab, ntab, nev);
}

/* the gates of this room's own loops in flag word w */
static unsigned int latch_bits(int w)
{
    unsigned int m = 0;
    int i;
    if (rom->evtp != reg_evtp || scd_sig() != reg_sig) return 0;
    for (i = 0; i < nlatch; i++)
        if (world_off(latch[i].set) + (latch[i].bit >> 5) == w) m |= 0x80000000u >> (latch[i].bit & 31);
    return m;
}

static Region* region_at(unsigned int start)
{
    int i;
    for (i = 0; i < nreg; i++)
        if (reg[i].start == start) return &reg[i];
    return NULL;
}

/* ---- a gate stays as our own scene left it until the scene ends
   A world event often takes the player through two steps of the room script: one step enters a scene
   (cinema mode, CineSet 3) and sets a room flag, and a later block, gated by the same world flag, ends it
   (its events, or the "No" branch's CineSet 4) - the turntable (7/11): the key's event asks "Move the
   turntable?" and the block that moves it, or not, tests ev_flg 350 clear; the guillotine gate (0/8):
   the Eagle Plate used in the item screen enters the scene, the block that opens the gate when the screen
   has closed tests ev_flg 11 clear; the lift corridor (7/8): the Halberd sets ev_flg 339 and enters the
   scene, the block that takes the lift once the screen has closed tests it set and clears it. In single
   player nothing changes that flag in between. In co-op the partner did: its EV_FLAGS (it moved the
   turntable while our question was open), or our replay of its block (its game ran the lift's block on our
   flag and sent it back: the replay cleared ev_flg 339 here, without the door), and the closing block no
   longer ran: our player stayed in cinema mode for good (frozen). So while our player is in a scene, the
   partner's changes to this room's world event gates wait (held) and are applied when the scene ends or
   the room changes (our own change of a held bit is newer and drops it), and so does the replay of its
   blocks (coop_scd1; the stream waits behind it as for any replay, SCENE_WAIT at most). */
#define SCENE_WAIT 300                  /* frames: a replay waits for our scene at most 10 s (the stream waits with it) */
static unsigned int held_set[NFLAG], held_clr[NFLAG];
static int held_key = -1;               /* the room they were held in */
static int in_scene(void) { return (sys->cb_flg & 0x4) != 0; }   /* cinema: bhCineSet */
/* the gate bits of this room's world event blocks in flag word w (none before the room's script was swept) */
static unsigned int gate_bits(int w)
{
    unsigned int m = 0;
    int i;
    if (rom->evtp != reg_evtp || scd_sig() != reg_sig) return 0;
    for (i = 0; i < nreg; i++)
        if (world_off(reg[i].set) + (reg[i].bit >> 5) == w) m |= 0x80000000u >> (reg[i].bit & 31);
    return m;
}
/* EV_FLAGS: keep the partner's gate changes out of *set / *clr while our scene runs */
static void gate_hold(int w, unsigned int* set, unsigned int* clr)
{
    unsigned int g;
    if (!in_scene() || !(g = gate_bits(w) & (*set | *clr))) return;
    held_set[w] = (held_set[w] | (*set & g)) & ~(*clr & g);
    held_clr[w] = (held_clr[w] | (*clr & g)) & ~(*set & g);
    *set &= ~g; *clr &= ~g;
    held_key = coop_room_key();
    host_log("coop: the partner changed world flag word %d (%08x) while our scene runs in %d/%d: held until it ends\n", w, g, sys->stg_no, sys->rom_no);
}
static void gate_drop(void) { memset(held_set, 0, sizeof(held_set)); memset(held_clr, 0, sizeof(held_clr)); held_key = -1; }
static void gate_ours(int w, unsigned int bits) { held_set[w] &= ~bits; held_clr[w] &= ~bits; }   /* diff_flags: we changed them */
/* every frame in game: the held changes once our scene has ended (or we left the room) */
static void gate_release(void)
{
    unsigned int* f = flag_block();
    int w, n = 0;
    if (held_key < 0 || (in_scene() && held_key == coop_room_key())) return;
    for (w = 0; w < NFLAG; w++) {
        if (!(held_set[w] | held_clr[w])) continue;
        rb_mark(w, f[w] ^ ((f[w] | held_set[w]) & ~held_clr[w]));
        f[w] = (f[w] | held_set[w]) & ~held_clr[w];
        last[w] = (last[w] | held_set[w]) & ~held_clr[w];
        n++;
    }
    if (n) host_log("coop: our scene ended: the partner's held world flags (%d words) applied\n", n);
    gate_drop();
}

static void count_keys(unsigned char* keys);

/* ---- the room as the world event left it (shared world changes end the same on both sides)
   A replayed block starts the same events on the partner, but they run on the partner's own state:
   per-room and per-frame flags it never got, item flags of individual loot, flags still on their way,
   its own events in the same task levels, a partner that is busy meanwhile. Their visible part then
   matches while what they did to the room's collision may not (a shutter that rolls up but stays
   solid). So the side whose block fired keeps a window open while the events it started run; when it
   closes, every room record (walls, triggers, floors) and object placement that differs from before
   the block goes to the partner (EV_ATR), which takes them over - at once, and again when its own
   replayed events are done. Outside such windows records are not synced: the room's scripts set many
   of them from the player's own entry door and position. */
#define OBJ_N 32                        /* sys->obwp: 39936 bytes of O_WRK (bhFirstGameStart) */
static ATR_WORK atr_known[3][256];      /* the room's records as the partner has them (before the block) */
static AtrRec obj_known[OBJ_N];
static int atr_key = -1;                /* the room (and case) they belong to */
typedef struct Win { int open; unsigned int at; unsigned short levels; } Win;
static Win win_tx, win_rx;              /* our block's events / the partner's events, replayed */
static struct { unsigned char kind, ix; AtrRec r; } atr_pend[64];   /* the partner's records, applied again when win_rx closes */
static int atr_pend_n;

static ATR_WORK* atr_table(int k, int* n)
{
    *n = k == ATR_WAL ? rom->wal_n : k == ATR_ETC ? rom->etc_n : rom->flr_n;
    if (*n > 256) *n = 256;
    if (*n < 0) *n = 0;
    return k == ATR_WAL ? rom->walp : k == ATR_ETC ? rom->etcp : rom->flrp;
}
static int obj_count(void) { return sys->obwp ? (rom->obj_n < OBJ_N ? rom->obj_n : OBJ_N) : 0; }
static void obj_get(int i, AtrRec* r)
{
    const O_WRK* o = &sys->obwp[i];
    memset(r, 0, sizeof(*r));
    r->p[0] = o->px; r->p[1] = o->py; r->p[2] = o->pz;
    r->p[3] = (float)o->ax; r->p[4] = (float)o->ay; r->p[5] = (float)o->az;
}
static void atr_snap(void)
{
    int k, n, i;
    for (k = 0; k < 3; k++) {
        const ATR_WORK* t = atr_table(k, &n);
        if (t) memcpy(atr_known[k], t, n * sizeof(ATR_WORK));
    }
    for (i = 4; i < obj_count(); i++) obj_get(i, &obj_known[i]);
}

/* task levels the EvtOns among `ops` start (Event_exec: a level >= 16 takes the first free one) */
static unsigned short ev_levels(const unsigned char* d, const unsigned short* ops, int n)
{
    unsigned short m = 0;
    int i;
    for (i = 0; i < n; i++)
        if (d[ops[i]] == 20) m |= d[ops[i] + 2] < 16 ? 1u << d[ops[i] + 2] : 0xFFFFu;
    return m;
}
static void win_open(Win* x, unsigned short levels)
{
    if (!x->open) x->levels = 0;
    x->open = 1;
    x->at = frame_no;
    x->levels |= levels;
}
/* a window closes when the events it waits for have ended (after 10 frames at least), at most after 60 s */
static int win_done(Win* x)
{
    int l;
    if (!x->open || frame_no - x->at < 10) return 0;
    if (frame_no - x->at < 1800)
        for (l = 0; l < 16; l++)
            if ((x->levels >> l & 1) && bhEtask[l].status) return 0;
    x->open = 0;
    return 1;
}

static void atr_put(int kind, int ix, const AtrRec* r)
{
    int n;
    if (kind == ATR_OBJ) {
        O_WRK* o;
        if (ix < 4 || ix >= obj_count()) return;
        o = &sys->obwp[ix];
        o->px = r->p[0]; o->py = r->p[1]; o->pz = r->p[2];
        o->ax = (int)r->p[3]; o->ay = (int)r->p[4]; o->az = (int)r->p[5];
        obj_known[ix] = *r;
    } else {   /* flag and placement; type, attr, prm only as our script writes them (coop_atr_merge) */
        ATR_WORK* t = atr_table(kind, &n);
        AtrRec v;
        if (!t || ix >= n) return;
        memcpy(&v, &t[ix], sizeof(v));
        coop_atr_merge(&v, r, kind, ix, aset, rom->evtp == reg_evtp && scd_sig() == reg_sig ? naset : 0);
        if (trace && (v.type != r->type || v.attr != r->attr || memcmp(v.prm, r->prm, 4)))
            fprintf(stderr, "coop: the partner's record kind %d #%d: type %d attr %#x prm %d %d %d %d is none our script writes, ours kept\n",
                    kind, ix, r->type, r->attr, r->prm[0], r->prm[1], r->prm[2], r->prm[3]);
        memcpy(&t[ix], &v, sizeof(v));
        atr_known[kind][ix] = t[ix];
    }
}

/* ---- the room as the partner's flags have it
   The room's entry script (scd0) builds the room from the world flags: which triggers ask what (a puzzle's
   prompt, "It's locked"), which are doors, which walls stand. A partner in another room gets the flags, and
   its entry script builds the room right when it gets there. A partner in the room keeps the room as it
   was, unless the change came as a world event it replays. A puzzle solved by steps of the other player's
   own game left its trigger asking: the Gold Luger room's switch panel (1/05; the answer starts an event
   from a block gated by room flags only) opened again and waited for an answer that its blocks, gated by
   the solved flag, no longer take (stuck in the panel, no text); the main hall's terminal (1/02) sets its
   flag, a room-flag block then makes the door, so the partner's door stayed locked. So when the partner
   changes world flags (ev_flg, ky_flg) while we are in the room, the entry script's record writes (walls,
   triggers, floors) in the blocks that test them run again, as entering would run them, once our scene
   and any world event window are over. So do the placements of the room's objects there (an object's
   LoadWork, then its position and angles: CommonCtr 7 8 9 11 12 13 47) and effects shown or hidden
   (EffDispSet): a card door's shutter (7/20, 7/21, 3/15..3/17: the card's scene is the user's own, not a world
   event) let the partner through but still looked closed until the room was entered again. Nothing else of
   the entry script runs (enemies, items, sounds, lights, room flags), and a block with another test than
   Ck / CmpB / CmpW is left alone. */
static unsigned int rb_bits[NFLAG];     /* world flags the partner changed in this room, not rebuilt yet */
static int rb_key = -1;
static void rb_mark(int w, unsigned int bits)
{
    int key = coop_room_key() * 16 + sys->rcase;
    if (!bits || !(w < 32 || (w >= 96 && w < 112))) return;   /* ev_flg, ky_flg */
    used_mark(w, bits);
    if (key != rb_key) { memset(rb_bits, 0, sizeof(rb_bits)); rb_key = key; }
    rb_bits[w] |= bits;
}
static int rb_is_rec(int op) { return op == 10 || op == 11 || op == 12 || op == 37 || op == 123 || op == 124; }   /* (Wal|Etc|Flr)AtariSet(2) */
static BH_SCEWORK rb_task;              /* the walk's own task: its LoadWork must not retarget a running event's */
static int rb_obj;                      /* rb_task's work is an object */
/* an entry script write of what the room shows: an object's placement (behind its LoadWork), an effect on or off */
static int rb_is_show(const unsigned char* p)
{
    if (p[0] == 105) return rb_obj && (p[1] == 7 || p[1] == 8 || p[1] == 9 || p[1] == 11 || p[1] == 12 || p[1] == 13 || p[1] == 47);
    return p[0] == 67;   /* EffDispSet */
}
/* the entry script's code in [o, end) as the interpreter takes it; `hit`: inside a block that tests a changed
   flag. Returns the writes run. */
static int rb_walk(const unsigned char* d, unsigned int o, unsigned int end, int hit, int depth)
{
    int n, runs = 0;
    while (o < end && (n = insn_len(d + o)) > 0 && d[o] != 0) {
        if (d[o] == 1) {   /* IfelCk: its tests, then the part they choose */
            Blk b;
            unsigned int p;
            int ok = 1, known = 1, h = hit, m;
            open_block(d, o, &b);
            if (b.end <= o || b.end > end) return runs;
            for (p = o + 2; p < b.then0; p += m) {
                int op = d[p], set = d[p + 1], bit = u16at(d + p + 2) & 0x3FF;
                m = insn_len(d + p);
                if (m <= 0 || (op != 4 && op != 6 && op != 7)) { known = 0; break; }
                if (op == 4 && world_off(set) >= 0 && (rb_bits[world_off(set) + (bit >> 5)] << (bit & 31) >> 31)) h = 1;
                if (ok) { bhScePtr = (unsigned char*)d + p; ok = bhScenarioJmpT[op]() != 0; }
            }
            if (known && depth < 16)
                runs += ok ? rb_walk(d, b.then0, b.end != b.then1 ? b.then1 - 2 : b.then1, h, depth + 1) : rb_walk(d, b.then1, b.end, h, depth + 1);
            o = b.end;
            continue;
        }
        if (d[o] == 101 && (rb_obj = d[o + 1] == 2)) {   /* LoadWork of an object: only points rb_task at it */
            bhScePtr = (unsigned char*)d + o;
            bhScenarioJmpT[101]();
        }
        if (d[o] == 104 || d[o] == 181) rb_obj = 0;   /* LoadWork2, LoadWorkEx: another work, not run here */
        if (hit && (rb_is_rec(d[o]) || rb_is_show(d + o))) {
            if (trace) fprintf(stderr, "coop: entry script write at %04x: op %d %d\n", o, d[o], d[o + 1]);
            bhScePtr = (unsigned char*)d + o;
            bhScenarioJmpT[d[o]]();
            runs++;
        }
        o += n;
    }
    return runs;
}
static void rebuild(int key)
{
    const unsigned char* d = (const unsigned char*)rom->evtp;
    const unsigned int* tab = (const unsigned int*)rom->evtp;
    unsigned int first, k;
    int w, any = 0, runs;
    if (key != rb_key) { memset(rb_bits, 0, sizeof(rb_bits)); rb_key = key; return; }   /* entering built it */
    for (w = 0; w < NFLAG && !any; w++) any = rb_bits[w] != 0;
    if (!any || !d || in_scene() || win_rx.open || win_tx.open) return;
    for (first = tab[0], k = 0; k * 4 < first && k < 1024; k++)
        if (tab[k] && tab[k] < first) first = tab[k];
    {
        BH_SCEWORK* cur = bhCetask;
        memset(&rb_task, 0, sizeof(rb_task));
        bhCetask = &rb_task;
        rb_obj = 0;
        runs = rb_walk(d, tab[0], code_end(tab, first / 4, tab[0]), 0, 0);
        bhCetask = cur;
    }
    memset(rb_bits, 0, sizeof(rb_bits));
    if (runs) host_log("coop: the partner changed world flags of %d/%d: %d writes of its entry script run again\n", sys->stg_no, sys->rom_no, runs);
}

/* ---- an entry script that would undo the partner's solution
   Some entry scripts clear a world flag that another room still needs. 9/31's (the self-destruct terminal)
   clears ev_flg 5 on every entry: its code sets it (sp_evt.c, VERONICA), and 9/30 starts the launch when it is
   entered with the flag set. A single player goes from the terminal straight to 9/30; in co-op a partner who
   (re-)entered 9/31 after the solution cleared it for both, and 9/30 never launched (the card is used up: no
   second try). In co-op such a clear of a flag that was set before the entry script ran is undone at once (it
   never reaches the partner). The other entry-script clears of flags other rooms test re-arm transient state
   (the lifts' floor, the palace piano's music, ambient voices, a room's entry cutscene) and are left alone. */
static const struct { unsigned char stg, rom, set; unsigned short bit; } keep_solved[] = {
    { 9, 31, 1, 5 },   /* ev_flg 5: the self-destruct code entered (9/30's launch) */
};
static unsigned int scd0_pre[NFLAG];
/* event.c bhInitEvent, around the entry script: 0 before, 1 after */
void coop_scd0(int after)
{
    unsigned int* f = flag_block();
    int k;
    if (!sys) return;
    if (!after) { memcpy(scd0_pre, f, sizeof(scd0_pre)); return; }
    for (k = 0; k < (int)(sizeof(keep_solved) / sizeof(keep_solved[0])); k++) {
        int w = world_off(keep_solved[k].set) + (keep_solved[k].bit >> 5);
        unsigned int m = 0x80000000u >> (keep_solved[k].bit & 31);
        if (sys->stg_no != keep_solved[k].stg || sys->rom_no != keep_solved[k].rom || !(scd0_pre[w] & m) || (f[w] & m)) continue;
        f[w] |= m;
        host_log("coop: %d/%d's entry script cleared flag %d/%d, set by a solution: kept\n", sys->stg_no, sys->rom_no, keep_solved[k].set, keep_solved[k].bit);
    }
}

/* at the hand-off of a frame in game */
static void world_sync(void)
{
    int key = coop_room_key() * 16 + sys->rcase, k, n, i, sending = 0, sent = 0;
    Event e = { 0 };
    rebuild(key);
    if (key != atr_key) {   /* another room: nothing open carries over */
        atr_key = key;
        win_tx.open = win_rx.open = 0;
        atr_pend_n = 0;
        atr_snap();
        return;
    }
    {   /* test aid RECVX_COOP_TEST_KILL="frames:levelmask": that many frames into a replay, end the replayed
           events of those task levels (as if something here took their levels): what they would have
           done to the room then comes only from the partner's records */
        static const char* env = (const char*)-1;
        unsigned int f, m, l;
        if (env == (const char*)-1) env = getenv("RECVX_COOP_TEST_KILL");
        if (env && win_rx.open && sscanf(env, "%u:%u", &f, &m) == 2 && frame_no - win_rx.at == f)
            for (l = 0; l < 16; l++)
                if ((m >> l & 1) && bhEtask[l].status) { bhEtask[l].status = 0; host_log("coop: test: the replayed event in task level %u ended early\n", l); }
    }
    if (win_done(&win_rx)) {   /* our replay of the partner's events ended: its records again */
        for (i = 0; i < atr_pend_n; i++) atr_put(atr_pend[i].kind, atr_pend[i].ix, &atr_pend[i].r);
        if (atr_pend_n) host_log("coop: the partner's world event ended here too: its %d room records again\n", atr_pend_n);
        atr_pend_n = 0;
    }
    if (win_tx.open) {   /* our block's events run: `known` stays as before it; when they end, the difference goes */
        if (!win_done(&win_tx)) return;
        sending = have_peer && !echo_delay && same_room();
    }
    e.type = EV_ATR;
    e.idx = sys->rcase;
    e.u.at.sig = reg_sig;
    for (k = 0; k <= ATR_OBJ; k++) {
        const ATR_WORK* t = NULL;
        if (k == ATR_OBJ) n = obj_count();
        else t = atr_table(k, &n);
        e.u.at.kind = k;
        for (i = k == ATR_OBJ ? 4 : 0; i < n; i++) {
            AtrRec r;
            if (k == ATR_OBJ) {
                obj_get(i, &r);
                if (!memcmp(&r, &obj_known[i], sizeof(r))) continue;
                obj_known[i] = r;
            } else {
                if (!t || !memcmp(&t[i], &atr_known[k][i], sizeof(ATR_WORK))) continue;
                atr_known[k][i] = t[i];
                memcpy(&r, &t[i], sizeof(r));
            }
            if (!sending) continue;   /* only what our block's events changed */
            e.u.at.ix[e.u.at.n] = i;
            e.u.at.r[e.u.at.n] = r;
            sent++;
            if (++e.u.at.n == 2) { queue_event(&e); e.u.at.n = 0; }
        }
        if (e.u.at.n) { queue_event(&e); e.u.at.n = 0; }
    }
    if (sent) host_log("coop: our world event ended: %d room records to the partner\n", sent);
}

/* EV_ATR: the partner's room records after its world event, in the same room and script */
static int apply_atr(const Event* e)
{
    int k;
    if (!sys || !rom || !in_session_game() || (sys->tk_flg & 0x28) || e->stg != sys->stg_no || e->rom != sys->rom_no ||
        e->idx != sys->rcase || e->u.at.sig != scd_sig())
        return 1;   /* another room: its flags build it when we get there */
    for (k = 0; k < e->u.at.n; k++) {
        int pend = win_rx.open && atr_pend_n < 64;
        /* an object our replay of its events still moves takes its place when they have ended: put now, the
           rest of a relative motion (a bridge's steps) took it past its end (4/1's bridge rose 5 m too high) */
        if (!pend || e->u.at.kind != ATR_OBJ) atr_put(e->u.at.kind, e->u.at.ix[k], &e->u.at.r[k]);
        if (pend) {
            atr_pend[atr_pend_n].kind = e->u.at.kind;
            atr_pend[atr_pend_n].ix = e->u.at.ix[k];
            atr_pend[atr_pend_n++].r = e->u.at.r[k];
        }
        if (trace) fprintf(stderr, "coop: the partner's world record kind %d #%d (flg %d)\n", e->u.at.kind, e->u.at.ix[k], e->u.at.r[k].flg);
    }
    return 1;
}

/* The partner's block: its actions in order, as our interpreter would have run them - those that
   may run here (coop_op_replay: no door, no player controller); controller ops behind a refused
   LoadWork do not run (they would act on another work). */
static void replay(void)
{
    unsigned char* d = (unsigned char*)rom->evtp;
    unsigned int* f = flag_block();
    unsigned int pre[NFLAG];
    int i, work_ok = 1, skipped = 0;
    memcpy(pre, f, sizeof(pre));
    for (i = 0; i < trig.n; i++) {
        unsigned char* p = d + trig.ops[i];
        if (is_cond(*p) || is_flow(*p)) continue;
        if (*p == 101) work_ok = coop_op_replay(p);
        if (!coop_op_replay(p) || (!work_ok && *p >= 100 && *p <= 105)) { skipped++; continue; }
        bhScePtr = p;
        bhIfelFlg = 0;
        bhScenarioJmpT[*bhScePtr]();
    }
    bhIfelFlg = 0;
    for (i = 0; i < 32; i++) used_mark(i, pre[i] ^ f[i]);   /* ev_flg */
    for (i = 96; i < 112; i++) used_mark(i, pre[i] ^ f[i]);   /* ky_flg */
    if (skipped) host_log("coop: %d actions of the partner's block act on its own game (door, player): not run here\n", skipped);
    win_open(&win_rx, ev_levels(d, trig.ops, trig.n));
    count_keys(keys_had);   /* a key the block took (PlItemLost) is the partner's use, not ours */
}
/* the actions a fired block executed this frame go to the partner */
static void send_block(const Region* r)
{
    const unsigned char* d = (const unsigned char*)rom->evtp;
    unsigned short ops[TRIG_OPS];
    int n = 0, j, k;
    for (j = 0; j < ntrace && n < TRIG_OPS; j++) {
        unsigned int o = trace_buf[j];
        if (o >= r->then0 && o < r->then1 && !is_cond(d[o]) && !is_flow(d[o])) ops[n++] = o;
    }
    host_log("coop: world event block %04x fired here (gate flag %d/%d), %d actions%s\n", r->start, r->set, r->bit, n,
             have_peer && !echo_delay ? ", sent to the partner" : "");
    if (!have_peer || echo_delay) return;
    win_open(&win_tx, ev_levels(d, ops, n));   /* what its events do to the room follows when they end */
    for (j = 0; j == 0 || j < n; j += 24) {
        Event e = { 0 };
        e.type = EV_TRIG;
        e.u.tr.set = r->set; e.u.tr.val = r->val; e.u.tr.bit = r->bit; e.u.tr.start = r->start;
        e.u.tr.rcase = sys->rcase; e.u.tr.sig = reg_sig;
        e.u.tr.first = j; e.u.tr.n = n;
        e.u.tr.last = j + 24 >= n;
        for (k = 0; k < 24 && j + k < n; k++) e.u.tr.ops[k] = ops[j + k];
        queue_event(&e);
    }
}

/* bhControlEvent: scd1 as bhScenarioCheck runs it, recording each instruction executed */
void coop_scd1(void)
{
    unsigned char* d = (unsigned char*)rom->evtp;
    int i, k, j, fired[MAX_REG], woke;
    unsigned int wkeep[3];

    if (rom->evtp != reg_evtp || scd_sig() != reg_sig) analyze();   /* a new room script */
    woke = wake_partner(wkeep);   /* (enemies a room script wakes when a player steps on a spot) */
    memcpy(pre_flags, flag_block(), sizeof(pre_flags));
    ntrace = 0;
    bhScePtr = d + (int)(size_t)bhScd2Ptr;
    bhIfelFlg = 0;
    while (TRUE) {
        do {
            if (ntrace < (int)(sizeof(trace_buf) / sizeof(trace_buf[0]))) trace_buf[ntrace++] = (unsigned short)(bhScePtr - d);
        } while (bhScenarioJmpT[*bhScePtr]() != 0);
        if (bhIfelFlg <= 0) break;
        bhScePtr = (unsigned char*)--G_Sp;
        bhIfelFlg--;
    }
    if (woke) { sys->cb_flg = (sys->cb_flg & ~0x300u) | wkeep[0]; sys->flr_idx = wkeep[1]; sys->etc_idx = wkeep[2]; }

    /* which world blocks fired: the gate had its "before" value and a flip ran. A flip by an
       event comes later (the flag is still unchanged): at most once per 10 s then. */
    for (i = 0; i < nreg; i++) {
        Region* r = &reg[i];
        int set_flip = 0, ev_flip = 0;
        fired[i] = 0;
        if (flag_val(pre_flags, r->set, r->bit) != r->val) continue;
        for (j = 0; j < ntrace; j++)
            for (k = 0; k < r->nf; k++)
                if (trace_buf[j] == r->flip[k]) { if (d[r->flip[k]] == 5) set_flip = 1; else ev_flip = 1; }
        fired[i] = set_flip || (ev_flip && frame_no - r->fired_at > 300);
    }
    for (i = 0; i < nreg; i++) {
        int outer = 1;
        if (!fired[i]) continue;
        for (k = 0; k < nreg; k++)   /* a block inside another fired one goes with it */
            if (k != i && fired[k] && reg[i].start >= reg[k].then0 && reg[i].start < reg[k].then1) outer = 0;
        reg[i].fired_at = frame_no;
        if (outer) send_block(&reg[i]);
    }

    /* the partner's block, in the room it fired in */
    if (trig.on && trig.id != trig_done && (trig.ev.stg != sys->stg_no || trig.ev.rom != sys->rom_no || trig.ev.u.tr.sig != reg_sig)) {
        host_log("coop: the partner's world event block %04x: we left the room, its flags only\n", trig.ev.u.tr.start);
        trig_done = trig.id;
    }
    if (trig.on && trig.id != trig_done && in_scene() && frame_no - trig.at < SCENE_WAIT) {   /* (see gate_hold) */
        if (!trig.held) host_log("coop: the partner's world event block %04x waits until our scene in %d/%d ends\n", trig.ev.u.tr.start, sys->stg_no, sys->rom_no);
        trig.held = 1;
    } else if (trig.on && trig.id != trig_done && plp && plp->mode0 != 4 && plp->mode0 != 6 && plp->hp >= 0 && !(sys->cb_flg & 0x1)) {
        Region* r = region_at(trig.ev.u.tr.start);
        int door = 0, k2;
        for (k2 = 0; k2 < trig.n; k2++)
            if (d[trig.ops[k2]] == 20 && evdoor[d[trig.ops[k2] + 3]]) door = 1;
        if (flag_val(flag_block(), trig.ev.u.tr.set, trig.ev.u.tr.bit) != trig.ev.u.tr.val)
            host_log("coop: the partner's world event block %04x happened here already\n", trig.ev.u.tr.start);
        else if (door)   /* its events take the partner through a door: its own way, not ours (Follow host decides) */
            host_log("coop: the partner's world event block %04x takes it through a door: its flags only here\n", trig.ev.u.tr.start);
        else if (r && frame_no - r->fired_at <= 300)
            host_log("coop: the partner's world event block %04x is running here already\n", trig.ev.u.tr.start);
        else {
            replay();
            host_log("coop: replayed the partner's world event block %04x (%d actions)\n", trig.ev.u.tr.start, trig.n);
        }
        if (r) r->fired_at = frame_no;
        trig_done = trig.id;
    }
}

/* EV_TRIG: collect the chunks; the last one waits (stops the stream) until our next scd1 replayed
   it, so the flags the partner sends behind it cannot arrive before. Replayed only as one of our own
   world event blocks: the block it names is one our sweep of this room's script found, with the
   same gate, and every action is an instruction of that block's then-part (offsets into our own
   script: the partner names instructions, it cannot send any). */
static struct { unsigned short start, n; } trig_hdr;
static int apply_trig(const Event* e)
{
    int k, first = e->u.tr.first;
    const Region* r;
    if (!first) { memset(trig_ops, 0, sizeof(trig_ops)); trig_hdr.start = e->u.tr.start; trig_hdr.n = e->u.tr.n; }
    else if (trig_hdr.start != e->u.tr.start || trig_hdr.n != e->u.tr.n) return 1;   /* not the chunks of one block */
    for (k = 0; k < 24 && first + k < e->u.tr.n; k++) trig_ops[first + k] = e->u.tr.ops[k];
    if (!e->u.tr.last) return 1;
    if (trig.on && trig_done == trig.id && e->id == trig.id) { trig.on = 0; return 1; }   /* replayed */
    if (!plp || e->stg != sys->stg_no || e->rom != sys->rom_no || e->u.tr.rcase != sys->rcase || !in_session_game() ||
        e->u.tr.sig != scd_sig() || (sys->tk_flg & 0x28)) {
        if (trig.on && e->id == trig.id) trig.on = 0;
        host_log("coop: the partner's world event block %04x is not in our room: its flags only\n", e->u.tr.start);
        return 1;
    }
    if (rom->evtp != reg_evtp || scd_sig() != reg_sig) analyze();
    r = region_at(e->u.tr.start);
    if (!r || r->set != e->u.tr.set || r->bit != e->u.tr.bit || r->val != e->u.tr.val ||
        !coop_trig_ops_ok((const unsigned char*)rom->evtp, r->then1 + 2, r->then0, r->then1, trig_ops, e->u.tr.n)) {
        host_log("coop: the partner's world event block %04x is none of this room's: ignored\n", e->u.tr.start);
        return 1;
    }
    if (!trig.on || trig.id != e->id) {
        trig.on = 1;
        trig.held = 0;
        trig.id = e->id;
        trig.at = frame_no;
        trig.ev = *e;
        trig.n = e->u.tr.n;
        memcpy(trig.ops, trig_ops, sizeof(trig.ops));
    }
    return 0;
}

/* ---------------------------------------------------------------- trade (M8) */
/* One offer each way at a time, between players standing next to each other. The offerer takes
   the item out of its inventory into escrow when it offers. The receiver decides: it adds the
   item and answers "taken", or answers "refused" (declined, cancelled by the offerer, asked about
   an offer it does not have). The offerer gives the escrow back only on "refused" and drops it on
   "taken": the item is never in both inventories and never gone; without an answer it stays held
   (F3 shows it). Answers are kept in a small ledger that outlives sessions; after a reconnect
   the offerer asks again (QUERY) and gets the same answer. Not allowed: the equipped weapon,
   two-slot items. */
#define TRADE_NEAR 30.0f                    /* game units (~3 m) */
static struct { unsigned int tid, entry; int state, back; } t_out;   /* our offer: 1 waiting, 2 refused - to give back; back: in our inventory again (escrow_home) */
static struct { unsigned int tid, entry; int on; } t_in;        /* the partner's offer to us */
static struct { unsigned int tid; unsigned char yes; } ledger[32];
static int ledger_n;
static unsigned int tid_count;

static unsigned int* my_inv(void) { return &sys->itm[sys->ply_id * 16]; }
static int inv_slots(void) { return (sys->gm_flg & 0x8000000) ? 10 : 8; }

static void count_keys(unsigned char* keys)
{
    unsigned int* inv = my_inv();
    int i;
    memset(keys, 0, 156);
    for (i = 2; i < 12; i++) {
        int id = (inv[i] >> 16) & 0xFF;
        if (key_item(id) && keys[id] < 255) keys[id]++;
    }
}

/* Inventory blocks (pc_rules.h pc_inv_*): ours, or a copy to try something on. A two-slot item (itemdata
   type 0x100: rocket launcher, rifles, Lugers, sub machine guns) is one word over two cells. */
static int is_two(unsigned int w) { return pc_inv_two(itemdata, w); }
static int blk_add(unsigned int* b, int max, unsigned int entry) { return pc_inv_add(b, max, entry, itemdata); }
static unsigned int blk_take(unsigned int* b, int max, int s, int count, int two) { return pc_inv_take(b, max, s, count, two, itemdata); }

static int inv_add(unsigned int entry) { return blk_add(my_inv(), inv_slots(), entry); }
static unsigned int inv_take(int s, int count) { return blk_take(my_inv(), inv_slots(), s, count, 0); }   /* F3: no two-slot items */

static const char* item_name(unsigned int entry)
{
    static char buf[48];
    int id = (entry >> 16) & 0xFF;
    if (id < CHEAT_ITEMS && g_cheat.names[id][0]) snprintf(buf, sizeof(buf), "%s x%u", g_cheat.names[id], entry & 0xFFFF);
    else snprintf(buf, sizeof(buf), tr("item %d x%u"), id, entry & 0xFFFF);
    return buf;
}

static void trade_msg(const char* fmt, unsigned int entry)
{
    snprintf(g_coop.trade_msg, sizeof(g_coop.trade_msg), tr(fmt), item_name(entry));
    host_log("coop: trade: %s\n", g_coop.trade_msg);
}

static void trade_send(int op, unsigned int tid, unsigned int entry, int yes)
{
    Event e = { 0 };
    if (!sys) return;
    e.type = EV_TRADE;
    e.u.td.op = op; e.u.td.tid = tid; e.u.td.entry = entry; e.u.td.yes = yes;
    queue_event(&e);
}

static int ledger_find(unsigned int tid)
{
    int i;
    for (i = 0; i < 32 && i < ledger_n; i++)
        if (ledger[i].tid == tid) return ledger[i].yes;
    return -1;
}
static void ledger_add(unsigned int tid, int yes)
{
    ledger[ledger_n % 32].tid = tid;
    ledger[ledger_n % 32].yes = yes;
    ledger_n++;
}

/* a new session: an open offer of ours asks again, the partner's open offer to us is refused
   (the partner asks and hears so) */
static void trade_reset(void)
{
    if (t_out.state == 1) trade_send(TD_QUERY, t_out.tid, t_out.entry, 0);
    if (t_in.on) { ledger_add(t_in.tid, 0); t_in.on = 0; }
}

static int trade_near(void)
{
    float dx = rx.px - plp->px, dz = rx.pz - plp->pz;
    return same_room() && (rx.flags & PF_VISIBLE) && dx * dx + dz * dz < TRADE_NEAR * TRADE_NEAR;
}

/* ---------------------------------------------------------------- shared item box (M8) */
/* With shared loot both players use one item box (sys->itm 64..223: the box and the game's two
   special boxes). The box screen runs while cb_flg 0x40000 is set; when one closes it, what it took
   out and put in (inventory words, as a multiset) goes to the partner, which applies it to its box
   once its own box screen is closed. Both may use a box at the same time (the boxes are linked
   across rooms): the host decides. Its own changes count as they are; the client's are applied by
   the host where they still fit - an item the client took that the host's box no longer has (the
   host took it first) goes back as EV_BOXFIX and leaves the client's inventory again. So an item
   is never taken out twice. Individual loot: a box per player. */
#define BOX0 64
#define BOX_N 160
static unsigned int box_base[BOX_N];
static int box_was_open;

static int box_shared(void) { return have_peer && !echo_delay && !individual(); }

static void box_pack(void)   /* the main box's entries to the front, in order */
{
    unsigned int* b = &sys->itm[BOX0];
    int i, n = 0;
    for (i = 0; i < 128; i++)
        if (b[i] >> 16) b[n++] = b[i];
    for (; n < 128; n++) b[n] = 0;
}
static int box_take(unsigned int w)
{
    int i;
    for (i = 0; i < BOX_N; i++)
        if (sys->itm[BOX0 + i] == w) { sys->itm[BOX0 + i] = 0; return 1; }
    return 0;
}
static int box_put(unsigned int w)
{
    int i;
    for (i = 0; i < 128; i++)
        if (!(sys->itm[BOX0 + i] >> 16)) { sys->itm[BOX0 + i] = w; return 1; }
    return 0;
}

static void box_send(int type, const unsigned int* rm, int nrm, const unsigned int* add, int nadd)
{
    while (nrm > 0 || nadd > 0) {
        Event e = { 0 };
        int k = 0;
        e.type = type;
        for (; nrm > 0 && k < 16; nrm--, k++) { e.u.bx.w[k] = *rm++; e.u.bx.nrm++; }
        for (; nadd > 0 && k < 16; nadd--, k++) { e.u.bx.w[k] = *add++; e.u.bx.nadd++; }
        queue_event(&e);
    }
}

/* at the hand-off: the box screen opened (remember the box) or closed (send the difference) */
static void box_frame(void)
{
    int open = (sys->cb_flg & 0x40000) != 0;
    static const char* env = (const char*)-1;
    unsigned int f, k;
    /* test aid "frame:entry[,frame:entry]": take that box entry out, as the box screen would; entry
       100 + n puts inventory slot n in */
    if (env == (const char*)-1) env = getenv("RECVX_COOP_BOX");
    if (env && sscanf(env, "%u:%u", &f, &k) == 2 && test_clock() >= f && tx_in_game && !open &&
        (k >= 100 ? my_inv()[(k - 100) & 15] >> 16 : k < 128 && sys->itm[BOX0 + k] >> 16)) {   /* waits for the entry */
        unsigned int w = k >= 100 ? my_inv()[k - 100] : k < 128 ? sys->itm[BOX0 + k] : 0;
        env = strchr(env, ',') ? strchr(env, ',') + 1 : NULL;
        memcpy(box_base, &sys->itm[BOX0], sizeof(box_base));
        if (k >= 100 && w >> 16 && inv_take(k - 100, 0)) { box_put(w); box_was_open = 1; host_log("coop: test: put %08x into the item box\n", w); }
        else if (k < 100 && w >> 16 && inv_add(w)) { sys->itm[BOX0 + k] = 0; box_pack(); box_was_open = 1; host_log("coop: test: took %08x out of the item box\n", w); }
    }
    if (open && !box_was_open) memcpy(box_base, &sys->itm[BOX0], sizeof(box_base));
    if (!open && box_was_open && box_shared()) {
        unsigned int now[BOX_N], rm[BOX_N], add[BOX_N];
        int nrm = 0, nadd = 0, i, j;
        memcpy(now, &sys->itm[BOX0], sizeof(now));
        for (i = 0; i < BOX_N; i++) {   /* multiset difference */
            if (!(box_base[i] >> 16)) continue;
            for (j = 0; j < BOX_N && now[j] != box_base[i]; j++) ;
            if (j < BOX_N) now[j] = 0; else rm[nrm++] = box_base[i];
        }
        for (j = 0; j < BOX_N; j++)
            if (now[j] >> 16) add[nadd++] = now[j];
        if (nrm || nadd) { box_send(EV_BOX, rm, nrm, add, nadd); host_log("coop: item box: %d taken out, %d put in, sent\n", nrm, nadd); }
    }
    box_was_open = open;
}

static int inv_remove(unsigned int w)   /* w's item (its count of a stack) out of our inventory, not the equipped one */
{
    unsigned int* inv = my_inv();
    int i, max = inv_slots(), id = (w >> 16) & 0xFF, c = w & 0xFFFF;
    for (i = 2; i < 2 + max; i++)
        if (((inv[i] >> 16) & 0xFF) == (unsigned)id && (int)inv[0] != i) {
            if ((itemdata[id].type & 0x200) && (int)(inv[i] & 0xFFFF) > c) inv[i] -= c;
            else blk_take(inv, max, i, 0, 1);
            count_keys(keys_had);
            return 1;
        }
    return 0;
}

static void inv_drop(unsigned int w)   /* EV_BOXFIX: an item we took that the partner had taken first */
{
    if (inv_remove(w)) trade_msg("Your partner took %s from the item box first.", w);
}

static int grant_return(unsigned int w);
/* EV_BOXFIX: an item we put into the box that the host's box had no room for (both put items in at the same
   time). It leaves our copy of the box (the host's is the one that counts) and comes back: into our inventory,
   else at our feet as a pickup (grant_return: kept with our game, taken back word for word), else (8 items wait
   there already) it stays in our copy of the box. Added even when our copy no longer has it: we took it out
   again meanwhile, and the host answers that take with its own EV_BOXFIX (inv_drop), which removes one copy. */
static void box_back(unsigned int w)
{
    box_take(w);
    if (inv_add(w)) trade_msg("Item box full. Returned %s to your inventory.", w);
    else if (grant_return(w)) trade_msg("Inventory and item box full. Placed %s at your feet.", w);
    else { box_put(w); trade_msg("Item box full. %s remains in the item box.", w); }
}

static int apply_box(const Event* e)
{
    unsigned int miss[16], back[16];
    int k, n = 0, nb = 0;
    if (!tx_in_game || (sys->cb_flg & 0x40000)) return 0;   /* our box screen (or a menu) first */
    if (e->type == EV_BOXFIX) {   /* from the host: what we took that it had not, what it had no room for */
        for (k = 0; k < e->u.bx.nrm; k++) inv_drop(e->u.bx.w[k]);
        for (; k < e->u.bx.nrm + e->u.bx.nadd; k++) box_back(e->u.bx.w[k]);
        box_pack();
        count_keys(keys_had);
        return 1;
    }
    for (k = 0; k < e->u.bx.nrm; k++)
        if (!box_take(e->u.bx.w[k])) miss[n++] = e->u.bx.w[k];
    for (; k < e->u.bx.nrm + e->u.bx.nadd; k++)
        if (!box_put(e->u.bx.w[k])) back[nb++] = e->u.bx.w[k];
    box_pack();
    host_log("coop: item box: the partner took %d out and put %d in (%d already gone here, %d without room)\n", e->u.bx.nrm, e->u.bx.nadd, n, nb);
    if (is_host && (n || nb)) { box_send(EV_BOXFIX, miss, n, back, nb); host_log("coop: item box: %d of the partner's items were taken here first, %d go back to it\n", n, nb); }
    return 1;
}

static int apply_trade(const Event* e)
{
    int yes;
    switch (e->u.td.op) {
    case TD_OFFER:
        if (ledger_find(e->u.td.tid) >= 0 || t_in.on) {   /* one at a time */
            if (ledger_find(e->u.td.tid) < 0) ledger_add(e->u.td.tid, 0);
            trade_send(TD_ANSWER, e->u.td.tid, e->u.td.entry, ledger_find(e->u.td.tid));
            return 1;
        }
        t_in.tid = e->u.td.tid; t_in.entry = e->u.td.entry; t_in.on = 1;
        trade_msg("Your partner offers %s.", e->u.td.entry);
        return 1;
    case TD_CANCEL:
    case TD_QUERY:
        if (t_in.on && t_in.tid == e->u.td.tid) t_in.on = 0;
        if ((yes = ledger_find(e->u.td.tid)) < 0) ledger_add(e->u.td.tid, yes = 0);   /* never taken: refused from now on */
        trade_send(TD_ANSWER, e->u.td.tid, e->u.td.entry, yes);
        if (e->u.td.op == TD_CANCEL && !yes) trade_msg("Your partner withdrew %s.", e->u.td.entry);
        return 1;
    case TD_ANSWER:
        if (t_out.state != 1 || t_out.tid != e->u.td.tid) return 1;
        if (e->u.td.yes) { t_out.state = 0; trade_msg("Your partner received %s.", t_out.entry); if (t_out.back) escrow_taken(t_out.entry); }
        else t_out.state = t_out.back ? 0 : 2;   /* given back at the next frame in game (unless it is back already) */
        return 1;
    }
    return 1;
}

/* at the hand-off: the overlay's requests (in game only: the inventory is not open), escrow back */
static void trade_frame(void)
{
    int req = g_coop.trade_req ? __atomic_exchange_n(&g_coop.trade_req, 0, __ATOMIC_ACQ_REL) : 0;
    int near = tx_in_game && local_visible() && have_rx && trade_near();
    static const char* env = (const char*)-1;
    if (env == (const char*)-1) env = getenv("RECVX_COOP_TRADE");   /* test aid: "frame:slot:count[:accept]" */
    if (env && !req) {
        unsigned int f; int s, c, a = -1;
        if (sscanf(env, "%u:%d:%d:%d", &f, &s, &c, &a) >= 3 && f && test_clock() >= f && near && !t_out.state) { g_coop.trade_slot = s; g_coop.trade_count = c; req = COOP_TRADE_OFFER; env = NULL; }
        else if (sscanf(env, "0:%d:%d:%d", &s, &c, &a) == 3 && t_in.on && near) req = a ? COOP_TRADE_ACCEPT : COOP_TRADE_DECLINE;
    }
    if (!sys || !plp) return;
    if (tx_in_game && t_out.state == 2) {
        if (inv_add(t_out.entry)) { t_out.state = 0; count_keys(keys_had); trade_msg("Offer ended. %s returned to your inventory.", t_out.entry); }
        else snprintf(g_coop.trade_msg, sizeof(g_coop.trade_msg), tr("Make room in your inventory to reclaim %s."), item_name(t_out.entry));
    }
    if (req && !tx_in_game) { snprintf(g_coop.trade_msg, sizeof(g_coop.trade_msg), tr("Finish the current menu, scene or door transition first.")); req = 0; }
    switch (req) {
    case COOP_TRADE_OFFER: {
        unsigned int e;
        if (t_out.state) { snprintf(g_coop.trade_msg, sizeof(g_coop.trade_msg), tr("Finish your current offer first.")); break; }
        if (!near || !have_peer) { snprintf(g_coop.trade_msg, sizeof(g_coop.trade_msg), tr("Stand next to your partner to trade.")); break; }
        if (!(e = inv_take(g_coop.trade_slot, g_coop.trade_count))) { snprintf(g_coop.trade_msg, sizeof(g_coop.trade_msg), tr("Choose an unequipped item that fits in one inventory space.")); break; }
        count_keys(keys_had);
        t_out.tid = my_sid * 31u + ++tid_count;
        t_out.entry = e;
        t_out.state = 1;
        t_out.back = 0;
        trade_send(TD_OFFER, t_out.tid, e, 0);
        trade_msg("You offer %s.", e);
        break;
    }
    case COOP_TRADE_CANCEL:
        if (t_out.state == 1) { trade_send(TD_CANCEL, t_out.tid, t_out.entry, 0); snprintf(g_coop.trade_msg, sizeof(g_coop.trade_msg), tr("Withdrawing offer...")); }
        break;
    case COOP_TRADE_ACCEPT:
        if (!t_in.on) break;
        if (!near) { snprintf(g_coop.trade_msg, sizeof(g_coop.trade_msg), tr("Stand next to your partner to trade.")); break; }
        if (!inv_add(t_in.entry)) { snprintf(g_coop.trade_msg, sizeof(g_coop.trade_msg), tr("No room for %s."), item_name(t_in.entry)); break; }
        count_keys(keys_had);
        ledger_add(t_in.tid, 1);
        trade_send(TD_ANSWER, t_in.tid, t_in.entry, 1);
        t_in.on = 0;
        trade_msg("Received %s.", t_in.entry);
        break;
    case COOP_TRADE_DECLINE:
        if (!t_in.on) break;
        ledger_add(t_in.tid, 0);
        trade_send(TD_ANSWER, t_in.tid, t_in.entry, 0);
        t_in.on = 0;
        trade_msg("Declined %s.", t_in.entry);
        break;
    }
    g_coop.trade_near = near;
    g_coop.trade_slots = inv_slots();
    memcpy(g_coop.trade_inv, my_inv(), sizeof(g_coop.trade_inv));
    g_coop.trade_out = t_out.state && !t_out.back ? t_out.entry : 0;
    g_coop.trade_in = t_in.on ? t_in.entry : 0;
}

/* ---------------------------------------------------------------- trade screen (protocol 14, EV_TSCREEN) */
/* docs/COOP.md "Trade screen". The trade request binding (B_TRADE: T, left stick click) next to the
   partner asks it (REQ); the partner's press accepts (OPEN) and both get the screen (trade_glue.c):
   our inventory and the partner's, each player picks a cell of its own side, possibly empty, and
   confirms (ready). Either side's change of selection takes back our ready. With both ready on the
   same pair the host commits: it takes its item out into escrow and sends SWAP; the guest applies its
   side at once (its item out, the host's in) or refuses, answers DONE and keeps the answer in the trade
   ledger (the F3 trade's); the host then adds the guest's item, or gives its own back on a refusal.
   After a reconnect the host asks again (QUERY) and hears the same answer: no copy, no loss. Leaving
   (cancel, right click, distance, an event, a lost partner) closes the screen for both, unchanged.
   Not tradable: the equipped weapon (unequip it in the inventory first) and the lighter's slot. */
TradeView g_trade;
#define TS_ASK_MS 12000                 /* a request waits this long for an answer */
#define TS_ASKED_MS 15000               /* the partner's request is shown at most this long */
enum { WHY_OK, WHY_NOTHING, WHY_EQUIPPED, WHY_ROOM, WHY_PROOM, WHY_CHANGED, WHY_WAIT, WHY_LOST };   /* < TS_WHY_MAX */
static struct {
    unsigned int sid, t;                /* the screen (the request's id); when the phase began (net_ms) */
    int key_was;                        /* the trade binding was held */
    int pcell;                          /* the partner's view of our cell and word (its last STATE) */
    unsigned int ptake;
    Event last;                         /* our last STATE */
    struct { unsigned int tid, give, take; int state, back; } sw;   /* host's swap: 1 sent, 2 refused (give back), 3 done (take in); back: give is in our inventory again (escrow_home) */
} ts;

int pad_trade_held(void);               /* pc/src/input/pad.c */
extern unsigned int trade_test_pad;     /* trade_glue.c: test presses */
const char* bind_code_name(int dev, int code, char* buf, int n);
int bind_get(int row, int dev, int slot);

int coop_trade_frozen(void) { return frozen; }

const char* coop_item_name(unsigned int w)
{
    static char buf[40];
    int id = (w >> 16) & 0xFF;
    if (id < CHEAT_ITEMS && g_cheat.names[id][0]) return g_cheat.names[id];
    snprintf(buf, sizeof(buf), tr("item %d"), id);
    return buf;
}

/* cell -> grid word as the game lays them out (ItemSort, ItemSet): two-slot items first, a row
   each, then the others two per row; returns the cells used */
int coop_trade_layout(const unsigned int* grid, int cells, signed char* word_at)
{
    int n = 0, pass, i;
    for (i = 0; i < 10; i++) word_at[i] = -1;
    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < 10 && n < cells; i++)
            if ((grid[i] >> 16) & 0xFF && is_two(grid[i]) == !pass) {
                word_at[n++] = i;
                if (!pass && n < cells) word_at[n++] = i;
            }
    return n;
}

static int cell_word(int side, int cell)
{
    signed char w[10];
    if (cell < 0 || cell >= g_trade.cells[side]) return -1;
    coop_trade_layout(g_trade.grid[side], g_trade.cells[side], w);
    return w[cell];
}
static unsigned int cell_val(int side, int cell) { int w = cell_word(side, cell); return w >= 0 ? g_trade.grid[side][w] : 0; }
static unsigned int wire_word(unsigned int w) { return coop_item_word_ok(w) ? w : 0; }   /* what the checks pass */

static void blk_of(int side, unsigned int* b)   /* a side as an inventory block */
{
    b[0] = g_trade.eq[side] >= 0 ? 2 + g_trade.eq[side] : 0;
    b[1] = 0;
    memcpy(b + 2, g_trade.grid[side], 40);
}

static void ts_view(void)   /* our side from the inventory */
{
    unsigned int* inv = my_inv();
    int i;
    g_trade.cells[0] = inv_slots();
    for (i = 0; i < 10; i++) g_trade.grid[0][i] = wire_word(inv[2 + i]);
    g_trade.eq[0] = inv[0] >= 2 && inv[0] < 12 ? (int)inv[0] - 2 : -1;
}

static void inv_log(const char* tag)   /* the inventory, for the logs (tests check both sides) */
{
    unsigned int* v = my_inv();
    host_log("coop: trade inventory (%s): equipped %u, grid %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x, held for a swap %08x\n", tag,
             v[0], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11], (ts.sw.state == 1 && !ts.sw.back) || ts.sw.state == 2 ? ts.sw.give : 0);
}

static void ts_say(const char* m)
{
    snprintf(g_trade.msg, sizeof(g_trade.msg), "%s", m);
    g_trade.msg_until = net_ms() + 4000;
    host_log("coop: trade screen: %s\n", m);
}

/* why the selected pair does not swap (text into g_trade.why), WHY_OK when it does */
static int pair_why(void)
{
    unsigned int mine = cell_val(0, g_trade.sel[0]), theirs = cell_val(1, g_trade.sel[1]), b[12];
    int wm = cell_word(0, g_trade.sel[0]), wt = cell_word(1, g_trade.sel[1]);
    char* o = g_trade.why;
    int n = sizeof(g_trade.why);
    *o = 0;
    if (g_trade.sel[0] < 0) { snprintf(o, n, tr("Choose an item to give and confirm. Choose an empty space to receive only.")); return WHY_NOTHING; }
    if (g_trade.sel[1] < 0) { snprintf(o, n, tr("Waiting for %s to choose..."), g_trade.name); return WHY_WAIT; }
    if (!mine && !theirs) { snprintf(o, n, tr("Choose an item to give, or wait for %s to offer one."), g_trade.name); return WHY_NOTHING; }
    if (mine && wm == g_trade.eq[0]) { snprintf(o, n, tr("Unequip your %s in the inventory first."), coop_item_name(mine)); return WHY_EQUIPPED; }
    if (theirs && wt == g_trade.eq[1]) { snprintf(o, n, tr("%s's %s is equipped"), g_trade.name, coop_item_name(theirs)); return WHY_EQUIPPED; }
    blk_of(0, b);
    if (mine) blk_take(b, g_trade.cells[0], 2 + wm, 0, 1);
    if (theirs && !blk_add(b, g_trade.cells[0], theirs)) {
        snprintf(o, n, tr(is_two(theirs) ? "No room for %s. Free two inventory spaces." : "No room for %s."), coop_item_name(theirs));
        return WHY_ROOM;
    }
    blk_of(1, b);
    if (theirs) blk_take(b, g_trade.cells[1], 2 + wt, 0, 1);
    if (mine && !blk_add(b, g_trade.cells[1], mine)) {
        snprintf(o, n, tr(is_two(mine) ? "%s needs two free inventory spaces for %s." : "%s has no room for %s."), g_trade.name, coop_item_name(mine));
        return WHY_PROOM;
    }
    return WHY_OK;
}

static Event ts_ev(int op, unsigned int sid)
{
    Event e;
    memset(&e, 0, sizeof(e));
    e.type = EV_TSCREEN;
    e.u.ts.op = op;
    e.u.ts.sid = sid;
    e.u.ts.cell = e.u.ts.pcell = e.u.ts.eq = TSC_NONE;
    return e;
}
static void ts_send(int op, unsigned int sid) { Event e = ts_ev(op, sid); queue_event(&e); }

/* test aid RECVX_COOP_TS_LOSE="s<n>" (host: its n-th SWAP) or "d<n>" (guest: its n-th DONE) is not sent,
   as if it went with the old session (a reconnect then asks again) */
static int ts_lose(char what)
{
    static const char* e = (const char*)-1;
    static int n;
    if (e == (const char*)-1) e = getenv("RECVX_COOP_TS_LOSE");
    if (!e || e[0] != what || ++n != atoi(e + 1)) return 0;
    host_log("coop: test: the swap's %s is not sent\n", what == 's' ? "SWAP" : "DONE");
    return 1;
}

static void ts_state(int force)   /* our side to the partner when it changed */
{
    Event e = ts_ev(TS_STATE, ts.sid);
    e.u.ts.slots = g_trade.cells[0];
    e.u.ts.eq = g_trade.eq[0] >= 0 ? g_trade.eq[0] : TSC_NONE;
    e.u.ts.cell = g_trade.sel[0] >= 0 ? g_trade.sel[0] : TSC_NONE;
    e.u.ts.pcell = g_trade.sel[1] >= 0 ? g_trade.sel[1] : TSC_NONE;
    e.u.ts.ready = g_trade.ready[0] != 0;
    e.u.ts.give = cell_val(0, g_trade.sel[0]);
    e.u.ts.take = cell_val(1, g_trade.sel[1]);
    memcpy(e.u.ts.inv, g_trade.grid[0], sizeof(e.u.ts.inv));
    if (!force && !memcmp(&e.u.ts, &ts.last.u.ts, sizeof(e.u.ts))) return;
    ts.last = e;
    queue_event(&e);
}

static void ts_open(unsigned int sid)
{
    signed char w[10];
    int i;
    g_trade.phase = TRADE_OPEN;
    ts.sid = sid;
    ts.t = net_ms();
    ts.pcell = -1; ts.ptake = 0;
    ts_view();
    coop_trade_layout(g_trade.grid[0], g_trade.cells[0], w);
    for (i = 0; i < g_trade.cells[0] && (w[i] < 0 || w[i] == g_trade.eq[0]); i++) ;   /* the cursor on the first item that can go */
    g_trade.cur_side = 0;
    g_trade.cur = i < g_trade.cells[0] ? i : 0;
    g_trade.sel[0] = g_trade.sel[1] = -1;   /* nothing offered until chosen */
    g_trade.want[0] = g_trade.want[1] = -1;
    g_trade.ready[0] = g_trade.ready[1] = 0;
    g_trade.eq[1] = -1; g_trade.cells[1] = 8;
    memset(g_trade.grid[1], 0, sizeof(g_trade.grid[1]));
    g_trade.msg_until = 0;
    ts_state(1);
    host_log("coop: trade screen open (%08x)\n", sid);
    inv_log("open");
}

static void ts_close(int send, const char* why)
{
    if (g_trade.phase != TRADE_OPEN) return;
    if (send) ts_send(TS_CLOSE, ts.sid);
    g_trade.phase = TRADE_IDLE;
    g_trade.ready[0] = g_trade.ready[1] = 0;
    g_trade.want[0] = g_trade.want[1] = -1;   /* requests end with the screen */
    ts_say(why);
    inv_log("closed");
}

/* the screen (trade_glue.c, game thread). The cursor and the selection are apart: the cursor (keys,
   pad, mouse hover) only points; an activation offers a cell and only cancel takes the offer back, so
   moving, switching sides, hovering or the window losing focus never changes what is offered. */
void coop_trade_select(int cell)
{
    if (g_trade.phase != TRADE_OPEN || cell < 0 || cell >= g_trade.cells[0]) return;
    g_trade.cur_side = 0; g_trade.cur = cell;
    if (cell == g_trade.sel[0]) return;
    g_trade.sel[0] = cell;
    g_trade.ready[0] = 0;
    CallSystemSe(0, 2);
    host_log("coop: trade screen: we offer cell %d (%08x)\n", cell, cell_val(0, cell));
}

void coop_trade_cursor(int side, int cell)
{
    if (g_trade.phase != TRADE_OPEN || side < 0 || side > 1 || cell < 0 || cell >= g_trade.cells[side]) return;
    if (side == g_trade.cur_side && cell == g_trade.cur) return;
    g_trade.cur_side = side; g_trade.cur = cell;
    CallSystemSe(0, 2);
    host_log("coop: trade screen: cursor on %s %d (offered: %d)\n", side ? "the partner's" : "our", cell, g_trade.sel[0]);
}

static void ts_want_send(int op, int cell, unsigned int w)
{
    Event e = ts_ev(op, ts.sid);
    e.u.ts.cell = cell >= 0 ? cell : TSC_NONE;
    e.u.ts.take = cell >= 0 ? w : 0;
    queue_event(&e);
}

void coop_trade_want(int cell)
{
    unsigned int w = cell_val(1, cell);
    char m[256];
    if (g_trade.phase != TRADE_OPEN) return;
    if (cell >= 0 && !w) { CallSystemSe(0, 1); return; }   /* an empty cell: nothing to ask for */
    if (cell >= 0 && cell_word(1, cell) == g_trade.eq[1]) {
        snprintf(m, sizeof(m), tr("%s's %s is equipped"), g_trade.name, coop_item_name(w));
        ts_say(m); CallSystemSe(0, 1); return;
    }
    if (cell == g_trade.want[0] && (cell < 0 || w == g_trade.want_w[0])) return;
    g_trade.want[0] = cell; g_trade.want_w[0] = w;
    ts_want_send(TS_WANT, cell, w);
    CallSystemSe(0, cell >= 0 ? 3 : 0);
    host_log("coop: trade screen: we request %d (%08x)\n", cell, w);
}

int coop_trade_wanted(int cell, unsigned int w)
{
    char m[256];
    if (g_trade.phase != TRADE_OPEN) return 0;
    if (cell < 0) {
        if (g_trade.want[1] >= 0) { snprintf(m, sizeof(m), tr("%s withdrew the request"), g_trade.name); ts_say(m); }
        g_trade.want[1] = -1;
        return 1;
    }
    ts_view();   /* our real inventory */
    if (cell >= g_trade.cells[0] || !w || cell_val(0, cell) != w) { host_log("coop: trade screen: request for %d (%08x) refused: not what we have there\n", cell, w); return 0; }
    if (cell == g_trade.want[1] && w == g_trade.want_w[1]) return 1;
    g_trade.want[1] = cell; g_trade.want_w[1] = w;
    snprintf(m, sizeof(m), tr("%s requests: %s"), g_trade.name, coop_item_name(w));
    ts_say(m);
    CallSystemSe(0, 3);
    return 1;
}

void coop_trade_offer_wanted(void) { if (g_trade.want[1] >= 0) coop_trade_select(g_trade.want[1]); }

void coop_trade_activate(void)
{
    if (g_trade.phase != TRADE_OPEN) return;
    if (g_trade.cur_side) coop_trade_want(g_trade.cur == g_trade.want[0] ? -1 : g_trade.cur);
    else if (g_trade.cur == g_trade.sel[0]) coop_trade_ready();
    else coop_trade_select(g_trade.cur);
}

int coop_trade_back_what(void)
{
    if (g_trade.want[0] >= 0 && g_trade.cur_side) return TB_UNWANT;
    if (g_trade.sel[0] >= 0) return TB_UNSELECT;
    if (g_trade.want[0] >= 0) return TB_UNWANT;
    if (g_trade.want[1] >= 0) return TB_DECLINE;
    return TB_LEAVE;
}

int coop_trade_back(int may_leave)
{
    int what = g_trade.phase == TRADE_OPEN ? coop_trade_back_what() : TB_NONE;
    char m[256];
    switch (what) {
    case TB_UNWANT: coop_trade_want(-1); break;
    case TB_UNSELECT:
        g_trade.sel[0] = -1; g_trade.ready[0] = 0;
        CallSystemSe(0, 0);
        host_log("coop: trade screen: we offer nothing\n");
        break;
    case TB_DECLINE:
        ts_want_send(TS_DECLINE, g_trade.want[1], g_trade.want_w[1]);
        snprintf(m, sizeof(m), tr("Declined %s's request"), g_trade.name);
        ts_say(m);
        g_trade.want[1] = -1;
        CallSystemSe(0, 0);
        break;
    case TB_LEAVE:
        if (!may_leave) return TB_NONE;
        coop_trade_leave();
        break;
    }
    return what;
}

void coop_trade_ready(void)
{
    if (g_trade.phase != TRADE_OPEN) return;
    if (g_trade.ready[0]) { g_trade.ready[0] = 0; CallSystemSe(0, 0); return; }
    if (g_trade.waiting || pair_why() != WHY_OK) { CallSystemSe(0, 1); if (g_trade.why[0]) ts_say(g_trade.why); return; }
    g_trade.ready[0] = 1;
    CallSystemSe(0, 3);
}

void coop_trade_leave(void)
{
    if (g_trade.phase != TRADE_OPEN) return;
    CallSystemSe(0, 0);
    ts_close(1, "You left the trade");
}

static void ts_done_msg(int yes, int why, unsigned int gave, unsigned int got)
{
    char m[256];
    static const char* const whys[] = { "", "nothing to swap", "an item is equipped", "no room", "no room", "the selection changed", "not ready", "it did not reach the partner" };
    if (!yes) snprintf(m, sizeof(m), tr("Trade failed: %s."), tr(why < (int)(sizeof(whys) / sizeof(whys[0])) ? whys[why] : "refused"));
    else if (gave && got) snprintf(m, sizeof(m), tr("Traded %.32s for %.32s."), coop_item_name(gave), coop_item_name(got));
    else snprintf(m, sizeof(m), tr(gave ? "Gave %.32s." : "Received %.32s."), coop_item_name(gave ? gave : got));
    ts_say(m);
}

/* host: both ready on the same pair (each saw the other's current choice): commit */
static void host_commit(void)
{
    unsigned int h, g, w;
    int wm;
    if (ts.sw.state || !g_trade.ready[0] || !g_trade.ready[1] || g_trade.sel[1] < 0) return;
    if (ts.pcell != g_trade.sel[0] || ts.ptake != cell_val(0, g_trade.sel[0])) return;   /* its ready is for another pair */
    if (pair_why() != WHY_OK) { g_trade.ready[0] = 0; return; }
    h = cell_val(0, g_trade.sel[0]);
    g = cell_val(1, g_trade.sel[1]);
    wm = cell_word(0, g_trade.sel[0]);
    if (h && (w = blk_take(my_inv(), inv_slots(), 2 + wm, 0, 1)) != h) {   /* into escrow */
        if (w) blk_add(my_inv(), inv_slots(), w);
        g_trade.ready[0] = 0;
        return;
    }
    count_keys(keys_had);
    ts.sw.tid = my_sid * 31u + ++tid_count;
    ts.sw.give = h; ts.sw.take = g; ts.sw.state = 1; ts.sw.back = 0;
    {
        Event e = ts_ev(TS_SWAP, ts.sid);
        e.u.ts.tid = ts.sw.tid;
        e.u.ts.cell = g_trade.sel[1]; e.u.ts.pcell = g_trade.sel[0];
        e.u.ts.give = h; e.u.ts.take = g;
        if (!ts_lose('s')) queue_event(&e);
    }
    g_trade.ready[0] = 0;
    g_trade.sel[0] = -1;   /* the offer went into the swap */
    ts_view();
    host_log("coop: trade screen: swap %08x sent: our %08x for the partner's %08x\n", ts.sw.tid, h, g);
}

/* host: the swap's answer applied (in game: no inventory screen open) */
static void host_swap_frame(void)
{
    if (!tx_in_game || ts.sw.state < 2) return;
    if (ts.sw.state == 3) {
        if (ts.sw.take && !inv_add(ts.sw.take)) { snprintf(g_coop.trade_msg, sizeof(g_coop.trade_msg), tr("Make room in your inventory to receive %s."), coop_item_name(ts.sw.take)); return; }
        ts_done_msg(1, 0, ts.sw.give, ts.sw.take);
        CallSystemSe(0, 3);
    } else if (ts.sw.give && !inv_add(ts.sw.give)) {
        snprintf(g_coop.trade_msg, sizeof(g_coop.trade_msg), tr("Make room in your inventory to reclaim %s."), coop_item_name(ts.sw.give));
        return;
    }
    count_keys(keys_had);
    ts.sw.state = 0;
    inv_log("swap done");
}

/* guest: the host's swap. Applied whole (our item out, the host's in) or not at all; the answer
   is kept (ledger) and given again when the host asks after a reconnect */
static void guest_swap(const Event* e)
{
    unsigned int b[12], mine;
    int y = ledger_find(e->u.ts.tid), why = WHY_OK, wm;
    Event d = ts_ev(TS_DONE, e->u.ts.sid);
    d.u.ts.tid = e->u.ts.tid;
    if (y >= 0) { d.u.ts.yes = y; queue_event(&d); return; }   /* asked twice */
    mine = cell_val(0, g_trade.sel[0]);
    wm = cell_word(0, g_trade.sel[0]);
    if (g_trade.phase != TRADE_OPEN || e->u.ts.sid != ts.sid || !g_trade.ready[0] || !tx_in_game) why = WHY_WAIT;
    else if (g_trade.sel[0] != e->u.ts.cell || mine != e->u.ts.take || g_trade.sel[1] != e->u.ts.pcell || cell_val(1, g_trade.sel[1]) != e->u.ts.give) why = WHY_CHANGED;
    else {
        memcpy(b, my_inv(), sizeof(b));
        if (mine && blk_take(b, inv_slots(), 2 + wm, 0, 1) != mine) why = WHY_EQUIPPED;
        else if (e->u.ts.give && !blk_add(b, inv_slots(), e->u.ts.give)) why = WHY_ROOM;
    }
    ledger_add(e->u.ts.tid, why == WHY_OK);
    d.u.ts.yes = why == WHY_OK;
    d.u.ts.why = why;
    if (!ts_lose('d')) queue_event(&d);
    g_trade.ready[0] = 0;
    if (why == WHY_OK) {
        g_trade.sel[0] = -1;   /* the offer went */
        memcpy(my_inv(), b, sizeof(b));
        count_keys(keys_had);
        ts_view();
        inv_log("swapped");
        CallSystemSe(0, 3);
    }
    host_log("coop: trade screen: swap %08x %s (why %d)\n", e->u.ts.tid, why == WHY_OK ? "made" : "refused", why);
    ts_done_msg(why == WHY_OK, why, e->u.ts.take, e->u.ts.give);
}

static int apply_tscreen(const Event* e)
{
    const char* nm = g_trade.name[0] ? g_trade.name : tr("The partner");
    char m[256];
    int y;
    if (!sys || !plp) return 1;
    switch (e->u.ts.op) {
    case TS_REQ:
        if (g_trade.phase == TRADE_ASKING && is_host) {   /* both asked at once: the host takes the guest's (the guest ignores ours) */
            ts_send(TS_OPEN, e->u.ts.sid);
            ts_open(e->u.ts.sid);
        } else if (g_trade.phase == TRADE_IDLE || g_trade.phase == TRADE_ASKED) {
            g_trade.phase = TRADE_ASKED;
            ts.sid = e->u.ts.sid;
            ts.t = net_ms();
            CallSystemSe(0, 3);
            host_log("coop: trade screen: %s asks to trade (%08x)\n", nm, e->u.ts.sid);
        }
        return 1;
    case TS_CANCEL:
        if (g_trade.phase == TRADE_ASKED && e->u.ts.sid == ts.sid) {
            g_trade.phase = TRADE_IDLE;
            snprintf(m, sizeof(m), tr("%s withdrew the trade request"), nm);
            ts_say(m);
        }
        return 1;
    case TS_OPEN:
        if (g_trade.phase == TRADE_ASKING && e->u.ts.sid == ts.sid) { ts_open(ts.sid); CallSystemSe(0, 3); }
        else if (!(g_trade.phase == TRADE_OPEN && e->u.ts.sid == ts.sid)) ts_send(TS_CLOSE, e->u.ts.sid);   /* we stopped asking */
        return 1;
    case TS_CLOSE:
        if (e->u.ts.sid != ts.sid) return 1;
        if (g_trade.phase == TRADE_OPEN) { snprintf(m, sizeof(m), tr("%s closed the trade"), nm); ts_close(0, m); CallSystemSe(0, 0); }
        else if (g_trade.phase == TRADE_ASKING) g_trade.phase = TRADE_IDLE;
        return 1;
    case TS_STATE: {
        int cell = e->u.ts.cell == TSC_NONE ? -1 : e->u.ts.cell;
        unsigned int was = cell_val(1, g_trade.sel[1]);
        if (g_trade.phase != TRADE_OPEN || e->u.ts.sid != ts.sid) return 1;
        g_trade.cells[1] = e->u.ts.slots;
        memcpy(g_trade.grid[1], e->u.ts.inv, sizeof(g_trade.grid[1]));
        g_trade.eq[1] = e->u.ts.eq == TSC_NONE ? -1 : e->u.ts.eq;
        if (cell >= g_trade.cells[1]) cell = -1;
        if (cell != g_trade.sel[1] || cell_val(1, cell) != was) g_trade.ready[0] = 0;   /* the pair changed */
        if (cell != g_trade.sel[1]) host_log("coop: trade screen: %s offers cell %d (%08x)\n", nm, cell, cell_val(1, cell));
        g_trade.sel[1] = cell;
        g_trade.ready[1] = e->u.ts.ready;
        ts.pcell = e->u.ts.pcell == TSC_NONE ? -1 : e->u.ts.pcell;
        ts.ptake = e->u.ts.take;
        if (g_trade.want[0] >= 0 && cell_val(1, g_trade.want[0]) != g_trade.want_w[0]) {   /* it moved or went */
            g_trade.want[0] = -1;
            host_log("coop: trade screen: our request ended (%s's item there changed)\n", nm);
        }
        if (g_trade.cur_side && g_trade.cur >= g_trade.cells[1]) g_trade.cur = g_trade.cells[1] - 1;
        return 1;
    }
    case TS_WANT:
        if (g_trade.phase == TRADE_OPEN && e->u.ts.sid == ts.sid) coop_trade_wanted(e->u.ts.cell == TSC_NONE ? -1 : e->u.ts.cell, e->u.ts.take);
        return 1;
    case TS_DECLINE:
        if (g_trade.phase == TRADE_OPEN && e->u.ts.sid == ts.sid && e->u.ts.cell == g_trade.want[0] && e->u.ts.take == g_trade.want_w[0]) {
            g_trade.want[0] = -1;
            snprintf(m, sizeof(m), tr("%s declined the request"), nm);
            ts_say(m);
            CallSystemSe(0, 0);
        }
        return 1;
    case TS_SWAP:
        if (!is_host) guest_swap(e);
        return 1;
    case TS_DONE:
        if (!is_host || ts.sw.state != 1 || ts.sw.tid != e->u.ts.tid) return 1;
        ts.sw.state = e->u.ts.yes ? 3 : ts.sw.back ? 0 : 2;
        if (e->u.ts.yes && ts.sw.back) escrow_taken(ts.sw.give);
        host_log("coop: trade screen: swap %08x %s by the partner (why %d)\n", ts.sw.tid, e->u.ts.yes ? "made" : "refused", e->u.ts.why);
        if (!e->u.ts.yes) ts_done_msg(0, e->u.ts.why, 0, 0);
        return 1;
    case TS_QUERY:
        if (is_host) return 1;
        if ((y = ledger_find(e->u.ts.tid)) < 0) ledger_add(e->u.ts.tid, y = 0);   /* never had it: refused from now on */
        {
            Event d = ts_ev(TS_DONE, e->u.ts.sid);
            d.u.ts.tid = e->u.ts.tid;
            d.u.ts.yes = y;
            d.u.ts.why = y ? WHY_OK : WHY_LOST;
            queue_event(&d);
        }
        return 1;
    }
    return 1;
}

/* a new session: the screen and requests end; a swap of ours on its way asks again */
static void ts_reset(void)
{
    if (ts.sw.state == 1) {
        Event e = ts_ev(TS_QUERY, ts.sid);
        e.u.ts.tid = ts.sw.tid;
        queue_event(&e);
        host_log("coop: trade screen: asking again about swap %08x\n", ts.sw.tid);
        inv_log("asking again");
    }
    ts_close(0, "Trade closed (the session restarted)");
    g_trade.phase = TRADE_IDLE;
}

/* An item of ours in escrow (an F3 offer or a trade screen swap without its answer, one refused or taken
   that waits for room) when the session ends: into our inventory, else the item box, at once - a save
   before a reconnect has it (before, it was held in memory only and went with the game). The question
   still goes out in the next session (QUERY); if it was taken after all, the copy comes out again
   (escrow_taken). Not while the item box screen is open: then at the next frame in game, as a refusal. */
static int escrow_put(unsigned int w)
{
    if (!w) return 1;
    if (inv_add(w)) { count_keys(keys_had); trade_msg("Session ended. %s returned to your inventory.", w); return 1; }
    if (box_put(w)) { box_pack(); trade_msg("Session ended. %s placed in your item box.", w); return 1; }
    trade_msg("No room to return %s. Free inventory or item box space; check F3.", w);
    return 0;
}
static void escrow_home(void)
{
    int n = 0;
    if (!sys || (sys->cb_flg & 0x40000)) return;
    if (t_out.state == 1 && !t_out.back) n += t_out.back = escrow_put(t_out.entry);
    if (t_out.state == 2 && escrow_put(t_out.entry)) { t_out.state = 0; n++; }
    if (ts.sw.state == 1 && !ts.sw.back) n += ts.sw.back = escrow_put(ts.sw.give);
    if (ts.sw.state == 2 && escrow_put(ts.sw.give)) { ts.sw.state = 0; n++; }
    if (ts.sw.state == 3 && escrow_put(ts.sw.take)) { ts.sw.state = 0; ts.sw.give = 0; n++; }
    if (n) inv_log("escrow home");
}
/* the answer came in a later session: taken after all - our copy (the inventory's, else the box's) out again */
static void escrow_taken(unsigned int w)
{
    if (!w || inv_remove(w) || box_take(w)) host_log("coop: trade: %08x was taken after all: our copy is gone again\n", w);
    else host_log("coop: trade: %08x was taken after all, our copy was used meanwhile: it stays\n", w);
}

/* "T / Left stick press": what presses binding row `row` (keyboard, gamepad), for hints */
static const char* binding_names(int row, char* out, int n)
{
    char k1[24], k2[24];
    int kb = bind_get(row, 0, 0), kp = bind_get(row, 1, 0);
    snprintf(out, n, "%s%s%s", kb ? bind_code_name(0, kb, k1, sizeof(k1)) : "", kb && kp ? " / " : "", kp ? bind_code_name(1, kp, k2, sizeof(k2)) : "");
    return out;
}

/* at the hand-off, after trade_frame */
static void ts_frame(void)
{
    unsigned int now = net_ms();
    int key = pad_trade_held(), press = key && !ts.key_was, me, partner, near;
    char m[256];
    static const char* env = (const char*)-1;
    ts.key_was = key;
    {   /* test aid RECVX_COOP_TSCREEN="hostframe:act,...": k the trade key, s<cell> select, r ready, l leave, d disconnect, i log the inventory,
           c<cell>/cp<cell> cursor (ours/partner's), a confirm on the cursor, b cancel, w<cell> request (w-1 withdraw), o offer the requested */
        unsigned int f;
        int n = 0;
        if (env == (const char*)-1) env = getenv("RECVX_COOP_TSCREEN");
        while (env && sscanf(env, "%u:%n", &f, &n) == 1 && n && test_clock() >= f) {
            const char* a = env + n;
            host_log("coop: test: trade screen action %.*s at host frame %u\n", (int)strcspn(a, ","), a, test_clock());
            if (*a == 'k') press = 1;
            else if (*a == 's') coop_trade_select(atoi(a + 1));
            else if (*a == 'c') coop_trade_cursor(a[1] == 'p', atoi(a + 1 + (a[1] == 'p')));   /* c<cell> / cp<cell>: the cursor on ours / the partner's */
            else if (*a == 'a') coop_trade_activate();
            else if (*a == 'b') coop_trade_back(1);
            else if (*a == 'o') coop_trade_offer_wanted();
            else if (*a == 'w') coop_trade_want(atoi(a + 1));
            else if (*a == 'P') trade_test_pad |= (unsigned int)strtoul(a + 1, NULL, 16);   /* P<hex>: pad presses through the screen's input */
            else if (*a == 'M') {   /* M<x>/<y>[l|r]: the mouse at game 2D x, y (with RECVX_MENU_MOUSE="": nothing else moves it), l/r click */
                char* t;
                int x = (int)strtol(a + 1, &t, 10), y = *t == '/' ? (int)strtol(t + 1, &t, 10) : 0;
                g_menu_mouse.x = x * 16; g_menu_mouse.y = y * 16;
                if (*t == 'l') __atomic_add_fetch(&g_menu_mouse.left, 1, __ATOMIC_RELAXED);
                if (*t == 'r') __atomic_add_fetch(&g_menu_mouse.right, 1, __ATOMIC_RELAXED);
            }
            else if (*a == 'x') ts_want_send(TS_WANT, atoi(a + 1), cell_val(1, atoi(a + 1)) + 1);   /* x<cell>: a request naming another item than the one there */
            else if (*a == 'r') coop_trade_ready();
            else if (*a == 'l') coop_trade_leave();
            else if (*a == 'd') __atomic_store_n(&g_coop.request, COOP_REQ_STOP, __ATOMIC_RELEASE);   /* F3's Disconnect */
            else if (*a == 'i') inv_log("test");
            env = strchr(a, ',') ? strchr(a, ',') + 1 : NULL;
        }
    }
    snprintf(g_trade.name, sizeof(g_trade.name), "%.15s", rx_ext.name[0] ? rx_ext.name : tr("Partner"));
    binding_names(B_TRADE, g_trade.key, sizeof(g_trade.key));
    host_swap_frame();
    me = tx_in_game && local_visible() && plp->mode0 == 1 && plp->mnwP == plp->mnwPb && plp->hp >= 0;
    if (is_host) { int i; for (i = 0; i < PC_ENE_SLOTS && me; i++) if (owned[i]) me = 0; }   /* the partner runs a grab of one of ours */
    partner = have_rx && same_room() && (rx.flags & (PF_IN_GAME | PF_VISIBLE)) == (PF_IN_GAME | PF_VISIBLE) &&
              (rx.busy == BUSY_NONE || rx.busy == BUSY_TRADE) && rx.mode0 == 1 && rx.mset == MSET_OWN;
    near = have_peer && have_rx && tx_in_game && trade_near();
    g_trade.hint[0] = 0;
    switch (g_trade.phase) {
    case TRADE_IDLE:
        if (!press || !tx_in_game) break;
        if (!have_peer || g_coop.state != COOP_CONNECTED) ts_say("No partner to trade with");
        else if (!near) { snprintf(m, sizeof(m), tr("Stand next to %s to trade"), g_trade.name); ts_say(m); }
        else if (!me || !partner) ts_say("Finish menus, scenes or enemy grabs before trading. Both players must be free.");
        else if (ts.sw.state) ts_say("The last trade is still completing.");
        else {
            ts.sid = (my_sid * 31u + ++tid_count) ^ (is_host ? 0 : 0x80000000u);   /* both sides may start with the same session id */
            ts.t = now;
            ts_send(TS_REQ, ts.sid);
            g_trade.phase = TRADE_ASKING;
            g_trade.msg_until = 0;
            CallSystemSe(0, 3);
            host_log("coop: trade screen: we ask to trade (%08x)\n", ts.sid);
        }
        break;
    case TRADE_ASKING:
        snprintf(g_trade.hint, sizeof(g_trade.hint), tr("Waiting for %s...  (%s: cancel)"), g_trade.name, g_trade.key);
        if (press) { ts_send(TS_CANCEL, ts.sid); g_trade.phase = TRADE_IDLE; CallSystemSe(0, 0); ts_say("Trade request withdrawn"); }
        else if (!near || !me) { ts_send(TS_CANCEL, ts.sid); g_trade.phase = TRADE_IDLE; ts_say("Too far apart: trade request withdrawn"); }
        else if (now - ts.t > TS_ASK_MS) { ts_send(TS_CANCEL, ts.sid); g_trade.phase = TRADE_IDLE; snprintf(m, sizeof(m), tr("%s did not answer"), g_trade.name); ts_say(m); }
        break;
    case TRADE_ASKED:
        snprintf(g_trade.hint, sizeof(g_trade.hint), tr("%s wants to trade - press %s"), g_trade.name, g_trade.key);
        if (now - ts.t > TS_ASKED_MS || !have_rx || !same_room()) g_trade.phase = TRADE_IDLE;
        else if (press) {
            if (!me) ts_say("Finish the menu, scene or enemy grab before trading.");
            else if (!near) { snprintf(m, sizeof(m), tr("Stand next to %s to trade"), g_trade.name); ts_say(m); }
            else { ts_send(TS_OPEN, ts.sid); ts_open(ts.sid); CallSystemSe(0, 3); }
        }
        break;
    case TRADE_OPEN:
        ts_view();
        if (!have_rx || !have_peer) ts_close(0, "Partner disconnected. Trade closed.");
        else if (!near) ts_close(1, "Too far apart. Trade closed.");
        else if (!tx_in_game || plp->hp < 0 || plp->mode0 != 1) ts_close(1, "Trade closed");
        else if (rx.busy != BUSY_TRADE && now - ts.t > 3000) { snprintf(m, sizeof(m), tr("%s stopped trading. Trade closed."), g_trade.name); ts_close(1, m); }
        else {
            if (g_trade.want[1] >= 0 && cell_val(0, g_trade.want[1]) != g_trade.want_w[1]) {   /* the item moved or went */
                g_trade.want[1] = -1;
                host_log("coop: trade screen: %s's request ended (our item there changed)\n", g_trade.name);
            }
            if (pair_why() != WHY_OK) g_trade.ready[0] = 0;
            ts_state(0);
            if (is_host) host_commit();
        }
        break;
    }
    g_trade.waiting = ts.sw.state != 0;
}

/* ---------------------------------------------------------------- items handed over by events (protocol 13, EV_GRANT) */
/* Some events end by giving the player an item without asking: the script sets sb_id and cb_flg
   0x10 | 0x4000 and the status screen adds it (sub1.c GetItem) - the courtyard handgun (0/3), the
   lighter (7/23), story keys (docs/COOP.md "Items from events"). Such an event runs for the player who
   triggered it and for a partner who replayed it in the same room (M8); a partner elsewhere gets its
   flags only, and the event never runs for it again. So GetItem reports the add (coop_item_granted) and,
   once we are back in control, it goes to the partner as EV_GRANT: item, count, room, where we stand.
   The partner keeps it pending in its own save region (sys->itm words GRANT0.., which no game code uses:
   saved with the game, kept by own_restore when the guest joins) and shows it in that room as a floor
   item: the room's own item record of that id (its model; the event used it as its prop), put where we
   stood, with a pickup trigger of the game's own kind (sys->metcp, the movable objects' extra
   triggers), so taking it is the game's pickup (crouch, "Will you take it?", full inventory: it stays).
   Nothing is kept when the partner got that item from the same event in that room in this game (it watched
   it), or has it already (unless it stacks). Which items go to the partner (grant_for_partner): individual loot
   weapons (with the lighter) and key items (the partner cannot run the event); shared loot (one pool) key items and
   the lighter, weapons and ink ribbons only with "important items for both". The others stay the triggering
   player's: with shared loot a partner that replayed the event here and got one too gives it back (give_back). */
#define GRANT_NOWHERE 63                /* floor of a grant whose spot is unknown (the giver was hidden in its scene) */
#define GRANT_GLINT 100                 /* the glint of floor loot (item record aspd: its light 0.1 * aspd; most pickups have 100) */
static struct { int item, count, room, flr; float p[3]; } gout[4];   /* ours, to send when we are in control */
static int gout_n;
static struct { int room, flr; float p[3]; } gseen_spot = { -1 };   /* where we last stood in control (an event may hide us) */
static struct { unsigned char item; short room; unsigned int tick; } gseen[8];   /* our own event items (a partner's echo) */
static unsigned int gseen_n;
static int gk = -1, gslot, getc_ix, gtries = -1, groom = -1, ghave;  /* the entry shown here: its item slot, trigger */

static unsigned int* gw(int i) { return &sys->itm[GRANT0 + i * 5]; }
static int g_item(int i) { return (gw(i)[0] >> 16) & 0xFF; }
#define GRANT_RET 0x1000000u            /* gw()[2]: a box item of ours the host had no room for (bits 16..23: its word's top byte) */
static int g_ret(int i) { return (gw(i)[2] & GRANT_RET) != 0; }
static unsigned int g_word(int i) { return (gw(i)[2] >> 16 & 0xFF) << 24 | (gw(i)[0] & 0xFFFFFF); }   /* (a returned one's whole word) */
static unsigned int gsnap[12];          /* our inventory before the game's pickup of the entry shown */
static float g_pos(int i, int k) { return (short)(gw(i)[2 + k] & 0xFFFF) / 100.0f; }
static int stackable(int id) { return (itemdata[id].type & 0x200) != 0; }
static int story_item(int id) { return (itemdata[id].type & 0x41) != 0; }   /* weapons (and the lighter), key items */
static int grant_for_partner(int id) { return individual() ? story_item(id) : for_both(id); }

/* what we hold of the item (the STANDARD slot too: the lighter): its amount if it stacks, else its slots */
static int item_amount(int id)
{
    unsigned int* inv = my_inv();
    int i, n = 0;
    for (i = 1; i < 12; i++)
        if (((inv[i] >> 16) & 0xFF) == (unsigned)id) n += stackable(id) ? (int)(inv[i] & 0xFFFF) : 1;
    return n;
}
static int have_item(int id)
{
    int i;
    if (item_amount(id)) return 1;
    for (i = 64; i < 224; i++)   /* the item box */
        if (((sys->itm[i] >> 16) & 0xFF) == (unsigned)id) return 1;
    return 0;
}

static void grant_drop(int i, const char* why)
{
    host_log("coop: the partner's %s from room %d/%d: %s\n", item_name((unsigned)g_item(i) << 16 | (gw(i)[0] & 0xFFFF)),
             gw(i)[1] >> 8, gw(i)[1] & 0xFF, why);
    memset(gw(i), 0, 5 * sizeof(unsigned int));
    if (i == gk) gk = -1;
}

/* our event items and the ones still to send are this game's: an earlier game's (a session before, a game over
   that went back before the event) made a later gift of the same item there count as one we got ourselves */
static void grant_forget(void)
{
    memset(gseen, 0, sizeof(gseen));
    gout_n = 0;
}

static int watched(int item, int room)
{
    int i;
    for (i = 0; i < 8; i++)
        if (gseen[i].item == item && gseen[i].room == room) return 1;
    return 0;
}

/* an event here gave us `item` just now (its script then hides the prop and sets the item flag: a pickup to
   diff_flags, which must not hand the partner the item itself - EV_GRANT puts it on the partner's floor) */
static int event_gave(int item)
{
    int i, room = coop_room_key();
    for (i = 0; i < 8; i++)
        if (gseen[i].item == item && gseen[i].room == room && tick - gseen[i].tick < 300) return 1;
    return 0;
}

/* sub1.c GetItem: an event just gave us `item` without asking */
void coop_item_granted(int item)
{
    int room, i, n = gout_n;
    if (!sys || !plp || item <= 0 || item >= 156) return;
    room = coop_room_key();
    gseen[gseen_n & 7].item = item;
    gseen[gseen_n & 7].tick = tick;
    gseen[gseen_n++ & 7].room = room;
    for (i = 0; i < GRANT_N; i++)   /* the partner's copy of it waits here: we got it ourselves now */
        if (g_item(i) == item && (int)gw(i)[1] == room && !g_ret(i)) grant_drop(i, "we got it from the event ourselves");
    if (!grant_for_partner(item)) {
        if (!individual() && win_rx.open && !win_tx.open) {   /* the partner's event, replayed here: its item */
            give_back(item, BACK_EVENT);
            return;
        }
        host_log("coop: an event gave us %s (%s loot: ours alone)\n", item_name(item << 16), individual() ? "individual" : "shared");
        return;
    }
    if (n == 4) return;
    gout[n].item = item;
    gout[n].count = 0;
    for (i = 1; i < 12; i++)
        if (((my_inv()[i] >> 16) & 0xFF) == (unsigned)item) gout[n].count = my_inv()[i] & 0xFFFF;
    gout[n].room = room;
    gout[n].flr = plp->flr_no;
    gout[n].p[0] = plp->px; gout[n].p[1] = plp->py; gout[n].p[2] = plp->pz;
    if ((plp->stflg & 0x1000000) || (plp->px == 0 && plp->pz == 0)) {   /* hidden in its scene: where we last stood here */
        gout[n].flr = gseen_spot.room == room ? gseen_spot.flr : GRANT_NOWHERE;
        memcpy(gout[n].p, gseen_spot.p, sizeof(gout[n].p));
    }
    host_log("coop: an event gave us %s\n", item_name((unsigned)item << 16 | gout[n].count));
    gout_n++;
}

/* apply_box, box_back: our own item put down where we stand, in a free pending entry; the pickup gives back the
   word itself (count, ammo kind), not what a pickup of that item adds (grant_frame). 0: all entries in use. */
static int grant_return(unsigned int w)
{
    int i;
    for (i = 0; i < GRANT_N && g_item(i); i++) ;
    if (i == GRANT_N || !plp) return 0;
    gw(i)[0] = (unsigned)(plp->flr_no & 0x3F) << 24 | (w & 0xFFFFFF);
    gw(i)[1] = coop_room_key();
    gw(i)[2] = (unsigned short)(short)lrintf(plp->px * 100) | (w >> 24) << 16 | GRANT_RET;
    gw(i)[3] = (unsigned short)(short)lrintf(plp->py * 100);
    gw(i)[4] = (unsigned short)(short)lrintf(plp->pz * 100);
    return 1;
}

static int apply_grant(const Event* e)
{
    int room = e->stg * 256 + e->rom, item = e->u.gt.item, i, k;
    unsigned int w = (unsigned)item << 16 | e->u.gt.count;
    if (!sys || !plp || !tx_in_game || !in_session_game() || (!is_host && (need_blob || pop_pending || popping))) return 0;   /* our own game first */
    if (!grant_for_partner(item)) { host_log("coop: the partner got %s from an event: %s loot, the partner's alone\n", item_name(w), individual() ? "individual" : "shared"); return 1; }
    if (watched(item, room)) { host_log("coop: the partner got %s in room %d/%d too: we were in that event\n", item_name(w), e->stg, e->rom); return 1; }
    if (!stackable(item) && have_item(item)) { host_log("coop: the partner got %s from an event: we have one\n", item_name(w)); return 1; }
    for (i = 0; i < GRANT_N && g_item(i); i++) ;
    if (i == GRANT_N) { host_log("coop: no room to keep the partner's %s\n", item_name(w)); return 1; }
    gw(i)[0] = (unsigned)(e->u.gt.flr_no & 0x3F) << 24 | (unsigned)item << 16 | e->u.gt.count;
    gw(i)[1] = room;
    for (k = 0; k < 3; k++) gw(i)[2 + k] = (unsigned short)(short)lrintf(e->u.gt.p[k] * 100);
    host_log("coop: the partner got %s from an event in room %d/%d: it waits there for us\n", item_name(w), e->stg, e->rom);
    return 1;
}

/* the room's item record (slot) that shows the partner's item: one of that id the room hides, else one
   it shows only as a prop (no pickup trigger of its own, e.g. in a display case); -1 none, -2 the room's
   own pickup of it is there */
static int grant_slot(int item)
{
    int k, j, prop = -1, any = 0;
    for (k = 0; k < rom->itm_n && k < ITWP_N; k++) {
        const O_WRK* op = &sys->itwp[k];
        if (pc_pickup_item(op->type, op->id) != item || !(op->flg & 0x1)) continue;   /* the item its pickup gives (BioRand: type) */
        any = 1;
        if (op->stflg & 0x1000000) return k;
        for (j = 0; j < rom->etc_n; j++) {
            const ATR_WORK* t = &rom->etcp[j];
            if ((t->flg & 0x1) && t->type == 4 && !(t->attr & 0x2) && t->prm0 == k) break;
        }
        if (j == rom->etc_n && prop < 0) prop = k;
    }
    return prop >= 0 ? prop : any ? -2 : -1;
}

/* A room with no item record of the partner's item gets one: the item's display model, the one the status
   screen shows for it (ITEM AFS file display id - 1, as sub1.c picks and itemview.c realizes it; the same
   mesh as the rooms' floor records of that item), realized into the next free item slot the way the room
   loader realizes a record (bhSetItmMdl, its texture uploaded and released with the room's items,
   bhReleaseMainTexture), lying flat where the partner stood. The slot is the room's for this visit (rom->itm_n
   grows by one; the next room load starts from the file again). The item slot, or -1. */
static unsigned char gmodel[512 << 10] __attribute__((aligned(64)));   /* the models of this room visit */
static int gmodel_n, gmodel_gen = -1;

static int grant_model(int item, int i)
{
    int disp = itemidtbl[itemflg[dsptbl[item].hide][6]], slot = rom->itm_n, entries, heap, dt, at, k;
    unsigned char *p, *mlb;
    ETTY_WORK rec;
    O_WRK* op;
    ADXF h;
    if (gmodel_gen != enemy_room_gen) { gmodel_gen = enemy_room_gen; gmodel_n = 0; }
    if (!disp) disp = item;
    if (disp == 131) disp = 5;    /* sub1.c: the Enhanced Handgun shows the handgun's */
    if (disp == 136) disp = 33;
    for (k = 0; k < rom->itm_n && k < ITWP_N && !g_ret(i); k++)   /* (a returned item of ours shows beside the room's own) */
        if (pc_pickup_item(sys->itwp[k].type, sys->itwp[k].id) == item) return -1;   /* (one of ours: grant_slot found it, or not to be shown) */
    if (slot >= ITWP_N || slot >= 32) { host_log("coop: no free item slot here for the partner's %s\n", item_name((unsigned)item << 16)); return -1; }
    actor_tex_free(&entries, &heap);
    if (entries < 16 || heap < (1 << 20) || sys->endp - sys->memp < (256 << 10)) {
        host_log("coop: no texture or room memory here for the partner's %s\n", item_name((unsigned)item << 16));
        return -1;
    }
    if (!(h = ADXF_OpenAfs(sys->itm_partid, disp - 1))) return -1;
    at = (gmodel_n + 63) & ~63;
    k = ADXF_GetFsizeSct(h) * 2048 <= (int)sizeof(gmodel) - at && ADXF_GetFsizeByte(h) > 16;
    if (k) {
        ADXF_ReadNw32(h, ADXF_GetFsizeSct(h), gmodel + at);   /* synchronous on the PC */
        k = ADXF_GetStat(h) == 3;
    }
    dt = k ? *(int*)(gmodel + at) : 0;
    if (!k || dt <= 0 || dt > ADXF_GetFsizeByte(h) - 8 || memcmp(gmodel + at + 4, "MDL", 3)) {
        ADXF_Close(h);
        host_log("coop: no display model %d for the partner's %s\n", disp, item_name((unsigned)item << 16));
        return -1;
    }
    gmodel_n = at + ADXF_GetFsizeByte(h);
    ADXF_Close(h);
    mlb = gmodel + at + 4;
    p = mlb + dt;   /* the texture pack follows (sys->sb_ppp in system.c's reader) */
    p = (*(int*)p & 0x80000000) ? (unsigned char*)(((size_t)p + 4 + 31) & ~(size_t)31) : p + 4;
    memset(&rec, 0, sizeof(rec));
    rec.flg = 0x1;
    rec.id = item;
    rec.flr_no = (char)(gw(i)[0] >> 24);
    rec.px = g_pos(i, 0); rec.py = g_pos(i, 1); rec.pz = g_pos(i, 2);
    memset(rec.hide, -1, sizeof(rec.hide));
    bhSetItmMdl(mlb, &rec, item, slot);
    op = &sys->itwp[slot];
    if (op->mlwP->texP) {
        bhSetMemPvpTexture(op->mlwP->texP, p, 0);
        op->mlwP->flg |= 0x200;   /* its own: released with the room's (bhReleaseMainTexture) */
    }
    sys->memp = bhKeepObjWork(op->mlwP, sys->memp);
    op->stflg |= 0x1000000;       /* shown by grant_frame */
    rom->itm_n++;
    host_log("coop: no %s in this room: its display model %d loaded into item slot %d\n", item_name((unsigned)item << 16), disp, slot);
    return slot;
}

/* every frame in a session: ours out; a pending one in this room shown, taken */
static void grant_frame(void)
{
    int room = coop_room_key(), i, k;
    O_WRK* op;
    ATR_WORK* t;
    if (tx_in_game && local_visible() && !(plp->stflg & 0x1000000)) {
        gseen_spot.room = room; gseen_spot.flr = plp->flr_no;
        gseen_spot.p[0] = plp->px; gseen_spot.p[1] = plp->py; gseen_spot.p[2] = plp->pz;
    }
    if (gout_n && tx_in_game && local_visible()) {   /* back in control: where the event left us */
        for (i = 0; i < gout_n && have_peer && !echo_delay; i++) {
            Event e = { 0 };
            e.type = EV_GRANT;
            e.u.gt.item = gout[i].item;
            e.u.gt.count = gout[i].count;
            if (gout[i].room == room) { gout[i].flr = plp->flr_no; gout[i].p[0] = plp->px; gout[i].p[1] = plp->py; gout[i].p[2] = plp->pz; }
            e.u.gt.flr_no = gout[i].flr & 0x3F;
            for (k = 0; k < 3; k++) e.u.gt.p[k] = gout[i].p[k] < -320 ? -320 : gout[i].p[k] > 320 ? 320 : gout[i].p[k];
            queue_event(&e);   /* it puts our room in: the event's goes over it */
            if (!ring.overflow) { ring.ev[e.id % COOP_RING].stg = gout[i].room >> 8; ring.ev[e.id % COOP_RING].rom = gout[i].room & 0xFF; }
            host_log("coop: the partner gets %s too (room %d/%d at %.1f %.1f)\n", item_name((unsigned)e.u.gt.item << 16 | e.u.gt.count),
                     gout[i].room >> 8, gout[i].room & 0xFF, e.u.gt.p[0], e.u.gt.p[2]);
        }
        gout_n = 0;
    }
    for (; back_n && tx_in_game && local_visible(); back_n--) {   /* shared loot: pool items that are the partner's (raced) */
        unsigned int w = (unsigned)back[back_n - 1].item << 16 | getbulletmax[back[back_n - 1].item][sys->gm_mode & 3];   /* a pickup's amount */
        if (!inv_remove(w)) host_log("coop: %s is the partner's, but no longer here to give back\n", item_name(w));
        else trade_msg(back[back_n - 1].why == BACK_EVENT ? "%s belongs to your partner (shared loot)." : "Your partner took %s first.", w);
    }
    if (!tx_in_game || !rom || !sys->itwp || (!is_host && (need_blob || pop_pending || popping))) return;
    {   /* test aid: RECVX_COOP_GRANT="item:stage:room[:floor:x:y:z][,...]": as if the partner got these from events there
           (no spot given: unknown, it lies where we stand when we come in) */
        static const char* tg = (const char*)-1;
        Event e = { 0 };
        int it, st, rm, n, m, f;
        if (tg == (const char*)-1) tg = getenv("RECVX_COOP_GRANT");
        while (tg && sscanf(tg, "%d:%d:%d%n", &it, &st, &rm, &n) == 3) {
            e.type = EV_GRANT; e.stg = st; e.rom = rm; e.u.gt.item = it; e.u.gt.flr_no = GRANT_NOWHERE;
            if (sscanf(tg + n, ":%d:%f:%f:%f%n", &f, &e.u.gt.p[0], &e.u.gt.p[1], &e.u.gt.p[2], &m) == 4) { e.u.gt.flr_no = f; n += m; }
            if (coop_check_event(&e, 1) == COOP_OK) apply_grant(&e);
            tg = tg[n] == ',' ? tg + n + 1 : NULL;
        }
    }
    if (room != groom) { groom = room; gk = -1; gtries = -1; }
    if (gk >= 0) {
        op = &sys->itwp[gslot];
        t = &sys->metcp[getc_ix];
        k = item_amount(g_item(gk));
        if (k > ghave && sys->etc_idx == (unsigned)(rom->etc_n + getc_ix) && g_ret(gk)) {   /* our own item back: */
            memcpy(my_inv(), gsnap, sizeof(gsnap));   /* not what the game's pickup adds, the word itself */
            if (!inv_add(g_word(gk))) {   /* (a whole stack may need a cell the pickup did not): it stays */
                trade_msg("No room for %s.", g_word(gk));
                sys->cb_flg &= ~0x800;
                k = ghave;
            }
        }
        if (k > ghave && sys->etc_idx == (unsigned)(rom->etc_n + getc_ix)) {   /* the game's pickup took it */
            op->stflg |= 0x1000000;
            t->flg &= ~0x1;
            sys->cb_flg &= ~0x800;   /* "taken": no ItmCk of the room's clears it for this trigger */
            grant_drop(gk, k == 1 ? "taken (we have it now)" : "taken");
            return;
        }
        ghave = k;
        memcpy(gsnap, my_inv(), sizeof(gsnap));
        if (getc_ix >= sys->metc_n || t->type != 4 || t->prm0 != gslot) gk = -1;   /* the room was set up again */
    }
    if ((sys->cb_flg & 0x5) || !local_visible()) return;   /* not in an event, a cutscene, a door */
    if (gk < 0) {
        for (i = 0; i < GRANT_N; i++) {
            int item = g_item(i);
            if (!item || (int)gw(i)[1] != room) continue;
            if (!stackable(item) && !g_ret(i) && have_item(item)) { grant_drop(i, "we have one now"); continue; }
            if ((gw(i)[0] >> 24) == GRANT_NOWHERE) {   /* no spot known (the partner never stood in this room): at our feet */
                gw(i)[0] = (gw(i)[0] & 0xFFFFFF) | (unsigned)(plp->flr_no & 0x3F) << 24;
                gw(i)[2] = (gw(i)[2] & 0xFFFF0000) | (unsigned short)(short)lrintf(plp->px * 100);
                gw(i)[3] = (unsigned short)(short)lrintf(plp->py * 100);
                gw(i)[4] = (unsigned short)(short)lrintf(plp->pz * 100);
            }
            k = grant_slot(item);
            if (k == -1 || (k == -2 && g_ret(i))) {   /* this room has no record of it (3/10's Gold Lugers, 7/4's Enhanced Handgun): its own model */
                if (gtries == i) continue;
                gtries = i;
                if ((k = grant_model(item, i)) < 0) continue;   /* (tried again in the next visit) */
            }
            if (k < 0 || sys->metc_n >= 64) continue;   /* the room's own pickup of it is there (taken as usual) */
            gk = i; gslot = k; getc_ix = sys->metc_n++;
            ghave = item_amount(item);
            memcpy(gsnap, my_inv(), sizeof(gsnap));
            host_log("coop: the partner's %s lies here at %.1f %.1f (item slot %d, trigger %d)\n", item_name((unsigned)item << 16 | (gw(i)[0] & 0xFFFF)),
                     g_pos(i, 0), g_pos(i, 2), k, rom->etc_n + getc_ix);
            break;
        }
        if (gk < 0) return;
    }
    {   /* test aid: RECVX_COOP_TAKE=n: n frames after it was shown we stand 8 units from it, facing it (frame
           dumps), n frames later on it (cross takes it) */
        static const char* tk = (const char*)-1;
        static int shown_gk = -1, n;
        if (tk == (const char*)-1) tk = getenv("RECVX_COOP_TAKE");
        if (gk != shown_gk) { shown_gk = gk; n = 0; }
        if (tk && n == atoi(tk)) { move_player(g_pos(gk, 0), g_pos(gk, 2) + 8); plp->ay = 0; }
        if (tk && n++ == 2 * atoi(tk)) move_player(g_pos(gk, 0), g_pos(gk, 2));
    }
    /* where the partner stood, takeable; kept so every frame (a room script may hide its prop again) */
    op = &sys->itwp[gslot];
    op->px = g_pos(gk, 0); op->py = g_pos(gk, 1); op->pz = g_pos(gk, 2);
    op->flr_no = gw(gk)[0] >> 24;
    op->flg = (op->flg & ~(0x40000000 | 0x80)) | 0x1;   /* in every camera cut; not linked (bhCalcModel reads lkwkp) */
    op->stflg &= ~0x1000000;
    op->lkwkp = NULL;                          /* not in a cutscene actor's hand (0/0's lockpick record is linked) */
    if (!op->aspd) op->aspd = GRANT_GLINT;     /* it glints as floor loot does (objitm.c; an event's prop has none) */
    t = &sys->metcp[getc_ix];
    memset(t, 0, sizeof(*t));
    t->flg = 0x1;
    t->type = 4;
    t->attr = 0x1;                             /* crouch to pick it up */
    t->flr_no = op->flr_no;
    t->px = op->px - 3; t->pz = op->pz - 3; t->py = op->py;
    t->w = t->d = 6;
    t->prm0 = gslot;
}

/* ---------------------------------------------------------------- synchronized cutscene skip (protocol 19, OPT_SKIP_SYNC)
   docs/COOP.md "Cutscenes". Each game plays a cutscene of the room itself (the triggering player's script, the
   partner's replay of its world event), and its skip button jumps that script to its end, which places the player
   where the scene leaves it. Skipped on one side only, that player walked off while the partner still watched.
   Now, while the partner plays a cutscene in our room too, the skip button (pad.c, coop_skip_pad) does not skip:
   it asks (PF_SKIP in our pose), both see who waits (coop_skip_hint), and once both asked the host skips its
   own and sends EV_SKIP; the guest skips on it. Each script then ends the scene as a skip does in single player,
   on both sides at the same time. A request ends with the cutscene; a partner who is not (or no longer) in a
   cutscene in our room does not hold a skip up. */
char coop_skip_hint[96];
static int skip_want;                   /* we pressed skip in this cutscene: waiting for the partner */
static unsigned int pad_test;           /* test aid (RECVX_COOP_PAD, coop_testaids.c): presses for the next pad read */
#define PAD_SKIP 0x10000                /* the game's skip button (pad.c pad_type entry 16, system.c bhSysCallGame) */
extern unsigned int* pad_type[4];       /* pad.c: the key types' button tables */

/* the skip button would skip the cutscene we are in now (bhSysCallGame's test) */
static int cine_now(void)
{
    return sys && (sys->tk_flg & 0x80) && (sys->cb_flg & 0x4) && !(sys->cb_flg & 0x10000000) && !(sys->st_flg & 0x1C040008) &&
           !(sys->ss_flg & 0x80000000);
}
/* the partner plays a cutscene in our room too: a skip waits for both */
static int skip_together(void)
{
    return coop_on && have_peer && !echo_delay && rule(OPT_SKIP_SYNC) && have_rx && (rx.flags & PF_CINE) && partner_here();
}
static void skip_now(const char* why)
{
    sys->cb_flg |= 0x10000000;   /* as the skip button (bhSysCallGame) */
    skip_want = 0;
    host_log("coop: cutscene skipped in %d/%d (%s)\n", sys->stg_no, sys->rom_no, why);
}

/* ---- one player at a trigger at a time
   A puzzle runs on the game of the player who examined it: its prompts, its steps in room flags, its
   screen. Two players in the same one at once each solved or failed it on their own state, and one
   flag of the other's answer ended the other's steps halfway (a panel or keypad left open with nothing
   to answer). So the trigger a player examined is sent in its pose (Pose.use) for as long as that player
   is held by it (a message, a scene, a terminal), and the partner in the same room cannot examine the
   same trigger meanwhile: "<name> is using this". */
static int use_etc = -1, use_free, use_key = -1;   /* the trigger we examined; frames we have been free since */
/* A lever the partner moved stays as it left it: the trigger the partner used last in our room (p_use, seen in
   its pose until p_use_at) is locked for us for the rest of our visit of the room (used_etc; used_gen: the room
   load) when, within USED_WIN frames, the partner changes a world flag that our room script tests in that
   trigger's own block: "<name> already used this". The submarine's lever (1/14, 4/5) takes the player who pulls
   it through a door into the moved submarine, where the toggle of ev_flg 19 follows; the partner in the
   submarine got the flag and the lever's motion, but could pull it again at once, which moved the submarine
   back for both (and took that player alone to the other dock). The airport hangar's bridge switch (4/1, ev_flg
   35) toggles the same way. A trigger the flag closes (a solved puzzle, a one-way switch) is closed anyway. */
#define USED_WIN 600
static int p_use = -1, p_use_key = -1, used_etc = -1, used_gen = -1;
static unsigned int p_use_at;
/* the room's scd1 has a block taken at trigger `etc` (its Ck "player is at trigger") that tests flag set/bit */
static int scd1_tests_at(int etc, int set, int bit)
{
    const unsigned char* d = (const unsigned char*)rom->evtp;
    const unsigned int* tab = (const unsigned int*)rom->evtp;
    unsigned int first = tab[0], o, p, end;
    int k, n, m;
    for (k = 0; k * 4 < (int)first && k < 1024; k++)
        if (tab[k] && tab[k] < first) first = tab[k];
    end = code_end(tab, first / 4, tab[1]);
    for (o = tab[1]; o < end && (n = insn_len(d + o)) > 0 && d[o] != 0; o += n) {
        Blk b;
        int at = 0;
        if (d[o] != 1) continue;
        open_block(d, o, &b);
        for (p = o + 2; p < b.then0 && (m = insn_len(d + p)) > 0; p += m)
            if (d[p] == 4 && d[p + 1] == 10 && (u16at(d + p + 2) & 0x3FF) == 23 && d[p + 4] == etc) at = 1;
        if (!at || b.end > end) continue;
        for (p = o; p < b.end && (m = insn_len(d + p)) > 0; p += m)
            if (d[p] == 4 && d[p + 1] == set && (u16at(d + p + 2) & 0x3FF) == bit) return 1;
    }
    return 0;
}
/* world flags the partner changed in our room (its flags, its block replayed here): ev_flg / ky_flg word w */
static void used_mark(int w, unsigned int bits)
{
    int b, set = w < 32 ? 1 : 2, key;
    if (!bits || !sys || !rom || !rom->evtp) return;
    key = coop_room_key() * 16 + sys->rcase;
    if (p_use < 0 || p_use_key != key || frame_no - p_use_at > USED_WIN || (used_etc == p_use && used_gen == enemy_room_gen)) return;
    for (b = 0; b < 32; b++)
        if ((bits << b & 0x80000000u) && scd1_tests_at(p_use, set, (w - world_off(set)) * 32 + b)) {
            used_etc = p_use;
            used_gen = enemy_room_gen;
            host_log("coop: the partner's trigger %d in %d/%d changed flag %d/%d: used, not ours until we leave\n", p_use, sys->stg_no, sys->rom_no, set, (w - world_off(set)) * 32 + b);
            return;
        }
}
/* hitchk.c bhCheckExmAtari: we examine trigger `etc` (a puzzle, a message, a terminal); 1: not now */
int coop_examine(int etc)
{
    if (!sys || etc < 0 || etc > 254) return 0;
    if (have_rx && !echo_delay && partner_here() && rx.use == etc + 1) {
        snprintf(g_trade.msg, sizeof(g_trade.msg), tr("%s is using this"), g_trade.name[0] ? g_trade.name : tr("Your partner"));
        g_trade.msg_until = net_ms() + 3000;
        host_log("coop: trigger %d in %d/%d: the partner is using it\n", etc, sys->stg_no, sys->rom_no);
        return 1;
    }
    if (etc == used_etc && used_gen == enemy_room_gen) {
        snprintf(g_trade.msg, sizeof(g_trade.msg), tr("%s already used this"), g_trade.name[0] ? g_trade.name : tr("Your partner"));
        g_trade.msg_until = net_ms() + 3000;
        host_log("coop: trigger %d in %d/%d: the partner used it\n", etc, sys->stg_no, sys->rom_no);
        return 1;
    }
    use_etc = etc;
    use_free = 0;
    use_key = coop_room_key() * 16 + sys->rcase;
    return 0;
}
/* every frame before our pose goes out: we use the trigger while the game holds our player (st_flg 4: a
   message, a close-up, cinema) or we are out of the game screen (a terminal); free for 10 frames, done */
static void use_frame(void)
{
    if (use_etc >= 0 && (!in_session_game() || (sys->tk_flg & 0x28) || use_key != coop_room_key() * 16 + sys->rcase)) use_etc = -1;
    if (use_etc >= 0 && tx_in_game && !(sys->st_flg & 0x4) && !(sys->cb_flg & 0x4) && ++use_free > 10) use_etc = -1;
    else if (use_etc >= 0 && (!tx_in_game || (sys->st_flg & 0x4) || (sys->cb_flg & 0x4))) use_free = 0;
    tx.use = use_etc >= 0 ? (uint8_t)(use_etc + 1) : 0;
    if (have_rx && !echo_delay && rx.use && partner_here()) {   /* the partner's trigger, for used_mark */
        p_use = rx.use - 1;
        p_use_key = coop_room_key() * 16 + sys->rcase;
        p_use_at = frame_no;
    }
}
/* event.c bhEvtComSet: an event of the partner's world event, replayed here, opens no terminal (1/09: the
   office computer's power-on event ends in the password screen; replayed, it put the partner into it) */
int coop_replayed_event(void)
{
    int l = bhCetask ? (int)(bhCetask - bhEtask) : -1;
    if (!win_rx.open || l < 0 || l >= 16 || !(win_rx.levels >> l & 1)) return 0;
    host_log("coop: the partner's replayed event (task level %d) opens no terminal here\n", l);
    return 1;
}

/* pad.c bhSetPad (via trade_pad): the skip button while the partner plays a cutscene with us asks instead */
unsigned int coop_skip_pad(unsigned int pad)
{
    static unsigned int prev;
    int press;
    pad |= pad_test;
    pad_test = 0;
    press = (pad & ~prev & PAD_SKIP) != 0;
    prev = pad;
    if (!coop_on || !cine_now() || !skip_together()) return pad;
    if (press && !skip_want) {
        skip_want = 1;
        CallSystemSe(0, 3);
        host_log("coop: we want to skip the cutscene in %d/%d: waiting for the partner\n", sys->stg_no, sys->rom_no);
    }
    return pad & ~PAD_SKIP;
}

/* every frame in a session, before our pose goes out */
static void skip_frame(void)
{
    static int partner_asked;
    int cine = cine_now(), together = cine && skip_together(), asked = together && (rx.flags & PF_SKIP);
    char key[48];
    if (!cine) {
        if (skip_want) host_log("coop: the cutscene ended: our skip request is over\n");
        skip_want = 0;
    } else if (skip_want && !together) skip_now("the partner is not in it (any more)");
    else if (skip_want && asked && is_host) {
        Event e = { 0 };
        e.type = EV_SKIP;
        e.u.sk.op = SKIP_GO;
        queue_event(&e);
        skip_now("both asked: the partner skips with us");
    }
    if (asked && !partner_asked && !skip_want) CallSystemSe(0, 2);   /* the partner asks us */
    partner_asked = asked;
    tx.flags = (tx.flags & ~(PF_CINE | PF_SKIP)) | (cine ? PF_CINE : 0) | (cine && skip_want ? PF_SKIP : 0);
    coop_skip_hint[0] = 0;
    if (together && skip_want) snprintf(coop_skip_hint, sizeof(coop_skip_hint), tr("Waiting for %s to skip..."), g_trade.name);   /* (its name as ts_frame shows it) */
    else if (asked) {
        unsigned int raw = pad_type[sys->keytype & 3][16];   /* the DS2 button of the skip action: binding row in report order */
        int row = raw >= 0x100 ? __builtin_ctz(raw) - 8 : raw ? __builtin_ctz(raw) + 8 : B_SELECT;
        snprintf(coop_skip_hint, sizeof(coop_skip_hint), tr("%s wants to skip. Press %s to skip."), g_trade.name, binding_names(row, key, sizeof(key)));
    }
}

static int apply_event(const Event* e)
{
    if (e->type == EV_EOPTS) { if (!is_host) enemy_opts_from_host(&e->u.eo); return 1; }   /* before the blob it precedes */
    if (e->type == EV_CASTOFF) { if (!is_host) enemy_cast_off_from_host(e->u.co.at, e->u.co.n, e->u.co.bits); return 1; }   /* (and EV_EOPTS) */
    if (e->type == EV_BRSEED) {   /* the host's BioRand seed string (checked: coop_check_event; decoded here) and CLI digest */
        BioOpts o;
        memcpy(br_str, e->u.br.str, COOP_BR_STR);
        memcpy(br_dig, e->u.br.dig, COOP_BR_DIG);
        br_str[COOP_BR_STR] = br_dig[COOP_BR_DIG] = 0;
        if (br_str[0] && (strlen(br_str) != BIORAND_SEED_LEN || biorand_seed_decode(br_str, &o))) {
            host_log("coop: the host's BioRand seed string cannot be read here: %s\n", br_str);
            br_str[0] = 0;
        }
        br_got = 1;
        host_log("coop: the host's BioRand seed string %s, its CLI %s\n", br_str[0] ? br_str : "unknown", br_dig[0] ? br_dig : "unknown");
        return 1;
    }
    if (pop_pending) return 0;   /* what follows a blob applies to the loaded game */
    if (e->type == EV_SPAWN) return is_host ? 1 : spawn_note_apply(e->idx, &e->u.sp, e->stg, e->rom);
    if (e->type == EV_ESTATE) {   /* EV_GRAB / EV_UNGRAB name the transfer they need complete */
        if (e->u.blob.total == ESTATE_SIZE || e->u.blob.total == ESTATE_PLY)
            coop_xfer_chunk(&estate_x, e->u.blob.tid, e->u.blob.total, e->u.blob.off, e->u.blob.len, e->u.blob.data);
        return 1;
    }
    if (e->type == EV_GRAB) {
        if (!is_host && sys && plp) grab_in(e);
        return 1;
    }
    if (e->type == EV_TRIG) return sys ? apply_trig(e) : 1;
    if (e->type == EV_SKIP) {   /* both asked to skip: the host skipped, we too (only while we still ask, in its room) */
        if (!is_host && sys && skip_want && cine_now() && e->stg == sys->stg_no && e->rom == sys->rom_no) skip_now("the host skips with us");
        return 1;
    }
    if (e->type == EV_TRADE) return apply_trade(e);
    if (e->type == EV_TSCREEN) return apply_tscreen(e);
    if (e->type == EV_BOX || e->type == EV_BOXFIX)   /* one box for both only with shared loot */
        return sys && plp && in_session_game() && !(is_host ? blob_due : need_blob) && box_shared() ? apply_box(e) : 1;
    if (e->type == EV_ATR) return apply_atr(e);
    if (e->type == EV_FX) return apply_fx(e);
    if (e->type == EV_GRANT) return apply_grant(e);
    if (e->type == EV_GONE) return apply_gone(e);
    if (e->type == EV_UNGRAB) {
        int i = e->idx, c = i < PC_ENE_SLOTS ? owned_child[i] : 255;
        /* only a grab we handed over. It ends here whatever comes with it: the enemy is ours again
           (with the state only when the child is the one we handed over with it - the transaction) */
        if (!is_host || !sys || i >= PC_ENE_SLOTS || !owned[i]) return 1;
        owned[i] = 0;
        owned_child[i] = 255;
        if (c < PC_ENE_SLOTS) owned[c] = 0;
        own_wait[i] = 0;
        if (e->stg != sys->stg_no || e->rom != sys->rom_no || !(ene[i].flg & 0x1)) return 1;
        if (!e->u.gr.state || c != e->u.gr.child || !estate_x.done || estate_x.tid != e->u.gr.tid || estate_x.total != ESTATE_PLY ||
            !ene_unpack(&ene[i], estate, e->u.gr.mdl, e->u.gr.mnw)) {
            pc_enemy_unhold(&ene[i]);
            host_log("coop: enemy %d is back from the partner without its grab state: released\n", i);
            return 1;
        }
        if (c < PC_ENE_SLOTS && (ene[c].flg & 0x1)) ene_unpack(&ene[c], estate + ESTATE_ENE, e->u.gr.cmdl, e->u.gr.cmnw);
        host_log("coop: enemy %d is back from the partner's grab\n", i);
        return 1;
    }
    if (e->type >= EV_BLOB) return apply_session_event(e);
    if (trace) fprintf(stderr, "coop: event %u in type=%d idx=%d damage=%d target_hp=%d\n", e->id, e->type, e->idx,
                       e->type == EV_PLY_HIT ? e->u.ph.hp_delta : e->u.eh.dam[0] + e->u.eh.dam[1] + e->u.eh.dam[2] + e->u.eh.dam[3] + e->u.eh.dam[4] + e->u.eh.dam[5],
                       e->type == EV_PLY_HIT ? (plp ? plp->hp : 0) : e->idx < PC_ENE_SLOTS ? ene[e->idx].hp : 0);
    if (!sys || !plp || e->stg != sys->stg_no || e->rom != sys->rom_no) return 1;
    if (e->type == EV_ENE_HIT && e->idx < sys->ewk_n && e->idx < PC_ENE_SLOTS && owned[e->idx] != is_host) {
        BH_PWORK* ep = &ene[e->idx];
        int n;
        if (!(ep->flg & 0x1) || !ep->mlwP) return 1;
        for (n = 0; n < 6; n++)   /* joints of its model only (coop_check_event: 0..63, damage bounded) */
            if (e->u.eh.jnt[n] >= (int)ep->mlwP->obj_num) return 1;
        for (n = 0; n < 6; n++)
            if (e->u.eh.jnt[n] >= 0 && ep->dam[(int)e->u.eh.jnt[n]] < 100000) ep->dam[(int)e->u.eh.jnt[n]] += e->u.eh.dam[n];
        ep->flg |= e->u.eh.flg & 0x4; ep->flg2 |= e->u.eh.flg2; ep->comb_flg |= e->u.eh.comb_flg;
        if (e->u.eh.flg & COOP_HIT_AIM) hit_aim[e->idx] = 0x80 | ((e->u.eh.flg >> 8) & 0xE);
        ep->djnt_no = e->u.eh.djnt_no; ep->wpnr_no = e->u.eh.wpnr_no;
        tps_hit_precise[e->idx] = e->u.eh.flg & COOP_HIT_PRECISE ? e->u.eh.wpnr_no + 1 : 0;
        ep->dpx = e->u.eh.dp[0]; ep->dpy = e->u.eh.dp[1]; ep->dpz = e->u.eh.dp[2];
        ep->dvx = e->u.eh.dv[0]; ep->dvy = e->u.eh.dv[1]; ep->dvz = e->u.eh.dv[2];
        ep->dax = e->u.eh.dax; ep->day = e->u.eh.day;
    } else if (e->type == EV_PLY_HIT && !is_host) {
        plp->hp -= e->u.ph.hp_delta;
        /* the enemy's hit reaction (as en01's bite/push sets it), unless we are already reacting */
        if (e->u.ph.mode[0] == 2 && plp->mode0 != 2 && plp->mode0 != 3 && plp->hp >= 0) {
            plp->mode0 = 2; plp->mode1 = e->u.ph.mode[1]; plp->mode2 = e->u.ph.mode[2]; plp->mode3 = e->u.ph.mode[3];
            plp->flg |= e->u.ph.flg_set & 0xC;
            plp->flg &= ~0x40000;   /* no planted foot, as the reaction's start (bhCPM0_damage) when the event skips it */
            *(int*)plp->exp1 |= e->u.ph.exp1_set & 0x4;
            plp->dvx = e->u.ph.dv[0]; plp->dvy = e->u.ph.dv[1]; plp->dvz = e->u.ph.dv[2];
            plp->stflg &= ~0x50000;
            plp->at_flg = 0;
            plp->mnwP = plp->mnwPb;
            StartVibrationEx(1, 11);   /* the rumble of an enemy's hit, which here ran in the host's game (OPTION VIBRATION) */
        }
    }
    return 1;
}

/* Every way out of a session (Disconnect, LEAVE, CLOSE THE LOBBY, leaving the lobby screen, back to the
   title, quitting) says goodbye: PKT_BYE (protocol 10 has it) three times, UDP may drop one; a partner
   that never hears it times out (LOST_MS). */
#define LOST_MS 15000
static int host_br = -1;                /* guest: the host plays its BioRand pack (1) or not (0), -1 not known yet */
static NetResolve* join_dns;            /* joiner: the host's name being looked up (join_resolve) */
static char join_host[128];
static char left_msg[160];              /* host: why the last partner went ("Partner left"), shown while waiting */
static int was_game;                    /* we were in a game during this session (back at the title ends it) */
static unsigned int gone_sid;           /* host: the session of the partner that said goodbye */

/* The join code (protocol 11): the host's game makes one per run and shows it in the lobby and F3
   ("Copy join address" gives ip:port#CODE). A direct session starts with a handshake bound to the
   whole code (protocol 28, coop_proto.h CoopDirect); from then on every datagram is sealed with its
   keys: the host only takes a partner whose datagrams open with them, and the partner only listens
   to a host whose do. Without the code a stranger gets nothing but a rate-limited "wrong code", and
   each failed address waits longer (CoopGuard). */
static char code_rx[COOP_CODE_LEN + 1];   /* the session's code (host: ours; joining: from the address) */
static CoopDirect direct;               /* the direct session's handshakes and keys (none through a server) */
static char ver_msg[256];               /* a sender of another protocol version (shown while waiting) */
static char srv_msg[160];               /* host: why there is no room on the server (shown while waiting) */

/* Through the server (docs/SERVER.md) the partner is rdv_peer, whatever the path (punched,
   relayed): rendezvous.c frames, encrypts and routes what goes there. Directly, CoopDirect seals it. */
static int via_rdv(const NetAddr* a) { return rdv_active() && a->ip == rdv_peer.ip && a->port == rdv_peer.port; }
static void xsend(const NetAddr* to, const void* b, int n)
{
    static uint8_t out[COOP_DGRAM_MAX + COOP_DIRECT_OVER];
    int k;
    if (via_rdv(to)) rdv_send(b, n);
    else if ((k = coop_direct_seal(&direct, to->ip, to->port, b, n, out, sizeof(out)))) net_send(to, out, k);   /* nothing without keys */
}

static void hdr(Pose* b, int type)
{
    memset(b, 0, sizeof(*b));
    b->magic = COOP_MAGIC;
    b->version = COOP_VERSION;
    b->type = type;
    b->sid = my_sid;
}

static void send_bye(int why)
{
    Pose b;
    int k;
    hdr(&b, PKT_BYE);
    b.reject = (uint8_t)why;   /* COOP_REJ_REMOVED: the host removed the partner */
    for (k = 0; k < 3; k++) xsend(&peer, &b, sizeof(b));
}

/* host: a sender without the code (or a second partner) hears why, at most once a second; in plain
   text (one without the code has no keys of ours to open it with) */
static void send_reject(const NetAddr* to, int why)
{
    static unsigned int last;
    Pose b;
    if (net_ms() - last < 1000) return;
    last = net_ms();
    hdr(&b, PKT_REJECT);
    b.reject = why;
    if (via_rdv(to)) rdv_send(&b, sizeof(b));
    else net_send(to, &b, sizeof(b));
    host_log("coop: %s from %s refused\n", why == COOP_REJ_CODE ? "a join without our code" : "a second partner", net_addr_str(to));
}

/* atexit (main thread: window closed, quit): the game thread may be anywhere, so only the goodbye */
static void coop_exit(void)
{
    if (coop_on && have_peer && !echo_delay) send_bye(0);
}

static void stop(void)
{
    /* the session's end never moves our player: our work back if an enemy step had it swapped for the
       partner's (forget() clears that one), and no grab position guard carried over */
    if (swap_plp) { plp = swap_plp; swap_plp = NULL; ply2 = ply2_save; }
    if (swap_pid >= 0) { sys->ply_id = swap_pid; swap_pid = -1; }
    ghost.slot = -1;
    memset(&safe, 0, sizeof(safe));
    if (sys && rom) lighter_light(0);   /* the partner's lighter flame and light */
    if (have_peer && !echo_delay) send_bye(bye_code);
    bye_code = 0;
    host_br = -1;   /* single player: our own BioRand setting again */
    br_str[0] = br_dig[0] = 0;
    br_offer = br_got = 0;
    br_wait = 0;
    rdv_stop();
    coop_direct_wipe(&direct);
    if (join_dns) { net_resolve_cancel(join_dns); join_dns = NULL; }
    g_coop.room[0] = 0;
    srv_msg[0] = 0;
    net_close();
    portmap_stop();
    /* our own model back; display actor 0 only in game: at the title it is the lobby's preview (self_frame
       sets ours only in game) and sys->obwp is NULL there (setup_hair crashed: OPEN THE LOBBY, CLOSE, LEAVE) */
    if (sys && plp) { self_restore(); if (!(sys->tk_flg & 0x8)) self_actor(-1); }
    if (p2_chr >= 0) release_textures();
    forget();
    have_peer = have_rx = 0;
    coop_ring_reset(&ring); in_last = 0; snap_n = 0; proxy_ok = 0; ver_msg[0] = 0;
    coop_xfer_init(&blob_x, blob_in, sizeof(blob_in));
    coop_xfer_init(&estate_x, estate, sizeof(estate));
    owned_drop();
    peer_sid = blob_end = 0;
    blob_due = need_blob = pop_pending = popping = follow_pending = 0;
    host_room = -1;
    bye_reason = NULL;
    left_msg[0] = 0;
    id_ok = id_rejoin = crash_ok = rec_done = 0;
    if (!is_host && own_live && in_session_game()) own_cont = 1;   /* the host's game goes on here: a join again keeps its items */
    own_live = own_sent_ok = blob_rec = want_rec = 0;
    own_hold = go_was = go_cont = go_want = go_seen = go_held = 0;
    peer_opts = -1;
    spawn_pending(0);
    enemy_opts_from_host(NULL);   /* single player again: our own enemy options */
    note_msg[0] = 0;
    was_game = 0;
    coop_on = 0;
    g_coop.state = COOP_OFF;
    escrow_home();
    if (g_trade.phase == TRADE_OPEN || ts.sw.state) inv_log("session ended");
    g_trade.phase = TRADE_IDLE;   /* the screen closes; a swap of ours on its way asks at the next session */
    g_trade.want[0] = g_trade.want[1] = -1;
    frozen = 0;
    g_coop.status[0] = 0;   /* "not connected"; a failed start or a goodbye writes its reason after this */
}

/* host: "Remove stuck player" (F3, lobby). The session with the partner ends here and, through a
   server, there too (KICK: its place is free, the room code stays), so it - or anyone with the code -
   can join again at once. A partner that is still there hears a goodbye. */
static void kick(void)
{
    char who[COOP_NAME_MAX];
    if (!coop_on || !is_host || !have_peer || echo_delay) return;
    snprintf(who, sizeof(who), "%s", id_ok ? id_name : "");
    host_log("coop: removing the partner %s\n", who);
    send_bye(COOP_REJ_REMOVED);
    if (via_rdv(&peer)) rdv_kick();
    else coop_direct_forget(&direct, peer.ip, peer.port);   /* its keys go: back only with a new handshake */
    partner_gone(0);   /* its next session is a rejoin: its saved items */
    gone_sid = peer_sid;
    have_peer = have_rx = 0;
    snap_n = 0;
    snprintf(left_msg, sizeof(left_msg), tr("%s removed"), who[0] ? who : tr("The partner"));
    g_coop.state = COOP_HOSTING;
}

/* a session id: random - a game started again after a crash must not reuse the one the partner still
   holds (the clock and the address alone repeat from run to run: the stale session was taken for the new) */
static unsigned int new_sid(void)
{
    unsigned int s;
    if (!rdv_random(&s, sizeof(s))) s = net_ms() * 2654435761u ^ (unsigned int)(size_t)&new_sid ^ frame_no * 40503u ^ (unsigned int)time(NULL);
    return s | 1;
}

/* the host's join code for this run: RECVX_COOP_CODE (tests), else a new random one (OS randomness,
   as the server's room codes: a code made from the clock could be guessed in far fewer tries) */
static int host_code(void)
{
    const char* env = getenv("RECVX_COOP_CODE");
    uint8_t rnd[COOP_CODE_LEN];
    if (env && rdv_code_norm(env, g_coop.code)) { host_log("coop: warning: the join code is fixed by RECVX_COOP_CODE (a test aid)\n"); return 1; }
    if (rdv_code_norm(g_coop.code, g_coop.code)) return 1;
    if (!rdv_random(rnd, sizeof(rnd))) return 0;
    rdv_code_from(rnd, g_coop.code);
    return 1;
}

/* joiner: the host's address being looked up (start); nothing goes out until it is there */
static void join_resolve(void)
{
    int r = net_resolve_poll(join_dns, &peer);
    if (r > 0) { join_dns = NULL; join_at = net_ms(); join_frame = frame_no; return; }
    if (r < 0 || net_ms() - join_at > 5000) {
        static char why[200];
        if (!r) net_resolve_cancel(join_dns);
        join_dns = NULL;
        snprintf(why, sizeof(why), tr("Cannot find host %s%s. Check the address and connection."), join_host, r ? "" : tr(" (lookup timed out)"));
        bye_reason = why;
    }
}

/* start a server session: host (room == NULL) or join `room`. The socket is open (host) or opened
   here (join). 0 with g_coop.status saying why. */
static int rdv_begin(int host, const char* room)
{
    RdvConfig c;
    const char* env;
    memset(&c, 0, sizeof(c));
    c.host = host;
    if (room) snprintf(c.code, sizeof(c.code), "%s", room);
    memcpy(c.password, (const void*)g_coop.password, sizeof(c.password));   /* the overlay may be editing it */
    c.password[sizeof(c.password) - 1] = 0;
    c.force_relay = g_coop.conn == COOP_CONN_SERVER || ((env = getenv("RECVX_COOP_FORCE_RELAY")) && atoi(env));
    c.no_punch = (env = getenv("RECVX_COOP_NO_PUNCH")) && atoi(env);
    c.punch_ms = (env = getenv("RECVX_COOP_PUNCH_MS")) ? atoi(env) : 0;
    c.e2e = (env = getenv("RECVX_COOP_E2E")) ? atoi(env) : COOP_E2E;
    if (!c.e2e) host_log("coop: warning: RECVX_COOP_E2E=0, a test aid: server sessions without a password are not encrypted\n");
    if (!host) memcpy(c.rejoin, my_tag, 16);   /* our own place in the room is ours again after a crash */
    {
        char srv[sizeof(g_coop.server)];
        memcpy(srv, (const void*)g_coop.server, sizeof(srv));
        srv[sizeof(srv) - 1] = 0;
        if (!host && g_coop.conn == COOP_CONN_DIRECT)
            snprintf(g_coop.status, sizeof(g_coop.status), tr("Choose Automatic or Server Relay to join with a room code."));
        else if (!srv[0]) snprintf(g_coop.status, sizeof(g_coop.status), tr("Enter a co-op server address to join with a room code."));
        else if (!rdv_server_valid(srv)) snprintf(g_coop.status, sizeof(g_coop.status), tr("Invalid server address: %s. Use a name or name:port."), srv);
        else if (!host && net_open(0) != 1) snprintf(g_coop.status, sizeof(g_coop.status), tr("Cannot open a network connection. Try again."));
        else g_coop.status[0] = 0;
    }
    if (g_coop.status[0]) {
        if (host) { snprintf(srv_msg, sizeof(srv_msg), "%s", g_coop.status); host_log("coop: no room on the server: %s\n", srv_msg); g_coop.status[0] = 0; }
        rdv_wipe(&c, sizeof(c));
        return 0;
    }
    /* the name is looked up on a thread (rdv_frame, 5 s at most): DNS never stalls the game */
    memcpy(c.server_name, (const void*)g_coop.server, sizeof(c.server_name));
    c.server_name[sizeof(c.server_name) - 1] = 0;
    rdv_start(&c);
    rdv_wipe(&c, sizeof(c));   /* the password copy */
    return 1;
}

static void start(int req)
{
    static int registered;
    char join[sizeof(g_coop.join)], host[128];
    int r, port;
    stop();
    if (!registered) { atexit(coop_exit); registered = 1; }
    tx.magic = COOP_MAGIC;
    tx.version = COOP_VERSION;
    tx.sid = my_sid = new_sid();
    if (req == COOP_REQ_HOST) {
        if ((r = net_open(g_coop.port)) != 1) {
            snprintf(g_coop.status, sizeof(g_coop.status), tr(r == NET_IN_USE ? "Port %d is in use. Choose another."
                     : r == NET_DENIED ? "Port %d is unavailable. Choose another."
                     : "Cannot open port %d. Check your network settings."), g_coop.port);
            host_log("coop: %s\n", g_coop.status);
            return;
        }
        if (!host_code()) {
            snprintf(g_coop.status, sizeof(g_coop.status), tr("Cannot create a secure join code. Try restarting the game."));
            host_log("coop: %s\n", g_coop.status);
            net_close();
            return;
        }
        memcpy(code_rx, g_coop.code, COOP_CODE_LEN + 1);
        is_host = 1;
        g_coop.state = COOP_HOSTING;
        portmap_start(g_coop.port, g_coop.upnp);   /* background thread; status in the F3 window */
        host_log("coop: join code %.4s-xxxx (the lobby and F3 show it)\n", g_coop.code);   /* logs get shared: not the whole code */
        if (g_coop.conn != COOP_CONN_DIRECT && g_coop.server[0]) rdv_begin(1, NULL);   /* also a room on the server */
    } else {
        char room[RDV_CODE_LEN + 1];
        coop_items_apply();   /* the joining inventory (lobby ITEMS, F3) */
        memcpy(join, (const void*)g_coop.join, sizeof(join));   /* the overlay may be editing it: a bounded copy */
        join[sizeof(join) - 1] = 0;
        if (!strpbrk(join, ".:#") && rdv_code_norm(join, room)) {   /* a room code: through the server */
            rdv_rejoin_tag(g_coop.pkey, room, my_tag);
            if (!rdv_begin(0, room)) { host_log("coop: %s\n", g_coop.status); return; }
            is_host = 0;
            peer = rdv_peer;
            have_peer = 1;
            need_blob = 1;
            join_at = net_ms(); join_frame = frame_no; join_hinted = 0;
            code_rx[0] = 0;
            g_coop.state = COOP_JOINING;
            coop_on = 1;
            host_log("coop: joining room %s through the server\n", room);
            return;
        }
        if (g_coop.conn == COOP_CONN_SERVER)
            snprintf(g_coop.status, sizeof(g_coop.status), tr("Server Relay needs a room code. For an address, choose Automatic or Direct."));
        else if (!coop_parse_join(join, host, sizeof(host), &port, code_rx))
            snprintf(g_coop.status, sizeof(g_coop.status), tr("Invalid address: %s. Paste the host's copied join address."), join);
        else if (!code_rx[0])
            snprintf(g_coop.status, sizeof(g_coop.status), tr("Join code missing. Ask the host to use Copy Join Address."));
        else if (!strcmp(code_rx, "?"))
            snprintf(g_coop.status, sizeof(g_coop.status), tr("Invalid join code. Use the eight letters and digits shown by the host."));
        else if (net_open(0) != 1) snprintf(g_coop.status, sizeof(g_coop.status), tr("Cannot open a network connection. Try again."));
        else if (!(join_dns = net_resolve_start(host, port))) snprintf(g_coop.status, sizeof(g_coop.status), tr("Cannot look up host %s. Try restarting the game."), host);
        else g_coop.status[0] = 0;
        if (g_coop.status[0]) { host_log("coop: %s\n", g_coop.status); net_close(); return; }
        /* the host's name is looked up on a thread (join_resolve, 5 s at most): DNS never stalls the game */
        snprintf(join_host, sizeof(join_host), "%s", host);
        memset(&peer, 0, sizeof(peer));
        rdv_rejoin_tag(g_coop.pkey, code_rx, my_tag);
        is_host = 0;
        have_peer = 1;
        need_blob = 1;
        join_at = net_ms(); join_frame = frame_no; join_hinted = 0;
        g_coop.state = COOP_JOINING;
    }
    coop_direct_init(&direct, is_host, code_rx);   /* the handshake's generator from the code (direct joins, also while hosting with a server) */
    coop_on = 1;
    host_log("coop: %s\n", is_host ? "hosting" : "joining");
}

/* the event ring overflowed (queue_event): a new session of ours restarts both streams on both sides,
   and the host's game state goes over again - every side ends with the host's state */
static void resync(void)
{
    tx.sid = my_sid = new_sid();
    session_reset();
    host_log("coop: resynchronised (new session %08x)\n", my_sid);
}

/* What came this frame, read within a budget (datagrams, bytes, time): a flood from anywhere cannot
   stall the game thread. Datagrams not from our partner are dropped unread once we have one. Each
   datagram is opened with its session's keys (the proof of the join code) and checked as a whole -
   header, version, exact size, every event and snapshot within its ranges - before it may change
   anything, make its sender our partner or restart its session. */
/* host: a datagram from a new address (or session) of our partner's player - the same player id
   and the rejoin tag only it can make for this session's code: its game crashed and started again
   while our session with the old one still runs. 1: it is our partner now (the old session goes). */
static int takeover(const NetAddr* from, const unsigned char* buf, int n)
{
    PoseExt x;
    Pose p;
    int nev, nene, ext;
    if (!id_ok || zero16(id_pid) || zero16(id_tag) || coop_check_all(buf, n, 0, &nev, &nene, &ext) || !ext) return 0;
    memcpy(&p, buf, sizeof(p));
    memcpy(&x, buf + n - sizeof(x), sizeof(x));
    if (p.type != PKT_POSE || p.sid == peer_sid || p.sid == gone_sid || !same16(x.pid, id_pid) || !same16(x.tag, id_tag)) return 0;
    host_log("coop: %s joined again from %s (the same player): the stale session is replaced\n", id_name, net_addr_str(from));
    peer = *from;
    have_rx = 0;
    snap_n = 0;
    return 1;
}

/* guest: the host's BioRand (protocol 27, docs/COOP.md "BioRand"). The loader follows the host's choice for the
   session (coop_biorand); while the host plays its pack, ours must be the same one (seed, options, files), else we
   leave with why. At the title (the lobby or the title menu; --dev, as the BIORAND screen), with the host's seed string
   (EV_BRSEED), the BIORAND screen first offers to generate it (protocol 30, docs/BIORAND_INGAME.md S5b): the join waits
   meanwhile, the poses keep the session. */
static void biorand_from_host(void)
{
    static char why[200];
    host_br = rx_ext.br;
    if (bye_reason) return;
    if (!rx_ext.br || (biorand_pack() && rx_ext.br_id == biorand_id())) {
        if (br_offer != BR_GEN) br_offer = BR_NONE;   /* (the screen leaves an offer that is no longer needed) */
        br_wait = 0;
        return;
    }
    if (br_offer == BR_ASK || br_offer == BR_GEN) return;
    if (br_offer == BR_NONE && br_str[0] && biorand_shown() && sys && !in_session_game()) {
        if (!(sys->tk_flg & 0x8)) return;   /* (starting up, the attract demo: the title comes) */
        br_offer = BR_ASK;
        host_log("coop: the host plays BioRand %s; ours differs: the BIORAND screen offers to generate it\n", br_str);
        return;
    }
    if (br_offer == BR_NONE && !br_got) {   /* the host sends EV_BRSEED with the extension that says br; wait a moment for it */
        if (!br_wait) br_wait = net_ms() ? net_ms() : 1;
        if (net_ms() - br_wait < 2000) return;
    }
    if (rx_ext.br_seed == 0xFFFFFFFFu) snprintf(why, sizeof(why), tr("BioRand games do not match. Generate the host's seed and options, or ask them to turn BioRand off."));
    else if (biorand_pack() && (uint32_t)biorand_seed() == rx_ext.br_seed)
        snprintf(why, sizeof(why), tr("BioRand seed %u differs. Match the host's options and BioRand version, or ask them to turn BioRand off."), rx_ext.br_seed);
    else snprintf(why, sizeof(why), tr("Generate the host's BioRand seed %u, or ask them to turn BioRand off."), rx_ext.br_seed);
    bye_reason = why;
    bye_code = COOP_REJ_BIORAND;
}
int coop_biorand(void) { return coop_on && !is_host ? host_br : -1; }

/* the BIORAND screen (biorand_menu.c): the host's seed string while it offers or generates it, else NULL; *dig its CLI
   digest's first hex digits ("" unknown) */
const char* coop_biorand_offer(const char** dig)
{
    if (!coop_on || is_host || (br_offer != BR_ASK && br_offer != BR_GEN)) return NULL;
    if (dig) *dig = br_dig;
    return br_str;
}

/* ... and how it went: 1 generating, 2 generated (the pack is compared again: the same joins, another leaves), 0 No
   (or it could not): we leave as without the offer */
void coop_biorand_reply(int r)
{
    if (!coop_on || is_host || (br_offer != BR_ASK && br_offer != BR_GEN)) return;
    br_offer = r == 1 ? BR_GEN : BR_DONE;
    host_log("coop: the host's BioRand seed: %s\n", r == 1 ? "generating it" : r ? "generated; compared again" : "not generated");
}

#define RX_DGRAMS 96
#define RX_BYTES (128 * 1024)
#define RX_MS 3
static unsigned int rx_bad, rx_strangers;   /* counted for the trace and the stats line */
/* host: a datagram with keys of another code (CoopDirect): it hears so (send_reject) and waits; the log says
   it at most once a second */
static void wrong_code(const NetAddr* from)
{
    static unsigned int last, n;
    n++;
    if (net_ms() - last >= 1000) {
        host_log("coop: %u join attempt%s with a wrong code, the last from %s: that address waits %d ms\n", n, n == 1 ? "" : "s",
                 net_addr_str(from), coop_guard_wait(&direct.guard, from->ip, net_ms()));
        last = net_ms();
        n = 0;
    }
    send_reject(from, COOP_REJ_CODE);
}

static void receive(void)
{
    static unsigned char buf[COOP_DGRAM_MAX + 64];
    static unsigned int snap_seq;
    NetAddr from;
    Pose p;
    const Event* ev;
    const EneSnap* sn;
    int n, k, nev, nene, ext, count = 0, bytes = 0, r, took = 0;
    unsigned int now = net_ms();
    /* the time budget is checked before a read: a datagram read (and decrypted, which uses up its
       counter) is never dropped for it */
    while (count < RX_DGRAMS && bytes < RX_BYTES && ((count & 15) || net_ms() - now <= RX_MS) &&
           (n = net_recv(&from, buf, sizeof(buf))) >= 0) {
        int known, sealed = 1;   /* sealed: opened with a session's keys (direct or through the server) */
        uint64_t arr = net_rx_us();
        count++;
        bytes += n;
        if (rdv_active()) {   /* server messages, punches, relayed/encrypted game datagrams (from becomes rdv_peer) */
            int k = rdv_input(&from, buf, n, sizeof(buf));
            if (k < 0) continue;
            if (k > 0) n = k;
        }
        if (!via_rdv(&from)) {   /* direct: the handshake, a sealed datagram, or plain text of another kind */
            CoopHello reply;
            int rn, d = coop_direct_in(&direct, from.ip, from.port, net_ms(), buf, n, sizeof(buf), &reply, &rn);
            if (rn) net_send(&from, &reply, rn);
            if (d == COOP_DIN_WRONG) { rx_bad++; wrong_code(&from); continue; }
            if (d == 0) continue;
            if (d > 0) n = d;
            else sealed = 0;   /* COOP_DIN_RAW: a PKT_REJECT, another protocol version */
        }
        known = have_peer && from.ip == peer.ip && from.port == peer.port;
        if (!known && (!is_host || (have_peer && have_rx))) {   /* not our partner */
            if (is_host && sealed && takeover(&from, buf, n)) known = 1;   /* our partner's player again, from a new game: the stale session goes */
            else {
                rx_strangers++;
                if (is_host && sealed && !coop_check_dgram(buf, n, &nev, &nene, &ext)) send_reject(&from, COOP_REJ_BUSY);   /* it has the code */
                continue;
            }
        }
        r = coop_check_all(buf, n, !is_host, &nev, &nene, &ext);
        if (r == COOP_BAD_VERSION && n >= 8) {   /* another build: say so (an older one reads our version from the reply) */
            memcpy(&p, buf, 8);
            if (!ver_msg[0]) host_log("coop: %s runs co-op protocol %d, we %d\n", net_addr_str(&from), p.version, COOP_VERSION);
            snprintf(ver_msg, sizeof(ver_msg), tr("%s uses co-op compatibility version %d; yours is %d. Use matching game builds."), tr(is_host ? "A partner" : "The host"), p.version, COOP_VERSION);
            if (is_host) send_reject(&from, COOP_REJ_CODE);
            continue;
        }
        if (r) { rx_bad++; if (trace) fprintf(stderr, "coop: datagram from %s dropped (check %d)\n", net_addr_str(&from), r); continue; }
        memcpy(&p, buf, sizeof(p));
        if (p.type == PKT_REJECT) {   /* the host answers our join: wrong code, or it has a partner */
            if (!is_host && known && !have_rx) bye_reason = p.reject == COOP_REJ_BUSY ? "The host already has a partner" : "The host did not accept the join code";
            continue;
        }
        if (!sealed) continue;   /* (coop_direct_in passes nothing else in plain text) */
        ev = (const Event*)(buf + sizeof(Pose));
        sn = (const EneSnap*)(buf + sizeof(Pose) + nev * sizeof(Event));
        /* the whole datagram is fine: from here it may change things */
        if (p.sid == gone_sid) continue;   /* a partner that said goodbye: its last poses may still come */
        /* ponytail: one partner; the host takes the first one with the code (another one once that
           one has left or timed out) */
        if (is_host && !known) {
            if (p.type == PKT_BYE) continue;
            peer = from; have_peer = 1; known = 1;
            host_log("coop: partner %s\n", net_addr_str(&from));
        }
        if (!known) continue;
        if (p.sid != peer_sid) {   /* a new partner session: new event streams, frame counter */
            if (peer_sid) host_log("coop: partner session restarted\n");
            if (is_host) partner_gone(0);   /* a new session without a goodbye before: the same player rejoins (or another one) */
            peer_sid = p.sid;
            have_rx = 0;
            session_reset();
        }
        if (p.type == PKT_BYE) {   /* sent three times: the copies after the first find no partner */
            if (is_host && have_peer) {   /* the next partner may come */
                host_log("coop: partner left%s\n", p.reject == COOP_REJ_STATE ? ": it cannot load our game state" : p.reject == COOP_REJ_BIORAND ? ": its BioRand pack is not ours" : "");
                partner_gone(1);
                snprintf(left_msg, sizeof(left_msg), tr(p.reject == COOP_REJ_BIORAND ? "%s left: BioRand games do not match." :
                         p.reject == COOP_REJ_STATE ? "%s left: unable to load this game." : "%s left"),
                         g_coop.peer_name[0] ? g_coop.peer_name : tr("The partner"));
                have_rx = have_peer = 0;
                snap_n = 0;
                gone_sid = p.sid;
                g_coop.state = COOP_HOSTING;
            } else if (!is_host) {   /* the session is over: coop_frame stops it */
                host_log("coop: the host left\n");
                bye_reason = p.reject == COOP_REJ_REMOVED ? "The host removed you. You can join again."
                           : (have_rx && (rx.flags & PF_IN_GAME)) || in_session_game() ? "The host left the game"   /* we play in its game: its last
                           poses may be from the title already (game over "No", OPTION's RETURN TO TITLE) */
                           : "The host closed the lobby";
                have_peer = 0;   /* no goodbye back */
                break;
            }
            continue;
        }
        /* events: in order, each once (the sender repeats unacknowledged ones) */
        coop_ring_ack(&ring, p.ev_ack);
        for (k = 0; k < nev; k++)
            if (ev[k].id == in_last + 1) {
                if (!apply_event(&ev[k])) break;   /* not yet: the peer resends it */
                in_last++;
            }
        /* a frame comes in one or more datagrams, each with the pose: the newest frame's snapshots
           are all those of its datagrams; an older frame changes nothing */
        if (have_rx && (int)(p.seq - rx.seq) < 0) continue;
        if (!have_rx || p.seq != snap_seq) { snap_n = 0; snap_seq = p.seq; }
        for (k = 0; k < nene && snap_n < MAX_SNAP; k++) snap[snap_n++] = sn[k];
        if (ext) {
            memcpy(&rx_ext, buf + n - sizeof(PoseExt), sizeof(PoseExt));
            if (is_host && !id_ok) partner_identity();
            if (!is_host) biorand_from_host();
        }
        if (!took) {   /* ping: the first datagram read here (on Windows the one with a noted arrival) */
            took = 1;
            if (!have_rx) ping_f = -1;   /* a new partner or session, a time-out: a new average */
            echo_t = p.t_ms;
            echo_arr = arr;
            if (p.echo_ms) {   /* our clock (whole ms, sent mid-ms on average) back, less the partner's hold */
                double ms = (int32_t)((uint32_t)(arr / 1000) - p.echo_ms) + (double)(arr % 1000) / 1000.0 - 0.5 - p.echo_age;
                if (ms < 0) ms = 0;
                if (ms < 10000) ping_f = ping_f < 0 ? ms : ping_f + 0.1 * (ms - ping_f);   /* ~10 frames */
                if (ping_f >= 0) g_coop.ping_ms = (int)(ping_f + 0.5);
                if (trace) fprintf(stderr, "coop: ping sample %.2f ms (the partner held ours %u ms), smoothed %.2f\n", ms, p.echo_age, ping_f);
            }
        }
        rx = p;
        have_rx = 1;
        rx_at = now;
        g_coop.state = COOP_CONNECTED;
        left_msg[0] = 0;
        if (trace) fprintf(stderr, "coop: rx seq=%u flags=%d chr=%d stg=%d rom=%d pos=(%.1f,%.1f,%.1f) ay=%d mtn=%d frm=%d wpn=%d hp=%d mode0=%d mset=%d\n",
                            p.seq, p.flags, p.chr, p.stg, p.rom, p.px, p.py, p.pz, p.ay, p.mtn_no, p.frm_no >> 16, p.wpnr_no, p.hp, p.mode0, p.mset);
    }
}

/* a name or note from the partner: printable ASCII only (the game's font, the overlay) */
static void peer_text(char* out, const char* in, int n)
{
    int i;
    for (i = 0; i < n - 1 && in[i]; i++) out[i] = in[i] >= 32 && in[i] < 127 ? in[i] : '?';
    out[i] = 0;
}

/* the overlay's copy (coop.h CoopStatus): written whole under a lock once per frame */
static CoopStatus pub;
static char pub_lock;
void coop_status_get(CoopStatus* out)
{
    while (__atomic_test_and_set(&pub_lock, __ATOMIC_ACQUIRE)) ;
    *out = pub;
    __atomic_clear(&pub_lock, __ATOMIC_RELEASE);
}
static void publish_status(void)
{
    static CoopStatus s;
    s.state = g_coop.state; s.ping_ms = g_coop.ping_ms; s.host = g_coop.host; s.at_title = g_coop.at_title;
    s.lobby = g_coop.lobby; s.loot_now = g_coop.loot_now;
    s.peer_stg = g_coop.peer_stg; s.peer_rom = g_coop.peer_rom; s.peer_chr = g_coop.peer_chr;
    s.peer_in_game = g_coop.peer_in_game; s.remote_visible = g_coop.remote_visible;
    memcpy(s.status, g_coop.status, sizeof(s.status)); s.status[sizeof(s.status) - 1] = 0;
    s.trade_near = g_coop.trade_near; s.trade_slots = g_coop.trade_slots;
    memcpy(s.trade_inv, g_coop.trade_inv, sizeof(s.trade_inv));
    s.trade_out = g_coop.trade_out; s.trade_in = g_coop.trade_in;
    memcpy(s.trade_msg, g_coop.trade_msg, sizeof(s.trade_msg)); s.trade_msg[sizeof(s.trade_msg) - 1] = 0;
    memcpy(s.peer_name, g_coop.peer_name, sizeof(s.peer_name)); s.peer_name[sizeof(s.peer_name) - 1] = 0;
    s.peer_lobby = g_coop.peer_lobby; s.peer_lchr = g_coop.peer_lchr;
    memcpy(s.peer_note, g_coop.peer_note, sizeof(s.peer_note)); s.peer_note[sizeof(s.peer_note) - 1] = 0;
    s.busy_kind = g_coop.busy_kind;
    s.peer_rec = g_coop.peer_rec;
    s.trade_hint[0] = 0;
    s.trade_open = g_trade.phase == TRADE_OPEN;
    if (g_trade.phase == TRADE_ASKED) snprintf(s.trade_hint, sizeof(s.trade_hint), tr("wants to trade - press %.30s"), g_trade.key);
    memcpy(s.code, g_coop.code, sizeof(s.code)); s.code[sizeof(s.code) - 1] = 0;
    memcpy(s.room, g_coop.room, sizeof(s.room)); s.room[sizeof(s.room) - 1] = 0;
    while (__atomic_test_and_set(&pub_lock, __ATOMIC_ACQUIRE)) ;
    pub = s;
    __atomic_clear(&pub_lock, __ATOMIC_RELEASE);
}

/* the partner for the status line: its address, or the server session's path */
static const char* peer_desc(void)
{
    static char s[160];
    if (!via_rdv(&peer)) { snprintf(s, sizeof(s), tr("%s, encrypted"), net_addr_str(&peer)); return s; }
    snprintf(s, sizeof(s), tr("the partner (%s)"), rdv_status());
    return s;
}

static void publish(void)
{
    static const char* names[] = { "Claire", "Chris", "Steve", "Wesker" };
    g_coop.remote_visible = p2_drawn;
    g_coop.host = is_host;
    peer_text(g_coop.peer_name, have_rx ? rx_ext.name : "", sizeof(g_coop.peer_name));
    peer_text(g_coop.peer_note, have_rx ? rx_ext.note : "", sizeof(g_coop.peer_note));
    g_coop.peer_lobby = have_rx && rx_ext.lobby;
    g_coop.peer_lchr = rx_ext.lchr;
    g_coop.peer_in_game = have_rx && (rx.flags & PF_IN_GAME);
    g_coop.peer_stg = rx.stg; g_coop.peer_rom = rx.rom;
    g_coop.peer_chr = rx.chr;
    g_coop.loot_now = individual();
    if (!is_host && have_rx && rx_ext.rec != REC_UNKNOWN && rx_ext.rec != g_coop.peer_rec)   /* the lobby's ITEMS row: "Saved with the host (available)" */
        host_log("coop: the host %s\n", rx_ext.rec == REC_HAVE ? "has saved items for us" : rx_ext.rec == REC_OFF ? "keeps no saved items" : "has no saved items for us");
    g_coop.peer_rec = !is_host && have_rx && rx_ext.rec != REC_UNKNOWN ? rx_ext.rec : -1;
    snprintf(g_coop.room, sizeof(g_coop.room), "%s", rdv_active() ? rdv_room() : "");
    switch (g_coop.state) {
    case COOP_HOSTING:
        snprintf(g_coop.status, sizeof(g_coop.status), tr("%s%sHosting on port %d. Waiting for a partner.%s%s%s%s"), left_msg, left_msg[0] ? " - " : "", g_coop.port,
                 ver_msg[0] ? " - " : "", ver_msg, rdv_active() || srv_msg[0] ? tr(" - server: ") : "", rdv_active() ? rdv_status() : srv_msg);
        break;
    case COOP_JOINING:   /* the hint after 5 s and 30 frames sent (a frame may take seconds while loading) */
        if (ver_msg[0]) snprintf(g_coop.status, sizeof(g_coop.status), "%s", ver_msg);
        else if (via_rdv(&peer)) snprintf(g_coop.status, sizeof(g_coop.status), tr("Joining via server: %s"), rdv_status());
        else if (join_dns) snprintf(g_coop.status, sizeof(g_coop.status), tr("Looking up host %s..."), join_host);
        else if (net_ms() - join_at < 5000 || frame_no - join_frame < 30) snprintf(g_coop.status, sizeof(g_coop.status), tr("Joining %s. Waiting for the host."), net_addr_str(&peer));
        else {
            snprintf(g_coop.status, sizeof(g_coop.status),
                     tr("Joining %s: no reply for %u s. Check the copied address, host lobby, router and firewall. Try Via Server."),
                     net_addr_str(&peer), (net_ms() - join_at) / 1000);
            if (!join_hinted) { join_hinted = 1; host_log("coop: no reply from the host %s yet (port forwarding / firewall?)\n", net_addr_str(&peer)); }
        }
        break;
    case COOP_LOST: snprintf(g_coop.status, sizeof(g_coop.status), tr("Partner at %s is not responding (%u s)."), peer_desc(), (net_ms() - rx_at) / 1000); break;
    case COOP_CONNECTED:
        if (!g_coop.peer_in_game) snprintf(g_coop.status, sizeof(g_coop.status), tr("Connected to %s. Ping: %d ms. Partner has not entered the game."), peer_desc(), g_coop.ping_ms);
        else snprintf(g_coop.status, sizeof(g_coop.status), tr("Connected to %s. Ping: %d ms. %s is in area %d, room %d%s"),
                      peer_desc(), g_coop.ping_ms, g_coop.peer_name[0] ? g_coop.peer_name : names[rx.chr & 3], rx.stg, rx.rom, g_coop.remote_visible ? tr(", visible") : "");
        break;
    }
    if (g_coop.state == COOP_CONNECTED && note_msg[0] && (int)(net_ms() - note_until) < 0) {   /* "Restored ... saved items" */
        size_t k = strlen(g_coop.status);
        snprintf(g_coop.status + k, sizeof(g_coop.status) - k, " - %s", note_msg);
    }
}

/* test aids: RECVX_COOP_DROP="from:to[,from:to...]" sends nothing in those co-op frames (packet
   loss / a stalled partner, as seen from the other side); RECVX_COOP_DROP_EVENTS the same for
   the events only (the pose still goes: lost answers while the partner looks connected);
   RECVX_COOP_LOSS="pct" drops that percentage of our frames at random (its own generator: the
   game's rand() drives the enemies) */
/* trace: the room's collision records switched on/off (walls, triggers, floors), to compare the
   instances after a shared world event */
static void world_trace(void)
{
    static unsigned char was[3][256];
    static int room = -1;
    const ATR_WORK* t[3] = { rom->walp, rom->etcp, rom->flrp };
    int n[3] = { rom->wal_n, rom->etc_n, rom->flr_n }, k, i, r = coop_room_key();
    for (k = 0; k < 3; k++)
        for (i = 0; i < n[k] && i < 256; i++) {
            unsigned char on = t[k][i].flg & 1;
            if (r == room && on != was[k][i]) fprintf(stderr, "coop: room %d/%d %s %d %s (frame %u)\n", sys->stg_no, sys->rom_no, k == 0 ? "wall" : k == 1 ? "trigger" : "floor", i, on ? "on" : "off", frame_no);
            was[k][i] = on;
        }
    room = r;
    if (frame_no % 300 == 0) {   /* the world flags (ev ed ky mp: not the item words), to compare both sides */
        const unsigned int* f = flag_block();
        unsigned int h = 0;
        for (i = 0; i < NFLAG; i++)
            if (!item_word(i)) h = h * 31 + (f[i] & ~(latch_bits(i) | own_bits(i)));   /* a loop's gate and the camera's flags are each game's own */
        fprintf(stderr, "coop: world flags %08x at frame %u in %d/%d\n", h, frame_no, sys->stg_no, sys->rom_no);
        fprintf(stderr, "coop: objects at frame %u:", frame_no);   /* their placement (a door, a bridge) */
        for (i = 4; i < obj_count(); i++) fprintf(stderr, " %d:%.0f,%.0f,%.0f", i, sys->obwp[i].px, sys->obwp[i].py, sys->obwp[i].pz);
        fprintf(stderr, "\n");
    }
}

static int drop_now(int events)
{
    static const char* envs[2] = { (const char*)-1, (const char*)-1 };
    static int loss = -1;
    static unsigned int seed = 12345;
    const char* env;
    const char* p;
    unsigned int a, b;
    int n;
    if (envs[events] == (const char*)-1) envs[events] = getenv(events ? "RECVX_COOP_DROP_EVENTS" : "RECVX_COOP_DROP");
    env = envs[events];
    if (loss < 0) loss = getenv("RECVX_COOP_LOSS") ? atoi(getenv("RECVX_COOP_LOSS")) : 0;
    if (!events && loss > 0 && (int)((seed = seed * 1103515245u + 12345u) >> 16 & 0x7FFF) % 100 < loss) return 1;
    for (p = env; p && sscanf(p, "%u:%u%n", &a, &b, &n) == 2; p += n + (p[n] == ','))
        if (test_clock() >= a && test_clock() < b) return 1;
    return 0;
}

/* Our frame to the partner: the pose, the unacknowledged events (oldest first) and the snapshots,
   in datagrams of at most COOP_DGRAM_MAX bytes (above an internet path's MTU a datagram is split
   into IP fragments, and losing one loses all). Each datagram carries the pose: it is useful alone
   (acknowledgement, ping, its events in order, its snapshots); the first one also the PoseExt. */
static void send_frame(void)
{
    static unsigned char buf[COOP_DGRAM_MAX];
    static Event evs[COOP_EV_FRAME];
    static EneSnap snaps[COOP_SNAP_FRAME];
    static int stats = -1, frames, dgrams, evn, snn, bytes, maxb, peakd, peakb;
    unsigned int id;
    int ne = 0, ns, ie = 0, is = 0, first = 1, fd = 0, fb = 0;
    if (!is_host && !via_rdv(&peer) && !coop_direct_ready(&direct, peer.ip, peer.port)) {   /* direct, no keys yet: the handshake */
        static unsigned int hello_at;
        CoopHello h;
        if (net_ms() - hello_at < 250) return;   /* until the host answers */
        hello_at = net_ms();
        if (coop_direct_hello(&direct, peer.ip, peer.port, &h)) net_send(&peer, &h, sizeof(h));
        else if (!bye_reason) bye_reason = "Cannot secure the connection. Try restarting the game.";
        return;
    }
    if (need_blob && br_offer != BR_ASK && br_offer != BR_GEN) tx.flags |= PF_WANT_BLOB;
    tx.ev_ack = in_last;
    for (id = ring.head; id != ring.next && ne < COOP_EV_FRAME && !drop_now(1); id++) evs[ne++] = *coop_ring_get(&ring, id);
    ns = build_snapshots(snaps);
    {   /* ping: our clock as it goes out, and how long we held the partner's (real time, not frames) */
        uint64_t t = net_us();
        tx.t_ms = (uint32_t)(t / 1000) ? (uint32_t)(t / 1000) : 1;
        tx.echo_ms = have_rx ? echo_t : 0;
        tx.echo_age = have_rx && t > echo_arr ? (uint32_t)((t - echo_arr + 500) / 1000) : 0;
    }
    do {
        int room = COOP_DGRAM_MAX - (via_rdv(&peer) ? rdv_overhead() : COOP_DIRECT_OVER) - (int)sizeof(Pose) - (first ? (int)sizeof(PoseExt) : 0), a = 0, b = 0, off = sizeof(Pose);
        while (ie + a < ne && room >= (int)sizeof(Event)) { a++; room -= sizeof(Event); }
        while (is + b < ns && room >= (int)sizeof(EneSnap)) { b++; room -= sizeof(EneSnap); }
        tx.nev = a;
        tx.nene = b;
        tx.flags = (tx.flags & ~PF_EXT) | (first ? PF_EXT : 0);
        memcpy(buf, &tx, sizeof(tx));
        memcpy(buf + off, &evs[ie], a * sizeof(Event)); off += a * sizeof(Event);
        memcpy(buf + off, &snaps[is], b * sizeof(EneSnap)); off += b * sizeof(EneSnap);
        if (first) { memcpy(buf + off, &tx_ext, sizeof(tx_ext)); off += sizeof(tx_ext); }
        if (!drop_now(0)) xsend(&peer, buf, off);
        ie += a; is += b; first = 0;
        dgrams++; bytes += off; if (off > maxb) maxb = off;
        fd++; fb += off + (via_rdv(&peer) ? rdv_wire_extra() : COOP_DIRECT_OVER);
    } while (ie < ne || is < ns);
    if (fd > peakd) peakd = fd;
    if (fb > peakb) peakb = fb;
    if (stats < 0) stats = getenv("RECVX_COOP_STATS") != NULL;   /* test aid: every 300 frames what we sent and the ping */
    if (stats && (frames++, evn += ne, snn += ns, frames == 300)) {
        host_log("coop: stats: %d frames, %.2f datagrams per frame, avg %d bytes per datagram (max %d), %d bytes per frame, "
                 "%.1f events and %.1f snapshots per frame, ping %.2f ms, room %d/%d, %d enemy slots, %u bad / %u foreign datagrams, path %s (+%d bytes per datagram on the wire), "
                 "busiest frame %d datagrams / %d bytes on the wire, effects sent %u (dropped %u)\n",
                 frames, dgrams / (float)frames, bytes / dgrams, maxb, bytes / frames, evn / (float)frames, snn / (float)frames, ping_f,
                 sys ? sys->stg_no : -1, sys ? sys->rom_no : -1, sys ? (int)sys->ewk_n : 0, rx_bad, rx_strangers,
                 via_rdv(&peer) ? rdv_path() : "direct (address)", via_rdv(&peer) ? rdv_wire_extra() : COOP_DIRECT_OVER, peakd, peakb, fx_sent, fx_dropped);
        fx_sent = fx_dropped = 0;
        frames = dgrams = evn = snn = bytes = maxb = peakd = peakb = 0;
    }
    if (trace) fprintf(stderr, "coop: tx seq=%u flags=%d chr=%d stg=%d rom=%d pos=(%.1f,%.1f,%.1f) ay=%d mtn=%d frm=%d wpn=%d\n",
                       tx.seq, tx.flags, tx.chr, tx.stg, tx.rom, tx.px, tx.py, tx.pz, tx.ay, tx.mtn_no, tx.frm_no >> 16, tx.wpnr_no);
}

/* Safety net: the screen stays black while we play. A fade to black whose fade-in never ran (fade_an and its target
   255), or the full-screen render texture drawn in place of the room (gm_flg 0x100) with no effect 110 (the
   under-water distortion that sets and clears it) alive: the game runs, the room is not shown, until a door
   (bhInitRoomChangeSystem, the door's fade-in) - a script step co-op cut short. In normal play for SCREEN_STUCK
   frames (no event, scene, door or menu; pc_in_play): faded in / the room drawn again, as the door would. */
#define SCREEN_STUCK 90                 /* game frames: 3 s */
int pc_in_play(const BH_PWORK* p, int scope);   /* reload_glue.c */
void bhSetScreenFade(unsigned int argb, float ct);   /* screen.c */
extern float BHD_ASPECT_X, BHD_ASPECT_Y;
static void screen_guard(void)
{
    static unsigned int fade_n, rt_n, last_gfrm;
    int i, fx = 0, ev = 0;
    if (sys->gfrm_ct == last_gfrm) return;   /* game frames only */
    last_gfrm = sys->gfrm_ct;
    if (!pc_in_play(plp, 0)) { fade_n = rt_n = 0; return; }
    for (i = 0; i < 512; i++) if ((eff[i].flg & 1) && eff[i].id == 110 && !(eff[i].stflg & 0x1000000)) fx = 1;   /* alive and stepping (not hidden: EffDispSet) */
    for (i = 0; i < 16; i++) if (bhEtask[i].status) ev |= 1 << i;
    fade_n = sys->fade_an >= 255.0f && sys->fade_ap == 255 ? fade_n + 1 : 0;
    rt_n = (sys->gm_flg & 0x100) && !fx ? rt_n + 1 : 0;
    if (fade_n == SCREEN_STUCK) {
        host_log("coop: the screen stayed black for %d s while we play in %d/%d (fade %.0f, flags cb %x st %x sp %x, event tasks %x): faded in\n",
                 SCREEN_STUCK / 30, sys->stg_no, sys->rom_no, sys->fade_an, sys->cb_flg, sys->st_flg, sys->sp_flg, ev);
        bhSetScreenFade(0, 10.0f);
    }
    if (rt_n == SCREEN_STUCK) {
        host_log("coop: the room was not drawn for %d s while we play in %d/%d (full-screen picture, no effect 110; cb %x st %x, event tasks %x): drawn again\n",
                 SCREEN_STUCK / 30, sys->stg_no, sys->rom_no, sys->cb_flg, sys->st_flg, ev);
        sys->gm_flg &= ~0x100100;   /* as effect 110 ends it (effsub1.c) */
        BHD_ASPECT_Y = 1.0f;
        njSetAspect(BHD_ASPECT_X, 1.0f);
    }
}

void coop_frame(void)
{
    int req = g_coop.request ? __atomic_exchange_n(&g_coop.request, 0, __ATOMIC_ACQ_REL) : 0;
    unsigned int now;

    if (trace < 0) {
        trace = getenv("RECVX_COOP_TRACE") != NULL;
        if (getenv("RECVX_INF_HEALTH")) g_cheat.inf_health = 1;   /* test aid: the F2 cheat (survive grabs in tests) */
        /* debugging aid: the partner is our own pose N frames ago, no network */
        if ((echo_delay = getenv("RECVX_COOP_ECHO") ? atoi(getenv("RECVX_COOP_ECHO")) : 0) > 0) {
            if (echo_delay > 255) echo_delay = 255;
            coop_on = 1;
            g_coop.state = COOP_CONNECTED;
            tx.magic = COOP_MAGIC;
        }
    }
    {   /* the test aids (coop_testaids.c) */
        CoopTest t = { req, is_host, coop_on, echo_delay, tx_in_game, proxy_ok, frozen, test_clock(), frame_no, owned, &ply2, move_player };
        coop_test_aids(&t);
        req = t.req;
        pad_test |= t.pad;
    }
    if (req == COOP_REQ_STOP) stop();
    else if (req == COOP_REQ_KICK) kick();
    else if (req) start(req);
    if (sys && (sys->tk_flg & 0x8)) title_seen = 1, own_cont = 0;   /* saved partner inventories: a game from the title takes its save's */
    else if (sys && title_seen && in_session_game()) { title_seen = 0; records_for_game(); }
    if (coop_on && !is_host && !echo_delay && items_applied != g_coop.items && !own_live && !pop_pending && !popping) coop_items_apply();   /* F3 changed it */
    if (coop_on && !echo_delay && sys) {   /* back at the title from a game of this session: it ends (both sides) */
        if (in_session_game()) was_game = 1;
        else if ((sys->tk_flg & 0x8) && was_game) {
            host_log("coop: back at the title, the session ends\n");
            stop();
            snprintf(g_coop.status, sizeof(g_coop.status), tr("Returned to title. Co-op session ended."));
        }
    }
    g_coop.at_title = sys && (sys->tk_flg & 0x8);   /* the lobby hint (overlay) */
    if (!coop_on) {   /* "You appear as" also in single player (appear_on: game.c calls coop_control_remote) */
        appear_on = sys && plp && (g_coop.chr || pc_biorand_appear() > 0 || self_mesh >= 0 || actor_shown(0) || actor_shown(ACTOR_DOUBLE) || cast_n);   /* (cast_n: the cutscene cast's hands and faces, actor_double_tick) */
        if (appear_on) self_frame();
        tx_in_game = 0;
        publish_status();
        return;
    }

    now = net_ms();
    frame_no++;
    if (is_host) records_resolve();   /* the saved partner items of the save our game came from */
    if (!tx_in_game) tx.flags = 0;   /* title, menus, loading */
    else {
        tx.stflg |= plp->stflg & 0x300;   /* shot/footstep marks: guns set theirs after our hook (weapon objects) */
        int v = (int)lroundf(tps_aim_pitch() / COOP_PITCH_STEP);
        tx.pflg = (uint8_t)((plp->flg & 6) |   /* hit or held, after the enemies' steps of this frame */
                            coop_pitch_bits(v > COOP_PITCH_MAX ? COOP_PITCH_MAX : v < -COOP_PITCH_MAX ? -COOP_PITCH_MAX : v));   /* the aim pitch */
        tx.flr = (unsigned char)(plp->flr_no + 3);
    }
    tx.busy = !sys || (sys->tk_flg & 0x28) || (sys->ss_flg & 0x400000) || (sys->cb_flg & 0x1) ? BUSY_NONE :   /* title, (re)start, demo, a door: gone */
              !tx_in_game ? ((sys->cb_flg & 0x40000) ? BUSY_BOX : BUSY_MENU) : !(tx.flags & PF_VISIBLE) ? BUSY_EVENT : BUSY_NONE;
    if (tx.busy == BUSY_NONE && tx_in_game && g_trade.phase == TRADE_OPEN) tx.busy = BUSY_TRADE;
    if (sys) use_frame(); else tx.use = 0;
    tx.type = PKT_POSE;
    memset(&tx_ext, 0, sizeof(tx_ext));
    memcpy(tx_ext.name, g_coop.name, sizeof(tx_ext.name) - 1);
    snprintf(tx_ext.note, sizeof(tx_ext.note), "%.*s", (int)sizeof(tx_ext.note) - 1,   /* (the host's lobby shows it under our model) */
             br_offer == BR_GEN ? "BioRand: generating..." : br_offer == BR_ASK ? "BioRand: asked to generate" : g_coop.note);
    tx_ext.lchr = my_chr() <= COOP_CHR_MAX ? my_chr() : 0;   /* (int8_t; an import: the character played, as in tx.chr; "As played (cutscene)": the cutscene model) */
    tx_ext.lobby = g_coop.lobby;
    if (!is_host) { memcpy(tx_ext.pid, g_coop.pid, 16); memcpy(tx_ext.tag, my_tag, 16); tx_ext.items = (uint8_t)items_wire(); }
    else tx_ext.rec = !g_coop.remember ? REC_OFF : !id_ok ? REC_UNKNOWN : rec_have(id_pid) ? REC_HAVE : REC_NONE;
    tx_ext.br = (uint8_t)(is_host ? biorand_next() : biorand_pack());   /* BioRand (protocol 27): the host's game (at the title: the one
                                                                             its lobby starts); a guest's pack */
    tx_ext.br_seed = biorand_seed() >= 0 ? (uint32_t)biorand_seed() : 0xFFFFFFFFu;
    tx_ext.br_id = biorand_id();
    tx_ext.imp_id = cast_import_id();   /* (protocol 29) the cutscene cast takes imported models only while both have the same */
    tx.opts = is_host ? (g_coop.loot ? OPT_INDIVIDUAL : 0) | (g_coop.trade_freeze ? OPT_TRADE_FREEZE : 0) | (g_coop.important ? OPT_IMPORTANT : 0) |
                        (pc_cfg_no_ink_ribbon() ? OPT_NO_RIBBON : 0) | (g_coop.skip_sync ? OPT_SKIP_SYNC : 0) |
                        (g_coop.gameover_both ? OPT_GAMEOVER_BOTH : 0) | (g_coop.skip_detector ? OPT_SKIP_DETECTOR : 0) |
                        (g_coop.quick_start && !biorand_next() ? OPT_QUICK_START : 0) : 0;   /* BioRand: its own start inventory */
    tx.regroup = regroups;
    tx.seq = frame_no;
    if (echo_delay) {
        echo_ring[frame_no & 255] = tx;
        rx = echo_ring[(frame_no - echo_delay) & 255];
        rx_ext = tx_ext;
        have_rx = frame_no > (unsigned)echo_delay;
    } else {
        enemy_epoch();
        capture_hits();   /* before the snapshots overwrite the puppets' hit bits */
        if (join_dns) join_resolve();
        receive();
        if (have_rx) peer_opts = rx.opts;
        if (!is_host) spawn_pending(1);   /* the host's added enemies that came while our room did not run */
        if (rdv_active()) {
            rdv_frame();
            if (!is_host && rdv_error() && !bye_reason) bye_reason = rdv_error();
        }
        /* signed: receive() stamps rx_at with its own, later clock reading (a tick after `now` read as ~49 days) */
        if (have_rx && (int)(now - rx_at) > 3000) { have_rx = 0; snap_n = 0; g_coop.state = COOP_LOST; host_log("coop: partner timed out\n"); }
        else if (g_coop.state == COOP_LOST && (int)(now - rx_at) > LOST_MS) {   /* no goodbye came: crashed, or the network is gone */
            host_log("coop: partner gone (nothing for %d s)\n", LOST_MS / 1000);
            if (is_host) { partner_gone(0); have_peer = 0; snprintf(left_msg, sizeof(left_msg), tr("The partner timed out")); g_coop.state = COOP_HOSTING; }
            else bye_reason = "Lost the connection to the host";
        }
        if (bye_reason) {   /* the guest's session ends; in game it goes on in single player */
            const char* why = bye_reason;
            host_log("coop: session ended: %s\n", why);
            gameover_ended();
            stop();
            snprintf(g_coop.status, sizeof(g_coop.status), "%s", tr(why));
            tx_in_game = 0;
            p2_show = p2_drawn = 0;
            return;
        }
        /* host: the client's grabs end with the room (ours, or the partner's when it is seen elsewhere; a
           time-out alone keeps them: room loads at low frame rates take that long) */
        if (sys && owned_room >= 0 && is_host && (owned_room != coop_room_key() ||
                                                  (have_rx && (rx.flags & PF_IN_GAME) && !same_room()))) owned_drop();
        own_timeout();
        if (sys && !is_host) grab_check();
        if (sys && plp) grab_failsafe();
        if (sys && plp) hold_watch();
        if (sys && !is_host) gone_frame();
        else if (sys) gone_host_frame();
        apply_snapshots();
        proxy_ok = is_host && p2_show && !p2_busy;   /* a busy partner is no target (as the paused game in single player) */
        if (sys) session();
        if (sys && plp) gameover_frame();
        if (sys && plp) enemy_sync();
        if (trace && is_host && sys && sys->ewk_n && tx_in_game && same_room() && (rx.flags & PF_VISIBLE) && !rx.busy && frame_no - enemy_frame == 30)
            fprintf(stderr, "coop: STALL our enemies have not run for 30 frames while the partner plays in this room (sp %x st %x cb %x gm %x)\n",
                    sys->sp_flg, sys->st_flg, sys->cb_flg, sys->gm_flg);
        if (sys && plp) trade_frame();
        if (sys && plp) ts_frame();
        {   /* the enemies stand still while both players are in the trade screen (the host's setting) */
            int f = g_trade.phase == TRADE_OPEN && have_rx && same_room() && rx.busy == BUSY_TRADE &&
                    (is_host ? g_coop.trade_freeze : (rx.opts & OPT_TRADE_FREEZE) != 0);
            if (f != frozen) host_log("coop: enemies %s (trade screen)\n", f ? "frozen" : "move again");
            frozen = f;
        }
        if (sys && plp && in_session_game()) box_frame();
        if (sys && plp && in_session_game()) grant_frame();
        if (trace && frame_no % 30 == 0 && sys && sys->ewk_n) {
            int i, nt = 0, np = 0, no = 0;
            for (i = 0; i < (int)sys->ewk_n && i < PC_ENE_SLOTS; i++) { nt += tgt[i]; np += ene[i].mode0 != 0; no += owned[i] && !(ene[i].flg & 0x80); }
            fprintf(stderr, "coop: enemies %s n=%d targeting partner=%d puppets=%d ene0=(%.1f,%.1f) hp=%d mode=%d, partner drawn=%d busy=%d, guest grabs=%d\n",
                    is_host ? "host" : puppets() ? "puppet" : "local", sys->ewk_n, nt, puppets() ? np : 0,
                    ene[0].px, ene[0].pz, ene[0].hp, ene[0].mode0, p2_drawn, p2_busy, no);
        }
        if (sys && (sys->tk_flg & 0x28)) atr_key = -1;   /* continue, game over, load in the same room: its windows and records start again */
        if (sys && plp && rom && tx_in_game && in_session_game()) world_sync();
        if (sys) skip_frame();
        if (ring.overflow) resync();
        if (have_peer && !join_dns) send_frame();
    }
    if (sys && plp) self_frame();
    if (sys && plp && in_session_game()) screen_guard();
    if (sys && rom) lighter_light(p2_show && !p2_busy && p2_wpn == 1 && ply2.mlwP && ply2.mlwP->obj_num >= 10);
    if (trace && sys && rom && tx_in_game) world_trace();
    publish();
    publish_status();
    tx_in_game = 0;
    p2_show = p2_drawn = 0;
}

/* the enemy spawner (spawn_glue.c): the client's enemies are the host's puppets, so only the host spawns */
int coop_is_guest(void) { return coop_on && !is_host && !echo_delay; }

/* enemy options (enemyopt_glue.c): a room script instruction's length, as the M8 sweep reads them */
int coop_insn_len(const unsigned char* p) { return insn_len(p); }
