# Enemies

How a room spawns and runs its enemies, what the PC port changed, and the features built on
it: the F2 enemy spawner and the enemy options (extra enemies, rearranged placements,
randomizer). Related: [ACTORS.md](ACTORS.md) (models, skeletons, motions, the actor
catalogue), [modding/ROOM_FORMAT.md](modding/ROOM_FORMAT.md) (room file layout, records,
scripts and opcodes), [COOP.md](COOP.md) (host-authoritative enemies),
[USER_GUIDE.md](USER_GUIDE.md) (cheats).

Paths are relative to `decomp/src/ps2/veronica/prog/` unless noted. Room counts come from
`pc/tools/enemies.py` over all 205 room files.

## Summary

* Every enemy of a room is a **record** of the room file (759 in all rooms), realized at load into
  the slot of the same index. **No script op creates an enemy**: scripts hide, show, drive and
  heal records; a "script-spawned" enemy is a record hidden at entry and shown on a flag.
  Everything else is spawned by enemies (zombie parts, helpers, babies, eggs, segments).
* A type's model, motions and textures exist **only in room files**. Room variants are separate
  files (`RM_SRRC`, C = case, picked by op 55 `RoomCaseNo`). Difficulty never changes placements.
  111 hostile records have no kill bit and come back on every visit.
* The PC lifts the PS2's texture, helper-code and enemy-slot budgets (**255 slots**, 128 on the
  PS2); the slots are still what runs out first ("Budgets on the PC").

## Where a room's enemies come from

### Records, realized at room load

`system.c bhSysCallMonitor` mode 4 reads the room file; `room.c bhSetRoom` walks the enemy
records: every record with `flg & 1` goes through `room.c bhSetEneMdl` -> `eneset.c bhSetEnemy`,
which takes the **first free slot** of `ene[]` (128 slots on the PS2, 255 on the PC; at load: slot `k`
for record `k`).
`bhFinishRoom` assumes slot `k` = record `k` for motion sets and matrices, and every script op
addresses a record as `ene[rom->enep[k].wrk_no]`. Record order and count are therefore
load-bearing.

Init runs on the slot's first enemy tick (`mode0 == 0`). Enemies present at load get their first
tick from `bhInitEvent` (the entry script `scd0`), before the first frame is shown.

### What the scripts do to records

| Op | Effect |
|---|---|
| 13 `DieCk rec, bit` (every frame in `scd1`) | the record is dead (`flg & 2`): set `ed_flg` bit (`& 0x3FF`) |
| 34 `EneSetCk rec, bit` (in `scd0`) | `ed_flg` bit set: hide the slot (`stflg \|= 0x1000000`); bit masked with `0x1FF` |
| 33 `DieEventCk n, bit, cmp, count` | count of set bits `bit..bit+n-1` compared ("all dead" checks) |
| 36 `InitModelSet 1, rec, 0/1` | hide (also `flg \|= 0x8000`) / show the slot |
| 101/104/0xD3 `LoadWork 1, rec` | an event task works on this enemy (controllers 100/103/105 move and animate it) |
| 45/48 `InitMotionPause(Ex) rec` | the enemy plays room motions for the script (`mode0 = 5`, `bhEne_Event`) |
| 7 `CmpW rec, 10, ...` / 9 `SvW 11..15` | test / set HP (SvW only records 0..4) |
| 153 `EnemyHpUp`, `EneLifeSetM` | raise / set HP |
| 142 `ZombieUpDieCk`; 81/83/84 links, 121 `EneSeOn`, 125 positioning, render/look/face ops | other per-record effects |

The 569 hostile records (ids 0..30, 53..56, 71):

| Class | Records | Meaning |
|---|---|---|
| free | 283 | not hidden at entry, not touched by an event (186 with a kill bit) |
| event-driven | 252 | `LoadWork` targets: cutscene actors, enemies that rise/burst in on cue, bosses |
| hidden at entry, shown on a condition | 13 | e.g. RM_0060 #3 (shown once `ev_flg` 116 is set and kill bit 15 is clear) |
| hidden at entry, shown by events | 16 | e.g. RM_3020 Cerberus, RM_3160 zombies |

`pc/tools/enemies.py room ROOM` prints each reference with its enclosing conditions (e.g.
"after story event X": `Ck ev_flg X set; Ck ed_flg K clear; InitModelSet 1 rec show`). 14 rooms
branch on the character played (`CmpB ply_id`); none on `gm_mode`.

### Spawned at run time by enemies

| Parent | Children (ids) | Where |
|---|---|---|
| zombie (1) | linked parts, ids 31..38: link, head, 2 arms, 2 legs, + cap / worm / bomb by model version | `en01.c bhEne01_SetLinkEnemy`; 7 extra slots for a plain zombie |
| Hunter (5) | 1 helper (31) | `en05.c` |
| Black Widow (3) | leader, children (31), 15 babies (24) | `en03.c` |
| Giant Black Widow (23) | children (31) | `en23.c` |
| moth (6) | leader, children (31), eggs (27) on the player | `en06.c`, `player.c bhCheckMothEgg` |
| Gulp Worm (2) | segments | `en02.c` |
| Alexia 2 (13) | tentacles (18), body part (31) | `en13.c` |
| Monster Steve (17) | arms (31, 32) | `en17.c` |
| tentacle (29) | a damage dummy | `en29.c` |

`bhSetEnemy` returns NULL when the slots are full, and none of these callers checks it.

## Data an enemy type needs

### Per room file

| Data | Room file table | Realized by | Lives in |
|---|---|---|---|
| Model: chunk list (MLB sub-models, `SKIN`, `MASK` for ids > 90) | `hed01`, one per record | `bhSetEneMdl` (relocates in place) | room arena |
| Motions: 512-entry set per type | `hed02`, first record of the type that has one; others share `sys->emtp[id]` | `bhFinishRoom` / `bhSetEneMtn` | room arena |
| Textures | `hed04`, one per first (type, model version, textured sub-model) | upload list built by `bhSetRoom` | texture heap; released by `bhReleaseMainTexture` at the door |
| `O_WORK` matrices, AI work (`exp0`) | - | `bhKeepObjWork`, the type's Init | room arena |

Also from the room: **enemy floor areas** (floor records of type 2 whose `prm0` is the enemy
type: Cerberus wake-up areas, Hunter stairs and jump spots, zombie areas), read by
`zonzon1.c bhEne_CheckEventAtr`; **route boxes** (`rutp`/`ruttp`, 114 rooms) for path finding
(`rutchk.c bhCheckRoute`); water areas (fish, albinoids); the room's **effect textures** (effect
texture ids 30..99 and 400+) and its **sound bank** (SE bank 3 with the sounds of the room's own
types).

Global: the effect code, effect textures 0..29, the player's part file (the moth egg uses
Claire's sub-models 6/7 and player motions 90..97; other characters lack them, and the original
code crashed in `bhEne27_Init` on them: on the PC a moth lays no egg then, `en06.c
pc_moth_egg_ok`), the enemy code. An effect's *texture* id
(`O_WRK.tex_id`, through `sys->ef_tn`) is not its effect id: a few global enemy effects draw with
a room effect texture ("Effect textures of added types").

### Budgets on the PC

