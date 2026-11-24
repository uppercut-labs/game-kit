"""The errors this package raises."""

from __future__ import annotations


class IsoError(ValueError):
    """The image is not a readable ISO 9660 volume, or something in it is malformed or out of bounds."""


class IsoNotFound(IsoError, FileNotFoundError):
    """A path is not on the disc."""
