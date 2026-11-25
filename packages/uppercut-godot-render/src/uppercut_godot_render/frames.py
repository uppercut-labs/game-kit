"""The frames Movie Maker wrote: trim them to the run, check them, and hash every one."""

from __future__ import annotations

import hashlib
from pathlib import Path

from .errors import RenderError
from .godot import MOVIE_BASE_NAME
from .media import FRAME_PATTERN, Media

DEFAULT_BLANK_DEVIATION = 2.0
"""A frame counts as blank when its 64x36 greyscale thumbnail has a deviation below this. A busy scene measured about
30; a solid frame measures 0."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def png_size(path: Path) -> tuple[int, int]:
    """Width and height from a PNG's IHDR chunk."""
    header = Path(path).read_bytes()[:24]
    if header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        raise RenderError(f"{path} is not a PNG")
    return int.from_bytes(header[16:20], "big"), int.from_bytes(header[20:24], "big")


def trim_frames(raw: Path, frames: Path, first: int, count: int) -> list[Path]:
    """Move ``count`` of Movie Maker's frames, from the ``first``-th on, from ``raw/`` to ``frames/frame00000000.png``
    onward. The ones before and after (preroll, the tail) stay in ``raw``."""
    written = sorted(Path(raw).glob(f"{MOVIE_BASE_NAME}*.png"))
    if first < 0 or count <= 0:
        raise RenderError(f"cannot trim {count} frames from frame {first}")
    if len(written) < first + count:
        raise RenderError(f"Movie Maker wrote {len(written)} frames; the run needs frames {first} to {first + count - 1}")
    frames = Path(frames)
    frames.mkdir(parents=True, exist_ok=True)
    moved = []
    for index, source in enumerate(written[first : first + count]):
        target = frames / (FRAME_PATTERN % index)
        source.replace(target)
        moved.append(target)
    return moved


def verify_frames(
    frames: Path,
    expected: int,
    size: tuple[int, int],
    media: Media,
    *,
    min_deviation: float = DEFAULT_BLANK_DEVIATION,
) -> list[Path]:
    """The folder holds exactly ``expected`` frames; the first and the last are ``size`` and not blank."""
    found = sorted(Path(frames).glob("frame*.png"))
    if len(found) != expected:
        raise RenderError(f"expected {expected} frames, found {len(found)}")
    if expected == 0:
        raise RenderError("the run has no frames")
    for label, frame in (("first", found[0]), ("last", found[-1])):
        actual = png_size(frame)
        if actual != size:
            raise RenderError(f"{label} frame is {actual[0]}x{actual[1]}, expected {size[0]}x{size[1]}")
        deviation = media.frame_deviation(frame)
        if deviation < min_deviation:
            raise RenderError(f"{label} frame is blank (deviation {deviation:.2f})")
    return found


def write_frame_hashes(frames: Path, destination: Path, *, prefix: str = "") -> list[tuple[str, str]]:
    """Write ``<sha256>  <prefix><name>`` for every frame, in name order, and return ``(sha256, name)`` pairs."""
    pairs = [(sha256_file(frame), frame.name) for frame in sorted(Path(frames).glob("frame*.png"))]
    Path(destination).write_text("".join(f"{digest}  {prefix}{name}\n" for digest, name in pairs), encoding="utf-8")
    return pairs


def compare_frame_hashes(first: Path, second: Path) -> tuple[int, list[str]]:
    """Compare two hash files (a determinism check): the number of lines and the names that differ. A different
    number of frames is an error."""
    a = Path(first).read_text(encoding="utf-8").splitlines()
    b = Path(second).read_text(encoding="utf-8").splitlines()
    if len(a) != len(b):
        raise RenderError(f"{first} lists {len(a)} frames and {second} lists {len(b)}")
    return len(a), [x.split()[1] for x, y in zip(a, b) if x != y]


