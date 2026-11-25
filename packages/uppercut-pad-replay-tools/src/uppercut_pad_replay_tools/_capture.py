"""The capture script some emulator hooks read: `wait N`, `shot name`, `quit`, one per line."""

from __future__ import annotations

import re
from pathlib import Path
from typing import Iterable

_NAME = re.compile(r"[A-Za-z0-9._-]+")


class CaptureError(ValueError):
    """A capture script that could not be written as asked."""


class CaptureScript:
    """A capture script under construction. Methods return the script, so calls chain.

    `wait N` waits N frames, `shot name` saves a screenshot called name, `quit` ends the run. The script does not know
    what a frame is: it is the host's (the game's frame counter, an emulator tick). Anything else a particular hook
    reads can go in through `raw`.
    """

    def __init__(self) -> None:
        self._lines: list[str] = []

    def wait(self, frames: int) -> "CaptureScript":
        if isinstance(frames, bool) or not isinstance(frames, int) or frames < 0:
            raise CaptureError(f"wait takes a whole number of frames, 0 or more, got {frames!r}")
        self._lines.append(f"wait {frames}")
        return self

    def shot(self, name: str) -> "CaptureScript":
        if not _NAME.fullmatch(name):
            raise CaptureError(f"a shot name is letters, digits, '.', '_' and '-' only, got {name!r}")
        self._lines.append(f"shot {name}")
        return self

    def quit(self) -> "CaptureScript":
        self._lines.append("quit")
        return self

    def raw(self, line: str) -> "CaptureScript":
        """One more line as given, for a directive a hook has that this class does not."""
        if "\n" in line or "\r" in line or not line.strip():
            raise CaptureError("a raw line is one non-empty line")
        self._lines.append(line)
        return self

    def shots_at(self, shots: Iterable[tuple[int, str]], tail: int = 45, start: int = 0) -> "CaptureScript":
        """Shots at absolute frames: for each (frame, name) in frame order a `wait` of the frames since the last one,
        then the `shot`; then `wait tail` and `quit`. `start` is the frame the script begins at (0 when the run does).
        Frames must not go down."""
        previous = start
        for frame, name in sorted(shots, key=lambda pair: pair[0]):
            if frame < previous:
                raise CaptureError(f"shot {name!r} at frame {frame} is before the script's start at {previous}")
            self.wait(frame - previous).shot(name)
            previous = frame
        return self.wait(tail).quit()

    @property
    def lines(self) -> list[str]:
        return list(self._lines)

    def text(self) -> str:
        return "\n".join(self._lines) + "\n"

    def write(self, path: str | Path) -> Path:
        if not self._lines:
            raise CaptureError("a capture script needs at least one line")
        target = Path(path)
        target.write_text(self.text(), encoding="ascii", newline="\n")
        return target
