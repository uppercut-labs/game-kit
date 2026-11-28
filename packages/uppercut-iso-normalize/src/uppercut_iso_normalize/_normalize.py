"""Rewrite the dates and names of a single-directory PlayStation 2 data disc so the same files always give the same bytes."""

from __future__ import annotations

import re
from typing import MutableSequence, Optional, Sequence

SECTOR_SIZE = 2048
PRIMARY = 16 * SECTOR_SIZE
# The ISO 9660 short date of a directory record: years since 1900 (100 is 2000), month, day, hour, minute, second, zone.
SHORT_DATE = bytes((100, 1, 1, 0, 0, 0, 0))
# The 17-byte long date of a volume descriptor and of long-form Rock Ridge timestamps: 2000-01-01 00:00:00.00, zone 0.
LONG_DATE = b"2000010100000000\0"

SYSTEM_CNF_NAME = b"SYSTEM.CNF"
_ELF_NAME = re.compile(r"^[A-Z0-9_]{1,8}\.[A-Z0-9_]{1,3}$")


class IsoNormalizeError(ValueError):
    """Raised when the image is not the disc this module expects. The image is left unchanged."""


def ps2_system_cnf(elf_name: str, *, ver: str = "1.00", vmode: str = "NTSC") -> list[bytes]:
    """The lines of the SYSTEM.CNF a one-ELF PlayStation 2 disc carries: ``BOOT2``, ``VER`` and ``VMODE``."""
    return [f"BOOT2 = cdrom0:\\{elf_name};1".encode("ascii"), f"VER = {ver}".encode("ascii"), f"VMODE = {vmode}".encode("ascii")]


def _extent(record: MutableSequence[int]) -> tuple[int, int]:
    sector = int.from_bytes(bytes(record[2:6]), "little")
    size = int.from_bytes(bytes(record[10:14]), "little")
    return sector * SECTOR_SIZE, size


def _normalize_record(record: bytearray) -> None:
    if len(record) < 34 or 33 + record[32] > len(record):
        raise IsoNormalizeError("invalid ISO9660 directory record")

    record[18:25] = SHORT_DATE
    name_size = record[32]
    # The ISO9660 identifier has even-length alignment before Rock Ridge entries.
    offset = 33 + name_size + (name_size % 2 == 0)
    while offset + 4 <= len(record):
        signature = bytes(record[offset : offset + 2])
        length = record[offset + 2]
        if length < 4 or offset + length > len(record):
            raise IsoNormalizeError("invalid Rock Ridge entry")
        if signature == b"ST":
            break
        if signature == b"CE":
            raise IsoNormalizeError("unexpected Rock Ridge continuation")
        if signature == b"TF":
            if length < 5:
                raise IsoNormalizeError("missing Rock Ridge timestamp flags")
            flags = record[offset + 4]
            date = LONG_DATE if flags & 0x80 else SHORT_DATE
            dates = date * (flags & 0x7F).bit_count()
            if length != 5 + len(dates):
                raise IsoNormalizeError("invalid Rock Ridge timestamps")
            record[offset + 5 : offset + length] = dates
        offset += length


