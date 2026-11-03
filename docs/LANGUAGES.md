# Languages (imports from other releases)

State document of the language work: what the other releases' discs hold, how the port uses them, and how far it is.
Nothing from any disc is in the repository: the importer reads the player's own image of the European release
(**SLES-50306**: French, German, Spanish), of the US release with the Russian fan translation by Brosko
(**SLUS-20184**, patched: Russian), of the Japanese release (**SLPM-65022**: Japanese), of the European release with
the Turkish fan translation by Patriots Çeviri (**SLES-50306**, patched: Turkish in place of Spanish), of the US
release with the Brazilian Portuguese fan translation by gledson999 (**SLUS-20184**, patched), of the European release
with the Arabic fan translation by Abdulaziz Al-Amri (SKZEZO EDITING; **SLES-50306**, patched: Arabic in place of
Spanish), of the Japanese release with a Korean fan translation (**SLPM-65022**, patched; its author is not named on
the disc) or of the European release with the Italian fan translation by KimeraVX (**SLES-50306**, patched: Italian in
place of French) and writes what it needs to `<import_dir>/<source>/<code>/` (default `mods/import/sles50306/fr`, `de`,
`es`, `mods/import/slus20184ru/ru`, `mods/import/slpm65022/ja`, `mods/import/sles50306tr/tr`,
`mods/import/slus20184pt/pt-BR`, `mods/import/sles50306ar/ar`, `mods/import/slpm65022ko/ko`,
`mods/import/sles50306it/it`), as the Outbreak import does ([IMPORT_OUTBREAK.md](IMPORT_OUTBREAK.md)). The import is
in the title menu's EXTRA > IMPORT > LANGUAGES, or F9; the Language and Subtitles settings show once a language
is imported.

## PROGRESS

| Step | State |
|---|---|
| 1. Research (this document) | done (European, Russian fan translation, Japanese, Turkish, Brazilian Portuguese, Arabic, Korean and Italian fan translations) |
| 2. Importer: IMPORT > LANGUAGES, F9, `pc/src/import/lang.c` | done: French, German, Spanish in about 7 s, 18-47 MB each; Russian 2 s, 28 MB; Japanese 3 s, 144 MB (136 MB of them two movies); Turkish 2 s, 18 MB; Brazilian Portuguese 1 s, 21 MB; Arabic 2 s, 22 MB; Korean 3 s, 38 MB; Italian 2 s, 48 MB |
| 3. Runtime: the language's files, room messages, font (widths, cells, space), file pages (Japanese, Korean: pictures), the title and OPTION pictures, the executable's texts, Language setting | done |
| 4. Cutscene subtitles, Subtitles setting: a language of its own, in its own font | done (French, German, Spanish, Russian, Japanese, Turkish, Brazilian Portuguese, Arabic, Korean, Italian; English has none on any disc) |
| 5. Our own texts: `tr()`, the table (French, German, Spanish, Japanese, Russian, Turkish, Brazilian Portuguese, Arabic, Korean, Italian), the game fonts' letters (glyphs.c, ctest `lang`), the overlay's fonts; Arabic: joined forms and right-to-left order (`lang_shape`) | done (about 1400 texts) |
| 6. Verification | done: runs per language (below), layout check identical, Linux and Windows builds, ctest |
| 7. Movies and the opening's text crawl (see "Movies") | done: the Russian and Brazilian Portuguese text crawl, the Japanese movies' logo; no disc has a movie with translated text |
| 8. Voices (see "Voices"), Translated Voices | done: the title call of the Japanese, Korean and Italian discs; no disc has dubbed dialogue |

Resume notes: research scripts (Python, capstone in a venv), the translation pipeline and the runs are in the job
folders `/home/dev/.claude/jobs/c1d3e63f/lang/` (European), `lang2/` (Russian, Japanese: `x/ru`, `x/jp` hold the
discs' files unpacked with `bsdtar`, `jpfont/` the Japanese glyph map's sources) and `lang3/` (Turkish, Brazilian
Portuguese: `x/tr`, `x/pt` with the clean `x/eu`, `x/us`; `tr/` the translation pipeline: brief, chunks, `check.py`,
`fit.py`, `merge.py`) and `lang_ar/` (Arabic: `x/ar` the patched disc's files, `armap.py` the font's cells, `dump.py`
its text decoded, `tr/` the Arabic pipeline with `check_ar.py`, the runs' scripts, `shots/`) and `lang_ko_it/` (Korean,
Italian: `x/ko`, `x/it`, `x/eu` the discs' files; `kocells.py`, `koread.txt`, `kofix.txt`, `kodec.py`, `gen_ko.py` the
Korean font's cells read and checked in the text, `kotext.txt` the Korean text decoded; `tr/` the pipeline with the
briefs; `imp.sh`, `refuse.sh`, `title.sh`, `menus.sh`, `ingame.sh`, `extra.sh`, `impl.sh`, `sw/`, `pickup/`, `icons/`,
`shots/`), outside the repository. Headless import: `RECVX_MODS=<out>
RECVX_IMPORT=sles50306|slus20184ru|slpm65022|sles50306tr|slus20184pt|sles50306ar|slpm65022ko|sles50306it RECVX_IMPORT_ISO=<image>
build/pc/recvx "<US image>" --save-dir <tmp>` (exit status 0: every language imported). Then `language = de` (or `fr`,
`es`, `ja`, `ru`, `tr`, `pt-BR`, `ar`, `ko`, `it`) and `subtitles = 0` (Off) / `2 + LANG_*` in recvx.ini.

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
The US engine already handles every code the European text uses. `0xFF02` and `0xFF03` take an operand, and
`0xFF03 0xFFFF` (the current item's name: system messages 152 "You've filed the ...", 157 "Take the ...?", 158
"You've taken the ...") is **not** the end: a reader of the tables goes by `pc_parse.h pc_mes_len` (ctest
`parsers`, `import`). The imports before lang.txt version 2 cut those three messages at the operand: in French,
German, Spanish and Japanese a pickup ran on into the next messages ("There's no space ...") without its Yes / No
and hung or crashed; Russian (its system messages copied whole) and the rooms (no such operand) were not hit.

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
No Cyrillic, kana or kanji: Russian and Japanese come with fonts of their own (below).

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
and the computer terminal events, none in the movie player). FMVs stay without subtitles. The movies are PAL
re-encodes of the US ones (25 frames a second, the same sizes and sound) with no language in them: see "Movies".

### What else the European executable changes by language

