# game-kit

Small, focused packages from Uppercut Labs for making, porting, testing and capturing games. Each package lives in its own folder under `packages/`, is versioned on its own, and has a README with its API and install line. The general-purpose kits are separate: [js-kit](https://github.com/uppercut-labs/js-kit), [py-kit](https://github.com/uppercut-labs/py-kit), [ps-kit](https://github.com/uppercut-labs/ps-kit) and [c-kit](https://github.com/uppercut-labs/c-kit).

## Packages

Every package is at version 1.0.0 or later; each README gives its current version. npm packages are on npm under `@uppercut-labs`, Python packages are on PyPI with the `uppercut-` prefix, and C and Swift packages are released as git tags `<name>@<version>` in this repository.

### TypeScript

| Package | What it does |
| --- | --- |
| [`@uppercut-labs/cappy-adapter`](packages/cappy-adapter) | A typed client for Cappy's game adapter protocol v1: handshake, scenarios, recording and replay, heartbeat and error codes, plus a child-process runner with a clean shutdown |
| [`@uppercut-labs/emulator-session`](packages/emulator-session) | Runs an emulator for a headless capture: isolated profile, muted and backgrounded, bounded output reader, window captures at set times, and a stop that signals only the launched process. Profiles for ares, DuckStation, Flycast, PCSX2, PPSSPP, redream, Ymir, Dolphin (with DTM movie playback) and xemu |
| [`@uppercut-labs/fgc-data`](packages/fgc-data) | Fighting-game rosters, gameplay summaries, a catalog of games with team-selection schemas, and builder helpers, as typed data |

### Python

| Package | What it does |
| --- | --- |
| [`uppercut-bizhawk-driver`](packages/uppercut-bizhawk-driver) | Drives BizHawk: a pinned, checked install, a private config per run, Lua table configs, a launch that stops only its own process, and a wait for the script's result file |
| [`uppercut-c-style`](packages/uppercut-c-style) | Checks a repository's C against a fixed-width integer style: no plain `int` or `uint32_t`, pointer stars on the type, byte-range loops on the 8-bit alias, with per-repository ABI exemptions and excludes |
| [`uppercut-chiptune-synth`](packages/uppercut-chiptune-synth) | Deterministic chiptune audio for console homebrew: square, triangle and noise voices, envelopes, step sequencing, WAV and raw PCM output |
| [`uppercut-gba-asset-export`](packages/uppercut-gba-asset-export) | Exports indexed pixel art for the Game Boy Advance: BGR555 palettes, 4bpp tiles, grit-style C files, 4bpp BMP, Butano JSON and GIMP palettes |
| [`uppercut-gba-rom-verify`](packages/uppercut-gba-rom-verify) | Checks a built Game Boy Advance ROM and its ELF before release: logo, title, game and maker codes, header complement, size and alignment, with a SHA-256 to pin |
| [`uppercut-godot-render`](packages/uppercut-godot-render) | Renders a Godot scene offline with Movie Maker and checks it: frame count, size, blank frames, per-frame hashes, bt709 H.264 and a JSON manifest |
| [`uppercut-iso-normalize`](packages/uppercut-iso-normalize) | Builds a PlayStation 2 data disc from an ELF with pycdlib and makes disc images byte-reproducible: fixed dates, ISO 9660 version suffixes and a checked SYSTEM.CNF and ELF |
| [`uppercut-iso9660`](packages/uppercut-iso9660) | Reads ISO 9660 disc images in 2048 and 2352-byte sectors: walks directories, extracts files with a SHA-256, finds a PS2 disc's SYSTEM.CNF and boot ELF |
| [`uppercut-mgba-smoke`](packages/uppercut-mgba-smoke) | Runs a Game Boy Advance ROM in native mGBA in a sandbox: key taps, frame stepping, RAM probes, screenshots and Lua assertions, with the screenshots checked |
| [`uppercut-pad-replay-tools`](packages/uppercut-pad-replay-tools) | Builds and checks controller replay files and capture scripts for game ports, and exports a replay as a Dolphin DTM |
| [`uppercut-pcsx2-pine`](packages/uppercut-pcsx2-pine) | A client for PCSX2's PINE socket: PS2 memory reads and writes, batch reads, title, status, game id and save states, with an optional write allowlist |
| [`uppercut-pinned-sources`](packages/uppercut-pinned-sources) | Fetches build inputs listed with a SHA-256 into a cache, refuses any mismatch, reuses an intact cache offline and reports what it fetched |
| [`uppercut-pixel-art-sheets`](packages/uppercut-pixel-art-sheets) | Pillow helpers for indexed pixel-art sheets: palettes, pattern fills, a bitmap font and sheet packing |
| [`uppercut-poly-meshes`](packages/uppercut-poly-meshes) | Regular-polyhedron meshes (exact vertices, exact hull, icosphere) and C-array emitters for float and fixed-point console layouts |
| [`uppercut-port-proof`](packages/uppercut-port-proof) | The proof run of a recompiled game port: PPM frame dumps to PNG, frame MD5 comparison, WAV statistics, press scripts and a debug-socket driver |
| [`uppercut-ps2-match`](packages/uppercut-ps2-match) | PlayStation 2 matching decompilation: compile one function and check its bytes, place data, split C files, fetch a pinned toolchain, audit a public repo |
| [`uppercut-retro-pixels`](packages/uppercut-retro-pixels) | Retro console pixel formats and texture layouts as pure functions: RGB565, ARGB4444, ARGB1555, RGBA5551 and BGR555, the PSP swizzle, GameCube RGBA8 tiles, the original Xbox swizzle, RLE, palette ramps and a glyph-atlas packer |
| [`uppercut-rom-headers`](packages/uppercut-rom-headers) | Computes, fixes and verifies the header checksums of Nintendo 64, Mega Drive and 32X, and Game Boy Advance ROMs |
| [`uppercut-sound-effect-synth`](packages/uppercut-sound-effect-synth) | Deterministic procedural sound effects: envelopes, filtered noise, sweeps, seeded variation and WAV output |

### C

| Package | What it does |
| --- | --- |
| [`bitmap-font`](packages/bitmap-font) | Small bitmap fonts (5x7, 8x8 and 3x5 title letters) as tables, drawn through a callback per run of pixels, generated from one data file for C and Python |
| [`det-core`](packages/det-core) | The deterministic numeric core of a game simulation: exact-width types (optionally the console SDK's own), saturating Q16.16 fixed point, FNV-1a and xorshift32, integer only |
| [`gba-tile-ui`](packages/gba-tile-ui) | Shadow tile maps and sprite table for a Game Boy Advance game, copied to VRAM and OAM once a frame when dirty, so screen code is tested on a host |
| [`hud-path-font`](packages/hud-path-font) | A single-stroke HUD font as paths on a drafting grid, baked once into a 4-bit coverage atlas through a callback, or stroked directly by a line renderer |
| [`kos-naomi-cabinet`](packages/kos-naomi-cabinet) | What a Sega NAOMI game built with KallistiOS needs from the cabinet: EEPROM settings the BIOS accepts, coin credits, test and service switch edges, and a save-block adapter, all tested without a cabinet |
| [`libretro-guest-probe`](packages/libretro-guest-probe) | Runs a libretro core with no window or audio, feeds it scripted pad input and reads guest memory, so a test proves a port's input on an emulator core |
| [`minimal-png`](packages/minimal-png) | A PNG writer with no zlib for frame dumps, screenshot keys and debug atlases: stored deflate blocks, any viewer opens the file |
| [`pad-replay`](packages/pad-replay) | A plain-text controller replay for a game port: a six-field line format, a strict reader, a player and a recorder |
| [`port-input-harness`](packages/port-input-harness) | Tests a game port's controller handling with SDL3 virtual controllers (DualSense, Xbox, Switch Pro, Steam Deck and more as data), hot-plug, scripts and pass/fail runners |
| [`port-sdl-input`](packages/port-sdl-input) | The SDL3 input layer for a game port: every keyboard and controller at once, positional mappings, two players with pinning and swap, SOCD cleaning, remapping, rumble and prompt letters |
| [`port-settings`](packages/port-settings) | A key-value settings file for a game port: keeps key order, saves through a temporary file, reads typed values and UTF-8 paths on Windows |
| [`ps2-audsrv-stream`](packages/ps2-audsrv-stream) | Starts the PlayStation 2's audsrv from an embedded module and keeps its ring fed once a frame from a callback that renders 16-bit stereo, with timed mutes for cuts |
| [`ps2-multitap-pads`](packages/ps2-multitap-pads) | Reads up to eight PlayStation 2 controllers through two multitaps once a frame, with a grace period for loose plugs, DualShock mode switching, the button names without the PS2SDK and a pad-replay name table |
| [`save-block`](packages/save-block) | Saved data that survives a power cut, a wrong disc and a bad card: two alternating checksummed, versioned slots, each save read back before it counts |
| [`sgdk-host-stub`](packages/sgdk-host-stub) | Builds a Mega Drive game's logic with a host compiler and runs it under ctest: a stub genesis.h records tiles, palettes, sprites, sound, SRAM and pad reads |

### Swift

| Package | What it does |
| --- | --- |
| [`mac-window-tools`](packages/mac-window-tools) | A macOS command line tool for capture scripts: list windows by owner, find a process's window, and capture it to a PNG without raising or focusing it |

## Installing

- **npm packages** are published as `@uppercut-labs/<name>`.
- **Python packages** are published on PyPI as `uppercut-<name>`.
- **C packages** have no package index. A release is a git tag `<name>@<version>`; take it with CMake `FetchContent` and `SOURCE_SUBDIR packages/<name>`, or vendor the folder and `add_subdirectory` it.
- **PowerShell modules** are imported from their folder or copied onto `$env:PSModulePath`.

## Working in this repo

| Language | Full check |
| --- | --- |
| TypeScript and JavaScript | `npm install && npm run check` |
| Python | `uv sync --all-packages && uv run pytest` |
| C | `./tools/check-c.sh` on macOS and Linux, `.\tools\check-c.ps1` on Windows |
| PowerShell | `Invoke-Pester` in each module's `tests` folder, with Pester 5 or later |

A package's tests must run without a display, a GPU, game data or hardware, so they run in a plain container. Fixtures are synthetic: no game ROMs, discs, dumps or copyrighted assets live in this repository.

See [RELEASING.md](RELEASING.md) for how a version is released.

## Licence

MIT
