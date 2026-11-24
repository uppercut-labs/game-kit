"""FFmpeg and FFprobe: the blank-frame check, the bt709 encode, the footage probe and the contact sheet.

Every call goes through ``Media.run`` (``subprocess.run`` by default), so a test can stand in for the tools."""

from __future__ import annotations

import json
import shutil
import subprocess
import wave
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Mapping, Optional, Sequence

from .errors import RenderError

FRAME_PATTERN = "frame%08d.png"
THUMBNAIL_SIZE = (64, 36)
# scale converts to bt709 YUV; setparams tags the frames bt709 too. Without it FFmpeg 7 and later carry the PNGs' sRGB
# transfer (iec61966-2-1) into the stream despite -color_trc.
BT709_FILTER = (
    "scale=out_color_matrix=bt709:out_range=tv,"
    "setparams=color_primaries=bt709:color_trc=bt709:colorspace=bt709:range=tv"
)


def x264_arguments(crf: int = 16, preset: str = "medium") -> list[str]:
    """Constant-quality H.264 in yuv420p, tagged bt709 and TV range."""
    return [
        "-c:v", "libx264", "-preset", preset, "-crf", str(crf), "-pix_fmt", "yuv420p",
        "-vf", BT709_FILTER,
        "-colorspace", "bt709", "-color_primaries", "bt709", "-color_trc", "bt709", "-color_range", "tv",
    ]  # fmt: skip


