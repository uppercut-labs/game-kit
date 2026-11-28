"""Fetch build inputs pinned by SHA-256 into a cache, and reuse the cache without the network."""

from __future__ import annotations

import hashlib
import http.client
import os
import re
import secrets
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable, Optional
from urllib.parse import urljoin
from urllib.request import Request, urlopen

DEFAULT_USER_AGENT = "uppercut-pinned-sources/1.0"
DEFAULT_TIMEOUT = 120.0
_DIGEST = re.compile(r"^[0-9a-f]{64}$")
_URL = re.compile(r"^[A-Za-z][A-Za-z0-9+.-]*://")

Download = Callable[[str], bytes]


class PinnedSourceError(ValueError):
    """Raised for a manifest that cannot be read, a file whose SHA-256 differs from its pin, or a file missing offline."""


class DownloadError(OSError):
    """Raised when a download fails. The cache is left as it was."""


@dataclass(frozen=True)
class PinnedSource:
    """One manifest line: the expected SHA-256 (64 lowercase hex digits), the file name in the cache, and where to
    download it from: a full URL, or a path relative to the base URL."""

    digest: str
    name: str
    location: str

    def url(self, base_url: Optional[str] = None) -> str:
        """The download URL: ``location`` when it is a full URL, else ``location`` joined to ``base_url``."""
        if _URL.match(self.location):
            return self.location
        if not base_url:
            raise PinnedSourceError(f"{self.name}: {self.location!r} is relative and no base URL was given")
        return urljoin(base_url if base_url.endswith("/") else base_url + "/", self.location)


@dataclass(frozen=True)
class FetchResult:
    """What happened to one source: ``action`` is ``"cached"`` (an intact copy was already there) or
    ``"downloaded"``. ``url`` is the address it came from, or ``None`` when it came from the cache."""

    source: PinnedSource
    path: Path
    action: str
    size: int
    url: Optional[str] = None


def _check_name(name: str, where: str) -> None:
    if name in ("", ".", "..") or "/" in name or "\\" in name or "\0" in name:
        raise PinnedSourceError(f"{where}: {name!r} must be a plain file name")


def parse_manifest(text: str, *, origin: str = "manifest") -> list[PinnedSource]:
    """Read manifest text: one source per line as ``SHA256 NAME LOCATION``, separated by spaces or tabs.

    Blank lines and lines starting with ``#`` are skipped. A name may appear only once.
    """
    sources: list[PinnedSource] = []
    names: set[str] = set()
    for number, line in enumerate(text.splitlines(), 1):
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        where = f"{origin}:{number}"
        fields = stripped.split()
        if len(fields) != 3:
            raise PinnedSourceError(f"{where}: expected SHA256 NAME LOCATION, found {len(fields)} fields")
        digest, name, location = fields
        if not _DIGEST.match(digest):
            raise PinnedSourceError(f"{where}: {digest!r} is not a lowercase SHA-256")
        _check_name(name, where)
        if name in names:
            raise PinnedSourceError(f"{where}: {name} is listed twice")
        names.add(name)
        sources.append(PinnedSource(digest, name, location))
    return sources


def read_manifest(path: "Path | str") -> list[PinnedSource]:
    """Read a manifest file (see ``parse_manifest``)."""
    path = Path(path)
    return parse_manifest(path.read_text(encoding="utf-8"), origin=str(path))


def sha256_file(path: "Path | str") -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def url_download(*, user_agent: str = DEFAULT_USER_AGENT, timeout: float = DEFAULT_TIMEOUT) -> Download:
    """A download function over ``urllib``: one GET with this ``User-Agent`` and timeout. ``file://`` URLs work too."""

    def download(url: str) -> bytes:
        request = Request(url, headers={"User-Agent": user_agent})
        try:
            with urlopen(request, timeout=timeout) as response:
                return response.read()
        except (OSError, http.client.HTTPException, ValueError) as error:  # URLError and HTTPError are OSErrors
            raise DownloadError(f"{url}: {getattr(error, 'reason', None) or error}") from error

    return download


def _write_atomically(path: Path, data: bytes) -> None:
    temporary = path.with_name(f".{path.name}.{secrets.token_hex(4)}.part")
    try:
        with open(temporary, "xb") as stream:
            stream.write(data)
        os.replace(temporary, path)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise


def fetch_source(
    source: PinnedSource,
    cache: "Path | str",
    *,
    base_url: Optional[str] = None,
    download: Optional[Download] = None,
    offline: bool = False,
) -> FetchResult:
    """Make sure ``cache/source.name`` holds the pinned file, and return what was done.

    A file already in the cache is hashed and kept when it matches; a file that does not match is refused and left
    alone, so a bad cache is never silently replaced. A missing file is downloaded, hashed, and written into place
    (through a temporary file, so a failed download leaves nothing behind) only when it matches. With ``offline``,
    a missing file is an error instead of a download.
    """
    cache = Path(cache)
    path = cache / source.name
    if path.exists():
        if not path.is_file():
            raise PinnedSourceError(f"{path}: expected a file")
        if sha256_file(path) != source.digest:
            raise PinnedSourceError(f"{path}: the cached file does not match its SHA-256 {source.digest}")
        return FetchResult(source, path, "cached", path.stat().st_size)
    if offline:
        raise PinnedSourceError(f"{path}: missing from the cache, and downloads are off")
    url = source.url(base_url)
    data = (download or url_download())(url)
    actual = hashlib.sha256(data).hexdigest()
    if actual != source.digest:
        raise PinnedSourceError(f"{source.name}: the download from {url} has SHA-256 {actual}, expected {source.digest}")
    cache.mkdir(parents=True, exist_ok=True)
    _write_atomically(path, data)
    return FetchResult(source, path, "downloaded", len(data), url)


def fetch_sources(
    sources: Iterable[PinnedSource],
    cache: "Path | str",
    *,
    base_url: Optional[str] = None,
    user_agent: str = DEFAULT_USER_AGENT,
    timeout: float = DEFAULT_TIMEOUT,
    download: Optional[Download] = None,
    offline: bool = False,
    report: Optional[Callable[[FetchResult], None]] = None,
) -> list[FetchResult]:
    """Fetch every source in order with ``fetch_source``, stopping at the first error.

    ``download`` replaces the ``urllib`` download (``user_agent`` and ``timeout`` then do nothing). ``report`` is
    called with each result as it is made.
    """
    cache = Path(cache)
    cache.mkdir(parents=True, exist_ok=True)
    getter = download or url_download(user_agent=user_agent, timeout=timeout)
    results = []
    for source in sources:
        result = fetch_source(source, cache, base_url=base_url, download=getter, offline=offline)
        if report is not None:
            report(result)
        results.append(result)
    return results


def fetch_manifest(manifest: "Path | str", cache: "Path | str", **options: object) -> list[FetchResult]:
    """Read the manifest file and fetch its sources into ``cache``. Takes the keyword options of ``fetch_sources``."""
    return fetch_sources(read_manifest(manifest), cache, **options)  # type: ignore[arg-type]


def describe(result: FetchResult) -> str:
    """One line for a log: ``downloaded NAME (SIZE bytes) from URL`` or ``cached NAME (SIZE bytes)``."""
    if result.action == "downloaded":
        return f"downloaded {result.source.name} ({result.size} bytes) from {result.url}"
    return f"cached {result.source.name} ({result.size} bytes)"
