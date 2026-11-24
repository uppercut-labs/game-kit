import json
from pathlib import Path

import pytest

from fakes import GOOD_LOG, fake_godot, fake_media
from uppercut_godot_render import (
    RenderError,
    RenderPlan,
    compare_frame_hashes,
    render,
    run_start_frame,
    sha256_file,
    shot_start_frame,
)

SIZE = (32, 18)


def plan(tmp_path, **overrides):
    values = dict(
        godot="/opt/godot",
        project=tmp_path / "game",
        output=tmp_path / "out",
        fps=60,
        size=SIZE,
        frame_count=5,
        start_frame=run_start_frame,
        game_arguments=("--look=hd",),
    )
    values.update(overrides)
    (tmp_path / "game").mkdir(exist_ok=True)
    return RenderPlan(**values)


def good_media():
    media, ffmpeg = fake_media()
    ffmpeg.video = ffmpeg.stream(60, SIZE, 5)
    return media, ffmpeg


def test_a_good_render_gives_verified_footage_and_a_manifest(tmp_path):
    media, ffmpeg = good_media()
    seen = []
    result = render(plan(tmp_path), media=media, runner=fake_godot(frames=8, size=SIZE, seen=seen))

    out = tmp_path / "out"
    assert result.output == out.resolve()
    assert sorted(p.name for p in out.iterdir()) == ["contact_sheet.png", "footage.mp4", "frames", "frames.sha256", "godot.log", "manifest.json"]
    assert len(list((out / "frames").iterdir())) == 5
    assert (out / "godot.log").read_text() == GOOD_LOG
    # RUN_START_FRAME 1 in the log: footage frame 0 is Movie Maker frame 1.
    first_footage = (out / "frames" / "frame00000000.png").read_bytes()
    assert first_footage == __import__("fakes").make_png(32, 18, tag=1)

    manifest = json.loads((out / "manifest.json").read_text())
    assert manifest == result.manifest
    assert manifest["fps"] == 60 and manifest["frameSize"] == "32x18" and manifest["frameCount"] == 5 and manifest["firstMovieFrame"] == 1
    assert manifest["rendererLine"].startswith("Vulkan 1.3.280 - Forward+")
    assert manifest["godotExitCode"] == 0 and manifest["extra"] == {}
    assert [o["path"] for o in manifest["outputs"]] == ["frames.sha256", "footage.mp4", "contact_sheet.png"]
    for entry in manifest["outputs"]:
        assert entry["sha256"] == sha256_file(out / entry["path"])
    assert (out / "frames.sha256").read_text().splitlines()[0].endswith("  frame00000000.png")

    command = seen[0]
    assert command[:3] == ["/opt/godot", "--path", str(tmp_path / "game")]
    assert command[command.index("--fixed-fps") + 1] == "60"
    assert command[command.index("--write-movie") + 1] == str(out.resolve() / "raw" / "frame.png")
    assert command[-2:] == ["--", "--look=hd"]
    assert not (out / "raw").exists()
    assert any("libx264" in c for call in ffmpeg.calls for c in call)


def test_options_reach_godot(tmp_path):
    media, _ = good_media()
    seen = []
    render(
        plan(tmp_path, quit_after=38, always_on_top=True, godot_arguments=("--position", "5119,2159")),
        media=media,
        runner=fake_godot(frames=8, size=SIZE, seen=seen),
    )
    command = seen[0]
    assert command.index("--always-on-top") < command.index("--write-movie")
    assert command[command.index("--quit-after") + 1] == "38"
    assert command[command.index("--position") + 1] == "5119,2159"
    assert command.index("--position") < command.index("--")


def test_a_shot_log_gives_the_start_from_its_counter(tmp_path):
    media, _ = good_media()
    log = GOOD_LOG.replace("RUN_START_FRAME 1", "SHOT_FRAME 0 COUNTER 3\nSHOT_FRAME 4 COUNTER 7")
    render(plan(tmp_path, start_frame=lambda text: shot_start_frame(text, 0)), media=media, runner=fake_godot(frames=9, size=SIZE, log=log))
    assert json.loads((tmp_path / "out" / "manifest.json").read_text())["firstMovieFrame"] == 3


def test_audio_is_trimmed_and_encoded(tmp_path):
    media, ffmpeg = good_media()
    ffmpeg.audio_streams = 1
    render(plan(tmp_path, audio=True), media=media, runner=fake_godot(frames=8, size=SIZE, audio_rate=48000))
    assert (tmp_path / "out" / "audio.wav").exists()
    names = [o["path"] for o in json.loads((tmp_path / "out" / "manifest.json").read_text())["outputs"]]
    assert names == ["frames.sha256", "audio.wav", "footage.mp4", "contact_sheet.png"]
    encode = next(c for c in ffmpeg.calls if "libx264" in c)
    assert encode.count("-i") == 2 and "aac" in encode


def test_audio_missing_is_an_error(tmp_path):
    media, _ = good_media()
    with pytest.raises(RenderError, match="wrote no audio"):
        render(plan(tmp_path, audio=True), media=media, runner=fake_godot(frames=8, size=SIZE))


