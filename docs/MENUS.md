# The menus (new by default; Menu Style Legacy for the old ones)

The port's reworked menus are the default. The old ones stay as an option: **Menu Style** Legacy (SETTINGS > GAMEPLAY >
STARTUP AND UNLOCKS; in the old ENHANCED SETTINGS and F1 under GAMEPLAY > GENERAL; ini `menu_style = 0`, or the older
`legacy_menu = 1`, which still means Legacy) brings back
OPTION, ENHANCED SETTINGS, the old MULTIPLAYER lobby and the BIORAND screen as they were. A change takes effect on the
title menu: at the title as soon as SETTINGS (or OPTION) closes, in a game once you are back at the title; never under
the cursor, since the OPTION task and our screens read the choice as they go (`menu_layout.h menu_choice_apply`, called
by `lobby_glue.c pc_title_mouse` at level 0). For one run `--legacy-menu` gives the old menus and `--new-menu` the new
ones whatever the ini says; changing Menu Style in a menu ends that. `--dev` adds a third value, Redesigned, for
testing (below, "Menu Style / Redesigned (--dev)"). The menu rework is done in phases; this page describes what is in now (phases P0 to P4) and how it is built.
Some game rules changed with P2 and P3 for both menus: see "Behaviour changes" below.

## What a player sees

**OPTION at the title opens SETTINGS** at full size (no scrolling image bands; the sepia OPTION picture stays, darkened
under the panel). Eight categories on the left:

