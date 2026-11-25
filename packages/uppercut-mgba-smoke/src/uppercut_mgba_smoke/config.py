"""What a smoke run is, and the pure functions that turn it into a sandbox, a command line and an environment."""

from __future__ import annotations

import contextlib
import os
import re
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterator, Mapping, Optional, Sequence

from .errors import SmokeError

LOADERS = ("applescript", "argument")
RESERVED_ENV = ("XDG_CONFIG_HOME", "MGBA_SMOKE_OUTPUT", "MGBA_SMOKE_RESULT", "MGBA_SMOKE_HELPER")
_ENV_NAME = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
_PREFIX = re.compile(r"^[A-Z][A-Z0-9_]*$")


@dataclass(frozen=True)
class SmokeConfig:
    """One smoke run. Everything about the game is here; nothing is built in."""

    mgba: Path
    """The mGBA executable (its Qt frontend, the one with the scripting window)."""
    rom: Path
    lua: Path
    """The caller's smoke script."""
    output: Path
    """Where passing screenshots and ``mgba-smoke-result.txt`` are published."""
    screenshots: Sequence[str]
    """The exact screenshot file names the script must produce (for example ``01-title.png``), no more and no fewer."""
    timeout: float = 90.0
    env: Mapping[str, str] = field(default_factory=dict)
    """Extra environment for the script: the game's RAM addresses and struct offsets, as strings."""
    env_prefix: Optional[str] = None
    """If set (for example ``MY_GAME``), also export ``MY_GAME_OUTPUT`` and ``MY_GAME_RESULT``, so a script written
    before this package, which reads those names, keeps working."""
    reviewed_sha256: Mapping[str, str] = field(default_factory=dict)
    """Screenshot name to the SHA-256 of its PNG, for shots a person reviewed and pinned."""
    loader: str = "applescript"
    """How the script is loaded: ``"applescript"`` drives mGBA's Tools > Scripting menu on macOS (works with older
    mGBA, needs Accessibility access), ``"argument"`` passes ``--script`` (mGBA builds that have the option)."""
    scale: int = 2
    process_name: str = "mGBA"
    """The process name the single-instance check looks for with ``pgrep -x``."""
    check_running: bool = True
    sandbox_prefix: str = "mgba-smoke-"
    width: int = 240
    height: int = 160
    min_colors: int = 4
    mgba_options: Mapping[str, str] = field(default_factory=dict)
    """Extra ``-C name=value`` options, added after the isolating ones (which they can override)."""

    def __post_init__(self) -> None:
        if self.timeout <= 0:
            raise SmokeError("timeout must be positive")
        if self.loader not in LOADERS:
            raise SmokeError(f"loader must be one of {LOADERS}, not {self.loader!r}")
        if not 1 <= self.scale <= 6:
            raise SmokeError("scale must be 1 to 6")
        names = list(self.screenshots)
        if not names:
            raise SmokeError("screenshots must name at least one file")
        for name in names:
            if not name.endswith(".png") or "/" in name or "\\" in name or name.startswith("."):
                raise SmokeError(f"screenshot name {name!r} must be a plain file name ending in .png")
        if len(set(names)) != len(names):
            raise SmokeError("screenshots lists a name twice")
        if self.env_prefix is not None and not _PREFIX.match(self.env_prefix):
            raise SmokeError("env_prefix must be capitals, digits and underscores, starting with a capital")
        for key, value in self.env.items():
            if not _ENV_NAME.match(key):
                raise SmokeError(f"env name {key!r} is not a valid environment variable name")
            if key in RESERVED_ENV or key.startswith("MGBA_SMOKE_"):
                raise SmokeError(f"env name {key} is set by the runner")
            if not isinstance(value, str):
                raise SmokeError(f"env value for {key} must be a string")
            if "\0" in value:
                raise SmokeError(f"env value for {key} holds a NUL")
        for key in self.reviewed_sha256:
            if key not in names:
                raise SmokeError(f"reviewed_sha256 names {key}, which is not in screenshots")


@dataclass(frozen=True)
class Sandbox:
    """The throwaway folders one run uses, so nothing it does touches the player's own mGBA setup."""

    root: Path
    config: Path
    screenshots: Path
    saves: Path
    states: Path
    cheats: Path
    result: Path
    log: Path
    helper: Path


@contextlib.contextmanager
def make_sandbox(prefix: str = "mgba-smoke-", *, parent: Optional[Path] = None) -> Iterator[Sandbox]:
    """Create the sandbox in a fresh temporary folder and delete it afterwards."""
    with tempfile.TemporaryDirectory(prefix=prefix, dir=str(parent) if parent else None) as temporary:
        root = Path(temporary)
        box = Sandbox(
            root=root,
            config=root / "config",
            screenshots=root / "screenshots",
            saves=root / "saves",
            states=root / "states",
            cheats=root / "cheats",
            result=root / "mgba-smoke-result.txt",
            log=root / "mgba.log",
            helper=root / "mgba_smoke_helper.lua",
        )
        for folder in (box.config, box.screenshots, box.saves, box.states, box.cheats):
            folder.mkdir()
        yield box


def build_command(config: SmokeConfig, sandbox: Sandbox) -> list[str]:
    """The mGBA command line: windowed at ``scale``, muted, no autoload or autosave, and every path it writes to
    (screenshots, saves, states, cheats) inside the sandbox."""
    options: dict[str, str] = {
        "mute": "1",
        "pauseOnFocusLost": "0",
        "autoload": "0",
        "autosave": "0",
        "screenshotPath": str(sandbox.screenshots),
        "savegamePath": str(sandbox.saves),
        "savestatePath": str(sandbox.states),
        "cheatsPath": str(sandbox.cheats),
    }
    options.update(config.mgba_options)
    command = [str(config.mgba), f"-{config.scale}"]
    for name, value in options.items():
        command += ["-C", f"{name}={value}"]
    if config.loader == "argument":
        command += ["--script", str(config.lua)]
    command.append(str(config.rom))
    return command


def build_environment(config: SmokeConfig, sandbox: Sandbox, base: Optional[Mapping[str, str]] = None) -> dict[str, str]:
    """The child's environment: a copy of ``base`` (default ``os.environ``) with mGBA's config folder moved into the
    sandbox, the output, result and helper paths, any prefixed aliases, and the caller's ``env``."""
    environment = dict(os.environ if base is None else base)
    environment.update(
        {
            "XDG_CONFIG_HOME": str(sandbox.config),
            "MGBA_SMOKE_OUTPUT": str(sandbox.screenshots),
            "MGBA_SMOKE_RESULT": str(sandbox.result),
            "MGBA_SMOKE_HELPER": str(sandbox.helper),
        }
    )
    if config.env_prefix:
        environment[f"{config.env_prefix}_OUTPUT"] = str(sandbox.screenshots)
        environment[f"{config.env_prefix}_RESULT"] = str(sandbox.result)
    environment.update(config.env)
    return environment
