# Languages (imports from other releases)

State document of the language work: what the other releases' discs hold, how the port uses them, and how far it is.
Nothing from any disc is in the repository: the importer reads the player's own image of the European release
(**SLES-50306**: French, German, Spanish), of the US release with the Russian fan translation by Brosko
(**SLUS-20184**, patched: Russian), of the Japanese release (**SLPM-65022**: Japanese), of the European release with
the Turkish fan translation by Patriots Çeviri (**SLES-50306**, patched: Turkish in place of Spanish) or of the US
release with the Brazilian Portuguese fan translation by gledson999 (**SLUS-20184**, patched) and writes what it needs
to `<import_dir>/<source>/<code>/` (default `mods/import/sles50306/fr`, `de`, `es`, `mods/import/slus20184ru/ru`,
`mods/import/slpm65022/ja`, `mods/import/sles50306tr/tr`, `mods/import/slus20184pt/pt-BR`), as the Outbreak import does ([IMPORT_OUTBREAK.md](IMPORT_OUTBREAK.md)). The import is
in the title menu's EXTRA > IMPORT > LANGUAGES, or F9; the Language and Subtitles settings show once a language
is imported.

## PROGRESS

| Step | State |
|---|---|
| 1. Research (this document) | done (European, Russian fan translation, Japanese, Turkish and Brazilian Portuguese fan translations) |
| 2. Importer: IMPORT > LANGUAGES, F9, `pc/src/import/lang.c` | done: French, German, Spanish in about 7 s, 18-47 MB each; Russian 2 s, 22 MB; Japanese 3 s, 7.5 MB; Turkish 2 s, 18 MB; Brazilian Portuguese 1 s, 15 MB |
| 3. Runtime: the language's files, room messages, font (widths, cells, space), file pages (Japanese: pictures), the title and OPTION pictures, the executable's texts, Language setting | done |
| 4. Cutscene subtitles, Subtitles setting: a language of its own, in its own font | done (French, German, Spanish, Russian, Japanese, Turkish, Brazilian Portuguese; English has none on any disc) |
| 5. Our own texts: `tr()`, the table (French, German, Spanish, Japanese, Russian, Turkish, Brazilian Portuguese), the game fonts' letters (glyphs.c, ctest `lang`), the overlay's fonts | done (about 1150 texts) |
| 6. Verification | done: runs per language (below), layout check identical, Linux and Windows builds, ctest |

Resume notes: research scripts (Python, capstone in a venv), the translation pipeline and the runs are in the job
folders `/home/dev/.claude/jobs/c1d3e63f/lang/` (European), `lang2/` (Russian, Japanese: `x/ru`, `x/jp` hold the
discs' files unpacked with `bsdtar`, `jpfont/` the Japanese glyph map's sources) and `lang3/` (Turkish, Brazilian
Portuguese: `x/tr`, `x/pt` with the clean `x/eu`, `x/us`; `tr/` the translation pipeline: brief, chunks, `check.py`,
`fit.py`, `merge.py`), outside the repository. Headless import: `RECVX_MODS=<out>
RECVX_IMPORT=sles50306|slus20184ru|slpm65022|sles50306tr|slus20184pt RECVX_IMPORT_ISO=<image> build/pc/recvx "<US
image>" --save-dir <tmp>` (exit status 0: every language imported). Then `language = de` (or `fr`, `es`, `ja`, `ru`,
`tr`, `pt-BR`) and `subtitles = 0` (Off) / `2 + LANG_*` in recvx.ini.

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
and the computer terminal events, none in the movie player). FMVs stay without subtitles.

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
| `RDX_LNK.AFS` | 196 rooms (839 MB) | the messages (same counts in every room) and 473 subtitles (463 shown at once, `0xFE00`); rooms repacked: their PRS streams end with the data, without the end mark (`pc_prs_expand_ex` lax); textures: the opening's text crawl (4 textures of `RM_0000`) and one non-text texture in 134 rooms |
| `VOICE1.AFS`, `MOVIE/*.PSS` | all movies re-encoded (larger) | not language data the port takes |

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
| `SPA/ITEM14.AFS` / `ITEM24.AFS` | 145..159 | the inventory's words (161, 162, 194, the Spanish file covers, are the English ones again) |
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
| `RDX_LNK.AFS` | 197 rooms (1.37 GB) | the messages (the US counts) and 489 subtitles in the US rooms' placeholders; rooms stored as PRS of literals only (no compression) |

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

