"""Command line: normalize a PlayStation 2 disc image in place, or build one from an ELF."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Optional, Sequence

from ._build import build_file
from ._normalize import IsoNormalizeError, normalize_disc, ps2_system_cnf


def normalize_file(
    iso: "Path | str",
    elf: "Path | str",
    *,
    elf_name: str,
    system_cnf: Optional[Sequence[bytes]] = None,
    versioned: bool = False,
) -> None:
    """Normalize the disc image file at ``iso`` in place, against the ELF file at ``elf``.

    Raises ``IsoNormalizeError`` for a disc that does not match, leaving the file as it was, and ``OSError`` when a
    file cannot be read or written.
    """
    iso_path = Path(iso)
    image = bytearray(iso_path.read_bytes())
    normalize_disc(image, Path(elf).read_bytes(), elf_name=elf_name, system_cnf=system_cnf, versioned=versioned)
    iso_path.write_bytes(image)


def _system_cnf_options(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--elf-name", required=True, metavar="NAME", help="the ELF's name on the disc, for example GAME.ELF")
    parser.add_argument("--ver", default="1.00", help="the VER line of SYSTEM.CNF (default 1.00)")
    parser.add_argument("--vmode", default="NTSC", help="the VMODE line of SYSTEM.CNF (default NTSC)")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="uppercut-iso-normalize",
        description=(
            "Verify a PlayStation 2 data disc (SYSTEM.CNF and one ELF) and rewrite its dates and file names in place, "
            "so the same inputs always make the same bytes. To build a new disc from an ELF, run "
            "'uppercut-iso-normalize build --help'."
        ),
    )
    parser.add_argument("iso", type=Path, help="the disc image, rewritten in place")
    parser.add_argument("elf", type=Path, help="the ELF the disc must contain")
    _system_cnf_options(parser)
    parser.add_argument(
        "--versioned", action="store_true", help="the names already end in ;1 (a pycdlib image); keep them"
    )
    return parser


def build_command_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="uppercut-iso-normalize build",
        description=(
            "Build a PlayStation 2 data disc that boots ELF: SYSTEM.CNF and the ELF in the root of an ISO 9660 "
            "image, read back and checked, with fixed dates so the same ELF always makes the same bytes."
        ),
    )
    parser.add_argument("elf", type=Path, help="the ELF to boot")
    parser.add_argument("iso", type=Path, help="the disc image to write (replaced only when complete)")
    _system_cnf_options(parser)
    parser.add_argument("--volume-id", metavar="ID", help="the volume identifier (default: the ELF name without its extension)")
    return parser


def _run(action, path: Path) -> int:
    try:
        action()
    except IsoNormalizeError as error:
        print(f"{path}: {error}", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"{error.filename or path}: {error.strerror or error}", file=sys.stderr)
        return 2
    return 0


def build_main(argv: Optional[Sequence[str]] = None) -> int:
    """Run ``uppercut-iso-normalize build``. Returns 0 on success, 1 when the disc cannot be built as asked and 2
    for a file error."""
    args = build_command_parser().parse_args(argv)
    lines = ps2_system_cnf(args.elf_name, ver=args.ver, vmode=args.vmode)
    return _run(
        lambda: build_file(args.elf, args.iso, elf_name=args.elf_name, volume_id=args.volume_id, system_cnf=lines),
        args.iso,
    )


def main(argv: Optional[Sequence[str]] = None) -> int:
    """Run the command line. Returns 0 on success, 1 when the disc does not match and 2 for a file error.

    ``build`` as the first argument runs ``build_main`` with the rest.
    """
    arguments = list(sys.argv[1:] if argv is None else argv)
    if arguments[:1] == ["build"]:
        return build_main(arguments[1:])
    args = build_parser().parse_args(arguments)
    lines = ps2_system_cnf(args.elf_name, ver=args.ver, vmode=args.vmode)
    return _run(
        lambda: normalize_file(args.iso, args.elf, elf_name=args.elf_name, system_cnf=lines, versioned=args.versioned),
        args.iso,
    )
