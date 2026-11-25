"""Shared types: the error, the verification result and the input checks."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Tuple, Union

ByteString = Union[bytes, bytearray, memoryview]


class RomHeaderError(ValueError):
    """Raised when a ROM image is too short for the checksum, or is not a byte buffer."""


@dataclass(frozen=True)
class Verification:
    """The outcome of a ``verify_*`` call.

    ``expected`` is what the image's contents give, ``found`` is what its header holds. Each is a tuple of
    integers: ``(crc1, crc2)`` for Nintendo 64, one value for Mega Drive and Game Boy Advance.
    """

    ok: bool
    expected: Tuple[int, ...]
    found: Tuple[int, ...]


def require_length(rom: ByteString, minimum: int, what: str) -> None:
    if not isinstance(rom, (bytes, bytearray, memoryview)):
        raise RomHeaderError(f"{what} must be bytes, bytearray or memoryview, not {type(rom).__name__}")
    if len(rom) < minimum:
        raise RomHeaderError(f"{what} is too short for its checksum: {len(rom)} bytes, need at least {minimum}")


def require_writable(rom: ByteString) -> None:
    if not isinstance(rom, bytearray):
        raise RomHeaderError(f"fixing a checksum writes into the image, so it must be a bytearray, not {type(rom).__name__}")
