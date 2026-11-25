#!/usr/bin/env python3
"""Generate glyph tables from data/bitmap-fonts.txt, the one source of truth for bitmap-font.

  gen_bitmap_font.py tables [--out include/bfont/bfont_tables.h]
      The header the C library compiles in.
  gen_bitmap_font.py array --font 5x7 --name my_font --type u8 [--first 32] [--count 96] [--out FILE]
      A standalone C array for a project that vendors one table and not the library.
  gen_bitmap_font.py python --font 5x7 --name FONT_5X7 [--out FILE]
      A Python module with the font as rows of "0" and "1" characters.
  gen_bitmap_font.py check
      Regenerate every output kept in this repository and fail if one is out of date.

Standard library only. Python 3, standard library only.
"""
from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

HERE = Path(__file__).resolve().parent
PACKAGE = HERE.parent
DEFAULT_SOURCE = PACKAGE / "data" / "bitmap-fonts.txt"
TABLES_HEADER = PACKAGE / "include" / "bfont" / "bfont_tables.h"
SHEETS_MODULE = PACKAGE.parent / "uppercut-pixel-art-sheets" / "src" / "uppercut_pixel_art_sheets" / "_font5x7.py"
SOURCE_NAME = "data/bitmap-fonts.txt"


class SourceError(ValueError):
    """The source file is malformed. The message names the line."""


@dataclass
class Font:
    name: str
    width: int
    height: int
    advance: int
    glyphs: dict[int, tuple[str, ...]] = field(default_factory=dict)  # code -> rows of "." and "#"

    @property
    def first(self) -> int:
        return min(self.glyphs)

    @property
    def last(self) -> int:
        return max(self.glyphs)

    def row_value(self, code: int, row: int) -> int:
        """The row as an integer: the leftmost pixel is bit width-1."""
        value = 0
        for ch in self.glyphs[code][row]:
            value = (value << 1) | (ch == "#")
        return value


def parse(text: str) -> dict[str, Font]:
    """Parse the source text into fonts keyed by their ``WxH`` name, in file order."""
    fonts: dict[str, Font] = {}
    font: Font | None = None
    pending: tuple[int, int, list[str]] | None = None  # (code, line number, rows so far)

    def finish_glyph() -> None:
        nonlocal pending
        if pending is not None:
            code, lineno, rows = pending
            assert font is not None
            if len(rows) != font.height:
                raise SourceError(f"line {lineno}: glyph 0x{code:02X} has {len(rows)} rows, the font has {font.height}")
            font.glyphs[code] = tuple(rows)
            pending = None

    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.split(";", 1)[0].strip()
        if not line:
            continue
        words = line.split()
        if words[0] == "font":
            finish_glyph()
            m = re.fullmatch(r"(\d+)x(\d+)", words[1]) if len(words) == 4 else None
            if not m or words[2] != "advance" or not words[3].isdigit():
                raise SourceError(f"line {lineno}: expected 'font <width>x<height> advance <pixels>'")
            width, height = int(m.group(1)), int(m.group(2))
            if not (1 <= width <= 8 and 1 <= height <= 255):
                raise SourceError(f"line {lineno}: a font is 1 to 8 pixels wide and 1 to 255 tall")
            name = words[1]
            if name in fonts:
                raise SourceError(f"line {lineno}: font {name} appears twice")
            font = fonts[name] = Font(name, width, height, int(words[3]))
        elif words[0] == "glyph":
            finish_glyph()
            if font is None:
                raise SourceError(f"line {lineno}: a glyph before any font")
            try:
                code = int(words[1], 0) if len(words) == 2 else -1
            except ValueError:
                code = -1
            if not 0 <= code <= 255:
                raise SourceError(f"line {lineno}: expected 'glyph <code 0..255>'")
            if code in font.glyphs or (pending and pending[0] == code):
                raise SourceError(f"line {lineno}: glyph 0x{code:02X} appears twice in font {font.name}")
            pending = (code, lineno, [])
        elif re.fullmatch(r"[.#]+", line):
            if pending is None:
                raise SourceError(f"line {lineno}: pixel row outside a glyph")
            assert font is not None
            if len(line) != font.width:
                raise SourceError(f"line {lineno}: row is {len(line)} wide, the font is {font.width}")
            pending[2].append(line)
        else:
            raise SourceError(f"line {lineno}: unexpected text {line!r}")
    finish_glyph()
    for f in fonts.values():
        if not f.glyphs:
            raise SourceError(f"font {f.name} has no glyphs")
    if not fonts:
        raise SourceError("no fonts")
    return fonts


