"""The errors this package raises. All of them are `ProofError`, which is a `ValueError`."""

from __future__ import annotations


class ProofError(ValueError):
    """A file or an argument that cannot be used. The message says which and why."""


class PpmError(ProofError):
    """A PPM frame that is not a complete binary (P6) 8-bit image."""


class WavError(ProofError):
    """A WAV file that is not 16-bit PCM, or has no format or data."""


class PressError(ProofError):
    """A press-file line or button name that cannot be read."""


class DebugError(ProofError, ConnectionError):
    """The debug server did not answer."""
