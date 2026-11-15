# The new menus (`--new-menu`)

A rework of the port's menus, in testing behind `--new-menu` (also on with `--dev`). Without the flag nothing changes:
OPTION, ENHANCED SETTINGS and every other screen are as before. The menu rework is done in phases; this page describes
what is in now (phases P0 to P2) and how it is built. Some game rules changed with P2 for both menus: see "Behaviour
changes" below.

## What a player sees

**OPTION at the title opens SETTINGS** at full size (no scrolling image bands; the sepia OPTION picture stays, darkened
under the panel). Eight categories on the left:

| Category | Rows |
|---|---|
| GAMEPLAY | GENERAL (door animations, loading, quick knife, stairs, Your Character ▸), COMBAT (aim and hit options, Headshots and Piercing ▸), SAVES (Save System, Import Memory Card Saves), STARTUP AND UNLOCKS |
| CAMERA AND HUD | THIRD-PERSON CAMERA (on/off, field of view, shoulder, lighting; Advanced ▸ distances and height), HUD |
| CONTROLS | INPUT (Fixed-camera Movement, devices, Control Type ▸ = the game's KEY CONFIG, Vibration), MOUSE, CONTROLLER (Advanced ▸ dead zones), BUTTONS (Customize Keyboard and Mouse ▸, Customize Controller ▸) |
| DISPLAY | WINDOW, PICTURE (aspect, brightness, contrast, gamma, Screen Position ▸ = SCREEN SETUP, Monitor Test ▸ = MONITOR ADJUSTMENT), WIDESCREEN, INFO |
| GRAPHICS | Preset (Original Look / Enhanced / Custom), QUALITY, MOTION, TEXTURE PACK, Advanced ▸ (renderer) |
| AUDIO | volumes, Output (Stereo / Mono, the game's AUDIO SETUP) |
| LANGUAGE | always shown (Language reads English without an imported one), Import Languages ▸ (opens the F9 window) |
| CO-OP | PERSONAL (Player Name, name tag, ping, hide IP, follow host), HOSTING, CONNECTION (Connection, Co-op Server) |

The rows' values, ranges, locks and help are the settings table's (the same as F1 and ENHANCED SETTINGS); some rows
have new names (Banding Reduction, Shoulder Right / Left, Smooth Motion, Active Controller, ...). Vibration is one row:
Off turns the game's vibration off (the system save's VIBRATION), a percentage turns it on at that strength.

**In a game OPTION opens PAUSE** (Select / View or Backspace as before; with the Modern third-person controller layout
its Start): RESUME, Camera (Fixed / Third Person, the F5 setting), SETTINGS, GAME SETUP (this game's setup: OVERVIEW, RULES,
ENEMIES, CHARACTERS, BIORAND in a BioRand game, CO-OP RULES, on the same row screen, below), CO-OP (in a session: opens the F3 window), SPOILER LOG (the
F8 screen; a BioRand game outside co-op, greyed otherwise), CHEATS AND TOOLS (cheats, actions, items, spawn, teleport;
Advanced Tools ▸ opens the F2 window), RETURN TO TITLE (the old question). On the right: THIS GAME (difficulty,
BioRand, the rules that differ from the original game) and the keyboard shortcuts. In co-op the header says the game
keeps running. A window opened from the pause menu (F2, F3, F9) can be used with the controller (ImGui's gamepad
navigation); Back or Start closes it.

**NEW GAME opens GAME SETUP** (what this run is), on OVERVIEW with the cursor on START GAME. **EXTRA > BIORAND** opens
the same screen with Game Type BioRand, on its BIORAND category. Categories:

| Category | Rows |
|---|---|
| OVERVIEW | Game Type (Standard / BioRand), Difficulty (Normal / Easy / Very Easy, ini `newgame_difficulty`: Difficulty Select is not shown in the new menu), Preset (Original / Remix / Chaos / Custom), Your Character ▸ (a shortcut to CHARACTERS), ACTIVE MODIFIERS (BioRand's seed, every rule that differs from the original game, a Looks line; BioRand's status or the seed below it), START GAME |
| RULES | Save Without Ink Ribbons, Extra Item Boxes and Herbs, Headshots and Piercing |
| ENEMIES | Extra Enemies, Rearranged Enemies, Enemy Randomizer, Enemy Pool (Custom: Enemy Rates), Max Enemy Types per Room, Randomize Event Enemies (those three greyed while the randomizer is off), Enemy Randomizer Seed (Confirm: type it), RANDOM SEED |
| CHARACTERS | YOU: You Look Like (Original / Chosen Model / Random), Claire and Chris (Same Look / Separate Looks), Your Look ▸ or Claire's Look ▸ and Chris's Look ▸ (the 3D picker), Random From (Story Characters / Any Model / Custom: Your Random Looks) and Include Imported Models with Random, In Cutscenes, Advanced ▸, Import Characters ▸ (the F9 window). STORY CHARACTERS IN CUTSCENES: Story Characters (Original / Swapped / Any Model / Custom: Story Character Looks), Keep Their Look, Include Imported Models. MONSTERS IN CUTSCENES: Cutscene Monsters (Original / Any Monster / Like Story Characters (only with Story Characters on) / Custom: Monster Looks). VOICES: Voices Follow the Look, Extra Mouth Movement (greyed while voices are off) |
| BIORAND | only with Game Type BioRand. SEED (Seed: Confirm types it; RANDOM SEED, COPY / PASTE SEED STRING), DOORS (4), ITEMS (3 and Item Pool and Weapons ▸ with the 22 detail rows), a note that BioRand changes no enemy here, Check for BioRand Updates |

The value preview shows, for every CHARACTERS row, **IN CUTSCENES THIS GAME** (who looks like whom: Claire and Chris,
the other story characters, the monsters, the voices); for START GAME what starts (READY or "generates first", game type,
difficulty, the number of modifiers); for BIORAND the seed string and its status, the doors' estimated play time or
the item distribution. Values are kept between runs (the same ini keys as before); Game Type starts Standard at
every NEW GAME (EXTRA > BIORAND sets BioRand).

**Presets** own the rows of RULES, ENEMIES and STORY CHARACTERS / MONSTERS / VOICES but the seed and Extra Mouth
Movement (`menu_setup_owned`): Original puts them all to their defaults, Remix adds Rearranged Enemies, Extra Enemies Up
to 2 and Extra Item Boxes and Herbs, Chaos the Enemy Randomizer (Wild, event enemies), Story Characters Any Model and
Voices Follow the Look. Custom is shown when the rows match none. Presets never touch BioRand, co-op rules or SETTINGS.

**START GAME**: Standard starts a new game at once with the difficulty chosen. BioRand starts at once when the installed
game is this setup's (its seed string is the same); otherwise one question ("Generate BioRand seed ... now? It replaces
the installed BioRand game", or, without BioRand, "Download it ... and generate"), then the download, the generation
with its progress and CANCEL, and the game starts. The BIORAND rows can be browsed and changed without BioRand installed.

**The look picker** (Your Look ▸, Claire's Look ▸, Chris's Look ▸ at the title) is the lobby's 3D model page with NEW
GAME's title and, on the right, the look's voice and animations. Shown in the pause menu, the look rows step through
the list instead (the 3D page needs the title).

**In a game**, PAUSE > GAME SETUP shows the same categories: Game Type, Difficulty and the BIORAND rows are read-only
("Set when the game started."; COPY SEED STRING works, BIORAND is listed in a BioRand game), START GAME is not there,
CO-OP RULES is (in single player it is hidden at NEW GAME). RULES, ENEMIES and CHARACTERS change as before (from the
next room).

**LOAD GAME** (native saves) names the modifiers in effect at the bottom: "Game Setup: Extra Enemies, ... Change in the
pause menu." (they apply to a loaded game as they always did); nothing when none is on.

### Behaviour changes (both menus, P2)

These are game rules, so they also hold without `--new-menu`; the old menus' rows are unchanged:

* **Random look in every game (D3).** "Randomize Character Model" (ini `biorand_appear`, the old BIORAND screen's
  RANDOMIZER page) picks a look at every new game, not only in BioRand games: seeded by the enemy seed + an offset outside
  BioRand (the pack's seed in a BioRand game, as before), stored in the save (the same save words, free outside BioRand
  saves). An `recvx.ini` with `biorand_appear` 1 or 2 now gets a random look in a standard NEW GAME too.
* **A co-op guest's random look (D19).** A guest with the setting on gets a random look of its own, picked from the
  host's seed and its player slot (before: never). Not stored; both screens show it (the guest sends its look).
* **The other protagonist (D20).** Claire or Chris not played shows your look in cutscenes only with separate looks
  (Character Models Per Character, or this game's random looks per character). Changed: Randomize Cutscene Cast on +
  Character Models Same for Both + a chosen look (or a one-look random pick): the other protagonist now stays original
  (before: your look, so it showed twice). Everything else (Per Character with the cast on or off, Same for Both with the
  cast off, "Current Character") is as before.
* **Cutscene Monsters without the cast (D20).** A new flag (ini `cast_monsters_alone`, set only by the new menu's Cutscene
  Monsters with Story Characters Original) casts the creatures and scene zombies with the cast off. No old-menu setting
  sets it, so old-menu combinations do not change (their default, cast off with Enemy Models All Enemies, still leaves the
  monsters as they are).
* The voice plan cache (`voices.plan`) is made again once (its format changed with these rules).

None of these changes the default settings: `pc/tools/layout_check.sh` (main against P2, single player without
`--new-menu`) has 0 frames differing.

### Controls (every new screen)

| | Controller | Keyboard | Mouse |
|---|---|---|---|
| Row | Up / Down | Up / Down, Page Up / Down, Home / End | hover, wheel |
| Value | Left / Right | Left / Right | click an arrow |
| Open / run / cycle | Confirm (A / Cross by key type) | Enter | click the label |
| Back | Cancel, Select | Esc, Backspace | right click |
| Category | L1 / R1 (LB / RB) | Q / W | click the category |
| Default | hold Triangle (Y) 0.5 s | Delete | click the hint |
| Pause: resume | Start, Cancel | Esc | right click |

A gold dot marks a row that differs from its default; RESET CATEGORY (last row, YES / NO) puts a whole category back.
Each screen remembers its category and each category its row while the game runs. Sub-pages (Advanced, the binding
pages, the Custom lists, a shortcut's home) replace the rows; the breadcrumb grows and Back returns to the row that
opened them. The button bar shows the buttons of the device pressed last. Arabic mirrors the whole layout.

## How it is built

| Piece | File |
|---|---|
| The tree: screens, categories, groups, each row's home, label / value-name / help overrides | `pc/src/host/menu_layout.c` (`menu_layout.h`) |
| Its check: every settings-table row placed exactly once (or retired), shortcuts name real rows | `pc/tests/check_menu_layout.cmake` (ctests `menu_layout`, `menu_layout_selftest`) |
| The row screen (categories, rows, help, preview, button bar, input); draws no background | `pc/glue/ui_rows.c` |
| The OPTION host: SETTINGS at the title, PAUSE in a game, the windows opened after OPTION closes | `pc/glue/pause_glue.c` |
| GAME SETUP: where it is shown, its own rows (Game Type, Difficulty, Preset, the merged CHARACTERS rows, BioRand's options, START GAME), ACTIVE MODIFIERS, the previews, the title-mode-30 host | `pc/glue/setup_glue.c` |
| BioRand's dialogs for START GAME (download, generate, start) | `pc/glue/biorand_menu.c` `pc_biorand_flow` |
| NEW GAME and EXTRA > BIORAND open GAME SETUP; its look rows open the lobby's model page | `pc/glue/lobby_glue.c` (`pc_title_mouse`, `extra_input`, `pc_lobby`) |
| Hooks in the game's OPTION task | decomp `adv.c`: `Adv_GameOptionScreen` (start level), `DisplayOptionPlateLevel0` (pause), `DisplayOptionBg` (no bands), levels 1-3 (`pc_options_back_level`) |

- **The layout table names settings by their table label** (`ref`), because 21 rows have no variable. A row kind says
  what it is: `ML_SET` (a table row in its home), `ML_SHORTCUT` (a row whose home is elsewhere: shows its value,
  Confirm opens the home category in place), `ML_PAGE` (a sub-page), `ML_SCREEN` (one of the game's OPTION sub-screens),
  `ML_OUTPUT`, `ML_PRESET`, `ML_WINDOW` (an F-key window), `ML_TEXT` (a co-op text) and `ML_RETIRED` (Difficulty Select:
  Difficulty is GAME SETUP's own row). GAME SETUP adds `ML_VAR` (a row over the game's variables, not one table row:
  `SV_*`, setup_glue.c), `ML_MERGED` (a table row shown inside an `ML_VAR` row: Model Selection in Story Characters; it
  counts as its home), `ML_BIO` (a BioRand option by its `biorand_opt` key), `ML_ACTION` (START GAME, RANDOM SEED, ...)
  and `ML_INFO` (lines of text: ACTIVE MODIFIERS). The new menu stores no values: everything is read and written through
  the settings table or the variables the old menus use (`g_enemy_opts`, `g_appear_*`, `g_coop.chr`, `g_biorand_*`,
  `biorand_seed`), so it never disagrees with F1, ENHANCED SETTINGS or the BIORAND screen.
- **GAME SETUP's context** (`g_setup_ctx`, `setup_glue.c ctx()`): NEW GAME, in game (the pause menu), and the lobby's
  host and guest for P3. ui_rows.c asks setup_glue.c for every row of a GAME SETUP category, wherever it is shown, which
  categories and rows are shown (`setup_cat_shown`, `setup_row_shown`) and which are read-only and why (`setup_row_lock`:
  "Set when the game started.", "Set by the host." for a guest, who keeps YOU and Extra Mouth Movement). The lobby (P3)
  only sets the context and hosts the same screen.
- **Adding a setting**: besides its settings-table row, give it one home in `menu_layout.c` (ctest `menu_layout` fails
  otherwise). Keep each entry's kind and label together (`ML_SET, "Label"`) so the check finds it.
- **Defaults** are the variables' values before the command line and `recvx.ini` (`menu_defaults_capture`, first in
  `main`). Rows without a variable (Display Mode, Window Size, the looks, Language) have no default and no dot.
- **The original OPTION rows**: Output writes `SetSoundModeEx` and `AdvWork.SoundMode`; Vibration writes `sys->vibration`
  and `SetUseVibrationUnit` besides the strength; Control Type opens KEY CONFIG (level 1, cycling it in place would swap
  the menu's own Confirm and Cancel). These live in the system save, which OPTION writes when it closes, as before.
- **Hosts**: the row screen draws rows, help, preview and the button bar only (`ui_rows_open`, `ui_rows_run`); its host
  draws the background and the title. The pause menu reuses its button bar (`ui_device`, `ui_glyph`, `ui_hint`). GAME
  SETUP in title mode 30 and the lobby (later phases) are meant to host the same screen.
- **Hold for Default** is Triangle: Square confirms in key types A, C and D and cancels in B, so it cannot be the hold
  button; Triangle is free in every type.

## Testing headless

```
SDL_VIDEO_DRIVER=offscreen RECVX_DUMP_FRAMES=/tmp/dump RECVX_DUMP_EVERY=10 RECVX_EXIT_FRAME=600 \
  RECVX_INPUT="100:down,104:-,114:down,118:-,128:cross,132:-,200:r1,204:-" \
  build/pc/recvx --new-menu --save-dir /tmp/save --skip-intro --size 1280x960 --windowed <disc image>
```

`RECVX_KEYS` (keyboard: `Return`, `Escape`, `Delete`, `Q`, `W`), `RECVX_VPAD` (a virtual controller: `south`, `east`,
`north`, `back`, `leftshoulder`, `dpdown`, ...) and `RECVX_DUMP_OVERLAY=1` (the F-key windows in the dumps) drive and
show the rest. Use a temporary save directory.

GAME SETUP: `100:down,103:-,110:cross,113:-` opens it from the title (START GAME is the next Confirm). A BioRand START
without touching the network: a temporary `RECVX_MODS` folder, `RECVX_BIORAND_CLI_DIR_TEST` pointing at a folder with
an installed `biorand-cli`, the run under `unshare -rn`. The random look's pick is logged (`appear: random look: enemy
seed ... -> Steve`); `RECVX_CHEAT_SAVE=frame` saves in game, LOAD GAME brings the pick back. The cast's rules show with
`--dev`'s `RECVX_TEST_SCENE=200:RM_9150:0:5:1` (Chris in Claire's part) and `RM_3230:0:0` (the Tyrant), in the log's
`cast: room ...` lines.

## Not in yet (later phases)

- P2 left out: the look picker is the lobby's model page (its own list layout and help line, the voice and animations
  added), not mockup 09's new layout; in a game the actual cast picks are not listed (D17: the default is hidden);
  "Copy Setup Code" (D16); "Your Character" is not a shortcut in the lobby and JOIN yet (P3).
- P3: MULTIPLAYER (instant lobby, JOIN, the guest's read-only GAME SETUP: `g_setup_ctx` SC_LOBBY_HOST / SC_LOBBY_GUEST
  are there, the lobby still has to host the screen, send the host's values and show Start From), the guest's BioRand
  question in the lobby (it still opens the old BIORAND screen), Quick Start greyed with BioRand, the on-screen character
  grid for text rows (Player Name and Co-op Server are edited in the F3 window until then).
- P4: PAUSE > CO-OP as its own page (today it opens F3), IMPORT restyled with MEMORY CARD SAVES (Import Memory Card
  Saves still runs from GAMEPLAY > SAVES), Performance Graph in DISPLAY > INFO, keyboard search.
- P5: translations of the new words, help for the rows that have none, the RTL polish, then the new menu by default.
- Also not yet: the "Keep these settings?" countdown after a display change, restart / next-room marks as small icons
  (a "(restart)" note after the label for now), the darkened game frame behind PAUSE (the OPTION task has released the
  room's textures, so it shows the darkened OPTION picture).
