# bitmap-font

Small bitmap fonts as tables, with a callback to draw them. Three fonts (5x7, 8x8 and 3x5) are written once, in `data/bitmap-fonts.txt`; a generator turns that file into the C header the library compiles, into a standalone C array a project can vendor, and into the Python table that `uppercut-pixel-art-sheets` uses. A glyph is drawn by calling a function of yours once per run of set pixels, so the same table works with a framebuffer, a sprite batch or a GPU quad.

Nothing here allocates or depends on a graphics library. Version 1.1.0. Written in C99 with CMake; the generator is Python 3 (standard library only). MIT licence.

## Install

This is a C library, so it is not on a package index. A project takes it at a release tag, with CMake `FetchContent` (CMake 3.20 or later, git, and network at configure time):

```cmake
include(FetchContent)
FetchContent_Declare(bitmap_font
  GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git
  GIT_TAG        bitmap-font@1.1.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  packages/bitmap-font)
FetchContent_MakeAvailable(bitmap_font)

target_link_libraries(my-game PRIVATE bfont)
```

`examples/consumer/CMakeLists.txt` is that, complete, with a program that draws a line of text into a character grid. For an offline build set `-DFETCHCONTENT_SOURCE_DIR_BITMAP_FONT=<a checkout of game-kit>`. A project that vendors the folder instead uses `add_subdirectory(<path>/bitmap-font)`. The including project gets the CMake variable `BFONT_VERSION`.

## Use

```c
#include "bfont/bfont.h"

static void fill_rect(void *user, int x, int y, int w, int h)
{
    Framebuffer *fb = user;
    fb_fill(fb, x, y, w, h, fb->ink);
}

bfont_draw_text(&bfont_5x7, "Score 10", 8, 8, 2, fill_rect, &fb);   /* 2x scale; lowercase draws as capitals */
int width = bfont_text_width(&bfont_5x7, "Score 10", 2);
```

