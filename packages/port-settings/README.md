# port-settings

A plain `key = value` settings file for a game port: window mode, scaling, button bindings, a pinned controller, whatever
the port keeps. One text file in the per-user folder, readable and editable by hand, rewritten safely by the port.

```
# myport settings. The port rewrites this file: values are kept, comments are not.
window = windowed
p1.x = key:Z, pad:south
touch.size = 120
```

Nothing in this package knows a game. It has no dependencies beyond the C library: no SDL, so a port's tools and tests
can use it too. It works with `port-sdl-input` (its three store functions are the ones that package's input layer takes),
and by itself.

Version 0.1.0. Written in C (C11) with CMake. MIT licence.

## Install

This is a C library, so it is not on a package index. A project takes it at a release tag, with CMake `FetchContent`
(CMake 3.20 or later, git, and network at configure time):

```cmake
include(FetchContent)
FetchContent_Declare(port_settings
  GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git
  GIT_TAG        port-settings@0.1.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  packages/port-settings)
FetchContent_MakeAvailable(port_settings)

target_link_libraries(my-port PRIVATE pset)
```

`examples/consumer/CMakeLists.txt` is that, complete, with a program that reads, changes, saves and reads again. For an
offline build set `-DFETCHCONTENT_SOURCE_DIR_PORT_SETTINGS=<a checkout of game-kit>`. A project that vendors the folder
instead uses `add_subdirectory(<path>/port-settings)`. The including project gets the CMake variable `PSET_VERSION`.

## Use

```c
#include "pset/pset.h"

char dir[512];
pset_user_dir("my-port", dir, sizeof dir);               /* ~/.local/share/my-port/ and the like, created */
/* path = dir + "my-port.cfg" */
PsetOptions o = { .name = "my-port", .header = "my-port settings. The port rewrites this file." };
PortSettings *s = pset_open(path, &o);                   /* an empty one if the file is not there yet */

int scale = pset_get_int_step(s, "touch.size", 100, 50, 150, 10);   /* 50-150, in tens */
int mode  = pset_get_choice(s, "mode", (const char *const[]){"enhanced", "original"}, 2, 0);
int fps   = pset_get_bool(s, "fps", 0);

pset_set(s, "mode", "original");
pset_set_int(s, "touch.size", 120);
pset_save(s);                                            /* temporary file, then a replace: never half a file */
pset_free(s);
```

| Function | What it does |
| --- | --- |
| `pset_open(path, opt)`, `pset_load(path)`, `pset_free` | Read a file (a missing file is an empty object). `opt` names the warnings' prefix, sets the header line `save` writes, and can send warnings to a callback instead of stderr. |
| `pset_get`, `pset_get_or` | The text, or NULL / a fallback. |
| `pset_get_int`, `pset_get_int_step`, `pset_get_bool`, `pset_get_choice` | Typed reads that never fail: a bad or missing value gives the fallback. `get_int_step` rounds to a step and holds the number between two ends, for a setting a person nudges. `get_bool` takes on/off, true/false, yes/no, 1/0 in any case. |
| `pset_set`, `pset_set_int`, `pset_set_bool`, `pset_remove` | Change one key. A new key goes last. Text the file could not read back is refused (see below). |
| `pset_count`, `pset_key_at`, `pset_value_at` | Walk the keys in file order. |
| `pset_save`, `pset_path` | Write the whole file (creating its folder) through `<path>.part` and a replace, so a crash leaves the old file. |
| `pset_store_get`, `pset_store_set`, `pset_store_save` | The same as callbacks with a `void *ctx`, the shape `port-sdl-input`'s `PsiStore` takes. |
| `pset_user_dir(app, out, cap)` | The per-user folder, with a trailing separator, created: `%APPDATA%\<app>\` on Windows, `~/Library/Application Support/<app>/` on macOS, `$XDG_DATA_HOME/<app>/` or `~/.local/share/<app>/` elsewhere. A phone's app folder is the port's own to give. |

## The file

One `key = value` per line. A `#` starts a comment anywhere on a line, so a value can not hold one, and `pset_set`
refuses a value with a `#` or a line break and a key with `=`, `#` or a line break, instead of writing a file that reads
back differently. Spaces and tabs around key and value are dropped; a later duplicate of a key wins; a line that is not
`key = value` is reported (with its line number) and dropped. The file is read whole, so a line of any length works, and a
byte order mark from an editor is skipped. On Windows the path is UTF-8 and is converted for the file calls, so a user
name with an accent works. Keys keep their order, so a hand-edited file keeps its order; comments are not kept (the
header line says so).

## A settings screen

A settings panel is not in this package. What a panel shows (rows, wording, a font, the port's own choices, what needs a
restart) is the game's, and it draws with the game's renderer. This package is what such a panel reads and writes.

## What runs where

| Part | Runs on | In |
| --- | --- | --- |
| the library and its test (C, CMake) | Windows, macOS, Linux; CMake 3.20+, Ninja and a C compiler; no SDL | any shell: it is a program |
| `build-and-test.ps1` | Windows | PowerShell 5.1 or 7 |
| `build-and-test.sh` | macOS, Linux | Bash or zsh |

Tested at 0.1.0:

| System | Toolchain | Result |
| --- | --- | --- |
| Linux x86-64 (Ubuntu 24.04, in a container) | gcc 13, `build-and-test.sh` in Bash; also under AddressSanitizer and UBSan | pass |
| Windows 11, macOS | not run yet: the Windows file calls (`_wfopen`, `MoveFileExW`) are untested | waits for a maintainer machine |

Build and run the test, from this folder:

```powershell
.\build-and-test.ps1                       # Windows, PowerShell
```
```bash
./build-and-test.sh                        # macOS, Linux, Bash
```

## Layout

```
include/pset/pset.h    the whole API
src/pset.c             the library
tests/pset_test.c      reading, typed reads, refusals, saving, folders, the per-user folder
examples/consumer/     a project using the package
build-and-test.ps1     build and test, Windows PowerShell
build-and-test.sh      build and test, macOS and Linux Bash
CMakeLists.txt         library `pset`, test `pset-test`
```

## Licence

MIT, see `LICENSE`.
