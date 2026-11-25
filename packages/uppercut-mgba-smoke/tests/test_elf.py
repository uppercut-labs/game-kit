import stat
import sys

import pytest

from uppercut_mgba_smoke import (
    SmokeError,
    Symbol,
    find_symbol,
    parse_dwarf_structs,
    read_dwarf,
    read_symbols,
    require_in_ram,
    require_struct,
)

SYMBOLS = """\
02005c70 00000994 b s_state
02000000 00000010 b s_other
08000100 T main
03000010 d s_unsized
"""

DWARF = """
 <1><100>: Abbrev Number: 1 (DW_TAG_structure_type)
    <101>   DW_AT_name        : Cord
    <102>   DW_AT_byte_size   : 60
 <2><110>: Abbrev Number: 2 (DW_TAG_member)
    <111>   DW_AT_name        : live
    <112>   DW_AT_data_member_location: 0
 <2><113>: Abbrev Number: 0
 <1><140>: Abbrev Number: 1 (DW_TAG_structure_type)
    <141>   DW_AT_name        : (indirect string, offset: 0x10): World
    <142>   DW_AT_byte_size   : 2452
 <2><150>: Abbrev Number: 2 (DW_TAG_member)
    <151>   DW_AT_name        : cords
    <152>   DW_AT_data_member_location: 216
 <2><160>: Abbrev Number: 2 (DW_TAG_member)
    <161>   DW_AT_name        : count
    <162>   DW_AT_data_member_location: 0x218 (location list)
 <2><173>: Abbrev Number: 0
 <1><180>: Abbrev Number: 1 (DW_TAG_structure_type)
    <181>   DW_AT_name        : World
    <182>   DW_AT_byte_size   : 2452
 <2><190>: Abbrev Number: 2 (DW_TAG_member)
    <191>   DW_AT_name        : cords
    <192>   DW_AT_data_member_location: 216
 <2><1a0>: Abbrev Number: 2 (DW_TAG_member)
    <1a1>   DW_AT_name        : count
    <1a2>   DW_AT_data_member_location: 536
"""


def test_find_symbol_with_and_without_a_size():
    assert find_symbol(SYMBOLS, "s_state") == Symbol(0x02005C70, 0x994)
    assert find_symbol(SYMBOLS, "main") == Symbol(0x08000100, None)
    assert find_symbol(SYMBOLS, "s_unsized") == Symbol(0x03000010, None)


def test_find_symbol_wants_exactly_one():
    with pytest.raises(SmokeError, match="expected one nothing symbol, found 0"):
        find_symbol(SYMBOLS, "nothing")
    with pytest.raises(SmokeError, match="found 2"):
        find_symbol(SYMBOLS + "02000040 00000004 b s_state\n", "s_state")


def test_find_symbol_matches_a_whole_name_only():
    assert find_symbol("02000000 00000004 b s_state_extra\n02000010 00000004 b s_state\n", "s_state").address == 0x02000010


def test_require_in_ram():
    require_in_ram(Symbol(0x02005C70, 0x994), "state")
    require_in_ram(Symbol(0x03000000, 4), "state")
    require_in_ram(Symbol(0x0203FFFC, 4), "state")
    with pytest.raises(SmokeError, match="wholly inside GBA RAM"):
        require_in_ram(Symbol(0x08000000, 0x994), "state")
    with pytest.raises(SmokeError, match="0x0203FFFC\\+0x8"):
        require_in_ram(Symbol(0x0203FFFC, 8), "state")


def test_parse_dwarf_structs_reads_names_sizes_and_offsets():
    structures = parse_dwarf_structs(DWARF)
    assert structures["Cord"][0].byte_size == 60
    assert structures["Cord"][0].members == {"live": 0}
    assert len(structures["World"]) == 2
    assert structures["World"][0].members == {"cords": 216, "count": 0x218}  # "0x218 (location list)" keeps its number


def test_require_struct_accepts_identical_repeats_and_refuses_conflicts():
    structures = parse_dwarf_structs(DWARF)
    assert require_struct(structures, "Cord", ["live"]).byte_size == 60
    # The two World definitions differ in `count` (0x218 is 536, so they agree), so this is one definition.
    assert require_struct(structures, "World", ["cords", "count"]).members["count"] == 536
    with pytest.raises(SmokeError, match="found 0"):
        require_struct(structures, "World", ["missing"])
    conflicting = parse_dwarf_structs(DWARF.replace("536", "540"))
    with pytest.raises(SmokeError, match="found 2"):
        require_struct(conflicting, "World", ["cords", "count"])


def test_a_member_without_a_numeric_offset_is_an_error():
    broken = DWARF.replace("216", "a location expression", 1)
    with pytest.raises(SmokeError, match="not a constant"):
        parse_dwarf_structs(broken)


def _tool(tmp_path, body: str, exit_code: int = 0):
    path = tmp_path / "tool"
    path.write_text(f"#!{sys.executable}\nimport sys\nprint({body!r})\nsys.exit({exit_code})\n")
    path.chmod(path.stat().st_mode | stat.S_IXUSR)
    return path


def test_read_symbols_and_dwarf_run_the_given_tools(tmp_path):
    assert read_symbols(_tool(tmp_path, "02000000 00000004 b s_state"), tmp_path / "game.elf").startswith("02000000")
    assert read_dwarf(_tool(tmp_path, "DWARF"), tmp_path / "game.elf").strip() == "DWARF"


def test_tool_failures_are_smoke_errors(tmp_path):
    with pytest.raises(SmokeError, match="failed to inspect debug ELF"):
        read_symbols(_tool(tmp_path, "", exit_code=3), tmp_path / "game.elf")
    with pytest.raises(SmokeError, match="cannot run"):
        read_dwarf(tmp_path / "no-such-tool", tmp_path / "game.elf")
