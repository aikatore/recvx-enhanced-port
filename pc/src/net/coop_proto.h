/* Online co-op wire format (protocol 31, docs/COOP.md "Protocol 31" and "Security") and
   the checks every received packet passes before it may change anything. No game state here:
   pc/glue/coop_glue.c applies what passes; pc/tests/test_coop.c feeds malformed packets. */
#ifndef RECVX_COOP_PROTO_H
#define RECVX_COOP_PROTO_H
#include <stdint.h>
#include "../host/enemies.h"
#include "rdv_proto.h"
#ifdef __cplusplus
extern "C" {
#endif

#define COOP_MAGIC 0x504F4F43u          /* "COOP" */
#define COOP_VERSION 31                /* 11: join code, datagrams <= 1200 bytes, checked events, EV_ATR; 12: EV_FX; 13: EV_GRANT;
                                           14: EV_TSCREEN, BUSY_TRADE, OPT_TRADE_FREEZE; 15: EV_GONE;
                                           16: player id and rejoin tag in PoseExt, EV_OWN, saved inventories (EV_BLOB pad);
                                           17: TS_WANT, TS_DECLINE (trade screen item requests);
                                           18: Pose.voice_n/voice (the sender's player voices);
                                           19: game rules OPT_IMPORTANT, OPT_NO_RIBBON, OPT_SKIP_SYNC; PF_CINE, PF_SKIP and
                                               EV_SKIP (synchronized cutscene skip); unknown flag and option bits refused;
                                           20: EV_EOPTS variety and event enemies (the randomizer's choices changed);
                                           21: COOP_HIT_PRECISE (third person "Precise hits");
                                           22: OPT_GAMEOVER_BOTH, PF_GAME_OVER, EV_BLOB pad 2 (a shared game over's continue),
                                               EV_OWN tid (the last game state the guest loaded); OPT_SKIP_DETECTOR;
                                           23: OPT_QUICK_START (Pose.opts is full: a further rule needs a field of its own);
                                           24: enemy slots 0..254 (COOP_ENE_SLOTS), EV_FX's FX_* references marked in `refs`;
                                           25: Pose.use (the trigger the sender is using: one player at a puzzle at a time);
                                           26: EneSnap.id (the enemy type in the slot: a snapshot only for the same type);
                                           27: PoseExt.br, br_seed, br_id (BioRand: the host's choice, each side's pack);
                                           28: direct sessions encrypted after a handshake bound to the whole join code
                                               (PKT_HELLO, CoopDirect); Pose.auth gone (rsv, zero); COOP_REJ_STATE;
                                               generic EV_FX only for the effects in coop_fx_rules, each field checked;
                                           29: EV_EOPTS cast, cast_all, cast_assign, cast_imports (the cutscene cast), PoseExt.imp_id;
                                           30: EV_BRSEED (the host's BioRand seed string and CLI digest: a guest can generate the pack);
                                           31: EV_CASTOFF, EV_EOPTS cast_all 2 (the cutscene cast's "Custom" models) */
#define COOP_DGRAM_MAX 1200             /* bytes per datagram (below every internet path MTU) */
#define COOP_EV_FRAME 16                /* events per frame (over the frame's datagrams) */
#define COOP_SNAP_FRAME 48              /* enemy snapshots per frame */
#define COOP_GONE_MAX 26                /* EV_GONE: enemy slots per event */
#define COOP_ENE_SLOTS 255              /* enemy slots 0..254 (pc_prelude.h PC_ENE_SLOTS); 255 in a slot byte: none */
#define COOP_TRIG_OPS 48                /* actions of one world event block (retail: at most 33) */
#define COOP_CODE_LEN 8                 /* join code: 8 of the 32 characters below (40 bits) */

enum { PKT_POSE = 1, PKT_BYE = 2, PKT_REJECT = 3,
       PKT_HELLO = 4, PKT_HELLO_ACK = 5 };   /* direct sessions' handshake (CoopHello, protocol 28): never inside a session */
enum { PF_IN_GAME = 1, PF_VISIBLE = 2, PF_ROOM_MTN = 4, PF_WANT_BLOB = 8, PF_EXT = 16,
       PF_CINE = 32,                    /* in a cutscene its skip button could skip now */
       PF_SKIP = 64,                    /* ... and we pressed it: we wait for the partner (synchronized skip) */
       PF_GAME_OVER = 128,              /* our game over runs (its screen, before CONTINUE / NO is answered) */
       PF_ALL = 255 };
enum { OPT_INDIVIDUAL = 1, OPT_TRADE_FREEZE = 2,   /* Pose.opts: the host's session options (its game rules) */
       OPT_IMPORTANT = 4,               /* shared loot: important items (weapons, ink ribbons) are each player's own to take */
       OPT_NO_RIBBON = 8,               /* typewriters save without an ink ribbon */
       OPT_SKIP_SYNC = 16,              /* a cutscene both play is skipped once both pressed skip */
       OPT_GAMEOVER_BOTH = 32,          /* one player's game over is both players'; the host's CONTINUE / NO decides */
       OPT_SKIP_DETECTOR = 64,          /* the prison's metal detector (0/9) never goes off, its security boxes rest */
       OPT_QUICK_START = 128,           /* a new-game inventory starts with the manual read and the knife from the first room */
       OPT_ALL = 255 };
enum { BUSY_NONE, BUSY_EVENT, BUSY_MENU, BUSY_BOX, BUSY_TRADE };
enum { MSET_OWN = -1, MSET_ROOM = -2, MSET_NONE = -3 };
#define MNW_ROOM 0x7FFE
#define MNW_KEEP 0x7FFF
#define COOP_MNW_MAX 512                /* motions in a set */

/* Every datagram starts with this pose (little-endian, packed by the compiler as on all supported
   hosts: x86). A frame's events and snapshots are spread over as many datagrams as needed; each
   carries the pose, so each is useful on its own. In a session every datagram but PKT_REJECT is
   sealed (XChaCha20-Poly1305): directly with CoopDirect's keys, through a server with rdv_session's. */
typedef struct Pose {
    uint32_t magic;
    uint16_t version;
    uint8_t type, flags;
    uint32_t sid;                       /* sender's session: a new one restarts the event streams */
    uint32_t rsv;                       /* zero (protocol 27 and before: a 32-bit join-code proof) */
    uint32_t seq;                       /* sender's frame counter: older poses are dropped */
    uint32_t t_ms, echo_ms, echo_age;   /* ping: our clock, last clock seen from the peer, ms held */
    int8_t chr, costume, stg, rom;
    int8_t pos;                         /* sys->pos_no: where the room was entered (room follow) */
    uint8_t opts;                       /* host: OPT_*, the client follows them */
    uint8_t regroup;                    /* host: counts regroups (cutscene, character switch) */
    int8_t mfile;                       /* the sender's own part file - 10 (ply_id + costume*4) */
    float px, py, pz;
    int32_t ax, ay, az;
    int32_t mtn_no, frm_no, mtn_md, hokan_count, hokan_rate;
    int16_t wpnr_no, hp;
    uint8_t mode0, mode1, nev, nene;    /* events and snapshots in this datagram */
    uint32_t stflg;
    uint32_t ev_ack;                    /* highest event id applied from the peer */
    int16_t mset;                       /* MSET_OWN, MSET_ROOM, or the enemy slot whose set mtn_no indexes */
    uint8_t busy;                       /* BUSY_* */
    union { uint8_t reject, rcase; };   /* PKT_REJECT, PKT_BYE: COOP_REJ_*. PKT_POSE: the room case of the room the sender
                                           is in, + 1 (0: not sent, a build before it): its room file RM_<stg><rom><case> */
    uint8_t voice_n, voice;             /* counts the sender's player voices; the last one's program (SE 0x400 + voice) */
    uint8_t pflg;                       /* the sender's player flg & 6 (2, 4: being hit or held: enemies wait); bits 0 and
                                           3..7: its aim pitch (coop_pitch_get; 0 straight, also from a build before it) */
    uint8_t flr;                        /* the sender's floor (flr_no + 3); 0: not sent (a build before this field) */
    uint8_t use;                        /* the room trigger (etc record) the sender examined and is busy with + 1, 0 none:
                                           the partner may not examine it meanwhile (a puzzle, a terminal) */
    uint8_t spare[3];
} Pose;
/* Pose.pflg's other bits (receivers before them read pflg & 6): the pitch the sender's upper body is bent by
   (third-person free aim; the fixed cameras' aim up/down is a motion), -31..31 steps of COOP_PITCH_STEP radians */
#define COOP_PITCH_MAX 31
#define COOP_PITCH_STEP (1.0471976f / COOP_PITCH_MAX)   /* free aim's limit, 60 degrees */
static inline int coop_pitch_get(uint8_t pflg) { return ((int8_t)(pflg & 0xF8) >> 2) | (pflg & 1); }
static inline uint8_t coop_pitch_bits(int v) { return (uint8_t)((v & 1) | (v >> 1 & 0x1F) << 3); }
enum { COOP_REJ_CODE = 1, COOP_REJ_BUSY = 2, COOP_REJ_REMOVED = 3,   /* REMOVED: in a goodbye, the host removed us */
       COOP_REJ_BIORAND = 4,            /* in a guest's goodbye: its BioRand pack is not the host's (protocol 27) */
       COOP_REJ_STATE = 5 };            /* in a guest's goodbye: the host's game state failed its checks (protocol 28) */

typedef struct PoseExt {                /* lobby extension: in the first datagram of a frame (PF_EXT) */
    char name[16];
    int8_t lchr;
    uint8_t lobby;
    uint8_t items;                      /* guest: where its inventory comes from (ITEMS_*) */
    uint8_t rec;                        /* host: REC_*, whether it keeps saved items for this guest's player id */
    char note[40];
    uint8_t pid[16];                    /* guest: its player id (recvx.ini coop_player_id; zero: none) */
    uint8_t tag[16];                    /* guest: rdv_rejoin_tag of its secret over the session's code: the same player again */
    uint8_t br;                         /* BioRand (protocol 27): host 1 = it plays its pack; guest 1 = it has a pack */
    uint8_t br_pad[3];                  /* zero */
    uint32_t br_seed;                   /* the sender's pack: BioRand's seed number (0xFFFFFFFF unknown) and identity */
    uint32_t br_id;                     /*   (biorand.h biorand_id; 0: no pack). A guest whose pack differs leaves. */
    uint32_t imp_id;                    /* the sender's imported models (cast.h cast_import_id, 0: none): the cutscene cast
                                           uses them only while both sides send the same (protocol 29) */
} PoseExt;
enum { ITEMS_NEW, ITEMS_SAVE, ITEMS_HOST, ITEMS_CURRENT };   /* PoseExt.items */
enum { REC_UNKNOWN, REC_NONE, REC_HAVE, REC_OFF };   /* PoseExt.rec: not known yet (or a guest), no saved items for you, some,
                                                         the host keeps none ("Remember partner inventories" off) */
/* a guest's own items as the host keeps them (EV_OWN, saved inventories): its item words (sys->itm, 384), status
   flags (4), weapons (4 bytes), health (4 shorts) - the save region's fields the guest keeps of its own */
#define COOP_OWN_SIZE 1564

enum { EV_ENE_HIT = 1, EV_PLY_HIT = 2, EV_BLOB = 3, EV_FLAGS = 4, EV_PICKUP = 5, EV_LOST = 6,
       EV_ESTATE = 7, EV_GRAB = 8, EV_UNGRAB = 9, EV_TRIG = 10, EV_TRADE = 11, EV_BOX = 12, EV_BOXFIX = 13,
       EV_SPAWN = 14, EV_EOPTS = 15, EV_ATR = 16, EV_FX = 17, EV_GRANT = 18, EV_TSCREEN = 19, EV_GONE = 20, EV_OWN = 21,
       EV_SKIP = 22, EV_BRSEED = 23, EV_CASTOFF = 24, EV_TYPES };
#define COOP_BR_STR 68                  /* EV_BRSEED: BioRand's seed string (biorand_opts.h BIORAND_SEED_LEN), no NUL */
#define COOP_BR_DIG 12                  /*   and the first hex digits of the host's CLI archive's SHA-256 */
#define COOP_CASTOFF_CHUNK 64           /* EV_CASTOFF: bytes per event */
#define COOP_CASTOFF_MAX 272            /*   of the whole bit list (cast.h CAST_OFF_BYTES, at most) */
#define SKIP_GO 1                       /* EV_SKIP: both pressed skip, the cutscene ends now (host -> guest) */
enum { TD_OFFER = 1, TD_ANSWER = 2, TD_CANCEL = 3, TD_QUERY = 4 };
/* EV_TSCREEN (protocol 14), the trade screen: REQ asks, CANCEL withdraws the request, OPEN accepts it
   (both open the screen), CLOSE leaves it; STATE is a player's side (grid, selection, ready); the host
   commits a swap (SWAP), the guest applies or refuses it (DONE), QUERY asks again after a reconnect */
enum { TS_REQ = 1, TS_CANCEL, TS_OPEN, TS_CLOSE, TS_STATE, TS_SWAP, TS_DONE, TS_QUERY,
       TS_WANT, TS_DECLINE };            /* WANT: we request the partner's cell and word (TSC_NONE, 0: withdrawn); DECLINE: its request */
#define TSC_NONE 255                     /* no cell / nothing equipped */
#define TS_WHY_MAX 16                   /* refusal reasons (coop_glue.c trade_why) */
enum { ATR_WAL, ATR_ETC, ATR_FLR, ATR_OBJ };   /* EV_ATR kinds: room records, objects */
/* EV_FX: who an effect follows or whose model it takes its texture from - an enemy slot (0..254),
   the receiver's own player (the sender's proxy of it), the sender's player, or none. A byte on the
   wire: a slot, or with its bit (FXR_*) set in the event's `refs` one of these, as FX_* - FX_RECEIVER */
enum { FX_RECEIVER = 0x100, FX_SENDER, FX_NONE };
enum { FXR_IDX = 1, FXR_OWNER = 2, FXR_EXP0 = 4 };
static inline void coop_fx_put(uint8_t* b, uint8_t* refs, int bit, int r)
{
    if (r >= FX_RECEIVER) { *b = (uint8_t)(r - FX_RECEIVER); *refs |= bit; } else *b = (uint8_t)r;
}
static inline int coop_fx_get(uint8_t b, uint8_t refs, int bit) { return refs & bit ? FX_RECEIVER + b : b; }
#define FX_BURST 308                    /* a particle burst (sparks): EV_FX carries its parameter words */
#define FX_PARTICLE 302                 /* blood and debris particles: EV_FX carries the bhSetEffParticle call */
#define FX_PTCL_TYPES 19                /* its table (effsub3.c PtclTbl) */
#define FX_ARM 1000                     /* not an effect: a Bandersnatch's arm length (its AI sets node offsets) */
#define FX_RAW_WORDS 18
/* EV_FX's generic entries (protocol 28): only the effects a sender's window can start (an enemy's step,
   our player's, our projectiles': docs/COOP.md "Effects"), each with what its creators set and what its
   step and drawing stay safe with (the decomp's effect steps read type, mdlver, counters and textures as
   table indices). Every entry is fresh (modes 0, the step's own init runs here). Anything else is refused. */
enum { FXF_LINK = 1,                    /* may follow a work (flg 0x80 iff Event.idx names one; lkono < its nodes) */
       FXF_LINK_REQ = 2,                /* must */
       FXF_LKWK = 4,                    /* follows Event.idx's work without flg 0x80 (its step reads lkwkp): required */
       FXF_TXP = 8,                     /* stflg 0x20: txp a texture of the owner's model, tex_id one of that list */
       FXF_MLW = 16,                    /* mlw: its model is one of the owner's, mdlver < its objects */
       FXF_EXP0 = 32 };                 /* exp0: the work it belongs to (required) */
#define FXV_ANY 255                     /* FxRule.mdlver_max: any (the step does not read it) */
#define FXC_ANY (-1)                    /* FxRule.ct_max: any (the step sets it before reading it) */
typedef struct FxRule {
    uint16_t id, type_max;              /* type <= type_max (0xFFFF: any; not read) */
    uint32_t types;                     /* and, if not 0, bit `type` set (type < 32) */
    uint32_t flg;                       /* flg bits it may have besides 0x1 (always) and 0x80 (FXF_LINK*) */
    uint8_t refs, mdlver_max;           /* FXF_*; mdlver <= max */
    int8_t flr_min, flr_max;            /* flr_no */
    uint8_t lk_min, lk_max;             /* lkono */
    int16_t ct_max[4];                  /* ct0..ct3 within 0..max, or FXC_ANY */
} FxRule;
const FxRule* coop_fx_rule(int id);     /* NULL: not an effect co-op sends */

typedef struct AtrRec {                 /* a room record (ATR_WORK, 0x24 bytes) or an object's placement */
    uint8_t flg, type, id;
    int8_t flr_no;
    uint32_t attr;
    float p[6];                         /* px py pz w h d; objects: px py pz, ax ay az as floats */
    uint8_t prm[4];
} AtrRec;

#define COOP_HIT_AIM 0x1000u            /* EV_ENE_HIT flg: bits 0xE00 hold the shooter's aim height */
#define COOP_HIT_PRECISE 0x2000u        /* EV_ENE_HIT flg: the shooter's "Precise hits" judged it (a zombie's head bursts only from a head hit) */
typedef struct Event {
    uint32_t id;
    uint8_t type, idx, stg, rom;        /* enemy slot; events for another room are dropped */
    union {
        struct {                        /* our shot hit a puppet enemy: the weapon code's output; flg: the hit
                                           bit 4, and COOP_HIT_AIM with the shooter's aim height (at_flg 0xE) << 8 */
            int8_t jnt[6];
            int16_t djnt_no, wpnr_no;
            int32_t dam[6];
            uint32_t flg, flg2, comb_flg;   /* the hit bits only */
            float dp[3], dv[3];
            int32_t dax, day;
        } eh;
        struct {                        /* a host enemy hit our proxy: what it did to the player */
            int32_t hp_delta, flg_set, exp1_set;
            uint8_t mode[4];
            float dv[3];
        } ph;
        struct {                        /* a chunk of a transfer (EV_BLOB save region, EV_ESTATE grab state, EV_OWN the guest's items) */
            uint16_t tid;               /* transfer: all chunks of one carry it */
            uint16_t total;             /* the transfer's size */
            uint16_t off;               /* a multiple of 64 */
            uint8_t len;                /* 64, the last chunk less */
            uint8_t pad;                /* EV_BLOB: 1 = its items are the guest's saved ones (host record); EV_OWN: 1 = the change's last chunk; else 0 */
            uint8_t data[64];
        } blob;
        struct {                        /* changed words of the flag block (NFLAG) */
            uint32_t base;              /* the sender's in_last when it took the change */
            uint8_t n, word[6];
            uint32_t set[6], clr[6];
        } fl;
        struct {                        /* we took an item: its spot goes; `item` (0: none) is the partner's too */
            uint8_t item, etc, etc_on;
            uint8_t undo;               /* host only, 1: both took spot `bit`, ours stands - the guest's pickup goes back */
            int16_t bit;                /* the spot's it_flg bit, -1 when not seen */
        } pk;
        struct { uint8_t item; } lost;  /* a key item of ours was used up */
        struct { uint8_t op; } sk;      /* EV_SKIP: SKIP_GO; Event.stg/rom: the host's room (the cutscene's) */
        struct {                        /* a grab changes hands; the states came before as EV_ESTATE */
            uint8_t child, state, pset; /* child slot (255 none); state: the transfer holds them */
            int8_t mdl, cmdl;
            uint16_t tid;               /* the EV_ESTATE transfer with the states */
            int16_t mnw, cmnw;
            uint32_t flg_b, stflg_b, flg2_b, exp1_b, exp1_a;   /* the proxy before/after the grab's start */
        } gr;
        struct {                        /* a world event block fired here (M8): what it executed */
            uint8_t set, val, rcase, last;   /* gate flag (set 1 ev 2 ky 3 ed 8 mp) and its value before */
            uint16_t bit, first, n, start;   /* first action in this chunk, actions in total, the block */
            uint32_t sig;                    /* the room script's signature */
            uint16_t ops[24];                /* offsets of the actions in the room script */
        } tr;
        struct {                        /* shared item box: taken out (rm) and put in (add) */
            uint8_t nrm, nadd;
            uint32_t w[16];             /* rm first, then add */
        } bx;
        struct {                        /* trade: OFFER, ANSWER (yes: taken), CANCEL, QUERY */
            uint8_t op, yes;
            uint32_t tid, entry;        /* entry: inventory word (id << 16 | count) */
        } td;
        struct {                        /* the trade screen (TS_*) */
            uint8_t op, cell, ready, yes;   /* STATE: our cell (0..9, TSC_NONE), ready; SWAP: the guest's cell; DONE: swapped */
            uint8_t slots, eq, why, pcell;  /* STATE: grid cells (8/10), the equipped grid word (0..9, TSC_NONE), the partner's
                                               cell as we saw it; SWAP: the host's cell; DONE: why not (< TS_WHY_MAX) */
            uint32_t sid;                   /* the screen: the request's id */
            uint32_t tid;                   /* SWAP, DONE, QUERY: the swap */
            uint32_t give, take;            /* STATE: our word at cell, the partner's we saw (0: empty); SWAP: the host's
                                               word (to the guest), the guest's (to the host) */
            uint32_t inv[10];               /* STATE: our grid words */
        } ts;
        struct {                        /* after a world event: room records as the sender has them */
            uint8_t kind, n, ix[2];     /* ATR_*, records (1..2), their indices */
            uint32_t sig;               /* the room script's signature; Event.idx: the room case */
            AtrRec r[2];
        } at;
        struct {                        /* an effect the sender's game started (an enemy's attack, a shot's hit or
                                           projectile); Event.idx: what it follows - FX_* or an enemy slot */
            uint16_t id, type;          /* effect number (coop_fx_rule), its type */
            uint32_t flg;
            uint16_t stflg;
            int16_t tex_id;
            uint8_t mdlver;
            int8_t flr_no;
            uint8_t lkono, owner;       /* the node it follows (< 64); the model txp/mlw take parts of: FX_* or an enemy slot */
            uint8_t mode[4];
            int8_t txp, mlw;            /* txp[0], mlwP: owner's mdl[k] (-1: none / the effect's own model) */
            uint8_t exp0;               /* exp0: FX_* or an enemy slot */
            uint8_t refs;               /* FXR_*: Event.idx, owner, exp0 hold an FX_* */
            int16_t ang[3];
            int16_t ct[4];
            int16_t s[3];               /* scale, 1/256 */
            float p[3], o[3], n[3];     /* position, offset (link offset or direction), xn yn zn */
        } fx;
        struct {                        /* EV_FX of effect FX_BURST (bhSetRapEff 308: the sparks of a hit) */
            uint16_t id, nw;            /* FX_BURST, FX_RAW_WORDS */
            uint32_t w[FX_RAW_WORDS];   /* its parameters, then position, direction, colours, ground height */
        } fxr;
        struct {                        /* EV_FX of FX_PARTICLE: bhSetEffParticle(Mk2)(follows, node, off, dir, colours, type);
                                           Event.idx: the work it follows (FX_NONE: off is a position) */
            uint16_t id, typ;           /* FX_PARTICLE, the table entry */
            uint8_t mk2, lnk;           /* Mk2 (start and end colour); the node */
            uint8_t refs, pad;          /* FXR_IDX: Event.idx holds an FX_* */
            uint32_t col, dst;
            float off[3], dir[3];
        } fxp;
        struct {                        /* EV_FX of FX_ARM; Event.idx: the enemy slot (no refs) */
            uint16_t id, pad;
            float len[4];               /* x offsets of the arm's nodes 7..10 */
        } fxa;
        struct {                        /* an event gave the sender an item the receiver may have missed;
                                           Event.stg/rom: the room it happened in */
            uint8_t item;
            int8_t flr_no;              /* the sender's floor there */
            uint16_t count;
            float p[3];                 /* where the sender stood */
        } gt;
        struct {                        /* guest: enemies that died in its game while the host's were not driving them
                                           (the host elsewhere or busy); Event.idx: the room case */
            uint8_t n, pad;
            uint8_t slot[COOP_GONE_MAX], id[COOP_GONE_MAX], ver[COOP_GONE_MAX];   /* slot, type, model version */
        } gn;
        struct {                        /* host, while it plays BioRand (protocol 30): its pack's seed string ("R840-...", every
                                           byte 0: unknown) and its CLI's digest (lower-case hex, all 0: unknown) */
            char str[COOP_BR_STR];
            char dig[COOP_BR_DIG];
        } br;
        struct {                        /* host (protocol 31): bytes at..at+n-1 of its cutscene cast's "Custom" list (cast.h
                                           cast_off: a bit per "All models" entry, set: not checked) */
            uint16_t at;                    /* a multiple of COOP_CASTOFF_CHUNK */
            uint8_t n, pad;                 /* 1..COOP_CASTOFF_CHUNK, within COOP_CASTOFF_MAX; pad zero */
            uint8_t bits[COOP_CASTOFF_CHUNK];
        } co;
        SpawnNote sp;                   /* host: an enemy not in the room file, into slot idx */
        EnemyOpts eo;                   /* host: its enemy options */
    } u;
} Event;

typedef struct EneSnap {
    uint8_t idx, mode[4];
    int8_t mdl;                         /* mlwP as an index into the enemy's own mdl[] */
    int16_t mnw;                        /* mnwP: entry offset from mnwPb (0..511), MNW_ROOM, or MNW_KEEP */
    uint32_t flg, flg2, stflg, mdflg;
    float px, py, pz;
    int32_t ax, ay, az;
    int32_t mtn_no, frm_no, mtn_md;
    int16_t hokan_count;
    uint8_t id, pad;                    /* the enemy's type (BH_PWORK.id): applied only to a slot holding the same type */
    int32_t hokan_rate, hp;
    float watr[7];                      /* hit capsule (NJS_CAPSULE) */
} EneSnap;

/* ---------------------------------------------------------------- checks (0 = fine) */
enum { COOP_OK, COOP_BAD_SIZE, COOP_BAD_HDR, COOP_BAD_VERSION, COOP_BAD_COUNT,
       COOP_BAD_TYPE, COOP_BAD_DIR, COOP_BAD_FIELD, COOP_BAD_FLOAT };

/* a datagram (already opened: CoopDirect or rendezvous.c authenticated it): header, version, counts
   and exact size. Writes the counts (nev, nene, whether a PoseExt follows). */
int coop_check_dgram(const void* buf, int n, int* nev, int* nene, int* ext);
int coop_check_event(const Event* e, int from_host);   /* fields within their ranges, sent the right way */
int coop_check_snap(const EneSnap* s);
int coop_check_pose(const Pose* p);                    /* the pose's own fields (numbers, not their meaning) */
int coop_item_word_ok(uint32_t w);                     /* id 1..155, count <= 999 */
int coop_flag_bits(int set);                           /* bits of a world flag set (1 ev, 2 ky, 3 ed, 8 mp), 0 for others */

/* a datagram as receive() takes it: coop_check_dgram, then every event (sent by the host when
   `from_host`) and every snapshot; 0 = all of it may be used */
int coop_check_all(const void* buf, int n, int from_host, int* nev, int* nene, int* ext);

/* the reliable event stream, sender side: ids from 1, kept until acknowledged. Full: nothing is
   dropped; the push fails and `overflow` is set (the caller resynchronises). */
#define COOP_RING 512
typedef struct CoopRing {
    Event ev[COOP_RING];
    uint32_t head, next;                /* unacknowledged: head..next-1 */
    int overflow;
} CoopRing;
void coop_ring_reset(CoopRing* r);
int coop_ring_push(CoopRing* r, Event* e);          /* sets e->id; 0 when full */
void coop_ring_ack(CoopRing* r, uint32_t ack);      /* the peer applied everything up to `ack` */
const Event* coop_ring_get(const CoopRing* r, uint32_t id);   /* NULL unless id is unacknowledged */

/* ---------------------------------------------------------------- direct sessions (protocol 28)
   The join code is a password both players know; the handshake is CPace (a PAKE) on X25519:
   G = Elligator2(BLAKE2b(code)), each side sends Y = X25519(y, G) for a fresh secret y (PKT_HELLO
   joiner -> host, PKT_HELLO_ACK back), and both take K = X25519(y, Y') into the session keys with
   both Y and the code. Only the same code gives the same keys; nothing on the wire (not even the
   whole handshake) lets anyone test a code offline - an active attacker gets one guess per
   handshake, and the host answers handshakes at COOP_HS_RATE a second and backs off from each
   address that failed (CoopGuard). From then on every datagram is sealed (rdv_seal: XChaCha20-
   Poly1305, a 64-bit counter, the replay window): the host takes a partner, and the joiner a host,
   only when its datagrams open with the keys - the proof of the code. PKT_REJECT stays plaintext
   (it has to reach a joiner whose code was wrong). */
typedef struct CoopHello {              /* PKT_HELLO / PKT_HELLO_ACK: as large as a Pose, so a build before
                                           protocol 28 reads our version from it and says so */
    uint32_t magic;
    uint16_t version;
    uint8_t type, zero8;
    uint8_t key[32];                    /* Y */
    uint8_t zero[72];
} CoopHello;
#define COOP_HS_N 64                    /* handshakes a host keeps (answered, or a session); oldest unproven first out: at
                                           COOP_HS_RATE an answered one stays at least 3 s, however many come */
#define COOP_HS_RATE 20                 /* host: new handshakes a second (the budget's refill, also its burst) */
#define COOP_HS_TTL 10000               /* ms an unproven handshake is kept */
#define COOP_GUARD_N 32                 /* addresses whose failures are remembered (oldest first out) */
#define COOP_GUARD_MIN 250              /* ms a failed address waits after its first failure, doubled per failure ... */
#define COOP_GUARD_MAX 60000            /* ... up to a minute */
#define COOP_GUARD_FORGET 600000        /* 10 minutes without a failure: forgotten */
typedef struct CoopGuard {
    struct { uint32_t ip, last, until; uint16_t fails; } e[COOP_GUARD_N];
    uint32_t tok_ms;                    /* the budget: tokens (x1000) and when it was refilled */
    int32_t tok;
    int tok_init;
} CoopGuard;
int coop_guard_wait(const CoopGuard* g, uint32_t ip, uint32_t now);   /* ms this address must still wait (0: may try) */
uint32_t coop_guard_fail(CoopGuard* g, uint32_t ip, uint32_t now);    /* it failed: how long it waits now */
int coop_guard_take(CoopGuard* g, uint32_t now);                      /* 1: a new handshake fits the budget (and is counted) */

enum { HS_FREE, HS_HELLO, HS_KEYED, HS_PROVEN };   /* joiner: sent HELLO; both: keys; a datagram opened with them */
typedef struct __attribute__((aligned(8))) CoopHs {
    RdvSession ses;                     /* first, 8-aligned: rdv_seal counts tx_ctr atomically (no split lock on 32-bit x86) */
    uint32_t ip; uint16_t port;         /* network byte order (NetAddr) */
    uint8_t state;
    uint8_t sk[32];                     /* joiner, HS_HELLO: our secret until the answer */
    uint8_t yj[32], yh[32];             /* both public values (the host answers a repeated HELLO with the same yh) */
    uint32_t t;                         /* made / last opened datagram (ms) */
} CoopHs;
typedef struct CoopDirect {
    int host;
    uint8_t g[32];                      /* the code's generator */
    char code[COOP_CODE_LEN + 1];
    CoopHs hs[COOP_HS_N];               /* a joiner uses hs[0]: the host */
    CoopGuard guard;
} CoopDirect;
enum { COOP_DIN_RAW = -1, COOP_DIN_WRONG = -2 };
void coop_direct_init(CoopDirect* d, int host, const char* code);   /* code normalized (rdv_code_norm) */
void coop_direct_wipe(CoopDirect* d);                               /* every key gone */
/* joiner: the HELLO for the host at ip:port (the same key until it answers); 0 without randomness */
int coop_direct_hello(CoopDirect* d, uint32_t ip, uint16_t port, CoopHello* out);
/* a datagram from ip:port, in place (cap bytes in buf): > 0 a game datagram opened with its keys, its
   length; 0 nothing more (a handshake answered: reply/reply_n to send back, or dropped);
   COOP_DIN_RAW the datagram as it came (another protocol version, a PKT_REJECT: unauthenticated);
   COOP_DIN_WRONG a sender with keys of another code (the host rejects it; it waits, CoopGuard). */
int coop_direct_in(CoopDirect* d, uint32_t ip, uint16_t port, uint32_t now, uint8_t* buf, int n, int cap, CoopHello* reply, int* reply_n);
int coop_direct_ready(const CoopDirect* d, uint32_t ip, uint16_t port);   /* keys for this address (we may send) */
int coop_direct_seal(CoopDirect* d, uint32_t ip, uint16_t port, const void* in, int n, uint8_t* out, int cap);   /* length, 0: no keys */
void coop_direct_forget(CoopDirect* d, uint32_t ip, uint16_t port);   /* its keys gone (a removed partner) */
#define COOP_DIRECT_OVER RDV_E2E_OVER   /* bytes a sealed datagram adds */

/* "host:port#CODE" (or "host:port CODE"): 1 when host and port are there; code "" when none */
int coop_parse_join(const char* s, char* host, int host_cap, int* port, char code[COOP_CODE_LEN + 1]);

/* transfers (EV_BLOB, EV_ESTATE): chunks of 64 bytes into a buffer, complete when every chunk of
   one transfer id arrived. A chunk that does not fit leaves everything as it was. */
typedef struct CoopXfer {
    uint16_t tid, total;
    int done;                           /* every chunk is here */
    uint8_t have[80];                   /* a byte per chunk: up to 5 KB */
    uint8_t* buf;
    int cap;
} CoopXfer;
void coop_xfer_init(CoopXfer* x, void* buf, int cap);
void coop_xfer_reset(CoopXfer* x);
/* -1 rejected (nothing changed), 0 taken, 1 the transfer is complete */
int coop_xfer_chunk(CoopXfer* x, int tid, int total, int off, int len, const uint8_t* data);

/* Which player an enemy of the host goes for (coop_glue.c coop_enemy_begin): 0 the host's, 1 the
   partner's stand-in. Per player ([0] the host's, [1] the partner's): d squared XZ distance (the
   caller weighs a player on another floor, which the enemy cannot attack, as twice as far); busy:
   being hit or held, dying, or left alone by this enemy for a while (the loop guard); front: free,
   within COOP_TGT_TOUCH and inside the enemy's front quarter - what its attacks reach.
   In order:
   1. commit HOLD (it drives its victim: a zombie's hold, or the victim plays its motions): the
      victim stays, so one attack never reaches both players.
   2. a busy player is never chosen.
   3. commit ATTACK (its grab/bite/throw state): the victim stays while within COOP_TGT_KEEP;
      a victim farther away is not what it is attacking.
   4. a busy target is left for the free player within COOP_TGT_REACH (else it waits its turn).
   5. a target right in front stays, unless the other is at less than half its distance (behind it);
      6. the other player right in front is taken instead, unless it is over 1.5 times as far as the
      target (no enemy walks into a player without going for it).
   7. the nearer player, only by a margin (1.5 x squared distance), so near-equal distances do not
      flip it. */
#define COOP_TGT_REACH 40.0f            /* 4 m: how far an enemy goes for the free player */
#define COOP_TGT_KEEP 30.0f             /* 3 m: a victim mid-attack farther than this is let go */
#define COOP_TGT_TOUCH 15.0f            /* 1.5 m: "right in front" */
enum { COOP_TGT_FREE, COOP_TGT_ATTACK, COOP_TGT_HOLD };
typedef struct CoopTgtIn { float d[2]; unsigned char busy[2], front[2], commit; } CoopTgtIn;
int coop_pick_target(int cur, const CoopTgtIn* in);
/* The loop guard: attacks one enemy starts on one player (a hit reaction or a grab). The
   COOP_LOOP_N-th within COOP_LOOP_WIN frames is a loop - nothing in retail attacks that fast (a grab
   alone lasts longer) - and coop_atk_note returns 1: the enemy then leaves that player alone (busy for
   it) for COOP_LOOP_COOL frames. */
#define COOP_LOOP_N 3
#define COOP_LOOP_WIN 90u
#define COOP_LOOP_COOL 60u
typedef struct CoopAtk { unsigned int t0, until; unsigned char n; } CoopAtk;
int coop_atk_note(CoopAtk* a, unsigned int now);
int coop_atk_cooling(const CoopAtk* a, unsigned int now);

/* room scripts (docs/modding/ROOM_FORMAT.md "Event scripts") */
int coop_insn_len_raw(const uint8_t* p);    /* bytes of the instruction at p, 0 unknown */
int coop_op_cond(int op);                   /* tests: continue or skip, nothing else */
int coop_op_flow(int op);                   /* End IfelCk ElseCk Endif, loops, Sleep, EvtNext, EvtEnd */
int coop_op_replay(const uint8_t* p);       /* an action a partner's world event may run here */
/* the actions `ops` (script offsets) are instruction starts in [then0, then1) of script `d`
   (length `size`), in increasing order; 1 = fine */
int coop_trig_ops_ok(const uint8_t* d, int size, int then0, int then1, const uint16_t* ops, int n);
/* Spots of a room script that wake enemies (docs/COOP.md "Enemies a room script wakes"): the floor records
   (scd1 test "player is at floor N", cb_flg bit 22) and triggers (bit 23) that every block of scd1 testing them
   tests for "there", with no else part and a then-part that loads enemy works and controls them (LoadWork of an
   enemy, then SubCtr / CommonCtr), shows or hides enemy records, sets room flags (rm_flg, cb_flg, sp_flg 27),
   turns records off (FlrAtariSet), or starts events that do only that; at least one enemy touched. Script `d`
   (`size` bytes), scd1 in [scd1, scd1_end), event k's code in [evs[k], eve[k]). Writes up to `max`, returns
   how many. */
typedef struct CoopWake { uint8_t etc, ix; } CoopWake;   /* etc: a trigger, else a floor record; its index */
int coop_wake_scan(const uint8_t* d, int size, int scd1, int scd1_end, const uint32_t* evs, const uint32_t* eve, int nev,
                   CoopWake* out, int max);
/* EV_ATR: of the partner's room record (wall, trigger, floor) we take the on/off flag and the
   placement; its type, attr and prm only as one of our room script's own WalAtariSet2 /
   EtcAtariSet2 / FlrAtariSet2 writes them for that record (retail code indexes object, item and
   record tables with them: obwp[prm2], itwp[prm0], etcp[prm3], kaidan_ang[prm2] ...); id and
   flr_no never (no script op changes them). */
typedef struct AtrSet { uint8_t kind, ix, type, prm[4]; uint32_t attr; } AtrSet;
int coop_atr_set(const uint8_t* insn, AtrSet* out);   /* 1: the instruction is such a write (out: what it writes) */
void coop_atr_merge(AtrRec* rec, const AtrRec* in, int kind, int ix, const AtrSet* sets, int n);

#ifdef __cplusplus
}
#endif
#endif
