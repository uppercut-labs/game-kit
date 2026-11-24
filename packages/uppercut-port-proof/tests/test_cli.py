import json
import struct
import zlib

from port_proof_synthetic import FakeDebugServer, ppm_bytes, silence, tone, wav_bytes
from uppercut_port_proof.cli import main


def test_wav_stats_prints_a_line_or_json(tmp_path, capsys):
    path = tmp_path / "a.wav"
    path.write_bytes(wav_bytes(tone(1.0) + silence(1.2)))
    assert main(["wav-stats", str(path)]) == 0
    out = capsys.readouterr().out
    assert out.startswith(f"{path}: 2.20 s, 8000 Hz, 1 ch;") and "silent stretches (>= 1 s): 1.2 s at 1.0 s" in out
    assert main(["wav-stats", str(path), "--json", "--silence-db", "-80"]) == 0
    data = json.loads(capsys.readouterr().out)
    assert data["silence_threshold_dbfs"] == -80 and data["frames"] == 17600
    path2 = tmp_path / "b.wav"
    path2.write_bytes(wav_bytes(tone(0.5)))
    assert main(["wav-stats", str(path), str(path2), "--json"]) == 0
    assert [d["file"] for d in json.loads(capsys.readouterr().out)] == [str(path), str(path2)]


def test_a_bad_file_exits_2_with_a_message(tmp_path, capsys):
    (tmp_path / "x.wav").write_bytes(b"not a wav at all")
    assert main(["wav-stats", str(tmp_path / "x.wav")]) == 2
    assert "uppercut-port-proof wav-stats: not a WAV" in capsys.readouterr().err
    assert main(["wav-stats", str(tmp_path / "missing.wav")]) == 2


def test_md5_compare_prints_the_summary_and_writes_the_lines(tmp_path, capsys):
    for folder, data in (("a", b"1"), ("b", b"2")):
        (tmp_path / folder).mkdir()
        (tmp_path / folder / "frame_1.ppm").write_bytes(data)
    assert main(["md5-compare", str(tmp_path / "a"), str(tmp_path / "b"), str(tmp_path / "out.txt")]) == 0
    assert capsys.readouterr().out == "1 frames in A: 0 identical, 1 different, 0 missing in B\n"
    assert (tmp_path / "out.txt").read_text().endswith("DIFFERENT\n1 frames in A: 0 identical, 1 different, 0 missing in B\n")
    assert main(["md5-compare", str(tmp_path / "a"), str(tmp_path / "b"), "--strict"]) == 1


def test_ppm2png_and_ppm_crop(tmp_path, capsys):
    (tmp_path / "frame_1.ppm").write_bytes(ppm_bytes(8, 6))
    assert main(["ppm2png", str(tmp_path / "png"), str(tmp_path / "frame_1.ppm")]) == 0
    line = capsys.readouterr().out.split()
    assert line[1:] == ["frame_1", "8", "6"] and len(line[0]) == 32
    png = (tmp_path / "png" / "frame_1.png").read_bytes()
    assert png[:8] == b"\x89PNG\r\n\x1a\n" and struct.unpack(">II", png[16:24]) == (8, 6)
    assert main(["ppm-crop", str(tmp_path / "frame_1.ppm"), str(tmp_path / "c.png"), "1", "1", "5", "4", "3", "--stored"]) == 0
    crop = (tmp_path / "c.png").read_bytes()
    assert struct.unpack(">II", crop[16:24]) == (12, 9)
    z = crop[crop.index(b"IDAT") + 4 :]
    assert zlib.decompress(z[: z.index(b"IEND") - 8])  # stored blocks inflate like any other
    assert main(["ppm-crop", str(tmp_path / "frame_1.ppm"), str(tmp_path / "d.png"), "0", "0", "99", "4"]) == 2


def test_presses2script(tmp_path, capsys):
    (tmp_path / "p.txt").write_text("2:start:3\n6:cross+up:2\np2 4:start\n")
    assert main(["presses2script", str(tmp_path / "p.txt"), "10", str(tmp_path / "o.txt"), "--shot-every", "5", "--prefix", "p"]) == 0
    assert capsys.readouterr().out == f"wrote {tmp_path / 'o.txt'}: 8 lines, 2 shots\n"
    assert (tmp_path / "o.txt").read_text() == "wait 2\n3 Start\nshot p00005\nwait 1\n2 Cross+Up\nwait 2\nshot p00010\nquit\n"


def test_drive(tmp_path, capsys):
    (tmp_path / "p.txt").write_text("10:start\n")
    with FakeDebugServer(step=25) as server:
        assert main(["drive", "--presses", str(tmp_path / "p.txt"), "--until", "100", "--shot-every", "60",
                     "--out", str(tmp_path / "shots"), "--port", str(server.port), "--quit"]) == 0
    out = capsys.readouterr().out
    assert "press start for 4" in out and "shot shot_000060.png" in out and out.rstrip().endswith("done at frame 100")
    assert server.commands()[-1] == "quit"
