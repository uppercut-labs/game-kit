from uppercut_ps2_match import asmtu, csplit

SOURCE = '''#include "game.h"
typedef struct { int a; } T;
extern int counter;
static int table[3] = { 1, 2, 3 };
const char *msg = "a } brace in a string; and a ; too";
int add(int a, int b) /* { not a body */
{
    char c = '}';
    return a + b + c; // } nor this
}
static void noop(void);
void noop(void) { }
'''


def test_split_separates_prelude_and_functions():
    prelude, funcs = csplit.split(SOURCE)
    assert [n for n, _ in funcs] == ["add", "noop"]
    assert prelude[0].startswith("#include")
    assert any("typedef struct" in p for p in prelude)
    assert any("static int table[3]" in p for p in prelude)
    assert any("a } brace in a string; and a ; too" in p for p in prelude)
    assert "return a + b + c;" in funcs[0][1]


def test_split_handles_continued_preprocessor_lines():
    prelude, funcs = csplit.split("#define M(x) \\\n  ((x) + 1)\nint f(void) { return M(1); }\n")
    assert len(prelude) == 1 and prelude[0].count("\n") == 2
    assert [n for n, _ in funcs] == ["f"]


def test_prototype_drops_the_body_and_comments():
    assert csplit.prototype("int f(int a) /* c */ {\n return a;\n}") == "int f(int a);"
    assert csplit.prototype("int f(int a) { return a; }") == "int f(int a);"


def test_data_definition_finds_initialised_variables():
    assert csplit.data_definition("static int t[2] = { 1, 2 };") == ("t", "extern int t[2];")
    assert csplit.data_definition("const char *names[] = { \"a\" };")[0] == "names"
    assert csplit.data_definition("extern int t;") is None
    assert csplit.data_definition("int counter;") is None
    assert csplit.data_definition("typedef int T;") is None
    assert csplit.data_definition("struct S { int a; } = {0};") is None


COP2 = "/* 1000 00100000 0000A8BF */  vnop\n/* 1004 00100004 11223344 */  addiu $a0, $a0, 1\n/* 1008 00100008 01020304 */  vadd.xyzw $vf1, $vf2, $vf3\n"


def test_fix_cop2_turns_vu_macro_lines_into_words():
    out = asmtu.fix_cop2(COP2)
    assert ".word 0xBFA80000 /* vnop */" in out
    assert ".word 0x04030201 /* vadd.xyzw $vf1, $vf2, $vf3 */" in out
    assert "addiu $a0, $a0, 1" in out


def test_fix_cop2_tree_rewrites_only_changed_files(tmp_path):
    (tmp_path / "a.s").write_text(COP2)
    (tmp_path / "b.s").write_text("nop\n")
    assert asmtu.fix_cop2_tree(tmp_path) == 1
    assert "vnop\n" not in (tmp_path / "a.s").read_text().replace("/* vnop */", "")


TU = """.set noreorder
.section .text
nonmatching f, 0x8
glabel f
/* 0 00100000 00000000 */ nop
/* 4 00100004 00000000 */ nop
endlabel f

nonmatching g, 0x8
glabel g
/* 8 00100008 00000001 */ nop
endlabel g

nonmatching h, 0x4
glabel h
/* C 0010000C 00000002 */ nop
endlabel h
"""


def test_split_tu_drops_functions_and_starts_a_part_per_kept_run():
    text, order = asmtu.split_tu(TU, {"g"})
    assert "glabel g" not in text and "glabel f" in text and "glabel h" in text
    assert order == [".text.p0", "g", ".text.p1"]
    assert text.index(".section .text.p0") < text.index("glabel f") < text.index(".section .text.p1") < text.index("glabel h")


def test_split_tu_keep_pad_leaves_padding_words():
    tu = TU.replace("endlabel g\n", "endlabel g\n/* 8 0010000C 00000000 */ nop\n")
    text, order = asmtu.split_tu(tu, {"g"}, keep_pad=True)
    assert "glabel g" not in text
    assert order == [".text.p0", "g", ".text.p1"]
    assert text.index("0010000C") < text.index("glabel h")  # the padding word stays, in the part that h continues


def test_strip_attributes_and_unstatic():
    src = 'int t[4] __attribute__((section(".rodata"), aligned(16))) = { 1 };'
    assert asmtu.strip_attributes(src) == "int t[4]  = { 1 };"
    assert asmtu.unstatic("/* c */ static int f(void) { }") == "/* c */ int f(void) { }"
    assert asmtu.unstatic("int static_x;") == "int static_x;"


def test_is_static_prototype():
    assert asmtu.is_static_prototype("static int f(int);")
    assert not asmtu.is_static_prototype("static int f(int) { }")
    assert not asmtu.is_static_prototype("static int table[2] = { 1 };")
    assert not asmtu.is_static_prototype("int f(int);")
