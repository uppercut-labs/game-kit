# pad-replay

A plain-text controller replay for a game port: a six-field line format, a reader that refuses a bad file whole, a player
that gives every pad's state at a frame, and a recorder that writes the same format. A port records a real play session,
a test replays it with no pad attached, and a builder script writes one by hand.

```
# frame slot buttons_hex lx ly connected
0 0 0000 128 128 1
30 0 0008 128 128 1
33 0 0000 128 128 1
```

Nothing in this package knows a console or a game. The button bits are the port's own: the package carries no header from
any SDK, and a port that wants names for its bits gives a table. It has no dependencies beyond the C library.

Version 1.0.0. Written in C (C11) with CMake. MIT licence. The format is in [docs/format.md](docs/format.md).

## Install

This is a C library, so it is not on a package index. A project takes it at a release tag, with CMake `FetchContent`
(CMake 3.20 or later, git, and network at configure time):

```cmake
include(FetchContent)
FetchContent_Declare(pad_replay
  GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git
  GIT_TAG        pad-replay@1.0.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  packages/pad-replay)
FetchContent_MakeAvailable(pad_replay)

target_link_libraries(my-port PRIVATE padr)
```

`examples/consumer/CMakeLists.txt` is that, complete, with a program that records, then replays. For an offline build set
`-DFETCHCONTENT_SOURCE_DIR_PAD_REPLAY=<a checkout of game-kit>`. A project that vendors the folder instead uses
`add_subdirectory(<path>/pad-replay)`. A build that is not CMake needs only `src/padr.c` and `src/padtrace.c` with
`include/` on the include path. The including project gets the CMake variable `PADR_VERSION`.

## Use

Replay a file:

```c
#include "padr/padr.h"

PadrOptions opt = { .slots = 8, .max_events = 32768 };      /* both 0 means these defaults */
PadrError err;
PadrScript *script = padr_load_file("session.input", &opt, &err);
if (!script) {
    fprintf(stderr, "session.input: %s (line %u)\n", padr_status_text(err.status), err.line);
    return 1;
}

PadrSample pads[8];
for (unsigned frame = 0; running; ++frame) {
    padr_sample(script, frame, pads, 8);      /* every slot's state at this frame; frames must rise */
    game_update(pads);                        /* pads[i].buttons is the port's own bit set */
}
padr_free(script);
```

Record the physical pads (before any replay replaces them):

```c
PadrTrace *trace = padr_trace_open("session.input", 8, NULL);   /* NULL if the file exists: never overwritten */
...
padr_trace_sample(trace, frame, physical_pads, 8);               /* once per frame; only changes reach the file */
...
padr_trace_close(trace);                                         /* 1 when every write succeeded */
```

| Function | What it does |
| --- | --- |
| `padr_load_file(path, opt, err)`, `padr_load_text(text, length, opt, err)` | Read a replay from a file or from memory (for a platform with no file system). Strict: the first bad line fails the load, with a status and a line number. `opt` sets the slot count (default 8, at most 255) and the event cap (default 32768). |
| `padr_sample(script, frame, out, n)`, `padr_rewind`, `padr_free` | The state of every slot at a frame, in rising frames. A NULL script gives absent pads, so a game can call it with or without a replay. |
| `padr_slots`, `padr_event_count`, `padr_last_frame` | What was read, to size a run. |
| `padr_trace_open(path, slots, opt)`, `padr_trace_sample`, `padr_trace_records`, `padr_trace_close` | The recorder. Exclusive create, a line only when a slot changes, unreadable pads keep their state, a flush every 60 frames (a setting). |
| `padr_format_event(buf, cap, frame, slot, sample)` | The one line of the file for an event, for a generator that writes its own files. |
| `padr_buttons_parse`, `padr_buttons_format` | `"Left+Cross"` to a mask and back, with the caller's table of names. |
| `padr_neutral(connected)` | A pad at rest. |

## What it fixes over the copies it replaces

The format came out of several ports that each carried their own reader. This package takes the union of their fixes:

- The reader refuses a NUL byte and a line over 255 bytes, in a file and in memory (one copy had the check, the in-memory
  copy did not).
- The event cap is a setting. One port ran at 32768 events and another at 4096, which an eight-pad trace of about 8.5
  minutes passed because the recorder wrote a line per slot per second even when nothing happened.
- The recorder writes a line only when a slot changes, so an idle multi-slot trace stays replayable at any cap.

## What runs where

| Part | Runs on | In |
| --- | --- | --- |
| the library and its test (C, CMake) | Windows, macOS, Linux; CMake 3.20+, Ninja and a C compiler; no SDL | any shell: it is a program |
| `build-and-test.ps1` | Windows | PowerShell 5.1 or 7 |
| `build-and-test.sh` | macOS, Linux | Bash or zsh |

Tested at 1.0.0:

| System | Toolchain | Result |
| --- | --- | --- |
| Linux x86-64 (Ubuntu 24.04, in a container) | gcc 13, `build-and-test.sh` in Bash; also under AddressSanitizer and UBSan | pass |
| Windows, macOS | not run yet: nothing here is platform specific beyond C11's `fopen` mode `"x"` | waits for a maintainer machine |

Build and run the test, from this folder:

```powershell
.\build-and-test.ps1                       # Windows, PowerShell
```
```bash
./build-and-test.sh                        # macOS, Linux, Bash
```

## Layout

```
include/padr/padr.h    the whole API
src/padr.c             reader, player, line writer, button names
src/padtrace.c         the recorder
tests/padr_test.c      reading, rejections, the cap, slots, the recorder, an idle long session
docs/format.md         the file format
examples/consumer/     a project using the package
build-and-test.ps1     build and test, Windows PowerShell
build-and-test.sh      build and test, macOS and Linux Bash
CMakeLists.txt         library `padr`, test `padr-test`
```

## Companion

The Python package `uppercut-pad-replay-tools` in this repository writes the same files from a script (event and tap
helpers, capture scripts, a Dolphin DTM export).

## Licence

MIT, see `LICENSE`.
