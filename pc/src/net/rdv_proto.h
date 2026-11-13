/* Co-op server protocol (docs/SERVER.md "Wire protocol"): the messages between a game and the
   rendezvous/relay server (pc/server), the hole-punching messages between the games, and the
   end-to-end encryption of game datagrams in server sessions. Pure functions, no sockets: the
   server, the game (rendezvous.c) and pc/tests/test_rdv.c share them. Little-endian, packed by
   hand (any host). */
#ifndef RECVX_RDV_PROTO_H
#define RECVX_RDV_PROTO_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define RDV_MAGIC 0x53585643u           /* "CVXS": server message / punch / relay frame */
#define RDV_E2E_MAGIC 0x45585643u       /* "CVXE": an encrypted game datagram */
#define RDV_VERSION 3                   /* 2: authenticated RELAY frames, keys bound to the introduction (docs/SERVER.md "Security");
                                           3: JOIN carries a rejoin tag (a crashed player takes its own slot back), KICK (docs/SERVER.md "Rejoin, KICK and member timeout") */
#define RDV_DEFAULT_PORT 7141
#define RDV_HDR 8                       /* magic, version, type, flags */
#define RDV_RELAY_HDR 24                /* + room id, counter, MAC (8): the sender's token authenticates the frame to the server */
#define RDV_E2E_OVER 28                 /* magic 4 + counter 8 + MAC 16 */
#define RDV_HELLO_MIN 64                /* HELLO is padded: no reply is larger than its request */
#define RDV_DGRAM_MAX 1200              /* the most any datagram on any path may have */
#define RDV_CODE_LEN 8                  /* room code: 8 of the 32 characters (40 bits), shown XXXX-XXXX */
#define RDV_CODE_CHARS "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"   /* also the co-op join codes */

enum { RDV_HELLO = 1, RDV_WELCOME, RDV_CREATE, RDV_ROOM, RDV_JOIN, RDV_INTRO, RDV_PUNCH, RDV_RELAY,
       RDV_KEEPALIVE, RDV_CLOSE, RDV_ERROR, RDV_STATUS, RDV_KICK, RDV_PING, RDV_TYPES };
/* RDV_PING (0.7.9, no new RDV_VERSION): the server's round-trip probe to a room member and the
   member's answer (docs/SERVER.md "Session history"). Older games drop the unknown type; older
   servers never send it, so a newer game never answers one. */
enum { RDV_E_VERSION = 1, RDV_E_NO_ROOM, RDV_E_NEED_PW, RDV_E_BAD_PW, RDV_E_FULL, RDV_E_RATE, RDV_E_COOKIE,
       RDV_E_SERVER_FULL, RDV_E_TOO_MANY };
enum { RDV_F_PW = 1, RDV_F_E2E = 2 };   /* CREATE / INTRO flags; JOIN: RDV_F_PW = a proof is given */
enum { RDV_BAD_SIZE = -1, RDV_BAD_HDR = -2, RDV_BAD_VERSION = -3, RDV_BAD_TYPE = -4, RDV_BAD_FIELD = -5 };

/* an address on the wire: family 4 or 6 (0 none), port, 16 bytes (IPv4: the first 4). IPv6 is
   not used yet (docs/SERVER.md "IPv4 / IPv6"); the format already carries it. */
typedef struct RdvAddr { uint8_t fam; uint16_t port; uint8_t ip[16]; uint8_t via; } RdvAddr;   /* port: host order */
/* via: the server's local socket the address was seen on (listen = several addresses); not on the wire */
#define RDV_ADDR_LEN 19

typedef struct RdvMsg {
    int type, flags, version;
    uint32_t nonce, room_id, seq;       /* HELLO/WELCOME nonce; INTRO/KEEPALIVE member sequence */
    uint64_t req_id;                    /* CREATE/ROOM: the host's request id (a retry gets the same room) */
    uint8_t cookie[16], token[16], salt[16], verifier[32], proof[32], pk[32], mac[16];
    uint8_t rejoin[16];                 /* JOIN: the player's rejoin tag for this room (rdv_rejoin_tag; zero: none) */
    char code[RDV_CODE_LEN + 1];
    RdvAddr pub, lan;                   /* WELCOME: pub = the sender as the server sees it; CREATE/JOIN: lan */
    int err, role, attempt, has_member;
    uint32_t hold_us;                   /* PING answer: how long the probe waited in the game before the answer (us) */
    const uint8_t* payload;             /* RELAY: the frame after the 24-byte header (seq = its counter, mac[0..8) its MAC) */
    int payload_len;
} RdvMsg;

