"""Starting Godot with Movie Maker: the command line, the run, and the project override that sizes the frames."""

from __future__ import annotations

import contextlib
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterator, Mapping, Optional, Sequence

from .errors import RenderError

TIMEOUT_SECONDS = 1800
# Square, so a portrait window fits as well as a landscape one.
XVFB_SCREEN = "-screen 0 1920x1920x24"
MOVIE_BASE_NAME = "frame"


@dataclass(frozen=True)
class GodotRun:
    exit_code: int
    output: str
    """Standard output then standard error."""


def find_godot(explicit: Optional[str] = None, environ: Optional[Mapping[str, str]] = None) -> Optional[str]:
    """``explicit``, else ``$GODOT``, else ``godot`` or ``godot4`` on ``PATH``; ``None`` when none is found."""
    if explicit:
        return explicit
    env = os.environ if environ is None else environ
    if env.get("GODOT"):
        return env["GODOT"]
    for name in ("godot", "godot4"):
        found = shutil.which(name)
        if found:
            return found
    return None


def needs_virtual_display(platform: Optional[str] = None, environ: Optional[Mapping[str, str]] = None) -> bool:
    """True on Linux with neither ``DISPLAY`` nor ``WAYLAND_DISPLAY``, where a movie render needs ``xvfb-run``."""
    system = platform if platform is not None else sys.platform
    env = os.environ if environ is None else environ
    return system.startswith("linux") and not env.get("DISPLAY") and not env.get("WAYLAND_DISPLAY")


def movie_arguments(
    raw_directory: Path,
    fps: int,
    *,
    quit_after: Optional[int] = None,
    resolution: Optional[tuple[int, int]] = None,
    always_on_top: bool = False,
    extra: Sequence[str] = (),
) -> list[str]:
    """Godot's own flags for a Movie Maker render of PNG frames at ``fps``, written to
    ``<raw_directory>/frame00000000.png`` onward (and ``frame.wav`` beside them). ``quit_after`` bounds the run in
    frames; ``always_on_top`` keeps another window from covering a macOS render, which then repeats frames."""
    if fps <= 0:
        raise RenderError("fps must be positive")
    arguments: list[str] = []
    if always_on_top:
        arguments.append("--always-on-top")
    arguments += ["--write-movie", str(Path(raw_directory) / f"{MOVIE_BASE_NAME}.png"), "--fixed-fps", str(fps)]
    if quit_after is not None:
        arguments += ["--quit-after", str(quit_after)]
    if resolution is not None:
        arguments += ["--resolution", f"{resolution[0]}x{resolution[1]}"]
    return arguments + list(extra)


def build_command(godot: str, project: Path, godot_arguments: Sequence[str], game_arguments: Sequence[str] = ()) -> list[str]:
    """``godot --path <project> <godot arguments> -- <game arguments>``; the game's own flags follow ``--``."""
    command = [godot, "--path", str(project), *godot_arguments]
    if game_arguments:
        command += ["--", *game_arguments]
    return command


Runner = Callable[[Sequence[str]], GodotRun]


def run_godot(
    command: Sequence[str],
    *,
    display: bool = True,
    environment: Optional[Mapping[str, str]] = None,
    timeout: float = TIMEOUT_SECONDS,
    run: Callable[..., "subprocess.CompletedProcess[str]"] = subprocess.run,
    platform: Optional[str] = None,
) -> GodotRun:
    """Run Godot to completion and capture its log. With ``display`` it is wrapped in ``xvfb-run`` when Linux has no
    display. A timeout is a ``RenderError``."""
    env = dict(os.environ)
    if environment:
        env.update(environment)
    full = list(command)
    if display and needs_virtual_display(platform, env):
        full = ["xvfb-run", "-a", "-s", XVFB_SCREEN, *full]
    try:
        completed = run(full, capture_output=True, text=True, timeout=timeout, check=False, env=env)
    except subprocess.TimeoutExpired as error:
        raise RenderError(f"Godot did not finish within {timeout:.0f} s") from error
    except OSError as error:
        raise RenderError(f"cannot run Godot: {error}") from error
    return GodotRun(completed.returncode, (completed.stdout or "") + (completed.stderr or ""))


@contextlib.contextmanager
def viewport_override(path: Path, size: Optional[tuple[int, int]] = None, *, stretch_viewport: bool = True) -> Iterator[None]:
    """Size Movie Maker's frames while the block runs, by writing ``override.cfg`` in the project.

    Movie Maker records at the project's viewport size, not the window's (``--resolution`` is ignored), so a render at
    another size writes an ``override.cfg`` that exists only while that render runs. With ``stretch_viewport`` the
    window content also stretches from the viewport size, so a screen shorter than the frame cannot letterbox it.

    The block refuses to start, whatever the size, while the file exists: another render is running, or a killed one
    left it behind and every later run in the checkout would present at the wrong size without saying so. With
    ``size=None`` nothing is written and only that guard applies. The file is removed on any exit.
    """
    path = Path(path)
    stale = f"{path} exists: another render is running, or a killed one left it behind; delete it once nothing is rendering"
    if path.exists():
        raise RenderError(stale)
    if size is None:
        yield
        return
    text = f"[display]\nwindow/size/viewport_width={size[0]}\nwindow/size/viewport_height={size[1]}\n"
    if stretch_viewport:
        text += 'window/stretch/mode="viewport"\n'
    try:
        with path.open("x", encoding="utf-8") as stream:
            stream.write("; Written for one Movie Maker render; deleted when it ends.\n" + text)
    except FileExistsError as error:
        raise RenderError(stale) from error
    try:
        yield
    finally:
        path.unlink(missing_ok=True)
