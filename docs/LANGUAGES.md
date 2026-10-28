# Languages (European disc import)

State document of the language work: what the European disc holds, how the port uses it, the plan and
how far it is. Nothing from any disc is in the repository: the importer reads the player's own image
of the European release (**SLES-50306**) and writes what it needs to `<import_dir>/lang/<code>/`
(default `mods/import/lang/`), as the Outbreak import does ([IMPORT_OUTBREAK.md](IMPORT_OUTBREAK.md)).

## PROGRESS

| Step | State |
|---|---|
| 1. Research (this document) | done |
| 2. Importer: IMPORT > LANGUAGES, `pc/src/import/lang.c` | planned |
| 3. Runtime: the language overlay, room messages, font widths, file pages, Language setting | planned |
| 4. Cutscene subtitles, Subtitles setting | planned |
| 5. Our own texts: the string table, the game font's accents, the overlay | planned |
| 6. Verification (runs per language, layout check, builds, ctest) | planned |

Resume notes: research scripts (Python, capstone in a venv) and runs are in the job folder
`/home/dev/.claude/jobs/c1d3e63f/lang/` (outside the repository); `x/` there holds the two discs'
files unpacked with `bsdtar` (`x/us`, `x/pal`), never copied into the repository.

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
(`lobby_glue.c plate_word`). Only EXTRA's letters (from EXTREAM) are at the same place in every European
picture; with another language the other rows are drawn in the game's font.

## Design

- **Import** (`pc/src/import/lang.c`, the IMPORT screen's LANGUAGES category, F9): a table of language
  sources (`lang_sources[]`: disc ID, folder of each language, the executable's tables); the European disc
  now, Japanese and Russian later. The disc is checked by its `SYSTEM.CNF` (`SLES_503.06`). Per language
  (French, German, Spanish; the European English is the US text without subtitles and is not installed):
  `<import_dir>/lang/<code>/` with `SYSMES.ALD` (US order), `SYSTEM.AFS/1`, the differing `ADV.AFS/n`,
  `MRY.AFS/n`, `ITEM1.AFS/n`, `ITEM2.AFS/n` (as the mods folder names entries), `ROOMS.MES` (the messages of
  every room) and `lang.txt` (name, source, the width table, the file tables). About 25 MB per language.
- **Runtime**: setting `language` (`en` default and always there, plus each installed folder). The file
  loader (`adx.c mod_open_path`) looks in the language's folder after the mods folder and before the disc;
  `bhSetRoom` swaps the room's message table; `bhGetFontSize` and the direct `FontSz` reads use the
  language's widths; `fileview.c` uses its file tables. The language changes at the title screen only (the
  font and system messages are read when a game starts, the title pictures when the title starts).
- **Subtitles** setting (On by default): per subtitle message, the language's or the US one.
- **Our texts**: `tr()` with a table keyed by the English text (French, German, Spanish, Japanese, Russian);
  the game font draws accented letters with the European font's codes when that font is loaded, and the
  letter without its accent otherwise.

## Notes for later

- Japanese (SLPM-65022) and Russian (a fan translation) imports: add a row to `lang_sources[]` and their font
  (kana and kanji pages, Cyrillic) handling; our strings for both are in the table already.
