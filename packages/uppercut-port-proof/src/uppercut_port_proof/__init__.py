"""Helpers for the proof run of a recompiled game port."""

__version__ = "0.1.0"

from .debug import DebugClient, run_presses, wait_frame, wait_ready  # noqa: E402
from .errors import DebugError, PpmError, PressError, ProofError, WavError  # noqa: E402
from .frames import FrameComparison, FrameRow, compare_frame_dirs, write_report  # noqa: E402
from .png import encode_png, picture_to_png, ppm_to_png, write_png  # noqa: E402
from .ppm import Ppm, crop, encode_ppm, enlarge, md5_of, parse_ppm, read_ppm, write_ppm  # noqa: E402
from .presses import SCRIPT_BUTTON_NAMES, PS1_BITS, Press, pad_word, parse_presses, read_presses, to_script  # noqa: E402
from .wav import DEFAULT_SILENCE_DB, Wav, format_stats, parse_wav, read_wav, stats_for_file, wav_stats  # noqa: E402

__all__ = [
    "DEFAULT_SILENCE_DB", "DebugClient", "DebugError", "FrameComparison", "FrameRow", "SCRIPT_BUTTON_NAMES", "PS1_BITS", "Ppm", "PpmError",
    "Press", "PressError", "ProofError", "Wav", "WavError", "compare_frame_dirs", "crop", "encode_png", "encode_ppm", "enlarge",
    "format_stats", "md5_of", "pad_word", "parse_ppm", "parse_presses", "parse_wav", "picture_to_png", "ppm_to_png", "read_ppm",
    "read_presses", "read_wav", "run_presses", "stats_for_file", "to_script", "wait_frame", "wait_ready", "wav_stats", "write_png",
    "write_ppm", "write_report",
]
