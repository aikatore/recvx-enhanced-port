# Import: Code: Veronica on the Dreamcast

Steve's models of the original Dreamcast release (his first hairstyle, with the strand over his face) put into the PS2
game's own formats, so the game draws, skins and animates them as its own: his cutscene model (his mouth moves with the
lines, `bhLipSet`, the face mask's mouth shapes, and his eyes blink, the mask's lid muscles 8 and 9, exactly as the PS2
Steve's do), his event model, and his player model, which the game plays as Steve with all his motions, weapons, grabs
and deaths. Read from the user's own disc image at run time; nothing of the disc is in the repository.

Code: `pc/src/import/gdrom.c` (disc images), `pc/src/import/dc.c` (the conversion), `import.c` (source `dreamcast`,
`IMPORT_DREAMCAST`), `pc/glue/actor_glue.c` (`load_rdx_file`, `load_room_model`), `pc/glue/cast_glue.c` (`own_look`),
`pc/glue/import_menu.c`, the player model `pc/src/audio/adx.c ADXF_OpenAfs` and `actor_glue.c pc_part_file`, `afs_read`,
`coop_glue.c self_frame` (played, looks, co-op). Tests: ctest `import` (LZMA, deflate, a synthetic GDI and CUE disc, an AFS
entry on it, PVR twiddled and VQ), `appear` (the look).

## Using it

IMPORT > CHARACTERS > Resident Evil Code: Veronica (Dreamcast) (new and legacy menu; F9 the same): choose the disc
image, START IMPORT (well under a second). Three models come out:

* **Steve (Dreamcast Hair)**: his player model, a look under "Dreamcast Models" (last in the lists) everywhere a look is
  chosen: Character Model, Claire's and Chris's Model; worn on the player's own skeleton as the player files are (the
  "Steve" look's way: his motions, the weapon's hands, the character played's motions with gender-matched animations).
  Not in the cutscene cast's pools (as the player files there are the cutscene models).
* **Steve (Dreamcast Hair, Cutscene)**: his cutscene model, listed as the imports are (looks, the cast's pools, also
  Swapped's as a Steve look, *Custom*'s lists) while the game is checked.
* his event model (type 43): in no list; it stands in for his type-43 records (below).

Both are listed only while the game is checked ("Code: Veronica (Dreamcast)": the look picker's and the "Custom" lists'
check box, GAME SETUP > CHARACTERS, "Show in Model Lists"; unchecked at first, [ACTORS.md](ACTORS.md) "Imported games").
The Dreamcast Steve setting below does not need it: it replaces Steve, it is not a list entry.

**Dreamcast Steve** (that page, ini `import_dreamcast_steve`, default 0):

| Value | ini | |
|---|---|---|
| Off | 0 | the PS2's Steve |
| Cutscenes | 1 (the old On) | Steve's records are drawn as the Dreamcast models (type 93 as the cutscene model, 43 as the event model; an import from before the event model: the cutscene model for both) in every room where the cutscene cast leaves him as himself (cast off, or a pick of his own look), from the next room load on; while he is played, his doubles too where they would be the game's own Steve |
| Cutscenes and Gameplay | 2 | as Cutscenes, and Steve played is the Dreamcast Steve: the game loads the imported part file for `SYSTEM.AFS/12` (`pc_part_file`); set during a game, the model is swapped at once (`coop_glue.c self_frame`), from the next load of Steve on the game's own |

It applies with the source hidden too (asked for by name). In co-op the partner sees what you play: Steve in
"costume 1" (protocol 60), which a partner with the import shows as the Dreamcast Steve and one without as the PS2's
Steve (`actor_glue.c afs_read`); the cutscene part follows each screen's own setting (visual only).

Headless: `RECVX_IMPORT=dreamcast RECVX_IMPORT_ISO=<image>` (as the other sources).

## Disc images

The US disc 1 (product T-1204N, checked in the IP.BIN, the first sector of the high-density area: "SEGA SEGAKATANA",
product number at 0x40, area `U`); other discs and releases are refused. Read in place:

* **CUE + BIN** (Redump): one BIN per track; a file's first sector is the next address of its area, 0 or, after
  `REM HIGH-DENSITY AREA`, 45000. MODE1/2352 (user data at 16), MODE2/2352 (24), MODE1/2048.
* **GDI** (TOSEC, emulators): each track's address, type (4 data), sector size and file.
* **CHD** version 5 (MAME `chdman createcd`). TOSEC's `.CD1` files are CHDs renamed ("MComprHD" at 0). The hunk map
  (Huffman-coded types, run lengths, then lengths and offsets; copies of other hunks), the codecs `cdlz` (raw LZMA,
  lc 3 lp 0 pb 2) and `cdzl` (raw deflate) for the sector data; the subcode and ECC are not decoded (not needed).
  Tracks: the `CHGD` metadata, consecutive in the image, each padded to 4 frames; track 3 starts at address 45000.
  Not read: `cdfl` (FLAC, which chdman picks for audio tracks only), parent CHDs, versions before 5.

Then ISO 9660 of the high-density area (the volume descriptor at 45016). Both images of the test disc give the same
output file byte for byte (0.06 s from the CUE, 0.2 s from the CHD).

## The conversion

