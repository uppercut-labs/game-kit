# ps2-multitap-pads

Reads up to eight PlayStation 2 controllers (two ports, each with a multitap of four slots) once a frame and tells a game
for each one: nothing there, a fresh sample, or "there but not readable this frame". It keeps the parts that are easy to
get wrong: a pad being found is not a pad that left, so a loose plug holds its player slot for a grace period instead of
flickering; a digital pad is asked once to become a DualShock; a sample with a bad flag or too few bytes is not a press.

The poller is a state machine over a small table of functions (`MpadOps`). Nothing in it calls the console, so a host
test drives the whole thing with a fake and no hardware. A short PS2SDK file fills the table with `libpad` and `libmtap`
calls and is compiled only when you build for the console.

Version 1.1.0. Written in C (C99) with CMake. MIT licence.

## What is not in it

- **No IRX binaries.** The three IOP modules the pad services run in (`sio2man`, `mtapman`, `padman`) come from the
  PS2SDK. The glue takes them as buffers you give it; usually they are the `.irx` files from the SDK turned into C arrays
  by `bin2c`. `mpad/ps2.h` declares the symbols `bin2c` produces for them, and the program defines them.
- **No IOP reset, no SIF setup, no loader patch inside the pad start.** See "Who owns the IOP reset" below. A program
  that owns the whole machine can call `mpad_ps2_reset_iop()` for those steps, at the point it chooses.
- No game mapping: it gives buttons and both analog sticks. What a button does is the game's.

## Install

This is a C library, so it is not on a package index. A project takes it at a release tag, with CMake `FetchContent`
(CMake 3.20 or later, git, and network at configure time):

```cmake
include(FetchContent)
FetchContent_Declare(ps2_multitap_pads
  GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git
  GIT_TAG        ps2-multitap-pads@1.1.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  packages/ps2-multitap-pads)
FetchContent_MakeAvailable(ps2_multitap_pads)

target_link_libraries(my-game-test PRIVATE mpad)
```

`examples/consumer/CMakeLists.txt` is that, complete, with a program that drives the poller through a plugged-in,
loose and lost pad. For an offline build set `-DFETCHCONTENT_SOURCE_DIR_PS2_MULTITAP_PADS=<a checkout of game-kit>`.
The including project gets the CMake variable `MPAD_VERSION`.

### On the console

The PS2SDK builds with its own Makefiles, so compile two files from this folder with it (`src/mpad.c` and `src/ps2.c`,
plus `src/ps2_bin2c.c` if you embed the modules under the `bin2c` names, and `src/ps2_program.c` for the whole-program
helpers):

```make
MPAD := path/to/game-kit/packages/ps2-multitap-pads
IRX := sio2man mtapman padman
EE_OBJS += mpad.o mpad_ps2.o mpad_ps2_bin2c.o mpad_ps2_program.o $(addsuffix _irx.o,$(IRX))
EE_INCS += -I$(MPAD)/include
EE_LIBS += -lpadx -lmtap -lpatches   # or -lpad -lmtap; -lpatches is for mpad_ps2_reset_iop

mpad.o: $(MPAD)/src/mpad.c
	$(EE_C_COMPILE) -c $< -o $@
mpad_ps2.o: $(MPAD)/src/ps2.c
	$(EE_C_COMPILE) -c $< -o $@
mpad_ps2_bin2c.o: $(MPAD)/src/ps2_bin2c.c
	$(EE_C_COMPILE) -c $< -o $@
mpad_ps2_program.o: $(MPAD)/src/ps2_program.c
	$(EE_C_COMPILE) -c $< -o $@
%_irx.c: $(PS2SDK)/iop/irx/%.irx
	$(PS2SDK)/bin/bin2c $< $@ $*_irx
```

The code is C99 (with the alignment attribute of GCC, Clang or MSVC); the PS2SDK's EE compiler is GCC.

## Who owns the IOP reset

