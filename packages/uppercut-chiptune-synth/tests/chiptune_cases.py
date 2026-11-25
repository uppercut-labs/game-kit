"""Synthetic inputs, run through the package and, to make the goldens, through the three source copies.

`package_cases()` returns {label: WAV bytes} rendered by this package. `copy_cases(...)`, used only by
make_goldens.py, renders the same labels with the original scripts. golden.json holds the SHA-256 of
each. Nothing here is game data: the melodies are invented.
"""

from __future__ import annotations

import hashlib
import math
import os
import tempfile

import uppercut_chiptune_synth as u

MELODY = [(0, 60, 2), (2, 64, 2), (4, 67, 4), (8, 72, 2), (10, 71, 1), (12, 69, 4), (16, 65, 3), (20, 62, 4)]
BASS = [(s, 36 + (s // 8) * 5, 2) for s in range(0, 32, 2)]
CHORDS = [[60, 64, 67], [65, 69, 72]]
SONGS = [
    # label, bpm, bars, pattern length, lead, bass, drums, seed
    ("drive-pulse", 100.0, 2, 64, "pulse", "tri", "drive", 3),
    ("clock-bell", 120.0, 2, 64, "bell", "sine", "clock", 1),
    ("light-saw", 90.0, 2, 64, "saw", "pulse", "light", 7),
    ("none-tri", 110.0, 2, 64, "tri", "saw", "silent", 2),
    ("unknown-kind", 100.0, 1, 64, "wobble", "sine", "drive", 4),
    ("pattern-shorter-than-song", 140.0, 4, 32, "pulse", "tri", "drive", 5),
]
HYPER_SONG = ("hyper-pulse", 130.0, 2, 64, "pulse", "saw", "hyper", 6)
NOTE_KINDS = ["pulse", "tri", "saw", "sine", "bell", "unknown"]
DRUMS = ["kick", "snare", "hat", "unknown"]
RATE = 22050


def shared_signals() -> dict[str, list[float]]:
    sig = {}
    sig["loud-sine"] = [1.6 * math.sin(i * 0.05) for i in range(1000)]
    sig["quiet-sine"] = [0.4 * math.sin(i * 0.031) for i in range(1000)]
    sig["exactly-128"] = [0.9 * math.sin(i * 0.2) for i in range(128)]
    sig["129"] = [0.9 * math.sin(i * 0.2) for i in range(129)]
    sig["short-100"] = [0.95 * math.cos(i * 0.1) for i in range(100)]
    sig["clipping-square"] = [(2.0 if (i // 40) % 2 else -2.0) for i in range(500)]
    return sig


def _hash(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def package_cases() -> dict[str, bytes]:
    out: dict[str, bytes] = {}
    for label, samples in shared_signals().items():
        out[f"shared.write_wav.{label}"] = u.wav_bytes(samples)
    for voice in ("closed-form", "accumulated"):
        v = u.voicing(voice)
        songs = SONGS + ([HYPER_SONG] if voice == "accumulated" else [])
        for label, bpm, bars, plen, lead, bass, drums, seed in songs:
            m, b = u.expand_pattern(MELODY, plen), u.expand_pattern(BASS, plen)
            out[f"{voice}.song.{label}"] = u.wav_bytes(v.render_song(bpm, bars, m, b, CHORDS, lead, bass, drums, seed))
        for kind in NOTE_KINDS:
            buf = [0.0] * 6000
            v.add_note(buf, 0.01, 0.2, 57, 0.6, kind=kind, duty=0.4)
            out[f"{voice}.note.{kind}"] = u.wav_bytes(buf)
        buf = [0.0] * 600
        v.add_note(buf, 0.0, 0.0001, 69, 0.5, kind="tri")
        out[f"{voice}.note.shorter-than-attack"] = u.wav_bytes(buf)
        buf = [0.0] * 3000
        v.add_note(buf, 0.1, 0.5, 60, 0.5, kind="saw")
        out[f"{voice}.note.cut-by-buffer-end"] = u.wav_bytes(buf)
        for kind in DRUMS:
            buf = [0.0] * 5000
            v.add_drum(buf, 0.02, kind, 0.7, v.new_rng(5))
            out[f"{voice}.drum.{kind}"] = u.wav_bytes(buf)
        out[f"{voice}.arpeggio"] = u.wav_bytes(v.arpeggio([72, 76, 79, 84], 0.05, "bell"))
    cf = u.voicing("closed-form")
    for label, kind, to in (("slide-sine", "sine", 72), ("slide-saw", "saw", 48)):
        buf = [0.0] * 6000
        cf.add_note(buf, 0.0, 0.2, 60, 0.5, kind=kind, slide_to=to)
        out[f"closed-form.note.{label}"] = u.wav_bytes(buf)
    ac = u.voicing("accumulated")
    rng = ac.new_rng(42)
    for label, args in {
        "plain": (0.14, 240.0, 700.0, 0.7),
        "noisy": (0.34, 180.0, 900.0, 1.0, 0.35, 0.0),
        "decay": (0.22, 220.0, 55.0, 0.5, 0.35, 12.0),
        "flat": (0.08, 880.0, 880.0),
    }.items():
        out[f"accumulated.sweep.{label}"] = u.wav_bytes(ac.sweep(*args, rng=rng))
    return out


def _load_write_wav(mod, samples) -> bytes:
    path = os.path.join(tempfile.mkdtemp(), "x.wav")
    mod.write_wav(path, samples)
    with open(path, "rb") as fh:
        return fh.read()


def copy_cases(n64, naomi, dc, np) -> dict[str, dict[str, str]]:
    """{label: {copy name: sha256}} from the original scripts' own functions."""
    res: dict[str, dict[str, str]] = {}

    def put(label, who, data):
        res.setdefault(label, {})[who] = _hash(data)

    for label, samples in shared_signals().items():
        put(f"shared.write_wav.{label}", "n64", _load_write_wav(n64, np.array(samples)))
        put(f"shared.write_wav.{label}", "naomi", _load_write_wav(naomi, np.array(samples)))
        put(f"shared.write_wav.{label}", "dreamcast", _load_write_wav(dc, list(samples)))
    for who, mod in (("n64", n64), ("naomi", naomi)):
        z = lambda n: np.zeros(n, dtype=np.float64)  # noqa: E731
        for label, bpm, bars, plen, lead, bass, drums, seed in SONGS:
            m, b = mod.expand_pattern(MELODY, plen), mod.expand_pattern(BASS, plen)
            put(f"closed-form.song.{label}", who, _load_write_wav(mod, mod.render_song(bpm, bars, m, b, CHORDS, lead, bass, drums, seed)))
        for kind in NOTE_KINDS:
            buf = z(6000)
            mod.add_note(buf, 0.01, 0.2, 57, 0.6, kind=kind, duty=0.4)
            put(f"closed-form.note.{kind}", who, _load_write_wav(mod, buf))
        buf = z(600)
        mod.add_note(buf, 0.0, 0.0001, 69, 0.5, kind="tri")
        put("closed-form.note.shorter-than-attack", who, _load_write_wav(mod, buf))
        buf = z(3000)
        mod.add_note(buf, 0.1, 0.5, 60, 0.5, kind="saw")
        put("closed-form.note.cut-by-buffer-end", who, _load_write_wav(mod, buf))
        for kind in DRUMS:
            buf = z(5000)
            mod.add_drum(buf, 0.02, kind, 0.7, np.random.default_rng(5))
            put(f"closed-form.drum.{kind}", who, _load_write_wav(mod, buf))
        # The arpeggio helper is nested inside generate_sfx in the copies; this is its body.
        buf = z(int((0.05 * 4 + 0.16) * RATE))
        for i, m in enumerate([72, 76, 79, 84]):
            mod.add_note(buf, i * 0.05, 0.16, m, 0.50, kind="bell")
        put("closed-form.arpeggio", who, _load_write_wav(mod, buf))
    for label, kind, to in (("slide-sine", "sine", 72), ("slide-saw", "saw", 48)):
        buf = np.zeros(6000)
        n64.add_note(buf, 0.0, 0.2, 60, 0.5, kind=kind, slide_to=to)
        put(f"closed-form.note.{label}", "n64", _load_write_wav(n64, buf))
    for label, bpm, bars, plen, lead, bass, drums, seed in SONGS + [HYPER_SONG]:
        m, b = dc.expand_pattern(MELODY, plen), dc.expand_pattern(BASS, plen)
        put(f"accumulated.song.{label}", "dreamcast", _load_write_wav(dc, dc.render_song(bpm, bars, m, b, CHORDS, lead, bass, drums, seed)))
    for kind in NOTE_KINDS:
        buf = [0.0] * 6000
        dc.add_note(buf, 0.01, 0.2, 57, 0.6, kind=kind, duty=0.4)
        put(f"accumulated.note.{kind}", "dreamcast", _load_write_wav(dc, buf))
    buf = [0.0] * 600
    dc.add_note(buf, 0.0, 0.0001, 69, 0.5, kind="tri")
    put("accumulated.note.shorter-than-attack", "dreamcast", _load_write_wav(dc, buf))
    buf = [0.0] * 3000
    dc.add_note(buf, 0.1, 0.5, 60, 0.5, kind="saw")
    put("accumulated.note.cut-by-buffer-end", "dreamcast", _load_write_wav(dc, buf))
    import random

    for kind in DRUMS:
        buf = [0.0] * 5000
        dc.add_drum(buf, 0.02, kind, 0.7, random.Random(5))
        put(f"accumulated.drum.{kind}", "dreamcast", _load_write_wav(dc, buf))
    put("accumulated.arpeggio", "dreamcast", _load_write_wav(dc, dc.synth_arpeggio([72, 76, 79, 84], 0.05, "bell")))
    rng = random.Random(42)
    for label, args in {
        "plain": (0.14, 240.0, 700.0, 0.7),
        "noisy": (0.34, 180.0, 900.0, 1.0, 0.35, 0.0),
        "decay": (0.22, 220.0, 55.0, 0.5, 0.35, 12.0),
        "flat": (0.08, 880.0, 880.0),
    }.items():
        put(f"accumulated.sweep.{label}", "dreamcast", _load_write_wav(dc, dc.synth_sweep(*args, rng=rng)))
    return res
