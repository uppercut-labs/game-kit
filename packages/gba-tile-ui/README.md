# gba-tile-ui

A software layer between a Game Boy Advance game's screen code and the console's video memory. The game draws tiles,
text, numbers and sprites into shadow copies in ordinary memory: up to four 32 x 32 tile maps and the 128-entry sprite
table (OAM). Each change that alters a word sets a dirty flag, and once a frame the game copies what is dirty to VRAM
and OAM. Because the drawing never touches the console's registers, the screen code builds and runs on a host, and its
output can be read back and tested with `ctest`, without an emulator or devkitARM.

Several GBA games kept writing the same helpers (put a tile, fill, a glyph table, text, centred text, a number, hide
the sprites, set a sprite) with the same conventions: a 30 x 20 visible area on a 32 x 32 map, one glyph table, one
fallback tile for a character the font lacks. This is one tested copy.

Version 0.1.0. Written in C (C99) with CMake for the host build. MIT licence.

## What is in it, and what is not

| Part | Needs | In |
| --- | --- | --- |
| **Core**: `include/gba_tile_ui/gba_tile_ui.h`, `src/gba_tile_ui.c` | a C99 compiler and the C library (`string.h`) | everything above |
| **libgba glue** (optional): `include/gba_tile_ui/gba_tile_ui_gba.h` | libgba from devkitARM | `gtu_present_gba`, the copy of what changed to VRAM and OAM |

The core does not include `gba.h` and does not know a register. The glue is one short inline function; a game that
does its own DMA, or uses another library, can ignore it and call `gtu_take_map_dirty` and `gtu_take_oam_dirty`.

Not in it: loading tile art or palettes, the background and display registers, scrolling, affine sprites, 256-colour
sprites, non-square sprite shapes, windows and blending. Those stay in the game's own video code.

## Install

This is a C library, so it is not on a package index. For a host build (tests), a project takes it at a release tag
with CMake `FetchContent` (CMake 3.20 or later, git, and network at configure time):

```cmake
include(FetchContent)
FetchContent_Declare(gba_tile_ui
  GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git
  GIT_TAG        gba-tile-ui@0.1.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  packages/gba-tile-ui)
FetchContent_MakeAvailable(gba_tile_ui)

target_link_libraries(my-screens PUBLIC gtu)
```

`examples/consumer/CMakeLists.txt` is that, complete, with a made-up title screen and its test. For an offline build set
`-DFETCHCONTENT_SOURCE_DIR_GBA_TILE_UI=<a checkout of game-kit>`. A project that vendors the folder uses
`add_subdirectory(<path>/gba-tile-ui)`. The including project gets the CMake variable `GTU_VERSION`.

**On the console**, a devkitARM project that builds with its own Makefile takes two things from the folder: add
`src/gba_tile_ui.c` to its sources and `include/` to its include path (`INCLUDES` in the devkitPro template). The same
`include/` holds the glue header. Define `GTU_MAP_COUNT` the same way in every file if the game needs fewer than four
maps (see below).

## Use

```c
#include "gba_tile_ui/gba_tile_ui.h"

static GtuUi ui;                      /* in EWRAM if the game wants its IWRAM: EWRAM_BSS static GtuUi ui; */

gtu_init(&ui);
gtu_set_font(&ui, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:.,!?-/+=<>'#%&() ", FONT_BASE_TILE, FONT_BASE_TILE + 53);
ui.skip_spaces = true;                /* text leaves the tile behind a space as it is */

gtu_clear_map(&ui, 0, BACKGROUND_TILE, 0);
gtu_center(&ui, 0, 3, "MY GAME", 1);                  /* map 0, row 3, palette bank 1 */
gtu_number(&ui, 0, 8, 17, score, 5, 1);               /* right-aligned in 5 columns, like "%*d" */
gtu_set_sprite(&ui, 0, x, y, CURSOR_TILE, 0, GTU_SPRITE_16, 0, 0);

/* once a frame, in the vertical blank, with libgba: */
#include "gba_tile_ui/gba_tile_ui_gba.h"
gtu_present_gba(&ui, 28);             /* map n goes to screen block 28 + n */
```

