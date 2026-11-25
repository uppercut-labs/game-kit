"""`uppercut-port-proof`: the helpers as commands. Every command exits 0 on success and 2 on a file or argument it cannot use."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import List, Optional, Sequence

from . import __version__
from .debug import DebugClient, run_presses
from .errors import ProofError
from .frames import compare_frame_dirs, write_report
from .png import ppm_to_png
from .ppm import crop, enlarge, md5_of, read_ppm
from .png import picture_to_png
from .presses import read_presses, to_script
from .wav import DEFAULT_SILENCE_DB, format_stats, stats_for_file


def _wav_stats(args: argparse.Namespace) -> int:
    results = [stats_for_file(p, silence_db=args.silence_db) for p in args.wav]
    if args.json:
        print(json.dumps(results[0] if len(results) == 1 else results, indent=1))
    else:
        for r in results:
            print(format_stats(r))
    return 0


def _md5_compare(args: argparse.Namespace) -> int:
    result = compare_frame_dirs(args.a, args.b)
    print(result.summary())
    if args.out:
        write_report(result, args.out)
    return 0 if result.identical or not args.strict else 1


def _ppm2png(args: argparse.Namespace) -> int:
    out = Path(args.out_dir)
    out.mkdir(parents=True, exist_ok=True)
    for f in args.frames:
        picture = ppm_to_png(f, out / (Path(f).stem + ".png"), compress=not args.stored)
        print(md5_of(f), Path(f).stem, picture.width, picture.height)
    return 0


def _ppm_crop(args: argparse.Namespace) -> int:
    picture = enlarge(crop(read_ppm(args.src), args.x0, args.y0, args.x1, args.y1), args.scale)
    Path(args.out).write_bytes(picture_to_png(picture, compress=not args.stored))
    return 0


def _presses2script(args: argparse.Namespace) -> int:
    presses = read_presses(args.presses, default_hold=6)
    lines = to_script(
        presses, args.total, shot_every=args.shot_every, shot_ranges=[tuple(r) for r in args.shot_range], prefix=args.prefix, turbo=args.turbo
    )
    Path(args.out).write_text("\n".join(lines) + "\n")
    shots = sum(1 for line in lines if line.startswith("shot "))
    print("wrote %s: %d lines, %d shots" % (args.out, len(lines), shots))
    return 0


def _drive(args: argparse.Namespace) -> int:
    presses = read_presses(args.presses, default_hold=4)
    client = DebugClient(args.host, args.port)
    run_presses(client, presses, args.until, shot_every=args.shot_every, out_dir=Path(args.out), quit_at_end=args.quit)
    return 0


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="uppercut-port-proof", description=__doc__)
    p.add_argument("--version", action="version", version=f"uppercut-port-proof {__version__}")
    sub = p.add_subparsers(dest="command", required=True)

    s = sub.add_parser("wav-stats", help="duration, peak, RMS, full-scale samples and silent stretches of 16-bit PCM WAV files")
    s.add_argument("wav", nargs="+")
    s.add_argument("--silence-db", type=float, default=DEFAULT_SILENCE_DB, help="a one-second window is silent under this peak (default %(default)s dBFS)")
    s.add_argument("--json", action="store_true", help="print JSON (a list when several files are given)")
    s.set_defaults(run=_wav_stats)

    s = sub.add_parser("md5-compare", help="compare the frame_*.ppm dumps of two run folders by MD5")
    s.add_argument("a")
    s.add_argument("b")
    s.add_argument("out", nargs="?", help="write one line per frame here")
    s.add_argument("--strict", action="store_true", help="exit 1 unless every frame is the same")
    s.set_defaults(run=_md5_compare)

    s = sub.add_parser("ppm2png", help="convert PPM frames to PNG files in a folder and print each frame's MD5")
    s.add_argument("out_dir")
    s.add_argument("frames", nargs="+")
    s.add_argument("--stored", action="store_true", help="write stored (uncompressed) blocks, as the C package minimal-png does")
    s.set_defaults(run=_ppm2png)

    s = sub.add_parser("ppm-crop", help="crop a region of a PPM frame, enlarge it by a whole number and write a PNG")
    s.add_argument("src")
    s.add_argument("out")
    for name in ("x0", "y0", "x1", "y1"):
        s.add_argument(name, type=int)
    s.add_argument("scale", nargs="?", type=int, default=2)
    s.add_argument("--stored", action="store_true")
    s.set_defaults(run=_ppm_crop)

    s = sub.add_parser("presses2script", help="turn a press file into a script for the PS2 emulator build, with screenshots at chosen frames")
    s.add_argument("presses")
    s.add_argument("total", type=int)
    s.add_argument("out")
    s.add_argument("--shot-every", type=int, default=0)
    s.add_argument("--shot-range", type=int, nargs=2, action="append", default=[], metavar=("A", "B"))
    s.add_argument("--prefix", default="f")
    s.add_argument("--turbo", action="store_true")
    s.set_defaults(run=_presses2script)

    s = sub.add_parser("drive", help="press buttons at frame numbers and take screenshots over a port's debug socket")
    s.add_argument("--presses", required=True)
    s.add_argument("--until", type=int, required=True, help="stop after this frame")
    s.add_argument("--shot-every", type=int, default=300)
    s.add_argument("--out", required=True)
    s.add_argument("--port", type=int, default=4370)
    s.add_argument("--host", default="127.0.0.1")
    s.add_argument("--quit", action="store_true", help="send quit to the program at the end")
    s.set_defaults(run=_drive)
    return p


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return int(args.run(args))
    except (ProofError, OSError) as error:
        print(f"uppercut-port-proof {args.command}: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
