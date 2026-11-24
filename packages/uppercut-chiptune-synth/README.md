# uppercut-chiptune-synth

Deterministic chiptune and procedural audio for console homebrew. It has square (pulse), triangle, saw, sine and bell voices, kick, snare and hat drums, envelopes, a step sequencer that turns a melody, a bass line and chords into a song, seeded noise and seeded pattern variation, and output as a WAV file or as raw PCM at 8 or 16 bits. It uses the Python standard library only, and the same inputs give the same bytes on every platform.

It is not [`uppercut-sound-effect-synth`](../uppercut-sound-effect-synth). That package renders 48 kHz, 24-bit stereo sound effects (filtered noise, sweeps, saturation) with NumPy. This one is a tracker-style music and effects engine for small fixed-rate output (22,050 Hz, mono, 8 or 16 bits by default) and needs nothing installed.

## Install

```sh
pip install uppercut-chiptune-synth
```

## Example

```python
import uppercut_chiptune_synth as ct

v = ct.voicing()                      # "accumulated" (default) or "closed-form"
melody = ct.expand_pattern([(0, 72, 2), (2, 76, 2), (4, 79, 4)], 64)   # (step, midi note, steps long)
bass = ct.expand_pattern([(s, 36, 2) for s in range(0, 64, 2)], 64)
song = v.render_song(128.0, 4, melody, bass, [[60, 64, 67], [65, 69, 72]],
                     lead_kind="pulse", bass_kind="tri", drum_style="drive", seed=1)

ct.write_wav("title.wav", song)                         # 22,050 Hz mono 16-bit; returns the SHA-256
raw = ct.pcm_bytes(song, bits=16, byteorder="little")   # headerless PCM for a console's own loader
raw8 = ct.pcm_bytes(song, bits=8)                       # unsigned 8-bit, 128 is silence
```

A sound effect is a buffer you fill with notes, sweeps or drums:

```python
v = ct.voicing("accumulated")
rng = v.new_rng(42)
blip = v.arpeggio([72, 79], step_s=0.045, kind="sine")
fall = v.sweep(0.22, 220.0, 55.0, exp_p=0.5, noise_mix=0.35, decay_rate=12.0, rng=rng)
ct.write_wav("fall.wav", fall)
```

## Two voicings, because the copies it came from disagree

The code was written three times for three consoles: two NumPy copies and one standard-library copy. They share the file format and the finishing step, but they sound different, and each shipped. Merging them into one set of constants would have changed shipped audio, so the package keeps both as named voicings and each one reproduces its copies' output byte for byte (see Tests below). Choose one by name, `ct.voicing("closed-form")` or `ct.voicing("accumulated")`, or construct `ClosedForm(rate)` / `Accumulated(rate)` directly.

| | closed-form (the two NumPy copies) | accumulated (the stdlib copy, the default) |
| --- | --- | --- |
| Oscillator phase | computed from the time since the note began | a running phase, one increment per sample |
| Envelope | attack 8 ms, decay 60 ms to 0.6, release 30 ms; every ramp includes both end values | attack 8 ms, decay 60 ms to 0.62, release 35 ms; ramps are `k / n` and stop one step short |
| Bell | `0.65 sin(f) + 0.28 sin(2f + 0.4 sin(3f)) e^(-8t)` from frequency | `0.66 sin(p) + 0.26 sin(p2 + 0.4 sin(p3)) e^(-8t)` from phase |
| Unknown voice name | sine | pulse |
| Lead and bass pulse duty | 0.28 and 0.50 in `render_song` | 0.35 |
| Mix gains (lead, bass) | 0.24, 0.28 | 0.28, 0.25 |
| Chords | sine pads, 15.5 steps long, once per bar, gain `pad_gain` (0.14) | triangle stabs, 3.6 steps long, every fourth step, gain 0.09 |
| Pattern shorter than the song | repeats | silent past its end |
| Drum styles | `drive`, `clock`, `light` | `drive`, `clock`, `hyper`, `light` |
| Drum voices and gains | own kick, snare, hat and gains | own kick, snare, hat and gains |
| Noise source | `NumpyCompatRng`, which is `numpy.random.default_rng` without NumPy | `random.Random` |
| Pitch slide inside a note (`slide_to`) | yes (only one of the two copies had it; kept) | raises `ValueError` |
| Sweeps | no helper (effects were written as formulas of a time axis: `time_axis`, `cumulative_phase`, `adsr`) | `sweep()` and `env_adsr()` |

