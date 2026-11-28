"""The checks: plain C integer spellings, pointer stars away from the type, and byte-range loops on a wide index."""

from __future__ import annotations

import re
import subprocess
from dataclasses import dataclass
from fnmatch import fnmatchcase
from pathlib import Path
from typing import Iterable, Iterator, Optional, Sequence

from ._config import CStyleError, StyleConfig, find_config

LITERALS = re.compile(r'''/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*' ''', re.S | re.X)
# Plain C integer spellings, SDK short names and the <stdint.h> names: each should be a fixed-width alias.
RAW = re.compile(r"\b(?:int|long|short|unsigned|signed|s(?:8|16|32|64)|u?int(?:8|16|32|64)_t)\b")
BASE_TYPE_NAMES = frozenset({"char", "void", "float", "double", "FILE", "size_t", "int", "unsigned"})
_TYPEDEF_AGGREGATE = re.compile(r"\btypedef\s+(?:struct|union|enum)\s*\w*\s*\{[^{}]*(?:\{[^{}]*\}[^{}]*)*\}\s*(\w+)\s*;")
_TYPEDEF_SIMPLE = re.compile(r"\btypedef\s+\w+\s+(\w+)\s*;")
_INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.M)
_DEFINE = re.compile(r"^\s*#define[ \t]+(\w+)[ \t]+(\d+|0[xX][\da-fA-F]+)[uU]?[ \t]*$", re.M)


@dataclass(frozen=True)
class Finding:
    """One problem: the path relative to the root (with ``/``), the 1-based line and what to change."""

    path: str
    line: int
    message: str

    def __str__(self) -> str:
        return f"{self.path}:{self.line}: {self.message}"


@dataclass(frozen=True)
class CheckResult:
    """The findings of one run and the number of C sources and headers it read."""

    findings: tuple[Finding, ...]
    checked: int

    @property
    def ok(self) -> bool:
        return not self.findings


def strip_literals(source: str) -> str:
    """Blank out comments, strings and character constants, keeping every newline so line numbers stay right."""
    return LITERALS.sub(lambda m: re.sub(r"[^\n]", " ", m[0]), source)


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8-sig")


class _Constants:
    """Integer ``#define`` values reachable through local quoted includes, read once per file."""

    def __init__(self, root: Path) -> None:
        self.root = root.resolve()
        self._files: dict[Path, tuple[list[str], dict[str, int]]] = {}

    def _definitions(self, path: Path) -> tuple[list[str], dict[str, int]]:
        if path not in self._files:
            text = _read(path)
            includes = _INCLUDE.findall(text)
            macros = _DEFINE.findall(strip_literals(text))
            self._files[path] = (includes, {name: int(value, 0) for name, value in macros})
        return self._files[path]

    def for_file(self, path: Path) -> dict[str, Optional[int]]:
        # A name defined twice with different values is ambiguous and left to a reviewer.
        pending = [path.resolve()]
        visited: set[Path] = set()
        values: dict[str, Optional[int]] = {}
        while pending:
            file = pending.pop()
            if file in visited:
                continue
            visited.add(file)
            includes, macros = self._definitions(file)
            for name, value in macros.items():
                values[name] = value if name not in values or values[name] == value else None
            for name in includes:
                header = (file.parent / name).resolve()
                if header.is_relative_to(self.root) and header.is_file():
                    pending.append(header)
        return values


def _loop_pattern(config: StyleConfig) -> re.Pattern[str]:
    types = "|".join(re.escape(name) for name in sorted(config.types, key=lambda name: (-len(name), name)))
    return re.compile(
        r"\bfor\s*\(\s*(?P<type>" + types + r"|size_t)\s+(?P<index>\w+)\s*=\s*\d+[uU]?"
        r"\s*;\s*(?P=index)\s*(?P<op><=?)\s*(?P<end>0[xX][\da-fA-F]+[uU]?|\d+[uU]?|[A-Z][A-Z_0-9]*)\s*(?:;|&&)"
    )


