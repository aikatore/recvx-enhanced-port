# BioRand in game: plan and state

This file plans the in-game BioRand generator (branch `biorand-ui`) and tracks its progress.
Each sub-step below should fit one agent session. Update **STATUS** at the end of each step.

The goal: a BIORAND entry on the title menu opens a native generator. It downloads the BioRand CLI
when it is missing, offers BioRand's Code: Veronica options in the game's menu style, and runs the
CLI into `mods/biorand/`, which the port already loads (`pc/src/host/biorand.c`,
docs/modding/README.md "Randomizer (BioRand)"). It is `--dev` only, like BioRand now
(`biorand_shown`).

The option catalogue is `pc/src/host/biorand_options.json`. It holds option metadata and the seed
string's bit layout, with no game data.

## The CLI (researched 2026-10-26, nightly = BioRand 3.3.2, commit c8df994)

### Release and download

- Release `nightly` of https://github.com/biorand/classic. It is rebuilt from `master` on each push
  and has no version in its tag or name.
- API: `GET https://api.github.com/repos/biorand/classic/releases/tags/nightly`. Each entry of
  `assets[]` has `name`, `size`, `digest` (`"sha256:<hex>"`), `updated_at` and
  `browser_download_url`. The download URL redirects to `objects.githubusercontent.com`. Send a
  `User-Agent` header; unauthenticated calls are limited to 60 per hour per IP.
- `biorand-cli-linux-x64.tar.gz` (7.7 MB) holds `./biorand-cli` (17.6 MB, mode 755), `./LICENSE`
  and `./README.md`.
- `biorand-cli-win-x64.zip` (7.1 MB) holds `biorand-cli.exe`, LICENSE and README.md (the same
  build, by its README).
- The digest was checked: `sha256sum` of the Linux archive equals the API `digest`.

### Runtime

- Native AOT (`PublishAot`), x64 only, self-contained. It needs no .NET runtime, and BioRand's data
  is embedded.
- Linux: it needs only libc and libm, glibc 2.34 or newer (Ubuntu 22.04+, SteamOS 3).
- Windows: x64 Windows 10+ (the UCRT is part of the OS). Not tested here.
- Our 32-bit game can start it: a 32-bit process may start a 64-bit one on a 64-bit OS, both on
  Windows (CreateProcess from WOW64) and on Linux (execve). SDL 3.4's `SDL_CreateProcessWithProperties`
  is built into our vendored SDL (`HAVE_SDL_PROCESS`). It can pipe stdout/stderr, set the
  environment and wait for the exit code.
- Time: 4 s for a full pack with a warm file cache (16 cores). A cold cache reads the 1.8 GB ISO
  first.
- **Peak memory: 6.0 GB RSS** (measured, `ru_maxrss`). See Risks.

### Command line

```
biorand-cli [-s seed] [-i input] [-o output] [-f]      (--help, --version; no subcommands)
```

| arg | meaning |
|---|---|
| `-s <string>` | **The seed string** `R840-XXXX-....`. It carries the game (CVX = 4), the seed number and **every option**. A plain number instead uses the options of the last seed saved in settings.json. |
| `-i <path>` | For CVX, the PS2 ISO (or a folder holding `recvx.iso`). Our `g_config.iso_path`, made absolute. |
| `-o <path>` | The mod folder to create: our `<mods>/biorand`, absolute. |
| `-f` | Replace an existing output. It refuses (exit 2) a non-empty folder that does not look like BioRand output (`RM_*.RDX` plus `SYSTEM.AFS/` or `ADV.AFS/`). |

There are no per-option flags and no JSON option file. **Options travel only inside the seed
string** (`RandoConfig.ToString`). The layout is in the catalogue's `seed_string`, with two
golden strings that our encoder must reproduce.

### settings.json

The CLI also reads and writes `settings.json`. It is shared with BioRand's GUI and lives in
`%APPDATA%\biorand` or `~/.config/biorand`, unless `BIORAND_CONFIG` names a file or folder.
**`generateCvPcMod: true` is required**: without it, a CVX seed writes a PS2 ISO
(`recvx_biorand.iso`) instead of the mod folder. After a run the CLI writes `seedCv` and
`lastSelectedGame` back.

