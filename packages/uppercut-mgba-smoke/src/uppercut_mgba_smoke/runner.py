"""Run one smoke test: start mGBA in a sandbox, let the Lua script play the game, then check and publish the shots."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Mapping, Optional, Sequence

from . import host
from .config import SmokeConfig, build_command, build_environment, make_sandbox
from .errors import SmokeError
from .lua import parse_result, write_helper
from .png import validate_screenshot_set

RESULT_NAME = "mgba-smoke-result.txt"


@dataclass(frozen=True)
class SmokeRun:
    output: Path
    screenshots: Mapping[str, str]
    """Name to the SHA-256 of each published PNG file."""


def _atomic_copy(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary: Optional[Path] = None
    try:
        with tempfile.NamedTemporaryFile(mode="wb", dir=destination.parent, prefix=f".{destination.name}.", suffix=".tmp", delete=False) as handle:
            temporary = Path(handle.name)
            with source.open("rb") as source_file:
                shutil.copyfileobj(source_file, handle)
            handle.flush()
            os.fsync(handle.fileno())
        temporary.chmod(0o644)
        os.replace(temporary, destination)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def publish_results(staging: Path, output: Path, names: Sequence[str]) -> None:
    """Copy the checked screenshots into ``output``, each through a temporary file and a rename, then write
    ``mgba-smoke-result.txt`` (``PASS``) last, so a reader that sees the result file sees every screenshot."""
    output.mkdir(parents=True, exist_ok=True)
    for name in names:
        _atomic_copy(staging / name, output / name)
    with tempfile.NamedTemporaryFile(mode="w", encoding="ascii", dir=output, prefix=".mgba-smoke-result.", suffix=".tmp", delete=False) as handle:
        result = Path(handle.name)
        handle.write("PASS\n")
        handle.flush()
        os.fsync(handle.fileno())
    try:
        result.chmod(0o644)
        os.replace(result, output / RESULT_NAME)
    finally:
        result.unlink(missing_ok=True)


def _tail(path: Path, lines: int = 40) -> str:
    try:
        return "\n".join(path.read_text(encoding="utf-8", errors="replace").splitlines()[-lines:])
    except OSError:
        return ""


def _wait_for_result(process: "subprocess.Popen[bytes]", result: Path, timeout: float, *, sleep: Callable[[float], None], monotonic: Callable[[], float]) -> str:
    deadline = monotonic() + timeout
    while monotonic() < deadline:
        if result.is_file():
            try:
                return result.read_text(encoding="utf-8")
            except OSError as error:
                raise SmokeError(f"cannot read Lua smoke result: {error}") from error
        code = process.poll()
        if code is not None:
            # The script may have written its result just before mGBA exited.
            if result.is_file():
                continue
            raise SmokeError(f"mGBA exited with status {code} before producing a result")
        sleep(0.1)
    raise SmokeError(f"timed out after {timeout:.0f}s waiting for Lua smoke result")


def run_smoke(
    config: SmokeConfig,
    *,
    popen: Callable[..., "subprocess.Popen[bytes]"] = subprocess.Popen,
    run: host.Run = subprocess.run,
    platform: Optional[str] = None,
    sleep: Callable[[float], None] = time.sleep,
    monotonic: Callable[[], float] = time.monotonic,
) -> SmokeRun:
    """Run the smoke test described by ``config`` and publish its screenshots.

    Checks the files exist, that no other mGBA runs and (for the AppleScript loader) that the platform is macOS with
    Accessibility access; starts mGBA with everything it writes inside a temporary sandbox; waits for the script's
    ``PASS`` or ``FAIL``; validates the screenshots; publishes them atomically. On any failure the last lines of
    mGBA's log are added to the message. Raises ``SmokeError``.

    ``popen``, ``run``, ``platform``, ``sleep`` and ``monotonic`` exist so tests can stand in for the machine.
    """
    system = platform if platform is not None else sys.platform
    if config.loader == "applescript" and system != "darwin":
        raise SmokeError('the "applescript" loader needs macOS; use loader="argument" with an mGBA that has --script')
    for path, label in ((config.mgba, "mGBA"),):
        if not path.is_file() or not os.access(path, os.X_OK):
            raise SmokeError(f"{label} is not executable: {path}")
    for path, label in ((config.rom, "ROM"), (config.lua, "Lua smoke script")):
        if not path.is_file():
            raise SmokeError(f"{label} does not exist: {path}")
    if config.check_running:
        host.ensure_no_running_mgba(config.process_name, run=run)
    if config.loader == "applescript":
        host.ensure_accessibility(run=run)

    with make_sandbox(config.sandbox_prefix) as sandbox:
        write_helper(sandbox.helper)
        command = build_command(config, sandbox)
        environment = build_environment(config, sandbox)
        process: Optional["subprocess.Popen[bytes]"] = None
        try:
            with sandbox.log.open("wb") as log:
                process = popen(command, env=environment, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
                if config.loader == "applescript":
                    host.load_lua_script(config.lua, process.pid, run=run)
                text = _wait_for_result(process, sandbox.result, config.timeout, sleep=sleep, monotonic=monotonic)
            outcome = parse_result(text)
            if not outcome.passed:
                raise SmokeError(f"Lua smoke test failed:\n{outcome.detail}")
            images = validate_screenshot_set(
                sandbox.screenshots,
                config.screenshots,
                reviewed_sha256=config.reviewed_sha256,
                width=config.width,
                height=config.height,
                min_colors=config.min_colors,
            )
            publish_results(sandbox.screenshots, config.output, config.screenshots)
            return SmokeRun(config.output, {name: image.sha256 for name, image in images.items()})
        except (OSError, SmokeError, subprocess.SubprocessError) as error:
            tail = _tail(sandbox.log)
            message = str(error) if isinstance(error, SmokeError) else f"{type(error).__name__}: {error}"
            if tail:
                message += f"\nLast mGBA log lines:\n{tail}"
            raise SmokeError(message) from error
        finally:
            if process is not None and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