| Function | What it does |
| --- | --- |
| `gtu_init` | Empty maps, every sprite hidden, default areas, no font, everything dirty. |
| `gtu_set_font(glyphs, base, unknown)` | A character's tile is `base` plus its place in `glyphs`; a character not listed draws `unknown`. |
| `gtu_set_clip(columns, rows)` | Tile writes at or past these are dropped. All 32 x 32 until set; a game that wants a 30 x 20 screen sets that. |
| `gtu_set_text_area(columns, rows)` | Text is held to this (30 x 20 until set), and `gtu_center` centres in its width. |
| `gtu_clear_map(bg, tile, palette)` | Every cell of a map, the whole 32 x 32 (not held to the clip). |
| `gtu_set_tile(bg, x, y, tile, palette, flags)`, `gtu_set_entry`, `gtu_get_entry` | One cell; flags are `GTU_HFLIP` and `GTU_VFLIP`. `gtu_entry` makes the entry word. |
| `gtu_fill(bg, x, y, w, h, tile, palette)` | A rectangle, clipped. |
| `gtu_load_map(bg, entries, count)` | A prebuilt map (such as one exported by grit) copied in. |
| `gtu_text(bg, x, y, text, palette)` | Text from a column and row. `\n` goes back to `x` and down a row. Columns past the text area are dropped, as are columns left of the screen. |
| `gtu_center(bg, y, text, palette)` | `gtu_text` at `(text_columns - length) / 2`. |
| `gtu_number(bg, x, y, value, width, palette)` | A decimal number right-aligned in `width` characters (`"%*d"`), negative numbers included, without the C library's printf. |
| `gtu_clear_text_rect(bg, x, y, w, h, palette)` | The space glyph over a rectangle. |
| `gtu_set_sprite(index, x, y, tile, palette, size, priority, flags)` | A square 16-colour sprite (`GTU_SPRITE_8` to `GTU_SPRITE_64`). `x` and `y` wrap as the console's 9-bit and 8-bit fields do. |
| `gtu_hide_sprite`, `gtu_hide_all_sprites` | Sets the OAM disable bit (`0x0200`) and zeroes the rest of the entry. |
| `gtu_take_map_dirty(bg)`, `gtu_take_oam_dirty` | True if that layer changed since the last take, and clears the flag. |
| `gtu_mark_all_dirty` | Everything is copied at the next present (after the game loads new tiles or palettes over a map). |

A call with a map or sprite number out of range does nothing.

### Maps in memory

A `GtuUi` holds `GTU_MAP_COUNT` maps of 2 KB (4 by default) and the 1 KB OAM copy. A game with one text layer defines
`GTU_MAP_COUNT` as 1 before including the header (in every file that includes it, or with `-DGTU_MAP_COUNT=1` for the
build) and saves 6 KB.

### What dirty means

A flag is set when a write changes a word, so writing the value that is already there costs nothing. It is not a
promise that the final picture differs from the last frame: a screen that clears a map and draws it again flags the map
every frame, as copying it every frame did before. A game that draws only what changed copies less.

## Differences between the helpers it replaces

The three games' helpers differed in small ways; this package picks one behaviour and makes the others a setting:

- **Hidden sprites** use the disable bit (`ATTR0_DISABLED`, `0x0200`). One game used a `y` of 160 instead; both leave the
  sprite off screen, but the words differ.
- **Spaces**: `skip_spaces` writes nothing for a space (two of the games); off, a space draws its glyph (the third).
- **A missing character**: the unknown tile is whatever the game passes to `gtu_set_font`: one game's last glyph, another's
  blank.
- **Clip**: `gtu_set_clip(30, 20)` drops tile writes outside the screen (two games); the default 32 x 32 does not (the third).

## What runs where

| Part | Runs on | In |
| --- | --- | --- |
| the core and its test (C, CMake) | Windows, macOS, Linux; CMake 3.20+, Ninja and a C compiler | any shell: it is a program |
| `build-and-test.ps1` | Windows | PowerShell 5.1 or 7 |
| `build-and-test.sh` | macOS, Linux | Bash or zsh |

Tested at 0.1.0:

| System | Toolchain | Result |
| --- | --- | --- |
| Linux x86-64 (Ubuntu, in a container) | gcc, `-Wall -Wextra -pedantic`; the library test (the glue is compiled against a made-up `gba.h` in `tests/fake_gba`); the example consumer | pass |
| devkitARM and libgba, real console | not run: no devkitARM was available. The glue has only been compiled against the test's fake `gba.h`; `dmaCopy`, `MAP_BASE_ADR` and `OAM` are used as libgba defines them | waits for a maintainer machine |
| Windows, macOS | not run yet | waits for a maintainer machine |

Build and run the test, from this folder:

```powershell
.\build-and-test.ps1                       # Windows, PowerShell
```
```bash
./build-and-test.sh                        # macOS, Linux, Bash
```

## Layout

```
include/gba_tile_ui/gba_tile_ui.h        the whole core API
include/gba_tile_ui/gba_tile_ui_gba.h    the optional libgba glue (gtu_present_gba)
src/gba_tile_ui.c                        the core
tests/gtu_test.c                         maps, text, numbers, sprites, dirty flags, the glue
tests/fake_gba/gba.h                     a stand-in for the few libgba names the glue uses
examples/consumer/                       a made-up title screen and its host test
build-and-test.ps1                       build and test, Windows PowerShell
build-and-test.sh                        build and test, macOS and Linux Bash
CMakeLists.txt                           library `gtu`, test `gtu-test`
```
