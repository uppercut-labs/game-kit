# uppercut-port-proof

The small tools a proof run of a recompiled game port keeps needing: turn the frame dumps (PPM) into PNG files, crop and
enlarge a region, compare two runs frame by frame, measure the audio the port wrote, turn a list of button presses into
input for the port or for an emulator, and drive a port that has a debug socket. Standard library only.

```sh
pip install uppercut-port-proof
```

Requires Python 3.10 or newer. Nothing here knows a game, a console's BIOS or a disc: a port's own commands, its pad
bits and its paths are arguments.

## Commands

```sh
uppercut-port-proof ppm2png out/png run/frame_*.ppm            # PNGs, and each frame's MD5
uppercut-port-proof ppm-crop run/frame_0300.ppm out/hud.png 8 8 120 40 4
uppercut-port-proof md5-compare run-a run-b report.txt         # frame_*.ppm of A against the same names in B
uppercut-port-proof wav-stats run/audio.wav --silence-db -66.2 --json
uppercut-port-proof presses2script presses.txt 9000 script.txt --shot-every 300
uppercut-port-proof drive --presses presses.txt --until 9000 --shot-every 300 --out run/shots --port 4370 --quit
```

Every command exits 0 on success and 2 on a file or argument it cannot use, with the reason on stderr. `md5-compare` also
takes `--strict` (exit 1 unless every frame matches), and `ppm2png` and `ppm-crop` take `--stored` (see below).

## Python

```python
from uppercut_port_proof import (DebugClient, compare_frame_dirs, parse_presses, read_ppm, run_presses,
                                 stats_for_file, to_script, write_png)

pic = read_ppm("run/frame_0300.ppm")                        # Ppm(width, height, pixels): 8-bit RGB
write_png("frame.png", pic.width, pic.height, pic.pixels)

result = compare_frame_dirs("run-a", "run-b")               # FrameComparison: rows, same, different, missing
print(result.summary(), result.identical)

stats = stats_for_file("run/audio.wav", silence_db=-66.2)   # a dict: see the keys below
```

### Frames and PNG

| Name | What it does |
| --- | --- |
| `read_ppm(path)`, `parse_ppm(bytes)` | A binary PPM (P6) of 8-bit samples. A comment in the header is skipped. A file with fewer pixel bytes than its header promises, a maxval over 255 or another magic raises `PpmError`; bytes after the pixels are ignored. |
| `write_ppm(path, pic)`, `encode_ppm(pic)` | The inverse. |
| `crop(pic, x0, y0, x1, y1)`, `enlarge(pic, k)` | A region (the end is not included), and each pixel repeated `k` times across and down. |
| `md5_of(path)` | The MD5 of a file, to tell two dumps apart. |
| `encode_png(w, h, pixels, channels=3, compress=True, level=6)`, `write_png(path, ...)`, `ppm_to_png(source, target)` | 8-bit RGB or RGBA with `zlib`. |

`compress=False` writes stored deflate blocks, with the same bytes the C package [`minimal-png`](../minimal-png) writes for the
same image (the tests pin one file in both), so a Python tool and a port can be compared with `cmp`.

### Frame comparison

`compare_frame_dirs(a, b, prefix="frame_", suffix=".ppm")` MD5s every matching file of folder `a` against the same name in `b`.
`FrameComparison.rows` are `FrameRow(name, md5_a, md5_b)` (`md5_b` is None when `b` lacks the file), with `status` of `same`,
`different` or `missing`; `only_in_b` lists the frames `a` lacks. `lines()` are the report lines, one per frame and then the
summary `N frames in A: x identical, y different, z missing in B`.

### Audio

`stats_for_file(path, silence_db=-66.2)` reads a 16-bit PCM WAV and returns a dict:

| Key | Meaning |
| --- | --- |
| `file`, `sample_rate`, `channels`, `frames`, `seconds` | the format and length (seconds rounded to 2 places) |
| `peak`, `peak_dbfs`, `rms_dbfs` | the largest absolute sample (32768 for a full-scale negative), and both in dBFS (`-Infinity` for digital silence) |
| `full_scale_samples`, `total_samples`, `full_scale_percent` | samples at 32767 or beyond, out of all samples of all channels |
| `silence_threshold_dbfs` | the threshold used: `silence_db` |
| `one_second_windows`, `silent_one_second_windows` | whole seconds, and those whose largest sample (any channel) is under the threshold |
| `silent_stretches_1s_or_more` | `[{"start_s", "length_s"}]`, found at 100 ms resolution with the same test |
| `longest_silence_s` | the longest stretch found, of any length |
| `header_repaired` | present and true only when the header's data length did not fit the file |

