"""The Game Boy Advance header complement byte at 0xBD."""

from __future__ import annotations

from ._checks import ByteString, Verification, require_length, require_writable

HEADER_SIZE = 0xC0
COMPLEMENT_OFFSET = 0xBD


def compute_gba_complement(rom: ByteString) -> int:
    """The complement of the header bytes 0xA0 to 0xBC: ``-(sum + 0x19)`` modulo 256."""
    require_length(rom, HEADER_SIZE, "GBA ROM")
    return (-sum(rom[0xA0:COMPLEMENT_OFFSET]) - 0x19) & 0xFF


def fix_gba_complement(rom: bytearray) -> int:
    """Write the complement byte at 0xBD and return it."""
    require_writable(rom)
    complement = compute_gba_complement(rom)
    rom[COMPLEMENT_OFFSET] = complement
    return complement


def verify_gba_complement(rom: ByteString) -> Verification:
    """Compare the header's complement byte with the one its header bytes give."""
    expected = compute_gba_complement(rom)
    return Verification(ok=rom[COMPLEMENT_OFFSET] == expected, expected=(expected,), found=(rom[COMPLEMENT_OFFSET],))
