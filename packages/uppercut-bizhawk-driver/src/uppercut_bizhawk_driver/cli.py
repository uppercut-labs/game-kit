"""`uppercut-bizhawk-driver`: install a pinned BizHawk, edit a shared config safely, show what a Lua config looks like."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import List, Optional, Sequence

from . import __version__
from .config import edit_config_in_place, parse_assignment
from .errors import BizhawkError
from .install import install_bizhawk, validate_install
from .lua import lua_chunk


def _install(args: argparse.Namespace) -> int:
    where = install_bizhawk(args.destination, args.url, args.sha256, replace=args.replace)
    launcher, lua_root = validate_install(where)
    print(f"Installed in {where}: {launcher.name}, Lua resources at {lua_root}")
    return 0


def _edit_config(args: argparse.Namespace) -> int:
    edits = [parse_assignment(text) for text in args.set]
    config = edit_config_in_place(args.config, edits, create=args.create, keep_copy=args.copy_to)
    for path, _ in edits:
        node = config
        for step in path:
            node = node[step]
        print("/".join(path), "=", json.dumps(node))
    return 0


def _lua(args: argparse.Namespace) -> int:
    sys.stdout.write(lua_chunk(json.loads(Path(args.json).read_text(encoding="utf-8"))))
    return 0


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="uppercut-bizhawk-driver", description=__doc__)
    p.add_argument("--version", action="version", version=f"uppercut-bizhawk-driver {__version__}")
    sub = p.add_subparsers(dest="command", required=True)
    s = sub.add_parser("install", help="download a BizHawk release zip, check its SHA-256 and unpack it")
    s.add_argument("destination")
    s.add_argument("--url", required=True)
    s.add_argument("--sha256", required=True, help="the 64-digit pin of the archive")
    s.add_argument("--replace", action="store_true", help="replace an existing folder (after the new copy has been checked)")
    s.set_defaults(run=_install)
    s = sub.add_parser("edit-config", help="edit a shared config.ini; refused while EmuHawk runs")
    s.add_argument("config")
    s.add_argument("--set", action="append", default=[], metavar="PATH=VALUE", help="steps separated by /, value as JSON or text (repeatable)")
    s.add_argument("--create", action="store_true", help="make missing steps")
    s.add_argument("--copy-to", help="also save the edited config here")
    s.set_defaults(run=_edit_config)
    s = sub.add_parser("lua", help="print a JSON file as the Lua file a script would read")
    s.add_argument("json")
    s.set_defaults(run=_lua)
    return p


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return int(args.run(args))
    except (BizhawkError, OSError, ValueError) as error:
        print(f"uppercut-bizhawk-driver {args.command}: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
