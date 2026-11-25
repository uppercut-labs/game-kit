"""The same pipeline with the real FFmpeg and FFprobe (Godot is still a stand-in). Skipped where they are missing."""

import json
import shutil
import subprocess

import pytest

from fakes import GOOD_LOG, fake_godot, make_png
from uppercut_godot_render import Media, RenderError, RenderPlan, render, run_start_frame

pytestmark = pytest.mark.skipif(shutil.which("ffmpeg") is None or shutil.which("ffprobe") is None, reason="needs ffmpeg and ffprobe")

SIZE = (64, 36)


def noisy_png(width, height, seed):
    """A busy RGB PNG so the blank-frame check passes for real."""
    import struct
    import zlib

    rows = b"".join(
        b"\x00" + b"".join(bytes(((x * 5 + seed * 9) % 256, (y * 7 + seed) % 256, (x + y + seed * 3) % 256)) for x in range(width))
        for y in range(height)
    )

    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)

    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")


def busy_godot(frames, size, *, blank_index=None):
    def run(command):
        from pathlib import Path

        target = Path(command[command.index("--write-movie") + 1])
        target.parent.mkdir(parents=True, exist_ok=True)
        for index in range(frames):
            data = make_png(size[0], size[1], tag=0) if index == blank_index else noisy_png(size[0], size[1], index)
            (target.parent / f"frame{index:08d}.png").write_bytes(data)
        from uppercut_godot_render import GodotRun

        return GodotRun(0, GOOD_LOG)

    return run


def test_a_real_encode_is_bt709_constant_rate_and_the_right_length(tmp_path):
    (tmp_path / "game").mkdir()
    plan = RenderPlan(godot="godot", project=tmp_path / "game", output=tmp_path / "out", fps=30, size=SIZE, frame_count=12, start_frame=run_start_frame, crf=20, preset="ultrafast")
    result = render(plan, runner=busy_godot(15, SIZE))

    probe = json.loads(
        subprocess.run(
            ["ffprobe", "-v", "error", "-count_frames", "-show_streams", "-of", "json", str(tmp_path / "out" / "footage.mp4")],
            capture_output=True, text=True, check=True,
        ).stdout
    )  # fmt: skip
    video = probe["streams"][0]
    assert (video["width"], video["height"], video["nb_read_frames"], video["r_frame_rate"]) == (64, 36, "12", "30/1")
    assert (video["color_space"], video["color_primaries"], video["color_transfer"]) == ("bt709", "bt709", "bt709")
    assert (tmp_path / "out" / "contact_sheet.png").stat().st_size > 0
    assert [o["path"] for o in result.manifest["outputs"]] == ["frames.sha256", "footage.mp4", "contact_sheet.png"]


def test_a_real_blank_frame_is_caught(tmp_path):
    (tmp_path / "game").mkdir()
    plan = RenderPlan(godot="godot", project=tmp_path / "game", output=tmp_path / "out", fps=30, size=SIZE, frame_count=12, start_frame=run_start_frame)
    with pytest.raises(RenderError, match="last frame is blank"):
        render(plan, runner=busy_godot(15, SIZE, blank_index=12))  # footage frames are Movie Maker frames 1 to 12


def test_the_real_deviation_of_a_busy_and_a_solid_frame(tmp_path):
    (tmp_path / "busy.png").write_bytes(noisy_png(64, 36, 3))
    (tmp_path / "solid.png").write_bytes(make_png(64, 36, tag=5))
    media = Media()
    assert media.frame_deviation(tmp_path / "busy.png") > 10
    assert media.frame_deviation(tmp_path / "solid.png") == pytest.approx(0, abs=0.5)
