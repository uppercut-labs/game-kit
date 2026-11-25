"""Numbers for a 16-bit PCM WAV: duration, peak, RMS, full-scale samples and silent stretches. Standard library only.

The audio tap of a recompiled port is written while the port runs, and a run that was killed leaves a header that was
never brought up to date: the data length reads 0, or more than the file holds. `read_wav` takes the data to the end of the
file when that happens, so the stretch that was written is still measured.
"""

from __future__ import annotations

import array
import math
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Tuple

from .errors import WavError
from .ppm import PathLike

DEFAULT_SILENCE_DB = -66.2
"""A peak under 16 of 32768, a level no audible sound sits under."""
FULL_SCALE = 32767


@dataclass(frozen=True)
class Wav:
    samples: "array.array[int]"
    """Interleaved 16-bit samples, `channels` to a frame."""
    sample_rate: int
    channels: int
    repaired: bool = False
    """True when the data length in the header did not fit the file and the file's own length was used."""

    @property
    def frames(self) -> int:
        return len(self.samples) // self.channels


def parse_wav(raw: bytes, name: str = "WAV") -> Wav:
    if raw[:4] != b"RIFF" or raw[8:12] != b"WAVE":
        raise WavError(f"not a WAV: {name}")
    pos, fmt, data, repaired = 12, None, None, False
    while pos + 8 <= len(raw):
        cid, size = raw[pos : pos + 4], struct.unpack("<I", raw[pos + 4 : pos + 8])[0]
        body = pos + 8
        if cid == b"fmt ":
            if body + 16 > len(raw):
                raise WavError(f"fmt chunk is cut short: {name}")
            fmt = struct.unpack("<HHIIHH", raw[body : body + 16])
        elif cid == b"data":
            if size and body + size <= len(raw):
                end = body + size
            else:
                end, repaired = len(raw), True
            data = raw[body:end]
            break
        pos = body + size + (size & 1)
    if fmt is None or data is None:
        raise WavError(f"no fmt or data chunk: {name}")
    tag, channels, rate, _, _, bits = fmt
    if tag != 1 or bits != 16:
        raise WavError(f"only 16-bit PCM is handled (format {tag}, {bits} bits): {name}")
    if channels < 1 or rate < 1:
        raise WavError(f"{name} has {channels} channels at {rate} Hz")
    usable = len(data) // (2 * channels) * 2 * channels
    samples = array.array("h")
    samples.frombytes(data[:usable])
    if sys.byteorder == "big":
        samples.byteswap()
    return Wav(samples, rate, channels, repaired)


def read_wav(path: PathLike) -> Wav:
    return parse_wav(Path(path).read_bytes(), str(path))


def _peak(seg: "array.array[int]") -> int:
    return max(max(seg), -min(seg)) if len(seg) else 0


def _db(x: float) -> float:
    return 20 * math.log10(x) if x > 0 else float("-inf")


def wav_stats(wav: Wav, *, silence_db: float = DEFAULT_SILENCE_DB, name: str = "") -> Dict[str, Any]:
    """The statistics of a WAV, as a dict whose keys are listed in the README.

    A one-second window is silent when its largest sample is under `silence_db` (dBFS, relative to 32768). Stretches are found
    at 100 ms resolution with the same test, and those of a second or more are listed with their start."""
    s, ch, rate = wav.samples, wav.channels, wav.sample_rate
    n = wav.frames
    peak = _peak(s)
    rms = math.sqrt(sum(v * v for v in s) / len(s)) if n else 0.0
    full = s.count(FULL_SCALE) + s.count(-FULL_SCALE) + s.count(-32768)
    thr = 32768 * 10 ** (silence_db / 20)
    out: Dict[str, Any] = {
        "file": name,
        "sample_rate": rate,
        "channels": ch,
        "frames": n,
        "seconds": round(n / rate, 2),
        "peak": peak,
        "peak_dbfs": round(_db(peak / 32768), 2),
        "rms_dbfs": round(_db(rms / 32768), 2),
        "full_scale_samples": full,
        "total_samples": len(s),
        "silence_threshold_dbfs": silence_db,
    }
    out["full_scale_percent"] = round(100.0 * full / max(1, len(s)), 3)
    nwin = n // rate
    out["one_second_windows"] = nwin
    out["silent_one_second_windows"] = sum(1 for i in range(nwin) if _peak(s[i * rate * ch : (i + 1) * rate * ch]) < thr)
    blk = rate // 10
    nb = n // blk if blk else 0
    quiet = [_peak(s[i * blk * ch : (i + 1) * blk * ch]) < thr for i in range(nb)]
    runs: List[Tuple[float, float]] = []
    i = 0
    while i < nb:
        if quiet[i]:
            j = i
            while j < nb and quiet[j]:
                j += 1
            runs.append((i / 10.0, (j - i) / 10.0))
            i = j
        else:
            i += 1
    out["silent_stretches_1s_or_more"] = [{"start_s": a, "length_s": d} for a, d in runs if d >= 1.0]
    out["longest_silence_s"] = max([d for _, d in runs], default=0.0)
    if wav.repaired:
        out["header_repaired"] = True
    return out


def stats_for_file(path: PathLike, *, silence_db: float = DEFAULT_SILENCE_DB) -> Dict[str, Any]:
    return wav_stats(read_wav(path), silence_db=silence_db, name=str(path))


def format_stats(s: Dict[str, Any]) -> str:
    """The one-line summary (plus a second line when there are silent stretches of a second or more)."""
    text = (
        "%s: %.2f s, %d Hz, %d ch; peak %d (%.1f dBFS), RMS %.1f dBFS; %d of %d samples at full scale (%.3f %%); "
        "%d of %d one-second windows silent (< %.0f dBFS); longest silence %.1f s"
        % (
            s["file"], s["seconds"], s["sample_rate"], s["channels"], s["peak"], s["peak_dbfs"], s["rms_dbfs"],
            s["full_scale_samples"], s["total_samples"], s["full_scale_percent"], s["silent_one_second_windows"],
            s["one_second_windows"], s["silence_threshold_dbfs"], s["longest_silence_s"],
        )
    )
    if s["silent_stretches_1s_or_more"]:
        text += "\nsilent stretches (>= 1 s): " + ", ".join(
            "%.1f s at %.1f s" % (r["length_s"], r["start_s"]) for r in s["silent_stretches_1s_or_more"]
        )
    return text
