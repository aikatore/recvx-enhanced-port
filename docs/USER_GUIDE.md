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
| `save/card0/`, `save/card1/` | the emulated memory cards in slot 1 and slot 2 (Save System: Memory Card only) |
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

### Title menu

The title menu has **LOAD GAME**, **NEW GAME**, **OPTION**, **EXTRA**, **MULTIPLAYER**
([Co-op quick start](#co-op-quick-start)) and **EXIT**, which closes the game after asking.

- **EXTRA** holds **BIORAND** (the randomizer, modding/README.md, "Randomizer (BioRand)"),
  **BATTLE GAME** and **IMPORT** (models and languages from other discs). BATTLE GAME appears once
  it is unlocked (a cleared game, or **Battle Game Unlocked** in Gameplay).
- Choose with up/down and confirm, or point and click. Cancel or a right click goes back to the
  title menu. Leaving Battle Game returns to EXTRA.

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
| Inventory | Enter | Menu / Start |
| Map | S | Y / Triangle |
| OPTION | Backspace | View / Back |
| Sniper scope zoom (hold) | Q | LB / L1 |
| Trade (Co-op) | T | left stick click |
| Weapon shortcuts ([below](#weapon-shortcuts)) | 1-8; hold C | hold right stick click |

On the keyboard, 9 and 0 are the PS2's L2 and R2. Walking into stairs climbs them (Auto-climb
Stairs). Walking or running into a box (or other pushable object) pushes it; on the PS2 only walking did.

### Modern controls (fixed cameras)

**Fixed Camera Controls** (CONTROLS page):

- **Classic**: up walks forward, left / right turn, as on the PS2: the d-pad, the left stick and
  the arrow keys.
- **Modern**: the stick, d-pad or arrow keys point where you walk on screen;
  pushed far, the stick runs. Holding the direction through a camera cut keeps you going the same
  way until you change it or let go.
- **Combined** (default): the left stick and the arrow keys play Modern, the gamepad's d-pad plays
  Classic. Choose Classic for tank controls on the left stick and the keyboard.

Aiming is the same in every scheme. With Modern, bind the **Quick Turn** row (unbound by default).
The schemes do not apply in third person (it has its own Gamepad Controls row), and in co-op change
only your own player.

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

### Weapon shortcuts

Swap weapons without opening the inventory. Each character (Claire, Chris, Steve, Wesker in the Battle
Game) has eight shortcuts, kept in `recvx.ini`, so they stay with every save and a new game.

- **Assign** in the inventory: put the cursor on a weapon and press 1-8 (again: clears it). Or press
  **Weapon Shortcuts** (C, the right stick click) there for their screen: your bag on the left, the
  eight shortcuts on the right. Choose a weapon and then a shortcut, or the other way round (confirm or
  click); cancel or right click on a shortcut clears it. Assigned weapons show their number in the
  inventory.
- **Keyboard**: 1-8 equip the weapon of that shortcut.
- **Gamepad**: hold the right stick click. Shortcuts 1-4 show around the d-pad (up, right, down,
  left); press a direction to equip one. RB (R1) switches to 5-8 and back. Let go without a direction:
  nothing happens. While you hold it the d-pad and RB do not move you or aim (the stick still moves
  you; LB keeps its own use). Holding C on the keyboard does the same with the arrow keys, E switching
  the page (outside the wheel E is still free aim, and action in third person).

A shortcut works when you could open the inventory and the weapon is lowered: standing, walking or
running. Aiming, reloading, climbing, doors, events and menus wait; a press up to half a second before
counts. A weapon no longer in your bag shows grey and does nothing. In co-op each player has their own
shortcuts and the partner sees the new weapon. Turn them off with **Weapon Shortcuts** (CONTROLS); the
keys are rebound in CONTROLS like the others.

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
| none until you set one | Save Anywhere, Item Box Anywhere, Load Anywhere (the F2 cheats Save Now, Open Item Box, Load Game) |

## Settings

Two menus change the same values:

- **OPTION -> ENHANCED SETTINGS**: in the game's OPTION screen (title, or Select in game), the row
  below VIBRATION. Q / W or LB / RB switch pages; left / right change a value. The bottom lines
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
| Internal Resolution | Auto | Auto follows the window, or 1x to 8x of 640x448. Above the window's size the picture is scaled down smoothly (supersampling) |
| Anti-Aliasing | Off | smooths the jagged edges of the 3D scene. SMAA keeps textures sharper, FXAA smooths more (also fine texture detail). Text, menus, the inventory, map and movies are never touched. Needs Hardware 3D Rendering. Costs more the higher the Internal Resolution; well above the window it is hardly needed |
| Dithering | Off | reduces colour banding (visible steps in smooth gradients: dark fog, light falling off on walls, glows), which a CRT hid on the PS2. On keeps the 3D scene finer than the 256 levels a screen shows and spreads the in-between shades as a fine, fixed pattern. Deband also smooths the wider steps baked into some textures, softening faint texture detail a little. Text and other 2D are not dithered; menus, the inventory, map and movies stay exactly as with Off. With On, rooms show up to one level brighter than Off, as bright as on the PS2 (Off loses a level when the finished picture is copied). Needs Hardware 3D Rendering. On costs next to nothing; Deband costs about as much as FXAA (more the higher the Internal Resolution) |
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
| Check For Updates | Ask to Install | see [Updating](#updating) |
| Skip Door Animations | Instant | Off: played. Instant: a short load screen instead. Skippable: any button ends them |
| Fast Loading | On | room changes without the PS2's disc waits |
| Battle Game Unlocked | On | Battle Game (title menu, EXTRA) from the start, all five characters; saves and records unchanged |
| Difficulty Select | On | see [Difficulty select](#difficulty-select) |
| Quick Knife | On | see [Quick knife](#quick-knife) |
| Auto-climb Stairs | On | walk into stairs to climb them. Ledges, doors and items still need the button |
| Zombie Decapitation | On | a zombie whose head bursts (magnum, close shotgun) loses it |
| Stack Rifle Ammo | **On** | rifle bullets stack in one slot |
| Save System | Separate Files | see [Saves](#saves) |
| Import Memory Card Saves | | copies the card's saves into save files |
| Free Typewriter Saves | Off | typewriters save without an ink ribbon |
| Extra Item Boxes and Herbs | Off | four more item boxes and eight blue herb planters in some rooms (helps with the randomizers); in co-op the host's |
| Extra Enemies | Off | up to 1..8 more per room, of kinds the room has (one copy per enemy at most) |
| Rearranged Enemies | Off | free-roaming enemies start elsewhere in their room |
| Enemy Randomizer | Off | enemies become other kinds; bosses stay |
| Enemy Selection | Wild | Wild: any kind that fits. Mild: two common kinds per room. Custom: your rate (Off, 1..10) for each kind, set in Enemy Rates (below); picking Wild or Mild presets them |
| Enemy Rates | | with Custom, right under Enemy Selection: opens the list of rates. The same list is on the BIORAND screen's RANDOMIZER page, in the co-op lobby and in F1 |
| Types per Room | No Limit | at most that many different kinds of enemies in a room (1..8) |
| Randomize Event Enemies | On | event enemies change too |
| Enemy Seed | 0 | the same seed gives the same enemies (and the same cutscene cast) |
| Randomize Cutscene Cast | Off | see [Cutscene cast](#cutscene-cast): the story characters as other models in cutscenes |
| Model Selection | Cutscene Models | where the characters' new looks come from |
| Choose Models | | with Custom, right under Model Selection: opens the list of models to check (below) |
| Enemy Models | All Enemies | what the monsters seen only in cutscenes become: All Enemies, Custom (your own list, Choose Enemies), Same as Cast (the Model Selection) or Off (below) |
| Choose Enemies | | with Enemy Models Custom, right under it: opens the list of enemies to check |
| Model Assignment | Per Character | Per Character: the same model in every cutscene. Per Cutscene: each cutscene room picks again |
| Include Imported Models | Off | shown once you have imported models: they can be picked too. In co-op both players need the same ones |
| Randomize Voice Lines | Off | a character shown as another story character says that character's lines; shown as an enemy, it makes that enemy's sounds (below) |
| Talking Mouths | On | with Randomize Voice Lines: the mouth also moves for new lines in place of lines without mouth movement, when it is clear who speaks (below) |

Enemy options apply from the next room (ENEMIES.md, "Enemy options"). In co-op the host's enemy
options and ink ribbons count for both.

#### Cutscene cast

GAMEPLAY, under CUTSCENE CAST (and in the co-op lobby: GAME RULES, RANDOMIZER, CUTSCENE CAST). In
cutscenes the story characters are shown as other models; nothing else changes: the scenes, the
voices (unless Randomize Voice Lines is on) and what happens stay as they are.

* **Randomize Cutscene Cast**: On swaps the story characters (Alfred, Alexia, Steve, Rodrigo, Wesker)
  in cutscenes; a character who stays in the room afterwards keeps the new look. Claire and Chris are never
  swapped: in every cutscene, also while you play the other one, they show your look for them ("Character
  Model", Claire's or Chris's Model with Per Character, or BioRand's random look for that character).
* **Model Selection**: *Cutscene Models* picks among the characters' cutscene models, Claire's and Chris's
  included (they are never replaced themselves, but others can look like them): their mouths
  move with the lines and their eyes look where the original character's do (the voice stays the
  original character's). *All Models* also uses the creatures and the zombies
  (all zombie looks count as one choice, so zombies do not crowd out the rest); most of those keep a
  still face. Claire, Chris, Steve and Wesker always come as their cutscene models, so they talk;
  Claire's second costume has none and keeps a still face. The new model shows its own hands, not the original
  character's (Wesker's big black gloves are gone when someone else plays his part). Claire's
  and Chris's looks are never given to someone else: how they look is up to "Character Model" (or Claire's and Chris's Model).
  *Custom* uses only the models you check in a list of the *All Models* ones (with SELECT ALL and
  DESELECT ALL): Choose Models right under Model Selection, in F1 under Model Selection, on the BIORAND
  screen (EXTRA OPTIONS, RANDOMIZER) or in the co-op lobby (GAME RULES, RANDOMIZER, CUTSCENE CAST, CHOOSE
  MODELS). It is one list everywhere, and it is saved. With nothing checked, nobody changes. The four cutscene
  models (Claire, Chris, Steve, Wesker) come first. At first only
  the characters with voice lines are checked (Claire, Chris, Steve, Wesker, Alfred, Alexia, Rodrigo; not Claire's
  second costume, whose mouth does not move), so
  with Randomize Voice Lines everyone in a cutscene speaks; check the monsters too if you want the characters to
  become monsters.
* **Enemy Models**: the monsters that appear only in cutscenes (Nosferatu breaking free, the Tyrant Alfred
  releases, the Hunter Wesker sends out, Steve's father, the zombies around Chris and at the courtyard fence) are
  swapped too, by their own setting. *All Enemies* (the default) makes them other monsters: Alexia's first form,
  Nosferatu, the Tyrant, Monster Steve, the Hunter, the Sweeper, the Bandersnatch, the anatomist or another zombie.
  *Custom* uses only the ones you check in its own list (Choose Enemies, at the same places as Choose Models; none
  checked keeps them as they are). That list also has the characters' models after the monsters (Claire, Chris, Steve,
  Wesker, Alfred, Alexia, Rodrigo and Claire's second costume): at first the monsters are checked and the characters
  are not; check a character and the monsters can become that character. *Same as Cast* takes them from the Model Selection, as
  before this setting (a monster can then become a character). *Off* keeps them as they are. A monster never becomes
  itself, each kind keeps one model with Per Character, and monsters you fight are never swapped. An older settings
  file with Custom chosen keeps its monster choices here; otherwise it starts with All Enemies.
* **Model Assignment**: *Per Character*: e.g. Steve is shown as Alfred in every cutscene, and no two
  characters share a model. *Per Cutscene*: each cutscene room picks again. Both come from
  the enemy seed: the same seed gives the same cast.
* Your own character is not part of it: how you look in cutscenes follows APPEARANCE ("Character
  Model", "Custom Cutscene Model"). Alexia asleep in her capsule keeps her own look.
* In co-op the host's settings and seed count, so both players see the same cast.
* **Randomize Voice Lines** (also on its own, with only your character model changed): a character shown as another
  story character (Steve shown as Wesker) says random lines of that character instead of its own, and its mouth moves
  with the new line where the original line moves it. **Talking Mouths** (on by default, right under it; each co-op
  player decides for their own screen) also moves the mouth with the new line where the original line kept it still
  (about a third of the lines), but only when the game's data makes clear who is speaking and the character is shown
  with a moving mouth (a cutscene model, an imported model; your own character's double only with "Animated Faces").
  Lines can repeat and be longer or shorter than the scene
  expects; the subtitles still show the original text. A character shown as a zombie or a creature (Nosferatu, the
  Tyrant, Monster Steve, a Hunter, ...) makes that enemy's own sounds instead: moans, growls and shrieks strung
  together for as long as the line lasts. The man in the hazmat suit and imported models keep the original line. The
  seed decides the lines and sounds, so a scene sounds the same each time.

### Language

Shown once you have imported another language (title menu, IMPORT, or F9; see LANGUAGES.md).

| Setting | Default | What it does |
|---|---|---|
| Language | English | the game's text, from the next screen; change it on the title screen |
| Subtitles | Off | cutscene subtitles in any installed language that has them |
| Translated Pictures | Off | pictures with writing (menus, maps, labels) in the language. Off: the English ones, which an HD pack can replace |
| Translated Title Call | On | the language's title screen call, where its disc has one |
| Translated Overlay Windows | Off | the F1, F2, F3 and F9 windows in the language. Off: they stay English; the game's own menus are translated either way |

### Controls

Input Devices and Gamepad ([above](#input-source-and-local-co-op)), Vibration Strength (100%), Mouse
In Menus (on), Fixed Camera Controls (Combined, [Modern controls](#modern-controls-fixed-cameras)),
Weapon Shortcuts (on, [Weapon shortcuts](#weapon-shortcuts)), then the bindings.

### Third person

| Setting | Default | What it does |
|---|---|---|
| Over-the-shoulder Camera | Off | also F5 |
| Mouse Camera Style | Behind Character | Behind Character: the mouse turns you. Free Camera: the mouse orbits the camera, W/A/S/D walk relative to it |
| Lighting and Fog | Smooth | fades when the room's camera cut changes. Original: changes with each camera cut, as the original |
| HUD | Auto | a small display in a bottom corner: the equipped weapon's picture with the rounds loaded, the spare ammo for it in your inventory beside it, and a health gauge around it in the condition's colour (Fine green, Caution yellow, then orange, Danger red, Poison purple). Auto: with the over-the-shoulder camera only. On: also with the fixed cameras. Hidden in cutscenes, doors, menus and messages |
| HUD Position | Follow Shoulder | the side of the camera's shoulder (Left Shoulder: left; fixed cameras: right), or always Left or Right |
| HUD Style | Weapon Circle | Weapon Circle: the weapon in a disc with the health gauge round it. Minimal: a plain health bar with the ammo as loaded/spare (for example 10/15) and a small weapon picture above it |
| Aim Assist | Off | Off: free aim. On: the game's lock-on (gamepad: aim assist) |
| Precise Shots | On | free-aim shots hit only where the crosshair meets the body; head hits do more damage, and the blood comes from where the shot hit (the head, an arm, a leg) |
| Locational Hit Reactions | On | a zombie shot in the head, an arm or a leg flinches there; damage and timing stay the same |
| Headshots & Piercing | On | sniper and assault rifle headshots do more damage; a killing headshot from the magnum, shotgun, Gold Lugers, sniper or assault rifle bursts a zombie's head; sniper rifle shots go through enemies (not walls). In co-op, the host's choice counts (the lobby's GAME RULES show it as HEADSHOTS) |
| Move While Aiming | Off | walk slowly with the weapon raised, firing, slashing or reloading: W/A/S/D or the left stick move you (forward, back, sideways), the arms keep aiming; not with Aim Assist's lock-on (mouse, Classic gamepad) |
| Gamepad Controls | Modern | see [Third person with a gamepad](#third-person-with-a-gamepad) |

Also: field of view, camera distance, height and shoulder, mouse and stick speeds, invert Y,
crosshair and stick dead zones. Defaults: THIRDPERSON.md, "Settings".

### Audio

Music And Voice, Sound Effects: 0-100%, default 100%.

### Appearance

| Setting | Default | What it does |
|---|---|---|
| Character Models | Same for Both | Same for Both: one Character Model for whoever you play. Per Character: Claire's Model and Chris's Model instead, so each has a look of their own, also when one shows up in the other's cutscenes (Steve and Wesker then look like themselves) |
| Character Model | Current Character | play as another character, creature or zombie; your co-op partner sees it too. "Current Character (Cutscene)": the character you play in its cutscene model (follows character switches; also over other costumes) |
| Claire's Model, Chris's Model | the Character Model | with Per Character: the same list, one for each. A BioRand game's random looks come first until you choose a model in game. The BIORAND screen's EXTRA OPTIONS has these rows too |
| Match Model Animations | On | a male model moves like Chris, a female one like Claire |
| Custom Cutscene Model | On | Off: cutscenes show the real Claire or Chris |
| Animated Faces (Experimental) | Off | the swapped model moves its mouth |

### Co-op

On the CO-OP page and in F3. HOST RULES settings count only on the host. F3 also has the other host rules
(Free Typewriter Saves, Extra Item Boxes and Herbs, Headshots & Piercing) and Character Models. Details:
COOP.md, "Settings".

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
- **Memory Card** brings back the game's memory card screens: two cards (MEMORY CARD slot 1 and
  slot 2) with 15 saves each, chosen in the save and load screens. A card is full when its folder
  holds about 8 MB (another game's saves copied in, for example). The two systems do not see each
  other's saves; **Import Memory Card Saves** copies both cards' saves again (slot 2's as saves 16 to 30).

File format: ARCHITECTURE.md, "Save system".

### Back to the title and skipping the ending

- In a game, OPTION has **RETURN TO TITLE**: the bottom row, left of EXIT. It asks first. Unsaved
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
- **Save Now**: the typewriter's save screen where you stand; no ink ribbon needed or used. Loading the
  save puts you back on that spot.
- **Open Item Box**: your item box wherever you are, the same box as at every item box.
- **Load Game**: the load screen where you stand, with the save system you play with. A loaded save starts
  as from the title's LOAD GAME; backing out returns to the game as it was. Not in a co-op session.

On the EXTRAS page, Heal Now, Save Now, Load Game and Open Item Box run when you leave the menu; they need a
game in progress.

Save Now, Load Game and Open Item Box work while you control your character: not in a cutscene, door, menu,
message, grab, first-person view, with the weapon raised or in the Battle Game. F2 says why when
not. Each can have a key: OPTION -> ENHANCED SETTINGS -> CONTROLS or F1 -> Controls, HOTKEYS (Save
Anywhere, Item Box Anywhere, Load Anywhere; none by default; ini `key_cheat_save`, `key_cheat_box`,
`key_cheat_load`).

In co-op only the host can spawn enemies. Both players can save anywhere. With shared loot the item
box opens only while your partner doesn't have it open ("Your partner is using the item box.").

## Updating

**Check For Updates** (GAMEPLAY) has three choices:

- **Ask to Install** (default): at start the game looks for the newest version in the background.
  When a newer one is out, the title screen asks once: **INSTALL**, **LATER** (asked again next
  start) or **SKIP THIS VERSION** (not asked again until an even newer one is out).
- **Notify Only**: no question; "Latest Version" at the title's bottom right shows it, and "Game
  Version" turns red when you are behind.
- **Off**: no network at all outside co-op; the title shows "off". Also `update_check = 0` in
  `recvx.ini`.

**INSTALL** downloads the release for your system (Windows or Linux) from the project's GitHub
releases, with a progress bar you can cancel. It checks the download against the checksum GitHub
lists and refuses it if they differ. Then it replaces the program files in the game's folder: the
executable (under whatever name you gave it), `LICENSE`, `README.md`, `CHANGELOG.md`,
`THIRD_PARTY_NOTICES.md`, `docs/` and the `README.txt` files in `mods/`. **RESTART NOW** starts the
new version with the same options; LATER keeps playing the old one until you quit.

Never touched: your saves and `recvx.ini` (`save/`), everything else in `mods/` (imports, the
BioRand pack, HD textures), the BioRand program and any other file of yours. The old program files
wait in `recvx-update/` next to the game until the next start, which deletes them.

If anything goes wrong (no connection, a bad download, a folder the game cannot write to, a file in
use), the old files are put back and the game offers to open the release page in your browser so you
can download it yourself.

**What is sent**: one request to GitHub's public API for the list of releases (GitHub sees your IP
address, as for any download from GitHub). Only if GitHub does not answer after three tries, one
plain request to the project's own update server for the newest version number (that answer only
updates the title line; the game installs only what GitHub lists). Nothing about you or your game
is sent, nothing is stored apart from a skipped version (`update_skip` in `recvx.ini`).

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
