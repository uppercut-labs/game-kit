# ps2-audsrv-stream

Plays a PlayStation 2 game's sound through audsrv. It starts audsrv from a module embedded in the program, sets 16-bit
stereo at the game's rate, and then, once a frame, keeps audsrv's ring buffer on the IOP full from a callback that
renders signed 16-bit stereo PCM. The game writes a mixer; this package decides how much to render and when.

It keeps the parts that are easy to get wrong: four bytes of the ring stay free so a full ring is never mistaken for an
empty one, only whole stereo frames are sent, a render the ring only partly took is finished before anything new is
rendered (so the sound never skips or repeats), and a mute can last a set number of frames while the ring keeps
draining, which is how a cut between two pieces of music avoids a click.

The stream is bookkeeping over a small table of functions (`PasOps`). Nothing in it calls the console, so a host test
drives the whole thing with a fake ring and no hardware. A short PS2SDK file fills the table with audsrv calls and is
compiled only when you build for the console.

Version 1.0.0. Written in C (C99) with CMake. MIT licence.

## What is not in it

- **No IRX binary.** audsrv comes from the PS2SDK (`iop/irx/audsrv.irx`). The glue takes it as a buffer you give it,
  usually the `.irx` file turned into a C array by `bin2c`. `pas/ps2.h` declares the symbols `bin2c` produces for it,
  and the program defines them. LIBSD, which audsrv needs, is loaded from the console's ROM.
- **No IOP reset and no loader patch.** The program does both once, before any IOP module is loaded. audsrv is started
  from memory, so `sbv_patch_enable_lmb()` must already have run. ps2-multitap-pads' `mpad_ps2_reset_iop()` does both.
- **No mixer.** What the sound is, and how game events change it, is the game's.

## Install

This is a C library, so it is not on a package index. A project takes it at a release tag, with CMake `FetchContent`
(CMake 3.20 or later, git, and network at configure time):

```cmake
include(FetchContent)
FetchContent_Declare(ps2_audsrv_stream
  GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git
  GIT_TAG        ps2-audsrv-stream@1.0.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  packages/ps2-audsrv-stream)
FetchContent_MakeAvailable(ps2_audsrv_stream)

target_link_libraries(my-game-test PRIVATE pas)
```

`examples/consumer/CMakeLists.txt` is that, complete, with a program that runs a minute of frames against a ring that
drains at 22,050 Hz and checks that it never runs dry. For an offline build set
`-DFETCHCONTENT_SOURCE_DIR_PS2_AUDSRV_STREAM=<a checkout of game-kit>`. The including project gets the CMake variable
`PAS_VERSION`.

### On the console

The PS2SDK builds with its own Makefiles, so compile two files from this folder with it (`src/pas.c` and `src/ps2.c`,
plus `src/ps2_bin2c.c` if you embed audsrv under the `bin2c` names):

```make
PAS := path/to/game-kit/packages/ps2-audsrv-stream
EE_OBJS += pas.o pas_ps2.o pas_ps2_bin2c.o audsrv_irx.o
EE_INCS += -I$(PAS)/include
EE_LIBS += -laudsrv

pas.o: $(PAS)/src/pas.c
	$(EE_C_COMPILE) -c $< -o $@
pas_ps2.o: $(PAS)/src/ps2.c
	$(EE_C_COMPILE) -c $< -o $@
pas_ps2_bin2c.o: $(PAS)/src/ps2_bin2c.c
	$(EE_C_COMPILE) -c $< -o $@
audsrv_irx.c: $(PS2SDK)/iop/irx/audsrv.irx
	$(PS2SDK)/bin/bin2c $< $@ audsrv_irx
```

The code is C99 (with the alignment attribute of GCC, Clang or MSVC); the PS2SDK's EE compiler is GCC.

## Use

```c
#include "pas/ps2.h"

static PasStream sound;                  /* static: the IOP reads its buffer after pas_feed returns */

static void mix(void *user, int16_t *pcm, uint16_t frames)
{
    /* write 2 * frames samples, left then right */
}

/* once, after the IOP reset and the loader patch: */
PasPs2Config config = pas_ps2_config_bin2c(22050, 90, mix, NULL);   /* rate, volume 0..100, callback, its pointer */
PasPs2Failure failure;
int started = pas_ps2_start(&sound, &config, &failure);
if (started < 0) {
    printf("audio: %s failed (%d/%d)\n", pas_ps2_step(started), failure.result, failure.status);
}

/* every frame: */
pas_feed(&sound);                        /* does nothing if the start failed */
```

### The stream (`pas/pas.h`)

| Function | What it does |
| --- | --- |
| `pas_init(stream, ops, render, user, chunk, volume)` | Sets a stream up over `ops`. `chunk` is the most stereo frames rendered at once (0 = `PAS_MAX_FRAMES`, 960). `volume` is what a mute comes back to; it is not applied here. Returns 1, or 0 for a missing stream, table, callback or required function. |
| `pas_ready(stream)` | 1 once set up. Every other call does nothing on a stream that is not ready. |
| `pas_feed(stream)` | One frame's work (below). Returns the bytes the ring took. |
| `pas_drop_pending(stream)` | Forgets rendered sound the ring has not taken, so the next feed renders fresh. For a cut to new music. |
| `pas_mute_for(stream, feeds)` | Volume 0 now, back at the start of the `feeds`-th feed from now; the ring keeps filling meanwhile, so what was queued drains in silence. 0 gives the volume back at once. |
| `pas_set_volume(stream, volume)` | Changes the stream's volume, and the ring's unless a mute is running, which then comes back to the new volume. |

`pas_feed`, in order:

1. A mute that has run its course gives the volume back.
2. The free space is the ring's free bytes less 4 (`PAS_RING_SLACK_BYTES`), rounded down to whole stereo frames. Under
   one frame there is nothing to do this frame.
