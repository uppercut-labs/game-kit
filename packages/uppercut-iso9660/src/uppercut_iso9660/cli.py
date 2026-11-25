"""``uppercut-iso9660``: list, extract and identify a disc image from the command line."""

from __future__ import annotations

import argparse
import json
import sys
from typing import Optional, Sequence

from .errors import IsoError
from .image import IsoImage
from .ps2 import ps2_summary


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="uppercut-iso9660", description="Read an ISO 9660 disc image (never writes to it).")
    sub = parser.add_subparsers(dest="command", required=True)
    ls = sub.add_parser("ls", help="list files (and directories) with their size and LBA")
    ls.add_argument("image")
    ls.add_argument("path", nargs="?", default="", help="a directory to list (default: the whole disc)")
    get = sub.add_parser("get", help="extract one file and print its size and SHA-256")
    get.add_argument("image")
    get.add_argument("path")
    get.add_argument("output")
    boot = sub.add_parser("ps2", help="print the PlayStation 2 boot serial, ELF name, size and SHA-256 as JSON")
    boot.add_argument("image")
    for command in (ls, get, boot):
        command.add_argument("--lenient", action="store_true", help="accept records whose two byte-order copies disagree")
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        with IsoImage.open(args.image, strict=not args.lenient) as image:
            if args.command == "ls":
                start = image.find(args.path) if args.path else None
                for entry in image.walk(start):
                    print(f"{'D' if entry.is_dir else 'F'} {entry.size:>11} {entry.lba:>8} {entry.path}")
            elif args.command == "get":
                result = image.extract(args.path, args.output)
                print(result.size, result.sha256)
            else:
                print(json.dumps(ps2_summary(image), indent=2))
    except (IsoError, OSError) as error:
        print(f"{args.image}: {error}", file=sys.stderr)
        return 1
    return 0
