"""Start BizHawk with a Lua script and a config table, wait for the script's `.done` or `.err` file, stop only what was started.

The contract with the Lua script is the one capture scripts already use: the script reads a Lua file named by an environment
variable (`return { out = "<prefix>", frames = 600, ... }`), does its work, and writes `<prefix>.done` when it finished or
`<prefix>.err` with a message when it failed. Everything else (what it dumps, how many frames) is the script's and the caller's.

Consistent with `@uppercut-labs/emulator-session` (the TypeScript session for other emulators): an isolated config for the run,
muted and allowed to run in the background where the config can say so, a bounded log, and a stop that signals only the process
this run started. The two do not share code; BizHawk is driven by its Lua script, not by window captures.
"""

from __future__ import annotations

import collections
import hashlib
import json
import os
import signal
import subprocess
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Deque, Dict, IO, Iterable, List, Mapping, Optional, Sequence, Tuple, Union

from .config import PathLike, make_run_config
from .errors import RunError, RunTimeout
from .install import sha256_file
from .lua import write_lua_config

Launcher = Union[PathLike, Sequence[str]]

DEFAULT_ENV_VAR = "BIZHAWK_RUN_CONFIG"
LOG_LIMIT = 16 * 1024 * 1024


def _launcher_argv(launcher: Launcher) -> List[str]:
    if isinstance(launcher, (str, Path)):
        return [str(launcher)]
    argv = [str(part) for part in launcher]
    if not argv:
        raise ValueError("the launcher is empty")
    return argv


def build_command(
    launcher: Launcher,
    rom: PathLike,
    *,
    lua: Optional[PathLike] = None,
    config: Optional[PathLike] = None,
    chromeless: bool = False,
    extra: Sequence[str] = (),
) -> List[str]:
    """The command line: the launcher (and `--gdi` after `EmuHawkMono.sh`, which BizHawk's Mono build needs), `--config=`,
    `--lua=`, `--chromeless`, any `extra` arguments, then the ROM last. `launcher` is a path or a command prefix (a list)."""
    argv = _launcher_argv(launcher)
    if Path(argv[-1]).name == "EmuHawkMono.sh":
        argv.append("--gdi")
    if config is not None:
        argv.append(f"--config={config}")
    if lua is not None:
        argv.append(f"--lua={lua}")
    if chromeless:
        argv.append("--chromeless")
    argv.extend(extra)
    argv.append(str(rom))
    return argv


@dataclass
class RunResult:
    argv: List[str]
    pid: int
    out: str
    returncode: Optional[int]
    seconds: float
    finished: bool
    """True when the script wrote `.done`."""
    stopped: bool
    """True when this package had to stop the process (it did not exit on its own after `.done`)."""
    rom_sha256: str
    config: Optional[str]
    cfg_lua: str
    log: Optional[str]
    muted: bool
    background: bool
    warnings: List[str] = field(default_factory=list)

    def to_dict(self) -> Dict[str, Any]:
        return dict(self.__dict__)


def write_evidence(result: RunResult, path: PathLike) -> Path:
    """The run as JSON: the command line, the ROM's SHA-256, the exit code, the files, what was and was not muted."""
    target = Path(path)
    target.write_text(json.dumps(result.to_dict(), indent=2) + "\n", encoding="utf-8")
    return target


def _suffix_paths(out: Path) -> Tuple[Path, Path]:
    return Path(f"{out}.done"), Path(f"{out}.err")


