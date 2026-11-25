import hashlib
import re
import subprocess

import pytest

from uppercut_ps2_match.audit import AuditConfig, audit_file, audit_tree

PNG = b"\x89PNG\r\n\x1a\n" + b"\0" * 8
LISTING = "".join(f"/* {i:04X} 0010{i:04X} 0000000{i % 10} */ nop\n" for i in range(12))


def write(root, rel, data):
    path = root / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data if isinstance(data, bytes) else data.encode())
    return path


def kinds(root, cfg=None, **kw):
    report = audit_tree([(root, "")], cfg, **kw)
    return {k: [rel for rel, _ in v] for k, v in report.findings.items()}, report


def test_clean_tree(tmp_path):
    write(tmp_path, "src/main.c", "int main(void) { return 0; }\n")
    write(tmp_path, "README.md", "# demo\n")
    found, report = kinds(tmp_path)
    assert found == {} and report.exit_status == 0 and report.total == 2
    assert "**Result: clean.**" in report.markdown()


def test_binaries_by_magic_and_by_nul(tmp_path):
    write(tmp_path, "a.dat", b"\x7fELF" + b"\0" * 20)
    write(tmp_path, "b.dat", b"plain" + b"\0" + b"more")
    write(tmp_path, "c.dat", b"PK\x03\x04rest")
    found, report = kinds(tmp_path)
    assert sorted(set(found["binary"])) == ["a.dat", "b.dat", "c.dat"] and report.exit_status == 1


def test_assets_by_extension_and_artwork_by_name(tmp_path):
    write(tmp_path, "sound.wav", "x")
    write(tmp_path, "game.iso", "x")
    write(tmp_path, "obj.o", "x")
    write(tmp_path, "docs/logo.txt", "x")
    write(tmp_path, "pic.png", PNG)
    found, _ = kinds(tmp_path)
    assert found["audio"] == ["sound.wav"] and found["disc image"] == ["game.iso"] and found["executable or object"] == ["obj.o"]
    assert found["logo or artwork"] == ["docs/logo.txt"] and "pic.png" in found["image"]


def test_assembly_listings_macros_and_extensions(tmp_path):
    write(tmp_path, "list.txt", LISTING)
    write(tmp_path, "unit.c", 'INCLUDE_ASM("asm/nonmatchings", func);\n')
    write(tmp_path, "boot.s", "nop\n")
    write(tmp_path, "short.txt", LISTING.splitlines(True)[0])
    found, _ = kinds(tmp_path)
    assert sorted(found["assembly"]) == ["boot.s", "list.txt", "unit.c"]


def test_disc_data_in_source(tmp_path):
    cfg = AuditConfig(max_literals=8)
    write(tmp_path, "tab.c", "int t[] = { " + ", ".join(str(i) for i in range(9)) + " };\n")
    write(tmp_path, "ok.c", "int t[] = { " + ", ".join(str(i) for i in range(8)) + " };\n")
    write(tmp_path, "blob.h", "const char *b = \"" + "A" * 2100 + "\";\n")
    found, _ = kinds(tmp_path, cfg)
    assert sorted(found["disc data"]) == ["blob.h", "tab.c"]


def test_compiler_dumps(tmp_path):
    write(tmp_path, "unit.i", "int x;\n")
    write(tmp_path, "unit.c.greg", "x\n")
    found, _ = kinds(tmp_path)
    assert sorted(found["compiler dump"]) == ["unit.c.greg", "unit.i"]


def test_screenshots_are_listed_not_flagged(tmp_path):
    write(tmp_path, "screenshots/title.png", PNG)
    write(tmp_path, "screenshots/fake.png", b"not a png")
    write(tmp_path, "screenshots/logo-shot.png", PNG)
    write(tmp_path, "screenshots/big.png", PNG + b"\0" * 100)
    write(tmp_path, "screenshots/anim.gif", b"GIF89a")
    write(tmp_path, "elsewhere/title.png", PNG)
    cfg = AuditConfig(screenshot_max=50)
    found, report = kinds(tmp_path, cfg)
    assert [rel for rel, _ in report.screenshots] == ["screenshots/title.png"]
    assert sorted(found["image"]) == ["elsewhere/title.png", "screenshots/anim.gif", "screenshots/big.png", "screenshots/fake.png", "screenshots/logo-shot.png"]
    assert "## Screenshots (allowed, 1)" in report.markdown()


