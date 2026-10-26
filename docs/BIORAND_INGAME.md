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

## The user's decisions (2026-10-26)

1. The CLI comes from the **nightly** release (`biorand-cli-linux-x64.tar.gz` /
   `biorand-cli-win-x64.zip`), not a tagged one.
2. **Co-op** (S5): the host sends its seed string, and the partner can generate the same pack
   from it.
3. **"Randomize Played as"** (S5) picks once per seed.
4. BioRand's licence stays next to the exe as `biorand-cli.LICENSE`. The other extracted files
   (README.md) and the archive are deleted.

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

### S2: secure downloader (done)

Files: `pc/src/net/biorand_dl.c/.h` (the module; its pure parts, the JSON scan and the URL
allowlist, are `static inline` in the header), `pc/src/net/sha256.c/.h` (our own FIPS 180-4
code, nothing borrowed), the ini key in `pc/src/host/config.c`, and `pc/tests/test_biorand_dl.c`.

**API for S3 and S5** (`biorand_dl.h`):

| call | what |
|---|---|
| `biorand_cli_present()` | `<game folder>/biorand-cli[.exe]` is a regular file |
| `biorand_cli_exe()` | that path (S5 runs it), or NULL if `SDL_GetBasePath` fails |
| `biorand_cli_check_update()` | starts the worker: the API only. Ends `BRDL_CURRENT` (installed, and the digest equals the ini's) or `BRDL_NEWER` (another digest, or no CLI). 0 if a job runs already. |
| `biorand_cli_install()` | starts the worker: API, download, verify, extract, install. Ends `BRDL_INSTALLED`. 0 if a job runs. |
| `biorand_cli_cancel()` | the worker kills curl/wget/tar (or leaves the WinHTTP read loop) and ends `BRDL_CANCELLED` |
| `biorand_cli_status(&s)` | any thread, cheap: `s.state`, `s.done` / `s.total` bytes, `s.error` |
| `g_biorand_cli_digest` | ini `biorand_cli_digest`: the installed archive's `sha256:<hex>` |

**Polling** (each frame of the progress screen):

- `state` is `BRDL_API`, `BRDL_DOWNLOAD`, `BRDL_VERIFY` or `BRDL_EXTRACT` while busy.
- `total` is known from `BRDL_DOWNLOAD` on (the API's `size`); `done` follows the file.
- Any state from `BRDL_INSTALLED` on is final. The next start is accepted a moment later.
- `error` is filled only in `BRDL_ERROR`. The texts are short sentences for the screen, for example
  "GitHub allows 60 requests per hour; try again later", "the download is damaged (its SHA-256
  differs from GitHub's); nothing was installed", "cannot download: install curl or wget".
- The thread is detached: quitting the game does not wait for it. A killed run's `biorand-dl/` is
  deleted by the next run.

**What it does** (one worker thread, no shell anywhere):

1. `<base>` is `SDL_GetBasePath()`, never the current directory. It writes only
   `<base>/biorand-dl/` (temporary) and the two installed files. A left-over `biorand-dl/` is
   deleted first.
2. API: `GET https://api.github.com/repos/biorand/classic/releases/tags/nightly` with
   `User-Agent: recvx-enhanced-port` and `Accept: application/vnd.github+json`, into
   `biorand-dl/release.json` (at most 256 KB).
3. The scan (`brdl_find_asset`) is a small bounded JSON reader (16 levels; every string and length
   is checked). It takes the `assets[]` element whose top-level `name` is the platform's asset, with
   `size` 1..100 MB, `digest` exactly `sha256:` + 64 lower-case hex, and a `browser_download_url`
   that passes the allowlist. A `name` in the nested `uploader` object or in the release text does
   not count. GitHub's error answer (no `assets`, a `message`) becomes the error text; the rate limit
   gets its own sentence.
4. Download into `biorand-dl/<asset>`, at most the API's `size` bytes; then the size must match.
   - Windows: WinHTTP. `winhttp.dll` is a system DLL; the static mingw build imports it like ws2_32.
     Automatic proxy, TLS 1.2/1.3, it follows the redirect to `*.githubusercontent.com` (never https
     to http), timeouts 20 s connect and 60 s receive.
   - Linux: `curl -fsSL --proto =https --proto-redir =https --tlsv1.2 --max-redirs 5 --max-filesize
     <size> --connect-timeout 20 --speed-time 60 --speed-limit 1 -A .. -o <file> -- <url>`. If curl
     cannot be started: `wget -q --https-only --max-redirect=5 --timeout=60 --tries=2 ...`.
     Progress: the file's size every 100 ms; a file past the limit kills the child.
5. SHA-256 of the archive against `digest`. A mismatch deletes the archive and reports it.
6. Extract into `biorand-dl/x/` with the system tar, fixed argv:
   - Linux: `tar -xzf <archive> -C <dir>` (from PATH).
   - Windows: `%SystemRoot%\System32\tar.exe -xf`, else `Sysnative`, else `SysWOW64`. None: "Windows'
     tar.exe was not found (it comes with Windows 10 1803 and later)". It starts with SDL's
     background flag (CREATE_NO_WINDOW), so no console window flashes.
7. Install: `x/LICENSE` becomes `<base>/biorand-cli.LICENSE` (decision 4). Then
   `x/biorand-cli[.exe]`, which must be a regular file and not a link, is renamed over
   `<base>/biorand-cli[.exe]`, with `chmod 0755` on Linux. Then `g_biorand_cli_digest` is set and
   `settings_save_later()` called.
8. `biorand-dl/` is deleted in every case (error, cancel). The delete never follows a link or a
   junction out of the folder.

**Allowlist** (`brdl_url_ok`): `https://` only. The host is exactly `api.github.com`, `github.com` or
`<name>.githubusercontent.com` (lower case, no port, no user part). The path is printable ASCII
without `\` or `"`. Redirect targets are not host-checked (curl and WinHTTP keep them https); the
digest covers the content.

**Test aids:** `RECVX_BIORAND_API_TEST` replaces the API URL; a `file:///` URL there also allows
`file:///` asset URLs (Linux curl only). `RECVX_BIORAND_CLI_DIR_TEST` replaces the game's folder.

**SDL processes, for S5:** `SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN` is set on Windows only. On
POSIX, SDL implements it as a double fork, and such a child cannot be waited for or killed.

**Tests:**

- ctest `biorand_dl`:
  - SHA-256: the FIPS 180-4 vectors, a million `a`, input fed in pieces across block edges.
  - The JSON scan: valid, pretty-printed, keys in another order; the other platform only; the name
    only in nested objects or strings; missing or bad digest, size and URL; every truncation of a
    valid answer; junk; too deep; the rate-limit and not-found answers.
  - The allowlist: 5 good and 22 bad URLs.
- ctest `biorand_dl_install` (Linux; skipped without curl or tar): a made-up archive laid out as the
  nightly (`./biorand-cli` with mode 644, `./LICENSE`, `./README.md`) and a release JSON, over
  `file://`. It checks:
  - check-update says newer; install; one job at a time; progress done = total; mode 0755;
  - exactly `biorand-cli` and `biorand-cli.LICENSE` are left; the digest is stored and saved once;
  - a left-over `biorand-dl/` with a symlink to an outside folder is removed without following it;
  - check-update then says current, and newer for another digest;
  - a wrong digest is refused and the installed CLI stays; a wrong size; the rate-limit answer; a
    missing API file; an http:// API URL; cancel; a fresh install.
- The same test with the step-1 archive (`test_biorand_dl DIR biorand-cli-linux-x64.tar.gz`): all
  passed. The installed `biorand-cli` and the LICENSE are byte-identical to the archive's.
- **Live run (2026-10-26, Linux):** `test_biorand_dl --live <job dir>`.
  - Check-update said newer.
  - The install took 2.0 s, with download progress, 7 719 279 bytes, digest `sha256:8465e828…6bb63a8c`.
  - The folder held only `biorand-cli` (ELF x86-64, 0755) and `biorand-cli.LICENSE`.
  - A second check said current.
  - curl 8.22 was used. This machine has no wget, so the wget fallback is untested.
- The ini key: a recvx.ini with `biorand_cli_digest` keeps it through the game's rewrite (first
  start).
- Windows: `recvx.exe` and `test_biorand_dl.exe` build and link with mingw; `recvx.exe` imports
  `WINHTTP.dll`. **Not run on Windows yet** (no machine or wine here). To check there: WinHTTP
  (proxy, TLS, redirect, the 403 body), tar.exe from the 32-bit process (System32, Sysnative,
  SysWOW64), the zip layout (`biorand-cli.exe` at the root), replacing the exe, Defender scanning
  the new file. Run `test_biorand_dl --live <dir>` there; the offline test needs curl and is Linux
  only.

### S3: title entry, Yes/No dialogs, progress screen (done)

Files: `pc/glue/biorand_menu.c` (the screen, new) and `pc/glue/lobby_glue.c` (the title row and the
hand-off). No decomp change.

**The title row** (`lobby_glue.c`):

- `pc_title_menu_init` adds BIORAND after MULTIPLAYER when `biorand_shown()` (`--dev`). Without
  `--dev` the menu is as before (the layout check and a title screenshot are byte-identical).
- The row is **not** in `ModeCommandId`: the PC struct has 5 entries, and with BATTLE GAME BIORAND
  is the sixth row. `br_title_row` holds its index instead. `CursorFlag[0][5]` is
  `CursorFlag[1][0]` (5 columns), which mode 0 sets to 1.
- `pc_title_mouse` (it runs at the start of `CheckButton`) takes Start/OK on that row: mode 10, then
  `NextMode` 30, as CheckButton does for MULTIPLAYER, and `br_screen = 1`. `pc_lobby` (mode 30) then
  calls `pc_biorand_menu()` instead of the lobby. Its 1 is "back", and the title menu fades in with
  the cursor still on BIORAND. `pc_lobby_close` (the title ends, a soft reset) calls
  `pc_biorand_menu_close()`, which cancels a running download.
- The word is cut from the plate texture as MULTIPLAYER is (`br_letters`: B of BATTLE, I O N of
  OPTION, R of NORMAL, A of GAME, D of LOAD; measured from the texture's alpha, a pixel of margin
  each side as `mp_letters`). `plate_word()` draws either word; `pc_title_word_br()` is the export
  for the screen's heading.
- 6 rows: `title_row_y` = 232 + 27 i (the letters are rows 6..26 of the 33 px plate, so the last ends
  at y 393, above the copyright). `title_row_h` gives the mouse its pitch.

**The screen** (`biorand_menu.c`), drawn as the lobby: the title picture darkened, the BIORAND plate
word at the top, a panel, the font through `pc_ui_text` / `pc_ui_text_w` / `pc_ui_text_wrap` /
`pc_ui_rect`. One state machine, `bm.state`:

| state | what | keys |
|---|---|---|
| `BS_MENU` | the page: heading RANDOMIZER, rows `R_CHECK` "CHECK FOR A NEW BIORAND CLI" and `R_BACK`, a placeholder line and a help strip | Up/Down, OK; cancel or right click: back to the title |
| `BS_ASK` | a Yes/No box over the page; `bm.ask` is `A_DOWNLOAD` (no CLI: No leaves the screen), `A_CHECK`, `A_NEWER`. NO is selected first | Left/Right/Up/Down toggle, OK; cancel or right click: No |
| `BS_WORK` | the downloader's job (`bm.install`: install, else check): the stage line, for an install a bar and "3.4 / 7.4 MB", CANCEL. `biorand_cli_status()` every frame; `work_end()` takes the final state | OK, cancel, a click on CANCEL or right click: `biorand_cli_cancel()` ("Cancelling...") |
| `BS_RESULT` | a message and OK: "The BioRand CLI is installed.", "You have the newest BioRand CLI.", or the module's error (orange, "BIORAND CLI - ERROR") | any of OK/cancel/click: `to_menu()` |

- Entering: `biorand_cli_present()` (cached in `bm.have`, refreshed after each job) decides `BS_MENU`
  or `BS_ASK`/`A_DOWNLOAD`.
- `to_menu()` returns to the page, or leaves the screen when there is still no CLI (No, cancel, or an
  error on the first download).
- After a check, `BRDL_NEWER` asks `A_NEWER`; Yes installs. `BRDL_CANCELLED` goes straight back.
- A job that cannot start (the previous thread is still ending) shows "The last download is still
  ending. Try again in a moment." or the module's error.
- Each kind of box has its own depth (`Z_ROW`, `Z_BOX`, `Z_BAR`, `Z_FILL`, `Z_BTN`). "Smooth
  menus" hashes the depth into a polygon's look, so a dialog never pairs with a row's highlight
  and slides out of it. In-between images checked (`RECVX_INTERP_T=0.5`, every frame of the whole
  keyboard path): only the Yes/No highlight moves between frames, as the lobby's row cursor.
- The progress bar is drawn at half pixels (228.5..244.5). With integer rows, the renderer put the two
  triangles of a long flat opaque quad a row apart (a step in the yellow fill; the lobby's
  translucent rects hide it). Now at most the last row differs.

**For S4:** put the generator into `BS_MENU`. Extend the `R_*` rows (`rows[]`, `help[]`, `row_at`
and the Up/Down wrap use `R_N`), and replace the placeholder line in `draw_page()`. Generation can
reuse `BS_WORK` and `BS_RESULT`: add a job kind next to `bm.install`, and let `draw_work()` show
the CLI's `Progress:` percent and message on the same bar. `choose()` is where a row's OK goes.

**Test aids** (job dir `biorand-ui/s3/`, not in git): `fake.sh` makes a release in a folder for
`RECVX_BIORAND_API_TEST=file://...`, with the step-1 archive:

- `fifo`: the asset is a FIFO fed at `RATE` bytes/s, so the progress screen can be seen;
- `fast`: the archive itself;
- `bad`: a wrong digest;
- `SLOWAPI=1`: the API answer comes 3 s after the request (the busy state).

`scen.sh` runs `--dev` with an empty `RECVX_BIORAND_CLI_DIR_TEST` (`PRE=1`: a dummy CLI in it).
`inputs.sh` is the scripted keyboard (`RECVX_KEYS`), gamepad (`RECVX_VPAD`) and mouse
(`RECVX_MENU_MOUSE`) paths. `states.sh` covers the error, newer, cancel and busy states. `live.sh`
is the real GitHub download.

**Tests (2026-10-26, Linux):**

- Scripted keyboard, gamepad and mouse, the same path each: BIORAND, the download prompt, No (back
  to the title), BIORAND again, Yes, the download at 1.5 MB/s, "installed", OK, the page, CHECK, Yes,
  "newest", OK, back to the title. All three logged install state 5 and check state 6, and left
  exactly `biorand-cli` and `biorand-cli.LICENSE`.
- Other states, by keyboard:
  - a wrong digest: the module's error, then OK goes back to the title;
  - a CLI with another digest: the page at once, the check says newer, Yes installs;
  - CANCEL mid-download: `cancelled`, back to the title;
  - a slow API: the busy box, then "newest".
- Live: the nightly from GitHub through the menu into a test folder: 7 719 279 bytes, digest
  `sha256:8465e828…`, then the check said newest.
- Without `--dev`, the title screenshot is byte-identical to the build before S3.
  `layout_check.sh` (the 919ab09 build against S3, with the ISO): vanilla 156 and enhanced 311
  frames identical.
- ctest: all pass. Both the Linux and the mingw (`recvx.exe`) builds pass.
- Screenshots: job dir `biorand-ui/s3/shots/` (title with and without `--dev`, the prompts, the
  progress, installed, the page, busy, newest, newer, the error, a live download).
- Not run on Windows (as S2).

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
   build. Different nightlies may differ; the pack id already detects that. Decided: the host sends
   its seed string, and the partner can generate the pack (S5).
4. **Windows untested here**: tar.exe in SysWOW64 (or Sysnative), WinHTTP redirects, Defender
   scanning a freshly written exe. Downloads made with WinHTTP carry no Mark-of-the-Web, so there
   is no SmartScreen prompt.
5. **The licence**: decided: kept as `biorand-cli.LICENSE`. README and the archive are deleted.
6. Linux without curl or wget: rare; we show an error ("cannot download: install curl or wget"). In-process TLS would be the alternative
   (a new dependency).
7. A cold-cache generation reads 1.8 GB. On an HDD that may take tens of seconds; the progress
   then sits at 8% ("Reading ISO file").
8. "Randomize Played as": decided: once per seed. Open: should it include the imported Outbreak
   models and the event doubles?
9. Keep the menu `--dev` only (planned) until told otherwise.

## STATUS

- **S1 done** (2026-10-26): CLI research, the catalogue `pc/src/host/biorand_options.json` with
  the golden strings, a test generation (4 s, 1.4 GB, the layout matches), this plan.
- **S2 done** (2026-10-26): the secure downloader `pc/src/net/biorand_dl.c/.h` with `sha256.c/.h`,
  the ini key `biorand_cli_digest`, ctests `biorand_dl` and `biorand_dl_install`, and a live install
  from GitHub on Linux. Nothing calls it yet (no UI). A real Windows run is still needed (see "S2",
  Tests).
- **S3 done** (2026-10-26): the BIORAND title row (`--dev`), its screen `pc/glue/biorand_menu.c`
  with the download prompt, the update check, the progress screen and the results. It works by
  keyboard, gamepad and mouse, and was tested headless with a fake release and live (see "S3").
- **Next: S4**: the generator pages, in `BS_MENU` of `biorand_menu.c` (see "S3", "For S4").
- The job-dir tools of S1 are `biorand-ui/enc.py` (a reference encoder that reads the
  catalogue), `gen.sh` and `err.sh`. They are not in git. S4's C encoder replaces enc.py.
