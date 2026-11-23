/* Online co-op (docs/COOP.md "Architecture").
 *
 * Each instance sends its player's pose once per game frame (UDP, at the frame hand-off) and
 * draws the partner as a second player model in the same room. The partner is a puppet: its
 * model is loaded with the game's own loaders (bhReadPlayerData, bhReadWeaponData) by swapping
 * the player context (plp and the sys player buffers) to the player-2 work below, and animated
 * with the stock bhSetMotion/bhCalcModel/bhPutModel from the received motion number and frame.
 * No game state other than the player-2 work is touched; with co-op off nothing here runs.
 * The parts: coop_look.c (the stand-in), coop_own.c (enemy hand-overs), coop_snap.c (enemy snapshots and
 * puppets), coop_fx.c (effects), coop_testaids.c; what they share is in coop_int.h.
 */
#include "coop_int.h"

CoopState g_coop = { .port = COOP_DEFAULT_PORT, .join = "127.0.0.1:7140", .follow = 1, .upnp = 1, .nametag = 1, .items = COOP_ITEMS_HOST, .server = COOP_DEFAULT_SERVER, .trade_freeze = 1, .remember = 1, .peer_rec = -1,
                    .important = 1, .skip_sync = 1, .skip_detector = 1 };
int coop_on;
int appear_on;                          /* co-op off, "You appear as" in use: our-player hooks only */

/* ---------------------------------------------------------------- wire format */
/* pc/src/net/coop_proto.h: every packet is a pose (the version and the partner's character arrive
   continuously; no separate HELLO), followed by `nev` events (reliable and in order: resent until
   acknowledged) and, from the host, `nene` enemy snapshots, spread over datagrams of at most
   COOP_DGRAM_MAX bytes. Everything received passes coop_check_* before it changes anything. */

/* ---------------------------------------------------------------- player 2 work */
/* Same buffers and sizes as bhInitPlayer gives player 1, in the same order (the loaders write
   O_WORK arrays behind the model data). Static, so they lie in the PS2 address window. */
struct coop_p2_s p2 __attribute__((aligned(64)));
unsigned char p2file[1 << 21] __attribute__((aligned(64)));   /* one part file as read from disc */
struct coop_p2sink_s p2sink __attribute__((aligned(64)));
unsigned char p2hair_exp0[0x4000] __attribute__((aligned(64))), p2hair_exp3[0x1000] __attribute__((aligned(64)));
BH_PWORK ply2;
O_WRK p2wpn[2];                  /* right/left hand weapon, as sys->obwp[0..1] for player 1 */
O_WRK p2obj[3];                  /* [2]: hair / Wesker's piece, as sys->obwp[2] for player 1 */
ML_WORK no_model;                /* stands in for a weapon slot that has none yet */
int p2_chr = -1, p2_costume, p2_mfile = -1, p2_wpn = -1, p2_fail = -1, p2_show, p2_drawn;
int p2_busy, p2_seen_room = -1;  /* the partner is busy (BUSY_*); the room it was last shown in */
int p2_seen_gen = -1;            /* ... and its room load (enemy_room_gen) */
float p2_pitch;                  /* the partner's aim pitch shown (radians, Pose.pflg) */
int frozen;                      /* both players are in the trade screen: the enemies stand still (coop_trade_frozen) */

/* a mesh's own bone offsets (node positions 1..21), put back where motions or scripts authored
   for another character wrote theirs */
float self_bone[22][3], p2_bone[22][3];
int p2_other_mesh;
int p2_mdl_kept;                /* load_player: sub-models of the motion file kept under another mesh (their textures) */

void save_bones(ML_WORK* m, float (*bone)[3])
{
    int i;
    for (i = 1; i < (int)m->obj_num && i < 22; i++) memcpy(bone[i], m->objP[i].pos, sizeof(bone[i]));
}

void keep_bones(ML_WORK* m, float (*bone)[3])
{
    int i;
    for (i = 1; i < (int)m->obj_num && i < 22; i++) memcpy(m->objP[i].pos, bone[i], sizeof(bone[i]));
}

/* ---------------------------------------------------------------- session */
static NetAddr peer;
int have_peer, is_host, have_rx, tx_in_game, trace = -1, echo_delay;
Pose rx, tx, echo_ring[256];
static PoseExt rx_ext, tx_ext;
extern int cast_n;                      /* actor_glue.c: cutscene cast records drawn as other models */
unsigned coop_partner_import_id(void) { return coop_on && have_rx ? rx_ext.imp_id : 0; }   /* (cast_glue.c) */
int coop_partner_ply(void) { return coop_on && have_rx && rx.mfile >= 0 ? rx.mfile & 3 : -1; }   /* (cast_glue.c) the character the partner plays, -1 */
unsigned int frame_no, rx_at, join_at, join_frame, join_hinted;
/* ping: the partner's clock in the first datagram of its newest frame here and when that arrived
   (net_rx_us: the network's arrival, not our frame's); the round trip smoothed (ms, < 0: none yet) */
static uint32_t echo_t;
static uint64_t echo_arr;
static double ping_f = -1;
int coop_room_key(void) { return sys->stg_no * 256 + sys->rom_no; }
/* The room data are those of the room we are in: a room change clears rom (room.c bhInitRoom) a few frames
   before the new file is read and set up (bhSetRoom; the frames in between run coop_frame), and before that
   sys->stg_no/rom_no name the next room already while rom is still the last one's; bhFinishRoom keeps the
   numbers of the room set up in the _b fields; a door (cb_flg 0x1) is under way until the new room shows. */
int room_ready(void)
{
    return rom && rom->evtp && !(sys->cb_flg & 0x1) && sys->stg_no == sys->stg_nob && sys->rom_no == sys->rom_nob;
}
/* the partner's pose is from our room: the same stage and room, and the same room case (a room's cases are
   different room files: other enemies, objects, script); a build that does not send its case: any */
int my_case(void)   /* the room loaded (bhFinishRoom keeps it in the _b fields); in a door to another room, the next one's */
{
    return sys->stg_no == sys->stg_nob && sys->rom_no == sys->rom_nob ? sys->rcase_b : sys->rcase;
}
int partner_here(void) { return rx.stg == sys->stg_no && rx.rom == sys->rom_no && (!rx.rcase || rx.rcase - 1 == my_case()); }
static int rx_room(void) { return (rx.stg * 256 + (unsigned char)rx.rom) | rx.rcase << 16; }   /* the partner's room and case (follow) */
/* test aids count the host's frames (the client takes them from its pose): both act together */
unsigned int test_clock(void) { return is_host ? frame_no : have_rx ? rx.seq : 0; }

/* ---------------------------------------------------------------- shared enemies (M2) */
/* The host runs the enemies; per enemy, the nearer player is its target: enemy code reads
   `plp`, so for an enemy that targets the partner, plp is pointed at ply2 (driven by the
   partner's pose) for that enemy's step, and what the enemy did to ply2 is forwarded and
   undone. The client does not run its enemies' AI (except their first, initialising step):
   they are puppets of the host's snapshots, animated locally, and our shots on them go to the
   host as the weapon code's output, applied there once. Only explicit fields are synced: the
   enemy work layout matched across instances in tests, but it depends on the load history. */
CoopRing ring = { .head = 1, .next = 1 };   /* our events until acknowledged (coop_proto.c) */
static unsigned int in_last;            /* the partner's last event applied here */

/* The ring holds every event the partner has not acknowledged; nothing is dropped from it. When it
   is full (a partner that stopped acknowledging for a long time), what follows cannot be ordered
   behind it: the frame's further events are not queued and the frame ends with a resync (resync():
   a new session id, both streams start again, the host's game state goes over) - deliberate and the
   same on both sides, instead of a stream that waits forever for an event that was dropped. */
