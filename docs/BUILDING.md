# Building

The game is built as a **32-bit (i686) executable**: the game's data and the decompiled code
assume the PS2's 32-bit pointers (see [ARCHITECTURE.md](ARCHITECTURE.md#data-model-and-memory)).
CMake refuses a 64-bit configuration.

Only GCC is supported (Linux GCC with multilib, mingw-w64 GCC for Windows). Clang and MSVC lay
out the decomp's data differently (32-byte array alignment, bitfields) and misrender.

## Getting the sources

```sh
git clone --recursive https://github.com/aikatore/recvx-enhanced-port.git
cd recvx-enhanced-port
# or, in an existing clone:
git submodule update --init --recursive
```

`decomp/` is the recvx-decomp fork (branch `pc-port`), which pulls the Katana and CRI header
submodules. The decomp's large Metrowerks compiler submodule (`recvx-decomp-mwcc`) is marked
`update = none` and is skipped; the PC build does not need it.

CMake downloads SDL 3, Dear ImGui, miniupnpc, libnatpmp and Monocypher at configure time
(FetchContent), so the first configure needs network access. Versions and licenses:
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).

## Linux

### Dependencies

- CMake 3.20 or newer, Ninja (or Make), Git
- GCC and G++ with 32-bit multilib support (`gcc -m32` must work)
- 32-bit development libraries for the SDL backends you want: X11 and/or Wayland, OpenGL (Mesa),
  PulseAudio/PipeWire/ALSA. SDL is built from source and linked statically, but it loads these
  libraries at run time and only builds the backends whose 32-bit libraries it finds.

Examples (package names vary between releases):

| Distribution | Packages |
|---|---|
| Arch / CachyOS | `gcc` `lib32-gcc-libs` `cmake` `ninja` `lib32-libx11` `lib32-libxext` `lib32-libxcursor` `lib32-libxrandr` `lib32-wayland` `lib32-mesa` `lib32-libpulse` `lib32-alsa-lib` `lib32-pipewire` |
| Debian / Ubuntu | `gcc-multilib` `g++-multilib` `cmake` `ninja-build`, plus the `:i386` variants of the X11/Wayland, Mesa and audio `-dev` packages (after `dpkg --add-architecture i386`) |

### Build

```sh
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/linux-i686.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
./build/pc/recvx            # first start asks for the disc image
```

`CMAKE_BUILD_TYPE` defaults to `RelWithDebInfo`. The executable is `build/pc/recvx`; the
co-op server and its test client are built alongside it (`build/pc/server/`).

### Running on older distributions (glibc)

With `RECVX_GLIBC_COMPAT=ON` (the default on Linux) the executable needs **glibc 2.34 or newer**,
whatever glibc the build machine has. `cmake/glibc_compat.h` is force-included into every source
file and binds math functions to their old symbol versions; `cmake/glibc_compat.c` provides the
newer C23/BSD symbols (`__isoc23_*`, `strlcpy`, `_dl_find_object`, ...) that SDL, ImGui and
libgcc would otherwise pull in. 2.34 is the floor because that release moved `pthread_*`,
`dlopen` and the `time64` functions into libc. Check the result with:

```sh
objdump -T build/pc/recvx | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1   # GLIBC_2.34
```

`-DRECVX_GLIBC_COMPAT=OFF` binds whatever the build machine's glibc provides.

At run time the 32-bit glibc, a 32-bit OpenGL 3.3 driver and the 32-bit X11/Wayland and audio
libraries must be installed (on Arch: `lib32-glibc`, `lib32-mesa`, `lib32-libx11` or
`lib32-wayland`, `lib32-libpulse`).

## Windows

### Cross-build from Linux (mingw-w64 GCC)

```sh
cmake -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake \
      -DMINGW_PREFIX=/path/to/usr -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-win --target recvx
```

`MINGW_PREFIX` is the directory that holds `bin/i686-w64-mingw32-gcc`; omit it when the
compiler is on `PATH`. Tested with Arch's `mingw-w64-gcc` (i686, UCRT). The packages do not need to be installed system-wide: unpacking
`mingw-w64-{gcc,binutils,crt,headers,winpthreads}` into a directory is enough.

