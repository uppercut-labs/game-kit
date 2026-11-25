"""Fixtures shared by the tests: a fake PINE server reachable over TCP and over a Unix socket."""

from __future__ import annotations

import socket

import pytest


HAS_UNIX = hasattr(socket, "AF_UNIX")
needs_unix = pytest.mark.skipif(not HAS_UNIX, reason="no Unix sockets on this platform")


def make_ram() -> bytearray:
    """4 KiB where every byte is its own offset modulo 251 (a prime, so aligned reads are not all alike)."""
    return bytearray(i % 251 for i in range(0x1000))
