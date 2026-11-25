# minimal-png

A PNG writer for game ports and tools that has no zlib. A frame dump, a screenshot key or a debug atlas needs a
file any viewer opens, not a small one, so the pixel data goes out as stored (uncompressed) deflate blocks. The file is
about the size of the raw pixels plus a few bytes per 64 KiB.

```c
#include "mpng/mpng.h"

MpngImage frame = { rgba_pixels, 640, 480, 0, MPNG_RGBA };   /* stride 0: rows are packed */
int rc = mpng_write_file("frame.png", &frame, MPNG_RGB);      /* alpha dropped */
if (rc != MPNG_OK) fprintf(stderr, "%s\n", mpng_error_text(rc));
```

Nothing in this package knows a game. It has no dependencies beyond the C library (no zlib, no SDL, no stb), and it
shares no state between calls, so two threads can write two files at once.

Version 0.1.0. Written in C (C11) with CMake. MIT licence.

## Install

This is a C library, so it is not on a package index. A project takes it at a release tag, with CMake `FetchContent`
(CMake 3.20 or later, git, and network at configure time):

```cmake
include(FetchContent)
FetchContent_Declare(minimal_png
  GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git
  GIT_TAG        minimal-png@0.1.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  packages/minimal-png)
FetchContent_MakeAvailable(minimal_png)

target_link_libraries(my-port PRIVATE mpng)
```

`examples/consumer/CMakeLists.txt` is that, complete, with a program that writes a gradient and reads the signature
back. For an offline build set `-DFETCHCONTENT_SOURCE_DIR_MINIMAL_PNG=<a checkout of game-kit>`. A project that
vendors the folder instead uses `add_subdirectory(<path>/minimal-png)`, and a build that is not CMake needs only
`src/mpng.c` with `include/` on the include path. The including project gets the CMake variable `MPNG_VERSION`.

## Use

| Function | What it does |
| --- | --- |
| `mpng_encode(&img, out, &png, &len)` | Encodes into a new buffer. Release it with `mpng_free`. On a failure the outputs are left as they were. |
| `mpng_write_file(path, &img, out)` | Encodes, then opens `path`, writes and closes it. A UTF-8 path works on Windows. A file that could not be written whole is removed, and an image that is refused creates no file. |
| `mpng_write_stream(f, &img, out)` | Encodes and writes to a stream the caller opened and keeps. |
| `mpng_encoded_size(width, height, out)` | The length of the file, or 0 when the image would be refused. For a caller that places the file in its own buffer. |
| `mpng_error_text(rc)`, `mpng_crc32(crc, data, n)` | A sentence for an error code; the CRC-32 PNG uses. |

`MpngImage` is `{ pixels, width, height, stride, layout }`. The layout is `MPNG_RGB` or `MPNG_RGBA`, 8 bits a channel.
`stride` is the bytes from one row to the next, or 0 for packed rows (a texture read back with a padded pitch passes it).
`out` is the layout of the file, and may differ from the input's: RGBA to RGB drops the alpha, and RGB to RGBA makes every
pixel opaque.

Errors are `MPNG_ERR_ARGS` (a NULL pointer, a bad layout, a zero width or height, a stride shorter than a row),
`MPNG_ERR_SIZE` (bigger than a PNG allows, or than this system can address: the size is worked out before any pixel is
read), `MPNG_ERR_MEMORY` and `MPNG_ERR_IO`.

## What the file holds

A PNG signature, `IHDR` (8 bits a channel, colour type 2 or 6, no interlace), one `IDAT`, `IEND`. The `IDAT` is a zlib
stream with the fixed header `78 01`, stored blocks of at most 65535 bytes and the Adler-32. Every row uses filter 0. One
`IDAT` caps the image at about 2 GB of pixel data, and larger images are refused with `MPNG_ERR_SIZE`. Writing is done from
one buffer: the pixels are copied once into their place between the block headers.

The Python package [`uppercut-port-proof`](../uppercut-port-proof) writes the same bytes from Python with
`write_png(..., compress=False)`, so a Python tool and a C port can be checked against each other.

## What runs where

| Part | Runs on | In |
| --- | --- | --- |
| the library and its tests (C, CMake) | Windows, macOS, Linux; CMake 3.20+, Ninja and a C compiler | any shell: it is a program |
| `tests/check_with_zlib.py` | wherever Python 3 is found at configure time; a test is added only then | `ctest` |
| `build-and-test.ps1` | Windows | PowerShell 5.1 or 7 |
| `build-and-test.sh` | macOS, Linux | Bash or zsh |

Tested at 0.1.0:

| System | Toolchain | Result |
| --- | --- | --- |
| Linux x86-64 (in a container) | gcc, `build-and-test.sh` in Bash; also under AddressSanitizer and UBSan | pass |
| Windows 11, macOS | not run yet: the Windows file calls (`_wfopen`, `_wremove`) are untested | waits for a maintainer machine |

The first test decodes every file it writes with a reader of its own for stored blocks, checking each chunk's length and
CRC, the zlib header and the Adler-32, and compares every pixel; it includes the block boundaries (65535 bytes exactly, one
over) and a 2 by 2 file pinned byte for byte, made by a separate writer. The second hands the files to Python's zlib and
`struct`, which are independent of this code.

Build and run the tests, from this folder:

```powershell
.\build-and-test.ps1                       # Windows, PowerShell
```
```bash
./build-and-test.sh                        # macOS, Linux, Bash
```

## Layout

```
include/mpng/mpng.h    the whole API
src/mpng.c             the library
tests/mpng_test.c      the decoder, round trips, block edges, refusals, files
tests/mpng_write.c     writes the shapes check_with_zlib.py reads
tests/check_with_zlib.py   zlib's reader on the C output (runs when Python 3 is found)
examples/consumer/     a project using the package
build-and-test.ps1     build and test, Windows PowerShell
build-and-test.sh      build and test, macOS and Linux Bash
CMakeLists.txt         library `mpng`, tests `mpng-test` and `mpng-zlib-read`
```

## Licence

MIT, see `LICENSE`.