def normalize_disc(
    image: bytearray,
    elf: bytes,
    *,
    elf_name: str,
    system_cnf: Optional[Sequence[bytes]] = None,
    versioned: bool = False,
) -> None:
    """Normalize a disc image in place, after checking that it is the disc it should be.

    The image must be a primary ISO 9660 volume with a single-sector root directory (what macOS ``hdiutil`` writes)
    that holds exactly ``SYSTEM.CNF`` and ``elf_name``. The function sets every directory record date, every Rock
    Ridge timestamp and the volume's creation and modification dates to 2000-01-01 00:00:00, and gives each file the
    ISO 9660 version suffix ``;1`` that the PlayStation 2 disc reader looks up.

    ``system_cnf`` is the list of lines ``SYSTEM.CNF`` must hold (default ``ps2_system_cnf(elf_name)``). ``elf`` is
    the file the disc's ELF must equal, and must be a 32-bit little-endian MIPS executable.

    With ``versioned``, the names on the disc must already end in ``;1`` (as pycdlib writes them) and are kept as
    they are, and a set volume effective date is fixed too.

    Raises ``IsoNormalizeError`` (a ``ValueError``) and leaves ``image`` unchanged when anything does not match.
    """
    if not _ELF_NAME.match(elf_name):
        raise IsoNormalizeError(f"{elf_name!r} is not an ISO 9660 level 1 file name (8.3, capitals, digits and underscores)")
    suffix = b";1" if versioned else b""
    expected_names = {SYSTEM_CNF_NAME + suffix, elf_name.encode("ascii") + suffix}
    expected_config = list(system_cnf) if system_cnf is not None else ps2_system_cnf(elf_name)

    terminator = PRIMARY + SECTOR_SIZE
    if (
        len(image) % SECTOR_SIZE
        or bytes(image[PRIMARY : PRIMARY + 7]) != b"\x01CD001\x01"
        or bytes(image[terminator : terminator + 7]) != b"\xffCD001\x01"
    ):
        raise IsoNormalizeError("expected hdiutil's primary ISO9660 volume and terminator")

    root_record = image[PRIMARY + 156 : PRIMARY + 190]
    directory_start, directory_size = _extent(root_record)
    end = directory_start + directory_size
    if directory_size != SECTOR_SIZE or directory_start < terminator + SECTOR_SIZE or end > len(image):
        raise IsoNormalizeError("expected a single-sector ISO9660 root directory")

    offset = directory_start
    directory = bytearray()
    files: dict[bytes, bytes] = {}
    while offset < end and image[offset]:
        length = image[offset]
        if offset + length > end:
            raise IsoNormalizeError("ISO9660 directory record overruns its sector")
        record = image[offset : offset + length]
        _normalize_record(record)
        name = bytes(record[33 : 33 + record[32]])
        if name not in (b"\0", b"\1"):
            start, size = _extent(record)
            if name in files or record[25] & 0x82 or start < end or start + size > len(image):
                raise IsoNormalizeError("invalid PS2 disc file extent")
            files[name] = bytes(image[start : start + size])
            if not versioned:
                # The IOP CD filesystem looks up disc files with ISO9660 version 1.
                # The two extra characters preserve the identifier's padding parity.
                name_end = 33 + record[32]
                record[name_end:name_end] = b";1"
                if len(record) > 255:
                    raise IsoNormalizeError("revised ISO9660 directory record is longer than 255 bytes")
                record[0] = len(record)
                record[32] += 2
        directory.extend(record)
        offset += length

    if files.keys() != expected_names:
        names = f"SYSTEM.CNF;1 and {elf_name};1" if versioned else f"SYSTEM.CNF and {elf_name}"
        raise IsoNormalizeError(f"disc must contain only {names}")
    if files[SYSTEM_CNF_NAME + suffix].splitlines() != expected_config:
        raise IsoNormalizeError(f"SYSTEM.CNF does not point to {elf_name};1")
    if len(elf) < 52 or elf[:7] != b"\x7fELF\x01\x01\x01" or elf[16:20] != b"\x02\0\x08\0":
        raise IsoNormalizeError("expected a 32-bit little-endian MIPS executable ELF")
    if files[elf_name.encode("ascii") + suffix] != elf:
        raise IsoNormalizeError("disc ELF differs from the input ELF")
    if len(directory) > directory_size:
        raise IsoNormalizeError("revised ISO9660 directory exceeds one sector")
    image[directory_start:end] = directory.ljust(directory_size, b"\0")

    # Normalize the primary descriptor's root record, creation and modification dates.
    image[PRIMARY + 174 : PRIMARY + 181] = SHORT_DATE
    image[PRIMARY + 813 : PRIMARY + 847] = LONG_DATE * 2
    # pycdlib also sets the effective date to the time of the build; an unset date is all zero digits.
    if versioned and bytes(image[PRIMARY + 864 : PRIMARY + 880]) != b"0" * 16:
        image[PRIMARY + 864 : PRIMARY + 881] = LONG_DATE
