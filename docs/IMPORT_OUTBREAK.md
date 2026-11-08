# Importing from Resident Evil Outbreak File #2

Nothing from any disc is in the repository: the importer reads the user's own image in place and
writes what it extracts to the user's mods folder (`mods/import/outbreak2/`, or `import_dir`).

Goal: take the character models of *Resident Evil Outbreak File #2* (US, SLUS-20984) and use them
as player models in this port ("Character Model", [ACTORS.md](ACTORS.md#you-appear-as)); later
voices and the social-call ("ad-lib") animations for co-op. The title menu's EXTRA > IMPORT screen and the F9 window
run the extraction; F9 also lists what was extracted. Both are built so
other games can be added as further sources.

## PROGRESS

| Stage | State |
|---|---|
| 1. Format research | done: disc layout, archives, compression, model and texture format ("Disc format") |
| 2. Extractor (C, `pc/src/import/`) | done: every character model on the disc (537: 30 playable, 40 cutscene / high detail, 276 NPCs, 191 enemies) to glTF + PNG |
| 3. Conversion to CVX player models | done for every model on the human skeleton (324: all 30 playable, 39 of 40 cutscene / high detail, 238 of 276 NPCs, 17 enemies): `.cvxm`, listed in "Character Model", skinned every frame, retargeted from the player's motions; checked in game (walk, aim, grabs, third person, cutscenes) |
| 4. F9 window | done: disc image, what to extract (four groups), progress, results grouped (playable, cutscene / high detail, NPCs, enemies) from the manifest (also after a restart) |
| 5. Notes for later (voices, ad-libs, animations) | first findings ("Notes for later") |
| 6. Verification | headless extraction, glTF/`.cvxm` checks and previews, in-game shots, ctest (`import`, `appear`), Linux and Windows builds, layout check |
| 7. Posture (round 2) | fixed: the retargeting's rest correction bent the imported meshes (hunched shoulders, head down); imported models now take the source's rotations as they are; the left arm's two-handed grip scaled to the model's shoulders, the palm onto the gun ("Posture") |
| 8. Faces (round 2) | blinking (lids, the player characters' timing) and the jaw from the cutscene double's lip sync ("Faces") |
| 9. Eyes (round 3) | fixed: white, "blind" eyes: the eyeball overlay mesh (white without Outbreak's UV scrolling) is left out, the face texture's painted irises show, in the `.glb` too ("Faces") |
| 10. Mouth (round 3) | the jaw arrived but turned only 0..5 degrees (the cast's masks talk mostly with lip muscles): amplified, checked in the opening's talking scene; in gameplay the jaw follows the loudness of the player's voice ("Faces") |
| 11. Shoulders (round 3) | narrowed towards Code: Veronica's proportions and lowered at conversion (a fit per model: the arms moved with their weighted vertices, "Adjusting parts") |
| 12. Grip (round 3) | the support hand under the gun again: below the gun hand, fingers along the gun, compared with Claire's and Chris's grips ("Posture") |
| 13. Blink (round 4) | fixed: the whole forehead moved with the lids; the upper lid's weights are now set from the height (the lid only) and it closes by its own measured opening ("Faces") |
| 14. Grip (round 4) | retuned: the support hand hung low and its fingers reached far past the grip; now right under the gun hand on the centre line, for all 8 characters ("Posture") |
| 15. Lobby list (round 4) | main's MODEL page lists all imports, grouped with headers; scroll bar click; checked with 384 entries ("F9 window") |
| 16. Weapon hands (round 5) | the weapons' hand pieces (Claire's / Chris's) hidden on imported models; their own hands hold the gun ("Posture") |
| 17. Groups offered (round 6) | for now only the playable characters and the NPCs: F9 offers, extracts and lists only those; "Character Model" leaves out the other groups of an older manifest (`IMPORT_OFFERED`) |
| 18. Left arm in idle and holds (round 6) | fixed: men's left arm pulled in across the thigh in idle, and the support hand pulled under the gun hand in long-gun holds ("Posture") |
| 19. IMPORT screen | the in-game version of F9: title menu EXTRA > IMPORT > CHARACTERS > the game; the groups, the disc image, START IMPORT with progress, the counts, DELETE IMPORTED MODELS ("IMPORT screen") |
| 20. Blink, hair (round 7) | fixed: only the brows moved (the crease bone was taken for the lid); hair see-through, too bright in streaks, with holes (Outbreak's alpha is 0..255, read as the PS2's 0..128); some NPCs lost hair pieces, visors and lenses (taken for the eyeball overlay). `.cvxm` version 4; older files get the alpha fixed when loaded, blink after importing again ("Round 7") |
| 21. High-detail models (round 8) | each character's first costume is converted from its high-detail model (`HP<c>`: 512x512 textures, 5400..6900 vertices, real eyeballs, its hair locks) instead of the in-game one, in gameplay, cutscenes and co-op alike; blink and jaw on its own face rig; the other 22 costumes have no such model and stay. Same `.cvxm` version; importing again brings them ("Round 8") |

Resume notes: build with `cmake --build build`; headless run:
`RECVX_MODS=<out> RECVX_IMPORT=outbreak2 RECVX_IMPORT_ISO=<image> build/pc/recvx --save-dir <tmp>`
(`RECVX_IMPORT_WHAT` = 1 playable, 4 NPCs, summed (5: both; the bits 2 cutscene / high detail and
8 enemies are ignored while they are not offered, `IMPORT_OFFERED`); exit status 0 = no failures).
Check the output with `pc/tools/import_check.py <model>.glb|.cvxm out.png`. In game:
`RECVX_MODS=<out> RECVX_APPEAR=30:68` shows the first imported model (chr `COOP_CHR_IMPORT` = 68).
Test aids: `RECVX_IMPORT_POSE=file` (per pose of slot 0 the node matrices in the base's frame,
`file.src` the source's joints; the double's jaw angle and the voice jaw are logged with the frame),
`RECVX_IMPORT_JAW=deg` (the jaw held open). A talking scene headless: a new game (no teleport),
cross at 1700 (the cell's message, the inventory opens), up, cross, cross (the lighter equipped),
start at 1950: Rodrigo's scene in RM_0000, Claire talks around frames 3260..3340. A voice:
`RECVX_TELEPORT=1:0:2:0 RECVX_SPAWN=300:1@6` (a zombie next to the player in RM_0020; it bites
from frame ~660). Posture and face
previews were rendered offline from such dumps (exploration scripts outside the repository).

### Next steps

1. Faces: eye direction (Outbreak's eyeball mesh with its UV scrolling, left out now) and
   expressions are not done; the cutscene voice lines (ADX streams) do not move the jaw of a
   player model that acts itself (only a double's lip sync and the player's voice sounds do).
2. Fingers are merged into the hands (OB's fist / open-hand poses are lost); the weapon grip
   could close them.
3. The high-detail models (`HP*.nbd`) serve the first costumes ("Round 8"); their fingers (five
   jointed ones) and the rest of their face rig (brows, lower lids, lip corners, the eyeballs' turn)
   still follow the hand and the head.
4. Co-op: the partner sees the played character instead of an imported model (the pose's `chr`
   has no room for it and the partner may not have the file); a "model hash" in the pose and a
   fallback would allow it.
5. NPC names and sexes are not known (listed as "Character <file>", moving with a man's motions).
6. Voices and ad-lib motions (below).

## Disc format

ISO 9660, 4.7 GB. Root files:

| File | Content |
|---|---|
| `SLUS_209.84` | the executable |
| `NETBIO00.DAT` (1.0 GB) | AFS: `romdata_usa.afs` (common data, 1087 files) and per scenario `rNNN.afs` + `rNNN_h.bin` (r006, r010, r015, r020..r030, r040, rfff) |
| `NETBIO01.DAT` (1.4 GB) | AFS: `adxsnd_usa.afs` (1853 CRI ADX streams), `bgm.afs` (96 ADX music tracks, named), `pl00_vo.afs` (997 ADX voice lines, 680 of them `dmy.adx` placeholders) |
| `BIN/01.DAT` .. `05.DAT` | AFS of CRI Sofdec movies (`.sfd`) |
| `BIN/0.DAT` .. `9.DAT` | `MWo3` overlays (program modules) |
| `0FLIST.DIR` | list of the big archives |

**AFS**: `"AFS\0"`, file count n, n x (offset, size), then (offset, size) of a name table of n
48-byte records (32-byte name, date, size). Offsets are relative to the archive; archives nest.

**romdata_usa.afs** (names as on the disc; NBD = model):

| Files | What |
|---|---|
| `P<cc>_<vv>.nbd` | the 8 playable characters (cc 00..07: Kevin Ryman, Mark Wilkins, Jim Chapman, George Hamilton, David King, Alyssa Ashcroft, Yoko Suzuki, Cindy Lennox), costumes vv 00..02 (Alyssa, Yoko, Cindy also 03, 04): 30 files |
| `EP<cc>_<vv>.nbd` | the same characters' event (cutscene) models |
| `HP<cc>.nbd` | high-detail player models, one per character: its first costume (512x512 textures, a face rig of its own, most with a second model pair: hair locks; "Round 8"). A second `HP03.NBD` among the NPCs' files is another model (one texture, 56 nodes) |
| `N*.NBD`, `HN*.NBD` | NPCs (normal / high detail); `*_N.NBD` parts |
| `E<tt>_<vv>.NBD`, `HE*.NBD` | enemies; `EF01.NBD` effects |
| `pl<cc>_pc_000.bin`, `pc_<nnn>.bin` | motion data (see "Animations") |
| `e<tt>_<nnn>.bin`, `ep<tt>_tbl.bin`, `rn<nn>_tbl.bin` | enemy motions, enemy-player tables (grabs), room tables |
| `pl<cc>_voi.snd`, `pl<cc>_npc.snd` | per character sound banks (see "Voices") |
| `*.snd`, `*.snp` | other sound banks (common, lobby, weapons, enemies) |
| `*.MAT`, `*.mat`, `SHADOW_*.SDT`, `*.tm2` | material tables, shadows, 2D textures |

The scenario archives (`rNNN.afs`) hold the rooms (`r*.nbd`, `r*.rdt`, `*.sld` textures),
objects (`OBJ*.NBD`), enemy sets (`e*.bin`), and per character (A..H) `dm*<c>.Bin` files.

### NBD (model)

Little endian. A table of 16-byte entries at 0 (`type[4], offset, size, count`), as long as the
first entry's offset (0x40 or 0x80):

| Entry | Content |
|---|---|
| `TEX` | `count` textures: u32 size + SLD-compressed TIM2 |
| `AMO` / `AHI` | the model and its node hierarchy (an optional second pair: an add-on model, e.g. the HP heads) |
| `SDW` | shadow data |
| `TID` | (room models) texture ids instead of `TEX` |

**SLD** (texture compression): 16-bit words; a flag word gives the kind of the next 16 items
(highest bit first): 0 = a literal word, 1 = a copy word `c`: count `c >> 11` (0: the count is the
next word), distance back `c & 0x7ff` words (0: that many zero words); count 0 and distance 0 ends.

**Chunks** (AMO and AHI): `u32 type, u32 count, u32 size` (size includes the 12-byte header).

* AMO `1` model: `0x20000` date, `2` mesh list, `9` material list, `0xA` texture list.
* Mesh `4`: `5` strip lists (up to 2: `0x30000` single-weight vertices, `0x40000` blended; each
  strip `u32 n` (bit 31 a flag) + n u32 indices), `0x50000` material refs, `0x60000` material per
  strip (index into the refs, over both lists in order), `0x70000` positions (float3),
  `0x80000` normals, `0xA0000` UVs (float2; values above 1 repeat the texture), `0xB0000`
  colours (float4, 0..255), `0xC0000` weights (per vertex `u32 k` + k x (joint, weight %)),
  `0x100000` joints (node ids the weights index), `0xF0000` render attributes.
* Material (`0x120000`..`0x150000`: constant, Lambert, Phong, vertex colours): ambient, diffuse,
  specular (float4 each), specular decay, texture count, 200 bytes, texture indices into the
  texture list. Texture list entry (`0`): id = TEX slot, width, height, tiling.
* AHI `0xC0000000`: a root list (`0x80000000`: root ids), then per node a `0x40000001` (node),
  `0x40000002` (mesh node) or `0x40000003` (joint) chunk: id, parent, child, next, scale[4],
  rotation[4] (Euler x, y, z in radians, w unused; stored negated for joints), translation[4],
  mesh id, group.
* Positions of skinned meshes are in model space, in the bind pose: a T-pose with almost all
  node rotations 0. Units are centimetre-like (Kevin is 173 high); y up, the character faces +z.

**TIM2** textures: standard PS2 TIM2, 8-bit indexed 256x256 with a 32-bit CLUT (CSM1: entries
8..15 and 16..23 of every 32 swapped), alpha 0..255 (0xFF opaque: every texture's largest value; not
the PS2's usual 0x80, which Code: Veronica's textures use: the reader scales it, `imp_tim2_alpha_255`). Both header alignments occur (format
id 0: 16-byte file header; 1: 128 bytes, e.g. most third costumes). The GS registers in the
picture header are the same as Code: Veronica's for the same size and depth.

**The human skeleton** (147 models share it: all P, EP, most N): 36 nodes (Cindy's main costume
43, an extra hair chain). 0 root, 1 (pivot), 2 hips (y 110), 3 hip pivot, 4..6 and 7..9 legs
(thigh, knee, ankle), 10 spine, 11 chest, 12..15 and 16..19 arms (clavicle, shoulder, elbow,
wrist), 20 neck, 21 head, 22 face (23..26 eyes/lids, 27 mouth), 28..31 and 32..35 fingers.
Groups: 0 lower body, 1 upper body, 2 face, 3/4 hands. A player model has 3 or 4 meshes (body,
eyes, hands, head) and 2 textures (head and body), 1900..3600 vertices.

Sources: format names and chunk ids follow the public notes of the Outbreak modding community,
chiefly Fothsid's [libthanatos](https://github.com/Fothsid/libthanatos) and
[OBTool](https://github.com/Fothsid/OBTool) (both GPL-3.0: read as documentation, **no code
copied**; ours is MIT and written from the data), the Xentax/zenhax and residentevil123 forum
threads on NBD textures, and the [Outbreak decomp](https://github.com/GalaxyNostalgia/Outbreak-decomp)
(not used yet: the best source for the motion and sound formats).

## Extraction (`pc/src/import/`)

| File | Role |
|---|---|
| `import.h`, `import.c` | the sources table, extraction thread and progress, manifest, the registry for "Character Model" |
| `ob2.c` | Outbreak File #2: ISO 9660 root lookup, AFS, SLD, NBD reading into an `ImpModel` |
| `impmodel.h`, `impmodel.c` | glTF 2.0 binary writer (skeleton, skin, PNG textures), TIM2 decoding, the `.cvxm` converter |

What is read: all character models are in `romdata_usa.afs` (`ob2_group`): `P*` the playable
characters (30), `EP*` their cutscene versions and `HP*` the high-detail ones (40, named after the
cast: "Cutscene Costume n", "High Detail"), `N*` / `HN*` NPCs (276, by file name), `E*` / `HE*`
enemies (191). The scenario archives `rNNN.afs` hold rooms (`r*.nbd`), objects and effects only:
no character models (checked: every `.nbd` in them is a room, an object or an effect).

Offered for now: the playable characters and the NPCs only (`import.h IMPORT_OFFERED`). F9 shows
only their checkboxes and table groups, `import_run` extracts only them and `import_registry_load`
lists only them, also from the manifest of an older run that holds the other groups. The code for
the cutscene / high-detail versions and the enemies stays (the counts below are from a run with all
four groups); `IMPORT_OFFERED = IMPORT_ALL` brings them back.

Output per model in `<dir>/outbreak2/<id>/`: `<id>.glb` (viewable in Blender: one skinned mesh, a
primitive per texture, the full skeleton, scale 0.01 to metres, textures embedded),
`<id>_tex<k>.png`, and for every model on the human skeleton `<id>.cvxm`. `<dir>/outbreak2/manifest.txt`
lists every model (counts, whether it is a player model, failures with their reason); F9 shows
it, also after a restart, and the registry reads it at start. The disc is identified by
`SYSTEM.CNF` (`SLUS_209.84`); another region's disc is refused. All 30 player models: 23 MB of
output, a few seconds; everything (537 models): 490 MB, about a minute.

Converted: every whole body on the human skeleton (clavicles on the chest, shoulders below them,
the head on the neck), and the people on skeletons of their own (`N08`'s 24 nodes, `HN_70`'s 74;
they convert by the body groups as well). Not converted (glTF only, "Exported only: incompatible body
structure (n parts)." in the manifest): 174 enemies (zombies of the 23-node skeleton,
dogs, creatures; `HE01_10` and `HE01_56`, severed body parts on the human skeleton: a model has to
be at least a metre tall), and 39 hair and face parts of the high-detail models (`HN*_N`,
`HP03_N`; an earlier version converted 29 of them into floating hair). The 17 enemies on the human
skeleton (the high-detail zombies `HE01_*`, `E28`, `E45`) move with a man's motions.

Settings (recvx.ini): `import_outbreak2_iso` (the disc image), `import_dir` (default
`<mods>/import`, `RECVX_MODS` respected). Environment: `RECVX_IMPORT=outbreak2` (headless, exits),
`RECVX_IMPORT_ISO`, `RECVX_IMPORT_WHAT`, `RECVX_SHOW_OVERLAY=4` (F9 open at start).

## Conversion

A player model of this port is a Ninja chunk model whose skeleton the "Character Model" retargeting
can map ([ACTORS.md](ACTORS.md#retargeting)). `imp_write_cvxm` (impmodel.c) builds one:

* **Skeleton**: the nodes of the lower and upper body groups that bend (offset not zero) are
  kept: root, hips (node 1, the pelvis), spine, chest, neck, head, both arms (clavicle, shoulder,
  elbow, wrist), both legs (hip, knee, ankle) = 20 nodes, plus the five face bones (four lids, the
  jaw; "Faces") = 25, in pre-order with children by index. Fingers, the face root and the
  zero-length pivots go to their nearest kept ancestor (weights included).
  All bind angles are 0 (a T-pose), as `rig_build` wants; the model is turned 180 degrees about y
  (Outbreak faces +z, Code: Veronica -z: a rotation, no mirroring) and scaled by 0.1 (the hips at
  11 units, as Claire's). `rig_build` finds the full map (pelvis, spine, chest, neck, head, 2 x 4
  arm nodes, 2 x 3 leg nodes; no toes).
* **Mesh**: all of the model's meshes as one mesh on the pelvis: a vertex chunk 51 (`CvVnPs2`:
  64-byte header, position and normal per vertex, w = 1) and per texture a material chunk, a
  texture chunk and strip chunks 66 (`CsUvh`: index, u, v with 1023 = 1.0; double-sided). UVs
  above 1 are moved back by whole tiles per strip (Code: Veronica clamps u). Strips that show
  see-through texels (fringes, lashes) are drawn last and alpha-blended, else they hide the face.
* **Skin**: Code: Veronica's own skin (`SKIN`, `npCalcSkin`) blends a vertex between a node and
  its parent's rotation only; Outbreak's are real weights (up to 3 joints). So the `.cvxm` carries
  per vertex the bind position, normal and 4 (node, weight), and `actor_glue.c imp_skin` does
  linear blend skinning after every pose (`actor_pose`), into the pelvis's frame (up to 3600
  vertices x 4 weights per pose).
* **Textures**: the TIM2 pictures as the game's texture chunks (32-byte chunk header, the 0x80
  extended file header with `Gindex` = `0xF00000 + 16 * disc index + k`, picture format 3: loaded
  without a palette list); the chunks follow each other without padding. The CLUTs' alpha is in the
  PS2 range (0x80 opaque) since version 4 ("Round 7").
* **In game** (`actor_glue.c load_import_model`): the file is read and checked in host memory; what the
  game reads (up to the texture block's end mark, `PcCvxm.end`) is copied into the display slot's buffer
  (inside the PS2 address window), the skin stays in the host copy ("Round 8"); realized like a room model, its textures uploaded (refused with too little texture memory, as
  the catalogue's). Actor index `ACTOR_MODELS + k`, chr `COOP_CHR_IMPORT + k`; listed after the
  cutscene models; women move with Claire's motions, men with Chris's (gender-matched animations),
  and speak with Claire's / Chris's voice.

Checked in game: walking, running, the handgun aimed and fired (the gun in the model's hand),
zombie grabs, third person; front and back views (`RECVX_TPS_ORBIT`).

Limits: no finger poses or expressions; costume pieces that hang (Cindy's ponytail chain,
coats) follow their body node rigidly; skirts tear open a little in long strides (Outbreak's own
weights put the skirt on the legs); the feet sink up to ~0.5 units (5 cm) into the floor at the
toe-off of the walk (the model has no toe bone; Claire's foot pitch is copied to its ankle); the
co-op partner sees the played character.

### Adjusting parts

Not only bones: the converter writes every vertex, so parts can be adjusted while converting.
`ImpFit` (impmodel.h) moves a source node and everything below it by an offset, and the vertices
with it by their weights (a vertex half on the moved node moves half way: the seam to the body
stays smooth). Other adjustments of the same kind (per-vertex offsets or scaling of a part, a
re-weighting, a bone's length) fit at the same place; they are not needed so far.

The shoulders: Outbreak's sit wider and higher than Code: Veronica's (Kevin's shoulder joints 1.76
out at a head height of 15.5, Claire's 1.4 at 15.05; padded jackets and vests, short necks).
`ob2.c ob2_fit` moves both arms (from the clavicles, nodes 12 / 16) in by a part of the difference
to Claire's width-to-height proportion and down; the table `fits[]` holds the defaults (0.8 of the
difference, 0.3 units = 3 cm down) and per model overrides (by file name prefix: Alyssa's padded
jackets 0.9 / 0.35). Kevin's shoulder joints: 1.51 out (Chris 1.48 at his height), 0.68 below the
neck (Chris 0.56).

### Posture

The first version looked hunched: shoulders pulled up, the head bent forward and down, sleeves
crushed, arms tucked in. Cause: the retargeting (`actor_glue.c retarget`) turns every mapped bone
by a rest correction, the rotation taking the model's rest bone onto the source's (the player's),
so that a catalogue model whose arms rest lower or more forward bends at the same joints. An
imported model is skinned to its own T-pose, which is the player's rest pose too, and its bones
are placed differently: Claire's clavicle bone runs from the sternum up to the shoulder joint,
Outbreak's starts at the shoulder and runs sideways, so the correction turned Outbreak's whole
shoulder region up by ~60 degrees; Claire's neck-to-head bone leans, Outbreak's is upright, so the
head went forward. The skinned mesh follows these rotations even at rest. Fix: an imported model
(`d->skin`) takes the source's world rotations unchanged (the delta from the common T-pose);
positions still come from its own bone offsets, the root height and feet adjustment and the
two-hand IK as before. Checked for all 30 models in idle, walk, run, aim and fire, front and side
(offline renders of the dumped poses), and in game.

**The left arm** (second report: broken and clipping in every motion set; in the two-handed aim
the left hand reached past the gun to the right). Cause: the two-handed grip (`retarget`,
`arm_ik`) solves the left arm to "the model's right hand + the source's left-to-right hand
offset", weighted fully below 3 units apart and faded out at 6. The source's hands hang 3.5 apart
with the arms down, so the solve also runs in idle and walk; Outbreak's shoulders are about a
quarter wider than Claire's/Chris's (shoulder joints 3.5 apart against 2.8), so the unscaled offset
pulled only the left hand in across the hips. In the aim the left wrist was put on the right
wrist, and Outbreak's hand has straight fingers (Code: Veronica's grip pieces from the weapon file
curl around the gun), so the left fingers stuck out past the gun. Fix (imported models only): the
offset is scaled by the ratio of the two shoulder spans. Idle and walking now hang both arms alike.

**The grip** (third report: the support hand no longer beneath the gun). Round 2 pulled the left
wrist back along its forearm by half the hand; with the gun hand in front, the support hand ended
up behind it, hidden by the gun hand. Compared frame by frame with Claire and Chris (side view,
offline renders front / side / above): their grip pieces reach in from the left wrist and curl
under the gun, Outbreak's open hand is straight. Now, in a grip (hands up to 2 apart, fading out at
4): the left wrist goes below the gun hand's wrist (Claire's sideways offset is left out: the palm
right under the gun), and the left hand is turned so its fingers point along the gun (the right
forearm; the hand's length and direction measured at load from its vertices).

Fourth report: the support hand (Kevin's at least) still hung beneath the gun and reached too far
out. With the source's full drop (0.36 below the gun hand's wrist for Chris, 0.46 for Claire, times
the shoulder ratio) the palm hung under the gun, and with the wrist a quarter hand back the
straight fingers (Outbreak's hands are 1.6 units, 16 cm) reached 1.2 units ahead of the gun hand's
wrist, past the grip. Tuned against Claire and Chris (third-person side view in game; front, side
and above offline), for all 8 characters: 0.6 of the source's drop, the wrist a twentieth of the
hand ahead of the gun hand's: the palm right under the gun hand, the fingers ending at the front of
the grip. The same for men (Chris's motions) and women
(Claire's) and every arm length (offsets in the model's proportion). The right hand and the gun are
unchanged (the shot still comes from the game's own skeleton). Dual weapons hold the hands further
apart and keep the scaled offset only.

**The left arm in idle and in holds** (sixth report: men's left arm broken and clipping when
standing and in idle, and holding weapons at the wrong spot, most of all the shotgun; aim, walk and
run fine). Cause: the two-handed solve above ran whenever the source's hands were less than 6 units
apart, and Chris's hang 3.4 apart in his idle (Claire's 3.75): the model's left hand was pulled to
"the right hand + the source's offset" scaled by the narrower shoulders, in across the thigh. And
the grip pull of round 4 (the palm under the gun hand) faded out only at 4 units, so it also took
the long guns' holds (the support hand on the shotgun's or rifle's fore-end, 2.6..3.8 from the gun
hand) and the two sub machine guns (2.3..3) back under the gun hand. Measured from the dumped poses
(Kevin, Chris's motions; Alyssa, Claire's), per motion: source hand distance, the source's left hand
forward of its shoulder, model vs source. Fix: for imported models the solve runs only while the
source's left hand reaches forward (full from 1.0 units in front of its shoulder, none below 0.6:
idle 0.2..0.5, holds 1.7, aims 1.0..4.5), and the grip pull only for one-gun grips (hands up to 1
apart, none from 1.5: the handgun's and magnum's 0.3..0.65). Kevin's left hand in idle is now 0.26
outward of Chris's instead of 0.8 inward, and in the shotgun hold 2.85 from the gun hand (Chris
2.65) instead of 1.22 (the rifle and the two sub machine guns alike). The handgun and magnum aims are
unchanged. Checked with the five men, two male NPCs and two women (Alyssa, Cindy), front and side,
next to Chris and Claire.

**The weapon hand** (fifth report: the gun hand too far left, clipping, not on the gun). Cause: Code:
Veronica's weapon models carry the hand that holds them: object 0 of each weapon file model is
Claire's or Chris's gloved hand piece (43 vertices) and the gun, its slide and magazine are its
children. The piece was drawn at the import's wrist next to the import's own hand, so a second,
brown-gloved hand sat on the grip and the import's real hand looked beside the gun. Fix:
`actor_glue.c actor_weapon_hands` hides object 0 of the weapon models (both hands, the partner's
too) while an imported model is shown; the weapon itself stays. Checked with the handgun, magnum,
shotgun, assault rifle, sub machine guns and Gold Lugers (both hands), the knife and unarmed, idle,
walk and aim, all 8 characters (men with Chris's motions, women with Claire's): the import's own
hand holds the grip where Claire's does, so no arm correction was needed.

### Faces

Outbreak's player models move their faces with five bones under the head (group 2): two lid bones
per eye (23..26, a centimetre apart; both on the upper lid, see "The blink") and the jaw (27, at the
ear, the chin and lower lip weighted to it; the mouth has teeth and an inside). The eyes: the face
texture has the irises painted in, and a small eyeball mesh over them (8..24 vertices on the head,
some also on a lid bone, textured with an eyeball picture; Outbreak scrolls its UVs to turn the
eyes). Without that scrolling it covered the irises ("blind"), so the extractor leaves it out
(`ob2.c eye_overlay`, for the `.glb` and the `.cvxm`; the manifest notes it). The converter keeps
these five bones (roles found from the positions: the lowest is the jaw, per side the lower lid bone
is the lid, the higher one the crease above it) and lists them in the `.cvxm` header (version 2;
version 1 files still load, without a face; version 3 adds each lid's travel to close; version 4
has the lid first and the travel measured on the eye's opening).

Code: Veronica: the player characters blink by switching the head's texture list (`player.c`,
`PlyEyeTab`: after 30 ticks a 3% chance per tick, then 8 ticks: half, closed, half, open); the
story cast's cutscene models talk with a face mask (`face.c`, `face_bh.c`: muscles and a jaw
driven per phoneme from the room's lip data, `bhLipSet`; `MASK_WORK.param.jawang` is the jaw
angle in degrees).

`actor_glue.c imp_face`, before the skinning of every pose (the player, the co-op partner, a
cutscene double and the cast alike): the same blink (own random numbers: the game's `rand()` is left
alone), the lids moved down along the head's up axis until their margin meets the bottom of the eye's
opening (the crease and the brows stay); and the jaw:

The blink (seventh report: "they move only their eyebrows instead of blinking"). Cause: Outbreak's two
lid bones per eye are not an upper and a lower lid. Measured on all 30 playable models and their
cutscene versions: the lower bone (24 / 26) holds the lid's margin (its vertices along the top of the
eyeball overlay, the top of the opening), the higher one (23 / 25) the skin above it up to the brow;
no vertex below the eye is on either. Rounds 2 and 4 moved the higher bone, so the crease and the
brows went down a few millimetres while the margin, and with it the open eye, stayed. Now
`ob2.c keep_body` takes the lower bone as the lid, and `lid_falloff` measures its travel: from the
margin (the lowest of the lid bone's most weighted vertices over the eye) down to the bottom of the
eyeball overlay (`ImpModel.eye`, kept from `eye_overlay` before the mesh is left out): 0.4..1.2 cm
on the playable models. Weights on the lid bone away from that eye go to the head (a corner of
Alyssa's sunglasses, costume 2, is on it). A model without an eyeball overlay (some NPCs; they also
have no lid weights) does not blink. Files of versions 2 and 3 name the crease as the lid and
know no opening: they no longer blink (no brow motion either) until imported again. Checked in game
at open, half and closed for all 30 playable models (`RECVX_IMPORT_BLINK=0..1` holds the lids;
`RECVX_IMPORT_POSE` logs each lid's travel at load).

* **Cutscenes**: while the model stands in for a cast double, the jaw turns by the double's
  current `jawang` (the lip sync the game computes for the double anyway). Checked in the
  opening's talking scene (Rodrigo and Claire, RM_0000): the angle arrives every tick, but the
  cast's masks talk mostly with lip muscles and turn the jaw only 0..5 degrees (1.5 at rest), which
  barely parted Outbreak's lips. What is above the resting 1.5 degrees is now turned 2.5 times as
  far (peaks 10 degrees): the mouth opens and closes with the syllables.
* **Gameplay**: Code: Veronica has no mouth motion for the player (the player part files have no
  face mask; voice calls are sound effects without lip data). The jaw follows the player's voice
  instead: `snd_synth.c` measures the mean level of SE port 4 (the player's vocal sounds, also from
  an extra CORE bank) per mixed buffer, `snd_synth_voice_level`; the jaw opens with it at once
  (a scream ~0.18 -> the 14 degree cap) and closes over a few ticks. Checked with a zombie bite.
  Not covered: cutscene lines streamed as ADX while the player model itself acts (the speaker of
  a stream is not known there).

Do the Outbreak models support mouth movement? The playable and cutscene models have a jaw bone
(teeth and the inside of the mouth are modelled) and the four lid bones; no lip shapes (no morph
targets, no bones in the lips) and no mouth texture swaps were found. So the mouth can open and
close; it cannot form vowels.

### Round 7: hair and missing parts

Report: "some hair like from Alyssa, Cindy and other characters have transparent textures and
sometimes holes in their head or some textures are broken".

* **Cause 1, the alpha scale.** Outbreak's CLUTs give alpha as 0..255: 534 of the 535 textures read (playable, cutscene,
  NPC models) have 0xFF as their largest value, and the hair's strand edges and tips
  use the values in between. The converter took the PS2's usual scale (0x80 opaque, as Code:
  Veronica's textures and the GS blending have it). So the hair strips (drawn alpha-blended, last)
  blended at up to twice their colour (light streaks), texels meant at a half (0x80) showed fully and
  those below were see-through where they should not be; and the test which strips are see-through
  missed the strands between 0x80 and 0xFE, so they were drawn opaque in the wrong order. With the
  game's alpha test (anything above 0 writes depth) the inner hair layers then went missing behind the
  outer ones: the scalp and the background showed through (Alyssa's top and back, a black gap at the
  back of Alyssa's costume 4). Fix: `ob2.c` scales each texture's CLUT alpha as it reads it
  (`imp_tim2_alpha_255`: 0xFF -> 0x80), so the PNGs, the `.glb`, the strip test and the `.cvxm` all
  have the PS2 scale. Files of versions 1..3 are scaled when loaded (`load_import_model`), so older
  imports show the hair right without importing again; only the strips' order (see-through strips
  last) comes with a new import.
* **Cause 2, parts taken for the eyeball overlay.** Round 3 left out every mesh under 40 vertices that
  hangs on the head alone. On 11 NPCs that also took glasses lenses, the firefighters' visors, the gas
  masks' lens glass, hair pieces and a ponytail band: holes in the head. `eye_overlay` now takes only a
  mesh on the head and its face bones covering both eyes, at most 2 units high and 6 from the middle,
  whose top lies at the lid's margin; it also finds the overlay of Mark's costume 3 and four NPCs that
  have a vertex on a lid bone (their eyes looked white). Needs a new import.
* **"Broken textures"**: the 60 decoded pictures of the 30 playable models (CSM1 swizzle, 8-bit
  indices, the CLUT) are whole and in place; what looked broken were the light streaks and the
  missing layers above.

Checked (headless, `RECVX_TELEPORT` to a lit room, third person close-ups `RECVX_TPS_ORBIT` front
and back): all 30 playable models before and after, the 13 NPCs whose meshes changed, Alyssa's old
files with the new build. Screenshots stay in the job directory (game content, not committed).

### Round 8: the high-detail models

Report: "there seem to be proper HD models of the playable characters like Cindy etc. Can we only import
those instead of the low-res ones? They look weird in cutscenes otherwise".

What the disc has, measured on all 68 files (`romdata_usa.afs`):

| | in-game `P<c>_<v>` | event `EP<c>_<v>` | high detail `HP<c>` |
|---|---|---|---|
| models | 30 (8 characters, 3..5 costumes) | 30, the same costumes | 8: each character's first costume only (the textures match `P<c>_00`'s, at twice the size) |
| textures | 2 x 256x256 | 2 x 256x256 (the same pictures) | 2 x 512x512 |
| vertices / triangles | 1700..3600 / 2000..4000 | 1600..2300 / 1600..2400: no more detail (some less) | 5400..6700 / 6000..7400 (with the hair locks up to 6900 / 7600) |
| skeleton | the human one, 36 nodes (Cindy's first costume 43) | the same | 73 (Cindy 80): the body 0..21 as the human one, five three-jointed fingers per hand (22..55), a face rig (56..72), Cindy's hair chain and coat flaps (73..79) |
| face | 2 lid bones per eye, the jaw; the eyes painted on the face texture, an eyeball overlay | the same | upper lids 71 / 72 (at the eyes' centres), lower lids 63 / 64, brows 65 / 66, lip corners 57 / 58, the jaw 59, the lower lip 60, the skin 61; real eyeballs on 68 / 69 (the irises on their texture, none on the face's); teeth |
| file | 0.25..0.39 MB | 0.25..0.30 MB | 0.87..1.02 MB |

Most `HP` files (not Mark's and Jim's) have a second AMO/AHI pair: 12..28 small rigid meshes, each on a node of
its own that places it around the head: hair locks (without them the hair is a smooth cap with blocky edges). The
`HN*` NPCs are the NPCs' own high-detail versions and stay as they are; a second `HP03.NBD` in the NPCs' part of
the archive is another model (one texture, 56 nodes) and is not used.

**The import** (`ob2.c ob2_extract`): `P<c>_00` is converted from `HP<c>` when it reads as the high-detail rig
(`hd_rig`: the human skeleton plus the face nodes' parents; tried from the last `HP<c>` of the archive, so the
NPCs' `HP03` is not taken; otherwise the in-game model as before). The id, name, folder and place in the lists
stay `P<c>_00` ("Costume 1"), so a choice saved in `recvx.ini`, the cast's Custom list and co-op keep it; the
manifest notes "High-detail model (HP<c>.nbd)." The same model serves gameplay, the cutscene double, the cast
and co-op: no costume needs the in-game model anywhere (the fit to Code: Veronica's shoulders, `ob2_fit`, is
measured on the model and takes its overrides by the `P<c>` id). The other 22 costumes have no
high-detail model and are converted as before, byte for byte the same; so are all NPCs.

* **Hair locks**: the second pair read onto the head (`read_addon`, the meshes into model space by their own
  nodes, weighted to node 21).
* **Face** (`hd_face`, instead of `lid_falloff`): the upper lids 71 / 72 are the blink's lids, 59 the jaw. Both
  carry only part of their vertices' weight in Outbreak (its own animations move them further): turned as the
  in-game models' jaw, the lips parted half way; moved by the margin's height, the lids stopped half way (David's
  margin is on its bone by 0.43). So the jaw takes twice its share plus the lower lip's (the chin and lower lip
  then wholly on it, the teeth with them), and a lid's weights are scaled so that its margin over the middle
  of the eye is wholly on it (`set_weight`: the vertex's other weights share the rest). It closes from that
  margin down to 0.4 of the eyeball's radius below its centre, where the lower lid's margin lies on the eight
  models (measured against the skin's rim: within 0.1 cm on five, Mark's rim not found). The brows, lower lids
  and lip corners follow the head. The eyeballs stay (no overlay to leave out). Lid travel 0.36..1.1 cm.
* **See-through strips**: the skin textures of the high-detail models have alpha 250..254 in places (Alyssa's
  face, neck, the locks' roots). Every texel under 255 made a strip see-through, drawn blended after the opaque
  ones: Alyssa showed dark specks on cheek, neck and lips and the locks as pale patches. For these models a strip
  counts as see-through only with texels below 240 (`ImpModel.see_cut`); the hair's strand edges are far below.
* **Size**: 1.0..1.13 MB a `.cvxm` (in-game models 0.41..0.44 MB): textures 0.5 MB, the skin (44 bytes a vertex)
  0.24..0.3 MB. A player, partner, double or variant slot has 1.5 MB (`KEEP_SIZE`): fits. The cast's slots share
  3 MB (`CAST_MEM`), where two high-detail models fitted. The skin is read only by `imp_skin` (host code), so the
  loader now keeps it in host memory outside the address window and copies only the game's part into the slot
  (0.76..0.82 MB for the high-detail ones; `PcCvxm.end`): three fit, and the window's use is unchanged (no buffer
  grew; ~2.3 MB of the window stay free on Linux). No texture is scaled down. The texture heap takes 0.5 MB more
  per model (64 MB on the PC). One vertex chunk holds them (at most 8189 vertices; 6923 for Cindy) and the vertex
  work buffer (8192) too.
* **Old files**: the format is the same (version 4), so no new version: files of versions 1..4 load as before
  (checked with a version 3 import of the user's and a version 4 one of main's). The high-detail models come with
  a new import.

Checked (Linux, headless; screenshots in the job directory, game content, not committed): all 30 playable models
(8 high-detail, 0 failed) and with the NPCs 306 models, 268 playable, 0 failed; every file but the 8 first
costumes identical to main's import. In game, all 8 next to main's in-game model: face close-ups open, lids held
closed (`RECVX_IMPORT_BLINK=1`, all 8 close fully), jaw held open (`RECVX_IMPORT_JAW=12`: lips part, teeth
show); head orbits; the opening scene (`RECVX_TEST_SCENE=400:RM_0000:0:0`) with Kevin, George, Alyssa and Cindy
as Claire's double: the lips move with the lines. The cast with only the 8 high-detail models in its Custom list
(two of them in one room, RM_2050 / RM_9320). Co-op: both sides on high-detail models, and the host on one and
the guest on a version 3 file of the same id: no resynchronisation, each side shows its own. `layout_check.sh`:
156 vanilla and 311 enhanced frames identical to main's.

## F9 window

`overlay.cpp import_window`, hotkey row "Import Window" (F9, `B_IMPORT`; F9 was free). A section
per source: the disc image (file selector, saved as `import_outbreak2_iso`), the output folder,
checkboxes (the groups offered: Playable Characters and Other Characters), "Import Characters" (a
thread; progress bar with the model being read), then the manifest per group (collapsible, with
counts; the playable characters open) as a table (character, variant, file, meshes, vertices,
triangles, textures, player model yes/no/failed, notes). After an extraction the new player
models are added to "Character Model" at once (up to 2048 imports, `ACTOR_IMPORT_MAX`, a log line
when more do not fit; an import is sent to the co-op partner as the played character, the lobby's
model byte too). The list only grows while the game runs: a model already listed keeps its place
(extracting it again replaces its file, written to a temporary file and renamed, so a model being
loaded is never half written), new ones are added at the end, and entries are never rewritten or
freed, so the game thread reads them without a lock (`actors.h`). A restart lists them by group again.

The lobby's MODEL page (main's scrollable list of "Character Model") lists the imports after Code:
Veronica's own models, by group (`import_registry_load` registers them group by group; the
manifest is in disc order). A group's first row (and the page's top row) carries the group's name
on the right with a line above it (`actors.c appear_group`: Code: Veronica Characters, Code: Veronica Cast, Code: Veronica
Enemies, Code: Veronica Cutscenes, Outbreak Characters, Outbreak Cutscenes, Outbreak Supporting Cast,
Outbreak Enemies). 384
entries with all groups extracted: L1/R1 and Page Up/Down a page, Home/End, the wheel 3 rows a
notch, and a click on the scroll bar jumps to that part of the list. The preview loads the entry
under the cursor once it has stayed 8 frames (main's delay); checked with 43 imports loaded one after
the other while paging (all loaded, none refused).

## IMPORT screen

The in-game version of the F9 window (`pc/glue/import_menu.c`): title menu **EXTRA** >
**IMPORT** (docs/BIORAND_INGAME.md "S7" for EXTRA). Drawn as the BIORAND screen, its boxes the shared dialog
(`options_glue.c pc_ui_dialog`, RETURN TO TITLE's look). Keyboard, gamepad and mouse; cancel or a right click goes
one page back, from the first to EXTRA.

| page | rows |
|---|---|
| IMPORT FROM OTHER GAMES | the categories (`cats[]`, `IMPORT_CAT_*`): **CHARACTERS**, **LANGUAGES** ([LANGUAGES.md](LANGUAGES.md)) |
| CHARACTERS | the games (`import_sources[]` of that category): Resident Evil Outbreak File #2 |
| LANGUAGES | the releases: Resident Evil Code: Veronica X (Europe); its page has Disc Image, START IMPORT (every language of the disc), DELETE IMPORTED LANGUAGES and "Needs a clean European disc image of Resident Evil Code: Veronica X: SLES-50306." with the installed languages |
| the game | **Playable Characters** and **NPCs** On/Off (the groups of `IMPORT_OFFERED`; both on at first); **Disc Image** (the file's name; confirm opens the system's file dialog, ini `import_outbreak2_iso`); **START IMPORT**; **DELETE IMPORTED MODELS**; BACK. Below: "Needs a clean US disc image of Resident Evil Outbreak File 2: SLUS-20984." and what is imported ("30 playable characters and 238 NPCs can be played." from the manifest, with its date) |

- **The file dialog** has to be opened by the main thread: the screen sets `g_lobby_io.file_req` (lobby.h), and
  `lobby_io_frame` calls `SDL_ShowOpenFileDialog` with the game's window; the answer comes back in
  `g_lobby_io.file`. Test aid: `RECVX_FILE_DIALOG_TEST=<path>` answers it without a dialog.
- **START IMPORT** runs `import_start` (the F9 thread) and shows its progress in a box: the model being read and a bar
  with "done / total". It cannot be cancelled. Afterwards a box says how many models can be played (and how many
  failed), or the importer's error: another disc is refused by its `SYSTEM.CNF` ("Use a US Resident Evil Outbreak
  File 2 disc (SLUS-20984)."). `ImportProgress.result` holds `import_run`'s result for it.
- The game's font draws `#` as the biohazard sign, so the screen writes the title as "Resident Evil Outbreak File 2".
- **New models** are listed at once: the screen sets `g_import_reload`, and the main thread runs
  `import_registry_load` (host.c, every frame; the registry has one writer). The log says "import: N imported
  player models listed".
- **DELETE IMPORTED MODELS** asks (Yes/No, No first), then deletes `<import_dir>/outbreak2/` with the BioRand
  downloader's link-safe delete (`brdl_rm_tree`: a link inside is removed, never followed). Decided: the list of
  "Character Model" only grows while the game runs (`actors.h`), so the deleted models stay listed until the game
  restarts. Choosing one then shows the character you play: its `.cvxm` is missing, and the load is refused as for
  any unreadable file (`actor_set` remembers the failure). Importing again brings the files back under the same
  entries. A restart lists only what is there.
- Both use the same thread, one extraction at a time.

Tests (2026-10-28, Linux, headless; job dir `extramenu/imp.sh`): EXTRA > IMPORT > CHARACTERS > the game, the disc
through the test aid, START IMPORT with both groups: 306 models, 0 failed, "30 playable characters and 238 NPCs can be
played.", "268 imported player models listed"; DELETE, YES: the folder gone. The CVX disc as the image: the
`SYSTEM.CNF` error in the box. Screenshots in the worktree's `scratch/extramenu/`.

## Notes for later

### Voices

* `NETBIO01.DAT/adxsnd_usa.afs`: 1853 CRI ADX streams without names: 1600 mono 24 kHz (median
  1.8 s: the scenario dialogue and, probably, the ad-lib lines), 222 stereo 48 kHz (music cues,
  ambience), the rest placeholders. ADX is what this port already decodes
  (`pc/src/audio/adx.c`), so they can be played as they are; which index is which line needs the
  game's tables (the Outbreak decomp, or a listening pass).
* `pl00_vo.afs`: 317 named ADX lines (`c00-245_01-03.adx`: line, take, variant), mono 24 kHz,
  0.3..4.8 s (median 1.1 s): short calls; the name suggests one character's (pl00, Kevin) set; the other characters' sets
  are not in separate archives (to be found: `adxsnd_usa.afs` or the scenario archives).
* `romdata_usa.afs/pl<cc>_voi.snd` (and `_npc.snd`, the AI partner's): MOMO containers of four
  parts: a Sony SCEI HD bank (`SCEIVers`, `SCEIHead`, `SCEIVagi` 20 samples, `SCEISmpl`,
  `SCEISset`, `SCEIProg`), its BD (PS-ADPCM, ~90 KB = ~7 s), and two tables. Short vocal sounds per
  character: pain, death, effort, likely the ad-lib shouts. The port's sound driver already plays
  PS-ADPCM programs (`snd_iop.c` extra banks, as the CORE voice banks of "Character Model"), so a
  converter to its bank format would let an imported character speak with its own voice.

### Social calls ("ad-libs") and animations

* `pl<cc>_pc_000.bin` (per character, 60..225 KB) and `pc_<nnn>.bin` (shared, 50 KB..1.9 MB): motion
  banks; header `u32 0, u32 count`, then (u32 frames, u32 offset) entries. The gestures that go
  with the calls (wave "Come on!", "Help!" with raised arms, nod "Thanks") are most likely in the
  per character file; the shared ones hold the common action set.
* For co-op the cheapest path: a call = a voice line + a motion index sent in the pose (like
  `voice_n`); the motion converted to the CVX 22-node skeleton by the same node map as the mesh
  (Outbreak motions animate the 36-node skeleton: rotations per node; the root carries the
  position).
* Facial/lip data: the face nodes (22..27) and the HP models' face bones; `LIP23_00_0.bin` in
  romdata is a lip track (one, for a cutscene); the event models `EP*` and `HP*` carry the faces.

## Verification (this round)

* Headless extraction from the real disc: all 30 player models (30 converted), and with
  `RECVX_IMPORT_WHAT=6` 285 NPC and enemy models, none failed (a few seconds each run).
* `pc/tools/import_check.py` over all 315 `.glb`: GLB header, accessors within their views, joints
  in range, weights summing to 1, inverse bind matrices against the node tree; previews of the rest
  pose and a skinned test pose (arms lowered) look right; the `.cvxm` read the way the game reads
  it renders the same.
* In game (headless, frame dumps): Kevin, Jim and Alyssa as "Character Model": walking, the
  handgun aimed, zombie grabs, third person from behind and (orbit) the front; the cutscene double
  loads the same model (textures shared). F9: the "Import Characters" button clicked in a run
  with an empty mods folder (progress, then the table); F3 shows the imported model chosen in
  recvx.ini. Screenshots stay in the job directory (game content, not committed).
* ctest 21/21 (`import` new, `appear` extended), Linux and Windows (mingw) builds,
  `layout_check.sh` main (339a4ca) vs this branch: frames identical (vanilla and enhanced).

### Round 3

* Eyes: P00_00, P05_00, P07_00 rendered offline before / after (`.cvxm` and `.glb`) and in the
  lobby close-up: irises visible.
* Mouth: the opening scene played headless (inputs above), Claire's double as Kevin: jaw log per
  tick and close-ups (the mouth opens with the syllables); gameplay: a spawned zombie bites, the
  voice jaw opens with the scream (third person close-up).
* Shoulders: lobby front views of Claire, Chris and four imports before / after.
* Grip: Claire, Chris and four imports aiming (third person side view) and offline renders of the
  dumped aim pose (front, side, above).
* All groups extracted (537 models, 0 failed, 324 player models); F9 with the four groups.
* ctest 21/21 (`movies` skipped, as on main), Linux and Windows (mingw) builds, `layout_check.sh`
  main (17a36c8) vs this branch: 156 vanilla and 311 enhanced frames identical.
