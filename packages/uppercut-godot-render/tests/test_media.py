import json
import struct
import subprocess
import wave
from pathlib import Path

import pytest

from fakes import FakeFFmpeg, fake_media, thumbnail, write_wav
from uppercut_godot_render import BT709_FILTER, FRAME_PATTERN, Media, RenderError, trim_audio, x264_arguments


def test_x264_arguments_tag_everything_bt709():
    arguments = x264_arguments()
    assert arguments[arguments.index("-crf") + 1] == "16"
    assert arguments[arguments.index("-preset") + 1] == "medium"
    assert arguments[arguments.index("-pix_fmt") + 1] == "yuv420p"
    assert arguments[arguments.index("-vf") + 1] == BT709_FILTER
    for flag in ("-colorspace", "-color_primaries", "-color_trc"):
        assert arguments[arguments.index(flag) + 1] == "bt709"
    assert arguments[arguments.index("-color_range") + 1] == "tv"
    assert "setparams=color_primaries=bt709:color_trc=bt709:colorspace=bt709:range=tv" in BT709_FILTER
    assert x264_arguments(14, "slow")[x264_arguments(14, "slow").index("-crf") + 1] == "14"


def test_require_tools():
    media, _ = fake_media()
    media.require_tools()
    missing, _ = fake_media(tools=False)
    with pytest.raises(RenderError, match="ffmpeg not found on PATH"):
        missing.require_tools()
    only_probe = Media(which=lambda name: "/bin/ffmpeg" if name == "ffmpeg" else None)
    with pytest.raises(RenderError, match="ffprobe not found"):
        only_probe.require_tools()


def test_deviation_is_the_standard_deviation_of_the_thumbnail():
    media, ffmpeg = fake_media()
    for wanted in (0.0, 2.0, 30.0):  # whole numbers: the fake builds its pixels from integer values
        ffmpeg.deviations["f.png"] = wanted
        assert media.frame_deviation(Path("dir/f.png")) == pytest.approx(wanted, abs=0.5)
    command = ffmpeg.calls[-1]
    assert command[:4] == ["ffmpeg", "-v", "error", "-i"]
    assert "scale=64:36:flags=area,format=gray" in command and command[-3:] == ["-f", "rawvideo", "-"]


def test_deviation_errors_name_the_frame():
    ffmpeg = FakeFFmpeg()
    ffmpeg.fail_on = "bad.png"
    media, _ = fake_media(ffmpeg)
    with pytest.raises(RenderError, match="cannot read bad.png"):
        media.frame_deviation(Path("bad.png"))

    def short(command, **_kw):
        return subprocess.CompletedProcess(command, 0, stdout=b"\x00" * 10, stderr=b"")

    with pytest.raises(RenderError, match="cannot read x.png"):
        Media(run=short).frame_deviation(Path("x.png"))
    assert thumbnail(3).count(131) == 1152


def test_encode_command_without_audio(tmp_path):
    media, ffmpeg = fake_media()
    media.encode(tmp_path / "frames", tmp_path / "out.mp4", 60, crf=14, preset="slow")
    command = ffmpeg.calls[0]
    assert command[:4] == ["ffmpeg", "-v", "error", "-y"]
    assert command[command.index("-framerate") + 1] == "60"
    assert command[command.index("-i") + 1] == str(tmp_path / "frames" / FRAME_PATTERN)
    assert command.count("-i") == 1
    assert command[command.index("-r") + 1] == "60" and command[command.index("-fps_mode") + 1] == "cfr"
    assert "-c:a" not in command
    assert command[-3:] == ["-movflags", "+faststart", str(tmp_path / "out.mp4")]
    assert command[command.index("-crf") + 1] == "14" and command[command.index("-preset") + 1] == "slow"


def test_encode_command_with_audio(tmp_path):
    media, ffmpeg = fake_media()
    media.encode(tmp_path / "frames", tmp_path / "out.mp4", 30, audio=tmp_path / "audio.wav", audio_bitrate="128k")
    command = ffmpeg.calls[0]
    assert command.count("-i") == 2 and command[command.index("-i", command.index("-i") + 1) + 1] == str(tmp_path / "audio.wav")
    assert command[command.index("-c:a") + 1] == "aac" and command[command.index("-b:a") + 1] == "128k"


def test_ffmpeg_failures_carry_stderr(tmp_path):
    ffmpeg = FakeFFmpeg()
    ffmpeg.fail_on = str(tmp_path / "out.mp4")
    media, _ = fake_media(ffmpeg)
    with pytest.raises(RenderError, match="ffmpeg failed: synthetic failure"):
        media.encode(tmp_path, tmp_path / "out.mp4", 60)


def test_probe_returns_the_video_stream():
    media, ffmpeg = fake_media()
    ffmpeg.video = ffmpeg.stream(60, (1920, 1080), 210)
    assert media.probe(Path("a.mp4"))["width"] == 1920
    assert ffmpeg.calls[-1][:2] == ["ffprobe", "-v"] and "-count_frames" in ffmpeg.calls[-1]


