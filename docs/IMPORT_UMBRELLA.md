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
| 3. Faces | the blink (the game's own morph), a jaw made for the mouth; the monsters' own jaws, the Tyrant ETY0's lids |
| 4. Monsters and others (phase 3) | done: 35 zombies (EZ00..EZ21, EZ26..EZ38), Hunter EHT1, Licker ELK1, Nemesis ENS1, Tyrants ETY0, ETY5..7, Lisa Trevor ELT1, Eliminator EEL0, Ivy EIV1, an office worker OSIT1: "Monsters and Others" |
| 5. Skeleton review (phase 3) | done: the limbs posed to Code: Veronica's rest pose, the gun-grip hands, coat tails and skirts on the thighs, repeating textures cut into tiles |
| 6. Mouths (phase 4) | done: the lips part when the made jaw turns (the lower lip on the jaw, the upper on the head, by the slit between them), a dark mouth inside behind them, Kenneth talks too. **Import again**: files from before keep the old jaw (the chin moves, the lips stay closed) |

Headless: `RECVX_MODS=<out> RECVX_IMPORT=umbrella RECVX_IMPORT_ISO=<image>.rvz build/pc/recvx --save-dir <tmp>`
(exit status 0 = no failures; the players only, about 8 seconds; `RECVX_IMPORT_WHAT=5` with the monsters and others, 62
models in 13 seconds). `RECVX_IMPORT_MESHES=1` also writes per model
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
   layers (environment shells), the hands' half-open poses (`hand_l2`, `hand_r2`; the relaxed `hand_?0` and the gun grip
   `hand_?1` are both kept, see "Hand poses"), the knife and Samurai Edge of the model's weapon set (by their visibility
   bone), **weapons held in a hand**: an object wholly on one wrist, past it and flat (under 0.6 high): Ada's and Carlos's
   pistols, HUNK's gun, Billy's and Wesker's (2003) guns, Jill's stun gun, and **loose parts far out of the body**
   (objects or visibility bones named `Tongue`, `syo_`, `cube1`, `it_root` reaching 5 units past the joints' box: the
   Licker's tongue stretched 2.5 m forward, Lisa Trevor's chains, Nemesis's rocket launcher; by name, since the Tyrant
   ETY0's claw and Ivy's leaves reach as far and are the body). The colour map of a material: the first layer whose name
   is not an environment, sphere, specular, light or indirect (`_ind`) map (a name rule, as the reference extractor).