/* send_frame: when each unacknowledged event went last (net_ms, 0 not yet), by id */
static unsigned int ev_sent[COOP_RING];
void queue_event(Event* e)
{
    int was = ring.overflow;
    e->stg = ev_room >= 0 ? ev_room >> 8 : sys->stg_no;
    e->rom = ev_room >= 0 ? ev_room & 0xFF : sys->rom_no;
    if (e->type != EV_ESTATE) { char w[16]; snprintf(w, sizeof(w), "event %d", e->type); ptr_scan(&e->u, (int)sizeof(e->u), w, e->idx); }   /* (EV_ESTATE: send_estate) */
    if (!coop_ring_push(&ring, e)) {
        if (!was) host_log("coop: %d events not acknowledged by the partner: resynchronising\n", COOP_RING);
        return;
    }
    ev_sent[e->id % COOP_RING] = 0;
    if (trace) fprintf(stderr, "coop: event %u out type=%d idx=%d\n", e->id, e->type, e->idx);
}

/* test aid RECVX_ENEMY_POSE=1: at the hand-off, every zombie's (and every type's that can change hands) motion, root and joint angles as its step left them and
   its joints as drawn (from its position), with the host frame they show (host: this one; guest: the snapshot's) and
   whether the step ran here: pc/tools/coop_enemy_sync_check.sh compares the guest's with the host's */
static void pose_trace(void)
{
    static int on = -1;
    int i, k;
    if (on < 0) on = getenv("RECVX_ENEMY_POSE") && atoi(getenv("RECVX_ENEMY_POSE"));
    if (!on || !sys || !coop_on || echo_delay || !same_room() || (sys->cb_flg & 0x5)) return;
    for (i = 0; i < (int)sys->ewk_n && i < PC_ENE_SLOTS; i++) {
        BH_PWORK* ep = &ene[i];
        BH_PWORK* c = own_child(ep) < PC_ENE_SLOTS ? (BH_PWORK*)ep->exp1 : NULL;
        if (!(ep->flg & 0x1) || !own_type(ep) || (ep->flg & 0x80) || !ep->mode0 || !ep->mlwP || !ep->mlwP->objP || (ep->stflg & 0x41000000)) continue;
        fprintf(stderr, "epose %c seq=%u s=%d m=%d.%d mtn=%u f=%d cmtn=%d cf=%d hp=%d run=%d o=%c p0=%.2f,%.2f,%.2f a=", is_host ? 'H' : 'G', we_run(i) ? frame_no : pose_seq[i], i,
                ep->mode0, ep->mode1, ep->mtn_no, ep->frm_no >> 16, c ? (int)c->mtn_no : -1, c ? c->frm_no >> 16 : -1, ep->hp, step_ran[i], we_run(i) == is_host ? 'H' : 'G',
                ep->mlwP->objP[0].pos[0], ep->mlwP->objP[0].pos[1], ep->mlwP->objP[0].pos[2]);
        for (k = 0; k < (int)ep->mlwP->obj_num && k < 18; k++)
            fprintf(stderr, "%04x%04x%04x%c", ep->mlwP->objP[k].ang[0] & 0xFFFF, ep->mlwP->objP[k].ang[1] & 0xFFFF, ep->mlwP->objP[k].ang[2] & 0xFFFF, k < 17 ? ',' : '\n');
        if (k < 18) fputc('\n', stderr);
        fprintf(stderr, "emtx %c seq=%u s=%d j=", is_host ? 'H' : 'G', we_run(i) ? frame_no : pose_seq[i], i);   /* the joints as drawn, from the enemy's position */
        for (k = 0; k < (int)ep->mlwP->obj_num && k < 18; k++)
            fprintf(stderr, "%.1f/%.1f/%.1f%c", ep->mlwP->owP[k].mtx[12] - ep->px, ep->mlwP->owP[k].mtx[13] - ep->py, ep->mlwP->owP[k].mtx[14] - ep->pz, k < 17 ? ',' : '\n');
        if (k < 18) fputc('\n', stderr);
    }
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
     290, 291  BioRand "Randomize Played as": its marker and Claire's look; its marker, Chris's and Steve's looks
               (biorand_glue.c BR_APPEAR, BR_APPEAR2: any value)
     292..301  a placed typewriter's location text, "LOC=" + UTF-8 (savestore.h SAVE_TEXT_ITM; extras_glue.c sets it for
               the save screen and clears it otherwise: any value)
     320..359  GRANT0: pending event items (word 0 of each entry an item word, its floor in bits 24..31)
     302..319, 360..382  the extra enemies' dead flags (enemies.h enemy_xdead_word, enemyopt_glue.c xrooms): world
               state, so EV_FLAGS sends their changes (words NFLAG..), own_restore and own_put keep the host's
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
static unsigned int xlast[ENEMY_XDEAD_WORDS];   /* and the extra enemies' dead flags (EV_FLAGS words NFLAG..; enemyopt_glue.c) */
static void last_sync(void)
{
    int w;
    memcpy(last, sys->ev_flg, sizeof(last));   /* (flag_block) */
    for (w = 0; w < ENEMY_XDEAD_WORDS; w++) xlast[w] = *enemy_xdead_word(w);
}
static int own_live, own_sent_ok;       /* guest: we play in the host's game of this session; our items went to it (EV_OWN) */
unsigned int my_sid, peer_sid, blob_end;   /* blob_end: host, id of the last chunk sent */
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
int in_session_game(void) { return (sys->tk_flg & 0x80) && !(sys->ss_flg & 0x400000); }   /* game task, not the attract demo */
int coop_session_game(void) { return g_coop.state != COOP_OFF && sys && in_session_game(); }   /* (coop.h) */

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
    enemy_partner_forget();   /* (the partner's randomizer results and extras: another session's) */
    if (sys && run_room >= 0) own_takeover("its session restarted"); else own_reset(1);   /* (protocol 56: every enemy of our room ours) */
    defer_n = 0;
    own_live = own_sent_ok = 0;
    own_done_ok = retry_ok = go_want = go_seen = 0; cont_tid = 0;   /* a new partner session: its items come again */
    in_last = 0; coop_ring_reset(&ring); blob_end = 0; snaps_reset();
    if (bufs) memset(bufs->rb, 0, sizeof(bufs->rb));
    if (bufs) { coop_snaptx_reset(stx); coop_snaprx_reset(srx); }   /* (protocol 45) the bases were the old session's */
    coop_xfer_init(&blob_x, blob_in, sizeof(blob_in));   /* half a transfer of the old session is no use */
    coop_xfer_init(&estate_x, estate_zin, sizeof(estate_zin)); estate_tid = 0;
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
    unsigned int box[160], ic = sys->ic_flg[15], spots[16], xd[ENEMY_XDEAD_WORDS], dead;
    /* our saved items came with the state (the host's record of us): requested, or a rejoin after
       our game ended without a goodbye - not over the game we are playing (a resync) */
    int i, rec = blob_rec == 2 || (blob_rec && (want_rec || own_fixed || !own_valid));   /* 2: a shared game over's continue */
    memcpy(box, &sys->itm[64], sizeof(box));   /* the host's: with shared loot, the one box both use */
    memcpy(spots, &sys->itm[IMP_SPOTS], sizeof(spots));   /* the important item spots the host's game knows */
    for (i = 0; i < ENEMY_XDEAD_WORDS; i++) xd[i] = *enemy_xdead_word(i);   /* the host's extra enemies' dead flags (world state) */
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
    for (i = 0, dead = 0; i < ENEMY_XDEAD_WORDS; i++) dead += __builtin_popcount(*enemy_xdead_word(i) = xd[i]);
    if (dead) host_log("coop: the host's game: %d extra enemies dead\n", dead);
    pc_biorand_stamp();   /* our saves of this game: the session's BioRand state */
    blob_rec = want_rec = 0;   /* this join's; later states (a story switch, a host restart) keep our own */
}