class Run:
    """A started BizHawk. Use `start_run`; call `wait`, or `stop` to give up. It is also a context manager that stops the
    process on the way out (a no-op once it has exited)."""

    def __init__(
        self,
        process: "subprocess.Popen[bytes]",
        argv: List[str],
        out: Path,
        cfg_lua: Path,
        config: Optional[Path],
        log: Optional[Path],
        rom: Path,
        rom_sha256: str,
        keep_config: bool,
        muted: bool,
        background: bool,
        warnings: List[str],
        reader: Optional[threading.Thread],
        tail: "Deque[str]",
        posix_group: bool,
    ) -> None:
        self.process, self.argv, self.out, self.cfg_lua = process, argv, out, cfg_lua
        self.config, self.log, self.rom, self.rom_sha256 = config, log, rom, rom_sha256
        self.keep_config, self.muted, self.background, self.warnings = keep_config, muted, background, warnings
        self._reader, self._tail, self._posix_group = reader, tail, posix_group
        self.started = time.monotonic()
        self.stopped = False

    @property
    def pid(self) -> int:
        return self.process.pid

    def tail(self, n: int = 20) -> List[str]:
        """The last lines the emulator printed (up to 200 are kept)."""
        return list(self._tail)[-n:]

    def stop(self, grace: float = 2.0) -> Optional[int]:
        """Stop the process this run started and nothing else: SIGTERM (or terminate on Windows), then SIGKILL after `grace`
        seconds. On POSIX the process leads a group of its own, so a launcher script's child dies with it. Returns the exit code."""
        if self.process.poll() is None:
            self.stopped = True
            self._signal(signal.SIGTERM if os.name != "nt" else None)
            try:
                self.process.wait(timeout=grace)
            except subprocess.TimeoutExpired:
                self._signal(signal.SIGKILL if os.name != "nt" else None, kill=True)
                self.process.wait()
        if self._reader is not None:
            self._reader.join(timeout=2)
        return self.process.returncode

    def _signal(self, sig: Optional[int], kill: bool = False) -> None:
        try:
            if os.name == "nt":
                self.process.kill() if kill else self.process.terminate()
            elif self._posix_group:
                os.killpg(self.process.pid, sig)  # type: ignore[arg-type]
            else:
                self.process.send_signal(sig)  # type: ignore[arg-type]
        except (ProcessLookupError, PermissionError):
            pass

    def __enter__(self) -> "Run":
        return self

    def __exit__(self, *exc: object) -> None:
        self.stop()

    def wait(self, timeout: float = 900, *, poll: float = 0.25, exit_grace: float = 5.0) -> RunResult:
        """Wait for `<out>.done` or `<out>.err`.

        `.err` (checked first) raises `RunError` carrying its text. A process that exits with neither file raises `RunError`
        with its exit code and the last lines it printed. Nothing within `timeout` seconds stops the process and raises
        `RunTimeout`. After `.done` the process gets `exit_grace` seconds to exit by itself (BizHawk releases its files as it
        does) and is stopped if it does not. The ROM's SHA-256 must be what it was at the start, or `RunError`."""
        done, err = _suffix_paths(self.out)
        deadline = time.monotonic() + timeout
        while True:
            if err.exists():
                text = err.read_text(encoding="utf-8", errors="replace")
                self.stop()
                raise RunError(f"the script failed: {text.strip()[:500]}", returncode=self.process.returncode, error_text=text)
            if done.exists():
                break
            code = self.process.poll()
            if code is not None:
                time.sleep(min(poll, 0.2))  # a script's last write can land just before the process exits
                if err.exists() or done.exists():
                    continue
                self.stop()
                raise RunError(
                    f"EmuHawk exited with code {code} before the script wrote {done.name} or {err.name}"
                    + (f"; it printed: {' | '.join(self.tail(5))}" if self._tail else ""),
                    returncode=code,
                )
            if time.monotonic() >= deadline:
                self.stop()
                raise RunTimeout(f"no {done.name} or {err.name} after {timeout} seconds; stopped pid {self.pid}", returncode=self.process.returncode)
            time.sleep(poll)
        try:
            self.process.wait(timeout=exit_grace)
        except subprocess.TimeoutExpired:
            self.stop()
        else:
            if self._reader is not None:
                self._reader.join(timeout=2)
        if sha256_file(self.rom) != self.rom_sha256:
            raise RunError(f"{self.rom.name} changed while the emulator ran")
        if self.config is not None and not self.keep_config:
            self.config.unlink(missing_ok=True)
        return RunResult(
            argv=self.argv, pid=self.pid, out=str(self.out), returncode=self.process.returncode,
            seconds=round(time.monotonic() - self.started, 2), finished=True, stopped=self.stopped, rom_sha256=self.rom_sha256,
            config=str(self.config) if self.config is not None and self.config.exists() else None, cfg_lua=str(self.cfg_lua),
            log=str(self.log) if self.log is not None else None, muted=self.muted, background=self.background, warnings=list(self.warnings),
        )


