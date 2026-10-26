# Modding

The PC port reads replacement and additional game files from a **mods folder**. Rooms can be
edited, cloned or built from scratch with the Python tools in `pc/tools/`. This directory
documents the formats and the tools:

| Document | Contents |
|---|---|
| [ROOM_FORMAT.md](ROOM_FORMAT.md) | room file (`RM_xxxx.RDX`) format, load path, records, event scripts, limits |
| [BUILDING_ROOMS.md](BUILDING_ROOMS.md) | building rooms with `rdxbuild.py`, the example areas, validation, headless testing |
| [RE4_VILLAGE.md](RE4_VILLAGE.md) | converting an RE4 village export into rooms |

Related: [../ACTORS.md](../ACTORS.md) (models, skeletons, motions, enemy records),
[../ENEMIES.md](../ENEMIES.md) (enemy scripts, kill flags, budgets).

## Legal

* The repository contains **no game data**. Every tool reads your own copy of the disc (and,
  for the RE4 tools, your own RE4 export) at run time.
* Mods are built **locally** from your disc and exports. Generated room files, extracted
  textures, models, previews, catalogues and sidecars contain game-derived data: **do not
  share, upload or commit them**. This applies to rooms built from purely original geometry as
  well when they import retail enemies, items, doors or textures, which all the example areas do.
* What can be shared: tool source code, JSON recipes and original procedural assets that
  contain no extracted bytes (for example `pc/tools/area/annex.json` and the generators).
* The RE4 tooling needs **your own RE4 export**; its outputs (rooms, reports, previews) are
  derived from both games and are likewise not for distribution.
* Keep build output in the ignored `mods/` and `scratch/` directories of your clone.

## The mods folder

`pc/src/audio/adx.c` implements it; small `#ifdef TARGET_PC` hooks in the decomp ask it first.

* Location: `$RECVX_MODS`, default `mods` relative to the **working directory** the game is
  started from (not the save directory). Names are case-sensitive on Linux; the game asks for
  upper-case names.
* The log shows `mods: <path> replaces the disc file (<size> bytes)` for every file it uses.
* Files are read as from the disc (whole 2048-byte sectors, zero padded).
* `mods/biorand/` is BioRand's pack (`pc/src/host/biorand.h`): while BioRand is on, a disc file is
  looked up there first, then in `mods/`; off, that folder is never read. BioRand writes room files,
  `SYSTEM.AFS/10`, `SYSTEM.AFS/11`, `ADV.AFS/2` and `log_pl0.txt` there and replaces the folder
  whole on each generation, so other mods belong in `mods/` itself. Its title picture (`ADV.AFS/2`)
  is not used (its colours come out broken): the title stays the disc's, or the HD pack's. Its room conventions (item
  `type` and `wrk_no`, the start inventory): [ROOM_FORMAT.md](ROOM_FORMAT.md#enemies-objects-items).

| Mods file | Replaces | Notes |
|---|---|---|
| `RM_SRRC.RDX` | room S (stage 0..9), RR (room 00..99), C (case 0..9) | replaces the disc's room, or **adds** a room the disc does not have; packed (LZ) like the disc files. Hooks: `ps2_sg_gd.c gdFsGetDirInfo` (size), `adxwrap.c OpenAfsIsoFile` (open) |
| `RM_SRRC.SPQ.key` | room sound bank key | text file with one decimal retail key, e.g. `60` = RM_0060's bank. Log: `mods: room sound key 1150 uses 0060`. Not recursive; an invalid file is ignored |
| `<ARCHIVE>/<index>` | entry *index* of an AFS archive opened by partition, e.g. `MRY.AFS/76` | sound banks, door animations, map files, player/weapon files, streamed ADX music and voices (ADX, at most 2 channels). Only ADX streams are tested in game |
| `<NAME>` | a plain disc file opened through `ADXF_Open` | |
| `enemies.ini` | nothing: adds custom enemies (enemy variants: a base type, another model, stat factors, motion and sound mappings, randomizer weights) | read once at start; format and limits in [../ENEMIES.md](../ENEMIES.md) "Enemy variants". Co-op partners need the same file |

Not covered: files the game reads with `ReadFileEx` (`MULTSPQ?.IDX`, `SYSMES.ALD`,
`COMMON.MLT`).

A room key is `stage*1000 + room*10 + case` written as four digits (`RM_1150.SPQ.key` for
RM_1150). Without an alias a new room has no sound bank of its own and the previous room's
banks stay loaded.

To uninstall a mod, remove its files from the mods folder. Rooms are never saved, so edited
rooms apply to existing saves the next time the room loads.

## What can be modded

| Change | How | Status |
|---|---|---|
| Records of an existing room (positions, items, enemies, walls, triggers, cameras, lights, fog, heights) with the same counts | `rdx.py dump` -> edit JSON -> `rdx.py build` | works |
| Script operands (door targets, flags) | `"patches"` in the `rdx.py build` JSON, offsets from `rdx.py script`; `rdxbuild.py link` for doors | works |
| A texture, same size and palette | `rdx.py tex` / `rdx.py teximport` | works |
| Record counts, sections, new geometry and textures | `rdxbuild.py decompile` / `build` (OBJ + PNG + JSON) | works |
| A cloned room in a new slot | copy to an unused `RM_xxxx.RDX`, edit, point a door at it | works |
| New original rooms connected to the game | `rdxbuild.py`, `build_area.py`, `build_village.py` | works, see [BUILDING_ROOMS.md](BUILDING_ROOMS.md) |
| New sound effects, map screen entries, inventory text, new animated creatures | | not supported |

A clone keeps everything in the file, including the scripts' flag numbers (give it its own
`it_flg`/`ed_flg` bits); it loses the room sound bank (use a `.SPQ.key` alias), its map entry and
the per-room tables in the executable.

