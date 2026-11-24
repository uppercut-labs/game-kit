"""Render, check and encode one clip: the steps every Godot Movie Maker project repeats, in order."""

from __future__ import annotations

import json
import shutil
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Mapping, Optional, Sequence

from . import godot as _godot
from . import log
from .errors import RenderError
from .frames import DEFAULT_BLANK_DEVIATION, trim_frames, verify_frames, write_frame_hashes, sha256_file
from .godot import GodotRun
from .media import Media, trim_audio


@dataclass(frozen=True)
class RenderPlan:
    """One clip. Everything about the game is in here; the package knows no game."""

    godot: str
    """The Godot binary."""
    project: Path
    """The Godot project folder (``--path``)."""
    output: Path
    """The work folder. It must be empty or absent; ``raw/``, ``frames/`` and the outputs go here."""
    fps: int
    size: tuple[int, int]
    frame_count: int
    """How many footage frames the run has."""
    start_frame: Callable[[str], int]
    """Reads the Godot log and returns the Movie Maker frame index of footage frame 0, for example
    ``uppercut_godot_render.run_start_frame`` or ``lambda text: shot_start_frame(text, 0)``."""
    game_arguments: Sequence[str] = ()
    """The game's own flags, after ``--``."""
    godot_arguments: Sequence[str] = ()
    """Extra Godot flags before ``--`` (``--quit-after``, ``--resolution``, ``--position``, a forced renderer)."""
    always_on_top: bool = False
    quit_after: Optional[int] = None
    override_path: Optional[Path] = None
    """The project's ``override.cfg`` (default ``<project>/override.cfg``). The render refuses to start while it exists:
    another render is running, or a killed one left it behind."""
    write_override: bool = False
    """Write ``override.cfg`` with ``size`` as the viewport for the render's duration. Movie Maker records at the
    project's viewport size, not the window's, so a clip that is not the project's own size needs this."""
    stretch_viewport: bool = True
    min_deviation: float = DEFAULT_BLANK_DEVIATION
    forward_plus: bool = True
    error_markers: Sequence[str] = log.ERROR_MARKERS
    audio: bool = False
    """Trim Movie Maker's ``frame.wav`` to the run and encode it (AAC) with the footage."""
    encode: bool = True
    crf: int = 16
    preset: str = "medium"
    contact_sheet: bool = True
    contact_tile: Optional[str] = None
    """``width:height`` of one contact-sheet tile; default 480:270 for landscape, 270:480 for portrait."""
    keep_frames: bool = True
    hash_prefix: str = ""
    """Text before each name in ``frames.sha256``; ``frames/`` makes the line a path from the output folder."""
    environment: Optional[Mapping[str, str]] = None
    timeout: float = _godot.TIMEOUT_SECONDS
    check_run: Optional[Callable[[GodotRun], None]] = None
    """Called with Godot's exit code and log before anything is trimmed; raise ``RenderError`` to fail the render."""
    manifest_extra: Mapping[str, Any] = field(default_factory=dict)
    """Anything the caller wants recorded under ``extra`` in ``manifest.json``."""


@dataclass(frozen=True)
class RenderResult:
    output: Path
    manifest: Mapping[str, Any]
    run: GodotRun


def render(
    plan: RenderPlan,
    *,
    media: Optional[Media] = None,
    runner: Optional[Callable[[Sequence[str]], GodotRun]] = None,
) -> RenderResult:
    """Run Godot with Movie Maker and turn what it wrote into verified footage.

    Steps: check the tools and the empty output folder; run Godot (under ``xvfb-run`` on a Linux box with no display)
    with the project override in place; keep its log as ``godot.log``; refuse a log with script or shader errors or a
    renderer other than Forward+; trim the frames (and the audio) to the run; check the frame count, the size of the
    first and last frame and that they are not blank; write ``frames.sha256``; encode bt709 H.264 and check the
    footage with ffprobe; write the contact sheet and ``manifest.json``. Any failure raises ``RenderError`` and leaves
    the partial output for a person to look at.

    ``media`` and ``runner`` (``command -> GodotRun``) stand in for FFmpeg and Godot in tests.
    """
    media = media or Media()
    media.require_tools()
    output = Path(plan.output).resolve()
    if plan.fps <= 0 or plan.frame_count <= 0:
        raise RenderError("fps and frame_count must be positive")
    if output.exists() and any(output.iterdir()):
        raise RenderError(f"{output} is not empty")
    raw = output / "raw"
    raw.mkdir(parents=True, exist_ok=True)

    arguments = _godot.movie_arguments(
        raw, plan.fps, quit_after=plan.quit_after, always_on_top=plan.always_on_top, extra=plan.godot_arguments
    )
    command = _godot.build_command(plan.godot, plan.project, arguments, plan.game_arguments)
    override_path = plan.override_path or Path(plan.project) / "override.cfg"
    with _godot.viewport_override(override_path, plan.size if plan.write_override else None, stretch_viewport=plan.stretch_viewport):
        if runner is not None:
            run = runner(command)
        else:
            run = _godot.run_godot(command, environment=plan.environment, timeout=plan.timeout)
    (output / "godot.log").write_text(run.output, encoding="utf-8")

    log.require_clean_log(run.output, tuple(plan.error_markers))
    renderer = log.require_forward_plus(run.output) if plan.forward_plus else (log.renderer_line(run.output) or "")
    if plan.check_run is not None:
        plan.check_run(run)

    first = plan.start_frame(run.output)
    frames = output / "frames"
    trim_frames(raw, frames, first, plan.frame_count)
    audio_path: Optional[Path] = None
    if plan.audio:
        audio_path = output / "audio.wav"
        trim_audio(raw / f"{_godot.MOVIE_BASE_NAME}.wav", audio_path, plan.fps, first, plan.frame_count)
    verify_frames(frames, plan.frame_count, plan.size, media, min_deviation=plan.min_deviation)
    write_frame_hashes(frames, output / "frames.sha256", prefix=plan.hash_prefix)

    outputs = ["frames.sha256"]
    if audio_path is not None:
        outputs.append("audio.wav")
    if plan.encode:
        footage = output / "footage.mp4"
        media.encode(frames, footage, plan.fps, audio=audio_path, crf=plan.crf, preset=plan.preset)
        media.verify_footage(footage, plan.fps, plan.frame_count, plan.size, require_audio=plan.audio)
        outputs.append("footage.mp4")
        if plan.contact_sheet:
            tile = plan.contact_tile or ("270:480" if plan.size[1] > plan.size[0] else "480:270")
            media.contact_sheet(frames, output / "contact_sheet.png", plan.frame_count, tile=tile)
            outputs.append("contact_sheet.png")
    if not plan.keep_frames:
        shutil.rmtree(frames)
    shutil.rmtree(raw, ignore_errors=True)

    manifest: dict[str, Any] = {
        "fps": plan.fps,
        "frameSize": f"{plan.size[0]}x{plan.size[1]}",
        "frameCount": plan.frame_count,
        "firstMovieFrame": first,
        "rendererLine": renderer,
        "godotExitCode": run.exit_code,
        "outputs": [{"path": name, "sha256": sha256_file(output / name)} for name in outputs],
        "extra": dict(plan.manifest_extra),
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return RenderResult(output, manifest, run)
