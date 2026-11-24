# Writing an adapter

An adapter tells the runner what is specific to one port. It is three things, all of them in the port's own repository, not in this package.

## 1. A map: `<port>.map`

What each control gives, in the port's own state bits (any 32-bit meaning):

```
# who      control        bits    [primary]
gamepad    south          0x0040  primary
joystick   button1        0x0040  primary
deck       l4             0x00C0
```

`who` is a layout (`gamepad`, `joystick`) or a profile name, which wins. `primary` marks the control a script presses to
give those bits (the d-pad, not the stick that also steers). A control the map doesn't name must give nothing, and the
runner checks that. See `selftest/selftest.map`.

## 2. A C adapter: the port's input layer behind four functions

```c
PihAdapter a = {0};
a.name = "my-port-test";
a.open  = my_open;    /* a fresh input layer, nothing plugged in, only virtual controllers counted: void *(*)(void *user) */
a.close = my_close;
a.pump  = my_pump;    /* handle SDL's events and decide one frame of input */
a.read  = my_read;    /* a player's state as bits after pump */
a.family = my_family; /* optional: the family the port detects, as a profile's family string */
a.players = 2;
return pih_main(&a, argc, argv);   /* --profiles <dir> --map <file> [--only a,b] [-v] */
```

Rules specific to the port go in `a.extra` (called for each profile) and `a.extra_all` (called once), which add rows with
`pih_check(run, profile, "what", ok, "why it failed %d", x)`. They use the library: `pih_attach`, `pih_press`,
`pih_hold`, `pih_stick`, `pih_raw_axis`, `pih_rumble_last`. `selftest/selftest.c` is a complete adapter in 170 lines.

A port whose input layer counts real controllers needs a switch that leaves them out for the test, so a pad plugged
into the test machine can't join the run.

## 3. A Python adapter for the end-to-end run (optional)

`e2e/pih_e2e.py --adapter my_adapter.py --profiles profiles --opt key=value`. The module defines `Adapter(opts)`:

| Method | Returns |
| --- | --- |
| `baseline(work)` | `Run(ok, state, reads)`: the port's scripted run played straight into the game: its state on every tick as bytes (a hash log), and `{(tick, player): input}` |
| `with_controllers(work, tag, profiles, plugs)` | the same, with the script played on virtual controllers of those profiles in slots 0 and 1; `plugs` is `[(tick, slot, profile or None)]`, plug and unplug during the run |
| `menu_cases()` and `menu(work, tag, profile, case)` | optional: which buttons leave a menu, and whether the run backed out |
| `has_prompts()`, `picture(work, tag, profile=None, family=None)` and `distinct_families()` | optional: a screen with the controller plugged in (or its family forced) as bytes, for the prompts, if the port draws any |

This needs the port to take the script through virtual controllers in a test-only mode: the port's program reads the
same profiles and map, attaches `pih_attach` devices, and each frame calls `pih_hold(device, map, wanted_bits)` for what
the script wants held, then lets its input layer read as it normally does. Keep the code that does this in one small
file of the port, guarded so a build without the harness still compiles.

`examples/e2e_adapter.py` is a complete one for a made-up port, and `selftest/e2e_selftest.py` runs it. The script itself is the port's own "drive into a match" script, in whatever format it has.

## Scripts

The library has its own small script format for sequences that need no game:

```
plug 0 dualsense
wait 1
hold 0 south 3          # pressed for 3 frames
stick 0 left 255 128    # bytes, 0x80 centred
unplug 0
```

`pih_script_load`, `pih_script_frame(script, rig)` once per frame.
