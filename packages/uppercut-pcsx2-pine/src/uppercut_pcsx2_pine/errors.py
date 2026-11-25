"""The errors this package raises. Every one is a ``PineError``."""

from __future__ import annotations


class PineError(RuntimeError):
    """Base class for everything this package raises on purpose."""


class PineConnectionError(PineError, ConnectionError):
    """The socket is missing, refused the connection, or closed before a whole reply arrived.

    It is also a ``ConnectionError`` (so an ``OSError``), which is what callers that wrote ``except OSError`` expect.
    """


class PineTimeoutError(PineConnectionError, TimeoutError):
    """The emulator did not answer within the timeout."""


class PineProtocolError(PineError):
    """The reply is not what PINE sends: a bad length, an unknown result byte, a payload of the wrong size."""


class PineCommandFailed(PineError):
    """PCSX2 answered with its failure byte (0xFF).

    PCSX2 does this for every memory, state and string command while no game is running, and when a message is
    too large. ``status()`` never fails this way: it answers "shutdown" with no game.
    """


class PineWriteDenied(PineError, PermissionError):
    """A write fell outside the client's write allowlist. Nothing was sent."""