| Resource | PS2 | PC | Notes |
|---|---|---|---|
| Enemy slots | 128 (`ene[]`) | **255** (`PC_ENE_SLOTS`, `pc_prelude.h`) | retail uses up to 76 records (RM_9170, ants); a zombie takes 8. First free slot, so vanilla rooms use the same slots. 255 is the most a byte holds with 255 as "none" (`ETTY_WORK.wrk_no`, `ATR_WORK.prm3`, the co-op packets); an effect's sound requests moved from enemy 127 to 255 |
| Room arena | 12.25 MB incl. ~1.8 MB player data | **+64 MB** (`PC_ARENA_EXTRA`) | allocators never check `sys->endp`; the map/file/option screens copy the room's textures above `sys->memp` (2.2..6.1 MB), which must stay free |
| Texture heap | 10 MB (`Ps2_tex_buff`) | **64 MB** (`PC_TEX_HEAP_SIZE`, `pc_prelude.h`) | full = `exit(0)`; retail rooms use up to 7.6 MB |
| Texture entries | 256 (`tbuf`) | **1024** (`PC_TEX_ENTRIES`) | first-free and bump allocation, so vanilla rooms use the same entries as on the PS2 |
| Texture slots per type | `sys->et_lp[100][16][16]` | model versions 16..63 in a table of their own (`room.c pc_et_lp`) | on the PS2 zombie model versions >= 16 alias other types (21 = Gulp Worm 5, 32..42 = Black Widow 0..10); no retail room combines them |
| Spawned types' data | room arena | **32 MB pool** (`spawn_glue.c`), emptied by `bhSetRoom` | model + motion chunk lists, 6 KB..1.1 MB per type |
| Motion sets | 512 per type, `sys->emtp[128]` | unchanged | |
| Enemy sound bank | one SE bank 3 per room | **31 extra banks**, one per source room, released with the room | "Sounds of added types" |
| Helper code | `bhJumpEnemy[31..38]`, global, shared | **per creator type** (`eneset.c pc_call_enemy`) in rooms with added enemies; children made before the room got them keep the shared function as it was then (`pc_snap`: RM_6030's zombie parts run the moths' code) | users: zombie 31..38, Monster Steve 31/32, Gulp Worm, Black Widow, Hunter, moth, Alexia 2, Giant Black Widow 31. A part runs its creator's function; retail rooms keep the shared table |

