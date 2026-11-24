"""A client for PCSX2's PINE socket: read and write PlayStation 2 memory, batch reads, ask for the title, status,
game id and version, and save and load states. Standard library only."""

from .client import RANGE_LIMIT, PineBatch, PineClient, WriteAllowlist
from .errors import (
    PineCommandFailed,
    PineConnectionError,
    PineError,
    PineProtocolError,
    PineTimeoutError,
    PineWriteDenied,
)
from .protocol import DEFAULT_SLOT, MAX_REPLY_SIZE, MAX_REQUEST_SIZE, PineStatus, signed
from .transport import TcpTransport, Transport, UnixTransport, default_socket_path

__all__ = [
    "DEFAULT_SLOT",
    "MAX_REPLY_SIZE",
    "MAX_REQUEST_SIZE",
    "RANGE_LIMIT",
    "PineBatch",
    "PineClient",
    "PineCommandFailed",
    "PineConnectionError",
    "PineError",
    "PineProtocolError",
    "PineStatus",
    "PineTimeoutError",
    "PineWriteDenied",
    "TcpTransport",
    "Transport",
    "UnixTransport",
    "WriteAllowlist",
    "default_socket_path",
    "signed",
]
