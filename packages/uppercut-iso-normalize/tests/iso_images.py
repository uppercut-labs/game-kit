"""Builds small synthetic ISO 9660 images the way macOS ``hdiutil`` lays them out: a primary volume descriptor at
sector 16, its terminator at 17, a one-sector root directory at 18 and the files after it. The dates are deliberately
not the normalized ones. Nothing here is a real disc: the "ELF" is a 52-byte header and the files hold made-up bytes."""

from __future__ import annotations

import struct
from typing import Optional, Sequence

SECTOR = 2048
ROOT_SECTOR = 18
FIRST_FILE_SECTOR = 20

DATE_A = bytes((126, 11, 24, 12, 34, 56, 252))  # 2026-11-24 12:34:56, a zone offset of -4 hours
DATE_B = bytes((125, 3, 9, 8, 1, 2, 4))
LONG_A = b"2026112412345600\x00"[:16] + bytes([252])
LONG_B = b"2025030908010200\x00"[:16] + bytes([4])


def both32(value: int) -> bytes:
    return struct.pack("<I", value) + struct.pack(">I", value)


def both16(value: int) -> bytes:
    return struct.pack("<H", value) + struct.pack(">H", value)


def make_elf(extra: int = 64, *, fill: int = 0xAB) -> bytes:
    """A 52-byte 32-bit little-endian MIPS executable header followed by ``extra`` filler bytes."""
    header = bytearray(52)
    header[0:7] = b"\x7fELF\x01\x01\x01"
    header[16:20] = b"\x02\x00\x08\x00"
    return bytes(header) + bytes([fill]) * extra


def system_cnf(elf_name: str = "GAME.ELF", *, ver: str = "1.00", vmode: str = "NTSC", newline: str = "\r\n") -> bytes:
    lines = [f"BOOT2 = cdrom0:\\{elf_name};1", f"VER = {ver}", f"VMODE = {vmode}"]
    return (newline.join(lines) + newline).encode("ascii")


def rr_px() -> bytes:
    return b"PX" + bytes([36, 1]) + both32(0o100644) + both32(1) + both32(0) + both32(0)


def rr_nm(name: bytes) -> bytes:
    return b"NM" + bytes([5 + len(name), 1, 0]) + name


def rr_tf(flags: int, stamps: Sequence[bytes]) -> bytes:
    body = b"".join(stamps)
    return b"TF" + bytes([5 + len(body), 1, flags]) + body


def rr_for(name: bytes, *, long_form: bool = False, stamp_count: int = 3) -> bytes:
    stamp = LONG_A if long_form else DATE_A
    flags = (0x80 if long_form else 0) | ((1 << stamp_count) - 1)
    return rr_px() + rr_nm(name) + rr_tf(flags, [stamp] * stamp_count) + b"ST" + bytes([4, 1])


def dir_record(
    name: bytes,
    extent: int,
    size: int,
    *,
    flags: int = 0,
    date: bytes = DATE_A,
    system_use: bytes = b"",
) -> bytes:
    pad = 1 if len(name) % 2 == 0 else 0
    length = 33 + len(name) + pad + len(system_use)
    record = bytearray(33) + name + b"\x00" * pad + system_use
    if length % 2:
        record += b"\x00"
        length += 1
    record[0] = length
    record[2:10] = both32(extent)
    record[10:18] = both32(size)
    record[18:25] = date
    record[25] = flags
    record[28:32] = both16(1)[0:2] + both16(1)[2:4]
    record[32] = len(name)
    return bytes(record)


def build_iso(
    files: Sequence[tuple[bytes, bytes]],
    *,
    version_suffix: bool = False,
    rock_ridge: bool = True,
    long_form: bool = False,
    dates: tuple[bytes, bytes] = (DATE_A, LONG_A),
    root_system_use: Optional[bytes] = None,
    pad_sectors: int = 0,
) -> bytearray:
    """An image holding ``files`` (name, data) in a one-sector root directory."""
    short_date, long_date = dates
    extents = []
    sector = FIRST_FILE_SECTOR
    for _name, data in files:
        extents.append(sector)
        sector += max(1, (len(data) + SECTOR - 1) // SECTOR)
    total_sectors = sector + pad_sectors

    if root_system_use is None:
        root_system_use = rr_px() + rr_tf(0x07, [short_date] * 3) if rock_ridge else b""
    records = [
        dir_record(b"\x00", ROOT_SECTOR, SECTOR, flags=2, date=short_date, system_use=root_system_use),
        dir_record(b"\x01", ROOT_SECTOR, SECTOR, flags=2, date=short_date, system_use=root_system_use),
    ]
    for (name, data), extent in zip(files, extents):
        listed = name + (b";1" if version_suffix else b"")
        use = rr_for(name, long_form=long_form) if rock_ridge else b""
        records.append(dir_record(listed, extent, len(data), date=short_date, system_use=use))
    directory = b"".join(records)
    assert len(directory) <= SECTOR, "synthetic directory does not fit one sector"

    image = bytearray(total_sectors * SECTOR)
    primary = bytearray(SECTOR)
    primary[0:7] = b"\x01CD001\x01"
    primary[8:72] = b" " * 64
    primary[40:50] = b"SYNTHETIC "[:10]
    primary[80:88] = both32(total_sectors)
    primary[120:124] = both16(1)
    primary[124:128] = both16(1)
    primary[128:132] = both16(SECTOR)
    primary[156:190] = dir_record(b"\x00", ROOT_SECTOR, SECTOR, flags=2, date=short_date)[:34]
    primary[190:813] = b" " * 623
    primary[813:830] = long_date
    primary[830:847] = long_date
    primary[847:864] = b"0000000000000000\x00"
    primary[864:881] = b"0000000000000000\x00"
    primary[881] = 1
    image[16 * SECTOR : 17 * SECTOR] = primary
    image[17 * SECTOR : 17 * SECTOR + 7] = b"\xffCD001\x01"
    image[ROOT_SECTOR * SECTOR : ROOT_SECTOR * SECTOR + len(directory)] = directory
    for (_name, data), extent in zip(files, extents):
        image[extent * SECTOR : extent * SECTOR + len(data)] = data
    return image


def build_disc(elf: bytes, *, elf_name: str = "GAME.ELF", **options: object) -> bytearray:
    """The disc ``normalize_disc`` expects: SYSTEM.CNF and one ELF."""
    return build_iso([(b"SYSTEM.CNF", system_cnf(elf_name)), (elf_name.encode("ascii"), elf)], **options)  # type: ignore[arg-type]


def read_root(image: bytes) -> list[dict]:
    """The root directory's records, as plain dictionaries."""
    start = ROOT_SECTOR * SECTOR
    records = []
    offset = start
    while offset < start + SECTOR and image[offset]:
        length = image[offset]
        record = image[offset : offset + length]
        name_len = record[32]
        records.append(
            {
                "raw": bytes(record),
                "name": bytes(record[33 : 33 + name_len]),
                "date": bytes(record[18:25]),
                "flags": record[25],
                "extent": int.from_bytes(record[2:6], "little"),
                "size": int.from_bytes(record[10:14], "little"),
                "system_use": bytes(record[33 + name_len + (name_len % 2 == 0) :]),
            }
        )
        offset += length
    return records


def file_bytes(image: bytes, record: dict) -> bytes:
    return bytes(image[record["extent"] * SECTOR : record["extent"] * SECTOR + record["size"]])