With these budgets 43 of the spawner's 58 entries fit in one room (RM_0060, `RECVX_SPAWN_SURVEY_ALL`;
26 with 128 slots) before the slots run out, with 702 texture entries, 52 MB heap and 25 MB spawn
memory still free; 30 added zombies, a Hunter, a Bandersnatch and a Cerberus take 249 slots there
(with the room's own 30). What still limits:

* The slots: records stay within 128 (the room tools, the enemy options' tables), a slot within a
  byte. More slots would need wider fields in the room format and a co-op protocol change.
* Time: 249 active slots cost ~3.5 ms of game logic per frame (0.7 ms empty, budget 33 ms).
* Co-op: each frame carries 48 snapshots (`COOP_SNAP_FRAME`), so with 249 active slots each
  enemy's state arrives every ~5 frames ("Co-op").
* The dual-gun lock-on (`playpch.c`) lists at most 64 enemies in front; the rest are skipped.

Added for mixed rooms (none of it occurs in retail rooms):

* Slot counts come from the types' Inits, plus 6 kept free (a Gulp Worm takes 19).
* An added albinoid adult's light (`en22.c` links it by slot) uses the slot beyond the room's
  records; a grab plus another type's hit at once clamps the damage level (`player.c
  bhCPM0_damage`).
* **One enemy takes a player at a time** (`eneset.c pc_call_enemy`): the enemy whose code set the
  player's busy flag (`flg & 4`) owns the player until it clears; other enemies' changes to that
  player work meanwhile are undone (parts count as their parent; the co-op partner too). A zombie
  refused this way lets go (`coop_glue.c pc_enemy_unhold`). The moth's and the Black Widow's
  leader counts as the holding body's family (`pc_family`): the body starts the grab, the type's
  one leader (an unlinked enemy of the same id that the first body's Init makes) plays it and
  lets go (`bhEne06_PlayerControl`, `bhEne03_PlayerControl`); refused, the player stayed grabbed.
  The albinoid adult and Monster Steve step the hold of any player in `mode0` 4/6 (`bhEne22` /
  `bhEne17` `PlyDG*`, which read a position table by the player's motion); while another enemy
  holds the player (`pc_enemy_refused`) they leave it alone. An infant albinoid's hold (motion 42)
  made the adult read `mtn_pos[42 - 30]` of 4 entries: crash.
* Watchdog (`coop_glue.c pc_enemy_watch`): an alive enemy unchanged for 150 steps is logged; a
  zombie stuck in a grab state is released.

* A Tyrant added to a room (a corridor) that turned beside a player lying next to it pushed the
  player through the wall in one tick (its body is an oval up to 16 units long); in rooms with added
  enemies a push across a wall is left out (`en19.c pc_ty_push_ok`).

Mixing types the PS2 never mixed can still show odd behaviour (a Hunter may leap out of a room).

### What breaks when a type the room did not load is added

| Missing | Effect | Handled by the spawner |
|---|---|---|
| model / motions | nothing to draw, `bhSetMotion` on an empty set | loaded from a source room file |
| textures | the instance owns an empty `et_lp` slot: wrong or no texture, never freed | uploaded from the source room's `hed04` entry, released with the room |
| helper slots | a zombie's head runs the Hunter's helper or vice versa | each type's parts run its own code |
| children's slots | `bhSetEnemy` NULL -> crash in the parent's Init | refused unless the type, its children and 6 more slots fit |
| floor areas | waiting variants never wake (Cerberus 1/2); no stairs/jumps for the Hunter | spawns variant 0 |
| effect textures | effect texture 0 (a page of kanji) instead of the room effect texture | uploaded from the source room; none: not drawn ("Effect textures of added types") |
| sounds | the type plays its ids against the room's bank 3: another type's sounds | the source room's bank 3 as an extra bank; none: silent ("Sounds of added types") |
| CLUT banks | an asset's fixed palette bank could overwrite the room's | not handled; not observed |

## Runtime

* `ene[]` (`BH_PWORK`, 0x580 bytes; 128, the PC 255); `sys->ewk_n` = highest used slot + 1; free =
  `!(flg & 1)`. `eneset.c bhControlEnemy` runs `bhJumpEnemy[id](ep)` then `bhEne_InitDamage` for
  every used slot not hidden/frozen (`stflg & 0x41000000`) or linked to a hidden parent; co-op
  hooks wrap the call. `bhDrawEnemy` draws the same set.
* `bhJumpEnemy[id]` -> `bhEneNN` (`enNN.c`) -> `bhEneNN_Mode0[mode0]`: 0 `Init` (work, HP from
  the difficulty table, collision), 1 `Move` (the brain picks `mode1`/`mode2`), 2 `Nage`
  (grabs/throws, the player animated from the enemy's motion set), 3 `Damage`, 4 `Die` (sets
  `flg & 2`), 5 `bhEne_Event` (script acting). Types with their own code: 0, 1..27, 29, 30, 53,
  54, 55, 71; 56 is a scripted object; other ids up to 99 run `subpl.c bhSubpl` (NPCs, no AI). The
  AI targets `plp` (co-op swaps it per enemy).
* **Death and kill flags**: the body stays in its slot until the room is left. `scd1` runs `DieCk`
  every frame; `ed_flg` is in the save, so kills persist. `scd0` runs `EneSetCk` on the next visit
  and hides dead records; records without a `DieCk` respawn. Kill bits above 511 would not hide
  (retail uses up to 357). "All dead" checks (`DieEventCk`): rooms 3210, 4012, 5550..5710.
* **Difficulty** (`sys->gm_mode`: 0 normal, 1 easy, 2 very easy, 3 Battle Game; set by the title's
  difficulty menu, setting "Difficulty Select", or the co-op lobby's START): very easy doubles the
  player's HP (320) and takes enemy HP from the second row of the type's table; easy and very easy
  change ammo pickups (`item.c getbulletmax`); easy keeps the enemies' normal HP (type 21 aside).
  Counts, placement and speed do not depend on it.

## Co-op

Details in [COOP.md](COOP.md); what matters for enemies:

* The host runs the AI; the guest's enemies are puppets updated from per-slot snapshots (no type
  or model), so both sides must hold the same enemy in the same slot. A frame carries at most 48
  snapshots (`COOP_SNAP_FRAME`, 10 per 1200-byte datagram); with more active slots each frame sends
  the next 48. Measured with 249 active slots: 5 datagrams and ~5.5 KB per frame (~1.3 Mbit/s at
  30 fps), each slot every ~5 frames; puppets keep animating in between.
* **Added enemies** (F2 spawner, extra enemies) are announced with one reliable `EV_SPAWN`
  {slot; source room file and record; type, model version, variant; position, angle, floor}. The
  guest realizes it with the spawner's code into the same slot (`spawn_glue.c spawn_note_apply`
  -> `spawn_at`, skipping lower free slots if needed); its Init creates the same children, and
  from then on it is an ordinary puppet. Notes arriving during a door, event or menu wait in
  `spawn_pending` (at most 900 frames). The host resends them when the guest (re)enters the room
  (`spawn_replay`); a door clears them on both sides. Randomizer records the host had to keep go
  along as notes marked `SPAWN_NOTE_KEPT`.
* **The host's enemy options** go to the guest as `EV_EOPTS` (session start, before the game
  state, on change) and replace the guest's. Placements and the randomizer depend only on the room
  file and the seed, so both sides make the same records in the same slots. The guest never spawns
  (F2 refuses there).
* **Targets** (host, `coop_proto.c coop_pick_target`): each enemy goes for one player at a time:
  kept through its grab or bite, never a busy one, the one right in front, else the nearer by a
  margin (another floor counts as twice as far); a busy target is left for a free player within
  4 m. A loop guard makes an enemy that attacks the same player three times within 90 frames
  leave it alone for 60 (COOP.md). The partner's stand-in carries what the AI reads of a player (position, angles, modes, hp,
  `stflg`, hit capsule `watr`, floor, busy bits); attacks on it reach the guest as `EV_PLY_HIT` or
  `EV_GRAB`. A zombie grab on the guest makes the slot the guest's until handed back (~10 s
  timeout).
* **Kills while the guest runs the room** (`EV_GONE`): at the take-over the guest names its dead
  enemies; the host hides its matching copies for that visit.

## Head blow-offs

**The game**: `en01.c bhEne01_DmgCheckType00` bursts a walking zombie's head (`bhEne01_CheckExpHead`)
when a weapon whose `En01_WpnDamageTbl` entry has flag `0x10` hits it and one of: the head joint
(11) took damage (`exp0 + 0x78 > 0`, any of these weapons: magnum, shotgun, sniper rifle, grenade
launcher); the shotgun or grenade launcher (weapons 11, 14) aimed up within 10 units on the same
floor; the magnum (weapon 6) on joint 1 or 8..11; weapon 19 on joint 1 or 8..17. Handguns never do. It
sets hp -1 and `exp0 + 0x78 = -1`, spawns effect 356 at the neck (`bhEne01_ExpHeadEffect2`: chunks,
then the neck's blood fountain) and the zombie falls (damage mode 4) and dies as from any kill.
`bhEne01_CutHead` (a scripted kill, the grab's counter) sets the same flag. Model versions 31 and 42
(prisoner 3, base staff 3) are left out by the game: their heads never burst.

**Nothing hid the head**, on any zombie look, original or swapped: every zombie model (all 24 looks)
is one skinned mesh on node 1 of the 18-node zombie skeleton, the head is part of it, and the PS2's
skin cutter `njplus.c npCutSkin` is an empty function. The head stayed on the falling body behind the
blood (checked: each look spawned in the lift hall and given the head hit, `RECVX_HEADSHOT`;
frame-identical between the room's own zombies and other looks).

**On the PC** ("Zombie Decapitation", GAMEPLAY, `head_blowoff`, on by default): a zombie whose
`exp0 + 0x78` is -1 is drawn without its head (`MdlPut.c bhPutModel` -> `variant_glue.c
pc_zombie_head`): while it is drawn, the rotation part of the matrices of head node 11 and the nodes
below it is zeroed, so the skin's vertices the head bone carries meet at the head joint and the
blended neck vertices close into a stump; the matrices are put back after the draw, so the game's
state, hits, collision and effects are untouched. One rule for every look: they share the skeleton.
A linked upper half (`flg & 0x80`) follows its body's flag. While the head part flies off
(`en01b.c`, head part `exp0 + 0x10` in mode 3) it is not hidden. An enemy variant with a zombie
base drawn as another model ("Enemy variants") hides that model's head role (`actor_head_node`,
the rig's head and the nodes below it). Off: as the original (frames identical to the previous
build). Caps the game links to the head (lab coat, base staff, civilian looks) fly off as before.

**Co-op**: the host decides the burst; a zombie's `exp0` is not in the snapshots, but effect 356 goes
to the guest as an `EV_FX` linked to the zombie's slot, and `coop_glue.c apply_fx` sets the
guest's flag from it, so the partner sees the head go too. A guest that enters the room later sees
the body with its head.

**Third person, "Precise Shots"** ([THIRDPERSON.md](THIRDPERSON.md#precise-hits)): a shot tested
against the zombie's body bursts the head only when that hit is on the head (joint 11); earlier head
damage, the magnum's near hit on joints 1 and 8..10 and the shotgun aimed up no longer do.

## Requirements for randomizers and difficulty options

1. **Record count and order** stay (scripts, `wrk_no`, model/motion/texture tables, co-op slots).
2. **Kill bits**: a replaced record keeps its bit; extra enemies get none. New bits < 512.
3. **Leave alone**: the *type* of event-driven records (another look of the same type keeps the
   scene's motions and AI states; the randomizer does that), NPCs (ids >= 31), ids 0, 25, 28, 53, 56.
4. **Bosses and set pieces** (2, 11..17, 19 in its story rooms, 23, 25, 29, 30, 53..55): their
   rooms' scripts wait for their death or events; replacing them can softlock the room.
5. Types that cannot die normally (29, 53, 25, 0) must not replace a record that a `DieEventCk`
   or a door condition counts.
6. **Slots**: children within the slots (128; 255 on the PC). Water types (8, 21, 22) need water, fliers (6, 7) head room.
7. **Variant** in range (zombie 0..15, Tyrant 0..2, others mostly 0..3); waiting variants need the
   room's floor areas.
8. **Grab motions** come with the type's motion set; Wesker (`ply_id` 3) overruns some 3-row
   tables.
9. **Co-op**: decide everything from the room file, the flags and a shared seed, at the same
   point of the load on both sides.

Softlock risks: "all dead" rooms, events that `LoadWork` a record expecting its motion numbers,
items given after a scripted kill, enemies on the entry position or in a door's trigger box.

## Enemy spawner (F2)

F2 -> "Spawn Enemy" (also the EXTRAS page): "Enemy" picks one of 58 entries (the "Character Model"
catalogue entries with ids up to 30, plus Gulp Worm, Alexia 2 and 3, Alexia's baby, tentacle,
both knights, ceiling trap, Nosferatu's core, Alfred, the ants, the baby spider, the exploding
prop), then the custom enemies, marked "[C] " ("Enemy variants": the built-in ones, then those of
`mods/enemies.ini`); "Spawn Ahead" puts it 3 m in front of the player, facing it. Code:
`pc/glue/spawn_glue.c`.

Steps, as the room loader does it for a record:

1. Checks: in a room, no door/event, not the co-op guest; slots for the type and its children
   (`spawn_slots_needed`: zombie 10, Gulp Worm 19, Black Widow 25, moth 16, Alexia 2 12, others
   1..3) plus 6 kept free; arena, spawn pool, texture entries (16 spare) and heap (1 MB spare).
2. `actor_find` (`pc/glue/actor_glue.c`, shared with the appearance loader) reads the source room
   file from `RDX_LNK.AFS` and finds the record's model chunk list, the type's motion list and the
   texture entries of its textured sub-models. The source is the smallest room holding the model
   and motion set (`pc/tools/enemies.py sources`).
3. The model list is copied into the spawn pool and realized with `bhSetEneMdl` on a copy of the
   source record (variant 0 except ants C, tentacle, core); it takes the first free slot and
   shares the textures of a (type, model version) the room already has (`sys->et_lp`).
4. New texture owners get their textures uploaded from the source room (`bhSetMemPvpTexture`);
   a hook in `ps2_texture.c bhReleaseMainTexture` releases them with the room.
5. The motion set: the room's own for the type, else the source's, copied and realized
   (`bhSetEneMtn`). Then `bhKeepObjWork` + `bhCalcModel`; Init runs on the next tick.
6. Sounds and effect textures (below).

A spawned enemy has no record: no kill bit, scripts never see it, gone after a room change.

| Type | Behaviour outside its own rooms |
|---|---|
| zombies, Hunter, Bandersnatch, Cerberus, Black Widow, Giant Black Widow, Tyrant, Nosferatu, Monster Steve, anatomist, Alexia's first form | move, attack, take damage, die (Tyrant: frozen death pose; Cerberus spawned as variant 0) |
| bat, moth, Spotter | fly, take damage and die; rarely attack |
| albinoid adult | walks on land |
| Gulp Worm | burrows; wanders out of the room without its sand arena |
| fish, ants, tentacle, ceiling trap, Nosferatu's core, exploding prop | spawn and run, but belong to water, walls, their boss or an event |
| **refused**: Baby Black Widow (24), Alexia's baby (30) | their code reads the parent that spawns them (`lkwkp`) |
| **refused**: Alexia second/third form (13, 14) | the second form reads its partner pointer (NULL) |
| **refused**: Alfred (16) | his boss logic corrupts memory outside his room |

### Sounds of added types

Enemy code requests a sound id (`RequestEnemySe`, `bhEne_CallSE`; effects through
`bhEne_CallEffectSE` as enemy 127, on the PC 255): bits 0..7 list (program), 8..11 SE bank, 12..15 how many may
play at once, 16..19 priority. Every enemy sound is in **bank 3**, played on one of 6 enemy SE
handles from SE port 3. A room's sound pack `RM_SRRC.SPQ` (key `stage*1000 + room*10 + case` in
`MULTSPQ1.IDX` for stages 0..5, `MULTSPQ2.IDX` for 6..9) loads bank 2 (room sounds) and, in rooms
with enemies, bank 3 with the room's own types' sounds. List numbers are per type and collide
between types, so an added type would play another type's sounds.

On the PC, when `spawn_at` realizes a type the room file has no record of (F2 spawner,
randomizer, co-op guest), `spawn_glue.c sound_load` reads bank 3 of its source room
(`snd_iop.c snd_xbank_load`; else another catalogue room of the type) into an extra synth bank
(one per source room, at most 31) and records type -> bank. Each request is attributed when made
(`sdfunc.c RequestEnemySeBasic` -> `eneset.c pc_enemy_sound_type`): the enemy whose code runs, a
part or child as its creator, an effect as the type that made it (`effect.c bhSetEffectTb`). Such
a sound carries the bank in bits 3..7 of the request's port byte (`ps2_sg_sd.c sdSysServer`; 0 on
the PS2) and `snd_synth.c` plays it from that bank; a type without a bank plays nothing. Everything
else keeps the room's banks, so vanilla rooms send the same driver commands. `bhSetRoom`
(`spawn_room_setup`) stops and releases the extra banks. Variants of one type share the first
one's bank. Player voices in another character's voice use the same 31 banks
([ACTORS.md](ACTORS.md) "Voices").

### Effect textures of added types

Enemy effects (ids 250..299) are global code, but an effect picks its texture by `tex_id`
through `sys->ef_tn[tex_id]`, and some take a room effect texture (ids 30..99, 400+), which
`room.c bhSetRoom` uploads only for the room's own effect records. Without it the effect drew
texture 0 (a page of kanji):

| Type | Effects | Room effect texture | Rooms with it |
|---|---|---|---|
| Alexia, first form (12) | 262/263 her fire, 265 `bhEff_E12_FrameLiquid` (burning blood on the floor) | 80 | RM_5820, 9301, 9321, 5810, 9302, 9370 |
| Gulp Worm (2) | 253 `bhEff_E02_SandDust` | 82 (variant 0), 83 (others) | 82: RM_3091, 7220/7221; 83: RM_5830, 5810, 7230/7231, 9302 |
| Alexia, third form (14; refused) | 269 `bhEff_E14_Fire` | 80 | as above |
| Alexia's baby (30; refused) | 267 `bhEff_E13_Fluid` | 81 | RM_5810, 9301, 9302 |

`spawn_glue.c fx_load` (after `sound_load` in `spawn_at`): for a type in the table whose texture
this room has not loaded (`sys->ef_tn[fx] == 0`), the entry is read from the source room
(`actor_glue.c actor_fx_tex`) and uploaded as `bhSetRoom` does (appended to `sys->ef_tlist`,
counted in `ef_extn`), so `bhReleaseMainTexture` releases it with the room's. Fallback
(`effect.c`): an effect whose room texture is not loaded gets the "not drawn" flag
(`flg 0x1000000`) instead of drawing texture 0, logged once per effect id.

## Enemy variants (custom enemies)

A variant is a spawnable enemy made of parts the game already has: a **base** type (its AI,
hit capsules, collision, motions; the spawner's entry of that type and model version), optionally
**another model** drawn in its place, factors on its stats, motion and sound mappings, and
randomizer metadata. Each has a **stable id** (1..99 built in, 100..999 from the mods file), which
the F2 spawner, the randomizer and co-op spawn notes carry. Code: `pc/src/host/variants.c` (the
table and the file reader, no game state; ctest `variants`), `pc/glue/variant_glue.c` (in the
game).

### Built in: Mutated Steve

Monster Steve himself, killable (id 1, F2 "[C] Mutated Steve"): his own code (`en17.c`), model,
arms, motions, attacks and sounds, spawned as an ordinary enemy, with the Tyrant's death.

* **Outside his chase**: his code is written for the Antarctic chase (RM_9280): no hit takes his
  hit points (250), his Die mode is empty and nothing moves him there, his axe swing (motion 2)
  kills outright and his other heavy blows (motions 15, 16) kill below 120 hit points and take 120
  otherwise, and he drives room light 2 (`bhEne17_SetLight`) and shakes the camera
  (`bhEne17_CameraControl`). For this variant only (`en17.c PC_VARIANT`: `pc_variant_is`, the enemy
  is a variant, which a chase's room record never is):
  * every hit takes his hit points (`bhEne17_DmgChk`); below 0 he goes to `bhEne17_Die`, which sets
    `flg & 2` (dead, as every enemy: a record the randomizer turned into him keeps its kill bit and
    `DieCk` counts it; a spawned one has none), stops his motion and his attacks, and leaves him
    where he fell for the visit;
  * his killing blows are heavy ones (`pc_steve_blow`): 50, as the Tyrant's heaviest, knocking the
    player down as his 120-point blow does, times the variant's damage (1.25: **63**; the player
    has 160 on normal, 320 on easy, so a player at full health always survives a blow); his close
    swipe (motion 11, the player played from his set's motions 20..23) stays 30, x1.25: **38**;
  * no room light and no camera shake.

  The chase runs his code as on the PS2 (checked: RM_9280 with his record running, frame-identical
  to the previous build).
* **Death**: he has no death motion. The Tyrant's (donor 19.0: its fall 18, then lying 2, held at
  its end; `en19.c` TY_BR0_DIE) is played by the Tyrant's skeleton at his place and retargeted onto
  his own model ("Monster Steve", RM_9280 record 0, in a display slot of the appearance system) with
  his claw and axe arm on its hands (sub-models 3 and 4 on nodes 9 and 13, as his code links them):
  he falls forward and lies face down. His own body and arm enemies stay in their slots, not drawn.
* **Numbers**: hp x3.4 (his 250 hit points: 850 of damage; the same on every difficulty, his code
  has no HP table). Measured in the lift hall (3/0) from 4 m with auto-aim: about 42 handgun hits
  (30 took 600), 15 shotgun shells, 5 grenade rounds (10 hits); Tyrant 1 there: 30, 21, 7. He walks,
  charges and swings as in the chase; with no shots he hits a standing player about every 4 s.
* **Hits on the player** are his own (busy flag set, released at the end): checked with Claire, Chris
  played, and "Character Model" Chris's file and Monster Steve: each heavy blow 19 steps knocked
  down, each close swipe 70 steps in his grab, then back in control.
* Randomizer: weight 1 in Wild, never in Mild, heavy (below). Slots: 3 (him and his two arms).

### What is applied where

| Field | Applied | Where |
|---|---|---|
| base | the spawner entry of that type and model version is realized (`spawn_at`), the variant remembered by its slot | `spawn_glue.c` -> `variant_spawned` |
| model, attach | drawn instead of the base (and of its linked parts), posed from the base's skeleton every time it is drawn (also while a zombie base plays a chewing or walking motion with its mouth morph, `mdflg & 2`: before, the base was drawn then); a display slot of the appearance system per model in the room (`ACTOR_VARIANT`, 2 slots), released with the room's textures; a model with no humanoid node map, or one that does not load: the base is drawn | `eneset.c bhDrawEnemy` -> `pc_variant_draw` |
| hp, armour | the damage a hit is about to deal (`ep->dam[]`, which `bhEne_CalcDamage` sums) x armour / hp, before its step; its own HP number, thresholds and HP tables stay the base's (so each difficulty keeps its ratio) | `eneset.c bhControlEnemy` -> `pc_variant_pre` |
| damage | the player's hit points its step took, x damage (a blow the game made fatal stays fatal) | `pc_variant_post` |
| speed | steps per frame (0.5..2: AI, motions and movement together; below 1 a step is skipped, never one with a hit) | `pc_variant_pre` |
| scale | drawn size (node matrices scaled about its position); hits and collision stay the base's | `pc_variant_draw` |
| motions | its own copy of the base's motion set with entries swapped (`a:b`: it plays b where the base asks for a); 4 such sets per room | `variant_spawned` |
| motions `a:T:b`, death | another enemy's motions (the donor T, one per variant): its first sub-model's skeleton and its motion set read from its room file as the spawner reads enemies (`actor_find`, no textures, 2 MB buffer, one donor at a time); while the base plays `a`, and once it is dead (`flg & 2`: the death motions in turn, timed by the steps since, the last one's end held), the donor is posed in that motion at the enemy's place and the base's own catalogue model (or `model`) is drawn retargeted from it, the base's linked parts hidden | `pc_variant_post` (the death's steps, a co-op puppet's too), `pc_variant_draw` |
| sounds, sound_map, hurt_sound | that room's bank 3 as an extra bank ("Sounds of added types"); a request of a mapped base program plays the other program from it; the hurt sound is requested on a hit, on the voice channel (whose bank is kept apart from the other channel's, `sdfunc.c`) | `sdfunc.c RequestEnemySeBasic` -> `pc_variant_sound` |

Its parts and children (linked to it) count as the variant. With no variant in the room
(`pc_variant_n` 0) none of the hooks is called: rooms, saves and frames are as before.

### mods/enemies.ini

Read once at start from the mods folder (`RECVX_MODS`, else `./mods`); the log names each problem
(`enemies.ini line N: ...`) and how many enemies were added. One `[Name]` section per enemy (the
name is shown as "[C] Name" in F2 and logged by the randomizer); `;` and `#` start comments.

```ini
[Brute zombie]
id = 100                 ; required, 100..999, unique: co-op and the randomizer use it
base = Zombie: Soldier   ; required: an F2 entry's name, or type[.model version] (1.2)
model = Nosferatu        ; a "Character Model" model drawn instead (humanoids; none = the base's)
attach = 3:9, 4:13       ; sub-model:node of that model drawn on its nodes (up to 4)
hp = 2                   ; times the base's hit points (0.05..50)
armour = 1               ; damage taken: weapon damage x armour / hp
damage = 1.5             ; damage dealt to the player
speed = 1.5              ; 0.5..2
scale = 1.4              ; drawn size, 0.25..4
motions = 4:2, 6:19:16   ; up to 8 of each: plays motion 2 of its own set where the base asks for 4,
                         ;   and shows motion 16 of enemy 19 (the Tyrant; type[.model version]) for 6
death = 19.0: 18, 2      ; when it dies: the Tyrant's motions 18, then 2, the last one's end held
sounds = 9280            ; room file whose enemy sound bank sound_map and hurt_sound use
sound_map = 0:0, 8:9     ; up to 16 pairs of sound programs: the base's -> that bank's
hurt_sound = 2           ; that bank's program played when a hit lands
wild = 3                 ; randomizer weight in Wild (0, the default: never)
mild = 2                 ; in Mild
slots = 4                ; enemy slots it reserves (default: the base's with its children)
big = 1                  ; needs open space (as the Tyrant)
heavy = 1                ; boss-like: Wild only, one per room, open space, not in kill-count rooms
stages = 0-3, 9          ; stages (areas) the randomizer may place it in; default all
```

Keys are case-insensitive; a key that is not understood is reported and ignored; a section without
an id or a base, or with a taken id, is left out. To find sound programs, `RECVX_TRACE_SND=1` logs
every sound a variant requests with its motion and frame.

### Randomizer and co-op

* The randomizer ("Randomizer" below) adds every variant with a weight as a kind of its own after
  the built-in kinds: its rules are its base kind's (another base type's: not in kill-count rooms,
  not where the script uses `rm_flg` bits 29..31), plus `big`, `heavy` (one per room) and `stages`;
  its slot cost is the larger of `slots` and the base's. It replaces free and script-shown records
  only (event-driven records keep their type), never in boss rooms (the whole room is kept). The
  log says `-> Mutated Steve (variant 1: type 17 model 0)`. In tours of 72 rooms with enemies (6
  seeds, Wild) he took one record in 0..1 rooms per seed (3060, 3170, 7130), none in the 12 boss
  rooms reached. Extra enemies copy a randomized
  record's variant too. Its grab family is its base type's (the hold rule keys on the type).
* **Co-op**: the spawn note's `variant` field (formerly padding; the protocol is unchanged) carries
  the id; the guest realizes the base into the same slot and applies the same variant (model,
  motions, donor, sounds), after which the enemy is an ordinary puppet (its death plays from the
  dead flag the host sends); its hits on the guest come from
  the host's step (damage already scaled). The randomizer draws the same variants on both sides
  **only if both have the same variants**: partners need the same `mods/enemies.ini`. An unknown
  id plays as its base type (logged).
* Test aid: `RECVX_SPAWN="frame:v<id>[@dist]"` spawns a variant.

### Limits

* The model is only drawn: hits, collision, the shadow and the camera follow the base's skeleton
  (a model much larger than its base is hit where the base is).
* One donor per variant; one donor loaded at a time (another variant's donor in the same room: its
  donor motions are not shown). The model only takes the donor's rotations (retargeting, humanoid
  skeletons): a donor motion snaps in without blending, and its hits, collision and shadow stay the
  base's (a dead body's where it stood).
