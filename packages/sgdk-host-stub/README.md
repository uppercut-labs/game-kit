# sgdk-host-stub

Builds a Mega Drive (Genesis) game's logic with an ordinary host compiler and runs it under `ctest`, with no emulator,
no cross-compiler and no SGDK install. The game does `#include <genesis.h>` as it does in the ROM build; with this
package's include folder first on the include path it gets a stub instead, where every call records what the game
asked for. A test then resets the recorder, runs frames of game code, and reads back the tiles, palette, sprites, sound
calls, PSG bytes, save RAM and pad reads.

It is a **call recorder, not an emulator**. Nothing runs 68000 or Z80 code, times a DMA, draws a frame or plays a
sample. A host pass says the game's own code made the calls its author meant and none that SGDK or the hardware would
reject; it does not say the ROM builds, boots, looks right or sounds right. Keep an emulator check for that.

**No SGDK code is in this package.** `genesis.h` and everything else here are our own declarations of the call shapes
the games use (a function name, its argument types, the button bit values), written for a stub. Nothing is copied from
SGDK's source or headers, and the SGDK headers are not needed to build or test.

Version 0.1.0. Written in C (C99) with CMake. No dependencies beyond the C library. MIT licence.

## Install

This is a C library, so it is not on a package index. A project takes it at a release tag, with CMake `FetchContent`
(CMake 3.20 or later, git, and network at configure time):

```cmake
include(FetchContent)
FetchContent_Declare(sgdk_host_stub
  GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git
  GIT_TAG        sgdk-host-stub@0.1.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  packages/sgdk-host-stub)
FetchContent_MakeAvailable(sgdk_host_stub)

add_library(my-game STATIC game.c)          # the game's logic, the files that include <genesis.h>
target_link_libraries(my-game PUBLIC sgdkstub)
```

`examples/consumer/CMakeLists.txt` is that, complete, with a small made-up game and its test. For an offline build set
`-DFETCHCONTENT_SOURCE_DIR_SGDK_HOST_STUB=<a checkout of game-kit>`. A project that vendors the folder uses
`add_subdirectory(<path>/sgdk-host-stub)`. A project with no CMake (a script that calls the compiler) needs two things
from the folder: `include/` on the include path, and `src/sgdkstub.c` among the sources. The including project gets the
CMake variable `SGDKSTUB_VERSION`.

## Use

```c
#include <sgdkstub/sgdkstub.h>      /* includes <genesis.h>, the stub's */
#include "game.h"

sgs_reset();                                   /* power-on state; also the state at program start */
sgs_opt.write_limit_w = 40;                    /* optional checks, once (see "Checks") */
sgs_opt.write_limit_h = 28;

const u16 pad[] = {0, BUTTON_RIGHT, BUTTON_A};
sgs_joy_script(JOY_1, pad, 3);                 /* the next three JOY_readJoypad(JOY_1) calls */
game_init();
game_frame(); game_frame(); game_frame();

assert(sgs_tile_at(BG_A, 1, 5) == expected_attr);
assert(sgs_row_contains(BG_A, 1, "SCORE 1"));
assert(sgs.audio_count == 1 && sgs.audio[0].priority == 4);
assert(sgs.violations == 0);
```

State is the global `sgs` (type `SgsState`, a plain struct a test reads directly) and options are `sgs_opt`.

| What | Where |
| --- | --- |
| Plane cells and text | `sgs.tiles[plane][y][x]` (the attribute word), `sgs.text[plane][y][x]`, `sgs_tile_at`, `sgs_row_text`, `sgs_row_contains`, `sgs.tile_writes`, `sgs.max_tile_x/y`, `sgs.clears`, `sgs.hscroll`, `sgs.vscroll`, `sgs.plane_w/h`, `sgs.screen_width` |
| Loaded tile data | `sgs.tile_pixels[index][8]`, `sgs.tile_loaded[index]`, `sgs_tile_has_ink(attr)` |
| Palette | `sgs.palette[64]`, `sgs.palette_writes`, `sgs.background_index` |
| Sprites | `sgs.sprites[]`, `sgs.sprite_count`, `sgs.sprite_updates` |
| Pads | `sgs_joy_set(port, state)` for a held state, `sgs_joy_script(port, states, n)` for one state per read, then the held one |
| Sound (XGM2) | `sgs.audio[]` (a call log, plays and stops), `sgs.audio_count`, `sgs.channel_sample[]`, `sgs.channel_loop[]`, `sgs.z80_driver`, `sgs_clear_audio_calls()` |
| PSG | `sgs.psg_log[]` (every byte), `sgs_psg_events` (decoded: channel, volume or tone, value), `sgs_psg_has_tone`, `sgs_psg_has_envelope`, `sgs_psg_tone_changed` |
| Save RAM | `sgs.sram[]`, `sgs.sram_mode` (0 off, 1 read only, 2 read and write) |
| Debug and timing | `sgs.alerts[]` (KDebug_Alert text), `sgs.vblanks` |
| Random numbers | `random()` and `setRandomSeed()`: a fixed 16-bit generator, the same numbers on every system |

### What the stub declares