**What to ship:** `build-win/pc/recvx.exe` alone. SDL3, miniupnpc, libnatpmp, libstdc++, libgcc
and winpthreads are linked statically; only Windows system DLLs remain (UCRT, kernel32, ws2_32,
opengl32 loaded at run time, ...). `i686-w64-mingw32-strip recvx.exe` removes the debug info.
The executable needs Windows 10 or later (or the Universal C Runtime update on 7/8.1) and an
OpenGL 3.3 driver.

The Windows build uses the same memory layout as Linux (image base `0x30000000`, ASLR off,
GCC bitfield layout via `-mno-ms-bitfields`) and keeps frame pointers so the crash log can walk
the stack. With a fixed image base, `i686-w64-mingw32-addr2line -f -e recvx.exe ADDR` resolves
addresses from `recvx.log`.

The Windows build is tested under Wine; reports from real Windows machines are welcome.

### Native Windows build

Not tested. A native MSYS2 MINGW32 GCC toolchain may work with the same CMake files; MSVC and
clang-cl are not supported.

## Co-op server only

`recvx-server` (see [SERVER.md](SERVER.md)) has no game, SDL or disc dependencies and can be
built on its own, for example on a server machine:

```sh
cmake -S pc/server -B build-server -DCMAKE_BUILD_TYPE=Release
cmake --build build-server
./build-server/recvx-server --help
```

On Linux it is linked statically by default (`RECVX_SERVER_STATIC=ON`). A container build is
in `pc/server/Dockerfile`, a systemd user unit in `pc/server/recvx-server.service`.

## Release packages

`pc/tools/package.sh OUT_DIR [--linux build] [--windows build-win] [--server build-server]`
packs finished builds into the release archives, named after the version in the top-level
`CMakeLists.txt` (`project(recvx_pc VERSION ...)`):

| Archive | Contents |
|---|---|
| `recvx-enhanced-port-VER-linux-i686.tar.gz` | `recvx` (stripped), README, LICENSE, THIRD_PARTY_NOTICES, CHANGELOG, `docs/USER_GUIDE.md`, empty `mods/` and `mods/textures/` (each with a short README.txt) |
| `recvx-enhanced-port-VER-windows-x86.zip` | `recvx.exe` (stripped), the same docs and folders |
| `recvx-server-VER-linux-x86_64.tar.gz` | `recvx-server` (static), `recvx-client`, example config, systemd unit, `docs/SERVER.md` |
| `...-linux-i686-debug.tar.gz`, `...-windows-x86-debug.zip` | the unstripped binaries, for `addr2line` on crash-log addresses |

