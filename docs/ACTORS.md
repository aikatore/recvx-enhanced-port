# Actors

How players and enemies are loaded, spawned, animated and drawn, and how the port shows any
model as the player ("Character Model"). Paths are relative to `decomp/src/ps2/veronica/prog/`
unless noted; types are in `decomp/include/ps2/veronica/prog/types.h`. Related:
[ENEMIES.md](ENEMIES.md) (room enemies, budgets, F2 spawner, enemy options),
[COOP.md](COOP.md), [THIRDPERSON.md](THIRDPERSON.md),
[modding/ROOM_FORMAT.md](modding/ROOM_FORMAT.md).

## Summary

* All characters and enemies are `BH_PWORK` (0x580 bytes): the player is `ply` (`plp`), enemies
  `ene[]` (128; 255 on the PC). A model is an `ML_WORK`: a flat array of `NJS_CNK_OBJECT` nodes in depth-first
  order plus one `O_WORK` (matrix) per node. Motions (`MN_WORK`) are per-frame keys, one track
  per node, bound to the skeleton **purely by node index**.
* The four player files (Claire, Chris, Steve, Wesker; Claire's second costume) share **the same
  22-node hierarchy**. Their motions carry root position and rotations only, so each body keeps its
  own bone lengths and any of the meshes plays any other's motions.
* Enemy models, motions and textures are **not** in global files: each room file carries copies
  for its own records. Other types are loaded from another room file (the F2 spawner,
  [ENEMIES.md](ENEMIES.md) "Enemy spawner (F2)").
* Any enemy or NPC model can be shown as the player ("Character Model"): a display actor drawn in
  place of the player, retargeted from the player's skeleton.
* The cutscene cast randomizer draws the story characters as other models in cutscenes the same
  way, retargeted from their own skeletons ("Cutscene cast"). With "Randomize Voice Lines" a character shown as
  another story character says that one's lines, with their mouth movements ("Voice lines").

## Memory and load pipeline

### Main arena

* `main.c njUserInit`: `freemem = syMalloc(12845056)` (12.25 MB). Vertex/view buffers live at
  the top; `sys->endp = njpmemp`.
* `system.c bhFirstGameStart`: `sys->obwp` (32 `O_WRK` objects, `[0..3]` reserved for the
  player's weapons/hair) and `sys->itwp` (32 `O_WRK` room items).
* `bhSysCallMonitor` mode 1 (game start / load): reads sys file 1 (common data), then
  `bhInitPlayer` allocates the **persistent player buffers** and sets `sys->mempb = sys->memp`.
  Everything above `mempb` is per room and discarded at every room change.
* Allocators: `pwksub.c bhGetFreeMemory` (64-byte aligned bump from `sys->memp`, refuses only
  beyond `freemem + 12.25 MB`) and `eneset.c bhEne_CallocWork` (refuses beyond
  `mempb + 0xC40000`); on the PC both limits grow by `PC_ARENA_EXTRA`. **Neither checks
  `sys->endp`**: an overrun writes into the vertex buffers. Nothing is freed before the next room.

Player buffers (`player.c bhInitPlayer`):

| Pointer | Size | Content |
|---|---|---|
| `sys->plmdlp` | 131072 | player model block (all sub-models) + their `O_WORK` arrays |
| `sys->wrmdlp` / `wlmdlp` | 32768 each | right / left hand weapon model |
| `sys->plmthp` | 12288 | 512 `MN_WORK` (motion headers), `plp->mnwPb` |
| `sys->plbmtp` | 393216 | base motion data |
| `sys->plwmtp` | 65536 | weapon motion data (motions 100+) |
| `sys->plzmtp` | 8192 | per-motion forward translation (`player.c bhGetTransZ`) |
| `sys->plexwp` / `plhdwp` / `pletcp` | 124 / 124 / 32768 | `plp->exp0`, `exp1` (head/eye), `exp3` |

### Room load

`system.c bhSysCallMonitor` mode 4: `rm_SRRC.rdx` (stage, room, case) is read to `endp - size`
and `Expand`ed to `sys->memp`; `room.c bhSetRoom` realizes the room model, **every enemy
record's model** (`bhSetEneMdl`), objects and items and builds the texture upload list (max 256,
one `bhSetMemPvpTexture` per frame); `bhFinishRoom` adds enemy motion sets (`bhSetEneMtn`), the
room motion set (`sys->rmthp`), `O_WORK`s and the player placement. `sys->memp` is left at the
start of the texture section, which is reused after upload. A story character switch
(`cb_flg & 0x80`, op 137 `bhPlayerChangeSet`) reads the new part and weapon files here. Then
`bhInitEvent` starts the room script and the room's sound bank is requested. The largest
expanded room file is 8.9 MB.

### Textures

* `ps2_texture.c bhSetMemPvpTexture(texlist, data, offset)` walks `PLI`/`TIM2` chunks and calls
  `njLoadTexture` per texture. Entries are keyed by the TIM2 global index (`Gindex`) + CLUT bank:
  the same `Gindex` loaded twice is shared and refcounted, which is why two instances of a type,
  or Claire and Chris in co-op, coexist.
* `Ps2TextureMalloc` copies the TIM2 into the texture heap (`Ps2_tex_buff`), so source buffers may
  be freed after upload. Table `main.c tbuf`. A full table or heap calls `exit(0)`. PS2: 10 MB,
  256 entries; PC: 64 MB, 1024 entries (`PC_TEX_HEAP_SIZE`, `PC_TEX_ENTRIES`).
* Palettes: each `PLI` entry carries a fixed CLUT bank id (`bk_id`, 255 = allocate from
  `sys->ef_pbnk`). Loading a model into a room it was not authored for can overwrite another
  asset's palette (wrong colours); not observed so far.
* Release: `bhReleaseMainTexture` (door) releases the room model, the textures of every **room
  record's** enemy (owners with `mdl[j].flg & 0x200`), objects and items. Anything loaded outside
  that list (spawned foreign enemies, display actors) is released by its loader.
  `njReleaseTextureAll` (title, game over) resets everything.

## Models and skeletons

### Structures

* `ML_WORK` (0x18): `flg` (`0x80` = chunk model; `0x200` = this instance owns the textures),
  `obj_num`, `datP`, `objP` (first `NJS_CNK_OBJECT`), `texP` (`NJS_TEXLIST`), `owP`
  (`O_WORK[obj_num]`).
* `NJS_CNK_OBJECT` (0x34): `evalflags, model, pos[3], ang[3], scl[3], child, sibling`. `pos/ang`
  are the **live pose**: motions overwrite them every frame.
* `O_WORK` (0x50): `flg` + world matrix `mtx[16]`. `flg` bits: `0x1` matrix set externally
  (`bhCalcTree` keeps it), `0x2` motion does not write this node's rotation (head look, aiming),
  `0x4` node has no motion track, `0x8` blend in YZX order (player upper arms 7 and 11).
* `BH_PWORK`: `mdl[16]` (sub-models), `mdl_n`, `mlwP` (the one drawn/animated), `skp[16]` (skin
  per sub-model), `mbp[16]`/`txp[16]` (copies of `objP`/`texP`), `mnwP` (current motion set) /
  `mnwPb` (own set), `mtn_no`, `frm_no`, link fields `lkwkp`, `lkono`, `lox/loy/loz`,
  `clp_jno[8]` (view clipping nodes), `cpcl` (hit capsules). `O_WRK` (objects, weapons, effects)
  has the same layout up to `0x3E4`, so the model functions take either.

### Binary format and hierarchy

`binfunc.c bhMlbBinRealize(bin, mlw)`: header `char[3]` + status byte, `u16[2]` header size,
`u16[3]` `obj_num`, `int[2]` texlist offset, `int[3]` object offset (relative to `bin + header
size`, `-1` = none). Objects are **one contiguous array**, relocated in place (a buffer can be
realized only once). `bhMnbBinRealize`: `NJS_MOTION` and per node an `NJS_MDATA2` pair (position
keys / rotation keys), plus optional per-frame attributes (`atrP` -> `mtn_attr`).

`MdlPut.c bhCalcModel`: base matrix = translate(`px..`) * rotateXYZ(`ax..`), or with `flg & 0x80`
the linked parent's node matrix `lkwkp->mlwP->owP[lkono].mtx` * translate(`lox..`) * rotate.
`bhCalcTree` walks the **flat array in order** (push on sibling, pop without child), so array
order is depth-first tree order and parents are implicit.

### Drawing

`MdlPut.c bhPutModel`: chunk models with skin data go through `njplus.c npCalcSkin` (or
`npCalcSkinFM` with `mdflg & 0x100`), which blends skinned vertices from the node matrices, then
`bhJumpCnkDraw[draw_tp]` draws each node's model with `owP[i].mtx`; mirror rooms
(`st_flg & 0x100`) add a mirror pass. Skin data is converted once against the model's own bind
pose (`npSkinConvert`), so it belongs to its model. `game.c bhAllDrawModel` draws room, enemy
objects, items, effects, enemies, then the player (unless `stflg & 0x1000000`). The shadow is a
blob, not model-dependent.

### Player part file

Sys AFS partition, file `ply_id + 10 + costume*4`, read by `dread.c bhReadPlayerData` (input at
`sys->memp`):

```
u32 size; model block      -> copied to sys->plmdlp
    repeated: u32 n, n bytes (an MLB, or "SKIN" skin data for the next MLB); u32 -1
u32 size; motion block     -> copied to sys->plbmtp, realized into sys->plmthp[0..]
    repeated: u32 n, n bytes (an MNB; n = 0 means an empty slot); u32 -1
u32 size; z-translation    -> copied to sys->plzmtp
per sub-model with a texlist: u32 size (bit31 = 32-byte align), PLI/TIM2 data -> bhSetMemPvpTexture
```

`O_WORK` arrays are placed behind the model data inside `plmdlp`; `sys->hd_pos = objP[5].pos`.
The loader does **not** clear `plp->skp[]`.

| File | Who | Sub-models (`mdl[i]`: nodes) | Motion slots / used |
|---|---|---|---|
| 10 | Claire (`ply_id` 0) | 0: 22 skinned body, 1/2: texlist only, 3: 22 untextured, 4: 4 skinned (hair), 5: 1, 6: 2, 7: 5 skinned | 111 / 60 |
| 11 | Chris (1) | 0: 22 skinned, 1/2: texlists, 3: 22 untextured | 105 / 55 |
| 12 | Steve (2) | as Chris | 105 / 56 |
| 13 | Wesker (3) | as Chris + 4: 1 node (sunglasses) | 106 / 55 |
| 14 | Claire, costume 1 | 0..3 as above, 4: 4 skinned (hair) | 107 / 60 |
| 15..19 | other costumes | 11-byte placeholders | |

* `mdl[0]` (drawn): **one skinned mesh on node 1**; the other nodes are bones without geometry.
  `mdl[1]`, `mdl[2]`: texlists for eye blink (`plp->mlwP->texP = plp->txp[PlyEyeTab[..]]`).
* `mdl[3]`: untextured copy with a rigid mesh per node, for line/ray hits against the player
  (`bhCollisionCheckL2MDL`).
* `mdl[4]`: Claire's hair / Wesker's piece: object `sys->obwp[2]` linked to head node 5 and
  simulated by `player.c bhCalcHair` (only `ply_id` 0 and 3).
* `mdl[6]`/`mdl[7]` (Claire): used by the moth egg (id 27: `plp->mdl[6]`, `plp->skp[7]`, player
  motions 93..95).

### The player skeleton

Identical in files 10..14:

```
0 -> 1 -> 2 -> 3 -> 4 -> 5                spine, neck, head
               3 -> 6 -> 7 -> 8 -> 9      right arm  (6, 10 are siblings of 4)
               3 -> 10 -> 11 -> 12 -> 13  left arm
          1 -> 14 -> 15 -> 16 -> 17       leg        (14, 18 are siblings of 2)
          1 -> 18 -> 19 -> 20 -> 21       leg
```

| Node | Role |
|---|---|
| 0 | root (root motion position, `clp_jno`) |
| 1 | pelvis, carries the body mesh |
| 2, 3 | spine, chest |
| 4 | neck (procedural look) |
| 5 | head (`hd_pos`, camera look-at, hair link) |
| 6..9 | right arm: shoulder, upper arm, forearm, hand (right weapon `lkono = 9`) |
| 10..13 | left arm (left weapon `lkono = 13`) |
| 14..17, 18..21 | legs (`PlyLegRoute`); 17 and 21 = feet (footstep sound/splash) |

`PlyFlip` (`player.c`) is the left/right map for mirrored motions (`mtn_md & 2`). Bone offsets
differ slightly per character; `PlyInfo[ply_id]` holds collision radius/height (Claire 16.5,
others 17.5).

### Attachments

* Weapons: `sys->obwp[0]` (right, node 9) and `[1]` (left, node 13), `flg 0x81` (linked),
  `weapon.c bhSetWeapon`; grip angles `WpnAngTab`, muzzle positions `WpnTab`.
* Enemies link to the player or to each other the same way (`flg & 0x80`, `lkwkp`, `lkono`).
* View clipping uses `clp_jno` (player 0, 5, 9, 13).
* Hard-coded player node numbers appear in ~40 places (`player.c`, `pl_evt.c`, `cut.c`,
  `weapon.c`, enemy grab code): 1, 2, 3, 4, 5, 7, 8, 9, 11, 12, 13, 15, 17, 19, 21.

### What makes two models animation-compatible

1. **Same node order and parentage**: motions address node `k` by position `k`.
2. **Node count >= track count**: `SetMtn*` writes `objP[k]` for every track with no bound check
   (memory corruption otherwise). Player motions have 17..22 tracks.
3. Similar bind orientation per joint (rotations are absolute local Euler angles). Proportions may
   differ: player motions carry no position keys below the root.
4. For the player: nodes 5, 9, 13, 17, 21 must be head, hands and feet, and the file should
   provide `mdl[1..3]`.

## Animation

### Playback

`Motion.c bhSetMotion(ew, add, mode, flip_tbl)`, once per 30 Hz tick, followed by
`bhCalcModel`:

* `frm_no` is 16.16 fixed point; `add` (`mtn_add`, usually 65536) advances it; wrap returns 1
  (end) or -1 (start). `mode & 1` ping-pongs.
* Track writers: *Fast* (default: root position, rotations for all nodes), *Normal*
  (`flg & 0x10`: position keys for every node that has them), *Slow* (`flg & 0x20` or `mode & 8`:
  linear interpolation by the fraction of `frm_no`).
* Blending ("hokan"): while `hokan_count > 0`, each node moves towards the target by
  `(65536 - hokan_rate) / 65536` per tick. Changing motion = set `mtn_no`, `frm_no = 0`,
  `hokan_count`, `hokan_rate`.
* `mode & 2` mirrors through `flip_tbl`; `atrP[frame]` -> `mtn_attr` (per-frame events).

### Where motion sets come from

| Set | Pointer | Source | Used for |
|---|---|---|---|
| Player base | `plp->mnwPb` = `sys->plmthp` | part file | walk/run/aim/damage/... |
| Player weapon | `plp->mnwP[100..]` | weapon file `wpnr_no + ply_id*30 + 20` (`dread.c bhReadWeaponData`) | stances and firing; weapon 0 files (and Claire's weapon 1) have no motions |
| Room | `sys->rmthp` | room file `hed02` after the enemy entries | event acting of the player and of NPCs |
| Enemy | `ep->mnwPb`, shared per type `sys->emtp[id]` | room file, one entry per record | enemy actions |
| Player in a grab | `plp->mnwP = ep->mnwP` | inside the enemy's set | the player is animated from the enemy's set, chosen by `ply_id` (zombie `{400, 425, 450, 425}`, bat 30..38, ...) |

### Player motion selection

`player.c bhControlPlayer` -> `bhCtrPly_mode0[mode0]` -> handlers `bhCPM2_act_*`. Locomotion
numbers come from `PlMtnAct[wpntp][dmlvl][k]` (`wpntp` 0 below weapon 10 else 1; `dmlvl` health
level fine/caution/danger; e.g. standing = 42/43/44); weapon stances `PlMtnWpn = {100, 104, 109,
114, 101}`. Numbers are shared by all four characters.

Enemy code sets `ep->mtn_no`, `hokan_*`, `mtn_add` and calls `bhSetMotion` itself. The first
record of a type in a room carries the set, later records reuse it; children often share the
parent's set.

## Player mesh replacement

| Option | Result |
|---|---|
| A. Another character's part file as mesh, own motions kept | works for any combination of the five player files; what co-op and "Character Model" use for player files (`coop_glue.c self_load`) |
| B. Load another character's file while `ply_id` stays | that character's mesh **and** motions, the played character's logic and weapon files |
| C. Change `sys->ply_id` | full character switch (as `bhPlayerChangeSet`); Wesker (3) overruns 3-row enemy grab tables (`en02/en07/en13/en23 mtn[3][..]`) |
| D. Enemy/NPC mesh as player | "Character Model" (display actor, below) |
| E. Custom mesh | a new chunk model + `SKIN` weights against the 22 bones, `mdl[1..3]`, TIM2 textures with unused `Gindex`/CLUT banks; no tool exists |

### Option A

`coop_glue.c self_load` (our player with a player file as "Character Model"; `p2_other_mesh` for the
co-op partner):

1. Save the original's `mdl[]`, `skp[]`, `mbp[]`, `txp[]`; clear `plp->skp[]` (the loader would
   apply stale skin pointers).
2. Read the part file and run the game's loader (`read_player` -> `player_model_read`, the
   `bhReadPlayerData` layout) with the model going into a buffer of its own (`self_mdl`, 128 KB)
   and the motions into a sink; restore `plp->mnwP`/`mnwPb`, so the original motions (incl. 100+)
   stay. The original model data in `sys->plmdlp` and its textures stay loaded.