def load(path: Path = DEFAULT_SOURCE) -> dict[str, Font]:
    return parse(path.read_text(encoding="utf-8"))


def _label(code: int) -> str:
    ch = chr(code)
    return "space" if ch == " " else (repr(ch) if 32 < code < 127 else "")


def _comment(code: int) -> str:
    label = _label(code)
    return f"/* 0x{code:02X}{' ' + label if label else ''} */"


def _ident(font: Font) -> str:
    return "BFONT_" + font.name.upper()


def tables_header(fonts: dict[str, Font]) -> str:
    """The generated header the library compiles in: one row table and one presence table per font."""
    out = [
        f"/* Generated by tools/gen_bitmap_font.py from {SOURCE_NAME}. Do not edit. */",
        "#ifndef BFONT_TABLES_H",
        "#define BFONT_TABLES_H",
        "",
    ]
    for font in fonts.values():
        n = _ident(font)
        count = font.last - font.first + 1
        out += [
            f"#define {n}_WIDTH {font.width}",
            f"#define {n}_HEIGHT {font.height}",
            f"#define {n}_ADVANCE {font.advance}",
            f"#define {n}_FIRST {font.first}",
            f"#define {n}_COUNT {count}",
            "",
            f"/* {count} characters from 0x{font.first:02X}, {font.height} row bytes each; the leftmost pixel is bit {font.width - 1}. */",
            f"static const unsigned char {n}_ROWS[{n}_COUNT * {n}_HEIGHT] = {{",
        ]
        for code in range(font.first, font.last + 1):
            rows = [font.row_value(code, r) if code in font.glyphs else 0 for r in range(font.height)]
            out.append("    " + ", ".join(f"0x{v:02X}" for v in rows) + f", {_comment(code)}")
        out += ["};", "", f"static const unsigned char {n}_PRESENT[{n}_COUNT] = {{"]
        flags = [1 if code in font.glyphs else 0 for code in range(font.first, font.last + 1)]
        for i in range(0, len(flags), 16):
            out.append("    " + ", ".join(str(v) for v in flags[i : i + 16]) + ",")
        out += ["};", ""]
    out += ["#endif", ""]
    return "\n".join(out)


def array_header(
    font: Font,
    *,
    name: str,
    type_name: str = "unsigned char",
    first: int | None = None,
    count: int | None = None,
    layout: str = "designated",
    number_format: str = "dec",
    guard: str | None = None,
    include: str | None = None,
    banner: str | None = None,
) -> str:
    """A standalone ``static const`` array, ``name[count][height]``, indexed by ``code - first``.

    ``layout="designated"`` lists only the glyphs the source has, as ``[index] = {...}``; the rest are zero.
    ``"dense"`` lists every index in order. Rows are decimal or hexadecimal (``number_format``). No folding of
    lowercase: the caller does that.
    """
    lo = font.first if first is None else first
    n = (font.last - lo + 1) if count is None else count
    if n < 1 or any(not lo <= c < lo + n for c in font.glyphs):
        raise SourceError(f"font {font.name} has characters outside {lo}..{lo + n - 1}")
    fmt = (lambda v: f"0x{v:02X}") if number_format == "hex" else str
    head = banner or f"{font.name} glyphs, one row per byte, leftmost pixel in bit {font.width - 1}. Generated from {SOURCE_NAME}. Do not edit."
    out = [f"/* {head} */"]
    if guard:
        out += [f"#ifndef {guard}", f"#define {guard}", ""]
    if include:
        out += [f'#include "{include}"', ""]
    out.append(f"static const {type_name} {name}[{n}][{font.height}] = {{")
    sep = "," if number_format == "dec" and layout == "designated" else ", "
    for index in range(n):
        code = lo + index
        if layout == "designated":
            if code not in font.glyphs:
                continue
            rows = sep.join(fmt(font.row_value(code, r)) for r in range(font.height))
            out.append(f" [{index}] = {{{rows}}},  {_comment(code)}")
        elif layout == "dense":
            rows = sep.join(fmt(font.row_value(code, r) if code in font.glyphs else 0) for r in range(font.height))
            out.append(f"    {{ {rows} }},  {_comment(code)}")
        else:
            raise SourceError(f"unknown layout {layout!r}")
    out.append("};")
    if guard:
        out += ["", "#endif"]
    return "\n".join(out) + "\n"


