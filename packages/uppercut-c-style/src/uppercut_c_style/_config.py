"""Per-repository configuration: a ``[tool.uppercut-c-style]`` table in ``pyproject.toml``, or a TOML or JSON file."""

from __future__ import annotations

import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Mapping

if sys.version_info >= (3, 11):
    import tomllib
else:  # pragma: no cover - exercised only on Python 3.10
    import tomli as tomllib

DEFAULT_TYPES = ("u8", "u16", "u32", "u64", "i8", "i16", "i32", "i64")
DEFAULT_SUFFIXES = (".c", ".h", ".inl")
CONFIG_FILES = ("uppercut-c-style.toml", "uppercut-c-style.json")
TABLE = "uppercut-c-style"
_IDENTIFIER = re.compile(r"^[A-Za-z_]\w*$")


class CStyleError(Exception):
    """Raised for a configuration that cannot be read, or a root that is not a git work tree."""


@dataclass(frozen=True)
class StyleConfig:
    """What one repository checks.

    ``types`` are the fixed-width aliases the code uses (they also count as type names for the pointer check).
    ``byte_type`` is the alias a loop over a byte range must use. ``abi`` maps a file path, relative to the root and
    written with ``/``, to the function names whose signatures belong to an external ABI and may use plain C integer
    types; their bodies are checked as usual. ``exclude`` lists glob patterns of paths to skip (vendored code, an
    alias header). ``include`` lists git pathspecs to check when none are given on the command line. ``suffixes`` are
    the file extensions checked. ``label`` goes into the messages ("use a lab fixed-width type for int").
    ``untracked`` also checks new files that git does not ignore.
    """

    types: tuple[str, ...] = DEFAULT_TYPES
    byte_type: str = "u8"
    abi: Mapping[str, tuple[str, ...]] = field(default_factory=dict)
    exclude: tuple[str, ...] = ()
    include: tuple[str, ...] = ()
    suffixes: tuple[str, ...] = DEFAULT_SUFFIXES
    label: str = ""
    untracked: bool = True

    def __post_init__(self) -> None:
        for name in (*self.types, self.byte_type):
            if not _IDENTIFIER.match(name):
                raise CStyleError(f"{name!r} is not a C identifier")
        if self.byte_type not in self.types:
            raise CStyleError(f"byte-type {self.byte_type!r} must be one of the types")
        for path, names in self.abi.items():
            for name in names:
                if not _IDENTIFIER.match(name):
                    raise CStyleError(f"abi entry for {path}: {name!r} is not a C identifier")


def _strings(value: Any, key: str) -> tuple[str, ...]:
    if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
        raise CStyleError(f"{key} must be a list of strings")
    return tuple(value)


def config_from_mapping(table: Mapping[str, Any]) -> StyleConfig:
    """Build a ``StyleConfig`` from a table that uses the file keys (``byte-type``, ``abi`` and the rest)."""
    options: dict[str, Any] = {}
    for key, value in table.items():
        if key in ("types", "exclude", "include", "suffixes"):
            options[key] = _strings(value, key)
        elif key == "byte-type":
            if not isinstance(value, str):
                raise CStyleError("byte-type must be a string")
            options["byte_type"] = value
        elif key == "label":
            if not isinstance(value, str):
                raise CStyleError("label must be a string")
            options["label"] = value
        elif key == "untracked":
            if not isinstance(value, bool):
                raise CStyleError("untracked must be true or false")
            options["untracked"] = value
        elif key == "abi":
            if not isinstance(value, Mapping):
                raise CStyleError("abi must be a table of file paths to lists of function names")
            options["abi"] = {str(path): _strings(names, f"abi.{path}") for path, names in value.items()}
        else:
            raise CStyleError(f"unknown setting {key!r}")
    return StyleConfig(**options)


def load_config(path: "Path | str") -> StyleConfig:
    """Read a configuration file.

    ``pyproject.toml`` is read for its ``[tool.uppercut-c-style]`` table. Any other ``.toml`` file holds the settings
    at its top level, and a ``.json`` file holds them in one object.
    """
    path = Path(path)
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as error:
        raise CStyleError(f"{path}: {error.strerror or error}") from error
    try:
        if path.suffix == ".json":
            table = json.loads(text)
        else:
            table = tomllib.loads(text)
            if path.name == "pyproject.toml":
                table = table.get("tool", {}).get(TABLE, {})
    except (ValueError, tomllib.TOMLDecodeError) as error:
        raise CStyleError(f"{path}: {error}") from error
    if not isinstance(table, Mapping):
        raise CStyleError(f"{path}: the settings must be a table")
    try:
        return config_from_mapping(table)
    except CStyleError as error:
        raise CStyleError(f"{path}: {error}") from None


def find_config(root: "Path | str") -> StyleConfig:
    """The configuration of the repository at ``root``.

    The first of these wins: a ``pyproject.toml`` with a ``[tool.uppercut-c-style]`` table, ``uppercut-c-style.toml``
    and ``uppercut-c-style.json``. With none of them, the defaults apply.
    """
    root = Path(root)
    pyproject = root / "pyproject.toml"
    if pyproject.is_file():
        try:
            data = tomllib.loads(pyproject.read_text(encoding="utf-8"))
        except tomllib.TOMLDecodeError as error:
            raise CStyleError(f"{pyproject}: {error}") from error
        if TABLE in data.get("tool", {}):
            return load_config(pyproject)
    for name in CONFIG_FILES:
        if (root / name).is_file():
            return load_config(root / name)
    return StyleConfig()
