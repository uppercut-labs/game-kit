"""Compare the frame dumps of two runs by MD5."""

from __future__ import annotations

import os
from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Optional, Sequence

from .errors import ProofError
from .ppm import PathLike, md5_of


@dataclass
class FrameRow:
    name: str
    md5_a: str
    md5_b: Optional[str]
    """None when the second run has no such file."""

    @property
    def status(self) -> str:
        if self.md5_b is None:
            return "missing"
        return "same" if self.md5_a == self.md5_b else "different"

    def line(self) -> str:
        if self.md5_b is None:
            return f"{self.name} {self.md5_a} MISSING"
        if self.md5_a == self.md5_b:
            return f"{self.name} {self.md5_a} same"
        return f"{self.name} {self.md5_a} {self.md5_b} DIFFERENT"


@dataclass
class FrameComparison:
    rows: List[FrameRow] = field(default_factory=list)
    only_in_b: List[str] = field(default_factory=list)
    """Frames the second run has and the first does not; they are listed, not counted against either side."""

    @property
    def same(self) -> int:
        return sum(1 for r in self.rows if r.status == "same")

    @property
    def different(self) -> int:
        return sum(1 for r in self.rows if r.status == "different")

    @property
    def missing(self) -> int:
        return sum(1 for r in self.rows if r.status == "missing")

    @property
    def identical(self) -> bool:
        """Every frame of the first run is in the second and the same, and the second has none the first lacks."""
        return not self.different and not self.missing and not self.only_in_b

    def summary(self) -> str:
        return f"{len(self.rows)} frames in A: {self.same} identical, {self.different} different, {self.missing} missing in B"

    def lines(self) -> List[str]:
        return [r.line() for r in self.rows] + [f"{n} only in B" for n in self.only_in_b] + [self.summary()]


def _frames(folder: Path, prefix: str, suffix: str) -> List[str]:
    return sorted(n for n in os.listdir(folder) if n.startswith(prefix) and n.endswith(suffix))


def compare_frame_dirs(a: PathLike, b: PathLike, *, prefix: str = "frame_", suffix: str = ".ppm") -> FrameComparison:
    """MD5 every `<prefix>*<suffix>` file of folder `a` against the same name in folder `b`."""
    a, b = Path(a), Path(b)
    for folder in (a, b):
        if not folder.is_dir():
            raise ProofError(f"{folder} is not a folder")
    result = FrameComparison()
    names_a = _frames(a, prefix, suffix)
    for name in names_a:
        other = b / name
        result.rows.append(FrameRow(name, md5_of(a / name), md5_of(other) if other.is_file() else None))
    known = set(names_a)
    result.only_in_b = [n for n in _frames(b, prefix, suffix) if n not in known]
    return result


def write_report(comparison: FrameComparison, path: PathLike) -> None:
    Path(path).write_text("\n".join(comparison.lines()) + "\n")


__all__: Sequence[str] = ["FrameRow", "FrameComparison", "compare_frame_dirs", "write_report"]
