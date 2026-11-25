"""Byte-check C functions against an original PS2 executable.

For each C file: preprocess and compile it with the game's compiler (the EE-GCC the SDK shipped, run through your toolchain
commands), assemble it, and link every function section at its original address with every other symbol at its own address. A
function matches when its compiled bytes equal the original's and the original's remaining bytes up to the next function are zero
padding.

The object's own data (jump tables and literals in `.rodata` and `.lit4`, `.sdata`/`.sbss`, `.data`/`.bss`), its common variables and
any extern without an address are linked where the original code points: each is first given a probe address, and the original's
instructions that refer to it say where it really sits (the same `lui`/`%lo`, `$gp`-relative or `jal` instruction in both builds).
Each data label is its own section (`datafrag`), so a function's literals, switch tables and statics are placed one by one, as they
sit in the original's whole-file sections; a reference is attributed by its relocation, and data that only placed data points at (a
table of strings) is solved through those pointers. A fragment nothing refers to is reported as unused. `data_ok` separately says
whether the object's own initialised data equals the original where it was placed.

The compiler runs wherever your `Toolchain` says (a container, qemu, directly): its commands run under a shell in the folder of the
compile, through `runner`, with `path_map` translating host folders into the runner's.
"""

from __future__ import annotations

import re
import shutil
import struct
import subprocess
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, Iterator, List, Optional, Sequence, Set, Tuple, Union

from . import datafrag
from .elf32 import Elf, Image, SHN_UNDEF
from .mips import refs, words
from .symbols import duplicate_functions, starts_of

PathLike = Union[str, Path]
Result = Dict[str, Any]
Key = Tuple[str, str]
"""`("sec", section name)` or `("sym", symbol name)`: a thing whose address is solved."""

DEFAULT_DEFINES = "-D__GNUC__=2 -D__GNUC_MINOR__=9 -D_LANGUAGE_C -DLANGUAGE_C -D__mips__ -DSPLAT"

INPUTS = {
    ".rodata": "*(.rodata)", ".data": "*(.data)", ".bss": "*(.bss) *(COMMON)", ".sdata": "*(.sdata)", ".lit4": "*(.lit4)",
    ".lit8": "*(.lit8)", ".sbss": "*(.sbss) *(.scommon)",
}


@dataclass
class Toolchain:
    """How to compile and link. The compiler commands are strings run by `sh -c` in the compile's folder, in the runner; the linker
    runs on the host.

    - `cpp`: the preprocessor up to the file arguments, flags included (`"<runner-specific> .../cpp -lang-c -Iinclude"`).
    - `cc1`: the compiler proper, flags that are always wanted included (`".../cc1 -quiet -ffunction-sections"`).
    - `assembler`: the assembler up to its file arguments (`".../as -EL -mabi=eabi"`).
    - `linker`: the host linker's argv up to its mode flags (`["mips-ps2-decompals-ld", "-EL"]`).
    - `defines`: added to the preprocessor's command line, after `extra_defines` of the target.
    - `runner`: the prefix that runs `sh -c <script>` (`["docker", "exec", "box"]`; the default `env` runs it here).
    - `path_map`: `(host prefix, runner prefix)` pairs, for a runner that sees folders under other names."""

    cpp: str
    cc1: str
    assembler: str
    linker: Sequence[str] = ("ld", "-EL")
    defines: str = DEFAULT_DEFINES
    runner: Sequence[str] = ("env",)
    path_map: Sequence[Tuple[str, str]] = ()

    def guest(self, path: PathLike) -> str:
        p = str(path)
        for host, guest in self.path_map:
            if p.startswith(host):
                return guest + p[len(host):]
        if self.path_map:
            raise ValueError(f"{p} is not visible to the runner (path_map has no entry for it)")
        return p

    def sh(self, script: str) -> Tuple[int, str]:
        r = subprocess.run([*self.runner, "sh", "-c", script], capture_output=True, text=True)
        return r.returncode, r.stdout + r.stderr