/* parse a CVXS datagram: the type, or RDV_BAD_*. Sizes are exact per type (HELLO >= 64). */
int rdv_parse(const uint8_t* b, int n, RdvMsg* m);
/* build one: the length (<= RDV_DGRAM_MAX), 0 when it does not fit `cap` */
int rdv_build(uint8_t* b, int cap, const RdvMsg* m);

int rdv_code_norm(const char* in, char out[RDV_CODE_LEN + 1]);   /* 1 if `in` is a room code (case, '-', ' ' ignored) */
void rdv_code_from(const uint8_t rnd[RDV_CODE_LEN], char out[RDV_CODE_LEN + 1]);
void rdv_wipe(void* p, int n);                                     /* zero a secret (not optimized away) */
int rdv_same16(const uint8_t a[16], const uint8_t b[16]);          /* 1 if equal; constant time */
int rdv_random(void* out, int n);                                  /* OS randomness; 1 on success (0: do not use `out`) */
extern int rdv_test_fail;   /* test hook: 1 rdv_random fails, 2 the password KDF's allocation fails */

/* crypto (Monocypher): BLAKE2b, Argon2id, X25519, XChaCha20-Poly1305 */
void rdv_cookie(const uint8_t secret[32], const RdvAddr* a, uint32_t bucket, uint8_t out[16]);
int rdv_pw_key(const char* password, const uint8_t salt[16], uint8_t out[32]);    /* Argon2id: slow on purpose (~20 ms); 0 (out wiped) on failure */
void rdv_verifier(const uint8_t pw_key[32], uint8_t out[32]);                     /* what the server keeps */
void rdv_proof(const uint8_t verifier[32], const uint8_t cookie[16], const char* code, const uint8_t pk[32], uint8_t out[32]);
/* a player's rejoin tag for one room / session code: BLAKE2b keyed with the player's secret (recvx.ini
   coop_player_key) over the code. Only that player can make it; a server or host that saw it can
   only replay it for that same code. */
void rdv_rejoin_tag(const uint8_t key[32], const char* code, uint8_t out[16]);
void rdv_punch_mac(const uint8_t key[32], const uint8_t* msg, int n, uint8_t out[16]);
/* RELAY frames: the payload is already at b + RDV_RELAY_HDR; writes header, counter and the MAC
   (keyed with the sender's member token, over everything else); the frame's length */
int rdv_relay_seal(uint8_t* b, int payload_len, uint32_t room_id, uint32_t ctr, const uint8_t token[16]);
int rdv_relay_ok(const uint8_t* b, int n, const uint8_t token[16]);   /* 1: the MAC is right */

typedef struct RdvSession {
    uint8_t tx[32], rx[32], punch[32]; /* directional keys, the punch MAC key */
    char code[9];                       /* a code both sides derive (co-op protocols 11..27 took it for their join-code proof) */
    char verify[7];                     /* shown to the players to compare by voice: equal only with the same keys (no man in the middle) */
    uint64_t tx_ctr, rx_top, rx_win;    /* nonce counter; replay window (highest seen, 64 below it) */
} RdvSession;
/* what both games bind their keys to: the introduction as each saw it. A server or an attacker that
   changes any of it (room, epoch = the member sequence, flags, room code, either public key) gets
   two sides with different keys: nothing decrypts, nothing is sent in clear. */
typedef struct RdvIntro { uint32_t room_id, epoch; int flags; char code[RDV_CODE_LEN + 1]; } RdvIntro;
/* both sides: X25519 of our secret and the peer's public key, both public keys, the password key
   (NULL without a password), the introduction: the same keys on both sides, unknown to the
   server. Fresh counters: call it only with a new epoch or new public keys (never the same keys
   twice). 0 (s wiped): a degenerate peer key (the shared secret is zero). */
int rdv_session(RdvSession* s, int host, const uint8_t my_sk[32], const uint8_t host_pk[32], const uint8_t join_pk[32],
                const uint8_t* pw_key, const RdvIntro* in);
/* a 64-entry replay window (the E2E and RELAY counters): `ctr` is 0, a repeat or too old */
int rdv_replayed(uint64_t top, uint64_t win, uint64_t ctr);
void rdv_replay_mark(uint64_t* top, uint64_t* win, uint64_t ctr);
int rdv_seal(RdvSession* s, uint8_t* out, int cap, const void* in, int n);        /* the datagram length, 0 if it does not fit */
int rdv_open(RdvSession* s, uint8_t* out, int cap, const uint8_t* in, int n);     /* plaintext length, -1 forged/replayed/malformed */

#ifdef __cplusplus
}
#endif
#endif