**The program does it.** A reset wipes every module on the IOP, so it has to happen once, before any
service is loaded: pads, memory card, sound and file access all share it. A pad library that reset the IOP inside its own
start function would unload whatever the program had already loaded, and would force every other service to start after
the pads. Two real programs written from the same demo did it both ways, one inside the pad start function (it had only
pads) and one in `main` before everything (it also had a memory card and sound); only the second scales. So this package
assumes the IOP is in the state the program wants, and the program does, once:

```c
sceSifInitRpc(0);
while (!SifIopReset("", 0)) {}
while (!SifIopSync()) {}
sceSifInitRpc(0);
sbv_patch_enable_lmb();               /* lets SifExecModuleBuffer load a module from memory */

MpadPs2Drivers drivers = { sio2man_irx, size_sio2man_irx, mtapman_irx, size_mtapman_irx, padman_irx, size_padman_irx };
int opened = mpad_ps2_start(&poller, &drivers, 0);   /* >= 0: endpoints that opened; < 0: MPAD_PS2_* failure */
/* ...then load the memory card and sound modules, open the card... */
```

The loader patch is the program's for the same reason: it is global, and every module loaded from memory needs it.

`mpad_ps2_reset_iop()` is exactly those five lines. Calling it first thing in `main`, then `mpad_ps2_open_all()`, then
the other services, is the order above with less to type.

## Whole-program helpers

A game that reads all eight endpoints every frame and keeps no poller of its own needs three calls from `mpad/ps2.h`
(compile `src/ps2_program.c` and `src/ps2_bin2c.c`, link `-lpatches`):

```c
#include "mpad/ps2.h"

int main(void)
{
    MpadSample pads[MPAD_ENDPOINTS];

    mpad_ps2_reset_iop();                 /* once, before any IOP module */
    int opened = mpad_ps2_open_all(0);    /* the bin2c modules; prints a line per failure; 0..8 or MPAD_PS2_* */
    /* ...load sound and the memory card here... */
    for (;;) {
        mpad_ps2_poll_all(pads);          /* index = port * 4 + slot */
        if (pads[0].status == MPAD_FRESH && (pads[0].held & MPAD_X)) { /* ... */ }
    }
}
```

| Function | What it does |
| --- | --- |
| `mpad_ps2_reset_iop()` | `sceSifInitRpc(0)`, `SifIopReset("", 0)` until it is accepted, `SifIopSync()` until it is done, `sceSifInitRpc(0)` again, `sbv_patch_enable_lmb()`. |
| `mpad_ps2_open_all(grace)` | `mpad_ps2_start_bin2c` on a poller kept in static storage. Prints `Controller start failed (code N)` for a failed start and `Controller port P slot S failed to open` for each endpoint that did not open. Returns the number opened or the `MPAD_PS2_*` failure. |
| `mpad_ps2_poll_all(out[8])` | One frame for all eight endpoints from that poller. Before a successful open, every sample is `MPAD_ABSENT` with centred sticks. |
| `mpad_ps2_poller()` | That poller, for `mpad_is_open` and the rest of `mpad/mpad.h`. |

## The sample and the button names without the PS2SDK

`mpad/sample.h` holds `MpadSample`, the `MPAD_*` button bits and the three statuses, and includes only `<stdint.h>`.
Code that a PS2 game shares with another console's build (a pause menu, a remap screen, a host test) includes it
without the PS2SDK or the poller. `mpad/mpad.h` includes it, so nothing changes for code that already uses that.

- The bits are the PS2SDK's `PAD_*` values: `MPAD_SELECT`, `MPAD_L3`, `MPAD_R3`, `MPAD_START`, `MPAD_UP`, `MPAD_RIGHT`,
  `MPAD_DOWN`, `MPAD_LEFT`, `MPAD_L2`, `MPAD_R2`, `MPAD_L1`, `MPAD_R1`, `MPAD_TRIANGLE`, `MPAD_CIRCLE`, `MPAD_X` and
  `MPAD_SQUARE`. `MPAD_CROSS` is the same bit as `MPAD_X`, under the SDK's name.
- `MPAD_FOR_EACH_BUTTON(X)` expands `X(name, bit)` once per button in bit order, with the printed names (`"Select"`,
  `"L3"`, ..., `"X"`, `"Square"`). `MPAD_BUTTON_COUNT` is 16.