We use a private file so the user's GUI settings stay untouched:
`BIORAND_CONFIG=<save_dir>/biorand/settings.json` with

```json
{ "lastSelectedGame": 3, "generateCvPcMod": true, "disableCustomContent": true }
```

`disableCustomContent` matters because BioRand's custom-data folder is always the default
config dir, whatever `BIORAND_CONFIG` says. Turning it off makes one seed give one pack on every
machine.

### Output and progress

- Everything goes to **stderr**; stdout stays empty. Lines (`CliProgress.cs`, `BiorandCli.cs`):
  - `Info: Game: CVX`, `Info: Mode: CVX PC mod`, `Info: Input: ..`, `Info: Output: ..`,
    `Info: Seed: <string as parsed>`
  - `Progress: <0-100>% <message>`. The percent comes from the message text: 0 Starting,
    2 Clearing output, 5 Generating seed, 8 Reading ISO file, 10 Creating backgrounds,
    20 Randomizing doors, 35 Randomizing items, 40 Changing player character, 65 Randomizing
    enemies, 75 Replacing enemies, 80 Randomizing enemy skins, 85 Randomizing portraits, 88 Writing
    RDT files, 96 Writing PC port mod, 100 Done. Unknown messages count +1, up to 95.
  - `Warning: <text>` (for example "player models were not found; ...").
  - `Error: <text>`, the last line on failure.
