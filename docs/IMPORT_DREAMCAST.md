# Import: Code: Veronica on the Dreamcast

Steve's cutscene model of the original Dreamcast release (his first hairstyle, with the strand over his face) put
into the PS2 game's own formats, so the game draws, skins and animates it as its own model: his mouth moves with the
lines (`bhLipSet`, the face mask's mouth shapes) and his eyes blink (the mask's lid muscles 8 and 9) exactly as the PS2
Steve's do. Read from the user's own disc image at run time; nothing of the disc is in the repository.

Code: `pc/src/import/gdrom.c` (disc images), `pc/src/import/dc.c` (the conversion), `import.c` (source `dreamcast`,
`IMPORT_DREAMCAST`), `pc/glue/actor_glue.c` (`load_rdx_file`, `load_room_model`), `pc/glue/cast_glue.c` (`own_look`),
`pc/glue/import_menu.c`. Test: ctest `import` (LZMA, deflate, a synthetic GDI and CUE disc, PVR twiddled and VQ).

## Using it

IMPORT > CHARACTERS > Resident Evil Code: Veronica (Dreamcast) (new and legacy menu; F9 the same): choose the disc
image, START IMPORT (well under a second). The model is then **Steve (Dreamcast Hair)**, listed under the imports
("Dreamcast Models") everywhere a look is chosen: Character Model (any character's look, the co-op partner), the
cutscene cast's pools with Include Imported Models, *Custom*'s lists. "Show in Model Lists" hides it as any import.

**Dreamcast Steve in Cutscenes** (that page, ini `import_dreamcast_steve = 1`, default off): Steve's records
(types 93 and 43) are drawn as the Dreamcast model in every room where the cutscene cast leaves him as himself (cast
off, or a pick of his own look), from the next room load on. Not while Steve is played (his double is "Character
Model"'s: choose the model there). It applies with the source hidden too (it is asked for by name). Visual only;
in co-op each screen follows its own setting.

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

## Other differences on the Dreamcast disc (not imported)

* **Steve's player model** (`SYSTEM.AFS` file 12): his Dreamcast hair too (717 vertices against 765, its own skin).
  The same conversion would apply (the part file's layout, `dread.c`).
* Steve's event model (type 43, rooms 1130, 1140, 3120, 3220, 4011, 4030, 4050): probably the same hair; not compared.
  With Dreamcast Steve in Cutscenes its records are drawn as the cutscene model.
* Disc 1 holds the rooms of stages 0..5 (Claire's part); stages 6..9 are on disc 2. Disc 2 passes the product check
  but has no `R0_0040.RDX`: the model fails with "Cannot read R0_0040.RDX from the disc (disc 1 has it)".

## Not tested

GDI images (only a synthetic one in the test), CHDs from other chdman versions (map or track layout), multi-track GD
layouts with data after audio tracks, disc 2, PAL / Japanese discs (refused), the Windows build (built, not run).