/* this room load's randomizer result (enemyopt_glue.c follow_host: a partner that loads the room while we play in it
   makes the same changes; protocol 52, the guest's too since 53): SPAWN_NOTE_DONE (the room file, how many follow), then a
   SPAWN_NOTE_RAND per record changed. The host's is sent once the guest is in game and in control (its event stream
   would wait while its room does not run), and with every game state we send (a guest that joins loads the room after
   it); the guest's at once */
static struct { SpawnNote n[128]; unsigned char rec[128]; int room, count, gen; unsigned int sent_sid, hash; } kept_out = { .gen = -1 };
void coop_rand_result(int room, const unsigned char* rec, const SpawnNote* n, int count, unsigned int hash)
{
    if (!coop_on || echo_delay) return;
    kept_out.gen = enemy_room_gen; kept_out.sent_sid = 0; kept_out.room = room; kept_out.hash = hash;
    kept_out.count = count < 0 ? 0 : count > 128 ? 128 : count;
    memcpy(kept_out.rec, rec, (size_t)kept_out.count);
    memcpy(kept_out.n, n, sizeof(n[0]) * (size_t)kept_out.count);
}
static void kept_send(void)
{
    Event e = { 0 };
    int k;
    if (kept_out.gen != enemy_room_gen) return;
    e.type = EV_SPAWN;
    e.u.sp.pad = SPAWN_NOTE_DONE; e.u.sp.room = (unsigned short)kept_out.room; e.u.sp.rec = (unsigned char)kept_out.count;
    e.u.sp.variant = (unsigned short)kept_out.hash; e.u.sp.id = (unsigned char)(kept_out.hash >> 16); e.u.sp.mdlver = (unsigned char)(kept_out.hash >> 24);
    queue_event(&e);
    for (k = 0; k < kept_out.count; k++) {
        memset(&e, 0, sizeof(e));
        e.type = EV_SPAWN;
        e.idx = kept_out.rec[k];
        e.u.sp = kept_out.n[k];
        queue_event(&e);
    }
    kept_out.sent_sid = peer_sid;
    host_log("coop: the randomizer's result of room %04d (%d changes) sent to the partner\n", kept_out.room, kept_out.count);
}

/* host: an enemy that is not in the room file (spawn_glue.c) to a guest in our room. Guest (protocol 53): the extra
   enemies it drew itself, the host elsewhere (enemyopt_glue.c), wherever the host is: kept for its next load of the room
   (gx_follow), then coop_extras_done */
