# BioRand in game: plan and state

This file plans the in-game BioRand generator (branch `biorand-ui`) and tracks its progress: the developer's and
state document. The player's usage is in docs/modding/README.md "Randomizer (BioRand)".

**STATUS: All steps done; open: Windows real-machine test list** (in "S6").

The goal: a BIORAND entry on the title menu opens a native generator. It downloads the BioRand CLI
when it is missing, offers BioRand's Code: Veronica options in the game's menu style, and runs the
CLI into `mods/biorand/`, which the port already loads (`pc/src/host/biorand.c`,
docs/modding/README.md "Randomizer (BioRand)"). It is `--dev` only, like BioRand now
(`biorand_shown`).

The options, the seed string's codec and the menu's row table are `pc/src/host/biorand_opts.c/.h`
(S4a), with no game data. S1's catalogue `biorand_options.json` was folded into it and deleted.

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
string** (`RandoConfig.ToString`). The layout is `layout[]` in `biorand_opts.c`; ctest
`biorand_opts` holds S1's two golden strings.

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
- The log's `Seed:` line is the config *after* CVX forced its fields off. With our forced values
  (`biorand_opts_rules`) it equals the string we passed, and no `Warning:` lines appear.

### Test runs (job dir `biorand-ui/`, deleted afterwards)

| seed | result |
|---|---|
| `-s 12345`, empty settings | exit 0, 4 s, `R840-T1C0-XXHH4HZZZMEZ9H0007JZZZZZZZ5ZZZ...` (the defaults; 4 warnings) |
| that string again | exit 0, 4.0 s, same log |
| doors on, custom ratios and weapons (golden string 2 of ctest `biorand_opts`) | exit 0, 3.9 s; the CLI echoed our string; the log seed is equal |
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
6. Extract into `biorand-dl/x/` with the system tar, fixed argv. It runs in `biorand-dl/` with
   relative arguments (S6):
   - Linux: `tar -xzf biorand-cli-linux-x64.tar.gz -C x` (from PATH).
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

S4a (done) is the model below; S4b (done) is the pages. The plan's first ideas follow; S4b changed
two of them: the pages have their own small renderer in `biorand_menu.c` (options_glue.c stays as
it is), and the pages are SEED, DOORS, ITEMS (with the inventory and weapons), ENEMIES and PORT
EXTRAS ("Randomize Played as" and the cutscene cast).

- `pc/src/host/biorand_opts.c` holds the config struct, the **encoder** and the **decoder** of the
  seed string, and the row table. `biorand_options.json` is deleted: the C table is the one source.
- Persist only **one ini key, the seed string** (`biorand_seed`); it holds every option. Decode
  it when the menu opens. A pasted or typed seed string from BioRand's GUI or a friend works the
  same way (the GUI's text box does the same).
- Rendering: reuse `options_glue.c`'s row renderer (label, value, help line, group headings,
  locked rows) by letting it take a row table other than `g_settings`. Add one row kind, the
  **distribution bar**: coloured slices with names, used twice (items, enemies). The doors group
  shows the GUI's completion-time estimate.
- Pages: SEED (seed, random seed, Generate, "Randomize Played as", "Cutscene cast", "Check for a
  new BioRand CLI"), DOORS, ITEMS, INVENTORY & WEAPONS, ENEMIES.
- Fields the menu does not show are written with their forced values (`biorand_opts_rules`).

#### S4a: the option model and the seed-string codec (done)

Files: `pc/src/host/biorand_opts.c/.h` (pure C, no SDL), the ini key in `config.c`, and
`pc/tests/test_biorand_opts.c`. Nothing in the game calls it yet.

**`BioOpts`** holds every field of the seed string as an `int`, in BioRand's names (`RandomDoors`,
`RatioAmmo`, `EnemyRatios[12]`, ...). A decoded string keeps the hidden fields too, so it re-encodes
byte for byte.

**API for S4b and S5** (`biorand_opts.h`):

