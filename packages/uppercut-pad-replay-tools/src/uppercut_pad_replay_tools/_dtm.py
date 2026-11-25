"""Export a replay as a Dolphin DTM movie of GameCube pad input.

The DTM layout (a 256 byte header, then 8 bytes per pad per frame) is the one in Dolphin's `Movie.h`. This module writes
it from the layout alone and contains no Dolphin code. A DTM made here is a plain input movie: the tick count is set to
the maximum so playback is not cut short by it, and nothing says what the game does with the input.
"""

from __future__ import annotations

import struct
from enum import IntFlag
from pathlib import Path
from typing import Iterable, Mapping, Sequence

from ._replay import Event, iter_state

HEADER_SIZE = 256
PAD_SIZE = 8
MAX_TICKS = 0xFFFFFFFFFFFFFFFF


class GCButton(IntFlag):
    """The 16 button bits of a GameCube pad in a DTM, in Dolphin's order."""

    START = 0x0001
    A = 0x0002
    B = 0x0004
    X = 0x0008
    Y = 0x0010
    Z = 0x0020
    UP = 0x0040
    DOWN = 0x0080
    LEFT = 0x0100
    RIGHT = 0x0200
    L = 0x0400
    R = 0x0800
    DISC = 0x1000  # a disc change
    RESET = 0x2000
    CONNECTED = 0x4000  # set by gc_pad for a connected pad
    GET_ORIGIN = 0x8000  # asks for an analog origin reset


class DtmError(ValueError):
    """A DTM that could not be built as asked."""


def gc_pad(
    buttons: int = 0,
    *,
    trigger_l: int = 0,
    trigger_r: int = 0,
    stick: tuple[int, int] = (128, 128),
    c_stick: tuple[int, int] = (128, 128),
    connected: bool = True,
) -> bytes:
    """One pad's 8 byte state: two bytes of button bits, then the L and R triggers, the main stick and the C stick."""
    values = (trigger_l, trigger_r, *stick, *c_stick)
    if any(not 0 <= value <= 255 for value in values):
        raise DtmError("triggers and sticks run 0 to 255")
    if not 0 <= buttons <= 0xFFFF:
        raise DtmError("buttons is a 16 bit number")
    bits = (int(buttons) & ~int(GCButton.CONNECTED)) | (int(GCButton.CONNECTED) if connected else 0)
    return struct.pack("<H6B", bits, *values)


def dtm_header(
    game_id: str, frames: int, ports: Sequence[int], *, author: str = "", ticks: int = MAX_TICKS
) -> bytes:
    """The 256 byte header for a movie of `frames` frames of the GameCube pads in `ports` (0 to 3), started from
    power on. `game_id` is the six characters Dolphin shows for the game. The input count is frames times pads."""
    if len(game_id) > 6 or not game_id.isascii():
        raise DtmError("game_id is up to 6 ASCII characters")
    if not ports or any(port not in range(4) for port in ports) or len(set(ports)) != len(ports):
        raise DtmError("ports are distinct numbers from 0 to 3")
    if frames < 0:
        raise DtmError("frames cannot be negative")
    header = bytearray(HEADER_SIZE)
    header[0:4] = b"DTM\x1a"
    header[4 : 4 + len(game_id)] = game_id.encode("ascii")
    header[10] = 0  # not a Wii game
    header[11] = sum(1 << port for port in ports)
    header[12] = 0  # starts from power on, not from a savestate
    struct.pack_into("<Q", header, 13, frames)
    struct.pack_into("<Q", header, 21, frames * len(ports))
    encoded = author.encode("utf-8")[:32]
    header[49 : 49 + len(encoded)] = encoded
    struct.pack_into("<Q", header, 237, ticks)
    return bytes(header)


class DtmBuilder:
    """A movie built frame by frame. `ports` are the GameCube ports in use (0 to 3, port 1 is 0)."""

    def __init__(self, game_id: str, ports: Sequence[int] = (0,), *, author: str = "", ticks: int = MAX_TICKS) -> None:
        dtm_header(game_id, 0, ports)  # validates the arguments now
        self.game_id = game_id
        self.ports = tuple(sorted(ports))  # a DTM lists the pads in port order
        self.author = author
        self.ticks = ticks
        self._frames: list[bytes] = []

    def add_frame(self, pads: Mapping[int, bytes] | None = None) -> "DtmBuilder":
        """One frame. `pads` maps a port to the 8 bytes from gc_pad; a port left out is a connected pad at rest."""
        pads = pads or {}
        unknown = set(pads) - set(self.ports)
        if unknown:
            raise DtmError(f"ports {sorted(unknown)} are not in this movie's ports {list(self.ports)}")
        for port, pad in pads.items():
            if len(pad) != PAD_SIZE:
                raise DtmError(f"the pad for port {port} is {len(pad)} bytes, not {PAD_SIZE}")
        rest = gc_pad()
        self._frames.append(b"".join(pads.get(port, rest) for port in self.ports))
        return self

    def __len__(self) -> int:
        return len(self._frames)

    def to_bytes(self) -> bytes:
        return dtm_header(self.game_id, len(self._frames), self.ports, author=self.author, ticks=self.ticks) + b"".join(
            self._frames
        )

    def write(self, path: str | Path) -> Path:
        target = Path(path)
        target.write_bytes(self.to_bytes())
        return target


def replay_to_dtm(
    events: Iterable[Event],
    frames: int,
    *,
    game_id: str,
    ports: Mapping[int, int],
    buttons: Mapping[int, int] | None = None,
    author: str = "",
) -> DtmBuilder:
    """Turn replay events into a movie of `frames` frames.

    `ports` maps a replay slot to a GameCube port (`{0: 0, 1: 2}` plays slot 0 on port 1 and slot 1 on port 3).
    `buttons` maps a replay button bit to a GCButton; with None the replay's bits are taken as GCButton bits as they
    are. A replay stick maps straight onto the main stick, a slot not yet mentioned or not connected is a connected pad
    at rest, and bits the map does not name are dropped.
    """
    if not ports:
        raise DtmError("ports maps at least one replay slot to a GameCube port")
    if len(set(ports.values())) != len(ports):
        raise DtmError("two replay slots map to the same GameCube port")
    builder = DtmBuilder(game_id, sorted(ports.values()), author=author)
    events = list(events)
    slot_count = max([event.slot for event in events] + list(ports)) + 1
    for state in iter_state(events, frames, slots=slot_count):
        pads: dict[int, bytes] = {}
        for slot, port in ports.items():
            event = state[slot]
            if event is None or not event.connected:
                pads[port] = gc_pad()
                continue
            if buttons is None:
                bits = event.buttons
            else:
                bits = 0
                for source, target in buttons.items():
                    if event.buttons & source:
                        bits |= int(target)
            pads[port] = gc_pad(bits, stick=(event.lx, event.ly))
        builder.add_frame(pads)
    return builder