The header of a file from a run that was killed is never brought up to date: the data length reads 0, or more than the file
holds. The data then runs to the end of the file, so what was written is still measured (and `header_repaired` says so). A
half frame at the end is dropped. Only 16-bit PCM is handled; anything else raises `WavError`. The JSON uses Python's
`-Infinity` for digital silence, which Python reads back and strict JSON parsers do not.

### Press files

A press file is plain text, one press a line: `frame:button[+button...][:frames]`. The press starts when the game's frame counter
reaches `frame` and is held for `frames` frames (the default is yours: 4 for `drive`, 6 for `presses2script`). A `#` starts a
comment, and a line that begins with `p2 ` is left out (it belongs to a second pad).

```
# open the menu, then confirm
300:start
360:cross+up:12
```

`parse_presses(text, default_hold=4, skip_prefixes=("p2 ",))` gives `Press(frame, buttons, hold)` records sorted by frame, and
names the line of anything it cannot read. `pad_word(buttons)` is the 16-bit pad word with those buttons pressed (all bits
set, each pressed button's bit cleared: the original console's pad protocol), from the table `PS1_BITS`.
`to_script(presses, total, shot_every=0, shot_ranges=(), prefix="f", turbo=False)` writes the lines of an emulator build's script for the same run:
held buttons as `count Button+Button`, idle stretches as `wait count`, `shot <prefix><frame>` before the input of that frame, then `quit`. The
button spellings are in `SCRIPT_BUTTON_NAMES`. Only pad 1 is scripted.

### The debug socket

Some recompiled ports built with their debug tools take commands over a local TCP socket: one JSON line in (`{"cmd": ...}`),
one JSON line out, then the server closes the connection. `DebugClient(host, port)` opens a new connection for every call
and tries again (5 times, 50 ms apart) when none answers, then raises `DebugError`, which is also a `ConnectionError`. The
commands are the port's own; `frame`, `press`, `clear_input`, `screenshot` and `quit` are the ones the drive loop uses.

| Name | What it does |
| --- | --- |
| `DebugClient(host="127.0.0.1", port=4370, timeout=30, wait=True)` | Waits up to `timeout` seconds for the server to listen. `.call(cmd, **fields)`, `.frame()`. |
| `run_presses(client, presses, until, shot_every=300, out_dir=None, quit_at_end=False)` | Presses each button when the frame counter reaches its frame, holds it, releases it a frame after, and saves `shot_NNNNNN.png` every `shot_every` frames, until `until`. Frame numbers are open loop (a poll or two of jitter). |
| `wait_frame(get_frame, target, timeout=90, alive=None)` | Polls until the counter reaches `target`. `alive()` returning False stops the wait with `DebugError` at once instead of at the timeout. |
| `wait_ready(ping, timeout=30, alive=None)` | Calls `ping()` until it stops raising `OSError` or `TimeoutError`. `alive()` is asked before each ping, so a program that is already gone is not pinged. |

`wait_frame` and `wait_ready` take plain callables, so a session that has its own way to talk to the port (a request that
carries an identity token, say) uses them with its own `command`.

## Not here, on purpose

Other packages in this repository, or in py-kit, already do these; use them rather than a copy.

- Finding a program's window on macOS and capturing it: [`mac-window-tools`](../mac-window-tools).
- Contact sheets from many frames: `uppercut-contact-sheet` in py-kit (it needs Pillow; `PIL` reads PPM).
- Controller replay files and capture scripts for a port's own replay format: [`pad-replay`](../pad-replay) and
  [`uppercut-pad-replay-tools`](../uppercut-pad-replay-tools). `to_script` writes the same `wait` / `shot` / `quit` lines its
  `CaptureScript` does, with held buttons added.
- Reading a PNG back and checking it: `validate_png` in [`uppercut-mgba-smoke`](../uppercut-mgba-smoke).

## What runs where

Everything runs on Linux, macOS and Windows with Python 3.10 or newer, with no display and no hardware. The tests start a fake
debug server on a local port (127.0.0.1, any free port) and write files under a temporary folder.

Tested at 1.0.0: `uv run pytest` passes (70 tests) on Windows 11 Pro (10.0.26300, Python 3.14.0) and on macOS 27.0
(Apple M6, Python 3.13.18). The Windows run found one test that split a screenshot path on `/` and failed on `\`; the test now uses
`os.path.basename`.

```sh
uv sync --all-packages && uv run pytest
```

## Licence

MIT, see `LICENSE`.
