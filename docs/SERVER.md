# recvx-server: co-op rendezvous and relay

`recvx-server` is an optional helper for online co-op ([COOP.md](COOP.md)). It lets two games
find each other by a short **room code** instead of an IP address, introduces them so they can
open a direct connection through their routers (**hole punching**), and **relays** their traffic
when no direct path works.

- UDP only, IPv4 only, one port (default **7141**).
- No game, no SDL, no disc. It sees room codes and addresses; game data it relays is end-to-end
  encrypted.
- All state is in memory; nothing is written to disk. A restart forgets every room (the games
  open a new one automatically).
- Direct P2P co-op (`ip:port#CODE`) does not use the server at all.

## For players

The game has no built-in default server (`COOP_DEFAULT_SERVER` in `pc/src/host/coop.h` is empty),
so someone in your group has to run one (below) and both players enter its address:

| where | setting |
|---|---|
| Lobby (co-op screen) | mode **VIA SERVER** / **DIRECT**, row **SERVER**, **PASSWORD**, **RELAY** (*Only if needed* / *Always*), **COPY ROOM CODE** |
| F3 window | **Connection**, **Co-op Server**, **Room code** / **Join address** |
| ENHANCED SETTINGS > CO-OP (F1) | **Connection** (Automatic, Direct, Server Relay) |
| recvx.ini | `coop_conn` (0 Automatic, 1 Direct, 2 Server Relay), `coop_server` (`host` or `host:port`; empty = no server) |
| command line | `--coop-server ADDR`, `--coop-conn auto\|direct\|server`, `--coop-pw PASSWORD`, `--coop-join ROOMCODE` |

The port 7141 is implied when the address has none. The password is never saved. With
**Automatic** the games punch a direct path and use the relay only if that fails; **Server Relay**
always relays. How to host and join step by step: [COOP.md](COOP.md).

## Building

Prebuilt: `recvx-server-VER-linux-x86_64.tar.gz` on the
[Releases](https://github.com/aikatore/recvx-enhanced-port/releases) page (static `recvx-server`,
`recvx-client`, the example config and the systemd unit).

`recvx-server` is part of the normal build (`pc/CMakeLists.txt` adds `pc/server`). For a server
machine it builds on its own, without the game's dependencies (Linux, any C compiler, CMake 3.20,
network access for the Monocypher fetch):

```sh
cmake -S pc/server -B build-server -DCMAKE_BUILD_TYPE=Release
cmake --build build-server --target recvx-server recvx-client
```

| CMake option | default | meaning |
|---|---|---|
| `RECVX_SERVER_STATIC` | ON | link `recvx-server` statically (a single file to copy, ~1 MB) |

