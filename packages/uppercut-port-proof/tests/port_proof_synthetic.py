"""Made-up frames, WAVs and a debug server for the tests. Nothing here is from a game."""

from __future__ import annotations

import json
import math
import socket
import struct
import threading
from typing import Callable, Dict, List, Optional


def ppm_bytes(width: int, height: int, fill=lambda x, y: (x * 5 & 255, y * 9 & 255, (x ^ y) & 255), *, comment: bool = False) -> bytes:
    header = b"P6\n" + (b"# made up\n" if comment else b"") + b"%d %d\n255\n" % (width, height)
    return header + bytes(c for y in range(height) for x in range(width) for c in fill(x, y))


def wav_bytes(frames: List[tuple], rate: int = 8000, *, data_size: Optional[int] = None, riff_size: Optional[int] = None, tail: bytes = b"") -> bytes:
    """A 16-bit PCM WAV of `frames` (each a tuple of channel values). `data_size` and `riff_size` can lie, as a killed run's header does."""
    channels = len(frames[0]) if frames else 1
    data = b"".join(struct.pack("<%dh" % channels, *f) for f in frames)
    fmt = struct.pack("<HHIIHH", 1, channels, rate, rate * channels * 2, channels * 2, 16)
    body = b"WAVE" + b"fmt " + struct.pack("<I", 16) + fmt + b"data" + struct.pack("<I", len(data) if data_size is None else data_size) + data + tail
    return b"RIFF" + struct.pack("<I", len(body) if riff_size is None else riff_size) + body


def tone(seconds: float, rate: int = 8000, amp: int = 8000, freq: float = 440.0) -> List[tuple]:
    return [(int(amp * math.sin(2 * math.pi * freq * i / rate)),) for i in range(int(seconds * rate))]


def silence(seconds: float, rate: int = 8000, channels: int = 1) -> List[tuple]:
    return [(0,) * channels for _ in range(int(seconds * rate))]


class FakeDebugServer:
    """A stand-in for a port's debug server: one JSON line in, one JSON line out, then the connection closes.

    The frame counter advances by `step` on every `frame` request. Every request is kept in `.requests`."""

    def __init__(self, step: int = 7, *, drop_first: int = 0, handler: Optional[Callable[[dict], Optional[dict]]] = None) -> None:
        self.step = step
        self.frame = 0
        self.requests: List[dict] = []
        self.drop_first = drop_first  # close this many connections without answering
        self.handler = handler
        self._sock = socket.socket()
        self._sock.bind(("127.0.0.1", 0))
        self._sock.listen(8)
        self.port = self._sock.getsockname()[1]
        self._stop = False
        self._thread = threading.Thread(target=self._serve, daemon=True)
        self._thread.start()

    def _serve(self) -> None:
        while not self._stop:
            try:
                conn, _ = self._sock.accept()
            except OSError:
                return
            with conn:
                stream = conn.makefile("rw", encoding="utf-8", newline="\n")
                try:
                    line = stream.readline()
                    if not line:
                        continue  # the client only probed the port
                    request = json.loads(line)
                    self.requests.append(request)
                    if self.drop_first > 0:
                        self.drop_first -= 1
                        continue
                    reply = self.answer(request)
                    if reply is not None:
                        stream.write(json.dumps(reply) + "\n")
                        stream.flush()
                except OSError:
                    pass
                finally:
                    stream.close()  # the socket closes only when its file object does, and the client waits for that

    def answer(self, request: dict) -> Optional[dict]:
        if self.handler is not None:
            reply = self.handler(request)
            if reply is not None:
                return reply
        cmd = request["cmd"]
        if cmd == "frame":
            self.frame += self.step
            return {"id": request["id"], "frame": self.frame}
        return {"id": request["id"], "ok": True}

    def commands(self) -> List[str]:
        return [r["cmd"] for r in self.requests]

    def close(self) -> None:
        self._stop = True
        try:
            self._sock.close()
        except OSError:
            pass

    def __enter__(self) -> "FakeDebugServer":
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()