@dataclass(frozen=True)
class Media:
    """The FFmpeg tools to use, and how to run them."""

    ffmpeg: str = "ffmpeg"
    ffprobe: str = "ffprobe"
    run: Callable[..., Any] = field(default=subprocess.run, repr=False)
    which: Callable[[str], Optional[str]] = field(default=shutil.which, repr=False)

    def require_tools(self) -> None:
        for tool in (self.ffmpeg, self.ffprobe):
            if self.which(tool) is None:
                raise RenderError(f"{tool} not found on PATH")

    def _ffmpeg(self, arguments: Sequence[str]) -> None:
        completed = self.run([self.ffmpeg, "-v", "error", "-y", *arguments], capture_output=True, text=True, check=False)
        if completed.returncode != 0:
            raise RenderError(f"ffmpeg failed: {(completed.stderr or '').strip()}")

    def frame_deviation(self, path: Path) -> float:
        """The standard deviation of a 64x36 greyscale thumbnail (0 for a solid frame, about 30 for a busy one)."""
        width, height = THUMBNAIL_SIZE
        completed = self.run(
            [self.ffmpeg, "-v", "error", "-i", str(path), "-vf", f"scale={width}:{height}:flags=area,format=gray", "-f", "rawvideo", "-"],
            capture_output=True,
            check=False,
        )
        pixels: bytes = completed.stdout
        if completed.returncode != 0 or len(pixels) != width * height:
            detail = completed.stderr.decode(errors="replace") if isinstance(completed.stderr, bytes) else str(completed.stderr or "")
            raise RenderError(f"cannot read {Path(path).name}: {detail.strip()}")
        mean = sum(pixels) / len(pixels)
        return float((sum((value - mean) ** 2 for value in pixels) / len(pixels)) ** 0.5)

    def encode(
        self,
        frames: Path,
        destination: Path,
        fps: int,
        *,
        audio: Optional[Path] = None,
        crf: int = 16,
        preset: str = "medium",
        audio_bitrate: str = "192k",
    ) -> None:
        """Encode ``frames/frame%08d.png`` to a constant-rate bt709 H.264 MP4 (with AAC audio when ``audio`` is given)."""
        command = ["-framerate", str(fps), "-i", str(Path(frames) / FRAME_PATTERN)]
        if audio is not None:
            command += ["-i", str(audio)]
        command += [*x264_arguments(crf, preset), "-r", str(fps), "-fps_mode", "cfr"]
        if audio is not None:
            command += ["-c:a", "aac", "-b:a", audio_bitrate]
        command += ["-movflags", "+faststart", str(destination)]
        self._ffmpeg(command)

    def probe(self, path: Path, *, require_audio: Optional[bool] = None) -> dict[str, Any]:
        """The video stream of ``path`` as ffprobe reports it, with every frame counted. ``require_audio`` ``True``
        demands one audio stream, ``False`` none, ``None`` does not look."""
        completed = self.run(
            [self.ffprobe, "-v", "error", "-count_frames", "-show_streams", "-of", "json", str(path)],
            capture_output=True,
            text=True,
            check=False,
        )
        if completed.returncode != 0:
            raise RenderError(f"ffprobe failed: {(completed.stderr or '').strip()}")
        try:
            probed = json.loads(completed.stdout)
        except ValueError as error:
            raise RenderError(f"ffprobe printed something other than JSON: {error}") from error
        streams = probed.get("streams") if isinstance(probed, dict) else None
        if not isinstance(streams, list):
            raise RenderError("ffprobe returned no streams")
        video = [s for s in streams if isinstance(s, dict) and s.get("codec_type") == "video"]
        audio = [s for s in streams if isinstance(s, dict) and s.get("codec_type") == "audio"]
        wanted_audio = {None: len(audio), True: 1, False: 0}[require_audio]
        if len(video) != 1 or len(audio) != wanted_audio:
            raise RenderError(f"expected one video and {wanted_audio} audio stream(s), found {len(video)} and {len(audio)}")
        return video[0]

    def verify_footage(
        self,
        path: Path,
        fps: int,
        frame_count: int,
        size: tuple[int, int],
        *,
        require_audio: Optional[bool] = None,
    ) -> dict[str, Any]:
        """Check the encode: size, constant frame rate, the number of frames and the bt709 tags. Returns the stream."""
        video = self.probe(path, require_audio=require_audio)
        rate = f"{fps}/1"
        expected: Mapping[str, object] = {
            "width": size[0],
            "height": size[1],
            "r_frame_rate": rate,
            "avg_frame_rate": rate,
            "nb_read_frames": str(frame_count),
            "color_space": "bt709",
            "color_primaries": "bt709",
            "color_transfer": "bt709",
        }
        for key, wanted in expected.items():
            if video.get(key) != wanted:
                raise RenderError(f"footage {key} is {video.get(key)}, expected {wanted}")
        return video

    def contact_sheet(
        self,
        frames: Path,
        destination: Path,
        frame_count: int,
        *,
        tile: str = "480:270",
        columns: int = 4,
        rows: int = 3,
    ) -> None:
        """One PNG of ``columns`` by ``rows`` frames picked evenly from the run. ``tile`` is ``width:height`` (a
        height of ``-2`` keeps the aspect)."""
        step = max(1, frame_count // (columns * rows))
        self._ffmpeg(
            [
                "-i", str(Path(frames) / FRAME_PATTERN),
                "-vf", f"select='not(mod(n\\,{step}))',scale={tile}:flags=area,tile={columns}x{rows}",
                "-frames:v", "1", str(destination),
            ]
        )  # fmt: skip


def trim_audio(source: Path, destination: Path, fps: int, first_frame: int, frame_count: int) -> None:
    """Keep exactly the samples of frames ``first_frame`` to ``first_frame + frame_count - 1`` of a WAV Movie Maker wrote."""
    source = Path(source)
    if not source.exists():
        raise RenderError("Movie Maker wrote no audio")
    with wave.open(str(source), "rb") as reader:
        rate = reader.getframerate()
        if rate % fps != 0:
            raise RenderError(f"audio at {rate} Hz does not divide into {fps} fps")
        per_frame = rate // fps
        needed = (first_frame + frame_count) * per_frame
        if reader.getnframes() < needed:
            raise RenderError(f"audio has {reader.getnframes()} samples, needs {needed}")
        reader.setpos(first_frame * per_frame)
        samples = reader.readframes(frame_count * per_frame)
        with wave.open(str(destination), "wb") as writer:
            writer.setnchannels(reader.getnchannels())
            writer.setsampwidth(reader.getsampwidth())
            writer.setframerate(rate)
            writer.writeframes(samples)