- With `MPAD_SHORT_NAMES` defined before the first include, the bits are also `B_SELECT`, `B_L3`, `B_R3`, `B_START`,
  `B_UP`, `B_RIGHT`, `B_DOWN`, `B_LEFT`, `B_L2`, `B_R2`, `B_L1`, `B_R1`, `B_TRIANGLE`, `B_CIRCLE`, `B_X` and `B_SQUARE`,
  and the statuses `INPUT_ABSENT`, `INPUT_FRESH` and `INPUT_UNREADABLE`. They are off by default because names that
  short collide easily.

A game whose controller type is `{ held, lx, ly, rx, ry, status }` can use the sample as that type:

```c
#define MPAD_SHORT_NAMES 1
#include "mpad/sample.h"

typedef MpadSample Controller;            /* if (pad.status == INPUT_FRESH && (pad.held & B_X)) ... */
```

### Names for replays and capture scripts

`mpad/padr.h` turns the list into a table for [pad-replay](../pad-replay)'s `padr_buttons_parse` and
`padr_buttons_format`. It needs pad-replay's include directory and nothing from the PS2SDK.

```c
#include "mpad/padr.h"

uint16_t mask;
padr_buttons_parse(mpad_padr_buttons, MPAD_PADR_BUTTON_COUNT, "R1+Down+Left", &mask);
padr_buttons_format(mpad_padr_buttons, MPAD_PADR_BUTTON_COUNT, sample.held, text, sizeof text);   /* "Left+X" */
```

The table has the sixteen buttons in bit order and one more entry, `"Cross"`, so scripts that spell the button that
way still parse. Formatting writes `X`, because that entry covers the bit first. Names are matched without regard to
case.

## Use

```c
#include "mpad/mpad.h"

static MpadPoller poller;                        /* static: the driver keeps writing into its buffers */
static MpadSample pads[MPAD_ENDPOINTS];

/* once, after the IOP is set up (on the console: mpad_ps2_start, which does this and the three modules): */
mpad_init(&poller, &ops, 0);                     /* ops: the MpadOps table; 0 = the default 30-frame grace */

/* every frame: */
mpad_poll(&poller, pads);
MpadSample p = pads[MPAD_ENDPOINT(0, 1)];        /* port 1, multitap slot 2: index = port * 4 + slot */
if (p.status == MPAD_FRESH && (p.held & MPAD_X)) { /* ... */ }
```

| Function | What it does |
| --- | --- |
| `mpad_init(poller, ops, grace)` | Opens both multitap ports and the eight pad endpoints through `ops`. Returns how many endpoints opened (0..8). An endpoint that did not open is absent for ever. `grace` is the number of "pad is being searched for" frames a pad that was reading may have before it counts as gone (0 = 30, at most 255). |
| `mpad_poll(poller, out[8])` | One frame: a sample per endpoint. |
| `mpad_is_open(poller, endpoint)` | Whether an endpoint opened. |
| `mpad_neutral(status)` | A sample with no buttons and centred sticks. |

A sample is `{ held, lx, ly, rx, ry, status }`. `held` is one bit per pressed button (`MPAD_X`, `MPAD_START` and the rest;
the hardware reports them the other way round and the poller turns them over). The sticks are `0..255` with
128 in the middle, and stay at 128 for a digital pad. `status` is:

| Status | Means |
| --- | --- |
| `MPAD_FRESH` | A pad was read this frame; `held` and the sticks are real. |
| `MPAD_UNREADABLE` | A pad is there (it read before, and has not been missing for the whole grace) but gave no sample this frame: it is being found again, the driver is busy, the read was bad, or a digital pad was just asked to become a DualShock. Treat it as "no input yet", not as "unplugged". |
| `MPAD_ABSENT` | Nothing there, never opened, unplugged for certain, or missing longer than the grace. |

### The rules, in the order the poller applies them

1. An endpoint that did not open is `MPAD_ABSENT`.
2. State `DISCONNECTED` is `MPAD_ABSENT` at once, and the endpoint forgets everything but its open port.
3. State `FINDING` or `ERROR` clears the "mode already checked" flag; for a pad that had been reading it also counts one
   searching frame, and when the count reaches the grace the pad is gone. Until then it is `MPAD_UNREADABLE`.