3. When nothing is pending, the callback renders as many frames as fit, up to the chunk.
4. The ring is given as much of the pending sound as fits. What it does not take waits for the next frame, and nothing
   new is rendered until it is gone.

`PasOps` is `{ context, available, play, set_volume }`: the ring's free bytes, hand it bytes (it returns how many it
took), and set its volume. `set_volume` may be NULL. Every function gets `context` first.

A cut between two pieces of music, as a game does it:

```c
my_mixer_start_new_piece();
pas_drop_pending(&sound);                /* do not finish the old piece's last render */
pas_mute_for(&sound, 10);                /* 10 frames: longer than a full ring takes to drain, so the old sound ends silently */
```

### The PS2SDK glue (`pas/ps2.h`)

| Function | What it does |
| --- | --- |
| `pas_ps2_start(stream, config, failure)` | `SifLoadModule("rom0:LIBSD")`, then audsrv from `config->audsrv` with `SifExecModuleBuffer`, `audsrv_init()`, `audsrv_set_format` with the rate, 16 bits and 2 channels, `audsrv_set_volume`, then `pas_init` over `pas_ps2_ops()`. Returns 0, or a `PAS_PS2_*` failure with the stream not ready and the SDK's numbers in `failure` (which may be NULL). |
| `pas_ps2_config_bin2c(rate, volume, render, user)` | A config with audsrv from the `bin2c` symbols `audsrv_irx` and `size_audsrv_irx`, and the default chunk. |
| `pas_ps2_ops()` | `audsrv_available`, `audsrv_play_audio` and `audsrv_set_volume` as a `PasOps` table. |
| `pas_ps2_step(code)` | A few words for a failure code, for a log line: `LIBSD load`, `audsrv module`, `audio RPC init`, `audio format`, `audio volume`. |

`PasPs2Config` is `{ audsrv, size_audsrv, rate, volume, chunk_frames, render, user }`. The failure codes are
`PAS_PS2_LIBSD_FAILED`, `PAS_PS2_AUDSRV_FAILED` (`failure.result` is the load's return, `failure.status` the module's),
`PAS_PS2_RPC_FAILED`, `PAS_PS2_FORMAT_FAILED`, `PAS_PS2_VOLUME_FAILED` and `PAS_PS2_BAD_ARGUMENT`.

### Testing a game without a console

Fill a `PasOps` with your own functions (the example consumer does) and the stream runs in a plain test, so a game's
mixer and its frame loop can be checked for underruns and dropped sound on a host. `tests/pas_test.c` is a fuller fake.

## What runs where

| Part | Runs on | In |
| --- | --- | --- |
| the stream and its tests (C, CMake) | Windows, macOS, Linux; CMake 3.20+, Ninja, a C99 compiler | any shell: it is a program |
| the glue's test (`pas-ps2-glue-test`) | the same | built against stand-in headers in `tests/ps2sdk-stub` |
| `src/ps2.c` against the real PS2SDK | any machine with Docker and a PS2SDK image | the Makefile under "On the console" |
| sound from a PS2 or an emulator | a project's own test | outside this package's tests, which drive a fake ring |
| `build-and-test.ps1` | Windows | PowerShell 5.1 or 7 |
| `build-and-test.sh` | macOS, Linux | Bash or zsh |

Tested at 1.0.0:

| System | Toolchain | Result |
| --- | --- | --- |
| macOS 27.0 (Apple M6) | Apple clang 21.0.0, CMake 4.5.1, Ninja 1.13.2, `tools/check-c.sh` | pass (stream 61 checks, glue 28, example consumer) |
| Linux arm64 (Debian trixie, in a container) | gcc 14.2 and clang 19.1 with `-Werror` on the tests; gcc under AddressSanitizer and UBSan | pass |
| PS2SDK: a PS2SDK image with gsKit (image id `sha256:59d1808a9ba4`, built 2026-11-22), Docker on Apple M6 | mips64r5900el-ps2-elf-gcc; a test ELF that calls `mpad_ps2_reset_iop`, `mpad_ps2_open_all`, `mpad_ps2_poll_all` and the ps2-audsrv-stream start and feed, built with `-Wall -Wextra -Werror` and linked with `-lpadx -lmtap -laudsrv -lpatches` (and with `-lpad`), the modules embedded by `bin2c`; every source file also compiles with `-std=c99 -Wpedantic -Werror` | builds clean; `pas_ps2_start`, `pas_feed` and the rest are in the ELF |

The glue test uses stand-in headers with the signatures of the public PS2SDK `audsrv.h` and `loadfile.h`. It proves the
glue's own call order and failure handling; the PS2SDK build above compiles `src/ps2.c` against the real headers. The
package's tests do not run on a console or an emulator.

Build and run the tests, from this folder:

```powershell
.\build-and-test.ps1                       # Windows, PowerShell
```
```bash
./build-and-test.sh                        # macOS, Linux, Bash
```

## Layout

```
include/pas/pas.h          the stream's API and the ops table
include/pas/ps2.h          the PS2SDK glue's API (compile src/ps2.c only with the SDK)
src/pas.c                  the stream
src/ps2.c                  audsrv behind the ops table, and the start
src/ps2_bin2c.c            a config from bin2c-named module symbols (optional)
tests/pas_test.c           the stream against a fake ring
tests/ps2_glue_test.c      the glue against stand-in SDK headers
tests/ps2sdk-stub/         those stand-in headers (declarations only)
examples/consumer/         a project using the package
build-and-test.ps1         build and test, Windows PowerShell
build-and-test.sh          build and test, macOS and Linux Bash
CMakeLists.txt             library `pas`, tests, optional `pas_ps2`
```

## Licence

MIT, see `LICENSE`.
