"""Deterministic chiptune and procedural audio for console homebrew. Standard library only."""

from .pcm import (
    LOG8_MAGNITUDES,
    WavInfo,
    inspect_wav,
    log8_decode_table,
    log8_encode,
    pcm_bytes,
    prepare,
    to_int16,
    wav_bytes,
    write_wav,
)
from .rng import NumpyCompatRng, seed_sequence_state, stdlib_rng
from .voicings import (
    DEFAULT_RATE,
    Accumulated,
    ClosedForm,
    expand_pattern,
    linspace,
    midi_to_hz,
    vary_events,
    voicing,
)

__version__ = "0.1.0"

__all__ = [
    "Accumulated",
    "ClosedForm",
    "DEFAULT_RATE",
    "LOG8_MAGNITUDES",
    "NumpyCompatRng",
    "WavInfo",
    "expand_pattern",
    "inspect_wav",
    "linspace",
    "log8_decode_table",
    "log8_encode",
    "midi_to_hz",
    "pcm_bytes",
    "prepare",
    "seed_sequence_state",
    "stdlib_rng",
    "to_int16",
    "vary_events",
    "voicing",
    "wav_bytes",
    "write_wav",
]
