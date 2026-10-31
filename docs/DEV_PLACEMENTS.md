# Object placer (`--dev`)

A developer tool for putting extra item boxes, herbs and other objects into rooms by hand, as
input for a later gameplay option ("Additional Boxes/Herbs", useful with the item and enemy
randomizers). The placed models are **drawn only**: they cannot be used, they are not in the
game's save, the scripts and collision never see them, and co-op does not send them. English only.

Code: `pc/src/host/placer.c` (catalogue, file), `pc/src/host/placer_ui.cpp` (window, gizmo,
keys; render thread), `pc/glue/placer_glue.c` (loading, drawing, projection; game thread),
hook `game.c bhAllDrawModel` (after the room's objects and items). Test: ctest `placer`.

## Use

1. Start with `--dev` (without it nothing of the placer exists: no window, no drawing, the file is
   not read). Third person (F5) is the easiest: the camera follows the player.
2. Walk to the spot, open **F2**. Next to the Cheats window the **Object Placer** window opens.
   While a window is open the game gets no input; close F2 to walk again.
3. Choose the **Model**, then **Place in Front** (12 units ahead, facing the player) or **Place at
   Player**. The new entry is selected; the list shows the entries of this room.
4. Move it with the gizmo or the fields, **Snap to Floor**, turn it. **Duplicate**, **Delete**,
   **To Player** work on the selected entry; click a list row to select or deselect it.
5. **Export** writes the file at once and copies its text to the clipboard: hand that text over.
   The file is also written by itself half a second after each change and read at the next start,
   so nothing is lost between sessions.

Entries are shown in their room (stage and room; every case of it) whenever you are there; the
"Other rooms" list has a **Go** button per room (the F2 teleport, entry 0).

### Gizmo (selected entry, F2 open)

| Handle | Mouse drag |
|---|---|
| red / green / blue arrow | move along world x / y (up) / z, as the arrow is drawn on the screen |
| white square (origin) | move on the floor plane (x and z) |
| green ring | turn about y (`ay`, the heading) |
| red / blue ring ("Pitch and Roll Rings") | the `ax` / `az` angles |

Ctrl while dragging: positions in 0.5 units, angles in 15 degrees. A hovered or dragged handle is
yellow. It works with the fixed cameras and the third-person camera alike (the frame's own
camera, projected by the game thread).

Keys (F2 open, entry selected, no text field active): **arrows** move on the floor plane as the
camera shows it (left/right/up/down on the screen), **Page Up / Page Down** up and down, **Q / E**
turn left / right; steps 0.1 units and 1 degree, with **Shift** 1 unit and 15 degrees.

**Snap to Floor** puts the model's lowest point (unturned) on the surface below its origin: the
highest floor level (`rom->grand`, the game's level rule) at or below it, or the top of a box wall
(desk, crate: `bhGetGroundPosition`) if that is higher.

## Models

| Key | Model | Source |
|---|---|---|
| `item_box` | Item Box (body and lid) | RM_1080: room model node 50 with its subtree (the body), object record 4 (the lid, id 376) |
| `typewriter` | Typewriter | RM_1080: room model nodes 35, 36, 43, 45, 47, 48 |
| `green_herb` | Green Herb Pot (item 21) | RM_1080 item record 6 |
| `red_herb` | Red Herb Pot (item 22) | RM_3190 item record 3 |
| `blue_herb` | Blue Herb Pot (item 23) | RM_3190 item record 5 |
| `blue_herb_bed` | Blue Herb Planter (cures poison, unlimited) | RM_7090 object record 9 (object id 774) |
| `handgun_ammo` | Handgun Bullets (item 12) | RM_1080 item record 5 |
| `first_aid` | First Aid Spray (item 20) | RM_3040 item record 1 |
| `ink_ribbon` | Ink Ribbon (item 31) | RM_1080 item record 3 |

* The item box's body and the typewriter are not objects in any room: they are part of each save
  room's room model. They are copied out of RM_1080's room model (objects, models, vertex and
  polygon lists of those nodes); only the pictures they use are uploaded (2 and 2 textures), their
  polygon lists renumbered to them. Origin: the bottom centre of the model; the item box's front
  (lock plate) faces +z at `ay` 0, its lid hinges at the back (-z).
* Object and item records are realized as `room.c bhSetObjMdl` / `bhSetItmMdl` do, with their
  texture lists uploaded whole. Origin: the model's own, so a position is directly usable as an
  item record's (a herb's origin is 2.9 above its pot's bottom). They are lit as the room's items
  (`objitm.c bhDrawObjItm`: item ambient, and the room's directional light where it has one), so
  a placed herb looks like a real one next to it; the planter, an object, gets the objects' ambient
  (`rom->amb_obj`) instead.
* **Herbs** are the potted plants as the rooms show them. The game has no separate pot: a herb
  pickup is one item record whose model is the plant in its pot (3 nodes: node 0 an empty root at y
  -2.9, node 1 the plant, node 2 the pot as its child, about 1.5 wide and 1.4 high), and taking the
  herb removes the whole item, pot included. All 121 herb records on the disc (91 green, 11 red, 19
  blue) use this same geometry; only the one 256x256 picture differs by colour, and it is the same
  picture (same gindex, same bytes) in the rooms compared. No object record, room model node or effect
  belongs to a herb, and no room has a mixed-herb pickup (no item record of the disc is one of the
  mixed herbs, items 24 .. 29). The keys stayed `green_herb`, `red_herb`, `blue_herb`, so older
  files load as before.
* **Blue herb planter** (`blue_herb_bed`): the game's only herb bed, a white planter box with
  several blue herb plants (object id 774: one node, 424 vertices, 6.0 x 4.2 x 2.7 units, one
  256x256 picture, gindex 0x599c). Examined while poisoned it asks "Use blue herb?" and cures; it
  never runs out and gives no item. It stands in three rooms (same model chunk and picture bytes in
  each): RM_6030 (stage 6 room 3, Antarctic Base corridor with the moth nest, Claire), RM_9030 (the
  same corridor in Chris's part) and RM_7090 (stage 7 room 9, Chris's Military Training Facility
  room with the typewriter and the colour drawers). RM_9140 has the record and the script too, but
  hides the model and switches its trigger off at entry (event 5), its texts are blank and two green
  herb pickups stand there instead. There is no green or red planter: no other object, room model
  node or text of the disc is a herb bed ("Blue herb is growing here." and "It's a planter containing
  the blue herb." occur only in these rooms). Source: RM_7090 (no enemies). Origin: the object's own,
  2.0 above its bottom (retail rooms put it at y 2.1 on floor 0).
* Source rooms have no enemies, so the texture table index of an object or item follows from the
  objects and items alone (room model, then per object id, then per item id, as `bhSetRoom`).
* Memory: one 160 KB buffer inside the address window holds all models and their work (all nine
  use 131 KB, the planter 21 KB of it); the textures (the planter's picture: 66 KB) stay loaded for the session and are read again
  after `njReleaseTextureAll` (title, game over). Gindex values of these pictures are unique on
  the disc, so no other room's texture is mistaken for them.

## File: `<save dir>/dev_placements.ini`

One line per entry, `key=value` pairs separated by spaces; lines starting with `;` or `#` are
comments; unknown keys are ignored. The header comment lists the models with their sources and
their extent around the origin (x, y, z at angle 0).

| Key | Meaning |
|---|---|
| `type` | model key (table above) |
| `stage`, `room`, `case` | `sys->stg_no`, `sys->rom_no`, `sys->rcase` where it was placed |
| `player` | `sys->ply_id` then (0 Claire, 1 Chris, 2 Steve, 3 Wesker): the story part (Claire's and Chris's parts use other room cases) |
| `x`, `y`, `z` | position in game units (about 10 cm; y up) |
| `ax`, `ay`, `az` | angles in game units (65536 = 360 degrees, `njRotateXYZ` order as `bhCalcModel`; `ay` 0 faces -z, 16384 -x, 32768 +z, 49152 +x) |
| `ax_deg`, `ay_deg`, `az_deg` | the same in degrees, -180..180 (for reading; ignored when read back) |
| `floor` | floor number at the position (`bhCheckFloorNum(y)`): the `flr_no` of records there |
| `file` | the room file of that case, `RM_SRRC.RDX` (for reading) |

Example:

```
; item_box      Item Box: RM_1080 room model node 50 + object record 4, extent x -6.0..6.0 y 0.0..9.0 z -4.7..4.7
; blue_herb     Blue Herb Pot: RM_3190 item record 5, item 23, extent x -2.0..1.6 y -2.9..2.3 z -0.9..1.3
; blue_herb_bed Blue Herb Planter: RM_7090 object record 9 (id 774), extent x -3.0..3.0 y -2.0..2.2 z -1.2..1.5
type=item_box stage=1 room=8 case=0 player=0 x=-5.37 y=0.00 z=20.00 ax=0 ay=16392 az=0 ax_deg=0.0 ay_deg=90.0 az_deg=0.0 floor=0 file=RM_1080.RDX
type=blue_herb stage=1 room=8 case=0 player=0 x=-10.37 y=10.58 z=21.00 ax=0 ay=8 az=0 ax_deg=0.0 ay_deg=0.0 az_deg=0.0 floor=1 file=RM_1080.RDX
```

## What a later importer needs

Everything per entry is in the line: the room (stage, room, and the case and character it was
placed in, to decide which cases get it), position, angles and floor number. To make an entry
work:

* **Potted herbs**: exactly a retail herb (every retail one is this): an item record with `flg` 1,
  `id` 21 / 22 / 23, `type` 0, `mdlver` 0, `wrk_no` 0, `prm1` 0, `aspd` 0 (119 of 121), `ax` = `az`
  = 0, `ay` from the entry, `hide` = -1 x 4, `flr_no` = `floor`, position = the entry's (the item
  origin is 2.9 above the pot's bottom: Snap to Floor gives that, retail herbs stand at floor +
  2.8..3.0). Model: that item record's own MLB (pot included) with its one-picture texture list,
  e.g. the source in the table. Trigger: type 4, `attr` 0x1 (picked up crouching; without 0x1 mostly
  for a herb up on a table or shelf, picked up standing; some add the direction limits
  0x400..0x2000), a box of `w` x `d` about 5 x 5 (4.99 .. 6.99) with its corner about 2.5 .. 3 below
  the herb's x and z, `prm0` = the item slot, `flr_no` = `floor`. The pickup gives `id` (or `type`
  when non-zero, see [modding/ROOM_FORMAT.md](modding/ROOM_FORMAT.md) "Generated pickups"); the
  general rules below apply.
* **Herb / item pickups**: an item record (`ETTY_WORK`: `id` = item, `flr_no` = `floor`, position,
  `ax/ay/az`) with its model, plus a type 4 trigger (`etcp`, `prm0` = the item slot, a box of
  about 5 x 5 around the position, `flr_no` = `floor`) and a persistent `it_flg` bit (script op 35
  `ItmSetCk` at entry, op 14 `ItmCk` on pickup), as the room loader and scripts do
  ([modding/ROOM_FORMAT.md](modding/ROOM_FORMAT.md) "Enemies, objects, items", "Triggers"). The
  model comes from the source in the table (the same loader as here).
* **Blue herb planter**: as in RM_6030, RM_9030 and RM_7090 (`rdx.py script RM_6030`: event 30
  and the two `scd1` blocks before its end; RM_7090: event 60, trigger 10, messages 14..16):
  * Object record: `flg` 1, `id` 774, `type` 0, `mdlver` 0, `wrk_no` 0, `prm1` 0, `aspd` 0,
    `hide` -1 x 4, `ax` = `az` = 0, `ay` from the entry (retail: -16380, about -90 degrees, its back
    to a wall on +x and the player on the -x side), `flr_no` = `floor`, position = the entry's.
    Model: the object's MLB (a plain `MDL` chunk, 18240 bytes, no skin) with its one-picture
    texture list, e.g. RM_7090 object 9.
  * Trigger: type 3, `attr` 0 (no message of its own, usable from every side), `prm0..3` 0,
    `flr_no` = `floor`. Retail boxes: RM_7090 x 37..47, z 17..24 (planter at 37.9, 20.6); RM_6030
    x 30..42, z 96..104 (planter at 31.8, 100.0); RM_9030 x 30..39, z 96..104. So the box is the
    planter's width plus 0.5..1 on each side (7..8 across), starting 0.9..1.8 from its centre on
    the player's side and reaching 7..10 back (into the wall). Type 3 tests a point 3..6 units
    ahead of the player's feet (`hitchk.c bhCheckExmAtari` `pxn[3]`), so the player at the
    planter, facing it, hits the box.
  * Collision: a box wall over the planter (RM_7090 wall 12: type 0, x 37..39, z 18..23, `h` 2;
    RM_6030 wall 58: type 1, x 31..40, z 97..103).
  * Messages (the room's `mesp`, three of them): Q "{FF00}Blue herb is growing here.{FF02} Use blue
    herb?" with the Yes/No choice (`{FF00}` + 16 spaces + `{FF04}Yes  {FF04}No`), N "It's a planter
    containing{FF00}the blue herb.{FF02} I don't need to use{FF00}this now.", U "{FF00}You've used the
    blue herb." (RM_6030/9030: 21, 22, 23; RM_7090: 14, 15, 16).
  * `scd1` (every frame): when the player activated the trigger (`Ck` cb_flg 23 with the trigger's
    number), clear cb_flg bit 23, start event E (`EvtOn`, task level 0), set sp_flg bit 27. And the
    answer: when rm_flg 12, cb_flg 19 and st_flg 17 are set, `CmpW 7` (`sys->mes_sel`) 0 (Yes) sets
    rm_flg 14, 1 (No) sets rm_flg 15 (RM_7090: rm_flg 0); both clear rm_flg 12.
  * Event E: `CineSet 3`; clear sp_flg 31, 30, 25; set st_flg 29. If poisoned (`plp->stflg`
    bit 12 in the script's numbering, mask 0x80000): set rm_flg 12, message Q, set cb_flg 18; else
    message N. Wait while rm_flg 12 is set, then while cb_flg 18 is clear. If poisoned: on rm_flg
    14 clear it, clear the poison bit, message U; on rm_flg 15 clear it, set cb_flg 18. Wait while
    cb_flg 18 is clear; `CineSet 1`; clear st_flg 29; set sp_flg 25, 30, 31; `EvtEnd`.
  * Why unlimited: nothing is counted or remembered. No `it_flg` / `ev_flg` bit, no item record or
    pickup; rm_flg 12/14/15 are the room's scratch bits and are cleared again. It cures poison only
    (the bit the blue herb item clears, `sub1.c`), heals no HP and gives no item; not poisoned, it
    only shows N.
* **Item box**: the body and the lid as drawn here (the lid an object, its slot in `sys->obwp`),
  and a type 4 trigger with `attr & 0x2` and `prm0` = the lid's object slot in front of the box:
  `hitchk.c bhCheckExmAtari` then animates the lid (`objitm.c bhObjItmBox`) and opens the item
  box screen; `prm0 = 0xFF` opens the screen without a lid. The front is +z of the model turned by
  `ay`; the footprint is the extent in the header (12 x 9.4 units).
* **Typewriter**: a type 3 trigger with the typewriter's message (the ink ribbon question) in
  front of it; the save screen opens as `RECVX_TEST_TYPEWRITER` does (`cb_flg |= 0x200000`).
* Walls: none are added here; a solid box needs a type 0 wall record (box `w` x `d`, height 9).

## Test aids

| Variable | Effect |
|---|---|
| `RECVX_PLACE="f:op[:args],..."` | game frames counted in rooms (with `--dev`): `add:type[:dist]` (in front of the player, facing it, selected), `sel:i`, `move:dx:dy:dz`, `rot:ax:ay:az` (degrees), `snap`, `del`, `rings:0/1`, `log` (the room's entries and, with the F2 window open, the gizmo's screen place) |
| `RECVX_PLACE_DRAG="f:handle:dx:dy,..."` | from presented frame f, the mouse grabs the selected entry's handle (`x y z free rx ry rz`) and moves by dx, dy window pixels over 20 presented frames (F2 open: `RECVX_SHOW_OVERLAY=2`) |

Limits: the list holds 256 entries; at most 8 roots and 24 nodes per model and 16
pictures; no click-to-select in the picture (use the list). Q/E were not pressed in the headless
tests (the offscreen driver gives letter keys no key code); arrows and Page Up/Down were.
