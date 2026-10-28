> [!IMPORTANT]
> **AI-assisted project.** Most of the code, tools and documentation in this repository were written by AI coding agents (Anthropic's Claude models, plus OpenAI Codex for parts of the map tooling), directed and reviewed by hand by the maintainer, who also tested the results in-game. Treat it like any other early-stage community project: expect bugs, read the code before relying on it, and report problems via issues.
> The underlying decompilation ([AshfordFamily/recvx-decomp](https://github.com/AshfordFamily/recvx-decomp)) is a separate, human-led project. Our changes to it live in a fork and are disclosed as AI-assisted, as that project asks.
> No game data is included. You need your own legally obtained copy of *Resident Evil Code: Veronica X* (PS2, NTSC-U, SLUS-20184).

Community Discordserver, thanks cristinathegamer056 for setting it up!
https://discord.gg/ZpDn46yv2

# recvx-enhanced-port

[![build](https://github.com/aikatore/recvx-enhanced-port/actions/workflows/build.yml/badge.svg)](https://github.com/aikatore/recvx-enhanced-port/actions/workflows/build.yml)

A native PC port of *Resident Evil Code: Veronica X* (PS2) for Linux and Windows, built from the
community decompilation [recvx-decomp](https://github.com/AshfordFamily/recvx-decomp). The
original game code and its PS2 platform layer are compiled for the PC; the PS2 hardware
underneath (GS, VU, DMA, sound driver, disc, memory card, pad) is emulated at its boundaries, and
native paths replace the expensive parts. The game reads your own disc image in place.

[![Resident Evil Code: Veronica X (PS2) | PC recompilation with online co-op and Third-Person](https://img.youtube.com/vi/0sW1BHgIMXs/maxresdefault.jpg)](https://www.youtube.com/watch?v=0sW1BHgIMXs)

*Click the image to watch the video on YouTube.*

## Features

- **Enhanced renderer**: native 3D and textures on the GPU, widescreen (hor+, up to ultrawide),
  internal resolution up to 8x, in-between frames up to the display's refresh rate while the game
  logic stays at its original 30 FPS, a frame rate limit, optional PS2 motion trail.
- **Online co-op for two players**: direct, automatic or server-relayed connections, a lobby in
  the title menu, join codes and passwords, trading with item requests, shared or individual loot,
  saved partner inventories, rejoin after a crash. Includes a small C rendezvous/relay server.
- **Third-person mode** (optional): over-the-shoulder camera with mouse/keyboard controls, free
  aim and two camera styles.
- **Modern controls** (optional) for the fixed cameras: the stick walks where it points on screen,
  as in the RE HD remasters, and keeps its direction through camera cuts.
- **Model swaps**: play as other characters and creatures, with matching voices and cutscene
  doubles.
- **Enemy options**: extra enemies, rearranged placements, a seeded randomizer, and an enemy
  spawner.
- **BioRand**: randomized items, start inventories and doors from
  [BioRand](https://github.com/biorand/classic) seeds, switched on at the title, in co-op too (see
  [USER_GUIDE.md](docs/USER_GUIDE.md#randomizer-biorand)).
- **Save files**: every save its own file, as many as you like, in the game's own save screens
  without the memory card screens (the emulated memory card stays available).
- **Enhanced settings** in the game's own OPTION menu and an F1 overlay; full rebinding of keyboard,
  mouse and gamepad; per-window input source for local co-op on one PC.
- **Cheats and debug tools** (F2), co-op window (F3).
- **HD texture packs**: PCSX2 HD texture packs for the game load as they come, from
  `mods/textures/` (see [HD texture packs](#hd-texture-packs)).
- **Mods**: replacement room files and archive entries from a `mods/` folder, plus tools to build
  new rooms.
- **Tooling** for determinism checks, rendering comparisons and profiling.

## Requirements

- Your own disc image of *Resident Evil Code: Veronica X*, PS2 NTSC-U, **SLUS-20184** (`.iso`).
- **Linux**: an x86 PC; the game is a 32-bit (i686) program and needs glibc 2.34 or newer plus
  the 32-bit (multilib) OpenGL, X11/Wayland and audio libraries.
- **Windows**: Windows 10 or later (32-bit executable, runs on 64-bit Windows).
- A GPU with OpenGL 3.3.

## Download

Prebuilt Linux and Windows builds and the co-op server are on the
[Releases](https://github.com/aikatore/recvx-enhanced-port/releases) page (no game data
included). Unpack and run `recvx.exe` (Windows) or `./recvx` (Linux); see
[docs/USER_GUIDE.md](docs/USER_GUIDE.md).

## HD texture packs

HD texture packs made for the PCSX2 emulator (for SLUS-20184, the US release) work without
conversion. None are included; use a pack you have.

1. Create a `textures` folder inside the `mods` folder, which sits next to the game (beside
   `recvx.exe` / `recvx` and the `save` folder): `mods/textures/`.
2. Put the pack in there as it comes. Any of these layouts works:
   - `mods/textures/SLUS-20184/replacements/...` (the folder from PCSX2's `textures` directory)
   - `mods/textures/replacements/...`
   - `mods/textures/<pack name>/...`, or the `.dds` files themselves
3. Start the game. The log says `hd textures: N replacements in <folder>`.

**Several packs and load order:** put each pack in its own folder and number the folders, for
example `mods/textures/1_addon/` and `mods/textures/2_base/`. When two packs replace the same
texture, the one in the folder that comes first by name wins, so `1_...` overrides `2_...`.

Settings (F1 or ENHANCED SETTINGS, DISPLAY): **HD Textures** turns packs on or off, **HD
Menus** also uses redesigned menus, title and inventory (on), **HD Font** uses
the pack's font (off). A pack in another folder: `hd_textures_dir = <path>` in `recvx.ini` or
`--hd-textures <path>`. Details: [docs/USER_GUIDE.md](docs/USER_GUIDE.md#hd-texture-packs).

## Building from source

```sh
git clone --recursive https://github.com/aikatore/recvx-enhanced-port.git
cd recvx-enhanced-port
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/linux-i686.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
./build/pc/recvx
```

`--recursive` fetches the decompilation and its header submodules; the decomp's large compiler
submodule is skipped by design (the PC build does not use it). On the first start a file
selector asks for the disc image; the choice is remembered. The Windows build is cross-compiled
with mingw-w64: see [docs/BUILDING.md](docs/BUILDING.md).

## Documentation

| Document | For |
|---|---|
| [docs/USER_GUIDE.md](docs/USER_GUIDE.md) | players: first start, settings, controls, cheats, saves, troubleshooting |
| [docs/COOP.md](docs/COOP.md) | players and developers: online and local co-op, protocol reference |
| [docs/SERVER.md](docs/SERVER.md) | running the co-op rendezvous/relay server |
| [docs/BUILDING.md](docs/BUILDING.md) | building on Linux and for Windows, CMake options, tests |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | developers: how the port works, renderer, determinism, test aids |
| [docs/THIRDPERSON.md](docs/THIRDPERSON.md) | third-person mode internals |
| [docs/ACTORS.md](docs/ACTORS.md) | models, skeletons, animation, model swaps |
| [docs/ENEMIES.md](docs/ENEMIES.md) | enemy data, spawner, enemy options |
| [docs/HD_PACK.md](docs/HD_PACK.md) | how PCSX2 HD texture packs are matched and loaded |
| [docs/modding/](docs/modding/README.md) | room format and building custom rooms |
| [CHANGELOG.md](CHANGELOG.md) | feature history |
| [CONTRIBUTING.md](CONTRIBUTING.md) | how to contribute |

## Credits

- **[recvx-decomp](https://github.com/AshfordFamily/recvx-decomp)** by AshfordFamily and its
  contributors: the decompilation this port is built on (MIT).
- Libraries: [SDL 3](https://github.com/libsdl-org/SDL) (zlib),
  [Dear ImGui](https://github.com/ocornut/imgui) (MIT),
  [miniupnpc and libnatpmp](https://github.com/miniupnp) (BSD-3-Clause),
  [Monocypher](https://github.com/LoupVaillant/Monocypher) (BSD-2-Clause or CC0). Versions and
  details: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## License

The code in this repository is released under the [MIT License](LICENSE). The decompilation in
`decomp/` is MIT-licensed by its authors; third-party components keep their own licenses (see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).

## Legal

This is an unofficial fan project, not affiliated with or endorsed by Capcom, Sony Interactive
Entertainment, Sega or CRI Middleware. *Resident Evil* and *Code: Veronica* are trademarks of
Capcom; PlayStation is a trademark of Sony Interactive Entertainment. No game assets are
distributed: you must supply your own legally obtained copy of the game. Files you generate from
your copy (for example modded rooms) contain game-derived data and must not be shared.
