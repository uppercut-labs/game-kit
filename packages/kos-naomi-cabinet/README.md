# kos-naomi-cabinet

What a game on a Sega NAOMI cabinet (built with KallistiOS) needs from the cabinet, written so that the logic runs and is
tested without one:

- **Settings in the EEPROM.** A game keeps its settings block in the cabinet's 128-byte EEPROM the way the BIOS expects it:
  two copies, a size header, a checksum, the BIOS's own system settings kept byte for byte, and a read-back after every
  write. The game chooses the magic and the layout; this package knows no fields.
- **Coin credits.** Credits from the JVS board's coin meters (the first reading is a baseline, a meter that falls is
  re-based), or from its one-shot insert flags, with a cap, and spend and take.
- **Test and service switch edges.** Pressed and released edges of a switch word.
- **KallistiOS glue.** A short file that fills those from the MIE driver (EEPROM, meters, switches), compiled only with
  KallistiOS, and an optional wrap of `hardware_sys_mode` for emulators that report a retail console.
- **A save-block adapter.** The EEPROM as the storage of a [save-block](../save-block) store, for a game with a small save
  that wants save-block's two alternating slots.

The logic takes the EEPROM as two callbacks (`KcabEeprom`), so a host test uses the RAM chip in the package, with faults.
Version 1.0.0. Written in C (C99) with CMake. MIT licence.

## What is not in it

- No KallistiOS, no NAOMI BIOS, no game data. The glue includes `<kos.h>` and `<dc/maple/mie.h>`, which you have if you
  build with KallistiOS, and is not compiled otherwise.
- No game profile: no settings fields, no scores, no magic. The game gives the magic and the layout.
- No VMU LCD text. It stayed out: it needs a font, a layout and a game's idea of what a screen shows, and the two copies
  of it do not share a drawing path.

## Install

This is a C library, so it is not on a package index. A project takes it at a release tag, with CMake `FetchContent`
(CMake 3.20 or later, git, and network at configure time):

```cmake
include(FetchContent)
FetchContent_Declare(kos_naomi_cabinet
  GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git
  GIT_TAG        kos-naomi-cabinet@1.0.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  packages/kos-naomi-cabinet)
FetchContent_MakeAvailable(kos_naomi_cabinet)

target_link_libraries(my-game PRIVATE kcab)          # the logic
# target_link_libraries(my-game PRIVATE kcab::save_block)   # the save-block adapter, when save-block is found
```

`examples/consumer/CMakeLists.txt` is that, complete, with a program that saves settings on a RAM chip, reboots and counts
coins. For an offline build set `-DFETCHCONTENT_SOURCE_DIR_KOS_NAOMI_CABINET=<a checkout of game-kit>`. The including
project gets the CMake variable `KCAB_VERSION`.

**The adapter** (`kcab::save_block`) is built when a `save_block::save_block` target exists, or the `save-block` folder is
next to this one (a checkout of game-kit has both), or `-DKCAB_SAVE_BLOCK=ON` makes CMake fetch it at `save-block@1.0.0`
(with `-DKCAB_SAVE_BLOCK=OFF` it is never looked for). The rest of the package needs nothing.

### On the console

KallistiOS builds with its own Makefiles. Compile `src/eeprom.c`, `src/cabinet.c`, `src/ram.c` (only if you want the RAM
chip) and `src/kos.c` with it, with `-I<this folder>/include`. If you want the system-mode override, also compile
`src/kos_naomi_mode.c` and link with `-Wl,--wrap=hardware_sys_mode` (a program built only for NAOMI: it makes a retail
answer read as NAOMI).

## Settings in the EEPROM

The EEPROM is 128 bytes. Bytes 0..35 belong to the BIOS (its system settings, in two checksummed copies). From byte 36 on
the layout is the BIOS's convention for a game's block, and what this package writes:

| Bytes | What |
| --- | --- |
| 0..35 | the BIOS's system settings. Read and written back as they were. |
| 36, 37 | CRC-16 of the first copy of the block, low byte first |
| 38, 39 | the block's size |
| 40, 41 | the CRC-16 again |
| 42, 43 | the size again |
| 44 .. | the block, then, with two copies, the block again; anything after is not touched |