- Exit codes: **0** done; **2** usage or user error (no `-o`, ISO not found, output not empty
  without `-f`, a foreign folder, seed version not 8); **3** any other failure (for example "File is
  not a valid UDF ISO").
- Where it writes:
  - scratch in `<temp>/biorand-cli/<guid>/` (`TMPDIR` / `%TEMP%`), deleted afterwards;
  - the pack in `<out>.tmp-<guid>/`, then renamed to `<out>` (an old `<out>` becomes
    `<out>.bak-<guid>` and is deleted).
  
  A killed run leaves these `biorand.tmp-*` / `.bak-*` folders behind in `mods/`.
- Pack (checked): 209 entries, 1.4 GB:
  - `RM_*.RDX` (205 room files, literal-only PRS);
  - `SYSTEM.AFS/10`, `SYSTEM.AFS/11` (player models);
  - `ADV.AFS/2` (title background);
  - `log_pl0.txt`, `graph_pl0.dgml`.
  
  This is the layout `biorand.c` and `adx.c` expect.
- The same seed string gave a byte-identical `log_pl0.txt` on a rerun.
- The log's `Seed:` line is the config *after* CVX forced its fields off. With our `fixed` values
  (catalogue) it equals the string we passed, and no `Warning:` lines appear.

### Test runs (job dir `biorand-ui/`, deleted afterwards)

| seed | result |
|---|---|
| `-s 12345`, empty settings | exit 0, 4 s, `R840-T1C0-XXHH4HZZZMEZ9H0007JZZZZZZZ5ZZZ...` (the defaults; 4 warnings) |
| that string again | exit 0, 4.0 s, same log |
| doors on, custom ratios and weapons (catalogue `fixed_example`) | exit 0, 3.9 s; the CLI echoed our string; the log seed is equal |
| error cases | exit 2 / 2 / 2 / 2 / 3 / 2 as listed above |

## Options (catalogue summary)

These are the groups as the GUI shows them for CVX (`MainWindow.xaml`; `ReCvRandomiser`
forces some of them off).

| group | what our menu does | options |
|---|---|---|
| Seed | shown | the seed number (0..1048575), with a "random seed" action |
| Change Player Character | **dropped**: our "randomize Played as" replaces it. For CVX it is a no-op anyway, since BioRand ships no CVX player models. | Claire/Chris becomes |
| Randomize Cutscenes | hidden: not for CVX | — |
| Randomize Doors | shown | toggle; Number of Segments 1..4; Segment Size 0..7; Prioritise cutscenes; an estimated completion time |
| Randomize Items | shown | toggle; Alternative routes; Segmented placement; Hidden key items; **Custom item pool** box with Gunpowder, Ammo, Health and Ink ratios (0..31), Ammo quantity (0..7), the **Random inventory** box (Weapon 1/2: None, Random, Sidearm, Primary, Powerful) and 12 weapons on/off. Include documents is hidden, because CVX forces it off. |
| Randomize Enemies | shown | toggle; **Random enemy placements** box (save rooms, enemies per room 1..16, rooms with enemies 0..100%); Difficulty 0..3; 6 enemy ratios 0..7 (Bat, Zombie, Hunter, Bandersnatch, Zombie Dog, Tyrant) with a bar |
| Enemy Skins, Randomize Music | hidden: not for CVX | — |
| Randomize NPCs | **dropped**: our cutscene cast covers it; CVX forces it off anyway | — |

In all, 26 shown controls:

- Doors: 1 toggle and 3 options.
- Items: 1 toggle and 13 options. These include 2 box toggles, 4 ratios, 2 enums and the 12-entry
  weapon list.
- Enemies: 1 toggle and 6 options. These include the 6-entry ratio list.
- Seed: 1.

Dependencies:

- Doors on forces Items on, Segmented on and the custom pool on, and greys Alternative routes
  and Segmented.
- The children of a box toggle are greyed while it is off.

**Item distribution** (`UpdateItemPie`): Keys is a fixed 1/8. The other 7/8 split by the four
ratios. The GUI draws a pie with these colours: Keys light blue, Gunpowder grey, Ammo indian red,
Health spring green, Ink black. We draw one horizontal bar with the same slices (as the GUI's
enemy bar).

Not in the seed string (settings.json only): the PS2 door skip, custom content, and RE1-only and
RE2-only tweaks. None are shown.

## Design per sub-step

### S2: secure downloader (no UI)

New files: `pc/src/net/fetch.c/.h`, `pc/src/net/sha256.c/.h` and `pc/src/host/biorand_cli.c/.h`.

**HTTPS:**

- Windows: **WinHTTP** in process (`winhttp.dll` is in every Windows, 32-bit included). It uses
  the system certificate store and follows the HTTPS→HTTPS redirect by default. A read loop counts
  bytes for the progress. Link `winhttp`.
- Linux: a **`curl` child process**, with `wget` as the fallback, started with
  `SDL_CreateProcess`:
  - `curl -fsSL --proto =https --tlsv1.2 -A recvx-enhanced-port -o <file> <url>`, or with
    `-H "Accept: application/vnd.github+json"` and stdout for the API call;
  - the fallback: `wget -q -O <file> <url>`.
  
  Why: the game is a 32-bit process. In-process TLS would need a new dependency and a CA bundle
  (mbedTLS/BearSSL), or a 32-bit `libcurl.so` that most systems lack (dlopen). The system's
  64-bit curl has the system's certificates and costs no dependency. Progress comes from polling
  the output file's size every 100 ms against the API `size`. Neither tool found: the error "Install
  curl or wget".

**The steps** (`biorand_cli.c`) run on a worker thread. Its state, bytes done and total, and the
error text are atomics or under a mutex for the UI.

1. API JSON into memory (max 256 KB). Find the asset by name: `biorand-cli-win-x64.zip` or
   `biorand-cli-linux-x64.tar.gz`. A plain string scan is enough: the first `"size":`,
   `"digest":"sha256:` and `"browser_download_url":"` after `"name":"<asset>"` (no JSON parser in
   the port; the nested `uploader` object has none of those keys).
2. Download to `<base>/biorand-cli.download`. `<base>` is `SDL_GetBasePath()`, the folder of the
   game's executable, so the same volume makes the rename atomic.
3. Check the size and **SHA-256** (portable `sha256.c`, about 100 lines, with the NIST "abc"
   vector as its test) against `digest`. On a mismatch, delete it and show an error.
4. Extract into `<base>/biorand-dl/`:
   - Windows: `%SystemRoot%\System32\tar.exe -xf <zip> -C <dir>`. Windows 10 1803+ has bsdtar,
     which reads zip. From our 32-bit process System32 is redirected to SysWOW64: verify that
     SysWOW64 has tar.exe, or use `%SystemRoot%\Sysnative\tar.exe`.
   - Linux: `tar -xzf`.
5. Move `biorand-dl/biorand-cli[.exe]` to `<base>/biorand-cli[.exe]` (`SDL_RenamePath`, which
   replaces). Then `chmod 0755` (Linux), and delete `biorand-dl/` with its LICENSE and README, and
   the archive.
6. Remember the installed `digest` in recvx.ini (`biorand_cli_digest`). "Check for a new BioRand
   CLI" compares it with the API's digest. The nightly has no version, so the digest is the
   identity.

**API** (`biorand_cli.h`):

- `biorand_cli_path()`: the executable, or NULL when missing.
- `biorand_cli_start(int check_only)` starts the worker. `biorand_cli_state()` returns idle,
  api, download (with done/total), verify, extract, done, newest (no update) or error (with text).
- `biorand_cli_cancel()`.

**Test** (Linux, no network):

- `RECVX_BIORAND_API=file:///.../nightly.json`; curl reads `file://`. The JSON's asset URL also
  points to a local `file://` archive.
- The check: a good archive installs, and a wrong digest is refused.
- `sha256.c` gets its own vector check.

### S3: title entry, Yes/No dialogs, progress screen

- `lobby_glue.c pc_title_menu_init` adds a row as MULTIPLAYER does: `TITLE_BR` in
  `ModeCommandId`, only when `biorand_shown()`.
  - Its word is in the game's font (the plates' texture lacks B, O, D), or cut from letters as
    `mp_word` does.
  - Check `title_row_y` for 6 rows: NEW GAME, LOAD, OPTION, BATTLE, MULTIPLAYER, BIORAND. Its
    last row ends at about y 384, just above the copyright at 396.
