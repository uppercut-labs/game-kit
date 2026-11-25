from uppercut_pad_replay_tools import Replay, main


def test_check_passes_and_fails(tmp_path, capsys):
    good = Replay().event(0, 0).tap(5, 0, 8).write(tmp_path / "good.input")
    bad = tmp_path / "bad.input"
    bad.write_text("0 0 0000 128 128 1\nnot a line\n")
    assert main([str(good)]) == 0
    assert "PASS" in capsys.readouterr().out
    assert main([str(good), str(bad), str(tmp_path / "missing.input")]) == 1
    err = capsys.readouterr().err
    assert "bad.input: line 2" in err and "missing.input" in err


def test_the_cap_and_slots_flags(tmp_path):
    path = Replay().event(0, 0).tap(1, 0, 1).write(tmp_path / "x.input")
    assert main([str(path), "--max-events", "2"]) == 1
    assert main([str(path), "--max-events", "3"]) == 0
    assert main([str(path), "--slots", "1"]) == 0
