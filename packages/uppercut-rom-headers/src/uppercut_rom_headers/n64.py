"""Nintendo 64 header CRCs for the CIC-6102 boot code (and its 6101 sibling, which uses the same seed and formula)."""

from __future__ import annotations

import struct
from dataclasses import dataclass

from ._checks import ByteString, Verification, require_length, require_writable

CRC_RANGE_START = 0x1000
CRC_RANGE_END = 0x101000
CRC1_OFFSET = 0x10
CRC2_OFFSET = 0x14
CIC_6102_SEED = 0xF8CA4DDC

_WORDS = (CRC_RANGE_END - CRC_RANGE_START) // 4
_MASK = 0xFFFFFFFF


@dataclass(frozen=True)
class N64Checksums:
    """The two header CRCs. ``skipped`` is set when ``preserve_zero_header`` left an all-zero header alone."""

    crc1: int
    crc2: int
    skipped: bool = False


def compute_n64_checksums(rom: ByteString) -> N64Checksums:
    """CRC1 and CRC2 of the 1 MiB at 0x1000, for CIC-6102. The image must reach 0x101000."""
    require_length(rom, CRC_RANGE_END, "N64 ROM")
    words = struct.unpack_from(f">{_WORDS}I", rom, CRC_RANGE_START)
    t1 = t2 = t3 = t4 = t5 = t6 = CIC_6102_SEED
    for d in words:
        total = t6 + d
        if total > _MASK:
            t4 = (t4 + 1) & _MASK
        t6 = total & _MASK
        t3 ^= d
        shift = d & 0x1F
        rotated = ((d << shift) | (d >> (32 - shift))) & _MASK if shift else d
        t5 = (t5 + rotated) & _MASK
        if t2 > d:
            t2 ^= rotated
        else:
            t2 ^= t6 ^ d
        t1 = (t1 + (t5 ^ d)) & _MASK
    return N64Checksums(crc1=t6 ^ t4 ^ t3, crc2=t5 ^ t2 ^ t1)


def fix_n64_checksums(rom: bytearray, *, preserve_zero_header: bool = False) -> N64Checksums:
    """Write CRC1 and CRC2 big-endian at 0x10 and 0x14 and return them.

    With ``preserve_zero_header`` an image whose two header words are both zero is left as it is (some toolchains
    leave them zero on purpose) and ``N64Checksums(0, 0, skipped=True)`` is returned.
    """
    require_writable(rom)
    require_length(rom, CRC_RANGE_END, "N64 ROM")
    if preserve_zero_header and struct.unpack_from(">II", rom, CRC1_OFFSET) == (0, 0):
        return N64Checksums(0, 0, skipped=True)
    checksums = compute_n64_checksums(rom)
    struct.pack_into(">II", rom, CRC1_OFFSET, checksums.crc1, checksums.crc2)
    return checksums


def verify_n64_checksums(rom: ByteString) -> Verification:
    """Compare the header's CRC1 and CRC2 with what the image's contents give."""
    expected = compute_n64_checksums(rom)
    found = struct.unpack_from(">II", rom, CRC1_OFFSET)
    return Verification(ok=found == (expected.crc1, expected.crc2), expected=(expected.crc1, expected.crc2), found=found)
