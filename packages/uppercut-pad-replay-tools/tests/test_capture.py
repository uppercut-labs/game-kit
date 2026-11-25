import pytest

from uppercut_pad_replay_tools import CaptureError, CaptureScript


def test_wait_shot_quit():
    script = CaptureScript().wait(600).shot("shapes-sixteen").wait(45).quit()
    assert script.text() == "wait 600\nshot shapes-sixteen\nwait 45\nquit\n"


def test_shots_at_turns_frames_into_waits():
    script = CaptureScript().shots_at([(240, "one-last-slot"), (330, "sparse-two"), (550, "select-eight")])
    assert script.lines == [
        "wait 240", "shot one-last-slot", "wait 90", "shot sparse-two", "wait 220", "shot select-eight", "wait 45", "quit"
    ]


def test_shots_at_sorts_by_frame_and_takes_a_start_and_a_tail():
    script = CaptureScript().shots_at([(150, "b"), (100, "a")], tail=60, start=50)
    assert script.lines == ["wait 50", "shot a", "wait 50", "shot b", "wait 60", "quit"]
    with pytest.raises(CaptureError):
        CaptureScript().shots_at([(10, "early")], start=20)


def test_same_frame_shots_keep_a_zero_wait():
    assert CaptureScript().shots_at([(5, "a"), (5, "b")], tail=1).lines == [
        "wait 5", "shot a", "wait 0", "shot b", "wait 1", "quit"
    ]


def test_bad_arguments():
    with pytest.raises(CaptureError):
        CaptureScript().wait(-1)
    with pytest.raises(CaptureError):
        CaptureScript().wait(1.5)  # type: ignore[arg-type]
    for name in ("", "two words", "line\nbreak", "slash/name"):
        with pytest.raises(CaptureError):
            CaptureScript().shot(name)
    with pytest.raises(CaptureError):
        CaptureScript().raw("two\nlines")


def test_raw_and_write(tmp_path):
    script = CaptureScript().raw("1 Circle").wait(5).quit()
    path = script.write(tmp_path / "capture.txt")
    assert path.read_bytes() == b"1 Circle\nwait 5\nquit\n"
    with pytest.raises(CaptureError):
        CaptureScript().write(tmp_path / "empty.txt")