* Two different models per room (a third: its base is drawn); 4 swapped motion sets per room.
* Sound programs are per type: `sound_map` must name programs of the bank it uses; nothing checks
  that they fit.

## Enemy options

Extra enemies, placements and the randomizer are off by default; off, the code returns before touching
anything (the variety and event settings only matter with the randomizer on). In co-op the host's options
count for both. Code: `pc/glue/enemyopt_glue.c`, one decomp hook at the end of
`room.c bhFinishRoom` (`enemy_room_finished`).

| Setting (page) | ini key | Values | Test aid |
|---|---|---|---|
| "Extra Enemies" (GAMEPLAY) | `enemy_extra` | 0 = Off, 1..3 = "Up to N" | `RECVX_ENEMY_EXTRA` |
| "Rearranged Enemies" (GAMEPLAY) | `enemy_place` | 0/1 | `RECVX_ENEMY_PLACE` |
| "Enemy Randomizer" (GAMEPLAY) | `enemy_random` | 0/1 | `RECVX_ENEMY_RANDOM` |
| "Enemy Seed" (GAMEPLAY) | `enemy_seed` | seed for placements and randomizer | `RECVX_ENEMY_SEED` |
| "Randomizer Variety" (GAMEPLAY) | `enemy_variety` | 0 = Mild, **1 = Wild** | `RECVX_ENEMY_VARIETY` |
| "Randomize Event Enemies" (GAMEPLAY) | `enemy_random_events` | 0/**1** | `RECVX_ENEMY_EVENTS` |
| "Randomize Cutscene Cast" and its page (GAMEPLAY, CUTSCENE CAST) | `cast_random`, `cast_models`, `cast_assign`, `cast_imports`, `cast_custom_off` lines | the story characters drawn as other models in cutscenes, visual only (no enemy is touched): [ACTORS.md](ACTORS.md) "Cutscene cast" | `RECVX_CAST` |

Every option first reads the room script once per load (`scan_script`) and marks records that an
event drives (`LoadWork`, motion pauses, links, positioning, zombie/face/look/render ops), that the
script shows or hides, whose HP the script tests or sets, and rooms with an "all dead" count.
Extra enemies and placements never change marked records; the randomizer changes some of them
("Randomizer"). Choices come from a xorshift generator seeded with (seed, room file, record), never
from the game's `rand()`.

### Extra enemies

* "Up to N per room": at the first frame a room runs, the host (or single player) picks up to N
  of the room's unmarked, visible, living records of the roaming types zombie, Black Widow,
  Cerberus, Hunter, Bandersnatch, albinoid adult, anatomist, and spawns a copy of each from the
  current room file (`spawn_add`, so it reaches the guest as an added enemy): same type and model
  version (no upload), variant 0, placed 15..35 units from the source where a line test against
  the walls (`hitchkl.c bhCollisionCheckLine2`) is clear, at least 40 units from every entry
  position and from the player.
* Deterministic per (seed, room) and the room's living records. Extras have no kill bit: they come
  back with the room, and "all dead" counts ignore them.
* Budgets are the spawner's; a room that runs out stops adding.

### Rearranged placements

* At room load each unmarked record of a roaming type (as above plus bats) starts 8..30 units from
  its own spot or (half the time) another such record's spot on the same floor, in a direction
  with a clear line, 40+ units from every entry position, at a random angle. Kill bits, slots and
  types stay; a waiting Cerberus (variant 1/2) becomes variant 0.
* Not moved: story/cutscene/scripted enemies, bosses and set pieces, NPCs.

### Randomizer

At room load (`enemy_room_finished`, end of `bhFinishRoom`) records of zombies, Black Widows,
Cerberus, Hunters and Bandersnatches (and, in Wild, bats and Spotters) become other enemies. Each
record keeps its slot, kill bit, position, angle and floor: `spawn_at` realizes the new type into
the record's own slot during the load.

**Which records change, and how** (from `scan_script`, as `pc/tools/enemies.py room` shows it):

| Record | Becomes | Why |
|---|---|---|
| free (no script use but its kill bit) | another kind | nothing in the script depends on its type |
| shown or hidden by the script only (`InitModelSet`, "later-activated", ambushes that just appear) | another kind, with "Randomize Event Enemies" | the script only switches it on and off; it starts in its AI |
| event-driven (`LoadWork`, motion pauses, links, positioning, zombie/face/render/look ops, `EneSeOn`) or HP read/set by the script | **another look of its own type**, with "Randomize Event Enemies" | the event plays the type's motion numbers and room motions on its skeleton, sets its AI modes (`SubCtr` 143/146/147), tests `id == 1` and its HP. All zombie looks share an 18-node first sub-model, Hunter and Sweeper a 28-node one, so the scene still fits; types with one look (Cerberus, Bandersnatch, Black Widow, bat, Spotter, moth, albinoids, anatomist) stay |
| bosses and everything in their rooms (ids 2, 12..17, 19, 23, 24, 29, 30, 53..55: Gulp Worm, Alexia, Nosferatu, Alfred, Monster Steve, Tyrant fights, Giant Black Widow, tentacles, knights) | kept: the whole room | the scripts wait for the boss's death or drive it |
| NPCs (ids >= 31), props (0), fish, albinoids, ants, moths, ceiling trap, scripted object | kept | water/wall/set-piece types, or no AI of their own |
| a record without a floor (`flr_no < 0`) | kept | hangs somewhere for an event |

**Kinds** a free or shown record can become (`enemyopt_glue.c kinds[]`); a kind is drawn first by
weight, then one of its looks (the spawner's entries of that type, from any stage's room file):

| Kind | Looks | Weight Mild / Wild | Rules |
|---|---|---|---|
| zombie | 24 | 5 / 5 | |
| Cerberus | 1 | 4 / 4 | |
| Bandersnatch | 1 (Bandersnatch 2 is an event's arm: never) | 4 / 4 | |
| Hunter, Sweeper | 2 | 3 / 4 | not in kill-count rooms (may leap out of a room) |
| Black Widow | 1 | 2 / 3 | open space (25 slots with its brood); not in kill-count rooms (climbs walls) |
| anatomist zombie | 1 | 2 / 2 | |
| Tyrant | 2 | - / 2 | open space; not in kill-count rooms; not where the script uses `rm_flg` bits 29..31 (its code clears `rm_flg & 7` every frame) |
| Giant Black Widow | 1 | - / 1 | as the Tyrant (writes `rm_flg & 1`) |
| bat, Spotter | 1 each | - / 3, 2 | only for bat/Spotter records (fliers hover where they are placed; ground enemies never become fliers and back); not in kill-count rooms; `rm_flg` as above |

* Then the enemy variants with a randomizer weight ("Enemy variants"): Mutated Steve, 1 in Wild.
* Not in the pool: the adult albinoid (its code drives its own room's light 2 and an effect linked
  to it: two of them in a prison room crashed in `en22.c bhEne22_MV04`), moths (their eggs use
  Claire's sub-models and 16 slots), bosses, water and wall types.
* **Open space** (`open_dirs`): line tests against the walls (`bhCollisionCheckLine2`, as the
  placements use) from the record in 8 directions at waist height; at least 5 must be clear for
  35 units (3.5 m). Corridors and small rooms fail it. Ceiling height is not tested (the room
  collision has no ceilings to test against): fliers only replace fliers, at their own heights.
* **Kill-count rooms** (`DieEventCk`: doors that open when everything is dead, e.g. the prison's
  5550..5710, 4012) are randomized with the kinds that stay on the floor and die there: zombies,
  Cerberus, Bandersnatches, anatomists. Each dies with `flg & 2` (each has a `DieCk` in its own
  rooms, and `RECVX_ENEMY_KILL` kills them all in these rooms), so the record's kill bit is set as before.
* **Variety**: *Wild* (default) draws each record's kind from all kinds that fit, a kind not yet
  drawn in this room first, then a look not yet used in this room. *Mild* draws two kinds per
  room (no Tyrant, Giant Black Widow or fliers) and alternates between them; if neither fits a
  record, any Mild kind that does.
* **Variants**: a record that keeps its type (another look) keeps its variant (an event's pose, a
  lying zombie); another kind gets variant 0 (free-roaming).
* **Slots**: all records are counted with what their Inits spawn; a change may only add up to 122
  (6 stay free), so a room never exceeds the larger of that and its own count.
* **Determinism**: each record's draws come from xorshift seeded with (seed, room file, record),
  plus what earlier records of the room drew; nothing depends on the player, the game's `rand()`
  or what was loaded before. Both co-op sides compute the same.
* **Fallback**: any refusal (data not found, memory, textures) puts the original record back
  exactly as it was, logged as `record N stays`; the host sends it to the guest as a
  `SPAWN_NOTE_KEPT` note.
* Log: one line per change (`room R record N: type T model V -> name (type T' model V')`, event
  records marked `event enemy: same type`) and per room `now k:type.model ...`.
* Offline alternative: `pc/tools/rdxbuild.py` builds rooms with several `enemy_assets` donors
  ([modding/BUILDING_ROOMS.md](modding/BUILDING_ROOMS.md)).

#### Measured variety

Before this version the pool was the spawner's entries of a few types drawn uniformly (24 of its
27..30 entries are zombie looks), Black Widows and Hunters only from stage 2, at most two entries
per room, only free records (event and script-shown records are 64% of them), and rooms with a
prop, a Spotter, the ceiling trap or a kill count were skipped. No texture refusals were seen
either way. Headless tours (new game, teleport through 87 rooms of all stages with enemies, 60
game frames each), the roaming-type records (zombie, Black Widow, Cerberus, Hunter, Bandersnatch,
bat, Spotter) as the log reports them:

| | Before (10 seeds) | Wild (10 seeds) | Mild (6 seeds) |
|---|---|---|---|
| zombies among all these records | 78% | 52% | 55% |
| zombies among free and script-shown records | 72% | 21% | 26% |
| free records changed | 38% | 91% | 85% |
| script-shown / event-driven records changed | 0% / 0% | 99% / 88% | 100% / 88% |
| kinds per room (rooms with 2+ such records) | 1.10 | 2.29 | 1.57 |
| (kind, look) pairs per room | 1.95 | 3.54 | 3.08 |
| kinds a room showed over all seeds | 1.44 | 3.21 | 2.78 |

Wild's free and script-shown records: Cerberus 22%, zombies 21%, Bandersnatches 19%, Hunters 13%,
anatomists 13%, bats 5%, Spotters 4%, Black Widows 1.4%, Tyrants 0.9%, Giant Black Widows 0.6%
(the big kinds pass the open-space test at few records). Rooms that still show one kind have only
event-driven records (other looks of one type) or single-look event types.

#### Event enemies per room

What "Randomize Event Enemies" does with each room's event-driven and script-shown records of the
roaming types (generated from the script walk; records not listed are free, or not enemies of these
types). *Another kind*: the script only shows or hides them. *Another look*: an event drives them
(the scene keeps its type). *Kept*: event-driven with no other look of their type, or a boss arena.

| Room | Records (index) and what they become |
|---|---|
| 0020 | zombie 0,1,2,3,5: another look (event-driven) |
| 0030 | zombie 3: another look (event-driven) |
| 0031 | zombie 1,2,3,4,5: another look (event-driven) |
| 0050 | zombie 2: another look (event-driven); Cerberus 0,1: **kept** (event-driven, one look) |
| 0060 | zombie 3: another look (event-driven) |
| 0070 | zombie 0,1,2,3: another look (event-driven) |
| 0080 | zombie 0,1,2,3,4: another look (event-driven) |
| 0090 | zombie 0,1,2,3,4: another look (event-driven) |
| 0110 | zombie 1: another look (event-driven); anatomist 4: **kept** (event-driven, one look) |
| 0140 | zombie 0,1,2,3: another look (event-driven) |
| 0150 | zombie 0: another look (event-driven) |
| 0160 | zombie 0,1,2,3,4: another look (event-driven) |
| 1021 | zombie 0,1: another kind (shown or hidden only); zombie 2,3: another look (event-driven) |
| 1030 | bat 3: **kept** (event-driven, one look) |
| 1070 | zombie 0,1,2,3: another look (event-driven) |
| 1090 | Bandersnatch 1: **kept** (event-driven, one look) |
| 1100 | zombie 0,1,2,3,4: another look (event-driven) |
| 1121 | Bandersnatch 0,1: **kept** (event-driven, one look) |
| 2000 | Bandersnatch 0,1: **kept** (event-driven, one look) |
| 2031 | zombie 0,3: another look (event-driven) |
| 3020 | Cerberus 2,3: another kind (shown or hidden only) |
| 3030 | zombie 0,1: another look (event-driven) |
| 3050 | zombie 0,1,2,3,4: another look (event-driven) |
| 3070 | zombie 1: another look (event-driven) |
| 3100 | Bandersnatch 0,1: **kept** (event-driven, one look) |
| 3120 | zombie 1,2: another look (event-driven); albinoid infant 0: **kept** (event-driven, one look) |
| 3130 | Bandersnatch 0: **kept** (event-driven, one look) |
| 3140 | zombie 7: another look (event-driven); albinoid infant 0,1,2,3,4,5,6: **kept** (event-driven, one look) |
| 3160 | zombie 0,1,2,4: another kind (shown or hidden only) |
| 3170 | zombie 0,1,2,3: another kind (shown or hidden only) |
| 3180 | Bandersnatch 2,3: another kind (shown or hidden only) |
| 3200 | zombie 0,1,2: another kind (shown or hidden only) |
| 4000 | zombie 3,4,5: another look (event-driven) |
| 4001 | zombie 0,1,2: another look (event-driven) |
| 4010 | bat 0,1,2,3,4: **kept** (event-driven, one look) |
| 4012 | Bandersnatch 0,1: **kept** (event-driven, one look) |
| 4020 | zombie 1,2,3,4,5: another look (event-driven) |
| 5520 | zombie 1,2: another look (event-driven) |
| 5610 | zombie 0,1,2,4,5: another look (event-driven) |
| 5650 | zombie 1,2,4: another look (event-driven) |
| 5660 | zombie 0,1,2,3: another look (event-driven) |
| 5670 | Hunter 1: another kind (shown or hidden only) |
| 6010 | zombie 0,1,2,3: another look (event-driven) |
| 6020 | zombie 1: another look (event-driven) |
| 6030 | zombie 2: another look (event-driven); moth 3,4,5: **kept** (event-driven, one look) |
| 6050 | zombie 0,1,2,3,4: another look (event-driven) |
| 6070 | Cerberus 1: another kind (shown or hidden only); Cerberus 0,2: **kept** (event-driven, one look) |
| 6080 | zombie 1,2,3,4,5: another look (event-driven) |
| 6090 | Black Widow 0,1,2: another kind (shown or hidden only) |
| 7031 | zombie 0,1: another kind (shown or hidden only) |
| 7050 | Hunter 0: another look (event-driven) |
| 7060 | zombie 1: another look (event-driven) |
| 7070 | Bandersnatch 6: **kept** (event-driven, one look) |
| 7071 | zombie 4,5,6: another kind (shown or hidden only) |
| 7080 | Black Widow 0,1: **kept** (event-driven, one look) |
| 7081 | Hunter 2,3: another look (event-driven); Spotter 0,1: **kept** (event-driven, one look) |
| 7100 | albinoid adult 0: **kept** (event-driven, one look) |
| 7110 | Hunter 1,3: another look (event-driven); Spotter 0: **kept** (event-driven, one look) |
| 7130 | Hunter 0,1,2,3: another kind (shown or hidden only) |
| 7140 | zombie 0: another look (event-driven) |
| 7181 | Hunter 0: another look (event-driven); Spotter 1: **kept** (event-driven, one look) |
| 7190 | zombie 0,1: another kind (shown or hidden only) |
| 7191 | Hunter 1: another look (event-driven); Spotter 0: **kept** (event-driven, one look) |
| 7200 | Hunter 0: another kind (shown or hidden only) |
| 7250 | Black Widow 0,1: another kind (shown or hidden only) |
| 8001 | zombie 0,1,2: another look (event-driven) |
| 8010 | Hunter 0,1: another kind (shown or hidden only) |
| 8020 | Hunter 0: another look (event-driven) |
| 8030 | Hunter 2: another look (event-driven); Spotter 1: **kept** (event-driven, one look) |
| 9010 | zombie 0,1,2,3: another look (event-driven) |
| 9020 | zombie 0,1,2: another kind (shown or hidden only) |
| 9030 | zombie 1,2,3,4,5: another look (event-driven) |
| 9060 | zombie 0,1,2,3,4: another look (event-driven) |
| 9080 | zombie 1,2,3,4,5: another look (event-driven) |
| 9100 | Hunter 2: another look (event-driven); Spotter 0,1: **kept** (event-driven, one look) |
| 9110 | Hunter 0: another look (event-driven) |
| 9120 | zombie 0,1,2,3,4,5,6: another kind (shown or hidden only) |
| 9130 | Hunter 0: another kind (shown or hidden only) |
| 9190 | zombie 1,4: another kind (shown or hidden only); zombie 0,2,3: another look (event-driven) |
| 9200 | zombie 0,1,2,3,4: another look (event-driven) |
| 9220 | Hunter 0: another look (event-driven) |
| 9290 | zombie 3: another kind (shown or hidden only); zombie 2,4,5: another look (event-driven) |
| 9340 | zombie 1,2,3: another look (event-driven) |

### Limits

* Extra enemies come only from the host: in a room only the guest is in, there are none until the
  host comes in.
* Two looks of one type share the first one's sound bank.
* A refusal by `spawn_at` keeps the original record; both co-op sides decide the same unless one
  side's memory is fuller.

## Tools

`pc/tools/enemies.py` (Python 3, stdlib, imports `rdx.py`; reads the disc image at run time):

| Command | Output |
|---|---|
| `room ROOM` | the room's enemy records, floor areas and route boxes, and every script reference to each record with the conditions of its enclosing blocks |
| `census [--rooms]` | counts per type and room, kill flags, hidden/shown/event-driven records, helper conflicts, case variants, `gm_mode`/`ply_id` tests, floor areas, routes |
| `sources` | per (type, model version) the smallest room carrying its model and motions |
| `selftest` | the script walker on RM_0060 |

Test aids:

| Env var | Effect |
|---|---|
| `RECVX_SPAWN="gameframe:id[.mdlver][@dist],..."` | spawns the first spawner entry of that type/model version at that game frame (`v<id>`: an enemy variant); traces its state every 30 frames, HP changes and hits on the player |
| `RECVX_SPAWN_SURVEY="stg:rom,..."` | tries every spawner entry in each room and logs the result; `RECVX_SPAWN_SURVEY_ALL=1` keeps the spawns |
| `RECVX_ENEMY_EXTRA`, `_PLACE`, `_RANDOM`, `_SEED`, `_VARIETY`, `_EVENTS` | override the enemy options |
| `RECVX_ENEMY_KILL=n` | from n game frames into each room, every 20 frames each living, shown enemy of the room's records (ids 1..30, in its AI) takes a heavy hit as from the player's weapon; logs each death (`flg & 2`, what `DieCk` turns into the kill bit) and per room how many died (Black Widows take damage through their body parts and do not die from it) |
| `RECVX_HEADSHOT=n` | n game frames into each room, every living zombie in its AI (records and added ones) takes one head hit as from the magnum (weapon 6 on joint 11, the game's burst path); its head state (`exp0 + 0x78`), hp and modes are logged every 15 frames for 90 frames |
| `RECVX_ENEMY_TRACE=dist` | traces enemy steps (co-op targets) |
| `RECVX_TRACE_SND=1` | logs sound requests and extra banks |
