"""Command line: fetch the sources of a manifest into a cache directory."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Optional, Sequence

from ._sources import (
    DEFAULT_TIMEOUT,
    DEFAULT_USER_AGENT,
    DownloadError,
    PinnedSourceError,
    describe,
    fetch_manifest,
)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="uppercut-pinned-sources",
        description=(
            "Fetch the build inputs listed in a manifest (SHA256 NAME LOCATION per line) into a cache directory. "
            "A cached file is reused when its SHA-256 matches; any mismatch is refused."
        ),
    )
    parser.add_argument("manifest", type=Path, help="the manifest file")
    parser.add_argument("cache", type=Path, help="the cache directory, created if missing")
    parser.add_argument("--base-url", metavar="URL", help="the URL that relative locations are joined to")
    parser.add_argument("--user-agent", default=DEFAULT_USER_AGENT, help=f"the User-Agent of downloads (default {DEFAULT_USER_AGENT})")
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT, help=f"seconds per download (default {DEFAULT_TIMEOUT:g})")
    parser.add_argument("--offline", action="store_true", help="never download: a missing file is an error")
    parser.add_argument("--quiet", action="store_true", help="print nothing on success")
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    """Run the command line. Returns 0 on success, 1 for a mismatch, a bad manifest or a file missing offline, and
    2 for a failed download or a file that cannot be read or written."""
    args = build_parser().parse_args(argv)
    report = None if args.quiet else (lambda result: print(describe(result)))
    try:
        fetch_manifest(
            args.manifest,
            args.cache,
            base_url=args.base_url,
            user_agent=args.user_agent,
            timeout=args.timeout,
            offline=args.offline,
            report=report,
        )
    except PinnedSourceError as error:
        print(f"uppercut-pinned-sources: {error}", file=sys.stderr)
        return 1
    except (DownloadError, OSError) as error:
        detail = f"{error.filename}: {error.strerror}" if getattr(error, "filename", None) else str(error)
        print(f"uppercut-pinned-sources: {detail}", file=sys.stderr)
        return 2
    return 0