| call | what |
|---|---|
| `biorand_opts_default(&o)` | `new RandoConfig { Game = 4 }` (what the CLI uses for `-s <number>`) |
| `biorand_opts_rules(&o)` | the forced fields: Version 8, Game 4, variant 0, no player change, NPCs, cutscenes, events, music or documents; weapon slots 12-14 = 1 and enemy slots 6-11 = 7, as the GUI writes them; doors on: items on, segmented, custom pool. `set()` runs it. |
| `biorand_seed_encode(&o, buf, n)` | the 68-character string; -1 if `n` < 69 |
| `biorand_seed_decode(str, &o)` | NULL, or a sentence for the screen (o untouched): empty, not a seed string, "Seed version 7 is not supported: this game reads version 8.", "This seed string is for RE2, not Code: Veronica.", "... has 17 characters; it needs 68.", "... a wrong character at position 23.". Spaces around it and lower case are accepted. |
| `biorand_opt[]`, `biorand_opt_n` | the rows, in menu order: `key` (BioRand's name, `EnabledWeapons[3]` for an entry), `group` (`BOG_SEED/DOORS/ITEMS/ENEMIES`, names in `biorand_groups[]`), `type` (`BO_BOOL`, `BO_RANGE`, `BO_ENUM` with `names`, `BO_RATIO`), `lo`/`hi`/`def`, `flags`, `parent` (key of the box toggle), `label`, `help` (NULL for Weapon 1/2), `low`/`high` (slider ends) |
| `biorand_opt_find(key)` | the row index, or -1 |
| `biorand_opt_get(&o, i)` / `biorand_opt_set(&o, i, v)` | the shown value (Custom item pool is `!ShuffleItems`, `BOF_INVERT`); set clamps, ignores a locked row, then applies the rules |
| `biorand_opt_active(&o, i)` | every parent box is on. 0: the GUI greys the row; S4b greys or hides it. |
| `biorand_opt_locked(&o, i)` | "Randomize Doors sets this." for the `BOF_DOORS` rows (Randomize Items, Allow alternative routes, Segmented placement, Custom item pool) while doors are on; else NULL |
| `biorand_item_dist(&o, s[5])` | Keys, Gunpowder, Ammo, Health, Ink: `name`, `rgb` (the GUI's colours), `pct` (sums to 100; Keys 12.5, or 100 when all four ratios are 0) |
| `biorand_enemy_dist(&o, s[6])` | the CVX enemies with a ratio above 0, `pct` summing to 100; returns their count (0: every ratio is 0) |
| `biorand_doors_estimate(&o, buf, n)` | "4 to 6 hours per scenario", "15 to 30 minutes per scenario", or "" with doors off |
| `g_biorand_seed`, `biorand_opts_load(&o)`, `biorand_opts_save(&o)` | ini `biorand_seed`. Load: decode and apply the rules; an empty key gives the defaults plus the rules, an invalid one the same and a log line. Save: encode, `settings_save_later()`. |

The 42 rows: Seed; Randomize Doors (box), Number of Segments, Segment Size, Prioritise rooms with
cutscenes; Randomize Items (box), Allow alternative routes, Segmented placement, Hidden Key Items,
Custom item pool (box: the 4 ratios, Ammo average quantity, Random inventory (box: Weapon 1, Weapon
2), the 12 weapons); Randomize Enemies (box), Random enemy placements (box: save rooms, enemies per
room, rooms with enemies), Difficulty, the 6 enemy ratios.

- Labels and help texts are BioRand's. The help of the group toggles, the seed, the weapons and the
  enemy ratios is ours (BioRand has none); the enemy ratios end with the GUI's note "Some rooms can
  only contain certain enemies."
- Rooms with enemies is inside the placements box, so it greys with it, as in the GUI (the
  `CheckGroupBox` template greys its whole content; S1's note said otherwise).
- The seed string has **no checksum**: BioRand's reader takes any character, so the decoder's
  checks are the shape (R, version, game, length, the dashes, the alphabet). Values are exactly
  `FromString`'s: GameVariant capped at 1, the unused bits dropped.

**Tests:**

- ctest `biorand_opts`:
  - both golden strings encode from their options and decode back to them; golden 1 with the rules
    gives the CLI's log seed of S1 (`R840-T1C0-T4HH...`);
  - lower case with spaces, and GameVariant capped;
  - every row at lo, hi and its default, with doors off and on: `get` returns it (unless locked) and
    the struct round-trips byte for byte; out-of-range values are clamped; 2000 random option sets
    round-trip; a string of all ones decodes and its re-encoding decodes to the same;
  - the visibility and lock rules;
  - rejected without a crash and with `o` untouched: every truncation of a golden string, 10 bad
    characters at every position, other games and versions, NULL, too long;
  - the distributions sum to 100 (5000 random sets), the all-zero cases, the estimate's four forms;
  - the ini: no key, an invalid key (one log line), golden 2, save.
- **CLI cross-check** (manual, job dir `biorand-ui/s4a/cli_check.sh N SEED`): `test_biorand_opts
  --random N SEED` prints N random option sets; each runs through the real nightly CLI. 5 sets
  (2 with doors on): each exit 0, the CLI echoed our string, `log_pl0.txt`'s seed equals it, no
  `Warning:` lines.
- The ini key: a recvx.ini with `biorand_seed` keeps it through the game's rewrite (first start).
- ctest: all pass. The Linux and mingw builds (`recvx.exe`, `test_biorand_opts.exe`) pass.
  `layout_check.sh` (the 9c219d8 build against S4a, with the ISO): vanilla 156 and enhanced 311
  frames identical.

#### S4b: the generator pages (done)

Files: `pc/glue/biorand_menu.c` (`BS_MENU`), the clipboard read in `pc/src/host/lobby.h` /
`lobby_io.c`, the ini key `biorand_appear` in `config.c` and `biorand_opts.h`. No decomp change.

**The pages** (`bm.page`; `rows[]` rebuilt every frame by `build_rows()`). Row 0 is the page row, as
in ENHANCED SETTINGS: `< ITEMS >`, the page keys (L1/R1 by name, Q/W on the keyboard) and `3/5`.

| page | rows |
|---|---|
| SEED | Seed (a number: left/right step 1; confirm types it); RANDOM SEED; COPY SEED STRING; PASTE SEED STRING; GENERATE (greyed, `K_GEN`); CHECK FOR A NEW BIORAND CLI; BACK. Below: the seed string. |
| DOORS | the 4 `BOG_DOORS` rows; BACK. Below: "ESTIMATED COMPLETION TIME" (`biorand_doors_estimate`), or "Doors are not randomized." |
| ITEMS | the 23 `BOG_ITEMS` rows (it scrolls); BACK. Below: the item distribution bar, or a note (items off, custom pool off). |
| ENEMIES | the 12 `BOG_ENEMIES` rows; BACK. Below: the enemy distribution bar, or a note (enemies off, every ratio None). |
| PORT EXTRAS | Randomize Played as (`g_biorand_appear`); the cutscene cast's rows of the settings table (`cast_random`, and Character models, Assignment, Include imported models as their `shown()` says); BACK. |

- A `K_OPT` row is drawn by its `type`: On/Off, a number, an enum's name, or a slider (`BO_RATIO`,
  and `BO_RANGE` with `low`/`high`): the end names around a small bar, no number (as the GUI). Rows
  are indented by their box depth (the `parent` chain).
- `row_why()` greys a row and makes it read-only: `biorand_opt_locked` ("Randomize Doors sets
  this."), an inactive box ("Needs "Custom item pool" on."), a locked setting, GENERATE. The help
  strip shows the reason in orange before the row's help text; a change is refused with the buzz.
- Each change: `biorand_opt_set`, then `saved()`: `biorand_opts_save` (ini `biorand_seed`), the string
  shown, and a log line `biorand menu: <key> = <v>, seed string <s>`.
- Seed typing: the lobby's field code (`g_lobby_io.text_on`, digits, Backspace, Delete, Home/End,
  Enter, Esc; the gamepad's up/down change the digit at the caret). At most 7 digits; set clamps to
  1048575.
- COPY: `g_lobby_io.clip` and `clip_req`, a message with the string. PASTE: `g_lobby_io.paste_req = 1`;
  the main thread (`lobby_io_frame`) puts `SDL_GetClipboardText()` into `g_lobby_io.paste` (159
  characters) and sets 2; the menu decodes it then. An error is the decoder's sentence (orange); a
  good one replaces the options (rules applied) and says "Pasted: seed N and its options."
- Messages (`say()`) last 4 s or until the selection or the page changes.
- The bars (`dist()`): one stacked bar (on a grey frame so the black slices show, 1 px gaps) and a
  legend of swatches with `name pct%` (the percent rounded), wrapping to a second line. The colours
  are the GUI's (`biorand_item_dist`, `biorand_enemy_dist`).
- Input: up/down rows (wrapping), left/right values (enums wrap, ranges stop at the ends), confirm
  (bool and enum: next; seed: type; actions), L1/R1 or Q/W pages, cancel or right click back to the
  title. Mouse: hover selects; a click on the label confirms, on the value's `<` steps down and
  elsewhere up, on a slider's track sets the value there, on the page row's halves changes the page;
  the wheel moves the selection a row per notch.
- Layout (640x480, the 4:3 part): page row y 70, 10 rows from y 102 at 21 px (more scroll, with the
  ENHANCED SETTINGS triangles), the info area from y 314, the help strip from y 384. Widescreen is
  pillarboxed as the lobby.
- **Smooth menus**: the title's 2D depth is about 131085 * z, clamped at 65534, so every z is below
  0.5 (from 0.5 on all depths are one, and the tags pair anything). Each box kind has its own depth;
  the slices and swatches one per id (item index, enemy row index); a row's arrows, slider track and
  fill one per row index and scroll parity (`ROW_Z`), so while a row stays they pair with its own,
  and a scroll shows them at once in their new place, as the letters. Checked on every frame of the
  keyboard path (`RECVX_INTERP_T` in-between images): only the selection highlight moves (as in the
  lobby), and a slider's fill when its value changes.
- `MAX_ROWS` is 30 (ROW_Z's room; ITEMS has 27).
- **Randomize Played as**: ini `biorand_appear`, written only when 1 (like `biorand_seed`, so a game
  without `--dev` writes nothing new). It has no settings table row (the --dev screen only), so it is
  read and written by name in `config.c`, as `biorand_seed`. S5 uses it.

**Test aids** (job dir `biorand-ui/s4b/`, not committed): `run.sh` (a `--dev` run with a dummy CLI);
`inputs.py` (keyboard, gamepad and mouse runs); `paste.sh`; `expect` (`expect.c` with
`biorand_opts.c`: the string after given sets, or `-d` a pasted string); `cli_check.sh`; `interp.py`
(in-between images); `shots_run.sh` and `shots.py`. New in the game: `RECVX_CLIPBOARD_TEST="text"`
puts text on the clipboard at the start (`lobby_io.c`).

**Tests (2026-10-26, Linux):**

- Scripted keyboard, gamepad and mouse runs (`inputs.py`), each: the seed (typed 12345 then +1; the
  pad's digit editing to 3 then +1; the mouse's `>` twice), doors on with 3 segments, gunpowder 13
  and ammo 18 (the mouse on the tracks), Linear Launcher off at the bottom of ITEMS (scrolled by
  Down x18, the pad's Up wrapping, the mouse wheel x18), Bat None, Randomize Played as on, the cast
  on, back to the title. All three ini `biorand_seed` equal `expect`'s strings, with
  `biorand_appear = 1` and `cast_random = 1`.
- Copy and paste (`paste.sh`): a 9-character clipboard is refused with "The seed string has 9
  characters; it needs 68."; COPY logs the shown string; PASTE takes it back. Golden string 2 in lower
  case with spaces is pasted; the ini holds its decoding with the rules.
- The real nightly CLI (`cli_check.sh`) generated the keyboard run's string (doors on) and the
  pasted one: exit 0, the string echoed and logged in `log_pl0.txt`, no warnings, 205 rooms.
- An ini with `biorand_appear = 1` and `cast_random = 1`: the PORT EXTRAS page shows both on and the
  cast's sub-rows; the ini keeps them.
- 1280x720: the screen fits in the 4:3 part.
- `layout_check.sh` (the 1e7013c build against S4b, with the ISO): vanilla 156 and enhanced 311
  frames identical.
- ctest: all pass. The Linux and mingw (`recvx.exe`) builds pass.
- Screenshots: `scratch/biorand-s4b/` and the job dir's `s4b/shots/` (each page, the bars at default
  and changed ratios, Bat None, locked and greyed rows, the seed being typed, GENERATE refused, the
  paste error, the copied string, pasted strings).

**For S5:**

- GENERATE: `activate()` `case K_GEN` (now a refusal) starts the job, and `row_why()` `case K_GEN`
  (now "Generating comes in a later version.") returns NULL, or why not now (no ISO path, a co-op
  session, `biorand_locked`). Its help text is in `help_text()`.
- The work state: `BS_WORK` with a job kind next to `bm.install` (`work()` starts it, `work_end()`
  takes its final state, `draw_work()` draws the stage line and the bar: add the CLI's `Progress:`
  percent and message). CANCEL already goes through `choose()`; add the CLI's kill there. `BS_RESULT`
  shows the outcome ("Seed N generated", or the CLI's `Error:` line).
- The string to run is `bm.str` (equal to ini `biorand_seed`). "Randomize Played as" is
  `g_biorand_appear`.

### S5: Generate

S5a (done) is the generation and "Randomize Played as"; S5b (done) is co-op (the host sends its seed string, the
partner generates with one click).

#### S5a: GENERATE and "Randomize Played as" (done)

Files: `pc/src/net/biorand_dl.c/.h` (the CLI run, on the downloader's thread), `pc/src/host/biorand.c/.h` (paths,
settings.json, BioRand on), `pc/glue/biorand_menu.c` (the row, the question, the progress, the result),
`pc/src/host/cast.c/.h` (the pick), `pc/glue/biorand_glue.c` (the save word), `pc/glue/coop_glue.c` and
`pc/src/host/settings.c` (the look used), ctests `biorand_dl` and `cast`. No decomp change.

**GENERATE** (`K_GEN` on the SEED page). `row_why` greys it, with the reason in the help strip, when:

- the CLI is not installed ("The BioRand CLI is not installed.");
- `g_config.iso_path` is empty ("The game's disc image is not known.");
- a game runs (`pc_in_game`: "Only at the title menu.");
- we are a co-op guest (`coop_biorand() >= 0`: "Not as a guest in a co-op session."; S5b lifts it for the host's
  seed only);
- the mods folder has less than `BRDL_GEN_FREE_MB` (4 GB) free: "Needs about 4 GB free in the mods folder; it has
  1.2 GB." The free space is looked at when the screen opens, after each job and on GENERATE's OK
  (`biorand_mods_free_mb`: statvfs / GetDiskFreeSpaceExW; the mods folder is created first).

**Why 4 GB, not 1.5:** measured in S5a, the CLI's scratch (`<temp>/biorand-cli/<guid>/`) peaks at **2.4 GB** while the
new pack (1.4 GB) is being written next to it. The old pack's space does not count (the free space already excludes
it), so 3.8 GB must be free. The scratch goes into `mods/biorand-temp/` (below), so one check covers both, and a RAM
`/tmp` (tmpfs) no longer adds 2.4 GB to the 6 GB memory peak.

OK on the row asks (`A_GEN`, No first): "Generate BioRand seed N into mods/biorand? It replaces the pack there.
BioRand may use several GB of memory (about 6 GB) for a short time. Free space: 261.9 GB."

Yes: `biorand_generate(bm.str)` (biorand.c):

1. writes `<save_dir>/biorand/settings.json` (absolute), every run:
   `{ "lastSelectedGame": 3, "generateCvPcMod": true, "disableCustomContent": true }`;
2. makes the ISO path (`g_config.iso_path`) and the mods folder (`RECVX_MODS`, else `mods`) absolute from the
   current directory;
3. calls `biorand_cli_generate(seed, iso, mods, cfg)` (biorand_dl.c): one job at a time with the download
   (the same `busy` flag); 0 when one runs ("The last download is still ending...").

The thread (`gen()`):

- deletes leftovers first: `mods/biorand.tmp-*`, `mods/biorand.bak-*` (the CLI's, from a killed run) and
  `mods/biorand-temp/` (ours). `rm_tree` never follows a link. Nothing else in mods is touched.
- runs `[cli, -s, <string>, -i, <abs iso>, -o, <abs mods>/biorand, -f]` with `SDL_CreateProcessWithProperties`:
  no shell, stdout piped (`SDL_PROCESS_STDIO_APP`) with stderr into it, stdin null. The environment is ours plus
  `BIORAND_CONFIG=<cfg>` and `TMPDIR`, `TMP`, `TEMP` = `mods/biorand-temp` (.NET's `Path.GetTempPath`: TMPDIR on
  Linux, TMP/TEMP on Windows). SDL's background flag only on Windows (CREATE_NO_WINDOW); on POSIX it double-forks.
- reads the pipe (non-blocking on both systems) into lines: `brdl_line()` (biorand_dl.h, inline, ctest
  `biorand_dl`) gives `'P'` "Progress: N% text" (percent and message for the bar), `'E'` "Error: text" and `'W'`
  "Warning: text" (the last of each kept), 0 the rest (Info: lines, logged).
- exit code: `SDL_WaitProcess`. **Windows:** SDL reports 0 for every background process, so `exit_code()` asks the
  system (`OpenProcess` by the pid while SDL's handle keeps the process object, `GetExitCodeProcess`). S2's `run()`
  (curl, wget, tar) uses it too: on Windows it got 0 for a failed tar before.
- the result: 0 `BRDL_GENERATED`; 2 the CLI's `Error:` line ("CVX ISO not found at '...'. Use -i to point at
  recvx.iso."); 3 "BioRand failed: <Error line>" ("File is not a valid UDF ISO"); an error mentioning the seed version
  gets " Update the game or use an older BioRand CLI." (risk 2); other codes "BioRand ended with exit code N".
- **CANCEL** (`choose()` in `BS_WORK` -> `biorand_cli_cancel`): the thread kills the CLI (`SDL_KillProcess`, forced),
  waits for it, and deletes the leftovers as above; `BRDL_CANCELLED`. A soft reset (`pc_biorand_menu_close`) cancels
  too. Quitting the game does not wait (the thread is detached): the CLI then runs to its end on its own
  (ponytail: no kill at exit; the next run deletes its leftovers).

New states: `BRDL_GENERATE` (busy, before `BRDL_INSTALLED`; the numbers of the later states moved by one) and
`BRDL_GENERATED` (final, last). `BrdlStatus` has `pct`, `msg` and `warn` (a spinlock guards the strings).

The screen: `BS_WORK` with `bm.gen` draws "BIORAND - GENERATING SEED N", the message (a "[P1] " prefix cut), the bar
and the percent, CANCEL. The end (`gen_end`) always rescans (`biorand_scan`); success: `biorand_enable()`
(`g_config.biorand = 1`, saved) and "BioRand seed N generated. BIORAND is on: start a NEW GAME." (plus "BioRand
warned: ..." if a Warning: came); an error: "BIORAND - ERROR" and the text; cancelled: back to the page with
"Generating cancelled.". Going straight to NEW GAME was left out: OK goes back to the page, cancel to the title.
The save-seed warning (`pc_biorand_save_differs`) is unchanged: it compares the save's words with the rescanned pack.

Test aid: `RECVX_BIORAND_ISO_TEST=<path>` gives the CLI another image (the game itself needs the real one).

**"Randomize Played as"** (`g_biorand_appear`):

- The pick: `cast_appear_pick(seed, imports)` (cast.c, pure): a hash of BioRand's seed number indexes
  `cast_pool(1, imports)`, the cutscene cast's "All models" pool (humanoid catalogue looks and player files, never
  Claire's or Chris's own looks; the zombies one entry, then `cast_zombie_look(seed, 0, 7)` picks the zombie). Imports
  count only with the cast's "Include imported models" (`g_enemy_opts.cast_imports`). The result is a "You appear as"
  chr (player file n: 1 + n; catalogue i: `appear_canon(6 + i)`; import k: `COOP_CHR_IMPORT + k`).
- When: `pc_quick_start` (a new game, coop_glue.c) calls `pc_biorand_appear_new()` after `pc_biorand_stamp()`. It
  picks when "Randomize Played as" is on, BioRand is active, the pack's seed is known and we are not a co-op guest.
  Seed 123456 gives "Zombie: base staff 3", 654321 "Wesker".
- **Storage: save word `sys->itm[290]` (`BR_APPEAR`)** = `0x41000000 | chr` ('A'), else 0. It is next to BioRand's
  words 288/289 in the unused part of the item region (coop_glue.c's map updated: 290 BioRand's pick, 291..319
  unused). The new game writes it (0 when nothing was picked). It is part of the save region, so every save of that
  game holds it, and **loading a save restores it** (the region is loaded whole; checked: the save file holds
  0x4100003B at the word after BioRand's 0x4201E240). recvx.ini's `coop_char` (the user's choice) is never written.
- Use: `pc_biorand_appear()` returns the chr while in game, BioRand is active, we are not a co-op guest and the word
  is set (an import no longer listed: -1). coop_glue.c `my_chr()` (our look, the cutscene doubles, the voice, what the
  partner is sent) takes it before `g_coop.chr`; so does the `appear_on` test. The F1 "You appear as" row shows it;
  choosing a look there drops this game's pick (`pc_biorand_appear_drop`) and becomes the user's choice as before.
- Co-op: the host's pick is its look, and the partner sees it (as any "You appear as"). A guest's own choice stays
  its own: a guest neither picks nor reads the word (it runs on the host's state, words and all). Ponytail: a guest's
  own saves taken in a session may carry the host's word; it applies only if such a save is later loaded solo.

**Tests (2026-10-26, Linux; job dir `biorand-ui/s5a/`, not committed):**

- `scen.sh gen` (keyboard: the title's BIORAND, Down x5 to GENERATE, OK, Yes) with the real nightly CLI into a test
  folder (`RECVX_MODS`, `RECVX_BIORAND_CLI_DIR_TEST`): the bar went 10, 35, 88, 96 %, then "BioRand seed 123456
  generated". 205 room files; `log_pl0.txt`'s `Seed:` line equals the menu's string; ini `biorand = 1`;
  settings.json as above (the CLI added its fields, `seedCv` the string); nothing but `biorand/` left in mods.
- `scen.sh cancel`: CANCEL at 35 %: `biorand.tmp-<guid>` deleted, `biorand-temp/` gone, no CLI process left, the
  page says "Generating cancelled.", BioRand stays off.
- `scen.sh badiso` (`RECVX_BIORAND_ISO_TEST`): a missing file: exit 2, the result shows the CLI's "CVX ISO not found
  at ..."; a 3 MB file of zeros: exit 3, "BioRand failed: File is not a valid UDF ISO".
- NEW GAME after generating (`played.sh`, item trace): BioRand on, the pack's files served, and the inventory after
  0/0's entry is the pack's (seed 123456: Linear Launcher 5, Combat Knife, Lighter, Magnum 6, Magnum Bullets 4, Red
  Herb, Ink Ribbon, as its log's "Randomizing Inventory 0"; seed 654321 its own list).
- "Randomize Played as": seed 123456 twice gave chr 59 "Zombie: base staff 3" both times; seed 654321 (a second
  pack) "Wesker"; off: Claire. `look.sh` shows the three in 0/2. ini `coop_char` stayed 0.
- ctest `biorand_dl`: `brdl_line` (progress, bounds, error, warning, other lines); ctest `cast`: the pick is
  deterministic, a listed look, never Claire's or Chris's, 40 looks over 2000 seeds, the two seeds differ.
- `layout_check.sh` (the 5ef0ae6 build against S5a, with the ISO): see STATUS. ctest: all pass. Linux and mingw
  (`recvx.exe`, `test_biorand_dl.exe`) build.
- Screenshots: job dir `biorand-ui/s5a/shots/` (the question, the progress at 10/35/88/96 %, generated, the page,
  cancel, both errors, the three looks).
- Not tested: the 4 GB refusal (no small disk here; the reason text is in `row_why`), Windows (as S2), the in-game
  load of the save (the load screen did not take the scripted keys; the word in the save file was checked instead).

#### S5b: co-op, the host's seed (done)

Files: `pc/src/net/coop_proto.c/.h` (`EV_BRSEED`, protocol 30), `pc/glue/coop_glue.c` (send, receive, the offer and
the hold), `pc/glue/biorand_menu.c` (the host's seed: `bm.host`), `pc/glue/lobby_glue.c` (opening the screen),
`pc/src/host/biorand.c/.h` (`biorand_seed_string`), ctest `coop`, docs/COOP.md ("BioRand", "Protocol 30",
"Events"). No decomp change.

**Transport: one reliable event, protocol 30.** `EV_BRSEED` (host only, 80-byte payload): `str[68]`, the pack's
whole seed string, and `dig[12]`, the first 12 hex digits of the host's CLI archive's SHA-256 (ini
`biorand_cli_digest`; 48 bits are plenty to tell two builds apart). All zeros mean unknown. The pose and `PoseExt`
are unchanged (nothing per frame).

- The string is the pack's own: `biorand_scan` keeps `log_pl0.txt`'s `Seed:` value when it is 68 characters and
  `biorand_seed_decode` reads it (`biorand_seed_string()`, "" otherwise). Not the menu's ini string, which may
  differ from the pack (BioRand's GUI, an edit after generating).
- The host: `brseed_sync()` (from `enemy_sync`, as `EV_EOPTS`) queues it while `biorand_active()`, to each new
  partner session and whenever the string or the digest changes. It goes out in the same frame as the
  extension's `br`; a datagram's events are applied before its extension, so the guest has it first.
- Checked on receipt: `coop_check_event` (from the host only; each byte in BioRand's alphabet or `-`, upper
  case, `R` at 0, dashes at 4 and 9; the digest lower-case hex; or all zero), then `apply_event` decodes the
  string with `biorand_seed_decode` (length, version 8, game, alphabet); one it cannot read counts as unknown.
- A build of protocol 29 is refused at the first datagram, both ways: "127.0.0.1:47617 runs co-op protocol 30,
  we 29" (the existing version check).

**The guest** (`biorand_from_host`, every extension): while the host's `br` is 1 and our pack id differs:

1. no seed string yet: wait up to 2 s for `EV_BRSEED` (`br_wait`), then leave as before;
2. a seed string, `--dev` (`biorand_shown`), not in a game: at the title, `br_offer = BR_ASK`; while starting up
   or in the attract demo, wait for the title;
3. otherwise (in a game, no string, without `--dev`, or `BR_DONE`): leave with the old messages.

`BR_ASK` and `BR_GEN` **hold the join**: the guest asks for no game state (`PF_WANT_BLOB` off) and drops a state
that comes meanwhile (`pop_pending = 0`); afterwards it is at the title with `need_blob` and asks again, so it
loads the host's current state, not one from before the generation. The host may start its game meanwhile.

**The screen.** `pc_lobby` (the lobby, mode 30) and `pc_title_mouse` (the title menu at level 0: an F3 join,
`--coop-join`) open the BIORAND screen while `coop_biorand_offer()` returns the host's string. From the lobby,
leaving it returns to the lobby (`pc_lobby` returns 0 while `lb.open`); from the title menu, to the title menu.
On open, `bm.host` = 1, `bm.str` = the host's string as it came, `bm.o` its decode (for the seed number); recvx.ini's
`biorand_seed` is neither read into it nor written. Then:

| step | shown | Yes | No / cancel |
|---|---|---|---|
| `A_HOST` | "The host plays BioRand seed N. Generate it now into mods/biorand? It replaces the pack there. BioRand may use several GB of memory (about 6 GB) for a short time. Free space: X GB." | `host_next()` | leave |
| `A_DOWNLOAD` (no CLI) | S3's download question | install (S3's progress), then `host_next()` | leave |
| `A_DIGEST` (once; the host's digest known and not ours) | "Your BioRand CLI is not the host's build, so your pack may differ from the host's. Download the newest BioRand CLI first? (The host should have the newest too.) No generates with yours." | install the newest, then `host_next()` | `host_next()`: generate with ours |
| GENERATE's checks (`row_why`, the "co-op guest" refusal lifted for `bm.host` only) | the reason as an error (ISO unknown, < 4 GB free) | | OK leaves |
| `BS_WORK` | S5a's generation progress | | CANCEL: leave |

- `gen_start` tells coop `coop_biorand_reply(1)` (`BR_GEN`); success or cancel leaves the screen at once (no OK);
  an error shows S5a's error box, OK leaves. `biorand_enable()` is not called: our own BioRand setting stays
  (the session follows the host's anyway).
- Leaving (`pc_biorand_menu_close`) replies 2 (generated: `BR_DONE`, the pack compared again; the same id joins,
  another one leaves with "The host plays BioRand seed N with other options or another BioRand version; generate
  the same ...") or 0 (`BR_DONE` at once: the next extension leaves with the old message).
- The screen also leaves when the offer goes (the session ended, the host turned BioRand off, the pack matches).

**Keeping the session** (decided: keepalive, no reconnect): the CLI and the download run on the downloader's
thread, and the game's frame loop goes on at the title, so `coop_frame` sends the usual poses every frame on
both sides. Nothing times out (3 s "not responding", 15 s lost). A reconnect would need the join address again
and a new session id; staying is simpler. Ceiling: a machine that swaps for more than 15 s under the CLI's 6 GB
(risk 1) loses the session; the pack is then there for the next join.

**The host's lobby** shows the guest's note (`PoseExt.note`, under its model): "BioRand: asked to generate" while
asked (and downloading), "BioRand: generating..." while the CLI runs (coop_glue.c fills it from `br_offer`).

**Tests (2026-10-26, Linux; job dir `biorand-ui/s5b/`, not committed).** `co.sh` runs a host (pack A, seed 123456,
id ed803757, BioRand on, `--coop-host`) and a guest (`--coop-join`, or the lobby), both `--dev`, ports 476xx;
`scen.sh` has the cases; the host teleports into 0/6 so it gets control.

- `nopack` (no pack): the prompt, YES, the real nightly CLI generates seed 123456 (id ed803757 = the host's),
  "compared again", "joined the host's game (stage 0 room 6)", the pack's files served (RM_0060, SYSTEM.AFS/10),
  then the follow to 4/4 with RM_4040 from the pack.
- `packb` (pack B, seed 654321, id 7281350a): the same; pack A replaced B, joined.
- `nocli` (no CLI, a fake release over `file://` at 3 MB/s): YES, the download question, YES, the download, then
  the generation without another question, joined.
- `no`: NO: "session ended: The host plays BioRand seed 123456; generate the same seed (or ask ...)", the host
  "partner left: its BioRand pack is not ours"; nothing written to mods.
- `digest` (the guest's ini digest is another): YES, the warning, NO: generated with its CLI, joined. `digesty`:
  YES on the warning: the fake newest CLI installed (ini digest now the nightly's), generated, joined.
- `differs` (the host's `log_pl0.txt` changed: id 44c8afab): generated, compared, left with "... with other
  options or another BioRand version ...".
- `oldguest` / `oldhost` (the S5a build, protocol 29, on either side): refused, "runs co-op protocol 30, we 29"
  (and the reverse); no BioRand screen.
- `lobby`: both in the lobby (address typed, JOIN), no CLI: the screen over the lobby, download, generation, back
  in the lobby connected; the host's lobby showed "BioRand: asked to generate", then "BioRand: generating...";
  the host started the game, the guest joined from the lobby. `lobbyno`, `lobbydiff`: the lobby's status line
  shows the two messages.
- ctest `coop`: `EV_BRSEED` from the host passes, from the guest refused; a bad letter, lower case, short, no `R`,
  dashes, an upper-case or short digest refused; an unknown string or digest (zeros) passes.
- `layout_check.sh` (the S5a build against S5b, with the ISO): vanilla 156 and enhanced 311 frames identical.
  ctest: all pass. Linux and mingw (`recvx.exe`) build.
- Screenshots: job dir `biorand-ui/s5b/shots/` (the prompt, the download question and progress, the generation,
  back in the lobby, the host's lobby notes, both in game, the digest warning, the NO and still-differs
  messages, `sheet.png`). The generated packs and the frame dumps are deleted.
- Not tested: Windows (as S2), a machine that swaps during the generation.
- "Randomize Played as" in co-op stays as S5a has it (the host's pick is its own look).

### S6: review, `--dev` gating, docs, tests (done)

The player-facing usage is in docs/modding/README.md "Randomizer (BioRand)". USER_GUIDE.md and the
CHANGELOG are untouched: BioRand is held back.

**Review of the whole branch (`git diff main...biorand-ui`).** Fixed:

- **tar.exe and the game folder's name** (`extract`): tar now runs with its working folder set to
  `biorand-dl/` (`SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING`), with the relative, ASCII-only
  arguments `-xf biorand-cli-win-x64.zip -C x`. Windows' bsdtar reads its command line in the ANSI code page. A
  game folder with other characters, such as `C:\Spiele\Ünïcode` on a system with another code page,
  would not have reached it intact. A long path is shorter this way too.
- **Defender and a fresh exe** (`move_file`): installing `biorand-cli.exe` and the LICENSE retries a
  failed rename for up to 5 s on Windows (20 x 250 ms, stopped by CANCEL). A virus scanner may still
  hold the file that tar has just written.
- **The exit code by PID** (`exit_code`): when Windows cannot give it (OpenProcess or
  GetExitCodeProcess fails), it is now -4 with a log line. Before, SDL's 0 for background processes
  stood, so a failure counted as success.
- **A data race** (`biorand_cli_exe`): the path was rewritten into a static buffer on each call, from both the UI
  thread and the worker. It is now set once (the screen's first look, before any job), and the worker
  only reads it.
- **Main's behaviour kept** (coop_glue.c `appear_on`, single player): the S5a change tested `my_chr()`
  instead of `g_coop.chr`. With "As played (cutscene)" and a `ply_id` outside 0..3 that is 0, so
  `self_frame` would have stopped. It is now `g_coop.chr || pc_biorand_appear() > 0`, which is main's
  test plus BioRand's pick.
- Test: ctest `biorand_dl_install` now installs into a game folder named `My Game ü` (a space and a
  non-ASCII letter). tar runs in it.

Checked and left as they are:

- **WinHTTP**:
  - the automatic proxy (8.1+, else the default);
  - TLS 1.2 and 1.3, falling back to 1.2;
  - redirects (never https to http);
  - the API's error body is read for its message, and other HTTP errors are refused;
  - the size is capped while reading;
  - every handle is closed on every path.
  
  CANCEL waits for the current read at most (the 60 s receive timeout).
- **tar.exe from the 32-bit game**: System32 is redirected to SysWOW64, which also holds a
  tar.exe; Sysnative is the 64-bit one. `GetPathInfo` and `CreateProcess` see the same redirection.
- **Spaces and backslashes in argv**:
  - SDL's `join_arguments` quotes an argument with spaces, and doubles the backslashes before a
    quote or before the closing quote. The ISO path `...\SLUS-20184 ROM.iso`, the output and
    `BIORAND_CONFIG` reach the CLI intact (.NET splits the wide command line by the same rules).
  - Mixed separators (`C:\Games\recvx\mods/biorand`) are fine for .NET and for SDL's file
    functions.
  - The game's own paths are UTF-8 (`pc_utf8_argv`, SDL), and SDL converts them to UTF-16 for
    every Windows call and for the environment block.
- **Long paths**: our buffers hold 1024 or more characters. The game has no `longPathAware` manifest,
  so on Windows the whole game, not only BioRand, needs its folder below MAX_PATH. The deepest
  BioRand path is `mods\biorand\SYSTEM.AFS\10`. The CLI (.NET) handles long paths itself.
- **Threads**:
  - one job at a time (`busy`, compare-and-swap);
  - the job's strings are copied before the thread starts;
  - `st_msg`, `st_warn` and the CLI's `Error:` line are under the spinlock;
  - the paste request is handed over between the game and the main thread with acquire/release;
  - a soft reset cancels the job;
  - quitting the game leaves the CLI running (documented in S5a).
- **Leaks**:
  - every `SDL_Process`, environment, property set, glob list, loaded file and IO stream is freed
    on every return;
  - `biorand-dl/` is deleted after every job;
  - the CLI's leftovers are deleted before and after each run.

**`--dev` gating (checked 2026-10-26, Linux).** Without `--dev`:

- **No BIORAND title row.** `pc_title_menu_init` adds it only with `biorand_shown()`. The main
  build and the S6 build ran the same keys: the title, then MULTIPLAYER (the lobby). All 64 dumps
  (frames 100..400 and their in-between images) are byte-identical. The ini held every BioRand key,
  the CLI folder held a CLI, and the mods folder held a pack. Screenshot: job dir
  `biorand-ui/s6/nodev_title_lobby.png`.
- **No BioRand settings.**
  - The settings table's BioRand row has `.shown = biorand_shown`.
  - The new keys (`biorand_seed`, `biorand_appear`, `biorand_cli_digest`) have no rows. They are
    read and kept, and written only once the `--dev` screen has set them. In the run above they came
    back unchanged.
  - NEW GAME's BIORAND row exists only with a pack (`CursorMax[1]`, `biorand_pack()`), and
    `biorand_scan` finds none without `--dev`.
  - `biorand_scan` reads no pack, so `biorand_active()` is 0, and so are Randomize Played as and
    `EV_BRSEED`.
- **No co-op offer.** `biorand_from_host` offers only with `biorand_shown()`. A host (`--dev`, pack
  seed 123457) and a guest without `--dev` ran together. The guest left with the old message ("The
  host plays BioRand seed 123457; generate the same seed ...") and the host logged "partner left:
  its BioRand pack is not ours". No BIORAND screen opened, and nothing was written to the guest's
  mods.
- **No network access.**
  - The only callers of `biorand_cli_check_update`, `biorand_cli_install` and `biorand_generate` are
    the BIORAND screen's (`work`, `gen_start`). That screen is reached only from the title row or the
    co-op offer, and both are gated.
  - On Linux, BioRand's network is curl or wget, both child processes. In the run above, shims of
    curl, wget, tar and the CLI recorded no start.
  - The game's own update check is unrelated (`RECVX_UPDATE_CHECK`).
- **Protocol 30** applies with or without `--dev`. Co-op with a build of protocol 29 is refused, as
  with every protocol bump.

**End to end (2026-10-26, Linux, headless; job dir `biorand-ui/s6/`: `e2e.sh`, `co.sh`, `gate.sh`):**

1. `e2e.sh menu`, `--dev`, an empty CLI folder (`RECVX_BIORAND_CLI_DIR_TEST`) and the real GitHub
   API:
   - the title's BIORAND opened the download question; YES downloaded 7 719 279 bytes
     (`sha256:8465e828…`). The folder held only `biorand-cli` (ELF x86-64, 0755) and
     `biorand-cli.LICENSE`;
   - the page: DOORS, Randomize Doors on; SEED, Seed +1 (123456 to 123457);
   - GENERATE, YES: exit 0, 205 room files, and `log_pl0.txt`'s `Seed:` equals the menu's string
     `R840-1KR3-V4HH…` (the `expect` tool gives the same for Seed=123457 with doors on). Then
     "BioRand on", and ini `biorand = 1`, `biorand_seed` and `biorand_cli_digest` were set.
2. `e2e.sh newgame`: NEW GAME with that pack. The inventory after 0/0's entry was Handgun 18,
   M-100P 0, Combat Knife, Lighter, Handgun Bullets 21, F. Aid Spray, Ink Ribbon 2. That is the log's
   "Randomizing Inventory 0" (HandgunGlock17 x18, M1P x0, CombatKnife, Lighter, HandgunBullets x21,
   FAidSpray, InkRibbon x2).
3. `co.sh coop`: that pack's host and a guest with no pack (the downloaded CLI). The guest got
   `EV_BRSEED` and the offer, and YES generated the same pack (id b4f4e7a3 on both). It then logged
   "compared again", "joined the host's game (stage 0 room 6)".
4. The packs and frame dumps were deleted afterwards.

ctest: all pass. The Linux and mingw builds pass (`recvx.exe`, `test_biorand_dl.exe`).
`layout_check.sh` (a main build, 8d0dc30, against S6, with the ISO): vanilla 156 and enhanced 311
frames identical.

**Merge with main:** main was still 8d0dc30, the branch's base, so there was nothing to merge.

#### Windows real-machine test list (open)

Nothing of BioRand has run on Windows yet (no machine or wine here). Use a release-like
`recvx.exe` (mingw RelWithDebInfo), started with `--dev --log`, so `recvx.log` is written in the
folder it is started from.

Setup:

- [ ] 64-bit Windows 10 1803+ or 11, with Defender on (the default).
- [ ] Put the game into a folder whose name has a space and a non-ASCII letter, for example
      `C:\Spiele\RE CVX Tést\`. Its ISO path should have a space too (for example
      `D:\ISOs\SLUS-20184 ROM.iso`).

Download and update:

- [ ] Start the game without `--dev`. Expect no BIORAND row, and no `biorand-cli*` files appearing.
- [ ] Start it with `--dev`. Expect BIORAND to be the last title row, after MULTIPLAYER.
- [ ] Open BIORAND and answer YES to the download question. Expect:
  - the bar and "x.x / 7.1 MB", then "The BioRand CLI is installed.";
  - no console window flashing (tar.exe);
  - the game folder holding `biorand-cli.exe` and `biorand-cli.LICENSE`, and no `biorand-dl\`;
  - log lines "biorand-cli: nightly biorand-cli-win-x64.zip ...", "installed ...", and no
    "tar.exe was not found" or "cannot move".
- [ ] CHECK FOR A NEW BIORAND CLI. Expect "You have the newest BioRand CLI."
- [ ] Quit, change one hex digit of `biorand_cli_digest` in recvx.ini, then CHECK again. Expect
      "A newer BioRand CLI is available"; YES replaces the exe, which tests replacing it while
      Defender scans it. Expect "installed", or a "cannot move" line in the log.
- [ ] Unplug the network and CHECK. Expect "no connection to GitHub (WinHTTP error 12007)" or similar,
      in orange.
- [ ] CANCEL during a download. Expect "cancelled", and no `biorand-dl\` left.
- [ ] Optional: behind a system proxy (Settings > Network > Proxy), the download still works
      (WinHTTP's automatic proxy).

Generate:

- [ ] GENERATE (SEED page). Expect:
  - the question to show the free space;
  - the bar to go to 100 % with BioRand's messages, with no console window;
  - "BioRand seed N generated";
  - `mods\biorand\` to hold 205 `RM_*.RDX`, and its `log_pl0.txt` `Seed:` line to equal the
    string on the SEED page;
  - no `mods\biorand-temp\`, `biorand.tmp-*` or `biorand.bak-*` left;
  - `<save folder>\biorand\settings.json` to be written, and `%APPDATA%\biorand` to be untouched.
- [ ] The exit code read by PID:
  - Start the game with `RECVX_BIORAND_ISO_TEST=C:\nope.iso` and GENERATE. Expect "CVX ISO not found
    at ..." (exit 2).
  - With a 3 MB file of zeros as the test image, expect "BioRand failed: File is not a valid UDF
    ISO" (exit 3).
  - The log must not show "the exit code is unknown".
- [ ] CANCEL during a generation. Expect "Generating cancelled.", no `biorand-cli.exe` in Task
      Manager, and the leftovers deleted.
- [ ] NEW GAME. Expect the start inventory to equal `log_pl0.txt`'s "Randomizing Inventory 0" (with
      Randomize Played as on, a look picked from the seed).
- [ ] Optional: the same from a game folder with Japanese or Cyrillic letters (the ANSI code page
      case of tar.exe).

Co-op and the rest:

- [ ] Two machines (or Windows and Linux), both `--dev`: the host has a pack, and the guest has none.
      Expect the guest to be asked at the title or in the lobby, to generate, and to join. The host's
      lobby shows "BioRand: generating...".
- [ ] `test_biorand_dl.exe --live C:\Temp\brtest`: the downloader on its own. Expect "check newer",
      "installed", "current".
- [ ] Optional, an 8 GB machine: the game stays responsive while the CLI runs (about 6 GB), and a
      co-op session survives it.

Report any failure with `recvx.log` (the `biorand-cli:` and `biorand menu:` lines).

## Risks and open questions

1. **Memory: the CLI peaked at 6.0 GB RSS** for one CVX pack. With the game running, 8 GB
   machines may swap or fail. Ask IntelOrca whether it can be lowered: it reads the whole ISO
   and keeps every room. S5a: GENERATE's question says so. Its 2.4 GB scratch goes to
   `mods/biorand-temp/`, not a RAM `/tmp`.
2. **Nightly drift.** The nightly is rolling and unversioned. A new seed-string version gives
   exit 2 ("Seed version N is not supported"), and a changed layout makes our encoder wrong.
   - The S5 error should then say "update the game or use an older CLI".
   - Should we pin a tagged release instead of `nightly` once IntelOrca's port changes are in a
     release?
3. **Co-op**: partners need the same pack, which takes the same seed string **and** the same CLI
   build. Different nightlies may differ; the pack id already detects that. Decided: the host sends
   its seed string, and the partner can generate the pack (S5). Done in S5b, with the CLI digest
   compared and the newest CLI offered.
4. **Windows untested here**: tar.exe in SysWOW64 (or Sysnative), WinHTTP redirects, Defender
   scanning a freshly written exe (S6 retries the rename), a game folder outside the ANSI code page
   (S6 runs tar with relative arguments). Downloads made with WinHTTP carry no Mark-of-the-Web, so
   there is no SmartScreen prompt. The test list is in "S6".
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
- **S4a done** (2026-10-26): the option model and the seed-string codec `pc/src/host/biorand_opts.c/.h`
  (row table, encoder, decoder, rules, distributions, the doors estimate), the ini key
  `biorand_seed`, ctest `biorand_opts`, and a cross-check with the real CLI (see "S4a"). The JSON
  catalogue is deleted.
- **S4b done** (2026-10-26): the generator pages in `BS_MENU` of `biorand_menu.c`: SEED (seed, random,
  copy, paste, GENERATE greyed, the CLI check), DOORS, ITEMS and ENEMIES with the estimate and the
  two distribution bars, PORT EXTRAS (Randomize Played as, ini `biorand_appear`; the cutscene cast).
  Every change saves the seed string. Tested by keyboard, gamepad and mouse, with copy and paste and
  the real CLI (see "S4b").
- **S5a done** (2026-10-26): GENERATE runs the CLI from the menu (preconditions, the memory note, the progress bar from
  its `Progress:` lines, CANCEL with the leftovers deleted, the CLI's errors, BioRand on afterwards) and "Randomize
  Played as" picks a look per seed for each new game (save word 290). Tested with the real CLI (see "S5a").
- **S5b done** (2026-10-26): co-op seed sharing. The host sends its pack's seed string and CLI digest (`EV_BRSEED`,
  protocol 30); a guest whose pack differs is asked at the title (lobby or title menu) to generate it, with the CLI
  download and the digest warning on the way, and joins once the pack id matches; the session stays meanwhile and the
  host's lobby shows the guest's state. Tested with two instances and the real CLI (see "S5b").
- **S6 done** (2026-10-26): the review of the whole branch, with five fixes (tar's arguments, the Defender retry, an
  unknown exit code, a data race, main's `appear_on`); the `--dev` gating checked (title and lobby byte-identical to
  main, no settings, no co-op offer, no network); the player's docs in docs/modding/README.md; a live end-to-end run
  (download, options, generate, NEW GAME's inventory, a co-op join with the seed share); main merged (nothing new).
  `layout_check.sh` (a main build, 8d0dc30, against S6, with the ISO): vanilla 156 and enhanced 311 frames identical.
  ctest: all pass; Linux and mingw build. See "S6".
- **STATUS: All steps done; open: Windows real-machine test list** ("S6", "Windows real-machine test list").
- The job-dir tools (not committed): S5b's `s5b/` (`co.sh`, `scen.sh`, `mkpack.sh`, `build.sh`); S5a's `s5a/` (`run.sh`, `scen.sh`, `played.sh`, `look.sh`, `saveload.sh`); S1's `biorand-ui/enc.py` (it read the deleted JSON; the C
  encoder replaces it), `gen.sh`, `err.sh`; S4a's `s4a/cli_check.sh`; S4b's `s4b/` scripts (see "S4b").
