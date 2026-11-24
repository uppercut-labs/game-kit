import hashlib

import pytest

from uppercut_port_proof import ProofError, compare_frame_dirs, write_report


def _put(folder, name, data):
    folder.mkdir(exist_ok=True)
    (folder / name).write_bytes(data)


def test_compares_each_frame_of_a_by_md5(tmp_path):
    a, b = tmp_path / "a", tmp_path / "b"
    _put(a, "frame_000001.ppm", b"one")
    _put(a, "frame_000002.ppm", b"two")
    _put(a, "frame_000003.ppm", b"three")
    _put(a, "notes.txt", b"not a frame")
    _put(b, "frame_000001.ppm", b"one")
    _put(b, "frame_000002.ppm", b"TWO")
    _put(b, "frame_000009.ppm", b"extra")
    result = compare_frame_dirs(a, b)
    assert [(r.name, r.status) for r in result.rows] == [
        ("frame_000001.ppm", "same"), ("frame_000002.ppm", "different"), ("frame_000003.ppm", "missing")]
    assert (result.same, result.different, result.missing) == (1, 1, 1)
    assert result.only_in_b == ["frame_000009.ppm"]
    assert not result.identical
    assert result.summary() == "3 frames in A: 1 identical, 1 different, 1 missing in B"
    md5 = lambda d: hashlib.md5(d).hexdigest()
    assert result.lines() == [
        f"frame_000001.ppm {md5(b'one')} same",
        f"frame_000002.ppm {md5(b'two')} {md5(b'TWO')} DIFFERENT",
        f"frame_000003.ppm {md5(b'three')} MISSING",
        "frame_000009.ppm only in B",
        "3 frames in A: 1 identical, 1 different, 1 missing in B",
    ]


def test_identical_runs(tmp_path):
    for folder in ("a", "b"):
        _put(tmp_path / folder, "frame_000001.ppm", b"x")
    result = compare_frame_dirs(tmp_path / "a", tmp_path / "b")
    assert result.identical and result.summary().startswith("1 frames in A: 1 identical")
    write_report(result, tmp_path / "report.txt")
    assert (tmp_path / "report.txt").read_text().endswith("1 frames in A: 1 identical, 0 different, 0 missing in B\n")


def test_prefix_and_suffix_are_options(tmp_path):
    _put(tmp_path / "a", "shot_1.png", b"x")
    _put(tmp_path / "b", "shot_1.png", b"y")
    assert compare_frame_dirs(tmp_path / "a", tmp_path / "b", prefix="shot_", suffix=".png").different == 1
    assert compare_frame_dirs(tmp_path / "a", tmp_path / "b").rows == []


def test_a_folder_that_is_not_there_is_refused(tmp_path):
    (tmp_path / "a").mkdir()
    with pytest.raises(ProofError, match="not a folder"):
        compare_frame_dirs(tmp_path / "a", tmp_path / "nope")
