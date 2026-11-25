"""Puts this folder on the import path so the tests can share helpers."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import shutil  # noqa: E402
import tempfile  # noqa: E402

import pytest  # noqa: E402

from servers import make_ram  # noqa: E402
from uppercut_pcsx2_pine import PineClient  # noqa: E402
from uppercut_pcsx2_pine.fake_server import FakePineServer  # noqa: E402


@pytest.fixture
def server():
    with FakePineServer(make_ram(), base=0x100000) as fake:
        yield fake


@pytest.fixture
def tcp(server):
    port = server.start_tcp()
    with PineClient.tcp(port=port) as client:
        yield client


@pytest.fixture
def unix(server):
    folder = Path(tempfile.mkdtemp(prefix="pine"))
    try:
        path = server.start_unix(folder / "p.sock")
        with PineClient.unix(path) as client:
            yield client
    finally:
        shutil.rmtree(folder, ignore_errors=True)
