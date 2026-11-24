"""The two voicings, kept apart because the console copies they come from disagree.

`ClosedForm` is the voicing of the two NumPy copies: every oscillator sample is a
function of the time since the note began, envelopes run from 0 to 1 and back to
0 inclusive, and noise comes from NumPy's generator (`NumpyCompatRng`).

`Accumulated` is the voicing of the stdlib copy: oscillators add a phase
increment per sample, envelopes use `k / n` ramps, and noise comes from
`random.Random`.

Both write into a plain list of floats and both are plain Python.
"""

from __future__ import annotations

import math
from typing import Callable, Optional, Sequence

from .rng import NumpyCompatRng, Rng, stdlib_rng

TWO_PI = 2.0 * math.pi
DEFAULT_RATE = 22050

Pattern = Sequence[Optional[tuple]]


def midi_to_hz(note: float) -> float:
    return 440.0 * (2.0 ** ((note - 69.0) / 12.0))


def expand_pattern(events: Sequence[tuple[int, int, int]], length: int = 128) -> list:
    """Step events `(step, note, duration_in_steps)` to a list of `(note, duration)` or `None`."""
    out: list = [None] * length
    for step, note, dur in events:
        if 0 <= step < length:
            out[step] = (note, dur)
    return out


def vary_events(
    events: Sequence[tuple[int, int, int]],
    seed: int,
    *,
    drop: float = 0.0,
    octave_up: float = 0.0,
) -> list[tuple[int, int, int]]:
    """Seeded variation of a pattern: each event is dropped with probability `drop`
    and otherwise raised an octave with probability `octave_up`. The same seed gives
    the same result on every platform."""
    rng = stdlib_rng(seed)
    out = []
    for step, note, dur in events:
        if rng.random() < drop:
            rng.random()
            continue
        up = rng.random() < octave_up
        out.append((step, note + 12 if up else note, dur))
    return out


def linspace(start: float, stop: float, num: int) -> list[float]:
    """`numpy.linspace(start, stop, num)` (endpoint included), sample for sample."""
    if num <= 0:
        return []
    if num == 1:
        return [start]
    div = num - 1
    delta = stop - start
    step = delta / div
    if step == 0:
        out = [(k / div) * delta + start for k in range(num)]
    else:
        out = [k * step + start for k in range(num)]
    out[-1] = stop
    return out


class _Base:
    name = ""

    def __init__(self, rate: int = DEFAULT_RATE) -> None:
        if rate <= 0:
            raise ValueError("rate must be positive")
        self.rate = rate

    def new_rng(self, seed: int) -> Rng:
        raise NotImplementedError

    def add_note(self, buf, start_s, dur_s, midi_note, gain, kind="pulse", duty=0.35, slide_to=None) -> None:
        raise NotImplementedError

    def add_drum(self, buf, start_s, kind, gain, rng) -> None:
        raise NotImplementedError

    def silence(self, seconds: float) -> list[float]:
        return [0.0] * int(seconds * self.rate)

    def arpeggio(self, notes: Sequence[int], step_s: float = 0.055, kind: str = "bell") -> list[float]:
        """Notes in turn, each 0.16 s long at gain 0.5. Both copies agree on this."""
        total = step_s * len(notes) + 0.16
        buf = [0.0] * int(total * self.rate)
        for i, m in enumerate(notes):
            self.add_note(buf, i * step_s, 0.16, m, 0.50, kind=kind)
        return buf


