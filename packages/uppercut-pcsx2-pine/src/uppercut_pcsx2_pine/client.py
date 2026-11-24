"""The PINE client."""

from __future__ import annotations

import os
from typing import Iterable, Optional, Sequence, Tuple, Union

from . import protocol as p
from .errors import PineProtocolError, PineWriteDenied
from .transport import TcpTransport, Transport, UnixTransport

Range = Tuple[int, int]
RANGE_LIMIT = 32_768
"""How many bytes of memory ``read_bytes`` and ``write_bytes`` move per message."""


class WriteAllowlist:
    """Guest address ranges a client may write to. Each range is ``(start, end)``, ``end`` exclusive."""

    def __init__(self, ranges: Iterable[Range] = ()) -> None:
        self._ranges: list[Range] = []
        for start, end in ranges:
            self.add(start, end)

    def add(self, start: int, end: int) -> None:
        p.check_address(start)
        if not start < end <= 0x1_0000_0000:
            raise ValueError(f"allowlist range {start:#x} to {end:#x} is empty or outside the 32-bit address space")
        self._ranges.append((start, end))

    def allows(self, address: int, size: int = 1) -> bool:
        """True when ``size`` bytes starting at ``address`` lie inside one range."""
        return any(start <= address and address + size <= end for start, end in self._ranges)

    @property
    def ranges(self) -> list[Range]:
        return list(self._ranges)


