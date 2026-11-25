"""`uppercut-ps2-match`: the tools as commands."""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence

from . import __version__, csplit
from .audit import AuditConfig, audit_tree
from .config import load_verify_config
from .errors import MatchError
from .fetch import default_host, fetch_toolchain, load_packages


def _verify(args: argparse.Namespace) -> int:
    cfg = load_verify_config(args.config)
    prefer = tuple(int(x, 16) for x in args.prefer_range.split(":")) if args.prefer_range else None
    allr: List[Dict[str, Any]] = []
    for src in args.files:
        t = time.time()
        res = cfg.verifier.check(Path(src).resolve(), Path(args.work).resolve() / Path(src).stem, cfg.flags,
                                 assembler=args.assembler or cfg.assembler, prefer=prefer)  # type: ignore[arg-type]
        for r in res:
            r.setdefault("file", str(src))
            r["seconds"] = round(time.time() - t, 1)
            allr.append(r)
            if not args.quiet or r["status"] != "match":
                print(json.dumps({k: v for k, v in r.items() if k not in ("file", "log")}))
                if "log" in r and not args.quiet:
                    print(r["log"])
    if args.json:
        with open(args.json, "a", encoding="utf-8") as fh:
            for r in allr:
                r.pop("log", None)
                fh.write(json.dumps(r) + "\n")
    matched = [r for r in allr if r["status"] == "match"]
    print(f"{len(matched)}/{len(allr)} match, {sum(r.get('size', 0) for r in matched)} bytes")
    return 0 if len(matched) == len(allr) else 1


def _csplit(args: argparse.Namespace) -> int:
    prelude, funcs = csplit.split(Path(args.file).read_text(encoding="utf-8"))
    print(f"{len(prelude)} prelude chunks; functions: {[n for n, _ in funcs]}")
    return 0


def _fetch(args: argparse.Namespace) -> int:
    packages = load_packages(args.config, args.host or default_host())
    fetch_toolchain(packages, args.dest, dry_run=args.dry_run)
    return 0


def _audit(args: argparse.Namespace) -> int:
    cfg = AuditConfig()
    if args.config:
        data = json.loads(Path(args.config).read_text(encoding="utf-8"))
        cfg = AuditConfig(
            known={k: (v[0], v[1]) for k, v in data.get("known", {}).items()},
            publish=data.get("publish"), publish_exclude=data.get("publish_exclude", ()),
            screenshot_dir=data.get("screenshot_dir", "screenshots"), screenshot_max=data.get("screenshot_max", 2 * 1024 * 1024),
            max_literals=data.get("max_literals", 1024), table_extension=data.get("table_extension"),
            table_row=re.compile(data["table_row"]) if data.get("table_row") else None,
        )
    roots = [(args.root, "")]
    for extra in args.extra:
        folder, _, prefix = extra.partition("=")
        roots.append((folder, prefix.strip("./") and prefix.rstrip("/") + "/"))
    report = audit_tree(roots, cfg, publish_first=args.publish)
    text = report.markdown()
    if args.out:
        Path(args.out).write_text(text, encoding="utf-8", newline="\n")
    print(text)
    return report.exit_status


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="uppercut-ps2-match", description=__doc__)
    p.add_argument("--version", action="version", version=f"uppercut-ps2-match {__version__}")
    sub = p.add_subparsers(dest="command", required=True)
    s = sub.add_parser("verify", help="compile C files and byte-check each function against the original image")
    s.add_argument("config", help="the verify config (JSON)")
    s.add_argument("files", nargs="+")
    s.add_argument("--work", required=True, help="folder for each file's compile (a folder per file)")
    s.add_argument("--assembler", help="an assembler command in place of the config's")
    s.add_argument("--prefer-range", help="lo:hi (hex): a repeated function name resolves to its address in this range")
    s.add_argument("--json", help="append the results as JSON lines")
    s.add_argument("--quiet", action="store_true", help="print only what does not match")
    s.set_defaults(run=_verify)
    s = sub.add_parser("csplit", help="list the top-level functions of a C file")
    s.add_argument("file")
    s.set_defaults(run=_csplit)
    s = sub.add_parser("fetch", help="fetch a toolchain described in a JSON file, checking every SHA-256")
    s.add_argument("config")
    s.add_argument("--dest", required=True)
    s.add_argument("--host", help="which host-specific build to take (default: this machine's)")
    s.add_argument("--dry-run", action="store_true")
    s.set_defaults(run=_fetch)
    s = sub.add_parser("audit", help="audit a tree for game data, disc images and disassembly; exit 1 on any finding")
    s.add_argument("--root", default=".")
    s.add_argument("--config", help="JSON: known, publish, publish_exclude, screenshot_dir, screenshot_max, max_literals, table_extension, table_row")
    s.add_argument("--publish", action="store_true", help="limit the root to the config's publish set")
    s.add_argument("--extra", action="append", default=[], metavar="DIR=PREFIX", help="also audit DIR, reported under PREFIX")
    s.add_argument("--out", help="write the Markdown report here too")
    s.set_defaults(run=_audit)
    return p


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return int(args.run(args))
    except (MatchError, OSError, ValueError) as error:
        print(f"uppercut-ps2-match {args.command}: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
