"""Fetch the toolchain a matching build needs, checked by SHA-256. Nothing is vendored here, and no address or hash is built in.

A toolchain is a JSON file you keep with your project:

```json
{
  "packages": [
    {"name": "compiler", "url": "https://example.invalid/compiler.tar.xz", "sha256": "<archive digest>",
     "files": {"lib/cc1": "<digest of the file the build runs>", "bin/as": "<digest>"}},
    {"name": "binutils", "hosts": {"linux-x86-64": {"url": "https://example.invalid/b-linux.tar.gz", "sha256": "<digest>"},
                                  "macos-arm64": {"url": "https://example.invalid/b-mac.tar.gz", "sha256": "<digest>"}}}
  ]
}
```

Each archive is checked by its SHA-256 before it is unpacked, each file named in `files` is checked after, and a package is moved
into place only when both pass: a mismatch stops the fetch and leaves nothing of that package behind. An archive that holds a link or
a path outside its folder is refused. A package already in `dest` is checked (its `files`) and left alone.
"""

from __future__ import annotations

import hashlib
import io
import json
import shutil
import sys
import tarfile
import tempfile
import urllib.request
import zipfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Dict, List, Mapping, Optional, Sequence, Union

from .errors import ConfigError, FetchError

PathLike = Union[str, Path]
Download = Callable[[str], bytes]


@dataclass
class Package:
    name: str
    url: str
    sha256: str
    files: Dict[str, str] = field(default_factory=dict)
    """Path inside the package to the SHA-256 of that file."""
    strip: Optional[str] = None
    """A path that must exist in the archive; the package root is the unpack folder or its one subfolder holding it. Defaults to the first of `files`."""


def default_host() -> Optional[str]:
    import platform

    machine = platform.machine().lower()
    if sys.platform == "darwin":
        return "macos-arm64" if machine in ("arm64", "aarch64") else "macos-x86-64"
    if sys.platform == "win32":
        return "windows-x86-64"
    return "linux-x86-64" if machine in ("x86_64", "amd64") else None


def load_packages(config: Union[PathLike, Mapping[str, object]], host: Optional[str] = None) -> List[Package]:
    """Read a toolchain JSON (a path or the parsed object) into packages, choosing each host-specific entry for `host`."""
    data = json.loads(Path(config).read_text(encoding="utf-8")) if isinstance(config, (str, Path)) else dict(config)
    raw = data.get("packages")
    if not isinstance(raw, list) or not raw:
        raise ConfigError('the toolchain file needs a non-empty "packages" list')
    out: List[Package] = []
    for entry in raw:
        if not isinstance(entry, dict) or "name" not in entry:
            raise ConfigError("every package needs a name")
        name = str(entry["name"])
        spec = dict(entry)
        if "hosts" in entry:
            hosts = entry["hosts"]
            if host is None or host not in hosts:
                raise ConfigError(f"package {name} has no build for host {host!r} (it has {sorted(hosts)})")
            spec.update(hosts[host])
        for key in ("url", "sha256"):
            if key not in spec:
                raise ConfigError(f"package {name} has no {key}")
        digest = str(spec["sha256"]).lower()
        if len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
            raise ConfigError(f"package {name}: sha256 is 64 hex digits")
        files = {str(k): str(v).lower() for k, v in dict(spec.get("files", {})).items()}
        out.append(Package(name, str(spec["url"]), digest, files, spec.get("strip")))
    return out


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def unpack(data: bytes, filename: str, dest: Path) -> None:
    """Unpack a `.zip`, `.tar.*` or `.tgz` into the new folder `dest`, refusing links and paths outside it."""
    dest.mkdir(parents=True)
    root = dest.resolve()
    if filename.endswith(".zip"):
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            for m in z.infolist():
                if not (root / m.filename).resolve().is_relative_to(root):
                    raise FetchError(f"{filename}: unsafe path {m.filename}")
            z.extractall(root)
        return
    with tarfile.open(fileobj=io.BytesIO(data)) as t:
        for member in t.getmembers():
            if member.issym() or member.islnk() or not (root / member.name).resolve().is_relative_to(root):
                raise FetchError(f"{filename}: unsafe member {member.name}")
        if hasattr(tarfile, "data_filter"):
            t.extractall(root, filter="data")
        else:  # a Python before the extraction filters: the members were checked above
            t.extractall(root)  # noqa: S202


def package_root(unpacked: Path, expect: str) -> Path:
    """The folder that holds `expect`: the unpack folder itself, or its single subfolder."""
    if (unpacked / expect).exists():
        return unpacked
    subs = [p for p in unpacked.iterdir() if p.is_dir()]
    if len(subs) == 1 and (subs[0] / expect).exists():
        return subs[0]
    raise FetchError(f"{unpacked.name}: {expect} not found in the archive")


def check_files(root: Path, files: Mapping[str, str], label: str) -> None:
    """Every file in `files` must exist under `root` with its pinned SHA-256."""
    for rel, want in files.items():
        path = root / rel
        if not path.is_file():
            raise FetchError(f"{label}: {rel} is missing")
        got = sha256_bytes(path.read_bytes())
        if got != want:
            raise FetchError(f"{label}: {rel} has SHA-256 {got}, expected {want}")


def _download(url: str) -> bytes:
    with urllib.request.urlopen(url) as r:  # noqa: S310 - the caller's pinned URL; the digest is checked before anything is unpacked
        return r.read()  # type: ignore[no-any-return]


def fetch_toolchain(packages: Sequence[Package], dest: PathLike, *, dry_run: bool = False, download: Optional[Download] = None,
                    log: Callable[[str], None] = print) -> List[str]:
    """Fetch and check each package into `dest/<name>`. Returns the names it installed (not those already present).

    `dry_run` prints the plan and downloads nothing. `download(url) -> bytes` is a stand-in for the default `urllib` download."""
    root = Path(dest).resolve()
    for p in packages:
        state = "present" if (root / p.name).exists() else "fetch"
        log(f"{p.name:24} {state:8} {p.url}")
    if dry_run:
        return []
    root.mkdir(parents=True, exist_ok=True)
    installed: List[str] = []
    for p in packages:
        final = root / p.name
        if final.exists():
            if p.files:
                check_files(final, p.files, p.name)
            log(f"{p.name}: present")
            continue
        log(f"  GET {p.url}")
        data = (download or _download)(p.url)
        digest = sha256_bytes(data)
        if digest != p.sha256:
            raise FetchError(f"{p.name}: archive SHA-256 {digest}, expected {p.sha256}")
        with tempfile.TemporaryDirectory(dir=root) as tmp:
            unpacked = Path(tmp) / "unpack"
            unpack(data, p.url.rsplit("/", 1)[-1], unpacked)
            expect = p.strip or next(iter(p.files), ".")
            pkg_root = package_root(unpacked, expect)
            check_files(pkg_root, p.files, p.name)
            shutil.move(str(pkg_root), final)
        installed.append(p.name)
        log(f"{p.name}: ok (archive SHA-256 {digest})")
    return installed