- A full screen in the style of the MULTIPLAYER lobby (`pc_ui_text`, the lobby's helpers and
  input). States:
  - `MENU`: the generator (S4).
  - `ASK_DOWNLOAD`: "The BioRand CLI is missing. Download it from GitHub (biorand/classic, about
    7 MB)?" Yes or No; No goes back to the title.
  - `ASK_UPDATE`: "Check GitHub for a new BioRand CLI?"
  - `PROGRESS`: a bar, "3.1 / 7.1 MB" (download) or the CLI's message and percent (generate), and
    Cancel.
  - `RESULT`: an error or "BioRand CLI is up to date / installed".
- Entering with no CLI goes to `ASK_DOWNLOAD` first. The update action is a row in the generator
  ("Check for a new BioRand CLI").
- Mouse and pad as on the lobby screen (`pc_title_mouse`).

### S4: the generator pages

- `pc/src/host/biorand_opts.c` holds a C table from the catalogue. Each row has the key, type,
  range, default, label, help and parent/disabled_if. It also has the config struct, the
  **encoder** and the **decoder** of the seed string, and a unit test with both golden strings.
  After that, delete `biorand_options.json`: the C table is the one source.
- Persist only **one ini key, the seed string** (`biorand_config`); it holds every option. Decode
  it when the menu opens. A pasted or typed seed string from BioRand's GUI or a friend works the
  same way (the GUI's text box does the same).
- Rendering: reuse `options_glue.c`'s row renderer (label, value, help line, group headings,
  locked rows) by letting it take a row table other than `g_settings`. Add one row kind, the
  **distribution bar**: coloured slices with names, used twice (items, enemies). The doors group
  shows the GUI's completion-time estimate.
