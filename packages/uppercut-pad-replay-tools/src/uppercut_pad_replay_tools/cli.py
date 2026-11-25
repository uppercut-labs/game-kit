"""Command line: check a replay file the way the game port's reader will."""

from __future__ import annotations

import argparse
import sys
from typing import Optional, Sequence

from ._replay import DEFAULT_MAX_EVENTS, DEFAULT_SLOTS, MAX_SLOTS, ReplayError, read_replay


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="uppercut-pad-replay-tools",
        description="Check a controller replay file as strictly as the pad-replay reader does.",
    )
    parser.add_argument("replay", nargs="+", help="path to a replay file (frame slot buttons_hex lx ly connected)")
    parser.add_argument("--slots", type=int, default=DEFAULT_SLOTS, help=f"slot numbers allowed, up to {MAX_SLOTS}")
    parser.add_argument(
        "--max-events", type=int, default=DEFAULT_MAX_EVENTS, help="most events a file may hold (the reader's cap)"
    )
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    """Print `PASS path: N events, last frame F` or `FAIL path: why` for each file. Exit 0 when all pass, 1 when a file
    fails, 2 for a usage error."""
    parser = build_parser()
    args = parser.parse_args(argv)
    if not 1 <= args.slots <= MAX_SLOTS or args.max_events < 1:
        parser.error(f"--slots is 1 to {MAX_SLOTS} and --max-events at least 1")
    failed = False
    for path in args.replay:
        try:
            events = read_replay(path, slots=args.slots, max_events=args.max_events)
        except (OSError, ReplayError) as error:
            print(f"FAIL {path}: {error}", file=sys.stderr)
            failed = True
        else:
            print(f"PASS {path}: {len(events)} events, last frame {events[-1].frame}")
    return 1 if failed else 0
