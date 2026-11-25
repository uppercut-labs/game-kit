"""Command line: normalize a PlayStation 2 disc image in place."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Optional, Sequence

from ._normalize import IsoNormalizeError, normalize_disc, ps2_system_cnf


def normalize_file(
    iso: "Path | str",
    elf: "Path | str",
    *,
    elf_name: str,
    system_cnf: Optional[Sequence[bytes]] = None,
) -> None:
    """Normalize the disc image file at ``iso`` in place, against the ELF file at ``elf``.

    Raises ``IsoNormalizeError`` for a disc that does not match, leaving the file as it was, and ``OSError`` when a
    file cannot be read or written.
    """
    iso_path = Path(iso)
    image = bytearray(iso_path.read_bytes())
    normalize_disc(image, Path(elf).read_bytes(), elf_name=elf_name, system_cnf=system_cnf)
    iso_path.write_bytes(image)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="uppercut-iso-normalize",
        description=(
            "Verify a PlayStation 2 data disc (SYSTEM.CNF and one ELF) and rewrite its dates and file names in place, "
            "so the same inputs always make the same bytes."
        ),
    )
    parser.add_argument("iso", type=Path, help="the disc image, rewritten in place")
    parser.add_argument("elf", type=Path, help="the ELF the disc must contain")
    parser.add_argument("--elf-name", required=True, metavar="NAME", help="the ELF's name on the disc, for example GAME.ELF")
    parser.add_argument("--ver", default="1.00", help="the VER line of SYSTEM.CNF (default 1.00)")
    parser.add_argument("--vmode", default="NTSC", help="the VMODE line of SYSTEM.CNF (default NTSC)")
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    """Run the command line. Returns 0 on success, 1 when the disc does not match and 2 for a file error."""
    args = build_parser().parse_args(argv)
    try:
        normalize_file(
            args.iso,
            args.elf,
            elf_name=args.elf_name,
            system_cnf=ps2_system_cnf(args.elf_name, ver=args.ver, vmode=args.vmode),
        )
    except IsoNormalizeError as error:
        print(f"{args.iso}: {error}", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"{error.filename or args.iso}: {error.strerror or error}", file=sys.stderr)
        return 2
    return 0
