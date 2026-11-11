# recvx-server: co-op rendezvous and relay

`recvx-server` is an optional helper for online co-op ([COOP.md](COOP.md)). It lets two games
find each other by a short **room code** instead of an IP address, introduces them so they can
open a direct connection through their routers (**hole punching**), and **relays** their traffic
when no direct path works.

- UDP only, IPv4 only, one port (default **7141**).
- No game, no SDL, no disc. It sees room codes and addresses; game data it relays is end-to-end
  encrypted.
- All state is in memory; nothing is written to disk. A restart forgets every room (the games
  open a new one automatically); an update with `--take-over` keeps them all
  ([Updating without dropping sessions](#updating-without-dropping-sessions)).
- Direct P2P co-op (`ip:port#CODE`) does not use the server at all.

## For players

The game comes with a server address filled in: **eu.cvxenhanced.de** (UDP 7141;
`COOP_DEFAULT_SERVER` in `pc/src/host/coop.h`). The game contacts it only when you host or join
through it, never at startup or at the title. To use your own server (below), both players enter
its address instead; to use none, clear the field (it stays empty):

| where | setting |
|---|---|
| Lobby (co-op screen) | mode **VIA SERVER** / **DIRECT**, row **SERVER**, **PASSWORD**, **RELAY** (*Only if needed* / *Always*), **COPY ROOM CODE** |
| F3 window | **Connection**, **Co-op Server**, **Room code** / **Join address** |
| ENHANCED SETTINGS > CO-OP (F1) | **Connection** (Automatic, Direct, Server Relay) |
| recvx.ini | `coop_conn` (0 Automatic, 1 Direct, 2 Server Relay), `coop_server` (`host` or `host:port`; empty = no server; a file from before the default, without the `coop_server_version` line, gets the default once in place of an empty value) |
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
recvx-server --take-over [--config PATH] [--KEY VALUE ...]
recvx-server --drain [--config PATH]
  --config PATH   the config file (default /etc/recvx-server.conf if it exists)
  -v, --verbose   log requests, joins and relay changes too
  --take-over     replace the server running on the control socket (see below)
  --drain         tell that server to open no new rooms and to stop after the last one
  -h, --help      usage and every key with its default
```

Every config key is also a flag: `--relay-kb-per-sec 1024` or `--relay-kb-per-sec=1024` (`_`
and `-` are the same, case is ignored). Boolean keys (`--verbose`) take no value. An unknown flag
or an invalid value is an error and the server does not start (exit code 2).

Signals: SIGTERM / SIGINT stop it, SIGHUP reloads the config, SIGUSR1 drains it.

### systemd

`pc/server/recvx-server.service` is a **user** unit: the server runs as an ordinary user, from a
layout in that user's home that keeps every installed version side by side:

```
~/recvx-server/
    versions/0.7.6/recvx-server     one directory per release (recvx-client next to it)
    versions/0.7.7/recvx-server
    current -> versions/0.7.7       the version that runs (ExecStart and ExecReload use it)
    recvx-server.conf               the config, with control = /home/USER/recvx-server/control.sock
```

```sh
mkdir -p ~/recvx-server/versions/0.7.7 ~/.config/systemd/user
install -m 755 recvx-server recvx-client ~/recvx-server/versions/0.7.7/
ln -sfn versions/0.7.7 ~/recvx-server/current
install -m 600 recvx-server.conf.example ~/recvx-server/recvx-server.conf   # then edit it: set control
cp recvx-server.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now recvx-server
sudo loginctl enable-linger "$USER"     # start at boot, keep running after logging out
journalctl --user -u recvx-server       # log and metrics lines
```

The unit (`~/.config/systemd/user/recvx-server.service`; the shipped file adds these notes as
comments):

```ini
[Unit]
Description=recvx co-op rendezvous and relay server

[Service]
Type=notify
NotifyAccess=all
ExecStart=%h/recvx-server/current/recvx-server --config %h/recvx-server/recvx-server.conf
ExecReload=%h/recvx-server/current/recvx-server --take-over --config %h/recvx-server/recvx-server.conf
Restart=always
RestartSec=1
NoNewPrivileges=yes
RestrictAddressFamilies=AF_INET AF_INET6 AF_UNIX
RestrictNamespaces=yes
RestrictRealtime=yes
RestrictSUIDSGID=yes
LockPersonality=yes
MemoryDenyWriteExecute=yes
SystemCallArchitectures=native
SystemCallFilter=@system-service
SystemCallFilter=~@privileged @resources
UMask=0077
MemoryMax=64M
TasksMax=8

[Install]
WantedBy=default.target
```

A reload that is refused fails the reload job and leaves the running server as it was: the
`ExecReload` process exits with the take-over's code (3 = incompatible; the journal says
`Control process exited, code=exited, status=3/NOTIMPLEMENTED`), while `systemctl --user reload`
itself exits 1.

The unit is `Type=notify` (the server says `READY=1` once it listens) with `NotifyAccess=all`,
`Restart=always`, and `ExecReload=%h/recvx-server/current/recvx-server --take-over --config
%h/recvx-server/recvx-server.conf`: **`systemctl --user reload recvx-server` installs the version
`current` points at without dropping a session** ([Updating without dropping
sessions](#updating-without-dropping-sessions)). It applies the whole config file too, except
`port`, `listen` and `control`. Its hardening needs no namespaces (a user manager may not be
allowed to create them): a system call filter, no new privileges, IPv4/IPv6/unix sockets only,
64 MB of memory.

For changing only limits or the log level, SIGHUP still works (`systemctl --user kill -s HUP
recvx-server`).

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
| `control` | | none | an absolute path, up to 107 bytes | Unix socket for `--take-over` and `--drain`, created with mode 0600. Restart to change. See [Updating without dropping sessions](#updating-without-dropping-sessions). |

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

Counters and gauges kept in memory, readable three ways:

- **Summary line** in the log every `stats_interval` seconds:
  `stats: up 120 s, cpu 0.06 s (0.05%), rss 1.1 MB | rooms 1, players 2 | ... | drops: ... | top rooms: E9XM-xxxx 164.9 KB/s`
- **STATUS** (message type 12, answered only to 127.0.0.1): `recvx-client 127.0.0.1[:PORT] status`
  prints everything in the Prometheus text format. For Prometheus, a cron job can write it to a
  node_exporter textfile collector
  (`recvx-client 127.0.0.1 status > recvx.prom.$$ && mv recvx.prom.$$ .../textfile_collector/recvx.prom`).
  There is no HTTP port.
- **status-json** (the same STATUS, only to 127.0.0.1): `recvx-client 127.0.0.1[:PORT] status-json`
  prints one JSON object on one line, for monitoring and management tools (exit 0; 1 when the
  server does not answer, or is a version without status-json):

```json
{"version":"0.7.7","state_format":1,"started_at":1794343182,"uptime_s":3612,"process_started_at":1794346500,"handovers":1,
 "draining":false,"handover_capable":true,
 "rooms":2,"room_list":[{"id":"5f0c2a9e41b7","age_s":95,"players":2,"path":"relay","relay_bps":171230},
                        {"id":"c81d07e3aa52","age_s":12,"players":1,"path":"waiting","relay_bps":0}],
 "room_list_truncated":false,"players":3,"sessions":{"direct":0,"relay":1},
 "counters":{"recvx_uptime_seconds":3612,"recvx_cpu_seconds_total":{"user":0.412,"system":0.733},...,
             "recvx_drops_total":{"request_rate":0,...,"busy":0},"recvx_code_retries_total":0,...,
             "drops":{"request_rate":0,"relay_datagram_rate":0,...,"busy":0}},
 "drops":{"request_rate":0,"relay_datagram_rate":0,...,"busy":0}}
```

| field | meaning |
|---|---|
| `version` | the release version (`--version` adds the git description) |
| `state_format` | the handover state format this build writes and reads |
| `started_at`, `uptime_s` | the service's start (unix seconds) and age: kept across `--take-over` (so is `recvx_uptime_seconds`) |
| `process_started_at`, `handovers` | this process's start (unix seconds), and the take-overs since `started_at`: a take-over raises `handovers` and keeps `started_at`, a crash or restart starts both again |
| `draining` | `--drain` / SIGUSR1: no new rooms, stops after the last one |
| `handover_capable` | a control socket is open: `--take-over` can replace this process |
| `rooms`, `players` | open rooms; players in them (hosts + partners) |
| `room_list` | each room: `id` (a keyed hash of its internal id, stable across a take-over, nothing of the room code), `age_s`, `players` (1 or 2), `path` (`waiting` = no partner, `relay` = relayed in the last 5 s, else `direct`), `relay_bps` (relayed bytes per second, last ~10 s). At most ~600 rooms; `room_list_truncated` says when more are open |
| `sessions` | rooms with a partner now, by path |
| `counters` | every metric below by its Prometheus name; one with a label is an object by label value (`"recvx_joins_total":{"ok":3,"failed":0}`); `drops` repeats `recvx_drops_total` by reason. `recvx_room_relay_bytes_per_second` is left out (`room_list` has it) |
| `drops` | the drops by reason (the same as `counters.drops`) |

It never contains room codes, passwords, tokens, keys or addresses. The counters (`*_total`,
`drops`) only grow, across `--take-over` too; the gauges are counted again.

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

## Updating without dropping sessions

With `control = PATH` in the config, a new version replaces the running process in place: the
UDP socket, every room, code, token and key, the code grace table, the per-address limit tables
and the counters move over, and no game notices. Under the [systemd user unit](#systemd) this is
`systemctl --user reload recvx-server` after pointing `current` at the new version:

```sh
mkdir -p ~/recvx-server/versions/0.7.8 && install -m 755 recvx-server recvx-client ~/recvx-server/versions/0.7.8/
ln -sfn versions/0.7.8 ~/recvx-server/current
systemctl --user reload recvx-server    # fails, and changes nothing, when the new version cannot take over
```

Without systemd, the same is `NEW/recvx-server --take-over --config FILE`. It returns once the
handover is done or refused (the new server goes on in the background, logging to the same
stdout):

| exit | meaning |
|---|---|
| 0 | the new process runs now; the old one exited 0 |
| 3 | incompatible: the running server's state format is not one this build reads. Nothing was taken over; the old server goes on |
| 2 | configuration errors (printed) |
| 1 | anything else (no server on the control socket, it answered `busy`, a time-out, an invalid state). Nothing was taken over; the old server goes on |

How it works:

1. The new process connects to the control socket and asks to take over. The old one checks the
   peer is its own user (SO_PEERCRED), **stops reading the UDP socket** (what arrives meanwhile
   waits in the kernel's buffer) and sends the UDP socket(s) and the listening control socket over
   SCM_RIGHTS, then the state: a versioned blob (`state_format`, fixed-width fields), its length
   announced first and limited to 64 MB.
2. The new process checks the descriptors (bound IPv4 UDP sockets on one port, a listening unix
   socket) and every field (slots, codes, address families, sockets, numbers that must be finite)
   and imports it. A format it cannot read: it answers `incompatible` and exits 3; an invalid
   state: `invalid`, exit 1. Either way the old server resumes at once.
3. Under systemd it sends `MAINPID=<its pid>` to `$NOTIFY_SOCKET` (no libsystemd) and waits with
   `BARRIER=1` until systemd has it, then answers `ok`. The old process answers `bye` and exits 0;
   only then does the new one read the socket, send `READY=1`, and the `--take-over` launcher
   returns 0 (which ends `ExecReload`). `bye` decides: once the new process said `ok` it waits
   for `bye` (then it serves) or for the old one to close the connection (then the old one goes
   on), and the old one does either within its own time-out. The new process does not say `ok`
   once its own time-out (from its connect) has passed. So exactly one of the two serves: no
   answer within 3 s on either side leaves the old server running (and `MAINPID` goes back to
   it).

The pause is the time to copy the state between two processes: milliseconds for a few hundred
rooms, and never more than the 3 s time-out (so the systemd barrier has to be answered within
it too), well below `member_timeout` (at least 6 s). The pause does not count as anyone's
silence: whichever process goes on moves the host and joiner time-outs on by it before the next
check, so a slow or refused take-over drops no player whose keepalives waited in the socket
buffer. Times inside the state are the system's monotonic clock, the same for both processes.
The new process applies its own config file (like a reload) but keeps the running server's
`port`, `listen` and `control`, and a `max_rooms` at least as large. The room ids in
status-json stay, and so do `started_at` and `uptime_s`; `process_started_at` is the new process's and `handovers` goes up by one.

**Incompatible updates (drain)**: when a take-over is refused (exit 3), `recvx-server --drain
--config FILE` (or SIGUSR1) makes the running server open no new rooms while the open ones go
on, and it exits 0 when the last room closed; under systemd, `Restart=always` then starts the
version `current` points at. A refused CREATE gets ERROR 8 (SERVER_FULL): the game shows "The
co-op server is full or busy" at once and the player can try again a moment later. (Dropping the
CREATE silently would only end in "No reply from co-op server ... Check port" after 10 s, and a new
error code would be unknown to games already released.) A retried CREATE of a room that is open
is still answered, joins and rejoins of open rooms still work. A host waiting for its partner
keeps its room open, so a drain can take as long as the longest lobby.

**Security**: the control socket is protected by file permissions only: mode 0600 (created under
`umask 0177`), and connections from another user are refused too. Keep it in a directory only the
server user can write (its home, as in the layout above). A stale socket file (nobody accepts on
it) is removed at start; a live one, or a path that is not a socket, makes the start fail. Nothing
in the handover is reachable over the network: the UDP protocol has no command for it, and STATUS
only reads. Every size is bounded (command lines 64 bytes, the state 64 MB, at most 8 sockets),
one control connection at a time (others get `busy` at once, a silent one is closed after 3 s), and no
code, token or key is logged.

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
| STATUS 12 | localhost only | request (body empty, or `json`); reply: the metrics as Prometheus text, in as many datagrams of whole lines as needed; to `json`: parts of the status-json text, each `J`, part index u16, part count u16, then the text |
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
| ctest `rdv` (`pc/tests/test_rdv.c`) | protocol and server core: messages, crypto, room codes, hijacks, passwords, rejoin/KICK, limits, config parser (the shipped example must equal the defaults), metrics, log privacy, amplification invariant, the handover state (round trip; truncated, corrupted and other-format blobs) |
| ctest `rdv_client` (`pc/tests/test_rdv_client.c`) | the client (`rendezvous.c`) against the server core on a fake network and clock: INTRO checks, epochs, downgrades, relay/direct switching, restarts, stalls, DNS |
| ctest `rdv_fuzz` (`pc/tests/fuzz_rdv.c`, Linux) | parser, server and client state machines under AddressSanitizer + UBSan, 300,000 inputs; libFuzzer-ready with `clang -fsanitize=fuzzer -DRDV_LIBFUZZER` |
| ctest `rdv_paths` (`pc/server/test_paths.sh`, Linux) | a local server and two `recvx-client`s: punched, Via server, relay fallback, passwords, loss, plaintext, sustained relay delivery |
| ctest `rdv_nat` (`pc/server/test_nat.sh`, Linux) | cone/symmetric NATs, same router, netem, in rootless network namespaces (skipped without user namespaces) |
| ctest `rdv_handover` (`pc/server/test_handover.sh`, Linux) | `--take-over` refused by a build that cannot read the state (the old server goes on), then done by a second binary during relayed and direct sessions (no drops, a code from before still joins, counters only grow, the old process exits 0, MAINPID / BARRIER / READY on a fake notify socket); status-json parsed by python3; `--drain`; the control socket (mode, stale, live); a second control connection gets `busy`; exactly one server after a take-over behind a silent control connection, with a systemd barrier slower than the time-out, or with the old server frozen after `ok` |

Test aids in the game (environment variables):

| variable | effect |
|---|---|
| `RECVX_COOP_FORCE_RELAY=1` | always relay (like Server Relay) |
| `RECVX_COOP_NO_PUNCH=1` | no punching: exercises the relay fallback |
| `RECVX_COOP_PUNCH_MS=N` | punching time before the relay fallback (default 2000) |
| `RECVX_COOP_STATS=1` | every 300 frames: the path, datagrams, bytes and framing overhead |