class ClosedForm(_Base):
    """Voicing of the two NumPy copies."""

    name = "closed-form"

    def new_rng(self, seed: int) -> NumpyCompatRng:
        return NumpyCompatRng(seed)

    def time_axis(self, n: int) -> list[float]:
        return [k / self.rate for k in range(n)]

    def cumulative_phase(self, freqs: Sequence[float]) -> list[float]:
        """Running sum of `f / rate`, the phase in cycles of a frequency curve."""
        out = []
        acc = 0.0
        for f in freqs:
            acc += f / self.rate
            out.append(acc)
        return out

    def adsr(self, n: int, a_s: float = 0.008, d_s: float = 0.06, s_lvl: float = 0.6, r_s: float = 0.03) -> list[float]:
        a = max(1, int(a_s * self.rate))
        d = max(1, int(d_s * self.rate))
        r = max(1, int(r_s * self.rate))
        if a >= n:
            return linspace(0.0, 1.0, n)
        env = [s_lvl] * n
        env[:a] = linspace(0.0, 1.0, a)
        d_end = min(n, a + d)
        env[a:d_end] = linspace(1.0, s_lvl, d_end - a)
        if r < n:
            tail = linspace(1.0, 0.0, r)
            for j in range(r):
                env[n - r + j] *= tail[j]
        return env

    def osc(self, freq: float, n: int, kind: str = "pulse", duty: float = 0.35) -> list[float]:
        rate = self.rate
        c1 = 2.0 * math.pi * freq
        sin, exp = math.sin, math.exp
        out = [0.0] * n
        if kind == "tri":
            for k in range(n):
                p = (freq * (k / rate)) % 1.0
                out[k] = 2.0 * abs(2.0 * p - 1.0) - 1.0
        elif kind == "saw":
            for k in range(n):
                p = (freq * (k / rate)) % 1.0
                out[k] = 2.0 * p - 1.0
        elif kind == "pulse":
            for k in range(n):
                p = (freq * (k / rate)) % 1.0
                out[k] = 1.0 if p < duty else -1.0
        elif kind == "bell":
            c2 = c1 * 2.0
            c3 = c1 * 3.0
            for k in range(n):
                t = k / rate
                out[k] = 0.65 * sin(c1 * t) + 0.28 * sin(c2 * t + 0.4 * sin(c3 * t)) * exp(-t * 8.0)
        else:
            for k in range(n):
                out[k] = sin(c1 * (k / rate))
        return out

    def add_note(self, buf, start_s, dur_s, midi_note, gain, kind="pulse", duty=0.35, slide_to=None) -> None:
        rate = self.rate
        i0 = int(round(start_s * rate))
        n = max(16, int(round(dur_s * rate)))
        if i0 >= len(buf):
            return
        if i0 + n > len(buf):
            n = len(buf) - i0
        f0 = midi_to_hz(midi_note)
        if slide_to is not None:
            freqs = linspace(f0, midi_to_hz(slide_to), n)
            acc = 0.0
            w = [0.0] * n
            for k in range(n):
                acc += freqs[k] / rate
                p = acc % 1.0
                w[k] = math.sin(2.0 * math.pi * p) if kind == "sine" else 2.0 * p - 1.0
        else:
            w = self.osc(f0, n, kind=kind, duty=duty)
        env = self.adsr(n)
        for k in range(n):
            buf[i0 + k] += w[k] * env[k] * gain

    def add_drum(self, buf, start_s, kind, gain, rng) -> None:
        rate = self.rate
        i0 = int(round(start_s * rate))
        if i0 >= len(buf):
            return
        sin, exp = math.sin, math.exp
        if kind == "kick":
            n = min(int(0.14 * rate), len(buf) - i0)
            acc = 0.0
            for k in range(n):
                t = k / rate
                f = 46.0 + 130.0 * exp(-t * 36.0)
                acc += f / rate
                buf[i0 + k] += (sin(2.0 * math.pi * acc) * exp(-t * 20.0)) * gain
        elif kind == "snare":
            n = min(int(0.12 * rate), len(buf) - i0)
            noise = [rng.uniform(-1.0, 1.0) for _ in range(n)]
            for k in range(n):
                t = k / rate
                body = sin(2.0 * math.pi * 185.0 * t) * exp(-t * 28.0)
                w = (noise[k] * 0.65 + body * 0.45) * exp(-t * 24.0)
                buf[i0 + k] += w * gain
        elif kind == "hat":
            n = min(int(0.05 * rate), len(buf) - i0)
            noise = [rng.uniform(-1.0, 1.0) for _ in range(n)]
            prev = 0.0
            for k in range(n):
                t = k / rate
                hp = (noise[k] - prev) * 0.7
                prev = noise[k]
                buf[i0 + k] += hp * exp(-t * 65.0) * gain

    def render_song(
        self,
        bpm: float,
        bars: int,
        melody: Pattern,
        bass: Pattern,
        chords: Sequence[Sequence[int]],
        lead_kind: str = "pulse",
        bass_kind: str = "tri",
        drum_style: str = "drive",
        seed: int = 1,
        pad_gain: float = 0.14,
    ) -> list[float]:
        """Patterns repeat when shorter than the song. Chords are sine pads, one chord per bar."""
        rng = self.new_rng(seed)
        step_s = 60.0 / bpm / 4.0
        total_steps = bars * 16
        buf = [0.0] * int(round(total_steps * step_s * self.rate))
        for step in range(total_steps):
            t = step * step_s
            m = melody[step % len(melody)]
            if m is not None:
                note, dur_steps = m
                self.add_note(buf, t, dur_steps * step_s * 0.92, note, 0.24, kind=lead_kind, duty=0.28)
            b = bass[step % len(bass)]
            if b is not None:
                note, dur_steps = b
                self.add_note(buf, t, dur_steps * step_s * 0.88, note, 0.28, kind=bass_kind, duty=0.50)
            if step % 16 == 0:
                for cn in chords[(step // 16) % len(chords)]:
                    self.add_note(buf, t, 15.5 * step_s, cn, pad_gain, kind="sine")
            if drum_style == "drive":
                if step % 4 == 0:
                    self.add_drum(buf, t, "kick", 0.34, rng)
                if step % 8 == 4:
                    self.add_drum(buf, t, "snare", 0.22, rng)
                if step % 2 == 1:
                    self.add_drum(buf, t, "hat", 0.12, rng)
            elif drum_style == "clock":
                if step % 4 == 0:
                    self.add_drum(buf, t, "kick", 0.36, rng)
                if step % 8 == 4:
                    self.add_drum(buf, t, "snare", 0.25, rng)
                self.add_drum(buf, t, "hat", 0.10 if step % 2 == 0 else 0.16, rng)
            elif drum_style == "light":
                if step % 8 == 0:
                    self.add_drum(buf, t, "kick", 0.26, rng)
                if step % 8 == 4:
                    self.add_drum(buf, t, "hat", 0.14, rng)
        return buf


class Accumulated(_Base):
    """Voicing of the stdlib copy."""

    name = "accumulated"

    def new_rng(self, seed: int):
        return stdlib_rng(seed)

    def env_adsr(self, k: int, n: int, a_s: float, d_s: float, s_lvl: float, r_s: float) -> float:
        rate = self.rate
        a_n = max(1, int(a_s * rate))
        d_n = max(1, int(d_s * rate))
        r_n = max(1, int(r_s * rate))
        if k < a_n:
            e = k / a_n
        elif k < a_n + d_n:
            e = 1.0 - (1.0 - s_lvl) * ((k - a_n) / d_n)
        else:
            e = s_lvl
        if k >= n - r_n:
            e *= (n - k) / r_n
        return e

    def add_note(self, buf, start_s, dur_s, midi_note, gain, kind="pulse", duty=0.35, slide_to=None) -> None:
        if slide_to is not None:
            raise ValueError("slide_to is only available in the closed-form voicing")
        rate = self.rate
        i0 = int(round(start_s * rate))
        n = max(16, int(round(dur_s * rate)))
        buf_len = len(buf)
        if i0 >= buf_len:
            return
        if i0 + n > buf_len:
            n = buf_len - i0
        freq = midi_to_hz(midi_note)
        dt = 1.0 / rate
        a_n = max(1, int(0.008 * rate))
        d_n = max(1, int(0.060 * rate))
        r_n = max(1, int(0.035 * rate))
        s_lvl = 0.62
        d_end = min(n, a_n + d_n)
        phase = 0.0
        p_inc = freq * dt
        sin = math.sin
        for k in range(n):
            if k < a_n:
                env = k / a_n
            elif k < d_end:
                env = 1.0 - (1.0 - s_lvl) * ((k - a_n) / d_n)
            else:
                env = s_lvl
            if k >= n - r_n:
                env *= (n - k) / r_n
            p = phase - int(phase)
            if kind == "sine":
                w = sin(TWO_PI * p)
            elif kind == "tri":
                w = 2.0 * abs(2.0 * p - 1.0) - 1.0
            elif kind == "saw":
                w = 2.0 * p - 1.0
            elif kind == "bell":
                t = k * dt
                p2 = (phase * 2.0) - int(phase * 2.0)
                p3 = (phase * 3.0) - int(phase * 3.0)
                w = 0.66 * sin(TWO_PI * p) + 0.26 * sin(TWO_PI * p2 + 0.4 * sin(TWO_PI * p3)) * math.exp(-t * 8.0)
            else:
                w = 1.0 if p < duty else -1.0
            buf[i0 + k] += w * env * gain
            phase += p_inc

    def add_drum(self, buf, start_s, kind, gain, rng) -> None:
        rate = self.rate
        i0 = int(round(start_s * rate))
        buf_len = len(buf)
        if i0 >= buf_len:
            return
        exp, sin = math.exp, math.sin
        if kind == "kick":
            n = min(buf_len - i0, int(0.14 * rate))
            phase = 0.0
            for k in range(n):
                t = k / rate
                f = 155.0 * exp(-t * 28.0) + 46.0
                phase += f / rate
                buf[i0 + k] += sin(TWO_PI * phase) * exp(-t * 22.0) * gain
        elif kind == "snare":
            n = min(buf_len - i0, int(0.11 * rate))
            for k in range(n):
                t = k / rate
                tone = sin(TWO_PI * 190.0 * t) * exp(-t * 36.0)
                nz = (rng.random() * 2.0 - 1.0) * exp(-t * 24.0)
                buf[i0 + k] += (tone * 0.35 + nz * 0.65) * gain
        else:
            n = min(buf_len - i0, int(0.045 * rate))
            prev = 0.0
            for k in range(n):
                t = k / rate
                nz = rng.random() * 2.0 - 1.0
                hp = nz - prev * 0.7
                prev = nz
                buf[i0 + k] += hp * exp(-t * 65.0) * gain

    def sweep(
        self,
        dur_s: float,
        f0: float,
        f1: float,
        exp_p: float = 1.0,
        noise_mix: float = 0.0,
        decay_rate: float = 0.0,
        rng: Optional[Rng] = None,
    ) -> list[float]:
        """A sine whose frequency moves from `f0` to `f1` along `u ** exp_p`, optionally
        mixed with noise and shaped by an exponential decay or a short envelope."""
        rate = self.rate
        n = max(64, int(round(dur_s * rate)))
        buf = [0.0] * n
        phase = 0.0
        for k in range(n):
            t = k / rate
            u = k / (n - 1)
            f = f0 + (f1 - f0) * (u ** exp_p)
            phase += f / rate
            tone = math.sin(TWO_PI * phase)
            if noise_mix > 0.0 and rng is not None:
                tone = tone * (1.0 - noise_mix) + (rng.random() * 2.0 - 1.0) * noise_mix
            if decay_rate > 0.0:
                env = math.exp(-t * decay_rate)
            else:
                env = self.env_adsr(k, n, 0.006, 0.05, 0.55, 0.06)
            buf[k] = tone * env * 0.65
        return buf

    def render_song(
        self,
        bpm: float,
        bars: int,
        melody: Pattern,
        bass: Pattern,
        chords: Sequence[Sequence[int]],
        lead_kind: str = "pulse",
        bass_kind: str = "tri",
        drum_style: str = "drive",
        seed: int = 1,
    ) -> list[float]:
        """Patterns do not repeat: steps past the end of a pattern are silent. Chords are
        triangle stabs on every fourth step, one chord per bar."""
        step_s = 60.0 / bpm / 4.0
        steps = bars * 16
        buf = [0.0] * int(round(steps * step_s * self.rate))
        rng = self.new_rng(seed)
        for step in range(steps):
            t = step * step_s
            m = melody[step] if step < len(melody) else None
            if m is not None:
                note, dur = m
                self.add_note(buf, t, dur * step_s * 0.92, note, 0.28, kind=lead_kind)
            b = bass[step] if step < len(bass) else None
            if b is not None:
                note, dur = b
                self.add_note(buf, t, dur * step_s * 0.88, note, 0.25, kind=bass_kind)
            if step % 4 == 0 and chords:
                for cn in chords[(step // 16) % len(chords)]:
                    self.add_note(buf, t, 3.6 * step_s, cn, 0.09, kind="tri")
            if drum_style == "drive":
                if step % 4 == 0:
                    self.add_drum(buf, t, "kick", 0.34, rng)
                if step % 8 == 4:
                    self.add_drum(buf, t, "snare", 0.24, rng)
                if step % 2 == 1:
                    self.add_drum(buf, t, "hat", 0.14, rng)
            elif drum_style == "clock":
                if step % 4 == 0:
                    self.add_drum(buf, t, "kick", 0.35, rng)
                if step % 8 == 4:
                    self.add_drum(buf, t, "snare", 0.25, rng)
                self.add_drum(buf, t, "hat", 0.10 if step % 2 == 0 else 0.15, rng)
            elif drum_style == "hyper":
                if step % 4 == 0 or step % 16 in (10, 14):
                    self.add_drum(buf, t, "kick", 0.36, rng)
                if step % 8 == 4:
                    self.add_drum(buf, t, "snare", 0.27, rng)
                self.add_drum(buf, t, "hat", 0.14 if step % 2 == 1 else 0.08, rng)
            elif drum_style == "light":
                if step % 8 == 0:
                    self.add_drum(buf, t, "kick", 0.26, rng)
                if step % 8 == 4:
                    self.add_drum(buf, t, "hat", 0.13, rng)
        return buf


def voicing(name: str = "accumulated", rate: int = DEFAULT_RATE) -> _Base:
    """`"accumulated"` (the default) or `"closed-form"`."""
    table: dict[str, Callable[[int], _Base]] = {"accumulated": Accumulated, "closed-form": ClosedForm}
    if name not in table:
        raise ValueError(f"unknown voicing {name!r}; use one of {sorted(table)}")
    return table[name](rate)