## Tools

Run from the repository root. The disc image is taken from `--iso`, `$RECVX_ISO` or
`ISO/SLUS-20184 ROM.iso`.

### Room tools

| Tool | Purpose |
|---|---|
| `pc/tools/rdx.py` | room files: `list`, `dump`, `build`, `extract`, `pack`, `script` (disassembler), `tex`, `teximport`, `obj`, `census`, `grants` (items scripts hand over), `selftest` |
| `pc/tools/rdxbuild.py` | authoring compiler: `decompile`, `build`, `link`, `census`, `roundtrip` |
| `pc/tools/rdx_demo.py OUT` | generates the original Service Corridor source (no disc data) |
| `pc/tools/rdx_slopes.py OUT` | generates a slope test room (no disc data) |
| `pc/tools/rdx_bigroom.py OUT` | generates a large PC-budget stress room (no disc data) |
| `pc/tools/assets.py` | retail asset catalogue: `inventory [--previews] [--geometry]`, `search`, `item` |
| `pc/tools/kitbash.py` | module: bakes retail MLB nodes into authored room geometry |
| `pc/tools/build_area.py` | builds the Quarantine Annex (6 original rooms); `--source-only` without the disc |
| `pc/tools/build_village.py` | builds the village from retail assets (4 rooms + graveyard door) |
| `pc/tools/area_validate.py` | module: offline checks of the annex (used by `build_area.py`) |
| `pc/tools/village_validate.py` | offline checks of the village (`--iso`, `--mods`, `--source`) |
| `pc/tools/enemies.py` | enemy census: `room`, `census`, `sources`, `selftest` |
| `pc/tools/analyze_obj_surfaces.py` | measures an external OBJ (orientation, heights, materials) |
| `pc/tools/room_test.sh` | headless new game + teleport with frame dumps and log |
| `pc/tools/test_rdxbuild.py`, `test_area.py`, `test_assets.py`, `test_obj_surfaces.py` | disc-free unit tests |
| `pc/tools/area/annex.json` | annex design: rooms, flags, links |
| `pc/tools/area/village_assets.json` | hand-written semantic tags for `assets.py search` |
| `pc/tools/area/re4_materials.json` | RE4 material -> retail texture mapping |
| `pc/tools/re4/` | RE4 village conversion ([RE4_VILLAGE.md](RE4_VILLAGE.md)) |

### Developer tools (not modding)

| Tool | Purpose |
|---|---|
| `pc/tools/asm2c.py` | translates the decomp's inline EE/VU0 asm into C |
| `pc/tools/vsm2inc.py` | converts VU microcode listings into tables for `pc/src/ps2/vu.c` |
| `pc/tools/decomp_warnings.py` | regenerates the decomp warning lists in `docs/dev/` |
| `pc/tools/profile.py` | summarises a `RECVX_PROFILE` sample file |
| `pc/tools/compare_frames.py` | per-frame diff and montage of two frame dump directories |
| `pc/tools/compare_native.sh` | runs the emulated and native renderer headless and compares |
| `pc/tools/layout_check.sh` | determinism check of two binaries or layouts |

### Python requirements

* Python 3.9 or newer. `rdx.py`, `rdxbuild.py`, `assets.py`, `kitbash.py`, the generators,
  `build_area.py`, `build_village.py`, the validators and the tests use the **standard library
  only**.
* `pc/tools/re4/`: NumPy; the previews, contact sheets and several surveys also need Pillow;
  `slope_engine.py` needs a C compiler (`cc`).
* `compare_frames.py` / `compare_native.sh`: OpenCV (`cv2`) and NumPy.

## Randomizer (BioRand)

For now only with the `--dev` launch option. Without it, all of the following is hidden: the
BioRand settings, the BIORAND rows of the title menu and of NEW GAME, the BIORAND screen, the
lobby rule and the co-op offer. `mods/biorand/` is then not read, and nothing goes to GitHub.

