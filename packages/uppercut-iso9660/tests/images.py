"""Builds small synthetic ISO 9660 images for the tests. Nothing here is a real disc: the "ELF" is an 8-byte header and
the other files hold made-up bytes. ``wrap`` re-lays a cooked image out as a raw CD dump (sync, header, sub-header and
filler error-correction bytes around each 2048-byte sector)."""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from typing import Optional

SECTOR = 2048


def both32(value: int) -> bytes:
    return struct.pack("<I", value) + struct.pack(">I", value)


def both16(value: int) -> bytes:
    return struct.pack("<H", value) + struct.pack(">H", value)


def record(
    name: bytes,
    lba: int,
    size: int,
    *,
    flags: int = 0,
    system_use: bytes = b"",
    little_only: Optional[tuple[int, int]] = None,
) -> bytes:
    """One directory record. ``little_only=(lba, size)`` writes different big-endian copies, to test disagreement."""
    pad = 1 if len(name) % 2 == 0 else 0
    body = bytearray(33) + name + b"\x00" * pad + system_use
    if len(body) % 2:
        body += b"\x00"
    body[0] = len(body)
    body[2:10] = both32(lba)
    body[10:18] = both32(size)
    if little_only is not None:
        body[6:10] = struct.pack(">I", little_only[0])
        body[14:18] = struct.pack(">I", little_only[1])
    body[18:25] = bytes((126, 11, 24, 12, 0, 0, 0))
    body[25] = flags
    body[28:32] = both16(1)
    body[32] = len(name)
    return bytes(body)


def rock_ridge(name: bytes) -> bytes:
    px = b"PX" + bytes([36, 1]) + both32(0o100644) + both32(1) + both32(0) + both32(0)
    nm = b"NM" + bytes([5 + len(name), 1, 0]) + name
    return px + nm


@dataclass
class Node:
    name: str
    data: bytes = b""
    children: list["Node"] = field(default_factory=list)
    is_dir: bool = False
    raw_name: Optional[bytes] = None
    flags: int = 0


def directory(name: str, *children: Node) -> Node:
    return Node(name, is_dir=True, children=list(children))


def file(name: str, data: bytes, **options: object) -> Node:
    return Node(name, data=data, **options)  # type: ignore[arg-type]


def make_elf(extra: int = 40) -> bytes:
    return b"\x7fELF\x01\x01\x01\x00" + bytes(range(extra))