- Pages: SEED (seed, random seed, Generate, "Randomize Played as", "Cutscene cast", "Check for a
  new BioRand CLI"), DOORS, ITEMS, INVENTORY & WEAPONS, ENEMIES.
- Fields the menu does not show are written as the catalogue's `fixed` values.

### S5: Generate

- Only at the title, and not while a co-op session or lobby is open (as `biorand_locked`).
- Write the private `settings.json` (above).
- Run with `SDL_CreateProcessWithProperties`: args
  `[cli, -s, <string>, -i, <abs iso>, -o, <abs mods>/biorand, -f]`, env plus
  `BIORAND_CONFIG`, stderr to stdout, piped.
- A worker thread reads lines: `Progress:` updates the bar, `Warning:` goes to the log and a
  notice, `Error:` keeps the message. Then `SDL_WaitProcess` gives the exit code: 0, 2 or 3.
- Cancel is `SDL_KillProcess`. Before each run, delete leftover `mods/biorand.tmp-*` and
  `mods/biorand.bak-*`.
- `mods` is `RECVX_MODS` or `mods` under the current directory, made absolute.
- On success: `biorand_scan()`, then BioRand on (`g_config.biorand = 1`,
  `settings_save_later`). Show "Seed N generated", log the string, and back to the menu.
- **Randomize Played as** (on/off, ini): after a successful generation, pick a "You appear as"
  entry (`settings.c` SP_APPEAR list, `appear_count`) from the seed number and set it through
  that setting's `set`. The same seed gives the same look.
- **Cutscene cast**: the existing setting (`cast_random`, `g_enemy_opts.cast`), shown as a row in
  this menu (find it by its variable, as `lobby_glue.c` does). Nothing else is needed.
- Error texts: no ISO path, no CLI, exit 2 or 3 with the CLI's `Error:` line, the process could
  not be started.

### S6: polish

- `--dev` gating: the title row, the menu and the ini keys exist only with `--dev`. Check the
  game without `--dev` too.
- Docs:
  - docs/USER_GUIDE.md "Randomizer (BioRand)" and docs/modding/README.md, from "generate with
    BioRand's GUI" to "or in game: BIORAND";
  - this file's STATUS.
- Tests: the encoder/decoder test, sha256, the downloader on `file://`, and a headless generation
  with a test CLI.
- A test on a real Windows machine (WinHTTP, tar.exe from WOW64, CreateProcess of the 64-bit
  exe). No CHANGELOG entry until the user says so (BioRand is held back).

## Risks and open questions

1. **Memory: the CLI peaked at 6.0 GB RSS** for one CVX pack. With the game running, 8 GB
   machines may swap or fail. Ask IntelOrca whether it can be lowered: it reads the whole ISO
   and keeps every room. Show a note on the generate page?
2. **Nightly drift.** The nightly is rolling and unversioned. A new seed-string version gives
   exit 2 ("Seed version N is not supported"), and a changed layout makes our encoder wrong.
   - The S5 error should then say "update the game or use an older CLI".
   - Should we pin a tagged release instead of `nightly` once IntelOrca's port changes are in a
     release?
3. **Co-op**: partners need the same pack, which takes the same seed string **and** the same CLI
   build. Different nightlies may differ; the pack id already detects that. Should the lobby send
   the host's seed string so the guest can generate it?
4. **Windows untested here**: tar.exe in SysWOW64 (or Sysnative), WinHTTP redirects, Defender
   scanning a freshly written exe. Downloads made with WinHTTP carry no Mark-of-the-Web, so there
   is no SmartScreen prompt.
5. **The licence**: the user asked us to delete LICENSE and README after extraction. That is
   fine for a user's own download (not a redistribution). Should we keep `biorand-cli.LICENSE`
   anyway?
6. Linux without curl or wget: rare; we show an error. In-process TLS would be the alternative
   (a new dependency).
7. A cold-cache generation reads 1.8 GB. On an HDD that may take tens of seconds; the progress
   then sits at 8% ("Reading ISO file").
8. "Randomize Played as": should it pick once per generated seed (planned) or anew each new
   game? Should it include the imported Outbreak models and the event doubles?
9. Keep the menu `--dev` only (planned) until told otherwise.

## STATUS

- **S1 done** (2026-10-26): CLI research, the catalogue `pc/src/host/biorand_options.json` with
  the golden strings, a test generation (4 s, 1.4 GB, the layout matches), this plan.
- **Next: S2**, the secure downloader.
- The job-dir tools of S1 are `biorand-ui/enc.py` (a reference encoder that reads the
  catalogue), `gen.sh` and `err.sh`. They are not in git. S4's C encoder replaces enc.py.
