"""The Python reader and builder against pad-replay's C reader, when the C package is beside this one and a C compiler
exists. Skipped in an sdist, where neither is there."""

import shutil
import subprocess
from pathlib import Path

import pytest

from uppercut_pad_replay_tools import Replay, parse_replay, ReplayError

C_PACKAGE = Path(__file__).resolve().parents[2] / "pad-replay"
COMPILER = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")

pytestmark = pytest.mark.skipif(
    not (C_PACKAGE / "src" / "padr.c").exists() or not COMPILER, reason="needs pad-replay beside this package and a C compiler"
)

PROGRAM = r"""
#include <stdio.h>
#include "padr/padr.h"
int main(int argc, char **argv) {
    PadrError err;
    PadrOptions opt = {8, 32768};
    PadrScript *s = padr_load_file(argv[1], &opt, &err);
    unsigned frame, slot;
    if (!s) { printf("REFUSED %d\n", (int)err.status); return 0; }
    printf("OK %u\n", padr_event_count(s));
    for (frame = 0; frame <= padr_last_frame(s); ++frame) {
        PadrSample p[8];
        padr_sample(s, frame, p, 8);
        for (slot = 0; slot < 8; ++slot)
            printf("%u %u %04x %u %u %u\n", frame, slot, p[slot].buttons, p[slot].lx, p[slot].ly, p[slot].connected);
    }
    padr_free(s);
    return 0;
}
"""


@pytest.fixture(scope="module")
def reader(tmp_path_factory):
    folder = tmp_path_factory.mktemp("c-reader")
    source = folder / "reader.c"
    source.write_text(PROGRAM)
    exe = folder / "reader"
    subprocess.run(
        [COMPILER, "-std=c11", "-I", str(C_PACKAGE / "include"), str(source), str(C_PACKAGE / "src" / "padr.c"), "-o", str(exe)],
        check=True,
    )
    return exe


def c_read(reader, path):
    out = subprocess.run([str(reader), str(path)], capture_output=True, text=True, check=True).stdout.splitlines()
    return out[0], out[1:]


def test_a_built_replay_plays_the_same_in_both_readers(reader, tmp_path):
    replay = Replay().event(0, 0).event(0, 1, connected=0).tap(3, 0, 0x4080).event(5, 2, 1, 7, 250).disconnect(6, 2)
    path = replay.write(tmp_path / "built.input")
    status, rows = c_read(reader, path)
    assert status == f"OK {len(replay.events)}"
    state = {slot: (0, 128, 128, 0) for slot in range(8)}
    expected = []
    events = parse_replay(path.read_bytes())
    for frame in range(replay.last_frame + 1):
        for event in events:
            if event.frame == frame:
                state[event.slot] = (event.buttons, event.lx, event.ly, event.connected)
        for slot in range(8):
            b, x, y, c = state[slot]
            expected.append(f"{frame} {slot} {b:04x} {x} {y} {c}")
    assert rows == expected


def test_both_readers_refuse_the_same_files(reader, tmp_path):
    from replay_cases import INVALID

    for label, contents in INVALID:
        path = tmp_path / "bad.input"
        path.write_text(contents)
        with pytest.raises(ReplayError):
            parse_replay(contents)
        assert c_read(reader, path)[0].startswith("REFUSED"), label
    path.write_bytes(b"0 0 0008 0 255 1\0junk\n")
    assert c_read(reader, path)[0].startswith("REFUSED")
