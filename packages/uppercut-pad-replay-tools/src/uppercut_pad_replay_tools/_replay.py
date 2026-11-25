"""The replay file: build one from events and taps, write it, and read it back as strictly as the C reader does."""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Iterator, Union

NEUTRAL = 128
DEFAULT_SLOTS = 8
DEFAULT_MAX_EVENTS = 32768
MAX_SLOTS = 255
MAX_LINE = 255
HEADER = "# frame slot buttons_hex lx ly connected"

SlotArg = Union[int, Iterable[int]]


class ReplayError(ValueError):
    """A replay that is not valid. `line` is the 1-based line of the problem, 0 when it is not about one line."""

    def __init__(self, message: str, line: int = 0) -> None:
        super().__init__(f"line {line}: {message}" if line else message)
        self.line = line


@dataclass(frozen=True)
class Event:
    """One line of a replay: from `frame` on, the pad in `slot` holds `buttons` with its stick at (lx, ly).

    `connected` 0 is a pad that is not there; the other fields are then written at rest, as the recorder does."""

    frame: int
    slot: int
    buttons: int = 0
    lx: int = NEUTRAL
    ly: int = NEUTRAL
    connected: int = 1

    def __post_init__(self) -> None:
        for name, value, high in (
            ("frame", self.frame, 0xFFFFFFFF),
            ("slot", self.slot, MAX_SLOTS - 1),
            ("buttons", self.buttons, 0xFFFF),
            ("lx", self.lx, 255),
            ("ly", self.ly, 255),
            ("connected", self.connected, 1),
        ):
            if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= high:
                raise ReplayError(f"{name} must be an integer from 0 to {high}, got {value!r}")

    def line(self) -> str:
        """The event as the file writes it, without a line break."""
        if not self.connected:
            return f"{self.frame} {self.slot} 0000 {NEUTRAL} {NEUTRAL} 0"
        return f"{self.frame} {self.slot} {self.buttons:04x} {self.lx} {self.ly} 1"


def _slots(slot: SlotArg) -> list[int]:
    return [slot] if isinstance(slot, int) else list(slot)


class Replay:
    """A replay under construction. Methods return the replay, so calls chain.

    Events are kept by (frame, slot): a later call for the same frame and slot replaces the earlier one, which is what
    the game would have seen anyway (the reader applies lines in file order). The file lists events by frame, then slot.
    """

    def __init__(self, slots: int = DEFAULT_SLOTS) -> None:
        if not 1 <= slots <= MAX_SLOTS:
            raise ReplayError(f"slots must be from 1 to {MAX_SLOTS}, got {slots}")
        self.slots = slots
        self._events: dict[tuple[int, int], Event] = {}

    def event(
        self, frame: int, slot: SlotArg, buttons: int = 0, lx: int = NEUTRAL, ly: int = NEUTRAL, connected: int = 1
    ) -> "Replay":
        """From `frame` on, `slot` (one slot or several) holds `buttons` and the stick (lx, ly). With no other argument
        it is a connected pad at rest, which is how a scenario brings a pad in or lets a button go."""
        for one in _slots(slot):
            if one >= self.slots:
                raise ReplayError(f"slot {one} is out of range for {self.slots} slots")
            self._events[(frame, one)] = Event(frame, one, buttons, lx, ly, connected)
        return self

    def tap(self, frame: int, slot: SlotArg, buttons: int, hold: int = 1) -> "Replay":
        """Press `buttons` at `frame` and let go `hold` frames later (one frame by default). The release puts the
        stick back at rest."""
        if hold < 1:
            raise ReplayError(f"hold must be at least 1 frame, got {hold}")
        self.event(frame, slot, buttons)
        return self.event(frame + hold, slot)

    def disconnect(self, frame: int, slot: SlotArg) -> "Replay":
        """The pad is unplugged from `frame` on."""
        return self.event(frame, slot, connected=0)

    @property
    def events(self) -> list[Event]:
        """The events in file order: by frame, then slot."""
        return [self._events[key] for key in sorted(self._events)]

    @property
    def last_frame(self) -> int:
        return max((frame for frame, _ in self._events), default=0)

    def text(self, header: str | None = HEADER) -> str:
        """The file's text, ending in a line break. `header` is written as a comment line first; None leaves it out."""
        lines = [] if header is None else [header if header.startswith("#") else f"# {header}"]
        lines.extend(event.line() for event in self.events)
        return "\n".join(lines) + "\n"

    def write(self, path: str | Path, header: str | None = HEADER) -> Path:
        """Write the file and return its path. An empty replay is refused: the reader would refuse the file."""
        if not self._events:
            raise ReplayError("a replay needs at least one event")
        target = Path(path)
        target.write_text(self.text(header), encoding="ascii", newline="\n")
        return target


