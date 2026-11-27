# port-input-harness

Tests a game port's controller handling with simulated controllers, so every port gets the same checks without
anyone plugging pads in, and without a system driver. It uses SDL3's virtual joystick API: a virtual controller is
attached with the vendor and product IDs of a real one (a DualSense, an Xbox pad, a Switch Pro...), SDL treats it as
that controller, and the port's input layer reads it through its real code path.

Nothing in this package knows any game. A port brings an adapter (a small C file, a map file, optionally a small
Python file) and gets the whole suite.

Version 1.0.0. Written in C (C11) with CMake, against SDL 3.2 or later. MIT licence.

## Install

Version 0.1.0 was first released from uppercut-labs/c-kit, and its `port-input-harness@0.1.0` tag stays there, so projects already pinned to it keep building. New releases are tagged here.

This is a C library, so it is not on a package index. A project takes it at a release tag, with CMake `FetchContent`
(CMake 3.20 or later, git, and network at configure time):

```cmake
include(FetchContent)
FetchContent_Declare(port_input_harness
  GIT_REPOSITORY https://github.com/uppercut-labs/game-kit.git
  GIT_TAG        port-input-harness@1.0.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  packages/port-input-harness)
FetchContent_MakeAvailable(port_input_harness)

add_executable(my-port-controllers-test my_port_adapter.c ...)
target_link_libraries(my-port-controllers-test PRIVATE pih)
add_test(NAME my-port-controllers COMMAND my-port-controllers-test
         --profiles "${PIH_PROFILES_DIR}" --map "${CMAKE_CURRENT_SOURCE_DIR}/my-port.map")
```

`examples/consumer/CMakeLists.txt` is that, complete, and the kit's own check builds it. For an offline build set
`-DFETCHCONTENT_SOURCE_DIR_PORT_INPUT_HARNESS=<a checkout of game-kit>`. A project that vendors the folder instead uses
`add_subdirectory(<path>/port-input-harness)`.

The including project gets these CMake variables: `PIH_VERSION`, `PIH_PROFILES_DIR` (the controller profiles),
`PIH_E2E_RUNNER` (`e2e/pih_e2e.py`) and `PIH_E2E_DIR`. Set `PIH_ENABLE_TESTS` to have `pih-selftest` and
`pih-e2e-selftest` in that project's `ctest`.

## What it tests

For each controller profile, the C runner (`pih_main`, `include/pih/pihrun.h`) checks, and prints one table with a
column per profile and a row per check:

| Check | What it proves |
| --- | --- |
| SDL's identity | SDL gives the virtual controller the gamepad mapping and type the profile says |
| the port detects the family | the port's own idea of the controller family (for button prompts) matches the profile |
| nothing pressed: nothing read | no ghost input at rest, on any player |
| every control alone gives what the map says | each physical control, pressed alone, gives exactly the port's state bits the adapter's map names, and nothing once released; controls the map doesn't name must do nothing |
| holding each mapped bit gives it | pressing the controls that give a bit gives exactly that bit |
| every mapped bit at once | all the buttons together |
| unplugged: nothing read | removing the controller leaves nothing held |
| two controllers are players 1 and 2 in plug order | multiple players |
| unplugged with a button held: nothing stuck | hot-unplug mid-press |
| a replacement takes the free player | hot-plug |
| a third controller shares player 1 | (if the port says so) |
| a controller that comes and goes before the port looks leaves nothing | plug and unplug inside one frame |
| a scripted press reads for exactly its frames | the script engine (`plug`, `hold`, `wait`...) |

and the adapter adds its own rows (directions, thresholds, analog mode, menu confirm and back, prompt letters,
rumble, pinning a controller to a player, whatever the port has).

The end-to-end runner (`e2e/pih_e2e.py`) takes a port's scripted run into a match and plays it on controllers of
each profile and some mixed pairs, in the real program, and checks that the run is identical to the same script
played straight into the game (the controller was invisible), that hot-plug in the middle of a fight loses only the
gap, that menu confirm and back behave as the port intends, and that the button prompts follow the controller family.

## Layout

```
include/pih/pih.h      profiles, devices, maps, scripts
include/pih/pihrun.h   the check runner and the adapter interface
src/                   the library (profile.c device.c map.c script.c runner.c)
profiles/              controller profiles as data: *.profile, and the shared control sets (*.inc)
selftest/              selftest.c, a made-up port and its map: the library's own test and a worked example of an adapter;
                       e2e_selftest.py, the end-to-end runner's test
e2e/pih_e2e.py         the end-to-end runner (Python)
examples/              e2e_adapter.py (a made-up port's Python adapter), consumer/ (a project using the package)
docs/                  adapter.md (how to write an adapter), real-controllers.md (what needs a real pad)
build-and-test.ps1     build and test, Windows PowerShell
build-and-test.sh      build and test, macOS and Linux Bash
CMakeLists.txt         library `pih`, tests `pih-selftest` and `pih-e2e-selftest`
```