| Category | Rows |
|---|---|
| GAMEPLAY | GENERAL (door animations, loading, quick knife, stairs, Your Character ▸), COMBAT (aim and hit options: Move While Aiming (Third Person) and (Fixed Cameras), Aim Scoped Weapons Normally; Headshots and Piercing ▸), SAVES (Save System, Import Memory Card Saves ▸: IMPORT's MEMORY CARD SAVES), STARTUP AND UNLOCKS (Skip Intro, updates, Battle Game, Menu Style) |
| CAMERA AND HUD | THIRD-PERSON CAMERA (on/off, field of view, shoulder, lighting; Advanced ▸ distances and height), HUD |
| CONTROLS | INPUT (Fixed-camera Movement, devices, Control Type ▸ = the game's KEY CONFIG, Button Icons, Vibration), MOUSE, CONTROLLER (Advanced ▸ dead zones), BUTTONS (Customize Keyboard and Mouse ▸, Customize Controller ▸) |
| DISPLAY | WINDOW, PICTURE (aspect, brightness, contrast, gamma, Screen Position ▸ = SCREEN SETUP, Monitor Test ▸ = MONITOR ADJUSTMENT), WIDESCREEN, INFO (Show Frame Rate, Performance Graph: F1's Performance section as a panel in the top right corner; not saved) |
| GRAPHICS | Preset (Original Look / Enhanced / Custom), QUALITY, MOTION, TEXTURE PACK, Advanced ▸ (renderer) |
| AUDIO | volumes, Output (Stereo / Mono, the game's AUDIO SETUP) |
| LANGUAGE | always shown (Language reads English without an imported one), Import Languages ▸ (IMPORT's LANGUAGES in place) |
| CO-OP | PERSONAL (Player Name, name tag, ping, hide IP, follow host), HOSTING, CONNECTION (Connection, Co-op Server) |

The rows' values, ranges, locks and help are the settings table's (the same as F1 and ENHANCED SETTINGS; the 28 rows the
table has no help for get theirs from the layout, ctest `menu_help`); some rows have new names (Banding Reduction, Shoulder Right / Left, Smooth Motion, Active Controller, ...). Vibration is one row:
Off turns the game's vibration off (the system save's VIBRATION), a percentage turns it on at that strength.

**Search** (SETTINGS only, the keyboard's): Ctrl+F or `/` (or a click on the bar's Search), then type. Every SETTINGS
row whose label, old name (the table's label, e.g. "Mouse Camera Style"), help or value holds the text is listed under its
category; Enter or Down goes to the results, which change as anywhere else; Esc (Back) closes the search, L1 / R1 too.

**Display Mode and Fullscreen Resolution** ask "Keep these settings? Reverting in 10 s." after a change: KEEP keeps it,
REVERT, Cancel or no answer within 10 seconds puts the old one back (REVERT is chosen first).

**Marks**: a row a restart applies (HD Textures, Skip Intro, ...) has a small orange **R** before its value; a run's rule
in a game (PAUSE > GAME SETUP's RULES, ENEMIES, the cast's CHARACTERS rows) a turning arrow: it applies from the next
room. Their help says it too.

**In a game OPTION opens PAUSE** (Select / View or Backspace as before; with the Modern third-person controller layout
its Start): RESUME, Camera (Fixed / Over Shoulder / First Person, the F5 setting; each press the next), SETTINGS, GAME SETUP (this game's setup: OVERVIEW, RULES,
ENEMIES, CHARACTERS, BIORAND in a BioRand game, CO-OP RULES, on the same row screen, below), CO-OP (in a session: its own
page, below), SPOILER LOG (the
F8 screen; a BioRand game outside co-op, greyed otherwise), CHEATS AND TOOLS (cheats, actions, items, spawn, teleport;
Advanced Tools ▸ opens the F2 window), RETURN TO TITLE (the old question).

**CHEATS AND TOOLS > Give Item ▸** (`give_list.c`) lists every item with its icon as the item box draws it, under
WEAPONS, AMMUNITION, HEALING, OTHER ITEMS and KEY ITEMS (ids 152..155 and the "?" entries have no item name and are not
listed); on the right the chosen item large, its amount and the inventory as it is now. Confirm or a click adds it at
once and the menu stays open ("Added: Handgun Bullets x15" on the help line, the confirm sound); Left/Right (or the
panel's arrows) the amount where the item carries one (ammunition up to 999, a weapon's rounds up to its magazine, the
percent weapons in tenths); L1/R1 (Q/W) the previous / next group; the wheel scrolls; Page Up/Down, Home, End. It is
`cheat_glue.c cheat_give`, also F2's Add to Inventory: the current character's block (`sys->itm[ply_id * 16]`), as a
pickup puts it (`pc_inv_add`: onto stacks, a two-slot item into two cells and sorted first; the lighter / Claire's
lockpick into the special slot while it is empty). Full: outside co-op into the main item box ("Inventory full: ...
went into the item box"), in co-op refused ("Inventory full. Make room first."): the box is the session's and its sync
compares it while it is open. Adding inside the OPTION task is safe at once: the inventory is plain data in `sys`, which
OPTION neither copies nor restores, and the game thread runs the menu, so no queue to the next game frame is needed.
In co-op the item is ours alone, as a pickup the partner did not see: no event is sent (the co-op item sync is flag
based; a key item counts as ours for `EV_LOST` like one picked up), so nothing diverges. BioRand changes nothing here.
The icons are trade_glue.c's `pc_item_icons` (ITEM AFS file 145's two icon textures with their own CLUT, user bit 8;
the spoiler log is user 4 and loads them while it is open): the list's first open in a pause loads them, opening it
again reuses them, `ui_rows_close` (OPTION closing) releases them unless another user holds them, and
`njReleaseTextureAll` takes them with the rest (`trade_textures_released`). The old ENHANCED SETTINGS rows (Item,
Quantity, Add To Inventory: on resume) and F2 stay; in the new menu those three rows are retired. On the right: THIS GAME (difficulty,
BioRand, the rules that differ from the original game) and the keyboard shortcuts. In co-op the header says the game
keeps running. A window opened from the pause menu (F2, F3, F9) can be used with the controller (ImGui's gamepad
navigation); Back or Start closes it.

**NEW GAME opens GAME SETUP** (what this run is), on OVERVIEW with the cursor on START GAME. **EXTRA > BIORAND** opens
the same screen with Game Type BioRand, on its BIORAND category. Categories:

| Category | Rows |
|---|---|
| OVERVIEW | Game Type (Standard / BioRand), Difficulty (Normal / Easy / Very Easy, ini `newgame_difficulty`: Difficulty Select is not shown in the new menu), Preset (Original / Remix / Chaos / Custom), Your Character ▸ (a shortcut to CHARACTERS), ACTIVE MODIFIERS (BioRand's seed, every rule that differs from the original game, a Looks line; BioRand's status or the seed below it), START GAME |
| RULES | Save Without Ink Ribbons, Extra Item Boxes and Herbs, Headshots and Piercing, Break Free Pushes Enemies (Off / Auto / On; [ENEMIES.md](ENEMIES.md#breaking-free)) |
| ENEMIES | Extra Enemies, Rearranged Enemies, Enemy Randomizer, Enemy Pool (Custom: Enemy Rates), Max Enemy Types per Room, Randomize Event Enemies (those three greyed while the randomizer is off), Enemy Randomizer Seed (Confirm: type it), RANDOM SEED |
| CHARACTERS | YOU: You Look Like (Original / Chosen Model / Random), Claire and Chris (Same Look / Separate Looks), Your Look ▸ or Claire's Look ▸ and Chris's Look ▸ (the 3D picker), Random From (Story Characters / Any Model / Custom: Your Random Looks) with Random, In Cutscenes, Advanced ▸, Import Characters ▸ (the F9 window), then a row per imported game with models: Umbrella Chronicles, Outbreak File 2, Code: Veronica (Dreamcast) (Off / On, Off at first: the game's models in the lists and random pools; [ACTORS.md](ACTORS.md) "Imported games"). STORY CHARACTERS IN CUTSCENES: Story Characters (Original / Swapped / Any Model / Custom: Story Character Looks), Keep Their Look. MONSTERS IN CUTSCENES: Cutscene Monsters (Original / Any Monster / Like Story Characters (only with Story Characters on) / Custom: Monster Looks). VOICES: Voices Match Used Character, Extra Mouth Movement (greyed while voices are off) |
| BIORAND | only with Game Type BioRand. SEED (Seed: Confirm types it; RANDOM SEED, COPY / PASTE SEED STRING), DOORS (4), ITEMS (3 and Item Pool and Weapons ▸ with the 22 detail rows), a note that BioRand changes no enemy here, Check for BioRand Updates |

The value preview shows, for every CHARACTERS row, **IN CUTSCENES THIS GAME** (who looks like whom: Claire and Chris,
the other story characters, the monsters, the voices); for START GAME what starts (READY or "generates first", game type,
difficulty, the number of modifiers); for BIORAND the seed string and its status, the doors' estimated play time or
the item distribution. Values are kept between runs (the same ini keys as before); Game Type starts Standard at
every NEW GAME (EXTRA > BIORAND sets BioRand).

**Presets** own the rows of RULES, ENEMIES and STORY CHARACTERS / MONSTERS / VOICES but the seed and Extra Mouth
Movement (`menu_setup_owned`): Original puts them all to their defaults, Remix adds Rearranged Enemies, Extra Enemies Up
to 2 and Extra Item Boxes and Herbs, Chaos the Enemy Randomizer (Wild, event enemies), Story Characters Any Model and
Voices Match Used Character. Custom is shown when the rows match none. Presets never touch BioRand, co-op rules or SETTINGS.

**START GAME**: Standard starts a new game at once with the difficulty chosen. BioRand starts at once when the installed
game is this setup's (its seed string is the same); otherwise one question ("Generate BioRand seed ... now? It replaces
the installed BioRand game", or, without BioRand, "Download it ... and generate"), then the download, the generation
with its progress and CANCEL, and the game starts. The BIORAND rows can be browsed and changed without BioRand installed.

**The look picker** (Your Look ▸, Claire's Look ▸, Chris's Look ▸ at the title; the lobby's Your Character) is laid out
as mockup 09: the look under the cursor on the left with its name, the list right of it (13 rows, the game's groups on
the right of their first row, the counter above, no BACK row: Cancel), the look's voice and animations under it, a button
bar (Use This Look, Back, Page). Above the looks, a check box per imported game with models (Umbrella Chronicles, Outbreak
File 2, Code: Veronica (Dreamcast), under "Imported Games"; unchecked at first): Confirm or a click checks it and its
looks join the list (and the random pools); the same setting as the CHARACTERS rows, the *Custom* lists' boxes and
IMPORT's "Show in Model Lists". Home goes to the first box; the cursor opens on your look. Shown in the pause menu, the look rows step through the list instead: the 3D page is
drawn in title mode 30's scene (`pc_lobby_models`, `draw_previews`), which the game's OPTION task in a game does not
have (the room's models and textures are its own then).

**Show This Game's Looks** (D17, CHARACTERS in a game only, with Story Characters on): Off by default (a spoiler, not
saved); On, the preview names who each story character looks like this game ("Steve: Alexia, Wesker: Hunter, ...",
the Whole Game picks: `cast_model` with the run's seed, as the cutscenes do); with New Each Scene there is one look per
scene, so it says that instead.

**In a game**, PAUSE > GAME SETUP shows the same categories: Game Type, Difficulty and the BIORAND rows are read-only
("Set when the game started."; COPY SEED STRING works, BIORAND is listed in a BioRand game), START GAME is not there,
CO-OP RULES is (in single player it is hidden at NEW GAME). RULES, ENEMIES and CHARACTERS change as before (from the
next room).

**LOAD GAME** (native saves) names the modifiers in effect at the bottom: "Game Setup: Extra Enemies, ... Change in the
pause menu." (they apply to a loaded game as they always did); nothing when none is on.

**MULTIPLAYER** (P3; the player's side is in [COOP.md](COOP.md) "The new MULTIPLAYER screens"): MULTIPLAYER (HOST GAME,
JOIN GAME, Co-op Settings, BACK); HOST GAME opens the lobby at once with the room code (or the join address and code);
JOIN GAME is one page; the lobby's host edits GAME SETUP in it (Edit Game Setup, Your Character: the same row screen,
the session stays open) and starts it; a guest says READY, reads the host's GAME SETUP ("Set by the host.", what the host
changed flashes), keeps its own YOU, and generates the host's BioRand game in the lobby when it has another one.
The lobby is laid out as mockup 06 (P4): the two players' models side by side on the left, each one's name above it and
its state under the name (Host / Guest, the partner's Ready or Not ready and the ping), the room code (or the join
address) and the GAME SETUP box right of them, the rows below the box (the host: START GAME, Edit Game Setup, Room
Password with a server, Your Character, COPY ROOM CODE, REMOVE PARTNER, CLOSE LOBBY; Start From is in Edit Game Setup's
OVERVIEW). With a controller the host's bar also shows Start (START GAME) and Triangle (Copy Code / Copy Address).

**WHAT'S NEW** (both menus) shows the changelog of the version being played, so nobody has to look it up elsewhere.
Its button is in the title menu's top right corner (top left in Arabic), apart from the menu's rows: a click on it, or
Triangle / Y (keyboard: S, the key bound to Triangle), whose icon the button shows in Button Icons' style. A gold dot on
the button until it has been opened in this version (ini `whatsnew_seen`). The screen is in the row screen's look: the
version this build is first (a build after a release: its "Unreleased changes", marked "This build"; a release: its
section, "This version"), then EARLIER VERSIONS. Up / Down a line, Left / Right or Page Up / Down a page, L1 / R1 (Q / W)
the previous / next version, Home / End, the wheel, the scroll bar dragged or clicked, the bar's hints clicked; Cancel,
Triangle, Esc or a right click go back. The changelog is shown in English as written (its headings, bold titles and
bullets drawn in the game's font; links and `code` as plain text); the screen's labels are translated.

**EXTRA > IMPORT** (P4) is one place for everything that comes into the game, in the row screen's look: CHARACTERS (the
games characters are imported from: Resident Evil Outbreak File 2), LANGUAGES (the editions of this game), MEMORY CARD
SAVES (Import Memory Card Saves: the memory card's saves copied to separate files, at once, with the count in a
message). A game's page has What to Import (Playable Characters, NPCs), Your Disc (Disc Image ▸: the system's file
dialog, START IMPORT, DELETE IMPORTED ...); the help names the disc a game needs, the preview what is imported. L1 / R1
switch the category, Cancel goes back. The IMPORT plate stays at the top (the EXTRA submenu is unchanged).
SETTINGS > LANGUAGE's Import Languages ▸, GAME SETUP > CHARACTERS' Import Characters ▸ and GAMEPLAY's Import Memory Card
Saves ▸ show the same screen in place, on their category (at the title and in a game; the breadcrumb goes on from the
row's: "OPTION ▸ LANGUAGE ▸ IMPORT ▸ LANGUAGES"); Back returns to the row. F9 (the import window) is unchanged.

**Text fields** (Room Code or Address, Password, Room Password, Player Name, Co-op Server, the Enemy Randomizer Seed,
BioRand's Seed) open the **character grid**: A-Z (SHIFT: a-z), 0-9, `. - :` and the signs the game's font draws as signs
(`_ # ! ? ' ( ) + / , ; % ~`), SPACE, DELETE, PASTE, OK. The controller moves on it and types the cell; Triangle (Y)
deletes the last character, Start is OK, Cancel leaves the text as it was. The keyboard types at the same time (Backspace,
Enter, Esc, Ctrl+V), the mouse clicks cells. A seed and the room code start empty (OK on nothing keeps the old one); a
name starts with the old one. Player Name and Co-op Server are typed in SETTINGS > CO-OP now (before: the F3 window).

**PAUSE > CO-OP** (in a session; before P3 it opened the F3 window): TRADE ITEMS, Show Addresses (with Hide IP
Addresses), REMOVE PARTNER (host), END SESSION (host) or LEAVE (guest), each of those two with a second press, Co-op
Window (F3); the partner, ping and connection on the right.

### Behaviour changes (both menus, P3)

* **Loot Sharing and Duplicate Weapons and Ribbons** can change in a session until its game starts (before: not in a
  session at all), as Shared Enemy Control could: the new lobby is hosted from the start, so its host sets them there.
  F1 and ENHANCED SETTINGS follow the same lock; the old lobby's LOOT and GEAR rows keep their own (not in a session).
* A co-op guest's random look (D19) is picked in the lobby already (before: once in the game), so both lobbies' 3D models
  show it; the pick is the same.

### Behaviour changes (both menus, P2)

These are game rules, so they also hold with Menu Style Legacy; the old menus' rows are unchanged:

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
the new menu) has 0 frames differing.

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

**Button icons.** The button bar (and the pause menu's, the lobby's, the look picker's, the character grid's, the
pause menu's keyboard shortcuts, the category list's L1/R1) draws each button as an icon: the input actually bound to the
action (the gamepad set's first binding, the keyboard set's key or mouse button). They are geometry and the game's font,
drawn at the screen's resolution (`ui_rows.c` `glyph_run`; no picture files, generic shapes and letters only): Xbox style
coloured A/B/X/Y discs, LB/RB, LT/RT, View/Menu; PlayStation style cross/circle/square/triangle, L1/R1, L2/R2,
Share/Options; Switch style B/A/Y/X, L/R, ZL/ZR, -/+; the d-pad with the arm(s) marked, sticks, key caps (arrow keys as
arrows) and a mouse. Gamepad inputs are positional (SDL's south, east, ...), so the label is the style's: on a Switch
pad the bottom button, which confirms by default, shows B, as printed on it. CONTROLS > **Button Icons** (ini
`button_icons`, new menu only): Automatic (the connected pad's SDL type: PS3/4/5 PlayStation, Switch Pro and Joy-Cons
Switch, any other Xbox; the keyboard's keys whenever the keyboard or mouse was used last), Xbox, PlayStation, Switch
(that style for any pad; the keyboard still shows keys), Keyboard and Mouse (always the keyboard's keys, whatever was
pressed last). A hold (Default) fills a gold bar under its icon.

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
| MULTIPLAYER, JOIN and the LOBBY (host and guest; `nm_*`), Start From's choices (`pc_lobby_start_step`) | `pc/glue/lobby_glue.c` ("the new MULTIPLAYER screens") |
| GAME SETUP in the lobby (`setup_lobby_open`, `pc_setup_lobby`, `setup_lobby_start`), a guest's view of the host's values (`setup_host_value`), what the host changed (`setup_track`), the lobby's summary (`setup_lobby_lines`) | `pc/glue/setup_glue.c` |
| The character grid: its cells, moves, field rules, the READY note (pure, ctest `textgrid`) and its box | `pc/src/host/textgrid.h`, `pc/tests/test_textgrid.c`, `pc/glue/text_grid.c` |
| PAUSE > CO-OP; TRADE ITEMS's trade key (`coop_trade_menu`) | `pc/glue/pause_glue.c`, `pc/glue/coop_glue.c` |
| A co-op guest generates the host's BioRand game from the lobby (`pc_biorand_flow_open(3)`) | `pc/glue/biorand_menu.c` |
| EXTRA > IMPORT in the new look (`import_menu_open`, `import_menu_run`: the IMPORT screen's pages and dialogs drawn on the row screen's panel, breadcrumb and category list, `ui_frame_import`), and in place from SETTINGS / GAME SETUP (`ML_IMPORT`, or a shortcut whose home is an IMPORT category) | `pc/glue/import_menu.c`, `pc/glue/ui_rows.c` |
| The memory card's saves copied (`save_import_card`: the IMPORT screen's and the settings table's action) | `pc/src/host/lobby_io.c` |
| Performance Graph's panel (F1's Performance section, `perf_body`) | `pc/src/host/overlay.cpp`, `settings.c` (a row only the new menu lists), `prof.h` `g_perf_graph` |
| Every row the new menu shows has help (ctest `menu_help`) | `pc/tests/test_menu_help.py` |
| WHAT'S NEW: the corner button and its input (`pc_whatsnew_button`, `pc_whatsnew_title`, called from `pc_title_plates`, `pc_title_mouse`), the screen (title mode 30); CHANGELOG.md embedded as it is (Linux section `.trtab` beside our texts' table, Windows resource `CHANGELOG`, rebuilt when it changes) and turned into lines the game's font can draw (no `& * # < > = @ [ ]`, ctest `changelog`) | `pc/glue/whatsnew_menu.c`, `pc/src/host/changelog.c` |
| Hooks in the game's OPTION task | decomp `adv.c`: `Adv_GameOptionScreen` (start level), `DisplayOptionPlateLevel0` (pause), `DisplayOptionBg` (no bands), levels 1-3 (`pc_options_back_level`) |

- **The layout table names settings by their table label** (`ref`), because 21 rows have no variable. A row kind says
  what it is: `ML_SET` (a table row in its home), `ML_SHORTCUT` (a row whose home is elsewhere: shows its value,
  Confirm opens the home category in place), `ML_PAGE` (a sub-page), `ML_SCREEN` (one of the game's OPTION sub-screens),
  `ML_OUTPUT`, `ML_PRESET`, `ML_WINDOW` (an F-key window), `ML_TEXT` (a co-op text), `ML_IMPORT` (EXTRA > IMPORT on one
  of its categories, in place: `MS_IMPORT`'s `MC_IMPORT_*`, where Import Memory Card Saves has its home) and `ML_RETIRED` (Difficulty Select:
  Difficulty is GAME SETUP's own row). GAME SETUP adds `ML_VAR` (a row over the game's variables, not one table row:
  `SV_*`, setup_glue.c), `ML_MERGED` (a table row shown inside an `ML_VAR` row: Model Selection in Story Characters; it
  counts as its home), `ML_BIO` (a BioRand option by its `biorand_opt` key), `ML_ACTION` (START GAME, RANDOM SEED, ...)
  and `ML_INFO` (lines of text: ACTIVE MODIFIERS). The new menu stores no values: everything is read and written through
  the settings table or the variables the old menus use (`g_enemy_opts`, `g_appear_*`, `g_coop.chr`, `g_biorand_*`,
  `biorand_seed`), so it never disagrees with F1, ENHANCED SETTINGS or the BIORAND screen.
- **GAME SETUP's context** (`g_setup_ctx`, `setup_glue.c ctx()`): NEW GAME, in game (the pause menu), and the lobby's
  host and guest for P3. ui_rows.c asks setup_glue.c for every row of a GAME SETUP category, wherever it is shown, which
  categories and rows are shown (`setup_cat_shown`, `setup_row_shown`) and which are read-only and why (`setup_row_lock`:
  "Set when the game started.", "Set by the host." for a guest, who keeps YOU and Extra Mouth Movement). The lobby only
  sets the context and hosts the same screen; a guest's rows show the host's values through `setup_host_value` (the
  EnemyOpts rows from `enemy_opts()`, EV_EOPTS; the rest from the host's Pose.opts / opts2 rules, `coop_rule` and the
  existing `coop_*` getters), so the wire is unchanged. A guest in a game sees the pause menu's GAME SETUP the same way.
- **The lobby keeps the session**: everything over its page (GAME SETUP, SETTINGS > CO-OP alone via `ui_rows_only`, the
  character grid, BioRand's dialogs, the CLOSE LOBBY question) runs inside `pc_lobby` while `lb.open` and `g_coop.lobby`
  stay set; only CLOSE LOBBY, LEAVE, BACK from MULTIPLAYER or a soft reset stop the session.
- **READY** is the guest's lobby note (`g_coop.note`, sent to the host as before) starting with `MP_READY` ("Ready: ");
  the old lobby shows it as text. The host's Game Type BioRand turns the old lobby rule BIORAND on once the installed game
  is the setup's, so the guest is told (`biorand_next`) as before.
- **Adding a setting**: besides its settings-table row, give it one home in `menu_layout.c` (ctest `menu_layout` fails
  otherwise). Keep each entry's kind and label together (`ML_SET, "Label"`) so the check finds it.
- **Defaults** are the variables' values before the command line and `recvx.ini` (`menu_defaults_capture`, first in
  `main`). Rows without a variable (Display Mode, Window Size, the looks, Language) have no default and no dot.
- **The original OPTION rows**: Output writes `SetSoundModeEx` and `AdvWork.SoundMode`; Vibration writes `sys->vibration`
  and `SetUseVibrationUnit` besides the strength; Control Type opens KEY CONFIG (level 1, cycling it in place would swap
  the menu's own Confirm and Cancel). These live in the system save, which OPTION writes when it closes, as before.
- **Hosts**: the row screen draws rows, help, preview and the button bar only (`ui_rows_open`, `ui_rows_run`); its host
  draws the background and the title. The pause menu reuses its button bar (`ui_device`, `ui_glyph`, `ui_hint`); GAME
  SETUP in title mode 30 and the lobby host the same screen. A root of several words ("MULTIPLAYER>LOBBY>GAME SETUP")
  is drawn with arrows between (the game's font has no `>`; a help text's " > " is drawn as ", ", `ui_no_gt`).
- **Search** builds its rows from every `MS_SETTINGS` row of the table (`search_rows`), under a heading per category;
  typing uses lobby_io.c's key ring (`text_on`, as the character grid; Down comes as `LK_DOWN`), so letters bound to
  buttons type instead. **Keep these settings?** is `step_set`/`keep_end` (Display Mode, Fullscreen Resolution).
  The marks are `row_mark` (`SET_RESTART`; `setup_row_next_room` for a run's rules in a game).
- **Hold for Default** is Triangle: Square confirms in key types A, C and D and cancels in B, so it cannot be the hold
  button; Triangle is free in every type.

## Menu Style / Redesigned (--dev)

A test of a redesign of the game's own screens (the "Modern UI" assessment's P0 and P1). It exists only with `--dev`;
without it nothing of this is listed, honoured or drawn, and the game's screens are byte for byte as before.

**The setting.** SETTINGS > GAMEPLAY > STARTUP AND UNLOCKS > **Menu Style** (also the old ENHANCED SETTINGS and F1;
ini `menu_style`): **Legacy** (the old menus, what Legacy Menu was), **Standard** (the default: the new menus, the
game's screens as they are), **Redesigned** (the new menus, and the overlay below on the game's inventory; listed only
with `--dev`, and an ini `menu_style = 2` reads as Standard without it). `legacy_menu = 1` in an older ini still means
Legacy; choosing a style writes both keys. Legacy and Standard switch on the title menu as before; Redesigned applies
the next time the inventory opens. The name avoids "Modern", which is the fixed-camera controls' word.

**What Redesigned adds (P1)**: an overlay on the vanilla inventory, input and drawing only (`menu_glue.c`, the strip
in `weapon_glue.c`). Every mouse action becomes the game's own presses and cursor moves, one step per frame once the
state takes input (`macro`): the game's ItemCommand, ItemCombination and ItemBox decide, refuse and speak as they do for
a pad, so co-op's item sync, BioRand's items and the randomizer see nothing new. Keyboard and controller are unchanged.

| With the mouse | Becomes |
|---|---|
| Drag an item (6 px or more) onto another | COMBINE: the item, Confirm, COMBINE, the target, Confirm |
| Drag a weapon onto EQUIPMENT | its EQUIP (the item, Confirm, EQUIP) |
| Item box: drag a bag item onto a row of the box list, or a box row onto a bag cell | the box's exchange: the cell, Confirm, the list stepped to the row (as the wheel steps it), Confirm |
| Drag a weapon onto a slot of the shortcut strip | that weapon shortcut (port state, as the 1-8 keys in the inventory) |
| Press and release without moving | a click, taken on the release (Standard takes it on the press) |
| Rest on an item | a tooltip: its name (the game's item name, the disc's language) and amount |

While an item is dragged its icon follows the pointer with what a drop there does (Combine, Equip, Exchange, Shortcut
n) and the target framed. The **shortcut strip** (two rows of four under ITEM LIST, mirrored in Arabic) shows the eight
Weapon Shortcuts with their keys; it draws nothing for a player with no shortcut assigned, and appears while a weapon is
dragged. Not touched: the command list, COMBINE's target grid and CHECK's 3D view (clicks as in Standard), the map,
files, the save and load screens, the co-op trade screen and the Weapon Shortcuts screen. The overlay's words are in
`tr_table.inc` (ctest `menu_tr` reads `menu_glue.c` too); in Arabic the labels and tooltip sit left of the pointer.

**Guard rails (P0).** `pc/tools/screens_check.sh BIN_A BIN_B OUTDIR [ISO]` dumps the title, LOAD GAME, the inventory
(cursor, command list, COMBINE's grid, CHECK), MAP, FILE, the item box and the typewriter's save screen of two builds
with the same input and compares them byte for byte (MENU=legacy for the old menus, ARGS=--dev, INI, IMPORT with a
language). Main against this work with Standard: 0 frames differ (also with `--dev` and with Legacy); `layout_check.sh`
MENU=legacy and MENU=new: 0 differ. `RECVX_MENU_TRACE=1` logs every menu state change (`menu-state: frame ...`), from
which the script prints the transitions; measured at 30 fps (presses to the last state change):

| Transition | Frames | | Transition | Frames |
|---|---|---|---|---|
| inventory open (items take input) | 22 (0.73 s) | | inventory closed (back in play) | 20 (0.67 s) |
| command list slid in | 6 | | COMBINE's target grid | 2 |
| CHECK (3D view zoomed in) | 24 (0.80 s) | | CHECK back to the items | 12 |
| MAP open | 24 (0.80 s) | | MAP back to the inventory | 26 (0.87 s) |
| FILE open | 28 (0.93 s) | | FILE back | 12 |
| item box open (from its first frame) | 10 | | item box closed | 15 (0.50 s) |
| item box: bag to box list, and back | 1 each | | title: LOAD GAME until its screen settles | 150 (5.0 s) |

These are the game's own fades and slides (`BGFadeIn`/`BGFadeOut`, the title's mode 10 to 8 fade); the assessment's
"Fast Menu Transitions" (independent of the style) would start from them.

**Performance** (`--profile` CSV, the inventory open for 900 frames: Redesigned with an item dragged over the strip
the whole time, the worst case, against Standard with the pointer on a cell; medians of 3 runs):

| | game thread mean (p99) | render thread mean | peak RSS |
|---|---|---|---|
| 1280x960, Standard / Redesigned | 0.255 (0.36) / 0.297 (0.42) ms | 0.856 / 0.927 ms | 175 / 175 MB |
| 1920x1080, Standard / Redesigned | 0.246 (0.36) / 0.293 (0.43) ms | 0.823 / 0.924 ms | 179 / 179 MB |
| llvmpipe on 2 cores (`LIBGL_ALWAYS_SOFTWARE=1 taskset -c 0,1`), 1280x960 | | 33.3 / 34.9 ms | 387 / 390 MB |

Within the budget (+0.2 ms game, +0.5 ms p99, +0.3 ms render, +10 MB) on this machine's GPU. The software rasteriser
proxy is over its +1 ms (+1.6 ms, run-to-run spread about 2 ms; that setup misses 30 fps with either style): the
icons (one textured quad each) cost most there. The icons load once as the inventory opens (ITEM AFS 145, as the trade
screen's): the opening frames' game time stays at Standard's (about 1.2 ms at most) and the items take input on the same
frame. No new shader, render target or per-frame allocation; about 60 quads plus the glyphs at worst.

## Testing headless

```
SDL_VIDEO_DRIVER=offscreen RECVX_DUMP_FRAMES=/tmp/dump RECVX_DUMP_EVERY=10 RECVX_EXIT_FRAME=600 \
  RECVX_INPUT="100:down,104:-,114:down,118:-,128:cross,132:-,200:r1,204:-" \
  build/pc/recvx --save-dir /tmp/save --skip-intro --size 1280x960 --windowed <disc image>
```

`RECVX_KEYS` (keyboard: `Return`, `Escape`, `Delete`, `Q`, `W`), `RECVX_VPAD` (a virtual controller, `RECVX_VPAD_TYPE` xbox, ps5, switch or generic: `south`, `east`,
`north`, `back`, `leftshoulder`, `dpdown`, ...) and `RECVX_DUMP_OVERLAY=1` (the F-key windows in the dumps) drive and
show the rest. Use a temporary save directory.

Scripts that drive the old title menu (`pc/tools/layout_check.sh`, the `coop_*`, `enemy_*`, `retry_check.sh` and
`room_test.sh` checks) run with `--legacy-menu`. `layout_check.sh` also plays the new menu: `MENU=new` (NEW GAME at
frame 100, START GAME at 110, where the old script's NEW GAME is, so the game starts on the same frame); `MENU_A=old
MENU_B=new` compares a build from before the switch with the new default, whose game frames are the same.

GAME SETUP: `100:down,103:-,110:cross,113:-` opens it from the title (START GAME is the next Confirm). A BioRand START
without touching the network: a temporary `RECVX_MODS` folder, `RECVX_BIORAND_CLI_DIR_TEST` pointing at a folder with
an installed `biorand-cli`, the run under `unshare -rn`. The random look's pick is logged (`appear: random look: enemy
seed ... -> Steve`); `RECVX_CHEAT_SAVE=frame` saves in game, LOAD GAME brings the pick back. The cast's rules show with
`--dev`'s `RECVX_TEST_SCENE=200:RM_9150:0:5:1` (Chris in Claire's part) and `RM_3230:0:0` (the Tyrant), in the log's
`cast: room ...` lines.

Co-op (two instances on one machine, never the public co-op server): run both under one `unshare -rn` with `lo` up;
direct with `coop_conn = 1` in each `recvx.ini` (the guest on another `coop_port`), `RECVX_COOP_CODE=TESTKEYS` on the
host so the guest can type `127.0.0.1:PORT#TESTKEYS`; a relay through a local `build/pc/server/recvx-server --port 47141
--listen 127.0.0.1` with `coop_server = 127.0.0.1:47141`. `RECVX_LOBBY_KEYS="frame:text|"` types on the character grid
with the keyboard (`|` Enter); `RECVX_VPAD` moves on it (`dpright`, `south` types, `start` is OK). MULTIPLAYER is the
title's fifth row (four downs from LOAD GAME with EXTRA shown).

## Done, and what is left (P5)

P0 to P4 are in: every setting, game rule and window of the old menus has its place in the new screens, nothing is
F-key-only (F1, F2, F3, F9 stay as shortcuts), and every row the new menu shows has an English help line.

Left out on purpose, or deviating from the mockups and the proposal:

- The look rows in the pause menu step through the list (no 3D picker there: it needs title mode 30's scene).
- The look picker keeps the game's list with its groups on the right of each group's first row (mockup 09 has gold
  heading rows, and "Original" first: the game's list starts with Current Character).
- "Copy Setup Code" (D16) is not in: it needs a code for the whole GAME SETUP and its paste (later, if wanted).
- A guest's BIORAND category is hidden (the host's BioRand options are not on the wire outside the offer; its summary
  says "BioRand: the host's game"); Difficulty is not shown to a guest (the host's Start From words say it).
- The grid's `#` key draws as the font's picture (it is the address's separator, so it stays).
- PAUSE shows the darkened OPTION picture, not the darkened game frame (the OPTION task has released the room's textures).
- The Performance Graph is not saved (as F1's Measure); turning it off stops the measuring (also with `--profile`).

P5 so far: the new words are translated into the ten languages (about 490 rows of `tr_table.inc`; ctest `menu_tr`
fails when a text the new menu shows has no row or an empty column), and in Arabic IMPORT, MULTIPLAYER, JOIN GAME and
the lobby are mirrored too (the lobby's rows and GAME SETUP box in their own place right of the models), the dialogs'
lines wrap from the right. The button bar's words that mean something else in the table ("Select" the button, "Back"
the walking binding) are looked up as "Select (button hint)" first (`lang.h tr_ctx`). Left: the user's wording pass (the
translations follow it) and the user guide. The new menu is now the default, the old one is Menu Style Legacy (above);
its code goes when the user says so.

A co-op guest that generated the host's BioRand game in the lobby now gets into the host's game (P3 saw it stay in the
lobby; also with the old BIORAND screen): the host's game state that came while the guest generated was dropped, and the
events behind it in the stream waited for it for good. It is kept now and loads once the packs match (coop_glue.c,
[COOP.md](COOP.md) "BioRand").
