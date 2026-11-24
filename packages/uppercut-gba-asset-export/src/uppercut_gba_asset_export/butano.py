"""The JSON descriptor Butano's build step reads next to a 4bpp BMP."""

from __future__ import annotations

import json
from typing import Literal

Kind = Literal["sprite", "regular_bg_tiles"]


def butano_descriptor(kind: Kind, frame_size: tuple[int, int] | None = None) -> dict[str, object]:
    """A sprite sheet's descriptor (with the width and height of one frame) or a regular background's tile descriptor,
    both 4bpp, 16 colours, compressed where Butano finds it smaller (``auto_no_huffman``)."""
    if kind == "sprite":
        if frame_size is None:
            raise ValueError("a sprite descriptor needs the frame size")
        return {
            "type": "sprite",
            "width": int(frame_size[0]),
            "height": int(frame_size[1]),
            "bpp_mode": "bpp_4",
            "colors_count": 16,
            "compression": "auto_no_huffman",
        }
    if kind == "regular_bg_tiles":
        return {
            "type": "regular_bg_tiles",
            "bpp_mode": "bpp_4",
            "compression": "auto_no_huffman",
            "generate_palette": True,
            "palette_colors_count": 16,
            "palette_compression": "auto_no_huffman",
        }
    raise ValueError(f"unknown Butano asset kind {kind!r}")


def butano_json(kind: Kind, frame_size: tuple[int, int] | None = None) -> str:
    """The descriptor as the text of a ``.json`` file: two-space indent and a final newline."""
    return json.dumps(butano_descriptor(kind, frame_size), indent=2) + "\n"
