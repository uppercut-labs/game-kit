"""The PINE wire format, as pure functions.

Checked against the PCSX2 source (``pcsx2/PINE.cpp``). A request is a little-endian u32 holding the whole message
length (itself included), then commands back to back. A command is one opcode byte followed by its arguments, all
little-endian. A reply is a u32 length (itself included), one result byte (0 for success, 0xFF for failure), then
the answers of the commands in order. Memory reads answer with the value, string commands with a u32 size and that
many bytes including a trailing NUL, and the other commands with nothing.
"""

from __future__ import annotations

import enum
import struct
from typing import Iterable

from .errors import PineCommandFailed, PineProtocolError

DEFAULT_SLOT = 28011
"""PCSX2's default PINE slot: the TCP port on Windows, and the socket name without a suffix elsewhere."""
MAX_REQUEST_SIZE = 650_000
"""The most bytes PCSX2 accepts in one request, length field included."""
MAX_REPLY_SIZE = 449_999
"""The most bytes PCSX2 will send in one reply, length field included (it refuses to reach 450,000)."""

RESULT_OK = 0x00
RESULT_FAIL = 0xFF

READ_OPCODES = {8: 0, 16: 1, 32: 2, 64: 3}
WRITE_OPCODES = {8: 4, 16: 5, 32: 6, 64: 7}
VERSION = 0x08
SAVE_STATE = 0x09
LOAD_STATE = 0x0A
TITLE = 0x0B
GAME_ID = 0x0C
UUID = 0x0D
GAME_VERSION = 0x0E
STATUS = 0x0F

WIDTHS = (8, 16, 32, 64)


class PineStatus(enum.IntEnum):
    """What ``status()`` reports."""

    RUNNING = 0
    PAUSED = 1
    SHUTDOWN = 2

    @property
    def label(self) -> str:
        """``"running"``, ``"paused"`` or ``"shutdown"``."""
        return self.name.lower()


def check_width(bits: int) -> int:
    if bits not in WIDTHS:
        raise ValueError("PINE scalar width must be 8, 16, 32 or 64")
    return bits


def check_address(address: int, size: int = 1) -> int:
    if not isinstance(address, int) or isinstance(address, bool):
        raise TypeError("a guest address must be an int")
    if address < 0 or address + size - 1 > 0xFFFFFFFF:
        raise ValueError(f"guest address {address:#x} (size {size}) is outside 0 to 0xFFFFFFFF")
    return address


def check_value(value: int, bits: int) -> int:
    if not isinstance(value, int) or isinstance(value, bool):
        raise TypeError("a value to write must be an int")
    if not 0 <= value < (1 << bits):
        raise ValueError(f"{value} does not fit in {bits} unsigned bits")
    return value


def signed(value: int, bits: int) -> int:
    """Reinterpret an unsigned read as a two's-complement signed number of ``bits`` bits."""
    check_width(bits)
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


def read_command(address: int, bits: int) -> bytes:
    check_width(bits)
    return struct.pack("<BI", READ_OPCODES[bits], check_address(address, bits // 8))


def write_command(address: int, value: int, bits: int) -> bytes:
    check_width(bits)
    return struct.pack("<BI", WRITE_OPCODES[bits], check_address(address, bits // 8)) + check_value(value, bits).to_bytes(
        bits // 8, "little"
    )


def state_command(opcode: int, slot: int) -> bytes:
    if not isinstance(slot, int) or isinstance(slot, bool) or not 0 <= slot <= 255:
        raise ValueError("a save-state slot is one byte, 0 to 255")
    return bytes((opcode, slot))


def frame_request(commands: bytes) -> bytes:
    """Prefix ``commands`` with the whole-message length."""
    total = 4 + len(commands)
    if total > MAX_REQUEST_SIZE:
        raise ValueError(f"a request of {total} bytes is over PCSX2's {MAX_REQUEST_SIZE} byte limit")
    return struct.pack("<I", total) + commands


def parse_reply(body: bytes) -> bytes:
    """Check the result byte of a reply body (everything after the length field) and return the answers."""
    if not body:
        raise PineProtocolError("empty PINE reply")
    if body[0] == RESULT_FAIL:
        raise PineCommandFailed("PCSX2 refused the command (no game running, or the message was too large)")
    if body[0] != RESULT_OK:
        raise PineProtocolError(f"unknown PINE result byte {body[0]:#04x}")
    return body[1:]


def check_reply_length(total: int) -> int:
    if total < 5 or total > MAX_REPLY_SIZE:
        raise PineProtocolError(f"invalid PINE reply length: {total}")
    return total


def parse_string(answer: bytes) -> str:
    """Decode a string answer: a u32 size, then that many bytes ending in a NUL."""
    if len(answer) < 4:
        raise PineProtocolError("truncated PINE string reply")
    (size,) = struct.unpack_from("<I", answer)
    value = answer[4:]
    if len(value) != size:
        raise PineProtocolError(f"PINE string reply says {size} bytes but carries {len(value)}")
    return value.rstrip(b"\0").decode("utf-8", errors="replace")


def split_values(answer: bytes, bits_list: Iterable[int]) -> list[int]:
    """Cut the answers of a run of reads into unsigned integers, checking the total size."""
    widths = [bits // 8 for bits in bits_list]
    if sum(widths) != len(answer):
        raise PineProtocolError(f"PINE reply carries {len(answer)} bytes, expected {sum(widths)}")
    values, offset = [], 0
    for width in widths:
        values.append(int.from_bytes(answer[offset : offset + width], "little"))
        offset += width
    return values
