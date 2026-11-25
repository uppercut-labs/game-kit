# @uppercut-labs/emulator-session

Runs an emulator for a headless capture, the way a build-run-capture script does, without the machine details baked in. You give it the emulator program, a profile folder and the game files. It starts the emulator with its own settings, muted and allowed to run in the background, reads its output line by line with the noise taken out, takes window pictures at the seconds you name (never sooner than the emulator needs to boot), and at the end stops the process it started and no other.

How to launch each emulator is data, not code: a profile lists the arguments, how settings reach the emulator, and what mutes it. Profiles for ares, Ymir, Flycast, redream, PPSSPP, PCSX2 and the DuckStation regression runner come with the package, and a profile is a plain object, so adding another emulator is a data change.

```sh
npm install @uppercut-labs/emulator-session
```

## What runs where

TypeScript compiled to ESM JavaScript, with no runtime dependencies. It needs Node 18 or later. The session, the line reader, the settings writers and the stop logic run on macOS, Linux and Windows. Two parts have limits:

- Finding processes by command line (`ownedPids`, `stopOwned`) reads `/proc` on Linux and runs `ps` on macOS. It does not work on Windows.
- Window pictures come from a command or function you supply. The package ships none, so it carries no OS-specific capture code. The macOS preset builds a `screencapture` command around a window id your finder prints. The sibling package `mac-window-tools` in the same repository is such a finder for macOS.

Everything that belongs to one machine is yours to pass: `executable`, `profileDir`, `installDir`, the content files and any seed files. No profile names a path.

## Example

```ts
import { runAndCapture } from '@uppercut-labs/emulator-session';

const result = await runAndCapture(
  {
    profile: 'ares',
    system: 'n64',
    executable: process.env.ARES_BIN!,
    content: ['build/game.z64'],
    // ares keeps nothing in a profile folder here, so it needs none.
    noise: [/^Vulkan: /],
    capture: {
      kind: 'macos-screencapture',
      windowInfoCommand: [process.env.WINDOW_FINDER!, 'find', '--pid', '{pid}'],
    },
    rawLogPath: 'out/ares.log',
    filteredLogPath: 'out/game.log',
  },
  { totalSec: 20, captures: [{ atSec: 6, file: 'out/shot_6s.png' }, { atSec: 12, file: 'out/shot_12s.png' }] },
);

console.log(result.captures.map((capture) => capture.ok), result.lines.slice(-5));
```

`runAndCapture` is the whole loop. For anything else, `launchEmulator` returns a session you drive yourself:

```ts
import { launchEmulator } from '@uppercut-labs/emulator-session';

const session = await launchEmulator({ profile: 'flycast', executable, content: [image], profileDir, env: { HOME: profileDir } });
session.onLine((event) => { if (event.line.includes('FAIL')) console.error(event.line); });
await session.waitForLine(/boot complete/, { timeoutMs: 20_000 });
const captures = await session.captureAt([{ atSec: 8, file: 'out/title.png' }]);
const exit = await session.stop();
```

## Launch options

