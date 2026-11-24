import pytest

from uppercut_port_proof import PS1_BITS, Press, PressError, pad_word, parse_presses, to_script


def test_a_press_file_is_parsed_and_sorted():
    text = "# a route\n300:start\n  100:cross+circle:12  # held\n\n50:Up:1\np2 60:start\n"
    assert parse_presses(text) == [Press(50, ("up",), 1), Press(100, ("cross", "circle"), 12), Press(300, ("start",), 4)]


def test_the_default_hold_is_a_choice():
    assert parse_presses("5:up", default_hold=6)[0].hold == 6
    assert parse_presses("5:up")[0].hold == 4


def test_skipped_prefixes_are_a_choice():
    with pytest.raises(PressError):
        parse_presses("p2 5:up", skip_prefixes=())
    assert parse_presses("p2 5:up\n7:left", skip_prefixes=("p2 ",)) == [Press(7, ("left",), 4)]


@pytest.mark.parametrize(
    "text, message",
    [
        ("5", "line 1: expected frame:button"),
        ("a:up", "whole numbers"),
        ("5:up:x", "whole numbers"),
        ("-1:up", "frame is 0 or more"),
        ("5:up:0", "hold is 1 or more"),
        ("ok:up\n", "whole numbers"),
        ("5:turbo", "unknown button 'turbo'"),
        ("1:up\n2:up:3:4", "line 2"),
    ],
)
def test_a_line_that_cannot_be_read_names_its_number(text, message):
    with pytest.raises(PressError, match=message):
        parse_presses(text)


def test_pad_word_clears_one_bit_per_pressed_button():
    assert pad_word([]) == 0xFFFF
    assert pad_word(["start"]) == 0xFFF7 == 65527   
    assert pad_word(["Cross", " circle"]) == 0xFFFF & ~(1 << 14) & ~(1 << 13)
    assert pad_word(["start", "start"]) == 0xFFF7
    assert len(set(PS1_BITS.values())) == 16 and max(PS1_BITS.values()) == 15
    with pytest.raises(KeyError):
        pad_word(["nope"])


# the three expected scripts below were also written by the standalone script this package replaces, for the same input


def test_script_collapses_runs_and_puts_a_shot_before_the_input():
    presses = [Press(2, ("start",), 3), Press(6, ("cross", "up"), 2)]
    assert to_script(presses, 10, shot_every=5, prefix="p") == [
        "wait 2", "3 Start", "shot p00005", "wait 1", "2 Cross+Up", "wait 2", "shot p00010", "quit"]


def test_script_with_turbo_and_ranges_and_a_final_shot():
    assert to_script([], 6, shot_every=3, shot_ranges=[(1, 2)], turbo=True) == [
        "turbo on", "wait 1", "shot f00001", "wait 1", "shot f00002", "wait 1", "shot f00003", "wait 3", "shot f00006", "quit"]


def test_script_cuts_a_press_at_the_end_of_the_run():
    assert to_script([Press(8, ("start",), 50)], 10) == ["wait 8", "2 Start", "quit"]


def test_every_frame_is_counted_once():
    lines = to_script(parse_presses("3:up:5\n20:down:2", default_hold=6), 30, shot_every=7)
    held = sum(int(l.split()[0]) for l in lines if l.split()[0].isdigit() and not l.startswith(("shot", "wait")))
    waited = sum(int(l.split()[1]) for l in lines if l.startswith("wait"))
    assert held + waited == 30