## How the port uses it

**Import** (`pc/src/import/lang.c`; `import_sources[]` entries `sles50306`, `slus20184ru`, `slpm65022`,
`sles50306tr`, `slus20184pt`, category `IMPORT_CAT_LANGUAGES`): the sources table `lang_discs[]` names per disc its
kind, its executable (checked in `SYSTEM.CNF`: another image is refused with "Use a European Resident Evil Code:
Veronica X disc (SLES-50306).", "Use the US disc with the Russian fan translation by Brosko (SLUS-20184).", "Use a
Japanese Biohazard Code: Veronica Kanzenban disc (SLPM-65022).", "Use the European disc with the Turkish fan
translation by Patriots Çeviri (SLES-50306).", "Use the US disc with the Brazilian Portuguese fan translation by
gledson999 (SLUS-20184)."), for a fan translation the clean base disc's refusal (`fan_patched`: by the patched font or
text, above) and per language its folder, the digit of its file names and its index in the executable's tables. The
Turkish entry is the European kind with one language, Turkish in the Spanish folder and tables; the Brazilian
Portuguese one is laid out as the Russian (its `exemes`, `term` and `SAVELOAD.MES` from the executable, its widths
from `FontSz`: 108). The European discs' archives are compared with their own English, the others' with the US
disc's (`g_lang_us_iso`). Per language, into `<code>/`:

| File | From | How |
|---|---|---|
| `SYSMES.ALD` | European `<DIR>/SYSMES<d>.ALD`; `SYSMES.ALD` | European (and Turkish): rebuilt in the US order (`lang_sysmes`, "System messages"), the language's file pages appended; Russian, Brazilian Portuguese: as it is (its layout checked; the Brazilian Portuguese file, shorter than its table, padded and its last message ended after its last whole line: `ald_cut`); Japanese: its 0..279 and its load question at the US places, the US English elsewhere (`ja_sysmes`) |
| `SYSTEM.AFS/1` (`/2`) | `SYSTEM<d>.AFS` entry 1; entries from 1 | the effect textures with the language's font and its game over picture; Russian: the computer's font |
| `ADV.AFS/n`, `MRY.AFS/n`, `ITEM1.AFS/n`, `ITEM2.AFS/n` | the language's archives | the entries that differ from English; the Japanese title is its picture in the US file (the US plates: the title's layout reads them) |
| `ROOMS.MES` | `RDX_LNK<d>.AFS` | every room's message table, by room index (`"RMES"`, version, count, offset/size pairs) |
| `SAVELOAD.MES` | the executable | `SaveLoadMessage` (Russian, Japanese) |
| `lang.txt` | the executable | written last (a folder without it is not offered), first line `# recvx language 2`: the font's widths (European 162, Russian 192, Japanese: the look-alikes' widths and `offsets`), the file tables, the number of subtitles; Japanese `fpics` (the file pictures' first entry), `pageend` (`PlayPageCheck`'s); Russian `exemes` (demo, PAL start, pause, retry lines as u16 hex), `term` (the computer's lines) |

The Japanese text's glyph codes are moved up by `LANG_JA_BASE` (108): codes below it are the US font's, which the US
engine writes itself (digits and times, the item counts, `cnt_mes`, our texts' Latin letters), and are drawn with the
Japanese font's look-alike (glyphs.c `lang_cell`: the letter, else its full-width form, the signs by hand), at the
width of its ink (measured at the import: `widths`, `offsets`), shifted left by its cell's margin (`bhDispFont`); the
Japanese text's own cells keep their 28. The European English is not installed: its text is the US text, and it has
no subtitles.

**Run time** (`pc/glue/lang_glue.c`, `lang.h`):

- `lang_scan` (start, after an import or a delete) lists the installed languages; ini `language = fr|de|es|ja|ru`
  (English when that language is not installed), ini `subtitles` (below).
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
  not Russian: `lang_old_import`) is logged at start and noted in IMPORT ("An older import: import again.").
- Rooms: `room.c bhSetRoom` (TARGET_PC) replaces `rom->mesp` with a merged table: per message the language's,
  the US one where the language has none, and where the US message shows nothing (a subtitle) the **Subtitles
  language's** message. Rooms of the mods folder keep their text; BioRand's are translated. `lang_frame` rebuilds
  the table when Subtitles changes in a room (also from or to the US table).
- **Subtitles in another language**: a subtitle of another language than the loaded font's gets its codes moved to
  `LANG_OTHER(language, code)` (0x8000 + language << 11 + code, below 0xC000 for the eight languages; its space
  `0xFF01` becomes code `LANG_OTHER_SPACE`, 0x7FF). `bhDispFont` / `bhDispFontEx` ask `pc_font_glyph` for the cell to draw: such a code is
  drawn from that language's own font, the first 1 (4: Japanese) textures of its `SYSTEM.AFS/1`, read as
  `bhSetFontTexture` finds them, with its widths (`pc_font_width`). Two such fonts are kept (`other_font`: the
  subtitles' and our texts' languages, each in its own `ps2_dma_alloc` slot). Their colours are the ones the game
  shows (`font_colours`): the game draws its font with the palette bank (`palbuf`) its PLI chunk names, which a
  later texture of the file can overwrite (the Russian fan translation's font is drawn with another texture's
  palette, its own CLUT would show the letters black); that bank, as `bhSetMemPvpTexture` fills it, goes into our
  copy's CLUT. Two languages' `ROOMS.MES` are kept.
- Font: `message.c bhGetFontSize` and the direct `FontSz` reads (`adv.c` AdvEasyDispMessage,
  AutoSaveLoadEasyDispMessage; `system.c`) take the language font's widths while it is loaded (`pc_font_width`), a
  space (`0xFF01`) `pc_font_space` (Japanese 28).
- Japanese files: `fileview.c PageScroll` draws the page's picture (`pc_lang_file_page`: read from the disc, the
  first line where the text's first line is) instead of its text; `PlayPageCheck` gives the language's hidden pages.
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
  Ini `subtitles`: 0 Off, 2 + the language's index (`lang.h LANG_*`: fr 3, de 4, es 5, ja 6, ru 7, tr 8, pt-BR 9); 1, an older
  ini's On, means the game language's.

**Our texts** (`pc/src/host/tr.c`, `tr_table.inc`): `tr(en)` looks the English text up (sorted table, binary search;
an ImGui label's `##id` is kept) and returns the language of the setting, or the English. Translated where they are
drawn: the game-font text functions (`lobby_glue.c text`, `text_w`, `text_wrap`: every screen using `pc_ui_text*`),
the dialogs (`pc_ui_dialog*`), the settings' labels, values, help, headings and lock reasons (both menus), the
bindings' names, the formats of the screens' `snprintf`s, the co-op status and notes, the import's progress, and
the overlay's ImGui labels (F1, F2, F3, F9). Not translated: names of models and rooms the port lists from its
own tables (teleport list, imported models), BioRand's seed strings, the co-op texts sent to the partner (its
lobby note), the games' titles. Test aid: `RECVX_TR_MISSES=file` writes the texts asked for that have no row.

**Letters** (`pc/src/host/glyphs.c`, pure): our UTF-8 texts go to the game font through `pc_glyph_next` /
`lang_glyph` for the font of the text's language (`lang_font`): the loaded one, or, while the game's font is
still another language's (the Language setting changed in OPTION: the game loads the new font with the next
screen), that language's own font as `LANG_OTHER` codes (English texts: the loaded font, which has ASCII).
European accents at 95..149 (with the US font the letter without its accent: Français -> Francais), the Turkish
letters in the six redrawn cells (the European letters those cells lost show plain), the Brazilian Portuguese
accents at 96..107 (Ç: code 108 and then the 24-wide blank 94, two codes for one letter as the patch writes it;
capitals with accents plain, `[ ] ~` as `( ) -`), the Russian letters (table above), the Japanese kana and kanji at `LANG_JA_BASE` + cell (、 and , show as a space, 。 as ．),
ASCII at its US code everywhere. The Japanese font has only the game's kanji:
our Japanese texts were reworded where a kanji was missing (kana, other kanji, katakana loanwords: 選ぶ -> えらぶ,
字幕 -> サブタイトル, 難易度 -> 難しさ). **ctest `lang`** (`pc/tests/test_lang.c`) checks every translation against
its language's font and lists the texts and letters a font lacks. `pc_glyphs_utf8` turns the game's text back into
UTF-8 for the overlay with the loaded font's letters. The overlay (ImGui's default font) has ASCII and Latin-1 (also
Brazilian Portuguese); for Japanese, Russian and Turkish (ğ ı İ ş: Latin Extended-A) it merges a font of the system
once such a language is chosen (Windows: Meiryo, MS Gothic, Yu
Gothic / Segoe UI, Arial; Linux: Noto Sans CJK / DejaVu Sans), else those letters stay missing.