The Dreamcast room files (`R0_SRRC.RDX`, the same LZ as the PS2's) have the PS2 room layout. Steve's cutscene model is
record 0 (type 93) of `R0_0040.RDX`, as on the PS2: a chunk list `SKIN`, `MDL` (27 nodes, the same skeleton), `MASK`.

| | Dreamcast | PS2 | Done |
|---|---|---|---|
| vertex chunks | Ninja type 41: 8-byte header, 24 bytes a vertex | type 51 (`njCnkCvVnPs2`): 64-byte header, 32 bytes a vertex (w 1.0); the skin conversion (`npSkinConvSub`) and the VU loops read only 51 | each list written again as 51, 64-byte aligned, after the model's data; the model's `vlist` points there |
| polygons, strips, materials | the same chunk types (8, 17, 19, 66) | | kept |
| skin `SKIN` | the same layout | | kept |
| face mask `MASK` | byte for byte the PS2's | | kept |
| textures | PVR in `TPVR` chunks (`GBIX`, `PVRT`): twiddled RGB565, VQ RGB565 and ARGB4444 | TIM2, 8-bit with a CLUT | decoded (twiddled, rectangle, VQ, small VQ; ARGB1555, RGB565, ARGB4444) and written as the game's TIM2 chunks (`imp_tex_chunk`), in the list's order (texture k fills `texlist[k]`), Gindex `0xD00000` + the GBIX |

How the DC model differs: the head node holds 19 vertices against the PS2's 83 and the model has 9 textures, not 10:
the PS2 re-made the hair (a texture `hair3` of its own); the Dreamcast's hair uses the body's textures. 1470 body
vertices on both; the skin's per-vertex words differ in 253 of 1519 places (weights).

The output is a small PS2-format room file, expanded (`<import>/dreamcast/STEVE_CINE/STEVE_CINE.rdx`, 0.5 MB): the
header, a room block with that one record, the model table, an empty motion table (type 93 has no motions of its own
in 0040 either), the texture table (the room model's entry empty, then Steve's list). The manifest's `player` is 3 ("in
the game's own format"); the registry gives the import the record's type and version (`dc_model`), and
`actor_glue.c` loads it as it loads a catalogue model from a disc room file (`load_room_model` with the file), so the
face mask is set up (`face_init`), the lips and lids are the record's (`face_step`), the cast's unarmed hands are
Steve's and the voice is Steve's (`appear_voice`, `voice_model_chr`, `cast.c model_char`: an import with a type counts
as that type's character).

### The player model

`SYSTEM.AFS` file 12 (as on the PS2: part file `ply_id + 10`, `dread.c bhReadPlayerData`), read from the disc's AFS
(`gd_file` "SYSTEM.AFS/12"). Its layout is the PS2's: the model block (`SKIN`, the body MLB on node 1, two texture-list
MLBs for the eye blink, the untextured 22-node hit model), the motions, the z translations, a texture block per textured
sub-model.

| | Dreamcast | PS2 | Done |
|---|---|---|---|
| body | 717 vertices (type 41), its own skin | 765 (type 51) | the lists written as 51, 64-byte aligned in the block as `sys->plmdlp` holds it, as the PS2's |
| hit model | type 34 lists | the same | kept |
| motions (105 slots), z translations | byte for byte the PS2's | | kept (so every motion, weapon, grab and death is the PS2 Steve's) |
| textures | 3 blocks of `TPVR` (5 textures, the blink's 04a/04b) | TIM2 | as the cutscene model's (`put_textures`), Gindex `0xD00000` + GBIX; the GBIXes the cutscene model shares (0x2C1, 0x2BE, 0x2BF) are the same pictures on the disc |

The output is `<import>/dreamcast/STEVE_PLAYER/STEVE_PLAYER.part` (1 MB), a PS2 part file. The game reads it in place of
`SYSTEM.AFS/12` with Cutscenes and Gameplay; "part file 16" (Steve's unused costume 1, an 11-byte placeholder on the
disc) is always it (the look, a co-op partner's Dreamcast Steve; not imported: 12). Weapon files, hands and voice stay
Steve's (`ply_id` 2).

### The event model

Type 43 is the player model as a room record (22 nodes, the same `SKIN`; on the PS2 too). The Dreamcast's differs from
the PS2's as the player model does, and from the Dreamcast player model in its materials (diffuse colours) and strips;
taken from `R0_1130.RDX` record 0 (the same in 1140, 3120, 3220, 4011, 4030, 4050) as the cutscene model is
(`STEVE_EVENT.rdx`, five textures, the player model's pictures).

## Other differences on the Dreamcast disc (not imported)

* Disc 1 holds the rooms of stages 0..5 (Claire's part); stages 6..9 are on disc 2. Disc 2 passes the product check
  but has no `R0_0040.RDX`: the model fails with "Cannot read R0_0040.RDX from the disc (disc 1 has it)".

## Not tested

GDI images (only a synthetic one in the test), CHDs from other chdman versions (map or track layout), multi-track GD
layouts with data after audio tracks, disc 2, PAL / Japanese discs (refused), the Windows build (built, not run). The
player model: checked headless with idle, walk, run, turn, stairs, the sub machine guns (fire, reload), the Gold Lugers,
a zombie's grab and bite, death, third person, as Claire's and Chris's look, a co-op partner with and without the
import; not every weapon's every motion (knife, other characters' weapons through BioRand), the Battle Game, a
real playthrough of his sections, the new menu's look picker preview.
