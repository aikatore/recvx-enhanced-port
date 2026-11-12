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
  (`spawn_replay`); a door clears them on both sides. The randomizer result of each room
  load goes along as notes too (protocol 52; the guest's too since 53): `SPAWN_NOTE_DONE` (the room file, how many
  follow), then a `SPAWN_NOTE_RAND` per record it changed; a load of that room while the partner plays in it makes
  exactly those changes and keeps every other record (`follow_host`), whatever its own story flags would classify.
* **A guest there first** (protocol 53): a guest that loads a room with the host elsewhere draws the room's randomizer
  result and its extra enemies itself, as the host would (the same seed, room file and story state), and sends both
  (its extras as `EV_SPAWN` notes with their slots, then `SPAWN_NOTE_XDONE` with the room file and their count;
  `SPAWN_XDONE_NONE` when its extras are the host's). The host that comes in while the guest is still there makes the
  guest's result and the guest's extras in the same slots (`gx_follow`), but an extra whose dead flag is set by then;
  then the guest hands its enemies in with their state (COOP.md "The hand-in"). Before, the guest had no extras until
  the host came, and then they popped in at their spots while every other enemy went back to the room's start. An extra
  the guest killed meanwhile the host makes too (protocol 54), dead and hidden until the guest hands in its corpse, which
  then lies on the host where it lies on the guest (before: not made, nothing there on the host).
* **The host's enemy options** go to the guest as `EV_EOPTS` (session start, before the game
  state, on change) and replace the guest's: with protocol 39 also Types per Room and Custom's
  32 rates, which fill the event's 80 bytes (`EnemyOpts`; the lobby shows the guest the host's
  rates read-only). Placements and the randomizer depend only on the room
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
  enemies; the host hides its matching copies for that visit. The others go to the host with their state, living and
  dead (protocol 53 the Shared Enemy Control types, 54 every type but the bosses' and the moth's larva: COOP.md "The
  hand-in", its table says in which states each type goes).

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

**Third person, "Headshots & Piercing"** ([THIRDPERSON.md](THIRDPERSON.md#headshots-and-piercing)): a
precise head hit that kills a zombie also bursts its head with the magnum, Gold Lugers, shotgun, sniper rifle
and assault rifle, while it staggers or falls too (`bhEne01_DmgCheck` -> `pc_burst_head`, the same burst).

**Third person, "Locational Hit Reactions"** ([THIRDPERSON.md](THIRDPERSON.md#locational-hit-reactions)): a
zombie hit on its head, an arm or a leg also flinches that part while it is drawn; its state is untouched.

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
* **Numbers**: hp x2 (his 250 hit points: 500 of damage, as a randomized Tyrant on Normal and Easy
  (`En19HpTbl`, type 0); the same on every difficulty, his code has no HP table; until 2026-10-31 x3.4). Measured in the lift hall (3/0) from 4 m with auto-aim: about 42 handgun hits
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
| "Extra Enemies" (GAMEPLAY) | `enemy_extra` | 0 = Off, 1..8 = 0..N per room (random) | `RECVX_ENEMY_EXTRA` |
| "Rearranged Enemies" (GAMEPLAY) | `enemy_place` | 0/1 | `RECVX_ENEMY_PLACE` |
| "Enemy Randomizer" (GAMEPLAY) | `enemy_random` | 0/1 | `RECVX_ENEMY_RANDOM` |
| "Enemy Seed" (GAMEPLAY) | `enemy_seed` | seed for placements and randomizer | `RECVX_ENEMY_SEED` |
| "Enemy Selection" (GAMEPLAY) | `enemy_variety` | 0 = Mild, **1 = Wild**, 2 = Custom | `RECVX_ENEMY_VARIETY` |
| Custom's rates (ENHANCED SETTINGS GAMEPLAY's Enemy Rates, the BIORAND screen and lobby RANDOMIZER pages, F1) | `enemy_rates` line: `id:rate` per kind by type id (`5.0` Hunter, `5.1` Sweeper: type and model; a plain `5:rate`, as saved before the two were split, sets the Hunter to that rate and the Sweeper to half of it, rounded down, as the presets halve it), `vID:rate` per variant | 0 (Off)..10 | `RECVX_ENEMY_RATES` (same text) |
| "Types per Room" (GAMEPLAY) | `enemy_types` | **0 = No Limit**, 1..8 | `RECVX_ENEMY_TYPES` |
| "Randomize Event Enemies" (GAMEPLAY) | `enemy_random_events` | 0/**1** | `RECVX_ENEMY_EVENTS` |
| "Randomize Cutscene Cast" and its page (GAMEPLAY, CUTSCENE CAST) | `cast_random`, `cast_models`, `cast_assign`, `cast_imports`, `cast_custom_off` lines, `cast_custom_set` | the story characters drawn as other models in cutscenes, visual only (no enemy is touched): [ACTORS.md](ACTORS.md) "Cutscene cast" | `RECVX_CAST` |

Every room load first reads the room script (`scan_script`) and marks records that an event drives (motion pauses,
links, positioning, zombie/face/look/render ops, and a `LoadWork` of the record followed by an op only its own type takes),
that the script shows or hides or loads only for ops any type takes alike, whose HP the script tests or sets, and rooms
with an "all dead" count ("Which records change" below). It reads only the code that can run on this visit: a block whose
test wants an `ev_flg` or `ky_flg` bit clear that is set now and that no script of the room clears is skipped, and so is
an event only such code starts. Placements never change marked records; the randomizer changes some of them
("Randomizer"); extra enemies keep clear of them. Choices come from a xorshift generator seeded with (seed, room file,
record or extra), never from the game's `rand()`.

### Extra enemies

"0..N per room": at the first frame a room runs, the host (or single player) adds a random 0..N enemies to it, whatever
its records (`pc/glue/enemyopt_glue.c enemy_extras_frame`, spawned as the F2 spawner's: `spawn_add`, so they reach a
co-op guest as added enemies).

* **Which rooms** (`xrooms`, `extra_room`): the retail room files with enemy records (ids 1..30, event records too: 94
  files), but none of the boss arenas (`boss_arena`: their scripts wait for the boss), none of the Battle Game's
  (5510..5710: its own kill counts), not the cutscene rooms 8030 (Wesker's monitor) and 9340 (Wesker and Claire), not
  9170 (the tiger statue elevator's ant swarm, 76 records) and no room with an item box (a save room: a type 4 trigger
  with attr 0x2; of these files only 9220). Rooms without enemies get none: an enemy where the game has none (a corridor, a save
  room) would change what a room is for. Rooms whose doors count the kills (`DieEventCk`) get extras, but their
  "all dead" counts ignore them, so they never hold a door shut. A mod's or BioRand's room that has enemies where the
  disc has none gets none either (each extra needs a dead flag, below, and those are per listed room).
* **How many** (`extra_count`): a number of the room's own, 0..N, every value equally likely (a draw of the seed and the
  room file: the same on every visit; the host draws it, or a co-op guest in the room before the host), so N / 2 on average and none in one room of N + 1. The
  draw is one fraction per room scaled by N: raising N never gives a room fewer. Before (protocol 50) it was N whenever
  the room had the spots, which filled every room to the setting. Fewer only where the room has no more free spots
  (below) or the spawner refuses one (its budgets: enemy slots with 6 left free of 255, room memory, the 32 MB spawn
  pool, texture entries), and the room stops adding at the first refusal. Small rooms have few free spots once the doors
  are kept clear: "Measured" below.
* **Where** (`extra_grid`, `extra_spot`): once per room load the floor the player can walk to is filled in as a grid of
  4-unit cells (larger in rooms over 16384 cells), from the entry positions (the cells within 8 units of each with no wall
  in between: an entry by a door touches its frame), one cell at a time where the player and an enemy can walk:
  * no wall between the cells' centres (the wall line of "Spots" at waist height, with every wall type the player's own
    check takes: the odd types, the lips along ledges, line walls);
  * the player's body fits in the cell (`player_fits`: `hitchk.c bhCheckWallEx`, the player's wall check, with
    `PlyInfo`'s radius 3.5 and the taller characters' height 17.5, as a walking player: the walls only enemies pass,
    attr 0x4, count too);
  * a camera zone of the cell's floor shows it (`in_view`: `cut.c bhCheckCutArea`'s test over every zone of the room
    file, also those its script turns on later, so the same on every visit): where no camera shows the floor, the player
    never stands;
  * ground within a unit of the last cell's on the same floor and no slope (stairs, ramps), or no ground where the last
    had none (the lowest floor of a room without floor slabs).
  Before (protocol 50) the records that stand on the ground were anchors too, the body and the camera zones did not
  count and the lips were skipped: the fill leaked through gaps a body does not pass, over lips, past open edges and out
  of the camera zones (RM_0030's street: 5500 cells of the 5600 outside it, where 8 extras stood unseen), and records
  put in the scenery for an event seeded areas of their own (`pc/tools/enemy_bounds_check.sh`, "Measured"). A cell is a
  spot when the 4 cells beside it were reached at its height (`xg_inner`: a margin from the edges of the walkable floor:
  walls, the camera zones' edges, drops), it is not in water (`bhCheckWater`), there is no drop nor rising ground within
  6 units (`no_drop`: no pool edge, ledge, crate or step), no wall of any type within 6 units all round, at the waist and
  a half unit above the feet (not in an object, nor against a wall or on a line wall: a zombie put on RM_8010's catwalk
  rail line was pushed into the crate beside it, one on RM_7050's floor lines a unit high onto a table), and it is
  * at least **30 units (3 m) from every door** (the door's trigger box: type 0, or a trigger the script makes a door with
    `EtcAtariSet2`, or one where the script calls `SetDoorCall` while the player is at it, as RM_1121's) **and from every
    entry position**, so nobody coming in meets an extra at the door;
  * at least 15 units from what the player uses: item pickups and boxes (type 4 triggers), event triggers (type 3 with an
    event: typewriters, puzzles, the doors an event opens), the item spots of the script's `ItmSetCk`, the stairs'
    triggers (type 1), and the room's items. The examine messages (type 3 without an event: a shelf, a painting) and
    the climbing areas (type 2) do not count;
  * at least 15 units from every record (dead ones too) and from the extras before it.
  Each extra draws among the spots (32 draws), skipping those within 15 units of an earlier extra.
* **Which kind** (`plan_extras`): with the randomizer on, an extra is drawn as a free record of a walker would be at its
  spot (`draw_kind`), after the room's records and the extras before it: Custom by rate, Wild by weight with the kinds
  not yet drawn in the room first, Mild among the room's two Mild kinds (drawn as for its records, also in a room whose
  records all keep their kind), Types per Room in Wild and Custom a target (the kinds the room lacks while it has fewer)
  and in Mild a limit; the kinds' rules hold (open space for the big kinds, and for them level ground 12 units round: a
  Giant Black Widow on RM_1000's raised walk stood over its edge; no fliers, `rm_flg`, one heavy variant, the
  slot budget), the extras are not counted by "all dead" doors, so the kinds that may leave a room fit there too. So the
  extras count into the rates and into Types per Room as records do; once the room has that many kinds, an extra that
  none of them fits at its spot is left out (a record must become something and may go over the limit, an extra need
  not: a room of bats gets no walkers past it). A spot where nothing fits (every kind that would fit
  at Off) is replaced by another (up to 4). A type the room did not load comes as the F2 spawner's do (its model,
  textures and sounds from its own room file). With the randomizer off: a copy of a random record of the roaming types
  zombie, Black Widow, Cerberus, Hunter, Bandersnatch (not Bandersnatch 2, an event's arm), albinoid adult, anatomist (its own type and model: no upload); a
  room with none of those (bats only) gets none.
* **The same on every visit** (per room and story state): the count, and extra i's spot, angle and kind come from draws
  of their own (seed, room file, i) and from the extras before it, never from which are dead or where the player is. The
  count is the same on every visit; the spots follow the walkable floor, which a room's script can change with the story
  (walls and floors turned on or off at the load on story flags: RM_4010 has 63 spots with `ev_flg` 35 clear and 3 with it
  set, RM_8010 13 and 44 with `ev_flg` 381), so after such a change extra i can stand elsewhere or have no spot; the count only cuts the list
  short, so extra i is the same (spot, kind, dead flag, slot) whatever the count or the setting. A spot within 30 units of the player (a loaded
  save, a retry) skips that extra for the visit. Each keeps its slot: the room's first free slot when the extras come,
  plus i (a dead one's stays free), so a co-op guest still in the room finds them where they were.
* **Dead flags**: extra i of the room at index x of `xrooms` has its own flag, bit x * 8 + i of the extras' dead flags
  (`enemy_xdead_word`: 41 save-region words no game code uses, `sys->itm[302..319]` and `[360..382]`, 1312 bits; 94 rooms
  use 752). It is set when the extra dies, by whichever game runs it (single player, the host, or a co-op guest that has
  the host's extras while the host is elsewhere), and the extra stays away while it is set. Being in the save region, the
  flags go into saves (the card's and the native ones), come back with a load, and go back with a game over's retry to
  what they were at the retry point (as the records' kill bits do); a new game clears them (`AllItemInit`). Co-op:
  `EV_SPAWN` carries the flag (`SpawnNote.dead` = 1024 + the bit), `EV_FLAGS` sends their changes both ways (words
  120..160 after the 120 flag words), the guest's start and rejoin take the host's (EV_BLOB, kept over the guest's own
  items in `own_restore`, and the host's in `own_put` when it sends a guest's saved items). `xrooms` is append only. Before
  (0.7.x) an extra copied a free record and took `ed_flg` 512 + that record's kill bit, so a room had at most as many
  extras as free records (none in the rooms whose records are all event enemies); those bits mean nothing now, and an
  old save's dead extras come back once.
* Nothing else grows: the room's list of extras is 8 entries, the co-op list of added enemies 64, the guest's pending
  ones 64; the grid is 16384 cells (per room load, about 100 KB of static memory).

#### Measured

2026-11-12, `pc/tools/enemy_bounds_check.sh` (the 94 room files; `RECVX_ENEMY_OOB`: a separate flood fill of 2-unit
cells by the player's wall check from where the player stands and the entries, stairs and ramps included, and the
game's own camera zone test): every spot of every room's grid and the spots the extras of 100 seeds take (with 8
asked). Before (protocol 50) 15595 of the 19650 grid spots and 10987 of 39529 extras' spots (28%, in 36 rooms) were out
of bounds: 15459 cells the player cannot walk to, 15176 outside every camera zone, 50 where the player's body touches a
wall (in furniture), 464 in water; RM_0030 (5503 of 5606 cells, past the street's walls), 0031, 1090, 4012, 7100 (in
the water past the dock) leaked into large areas, 3060, 3120, 7050, 7180, 9020, 9080, 9190 and 0110 had only spots out
of bounds (behind railings, under catwalks, in the scenery records stand in for events), 9310 189 of 192. Now none of
2824 grid spots and 26958 extras' spots. The cost: 24 rooms have no spot (before 9): besides those 9, the 8 whose spots were
all out of bounds and the small rooms 0070, 0130, 3130, 4000, 7181, 8000 and 9310, whose few spots stood by the floor's
edges (the margin; without it 17 rooms had none). The counts with 8 over the 100 seeds and 93 rooms: 0:1037 1:1008 2:1010 3:1026 4:1035 5:1053 6:1057 7:1027
8:1047, 4.02 on average.

`pc/tools/enemy_door_check.sh` (50 surveyed seeds per room, and two seeds of real extras): 26972 surveyed spots and 355
extras that came, none within 30 units of a door or entry, as the game measures it and as the room files' doors and
entries give it. Of 8 asked (spots only, no count), 2.90 found a spot on average: 7 rooms take all 8 on every seed (0050,
1121, 3020, 3090, 7100, 7130, 9060), 31 at most 3 and 24 none (0070, 0110, 0130, 0160, 1030, 3060, 3120, 3130, 3140,
3160, 4000, 6010, 6020, 7050, 7180, 7181, 7200, 8000, 9010, 9020, 9080, 9120, 9190, 9310: small rooms whose floor is all
within 30 units of a door, full of furniture or out of the player's reach). `enemy_floor_check.sh` (4 seeds in Wild,
Mild and Custom, 1128 room visits): 4.21 extras drawn of 8 per room, 1.89 spawned, no spawn refused (the budgets never
ran out), the rest without a free spot; none in mid-air or sunk. (2026-11-11, before the count and the bounds: 4.18 of 8
spawned per room; Custom with every kind at 5: zombie, Cerberus, Bandersnatch, Hunter, Sweeper and anatomist each
14..16% of the extras, Black Widow, Tyrant, Giant Black Widow and Mutated Steve 2..3% each: open space, level ground,
`rm_flg`, one heavy variant.)

The radius: the cells of a room that pass every other rule, by their distance to the nearest door or entry at 25 / 30 / 40
units (measured before the clearance at the feet): 0060 (the barracks) 12 / 9 / 1, 3180 (the turntable control room)
38 / 20 / 4, 3030 (the indigo card reader corridor) 138 / 98 / 35. 40 units, the placements' distance from the entries,
left most small rooms without any; 30 keeps 3 m between an extra and wherever anyone comes in.

### Rearranged placements

* At room load each unmarked record of a roaming type (as above plus bats) starts 8..30 units from
  its own spot or (half the time) another such record's spot on the same floor, in a direction
  with a clear line, where it can stand ("Spots"), 40+ units from every entry position, at a random
  angle. Kill bits, slots and types stay; a waiting Cerberus (variant 1/2) becomes variant 0.
* Not moved: story/cutscene/scripted enemies, bosses and set pieces, NPCs.

### Spots

Rearranged placements take their spots from `find_spot`: up to 16 tries around the source, each with its own draws.
Extra enemies take theirs from a grid of the room's walkable floor under the same ground rules ("Extra enemies").

* **A walker stands on the ground**: the ground under the spot (`hitchk.c bhGetGroundPosition` from
  10 units above the feet: the highest box top, slope or floor slab, the type 7 walls) must be within a
  unit of the source's height, on the same floor number and not a slope (type 6), and the enemy is put at
  that height. The lowest floor of a room without floor slabs reports no ground at all: there a spot
  without ground is fine when the source has none either.
* **No drop near it** (`no_drop`): 6 units each way along x and z, no ground more than a unit lower (nor
  none where the source had some).
* Skipped so: spots over a pool or a pit, past or at a ledge, over stairs or in a crate. The
  wall line runs at waist height and skips the odd wall types, so the knee-high lips along ledges never
  stopped it: in the training facility's pool room (RM_3060) zombies from the deck stood over the pool,
  9 units above its floor, and those from the balcony over the stairs below it. On a slope an enemy's
  wall check pushes it along the slope at its own height (a Cerberus put at the foot of those stairs
  slid in under them); a Hunter put by the edge of RM_2011's top balcony leapt off it and stayed at the
  balcony's height, and a Cerberus by the edge of RM_9310's platform was pushed off it.
* A skipped spot uses the draws it used before, so a placement changes only where it was invalid, or
  where it started from another record's spot that changed.
* Bats keep their height (fliers hover where they are placed); extras are never fliers.
* `no_drop` also tests for ground more than a unit above the spot within 6 units (a crate, a step) for the extra enemies:
  a Cerberus put by a crate in RM_2031 stood inside it the next frame.
* The game's own records off the ground on purpose are event enemies (a zombie above a slope in
  RM_0140, Bandersnatches on RM_3100's catwalk, the Hunter of RM_9110, two zombies 3 units
  below RM_9060's floor), never moved or copied, and RM_6090's free Black Widow (variant 6) under the floor.
  The randomizer keeps each record's spot and gives such a record only another look of its type (the
  same test at its own spot), and ground records never become fliers.

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
| loaded by the script (`LoadWork`) only for ops any type takes alike: CommonCtr 7 (position), 11 (angle), 12/13 (sign flags), 23/41 (collision flags), 46 1 (hittable), 47 (`bhCalcModel`); SubCtr 128 (AI start), 139 (task reset), 143/147 with modes 0 or 1 and type 0 (an Init again) | another kind, with "Randomize Event Enemies" | the script puts it somewhere and starts its AI: any type does that alike. Before, every `LoadWork` made a record event-driven: zombies of 0031, 0140, 1021, 1070, 1100, 4000, 9200 took only other looks, and RM_0050's second Cerberus and RM_2000's first Bandersnatch stayed |
| event-driven (`LoadWork`, motion pauses, links, positioning, zombie/face/render/look ops, `EneSeOn`) or HP read/set by the script | **another look of its own type**, with "Randomize Event Enemies" | the event plays the type's motion numbers and room motions on its skeleton, sets its AI modes (`SubCtr` 143/146/147), tests `id == 1` and its HP. All zombie looks share an 18-node first sub-model, Hunter and Sweeper a 28-node one, so the scene still fits; types with one look (Cerberus, Bandersnatch, Black Widow, bat, Spotter, moth, albinoids, anatomist) stay |
| bosses and everything in their rooms (ids 2, 12..17, 19, 23, 24, 29, 30, 53..55: Gulp Worm, Alexia, Nosferatu, Alfred, Monster Steve, Tyrant fights, Giant Black Widow, tentacles, knights) | kept: the whole room | the scripts wait for the boss's death or drive it |
| NPCs (ids >= 31), props (0), fish, albinoids, ants, moths, ceiling trap, scripted object | kept | water/wall/set-piece types, or no AI of their own |
| a record without a floor (`flr_no < 0`) | kept, unless this visit's script puts it somewhere (above) | parked below the room for an event: RM_1121's and RM_2000's Bandersnatches wait 36 units under the courtyard and climb up (types 10 and 9 of their AI); on a later visit (`ev_flg` 168 / 173 set) the script puts them on the ground and starts their AI, and they take a kind that fits there |
| the Bandersnatch's arm (type 9 model 1) | kept | RM_3130's scene plays its room motions on it; before, it became the whole Bandersnatch (model 0) as "another look" |
| a free or shown ground record that does not stand on the ground ("Spots": RM_6090's Black Widow under the floor) | another look of its type | its own type does what it does there; another kind would be stuck in the air or under the floor |

**Kinds** a free or shown record can become (`enemyopt_glue.c kinds[]`); a kind is drawn first by
weight, then one of its looks (the spawner's entries of that type, from any stage's room file):

| Kind | Looks | Weight Mild / Wild | Rules |
|---|---|---|---|
| zombie | 24 | 5 / 5 | |
| Cerberus | 1 | 4 / 4 | |
| Bandersnatch | 1 (Bandersnatch 2 is an event's arm: never) | 4 / 4 | |
| Hunter, Sweeper | 1 each | Hunter 2 / 2, Sweeper 1 / 2 (3 / 4 together, as the single Hunter kind before the split) | not in kill-count rooms (may leap out of a room). Two kinds in every variety: Mild's two kinds per room, "each kind once" and Types per Room count them as two |
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
  Cerberus, Bandersnatches, anatomists. So is a record whose kill some code waits for (a `Ck` of its
  `DieCk` bit for set, anywhere in the room's scripts: the Battle Game's 5520, 5530, 5540 and 5650 open
  their exit only once each record's bit is set), and Mild's two kinds of such a room are of those kinds. Each dies with `flg & 2` (each has a `DieCk` in its own
  rooms, and `RECVX_ENEMY_KILL` kills them all in these rooms), so the record's kill bit is set as before.
* **Variety**: *Wild* (default) draws each record's kind from all kinds that fit, a kind not yet
  drawn in this room first, then a look not yet used in this room. *Mild* draws two kinds per
  room (no Tyrant, Giant Black Widow or fliers) and alternates between them; if neither fits a
  record, any Mild kind that does.
* **Custom** (Enemy Selection's third choice, as BioRand's enemy ratio sliders): each kind's
  weight is its rate, Off or 1..10 (`EnemyOpts.rate`, recvx.ini `enemy_rates`), one per kind
  above (the Hunter and the Sweeper each their own) and per variant with a randomizer weight (Mutated Steve too). The kinds' rules stay; a
  record draws from the kinds that fit it by rate alone (no "unused kinds first"; under Types per Room the kinds the room lacks first), so a kind at
  10 among kinds at 1 takes most records, and a kind at Off is never placed. Records that cannot
  change keep their type (event-driven records, single-look types, boss rooms, a record where
  nothing else with a rate fits): an Off zombie still appears where the room's own zombie stays. An event-driven Hunter
  may take the Sweeper's look (and the other way round) in every variety; under Custom only while the other has a rate.
  Fliers become fliers as in Wild (both at Off: they stay). Picking Wild or Mild presets the
  rates to that variety's weights (variants capped at 10), so Custom starts from the last one
  picked. Variants past the 21st have no rate and are not placed by Custom (the rates fill the
  co-op event, see "Co-op").
* **Types per Room** (`enemy_types`, 1..8, default No Limit): in **Wild and Custom a target**, in
  Mild a limit. The records that keep their type count first, then each record in record order.
  Wild and Custom: while the room has fewer kinds, a free or shown record draws only among the
  kinds it lacks that fit it (by weight or rate), so the room gets that many where its records and
  the kinds' rules allow; then the extra enemies add kinds it still lacks ("Extra enemies"). Once
  the room has that many (Mild: always), a record draws only among them (if one of them fits it),
  else as before; an event's record taking another look of its type takes one of the room's kinds
  then too (an event Hunter becomes a Sweeper only when the Sweeper is already one), else keeps its
  look. A room can only go over when a record fits none of its kinds (a bat record among ground
  kinds: fliers stay fliers), logged as `over it` on the room's line. No Limit draws as before the
  target (Wild's unused kinds first, Custom's rates alone: the same enemies per seed). It depends
  only on the options, the room file and the draws (the same on every visit; a co-op load with the partner in the room makes the partner's result).
  The price of variety: in a room with few records each takes another kind, so a kind with a high
  rate takes fewer records than its share ("Types per Room as a target" below).
* **Variants**: a record that keeps its type (another look) keeps its variant (an event's pose, a
  lying zombie); another kind gets variant 0 (free-roaming).
* **Slots**: all records are counted with what their Inits spawn; a change may only add up to 122
  (6 stay free), so a room never exceeds the larger of that and its own count.
* **Determinism**: each record's draws come from xorshift seeded with (seed, room file, record),
  plus what earlier records of the room drew; nothing depends on the player, the game's `rand()`
  or what was loaded before. Both co-op sides compute the same.
* **Fallback**: any refusal (data not found, memory, textures) puts the original record back
  exactly as it was, logged as `record N stays`; it is not in the host's result, so the guest
  keeps it too.
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

#### Types per Room as a target

Measured 2026-11-11 with dry runs of the randomizer (its draws without the spawns, checked against real
loads) at each of the 113 room files `enemy_floor_check.sh` tours, 100 seeds each, the extras as
`enemy_extras_frame` picks them; averages over the rooms the randomizer changes something in (11100
room x seed results per setting). These rooms average 3.3 records: 1.6 free or shown (another kind) and
1.6 event-driven (another look of their type). 48% have no free record (every zombie there plays in an
event), so they show 1.17 kinds whatever the setting, and got no extras either (extras then copied free records); the most kinds the
records alone could show averages 2.13. Before the target, Custom drew every record by rate alone, so
two records often drew the same kind (two free records at equal rates: 2.13 kinds where they could show
2.37), and extras only copied.

| Custom, every kind at 5 | No Limit | 1 | 2 | 4 | 8 |
|---|---|---|---|---|---|
| kinds per room, before | 1.87 | 1.11 | 1.55 | 1.86 | 1.87 |
| kinds per room, now (records) | 1.87 | 1.11 | 1.58 | 2.08 | 2.13 |
| now with 3 / 8 extras | 1.87 | 1.11 | 1.59 | 2.25 / 2.25 | 2.59 / 2.61 |
| rooms at the setting or their records' most, before / now | | 100% / 100% | 97% / 100% | 81% / 100% | 79% / 100% |

Custom at 5 with Types per Room 8, by the room's free records (share of the results): none (48%) 1.17
kinds; one (5%) 1.41 before, 1.50 now, 1.78 with extras; two (17%) 2.13, 2.37, 3.02; three (12%) 2.54,
3.00, 4.12; four or more (18%) 3.20, 4.05, 5.30. With 8 extras 12% of the rooms show 6 or more kinds
(before 0.1%). Wild with 8: 2.11 before (its unused kinds first already spread the records), 2.13
now, 2.61 with extras. No Limit (Wild, Mild, Custom) and Mild with a limit give the same results as before
for every seed and room.

The rates: at No Limit each free record's kind follows the rates among the kinds that fit it (chi-square
7 to 8 at 11 degrees of freedom, every kind within 0.3 points of its share). Under a target a room's
records take different kinds, so a kind with a high rate takes fewer records than its share: with
zombies 10, anatomists 8, Hunters 6, bats 5, Black Widows 4, Bandersnatches 3, Sweepers 2, Giant Black
Widows 2, Cerberus 1, Tyrants 1, Spotters 1 and Mutated Steve 3, at Types per Room 8 zombies take 22% of
the free records (33% at No Limit; most rooms already have event zombies), anatomists 24% (27%), Hunters
14% (13%), Bandersnatches 15% (10%), Cerberus 9% (3%): the kinds with high rates lose share, those with low rates
gain. At every kind 5 the shares stay within 2 points.

#### Event enemies per room

What "Randomize Event Enemies" does with each room's event-driven and script-shown records of the
roaming types on a first visit of a new game (generated from the randomizer's "scripted records" log lines of
`enemy_floor_check.sh`; records not listed are free, or not enemies of these types). On later visits, with the story
flags a first visit sets, some change more (the script's code for the first visit no longer runs): RM_1121's and
RM_2000's Bandersnatches (`ev_flg` 168 / 173, their climb), RM_1090's Bandersnatch (16), RM_0050's first Cerberus
(114 or 115) take another kind, RM_4010's bats (247) another flier. *Another kind*: the script only shows or hides them. *Another look*: an event drives them
(the scene keeps its type). *Kept*: event-driven with no other look of their type, or a boss arena.

| Room | Records (index) and what they become |
|---|---|
| 0020 | zombie 0,1,2,3,5: another look (event-driven) |
| 0030 | zombie 3: another look (event-driven) |
| 0031 | zombie 1,4,5: another look (event-driven); zombie 2,3: another kind (loaded only for ops any type takes) |
| 0050 | Cerberus 0: **kept** (event-driven, one look); Cerberus 1: another kind (loaded only for ops any type takes); zombie 2: another look (event-driven) |
| 0060 | zombie 3: another look (event-driven) |
| 0070 | zombie 0,1,2,3: another look (event-driven) |
| 0080 | zombie 0,1,2,3,4: another look (event-driven) |
| 0090 | zombie 0,1,2,3,4: another look (event-driven) |
| 0110 | zombie 1: another look (event-driven); anatomist 4: **kept** (event-driven, one look) |
| 0140 | zombie 0,1: another kind (loaded only for ops any type takes); zombie 2,3: another look (event-driven) |
| 0150 | zombie 0: another look (event-driven) |
| 0160 | zombie 0,1,2,3,4: another look (event-driven) |
| 1021 | zombie 0,1: another kind (shown or hidden only); zombie 2: another kind (loaded only for ops any type takes); zombie 3: another look (event-driven) |
| 1030 | bat 3: **kept** (event-driven, one look) |
| 1070 | zombie 0,3: another look (event-driven); zombie 1,2: another kind (loaded only for ops any type takes) |
| 1090 | Bandersnatch 1: **kept** (event-driven, one look) |
| 1100 | zombie 0,1,2,3,4: another kind (loaded only for ops any type takes) |
| 1121 | Bandersnatch 0,1: **kept** (event-driven, one look) |
| 2000 | Bandersnatch 0: another kind (loaded only for ops any type takes); Bandersnatch 1: **kept** (event-driven, one look) |
| 2031 | zombie 0,3: another look (event-driven) |
| 3020 | Cerberus 2,3: another kind (shown or hidden only) |
| 3030 | zombie 0,1: another look (event-driven) |
| 3050 | zombie 0,1,2,3,4: another look (event-driven) |
| 3070 | zombie 1: another look (event-driven) |
| 3100 | Bandersnatch 0,1: **kept** (event-driven, one look) |
| 3120 | albinoid infant 0: **kept** (not in the pool); zombie 1,2: another look (event-driven) |
| 3130 | Bandersnatch arm 0: **kept** (its scene plays room motions on it; before, it became the whole Bandersnatch) |
| 3140 | albinoid infant 0,1,2,3,4,5,6: **kept** (not in the pool); zombie 7: another look (event-driven) |
| 3160 | zombie 0,1,2,4: another kind (shown or hidden only) |
| 3170 | zombie 0,1,2,3: another kind (shown or hidden only) |
| 3180 | Bandersnatch 2,3: another kind (shown or hidden only) |
| 3200 | zombie 0,1,2: another kind (shown or hidden only) |
| 4000 | zombie 3,4,5: another kind (loaded only for ops any type takes) |
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
| 6030 | zombie 2: another look (event-driven); moth 3,4,5: **kept** (not in the pool) |
| 6050 | zombie 0,1,2,3,4: another look (event-driven) |
| 6070 | Cerberus 0,2: **kept** (event-driven, one look); Cerberus 1: another kind (shown or hidden only) |
| 6080 | zombie 1,2,3,4,5: another look (event-driven) |
| 6090 | Black Widow 0,1: another kind (shown or hidden only); Black Widow 2: **kept** (off the ground, one look) |
| 7031 | zombie 0,1: another kind (shown or hidden only) |
| 7050 | Hunter 0: another look (event-driven) |
| 7060 | zombie 1: another look (event-driven) |
| 7070 | Bandersnatch 6: **kept** (event-driven, one look) |
| 7071 | zombie 4,5,6: another kind (shown or hidden only) |
| 7080 | Black Widow 0,1: **kept** (event-driven, one look) |
| 7081 | Spotter 0,1: **kept** (event-driven, one look); Hunter 2,3: another look (event-driven) |
| 7100 | albinoid adult 0: **kept** (not in the pool) |
| 7110 | Spotter 0: **kept** (event-driven, one look); Hunter 1,3: another look (event-driven) |
| 7130 | Hunter 0,1,2,3: another kind (shown or hidden only) |
| 7140 | zombie 0: another look (event-driven) |
| 7181 | Hunter 0: another kind (loaded only for ops any type takes); Spotter 1: **kept** (event-driven, one look) |
| 7190 | zombie 0,1: another kind (shown or hidden only) |
| 7191 | Spotter 0: **kept** (event-driven, one look); Hunter 1: another look (event-driven) |
| 7200 | Hunter 0: another kind (shown or hidden only) |
| 7250 | Black Widow 0,1: another kind (shown or hidden only) |
| 8001 | zombie 0,1,2: another look (event-driven) |
| 8010 | Hunter 0,1: another kind (shown or hidden only) |
| 8020 | Hunter 0: another look (event-driven) |
| 8030 | Spotter 1: **kept** (event-driven, one look); Hunter 2: another look (event-driven) |
| 9010 | zombie 0,1,2,3: another look (event-driven) |
| 9020 | zombie 0,1,2: another kind (shown or hidden only) |
| 9030 | zombie 1,2,3,4,5: another look (event-driven) |
| 9060 | zombie 0,1,2,3,4: another look (event-driven) |
| 9080 | zombie 1,2,3,4,5: another look (event-driven) |
| 9100 | Spotter 0,1: **kept** (event-driven, one look); Hunter 2: another look (event-driven) |
| 9110 | Hunter 0: another look (event-driven) |
| 9120 | zombie 0,1,2,3,4,5,6: another kind (shown or hidden only) |
| 9130 | Hunter 0: another kind (shown or hidden only) |
| 9190 | zombie 0,2,3: another look (event-driven); zombie 1,4: another kind (shown or hidden only) |
| 9200 | zombie 0,1,2,3,4: another kind (loaded only for ops any type takes) |
| 9220 | Hunter 0: another look (event-driven) |
| 9290 | type 25 0: **kept** (event-driven); zombie 2,4,5: another look (event-driven); zombie 3: another kind (shown or hidden only) |
| 9340 | zombie 1,2,3: another look (event-driven) |

### Limits

* Extra enemies come only from the host: in a room only the guest is in, there are none until the
  host comes in.
* The script is read for this visit with the `ev_flg` / `ky_flg` bits as each co-op side has them at its room load: a
  flag that changed between the host's load and the guest's could make them randomize a record differently. The flags
  that matter (RM_1121's 168, RM_2000's 173: their Bandersnatches' climb) are set on an earlier visit, long synced.
* Extra enemies stand where the grid has one height per cell: where two floors overlap (a balcony over the floor) the
  first one reached has the cell, so the other gets fewer spots there.
* Two looks of one type share the first one's sound bank.
* A refusal by `spawn_at` keeps the original record; both co-op sides decide the same unless one
  side's memory is fuller.
* **BioRand with random doors**: each door it connects sets a fixed room case (`RoomCaseNo` before its
  `SetDoorCall`: the case file its room graph chose), while the doors it leaves keep their story
  conditions. So one room can be entered as two case files in the same story state, depending on the
  door. In the pack of seed string `R840-K9YC-…`: 4/01 is RM_4011 from 5/00 but RM_4010 from 6/05; 1/12
  stays RM_1120 (four zombies, kill bits 85..88) from 3/08 after the story made it RM_1121 (two
  Bandersnatches, 90..91) from 1/00. Each case file has its own records and kill bits, as after a story
  event in the original game, so the other door shows other enemies, and kills in one file do not
  count in the other; the enemy options key on the case file too. The port loads what the door says
  (a case BioRand did not choose could miss its items).

## Tools

`pc/tools/enemies.py` (Python 3, stdlib, imports `rdx.py`; reads the disc image at run time):

| Command | Output |
|---|---|
| `room ROOM` | the room's enemy records, floor areas and route boxes, and every script reference to each record with the conditions of its enclosing blocks |
| `census [--rooms]` | counts per type and room, kill flags, hidden/shown/event-driven records, helper conflicts, case variants, `gm_mode`/`ply_id` tests, floor areas, routes |
| `sources` | per (type, model version) the smallest room carrying its model and motions |
| `selftest` | the script walker on RM_0060 |

`pc/tools/enemy_revisit_check.sh OUT [SEED...]` (headless, about 7 minutes): a new game visits twelve
rooms twice with the randomizer, placements and three extras on, killing everything (and once only the
records); every room must get the same records and extras on both visits, and nothing killed may come back (an
extra keeps its slot, so a dead one may not be in its slot again, nor more of a type than lived on).
`EXTRA=8` raises the extras; the other `RECVX_ENEMY_*` (Custom, its rates, Types per Room) pass through.

`pc/tools/enemy_rates_check.sh OUT [SEED...]` (headless, about 4 minutes): a new game tours 22 rooms per
seed with eight extras, once with Custom rates (zombies Off, Cerberus 10, the rest 1: no record may become a
zombie, Cerberus must take most ground records), twice with Hunter 10 and Sweeper Off and the other way round
(no record may become the one at Off, some must become the other), Wild and Mild each with Types per Room 1 and 2,
and Custom with Types per Room 2 and 8 (no room over its limit, extras included, unless logged; the Hunter and the
Sweeper two kinds in every variety; in Mild every extra of a kind the room's records have or of its two Mild kinds; with 8 some extras of
kinds the room's records lack). It prints the kinds per room with and without the extras.

`pc/tools/enemy_floor_check.sh OUT [SEED...]` (headless, about 25 minutes): a new game tours the 114 room files
with records the options touch, per seed in Wild, Mild and Custom (every kind at 5), each with the randomizer,
placements and eight extras, and logs every enemy two frames into each room (`RECVX_ENEMY_FLOORS`). Every walking
enemy placed elsewhere or added, or turned into another type at its own spot, must stand within a unit of the
ground under it ("Spots"); records that keep their type at the game's own spot are listed apart. Before the
"Spots" rules, 4 seeds: 87 of 1853 placed or added walkers stood in mid-air (74: over the pool and stairs of
RM_3060, by the balconies of RM_2011, off RM_9310's platform, over RM_1120's ramp, in RM_5580) or sunk in a crate or
step (13: RM_1060, RM_2011, RM_5690, RM_7180), and RM_6090's Black Widow under the floor became another kind in
every run; with them, none. With the extras anywhere in a room (2026-11-11, 4 seeds): 3 more, now ruled out (RM_8010's
catwalk line wall, RM_7050's floor lines, a Giant Black Widow by the edge of RM_1000's raised walk).

`pc/tools/enemy_door_check.sh OUT [SEED...]` (headless, about 15 minutes): a new game tours the 94 room files that get
extras with the randomizer (Wild) and eight extras, and in each room also surveys the extras' spots of 50 seeds
(`RECVX_ENEMY_SPOTS`). Every surveyed spot and every extra that came must be at least 30 units from every door trigger
and entry position as the game measures them, and from every entry position and type 0 door trigger of the room file
as `pc/tools/rdx.py` reads it (an independent check). It prints per room how many of the eight found a spot.

`pc/tools/enemy_bounds_check.sh OUT [SEEDS]` (headless, about 5 minutes): the same tour without options, and in each
room every spot of the extras' grid and the spots the extras of seeds 1..SEEDS (default 100) take are checked apart
from the grid's rules (`RECVX_ENEMY_OOB`): reached by a flood fill of 2-unit cells by the player's own wall check from
where the player stands and the entry positions in a camera zone (stairs and ramps too), in a camera zone of the floor
by the game's test (`bhCheckCutArea`), the player's body fits, not in water. Passes when none is flagged; prints the
counts per room with 8 asked over the seeds.

Test aids:

| Env var | Effect |
|---|---|
| `RECVX_SPAWN="gameframe:id[.mdlver][@dist],..."` | spawns the first spawner entry of that type/model version at that game frame (`v<id>`: an enemy variant); traces its state every 30 frames, HP changes and hits on the player |
| `RECVX_SPAWN_SURVEY="stg:rom,..."` | tries every spawner entry in each room and logs the result; `RECVX_SPAWN_SURVEY_ALL=1` keeps the spawns |
| `RECVX_ENEMY_EXTRA`, `_PLACE`, `_RANDOM`, `_SEED`, `_VARIETY`, `_EVENTS`, `_TYPES`, `_RATES` | override the enemy options (`_RATES` as the `enemy_rates` line) |
| `RECVX_ENEMY_KILL=n[:r\|:x]` | from n game frames into each room, every 20 frames each living, shown enemy of the room's records and its extra enemies (ids 1..30, in its AI) takes a heavy hit as from the player's weapon (the knife's without one: weapons 0 and 1, none and the lighter, never hit in the game; `:r` only the records, `:x` only the extras); logs the living ones at frame n, each death (`flg & 2`, what `DieCk` turns into the kill bit) and per room how many died (Black Widows take damage through their body parts and do not die from it) |
| `RECVX_HEADSHOT=n` | n game frames into each room, every living zombie in its AI (records and added ones) takes one head hit as from the magnum (weapon 6 on joint 11, the game's burst path); its head state (`exp0 + 0x78`), hp and modes are logged every 15 frames for 90 frames |
| `RECVX_ENEMY_SPOTS=seeds` | at the first frame each room runs, the extras' spots of seeds 1..seeds (eight extras, the randomizer off: spots only, nothing spawns), one line per seed with each spot's x z and distances to the nearest door trigger and entry, then the room's grid (cells, walkable, spots) |
| `RECVX_ENEMY_OOB=seeds` | at the first frame each room runs, the bounds check of the extras' spots (`pc/tools/enemy_bounds_check.sh`: the grid's spots and those of seeds 1..seeds with eight extras, nothing spawns): a line per flagged extra's spot (`reach`, `cam`, `wall`, `water`), then the room's totals and its counts of 8 over the seeds; 0 lists every flagged grid spot instead |
| `RECVX_ENEMY_FLOORS=n` | n game frames into each room (2: the frame after the extras came), every enemy (ids 1..30) is logged with its position, floor and room-file type, the ground under it as `find_spot` sees it ("Spots", ` slope` on a slope) and the bottom of the box that ground is the top of (a shelf with room below is no crate), its floor's height and what the options did to it (`moved`, `random`, `extra`, `orig`; `other`: a part an enemy's Init put in a slot of its own) |
| `RECVX_ENEMY_TRACE=dist` | traces enemy steps (co-op targets) |
| `RECVX_TRACE_SND=1` | logs sound requests and extra banks |
