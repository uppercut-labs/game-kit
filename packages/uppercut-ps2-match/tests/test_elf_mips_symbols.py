import pytest

from ps2_support import (AT, GP, HI16, JR_RA, LO16, NOP, V0, assemble, build_elf, image_elf, jal, le_words, lui, lw)
from uppercut_ps2_match import mips, symbols
from uppercut_ps2_match.elf32 import Elf, Image
from uppercut_ps2_match.errors import ElfError

ASM = """
	.section .text.f,"ax",@progbits
	.globl f
f:
	lui $1,%hi($LC0)
	lw $2,%lo($LC0)($1)
	jal g
	jr $31
	nop
	.rdata
$LC0:
	.word 0x11223344
	.sbss
var:
	.space 8
	.comm shared,4
"""


def test_elf_reads_sections_symbols_relocations():
    elf = Elf(assemble(ASM))
    names = [s.name for s in elf.sections]
    assert names[:4] == ["", ".text.f", ".rodata", ".sbss"]
    assert elf.section(".rodata").data() == le_words(0x11223344)
    sbss = elf.section(".sbss")
    assert sbss.nobits and sbss.size == 8 and sbss.data() == b""
    by_name = {s.name: s for s in elf.symbols if s.name}
    assert by_name["f"].type == 2 and by_name["g"].undefined
    assert by_name["shared"].common and by_name["shared"].small_common
    ((rel_sec, target, rels),) = list(elf.relocations())
    assert rel_sec.name == ".rel.text.f" and target.name == ".text.f"
    assert [(r.offset, r.type) for r in rels] == [(0, HI16), (4, LO16), (8, 4)]
    assert elf.symbols[rels[0].sym].defined_in_section


def test_not_an_elf_and_truncated():
    with pytest.raises(ElfError):
        Elf(b"not an elf at all, just text" * 4)
    with pytest.raises(ElfError):
        Elf(b"\x7fELF\x02\x01" + b"\0" * 60)  # 64-bit
    good = assemble(ASM)
    with pytest.raises(ElfError):
        Elf(good[: len(good) - 30])


def test_image_bytes_at_uses_program_headers():
    image = Image(image_elf((0x100000, le_words(1, 2, 3)), (0x200000, b"abcd")))
    assert image.bytes_at(0x100004, 8) == le_words(2, 3)
    assert image.bytes_at(0x200000, 4) == b"abcd"
    assert image.bytes_at(0x100008, 8) is None  # runs off the end of the segment
    assert image.bytes_at(0x300000, 4) is None


def test_words_and_simm():
    assert mips.words(le_words(1, 0xFFFFFFFF) + b"\x07") == [1, 0xFFFFFFFF]
    assert mips.simm(0xFFFF) == -1 and mips.simm(0x7FFF) == 0x7FFF


def test_refs_finds_lui_lo_gp_and_calls():
    gp = 0x500000
    code = [lui(AT, 0x30), lw(V0, 0x10, AT), lw(V0, 0x20, GP), jal(0x100400), NOP, JR_RA, NOP]
    found = list(mips.refs(code, 0x100000, gp))
    assert (1, 0x300010, "lo") in found
    assert (2, gp + 0x20, "gp") in found
    assert (3, 0x100400, "call") in found


def test_refs_negative_offset_and_call_clobbers_registers():
    code = [lui(AT, 0x31), lw(V0, -0x10, AT), jal(0x100400), NOP, lw(V0, 4, AT)]
    found = list(mips.refs(code, 0x100000, 0x500000))
    assert (1, 0x30FFF0, "lo") in found
    assert not any(i == 4 for i, _, _ in found)  # $at is dead after the call


def test_symbol_file_first_address_wins_and_require(tmp_path):
    path = tmp_path / "symbol_addrs.txt"
    path.write_text("alpha = 0x100000; // type:func\nbeta = 0x200000;\nalpha = 0x300000;\nnot a symbol line\n")
    assert symbols.read_symbol_file(path) == {"alpha": 0x100000, "beta": 0x200000}
    assert symbols.read_symbol_file(path, require="type:func") == {"alpha": 0x100000}


def test_symbols_from_elf_functions_and_duplicates(tmp_path):
    path = tmp_path / "orig.elf"
    path.write_bytes(image_elf((0x100000, le_words(0)), symbols=[("main", 0x100000, 2), ("helper", 0x100100, 2), ("helper", 0x100200, 2), ("table", 0x300000, 1)]))
    syms, funcs = symbols.symbols_from_elf(path)
    assert syms["main"] == 0x100000 and syms["table"] == 0x300000
    assert funcs["helper"] == {0x100100, 0x100200}
    assert symbols.duplicate_functions(funcs) == {"helper": [0x100100, 0x100200]}
    assert symbols.starts_of(syms, funcs) == [0x100000, 0x100100, 0x100200, 0x300000]


def test_merge_first_table_wins():
    assert symbols.merge({"a": 1}, {"a": 2, "b": 3}) == {"a": 1, "b": 3}


def test_build_elf_round_trip_of_an_empty_object():
    assert Elf(build_elf([])).sections[0].name == ""