def python_module(font: Font, *, name: str = "FONT_5X7") -> str:
    """A Python module holding the font as ``{character: (row, ...)}`` with rows of ``"0"`` and ``"1"``."""
    out = [
        f'"""Generated by bitmap-font/tools/gen_bitmap_font.py from {SOURCE_NAME}. Do not edit."""',
        "",
        "from __future__ import annotations",
        "",
        "from typing import Mapping",
        "",
        f"# {font.width} wide, {font.height} tall glyphs. \"1\" is an inked pixel.",
        f"{name}: Mapping[str, tuple[str, ...]] = {{",
    ]
    for code in sorted(font.glyphs):
        rows = ", ".join('"' + r.replace(".", "0").replace("#", "1") + '"' for r in font.glyphs[code])
        out.append(f"    {chr(code)!r}: ({rows}),")
    out += ["}", ""]
    return "\n".join(out)


def outputs(fonts: dict[str, Font]) -> list[tuple[Path, str]]:
    """Every generated file kept in the repository, with its text."""
    result = [(TABLES_HEADER, tables_header(fonts))]
    if SHEETS_MODULE.parent.is_dir():
        result.append((SHEETS_MODULE, python_module(fonts["5x7"], name="FONT_5X7")))
    return result


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    sub = ap.add_subparsers(dest="command", required=True)
    t = sub.add_parser("tables")
    t.add_argument("--out", type=Path, default=TABLES_HEADER)
    a = sub.add_parser("array")
    a.add_argument("--font", required=True)
    a.add_argument("--name", required=True)
    a.add_argument("--type", default="unsigned char")
    a.add_argument("--first", type=int)
    a.add_argument("--count", type=int)
    a.add_argument("--layout", choices=("designated", "dense"), default="designated")
    a.add_argument("--format", choices=("dec", "hex"), default="dec")
    a.add_argument("--guard")
    a.add_argument("--include")
    a.add_argument("--banner")
    a.add_argument("--out", type=Path)
    p = sub.add_parser("python")
    p.add_argument("--font", required=True)
    p.add_argument("--name", default="FONT_5X7")
    p.add_argument("--out", type=Path)
    sub.add_parser("check")
    args = ap.parse_args(argv)

    try:
        fonts = load(args.source)
        if args.command == "check":
            stale = [str(p) for p, text in outputs(fonts) if not p.is_file() or p.read_text(encoding="utf-8") != text]
            if stale:
                print("out of date, run the generator: " + ", ".join(stale), file=sys.stderr)
                return 1
            print("generated files are current")
            return 0
        if args.command == "tables":
            text, out = tables_header(fonts), args.out
        elif args.command == "array":
            if args.font not in fonts:
                raise SourceError(f"no font {args.font}; the source has {', '.join(fonts)}")
            text = array_header(
                fonts[args.font], name=args.name, type_name=args.type, first=args.first, count=args.count,
                layout=args.layout, number_format=args.format, guard=args.guard, include=args.include,
                banner=args.banner,
            )
            out = args.out
        else:
            if args.font not in fonts:
                raise SourceError(f"no font {args.font}; the source has {', '.join(fonts)}")
            text, out = python_module(fonts[args.font], name=args.name), args.out
    except (SourceError, OSError) as error:
        print(f"gen_bitmap_font: {error}", file=sys.stderr)
        return 1
    if out is None:
        sys.stdout.write(text)
    else:
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(text, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