3. Sub-models only the original has (Claire's 5..7: the moth egg takes `mdl[6]`, `skp[7]`) keep
   the original's; `owP[7]`/`owP[11]` get `flg |= 0x8` as in `bhSetPlayer`; the hair/sunglasses
   object is set up for the new mesh. Putting the original back copies the saved `ML_WORK`s.
   The partner (`load_player`) has one model buffer: the mesh is loaded behind the motion file's
   model, so the motion file's extra sub-models stay, with their textures (before, the mesh
   overwrote them: an egg laid on a Claire partner shown as Chris, Steve or Wesker took other bytes).

* When the game reloads the player (game start, load, continue, story switch) the mesh is applied
  again at the next frame in control. The original is put back before the game lets go of the
  player: when a game ends (`coop_textures_released`) and before a character switch (the story's,
  each Battle Game room) releases the player's textures (`system.c bhSysCallMonitor`,
  `coop_player_unload`). The game then releases the original's textures and we the mesh's; the
  original's texture lists are gone after that (the new load overwrites them).
* Keeping `sys->ply_id` keeps everything else: `PlyInfo` size, weapons (they sit in the new hand
  node 9), inventory, enemy grab motions, room event motions. The voice follows the model shown
  ("Voices").
* The loader never clears `mdl[]` entries beyond the new `mdl_n`; stale entries must not be used.
* Cutscenes: op 52 `ObjLinkSetPly` re-attaches `sys->obwp[2]` at Claire's ponytail offset for
  every character, and 8 room motions (rooms 9090, 9370) carry position keys below the root.
  `coop_glue.c self_keep` puts the head part's own link fields and the mesh's own node offsets
  1..21 back every tick.
* Changing the mesh in game: `bhFixPosition` (motions that plant a foot) moves the player by the
  difference between last tick's toe matrix and the new pose's; `self_carry_pose` gives the new
  model the old one's node angles and root offset and computes its matrices at the player's base
  matrix, so the player does not jump.

## Enemies

Room records, spawning, budgets and co-op: [ENEMIES.md](ENEMIES.md).

### Data flow

Room file records (`ETTY_WORK`/`EGG_WORK` 0x24: `flg`, `id` (type), `type` (variant), `flr_no`,
`mdlver` (model version), `wrk_no` (slot), position, angles, `hide[4]`) are realized by
`room.c bhSetRoom` -> `bhSetEneMdl` -> `eneset.c bhSetEnemy` (first free slot). Model chunks:
`"MASK"` -> face mask `mskp`, `"SKIN"` -> `skp`, else an MLB into `ep->mdl[]`. Textures are shared
per `et_lp[id][mdlver][k]`; the first record owns them (`flg |= 0x200`). `bhFinishRoom` adds the
motion set (`sys->emtp[id]`), `O_WORK`s and `bhInitMask` for ids > 90.

### Enemy types

`bhJumpEnemy` (100 entries, `eneset.c`); code in `enNN.c` unless noted, names from the
`// ENEMY:` comments:

| id | Name | id | Name |
|---|---|---|---|
| 0 | exploding static object, wall collider (`eneset.c bhEne00`) | 17 | Monster Steve |
| 1 | Zombie (16 variants; parts as helpers 31..38) | 18 | Alexia's Tentacle |
| 2 | Gulp Worm | 19 | Tyrant T-078 |
| 3 | Black Widow (helpers 31, babies 24) | 21, 22 | Albinoid Infant, Adult |
| 4 | Cerberus | 23 | Giant Black Widow |
| 5 | Hunter/Sweeper | 24 | Baby Black Widow |
| 6 | Moth (lays 27) | 25 | Ceiling Trap |
| 7 | Bat | 26 | Anatomist Zombie |
| 8 | Fish | 27 | Moth Egg (on the player; player model/motions) |
| 9 | Bandersnatch | 28 | empty (`subpl.c`) |
| 10, 20, 71 | Ant A, B, C | 29 | Tentacle |
| 11 | Spotter | 30 | Alexia's Baby |
| 12, 13, 14 | Alexia first, second (spawns 18), third form | 53 | Nosferatu's core (`en15.c bhEne53`) |
| 15 | Nosferatu | 54, 55 | Armored Knight A (sword), B (lance) |
| 16 | Alfred Ashford | 56 | scripted object (`eneset.c bhEne56`) |

31..52, 57..70, 72..99: `subpl.c bhSubpl`, scripted NPC actors (room-script motions, no AI);
41..48 and 91..98 get player-like clip tables, ids > 90 face masks and lip sync.

Slots 31..38 are overwritten at run time with helper functions (`bhEne_SetCallFunc`): 31 by ids
2, 3, 5, 6, 13, 23 (17: 31, 32), 31..38 by id 1. On the PS2 the table is global and never
restored; the PC runs each part with its creator's function in rooms with added enemies
([ENEMIES.md](ENEMIES.md) "Budgets on the PC"). Ids 18, 27, 28 never appear in room data.

### Cloning a type the room already loaded

`bhSetEnemy` on a copy of an existing record of the type, then share its `mdl[]` (clear
`flg & 0x200`), give each sub-model its own `owP` (`bhEne_CallocWork`), copy
`skp`/`mbp`/`txp`/`mskp`, set `mnwP = mnwPb = sys->emtp[id]` and `bhCalcModel`; Init runs on the
next tick. Children share `objP` the same way (`en03.c`). Foreign types go through the spawner's
loader ([ENEMIES.md](ENEMIES.md) "Enemy spawner (F2)").

### Items and flags (for randomizers)

* Items: room records `rom->itmp` -> `sys->itwp[i]` (`objitm.c bhSetItem`, `id` = inventory id
  0..155). Pickup: an `etc` atari of type 4 whose `prm0` is the item slot (`sys->sb_id =
  itwp[prm0].id`). Ops 14 `bhItmCk` / 35 `bhItmSetCk` bind the `it_flg` bit to the trigger, not the
  id, so changing `itwp[i].id` after load keeps persistence (the 3D model stays the record's).
* Locks are room-script checks (ops 16 `bhUseItemCheck`, 17 `bhPlItemCheck`, 77
  `bhPlItemBlockCk`, `ky_flg`); `map.c ItmDat` lists 22 key items. Flags (`SYS_WORK`): `ev_flg`
  story, `it_flg` items, `ed_flg` dead enemies, `ky_flg` locks, `mp_flg` map, `ic_flg` item checks.

## You appear as

"Character Model" (F3 window, the lobby's MODEL page, F1 and ENHANCED SETTINGS -> APPEARANCE; ini `coop_char`; or,
with "Character Models" *Per Character*, "Claire's Model" and "Chris's Model", "Per Character" below) shows
our player as one of the player files or one of 62 models found in the room files: the story
cast, the creatures, the zombie variants (`pc/src/host/actors.c`). It applies in single player and
co-op (the partner sees it too) and in cutscenes to the event double of the character played
(unless "Custom Cutscene Model" is off, see "Cutscene doubles").
Models imported from other games' discs (F9) are listed after the
catalogue: chr `COOP_CHR_IMPORT + k`, actor index `ACTOR_MODELS + k`, skinned every frame
([IMPORT_OUTBREAK.md](IMPORT_OUTBREAK.md#conversion)).

### Per Character

"Character Models" (APPEARANCE, F1, F3 and the lobby's MODELS; ini `appear_per_char`, default 0):

| Value | Rows | Who gets what |
|---|---|---|
| **Same for Both** (0) | "Character Model" (`coop_char`) | that model for whoever is played (as before) |
| **Per Character** (1) | "Claire's Model" (`appear_claire`), "Chris's Model" (`appear_chris`) | Claire hers, Chris his; Steve (his short section) and Wesker (the Battle Game) as themselves ("Current Character") |

Both per-character rows offer the same list ("The list" below, imports and "Current Character (Cutscene)" too);
in Claire's row "Current Character" is Claire herself. Their ini value -1 (the default) means "not chosen yet":
`coop_char`'s model, so switching to Per Character starts with the model chosen so far for both. Steve's section
keeps his own look so that Claire, in the same scenes (3210), does not show the model a second time.
`actors.c appear_choice(one, c)` (ctest `appear`) resolves it, and `coop_glue.c look_of(c)` is the one place that
asks: our model (`my_chr`: the character played now, so a story switch changes the model), the cutscene doubles
(`actor_doubles`), Claire's or Chris's records in the other one's part (`coop_look_model`, "Cutscene cast"), the
voice, third person and the co-op pose (`Pose.chr`, the lobby's `lchr`). There is no random choice in the list
(BioRand's "Randomize Played as" is the only random look).

**Precedence**: a BioRand game's picks ("Randomize Played as", stored in its saves) come first for the
characters they cover, in either mode; choosing a model in game (any of the three rows; not the mode switch) drops
them, as before. A co-op guest never has them.

### The list

The menus list one entry per distinct look: "Current Character", "Current Character (Cutscene)", the five player
files, 50 catalogue models and, last, the player characters' four cutscene models (61 entries instead
of 69). They share
`actors.c` `appear_count`, `appear_at`, `appear_pos`, `appear_step`, `appear_name`, `appear_canon`;
the list is filtered and ordered for display only, so catalogue indices, `g_coop.chr`, `Pose.chr`
and the cutscene doubles are unaffected (no protocol change). A hidden value in `recvx.ini` is
mapped to its listed twin when the file is read. Test: ctest `appear` (`pc/tests/test_appear.c`).

Left out of the menus (`twins[]`):

| Hidden (catalogue index) | Shown instead | Why |
|---|---|---|
| Chris (event model) 2 | Chris | same look |
| Steve (event model) 4 | Steve | same look |
| Wesker (event models) 6, 7 | Wesker | same look; the doubles wear the sunglasses as a separate prop |
| Alfred Ashford 2 (9), (event model) 11 | Alfred Ashford (8) | same uniform |
| Tyrant 2 (18) | Tyrant (17) | one extra blood stain |
| Bandersnatch 2 (23) | Bandersnatch (22) | only its long arm (event piece) |

Kept although related: "Alfred Ashford (Wounded)", "Hunter (Sweeper)" and every zombie variant.
Order: Current Character, the player files, the cast, the creatures, the zombies, then the cutscene models
of Claire (0), Chris (1), Steve (3) and Wesker (5) (`tail[]`): listed next to the player files,
each would follow the same character's file. They are the event doubles' meshes: no hands (the
unarmed pieces are drawn, as for the rest of the cast), and no head part of their own (in
cutscenes the scripts link the player's `sys->obwp[2]` to the double); the display actor brings
Claire's ponytail and Wesker's sunglasses itself ("Head parts" below). Earlier versions hid them as
twins of the player files; their stored values stayed valid.

**Current Character (Cutscene)** (second in every list): the cutscene model of the character played now:
Claire's (0) while Claire is played, Chris's (1), Steve's (3) or Wesker's (5, the Battle Game). Its
value is a fixed id, `COOP_CHR_CINE` 4095 (past every import), so no entry was renumbered: the
list's display order puts it second (`appear_at(1)`, `appear_pos`), and an old `coop_char` keeps its
model. `appear_played(chr, sys->ply_id)` turns it into that catalogue entry wherever the choice is
used (`coop_glue.c my_chr`: our model, the cutscene doubles, the voice, the co-op pose; the lobby
preview with the character of the chosen save, else Claire), every frame, so it follows a story
switch (Claire -> Chris) as "Current Character" does. From there everything is that cutscene model's: the
gender-matched motions, the hand pieces, the voice, Claire's ponytail, and in cutscenes the
game's own double (the played character's own cutscene model replaces no double, see "Cutscene
doubles"). Costumes: there is one cutscene model per character, so Claire's second costume and any
other costume are shown as that model too (the entry is chosen for it; "Current Character" keeps the
costume). Co-op: the partner gets the resolved cutscene model (`Pose.chr` 4 + entry, `lchr` the
entry), as if it had been chosen; no protocol change, and an older partner shows it too. An older
build reading the id finds it out of range and keeps "Current Character".

### Head parts

Claire's cutscene model is shown with her ponytail, Wesker's (and his hidden event models) with his
sunglasses, wherever a display actor shows them: our player, the co-op partner, the
cutscene double ("Custom Cutscene Model" on) and the lobby previews (`actor_glue.c load_head`,
`head_pose`, `head_draw`).

* **Source**: sub-model 4 of the character's part file (`lkmtab`: 1211 the ponytail, 1212 the
  sunglasses; Claire's costume 0, file 10, and Wesker's, file 13), read when the model is loaded,
  whatever character is played: only its skin and model chunks are kept (Claire 1.4 KB, Wesker
  2.3 KB, into the display slot's own buffer) and only its texture block is uploaded (Claire 128 KB,
  Wesker 5 KB, one texture entry; refused like a model under the margins of "Loading"). The
  player's own model and `sys->obwp` are not touched; the part file goes through the loader's
  `pack` buffer.
* **Set up** as `bhSetPlayer` does (`player_hair`: offsets on head node 5, Claire's skin), in an
  `O_WRK` of the display slot, linked to the display actor's work (all four cast models have their
  head at node 5, which `bhObjClpn` reads). Per pose its position follows the model's head; the
  ponytail runs the game's own simulation (`bhObjClpn`, `bhCalcHair`: the same joints, from the
  display actor's retargeted angles) once per tick (our player and the partner: the pose after
  control; the double: `actor_double_tick`; of two doubles shown the first steps it, the other
  shows the same swing), in work of its own (`exp0`/`exp3`, 20 KB per slot,
  flag `0x100000`), so it never takes `bhGetFreeMemory` (the earlier crash: memory under a pending
  weapon read). Drawn right after the display actor.
* **Memory**: per display slot (11) 34 KB of static buffers (374 KB in all, not the game's arena); per shown head part one texture
  entry and the texture above; one part-file read (about 1.2 MB, the existing buffer) per model
  change. Released with the model (`unload`, `njReleaseTextureAll`: game over, the title).
* **The played character's own cutscene model** (Claire's while Claire is played): the doubles
  stay the game's (see "Cutscene doubles"), and while one acts `sys->obwp[2]` is the character's
  own head part linked by the script (`self_cine_hair`, as with "Custom Cutscene Model" off,
  `actor_double_kept`); in gameplay the display actor's copy is shown.
* Not covered: a script's InitPony (`flg2 & 0x2`) does not reset the copy; a teleport swings it once.

Models not in the catalogue:

| Model (id) | Why |
|---|---|
| Alexia second form (13) | the body is one of 14 linked sub-models |
| Alexia third form (14), Gulp Worm (2), Tentacle (29) | arena-sized or below the floor |
| Armored knights (54, 55) | armour pieces scattered on the floor in their idle motions |
| Alexia's baby (30) | a thin stalk in motions 0 and 1 |
| Ant A (10), Ant C (71), baby Black Widow (24), Nosferatu's core (53) | tiny, no model, or nothing visible |
| NPC 68 | a black figure without a head |
| ids 0, 16, 25, 56, 58..63, 65..67, 69, 70, 72..79, 81..85, 87..90 | props and set pieces |

### How

* **Catalogue** (`actors.c`): per model the room file and enemy record that carry it (the
  smallest room holding the model and its type's motion set), type and model version (checked at
  load), animation set (below) and idle motion. Only ids, no disc data.
* **Loading** (`pc/glue/actor_glue.c`): the room file is read from `RDX_LNK.AFS` and expanded into
  a static 12 MB buffer. The record's model chunk list (`hed01`) and its type's motion list
  (`hed02`) are copied into the display slot's buffer (1.5 MB; the cutscene cast's slots: their piece of
  one shared 3 MB buffer, without the motions, "Cutscene cast") and realized as `bhSetEneMdl` /
  `bhSetEneMtn` do. Textures: the table (`hed04`) has, after the room model's entry, one entry per
  first (type, model version, textured sub-model) in `bhSetRoom` order; the loader replays that
  order and uploads its model's entries (a model the room already shows costs nothing). Player
  part files use `bhReadPlayerData`'s layout (one loader: `afs_read`, `player_model_read`,
  `player_hair`). Released when the model changes; refused (the character's own model stays) with
  fewer than 32 texture entries or 2 MB of heap left.
* **Display actor**: a `BH_PWORK` per slot (our player, the co-op partner, the cutscene double,
  two for enemy variants' models: [ENEMIES.md](ENEMIES.md) "Enemy variants", posed from each such
  enemy's skeleton, or from another enemy's (a donor) playing a borrowed motion, when it is drawn,
  and six for the cutscene cast's models, "Cutscene cast"),
  drawn in place of the source (`game.c actor_put_player`, `coop_draw_remote`,
  `eneset.c actor_put_enemy`). The source stays what the game animates, collides with and links
  weapons and effects to; it is only not drawn. The head part (`sys->obwp[2]`) is disabled while a
  catalogue model is shown.
* **Humanoid retarget** (the cast, Alexia's first form, zombies, Tyrant, Hunter, Nosferatu,
  Bandersnatch, Monster Steve, the anatomist; all with bind angles 0 and a T-pose, like the
  player): a node map is built from the rest skeleton (`rig_build`): pelvis = node 1; legs =
  chains going down (hip, knee, ankle, toe); spine = up to the first node with a sideways child
  (chest); neck/head above it; arms = sideways chains from the chest or a spine node below it
  (clavicle if the first bone is shorter than 1 unit). A child at its parent's own position (a
  zero-length bone, e.g. Rodrigo's and the hazmat man's shoulders) is passed through
  (`rig_next`); a sideways chain that ends further up or down than out (tentacle, tail) stays
  unmapped. The source skeleton is mapped the same way. Every tick, per mapped node: model world
  rotation = rest correction x source world rotation; positions come from the model's own bone
  offsets, the root from the source's (feet on the floor). Unmapped nodes follow their parent
  rigidly. Details: "Retargeting" below.
* **Joint blending**: the meshes are skinned (`npCalcSkin` blends a vertex near a joint between
  the node's matrix and that matrix with the node's angles undone), so each node's angles are set
  to its local rotation (world x parent world^T, decomposed as `njRotateXYZ` builds it).
* **Other models**: their own idle motion (motion 0) looped, gliding with the player (bat, moth,
  spiders, Cerberus, albinoids, fish, Spotter, ant).
* **Without co-op**: `game.c` calls `coop_control_remote` while `appear_on` (an appearance other
  than the character played is chosen or still shown); with "Current Character" nothing runs.
* **Lobby preview**: `actor_set`, `actor_pose`, `actor_draw` work outside a room. The
  hands are the unarmed pieces (weapon file `chr * 30 + 20`, `player_weapon_read`) on nodes 9/13 of
  the preview's player model, as `sys->obwp[0..1]`; the character is `appear_voice` (the cast its
  own, other men Chris, women Claire; else the file shown). On a display actor they are drawn only
  where `actor_hand_free`: the hand is mapped and the model has no mesh there of its own (no geometry
  and no skinned vertices on that node: the story cast; zombies and creatures have theirs).
* **Game over**: `njReleaseTextureAll` unloads the display actors; `coop_textures_released`
  clears the "player in control" state, so the appearance returns only once the player is back.

### Gender-matched animations

Setting "Match Model Animations" (APPEARANCE page, ini `appear_gender_anim`, default 1): a man
shown while Claire is played moves with **Chris's** motions; a woman shown while a man is played
moves with **Claire's**. Gameplay stays the played character's.

The catalogue's `anim` field picks the set (`appear_motion_chr(anim, ply_id)`; player files by
their file, `appear_part_anim`):

| `anim` | Models |
|---|---|
| `ANIM_MALE` | Chris, Steve, Wesker (all files and models), Alfred (all), Rodrigo, the hazmat man, Nosferatu, Tyrant, Monster Steve, Hunter, Bandersnatch, the anatomist, every zombie |
| `ANIM_FEMALE` | Claire (player files, cutscene model), Alexia Ashford, Alexia's first form |
| `ANIM_IDLE` (own idle) | Cerberus, albinoids, Black Widows, moth, bat, fish, Spotter, ant |

* **Motion source** (`actor_glue.c`): per display slot (0 our player, 1 the partner) a hidden copy
  of the source's 22-node skeleton with its own motion set: part file `10 + chr` and weapon file
  `wpn + chr * 30 + 20` (only the weapon file is re-read on a weapon change), 500 KB per slot.
  Every tick after the source's motion the copy plays the same motion number at the source's
  phase, with its `mtn_md` and flip table, blending whenever the source blends; neck and head
  take the source's angles; the third-person aim bend applies too (`tps_bend`). A slot the other
  character lacks and room-set acting play the source's own motion, the other sex's (Claire's
  90..92, the moth larva, on a man; Chris's, Steve's and Wesker's 51, 57, 58 and Steve's 30 on a
  woman; weapon files 6, 13 and 18 differ too); it is always loaded. In a grab
  `appear_grab_motion` picks the other character's motion of the enemy's set (zombie
  400/425/450/425, Gulp Worm 21/29, Black Widow 14 * `ply_id`, Cerberus 100/105, bat 30/33/36,
  Bandersnatch 100/110/120, Alexia 2 10/16, Giant Black Widow 60/66).
* **The frame**: the phase is that of the frame the source **shows** this tick (`actor_frame`), not
  `frm_no`, which its `bhSetMotion` has already advanced: `player.c bhControlPlayer` records the
  frame and blend count just before (`pc_plp_frm`, `pc_plp_hokan`). Played one frame ahead, a
  motion the game leaves on its last frame had wrapped to its first, so the copy showed the start
  of the motion for a tick: after every shot the male fire motion's recoil (the twitch after a
  handgun shot, any weapon; also the start of zombie grabs and a quick turn). A blend the game starts
  within a motion (the end of raising a weapon: `hokan_count` 1 on the same motion) is copied too.
  The recorded frame counts only while the motion and set are still the ones recorded
  (`pc_plp_mtn`, `pc_plp_set`): enemy code runs after the player and may start another motion (a
  grab, Alexia's burn), whose own `frm_no` and blend are then taken, never the old motion's frame.
  Co-op sends the same frame (`Pose.frm_no`), so the partner sees each player's own frames (before,
  every player's one-shot motions flashed their first frame at the end on the partner's screen).