def test_without_encoding_only_the_frames_and_their_hashes_are_made(tmp_path):
    media, ffmpeg = good_media()
    result = render(plan(tmp_path, encode=False, keep_frames=False, hash_prefix="frames/"), media=media, runner=fake_godot(frames=8, size=SIZE))
    out = tmp_path / "out"
    assert sorted(p.name for p in out.iterdir()) == ["frames.sha256", "godot.log", "manifest.json"]
    assert [o["path"] for o in result.manifest["outputs"]] == ["frames.sha256"]
    assert "  frames/frame00000000.png" in (out / "frames.sha256").read_text()
    assert not any("libx264" in c for call in ffmpeg.calls for c in call)


def test_two_renders_can_be_compared_for_determinism(tmp_path):
    media, _ = good_media()
    for name in ("a", "b"):
        render(plan(tmp_path, output=tmp_path / name, encode=False), media=media, runner=fake_godot(frames=8, size=SIZE))
    assert compare_frame_hashes(tmp_path / "a" / "frames.sha256", tmp_path / "b" / "frames.sha256") == (5, [])


def test_extra_manifest_fields_and_the_contact_tile(tmp_path):
    media, ffmpeg = good_media()
    ffmpeg.video = ffmpeg.stream(60, (18, 32), 5)
    result = render(plan(tmp_path, size=(18, 32), manifest_extra={"clip": "intro_9x16"}), media=media, runner=fake_godot(frames=8, size=(18, 32)))
    assert result.manifest["extra"] == {"clip": "intro_9x16"}
    sheet = next(c for c in ffmpeg.calls if "-frames:v" in c)
    assert "scale=270:480" in sheet[sheet.index("-vf") + 1]
    media2, ffmpeg2 = good_media()
    render(plan(tmp_path, output=tmp_path / "again", contact_tile="100:-2"), media=media2, runner=fake_godot(frames=8, size=SIZE))
    sheet = next(c for c in ffmpeg2.calls if "-frames:v" in c)
    assert "scale=100:-2" in sheet[sheet.index("-vf") + 1]


# What a render refuses, and what it leaves behind.


def test_an_opengl_render_is_refused_with_the_log_kept(tmp_path):
    media, _ = good_media()
    log = "OpenGL API 3.3.0 - Compatibility - Using Device: Synthetic\nRUN_START_FRAME 1\n"
    with pytest.raises(RenderError, match="not Forward\\+ on Vulkan, Metal or D3D12: OpenGL API"):
        render(plan(tmp_path), media=media, runner=fake_godot(frames=8, size=SIZE, log=log))
    assert (tmp_path / "out" / "godot.log").read_text() == log
    assert (tmp_path / "out" / "raw").exists()  # the partial output stays for inspection
    assert not (tmp_path / "out" / "frames").exists()


def test_the_forward_plus_check_can_be_switched_off(tmp_path):
    media, _ = good_media()
    log = "OpenGL API 3.3.0 - Compatibility - Using Device: Synthetic\nRUN_START_FRAME 1\n"
    result = render(plan(tmp_path, forward_plus=False), media=media, runner=fake_godot(frames=8, size=SIZE, log=log))
    assert result.manifest["rendererLine"].startswith("OpenGL API")


@pytest.mark.parametrize("marker", ["SCRIPT ERROR", "SHADER ERROR", "Parse Error"])
def test_a_log_with_errors_is_refused_even_when_godot_exited_zero(tmp_path, marker):
    media, _ = good_media()
    with pytest.raises(RenderError, match=marker):
        render(plan(tmp_path), media=media, runner=fake_godot(frames=8, size=SIZE, log=GOOD_LOG + f"{marker}: boom\n", exit_code=0))


def test_the_caller_can_judge_the_run(tmp_path):
    media, _ = good_media()

    def check(run):
        if run.exit_code != 0:
            raise RenderError(f"the run exited {run.exit_code}")

    with pytest.raises(RenderError, match="the run exited 3"):
        render(plan(tmp_path, check_run=check), media=media, runner=fake_godot(frames=8, size=SIZE, exit_code=3))
    assert not (tmp_path / "out" / "frames").exists()


def test_too_few_frames(tmp_path):
    media, _ = good_media()
    with pytest.raises(RenderError, match="Movie Maker wrote 5 frames; the run needs frames 1 to 5"):
        render(plan(tmp_path), media=media, runner=fake_godot(frames=5, size=SIZE))


def test_a_missing_start_marker(tmp_path):
    media, _ = good_media()
    with pytest.raises(RenderError, match="found 0"):
        render(plan(tmp_path), media=media, runner=fake_godot(frames=8, size=SIZE, log=GOOD_LOG.replace("RUN_START_FRAME 1", "")))


def test_wrong_size_frames(tmp_path):
    media, _ = good_media()
    with pytest.raises(RenderError, match="first frame is 64x36, expected 32x18"):
        render(plan(tmp_path), media=media, runner=fake_godot(frames=8, size=(64, 36)))