Targets: `recvx-server` and `recvx-client` (a test client built from the game's network code),
both Linux-only. See also [BUILDING.md](BUILDING.md).

## Running a server

```sh
./build-server/recvx-server                                  # defaults; Ctrl+C stops it
./build-server/recvx-server --config my.conf -v              # a config file, verbose log
./build-server/recvx-server --port 7200 --relay-kb-per-sec 1024   # flags override the file
./build-server/recvx-client 127.0.0.1 status                 # metrics, on the server machine
```

Open **UDP 7141** in the firewall. The server runs as any user and needs no capabilities. The
log goes to stdout.

### Command line

```
recvx-server [--config PATH] [-v] [--KEY VALUE ...]
  --config PATH   the config file (default /etc/recvx-server.conf if it exists)
  -v, --verbose   log requests, joins and relay changes too
  -h, --help      usage and every key with its default
```

Every config key is also a flag: `--relay-kb-per-sec 1024` or `--relay-kb-per-sec=1024` (`_`
and `-` are the same, case is ignored). Boolean keys (`--verbose`) take no value. An unknown flag
or an invalid value is an error and the server does not start (exit code 2).

### systemd

`pc/server/recvx-server.service` runs the server without flags (so `/etc/recvx-server.conf` is the
configuration) as a hardened `DynamicUser` service; `ExecReload` sends SIGHUP.

```sh
sudo install -m 755 build-server/recvx-server build-server/recvx-client /usr/local/bin/
sudo install -m 644 pc/server/recvx-server.conf.example /etc/recvx-server.conf   # then edit it
sudo cp pc/server/recvx-server.service /etc/systemd/system/
sudo systemctl enable --now recvx-server
sudo systemctl reload recvx-server      # after editing limits / log level
journalctl -u recvx-server              # log and metrics lines
```

### Docker

`pc/server/Dockerfile` builds a static musl binary into an empty image (user 65534, the example
config as `/etc/recvx-server.conf`). Build from the repository root:

```sh
docker build -f pc/server/Dockerfile -t recvx-server .
docker run -d --name recvx-server --restart unless-stopped -p 7141:7141/udp --read-only --cap-drop ALL \
    -v "$PWD/recvx-server.conf:/etc/recvx-server.conf:ro" recvx-server
docker kill -s HUP recvx-server         # reload
```


### Testing a server without the game

`recvx-client SERVER host` opens a room and prints its code; on another machine
`recvx-client SERVER join CODE` joins it and prints the path (direct, LAN, relay) and the round
trip. Options: `--pw P`, `--relay` (Server Relay), `--no-punch` (forces the relay fallback),
`--loss PCT`, `--secs N`, `--size BYTES`, `--dgrams N` (load), `--plain` (no encryption).

## Configuration

Sources, later ones win: built-in defaults < config file < `RECVX_SERVER_PORT` (environment) <
command-line flags. The file is `/etc/recvx-server.conf` when it exists, or the file named by
`--config` (which must exist).

**File format**: one `key = value` per line; `#` or `;` starts a comment (also after a value);
blank lines and `[section]` headers are ignored; keys are case-insensitive and `-` equals `_`;
a UTF-8 BOM and CRLF line ends are accepted; lines up to 511 bytes. An unknown key is a warning
(the rest applies). An invalid value (not a whole number, out of range, not an IPv4 address,
empty) is an error with file and line, and the server refuses to start.
`pc/server/recvx-server.conf.example` lists every key at its default.

**Reload**: SIGHUP re-reads the file and flags and applies all limits, `log_level` and
`stats_interval` at once (budgets and counters are kept). `port`, `listen` and `max_rooms`
need a restart. A file with an error on reload is refused and the running settings stay.

| key | flag alias | default | range | meaning |
|---|---|---|---|---|
| `port` | | 7141 | 1-65535 | UDP port. `RECVX_SERVER_PORT` overrides the file. |
| `listen` | `--bind` | 0.0.0.0 | up to 8 IPv4 addresses, comma-separated | One socket each; 0.0.0.0 = all. A multi-homed server should list its public addresses (replies must leave from the address a game talked to). IPv6 is refused. |
| `max_rooms` | | 1024 | 1-65535 | Open rooms at once (~300 bytes each). Restart to change. |
| `rooms_per_ip` | | 4 | 1-65535 | Rooms one IP address hosts at once. |
| `rooms_per_ip_per_min` | | 6 | 1-1000000 | New rooms per IP address and minute (retries do not count); stops churning of the code grace table. |
| `requests_per_sec` | | 20 | 1-1000000 | Requests (not relay) per IP address and second, bursts of 2 s. A game needs ~1. |
| `failed_joins_per_min` | `--joins-per-min` | 5 | 1-1000000 | Failed joins (unknown code, wrong password) per IP address and minute. |
| `requests_total_per_sec` | | 5000 | 1-100000000 | Requests from all addresses together (floods from many or spoofed addresses). |
| `failed_joins_total_per_min` | | 600 | 1-100000000 | Failed joins from all addresses together; when used up, joins wait for the next minute. |
| `relay_kb_per_sec` | `--relay-kbps` | 512 | 1-10000000 | Relay bandwidth per player, KB/s (1024 bytes) of UDP payload; a bucket holding 1 s. |
| `relay_datagrams_per_sec` | `--relay-pps` | 600 | 1-10000000 | Relayed datagrams per player and second; a bucket holding 1 s. |
| `relay_address_kb_per_sec` | | 1024 | 1-10000000 | Per IP address, all its players and rooms; leaving and rejoining does not refill it. |
| `relay_address_datagrams_per_sec` | | 1200 | 1-10000000 | The same in datagrams. |
| `relay_total_kb_per_sec` | `--total-kbps` | 25600 | 0-100000000 | All relayed traffic (25 MB/s, ~210 Mbit/s each way, ~150 busy rooms). 0 = no limit. |
| `relay_total_datagrams_per_sec` | | 50000 | 0-100000000 | All relayed datagrams. 0 = no limit. |
| `room_timeout` | `--timeout` | 30 | 5-86400 s | Host silent this long: the room closes. |
| `member_timeout` | | 10 | 6-86400 s | Joiner silent this long: its place is free. |
| `code_grace` | `--grace` | 120 | 0-86400 s | A closed room's code is not reissued for this long. 0 = none. |
| `log_level` | | normal | normal, verbose | See [Logging](#logging). |
| `verbose` | `-v`, `--debug` | no | yes/no | Same as `log_level = verbose`. |
| `stats_interval` | | 300 | 0-86400 s | Metrics line in the log; 0 = off. |

### Sizing the relay

Only relayed rooms cost bandwidth. Typical game traffic through the relay (30 frames/s):

| sender | datagrams/s | bandwidth |
|---|---|---|
| host, vanilla room | ~120 | ~110 KB/s |
| host, busy room (extra enemies, randomizer) | ~150 | ~160-170 KB/s |
| host, protocol ceiling (16 events + 48 snapshots per frame) | ~210 | ~213 KB/s |
| joiner (pose only) | ~30 | ~6 KB/s |

The per-player defaults leave about 3x headroom; buckets hold a second's worth, so bursts pass.
Over a limit, datagrams are dropped (the game resends what matters), the room is logged at most
every 10 s, and the room stays. 1 TB of egress a month is roughly 1,600-2,400 relayed room-hours.
Server CPU is a few microseconds per relayed datagram; memory stays around 1 MB.

## Logging

Two levels, to stdout (journal, `docker logs`; no files). Configuration errors at start and on
reload go to stderr.

- **normal** (default): the start line (every setting in effect and the config file used),
  errors, rooms opened and closed (with reason, duration and relayed bytes), limit hits (each at
  most once per 10 s per address / room / server), reload results. No IP addresses at all.
- **verbose**: also every request and its result (HELLO, CREATE retries, JOIN outcomes, stale
  cookies, wrong tokens, invalid datagrams, STATUS), partners joining or leaving and why, and the
  relay state per room. Addresses are truncated to /24 (`198.51.100.x`).

Room codes appear only as their first half (`K7QM-xxxx`). Never logged: passwords, verifiers,
salts, keys, tokens, cookies, game data. All event lines share a budget of 20 lines a second
(bursts of 200); lines over it are dropped and counted, and the next line that passes says how
many were held back.

Logging never blocks packet processing: lines go to a 64 KB queue written between packet
batches; a stalled reader loses (counted) log lines, not traffic.

## Metrics

Counters and gauges kept in memory, readable two ways:

- **Summary line** in the log every `stats_interval` seconds:
  `stats: up 120 s, cpu 0.06 s (0.05%), rss 1.1 MB | rooms 1, players 2 | ... | drops: ... | top rooms: E9XM-xxxx 164.9 KB/s`
- **STATUS** (message type 12, answered only to 127.0.0.1): `recvx-client 127.0.0.1[:PORT] status`
  prints everything in the Prometheus text format. For Prometheus, a cron job can write it to a
  node_exporter textfile collector
  (`recvx-client 127.0.0.1 status > recvx.prom.$$ && mv recvx.prom.$$ .../textfile_collector/recvx.prom`).
  There is no HTTP port.

Metric names: `recvx_uptime_seconds`, `recvx_cpu_seconds_total{mode}`,
`recvx_resident_memory_bytes`, `recvx_rooms`, `recvx_players`, `recvx_rooms_opened_total`,
`recvx_rooms_closed_total{why="host|timeout"}`, `recvx_joins_total{result="ok|failed"}`,
`recvx_partners_left_total{why="rejoined|removed|timeout"}`, `recvx_sessions_total{path="direct|relay"}`,
`recvx_requests_total`, `recvx_requests_per_second`, `recvx_relay_datagrams_total{dir="in|out"}`,
`recvx_relay_bytes_total{dir}`, `recvx_relay_bytes_per_second{dir}` (last ~10 s),
`recvx_drops_total{reason}`, `recvx_code_retries_total`, `recvx_log_lines_held_back_total`,
`recvx_send_failures_total`, and `recvx_room_relay_bytes_per_second{room}` for the 5 busiest rooms.
Drop reasons: `request_rate`, `relay_datagram_rate`, `relay_bandwidth` (per player),
`relay_address`, `relay_total`, `invalid`, `unknown_room`, `foreign` (third address or wrong
token), `relay_auth` (bad MAC), `relay_replay`, `busy` (address table full, no randomness).

A health check is a HELLO round trip (or `recvx-client 127.0.0.1 status` on the machine).

## Connection flow

```
 host game ---- UDP ----\                       /---- UDP ---- joining game
                         recvx-server (one UDP port)
 host game <=========== direct UDP (after hole punching) ===========> joining game
```

1. **Host opens a room.** HELLO -> WELCOME (cookie, the host's public address). CREATE ->
   ROOM (room id, code, host token). Keepalives every 1 s while waiting (a lost INTRO returns in
   the answer), every 3 s in a session. Requests are retried every 500 ms for up to 10 s.
2. **Joiner joins.** HELLO/WELCOME, JOIN (code, password proof, rejoin tag). The server checks
   code, password and a free place, then sends **INTRO** to both (peer addresses and public key,
   own token, role, epoch).
3. **Hole punching** (Automatic). PUNCH every 50 ms to the peer's LAN address (same public
   address: "direct (LAN)"), its public address, and public port +1/+2 when the public addresses
   differ. Punches carry a session-key MAC and an "I hear you" flag; an unflagged punch is
   answered once with the flag, and a flagged punch or an authenticated datagram arriving directly
   confirms the path. Flagged punches are never answered.
4. **Relay fallback.** No path after 2 s: RELAY frames through the server. Punching continues
   every 500 ms for 10 s; a confirmation then switches to direct without a reconnect. Later
   punches are ignored. **Server Relay** never punches.
5. **Close.** CLOSE from the host, or `room_timeout` without its keepalive; the joiner's place
   frees after `member_timeout`.

**Lost room.** A waiting host without keepalive answers for 5 s registers again (new keys, new
code, shown in the lobby). In a session, after 35 s without an answer a direct session goes on
(the host re-registers once the partner is gone) and a relayed session ends. A stalled game
thread (long load, debugger) does not count as a lost room.

### Room codes

- Assigned by the server: 8 characters from `ABCDEFGHJKLMNPQRSTUVWXYZ23456789` (40 bits), shown
  `XXXX-XXXX`; input ignores case, `-` and spaces.
- Unique among open rooms and not reissued for `code_grace` seconds (collisions are redrawn, up to
  64 times). If the grace table is full, a closed code skips its grace period (logged).
- A room is bound to the creating address and a 16-byte host token; KEEPALIVE, CLOSE and KICK need
  both (the joiner: its own token), anything else is dropped as `foreign`.
- CREATE is idempotent: a retry with the same request id gets the same room; a new request id from
  the same address closes the old room first.
- Guessing: with 1000 open rooms one guess hits with p ~ 1e-9, inside the failed-join limits.

### Rejoin, KICK and member timeout

- **Rejoin tag.** JOIN carries BLAKE2b keyed with the player's secret (`coop_player_key` in
  recvx.ini) over `"recvx rejoin v1"` and the room code. A JOIN to a full room with the same
  non-zero tag replaces the stale member at once (its game crashed): new token, epoch, INTROs and
  keys. Otherwise: "The room is full". The password check comes first; a tag is useless in
  another room.
- **KICK** ("Remove Partner"): only from the host's address with its token, only for the
  member sequence it names. The server frees the place and keeps the room and code; the host drops
  the old keys at once.
- **member_timeout** (10 s): a joiner without keepalive or authenticated relay traffic leaves; with
  keepalives every 3 s, two may be lost.

## Wire protocol

`RDV_VERSION` is **3** (`pc/src/net/rdv_proto.h`): 2 added authenticated RELAY frames and keys
bound to the introduction, 3 the rejoin tag and KICK. Other versions get `ERROR version` ("The
co-op server speaks protocol N, this game M: update ..."). The co-op game protocol
([COOP.md](COOP.md)) is separate.

Every server message, punch and relay frame starts with an 8-byte header, little-endian:

```
u32 magic   "CVXS"  (game datagrams use "COOP"; encrypted ones "CVXE")
u16 version  3
u8  type
u8  flags
```

Sizes are exact per type; HELLO is padded to 64 bytes so that no reply is larger than its request.
No datagram on any path exceeds 1200 bytes. Addresses are family u8 (4 or 6; 0 none) + port u16 +
16 bytes (IPv4 in the first 4).

| type | direction | body |
|---|---|---|
| HELLO 1 | c -> s | nonce u32, padding to 64 bytes |
| WELCOME 2 | s -> c | nonce, cookie 16 (BLAKE2b keyed with a per-run secret over address + minute; valid 1-2 min), your public address |
| CREATE 3 | c -> s | cookie, request id u64, flags (password, e2e), salt 16, verifier 32, X25519 public key 32, LAN address |
| ROOM 4 | s -> c | request id, room id u32 (slot + generation), code 8, host token 16 |
| JOIN 5 | c -> s | cookie, code 8, flags (proof given), proof 32, public key 32, LAN address, rejoin tag 16 (zero = none) |
| INTRO 6 | s -> c | room id, member sequence u32 (epoch), role, flags, your token 16, peer public address, peer LAN address, peer public key |
| PUNCH 7 | game -> game | room id, attempt, MAC 16; header flag 1 = "I hear you" |
| RELAY 8 | c -> s -> c | room id, counter u32, MAC 8 (24-byte header in all), then the encrypted game datagram |
| KEEPALIVE 9 | c <-> s | request: room id, token, member sequence the host knows; reply: room id, partner present |
| CLOSE 10 | c -> s | room id, token |
| ERROR 11 | s -> c | code, server version, salt (with "password needed" / "wrong password") |
| STATUS 12 | localhost only | request; reply: the metrics as Prometheus text, in as many datagrams of whole lines as needed |
| KICK 13 | host -> s | room id, host token, member sequence; answered like a KEEPALIVE |

ERROR codes (`RDV_E_*`): 1 VERSION, 2 NO_ROOM ("Room not found"), 3 NEED_PW (carries the salt;
a joiner with a password retries with a proof), 4 BAD_PW, 5 FULL, 6 RATE ("Too many attempts.
Wait a minute."), 7 COOKIE (stale: the client restarts with HELLO), 8 SERVER_FULL ("full or busy":
no free room, address table full, no randomness), 9 TOO_MANY (`rooms_per_ip`).

**Datagram budget.** A game datagram to a server partner is at most 1200 - 52 bytes: the RELAY
header (24) plus the encryption overhead (28) still fit in 1200, so no path fragments with an MTU
of 1280 or more. A punched path carries +28 bytes per datagram, a relayed one +52.

## Security

### What the server can and cannot see

- The server sees source addresses, room codes, timing and sizes. It never sees passwords, keys
  or game data: every server session is end-to-end encrypted, punched or relayed, with or without
  a password.
- **Without a password** a passive observer (including a logging server) learns nothing. An
  *active* malicious server could swap the public keys (man in the middle). The
  status line shows a **check code** once the keys are confirmed ("..., encrypted, check code
  K7Q2MX"): 6 characters derived from the same transcript as the keys, so they differ on the two
  sides if the keys were swapped. Compare it by voice, or use a password.
- **With a password** neither a malicious server nor anyone on the path can read, alter or
  downgrade the session: the keys also depend on the Argon2id password key, which the server never
  has. It holds only the verifier, from which it could try to guess weak passwords offline.
- Direct P2P (`ip:port#CODE`) is not encrypted.

### End-to-end encryption

Crypto is Monocypher 4.0.2 (fetched by CMake): BLAKE2b, Argon2id, X25519, XChaCha20-Poly1305.

- Each game makes a fresh X25519 key pair per registration; the public keys travel in
  CREATE / JOIN and INTRO. A low-order peer key (zero shared secret) is refused.
- Both derive `BLAKE2b-512(keyed with the password key if any, "recvx e2e v2" | shared secret |
  host pk | joiner pk | room id | epoch | flags | room code)`, giving two directional
  XChaCha20-Poly1305 keys, the punch MAC key, a code (co-op protocols before 28 proved it per datagram) and the check code. Any change to
  the introduction on the way gives the two sides different keys: nothing decrypts, nothing goes
  out in clear.
- Encrypted datagram: `"CVXE"` | 64-bit counter (the nonce) | ciphertext | 16-byte MAC
  (+28 bytes). A 64-entry window drops replays.
- **Epochs.** The member sequence from INTRO is the epoch: a repeat changes nothing, an older one
  is dropped, a newer one gives new keys (counters restart only with new keys). The host accepts
  only epochs `ours + 1 .. ours + 64`, so a forged far-future epoch cannot block real ones.
- **Encryption is decided locally**, not by the INTRO's flags: the host encrypts by its own
  setting (`COOP_E2E`, on) and always with a password; a joiner refuses a plaintext host unless it
  allows plaintext itself, never with a password, and refuses a room without a password when it
  typed one.
- **Key confirmation**: the first punch with a valid MAC or the first authenticated datagram
  proves the partner has the same keys (and password); only then does the status say "encrypted".

**Password.** The host draws a 16-byte salt, computes `K = Argon2id(password, salt)` (4 MB, 3
passes, ~20 ms) and sends `verifier = BLAKE2b(K, "recvx verify")`. A joiner without a proof gets
`ERROR need password` with the salt, then proves `BLAKE2b-keyed(verifier, cookie | code | its
public key)`. The server keeps the verifier in memory for the room's life only. Secrets are wiped
after use, on stop and on failure.

**Fail closed.** Without OS randomness or memory for Argon2, the server refuses CREATE / JOIN and
the client fails, instead of using zero keys or tokens.

### Authenticated relay frames

RELAY frames carry a counter and an 8-byte MAC keyed with the sender's member token (16 random
bytes the server gave only to that member; the partner never sees it). The server checks the MAC
and a 64-entry counter window first; only then does the frame cost budget, count as a sign of
life, or get forwarded. Third addresses, wrong MACs and replays are dropped (`foreign`,
`relay_auth`, `relay_replay`).

### Amplification and flood protection

- No reply to an unverified source is larger than its request (HELLO is padded to 64 bytes).
  Everything after HELLO needs the cookie bound to the source address, so a spoofed source never
  gets a room, a join or relay service, and the server cannot reflect traffic at third parties.
- **Two address tables.** A direct-mapped pre-cookie table only rate-limits replies
  (`requests_per_sec`, capped globally by `requests_total_per_sec`). A second table holds verified
  addresses (valid cookie or token) with failed joins, room creations and the relay budget, under
  a keyed hash; an entry is replaced only after 2 minutes idle, and a full neighbourhood refuses
  the newcomer (`busy`) instead of evicting. Spoofed traffic cannot reset an address's budgets.
- Per-player, per-address and server-wide relay budgets (see [Configuration](#configuration)). A
  new member's per-player budget starts empty; throttled frames do not refresh liveness.

### Password guessing backoff

There is no room lockout. Wrong passwords count per verified address (`failed_joins_per_min`)
and globally (`failed_joins_total_per_min`). Per room, past 10 wrong passwords in a minute, an
address that already failed a join this minute must wait 2 s after the room's last wrong password
(`ERROR rate`, not checked). An address without failures is always checked, so the real joiner
gets in however hard the room is being guessed.

### Remaining limits

- An on-path attacker can drop or delay anything (denial of service), but cannot read or forge
  game data.
- Member tokens travel in clear in ROOM / INTRO, so an on-path attacker could forge RELAY frames
  for a member: this costs the member's relay budget, it never reaches its data.
- Punches carry a MAC but no counter: within the punching window an on-path attacker can replay
  one from another address and move a not yet direct session's path there (denial of service, not
  a data leak). After the window punches are ignored.

## IPv4 / IPv6

The game and the server are IPv4-only: the sockets are IPv4, and `listen` refuses IPv6 addresses.
The wire format already carries 16-byte addresses with a family byte, so IPv6 candidates can be
added without a protocol change. IPv6 would help players behind CGNAT who have native IPv6.

## Limits

- No TCP fallback: networks that block outgoing UDP cannot use co-op through the server.
- Port prediction goes only to +1/+2: cone/symmetric and symmetric/symmetric NAT pairs end on the
  relay. Cone/cone pairs and players behind the same router connect directly.
- Argon2id runs once on the game thread when a password is used: a ~20 ms hitch in the lobby.
- A host that quits without leaving (crash, kill) keeps its room open until `room_timeout`; a
  crashed joiner can take its place back at once (rejoin tag), others wait for `member_timeout`.

## Tests

| test | what |
|---|---|
| ctest `rdv` (`pc/tests/test_rdv.c`) | protocol and server core: messages, crypto, room codes, hijacks, passwords, rejoin/KICK, limits, config parser (the shipped example must equal the defaults), metrics, log privacy, amplification invariant |
| ctest `rdv_client` (`pc/tests/test_rdv_client.c`) | the client (`rendezvous.c`) against the server core on a fake network and clock: INTRO checks, epochs, downgrades, relay/direct switching, restarts, stalls, DNS |
| ctest `rdv_fuzz` (`pc/tests/fuzz_rdv.c`, Linux) | parser, server and client state machines under AddressSanitizer + UBSan, 300,000 inputs; libFuzzer-ready with `clang -fsanitize=fuzzer -DRDV_LIBFUZZER` |
| ctest `rdv_paths` (`pc/server/test_paths.sh`, Linux) | a local server and two `recvx-client`s: punched, Via server, relay fallback, passwords, loss, plaintext, sustained relay delivery |
| ctest `rdv_nat` (`pc/server/test_nat.sh`, Linux) | cone/symmetric NATs, same router, netem, in rootless network namespaces (skipped without user namespaces) |

Test aids in the game (environment variables):

| variable | effect |
|---|---|
| `RECVX_COOP_FORCE_RELAY=1` | always relay (like Server Relay) |
| `RECVX_COOP_NO_PUNCH=1` | no punching: exercises the relay fallback |
| `RECVX_COOP_PUNCH_MS=N` | punching time before the relay fallback (default 2000) |
| `RECVX_COOP_STATS=1` | every 300 frames: the path, datagrams, bytes and framing overhead |
