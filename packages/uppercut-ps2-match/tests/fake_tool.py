"""Stand-in cpp, cc1, as and ld for the tests: `python fake_tool.py cpp|cc1|as|ld [--log FILE] ARGS`.

The "C" file is assembly text in the dialect of ps2_support.assemble. cpp and cc1 copy it on (cc1 fails on a file that starts with
`ERROR`), `as` assembles it into an object, and `ld` places the sections as a link script says and applies the relocations
32, 26, HI16, LO16 and GPREL16 with zero addends. `ld -r` is refused, so a test can drive the verifier's fallback."""

from __future__ import annotations

import re
import struct
import sys
from pathlib import Path
from typing import List

HERE = Path(__file__).parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "src"))

from ps2_support import assemble, build_elf  # noqa: E402
from uppercut_ps2_match.elf32 import Elf  # noqa: E402


def main(argv: List[str]) -> int:
    tool, args = Path(argv[0]).name, argv[1:]
    if args[:1] == ["--log"]:
        with open(args[1], "a") as log:
            log.write(tool + " " + " ".join(args[2:]) + "\n")
        args = args[2:]
    if tool in ("cpp", "cc1"):
        src, out = args[args.index("-o") - 1], args[args.index("-o") + 1]
        text = Path(src).read_text()
        if tool == "cc1" and text.startswith("ERROR"):
            print("in.c:1: error: made-up compile error", file=sys.stderr)
            return 1
        Path(out).write_text(text)
        return 0
    if tool == "as":
        src, out = args[args.index("-o") - 1], args[args.index("-o") + 1]
        try:
            Path(out).write_bytes(assemble(Path(src).read_text()))
        except ValueError as error:
            print(f"{src}: Error: {error}", file=sys.stderr)
            return 1
        return 0
    if tool == "ld":
        return link(args)
    return 3


def signed16(word: int) -> int:
    value = word & 0xFFFF
    return value - 0x10000 if value & 0x8000 else value


def link(args: List[str]) -> int:
    if "-r" in args:
        print("ld: -r is not supported by the stand-in linker", file=sys.stderr)
        return 1
    script = Path(args[args.index("-T") + 1]).read_text()
    out = Path(args[args.index("-o") + 1])
    obj = Elf.read(args[-1])
    gp = int(re.search(r"_gp = 0x([0-9a-f]+);", script).group(1), 16)  # type: ignore[union-attr]
    symaddr = {m.group(1): int(m.group(2), 16) for m in re.finditer(r"^([A-Za-z_.$][\w.$]*) = 0x([0-9a-f]+);", script, re.M) if m.group(1) != "_gp"}
    place = {m.group(1): int(m.group(2), 16) for m in re.finditer(r"^\s+(\.\S+) 0x([0-9a-f]+) : \{", script, re.M)}
    data = {s.name: bytearray(s.data()) for s in obj.sections}
    addr_of = {s.name: place[s.name] for s in obj.sections if s.name in place and s.size}
    missing = set()
    for _, target, rels in obj.relocations():
        if target.name not in addr_of:
            continue
        for r in rels:
            sym = obj.symbols[r.sym]
            if sym.shndx == 0 or sym.common:
                if sym.name not in symaddr:
                    missing.add(sym.name)
                    continue
                s_addr = symaddr[sym.name]
            else:
                s_addr = addr_of[obj.sections[sym.shndx].name] + sym.value
            buf = data[target.name]
            word = struct.unpack_from("<I", buf, r.offset)[0]
            if r.type == 5:
                word = (word & 0xFFFF0000) | (((s_addr + 0x8000) >> 16) & 0xFFFF)
            elif r.type == 6:
                word = (word & 0xFFFF0000) | ((s_addr + signed16(word)) & 0xFFFF)
            elif r.type == 7:
                word = (word & 0xFFFF0000) | ((s_addr + signed16(word) - gp) & 0xFFFF)
            elif r.type == 4:
                word = (word & 0xFC000000) | ((s_addr >> 2) & 0x03FFFFFF)
            elif r.type == 2:
                word = (word + s_addr) & 0xFFFFFFFF
            struct.pack_into("<I", buf, r.offset, word)
    if missing:
        for name in sorted(missing):
            print(f"ld: in function: undefined reference to `{name}'", file=sys.stderr)
        return 1
    sections = []
    for s in obj.sections:
        if s.name in addr_of:
            entry = {"name": s.name, "addr": addr_of[s.name], "flags": s.flags}
            if s.nobits:
                entry.update(nobits=True, size=s.size)
            else:
                entry["data"] = bytes(data[s.name])
            sections.append(entry)
    out.write_bytes(build_elf(sections))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