```c
#include "kcab/eeprom.h"

static const uint8_t MAGIC[4] = { 'G', 'M', 'S', '1' };
static const KcabBlock BLOCK = { MAGIC, 4, 8, 2 };      /* magic, its length, bytes in a copy, copies */

KcabEeprom eeprom = kcab_kos_eeprom();                  /* kcab/kos.h; or kcab_ram_eeprom(&ram) in a test */
uint8_t block[8] = { 'G', 'M', 'S', '1', flags, 0, 0, 0 };   /* the game's layout; it starts with the magic */

kcab_result r = kcab_block_load(&eeprom, &BLOCK, block);     /* KCAB_OK, KCAB_RECOVERED, KCAB_EMPTY, ... */
r = kcab_block_save(&eeprom, &BLOCK, block);                 /* reads the chip, stores into it, writes, reads back */
```

| Function | What it does |
| --- | --- |
| `kcab_block_save(eeprom, block, data)` | Reads the chip, puts the block, its copy and the header into what it read, writes all 128 bytes and reads them back to compare. Everything it did not mean to change is kept: the BIOS bytes, and bytes after the block. |
| `kcab_block_load(eeprom, block, data)` | Reads and checks. |
| `kcab_image_store`, `kcab_image_fetch` | The same on an image in memory (no chip). |
| `kcab_eeprom_write_verified(eeprom, image)` | Writes a whole image and reads it back, for putting a chip back after a probe. |
| `kcab_crc16`, `kcab_block_fits`, `kcab_result_text` | The checksum, the layout check, a message. |

A layout fits when `size * copies` is at most 84. With one copy a block can hold 84 bytes; with two, 42.