**Address window** (docs/ARCHITECTURE.md "Data model and memory"): the imported data (`ROOMS.MES`, the merged
tables, the executable's texts, the Japanese glyph index) and the importer's tables are on the heap; the textures
of our own above the window (`ps2_dma_alloc`). The US plates' 257 KB buffer left the window; the seven languages'
strings of `tr_table.inc` and the Japanese cells (4 KB) are in it: `_end` 0x3FD4C008, about 2.70 MB free (five
languages: 0x3FD303A8; the Turkish and Brazilian Portuguese columns took about 110 KB).

**Layout**: the European font is monospaced (16 units for most letters, the US font 10..24), so translated text is
wider; the translations were kept short (labels at most about 15% longer, sentences 30%) and the long ones
abbreviated (e.g. German ERW. EINSTELLUNGEN). Rows that still do not fit are scaled down by the menus as before
(`text_fit`); help lines wrap to their 3 lines. Japanese: kana and kanji are 28 wide (twice a US letter) but the
texts are short; Latin letters (titles, names, numbers) take their own widths. Russian: the patch's tight widths.
Turkish: the European font and widths; Brazilian Portuguese: the US font's widths (as English), its accents 14-18.

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
- ctest `lang` (every Turkish and Brazilian Portuguese translation against its font), the other ctests; French,
  German, Spanish, Russian, Japanese again; `layout_check.sh` main build vs this build (English): identical; Windows
  build.

## Not done, and why

- **English subtitles**: the European English rooms have only the US placeholders; the Japanese disc has Japanese
  ones; there is no English subtitle text on any disc.
- **FMV subtitles**: none on the European or Japanese discs (one set of movies, no subtitle data). The Russian
  translation re-encoded every movie (larger files, likely with Russian burnt in): not imported (1.2 GB of movies;
  the language folder takes no movies).
- **Room textures with writing**: only messages are taken from rooms (the European and Russian rooms also carry
  other changes; the Russian rooms are repacked). Left out: the German rooms 87 (`RM_5500`) and 26 (`RM_1050`), the
  Russian opening's text crawl (4 textures of `RM_0000`; the crawl stays English) and a non-text texture the Russian
  patch changed in 134 rooms.
- **The executables' layouts**: the European executable moves text in about 100 places per language, the Russian
  patch moves the memory card screens' and the item box's words; the port keeps the US positions.
- **The European executables' save / load texts**: not imported yet (the European import predates `SAVELOAD.MES`).
- **Japanese**: the US disc's later memory card messages (451..493) have no Japanese counterpart (English); the OPTION
  picture has no green arrows (key configuration TYPE); the title's plates are the US ones (the Japanese plates are
  English and laid out otherwise); the computer is English, as on the Japanese disc.
- **Italian**: planned by Capcom (the executable has its tables) but not on the disc.
- **Turkish**: the patch's executable texts (the game over's retry question, the PRESS START line, one boot memory
  card message) are written one byte off and read wrongly on the disc too: not taken (English, as for the other
  European languages); the map's pictures are the English ones on the disc; the save / load screens' boot messages
  stay English as for the European languages (the patch translated one of them, also one byte off). The letter ğ's
  breve is faint in the patch's font at small sizes.
- **Brazilian Portuguese**: the patch empties the pause line (`pause_mes`), so it shows nothing, as on the disc;
  capitals with accents are drawn by the patch on the line above, and our texts write them plain (the font has
  none).

## Notes for later

- Adding a text of ours: write it in English as usual; it is translated where it is drawn if it is a literal
  (else wrap its format in `tr()`), and needs a row in `tr_table.inc` (the job folder's `tr/` scripts: `keys.py`
  lists the texts, `check.py` checks a language's file; the table is now edited directly); ctest `lang` checks the
  new row's letters (Japanese: only the game's kanji).
- Another release: a row in `import_sources[]` and `lang_discs[]` (a kind of its own if its layout differs), its
  font's letters in glyphs.c.
