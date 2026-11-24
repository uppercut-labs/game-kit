"""Find where a game keeps its state, from the debug ELF: a symbol's address and size from ``nm``, and struct member
offsets from ``readelf --debug-dump=info``. What the structs are called, and which members matter, is the caller's."""

from __future__ import annotations

import re
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping, Optional, Sequence

from .errors import SmokeError

EWRAM_START = 0x02000000
EWRAM_END = 0x02040000
IWRAM_START = 0x03000000
IWRAM_END = 0x03008000


@dataclass(frozen=True)
class Symbol:
    address: int
    size: Optional[int]
    """The size when ``nm -S`` printed one, else ``None``."""


@dataclass(frozen=True)
class DwarfStruct:
    byte_size: int
    members: Mapping[str, int]
    """Member name to byte offset."""


def find_symbol(nm_output: str, name: str) -> Symbol:
    """The one data or bss symbol called ``name`` in ``nm -an`` or ``nm -S -an`` output.

    Raises ``SmokeError`` unless there is exactly one match, so a renamed or duplicated symbol is caught early.
    """
    pattern = re.compile(rf"^([0-9a-fA-F]+)\s+(?:([0-9a-fA-F]+)\s+)?[A-Za-z]\s+{re.escape(name)}$", re.MULTILINE)
    matches = pattern.findall(nm_output)
    if len(matches) != 1:
        raise SmokeError(f"expected one {name} symbol, found {len(matches)}")
    address, size = matches[0]
    return Symbol(int(address, 16), int(size, 16) if size else None)


def require_in_ram(symbol: Symbol, label: str, *, regions: Sequence[tuple[int, int]] = ((EWRAM_START, EWRAM_END), (IWRAM_START, IWRAM_END))) -> None:
    """Raise unless the symbol (with its size, if known) lies wholly inside one of the GBA RAM regions."""
    end = symbol.address + (symbol.size or 1)
    if not any(start <= symbol.address and end <= stop for start, stop in regions):
        size = f"+0x{symbol.size:X}" if symbol.size else ""
        raise SmokeError(f"{label} must lie wholly inside GBA RAM, found 0x{symbol.address:08X}{size}")


def _attribute(lines: Sequence[str], attribute: str) -> Optional[str]:
    marker = f"DW_AT_{attribute}"
    for line in lines:
        if marker in line:
            value = line.split(marker, 1)[1].strip()
            return value.rsplit(":", 1)[1].strip() if ":" in value else value
    return None


def _integer(value: Optional[str], label: str) -> int:
    if value is None:
        raise SmokeError(f"DWARF {label} is missing")
    try:
        return int(value.split()[0], 0)
    except ValueError as error:
        raise SmokeError(f"DWARF {label} is not a constant: {value}") from error


def parse_dwarf_structs(dwarf_output: str) -> Mapping[str, tuple[DwarfStruct, ...]]:
    """Every named structure in ``readelf --debug-dump=info`` output, by name (a name can have several definitions)."""
    lines = dwarf_output.splitlines()
    die = re.compile(r"^\s*<(\d+)><[0-9a-fA-F]+>:")
    starts: list[tuple[int, Optional[str], int]] = []
    for index, line in enumerate(lines):
        match = die.match(line)
        if match is None:
            continue
        tag_match = re.search(r"\((DW_TAG_[^)]+)\)", line)
        starts.append((int(match.group(1)), tag_match.group(1) if tag_match else None, index))
    entries = [
        (depth, tag, start, starts[i + 1][2] if i + 1 < len(starts) else len(lines))
        for i, (depth, tag, start) in enumerate(starts)
    ]

    structures: dict[str, list[DwarfStruct]] = {}
    for index, (depth, tag, start, end) in enumerate(entries):
        if tag != "DW_TAG_structure_type":
            continue
        name = _attribute(lines[start:end], "name")
        size = _attribute(lines[start:end], "byte_size")
        if not name or size is None:
            continue
        members: dict[str, int] = {}
        child = index + 1
        while child < len(entries):
            child_depth, child_tag, child_start, child_end = entries[child]
            if child_depth <= depth:
                break
            if child_depth == depth + 1 and child_tag == "DW_TAG_member":
                member = _attribute(lines[child_start:child_end], "name")
                location = _attribute(lines[child_start:child_end], "data_member_location")
                if member and location is not None:
                    members[member] = _integer(location, f"{name}.{member} location")
            child += 1
        structures.setdefault(name, []).append(DwarfStruct(_integer(size, f"{name} byte size"), members))
    return {name: tuple(values) for name, values in structures.items()}


def require_struct(structures: Mapping[str, tuple[DwarfStruct, ...]], name: str, members: Sequence[str]) -> DwarfStruct:
    """The one complete definition of ``name`` that has all ``members`` (identical repeats from several compile
    units count as one). Raises ``SmokeError`` for none or for conflicting ones."""
    candidates = [c for c in structures.get(name, ()) if all(m in c.members for m in members)]
    unique = {(c.byte_size, tuple(sorted(c.members.items()))): c for c in candidates}
    if len(unique) != 1:
        raise SmokeError(f"expected one complete DWARF {name} definition, found {len(unique)}")
    return next(iter(unique.values()))


def _run_tool(command: Sequence[str]) -> str:
    try:
        result = subprocess.run(list(command), check=True, capture_output=True, text=True)
    except OSError as error:
        raise SmokeError(f"cannot run {command[0]}: {error}") from error
    except subprocess.CalledProcessError as error:
        detail = (error.stderr or error.stdout or str(error)).strip()
        raise SmokeError(f"failed to inspect debug ELF: {detail}") from error
    return result.stdout


def read_symbols(nm: Path, elf: Path) -> str:
    """``nm -S -an`` output for ``elf``, from the given ``nm`` (for devkitARM, ``arm-none-eabi-nm``)."""
    return _run_tool([str(nm), "-S", "-an", str(elf)])


def read_dwarf(readelf: Path, elf: Path) -> str:
    """``readelf --debug-dump=info`` output for ``elf``."""
    return _run_tool([str(readelf), "--debug-dump=info", str(elf)])
