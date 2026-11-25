"""Made-up ELF files, MIPS words and a tiny assembler for the tests. Nothing here comes from a game or an SDK.

`build_elf` writes a 32-bit little-endian ELF (an object with sections, symbols and REL relocations, or an image with program
headers). `assemble` turns a small assembly dialect into such an object, so the stand-in compiler tools (`fake_tool.py`) can run the
verifier end to end with no real compiler: the "C" file of a test is that assembly text.

The dialect: `.text`, `.rdata`, `.data`, `.sdata`, `.bss`, `.sbss`, `.section NAME,"flags",@type`, `.previous`, `.align`, `.globl`,
`.extern`, `.comm`, `.word` (a number or a symbol), `.byte`, `.space`, labels, and the instructions `lui $r,%hi(sym)`,
`lw $r,%lo(sym)($s)`, `lw $r,%gp_rel(sym)($28)`, `jal sym`, `jr $31`, `nop`, `addiu $r,$s,imm` and raw `.insn 0xWORD`.
"""

from __future__ import annotations

import re
import struct
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence, Tuple

FAKE_TOOL = str(Path(__file__).with_name("fake_tool.py"))

SHT_PROGBITS, SHT_SYMTAB, SHT_STRTAB, SHT_NOBITS, SHT_REL = 1, 2, 3, 8, 9
SHF_WRITE, SHF_ALLOC, SHF_EXEC = 1, 2, 4
R32, R26, HI16, LO16, GPREL16 = 2, 4, 5, 6, 7
SHN_ABS, SHN_COMMON, SHN_SCOMMON = 0xFFF1, 0xFFF2, 0xFF03


def lui(rt: int, imm: int) -> int:
    return 0x3C000000 | rt << 16 | (imm & 0xFFFF)


def addiu(rt: int, rs: int, imm: int) -> int:
    return 0x24000000 | rs << 21 | rt << 16 | (imm & 0xFFFF)


def lw(rt: int, imm: int, rs: int) -> int:
    return 0x8C000000 | rs << 21 | rt << 16 | (imm & 0xFFFF)


def jal(addr: int) -> int:
    return 0x0C000000 | ((addr >> 2) & 0x03FFFFFF)


JR_RA = 0x03E00008
NOP = 0
AT, V0, A0, GP = 1, 2, 4, 28


def le_words(*ws: int) -> bytes:
    return b"".join(struct.pack("<I", w & 0xFFFFFFFF) for w in ws)


