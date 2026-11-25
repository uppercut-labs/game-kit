"""BizHawk's `config.ini` is JSON. These helpers make a private copy for one run and edit it, so the shared file is never touched.

BizHawk rewrites its config when it exits, and sibling sessions often share one BizHawk folder. A run therefore gets its
own copy beside its output (`--config=<copy>`), and an edit of the shared file itself is refused while the emulator runs.
"""

from __future__ import annotations

import copy
import json
import os
import subprocess
from pathlib import Path
from typing import Any, Callable, Dict, Iterable, Mapping, MutableMapping, Optional, Sequence, Tuple, Union

from .errors import ConfigError

PathLike = Union[str, Path]
Config = Dict[str, Any]
KeyPath = Tuple[str, ...]

SOUND_KEYS = ("SoundEnabled", "SoundEnabledNormal", "SoundEnabledRWFF")
"""The top-level switches that turn BizHawk's sound output on."""


def load_config(path: PathLike) -> Config:
    """Read a config.ini (JSON, with or without a byte order mark)."""
    try:
        data = json.loads(Path(path).read_text(encoding="utf-8-sig"))
    except json.JSONDecodeError as error:
        raise ConfigError(f"{path} is not BizHawk's JSON config: {error}") from error
    if not isinstance(data, dict):
        raise ConfigError(f"{path} holds {type(data).__name__}, not an object")
    return data


def dump_config(config: Mapping[str, Any], path: PathLike) -> Path:
    target = Path(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(config, indent=2), encoding="utf-8")
    return target


def set_path(config: MutableMapping[str, Any], path: Sequence[str], value: Any, *, create: bool = False) -> None:
    """Set `config[path[0]][path[1]]... = value`. A path is a tuple because a core's name holds dots
    (`("CoreSettings", "BizHawk.Emulation.Cores.Nintendo.Gameboy.Gameboy", "Muted")`). A missing step raises `ConfigError`
    unless `create` is true, which makes it (as an empty object)."""
    if not path:
        raise ConfigError("an empty path")
    node: Any = config
    for step in path[:-1]:
        if not isinstance(node, MutableMapping):
            raise ConfigError(f"{'/'.join(path)}: {step!r} is under something that is not an object")
        if step not in node:
            if not create:
                raise ConfigError(f"{'/'.join(path)}: no {step!r} in this config (the core or its settings were never saved)")
            node[step] = {}
        node = node[step]
    if not isinstance(node, MutableMapping):
        raise ConfigError(f"{'/'.join(path)}: the parent is not an object")
    node[path[-1]] = value


def prefer_core(config: MutableMapping[str, Any], system: str, core: str) -> None:
    """`PreferredCores[system] = core` (`prefer_core(cfg, "PSX", "Octoshock")`)."""
    set_path(config, ("PreferredCores", system), core, create=True)


def mute(config: MutableMapping[str, Any]) -> None:
    """Turn BizHawk's sound output off."""
    for key in SOUND_KEYS:
        config[key] = False


def run_in_background(config: MutableMapping[str, Any], on: bool = True) -> None:
    """BizHawk keeps running, and keeps its input, when its window has no focus."""
    config["RunInBackground"] = on


def make_run_config(
    base: PathLike,
    target: PathLike,
    *,
    edits: Iterable[Tuple[Sequence[str], Any]] = (),
    sound: bool = False,
    background: bool = True,
    cores: Optional[Mapping[str, str]] = None,
    create: bool = False,
) -> Path:
    """Copy the config at `base` to `target` with these changes: sound off (unless `sound`), run in the background (unless not),
    the `cores` to prefer (system name to core name), and `edits`, a list of `(path, value)` as in `set_path`.

    `base` is only read. Returns `target`."""
    config = copy.deepcopy(load_config(base))
    if not sound:
        mute(config)
    if background:
        run_in_background(config)
    for system, core in (cores or {}).items():
        prefer_core(config, system, core)
    for path, value in edits:
        set_path(config, path, value, create=create)
    return dump_config(config, target)


PsRunner = Callable[[Sequence[str]], str]


def _default_ps(argv: Sequence[str]) -> str:
    return subprocess.run(list(argv), capture_output=True, text=True, check=False).stdout


def emuhawk_running(
    names: Sequence[str] = ("EmuHawk.exe", "EmuHawk"),
    *,
    ps: Optional[PsRunner] = None,
    windows: Optional[bool] = None,
) -> bool:
    """Whether a program called one of `names` is running (any user's, any instance). `ps` runs a command and gives its
    output (a stand-in for tests); the default runs `tasklist` on Windows and `ps -A -o comm=` elsewhere."""
    win = (os.name == "nt") if windows is None else windows
    run = ps or _default_ps
    if win:
        out = run(["tasklist", "/FO", "CSV", "/NH"])
        listed = {line.split('","')[0].strip('"').lower() for line in out.splitlines() if line.strip()}
    else:
        out = run(["ps", "-A", "-o", "comm="])
        listed = {Path(line.strip()).name.lower() for line in out.splitlines() if line.strip()}
    return any(n.lower() in listed for n in names)


def edit_config_in_place(
    path: PathLike,
    edits: Iterable[Tuple[Sequence[str], Any]],
    *,
    names: Sequence[str] = ("EmuHawk.exe", "EmuHawk"),
    ps: Optional[PsRunner] = None,
    create: bool = False,
    keep_copy: Optional[PathLike] = None,
) -> Config:
    """Edit a shared config.ini itself, as a one-off set-up step. Refuses with `ConfigError` while BizHawk runs (it rewrites
    the file when it exits and would undo the edit). `keep_copy` also saves the edited file there as evidence of the settings
    a capture used. Prefer `make_run_config` for anything a run depends on."""
    if emuhawk_running(names, ps=ps):
        raise ConfigError("EmuHawk is running; refusing to edit config.ini (BizHawk rewrites it on exit)")
    config = load_config(path)
    for key_path, value in edits:
        set_path(config, key_path, value, create=create)
    dump_config(config, path)
    if keep_copy is not None:
        dump_config(config, keep_copy)
    return config


def parse_assignment(text: str) -> Tuple[KeyPath, Any]:
    """`PreferredCores/PSX=Octoshock` to `(("PreferredCores", "PSX"), "Octoshock")`. Steps are separated by `/`; the value is read
    as JSON when it is JSON (`true`, `3`, `"x"`) and is the text itself otherwise."""
    if "=" not in text:
        raise ConfigError(f"{text!r}: expected path=value")
    key, _, raw = text.partition("=")
    path = tuple(step for step in key.split("/") if step)
    if not path:
        raise ConfigError(f"{text!r}: an empty path")
    try:
        value: Any = json.loads(raw)
    except json.JSONDecodeError:
        value = raw
    return path, value


__all__ = [
    "SOUND_KEYS", "Config", "KeyPath", "dump_config", "edit_config_in_place", "emuhawk_running", "load_config", "make_run_config",
    "mute", "parse_assignment", "prefer_core", "run_in_background", "set_path",
]
