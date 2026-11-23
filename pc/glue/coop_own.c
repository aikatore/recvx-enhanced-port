/* Co-op: who runs each enemy and the hand-overs (docs/COOP.md "Who runs the enemies"): the enemy state sent with a
 * hand-over or a grab (own_types, ene_pack/ene_unpack, EV_ESTATE), holds and grabs across the two games, EV_ENEOWN
 * (own_give/own_take/own_apply/own_frame), hits on enemies the other game runs (hit_send/hit_apply, defer), the guest's
 * hand-in and kills (hin_*, gone_*). Moved out of coop_glue.c; the shared state is in coop_int.h. */
#include "coop_int.h"


/* Grabs (M7). A host enemy that grabs the proxy hands the grab to the client: its state (and
   its linked child's) goes over, the client's game runs that enemy with its own player - the
   grab motions, the struggle, the damage, the release - and the host shows it as a puppet of
   the client's snapshots, until the client hands it back with its state. Regular zombies
   (en01) only; other grabs on the proxy are cancelled as before. owned[i]: the client runs
   slot i (host: for the client; client: ourselves). */
/* the wire carries these structs raw (the 32-bit game's layout; ene_fields' offsets): a 64-bit build
   would be another wire format, and a set of 512 motions is room.c's 12288 bytes */
_Static_assert(sizeof(BH_PWORK) == 1408 && ESTATE_SIZE <= 64 * (int)sizeof(((CoopXfer*)0)->have), "EV_ESTATE: BH_PWORK as the 32-bit game has it");
_Static_assert(12288 / sizeof(MN_WORK) == COOP_MNW_MAX, "an enemy's motion set: 512 entries");
unsigned char owned[PC_ENE_SLOTS], owned_child[PC_ENE_SLOTS], estate[ESTATE_SIZE], estate_out[ESTATE_SIZE];   /* received / being sent */
unsigned char estate_z[ESTATE_Z], estate_zin[ESTATE_Z];
unsigned short estate_tid;       /* the transfer unpacked into estate[] (0 none) */
unsigned short own_wait[PC_ENE_SLOTS];    /* host: the guest's frames in our room without a snapshot of a grab it runs */
unsigned int snap_at[PC_ENE_SLOTS];       /* guest: our frame at which the slot last took the host's state */
unsigned int pose_seq[PC_ENE_SLOTS];       /* guest: the host frame of the slot's last snapshot */
unsigned char step_ran[PC_ENE_SLOTS];      /* the slot's last step ran its AI here (else a puppet's) */
unsigned char gave_grab[PC_ENE_SLOTS];     /* its step here just handed its grab of the stand-in over (grab_out) */
CoopXfer estate_x;               /* EV_ESTATE chunks into estate_zin[] */
int aid_m12 = -1;                /* test aid RECVX_COOP_NO_EMBRACE: Alexia 1's modes before her step (coop_enemy_end) */
unsigned short xfer_tid;         /* our last transfer id (blob, estate) */
int owned_room = -1;
int ev_room = -1;                /* >= 0: the room key queue_event stamps instead of ours (an enemy of the room we leave) */
void pc_enemy_holds(BH_PWORK* pl, BH_PWORK* by);   /* eneset.c */
/* Shared Enemy Control (protocol 46..48, the host's rule "Shared Enemy Control", OPT2_OWNERSHIP): engaged-player ownership. A common
   enemy (zombies: id 1, its body with its upper half) runs in the game of the player it is engaged with; the other
   game shows it as a puppet of that game's snapshots. owned[i] keeps its meaning on both sides (the guest runs slot
   i); we_run(i): this game runs it. It changes hands between attacks (own_frame, own_give, own_take: EV_ESTATE + EV_ENEOWN,
   protocol 56 make before break), a grab it starts on the other player's stand-in goes over as the host's zombie grabs
   always did (grab_out / grab_in, either way). Only the runner hands an enemy over, so there is one runner at a time. */
/* the policy's constants (test aid RECVX_COOP_P3="give,hold,hit" on both sides overrides them: the tuning runs) */
static int SHARED_GIVE = 8;                 /* frames in a row the runner prefers the partner before it hands the enemy over */
static int SHARED_HOLD = 45;                /* frames after a change of hands before the next one (no flapping) */
static int SHARED_HIT = 150;                /* frames (5 s) a player's hit counts as being engaged with the enemy */
unsigned char own_grab[PC_ENE_SLOTS];     /* the slot changed hands for a grab, still running in the runner's game */
static unsigned char shared_want[PC_ENE_SLOTS];      /* frames in a row we would hand it to the partner */
#define LEAVE_WAIT 15
static unsigned char own_leave[PC_ENE_SLOTS];     /* frames we left with it not in a state to go with its state */
static unsigned int own_t[PC_ENE_SLOTS];         /* our frame of its last change of hands */
static unsigned int own_ref_t[PC_ENE_SLOTS];      /* our frame + 1 of the partner's last refusal of it */
unsigned int own_ms[PC_ENE_SLOTS];        /* net_ms when we handed it over (until the new runner's first snapshot) */
unsigned int hit_t[PC_ENE_SLOTS][2];      /* our frame + 1 of the last hit on it: [0] ours, [1] the partner's */
unsigned int own_n[8];                     /* stats: gave, took, refused, gave back (leaving), taken back, glitch ms sum, glitches */
/* the host's rule "Shared Enemy Control" (g_coop.shared_enemies, off by default; locked once a session's game runs, settings.c); test aid
   RECVX_COOP_OWNERSHIP=0/1 on the host forces it off / on */
int shared_env(void)
{
    static int env = -2;
    if (env == -2) env = getenv("RECVX_COOP_OWNERSHIP") ? atoi(getenv("RECVX_COOP_OWNERSHIP")) != 0 : -1;
    return env >= 0 ? env : g_coop.shared_enemies;
}
int shared_on(void)
{
    return coop_on && !echo_delay && have_peer && (is_host ? shared_env() : peer_opts >= 0 && (peer_opts & (OPT2_OWNERSHIP << 8)));
}
int coop_shared_enemies(void) { return is_host || g_coop.state == COOP_OFF ? shared_env() : peer_opts >= 0 && (peer_opts & (OPT2_OWNERSHIP << 8)); }   /* the guest's lobby: the host's */
int we_run(int i) { return i >= 0 && i < PC_ENE_SLOTS && owned[i] != is_host; }
void own_set(int i, int m) { if (i >= 0 && i < PC_ENE_SLOTS) owned[i] = (unsigned char)(is_host ? !m : !!m); }
/* Protocol 56: who runs the enemies (docs/COOP.md "Who runs the enemies"). The player who was in the room first runs its
   enemies until it leaves the room (a door, its menu, a scene that stops its enemies): then it hands every one it runs to the
   player staying, with its state, and the receiver takes it at the frame of that state as its playout shows it (no gap,
   no jump). The other player shows them as puppets; its own copies stay hidden and still until the runner's state of each
   has come (or until it is known that it runs them itself). Both enter at once: the host decides (it was first unless the
   guest was already in the room when the host's room loaded). The records the room's scripts act on are the host's
   whenever it plays in the room (the scripts run there): the guest hands them in when the host comes. Shared Enemy
   Control moves the common enemies between the two by engagement on top of that. A hand-over (EV_ENEOWN 1) is make
   before break: the giver runs the enemy on until the receiver says it runs it (EV_ENEOWN 6); hits meanwhile go to the
   receiver, so each is applied once. */
static unsigned int away_n;                      /* frames the partner has not been in our room */
static int host_coming;                          /* guest: the host's claim of our room came, its door into it not over yet */
static int dflt = -1;                            /* the runner of slots that come up in this room load: 1 the guest, 0 the host */
unsigned char giving[PC_ENE_SLOTS];       /* we handed it over and run it on until the partner's EV_ENEOWN 6 or 0 */
uint32_t take_at[PC_ENE_SLOTS];           /* the partner's frame of a hand-over we take (its state is in our copy already):
                                                    ours once the playout shows that frame; 0 none */
static unsigned char take_how[PC_ENE_SLOTS];     /* ... 1 the giver left the room, 2 without its state (let go from the puppet) */
uint32_t keep_from[PC_ENE_SLOTS];         /* the partner's first frame of its run of a slot we stopped running (EV_ENEOWN 6) */
unsigned char seen[PC_ENE_SLOTS];         /* this room load: the slot showed a state, ours or the partner's (else hidden) */
unsigned char hid[PC_ENE_SLOTS];          /* hidden by us: 1 | its mdflg 1 << 1 | its flg 0x60, put back */
unsigned char evd[PC_ENE_SLOTS];          /* guest: an event of ours drives the partner's enemy: it took the runner's state, runs here */
static unsigned char act_was[PC_ENE_SLOTS];      /* the slot was in use at our last frame */
static unsigned char prov[PC_ENE_SLOTS];         /* host: the guest's until its claims say (we came into the room it ran) */
static int claim_wait;                            /* host: frames left to wait for the guest's claims */
static int claimed, host_verdict;                /* guest: claims sent for this room load; host: our verdict sent */
int run_room = -1;                        /* the room (key | case << 16) of our ownership table: the room load whose enemies they are */
unsigned int load_frame;                  /* our frame of that room load */
static unsigned int rdy_t;                       /* host: our last frame before the room was ready (own_frame) */
#define TAKE_STALE 15                   /* frames (own_frame) */
#define PAUSE_LEAVE 8                            /* frames our game stops its enemies (a scene, a message) before we hand them over */
#define AWAY_GRACE 45                            /* frames the partner may be away (a door's hand-over on its way) before we run all */
static unsigned char hin_qt[PC_ENE_SLOTS];       /* (the guest's room, below) */
static unsigned short hin_t[PC_ENE_SLOTS];
/* Protocol 47: a slot that changed hands, by any path (a grab, a hand-over, a take-back, a time-out,
   a slot gone free). Its kept snapshots and its codec bases were the other direction's: they start again (the new
   runner's first record is full). One we stopped running is seeded with our own last two states (build_snapshots)
   as the partner's frames now shown, so the playout carries it on (coop_hist_at: its speed and motion anchor, at most
   COOP_PO_EXTRA frames) instead of holding it until the new runner's first snapshot comes, a round trip and the
   margin later. Called before the snapshots are applied and before ours are built. */
uint32_t own_seed[PC_ENE_SLOTS];          /* the newest seeded frame (the partner's seq), 0 none */
unsigned char own_win[PC_ENE_SLOTS];      /* handed over, the new runner's first snapshot not shown yet */
unsigned char owned_was[PC_ENE_SLOTS];
uint32_t run_q[PC_ENE_SLOTS][2];          /* our frames of run_snap */
unsigned int win_st[5];                   /* RECVX_COOP_STATS: window frames carried on, held; windows; position
                                                    correction at the first real one (x100 units), motion frames stepped back */

int ene_slot(void* p) { int k = (BH_PWORK*)p - ene; return p && k >= 0 && k < PC_ENE_SLOTS ? k : 255; }   /* 255: none (wire) */
_Static_assert(PC_ENE_SLOTS == COOP_ENE_SLOTS, "enemy slots: the game's and the protocol's");
/* motion sets travel as an entry offset from the enemy's own set (0..511), or "room set", or "keep" */
short mnw_of(BH_PWORK* ep)
{
    int d = ep->mnwP - ep->mnwPb;
    return ep->mnwP == sys->rmthp ? MNW_ROOM : ep->mnwP && ep->mnwPb && d >= 0 && d < COOP_MNW_MAX ? d : MNW_KEEP;
}
void mnw_set(BH_PWORK* ep, int m)
{
    if (m == MNW_ROOM) { if (sys->rmthp) ep->mnwP = sys->rmthp; }
    else if (m >= 0 && m < COOP_MNW_MAX && ep->mnwPb) ep->mnwP = ep->mnwPb + m;
}
signed char mdl_of(BH_PWORK* ep) { int k = ep->mlwP - ep->mdl; return ep->mlwP && k >= 0 && k < 16 ? k : -1; }
/* entries from `set` to the end of the 512-entry set it lies in: an enemy's own (12288 bytes from
   mnwPb, room.c) or the room's; a set can start mid-way (mnwP = mnwPb + offset) */
