"""Build and check controller replay files and capture scripts for game ports, and export a replay as a Dolphin DTM."""

from ._buttons import ButtonTable
from ._capture import CaptureError, CaptureScript
from ._dtm import DtmBuilder, DtmError, GCButton, dtm_header, gc_pad, replay_to_dtm
from ._replay import (
    DEFAULT_MAX_EVENTS,
    DEFAULT_SLOTS,
    HEADER,
    MAX_SLOTS,
    NEUTRAL,
    Event,
    Replay,
    ReplayError,
    parse_replay,
    read_replay,
)
from .cli import main

__all__ = [
    "ButtonTable",
    "CaptureError",
    "CaptureScript",
    "DEFAULT_MAX_EVENTS",
    "DEFAULT_SLOTS",
    "DtmBuilder",
    "DtmError",
    "Event",
    "GCButton",
    "HEADER",
    "MAX_SLOTS",
    "NEUTRAL",
    "Replay",
    "ReplayError",
    "dtm_header",
    "gc_pad",
    "main",
    "parse_replay",
    "read_replay",
    "replay_to_dtm",
]
__version__ = "0.1.0"