It also writes `RELEASE_NOTES.md` (this version's CHANGELOG section, requirements and first start,
the README's AI note) and `SHA256SUMS`. No disc image or game data goes into any archive.
`recvx --version` and `recvx-server --version` print the version plus `git describe` as of the
last CMake configure.

## Continuous integration

`.github/workflows/build.yml` runs on pushes to `main`, pull requests, `v*` tags and by hand:
`linux-i686` (Ubuntu 24.04, GCC 13 multilib and the `:i386` SDL backend libraries; build, tests,
package), `windows-x86` (cross build with Ubuntu's `mingw-w64` POSIX-thread GCC; package) and
`server` (standalone build, `rdv_paths`/`rdv_nat`, package). A `v*` tag whose version matches
`CMakeLists.txt` (`v0.1.0`, or `v0.1.0-rc1`) also creates a GitHub release with the archives,
`SHA256SUMS` and the generated notes; `-rc` tags and 0.x versions are marked as pre-releases.

The CI Windows build is not identical to the documented local one (Arch's `mingw-w64-gcc`):

| | CI (Ubuntu 24.04) | Arch `mingw-w64-gcc` |
|---|---|---|
| GCC | 13 | 16 |
| C runtime | `msvcrt.dll` | UCRT (`api-ms-win-crt-*`) |
| Threads | POSIX (winpthreads, static) | POSIX (winpthreads, static) |

Both link statically, keep the image base `0x30000000` and the same flags; only system DLLs are
imported. The game is tested with the UCRT build: report problems that only the release build shows.

## CMake options

| Option | Default | Meaning |
|---|---|---|
| `CMAKE_TOOLCHAIN_FILE` | none | `cmake/linux-i686.cmake` or `cmake/mingw-i686.cmake` (required for a 32-bit build on a 64-bit host) |
| `CMAKE_BUILD_TYPE` | `RelWithDebInfo` | `Debug`, `Release`, `RelWithDebInfo`, `MinSizeRel` |
| `RECVX_DECOMP_DIR` | `decomp` | path of the recvx-decomp checkout |
| `RECVX_VENDORED_SDL` | `ON` | build SDL 3 from source and link it statically; `OFF` uses a system SDL3 (32-bit) package |
| `RECVX_GLIBC_COMPAT` | `ON` (Linux) | run on glibc 2.34 and newer (see above) |
| `RECVX_SERVER_STATIC` | `ON` (Linux) | link `recvx-server` statically |
| `RECVX_LAYOUT_PAD` | `0` | test aid: bytes of padding before all code and data, for `pc/tools/layout_check.sh` (see [ARCHITECTURE.md](ARCHITECTURE.md#determinism)) |
| `MINGW_PREFIX` | `$MINGW_PREFIX` | mingw toolchain only: prefix of the cross compiler |

## Tests

```sh
ctest --test-dir build --output-on-failure
```

| Test | What it checks |
|---|---|
| `vu` | static VU code and the hand-written fast paths against the VU interpreter |
| `movies` | decodes every movie of the disc; skipped unless `RECVX_ISO=<image>` is set |
| `bindings` | control bindings: defaults, rebinding, ini format, a virtual SDL gamepad |
| `appear` | the "You appear as" model list |
| `net` | co-op UDP socket and port mapping start/stop |
| `coop` | co-op packet parser: malformed events of every type, join code, fuzz loop |
| `fileio` | UTF-8 paths and replace-on-rename saving |
| `parsers` | checked file reading: PRS rooms against the game's decoder, crafted AFS tables, `.cvxm` models, DDS / PNG headers, BioRand start-kit scripts (also worth running in a `-fsanitize=address,undefined` build) |
| `rdv`, `rdv_client` | co-op server protocol, server core and client state machine |
| `rdv_fuzz` | parser and state machines under AddressSanitizer/UBSan (Linux) |
| `rdv_paths` | a local server and two test clients: punched, relayed, passwords, loss (~45 s, Linux) |
| `rdv_nat` | NAT types, delay and loss in rootless network namespaces (~45 s; skipped where user namespaces are not allowed) |

The map tools have their own Python tests (no disc needed; run from the repository root):

```sh
python3 -m unittest discover -s pc/tools -p 'test_*.py'
python3 -m unittest discover -s pc/tools/re4 -p 'test_*.py'
```

Determinism and rendering comparisons that
need the disc image and a GPU are described in [ARCHITECTURE.md](ARCHITECTURE.md#determinism).

## Compiler warnings

The decomp's own sources compile with `-w` (unchanged code), except that calling an undeclared
function is an error (a `float` argument goes as a `double`, a `char`, `float` or 64-bit result is
read wrongly). `-w` also hides `-Werror=...`, so this is GCC 14's default error for it, kept by
leaving out `-fpermissive`; with GCC 13 the ctest `decomp_declarations`
(`pc/tools/decomp_warnings.py --declarations build`) finds them. The port's code (`pc/glue`,
`pc/src`) compiles with `-Wall`. `pc/tools/decomp_warnings.py [build dir]` recompiles the decomp with return-value and
uninitialized-variable warnings and regenerates [docs/dev/missing-returns.txt](dev/missing-returns.txt)
and [docs/dev/uninitialized-locals.txt](dev/uninitialized-locals.txt).