* **Display**: a catalogue model is retargeted from the copy; a player-file mesh is drawn with the
  copy's matrices and angles (`actor_put_body`), and the source's nodes 5, 9, 13 get the copy's
  matrices before the objects follow, so head part and weapons sit where they are drawn. The
  co-op partner (`ply2`) works the same from its model and motion file (`rx.mfile`).
* Not covered: the lobby preview and cutscene doubles keep their source's motions; positions stay
  the real player's; a switch between the matched set and a fallback does not blend.

### Retargeting

Each model keeps its own skeleton: bone lengths, rest pose and proportions are its own, only the
joint rotations come from the source (`actor_glue.c rig_build`, `retarget`).

* **Rest poses**: computed once at load from the model's node tree (`rig_build`: parents and
  model-space rest positions; all bind angles are 0, so every rest orientation is the identity, as
  the player's). The source's rest pose is its current node offsets (the player files' bones).
* **Rotations**: per mapped joint, the model's rotation is the source's rotation relative to its rest
  pose, turned by a rest correction: the rotation taking the model's rest bone (towards the next
  mapped joint of the chain) onto the source's. In local terms: model local = C(node) x source local x
  C(parent)^-1, so a model whose arm rests at another angle (an arm lower or more forward, a hunched
  spine) bends at the same joints by the same amounts without being pulled to Claire's bone lengths.
  A chain end (hand, toe) takes its parent bone's correction. The source's rotation of a joint is
  that of the node whose frame holds the next joint (`rig_rot`): past a zero-length bone (a node at
  its parent's own place, the chain's `rig_next` passes through it: Rodrigo's and the hazmat man's
  shoulders) the second node's, which turns the bone; the player files have none. Node angles are written back as
  `njRotateXYZ` builds them, so the skin blends joints (`npCalcSkin`).
* **Limb roles**: an arm runs from its first node sideways from the chest to the hand: the last node,
  or the first with two or more children whose subtree is short against the arm (fingers, claws; a
  spike on the Tyrant's forearm is passed). Clavicle, shoulder and elbow are the nodes nearest to where
  the player has them along the arm (3%, 35% and 68% of its length from the chest), in order, so a
  long arm (the Bandersnatch's six joints) has its hand at its claw and an arm without clavicle
  (Hunter) maps from the shoulder. A leg of more than four joints (Monster Steve) keeps hip, toe and
  ankle at its ends and the knee at mid-leg. Joints between mapped ones follow their parent rigidly.
* **Root and feet**: the root's height over the floor is scaled by the hips' heights (crouching,
  kneeling, the walk's bob), and the body is then moved up or down so that its lowest ankle or toe is
  exactly as high as the source's, each over its own sole (the rest pose's ankle heights): the model
  stands where the source stands and its feet leave the floor as far as the source's do (a jump, a
  climb, sitting with the feet up), not glued down. The floor for the scaling is under the source's
  lowest ankle or toe, not the base matrix: a cutscene record's base matrix often stays at the room's
  origin while its room motion carries it onto a floor 30 to 50 units up, and scaled from there a taller
  model floated (Alexia for Steve in 0030 3.2 units, a Hunter for Alfred in 1020 2.2, Nosferatu for
  Alexia in 9150 3.1) and a smaller one sank (the anatomist for Wesker in 8030 2.6); the old correction,
  in proportion and at most 3 units, did not reach. Models whose legs are longer than their hips suggest
  stood in the floor (the Tyrant for Rodrigo in the opening, ankles 1.6 units deep). Now within 0.4
  units in every scene measured; the shift stops at half the model's hip height (feet of another kind).
  For the player in gameplay (its base matrix on the floor) nothing visible changes (Tyrant: 0.1). Only
  the height: room motions move cutscene doubles across the room with the root. Test aid
  `RECVX_FEET_TRACE=1`: every 15 ticks each display slot's lowest ankle or toe and the source's over the
  base matrix, the hips and rest ankle heights and the shift.
* **Post-passes**: the two-handed grip IK and the lighter IK (below), the face mask's jaw, tongue and
  eyes, the cutscene doubles' hand pieces, as before.
* **A missing hand**: what the game links to a source hand the model does not have (Nosferatu's right
  hand, the Bandersnatch's left) is shrunk to a point instead of floating where the source's hand is.
* **Free aim** ([THIRDPERSON.md](THIRDPERSON.md) "Free-aim pose"): the bend is solved on the weapon as
  drawn (`actor_aim_hand`; only while the aim pose stands still, eased, not for the knife; a solve
  that stops converging, a target a few units from the muzzle, keeps its best step), and the third-person camera's height, shoulder offset and distance scale
  with the model's head height over the character's (`actor_cam_scale`, 0.8 .. 1.8): the camera is no
  longer inside a Tyrant or Monster Steve.
* **Previous retargeting**: `RECVX_RETARGET=0` (environment, read at start) keeps the earlier mapping
  (fixed chain positions, clavicle by length), the unscaled root and the aim taken from the unbent
  hand; frames are then identical to the previous version's. The character played without a
  model never goes through this code.
* Test aid `RECVX_RIG_DUMP=1`: logs each model's rest skeleton, the source's map and skeleton, and per
  tick the source's and the model's lowest foot over the floor.

Why aiming up looked broken before: the free-aim bend took its pitch from the hand of the unbent pose
and turned the upper body about the chest, which swings a hand far from the chest (a tall model's)
off the line (a drawn barrel up to 11 degrees off a near target); the camera stayed at Claire's
height, inside tall models; the old map took Monster Steve's clavicle for his upper arm (his weapon
at his elbow, his feet 4 units over the floor) and the Bandersnatch's mid-arm for its hand; the
weapon of a hand the model lacks floated at the invisible source's hand.

### Hands, joints, lighter and aim

* **Weapon hands** (`actor_pose`): the source's hand nodes 9/13 get the model's hand position and
  the copy's hand rotation for the tick's effects, objects and drawing, so weapons sit in the
  model's hands and the muzzle flash starts there.
* **Hand pieces on imported models** (`actor_weapon_hands`): a weapon model is a hand piece, object 0
  (the character's gloved hand around the grip; unarmed: the fist), with the weapon as its child. On
  an imported model (which has its own hands) object 0 is hidden (`NJD_EVAL_HIDE`, the weapon below it
  still drawn): ours from `coop_glue.c self_keep` every tick, the partner's after its pose.
* **Two-handed grips** (`retarget`, `arm_ik`): when the source's left hand is near its right
  (closer than 6 units, full below 3), the model's left hand gets a target at its right hand +
  (source left - source right), solved by a two-bone IK on the model's own bone lengths (elbow
  kept in its bend plane). One-handed poses are untouched.
* **The shot** (`actor_hands_back`, game.c before `bhControlPlayer`): the game's own matrices of
  nodes 5, 9, 13 are saved before they are moved and put back before the next player control, so
  the game fires (`bhCheckGunAtari` from the muzzle at `owP[9]`), swings the knife and places the
  lighter light from the real skeleton; only the drawing uses the model's hands. They are put back
  only while the skeleton is the one they were saved from (the chest, node 3, unchanged): a script that
  hides the player and moves it computes it again, and a hidden player's skeleton is not computed again
  by `bhControlPlayer`. Put back over that, the head stayed where the player had stood; in 9100 (Chris
  and the Spotters in the pipe corridor, entered from the east doors) that spot is in a Spotter's
  searchlight, which tests the player's nodes (`bhEff_E11_CheckCollisionPlayer`, from its draw), so the
  room's "spotted" event started on the scene's own task level: the Spotters flew off and the scene
  stayed on its camera with the player hidden. Third-person aim:
  [THIRDPERSON.md](THIRDPERSON.md) "Free-aim pose".
* **Lighter**: the game holds the lighter by an arm IK after the motion (`bhArmIkMdk` on node 6
  towards a point in front of chest node 4, nodes 7..9 flagged 2). The same IK runs on the
  gender-matched copy whenever the source's arm is flagged (our player, and the partner via
  `lighter_arm`).
* Known: the Tyrant's weapon is lost inside its big claw hand (it sits at the wrist node); in
  Chris's two-handed stance the left palm lies under the gun on every male model (the motion's
  own grip).

### Cutscene doubles

* Pre-rendered movies cannot change (the opening, the ending and 17 in-room movies started by op
  94 `MovieStart`). 142 rooms have in-engine cutscenes. In 77 of them the character played is the
  player model itself, and the appearance applies as everywhere. In the others the script hides
  the player and an **event double** acts: an enemy record of the story cast with a face mask
  (Claire 91, Chris 92 / 42, Steve 93 / 43, Wesker 97 / 49 / 47).
* `eneset.c bhDrawEnemy` calls `actor_put_enemy` instead of `bhPutModel(ep)`: a double of
  `sys->ply_id` is drawn as the chosen model (display slot `ACTOR_DOUBLE`), posed from the double's
  skeleton. The model is loaded at the hand-off while the room has such a double (player files
  too: `ACTOR_PART + n` loads part file `10 + n` as a display model). Creatures without a humanoid
  node map do not replace doubles.
* The played character's own cutscene model chosen (Claire's while Claire is played) replaces no
  double (`actor_doubles`): the double is that model already, with its face, hand pieces and the
  linked head part.
* **Hands are objects**: the cast's cutscene models have no hands; the room script links hand
  pieces (fist, open hand, holding poses) and props to the double's hand nodes and swaps them per
  gesture. `actor_double_tick` (from `coop_control_remote`, after `bhControlEnemy`, before
  `bhControlObjItm`) poses the double's model and moves the double's hand nodes to the model's
  hand positions, so pieces and props sit on the model's wrists. A model of another character hides
  the double's one-node pieces and shows its own hands, as the cutscene cast's ("Cutscene cast", "Drawing":
  `cast_double_parts` -> `hands_hide`, `actor_doubles` -> `actor_cast_hands`, `actor_put_enemy` ->
  `cast_hands_draw`); before, Claire's gloved hands were on Steve. Two doubles of the played character are each
  drawn in their own pose (`dbl_posed`).
