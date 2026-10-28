# Online co-op

Two players, each with their own copy of the game and the same build, play one game together
over the internet or a LAN (UDP). Co-op is an optional addon: with it off the game behaves exactly
as without it (every hook is `#ifdef TARGET_PC` and `if (coop_on)`).

Part 1 is for players: how to host, join and play. Part 2 is the reference: architecture,
protocol 30, security and limits. Related: [SERVER.md](SERVER.md) (the optional co-op server),
[USER_GUIDE.md](USER_GUIDE.md) (settings, controls), [ACTORS.md](ACTORS.md) (models and voices),
[ENEMIES.md](ENEMIES.md) (enemy options), [ARCHITECTURE.md](ARCHITECTURE.md),
[BUILDING.md](BUILDING.md).

---

# Part 1: Playing co-op

## Overview

* One player **hosts**, the other **joins**. The host's game runs the session: the room's enemies,
  the story progress, the game rules.
* Both control their own character. Each sees the other with its chosen model and name, hears
  its footsteps, shots and voice.
* What one player does to the world happens for both, once: a lever pulled, a door unlocked,
  zombies rising, a cutscene started by picking something up.
* Each player has an inventory of their own. Items can be traded on the trade screen.
* The default port is UDP **7140**. Only the host has to be reachable (see "Playing over the
  internet"); a co-op server can introduce the two games when neither can open a port.

Ways to start a session:

| Where | How |
|---|---|
| Title menu **MULTIPLAYER** | The lobby screen (below): the usual way. |
| **F3** | The co-op window, at the title and in game (also to join a running game from your own game). |
| Command line | `--coop-host PORT`, `--coop-join HOST:PORT#CODE` or `--coop-join ROOMCODE`, see "Command line". |

## Lobby

Choose **MULTIPLAYER** in the title menu (its last row; keyboard, gamepad or mouse). The lobby is
drawn in the game's own style; your model turns in a 3D preview on the left. It goes in steps:

**Step 1: how to connect, and host or join.**

| Row | Meaning |
|---|---|
| **CONNECT** | *Direct*: the partner connects straight to the host's address. *Via server*: a co-op server puts you together by a room code. |
| **HOST A GAME** / **JOIN A GAME** | Go to step 2 for that role. |
| **BACK** | Back to the title menu; this closes the session. |

**Step 2: only what that choice needs.** A heading names the step (e.g. "HOST A GAME - DIRECT").

| Role, connection | Rows |
|---|---|
| Host, Direct | **PORT**, NAME, MODEL, **START**, **GAME RULES**, **OPEN THE LOBBY**; once open: **START THE GAME**, **COPY JOIN ADDRESS**, **CLOSE THE LOBBY**, **REMOVE PARTNER** (with a partner) |
| Host, Via server | **SERVER**, **PASSWORD** (optional), **RELAY**, NAME, MODEL, START, GAME RULES, OPEN THE LOBBY; once open: **COPY ROOM CODE** ("WAITING FOR ROOM CODE" until it arrives) |
| Join, Direct | **ADDRESS** (`ip:port#CODE`), NAME, MODEL, **ITEMS**, **FOLLOW**, **JOIN** / **LEAVE**; once connected: **HOST RULES** |
| Join, Via server | **SERVER**, **CODE** (the room code), **PASSWORD**, **RELAY**, NAME, MODEL, ITEMS, FOLLOW, JOIN / LEAVE |

* **NAME**: what your partner sees above your head and in the lobby. Up to 15 characters: letters,
  digits, space and `-_.!?'()+`. Default: your login name.
* **MODEL** ("Character Model", see "You appear as"): shows your model's name and opens a page with
  the list of all models, in the order and with the names of the other menus. The list scrolls to
  your current model (gold, with an arrow); the 3D preview shows the one under the cursor, and a bar
  at the right shows where you are in the list. Up/down move (past the end to **BACK**), left/right,
  L1/R1 and Page Up/Down a page, Home/End to the first and last; the mouse hovers, the wheel
  scrolls. Confirm or a click takes the model and returns to MODEL; BACK (or cancel, or a right
  click) returns without changing it.
* **START** (host): a new game (normal, easy, very easy) or one of your saves
  ("Save nn: character stage-room"; nn is the save's number, or the memory card slot with the
  Memory Card save system, [USER_GUIDE.md](USER_GUIDE.md#saves)).
* **ITEMS** (joiner): what you bring, see "Saved partner inventories".
* **FOLLOW** (joiner): see "Follow host".
* **RELAY**: *Only if needed* (Automatic) or *Always* (Server Relay), see "Connection modes".

**Step 3: GAME RULES** (host): **BIORAND** (On / Off with the seed, only when the host has a BioRand pack,
see "BioRand"), **LOOT** (Shared / Individual),
**GEAR** (For both / Shared; only with shared loot, see "Loot modes"), **TRADING**, **CUTSCENE SKIP** (Together /
Separately, see "Cutscenes"), **INK RIBBONS** (Needed / Not needed, see "Saving without ink
ribbons"), **GAME OVER** (Each player / Both, see "Game over"), and two entries that open a page
of their own (an arrow after them; RANDOMIZER also shows On or Off):

| Entry | Its page |
|---|---|
| **RANDOMIZER** | The enemy options: **RANDOMIZER**, **VARIETY** (Wild / Mild) and **EVENT ENEMIES** (both shown only with the randomizer on), **SEED**, **RANDOM SEED**, then **EXTRA ENEMIES**, **LAYOUT**, and **CUTSCENE CAST** (it shows On or Off), which opens a page of its own: **RANDOMIZE CAST** (On / Off), **MODELS** (Cutscene Models / All Models) and **ASSIGN** (Per Character / Per Cutscene; both while on), and **IMPORTED MODELS** (On / Off; while on, shown only to a player who has imported models). BACK returns to the RANDOMIZER page. See [ACTORS.md](ACTORS.md) "Cutscene cast". |
| **EXTRA EVENTS** | Rules for single events: **QUICK START** (On / Off, see "Quick start"; hidden and off while BioRand is on), **METAL DETECTOR** (Skipped / Original, see "Metal detector"). |

Confirm or a click opens a page; BACK (or cancel, or a right click) returns to GAME RULES on its
entry. The joiner sees the host's GAME OVER rule and both pages read-only (HOST RULES) once
connected; they follow the host's changes as it makes them.

Both players then see each other in the lobby, with model, name and what the other chose. The
host picks **START THE GAME**; the partner enters the host's game in the host's room once the host
has control.

Navigation: up/down rows, left/right values, confirm, cancel = back; the mouse hovers, clicks,
steps values with the wheel and backs out with a right click. Text fields: type (Ctrl+V pastes,
Enter accepts, Escape cancels); with a gamepad, up/down change the letter, left/right move. The help line under the rows says what the selected row is for. Rows
that cannot change during a session (connection, role, port, server, password, relay, address,
loot) are dimmed.

**BACK** (or cancel, or a right click) goes one step back. From step 1 it leaves the lobby and
closes the session: hosting stops, the port and its router mapping are freed, and the partner sees
"The host closed the lobby" (or "<name> left"). Only starting the game keeps the session open. A
lobby opened from F3 during a session starts on step 2 of its role.

A port another program (or another copy of the game) holds is refused: "Port 7140 is in use. Choose
another." (or "Port 7140 is unavailable. Choose another."); change
PORT and open the lobby again.

### Join codes and passwords

* **Join code** (Direct): the host's game makes an 8-character code per run (letters and digits
  without 0/O/1/I, e.g. `K7QM-3XRA`), shown in the lobby's info lines and in F3. **COPY JOIN
  ADDRESS** puts `ip:port#CODE` on the clipboard. The joiner types that whole string; case, `-`
  and spaces in the code do not matter, and a space may replace `#`. Without the right code the
  host does not answer: "The host did not accept the join code" / "Join code missing. Ask the
  host to use Copy Join Address." After each wrong code the host waits a little longer before it
  answers that address again (up to a minute); the code itself never goes over the network.
* **Room code** (Via server): the server gives the session a code `XXXX-XXXX`; the joiner enters
  it in **CODE**.
* **Password** (Via server, optional): set by the host before opening; the joiner needs the same.
  It is never saved. A password typed for a room that has none is refused ("This room has no
  password. Clear PASSWORD to join.").

## The F3 window

F3 opens the co-op window at the title and in game: the lobby's settings in compact form
(Connection first; Co-op Server and Room Password only when not Direct; **Starting Inventory** adds
*Current Inventory*), the buttons **Host**, **Join**, **Disconnect** and **Remove Partner**
(host), the status (ping, the partner's room), the join or room code with **Copy Join Address** /
**Copy Room Code**, and the F3 trade (see "Trading from the F3 window").

Hosting from F3 at the title and then choosing New Game or Load Game works too: a partner waiting
at the title is pulled into that game.

## Command line

| Flag | Meaning |
|---|---|
| `--coop-host PORT` | Host on this UDP port. |
| `--coop-join HOST:PORT#CODE` | Join a host directly. |
| `--coop-join ROOMCODE` | Join a room through the co-op server. |
| `--coop-server ADDR` | The co-op server (`host` or `host:port`, UDP 7141 implied). |
| `--coop-conn auto\|direct\|server` | Connection mode. |
| `--coop-pw PASSWORD` | Room password. |
| `--save-dir DIR` | Saves and `recvx.ini` folder (two instances on one PC). |

## Connection modes

| Mode (`coop_conn`) | What it does |
|---|---|
| **Automatic** (0, default; lobby RELAY *Only if needed*) | With a server set: tries a direct path first (UDP hole punching, about 2 s), then the server's relay; a direct path found later is taken over without reconnecting. Without a server: exactly Direct. |
| **Direct** (1) | Address and join code only; never contacts a server. |
| **Server Relay** (2; RELAY *Always*) | Always through the relay: predictable, slower. |

Hosting with a server still accepts direct joins (`ip:port#CODE`). An address typed in the
joiner's CODE / ADDRESS row is always joined directly. The first partner to arrive gets the session.

The status line says how you are connected: through a server *Connected directly*, *Connected
directly (local network)* or *Connected via server relay* (the detour adds to the ping); a join by address shows
the address. Every session is *encrypted*, a direct one too (protocol 28).

## Playing over the internet

Only the host must be reachable: the joiner's game sends first, and the host answers wherever that
came from. On the same LAN nothing is needed (join the host's LAN address). Over the internet, in
this order:

1. **Automatic port mapping (UPnP / NAT-PMP).** With **Automatic Port Forwarding** on (the
   default, `coop_upnp`), the host's game asks the router to forward UDP `coop_port` to this PC
   while hosting (2 h lease, renewed while hosting, removed when the session ends and on exit).
   The lobby then shows `ADDRESS ip:port   CODE XXXX-XXXX`: the address **COPY JOIN ADDRESS** gives
   (`PUBLICIP:PORT#CODE` once the router mapped the port, else the local one). F3 shows the
   **Local address** and the join code; its **Copy Join Address** / **Copy Local Address** button
   copies the same. The mapping's result goes only to the log. Without a public address and
   partner, the lobby hints "If your partner cannot connect, set CONNECT to Via server.", F3 "If
   your partner can't connect, use a co-op server (Automatic)." The router must allow UPnP or
   NAT-PMP (some, e.g. FRITZ!Box, need "Permit independent port sharing" per device). A router
   that only allows permanent mappings is not used (a crash would leave the port open for good);
   the log then says to forward by hand. Discovery runs on a background thread and never blocks
   the game; nothing of this runs unless you host.
2. **Manual port forwarding**: in the router, forward **UDP** 7140 (external and internal) to the
   host PC's LAN address (F3's Local address; give the PC a fixed address / DHCP reservation). The partner
   joins `your-public-ip:7140#CODE`.
3. **Firewall** on the host PC, allow incoming UDP 7140:
   * Windows: allow the game when Windows Defender Firewall asks (tick *Private* and *Public* if
     your network is set to public), or in an admin prompt:
     `netsh advfirewall firewall add rule name="RE CVX co-op" dir=in action=allow protocol=UDP localport=7140`
   * Linux firewalld: `sudo firewall-cmd --add-port=7140/udp --permanent && sudo firewall-cmd --reload`;
     ufw: `sudo ufw allow 7140/udp`.
4. **CGNAT / double NAT.** If the log says the router's internet address is not public (100.64-100.127,
   10., 172.16-31., 192.168.), or it differs from what "what is my IP" sites show, the provider (or
   a second router) shares one public address: no port forwarding can make you reachable. Ask the
   provider for a public IPv4 address, use a co-op server, or use a VPN overlay.
5. **VPN overlay**: [Tailscale](https://tailscale.com) or [ZeroTier](https://www.zerotier.com).
   Both players join the same network; the partner joins the host's overlay address (Tailscale
   `100.x.y.z`) with `:7140#CODE`. No router changes; works through CGNAT. Allow the game in the
   firewall as in 3 if it still does not connect.

The joiner's F3 window says when no reply arrives after 5 s: one of the above is missing, the host
has not pressed Host, or the address/port is wrong. "The host already has a partner" means the
place is taken. There is no self-test of the public address (that needs a server outside your
network).

## Playing through a co-op server

When neither player can open a port, a co-op server introduces the two games by a room code and,
only if no direct path is found, relays their traffic. There is no official server: one of you (or
a friend with a small Linux server or VPS) runs `recvx-server` ([SERVER.md](SERVER.md)), and both
enter its address.

1. Both: lobby **CONNECT** *Via server*, HOST A GAME or JOIN A GAME, **SERVER** the server's
   address (`host` or `host:port`). Without a server address, opening or joining is refused
   ("Enter the co-op server address first."). Or F3 / F1 / OPTION > ENHANCED SETTINGS > CO-OP.
2. Host: **OPEN THE LOBBY**. The room code appears in the info lines and F3 (`ROOM CODE XXXX-XXXX`);
   copy it with COPY ROOM CODE / Copy Room Code. Optional PASSWORD before opening.
3. Joiner: **CODE** the room code (case and dash do not matter), the PASSWORD if one is set, **JOIN**.

Messages: "Room not found. Check the code.", "Enter the room password in PASSWORD.", "Wrong
password", "The room is full", "Too many attempts. Wait a minute.", "No reply from co-op server
... Check port 7141 and your connection." (the host keeps hosting directly then), "Cannot find
co-op server ..." (name lookup, at most 5 s, never stalls the game), "Host connection is not
encrypted. Ask the host to update the game."

* Through the server everything the games exchange is **encrypted end to end**; each game decides
  this itself and refuses a partner that would not encrypt. A room password also keeps out an
  operator who would tamper with the introduction (details in [SERVER.md](SERVER.md)).
* If the server restarts or forgets the room while you host, the game opens a new room by itself
  and shows the new code ("Server lost your room. Opening a new one..."); a
  direct session goes on, a relayed one ends.
* The server frees a vanished joiner's place after `member_timeout` (default 10 s); see also
  "Rejoin after a crash".
* Game and server must speak the same server protocol (3); the game protocol (30) is end to end.

## In the game

### Follow host

The joining player's choice (lobby FOLLOW, F3, F1 / ENHANCED SETTINGS CO-OP "Follow Host", ini `coop_follow`, default on; the host's value has no effect). It can change at any time.

* **On**: you go through every door the host takes.
* **Off**: you stay where you are and may explore other rooms. You are moved to the host only when
  you join or rejoin, when the host's game (re)starts (new game, load, continue), when your own
  game restarts (continue after a game over), at a story character switch, and at the end of a
  **story cutscene** of the host (cinema mode lasting 15 s or more). Shorter scenes move nobody, and
  a world event that would take the partner through a door is not replayed for it (it gets the
  flags only).

When pulled, you land at the door the host came through, not next to the host, in the room as the host has it:
some rooms change with the story (the prison yard once its gate is open), and each version is a room file of its
own (its *case*, picked by the script of the door the host took). A room that lacks the case you were in is
loaded in a case it has (before, the load waited for a file that does not exist: stuck in the load screen).

### The shared world

* World events (a lever, a switch, a door unlocked, zombies rising, a pickup that starts a
  cutscene) run once for both. In the same room both see it; a partner elsewhere finds the room
  changed when it gets there. Collision changes the event makes (a shutter that opens) end the
  same on both sides.
* Each instance runs the room it is in. While both are in the same room the host's enemies are
  authoritative and the joiner's copies follow them; in a room of its own each player's game runs
  that room's enemies itself. Re-entering the partner's room hands the enemies back to the host.
* Enemies killed while the joiner ran them stay dead when the host's enemies take over again
  (within the visit; once both left, the room is loaded the game's way again, as in single player).

### Puzzles

* A puzzle (a switch panel, a code terminal) that either of you solves is solved for both. A partner in the same room finds the room as the solution left it: the solved
  puzzle no longer opens, a door it unlocked is open, a wall it moved has moved. Before, the panel
  or terminal could open again for the partner and then show nothing (no question, no text) and
  keep the player there, and a door unlocked by the other player stayed locked until the room was
  entered again. The solution's cutscene plays for the player who solved it (and for the partner
  when the game runs it as a world event, see above).
* A solution stays when the other player enters the room again: the Antarctic base's self-destruct
  terminal (code entered by one of you) no longer resets, so the linear launcher next door starts
  for both.
* One of you at a time: while you use a puzzle, a terminal or anything else you examined (its
  messages, its screen), your partner cannot open the same one and reads "<name> is using this"
  at the top of the screen. It is free again as soon as you are back in control.
* Joining while the host is inside a puzzle: the joiner waits until the host is back in control
  (the game state is sent only then) and starts with the puzzle solved or not, as the host left it.

### A busy partner

A partner in a cutscene, a menu, the item box or the trade screen stays visible in its last pose
with a label above its head ("cutscene", "menu", "item box", "trading"). Enemies leave it alone
meanwhile, as the paused game does in single player. A busy host pauses its own game; the joiner's
enemies then run on its own game and target only the joiner.

### Cutscenes

A cutscene runs in each player's own game: the triggering player's, and the partner's when it is in
the same room (it replays the world event, see "The shared world"). The skip button (the game's
own, by default **Backspace** / **Back**: SELECT) skips it to its end, where the scene puts the player.

With **Skip Cutscenes Together** (lobby CUTSCENE SKIP *Together*, F3, ENHANCED SETTINGS CO-OP
"Skip Cutscenes Together"; ini `coop_skip_sync`, default on; the host's choice counts)
a cutscene both of you are watching in the same room is skipped only when **both** pressed skip:

* The first to press does not skip yet: "Waiting for <partner> to skip..." at the top of the
  screen. The partner hears a chime and reads "<name> wants to skip. Press <skip button> to skip."
* When the second presses, the host skips its scene and tells the joiner's game, which skips at
  once: both scenes end together, and each script puts its player where the scene leaves it (the
  same place on both screens). Without it, the one who skipped alone was already walking around
  while the other still watched.
* The request ends with the cutscene: a scene that ends by itself clears it.
* A partner who is not in a cutscene in your room (another room, already back in control) does not
  hold anything up: the button skips at once, as in single player. If the partner's scene ends
  while you wait, yours is skipped then.

*Separately* (`coop_skip_sync = 0`): the button skips your own scene at once, as before.

### Game over

What happens when one of you dies is the host's rule **GAME OVER** (lobby GAME RULES, F1 / ENHANCED
SETTINGS CO-OP "Game Over Rule"; ini `coop_gameover`, 0 *Each Player* (default), 1 *Both*). It can
change during a session; the joiner sees it under HOST RULES. The game's Retry (YES on "Retry?")
starts again from its **continue point**: the room scripts set it as you enter a room, saving sets it, and
a new or loaded game starts with one. **For playing the story together, *Both* is the better choice**: the
two of you always stay at the same point.

**Both**: one game over is both players'.

* When either of you dies, the partner gets the game over screen too: at once when it is in control; a
  partner in a menu, a cutscene or a door gets it when it is back in control. Being in another room makes
  no difference.
* Only the host is asked "Retry? Yes / No". The joiner's screen stays on YOU DIED until the host answers.
* **Yes**: both start again from the host's continue point, together in that room, each with the items
  it had at that point. What either of you collected since lies there again, used keys are back, and the
  world and both inventories match. A joiner that joined after that point gets the items it joined with.
  A joiner still busy (menu, cutscene) when the host answered skips its own game over screen and is taken
  there once it is back in control.
* **No**: the host goes to the title, the session ends, and the joiner's game goes to the title too. The
  same happens when the session ends while the joiner waits for the answer (the host quit or the
  connection was lost).

**Each player** (default, as before this rule): only the one who died gets the game over screen; the
partner plays on.

* **The joiner dies**: Yes takes it back into the host's game, into the host's room, with the items it
  had when it died and full health (its own continue point plays no part). The world is the host's and
  went on meanwhile, so nothing it took is lost or doubled. No: it goes to the title and the session ends
  (see "Leaving and disconnecting"); the host plays on alone.
* **The host dies**: Yes restarts the host's game from its continue point, with the host's items from
  then, as in single player. The host's game is the shared one, so the world goes back with it: the joiner
  is pulled into the host's room and finds the world as it was at that point, but keeps the items it has
  now. That means:
  * Supplies and weapons the joiner picked up since then lie there again; with shared loot one of you
    can take them a second time.
  * Doors unlocked and story events since then are undone: locked again, and played again.
  * Key items go to both players, and a key used up in a lock leaves both inventories. After the host's
    Retry the host has the keys of its continue point again, so the doors locked again can always be
    opened by the host; the joiner may lack such a key (the host opens the door, or trades the key).
  * No: the host goes to the title, the session ends, and the joiner plays on alone ("The host left the
    game").

### Name tags

The partner's name floats above its head while it is in your room and in view; out of view (behind
the camera or another angle) a marker at the screen edge points at it. Option "Partner Name Tag"
(F3, F1 / ENHANCED SETTINGS CO-OP; ini `coop_nametag`, default on). The tag scales with the window height and follows the head in enhanced (interpolated) mode.

### Ping display

**Show Ping** (F1 / ENHANCED SETTINGS CO-OP; ini `show_ping`, default on): while connected, "ping N ms"
next to the frame-rate counter (alone when the counter is off). F3 and the lobby status line show
it too. The ping is the real network round trip (receive timestamps), not rounded to game frames.

### Hiding IP addresses (streaming)

**Hide IP Addresses** (F1 / ENHANCED SETTINGS CO-OP; ini `hide_ip`, default off) is for streams and
screenshots. With it on, no IP address shows on screen, yours or the partner's:

* The lobby's status and info lines (ADDRESS) and F3 (status, "Local address", the join address
  next to **Copy Join Address**) show each address as
  `xxx.xxx.xxx.xxx`. Ports, the join code and the room code stay visible, e.g.
  `xxx.xxx.xxx.xxx:7140#ABCDEFGH`. A host name typed as the join address or co-op server is
  masked the same way.
* The address fields (lobby ADDRESS / CODE and SERVER, F3 Join address / Room code and Co-op Server)
  show stars, also while you type or paste. In F3, **Show Addresses** reveals them until
  the window closes; in the lobby turn the option off to see them.
* Only the display changes: **COPY JOIN ADDRESS** / **Copy Join Address** put the real address on the
  clipboard, and joining uses what you typed.

`recvx.log` (and the console) still contain the addresses, for troubleshooting: do not show the
console window on stream or post the log publicly. The crash report `recvx-crash.log` masks them.

### Sounds

You hear the partner's footsteps, its gun's own shot sound, its effects (muzzle flash, hit sparks,
blood, grenades, an enemy's vomit or acid aimed at it), and its voice (hits, grabs, death) in the
voice of the model it appears as. A partner holding the lighter lights the room around it.

## Loot and items

### Loot modes

The host's choice (lobby LOOT, F3 "Loot Sharing (Host)", F1 CO-OP "Loot Sharing", ini `coop_loot`; set
before the session):

| Mode | Pickups | Item box |
|---|---|---|
| **Shared** (0, default) | One pool: each item lies there once. Whoever takes it has it, the spot is gone for both and the partner gets nothing, so the two of you split the ammo, herbs and other supplies (hand them over on the trade screen). Exceptions: key items and the lighter go to both; weapons and ink ribbons with "Duplicate Weapons and Ribbons" (below). | One box for both. |
| **Individual** (1) | Each player finds and takes their own copy. | Each has their own box. |

Key items and story progress are shared in both modes. A key used up in a lock (the game removes
it) is removed from the partner's inventory too.

**Duplicate Weapons and Ribbons** (shared loot only; lobby GEAR *For both* / *Shared*, F3 and
ENHANCED SETTINGS CO-OP "Duplicate Weapons and Ribbons"; ini `coop_important`, default on; locked during a session): **weapons** (the guns,
launchers, bow gun, the combat knife when it lies somewhere) and **ink ribbons** lie there for each
player, as with individual loot: taking one does not give the partner a copy and does not take the
partner's; it stays lying in the partner's game until the partner takes it. Off (*Shared*), they are
pool items like ammunition: one copy, whoever takes it has it. Each player's own pickups of these
items are kept with that player's items: in its saves, and at the host in its saved partner
inventory (a guest joining again with *Saved with the host* finds the weapons it took gone and the
others still there). Pickups made outside a session are not known as such (see "Known limits").

What goes where:

| Item | Shared, important items *For both* (default) | Shared, important items *Shared* | Individual |
|---|---|---|---|
| Ammunition, herbs, sprays, other supplies and every other non-key item | the taker's alone; gone for both | the taker's alone; gone for both | each their own copy |
| Weapons (with the combat knife), ink ribbons | each their own copy | the taker's alone; gone for both | each their own copy |
| Key items (the game's key item kind: keys, emblems, plates, cards, jewels, ...) | both get it | both get it | both get it |
| The lighter | both | both | both |
| Files, maps | as before: the spot goes for both; maps are story progress | same | each their own; maps shared |
| Items an event hands over | see "Items from events" | | |

A partner with a full inventory leaves its copy of a key item lying in its room (it takes it later
like any pickup). If both of you take the same item at about the same moment, exactly one keeps it: the one whose
pickup reached the other first, and the host when both happened within one round trip. The other
one's copy leaves its inventory again as soon as it is back in control ("Your partner took <item>
first."). Each player's inventory is its own in every mode, so saves, saved partner
inventories, room changes and rejoins keep exactly what each player took.

### Items when joining

You join with your own inventory, never the host's. The joiner's choice (lobby ITEMS, F3 "Starting
Inventory", ini `coop_items`):

| Choice | Value | You get |
|---|---|---|
| *Saved with the host* (default) | -2 | What the host kept for you from your last visit; none: a new inventory, or, when you join again from the game you played in the host's session (the connection was lost, the host's game crashed, you left), the items you have now. In the host's lobby the row says "(available)", "(none)" or "(host: off)". |
| *New inventory* | -1 | The new-game inventory of the session's game mode. |
| A save | 0.. | The items of that save: save number - 1 (native save files), or memory card slot 0..14. |
| *Current Inventory* (F3 only) | -3 | Joining from your own running game keeps its items. |

### Shared item box

With shared loot both players use one item box. Both may use a box at the same time; if both take
the same entry, the host decides and the joiner's copy leaves its inventory again ("Your partner
took ... first."). If both put items into the last free places at once, the host's go in and the
joiner's item comes back: into its inventory, or, with that full too, onto the floor where the
joiner stands ("Inventory and item box full. Placed ... at your feet."). It lies there as the partner's event items do, only for
that player, kept with its game (saves too) until taken, and taking it gives back exactly that item
(its rounds, its ammunition kind). No item is lost either way. Saves keep the box as the player's
own game has it.

### Items from events

Some cutscenes end by handing the player an item (Claire's handgun in the courtyard, the Gold
Lugers, the lighter, key items such as the Music Box Plate or the jewels). Which of them the partner
gets too follows the loot mode:

| Item from an event | Shared, important items *For both* (default) | Shared, important items *Shared* | Individual |
|---|---|---|---|
| Key items, the lighter | both | both | both |
| Weapons, ink ribbons | both | the triggering player's alone | both |
| Ammunition, herbs and other supplies | the triggering player's alone | the triggering player's alone | the triggering player's alone |

An item that goes to both: a partner who watched the event in the same room gets it from the event
itself, as the triggering player. A partner who missed it gets the item **on the floor** of that
room, where the other player stood, as a normal pickup ("Take the Handgun? Yes / No") that glints
like other items on the floor. Only that partner sees it. It is kept with the partner's game (saved
with it, kept across disconnects) until taken. Nothing is given twice: a partner who was in the event,
or already holds the item (not for stackable items), gets nothing.

An item that is the triggering player's alone: nothing goes on the partner's floor; with shared
loot a partner who watched the event and got one too gives it back at once ("... belongs to your partner
(shared loot).").

### BioRand

With BioRand (only with `--dev`; [modding/README.md](modding/README.md#randomizer-biorand)) the host chooses in GAME RULES, row
**BIORAND** (the lobby's own rule, ini `biorand`; shown only when the host has a pack in `mods/biorand`): the game
the host starts from the lobby is then a BioRand game. A game the host plays already (started with the BIORAND
screen's START, or a loaded BioRand save) counts as it is. On, both play the host's pack:

* The partner needs the same pack: generated with the same seed and options (BioRand's seed string),
  so the same room files. The host sends its pack's seed string and its BioRand CLI's checksum
  (`EV_BRSEED`). A partner without the pack, or with another one, at the title (the lobby or the title menu)
  is asked on the BIORAND screen: "Generate the host's BioRand seed N? ..."
  (the memory note and the free space as GENERATE's). **Yes** generates the host's string as GENERATE does
  ([BIORAND_INGAME.md](BIORAND_INGAME.md) "S5b"): without a BioRand CLI it asks to download it first; when
  its CLI's checksum is not the host's it warns that the pack may differ and offers the newest CLI (No
  generates with its own). It then returns to where it came from and compares the pack again: the same one
  joins, another one leaves as below. The partner's own lobby rule and seed string (recvx.ini) stay.
  **No** (or cancel, or an error) leaves. Meanwhile the session stays (both keep sending their poses), the
  partner loads no game state (one sent meanwhile is asked for again afterwards), and the host's lobby shows
  "BioRand: asked to generate" or "BioRand: generating..." under the partner's model.
* Without that (in a game, without `--dev`, a host that sent no seed string, or after generating) a partner
  without the pack, or with another one, is refused when it joins (or when the host turns BIORAND on) with
  "Generate the host's BioRand seed N, or ask them to turn BioRand off." ("BioRand seed N differs. Match the
  host's options and BioRand version, ..." when its seed number is the host's); the host's status
  says "... left: BioRand games do not match.".
* The partner's game follows the host's choice for the session whatever its own setting (its own setting is
  back after the session); HOST RULES shows BIORAND while it is on.
* QUICK START is off and hidden (the pack has its own start inventory). A partner without a game of its own
  starts with the pack's start inventory, as the host's new game did (room 0/0's block, run by the game's own
  item actions on its inventory).
* Randomized pickups count by the item they give, not by their model: important items for both
  (`coop_item_taken`), the partner's event items shown in a room (`grant_slot`, `grant_model`).
* Loading a save from the lobby's START made with another BioRand state asks first (START again loads it).

Stack Rifle Ammo is on for both while BioRand is on. Off, the game is the normal one for both, packs or not.

### Quick start

The host's rule **QUICK START** (lobby GAME RULES, EXTRA EVENTS; F1 / ENHANCED SETTINGS CO-OP "Quick
Start"; ini `coop_quick_start`, 0 *Off* (default), 1 *On*) skips two chores at the start of a new game. With it
on, a new co-op game starts for both of you with:

* the **manual** already read: it is not in your bag, and it stays readable under FILE, as after reading it;
* the **combat knife** in your bag, where the manual was;
* the knife's table in the first room empty: its spot counts as taken.

Each player gets their own knife. Who sees the table empty follows the loot mode as for any weapon: with
"Duplicate Weapons and Ribbons" (the default) each player's own spot, otherwise the one spot taken for both.

It applies to new-game inventories only: the host's new game (the lobby's START with a new game, or a new game
while hosting), and a partner joining with a new inventory (ITEMS *New inventory*, or *Saved with the host* when
the host keeps none for it). Loading a save, continuing and joining with saved items change nothing. The joiner
sees the rule under HOST RULES. Default off: the game starts as the original does unless the host asks for it.
Outside a session the rule plays no part.

### Metal detector

The prison's metal detector room (where you have to put metal items in a security box before
walking through) is the host's rule **METAL DETECTOR** (lobby GAME RULES, EXTRA EVENTS; F1 / ENHANCED SETTINGS
CO-OP "Metal Detector"; ini `coop_skip_detector`, 1 *Skipped* (default), 0 *Original*). It
can change during a session; the joiner sees it under HOST RULES.

**Skipped**: both of you walk through with your weapons, the lighter and everything else. The
detector never goes off (no "!" stop, no alarm, the shutters stay open), and the two security
boxes stay shut. Coming back to the north box does not remind you to take your items. Nothing else
in the room changes: its doors and the way on are as after a clean pass, which in the original sets
nothing either.

A security box that still holds items (put there before the rule applied: an earlier game, a save,
playing alone before joining) opens as usual until you have taken everything out; then it stays shut
too. Nothing is moved for you, so nothing can get lost. An alarm that already went off (a save made
with the shutters down) is released at its switch, as in the original.

**Original**: as in single player. Outside a session the rule plays no part.

### Saving without ink ribbons

**Free Typewriter Saves** (lobby INK RIBBONS *Not needed*, F3 and
F1 / ENHANCED SETTINGS GAMEPLAY "Free Typewriter Saves"; ini `no_ink_ribbon`, default off): a typewriter
offers to save without an ink ribbon ("Use an ink ribbon? Yes" opens the save screen) and saving
uses none up; ribbons you carry stay. In single player it is your own setting; in co-op the host's
counts for both players.

## Trade screen

Standing next to your partner (same room, within about 3 m, both free: no menu, event, door or
grab), press **Trade (Co-op)**: **T** on the keyboard, **left stick click** on the gamepad
(rebindable; ini `key_trade` / `pad_trade`). The partner hears a chime and reads "<you> wants to
trade - press T / Left stick press" at the top of the screen and in your name tag. When it presses
its key, the trade screen opens for both. A request ends when you press again, walk apart, after
12 s without an answer, or when a grab, menu or event starts. Both asking at once also opens it.

**The screen**: your inventory on the left ("YOU"), the partner's on the right, in the game's look
(item icons, counts, "E" on the equipped weapon). The world behind is dimmed, not paused.

* **Offer**: move the cursor to a cell of your own (an empty one to only take) and confirm (or click
  it). Confirm it again (or CONFIRM) to be **ready**. The offer stays while you move the cursor,
  hover with the mouse or switch windows; only **Cancel** (the key, a right click or the CANCEL
  button) takes it back.
* **Request**: confirm a cell on the partner's side to ask for that item; again withdraws it. The
  partner sees the cell marked (WANTED) and "<you> requests: Handgun Bullets"; a click on that line
  offers it. A request moves nothing by itself.
* **Swap**: when both are ready on the same pair the items swap: "Traded X for Y.", "Gave
  X.", "Received Y.". A stack goes whole and joins a stack of the same kind. The screen stays open.
* **Cancel** steps back one thing at a time (your request, your offer, the partner's request:
  declined) and then leaves. **LEAVE**, walking apart or losing the partner closes it for both,
  nothing changed.

Rules: a **two-slot item** (rocket launcher, rifles, Gold Lugers, sub machine guns) needs two free
cells on the receiving side after its own item left ("No room for Assault Rifle. Free two
inventory spaces."). An **equipped weapon** cannot be traded ("Unequip your Handgun in the
inventory first."). Key items trade like any item. The **lighter** lives in its own slot and is not
offered.

While you are in the screen you stand still and the partner treats you as busy. With **Pause
Enemies During Trades** (lobby TRADING, F3, F1 CO-OP; ini `coop_trade_freeze`,
default on) the room's enemies stand still while both screens are open; effects already flying keep
flying. A trade cannot start while either player is held by an enemy ("Finish menus, scenes or enemy
grabs before trading. Both players must be free.").

Input: as in the inventory (d-pad/stick or keys move, confirm, cancel; the status button / Tab in
third person jumps to the other side). Mouse: hover moves the cursor, a click on one of your cells
offers it, on the partner's requests it.

An item is never lost or doubled: if the connection drops mid-swap, the next session settles it
(see "Trade transactions" in part 2); an unanswered offer returns to your inventory (or item box)
when the session ends.

### Trading from the F3 window

F3's Trade section still works: choose an item (not the equipped weapon or a two-slot item) and a
count (stacks can be split), **Offer**; the partner sees **Accept** / **Decline**. The item is held
while the offer is open and comes back if it is declined or taken back (**Withdraw Offer**).

## Saved partner inventories

The host keeps each partner's items (inventory, item box, pending event items, weapons, health,
status) per player, with its saves:

* Every installation has a random **player id** and a **secret**, made once and kept in
  `recvx.ini` (`coop_player_id`, `coop_player_key`). Keep them and do not give the key away. Separate
  save folders are separate players.
* While you play in the host's game, your items are sent to the host as they change (at most once a
  second). When the host saves, the records are written next to the save (removed when there are
  none): `<save dir>/saves/NNN.coop-partners.bin` for save file NNN, or
  `<save dir>/coop-partners-nn.bin` for memory card slot nn. A game started from that save has them again; a new game has none.
  Up to 16 players per playthrough (the oldest goes first).
* Joining with *Saved with the host*: "Restored your saved items from the host"; none: "No saved
  items at the host - starting with a new inventory". A new inventory or a save chosen explicitly
  wins over the record and becomes the new record. With shared loot the item box stays the host's.
* Setting **Remember Partner Items** (F1 / ENHANCED SETTINGS CO-OP, F3; ini `coop_remember`,
  default on). Off: "The host keeps no saved items - ...".
* Trust: the records are the host's data on the host's disk; the joiner trusts its host (as it
  must anyway). Someone who knows your player id and the host's join code could claim your saved
  items at that host; taking over a running session needs the secret.

## Rejoin after a crash

If your game crashes or is killed, start it again and join: you get your place and your saved items
back at once ("Player joined again ... the stale session is replaced" on the host), whatever ITEMS
says. Directly, the host recognises you by your player id plus a tag only your secret can make;
through a server the server gives you your old place the same way. A name alone, or an id without
the tag, takes over nothing.

If the host's game crashes, the host starts it again and loads its last save; you join again from
your game (F3 Join, or from the title). You get the items the host saved with that save, as the world
is that save's again. If that save has none of yours (saved before you joined), you keep the items you
have ("No saved items at the host - you keep your current items"); before, you started again with the
new-game inventory of the character (in Chris's part his starting items).

## Remove partner

Host: lobby **REMOVE PARTNER** or F3 **Remove Partner** (while a partner is connected or
not responding). It ends the session with that partner and frees its place; a partner still there
reads "The host removed you. You can join again.". Through a server the room
and its code stay. The same code works again at once.

## Leaving and disconnecting

Every way out ends the session on both sides; hosting or joining again works at once (same port too).

| Way out | This side | The partner |
|---|---|---|
| Leaving the lobby (BACK from step 1, cancel, right click; not START THE GAME) | session stopped | "The host closed the lobby" / "<name> left" |
| LEAVE, CLOSE THE LOBBY, F3 Disconnect | session stopped | as below |
| Quitting, closing the window | goodbye sent at exit | as below |
| Back at the title from a game of this session (game over "No", OPTION's RETURN TO TITLE) | "Returned to title. Co-op session ended." | as below; with GAME OVER *Both*, a joiner at or heading for the shared game over goes to the title too |
| Goodbye from the host | | joiner: "The host closed the lobby" / "The host left the game"; in game it plays on alone |
| Goodbye from the joiner | | host: "<name> left - Hosting on port 7140. Waiting for a partner."; in game it plays on alone |
| Crash, network gone | | after 3 s "... is not responding"; after 15 s joiner: "Lost the connection to the host", host: back to waiting ("The partner timed out") |

Grabs, trades and held items of a partner that leaves are resolved: a zombie the joiner was being
grabbed by is released on the host, an unanswered trade item returns to its owner.

## Enemy options in co-op

The host's enemy options (EXTRA ENEMIES, LAYOUT, RANDOMIZER, VARIETY, EVENT ENEMIES, SEED, and the
CUTSCENE CAST page; F1 / ENHANCED SETTINGS GAMEPLAY, lobby GAME RULES, RANDOMIZER) count for both: both
players see the same cutscene cast, picked from the host's seed (the joiner's own cast settings do not
count while it is joined). The joiner's lobby shows them (read-only) before the game
starts; changes in game take effect at the next room load on both sides. Enemies the host adds
appear in the joiner's game in the same slots. Details: [ENEMIES.md](ENEMIES.md).

## You appear as

"Character Model" (lobby MODEL, F3, F1 / ENHANCED SETTINGS APPEARANCE; ini `coop_char`) chooses your model: the
character you play, the character you play in its cutscene model ("Current Character (Cutscene)": sent to
the partner as that cutscene model, no protocol change), Claire, Chris, Steve, Wesker, Claire's second
costume, or a story-cast, creature or zombie model. Your partner sees you as that model, and so do you (also in single player). The
partner's voice follows its model. Both players need the same build. "Custom Cutscene Model"
(`appear_in_cutscenes`) counts for your own screen: off, your cutscenes (also one replayed from
the partner's event) show the real Claire or Chris. Models, voices and cutscene behaviour:
[ACTORS.md](ACTORS.md).

## Local co-op on one PC

Two copies of the game side by side, one hosting, one joining over localhost:

1. Start each with its own save folder (its own `recvx.ini`, saves and player id):
   `recvx --save-dir save1 game.iso` and `recvx --save-dir save2 game.iso`.
2. One hosts (MULTIPLAYER or F3, port e.g. 7140; or `--coop-host 7140`), the other joins
   `127.0.0.1:7140#CODE` with the host's join code (or `--coop-join 127.0.0.1:7140#CODE`).
3. Per window, F1 > Controls > **Input Devices** (ini `input_source`): e.g. window 1 *Keyboard and
   Mouse*, window 2 *Gamepad Only*; or both *Gamepad Only* with **Gamepad** (`input_pad`) 1 and 2.
   *Gamepad Only* keeps working while another window has focus; keyboard/mouse plays in the window
   you last clicked. Drag each window to its monitor (F11 fullscreen works per window).

More on input sources: [USER_GUIDE.md](USER_GUIDE.md).

## Settings

CO-OP page of OPTION > ENHANCED SETTINGS and F1 (plus F3 and the lobby). Settings marked "host"
(under HOST RULES) count only on the host; "locked" ones cannot change during a session.

| Label | ini key | Values (default) |
|---|---|---|
| Character Model (APPEARANCE page) | `coop_char` | model index (0: Current Character; 4095: Current Character (Cutscene)) |
| Partner Name Tag | `coop_nametag` | 0/1 (1) |
| Show Ping | `show_ping` | 0/1 (1) |
| Hide IP Addresses | `hide_ip` | 0/1 (0); see "Hiding IP addresses" |
| Follow Host (joining player) | `coop_follow` | 0/1 (1) |
| Loot Sharing (host), locked | `coop_loot` | 0 Shared, 1 Individual (0) |
| Duplicate Weapons and Ribbons (host), locked | `coop_important` | 0/1 (1); shared loot only |
| Skip Cutscenes Together (host) | `coop_skip_sync` | 0/1 (1) |
| Game Over Rule (host) | `coop_gameover` | 0 Each Player, 1 Both (0); see "Game over" |
| Metal Detector (host) | `coop_skip_detector` | 0 Original, 1 Skipped (1); see "Metal detector" |
| Quick Start (host) | `coop_quick_start` | 0/1 (0); see "Quick start" |
| Pause Enemies During Trades (host) | `coop_trade_freeze` | 0/1 (1) |
| Remember Partner Items (host) | `coop_remember` | 0/1 (1) |
| Connection, locked | `coop_conn` | 0 Automatic, 1 Direct, 2 Server Relay (0) |
| Host Port, locked | `coop_port` | 1..65535 (7140) |
| Automatic Port Forwarding, locked | `coop_upnp` | 0/1 (1) |
| Free Typewriter Saves (GAMEPLAY page; co-op: host) | `no_ink_ribbon` | 0/1 (0) |
| Extra Enemies (GAMEPLAY page; co-op: host) | `enemy_extra` | 0..3 (0); see [ENEMIES.md](ENEMIES.md) |

Kept in `recvx.ini` without a menu row of their own: `coop_join` (last address or room code),
`coop_server`, `coop_name`, `coop_items` (-3.., a save as in the ITEMS table), `coop_start` (0..2
new game normal/easy/very easy, 3 + n save n: native number - 1 or card slot), `coop_player_id`, `coop_player_key`. The room password is never saved.

---

# Part 2: Reference

## Architecture

### Code

| File | Role |
|---|---|
| `pc/glue/coop_glue.c` | The session: pose, events, snapshots, enemy authority, grabs, world events, loot, trade, saved inventories. Game thread. |
| `pc/glue/coop_testaids.c` | Test aids of `coop_frame` (headless runs). |
| `pc/glue/lobby_glue.c` | Title menu entry and lobby screen (game thread, the title task). |
| `pc/glue/trade_glue.c` | Trade screen drawing and input. |
| `pc/src/net/coop_proto.c/.h` | Wire format and every receive check; no game state (tested by `pc/tests/test_coop.c`). |
| `pc/src/net/net.c/.h` | Non-blocking IPv4 UDP socket, receive timestamps, threaded DNS. |
| `pc/src/net/portmap.c` | UPnP (miniupnpc) / NAT-PMP (libnatpmp) port mapping on a worker thread. |
| `pc/src/net/rendezvous.c`, `rdv_proto.h` | Co-op server client: rooms, hole punching, relay, encryption ([SERVER.md](SERVER.md)). |
| `pc/src/host/coop.h` | `CoopState g_coop` (settings, requests, published status). |
| `pc/src/host/overlay.cpp` | F3 window, name tags, ping. |
| `pc/src/host/lobby_io.c` | Lobby text input, clipboard, save listing and partners files of the active save system (`lobby.h`). |

There is no network thread. `coop_frame()` runs once per game frame at the frame hand-off
(`pc/src/ps2/libgraph.c`, the 30 Hz tick boundary on the game thread): it takes overlay requests,
receives, applies, runs the session logic and sends. Other threads read the state only through
`coop_status_get()` (one `CoopStatus` copied whole under a lock per frame).

Decomp hooks (all `#ifdef TARGET_PC` + `if (coop_on)`): `game.c bhMainSequence` after
`bhControlPlayer` (`coop_control_remote`: our pose, the partner's animation) and after the frame's
drawing (trade screen), `game.c bhAllDrawModel` (`coop_draw_remote`), `eneset.c bhControlEnemy`
around `bhJumpEnemy` (`coop_enemy_begin/end`), `event.c bhControlEvent` (`coop_scd1` in place of
`bhScenarioCheck(scd1)`), `effect.c` / `effsub3.c` (effect mirroring), `sub1.c GetItem`
(`coop_item_granted`), `event.c bhItmCk` (`coop_item_taken`: a pickup, for important items),
`pad.c bhSetPad` (trade input; `coop_skip_pad`: the skip button), `sdfunc.c` (the partner's voice bank),
`ps2_NaTextureFunction.c njReleaseTextureAll` (`coop_textures_released`), `system.c
bhSysCallMonitor` before a character switch releases the player's textures (`coop_player_unload`,
also without co-op), and the title menu in
`adv.c` / `system.c` (the MULTIPLAYER row and the lobby mode), `event.c bhControlEvent` before scd1
(`pc_detector_scd1`) and `event.c bhPlItemCheck` (`pc_detector_skipped`) for the metal detector
(`detector_glue.c`, see below), `system.c bhSysCallMonitor` after a new game's `AllItemInit` (`pc_quick_start`,
see "Quick start" below). Not co-op only: `event.c
bhPlItemCheck` and `ps2_McSaveFile.c mcWriteStartSaveFile` ask `pc_no_ink_ribbon()` (the setting,
in co-op the host's rule) whether a typewriter needs and uses an ink ribbon.

### How the game represents the player

The game has one player: `BH_PWORK ply; BH_PWORK* plp = &ply;` with thousands of `plp`
references (player code, enemies, events, camera cuts, doors). Enemies have no target field; they
read `plp`. Per-character state lives in `sys` (`ply_id`, inventory `itm[ply_id*16]`, item boxes in
`itm[64..223]`); pad input only via `sys->pad_*`.

Co-op uses a **context swap** in glue code instead of touching those references: it saves and sets
`plp`, `sys->ply_id`, the player model and motion buffers and `freemem`, then calls the stock
functions for player 2. The partner (`ply2`) has static buffers in the game's address window and is
loaded with the stock loaders (`bhReadPlayerData`, `bhReadWeaponData`) from its own part file, so
its motion numbers always index the set they came from; a different mesh ("Character Model") is
loaded over it.

### Netcode model

Hybrid, not lockstep (libc `rand` in hundreds of enemy calls and menus that pause the simulation
rule lockstep out):

* **Each player is authoritative for its own player**: movement, camera, doors, collision run
  unchanged; the pose goes to the partner every frame.
* **The host owns the session**: enemies of the room both are in, the game state a joiner loads,
  the session options, conflicting flag changes, the shared item box, trade commits.
* **Room authority is per instance**: in a room of its own each game runs that room's enemies.

### Pose and the partner's model

Every datagram starts with the sender's pose: room, position, angles, motion, frame, blend, weapon,
hp, modes, motion set (own, room, or an enemy's while grabbed), busy state, voice counter, hit/held
flags, aim pitch and floor. The receiver plays the motion on the partner's model with `bhSetMotion` (add 0)
and `bhCalcModel` and draws it with `bhPutModel` when both are in game in the same stage/room and room case, both
players are visible and no door transition or event runs. **Aim pitch**: the fixed cameras' aim up/down
is a motion of its own and arrives with the motion number; third-person free aim keeps the straight
motion and bends spine and chest (`tps_calc_model`, [THIRDPERSON.md](THIRDPERSON.md#free-aim-pose)), so
the sender puts that bend's pitch (`tps_aim_pitch`) into `Pose.pflg`'s spare bits (6 bits, about 2
degrees a step) and the receiver eases towards it (half the way per frame) and bends the partner's model
the same way (`tps_bend` after its `bhCalcModel`, and on its gender-matched copy and "Character Model"
model through `actor_source`): arms, head, both weapons and the muzzle flash follow. Hair (Claire) and Wesker's piece get
private object work. The partner's lighter gets the arm IK, its own flame and room light slot 3 (a
slot no game code uses), so both can hold one. Footsteps and shots play from flags in the pose; the
partner's weapon's arms sound bank is loaded beside ours (programs `0x40 + n`).

### Enemies: host authority and puppets

* **Targeting (host)**: each enemy goes for one player at a time (`coop_pick_target`, re-decided at
  every step; a linked part goes for its parent's). Before the rule: an enemy linked to a player
  (`lkwkp`: the moth's larva, en27, rides on the player it was laid on and plays that player's motions)
  goes for that player only, from its Init (`larva_laid`: the step that laid it names the player); and
  the enemy whose step set the host's busy flag (`flg & 4`), its parts and its type's leader (moth,
  Black Widow) keep the host until the flag clears (`hold`, below). In order:
  1. **One victim per attack**: a zombie holding, or an enemy whose motions its victim plays, keeps
     the victim. In its grab/bite/throw state (`mode0` 2) it keeps a victim within 3 m. Its hit and
     death states keep nobody.
  2. A **busy** player (being hit or held, dying, or kept away by the loop guard) is never chosen; a
     busy target is left for a free player within 4 m (else the enemy waits its turn).
  3. **Right in front**: a free player within 1.5 m inside the enemy's front quarter stays its
     target (unless the other is at less than half its distance), or becomes it (unless the target
     is at less than two thirds of its distance) - no enemy walks into a player without going for it.
  4. The **nearer** player, by a margin (1.5x squared distance), so near-equal distances do not flip
     it. A player on another floor than the enemy (which attacks only on its own) counts as twice
     as far.

  For the partner, `plp` points at `ply2` for that enemy's step; `ply2` carries the partner's
  position, angles, hp, flags, floor and a hit capsule from its model. For the step `plp` also
  carries the aim height (`at_flg`) of whoever shot the enemy: some enemies take no damage from a
  shot aimed straight (a lying zombie, `en01.c bhEne01_DmgCheck`) and read it from `plp`, so the
  partner's shots bring theirs in `EV_ENE_HIT`, and the stand-in, which has no aim of its own,
  carries the host's.
* **Loop guard (host)**: an enemy that starts its third attack (hit reaction or grab) on the same
  player within 90 frames is in a loop - nothing in the game attacks that fast - and leaves that
  player alone for 60 frames (logged: `loop guard: enemy N ... started 3 attacks`).
* **Damage to the partner**: what the enemy step did to `ply2` is diffed. An hp loss or a hit
  reaction becomes `EV_PLY_HIT`; everything else is undone. Grabs other than the zombie's arrive as
  a hit. The dead flag (`flg` 2) an enemy sets on the stand-in (Alexia's embrace, the Tyrant's last
  blow; in single player the game over) is the partner's death: the hit takes all its health. For
  the round trip until the partner's pose shows the hit (`HIT_BUSY`, 15 frames) the stand-in counts as
  busy (`flg` 4), as the hit player does: a blow lasting several frames hits once.
* **Boss holds (host)**: some holds play a sequence on their victim and end only when its motion has
  run: the Tyrant's blow takes the player over (mode 5, `en19.c bhEne19_PlyMoveMain`), Alexia's
  first form burns it (mode 6, `en12.c bhEne12_NG00`), the Gulp Worm swallows and throws it
  (`en02.c bhEne02_PlayerControl`). On the stand-in, put back after every step, they never ended (the
  Tyrant held his victim for good and harmed nobody again; Alexia stood still). Such a hold of the
  partner runs on a copy of the stand-in (`ghost`, one at a time), its motion advanced as the player's
  own, until the enemy lets go (or dies, or the room is left); the enemy keeps the partner as its
  target meanwhile, and what it does to the copy goes out as hits. Alexia's embrace has no way out (in
  single player the game over follows): after it she takes up the fight again. The Tyrant attacks in
  his move mode, so he keeps a victim playing his motions (and the host is not left in them when he
  would turn to the partner); the partner's death does not end his blows (`en19.c`). Nosferatu's drive
  of a struck or seized player (flung, landing, standing up) moves the victim's body: on the stand-in
  he does not drive it, keeps the partner as his target until it is up again and then lets go; a
  seized partner takes the throw's landing (80) as a hit (`en15.c`).
* **Puppets (joiner)**: an enemy runs its own AI once for its initialising step; after that, while
  the host is in the same room, it takes the host's snapshots (explicit fields: flags, position,
  angles, modes, motion, frame, blend, hp, hit capsule; pointers as indices). Linked parts with a
  model of their own are not animated. Snapshots are not used during door transitions. Enemies
  driven by the joiner's own event run its local AI during it. A puppet morphs (`mdflg` 2) only with
  the two shapes its own code picks (`obj_a`, `obj_b`); Alexia's tentacles have none on the guest
  and are drawn unmorphed (before, the guest crashed in her second form's fight).
* **Hits on puppets (joiner -> host)**: the weapon code's output on a puppet is taken at the
  hand-off, cleared locally and sent as `EV_ENE_HIT`; the host's enemy applies it once in its own
  damage code. A hit arriving after the host's room changed is dropped.
* **Zombie grabs** are handed to the grabbed joiner's game: the host sends the enemy's state as
  `EV_ESTATE` chunks plus `EV_GRAB`; the joiner runs the grab with its real player (bites, shake
  off, death) and sends snapshots of the slot; `EV_UNGRAB` returns it (with state, or without and
  the host releases the zombie). A grab the joiner cannot take (its state does not fit, or the
  position it puts the player at lies behind a wall from where the player stands) is refused and
  the zombie released. The joiner's player takes the grab's modes, motion and position from the
  host's proxy, and of its flags the ones the enemy changed there, plus the planted foot
  (`flg` 0x40000, `bhFixPosition`) the grab's start lets go of: the proxy never has it, and an
  aimed weapon or the knife left ours set, which moved the player off the zombie every frame of
  the grab. The host takes a slot back after 300 joiner frames without snapshots, and when the partner
  leaves. A grab started while the joiner's enemy ran its own AI keeps running locally until it
  lets go.
* **Safety nets**: a player in a grab state with no enemy holding it for 15 frames is let go (150
  frames when the holder is a dying zombie still holding it: a crawler's leg grab ends with the
  player stomping it dead and lets go at the end of the stomp); a zombie let go this way, or by any
  other release, is put back to standing only when alive - a dead one stays dead; after
  a grab, a player more than 2.5 m from its last free position in that room is put back; a watchdog
  logs any living enemy whose state has not changed for 5 s and, in co-op or rooms with added
  enemies, releases a zombie stuck in a grab state.
* **The busy flag** (`hold`, both sides): an enemy's attack or grab sets the player's `flg & 4` (with
  0x10000) and clears it when it ends; meanwhile the inventory, map, examining, doors and pickups wait
  (`bhCheckSubTask`, `bhControlPlayer`), the player still walks. In co-op the enemy that set it could stop
  running against that player first, and the flag stayed until a room change (`bhInitRoomChangePlayer`,
  hence "a teleport fixes it"): the host's retargeting gave the busy player's enemy to the free partner (a
  zombie's grab start, its counter-grab after a knife hit; the larva's throw-off), and the joiner's own
  enemies, running while the host was in a menu or elsewhere, became puppets again mid-attack. Now the
  enemy whose step set the flag (`grab_in` for a handed-over grab) is its holder: the host targets our
  player with it, the joiner keeps running it here (`runs_here`, logged "holds our player: it runs here
  until it lets go"), and a larva linked to the joiner's player too. **Watchdog** (`hold_watch`): the flag
  set while the player is in normal control (or held on its own motions) with no step of the holder's
  family on that player for 30 frames, for 60 frames in a row (not in a door, event, cinema or the trade
  freeze), is cleared as the enemies clear it (a grab on its own motions is let go) and logged: "watchdog:
  our player was left busy ... set by enemy N (id, gone/dead/alive, ...): cleared".
* **Kills while the joiner ran the room** (`EV_GONE`): when the host's enemies take over, the joiner
  names its dead enemies and the host hides its live copies of the same type and version.

### World events and records

The room's per-frame script `scd1` has blocks gated by a world flag (`ev_flg`, `ky_flg`, `ed_flg`,
`mp_flg`) that the block flips (by `Set`, or by an event it starts). Their other conditions (on a
floor or trigger, "Yes" chosen, an item used) hold only where the player did it.

* At room entry the script is swept for such blocks. Each instance runs `scd1` itself, recording
  every instruction; a block that fired (gate had its "before" value and a flip ran) sends its
  executed actions as `EV_TRIG` (offsets into the room script, gate, script signature).
* The partner, in the same room and script, replays them after its own `scd1` of the next frame,
  only while its gate still has the "before" value. So each instance runs each block once, with no
  arbiter and no round trip. The receiving stream waits for the replay so later flags cannot arrive
  first.
* A replay runs only as one of the receiver's own world blocks: same region, gate flag and value;
  every action an instruction start inside the block's then-part, in order. Actions that act on the
  triggering player's own game do not run (door calls, retry points, weapon/hand changes, player
  control). A block whose actions lead to a door event is not replayed (flags only).
* **World records** (`EV_ATR`): the triggering side keeps a window open until the events the block
  started have ended, then sends every room record that changed (walls, triggers, floors, object
  placement in slots 4..31). The receiver takes the on/off flag and placement; type, attr and
  parameters only as one of its own script's `WalAtariSet2` / `EtcAtariSet2` / `FlrAtariSet2` writes
  them for that record; never id or floor. Records are synced only after a shared world event.
* A partner in another room gets the flags only; `scd0` builds the room from them when it enters.
* **A scene of our own is not cut short** (`gate_hold`, `coop_scd1`): many world events take two steps
  of the script. One enters a scene (cinema mode) and sets a room flag; a later block, gated by the same
  world flag, ends it (its events, or a "No" branch). The turntable (7/11): the key's event asks "Move the
  turntable?" and the block that moves it tests `ev_flg` 350 clear; the guillotine gate (0/8) and the
  lift corridor (7/8): the item used in the item screen enters the scene, the block that acts once the
  screen has closed tests the gate. When the partner's change of that flag arrived in between (it moved
  the turntable while our question was open, or its game ran the lift's block on our flag and our replay
  of it cleared the flag), the closing block never ran and the player stayed in the scene (frozen). While
  our player is in cinema mode, the partner's changes to this room's world event gates are held and
  applied when the scene ends or the room changes (a change of our own to a held bit is newer and wins),
  and the replay of its blocks waits too (the stream with it, 10 s at most). Each game then ends its own
  scene as in single player (a "Yes" to the turntable's question takes that player down too).
* **The room as the partner's flags have it** (`rebuild`, `rb_mark`): a puzzle is often solved by steps
  that are not a world event block: the answer lives in room flags and its event sets the world flag
  (the Gold Luger room's switch panel, 1/05: "decide" sets `rm_flg` 8, the block on it starts event 31,
  which sets `ev_flg` 15), or a terminal sets it (`sp_evt.c` op 0xF0: the main hall's NTC0394 sets
  `ev_flg` 4, then a room-flag block's event turns trigger 12 into the door). A partner in the room got
  the flags only and kept the room as it was: the panel's trigger still opened the panel, whose answer
  blocks test `ev_flg` 15 clear, so the player stayed in it with a question nothing answers (no text);
  the door stayed locked. Now the world flags (`ev_flg`, `ky_flg`) the partner changes while we are in
  the room are noted, and once we are out of cinema mode and no world event window is open, the room's
  entry script `scd0` is walked as the interpreter takes it: its blocks' tests (`Ck`, `CmpB`, `CmpW`; a
  block with another test is left alone) choose the parts, and in the blocks that test a noted flag its
  record writes (`WalAtariSet`, `EtcAtariSet`, `FlrAtariSet` and their `...2` forms) run again, as
  entering would run them. Nothing else of the entry script runs (models, enemies, items, sounds, room
  flags). The partner does not see the solving player's cutscene (as before: it was not a world event).
* **An entry script that would undo a solution** (`coop_scd0`, `keep_solved`, `event.c bhInitEvent`): 9/31's
  entry script clears `ev_flg` 5 on every entry. The self-destruct terminal's code (VERONICA, `sp_evt.c`)
  sets it, and 9/30 starts the launch when entered with it set; a single player walks from the terminal
  straight to 9/30. In co-op a partner entering 9/31 after the code (or a game loaded there) cleared it
  for both, and with the card used up 9/30 never launched. The flag block is noted before the entry
  script runs; a listed flag (`keep_solved`: 9/31, `ev_flg` 5) that was set and the script cleared is set
  again at once, so the clear never reaches the partner. Of the other entry-script clears of flags other
  rooms test (a census of all rooms), none undoes a solution: they re-arm transient state the room
  sets again (lift floors 7/06 3/21 3/09 4/04 8/04, the piano's music 1/00..1/08, ambient voices
  1/00 1/09 2/01, 6/05's entry cutscene, 9/28's fight, cutscene rooms).
* **One player at a trigger** (`coop_examine`, `Pose.use`): `hitchk.c bhCheckExmAtari` asks before it takes
  a type 3 trigger (examine: a message, a close-up, the scripts' "at trigger" test). Our pose carries the
  trigger we examined + 1 while the game holds our player (`st_flg` 4: a message, a close-up, cinema) or we
  are out of the game screen (a terminal), until 10 frames after we are free; the partner in the same room
  and case does not take that trigger meanwhile ("<name> is using this", top of the screen, 3 s). `st_flg` 4
  is also set every frame our busy flag (`flg & 4`) is: a busy flag left behind kept the trigger "in use"
  for the partner too; the busy flag watchdog (`hold_watch`, "Enemies") frees both.
* **A replayed event opens no terminal** (`coop_replayed_event`, `event.c bhEvtComSet`): a world event's
  events run on the partner too, so the office computer's power-on (1/09, gate `ev_flg` 164: movie, then
  `EvtComSet 7`) put the partner into the password screen. An event of the partner's block replayed here
  (its task level in the replay window) skips the terminal; the rest of the event runs.

### Flags, pickups, game state

* **Flags** (`EV_FLAGS`): the 120-word block `ev_flg it_flg ic_flg ed_flg ky_flg mp_flg` is diffed
  each in-game frame; changed words go out as set and cleared bits. Host authority: the host drops
  a joiner's bits in words it changed itself since the joiner's base. Individual loot leaves the item
  words (`it_flg`, `ic_flg`) out. Not synced: `rm_flg st_flg sp_flg`.
* **Pickups** (`EV_PICKUP`): an item flag set and the examined spot's object hidden within 30 frames
  is a pickup; the partner hides the object in the same room (the flag came with `EV_FLAGS`). The
  event's `item` is what the partner gets too (0: nothing): with shared loot only the kinds of
  `for_both` (`coop_glue.c`: key items, `itemdata[].type & 0x40`; the lighter, item 55; with
  `OPT_IMPORTANT` the important kinds, which are not sent at all, below), which the partner adds with
  `GetItem` (stacking, capacity check; no room: the flag is cleared again there and the item stays
  lying). Every other pickup is the taker's alone (one pool). Individual loot: key items only.
  `EV_LOST` removes a key the partner used up in a lock.
* **Both took one pool item** (shared loot): exactly one keeps it.
  * The partner's flag came while our "Will you take it?" was open: the game adds the item and
    finds the flag set, so it sets none and hides nothing. `bhItmCk` reports that
    (`coop_item_late`, decomp hook) and ours goes back: the partner was first.
  * Both games set the flag within a round trip: the host's pickup stands. Host: the `it_flg` bits
    of a joiner's `EV_FLAGS` that `mask_host_changes` dropped (the host took that spot since the
    joiner's base), or that the host had set but not sent yet (it was still in the status screen),
    are `raced`; the joiner's `EV_PICKUP` of such a spot gives the host nothing and goes back as
    `EV_PICKUP` with `undo` 1 and the spot's bit, and the joiner gives back the item it remembers
    for that spot (`mine`). Joiner: the host's flag for a spot it had set but not sent yet means
    the same at once.
  * Giving back (`give_back`) waits until that player is in control (not in a menu or the status
    screen) and takes out the amount a pickup of that item adds (`getbulletmax`); "the partner took
    <item> first". Key items are not given back (both get them anyway).
* **Duplicate Weapons and Ribbons** (shared loot, `OPT_IMPORTANT`): the kinds in `important_kinds[]`
  (`coop_glue.c`: weapons, `itemdata[].type & 0x1`; ink ribbons, item 31). `bhItmCk` reports each pickup
  (`coop_item_taken`: its `it_flg` bit and trigger); one of those kinds marks the bit in
  `sys->itm[256..271]` ("important spots", save-region words no game code uses: the item boxes end at
  255). Those bits of `it_flg` / `ic_flg` are left out of `EV_FLAGS` both ways (sending and applying),
  and the pickup sends no `EV_PICKUP`: each game keeps its own. `sys->itm[272..287]` mirrors the
  player's own bits there every frame, so they travel with its items: saves, the host's game state,
  `EV_OWN` and the saved partner records. A guest loading the host's game state (`own_restore`)
  takes the union of both games' important spots and its own bits for them (`imp_restore`); the
  host puts its spots into a record it sends (`blob_record`). A new inventory starts with none taken.
* **Game state** (`EV_BLOB`): the host's save region (`sys->version..save_end`, 0x834 bytes) in
  64-byte chunks, when a joiner appears, when the host's game (re)starts, at a story character
  switch, and when the joiner asks. The joiner loads it through `bhPopGameData` (the continue path),
  then puts its own items, box (individual loot), health, weapon and status back (`own_restore`).
* **Regroups**: the host counts story cutscenes (15 s or longer) and character switches in the pose;
  a new count pulls the joiner (`bhSetDoorDemo` to the host's entry point).
* **Event items** (`EV_GRANT`): `GetItem` reports an add made by an event without asking; an item
  for the partner too (`grant_for_partner`: individual loot weapons, the lighter and key items;
  shared loot `for_both`) goes with count, room and position to the partner (the receiver checks
  the same rule), which keeps it pending in unused save-region words
  (`sys->itm` 320..359, 8 entries) and shows it on the floor of that room with a pickup trigger. A
  room without a record of the item gets the item's display model for the visit.
* **Item box** (`EV_BOX`, `EV_BOXFIX`): with shared loot, what was taken out and put in when the box
  screen closes; the host decides conflicts and sends back what the joiner could not have. An item
  the joiner took that the host had not (taken there first) leaves the joiner's inventory again
  (`inv_drop`). One it put in that the host's box had no room for (`box_back`) leaves the joiner's copy
  of the box and goes into its inventory, else into a free pending entry of the event items
  (`grant_return`: `GRANT_RET` and the word's top byte in the high half of the entry's x word) at its
  feet; the pickup restores the inventory as it was and adds that word (`pc_inv_add`), not what a
  pickup of the item gives, and the "already have one" rule of event items does not apply. Shown with
  the room's record of the item if hidden, else its display model (also beside a visible pickup of
  the same item). With all 8 entries in use it stays in the joiner's copy of the box. A host's put the
  joiner's copy has no room for stays the host's (its box is the one both use).

### Effects and voices

* **Effects** (`EV_FX`): `eff[]` is compared before and after each host enemy step, our player's
  step and our projectile's step; new entries go out once (number, type, flags, position, link,
  scale, angles, references as enemy slot / sender / receiver). Particle bursts and sparks go as the
  call. Mirrored projectiles show only; the owner's game does the damage. Enemy attack effects hurt
  the receiver's player where they land. Budget: at most 8 per frame and none while 48 events wait
  for acknowledgement (cosmetic, dropped rather than queued). The Bandersnatch's stretched arm goes
  as `FX_ARM`.
* **Which effects** (protocol 28, `coop_proto.c` `fx_rules`): only those a sender's window can start
  (a survey of every creation site in the decomp): 2 5 10 11 13 15 16 23 24 108 130 (blood, splashes,
  bolts, blasts, explosions, ripples, grenades), 250..298 (mince, fire, sand, acid, blood, the moth's
  powder, fluid), 303..306 and 350..399 (vomit, fire, sparks, blood emitters, poison). Each entry is
  fresh (modes 0: its own init runs on the receiver) and carries what its creators set: per effect the
  `type` and `mdlver` its tables have, its counters and delays, its flags, a link exactly when its step
  reads one (flg 0x80 with the work and node), its owner's texture (`stflg` 0x20, `tex_id` in that
  list) or model, `exp0` only where it belongs to a work. Left out on purpose: effects whose state
  cannot go as fields (OR_WORK effects, raw pointers in counters, a drawing function or extra slot set
  by the creator) and those only room scripts or other effects make. Anything else is refused.
* **Voices**: `Pose.voice_n` counts the sender's player voices, `Pose.voice` is the last one; the
  receiver plays each new count at the partner's position in the voice of the partner's model.
  Voice calls during a host enemy's step on the stand-in play nothing (the partner's game sends them).

### Added enemies and options

* `EV_SPAWN` (host): every enemy that exists on the host but not in the room file (F2 spawner,
  extra enemies) with slot, source room file and record, type, model version, variant, position,
  and the enemy variant's id (custom enemies, [ENEMIES.md](ENEMIES.md) "Enemy variants"; in what
  was padding, so the protocol is unchanged; an older joiner makes its base type).
  The joiner realizes it into the same slot; one that arrives while the joiner's room does not run
  waits (up to 900 frames). It also carries the randomizer records the host had to keep.
* `EV_EOPTS` (host): the enemy options (extra, placements, randomizer, seed, variety, event
  enemies, and the cutscene cast's on/off, models, assignment and imported models: protocol 29), at
  every new session, before each game state and on every change. Placements and
  randomizer are computed from the room file (records, script, collision) and the seed only, so
  both make the same records in the same slots, event records included.
* More than 48 active enemy slots are sent as a moving window of 48 per frame.
* **The cutscene cast** ([ACTORS.md](ACTORS.md) "Cutscene cast") is computed on each side from the
  host's options, the room file and the seed only, so both draw the same models in the same scenes;
  nothing per record is sent (it is only drawn: the records, their slots and snapshots are untouched). A side that runs
  out of display slots or texture memory shows that record as itself, only on its own screen.
* **Imported models** in the cast: each side sends the identity of its imported models list in
  `PoseExt.imp_id` (`cast_import_id`: a hash of the names and groups in list order, 0 none). With the
  host's IMPORTED MODELS on, each side uses them only while the partner's `imp_id` equals its own (the
  same models in the same order, so the same picks); otherwise both pick from the built-in models
  alone, and nobody sees a model the other lacks. A game without a partner yet (the host before the
  joiner arrives) has no partner list: no imported models in its cast until then.

### Trade transactions

* The F3 trade (`EV_TRADE`): the offerer moves the item into **escrow**; the receiver answers
  "taken" or "refused" and keeps the answer in a ledger that outlives sessions; after a reconnect
  the offerer asks again (`TD_QUERY`).
* The trade screen (`EV_TSCREEN`): both sides send their state (grid, cursor cell, offered word,
  ready, requests). The host commits only when both are ready and the joiner's last state shows the
  host's current offer: it moves its item to escrow and sends `TS_SWAP`. The joiner checks it is
  still ready on exactly that pair, tries the swap on a copy of its inventory, applies it whole or
  not at all, and answers `TS_DONE` (kept in its ledger). The host adds the joiner's item and drops
  the escrow, or gives the escrow back. After a reconnect the host asks `TS_QUERY`; an unanswered
  swap is recorded as refused so a late copy cannot apply.
* When a session ends, held escrow goes back into the inventory (else the item box); a later
  "taken" answer removes that copy again.
* Every item that moves in or out (trades, escrow, item box fixes) goes through `pc_rules.h` `pc_inv_add` /
  `pc_inv_take` (ctest `rules`): all or nothing, and a word with count 0 (the Combat Knife as picked up, an empty
  gun, Claire's Gold Lugers) takes a cell like any other.

### Saved inventories and rejoin

* `EV_OWN` (joiner -> host): the joiner's own items (`COOP_OWN_SIZE` 1564 bytes: item words, status
  flags, weapons, health) as changed 64-byte chunks, at most once a second, the last chunk of a
  change marked. The host keeps up to 16 records per player id and writes them to
  `saves/NNN.coop-partners.bin` / `coop-partners-nn.bin` with its save (`save_glue.c` / `mc.c` ->
  `coop_save_written`).
* The host answers in its `PoseExt.rec` whether it has a record (for the lobby's ITEMS row) and puts
  the record into the first `EV_BLOB` it sends that joiner (chunks with `pad` 1).
* **Rejoin**: `PoseExt.pid` and `PoseExt.tag` (`rdv_rejoin_tag`: BLAKE2b keyed with the secret over
  the session's code). A datagram from a new address that passes every check and carries the current
  partner's id **and** tag replaces the stale session at once. Session ids are random (`rdv_random`).
  The host remembers a partner that went without a goodbye and gives its rejoin the record.
* A guest whose session ended while it played in the host's game (`own_cont`, until its title) and that joins
  again with *Saved with the host* takes its current items as the fallback (`ITEMS_CURRENT`'s snapshots): the
  host's record wins when the host has one, else they stay (not the new-game inventory).

### Game over

* **Both** (`OPT_GAMEOVER_BOTH`): each game sends `PF_GAME_OVER` while its game over task
  (`gameover.c`, task 14: `tk_flg` 0x4000 set, `ts_flg` 0x4000 clear) runs. A new `PF_GAME_OVER` of the
  partner (an edge, not the level: a joiner still in its game over while the host's continued game runs
  starts nothing there) starts ours the way the scripts' `bhGameOverSet` does (`ts_flg &= ~0x4000`,
  `gov_md0` = `gov_md1` = 0), once our player is in control (`local_visible`, alive, the joiner with no
  game state on its way). The joiner's game over is held before its question (`gov_ct` kept below the
  50 at which `bhMainGameOver` asks).
* The host keeps the joiner's items at its continue point: `room.c bhPushGameData` (decomp hook) calls
  `coop_retry_point`, which copies the joiner's latest complete `EV_OWN`; a joiner that joins later
  counts with its first one. The push of the host's own Retry restart is the same point and keeps them.
  The game state the host's Retry sends goes with those items in place of the joiner's (`EV_BLOB` `pad`
  2), and the joiner loads it straight from its game over (`ts_flg |= 0x4000` first, as the game's own
  continue). `EV_OWN` carries in `tid` the last game state the joiner got, so items it sent before that
  continue reached it are not taken as its current ones.
* No: the host's session ends at the title as usual; a joiner whose session ends while it waits in (or
  for) the shared game over calls `bhReturnTitle`.
* **Each player**: the joiner's own Retry restores its own continue point first; until the host's game
  state is loaded it keeps the items it died with (`own_hold`: no `own` snapshot of the restarted game),
  which `own_restore` puts back. Before, a host busy in a menu or a cutscene sent its state late and the
  joiner kept its continue point's items instead (the world kept the host's flags, so the items taken since
  were gone).

### Metal detector

Room 0/9's script (event 0, started on entering floor zone 3, the north side with security box
`sys->itm[192..223]` at trigger 11, or floor 4, the south-west side with box `[224..255]` at trigger 12)
clears `rm_flg` bit 2 and sets it for each metal item `PlItemCheck` finds in the bag; carried that way,
entering zone 3 or 4 plays the "!" stop, and a gate zone (floor 5, 6) sounds the alarm: `ev_flg` bit 53
(shared progress), the shutters down, `ky_flg` bit 15 cleared, until the release switch (triggers 6, 7).
A clean pass sets no flag: the room's entry script opens the shutters and sets `ky_flg` bit 15 while
bit 53 is clear. Triggers 11 and 12 (type 3) start events 18 / 19, which open the box screen on
`cb_flg` 0x80000 / 0x100000 (`sub1.c` `ItemBoxInit`). Coming from zone 4 to zone 3 (`rm_flg` bit 10) plays
the reminder "Take the metal items you placed into the security box with you" (events 30, 32).

`OPT_SKIP_DETECTOR` (the host's `coop_skip_detector`; `coop_skip_detector()` is the session's value), in
`detector_glue.c`, only in 0/9 and only while `coop_on`:

* `bhPlItemCheck` finds nothing: every `PlItemCheck` of that room is event 0's, so `rm_flg` bit 2 stays
  clear and the stop, the alarm and its flags never happen, on both sides alike.
* Before scd1 each frame: a security box with no item in it has its trigger off (`flg` bit 0, as the
  script's `EtcAtariSet` does; the script never turns these two off, so on again is the room's own
  state); one with items keeps it until it is empty. With box 192 empty, `rm_flg` bit 10 is cleared in
  zone 3 before scd1 tests it (no reminder). The rule off in a session puts both triggers back on.

No item is ever moved: what a box holds stays reachable through its own screen.

### Quick start

`OPT_QUICK_START` (the host's `coop_quick_start`; `coop_quick_start()` is the session's value), `coop_glue.c`
`quick_start`. A new game's items come from `AllItemInit`: Claire's bag holds the manual (item 145, word
`0x910001`) and `sys->itm[383]` already lists FILE entry 0, the manual. Reading it (`fileview.c`) takes the
item out and sets `ic_flg[15]` bit 31. The combat knife on the table of 0/0 is item record 0 (item 8) with item
trigger 7 and `it_flg` bit 120: the room's scd1 `ItmCk` picks it up, its scd0 `ItmSetCk` leaves the spot empty
once the bit is set. `quick_start` puts the knife (`0x80000`: count 0, as `GetItem` gives it) in the manual's bag
slot, sets `ic_flg[15]` bit 31 and `it_flg` bit 120 and clears `ic_flg` bit 120, as `bhItmCk` does; with
`OPT_IMPORTANT` (shared loot) it marks bit 120 as an important spot (`IMP_SPOTS`) taken by us (`IMP_TAKEN`), as
`coop_item_taken` would. A bag without the manual is no new game's and is left alone.

* Host: `system.c bhSysCallMonitor` calls `pc_quick_start` right after `AllItemInit(0)`, the new-game step a
  loaded game skips (`ss_flg` 0x200); in a session as host with the rule on.
* Guest: `own_restore`, where a joiner without own items gets the new-game items (`AllItemInit`), before
  `imp_restore`: its knife spot stays taken by it, and `last` is taken after it, so nothing of it goes out as a flag
  change. Saved or kept items are not touched.

### Ping and name tag timing

Each datagram's arrival time comes from the network: Linux `SO_TIMESTAMPNS`; Windows a small thread
waiting on `FD_READ` notes when data arrived. The pose's `t_ms` is stamped at send, `echo_age` is
send time minus the echoed datagram's arrival. Sample = arrival - our clock - the partner's hold,
smoothed (EWMA 0.1). The name tag's point goes with the recorded frame (`stream_point`) and is
interpolated with the in-between frames; it snaps on camera cuts and in vanilla mode.

## Protocol 30

`COOP_VERSION` is **30**; both players need builds with the same version (a mismatch is reported,
e.g. "The host uses co-op compatibility version 29; yours is 30. Use matching game builds."). The co-op server does not
look at the game protocol. Version history in short: 11 join code, 1200-byte datagrams, checked
events, `EV_ATR`; 12 `EV_FX`; 13 `EV_GRANT`; 14 `EV_TSCREEN`; 15 `EV_GONE`; 16 player id and rejoin
tag, `EV_OWN`; 17 trade requests; 18 voices; 19 the game rules `OPT_IMPORTANT`, `OPT_NO_RIBBON`,
`OPT_SKIP_SYNC`, the cutscene flags `PF_CINE` / `PF_SKIP` and `EV_SKIP` (synchronized cutscene skip),
shared loot as one pool (`EV_PICKUP` gives the partner only items for both; `undo`), the shooter's
aim height in `EV_ENE_HIT`, unknown flag and option bits refused; 20 `EV_EOPTS` carries the
randomizer's variety and event-enemy setting (the randomizer's choices changed, so builds of 19 would
make other enemies from the same seed); 21 `COOP_HIT_PRECISE` in `EV_ENE_HIT` (third person
"Precise Shots", [THIRDPERSON.md](THIRDPERSON.md#precise-hits)); 22 the game rules `OPT_GAMEOVER_BOTH`
and `OPT_SKIP_DETECTOR`, `PF_GAME_OVER`, `EV_BLOB` `pad` 2 and the `EV_OWN` `tid` (see "Game over",
"Metal detector"); 23 the game rule `OPT_QUICK_START` (see "Quick start"; it takes the last free bit of
`Pose.opts`, so a further rule needs a field of its own); 24 enemy slots 0..254 (`COOP_ENE_SLOTS`, the PC's
255 slots) in every slot field, and `EV_FX` marks its `FX_*` references in `refs` (the bytes 253..255 are slots now); 25 `Pose.use`, the trigger the sender is using (one player at a puzzle at a time, see "Puzzles"; the pose grew to 112 bytes); 26 `EneSnap.id`, the enemy type in the slot: a snapshot applies only to a slot holding the same type (`hokan_count` is 16 bits now; the snapshot stays 100 bytes); 27 `PoseExt.br`, `br_seed`, `br_id` (BioRand: the host's choice and each side's pack; `Pose.opts` has no free bit, so a field of its own; the extension grew to 104 bytes); 28 direct sessions are encrypted after a handshake bound to the whole join code (`PKT_HELLO`, `PKT_HELLO_ACK`, see "Security"), so `Pose.auth`, the 32-bit join-code proof, is gone (`rsv`, zero), the goodbye's `COOP_REJ_STATE`, and a generic `EV_FX` only for the effects co-op sends, each field checked (see "Effects and voices"); 29 the cutscene cast: `EV_EOPTS` carries `cast`, `cast_all`, `cast_assign`, `cast_imports` (the event stays 88 bytes) and `PoseExt.imp_id`, the sender's imported models (the extension grew to 108 bytes); 30 `EV_BRSEED`, the host's BioRand seed string and CLI checksum (a guest can generate the host's pack, see "BioRand"; the pose and extension are unchanged). `Pose.pflg` and `Pose.flr` were added inside protocol 18,
`Pose.rcase` inside protocol 22, the aim pitch in `Pose.pflg`'s spare bits inside protocol 23 (0 means "not
sent", and receivers fall back; a receiver before the pitch reads `pflg & 6` only and keeps the straight pose).

All structures are little-endian, packed as the compiler lays them out on x86 (pinned by
`_Static_assert`s; a 64-bit build would be a different wire format).

### Datagrams

At most `COOP_DGRAM_MAX` = 1200 bytes on the wire (below every internet path MTU). In a session each
is sealed: `"CVXE"`, a 64-bit counter, the datagram encrypted, a 16-byte Poly1305 tag (28 bytes more:
`rdv_seal`, directly with the handshake's keys, through a server with the server session's). A
frame's content goes into as many datagrams as needed, each starting with the pose so each is useful
alone:

```
Pose (112) | Event x nev (88 each, nev <= 16) | EneSnap x nene (100 each, nene <= 48) | PoseExt (108, if PF_EXT)
```

The size must be exactly that sum. The first datagram of a frame carries the `PoseExt`. Up to 16
unacknowledged events and 48 snapshots per frame; the receiver gathers the newest frame's snapshots
over its datagrams.

| Packet type (`Pose.type`) | Meaning |
|---|---|
| `PKT_POSE` 1 | A frame's pose with events, snapshots, extension. |
| `PKT_BYE` 2 | Goodbye: a bare `Pose`, sent three times by every stop and at exit. `reject` 3 (`COOP_REJ_REMOVED`): the host removed us; 4 (`COOP_REJ_BIORAND`, a joiner's): its BioRand pack is not the host's; 5 (`COOP_REJ_STATE`, a joiner's): the host's game state failed its checks there (the joiner leaves rather than wait for the same again). |
| `PKT_REJECT` 3 | Host to a sender it will not adopt: `reject` 1 wrong join code, 2 already has a partner. At most one per second, plain text (the only datagram a session sends unsealed). |
| `PKT_HELLO` 4, `PKT_HELLO_ACK` 5 | The direct handshake (`CoopHello`, as large as a `Pose`: magic, version, type, a zero byte, the 32-byte public value, zeros), joiner to host and back; never inside a session. |

### Pose (112 bytes)

| Bytes | Fields |
|---|---|
| 0-15 | `magic` "COOP" (0x504F4F43), `version` u16, `type` u8, `flags` u8, `sid` (sender's session), `rsv` (zero; protocol 27 and before: the 32-bit join-code proof) |
| 16-31 | `seq` (sender's frame counter; older poses dropped), `t_ms`, `echo_ms`, `echo_age` (ping) |
| 32-39 | `chr`, `costume`, `stg`, `rom`, `pos` (room entry point), `opts` (host's game rules: `OPT_INDIVIDUAL` 1, `OPT_TRADE_FREEZE` 2, `OPT_IMPORTANT` 4, `OPT_NO_RIBBON` 8, `OPT_SKIP_SYNC` 16, `OPT_GAMEOVER_BOTH` 32, `OPT_SKIP_DETECTOR` 64, `OPT_QUICK_START` 128), `regroup` (host counter), `mfile` (sender's own part file - 10) |
| 40-91 | position (3 floats), angles (3 ints), `mtn_no`, `frm_no` (the frame shown, before the step's advance: `actor_frame`), `mtn_md`, `hokan_count`, `hokan_rate`, `wpnr_no`, `hp`, `mode0`, `mode1`, `nev`, `nene` |
| 92-107 | `stflg`, `ev_ack` (highest event id applied from the peer), `mset` (`MSET_OWN` -1, `MSET_ROOM` -2, `MSET_NONE` -3, or an enemy slot 0..254), `busy` (`BUSY_NONE/EVENT/MENU/BOX/TRADE`), `reject` (`PKT_BYE`, `PKT_REJECT`) or, in `PKT_POSE`, `rcase` (the room case of the room the sender is in + 1, 0 not sent; same room means the same case too, and the follow loads it), `voice_n`, `voice`, `pflg` (`flg & 6`: hit / held; bits 0 and 3-7 the aim pitch, -31..31 steps of 60/31 degrees, 0 straight or not sent: `coop_pitch_get`), `flr` (`flr_no + 3`, 0 not sent) |
| 108-111 | `use` (the room trigger the sender examined and is held by + 1, 0 none: the partner does not take it meanwhile), 3 spare bytes (zero, else refused) |

`flags`: `PF_IN_GAME` 1, `PF_VISIBLE` 2, `PF_ROOM_MTN` 4, `PF_WANT_BLOB` 8 (joiner asks for the
game state), `PF_EXT` 16 (a `PoseExt` follows), `PF_CINE` 32 (in a cutscene its skip button could
skip now), `PF_SKIP` 64 (pressed skip in it: waits for the partner), `PF_GAME_OVER` 128 (our game over
runs; every bit of `flags` is in use).

**Skip Cutscenes Together** (`OPT_SKIP_SYNC`): the skip button reaches the game as pad bit
`0x10000` (`pad.c bhSetPad`; `system.c bhSysCallGame` sets `cb_flg 0x10000000`, which the room
script's skip branch tests). `coop_skip_pad` (called from `trade_pad`) withholds it while the partner's
pose says `PF_CINE` in our room, and notes the press (`PF_SKIP` in our pose from then on). The host,
in a cutscene with `PF_SKIP` of its own and the partner's, sets `cb_flg 0x10000000` and sends
`EV_SKIP`; the guest applies it only while it still asks in a skippable cutscene of that room. A
request ends with the cutscene; while the partner is no longer in one in our room, a pending request
skips at once.

### PoseExt (108 bytes)

`name[16]`, `lchr` (lobby model), `lobby` (lobby open), `items` (joiner: `ITEMS_NEW/SAVE/HOST/CURRENT`),
`rec` (host: `REC_UNKNOWN/NONE/HAVE/OFF`), `note[40]` (what the player chose), `pid[16]` and `tag[16]`
(joiner: player id and rejoin tag), then BioRand (protocol 27): `br` (host: 1 it plays its pack; joiner: 1 it
has a pack; anything above 1 refused), 3 zero bytes (else refused), `br_seed` (the sender's pack's seed number,
0xFFFFFFFF unknown), `br_id` (its identity, `biorand_id`: a hash of `log_pl0.txt` with the seed string and spoiler,
and the room files' names and sizes; 0 no pack). A joiner whose `br_id` differs from the host's while the host's
`br` is 1 says goodbye with `COOP_REJ_BIORAND`. Then (protocol 29) `imp_id`, the sender's imported models
(`cast_import_id`, 0 none; see "Added enemies and options"). Only printable ASCII is shown.

### EneSnap (100 bytes)

Slot (0..254), `mode[4]`, model index (`mlwP` as index into the enemy's `mdl[]`, -1..15), motion set (offset
0..511 from the enemy's own set, `MNW_ROOM`, `MNW_KEEP`), `flg flg2 stflg mdflg`, position, angles,
`mtn_no` (< 512), `frm_no`, `mtn_md`, `hokan_count` (16 bits), the enemy type `id` (< 100) and a zero byte, `hokan_rate`,
hp, hit capsule `watr[7]`. The receiver applies an active slot's snapshot only to an enemy of the same `id` (the slot
layouts differ where one side made an enemy the other did not: a moth's leader, children, eggs), and not a new
enemy's (`mode[0]` 0) to one whose own Init ran.

### Events

`Event` (88 bytes): `id` (reliable, in order), `type`, `idx` (enemy slot 0..254 or a type-specific index),
`stg`, `rom` (events for another room are dropped), then an 80-byte payload.

| Type | Id | Direction | Payload |
|---|---|---|---|
| `EV_ENE_HIT` | 1 | either | Our shot hit a puppet: joints, damage, hit bits, hit point/direction, weapon; `flg` also carries the shooter's aim height (`at_flg & 0xE` << 8, with `COOP_HIT_AIM`) and `COOP_HIT_PRECISE` when the shooter's "Precise Shots" judged it (a zombie's head then bursts only from a head hit) |
| `EV_PLY_HIT` | 2 | host | A host enemy hit the partner's stand-in: hp delta, flag bits, modes, direction |
| `EV_BLOB` | 3 | host | Game state chunk (save region; `pad` 1: its items are the joiner's saved ones; 2: the host's Retry of a shared game over, with the joiner's items of that continue point) |
| `EV_FLAGS` | 4 | either | 1..6 changed flag words: set and cleared bits, base event id |
| `EV_PICKUP` | 5 | either | Item taken: item for the partner too (0: spot only), spot, its `it_flg` bit; `undo` 1 (host only, bit given): both took that spot, the joiner's pickup goes back |
| `EV_LOST` | 6 | either | A key item used up |
| `EV_ESTATE` | 7 | either | Grab state chunk (enemy work, `exp0`, linked child, the player) |
| `EV_GRAB` | 8 | host | A zombie grab handed to the joiner: child slot (255 none), model/set indices, the stand-in's flag changes, the `EV_ESTATE` transfer id |
| `EV_UNGRAB` | 9 | joiner | The grab handed back (with or without state) |
| `EV_TRIG` | 10 | either | World event block: gate flag set/bit/value, room case, script signature, action offsets (24 per event, up to 48) |
| `EV_TRADE` | 11 | either | F3 trade: `TD_OFFER`, `TD_ANSWER`, `TD_CANCEL`, `TD_QUERY`; transaction id, item word |
| `EV_BOX` | 12 | either | Shared item box: words taken out and put in (up to 16) |
| `EV_BOXFIX` | 13 | host | Box correction: items the joiner could not have |
| `EV_SPAWN` | 14 | host | An added enemy (`SpawnNote`), or a randomizer record the host kept |
| `EV_EOPTS` | 15 | host | Enemy options: extra 0..3, placements, randomizer, seed, variety 0..1, event enemies 0..1, cutscene cast 0..1, its models 0..1, assignment 0..1, imported models 0..1 |
| `EV_ATR` | 16 | either | 1..2 room records (wall, trigger, floor, object) after a world event |
| `EV_FX` | 17 | either | An effect started here (`FX_BURST` 308, `FX_PARTICLE` 302, `FX_ARM`, or a generic entry). What it follows (`idx`), its model's owner and `exp0` are each an enemy slot or, with their bit in `refs` (`FXR_IDX` 1, `FXR_OWNER` 2, `FXR_EXP0` 4), `FX_RECEIVER` 0, `FX_SENDER` 1 or `FX_NONE` 2 |
| `EV_GRANT` | 18 | either | An event gave the sender an item: item, count, floor, position |
| `EV_TSCREEN` | 19 | either | Trade screen op (below) |
| `EV_GONE` | 20 | joiner | Dead enemies (up to 26: slot, type, model version) |
| `EV_OWN` | 21 | joiner | The joiner's own items, chunk of 1564 bytes total; `tid`: the last `EV_BLOB` transfer it got |
| `EV_SKIP` | 22 | host | Both pressed skip: the cutscene ends now (`op` `SKIP_GO` 1; `stg`/`rom` the host's room) |
| `EV_BRSEED` | 23 | host | While it plays BioRand, to each new partner and when they change: its pack's seed string (`str[68]`, `log_pl0.txt`'s `Seed:`, BioRand's alphabet, `R` and the dashes in place; all 0: unknown) and the first 12 hex digits of its CLI archive's SHA-256 (`dig[12]`, lower case; all 0: unknown). The guest decodes the string too (`biorand_seed_decode`; one it cannot read counts as unknown). |

Transfers (`EV_BLOB`, `EV_ESTATE`, `EV_OWN`) carry a transfer id, total size, offset (a multiple of
64) and length (64, the last chunk less); a transfer is complete only when every chunk of that id
arrived, and a new id restarts it.

Trade screen ops (`EV_TSCREEN`):

| Op | From | What |
|---|---|---|
| `TS_REQ` 1 | either | Request (its id `sid`) |
| `TS_CANCEL` 2 | either | Request withdrawn |
| `TS_OPEN` 3 | either | Accepted: both open screen `sid` |
| `TS_CLOSE` 4 | either | Left the screen |
| `TS_STATE` 5 | either | Our side: grid words, cells (8/10), equipped word, our cell and word, the partner's cell and word as we saw them, ready |
| `TS_SWAP` 6 | host | Commit: transaction id, both cells and words |
| `TS_DONE` 7 | joiner | Swap made or not (and why) |
| `TS_QUERY` 8 | host | After a reconnect: what became of transaction `tid` |
| `TS_WANT` 9 | either | Request the partner's cell and its word (`TSC_NONE`, 0: withdrawn) |
| `TS_DECLINE` 10 | either | Decline the partner's request |

### Reliability and sessions

* Events are reliable and ordered: ids from 1, resent in every datagram until the peer's `ev_ack`
  covers them; the receiver applies only the next id. The send ring holds 512 unacknowledged
  events; when it is full nothing is dropped: the push fails and the frame ends with a resync (a new
  session id, both streams restart, the host's game state goes over).
* A new session id from the peer resets both event streams and the pose sequence check.
* 3 s without a datagram: partner "not responding"; 15 s: lost (joiner stops, host waits again).
* The host adopts the first sender whose datagrams open with keys of the join code (see "Security");
  once a partner is there, datagrams from anyone else are dropped (a stranger with the code gets
  "busy", rate limited).

## Security

Co-op is meant for playing with friends. The host and joiner trust each other's game for game
outcomes (a host could give a partner any items); the checks below keep a malformed or hostile
datagram from corrupting memory or crashing the game.

**Every received datagram** is opened with its session's keys (a forged, altered or replayed one is
dropped) and passes `coop_check_all` before anything changes (peer, session, event stream or game):

* Header: size at least a `Pose`, magic, version, type, `rsv` zero; for a pose,
  `nev <= 16`, `nene <= 48`, and the size exactly what the counts say; `PoseExt` enumerations in range.
* Pose: finite floats (and below 1e7), `busy`, `mset` and `flr` in range, no `flags` or `opts` bit
  this protocol does not have.
* Every event within range for its type and **direction** (host-only: `EV_PLY_HIT`, `EV_BLOB`,
  `EV_GRAB`, `EV_BOXFIX`, `EV_SPAWN`, `EV_EOPTS`, `EV_SKIP`, `TS_SWAP`, `TS_QUERY`; joiner-only: `EV_UNGRAB`,
  `EV_GONE`, `EV_OWN`, `TS_DONE`). Examples: joints -1..63, damage 0..10000 and none on an absent
  joint, only the hit bits, weapon < 23; flag words < 120; item words id 1..155, count <= 999; enemy
  slots < 255; transfer chunks aligned and inside the total; effects only as listed in "Effects and
  voices", references an enemy slot or `FX_*`; world-record objects in slots 4..31; grant positions within the save format;
  `EV_SKIP` only `SKIP_GO` in a room 0..9 / 0..99.
* Every snapshot: slot < 255, model index -1..15, motion set and motion < 512, enemy type < 100, finite floats.

Then, when applying:

* Motion numbers are checked against what is left of the 512-entry set they index (an enemy's set
  may start mid-way). A model index must name a model there.
* Grab state is copied through an allowlist of field ranges (modes, position, flags, motion state,
  counters, damage); ids, types, model data, every pointer, the slot index and the active/linked bits
  stay local. The slot must be a zombie with the same linked child; the host takes back only slots it
  handed over.
* The game state is checked before loading: version, character, a stage/room/case a room file exists
  for, difficulty, finite position, weapons < 23, items < 156 in the inventories and boxes (`sys->itm`
  0..255) and in the pending event items (`GRANT0`, with their floor). The other words of `sys->itm`
  are no items and take any value: the important item spots (256..287, bitmaps), BioRand's marker and
  pack id (288, 289), the files read (383, a bit per file), unused words. A state that fails ends the
  joiner's session with the reason (it would only get the same again), and the host shows that it left.
  Room follow only to a room that exists.
* Effects: the partner's references must name works that are here, with the node (`lkono`), model
  and texture (`tex_id` within the texture list) the effect will index; at most 64 a frame and 256
  alive (half of `eff[]`).
* World events and records: see "World events and records" (only the receiver's own script's
  blocks and writes).
* Item box changes apply only with shared loot; trade requests only for the open screen and when our
  real inventory holds exactly that word at that cell.

**Limits**: per frame at most 96 datagrams, 128 KB and 3 ms of reading (checked before a datagram is
read); oversized datagrams and ICMP errors are skipped. 1200-byte datagrams are never fragmented.

**Join code and direct sessions** (protocol 28, `coop_proto.h` `CoopDirect`): the code is 8
characters of a 32-character alphabet (40 bits) from the system's randomness; the log shows only its
first half. It is the password of a PAKE handshake (CPace on X25519, Monocypher): both games map the
code to a curve point G (Elligator 2 of `BLAKE2b("recvx direct v1 G" || code)`), the joiner sends
`Y = X25519(y, G)` for a fresh secret y in `PKT_HELLO`, the host answers with its own in
`PKT_HELLO_ACK`, and both take `K = X25519(own secret, other's Y)` with both values and the whole
code into `BLAKE2b-512`, which gives one XChaCha20-Poly1305 key per direction. Every datagram of the
session is then sealed with a 16-byte Poly1305 tag (counter nonces, a 64-entry replay window); the
host takes a partner, and the joiner listens to a host, only when its datagrams open with these keys.
So:

* A wrong code gives other keys: the host cannot open the joiner's datagrams (and answers "wrong
  join code"), nothing the joiner sends is taken, and nothing of its game state reaches it.
* Nothing on the wire lets anyone test codes offline, not even a whole recorded handshake (the
  values are uniform points; K needs a secret): each guess costs a handshake with the host, answered
  only then. An attacker who intercepts a join can try one code per handshake it intercepts.
* The host answers at most `COOP_HS_RATE` (20) new handshakes a second in all, keeps at most 64
  (an answered one stays at least 3 s however many come), lets each address have one guess open at a
  time, and after each wrong code an address waits: 250 ms, doubled per failure, up to a minute,
  forgotten after 10 minutes (`CoopGuard`, the last 32 addresses). A guesser gets at most 20 guesses a
  second however many addresses it has (one a minute per address after nine failures);
  the code is one of 2^40, so on average 2^39 guesses: about 870 years at that rate. A flood of
  handshakes can delay a join (the budget is shared), not get in.
* Everything is encrypted: someone who reads the traffic learns nothing of the game.

**Through the server**: every session is encrypted end to end (X25519 key exchange,
XChaCha20-Poly1305 per datagram, Argon2id for the password); the room code and the introduction are
bound into the session keys. Without a password a passive observer, including a logging server, learns nothing; a
room password also protects against an actively malicious server. Details and the server's own
limits: [SERVER.md](SERVER.md).

**Rejoin and saved items**: the player secret never leaves `recvx.ini`; the rejoin tag is bound to
the session's code. A takeover needs a non-zero tag that matches.

## Determinism and limits

* Each game simulates its own frames; nothing is lockstep. Enemies of a shared room are the host's;
  the joiner sees them one way trip late, and the host sees the partner's stand-in one way trip
  behind (an attack lands where the host saw it).
* Only explicit fields travel. Room memory layout is deterministic per room load, but depends on
  what was loaded before, so raw structs are sent only where needed (grab state, save region) and
  then through allowlists and checks.
* Placements and the randomizer depend only on the room file and the seed; a joiner that loads a
  room in the same moment as the host (following it through the door) may decide a randomized
  record before the host's "kept" note arrives (logged).
* Bandwidth (two instances, 30 frames/s): a pose-only datagram ~164 bytes; a host in a room with 30
  enemy slots ~3.4 KB per frame in 3 datagrams; effects add at most a few percent. The joiner sends
  ~170 bytes per frame.

## Known limits and risks

* A black (or frozen) screen while you can move: a script's fade to black whose fade-in never ran, or the
  under-water view's full-screen picture left on without its effect, after co-op cut a script step short.
  After 3 s in normal play (no event, scene, door or menu) the screen fades in / the room is drawn again
  (`coop_glue.c screen_guard`, logged "the screen stayed black" / "the room was not drawn"); before, it lasted
  until the next door.
* UDP and IPv4 only; networks that block UDP cannot play (no TCP fallback).
* Two players only.
* Only zombie (en01) grabs are handed over to the joiner's game; other grabbers reach the joiner as
  a hit. A larva the host's moth lays on the joiner rides on the joiner's stand-in in the host's game only
  (at most three; its bites reach the joiner as hp loss; it goes when the joiner leaves the room). A handover takes a few round trips during which that zombie stands still.
* The partner has no facial animation and no aim pitch.
* Some effects are not mirrored: those whose caller sets a matrix or a pointer the record cannot
  name (e.g. Alexia's acid, shell casings) and blood pools.
* A cutscene the joiner triggers alone plays only for the joiner (its flags reach the host).
* A puzzle solved by the partner's own steps in our room changes our room's walls and triggers
  (its entry script's), not its models, lights or effects: those follow when the room is entered again.
* One player at a trigger: two presses on the same trigger within one pose's trip (a ping) both
  get in.
* A room whose entry script writes world flags sets them for both when either enters (the lifts'
  floor, the palace piano's music, ambient voices, a room's entry cutscene: transient state that the
  room sets again). Of the clears that undo a solution only 9/31's is kept from it (see "An entry script
  that would undo a solution").
* Of a world event's results that depend on local state (per-room flags, timing), only collision
  and object placement are synced afterwards.
* Both taking one pool item (shared loot): the player who gives it back gives the amount a pickup
  of that item adds (`getbulletmax`); an equipped weapon, or a stack used up meanwhile, is not taken
  back (logged). Which player keeps it follows the order the flags arrive, not the order of the
  "Yes" presses: a player still in the status screen after taking has not sent its pickup yet.
* Shared loot, a pool item from the partner's event that this game replayed: given back by amount as
  above, only while the replay's window is open.
* A key item both take at the same moment may end up twice in the joiner's inventory.
* A key put into the item box is not "used up" for the partner.
* Two voices of the partner within one lost datagram's frames play only the last.
* Enemy options changed in game: a joiner that loads a room before the change arrives builds that
  room with the old options until it loads it again.
* Items held in escrow live in memory until the session ends; closing the game while one is held
  loses it with the unsaved state.
* Through a server, a relayed session ends if the server restarts.
* Duplicate Weapons and Ribbons: a pickup is known as one only when it happens in a session with the
  rule on. A weapon the host took while playing alone (or before this build) is gone for a joining
  partner too; one a guest took in its own game before joining can be taken again.
* Synchronized skip: the skip waits for the partner's pose (one way trip) and, when the joiner asked
  second, its skip comes one round trip after its press. A scene the partner has not started yet
  (it replays it a moment later) does not hold the first press.
* The joiner's lobby shows the host's game over and metal detector rules and enemy options, not its
  loot, skip and ink ribbon rules.

## Test aids

Environment variables for headless and scripted runs. Frames are co-op frames unless noted.

| Variable | Effect |
|---|---|
| `RECVX_COOP_TRACE=1` | Log poses, events, flags, enemies, world events, effects, sounds (stderr). |
| `RECVX_COOP_STATS=1` | Every 300 frames: datagrams, bytes, effects sent/dropped, ping. |
| `RECVX_COOP_ECHO=n` | Show our own pose n frames late as the partner, without a network. |
| `RECVX_COOP_CODE=CODE` | Fix the host's join code (logged as a warning). |
| `RECVX_COOP_E2E=0` | Server sessions without a password unencrypted (logged as a warning). |
| `RECVX_COOP_FORCE_RELAY=1`, `RECVX_COOP_NO_PUNCH=1`, `RECVX_COOP_PUNCH_MS=ms` | Server path: always relay, no hole punching, punch duration. |
| `RECVX_COOP_DROP="from:to,..."` | Send nothing in those frames; `RECVX_COOP_DROP_EVENTS` holds back only the events. |
| `RECVX_COOP_LOSS=pct` | Drop that share of our datagrams at random. |
| `RECVX_COOP_REJOIN="frame,..."` | Join again (new session). |
| `RECVX_COOP_KICK=n` | Host: "Remove Partner" at frame n. |
| `RECVX_COOP_BYE_GRABBED=n` | Joiner: Disconnect n frames into a zombie grab it runs. |
| `RECVX_COOP_KILL="hostframe:slot,..."` | A lethal hit on our enemy in that slot. |
| `RECVX_COOP_WARP="hostframe:x:z,..."` | Both players to x/z at that host frame. |
| `RECVX_COOP_VOMIT="hostframe,..."` | Host: the walking zombie nearest the partner vomits. |
| `RECVX_COOP_CRAWL="hostframe,..."` | Host: its walking zombies become crawlers (as a knock-down with little health left), which crawl towards their target and grab its legs. |
| `RECVX_COOP_ORPHAN=frame` | Put our player in a grab with no holder, 3 m off (failsafe test). |
| `RECVX_COOP_STALE="hostframe,..."` | Our player's busy flag set with no enemy to clear it (the busy flag watchdog's test). |
| `RECVX_COOP_LARVA="hostframe,..."` / `RECVX_COOP_HATCH="hostframe,..."` | A moth's larva on our player (as one carried into a room) / our larvae hatch now (not while held). |
| `RECVX_COOP_BUSYLOG=n` | Every n frames: our player's busy flag, modes, motion, whether the status screen is open, the larvae (count, the last one's mode2/mode3 and player: 1xx ours, 2xx the partner's). |
| `RECVX_COOP_FX=0` | Send no `EV_FX`. |
| `RECVX_COOP_GOTO="hostframe:x:z:angle,..."` | Our player alone to x/z, facing angle (0..65535), at that host frame. |
| `RECVX_COOP_PAD="hostframe:hex,..."` | These game pad bits pressed for one frame (`b00` confirm/Cross, `10000` the cutscene skip button). |
| `RECVX_COOP_TEST_KILL="frames:levelmask"` | End the replayed world event's tasks of those levels that many frames into a replay. |
| `RECVX_COOP_GRANT="item:stage:room[:floor:x:y:z],..."` | As if the partner got these items from events there. |
| `RECVX_COOP_TAKE=n` | Walk onto a shown event item and take it. |
| `RECVX_COOP_TRADE="frame:slot:count"` | F3 trade: offer; `"0:0:0:1"` / `"0:0:0:0"` accept / decline offers. |
| `RECVX_COOP_BOX="frame:entry,..."` | Take a box entry out (100 + n puts inventory slot n in), as the box screen would. |
| `RECVX_COOP_TSCREEN="hostframe:act,..."` | Trade screen actions: `k` trade key, `s<cell>` select, `r` confirm, `l` leave, `d` Disconnect, `i` log inventory, `c<cell>` / `cp<cell>` cursor ours / partner's, `a` confirm on cursor, `b` cancel, `w<cell>` request (`w-1` withdraw), `o` offer the requested, `x<cell>` wrong request, `P<hex>` pad presses, `M<x>/<y>[l\|r]` mouse. |
| `RECVX_COOP_TS_LOSE="s<n>"` / `"d<n>"` | Host's n-th `TS_SWAP` / joiner's n-th `TS_DONE` not sent. |
| `RECVX_TRADE_TRACE=1` | Every 10 frames: enemies frozen?, a hash of their state. |
| `RECVX_ENEMY_TRACE=dist` | One line per step of every living enemy within `dist` units of a player: type, target (`H`, `h` while the partner is no target, `P`), distances, modes, motion, whether each player counts as busy; on the host also an `atk` line per attack on the partner. |
| `RECVX_TEST_JOIN="n,..."` / `RECVX_TEST_STOP="n,..."` | F3 Join / Disconnect at the n-th frame hand-off. |
| `RECVX_TEST_TITLE=n` | Back to the title from a game (as game over "No"). |
| `RECVX_TEST_SAVE="gameframe:slot,..."` | Write the game in progress as a save (with the partners file). |
| `RECVX_LOBBY_KEYS="frame:text,..."` | Type into the edited lobby field (`\|` Enter, `` ` `` Escape, `<` Backspace). |
| `RECVX_SHOW_OVERLAY=3` | Open the F3 window at start. |
| `RECVX_DUMP_OVERLAY=1` | With `RECVX_DUMP_FRAMES`: also dump the window with the overlay (name tags, windows). |
| `RECVX_INF_HEALTH=1` | The F2 infinite-health cheat (survive grabs in tests). |

General test aids used with these (`RECVX_TELEPORT`, `RECVX_WARP`, `RECVX_EQUIP`, `RECVX_FLAGS`,
`RECVX_SKIP_CINE`, `RECVX_SPAWN`, `RECVX_KEYS`, `RECVX_TEST_EXAMINE`, `RECVX_TEST_ROOMFLAGS`, `RECVX_MENU_MOUSE`, `RECVX_EXIT_FRAME`,
`RECVX_TRACE_SND`, `RECVX_DUMP_*`) are listed in [ARCHITECTURE.md](ARCHITECTURE.md).

ctest `coop` (`pc/tests/test_coop.c`) checks every event type from the side that sends it and
refused from the other, malformed variants of each (`EV_SKIP`: its op, its room, from the joiner; `EV_BRSEED`: the alphabet, case, length, the dashes, the digest, from the joiner), pose
fields out of range and every pose flag and game rule (all eight bits of each are in use), datagram headers, every effect co-op sends against its rule (crafted ones refused: unknown effects, type 65535, modes, missing textures and models), the direct handshake (keys of every code character, replays, the backoff and its bounds), transfers, the event
ring, join address parsing, world event actions, record merging, the target rule and the loop guard, then fuzzes
200000 random and mutated datagrams. ctest `net` covers the socket (busy port, oversized datagrams,
receive timestamps) and port mapping start/stop cycles.
