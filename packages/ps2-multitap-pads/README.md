# ps2-multitap-pads

Reads up to eight PlayStation 2 controllers (two ports, each with a multitap of four slots) once a frame and tells a game
for each one: nothing there, a fresh sample, or "there but not readable this frame". It keeps the parts that are easy to
get wrong: a pad being found is not a pad that left, so a loose plug holds its player slot for a grace period instead of
flickering; a digital pad is asked once to become a DualShock; a sample with a bad flag or too few bytes is not a press.

The poller is a state machine over a small table of functions (`MpadOps`). Nothing in it calls the console, so a host
test drives the whole thing with a fake and no hardware. A short PS2SDK file fills the table with `libpad` and `libmtap`
calls and is compiled only when you build for the console.

Version 1.0.0. Written in C (C99) with CMake. MIT licence.

## What is not in it

- **No IRX binaries.** The three IOP modules the pad services run in (`sio2man`, `mtapman`, `padman`) come from the
  PS2SDK. The glue takes them as buffers you give it; usually they are the `.irx` files from the SDK turned into C arrays
  by `bin2c`. `mpad/ps2.h` declares the symbols `bin2c` produces for them, and the program defines them.
- **No IOP reset, no SIF setup, no loader patch.** See "Who owns the IOP reset" below.
- No game mapping: it gives buttons and a left stick. What a button does is the game's.

## Install

This is a C library, so it is not on a package index. A project takes it at a release tag, with CMake `FetchContent`
(CMake 3.20 or later, git, and network at configure time):

```cmake
include(FetchContent)
FetchContent_Declare(ps2_multitap_pads
  GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git
  GIT_TAG        ps2-multitap-pads@1.0.0
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
plus `src/ps2_bin2c.c` if you embed the modules under the `bin2c` names):

```make
MPAD := path/to/game-kit/packages/ps2-multitap-pads
IRX := sio2man mtapman padman
EE_OBJS += mpad.o mpad_ps2.o mpad_ps2_bin2c.o $(addsuffix _irx.o,$(IRX))
EE_INCS += -I$(MPAD)/include
EE_LIBS += -lpadx -lmtap          # or -lpad -lmtap

mpad.o: $(MPAD)/src/mpad.c
	$(EE_C_COMPILE) -c $< -o $@
mpad_ps2.o: $(MPAD)/src/ps2.c
	$(EE_C_COMPILE) -c $< -o $@
mpad_ps2_bin2c.o: $(MPAD)/src/ps2_bin2c.c
	$(EE_C_COMPILE) -c $< -o $@
%_irx.c: $(PS2SDK)/iop/irx/%.irx
	$(PS2SDK)/bin/bin2c $< $@ $*_irx
```

The code is C99 (with the alignment attribute of GCC, Clang or MSVC); the PS2SDK's EE compiler is GCC.

## Who owns the IOP reset

**The program does, not this package.** A reset wipes every module on the IOP, so it has to happen once, before any
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
if (p.status == MPAD_FRESH && (p.held & MPAD_CROSS)) { /* ... */ }
```

| Function | What it does |
| --- | --- |
| `mpad_init(poller, ops, grace)` | Opens both multitap ports and the eight pad endpoints through `ops`. Returns how many endpoints opened (0..8). An endpoint that did not open is absent for ever. `grace` is the number of "pad is being searched for" frames a pad that was reading may have before it counts as gone (0 = 30, at most 255). |
| `mpad_poll(poller, out[8])` | One frame: a sample per endpoint. |
| `mpad_is_open(poller, endpoint)` | Whether an endpoint opened. |
| `mpad_neutral(status)` | A sample with no buttons and centred sticks. |

A sample is `{ held, lx, ly, status }`. `held` is one bit per pressed button (`MPAD_CROSS`, `MPAD_START` and the rest;
the hardware reports them the other way round and the poller turns them over). The stick is the left stick, `0..255` with
128 in the middle, and stays at 128 for a digital pad. `status` is:

| Status | Means |
| --- | --- |
| `MPAD_FRESH` | A pad was read this frame; `held` and the stick are real. |
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
| `src/ps2.c` against the real PS2SDK, and a real pad | a PS2 or an emulator | not run here |
| `build-and-test.ps1` | Windows | PowerShell 5.1 or 7 |
| `build-and-test.sh` | macOS, Linux | Bash or zsh |

Tested at 1.0.0:

| System | Toolchain | Result |
| --- | --- | --- |
| Linux x86-64 (Ubuntu 24.04, in a container) | gcc 13 and clang 18 with `-Wall -Wextra -Wpedantic -Werror`; gcc under AddressSanitizer and UBSan | pass (poller 293 checks, glue 23) |
| Windows 11 Pro (10.0.26300) | MSYS2 UCRT64 clang 22.1.8, CMake 4.4.3, Ninja 1.13.2, `tools\check-c.ps1` in PowerShell 7 | pass |
| macOS 27.0 (Apple M6) | Apple clang 21.0.0, CMake 4.5.1, Ninja, `tools/check-c.sh` | pass |
| PS2SDK build, console, emulator | not run: no PS2SDK in the test container | waits for a PS2SDK machine |

The glue test uses stand-in headers whose numbers come from the public PS2SDK `libpad.h`. It proves the glue's own mapping
and call order; it does not prove the real SDK headers compile against it. Build `src/ps2.c` with the SDK once before
trusting a release on the console.

Build and run the tests, from this folder:

```powershell
.\build-and-test.ps1                       # Windows, PowerShell
```
```bash
./build-and-test.sh                        # macOS, Linux, Bash
```

## Layout

```
include/mpad/mpad.h        the poller's API, the button bits, the ops table
include/mpad/ps2.h         the PS2SDK glue's API (compile src/ps2.c only with the SDK)
src/mpad.c                 the poller
src/ps2.c                  libpad and libmtap behind the ops table, module loading
src/ps2_bin2c.c            start from bin2c-named module symbols (optional)
tests/mpad_test.c          the poller against a fake driver
tests/ps2_glue_test.c      the glue against stand-in SDK headers
tests/ps2sdk-stub/         those stand-in headers (declarations and constants only)
examples/consumer/         a project using the package
build-and-test.ps1         build and test, Windows PowerShell
build-and-test.sh          build and test, macOS and Linux Bash
CMakeLists.txt             library `mpad`, tests, optional `mpad_ps2`
```

## Licence

MIT, see `LICENSE`.
