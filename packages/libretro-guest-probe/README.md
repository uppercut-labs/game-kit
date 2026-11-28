# libretro-guest-probe

Runs a [libretro](https://www.libretro.com/) core with no window and no audio device, feeds it scripted pad input, answers its option queries, and reads the guest's memory while it runs, so a test can say "after this input the game reached this screen". It is how a console port's input is proven on an emulator core with no one at the controls.

As a library it can also play the game: a frame hook reads the guest's memory after every frame and decides the next frame's pad state for up to eight ports, the video and audio can be written to files or pipes (from a hardware-rendered core too), and every run counts the frames the core presented, repeated or skipped.

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

Version 1.1.0. Written in C (C11) with CMake. MIT licence. It depends on [`pad-replay`](../pad-replay) for the replay format.

## What it does

- **Scripted input.** `--replay` plays a [pad-replay](../pad-replay/docs/format.md) file into the core's ports (slot N feeds port N), or `--tap TICK:MASK[:HOLD]` makes a short script on the command line. The button bits are the port's own; `--map NAME=MASK` says which bit is which libretro joypad button (`b y select start up down left right a x l r l2 r2 l3 r3`).
- **Recording.** `--record FILE` writes the pad states the core actually asked for, in the same format and only when they change, so a second run can replay what the first run saw. The file is created exclusively.
- **Core options.** `--option KEY=VALUE` answers a core's option query; `KEY` may use `*` (`'*serial*=enabled'`). The first matching entry wins and each answered key is logged once. A key nothing matches is left to the core's default.
- **Guest memory.** The core's system RAM (or another memory block, `--memory-id`) is read by address. `--tick` names the guest's own frame counter, `--watch NAME=ADDR[:SIZE]` logs a value whenever it changes, `--expect NAME=VALUE` fails the run unless the value was seen, and `--stop NAME=VALUE[@TICK]` ends the run once it is. Addresses are the guest's own (a symbol address from the toolchain's `nm`); `--address-mask` turns one into an offset (`0x00ffffff` for a console that mirrors its RAM at several bases). Sizes are 1, 2 or 4 bytes, little endian, or big endian with `be` (`4be`).
- **Not started yet.** While the guest's tick reads 0, or its memory is not there, the run skips those frames (sleeping `--delay-ms`, 1 by default, so a slow core is given time) and does not move the script on. This is the guard that stops a run from reading a guest struct before the guest has initialised it.
- **Bounds.** `--max-frames` (default 20000) bounds the host frames; `--frame-delay-ms` slows a run down if a core needs it.
- **Hardware rendering.** A core that asks for an OpenGL context gets a hidden window, a core-profile context (`--gl 3.2` by default) and a framebuffer object to draw into, sized to the core's largest frame, with the depth and stencil buffers the core asked for. They are made with SDL3, or SDL2 when that is what the system has, and only when the core asks, so a software core runs on a machine with no display and no GPU. If no context can be made the core is told no and the log says why.
- **Video and audio capture.** `--video FILE` writes every frame as raw RGB24, top row first, one frame per video callback (a repeated frame is written again, so the stream keeps the core's frame rate), and `--audio FILE` writes the core's samples as raw signed 16-bit little-endian stereo. Either can be a named pipe that FFmpeg reads. A hardware-rendered frame is read back from the framebuffer object. Capture starts once the guest is ready (the not-started guard below); `--capture-frames N` ends it after N frames, and `--video-size WxH` fixes the frame size, so a frame of another size is an error rather than a broken stream.
- **Frame counts.** Every run counts `retro_run` calls, new frames presented, repeated frames (the core's dupes), runs with no frame at all, the guest's stalls and leaps (runs where its tick stood still or jumped, with `--tick`), the longest `retro_run`, and the audio sent and captured.
- **The frame hook** (library only). After every `retro_run` a callback gets the guest's memory, the frame counts and the run's own numbers, and sets the next frame's buttons for each port, whether to capture, and whether to stop. See [Drive a game from its memory](#drive-a-game-from-its-memory).
- **Guest addresses.** `lgp_nm_lookup` finds a symbol in an `nm` listing, `lgp_ram_offset` turns a guest address into an offset with a RAM window (a mask and the bases where the guest mirrors its RAM), and `lgp_read_words` copies a record of 32-bit fields out of guest memory, unaligned and in either byte order.

Exit codes: 0 ran and every `--expect` was seen, 1 an `--expect` was not seen, 2 usage, 3 the core would not load, 4 the game would not load, 5 the record file or a capture output could not be created or written, or a captured frame changed size.

The log (standard error) has one line per event, for a script to read: `CORE name version`, `AV core fps=F sample_rate=R base=WxH max=WxH`, `OPTION key=value`, `HW_CONTEXT ...`, `WATCH name host_frame=N tick=T value=0x..`, `CAPTURE video_frames=N size=WxH audio_frames=N error=0|1`, `FRAMES runs=N presented=N repeated=N no_video=N guest_stalls=N guest_leaps=N max_run_us=N audio_frames=N`, `EXPECT name=0x.. PASS|FAIL`, `PROBE PASS|FAIL frames=N not_ready=N tick=T stopped=0|1`.

## Install

This is a C library and tool, so it is not on a package index. A project takes it at a release tag, with CMake `FetchContent` (CMake 3.20 or later, git, and network at configure time):

```cmake
include(FetchContent)
FetchContent_Declare(libretro_guest_probe
  GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git
  GIT_TAG        libretro-guest-probe@1.1.0
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
| SDL3 (3.2 or later) or SDL2 (2.0.18 or later) | the system or `-DCMAKE_PREFIX_PATH` | `find_package`, SDL3 first; `-DLGP_SDL=SDL3` or `-DLGP_SDL=SDL2` picks one. `-DLGP_REQUIRE_SDL=OFF` builds with neither, and a core that wants OpenGL is then refused one |

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
| `lgp_ram_window_init`, `lgp_ram_window_add_base`, `lgp_ram_offset`, `lgp_ram_parse_offset` | A guest address to an offset into the memory the core exposes. |
| `lgp_nm_lookup`, `lgp_read_words` | A symbol's address from an `nm` listing; a record of 32-bit words out of guest memory. |
| `lgp_convert_rgb24`, `lgp_joypad_state` | A software frame to RGB24; what a port driven by the frame hook answers the core. |
| `lgp_args_parse`, `lgp_setup_free`, `lgp_usage` | The command line, as a library function. |
| `lgp_run`, `lgp_config_defaults` | Loads the core and runs it, with the frame hook and capture when they are set. One run at a time per process: libretro's callbacks carry no user pointer, so the state is static. |

Everything added in 1.1.0 is off unless it is set, and the fields are at the end of `LgpConfig` and `LgpResult`, so a 1.0.0 caller builds and runs as before.

## Drive a game from its memory

A test that plays a game, rather than replaying a fixed script, reads the game's state every frame and decides the pad from it. The game publishes a record (a struct of 32-bit fields) at a symbol, the toolchain's `nm` gives its address, and the hook reads it:

```c
typedef struct { uint32_t version, screen; float x, y; } Record;   /* the game's own layout */

static void steer(void *user, const LgpFrameInfo *info, LgpFrameControl *control)
{
    const uint32_t *offset = (const uint32_t *)user;
    Record r;
    if (!info->ready || !lgp_read_words(info->memory, info->memory_size, *offset, &r, sizeof(r) / 4, 0)) {
        return;                                    /* not started, or the record is not there yet */
    }
    if (r.screen != 1) {                           /* a menu: tap Start every half second */
        control->joypad[0] = info->host_frame % 30 < 4 ? 1u << 3 : 0;
        return;
    }
    control->joypad[0] = (r.x < 0 ? 1u << 7 : 1u << 6) | 1u << 13;    /* right or left, and R2 */
    control->capture = 1;
    control->stop = info->total->captured_video >= 300;
}

LgpRamWindow ram;                     /* SH-4: RAM at 0x0c000000, mirrored at 0x8c000000 and 0xac000000 */
uint32_t address, offset;
lgp_ram_window_init(&ram, 0x01ffffffu);
lgp_ram_window_add_base(&ram, 0x0c000000u);
lgp_ram_window_add_base(&ram, 0x8c000000u);
lgp_ram_window_add_base(&ram, 0xac000000u);
lgp_nm_lookup("game.sym", "_game_record", &address);
lgp_ram_offset(&ram, address, &offset);

config.frame_hook = steer;
config.frame_user = &offset;
config.ports = 4;
config.video_path = "video.rgb";      /* or a named pipe FFmpeg reads */
config.audio_path = "audio.raw";
config.capture_max_frames = 300;
lgp_run(&config, &result);
/* result.counters: runs, presented, repeated, no_video, guest_stalls, guest_leaps, captured_video, captured_audio */
```

The buttons are libretro joypad bits (bit N is `RETRO_DEVICE_ID_JOYPAD_*` N: 3 Start, 6 left, 7 right, 13 R2), and a pressed L2 or R2 also reads as a fully pressed analog trigger, which is how cores such as Flycast read the triggers. With a replay set as well, the hook is handed the replay's buttons for the next frame and may change them, and `--record` writes what the core read. The hook runs before the not-started guard, so it sees every frame and decides for itself what to do before the guest is ready.

The raw streams go straight into FFmpeg, with the size and rates from the `AV` and `CAPTURE` log lines:

```sh
ffmpeg -f rawvideo -pixel_format rgb24 -video_size 640x480 -framerate 59.826628 -i video.rgb \
       -f s16le -ar 44100 -ac 2 -i audio.raw -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest run.mp4
```

### The closed-loop example

`examples/closed-loop/main.c` (built as `lgp-closed-loop`) is a complete driver on the hook, with everything game-specific on its command line. It finds the record by symbol, waits for a word that says the record is published, presses menu buttons in turn until a screen word reads the play value, then holds a fire button and weaves the player between two x bounds with left and right, reading the player's x (a float word) to know when to turn. It captures video and audio from the first frame of play and, at the end, checks that play was reached, that the player moved both ways, that the capture holds the frames asked for, and optionally that every frame in play presented one new frame (`--strict-pacing`) and that the audio matches the video at the core's rate within 5% (`--check-av-sync`).

```sh
lgp-closed-loop --symbols game.sym --record-symbol _game_record --tick-symbol _frame_counter \
    --ram-mask 0x01ffffff --ram-base 0x0c000000 --ram-base 0x8c000000 --ram-base 0xac000000 \
    --ready-word 0=2 --screen-word 1 --play-screen 1 --x-word 24 --x-min -4 --x-max 4 \
    --menu-buttons start,b --fire r2 --play-frames 400 \
    --video video.rgb --audio audio.raw --video-size 640x480 --capture-frames 300 --strict-pacing --check-av-sync \
    --option flycast_threaded_rendering=disabled --option flycast_vblank_pacing=enabled \
    flycast_libretro.dylib game.cdi ./isolated-system-folder
```

The package's tests run it against the fake core. Against a real core and game it was run with the Flycast libretro core (built from commit `3a5267a5e`), which renders through OpenGL, and a Dreamcast game built with KallistiOS that publishes such a record, on macOS 27 (Apple M6). The first Start press skipped the opening and the first B press launched the game, 36 frames after the guest started. Then:

| | SDL3 build | SDL2 build (sdl2-compat) |
| --- | --- | --- |
| frames played | 400, five turns, x from -4.45 to 4.45 | 200, three turns, x from -4.45 to 4.45 |
| frames in play: runs, new frames, repeats, runs with no frame | 399, 399, 0, 0 | 199, 199, 0, 0 |
| guest stalls and leaps in play | 20 and 20 | 10 and 10 |
| whole run: runs, new frames, repeats | 436, 435, 1 | 236, 235, 1 |
| captured | 300 frames at 640x480 (5.01 s at 59.83 fps), 221,775 audio frames at 44,100 Hz against 221,139 expected (0.3% over) | 120 frames, 89,349 audio frames against 88,456 expected |
| result | `CHECK PASS`, exit 0 | `CHECK PASS`, exit 0 |

The 300 frames, encoded with the FFmpeg line above, are the game in flight: the HUD, the player weaving across the river and firing, top row first, and the sound track at -24.6 dB mean volume. The core first asked for a Vulkan context, was told no, and then asked for OpenGL 3.2, which it got with an 853x853 framebuffer object.

## What was merged, and what was left out

The tool replaces two runners that each carried a fix the other lacked. This package has both: scripted playback and recording from one, answers to core options from the other; the guest-not-started guard and its delay from one, the frame bound as a setting for both (one ran 20000 frames, the other 3000).

Version 1.1.0 covers a third runner, one that played a game rather than replaying input: it read a record from the guest's memory every frame to steer up to four pilots, piped OpenGL frames to FFmpeg and raw audio to a file from a hidden context, and counted new against repeated frames to check the game's pacing. That runner's own environment handler, window, framebuffer and capture code are what the frame hook, the framebuffer object, the capture and the frame counts replace; its steering and its pass rules stay with the game, in a hook.

Left to the caller, because they know a game: what the guest's memory means (the old runners read a whole `Game` struct and tested its screen number; here the caller names the addresses to watch), and the sanity bounds on that struct. The old runners also drove a hard-coded sequence of Start and Cross presses when no replay was given; here that is `--tap` or a replay, so no game's button timing lives in the package.

## What runs where

| Part | Runs on | In |
| --- | --- | --- |
| the library, the tool, the closed-loop example and the tests (C, CMake) | Windows, macOS, Linux; CMake 3.20+, Ninja, a C compiler, SDL3 or SDL2 (or `-DLGP_REQUIRE_SDL=OFF`) | any shell |
| `build-and-test.sh` | macOS, Linux | Bash or zsh |
| `build-and-test.ps1` | Windows | PowerShell 5.1 or 7 |

The tests need no display, no GPU and no libretro core. They cover the option table, the input bridge, the memory probe against a fake memory map, the RAM window, `nm` lookup, word reads, frame conversion, the command line, and the host end to end against a fake core (`tests/fake_core.c`, a few functions with no emulation) for scripted input, option answers, the not-started guard, the frame bound, a stop rule, expectations, recording, a hardware-render request refused where no display exists, the frame hook driving two ports from guest memory (with the analog trigger), the frame counts, the guest's stalls and leaps, capture of video and audio with its frame limit, a frame of the wrong size, and the closed-loop example.

Tested at 1.1.0:

| System | Toolchain | Result |
| --- | --- | --- |
| macOS 27.0 (Apple M6) | Apple clang 21.0.0, CMake 4.5.1, Ninja, SDL3 3.4, `tools/check-c.sh`; the package again with `-DLGP_SDL=SDL2` (sdl2-compat) | pass |
| Flycast libretro core (commit `3a5267a5e`, OpenGL) with a Dreamcast game, macOS 27.0 (Apple M6) | the closed-loop example, SDL3 and SDL2 builds | pass: see [The closed-loop example](#the-closed-loop-example) |
| Linux arm64 (Debian trixie, in a container, no GPU) | LINUX_TOOLCHAIN | LINUX_RESULT |
| Windows 11 Pro (10.0.26300) | WINDOWS_TOOLCHAIN | WINDOWS_RESULT |

Tested at 1.0.0:

| System | Toolchain | Result |
| --- | --- | --- |
| Linux x86-64 (Ubuntu 24.04, in a container, no GPU) | gcc 13, SDL3 3.4, `check-c.sh`; also built with `-DLGP_REQUIRE_SDL=OFF`, and under AddressSanitizer and UBSan | pass |
| Windows 11 Pro (10.0.26300) | MSYS2 UCRT64 clang 22.1.8, SDL3 from MSYS2, `tools\check-c.ps1` in PowerShell 7 | pass. This run found that an address over 32 bits was accepted on Windows, where `unsigned long` is 32 bits; it is now refused there too |
| macOS 27.0 (Apple M6) | Apple clang 21.0.0, CMake 4.5.1, Ninja, `tools/check-c.sh` | pass |

## Layout

```
include/lgp/lgp.h      the whole API
src/options.c          the option table
src/input.c            the input bridge
src/probe.c            the memory probe
src/ram.c              guest addresses: the RAM window, nm lookup, word reads
src/frames.c           software frames to RGB24 for the capture
src/args.c             the command line
src/host.c             the libretro host (dlopen, environment, input, the frame hook, capture, counts, the frame loop)
src/gl_sdl.c, gl_none.c  the hidden OpenGL context and framebuffer object with SDL3 or SDL2, or a refusal without SDL
src/main.c             the libretro-guest-probe tool
cmake/LgpLibretroHeader.cmake   the pinned, hash-checked download of libretro.h
tests/                 options, input, probe, ram and args tests; the host test and fake_core.c
examples/consumer/     a project using the package
examples/closed-loop/  lgp-closed-loop, a game driven from its own memory with the frame hook
build-and-test.ps1     build and test, Windows PowerShell
build-and-test.sh      build and test, macOS and Linux Bash
CMakeLists.txt         library `lgp`, tool `libretro-guest-probe`, example `lgp-closed-loop`, tests
```

## Licence

MIT, see `LICENSE`. `libretro.h` is fetched, not included: see above.
