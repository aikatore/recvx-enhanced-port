# Languages (European disc import)

State document of the language work: what the European disc holds, how the port uses it, and how far it is.
Nothing from any disc is in the repository: the importer reads the player's own image of the European release
(**SLES-50306**) and writes what it needs to `<import_dir>/sles50306/<code>/` (default
`mods/import/sles50306/fr`, `de`, `es`), as the Outbreak import does ([IMPORT_OUTBREAK.md](IMPORT_OUTBREAK.md)).
The import is `--dev` only (title menu EXTRA > IMPORT > LANGUAGES, or F9); the Language and Subtitles settings are
always there (English alone until a language is imported).

## PROGRESS

| Step | State |
|---|---|
| 1. Research (this document) | done |
| 2. Importer: IMPORT > LANGUAGES, F9, `pc/src/import/lang.c` | done: French, German, Spanish in about 7 s, 18-47 MB each |
| 3. Runtime: the language's files, room messages, font widths, file pages, the European title and OPTION pictures, Language setting | done |
| 4. Cutscene subtitles, Subtitles setting | done (French, German, Spanish; English has none on the disc) |
| 5. Our own texts: `tr()`, the table (French, German, Spanish, Japanese, Russian), the game font's accents, the overlay's fonts | done (about 1150 texts) |
| 6. Verification | done: runs per language (below), layout check identical, Linux and Windows builds, ctest |

Resume notes: research scripts (Python, capstone in a venv), the translation pipeline and the runs are in the job
folder `/home/dev/.claude/jobs/c1d3e63f/lang/` (outside the repository); `x/` there holds the two discs' files
unpacked with `bsdtar`, never copied into the repository. Headless import:
`RECVX_MODS=<out> RECVX_IMPORT=sles50306 RECVX_IMPORT_ISO=<SLES image> build/pc/recvx "<US image>" --save-dir <tmp>`
(exit status 0: every language imported). Then `language = de` (or `fr`, `es`) and `subtitles = 0/1` in recvx.ini.

## The European disc (SLES-50306)

`SYSTEM.CNF`: `BOOT2 = cdrom0:\SLES_503.06;1`, `VER = 1.01`, `VMODE = PAL`. 3.99 GB (the US disc 1.88 GB).
Four languages, each in a folder of its own with a digit in the file names (3, Italian, was planned and is
missing: the executable still has its tables):

| Folder | Digit | Language | Files |
|---|---|---|---|
| `ENG` | 1 | English | `SYSMES1.ALD`, `SYSTEM1.AFS`, `ADV1.AFS`, `MRY1.AFS`, `ITEM11.AFS`, `ITEM21.AFS`, `RDX_LNK1.AFS` |
| `FRA` | 2 | French | the same with 2 |
| `SPA` | 4 | Spanish | with 4 |
| `GER` | 5 | German | with 5 |

Shared by all languages, as on the US disc: `VOICE?.AFS` (the voices stay English), `BGM?.AFS`, `MULTSPQ?`,
`COMMON.MLT`, `MOVIE/*.PSS` (one set: the FMVs have no language of their own). `SYSEFF.ALD` is gone from the
root (it is `SYSTEM?.AFS` entry 1). The language is chosen on a screen of flags at start (its pictures are
textures 1 and 2 of `SYSEFF`, where the US disc has unused kanji pages).

**Same layout as the US files**: every per-language archive has the US archive's entry count, and only some
entries differ (US -> European English -> the other languages):

| Archive (US name) | Entries | Differ by language | What they are |
|---|---|---|---|
| `SYSTEM.AFS` | 140 | 0 (`SYSMES`), 1 (`SYSEFF`) | system messages and item names; the effect textures with the **font** and the "YOU ARE DEAD" picture |
| `ADV.AFS` | 33 | 0, 2, 4, 5, 32 | title (2: the menu plates, the copyright), warnings, option and ending pictures |
| `MRY.AFS` | 157 | 39 (0..67) | the map screens' area names (128x128 pictures) |
| `ITEM1.AFS` / `ITEM2.AFS` | 282 / 278 | 145..159, 161, 162, 194 | the inventory screen (ITEM / MAP / FILE words, the status words), the file pages' covers and page words |
| `RDX_LNK.AFS` | 205 rooms | all | room files: their **messages** differ; see "Rooms" |
| `SYSMES.ALD` (also `SYSTEM.AFS/0`) | | | see "System messages" |