4. Any state other than `STABLE` or `FINDING_CTP` (a command is running) is not read. It is `MPAD_UNREADABLE` if the pad
   had been reading, else `MPAD_ABSENT`, and it does not age the pad.
5. A read of fewer than 4 bytes, or with a bad flag, is the same: `MPAD_UNREADABLE` or `MPAD_ABSENT`.
6. A good read marks the pad as present and resets the searching count.
7. Once per plug-in, in state `STABLE`, a pad that reads as a digital pad and whose mode table lists a DualShock is asked
   to become one, locked. That frame is `MPAD_UNREADABLE`; the driver applies the mode over the next frames. The request
   is made once whether or not it is accepted, and again only after the pad has been lost and found.
8. Otherwise the sample is `MPAD_FRESH`. The stick is read only when the pad is analog or a DualShock and the read held
   at least 8 bytes.

### Testing a game without a console

Fill an `MpadOps` with your own functions (the example consumer does) and the poller runs in a plain test. The table is
seven functions: open a multitap port, open a pad endpoint, a pad's state, a pad's raw sample, the size and entries of its
mode table, and the mode request. `tests/mpad_test.c` is a fuller fake.

## The PS2SDK glue

`mpad/ps2.h`: `mpad_ps2_ops()` returns the table with `libpad` and `libmtap` behind it. `mpad_ps2_load_drivers()` loads
the three modules (in that order: `sio2man`, `mtapman`, `padman`) and starts the services. `mpad_ps2_start()` does both and
then `mpad_init`. It returns the number of endpoints opened, or `MPAD_PS2_SIO2MAN_FAILED`, `MPAD_PS2_MTAPMAN_FAILED`,
`MPAD_PS2_PADMAN_FAILED` (that module did not load or did not start), `MPAD_PS2_RPC_FAILED` (`mtapInit` or `padInit` did
not answer) or `MPAD_PS2_BAD_ARGUMENT`. `mpad_ps2_start_bin2c()` takes the modules from the `bin2c` symbol names.

`src/ps2.c` asserts at compile time that its button and pad-type numbers equal the SDK's own macros, so a changed SDK is a
build error and not a pad that reads wrong. It maps `PAD_STATE_EXECCMD` and any state it does not know to "busy".

## What runs where

| Part | Runs on | In |
| --- | --- | --- |
| the poller and its tests (C, CMake) | Windows, macOS, Linux; CMake 3.20+, Ninja, a C99 compiler | any shell: it is a program |
| the glue's mapping test (`mpad-ps2-glue-test`) | the same | built against stand-in headers in `tests/ps2sdk-stub` |
| the button names (`mpad-names-test`) and the pad-replay table (`mpad-padr-test`) | the same; the table test fetches pad-replay at its release tag when it is not beside this folder | a program |
| `src/ps2.c` against the real PS2SDK | any machine with Docker: the `ghcr.io/ps2dev/ps2dev` image | the Makefile under "On the console" |
| a real pad on a PS2 or an emulator | a project's own test | outside this package's tests, which drive a fake pad driver |
| `build-and-test.ps1` | Windows | PowerShell 5.1 or 7 |
| `build-and-test.sh` | macOS, Linux | Bash or zsh |

Tested at 1.1.0:

| System | Toolchain | Result |
| --- | --- | --- |
| macOS 27.0 (Apple M6) | Apple clang 21.0.0, CMake 4.5.1, Ninja 1.13.2, `tools/check-c.sh` | pass (poller 293 checks, glue 33, names 11, pad-replay table 24) |
| Linux arm64 (Debian trixie, in a container) | gcc 14.2 and clang 19.1; gcc under AddressSanitizer and UBSan | pass |
| PS2SDK: a PS2SDK image with gsKit (image id `sha256:59d1808a9ba4`, built 2026-11-22), Docker on Apple M6 | mips64r5900el-ps2-elf-gcc; a test ELF that calls `mpad_ps2_reset_iop`, `mpad_ps2_open_all`, `mpad_ps2_poll_all` and the ps2-audsrv-stream start and feed, built with `-Wall -Wextra -Werror` and linked with `-lpadx -lmtap -laudsrv -lpatches` (and with `-lpad`), the modules embedded by `bin2c`; every source file also compiles with `-std=c99 -Wpedantic -Werror` | builds clean; the new functions are in the ELF |
| GameCube: devkitPPC (powerpc-eabi-gcc 16.2.0) with libogc | `mpad/sample.h` with `MPAD_SHORT_NAMES`, and det-core with `DET_SDK_TYPES`, in one file with `-std=c99 -Wpedantic -Werror` | compiles |

