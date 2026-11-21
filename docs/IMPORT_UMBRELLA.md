# Importing from Resident Evil: The Umbrella Chronicles

Nothing from any disc is in the repository: the importer reads the user's own disc image in place and writes what it
extracts to the user's mods folder (`mods/import/umbrella/`, or `import_dir`), as the Outbreak import does
([IMPORT_OUTBREAK.md](IMPORT_OUTBREAK.md): the `.cvxm` format, the registry, the F9 window and the IMPORT screen are
shared).

Goal: the playable characters of *Resident Evil: The Umbrella Chronicles* (Wii, US, RBUE08) as looks ("Character
Model") and cutscene cast models, moving with Claire's / Chris's motions, blinking and talking.

## PROGRESS

| Stage | State |
|---|---|
| 1. Format research | done (phase 1, outside the repository: a Python reference extractor of all 152 character models) |
| 2. Converter (C, `pc/src/import/rvz.c`, `uc.c`) | done for the 16 player models: PCR1, PCR2, PJL1..3, PRB1, PRC1, PKS1, PBI1, PCA1, PAD1, PAD2, PHK1, PWS1..3 |
| 3. Faces | the blink (the game's own morph), a jaw made for the mouth |
| 4. Monsters and NPCs (zombies, Hunter, Licker, Nemesis, T-103, Lisa, Eliminator, Ivy, OSIT1) | phase 3 ("Next steps") |

Headless: `RECVX_MODS=<out> RECVX_IMPORT=umbrella RECVX_IMPORT_ISO=<image>.rvz build/pc/recvx --save-dir <tmp>`
(exit status 0 = no failures; about 8 seconds for all 16). `RECVX_IMPORT_MESHES=1` also writes per model
`<KEY>.meshes.txt` (each object of the model: kept, left out and why; the textures by slot).

## Disc format

**RVZ** (Dolphin's compressed Wii image; `rvz.c`): a header (0x48 bytes), a second header (compression 5 = zstd or 0,
the chunk size: 128 KiB on this disc, the disc header's first 0x80 bytes with the id `RBUE08`, where the partition,
raw-data and group tables are), the raw-data and group tables (zstd-compressed), the groups. The partitions are stored
decrypted, without their hashes: a partition group holds chunk / 0x8000 sectors of 0x7C00 bytes, after a list of hash
exceptions (u16 count, 22 bytes each) that is skipped; a group may be packed (runs of data and of junk: the disc's
padding, given by a seed; read as zeros, no file lies in it). Of the two partitions (update, game) the game's: its
file system table at 0x424 / 0x428 of its data (offsets and sizes / 4). A plain Wii ISO is encrypted (AES with the
common key) and is not supported; other discs are refused by their id ("Use a US Resident Evil: The Umbrella Chronicles
disc image (RBUE08)."). zstd's decompressor (BSD) is built from its official sources (FetchContent, `zstd_dec`).

`Data/chara/<KEY>.arc` is a **U8** archive (magic `55AA382D`; 12-byte nodes): `<KEY>.brres`, `collision.col`,
`expression.exp` (a small RPN script for helper bones: `62 63 2.0 radv` turns bone 62 by half of bone 63's rotation).

**BRRES** (Nintendo NW4R; `uc.c`, our own reading of the layouts the Wii modding community documents): resource
dictionaries of 16-byte entries; `3DModels(NW4R)` holds one **MDL0** (version 9; PKS1's 8): 11 sections (definitions,
bones, positions, normals, colours, UVs, materials, shaders, objects, texture links, palette links).

* Bones (0xD0 bytes): index +0x0C, parent +0x5C (an offset from the bone), the world rest matrix 3x4 +0x70. A T-pose
  facing +z, the character's left at +x, 1 unit = 10 cm: the game's scale (Chris's pelvis 11.4, Claire's 11).
* Definitions (bytecode): 3 envelopes (matrix id, n, then n x (matrix id, f32 weight)), 4 draw calls (material,
  object, visibility bone), 2/5/6 the node tree. The matrix-id table (info + 0x24): bone index, -1 an envelope.
* Objects: the vertex descriptor (+0x0C, +0x10), the display list (+0x24 + offset at +0x2C, size +0x28), the array ids
  (+0x48: position, normal, colour 0/1, UV 0..7). The display list: 0x20 loads a matrix into a slot (address / 12),
  each vertex's matrix byte / 3 picks it; primitives 0x90 triangles, 0x98 strips, 0xA0 fans, 0x80 quads. A vertex on
  one bone is stored in that bone's frame, one on an envelope in model space (up to 4 weights).
* Materials, through the texture links: per layer the texture's name and the wrap modes (+0x18 / +0x1C: 0 clamp,
  1 repeat, 2 mirror).
* **TEX0**: GX textures (level 0 read): CMPR (DXT1 in 8x8 tiles, with its 3-colour + transparent mode: 1-bit alpha),
  IA8, RGB5A3, RGB565, I8, RGBA8, IA4, I4. No palettes on this disc.
* The blink (`AnmShp` "mepati", 4 frames 0 -> 1 -> 0): a second position array per face mesh that no object uses, the
  eyes closed. Paired with the array of the same length, the same material suffix first (as the reference extractor;
  the SHP0's vertex sets would name the pair for sure).

## Conversion (`uc.c`)

Per model, in this order:

1. **The meshes of the body**: every object with a draw call and a textured material. Left out: materials without
   layers (environment shells), the hands' other poses (`hand_l1`, `hand_r2`: gun grip, half open; variant 0 stays),
   the knife and Samurai Edge of the model's weapon set (by their visibility bone), and **weapons held in a hand**: an
   object wholly on one wrist, past it and flat (under 0.6 high): Ada's and Carlos's pistols, HUNK's gun,
   Billy's and Wesker's (2003) guns, Jill's stun gun. The colour map of a material: the first layer whose name is not an environment,
   sphere, specular, light or indirect (`_ind`) map (a name rule, as the reference extractor).
2. **Textures**: GX decoded to RGBA, resized to powers of two where needed (PKS1's 550x312 -> 512x256), then 8-bit
   indexed with a 32-bit CLUT (median cut to 256 colours, refined three times, alpha weighed double; the CLUT in CSM1
   order and the PS2's alpha range): the `.cvxm`'s texture path unchanged. 6..15 textures, 0.24..0.66 MB per model.
3. **The glTF** of the whole model (`<KEY>.glb`: every bone, the source weights; scale 0.1 to metres) and the textures
   as PNG.
4. **The skeleton** (`reduce`): 20 roles (root, pelvis, spine, chest, neck, head, both arms clavicle / shoulder /
   elbow / wrist, both legs hip / knee / ankle) + the jaw, from the family's bone names:

   | family | models | root | pelvis | spine..head | arms (right) | legs (right) |
   |---|---|---|---|---|---|---|
   | A `_00.._23` | Chris, Jill, Rebecca, Richard, Kenneth | N_ALL | _00 (+ hips _15) | _01.._04 | _05 _07 _08 _09 | _16 _17 _18 (+ toe _19) |
   | B `_NN_Sxx` | Ada, Carlos, HUNK, Wesker | N_ALL | _00 (+ hips _17) | _01.._04 | _05_S _07_S _08_S _10_S | _18_S _19_S _20_S |
   | C1 Japanese | Billy | trans | kosi | hara mune kubi atama | kata jowan kawan te | momo sune asikub |

   Every other bone's weight goes to its nearest kept ancestor (toes to the ankle, fingers to the wrist, the hips'
   pivot, coat tails and packs to the pelvis, Jill's skirt flaps and shoulder pads to the thigh / clavicle they hang
   from, family B's forearm twist `_09_S15` to the elbow), except **driven bones**, split by how far they turn: an
   `.exp` `a b r radv` bone by 1/r to b's role and the rest to its parent's (the upper-arm twists `_06`/`_11`: half),
   family B's `_NN_D<a>_<b>_<pct>` bones by pct between bones a and b, Billy's `kenko` half to the clavicle and half
   to the shoulder. Weights are summed per role (never duplicated), the 4 largest kept, normalised. Right and left by
   x (the character's right at -x). Family B's clavicles start 0.11 from the chest's middle: moved to 0.2 (the rig
   takes a sideways child of the chest for an arm only from 0.15). Scale 1:1, turned 180 degrees about y (the
   writer, as Outbreak's).
5. **The jaw** (`make_jaw`; the players have none): on the face's meshes (a texture named `face` or `lip`, or a mesh
   of the blink), the head's share of the vertices below the mouth's line (a third of the way from the chin up to the
   eyes: the eyes from the blink morph) and in front of the head's pivot moves to a new node under the head, at 0.75 of
   that way, the head bone's depth: chin and lower lip, softly at the edges. The lips are closed meshes (no mouth
   inside: Rebecca's teeth show a little): they stretch. So the jaw turns 0.6 of the game's angle (`.cvxm` header
   `jaw gain`): Talking Mouths / the double's lip sync and the player's voice as for Outbreak. PKS1 (no face animation)
   and PHK1 (a mask) get none.
6. **Welding** (`imp_weld`): the display lists give a vertex per triangle corner (12000..24000); vertices of the
   same position, normal, weights and blink offset are merged, the UVs move into the strips (CsUvh has a UV per strip
   index): 5100..11200 vertices.
7. **The fit** (`uc_fit`, `fits[]`): the arms moved in by 0.8 of the difference to Claire's width-to-height
   proportion (Chris: 0.15 units), not lowered (UC's shoulders sit at the game's height: Chris 0.52 below the neck,
   CVX's Chris 0.56).
8. **The `.cvxm`** (`imp_write_cvxm`, version 5): **UVs**: a strip on a clamped texture keeps its UVs (the game
   clamps too); on a repeating one it moves by whole tiles (the round-9 rule: into the tile holding most of it).
   **See-through** strips (hair, lashes: texels under 255 alpha) are drawn last, blended. **The blink** as a morph
   block: per moving vertex its offset with the eyes closed (56..271 vertices).

### `.cvxm` version 5 and the vertex limit

* Header 44: morph block offset, 48: entries (u32 vertex, float d[3]; by vertex), 52: jaw gain (0 = 1). Versions
  1..4 load as before (`pc_cvxm_check`; Outbreak's files differ from version 4 only in the version byte).
* `actor_glue.c`: `imp_face` keeps its lid value (the player blink, or the record's eyelids for the double and the
  cast: muscles 8/9) in `d->shut`; `imp_skin` adds `shut` x the offset to each morph vertex's bind position before
  skinning. The jaw angle times the file's gain.
* **Vertices**: the vertex work buffer (`njInit3D`, 64 bytes an entry) holds 12288 entries now (`pc_prelude.h
  PC_VERTEX_ENTRIES`, decomp `main.c`; was 8192): Carlos 9706, Jill (2003) 10795, Chris (2003) 11141. Above 8189 a
  vertex chunk's size word cannot hold its size: it is written as 0xFFFF (`njCnkCvVnPs2` reads the count). 16384 would
  push the image's bss past 0x40000000 (the address window). The game's own models stay below 8192.
* Memory: what the game reads is 0.55..0.9 MB a model (`KEEP_SIZE` 1.5 MB; the cast's 3 MB fit three).

## The source in the menus

`import_sources[]` "umbrella" (`IMPORT_UMBRELLA`, at the end: the hide bits): `offered` = playable characters only,
`ext` = `rvz` (the file dialogs' filter, F9 and the IMPORT screen's), its group headings ("Umbrella Chronicles
Characters"). IMPORT > CHARACTERS lists it in the new and the legacy screen (Playable Characters, Disc Image, START
IMPORT, DELETE, Show in Model Lists); F9 has its section. Names: Chris Redfield (S.T.A.R.S. / Umbrella's End), Jill
Valentine (S.T.A.R.S. / Raccoon City / Umbrella's End), Rebecca Chambers, Richard Aiken, Kenneth J. Sullivan, Billy
Coen, Carlos Oliveira, Ada Wong (Costume 1 / 2), HUNK, Albert Wesker (S.T.A.R.S. / Umbrella's End / S.T.A.R.S.,
Wounded); women move with Claire's motions.

## Verification

* Against the phase-1 Python reference (outside the repository, `check.py`): per model every reference body mesh
  with the same vertex count (16/16, 0 mismatches; the held weapons left out on purpose), skin weights per vertex
  position in the `.glb` 99.94..100% the same (4 of Rebecca's 6662 differ), textures: mean error 0.08..0.52 of 255,
  PSNR 43..52 dB after the 256-colour quantising, alpha within 1; the blink: the same vertices and largest offset.
* In game (headless, Linux): each of the 16 as Claire's look in third person, front / side / back: idle, walk, run,
  turn, aim, fire; face close-ups open, lids held closed (`RECVX_IMPORT_BLINK=1`), jaw held open
  (`RECVX_IMPORT_JAW=14`), back of the head; as Claire's double in the opening's talking scene (six of them: the lips
  move with the lines, the lids follow the record's); Rodrigo as an imported model in the cast (`RECVX_CAST=1:imports`).
* Outbreak: its 30 player files imported by main's build and this one identical but for the version byte; main's
  files shown by both builds: the same frames (Kevin, Alyssa, Cindy).
* `layout_check.sh` (legacy menu) main vs this: 156 vanilla and 311 enhanced frames identical. ctest.

## Next steps (phase 3)

1. **Monsters and NPCs**: family C2 (`L_`/`R_` Japanese names: Licker, Nemesis ENS1, T-103, Talos, OSIT1) needs its
   role table (locator2, koshi, hara, mune, kubi, kao, sako / kata / hiji / te, momo / hiza / ashi); the zombies and
   Hunter are family A. Their real jaw bones (`_24`, `ago`) map straight to the jaw; OSIT1 has lids and brows. Many
   monsters repeat their textures with UVs from -18 to +9 in one strip and mirror: a strip spanning more than one tile
   needs the texture tiled into a larger one (or the strip cut at the tile edges); mirror is taken as repeat now.
   Non-humanoids (dogs, crows, plants) export glTF only.
2. **A skeleton review**: rest pose against CVX's (UC's arms are level, CVX's motions start from the same T-pose); the
   hands' gun-grip variant (`hand_r1`) could replace the open right hand while aiming; the eyeball bones (Ada, Wesker,
   Carlos, Richard) could turn the eyes; Wesker's coat tails and Jill's skirt follow the pelvis / thighs rigidly.
3. The colour-map choice is a name rule; the TEV stages would decide it for sure.