Entries that differ between the US disc and the European English but not between the European languages
(for example `ITEM1.AFS/99`, `/281`) are European changes, not language: the port keeps the US ones.

### Text

Text is u16 glyph codes (`message.c`): 0..107 the US font (ASCII - 32), `0xFF00` new line, `0xFF01` space,
`0xFF02 n` wait n frames (timed text, then the next part), `0xFF03 id` an item name, `0xFF04` a choice,
`0xFE00` show at once (no typing), `0xFE01..05` colours, `0xFE08` a page break of timed text, `0xFFFF` end.
The US engine already handles every code the European text uses.

**The font**: `SYSEFF`'s first texture, 512x512 4 bit, 18 x 18 cells of 28 px (code / 324 = texture,
code % 18, code / 18 the cell). The European English font is the US one. French, German and Spanish share
another one with accents in codes 95..149 (these codes are not ASCII any more):

| Codes | Glyphs |
|---|---|
| 95..107 | À Á Â Ä (99: an A with a dot, unused) È É Ê Ë Ì Í Î Ï |
| 108..125 | Ò Ó Ô Ö Ù Ú Û Ü à á â ä ß è é ê ë ì |
| 126..143 | í î ï ò ó ô ö ù ú û ü Ç ç Ñ ñ ª ¡ ¿ |
| 144..149 | æ Œ œ ® ™ ° |

(Code 95 is ® in the US font, À in the other one; the European English text writes ® as 95, the others as 147.)
No Cyrillic, kana or kanji: Russian and Japanese need fonts from their own discs.

**Glyph widths**: `bhGetFontSize` advances by `FontSz[code]` (126 entries, 0 from code 108). The European
executable (`SLES_503.06`) has a second table of 162 widths right after `FontSz` (file offset 0x273530 + 126,
found by `FontSz`'s bytes) and uses it whenever the language is not English (`bhGetFontSize` at 0x2BF880 tests
the language flags at 0x380AB0 English, 0x380AB8 French, 0x380AC0 Italian, 0x380AC8 Spanish, 0x380AD0 German).
With the US widths the European font's text is spaced wrongly (checked: "Pl ay ing M anual"), so the importer
copies the table from the executable and the port uses it for that language. `FontSz` is also read directly by
`adv.c` (2x) and `system.c` (1x).

### System messages (`SYSMES.ALD`)

`u32 size` + the item names (183, `sys->mes_ip`, same count in every language) + `u32` + the system
messages (`sys->mes_sp`): the US disc 494, French 494, Spanish 494, German 503. The indices match the US
ones up to the files' pages (280); then:

- **The files' pages** (from 280): each language lays out its 24 files with its own page counts. The
  executable has five pairs of tables (page count `fstbl[24]`, first page `fsheader[24]`: English, French,
  Italian, Spanish, German), 0xC0 bytes apart, the English pair equal to the US `fileview.c` tables (file
  offset 0x23CC20 in `SLES_503.06`).
- **After the files**: the load question and its line (US 447, 448) come right after the last file's last
  page + 1 in every language; the 43 messages from "Checking memory card" (US 451..493) are the last 43 of
  every language. In between only empty messages.

The importer writes a `SYSMES.ALD` in the US order (US indices everywhere), with the language's file pages
appended after the US count; the port reads the files with the language's tables moved to that place.

### Rooms

Same 205 rooms, same structure (the same sections, cameras, scripts); only the message section (`mesp`)
and the sections behind it move. The European English rooms have the US messages (all 205 identical) but
other European changes (textures in 10 rooms, script or model bytes in about 40): the port keeps the US rooms
and takes **only the messages** of the language. Per index:

- In 197 rooms the language has the US number of messages. In 8 it has fewer or more (trailing empty
  messages dropped or added, and 2 rooms with a few messages of its own): the port takes the language's
  message where it has one, the US message otherwise; extra ones are never asked for by the US scripts.
- The German room 87 (`RM_5500`, no messages) and room 26 (`RM_1050`) have 6 and 2 textures of their own
  (writing on walls): not taken (the messages-only rule); noted.

### Cutscene subtitles

The US rooms have the cutscenes' `MessageSet` calls but their messages are empty or a 1-frame placeholder
(`0xFF02 1`: nothing to read). In French, German and Spanish those messages hold the subtitle text, shown at
once (`0xFE00`) and timed (`0xFF02 n`, `0xFE08` parts), in the normal message box: the US engine shows them
as they are (checked in German: the opening's subtitles appear under the text crawl). The European **English
rooms have no subtitles** (the same placeholders as the US), so English cannot have subtitles from the disc.
A subtitle is a message whose US version is empty or the placeholder and whose language version starts with
`0xFE00`; Subtitles Off keeps the US message there.

**FMVs**: one set of movies for all languages, no subtitle text anywhere (no message file, no table; the
executable's language tests are in the files, map, messages, title, option, ranking, game over, typewriter
and the computer terminal events, none in the movie player). FMVs stay without subtitles.

### What else the European executable changes by language

About 100 places test the language (layouts of the file screen, map, computer terminal, ranking, title and
option plates). The port keeps the US layout; text that is longer in a language may sit closer to the edges.
The title's menu words are in the title picture (`ADV.AFS/2`, texture 2: one plate per word); the port's own
title rows (EXTRA, MULTIPLAYER, BIORAND, IMPORT) are cut letter by letter from the US plates
(`lobby_glue.c plate_word`). The European pictures have other words on their plates, so the US letters are not
where those cuts expect them (only EXTREAM's, which EXTRA uses, are at the same place in every picture); the port
keeps the US plates' texture for our words (below).

## How the port uses it

**Import** (`pc/src/import/lang.c`; `import_sources[]` entry `sles50306`, category `IMPORT_CAT_LANGUAGES`): the
sources table `lang_discs[]` names per disc its executable (checked in `SYSTEM.CNF`: another image is refused with
"Use a European Resident Evil Code: Veronica X disc (SLES-50306).") and per language its folder, the digit of its
file names and its index in the executable's tables. Per language, into `<code>/`:

| File | From | How |
|---|---|---|
| `SYSMES.ALD` | `<DIR>/SYSMES<d>.ALD` | rebuilt in the US order (`lang_sysmes`, "System messages"; the US disc's `SYSMES.ALD` is read for its layout); the language's file pages appended |
| `SYSTEM.AFS/1` | `SYSTEM<d>.AFS` entry 1 | the effect textures with the language's font and its "YOU ARE DEAD" picture |
| `ADV.AFS/n`, `MRY.AFS/n`, `ITEM1.AFS/n`, `ITEM2.AFS/n` | the language's archives | the entries that differ from the disc's European English |
| `ROOMS.MES` | `RDX_LNK<d>.AFS` | every room's message table, by room index (`"RMES"`, version, count, offset/size pairs) |
| `lang.txt` | the executable | written last (a folder without it is not offered): the font's 162 widths, the file tables (`fstbl`, `fsheader` moved to the appended pages), the number of subtitle messages |

The European English is not installed: its text is the US text, and it has no subtitles.

**Run time** (`pc/glue/lang_glue.c`, `lang.h`):

- `lang_scan` (start, after an import or a delete) lists the installed languages; ini `language = fr|de|es`
  (English when that language is not installed), ini `subtitles` (default On).
- Files: `audio/adx.c mod_open_path` looks for a file the game opens (by name, or `ARCHIVE.AFS/n`) in BioRand's
  pack, the mods folder, then the language's folder (upper-case names; the game asks for `sysmes.ald`), then the
  disc. `pc_lang_file_opened` learns from it which language the loaded font (`SYSTEM.AFS/1`), the title and OPTION
  pictures (`ADV.AFS/2`, `/4`) and the system messages are in; with `SYSMES.ALD` it puts the language's (or the
  US) file tables into `fileview.c fstbl / fsheader`.
- Rooms: `room.c bhSetRoom` (TARGET_PC) replaces `rom->mesp` with a merged table: per message the language's,
  the US one where the language has none (the 8 rooms with other counts), and for a subtitle (the US message shows
  nothing) the language's only with Subtitles On. Rooms of the mods folder keep their text; BioRand's are
  translated. `lang_frame` rebuilds the table when Subtitles changes in a room.
- Font: `message.c bhGetFontSize` and the direct `FontSz` reads (`adv.c` AdvEasyDispMessage,
  AutoSaveLoadEasyDispMessage; `system.c`) take the language font's widths while it is loaded (`pc_font_width`).
- The European title picture: its copyright is one block (`DisplayTitleBg` skips the US second block), the French
  PRESS START has two rows (`DisplayPressStartPlate`). Our own plate words stay English, in the US plates' letters,
  in every language: the title rows MULTIPLAYER, EXTRA, BIORAND and IMPORT, and in OPTION ENHANCED SETTINGS, RETURN
  TO TITLE, EXIT and the question's YES / NO (the game's own plates are the language's). With another language's
  picture loaded (`lang_picture`), `lang_us_plates` reads resource 2 of the disc's `ADV.AFS/2` or `/4` (the title's
  texture 1, OPTION's texture 0; past the mods and language folders) into a static buffer (257 KB, the larger of the
  two; reread when the screen changes) and makes it the current texture list for the letters: a one-texture
  `NJS_TEXLIST` whose `NJS_TEXMEMLIST` points at that TIM2, drawn as any loaded texture (`Ps2TexLoad`), outside the
  texture heap (`njLoadTexture` would find the language's texture: every copy has the same global index). It is
  marked to stay original with an HD pack (`pc_tex_keep_original`), as the US plates are.
- **Language** (GAMEPLAY > GENERAL, F1 and ENHANCED SETTINGS): English and the installed languages in their own
  names; locked "No other language is installed." or, in a game, "Change it on the title screen." It applies from
  the next screen: leaving OPTION reloads the title in the new language (checked), a game start reads the system
  messages and the font, a room its messages. **Subtitles**: locked "No language with subtitles is installed." or,
  with English chosen, "Only the imported languages have subtitles."

**Our texts** (`pc/src/host/tr.c`, `tr_table.inc`): `tr(en)` looks the English text up (sorted table, binary search;
an ImGui label's `##id` is kept) and returns the language of the setting, or the English. Translated where they are
drawn: the game-font text functions (`lobby_glue.c text`, `text_w`, `text_wrap`: every screen using `pc_ui_text*`),
the dialogs (`pc_ui_dialog*`), the settings' labels, values, help, headings and lock reasons (both menus), the
bindings' names, the formats of the screens' `snprintf`s, the co-op status and notes, the import's progress, and
the overlay's ImGui labels (F1, F2, F3, F9). Not translated: names of models and rooms the port lists from its
own tables (teleport list, imported models), BioRand's seed strings, the co-op texts sent to the partner (its
lobby note). Test aid: `RECVX_TR_MISSES=file` writes the texts asked for that have no row.

**Letters**: our UTF-8 texts go to the game font through `pc_glyph_next`: with a European font loaded the accented
letters use its codes (95..149), with the US font the letter without its accent (Français -> Francais). The
game font has no Cyrillic, kana or kanji (a later Japanese or Russian import brings its own font: their columns
are in the table, the glyph mapping is to be added then). The overlay (ImGui's default font) has ASCII and
Latin-1; for Japanese and Russian it merges a font of the system once such a language is chosen (Windows: Meiryo,
MS Gothic, Yu Gothic / Segoe UI, Arial; Linux: Noto Sans CJK / DejaVu Sans), else those letters stay missing.

**Address window** (docs/ARCHITECTURE.md "Data model and memory"): the imported data (`ROOMS.MES`, the merged
tables) and the importer's tables are on the heap; the F9 window's lists too (a second source would have doubled
their static array). What the work adds to the executable is about 350 KB, most of it the five languages' strings
of `tr_table.inc`; the US plates' buffer is 257 KB more; the window keeps about 2.6 MB free (`_end` 0x3FD5D608).

**Layout**: the European font is monospaced (16 units for most letters, the US font 10..24), so translated text is
wider; the translations were kept short (labels at most about 15% longer, sentences 30%) and the long ones
abbreviated (e.g. German ERW. EINSTELLUNGEN). Rows that still do not fit are scaled down by the menus as before
(`text_fit`); help lines wrap to their 3 lines.

## Verification (2026-10-28, Linux, headless; job folder `lang/`)

- Import: `imp.sh` (headless) and the IMPORT screen (`impmenu.sh`: EXTRA > IMPORT > LANGUAGES > the disc, START
  IMPORT, OK, DELETE IMPORTED LANGUAGES, YES): 3 languages, 202 rooms each, 468-471 subtitles; the US disc as the
  image: refused. A second import gives the same files byte for byte.
- Per language (French, German, Spanish; `ingame.sh`, `scene.sh`, `menus.sh`, `overlays.sh`): title menu (the
  European picture), OPTION (European plates), every ENHANCED
  SETTINGS page, the lobby (MULTIPLAYER, HOST A GAME), EXTRA > IMPORT, BIORAND, F1 / F2 / F3 / F9; in a new game the
  opening's subtitles, the cell's message, the inventory (its pictures and item names), the inventory's help
  texts, the map, the FILE page and the manual's pages (German: the remapped pages), Rodrigo's scene with Subtitles
  On (subtitles) and Off (none). English with the languages installed: unchanged (no subtitles, the US font).
- Our plate words (job folder `platewords/`, `plates.sh`, `hd.sh`): in French, German, Spanish and English the title
  (--dev; MULTIPLAYER selected and not, the EXTRA submenu with BIORAND and IMPORT), OPTION at the title (ENHANCED
  SETTINGS, EXIT) and in a game (RETURN TO TITLE, its YES / NO) show the US plates' English words; the same with the
  HD pack and `hd_textures_ui` (English and German).
- Switching at the title: GAMEPLAY > Language in ENHANCED SETTINGS, back to the title: the title in the new language.
- `pc/tools/layout_check.sh` main build vs this build (English): identical. ctest all passed (`import` checks the
  system messages' rebuild on a synthetic layout).

## Not done, and why

- **English subtitles**: the European English rooms have only the US placeholders; there is no English subtitle
  text on either disc.
- **FMV subtitles**: none on the disc (one set of movies, no subtitle data, no code in the executable's movie player).
- **The European executable's layouts**: about 100 places move text per language (file screen, map, computer
  terminal, ranking, option plates); the port keeps the US positions. Text sits where the US text sits.
- **Room textures with writing**: the German room 87 (`RM_5500`) and room 26 (`RM_1050`) have 6 and 2 textures of
  their own; only messages are taken from rooms (the European rooms also carry non-language changes).
- **Italian**: planned by Capcom (the executable has its tables) but not on the disc.
- **Japanese and Russian imports**: not implemented (sources table ready; translations of our texts ready).

## Notes for later

- Japanese (SLPM-65022) and Russian (a fan translation) imports: a row in `import_sources[]` and `lang_discs[]`,
  their message and font layouts (the Japanese font uses texture pages 1..3 for kanji: `bhDispFont`'s code / 324),
  and `pc_glyph_next`'s mapping for their letters; our strings for both are in `tr_table.inc` already.
- Adding a text of ours: write it in English as usual; it is translated where it is drawn if it is a literal
  (else wrap its format in `tr()`), and needs a row in `tr_table.inc` (the job folder's `tr/` scripts: `keys.py`
  lists the texts, `check.py` checks a language's file, `gen.py` writes the table).
