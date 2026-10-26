# Third-party notices

This repository contains only the PC port's own code (MIT, see `LICENSE`). It
contains no game data: you need your own copy of Resident Evil Code: Veronica X
(PS2, US, SLUS-20184). Resident Evil and Code: Veronica are trademarks of
Capcom; this project is not affiliated with or endorsed by Capcom, Sony, Sega
or CRI Middleware.

## Git submodules (fetched, not included in this repository)

| Component | Path | Source | License |
|---|---|---|---|
| recvx-decomp (decompilation of the game), `pc-port` branch of a fork | `decomp/` | https://github.com/aikatore/recvx-decomp (upstream https://github.com/AshfordFamily/recvx-decomp) | MIT, Copyright (c) 2024-2026 AshfordFamily; see `decomp/LICENSE` |
| Sega Katana (Dreamcast) SDK headers | `decomp/include/recvx-decomp-katana` | https://github.com/SkeletonPicture/recvx-decomp-katana, a submodule of the decomp | third-party, proprietary (Sega); not redistributed here |
| CRI Middleware (ADX/Sofdec) headers and PS2 libraries | `decomp/include/recvx-decomp-cri` | https://github.com/SkeletonPicture/recvx-decomp-cri, a submodule of the decomp | third-party, proprietary (CRI Middleware); not redistributed here. The PC build uses only the headers, not the `.a` libraries |
| Metrowerks CodeWarrior compiler (`recvx-decomp-mwcc`) | `decomp/include/recvx-decomp-mwcc` | submodule of the decomp | not fetched by default (`update = none`) and not used by the PC build |

## Fetched by CMake at configure time (FetchContent)

| Component | Version | Source | License |
|---|---|---|---|
| SDL 3 (static; skipped with `-DRECVX_VENDORED_SDL=OFF`, which uses the system SDL3) | release-3.4.16 | https://github.com/libsdl-org/SDL | zlib |
| Dear ImGui | v1.92.9b | https://github.com/ocornut/imgui | MIT |
| miniupnpc | miniupnpc_2_3_3 | https://github.com/miniupnp/miniupnp | BSD-3-Clause |
| libnatpmp | 134fc89e | https://github.com/miniupnp/libnatpmp | BSD-3-Clause |
| Monocypher | 4.0.2 | https://github.com/LoupVaillant/Monocypher | BSD-2-Clause or CC0-1.0 (dual) |
| xxHash (header only: XXH3, the hash PCSX2 names texture replacements by) | v0.8.3 | https://github.com/Cyan4973/xxHash | BSD-2-Clause |
| bcdec (header only: BC1-BC7 decoding for HD texture packs) | 80859ed3 | https://github.com/iOrange/bcdec | MIT or Unlicense (dual) |

The built binaries link these libraries statically; ship their license texts
with any binary release.

## Text from other projects

| Component | Path | Source | License |
|---|---|---|---|
| BioRand option labels, help texts, defaults and seed-string format (written anew in C from RandoConfig.cs and MainWindow.xaml; no game data) | `pc/src/host/biorand_opts.c` | https://github.com/biorand/classic (commit c8df994) | MIT, Copyright (c) 2022-2026 Ted John |