def _file_findings(
    path: str,
    source: str,
    pointers: re.Pattern[str],
    loop: re.Pattern[str],
    constants: dict[str, Optional[int]],
    config: StyleConfig,
) -> Iterator[Finding]:
    label = f"a {config.label} fixed-width type" if config.label else "a fixed-width type"
    signatures = config.abi.get(path, ())
    # inttypes.h conversion macros can need a different underlying C type from an SDK typedef of the same width.
    code = re.sub(r"\(\s*u?int(?:8|16|32|64)_t\s*\)", "", source)
    code = re.sub(r"\bint(?=\s+main\s*\()", "   ", code)
    code = re.sub(r"(\bmain\s*\(\s*)int(?=\s+argc\b)", r"\g<1>   ", code)
    if signatures:
        # Only the signatures belong to the external ABI. Their bodies are checked normally.
        interface = re.compile(
            r"^[ \t]*(?:static\s+)?[\w* ]+\b(?:" + "|".join(signatures) + r")\s*\([^()]*\)\s*(?=[{;])", re.M
        )
        code = interface.sub(lambda m: RAW.sub(lambda t: " " * len(t[0]), m[0]), code)
    for line, statement in enumerate(code.splitlines(), 1):
        if m := RAW.search(statement):
            yield Finding(path, line, f"use {label} for {m[0]}")
    for m in pointers.finditer(source):
        yield Finding(path, source.count("\n", 0, m.start()) + 1, "attach the pointer to its type")
    for m in loop.finditer(source):
        token = m["end"]
        end = int(token.rstrip("uU"), 0) if token[0].isdigit() else constants.get(token)
        if end is None:
            continue
        limit = end - (m["op"] == "<")
        if 0 <= limit <= 255:
            line = source.count("\n", 0, m.start()) + 1
            if m["type"] != config.byte_type:
                yield Finding(path, line, f"use {config.byte_type} for the byte-range loop")
            elif limit == 255:
                yield Finding(path, line, f"use an explicit exit before the {config.byte_type} index wraps")


def is_excluded(path: str, config: StyleConfig) -> bool:
    """Whether ``path`` (relative, with ``/``) matches one of the ``exclude`` patterns."""
    return any(fnmatchcase(path, pattern) for pattern in config.exclude)


def list_sources(root: "Path | str", config: StyleConfig, pathspecs: Sequence[str] = ()) -> list[str]:
    """The files git knows under ``root`` that the check reads: tracked files, and new files git does not ignore when
    ``config.untracked`` is set, limited to ``pathspecs`` (or ``config.include``), the suffixes and the excludes."""
    root = Path(root)
    command = ["git", "ls-files", "--cached"]
    if config.untracked:
        command += ["--others", "--exclude-standard"]
    command += ["-z", "--", *(pathspecs or config.include)]
    try:
        listing = subprocess.run(command, cwd=root, check=True, capture_output=True).stdout
    except FileNotFoundError as error:
        raise CStyleError("git is not on PATH") from error
    except subprocess.CalledProcessError as error:
        message = error.stderr.decode(errors="replace").strip() or f"git exited with {error.returncode}"
        raise CStyleError(f"{root}: {message}") from error
    names = []
    for name in filter(None, listing.decode("utf-8", errors="surrogateescape").split("\0")):
        path = root / name
        if is_excluded(name, config) or path.suffix not in config.suffixes or not path.is_file():
            continue
        names.append(name)
    return names


def check_paths(root: "Path | str", names: Iterable[str], config: Optional[StyleConfig] = None) -> CheckResult:
    """Check the files ``names`` (relative to ``root``, with ``/``) and return every finding, file by file.

    The type names a pointer may be attached to are the C base types, the configured aliases, anything ending in
    ``_t`` and every ``typedef`` name declared in these files. Loop bounds written as macros are looked up in the
    file and the local headers it includes.
    """
    root = Path(root)
    config = config if config is not None else find_config(root)
    sources: dict[str, str] = {}
    names_seen = set(BASE_TYPE_NAMES) | set(config.types)
    for name in names:
        source = strip_literals(_read(root / name))
        sources[name] = source
        names_seen.update(_TYPEDEF_AGGREGATE.findall(source))
        names_seen.update(_TYPEDEF_SIMPLE.findall(source))
    types = r"\b(?:" + "|".join(sorted(names_seen)) + r"|\w+_t)"
    pointers = re.compile(types + r"(?:[ \t]+\*+(?=[ \t]*(?:\w|\)))|\*+(?=[A-Za-z_]))")
    loop = _loop_pattern(config)
    constants = _Constants(root)
    findings: list[Finding] = []
    for name, source in sources.items():
        findings.extend(_file_findings(name, source, pointers, loop, constants.for_file(root / name), config))
    return CheckResult(tuple(findings), len(sources))


def check_repo(
    root: "Path | str", config: Optional[StyleConfig] = None, pathspecs: Sequence[str] = ()
) -> CheckResult:
    """List the sources of the git work tree at ``root`` and check them. ``config`` defaults to ``find_config(root)``."""
    config = config if config is not None else find_config(root)
    return check_paths(root, list_sources(root, config, pathspecs), config)
