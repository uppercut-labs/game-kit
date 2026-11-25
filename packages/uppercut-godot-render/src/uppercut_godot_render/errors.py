"""The one error this package raises."""

from __future__ import annotations


class RenderError(RuntimeError):
    """A render step failed; the message names what and why."""
