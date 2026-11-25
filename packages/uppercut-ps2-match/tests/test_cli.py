import json
import re

import pytest

from ps2_support import AT, JR_RA, NOP, V0, fake_toolchain_args, image_elf, le_words, lui, lw
from test_fetch import FILES, make_tar, sha
from test_verify import F_ADDR, F_ASM, F_CODE, G_ADDR, TABLE
from uppercut_ps2_match import __version__
from uppercut_ps2_match.cli import main

CONFIG = {"gp": "0x500000", "image": "orig.elf", "symbols": ["symbols.txt"], "flags": "-O2 -G8", "toolchain": fake_toolchain_args()}


@pytest.fixture
def project(tmp_path):
    (tmp_path / "orig.elf").write_bytes(image_elf((F_ADDR, F_CODE + b"\0" * 16), (G_ADDR, le_words(JR_RA, NOP)), (TABLE, le_words(0x11223344))))
    (tmp_path / "symbols.txt").write_text(f"f = 0x{F_ADDR:x};\ng = 0x{G_ADDR:x};\n")
    (tmp_path / "verify.json").write_text(json.dumps(CONFIG))
    (tmp_path / "good.c").write_text(F_ASM)
    (tmp_path / "bad.c").write_text(F_ASM.replace("lw $2", "lw $3"))
    return tmp_path


def test_verify_prints_results_and_a_summary(project, capsys):
    assert main(["verify", str(project / "verify.json"), str(project / "good.c"), "--work", str(project / "w"), "--json", str(project / "r.jsonl")]) == 0
    out = capsys.readouterr().out
    assert '"status": "match"' in out and "1/1 match, 16 bytes" in out
    (line,) = (project / "r.jsonl").read_text().splitlines()
    assert json.loads(line)["name"] == "f" and "seconds" in json.loads(line)


def test_verify_exit_status_and_quiet(project, capsys):
    code = main(["verify", str(project / "verify.json"), str(project / "good.c"), str(project / "bad.c"), "--work", str(project / "w"), "--quiet"])
    out = capsys.readouterr().out
    assert code == 1 and "1/2 match" in out and out.count('"status"') == 1 and '"mismatch"' in out


def test_verify_prefer_range_and_missing_config(project, capsys):
    assert main(["verify", str(project / "verify.json"), str(project / "good.c"), "--work", str(project / "w"), "--prefer-range", "100000:200000"]) == 0
    assert main(["verify", str(project / "nope.json"), str(project / "good.c"), "--work", str(project / "w")]) == 2
    assert "uppercut-ps2-match verify:" in capsys.readouterr().err


def test_csplit_lists_functions(project, capsys):
    (project / "unit.c").write_text("int a;\nint f(void) { return 1; }\nint g(void) { return 2; }\n")
    assert main(["csplit", str(project / "unit.c")]) == 0
    assert "1 prelude chunks; functions: ['f', 'g']" in capsys.readouterr().out


def test_fetch_dry_run_and_bad_config(tmp_path, capsys):
    archive = make_tar(FILES)
    cfg = tmp_path / "toolchain.json"
    cfg.write_text(json.dumps({"packages": [{"name": "compiler", "url": "https://example.invalid/c.tar.gz", "sha256": sha(archive)}]}))
    assert main(["fetch", str(cfg), "--dest", str(tmp_path / "t"), "--dry-run"]) == 0
    assert "compiler" in capsys.readouterr().out and not (tmp_path / "t").exists()
    cfg.write_text("{}")
    assert main(["fetch", str(cfg), "--dest", str(tmp_path / "t")]) == 2
    assert "packages" in capsys.readouterr().err


def test_audit_exit_status_report_and_config(tmp_path, capsys):
    root = tmp_path / "repo"
    root.mkdir()
    (root / "ok.c").write_text("int a;\n")
    (root / "disc.iso").write_text("x")
    out_file = tmp_path / "report.md"
    assert main(["audit", "--root", str(root), "--out", str(out_file)]) == 1
    assert "1 findings." in out_file.read_text() and "disc.iso" in capsys.readouterr().out
    cfg = tmp_path / "audit.json"
    cfg.write_text(json.dumps({"publish": ["ok.c"], "table_extension": ".tbl", "table_row": r"^\d+ \w+$"}))
    assert main(["audit", "--root", str(root), "--config", str(cfg), "--publish"]) == 0
    extra = tmp_path / "extra"
    extra.mkdir()
    (extra / "x.wav").write_text("x")
    assert main(["audit", "--root", str(root), "--config", str(cfg), "--publish", "--extra", f"{extra}=vendor/"]) == 1
    assert "vendor/x.wav" in capsys.readouterr().out


def test_version(capsys):
    with pytest.raises(SystemExit) as stop:
        main(["--version"])
    assert stop.value.code == 0 and __version__ in capsys.readouterr().out
    assert re.fullmatch(r"\d+\.\d+\.\d+", __version__)