class Verifier:
    """One original image and one way to build. Make one and call `check` for each C file.

    - `image`: the original executable (an `Image`, or a path).
    - `symbols`: name to address (`symbols.read_symbol_file`, `symbols_from_elf`). `funcs`: function name to every address it has
      (for a name that repeats: file statics), from `symbols_from_elf`.
    - `gp`: the original's `$gp`.
    - `include_dir`: copied into each compile's folder as `include/` (the headers the C files include).
    - `defines`: extra `-D` flags for this build (a region or version), such as `-DVERSION_JP`.
    - `fragments`: place data one label at a time (the default); False compiles each file's data as the compiler laid it out.
    """

    def __init__(
        self,
        image: Union[Image, PathLike],
        symbols: Dict[str, int],
        toolchain: Toolchain,
        *,
        gp: int,
        include_dir: Optional[PathLike] = None,
        funcs: Optional[Dict[str, Set[int]]] = None,
        defines: str = "",
        fragments: bool = True,
    ) -> None:
        self.image = image if isinstance(image, Image) else Image.open(image)
        self.syms = dict(symbols)
        self.funcs = funcs or {}
        self.toolchain = toolchain
        self.include_dir = Path(include_dir) if include_dir is not None else None
        self.defines = defines
        self.fragments = fragments
        self.starts = starts_of(self.syms, self.funcs)
        self.dups = duplicate_functions(self.funcs)
        self.gp = gp
        self.probes: Dict[str, int] = {
            ".rodata": 0x0F000000, ".data": 0x0E800000, ".bss": 0x0E400000, ".sdata": gp + 0x5000, ".lit4": gp + 0x5800,
            ".lit8": gp + 0x6000, ".sbss": gp + 0x6800,
        }
        # where each kind's fragment probes start, and where they must stop: below $gp, clear of near_gp_slots (gp - 0x4c00 and up)
        self.frag_probes: Dict[str, Tuple[int, int]] = {
            ".rodata": (0x0F100000, 0x0F800000), ".data": (0x0E900000, 0x0F000000), ".bss": (0x0E500000, 0x0E800000),
            ".sdata": (gp - 0x7FF0, gp - 0x6400), ".sbss": (gp - 0x6400, gp - 0x4C00), ".lit4": (gp + 0x7000, gp + 0x7FF0),
        }

    def read(self, addr: int, n: int) -> Optional[bytes]:
        return self.image.bytes_at(addr, n)

    # ---------------------------------------------------------------- compile

    def compile_c(self, src: PathLike, work: PathLike, flags: str = "-O2 -G8", *, assembler: Optional[str] = None, frag: bool = True) -> Tuple[Optional[Path], str]:
        """Compile one C file to `work/out.o`. Returns `(object or None, the tools' output)`."""
        tc = self.toolchain
        work = Path(work)
        work.mkdir(parents=True, exist_ok=True)
        if self.include_dir is not None:
            inc = work / "include"
            shutil.rmtree(inc, ignore_errors=True)
            shutil.copytree(self.include_dir, inc)
        shutil.copy(src, work / "in.c")
        for h in Path(src).parent.glob("*.h"):  # headers next to the source
            shutil.copy(h, work / h.name)
        g = tc.guest(work)
        rc, out = tc.sh(
            f"set -e; cd {g}; {tc.cpp} {tc.defines} {self.defines} in.c -o in.i; {tc.cc1} {flags} in.i -o raw.s"
        )
        if rc:
            return None, out
        gas = assembler or tc.assembler
        for f in ("out.o", "ref.o", "code.o", "data.o", "data.s", "frag.o", "merged.o"):
            (work / f).unlink(missing_ok=True)
        # each data label in its own section (datafrag); the plain object is the reference, and the fallback
        try:
            tag = datafrag.TAG + "_" + re.sub(r"\W", "_", Path(src).stem) + "_"
            code, data, nfrag = datafrag.split((work / "raw.s").read_text(), tag) if self.fragments and frag else ("", "", 0)
        except ValueError as e:
            code, data, nfrag = "", "", 0
            out += f"\ndatafrag: {e}\n"
        shutil.copy(work / "raw.s", work / "out.s")
        if not nfrag:
            rc, out2 = tc.sh(f"set -e; cd {g}; {gas} out.s -o out.o")
            return (None if rc else work / "out.o"), out + out2
        (work / "code.s").write_text(code)
        if data:
            (work / "data.s").write_text(data)
        rc, out2 = tc.sh(f"set -e; cd {g}; {gas} out.s -o ref.o; {gas} code.s -o code.o" + (f"; {gas} data.s -o data.o" if data else ""))
        if not (work / "ref.o").exists():
            return None, out + out2
        why: Optional[str] = None
        if rc:
            why = "fragmented assembly does not assemble"
        elif data:
            (work / "merge.ld").write_text("SECTIONS { }\n")
            r = subprocess.run([*tc.linker, "-r", "-T", str(work / "merge.ld"), "-o", str(work / "frag.o"), str(work / "code.o"), str(work / "data.o")],
                               capture_output=True, text=True)
            if r.returncode:
                why = "merge failed: " + r.stderr[-500:]
        else:
            shutil.copy(work / "code.o", work / "frag.o")
        if why is None and datafrag.code_shape(work / "frag.o") != datafrag.code_shape(work / "ref.o"):
            why = "fragmented code differs from the plain assembly"
        if why:
            out2 += f"\ndatafrag: {why}; using the plain object\n"
            shutil.copy(work / "ref.o", work / "out.o")
        else:
            shutil.copy(work / "frag.o", work / "out.o")
        return work / "out.o", out + out2

    # ------------------------------------------------------------------ link

    def link(self, obj: PathLike, work: Path, func_secs: Dict[str, int], sec_addr: Dict[str, int], sym_addr: Dict[str, int], frags: Sequence[str] = ()) -> Tuple[Optional[Path], List[str], str]:
        """Function sections at their addresses; data sections and symbols where given. Returns `(linked ELF or None, missing symbols, linker output)`."""
        lines = [f"_gp = 0x{self.gp:08x};"]
        lines += [f"{n} = 0x{a:08x};" for n, a in sorted(sym_addr.items())]
        lines.append("SECTIONS {")
        for f, addr in func_secs.items():
            lines.append(f"  .text.{f} 0x{addr:08x} : {{ *(.text.{f}) }}")
        lines.append("  .text 0x0e000000 : { *(.text) }")
        for s, inp in INPUTS.items():
            lines.append(f"  {s} 0x{sec_addr.get(s, self.probes[s]):08x} : {{ {inp} }}")
        for s in frags:
            lines.append(f"  {s} 0x{sec_addr[s]:08x} : {{ *({s}) }}")
        lines.append("  /DISCARD/ : { *(.reginfo) *(.mdebug) *(.comment) *(.pdr) *(.gnu.attributes) }")
        lines.append("}")
        (work / "link.ld").write_text("\n".join(lines) + "\n")
        r = subprocess.run([*self.toolchain.linker, "--no-check-sections", "-T", str(work / "link.ld"), "-o", str(work / "out.elf"), str(obj)],
                           capture_output=True, text=True)
        missing = sorted({m for m in re.findall(r"undefined reference to `([^']+)'", r.stderr)})
        return (work / "out.elf" if r.returncode == 0 else None), missing, r.stderr

    def near_gp_slots(self) -> Iterator[int]:
        # clear of the section probes (gp + 0x5000 .. gp + 0x7000)
        for k in range(1, 20):
            yield self.gp + 0x400 * k
            yield self.gp - 0x400 * k

    # ----------------------------------------------------------------- check

    def check(self, src: PathLike, work: PathLike, flags: str = "-O2 -G8", *, assembler: Optional[str] = None, frag: bool = True,
              prefer: Optional[Tuple[int, int]] = None) -> List[Result]:
        """One result dict per function of the C file (or one error dict). `prefer=(lo, hi)`: a function name with several addresses
        (file statics) resolves to the one in `[lo, hi)`, the C file's own translation unit.

        Statuses: `match`, `mismatch`, `no-address` (the function has no address in `symbols`), and for the file as a whole
        `compile-error` or `link-error`."""
        src = Path(src)
        work = Path(work)
        obj, log = self.compile_c(src, work, flags, assembler=assembler, frag=frag)
        if obj is None:
            return [{"file": str(src), "status": "compile-error", "log": log[-2000:]}]
        syms = self.syms
        if prefer and self.dups:
            syms = dict(syms)
            for n, addrs in self.dups.items():
                mine = [a for a in addrs if prefer[0] <= a < prefer[1]]
                if mine:
                    syms[n] = mine[0]
        info = ObjectInfo.read(obj)
        names, secs, commons, undefined = info.functions, info.sections, info.commons, info.undefined
        func_secs = {n: syms[n] for n in names if n in syms}

        # Probe items: the object's data sections (whole, or one per datafrag fragment), its common symbols and any undefined symbol
        # without an address. Each gets its own probe range; the original code's reference at the same instruction gives its address.
        slots = self.near_gp_slots()
        items: Dict[Key, Tuple[int, int]] = {}  # key -> (probe, span)
        sym_addr = {n: syms[n] for n in undefined if n in syms}
        for n, (size, small) in commons.items():
            if n in syms:
                sym_addr[n] = syms[n]
            else:
                items[("sym", n)] = (next(slots) if small or size <= 8 else 0x0C000000 + 0x100000 * len(items), max(size, 4))
        for n in sorted(undefined - set(syms) - {"_gp"} - set(names)):
            items[("sym", n)] = (next(slots), 0x400)
        frags: List[str] = []
        cursor = {k: lo for k, (lo, hi) in self.frag_probes.items()}
        for sname in secs:
            if sname in self.probes:
                items[("sec", sname)] = (self.probes[sname], max(secs[sname], 4))
                continue
            kind = datafrag.kind_of(sname)
            assert kind is not None
            a = cursor[kind]
            if a + secs[sname] > self.frag_probes[kind][1]:
                if not frag:
                    break
                # more fragments than probe room near $gp: check the plain object
                return self.check(src, work, flags, assembler=assembler, frag=False, prefer=prefer)
            cursor[kind] = (a + secs[sname] + 4 + 0xF) & ~0xF  # a gap, so an end address is unambiguous
            items[("sec", sname)] = (a, secs[sname])
            frags.append(sname)

        def assign(solved: Dict[Key, int]) -> Tuple[Dict[str, int], Dict[str, int]]:
            sa = dict(sym_addr)
            sec: Dict[str, int] = {}
            for (kind, n), (probe, _) in items.items():
                a = solved.get((kind, n), probe)
                if kind == "sym":
                    sa[n] = a
                else:
                    sec[n] = a
            return sec, sa

        sec, sa = assign({})
        elf, missing, err = self.link(obj, work, func_secs, sec, sa, frags)
        if elf is None:
            return [{"file": str(src), "status": "link-error", "missing": missing, "log": err[-2000:]}]
        got, data = linked_parts(elf)

        def owner(a: int, linked: Sequence[Key]) -> Tuple[Optional[Key], Optional[int]]:
            """The probe item whose range holds a (a fragment's end counts as its own)."""
            for k, (probe, span) in items.items():
                if k in linked:
                    continue
                base = data[k[1]][0] if k[0] == "sec" and k[1] in data else probe
                if base <= a < base + span or (k[0] == "sec" and k[1] in frags and a == base + span):
                    return k, base
            return None, None

        # a reference's relocation names its item; the address only gives the offset (a probe can share an address with a real
        # variable the code uses)
        rmap = info.reloc_map

        def item(section: str, off: int, a: int, linked: Sequence[Key]) -> Tuple[Optional[Key], Optional[int]]:
            if (section, off) not in rmap:
                return owner(a, linked)  # no relocation: go by the address
            k = rmap[section, off]
            if k is None or k not in items or k in linked:
                return None, None
            return k, (data[k[1]][0] if k[0] == "sec" and k[1] in data else items[k][0])

        votes: Dict[Key, Counter[int]] = {k: Counter() for k in items}
        for n, (addr, b) in got.items():
            orig = self.read(addr, len(b))
            if not orig or n not in func_secs:
                continue
            o = {i: a for i, a, _ in refs(words(orig), addr, self.gp)}
            for i, a, _ in refs(words(b), addr, self.gp):
                if i not in o:
                    continue
                k, base = item(f".text.{n}", 4 * i, a, ())
                if k is not None and base is not None:
                    votes[k][o[i] - (a - base)] += 1
        solved: Dict[Key, int] = {k: c.most_common(1)[0][0] for k, c in votes.items() if c}
        for _ in range(8):
            if solved:
                sec, sa = assign(solved)
                elf2, missing, err = self.link(obj, work, func_secs, sec, sa, frags)
                if elf2 is None:
                    return [{"file": str(src), "status": "link-error", "missing": missing, "log": err[-2000:]}]
                got, data = linked_parts(elf2)
            # pointers in placed data reach items no code refers to directly (a table of strings): the original word at the same offset gives them
            more: Dict[Key, Counter[int]] = {}
            for sname, (pa, size, content) in data.items():
                if content is None or ("sec", sname) not in solved:
                    continue
                orig = self.read(pa, size)
                if orig is None:
                    continue
                for off in range(0, size - 3, 4):
                    if (sname, off) not in rmap:
                        continue  # not a pointer
                    w, ow = struct.unpack_from("<I", content, off)[0], struct.unpack_from("<I", orig, off)[0]
                    k, base = item(sname, off, w, list(solved))
                    if k is not None and base is not None:
                        more.setdefault(k, Counter())[ow - (w - base)] += 1
            if not more:
                break
            solved.update({k: c.most_common(1)[0][0] for k, c in more.items()})
        solved_syms = {n: f"0x{a:08x}" for (kind, n), a in solved.items() if kind == "sym"}
        unsolved = sorted(n for (kind, n) in items if kind == "sym" and (kind, n) not in solved)
        # a fragment that neither the code nor placed data points at is another function's copy (or dead): nothing here depends on its address
        wanted: Set[str] = set()
        for src_sec, tg in info.reloc_targets.items():
            if src_sec.startswith(".text") or (datafrag.kind_of(src_sec) and ("sec", src_sec) in solved):
                wanted |= tg

        results: List[Result] = []
        for n in names:
            if n not in func_secs:
                results.append({"name": n, "status": "no-address"})
                continue
            addr, b = got[n]
            nxt = next((x for x in self.starts if x > addr), addr + len(b))
            orig_b = self.read(addr, nxt - addr)
            cw = words(b)
            ow = words(orig_b[: len(b)]) if orig_b and len(orig_b) >= len(b) else []
            diff = sum(1 for x, y in zip(cw, ow) if x != y) + abs(len(cw) - len(ow))
            pad_ok = orig_b is not None and all(x == 0 for x in orig_b[len(b):])
            r: Result = {"name": n, "addr": f"0x{addr:06x}", "size": len(b), "orig_size": nxt - addr, "diff_words": diff, "padding_ok": pad_ok}
            for sname, (pa, size, content) in data.items():
                placed = ("sec", sname) in solved
                dd: Dict[str, str] = r.setdefault("data", {})
                if placed:
                    dd[sname] = f"0x{pa:06x}+{size}"
                elif sname in frags and sname not in wanted:
                    dd[sname] = f"unused+{size}"
                else:
                    dd[sname] = f"unplaced+{size}"
                if content is not None and placed:
                    r["data_ok"] = r.get("data_ok", True) and self.read(pa, size) == content
            if solved_syms:
                r["solved"] = solved_syms
            if unsolved:
                r["unsolved"] = unsolved
            ok = diff == 0 and pad_ok  # data_ok is reported, not required
            r["status"] = "match" if ok else "mismatch"
            results.append(r)
        return results


