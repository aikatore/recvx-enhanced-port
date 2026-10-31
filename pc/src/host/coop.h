/* Online co-op (docs/COOP.md), shared between the overlay/command line (main/render thread)
   and the game thread (pc/glue/coop_glue.c: networking at the frame hand-off, player-2 hooks
   in the decomp). The game thread owns everything except `request`. */
#ifndef RECVX_COOP_H
#define RECVX_COOP_H
#ifdef __cplusplus
extern "C" {
#endif

enum { COOP_REQ_HOST = 1, COOP_REQ_JOIN = 2, COOP_REQ_STOP = 3, COOP_REQ_KICK = 4 };   /* KICK: host, "Remove stuck player" */
enum { COOP_OFF, COOP_HOSTING, COOP_JOINING, COOP_CONNECTED, COOP_LOST };
enum { COOP_TRADE_OFFER = 1, COOP_TRADE_ACCEPT, COOP_TRADE_DECLINE, COOP_TRADE_CANCEL };

#define COOP_DEFAULT_PORT 7140
/* the co-op server (docs/SERVER.md) filled in when recvx.ini has none: self-hosted servers
   only for now, so empty (Automatic then behaves exactly as Direct). host or host:port, UDP 7141. */
#define COOP_DEFAULT_SERVER ""
#define COOP_E2E 1                      /* hosting through the server: encrypt end to end (the joiner follows the host) */
enum { COOP_CONN_AUTO, COOP_CONN_DIRECT, COOP_CONN_SERVER };   /* ini coop_conn */
#define COOP_NAME_MAX 16                /* a player name, with its terminator */

typedef struct CoopState {
    /* settings (overlay / command line, saved in recvx.ini) */
    int port;                       /* host: listen port */
    int chr;                        /* model the others see: 0 = the character played, 1..4 = Claire, Chris, Steve, Wesker, 5 = Claire costume 2 */
    char join[128];                 /* client: "host:port" */
    int loot;                       /* host: 0 shared loot, 1 individual loot (key items stay shared) */
    int follow;                     /* client: follow the host through doors (regroups pull the client anyway) */
    int upnp;                       /* host: ask the router to forward the port (UPnP / NAT-PMP, src/net/portmap.c) */
    volatile int request;           /* COOP_REQ_*, taken by the game thread */
    /* published by the game thread */
    int state, ping_ms;
    int peer_stg, peer_rom, peer_chr, peer_in_game;
    int remote_visible;             /* partner drawn in this frame */
    int at_title;                   /* the title screen is up (the overlay shows the lobby hint) */
    int loot_now;                   /* the session's loot mode (the host's setting) */
    char status[400];
    /* trade (M8): overlay requests (taken by the game thread), game thread publishes the rest */
    volatile int trade_req;         /* COOP_TRADE_* */
    int trade_slot, trade_count;    /* offer: inventory slot (2..), count (0 = all) */
    int trade_near, trade_slots;    /* the partner stands next to us; inventory slots */
    unsigned int trade_inv[12];     /* our inventory: [0] equipped slot, [2..] (id << 16) | count */
    unsigned int trade_out, trade_in;   /* our open offer / the partner's to us (0 = none) */
    char trade_msg[256];
    /* lobby screen and name tag (pc/glue/lobby_glue.c) */
    char name[COOP_NAME_MAX];       /* our name, as the partner sees it (ini coop_name) */
    int nametag;                    /* show the partner's name above its head (ini coop_nametag) */
    int items;                      /* joining: COOP_ITEMS_* or 0..14 the items of that save slot (ini coop_items) */
    int start;                      /* hosting from the lobby: 0..2 new game normal/easy/very easy, 3 + n save slot n (ini coop_start) */
    int lobby;                      /* game thread: our lobby screen is open */
    char note[40];                  /* game thread: what we chose there, shown to the partner */
    /* published by the game thread */
    int host;                       /* we host (else we join), while state != COOP_OFF */
    char peer_name[COOP_NAME_MAX];
    int peer_lobby, peer_lchr;      /* the partner's lobby screen is open; the model it chose there (as `chr`) */
    char peer_note[40];
    /* above the partner's head while it is drawn in our room (the point goes with the frame:
       stream_point): its name and, when it is busy (cutscene / menu / item box), what with */
    int busy_kind;
    char code[12];                  /* host: the join code of this run (shown in the lobby and F3, in "Copy join address") */
    /* the co-op server (docs/SERVER.md) */
    int conn;                       /* COOP_CONN_*: Automatic (server when one is set, else direct), Direct, Via server (relay) (ini coop_conn) */
    char server[128];               /* host or host:port (ini coop_server) */
    char password[64];              /* room password, both sides; never saved */
    char room[12];                  /* game thread: the room code the server gave us (host), "" without */
    int trade_freeze;               /* host: the room's enemies stand still while both players are in the trade screen (ini coop_trade_freeze) */
    /* who we are (docs/COOP.md "Saved partner inventories"): made once, kept in recvx.ini; the name is only shown */
    unsigned char pid[16];          /* our player id (ini coop_player_id) */
    unsigned char pkey[32];         /* our secret (ini coop_player_key): rejoin tags, never sent */
    int remember;                   /* host: keep the partners' items per player id with our saves (ini coop_remember) */
    int peer_rec;                   /* game thread, joining: whether the host keeps saved items for us (REC_NONE / REC_HAVE / REC_OFF of coop_proto.h), -1 not known yet */
    /* game rules of protocol 19 (host; the guest follows the host's, Pose.opts). No ink ribbons is g_config.no_ink_ribbon. */
    int important;                  /* shared loot: weapons and ink ribbons stay each player's own to take (ini coop_important) */
    int skip_sync;                  /* a cutscene both play is skipped when both pressed skip (ini coop_skip_sync) */
    int gameover_both;              /* game over: 0 each player alone, 1 both (the host's CONTINUE / NO decides; ini coop_gameover) */
    int skip_detector;              /* the prison's metal detector (0/9) never goes off, its empty security boxes rest (ini coop_skip_detector) */
    int quick_start;                /* a new game starts with the manual read and the knife in the bag (ini coop_quick_start) */
    int hide_ip;                    /* Hide IP addresses (ini hide_ip): on screen masked, coop_shown */
} CoopState;
/* the session's "Metal detector: Skipped" rule (OPT_SKIP_DETECTOR): hosting our skip_detector, joining the host's (coop_glue.c) */
int coop_skip_detector(void);
/* the session's "Quick start" rule (OPT_QUICK_START): hosting our quick_start, joining the host's (coop_glue.c) */
int coop_quick_start(void);
/* Hide IP addresses: every address shown on screen goes through here. With hide_ip on, the
   text with its IPv4 addresses and the join and co-op server host names masked (net_mask_addrs),
   else text itself. A ring of 4 buffers per thread (lobby_io.c). Input fields mask as passwords. */
const char* coop_shown(const char* text);
/* joining: where our inventory comes from (g_coop.items; 0..14 is a save slot) */
enum { COOP_ITEMS_CURRENT = -3,     /* our game in progress (F3 in game); at the title the new-game items */
       COOP_ITEMS_HOST = -2,        /* saved with the host (its record of our player id); none: the new-game items */
       COOP_ITEMS_NEW = -1 };       /* the new-game items */

/* What the game thread publishes for other threads (the overlay), copied whole once per game frame:
   a reader never sees a half-written line. The game thread itself reads g_coop. */
typedef struct CoopStatus {
    int state, ping_ms, host, at_title, lobby, loot_now;
    int peer_stg, peer_rom, peer_chr, peer_in_game, remote_visible;
    char status[400];
    int trade_near, trade_slots;
    unsigned int trade_inv[12], trade_out, trade_in;
    char trade_msg[256];
    char peer_name[COOP_NAME_MAX];
    int peer_lobby, peer_lchr;
    char peer_note[40];
    int busy_kind;
    char code[12];
    char room[12];
    char trade_hint[64];            /* above the partner's head: it asks us to trade */
    int trade_open;                 /* our trade screen is open (no name tag over it) */
    int peer_rec;                   /* joining: REC_* from the host, -1 not known */
} CoopStatus;
void coop_status_get(CoopStatus* out);   /* any thread */

/* The in-game trade screen (docs/COOP.md "Trade screen"): pc/glue/coop_glue.c runs it (request,
   protocol, swap), pc/glue/trade_glue.c draws it and takes its input. Game thread. Index 0 is our
   side, 1 the partner's. */
enum { TRADE_IDLE, TRADE_ASKING, TRADE_ASKED, TRADE_OPEN };
typedef struct TradeView {
    int phase;                      /* TRADE_*: asking = we asked and wait, asked = the partner asks us */
    int cells[2];                   /* grid cells (8, 10 with the side pack) */
    unsigned int grid[2][10];       /* grid words (inventory words 2..11: id << 16 | count, flags) */
    int eq[2];                      /* the equipped grid word (0..9), -1 none */
    int sel[2];                     /* the offered cell, -1 none (the partner's: none or not known yet); only an
                                       activation (confirm, click) sets ours and only cancel clears it */
    int cur_side, cur;              /* our cursor (keys, pad, mouse hover): side and cell; moving it changes no selection */
    int want[2];                    /* requests: [0] the partner's cell we ask for, [1] our cell it asks for; -1 none */
    unsigned int want_w[2];         /* ... and the item word there when asked (a request ends when it changes) */
    int ready[2];
    int waiting;                    /* a swap is on its way */
    char why[256];                  /* why the selected pair does not swap, "" when it does */
    char msg[256];                  /* the last result or notice ... */
    unsigned int msg_until;         /* ... shown until then (net_ms) */
    char hint[256];                 /* request phase: the line at the bottom of the screen */
    char name[COOP_NAME_MAX];       /* the partner's */
    char key[48];                   /* the trade request binding(s), for hints */
} TradeView;
extern TradeView g_trade;
extern char coop_skip_hint[96];         /* synchronized cutscene skip: the line at the top of the screen, "" none (trade_glue.c draws it) */
unsigned int coop_skip_pad(unsigned int pad);   /* pad.c bhSetPad (via trade_pad): the skip button in a cutscene both play asks first */
void coop_trade_select(int cell);   /* offer our cell (an empty one: only take) */
void coop_trade_cursor(int side, int cell);
void coop_trade_activate(void);     /* confirm on the cursor: our cell offered (again: ready), the partner's requested (again: withdrawn) */
int  coop_trade_back(int may_leave);   /* cancel: the next of request (cursor on its side), selection, request, the partner's
                                          request (declined), else leave if may_leave; returns what it did (TB_*) */
enum { TB_NONE, TB_UNWANT, TB_UNSELECT, TB_DECLINE, TB_LEAVE };
int  coop_trade_back_what(void);    /* what coop_trade_back(1) would do */
void coop_trade_want(int cell);     /* request the partner's cell (-1 withdraw); the partner sees it, nothing moves */
void coop_trade_offer_wanted(void); /* offer the cell the partner requests */
/* the partner requests our cell (-1: withdrawn). Validated against our real inventory: the cell must
   hold exactly that word. Transport-free (the network's TS_WANT is one caller); returns 1 if taken */
int  coop_trade_wanted(int cell, unsigned int word);
void coop_trade_ready(void);        /* toggles ready (refused with a reason while the pair cannot swap) */
void coop_trade_leave(void);        /* closes the screen for both */
int  coop_trade_layout(const unsigned int* grid, int cells, signed char* word_at);   /* grid word at each cell (-1 empty), as the game lays them out */
const char* coop_item_name(unsigned int word);   /* "Handgun" (no count) */
int  coop_trade_frozen(void);       /* the enemies stand still (both in the trade screen, host setting) */

extern CoopState g_coop;
/* the joining player types a room code (through the co-op server) rather than an address: Via
   server, or Automatic with a server set. Labels the lobby's CODE/ADDRESS row and F3's field. */
static inline int coop_join_by_code(const CoopState* c) { return c->conn == COOP_CONN_SERVER || (c->conn == COOP_CONN_AUTO && c->server[0]); }
extern int coop_on;                 /* game thread: hooks active (changes only at the frame hand-off) */
void coop_frame(void);              /* game thread, once per frame */
/* coop_glue.c -> coop_testaids.c (game thread, at the start of coop_frame): what the test aids read of
   the session; REJOIN and KICK set the frame's request */
struct BH_PWORK;
typedef struct CoopTest {
    int req;                            /* in/out: COOP_REQ_* of this frame */
    int host, on, echo, in_game, proxy_ok, frozen;
    unsigned int clock, frame;          /* the host's frame (test_clock), ours (frame_no) */
    const unsigned char* owned;         /* [PC_ENE_SLOTS]: the partner's game runs that enemy (a grab) */
    const struct BH_PWORK* partner;     /* the partner's stand-in (ply2) */
    void (*move)(float x, float z);     /* our player to x/z, its collision points along */
    unsigned int pad;                   /* out: game pad bits pressed at the next pad read (RECVX_COOP_PAD) */
} CoopTest;
void coop_test_aids(CoopTest* t);
int coop_room_key(void);            /* game thread: the room we are in, sys->stg_no * 256 + sys->rom_no (any case) */
/* game thread, before or while joining: where the joining player's own inventory, item box, health
   and weapon come from - a save region (bytes from SYS_WORK.version, as a save holds them) or NULL
   for the current game state (the default) */
void coop_own_source(const void* save_region);
/* game thread: apply g_coop.items as the joining inventory (the lobby and F3's Join); 1 = a save's items were read */
int coop_items_apply(void);
/* game thread, joining: the host's "Game over: Both" rule as its last pose had it (the lobby shows it) */
int coop_host_gameover_both(void);
/* the memory card (pc/src/ps2/mc.c, game thread): save file `slot` (SAVEDATA-nn) was written. The
   host's saved partner inventories go with its saves (a file per slot). */
void coop_save_written(int slot);

/* decomp hooks (game thread, only called while coop_on) */
void coop_control_remote(void);     /* game.c bhMainSequence, after bhControlPlayer */
void coop_draw_remote(void);        /* game.c bhAllDrawModel, after the player's bhPutModel */
void coop_textures_released(void);  /* ps2_NaTextureFunction.c njReleaseTextureAll */
void coop_retry_point(void);       /* room.c bhPushGameData: our continue point moved */

#ifdef __cplusplus
}
#endif
#endif
