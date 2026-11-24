"""What a Godot log proves. A run is judged by its log, never by its exit code alone: Godot can exit 0 after a script
fails to load, and it falls back from Vulkan to OpenGL when no Vulkan driver exists."""

from __future__ import annotations

import re
from typing import Optional, Sequence

from .errors import RenderError

# D3D12 reports its API version as "12_0", Vulkan as "1.3.xyz", Metal as "3.0".
FORWARD_PLUS_RENDERER = re.compile(r"^(Vulkan|Metal|D3D12) [0-9._]+ - Forward\+ - Using Device.*$", re.MULTILINE)
ANY_RENDERER = re.compile(r"^(Vulkan|Metal|D3D12|OpenGL API) .* - Using Device.*$", re.MULTILINE)
RUN_START_FRAME = re.compile(r"^RUN_START_FRAME (\d+)$", re.MULTILINE)
SHOT_FRAME = re.compile(r"^SHOT_FRAME (-?\d+) COUNTER (\d+)", re.MULTILINE)
ERROR_MARKERS = ("SCRIPT ERROR", "SHADER ERROR", "Parse Error")


def renderer_line(log: str) -> Optional[str]:
    """The renderer's ``Using Device`` line, or ``None``."""
    match = ANY_RENDERER.search(log)
    return match.group(0).strip() if match else None


def is_forward_plus(log: str) -> bool:
    return FORWARD_PLUS_RENDERER.search(log) is not None


def require_forward_plus(log: str) -> str:
    """Return the renderer line, or raise unless it is Forward+ on Vulkan, Metal or D3D12."""
    if not is_forward_plus(log):
        raise RenderError(f"renderer is not Forward+ on Vulkan, Metal or D3D12: {renderer_line(log) or 'no renderer line'}")
    return renderer_line(log) or ""


def require_clean_log(log: str, markers: Sequence[str] = ERROR_MARKERS) -> None:
    """Raise when the log holds any of the error markers (script, shader and parse errors by default)."""
    for marker in markers:
        if marker in log:
            line = next(line for line in log.splitlines() if marker in line)
            raise RenderError(f"the Godot log reports {marker}: {line.strip()}")


def run_start_frame(log: str) -> int:
    """The Movie Maker frame index of the run's first tick, from the one ``RUN_START_FRAME <n>`` line the game logs.

    The game logs the process frame of its tick 0; Movie Maker's frame index is the same number, and footage frame
    ``i`` is Movie Maker frame ``n + i``.
    """
    matches = RUN_START_FRAME.findall(log)
    if len(matches) != 1:
        raise RenderError(f"expected one RUN_START_FRAME line in the log, found {len(matches)}")
    return int(matches[0])


def shot_start_frame(log: str, expected_first_shot: Optional[int] = None) -> int:
    """The Movie Maker frame index of a shot's first frame, from ``SHOT_FRAME <f> COUNTER <k>`` lines.

    The first such line gives ``k``, the index of Movie Maker's frame that shows shot frame ``f``. With
    ``expected_first_shot`` the first line's ``f`` must equal it.
    """
    marks = SHOT_FRAME.findall(log)
    if not marks:
        raise RenderError("no SHOT_FRAME line in the log")
    first_shot, counter = int(marks[0][0]), int(marks[0][1])
    if expected_first_shot is not None and first_shot != expected_first_shot:
        raise RenderError(f"first shot frame {first_shot}, expected {expected_first_shot}")
    return counter


def find_marker(log: str, pattern: str, *, count: Optional[int] = 1) -> list[tuple[str, ...]]:
    """Every match of a regular expression with groups, one tuple per ``MULTILINE`` match. With ``count`` (default 1)
    the number of matches must equal it; ``count=None`` accepts any number. For a log line of your own."""
    found = [m.groups() if m.groups() else (m.group(0),) for m in re.finditer(pattern, log, re.MULTILINE)]
    if count is not None and len(found) != count:
        raise RenderError(f"expected {count} log line(s) matching {pattern!r}, found {len(found)}")
    return found