_DECIMAL = re.compile(r"[0-9]+")
_HEX = re.compile(r"(?:0[xX])?[0-9a-fA-F]+")


def _field(token: str, name: str, high: int, hex_digits: bool = False) -> int:
    if not (_HEX if hex_digits else _DECIMAL).fullmatch(token):
        raise ReplayError(f"{name} {token!r} is not a plain number")
    value = int(token, 16 if hex_digits else 10)
    if value > high:
        raise ReplayError(f"{name} {token!r} is over {high}")
    return value


def _parse_line(text: str, slots: int) -> Event | None:
    stripped = text.strip()
    if not stripped or stripped.startswith("#"):
        return None
    tokens = stripped.split()
    if len(tokens) < 6:
        raise ReplayError("a field is missing")
    if len(tokens) > 6:
        raise ReplayError("extra text after the sixth field")
    frame = _field(tokens[0], "frame", 0xFFFFFFFF)
    slot = _field(tokens[1], "slot", slots - 1)
    buttons = _field(tokens[2], "buttons", 0xFFFF, hex_digits=True)
    lx = _field(tokens[3], "lx", 255)
    ly = _field(tokens[4], "ly", 255)
    connected = _field(tokens[5], "connected", 1)
    if not connected:
        return Event(frame, slot, 0, NEUTRAL, NEUTRAL, 0)
    return Event(frame, slot, buttons, lx, ly, 1)


def parse_replay(
    data: str | bytes, *, slots: int = DEFAULT_SLOTS, max_events: int = DEFAULT_MAX_EVENTS
) -> list[Event]:
    """Read a replay as strictly as the C reader in pad-replay: the first bad line raises ReplayError (with its line
    number), so there is never a half replay. Refused: a field missing, not a plain number or out of range, text after
    the sixth field, frames that go down, a NUL byte, a line over 255 bytes, more than `max_events` events, no event."""
    if not 1 <= slots <= MAX_SLOTS:
        raise ReplayError(f"slots must be from 1 to {MAX_SLOTS}, got {slots}")
    raw = data.encode("utf-8") if isinstance(data, str) else bytes(data)
    events: list[Event] = []
    lines = raw.split(b"\n")
    if lines[-1] == b"":
        lines.pop()  # the file ended with a line break (or was empty): no unfinished last line
    for line_number, chunk in enumerate(lines, start=1):
        if b"\0" in chunk:
            raise ReplayError("a NUL byte", line_number)
        if len(chunk) > MAX_LINE:
            raise ReplayError(f"a line over {MAX_LINE} bytes", line_number)
        try:
            event = _parse_line(chunk.decode("ascii"), slots)
        except UnicodeDecodeError:
            raise ReplayError("a field is not a plain number", line_number) from None
        except ReplayError as error:
            raise ReplayError(str(error), line_number) from None
        if event is None:
            continue
        if events and event.frame < events[-1].frame:
            raise ReplayError("frames go down", line_number)
        if len(events) == max_events:
            raise ReplayError(f"more than {max_events} events", line_number)
        events.append(event)
    if not events:
        raise ReplayError("no events")
    return events


def read_replay(path: str | Path, *, slots: int = DEFAULT_SLOTS, max_events: int = DEFAULT_MAX_EVENTS) -> list[Event]:
    """parse_replay on a file."""
    return parse_replay(Path(path).read_bytes(), slots=slots, max_events=max_events)


def iter_state(events: Iterable[Event], frames: int, slots: int = DEFAULT_SLOTS) -> Iterator[list[Event | None]]:
    """For each frame 0..frames-1, every slot's last event at or before it (None before a slot's first event)."""
    pending = sorted(events, key=lambda e: e.frame)
    state: list[Event | None] = [None] * slots
    index = 0
    for frame in range(frames):
        while index < len(pending) and pending[index].frame <= frame:
            state[pending[index].slot] = pending[index]
            index += 1
        yield list(state)