def test_a_blank_last_frame_fails_the_render_and_keeps_the_frames(tmp_path):
    media, ffmpeg = good_media()
    ffmpeg.deviations["frame00000004.png"] = 0.0
    with pytest.raises(RenderError, match="last frame is blank"):
        render(plan(tmp_path), media=media, runner=fake_godot(frames=8, size=SIZE))
    assert len(list((tmp_path / "out" / "frames").iterdir())) == 5
    assert not (tmp_path / "out" / "footage.mp4").exists()


def test_a_wrong_encode_fails_the_render(tmp_path):
    media, ffmpeg = fake_media()
    ffmpeg.video = ffmpeg.stream(60, SIZE, 4)  # one frame short
    with pytest.raises(RenderError, match="nb_read_frames is 4, expected 5"):
        render(plan(tmp_path), media=media, runner=fake_godot(frames=8, size=SIZE))


def test_ffmpeg_errors_fail_the_render(tmp_path):
    media, ffmpeg = good_media()
    ffmpeg.fail_on = str(tmp_path / "out" / "footage.mp4")
    with pytest.raises(RenderError, match="ffmpeg failed: synthetic failure"):
        render(plan(tmp_path), media=media, runner=fake_godot(frames=8, size=SIZE))


def test_a_non_empty_output_folder_is_refused_before_godot_runs(tmp_path):
    media, _ = good_media()
    (tmp_path / "out").mkdir()
    (tmp_path / "out" / "old.txt").write_text("keep me")
    ran = []
    with pytest.raises(RenderError, match="is not empty"):
        render(plan(tmp_path), media=media, runner=fake_godot(frames=8, size=SIZE, seen=ran))
    assert ran == [] and (tmp_path / "out" / "old.txt").read_text() == "keep me"


def test_an_existing_empty_output_folder_is_fine(tmp_path):
    media, _ = good_media()
    (tmp_path / "out").mkdir()
    render(plan(tmp_path), media=media, runner=fake_godot(frames=8, size=SIZE))


def test_missing_tools_are_reported_before_anything_else(tmp_path):
    media, _ = fake_media(tools=False)
    ran = []
    with pytest.raises(RenderError, match="ffmpeg not found"):
        render(plan(tmp_path), media=media, runner=fake_godot(frames=8, size=SIZE, seen=ran))
    assert ran == [] and not (tmp_path / "out").exists()


@pytest.mark.parametrize("field, value", [("fps", 0), ("frame_count", 0)])
def test_nonsense_plans(tmp_path, field, value):
    media, _ = good_media()
    with pytest.raises(RenderError, match="must be positive"):
        render(plan(tmp_path, **{field: value}), media=media, runner=fake_godot(frames=8, size=SIZE))


def test_a_stale_override_stops_the_render_before_godot_starts(tmp_path):
    media, _ = good_media()
    (tmp_path / "game").mkdir()
    (tmp_path / "game" / "override.cfg").write_text("[display]\n")
    ran = []
    with pytest.raises(RenderError, match="another render is running"):
        render(plan(tmp_path), media=media, runner=fake_godot(frames=8, size=SIZE, seen=ran))
    assert ran == []


def test_the_override_is_written_for_the_run_and_removed_after(tmp_path):
    media, _ = good_media()
    override = tmp_path / "game" / "override.cfg"
    during = []

    def look(command):
        during.append(override.read_text(encoding="utf-8"))

    render(plan(tmp_path, write_override=True), media=media, runner=fake_godot(frames=8, size=SIZE, inspect=look))
    assert "window/size/viewport_width=32" in during[0] and "window/size/viewport_height=18" in during[0]
    assert not override.exists()


def test_no_override_is_written_unless_asked(tmp_path):
    media, _ = good_media()
    override = tmp_path / "game" / "override.cfg"
    existed = []
    render(plan(tmp_path), media=media, runner=fake_godot(frames=8, size=SIZE, inspect=lambda c: existed.append(override.exists())))
    assert existed == [False]


def test_a_failed_run_still_removes_the_override(tmp_path):
    media, _ = good_media()

    def boom(command):
        raise RenderError("godot blew up")

    with pytest.raises(RenderError, match="blew up"):
        render(plan(tmp_path, write_override=True), media=media, runner=boom)
    assert not (tmp_path / "game" / "override.cfg").exists()


def test_a_custom_override_path(tmp_path):
    media, _ = good_media()
    custom = tmp_path / "elsewhere.cfg"
    seen = []
    render(plan(tmp_path, override_path=custom, write_override=True), media=media, runner=fake_godot(frames=8, size=SIZE, inspect=lambda c: seen.append(custom.exists())))
    assert seen == [True] and not custom.exists()


def test_the_default_runner_goes_through_run_godot(tmp_path, monkeypatch):
    media, _ = good_media()
    calls = []

    def run_godot(command, **kwargs):
        calls.append((command, kwargs))
        return fake_godot(frames=8, size=SIZE)(command)

    monkeypatch.setattr("uppercut_godot_render.pipeline._godot.run_godot", run_godot)
    render(plan(tmp_path, environment={"VK_ICD_FILENAMES": "/x"}, timeout=99), media=media)
    assert calls[0][1] == {"environment": {"VK_ICD_FILENAMES": "/x"}, "timeout": 99}
    assert Path(calls[0][0][0]).name == "godot"