Where the three copies agree, there is one implementation: 22,050 Hz mono 16-bit output, `midi_to_hz`, `expand_pattern`, the 64-sample edge fade and 0.78 peak limit, the WAV writer, sample counts (`round`), and `arpeggio` (notes 0.16 s long at gain 0.5, one every `step_s`). The two NumPy copies are identical to each other except that one has `slide_to`.

The note, drum and effect tables of the games themselves are not here: a song is data you pass to `render_song`.

## Functions

| Function | What it does |
| --- | --- |
| `voicing(name="accumulated", rate=22050)` | The voicing object. Both have `add_note`, `add_drum`, `render_song`, `arpeggio`, `silence(seconds)` and `new_rng(seed)`. |
| `v.add_note(buf, start_s, dur_s, midi_note, gain, kind="pulse", duty=0.35, slide_to=None)` | Mixes one note into a list of floats. Kinds: `pulse`, `tri`, `saw`, `sine`, `bell`. |
| `v.add_drum(buf, start_s, kind, gain, rng)` | `kick`, `snare` or `hat` (the accumulated voicing treats any other name as a hat, the closed-form one ignores it). |
| `v.render_song(bpm, bars, melody, bass, chords, lead_kind, bass_kind, drum_style, seed)` | A song of 16 steps per bar; the closed-form one also takes `pad_gain`. Returns a list of floats. |
| `v.arpeggio(notes, step_s, kind)` | A run of short notes. |
| `Accumulated.sweep(dur_s, f0, f1, exp_p, noise_mix, decay_rate, rng)` | A sine sweep with optional noise and decay. |
| `ClosedForm.adsr(n, ...)`, `.osc(freq, n, kind, duty)`, `.time_axis(n)`, `.cumulative_phase(freqs)` | The closed-form building blocks for writing an effect as a formula. |
| `expand_pattern(events, length)`, `midi_to_hz(note)`, `linspace(start, stop, num)` | Pattern table, pitch, and a `numpy.linspace` that gives the same numbers. |
| `vary_events(events, seed, drop=0.0, octave_up=0.0)` | Seeded variation: drops events and lifts some an octave, the same way for the same seed. |
| `to_int16(x)`, `pcm_bytes(x, bits=16, byteorder="little")`, `wav_bytes(x, rate, bits)`, `write_wav(path, x, rate, bits)` | Finishing (edge fade, peak limit 0.78), raw PCM and WAV output. `write_wav` returns the SHA-256. |
| `inspect_wav(data)` | Checks a canonical 44-byte PCM WAV header and returns channels, rate, bits and frames. |
| `log8_encode(pcm16)`, `log8_decode_table()`, `LOG8_MAGNITUDES` | The sign-magnitude logarithmic 8-bit format used by a PS2-style sample bank. |
| `NumpyCompatRng(seed)`, `stdlib_rng(seed)` | The two noise sources. |

## Errors

| Error | Raised when |
| --- | --- |
| `ValueError` | The voicing name is unknown, `rate` is not positive, `bits` is not 8 or 16, `byteorder` is not `little` or `big`, a seed is negative, `slide_to` is used on the accumulated voicing, or a WAV header does not check out. |

## Tests

`pytest` runs without NumPy. `tests/golden.json` holds the SHA-256 of what each original script produced for synthetic songs, notes, drums, sweeps and finishing cases (51 in all, 25 of them rendered by more than one copy with identical bytes). `tests/make_goldens.py` rebuilds it from the three scripts' paths and refuses to write it if copies that should agree do not. If NumPy happens to be installed, one more test checks `NumpyCompatRng` against `numpy.random.default_rng`.

## License

MIT
