# port-sdl-input

The input layer of a game port, on SDL3: every keyboard, gamepad and joystick open at once, positional default mapping
with no setup, players assigned as devices connect, device pinning, SOCD cleaning, an analog switch, one-button remap
capture saved as text, controller family detection for button prompts, rumble. The port reads each player's pad once a
frame, as a word of its own bits.

Nothing in this package knows a game. The port supplies its buttons as data (a **layout**: a name, a settings key, a bit
in the port's pad word and default bindings for each button), and everything a game decides stays in the game: what a
button means, which screen is a menu, what a macro presses.

Version 1.0.0. Written in C (C11) with CMake, against SDL 3.2 or later. MIT licence.

## Install

This is a C library, so it is not on a package index. A project takes it at a release tag, with CMake `FetchContent`
(CMake 3.20 or later, git, and network at configure time):

```cmake
include(FetchContent)
FetchContent_Declare(port_sdl_input
  GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git
  GIT_TAG        port-sdl-input@1.0.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  packages/port-sdl-input)
FetchContent_MakeAvailable(port_sdl_input)

target_link_libraries(my-port PRIVATE psi)       # the SDL layer; psi_core alone has no SDL
```

`examples/consumer/CMakeLists.txt` is that, complete (with `port-settings` beside it), and `examples/consumer/main.c` is
a small port using both. For an offline build set `-DFETCHCONTENT_SOURCE_DIR_PORT_SDL_INPUT=<a checkout of game-kit>`.
A project that vendors the folder instead uses `add_subdirectory(<path>/port-sdl-input)`. The including project gets the
CMake variable `PSI_VERSION`. Options, set before the package is included:

| Option | Default | What it does |
| --- | --- | --- |
| `PSI_ENABLE_TESTS` | off (always on standalone) | puts `psi-pad-test` and `psi-test` in the including project's `ctest` |
| `PSI_SDL_TARGET` | `SDL3::SDL3` | the CMake target that gives SDL3's headers and library, for a build that names its own (`SDL3::SDL3-static`, a target the parent made) |
| `PSI_REQUIRE_SDL` | on | off: a project that also builds without SDL (a headless tool that wants only the pad word) gets `psi_core` and no `psi` when SDL3 is not found |

The settings can live anywhere: the layer reads and writes them through three callbacks (`PsiStore`). The three functions
of [`port-settings`](../port-settings) fit, and nothing else of it is needed.

## Use

```c
#include "pset/pset.h"      /* or any store */
#include "psi/psi.h"

/* the port's pad word: its own bits (here the PlayStation 1 order) */
static const uint32_t BITS[PSI_PS_COUNT] = { 0x0001, 0x0002, /* ... one per button, see layout.h ... */ };

PsiSlot slots[PSI_PS_COUNT];
psi_playstation_slots(slots, BITS);              /* names, keys and default bindings for the sixteen buttons */
slots[PSI_PS_L3].defaults = "none";              /* the port changes what it likes */

PsiLayout layout = { .slots = slots, .nslots = PSI_PS_COUNT,
                     .up = BITS[PSI_PS_UP], .down = BITS[PSI_PS_DOWN],
                     .left = BITS[PSI_PS_LEFT], .right = BITS[PSI_PS_RIGHT],
                     .remap_order = psi_playstation_remap_order, .nremap = psi_playstation_remap_count };
PsiOptions opt = { .layout = &layout, .name = "my-port",
                   .store = { settings, pset_store_get, pset_store_set, pset_store_save } };
PsiInput *in = psi_new(&opt);                    /* SDL_Init(SDL_INIT_GAMEPAD) first */

/* in the event loop */
if (psi_event(in, &e)) { /* a remap capture took it */ }

/* once a frame */
PsiPad pads[PSI_PLAYERS];
psi_pads(in, pads);                              /* pads[0].buttons is the port's word, active high */
```

A port that makes its own buttons (a Genesis pad, an arcade panel) writes its own `PsiSlot` table and uses none of the
PlayStation helpers. `docs/porting.md` walks through a layout, the extras and a remap screen.

## What it does

| Part | How it behaves |
| --- | --- |
| Devices | Every gamepad and joystick SDL reports is opened, including the ones already connected when the layer starts. Up to 16. |
| Players | Two (`PSI_PLAYERS`). A device goes to player 1 when it connects, then player 2; a third and later go to player 1 too. The keyboard and the on-screen controller (`psi_set_touch`) are player 1's. `psi_swap_players` swaps the devices. |
| Pinning | `pN.device = <SDL GUID>` in the settings (or `psi_pin_device`) puts that device on player N whatever connects first. `psi_describe` prints every device's GUID. |
| Gamepads | By position, so a DualSense, a DualShock 4, an Xbox pad, a Switch Pro and the Steam Deck's own controls map the same way: SDL's south, east, west, north, shoulders, triggers (past a quarter), d-pad, left stick (past half way), back, start, stick clicks, and the Deck's back buttons (L4 R4 L5 R5). Which slot each one gives is the layout's default bindings. |
| Plain joysticks | A joystick SDL has no gamepad mapping for (many arcade sticks): buttons 0 to 9, the first hat and the first two axes, the DirectInput order PlayStation adapters use. |
| Bindings | Text, in the key `pN.<slot key>`: `key:Z, pad:south, pad:lefttrigger+, pad:l4, joy:b1, joy:h0.up, joy:a0-`. A key whose SDL name holds a comma or `#` is written by number (`key:sc54`). |
| SOCD | `socd = hitbox` (the default): left+right cancel, up+down gives up. `socd = off` passes them on. |
| Analog | A player's pad is digital unless `pN.analog = on` (`psi_set_analog`, `psi_toggle_analog`); then `PsiPad` has the sticks' bytes, and a key, d-pad or hat steers the left stick while it is centred. |
| Remap | `psi_remap_begin(in, player, slot)` takes the next key, gamepad button or axis, or joystick button, hat or axis, replacing that slot's binding of the same kind and keeping the others; Escape cancels. `psi_remap_start(in, player)` asks for each slot in the layout's order, with a prompt line (`psi_remap_prompt`); Escape keeps a slot and goes on. A capture ignores input for 300 ms so the press that started it does not count. While it runs, `psi_pads` gives nothing. |
| Family | The family of the last device used (`psi_family`), or `prompts = playstation`, `xbox`, `steam`, `nintendo`, `other`, `keyboard` (`psi_force_family`). `psi_prompt_letters` gives the letter on the button or key each slot is bound to (A, B, X, Y by SDL's label; none for a PlayStation pad's shapes). |
| Rumble | `psi_rumble(in, player, small, large, ms)` on the player's gamepads, unless `rumble = off`. |
| Extras | A slot whose `mask` is 0 gets its bits from the port while it runs (`psi_set_slot_mask`): macros that press the port's own buttons, as the game's button config says. |
| Swap | `psi_set_swap(in, player, on)` exchanges the layout's `swap_a` and `swap_b` bits while on (confirm and cancel in menus). A press keeps the meaning it had when it went down until it is released. |
| Window focus | Losing it lets go of every key. |
| Keyboard state | From the events given to `psi_event` (the default), or from SDL's keyboard state (`psi_keys_from_sdl(in, 1)`). |

Settings keys it reads: `pN.<slot key>` (and a slot's `old_key` when that is not set), `pN.analog`, `pN.device`, `socd`,
`rumble`, `prompts`. It writes `pN.<slot key>` and `pN.analog`. `PsiOptions.log` takes the log lines (a device
connecting, a remap) instead of stderr; `only_virtual` counts only SDL's virtual controllers, for tests.

`psi_core` (no SDL) holds `PsiPad`, `psi_socd_clean`, `psi_steer_stick`, the swap latch, the family enum and the
PlayStation table, so a port's renderer, its replay tools or its headless host can use the same pad word.

## Tests

`psi-test` runs two made-up ports on SDL's virtual controllers (no window, GPU or hardware): the first has sixteen
buttons in one bit order; the second has the same sixteen in another, other default keys, four macros and a menu swap.
It checks every button by position on a DualSense, a DualShock 4, an Xbox Series pad, the Steam Deck's controls and a
Switch Pro, the keyboard beside the pads, the keyboard and two pads at once, players assigned as devices connect,
hot-plug, swapping and pinning, SOCD, analog mode, an arcade stick, the on-screen controller, a remap of one button and
of a whole run (saved as text and read back, with a comma key), the family and prompt letters, rumble, the swap, macros
that follow the game's own buttons, an old settings key, and a layout the layer must refuse. `psi-pad-test` covers the
part without SDL. [`port-input-harness`](https://github.com/uppercut-labs/c-kit/tree/main/packages/port-input-harness)
is the companion for a port's own test: it attaches virtual controllers with the IDs of real ones (a DualSense, an Xbox
pad, a Switch Pro, a Steam Deck...) from data files and drives the port's adapter and its end-to-end run.

## What runs where

| Part | Runs on | In |
| --- | --- | --- |
| the library and its tests (C, CMake) | Windows, macOS, Linux; CMake 3.20+, Ninja, a C compiler and SDL3 3.2 or later | any shell: it is a program |
| `build-and-test.ps1` | Windows | PowerShell 5.1 or 7 |
| `build-and-test.sh` | macOS, Linux | Bash or zsh |

Tested at 1.0.0:

| System | Toolchain | SDL3 | Result |
| --- | --- | --- | --- |
| Linux x86-64 (Ubuntu 24.04, in a container) | gcc 13, `build-and-test.sh` in Bash, dummy video; also under AddressSanitizer and UBSan | 3.4.20 (built from its release tag) | pass |
| Windows 11 Pro (10.0.26300) | MSYS2 UCRT64 clang 22.1.8, `tools\check-c.ps1` in PowerShell 7 | MSYS2 package | pass |
| macOS 27.0 (Apple M6) | Apple clang 21.0.0, `tools/check-c.sh` | Homebrew | pass |

The virtual joystick needs no display or driver, so the tests run in a headless container too. A real pad's HID reports,
rumble motors and Bluetooth behaviour need real hardware.

Build and run the tests, from this folder:

```powershell
.\build-and-test.ps1                       # Windows, PowerShell
```
```bash
./build-and-test.sh                        # macOS, Linux, Bash
```

Both take a build folder and an SDL3 install prefix, for an SDL3 that is not on the default search path (`-Build` and
`-SdlPrefix` in PowerShell, the two arguments in Bash).

## Layout

```
include/psi/psi.h      the input layer: create, events, pads, remap, family, rumble
include/psi/layout.h   what the port supplies: slots, the layout, the store; the PlayStation slot table
include/psi/pad.h      the pad word, SOCD, stick steering, the swap latch (no SDL)
include/psi/family.h   the controller family (no SDL)
src/                   psi.c (the layer), bind.c (bindings as text), pad.c, playstation.c
tests/                 psi_test.c (virtual controllers), pad_test.c (no SDL)
examples/consumer/     a small port using this package and port-settings
docs/porting.md        writing a layout, extras, a remap screen
build-and-test.ps1     build and test, Windows PowerShell
build-and-test.sh      build and test, macOS and Linux Bash
CMakeLists.txt         libraries `psi_core` and `psi`, tests `psi-pad-test` and `psi-test`
```

## Not in this package

A settings screen, a font or any drawing; a game's own button meanings, macros or menus (the port gives the layer bits and
turns a swap on); more than two players; a hotkey table (F-keys, Select held to open a panel): the port decides those and
reads `psi_raw` for a panel's navigation.

## Licence

MIT, see `LICENSE`.
