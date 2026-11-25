import pytest

from ps2_support import assemble
from uppercut_ps2_match import datafrag

TAG = "__rd_t_"

PLAIN = """	.section .text.f,"ax",@progbits
	.globl f
f:
	lui $1,%hi($LC0)
	lw $2,%lo($LC0)($1)
	jr $31
	nop
	.rdata
	.align 2
$LC0:
	.word 0x11223344
$LC1:
	.word 5
"""


def test_kind_of_names():
    assert datafrag.kind_of(".rodata.F3") == ".rodata"
    assert datafrag.kind_of(".sbss.F0") == ".sbss"
    assert datafrag.kind_of(".lit4.F12") == ".lit4"
    assert datafrag.kind_of(".sdata") == ".sdata" and datafrag.kind_of(".lit8") == ".lit8"
    assert datafrag.kind_of(".text.f") is None and datafrag.kind_of(".rodata.str1.4") is None


def test_split_gives_each_label_its_own_section():
    code, data, n = datafrag.split(PLAIN, TAG)
    assert n == 2 and data == ""
    assert '.section .rodata.F0,"a",@progbits' in code and '.section .rodata.F1,"a",@progbits' in code
    assert code.index("$LC0:") < code.index(".rodata.F1") < code.index("$LC1:")
    assert "%hi($LC0)" in code  # a fragment label that code refers to keeps its name when it stays in the code file


def test_split_without_data_returns_the_text_unchanged():
    asm = '\t.section .text.f,"ax",@progbits\nf:\n\tnop\n'
    assert datafrag.split(asm, TAG) == (asm, "", 0)


def test_small_data_moves_to_a_companion_file_under_unique_names():
    asm = '\t.section .text.f,"ax",@progbits\n\t.globl f\nf:\n\tlw $2,%gp_rel(var)($28)\n\tjr $31\n\tnop\n\t.sdata\nvar:\n\t.word 5\n'
    code, data, n = datafrag.split(asm, TAG)
    assert n == 1
    assert ".extern\t__rd_t_0.var, 4" in code
    assert "%gp_rel(__rd_t_0.var)" in code
    assert '.section .sdata.F0,"aw",@progbits' in data and ".globl\t__rd_t_0.var" in data and "__rd_t_0.var:" in data


def test_li_s_literal_becomes_a_lit4_fragment_and_simple_floats_stay():
    asm = '\t.section .text.f,"ax",@progbits\n\t.globl f\nf:\n\tli.s\t$f0,3.14159\n\tli.s\t$f2,1.5\n\tjr $31\n\tnop\n'
    code, data, n = datafrag.split(asm, TAG)
    assert n == 1
    assert "l.s\t$f0,__rd_t_0.LIT0" in code and "li.s\t$f2,1.5" in code
    assert ".lit4.F0" in data and ".float\t3.14159" in data


def test_li_d_into_a_register_loads_through_at():
    asm = '\t.section .text.f,"ax",@progbits\n\t.globl f\nf:\n\tli.d\t$4,3.14159\n\tjr $31\n\tnop\n'
    code, data, n = datafrag.split(asm, TAG)
    assert n == 1 and data == ""
    assert "lui\t$1,%hi($LITD0)" in code and "ld\t$4,%lo($LITD0)($1)" in code and ".double\t3.14159" in code
    assert code.index("jr $31") < code.index(".rodata.F0")  # emitted after the code


def test_small_data_that_points_into_code_is_refused():
    asm = '\t.section .text.f,"ax",@progbits\nf:\n\tnop\n\t.sdata\nptr:\n\t.word f\n'
    with pytest.raises(ValueError):
        datafrag.split(asm, TAG)


def test_code_shape_masks_relocated_fields_and_keeps_relocation_class(tmp_path):
    code, _, _ = datafrag.split(PLAIN, TAG)
    a, b = tmp_path / "plain.o", tmp_path / "frag.o"
    a.write_bytes(assemble(PLAIN))
    b.write_bytes(assemble(code))
    assert datafrag.code_shape(a) == datafrag.code_shape(b)
    shape = datafrag.code_shape(a)[".text.f"]
    assert shape[0][1] == "hi" and shape[1][1] == "lo" and shape[2][1] is None
    assert shape[0][0] == 0x3C010000  # the lui with its immediate masked
