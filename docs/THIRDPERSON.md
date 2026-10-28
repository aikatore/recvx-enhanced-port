# Third-person mode

An optional over-the-shoulder camera with mouse/keyboard or twin-stick gamepad movement and free
aim, in the style of RE4/RE5 and the RE2/RE4 remakes. Off by default. With it off the game behaves exactly as without it: every decomp
hook is `#ifdef TARGET_PC` + `if (tps_on)`; the logic lives in `pc/glue/tps_glue.c` (game
thread), `pc/src/host/tps.h`, `pc/src/input/tps_input.c` and the F1 overlay (settings, mouse).

## Usage

Toggle with **F5**, the `--third-person` command-line flag, or `tps = 1` in
`<save dir>/recvx.ini`. It can be switched at any time; it only takes over in normal play.
While it is on the mouse is captured whenever you have control (F1/F2/F3, a menu, a question or
a puzzle with mouse support free it until you are back in control).

| Key | Action |
|---|---|
| W / S | walk forward / back (back + Shift = quick turn, as in the original); with "Aim Assist" also aim up / down while the weapon is raised, as the original's up / down |
| A / D | turn |
| Shift | run; in the scope ([Scope](#scope)): zoom in while held |
| mouse | X turns the character, Y looks up/down |
| right mouse (hold) | ready weapon; the mouse now aims (camera closer, narrower FOV, crosshair) |
| left mouse | fire while aiming; otherwise action like E |
| E | action / examine / confirm |
| Space (hold) | quick knife: knife drawn and raised, left mouse slashes ([USER_GUIDE.md](USER_GUIDE.md)) |
| Q | cancel |
| R | reload ([USER_GUIDE.md](USER_GUIDE.md#reload)) |
| Tab | status screen (inventory) |
| M | map |

Arrows, Enter, Backspace, Z/X, 1/2 and gamepads keep working. Any key bound to a third-person
row stops pressing its DualShock 2 button while third person is on (by default W/A/S/Q lose
R1/Square/Triangle/L1). All third-person keys, a quick-turn key (unbound by default) and the
toggle key can be rebound in F1 -> Controls or OPTION -> ENHANCED SETTINGS -> CONTROLS (ini
`key_tps_*`: `tps_forward`, `tps_back`, `tps_left`, `tps_right`, `tps_run`, `tps_action`,
`tps_cancel`, `tps_inventory`, `tps_map`, `tps_aim`, `tps_fire`, `tps_reload`, `tps_quickturn`,
`tps_toggle`; the toggle key's first slot is also `tps_key`).

### Mouse/keyboard camera (A/B)

Setting "Mouse Camera Style" (THIRD PERSON page, ini `tps_free_camera`):

* **Behind Character** (B), the default: the camera stays behind the character and the mouse
  turns it (the table above).
* **Free Camera** (A): the mouse orbits the camera around the character (pitch within +-60 degrees,
  walls as in B) without turning it. W/A/S/D walk towards that direction on screen
  (diagonals too) and the character turns to it smoothly (0.3 of the angle per game frame);
  Shift runs. Aiming turns the character to the camera direction (free aim: at once, through
  the crosshair as in B; with "Aim Assist": towards the camera unless the game locks on);
  after aiming the camera stays where it looked. The quick-turn key does the game's quick
  turn (S + Shift runs towards the camera). Arrow keys (and the gamepad with Classic gamepad
  controls) keep tank controls; while they steer, the camera follows behind as in B.

Doors, events and cutscenes put the camera behind the character again in both. A/B are this
document's short names for the two values, not the game's KEY CONFIG types.

### Scope

The sniper rifle and the linear launcher aim through the game's own first-person scope
(`playpch2.c bhCPM2_act_scp`, `mode2` 71, both weapons); our camera is off there
(`st_flg 0x800000`). The game turns it with the d-pad and the left stick only: `ay`, and `wax`
within +-45 degrees. The view is `cam.ay = -ay`, `cam.ax = -wax` from the head
(`cut.c bhSetPlEyeCamera`), and the shot (the rifle's bullet ray, the launcher's beam) leaves
from the same point along the same angles, so it goes where the scope's centre is.

* **Mouse** (`tps_player` -> `scope_look`, after the mode handler): X turns `ay`, Y pitches `wax`
  (invert Y as set), straight in game units with the part below one unit kept for the next frame,
  so it turns smoothly rather than in the stick's steps. Speed: the aiming sensitivity (X / Y
  times "Aiming Sensitivity") scaled by the field of view shown against "Aiming Field of View"
  (`cam.pers`): the scope's 65 degrees turn as third-person aiming does, zoomed in (20 degrees)
  about a third as fast, and in between while the zoom moves. `wax` is clamped as the game clamps
  it. During a shot's recoil (`mode3` 3, a second) the game sets `wax` to its base `ct3` plus the
  kick every frame and ignores the stick; the mouse moves `ct3`, so it keeps aiming under the
  kick. The scope's first tick still has our camera, whose free aim points the scope at what the
  crosshair was on (as before).
* **Zoom**: the game zooms while its "change aim" bit (`0x80`, L1) is held. L1's key is Q, the
  third-person cancel, so in the scope run (Shift) presses it (`tps_pad`).
* **Fire, leave, reload**: left mouse fires (the fire row), letting go of aim leaves the scope and
  our camera comes back behind the direction the scope looked; reload (R) works in the scope
  ([USER_GUIDE.md](USER_GUIDE.md#reload)).
* **Gamepad, Modern**: its third-person buttons keep playing in the scope (`pad_play` while
  `in_scope`: `mode2` 71, or `st_flg 0x800000` through the scope's reload), so the held LT keeps
  the scope up, RT fires and RB reloads. Before, the scope turned `pad_play` off with our camera,
  and an input held through that change counts for neither set until let go (`bindings.c
  pad_row_reads`): the held LT was no aim any more and the scope closed as it opened. The right
  stick turns the scope as the mouse does (`scope_look`: `stick_yaw` / `stick_pitch` from
  `pad_sticks`, with the stick speed, invert Y and "Stick Aiming Speed", scaled by the zoom); the
  left stick and d-pad up / left / right still turn it the game's way. The right stick click
  (`pad_tps_recenter`, which has nothing to recenter there) switches the zoom on and off: the
  stick that aims holds no button well, and LB, the game's zoom (L1), is the quick knife here. The
  zoom ends with the scope. Run (X) held zooms too, as Shift does.
* **Gamepad, Classic**: the game's own scope controls (R1 aims, the left stick and d-pad turn it, L1
  zooms), and the right stick turns it as in Modern.
* **Widescreen**: the view through the scope is hor+ as the normal view, and its tint fills the
  width as the DISPLAY setting "Screen Effects" says (Original keeps it 4:3, Off leaves it out);
  the reticle stays round in the middle, where the shots go.
* With the fixed cameras the scope keeps the original controls: the mouse is captured only in
  third person, and the fixed cameras have no mouse aim or fire.
* [Precise Shots](#precise-hits): the sniper rifle's shot is tested along the scope's centre against
  the enemies' joint volumes (the game's own test was nearly that, 0.1 units wide); the linear
  launcher's beam is a projectile and unchanged.

### Gamepad

Setting "Gamepad Controls" (THIRD PERSON page, ini `tps_pad_controls`): **Modern**,
the default, or Classic (the gamepad as a DualShock 2 through the game rows: tank controls, the
camera behind, no camera stick). The game's own turn while aiming cannot move free aim's crosshair
(the shot follows it), so in Classic the right stick, which no key type uses (the game reads only
the left stick), aims while the weapon is raised, at the stick aiming speed as in Modern; with
"Aim Assist" Classic keeps the game's lock-on. In both schemes, while aiming freely, the left
stick and the d-pad also move the crosshair (left / right turn it, up / down raise and lower it, as
they turn and tilt the original aim); in the scope the game turns by them itself and only the right
stick is added. Modern, as the RE2/RE4 remakes (button map:
[USER_GUIDE.md](USER_GUIDE.md#third-person)):

* **Left stick**: walks towards its direction seen from the camera (`modern_target` with the
  camera's right axis, as the fixed cameras' modern controls), past its dead zone; pushed 70% or
  with the run button it runs. The character turns as with the fixed cameras' modern controls
  (`modern_turn`: 40% of the way a frame, at most 30 degrees; more than 100 degrees away the
  game's turn on the spot first). Stairs, steps and pushing: "forward" while it points ahead.
* **Right stick**: orbits the camera (yaw free, pitch +-60 degrees, walls as above) at 180 / 120
  degrees a second fully pushed at 100%, a squared response past its dead zone. Nothing pulls the
  camera behind; the right stick click eases it there (pitch back to the start).
* **Aim (LT)**: free aim as the mouse's (the same crosshair ray, shot, pose and aim height): the
  character turns to the camera direction at once, the right stick moves the view (aim speed,
  default 50% of the camera speed). With "Aim Assist" on the gamepad keeps free aim with a
  gentle assist (`modern_assist`): with the middle of the enemy nearest the crosshair within 6
  degrees and no wall in between, the stick turns at half speed, and while it moves (not away
  from the enemy) the view drifts a tenth of the way to it. A still stick never moves the aim.
* **Quick Turn**: its button (d-pad down), or pulling the stick back (more than 100 degrees from the
  facing) and pressing run within 3 frames (the turn on the spot waits that long for it): the
  game's quick turn. Either swings the camera behind the character as it turns. The stick keeps
  the camera it was given in (`modern_hold`, as over the fixed cameras' cuts) until it turns by
  more than 35 degrees or is let go, so holding it keeps going the way it went.
* **Reload** (RB): the game's reload of an empty magazine at any time the magazine is not full
  (`pc/glue/reload_glue.c`, [USER_GUIDE.md](USER_GUIDE.md#reload)). Aiming it is the game's own;
  with the weapon lowered it comes up for the reload and goes down after it, and the camera stays
  as when walking (`reload_at_rest`: no aim view, crosshair, facing or bend). With Classic gamepad
  controls the fixed cameras' Aim + Triangle reloads; any reload binding may share a button with
  another action as Aim + button ([USER_GUIDE.md](USER_GUIDE.md#reload)).
* **Bindings**: the gamepad set has its own third-person rows (ini `pad_tps_aim`, `pad_tps_fire`,
  `pad_tps_action`, `pad_tps_cancel`, `pad_tps_run`, `pad_tps_inventory`, `pad_tps_map`,
  `pad_tps_reload`, `pad_tps_quickturn`, `pad_tps_knife`, `pad_tps_options`, `pad_tps_recenter`;
  options on Start, as the game's Select is the map here and type D's L2 the aim). They play only
  while `g_tps.pad_play` (`tps_frame`: third person on and Modern, our camera active, the game not
  frozen, no attract demo) and then take their buttons over from the game rows (the d-pad but its
  down and anything else they do not use stay the game's). In
  menus, messages, pick-ups, events and doors the gamepad is the DualShock 2 of the KEY CONFIG
  type, as in Classic. An input held when `pad_play` changes counts for neither set until it is
  let go (`bindings.c pad_row_reads`), so the button that opened a menu does not also act in it.
  A new press of the inventory, map or options row while its screen is up closes it, as the game's
  own buttons toggle theirs (`tps_pad` `toggle_closes`: the status / map bit; OPTION:
  `pc_options_mouse0` adds Select; both read the row raw with `bind_pad_raw`).
  Hits, grabs and weapon loads keep `pad_play` (a held trigger stays held).
* **Mixed devices**: `pad_cam` is the device used last (any gamepad button or stick past a third:
  the gamepad; mouse motion, a key or a mouse button: keyboard/mouse). With the gamepad the camera
  orbits freely and the left stick walks; with the mouse, camera A or B as set. "Aim Assist"
  means the lock-on with the mouse and the assist with the gamepad.

Hooks: `tps_pad` (`pad_sticks`: device, right stick as `stick_yaw`/`stick_pitch`, left stick into
the virtual bits; the quick-turn gesture into `tps_input_keys`), `tps_camera` (orbit, recenter),
`tps_player` (the turn towards the stick; the right stick and the assist while aiming), `knife_pad`
(`pad_quick_knife` also reads `pad_tps_knife`). Classic keeps `pad_play` 0 and the game rows; only
while aiming freely `pad_sticks` turns the right stick (and the left stick and d-pad, both schemes)
into `stick_yaw` / `stick_pitch`.

### Settings

All on the THIRD PERSON page (F1 or OPTION -> ENHANCED SETTINGS). Lengths are in tenths of a game
unit (the character is about 17 units tall).

| Setting | ini key | Default |
|---|---|---|
| Over-the-shoulder Camera | `tps` | 0 |
| Field Of View (horizontal over the 4:3 picture; widescreen adds to the sides) | `tps_fov` | 75 |
| Aiming Field of View | `tps_aim_fov` | 65 |
| Camera Distance | `tps_distance` | 140 |
| Aiming Camera Distance | `tps_aim_distance` | 100 |
| Camera Height | `tps_height` | 165 |
| Shoulder Offset | `tps_shoulder` | 35 |
| Left Shoulder | `tps_left_shoulder` | 0 |
| Horizontal / Vertical Mouse Sensitivity (percent; 100 = 0.12 degrees per mouse count) | `tps_sens_x`, `tps_sens_y` | 100 |
| Aiming Sensitivity (percent of the above) | `tps_aim_sens` | 70 |
| Invert Mouse Y | `tps_invert_y` | 0 |
| Crosshair | `tps_crosshair` | 1 |
| Aim Assist (0 = free aim) | `tps_auto_aim` | 0 |
| Precise Shots ([Precise Shots](#precise-hits)) | `tps_precise_hits` | 1 |
| Mouse Camera Style (0 = Behind Character, 1 = Free Camera) | `tps_free_camera` | 0 |
| Gamepad Controls (0 = Classic, 1 = Modern) | `tps_pad_controls` | 1 |
| Horizontal / Vertical Camera Speed (percent; 100 = 180 / 120 degrees a second) | `tps_pad_sens_x`, `tps_pad_sens_y` | 100 |
| Stick Aiming Speed (percent of the above) | `tps_pad_aim_sens` | 50 |
| Invert Stick Y | `tps_pad_invert_y` | 0 |
| Movement / Camera Dead Zone (percent) | `tps_pad_dead_move`, `tps_pad_dead_look` | 20, 15 |
| Lighting and Fog (1 = Smooth, 0 = Original) | `tps_atmos` | 1 |
| Camera Key (SDL scancode) | `tps_key` | 62 (F5) |

### Test aids

| Env var | Effect |
|---|---|
| `RECVX_TPS_MOUSE="frame:dx:dy,..."` | moves the mouse dx/dy counts per game frame from that presented frame on |
| `RECVX_VPAD="frame:input+input,..."` | a virtual gamepad ([ARCHITECTURE.md](ARCHITECTURE.md)): sticks as `leftx=0.5+lefty=-1` (up is negative), triggers `+lefttrigger`; a trigger after another input needs its own `+` (`+lefttrigger++righttrigger`) |
| `RECVX_TPS_TRACE=1` | logs the gamepad per frame ("tps: pad": the sticks past their dead zones, the walk direction, facing, camera, quick turn, the enemy nearest the crosshair) and its aim assist ("tps: assist"), the free aim per frame, each shot's direction and wall hit against the crosshair target ("SHOT"), the drawn weapon's barrel with "Character Model", the camera ("tps: cam A\|B": yaw, pitch, the player's position, the model's root, the point followed and the eye), the aim height given to the game, the scope per frame ("tps: scope": `ay`, `wax`, the field of view, the turn of the mouse and right stick in game units, the enemy nearest the centre in degrees, the enemies' health), each shot of a dual weapon ("tps: DUAL R\|L": the arms' yaws aimed and shown, the enemy under the crosshair and the other arm's, the aim height, each enemy's health, degrees off the facing and distance), each gun hit ("tps: HIT": weapon, enemy, joint, damage, precise or the game's test, the enemy's state), each precise test that met a body ("tps: PRECISE": joint, volume, along which ray), a zombie's head burst ("tps: HEAD BURST"); `=2` also keeps the camera on the enemy the game locked on to (dual weapons: the nearest of their lock-on), also in the scope, and logs per shot how far the crosshair passed from that enemy's body ("tps: SHOT at enemy") |
| `RECVX_TPS_AIM_AT=f`, `=cN`, `...:side:up` | with `RECVX_TPS_TRACE=2`, where on that enemy: `f` along its hit capsule (0 = `c1`, for a zombie the head, 1 = `c2`, its legs; default 0.5), `cN` the middle of volume N of its joint table ([Precise Shots](#precise-hits); a zombie: `c0` hips to chest, `c4` the head); `:side:up` adds that many units beside it (right of the view, negative left) and above |
| `RECVX_TPS_ORBIT=degrees` | turns only the view about the player (side views; aim unchanged, camera ignores walls) |
| `RECVX_ATMOS_TRACE=1` | logs each game frame's cut, the game's fog, lights and filter, the player's view depth from the view drawn and from the cut camera, and the fog shown with our camera (range in view depth) and what "smooth" shows |
| `RECVX_EFF_TRACE=1` | logs effects spawned and queued |

## How the original camera works

* **Cuts** (`cut.c`): each room has `rom->cut_n` camera cuts (two versions `cam[camver]`).
  `bhCheckCut` (game.c `bhMainSequence`, after drawing) picks the cut whose floor area contains
  the player; `bhSetCut` loads its camera (`cam.px/py/pz`, `ax/ay/az`, `ln`, `pers`), fog, filter,
  the per-cut **hidden room parts** (`hidobj` -> `NJS_CNK_OBJECT.evalflags |= 8`) and hidden
  lights (`hidlgt`). `bhControlActiveCamera` follows the player inside tracking cuts. Rooms with
  `ts_flg & 0x200` also change the clip range per cut.
* Event cameras (`st_flg & 1`), first person and the sniper scope (`gm_flg & 0x40`,
  `st_flg & 0x800000`) and door transitions (`cb_flg & 1`) are separate paths. First person is the
  precedent for a free camera: it un-hides all room parts and sets `njClipZ(-1, -20000)`.
* **View** (`camera.c bhControlCamera`, before drawing): `njSetPerspective(cam.pers)` and
  `cam.mtx = T(0,0,-ln) Rz(az) Rx(ax) Ry(ay) T(-(p + of))`, plus the view direction `cam.v*` and
  eye `cam.wp*`. Everything 3D uses `cam.mtx`: drawing, billboards, model culling, 3D sound.
* **Projection** (`ps2_NaView.c`): `_nj_screen_.dist = 320 / tan(ang/2)` (horizontal FOV over the
  4:3 screen); culling cone, VU1 projection and clip volume follow (widened by `pc_hor_scale()` in
  widescreen). A different camera needs no change there.

## How the player moves, turns and aims

* `pad.c bhSetPad` (decomp) maps the DualShock 2 bits through the key type table into virtual bits
  `sys->pad_on/pad_ps`: `0x1` forward, `0x2` back, `0x4`/`0x8` turn, `0x10` ready weapon,
  `0x20`/`0x40` aim up/down, `0x100` fire, `0x200` examine, `0x400` run, `0x1000` cancel,
  `0x2000` map, `0x4000` status.
* `player.c bhControlPlayer` runs tank controls; turning adds a fixed rate to `plp->ay`
  (65536 = 360 degrees; forward = `(-sin ay, 0, -cos ay)`).
* Raising a weapon calls `bhSearchNearEnemy` (auto-aim: `plp->ayp`, `flg |= 0x100000`, `ay` turns
  to it over `yrct` frames). Aim pitch is three-way (45 degrees; `at_flg` 2 straight, 4 up,
  8 down), and enemies read it when they take damage ("Aim height" below). Firing casts a ray from
  `(px, py + 11, pz) + forward * (3 + r)` along `Rx(wax) Ry(ay)` against enemy capsules
  (`weapon.c bhCheckGunAtari`); the wall test (`bhCheckL2Wall`) uses the same direction from the
  muzzle on hand node 9 (about 3 units higher). Setting `ay` and `wax` towards the crosshair target
  therefore makes the game's own ray hit it.

## What hides room parts

* Third person draws every room part while its camera is active (`ps2_NinjaCnk.c
  pc_show_hidden`), except nodes without a model: they carry the hide flag for good (`bhSetCut`
  only changes it on nodes with a model; rooms 6/8, 7/25, 9/8 and 9/17 have such nodes). Room records with `flg 0x40000000` (drawn only in the cuts in `hide[0..3]`)
  are drawn too (`objitm.c`). Model culling uses the active view: the bounding-sphere test of
  chunk models (`njCnkDrawModelLocal`) is `view_glue.c pc_model_clip`, the sphere against the
  view's planes with a margin of half the view's half width and height, so room parts do not
  pop at the screen edges while turning (the original test ignores how far an off-axis
  sphere's outline reaches; see ARCHITECTURE.md "Widescreen").
* Rooms are modelled for their camera angles: from elsewhere some walls are missing, single-sided
  or untextured. The camera is kept inside the collision walls (`bhCheckL2Wall`): the shoulder
  point is pulled in sideways if it is in a wall, the eye slides forward along the view ray in
  front of the first wall behind the shoulder.

## Design

* **The cut system keeps running** (`bhCheckCut` unchanged), so doors, triggers and per-cut
  lights/fog keep working. A hook in `game.c bhMainSequence` before `bhControlCamera()` writes our
  camera into `cam.px/py/pz, ax, ay, az, ln, pers`, so `cam.mtx`, `cam.v*`, `cam.wp*` and 3D sound
  agree. Not applied with an event/fixed cut, first person/scope, door transitions or events
  (`cb_flg & 5`, `gm_flg & 9`); when it stops, `gm_flg |= 0x800` re-initialises the current cut
  and the room's clip range is restored.
* **Frozen cut camera**: messages, examine texts, pick-up prompts and menu fades clear
  `sys->sp_flg 0x40`; our camera stays active and holds still until it is back.
* **The cut camera underneath**: `bhCheckCut` leaves the cut's camera in `cam`, and the next
  frame's game logic reads it (gore sprites towards `-cam.v*`, `bhEff305/306` towards `cam.wp*`,
  3D sound). `tps_after_cut` (after `bhCheckCut`) puts our view back and clears `gm_flg 0x20`
  (the cut change that wipes lens blood). The cut camera keeps its own state (`cut_cam`):
  `tps_after_draw` puts it into `cam` before `bhCheckCut`, so a tracking cut eases on from where
  it was rather than from our view, as with the fixed cameras, and `tps_after_cut` keeps it with
  its view vector and eye. It is where the fixed camera would be (fog, below; the
  camera-relative fog of `flg & 0x40/0x80` measures its eye).
* Enhanced-mode interpolation: `pc_scene_id` ignores cut changes while our camera is active.
* Hooks: `bhSetPad` (third-person keys and mouse buttons as virtual bits); `bhControlPlayer` after
  the mode handler (mouse yaw into `plp->ay`; while aiming `ay`/`wax` towards the crosshair target,
  auto-aim turn cancelled); its `bhCalcModel(plp)` replaced by `tps_calc_model` (free-aim pose).
* Camera A (`g_tps.free_cam`): `tps_pad` turns W/A/S/D into "forward" in move mode; `tps_player`
  turns `ay` towards camera yaw minus the keys' angle (not in the game's own turns or special
  moves, `mode2 > 13`); `tps_camera` adds the mouse X to the camera yaw. Pose, aim and co-op read
  `ay`/`wax`, never the camera.
* **Stairs and ledges** (the game's own moves, `mode2` 14..22: `bhCPM2_act_kdu`/`kdd` stairs,
  `dnu`/`dnd` climbing on or off a ledge; pushing, `psh`, moves the position every frame and the
  ladder modes are used by no room, ladders between rooms are door animations): these moves leave
  the player's position where the move started and add the motion's travel at once, the stairs
  every two steps (12 units on, 8 up or down), a ledge climb at its end; only the drawn model (its
  root, `owP[0]`) moves smoothly. Following the position, the camera stood still and
  then jumped (up to 14.5 units in a frame on stairs, 10 on a ledge). The camera follows the root
  instead (`follow`), less its offset when the move started, so it starts where the walking camera
  was; its height eases (0.35 a frame: the root bobs with each step and dips as the character
  crouches), and after the move the point followed eases back onto the position. The yaw is
  untouched (B eases behind the character as when walking, A and the gamepad keep theirs), walls
  pull the camera in as always. Largest camera move in a frame on stairs or a ledge now about
  2 units (walking: 1).
* Camera collision never feeds back into the aim: the crosshair ray starts at the wall-free
  shoulder point, and the camera position depends only on the player's position.
* Mouse: SDL relative mode while third person is on, the window has focus and no overlay window
  is open, unless a screen with mouse support has it ("Mouse In Menus", `menu_mouse.c`
  `menu_mouse_cursor`). That screen flag never outlasts play: the game thread publishes each frame
  whether the player has control (`g_menu_mouse.play`: `pc_in_play`, not the co-op trade screen),
  and in play the mouse is the camera's whatever a screen left behind. A message's choices count
  as a screen only while the message shows (`st_flg` 0x200): puzzles that end their choice with
  `bhMesDispEndSet` on cancel (the Gold Luger room's switch panel, the office dial, the bio-lab
  keypad) leave `st_flg` 0x8000 and `mes_seln` set until the next message. Deltas are consumed
  once per game frame. `RECVX_MOUSE_TRACE=1` logs each change (captured, cursor, menu, play,
  overlay, focus).

## Light and fog

With the fixed cameras each cut's atmosphere changes exactly when the picture cuts. With the
third-person camera the cut still follows the player underneath, so light and fog would
switch while walking.

**What a cut sets** (`cut.c bhSetCut`, `CAM_WRK` of `rom->cutp[ncut].cam[camver]`; counts over
all 205 rooms, camera version 0, from `pc/tools/rdx.py`):

| Setting | Where it acts | Rooms whose cuts differ |
|---|---|---|
| fog on/off (`flg & 8`) | `sync.c bhControlEOR` | 31 |
| fog colour (`fog_col`) | `njSetFogColor` | 110 |
| fog range (`fog_nr`, `fog_fr`, linear in view z) | `njGenerateFogTable3` | 172 |
| camera-relative fog (`flg & 0x40` near, `0x80` far): range shifted by eye minus cut camera base, every frame | `bhControlActiveCamera` | 82 |
| light set: lights 4.. hidden per cut (`hidlgt`); the first directional and first three point lights that are on fill the VU1 slots | `light.c bhControlLight` | 181 |
| point lights only if on screen (`flg & 0x10`) | `bhControlLight` | 9 |
| screen filter (`flg & 0x8000`, `fil_no`, `fil_rt`) | `effsub1.c bhEffFil` | 62 |
| under-water view: full-screen distortion (`flg & 0x400` -> `cam.flg 0x20`, room effect 110 type 1 `bhEff110`) | `gm_flg 0x100`: `bhDrawFullScreenRenderTexture` | 2 |
| `ts_flg & 0x200` rooms: view clip, background colour per cut | `event.c`, `ps2_event.c` | (third person keeps its own clip) |

Room ambient and lights 0..3 (muzzle flash, lighter, ...) do not change per cut.

**Smooth** (default; ini `tps_atmos` 1; 0 = Original, per cut). `tps_atmos` (`game.c` after
`bhControlLight`) and `tps_filter` (`bhEffFil`) take the game's per-cut values as the target:

* A change in a frame where the cut changed (`cam.ncut`/`camver`) fades; any other change
  (scripts, flicker, muzzle flash, event fog) applies at once. Lights 0..3 are never faded.
* Lights: a weight per room light, 0 -> 1 in 24 game frames (0.8 s), applied to the values the
  game computed this frame. The target is what `bhControlLight` shows: the first directional
  light that is on and the first three point lights that are on. A light that is on behind those
  gets no weight: the palace main hall's normal version (RM_1020) keeps the red alarm light of
  its alarm version (RM_1021, `ev_flg` 181, which turns the room's directional lights off and the
  red one on) switched on behind the room's directional light, and it showed during every fade.
  The outgoing and incoming directional light cross-fade. A point light fading in waits for a
  free slot (at most three in use); meanwhile the outgoing ones fade twice as fast (a short dip).
  The on-screen test of `flg & 0x10` is not applied.
* Fog: density fades for on/off; colour eases (8% per game frame) and follows the denser fog
  faster; at a cut change the range eases from the one shown (the difference shrinks 8% per game
  frame), otherwise it follows "Fog like the fixed cameras" (below) every frame.
* Screen filter: alpha fades over 0.8 s. Not faded: the per-cut background colour of
  `ts_flg & 0x200` rooms.
* When our camera stops, lights are recomputed and the fog is re-applied (`gm_flg 0x10`).

**Fog like the fixed cameras** (both settings; per cut the range changes at once). The game's fog
is linear in view depth (`ps2_NaFog.c`: `njGenerateFogTable3` fills `fNaFogTbl` from the cut's
`fog_nr`/`fog_fr`, `njCalcFogPower` looks it up by density / depth; 883 of the 1000 cuts with fog
start it at the camera, a near value behind it). It was authored for the fixed cameras, which
stand far off; ours is close behind the player. Measured (`RECVX_ATMOS_TRACE`, the player's depth
from both cameras; same spot, our view along the fixed camera's): the player at 35 to 115 units
from the fixed camera, at 15 from ours, so the player had a quarter to a tenth of the fog (graveyard
0/2: 0.61 fixed, 0.17 ours; Antarctic exterior 6/0: 0.62, 0.07) and the scenery around them as
little. Not the cause: smooth's range was the cut's own (no averaging), its table was made every
frame, and the field of view does not enter (the depth is the view's z).

`tps_atmos` therefore moves the range towards our camera by the player's depth from the cut
camera (`cut_cam`, above; the camera-relative shift of `flg & 0x40/0x80` included, `cam.fog_*`)
less their depth from ours: the player gets the fog the fixed camera gives them, what is near
them about the same, what is farther more, as the fixed camera would show the same surfaces
from a view along ours. The shift is divided by `pc_fog_scale()` (Fog intensity stretches all
depths), so the option scales it as before and the player's fog matches at any setting. A near
value that ends up behind our camera means fog from the camera on, which `njGenerateFogTable3`
cannot express (it starts the fog at the camera then); `fog_table` fills the table itself. The
fixed camera, events and first person keep the game's fog.

Result (`scratch/tpsfog`, not in the repository): the fog fraction of the whole picture, from two
renders of the same frame with white and black fog, averaged 0.50 fixed, 0.24 before and 0.56
after over ten matched views in 0/2, 0/5, 0/6, 4/5 and 6/0; at Fog intensity 50 (graveyard) 0.28
fixed, 0.12 before, 0.31 after. Our view shows more of what lies beyond the player, so a picture
can come out a little foggier than the fixed camera's. Rooms that use black fog as darkness (the
barracks 0/6) get as dark around the player as with the fixed camera, and farther away darker.

**Under-water views** (both settings). Cut flag `0x400` marks a cut whose camera looks in from
under the water: only the submarine dock (RM_4030 and RM_8050, cuts 0, 2 and 5 of 6, whose
cameras sit in the water outside the windows). `bhSetCut` turns it into `cam.flg 0x20`; the
room's effect 110 of type 1 (`effsub1.c bhEff110`) follows it every frame: the frame is drawn
into a render texture (`StoreRenderTex`) and back as a waving 16x16 grid
(`bhDrawFullScreenRenderTexture`, `gm_flg 0x100`), stretched to 1.25 times the height. That
look belongs to the fixed camera's place; ours is always in the room's air (there is no
diving), so while it is the view (`tps_own_view`, asked before `tps_camera` takes over that
frame) the effect stays off. `cam.flg` itself is unchanged, so the fixed camera, the F5
toggle, events and first person show it as the original. Effect 110 of type 0 (room 1/5) does
not follow the cut and is unchanged. The other per-cut screen layer, the
camera vignette (`fil_no`), is a lens effect of every camera and stays (above). `RECVX_ATMOS_TRACE`
shows the cut's flag (`eff110`) and whether the distortion is drawn (`drawn`).

## Effects (blood, hits)

* Creation does not depend on the camera (`bhEne01_SetBlood`, `bhEne_SetBlood*`,
  `bhSetGunSplash`). Drawing uses `cam.mtx`; the camera-dependent parts are the cut camera values
  seen by game logic (`tps_after_cut`) and the lens blood (`bhEff301`, follows our camera).
* Culling: `njDrawTexture3DEx` drops a quad with a vertex behind the camera or all vertices
  off screen (`_Clip_Screen`); its window is widened by `pc_hor_scale()` like the clip volume,
  so effects in the widescreen margins are drawn. Particles and the ordering table cull nothing.

## Free-aim pose

* The weapon motions (file `wpnr_no + ply_id*30 + 20`, motions 100+: 100 raise, 101 fire, 104
  hold, 116 reload, up/down at +5/+10) lack up/down variants for some weapons. Free aim keeps the
  straight pose (`tps_pad` clears the up/down bits) and `tps_calc_model` runs `bhCalcModel`,
  turns the world matrices of nodes 2 (spine) and 3 (chest) by half the pitch each about the
  model's x axis, marks both `O_WORK flg 1` and runs `bhCalcModel` again, so arms, head, hair and
  weapons follow. Node angles are untouched (no feedback into the motion blend). The bend eases in
  and out (0.35 per frame).
* The pitch is measured from the weapon hand (node 9) of the unbent pose to the target. Muzzle
  flash, knife hits and attachments use the bent matrices; the shot ray is the game's (`wax`).
* **Shots on walls** (`aim_at_crosshair`): enemies are aimed from the enemy-test origin (11 units
  up); when the crosshair target is a wall, the aim starts at the muzzle, so wall hits land on the
  crosshair.
* **Another model as "Character Model"** ([ACTORS.md](ACTORS.md) "You appear as"): the weapon is drawn
  in the model's hand, so the bend's pitch is taken from there (`actor_aim_offset`: drawn minus
  real hand, in the frame of the real chest). The shot stays the real skeleton's
  (`actor_hands_back`). With the retargeting ([ACTORS.md](ACTORS.md) "Retargeting") the pitch is
  then solved on the weapon as it will be drawn (`actor_aim_hand`: the drawn hand's position and
  rotation in the frame of the real chest, which the bend turns): up to four steps of bend, measure
  the barrel's pitch against the line from its muzzle to the crosshair target, correct. The barrel
  runs from the muzzle (`wp_fps1`) on to `wp_fps2` where that point lies beyond it (the grenade
  launcher's, 45 degrees off the hand's x axis), else along the hand's x axis (the other weapons:
  their second point is the muzzle again, on that axis, unset or the rocket launcher's back end).
  Measured on the x axis, the grenade launcher was held 44 degrees up with any model. The bend
  swings a hand far from the chest (a tall model's) off the line the unbent pose measured; the drawn
  barrel now stays within about 0.35 degrees of the target (before: up to 1.5 degrees on a far wall,
  11 on a near ceiling). The camera's height, shoulder offset and distance scale with the model's
  head height (`actor_cam_scale`). Nothing changes without a model.
  `actor_aim_hand` is the hand as drawn in the previous tick, so the solve runs only while the aim
  pose stands still: the weapon raised (`mode2` 65) for two ticks without a motion blend, never
  for the knife (it has no barrel). Its correction to the hand's pitch eases in (0.35 per frame)
  and is kept through raising, shots, recoil and slashes. Solved every tick on a moving hand, it
  bent the upper body back and forth from one tick to the next (the twitch when raising a weapon,
  firing or slashing). A step that does not bring the barrel closer to the line ends the solve with the best
  step so far: for a target a few units from the muzzle (an enemy stepping into the crosshair) each
  step moved the muzzle as much as the line, the solve swung to 50 degrees and the upper body jumped
  up for a tick and settled back.
* Sniper rifle and linear launcher switch to the game's scope (first person, [Scope](#scope)); no bend.

### Aim height

The enemies' damage code reads the player's aim height (`plp->at_flg & 0xE`), not only where
the shot went: zombies and the anatomist lying on the ground (`en01.c`/`en26.c`, `exp0 + 0x44 &
0x40`), a knocked-down Hunter (`en05.c`, motions 37/38) and Cerberus and Bandersnatch in one of
their states (`en04.c`, `en09.c`) take no damage from a straight shot (the bow gun and the
grenade, rocket and linear launchers excepted); a shotgun or grenade launcher blows a zombie's
head only aimed up at close range. In the original you aim down at a crawling zombie. Free aim
kept the straight pose and with it the straight aim height, so its hits on lying or crawling
zombies were thrown away.

* `aim_at_crosshair` sets the aim height from the free aim after the mode handler: up when the
  shot rises more than 22.5 degrees (half the original's 45), down when it drops more than that or
  goes at anything lower than 6 units above the player's floor (a crawling zombie, legs, the
  floor), else straight. The enemies read it on their next step (`bhControlEnemy` runs before
  `bhControlPlayer`).
* `tps_player_mode` (a hook before the mode handler in `bhControlPlayer`, and when third person
  stops) puts it back to straight, so the weapon modes keep the straight motions and the shot
  (`bhCheckGunAtari` copies `at_flg`) keeps the straight test; an enemy that cleared `at_flg`
  meanwhile (grabs) is left alone.
* With "Aim Assist" the game's own aim height plays: W / S press the up/down bits as the
  d-pad does (`0x21` / `0x42`).

### Dual weapons

The Gold Lugers and the sub machine guns (`WpnTab flg 0x800`, `playpch.c`) fire one shot per arm:
the right (hand 9) along `ay + arm_dr`, the left (hand 13) two ticks later along `ay + arm_dl`,
each from its own muzzle (`CheckGunHit`), and `PlyPchMain` turns each arm by `arm_ar` / `arm_al`,
eased towards those. The game sets the two offsets only at its lock-on (`SetLockOnDirection`:
raising the weapon, L1): with two enemies in front it turns the character between them and gives
each arm one. Free aim then turned the character to the crosshair and the offsets stayed, so the
arms were held outwards (20 and 27 degrees with a second zombie 45 degrees to the left) and both
shots went past the zombie under the crosshair, which took no damage.

Free aim now sets them every tick (`dual_aim`, after `aim_at_crosshair`; not while lowering, mode2 68):

* One arm aims at the crosshair target. The other aims at the nearest other enemy within 45 degrees
  of the crosshair direction and 15 degrees of its pitch, in the weapon's range, that the game's own
  search would take (alive, targetable, not hidden or behind a wall from that arm's muzzle,
  `bhSearchEnemy`'s tests) and that takes the same aim height as the crosshair target (one pitch,
  `wax`, and one aim height, `at_flg`, serve both arms). The arm on that enemy's side takes it, as
  the game gives the left arm the enemy further left; the head follows the other one (`hed_rate`).
  The enemy of the last tick is kept while it stays within 1.15 times those limits, so two enemies
  at the same distance or at the edge do not take turns (only from one aiming tick to the next in
  the same room: after a gap, a room change or another weapon the choice starts afresh).
* Without such an enemy both arms aim at the crosshair target, so the guns converge to the front.
* Each arm's yaw is taken from its own muzzle (last tick's hand, turned with the character since)
  and kept within 45 degrees; the arms ease there a third of the way per tick (`count` 3).
* Shots therefore go where each arm points; the pitch, bend and aim height are the crosshair
  target's as for single weapons, so lying zombies are hit the same way. The fixed cameras and
  "Aim Assist" with the mouse keep the game's own dual targeting.
* With [Precise Shots](#precise-hits) the other arm takes that enemy only if its own shot, turned to it,
  would hit it (`dual_aim`, `pc_dual_take`): the ray `CheckGunHit` casts, from that arm's muzzle at
  chest height along `Rx(wax) Ry(ay + its yaw)`, must enter the enemy's joint volumes before any other
  enemy's and before a wall. A second enemy beside that ray (other height or distance than the
  crosshair target, as the pitch is shared), behind another or behind a wall is not taken, and both
  arms aim at the crosshair. Shots: the arm at the crosshair is tested along the crosshair's ray, the
  turned arm along its own ray (`pc_precise_hit`: the shot's yaw `way` is the turned arm's).
  Measured (two zombies spawned in 3/0, SMG and Gold Lugers): 15 degrees apart the other arm turned
  23 times and hit the second zombie 26 times along its own ray; with the second zombie at a lower
  pitch, never; beyond 45 degrees, never. Off: as before Precise Shots (pose and health traces
  identical to that build). The fixed cameras run none of this: dual-weapon runs with one and two
  zombies are frame- and health-identical to v0.6.0.
* Co-op: each arm's hits reach the partner as any hit (`EV_ENE_HIT` per enemy hit, with the aim
  height). The partner draws our motions and our aim pitch (the bend, [COOP.md](COOP.md#pose-and-the-partners-model)),
  without the arms' turn.

## Precise hits

Setting "Precise Shots" (THIRD PERSON page, ini `tps_precise_hits`, on by default). Off, and with the
fixed cameras, the game's test as before (frames identical to the build without it).

**How the game hits** (`weapon.c bhCheckGunAtari`, measured with `RECVX_TPS_TRACE=2` at a zombie
spawned in the lift hall 3/0, 10 to 40 units away): the shot is a capsule from the player's chest
(`py + 11`, 3 + `r` ahead), `r` = 2 units plus `rn` per unit of distance, at most `rmax` (handguns
and sub machine guns 0.4 / 4, the magnum 0.2 / 1, the shotgun 0.3 / 40), tested against the enemy's
one hit capsule (`watr`: for a zombie the whole body, 2.5 units thick). The joint damaged is the
bone nearest the point where they touch, so the game does know head, arms and legs, but from where
the cone touched the big capsule, not where the shot went. There is no lock-on in free aim, but the
cone does the same: with the crosshair beside a zombie's body (the gap between the crosshair's ray
and the nearest part of the body, the volumes below), the handgun hit 64 of 66 shots within 0.5
units, 60 of 69 at 0.5 to 1, 57 of 68 at 1 to 2 and 15 of 23 at 2 or more. Only the
sniper rifle and the bow gun (`WpnTab flg 0x8`) test the enemy's joint capsules with a thin ray.

**On** (`tps_glue.c pc_precise_hits`, `pc_precise_hit`, `pc_ray_capsule` in `pc_rules.h`), for the
player's hitscan shots (`WpnTab flg 0x10000000`, not the projectiles `0x20000000`) with our camera
and free aim (not "Aim Assist" with the mouse: its lock-on needs the wide test), and in the scope:

* **The ray**: the crosshair's (from the shoulder point along the view, to the first wall; cast by
  `aim_at_crosshair` the tick before, which turned `ay` / `wax` to it), as seen. The game's ray starts
  at the chest, lower and beside it, and crossed the crosshair's only at its target: aimed just past
  an enemy at a far wall it went through the enemy. In the scope the game's ray, which is the view's.
* **The volumes**: the enemy's joint table (`pp->cpcl`, the game's own for the sniper rifle and bow
  gun: capsules between two bones and spheres on a bone, on the bones as posed this frame, so they
  follow walking, crawling, lying and attacks). A zombie: hips to chest (radius 1.6), two chest
  spheres (1.8), neck (0.6), head (1.1), upper and lower arms (0.6, 0.5), thighs and shins (0.9, 0.8);
  the Tyrant 1 to 2.5, the Gulp Worm 5.5. Chosen over shrinking `watr`: the game already sized them to
  the models and they carry the joint. Enemies without a table (fish, ants, the Spotter, Alexia's
  second form, Alfred, the knights, the ceiling trap) keep their hit capsule, tested with the thin
  ray, the joint the game's nearest bone. The hit is the first volume the ray enters (exact
  ray-capsule test); its joint takes the damage, its entry goes to `cpcl_no` as in the game's test.
  The game's later checks stay: a wall between the gun and the point hit stops the shot.
* **Measured** as above with it on: 0 of 674 shots with the crosshair beside the body hit (0 to 0.5
  units: 0 of 131, 0.5 to 1: 0 of 223, 1 to 2: 0 of 244, 2 and more: 0 of 76); 70 of 75 on the body
  hit (the other 5: a wall between the gun hand and the arm hit). Lying zombies (0/6) and crawlers
  (legs shot off with the shotgun) are hit where aimed; the Tyrant, Bandersnatch and Cerberus on their
  bodies and heads, 6 units beside them not.
* **Shotgun** (weapon 11; the game tests one cone per enemy, with no pellets): the volume under the
  crosshair if the ray meets one, else the volumes 0.3 units wider plus 0.06 per unit of distance (3.4
  degrees either side), at most 3. Every enemy in it is hit, each with its own hit point, joint and
  distance (the game gave all of them the nearest one's).
* **Dual weapons**: the second gun turns to another enemy only when its own ray would hit it
  ([Dual weapons](#dual-weapons)); each shot tested along its gun's ray.
* **Not changed**: the knife (`bhCheckKnifeAtari`), the bow gun's arrows, the grenade, rocket and
  linear launchers (projectiles and blasts: `bhCheckFlyAtari`, `bhCheckBombAtari`).

**Heads.** A hit whose joint is the head does 150 % of its damage (`HEAD_DAMAGE`, `pc_precise_dam`) on
zombies and the anatomist (joint 11 / 13), Cerberus (5), the Bandersnatch (4), the Tyrant (5),
Alexia's first form (5) and Steve (5). Not: Nosferatu (its weak spot, below), Alexia's second and
third form, the Gulp Worm (its table already gives its head +30 %), the Hunter (head and chest are one
volume), types without a table. A zombie's head burst (`en01.c bhEne01_CheckExpHead`,
[ENEMIES.md](ENEMIES.md#head-blow-offs)) is the game's for weapons with flag `0x10` (magnum, shotgun,
sniper rifle, the Gold Lugers), but for a precise hit (`pc_precise_head`: the zombie's last gun hit was
precise, with this weapon) only that hit on the head counts: not head damage from earlier shots, the
magnum's near hit on the hips or chest or the shotgun aimed up within 10 units. Measured: the magnum
at the hips 14 units away burst the head with the game's test and does not with this; on the head
it bursts at 14 and at 27 units, the shotgun too. The grenade launcher and bow gun keep the game's rule.

**Bosses.** Nosferatu (Battle Game 5/80): the sniper rifle on its core (volume 0) still does five
times the damage (`en15.c`, `cpcl_no` 0) and kills it as with the game's test. The final Alexia
(5/81) still falls to the linear launcher (a projectile, unchanged). The Gulp Worm (5/83) is hit
where it shows.

**Co-op**: the shooter decides as now and sends its damage per joint with `EV_ENE_HIT`; the flag
`COOP_HIT_PRECISE` (protocol 21) says its hit was precise, so the side running the zombie bursts its
head by the same rule (checked: the guest's magnum on the head bursts it on both sides, on the hips
it does not, with the guest's option off it does as before).

## Limits

* **Room geometry**: some corners have no wall, ceiling or back side; looking there shows the
  clear colour. With the back to a wall the camera slides forward up to the shoulder, so the
  character can leave the picture (view and aim stay put).
* Aim poses: free aim bends the straight pose up to 60 degrees of pitch. No laser sight.
* The crosshair goes down to 60 degrees: from the shoulder (16.5 units up) it meets the floor
  about 9.5 units ahead, so a crawling zombie nearer than that cannot be put under it (the shot
  goes at the floor behind it; with "Precise Shots" off the game's wide hit test may still reach it).
* Co-op: enemies judge a hit with the aim height of the player they are after; the partner's
  stand-in carries none, so every hit counts on an enemy after the partner.
* Near walls: a wall closer than 40 units on the crosshair ray counts as 40 units away for the
  character's yaw/pitch (else the shoulder offset would turn it by tens of degrees); the shot
  then hits a little beside the crosshair. Enemies, and walls farther away, are hit exactly.
* The crosshair marks the screen centre. Target selection uses enemy hit capsules and the
  room's collision walls, not every visible prop.
* Mouse turning rotates the character directly (no turning animation while standing). Not
  while climbing stairs/ladders, pushing or in other special moves.
* Events, cutscenes, door transitions, first person/scope and menus use the original camera;
  so does an event without its own camera (`mode0 == 7`, rare).
* Messages, examine texts, pick-up prompts and the fades to the inventory, map, files and
  options keep the third-person view; the camera holds still while the game is frozen.
