# mac-window-tools

Three small jobs on macOS windows that emulator and game capture scripts keep needing, as one command line tool: list windows by owner, find a process's window (its id and title), and capture a process's window to a PNG. A Swift package with no dependencies.

It reads a window by its id. It never raises, activates or focuses a window, and it does not send input. It is the macOS window finder for [`@uppercut-labs/emulator-session`](../emulator-session), and it works by itself.

Version 1.0.0. MIT licence.

## Tested at 1.0.0

| System | Toolchain | Result |
| --- | --- | --- |
| macOS 27.0 (Apple M6 Mac mini) | Swift 6.4.2, `swift build -c release` and `swift test` | builds with no warnings; 11 tests pass |
| the same, `help`, `list`, `find` | `.build/release/mac-window-tools` in an ssh session | pass: `list` printed 24 on-screen windows with their titles, `find --owner Finder` printed the Finder window's id and title, `capture --pid 999999 --wait 1` exited 1 with "no matching window appeared in time" |
| macOS 27.0 (a second Apple M6 Mac mini) | the same build and tests | pass |
| the same, `capture --pid <Terminal> --out x.png` in an ssh session | the same build | pass: it wrote an 874 x 499 PNG, the window's own size |

`capture` needs a logged-in desktop session and Screen Recording access. It asks for the window's own pixel size. Before that, it took ScreenCaptureKit's default 1920 x 1080 for every window.

## What runs where

macOS 14 or later: capture uses ScreenCaptureKit's single-window screenshot. `list` and `find` use the window list from CoreGraphics. The `WindowTools` library target (the matching rules, the output format and the argument parser) has no system dependency, so its tests need no display and no permission; the `mac-window-tools` executable builds as a stub that exits 1 on other systems.

Screen Recording access is needed for `capture`, and macOS only gives other programs' window titles to a caller that has it, so `find` and `list` print an empty title without it.

## Install

A Swift package is taken at a release tag, from a checkout of this repository:

```sh
git clone https://github.com/uppercut-labs/game-kit.git
cd game-kit/packages/mac-window-tools
swift build -c release
.build/release/mac-window-tools help
```

Or as a SwiftPM dependency (the package is in a subfolder, so a project that wants the library vendors the folder). The command line tool is the supported way to use it.

## Commands

```sh
# One line per window: id|pid|owner|layer|onscreen|x,y,w,h|title
mac-window-tools list --owner ares --on-screen
mac-window-tools list --pid 4242

# The largest normal on-screen window of a process (or an owner), as "<id> <title>".
# Exit 1 and no output when there is none yet, so a caller can retry.
mac-window-tools find --pid 4242
mac-window-tools find --owner Flycast --min-size 300x160

# Wait up to 20 seconds for the process's window, then write it to a PNG.
mac-window-tools capture --pid 4242 --out shot.png --wait 20
```

Options:

| Option | Meaning |
| --- | --- |
| `--owner TEXT` | The owner name contains TEXT |
| `--pid N` | The window belongs to process N |
| `--layer N`, `--on-screen` | `list` only: keep one layer, keep windows that are on screen. `find` and `capture` always want layer 0 and on screen |
| `--min-size WxH` | `find` and `capture`: the smallest window to accept. Default 300x160 |
| `--title-contains TEXT` | The title contains TEXT. Use it where one program has windows of its own besides the game's, and the game window's title carries a marker. This replaces a program name that was once written into the capture code |
| `--out FILE` | `capture` only: where to write the PNG |
| `--wait SECONDS` | `capture` only: how long to wait for the window. Default 20 |

The `list` title is the last field because a title can hold `|`: split on the first six bars and take the rest. The format is not the one of the older single-file `winid` helpers (owner first, title third), so a script that parsed those needs the new field order.

`capture` takes only the window of a process id you give it, so a second copy of the same program cannot be captured by mistake.

## Use it from emulator-session

```ts
capture: {
  kind: 'macos-screencapture',
  windowInfoCommand: ['/path/to/mac-window-tools', 'find', '--pid', '{pid}'],
}
```

`find` prints `<id> <title>`, which is the shape that preset reads. To capture with this tool itself instead of `screencapture`, use `kind: 'command'` with `argv: ['/path/to/mac-window-tools', 'capture', '--pid', '{pid}', '--out', '{out}']`.

## What it replaces

Small helpers of the same family that several projects each carry: a CoreGraphics window list filtered by owner, a window id and title lookup by process, a one-line tool that prints the first on-screen normal window of an exactly named app, and a ScreenCaptureKit one-window capture with an emulator-specific title test written into it. The one-line tool is `find --owner NAME --min-size 0x0`, except that `find` matches part of the owner name and takes the largest window, not the first. This package keeps the three jobs and makes the title test the `--title-contains` option. It does not carry over the H.264 recording mode of the capture helper, or the accessibility driver (`AXUIElement`, keyboard and mouse input) found in an app-specific tool: those are different jobs.

## Working on it

On a Mac:

```sh
swift build
swift test
```

The tests cover the matching rules, the largest-window choice with its tie-break, the list and find lines (including a title with `|`) and the argument parser. A capture needs a window and Screen Recording access for the process that runs it, so it is checked on a Mac, not by `swift test`.

## License

MIT