[BioRand](https://github.com/biorand/classic) randomizes items, the start inventory, enemies and,
optionally, doors. It reads your disc image and writes a pack for this game into `mods/biorand/`,
with the spoiler log `log_pl0.txt`. You can make the pack in the game (below) or with BioRand's own
program.

### In the game: the BIORAND screen

On the title menu, choose **BIORAND** (the last row, after MULTIPLAYER). Keyboard, gamepad and mouse
all work. Cancel or a right click goes back to the title.

**The BioRand CLI.** The game uses BioRand's command-line program, the nightly build of
`biorand/classic`.

- The first time, the screen asks whether to download it (about 7 MB). The download is checked
  against GitHub's SHA-256 checksum. A damaged one is not installed.
- It is placed next to the game's program, as `biorand-cli.exe` (Linux: `biorand-cli`) with its
  licence `biorand-cli.LICENSE`. The game's folder must be writable.
- **CHECK FOR A NEW BIORAND CLI** (SEED page) asks GitHub for a newer nightly and offers it.
- The CLI needs 64-bit Windows 10 (1803 or later) or 11, or 64-bit Linux with glibc 2.34 or later
  (Ubuntu 22.04, SteamOS 3). On Linux the download also needs `curl` (or `wget`) and `tar`.

**The options** are BioRand's own, for Code: Veronica, on five pages. Switch pages with left/right
on the page row, or with L1/R1 (Q/W on the keyboard).

| page | what |
|---|---|
| SEED | the seed number; RANDOM SEED; COPY / PASTE SEED STRING; **GENERATE**; CHECK FOR A NEW BIORAND CLI. Below: the seed string |
| DOORS | Randomize Doors, segments, segment size, cutscene rooms; the estimated completion time |
| ITEMS | Randomize Items, routes, hidden key items, the custom item pool, the random starting inventory, the weapons; the item distribution |
| ENEMIES | Randomize Enemies, placements, difficulty, the enemy ratios; the enemy distribution |
| PORT EXTRAS | the port's own options (not in the seed string): **Randomize Played as**, which picks who you appear as for each seed; the cutscene cast |

- A greyed row says why in the help strip (for example "Randomize Doors sets this.").
- Every change is saved at once as the **seed string** (recvx.ini `biorand_seed`). The string
  holds the seed and every option, so the same string gives the same game.
- COPY puts the string on the clipboard. PASTE takes one from the clipboard, for example from
  BioRand's own program or from a friend.

**GENERATE** (SEED page) asks first, then runs the CLI with a progress bar. CANCEL stops it.

- It needs the game's disc image (the one the game runs from) and about 4 GB free in the mods
  folder: the pack takes 1.4 GB, and the CLI's temporary files (`mods/biorand-temp/`) take 2.4 GB
  while it runs.
- BioRand may use about 6 GB of memory for a short time.
- It takes a few seconds once the disc image is in the system's file cache. Otherwise the 1.8 GB
  image is read first, which takes longer (from a hard disk, tens of seconds).
- It replaces `mods/biorand/`. When it is done, BioRand is switched on: start a NEW GAME.
- The CLI's settings file is the game's own, `<save folder>/biorand/settings.json`. The settings of
  BioRand's own program stay untouched.

**What goes to GitHub.** Only when you choose to download or to check for an update, and only to
GitHub:

- one request to `api.github.com` for the nightly release's details;
- for a download, the archive from `github.com`, which redirects to `githubusercontent.com`.

Nothing else is sent: no seed, no options, no game files and no personal data. The requests say
`User-Agent: recvx-enhanced-port`, and GitHub sees your IP address, as with any download. GitHub
allows 60 such requests per hour from one address. Generating works offline.

### With BioRand's own program

1. In BioRand choose Code: Veronica, the PC version, your `.iso` as game data and this game's
   folder (with `recvx.exe` and `mods`) as the PC folder. Generate.
2. On NEW GAME's difficulty screen, the row **BIORAND ON / OFF** switches it.
   Without Difficulty select, use **BioRand** (GAMEPLAY) at the title.

### Playing

- BioRand can only be switched at the title. When it is off, the pack is ignored.
- Each generation replaces `mods/biorand/` completely; keep nothing else there.
- A save remembers its pack. Loading it with BioRand off, another seed, or BioRand on for a normal
  save asks first.
- **Co-op**: the host switches BIORAND in GAME RULES. The partner needs the same pack (same seed,
  options and BioRand build) to join.
  - The host sends its seed string. A partner whose pack differs is asked at the title or in the
    lobby whether to generate it. If it has no CLI yet, it is asked to download one first. If its CLI
    is not the host's build, it is offered the newest. Once the pack is generated, it joins.
  - The partner needs `--dev` too. Without it, the partner leaves with a message.