| Option | What it does |
| --- | --- |
| `profile` | A built-in profile id or a profile object |
| `executable`, `commandArgs` | The emulator program, and arguments placed before the profile's own (an interpreter, a wrapper). `executable` is left out for a `portable` profile |
| `content` | The files to run. They must exist unless `checkContent` is false (it is false for headless runners) |
| `system` | A system name the profile knows (ares: `n64`, `n64dd`, `64dd`, `32x`, `gba`, `psx`) |
| `profileDir` | A folder owned by this run. Created when missing. Required by profiles that keep settings or state there |
| `refuseProfileDirs` | Folders that must never be the profile folder, or contain it, such as the emulator's real configuration folder |
| `seedFiles` | `{ from, to, overwrite?, optional? }`: files copied into the profile folder first. A seed never replaces a file the profile already has unless `overwrite` is true, and `to` may not leave the folder |
| `settings` | Overrides, applied last. Flags and files take them as the emulator spells them (`Audio/Mute`, `config:Dreamcast.Cable`, `EmuCore/GS/OsdShowMessages`) |
| `mute` (default true), `background` (default true) | Apply the profile's mute settings and its let-it-run-unfocused settings |
| `extraArgs`, `vars` | Fill a profile's `{extra}` slot and `{name}` placeholders |
| `env`, `cwd` | Environment added to the process (`undefined` removes a variable) and its working folder |
| `noise`, `replaceNoise` | Lines to drop, added to the profile's own, or in place of them |
| `maxLineChars`, `keepLines` | Line reader bounds (4096 characters, 400 lines) |
| `onLine` | Called for every line that is not noise, from the first one (attached before the process starts, so none is missed) |
| `rawLogPath`, `filteredLogPath`, `maxLogBytes` | Write every line, and the lines that are not noise, to files of at most 16 MiB each |
| `capture`, `bootLatencySec` | How to take pictures, and the seconds before the first window can be captured (the profile's own when not given) |
| `graceMs`, `quit`, `onStop` | Stop behaviour: wait this long (500 ms) after SIGTERM before SIGKILL; a command that asks the emulator to quit first; a hook that cleans up what the process id cannot |

A session has `pid`, `argv`, `muted`, `background`, `warnings`, `lines()`, `tail(n)`, `stats`, `onLine(fn)`, `waitForLine(pattern, { timeoutMs })`, `waitUntil(sec)`, `captureAt(captures)`, `stop()` and `exited`.

## What the guarantees mean

- **Muted by default.** The profile's mute settings are applied, and its mute environment (`SDL_AUDIODRIVER=dummy` where it helps). When an emulator has no known launch-time way to mute, the session says so: `muted` is false and `warnings` names it. It does not pretend.
- **Background running.** The profile's setting that stops an emulator pausing or dropping input when it has no focus (ares `Input/Defocus=Allow`, PPSSPP `General/PauseOnLostFocus=False`) is applied. Emulators with no known setting are reported in `warnings`.
- **Never takes focus.** Nothing in this package raises, activates or focuses a window, and a capture command or function you pass must not either. A capture that needs its window in front captures whatever is in front. The macOS preset uses `screencapture -l <window id>`, which reads one window by id. For Windows, use `PrintWindow` with `PW_CLIENTONLY | PW_RENDERFULLCONTENT` (3), or Windows Graphics Capture if a GPU-rendered window comes back black; neither needs the window in front, so do not call `SetForegroundWindow` first. The emulator itself may still come to the front when the operating system starts it. This package cannot change that.
- **Captures past boot latency.** `captureAt` takes times in seconds after the launch. A capture asked for earlier than the boot latency waits for it and is marked `delayed`. ares needs about 3.5 to 4.5 seconds before its window exists, and its profile says 4.5.
- **Stops only the launched process.** `stop()` runs the quit command if you gave one, sends SIGTERM to the process the session started, and SIGKILL after `graceMs`. It never looks processes up by name, so a second copy of the same emulator, yours or someone else's, is left alone. A process the emulator starts for itself is not signalled; use `ownedPids` and `stopOwned` when a launcher starts the emulator for you.
- **A failed capture is a result, not an exception.** Each capture reports `ok`, the window id and title, `takenAtSec` and an error. A capture tool that exits 0 without writing a file is a failed capture.

## Launching through another program

Some emulators are started through a launcher that returns at once, such as the macOS `open` command, so the process id you were given is not the emulator's. Give the launch a marker that only this run's command line holds (for example `--appendconfig=<your private profile>/overlay.ini`), then:

```ts
import { ownedPids, stopOwned, waitFor } from '@uppercut-labs/emulator-session';

const query = { executable: '/path/to/Emulator', marker: profileDir };
const [pid] = await waitFor('the emulator', async () => { const pids = await ownedPids(query); return pids.length === 1 && pids; });
// ... capture pid ...
await stopOwned(query); // SIGTERM, then SIGKILL after 2.5 s; only processes whose command line holds the marker
```

`ownedPids` matches a command line that is the executable (bare, or followed by a space and arguments, so a path with spaces works) and holds the marker. The marker is required and cannot be empty, because an empty one would match every instance of the program.

## Built-in profiles

| Profile | Settings go in as | Mute | Background | Notes |
| --- | --- | --- | --- | --- |
| `ares` | `--setting Key=Value` flags | `Audio/Mute` | `Input/Defocus=Allow` | `system` picks the console name and, for Nintendo 64 homebrew, `General/HomebrewMode`. Drops the "Stalled compile" and "Resizing output" lines. A Mega 32X run flushes its save RAM only on a clean quit: pass `quit` |
| `ymir` | none (its profile folder holds its state) | none known | none known | `-p <profileDir> -d <disc>`. Seed its state files with `seedFiles` |
| `flycast` | `-config section:key=value` flags | `audio:backend=null` | none known | Starts windowed. Set `HOME` in `env` to keep its data out of the real home folder |
| `redream` | edits `redream.cfg` (`key=value`) in the profile folder | none known | none known | Runs from its own copy of the program and flash file, taken from `installDir`. Windows only |
| `ppsspp` | an overlay file passed as `--appendconfig=` | `Sound/Enable=False` and `SDL_AUDIODRIVER=dummy` | `General/PauseOnLostFocus=False` | `HOME` points at the profile folder. Its remote debugger is a separate WebSocket client and is not part of this package |
| `pcsx2` | edits `PCSX2/inis/PCSX2.ini` under the profile folder (its data path) | none known | none known | `-datapath <profileDir> -batch -nogui`, then `extraArgs` (`-elf <file>`, `-gameargs ...`, `--`), then `content`. The profile folder needs a BIOS and a seed settings file |
| `duckstation-regtest` | none | not applicable | not applicable | Headless: writes a PNG of a frame into `dumpDir` and opens no window. Pass `frames` and `dumpDir` (and `renderer`, default `Software`) in `vars`. `captureAt` is refused |

The values come from launch scripts that ran. A profile that says "none known" has no launch-time setting in the scripts; it is not a claim that none exists. To add or fix one, copy a profile object, change it and pass it as `profile`. The shape is `EmulatorProfile`: `args` (with `{settings}`, `{content}`, `{extra}` slots and `{name}` placeholders), `settings` (how settings are delivered), `defaults`, `background`, `mute`, `unmute`, `env`, `muteEnv`, `systems`, `vars`, `noise`, `bootLatencySec`, `portable` and `headless`.

## The line reader

`LineReader` is also exported for use without a session. It splits stdout and stderr into lines, keeping a partial line per stream so their chunks never splice. A line longer than `maxLineChars` is cut and flagged, and the rest of that line is dropped, so a stream with no newline cannot grow memory. Only the latest `keepLines` lines are remembered. Noise rules are substrings, regular expressions, or `{ regex, flags }` objects that survive JSON. Blank lines are skipped.

## Other pieces

| Export | What it does |
| --- | --- |
| `applyIni(text, settings)`, `applyKeyValue(text, settings)` | The text edits behind file settings. An INI key is `Section/Name`, split at the last slash; existing values are replaced in place and the file's spacing and line ending are kept |
| `takeCapture(config, request)` | One capture, outside a session |
| `runCommand(command, args)` | Runs a command without a shell and returns its output |
| `privateProfileDir(base, prefix)` | Makes a new empty folder for one run |
| `profiles`, `getProfile(id)` | The built-in profiles |

## Working on it

From the repository root:

```sh
npm install
npm run check
```

The tests run without an emulator, a display or a game. A small Node script stands in for the emulator: it records its arguments and environment, prints the lines a test asks for, ignores SIGTERM on request, and stands in for a window finder and a capture tool. They check the exact arguments built for each profile, settings files, seeding, the line reader's bounds, capture timing and failure reporting, and that stopping signals only the launched process while a second process of the same kind survives.

## License

MIT
