"""The errors this package raises on purpose."""

from __future__ import annotations


class MatchError(Exception):
    """Base class."""


class ElfError(MatchError):
    """A file that is not a 32-bit little-endian ELF, or whose tables run past its end."""


class ConfigError(MatchError):
    """A configuration file that is missing a field, or holds a wrong one."""


class FetchError(MatchError):
    """A download whose SHA-256 is not the pinned one, an archive with an unsafe member, or a file that is not as pinned."""
