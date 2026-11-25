"""Helpers for the assembly a splat project writes: one file per translation unit, with a `nonmatching` line before each function.

`split_tu` takes the functions you have matched out of a unit's assembly, so the rest can still be assembled and linked around
the C you compiled. These work on text: they run no tool.
"""

from __future__ import annotations

import re
from pathlib import Path
from typing import Any, List, Sequence, Set, Tuple

COP2_LINE = re.compile(r"^(\s*/\* [0-9A-F]+ [0-9A-F]+ ([0-9A-F]{8}) \*/[ \t]+)(v[a-z0-9.]+[ \t].*|vnop|vwaitq)[ \t]*$", re.M)


def fix_cop2(text: str) -> str:
    """The assembler rejects the VU0 macro-mode operand syntax a disassembler writes; each such instruction becomes the `.word` it is
    (the original text stays in a comment)."""
    return COP2_LINE.sub(
        lambda m: f"{m.group(1)}.word 0x{bytes.fromhex(m.group(2))[::-1].hex().upper()} /* {m.group(3)} */", text
    )


def split_tu(text: str, drop: Set[str], keep_pad: bool = False) -> Tuple[str, List[str]]:
    """Drop the functions named in `drop` from a translation unit's assembly; each run of kept functions goes in its own section
    `.text.p<k>`. Returns `(assembly, order)`, where `order` lists the section names and the dropped function names in address order.

    `keep_pad`: the padding after a dropped function's end label stays as assembly (for objects whose padding is part of the
    object: padding to 16 or more, and objects that start 4 mod 8)."""
    lines = text.splitlines(keepends=True)
    head_end = next(i for i, l in enumerate(lines) if l.startswith(".section .text"))
    starts: List[Tuple[int, str]] = []
    for i, l in enumerate(lines):
        m = re.match(r"nonmatching (\S+),", l)
        if m:
            j = i - 1 if i > 0 and lines[i - 1].startswith(".align") else i
            starts.append((j, m.group(1)))
    out: List[str] = lines[:head_end]
    order: List[str] = []
    part, open_part = 0, False
    out += lines[head_end + 1 : starts[0][0]] if starts else lines[head_end + 1 :]
    for k, (j, name) in enumerate(starts):
        end = starts[k + 1][0] if k + 1 < len(starts) else len(lines)
        if name in drop:
            order.append(name)
            open_part = False
            tail = next((i + 1 for i in range(j, end) if lines[i].startswith("endlabel ")), end)
            pad = [l for l in lines[tail:end] if re.match(r"\s*/\* [0-9A-F]+ [0-9A-F]+ [0-9A-F]+ \*/", l)]
            if keep_pad and pad:
                # only the padding words: an .align here belongs to the next function
                out.append(f'\n.section .text.p{part}, "ax"\n')
                order.append(f".text.p{part}")
                part += 1
                open_part = True
                out += pad
            continue
        if not open_part:
            out.append(f'\n.section .text.p{part}, "ax"\n')
            order.append(f".text.p{part}")
            part += 1
            open_part = True
        out += lines[j:end]
    if keep_pad:
        # every word is in place, padding included; an .align would make the linker pad a part that starts 4 mod 8 after a C function
        out = out[:head_end] + [l for l in out[head_end:] if not l.startswith(".align")]
    return "".join(out), order


def strip_attributes(text: str) -> str:
    """Drop every `__attribute__((...))` (balanced parentheses), so that a declaration like
    `int t[4] __attribute__((section(".rodata"))) = {...}` reads as the data definition it is."""
    while True:
        m = re.search(r"__attribute__\s*\(", text)
        if not m:
            return text
        depth, i = 0, m.end() - 1
        while i < len(text):
            depth += {"(": 1, ")": -1}.get(text[i], 0)
            if depth == 0:
                break
            i += 1
        text = text[: m.start()] + text[i + 1 :]


def unstatic(text: str) -> str:
    """Drop a leading `static` (after comments) from a function or a prototype."""
    return re.sub(r"^((?:\s|/\*.*?\*/)*)static\s+", r"\1", text, count=1, flags=re.S)


def is_static_prototype(chunk: str) -> bool:
    body = re.sub(r"/\*.*?\*/|//[^\n]*", "", chunk, flags=re.S).strip()
    return body.startswith("static") and body.endswith(");") and "(" in body and "=" not in body


__all__: Sequence[Any] = ["fix_cop2", "split_tu", "strip_attributes", "unstatic", "is_static_prototype"]


def fix_cop2_tree(folder: "str | Path") -> int:
    """`fix_cop2` on every `.s` file under `folder`, in place. Returns the number of files changed."""
    changed = 0
    for f in Path(folder).rglob("*.s"):
        text = f.read_text()
        new = fix_cop2(text)
        if new != text:
            f.write_text(new)
            changed += 1
    return changed
