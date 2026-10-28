# User guide

How to start the game, play it and change its settings, alone or in co-op. Documents named here
(COOP.md, THIRDPERSON.md, ...) are in the project's `docs` folder:
<https://github.com/aikatore/recvx-enhanced-port/tree/main/docs>.

## Requirements

- Your own disc image of *Resident Evil Code: Veronica X*, PS2, US release (**SLUS-20184**), as an
  `.iso` file. Other regions are not supported. The image is read in place, nothing is copied.
- **Windows** 10 or later, or **Linux** with glibc 2.34 or newer and the 32-bit (multilib) OpenGL,
  X11/Wayland and audio libraries.
- A graphics card with OpenGL 3.3.

## First start

1. Unpack the download and run `recvx.exe` (Windows) or `./recvx` (Linux).
2. A file selector asks for the disc image. Pick your `.iso`; the game remembers it.

You can also pass the image on the command line (`recvx game.iso`) or drop it onto `recvx.exe`.
On Windows a console window next to the game shows the log.

### Where your files are

Everything lives next to the program. The download comes with empty `mods/` and `mods/textures/`
folders.

| Path | Contents |
|---|---|
| `save/recvx.ini` | settings, key bindings, the disc image path, your co-op name |
| `save/saves/` | your saves, one file each |
| `save/card0/` | the emulated memory card (Save System: Memory Card only) |
| `mods/` | mods and HD textures (`mods/textures/`) |
| `recvx.log`, `recvx-crash.log` | logs ([Troubleshooting](#troubleshooting)) |

If the directory you start from has a `save/` folder, that directory is used instead (for `mods/`
and the logs too); `--save-dir` picks another save folder. Two copies of
the game with different save folders keep separate settings and saves.

### Command-line options

| Option | Effect |
|---|---|
| `recvx PATH.iso` | use this disc image |
| `--save-dir DIR` | where saves and `recvx.ini` go |
| `--windowed`, `--borderless`, `--exclusive`, `--fullscreen` | start in that display mode |
| `--size WxH` | window size (default 1280x960) |
| `--hd-textures DIR` | HD texture pack folder (default `mods/textures`) |
| `--log[=FILE]` | write the whole session to `recvx.log` (or FILE) |
| `--version` | print the version and exit |

They override the saved settings for that run. All options: ARCHITECTURE.md, "Command line".

## Controls

Everything can be rebound ([Rebinding](#rebinding)). The tables show the defaults with the game's
KEY CONFIG type A; other types move the game's actions between the PS2 buttons.

### Fixed cameras (the original view)

| Action | Keyboard | Gamepad (Xbox / PlayStation) |
|---|---|---|
| Move | arrow keys | d-pad, left stick |
| Run (hold) | X | B / Circle |
| Action, confirm | Z | A / Cross |
| Cancel | X | B / Circle |
| Aim (Hold) | W | RB / R1 |
| Fire (while aiming) | Z | A / Cross |
| Free Aim (hold) | E | LT / L2 |
| Next target (while aiming) | Q | LB / L1 |
| Reload | R | hold RB / R1, press Y / Triangle |
| Quick Knife (Hold) | Space | RT / R2 |
| Quick Turn | back + run | back + run |
| Inventory | Enter | Menu / Start, right stick click |
| Map | S | Y / Triangle |
| OPTION | Backspace | View / Back |
| Sniper scope zoom (hold) | Q | LB / L1 |
| Trade (Co-op) | T | left stick click |

On the keyboard, 1 and 2 are the PS2's L2 and R2. Walking into stairs climbs them (Auto-climb
Stairs).

### Modern controls (fixed cameras)

**Fixed Camera Controls** (CONTROLS page):

- **Classic**: up walks forward, left / right turn, as on the PS2.
- **Modern**: the stick, d-pad or arrow keys point where you walk on screen;
  pushed far, the stick runs. Holding the direction through a camera cut keeps you going the same
  way until you change it or let go.
- **Combined** (default): the left stick and the arrow keys play Modern, the gamepad's d-pad plays
  Classic. Choose Classic for tank controls on the keyboard.

Aiming is the same in every scheme. With Modern, bind the **Quick Turn** row (unbound by default).
The schemes do not apply in third person, and in co-op change only your own player.

### Aiming without auto-aim

The aim button turns you to the nearest enemy. **Free Aim** (E, the left trigger) raises
the weapon without that turn: you keep your facing and aim yourself. Fixed cameras only.

### Reload

Reloads the equipped weapon from your inventory without opening it, while aiming or with the weapon
lowered.

| Mode | Keyboard | Gamepad |
|---|---|---|
| Fixed cameras; third person with Classic gamepad controls | R | hold RB / R1, press Y / Triangle |
| Third person, Modern gamepad controls | R | RB / R1 |

Not for the grenade launcher and the knife. The sniper rifle and linear launcher reload only in the
scope. A reload binding may share a button with another action: then it reloads only while you hold
aim (that is how Aim + Triangle works).

### Quick knife

Hold the quick-knife key (Space, the right trigger) to draw the knife without the inventory; let go
to get your weapon back.

### Mouse

**Mouse In Menus** (on): point and click in the inventory, item box, map, files, save screens,
OPTION and the game's questions. Right click goes back. When examining an item, drag to rotate and
scroll to zoom.

**Mouse buttons with the fixed cameras** are unbound by default. Bind them in CONTROLS, for example
right button to Aim (R1) and left to Fire / Confirm (Cross), or right to Free Aim. They act
only while you control your character.

### Third person

An over-the-shoulder camera with free aim, toggled with **F5**. Events, doors and menus keep the
original camera. Details: THIRDPERSON.md.

| Action | Keyboard and mouse | Gamepad, Modern (default) |
|---|---|---|
| Move | W / S / A / D | left stick (pushed far: run) |
| Camera | mouse | right stick |
| Run (hold) | Shift | X / Square |
| Aim (Hold) | right mouse | LT / L2 |
| Fire | left mouse | RT / R2 |
| Action, confirm | E | A / Cross |
| Cancel | Q | B / Circle |
| Reload | R | RB / R1 |
| Quick Knife (Hold) | Space | LB / L1 |
| Quick Turn | S + Shift | d-pad down, or stick back + run |
| Inventory | Tab | Y / Triangle |
| Map | M | View / Back |
| OPTION | Backspace | Menu / Start |
| Camera behind you | | right stick click |
| Scope zoom | Shift (hold) | right stick click |

### Third person with a gamepad

**Gamepad Controls** (THIRD PERSON page):

- **Modern** (default): the table above, as in the Resident Evil 2 and 4 remakes. With
  **Aim Assist** on you get a gentle aim assist instead of the lock-on.
- **Classic**: tank controls with the game's buttons, the camera behind you. Aim with RB / R1; the
  sticks and d-pad then move the crosshair.

Menus and events use the game's buttons. The Modern buttons are bound separately (CONTROLS,
Gamepad, "Third Person: Modern Gamepad").

### Vibration

A rumble gamepad vibrates as the DualShock 2 did. Turn it on or off in the game's **OPTION ->
VIBRATION**; it is greyed out until a rumble gamepad is connected. **Vibration Strength** (CONTROLS)
sets how strong.

### Rebinding

In **OPTION -> ENHANCED SETTINGS -> CONTROLS** (DEVICE picks keyboard or gamepad) or **F1 ->
Controls** (click a binding, press the key; right click clears). Each action has two slots. While
waiting for a key, Esc cancels and Delete clears. A key another action uses is swapped to it.
`recvx.ini` stores them as `key_...` / `pad_...` lines (ARCHITECTURE.md, "Binding ini format").

### Input source and local co-op

| Setting (CONTROLS) | Default | What it does |
|---|---|---|
| Input Devices | Auto | Auto: keyboard, mouse and gamepad while the window has focus. Keyboard and Mouse. Gamepad Only: keys and mouse do not play; the pad works without focus |
| Gamepad | Any | Any: the first pad. 1-4: the pad with that player number (Xbox slot, PlayStation / Switch light) |

Hotkeys always work, so F1 gets you back. Two windows with Gamepad Only (or one per device) give
local co-op on one PC (COOP.md, "Local co-op on one PC"). With Steam Input on, Steam may send its
pad only to the focused window: turn Steam Input off for the game.

### Hotkeys

| Key | Action |
|---|---|
| F1 | settings overlay |
| F2 | cheats and tools |
| F3 | co-op window |
| F5 | third person on / off |
| F11 | windowed / fullscreen |
| Page Up; F or Page Down | fly up / down (noclip cheat) |

## Settings

Two menus change the same values:

- **OPTION -> ENHANCED SETTINGS**: in the game's OPTION screen (title, or Select in game), select
  EXIT and press right. Q / W or LB / RB switch pages; left / right change a value. The bottom lines
  explain the selected setting; "(restart needed)" means after a restart.
- **F1**: the same as an overlay, plus a Performance section.

Changes apply at once and are saved automatically. The defaults favour the PC (smooth frames,
widescreen, no intro or door animations, fast loading); turn them off for a PS2-like experience.

### Display

| Setting | Default | What it does |
|---|---|---|
| Display Mode | Windowed | or Borderless Fullscreen (quick Alt+Tab) or Exclusive Fullscreen. F11 switches |
| Fullscreen Resolution | Desktop | exclusive fullscreen only |
| Window Size | 1280 x 960 | presets up to 3840 x 2160, or resize the window |
| VSync | On | |
| Frame Rate Limit | Display Rate | 30 to 144 frames shown per second; the game runs at 30 anyway |
| Show Frame Rate | On | counter at the top right |
| Aspect Ratio | Widescreen | 3D fills the window, 2D and menus stay 4:3. Or Original (4:3), Stretch |
| Internal Resolution | Auto | Auto follows the window, or 1x to 8x of 640x448 |
| Smoother Motion | On | smooth in-between frames up to your monitor's rate; gameplay stays at 30 FPS. Off: "vanilla" |
| Smooth Menus | On | menus at the display rate too, quicker to react |
| Motion Trails | Off | the PS2's motion trail. Auto: only in vanilla |
| Cutscene Bars, Camera Vignette, Screen Effects | Full Width | in widescreen: Original (4:3), Full Width or Off |
| Movie Zoom | Off | enlarges movies in widescreen to cut their black bars, never stretched |
| Fog Intensity | 100% | 100% as on the PS2, 50% twice as far away, 0% none (also the black fog of dark rooms) |
| Brightness, Contrast, Gamma | 0, 100%, 1.0 | picture calibration; a test pattern shows while one is selected |
| Reset Picture Calibration | | back to neutral |
| Hardware 3D Rendering, Texture Caching | On | faster drawing; leave on |
| Hide Edge Models | On | as on the PS2. Off: models no longer pop in at the screen edges |
| HD Textures | On | use a pack in `mods/textures` (next start), see [HD texture packs](#hd-texture-packs) |
| HD Font | Off | also the pack's font |
| HD Menus | On | also replacements that redesign menus, title and inventory |

Calibration: turn Brightness until the left square on the black strip is barely visible, Contrast
until the right square on the white strip is.

### Gameplay

| Setting | Default | What it does |
|---|---|---|
| Skip Intro | On | boot straight to the title (next start). Off: any button skips each logo and movie |
| Check For Updates | On | see [Update check](#update-check) |
| Skip Door Animations | Instant | Off: played. Instant: a short load screen instead. Skippable: any button ends them |
| Fast Loading | On | room changes without the PS2's disc waits |
| Battle Game Unlocked | On | Battle Game from the start, all five characters; saves and records unchanged |
| Difficulty Select | On | see [Difficulty select](#difficulty-select) |
| Quick Knife | On | see [Quick knife](#quick-knife) |
| Auto-climb Stairs | On | walk into stairs to climb them. Ledges, doors and items still need the button |
| Zombie Decapitation | On | a zombie whose head bursts (magnum, close shotgun) loses it |
| Stack Rifle Ammo | **On** | rifle bullets stack in one slot |
| Save System | Separate Files | see [Saves](#saves) |
| Import Memory Card Saves | | copies the card's saves into save files |
| Free Typewriter Saves | Off | typewriters save without an ink ribbon |
| Extra Enemies | Off | up to 1, 2 or 3 more per room, of kinds the room has |
| Rearranged Enemies | Off | free-roaming enemies start elsewhere in their room |
| Enemy Randomizer | Off | enemies become other kinds; bosses stay |
| Randomizer Variety | Wild | Wild: any kind that fits. Mild: two common kinds per room |
| Randomize Event Enemies | On | event enemies change too |
| Enemy Seed | 0 | the same seed gives the same enemies (and the same cutscene cast) |
| Randomize Cutscene Cast | Off | see [Cutscene cast](#cutscene-cast): the story characters as other models in cutscenes |
| Model Selection | Cutscene Models | where the characters' new looks come from |
| Model Assignment | Per Character | Per Character: the same model in every cutscene. Per Cutscene: each cutscene room picks again |

Enemy options apply from the next room (ENEMIES.md, "Enemy options"). In co-op the host's enemy
options and ink ribbons count for both.

#### Cutscene cast

GAMEPLAY, under CUTSCENE CAST (and in the co-op lobby: GAME RULES, RANDOMIZER, CUTSCENE CAST). In
cutscenes the story characters are shown as other models; nothing else changes: the scenes, the
voices and what happens stay as they are.

* **Randomize Cutscene Cast**: On swaps the story characters (Alfred, Alexia, Steve, Rodrigo, Wesker,
  and Claire or Chris while you play the other one) in cutscenes; a
  character who stays in the room afterwards keeps the new look.
* **Model Selection**: *Cutscene Models* picks among the characters' cutscene models: their mouths
  move with the lines and their eyes look where the original character's do (the voice stays the
  original character's). *All Models* also uses the player models, the creatures and the zombies
  (all zombie looks count as one choice, so zombies do not crowd out the rest); most of those keep a
  still face. A model with hands of its own shows them instead of the character's gloves. Claire's
  and Chris's looks are never given to someone else: how they look is up to "Character Model".
* **Model Assignment**: *Per Character*: e.g. Steve is shown as Alfred in every cutscene, and no two
  characters share a model. *Per Cutscene*: each cutscene room picks again. Both come from
  the enemy seed: the same seed gives the same cast.
* Your own character is not part of it: how you look in cutscenes follows APPEARANCE ("Character
  Model", "Custom Cutscene Model"). Alexia asleep in her capsule keeps her own look.
* In co-op the host's settings and seed count, so both players see the same cast.

### Controls

Input Devices and Gamepad ([above](#input-source-and-local-co-op)), Vibration Strength (100%), Mouse
In Menus (on), Fixed Camera Controls (Combined, [Modern controls](#modern-controls-fixed-cameras)),
then the bindings.

### Third person

| Setting | Default | What it does |
|---|---|---|
| Over-the-shoulder Camera | Off | also F5 |
| Mouse Camera Style | Behind Character | Behind Character: the mouse turns you. Free Camera: the mouse orbits the camera, W/A/S/D walk relative to it |
| Lighting and Fog | Smooth | fades when the room's camera cut changes. Original: changes with each camera cut, as the original |
| Aim Assist | Off | Off: free aim. On: the game's lock-on (gamepad: aim assist) |
| Precise Shots | On | free-aim shots hit only where the crosshair meets the body; head hits do more damage |
| Gamepad Controls | Modern | see [Third person with a gamepad](#third-person-with-a-gamepad) |

Also: field of view, camera distance, height and shoulder, mouse and stick speeds, invert Y,
crosshair and stick dead zones. Defaults: THIRDPERSON.md, "Settings".

### Audio

Music And Voice, Sound Effects: 0-100%, default 100%.

### Appearance

| Setting | Default | What it does |
|---|---|---|
| Character Model | Current Character | play as another character, creature or zombie; your co-op partner sees it too. "Current Character (Cutscene)": the character you play in its cutscene model (follows character switches; also over other costumes) |
| Match Model Animations | On | a male model moves like Chris, a female one like Claire |
| Custom Cutscene Model | On | Off: cutscenes show the real Claire or Chris |
| Animated Faces (Experimental) | Off | the swapped model moves its mouth |

### Co-op

On the CO-OP page and in F3. HOST RULES settings count only on the host. Details: COOP.md, "Settings".

| Setting | Default | What it does |
|---|---|---|
| Partner Name Tag, Show Ping | On | |
| Hide IP Addresses | Off | for streaming: addresses show as `xxx.xxx.xxx.xxx` |
| Follow Host | On | go through every door the host takes |
| Loot Sharing | Shared | Shared: each item once, split the supplies. Individual: a copy each |
| Duplicate Weapons and Ribbons | On | shared loot: weapons and ink ribbons for each player |
| Skip Cutscenes Together | On | skipped once both pressed skip |
| Game Over Rule | Each Player | Both: one game over is both players' |
| Metal Detector | Skipped | the prison's detector lets you through with weapons |
| Quick Start | Off | start with the manual read and the knife in the bag |
| Pause Enemies During Trades, Remember Partner Items | On | |
| Connection | Automatic | or Direct, Server Relay |
| Host Port, Automatic Port Forwarding | 7140, On | |

## Difficulty select

With **Difficulty Select** on, NEW GAME asks for the difficulty, as the European and Japanese
releases do:

| Difficulty | What changes |
|---|---|
| Normal | the original game; the only one with the end ranking and its reward |
| Easy | supplies in the item box, more ammo per pickup |
| Very Easy | as Easy, plus twice the health, stronger healing, weaker enemies, an endless ink ribbon and extra weapons |

Saves keep their difficulty (Easy green, Very Easy red in the list). In co-op the host picks it at
START in the lobby.

## Saves

With **Save System: Separate Files** (default) each save is a file in `save/saves/`, such as
`001_Claire_Prison-front.sav`: up to 10,000 saves, no memory card screens. Saving adds a new entry at the end
of the list, or overwrites one. Loading starts on the newest save.

- Renaming the part after the number is fine; deleting a file deletes the save.
- Back up by copying the `save` folder. `system.dat` holds options, unlocks and best times.
- On the first start, memory card saves are copied in; the card keeps them.
- **Memory Card** brings back the 15 slots. The two systems do not see each other's
  saves; **Import Memory Card Saves** copies the card's saves again.

File format: ARCHITECTURE.md, "Save system".

### Back to the title and skipping the ending

- In a game, OPTION has **RETURN TO TITLE**: select DEFAULT and press right. It asks first. Unsaved
  progress is lost; in co-op the partner plays on.
- Any button skips the ending movie, another the credits. Results, unlocks and rank stay the same.

## Co-op quick start

Two players, each with their own copy of the same version, play one game over the internet or a
LAN.

1. **Host**: title menu -> **MULTIPLAYER** -> *Direct* -> **HOST A GAME**. Choose START (new
   game or a save) and GAME RULES, **OPEN THE LOBBY**, then **COPY JOIN ADDRESS** and send it.
2. Over the internet, UDP port 7140 must reach the host. Automatic Port Forwarding tries this; otherwise
   forward it in your router and allow it in the firewall. If that is impossible, use a VPN such as
   Tailscale or your own co-op server (COOP.md, "Playing over the internet").
3. **Partner**: MULTIPLAYER -> *Direct* -> **JOIN A GAME**, paste the address
   (`ip:port#CODE`) into ADDRESS, **JOIN**.
4. Host: **START THE GAME**.

F3 offers the same in a small window, also during a game. Everything else: COOP.md.

## HD texture packs

HD texture packs made for the PCSX2 emulator for SLUS-20184 work as they come.

1. Put the pack into `mods/textures/`: PCSX2's folder (`mods/textures/SLUS-20184/replacements/...`),
   only `replacements`, or the files themselves.
2. Start the game. The log says `hd textures: N replacements in <folder>`.

- **Several packs**: one folder each, named `1_...`, `2_...`. On the same texture, the first in name
  order wins.
- **Another folder**: `hd_textures_dir = <path>` in `recvx.ini`, or `--hd-textures <path>`.
- Packs with redesigned menus or translated text change those too. Turn **HD Menus** off to
  keep only replacements of the original picture.
- A room's first visit can take a moment longer while its textures are checked.
- **After changing a pack**, delete `save/hd_textures.txt`.

How it works: HD_PACK.md.

## Mods

Files in `mods/` replace disc files without touching your image. The log names each one. To remove a
mod, delete its files.

| In `mods/` | What it is |
|---|---|
| `RM_xxxx.RDX`, other disc file names | replacement or new rooms, other disc files |
| `ARCHIVE.AFS/N` | entries of the game's AFS archives |
| `enemies.ini` | custom enemies (ENEMIES.md, "Enemy variants") |
| `textures/` | [HD texture packs](#hd-texture-packs) |

Co-op partners need the same `enemies.ini`. Modding guide: modding/README.md.

## Cheats and tools (F2)

F2 (or the EXTRAS page) is for testing and fun:

- **Infinite Health**, **Heal Now**, **Infinite Ammo**.
- **Pass Through Walls**: Walls, or Walls and Flight (Page Up and F / Page Down, or R2 / L2).
- **Unlock With No Key**: examine a lock to open it as if you had its key. Doors the story keeps shut stay
  shut; some doors notice it only after you re-enter the room.
- **Items**: add any item.
- **Spawn**: any enemy in front of you, custom ones marked "[C]".
- **Teleport**: a location, room and entry. Not every room works in every story state.

In co-op only the host can spawn enemies.

## Update check

With **Check For Updates** on, the game asks the project's update server once per start for the
newest version number, in the background.

- **Sent**: one request for that number, with the game's version. Nothing else, no third parties,
  nothing stored.
- **Shown**: "Game Version" and "Latest Version" at the title's bottom right; the installed one
  turns red when a newer one is out. "..." while asking, "unknown" without an answer.
- **Off**: GAMEPLAY -> Check For Updates, or `update_check = 0` in `recvx.ini`. Then no network is
  used outside co-op, and the title shows "off".

## Troubleshooting

| Problem | What to try |
|---|---|
| "cannot open game disc image" | check the path; only SLUS-20184 images work |
| No file selector (Linux) | install the desktop portal or `zenity`, or pass the image on the command line |
| Linux: "GLIBC_2.xx not found" | glibc 2.34 or newer needed, or build it yourself (BUILDING.md) |
| Linux: no window or sound, "cannot open shared object" | install the 32-bit OpenGL, X11/Wayland and audio libraries |
| Low frame rate | lower Internal Resolution, set a Frame Rate Limit or turn off Smoother Motion; F1 -> Performance |
| Too dark | Brightness, Gamma or Fog Intensity |
| No vibration | turn on OPTION -> VIBRATION with the gamepad connected |
| Gamepad dead in the second window | Input Devices: Gamepad Only, pick the pad number; check Steam Input |
| Saves gone after changing the save system | Import Memory Card Saves, or switch back |
| Wrong HD textures after changing a pack | delete `save/hd_textures.txt` |

### Reporting a problem

Every crash writes `recvx-crash.log` into the game's folder: a few KB with the version, the crash
location and the last messages, IP addresses masked. Attach it to an issue at
<https://github.com/aikatore/recvx-enhanced-port/issues> and say what you did before. For more
detail, start with `--log` and attach `recvx.log` too (the whole session, with IP addresses).

## Editing recvx.ini

Every setting is in `save/recvx.ini` as `key = value`; edit it while the game is closed. Most are in
the menus. Only in the ini:

| Key | What it does |
|---|---|
| `iso` | the disc image path |
| `hd_textures_dir` | another HD texture folder |
| `borderless_extra` | Windows borderless: the window reaches this many pixels past the screen (default 1) so drivers keep it borderless; 0 for exactly the screen |

All keys: ARCHITECTURE.md, "Settings".