def build_elf(
    sections: Sequence[Dict[str, Any]],
    symbols: Sequence[Dict[str, Any]] = (),
    relocs: Sequence[Dict[str, Any]] = (),
    segments: Sequence[Tuple[int, bytes]] = (),
    entry: int = 0,
) -> bytes:
    """Sections (index 1 is the first): `{"name", "data"}` or `{"name", "size", "nobits": True}`, with optional `addr` and `flags`.
    Symbols (the null symbol is added first, so the first given is index 1): `{"name", "value", "size", "bind", "type", "shndx"}`.
    Relocs: `{"section": name, "entries": [(offset, symbol index, type)]}`. Segments: `(vaddr, bytes)`, one program header each."""
    shstr = bytearray(b"\0")
    names: Dict[str, int] = {}

    def add_name(n: str) -> int:
        if n not in names:
            names[n] = len(shstr)
            shstr.extend(n.encode() + b"\0")
        return names[n]

    body = bytearray()
    base = 52 + 32 * len(segments)

    def place(data: bytes) -> int:
        off = base + len(body)
        body.extend(data)
        body.extend(b"\0" * (-len(body) % 4))
        return off

    seg_entries = [(1, place(data), vaddr, vaddr, len(data), len(data), 7, 4) for vaddr, data in segments]
    rows: List[Tuple[int, ...]] = [(0,) * 10]
    index_of: Dict[str, int] = {}
    for s in sections:
        nobits = s.get("nobits", False)
        data = s.get("data", b"")
        size = s.get("size", len(data))
        rows.append((add_name(s["name"]), SHT_NOBITS if nobits else SHT_PROGBITS, s.get("flags", SHF_ALLOC), s.get("addr", 0),
                     0 if nobits else place(data), size, 0, 0, 4, 0))
        index_of[s["name"]] = len(rows) - 1
    symtab_index = 0
    if symbols:
        strtab = bytearray(b"\0")
        sym_bytes = bytearray(16)
        for sym in symbols:
            nm = 0
            if sym["name"]:
                nm = len(strtab)
                strtab.extend(sym["name"].encode() + b"\0")
            sym_bytes.extend(struct.pack("<IIIBBH", nm, sym.get("value", 0), sym.get("size", 0), (sym.get("bind", 1) << 4) | sym.get("type", 0), 0, sym.get("shndx", 0)))
        symtab_index = len(rows)
        rows.append((add_name(".symtab"), SHT_SYMTAB, 0, 0, place(bytes(sym_bytes)), len(sym_bytes), symtab_index + 1, 1, 4, 16))
        rows.append((add_name(".strtab"), SHT_STRTAB, 0, 0, place(bytes(strtab)), len(strtab), 0, 0, 1, 0))
    for r in relocs:
        data = b"".join(struct.pack("<II", off, (sym << 8) | typ) for off, sym, typ in r["entries"])
        rows.append((add_name(".rel" + r["section"]), SHT_REL, 0, 0, place(data), len(data), symtab_index, index_of[r["section"]], 4, 8))
    rows.append((add_name(".shstrtab"), SHT_STRTAB, 0, 0, place(bytes(shstr)), len(shstr), 0, 0, 1, 0))
    shoff = base + len(body)
    table = b"".join(struct.pack("<IIIIIIIIII", *row) for row in rows)
    header = b"\x7fELF" + bytes([1, 1, 1, 0]) + b"\0" * 8
    header += struct.pack("<HHIIIIIHHHHHH", 2 if segments else 1, 8, 1, entry, 52 if segments else 0, shoff, 0, 52, 32, len(segments), 40, len(rows), len(rows) - 1)
    return header + b"".join(struct.pack("<IIIIIIII", *e) for e in seg_entries) + bytes(body) + table


def image_elf(*chunks: Tuple[int, bytes], symbols: Sequence[Tuple[str, int, int]] = ()) -> bytes:
    """An executable holding each `(address, bytes)` chunk; `symbols` are `(name, address, type)` (2 function, 1 object)."""
    syms = [{"name": n, "value": a, "size": 0, "bind": 1, "type": t, "shndx": SHN_ABS} for n, a, t in symbols]
    return build_elf([], syms, [], segments=list(chunks))


# ------------------------------------------------------------------ assembler

INSN = [
    (re.compile(r"^lui\s+\$(\d+),\s*%hi\(([^)]+)\)$"), "hi"),
    (re.compile(r"^lw\s+\$(\d+),\s*%lo\(([^)]+)\)\(\$(\d+)\)$"), "lo"),
    (re.compile(r"^lw\s+\$(\d+),\s*%gp_rel\(([^)]+)\)\(\$28\)$"), "gp"),
    (re.compile(r"^jal\s+(\S+)$"), "jal"),
    (re.compile(r"^addiu\s+\$(\d+),\s*\$(\d+),\s*(-?\w+)$"), "addiu"),
    (re.compile(r"^jr\s+\$31$"), "jr"),
    (re.compile(r"^nop$"), "nop"),
    (re.compile(r"^\.insn\s+(\w+)$"), "insn"),
]
IGNORED = ("type", "size", "ident", "set", "file", "option", "mask", "fmask", "frame", "ent", "end", "gcc2_compiled", "abicalls", "cpload")
DATA_KIND = {"rdata": ".rodata", "rodata": ".rodata", "data": ".data", "sdata": ".sdata", "bss": ".bss", "sbss": ".sbss"}


