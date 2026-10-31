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
| `green_herb` | Green Herb (item 21) | RM_1080 item record 6 |
| `red_herb` | Red Herb (item 22) | RM_3190 item record 3 |
| `blue_herb` | Blue Herb (item 23) | RM_3190 item record 5 |
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
  item record's (a herb's origin is 2.9 above its pot's bottom).
* Source rooms have no enemies, so the texture table index of an object or item follows from the
  objects and items alone (room model, then per object id, then per item id, as `bhSetRoom`).
* Memory: one 160 KB buffer inside the address window holds all models and their work (all eight
  use about 110 KB); the textures (a few dozen KB) stay loaded for the session and are read again
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
; item_box      RM_1080 room model node 50 + object record 4, extent x -6.0..6.0 y 0.0..9.0 z -4.7..4.7
; blue_herb     RM_3190 item record 5, item 23, extent x -2.0..1.6 y -2.9..2.3 z -0.9..1.3
type=item_box stage=1 room=8 case=0 player=0 x=-5.37 y=0.00 z=20.00 ax=0 ay=16392 az=0 ax_deg=0.0 ay_deg=90.0 az_deg=0.0 floor=0 file=RM_1080.RDX
type=blue_herb stage=1 room=8 case=0 player=0 x=-10.37 y=10.58 z=21.00 ax=0 ay=8 az=0 ax_deg=0.0 ay_deg=0.0 az_deg=0.0 floor=1 file=RM_1080.RDX
```

## What a later importer needs

Everything per entry is in the line: the room (stage, room, and the case and character it was
placed in, to decide which cases get it), position, angles and floor number. To make an entry
work:

* **Herb / item pickups**: an item record (`ETTY_WORK`: `id` = item, `flr_no` = `floor`, position,
  `ax/ay/az`) with its model, plus a type 4 trigger (`etcp`, `prm0` = the item slot, a box of
  about 5 x 5 around the position, `flr_no` = `floor`) and a persistent `it_flg` bit (script op 35
  `ItmSetCk` at entry, op 14 `ItmCk` on pickup), as the room loader and scripts do
  ([modding/ROOM_FORMAT.md](modding/ROOM_FORMAT.md) "Enemies, objects, items", "Triggers"). The
  model comes from the source in the table (the same loader as here).
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