| Name | What it does |
| --- | --- |
| `bfont_5x7`, `bfont_8x8`, `bfont_3x5` | The fonts: `width`, `height`, `advance` (pixels from one glyph's left edge to the next), `first` (the code of table entry 0), `count`, and the `rows` and `present` tables. |
| `bfont_glyph(font, code)` | The glyph's `height` row bytes, or NULL when the font has none for that code. A lowercase letter gives its capital's rows. Pixel `x` of a row is set when bit `width - 1 - x` is, so the leftmost pixel is the high bit. |
| `bfont_pixel(font, code, x, y)` | 1 when that pixel is set, 0 when it is clear, the glyph is missing or the position is outside the glyph. |
| `bfont_draw_glyph(font, code, x, y, scale, fill, user)` | Calls `fill(user, x, y, w, h)` once for each horizontal run of set pixels in each row, scaled. Returns 1 when drawn, 0 when the glyph is missing or `scale` is below 1. |
| `bfont_draw_text(font, text, x, y, scale, fill, user)` | Draws a string left to right, a missing glyph leaving a gap. Returns the pixels advanced. Bytes are character codes: UTF-8 is not decoded. |
| `bfont_text_width(font, text, scale)` | The width in pixels, without the gap after the last glyph. |

Draw an outline or a shadow by drawing the string again, one pixel over, in another colour, before the main pass.

## The fonts

| Font | Glyphs | Advance | Notes |
| --- | --- | --- | --- |
| 5x7 | 67: ASCII 32 to 126 without lowercase and braces | 6 | Capitals, digits and punctuation. The table most game code wants. |
| 8x8 | 64: ASCII 32 to 95 | 8 | Capitals, digits and symbols, drawn about six pixels wide inside the cell so there is room for an outline. One source, so nothing to reconcile. |
| 3x5 | 67: the same codes as the 5x7 font | 4 | Capitals, digits and punctuation for text that must stay one font pixel to one screen pixel on a low-resolution display, such as a HUD on a 320x240 framebuffer. |

## One source, three outputs

`data/bitmap-fonts.txt` holds every glyph as a picture:

```
font 5x7 advance 6

glyph 0x41 ; A
.###.
#...#
#...#
#####
#...#
#...#
#...#
```

`tools/gen_bitmap_font.py` (Python 3, no dependencies) reads it:

```sh
# the header the library compiles in (include/bfont/bfont_tables.h)
python3 tools/gen_bitmap_font.py tables

# a standalone array for a project that vendors one table and not the library: [96][7], indexed by code - 32,
# listing only the glyphs the font has (the rest are zero)
python3 tools/gen_bitmap_font.py array --font 5x7 --name g_font --type u8 --first 32 --count 96 --out font.h
# ... or every index in order, in hexadecimal (an 8x8 table laid out like a hand-written one)
python3 tools/gen_bitmap_font.py array --font 8x8 --name FONT_GLYPHS --type u8 --layout dense --format hex --out font8.h

# the Python table (rows of "0" and "1") that uppercut-pixel-art-sheets imports
python3 tools/gen_bitmap_font.py python --font 5x7 --name FONT_5X7 --out _font5x7.py

# fail when a generated file in this repository is older than the source
python3 tools/gen_bitmap_font.py check
```

A standalone array has no lowercase folding: the caller folds `a` to `A` before indexing, as the library's `bfont_glyph` does. The generated files are committed, so using the library needs no Python; `ctest` runs `check` so a changed glyph without a regenerated header fails the build.

## Which glyph shapes were kept

The 5x7 table existed in four hand-drawn versions that disagree: a Dreamcast texture generator (67 glyphs, the largest), a PS2 prototype's header (43 glyphs, also copied into a console probe project unchanged), `uppercut-pixel-art-sheets` 0.1.0 (54 glyphs), and, in a different size, a 32X HUD (the 8x8 font) and three copies of a title-screen block font (3x5). Seventeen 5x7 glyphs differ between the three 5x7 tables. The rule used, so it can be overruled glyph by glyph:

1. Where two of the three tables draw a glyph the same way, keep that shape.
2. Where they do not (all three differ, or only two tables have the glyph and they disagree), keep the `uppercut-pixel-art-sheets` shape: it is already published, and these are the cases with no better argument.
3. The glyph set is the Dreamcast table's 67, the largest; the other tables are subsets.

The seventeen, as pictures (`-` means the table has no such glyph). Each glyph is shown in the three tables and then the shape kept:

```
        DC         PS2        sheets   kept
#  (no majority: the sheets shape)
        .#.#.        -        .#.#.  ->  .#.#.
        #####        -        .#.#.  ->  .#.#.
        .#.#.        -        #####  ->  #####
        .#.#.        -        .#.#.  ->  .#.#.
        #####        -        #####  ->  #####
        .#.#.        -        .#.#.  ->  .#.#.
        .....        -        .#.#.  ->  .#.#.
%  (no majority: the sheets shape)
        ##..#        -        ##..#  ->  ##..#
        ##.#.        -        ##.#.  ->  ##.#.
        ..#..        -        ...#.  ->  ...#.
        ..#..        -        ..#..  ->  ..#..
        .#.##        -        .#...  ->  .#...
        #..##        -        .#.##  ->  .#.##
        .....        -        #..##  ->  #..##
,  (no majority: the sheets shape)
        .....        -        .....  ->  .....
        .....        -        .....  ->  .....
        .....        -        .....  ->  .....
        .....        -        .....  ->  .....
        ..#..        -        .##..  ->  .##..
        ..#..        -        ..#..  ->  ..#..
        .#...        -        .#...  ->  .#...
.  (the shape two tables share)
        .....      .....      .....  ->  .....
        .....      .....      .....  ->  .....
        .....      .....      .....  ->  .....
        .....      .....      .....  ->  .....
        .....      .....      .....  ->  .....
        .##..      ..#..      .##..  ->  .##..
        .##..      ..#..      .##..  ->  .##..
/  (no majority: the sheets shape)
        ....#      ....#      ....#  ->  ....#
        ...#.      ....#      ...#.  ->  ...#.
        ..#..      ...#.      ...#.  ->  ...#.
        ..#..      ..#..      ..#..  ->  ..#..
        .#...      .#...      .#...  ->  .#...
        #....      #....      .#...  ->  .#...
        .....      #....      #....  ->  #....
2  (the shape two tables share)
        .###.      .###.      .###.  ->  .###.
        #...#      #...#      #...#  ->  #...#
        ....#      ....#      ....#  ->  ....#
        ..##.      ...#.      ..##.  ->  ..##.
        .#...      ..#..      .#...  ->  .#...
        #....      .#...      #....  ->  #....
        #####      #####      #####  ->  #####
5  (the shape two tables share)
        #####      #####      #####  ->  #####
        #....      #....      #....  ->  #....
        ####.      #....      ####.  ->  ####.
        ....#      ####.      ....#  ->  ....#
        ....#      ....#      ....#  ->  ....#
        #...#      ....#      #...#  ->  #...#
        .###.      ####.      .###.  ->  .###.
6  (the shape two tables share)
        .###.      .###.      ..##.  ->  .###.
        #....      #....      .#...  ->  #....
        #....      #....      #....  ->  #....
        ####.      ####.      ####.  ->  ####.
        #...#      #...#      #...#  ->  #...#
        #...#      #...#      #...#  ->  #...#
        .###.      .###.      .###.  ->  .###.
9  (the shape two tables share)
        .###.      .###.      .###.  ->  .###.
        #...#      #...#      #...#  ->  #...#
        #...#      #...#      #...#  ->  #...#
        .####      .####      .####  ->  .####
        ....#      ....#      ....#  ->  ....#
        ....#      ....#      ...#.  ->  ....#
        .###.      .###.      .##..  ->  .###.
:  (the shape two tables share)
        .....      .....      .....  ->  .....
        .##..      ..#..      .##..  ->  .##..
        .##..      ..#..      .##..  ->  .##..
        .....      .....      .....  ->  .....
        .##..      ..#..      .##..  ->  .##..
        .##..      ..#..      .##..  ->  .##..
        .....      .....      .....  ->  .....
?  (no majority: the sheets shape)
        .###.        -        .###.  ->  .###.
        #...#        -        #...#  ->  #...#
        ...#.        -        ....#  ->  ....#
        ..#..        -        ..##.  ->  ..##.
        ..#..        -        ..#..  ->  ..#..
        .....        -        .....  ->  .....
        ..#..        -        ..#..  ->  ..#..
C  (the shape two tables share)
        .###.      .####      .###.  ->  .###.
        #...#      #....      #...#  ->  #...#
        #....      #....      #....  ->  #....
        #....      #....      #....  ->  #....
        #....      #....      #....  ->  #....
        #...#      #....      #...#  ->  #...#
        .###.      .####      .###.  ->  .###.
D  (the shape two tables share)
        ####.      ####.      ###..  ->  ####.
        #...#      #...#      #..#.  ->  #...#
        #...#      #...#      #...#  ->  #...#
        #...#      #...#      #...#  ->  #...#
        #...#      #...#      #...#  ->  #...#
        #...#      #...#      #..#.  ->  #...#
        ####.      ####.      ###..  ->  ####.
G  (no majority: the sheets shape)
        .###.      .####      .###.  ->  .###.
        #...#      #....      #...#  ->  #...#
        #....      #....      #....  ->  #....
        #.###      #.###      #.###  ->  #.###
        #...#      #...#      #...#  ->  #...#
        #...#      #...#      #...#  ->  #...#
        .###.      .####      .####  ->  .####
I  (the shape two tables share)
        .###.      ###..      .###.  ->  .###.
        ..#..      .#...      ..#..  ->  ..#..
        ..#..      .#...      ..#..  ->  ..#..
        ..#..      .#...      ..#..  ->  ..#..
        ..#..      .#...      ..#..  ->  ..#..
        ..#..      .#...      ..#..  ->  ..#..
        .###.      ###..      .###.  ->  .###.
J  (the shape two tables share)
        ..###      ..###      ..###  ->  ..###
        ...#.      ...#.      ...#.  ->  ...#.
        ...#.      ...#.      ...#.  ->  ...#.
        ...#.      ...#.      ...#.  ->  ...#.
        #..#.      #..#.      ...#.  ->  #..#.
        #..#.      #..#.      #..#.  ->  #..#.
        .##..      .##..      .##..  ->  .##..
W  (the shape two tables share)
        #...#      #...#      #...#  ->  #...#
        #...#      #...#      #...#  ->  #...#
        #...#      #...#      #...#  ->  #...#
        #.#.#      #.#.#      #.#.#  ->  #.#.#
        #.#.#      #.#.#      #.#.#  ->  #.#.#
        ##.##      #.#.#      #.#.#  ->  #.#.#
        #...#      .#.#.      .#.#.  ->  .#.#.
```

Who changes: the Dreamcast generator redraws 7 glyphs (`#`, `%`, `,`, `/`, `?`, `G`, `W`); the PS2 table redraws 8 (`C`, `G`, `I`, `2`, `5`, `.`, `:`, `/`), the biggest being `I`, which was three columns wide and left-aligned in its cell, and the stops, which are now two pixels wide like the comma; `uppercut-pixel-art-sheets` redraws 4 (`D`, `J`, `6`, `9`) and gains 13 glyphs it lacked. To keep a different shape for a glyph, edit its picture in `data/bitmap-fonts.txt` and run `python3 tools/gen_bitmap_font.py tables` (and `python` for the sheets table, see below); `check` lists what is stale.

In the 3x5 set, the Dreamcast title letters and the title letters of three other consoles' title screens agree on every letter they share except `P`: the three screens draw it with an open bowl (`##.`, `#.#`, `##.`, `#..`, `#..`) and the Dreamcast version with a closed one (`###`, `#.#`, `###`, `#..`, `#..`). The open bowl is kept, as the majority. The 3x5 `X` comes from the Dreamcast version alone.

Version 1.1.0 completed the 3x5 font with the other 55 codes the 5x7 font has, so it can set whole lines of HUD text. The twelve title letters keep their 1.0.0 shapes. The new glyphs were drawn for this package. Three columns leave little room, so a few letters differ from their neighbours by two pixels rather than one:

```
M      N      W      O      0      Q      %
###    ##.    #.#    .#.    ###    .#.    #.#
###    #.#    #.#    #.#    #.#    #.#    ..#
#.#    #.#    #.#    #.#    #.#    #.#    .#.
#.#    #.#    ###    #.#    #.#    ##.    #..
#.#    #.#    #.#    .#.    ###    .##    #.#
```

`M` fills its top two rows and `N` is open at the top right, so neither reads as `H`; `W` puts its bar one row below the bar of `H`; `O` is round and `0` is square.

The 8x8 table is the 32X HUD's, unchanged: no other 8x8 table exists to compare it with.

## Python side

`uppercut-pixel-art-sheets` imports `FONT_5X7` from `_font5x7.py`, a generated copy of this package's 5x7 font, so the Python and C sides cannot drift apart: a test in that package rebuilds the table from `data/bitmap-fonts.txt` and compares, and `check` here refuses a stale file. The package is self-contained when published: it carries the generated table, not a dependency on this folder.

## What runs where

| Part | Runs on | In |
| --- | --- | --- |
| the library and its test (C99, CMake) | Windows, macOS, Linux; CMake 3.20+, Ninja and a C compiler | any shell: it is a program |
| the generator and its test (Python 3, tested with 3.12) | wherever Python 3 is; the ctest entries are skipped without it | any shell |
| `build-and-test.ps1` | Windows | PowerShell 5.1 or 7 |
| `build-and-test.sh` | macOS, Linux | Bash or zsh |

Tested:

| Version | System | Toolchain | Result |
| --- | --- | --- | --- |
| 1.1.0 | Linux arm64 (Ubuntu 22.04, in a container) | gcc 11.4, CMake 3.22, `build-and-test.sh`; also `-std=c99 -pedantic` under AddressSanitizer and UBSan | pass |
| 1.1.0 | macOS 27.0 (Apple M6) | Apple clang 21.0.0, CMake 4.5.1, Ninja, `build-and-test.sh` in the repository and in the packed source, and the example consumer | pass |
| 1.0.0 | Linux x86-64 (Ubuntu 24.04, in a container) | gcc 13, `./tools/check-c.sh` from the repository root; also `-std=c99 -pedantic` under AddressSanitizer and UBSan | pass |
| 1.0.0 | Windows 11 Pro (10.0.26300) | MSYS2 UCRT64 clang 22.1.8, CMake 4.4.3, Ninja 1.13.2, `tools\check-c.ps1` in PowerShell 7 | pass |
| 1.0.0 | macOS 27.0 (Apple M6) | Apple clang 21.0.0, CMake 4.5.1, Ninja, `tools/check-c.sh` | pass |

Build and run the tests, from this folder:

```powershell
.\build-and-test.ps1                       # Windows, PowerShell
```
```bash
./build-and-test.sh                        # macOS, Linux, Bash
```

## Layout

```
include/bfont/bfont.h          the API
include/bfont/bfont_tables.h   generated: the tables the library compiles in
src/bfont.c                    the library
data/bitmap-fonts.txt          the one source of the glyphs
tools/gen_bitmap_font.py       the generator
tests/bfont_test.c             tables, lookup, pixels, runs, text
tests/check_generator.py              the generator: parser errors, outputs, freshness
examples/consumer/             a project using the package
build-and-test.ps1             build and test, Windows PowerShell
build-and-test.sh              build and test, macOS and Linux Bash
CMakeLists.txt                 library `bfont`, tests `bfont-test`, `bfont-generator-test`, `bfont-generated-current`
```

## Licence

MIT, see `LICENSE`.
