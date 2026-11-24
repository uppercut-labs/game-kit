"""The Mega Drive and 32X header checksum: the sum of every 16-bit word from 0x200 to the end of the image."""

from __future__ import annotations

import struct

from ._checks import ByteString, Verification, require_length, require_writable

SUM_START = 0x200
CHECKSUM_OFFSET = 0x18E


def compute_megadrive_checksum(rom: ByteString) -> int:
    """The big-endian 16-bit word sum of the image from 0x200, modulo 65536. A trailing odd byte is not counted."""
    require_length(rom, SUM_START, "Mega Drive ROM")
    words = (len(rom) - SUM_START) // 2
    return sum(struct.unpack_from(f">{words}H", rom, SUM_START)) & 0xFFFF


def fix_megadrive_checksum(rom: bytearray) -> int:
    """Write the checksum big-endian at 0x18E and return it."""
    require_writable(rom)
    checksum = compute_megadrive_checksum(rom)
    struct.pack_into(">H", rom, CHECKSUM_OFFSET, checksum)
    return checksum


def verify_megadrive_checksum(rom: ByteString) -> Verification:
    """Compare the header's checksum word with the image's word sum."""
    expected = compute_megadrive_checksum(rom)
    (found,) = struct.unpack_from(">H", rom, CHECKSUM_OFFSET)
    return Verification(ok=found == expected, expected=(expected,), found=(found,))