class PineBatch:
    """Commands sent as one PINE message. Reads answer in order; ``execute()`` returns their values.

    A batch is one message, so the emulator sees it between two frames of its own choosing but not necessarily
    between two game frames. It raises ``ValueError`` if it cannot fit in one request or reply.
    """

    def __init__(self, client: "PineClient") -> None:
        self._client = client
        self._commands: list[bytes] = []
        self._read_bits: list[int] = []
        self._size = 4
        self._reply_size = 5

    def _add(self, command: bytes, reply_bits: Optional[int]) -> "PineBatch":
        if self._size + len(command) > p.MAX_REQUEST_SIZE:
            raise ValueError("this batch no longer fits in one PINE request")
        if reply_bits is not None and self._reply_size + reply_bits // 8 > p.MAX_REPLY_SIZE:
            raise ValueError("this batch's reply no longer fits in one PINE reply")
        self._commands.append(command)
        self._size += len(command)
        if reply_bits is not None:
            self._read_bits.append(reply_bits)
            self._reply_size += reply_bits // 8
        return self

    def read(self, address: int, bits: int) -> "PineBatch":
        return self._add(p.read_command(address, bits), bits)

    def read8(self, address: int) -> "PineBatch":
        return self.read(address, 8)

    def read16(self, address: int) -> "PineBatch":
        return self.read(address, 16)

    def read32(self, address: int) -> "PineBatch":
        return self.read(address, 32)

    def read64(self, address: int) -> "PineBatch":
        return self.read(address, 64)

    def write(self, address: int, value: int, bits: int) -> "PineBatch":
        command = p.write_command(address, value, bits)
        self._client._check_write(address, bits // 8)
        return self._add(command, None)

    def write8(self, address: int, value: int) -> "PineBatch":
        return self.write(address, value, 8)

    def write16(self, address: int, value: int) -> "PineBatch":
        return self.write(address, value, 16)

    def write32(self, address: int, value: int) -> "PineBatch":
        return self.write(address, value, 32)

    def write64(self, address: int, value: int) -> "PineBatch":
        return self.write(address, value, 64)

    def __len__(self) -> int:
        return len(self._commands)

    def execute(self) -> list[int]:
        """Send the batch and return the values of its reads, in order. An empty batch sends nothing."""
        if not self._commands:
            return []
        answer = self._client._send(b"".join(self._commands))
        return p.split_values(answer, self._read_bits)


class PineClient:
    """Talks to a running PCSX2 over PINE.

    ``write_allowlist`` is ``None`` (the default) for unrestricted writes, or a ``WriteAllowlist`` or a list of
    ``(start, end)`` ranges (``end`` exclusive). An empty list makes the client read-only. A write outside the
    allowlist raises ``PineWriteDenied`` before anything is sent.

    Reads return unsigned integers; use ``signed(value, bits)`` for signed fields.
    """

    def __init__(
        self,
        transport: Optional[Transport] = None,
        *,
        write_allowlist: Union[WriteAllowlist, Iterable[Range], None] = None,
    ) -> None:
        self.transport: Transport = transport if transport is not None else _default_transport()
        if write_allowlist is None or isinstance(write_allowlist, WriteAllowlist):
            self.write_allowlist: Optional[WriteAllowlist] = write_allowlist
        else:
            self.write_allowlist = WriteAllowlist(write_allowlist)

    @classmethod
    def unix(
        cls,
        path: Union[str, "os.PathLike[str]", None] = None,
        *,
        slot: int = p.DEFAULT_SLOT,
        timeout: float = 2.0,
        persistent: bool = True,
        write_allowlist: Union[WriteAllowlist, Iterable[Range], None] = None,
    ) -> "PineClient":
        return cls(UnixTransport(path, slot=slot, timeout=timeout, persistent=persistent), write_allowlist=write_allowlist)

    @classmethod
    def tcp(
        cls,
        host: str = "127.0.0.1",
        port: int = p.DEFAULT_SLOT,
        *,
        timeout: float = 2.0,
        persistent: bool = True,
        write_allowlist: Union[WriteAllowlist, Iterable[Range], None] = None,
    ) -> "PineClient":
        return cls(TcpTransport(host, port, timeout=timeout, persistent=persistent), write_allowlist=write_allowlist)

    def close(self) -> None:
        self.transport.close()

    def __enter__(self) -> "PineClient":
        return self

    def __exit__(self, *_exc: object) -> None:
        self.close()

    def _send(self, commands: bytes) -> bytes:
        return p.parse_reply(self.transport.request(p.frame_request(commands)))

    def _check_write(self, address: int, size: int) -> None:
        if self.write_allowlist is not None and not self.write_allowlist.allows(address, size):
            raise PineWriteDenied(f"write of {size} byte(s) at {address:#x} is outside the write allowlist")

    # Memory.

    def batch(self) -> PineBatch:
        return PineBatch(self)

    def read(self, address: int, bits: int) -> int:
        return self.batch().read(address, bits).execute()[0]

    def read8(self, address: int) -> int:
        return self.read(address, 8)

    def read16(self, address: int) -> int:
        return self.read(address, 16)

    def read32(self, address: int) -> int:
        return self.read(address, 32)

    def read64(self, address: int) -> int:
        return self.read(address, 64)

    def read_many(self, requests: Sequence[Tuple[int, int]], *, max_per_message: int = 4096) -> list[int]:
        """Read several ``(address, bits)`` pairs. Each message holds at most ``max_per_message`` reads, so a long
        list becomes several messages and is not one atomic snapshot."""
        values: list[int] = []
        for start in range(0, len(requests), max_per_message):
            batch = self.batch()
            for address, bits in requests[start : start + max_per_message]:
                batch.read(address, bits)
            values.extend(batch.execute())
        return values

    def write(self, address: int, value: int, bits: int) -> None:
        self.batch().write(address, value, bits).execute()

    def write8(self, address: int, value: int) -> None:
        self.write(address, value, 8)

    def write16(self, address: int, value: int) -> None:
        self.write(address, value, 16)

    def write32(self, address: int, value: int) -> None:
        self.write(address, value, 32)

    def write64(self, address: int, value: int) -> None:
        self.write(address, value, 64)

    def read_bytes(self, address: int, length: int, *, chunk: int = RANGE_LIMIT) -> bytes:
        """Read ``length`` bytes: 64-bit reads for the bulk, then 32, 16 and 8-bit reads for the tail, batched into
        messages of at most ``chunk`` bytes. Several messages are not one atomic snapshot."""
        if length < 0:
            raise ValueError("length cannot be negative")
        p.check_address(address, max(length, 1))
        out = bytearray()
        offset = 0
        while offset < length:
            batch, widths, size = self.batch(), [], 0
            while offset + size < length and size < chunk:
                bits = _widest(length - offset - size)
                batch.read(address + offset + size, bits)
                widths.append(bits)
                size += bits // 8
            values = batch.execute()
            for bits, value in zip(widths, values):
                out += value.to_bytes(bits // 8, "little")
            offset += size
        return bytes(out)

    def write_bytes(self, address: int, data: bytes, *, chunk: int = RANGE_LIMIT) -> None:
        """Write ``data`` with the widest writes that fit, in messages of at most ``chunk`` bytes. The whole range is
        checked against the allowlist before the first message is sent."""
        data = bytes(data)
        p.check_address(address, max(len(data), 1))
        if data:
            self._check_write(address, len(data))
        offset = 0
        while offset < len(data):
            batch, size = self.batch(), 0
            while offset + size < len(data) and size < chunk:
                bits = _widest(len(data) - offset - size)
                value = int.from_bytes(data[offset + size : offset + size + bits // 8], "little")
                batch.write(address + offset + size, value, bits)
                size += bits // 8
            batch.execute()
            offset += size

    # Emulator and game.

    def _string(self, opcode: int) -> str:
        return p.parse_string(self._send(bytes((opcode,))))

    def version(self) -> str:
        """The emulator's version, for example ``"PCSX2 v2.x.y"``. Works with no game running."""
        return self._string(p.VERSION)

    def title(self) -> str:
        return self._string(p.TITLE)

    def game_id(self) -> str:
        """The disc serial, for example ``"SLUS-00000"``."""
        return self._string(p.GAME_ID)

    def uuid(self) -> str:
        """The disc CRC as eight hex digits (PCSX2 calls this the UUID)."""
        return self._string(p.UUID)

    def game_version(self) -> str:
        return self._string(p.GAME_VERSION)

    def status(self) -> p.PineStatus:
        """Running, paused or shut down. Works with no game running (it then says shut down)."""
        answer = self._send(bytes((p.STATUS,)))
        (value,) = p.split_values(answer, [32])
        try:
            return p.PineStatus(value)
        except ValueError:
            raise PineProtocolError(f"unknown PINE status {value}") from None

    def identity(self) -> dict[str, str]:
        """Version, title, game id, uuid, game version and status in one dictionary."""
        return {
            "pcsx2_version": self.version(),
            "title": self.title(),
            "game_id": self.game_id(),
            "uuid": self.uuid(),
            "game_version": self.game_version(),
            "status": self.status().label,
        }

    def save_state(self, slot: int) -> None:
        """Ask PCSX2 to save the state to ``slot`` (0 to 255). The reply comes at once; the file is written a moment
        later on the emulation thread, so wait for it to appear."""
        self._send(p.state_command(p.SAVE_STATE, slot))

    def load_state(self, slot: int) -> None:
        """Ask PCSX2 to load the state in ``slot``. Like ``save_state``, the load happens after the reply."""
        self._send(p.state_command(p.LOAD_STATE, slot))


def _widest(remaining: int) -> int:
    for bits in (64, 32, 16, 8):
        if remaining >= bits // 8:
            return bits
    raise ValueError("nothing left to move")


def _default_transport() -> Transport:
    """The Unix socket where the platform has one, else the TCP port: how PCSX2 itself picks."""
    return TcpTransport() if os.name == "nt" else UnixTransport()
