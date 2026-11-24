import pytest
from uppercut_gba_asset_export import bgr555, gpl_text, palette_entries, palette_words


def test_bgr555_corners() -> None:
    assert bgr555((0, 0, 0)) == 0
    assert bgr555((255, 255, 255)) == 0x7FFF
    assert bgr555((255, 0, 0)) == 0x001F
    assert bgr555((0, 255, 0)) == 0x03E0
    assert bgr555((0, 0, 255)) == 0x7C00


def test_rounding_modes_differ_and_nearest_is_the_default() -> None:
    assert bgr555((200, 0, 0)) == 24
    assert bgr555((200, 0, 0), rounding="nearest") == 24
    assert bgr555((200, 0, 0), rounding="truncate") == 25
    assert bgr555((255, 255, 255), rounding="truncate") == 0x7FFF
    # 8 is level 1 either way; 7 is level 1 when rounded and 0 when truncated
    assert bgr555((8, 0, 0)) == bgr555((8, 0, 0), rounding="truncate") == 1
    assert bgr555((7, 0, 0)) == 1
    assert bgr555((7, 0, 0), rounding="truncate") == 0


def test_nearest_matches_float_rounding_for_every_level() -> None:
    for v in range(256):
        assert bgr555((v, 0, 0)) == round(v * 31 / 255)


def test_bad_channel_is_an_error() -> None:
    with pytest.raises(ValueError):
        bgr555((256, 0, 0))


def test_palette_words_pad_and_transparent_first() -> None:
    colors = [(255, 0, 0), (0, 255, 0)]
    assert palette_words(colors, 4) == [0x1F, 0x3E0, 0, 0]
    assert palette_words(colors, 4, transparent_first=True) == [0, 0x3E0, 0, 0]
    assert len(palette_words(colors)) == 16
    with pytest.raises(ValueError):
        palette_words(colors, 1)


def test_gpl_text() -> None:
    text = gpl_text("TEST city", [(1, 2, 3), (255, 128, 0)], comment="# exact GBA BGR555 colors", labels=["a", "b"])
    assert text == (
        "GIMP Palette\nName: TEST city\nColumns: 16\n# exact GBA BGR555 colors\n"
        "  1   2   3\ta\n255 128   0\tb\n"
    )
    default = gpl_text("x", [(0, 0, 0)], columns=8)
    assert default == "GIMP Palette\nName: x\nColumns: 8\n#\n  0   0   0\tcolour_00\n"
    with pytest.raises(ValueError):
        gpl_text("x", [(0, 0, 0)], labels=["a", "b"])


def test_palette_entries() -> None:
    entries = palette_entries([(255, 0, 0), (0, 0, 255)])
    assert entries == [
        {"index": 0, "rgb": [255, 0, 0], "bgr555": "0x001F"},
        {"index": 1, "rgb": [0, 0, 255], "bgr555": "0x7C00"},
    ]
    assert list(entries[0]) == ["index", "rgb", "bgr555"]
    assert palette_entries([(171, 205, 239)], rgb="hex")[0]["rgb"] == "#ABCDEF"
    with pytest.raises(ValueError):
        palette_entries([(0, 0, 0)], rgb="css")  # type: ignore[arg-type]