Results of a load: `KCAB_OK`; `KCAB_RECOVERED` (the data is good but something was damaged: one copy, one checksum or a
size byte; save again to heal it); `KCAB_EMPTY` (no block of this layout: a blank chip, a zeroed one, or another game's);
`KCAB_INVALID` (a block of this layout is there and no copy is good); `KCAB_NO_EEPROM` (the chip did not read). A save
returns `KCAB_WRITE_FAILED` or `KCAB_VERIFY_FAILED` (the chip accepted the write and reads back different) as well.

### The size header, and what KallistiOS does with it

KallistiOS can recompute the checksums of a chip image: `mie_eeprom_fix_crc`. Two copies of a block written by hand with
that function alone drift, and this is what its source (KallistiOS at commit `9926b70aeafc`, `mie_fw.c`) does, read and not
copied:

- It recomputes bytes 0, 1, 18 and 19 (the system settings' checksum) from bytes 2..17. That rewrites BIOS bytes, which
  is harmless only if the BIOS's checksum is the same algorithm.
- For the game block it reads the size from **byte 39 alone**. If that is 0, or `44 + size` runs past 128, **it returns
  without computing anything**. Otherwise it computes the checksum over `size` bytes from offset 44 and stores it at 36, 37
  and 40, 41.
- It **never writes bytes 38, 39, 42 or 43**.

So a game that copies its block to byte 44 and calls `mie_eeprom_fix_crc`, without setting the size header itself, gets
whatever the header already held: on a blank chip (0xFF) or a zero size the game checksum is left as it was, and with a
size left over from another layout (8, say) the checksum covers only the first 8 bytes of a larger block. A game that sets
all four size bytes first is right. This package sets all four, computes the checksum itself over exactly the block, never
calls `mie_eeprom_fix_crc`, and so never touches bytes 0..35. `tests/test_eeprom.c` restates `fix_crc` from the source and
checks all three cases; and a one-off cross-check with KallistiOS's own two functions compiled beside this code agreed on
20,000 random checksums, and showed that `fix_crc` changes nothing in 6,300 images this package wrote (BIOS bytes valid).

The checksum is written here from the algorithm KallistiOS documents for `mie_eeprom_crc16` (a 32-bit register seeded with
`0xDEBDEB00`, a closing zero round), and `tests/test_eeprom.c` pins vectors for it.

## Coins and switches

```c
#include "kcab/cabinet.h"

KcabCoins coins;
kcab_coins_init(&coins, 99);                        /* credits stop at 99 (0 = no cap short of 65535) */
kcab_coins_update(&coins, meters);                  /* every frame, with the two coin meters; returns credits just added */
if (kcab_coins_spend(&coins, 1)) { /* start a game */ }

KcabSwitches sw = { 0 };
uint32_t pressed = kcab_switches_update(&sw, held, &released);   /* held: KCAB_SW_TEST | KCAB_SW_SERVICE | the game's bits */
```

A `KcabCoins` that is all zero (a static one) is a valid fresh tracker with no cap. The meters are the JVS board's running coin counts, 14 bits. The first reading is a baseline: coins in the meter at
power-on are not credits. A meter that falls (the driver subtracts from it when it gets high, or the board was reset)
re-bases without a credit. `kcab_coins_pulse` takes the board's one-shot insert flags instead; use the meters or the
pulses, not both. `kcab_switch_edges` is the same edge detection as a pure function. A new `KcabSwitches` sees a switch
held at power-on as one press; `kcab_switches_prime` starts from what is held now.

## KallistiOS glue

`kcab/kos.h` (compile `src/kos.c` only with KallistiOS; on a KOS target that is not NAOMI everything reports not ready):

| Function | What it does |
| --- | --- |
| `kcab_kos_ready()` | 1 when port A is wired to the JVS bridge and the MIE device is enumerated. The Maple scan takes a moment after boot: poll this before loading settings. |
| `kcab_kos_eeprom()` | The bridge's EEPROM as a `KcabEeprom`. |
| `kcab_kos_read_inputs(&in)` | The two coin meters, the insert flags and a switch word with `KCAB_SW_TEST` (the system or panel test switch) and `KCAB_SW_SERVICE` (the panel's or either player's service switch), from the state the driver already holds. |
| `kcab_kos_raw_hardware_mode()` | What KOS read before the override (`src/kos_naomi_mode.c`). |

## The save-block adapter

`kcab/save_block.h`: `kcab_sb_storage(&ctx, &eeprom, &format)` gives an `sb_storage` for an `sb_store`. save-block's two
slots are the two copies the layout has room for: slot 0 at byte 44, slot 1 after it. Its records are 16 bytes longer than
the payload, so the payload can be at most **26 bytes** (`kcab_sb_fits` says). The BIOS checksum covers slot 0 only, and
the adapter keeps it right on every write; bytes 0..35 are never touched, and a chip that holds another layout is empty
to the store until its first save replaces it. A game that keeps more than 26 bytes uses `kcab_block_save` with one copy
(up to 84 bytes) instead.

## What runs where

| Part | Runs on | In |
| --- | --- | --- |
| the logic, the adapter and their tests (C, CMake) | Windows, macOS, Linux; CMake 3.20+, Ninja, a C99 compiler | any shell: it is a program |
| the glue's mapping test (`kcab-test-kos-glue`) | the same | built against stand-in headers in `tests/kos-stub` |
| `src/kos.c` against real KallistiOS, a cabinet or an emulator | a KOS toolchain | not run here |
| `build-and-test.ps1` | Windows | PowerShell 5.1 or 7 |
| `build-and-test.sh` | macOS, Linux | Bash or zsh |

Tested at 1.0.0:

| System | Toolchain | Result |
| --- | --- | --- |
| Linux x86-64 (Ubuntu 24.04, in a container) | gcc 13 and clang 18, `-Wall -Wextra -Wpedantic -Werror`; gcc under AddressSanitizer and UBSan | pass (eeprom 1,840 checks, cabinet 36, glue 24, adapter 50) |
| Windows 11, macOS | not run yet | waits for a maintainer machine |
| KallistiOS build, cabinet, emulator | not run: no KOS toolchain in the test container | waits for a KOS machine |

The glue test uses stand-in headers that declare the names and field paths of the public MIE header. It proves the glue's
own mapping; it does not prove the real KallistiOS headers compile against it. Build `src/kos.c` with KOS once before
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
include/kcab/eeprom.h      the block, the checksum, load and save
include/kcab/ram.h         a RAM chip with faults, for tests and for builds with no chip
include/kcab/cabinet.h     coin credits and switch edges
include/kcab/save_block.h  the save-block adapter
include/kcab/kos.h         the KallistiOS glue's API (compile src/kos.c only with KOS)
src/                       eeprom.c ram.c cabinet.c save_block.c kos.c kos_naomi_mode.c
tests/                     one test program for each part, and tests/kos-stub/ (stand-in KOS headers)
examples/consumer/         a project using the package
build-and-test.ps1         build and test, Windows PowerShell
build-and-test.sh          build and test, macOS and Linux Bash
CMakeLists.txt             library `kcab`, `kcab_save_block`, optional `kcab_kos`, the tests
```

## Licence

MIT, see `LICENSE`. KallistiOS has its own licence; nothing of it is copied here.
