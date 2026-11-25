"""The one error this package raises."""

from __future__ import annotations


class SmokeError(RuntimeError):
    """A smoke run could not start, failed its Lua checks, or produced screenshots that do not validate."""