About 100 places test the language (layouts of the file screen, map, computer terminal, ranking, title and
option plates). The port keeps the US layout; text that is longer in a language may sit closer to the edges.
The title's menu words are in the title picture (`ADV.AFS/2`, texture 2: one plate per word); the port's own
title rows (EXTRA, MULTIPLAYER, BIORAND, IMPORT) are cut letter by letter from the US plates
(`lobby_glue.c plate_word`). The European pictures have other words on their plates, so the US letters are not
where those cuts expect them (only EXTREAM's, which EXTRA uses, are at the same place in every picture); the port
keeps the US plates' texture for our words (below).

## The Russian fan translation (SLUS-20184, by Brosko)

The US disc with Brosko's Russian translation patch (2022). `SYSTEM.CNF` and `DISCID.BIN` are the US disc's
(`BOOT2 = cdrom0:\SLUS_201.84;1`, `VER = 1.00`), 2.56 GB. Same file layout as the US disc (one language at the root).
What the patch changes, against the clean US disc:

| File | Changed | What |
|---|---|---|
| `SLUS_201.84` | 8911 bytes, same size | the font's widths, texts of the executable, a few positions (below) |
| `SYSMES.ALD` | rewritten | the US layout: 183 item names, 494 system messages, the US file tables (`fstbl` is not patched) |
| `SYSTEM.AFS` | entries 1, 2 | 1: the font with Cyrillic, "ВЫ УМЕРЛИ"; 2: the computer's font (Cyrillic in place of the lower-case ASCII) |
| `ADV.AFS` | 0, 2, 4 | 2: the title (Russian plates for the US layout; the copyright and "ЛОКАЛИЗАЦИЯ СТУДИИ BROSKO, 2022" in the picture); 4: OPTION |
| `MRY.AFS` | 37 of 0..67 | the map screens' names |
| `ITEM1.AFS` / `ITEM2.AFS` | 144..159, 277..281 / 144..159, 277 | the inventory's words, the files' covers |
| `RDX_LNK.AFS` | 196 rooms (839 MB) | the messages (same counts in every room) and 473 subtitles (463 shown at once, `0xFE00`); rooms repacked: their PRS streams end with the data, without the end mark (`pc_prs_expand_ex` lax); textures: the opening's text crawl (3 textures of `RM_0000`, 85..87: imported, see "Movies"; a fourth there, 88, has no writing) and one non-text texture in 134 rooms |
| `VOICE1.AFS`, `MOVIE/*.PSS` | entry 342; all movies re-encoded (larger) | no Russian in them (see "Movies", "Voices"): not taken |

**Telling it from the clean US disc**: the patched `bhGetFontSize` counts 192 glyphs (US: 108) and reads a new table of
widths at 0x3033A0 (free space in `sceSynthesizerSamples`). The importer finds the function by its instructions
(`andi v1,a0,0xffff; slti v0,v1,N; ...; lui/addiu` the table) and refuses N = 108 ("This is the US disc without the
Russian translation.").

**The font**: one texture as the US one; codes 0..94 are ASCII as on the US disc. The Cyrillic letters that look
Latin are the Latin ones (А В Е М Н О Р С Т Х, а е о р с у х); the others at 102..150 (Б Г Д Ё Ж З И Й К Л П У Ф
Ц Ч Ш Щ Ъ Ы Ь Э Ю Я 102..124, б в г д ё ж з и й к л м н п т ф ц ч ш щ ъ ы ь э ю я 125..150), « » № at 100, 101,
98. The glyphs sit in the middle of their cells and the widths are tight (the ink's width): our texts draw with them
unchanged (no extra tightening, `lobby_glue.c TRACK`).

**The executable's texts** (at the US addresses, or where the patched code points): `SaveLoadMessage` (the save and
load screens' 45 messages, rewritten in place), `pdm_mes` (DEMO PLAY), `pdm_palmes` and `pause_mes` (moved: the
patched `bhSetPad` and `bhSysCallScreenSaver` point at them), `cnt_mes` (the game over's "Повторить? Да Нет",
longer than the US one), the computer events' lines (`sp_evt.c comevt_message_*`: rewritten in place, in the
computer font's letters). Not taken: positions moved for the longer words (memory card screens `DispTexture`,
`ItemBoxInit`, the screen saver's text), the map's colours (`MapPal`), the title's `CheckButton` change and the
letterbox bars (`bhDrawCinesco`) turned off in `bhMainSequence`.

## The Japanese disc (SLPM-65022)

Biohazard Code: Veronica Kanzenban (`BOOT2 = cdrom0:\SLPM_650.22;1`, `VER = 1.01`, February 2001), 1.88 GB, the US
disc's file layout. The US disc still carries much of it: the Japanese file pages and the kanji textures.

| File | Differs from the US disc | What |
|---|---|---|
| `SYSMES.ALD` (= `SYSTEM.AFS/0`) | 19 KB | 183 item names, 300 system messages: 0..279 as the US ones, 280..299 the memory card's (280, 281: the load question); no file pages |
| `SYSTEM.AFS/1` | | the font (4 textures) and "YOU DIED" |
| `ADV.AFS` | 0, 2, 3, 4, 5, 32 | 2: the title "BIOHAZARD -CODE:Veronica- 完全版" (its plates are English, laid out otherwise: copyright rows, INFORMATION, VS GAME); 4: OPTION (Japanese key names and help, no green arrows) |
| `MRY.AFS` 0..4, `ITEM1/2.AFS` 145 | | the map's words (the area names are English, as on the US disc), the inventory's words |
| `RDX_LNK.AFS` | all 205 rooms | Japanese messages (4 rooms with other counts: 170, 171, 178 more, 196 one fewer), 565 subtitles shown at once (`0xFE00`) |
| `MOVIE/MV_015.PSS`, `MV_016.PSS` | 130.5 MB, 11.7 MB | the ending's credits and the title screen's demo with the logo "BIOHAZARD -CODE:Veronica- 完全版" (US: RESIDENT EVIL -CODE:Veronica- X); the other 16 movies are the US ones (see "Movies") |

**Text**: the US engine's codes (`0xFF00` new line, `0xFF01` space, `0xFF02` wait, `0xFF03` item, `0xFF04` choice,
`0xFE0x`), glyph codes are cells of the Japanese font: 0..25 A-Z, 26..51 a-z, 52 ▶, 53 ▼, 54..133 hiragana,
134..214 katakana (without ヲ ヴ ヂ ヅ ぢ ぺ ぽ), 215..242 ／ 0-9 － ？ ！ ． ・ ー α Σ （ ）「 」＋ 〜 ○ ％ ×, signs and the
letter-dot cells of S.T.A.R.S. and B.O.W. at 250..313, the rest kanji: about 1100, only those the game uses. No 、
or 。 (the text uses spaces and ．). Every glyph is 28 wide and so is a space; the executable has no
`bhGetFontSize` and no `bhFontScaleSet`.

**The font**: 4 textures of 324 cells (`bhDispFont`'s code / 324: the US engine draws 4 pages already; the US disc
has the same kanji pages, unused). The cells' characters (`pc/src/host/glyphs.c ja_cells`) were matched against
Noto Serif/Sans CJK and every cell the text uses read in context (all system and room messages decoded).

**The files**: pictures. `ITEM1.AFS` / `ITEM2.AFS` (whichever the scenario reads, `sys->itm_partid`) entry 160 +
`fsheader[file]` + page is a 512 x 512 picture of the page (117 pages; the same entries are on the US disc). The
Japanese `fstbl` / `fsheader` are right before the backgrounds' table (`wallpaper`, the same in every release); the
playing manual hides 5..0 pages as the story goes (`PlayPageCheck`: the US 7..0).

**The executable's texts**: `SaveLoadMessage` (45 messages, Japanese, its own layout: found by its count and first
offset); DEMO PLAY and the game over's "Retry? Yes No" are English (in its font's codes); the computer is English
(the US font and lines).

## The Turkish fan translation (SLES-50306, by Patriots Çeviri)

The European disc with Patriots Çeviri's Turkish patch: its credit is in the title picture's copyright block
("Patriots Çeviri, keyifli oyunlar diler."). `SYSTEM.CNF`, `CF_*.TXT` and the folders are the European disc's
(`BOOT2 = cdrom0:\SLES_503.06;1`, `VER = 1.01`, PAL), 3.99 GB. **Turkish replaces Spanish**: the `SPA` folder (digit 4)
holds the Turkish files, the executable's Spanish tables (index 3) serve it, and the language screen's Spanish flag
is a Turkish one ("TÜRKÇE"). English, French and German are untouched. What the patch changes, against the clean
European disc:

| File | Changed | What |
|---|---|---|
| `SLES_503.06` | 942 bytes, same size | the Spanish game over question, PRESS START line and one boot memory card message, written one byte off (their u16 codes read byte-swapped, so they show wrongly on the disc too): not taken |
| `SPA/SYSMES4.ALD` | rewritten | 183 item names, 494 system messages, the Spanish layout (its file tables, `fstbl` not patched) |
| `SPA/SYSTEM4.AFS` | 0, 1, 2 | 1: the font (below) and "ÖLDÜN" (YOU ARE DEAD), the flags; 2: the computer screens re-encoded, no visible change |
| `ENG/SYSTEM1.AFS` | 1, 2 | the language screen's flags (the English copy too), 2 re-encoded |
| `SPA/ADV4.AFS` | 0, 2, 4, 6, 7 (1, 5, 32 re-encoded English) | the warning, the title (Turkish plates in their own layout of the picture, the credit), OPTION, the battle game's selection screens |
| `SPA/MRY4.AFS` | 39 entries | the English map pictures re-encoded: **the map stays English** (the clean Spanish has its own) |
| `SPA/ITEM14.AFS` / `ITEM24.AFS` | 145..159 | the inventory's words (161, 162, 194, the Spanish file covers, are the English ones again); 145's item pictures redrawn, each of the two with colours of its own where the game has one palette for both (see "Pictures" under "How the port uses it") |
| `SPA/RDX_LNK4.AFS` | 205 rooms | the messages (202 rooms with messages, the Spanish counts), 471 subtitles shown at once (`0xFE00`) |