def build_iso(
    root_children: list[Node],
    *,
    volume_id: str = "SYNTHETIC",
    rock_ridge_names: bool = False,
    directory_sectors: int = 1,
    total_extra: int = 0,
) -> bytearray:
    """Lay out a volume: PVD at 16, terminator at 17, then directories and files. ``directory_sectors`` makes every
    directory that many sectors long (records never cross a sector boundary, and each sector is zero padded)."""
    root = Node("", is_dir=True, children=root_children)
    layout: list[tuple[Node, str]] = []
    queue = [(root, "")]
    while queue:
        node, path = queue.pop(0)
        layout.append((node, path))
        for child in node.children:
            if child.is_dir:
                queue.append((child, f"{path}/{child.name}"))

    next_sector = 18
    dir_lba: dict[int, int] = {}
    for node, _path in layout:
        dir_lba[id(node)] = next_sector
        next_sector += directory_sectors
    file_lba: dict[int, int] = {}
    for node, _path in layout:
        for child in node.children:
            if not child.is_dir:
                file_lba[id(child)] = next_sector
                next_sector += max(1, (len(child.data) + SECTOR - 1) // SECTOR) if child.data else 1
    total = next_sector + total_extra

    image = bytearray(total * SECTOR)
    parent_of: dict[int, Node] = {id(c): n for n, _ in layout for c in n.children}

    def entry_record(node: Node, name_bytes: bytes) -> bytes:
        use = rock_ridge(name_bytes) if rock_ridge_names else b""
        if node.is_dir:
            return record(name_bytes, dir_lba[id(node)], directory_sectors * SECTOR, flags=2, system_use=use)
        return record(name_bytes, file_lba[id(node)], len(node.data), flags=node.flags, system_use=use)

    for node, _path in layout:
        parent = parent_of.get(id(node), node)
        records = [
            record(b"\x00", dir_lba[id(node)], directory_sectors * SECTOR, flags=2),
            record(b"\x01", dir_lba[id(parent)], directory_sectors * SECTOR, flags=2),
        ]
        for child in node.children:
            records.append(entry_record(child, child.raw_name if child.raw_name is not None else child.name.encode("ascii")))
        sectors = [bytearray(SECTOR) for _ in range(directory_sectors)]
        used = [0] * directory_sectors
        target = 0
        for rec in records:
            if used[target] + len(rec) > SECTOR:
                target += 1
                assert target < directory_sectors, "synthetic directory does not fit"
            sectors[target][used[target] : used[target] + len(rec)] = rec
            used[target] += len(rec)
        start = dir_lba[id(node)] * SECTOR
        for index, sector in enumerate(sectors):
            image[start + index * SECTOR : start + (index + 1) * SECTOR] = sector
        for child in node.children:
            if not child.is_dir and child.data:
                at = file_lba[id(child)] * SECTOR
                image[at : at + len(child.data)] = child.data

    primary = bytearray(SECTOR)
    primary[0:7] = b"\x01CD001\x01"
    primary[8:40] = b"SYNTHETIC SYSTEM".ljust(32)
    primary[40:72] = volume_id.encode("ascii").ljust(32)
    primary[80:88] = both32(total)
    primary[120:124] = both16(1)
    primary[124:128] = both16(1)
    primary[128:132] = both16(SECTOR)
    primary[156:190] = record(b"\x00", dir_lba[id(root)], directory_sectors * SECTOR, flags=2)[:34]
    primary[881] = 1
    image[16 * SECTOR : 17 * SECTOR] = primary
    image[17 * SECTOR : 17 * SECTOR + 7] = b"\xffCD001\x01"
    return image


def system_cnf(elf_name: str = "SLUS_218.47", *, newline: str = "\r\n", extra: str = "") -> bytes:
    return (newline.join([f"BOOT2 = cdrom0:\\{elf_name};1", "VER = 1.00", "VMODE = NTSC"]) + newline + extra).encode("ascii")


def ps2_disc(elf: Optional[bytes] = None, *, elf_name: str = "SLUS_218.47", suffix: bool = True, **options: object) -> bytearray:
    """A disc with SYSTEM.CNF and one boot ELF in the root, named the way mastering tools do (``;1`` suffix)."""
    elf = elf if elf is not None else make_elf()
    version = ";1" if suffix else ""
    return build_iso(
        [file(f"SYSTEM.CNF{version}", system_cnf(elf_name)), file(f"{elf_name}{version}", elf)],
        **options,  # type: ignore[arg-type]
    )


def wrap(cooked: bytes, layout: str) -> bytes:
    """Re-lay a cooked image as raw sectors of the named layout."""
    out = bytearray()
    for index in range(len(cooked) // SECTOR):
        data = cooked[index * SECTOR : (index + 1) * SECTOR]
        minute, second, frame = (index + 150) // (75 * 60), ((index + 150) // 75) % 60, (index + 150) % 75
        address = bytes((minute, second, frame))
        sync = b"\x00" + b"\xff" * 10 + b"\x00"
        if layout == "raw-2352-mode1":
            out += sync + address + b"\x01" + data + bytes(288)
        elif layout == "raw-2352-mode2":
            out += sync + address + b"\x02" + bytes((0, 0, 8, 0, 0, 0, 8, 0)) + data + bytes(280)
        elif layout == "raw-2336-mode2":
            out += bytes((0, 0, 8, 0, 0, 0, 8, 0)) + data + bytes(280)
        else:
            raise ValueError(layout)
    return bytes(out)
