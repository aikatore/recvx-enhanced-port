# Room format reference

How Code: Veronica X (NTSC-U, SLUS-20184) stores, loads and connects its rooms, and what the PC
port changes. For building rooms with the tools see [BUILDING_ROOMS.md](BUILDING_ROOMS.md); for
enemy models, motions and records see [../ACTORS.md](../ACTORS.md) and [../ENEMIES.md](../ENEMIES.md).

Source paths are relative to `decomp/src/ps2/veronica/prog/` unless noted; types are in
`decomp/include/ps2/veronica/prog/types.h` (`RDT_WORK` in `system.h`). Counts marked "census"
come from `pc/tools/rdx.py census` over all 205 room files of the disc. Nothing here is disc
content beyond counts, offsets and short operand examples.

## Summary

A room is one file `RM_SRRC.RDX` (S = stage 0..9, RR = room 00..99, C = case 0..9), stored
LZ-compressed in `RDX_LNK.AFS` and opened by name; the PC port looks in the
[mods folder](README.md#the-mods-folder) first. The expanded file is one relocatable blob: a
header, a room block pointing at fixed-size record arrays, then model, motion, script and texture
sections. Everything a room *does* beyond static records is in its **event script**, which
rewrites triggers, hides models, starts events and sets flags. Doors are trigger records or
script calls; there is no global room graph.

## Files on the disc

### Room files

* `RDX_LNK.AFS`: AFS archive, `"AFS\0"`, `u32 count` (205), then `count` x (`u32 offset, u32
  size`); data 2048-aligned; **no name table**. Entry *i* is `rdx_files[i]` in
  `ps2_dvd_image.c` (sorted names).
* Name: `system.c bhSysCallMonitor` mode 4 builds `rm_%1d%02d%1d.rdx` from `sys->stg_no`,
  `sys->rom_no`, `sys->rcase`; the name is upper-cased (`RM_1110.RDX`) before lookup.
  `gdlib.c GetFileSize` -> `ps2_sg_gd.c gdFsGetDirInfo` gets the size, `adxwrap.c
  OpenAfsIsoFile` maps the name to its `rdx_files[]` index and opens AFS partition 17 entry *i*.
  On the PS2 an unknown name prints `not found` and hangs; the port asks the mods folder first.
* **Cases**: files of one room that differ by the case digit (`RM_1000/1001/1002`) are story
  variants. `sys->rcase` is set by script op 55 `RoomCaseNo` (or op 8 `Sv` 21) before a
  transition and **not reset** by doors. A door into a room whose file does not exist for the
  current case leaves the loader waiting.
* Retail IDs: `rdx.py list` prints all 205 (stage 1 uses 1000..1140, so 1150..1990 are free
  there); every number not on the disc is free for a new room.

### Compression (`expand.c Expand`)

LZ with 8-bit descriptor fields read LSB first; a field byte is fetched when its first bit is
needed (the very first one before any data):

| Bits | Meaning |
|---|---|
| `1` | copy one literal byte |
| `0 0 a b` + byte `o` | copy `2a + b + 2` bytes (2..5) from `o - 256` back |
| `0 1` + u16 `w` (LE) | `w = 0`: end; else offset `(w >> 3) - 8192`, length `(w & 7) + 2` (3..9), or if `w & 7 = 0` a further byte `n`: length `n + 1` |

Copies may overlap (run-length). `rdx.py` has `expand()` and a greedy `compress()` (output
4..6% larger than the original, `expand(compress(x)) == x`; about 5 s per MB in CPython).

### Per-room data outside the room file

| What | Where | Key | Notes |
|---|---|---|---|
| Room sound bank | `MULTSPQ1/2.AFS` | `stage*1000 + room*10 + case`, looked up in `MULTSPQ?.IDX` | `sdfunc.c RequestRoomSoundBank`. An unknown key loads nothing and the previous banks stay (no hang). Case: script op 182 `RoomSoundCase`. The port can alias a key (`RM_xxxx.SPQ.key`, [README.md](README.md#the-mods-folder)) |
| Door animation | `MRY.AFS` entry `71 + door type` (MPAC pack) | `prm3` of the door trigger | `door.c bhReadDoorData`; type 255 or door attr bit 31 = load screen |
| Door sound | `DOOR_%03u.SPQ`, key `0x8000 \| type` | | `sdfunc.c RequestDoorSoundBank` |
| Map screen | `MRY.AFS` entry `ply_id` (index pack) and `5 + stage*6 + floor` (map models) | `stage*100 + room` (`map.c MapGetFloorNo`) | a new room has no map entry; opening the map there is untested and may be unsafe |
| View clip, clip volume, background colour per camera | tables in the executable | stage/room/case/cut | a new room number gets the defaults |

Music is started by script ops (`BgmOn2`, `SetNextRoomBgm`). Some C code tests `stg_no`/`rom_no`
directly (`system.c`, `objitm.c`, `sub1.c`, `effsub1*.c`, `en01sub.c`, `screen.c`, `event.c`):
special cases a clone at a new number does not get.

## Load path

`system.c bhSysCallMonitor` mode 4 (after a door or teleport):

1. case 0: `GetFileSize(name)`; `bhInitReadRDT`; the packed file is read to the top of the arena
   (`sys->rdtp = ALIGN_DOWN(endp - size, 64)`).
2. case 1: `Expand(rdtp, memp)`; the expanded room lies at the bottom. Both copies are in the
   arena at once.
3. case 2: `bhSetRDT` -> `room.c bhSetRoom`. Version check: the float at offset 0 must be
   >= 1.72 (retail: 10.0) or the "RDT version error" screen appears.
4. case 3: one texture list per frame (`bhSetMemPvpTexture`), then `room.c bhFinishRoom`.
5. case 10: `bhInitEvent` runs script `scd0` once; the room sound bank is requested.

`room.c bhSetRoom` copies the 1004-byte room block into the global `rom` and adds the file base
to its 16 section pointers (and each camera's area pointer); realizes the room model, every
enemy record with `flg & 1`, objects 4.., items; sets `rom->evtp = file + hed03`; builds the
texture upload list from `hed04`; and ends with `sys->memp` = start of the texture section:
everything after `hed04` is scratch after the uploads, so **the texture section must be last**.

`bhFinishRoom`: enemy and room motion sets, `O_WORK` arrays, lights, effects,
`bhResetAtariAttr`, player placed at `rom->posp[sys->pos_no]` (y from the floor table) unless a
save restores the position.

## Room file layout

All offsets are from the start of the expanded file, little-endian.

### Header (`RDT_WORK`, 0x80 bytes)

| Offset | Field | Content |
|---|---|---|
| 0x00 | `float ver` | 10.0 in retail (>= 1.72 required) |
| 0x04..0x0C | `flg, dat0, dat1` | 0 |
| 0x10 | `hed00` | room block (`ROM_WORK`); 0x80 in all rooms |
| 0x14 | `hed01` | model offset table |
| 0x18 | `hed02` | motion offset table |
| 0x1C | `hed03` | event script block |
| 0x20 | `hed04` | texture table (last section) |
| 0x24..0x5C | `hed05..hed19` | 0 |
| 0x60 | `usrname[32]` | author tag string (retail `N-MIKAMI`, `rdxbuild` from the tools); any other tag names a mods-only room in the teleport lists |

### Room block (`ROM_WORK`, 0x3EC bytes at `hed00`)

| Offset | Field | Meaning |
|---|---|---|
| 0x00..0x3C | 16 section offsets | `cutp lgtp enep objp itmp effp walp etcp flrp posp rutp ruttp evtp evcp mesp evlp` (0 = none; `evtp` unused, see `hed03`) |
| 0x80..0xB4 | counts | `cut lgt ene obj itm eff wal etc flr pos rut evc mes evl` (int each) |
| 0x100 | `flg` | retail 0..3: bit 1 = positional sounds muffled behind walls; bit 0 compared with `ss_flg & 2` at entry (meaning unclear). PC: bit 0x100 = player on slopes ([Floors and heights](#floors-and-heights)) |
| 0x104 | `bak_col` | background colour 0x00RRGGBB |
| 0x108..0x110 | `fog_col, fog_nr, fog_fr` | room fog; `fog_nr <= fog_fr` = off |
| 0x114..0x11C | `w, h, d` | `h` = height of walls whose own `h` is 0 |
| 0x120 | `grand[32]` | floor heights |
| 0x1A0 | `mdl` | runtime: realized room model |
| 0x3B8 | `amb_rom, amb_chr, amb_obj, amb_itm` | index 0..3 into the ambient colours, per draw class |
| 0x3BC | `amb_r[4], amb_g[4], amb_b[4]` | ambient colours |

Other words are 0. In retail files the record arrays lie between the room block and `hed01` in
the order of the offsets, and `hed00 < hed01 < ... < hed04`; the game only uses the offsets, so
tools must too.

| Array | Struct | Size | `rdx.py dump` key |
|---|---|---|---|
| `cutp` | `CUT_WORK` (+ `CUT_WRK` areas) | 0x2A8 | `cuts` (`areas` inside) |
| `lgtp`, `evlp` | `LGT_WORK` | 0xE0 | `lights`, `event_lights` |
| `enep`, `objp`, `itmp` | `ETTY_WORK` | 0x24 | `enemies`, `objects`, `items` |
| `effp` | `EF_WRK` | 0x44 | `effects` |
| `walp`, `etcp`, `flrp`, `rutp` | `ATR_WORK` | 0x24 | `walls`, `triggers`, `floors`, `routes` |
| `posp` | `POS` | 0x10 | `positions` |
| `evcp` | `EVC_WORK` | 0x808 | `event_cameras` |
| `ruttp` | `u8[rut_n][rut_n]` | | `route_next` |
| `mesp` | u32 table + u16 text | | `messages` |

Retail maxima (census): cameras 14, lights 44, enemy records 76, objects 29 (incl. 4
reserved), items 13, effects 195, walls 168, triggers 27, floors 47, positions 60, routes 95,
event cameras 64, messages 65, event lights 50.

### Section tables

* `hed01` models: u32 offsets: room model; one per enemy record with `flg & 1` (an enemy model
  list of `u32 size, data` chunks, `MASK`/`SKIN`/MLB, ended by `-1`); one per object record 4..
  (MLB, or `u32 skin size, u32, SKIN, MLB`); one per item record (MLB).
* `hed02` motions: one u32 per enemy record (0 = reuse the type's set), then the room motion set,
  mouth data and lip data; motion sets are `u32 size, MNB` lists ended by `-1`.
* `hed03` script block: see [Event scripts](#event-scripts).
* `hed04` textures: `u32 n`, then `n` offsets of texture lists **in upload order**: room model,
  each texture-owning enemy model (record order, first per `(type, mdlver, part)`), each object
  id (first object of an id), each item id, each effect id (30..99, 400+). `room.c` reads them in
  this order without skipping, so one list too many shifts every following owner onto its
  neighbour's textures (`assets.dependencies()` replays the ownership). In 204 retail rooms the
  last list ends with the file, without its `-1`.

### MLB models and the room model

`binfunc.c bhMlbBinRealize`: header `char[3]` + status byte (0x80 = chunk model), u16 header
size, u16 node count, s32 texlist offset, s32 object offset (after the header, -1 = none).
Objects are a flat `NJS_CNK_OBJECT` array (0x34 bytes: `evalflags, model, pos[3], ang[3],
scl[3], child, sibling`; pointers are offsets, -1 = NULL) in depth-first order. A model is
`NJS_CNK_MODEL` (`vlist, plist, center[3], r`; `r < 0` on disc = not yet relocated). Object
models (`bhSetObjMdl`) start with the `MDL` magic.

The room model is one MLB (retail up to 129 nodes); each node with geometry is one hideable
piece. Chunks used by all room models:

* vertex list: only type **51** (`njCnkCvVnPs2`): `u8 51, u8 flags, u16 size (words after this
  u32), u16 index offset, u16 count`, 56 unused bytes, then per vertex `float x, y, z, 1, nx, ny,
  nz, 1`; `0xFF` ends.
* poly list: `8` texture id (u16, `id & 0x1FFF` = texlist index), `19` material diffuse+ambient,
  `64` plain strips and **66** UV strips: `u8 66, u8 flags (bit 3 alpha, bit 5 flat), u16 size
  (2-byte units), u16 strip count (& 0x3FFF)`, then per strip `s16 n` (negative = reversed
  winding) and n x (`u16 vertex, s16 u, s16 v`), u/v in 1/1024; `0xFF` ends.
* The renderer supports only vertex chunks 41/51 and strip chunks 64..66; an importer should
  emit 51 + 8/19/66. Room UVs are clamped (all retail room UVs are in 0..1).

### Textures

`ps2_texture.c bhSetMemPvpTexture`: a list of chunks, each a 32-byte header (`u32 code, u32
payload size`, padding) + payload, ended by code -1:

* `"PLI\0"`: palette info: `u32 n`, n x 8 bytes; the second word is the CLUT placement (byte 0
  flags, byte 1 CLUT bank, 255 = allocate; u16 offset).
* `"TIM2"`: one picture with an extended header (0xD0): TIM2 file header with `Gindex` (global
  texture id, sharing key) at 0x08, the standard picture header at 0x80 (`TotalSize, ClutSize,
  ImageSize, HeaderSize, ClutColors, PictFormat, MipMaps, ClutType, ImageType, Width, Height,
  GsTex0...`), pixels at 0x80 + `HeaderSize`, then the CLUT (32-bit RGBA, alpha 0x80 = opaque).
  `ImageType` 4 (4 bit) or 5 (8 bit, CSM1 CLUT) only. `PictFormat` 5..8 = shared palette banks
  named by the PLI entries, other values = own CLUT. FormatId must be 1. Exact-black CLUT
  entries become transparent.

The k-th TIM2 of a list fills `texlist[first + k]`, so a strip's texture id is the index of its
TIM2 in the room list. Uploads go into the texture heap (see [Limits and budgets](#limits-and-budgets)).

## Records

Positions are floats in game units (about 10 cm; the player is ~17 units tall), y up. Angles
are 16 bit (65536 = 360 degrees); the player moves by `px -= sin(ay)`, `pz -= cos(ay)`, so `ay
0` faces -z, 16384 -x, 32768 +z, 49152 +x.

### Cameras

`CUT_WORK` (0x2A8): `flg` (bit 0 active), `type` (0 in all retail cuts), `flr_no`, `ctab_n`
(area count), `cuttp` (offset of the areas), `cx, cy, cz, cw, ch, cd` (box for tracking
cameras), `cam[2]` (`CAM_WRK`, 0xC4 each), `exd[256]`.

Switching (`cut.c bhCheckCut`, every frame): the first active cut with an area on the player's
floor containing the point wins. Areas are boxes `minx <= x < maxx, minz <= z < maxz` (`atr_tp`
0) or right triangles of the box (`atr_tp` 1..4). A switch needs all four probes at ±1 unit to
have left the current cut (hysteresis). Area `attr` 0x8000 selects `cam[1]`; bits 0x1, 0x8,
0x10, 0x20, 0x40 clamp the tracking reference to the area.

`CAM_WRK`: position `px py pz`; angles `ax ay az` (16-bit; positive `ax` pitches down; the view
direction is 180 degrees minus `ay`, so `ay 32768` looks along +z); `pers` (field of view as a
16-bit angle, 10012 = 55 degrees); `flg` (0x1 fixed angles, 0x2 smooth transition, 0x20 = `pers`
holds four corner FOVs in degrees, 0x4/0x40/0x80 further follow modes); tracking parameters
(`w`/`d` slide, `y0..y3` corner heights, `laz0..3`, `lax/lay`, `am_spd/aa_spd`); `hidobj[16]`
(bit *i*, MSB first per word, hides room-model node *i*); `hidlgt[8]` (lights 4..); `fog_col,
fog_nr, fog_fr` (signed camera-z: the common `0/-100` is not a positive distance); `fil_no/fil_rt`.

Event cameras (`EVC_WORK`, 0x808): `flg, type, nxt_no, keyf_n` + 16 key frames (0x80 each:
frame, position, angles, pers, hide masks, fog, actor link), driven by script ops 19 `CamSet` /
42 `CamSet2`.

### Walls

`ATR_WORK` (0x24): `u8 flg` (bit 0 active), `u8 type`, `u8 id`, `s8 flr_no`, `u32 attr`, `px py
pz` (min corner), `w h d` (extent; `h` 0 = `rom->h`), `prm0..3`. Checked by `hitchk.c
bhCheckWallEx` (player, zombies) and `bhCheckWall` (most enemies):

| type | Shape | Notes |
|---|---|---|
| 0, 1 | box `px..px+w` x `pz..pz+d`, height `py..py+h` | pushes out |
| 2, 3 | cylinder at (px, pz), radius `w` | |
| 4, 5 | right-triangle prism of the box, `id` 0..3 = orientation | |
| 6 | slope across the box rising by `h` from `py`; `id` = direction it rises: 0 +x, 1 -z, 2 +z, 3 -x | no push; sets y, see [Floors and heights](#floors-and-heights) |
| 7 | box that can be stepped over / climbed | |

Odd types are ignored by actors with `flg & 0x400`. `attr`: 0x1 = only on its own floor,
0x2/0x4 = ignored by some actors, 0x10000 + 0x40000..0x200000 = player direction specific, 0x40
= breakable/special. Scripts change walls with ops 10 `WalAtariSet` and 123 `WalAtariSet2`.

### Floors and heights

* Levels: `rom->grand[32]`. `pwksub.c bhSetFloorNum`: the floor number is `i - 2` for the
  highest `i` with `grand[i] != 0` and `py >= grand[i]` (`grand[2]` counts even when 0). So
  `grand[2]` is floor 0, `grand[3]` floor 1; `grand[31]` is forced to 0. Every record has a
  `flr_no` and is only active on that floor. On arrival y = `grand[flr_no + 2]`.
* Floor records (`flrp`, `ATR_WORK`): type 0 = climb/push area (`attr 0x1` probe in front,
  0x400..0x2000 direction limits); type 1 = footstep sound `prm0` (`attr 0x1`: only running);
  type 2 = enemy area; type 3 = effect area (`prm0` 1 water, 3 footstep effect, 4 special).
  Floor records do not move actors vertically.
* **Level rule.** Every frame the wall check of an actor with `flg & 0x100` (the player, walking
  enemies) ends with `y = grand[flr_no + 2]` when it touched no wall. There is no walkable mesh.
* **Stairs** are animations, not ramps: a type 1 trigger starts `player.c bhCPM2_act_kdu/kdd`
  (`prm0` 0 up / 1 down, `prm1` facing in 90-degree steps, `prm2` steps, 2 units each); the
  target floor is `bhCheckFloorNum(py ± 2 * prm2)`. Type 2 triggers do one step.
* **Slopes (PS2)** are type 6 walls. Enemies moving with `bhCheckWall` (`en02`, `en05`, `en10`,
  `en12`, `en15`, `en17`, `en19`, `en21`, `en23`, `bhEne00`) are projected onto the slope's
  centre line (which also drifts them slightly along it) and the floor number follows y.
  `bhGetGroundPosition` (shadows, falling objects) reads slope surfaces too. **The player
  ignores slopes on the PS2** (`bhCheckWallEx` has no case 6), as do zombies. Retail has 156
  slope records in 85 rooms, mostly under stairs the player climbs by animation.
* **Slopes for the player (PC rooms).** Gated by `ROM_WORK.flg` bit **0x100**
  (`pc_prelude.h PC_ROOM_SLOPES`), which `rdxbuild.py` sets in every `"budget": "pc"` room;
  retail rooms never have it. In such a room `hitchk.c pc_slope_ground` (end of `bhCheckWallEx`,
  actors with `flg & 0x100`) takes, among the active type 6 walls whose box holds x/z (`px <= x <
  px + w`, `pz <= z < pz + d`) and whose band holds the actor (`y + ah >= py` and `y - |ah| <= py
  + h`), the surface nearest to the current y: `py + h * s`, `s` = `u/w` (id 0), `1 - v/d` (1),
  `v/d` (2), `1 - u/w` (3), `u = x - px`, `v = z - pz`. y is set to it (no drift) and the floor
  number to `bhCheckFloorNum(y)`. Outside every slope the level rule applies. Entry positions
  inside a slope land on its surface; the camera tracks `plp->py`; the co-op partner's floor is
  taken from its y. Authoring rules: [BUILDING_ROOMS.md "Slopes"](BUILDING_ROOMS.md#slopes).

### Triggers

`hitchk.c bhCheckExmAtari` (action button) tests a point in front of the player (2 units for
item triggers) against each active trigger box (`etcp`, `ATR_WORK`) on the player's floor:

| type | Use | Parameters |
|---|---|---|
| 0 | **door** | `prm0` stage, `prm1` room, `prm2` entry position, `prm3` door animation type; `attr` -> `sys->door.flg` (bit 31 no animation, bits 7..8 animation case, 0x400/0x800/0x1000/0x2000 = not usable facing +z/-x/-z/+x) |
| 1 | stairs | `prm0` 0 up / else down, `prm2` steps |
| 2 | step | `prm0` 0 climb up, else drop down |
| 3 | examine / event | `attr 0x8000` = show message `prm1`; `attr 0x1/0x8` = fixed camera `prm0`; direction bits as type 0 |
| 4 | item pickup / push object | `prm0` = item slot (= item record index) unless `attr & 0x2` (object slot); `attr 0x1` = pickup animation |

Activating sets `sys->etc_idx` and `cb_flg` bit 23; scripts test "player activated trigger N".
**Scripts rewrite triggers**: op 37 `EtcAtariSet2` sets type/attr/prm of trigger N, op 11
`EtcAtariSet` switches it on/off, ops 179/180 place it. Most rooms rewrite their door and item
triggers in `scd0` or every frame in `scd1`, so an edit of the record alone often has no effect.

### Entry positions

`POS` (0x10): `px, py, pz, s32 ay`. The door/teleport `pos_no` indexes this list; y comes from
the floor level (or a slope in PC rooms).

### Enemies, objects, items

`ETTY_WORK` (0x24): `u32 flg, u16 id, u16 type, s8 flr_no, u8 mdlver, u8 wrk_no, s8 prm1, px,
py, pz, s16 ax, az, ay, aspd, s8 hide[4]`.

* Enemies: see [../ACTORS.md](../ACTORS.md). Record order and count are load-bearing; `wrk_no`
  must equal the record index (`EneSetCk`/`DieCk` test `ene[enep[rec].wrk_no]`; all 759 retail
  records do).
* Objects: records 0..3 are placeholders (player weapons and hair); 4.. are `sys->obwp[i]`.
* Items: record *i* -> `sys->itwp[i]`: `id` = item id (as in the inventory), `type`, `flr_no`,
  `mdlver`, `prm1` = draw type, position, angles, `aspd` (rotation speed). Model = the record's
  MLB; textures shared per item id. Pickup is a type 4 trigger with `prm0 = i` (`attr & 1`: picked
  up crouching); persistence is an `it_flg` bit named by script ops 14 `ItmCk` / 35 `ItmSetCk`.
* Generated pickups (PC only; BioRand writes rooms this way, every retail record has `type` 0 and
  `wrk_no` 0): a non-zero `type` below 156 is the item the pickup gives, while `id` stays the
  model shown (`hitchk.c bhCheckExmAtari`: `type ? type : id`); `wrk_no` is that pickup's amount
  (0: the game's table `getbulletmax`, as retail), for standing and crouching pickups alike
  (`sub1.c GetItem`). `objitm.c` takes the low byte of the u16 `type` field; BioRand writes
  `type = id` for records it leaves alone.

### Effects and lights

* Effects (`EF_WRK`, 0x44): `flg, id, type, flr_no, mdlver, position, scale, ay/ax, link (lkflg
  0 none / 1 player / 2 enemy / 3 object / 4 item / 5 effect, lkno, node, offset), param`;
  placed by `room.c bhSetEffectTable`. Ids 30..99 and 400+ take their textures from the room.
* Lights (`LGT_WORK`, 0xE0, first 29 words authored): `flg` (bit 1 on), `type`, link fields,
  `lsrc`, position, target, direction, `spc dif amb`, colour, range `nr fr`, cone `iang oang`.
  Lights 0..3 are global; 4.. can be hidden per camera. At most 3 point lights per frame are
  used. Script ops 53/75/115/116/120 change them. Geometry is lit by `amb_*` + lights (normals
  in the vertex data; no lightmaps).
* Routes: `rutp` boxes + `ruttp` next-hop matrix; from box A towards box B go to the centre of
  box `ruttp[B*n + A]` (`rutchk.c`).
* Messages (`mesp`): u32 table, entry `i + 1` = offset of message *i*; text is u16 glyphs
  (`ASCII - 32`, `0xFF01` space, `0xFF00` new line, `0xFF02` page, `0xFF04` choice, `0xFFFF` end).

## Event scripts

### Structure and execution

`hed03` points to u32 offsets relative to its own start: word 0 `scd0` (run once on entry),
word 1 `scd1` (run every frame), words 2.. = events 0.. (started by op 20 `EvtOn` into one of 16
tasks, run each frame until `EvtEnd`/`End`). The first code offset ends the table.

`bhScenarioJmpT[256]`: the byte at the pointer is the opcode; the handler reads its operands,
advances the pointer and returns 1 (continue) or 0 (stop). Conditions return 0 when false.

* `IfelCk n` (op 1, 2 bytes): opens a block; `n` = distance to its `Endif` or `ElseCk`.
  Conditions follow; several in a row = AND. `ElseCk n` (op 2): `n` = distance to the first
  instruction after the else part. `Endif` (op 3). `End` (op 0, 2 bytes).
* Loops: `For n`/`Next`, `While len` + condition/`Ewhile`, `Sleep n` (4 bytes, resumes after n
  frames).
* Controller ops 100 `PlCtr`, 102 `ObjCtr`, 103 `SubCtr`, 105 `CommonCtr`: 1 byte + a
  sub-command (own length table) acting on the work selected by `LoadWork` (op 101). An unknown
  sub-command is not consumed.

Lengths of all 213 opcodes and the sub-commands are in `pc/tools/rdx.py` (`OP_LEN`, `CTRL_LEN`,
`OP_NAMES`); a linear sweep ends exactly at every script's end in all 205 rooms. Operand
meanings: read the handler `bh<Name>` in `event.c`.

### Flags

`Ck`/`Set` (ops 4/5, 6 bytes: `op, set, u16 bit, u8 ?, u8 value/mode`) address bit `n` of a set:
word `n >> 5`, mask `0x80000000 >> (n & 31)`. Sets: 1 `ev_flg` (story, 1024), 2 `ky_flg` (512),
3 `ed_flg` (dead enemies, 1024), 4 `rm_flg`, 5 `st_flg`, 6 `sp_flg`, 7 `it_flg` (picked items,
512), 8 `mp_flg` (256), 9 `ic_flg`, 10 `cb_flg` (bits 22/23 + index = "player activated floor /
trigger N"), 11 `gm_flg`, 12 `ts_flg`, 13..15 player, 16 `ssd_flg`. `Ck` continues when (bit
set) != byte 5; `Set` byte 5: 0 set, 1 clear, 2 toggle. `ev/ky/ed/it/mp_flg` are saved.

Bits never referenced by retail scripts (`rdx.py census`): `ev_flg` 463..699, 701..803,
805..962, 964..1023; `it_flg` 452..511; `ed_flg` 358..1023; `ky_flg` 216..511; `mp_flg`
247..255. Some C code uses `ev_flg` bits directly (2, 57..66, 72, 75; `it_flg` 326): grep
`bhCkFlg(sys->ev_flg` before taking a bit. **Persistence flags must be below 512**:
`ItmSetCk`/`EneSetCk` mask their bit to 9 bits while `ItmCk`/`DieCk` use 10, so a higher bit is
set on pickup/death but tested as another bit on re-entry.

### Opcodes for room authoring

| Op | Name | Bytes | Operands |
|---|---|---|---|
| 0 | End | 2 | |
| 1/2/3 | IfelCk/ElseCk/Endif | 2 | skip distance |
| 4 | Ck | 6 | set, u16 bit, ?, expected |
| 5 | Set | 6 | set, u16 bit, ?, mode |
| 6 | CmpB | 4 | what (0 stage, 1 room, 2 cut, 8 sb_id, 15 pos, 17 weapon, 21 case, 23 game mode, 24 player, 25 costume), cmp (== > >= < <= !=), value |
| 8 | Sv | 4 | what (0 stage, 1 room, 2 cut, 15 pos, 16 floor, 18 weapon, 20 etc_idx, 21 case, ...), value |
| 11 | EtcAtariSet | 4 | trigger, off (non-zero) / on |
| 13 | DieCk | 4 | enemy record, u16 `ed_flg` bit: set when the enemy is dead |
| 14 | ItmCk | 6 | item slot, u16 `it_flg` bit, trigger, mode: on pickup set bit, disable trigger, hide item |
| 16 | UseItemCheck | 2 | item used on the active trigger |
| 20 | EvtOn | 4 | ?, task level, event number |
| 31 | MessageSet | 4 | type, message, ? |
| 34 | EneSetCk | 4 | enemy record, u16 `ed_flg` bit: hide the enemy if set |
| 35 | ItmSetCk | 6 | item slot, u16 `it_flg` bit, trigger, mode: if picked disable trigger + hide item, else enable trigger |
| 36 | InitModelSet | 4 | class (0 player, 1 enemy, 2 object, 3 item), index, 0 hide / 1 show |
| 37 | EtcAtariSet2 | 10 | trigger, u16 attr, prm0, prm1, prm2, prm3, type, pad |
| 51 | SetDoorCall | 8 | ?, u16 attr, stage, room, pos, door type: start a transition |
| 55 | RoomCaseNo | 2 | case for the next room file |
| 123/124 | WalAtariSet2/FlrAtariSet2 | 10 | as 37 for walls/floors |
| 149 | BgmOn2 | 4 | track, ... |
| 182 | RoomSoundCase | 2 | case of the next room sound bank |
| 199 (0xC7) | PlItemChangeM | 4 | old item, new item, count: the first inventory cell holding the old item (0: the first empty one) gets the new item, count 0. PC: old item 0xFF puts new item and count into the STANDARD slot (`sys->itm[ply*16 + 1]`, the lighter's) |
| 201 (0xC9) | PlItemTamaSet | 4 | item, u16 count: the first cell holding the item gets that count |

BioRand's start inventory is a block of room 0/0's entry script (`scd0` of `RM_0000.RDX`) run
before the retail one: `IfelCk`, `Ck ev_flg 1 clear` (a new game), then `C7 145 X 0` (the Playing
Manual's cell becomes X), `C7 0 X 0` (more items into empty cells), `C9 X n 0` (their counts) and
`C7 0xFF X n` (the STANDARD slot), `Endif`. A co-op guest that joins without a game of its own runs
the C7/C9 actions of that block itself (`pc/glue/biorand_glue.c`).

## Rooms, stages and the world

* Transition: a type 0 trigger copies its fields into `sys->door`; script op 51 `SetDoorCall`
  or the teleport calls `room.c bhSetDoorDemo(attr, stage, room, pos, type)` directly.
  `room.c bhStartDoorDemo` fades, sets `stg_no/rom_no/pos_no` and plays the door animation
  (`MRY.AFS 71 + type`, or the load screen with type 255, attr bit 31 or `--skip-doors`), then
  loads the room with the current `rcase`.
* Connectivity exists only in each room's triggers and scripts, which may change targets by
  story state (RM_1110 turns its door into an examine trigger once `ev_flg` 151 is set).
  `rdx.py dump` lists a room's door targets.
* Arrival: entry `pos_no` of the target; `scd0` runs (music, flags, hiding picked items and
  dead enemies), then `scd1` every frame.
* Saves keep `SYS_WORK` from `version` to `save_end` (flags, inventory, stage/room/pos, case,
  position); room records are never saved, so room edits apply to existing saves.
* No checksums on room files.

## Limits and budgets

### Memory and drawing

The PC raises these under `#ifdef TARGET_PC` (constants in `pc/include/pc_prelude.h`); retail
rooms load and draw as before. The compiler budgets follow from them
([BUILDING_ROOMS.md "Budgets"](BUILDING_ROOMS.md#budgets)).

| Resource | PS2 | PC | Overflow on the PS2 / on the PC |
|---|---|---|---|
| Room arena (`freemem`, `sys->memp..endp`) | 12.25 MB block, ~11.7 MB arena, ~10 MB free on arrival | **+64 MB** (`PC_ARENA_EXTRA`), ~74.6 MB free on arrival | no check, overwrites player data and vertex buffers / fatal error with sizes |
| Room load | packed (top) + expanded (bottom) at once | packed + expanded must fit the free arena | |
| Texture swizzle scratch (`Ps2_PXLCONV`, every texture over 128x128) | the arena's top 320 KB | own 1 MB block (`main.c`): the port's models load textures at any frame, also while a room file is read | a texture loaded during a room read damages it |
| After load | file up to the texture section + runtime allocations; the map/file/option screens copy the room model's textures above `memp` | same | |
| Vertex work buffer (64 B per vertex of the node being drawn) | 5120 entries | **8192** (`PC_VERTEX_EXTRA`) | writes into sound memory / node not drawn |
| Vertex chunk | one per node; u16 size: <= 8189 vertices | unchanged | |
| Strip chunk | size read as signed short: <= 32767 shorts | unchanged | corruption |
| Strip length | translucent <= 194 vertices, type 64 <= 200 | unchanged | static variables overwritten |
| Ordering table (sorted primitives per frame) | 8192 entries, 1.75 MB | **32768 entries, 4.5 MB** (`PC_OT_ENTRIES`, `PC_PBUFF_SIZE`) | overwrite / primitive dropped, logged once |
| Room-model nodes | u16 (65535); matrix stack 128 deep | unchanged | |
| Camera hide masks | cut cameras: nodes 0..511; event key frames 0..255 | nodes 512+ never hidden | random parts hidden |
| Texture heap / entries | 10 MB / 256 | 64 MB / 1024 (`PC_TEX_HEAP_SIZE`, `PC_TEX_ENTRIES`) | `exit(0)` |
| Texture lists per room | `txlp[256]` | unchanged | |

**Address window.** Game memory must lie in 0x30000000..0x3FFFFFFF (EE address aliases and
28-bit DMA addresses). The executable is linked at 0x30000000 and all static game memory is its
`.bss`; `pc/src/ps2/memory.c` stops at start if the image ends past 0x40000000. With the raised
budgets about 10 MB of the window remain, so more arena needs room elsewhere in the window
(host-only buffers moved to `malloc`, or a smaller texture heap). Host allocations need not be
in the window.

### Records

Counts are `int`, but byte-wide indices elsewhere (scripts, record fields, `SYS_WORK`, co-op
packets) make these file and save limits that the PC does not raise:

| Records | Limit | Why |
|---|---|---|
| Walls | 32767; 0..255 addressable by scripts | `psh_idx` is a short; `WalAtariSet(2)` take a byte; co-op syncs walls/triggers/floors 0..255 |
| Triggers, floors | 256 | `etc_idx`/`flr_idx` compared with a byte in scripts |
| Cameras | 255 (127 if listed in `hide[4]`); 255 areas per cut | byte operands, `ctab_n` u8 |
| Entry positions | 128 | `sys->pos_no` is a signed char |
| Routes | 255 | u8 ids, 255 = none |
| Lights / event lights | 256 / 128 | `hidlgt` masks, byte operands |
| Effects | 256 records | `efid[256]`; ids < 450 |
| Enemies / objects / items | 128 (slots incl. children; the PC has 255 slots, records stay within 128) / 32 (4 reserved) / 32 | runtime arrays |
| Floor levels | `flr_no` -2..28 | `grand[32]`, `grand[31]` = 0 |
| Messages, event cameras | 256; 16 key frames per event camera | byte operands |
| Names | stage 1 digit, room 2, case 1 | file name pattern |
| Version | float at offset 0 >= 1.72 | `bhSetRoom` |

### Whole-area rooms and drawing cost

The arena, not the format, limits a room (about 44 MB expanded with 20 MB packed). Nodes are
culled by bounding sphere and strips against the clip volume; the cost is per drawn vertex and
strip. Keep nodes spatially compact (cells of ~100-200 units) with hideable parts below index
512, and keep walls that scripts or co-op change below 256. A 130k-triangle room runs at the
30 Hz cap.

## Coordinates

* x and z horizontal, y up; ~10 units per metre; angles 65536 = 360 degrees, `ay 0` faces -z.
* Record boxes (`ATR_WORK`, cut areas): min corner + extent (`w` along x, `d` along z).
* Right-handed as rendered: looking along +z, +x is on the screen's left. `rdx.py obj` and
  `rdxbuild` write game coordinates unchanged; Blender (import "Y up, -Z forward") shows the
  room as the game does.
* Camera `ay`: view direction = 180 degrees minus `ay`.

## Unknowns

* Camera follow modes in detail, floor type 0 beyond "climb/push", light `type/lsrc`, `exd[256]`,
  room `flg` bit 0, `hide[4]` of items/enemies.
* Operand meanings of most script ops (lengths are known for all).
* Map screen data format and how a new room could get a map entry.
* Sound bank (`.SPQ`) format; a real bank for a new room needs `MULTSPQ?.IDX`, which is read
  through `ReadFileEx` and is not covered by the mods folder.
