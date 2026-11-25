"""Symbol addresses: the `name = 0xADDR;` lines a splat project keeps, and the symbol table of an original that still has one."""

from __future__ import annotations

import re
from pathlib import Path
from typing import Dict, Optional, Set, Tuple, Union

from .elf32 import Elf, STB_GLOBAL, STT_FUNC, STT_NOTYPE, STT_OBJECT

PathLike = Union[str, Path]
SYMBOL_LINE = re.compile(r"\s*([A-Za-z_.$][\w.$]*)\s*=\s*(0x[0-9a-fA-F]+)\s*;")


def read_symbol_file(path: PathLike, *, require: Optional[str] = None) -> Dict[str, int]:
    """`name -> address` from a splat-style `symbol_addrs.txt`. A name seen twice keeps its first address. With `require`
    (`"type:func"`), only lines whose trailing comment holds that text are read."""
    out: Dict[str, int] = {}
    for line in Path(path).read_text(encoding="utf-8", errors="replace").splitlines():
        m = SYMBOL_LINE.match(line)
        if m and (require is None or require in line):
            out.setdefault(m.group(1), int(m.group(2), 16))
    return out


def symbols_from_elf(path: PathLike) -> Tuple[Dict[str, int], Dict[str, Set[int]]]:
    """`(symbols, functions)` of an original with a symbol table: every defined named symbol of type function, object or none, and
    the addresses of each function name (a file `static` of the same name appears once per file). A name that is both a local object
    and a global function takes the global one, then the first seen."""
    syms: Dict[str, int] = {}
    funcs: Dict[str, Set[int]] = {}
    rank: Dict[str, int] = {}
    for sym in Elf.read(path).symbols:
        if sym.name and not sym.undefined and sym.value and sym.type in (STT_FUNC, STT_OBJECT, STT_NOTYPE):
            r = 0 if sym.bind == STB_GLOBAL else 1
            if sym.type == STT_FUNC:
                funcs.setdefault(sym.name, set()).add(sym.value)
            if sym.name not in syms or r < rank[sym.name]:
                syms[sym.name], rank[sym.name] = sym.value, r
    return syms, funcs


def merge(*tables: Dict[str, int]) -> Dict[str, int]:
    """The first table's address wins for a name in several."""
    out: Dict[str, int] = {}
    for table in tables:
        for name, addr in table.items():
            out.setdefault(name, addr)
    return out


def starts_of(syms: Dict[str, int], funcs: Dict[str, Set[int]]) -> "list[int]":
    """Every address a symbol or a function begins at, sorted: where one function's bytes end and the next begin."""
    return sorted(set(syms.values()) | {a for v in funcs.values() for a in v})


def duplicate_functions(funcs: Dict[str, Set[int]]) -> Dict[str, "list[int]"]:
    """Function names with more than one address (file statics repeat a name), as sorted lists."""
    return {n: sorted(v) for n, v in funcs.items() if len(v) > 1}


__all__ = ["read_symbol_file", "symbols_from_elf", "merge", "starts_of", "duplicate_functions", "SYMBOL_LINE"]