**The font**: the European one (same cells, same widths: the executable's second width table is not patched) with
six cells redrawn: Ğ 102, İ 107, Ş 109, ş 124, ı 126, ğ 140 (in place of Ê Ï Ó ë í ñ, which Turkish does not use).
Ç ç Ö ö Ü ü are the European ones; â î û remain. Its palette is laid out as the US font's (opaque black at 0).

**Telling it from the clean European disc** (both have its ID): the Spanish folder's system messages never write the
Spanish font's ¡ and ¿ (codes 142, 143; the clean Spanish has 66). The Turkish entry refuses the clean disc ("This is
the European disc without the Turkish translation."); the European entry refuses the patched one ("This disc has the
Turkish translation: import it with its own entry.": its Spanish would be Turkish in the Spanish font's letters).

**Its words**: system messages in the polite "siz" ("Devam etmek istiyor musunuz?", "START düğmesine basın."),
passive questions ("Veriler yüklensin mi?"); items "eşya", files "dosya", OPTION "AYARLAR", the memory card "hafıza
kartı".

## The Brazilian Portuguese fan translation (SLUS-20184, by gledson999)

The US disc with gledson999's Brazilian Portuguese patch (Brazilian Warriors): the credit is a picture of its own,
`ADV.AFS/1` ("Tradução feita por gledson999.", the group's sites), and the title's copyright line ("TRADUZIDO POR
GLEDSON999-BRAZILIAN WARRIORS."). `SYSTEM.CNF` and `DISCID.BIN` are the US disc's (`VER = 1.00`, NTSC), 2.67 GB; the
movies, voices and music are the US ones. What the patch changes, against the clean US disc:

| File | Changed | What |
|---|---|---|
| `SLUS_201.84` | 8374 bytes, same size | `FontSz` (the accents' widths at 96..107, 61 = 2), `SaveLoadMessage` (rewritten in place), `pdm_mes`, `cnt_mes`, the computer events' lines (in the computer font's letters: `k z w " '` are ç ã é á í), `pause_mes` emptied |
| `SYSMES.ALD` | rewritten | the US layout (183 item names, 494 system messages, the US file tables); the file ends 56 bytes before its table says, inside the last message (the pause line) |
| `SYSTEM.AFS` | 0, 1, 2 | 1: the font (below), the game over picture (its own words); 2: the computer's font (accents in place of a few lower-case letters) |
| `ADV.AFS` | 0, 1, 2, 4 | the warning, the credit picture, the title (Portuguese plates in the US layout), OPTION |
| `MRY.AFS` | 0, 1, 2, 128, 129, 150 | the map's words (the area names stay English, as on the US disc) |
| `ITEM1.AFS` / `ITEM2.AFS` | 145..159 | the inventory's words |
| `RDX_LNK.AFS` | 197 rooms (1.37 GB) | the messages (the US counts) and 489 subtitles in the US rooms' placeholders; rooms stored as PRS of literals only (no compression); textures with writing: the opening's text crawl (`RM_0000` 85..87: imported, see "Movies") and signs in 28 rooms (CUIDADO, PARE, ALERTA, PERIGO ALTA VOLTAGEM, ARMA B.O.W, the battle game's player choice in `RM_5500`, the direction sign of `RM_4020`: not taken) |

**The font**: the US one (its widths, 108 codes) with õ ó ô â ã á à ê é í ú ç at 96..107 (the US 28-wide blanks
before), Ç at 108, ° at 59 (the US `[`). `bhGetFontSize` gives codes from 108 no width: the text writes Ç and then
code 94 (a blank 24 wide, the US `~`). There are **no accented capitals**: the patch writes capitals plain ("NAO",
"RELATORIO", "INSTALAÇOES") or sets a mark (`~ ´ \` ^` at 109..112, no width) on the line above, placed with code 61
(a blank 2 wide, the US `]`).

**Telling it from the clean US disc**: `bhGetFontSize` counts the US 108 glyphs, but `FontSz` 96..107 are not the US
28. The entry refuses the clean US disc ("This is the US disc without the Brazilian Portuguese translation.") and
another US-based disc (the Russian one: its glyph count is 192); the Russian entry refuses this one as a US disc
without the Russian translation.

**Its words**: "você" ("Acho melhor você sair daqui"), OPTION "OPÇÕES" (title) with AJUSTAR SOM / CONTROLE / TELA,
NOVA PARTIDA, CARREGAR PARTIDA, the item box "baú", "Fita de Tinta".

## The Arabic fan translation (SLES-50306, by Abdulaziz Al-Amri)

The European disc with the Arabic patch ("تعريب عبدالعزيز العامري", SKZEZO EDITING): the credit is in the title
picture (`ADV4.AFS/2`, texture 2, under the plates: "تعريب عبدالعزيز العامري" and "SKZEZO EDITING"); the disc has no
readme and the executable no credit string. `SYSTEM.CNF` and the folders are the European disc's (`BOOT2 =
cdrom0:\SLES_503.06;1`, `VER = 1.01`, PAL), 4.00 GB. **Arabic replaces Spanish**, as the Turkish patch does: the `SPA`
folder (digit 4) holds the Arabic files and the executable's Spanish tables (index 3) serve it. English, French and
German are untouched. What the patch changes, against the clean European disc:

| File | Changed | What |
|---|---|---|
| `SLES_503.06` | 6.6 KB longer | a second load segment (`heap`, 0x1E44900, 0x19DA bytes, empty on the clean disc) with the Spanish texts the executable lacked or that the patch rewrote: `SaveLoadMessage_SPA`'s texts (its offsets point there), `pdm_mes_SPA` (DEMO PLAY), `pause_mes_SPA`, `cnt_mes_SPA` (the game over question); `pdm_palmes_SPA` rewritten in place; `bhSetPad`, `bhSysCallScreenSaver`, `bhMainGameOver` point at the new segment. No code change (the text is drawn left to right as ever) |
| `SPA/SYSMES4.ALD` | rewritten | 183 item names, 494 system messages, the Spanish layout (its file tables) |
| `SPA/SYSTEM4.AFS` | 1, 2 | 1: the font (below) and "تم القضاء عليك" (YOU ARE DEAD); 2: the computer's screens re-encoded (the computer's lines stay English) |
| `ENG/SYSTEM1.AFS` | 1 | the language screen's flags (Arabic in Spanish's place) |
| `SPA/ADV4.AFS` | 0, 1, 2, 4, 6, 7 | the warning, the Capcom logo in Arabic, the title ("الشر المقيم - كود: فيرونيكا X", Arabic plates in the Spanish layout, the credit), OPTION (Arabic plates and help), the battle game's screens; 5 and 32 are the Spanish ones (pictures without writing) |
| `SPA/MRY4.AFS` | 40 entries | the map's words and area names in Arabic |
| `SPA/ITEM14.AFS` / `ITEM24.AFS` | 11, 19, 30, 82, 90, 105, 114, 120, 124, 145..159 | the inventory's words; textures of item models with writing (the ammunition boxes, the ink ribbon "حبر") |
| `SPA/RDX_LNK4.AFS` | 205 rooms | the messages (202 rooms with messages, the Spanish counts), 471 subtitles shown at once (`0xFE00`) |

**The text**: the US engine's codes (`0xFF00` .. `0xFFFF` as above), glyph codes 1..149 of the patch's font. Arabic is
written right to left and its letters join; the engine draws left to right, so the patch stores each line **in display
order** (reversed: a line's first glyph is its last letter; numbers left to right) with **every letter already in its
joined form** (isolated, initial, medial, final: a cell each; lam-alef ligatures). The choices (`0xFF04`) come after
their words, where the choice cursor (code 28) is drawn: ◀ in this font. Messages are left-aligned as the engine draws
them. The text was decoded with the font's cells (`lang_ar/armap.py`) and reads as Modern Standard Arabic ("هل تريد
المحاولة من جديد؟", "لقد حصلت على", "بطاقة الذاكرة"), alef often without hamza ("اعدادات", "الان").

**The font**: one texture as the European one (18 x 18 cells of 28), the European widths (the executable's second
table, not patched: 16 for nearly every cell). **No Latin letters**: 0 space, 1 !, 2..6 and 27..30 signs (biohazard,
crown, spade, heart, Umbrella; ○ ◀ ▼ "AA"), 9 ؟, 10 ء, 14 . 15 / 16..25 0-9, 26 :, and the letters from 31: per
letter its forms in the order isolated, initial, medial, final (ت 32..35, ج 40..43, ... ي 122..125), the right-joining
letters isolated and final (د 71, 53; ر 56, 57; ...), lam-alef 132..138, ئ 139..142, the alefs (ا 11, 143; أ 13, 144;
إ 8, 146; final آ 145; no isolated آ), ة 126, 127, ى 130, 131, ؤ 128, 129, tatweel 63; a few out of order (final س 7,
final ص 52, isolated د 71). The glyphs' ink is at about 10..27 of their 28-wide cells (a connecting stroke to 27),
drawn 16 apart: the strokes of neighbouring letters meet. The table is `glyphs.c ar_letters` (Unicode presentation
form -> cell), worked out from the cells' pictures and checked by where each cell stands in the text (a final form is
the leftmost glyph of a word in display order, an initial one the rightmost).

**Telling it from the clean European disc**: the Spanish folder's system messages write the article "al" (لـ ا: codes
105 11 in display order) about 450 times (the clean Spanish: never; that would be "Í+"). The Arabic entry refuses the
clean European disc ("This is the European disc without the Arabic translation."), the Turkish disc and others ("Use
the European disc with the Arabic fan translation by Abdulaziz Al-Amri (SLES-50306)."); the European entry ("This disc
has the Arabic translation: import it with its own entry.") and the Turkish entry refuse the Arabic disc.

## The Korean fan translation (SLPM-65022)

The Japanese disc with a Korean fan patch. Its author is named nowhere on the disc: not in the pictures (the title, the
warning, OPTION, the file pages were all looked at), the text (every system and room message decoded), the executable
or the volume descriptor (the Japanese disc's, Capcom); the entry calls it "Korean fan translation". `SYSTEM.CNF`,
`DISCID.BIN` and the file list are the Japanese disc's (`BOOT2 = cdrom0:\SLPM_650.22;1`, `VER = 1.01`, NTSC), every
file the same size as on the clean disc (1.88 GB). What the patch changes, against the clean Japanese disc:

| File | Changed | What |
|---|---|---|
| `SLPM_650.22` | 5.7 KB, same size | `SaveLoadMessage` only (rewritten in place in Korean); DEMO PLAY and the game over's "Retry? Yes No" stay the Japanese executable's English |
| `SYSMES.ALD` | rewritten, same layout | 183 item names, 300 system messages (the Japanese layout; message 0 and a few unused ones read "[원문 판독 필요]", and the last item name stayed Japanese) |
| `SYSTEM.AFS` | 1 | the font (below); "YOU DIED" stays the Japanese disc's English picture |
| `ADV.AFS` | 0, 2, 4 | the warning in Korean, the title "BIOHAZARD -CODE:Veronica-" without 完全版 (its plates are English as on the Japanese disc), OPTION (Korean key names and help; the menu words English as on the Japanese disc) |
| `ITEM1.AFS` / `ITEM2.AFS` | 145, 160..276 | the inventory's words; **the file pages: Korean pictures** (the same 117 entries the Japanese release shows as pictures) |
| `RDX_LNK.AFS` | rooms | the messages (the Japanese counts) and 565 subtitles shown at once (`0xFE00`) |
| `MRY.AFS` | none | the map's words stay the Japanese disc's (its area names are English) |

**The font**: the Japanese font's 4 textures, **755 cells redrawn**: 736 Hangul syllables (the ones the text uses: the
ordinary words of the game, not the whole set; for example no 옵, 협, 칭, 릭, 퇴, 폴, 페), the signs `! " ( ) , . : [ ]`
・ Σ α ○ 「 」 ♡ and ー (used as a hyphen: "AKー47") moved to other cells, and cell 500 blank: the text writes
it as a wide space for indentation. The biohazard, crown, heart, spade and Umbrella signs and the S. T. A. R. cells are
gone (the text writes S.T.A.R.S. with letters and dots); kana, the Latin letters and the digits are the
Japanese ones, and 334 of the kanji (the Korean text uses none of them but one item name). Text: the Japanese engine's codes, a space
`0xFF01` 28 wide between words as in the Japanese text. The cells were read from enlarged sheets and every cell the
text uses checked in context (decoded system and room messages: 25 misread cells corrected, such as 왼/원, 허/혀,
찮/참, 떻/똥, 든/돈); the table is `glyphs.c ko_cells` (the Japanese cells with the Korean ones in place).

**The executable's save / load messages**: the patch wrote its first message from 2 bytes before its place, over the
high half of the last message's offset: on the disc the first message ("메모리 카드(PS2)를 확인 중입니다") loses its 메
and the last one is lost. The importer puts the first message's glyph back and takes the last message from the low
half of its offset (`exe_saveload`, which now writes the 45 messages one after another).

**Telling it from the clean Japanese disc** (both have its ID): the system messages have no hiragana (cells 54..99;
the Japanese text is full of them). The Korean entry refuses the clean Japanese disc ("This is the Japanese disc without
the Korean translation.") and other discs ("Use the Japanese disc with the Korean fan translation (SLPM-65022)."); the
Japanese entry refuses the Korean disc ("This disc has the Korean translation: import it with its own entry.").

**Its words**: the formal register of Korean games: "~하시겠습니까?", "~습니다", "~하십시오", 예 / 아니요, 저장 / 불러오기,
메모리 카드, 슬롯, 아이템, 파일, 지도, 「…」 획득?, 「…」을 손에 넣었다.

## The Italian fan translation (SLES-50306, by KimeraVX)

The European disc with KimeraVX's Italian patch: its credit is a picture of its own, `FRA/ADV2.AFS/1` ("Patch By:
KimeraVX", a note not to share the patched image, thanks to the testers Dom3x, Paolo D. Roger, Popplo). `SYSTEM.CNF` and
the folders are the European disc's (`BOOT2 = cdrom0:\SLES_503.06;1`, `VER = 1.01`, PAL), 3.99 GB. **Italian replaces
French**: the `FRA` folder (digit 2) holds the Italian files, the executable's French tables (index 1) serve it, and the
language screen's French flag is an Italian one. The official European release has no Italian (its executable has an
Italian slot, index 2, never filled: see the European disc); the patch does not use it. English, German and Spanish are
untouched. What the patch changes, against the clean European disc:

| File | Changed | What |
|---|---|---|
| `SLES_503.06` | 19 KB, same size | `SaveLoadMessage_FRA` (Italian, in place), `pdm_palmes_FRA` ("PREMI START"), `cnt_mes_FRA` ("Riprovare? Sì No"); about 30 positions of the memory card screens (`mcDisplayFileSelectWindow`, `mcDispFileName`, ...: 128 -> 100) and two of `SpriteOnOff` |
| `FRA/SYSMES2.ALD` | rewritten | 183 item names, 494 system messages, the French layout (its file tables, not patched) |
| `FRA/SYSTEM2.AFS` | 1 | the flags (Italian in French's place) and "SEI MORTO"; the font is the European one, unchanged (à è é ì ò ù and È are in it) |
| `ENG/SYSTEM1.AFS`, `GER/SYSTEM5.AFS`, `SPA/SYSTEM4.AFS` | 1 | the language screen's flags |
| `FRA/ADV2.AFS` | 0..4 | the warning, the credit picture, the title (Italian plates: PREMI START, CARICA PARTITA, NUOVA PARTITA, OPZIONI, MODALITÀ BATTAGLIA, in the French layout of the picture), OPTION |
| `FRA/MRY2.AFS` | 39 entries | the map's words and area names in Italian |
| `FRA/ITEM12.AFS` / `ITEM22.AFS` | 145..159 | the inventory's words, the file covers |
| `FRA/RDX_LNK2.AFS` | rooms | the messages (202 rooms with messages) and 471 subtitles shown at once (`0xFE00`) |

**Telling it from the clean European disc**: the French folder's system messages write ì and ò (codes 125, 129) 77
times; the clean French never does (it writes ç and ê, 138 and 123, which Italian never does). The Italian entry refuses
the clean European disc ("This is the European disc without the Italian translation.") and other discs; the European
entry refuses the Italian disc ("This disc has the Italian translation: import it with its own entry.": its French
would be Italian). The Turkish and Arabic discs' French is the clean one: the Italian entry refuses them as discs
without the Italian translation, and they refuse the Italian disc likewise.

**Its words**: the informal "tu" ("Premi il tasto Start", "Inserisci una Memory Card", "Hai preso"), questions as an
infinitive ("Prendere ...?", "Sovrascrivere il salvataggio precedente?"), Sì / No, oggetto, documenti (files), mappa,
Memory Card, salvataggio / caricamento, MODALITÀ BATTAGLIA.

## Movies

Every disc's `MOVIE/*.PSS` (18 movies: `MV_000`..`MV_016`, `MV_021`) compared with the US disc's (md5) and, where they
differ, decoded with the port's player and compared picture by picture (one frame a second, the differences located
and looked at; job folder `langmovies/`: `hash.sh`, `mvdump.c`, `scan.py`):

| Disc | Movies that differ | What differs | Taken |
|---|---|---|---|
| Brazilian Portuguese (SLUS-20184) | none | byte for byte the US movies | nothing to take |
| Russian (SLUS-20184) | all 18 (1.18 GB, the US 627 MB) | re-encoded from another source, full height (320x352 where the US has 320x176), the same sound (the US PCM sample for sample; `MV_003`'s, silent on the US disc, is quiet piano music); `MV_003` (the video on the monitor) and `MV_016` (the title screen's demo) are other cuts, `MV_015` (credits) has no "PlayStation 2" staff page. No Russian anywhere | no (not a translation) |
| European (SLES-50306) | all 18 (531 MB) | PAL: 25 frames a second (the port's player paces at 29.97), the same pictures and sound; the credits say "STAFF FOR CONSOLE VERSION" (US: "PlayStation 2"). English, one set for all its languages | no |
| Turkish, Arabic, Italian (SLES-50306) | none against the clean European disc | the patches left the movies alone | nothing to take |
| Japanese (SLPM-65022) | `MV_015` (130.5 MB), `MV_016` (11.7 MB) | the logo "BIOHAZARD -CODE:Veronica- 完全版" in the credits' first seconds and at the demo's end; the rest the same | yes (Japanese) |
| Korean (SLPM-65022) | none against the clean Japanese disc | the Japanese logo, no Korean | no |

No disc has subtitles burnt into a movie: the dialogue under the movies is the rooms' messages (`MessageSet` after
`MovieStart`, e.g. `RM_0000` 49 under the opening's `MV_000`), the Subtitles setting's as everywhere.

**The opening's text crawl** (the story so far and "3 months later..." before the first movie) is not a movie: the
game draws it in `RM_0000` from three of the room's textures (85..87, 512x512, the text written into them). The
Russian and the Brazilian Portuguese patches translated those three pictures; no other disc did (the European
languages, Japanese and Korean show the English crawl, the European ones with the crawl as subtitles, `RM_0000`
34..36; the Arabic `RM_0000` changes one item label, 78, not the crawl). The import takes them (below).

## Voices

Every disc's sounds compared with its base disc's (the job folder `langvoices/`: `cmp.py` the audio archives entry by
entry, `adxscan.py` every ADX of the other archives, `mvcmp.py` the movies' sound decoded, `euvoice*.py` the European
voices decoded and matched): `VOICE1.AFS`, `VOICE2.AFS` (the spoken lines), `BGM1/2.AFS`
(music), `MULTSPQ1/2.AFS` with `.IDX` (each room's sound effects, the player characters' voice banks), `COMMON.MLT`,
every other ADX entry (`ADV.AFS` 3 and 8..31; 3 is the title call) and the movies' sound.

| Disc | Sounds that differ | What | Taken |
|---|---|---|---|
| Russian (SLUS-20184) | `VOICE1.AFS/342`; `MV_003`'s sound | 342, 0.2 s of silence on the US disc, holds a copy of the English line `VOICE1.AFS/524` (1.8 s); `MV_003` (the video on the monitor) has quiet piano music where the US one is silent. No Russian voice anywhere: every other sound is the US one, byte for byte (the movies' PCM sample for sample) | no |
| Brazilian Portuguese (SLUS-20184) | none | | |
| Japanese (SLPM-65022) | `ADV.AFS/3` | the title call (`adv.c TitleCall`, on PRESS START): the Japanese release's own recording (its name is Biohazard), 3.5 s (US: 4.3 s). The voices, music and sound effects are the US ones, byte for byte: English | yes |
| Korean (SLPM-65022) | none against the Japanese disc | the Japanese title call (against the US disc: `ADV.AFS/3`) | yes (the Japanese one) |
| European (SLES-50306) | `VOICE?.AFS` (343 and 291 entries, the US 525 and 515), 26 + 25 of `BGM?.AFS`, 36 + 34 of `MULTSPQ?.AFS` | one set at the root for all its languages: the US recordings in English, the US silent placeholders dropped, some lines joined or cut otherwise (most entries match a US one sample for sample, the rest by their speech's timing); PAL re-encodes of music and effects. The title call is the US one | no (not a language's) |
| Turkish, Arabic (SLES-50306) | none against the clean European disc | | |
| Italian (SLES-50306) | `FRA/ADV2.AFS/3` | the patch's own title call (4.2 s; another recording than the English one, louder) | yes |

No disc has dubbed dialogue: the spoken lines are English on every disc, so the cutscenes' mouth shapes (timed to
the English lines) fit as before, and subtitles of any language go with them as they did. The import already took
`ADV.AFS/3` with the pictures (`pictures`: every entry that differs from the base disc's English; 187 KB Japanese and
Korean, 224 KB Italian) since the first imports of these languages, so no import again.

## How the port uses it

**Import** (`pc/src/import/lang.c`; `import_sources[]` entries `sles50306`, `slus20184ru`, `slpm65022`,
`sles50306tr`, `slus20184pt`, `sles50306ar`, `slpm65022ko`, `sles50306it`, category `IMPORT_CAT_LANGUAGES`): the
sources table `lang_discs[]` names per disc its kind, its executable (checked in `SYSTEM.CNF`: another image is refused
with "Use a European Resident Evil Code: Veronica X disc (SLES-50306).", "Use the US disc with the Russian fan
translation by Brosko (SLUS-20184).", "Use a Japanese Biohazard Code: Veronica Kanzenban disc (SLPM-65022).", "Use the
European disc with the Turkish fan translation by Patriots Çeviri (SLES-50306).", "Use the US disc with the Brazilian
Portuguese fan translation by gledson999 (SLUS-20184).", "Use the Japanese disc with the Korean fan translation
(SLPM-65022).", "Use the European disc with the Italian fan translation by KimeraVX (SLES-50306)."), for a fan
translation the clean base disc's refusal (`fan_patched`: by the patched font or text, above) and per language its
folder, the digit of its file names and its index in the executable's tables. The entries of the clean discs refuse a
fan translation's disc of their base ("This disc has the ... translation: import it with its own entry."; the European
entry: Turkish, Arabic, Italian; the Japanese: Korean). The Turkish entry is the European kind with one language,
Turkish in the Spanish folder and tables, and so are the Arabic one (its glyph codes moved up by `LANG_AR_BASE`, as the
Japanese ones, in the system messages, the rooms and the executable's texts; `SAVELOAD.MES` and `exemes` from the
patch's segment: `eu_texts`) and the Italian one (in the French folder and tables; its `exemes`, the PAL start line and
the game over question, from the French ones the patch rewrote: `eu_texts`); the Brazilian Portuguese one is laid out
as the Russian (its `exemes`, `term` and `SAVELOAD.MES` from the executable, its widths from `FontSz`: 108); the Korean
one is the Japanese kind (its file pictures, its look-alikes measured in its own font, its `SAVELOAD.MES`). The European
discs' archives are compared with their own English, the others' with the US disc's (`g_lang_us_iso`). Every entry's
description names its base disc's game code. Per language, into `<code>/`:

| File | From | How |
|---|---|---|
| `SYSMES.ALD` | European `<DIR>/SYSMES<d>.ALD`; `SYSMES.ALD` | European (and Turkish): rebuilt in the US order (`lang_sysmes`, "System messages"), the language's file pages appended; Russian, Brazilian Portuguese: as it is (its layout checked; the Brazilian Portuguese file, shorter than its table, padded and its last message ended after its last whole line: `ald_cut`); Japanese: its 0..279 and its load question at the US places, the US English elsewhere (`ja_sysmes`) |
| `SYSTEM.AFS/1` (`/2`) | `SYSTEM<d>.AFS` entry 1; entries from 1 | the effect textures with the language's font and its game over picture; Russian: the computer's font |
| `ADV.AFS/n`, `MRY.AFS/n`, `ITEM1.AFS/n`, `ITEM2.AFS/n` | the language's archives | the entries that differ from English; the Japanese title is its picture in the US file (the US plates: the title's layout reads them) |
| `ROOMS.MES` | `RDX_LNK<d>.AFS` | every room's message table, by room index (`"RMES"`, version, count, offset/size pairs) |
| `SAVELOAD.MES` | the executable | `SaveLoadMessage` (Russian, Brazilian Portuguese, Japanese, Korean, Arabic), its 45 messages one after another |
| `RM_0000.RDX` | `RDX_LNK.AFS` entry 0 | Russian, Brazilian Portuguese: the opening's text crawl (`crawl`): the US room with the language's textures 85..87 (where they differ; same sizes), packed as PRS of literals only (`pc_prs_pack_literals`, as the Brazilian Portuguese disc's rooms), 5.8 MB; its messages are the US ones (`ROOMS.MES` gives the language's) |
| `MOVIE/MV_0nn.PSS` | `MOVIE/` | Japanese: the movies that differ from the US disc's (`movies`: size, then the bytes), copied as they are: `MV_015`, `MV_016`, 136 MB |
| `lang.txt` | the executable | written last (a folder without it is not offered), first line `# recvx language 3`: the font's widths (European 162, Russian 192, Japanese and Korean: the look-alikes' widths and `offsets`), the file tables, the number of subtitles; Japanese and Korean `fpics` (the file pictures' first entry), `pageend` (`PlayPageCheck`'s); Russian `exemes` (demo, PAL start, pause, retry lines as u16 hex), `term` (the computer's lines) |

The Japanese text's glyph codes are moved up by `LANG_JA_BASE` (108): codes below it are the US font's, which the US
engine writes itself (digits and times, the item counts, `cnt_mes`, our texts' Latin letters), and are drawn with the
Japanese font's look-alike (glyphs.c `lang_cell`: the letter, else its full-width form, the signs by hand), at the
width of its ink (measured at the import: `widths`, `offsets`), shifted left by its cell's margin (`bhDispFont`); the
Japanese text's own cells keep their 28. The Korean text's codes are moved up the same way and drawn the same way, with
the Korean font's look-alikes (its signs moved: `ko_signs`); a US code without one there (`/ + = ~ < > _` ...: the
patch dropped the Japanese ／ and signs) is drawn from the **US font** at the US width, as the Arabic text's Latin letters
are. The European English is not installed: its text is the US text, and it has no subtitles.

**Run time** (`pc/glue/lang_glue.c`, `lang.h`):

- `lang_scan` (start, after an import or a delete) lists the installed languages; ini `language = fr|de|es|ja|ru|tr|pt-BR|ar|ko|it`
  (English when that language is not installed), ini `subtitles` (below).
- **Pictures** (the language's `SYSTEM.AFS/1`, `/2`, `ADV.AFS/n`, `MRY.AFS/n`, `ITEM1.AFS/n`, `ITEM2.AFS/n`): only the
  textures with the language's writing are the language's. `adx.c ADXF_OpenAfs` asks `pc_lang_disc_parts` for a
  language's file of an archive: `pc_parse.h pc_lang_parts` pairs the k-th TIM2 of the file (raw, or a PVP chunk's
  payload) with the k-th of the disc's entry (the same count and geometry; the offsets may differ, as in the European
  `SYSTEM.AFS/1`) and a texture whose every pixel shows the disc's colour (within 8, alpha 16) is read from the disc
  (`ADXF_ReadNw32` puts the disc's bytes there): a re-encode, no writing changed. An HD texture pack (hashes of the US
  texels and palettes) then replaces it as in English. Measured on the imports (`langicons/tools/tdiff.py`,
  `small.py`): such textures differ by at most 1, every texture with writing by 77 or more. Never when that would
  change a palette bank (the PLI chunk before it) a texture staying the language's draws with (the Russian font is
  drawn with another texture's bank). What this takes from the disc:
  - Turkish: the **item pictures** (`ITEM1/2.AFS/145` textures 3, 4: the patch redrew both sheets with a palette
    each, but both fill palette bank 48, so one sheet showed with the other's colours: the lighter, the Calico, the
    keys scrambled; now the disc's, and the HD pack's), the English map pictures (`MRY.AFS`, all but 1..4), the
    title's, OPTION's and the battle game's pictures without writing (`ADV.AFS/1`, `/2`, `/4`, `/5`, `/32`), two
    effect textures (`SYSTEM.AFS/1`).
  - French, German, Spanish, Arabic, Italian: 12 effect textures of `SYSTEM.AFS/1` (the European disc re-encoded them).
  - Japanese: 11 textures of `SYSTEM.AFS/1` (the same pictures; the kanji pages among them); Korean: 8.
  - Russian, Brazilian Portuguese: nothing (their other pictures all carry writing; the Russian typewriter's
    selection boxes, `ITEM1.AFS/144` texture 2, have 16 colours of their second palette changed).
  The rest is the language's: the fonts, the inventory's words, frame and status screen (`ITEM?.AFS/145` 0..2), the
  file covers (146..159: the book with its word), the map words and area names, the title and OPTION plates, the game
  over picture, the Arabic item model labels (ammunition boxes, ink ribbon: `ITEM1.AFS/11` ... `/124`), the Russian
  typewriter's keys and clipboard (`ITEM1.AFS/144`), the computer's screens with words (Brazilian Portuguese
  `SYSTEM.AFS/2` texture 2). No import again: the imports stay as they are.
- **Translated Pictures** (GAMEPLAY > GENERAL, under Subtitles, shown with an imported language; ini
  `translated_pictures`, default 0 since 2026-11-01): Off reads the archives' pictures from the disc (`pc_lang_file_dir(name)` gives
  no folder for `ADV.AFS/`, `MRY.AFS/`, `ITEM1.AFS/`, `ITEM2.AFS/`) and in `SYSTEM.AFS/1` and `/2` every texture
  but the fonts (`/1` texture 0, Japanese and Korean 0..3; `/2` texture 1, the computer's font) and those the bank rule keeps:
  the pictures are English (the text stays the language's), so an HD pack replaces them; only the font stays the
  language's. The rooms and movies of the language's folder (`RM_0000.RDX`: the opening's text crawl; `MOVIE/`: the
  Japanese logo) are pictures too (`picture_whole`): with it off, the disc's. It applies to the files loaded next (the
  next screen, the next room, the next movie).
- **Translated Voices** (GAMEPLAY > GENERAL, under Translated Pictures, shown when an imported language has a sound
  of its own: `lang_has_voices`, its folder has `ADV.AFS/3`; ini `translated_voices`, default 0): On plays the
  language's title call (`pc_lang_file_dir`: `ADV.AFS/3` follows this setting, not Translated Pictures); Off the disc's.
  `ADXT_StartAfs` streams it from the file (the log's `adxt: start 3/3 ... from <folder>/ADV.AFS/3`), as a mods
  folder's sound. It applies to the next title call. The cutscene voices are the disc's either way, with subtitles in
  any language; in co-op each player hears their own.
- Movies: `movie_glue.c initAll` opens `MOVIE/<name>` through `mod_open_path` (`pc_mod_fopen`: BioRand's pack, the
  mods folder, the language's folder with Translated Pictures on), else the disc's sectors; `player.c movie_open`
  streams from that file. The trace (`RECVX_MOVIE_TRACE`) names where it came from.
- Files: `audio/adx.c mod_open_path` looks for a file the game opens (by name, or `ARCHIVE.AFS/n`) in BioRand's
  pack, the mods folder, then the language's folder (upper-case names; the game asks for `sysmes.ald`), then the
  disc. `pc_lang_file_opened` learns from it which language the loaded font (`SYSTEM.AFS/1`), the title and OPTION
  pictures (`ADV.AFS/2`, `/4`) and the system messages are in; with `SYSMES.ALD` it puts the language's (or the
  US) file tables into `fileview.c fstbl / fsheader`; with the font it puts the language's `SAVELOAD.MES` into
  `adv.c SaveLoadMessage` and its computer lines into `comevt_message_*` (the US ones back for another language).
  `pc_lang_mes` gives `pad.c`, `system.c` and `gameover.c` the language's short lines.
- System messages: after the game reads `SYSMES.ALD` (`system.c`, `bup_00.c`, `ranking.c`, TARGET_PC),
  `pc_lang_sys_mes` checks every message of the language is whole in its place (`pc_mes_at`: it ends before the
  next message starts) and the table has the US count; otherwise it gives the game a table with the US message
  (read from the disc) for each one that is not, and logs it. An older import (lang.txt `# recvx language 1`,
  not Russian; or before 3, Russian, Brazilian Portuguese and Japanese: without the text crawl or the movies:
  `lang_old_import`) is logged at start and noted in IMPORT ("An older import: import again.").
- Rooms: `room.c bhSetRoom` (TARGET_PC) replaces `rom->mesp` with a merged table: per message the language's,
  the US one where the language has none, and where the US message shows nothing (a subtitle) the **Subtitles
  language's** message. Rooms of the mods folder keep their text; BioRand's are translated. `lang_frame` rebuilds
  the table when Subtitles changes in a room (also from or to the US table).
- **Subtitles in another language**: a subtitle of another language than the loaded font's gets its codes moved to
  `LANG_OTHER(language, code)` (0x8000 + language << 11 + code, below 0xD800 for the eleven languages, clear of the
  control codes from 0xFE00 up to fifteen: a static assert in glyphs.c; the Korean codes reach 1403, below 0x7FF; its space
  `0xFF01` becomes code `LANG_OTHER_SPACE`, 0x7FF). `bhDispFont` / `bhDispFontEx` ask `pc_font_glyph` for the cell to draw: such a code is
  drawn from that language's own font, the first 1 (4: Japanese, Korean) textures of its `SYSTEM.AFS/1`, read as
  `bhSetFontTexture` finds them, with its widths (`pc_font_width`). Two such fonts are kept (`other_font`: the
  subtitles' and our texts' languages, each in its own `ps2_dma_alloc` slot). Their colours are the ones the game
  shows (`font_colours`): the game draws its font with the palette bank (`palbuf`) its PLI chunk names, which a
  later texture of the file can overwrite (the Russian fan translation's font is drawn with another texture's
  palette, its own CLUT would show the letters black); that bank, as `bhSetMemPvpTexture` fills it, goes into our
  copy's CLUT. Two languages' `ROOMS.MES` are kept.
- Font: `message.c bhGetFontSize` and the direct `FontSz` reads (`adv.c` AdvEasyDispMessage,
  AutoSaveLoadEasyDispMessage; `system.c`) take the language font's widths while it is loaded (`pc_font_width`), a
  space (`0xFF01`) `pc_font_space` (Japanese 28).
- **Arabic** (`LANG_AR`): the importer moves the Arabic text's codes up by `LANG_AR_BASE` (108, as the Japanese), so
  every code below it is the US engine's own (its English texts, digits, times: the Arabic font has no Latin letters)
  and is drawn from the **US font** (`other_font(LANG_EN)`: the disc's `SYSTEM.AFS/1`, past the mods and language
  folders) at the US widths, except the signs 28..30 (the choice cursor: the patch's ◀). The Arabic font's cells are
  drawn 10 to the left of their place (`shift_of`, `AR_SHIFT`: their ink starts at about 10 of 28), so they sit next
  to the US font's letters (ink from 0) without overlapping, and the patch's messages begin at the box's edge. The game
  over question is drawn where the European executable draws the Spanish one (`pc_lang_mes_x`: x 184, the US 264; the
  patch's text is laid out for it). The title is the European layout (`lang_eu_picture`).
- Japanese and Korean files: `fileview.c PageScroll` draws the page's picture (`pc_lang_file_page`: the language's
  folder's `ITEM1.AFS/n` / `ITEM2.AFS/n` (the Korean pages, whatever Translated Pictures says: they are the files'
  text), else the disc's (the Japanese pages are on the US disc); the first line where the text's first line is)
  instead of its text; `PlayPageCheck` gives the language's hidden pages.
- Korean: a US code without a look-alike cell (`lang_cell` -1) is drawn from the US font (`pc_font_glyph`, as Arabic's
  Latin letters) at its US width (`width_of`); the game's own Korean text never writes one.
- Italian: the title is the European layout (`lang_eu_picture`; the French picture's two-row PRESS START is not used:
  the Italian plate is one row), the game over question at the US place (the European executable draws only the
  Spanish one elsewhere).
- **Textures of our own** (the US plates, the subtitle font, a file page) are TIM2s in memory as one-texture lists
  (`NJS_TEXMEMLIST` pointing at the TIM2, drawn by `Ps2TexLoad`, outside the texture heap: `njLoadTexture` would
  find the loaded language's texture, every copy has the same global index). Their memory is mapped above the
  address window per slot (`memory.c ps2_dma_alloc`), which the DMA reads as host memory (a heap address could read
  as a scratchpad or physical PS2 one). Marked to stay original with an HD pack (`pc_tex_keep_original`).
- The European title picture: its copyright is one block (`DisplayTitleBg` skips the US second block: `lang_eu_picture`),
  the French PRESS START has two rows (`DisplayPressStartPlate`). Our own plate words stay English, in the US
  plates' letters, in every language: the title rows MULTIPLAYER, EXTRA, BIORAND and IMPORT, and in OPTION ENHANCED
  SETTINGS, RETURN TO TITLE, EXIT and the question's YES / NO (the game's own plates are the language's). With
  another language's picture loaded (`lang_picture`), `lang_us_plates` reads resource 2 of the disc's `ADV.AFS/2` or
  `/4` (the title's texture 1, OPTION's texture 0; past the mods and language folders; reread when the screen changes)
  and makes it the current texture list for the letters.
- **Language** (GAMEPLAY > GENERAL, F1 and ENHANCED SETTINGS): English and the installed languages in their own
  names; locked "No other language is installed." or, in a game, "Change it on the title screen." It applies from
  the next screen: leaving OPTION reloads the title in the new language, a game start reads the system messages and
  the font, a room its messages. **Subtitles**: Off and every installed language with subtitles, whatever the game's
  language (default Off; locked "No language with subtitles is installed."); it applies at once, also in a room.
  Ini `subtitles`: 0 Off, 2 + the language's index (`lang.h LANG_*`: fr 3, de 4, es 5, ja 6, ru 7, tr 8, pt-BR 9, ar 10,
  ko 11, it 12); 1, an older
  ini's On, means the game language's.

**Our texts** (`pc/src/host/tr.c`, `tr_table.inc`): `tr(en)` looks the English text up (sorted table, binary search;
an ImGui label's `##id` is kept) and returns the language of the setting, or the English. Translated where they are
drawn: the game-font text functions (`lobby_glue.c text`, `text_w`, `text_wrap`: every screen using `pc_ui_text*`),
the dialogs (`pc_ui_dialog*`), the settings' labels, values, help, headings and lock reasons (both menus), the
bindings' names, the formats of the screens' `snprintf`s, the co-op status and notes, the import's progress, and
the overlay's ImGui labels (F1, F2, F3, F9). Not translated: names of models and rooms the port lists from its
own tables (teleport list, imported models), BioRand's seed strings, the co-op texts sent to the partner (its
lobby note), the games' titles. Test aid: `RECVX_TR_MISSES=file` writes the texts asked for that have no row.

**Arabic display order** (`glyphs.c lang_shape`, pure): the Arabic column is written as normal (logical) Arabic and
turned once, at `tr_init`, into what the game font and ImGui draw left to right, as the patch's own text is: each
letter in its joined form (Unicode presentation forms B, mapped to the font's cells; lam + alef one ligature;
diacritics dropped), each line reversed, runs of Latin letters, digits and printf placeholders (`%d`, `%.4s`) kept left
to right, brackets paired and mirrored. The bidi part is a small subset of the Unicode algorithm (strong classes;
neutrals L between two L, else R; bracket pairs resolved by their content; no embeddings, no number separators;
lines of at most 1024 characters): enough for menu texts. Consequences, checked by ctest `lang`:
- **printf placeholders are filled left to right on screen**, so in display order they must come out in the English
  order: a translation with two placeholders separated by Arabic words is worded so that the one read first is the
  English's last (`من أصل %d لغات تم استيراد %d.` for `%d of %d languages imported.`); placeholders joined by Latin
  or signs (`%d x %d`, `%.4s-%.4s`) form one left-to-right group and keep the English order. ctest `lang` checks the
  conversions' order of every Arabic row after `lang_shape` (the meaning with two `%d` is the translation's job).
- **Wrapping**: a text in display order starts at its right end, so `lobby_glue.c text_wrap` (the menus' help lines)
  and the overlay's `wrapped()` (ImGui `TextWrapped`) take each line from the right (`lang_rtl_cut`).
- **Pieces joined in code** go the other way round in Arabic (`lang_rtl_text`): the settings' label and its
  "(restart needed)", the help line's lock reason, "Restart needed.", help and "Applies when you leave this menu.", the
  F1 window's title and version, F9's language and "(cutscene subtitles)". Other joins in code (a label and a value
  drawn in one string, a bullet before a sentence) read left to right as pieces; each piece itself is right.
- **ENHANCED SETTINGS** is laid out right to left (`options_glue.c`: `rtl`, `mx` mirrors the left-to-right layout's
  positions about the screen's middle when drawing and the pointer when hit-testing): labels and headings end at the
  list's right edge, values (between their arrows, which still lower and raise) end at the middle, the bindings'
  first slot sits next to the label (left/right move to the slot on that side), the help lines are right-aligned
  (`pc_ui_text_wrap_r`), the page counter is on the left and the page keys on the right; Latin values read left to
  right where they stand. Other screens (title menus, co-op lobby, BIORAND, IMPORT, the dialogs, F1) stay
  left-aligned, as the patch's own messages are; values sit between their arrows as in English.
- The language's name in the settings is `العربية` in display order (`lang.c lang_names`); an Arabic word in another
  language's text is drawn with the Arabic font (`pc_glyph_next`: the Subtitles row in any language). The same holds
  for the other scripts (`script_at`): a Hangul, Cyrillic, kana or kanji letter the text's font lacks is drawn with the
  Korean, Russian or Japanese font when that language is installed (한국어, Русский, 日本語 in the Language and Subtitles
  rows and in IMPORT's "Installed:" line of another language).

**Letters** (`pc/src/host/glyphs.c`, pure): our UTF-8 texts go to the game font through `pc_glyph_next` /
`lang_glyph` for the font of the text's language (`lang_font`): the loaded one, or, while the game's font is
still another language's (the Language setting changed in OPTION: the game loads the new font with the next
screen), that language's own font as `LANG_OTHER` codes (English texts: the loaded font, which has ASCII).
European accents at 95..149 (with the US font the letter without its accent: Français -> Francais), the Turkish
letters in the six redrawn cells (the European letters those cells lost show plain), the Brazilian Portuguese
accents at 96..107 (Ç: code 108 and then the 24-wide blank 94, two codes for one letter as the patch writes it;
capitals with accents plain, `[ ] ~` as `( ) -`), the Russian letters (table above), the Japanese kana and kanji at `LANG_JA_BASE` + cell (、 and , show as a space, 。 as ．),
the Korean font's Hangul syllables at `LANG_JA_BASE` + cell (`ko_cells`; ASCII at its US code: the look-alike, else the US
font's letter), the Italian accents as the French ones,
the Arabic presentation forms at `LANG_AR_BASE` + cell (base letters as their isolated form, ، as a Latin comma, a
stand-alone آ as ا: the font has no isolated one), ASCII at its US code everywhere. The Japanese font has only the game's kanji:
our Japanese texts were reworded where a kanji was missing (kana, other kanji, katakana loanwords: 選ぶ -> えらぶ,
字幕 -> サブタイトル, 難易度 -> 難しさ). The Korean font has only the 736 syllables of the patch's text: our Korean texts were
written with them (the translators' `check.py` lists a row's missing syllables; reworded: 협력 플레이 -> 2인 플레이,
캐릭터 -> 인물, 3인칭 -> 후방 시점, 폴더 -> 디렉터리, 스페인어 -> 에스파냐어, 옵션 -> 설정). **ctest `lang`** (`pc/tests/test_lang.c`) checks every translation against
its language's font and lists the texts and letters a font lacks. `pc_glyphs_utf8` turns the game's text back into
UTF-8 for the overlay with the loaded font's letters. The overlay (ImGui's default font) has ASCII and Latin-1 (also
Brazilian Portuguese, Italian); for Japanese, Korean, Russian, Turkish (ğ ı İ ş: Latin Extended-A) and Arabic (the
presentation forms our display-order texts use) it merges a font of the system once such a language is chosen
(Windows: Meiryo, MS Gothic, Yu Gothic / Korean: Malgun Gothic, Gulim / Segoe UI, Arial; Arabic: Tahoma, Arial, Segoe
UI; Linux: Noto Sans CJK / Korean also NanumGothic, Baekmuk Gulim / DejaVu Sans, Noto Sans Arabic), else those letters
stay missing. Test aid: `RECVX_TEST_OVERLAY_FONT=<file>` is tried first.

**Address window** (docs/ARCHITECTURE.md "Data model and memory"): the imported data (`ROOMS.MES`, the merged
tables, the executable's texts, the Japanese glyph index) and the importer's tables are on the heap; the textures
of our own above the window (`ps2_dma_alloc`). The US plates' 257 KB buffer left the window, and so did our texts'
table: `tr_table.inc` is not compiled, its text is in the executable outside the window (Linux: the section
`.trtab`, linked at 0x40000000; Windows: a resource, after the image's game memory, which `memory.c`'s check leaves
out) and `tr_init` reads its rows into the heap (the same sorted table; ctest `lang` checks the rows read are the
compiled ones). The Japanese cells (4 KB) are in the window: `_end` 0x3FCE79E8, about 3.1 MB free (with the table
in the window: 0x3FD4C008; Windows: the end 0x3FC38864, before 0x3FD11A90 with the table and the icon counted).

**Layout**: the European font is monospaced (16 units for most letters, the US font 10..24), so translated text is
wider; the translations were kept short (labels at most about 15% longer, sentences 30%) and the long ones
abbreviated (e.g. German ERW. EINSTELLUNGEN). Rows that still do not fit are scaled down by the menus as before
(`text_fit`); help lines wrap to their 3 lines. Japanese: kana and kanji are 28 wide (twice a US letter) but the
texts are short; Latin letters (titles, names, numbers) take their own widths. Russian: the patch's tight widths.
Turkish: the European font and widths; Brazilian Portuguese: the US font's widths (as English), its accents 14-18.
Korean: every syllable 28 wide as a kanji, a space ours 10 (the game's text 28): our labels about as long as the
Japanese ones (checked with `tr/fit.py` and on screen). Italian: the European font and widths (as French); the IMPORT
screen's DISC and IMPORTED lines are scaled down to the rule's end when longer (all languages).

## Verification

European (2026-10-28, Linux, headless; job folder `lang/`):

- Import: `imp.sh` (headless) and the IMPORT screen (`impmenu.sh`: EXTRA > IMPORT > LANGUAGES > the disc, START
  IMPORT, OK, DELETE IMPORTED LANGUAGES, YES): 3 languages, 202 rooms each, 468-471 subtitles; the US disc as the
  image: refused. A second import gives the same files byte for byte.
- Per language (French, German, Spanish; `ingame.sh`, `scene.sh`, `menus.sh`, `overlays.sh`): title menu (the
  European picture), OPTION (European plates), every ENHANCED SETTINGS page, the lobby (MULTIPLAYER, HOST A GAME),
  EXTRA > IMPORT, BIORAND, F1 / F2 / F3 / F9; in a new game the opening's subtitles, the cell's message, the
  inventory (its pictures and item names), the inventory's help texts, the map, the FILE page and the manual's pages
  (German: the remapped pages), Rodrigo's scene with Subtitles On (subtitles) and Off (none). English with the
  languages installed: unchanged (no subtitles, the US font).
- Our plate words (job folder `platewords/`, `plates.sh`, `hd.sh`): in French, German, Spanish and English the title
  (--dev; MULTIPLAYER selected and not, the EXTRA submenu with BIORAND and IMPORT), OPTION at the title (ENHANCED
  SETTINGS, EXIT) and in a game (RETURN TO TITLE, its YES / NO) show the US plates' English words; the same with the
  HD pack and `hd_textures_ui` (English and German).

Russian and Japanese (2026-10-30, Linux, headless; job folder `lang2/`, screenshots in `lang2/shots/`):

- Import: `imp.sh` (headless) and the IMPORT screen (`imps.sh`: EXTRA > IMPORT > LANGUAGES > each disc, the disc
  through the file dialog's test aid, START IMPORT, OK, DELETE, YES): Russian 203 rooms, 463 subtitles, 80 pictures;
  Japanese 202 rooms, 565 subtitles, 14 pictures; the clean US disc for the Russian entry and the Japanese disc for
  it: refused.
- Per language (`title.sh`, `menus.sh`, `scene.sh`, `ingame.sh`, `extra.sh`): the title (Russian: the patch's plates
  and copyright; Japanese: BIOHAZARD 完全版 with the US plates) with our English words, OPTION and every ENHANCED
  SETTINGS page, the lobby, IMPORT, BIORAND; in a new game the opening (Japanese: its subtitles under the English
  crawl), the cell's message, the inventory (item names, words), the FILE page and the manual's pages (Japanese: the
  pictures, the hidden pages), the map, the typewriter's save screen (`RECVX_TEST_TYPEWRITER`: `SaveLoadMessage`),
  computer 1 (`RECVX_TEST_COMPUTER`: the Russian lines in the Russian computer font), the game over (`RECVX_HP`:
  ВЫ УМЕРЛИ / YOU DIED, the retry question), Rodrigo's scene with subtitles.
- Subtitles in another language: German game with Japanese subtitles, English with Japanese, Russian with Spanish
  (each in its own font), Japanese with Subtitles Off (none); German with `subtitles = 1` (an older ini: German).
- F1 / F2 / F3 / F9 in Japanese and Russian with a CJK font where the overlay looks for one (on this machine none is
  installed: checked with Noto Sans CJK added to the list for the run).
- French, German, Spanish again (`scene.sh`): unchanged. `pc/tools/layout_check.sh` main build vs this build
  (English): identical (vanilla 156, enhanced 311 dumps). ctest all passed (`lang`: every translation against its
  font; `import`); Windows build.

The item pickups (2026-10-31, Linux; job folder `frpickup/`): the maintainer's save 6 (Claire, palace save
room), the Ink Ribbon next to the typewriter. Before: French, Spanish, German and Japanese ran on into "There's
no space ..." and hung. After, imported again: the language's question, Yes / No, "You've taken ..." in every
language and English; with the older imports: the English question and answer, no hang. Also a full inventory
("You can't take any more items"), examining an item, the item box, the typewriter, the IMPORT note.

Turkish and Brazilian Portuguese (2026-10-31, Linux, headless; job folder `lang3/`, scripts there, pictures in
`lang3/img/`):

- Import: both discs headless (Turkish 2 s, 202 rooms, 471 subtitles, 78 pictures; Brazilian Portuguese 1 s, 202
  rooms, 489 subtitles, 42 pictures; `# recvx language 2`); refused: the clean European disc for the Turkish entry,
  the clean US disc and the Russian disc for the Brazilian Portuguese one, the Turkish disc for the European entry
  and the Brazilian Portuguese disc for the Russian one; the clean European disc still imports French, German,
  Spanish byte for byte as before. IMPORT > LANGUAGES lists both entries with their texts.
- Per language (`title.sh`, `menus.sh`, `ingame.sh`, `extra.sh`, `pickup/langs.sh`, `hires.sh`): the title (the
  patches' pictures: the Turkish plates in their own layout with the credit, the Portuguese plates with
  "TRADUZIDO POR GLEDSON999") with our English plate words, OPTION (AYARLAR words / OPÇÕES), every ENHANCED SETTINGS
  page, the lobby, IMPORT, BIORAND, in a new game the cell's message, the opening and Rodrigo's subtitles, the
  inventory (its words, item names, the help lines), the FILE page and manual, the map (Turkish: the English
  pictures, as the patch has them), the typewriter, computer 1 (Portuguese lines in its computer font; Turkish:
  English as for the European languages), the game over (ÖLDÜN with the English retry question; the Portuguese
  picture with "Repetir? Sim Não"), the Ink Ribbon pickup in save 6 (the question with Evet / Hayır, Sim / Não, the
  "taken" message).
- Switching (`sw/switch.sh`): English to Turkish and to Brazilian Portuguese in ENHANCED SETTINGS (the page's
  letters right at once, in the new language's font), Russian and Japanese to Brazilian Portuguese, then OPTION left
  and entered again (the title and OPTION in the new language); F1 in both (Turkish letters from a merged DejaVu
  Sans, added to the font list for the test run only: this machine has no system font).
- Subtitles in another language (`scene.sh`): English with Turkish subtitles, Turkish with Brazilian Portuguese,
  Brazilian Portuguese with Japanese (the new `LANG_OTHER` encoding).
- Pictures (2026-11-01, job folder `langicons/`): `inv.sh` pages through every item (155, 6 per bag: `RECVX_BAG`
  `p<frame>` bags; bag 17, the Hemostatic, in the next room: it starts the cell's cutscene) in every language before
  (the language's `145` whole) and after, against English (`tools/compare.py`, sheets `cmp_<lang>.png`): Turkish
  before: every key item picture scrambled (bags 10..25), after: as English; the other languages: as English before
  and after. With the HD pack (read only, `RECVX_TRACE_HD`, `hd.sh`): Turkish's item sheets now hit the pack's
  replacements; with Translated Pictures off only the language's font misses (Turkish, Arabic).
- ctest `lang` (every Turkish and Brazilian Portuguese translation against its font), the other ctests; French,
  German, Spanish, Russian, Japanese again; `layout_check.sh` main build vs this build (English): identical; Windows
  build.

Arabic (2026-11-01, Linux, headless; job folder `lang_ar/`, pictures in `lang_ar/shots/`):

- Import: headless 2 s, 202 rooms, 471 subtitles, 103 pictures, `SAVELOAD.MES` and the executable's lines (`imp.sh`);
  refused (`refuse.sh`): the clean European disc and the Turkish and US discs for the Arabic entry, the Arabic disc
  for the European and the Turkish entry; the clean European disc and the Turkish disc import byte for byte as before
  (only lang.txt's date and path differ). IMPORT > LANGUAGES lists the entry.
- In Arabic (`title.sh`, `menus.sh`, `ingame.sh`, `extra.sh`, `dead.sh`, `scene.sh`, `pickup/langs.sh`): the title
  (the patch's picture, plates and credit, our English plate words EXTRA, MULTIPLAYER), OPTION (Arabic plates) and
  every ENHANCED SETTINGS page, the lobby, IMPORT, BIORAND (seed string in the US font's letters and digits); in a new
  game the opening's subtitles, the cell's message, the inventory (Arabic words, item names, "استخدام / فحص."), the
  FILE page and the manual, the map (السجن), the typewriter's save screen, the game over ("تم القضاء عليك" with
  "هل تريد المحاولة من جديد؟", ◀ at Yes / No), the Ink Ribbon pickup in save 6 ("هل ستأخذ حبر ؟" with نعم / لا, "لقد
  حصلت على حبر."). The shaping checked on screen: letters joined, words and lines in the right order, Latin and
  numbers left to right, brackets mirrored.
- Switching (`sw/switch_ar.sh`): English to Arabic and Arabic to English in ENHANCED SETTINGS (the page's letters
  right at once), Turkish to Arabic and Arabic to Japanese then OPTION left and entered again (the title and OPTION in
  the new language); F1, F2, F3, F9 in Arabic with DejaVu Sans (`RECVX_TEST_OVERLAY_FONT`: this machine has no font of
  its own): joined, right to left, help lines wrapped from the right.
- The other languages' titles again; `layout_check.sh` main (deb6789) vs this build (English): identical (vanilla 156,
  enhanced 311 dumps); ctest all passed (`lang`: every Arabic translation against its font and its placeholders'
  order, `lang_shape` cases); Windows build.

Korean and Italian (2026-11-01, Linux, headless; job folder `lang_ko_it/`, pictures in `lang_ko_it/shots/`):

- Import (`imp.sh`): Korean 3 s, 202 rooms, 565 subtitles, 248 pictures (the file pages among them), `SAVELOAD.MES`;
  Italian 2 s, 202 rooms, 471 subtitles, 311 pictures, `exemes` press and retry. Refused (`refuse.sh`): the clean
  Japanese disc, the US and the European disc for the Korean entry; the Korean disc for the Japanese and the Russian
  entry; the clean European, the Turkish, the Arabic and the Korean disc for the Italian entry; the Italian disc for the
  European, Turkish and Arabic entries. The other discs imported again: byte for byte as before but lang.txt's date (the
  rooms' padding bytes, uninitialised before, are now zero). IMPORT > LANGUAGES lists both entries with their texts
  (`impl.sh`).
- In Korean and Italian (`title.sh`, `menus.sh`, `ingame.sh`, `extra.sh`, `pickup/langs.sh`), Translated Pictures on
  and off: the title (Korean: BIOHAZARD without 完全版, the US plates; Italian: CARICA PARTITA / NUOVA PARTITA / OPZIONI,
  the European copyright block), OPTION and every ENHANCED SETTINGS page, the lobby, IMPORT, BIORAND; in a new game the
  opening's subtitles, the cell's message, the inventory (words, item names, help), the FILE page and the manual
  (Korean: its own page pictures), the map, the typewriter, the game over (YOU DIED / Retry? Yes No; SEI MORTO /
  Riprovare? Sì No), the Ink Ribbon pickup in save 6 (「잉크 리본」 획득? ▶예 아니요; Prendere Nastro? ▶Sì No, the
  "taken" message).
- Item pictures (`icons/`: every item, 6 per bag, against English): Korean identical with Translated Pictures on and
  off; Italian identical but the list's own word (ELENCO OGGETTI) with it on.
- Switching (`sw/switch.sh`): English to Korean and to Italian in ENHANCED SETTINGS (the page's letters right at once),
  Japanese to Korean, Korean to Italian; Italian with Subtitles 한국어 (the Korean name in the Korean font in Italian
  text); F1 in Korean with Noto Sans CJK (`RECVX_TEST_OVERLAY_FONT`: this machine has no Hangul font) and in Italian.
- The other languages' titles again; `layout_check.sh` main (a2fadf8) vs this build (English): identical; ctest all
  passed (`lang`: every Korean and Italian translation against its font, the Korean cells and look-alikes); Windows
  build.

Movies and the opening's text crawl (2026-11-01, Linux, headless; job folder `langmovies/`, pictures in
`langmovies/shots/`):

- The discs' movies (above, "Movies"): `hash.sh` (md5 of every movie of every disc), `mvdump.c` (the port's demuxer
  and decoder: size, frame rate, frames, sound, every 30th picture), `scan.py` / `grid.py` / `pair.py` (the pictures
  against the US ones). `test_movies` decodes every movie of the US and the Japanese disc without errors.
- Import (`impall.sh`): Brazilian Portuguese 1 s, 21 MB, Russian 2 s, 28 MB (`RM_0000.RDX` each: the US room but the
  three textures, `chk.py`), Japanese 3 s, 144 MB (`MOVIE/MV_015.PSS`, `MV_016.PSS`, byte for byte the disc's), Korean
  as before (no movies). IMPORT (`impmenu.sh`): an older Brazilian Portuguese import shows "An older import: import
  again."; after START IMPORT not.
- A new game (`crawl.sh`): the crawl in Portuguese and in Russian with Translated Pictures on, in English with it off
  (`crawl_*.png`). The Japanese movies (`movie.sh`, `RECVX_MOVIE` 15, 16 in place of the opening's movie, played):
  with it on from the language's folder (the trace: "from a file"), the BIOHAZARD 完全版 logo, 601 and 9896 frames,
  the sound as on the disc (22.46 s); off the disc's (`movie15_*.png`, `movie16_*.png`).
- `layout_check.sh` main vs this build (English): identical; ctest all passed (`parsers`: the literal PRS packing
  decodes back); Windows build.

## Not done, and why

- **English subtitles**: the European English rooms have only the US placeholders; the Japanese disc has Japanese
  ones; there is no English subtitle text on any disc.
- **Translated movies**: no disc has one (see "Movies"); the Russian movies (re-encoded, no Russian in them) and the
  European ones (PAL, English) are not taken.
- **Room textures with writing**: only messages are taken from rooms (the European and Russian rooms also carry
  other changes; the Russian rooms are repacked), and the opening's text crawl (Russian, Brazilian Portuguese). Left
  out: the German rooms 87 (`RM_5500`) and 26 (`RM_1050`), the Brazilian Portuguese signs in 28 rooms, the Arabic
  item label in `RM_0000`, and a non-text texture the Russian patch changed in 134 rooms.
- **The executables' layouts**: the European executable moves text in about 100 places per language, the Russian
  patch moves the memory card screens' and the item box's words; the port keeps the US positions.
- **The European executables' save / load texts**: not imported yet (the European import predates `SAVELOAD.MES`).
- **Japanese**: the US disc's later memory card messages (451..493) have no Japanese counterpart (English); the OPTION
  picture has no green arrows (key configuration TYPE); the title's plates are the US ones (the Japanese plates are
  English and laid out otherwise); the computer is English, as on the Japanese disc.
- **Italian (official)**: planned by Capcom (the executable has its tables) but not on the disc; the fan translation
  (below) uses the French slot instead.
- **Turkish**: the patch's executable texts (the game over's retry question, the PRESS START line, one boot memory
  card message) are written one byte off and read wrongly on the disc too: not taken (English, as for the other
  European languages); the map's pictures are the English ones on the disc; the save / load screens' boot messages
  stay English as for the European languages (the patch translated one of them, also one byte off). The letter ğ's
  breve is faint in the patch's font at small sizes.
- **Brazilian Portuguese**: the patch empties the pause line (`pause_mes`), so it shows nothing, as on the disc;
  capitals with accents are drawn by the patch on the line above, and our texts write them plain (the font has
  none).

- **Arabic**: our menus stay left-aligned (as the patch's messages are) and their arrows, cursors and bullets keep
  the English side; pieces our code joins into one string (outside the cases above) read left to right as pieces;
  `lang_shape` is a subset of the bidi algorithm (no embeddings, no Arabic-Indic number rules). The patch's own layout
  follows the European executable's Spanish positions, which the port keeps US except the game over question: the
  file pages' text may run over the book, as for the other European languages. The computer's lines stay English (the
  patch re-encoded the computer's font only). Digits the engine writes (item counts, times) are the US font's, the
  patch's text keeps its own. A stand-alone آ shows as ا (the font has no isolated form; our translations avoid it).
- **Korean**: the author of the patch is not named on the disc (the entry and the docs say "Korean fan translation").
  What the patch left Japanese or English stays so, as on its disc: "YOU DIED" and the game over's "Retry? Yes No",
  DEMO PLAY, the map's words, the title's and OPTION's menu words (English plates), OPTION's background banner with
  完全版, one item name in Japanese, a few unused system messages reading "[원문 판독 필요]". Our Korean texts use only
  the 736 syllables of the patch's font (reworded where one was missing); a US sign the font lacks (`/ + = ~`) is the
  US font's, a little lighter and narrower than the Korean letters. The computer's lines stay English (the Japanese
  disc's).
- **Italian**: its save / load screens' texts (`SaveLoadMessage_FRA`, rewritten by the patch) are about twice the size
  of the US table the port's game reads and laid out for the European executable's positions: not taken (English, as
  for the clean European languages); the positions the patch moved on the memory card screens are not taken either
  (the port keeps the US layout). The French picture's two-row PRESS START is the Italian one-row plate (drawn as in
  English). The computer's lines stay English (as for the European languages).

## Notes for later

- Adding a text of ours: write it in English as usual; it is translated where it is drawn if it is a literal
  (else wrap its format in `tr()`), and needs a row in `tr_table.inc` (the job folder's `tr/` scripts: `keys.py`
  lists the texts, `check.py` checks a language's file; the table is now edited directly); ctest `lang` checks the
  new row's letters (Japanese: only the game's kanji).
- Another release: a row in `import_sources[]` and `lang_discs[]` (a kind of its own if its layout differs), its
  font's letters in glyphs.c.
