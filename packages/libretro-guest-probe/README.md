# libretro-guest-probe

Runs a [libretro](https://www.libretro.com/) core with no window and no audio device, feeds it scripted pad input, answers its option queries, and reads the guest's memory while it runs, so a test can say "after this input the game reached this screen". It is how a console port's input is proven on an emulator core with no one at the controls.

```sh
libretro-guest-probe --map start=0x0008 --map b=0x4000 \
    --replay session.input \
    --option '*serial*=enabled' \
    --address-mask 0x00ffffff --tick 0x8c0e0100 --watch screen=0x8c0e0104 \
    --expect screen=3 --stop screen=3@450 \
    --record callback.input \
    ./some_core_libretro.so game.elf ./isolated-system-folder
```

The game and the core are the caller's. Nothing here ships a core, a BIOS or game data, and the tests use a fake core with no emulation.

Version 1.0.0. Written in C (C11) with CMake. MIT licence. It depends on [`pad-replay`](../pad-replay) for the replay format.

## What it does

- **Scripted input.** `--replay` plays a [pad-replay](../pad-replay/docs/format.md) file into the core's ports (slot N feeds port N), or `--tap TICK:MASK[:HOLD]` makes a short script on the command line. The button bits are the port's own; `--map NAME=MASK` says which bit is which libretro joypad button (`b y select start up down left right a x l r l2 r2 l3 r3`).
- **Recording.** `--record FILE` writes the pad states the core actually asked for, in the same format and only when they change, so a second run can replay what the first run saw. The file is created exclusively.
- **Core options.** `--option KEY=VALUE` answers a core's option query; `KEY` may use `*` (`'*serial*=enabled'`). The first matching entry wins and each answered key is logged once. A key nothing matches is left to the core's default.
- **Guest memory.** The core's system RAM (or another memory block, `--memory-id`) is read by address. `--tick` names the guest's own frame counter, `--watch NAME=ADDR[:SIZE]` logs a value whenever it changes, `--expect NAME=VALUE` fails the run unless the value was seen, and `--stop NAME=VALUE[@TICK]` ends the run once it is. Addresses are the guest's own (a symbol address from the toolchain's `nm`); `--address-mask` turns one into an offset (`0x00ffffff` for a console that mirrors its RAM at several bases). Sizes are 1, 2 or 4 bytes, little endian, or big endian with `be` (`4be`).
- **Not started yet.** While the guest's tick reads 0, or its memory is not there, the run skips those frames (sleeping `--delay-ms`, 1 by default, so a slow core is given time) and does not move the script on. This is the guard that stops a run from reading a guest struct before the guest has initialised it.
- **Bounds.** `--max-frames` (default 20000) bounds the host frames; `--frame-delay-ms` slows a run down if a core needs it.
- **Hardware rendering.** A core that asks for an OpenGL context gets a hidden SDL3 window and a core-profile context (`--gl 3.2` by default), made only when the core asks, so a software core runs on a machine with no display and no GPU. If no context can be made the core is told no and the log says why.

Exit codes: 0 ran and every `--expect` was seen, 1 an `--expect` was not seen, 2 usage, 3 the core would not load, 4 the game would not load, 5 the record file could not be created or written.

The log (standard error) has one line per event, for a script to read: `CORE name version`, `OPTION key=value`, `HW_CONTEXT ...`, `WATCH name host_frame=N tick=T value=0x..`, `EXPECT name=0x.. PASS|FAIL`, `PROBE PASS|FAIL frames=N not_ready=N tick=T stopped=0|1`.

## Install

This is a C library and tool, so it is not on a package index. A project takes it at a release tag, with CMake `FetchContent` (CMake 3.20 or later, git, and network at configure time):

```cmake
include(FetchContent)
FetchContent_Declare(libretro_guest_probe
  GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git
  GIT_TAG        libretro-guest-probe@1.0.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  packages/libretro-guest-probe)
FetchContent_MakeAvailable(libretro_guest_probe)

target_link_libraries(my-tool PRIVATE lgp)          # the library; the command line tool is the target libretro-guest-probe
```

`examples/consumer/CMakeLists.txt` is that, complete. Three things are fetched at configure time:

| What | From | Pinned by |
| --- | --- | --- |
| `pad-replay` | this repository, from the checkout beside this folder when there is one, else at the tag `LGP_PAD_REPLAY_TAG` (default `pad-replay@1.0.0`) | the tag |
| `libretro.h` | the RetroArch repository, `libretro-common/include/libretro.h` at commit `39fd949a6d45586501251a94cf795c5bba974606` | its SHA-256, `469b0fbe2919ca9f5372b48a94ff8af51f70ee00c364798ec70670ecf6b7f9da`; a different file fails the configure |
| SDL3 | the system or `-DCMAKE_PREFIX_PATH` (3.2 or later) | `find_package`; `-DLGP_REQUIRE_SDL=OFF` builds without it, and a core that wants OpenGL is then refused one |

For an offline build use `-DFETCHCONTENT_SOURCE_DIR_LIBRETRO_GUEST_PROBE=<a checkout of game-kit>`, `-DFETCHCONTENT_SOURCE_DIR_PAD_REPLAY=<a checkout of game-kit>` and `-DLGP_LIBRETRO_HEADER=<a copy of that libretro.h>` (it is checked against the same hash).

**Licence of `libretro.h`.** The header is the libretro API header of the RetroArch project, MIT licence, "Copyright (C) 2010-2024 The RetroArch team". It is downloaded, not copied into this repository, so its licence text stays with the file. A libretro core is a separate program under its own licence (often GPL); this package loads one at run time and ships none.

