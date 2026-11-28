"""Fetch build inputs pinned by SHA-256 into a cache, refuse a mismatch, and reuse an intact cache offline."""

from ._sources import (
    DownloadError,
    FetchResult,
    PinnedSource,
    PinnedSourceError,
    describe,
    fetch_manifest,
    fetch_source,
    fetch_sources,
    parse_manifest,
    read_manifest,
    sha256_file,
    url_download,
)
from .cli import main

__all__ = [
    "DownloadError",
    "FetchResult",
    "PinnedSource",
    "PinnedSourceError",
    "describe",
    "fetch_manifest",
    "fetch_source",
    "fetch_sources",
    "main",
    "parse_manifest",
    "read_manifest",
    "sha256_file",
    "url_download",
]
__version__ = "1.0.0"
