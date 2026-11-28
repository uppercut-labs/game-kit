"""Command line: check the C sources of a git work tree and print one line per finding."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path
from typing import Optional, Sequence

from ._check import check_repo
from ._config import CStyleError, find_config, load_config


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="uppercut-c-style",
        description=(
            "Check integer spellings, pointer alignment and simple bounded loops in the C sources of a git work tree."
        ),
    )
    parser.add_argument("pathspecs", nargs="*", metavar="PATHSPEC", help="git pathspecs relative to the root (default: the whole tree)")
    parser.add_argument("--root", type=Path, help="the work tree to check (default: the directory of --config, else the git top level of the current directory)")
    parser.add_argument("--config", type=Path, help="a TOML or JSON settings file, or a pyproject.toml (default: found in the root)")
    return parser


def _git_top_level() -> Path:
    try:
        output = subprocess.run(
            ["git", "rev-parse", "--show-toplevel"], check=True, capture_output=True, text=True
        ).stdout
    except FileNotFoundError as error:
        raise CStyleError("git is not on PATH") from error
    except subprocess.CalledProcessError as error:
        raise CStyleError(error.stderr.strip() or "not inside a git work tree") from error
    return Path(output.strip())


def main(argv: Optional[Sequence[str]] = None) -> int:
    """Run the command line. Returns 0 when the sources pass, 1 for findings or an error."""
    args = build_parser().parse_args(argv)
    try:
        root = args.root or (args.config.resolve().parent if args.config else _git_top_level())
        config = load_config(args.config) if args.config else find_config(root)
        result = check_repo(root, config, args.pathspecs)
    except (CStyleError, OSError, UnicodeDecodeError) as error:
        print(f"uppercut-c-style: {error}", file=sys.stderr)
        return 1
    for finding in result.findings:
        print(finding)
    if not result.checked:
        label = f"{config.label} " if config.label else ""
        print(f"No {label}C sources found", file=sys.stderr)
        return 1
    if result.ok:
        print(f"Checked {result.checked} C sources and headers")
    return 0 if result.ok else 1