int set_left(const BH_PWORK* ep, const MN_WORK* set)
{
    if (ep && ep->mnwPb && set >= ep->mnwPb && set < ep->mnwPb + COOP_MNW_MAX) return COOP_MNW_MAX - (int)(set - ep->mnwPb);
    if (sys->rmthp && set >= sys->rmthp && set < sys->rmthp + COOP_MNW_MAX) return COOP_MNW_MAX - (int)(set - sys->rmthp);
    return COOP_MNW_MAX;
}
/* motion `mtn` of set `set` (enemy `ep`'s, the room's, or ours) exists and has frame `frm` */
int mtn_ok(const BH_PWORK* ep, const MN_WORK* set, unsigned int mtn, int frm)
{
    return set && mtn < (unsigned int)set_left(ep, set) && set[mtn].datP && frm >= 0 && (frm >> 16) < (int)set[mtn].frm_num;
}
/* NaN and infinities fail the comparison too */
int finite3(float x, float y, float z)
{
    return fabsf(x) < 1e6f && fabsf(y) < 1e6f && fabsf(z) < 1e6f;
}
static int fins_n(const float* f, int n) { while (n--) if (!(fabsf(*f++) < 1e6f)) return 0; return 1; }
static const OwnType own_types[] = {
    { 1, 1, 0xB0, { { 0x00, 0x28 }, { 0x70, 0x78 } } },     /* zombie: 0x00..0x23 its parts (en01 bhEne01_Init), 0x24 its eff[] shadow,
                                                               0x70 the wall in its line of sight (ATR_WORK*, only tested for NULL; protocol 62), 0x74 the stairs
                                                               (ATR_WORK*: their in-use mark is the setter's) */
    { 4, 1, 0x58, { { 0x3C, 0x48 } } },                     /* Cerberus: 0x3C wall, 0x40 stairs, 0x44 line of sight (ATR_WORK*) */
    { 5, 1, 0x84, { { 0x60, 0x68 }, { 0x74, 0x78 }, { 0x7C, 0x80 } } },   /* Hunter, Sweeper: 0x60 the other Hunter, 0x64 its helper, 0x74 a
                                                               floor area (ATR_WORK*), 0x7C into its joint_tree */
    { 9, 1, 0x7C, { { 0x50, 0x58 } } },                     /* Bandersnatch: 0x50 wall, 0x54 jump or climb target (ATR_WORK*) */
    { 26, 1, 0xA0, { { 0x70, 0x74 } } },                    /* anatomist: 0x70 (ATR_WORK*) */
    /* the hand-in only (protocol 54) */
    { 3, 0, 0xF0, { { 0x80, 0xA0 }, { 0xA8, 0xE4 } }, { 0x60, 0x64 } },   /* Black Widow (en03): 0x80 its 8 legs, 0xA8 its 15 dormant babies; 0x60 the
                                                               surface it is on, 0x64 the one it climbs to; 0xA0 its legs off go along
                                                               (protocol 57, bw_pack) */
    { 6, 0, 0xB0, { { 0x18, 0x1C }, { 0x34, 0x6C }, { 0x84, 0x88 } } },   /* moth (en06): 0x18 its last wall hit, 0x34 its 14 wing parts,
                                                               0x84 its eff[] shadow */
    { 7, 0, 0x44, { { 0 } }, { 0x20, 0x24 } },              /* bat (en07): the floor or ceiling box it hangs from, and its copy */
    { 8, 0, 0 },                                            /* fish (en08): no work */
    { 10, 0, 0x24 },                                        /* ant A (en10) */
    { 11, 0, 0x70, { { 0x60, 0x64 } }, { 0x58 } },          /* Spotter (en11): 0x60 its search light (eff[]); 0x58 the wall it sits on */
    { 15, 0, 0x64 },                                        /* Nosferatu (en15, bhGetFreeMemory): no pointers */
    { 19, 0, 0x204, { { 0x68, 0x6C }, { 0x88, 0xC4 }, { 0x1D4, 0x1E4 }, { 0x1E8, 0x1EC } } },   /* Tyrant (en19 FW_WORK): 0x68 b_prgP,
                                                               0x88 ply_act (its blow on its own game's player), 0x1D4 its effect
                                                               table and claws, 0x1E8 cam_clr; 0x48 prgP goes as its En19ActTbl row.
                                                               0xC0 ewP (itself, Init): before protocol 62 it went as the sender's
                                                               address, which a Windows and a Linux build do not share */
    { 20, 0, 0x30 },                                        /* ant B (en20) */
    { 21, 0, 0x18, { { 0x14, 0x18 } } },                    /* albinoid infant (en21): 0x14..0x17 its grab of its own game's player */
    { 22, 0, 0xB0, { { 0x48, 0x50 } } },                    /* albinoid adult (en22): 0x48 wall (ATR_WORK*), 0x4C its light effect;
                                                               0x08's bits 0x1000 (0x4C set) and 0x80000 (the room light) stay ours */
    { 23, 0, 0x130, { { 0x80, 0x104 }, { 0x10C, 0x110 } }, { 0x60, 0x64, 0x104 } },   /* Giant Black Widow (en23): 0x80 its babies, 0xF8
                                                               its legs, 0x100 their joints, 0x10C the babies' count; 0x60, 0x64, 0x104
                                                               ground, wall and climb target */
    { 24, 0, 0x48, { { 0x28, 0x48 } } },                    /* baby spider (en24): 0x28 its own game's player's feet (the stomp) */
    { 25, 0, 0x0C, { { 0x04, 0x0C } } },                    /* ceiling trap (en25): 0x04 its moving wall, 0x08 its own game's crush */
    { 71, 0, 0x04 },                                        /* ant C (en71) */
    /* (protocol 57) bosses: their state goes only when their runner leaves the room through a door (shared 2: never at the hand-in,
       in a menu or by Shared Enemy Control - their rooms' scripts drive the fights); rmf: the bits of sys->rm_flg their steps set
       for the room's script (they go along). Before, they went without their state and the partner's game ran them on from
       their puppet's modes with its own stale work (the Gulp Worm jumped 188 units, a fight could start again). The works from
       their en*.c Inits; the parts (Gulp Worm's segments, Steve's arms, the knights' links) are the receiver's own children */
    { 2, 2, 0x90, { { 0x28, 0x70 }, { 0x7C, 0x80 } }, { 0x74 }, 0x1 },   /* Gulp Worm (en02): 0x28 its 18 segments, 0x74 a floor (memo),
                                                               0x7C its moving wall; rm_flg 0x1 (MV00 clears, MV02/03 set) */
    { 12, 2, 0xB0, { { 0x18, 0x1C }, { 0x20, 0x24 }, { 0x28, 0x68 } }, { 0 }, 0x1 },   /* Alexia 1 (en12): 0x18 a joint_tree row (set from
                                                               her motion), 0x20 her fire manager (eff[] 262), 0x28 her 16 fires (274);
                                                               rm_flg 0x1 (MV07) */
    { 16, 2, 0x68, { { 0x00, 0x04 } } },                    /* Alfred (en16): 0x00 his laser sight (eff[] 270) */
    { 17, 2, 0x54, { { 0x40, 0x44 } } },                    /* Monster Steve (en17): 0x40 a wall (rewritten every step); his arms are
                                                               exp2/exp3 (BH_PWORK: ours), exp1 his trails (drawing only: ours) */
    { 54, 2, 0 }, { 55, 2, 0 },                             /* Steve's knights (en54/55): no work */
};
const OwnType* own_type(const BH_PWORK* ep)
{
    unsigned int k;
    for (k = 0; ep && k < sizeof(own_types) / sizeof(own_types[0]); k++)
        if (own_types[k].id == ep->id) return &own_types[k];
    return NULL;
}
int own_shared(const BH_PWORK* ep) { const OwnType* t = own_type(ep); return t && t->shared == 1; }
/* (protocol 57) a boss of own_types: goes with its state only when its runner leaves through a door */
static int own_boss(const BH_PWORK* ep) { const OwnType* t = own_type(ep); return t && t->shared == 2; }
/* its work is there (the fish has none) */
static int own_work(const BH_PWORK* ep) { const OwnType* t = own_type(ep); return t && (!t->exp0 || ep->exp0); }
static int own_kept(const OwnType* t, unsigned int off)
{
    int k;
    for (k = 0; k < 6 && t->keep[k][1]; k++)
        if (off >= t->keep[k][0] && off < t->keep[k][1]) return 1;
    return 0;
}
static int own_atr(const OwnType* t, unsigned int off) { return off && (off == t->atr[0] || off == t->atr[1] || off == t->atr[2]); }
/* an ATR_WORK* of the room's collision on the wire: 1 + (its table << 12 | its index), 0 NULL, ATR_ELSE none of them (the
   receiver keeps its own). Both games read the same room file, and the moving boxes (sys->mwalp ...) are made by the same
   Inits in the same order. */
#define ATR_ELSE 0xFFFFFFFFu
static ATR_WORK* atr_tab(int k, int* n)
{
    switch (k) {
    case 0: *n = rom->wal_n; return rom->walp;
    case 1: *n = rom->etc_n; return rom->etcp;
    case 2: *n = rom->flr_n; return rom->flrp;
    case 3: *n = rom->rut_n; return rom->rutp;
    case 4: *n = sys->mwal_n; return sys->mwalp;
    case 5: *n = sys->metc_n; return sys->metcp;
    case 6: *n = sys->mflr_n; return sys->mflrp;
    }
    *n = 0;
    return NULL;
}
static uint32_t atr_enc(const ATR_WORK* p)
{
    int k, n;
    if (!p) return 0;
    for (k = 0; rom && k < 7; k++) {
        const char* t = (const char*)atr_tab(k, &n);
        intptr_t d = (const char*)p - t;
        if (t && n > 0 && n < 4096 && d >= 0 && d % (int)sizeof(ATR_WORK) == 0 && d / (int)sizeof(ATR_WORK) < n) return 1 + (uint32_t)(k << 12 | (int)(d / (int)sizeof(ATR_WORK)));
    }
    return ATR_ELSE;
}
static int atr_dec(uint32_t w, ATR_WORK** out)   /* 1: *out set */
{
    int n;
    ATR_WORK* t;
    if (w == ATR_ELSE) return 0;
    if (!w) { *out = NULL; return 1; }
    t = rom ? atr_tab((int)((w - 1) >> 12), &n) : NULL;
    if (!t || (int)((w - 1) & 0xFFF) >= n) return 0;
    *out = t + ((w - 1) & 0xFFF);
    return 1;
}
/* the Tyrant's action (FW_WORK 0x48 prgP, en19.c): its row of En19ActTbl + 1, 0 none; its code cannot be told from act_now */
typedef struct { int label; unsigned char mtn_no, frm_no, hkn_lvl, hkn_cnt; float chg_rte; int flag; void* prgP; } TyAct;   /* en19.h EA_WORK */
_Static_assert(sizeof(TyAct) == 0x14, "en19.h EA_WORK");
extern TyAct En19ActTbl[50];
static uint32_t ty_act_enc(void* f)
{
    int k;
    for (k = 0; f && k < 50; k++)
        if (En19ActTbl[k].prgP == f) return (uint32_t)k + 1;
    return 0;
}
/* the slot that changes hands with an enemy: a zombie's upper half (en01: exp1); 255 none */
int own_child(const BH_PWORK* ep) { return ep->id == 1 && ep->exp0 ? ene_slot(ep->exp1) : 255; }
/* the upper half the partner names for its zombie i (its slot), as ours: a mod's room file that gives a record another look
   (another count of linked parts) shifts the slots of every later upper half between the two games; before, every later
   zombie's hand-in, grab and hand-over was refused (jumps, other HP) and the snapshots of an upper half posed another
   zombie's (torn zombies). A zombie has one: the partner's names ours. */
int child_here(int i, int c)
{
    int h = i >= 0 && i < PC_ENE_SLOTS ? own_child(&ene[i]) : 255;
    return c < PC_ENE_SLOTS && h < PC_ENE_SLOTS ? h : c;
}
/* (protocol 57) a Black Widow's legs (en03: 8 linked works, id 31, at exp0 0x80; each moves the object BrokenParts[k] of its
   body's model): a leg shot off (bhEne03_DamageInit: exp0 0xA0 + k, its object's matrix set free, owP flg 0x3 and its two
   children's 0x2; on its death every leg, bhEne03_DD03) flies, lands and lies by its own step (bhEne03s_MV00) on that matrix.
   A leg off goes with its body's state as a record after the body's (ESTATE_ENE): its object's flags and matrix, the leg's
   modes, counters, place and its work (0xD4 bytes of plain values). Before, a Black Widow with a leg off went without its
   state (the receiver's would have had the leg back) and ran on from its puppet's pose with its own stale work */
#define BW_LEGS 8
#define BW_LEG_X0 0xD4
typedef struct { unsigned char on, oflg[3], mode[4]; int ct0, ct3, azp; float p[3], ar[4], mtx[16]; unsigned char x0[BW_LEG_X0]; } BwLeg;
static const unsigned char bw_parts[BW_LEGS] = { 10, 16, 22, 28, 7, 13, 19, 25 };   /* en03.c BrokenParts */
static BH_PWORK* bw_leg(const BH_PWORK* ep, int k)
{
    BH_PWORK* l = ep->exp0 ? *(BH_PWORK**)(ep->exp0 + 0x80 + 4 * k) : NULL;
    int s = ene_slot(l);
    return s < PC_ENE_SLOTS && (const char*)l == (const char*)&ene[s] && l->id == 31 && l->lkwkp == (const unsigned char*)ep && l->exp0 ? l : NULL;
}
static O_WORK* bw_obj(const BH_PWORK* ep, int k) { return ep->mlwP && ep->mlwP->owP && ep->mlwP->obj_num > bw_parts[k] + 2 ? &ep->mlwP->owP[bw_parts[k]] : NULL; }
static void bw_pack(const BH_PWORK* ep, unsigned char* out)
{
    int k, j;
    for (k = 0; k < BW_LEGS; k++) {
        BH_PWORK* l = bw_leg(ep, k);
        O_WORK* o = bw_obj(ep, k);
        BwLeg r;
        if (!l || !o || !(o->flg & 0x1)) continue;   /* (on: its object's matrix is free, the leg is off) */
        memset(&r, 0, sizeof(r));
        r.on = 1;
        for (j = 0; j < 3; j++) r.oflg[j] = (unsigned char)(o[j].flg & 0x3);
        r.mode[0] = l->mode0; r.mode[1] = l->mode1; r.mode[2] = l->mode2; r.mode[3] = l->mode3;
        r.ct0 = l->ct0; r.ct3 = l->ct3; r.azp = l->azp;
        r.p[0] = l->px; r.p[1] = l->py; r.p[2] = l->pz;
        r.ar[0] = l->ar; r.ar[1] = l->ah; r.ar[2] = l->car; r.ar[3] = l->cah;
        memcpy(r.mtx, o->mtx, sizeof(r.mtx));
        memcpy(r.x0, l->exp0, BW_LEG_X0);
        memcpy(out + k * sizeof(BwLeg), &r, sizeof(r));
        host_log("coop: Black Widow %d: its leg %d (off, mode %d.%d) goes with its state\n", (int)(ep - ene), k, r.mode[0], r.mode[3]);
    }
}
static int bw_legs_ok(const BH_PWORK* ep, const unsigned char* in)
{
    int k;
    for (k = 0; k < BW_LEGS; k++) {
        BwLeg r;
        memcpy(&r, in + k * sizeof(BwLeg), sizeof(r));
        if (!r.on) continue;
        if (!bw_leg(ep, k) || !bw_obj(ep, k) || r.on != 1 || r.mode[0] < 1 || r.mode[0] > 2 || r.mode[2] || r.mode[3] > 3 ||
            !finite3(r.p[0], r.p[1], r.p[2]) || !fins_n(r.ar, 4) || !fins_n(r.mtx, 16) || !fins_n((const float*)(r.x0 + 4), (0xC4 - 4) / 4) ||
            (unsigned int)*(const int*)(r.x0 + 0xC4) > 2 || !fins_n((const float*)(r.x0 + 0xC8), (BW_LEG_X0 - 0xC8) / 4))   /* (0..3: its flags) */
            return 0;
    }
    return 1;
}
static void bw_unpack(BH_PWORK* ep, const unsigned char* in)
{
    int k, j;
    for (k = 0; k < BW_LEGS; k++) {
        BH_PWORK* l = bw_leg(ep, k);
        O_WORK* o = bw_obj(ep, k);
        BwLeg r;
        memcpy(&r, in + k * sizeof(BwLeg), sizeof(r));
        if (!r.on || !l || !o) continue;
        for (j = 0; j < 3; j++) o[j].flg |= r.oflg[j];
        memcpy(o->mtx, r.mtx, sizeof(r.mtx));
        l->mode0 = r.mode[0]; l->mode1 = r.mode[1]; l->mode2 = r.mode[2]; l->mode3 = r.mode[3];
        l->ct0 = r.ct0; l->ct3 = r.ct3; l->azp = r.azp;
        l->px = r.p[0]; l->py = r.p[1]; l->pz = r.p[2];
        l->ar = r.ar[0]; l->ah = r.ar[1]; l->car = r.ar[2]; l->cah = r.ar[3];
        memcpy(l->exp0, r.x0, BW_LEG_X0);
        ep->mdflg |= 0x20;
        host_log("coop: Black Widow %d: its leg %d is off here too (mode %d.%d)\n", (int)(ep - ene), k, r.mode[0], r.mode[3]);
    }
}
_Static_assert(ESTATE_ENE + BW_LEGS * (int)sizeof(BwLeg) <= ESTATE_SIZE, "EV_ESTATE: a Black Widow with its legs");
/* bytes of one enemy's state: its work and exp0; a zombie's goes with its upper half's after ESTATE_ENE (ESTATE_PLY), a Black
   Widow with its legs off (protocol 57) */
