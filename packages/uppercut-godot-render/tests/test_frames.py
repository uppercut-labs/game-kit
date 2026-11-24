import hashlib

import pytest

from fakes import fake_media, make_png
from uppercut_godot_render import (
    RenderError,
    compare_frame_hashes,
    png_size,
    sha256_file,
    trim_frames,
    verify_frames,
    write_frame_hashes,
)


def write_frames(folder, count, size=(32, 18), *, prefix="frame", start=0):
    folder.mkdir(parents=True, exist_ok=True)
    for index in range(count):
        (folder / f"{prefix}{start + index:08d}.png").write_bytes(make_png(size[0], size[1], tag=index))


def test_sha256_file(tmp_path):
    path = tmp_path / "x.bin"
    data = bytes(range(256)) * 9000  # more than one read block
    path.write_bytes(data)
    assert sha256_file(path) == hashlib.sha256(data).hexdigest()


def test_png_size_reads_the_header(tmp_path):
    path = tmp_path / "a.png"
    path.write_bytes(make_png(1080, 1920))
    assert png_size(path) == (1080, 1920)
    path.write_bytes(b"not a png at all, just text of the right length")
    with pytest.raises(RenderError, match="is not a PNG"):
        png_size(path)


def test_trim_moves_the_runs_frames_and_leaves_the_rest(tmp_path):
    raw, frames = tmp_path / "raw", tmp_path / "frames"
    write_frames(raw, 10)
    before = {index: (raw / f"frame{index:08d}.png").read_bytes() for index in range(10)}

    moved = trim_frames(raw, frames, 2, 5)

    assert [p.name for p in moved] == [f"frame{i:08d}.png" for i in range(5)]
    for index in range(5):
        assert (frames / f"frame{index:08d}.png").read_bytes() == before[index + 2]
    assert sorted(p.name for p in raw.iterdir()) == [f"frame{i:08d}.png" for i in (0, 1, 7, 8, 9)]


def test_trim_needs_enough_frames(tmp_path):
    raw = tmp_path / "raw"
    write_frames(raw, 6)
    with pytest.raises(RenderError, match="Movie Maker wrote 6 frames; the run needs frames 3 to 7"):
        trim_frames(raw, tmp_path / "frames", 3, 5)
    assert len(list(raw.iterdir())) == 6  # nothing moved


@pytest.mark.parametrize("first, count", [(-1, 3), (0, 0), (0, -2)])
def test_trim_rejects_nonsense(tmp_path, first, count):
    write_frames(tmp_path / "raw", 4)
    with pytest.raises(RenderError, match="cannot trim"):
        trim_frames(tmp_path / "raw", tmp_path / "frames", first, count)


def test_trim_ignores_other_files_in_raw(tmp_path):
    raw = tmp_path / "raw"
    write_frames(raw, 3)
    (raw / "frame.wav").write_bytes(b"audio")
    trim_frames(raw, tmp_path / "frames", 0, 3)
    assert (raw / "frame.wav").exists()


def test_verify_accepts_a_good_run(tmp_path):
    media, ffmpeg = fake_media()
    write_frames(tmp_path, 5)
    assert len(verify_frames(tmp_path, 5, (32, 18), media)) == 5
    checked = [call[call.index("-i") + 1] for call in ffmpeg.calls]
    assert [c.rsplit("/", 1)[-1] for c in checked] == ["frame00000000.png", "frame00000004.png"]  # first and last only


def test_verify_checks_the_count_and_an_empty_run(tmp_path):
    media, _ = fake_media()
    write_frames(tmp_path, 4)
    with pytest.raises(RenderError, match="expected 5 frames, found 4"):
        verify_frames(tmp_path, 5, (32, 18), media)
    with pytest.raises(RenderError, match="the run has no frames"):
        verify_frames(tmp_path / "empty", 0, (32, 18), media)


def test_verify_checks_the_size_of_the_first_and_last_frame(tmp_path):
    media, _ = fake_media()
    write_frames(tmp_path, 3, size=(32, 18))
    (tmp_path / "frame00000002.png").write_bytes(make_png(32, 17, tag=2))
    with pytest.raises(RenderError, match="last frame is 32x17, expected 32x18"):
        verify_frames(tmp_path, 3, (32, 18), media)
    (tmp_path / "frame00000000.png").write_bytes(make_png(18, 32, tag=0))
    with pytest.raises(RenderError, match="first frame is 18x32, expected 32x18"):
        verify_frames(tmp_path, 3, (32, 18), media)


def test_verify_checks_for_blank_frames(tmp_path):
    media, ffmpeg = fake_media()
    write_frames(tmp_path, 3)
    ffmpeg.deviations["frame00000002.png"] = 0.0
    with pytest.raises(RenderError, match=r"last frame is blank \(deviation 0.00\)"):
        verify_frames(tmp_path, 3, (32, 18), media)
    ffmpeg.deviations["frame00000002.png"] = 1.0
    with pytest.raises(RenderError, match="last frame is blank"):
        verify_frames(tmp_path, 3, (32, 18), media)
    ffmpeg.deviations["frame00000002.png"] = 2.0
    verify_frames(tmp_path, 3, (32, 18), media)  # the threshold is "below 2.0"


def test_a_dark_scene_can_lower_the_blank_threshold(tmp_path):
    media, ffmpeg = fake_media()
    write_frames(tmp_path, 3)
    ffmpeg.deviations["frame00000000.png"] = 1.0
    with pytest.raises(RenderError, match="first frame is blank"):
        verify_frames(tmp_path, 3, (32, 18), media)
    verify_frames(tmp_path, 3, (32, 18), media, min_deviation=0.4)


def test_frame_hashes_file_format(tmp_path):
    write_frames(tmp_path / "frames", 3)
    pairs = write_frame_hashes(tmp_path / "frames", tmp_path / "plain.sha256")
    write_frame_hashes(tmp_path / "frames", tmp_path / "paths.sha256", prefix="frames/")
    expected = [(hashlib.sha256((tmp_path / "frames" / f"frame{i:08d}.png").read_bytes()).hexdigest(), f"frame{i:08d}.png") for i in range(3)]
    assert pairs == expected
    assert (tmp_path / "plain.sha256").read_text() == "".join(f"{h}  {n}\n" for h, n in expected)
    assert (tmp_path / "paths.sha256").read_text() == "".join(f"{h}  frames/{n}\n" for h, n in expected)
    assert len({h for h, _ in pairs}) == 3


def test_comparing_hash_files_for_determinism(tmp_path):
    for name, tags in (("a", (0, 1, 2)), ("b", (0, 9, 2))):
        folder = tmp_path / name
        folder.mkdir()
        for index, tag in enumerate(tags):
            (folder / f"frame{index:08d}.png").write_bytes(make_png(8, 8, tag=tag))
        write_frame_hashes(folder, tmp_path / f"{name}.sha256")
    assert compare_frame_hashes(tmp_path / "a.sha256", tmp_path / "a.sha256") == (3, [])
    assert compare_frame_hashes(tmp_path / "a.sha256", tmp_path / "b.sha256") == (3, ["frame00000001.png"])
    (tmp_path / "short.sha256").write_text("x  frame00000000.png\n")
    with pytest.raises(RenderError, match="lists 3 frames and"):
        compare_frame_hashes(tmp_path / "a.sha256", tmp_path / "short.sha256")