@pytest.mark.parametrize(
    "streams, audio, ok",
    [(0, None, True), (1, None, True), (0, False, True), (1, True, True), (1, False, False), (0, True, False), (2, True, False)],
)
def test_probe_audio_requirements(streams, audio, ok):
    media, ffmpeg = fake_media()
    ffmpeg.video = ffmpeg.stream(60, (16, 9), 1)
    ffmpeg.audio_streams = streams
    if ok:
        media.probe(Path("a.mp4"), require_audio=audio)
    else:
        with pytest.raises(RenderError, match="expected one video and"):
            media.probe(Path("a.mp4"), require_audio=audio)


def test_probe_rejects_odd_output():
    media, ffmpeg = fake_media()
    for text, message in (("not json", "other than JSON"), (json.dumps({}), "no streams"), (json.dumps({"streams": []}), "expected one video")):
        ffmpeg.probe_text = text
        with pytest.raises(RenderError, match=message):
            media.probe(Path("a.mp4"))
    ffmpeg.fail_on = "a.mp4"
    ffmpeg.probe_text = None
    with pytest.raises(RenderError, match="ffprobe failed"):
        media.probe(Path("a.mp4"))


@pytest.mark.parametrize(
    "key, value, message",
    [
        ("width", 1280, "footage width is 1280, expected 1920"),
        ("height", 720, "footage height is 720, expected 1080"),
        ("r_frame_rate", "30/1", "footage r_frame_rate is 30/1, expected 60/1"),
        ("avg_frame_rate", "59/1", "footage avg_frame_rate is 59/1"),
        ("nb_read_frames", "209", "footage nb_read_frames is 209, expected 210"),
        ("color_space", "smpte170m", "footage color_space is smpte170m, expected bt709"),
        ("color_primaries", None, "footage color_primaries is None, expected bt709"),
        ("color_transfer", "iec61966-2-1", "footage color_transfer is iec61966-2-1, expected bt709"),
    ],
)
def test_verify_footage_checks_each_property(key, value, message):
    media, ffmpeg = fake_media()
    ffmpeg.video = ffmpeg.stream(60, (1920, 1080), 210)
    assert media.verify_footage(Path("a.mp4"), 60, 210, (1920, 1080))["width"] == 1920
    if value is None:
        del ffmpeg.video[key]
    else:
        ffmpeg.video[key] = value
    with pytest.raises(RenderError, match=message):
        media.verify_footage(Path("a.mp4"), 60, 210, (1920, 1080))


def test_contact_sheet_picks_frames_evenly(tmp_path):
    media, ffmpeg = fake_media()
    media.contact_sheet(tmp_path / "frames", tmp_path / "sheet.png", 210)
    command = ffmpeg.calls[0]
    filter_text = command[command.index("-vf") + 1]
    assert filter_text == "select='not(mod(n\\,17))',scale=480:270:flags=area,tile=4x3"
    assert command[-3:] == ["-frames:v", "1", str(tmp_path / "sheet.png")]
    media.contact_sheet(tmp_path, tmp_path / "s2.png", 5, tile="270:480", columns=2, rows=2)
    assert "mod(n\\,1)" in ffmpeg.calls[1][ffmpeg.calls[1].index("-vf") + 1] and "tile=2x2" in ffmpeg.calls[1][ffmpeg.calls[1].index("-vf") + 1]


def samples(path):
    with wave.open(str(path), "rb") as reader:
        data = reader.readframes(reader.getnframes())
        return [v for (v,) in struct.iter_unpack("<h", data)], reader.getframerate(), reader.getnchannels()


def test_trim_audio_keeps_exactly_the_runs_samples(tmp_path):
    write_wav(tmp_path / "frame.wav", rate=48000, seconds=2.0)
    trim_audio(tmp_path / "frame.wav", tmp_path / "audio.wav", 60, first_frame=3, frame_count=10)
    values, rate, channels = samples(tmp_path / "audio.wav")
    assert (rate, channels, len(values)) == (48000, 1, 10 * 800)
    assert values[0] == 3 * 800 and values[-1] == 3 * 800 + 7999


def test_trim_audio_errors(tmp_path):
    with pytest.raises(RenderError, match="wrote no audio"):
        trim_audio(tmp_path / "missing.wav", tmp_path / "a.wav", 60, 0, 1)
    write_wav(tmp_path / "frame.wav", rate=44100, seconds=1.0)
    with pytest.raises(RenderError, match="44100 Hz does not divide into 11 fps"):
        trim_audio(tmp_path / "frame.wav", tmp_path / "a.wav", 11, 0, 1)
    with pytest.raises(RenderError, match="audio has 44100 samples, needs 88200"):
        trim_audio(tmp_path / "frame.wav", tmp_path / "a.wav", 60, 60, 60)
