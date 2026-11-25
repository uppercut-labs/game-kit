import pytest

from uppercut_godot_render import (
    RenderError,
    find_marker,
    is_forward_plus,
    renderer_line,
    require_clean_log,
    require_forward_plus,
    run_start_frame,
    shot_start_frame,
)

VULKAN = "Vulkan 1.3.280 - Forward+ - Using Device #0: Synthetic GPU"
METAL = "Metal 3.0 - Forward+ - Using Device: Synthetic Apple GPU"
D3D12 = "D3D12 12_0 - Forward+ - Using Device #0: Synthetic GPU"
OPENGL = "OpenGL API 3.3.0 - Compatibility - Using Device: Synthetic GPU"
MOBILE = "Vulkan 1.3.280 - Mobile - Using Device #0: Synthetic GPU"


@pytest.mark.parametrize("line", [VULKAN, METAL, D3D12])
def test_forward_plus_on_vulkan_metal_and_d3d12_passes(line):
    log = f"Godot Engine v4\n{line}\nmore\n"
    assert is_forward_plus(log)
    assert require_forward_plus(log) == line


@pytest.mark.parametrize("line", [OPENGL, MOBILE])
def test_other_renderers_are_refused_and_named(line):
    assert not is_forward_plus(line)
    with pytest.raises(RenderError, match="not Forward\\+ on Vulkan, Metal or D3D12: " + line.split(" - ")[0]):
        require_forward_plus(line)


def test_a_missing_renderer_line_is_refused():
    with pytest.raises(RenderError, match="no renderer line"):
        require_forward_plus("Godot Engine v4\n")


def test_renderer_line():
    assert renderer_line(f"x\n{OPENGL}\n") == OPENGL
    assert renderer_line("nothing") is None


def test_the_renderer_line_must_start_a_line():
    assert not is_forward_plus(f"warning: {VULKAN}")


def test_one_run_start_frame():
    assert run_start_frame("a\nRUN_START_FRAME 12\nb\n") == 12
    assert run_start_frame("RUN_START_FRAME 0") == 0


@pytest.mark.parametrize("text, found", [("", 0), ("RUN_START_FRAME 1\nRUN_START_FRAME 2\n", 2), ("  RUN_START_FRAME 3", 0), ("RUN_START_FRAME x", 0)])
def test_the_start_frame_must_appear_exactly_once(text, found):
    with pytest.raises(RenderError, match=f"expected one RUN_START_FRAME line in the log, found {found}"):
        run_start_frame(text)


def test_shot_start_frame_takes_the_first_counter():
    log = "SHOT_FRAME 100 COUNTER 33 extra\nSHOT_FRAME 160 COUNTER 93\n"
    assert shot_start_frame(log) == 33
    assert shot_start_frame(log, 100) == 33
    with pytest.raises(RenderError, match="first shot frame 100, expected 0"):
        shot_start_frame(log, 0)
    with pytest.raises(RenderError, match="no SHOT_FRAME"):
        shot_start_frame("nothing")
    assert shot_start_frame("SHOT_FRAME -4 COUNTER 7", -4) == 7


def test_error_markers():
    require_clean_log("all quiet\nWARNING: something minor\n")
    for marker in ("SCRIPT ERROR", "SHADER ERROR", "Parse Error"):
        with pytest.raises(RenderError, match=f"reports {marker}: {marker}: boom at res://x.gd"):
            require_clean_log(f"ok\n{marker}: boom at res://x.gd\n")
    with pytest.raises(RenderError, match="reports ERROR: custom"):
        require_clean_log("ERROR: custom failure", ("ERROR: custom",))


def test_find_marker():
    log = "WINDOW_SIZE 1080x1920\nnothing\n"
    assert find_marker(log, r"^WINDOW_SIZE (\d+)x(\d+)") == [("1080", "1920")]
    assert find_marker(log, r"^nothing$") == [("nothing",)]
    assert find_marker(log, r"^missing", count=None) == []
    with pytest.raises(RenderError, match="expected 1 log line"):
        find_marker(log, r"^missing")