* **Talking faces** (setting "Animated Faces (Experimental)", APPEARANCE page, ini
  `appear_cutscene_face`, default 0): the cast talks through a face mask (`face.c`, `face_bh.c`,
  the model's `MASK` chunk; `fmCnkInitContext` inserts jaw/tongue/eye pivots, 27 -> 31 nodes;
  op `bhLipSet` starts a line of phonemes from the model's face table). With the option a cast
  model shown for the double gets its own mask (`face_init`, ~55 KB of its slot's 128 KB in `face_mem`)
  and the double's lip state is copied onto it every tick (`face_step`), and so are its eye angles
  (pivots 9 and 13 of every cast mask: the eyes look where the double's expression has them). A line
  names a bank of 6 mouth shapes in the speaker's face table (`bhLipSet`); where the model's table has
  no such bank or a still one (`bank_still`: no jaw angle, the same lip muscles in all six; bank 1 of
  Wesker's, Alexia's and Rodrigo's tables, which many of the others' lines use) the model's first bank
  is played instead, so its mouth moves with every line (before, Wesker's, Alexia's and Rodrigo's
  stayed shut through all of Alfred's lines in 1020). The expressions' muscles are not copied (each face has
  its own). A player file chosen as the appearance is shown
  on the double as the same character's cutscene model. `face_src` copies rest meshes to 64-byte
  boundaries (the morph reads them with `lq`). A mask that does not fit leaves the model without.
* **Custom Cutscene Model** (APPEARANCE page and F1, ini `appear_in_cutscenes`, default 1). Off: the
  doubles stay the game's own (the real Claire or Chris, their hand pieces, face mask, lip sync);
  the chosen model is shown only while the player itself is. What counts as a cutscene is the
  double: a scene (story cutscene or a short in-game event) counts while an event double of the
  character played is shown (`actor_double_acting`: an enemy record of `doubles[ply_id]`, active,
  not hidden). Scenes the player model acts in itself (the 77 rooms above, room motions on the
  player) keep the chosen model, as gameplay does. How:
  * `actor_doubles` loads no model for `ACTOR_DOUBLE`, so `actor_put_enemy` and
    `actor_double_tick` leave the doubles alone (no hand or face work at all).
  * The head part: the scripts link the player's `sys->obwp[2]` (Claire's ponytail) to the double
    (`ObjLinkSet`), and the appearance had disabled it (catalogue model) or replaced it (player
    file). `coop_glue.c self_cine_hair`, every tick before the objects follow their links, sets
    up the played character's own head part (from `self_orig` under a player file) when a double
    starts acting and the chosen model's again when none does; the script's link (work, node, x
    offset) is kept. The ponytail's work is the player's (`pletcp`, as `bhObjClpn` takes it when
    linked to the player): linked to the double it would come from `sys->memp`, which broke a
    pending disc read (weapon change). Its swing restarts at the cutscene's start.
  * Voices: `pc_player_voice` keeps the played character's voice while a double acts. Cutscene
    dialogue is streamed and stays the script's, unless "Randomize Voice Lines" is on ("Voice lines").
  * Co-op: each game decides for its own screen (the doubles are local; the partner is not drawn
    during a cutscene of ours, `local_visible`), so each player chooses what they see; a replayed
    cutscene follows the replaying player's setting.

### Voices

The player's vocal sounds follow the model shown: hit reactions, death, grab and struggle
screams, and what room scripts play as the player's voice (not cutscene dialogue, which is
streamed ADX).

**Where they come from.** Every player vocal sound is SE bank 4 (`0x400 + program`) through
`sdfunc.c CallPlayerVoice` (slot 7): damage and death (`player.c bhCPM0_damage`, `bhCPM0_die`),
enemy grabs and room scripts (`PlayGameSe4Event` type 0). Bank 4 is `CORE_<ply_id>.SPQ` (AFS key
`0xFFF0 | ply_id` in MULTSPQ1/2); the disc has exactly four (Claire, Chris, Steve, Wesker) with the
same 4 programs each. No other character has player vocal sounds.

**Mapping** (`actors.c appear_voice`, ctest `appear`):

| Shown as | Voice |
|---|---|
| Current Character, Current Character (Cutscene) | the character played |
| Claire (all files and doubles) | Claire (`CORE_000`) |
| Chris and his doubles | Chris (`CORE_001`) |
| Steve and his doubles, Monster Steve | Steve (`CORE_002`) |
| Wesker and his doubles | Wesker (`CORE_003`) |
| other men (male `anim`): Alfred, Rodrigo, the hazmat man, Nosferatu, Tyrant, Hunter, Bandersnatch, the anatomist, every zombie | Chris |
| women (female `anim`): Alexia, Alexia's first form | Claire |
| creatures with their own idle | the character played |

Creatures keep a human voice: enemy sounds are motion- and table-driven per type, list numbers
collide between types ([ENEMIES.md](ENEMIES.md) "Sounds of added types"), none is labelled hurt or
death, and they play through the enemy sound slots.

**How.** `CallPlayerVoice` (TARGET_PC) asks `coop_glue.c pc_player_voice(se)`. For the played
character's voice it returns 0 and the request is the game's own. Otherwise it loads that
`CORE_00n.SPQ` bank 4 as an extra bank (`snd_iop.c snd_xbank_load(0xFFF0 + n)`, the loader of the
spawner's enemy banks) and names it in the port byte (`pc_se_xbank`, bits 3..7), so the synth
plays it on the same port, slot, volume and pan. The game's bank 4 is never touched. The extra
bank is released with the room (`spawn_room_setup`) and loaded again at the next voice.

**Co-op.** Each side counts its own player's voices into the pose (`Pose.voice_n`, `voice` = the
program); the partner plays each new one at the partner's position in the voice of the model the
partner appears as (`Pose.chr`, else its character `Pose.mfile & 3`). Voices after a gap of 10 or
more frames are skipped. While a host enemy's step targets the partner (`plp` is `ply2`) its voice
calls play nothing; the partner's game sends the voice it makes.

**Memory.** A voice bank is 0.7 KB header + 38..54 KB samples, at most two at a time (ours and the
partner's), taken from the same 31 extra-bank numbers as the spawner's enemy banks.

### Limits

* Skinned areas near joints whose rest orientation differs (zombie shoulders in T vs arms down)
  stretch a little; the motions are the player's, so a body of other proportions strikes the same
  angles (a Tyrant's long arms reach further); grips sit at the model's wrist. Creatures do not scale.
* Per model: Nosferatu has no right hand (its weapon is not drawn) and its spine has no mapped lower
  node; the Bandersnatch has one arm (no two-handed grip) and no head node; the Tyrant's weapon sits
  in its claw; the hazmat man's and the zombies' torso is one bone from the hips; the camera scales by
  the neck where no head is found (the Bandersnatch).
* No creature sounds (footsteps stay the player's; the voice per "Voices").
* Lip sync only for cast models and with the option (the cutscene cast: always); the double's hand pieces are its own.
* A room with little texture memory left refuses the model (logged as `actor: ... not enough
  texture memory`). Wrong colours are possible where a model's CLUT bank collides with the room's.
* Test aids: `RECVX_APPEAR="frame:chr,..."` sets the appearance at a frame;
  `RECVX_COOP_ECHO` runs the partner path locally.

## Cutscene cast

Setting "Randomize Cutscene Cast" (GAMEPLAY, group CUTSCENE CAST; the lobby's GAME RULES, RANDOMIZER,
CUTSCENE CAST page; ini `cast_random`, default 0): in cutscenes the story characters are drawn as other
"Character Model" models. Purely visual: every record keeps its type, motions, script and collision. The
voices are the scripts' and stay the original character's (cutscene dialogue is streamed; "Voice lines" below changes them); the faces
follow the lip data where the model has a face mask. Code: `pc/src/host/cast.c` (who is cast and the
picks; no game state, ctest `cast`), `pc/glue/cast_glue.c` (in the game), the display actors of
`actor_glue.c`.

| Setting | ini | Values |
|---|---|---|
| Randomize Cutscene Cast | `cast_random` | **0 Off**, 1 On (`EnemyOpts.cast`; a file from the first version: 1 Characters and 3 Both read as On, 2 Enemies as Off) |
| Model Selection (shown while on) | `cast_models` | **0 Cutscene Models**, 1 All Models, 2 Custom (below, "Custom") |
| Model Assignment (shown while on) | `cast_assign` | **0 Per Character**, 1 Per Cutscene |
| Include Imported Models (shown while on and imported models exist) | `cast_imports` | **0**/1 |

The seed is the enemy options' (`enemy_seed`). In co-op the host's options count (they travel with the
other enemy options, `EV_EOPTS`, [COOP.md](COOP.md) "Added enemies and options").

### Who is cast

At the end of every room load (`enemyopt_glue.c enemy_room_finished` -> `cast_room`, after the enemy randomizer and
the placements: the cast sees the records as they are drawn) each room record of a story character (`cast_character`) gets a model: Claire 91; Chris 92, 42; Steve 93, 43; Wesker 97, 49,
47; Alfred 94, 96 (both model versions), 44; Alexia 95; Rodrigo 98. Not: the man in the hazmat suit 99 (his helmet's mouth does not move, so neither cast nor a pick, in either pool);
the bosses (Alfred's boss type 16, Alexia's forms 12..14), props and set pieces, enemies (zombies and
creatures stay as they are; a first version swapped the cutscenes' zombies too, removed: the fights changed), anything
spawned after the load. The creatures and the zombies that only cutscenes show are cast too ("Creatures" and "Scene
zombies" below). Kept by hand (`cast_glue.c kept[]`):

| Room, record | Why |
|---|---|
| 7030, 1 | Alexia asleep in her ice capsule (the monitor scene): the scene needs her silhouette |

**Claire and Chris are never cast** (`cast.c cast_role`): they are the characters the story is played as, and
in every cutscene they show the player's look for them, played or not. The one played now is the game's
double ("Cutscene doubles": "Character Model" and "Custom Cutscene Model"); the other one's records get, at the
load, that character's look as their model instead of a pick (`cast_glue.c cast_room` -> `coop_glue.c
coop_look_model`: "Character Model" resolved for that character, BioRand's "Randomize Played as" pick for that
character first, then as the doubles take it, `actor_double_model`: none with "Custom Cutscene Model" off, the
talking twin of a player file with "Animated Faces", none for the character's own look) and are drawn as it
while that character is not the one played. So Chris arriving at Rockfort Island (7240) and with Rodrigo (7220)
is your Chris whether or not the game has switched to him yet (played through, 6100's `PlayerChangeSet` 1 and
its door to 7240 switch at 7240's load, and he is the double; arriving there without the switch, by a teleport,
the game still plays Claire and he is drawn as your Chris look in a cast slot), Claire in Chris's part (9150,
9300, 9350, 9360) is your Claire, Chris in Claire's part (5820) your Chris, Claire in Steve's section (3210) your
Claire. "Current Character" (the user's own look), the character's own cutscene model or "Custom Cutscene Model"
off: they are themselves. In co-op each screen shows them with its own player's looks, as its doubles.
`--dev` cutscene log: `r0 Chris(92)->Nosferatu (your look)`, a played one `(played: the double)`.
With "Character Models" *Per Character* ("Per Character" above) this runs also with the cast off: `cast_room`
then gives only Claire's and Chris's records their look (`cast: room 9350: 2 records of Claire or Chris drawn as
your look for them (Per Character)`), so Claire in Chris's part is "Claire's Model" and Chris in hers "Chris's
Model" without any randomizer; with *Same for Both* and the cast off they stay themselves, as before (one model
for both would show twice).

**Precedence** for the others: the doubles of Steve and Wesker while played (Steve 93/43, Wesker 97/49/47) belong
to "Character Model" too; the cast leaves them to the game or the appearance. Which characters count as played
is read when a record is drawn, not at the load (`cast_glue.c role`): the one played now (`sys->ply_id`, which a
story switch changes at the end of the next room load's `bhFinishRoom`, after `cast_room`), the one a switch the
script has set is about to make played (`PlayerChangeSet`: `cb_flg & 0x80`, `cng_pid`: Steve in 3100 before his
section), and in co-op the partner's (its part file, `Pose.mfile & 3`). Such a record gets a model at the load
but is not drawn as it then. Steve and Wesker not played are ordinary cast. Every double of the played
character is one of the types above (checked over all room files: Claire 91; Chris 92, 42; Steve 93, 43; Wesker
97, 49, 47), so no scene stands the player in with another type.

### Creatures

A few creatures appear in cutscenes as records that no fight uses: a scripted actor without AI, or an
enemy type in a room that is only a cutscene. They are cast like the characters (`cast.c cast_creature`, a
table by room file, record and type), each kind as a character of its own after the seven, so with *Per
Character* a kind gets one model in all its scenes and *Per Cutscene* picks again per room. What they draw from
is the setting *Enemy Models* ("Enemy Models" below; default *All Enemies*: other monsters). Never its own look
(the Tyrant not as the Tyrant, the Hunter not as a Hunter or Sweeper, Steve's father not as a zombie). These records
change nothing in play.

| Room, record (type) | Creature | Why it is safe |
|---|---|---|
| 6040, 4 (52) | Nosferatu breaking out of his restraints | a scripted actor (`subpl.c`, no AI); the fight is in 6100, another record (type 15) |
| 3230, 1 (19) | the Tyrant Alfred releases | a cutscene room ("Cutscene: Alfred and the Tyrant"); the Tyrant fights are in 1122 and 5010, other records |
| 8030, 2 (5) | the Hunter Wesker sends out | a cutscene room ("Cutscene: Wesker's monitor room") |
| 3000, 2 (51) | Steve's father, a zombie | a scripted actor, no AI |

Not cast (`enemies.py census` over all room files; every creature record without a kill flag looked at):

* Fought, or the scene leads straight into the fight with the same record: Nosferatu in 6100, the Tyrant
  in 1122 and on the plane (5010), Alexia's forms (9301, 9302, 9321), Monster Steve (9280), the
  albinoid, the Giant Black Widow, the Gulp Worm in 3091 and 7230, the Hunters and Bandersnatches of the rooms. Swapping
  only during the scene would not change the fight (visual only), but it shows the fight's own enemy.
* Not shaped like a person (no humanoid skeleton to pose a model from: it would be drawn as itself anyway): the
  Gulp Worm eating Rodrigo (7220, 7221), the tentacle (9300), the Spotter (8030), the albinoid infant (3120), the
  Bandersnatch's long arm (3130: only the arm).
* 9050, 1 (52, model 1): a body lying under the crane; the catalogue's "Zombie: Bandaged 2", a zombie look.
* Nosferatu's core (53) and the close-up props in 6040: no model to replace (the core is hidden while he is
  his model, below).

Two things the creatures needed in drawing:

* **A morphing record**: Nosferatu's mesh in 6040 morphs (`mdflg & 0x2`, his face and chest), and
  `eneset.c bhDrawEnemy` drew such a record with `bhPutModel` alone; under `TARGET_PC` it now asks
  `cast_draw` first (only the cutscene cast; the doubles and the variants as before), so he is his model
  in every shot, not only the first.
* **A source without arms**: in his straitjacket Nosferatu's skeleton has no arms (16 nodes). The model's
  arms took no rotation and stayed out in its T-pose; now an arm the source lacks hangs down beside the body
  (`actor_glue.c retarget`, the upper arm turned from its rest direction to down and slightly out). The
  restraints (records of their own, not linked to him) stay where the script puts them.
* **Parts of the body**: his heart is a record of its own (type 53, record 5, morphing, beating) linked to his
  chest (`flg & 0x80`, `lkwkp` his record, node 3); it showed on the chest or the face of the model in his
  place. A record linked to a creature drawn as its model is not drawn (`cast_glue.c creature_part`: `cast_draw`
  answers "drawn" for it; it still runs); with the creature drawn as itself it is back. Only the creatures:
  what a character's record has linked to it could be something it holds. The other three have nothing linked
  but the script's objects of the room.
* **Hands**: a creature has no hand pieces of the script's (Nosferatu has no arms; the Tyrant, the Hunter and
  Steve's father have theirs in their mesh), so a model without hands of its own (the cutscene models, the player
  files) stood there with its sleeves ending at the wrists. Every cast slot (and the double's) whose model has no
  hands gets the unarmed pieces of the character the model is (`actor_glue.c actor_cast_hands`; `appear_voice`: the cast their own,
  other men Chris's, women Claire's; weapon file `c * 30 + 20`, as the lobby preview), linked to the model's
  hand nodes and drawn after it on each hand without a mesh of its own while the record shows no piece of the
  script's on that hand (`cast_hands_draw`; a character's own pieces are hidden there, "Drawing"). Loaded with the model at the room load (two textures: Steve's and
  Wesker's 17 KB each, Chris's 65 KB, Claire's 128 KB; refused like a model under the margins). Monster Steve's
  arms end without a mesh at the hand node too (his claws are linked records in his fight): he gets Steve's
  hands.

### Scene zombies

Some zombies are only actors of a scene: zombie records (type 1) without a kill flag that the scene's events move
(`LoadWork`, their motions by `CommonCtr` 34, the record in event mode by `CommonCtr` 23) and hide again, in a room
no fight uses. They are cast as creatures (`cast.c cast_scene_zombie`, a table by room file and records; `rec_chr`
`CAST_CROWD`, drawn and given hands as the creatures). Under the same setting as the creatures, the cutscene cast.
The creature table did not cover them: it lists single creature records by type, and zombies were left out since the
first version swapped fought ones too.

* **What they become** (`cast_crowd_model`): what *Enemy Models* says ("Enemy Models" below): a creature of *All
  Models* (Alexia's first form, Nosferatu, the Tyrant, Monster Steve, the Hunter, the Sweeper, the Bandersnatch, the
  anatomist) or another zombie look; its *Custom* the checked ones of those and of the characters (none checked: they
  stay zombies); *Same as
  Cast* as before it, the Model Selection's monsters whatever it is (*Cutscene Models* has none: all of them); *Off*
  themselves. Never an imported model, never their own look.
* **One per kind**: each zombie look is a kind of its own. With *Per Character* a look gets one model in every scene
  (scene 0), *Per Cutscene* picks again per room. The looks are taken in turn over a shuffle of the pool by (seed,
  scene), so the zombies of one scene mostly differ (9340: three looks, three monsters).
* **Display only**: the record keeps its type, motions, script and timing; the model is posed from its skeleton as
  every cast model (the zombie skeleton is a man's). Co-op: from the seed and the options the host sends
  (`EV_EOPTS`), so both screens show the same.

| Room, records | Scene | Why it is safe |
|---|---|---|
| 9340, 1..3 | the zombies around Chris ("Cutscene: Wesker and Claire (1)") | a cutscene room: it plays on entry and leaves for 9350 |
| 0050, 2 | the zombie crawling at the courtyard fence | no kill flag; shown until its scene, hidden after it; the dogs are other records |

Not cast (`enemies.py room` over every room with a zombie record without a kill flag, and the scenes played):

* A scene that wakes zombies who then fight, or leads into a fight in the room: 0070 (the shower room), 9290 (the
  cell block; record 5 waits in event mode among fought ones), and every zombie with a kill flag.
* 0030 ("Claire meets Steve"): its zombie crowd is room objects with zombie meshes (`LoadWork 2`), not enemy records;
  record 3 stays at the origin. 6020 (record 1: a shot of 0.6 s where it is hardly visible); set pieces seen in
  gameplay (3140, 4001); records no scene of the scene list names (3050, 6030, 9310).
* The bodies of type 52 (9050) and Steve's father (3000, a creature already).

### Enemy Models

The creatures and the scene zombies are monsters; the characters' pool put Nosferatu in Alfred's or Rodrigo's model.
*Enemy Models* (under Model Selection; ini `cast_enemy_models`, `EnemyOpts.cast_enemy`, `cast.h CAST_ENEMY_*`) gives them
a pool of their own:

| Value | The creatures | The scene zombies |
|---|---|---|
| *Same as Cast* (0) | from the characters' pool, after them in one shuffle (the behaviour before the setting) | the Model Selection's monsters |
| *All Enemies* (1, default) | from the enemies' list's monsters | the same |
| *Custom* (2) | the checked ones of the enemies' list (its characters too) | the same |
| *Off* (3) | themselves | themselves |

* **The enemies' list** (`cast_custom_list(2, ...)`, `cast.c enemy_list`, `enemy_pool`): the monsters of *All Models* and
  the zombies' one entry, nine: Alexia (First Form), Nosferatu, Tyrant, Monster Steve, Hunter, Hunter (Sweeper),
  Bandersnatch, Anatomist Zombie, Zombies (Random Look); then the characters of the cast's *Custom* list, eight (since
  2026-11-08): the cutscene models of Claire, Chris, Steve and Wesker, Claire's second costume, Alfred, Alexia, Rodrigo.
  The monsters are *All Enemies*' pool; the characters only *Custom*'s, and only checked ones. No imports: the bits
  ride in `EV_EOPTS` (4 bytes, 17 used; ctest `cast` checks they fit), where up to 2048 imports would not, and a new
  import would start checked. Its *Custom* list starts with the monsters checked and the characters not; stored as the
  unchecked ones, `EnemyOpts.cast_enemy_off` (a bit per position, the monsters first), `recvx.ini` lines
  `enemy_custom_off = Tyrant` (the same keys as the cast's list) and `enemy_custom_set = 1` (`config.c cast_default`,
  as the cast's `cast_custom_set`: a file without it, a first start or one from before the characters, gets them
  unchecked, `cast_enemy_default`, its lines kept for the monsters). The
  same list code as the cast's (`cast_list.c` list 2): ENHANCED SETTINGS GAMEPLAY (Enemy Models, Choose Enemies), the
  BIORAND RANDOMIZER page, the lobby's CUTSCENE CAST page (ENEMY MODELS, CHOOSE ENEMIES), F1 (check boxes under Enemy
  Models).
* **The picks** (`cast_model`, enemy not *Same as Cast*): the four creatures take their turn over a shuffle of the
  enemies' pool by (seed, scene), each the next not taken and not its own (`model_char`), as the characters over theirs;
  the characters' turn then has only the characters (their picks are *Same as Cast*'s unless a creature traded with
  one there). Fixed per kind with *Per Character*. The rules stay: the scene right before a fight and every fought
  enemy never cast, Claire and Chris never picked. A checked character is a model like the monsters: a creature or a
  scene zombie shown as it gets the model's character's hands (`actor_cast_hands`, as with *Same as Cast*), never its
  own look (`model_char`). With no character checked the pool is the checked monsters in the same order, so the picks
  are those of before them (ctest `cast`; old and new `cast.c` compared over 200 monster sets, 100 seeds, both
  assignments: the same).
* **Old files** (`config_load`): without `cast_enemy_models`, a file with the cast's *Custom* gets *Custom* here, its
  monsters' `cast_custom_off` lines also unchecked in the enemies' list (the same names); any other gets *All Enemies*
  (the scene zombies drew monsters with every Model Selection already). `RECVX_CAST_ENEMY=0..3` (test aid) sets it where
  the file does not.
* Co-op: the host's value and list in `EV_EOPTS` (protocol 43, `Event` 96 bytes; the characters' bits fit the same 4
  bytes, so nothing on the wire changed for them).
* Checked (seeds 1 and 7, 9340, 8030, 6040): *Same as Cast* with *All Models* gave the 8030 Hunter Alfred and Nosferatu
  Rodrigo; *All Enemies* Monster Steve / Nosferatu and a Bandersnatch / Monster Steve; *Custom* (Tyrant and Hunter
  checked) only those (the Hunter as the Tyrant); *Off* none, Wesker still cast; the characters the same in all four.
  *Custom* with only Steve's cutscene model checked (seed 1): the three 9340 zombies and the 8030 Hunter are Steve, with
  Steve's hands.

### For the whole room visit

A record is drawn as its model for the whole room visit: in the cutscene and afterwards, where the
character stays in the room as a gameplay figure (Rodrigo at his desk after the opening, 0000).
`cast_scene` (an event camera: `cam.flg & 0x2`, mirrored by `light.c` in `sys->st_flg & 0x1000000`;
the cinema mode `cb_flg & 0x4`; or the player hidden by an event) only marks the cutscene for the test
aid: with `RECVX_CAST_TRACE=1` the log names each change (`cast: a cutscene ...`, `cast: gameplay ...`)
with where the cast records are.

A record that is no longer what was given its model (`still`: its type or model version changed since `cast_room`)
is drawn as itself, and the log says so once per record and room (`cast: room 9340 record 1: given a model as type 1
model 35, now type 1 model 7: drawn as itself`; the devcine log leaves it out of its swaps). That was the scene zombies
with *Randomize Event Enemies*: `cast_room` ran before the randomizer, which gives an event's zombie another zombie
look by spawning a new record, so the pick was logged and never drawn. Check with a run of the scene and the
settings, the log free of that line: `RECVX_TEST_SCENE=3200:RM_9340:8:0:1` with `enemy_random = 1`,
`enemy_random_events = 1`, `cast_random = 1`.

### The pools

| Model Selection | Pool |
|---|---|
| *Cutscene Models* | the cutscene models with a face mask: Claire (catalogue 0), Chris (1), Steve (3), Wesker (5), Alfred (8), Alexia (12), Rodrigo (13). Claire and Chris are never cast themselves, but others may be shown as them (since 2026-11-08; before, only *Custom* had them) |
| *All Models* | every listed humanoid catalogue model (`anim` not `ANIM_IDLE`, not a hidden twin): the cast, Alexia's first form, Nosferatu, the Tyrant, Monster Steve, the Hunters, the Bandersnatch, the anatomist, and **the zombies as one entry** (`CAST_ZOMBIES`): a character that draws it gets one of the 27 zombie looks, from (seed, scene, character) (`cast_zombie_look`: Per Character, the same look everywhere; Per Cutscene, per room). As 27 entries they were more than half of the pool, and most characters became zombies. Not "Alfred Ashford (Wounded)" (a special state). No other family is large: Hunter and Sweeper two; the event models and Tyrant 2 are hidden twins already. Claire's and Chris's cutscene models too. No player file: each has a talking twin ("Talking twins" below); Claire's second costume (no moving face) only in *Custom* |
| *Custom* | the *All Models* entries and Claire's second costume, those the player checked ("Custom" below) |
| with imported models | the imported models after them (playable characters and NPCs, `IMPORT_OFFERED`: people with jaw and lid bones, [IMPORT_OUTBREAK.md](IMPORT_OUTBREAK.md) "Faces", so they count as cutscene models: their jaw follows the record's mask, `imp_face`) |

### Talking twins

The player files (`ACTOR_PART + n`) have no face mask: a character drawn as one kept its mouth still through
every line. Claire's, Chris's, Steve's and Wesker's files have a twin with a face mask, the same character's
cutscene model (catalogue 0, 1, 3, 5), as the cutscene doubles use it ("Cutscene doubles", `actor_doubles`). So
the cast lists only the twin (`cast.c twin_of`): picking that character's look gives the cutscene model, and its
mouth moves with the record's lines. The file is no entry of its own, in no pool and not in *Custom*'s list (as
two entries the character had twice the chance). Claire's second costume has no twin (one cutscene model per
character; the costume exists only as her player file and has no face mask) and stays as it is, in *Custom* only.
Every other look with a face is its only entry already: the event models and Alfred's other uniforms are hidden
twins of the cutscene models; Monster Steve and Alexia's first form are other looks. Wesker's, Alexia's and
Rodrigo's cutscene models also kept their mouths shut through lines whose bank of mouth shapes is still in their
face table (all of Alfred's in 1020); they now play their first bank there ("Cutscene doubles", talking faces). "Randomize Played as" (BioRand)
still picks from the files too (and its own *Custom* list lists them): it sets the player's own look, and that one talks through the doubles.

* `recvx.ini`: an older *Custom* list's lines for the files (`cast_custom_off = Steve`, `Chris`, ...) name no
  entry now and are ignored (dropped at the next save); the cutscene models' own lines (`Steve (Cutscene Model)`)
  keep counting, so a look unchecked under that name stays unchecked.
* Co-op: protocol 32 (see the version history in [COOP.md](COOP.md) "Protocol 36"): the positions of the `EV_CASTOFF` bits and the picks from a seed changed.

### Custom

*Custom* is the *All Models* pool (with Include Imported Models on, the imported models too) cut down to the
entries the player checks in a list: SELECT ALL / DESELECT ALL, then a check box per entry, in the order and
groups of the lobby's model page (`cast_custom_list`: the "You appear as" list's order, but Claire's, Chris's, Steve's
and Wesker's cutscene models, that list's last, first; the zombies' one entry where their looks start, the imports
last). The order is only the display: each entry's bit is its position in the pool, so the picks, the ini lines and
`EV_CASTOFF` do not depend on it. Shared code `pc/glue/cast_list.c`, shown at three places: the lobby's
CUTSCENE CAST page (CHOOSE MODELS, "12 of 17", opens it as step 8), the BIORAND screen's EXTRA OPTIONS page
(Choose Models) and OPTION > ENHANCED SETTINGS, GAMEPLAY (Choose Models right under Model Selection: the list in
place of the page's rows, `options_glue.c` `st.list`); F1 shows the same list as check boxes under Model Selection
(`overlay.cpp cast_custom_imgui`). "Randomize Character Model"'s own Custom list (`cast_appear_off`) is only on the
BIORAND screen, as are its other rows.

* Stored as the entries **not** checked (so a model imported later starts checked): `cast_off`, a bit per
  position in `cast_pool(1, every import)` (the built-in entries, then import k). In `recvx.ini` one line per
  unchecked entry, by key, not position: `cast_custom_off = Tyrant` (the list's name; the zombies' entry
  `Zombies`), an import by its folder, `cast_custom_off = import:HN01` (names repeat). Read after the imports are
  listed (`config_load`); a key no entry has (an import deleted since) is ignored and dropped at the next save.
* **As it starts**: only the models with voice lines checked (`cast_off_default`: each entry whose
  `voice_model_chr` is a story character and that is not a player file, i.e. the looks of Claire, Chris, Steve,
  Wesker, Alfred, Alexia and Rodrigo with a moving mouth: 7 of the 17 built-in entries; Claire's second costume, a
  player file without face motion, starts unchecked), so the cast talks with "Randomize Voice Lines"; the creatures, the
  zombies, Alexia's first form, Monster Steve and the imports listed then start unchecked (a model imported later
  starts checked, as always). The rule is the static mapping, not the voice catalogue (that is read from the room
  files later; every one of the seven has lines on every disc). Applied once, by `config_load`, to a `recvx.ini`
  without `cast_custom_set` (a first start, or a file from before it) that has no `cast_custom_off` line and not
  *Custom* chosen; every save then writes `cast_custom_set = 1`, so a list later made all checked stays so. A file
  with `cast_custom_off` lines keeps its list; one with *Custom* chosen and no lines keeps everything checked (what
  it showed). With this list the scene zombies find no checked creature and stay zombies ("Scene zombies").
* **Nothing checked**: nothing to swap; every character keeps its own look (`cast_model` returns -1; the list
  says "0/17" in amber). One checked: every character but that model's own is drawn as it (a character whose
  only choices are its own looks stays itself, e.g. Steve with only Steve's cutscene model checked).
* Co-op: the host's list goes to the guest (`EV_CASTOFF`, protocol 31: the bit list in 64-byte chunks, each
  chunk that changed, all to a new partner, before `EV_EOPTS`), so both pick from the host's; the guest's own
  list stays in its `recvx.ini`. Imported entries count only while both sides have the same imports, as before.
* BioRand's "Randomize Character Model" has a *Custom* list of its own ([BIORAND_INGAME.md](BIORAND_INGAME.md)
  "S10"): `cast_appear_off`, `recvx.ini` lines `appear_custom_off = <key>` (the same keys), the same list code
  (`cast_list_open(1, ...)`, `cast_custom_list(1, ...)`) over its own *All Models* entries (these plus Steve's and
  Wesker's player files, "Talking twins"; never Claire's or Chris's looks). The two lists never change each other;
  it is never sent in co-op (the pick is the host's own look).

### The picks

Never in a pool but *Custom*'s: Claire's and Chris's looks (their cutscene models 0 and 1, which stand for their
player files, and Claire's second costume). They are the characters the story is played as, whose look "Character Model" decides; on
someone else they read as the player character (Steve at the end of Claire's part, 6100, just before the
switch to Chris, was drawn as Chris's cutscene model: it looked as if Chris's part began with a random
Chris instead of the chosen model). Claire and Chris themselves get no pick (above, "Claire and Chris are never
cast"). *Custom* lists these looks too (after the *All Models* entries, checked like any new entry): a player who
checks them wants them. The player's chosen looks (a catalogue model, an import) stay in the pools: the picks do
not depend on them, so both co-op screens pick alike.

`cast_model(seed, scene, character, ...)`: the five characters but Claire and Chris, then the creatures ("Creatures"), are assigned at once from a Fisher-Yates
shuffle of the pool by xorshift32 seeded with (seed, scene): each character in turn takes the first model
of the shuffle not taken and not its own look (its own cutscene model or player file); when only its own
are left it trades with an earlier character; with the pool used up (more characters than models: *Custom* with
few checked) a new round over it begins. With *Cutscene Models* (seven) each character gets another one. Every answer is the same
whichever character is asked, and it depends on neither the character played nor the room (Per Character):
the same in Claire's part and in Chris's.

* *Per Character*: scene 0, the same in every room (seed 1, *Cutscene Models*: Steve as Rodrigo, Rodrigo as Wesker
  in every room).
* *Per Cutscene*: scene = room file + 1: each cutscene room (`RM_<stg><rom><case>`) picks again,
  the same room the same; deterministic from (seed, room file, character), so both co-op sides pick alike
  and a revisit shows the same cast.
* A pick that cannot be loaded or mapped (texture memory, an imported model without a humanoid node map)
  leaves that record as itself (logged `cast: ... cannot be loaded or mapped`).

### Drawing

* **Slots**: one display slot per model the room shows (`ACTOR_CAST + k`, 6), shared by every record drawn
  as it; a seventh model leaves its records as themselves. Their model data share one 3 MB buffer
  (`cast_mem`; the cast's own models 0.1..0.2 MB, imported ones up to 1.07 MB, their skin kept outside it), given back once all the
  cast's slots are unloaded: with the room's textures (`spawn_glue.c spawn_release`, the door) and at
  `njReleaseTextureAll`. Each slot also has 128 KB for a face mask (`face_mem`).
* **As the cutscene double** ("Cutscene doubles"): every tick of a cutscene before the objects follow
  their links (`coop_control_remote` -> `actor_double_tick` -> `cast_tick` -> `actor_act`) the record is
  posed (humanoid retarget from its skeleton), the model's face mask takes the record's lip sync and eye
  angles (`face_step`), and the record's hand nodes are moved to the model's hands and turned as them (the
  double keeps the record's turn), so the hand pieces and props the script links there sit on the model's
  wrists along its forearms; then it is drawn in place of the record
  (`eneset.c bhDrawEnemy` -> `actor_put_enemy` -> `cast_draw` -> `actor_put_as`; records sharing a slot are
  posed in turn). Every cast model with a face mask gets it (`face_init`, whatever "Animated Faces" says),
  so with *Cutscene Models* every character talks (measured in the opening, Rodrigo as Claire's cutscene
  model: the model's lips run with his, its jaw 0..8.4 degrees against his 0..7.6). A record without a
  mask (the event models 42..49) leaves the model's face at rest. In single player `appear_on` includes
  `cast_n`, so `coop_control_remote` runs while a room has cast records.
* **Hands**: the cast's cutscene models have no hands; the room script links hand pieces (the
  character's own gloved fist, open hand, a hand holding something: objects `sys->obwp[4..31]` with ids
  1000..1137 in the rooms checked) to the record's hand nodes and swaps them per gesture. They are the
  record's character's hands: on another character's model they showed that character's gloves (Wesker's big
  black ones on Alexia in 9350, Alfred's pale hands and cuffs on Steve in 1020). A model with no hands of its own
  (the cutscene models, the player files) gets the unarmed pieces of the character it is (`actor_cast_hands`, as a
  creature's model: Claire's, Chris's, Steve's or Wesker's; the cast's other models by `appear_voice`, other men
  Chris's, women Claire's), and the record's one-node pieces are hidden as below; they stay only where the model
  is the record's own character (`actor_cast_hands_chr`: Claire's costume for Claire, Monster Steve for Steve) or
  its hands did not load. The unarmed piece is one pose (the fist or relaxed hand), so the script's gestures
  (open hand, pointing) are lost there. On a model with hands of its own (the zombies, the creatures, the imported models; `actor_own_hand`: a mesh
  or skinned vertices on its hand node) the record's one-node pieces are hidden while it is drawn as the
  model (`cast_glue.c hands_hide`: the piece's mesh `NJD_EVAL_HIDE`, shown again at the next tick, as
  `actor_weapon_hands` does for the weapons' hand pieces): before, Rodrigo's gloves lay over a zombie's
  own hands. A piece of more nodes holds a prop in the same mesh as the hand (Claire's hand with the
  lighter, two of Rodrigo's) and stays whole; items the script links to a hand are other objects and stay.
  The same for a model without that hand at all (the Bandersnatch's left). A creature's record has no pieces:
  a model without hands gets a character's unarmed ones there ("Creatures"). Ours are drawn on a hand only while no
  piece of the script's is drawn there (`cast_hands_draw` -> `piece_drawn`, the test of `objitm.c bhDrawObject`: a kept
  piece with a prop is the hand). The script keeps its pieces linked to the hand while it does not use them (left out
  of the drawing by `mdflg 0x1`; also `stflg 0x1000000`, or shown only in other cuts): those do not count. Before
  (until 2026-11-11) any linked piece counted, so a kept prop piece not in use left the hand bare, with the record's
  one-node pieces hidden and ours not drawn: Rodrigo in the opening (0000, both hands the whole scene: his pieces
  1131 and 1137 linked, not drawn), Alfred's left hand in 1020, Wesker's right in 8030 (event model 47) and, as the
  double, Chris's left in 9350 at times, on every model without hands of its own (the cutscene models, Claire's second
  costume, Monster Steve). The same for the double of the character played ("Cutscene doubles"). Wesker's afterimages (`trail_glue.c`) leave no trail of a
  hidden piece. Test aid: `RECVX_CAST_TRACE=1` logs each piece hidden (`... hidden: the model has Claire's hands`) or
  kept (`... kept: a prop in it`). Checked in 9350, 8030, 9370 (Wesker as Alexia), 1020 (Alfred as Steve; Claire's
  double as Rodrigo), 0030 (Steve as Rodrigo); on 2026-11-11 every kind of model (the cutscene models, Claire's second
  costume, the creatures, the zombies, Outbreak imports of every kind: high-detail with fingers, standard, the
  player characters) as the only model of *Custom*, in 0000, 0030, 0040, 1002, 1020, 3230, 6000, 6040, 8030, 9340,
  9350 and 9370, and as the double: each hand shows the model's own, our unarmed pieces, the record's piece (a prop
  in it, or the model is the record's character) or, on a model without that hand at all (Nosferatu's right, the
  Bandersnatch's left), none.
* **Head parts**: Claire's ponytail is an object of its own on her cutscene record's head (id 1200, 4
  nodes, node 5: 9150, 9320, 3210), and so are Wesker's sunglasses on his event model 47 (id 1127, 8030);
  the player's head part `sys->obwp[2]` when a script links it there. Drawn as another model, the record
  left them swinging or sitting in the air where its head is. Every object linked to the record's neck or
  head (the source's rig roles 4, 5: `actor_src_head`) is hidden while it is drawn as its model, all its
  nodes (`cast_glue.c parts_hide`, `NJD_EVAL_HIDE`, as the hand pieces), and shown again at the next tick,
  so they are back whenever the record is drawn as itself. The other objects linked to cast records sit on
  their hands (checked in 0000, 0030, 1020, 3100, 3210, 7030, 7220, 8030, 9150, 9320). The same for the
  double of the character played drawn as the player's look ("Character Model", Per Character, BioRand's
  pick; `actor_double_tick` -> `cast_double_parts`): Claire's ponytail stayed on it in her own part (9150:
  behind Alexia's or Wesker's head); there from object 3, `sys->obwp[2]` being the look's own head part
  (`self_cine_hair`). Kept on a player file of the record's own character (Claire's other costumes): it
  has no head part of its own, the record's is its (before, Claire's Model set to her second costume showed
  her without a ponytail in Chris's part). Test aid `RECVX_CAST_PARTS=0`: the parts kept.
* **Zero-length bones**: Rodrigo's and the hazmat man's shoulders are two nodes at one place, and the
  arm's rotation is the second's (see "Retargeting"). Taken from the first, the models on Rodrigo kept
  their upper arms near their rest pose, raised like a T-pose, while his hung down or lay on the table.
* **Afterimages**: Wesker's fast moves leave a trail of 3D afterimages (`effsub1b.c` effects 135/136, which the
  scripts of 1002, 9321, 9370 "Chris fights Wesker" and the Battle Game's 5820 link to his record): each frame the
  game copies the skinned vertices of one object of the record (his body, object 1, and his gloved fist, a hand
  piece) and draws the copy for five frames at that frame's matrix, fading (material alpha 200 down to 40). Drawn
  as another model, the record left Wesker's trail behind it. Now, at each copy, the model's node matrices are
  kept (`pc/glue/trail_glue.c pc_trail_grab`; `actor_glue.c actor_drawn_for`: the display actor in the record's
  place, the cast's slot or the double, posed from the record as its drawing poses it, so records sharing a slot
  each leave their own pose), and the afterimage is that model drawn again at them, with the game's alpha and
  light (`pc_trail_draw` -> `actor_draw_nodes`: an imported model's vertices skinned for that pose and back; the
  materials' alpha put back after). A hand piece the model hides (it has hands of its own) leaves none. Not
  swapped (the cast off, a record drawn as itself, a model without a humanoid map) the game's copy is drawn as
  before; the game still makes its copy, so an afterimage not kept (32 at a time, models of up to 128 nodes) is
  Wesker's, never a stale one. Checked in 9370 with Steve, Alfred, Alexia, Rodrigo, the Hunter, the Tyrant, the
  anatomist and imported models, in 1002 (two records sharing one slot) and 9321, and with Wesker played (the
  double as "Character Model"); with the cast off, frames identical to before.
* Off (and in every room without cast records, `cast_n` 0) none of this runs: frames are identical to a
  build without it (`layout_check.sh`). On, the game's state is unchanged: the opening, Steve's scene
  (0030) and a room after them gave the same flags, items and player position at every room load and the
  same player pose every game frame as with it off.

### Limits

* On a model without hands the hands are one unarmed pose of the model's character (no gestures), Alfred, Alexia
  and Rodrigo have none of their own (Chris's or Claire's are used); a piece that holds a prop in its own mesh is the
  record's and shown with its hand, on any model.
* Records drawn as the same model share its face mask: the one posed last talks.
* A line whose bank of mouth shapes the model lacks (or has still) is played with the model's first bank
  (as for the double); expressions keep the model's neutral face, only the eyes follow.
* Each model is read from its room file during the room load (as the enemy spawner reads one), at most
  six per room.
* Test aids: `RECVX_CAST=0|1[:all][:random][:imports]` sets the options; `RECVX_CAST_TRACE=1` logs the
  game's state (flags, items, the player) at each room load, each change between cutscene and gameplay,
  and every 10 ticks each cast face's lips, muscles and jaw against its record's;
  `RECVX_TEST_EVENT="f:level:ev,..."` starts a room script's event (a cutscene) without walking to its
  trigger.

## Voice lines

Setting "Randomize Voice Lines" (GAMEPLAY, group CUTSCENE CAST; the BIORAND screen's RANDOMIZER page under the
character model rows; the lobby's CUTSCENE CAST page, RANDOMIZE VOICES; ini `voice_random`, default 0;
`EnemyOpts.voices`): in cutscenes a character shown as another story character's model says random lines of that
character, and its mouth moves as for that line (also where the original line moves none: "Talking Mouths" below). Steve shown as Wesker says Wesker's lines; shown as a zombie or a
creature, he makes that enemy's own sounds instead ("Enemy models" below). It works with whatever
shows the other model: the cutscene cast's picks, Claire's and Chris's records shown as the player's look for them
(`coop_look_model`), and the played character's double shown as "Character Model" (also BioRand's "Randomize
Character Model" and *Per Character*), so it also works with the cast off. Code: `pc/src/host/voices.c` (the
catalogue and the picks, the enemy models' sounds; no game state, ctest `cast`), `pc/glue/cast_glue.c` `pc_voice_*`
(in the game), hooks in `event.c` (`bhVoiceOn`, `bhLipSet`) and `sdfunc.c` (`CheckPlayEndAdx`, `StopVoice`).

### Lines and speakers

* A cutscene line is op 25 `VoiceOn` (kind, voice number u16, mode, fade, record): file *voice* of `VOICE1.AFS`
  (stages 0..5) or `VOICE2.AFS` (6..9), streamed on ADX slot 1 (`PlayVoiceEx2`, partition `PatId[1]`, the disc of
  the room). Kind 0 positions the sound at the player, 1 at enemy record *record*, 2 at an object; every line on the
  US disc has mode 1 (not positioned), so the record is only a hint. The mouth is op 61 `LipSet` (bank, record,
  entry): entry *n* of the room's lip list (`sys->lspp`: `hed02` after the motion entries; size, then pairs phoneme
  0..5 and time, 0xFF last), on that record's face; op 63 `LipStart` starts it (`face.c bhControlMask`).
* The **speaker** is the record of the `LipSet` that follows the `VoiceOn` before another `VoiceOn`, `End` or
  `EvtEnd` (`voice_lipset`, at most 16 instructions; on the disc it is the next or the second next instruction).
  Without one, a `LipSet` right before it (only `LipStart` and `MessageSet` between, and not the line before's;
  `voice_lipset_before`, at run time the last `LipSet` the task ran, `pc_voice_lip`): two lines of the disc start the
  mouth first, Claire's cry that she cannot open the door while the researcher is killed behind the glass in 3130
  (1/76, a kind-0 line that named nobody, so it kept her voice) and one of hers in 6051 (2/283). A line without
  either: the `VoiceOn`'s record when kind is 1. A line placed at the player (kind 0) without one: the record that
  every kind-0 line of the same script with a `LipSet` moves the mouth of (at least one, all the same record;
  `voice_said_by`, from the catalogue's scan): Claire waking up after the crash in 6000 (2/1), Alfred in 1020
  (1/114). Else nobody (the line stays): the scripters also put other speakers' lines at the player (Steve's in 1001,
  Wesker's in 7070), so a kind-0 line is the player's only where its script says so.
* Counted over all 205 room files of the US disc (with `pc/tools/rdx.py`'s room reader): 673 `VoiceOn` (562
  distinct files), no `VoiceWait`. 451 have a `LipSet` after them and 2 one right before: the speaker is the face
  that moves. 194 more name a record (kind 1) and no mouth; where both exist the record and the `LipSet` agree in
  345 of 428 lines (81 %), so about one in five of these is said by someone else. 26 kind-0 and 3 kind-2 lines have
  no `LipSet`; 2 of the kind-0 ones get a speaker as above, the other 24 and the kind-2 ones name nobody and stay.
  The speaker's character is `cast_character` of the record's type (the seven; the hazmat man, the
  creatures and the zombies have none).

### The catalogue

The lines another role may say: every line with a `LipSet` on a story character's record, with that lip entry's
bytes copied from its room (`voice_scan_room`), and the lines checked by hand below. Sorted by character, disc and
file; a file in several rooms once (its first room's lips); a file two characters' `LipSet`s claim is nobody's (2
files) unless its speaker was checked by hand. Without the hand-checked table: 374 lines, Claire 116, Chris 45, Steve
124, Wesker 46, Alfred 13, Alexia 10, Rodrigo 20 (19 KB of lips; Claire's two whose `LipSet` comes first among them).
Nothing of the disc is in the repository.

* **When it is read**: from the start, before the first cutscene: `voices_frame` (`teleport.c`, every frame of the
  game thread) asks for it at the title as soon as the option is on (at the start, or turned on later) and again when
  BioRand's choice for the next game changes (`biorand_next`); `cast_room` asks at every room load with the option on
  (also a co-op guest's, whose option is the host's). `voices_want` then takes the catalogue it has, else
  `<save_dir>/voices.cache`, else reads the room files on a thread of its own (`vscan`, about 2 s, its own handle on the
  disc image; the teleport list reads them on its own when it is first needed). It is published whole
  (`voice_scan_done`: an atomic pointer to the new catalogue), so the game thread never sees one half built; a newer
  one replaces it, and the one before serves until then.
* **The cache**: `voices.cache` holds the catalogue for one key: the disc image's size and the place and length of
  each of its room files, the name, size and time of every mods and BioRand room file served, whether BioRand's are,
  the scan's format version and the hand-checked table (`voice_cat_hash`). The file: `RVXV`, format 1, the key, the
  counts, an FNV-1a checksum, then the lines, the sure lines and the lips (little endian, ~25 KB). It is written
  atomically (`pc_save_file`) after a build that read every file, and taken only when the magic, the format, the key,
  the sizes (they add up to the file's) and the checksum match; any other file is ignored (`voices: voices.cache is
  for other room files or damaged: read anew`) and replaced. Another key (other mods, BioRand on or off, a new pack,
  another disc, a new table) reads the room files again in the background.
* **Too early**: a line before the catalogue is there (the first start without a cache and a scene right away)
  stays quiet, as before (`voices: the lines are not read yet: line 1/0 quiet`).
* The log: `voices: the lines read from the room files (2364 ms): Claire 151, Chris 77, Steve 148, Wesker 72, Alfred
  14, Alexia 25, Rodrigo 20 lines (grunts 23 12 4 2 0 1 0, without lips 39 33 28 28 6 10 0); 135 without a LipSet said
  surely by their record` (with the first table: 507 lines, 133 more than the scan's own 374), or `voices: the lines
  from voices.cache (0 ms): ...` (20 KB).
* Measured (headless, the user's settings, `RECVX_TEST_SCENE` of 6000): the first start without a cache had the lines
  2.7 s after the start, at the title, long before the scene; the next start took them from the cache at 0.2 s; Claire's
  first line in 6000 (2/1, waking up) was replaced both times. A damaged cache (one byte changed) and a new mods room
  file were each read anew, and the start after the mods change took the new cache.
* Test aid: `RECVX_VOICE_CHECKED=0`: without the hand-checked table (the scan's own rules only, before and after).

### Lines checked by hand

A local tool outside the repository (the voice tool: a browser page served from the user's disc image, which plays
every line with the port's guess of its speaker) writes `voice_curation.json`; `pc/tools/voice_curation.py` turns it
into `pc/src/host/voice_curation.inc`, a table of ids and flags only (no sound, no text): per checked line its disc,
file, character (-1 none of the cast), flags `VOICE_CONFIRMED`, `VOICE_EXCLUDE`, `VOICE_GRUNT`, `VOICE_PART`, and a
group's next part (`voice_curated`, `cast.h`). The rules:

* **Speaker checked** (`VOICE_CONFIRMED`): the line is that character's, over the `LipSet`'s or the record's guess.
  In the catalogue it is that character's line, also without a `LipSet` (no lips: it is inserted with a still mouth,
  or with the script's own lips where the original moves one, as "Talking Mouths" has it: `the inserted line has no
  lips`). As an original, the record of that character in the room says it (the first one of that character; none
  there: the line stays, `its speaker, checked by hand, is not in the room`), and it is replaced when that one is shown
  as another; Talking Mouths counts its speaker as sure. "None of the cast" (narration, radio, crowds, creatures): never
  inserted and never replaced.
* **Do not use** (`VOICE_EXCLUDE`): never inserted. As an original it is replaced as any line where its speaker is
  swapped (the tool's help says "and not replaced", but these include Claire waking up in 6000, 2/1, and Alfred's
  line at the player in 1020, 1/114: kept, they would let the swapped speaker's own voice through again, "Never the
  original voice").
* **Grunt** (`VOICE_GRUNT`): a gasp, grunt or short sound, also a longer one (a scream). Such a line is in the
  character's own pool of grunts (also one without a `LipSet`, under the tool's guess of its speaker where it was not
  checked). A grunt is replaced only by a grunt of the character shown (`voice_pick` `VOICE_PICK_GRUNTS`, weighted as
  any line); a character without one stays quiet there (`... is a grunt and Alexia has
  none: quiet`). A line of 1.2 s or more that is not a grunt never gets a grunt (`VOICE_PICK_SPOKEN`); a short line not marked as one keeps the rules above (any line that ends in time,
  `VOICE_PICK_ANY`).
* **Groups** (`VOICE_PART`, `next`): lines the tool joined, said one after another as one line. The first part stands
  for the group: in the catalogue its length is all the parts' (`cast_glue.c group_ms`), and when it is inserted the
  parts play in order (`pc_voice_tick` starts the next part on ADX slot 1 when one ends; the next line, `VoiceOff`, a
  skip or the fade at `VOICE_GRACE_MS` past the original stop it, and no part starts after that). As an original, the line in place of its first part
  is picked to fill the time of all its parts, and its later parts stay quiet (the script still waits each one's own
  length). The later parts are never inserted on their own. (No groups in the table yet.)
* **Not checked**: the port's own guess as above.
* The log marks them: `voices: room 9350 record 2: Wesker's line 2/514 (451 ms, speaker checked), shown as Claire
  (Cutscene Model) -> Claire's line 2/283 (872 ms) from RM_6051, the speaker checked by hand: Talking Mouths: its lips
  added`; `Wesker's line 2/500 (4452 ms, a grunt), shown as ... -> Claire's line 1/507 (2640 ms, a grunt) from
  RM_1002`; `-> Claire's line 1/373 (2168 ms) from no scene` (a line no script says); a line that stays: `... stays
  (none of the cast says it, checked by hand)`.
* Measured (the first table: 171 speakers checked, 63 "do not use", 72 grunts, no groups; headless with the user's
  settings, 0030, 1020, 9350, 9370 and 6000): 41 lines inserted, 9 of them checked by hand (3 that no script says),
  2 grunts in place of grunts, 3 "do not use" originals replaced, no "do not use" line inserted, no grunt in place of a
  spoken line, every checked speaker kept; 68 enemy-model lines, 59 with one sound, 7 with two, 2 with three (Claire
  as a zombie on her 12 s line in 1020: 3 moans in 5.6 s). Every scene that ended lasted as long as without the
  table (97.3, 122.2, 125.9, 87.5 s), and with the option off 6000 read nothing and logged no line. Co-op (9350, 9370):
  the host, its lines read at the title, and the guest, its own option off and the lines read at its first room load
  with the host's, said the same 18 lines.

**Updating the table** after another pass with the tool (the tool saves `voice_curation.json` in its own folder):

```
python3 pc/tools/voice_curation.py /path/to/recvx-voice-tool/voice_curation.json
```

It writes `pc/src/host/voice_curation.inc` and prints the counts; rebuild. It refuses a file whose `characters` are
not cast.h's seven in order, or a `speaker` index whose name is another's (ctest `cast` checks the port's side). The JSON is the user's and is not in the
repository. A new table changes the cache's key, so the first start reads the room files again.

A model's character (`voice_model_chr`): the catalogue entries by type (`cast_character`: the cutscene models, the
event models, Alfred's other uniforms), the player files 10..13 (14, Claire's second costume: Claire). Every other
model has no lines: the creatures (Monster Steve, Nosferatu, the Tyrant, ...) and the zombies make their own sounds
("Enemy models"); the hazmat man and every **imported model** (an Outbreak character has no lines on this disc) keep
the line.

### In a cutscene

* `bhVoiceOn` asks `pc_voice_on`: with the option on (`enemy_opts()->voices`: the host's in co-op), the catalogue read,
  a speaker of a story character shown as another one's model (`shown_model`: the cast's slot, `cast_glue.c as_model`,
  else the double's `actor_double_shown`), whose character has lines: `voice_pick(seed, room file, disc, file,
  character, length, ...)`, a hash of the enemy seed, the room file and the line into that character's lines:
  * **One line, ending in time**: each original line gets exactly one line (or none: quiet), and always one that
    ends at most `VOICE_GRACE_MS` (1 s) after the original line's length while the character has one that is not the
    line it said last. Only where none of its lines ends in time but that one does it the shortest (closest to
    fitting) instead, which fades out at the original's end and grace ("The end of an inserted line"). The previous
    line stops when the next original starts (`PlayVoiceEx2` stops slot 1). So a speaker never talks on past its line
    with one or several lines of another.
  * **Not said yet first**, among the lines that end in time: the lines said since the room was loaded are kept in
    order (`VoiceSaid`, `voice_said_add`, `voice_said_age`: how many of that character's lines came since). A line
    not said yet in the room is taken before one said already. Once every one that ends in time was said, a line comes
    back only after **min(pool - 1, max(3, pool / 2))** other lines of that character (the pool: the character's lines
    of that kind with a length; Alfred's 14 spoken: 7, Rodrigo's 20: 10, Alexia's 24: 12, Claire's 128: 64; two
    lines alternate, three go in turn). So no line twice in a row and no A-B-A; across the rounds too, since nothing
    is reset when the lines run out. Where none of the lines that end in time is out of that cooldown, any of them
    but the last one said (an A-B-A then, never a line that runs on); for a gasp, quiet instead ("Short sounds").
  * **The length a soft preference** within those: a line weighs 1 + 5 r² (r the part of the original's time it fills,
    at most 1: a line that fills it is 6 times as likely as a one-word shout, one filling half 2.25 times), a gasp,
    grunt or shout (shorter than `VOICE_SHORT_MS`, 1.2 s) a tenth of that again in place of a line of 1.2 s or more,
    so spoken lines take the place of spoken lines.
  * Before (the soft window of `cca465b8`): a line longer than the original's time and grace was picked too (weight
    (max/len)², and a line not said yet in the room before any said one whatever its length), then faded out 1 s
    after the original's end. In 9370 and 9350 (headless, the user's settings, 6 runs, 4 seeds) 14 of 102 inserted
    lines were such longer ones (0 before that change), heard up to 4.5 s past the original's end (16 more than
    1 s past, against 2 before, both of them the pause after a short line below): the speaker went on talking into
    the scene's pauses and up to the next line, and several originals in a row sounded like one speaker talking
    nonstop with several lines.
  * The lines said in place of Claire's and Chris's count too, though each co-op screen shows those two with its own
    player's look: the screens pick alike except where one shows them as the character another role is shown as (that
    role then does not repeat their lines on that screen). A room load starts afresh (co-op players may be in other
    rooms), so a scene that goes on in the next room may say a line of the last one again.
  * Measured over every scene of the disc (120 room scripts with lines, 10 seeds including the user's, the user's
    table: pools Claire 151, Chris 77, Steve 148, Wesker 72, Alfred 14, Alexia 25, Rodrigo 20), the real `voice_pick`
    with the lines' lengths, one speaker at a time shown as each character: Alfred's lines back to back 1020 times in
    5150 lines before (the longest run 7 in a row; 308 A-B-A), none after (no A-B-A; a line again after 7 others at
    the least); Alexia 275 to 0, Rodrigo 182 to 0, Wesker 61 to 0 (his two grunts alternate: 42 A-B-A, the best two
    can do). The gasps of a character with few short lines go quiet more often instead (Alfred: 1040 to 1399 of 6190,
    his only line under 2.2 s, 1/242, said once in the last seven). With the lines that end in time first (the same
    run, 9 seeds and the user's, 35 834 lines): lines longer than the original's time and grace 3492 (9.7 %) with the
    soft window, 513 (1.4 %) now, only Alfred's (482) and Wesker's (31) where none of theirs ends in time but the one
    said last; still none back to back; A-B-A 53 to 731, nearly all Alfred's (671 of his 4722: of his 14 lines only
    a few are short, and a line again beats one that runs on), quiet gasps 3025 to 3156.
  The lengths are the lines' ADX headers (`cast_glue.c line_ms`, read once each). The same line in the
  same room with the same seed in the same scene order is always the same: replays repeat, and both co-op games
  pick alike (the lengths come from the disc, the order is the script's). The line is played from its own disc's archive (partitions 28 `VOICE1.AFS`, 29 `VOICE2.AFS`, both
  loaded at the first such line; a mods folder's `VOICE1.AFS/<n>` counts as for the game's), so Chris's lines can be
  said in Claire's part.
  The original line's speaker shown as itself, as its own character's other model, or anything without lines (and
  without sounds, "Enemy models"): the game's own line.
* The mouth: the `LipSet` after it (the speaker's) runs as the game's, then `pc_voice_lip` points the record's lip data
  (`exp0[268]`) at the inserted line's entry; `LipStart` plays it, and the model shown takes the record's lips as
  always (`face_step`: the bank past or still in the model's face table is its first). The bank stays the script's
  (each face table has its own banks; the phonemes are 0..5 in every entry).
* **Short sounds**: a line of a swapped speaker shorter than `VOICE_SHORT_MS` (1.2 s: gasps, grunts, cries on a hit)
  gets one of the character's own short sounds, by the same pick (a line ending at most 1 s after it: in the catalogue
  Claire has 35 under 1.2 s, Steve 30, Chris 23, Wesker 9, Rodrigo 1). It stays quiet where the character has
  none that short (Alfred, Alexia), or where those were said among its last few (above; `pc_voice_on` answers -1, the
  script still waits its length; log `... is a short sound and Alfred has none: quiet`), and then an inserted line before it plays on through it. The catalogue's lines
  all have a `LipSet` (so their lips); the lines without one are not added for this: the four characters with short
  sounds have enough of their own.
* **No line in the pause after a short one**: an earlier version gave a short line before a long script pause a whole
  line of up to 4 s (`voice_gap`, `VOICE_GAP_MS`); it is gone: a gasp gets a short sound that ends in time, or
  stays quiet, so a speaker never says more than its line in its place.
* **Timing**: the script waits for a line with `AdxCk` (op 27, 66 in the scripts, all slot 1); `CheckPlayEndAdx(1)`
  answers from `pc_voice_playing`: "playing" for exactly the original line's length (its ADX header, `adx_length_ms`),
  whatever the inserted line's, then "ended". So the scene keeps its timing; many scripts time their lines by frame
  counts instead (`FrameCheck`, 6000), which it does not touch either. No script uses `AdxTimeCk`.
* **The end of an inserted line**: it ends by itself (the pick prefers one that ends at most `VOICE_GRACE_MS` after the
  original would), at the next line (`PlayVoiceEx2` stops slot 1; `bhVoiceOn` first stops the task's own line), at `StopVoice`
  (`VoiceOff`, a skip), and at the latest `VOICE_GRACE_MS` after the original's end: a longer one (none of the
  character's lines ends in time but the one it said last) fades out there over 8 sound ticks with the game's own fade
  (`RequestAdxFadeFunctionEx`, `StopAdx` at its end; `vl.cap`, checked every tick by `pc_voice_tick`; log `voices: the
  inserted line fades out 1000 ms after the original's end`). The next line or a room change during the fade stops it
  at once; a room change before keeps the fade's time. A shorter line leaves a silence. Before, a line could run on
  well after the scene's speech: a line picked at random from all of the character's could be many times the
  original's length, and where the script had no next line (a scene's last line, a long gap, a scene timed by frames)
  nothing cut it (Rodrigo's last 1.8 s line of the opening went on for 11 s as Wesker's).
* **Measured** (`RECVX_TEST_SCENE`, 39 scenes of both discs, `RECVX_CAST_TRACE` with the slot's starts and ends in
  the log, `adxt: stop talk 1 at ...`; once with the user's settings: the cast, Claire and Chris as zombies, and
  once with Claire as Wesker and Chris as Steve): before, 32 inserted lines played on more than 1.5 s past the
  original line's end, up to 19.6 s (Wesker's 2.2 s line in 9370 as Claire's 22 s one); then none past 1.0 s, but
  for the 25 lines in a pause after a short one (that pause fill is gone now). Every scene that ended in the run (33 of 39) lasted as long to 0.1 s
  as before, and with the option off the runs were the same (0030, 6000, 9350, 3130: the same ADX starts and stops).
  In 9350, 9370 and 1020 (Claire as Wesker): the 19 short lines that were quiet are short lines of the character;
  the lines of 1.2 s or more fill 0.76 of the original's time on average (0.54 before), and none of them is a gasp
  or shout (29 % before).
* **One line per line, measured** (headless, the user's settings, a trace of every `VoiceOn` and every start on ADX
  slot 1 and SE slot 10): 9370 with 4 seeds and 9350 with 2 (148 originals), 1020, 0030 and 6000 with 2 (114).
  Every original of a character shown as another character started exactly one line (202 of 202 now; also 107 of 107
  before the soft window and 102 of 102 with it), or none (quiet: 39); a speaker shown as a monster made one to three
  sounds ("Enemy models", unchanged). Heard past the original's end (the line's played time minus the original's
  length; in 9370 and 9350): with the soft window p50 0.21 s, p95 2.0 s, max 4.5 s, 16 of 102 more than 1 s, the
  lines heard 0.86 of the originals' time; now p50 0.18 s, p95 0.88 s, 1 of 101 more than 1 s (Alfred's shortest
  line, 3.2 s, for a 1.4 s line: none of his ends in time, faded out), 0.77 of the time, as before the soft window
  (0.77; 2 of 107 more than 1 s, both in a pause after a short line). In 1020, 0030 and 6000 now: 101 lines, p95
  0.87 s, 3 more than 1 s, all Alfred's 3.2 s line in place of a 1.2..2 s one, faded out. (Headless runs stall now and
  then, so a fade can take longer there than its 9 sound ticks in the game.) The same seed again said the same
  lines (9370, 9350), and in co-op the host and the guest (its own option off) said the same 18.
* The **mouth** follows the inserted line wherever the original line moves a mouth (two thirds of all lines: every
  catalogue line has lips): measured 19 of 30 lines in 0030 (the rest have no `LipSet`), 25 of 32 in 9350, 4 of 4 in
  1020; the faces' jaw then runs with the inserted line's phonemes (`RECVX_CAST_TRACE`: `voices: record 2 moves its
  mouth with the inserted line's lips`). A line without a `LipSet` keeps a still mouth unless "Talking Mouths" gives it
  one (below). The mouth moves only on a model with a face (the cutscene models; a player file on the double with
  "Animated Faces" on, its talking twin; an imported model's jaw, `imp_face`).
* **Subtitles** stay the script's: the original line's text (`MessageSet` is its own op, timed by the script; the US and
  European English rooms have no subtitles at all). The text tells the story while the voice is the joke; the inserted
  line's subtitle would be another room's message, in some languages a message the room does not have.
* **Translated voices**: the language imports change only the title call (`ADV.AFS/3`, "Translated Title Call",
  `g_lang_voices`); no import has `VOICE?.AFS` files, so the cutscene voices, inserted or not, are the disc's in every
  language.
* **Co-op**: the option goes with the host's enemy options (`EV_EOPTS` `voices`, protocol 38). The cast's picks are
  the host's on both screens, so both games say the same lines for them (checked: host and guest, the guest's own
  option off, picked the same 12 lines in 9350 and 9370, also with the length weighting and the lines not said
  again). Claire and Chris show each player's own look (and the
  played one's double each player's "Character Model"), so their lines follow each screen's look.
* The log names every inserted line: `voices: room 9350 record 2: Wesker's line 2/448 (8278 ms), shown as Steve
  (Cutscene Model) -> Steve's line 1/112 (877 ms) from RM_1050, with its lips` (`, none shorter: faded out` after the
  length where it is cut).
* **Never the original voice**: a speaker that is swapped says no line of its own. The paths that let one through
  before, and now: a kind-0 line of the player's character without a `LipSet` (Claire waking up in 6000: now her
  record's, "Lines and speakers"); a line whose `LipSet` comes before it (Claire behind the glass in 3130: now hers,
  and its lips are the inserted line's, set afresh as `LipStart` has already run); a short line (now one of the character's short sounds, else quiet); a line while
  the catalogue is still being read (the first seconds after the first room load of a session: now quiet, `voices:
  the lines are not read yet: line 1/0 quiet`, where it stayed). Every cutscene line goes through `bhVoiceOn`; the
  scripts have no `VoiceWait` and no other op plays slot 1 (the title call is `adv.c`'s); the player's vocal sounds
  are SE bank 4 ("Voices"), not dialogue. Still the original: a line whose speaker the script does not say (the
  kind-0 and kind-2 lines above that name nobody; a kind-1 line whose record is the listener, about one in five).
* Off: `pc_voice_on` returns at once and plays as `PlayVoice` did; nothing is read.

### Enemy models

A speaker shown as a zombie look or a creature (a catalogue entry whose type `voice_creature` knows) makes that enemy's
own sounds in place of every line of it, from the speakers above (the cast, the player's looks, the double).

* **The sounds**: every enemy sound is a program of SE bank 3, which a room's sound pack (`RM_SRRC.SPQ` in
  `MULTSPQ1/2`) fills with the room's own types' sounds; the numbers are per type and collide between types (ENEMIES.md
  "Sounds of added types"). `voices.c` names, per type, a room whose bank 3 has that type's sounds and none of another
  type's under those numbers, and the programs: the ones the type's code requests on the **enemy voice channel**
  (SeNo bit 24, `RequestEnemySeBasic`'s `SeNoV`: moans, barks, screams), else its hurt and death cries, else what its
  moves sound like. Program numbers only; the sounds are read from the disc. Lengths below from the US disc.

  | Shown as | Room | Programs | What |
  |---|---|---|---|
  | Zombies (every `id` 1 look) | the look's own room | 20..23 + `add_seno[type]` (`en01.c bhEne01_SePlay`, `ene01_typ_tbl` by model version) | that look's four moans (0.5..2.3 s) |
  | Scripted zombies (51, 52) | their room (Bandaged 2: RM_0030, RM_9050's bank is the spiders') | any of 20..63 there | the moans of the room's zombies |
  | Anatomist Zombie (26) | 0110 | 20..23 (`bhEne26_SePlay`) | moans (1.4..2 s) |
  | Nosferatu (15) | 5800 | 10, 11, 12 (13 is in no bank) | screams (0.7, 2, 2 s) |
  | Monster Steve (17) | 9280 | 2, 8, 9 | roars (0.7..1.8 s) |
  | Alexia, First Form (12) | 5820 | 7, 8, 9 | shrieks (0.3..0.45 s) |
  | Hunter, Hunter (Sweeper) (5) | 8010 | 17, 18 (`bhEne05_InitDamage`: hit, death) | a short cry and a 1.5 s scream; the Hunter has no voice-channel sounds |
  | Bandersnatch, Bandersnatch 2 (9) | 2000 (3130 has no bank 3) | 12, 14, 15, 16 | growls (0.5..1.4 s) |
  | Tyrant, Tyrant 2 (19) | 5010 | 5, 6, 7, 9, 13 | blows and stomps (0.2..0.9 s): its voice-channel programs 2..4 are in no room's bank |
  | Cerberus (4) | 3020 | 2, 3, 4, 6, 7, 9, 13, 14 | barks, growls, howls |
  | Albinoid adult (22), infant (21) | 7100; 3140 | 1, 2, 4, 5, 6, 10, 11; 0, 1, 2 | cries |
  | Black Widow (3), Giant Black Widow (23) | 7080; 9050 | 3, 4, 14, 16, 17; 3, 14, 16, 18, 20 | hisses, hurt cries |
  | Moth (6), Bat (7), Spotter (11), Ant (20) | 6030, 0130, 7081, 9170 | 0, 3, 4; 5, 6, 7; 1, 2; 0..3 | their cries |

  **Keep the original line**: the fish (8: no sound program in any room), the man in the hazmat suit (99: not an
  enemy), the story cast (lines of their own) and every imported model. Outbreak's enemy models (its zombies `HE01_*`,
  `E28`, `E45`) are extracted but never offered as models (`IMPORT_OFFERED`: players and NPCs only), so no import is
  an enemy. The four-legged, flying and crawling kinds (Cerberus, the spiders, moth, bat, ant, albinoids, spotter)
  have no humanoid skeleton and are never shown in a cutscene (`cast_room`, the double: `actor_rigged`); their sounds
  are listed for when they are.
* **Loading**: at the first such line in a room, `snd_xbank_load(room)` reads that room's bank 3 from the disc into
  one of the 31 extra synth banks (heap, ~0.2..0.6 MB, released with the room by `spawn_room_setup`; shared with the
  enemy spawner's when the room is the same); `snd_xbank_prog_ms` keeps the programs the bank has, with their lengths
  (the driver's count). None there (a zombie look whose room lacks its group): the next place, any moans in that room,
  then RM_0030's.
* **Playing**: a few sounds per line (`voice_creature_plan(seed, room file, disc, file, ...)`): a line shorter than
  `VOICE_CR_LONG_MS` (2.4 s) gets one, `voice_creature_pick`, a hashed pick among the sounds that end at most
  `VOICE_GRACE_MS` after the original line, weighted as `voice_pick` by how much of the line's time they fill, and not
  the sound the same model made at the line before in the room while another ends in time (none ends in time: the
  shortest; a line of unknown length: any). A longer line gets up to three, `VOICE_CR_GAP_MS` (0.35 s) apart, which
  with their gaps take at most half of the line's time: each picked as above to fill what is left of that half, never
  the one just before (and the first not the line before's last); the first always plays (none that short: the
  shortest). So a long line is a cry or two with pauses, never the whole line of sounds. A short line (under
  `VOICE_SHORT_MS`) gets one only if one is that short, else it stays quiet. `bhVoiceOn` then plays no ADX
  (`pc_voice_on` returns -1); the first sound starts at once on SE slot 10 (the shell casings', idle in cutscenes)
  from the extra bank (`pc_se_xbank`), unpositioned at full volume as every line on the disc (mode 1), the next ones
  each a gap after the one before ends (`pc_voice_tick`). The script waits the original line's time as above, so the
  scene's timing stays. They end by themselves, or at the next line, `VoiceOff`, a skip or a room change (`voice_end`:
  `StopSe`, no more of them), and at the latest `VOICE_GRACE_MS` after the original's end (`cr.cap`). Before, the
  sounds were once chained to fill each line's whole time, so a long line was a run of cries and a scene with many
  lines sounded like one looping sound (the Hunter's two cries over Wesker's eight-second line in 9350: nine of them in
  a row); then one per line, which left long lines mostly silent. Nothing loops by itself: a program whose first
  sample loops has no length (`snd_xbank_prog_ms` 0) and is never picked, and the driver keys a sound off at its
  length.
* **Mouth**: no lips of the sounds; the `LipSet` runs as the script has it, so a model with a face moves its
  mouth with the original line, as before.
* **Co-op and replays**: the sounds come from the seed, the room file and the line (and the sound before, in the
  scene's order), like the inserted lines: the same in every run and on both screens (checked: host and guest, 12
  lines in 9350 and 9370, the same sounds); no protocol change. Measured with Wesker as the Hunter in 9350 and 9370:
  one sound per line (28 lines; before, 9 cries in a row for an 8 s line), the scenes as long as before.
* The log: `voices: room 9350 record 2: Wesker's line 2/448 (8278 ms), shown as Hunter -> 2 of its sounds from RM_8010
  bank 3: 18 (1466 ms), 17 (514 ms)`.

### Talking Mouths

Setting "Talking Mouths" (right under "Randomize Voice Lines" in GAMEPLAY / F1, the BIORAND RANDOMIZER page and the
lobby's CUTSCENE CAST page, TALKING MOUTHS; shown while the voice lines are on; ini `voice_mouths`, default 1;
`g_voice_mouths`, `cast.h`): an inserted line in place of a line without a `LipSet` moves the speaker's mouth too.

* **What moves it**: the inserted line's own lip entry. Every catalogue line from a `LipSet` has one (none of the 450
  lip entries the disc's lines use is a still mouth: each has phonemes other than 0), and it came with that line from
  its own scene, so it fits the sound. No mouth is made from the sound's loudness: a line checked by hand that no
  `LipSet` moves has none, and keeps a still mouth (`the inserted line has no lips`). (The
  game's own lines without lips, with the voice lines off, are left alone; they have no lip entry, so they would need
  one made from the sound, as `imp_face` does for the player's voice.)
* **When**: `cast_glue.c mouth_add`, from `pc_voice_on` for an inserted line without a `LipSet`, if all hold:
  * the speaker is **sure** (`voice_sure`, from the catalogue's scan, `voice_scan_room`): the line's `VoiceOn` names
    a record (kind 1; the 30 lines that name nobody never count), and in the same script at least 9 in 10 of the lines
    with a `LipSet` that name that record are said by it (the `LipSet` moves that record's mouth); a record no such
    line names counts only if every line of the script with a `LipSet` names its speaker. The scripters used the
    record as the speaker in some scripts and as the listener now and then in others; a script that names its
    speakers for one record does so for its other lines too. Tried on the US disc's 428 lines that have both, each
    judged by the other lines of its script: 294 pass, 284 of them right (97 %, against 81 % for all). 133 of the 191
    lines without a `LipSet` pass, and the 2 placed at the player that get a speaker ("Lines and speakers"; log: `voices:
    lines with mouth movements: ...; 135 without one said surely by their record`). In 0030 Steve's seven lines pass, Claire's four do not (one of the three lines naming her is Steve's);
  * the record is drawn as a model now (`shown_slot`: its cast slot or the double; not hidden, `stflg` 0x41000000)
    whose model moves a mouth (`actor_glue.c actor_talks`: a face mask with a bank that moves, or an imported model's
    jaw; the double only with "Animated Faces");
  * its mouth is not moving already (`exp0[0]` bit 0x4: the scene's own lips stay).
* **How**: as `LipSet` and `LipStart` would: the record's lip data (`exp0[268]`) set to the entry, its `LIP_WORK`
  restarted, bit 0x4 set; its bank stays the record's last. The models shown take the record's lips as for any line
  (`face_step`, `imp_face`). It ends with the entry, the next line, `VoiceOff`, a skip (`pc_voice_stop`: `StopVoice`)
  or a `LipSet` of the script for that record (`pc_voice_lip`: the script's lips start afresh) (`mouth_stop`, only
  while its lips are the record's, in the same room).
* **Timing**: nothing of the script or the sound changes; only the face.
* **Co-op**: each screen's own (a face only; not in `EV_EOPTS`, no protocol change): the lines are the same on both
  screens, the mouths follow each player's setting. The lobby row is not locked for a guest.
* The log: `... from RM_0000, no lips in the script (the speaker is the VoiceOn's record): Talking Mouths: its lips
  added`, else `a still mouth, ` and why (`Talking Mouths is off`, `the speaker is not sure`, `its model is not
  drawn`, `its model has no mouth`, `its mouth moves already`).
* Measured (`RECVX_TEST_SCENE`, seed 1, the cast on): in 0030, 0040, 1002, 6060, 5001 and 9350 41 inserted lines
  had no `LipSet`; 24 were given a mouth (0030 7, 0040 6, 6060 6, 9350 3, 1002 1, 5001 1), all with the inserted line's
  lips; the other 17 kept a still mouth: 4 speakers not sure (Claire in 0030), 10 shown as the double without
  "Animated Faces", 3 not drawn. The face trace (`RECVX_CAST_TRACE`: `model lips 1`, the jaw moving) and the frames
  show the mouth moving (0040 frames 2361..2370: Steve shown as Rodrigo talks with it on, keeps his mouth shut with it
  off). Every scene lasted as long to 0.1 s with it on, off and the voice lines off (97.3, 81.2, 106.3, 44.4, 89.7,
  125.9 s); with it off the lines and the face trace were the same as before.

### Limits

* About one in five lines without a `LipSet` (195) is given to the record that is only its sound position; such a
  line may be replaced although its real speaker is not swapped, or kept although it is. With Talking Mouths about
  one in thirty of the lines it gives a mouth may move the listener's mouth.
* The inserted lines are the lines with mouth movements and the lines checked by hand; the other lines without them
  (off-screen calls, many short exclamations) are never inserted.
* The hand-checked table covers the lines checked so far (198 of 691 in the first pass); the rest keep the guesses.
* An inserted line or sound can go on up to 1 s after the original line's end, also past the scene's end.
* A line the script names no speaker for keeps its voice (the kind-0 lines of scripts that put several speakers at
  the player, the kind-2 ones); see "Never the original voice".
* Enemy sounds: which program is a cry was read from the code (the voice channel, the hurt and death calls) and the
  samples' lengths, not by ear; the Hunter has two, the Tyrant none of its own (its blows stand in).
* Talking Mouths moves no mouth for the game's own lines (the voice lines off, or a line kept), and none where the
  speaker is not sure (58 of the 192 lines without a `LipSet`) or shown with a still face.
* Test aid: `RECVX_VOICES=0|1` sets the option.

## Limits

Budgets for enemies (slots, arena, textures, sounds): [ENEMIES.md](ENEMIES.md) "Budgets on the PC".

| Resource | Limit | Where |
|---|---|---|
| Objects / items | 32 `obwp` (0..3 player), 32 `itwp` | `system.c bhFirstGameStart` |
| Effects | 512 `eff[]`, 450 effect models `sys->efm` | `main.c`, `SYS_WORK` |
| Enemy types | 100 jump entries; textures `et_lp[100][16][16]` | `eneset.c`, `room.c` |
| Motions | 512 per set; 64 tracks per motion (`NormalTbl`), at most the model's nodes | `Motion.c` |
| Sub-models | 16 per actor (`mdl[16]`) | `BH_PWORK` |
| Player buffers | model 128 KB, base motions 384 KB, weapon motions 64 KB | `bhInitPlayer` |
