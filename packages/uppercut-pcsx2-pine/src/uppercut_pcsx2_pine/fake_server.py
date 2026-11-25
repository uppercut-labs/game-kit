"""An in-process stand-in for PCSX2's PINE server, for tests. No emulator, game or BIOS is involved.

It follows the PCSX2 server's rules: commands run in order, any bad command fails the whole message with a single
0xFF result byte, memory and string commands fail while ``vm`` is false, and the size limits are the real ones.
Memory is a plain ``bytearray`` mapped at ``base``; an access outside it fails the message.

    with FakePineServer(ram=bytearray(0x1000), title="Test Game", game_id="TEST-00000") as server:
        client = PineClient.tcp(port=server.start_tcp())
"""

from __future__ import annotations

import socket
import socketserver
import struct
import threading
from pathlib import Path
from typing import Callable, Optional, Union

from . import protocol as p

_READ_SIZES = {0: 1, 1: 2, 2: 4, 3: 8}
_WRITE_SIZES = {4: 1, 5: 2, 6: 4, 7: 8}


class FakePineServer:
    def __init__(
        self,
        ram: Optional[bytearray] = None,
        *,
        base: int = 0,
        vm: bool = True,
        version: str = "PCSX2 v0.0.0-test",
        title: str = "Synthetic Test Game",
        game_id: str = "TEST-00000",
        uuid: str = "00000000",
        game_version: str = "1.00",
        status: p.PineStatus = p.PineStatus.RUNNING,
    ) -> None:
        self.ram = ram if ram is not None else bytearray(0x10000)
        self.base = base
        self.vm = vm
        self.version = version
        self.title = title
        self.game_id = game_id
        self.uuid = uuid
        self.game_version = game_version
        self.status = status
        self.requests: list[bytes] = []
        """Every request body received (the commands, without the length field), in order."""
        self.saved_slots: list[int] = []
        self.loaded_slots: list[int] = []
        self.connections = 0
        self.reply_hook: Optional[Callable[[bytes], Optional[bytes]]] = None
        """Return bytes to send instead of the real reply (a whole reply including its length), or None."""
        self._servers: list[socketserver.BaseServer] = []
        self._threads: list[threading.Thread] = []
        self._clients: list[socket.socket] = []
        self._lock = threading.Lock()

    # Command execution.

    def execute(self, commands: bytes) -> bytes:
        """Run a request body and return the whole reply, length field included."""
        reply = bytearray(5)
        pos = 0
        try:
            while pos < len(commands):
                opcode = commands[pos]
                pos += 1
                pos = self._run(opcode, commands, pos, reply)
                if len(reply) >= p.MAX_REPLY_SIZE + 1:
                    raise ValueError("reply too large")
        except (ValueError, IndexError, struct.error):
            return struct.pack("<IB", 5, p.RESULT_FAIL)
        reply[0:4] = struct.pack("<I", len(reply))
        reply[4] = p.RESULT_OK
        return bytes(reply)

    def _offset(self, address: int, size: int) -> int:
        offset = address - self.base
        if offset < 0 or offset + size > len(self.ram):
            raise ValueError("address outside the fake RAM")
        return offset

    def _text(self, reply: bytearray, text: str) -> None:
        data = text.encode("utf-8") + b"\0"
        reply += struct.pack("<I", len(data)) + data

    def _run(self, opcode: int, commands: bytes, pos: int, reply: bytearray) -> int:
        needs_vm = opcode != p.VERSION and opcode != p.STATUS
        if needs_vm and not self.vm:
            raise ValueError("no VM")
        if opcode in _READ_SIZES:
            size = _READ_SIZES[opcode]
            (address,) = struct.unpack_from("<I", commands, pos)
            offset = self._offset(address, size)
            reply += self.ram[offset : offset + size]
            return pos + 4
        if opcode in _WRITE_SIZES:
            size = _WRITE_SIZES[opcode]
            (address,) = struct.unpack_from("<I", commands, pos)
            data = commands[pos + 4 : pos + 4 + size]
            if len(data) != size:
                raise ValueError("truncated write")
            offset = self._offset(address, size)
            self.ram[offset : offset + size] = data
            return pos + 4 + size
        if opcode == p.VERSION:
            self._text(reply, self.version)
        elif opcode == p.TITLE:
            self._text(reply, self.title)
        elif opcode == p.GAME_ID:
            self._text(reply, self.game_id)
        elif opcode == p.UUID:
            self._text(reply, self.uuid)
        elif opcode == p.GAME_VERSION:
            self._text(reply, self.game_version)
        elif opcode == p.STATUS:
            reply += struct.pack("<I", int(self.status if self.vm else p.PineStatus.SHUTDOWN))
        elif opcode in (p.SAVE_STATE, p.LOAD_STATE):
            slot = commands[pos]
            (self.saved_slots if opcode == p.SAVE_STATE else self.loaded_slots).append(slot)
            return pos + 1
        else:
            raise ValueError("unimplemented opcode")
        return pos

    # Serving.

    def _handler(self) -> type:
        outer = self

        class Handler(socketserver.BaseRequestHandler):
            def handle(self) -> None:
                connection: socket.socket = self.request
                with outer._lock:
                    outer.connections += 1
                    outer._clients.append(connection)
                while True:
                    head = _recv(connection, 4)
                    if head is None:
                        return
                    (total,) = struct.unpack("<I", head)
                    if total < 4 or total > p.MAX_REQUEST_SIZE:
                        return
                    body = _recv(connection, total - 4)
                    if body is None:
                        return
                    with outer._lock:
                        outer.requests.append(body)
                    reply = outer.execute(body)
                    if outer.reply_hook is not None:
                        override = outer.reply_hook(body)
                        if override is not None:
                            reply = override
                    if reply == b"":
                        return  # the hook asked to close the connection without answering
                    try:
                        connection.sendall(reply)
                    except OSError:
                        return

        return Handler

    def _serve(self, server: socketserver.BaseServer) -> None:
        thread = threading.Thread(target=server.serve_forever, kwargs={"poll_interval": 0.02}, daemon=True)
        thread.start()
        self._servers.append(server)
        self._threads.append(thread)

    def start_tcp(self, host: str = "127.0.0.1", port: int = 0) -> int:
        """Listen on TCP (port 0 picks a free one) and return the port."""

        class Server(socketserver.ThreadingMixIn, socketserver.TCPServer):
            allow_reuse_address = True
            daemon_threads = True

        server = Server((host, port), self._handler())
        self._serve(server)
        return server.server_address[1]

    def start_unix(self, path: Union[str, Path]) -> Path:
        """Listen on a Unix socket at ``path`` (keep it short: socket paths top out near 100 bytes)."""

        class Server(socketserver.ThreadingMixIn, socketserver.UnixStreamServer):
            daemon_threads = True

        server = Server(str(path), self._handler())
        self._serve(server)
        return Path(path)

    def stop(self) -> None:
        """Stop listening and close every open client connection, as PCSX2 does when it exits."""
        for server in self._servers:
            server.shutdown()
            server.server_close()
        for thread in self._threads:
            thread.join(timeout=2)
        with self._lock:
            clients, self._clients = self._clients, []
        for connection in clients:
            try:
                connection.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
        self._servers.clear()
        self._threads.clear()

    def __enter__(self) -> "FakePineServer":
        return self

    def __exit__(self, *_exc: object) -> None:
        self.stop()


def _recv(connection: socket.socket, size: int) -> Optional[bytes]:
    data = bytearray()
    while len(data) < size:
        try:
            chunk = connection.recv(size - len(data))
        except OSError:
            return None
        if not chunk:
            return None
        data += chunk
    return bytes(data)