## What runs where

| Part | Runs on | In |
| --- | --- | --- |
| the library, the runner, the self-test (C, CMake) | Windows, macOS, Linux; needs CMake 3.20+, Ninja, a C compiler and SDL3 3.2 or later | any shell: it is a program |
| `build-and-test.ps1` | Windows | PowerShell 5.1 or 7 |
| `build-and-test.sh` | macOS, Linux | Bash or zsh |
| `e2e/pih_e2e.py`, `selftest/e2e_selftest.py` | Windows, macOS, Linux; Python 3.8 or later, no packages | PowerShell or Bash: it only starts the port's own program |

Tested at 1.0.0:

| System | Toolchain | SDL3 | Result |
| --- | --- | --- | --- |
| Windows 11 | MSYS2 UCRT64 clang 22, `build-and-test.ps1` and `tools/check.ps1` in PowerShell 7 | 3.4.16 | pass |
| macOS 27, Apple silicon | Apple clang 21, `build-and-test.sh` and `tools/check.sh` in the system Bash 3.2 | 3.4.18 (Homebrew) | pass |
| Debian 13 (Linux, arm64, in a container) | gcc 14, `build-and-test.sh` and `tools/check.sh` in Bash | 3.2.10 (distribution package) | pass |

The virtual joystick needs no display or driver, so the tests run in a headless container too.

Build and run the self-test, from this folder:

```powershell
.\build-and-test.ps1                       # Windows, PowerShell
```
```bash
./build-and-test.sh                        # macOS, Linux, Bash
```

Both take a build folder and an SDL3 install prefix, for an SDL3 that is not on the default search path
(`-Build` and `-SdlPrefix` in PowerShell, the two arguments in Bash).

## Controller profiles

A profile is `profiles/<name>.profile`, one `key value` per line:

```
name dualsense
device_name DualSense Wireless Controller
vendor 0x054c
product 0x0ce6
layout gamepad            # gamepad: SDL's standard controls. joystick: a plain DirectInput device
joytype gamepad           # gamepad, unknown, arcade_stick, wheel, flight_stick, arcade_pad
gamepad_type ps5          # what SDL must report (SDL's names: ps5, ps4, xbox360, xboxone, switchpro, steam, standard; several: any one passes)
family playstation        # the prompt family: playstation, xbox, nintendo, steam, other
include gamepad-standard.inc
control touchpad gbutton touchpad
```

`gamepad_type` can list several names because SDL releases differ: the Steam Deck profile says `steam standard`, since
SDL 3.4.18 reports a Steam type for it and 3.4.16 a standard gamepad. A name the installed SDL doesn't know
is skipped.

A joystick also gives `axes`, `buttons` and `hats` counts. Controls are named, and a map refers to them by name:

```
control <name> gbutton <SDL button name>             an SDL gamepad button
control <name> gaxis <SDL axis name> <pressed> <rest>  an SDL gamepad axis, as raw values (a trigger rests at -32768)
control <name> jbutton <n>                           a joystick's button
control <name> jaxis <n> <pressed> <rest>
control <name> jhat <n> up|right|down|left
```

Shipped: `dualsense`, `dualshock4`, `xbox360` (the IDs SDL's XInput driver reports for an XInput pad), `xboxone` (Series),
`switchpro`, `deck` (Steam Deck's own controls, with the back paddles), `dinput` (a generic DirectInput pad with no SDL
mapping: 4 axes, 12 buttons, a hat) and `arcade` (an arcade stick with no mapping). To add one, copy a profile, change the
IDs and names, and add controls; every port then runs it.

Gamepad controls are named by position (`south`, `east`, `west`, `north`, `leftshoulder`, `lefttrigger`, `back`, `start`,
`dpup`..., `leftstick-up`..., `leftstickclick`, `touchpad`, `guide`...). A joystick's are `button0`..., `hat-up`...,
`axis0+`...

## Writing an adapter

[docs/adapter.md](docs/adapter.md) covers the map file, the four C functions (`open`, `close`, `pump`, `read`), the
optional Python adapter for the end-to-end run, and the script format. `selftest/selftest.c` and
`examples/e2e_adapter.py` are complete worked examples.

```c
PihAdapter a = {0};
a.name = "my-port-test";
a.open  = my_open;    /* a fresh input layer, nothing plugged in, only virtual controllers counted */
a.close = my_close;
a.pump  = my_pump;    /* handle SDL's events and decide one frame of input */
a.read  = my_read;    /* a player's state as bits after pump */
a.players = 2;
return pih_main(&a, argc, argv);   /* --profiles <dir> --map <file> [--only a,b] [-v] */
```

## What needs a real controller

The virtual joystick replaces the device, not SDL's view of it: HID reports, Windows' own controller stacks, rumble
motors and Bluetooth behaviour need real hardware. [docs/real-controllers.md](docs/real-controllers.md) lists them.

## Licence

MIT, see `LICENSE`.
