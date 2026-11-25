"""How a request reaches PCSX2: a Unix socket (Linux and macOS) or a TCP port on 127.0.0.1 (Windows)."""

from __future__ import annotations

import abc
import os
import socket
import struct
import sys
import threading
from pathlib import Path
from typing import Mapping, Optional, Union

from .errors import PineConnectionError, PineTimeoutError
from .protocol import DEFAULT_SLOT, check_reply_length

SOCKET_NAME = "pcsx2.sock"


def default_socket_path(
    slot: int = DEFAULT_SLOT,
    *,
    environ: Optional[Mapping[str, str]] = None,
    platform: Optional[str] = None,
) -> Path:
    """Where PCSX2 puts its PINE socket for ``slot``.

    PCSX2 uses ``$TMPDIR`` on macOS and ``$XDG_RUNTIME_DIR`` elsewhere, falling back to ``/tmp``, names the file
    ``pcsx2.sock``, and appends ``.<slot>`` when the slot is not the default 28011.
    """
    env = os.environ if environ is None else environ
    key = "TMPDIR" if (platform or sys.platform) == "darwin" else "XDG_RUNTIME_DIR"
    directory = env.get(key) or "/tmp"
    name = SOCKET_NAME if slot == DEFAULT_SLOT else f"{SOCKET_NAME}.{slot}"
    return Path(directory) / name


class Transport(abc.ABC):
    """Sends one framed request and returns the reply body (the bytes after the length field)."""

    @abc.abstractmethod
    def request(self, message: bytes) -> bytes: ...

    @abc.abstractmethod
    def close(self) -> None: ...

    def __enter__(self) -> "Transport":
        return self

    def __exit__(self, *_exc: object) -> None:
        self.close()


class _SocketTransport(Transport):
    """Shared logic: connect lazily, keep the connection (or not), read exactly one reply, map socket errors."""

    def __init__(self, *, timeout: float, persistent: bool) -> None:
        self.timeout = timeout
        self.persistent = persistent
        self._socket: Optional[socket.socket] = None
        self._lock = threading.Lock()

    @abc.abstractmethod
    def _open(self) -> socket.socket: ...

    @abc.abstractmethod
    def _describe(self) -> str: ...

    def _recv_exact(self, connection: socket.socket, size: int) -> bytes:
        chunks = bytearray()
        while len(chunks) < size:
            chunk = connection.recv(size - len(chunks))
            if not chunk:
                raise PineConnectionError("PINE connection closed before a complete reply")
            chunks.extend(chunk)
        return bytes(chunks)

    def request(self, message: bytes) -> bytes:
        with self._lock:
            try:
                connection = self._socket if self._socket is not None else self._open()
                if self.persistent:
                    self._socket = connection
                connection.sendall(message)
                (total,) = struct.unpack("<I", self._recv_exact(connection, 4))
                check_reply_length(total)
                return self._recv_exact(connection, total - 4)
            except socket.timeout as error:
                self._drop()
                raise PineTimeoutError(f"PCSX2 did not answer within {self.timeout} s at {self._describe()}") from error
            except OSError as error:
                self._drop()
                if isinstance(error, PineConnectionError):
                    raise
                raise PineConnectionError(f"cannot talk to PCSX2 PINE at {self._describe()}: {error}") from error
            except BaseException:
                # A protocol error leaves the stream out of step, so the next request starts on a new connection.
                self._drop()
                raise
            finally:
                if not self.persistent:
                    self._drop()

    def _drop(self) -> None:
        connection, self._socket = self._socket, None
        if connection is not None:
            try:
                connection.close()
            except OSError:
                pass

    def close(self) -> None:
        with self._lock:
            self._drop()


class UnixTransport(_SocketTransport):
    """PCSX2's Unix socket (Linux and macOS). ``path`` defaults to ``default_socket_path(slot)``."""

    def __init__(
        self,
        path: Union[str, "os.PathLike[str]", None] = None,
        *,
        slot: int = DEFAULT_SLOT,
        timeout: float = 2.0,
        persistent: bool = True,
    ) -> None:
        super().__init__(timeout=timeout, persistent=persistent)
        self.path = Path(path) if path is not None else default_socket_path(slot)

    def _describe(self) -> str:
        return str(self.path)

    def _open(self) -> socket.socket:
        if not hasattr(socket, "AF_UNIX"):
            raise PineConnectionError("this Python has no Unix sockets; use TcpTransport (PCSX2 on Windows listens on TCP)")
        connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            connection.settimeout(self.timeout)
            connection.connect(str(self.path))
        except BaseException:
            connection.close()
            raise
        return connection


class TcpTransport(_SocketTransport):
    """PCSX2's TCP port on the local machine (Windows). ``port`` is the slot, 28011 by default."""

    def __init__(self, host: str = "127.0.0.1", port: int = DEFAULT_SLOT, *, timeout: float = 2.0, persistent: bool = True) -> None:
        super().__init__(timeout=timeout, persistent=persistent)
        self.host = host
        self.port = port

    def _describe(self) -> str:
        return f"{self.host}:{self.port}"

    def _open(self) -> socket.socket:
        connection = socket.create_connection((self.host, self.port), timeout=self.timeout)
        connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        return connection
