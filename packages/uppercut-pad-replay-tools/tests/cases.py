"""Replay texts shared by the tests. All made up."""

# (label, contents) pairs a strict reader must refuse. The same table is in pad-replay's C test.
INVALID = [
    ("zero events", "# nothing to apply\n\n"),
    ("missing field", "0 0 0000 128 128\n"),
    ("out of order", "1 0 0008 128 128 1\n0 7 4000 128 128 1\n"),
    ("unknown trailing token", "0 0 0000 128 128 1 extra\n"),
    ("trailing comment", "0 0 0000 128 128 1 # note\n"),
    ("slot out of range", "0 8 0000 128 128 1\n"),
    ("button mask out of range", "0 0 10000 128 128 1\n"),
    ("axis out of range", "0 0 0000 256 128 1\n"),
    ("connection flag out of range", "0 0 0000 128 128 2\n"),
    ("negative frame", "-1 0 0000 128 128 1\n"),
    ("explicit plus sign", "+0 0 0000 128 128 1\n"),
    ("frame overflow", "4294967296 0 0000 128 128 1\n"),
    ("button mask overflow", "0 0 100000000 128 128 1\n"),
    ("digits glued to letters", "0 0 0000 128abc 128 1\n"),
    ("hex prefix without digits", "0 0 0x 128 128 1\n"),
]

PLAYBACK = (
    "  # first frame includes both physical ports\r\n"
    "\n"
    "0 0 0008 0 255 1\r\n"
    "0 7 0x4000 240 30 1\n"
    "3 0 0000 128 128 1\n"
    "5 7 4000 12 245 0\n"
    "6 4 0080 128 128 1"
)
