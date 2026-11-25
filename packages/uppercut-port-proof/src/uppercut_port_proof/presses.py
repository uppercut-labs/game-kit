"""Press scripts: the plain-text input a proof run replays, and its two forms on the way to a port.

A line is `frame:button[+button...][:frames]`. The press starts when the game's frame counter reaches `frame` and is held for
`frames` frames. A `#` starts a comment. The same file drives a recompiled port over its debug socket (`pad_word`) and an
emulator's script (`to_script`), so both runs get the same input at the same frame numbers.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Mapping, Sequence, Set, Tuple

from .errors import PressError
from .ppm import PathLike

PS1_BITS: Mapping[str, int] = {
    "select": 0, "l3": 1, "r3": 2, "start": 3, "up": 4, "right": 5, "down": 6, "left": 7,
    "l2": 8, "r2": 9, "l1": 10, "r1": 11, "triangle": 12, "circle": 13, "cross": 14, "square": 15,
}
"""The bit of each button in a 16-bit pad word of the original console's pad protocol (a pressed button is a 0 bit)."""

SCRIPT_BUTTON_NAMES: Mapping[str, str] = {
    "select": "Select", "l3": "L3", "r3": "R3", "start": "Start", "up": "Up", "right": "Right", "down": "Down", "left": "Left",
    "l2": "L2", "r2": "R2", "l1": "L1", "r1": "R1", "triangle": "Triangle", "circle": "Circle", "cross": "Cross", "square": "Square",
}
"""How the PS2 emulator build's scripting language spells each button."""


@dataclass(frozen=True)
class Press:
    frame: int
    buttons: Tuple[str, ...]
    """Lower-case names, in the order written."""
    hold: int


def parse_presses(
    text: str,
    *,
    default_hold: int = 4,
    skip_prefixes: Sequence[str] = ("p2 ",),
    known: Mapping[str, object] = PS1_BITS,
    name: str = "presses",
) -> List[Press]:
    """Read a press file's text. The result is sorted by frame (presses of one frame keep the order written).

    A line that begins with one of `skip_prefixes` is left out (`p2 ` lines belong to a second pad). A frame, a hold or a
    button the file cannot use raises `PressError` with the line number."""
    out: List[Press] = []
    for number, line in enumerate(text.splitlines(), 1):
        line = line.split("#")[0].strip()
        if not line or any(line.startswith(p) for p in skip_prefixes):
            continue
        parts = line.split(":")
        if len(parts) not in (2, 3):
            raise PressError(f"{name} line {number}: expected frame:button[+button][:frames], got {line!r}")
        try:
            frame = int(parts[0])
            hold = int(parts[2]) if len(parts) == 3 else default_hold
        except ValueError:
            raise PressError(f"{name} line {number}: the frame and the hold are whole numbers: {line!r}") from None
        if frame < 0 or hold < 1:
            raise PressError(f"{name} line {number}: the frame is 0 or more and the hold is 1 or more: {line!r}")
        buttons = tuple(b.strip().lower() for b in parts[1].split("+"))
        for b in buttons:
            if b not in known:
                raise PressError(f"{name} line {number}: unknown button {b!r}")
        out.append(Press(frame, buttons, hold))
    out.sort(key=lambda p: p.frame)
    return out


def read_presses(path: PathLike, **options: object) -> List[Press]:
    return parse_presses(Path(path).read_text(encoding="utf-8"), name=Path(path).name, **options)  # type: ignore[arg-type]


def pad_word(buttons: Iterable[str]) -> int:
    """The 16-bit pad word with these buttons pressed: all bits 1, each pressed button's bit cleared."""
    word = 0xFFFF
    for b in buttons:
        word &= ~(1 << PS1_BITS[b.strip().lower()])
    return word & 0xFFFF


def to_script(
    presses: Sequence[Press],
    total: int,
    *,
    shot_every: int = 0,
    shot_ranges: Sequence[Tuple[int, int]] = (),
    prefix: str = "f",
    turbo: bool = False,
) -> List[str]:
    """The script lines for a run of `total` frames: held buttons as `count Button+Button`, idle stretches as `wait count`,
    `shot <prefix><frame>` before the input of each chosen frame, then `quit`.

    Frames count from the start of the run. A shot at frame F is taken before frame F's input is applied. Only pad 1 is
    scripted. Presses held past `total` are cut at it."""
    held: List[Set[str]] = [set() for _ in range(total + 1)]
    for p in presses:
        for f in range(p.frame, min(p.frame + p.hold, total + 1)):
            for b in p.buttons:
                held[f].add(SCRIPT_BUTTON_NAMES[b])
    shots: Set[int] = set()
    if shot_every:
        shots.update(range(shot_every, total + 1, shot_every))
    for lo, hi in shot_ranges:
        shots.update(range(lo, hi + 1))
    out: List[str] = []
    if turbo:
        out.append("turbo on")
    run = None
    count = 0

    def flush() -> None:
        nonlocal run, count
        if count:
            out.append("wait %d" % count if not run else "%d %s" % (count, "+".join(sorted(run))))
        run = None
        count = 0

    for f in range(total):
        if f in shots:
            flush()
            out.append("shot %s%05d" % (prefix, f))
        cur = frozenset(held[f])
        if count and cur == (run if run else frozenset()):
            count += 1
        else:
            flush()
            run = cur
            count = 1
    flush()
    if total in shots:
        out.append("shot %s%05d" % (prefix, total))
    out.append("quit")
    return out


def presses_to_dict(presses: Sequence[Press]) -> List[Dict[str, object]]:
    return [{"frame": p.frame, "buttons": list(p.buttons), "hold": p.hold} for p in presses]