int ene_len(const BH_PWORK* ep, int c)
{
    const OwnType* t = own_type(ep);
    return c < PC_ENE_SLOTS ? ESTATE_PLY : ep->id == 3 ? ESTATE_ENE + BW_LEGS * (int)sizeof(BwLeg) : (int)sizeof(BH_PWORK) + (t ? t->exp0 + (t->rmf ? 4 : 0) : 0);
}
static void ene_mask(unsigned char* w);
static void ene_pack(BH_PWORK* ep, unsigned char* out)
{
    const OwnType* t = own_type(ep);
    unsigned char* x = out + sizeof(BH_PWORK);
    unsigned int k;
    memcpy(out, ep, sizeof(BH_PWORK));
    ene_mask(out);   /* (protocol 56: only what ene_unpack takes; the rest, pointers and matrices, goes as zeros, which pack away) */
    memset(x, 0, ENE_EXP0);
    if (!ep->exp0 || !t) return;
    memcpy(x, ep->exp0, t->exp0);
    /* (protocol 62) what the receiver keeps as its own goes as zeros: its pointers are addresses of our process, which a
       Windows and a Linux build (or two builds) do not share - none goes on the wire (test aid RECVX_COOP_PTRSCAN) */
    for (k = 0; k < 6 && t->keep[k][1]; k++) memset(x + t->keep[k][0], 0, t->keep[k][1] - t->keep[k][0]);
    for (k = 0; k < 3 && t->atr[k]; k++) { uint32_t w = atr_enc(*(ATR_WORK**)(ep->exp0 + t->atr[k])); memcpy(x + t->atr[k], &w, 4); }
    if (ep->id == 19) { uint32_t w = ty_act_enc(*(void**)(ep->exp0 + 0x48)); memcpy(x + 0x48, &w, 4); }
    if (ep->id == 3) bw_pack(ep, out + ESTATE_ENE);
    if (t->rmf) { uint32_t w = sys->rm_flg & t->rmf; memcpy(x + t->exp0, &w, 4); }   /* (a boss: its bits of the room's flags) */
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
static void ene_mask(unsigned char* w)
{
    static unsigned char keep[sizeof(BH_PWORK)];
    unsigned int k;
    if (!keep[0]) {
        memset(keep, 1, offsetof(BH_PWORK, id));   /* flg; type (ene_unpack takes them) */
        memset(keep + offsetof(BH_PWORK, type), 1, sizeof(((BH_PWORK*)0)->type));
        for (k = 0; k < sizeof(ene_fields) / sizeof(ene_fields[0]); k++) memset(keep + ene_fields[k].from, 1, ene_fields[k].to - ene_fields[k].from);
    }
    for (k = 0; k < sizeof(BH_PWORK); k++)
        if (!keep[k]) w[k] = 0;
}
/* the modes (and type) the partner sends for an enemy of ours: within the tables its code calls through and indexes
   (coop_ene_modes_ok); `x0`: its exp0 as it will be (a zombie's upper half runs en01b's tables), NULL none */
int ene_modes_ok(const BH_PWORK* ep, int type, const unsigned char* m, const unsigned char* x0)
{
    int up = ep->id == 1 && x0 && (*(const int*)(x0 + 0x40) & 0x80000000);
    if (!coop_ene_modes_ok(ep->id, type, m[0], m[1], m[2], up)) return 0;
    if (!x0) return ep->id != 71 || m[3] < 4;
    switch (ep->id) {
    case 1:   /* en01's own tables by its work: en01_PersonalType[27] by 0x4C, en01_walk_mtn[6] / stp_tbl[6] by 0xA0 and 0xA0 + 1,
                 rot_tbl[4][8] by the byte 0x2C (en26 the same) */
        return (unsigned int)*(const int*)(x0 + 0x4C) < 27 && (unsigned int)*(const int*)(x0 + 0xA0) < 5 && x0[0x2C] < 8;
    case 26: return x0[0x2C] < 8;
    case 3: return x0[0x69] < 6 && x0[0x6A] < 6;   /* the surface it is on / goes to (en03 MakeMatrix's switch) */
    case 7: return x0[0x35] < 5;                   /* its place on the player (AtkPos[5]) */
    case 10: return x0[1] < 4 && ep->mdl[x0[1]].objP;   /* its flipbook frame: mlwP = &mdl[x0[1]] */
    case 15: return m[0] != 1 || m[1] < 5;         /* Move_func[5] by mode1 */
    case 23: return x0[0x69] < 6;
    case 12: return (unsigned int)*(const int*)(x0 + 0x1C) <= 16;   /* the head's turn, divided over its frames left (en12 LookPlayaer: 16, counted down) */
    case 16: return (unsigned int)*(const int*)(x0 + 0x64) <= 7;    /* his laser's points, EXP0_P(0..7) (en16 AddEffPos; effsub2 draws them) */
    case 71: return m[3] < 4 && ep->mdl[m[3]].objP;   /* its flipbook frame: mlwP = &mdl[mode3] */
    }
    return 1;
}
int ene_unpack(BH_PWORK* ep, const unsigned char* in, int mdl, int mnw)
{
    const BH_PWORK* r = (const BH_PWORK*)in;
    const MN_WORK* set = mnw >= 0 && mnw < COOP_MNW_MAX && ep->mnwPb ? ep->mnwPb + mnw : mnw == MNW_ROOM ? sys->rmthp : ep->mnwP;
    const OwnType* t = own_type(ep);
    const unsigned char* x = in + sizeof(BH_PWORK);
    ATR_WORK* a[3] = { 0 };
    uint32_t w;
    unsigned int k;
    if (!finite3(r->px, r->py, r->pz) || !finite3(r->pxb, r->pyb, r->pzb) || r->hp < -10000 || r->hp > 100000 ||
        !ene_modes_ok(ep, r->type, &r->mode0, ep->exp0 && t ? x : NULL) || (mdl >= 0 && (mdl >= 16 || !ep->mdl[mdl].objP)) ||
        /* (no motion set: the baby spider's frm_no counts its model frames, mdl[frm_no + 2] of its mother's) */
        (!set && !ep->mnwP && !ep->mnwPb ? r->frm_no < 0 : !mtn_ok(ep, set, r->mtn_no, r->frm_no))) {
        if (trace) fprintf(stderr, "coop: enemy %d (id %d): its state does not fit (modes %d %d %d %d type %d, motion %u frame %d of set %d: %s, model %d)\n", (int)(ep - ene), ep->id,
                           r->mode0, r->mode1, r->mode2, r->mode3, r->type, r->mtn_no, r->frm_no >> 16, mnw,
                           set && mtn_ok(ep, set, r->mtn_no, r->frm_no) ? "here" : "not here", mdl);
        return 0;
    }
    for (k = 0; ep->exp0 && t && k < 3 && t->atr[k]; k++) {   /* its collision records here, or it is kept as ours */
        memcpy(&w, x + t->atr[k], 4);
        if (!atr_dec(w, &a[k])) a[k] = *(ATR_WORK**)(ep->exp0 + t->atr[k]);
    }
    if (ep->id == 3 && ep->exp0 && t && !bw_legs_ok(ep, in + ESTATE_ENE)) return 0;
    if (ep->id == 19 && ep->exp0) {   /* the Tyrant: its action's code, and its claws' effects if it shows them (en19 Mv13/14) */
        memcpy(&w, x + 0x48, 4);
        if (w > 50 || ((*(const int*)(x + 0x40) & 0x80000008) == 0x80000008 && !(*(void**)(ep->exp0 + 0x1DC) && *(void**)(ep->exp0 + 0x1E0)))) return 0;
    }
    for (k = 0; k < sizeof(ene_fields) / sizeof(ene_fields[0]); k++)
        memcpy((char*)ep + ene_fields[k].from, in + ene_fields[k].from, ene_fields[k].to - ene_fields[k].from);
    ep->flg = (r->flg & ~0x81u) | (ep->flg & 0x81u);
    ep->type = r->type;   /* (its AI variant: a zombie crawls after a leg cut, a Cerberus or Bandersnatch woke up) */
    if (mdl >= 0) ep->mlwP = &ep->mdl[mdl];
    mnw_set(ep, mnw);
    if (!ep->exp0 || !t) return 1;
    {
        unsigned int keep22 = *(unsigned int*)(ep->exp0 + 8) & 0x81000;
        for (k = 0; k < t->exp0 / 4u; k++)
            if (!own_kept(t, 4 * k) && !own_atr(t, 4 * k)) memcpy(ep->exp0 + 4 * k, x + 4 * k, 4);
        for (k = 0; k < 3 && t->atr[k]; k++) *(ATR_WORK**)(ep->exp0 + t->atr[k]) = a[k];
        if (ep->id == 19) { memcpy(&w, x + 0x48, 4); *(void**)(ep->exp0 + 0x48) = w ? En19ActTbl[w - 1].prgP : NULL; }
        if (ep->id == 22) *(unsigned int*)(ep->exp0 + 8) = (*(unsigned int*)(ep->exp0 + 8) & ~0x81000u) | keep22;   /* (its light: ours) */
        if (t->rmf) { memcpy(&w, x + t->exp0, 4); sys->rm_flg = (sys->rm_flg & ~t->rmf) | (w & t->rmf); }
    }
    if (ep->id == 3) bw_unpack(ep, in + ESTATE_ENE);
    if (ep->id == 9 && ep->mlwP && ep->mlwP->objP && ep->mlwP->obj_num > 10)   /* the Bandersnatch's arm as its rest lengths */
        for (k = 0; k < 4; k++) ep->mlwP->objP[7 + k].pos[0] = *(float*)(ep->exp0 + 4 * k);   /* (en09 MV00; a puppet never moved it) */
    return 1;
}
#undef FLD

/* our grab state to the partner: the EV_ESTATE chunks of one transfer, whose id goes with EV_GRAB/UNGRAB/ENEOWN; n bytes of
   estate_out, packed (protocol 56) */
static int estate_events(int n) { return (coop_zpack(estate_out, n, estate_z, sizeof(estate_z)) + 63) / 64 + 1; }   /* (with the event naming it) */
static unsigned short send_estate(int n)
{
    int off, z = coop_zpack(estate_out, n, estate_z, sizeof(estate_z));
    ptr_scan(estate_out, n, "EV_ESTATE", -1);
    if (!++xfer_tid) xfer_tid = 1;
    for (off = 0; off < z; off += 64) {
        Event e = { 0 };
        e.type = EV_ESTATE;
        e.u.blob.tid = xfer_tid;
        e.u.blob.total = z;
        e.u.blob.off = off;
        e.u.blob.len = z - off < 64 ? z - off : 64;
        memcpy(e.u.blob.data, estate_z + off, e.u.blob.len);
        queue_event(&e);
    }
    return xfer_tid;
}
/* the partner's transfer `tid` complete, unpacked into estate[], and of n bytes */
int estate_take(int tid, int n)
{
    if (!tid || !estate_x.done || estate_x.tid != tid || estate_x.total < 2) return 0;
    if (estate_tid != tid) {
        memset(estate, 0, sizeof(estate));
        if (coop_zunpack(estate_zin, estate_x.total, estate, sizeof(estate)) < 0) return 0;
        estate_tid = (unsigned short)tid;
    }
    return (estate_zin[0] | estate_zin[1] << 8) == n;
}

/* an en01 grab hold: set when the grab starts, cleared by every release (~0x24080, ~0xA4080) */
int holding(BH_PWORK* ep) { return ep->exp0 && (*(int*)(ep->exp0 + 0x40) & 0x20000); }

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
struct coop_hold_s hold = { -1, 0, -1, 0, 0, 0 };
int hold_family(BH_PWORK* ep)
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
int linked_to(const BH_PWORK* ep) { return !plp ? 0 : ep->lkwkp == (unsigned char*)(swap_plp ? swap_plp : plp) ? 1 : ep->lkwkp == (unsigned char*)&ply2 ? 2 : 0; }
/* guest: an enemy of ours that holds our player or rides on it keeps running here (as grips_us) */
int runs_here(BH_PWORK* ep)
{
    if (linked_to(ep) == 1) return 1;
    if (!hold_family(ep)) return 0;
    if (!hold.said++) host_log("coop: enemy %d (id %d) holds our player: it runs here until it lets go\n", (int)(ep - ene), ep->id);
    return 1;
}
unsigned int h_stflg;            /* our player's stflg before the step (a larva laid) */
int h_busy, h_ran;               /* our player's busy flag before the step; the step ran here */
/* the step laid larvae on a player (stflg 0x8000000 << type, en06 NG00): they go for that player from their
   Init (it links them to plp), whatever the slot targeted before */
void larva_laid(unsigned int bits, int partner)
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
    if (!dead && ep->id != 1 && own_type(ep) && !own_boss(ep) && ep->mode0) {   /* (Shared Enemy Control: another type the partner's game ran, back from a puppet's
                                                               state - its own work is as it last ran here: its Move from the start) */
        *(int*)&ep->mode0 = 1;
        return;
    }
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
   the guest's here - a frozen puppet - for good. (A guest in another room in game: own_takeover.) */
#define OWN_LOST 300
/* an ownership message (EV_ENEOWN) of slot i: state, child, frame */
enum { ROOM_CLAIM = COOP_ROOM_CLAIM, ROOM_DONE = COOP_ROOM_DONE, ROOM_HOST = COOP_ROOM_HOST };   /* EV_ENEOWN 5's child */
static void own_msg(int i, int c, int state, uint32_t frame)
{
    Event e = { 0 };
    e.type = EV_ENEOWN; e.idx = i; e.u.gr.child = c; e.u.gr.state = state;
    e.u.gr.mdl = e.u.gr.cmdl = -1; e.u.gr.mnw = e.u.gr.cmnw = MNW_KEEP;
    e.u.gr.stflg_b = frame;
    queue_event(&e);
}
/* host: an enemy the guest's game ran is ours again (the guest's game is told: EV_ENEOWN state 2) */
static void own_takeback(int i, int release, const char* why)
{
    int c = own_child(&ene[i]);
    owned[i] = 0; own_wait[i] = 0; own_grab[i] = 0; own_ms[i] = 0; own_t[i] = frame_no; take_at[i] = 0; giving[i] = 0;
    if (c < PC_ENE_SLOTS) owned[c] = 0;
    if (release && (ene[i].flg & 0x1)) pc_enemy_unhold(&ene[i]);
    own_msg(i, c, 2, frame_no);
    own_n[4]++;
    host_log("coop: own: enemy %d taken back from the partner's game (%s)\n", i, why);
}
void own_timeout(void)
{
    static unsigned int seq;
    int i;
    if (!is_host || !sys || !have_rx || rx.seq == seq) return;
    seq = rx.seq;
    for (i = 0; i < PC_ENE_SLOTS; i++) {
        if (!owned[i] || (ene[i].flg & 0x80) || !(ene[i].flg & 0x1) || take_at[i]) continue;
        if (!own_grab[i] && (!same_room() || !tx_in_game)) continue;   /* (one it runs waits while either is away) */
        if (++own_wait[i] < OWN_LOST) continue;
        own_takeback(i, 1, "no snapshot of it for 10 s");
    }
}

/* our copy waits for the runner's state: not drawn (mdflg 1, its shadow and effects with it), not hit, not in the way */
void hide_on(int i, BH_PWORK* ep)
{
    if (hid[i]) return;
    hid[i] = (unsigned char)(1 | (ep->mdflg & 1) << 1 | (ep->flg & 0x60));
    ep->mdflg |= 1;
    ep->flg &= ~0x60u;
}
int coop_enemy_waiting(int i) { return i >= 0 && i < PC_ENE_SLOTS && hid[i]; }   /* (test aid RECVX_COOP_ENEPOS: H) */
void hide_off(int i)
{
    BH_PWORK* ep = &ene[i];
    if (!hid[i]) return;
    if (ep->flg & 0x1) { ep->mdflg = (ep->mdflg & ~1u) | (hid[i] >> 1 & 1); ep->flg |= hid[i] & 0x60; }
    hid[i] = 0;
}
/* hits on an enemy whose hand-over to us is under way: kept until we run it (hit_apply) */
#define DEFER_N 64
static struct { Event e; int n; } defer[DEFER_N];
int defer_n;
void defer_hit(const Event* e)
{
    if (defer_n < DEFER_N) defer[defer_n++].e = *e;
    else host_log("coop: own: no room for the hit on enemy %d: lost\n", e->idx);
}
static void defer_run(int i)
{
    int k, j;
    for (k = j = 0; k < defer_n; k++)
        if (defer[k].e.idx == i) hit_apply(&defer[k].e);
        else defer[j++] = defer[k];
    defer_n = j;
}
/* a hand-over of slot i to us that does not happen (refused as stale, or the partner took it back): the hits kept for it go to
   the partner's game, which runs it on. Before, they waited in defer[] until the slot became ours, if ever: lost */
static void defer_fwd(int i)
{
    int k, j, n = 0;
    for (k = j = 0; k < defer_n; k++)
        if (defer[k].e.idx == i) { ev_room = run_room & 0xFFFF; queue_event(&defer[k].e); ev_room = -1; n++; }
        else defer[j++] = defer[k];
    defer_n = j;
    if (n) host_log("coop: own: %d hit(s) kept for enemy %d passed on to the partner's game, which runs it\n", n, i);
}

/* slot i (and its child c) runs here now: no hand-over of it is under way any more, it shows, the hits kept for it apply */
void own_ours(int i, int c)
{
    int k, s2[2] = { i, c };
    for (k = 0; k < 2; k++) {
        int j = s2[k];
        if (j < 0 || j >= PC_ENE_SLOTS) continue;
        giving[j] = 0; take_at[j] = 0; take_how[j] = 0; prov[j] = 0;
        hide_off(j);
        seen[j] = 1;
        defer_run(j);
    }
}

/* every slot ours: the partner left the room without handing them over (its game ended, crashed or timed out; a hand-over
   that did not come in AWAY_GRACE frames), or its session restarted. One it ran goes on from what we showed of it, let go
   (unhold: a zombie stands, another type starts its Move) - its own work here is what it last ran on; one still hidden here
   is shown as our game has it */
void own_takeover(const char* why)
{
    int i, n = 0;
    for (i = 0; sys && i < PC_ENE_SLOTS; i++) {
        BH_PWORK* ep = &ene[i];
        int act = i < (int)sys->ewk_n && (ep->flg & 0x1);
        giving[i] = 0;
        if (we_run(i) || (take_at[i] && have_rx && act)) continue;   /* (a hand-over on its way completes at its frame) */
        own_set(i, 1);
        own_grab[i] = 0;
        if (!act || run_room != (coop_room_key() | my_case() << 16)) { take_at[i] = 0; hid[i] = 0; continue; }
        if (!seen[i]) hide_off(i);
        else if (!(ep->flg & 0x80) && (take_how[i] & 2 || !take_at[i])) { pc_enemy_unhold(ep); n++; }
        take_at[i] = 0; take_how[i] = 0;
        seen[i] = 1;
        defer_run(i);
    }
    memset(prov, 0, sizeof(prov));
    claim_wait = 0;
    dflt = !is_host;
    host_log("coop: own: the partner is not in the room (%s): %d of its enemies run here from what we showed\n", why, n);
}

/* drop the ownership of the room load (the session ends, a new room loads): `put` the hidden ones back as they were */
void own_reset(int put)
{
    int i;
    for (i = 0; put && sys && i < PC_ENE_SLOTS && run_room == (coop_room_key() | my_case() << 16); i++) hide_off(i);
    memset(owned, 0, sizeof(owned)); memset(owned_was, 0, sizeof(owned_was));
    memset(owned_child, 255, sizeof(owned_child));
    memset(own_grab, 0, sizeof(own_grab)); memset(own_ms, 0, sizeof(own_ms)); memset(own_wait, 0, sizeof(own_wait));
    memset(giving, 0, sizeof(giving)); memset(take_at, 0, sizeof(take_at)); memset(take_how, 0, sizeof(take_how));
    memset(keep_from, 0, sizeof(keep_from)); memset(seen, 0, sizeof(seen)); memset(hid, 0, sizeof(hid)); memset(evd, 0, sizeof(evd));
    memset(act_was, 0, sizeof(act_was)); memset(prov, 0, sizeof(prov)); memset(own_leave, 0, sizeof(own_leave)); memset(shared_want, 0, sizeof(shared_want));
    memset(hin_t, 0, sizeof(hin_t)); memset(hin_qt, 0, sizeof(hin_qt)); memset(own_ref_t, 0, sizeof(own_ref_t));
    defer_n = 0;
    memset(hist_applied, 0, sizeof(hist_applied)); memset(shown_seq, 0, sizeof(shown_seq)); memset(own_win, 0, sizeof(own_win)); memset(own_seed, 0, sizeof(own_seed));
    claim_wait = claimed = host_verdict = 0;
    dflt = -1;
    owned_room = run_room = -1;
}
/* the partner is gone (it left, timed out, a new session of it): every enemy of our room ours, the room's ownership kept */
void owned_drop(void) { if (coop_on && sys && run_room >= 0) own_takeover("it left the session"); else own_reset(1); }

/* the partner is in our room: in game there, or busy there (its menu, the item box, a scene, the trade screen; a door or
   the title say BUSY_NONE out of game) */
static int partner_present(void)
{
    return have_peer && have_rx && !echo_delay && partner_here() && rx.busy != BUSY_DOOR && ((rx.flags & PF_IN_GAME) || rx.busy != BUSY_NONE);
}
/* every room load (bhFinishRoom), also of the same room again (a Retry, a loaded save): the old load's ownership goes (before,
   a Retry of the same room kept a slot the guest ran as the guest's: our fresh copy never ran its first step and crashed on its
   empty work). Who runs the new load's enemies: the partner when it is in the room already (we are second: our copies wait
   hidden for its state), else we. The host that comes second waits for the guest's claims (the guest may have killed some,
   or have other enemies in the room): EV_ENEOWN 5 asks for them. */
void coop_room_loaded(void)
{
    int second, i;
    own_reset(0);
    if (!coop_on || echo_delay || !sys) return;
    second = partner_present();
    run_room = coop_room_key() | my_case() << 16;
    owned_room = coop_room_key();
    load_frame = rdy_t = frame_no;
    host_coming = 0;
    dflt = second ? is_host : !is_host;
    for (i = 0; i < PC_ENE_SLOTS; i++) owned[i] = owned_was[i] = (unsigned char)dflt;
    if (second && is_host) {
        memset(prov, 1, sizeof(prov));
        claim_wait = 150;
        own_msg(0, ROOM_CLAIM, 5, frame_no);   /* "claim" */
    }
    host_log("coop: own: room %d/%d loaded, the %s in it %s: %s\n", sys->stg_no, sys->rom_no, is_host ? "guest" : "host", second ? "already" : "not",
             dflt == !is_host ? "we run its enemies" : "the partner runs its enemies, ours wait for its state");
}

/* host, after the step of an enemy targeting the proxy that started a grab on it */
void grab_out(int i, BH_PWORK* ep)
{
    BH_PWORK* a = &ply2_save;
    BH_PWORK* me = swap_plp ? swap_plp : plp;   /* (our player: the step runs on the partner's stand-in) */
    int c = ene_slot(ep->exp1);
    Event e = { 0 };
    /* its attack began on our player and turned to the partner (Shared Enemy Control: a guest's zombie): our busy flag, set at the
       attack's start, is nobody's now. Left set, it made the enemy's family run here as well (runs_here, the hold)
       while the partner's game ran the grab: both games ran it for up to 100 frames */
    if (hold.by >= 0 && me && (me->flg & 4) && pc_root_in(&ene[hold.by], ene, PC_ENE_SLOTS) == pc_root_in(ep, ene, PC_ENE_SLOTS)) {
        me->flg &= ~0x10004; me->stflg &= ~0x10000;
        hold.by = -1;
        host_log("coop: enemy %d turned from our player to the partner: our busy flag cleared\n", i);
    }
    memset(estate_out, 0, sizeof(estate_out));
    ene_pack(ep, estate_out);
    if (c < PC_ENE_SLOTS) ene_pack(&ene[c], estate_out + ESTATE_ENE);
    memcpy(estate_out + ESTATE_PLY, &ply2, sizeof(BH_PWORK));
    ene_mask(estate_out + ESTATE_PLY);   /* (protocol 62: grab_in reads only allow-listed fields; the stand-in's pointers stay here) */
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
    own_set(i, 0);   /* (the host: owned[i] 1; Shared Enemy Control, a guest's zombie that grabs the host's stand-in: 0) */
    /* its steps run in the partner's game now: the hold bit our copy keeps is the stand-in's. Counted as fresh (grips_us: its
       last step ran here), the guest's copy ran on as well while another zombie of its set held the guest (65-105 frames) */
    gave_grab[i] = 1;
    if (c < PC_ENE_SLOTS) gave_grab[c] = 1;
    own_grab[i] = 1;
    own_t[i] = frame_no;
    owned_child[i] = c;
    own_wait[i] = 0;
    if (c < PC_ENE_SLOTS) own_set(c, 0);
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
        e.u.gr.tid = send_estate(ene_len(&ene[i], c));
        e.u.gr.mdl = mdl_of(&ene[i]); e.u.gr.mnw = mnw_of(&ene[i]);
        if (c < PC_ENE_SLOTS) { e.u.gr.cmdl = mdl_of(&ene[c]); e.u.gr.cmnw = mnw_of(&ene[c]); }
    }
    e.type = EV_UNGRAB;
    e.idx = i;
    e.u.gr.child = c;
    e.u.gr.state = state;
    queue_event(&e);
    own_set(i, 0);
    own_set(c, 0);
    if (i < PC_ENE_SLOTS) own_grab[i] = 0;
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

#define GRAB_EASE 8                     /* frames (grab_in) */
struct coop_gease_s gease = { -1 };
/* after the holder's step: our player slides from where it was to the grab's place (grab_in). The zombie's link (exp0 0x40
   0x80000, bhEne01_PlayerLink) puts our player at its place each step: the offset left is added to that; without it the step
   leaves our player where it was, and the offset is taken off by a share each step */
void grab_ease(BH_PWORK* ep)
{
    int linked = ep->id == 1 && ep->exp0 && (*(int*)(ep->exp0 + 0x40) & 0x80000);
    float now, f;
    unsigned int keep;
    if (!(ep->flg & 0x1) || !we_run(gease.slot) || (plp->mode0 != 4 && plp->mode0 != 6)) { gease.n = 0; return; }
    gease.n--;
    now = (float)gease.n / gease.len;
    f = linked ? now : now - (float)(gease.n + 1) / gease.len;
    keep = plp->flg & 0xC0000;   /* (move_player lets go of the planted foot: the grab's own business) */
    move_player(plp->px + gease.dx * f, plp->pz + gease.dz * f);
    plp->flg |= keep;
}
/* client: the host's enemy grabbed us. Taken only as a whole: the transfer the grab names is
   complete, the slot holds a zombie here (en01, the only grab handed over) with the same linked
   child, the state is plausible and its position not behind a wall; otherwise the host gets the
   grab back and lets go. */
void grab_in(const Event* e)
{
    const BH_PWORK* a = (const BH_PWORK*)(estate + ESTATE_PLY);
    const MN_WORK* set;
    BH_PWORK* ep;
    float was_x, was_z;
    int i = e->idx, c, have = estate_take(e->u.gr.tid, ESTATE_SIZE);
    unsigned int x;
    if (!have) memset(estate + ESTATE_PLY, 0, sizeof(BH_PWORK));   /* (no state: refused below) */
    /* late: our own copy of that enemy grabbed us meanwhile (its step ran here: the host left mid-attack and our game took
       over, or its snapshot of the grab's start). Before, refused: the enemy became the host's puppet frozen in its grab
       while our player played the grab with nobody holding it, and the failsafe saw that puppet as the holder. Our game
       runs that grab: it is ours, handed back at its end as any grab of the host's */
    for (c = 0; c < (int)sys->ewk_n && c < PC_ENE_SLOTS && (c == i || !(ene[c].flg & 0x1) || !grips_us(&ene[c])); c++) ;   /* (another holds us: c) */
    if (i < PC_ENE_SLOTS && i < (int)sys->ewk_n && e->stg == sys->stg_no && e->rom == sys->rom_no && (ene[i].flg & 0x1) && ene[i].id == 1 &&
        (plp->mode0 == 4 || plp->mode0 == 6) && holding(&ene[i]) && step_ran[i] && !we_run(i) && (c >= (int)sys->ewk_n || c >= PC_ENE_SLOTS)) {
        c = ene_slot(ene[i].exp1);
        own_set(i, 1);
        if (c < PC_ENE_SLOTS) own_set(c, 1);
        own_grab[i] = 1; own_t[i] = frame_no; owned_room = coop_room_key();
        own_ours(i, c);
        host_log("coop: enemy %d grabs us, as our copy of it does already: ours runs it\n", i);
        return;
    }
    if (i >= PC_ENE_SLOTS || i >= (int)sys->ewk_n || e->stg != sys->stg_no || e->rom != sys->rom_no || !(ene[i].flg & 0x1) ||
        !tx_in_game || (sys->cb_flg & 0x5) || plp->mode0 == 3 || plp->mode0 == 4 || plp->mode0 == 6 || plp->hp < 0 || we_run(i) ||
        (a->px - plp->px) * (a->px - plp->px) + (a->pz - plp->pz) * (a->pz - plp->pz) > 10.0f * 10.0f) {   /* late: we moved on */
        host_log("coop: enemy %d grabs us, but not now (room %d, active %d, in game %d, cb %x, mode %d, hp %d, owned %d, %.1f m off)\n", i, e->stg == sys->stg_no && e->rom == sys->rom_no, i < PC_ENE_SLOTS ? ene[i].flg & 1 : 0, tx_in_game, sys->cb_flg & 5, plp->mode0, plp->hp, i < PC_ENE_SLOTS ? owned[i] : 0, sqrtf((a->px - plp->px) * (a->px - plp->px) + (a->pz - plp->pz) * (a->pz - plp->pz)) / 10);
        grab_back(i, 0, e->u.gr.child);
        return;
    }
    ep = &ene[i];
    c = ene_slot(ep->exp1);
    set = !e->u.gr.pset ? plp->mnwPb : e->u.gr.mnw >= 0 && e->u.gr.mnw < COOP_MNW_MAX && ep->mnwPb ? ep->mnwPb + e->u.gr.mnw
                                     : e->u.gr.mnw == MNW_ROOM ? sys->rmthp : ep->mnwP;
    if (!have || ep->id != 1 || !ep->exp0 ||
        c != child_here(i, e->u.gr.child) || (c < PC_ENE_SLOTS && !(ene[c].flg & 0x1)) || !finite3(a->px, a->py, a->pz) || !mtn_ok(ep, set, a->mtn_no, a->frm_no) ||
        wall_between(plp, a) || !ene_unpack(ep, estate, e->u.gr.mdl, e->u.gr.mnw)) {
        host_log("coop: enemy %d grabs us, but its state does not fit here\n", i);
        grab_back(i, 0, e->u.gr.child);
        return;
    }
    if (c < PC_ENE_SLOTS && ene_unpack(&ene[c], estate + ESTATE_ENE, e->u.gr.cmdl, e->u.gr.cmnw)) own_set(c, 1);
    own_set(i, 1);
    own_grab[i] = 1;
    own_t[i] = frame_no;
    owned_room = coop_room_key();
    own_ours(i, c);
    /* our player as the grab's start left the proxy: modes, motion, position, and the bits
       the enemy changed */
    was_x = plp->px; was_z = plp->pz;
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
    /* (GRAB_EASE) the grab's place is where the partner's game saw our player (a pose a path's time old): our player stays where it
       is and slides there over a few frames (grab_ease); the zombie is as the partner's game had it. Before, our player was put
       there at once: up to 10 units in a frame (2 to 3 without a hit). (Moving the zombie instead made it jump in the giver's game) */
    {
        float dx = was_x - plp->px, dz = was_z - plp->pz;
        if (dx * dx + dz * dz > 0.25f) {
            plp->px = was_x; plp->pz = was_z;   /* (where its collision points are) */
            gease.slot = i; gease.dx = dx; gease.dz = dz; gease.n = gease.len = GRAB_EASE;
        }
    }
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
void move_player(float x, float z)
{
    EXP_WORK* e = (EXP_WORK*)plp->exp0;
    float dx = x - plp->px, dz = z - plp->pz;
    plp->px += dx; plp->pxb += dx; plp->pz += dz; plp->pzb += dz;
    plp->flg &= ~0xC0000;
    if (e) { e->fpx += dx; e->bpx += dx; e->bpxb += dx; e->spx += dx; e->plx += dx; e->fpz += dz; e->bpz += dz; e->bpzb += dz; e->spz += dz; e->plz += dz; }
}

/* our player out of a grab, as en01 releases it */
void player_let_go(void)
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
struct coop_safe_s safe;

void grab_failsafe(void)
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
void hold_watch(void)
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

/* at the hand-off: a grab handed to our game is over when the enemy lets go (or is gone). The enemy runs on here (EV_UNGRAB
   state 2, protocol 56 also without Shared Enemy Control): before, it went back to the host with its state, a second hand-over
   right at the player (a wobble, its hits of those frames lost). A record the room's scripts act on goes to the host by the rule
   for those (own_frame) */
void grab_check(void)
{
    int i;
    for (i = 0; i < PC_ENE_SLOTS; i++) {
        BH_PWORK* ep = &ene[i];
        if (!own_grab[i] || !we_run(i) || (ep->flg & 0x80)) continue;   /* children go with their parent */
        if (owned_room != coop_room_key() || i >= (int)sys->ewk_n || !(ep->flg & 0x1)) { own_grab[i] = 0; continue; }
        if (!holding(ep) && plp->mnwP != ep->mnwP && tx_in_game) {
            Event e = { 0 };
            e.type = EV_UNGRAB; e.idx = i; e.u.gr.child = ene_slot(ep->exp1); e.u.gr.state = 2;
            e.u.gr.mdl = e.u.gr.cmdl = -1; e.u.gr.mnw = e.u.gr.cmnw = MNW_KEEP;
            queue_event(&e);
            own_grab[i] = 0;
            own_t[i] = frame_no;
            host_log("coop: the grab of enemy %d ends, it runs on here\n", i);
        }
    }
}

/* ---------------------------------------------------------------- the hand-over (see owned[] above) */
/* an enemy that may change hands now: one of own_types in its Move between attacks (walking, standing, chasing: not
   attacking, holding, leaping, climbing, hit), alive, awake (hittable: flg 0x20; not a waiting variant), not driven by an
   event, not a record the room's scripts act on (enemy_rec_scripted: the scripts run on the host), not the holder of our
   player's busy flag. Per type (the conditions from its en*.c code, docs/COOP.md):
   zombie: not a room script's waiting type 8, not on stairs, with its upper half (the linked zombie that poses its arms and head);
   Cerberus (en04): mode2 idle, wander, chase or search, its Brain on (mode1 1), awake (type 0), on the ground, not on
   stairs, airborne, holding or linked to a player (exp0 0x10); Hunter (en05): standing, walking, running or getting up
   after a hit (mode2 16), its Brain on, type 0/1,
   no attack, chained damage or beheading under way (flg, exp0 0x80), not in a jump; Bandersnatch (en09): idle or walking,
   its Brain on, type 0..8 (9 drops in, 10 sleeps), on the ground, its arm in, no climb, jump or hold (exp0 0x18);
   anatomist (en26): idle, walking or chasing, no hold or player link (exp0 0x40) */
static int own_movable(int i)
{
    BH_PWORK* ep = &ene[i];
    int c, m2 = ep->mode2, x;
    if (i >= (int)sys->ewk_n || (ep->flg & 0xA3) != 0x21 || !own_shared(ep) || !ep->exp0 || ep->mode0 != 1 || ep->hp < 0 ||
        (ep->stflg & 0x41000000) || event_driven(ep) || enemy_rec_scripted(i) || hold_family(ep) || own_grab[i] || ghost.slot == i)
        return 0;
    switch (ep->id) {
    case 4:
        x = *(int*)(ep->exp0 + 0x10);
        return ep->mode1 == 1 && (m2 <= 2 || m2 == 6) && !ep->type && (x & 0xF) == 1 && (x & 0x20) && !(x & 0x461F0410);
    case 5:
        return ep->mode1 == 1 && (m2 <= 2 || m2 == 16) && ep->type < 2 && !(ep->flg & 0x49000000) && !ep->exp0[0x80] && (signed char)ep->flr_no != -128;
    case 9:
        x = *(int*)(ep->exp0 + 0x18);
        return ep->mode1 == 1 && m2 <= 1 && ep->type <= 8 && (x & 0xF) == 1 && !(x & 0xF6EDE1F0);
    case 26:
        return m2 <= 2 && !(*(int*)(ep->exp0 + 0x40) & 0xA0000) && !(ep->flg2 & 1);
    }
    /* (not on stairs: MV08, flg2 1, exp0 0x40 0x400000 - the stair's in-use mark, its ATR_WORK at 0x74, stay in the game
       that set them: the other zombies there kept off those stairs for the visit) */
    if (ep->type == 8 || holding(ep) || m2 == 8 || (ep->flg2 & 1) || (*(int*)(ep->exp0 + 0x40) & 0x400000)) return 0;
    c = own_child(ep);
    return c < PC_ENE_SLOTS && c < (int)sys->ewk_n && (ene[c].flg & 0x81) == 0x81 && ene[c].lkwkp == (unsigned char*)ep && ene[c].exp0 &&
           we_run(c) && !event_driven(&ene[c]);
}
/* who should run it (1 the partner): the only one of the two that shot it lately (its hits then react at once in the
   shooter's game), unless it is right at the other player it goes for (COOP_TGT_TOUCH: its attack is due - the victim's
   game runs it); else its target (coop_pick_target, with its margins) */
static int shared_pref(int i)
{
    const BH_PWORK* ep = &ene[i];
    int t = tgt[i] != 0, h0 = hit_t[i][0] && frame_no + 1 - hit_t[i][0] < SHARED_HIT, h1 = hit_t[i][1] && frame_no + 1 - hit_t[i][1] < SHARED_HIT;
    const BH_PWORK* v = t ? &ply2 : plp;
    float dx = ep->px - v->px, dz = ep->pz - v->pz;
    if (h0 != h1 && (t == h1 || dx * dx + dz * dz >= COOP_TGT_TOUCH * COOP_TGT_TOUCH)) return h1;
    return t;
}
static int hin_ok(int k, int any);
static int hin_any_event;               /* hin_ok: also one an event of ours drives (we leave the room: our event ends) */
static int hin_leave;                   /* hin_ok: we leave the room (or stop our enemies): it goes to the partner's game for good */
#define HIN_WAIT 90
/* hand enemy i (with its upper half) to the partner's game (EV_ENEOWN 1, protocol 56): with its state (`state`: ene_pack) or
   without it (the receiver lets it go from its puppet); `leaving`: we leave the room (a door, our menu, a scene that stops our
   enemies) and stop running it now, else we run it on until the partner says it runs it (6) or refuses (0). The partner
   takes it at the frame of the state (stflg_b: our frame) as its playout shows it */
static void own_give(int i, int leaving, int state, const char* why)
{
    BH_PWORK* ep = &ene[i];
    int c = own_child(ep);
    Event e = { 0 };
    e.u.gr.mdl = e.u.gr.cmdl = -1; e.u.gr.mnw = e.u.gr.cmnw = MNW_KEEP;
    if (state) {
        memset(estate_out, 0, sizeof(estate_out));
        ene_pack(ep, estate_out);
        if (c < PC_ENE_SLOTS) ene_pack(&ene[c], estate_out + ESTATE_ENE);
        e.u.gr.tid = send_estate(ene_len(ep, c));
        e.u.gr.mdl = mdl_of(ep); e.u.gr.mnw = mnw_of(ep);
        if (c < PC_ENE_SLOTS) { e.u.gr.cmdl = mdl_of(&ene[c]); e.u.gr.cmnw = mnw_of(&ene[c]); }
    }
    e.type = EV_ENEOWN;
    e.idx = i;
    e.u.gr.child = c;
    e.u.gr.state = 1;
    e.u.gr.pset = (unsigned char)leaving;
    e.u.gr.flg_b = ep->id | ep->mdlver << 8;   /* what the receiver's slot must hold */
    e.u.gr.stflg_b = frame_no;                 /* our frame of the state: the receiver runs it from there */
    queue_event(&e);
    /* a hit our copy has not taken yet goes after it (with its state it is in the state's damage fields) */
    if (!state && ep->mode0) {
        hit_send(i, ep, 0);
        if (c < PC_ENE_SLOTS && ene[c].mode0) hit_send(c, &ene[c], 0);
    }
    if (leaving) { own_set(i, 0); own_set(c, 0); giving[i] = 0; if (c < PC_ENE_SLOTS) giving[c] = 0; dflt = is_host; }   /* (and what comes up: the partner's) */
    else { giving[i] = 1; if (c < PC_ENE_SLOTS) giving[c] = 1; }
    own_t[i] = frame_no; own_ms[i] = net_ms(); shared_want[i] = 0; tgt[i] = 0; own_leave[i] = 0;
    snap_at[i] = frame_no;
    own_n[leaving ? 3 : 0]++;
    host_log("coop: own: enemy %d (id %d) handed to the partner's game (%s%s) at frame %u, %.1f m from us, %.1f m from the partner (mode %d.%d.%d.%d)\n", i, ep->id, why,
             state ? "" : ", without its state", frame_no,
             sqrtf((ep->px - plp->px) * (ep->px - plp->px) + (ep->pz - plp->pz) * (ep->pz - plp->pz)) / 10,
             sqrtf((ep->px - ply2.px) * (ep->px - ply2.px) + (ep->pz - ply2.pz) * (ep->pz - ply2.pz)) / 10, ep->mode0, ep->mode1, ep->mode2, ep->mode3);
}
/* the partner hands us enemy i (EV_ENEOWN 1): taken only as the same enemy (type and look, the same upper half), not one an
   event of ours drives, not in a door; its state goes into our copy at once (a puppet still: the snapshots it shows meanwhile
   change only what they carry, its own work is the state's) and it is ours once the playout shows the state's frame
   (own_frame). Without its state (or a state that does not fit) only when the giver leaves: it is let go from the puppet then.
   Refused: 0, the giver runs it on (2: we run it already) */
static void own_take(const Event* e)
{
    int i = e->idx, c = child_here(e->idx, e->u.gr.child), leaving = e->u.gr.pset, ok, st = 0;
    BH_PWORK* ep = &ene[i];
    if (i >= PC_ENE_SLOTS) return;
    ok = e->stg == sys->stg_no && e->rom == sys->rom_no && run_room == (coop_room_key() | my_case() << 16) && !(sys->cb_flg & 0x1) &&
         i < (int)sys->ewk_n && (ep->flg & 0x81) == 0x1 && !we_run(i) && !take_at[i] &&
         ((ep->id | ep->mdlver << 8) == e->u.gr.flg_b || (ep->id == (e->u.gr.flg_b & 0xFF) && enemy_room_differs())) && own_child(ep) == c &&
         (c >= PC_ENE_SLOTS || (c < (int)sys->ewk_n && (ene[c].flg & 0x1) && !we_run(c)));
    if (ok && e->u.gr.tid && own_work(ep) && !event_driven(ep) && estate_take(e->u.gr.tid, ene_len(ep, c)) && ene_unpack(ep, estate, e->u.gr.mdl, e->u.gr.mnw) &&
        (c >= PC_ENE_SLOTS || ene_unpack(&ene[c], estate + ESTATE_ENE, e->u.gr.cmdl, e->u.gr.cmnw)))
        st = 1;
    if (!ok || (!st && !leaving)) {
        ev_room = e->stg * 256 + e->rom;   /* (the giver's room: we may be in a door to the next one) */
        own_msg(i, c, i < PC_ENE_SLOTS && we_run(i) ? 2 : 0, frame_no);
        ev_room = -1;
        own_n[2]++;
        host_log("coop: own: enemy %d from the partner's game does not fit here: refused%s\n", i, we_run(i) ? " (we run it)" : !ok ? "" : " (its state)");
        return;
    }
    take_at[i] = e->u.gr.stflg_b ? e->u.gr.stflg_b : 1;
    take_how[i] = (unsigned char)(leaving | (st ? 0 : 2));
    if (st) { seen[i] = 1; hid[i] = 0; }   /* (its state is shown from now: the snapshots of the frames before it change only what they carry) */
    if (c < PC_ENE_SLOTS) { take_at[c] = take_at[i]; take_how[c] = take_how[i]; if (st) { seen[c] = 1; hid[c] = 0; } }
    host_log("coop: own: enemy %d (id %d) comes from the partner's game%s%s%s: ours from its frame %u\n", i, ep->id, leaving ? " (it leaves)" : "",
             st ? "" : " without its state", st || !e->u.gr.tid ? "" : !own_work(ep) ? " (no work here)" : event_driven(ep) ? " (an event of ours drives it)" :
             !estate_take(e->u.gr.tid, ene_len(ep, c)) ? " (its transfer)" : " (it does not fit here)", take_at[i]);
}
/* a hand-over to us is ours: the playout shows its frame (or the partner is not in the room any more) */
static void own_took(int i)
{
    BH_PWORK* ep = &ene[i];
    int c = own_child(ep), how = take_how[i];
    own_set(i, 1); own_set(c, 1);
    if (how & 1) dflt = !is_host;   /* (the giver left: what comes up is ours) */
    if ((how & 2) && (ep->flg & 0x1) && seen[i]) pc_enemy_unhold(ep);   /* (without its state: let go from the puppet) */
    own_ours(i, c < PC_ENE_SLOTS ? c : -1);
    own_t[i] = frame_no; shared_want[i] = 0; tgt[i] = 0;
    own_n[1]++;
    ev_room = run_room & 0xFFFF;   /* (the room of the enemy: we may be in a door out of it) */
    own_msg(i, c, 6, frame_no);   /* "we run it": the giver stops (and drops what it showed of us before this frame) */
    ev_room = -1;
    host_log("coop: own: enemy %d is ours now\n", i);
}
/* the partner's EV_ENEOWN 0, 1, 2, 6 */
void own_apply(const Event* e)
{
    int i = e->idx, c = child_here(e->idx, e->u.gr.child);
    BH_PWORK* ep = &ene[i];
    if (!sys || !plp || i >= PC_ENE_SLOTS) return;
    if (e->u.gr.state == 1) { own_take(e); return; }
    if (e->stg != sys->stg_no || e->rom != sys->rom_no || run_room != (coop_room_key() | my_case() << 16)) return;   /* (another room's) */
    if (e->u.gr.state == 6) {   /* it runs it now (from its frame stflg_b): we stop, its frames before are not ours to show */
        if (giving[i]) { own_set(i, 0); own_set(c, 0); giving[i] = 0; if (c < PC_ENE_SLOTS) giving[c] = 0; }
        if (!we_run(i)) { keep_from[i] = e->u.gr.stflg_b; if (c < PC_ENE_SLOTS) keep_from[c] = e->u.gr.stflg_b; }
        return;
    }
    if (e->u.gr.state == 0) {   /* refused: ours again (one we gave leaving: it runs here when our game runs its enemies) */
        if (giving[i]) { giving[i] = 0; if (c < PC_ENE_SLOTS) giving[c] = 0; }
        else if (!we_run(i) && !take_at[i] && (ep->flg & 0x1) && own_child(ep) == c) { own_set(i, 1); own_set(c, 1); }
        else return;
        own_t[i] = frame_no; own_ms[i] = 0; own_ref_t[i] = frame_no + 1;
        own_n[2]++;
        host_log("coop: own: enemy %d: the partner's game did not take it, it runs on here\n", i);
        return;
    }
    if (e->u.gr.state == 2) {   /* taken back by the host (or a hand-over crossed one of the partner's): it runs it */
        if (!we_run(i) && take_at[i]) {   /* its hand-over to us is void: the partner runs it on. Before, take_at stayed, we took it at
                                             its frame and sent 6, which the partner ignored: both games ran it to the end of the room
                                             load (a scene of the host's stops its enemies, gives them away leaving, then its event
                                             drives one of them; pc/tools/coop_scene_check.sh) */
            int k, s2[2] = { i, c };
            for (k = 0; k < 2; k++)
                if (s2[k] >= 0 && s2[k] < PC_ENE_SLOTS) { take_at[s2[k]] = 0; take_how[s2[k]] = 0; defer_fwd(s2[k]); }
            host_log("coop: own: enemy %d: its hand-over to us is void, the partner's game runs it\n", i);
            return;
        }
        if (!we_run(i) || (is_host && !giving[i])) return;   /* (the host keeps one both took) */
        own_set(i, 0); own_set(c, 0); own_grab[i] = 0;
        giving[i] = 0; if (c < PC_ENE_SLOTS) giving[c] = 0;
        host_log("coop: own: enemy %d: the partner's game runs it\n", i);
    }
}

/* host -> guest: the guest's claims (3, answered 4/2), its "claims done" (5), the host's verdict */
static void claim_room(void);
void own_room_msg(const Event* e)
{
    int i, kind = e->u.gr.child;
    if (!sys || e->stg != sys->stg_no || e->rom != sys->rom_no || run_room < 0) return;
    if (!is_host && kind == ROOM_CLAIM) {   /* the host came into our room: we run it (also when we showed the host's: it loaded the room again) */
        own_takeover("the host loaded the room we are in");
        host_coming = 1;
        claimed = 0;
        claim_room();
        return;
    }
    if (!is_host && kind == ROOM_HOST) {   /* both came at once: the host's room */
        for (i = 0; i < PC_ENE_SLOTS; i++)
            if (we_run(i) && !own_grab[i]) { own_set(i, 0); giving[i] = 0; take_at[i] = 0; }
        dflt = 0; claimed = 1;
        host_log("coop: own: the host runs this room's enemies (it came in with us)\n");
        return;
    }
    if (is_host && kind == ROOM_DONE) {   /* every claim of the guest came: the rest is ours */
        int n = 0;
        if (!claim_wait) return;   /* (each claim had its answer: ours run here) */
        for (i = 0; i < PC_ENE_SLOTS; i++)
            if (prov[i] && !hin_qt[i]) {   /* (a claim still waiting for our copy to be set up keeps it: hin_answer) */
                if (i < (int)sys->ewk_n && (ene[i].flg & 0x81) == 0x1) { own_set(i, 1); own_ours(i, -1); n++; }
                prov[i] = 0;
            }
        claim_wait = 0;
        host_log("coop: own: the guest's claims are in: %d of the room's enemies run here, the rest in the guest's game\n", n);
    }
}

/* at the hand-off, after the snapshots: who runs what */
void own_frame(void)
{
    int i, pres, runs, paused, leave, door, budget, me;
    static int tuned, paused_n;
    if (!tuned++ && getenv("RECVX_COOP_P3") && sscanf(getenv("RECVX_COOP_P3"), "%d,%d,%d", &SHARED_GIVE, &SHARED_HOLD, &SHARED_HIT) == 3)
        host_log("coop: p3: test: give %d, hold %d, hit %d\n", SHARED_GIVE, SHARED_HOLD, SHARED_HIT);
    if (!coop_on || echo_delay || !sys || !plp || run_room < 0) return;
    me = coop_room_key() | my_case() << 16;
    /* (protocol 57) the frames count from our first one in the room, not from its load: a door longer than 30 frames took the
       enemy our entry event drives back at once, from where its record put it (the guest's state not applied yet: 55 units).
       Before the returns below (and from coop_room_loaded): a room entered without a door (a loaded save, a scripted change)
       had the grace of the last room used up */
    if (!room_ready() || run_room != me) rdy_t = frame_no;
    /* the partner in the room our enemies are of (also while we are in a door out of it) */
    pres = have_peer && have_rx && (rx.stg * 256 + (unsigned char)rx.rom) == (run_room & 0xFFFF) && (!rx.rcase || rx.rcase - 1 == (run_room >> 16)) &&
           rx.busy != BUSY_DOOR && ((rx.flags & PF_IN_GAME) || rx.busy != BUSY_NONE);
    if (pres) host_coming = 0;
    if (have_rx) away_n = pres || host_coming ? 0 : away_n + 1;   /* (the host coming in: its door's fade is no leave; before, a fade
                                                    of 45 frames gave the guest back what its claims had left to the host: two runners) */   /* (no word from it: a network outage holds its enemies where they are, as
                                                   they were before it, until it is back or the session ends) */
    /* hand-overs to us whose frame the playout shows now (or whose giver is gone). Not during a network outage: the giver runs it
       on meanwhile (make before break). One whose frame we showed long ago already (TAKE_STALE: it came after an outage or a long
       resend) is refused while its giver stays in the room: the giver runs it on and gives it again with its state then (before,
       it was taken from that frame, and the giver's view of it went back there: 11 units after a 5 s outage) */
    for (i = 0; i < PC_ENE_SLOTS; i++) {
        int po = have_rx && playout.started;
        if (!take_at[i] || (i < (int)sys->ewk_n && (ene[i].flg & 0x80) && own_child((BH_PWORK*)ene[i].lkwkp) == i)) continue;
        if (po && pres && run_room == me && !(take_how[i] & 1) && (int32_t)(playout.P - take_at[i]) > TAKE_STALE) {
            int c = i < (int)sys->ewk_n ? own_child(&ene[i]) : PC_ENE_SLOTS;
            host_log("coop: own: enemy %d from the partner's game: its frame %u was shown %d frames ago, refused (it runs on there)\n", i, take_at[i], (int)(playout.P - take_at[i]));
            ev_room = run_room & 0xFFFF;
            own_msg(i, c, 0, frame_no);
            ev_room = -1;
            take_at[i] = 0; take_how[i] = 0; defer_fwd(i);
            if (c < PC_ENE_SLOTS) { take_at[c] = 0; take_how[c] = 0; defer_fwd(c); }
            own_n[2]++;
            continue;
        }
        if ((po && ((int32_t)(playout.P - take_at[i]) >= 0 || (int32_t)(playout.newest - take_at[i]) > 60)) || run_room != me)
            own_took(i);
    }
    /* not there: gone, in game elsewhere, or away (a door, a load) for AWAY_GRACE frames without handing us what it ran */
    if (!have_peer || (!pres && have_rx && (((rx.flags & PF_IN_GAME) && rx.busy != BUSY_DOOR) || away_n >= AWAY_GRACE))) {
        for (i = 0; i < PC_ENE_SLOTS && (we_run(i) || take_at[i]); i++) ;
        if (i < PC_ENE_SLOTS) own_takeover(!have_peer ? "the session ended" : (rx.flags & PF_IN_GAME) ? "it plays in another room" : "it is away");
        for (i = 0; i < (int)sys->ewk_n && i < PC_ENE_SLOTS; i++) act_was[i] = (ene[i].flg & 0x1) != 0;
        return;
    }
    /* a slot that comes up (an extra at the room's first frame, a spawned enemy, a slot used again): the room's runner's. One the
       partner runs whose state has not come is hidden also while our game steps no enemies (the door's fade, a scene): it
       would be drawn where its record put it */
    for (i = 0; i < (int)sys->ewk_n && i < PC_ENE_SLOTS; i++) {
        int act = (ene[i].flg & 0x1) != 0;
        if (act && !act_was[i] && !take_at[i] && !giving[i] && !own_grab[i] && dflt >= 0 && run_room == me) {
            /* (a boss or its part, no type of the hand-overs, coming up while both play in the room: the host's, as the
               host runs the fight - its entry script re-arms it. Before, the guest's game ran Alexia 2's babies that came up
               after the host's coming, the host's too: two runners, their HP apart for good) */
            owned[i] = (unsigned char)((!own_type(&ene[i]) || own_boss(&ene[i])) && (is_host ? pres && !(sys->cb_flg & 0x1) : claimed && have_rx && partner_here()) ? 0 : dflt);   /* (host: not in
                                                    our door's fade, the room's load, whose enemies the claims sort out; guest: once the
                                                    host came, it claimed what it runs) */
            if (we_run(i)) { seen[i] = 1; hide_off(i); }
        }
        act_was[i] = (unsigned char)act;
        if (act && !seen[i] && !we_run(i) && ene[i].mode0 && run_room == me && (is_host || !event_driven(&ene[i]))) hide_on(i, &ene[i]);
    }
    /* host: the guest's claims did not come */
    if (claim_wait && !--claim_wait) {
        int n = 0;
        for (i = 0; i < PC_ENE_SLOTS; i++)
            if (prov[i] && !hin_qt[i]) { if (!we_run(i)) n++; own_set(i, 1); own_ours(i, -1); prov[i] = 0; }
        dflt = 0;
        if (!host_verdict++) own_msg(0, ROOM_HOST, 5, frame_no);
        host_log("coop: own: no claims of the guest for this room: we run its %d enemies\n", n);
    }
    /* we leave (a door, a menu, a scene that stops our enemies) while the partner plays on in the room: what we run goes to its
       game. Our enemies are the room's still until our next room loads (run_room), so the hand-over may take a few frames */
    runs = pres && (rx.flags & PF_IN_GAME) && rx.busy != BUSY_PAUSED;
    door = run_room != me || (sys->cb_flg & 0x1);
    paused = !tx_in_game || door || !((sys->sp_flg & 0x2) || coop_enemies_run()) || aid_scene_on();
    /* (a scene or message that stops our enemies counts after PAUSE_LEAVE frames: a short one hands nothing over and back) */
    paused_n = paused ? paused_n + 1 : 0;
    leave = runs && (door || !tx_in_game || paused_n >= PAUSE_LEAVE) && !frozen;
    budget = (int)(COOP_RING * 3 / 4) - (int)(ring.next - ring.head);
    for (i = 0; i < (int)sys->ewk_n && i < PC_ENE_SLOTS; i++) {
        BH_PWORK* ep = &ene[i];
        if (own_grab[i] && !we_run(i) && (!(ep->flg & 0x1) || (frame_no - own_t[i] > 90 && !ply_busy(&ply2)))) own_grab[i] = 0;   /* (no word of its end) */
        /* (once the guest's state of it is here: before, the host's copy was taken back from where its record put it) */
        /* (not while we leave: the event ends in our door, the partner runs it on; before, our door gave it and took it back every
           other frame, the partner's game refusing the gives as it ran it) */
        if (is_host && !door && !we_run(i) && !own_grab[i] && !take_at[i] && (seen[i] || frame_no - rdy_t > 30) && (ep->flg & 0x1) && event_driven(ep)) {
            seen[i] = 1; hide_off(i);
            own_takeback(i, 0, "an event of ours drives it");
            continue;
        }
        if (!we_run(i)) { shared_want[i] = own_leave[i] = 0; continue; }
        if (giving[i] && !(ep->flg & 0x80) && frame_no - own_t[i] > 300) {   /* (no answer to a hand-over in 10 s: ours as before) */
            int c = own_child(ep);
            giving[i] = 0; if (c < PC_ENE_SLOTS) giving[c] = 0;
            host_log("coop: own: enemy %d: no answer to its hand-over, it runs on here\n", i);
        }
        if (giving[i] || own_grab[i] || (ep->flg & 0x81) != 0x1 || (event_driven(ep) && !door)) continue;   /* (an event of ours drives it: it runs
                                                    here, until we leave the room: then our event ends) */
        if (leave) {
            int st, need;
            hin_any_event = door; hin_leave = 1;
            st = own_work(ep) && hin_ok(i, 1);
            hin_any_event = hin_leave = 0;
            need = st ? estate_events(ene_len(ep, own_child(ep))) : 1;
            if (own_ref_t[i] && frame_no + 1 - own_ref_t[i] < (unsigned int)SHARED_HOLD) continue;   /* (refused: again later) */
            if (budget < need) continue;
            if (!st && !door && (!own_type(ep) || own_boss(ep))) continue;   /* (a boss or a part without a work we can send: it stays ours while our menu
                                                    or scene stops it, as in single player; given without its state the partner's game ran
                                                    it on from its puppet's pose, its Move from the start - Alexia's fight reset there) */
            if (!st && !door && ++own_leave[i] < LEAVE_WAIT) continue;   /* (in a menu: it may come into a state to go) */
            ev_room = run_room & 0xFFFF;   /* (in a door ours may be the next room's already) */
            own_give(i, 1, st, door ? "we leave the room" : "our game stops its enemies");
            ev_room = -1;
            if (i == aid_scene.slot && aid_scene_on()) { aid_scene.gave = 1; seen[i] = 1; own_takeback(i, 0, "an event of ours drives it"); }   /* (test aid) */
            budget -= need;
            continue;
        }
        own_leave[i] = 0;
        if (!runs || door) { shared_want[i] = 0; continue; }
        /* a record the room's scripts act on runs in the host's game while it plays in the room (the scripts run there) */
        if (!is_host && enemy_rec_scripted(i)) {
            int any = ++hin_t[i] >= HIN_WAIT, need;
            if (!own_work(ep) || !hin_ok(i, any) || (own_ref_t[i] && frame_no + 1 - own_ref_t[i] < (unsigned int)SHARED_HOLD)) continue;
            need = estate_events(ene_len(ep, own_child(ep)));
            if (budget < need) continue;
            own_give(i, 0, 1, "a record the room's scripts act on: the host's");
            budget -= need;
            continue;
        }
        /* Shared Enemy Control: the player it is engaged with runs it */
        if (!shared_on() || !own_movable(i) || !proxy_ok || frozen || (sys->cb_flg & 0x5) || !tx_in_game) { shared_want[i] = 0; continue; }
        if (!shared_pref(i)) { shared_want[i] = 0; continue; }
        if (++shared_want[i] >= SHARED_GIVE && frame_no - own_t[i] >= SHARED_HOLD) {
            int need = estate_events(ene_len(ep, own_child(ep)));
            if (budget < need) continue;
            own_give(i, 0, 1, hit_t[i][1] && frame_no + 1 - hit_t[i][1] < SHARED_HIT ? "engaged: target or shooter" : "engaged: target");
            budget -= need;
        }
    }
}

/* our hit on enemy i (the weapon code's output on it) as EV_ENE_HIT, taken off the enemy: to the partner, which runs it, or
   (protocol 56) kept here when its hand-over to us is under way (take_at: we run it from the giver's state, which has not
   seen this hit) */
int hit_send(int i, BH_PWORK* ep, int keep)
{
    int j, n;
    Event e = { 0 };
    for (j = n = 0; ep->mlwP && j < ep->mlwP->obj_num && j < 64; j++)
        if (ep->dam[j] && n < 6) { e.u.eh.jnt[n] = j; e.u.eh.dam[n++] = ep->dam[j]; ep->dam[j] = 0; }
    if (!n && !(ep->flg & 0x4) && !(ep->flg2 & 0x4) && !(ep->comb_flg & 0x70)) return 0;
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
    hit_t[i][0] = frame_no + 1;
    if (keep) { e.stg = sys->stg_no; e.rom = sys->rom_no; defer_hit(&e); }
    else queue_event(&e);
    return 1;
}
/* at the hand-off: our hits on puppets go to the side that runs them (and are cleared here: the puppets do not run their own
   damage code) */
void capture_hits(void)
{
    int i;
    if (!sys || !plp || !coop_on || echo_delay) return;
    for (i = 0; i < (int)sys->ewk_n && i < PC_ENE_SLOTS; i++) {
        BH_PWORK* ep = &ene[i];
        if (!(ep->flg & 0x1) || !ep->mode0 || we_run(i)) continue;
        hit_send(i, ep, take_at[i] != 0);
    }
}
/* "Break Free Pushes Enemies" (shove_glue.c, protocol 59): our player broke free next to enemy i, which the partner's game
   runs (or will: as a hit, kept or passed on), so that game knocks it back - a COOP_HIT_SHOVE EV_ENE_HIT from the point
   (x, z). 0: we run it, the caller pushes it. */
int pc_shove_apply(BH_PWORK* ep, float cx, float cz);
int coop_runs(int i) { return we_run(i) && !giving[i]; }
int coop_plp_partner(void) { return plp == &ply2; }
int coop_shove(int i, float x, float z)
{
    Event e = { 0 };
    int n;
    if (i < 0 || i >= PC_ENE_SLOTS || coop_runs(i) || echo_delay) return 0;
    for (n = 0; n < 6; n++) e.u.eh.jnt[n] = -1;
    e.type = EV_ENE_HIT; e.idx = i;
    e.u.eh.flg = COOP_HIT_SHOVE;
    e.u.eh.djnt_no = -1;
    e.u.eh.dp[0] = x; e.u.eh.dp[1] = plp->py; e.u.eh.dp[2] = z;
    if (take_at[i]) { e.stg = sys->stg_no; e.rom = sys->rom_no; defer_hit(&e); }
    else queue_event(&e);
    return 1;
}

/* a hit (EV_ENE_HIT) on an enemy we run: its damage code takes it at its next step */
void hit_apply(const Event* e)
{
    BH_PWORK* ep = &ene[e->idx];
    int n;
    if (e->idx >= sys->ewk_n || !(ep->flg & 0x1) || !ep->mlwP) return;
    if (e->u.eh.flg & COOP_HIT_SHOVE) {   /* (protocol 59) a push, not a hit */
        int m0 = ep->mode0, m2 = ep->mode2, ok = pc_shove_apply(ep, e->u.eh.dp[0], e->u.eh.dp[2]);
        host_log("breakfree: enemy %d (id %d) mode %d/%d %s for the partner%s\n", e->idx, ep->id, m0, m2, ok ? "pushed" : "not pushed",
                 ok ? "" : m0 == 2 || m0 == 3 ? " (busy: pushed if it walks again soon)" : "");
        return;
    }
    for (n = 0; n < 6; n++)   /* joints of its model only (coop_check_event: 0..63, damage bounded) */
        if (e->u.eh.jnt[n] >= (int)ep->mlwP->obj_num) return;
    for (n = 0; n < 6; n++)
        if (e->u.eh.jnt[n] >= 0 && ep->dam[(int)e->u.eh.jnt[n]] < 100000) ep->dam[(int)e->u.eh.jnt[n]] += e->u.eh.dam[n];
    ep->flg |= e->u.eh.flg & 0x4; ep->flg2 |= e->u.eh.flg2; ep->comb_flg |= e->u.eh.comb_flg;
    if (e->u.eh.flg & COOP_HIT_AIM) hit_aim[e->idx] = 0x80 | ((e->u.eh.flg >> 8) & 0xE);
    ep->djnt_no = e->u.eh.djnt_no; ep->wpnr_no = e->u.eh.wpnr_no;
    tps_hit_precise[e->idx] = e->u.eh.flg & COOP_HIT_PRECISE ? e->u.eh.wpnr_no + 1 : 0;
    ep->dpx = e->u.eh.dp[0]; ep->dpy = e->u.eh.dp[1]; ep->dpz = e->u.eh.dp[2];
    ep->dvx = e->u.eh.dv[0]; ep->dvy = e->u.eh.dv[1]; ep->dvz = e->u.eh.dv[2];
    ep->dax = e->u.eh.dax; ep->day = e->u.eh.day;
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
unsigned int gone_mask[(PC_ENE_SLOTS + 31) / 32];       /* guest: slots we named dead; the host's state does not revive them */
static int gone_t;                      /* guest: frames left to wait for the host to hide them */
static struct { int key, t, n; unsigned char slot[PC_ENE_SLOTS], id[PC_ENE_SLOTS], ver[PC_ENE_SLOTS]; } gone_in;   /* host: the guest's, until done */
int gone_has(int k) { return k >= 0 && k < PC_ENE_SLOTS && (gone_mask[k >> 5] >> (k & 31) & 1); }

static int gone_dead(const BH_PWORK* ep)   /* a killed enemy (not a linked part, not hidden), any slot */
{
    return (ep->flg & 0x83) == 0x3 && !(ep->stflg & 0x1000000) && ep->mode0 && ep->id <= 90;
}

/* ---------------------------------------------------------------- the guest's room (protocol 53, every type since 54; protocol 56)
   The host's fresh load of a room the guest has been in alone (the host elsewhere, its enemies run by the guest's game)
   had the host's enemies take over: the guest's became puppets of the host's, which stood at the room's start positions,
   so everything the guest had moved jumped back (and the extras only the host drew popped in, enemyopt_glue.c). Since
   protocol 53 the guest's enemies go on as they are: the guest claims each one it ran alone (of own_types: every type the
   guest can meet outside a boss fight's script; EV_ENEOWN state 3: its slot, type and look; stflg_b 1: it lies dead there),
   the host, whose copy in that slot is the same enemy, shows it as a puppet of the guest's snapshots (state 4; else state
   2, refused: the host's runs). Protocol 56: the guest was first in the room, so it runs them on until it leaves (then it
   hands them to the host with their state, own_frame), and the host's copies wait hidden until the guest's state of each has
   come (its snapshots come all the time) or its claims are done (EV_ENEOWN 5: the rest is the host's). Before (53..55) the
   guest handed every one in to the host as soon as it could go, a hand-over each. Only a record the room's scripts act on is
   still handed in (the scripts run on the host). The guest's dead ones it did not claim the host hides (EV_GONE, below). */
static unsigned char hin[PC_ENE_SLOTS];          /* guest: 1 claimed, 2 the host shows it as ours */
static int hin_alone, hin_gen = -1;              /* guest: our game ran this room load's enemies with the host elsewhere */
/* not claimed: what the host's fresh copy is as well (a bat asleep, a baby spider still inside its mother), and the Tyrant of
   a fight whose room script talks to it (type 1, rm_flg 0x1F: en19 Br01). (protocol 57) A boss of own_types (one whose work
   goes at a leave) is claimed: the guest runs its fight on when the host comes back into the room; before, the host's copy took
   it over with its own work from its Init and its entry scene handed that back to the guest (the Gulp Worm stood a frame at its
   start, its fight's state reset) */
static int hin_wanted(const BH_PWORK* ep)
{
    return !(ep->id == 7 && ep->type == 1) && !(ep->id == 24 && ep->mode0 == 6) && !(ep->id == 19 && ep->type == 1);
}
/* the types of the hand-in only (own_types shared 0): the states in which their state can go (from the type's en*.c code,
   docs/COOP.md "The hand-in"); in any other the guest's game runs it on until it is in one, or it leaves the room */
static int hin_state_ok(int k, int any)
{
    BH_PWORK* ep = &ene[k];
    const unsigned char* x = ep->exp0;
    int m0 = ep->mode0, m1 = ep->mode1, m2 = ep->mode2, dead = (ep->flg & 0x2) && m0 == 4;
    if (own_boss(ep)) return hin_leave && hin_any_event;   /* (protocol 57) a boss: at a door of its runner's, in any state */
    if (m0 == 5 && any && ep->id != 19) return 1;   /* in the common event mode (bhEne_Event: a room's hidden moths, ants, Spotters
                                                       waiting for their event; what it runs on is its work), as the shared types */
    switch (ep->id) {
    case 3:   /* Black Widow: on the floor or the ceiling (flg 0x2000000: one that started there, en03 BR02), idle, walking, turning
                 or chasing (no dive, jump, attack, grab, change of surface); its legs shot off go with it (protocol 57, bw_pack;
                 before, it stayed with the game that shot them, and went without its state when that one left) */
        /* we leave: any state but its grab (its leader's grab block, exp1, is our game's) goes with it - its dive, jump, climb
           and change of surface go on from where they are (their surfaces go as records, atr_enc). Before, it went without
           its state and the partner's game ran it on from its puppet's pose with its own stale work: 47-unit jumps (1 in 3) */
        if (hin_leave && m0 != 2) return 1;
        return dead || (m0 == 1 && m1 == 1 && (m2 <= 4 || m2 == 17) && ep->type < 2 && x[0x69] <= 1 && !(ep->flg & 0x1880000) && !(ep->flg2 & 1));
    case 6:   /* moth: perched, flying or closing in, no moth holding the player (exp1: all its moths' block) */
        return dead || (m0 == 1 && m1 == 1 && (m2 == 0 || m2 == 2 || m2 == 3) && !ep->type && !(ep->flg & 0x800000) && !(ep->exp1 && (*(int*)ep->exp1 & 1)));
    case 7:   /* bat: hanging awake, chasing, hovering or circling, not latched on, not the swarm's leader while its pause runs
                 (exp1[6], [7]: only that bat counts it down) */
        return dead || (m0 == 1 && m1 == 1 && (m2 == 0 || (m2 >= 2 && m2 <= 4)) && !ep->type && !(ep->flg & 0x1000) &&
                        !(ep->exp1 && ep->exp1[6] == k && ep->exp1[7]));
    case 10: return m0 == 1 || dead;   /* ant A (its bite is over within its frame) */
    case 11:  /* Spotter: patrolling, not alarmed, no event motion */
        return m0 == 1 && m1 == 1 && (m2 == 1 || m2 == 2 || (m2 >= 4 && m2 <= 6)) && !x[0x56] && !(ep->flg & 0x80000) && ep->mnwP == ep->mnwPb;
    case 15:  /* Nosferatu: standing, turning, keeping away or chasing (Move_func by mode1), holding nobody (0x5A bit 0) */
        return m0 == 1 && m1 <= 3 && !(*(const unsigned short*)(x + 0x5A) & 1);
    case 19: {   /* Tyrant: waiting, walking or turning (br_mde0 NORMAL, br_mde1 0..2, act_now 0/4/8), no blow under way on its game's
                    player (ply_act.p_status), not in a hit frame (status 0x40) */
        int act = *(const int*)(x + 0x50);
        return m0 == 1 && !*(const int*)(x + 0xC4) && (unsigned int)*(const int*)(x + 0xC8) <= 2 && (act == 0 || act == 4 || act == 8) &&
               !(*(const int*)(x + 0x88) & 1) && !(*(const int*)(x + 0x40) & 0x40) && ep->hp >= 0 && !(ep->flg & 0x2);
    }
    case 21: return (m0 == 1 || dead) && !x[0x17];   /* albinoid infant: not holding its game's player */
    case 22: return m0 == 1 && m2 <= 2 && !(*(const int*)(x + 8) & 0x81000);   /* albinoid adult: no discharge, its light off */
    case 23:  /* Giant Black Widow: waiting or walking (MV02: to its nest, its target points plain), no climb, drop or attack, no
                 leg shot off, its game's player not caught */
        return m0 == 1 && ((m1 == 1 && m2 <= 1) || (m2 == 2 && ep->mode3 == 1)) &&!(ep->flg & 0x1C00000) && x[0x69] <= 1 && !x[0x102] && !x[0x103] && plp->mode0 != 4 &&
               plp->mode0 != 6 && ep->hp >= 0;
    case 24: return m0 == 1 && m2 >= 1 && m2 <= 3 && !(ep->flg & 0x100000) && (ep->flg & 0x20);   /* baby spider: out, on the ground */
    case 25: return m2 == 0 || m0 == 4 || ep->type == 1;   /* ceiling trap: up, its wall at rest */
    }
    return 1;   /* fish, ants B and C */
}
static int hin_ok(int k, int any)
{
    BH_PWORK* ep = &ene[k];
    int c = own_child(ep);
    if (k >= (int)sys->ewk_n || (ep->flg & 0x81) != 0x1 || !own_work(ep) || !ep->mode0 || (event_driven(ep) && !hin_any_event) ||
        (hold_family(ep) && !(hin_leave && ep->id == 3)) ||   /* (we leave: a Black Widow other than the one holding us goes as it is) */
        own_grab[k] || ghost.slot == k)
        return 0;
    if (!own_shared(ep)) return hin_state_ok(k, any);
    /* (a dead zombie goes as the corpse it is: its stale hold bit, its upper half let go already) */
    if (ep->id == 1 && !(ep->flg & 0x2) && (holding(ep) || ep->mode2 == 8 || (ep->flg2 & 1) || (*(int*)(ep->exp0 + 0x40) & 0x400000) ||   /* (own_movable: stairs) */
                        !(c < PC_ENE_SLOTS && (ene[c].flg & 0x81) == 0x81 && ene[c].exp0 && we_run(c))))
        return 0;
    return any || (ep->mode0 == 1 && ep->hp >= 0 && !(ep->flg & 0x2));
}
/* guest: the host came into the room we ran alone (it asked, ROOM_CLAIM, or its pose came): our enemies go on as they are,
   claimed one by one; what we cannot claim (a boss of a fight, one an event drives, one hidden) is the host's. Then "done",
   and the dead we did not claim named (EV_GONE) */
static void claim_room(void)
{
    int k, c, n = 0, d = 0;
    Event g = { 0 };
    if (claimed || is_host) return;
    claimed = 1;
    dflt = 1;
    for (k = 0; k < (int)sys->ewk_n && k < PC_ENE_SLOTS; k++) {
        BH_PWORK* ep = &ene[k];
        Event e = { 0 };
        int dead = (ep->flg & 0x2) != 0;
        if ((ep->flg & 0x81) != 0x1 || !we_run(k) || own_grab[k]) continue;
        c = own_child(ep);
        if ((dead && !gone_dead(ep)) || !own_work(ep) || !ep->mode0 || event_driven(ep) || !hin_wanted(ep) ||
            (ep->id == 1 && !(c < PC_ENE_SLOTS && (ene[c].flg & 0x81) == 0x81 && ene[c].exp0 && we_run(c)))) {
            own_set(k, 0); own_set(c, 0); giving[k] = 0;   /* the host's: our copy shows its state from now */
            if (trace) host_log("coop: own: enemy %d (id %d) not claimed: the host's\n", k, ep->id);
            continue;
        }
        hin[k] = 1;
        e.type = EV_ENEOWN; e.idx = k; e.u.gr.child = c; e.u.gr.state = 3;
        e.u.gr.mdl = e.u.gr.cmdl = -1; e.u.gr.mnw = e.u.gr.cmnw = MNW_KEEP;
        e.u.gr.flg_b = ep->id | ep->mdlver << 8;
        e.u.gr.stflg_b = dead;
        queue_event(&e);
        n++; d += dead;
    }
    own_msg(0, ROOM_DONE, 5, frame_no);
    owned_room = coop_room_key();
    host_log("coop: own: the host comes into the room we ran alone: %d of our enemies go on as they are, %d of them dead (%d slots)\n", n, d, (int)sys->ewk_n);
    /* the dead we did not claim: the host hides its copies (EV_GONE) */
    memset(gone_mask, 0, sizeof(gone_mask));
    g.type = EV_GONE;
    g.idx = sys->rcase;
    for (k = n = 0; k < (int)sys->ewk_n && k < PC_ENE_SLOTS; k++) {
        if (!(ene[k].flg & 0x1) || !gone_dead(&ene[k]) || we_run(k)) continue;
        gone_mask[k >> 5] |= 1u << (k & 31);
        g.u.gn.slot[g.u.gn.n] = k;
        g.u.gn.ver[g.u.gn.n] = ene[k].mdlver;
        g.u.gn.id[g.u.gn.n++] = ene[k].id;
        n++;
        if (g.u.gn.n == COOP_GONE_MAX) { queue_event(&g); g.u.gn.n = 0; }
    }
    if (g.u.gn.n) queue_event(&g);
    if (n) host_log("coop: the host's enemies take over: %d of ours died meanwhile, the host hides its copies\n", n);
    gone_t = n ? 150 : 0;
}
/* host: the guest's claims (state 3), answered once our copy is there and set up (an extra enemy comes at the room's first
   frame, which can be after the guest's claim) or after 150 frames. A dead one of the guest's (stflg_b 1) is taken also when
   our copy is hidden or dead: its corpse shows here as it lies there (before, what our script or EV_GONE hid) */
static Event hin_q[PC_ENE_SLOTS];             /* (hin_qt: 1 + frames waited, 0 none) */
static void hin_answer(int i, int last)
{
    const Event* e = &hin_q[i];
    int c = child_here(i, e->u.gr.child), ok, here, other, dead = e->u.gr.stflg_b == 1;
    BH_PWORK* ep = &ene[i];
    Event r = { 0 };
    here = e->stg == sys->stg_no && e->rom == sys->rom_no;
    if (here && (!room_ready() || i >= (int)sys->ewk_n || !(ep->flg & 0x1) || !ep->mode0) && !last) return;   /* (not set up yet) */
    hin_qt[i] = 0;
    /* (the players' room files differ: the same type in another look is taken too, each game showing its own look; its state
       runs on its own motions and model, which ene_unpack checks, instead of jumping back to our start position) */
    other = (ep->id | ep->mdlver << 8) != e->u.gr.flg_b && ep->id == (e->u.gr.flg_b & 0xFF) && enemy_room_differs();
    /* (protocol 56) one the guest runs as we see it (prov: we came into its room and ours wait for its claims; or one we handed
       it); one we run is ours: refused (both came at once, or both loaded the room again at a Retry), its copy shows ours */
    ok = here && room_ready() && (prov[i] || owned[i]) && i < (int)sys->ewk_n && (ep->flg & 0x81) == 0x1 && ep->mode0 && (dead || !(ep->flg & 0x2)) &&
         ((ep->id | ep->mdlver << 8) == e->u.gr.flg_b || other) && !own_grab[i] && own_child(ep) == c && !event_driven(ep) &&
         (c >= PC_ENE_SLOTS || (c < (int)sys->ewk_n && (ene[c].flg & 0x1)));
    r.type = EV_ENEOWN; r.idx = i; r.u.gr.child = c; r.u.gr.state = ok ? 4 : 2;
    r.u.gr.mdl = r.u.gr.cmdl = -1; r.u.gr.mnw = r.u.gr.cmnw = MNW_KEEP;
    queue_event(&r);
    if (!ok) {
        if (here && i < (int)sys->ewk_n && (ep->flg & 0x1)) { own_set(i, 1); own_ours(i, -1); }   /* (ours, as our game has it: the guest's
                                                    copy shows it from now, 2) */
        host_log("coop: hand-in: the guest's enemy %d (id %d model %d) is not ours here (id %d model %d): ours runs\n", i, e->u.gr.flg_b & 0xFF, e->u.gr.flg_b >> 8, ep->id, ep->mdlver);
        return;
    }
    own_set(i, 0);
    if (c < PC_ENE_SLOTS) { own_set(c, 0); prov[c] = 0; }
    prov[i] = 0;
    own_wait[i] = 0; own_t[i] = frame_no; owned_child[i] = 255;
    owned_room = coop_room_key();
    host_log("coop: hand-in: enemy %d as the guest's game has it%s%s, until it hands it in\n", i, dead ? " (dead)" : "",
             other ? " (in another look here: the room files differ)" : "");
}
void hin_host_frame(void)
{
    int i;
    for (i = 0; i < PC_ENE_SLOTS; i++)
        if (hin_qt[i]) hin_answer(i, ++hin_qt[i] > 150);
}
/* host: a claim (state 3); guest: the host's answer (state 4) */
void hin_apply(const Event* e)
{
    int i = e->idx;
    if (i >= PC_ENE_SLOTS) return;
    if (!is_host) {
        if (e->u.gr.state == 4 && hin[i] == 1 && we_run(i)) hin[i] = 2;   /* (it runs here on: protocol 56) */
        return;
    }
    if (e->u.gr.state != 3) return;
    hin_q[i] = *e; hin_qt[i] = 1;
    hin_answer(i, 0);
}
/* guest, every frame before the snapshots: the host comes into the room we ran alone (its ROOM_CLAIM is the usual way; its pose
   in game here covers both coming in at once) */
void gone_frame(void)
{
    static int was;
    int now = same_room() && rx.busy != BUSY_DOOR && room_ready() && run_room == (coop_room_key() | my_case() << 16);   /* (not in our door:
                                                    our enemies are the old room's then) */
    if (hin_gen != enemy_room_gen) { hin_gen = enemy_room_gen; hin_alone = 0; memset(hin, 0, sizeof(hin)); }
    /* our enemies run alone with the host known in game in another room (not while a rejoin's session starts: no pose yet, and its
       game state reloads our room) */
    if (!now && tx_in_game && room_ready() && !(sys->cb_flg & 0x5) && have_peer && have_rx && (rx.flags & PF_IN_GAME) && rx.busy != BUSY_DOOR && !partner_here())
        hin_alone = 1, claimed = 0;   /* (its next coming in is claimed again) */
    if (now && !was && dflt == 1) claim_room();   /* (once per coming in: both came at once, both reloaded it at a Retry) */
    if (now) hin_alone = 0;
    if (!now || (gone_t && !--gone_t)) memset(gone_mask, 0, sizeof(gone_mask));
    was = now;
}

int apply_gone(const Event* e)
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

/* host: the extras the guest killed while it was in the room alone (gx_follow makes them too, protocol 54): dead and hidden
   here once set up, as EV_GONE hides, unless the guest claimed the corpse first (the hand-in shows it as it lies there).
   Before, they were not made: the guest's corpse lay there, nothing on the host */
static unsigned char xcorpse[PC_ENE_SLOTS];
static int xcorpse_gen = -1;
void coop_extra_corpse(int slot)
{
    if (xcorpse_gen != enemy_room_gen) { memset(xcorpse, 0, sizeof(xcorpse)); xcorpse_gen = enemy_room_gen; }
    if (slot >= 0 && slot < PC_ENE_SLOTS) xcorpse[slot] = 1;
}
static void xcorpse_frame(void)
{
    int k;
    if (xcorpse_gen != enemy_room_gen) return;
    for (k = 0; k < PC_ENE_SLOTS; k++) {
        BH_PWORK* ep = &ene[k];
        if (!xcorpse[k]) continue;
        if (!(ep->flg & 0x1)) { xcorpse[k] = 0; continue; }
        if (!ep->mode0) continue;   /* (its Init first: it sets the flags up) */
        xcorpse[k] = 0;
        if (owned[k] || (ep->flg & 0x2)) continue;   /* (the guest's corpse already) */
        ep->stflg |= 0x1000000;
        ep->flg |= 0x2;
        host_log("coop: the guest's dead extra enemy in slot %d: dead and hidden here until the guest hands its corpse in\n", k);
    }
}

/* host, every frame: hide what the guest named, once our room runs */
void gone_host_frame(void)
{
    int k, j, hid = 0;
    xcorpse_frame();
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
