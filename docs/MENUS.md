# The new menus (`--new-menu`)

A rework of the port's menus, in testing behind `--new-menu` (also on with `--dev`). Without the flag nothing changes:
OPTION, ENHANCED SETTINGS and every other screen are as before. The menu rework is done in phases; this page describes
what is in now (phases P0 and P1) and how it is built.

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
its Start): RESUME, Camera (Fixed / Third Person, the F5 setting), SETTINGS, GAME SETUP (this game's rules: RULES,
ENEMIES, CHARACTERS, CO-OP RULES, on the same row screen), CO-OP (in a session: opens the F3 window), SPOILER LOG (the
F8 screen; a BioRand game outside co-op, greyed otherwise), CHEATS AND TOOLS (cheats, actions, items, spawn, teleport;
Advanced Tools ▸ opens the F2 window), RETURN TO TITLE (the old question). On the right: THIS GAME (difficulty,
BioRand, the rules that differ from the original game) and the keyboard shortcuts. In co-op the header says the game
keeps running. A window opened from the pause menu (F2, F3, F9) can be used with the controller (ImGui's gamepad
navigation); Back or Start closes it.

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
| Hooks in the game's OPTION task | decomp `adv.c`: `Adv_GameOptionScreen` (start level), `DisplayOptionPlateLevel0` (pause), `DisplayOptionBg` (no bands), levels 1-3 (`pc_options_back_level`) |

- **The layout table names settings by their table label** (`ref`), because 21 rows have no variable. A row kind says
  what it is: `ML_SET` (a table row in its home), `ML_SHORTCUT` (a row whose home is elsewhere: shows its value,
  Confirm opens the home category in place), `ML_PAGE` (a sub-page), `ML_SCREEN` (one of the game's OPTION sub-screens),
  `ML_OUTPUT`, `ML_PRESET`, `ML_WINDOW` (an F-key window), `ML_TEXT` (a co-op text) and `ML_RETIRED` (Difficulty Select:
  Difficulty becomes a GAME SETUP row). The new menu stores no values: everything is read and written through the
  settings table, so it never disagrees with F1 or ENHANCED SETTINGS.
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

## Not in yet (later phases)

- P2: GAME SETUP from NEW GAME (OVERVIEW, presets, BioRand as a game type, START GAME), the merged CHARACTERS rows of
  the proposal's §8 (You Look Like, Story Characters with its pool, Cutscene Monsters without the cast), the 3D look
  picker, `newgame_difficulty`, EXTRA > BIORAND as a shortcut, the LOAD GAME note.
- P3: MULTIPLAYER (instant lobby, JOIN, the guest's read-only GAME SETUP), the on-screen character grid for text rows
  (Player Name and Co-op Server are edited in the F3 window until then).
- P4: PAUSE > CO-OP as its own page (today it opens F3), IMPORT restyled with MEMORY CARD SAVES (Import Memory Card
  Saves still runs from GAMEPLAY > SAVES), Performance Graph in DISPLAY > INFO, keyboard search.
- P5: translations of the new words, help for the rows that have none, the RTL polish, then the new menu by default.
- Also not yet: the "Keep these settings?" countdown after a display change, restart / next-room marks as small icons
  (a "(restart)" note after the label for now), the darkened game frame behind PAUSE (the OPTION task has released the
  room's textures, so it shows the darkened OPTION picture).
