import json
import shlex
import sys
from pathlib import Path

import pytest

from ps2_support import AT, JR_RA, NOP, V0, fake_toolchain_args, image_elf, jal, le_words, lui, lw
from uppercut_ps2_match import Toolchain, Verifier
from uppercut_ps2_match.config import load_verify_config, parse_toolchain
from uppercut_ps2_match.errors import ConfigError
from uppercut_ps2_match.verify import ObjectInfo, linked_parts

F_ADDR, G_ADDR, TABLE = 0x100000, 0x100020, 0x300010
F_CODE = le_words(lui(AT, 0x30), lw(V0, 0x10, AT), JR_RA, NOP)

F_ASM = """	.section .text.f,"ax",@progbits
	.globl f
f:
	lui $1,%hi($LC0)
	lw $2,%lo($LC0)($1)
	jr $31
	nop
	.rdata
$LC0:
	.word 0x11223344
"""


def toolchain(log=None, **kw):
    cfg = fake_toolchain_args(log)
    quoted = {k: v for k, v in cfg.items()}
    return Toolchain(cpp=quoted["cpp"], cc1=quoted["cc1"], assembler=quoted["assembler"], linker=cfg["linker"], **kw)


def make_verifier(tmp_path, *, code=F_CODE, pad=b"\0" * 16, data=le_words(0x11223344), symbols=None, funcs=None, extra_chunks=(), **kw):
    chunks = [(F_ADDR, code + pad), (G_ADDR, le_words(JR_RA, NOP)), (TABLE, data), *extra_chunks]
    image = tmp_path / "orig.elf"
    image.write_bytes(image_elf(*chunks))
    syms = {"f": F_ADDR, "g": G_ADDR}
    syms.update(symbols or {})
    return Verifier(image, syms, toolchain(), gp=0x500000, funcs=funcs, **kw)


def check(verifier, tmp_path, asm, **kw):
    src = tmp_path / "unit.c"
    src.write_text(asm)
    return verifier.check(src, tmp_path / "work", "-O2 -G8", **kw)


def test_match_with_data_solved_by_the_originals_instructions(tmp_path):
    (r,) = check(make_verifier(tmp_path), tmp_path, F_ASM)
    assert r["status"] == "match" and r["name"] == "f" and r["addr"] == "0x100000"
    assert r["size"] == 16 and r["orig_size"] == 0x20 and r["diff_words"] == 0 and r["padding_ok"]
    assert r["data"] == {".rodata.F0": "0x300010+4"} and r["data_ok"] is True
    assert "unsolved" not in r


def test_fragments_off_checks_the_plain_object(tmp_path):
    (r,) = check(make_verifier(tmp_path, fragments=False), tmp_path, F_ASM)
    assert r["status"] == "match" and r["data"] == {".rodata": "0x300010+4"}


def test_data_that_differs_is_reported_but_does_not_fail_the_match(tmp_path):
    (r,) = check(make_verifier(tmp_path, data=le_words(0xDEADBEEF)), tmp_path, F_ASM)
    assert r["status"] == "match" and r["data_ok"] is False


def test_mismatching_instruction(tmp_path):
    code = le_words(lui(AT, 0x30), lw(3, 0x10, AT), JR_RA, NOP)
    (r,) = check(make_verifier(tmp_path, code=code), tmp_path, F_ASM)
    assert r["status"] == "mismatch" and r["diff_words"] == 1


def test_non_zero_padding_is_a_mismatch(tmp_path):
    (r,) = check(make_verifier(tmp_path, pad=b"\0" * 8 + b"\x01" + b"\0" * 7), tmp_path, F_ASM)
    assert r["status"] == "mismatch" and r["diff_words"] == 0 and r["padding_ok"] is False


def test_function_without_an_address(tmp_path):
    asm = F_ASM.replace("f:", "f:").replace(".text.f", ".text.h").replace(".globl f", ".globl h").replace("\nf:", "\nh:")
    (r,) = check(make_verifier(tmp_path), tmp_path, asm)
    assert r == {"name": "h", "status": "no-address"}


def test_compile_error_carries_the_tools_output(tmp_path):
    (r,) = check(make_verifier(tmp_path), tmp_path, "ERROR\n" + F_ASM)
    assert r["status"] == "compile-error" and "made-up compile error" in r["log"]


def test_link_error_names_the_missing_symbol(tmp_path):
    verifier = make_verifier(tmp_path)
    verifier.toolchain.linker = [sys.executable, "-c", "import sys; sys.stderr.write(\"x.o: undefined reference to `boom'\\n\"); sys.exit(1)"]
    (r,) = check(verifier, tmp_path, F_ASM)
    assert r["status"] == "link-error" and r["missing"] == ["boom"]


