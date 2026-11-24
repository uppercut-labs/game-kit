"""A pinned, checked BizHawk install, and finding the program inside one."""

from __future__ import annotations

import hashlib
import os
import shutil
import tempfile
import urllib.request
import zipfile
from pathlib import Path
from typing import Callable, Optional, Sequence, Union

from .errors import InstallError

PathLike = Union[str, Path]
Fetch = Callable[[str, Path], None]
"""`fetch(url, destination_file)`: downloads one file. The default is `urllib.request.urlretrieve`."""


def sha256_file(path: PathLike) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _default_fetch(url: str, destination: Path) -> None:
    urllib.request.urlretrieve(url, destination)  # noqa: S310 - the caller names the URL, and the SHA-256 is checked after


def safe_extract(archive: PathLike, destination: PathLike) -> None:
    """Unpack a zip into `destination`, refusing a member that would land outside it."""
    root = Path(destination).resolve()
    with zipfile.ZipFile(archive) as package:
        for member in package.infolist():
            target = (root / member.filename).resolve()
            if target != root and root not in target.parents:
                raise InstallError(f"{Path(archive).name} holds a path outside its folder: {member.filename}")
        package.extractall(root)


def install_bizhawk(
    destination: PathLike,
    url: str,
    sha256: str,
    *,
    replace: bool = False,
    fetch: Optional[Fetch] = None,
) -> Path:
    """Download a BizHawk release zip, check its SHA-256 against the pin, and unpack it at `destination`.

    The URL and the digest are the caller's: this package carries no release's address or hash. The archive is unpacked in a
    scratch folder next to `destination` and moved into place only after the check, so a bad download leaves nothing behind.
    A `destination` that already exists is an `InstallError` unless `replace` is true (then it is removed first, and only
    after the new copy has been checked and unpacked)."""
    if len(sha256) != 64 or any(c not in "0123456789abcdefABCDEF" for c in sha256):
        raise InstallError("the pin is a SHA-256 of 64 hex digits")
    target = Path(destination).resolve()
    if target.exists() and not replace:
        raise InstallError(f"{target} already exists (pass replace=True to put a new copy in its place)")
    target.parent.mkdir(parents=True, exist_ok=True)
    name = url.rsplit("/", 1)[-1] or "bizhawk.zip"
    with tempfile.TemporaryDirectory(prefix=".bizhawk-install-", dir=target.parent) as scratch:
        archive = Path(scratch) / name
        (fetch or _default_fetch)(url, archive)
        actual = sha256_file(archive)
        if actual.lower() != sha256.lower():
            raise InstallError(f"checksum mismatch for {name}: expected {sha256.lower()}, got {actual}")
        staging = Path(scratch) / "unpacked"
        staging.mkdir()
        try:
            safe_extract(archive, staging)
        except zipfile.BadZipFile as error:
            raise InstallError(f"{name} is not a zip file: {error}") from error
        if target.exists():
            shutil.rmtree(target)
        shutil.move(str(staging), str(target))
    return target


def launcher_names(windows: Optional[bool] = None) -> Sequence[str]:
    win = (os.name == "nt") if windows is None else windows
    return ("EmuHawk.exe", "EmuHawkMono.sh") if win else ("EmuHawkMono.sh", "EmuHawk.exe")


def find_emuhawk(root: PathLike, *, windows: Optional[bool] = None) -> Path:
    """The BizHawk launcher under `root`: `EmuHawk.exe` first on Windows, `EmuHawkMono.sh` first elsewhere, in `root` itself
    and then in any folder below it. Raises `FileNotFoundError` when there is none."""
    base = Path(root)
    names = launcher_names(windows)
    candidates = [base / name for name in names]
    candidates.extend(candidate for name in names for candidate in sorted(base.rglob(name)))
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise FileNotFoundError(f"No supported EmuHawk launcher found under {base}")


def validate_install(root: PathLike) -> "tuple[Path, Path]":
    """`(launcher, lua_folder)` of an install, or `FileNotFoundError` when the launcher or its `Lua` folder is missing."""
    executable = find_emuhawk(Path(root).resolve())
    lua_root = executable.parent / "Lua"
    if not lua_root.is_dir():
        raise FileNotFoundError(f"No bundled Lua resources found at {lua_root}")
    return executable, lua_root