Tested at 1.0.0:

| System | Toolchain | Result |
| --- | --- | --- |
| Linux x86-64 (Ubuntu 24.04, in a container) | gcc 13 and clang 18 with `-Wall -Wextra -Wpedantic -Werror`; gcc under AddressSanitizer and UBSan | pass (poller 293 checks, glue 23) |
| Windows 11 Pro (10.0.26300) | MSYS2 UCRT64 clang 22.1.8, CMake 4.4.3, Ninja 1.13.2, `tools\check-c.ps1` in PowerShell 7 | pass |
| macOS 27.0 (Apple M6) | Apple clang 21.0.0, CMake 4.5.1, Ninja, `tools/check-c.sh` | pass |
| PS2SDK: `ghcr.io/ps2dev/ps2dev:latest` (digest `sha256:6452994a6cf1`, built 2026-11-20 from ps2dev master `594e2b2`), Docker on Apple M6 | mips64r5900el-ps2-elf-gcc 15.2.0; the Makefile under "On the console" with `-Wall -Wextra -Werror` builds `src/mpad.c`, `src/ps2.c` and `src/ps2_bin2c.c` and links them with `-lpadx -lmtap` (and with `-lpad -lmtap`), `sio2man`, `mtapman` and `padman` embedded by `bin2c`, into a test ELF that resets the IOP, starts the pads and polls; the three files also compile with `-std=c99 -Wpedantic -Werror` | builds clean; `mpad_ps2_ops`, `mpad_ps2_start` and `mpad_ps2_start_bin2c` are in the ELF |

The glue test uses stand-in headers whose numbers come from the public PS2SDK `libpad.h`. It proves the glue's own mapping
and call order. The PS2SDK build above compiles `src/ps2.c` against the real SDK headers, and the glue's compile-time
assertions check its button and pad-type numbers against the SDK's own macros. The package's tests do not run on a console
or an emulator.

Build and run the tests, from this folder:

```powershell
.\build-and-test.ps1                       # Windows, PowerShell
```
```bash
./build-and-test.sh                        # macOS, Linux, Bash
```

## Layout

```
include/mpad/mpad.h        the poller's API and the ops table
include/mpad/sample.h      the sample, the button bits and names, the statuses (needs only <stdint.h>)
include/mpad/padr.h        the buttons as a pad-replay name table (needs pad-replay's headers)
include/mpad/ps2.h         the PS2SDK glue's API (compile src/ps2*.c only with the SDK)
src/mpad.c                 the poller
src/ps2.c                  libpad and libmtap behind the ops table, module loading
src/ps2_bin2c.c            start from bin2c-named module symbols (optional)
src/ps2_program.c          IOP reset, open and poll all eight endpoints on a static poller (optional)
tests/mpad_test.c          the poller against a fake driver
tests/ps2_glue_test.c      the glue and the whole-program helpers against stand-in SDK headers
tests/mpad_names_test.c    mpad/sample.h alone, with the short names
tests/mpad_padr_test.c     the pad-replay table through pad-replay
tests/ps2sdk-stub/         those stand-in headers (declarations and constants only)
examples/consumer/         a project using the package
build-and-test.ps1         build and test, Windows PowerShell
build-and-test.sh          build and test, macOS and Linux Bash
CMakeLists.txt             library `mpad`, tests, optional `mpad_ps2` and `mpad_ps2_program`
```

## Licence

MIT, see `LICENSE`.
