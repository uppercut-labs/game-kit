"""Command line: fix or verify the header checksum of a ROM file."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Callable, Optional, Sequence

from ._checks import RomHeaderError, Verification
from .gba import fix_gba_complement, verify_gba_complement
from .megadrive import fix_megadrive_checksum, verify_megadrive_checksum
from .n64 import fix_n64_checksums, verify_n64_checksums

# For each format: the verifier and the number of hex digits to show per value.
_VERIFIERS: dict[str, tuple[Callable[[bytearray], Verification], int]] = {
    "n64": (verify_n64_checksums, 8),
    "megadrive": (verify_megadrive_checksum, 4),
    "gba": (verify_gba_complement, 2),
}


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="uppercut-rom-headers",
        description="Fix or verify the header checksum of a Nintendo 64, Mega Drive or 32X, or Game Boy Advance ROM.",
    )
    parser.add_argument("action", choices=("fix", "verify"), help="write the checksum into the file, or check it")
    parser.add_argument(
        "format",
        choices=tuple(_VERIFIERS),
        help="n64 (CIC-6102 CRC1 and CRC2), megadrive (also 32X) or gba (header complement)",
    )
    parser.add_argument("rom", help="path to the ROM file")
    parser.add_argument(
        "--preserve-zero-header",
        action="store_true",
        help="n64 fix only: leave an image whose two CRC words are both zero as it is",
    )
    return parser


def _show(values: Sequence[int], digits: int) -> str:
    return ", ".join(f"0x{value:0{digits}X}" for value in values)


def main(argv: Optional[Sequence[str]] = None) -> int:
    """Run the command line. Returns 0 on success, 1 when a verification fails and 2 for a file or usage error."""
    args = build_parser().parse_args(argv)
    path = Path(args.rom)
    verifier, digits = _VERIFIERS[args.format]
    try:
        image = bytearray(path.read_bytes())
    except OSError as error:
        print(f"cannot read {path}: {error.strerror or error}", file=sys.stderr)
        return 2
    try:
        if args.action == "verify":
            result = verifier(image)
            text = f"{args.format} header: expected {_show(result.expected, digits)}, found {_show(result.found, digits)}"
            if result.ok:
                print(f"PASS {text}")
                return 0
            print(f"FAIL {text}", file=sys.stderr)
            return 1
        if args.format == "n64":
            fixed = fix_n64_checksums(image, preserve_zero_header=args.preserve_zero_header)
            if fixed.skipped:
                print(f"left {path} as it is: both n64 CRC words are zero")
                return 0
        elif args.format == "megadrive":
            fix_megadrive_checksum(image)
        else:
            fix_gba_complement(image)
        written = verifier(image).expected
    except RomHeaderError as error:
        print(f"{path}: {error}", file=sys.stderr)
        return 2
    try:
        path.write_bytes(image)
    except OSError as error:
        print(f"cannot write {path}: {error.strerror or error}", file=sys.stderr)
        return 2
    print(f"fixed {args.format} header of {path}: {_show(written, digits)}")
    return 0