def test_known_files_pass_only_with_their_digest(tmp_path):
    data = b"font bytes"
    write(tmp_path, "fonts/ui.ttf", data)
    cfg = AuditConfig(known={"fonts/ui.ttf": (hashlib.sha256(data).hexdigest(), "our own UI font")})
    found, report = kinds(tmp_path, cfg)
    assert found == {} and report.known == [("fonts/ui.ttf", "our own UI font")]
    write(tmp_path, "fonts/ui.ttf", b"changed")
    found, _ = kinds(tmp_path, cfg)
    assert found["font"] == ["fonts/ui.ttf"]


def test_publish_set_limits_the_first_tree(tmp_path):
    write(tmp_path, "pub/a.c", "int a;\n")
    write(tmp_path, "pub/skip.wav", "x")
    write(tmp_path, "private/b.wav", "x")
    write(tmp_path, "top.wav", "x")
    cfg = AuditConfig(publish=["pub/", "top.wav"], publish_exclude=["pub/skip.wav"])
    found, report = kinds(tmp_path, cfg, publish_first=True)
    assert found == {"audio": ["top.wav"]} and report.total == 2
    found, report = kinds(tmp_path, cfg)  # without publish_first the whole tree counts
    assert sorted(found["audio"]) == ["private/b.wav", "pub/skip.wav", "top.wav"]


def test_generator_tables_hold_rows_only(tmp_path):
    cfg = AuditConfig(table_extension=".tbl", table_row=re.compile(r"^\d+ \w+$"))
    write(tmp_path, "ok.tbl", "1 alpha\n2 beta\n")
    write(tmp_path, "bad.tbl", "1 alpha\nint x = 5;\n")
    found, _ = kinds(tmp_path, cfg)
    assert found == {"assembly": ["bad.tbl"]}


def test_extra_roots_get_a_prefix(tmp_path):
    a, b = tmp_path / "a", tmp_path / "b"
    write(a, "ok.c", "int a;\n")
    write(b, "data.iso", "x")
    report = audit_tree([(a, ""), (b, "sub/")])
    assert report.findings["disc image"][0][0] == "sub/data.iso" and len(report.methods) == 2


def test_git_checkout_uses_tracked_and_untracked_but_not_ignored(tmp_path):
    subprocess.run(["git", "init", "-q"], cwd=tmp_path, check=True)
    write(tmp_path, ".gitignore", "build/\n")
    write(tmp_path, "build/out.o", "x")
    write(tmp_path, "src/a.c", "int a;\n")
    found, report = kinds(tmp_path)
    assert found == {} and "git (" in report.methods[0] and report.total == 2


def test_audit_file_reports_each_reason(tmp_path):
    path = write(tmp_path, "x.wav", b"RIFF\0\0\0\0")
    reasons = audit_file(path, "x.wav", AuditConfig())
    assert ("audio", "extension .wav") in reasons and ("binary", "RIFF audio/video") in reasons


def test_report_markdown_lists_findings(tmp_path):
    write(tmp_path, "g.iso", "x")
    text = audit_tree([(tmp_path, "")]).markdown()
    assert "1 findings." in text and "## disc image" in text and "`g.iso`" in text


@pytest.mark.parametrize("name", ["tool.pyc", "x.CUE"])
def test_extension_checks_ignore_case(tmp_path, name):
    write(tmp_path, name, "x")
    found, _ = kinds(tmp_path)
    assert found


def test_markdown_options(tmp_path):
    data = b"font bytes"
    write(tmp_path, "f.ttf", data)
    cfg = AuditConfig(known={"f.ttf": (hashlib.sha256(data).hexdigest(), "ours")}, table_extension=".tbl", table_row=re.compile(r"^\d+ \w+$"), table_row_name='"number name" rows')
    write(tmp_path, "t.tbl", "1 a\nbad line here\n")
    report = audit_tree([(tmp_path, "")], cfg)
    text = report.markdown(generated_by="scripts/audit_repo.py", known_heading="Known port files")
    assert "Generated by `scripts/audit_repo.py`. Scanned 2 files" in text and "## Known port files (not game data, 1)" in text
    assert 'not "number name" rows (first: line 2)' in text
    assert "Generated by" not in report.markdown()