def test_calls_to_known_and_unknown_symbols(tmp_path):
    asm = F_ASM.replace("\tlui $1,%hi($LC0)\n\tlw $2,%lo($LC0)($1)\n", "\tjal g\n\tjal elsewhere\n")
    code = le_words(jal(G_ADDR), jal(0x100400), JR_RA, NOP)
    (r,) = check(make_verifier(tmp_path, code=code, symbols={"elsewhere": 0x100400}), tmp_path, asm)
    assert r["status"] == "match"
    # a call to a symbol the original has no address for is solved from the original's jal
    (r,) = check(make_verifier(tmp_path, code=code), tmp_path, asm)
    assert r["status"] == "match" and r["solved"] == {"elsewhere": "0x00100400"}


def test_common_symbols_are_solved_through_gp(tmp_path):
    asm = F_ASM.replace("\tlui $1,%hi($LC0)\n\tlw $2,%lo($LC0)($1)\n", "\tlw $2,%gp_rel(counter)($28)\n\tnop\n").replace("\t.rdata\n$LC0:\n\t.word 0x11223344\n", "\t.comm counter,4\n")
    code = le_words(lw(V0, 0x40, 28), NOP, JR_RA, NOP)
    (r,) = check(make_verifier(tmp_path, code=code), tmp_path, asm)
    assert r["status"] == "match" and r["solved"] == {"counter": "0x00500040"}


def test_string_table_is_placed_through_pointers_in_placed_data(tmp_path):
    asm = F_ASM.replace("\t.word 0x11223344\n", "\t.word $LC1\n\t.rdata\n$LC1:\n\t.word 0x64636261\n")
    verifier = make_verifier(tmp_path, data=le_words(0x300100), extra_chunks=[(0x300100, b"abcd")])
    (r,) = check(verifier, tmp_path, asm)
    assert r["status"] == "match"
    assert r["data"] == {".rodata.F0": "0x300010+4", ".rodata.F1": "0x300100+4"} and r["data_ok"] is True


def test_a_fragment_nothing_points_at_is_unused(tmp_path):
    asm = F_ASM + "$LC9:\n\t.word 7\n"
    (r,) = check(make_verifier(tmp_path), tmp_path, asm)
    assert r["status"] == "match" and r["data"][".rodata.F0"] == "0x300010+4" and r["data"][".rodata.F1"] == "unused+4"


def test_small_data_falls_back_to_the_plain_object_when_fragments_cannot_merge(tmp_path):
    asm = F_ASM.replace("\tlui $1,%hi($LC0)\n\tlw $2,%lo($LC0)($1)\n", "\tlw $2,%gp_rel(var)($28)\n\tnop\n").replace("\t.rdata\n$LC0:", "\t.sdata\nvar:")
    code = le_words(lw(V0, -0x7000, 28), NOP, JR_RA, NOP)
    verifier = make_verifier(tmp_path, code=code, extra_chunks=[(0x500000 - 0x7000, le_words(0x11223344))])
    src = tmp_path / "unit.c"
    src.write_text(asm)
    obj, log = verifier.compile_c(src, tmp_path / "work")
    assert "merge failed" in log and "using the plain object" in log
    (r,) = verifier.check(src, tmp_path / "work2")
    assert r["status"] == "match" and r["data"] == {".sdata": f"0x{0x500000 - 0x7000:06x}+4"} and r["data_ok"] is True


def test_file_statics_resolve_to_the_address_in_the_preferred_range(tmp_path):
    other = 0x200000
    verifier = make_verifier(tmp_path, funcs={"f": {F_ADDR, other}}, extra_chunks=[(other, F_CODE + b"\0" * 16)])
    (r,) = check(verifier, tmp_path, F_ASM, prefer=(other, other + 0x1000))
    assert r["addr"] == "0x200000" and r["status"] == "match"
    (r,) = check(verifier, tmp_path, F_ASM, prefer=(F_ADDR, F_ADDR + 0x1000))
    assert r["addr"] == "0x100000" and r["status"] == "match"


def test_commands_run_in_the_compile_folder_with_the_defines(tmp_path):
    log = tmp_path / "tools.log"
    verifier = make_verifier(tmp_path, defines="-DVERSION_B")
    verifier.toolchain = toolchain(log)
    check(verifier, tmp_path, F_ASM)
    lines = log.read_text().splitlines()
    assert lines[0].startswith("cpp ") and "-DVERSION_B" in lines[0] and "-D__mips__" in lines[0] and "-Iinclude" in lines[0]
    assert lines[1].startswith("cc1 ") and "-O2 -G8" in lines[1] and "in.i" in lines[1]
    assert any(line.startswith("as ") for line in lines)


