"""Stand-ins for Godot, FFmpeg and FFprobe, and synthetic PNG frames. Nothing here starts a real program."""

from __future__ import annotations

import json
import struct
import subprocess
import wave
import zlib
from pathlib import Path
from typing import Any, Callable, Optional, Sequence

from uppercut_godot_render import GodotRun, Media

GOOD_LOG = """Godot Engine v4.7.2.stable.official.deadbeef - https://godotengine.org
Vulkan 1.3.280 - Forward+ - Using Device #0: Synthetic GPU
RUN_START_FRAME 1
"""


def make_png(width: int, height: int, *, tag: int = 0) -> bytes:
    """A valid RGB PNG (solid colour from ``tag``, so frames with different tags differ)."""
    row = b"\x00" + bytes((tag % 256, (tag * 3) % 256, 40)) * width

    def chunk(kind: bytes, payload: bytes) -> bytes:
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)

    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(row * height))
        + chunk(b"IEND", b"")
    )


def write_wav(path: Path, *, rate: int = 48000, seconds: float = 1.0) -> None:
    """A mono 16-bit WAV whose sample ``n`` holds the value ``n % 30000``, so a trimmed copy shows where it started."""
    count = int(rate * seconds)
    with wave.open(str(path), "wb") as writer:
        writer.setnchannels(1)
        writer.setsampwidth(2)
        writer.setframerate(rate)
        writer.writeframes(b"".join(struct.pack("<h", n % 30000) for n in range(count)))


def thumbnail(deviation: float) -> bytes:
    """64x36 grey pixels with the given standard deviation."""
    half = 64 * 36 // 2
    low, high = round(128 - deviation), round(128 + deviation)
    return bytes([low]) * half + bytes([high]) * half


class FakeFFmpeg:
    """Answers ffmpeg and ffprobe the way the pipeline expects, and records every command it was given."""

    def __init__(self) -> None:
        self.calls: list[list[str]] = []
        self.deviations: dict[str, float] = {}
        self.default_deviation = 30.0
        self.fail_on: Optional[str] = None
        self.video: dict[str, Any] = {}
        self.audio_streams = 0
        self.probe_text: Optional[str] = None

    def stream(self, fps: int, size: tuple[int, int], frames: int) -> dict[str, Any]:
        return {
            "codec_type": "video",
            "width": size[0],
            "height": size[1],
            "r_frame_rate": f"{fps}/1",
            "avg_frame_rate": f"{fps}/1",
            "nb_read_frames": str(frames),
            "color_space": "bt709",
            "color_primaries": "bt709",
            "color_transfer": "bt709",
        }

    def __call__(self, command: Sequence[str], **_kwargs: Any) -> "subprocess.CompletedProcess[Any]":
        command = list(command)
        self.calls.append(command)
        program = Path(command[0]).name
        if self.fail_on and self.fail_on in command:
            return subprocess.CompletedProcess(command, 1, stdout=b"", stderr="synthetic failure\n")
        if program == "ffprobe":
            streams = [self.video] + [{"codec_type": "audio"}] * self.audio_streams
            text = self.probe_text if self.probe_text is not None else json.dumps({"streams": streams})
            return subprocess.CompletedProcess(command, 0, stdout=text, stderr="")
        if command[-1] == "-" and "rawvideo" in command:
            name = Path(command[command.index("-i") + 1]).name
            return subprocess.CompletedProcess(command, 0, stdout=thumbnail(self.deviations.get(name, self.default_deviation)), stderr=b"")
        Path(command[-1]).write_bytes(b"FAKE " + " ".join(command).encode())
        return subprocess.CompletedProcess(command, 0, stdout="", stderr="")


def fake_media(ffmpeg: Optional[FakeFFmpeg] = None, *, tools: bool = True) -> tuple[Media, FakeFFmpeg]:
    fake = ffmpeg or FakeFFmpeg()
    return Media(run=fake, which=lambda name: f"/usr/bin/{name}" if tools else None), fake


def fake_godot(
    *,
    frames: int,
    size: tuple[int, int],
    log: str = GOOD_LOG,
    exit_code: int = 0,
    audio_rate: Optional[int] = None,
    seen: Optional[list[Any]] = None,
    inspect: Optional[Callable[[Sequence[str]], None]] = None,
) -> Callable[[Sequence[str]], GodotRun]:
    """A Godot stand-in: it writes ``frames`` PNGs (and a WAV) where ``--write-movie`` says, as Movie Maker does."""

    def run(command: Sequence[str]) -> GodotRun:
        if seen is not None:
            seen.append(list(command))
        if inspect:
            inspect(command)
        target = Path(command[list(command).index("--write-movie") + 1])
        target.parent.mkdir(parents=True, exist_ok=True)
        for index in range(frames):
            (target.parent / f"frame{index:08d}.png").write_bytes(make_png(size[0], size[1], tag=index))
        if audio_rate:
            write_wav(target.with_suffix(".wav"), rate=audio_rate, seconds=max(1.0, frames / 30))
        return GodotRun(exit_code, log)

    return run