## Use as a library

Every part but the host works with no core, which is how the tests run:

```c
#include "lgp/lgp.h"

LgpOptions *options = lgp_options_new();
lgp_options_parse(options, "*serial*=enabled");

LgpProbe *probe = lgp_probe_new();
lgp_probe_set_mask(probe, 0x00ffffffu);
lgp_probe_parse_tick(probe, "0x8c0e0100");
lgp_probe_parse_watch(probe, "screen=0x8c0e0104");
lgp_probe_parse_expect(probe, "screen=3");

LgpConfig config;
lgp_config_defaults(&config);
config.core_path = "some_core_libretro.so";
config.content_path = "game.elf";
config.system_dir = "isolated-system-folder";
config.options = options;
config.probe = probe;
config.map[0].retro_id = lgp_button_id("start");
config.map[0].mask = 0x0008;             /* the port's own bit */
config.map_count = 1;
LgpResult result;
int code = lgp_run(&config, &result);
```

| Part | What it does |
| --- | --- |
| `lgp_options_*` | The option table: `*` patterns, first match wins, an answer reported once. |
| `lgp_button_id`, `lgp_map_parse`, `lgp_pads_init`, `lgp_pads_state` | The input bridge: the port's bits to libretro joypad buttons, the left stick as `(axis - 128) * 256`, the whole-joypad mask. This is what `retro_input_state` returns. |
| `lgp_probe_*`, `lgp_memory_read` | Watches, the guest tick, stop rules and expectations over a block of memory. `lgp_probe_sample` takes any block, so it can be tested against a fake memory map. |
| `lgp_args_parse`, `lgp_setup_free`, `lgp_usage` | The command line, as a library function. |
| `lgp_run`, `lgp_config_defaults` | Loads the core and runs it. One run at a time per process: libretro's callbacks carry no user pointer, so the state is static. |

## What was merged, and what was left out

The tool replaces two runners that each carried a fix the other lacked. This package has both: scripted playback and recording from one, answers to core options from the other; the guest-not-started guard and its delay from one, the frame bound as a setting for both (one ran 20000 frames, the other 3000).

Left to the caller, because they know a game: what the guest's memory means (the old runners read a whole `Game` struct and tested its screen number; here the caller names the addresses to watch), and the sanity bounds on that struct. The old runners also drove a hard-coded sequence of Start and Cross presses when no replay was given; here that is `--tap` or a replay, so no game's button timing lives in the package.

## What runs where

| Part | Runs on | In |
| --- | --- | --- |
| the library, the tool and its tests (C, CMake) | Windows, macOS, Linux; CMake 3.20+, Ninja, a C compiler, SDL3 (or `-DLGP_REQUIRE_SDL=OFF`) | any shell |
| `build-and-test.sh` | macOS, Linux | Bash or zsh |
| `build-and-test.ps1` | Windows | PowerShell 5.1 or 7 |

The tests need no display, no GPU and no libretro core. They cover the option table, the input bridge, the memory probe against a fake memory map, the command line, and the host end to end against a fake core (`tests/fake_core.c`, a few functions with no emulation) for scripted input, option answers, the not-started guard, the frame bound, a stop rule, expectations, recording, and a hardware-render request refused where no display exists.

Tested at 1.0.0:

| System | Toolchain | Result |
| --- | --- | --- |
| Linux x86-64 (Ubuntu 24.04, in a container, no GPU) | gcc 13, SDL3 3.4, `check-c.sh`; also built with `-DLGP_REQUIRE_SDL=OFF`, and under AddressSanitizer and UBSan | pass |
| a real core (PPSSPP, Flycast) with a game | the tests drive the host with a fake core (`tests/fake_core.c`). A project supplies its own core and its own game and points the tool at them; the package carries neither | scope of the tests: the host, input, options and memory probe against a fake core |
| the hidden OpenGL window and context | the tests run the refusal path: with no display and no GPU a core's request for OpenGL is answered no and the log says why. The context is made on a machine whose SDL3 has an OpenGL window system and a GPU | refusal path passes |
| Windows 11 Pro (10.0.26300) | MSYS2 UCRT64 clang 22.1.8, SDL3 from MSYS2, `tools\check-c.ps1` in PowerShell 7 | pass. This run found that an address over 32 bits was accepted on Windows, where `unsigned long` is 32 bits; it is now refused there too |
| macOS 27.0 (Apple M6) | Apple clang 21.0.0, CMake 4.5.1, Ninja, `tools/check-c.sh` | pass |

## Layout

```
include/lgp/lgp.h      the whole API
src/options.c          the option table
src/input.c            the input bridge
src/probe.c            the memory probe
src/args.c             the command line
src/host.c             the libretro host (dlopen, environment, input, the frame loop)
src/gl_sdl.c, gl_none.c  the hidden OpenGL context with SDL3, or a refusal when built without it
src/main.c             the libretro-guest-probe tool
cmake/LgpLibretroHeader.cmake   the pinned, hash-checked download of libretro.h
tests/                 options, input, probe and args tests; the host test and fake_core.c
examples/consumer/     a project using the package
build-and-test.ps1     build and test, Windows PowerShell
build-and-test.sh      build and test, macOS and Linux Bash
CMakeLists.txt         library `lgp`, tool `libretro-guest-probe`, tests
```

## Licence

MIT, see `LICENSE`. `libretro.h` is fetched, not included: see above.