Video (`VDP_loadTileSet`, `VDP_setTileMapXY`, `VDP_fillTileMapRect`, `VDP_clearPlane`, `VDP_setPlaneSize`, scrolls,
`VDP_drawText` and the text settings, `VDP_setBackgroundColor`, screen width), `PAL_setPalette` and `PAL_setColor`,
sprites (`SPR_init`, `SPR_addSprite`, `SPR_setPosition`, `SPR_setAnim`, `SPR_setFrame`, `SPR_setVisibility`,
`SPR_update`), `JOY_init` and `JOY_readJoypad`, `Z80_loadDriver`, `XGM2_playPCMEx` and `XGM2_stopPCM`, the PSG calls,
the SRAM byte calls, `SYS_doVBlankProcess`, `KDebug_Alert`; the types (`u8` to `s32`, `TileSet`, `Palette`,
`SpriteDefinition`, `Sprite`); the constants (`BUTTON_*` with the Mega Drive bit values, `PAL0` to `PAL3`,
`TILE_ATTR`, `TILE_ATTR_FULL`, `TILE_USER_INDEX` fixed at 16, `PSG_*`) and the `RGB8_8_8_TO_VDPCOLOR` and
`RGB24_TO_VDPCOLOR` rounding. These are the calls two games' host tests needed. A call that is missing is a build or link error
and takes a few lines in `genesis.h` and `src/sgdkstub.c`; it is not a hidden no-op.

Two details to know:

- `BUTTON_B` is `0x0010`, `BUTTON_C` `0x0020` and `BUTTON_A` `0x0040`, as on the Mega Drive pad. A stub that gets these
  wrong lets a game's tests pass while its buttons are swapped on a console.
- `SYS_doVBlankProcess` returns `TRUE`, so a caller that tests the result and a caller that ignores it both build.
  `random` is a macro for `sgs_random` so it does not clash with the C library's `random`.

## Checks

A call that the hardware or SGDK would misbehave on is a **violation**. By default a violation prints
`sgdkstub: <what>` to stderr and aborts, so a test stops at the call that broke the rule and the message names it.

Always checked:

- a tile or text write outside the plane (64 x 32 cells until `VDP_setPlaneSize` says otherwise), a plane that does not
  exist, a tile set that runs past the 2048 tile slots;
- a palette or colour index past 63, a colour with bits outside `0x0eee`, a text palette past 3;
- a sprite list past 80 sprites, `SPR_setAnim` or `SPR_setFrame` past the counts a `SpriteDefinition` gives (when it
  gives any);
- an XGM2 sample that is missing or empty, a channel outside -1 to 2 (stop: 0 to 2), a priority past 15, more than 64
  sound calls since `sgs_clear_audio_calls()`;
- PSG arguments out of range; SRAM read or written while it is off, written while it is read only, or past the 64 KB
  the stub holds;
- a pad port other than `JOY_1` and `JOY_2`.

Options in `sgs_opt` (all off until a test sets them; `sgs_reset()` leaves them alone):

| Option | A violation is |
| --- | --- |
| `write_limit_w`, `write_limit_h` | a tile or text write at or past those cells, such as `40`, `28` for the screen |
| `sprite_bound_w`, `sprite_bound_h` | a sprite added or moved outside `0..w-1`, `0..h-1` |
| `require_loaded_tiles` | `VDP_setTileMapXY` naming a tile (other than 0) that no `VDP_loadTileSet` loaded |
| `forbid_tile_overlap` | `VDP_loadTileSet` over a tile that is already loaded |
| `alert_stream` | (not a check) `KDebug_Alert` also prints `ROM: <text>` to this stream |

`sgs_set_fail_handler(fn)` has the violation text go to `fn` instead of aborting, and `sgs_record_violations_only()`
counts them in `sgs.violations` and keeps the last text for `sgs_last_violation()`. A rejected call does not change
the recorded state.

A `SpriteDefinition` here has `numAnimation` and `numFrame` fields of its own (frames per animation); a game's test
data fills them in to have `SPR_setAnim` and `SPR_setFrame` checked, and leaves them 0 to have them unchecked.

## What it does not do

- It does not run the ROM's code, emulate the VDP (no DMA timing, no sprite limits per line, no VRAM layout) or the
  sound chips. A pass is not an emulator pass.
- It does not convert PNG art. A palette made by the resource compiler is a separate conversion from the runtime
  `RGB8_8_8_TO_VDPCOLOR`; the stub models only the runtime rounding.
- It does not include `rom_head.c`, the boot code or any part of SGDK. A game's ROM header stays out of the host build.

## What runs where

| Part | Runs on | In |
| --- | --- | --- |
| the library and its tests (C, CMake) | Windows, macOS, Linux; CMake 3.20+, Ninja and a C compiler | any shell: it is a program |
| `build-and-test.ps1` | Windows | PowerShell 5.1 or 7 |
| `build-and-test.sh` | macOS, Linux | Bash or zsh |

Tested at 0.1.0:

| System | Toolchain | Result |
| --- | --- | --- |
| Linux x86-64 (Ubuntu, in a container) | gcc, `-Wall -Wextra -pedantic`; the library test, its power-on test and its abort test; the example consumer; the library test again under AddressSanitizer and UBSan | pass |
| Windows, macOS | not run yet | waits for a maintainer machine |

Build and run the tests, from this folder:

```powershell
.\build-and-test.ps1                       # Windows, PowerShell
```
```bash
./build-and-test.sh                        # macOS, Linux, Bash
```

## Layout

```
include/genesis.h              the stub of SGDK's genesis.h: types, constants and the call declarations
include/sgdkstub/sgdkstub.h    the recorder's state, options and read-back helpers
src/sgdkstub.c                 the calls and the checks
tests/sgdkstub_test.c          every area, with made-up tiles, palettes and sounds
examples/consumer/             a made-up game and its host test, built against the package
build-and-test.ps1             build and test, Windows PowerShell
build-and-test.sh              build and test, macOS and Linux Bash
CMakeLists.txt                 library `sgdkstub`, test `sgdkstub-test`
```
