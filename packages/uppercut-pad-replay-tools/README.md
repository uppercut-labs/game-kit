# uppercut-pad-replay-tools

Writes and checks the controller replay files that the C package [`pad-replay`](../pad-replay) reads, and the small capture scripts that go with them. A scenario script says "at frame 10 slot 7 taps Start"; this package turns that into the six-field file a game port replays, a `wait N / shot name / quit` capture script for an emulator hook, and, for a GameCube game, a Dolphin DTM of the same input. Standard library only.

```sh
pip install uppercut-pad-replay-tools
```

## Replays

```python
from uppercut_pad_replay_tools import CaptureScript, Replay

START, CROSS = 0x0008, 0x4000          # the port's own bit set: the package gives the bits no meaning

replay = Replay(slots=8)
for slot in range(8):
    replay.event(0, slot)              # every pad connected and at rest from frame 0
replay.tap(10, 7, START)               # press at frame 10, let go at frame 11
replay.tap(120, range(8), CROSS, hold=3)
replay.event(300, 2, 0, lx=225, ly=180)   # a stick held over from frame 300
replay.write("scenario.input")

CaptureScript().shots_at([(60, "title"), (240, "play")]).write("capture-scenario.txt")
```

That writes

```
# frame slot buttons_hex lx ly connected
0 0 0000 128 128 1
...
```

and

```
wait 60
shot title
wait 180
shot play
wait 45
quit
```

| Name | What it does |
| --- | --- |
| `Replay(slots=8)` | A replay under construction. `.event(frame, slot, buttons=0, lx=128, ly=128, connected=1)`, `.tap(frame, slot, buttons, hold=1)` and `.disconnect(frame, slot)` take one slot or several, and return the replay. A later call for the same frame and slot replaces the earlier one. `.events`, `.last_frame`, `.text(header=...)`, `.write(path)`. |
| `parse_replay(text_or_bytes, slots=8, max_events=32768)`, `read_replay(path, ...)` | Read a replay as strictly as the C reader: the first bad line raises `ReplayError` with its line number. For checking a hand-edited file or a recording before a long emulator run. |
| `uppercut-pad-replay-tools FILE... [--slots N] [--max-events N]` | The same check on the command line. Prints `PASS` or `FAIL` per file; exit 0 when all pass, 1 when one fails. |
| `CaptureScript()` | `.wait(n)`, `.shot(name)`, `.quit()`, `.raw(line)`, and `.shots_at([(frame, name), ...], tail=45, start=0)`, which turns absolute frames into waits. `.text()`, `.write(path)`. |
| `ButtonTable({"Start": 0x0008, ...})` | `.mask("Left+Cross")` and `.names(mask)`, for scenarios and logs that name the port's bits. The table is the caller's: there is no default. |

The format is documented in [pad-replay's `docs/format.md`](../pad-replay/docs/format.md). The strictness matches the C reader's: a field must be a plain number in range (no sign), nothing may follow the sixth field (not even a `#` comment), frames may not go down, no NUL byte, a line is at most 255 bytes, and a file holds at most `max_events` events (the C reader's cap is its own setting; ports have used 4096 and 32768).

## Dolphin DTM export

For a GameCube game run under Dolphin, a replay becomes an input movie:

```python
from uppercut_pad_replay_tools import GCButton, Replay, replay_to_dtm

replay = Replay().tap(120, 0, GCButton.START, hold=20).tap(240, 0, GCButton.A, hold=20).tap(360, 1, GCButton.A, hold=20)
replay_to_dtm(replay.events, 1800, game_id="ID-abc", ports={0: 0, 1: 2}).write("input.dtm")
```

`ports` maps a replay slot to a GameCube port (`{0: 0, 1: 2}` plays slot 0 on port 1 and slot 1 on port 3). `buttons=` maps the replay's own bits to `GCButton` bits; without it the replay's bits are taken as `GCButton` bits. `gc_pad(...)`, `dtm_header(...)` and `DtmBuilder` are there for a movie built frame by frame. The header is 256 bytes and each pad is 8 bytes per frame, as in Dolphin's `Movie.h`; the layout is written from that description and no Dolphin code is included. The movie starts from power on, and its tick count is set to the maximum so playback is not cut short by it. A DTM is replayed by Dolphin's movie playback; whether a game then does what the replay meant is for the run to show, so check the first run.

## What it reconciles

The scenario scripts of a console test lab each redefined `event()` and `tap()` and wrote the file and the capture script by hand (`wait`, `shot`, `quit` lines), and one other script wrote a Dolphin DTM. This package is the one copy of those, plus the strict check.

## Layout

```
src/uppercut_pad_replay_tools/   _replay.py, _capture.py, _buttons.py, _dtm.py, cli.py
tests/                           synthetic only; test_c_reader.py compares with pad-replay's C reader when that package is beside this one
```

## Tests

`uv sync --all-packages && uv run pytest` from the repository root. The tests build their replays in code; none read a game file. `test_c_reader.py` needs a C compiler and `packages/pad-replay` in the checkout, and skips itself otherwise (it is skipped in an sdist).

## Licence

MIT.