void coop_spawn_note(int slot, const SpawnNote* n)
{
    Event e = { 0 };
    if (!coop_on || echo_delay || !have_peer || (is_host && !same_room())) return;
    e.type = EV_SPAWN;
    e.idx = (unsigned char)slot;
    e.u.sp = *n;
    e.u.sp.pad = 0;
    queue_event(&e);
}
void coop_extras_done(int room, int count)
{
    Event e = { 0 };
    if (!coop_on || is_host || echo_delay || !have_peer) return;
    e.type = EV_SPAWN;
    e.u.sp.pad = SPAWN_NOTE_XDONE; e.u.sp.room = (unsigned short)room; e.u.sp.rec = (unsigned char)count;
    queue_event(&e);
    if (count != SPAWN_XDONE_NONE) host_log("coop: our %d extra enemies of room %04d sent to the host (it is elsewhere)\n", count, room);
}
int coop_partner_in_room(void) { return coop_on && !echo_delay && have_peer && sys && have_rx && (rx.flags & PF_IN_GAME) && partner_here(); }
/* host: the guest's EV_SPAWN (protocol 53): its randomizer result and its own extras of a room load, kept, never placed now */
static int host_spawn_note(const Event* e)
{
    const SpawnNote* n = &e->u.sp;
    if (n->pad == SPAWN_NOTE_DONE) enemy_rand_result_begin(n->room, n->rec, n->variant | (unsigned int)n->id << 16 | (unsigned int)n->mdlver << 24);
    else if (n->pad == SPAWN_NOTE_RAND) enemy_rand_result_add(e->idx, n);
    else if (n->pad == SPAWN_NOTE_XDONE) enemy_xres_done(n->room, n->rec);
    else enemy_xres_note(e->idx, n);
    return 1;
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
    if (!is_host && have_peer && !echo_delay && kept_out.sent_sid != peer_sid && kept_out.gen == enemy_room_gen) kept_send();   /* (the guest's, protocol 53) */
    if (!is_host) emodel_guest(0);   /* (the host's import list or ours changed) */
    if (!is_host || !have_peer || echo_delay) { was = 0; return; }
    castoff_sync();
    if (eopts_sid != peer_sid || memcmp(&eopts_sent, &g_enemy_opts, sizeof(eopts_sent))) eopts_send();
    emodel_sync();
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
    kept_send();    /* and our randomizer's result of it */
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
    last_sync();
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
    last_sync();
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
    {   /* the extra enemies' dead flags: ours (world state in item words) */
        int k;
        for (k = 0; k < ENEMY_XDEAD_WORDS; k++)
            memcpy(b + BO(itm) + ((char*)enemy_xdead_word(k) - (char*)sys->itm), enemy_xdead_word(k), 4);
    }
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
int peer_opts = -1;
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
#define OPT_EXTRA_OBJECTS (OPT2_EXTRA_OBJECTS << 8)   /* Pose.opts2's rules in peer_opts' second byte (protocol 33) */
int pc_cfg_extra_objects(void);         /* host.c: the GAMEPLAY setting */
static int rule(int opt)
{
    if (is_host && opt == OPT_EXTRA_OBJECTS) return pc_cfg_extra_objects();
    if (is_host) return opt == OPT_IMPORTANT ? g_coop.important : opt == OPT_SKIP_SYNC ? g_coop.skip_sync : opt == OPT_GAMEOVER_BOTH ? g_coop.gameover_both :
                        opt == OPT_SKIP_DETECTOR ? g_coop.skip_detector : opt == OPT_QUICK_START && g_coop.quick_start && !biorand_next();
    return peer_opts >= 0 && (peer_opts & opt);
}

/* the host's rule "Metal detector: Skipped" (OPT_SKIP_DETECTOR, protocol 22): pc/glue/detector_glue.c in a session, the
   guest's lobby (the host's value) */
int coop_skip_detector(void) { return rule(OPT_SKIP_DETECTOR); }
int coop_quick_start(void) { return rule(OPT_QUICK_START); }   /* "Quick start" (protocol 23): the guest's lobby */
int coop_rule(int opt) { return rule(opt); }   /* the new menu's read-only GAME SETUP of a guest (setup_glue.c): the host's Pose.opts rule */

/* event.c bhPlItemCheck, ps2_McSaveFile.c mcWriteStartSaveFile: typewriters save without an ink ribbon and use none.
   The setting (single player, and the host); a guest plays by the host's rule once it knows it. */
int pc_cfg_no_ink_ribbon(void);         /* host.c: the GAMEPLAY setting */
int pc_no_ink_ribbon(void)
{
    if (coop_on && !is_host && !echo_delay && peer_opts >= 0) return (peer_opts & OPT_NO_RIBBON) != 0;
    return pc_cfg_no_ink_ribbon();
}
/* "Extra Item Boxes and Herbs" (OPT2_EXTRA_OBJECTS, protocol 33; pc/glue/extras_glue.c): the setting, a guest plays by the
   host's rule once it knows it */
int pc_extra_objects(void)
{
    if (coop_on && !is_host && !echo_delay && peer_opts >= 0) return (peer_opts & OPT_EXTRA_OBJECTS) != 0;
    return pc_cfg_extra_objects();
}
int coop_extra_objects(void) { return rule(OPT_EXTRA_OBJECTS); }   /* the guest's lobby: the host's */
/* third person "Headshots & Piercing" (OPT2_HEADSHOTS, protocol 41; tps_glue.c): the setting, a guest plays by the host's
   rule once it knows it (the shooter's head damage and piercing, the head bursts of the side running the zombie) */
int pc_headshots(void)
{
    if (coop_on && !is_host && !echo_delay && peer_opts >= 0) return (peer_opts & (OPT2_HEADSHOTS << 8)) != 0;
    return g_tps.headshots;
}
int coop_headshots(void) { return is_host ? g_tps.headshots : rule(OPT2_HEADSHOTS << 8); }   /* the guest's lobby: the host's */
/* "Break Free Pushes Enemies" (OPT2_SHOVE, protocol 59; shove_glue.c): the setting in effect (Auto: with the Enemy
   Randomizer or Extra Enemies on), a guest plays by the host's once it knows it */
int pc_cfg_break_free(void);            /* host.c: 0 Off, 1 Auto, 2 On */
static int shove_local(void) { const EnemyOpts* o = enemy_opts(); int v = pc_cfg_break_free(); return v == 2 || (v == 1 && o && (o->rand || o->extra)); }
int coop_shove_rule(void)
{
    if (coop_on && !is_host && !echo_delay && peer_opts >= 0) return (peer_opts & (OPT2_SHOVE << 8)) != 0;
    return shove_local();
}
int coop_break_free(void) { return is_host ? shove_local() : rule(OPT2_SHOVE << 8); }   /* the guest's GAME SETUP: the host's (in effect) */

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
   is seen. Shared loot: the partner was first, a pool item goes back out. Key items stay, and the partner's EV_PICKUP
   of that spot (it gives one to both) gives none here (`late_both`, also for a spot whose flag the host's EV_FLAGS
   brought while ours was not sent yet: before, both taking a key at once left the guest two), as does an event's item
   (its flag came from the partner's copy of the event). */
static short late_both[4];              /* 1 + it_flg bits of for-both items our game added late (the partner's pickup gives none) */
static unsigned int late_n;
void coop_item_late(int bit)
{
    int item = sys ? (sys->sb_id == 130 ? 35 : sys->sb_id) : 0;
    if (item > 0 && item < 156 && for_both(item)) late_both[late_n++ & 3] = (short)(bit + 1);
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
    for (w = 0; w < ENEMY_XDEAD_WORDS; w++) {   /* the extra enemies' dead flags (world state in item words: as world flags) */
        unsigned int x = *enemy_xdead_word(w), set = x & ~xlast[w], clr = xlast[w] & ~x;
        if (!(set | clr)) continue;
        xlast[w] = x;
        if (!have_peer) continue;
        e.u.fl.word[e.u.fl.n] = (uint8_t)(NFLAG + w); e.u.fl.set[e.u.fl.n] = set; e.u.fl.clr[e.u.fl.n] = clr;
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
            if (w >= NFLAG) {   /* an extra enemy's dead flag */
                unsigned int* x;
                if (w >= NFLAG + ENEMY_XDEAD_WORDS) continue;
                x = enemy_xdead_word(w - NFLAG);
                if ((c.u.fl.set[j] & ~*x) | (c.u.fl.clr[j] & *x))
                    host_log("coop: the partner's extra enemy dead flags, word %d: set %08x, cleared %08x\n", w - NFLAG, c.u.fl.set[j] & ~*x, c.u.fl.clr[j] & *x);
                *x = (*x | c.u.fl.set[j]) & ~c.u.fl.clr[j];
                xlast[w - NFLAG] = (xlast[w - NFLAG] | c.u.fl.set[j]) & ~c.u.fl.clr[j];
                continue;
            }
            if (trace && w >= 64 && w < 96)
                fprintf(stderr, "coop: ed_flg word %d set %08x clr %08x (the partner's, as sent %08x %08x)\n", w - 64, c.u.fl.set[j], c.u.fl.clr[j], e->u.fl.set[j], e->u.fl.clr[j]);
            if (is_host && item_word(w) && individual()) continue;   /* individual loot: the guest's item words are its own */
            if (item_word(w) && own_copies()) { c.u.fl.set[j] &= ~imp_spots(w); c.u.fl.clr[j] &= ~imp_spots(w); }   /* ours (important items) */
            c.u.fl.set[j] &= ~own_bits(w); c.u.fl.clr[j] &= ~own_bits(w);   /* our own state (a camera view) */
            if (w >= IT_FLG && w < IT_FLG + 16 && !individual() && (c.u.fl.set[j] & f[w] & ~last[w])) {   /* we took that spot too, not sent yet */
                int item = sys->sb_id == 130 ? 35 : sys->sb_id;
                if (is_host) raced[w - IT_FLG] |= c.u.fl.set[j] & f[w] & ~last[w];
                else if (item > 0 && item < 156 && for_both(item))   /* ours is that one: the host's EV_PICKUP of it gives none */
                    late_both[late_n++ & 3] = (short)((w - IT_FLG) * 32 + __builtin_clz(c.u.fl.set[j] & f[w] & ~last[w]) + 1);
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
        for (i = 0; b >= 0 && i < 4 && late_both[i] != b + 1; i++) ;
        if (b >= 0 && i < 4 && e->u.pk.item) {   /* (coop_item_late: ours is that one) */
            late_both[i] = 0;
            host_log("coop: the partner took item %d (flag %d) as our game added it: not twice\n", e->u.pk.item, b);
        } else if (e->u.pk.item && !give_item(e->u.pk.item)) {
            /* no room: the item stays in our room (flag cleared here only, so the partner
               keeps it set; our later pickup of it is not passed on) */
            if (b >= 0 && b < 512) { f[w] &= ~m; last[w] &= ~m; left_behind[b >> 5] |= m; }
            return 1;
        }
        if (b >= 0 && b < 512) {   /* the spot's flag (with individual loot only this one is synced) */
            f[w] |= m; last[w] |= m;
            f[w + 16] &= ~m; last[w + 16] &= ~m;   /* ic_flg: no longer "examined, not taken" (as bhItmCk) */
        }
        if (e->stg == sys->stg_no && e->rom == sys->rom_no && room_ready() && e->u.pk.etc < rom->etc_n) {   /* (loading: its flag builds it) */
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
    /* not out of our game over: a regroup at the end of the host's scene after its Retry ("Game over: Both") pulled us
       while gameover.c still ran (its face models half released) and crashed; its game state takes us there */
    if (go_running()) return;
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
        /* load at the title or while we are in control (not from a menu, an event, a door). While the host's BioRand pack is
           offered or generated (br_offer), a state that came is kept, not dropped: the events behind it in the stream wait
           for it (apply_event), and some wait for our game (EV_GRANT), so a dropped state stalled the stream for good and
           the guest never got into the host's game. It loads once the pack matches (biorand_from_host: BR_NONE), else we
           leave */
        if (pop_pending && br_offer == BR_NONE && ((sys->tk_flg & 0x8) || (tx_in_game && local_visible()) || (go_running() && rule(OPT_GAMEOVER_BOTH)))) load_blob();
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

static unsigned int scd_sig(void) { return rom->evtp ? (unsigned int)(size_t)rom->evtp->scd1 ^ ((unsigned int)(size_t)rom->evtp->evd << 11) ^ ((unsigned int)sys->rcase << 28) : 0; }
/* this room's script is the one analyze() swept */
static int script_known(void) { return rom->evtp && rom->evtp == reg_evtp && scd_sig() == reg_sig; }
/* The partner's world event (EV_TRIG, EV_ATR) for the room we are still loading: it waits (stops the stream)
   until our room is set up (the partner, faster through the door or following us, fired a block of it at entry;
   applied to a room not set up it read a cleared script: the crash in 7/24 after the heliport). 1: wait. */
#define ROOM_WAIT_MS 30000              /* a room that does not get ready by then: its flags only, as another room's */
static int room_wait(int stg, int rom_no)
{
    static unsigned int since;
    if (room_ready() || stg != sys->stg_no || rom_no != sys->rom_no) { since = 0; return 0; }
    if (!since) {
        since = net_ms() | 1;
        host_log("coop: the partner's world event in %d/%d waits until our room is set up\n", stg, rom_no);
    }
    if ((int)(net_ms() - since) < ROOM_WAIT_MS) return 1;   /* (signed: since may be a tick ahead) */
    host_log("coop: our room %d/%d is still not set up: the partner's world event there gives its flags only\n", stg, rom_no);
    since = 0;
    return 0;
}

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
    if (!nwake || echo_delay || !have_rx || (rx.flags & (PF_IN_GAME | PF_VISIBLE)) != (PF_IN_GAME | PF_VISIBLE) || (rx.busy && rx.busy != BUSY_PAUSED) || !rx.flr ||
        (rx.pflg & 6) || !partner_here() || (sys->cb_flg & 0x5) || !script_known()) return 0;
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
    if (!script_known()) return 0;
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
    if (!script_known()) return 0;
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
        coop_atr_merge(&v, r, kind, ix, aset, script_known() ? naset : 0);
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
   flag, a room-flag block then makes the door, so the partner's door stayed locked. The map flags (mp_flg) too:
   the passage to the residence (1/10) makes trigger 1 at the foot of its stairs a door to 2/0 only once 2/0 was
   visited (mp_flg 70); before, the trigger starts the scene of Alfred's laugh, which takes the player through
   (a door event: the partner gets the flags only), so a partner in the room kept a trigger that no longer
   answers and could not go down to the residence until it entered 1/10 again. So when the partner
   changes world flags (ev_flg, ky_flg, mp_flg) while we are in the room, the entry script's record writes (walls,
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
    if (!bits || !(w < 32 || (w >= 96 && w < NFLAG))) return;   /* ev_flg, ky_flg, mp_flg */
    if (w < 112) used_mark(w, bits);   /* a lever: ev_flg, ky_flg */
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
    if (!sys || !rom || !in_session_game() || (sys->tk_flg & 0x28)) return 1;
    if (room_wait(e->stg, e->rom)) return 0;   /* our room is still loading */
    if (!room_ready() || e->stg != sys->stg_no || e->rom != sys->rom_no || e->idx != sys->rcase || e->u.at.sig != scd_sig())
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
    int n = 0, j, k, door = 0;
    for (j = 0; j < ntrace && n < TRIG_OPS; j++) {
        unsigned int o = trace_buf[j];
        if (o >= r->then0 && o < r->then1 && !is_cond(d[o]) && !is_flow(d[o])) ops[n++] = o;
    }
    for (j = 0; j < n; j++)
        if (d[ops[j]] == 20 && evdoor[d[ops[j] + 3]]) door = 1;
    host_log("coop: world event block %04x fired here (gate flag %d/%d), %d actions%s\n", r->start, r->set, r->bit, n,
             have_peer && !echo_delay ? ", sent to the partner" : "");
    if (!have_peer || echo_delay) return;
    /* what its events do to the room follows when they end; not for a block that takes us through a door: the
       partner takes its flags only (coop_scd1) and its entry script builds the room from them (rebuild). Our
       room as the scene left it is ours alone: 1/10's scene of Alfred's laugh marks trigger 1 as stairs in use */
    if (!door) win_open(&win_tx, ev_levels(d, ops, n));
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

    if (!script_known()) analyze();   /* a new room script */
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
    if (plp && rom && in_session_game() && !(sys->tk_flg & 0x28) && room_wait(e->stg, e->rom)) return 0;   /* our room is still loading */
    if (!plp || !rom || !room_ready() || e->stg != sys->stg_no || e->rom != sys->rom_no || e->u.tr.rcase != sys->rcase || !in_session_game() ||
        (sys->tk_flg & 0x28) || e->u.tr.sig != scd_sig()) {
        if (trig.on && e->id == trig.id) trig.on = 0;
        host_log("coop: the partner's world event block %04x is not in our room: its flags only\n", e->u.tr.start);
        return 1;
    }
    if (!script_known()) analyze();
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

/* cheat_glue.c, F2 "Open Item Box": the partner has the shared box open (its box screen, BUSY_BOX) */
int coop_box_busy(void) { return coop_on && have_rx && box_shared() && rx.busy == BUSY_BOX; }

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

/* the new menu's PAUSE > CO-OP > TRADE ITEMS (pause_glue.c, once OPTION has closed): the trade key's press within the next
   frames (a second and a half), as soon as both players are free and next to each other; else at the end, which says why */
static int trade_menu;
void coop_trade_menu(void) { trade_menu = 90; }

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
    { int i; for (i = 0; i < PC_ENE_SLOTS && me; i++) if (own_grab[i] && !we_run(i)) me = 0; }   /* the partner runs a grab of one of ours (Shared Enemy Control: either side) */
    partner = have_rx && same_room() && (rx.flags & (PF_IN_GAME | PF_VISIBLE)) == (PF_IN_GAME | PF_VISIBLE) &&
              (rx.busy == BUSY_NONE || rx.busy == BUSY_PAUSED || rx.busy == BUSY_TRADE) && rx.mode0 == 1 && rx.mset == MSET_OWN;
    near = have_peer && have_rx && tx_in_game && trade_near();
    if (trade_menu && g_trade.phase == TRADE_IDLE) {   /* the pause menu's TRADE ITEMS: the trade key, once we can (or why not) */
        if ((me && partner && near) || trade_menu == 1) press = 1;
        trade_menu = press ? 0 : trade_menu - 1;
    } else trade_menu = 0;
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

void coop_test_press(unsigned int bits) { pad_test |= bits; }   /* test aids (extras_glue.c RECVX_EXTRAS pad): pressed at the next pad read */

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
    if (e->type == EV_EMODEL) { if (!is_host) { em_in.e = *e; em_in.have = 1; emodel_guest(1); } return 1; }
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
    if (e->type == EV_SPAWN) return is_host ? host_spawn_note(e) : spawn_note_apply(e->idx, &e->u.sp, e->stg, e->rom);
    if (e->type == EV_ESTATE) {   /* EV_GRAB / EV_UNGRAB name the transfer they need complete */
        if (e->u.blob.total >= 2 && e->u.blob.total <= ESTATE_Z)   /* (protocol 56: packed) */
            if (coop_xfer_chunk(&estate_x, e->u.blob.tid, e->u.blob.total, e->u.blob.off, e->u.blob.len, e->u.blob.data) >= 0) estate_tid = 0;
        return 1;
    }
    if (e->type == EV_GRAB) {
        if (sys && plp) grab_in(e);   /* (either way: the guest runs enemies too) */
        return 1;
    }
    if (e->type == EV_ENEOWN) {
        if (sys && plp && (e->u.gr.state == 3 || e->u.gr.state == 4)) hin_apply(e);   /* (the guest's room) */
        else if (sys && plp && e->u.gr.state == 5) own_room_msg(e);
        else if (sys && plp) own_apply(e);   /* (protocol 56: hand-overs whoever runs it) */
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
        if (!sys || i >= PC_ENE_SLOTS || we_run(i)) return 1;
        if (e->u.gr.state == 2) {   /* the grab is over, the partner's game runs the enemy on */
            own_grab[i] = 0;
            return 1;
        }
        if (!own_grab[i]) c = own_child(&ene[i]);   /* (Shared Enemy Control: one the partner ran between attacks, given up as it leaves) */
        own_set(i, 1);
        own_grab[i] = 0;
        owned_child[i] = 255;
        if (c < PC_ENE_SLOTS) own_set(c, 1);
        own_wait[i] = 0;
        if (!(ene[i].flg & 0x1)) return 1;
        if (e->stg != sys->stg_no || e->rom != sys->rom_no) {
            /* stamped with another room (an older guest's door hand-back: its next room), the enemy still in ours: it ran there
               from the puppet's modes on its own stale work (a Hunter mid-jump on a floor-area pointer of 0) - released */
            if (is_host && !e->u.gr.state && owned_room == coop_room_key()) {
                pc_enemy_unhold(&ene[i]);
                host_log("coop: enemy %d is back from the partner (another room's stamp): released\n", i);
            }
            return 1;
        }
        own_ours(i, c < PC_ENE_SLOTS ? c : -1);
        if (!e->u.gr.state || c != child_here(i, e->u.gr.child) || !estate_take(e->u.gr.tid, ene_len(&ene[i], c)) ||
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
    if (e->type == EV_ENE_HIT && sys && plp) {   /* (by the room of our enemies: in a door we hand them over, a hit then is the receiver's) */
        int i = e->idx, ours = (e->stg * 256 + e->rom) == (run_room & 0xFFFF);
        if (!ours || i >= (int)sys->ewk_n || i >= PC_ENE_SLOTS || !(ene[i].flg & 0x1)) {
            if (!ours && !(e->u.eh.flg & COOP_HIT_FWD) && have_peer && (e->stg != sys->stg_no || e->rom != sys->rom_no)) {   /* (we left that
                                                    room: the partner plays there, it runs them) */
                Event f = *e;
                f.u.eh.flg |= COOP_HIT_FWD;
                ev_room = e->stg * 256 + e->rom; queue_event(&f); ev_room = -1;
                own_n[7]++;
            }
            return 1;
        }
        hit_t[i][1] = frame_no + 1;   /* (Shared Enemy Control: who it is engaged with) */
        /* (protocol 56) a hit always reaches the game that runs the enemy, once: here when we run it (not one we handed over
           and run on only until the partner runs it from our state), kept when its hand-over to us is under way, else on to
           the partner (it ran it when the shot was fired here, or runs it now: a grab, a hand-over crossed it). Before, a hit
           on an enemy we did not run (yet) was dropped: hits lost around grabs and hand-overs */
        if (we_run(i) && !giving[i]) hit_apply(e);
        else if (take_at[i]) defer_hit(e);
        else if (!(e->u.eh.flg & COOP_HIT_FWD)) {
            Event f = *e;
            f.u.eh.flg |= COOP_HIT_FWD;
            ev_room = run_room & 0xFFFF; queue_event(&f); ev_room = -1;   /* (in a door ours may be the next room's already) */
            own_n[7]++;
            if (trace) fprintf(stderr, "coop: hit on enemy %d passed on to the partner's game, which runs it\n", i);
        } else host_log("coop: own: a hit on enemy %d came back from the partner, and we do not run it either: dropped\n", i);
        return 1;
    }
    if (!sys || !plp || e->stg != sys->stg_no || e->rom != sys->rom_no) return 1;
    if (e->type == EV_PLY_HIT) {   /* (protocol 56: the guest runs enemies too) */
        /* held here (a grab our game runs): as in single player, where a held player is no target, no other enemy's blow (the
           sender saw the stand-in free: its pose of the grab was not there yet) */
        if (plp->mode0 == 4 || plp->mode0 == 6) {
            host_log("coop: enemy %d's hit of %d on our player ignored: held\n", e->idx, e->u.ph.hp_delta);
            return 1;
        }
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
    if (coop_on && have_peer && !echo_delay) { send_bye(0); net_flush(); }
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
    coop_ring_reset(&ring); in_last = 0; snaps_reset(); proxy_ok = 0; ver_msg[0] = 0;
    coop_xfer_init(&blob_x, blob_in, sizeof(blob_in));
    coop_xfer_init(&estate_x, estate_zin, sizeof(estate_zin)); estate_tid = 0;
    own_reset(1);   /* (single player again: the hidden ones shown) */
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
    emodel_from_host(NULL);
    em_in.have = 0;
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
    bufs_free();
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
    snaps_reset();
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
    snaps_reset();
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
    NetAddr from;
    Pose p;
    const Event* ev;
    const unsigned char* sb;
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
        sb = buf + sizeof(Pose) + nev * sizeof(Event);
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
                snaps_reset();
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
        coop_snaptx_ack(stx, p.snap_ack);
        /* protocol 56: an event that comes ahead of a gap (a datagram lost or overtaken by a later one) is kept until the
           gap is filled, then applied in order. Before, it was dropped and came again only once its answer was overdue
           (the round trip + 80 ms): with jitter that reorders datagrams a hand-over's state (dozens of events) waited for
           one resend after another, up to 8 s, and every event behind it with it */
        for (k = 0; k < nev; k++)
            if (ev[k].id - in_last - 1 < COOP_RING && bufs->rb[ev[k].id % COOP_RING].id != ev[k].id) bufs->rb[ev[k].id % COOP_RING] = ev[k];
        while (bufs->rb[(in_last + 1) % COOP_RING].id == in_last + 1) {
            Event* e = &bufs->rb[(in_last + 1) % COOP_RING];
            if (!apply_event(e)) break;   /* not yet: kept */
            e->id = 0;
            in_last++;
        }
        /* a frame comes in one or more datagrams, each with the pose: each datagram's snapshots are kept under its
           frame (also a late one's), the playout learns the path from every arrival; an older frame's pose changes
           nothing */
        coop_playout_arrive(&playout, p.seq, arr, net_rx_exact());
        {
            int room = (p.stg * 256 + (unsigned char)p.rom) | p.rcase << 16;
            if ((nene || (p.flags & PF_IN_GAME)) && (!have_rx || (int)(p.seq - rx.seq) >= 0) && room != snap_room) {   /* the partner's room: other enemies */
                memset(hist, 0, PC_ENE_SLOTS * sizeof(*hist)); memset(hist_applied, 0, sizeof(hist_applied)); snap_room = room;
            }
            if (nene && room == snap_room) {   /* (protocol 45: decoded against the bases we hold, late frames too) */
                EneSnap* sn = bufs->rx_sn;
                int got = coop_snaprx_take(srx, p.seq, sb, n - (int)(sb - buf) - ext * (int)sizeof(PoseExt), nene, sn);
                for (k = 0; k < got; k++) {
                    int i = sn[k].idx;
                    coop_hist_put(&hist[i], p.seq, &sn[k]);
                    if (own_ms[i] && !we_run(i)) {   /* the new runner's first snapshot of one we handed over */
                        unsigned int ms = net_ms() - own_ms[i];
                        own_n[5] += ms; own_n[6]++;
                        own_ms[i] = 0;
                        host_log("coop: p3: enemy %d: the partner's first snapshot %u ms after the hand-over\n", i, ms);
                    }
                }
            }
        }
        if (have_rx && (int)(p.seq - rx.seq) < 0) continue;
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
#define EV_BURST 96                     /* events a frame (receivers check COOP_EV_FRAME a datagram; one holds at most 11) */
#define EV_RESEND 64                    /* of them sent again */
#define EV_AGAIN 30                     /* ms: a new one goes once more in the next frame */
#define EV_RTO 80                       /* ms over the round trip: the partner answers with its next frame, we read it at ours */

static void send_frame(void)
{
    static unsigned char buf[COOP_DGRAM_MAX];
    static Event evs[EV_BURST];
    EneSnap* snaps = bufs->tx_snaps;
    static unsigned char recs[COOP_SNAP_BUDGET + COOP_SNAP_REC_MAX];
    static short rlen[PC_ENE_SLOTS];
    static int stats = -1, frames, dgrams, evn, snn, bytes, maxb, peakd, peakb, snb, sfull, sleft;
    static float qe_pos, qe_frm;
    unsigned int id;
    int ne = 0, ns, nr = 0, used = 0, ie = 0, ir = 0, ro = 0, part = 0, first = 1, fd = 0, fb = 0;
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
    tx.snap_ack = srx->ack;
    {   /* the events: each new one now (a grab's state goes whole in its frame: EV_BURST) and once more in the next
           frame (a lost datagram of a burst then costs a frame, not a round trip: the receiver takes events only in
           order); one not acknowledged again once its answer is overdue (EV_RTO); at most EV_RESEND sent again a frame.
           Before, every unacknowledged one went again in every frame, at most 16 a frame: the 72 chunks of a grab's
           state took 5 round trips. */
        unsigned int now = net_ms(), rto = (unsigned int)(ping_f >= 0 ? ping_f : 200) + EV_RTO, re = 0;
        if (rto > 600) rto = 600;
        for (id = ring.head; id != ring.next && ne < EV_BURST && !drop_now(1); id++) {
            unsigned int* t = &ev_sent[id % COOP_RING];
            if (*t && (now - *t < rto || re++ >= EV_RESEND)) continue;
            *t = (*t ? now : now - rto + EV_AGAIN) | 1;
            evs[ne++] = *coop_ring_get(&ring, id);
        }
    }
    owners_sync();
    ns = build_snapshots(snaps);
    snap_prioritise(snaps, ns);
    while (nr < ns && used < COOP_SNAP_BUDGET) {   /* coded = sent (coop_snaptx_put notes it as this frame's) */
        int full = 0, r = coop_snaptx_put(stx, tx.seq, &snaps[nr], recs + used, (int)sizeof(recs) - used, &full);
        if (!r) break;
        if (stats > 0) {   /* RECVX_COOP_STATS: the quantization's error, of what was coded */
            CoopSnapQ q;
            EneSnap d;
            int k;
            coop_snap_quant(&snaps[nr], &q);
            if (coop_snap_dequant(&q, snaps[nr].idx, &d)) {
                float e = fmaxf(fabsf(d.px - snaps[nr].px), fmaxf(fabsf(d.py - snaps[nr].py), fabsf(d.pz - snaps[nr].pz)));
                for (k = 0; k < 7; k++) e = fmaxf(e, fabsf(d.watr[k] - snaps[nr].watr[k]));
                qe_pos = fmaxf(qe_pos, e);
                qe_frm = fmaxf(qe_frm, (snaps[nr].frm_no - d.frm_no) / 65536.0f);
            }
        }
        snap_prio[snap_grp[snaps[nr].idx]] = 0;
        rlen[nr++] = (short)r; used += r; sfull += full;
    }
    sleft += ns - nr;   /* (they wait for the next frame, by priority) */
    snb += used;
    {   /* ping: our clock as it goes out, and how long we held the partner's (real time, not frames) */
        uint64_t t = net_us();
        tx.t_ms = (uint32_t)(t / 1000) ? (uint32_t)(t / 1000) : 1;
        tx.echo_ms = have_rx ? echo_t : 0;
        tx.echo_age = have_rx && t > echo_arr ? (uint32_t)((t - echo_arr + 500) / 1000) : 0;
    }
    do {
        int room = COOP_DGRAM_MAX - (via_rdv(&peer) ? rdv_overhead() : COOP_DIRECT_OVER) - (int)sizeof(Pose) - (first ? (int)sizeof(PoseExt) : 0), a = 0, b = 0, bl = 0, off = sizeof(Pose);
        while (ie + a < ne && room >= (int)sizeof(Event)) { a++; room -= sizeof(Event); }
        room -= COOP_SNAP_HDR;   /* records whole, at most 255 (nene), the frame's parts 0..15 (the budget keeps them to 5) */
        while (ir + b < nr && b < 255 && part < 16 && room >= rlen[ir + b]) { room -= rlen[ir + b]; bl += rlen[ir + b]; b++; }
        tx.nev = a;
        tx.nene = b;
        tx.flags = (tx.flags & ~PF_EXT) | (first ? PF_EXT : 0);
        memcpy(buf, &tx, sizeof(tx));
        memcpy(buf + off, &evs[ie], a * sizeof(Event)); off += a * sizeof(Event);
        if (b) {
            buf[off] = (unsigned char)bl; buf[off + 1] = (unsigned char)(bl >> 8);
            buf[off + 2] = (unsigned char)(part++ | (ir + b == nr ? 0x80 : 0));
            memcpy(buf + off + COOP_SNAP_HDR, recs + ro, bl); off += COOP_SNAP_HDR + bl;
        }
        if (first) { memcpy(buf + off, &tx_ext, sizeof(tx_ext)); off += sizeof(tx_ext); }
        if (!drop_now(0)) xsend(&peer, buf, off);
        ie += a; ir += b; ro += bl; first = 0;
        dgrams++; bytes += off; if (off > maxb) maxb = off;
        fd++; fb += off + (via_rdv(&peer) ? rdv_wire_extra() : COOP_DIRECT_OVER);
    } while (ie < ne || (ir < nr && part < 16));
    if (fd > peakd) peakd = fd;
    if (fb > peakb) peakb = fb;
    if (stats < 0) anc_stats = stats = getenv("RECVX_COOP_STATS") != NULL;   /* test aid: every 300 frames what we sent and the ping */
    if (stats && (frames++, evn += ne, snn += nr, frames == 300)) {
        host_log("coop: snapstats: %d snapshot bytes per frame, %.1f records per frame (%.1f bytes each), %.2f full per frame, %.2f waiting per frame, "
                 "quantization error max %.4f units / %.5f frames; taken: %u records without their base, ack %u of seq %u; "
                 "anchors: %u of %u frames predicted to 1/256 (%u of %u over 2+ frames)\n",
                 snb / frames, snn / (float)frames, snn ? snb / (float)snn : 0.0f, sfull / (float)frames, sleft / (float)frames, qe_pos, qe_frm,
                 srx->n_bad, srx->ack, srx->seq, anc_ok, anc_n, anc_far_ok, anc_far);
        snb = sfull = sleft = 0; qe_pos = qe_frm = 0; anc_n = anc_ok = anc_far = anc_far_ok = 0;
        host_log("coop: stats: %d frames, %.2f datagrams per frame, avg %d bytes per datagram (max %d), %d bytes per frame, "
                 "%.1f events and %.1f snapshots per frame, ping %.2f ms, room %d/%d, %d enemy slots, %u bad / %u foreign datagrams, path %s (+%d bytes per datagram on the wire), "
                 "busiest frame %d datagrams / %d bytes on the wire, effects sent %u (dropped %u)\n",
                 frames, dgrams / (float)frames, bytes / dgrams, maxb, bytes / frames, evn / (float)frames, snn / (float)frames, ping_f,
                 sys ? sys->stg_no : -1, sys ? sys->rom_no : -1, sys ? (int)sys->ewk_n : 0, rx_bad, rx_strangers,
                 via_rdv(&peer) ? rdv_path() : "direct (address)", via_rdv(&peer) ? rdv_wire_extra() : COOP_DIRECT_OVER, peakd, peakb, fx_sent, fx_dropped);
        fx_sent = fx_dropped = 0;
        frames = dgrams = evn = snn = bytes = maxb = peakd = peakb = 0;
        {   /* hand-overs, the glitch (until the new runner's first snapshot), what the enemy steps cost here */
            int k, mine_n = 0;
            for (k = 0; sys && k < (int)sys->ewk_n && k < PC_ENE_SLOTS; k++) mine_n += (ene[k].flg & 0x81) == 0x1 && we_run(k);
            host_log("coop: p3 stats: on %d, we run %d enemies, handed over %u, taken %u, refused %u, given back leaving %u, taken back %u, "
                     "glitch avg %u ms (%u), windows %u: frames carried on %u, held %u, correction %.2f units, frames back %u, "
                     "enemy steps run here %u at %.1f us, puppets %u at %.1f us, cpu %ld ms\n",
                     shared_on(), mine_n, own_n[0], own_n[1], own_n[2], own_n[3], own_n[4], own_n[6] ? own_n[5] / own_n[6] : 0, own_n[6],
                     win_st[2], win_st[0], win_st[1], win_st[3] / 100.0, win_st[4],
                     step_cnt[0], step_cnt[0] ? step_ns[0] / 1000.0 / step_cnt[0] : 0.0, step_cnt[1], step_cnt[1] ? step_ns[1] / 1000.0 / step_cnt[1] : 0.0,
                     (long)(clock() / (CLOCKS_PER_SEC / 1000)));
            memset(step_ns, 0, sizeof(step_ns)); memset(step_cnt, 0, sizeof(step_cnt)); memset(win_st, 0, sizeof(win_st));
        }
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
        appear_on = sys && plp && (my_chr() || pc_biorand_appear(0) > 0 || self_mesh >= 0 || self_played() != self_loaded() || actor_shown(0) || actor_shown(ACTOR_DOUBLE) || cast_n);   /* (cast_n: the cutscene cast's hands and faces, actor_double_tick) */
        if (appear_on) self_frame();
        tx_in_game = 0;
        publish_status();
        return;
    }

    if (!bufs_get()) { host_log("coop: out of memory: the session ends\n"); stop(); return; }
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
    tx.legs = tps_legs_me.on ? coop_legs_pack(tps_legs_me.mtn, tps_legs_me.yaw) : 0;   /* walking under the aim (tps_glue.c) */
    tx.legs_frm = tx_in_game && tps_legs_me.on ? (uint8_t)((tps_legs_me.frm >> 16) + 1) : 0;
    tx.busy = !sys || (sys->tk_flg & 0x28) || (sys->ss_flg & 0x400000) ? BUSY_NONE :   /* title, (re)start, demo: gone */
              (sys->cb_flg & 0x1) || !room_ready() ? BUSY_DOOR :   /* (protocol 56; before, BUSY_NONE: gone) a door, the next room loading */
              !tx_in_game ? ((sys->cb_flg & 0x40000) ? BUSY_BOX : BUSY_MENU) : !(tx.flags & PF_VISIBLE) ? BUSY_EVENT :
              /* (protocol 56) in game, its enemies stand still (a scene, a message): it hands over what it runs, takes nothing */
              aid_scene_on() || !((sys->sp_flg & 0x2) || coop_enemies_run()) ? BUSY_PAUSED : BUSY_NONE;
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
    tx.opts2 = is_host ? (pc_cfg_extra_objects() ? OPT2_EXTRA_OBJECTS : 0) | (g_tps.headshots ? OPT2_HEADSHOTS : 0) | (shared_env() ? OPT2_OWNERSHIP : 0) |
                         (shove_local() ? OPT2_SHOVE : 0) : 0;   /* (protocols 33, 41, 46, 59) */
    tx.regroup = regroups;
    tx.seq = frame_no;
    if (echo_delay) {
        echo_ring[frame_no & 255] = tx;
        rx = echo_ring[(frame_no - echo_delay) & 255];
        rx_ext = tx_ext;
        have_rx = frame_no > (unsigned)echo_delay;
    } else {
        enemy_epoch();
        pose_trace();
        fx_live_trace();
        capture_hits();  /* before the snapshots overwrite the puppets' hit bits */
        if (join_dns) join_resolve();
        receive();
        if (have_rx) peer_opts = rx.opts | rx.opts2 << 8;
        if (!is_host) spawn_pending(1);   /* the host's added enemies that came while our room did not run */
        if (rdv_active()) {
            rdv_frame();
            if (!is_host && rdv_error() && !bye_reason) bye_reason = rdv_error();
        }
        /* signed: receive() stamps rx_at with its own, later clock reading (a tick after `now` read as ~49 days) */
        if (have_rx && (int)(now - rx_at) > 3000) {   /* (what it ran is ours: own_frame) */
            have_rx = 0; snaps_lost(); g_coop.state = COOP_LOST; host_log("coop: partner timed out\n");
        }
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
        own_timeout();
        if (sys) grab_check();
        if (sys && plp) grab_failsafe();
        if (sys && plp) hold_watch();
        if (sys && !is_host) gone_frame();
        else if (sys) { gone_host_frame(); hin_host_frame(); }
        apply_snapshots();
        proxy_ok = p2_show && !p2_busy;   /* a busy partner is no target (as the paused game in single player); the guest's enemies target the host too */
        own_frame();
        if (sys) session();
        if (sys && plp) gameover_frame();
        if (sys && plp) enemy_sync();
        if (trace && is_host && sys && sys->ewk_n && tx_in_game && same_room() && (rx.flags & PF_VISIBLE) && (!rx.busy || rx.busy == BUSY_PAUSED) && frame_no - enemy_frame == 30)
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
            for (i = 0; i < (int)sys->ewk_n && i < PC_ENE_SLOTS; i++) { nt += tgt[i]; np += ene[i].mode0 != 0 && !we_run(i); no += owned[i] && !(ene[i].flg & 0x80); }
            fprintf(stderr, "coop: enemies %s n=%d targeting partner=%d puppets=%d ene0=(%.1f,%.1f) hp=%d mode=%d, partner drawn=%d busy=%d, guest grabs=%d\n",
                    is_host ? "host" : "guest", sys->ewk_n, nt, np,
                    ene[0].px, ene[0].pz, ene[0].hp, ene[0].mode0, p2_drawn, p2_busy, no);
        }
        if (sys && (sys->tk_flg & 0x28)) atr_key = -1;   /* continue, game over, load in the same room: its windows and records start again */
        if (sys && plp && rom && tx_in_game && in_session_game()) world_sync();
        if (sys) skip_frame();
        if (ring.overflow) resync();
        if (have_peer && !join_dns) { fx_tick(); send_frame(); }
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