def _stale_files(out: Path) -> List[Path]:
    if not out.parent.is_dir():
        return []
    return sorted(p for p in out.parent.glob(out.name + "*") if p.is_file())


def _pump(stream: IO[bytes], log: Path, limit: int, tail: "Deque[str]") -> None:
    written = 0
    with stream, log.open("wb") as sink:
        for raw in iter(stream.readline, b""):
            tail.append(raw.decode("utf-8", errors="replace").rstrip("\r\n")[:500])
            if written < limit:
                sink.write(raw[: limit - written])
                written += len(raw)


def start_run(
    launcher: Launcher,
    rom: PathLike,
    out: PathLike,
    *,
    lua: PathLike,
    settings: Optional[Mapping[str, Any]] = None,
    base_config: Optional[PathLike] = None,
    config_edits: Iterable[Tuple[Sequence[str], Any]] = (),
    cores: Optional[Mapping[str, str]] = None,
    sound: bool = False,
    background: bool = True,
    create_config_paths: bool = False,
    keep_config: bool = False,
    env_var: str = DEFAULT_ENV_VAR,
    env: Optional[Mapping[str, Optional[str]]] = None,
    cwd: Optional[PathLike] = None,
    extra_args: Sequence[str] = (),
    chromeless: bool = False,
    stale: str = "error",
    log_limit: int = LOG_LIMIT,
    windows: Optional[bool] = None,
) -> Run:
    """Start one BizHawk run.

    `out` is the prefix of everything the run writes (`<out>.done`, `<out>_*.png`, ...). `settings` is the table the Lua script
    reads, written to `<out>.cfg.lua` as `return { out = "<out>", ... }` (an `out` key of yours wins) and named in the environment
    variable `env_var`. With `base_config`, a private copy goes to `<out>.bizhawk.ini` (sound off unless `sound`, running in the
    background unless `background` is false, `cores` preferred, `config_edits` applied) and BizHawk gets `--config=` it; the base file
    is never edited. Without it BizHawk uses its own config and the run says in `warnings` that sound and background were not set.

    `stale` decides what to do when files starting with the prefix already exist (an earlier run's `.done`, dumps that are written only
    when memory changes and would be read as this run's): `"error"` (the default) raises `FileExistsError`, `"remove"` deletes them,
    `"keep"` leaves them. The process gets a group of its own on POSIX, no focus on Windows, and its output goes to
    `<out>.emuhawk.log` (at most `log_limit` bytes)."""
    if stale not in ("error", "remove", "keep"):
        raise ValueError("stale is 'error', 'remove' or 'keep'")
    rom_path = Path(rom).resolve()
    if not rom_path.is_file():
        raise FileNotFoundError(f"ROM does not exist: {rom_path}")
    out_path = Path(out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    old = _stale_files(out_path)
    if old and stale == "error":
        raise FileExistsError(
            f"{len(old)} files from an earlier run under {out_path}*, e.g. {old[0].name}; use another prefix, an empty folder, or stale='remove'"
        )
    if old and stale == "remove":
        for p in old:
            p.unlink()
    rom_hash = sha256_file(rom_path)
    warnings: List[str] = []
    config: Optional[Path] = None
    if base_config is not None:
        config = make_run_config(
            base_config, Path(f"{out_path}.bizhawk.ini"), edits=config_edits, sound=sound, background=background, cores=cores,
            create=create_config_paths,
        )
    else:
        warnings.append("no base config: BizHawk's own config is used, so sound and background running were not set")
    cfg_table: Dict[str, Any] = {"out": out_path}  # a path: written with forward slashes, which BizHawk's Lua takes on Windows
    cfg_table.update(settings or {})
    cfg_lua = write_lua_config(Path(f"{out_path}.cfg.lua"), cfg_table)
    argv = build_command(launcher, rom_path, lua=Path(lua).resolve(), config=config, chromeless=chromeless, extra=extra_args)
    environment: Dict[str, str] = dict(os.environ)
    for key, value in (env or {}).items():
        if value is None:
            environment.pop(key, None)
        else:
            environment[key] = value
    environment[env_var] = str(cfg_lua.resolve())
    win = (os.name == "nt") if windows is None else windows
    kwargs: Dict[str, Any] = {}
    if win:
        startup = subprocess.STARTUPINFO()  # type: ignore[attr-defined]
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW  # type: ignore[attr-defined]
        startup.wShowWindow = 4  # SW_SHOWNOACTIVATE: show the window without taking focus
        kwargs["startupinfo"] = startup
        warnings.append("the window is shown without activation, but Windows may still give it focus when it first appears")
    else:
        kwargs["start_new_session"] = True
    log_path = Path(f"{out_path}.emuhawk.log")
    process = subprocess.Popen(
        argv, cwd=str(cwd) if cwd is not None else None, env=environment, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, **kwargs,
    )
    tail: Deque[str] = collections.deque(maxlen=200)
    reader = threading.Thread(target=_pump, args=(process.stdout, log_path, log_limit, tail), daemon=True)
    reader.start()
    return Run(
        process, argv, out_path, cfg_lua, config, log_path, rom_path, rom_hash, keep_config,
        muted=config is not None and not sound, background=config is not None and background, warnings=warnings,
        reader=reader, tail=tail, posix_group=not win,
    )


def run_capture(
    launcher: Launcher,
    rom: PathLike,
    out: PathLike,
    *,
    lua: PathLike,
    timeout: float = 900,
    exit_grace: float = 5.0,
    evidence: bool = True,
    **options: Any,
) -> RunResult:
    """`start_run`, `wait`, and `<out>.run.json` as evidence (unless `evidence` is false). The process is stopped on any failure."""
    run = start_run(launcher, rom, out, lua=lua, **options)
    try:
        result = run.wait(timeout, exit_grace=exit_grace)
    except BaseException:
        run.stop()
        raise
    if evidence:
        write_evidence(result, f"{Path(out)}.run.json")
    return result


def wait_for_done(out: PathLike, timeout: float = 900, *, poll: float = 0.5, process: "Optional[subprocess.Popen[Any]]" = None) -> Path:
    """For a caller that starts BizHawk its own way: wait for `<out>.done` or `<out>.err`. Returns `<out>`.

    `.err` raises `RunError` with its text; `timeout` seconds raise `RunTimeout`. With `process` (a `Popen` the caller owns),
    an exit with neither file raises `RunError` at once instead of waiting out the timeout. Nothing is stopped here."""
    base = Path(out)
    done, err = _suffix_paths(base)
    deadline = time.monotonic() + timeout
    while True:
        if err.exists():
            text = err.read_text(encoding="utf-8", errors="replace")
            raise RunError(f"the script failed: {text.strip()[:500]}", error_text=text)
        if done.exists():
            return base
        if process is not None and process.poll() is not None:
            time.sleep(min(poll, 0.2))
            if err.exists() or done.exists():
                continue
            raise RunError(f"the process exited with code {process.returncode} before {done.name} or {err.name}", returncode=process.returncode)
        if time.monotonic() >= deadline:
            raise RunTimeout(f"no {done.name} or {err.name} after {timeout} seconds")
        time.sleep(poll)
