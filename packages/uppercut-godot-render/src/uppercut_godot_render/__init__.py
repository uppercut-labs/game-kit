"""Render a Godot scene offline with Movie Maker and check the result: trim the frames to the run, check the frame
count, size and blank frames, hash every frame, encode bt709 H.264 and write a JSON manifest. Standard library only;
Godot, FFmpeg and FFprobe are run as programs."""

from .errors import RenderError
from .frames import (
    DEFAULT_BLANK_DEVIATION,
    compare_frame_hashes,
    png_size,
    sha256_file,
    trim_frames,
    verify_frames,
    write_frame_hashes,
)
from .godot import (
    MOVIE_BASE_NAME,
    GodotRun,
    build_command,
    find_godot,
    movie_arguments,
    needs_virtual_display,
    run_godot,
    viewport_override,
)
from .log import (
    ERROR_MARKERS,
    find_marker,
    is_forward_plus,
    renderer_line,
    require_clean_log,
    require_forward_plus,
    run_start_frame,
    shot_start_frame,
)
from .media import BT709_FILTER, FRAME_PATTERN, Media, trim_audio, x264_arguments
from .pipeline import RenderPlan, RenderResult, render

__all__ = [
    "BT709_FILTER",
    "DEFAULT_BLANK_DEVIATION",
    "ERROR_MARKERS",
    "FRAME_PATTERN",
    "MOVIE_BASE_NAME",
    "GodotRun",
    "Media",
    "RenderError",
    "RenderPlan",
    "RenderResult",
    "build_command",
    "compare_frame_hashes",
    "find_godot",
    "find_marker",
    "is_forward_plus",
    "movie_arguments",
    "needs_virtual_display",
    "png_size",
    "render",
    "renderer_line",
    "require_clean_log",
    "require_forward_plus",
    "run_godot",
    "run_start_frame",
    "sha256_file",
    "shot_start_frame",
    "trim_audio",
    "trim_frames",
    "verify_frames",
    "viewport_override",
    "write_frame_hashes",
    "x264_arguments",
]