def test_include_dir_is_copied_next_to_each_compile(tmp_path):
    inc = tmp_path / "headers"
    inc.mkdir()
    (inc / "game.h").write_text("// header\n")
    verifier = make_verifier(tmp_path, include_dir=inc)
    check(verifier, tmp_path, F_ASM)
    assert (tmp_path / "work" / "include" / "game.h").read_text() == "// header\n"


def test_toolchain_path_map_and_errors():
    tc = Toolchain("cpp", "cc1", "as", path_map=[("/host/work", "/guest/work")])
    assert tc.guest("/host/work/a") == "/guest/work/a"
    with pytest.raises(ValueError, match="not visible"):
        tc.guest("/elsewhere/a")
    assert Toolchain("cpp", "cc1", "as").guest("/anything") == "/anything"
    rc, out = Toolchain("cpp", "cc1", "as").sh("echo hi; echo err 1>&2; exit 3")
    assert rc == 3 and "hi" in out and "err" in out


def test_object_info_and_linked_parts(tmp_path):
    from ps2_support import assemble

    obj = tmp_path / "x.o"
    obj.write_bytes(assemble(F_ASM.replace(".rdata\n", ".section .rodata.F0,\"a\",@progbits\n") + "\t.comm counter,4\n"))
    info = ObjectInfo.read(obj)
    assert info.functions == ["f"] and info.sections == {".rodata.F0": 4} and info.commons == {"counter": (4, True)}
    assert info.reloc_map[".text.f", 0] == ("sec", ".rodata.F0") and info.reloc_targets == {".text.f": {".rodata.F0"}}
    linked = tmp_path / "y.elf"
    from ps2_support import build_elf

    linked.write_bytes(build_elf([{"name": ".text.f", "data": F_CODE, "addr": F_ADDR}, {"name": ".bss.F1", "size": 8, "nobits": True, "addr": 0x1000}]))
    code, data = linked_parts(linked)
    assert code == {"f": (F_ADDR, F_CODE)} and data == {".bss.F1": (0x1000, 8, None)}


def test_config_builds_a_verifier(tmp_path):
    (tmp_path / "orig.elf").write_bytes(image_elf((F_ADDR, F_CODE + b"\0" * 16), (G_ADDR, le_words(JR_RA, NOP)), (TABLE, le_words(0x11223344)),
                                                  symbols=[("f", F_ADDR, 2), ("g", G_ADDR, 2)]))
    (tmp_path / "extra.txt").write_text("late = 0x100100; // type:func\nplain = 0x100200;\n")
    cfg_path = tmp_path / "verify.json"
    cfg_path.write_text(json.dumps({"image": "orig.elf", "symbols_from_elf": True, "function_symbols": ["extra.txt"], "symbols": ["extra.txt"],
                                    "gp": "0x500000", "flags": "-O1", "fragments": False, "defines": "-DX",
                                    "toolchain": {**fake_toolchain_args(), "runner": ["env"]}}))
    cfg = load_verify_config(cfg_path)
    assert cfg.flags == "-O1" and cfg.verifier.gp == 0x500000 and cfg.verifier.fragments is False and cfg.verifier.defines == "-DX"
    assert cfg.verifier.syms["f"] == F_ADDR and cfg.verifier.syms["late"] == 0x100100 and cfg.verifier.syms["plain"] == 0x100200
    assert "f" in cfg.verifier.funcs


@pytest.mark.parametrize("patch, message", [
    ({"image": None}, "image"), ({"gp": None}, "gp"), ({"gp": "nope"}, "gp is a number"), ({"toolchain": None}, "toolchain"),
    ({"toolchain": {"cpp": "x"}}, "cc1"),
])
def test_config_errors(tmp_path, patch, message):
    data = {"image": "orig.elf", "gp": 1, "toolchain": {"cpp": "a", "cc1": "b", "assembler": "c"}}
    (tmp_path / "orig.elf").write_bytes(image_elf((0, b"\0\0\0\0")))
    for k, v in patch.items():
        if v is None:
            data.pop(k)
        else:
            data[k] = v
    path = tmp_path / "c.json"
    path.write_text(json.dumps(data))
    with pytest.raises(ConfigError, match=message):
        load_verify_config(path)


def test_config_that_is_not_json(tmp_path):
    (tmp_path / "c.json").write_text("{")
    with pytest.raises(ConfigError, match="not JSON"):
        load_verify_config(tmp_path / "c.json")


def test_parse_toolchain_defaults():
    tc = parse_toolchain({"cpp": "a", "cc1": "b", "assembler": "c"})
    assert tc.linker == ["ld", "-EL"] and tc.runner == ["env"] and tc.path_map == []