@dataclass
class ObjectInfo:
    """What the verifier needs from one compiled object."""

    functions: List[str]
    """Names of the `.text.<name>` sections, in file order."""
    sections: Dict[str, int]
    """Data sections (and fragments) with a size, to that size."""
    commons: Dict[str, Tuple[int, bool]]
    """Common symbols: size and whether the small-data kind."""
    undefined: Set[str]
    reloc_map: Dict[Tuple[str, int], Optional[Key]] = field(default_factory=dict)
    """`(section, offset)` of each relocation to what it points at: `("sym", name)`, `("sec", name)` or None."""
    reloc_targets: Dict[str, Set[str]] = field(default_factory=dict)
    """Section to the data sections its relocations point into."""

    @classmethod
    def read(cls, path: PathLike) -> "ObjectInfo":
        elf = Elf.read(path)
        funcs = [s.name[6:] for s in elf.sections if s.name.startswith(".text.")]
        secs = {s.name: s.size for s in elf.sections if s.size and datafrag.kind_of(s.name)}
        commons: Dict[str, Tuple[int, bool]] = {}
        undefined: Set[str] = set()
        for sym in elf.symbols:
            if not sym.name:
                continue
            if sym.shndx == SHN_UNDEF:
                undefined.add(sym.name)
            elif sym.common:
                commons[sym.name] = (sym.size, sym.small_common)
        rmap: Dict[Tuple[str, int], Optional[Key]] = {}
        targets: Dict[str, Set[str]] = {}
        for _, target, rels in elf.relocations():
            for r in rels:
                sym = elf.symbols[r.sym]
                if sym.undefined or sym.common:
                    rmap[target.name, r.offset] = ("sym", sym.name)
                elif sym.defined_in_section and sym.shndx < len(elf.sections) and datafrag.kind_of(elf.sections[sym.shndx].name):
                    name = elf.sections[sym.shndx].name
                    rmap[target.name, r.offset] = ("sec", name)
                    targets.setdefault(target.name, set()).add(name)
                else:
                    rmap[target.name, r.offset] = None
        return cls(funcs, secs, commons, undefined, rmap, targets)


def linked_parts(path: PathLike) -> Tuple[Dict[str, Tuple[int, bytes]], Dict[str, Tuple[int, int, Optional[bytes]]]]:
    """From a linked ELF: `{function: (address, bytes)}` and `{data section: (address, size, bytes or None for nobits)}`."""
    elf = Elf.read(path)
    out: Dict[str, Tuple[int, bytes]] = {}
    data: Dict[str, Tuple[int, int, Optional[bytes]]] = {}
    for s in elf.sections:
        if s.name.startswith(".text."):
            out[s.name[6:]] = (s.addr, s.data())
        elif datafrag.kind_of(s.name) and s.size:
            data[s.name] = (s.addr, s.size, None if s.nobits else s.data())
    return out, data