2. **Textures**: GX decoded to RGBA, resized to powers of two where needed (PKS1's 550x312 -> 512x256), then 8-bit
   indexed with a 32-bit CLUT (median cut to 256 colours, refined three times, alpha weighed double; the CLUT in CSM1
   order and the PS2's alpha range): the `.cvxm`'s texture path unchanged. 6..15 textures, 0.24..0.66 MB per model. What
   the game reads must fit the model buffer (1.5 MB): above 1 MB of texels the largest texture is halved until they fit
   (Nemesis: six of 512x512; the glTF keeps them whole).
3. **The glTF** of the whole model (`<KEY>.glb`: every bone, the source weights; scale 0.1 to metres) and the textures
   as PNG.
4. **Repeating textures** (`cut_tiles`): the monsters repeat (and mirror) textures, one strip of a zombie's coat over u
   -18..+9; the game clamps. A strip inside one tile per repeating axis moves into it (mirrored for an odd tile of a
   mirroring axis); one over several is cut: each triangle clipped at the tile edges (Sutherland-Hodgman), each piece
   in one tile, the new vertices' position, normal, weights and blink offset interpolated. Never clamped: no smeared
   edge texels, no texture stretched across a seam. Clamped textures with UVs outside 0..1 (the zombies EZ06, EZ13,
   EZ18, EZ20, EZ33, Nemesis's belt, the T-103's zipper) take Outbreak's round-9 rule in the writer: a strip mostly
   outside moves by whole tiles into the tile holding most of it (EZ33's shirt at v 1..2), the overhang clamped as the Wii
   shows it.
5. **The skeleton** (`reduce`): 20 roles (root, pelvis, spine, chest, neck, head, both arms clavicle / shoulder /
   elbow / wrist, both legs hip / knee / ankle) + the face (jaw, lids), from the family's bone names ("a|b": either; a
   name also matches with `0` or `_root_<n>` after it):

   | family | models | root | pelvis | spine..head | arms (right) | legs (right) | face |
   |---|---|---|---|---|---|---|---|
   | A `_00.._23` | Chris, Jill, Rebecca, Richard, Kenneth; zombies, Lisa, Ivy | N_ALL | _00 (+ hips _15) | _01.._04 | _05 _07 _08 _09 | _16 _17 _18 (+ toe _19) | jaw _24 (on the head) |
   | B `_NN_Sxx` | Ada, Carlos, HUNK, Wesker | N_ALL | _00 (+ hips _17) | _01.._04 | _05_S _07_S _08_S _10_S | _18_S _19_S _20_S | |
   | H numbered | Hunter | N_ALL | _00 (+ hips _21) | _01.._04 | _07 _09 _10 _11 | _22 _23 _24 | jaw _05 |
   | C1 Japanese | Billy; Tyrant ETY0 | trans / N_ALL | kosi | hara/kara mune kubi atama | kata jowan/jyowan kawan/hiji te | momo sune/hiza asikub/asi | jaw ago, lids mabu_l / mabu_r |
   | C1E | Eliminator | trans | hara | koshi mune kubi kao | kata jouwan kawan tekubi | momo sune asikubi | jaw ago |
   | C2 `L_` / `R_` | Licker, Nemesis, T-103 ETY5..7, office worker | locator2 | koshi | hara mune kubi kao | R_sako R_kata R_hiji R_te | R_momo R_hiza R_ashi | jaw ago / kuchi |

   A family is taken only if its bones hang from each other as the roles do (the forearm from the upper arm, the arm
   from the chest, the leg from the pelvis, the head from the neck): the Hunter's `_05` is its jaw, not a clavicle,
   so family A does not fit it, and the Eliminator's legs hang from `hara`. A face bone must hang from the head
   (the players' `_24` is a knee helper or Jill's chest: no jaw).
   Every other bone's weight goes to its nearest kept ancestor (toes to the ankle, fingers to the wrist, the hips'
   pivot, packs to the pelvis, Jill's skirt flaps and shoulder pads to the thigh / clavicle they hang from, family B's
   forearm twist `_09_S15` to the elbow); a bone hung from the root beside the body (the Licker's tongue chain) to the
   nearest joint. Except **driven bones**, split by how far they turn: an `.exp` `a b r radv` bone by 1/r to b's role
   and the rest to its parent's (the upper-arm twists `_06`/`_11`: half), family B's `_NN_D<a>_<b>_<pct>` bones by pct
   between bones a and b, C1's `kenko` half to the clavicle and half to the shoulder. **Coat tails and skirts** (bones
   below the pelvis that are not the legs: Wesker's 12 coat tails in PWS2, the office worker's skirt): a vertex's
   share on them goes to the thigh on its side as far as it hangs down (none at the hips, 0.6 from the knees down,
   between the legs half on each; Outbreak's `flaps_to_thighs`, softened). Weights are summed per role (never
   duplicated), the 4 largest kept, normalised. Right and left by x (the character's right at -x). Family B's
   clavicles start 0.11 from the chest's middle: moved to 0.2 (the rig takes a sideways child of the chest for an arm
   only from 0.15). Scale 1:1, turned 180 degrees about y (the writer, as Outbreak's).
6. **The rest pose** (`repose`): an imported model takes the player's world rotations unchanged (`actor_glue.c
   retarget`: it is skinned to its own rest pose), so a limb that lies otherwise than the game's at rest lies otherwise
   in every pose. UC's limbs are straight (arms level, legs plumb); Code: Veronica's player skeleton (Claire's offsets,
   which also drive the hidden copy that moves a man) has its upper arms 4 degrees down, its thighs 4 degrees in and 5
   back, its shins 6 back. Measured in game on the pose dumps (`RECVX_IMPORT_POSE`, Chris and Jill): every limb 4..6.5
   degrees off the player's, the feet 0.5 wider apart and 0.8 ahead. So the mesh is posed: upper arm, forearm and hand,
   thigh and shin turned onto the game's bone directions (the shortest turn: no twist), the feet moved along without
   turning (flat as before), by the vertices' weights (linear blend). After: every limb within 0.1 degrees, the ankles
   within 0.07. The spine keeps its own curve (the mesh is upright in both; only the joints' places differ). Monsters
   whose arms are not level at rest come out in the same pose.
7. **The jaw** (`make_jaw`; the players have none): a new node under the head, its pivot below the ears (0.75 of the
   way from the chin up to the eyes: the eyes from the blink morph, else half way up the head; the head bone's depth),
   on the face's meshes (a texture named `face` or `lip`, a material `*_lip` (Rebecca's lips on her jacket's texture)
   or `M_head` (Kenneth: no face texture, no blink), or a mesh of the blink). Of each vertex the head's share moves
   to it by how much it belongs to the lower jaw:
   * **The mouth's slit.** UC's mouths are open in the mesh: the lips' inner edges are a boundary loop, 0.01..0.5 mm
     apart on the closed mouths (on Rebecca's and Jill's lip meshes, on the face mesh of the others). The face's
     meshes welded by position (0.05 mm: Rebecca's and Jill's lips join the face there), the slit is the widest
     boundary loop 0.2..1 wide, at most a fifth as high, centred, in front of the head's pivot, between the chin and
     the eyes. It is walked edge by edge: Chris's and Wesker's pinch near the corners (a vertex on four boundary
     edges), where the walk keeps its direction and goes round the corner's little loop. Its two arcs between the
     corners (the left- and rightmost vertex) are the lips' edges.
   * **Upper or lower lip**: the arc a vertex reaches first over the mesh (breadth first from both arcs, not within
     the outer tenth of the width: Wesker's lips join beyond the slit's end); the lips' inner faces curl past each
     other (Carlos's lower edge lies above his upper one), so height alone cannot tell them. The upper lip is the side
     whose vertices lie higher (Ada's arcs are 0.1 mm apart). Lower lip, chin: 1; upper lip, nose: 0; a corner 0.5;
     beside the corners by height, the softer the farther out (cheeks); what lies behind the slit, not joined to
     the lips (teeth, a backing) 0; towards the ears down to 0 at the head's pivot (except near the mouth).
   * **Billy's lips are closed** (they share their line: no slit; the loop found is his teeth's): his line is in a
     table (`closed_mouths`, measured on the disc's model: its height and half width). Near it a vertex goes by the
     way it faces (the upper lip's inner face looks down, the lower's up: they lie 0.02 mm apart), what lies behind,
     not joined to the face's front, stays.
   * **The mouth's inside**: the lips' edges are the boundary already (no vertex shared), so the slit opens as the
     lower lip turns. Behind it a pocket: a ring of copies of the slit's vertices and one 0.2 L back (L: chin to eyes;
     pulled in to 0.8 of the width), quads between them and a fan closing the back, on a texture of its own (16x16
     dark red; the model's 7..16th); each with its lip's share (upper on the head, lower on the jaw, the corners and
     the back's middle half), so the walls follow the lips and the back opens with them. The teeth (Chris's, Rebecca's,
     Wesker's box, Billy's) stay on the head in front of it. A flat backing behind the lips off the face (Jill's quad,
     grey on PJL2's texture) is left out: the pocket takes its place. 17..61 vertices more (PCR2: 12226 of 12288).
   No slit (none of the 15): chin and lower lip below the mouth's line (a third of the way up), the lips stretch. The
   jaw turns half the game's angle (`.cvxm` header `jaw gain`): at the opening's loudest lines (10 degrees) the lips
   part by about 1 cm, Claire's own a little less; Talking Mouths / the double's lip sync and the player's voice as for
   Outbreak. PHK1 (a mask) gets none. **A jaw bone of the model's own** (the zombies' `_24`, the Hunter's `_05`, `ago` of the
   Licker, Nemesis, the Tyrants, the Eliminator; the office worker's mouth `kuchi`) becomes the jaw with what hangs from
   it, turned by half the game's angle (gain 0.5: the Licker gaped through every line at the full angle). **Lid bones** (the Tyrant ETY0's `mabu_l` / `mabu_r`) become the lids
   (`IMP_FACE_LID_L` / `_R`): they slide down by 2.5 times the height of what is on them (a thin flap; at most 0.29).
8. **Welding** (`imp_weld`): the display lists give a vertex per triangle corner (12000..24000); vertices of the
   same position, normal, weights and blink offset are merged, the UVs move into the strips (CsUvh has a UV per strip
   index): 5100..11200 vertices.
9. **The fit** (`uc_fit`, `fits[]`): the arms moved in by 0.8 of the difference to Claire's width-to-height
   proportion (Chris: 0.15 units), not lowered (UC's shoulders sit at the game's height: Chris 0.52 below the neck,
   CVX's Chris 0.56).
10. **The `.cvxm`** (`imp_write_cvxm`, version 5, 6 with hand poses): **UVs**: every strip in one tile (`cut_tiles`, or
   moved by whole tiles into the one holding most of it: the round-9 rule). **See-through** strips (hair, lashes: texels under 255 alpha) are drawn last, blended. **The blink** as a morph
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

### Hand poses (`.cvxm` version 6)

UC's hands come in three poses, each a mesh of its own shown by a visibility bone: `hand_r0` / `hand_l0` relaxed,
`hand_?1` the gun grip, `hand_?2` half open. The relaxed and the gun-grip hands are both kept; header 56 points to a byte
per vertex (`IMP_HAND_R_OPEN`, `_R_GRIP`, `_L_OPEN`, `_L_GRIP`, 0 the rest; `imp_weld` keeps the poses apart at the wrist's
seam). A side with only one of them (Kenneth's hands have no visibility bones) gets no poses. `actor_glue.c`: the right
hand grips while the player (or the partner) holds a weapon (`wpnr_no` > 0: the gun in the hand at all times, aimed or
not), the left one too while the model's wrists are under 2.5 apart (a long gun's fore-end, a two-handed aim); the
cutscene double and the cast keep their hands open. The pose not shown is shrunk to its wrist in `imp_skin`.
`RECVX_IMPORT_GRIP=0/1/3` holds them (test aid). Files with poses are version 6 (older builds refuse them: both hands
would show); files without them stay version 5, Outbreak's byte for byte.

## The source in the menus

`import_sources[]` "umbrella" (`IMPORT_UMBRELLA`, at the end: the hide bits): `offered` = playable characters and the
second group, here "Monsters and Others" (`IMPORT_GROUP_NPCS`; `uc_group`: `P*` players, `E*`/`O*` the others; the IMPORT
screen's row and its help say so for this source, heading "Umbrella Chronicles Monsters and Others"),
`ext` = `rvz` (the file dialogs' filter, F9 and the IMPORT screen's), its group headings ("Umbrella Chronicles
Characters"). IMPORT > CHARACTERS lists it in the new and the legacy screen (Playable Characters, Monsters and Others, Disc Image, START
IMPORT, DELETE, Show in Model Lists); F9 has its section ("Other Characters"). Names: Chris Redfield (S.T.A.R.S. / Umbrella's End), Jill
Valentine (S.T.A.R.S. / Raccoon City / Umbrella's End), Rebecca Chambers, Richard Aiken, Kenneth J. Sullivan, Billy
Coen, Carlos Oliveira, Ada Wong (Costume 1 / 2), HUNK, Albert Wesker (S.T.A.R.S. / Umbrella's End / S.T.A.R.S.,
Wounded); women move with Claire's motions. The others: Zombie (1..35, the file order), Hunter, Licker, Nemesis, Tyrant
(T-002 / T-103, 2, 3), Lisa Trevor, Eliminator, Ivy, Office Worker; Lisa and the office worker move with Claire's
motions, the others with a man's.

How they are used: as every import, "Character Model" (the player's look, the partner's), the cutscene cast's "Any
Model" / "Custom" pools with imports (`cast_pool`), and so the cutscene monsters while "Cutscene Monsters" is "Like
Story Characters" (`cast_model`: with "Enemy Models" the same as the cast, the creatures pick from the cast's pool). The
enemies' own list (cutscene monsters "Any Monster" / "Custom") takes no imports at all (`cast.c imps`), as for
Outbreak. "Show in Model Lists" hides all of them with the players.

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

Phase 3 (the skeleton review, the monsters; sheets outside the repository):

* The limbs measured on the pose dumps (`RECVX_IMPORT_POSE`, idle, walk, run, turn, aim, fire): before 3.9..6.5 degrees
  off the player's at every pose (the feet 0.5 wider apart, 0.8 ahead), after 0.0..0.1 (the ankles within 0.07); the
  left arm of a two-handed aim differs by its IK, as before. All 62 models load with the player's motions (the rig maps
  every role).
* `check.py` against the reference for all 62: the body meshes as the reference's (the extra gun-grip hands, the
  tongue, Lisa's chains and Nemesis's launcher left out on purpose), skin weights per position 100% the same, textures
  PSNR 35..52 dB (EZ00's 212x256 texture resized to 256: 25.6 against the unresized reference).
* In game, before (main's build and import) and after, each of the 16 players: gameplay at three orbits (idle, walk,
  run, turn on the stairs, aim, fire), the handgun, shotgun, sniper rifle (normal aim), bow gun and rocket launcher held
  and fired, a zombie's bite (grab, struggle) and the death; the coat, skirt and pack side by side in the walk.
  The monsters and others (15 of them): the same, and face close-ups (jaw at 14 degrees, lids). Cutscenes: Claire's
  double in the opening's talk as Chris, Jill, a zombie, the Licker, the Tyrant ETY0 and the office worker (mouths
  with the lines), Rodrigo as a random import (`RECVX_CAST=1:imports`: Lisa Trevor, Nemesis).
* Outbreak: its players and NPCs (306 models) imported by main's build and this one: all 1032 files identical; main's
  files shown by both builds: the same game frames (Kevin, Alyssa, Cindy, HN13).

Phase 4 (the mouths; sheets outside the repository):

* The slit found on 14 of the 15 faces (Billy: the table), its lips checked on front / side plots of the welded face
  (each vertex's lip). `check.py`: meshes, glTF weights, textures and blink as before; the `.cvxm`s 17..61 vertices
  more (PCR2 12226, under 12288), PKS1 with a jaw; the monsters' files byte for byte the same (Lisa Trevor's and
  HUNK's but for the unused gain); the hand poses as before.
* In game, before (main's build and import) and after, each of the 15: face close-ups with the jaw held at 0, 7 and 14
  degrees (`RECVX_IMPORT_JAW`), the lids shut with the jaw open; Claire's double in the opening's talk (all 15:
  before, the chin moved and the lips stayed shut; after, they part with the lines, about as far as Claire's own and
  a little less than two Outbreak imports). The zombies', Nemesis's, the Tyrant's and the office worker's own
  jaws as before.
* Outbreak: 1032 files identical. `layout_check.sh` (legacy menu) main vs this: 156 vanilla and 311 enhanced frames
  identical. ctest.

## Next steps

1. Eye direction: the eyeball bones of Ada, Wesker, Carlos, Richard and the office worker (`L_me` / `R_me`) could turn
   the eyes towards the camera or what the record looks at.
2. The Tyrant ETY0's lids slide down (UC turns its flaps): they close only partly.
3. Toes: merged into the ankles (the game's skeleton has toe nodes: walking would roll the foot).
4. The colour-map choice is a name rule; the TEV stages would decide it for sure.
5. The monsters move with a person's motions; the enemies' own list (cutscene monsters "Any Monster" / "Custom") takes
   no imports. Non-humanoids (dogs, crows, plants, spiders) export glTF only.
