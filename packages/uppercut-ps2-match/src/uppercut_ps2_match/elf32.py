"""A reader for 32-bit little-endian ELF files (the PS2's Emotion Engine and the objects its compilers make), with no dependency.

It reads what the matching tools need: sections with their data, the symbol table, the `REL` relocations of an object, and the
program headers of a linked image. It writes nothing.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, Iterator, List, Optional, Union

from .errors import ElfError

SHT_NULL, SHT_PROGBITS, SHT_SYMTAB, SHT_STRTAB, SHT_NOBITS, SHT_REL = 0, 1, 2, 3, 8, 9
SHN_UNDEF, SHN_COMMON, SHN_MIPS_SCOMMON, SHN_LORESERVE = 0, 0xFFF2, 0xFF03, 0xFF00
STT_NOTYPE, STT_OBJECT, STT_FUNC = 0, 1, 2
STB_LOCAL, STB_GLOBAL = 0, 1
R_MIPS_32, R_MIPS_26, R_MIPS_HI16, R_MIPS_LO16, R_MIPS_GPREL16, R_MIPS_LITERAL = 2, 4, 5, 6, 7, 8
SHF_ALLOC = 2


@dataclass
class Section:
    index: int
    name: str
    type: int
    flags: int
    addr: int
    offset: int
    size: int
    link: int
    info: int
    entsize: int
    _raw: bytes = field(repr=False, default=b"")

    @property
    def nobits(self) -> bool:
        return self.type == SHT_NOBITS

    def data(self) -> bytes:
        """The section's bytes (empty for `SHT_NOBITS`)."""
        return b"" if self.nobits else self._raw


@dataclass
class Symbol:
    name: str
    value: int
    size: int
    bind: int
    type: int
    shndx: int

    @property
    def undefined(self) -> bool:
        return self.shndx == SHN_UNDEF

    @property
    def common(self) -> bool:
        return self.shndx in (SHN_COMMON, SHN_MIPS_SCOMMON)

    @property
    def small_common(self) -> bool:
        return self.shndx == SHN_MIPS_SCOMMON

    @property
    def defined_in_section(self) -> bool:
        return 0 < self.shndx < SHN_LORESERVE


@dataclass
class Relocation:
    offset: int
    sym: int
    type: int


@dataclass
class Segment:
    type: int
    offset: int
    vaddr: int
    filesz: int
    memsz: int
    flags: int


class Elf:
    """A parsed ELF file: `.sections`, `.symbols` (of `.symtab`, index 0 included), `.segments`, `.entry`."""

    def __init__(self, data: bytes, name: str = "ELF") -> None:
        self.name = name
        if len(data) < 52 or data[:4] != b"\x7fELF":
            raise ElfError(f"{name} is not an ELF file")
        if data[4] != 1 or data[5] != 1:
            raise ElfError(f"{name} is not 32-bit little-endian")
        (self.type, self.machine, _, self.entry, phoff, shoff, _, _, phentsize, phnum, shentsize, shnum, shstrndx) = struct.unpack_from(
            "<HHIIIIIHHHHHH", data, 16
        )
        self.sections: List[Section] = []
        self.segments: List[Segment] = []
        self.symbols: List[Symbol] = []
        try:
            for i in range(shnum):
                off = shoff + i * shentsize
                nm, ty, fl, ad, of, sz, lk, inf, _, es = struct.unpack_from("<IIIIIIIIII", data, off)
                raw = b"" if ty == SHT_NOBITS else data[of : of + sz]
                if ty != SHT_NOBITS and len(raw) != sz:
                    raise ElfError(f"{name}: section {i} runs past the end of the file")
                self.sections.append(Section(i, str(nm), ty, fl, ad, of, sz, lk, inf, es, raw))
            for i in range(phnum):
                t, of, va, _, fs, ms, fl, _ = struct.unpack_from("<IIIIIIII", data, phoff + i * phentsize)
                self.segments.append(Segment(t, of, va, fs, ms, fl))
        except struct.error as error:
            raise ElfError(f"{name}: a table runs past the end of the file") from error
        if self.sections:
            names = self.sections[shstrndx].data() if 0 <= shstrndx < len(self.sections) else b""
            for s in self.sections:
                start = int(s.name)
                s.name = names[start : names.index(b"\0", start)].decode("ascii", "replace") if names and start < len(names) else ""
        self._by_name: Dict[str, Section] = {s.name: s for s in self.sections if s.name}
        symtab = self.section(".symtab")
        if symtab is not None:
            strtab = self.sections[symtab.link].data()
            for i in range(len(symtab.data()) // 16):
                nm, val, sz, info, _, shndx = struct.unpack_from("<IIIBBH", symtab.data(), i * 16)
                end = strtab.index(b"\0", nm) if nm < len(strtab) else nm
                self.symbols.append(Symbol(strtab[nm:end].decode("ascii", "replace"), val, sz, info >> 4, info & 15, shndx))

    @classmethod
    def read(cls, path: Union[str, Path]) -> "Elf":
        return cls(Path(path).read_bytes(), str(path))

    def section(self, name: str) -> Optional[Section]:
        return self._by_name.get(name)

    def relocations(self) -> Iterator["tuple[Section, Section, List[Relocation]]"]:
        """For each `SHT_REL` section: `(rel_section, target_section, [Relocation, ...])`."""
        for s in self.sections:
            if s.type != SHT_REL:
                continue
            raw = s.data()
            rels = []
            for i in range(len(raw) // 8):
                off, info = struct.unpack_from("<II", raw, i * 8)
                rels.append(Relocation(off, info >> 8, info & 0xFF))
            yield s, self.sections[s.info], rels


class Image:
    """A linked image (the original executable): bytes at virtual addresses, from its program headers."""

    def __init__(self, data: bytes, name: str = "image") -> None:
        self.elf = Elf(data, name)
        self.data = data
        self.segs = [(s.vaddr, s.offset, s.filesz) for s in self.elf.segments]

    @classmethod
    def open(cls, path: Union[str, Path]) -> "Image":
        return cls(Path(path).read_bytes(), str(path))

    def bytes_at(self, addr: int, n: int) -> Optional[bytes]:
        """`n` bytes at a virtual address, or None when the range is not inside one segment's file bytes."""
        for va, off, sz in self.segs:
            if va <= addr and addr + n <= va + sz:
                return self.data[off + addr - va : off + addr - va + n]
        return None