class Asm:
    def __init__(self) -> None:
        self.sections: Dict[str, Dict[str, Any]] = {}
        self.labels: Dict[str, Tuple[str, int]] = {}
        self.globls: set[str] = set()
        self.commons: Dict[str, int] = {}
        self.cur = ".text"
        self.prev = ".text"

    def sec(self, name: str, *, nobits: bool = False, flags: int = SHF_ALLOC) -> Dict[str, Any]:
        if name not in self.sections:
            self.sections[name] = {"name": name, "data": bytearray(), "size": 0, "nobits": nobits, "flags": flags, "relocs": []}
        return self.sections[name]

    def switch(self, name: str, **kw: Any) -> None:
        self.prev, self.cur = self.cur, name
        self.sec(name, **kw)

    def emit(self, data: bytes, reloc: Optional[Tuple[str, int]] = None) -> None:
        s = self.sections[self.cur]
        if reloc:
            s["relocs"].append((len(s["data"]), reloc[0], reloc[1]))
        s["data"].extend(data)
        s["size"] = len(s["data"])

    def line(self, raw: str) -> None:
        line = raw.split("#", 1)[0].strip()
        if not line:
            return
        m = re.match(r"^([A-Za-z_.$][\w.$]*):\s*(.*)$", line)
        if m:
            s = self.sections[self.cur]
            self.labels[m.group(1)] = (self.cur, s["size"])
            line = m.group(2).strip()
            if not line:
                return
        if line.startswith("."):
            self.directive(line)
            return
        for rx, kind in INSN:
            m = rx.match(line)
            if m:
                self.insn(kind, m.groups())
                return
        raise ValueError(f"cannot assemble: {raw!r}")

    def directive(self, line: str) -> None:
        parts = line.split(None, 1)
        name, rest = parts[0][1:], (parts[1].strip() if len(parts) > 1 else "")
        if name == "text":
            self.switch(".text", flags=SHF_ALLOC | SHF_EXEC)
        elif name in DATA_KIND:
            k = DATA_KIND[name]
            self.switch(k, nobits=k in (".bss", ".sbss"), flags=SHF_ALLOC | (0 if k == ".rodata" else SHF_WRITE))
        elif name == "section":
            parts = [p.strip() for p in rest.split(",")]
            flags = parts[1].strip('"') if len(parts) > 1 else ""
            self.switch(parts[0], nobits="@nobits" in parts, flags=SHF_ALLOC | (SHF_EXEC if "x" in flags else 0) | (SHF_WRITE if "w" in flags else 0))
        elif name == "previous":
            self.cur, self.prev = self.prev, self.cur
        elif name in ("globl", "global"):
            self.globls.add(rest)
        elif name == "comm":
            n, size = [x.strip() for x in rest.split(",")][:2]
            self.commons[n] = int(size, 0)
        elif name == "word":
            for item in rest.split(","):
                item = item.strip()
                if re.match(r"^-?(0x[0-9a-fA-F]+|\d+)$", item):
                    self.emit(struct.pack("<I", int(item, 0) & 0xFFFFFFFF))
                else:
                    self.emit(b"\0\0\0\0", (item, R32))
        elif name == "byte":
            for item in rest.split(","):
                self.emit(bytes([int(item.strip(), 0) & 0xFF]))
        elif name == "space":
            n = int(rest.split(",")[0], 0)
            s = self.sections[self.cur]
            if s["nobits"]:
                s["size"] += n
            else:
                self.emit(b"\0" * n)
        elif name in ("align", "p2align", "extern") or name in IGNORED:
            if name in ("align", "p2align") and not self.sections[self.cur]["flags"] & SHF_EXEC:
                s = self.sections[self.cur]
                a = 1 << int(rest.split(",")[0], 0)
                pad = -s["size"] % a
                if s["nobits"]:
                    s["size"] += pad
                else:
                    self.emit(b"\0" * pad)
        else:
            raise ValueError(f"unknown directive: {line!r}")

    def insn(self, kind: str, g: Tuple[str, ...]) -> None:
        if kind == "hi":
            self.emit(struct.pack("<I", lui(int(g[0]), 0)), (g[1], HI16))
        elif kind == "lo":
            self.emit(struct.pack("<I", lw(int(g[0]), 0, int(g[2]))), (g[1], LO16))
        elif kind == "gp":
            self.emit(struct.pack("<I", lw(int(g[0]), 0, GP)), (g[1], GPREL16))
        elif kind == "jal":
            self.emit(struct.pack("<I", jal(0)), (g[0], R26))
        elif kind == "addiu":
            self.emit(struct.pack("<I", addiu(int(g[0]), int(g[1]), int(g[2], 0))))
        elif kind == "jr":
            self.emit(struct.pack("<I", JR_RA))
        elif kind == "nop":
            self.emit(struct.pack("<I", NOP))
        else:
            self.emit(struct.pack("<I", int(g[0], 0)))

    def object(self) -> bytes:
        order = [s for s in self.sections.values() if s["size"]]
        index = {s["name"]: i + 1 for i, s in enumerate(order)}
        syms: List[Dict[str, Any]] = []
        sym_of: Dict[str, int] = {}

        def symbol(name: str, offset: int) -> int:
            if name in self.labels:
                sec, off = self.labels[name]
                if sec.startswith(".text"):
                    key = name
                    if key not in sym_of:
                        syms.append({"name": name, "value": off, "size": 0, "bind": 1, "type": 2, "shndx": index[sec]})
                        sym_of[key] = len(syms)
                    return sym_of[key]
                key = "sec:" + sec
                if key not in sym_of:
                    syms.append({"name": "", "value": 0, "size": 0, "bind": 0, "type": 3, "shndx": index[sec]})
                    sym_of[key] = len(syms)
                return sym_of[key]
            if name in self.commons:
                if name not in sym_of:
                    size = self.commons[name]
                    syms.append({"name": name, "value": 4, "size": size, "bind": 1, "type": 1, "shndx": SHN_SCOMMON if size <= 8 else SHN_COMMON})
                    sym_of[name] = len(syms)
                return sym_of[name]
            if name not in sym_of:
                syms.append({"name": name, "value": 0, "size": 0, "bind": 1, "type": 0, "shndx": 0})
                sym_of[name] = len(syms)
            return sym_of[name]

        for s in order:
            if s["name"].startswith(".text"):
                for name, (sec, off) in self.labels.items():
                    if sec == s["name"] and name in self.globls:
                        symbol(name, 0)
        for name in self.commons:
            symbol(name, 0)
        rels = []
        for s in order:
            entries = []
            for off, target, typ in s["relocs"]:
                entries.append((off, symbol(target, 0), typ))
                if typ in (R32, LO16, GPREL16) and target in self.labels and not self.labels[target][0].startswith(".text") and self.labels[target][1]:
                    # a label inside its section: its offset is the addend, kept in the word as a REL relocation does
                    word = struct.unpack_from("<I", s["data"], off)[0] + self.labels[target][1]
                    struct.pack_into("<I", s["data"], off, word & 0xFFFFFFFF)
            if entries:
                rels.append({"section": s["name"], "entries": entries})
        secs = [{"name": s["name"], "data": bytes(s["data"]), "nobits": s["nobits"], "size": s["size"], "flags": s["flags"]} for s in order]
        return build_elf(secs, syms, rels)


def assemble(text: str) -> bytes:
    a = Asm()
    a.switch(".text", flags=SHF_ALLOC | SHF_EXEC)
    a.prev = ".text"
    for raw in text.splitlines():
        a.line(raw)
    return a.object()


def fake_toolchain_args(log: Optional[Path] = None) -> Dict[str, Any]:
    py = sys.executable
    base = f"{py} {FAKE_TOOL}"
    env = f" --log {log}" if log else ""
    return {
        "cpp": f"{base} cpp{env} -lang-c -Iinclude",
        "cc1": f"{base} cc1{env} -quiet -ffunction-sections",
        "assembler": f"{base} as{env} -EL -mabi=eabi",
        "linker": [py, FAKE_TOOL, "ld", "-EL"],
    }
