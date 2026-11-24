import pytest

import uppercut_chiptune_synth as u


def test_default_voicing_is_accumulated():
    assert u.voicing().name == "accumulated"
    assert u.voicing("closed-form").name == "closed-form"
    with pytest.raises(ValueError):
        u.voicing("nope")


def test_midi_to_hz_and_pattern():
    assert u.midi_to_hz(69) == 440.0
    assert u.midi_to_hz(81) == pytest.approx(880.0)
    p = u.expand_pattern([(0, 60, 2), (3, 62, 1), (99, 1, 1), (-1, 1, 1)], 8)
    assert p == [(60, 2), None, None, (62, 1), None, None, None, None]


def test_linspace_endpoints_and_steps():
    assert u.linspace(0.0, 1.0, 1) == [0.0]
    assert u.linspace(0.0, 1.0, 0) == []
    out = u.linspace(1.0, 0.5, 5)
    assert out[0] == 1.0 and out[-1] == 0.5 and out[2] == pytest.approx(0.75)
    assert u.linspace(2.0, 2.0, 4) == [2.0] * 4


@pytest.mark.parametrize("name", ["accumulated", "closed-form"])
def test_same_inputs_same_samples(name):
    v = u.voicing(name)
    mel = u.expand_pattern([(0, 60, 2), (4, 64, 2)], 16)
    a = v.render_song(120.0, 1, mel, mel, [[60, 64, 67]], "pulse", "tri", "drive", 5)
    b = v.render_song(120.0, 1, mel, mel, [[60, 64, 67]], "pulse", "tri", "drive", 5)
    c = v.render_song(120.0, 1, mel, mel, [[60, 64, 67]], "pulse", "tri", "drive", 6)
    assert a == b and a != c
    assert len(a) == round(16 * (60.0 / 120.0 / 4.0) * 22050)


def test_pattern_repeat_differs_by_voicing():
    mel = u.expand_pattern([(0, 60, 2)], 16)
    quiet = [None] * 16
    ac, cf = u.voicing("accumulated"), u.voicing("closed-form")
    args = ([[60, 64, 67]], "sine", "sine", "none", 1)
    # Two bars from a one-bar pattern: closed-form repeats it, accumulated goes silent.
    second_bar = slice(len(ac.render_song(60.0, 2, mel, quiet, *args)) // 2 + 2000, None)
    assert max(abs(v) for v in cf.render_song(60.0, 2, mel, quiet, *args)[second_bar]) > 0.05
    # accumulated still has the chord stabs in bar two, so test the lead alone with no chords
    only_lead = ac.render_song(60.0, 2, mel, quiet, [], "sine", "sine", "none", 1)
    assert max(abs(v) for v in only_lead[len(only_lead) // 2 + 2000 :]) == 0.0


def test_slide_is_closed_form_only():
    buf = [0.0] * 3000
    u.voicing("closed-form").add_note(buf, 0.0, 0.1, 60, 0.5, kind="sine", slide_to=72)
    assert any(buf)
    with pytest.raises(ValueError):
        u.voicing("accumulated").add_note(buf, 0.0, 0.1, 60, 0.5, slide_to=72)


def test_rate_option():
    v = u.voicing("accumulated", rate=11025)
    assert len(v.silence(1.0)) == 11025
    with pytest.raises(ValueError):
        u.voicing("accumulated", rate=0)


def test_vary_events_is_seeded():
    ev = [(i, 60 + i, 1) for i in range(40)]
    a = u.vary_events(ev, 3, drop=0.3, octave_up=0.3)
    assert a == u.vary_events(ev, 3, drop=0.3, octave_up=0.3)
    assert a != u.vary_events(ev, 4, drop=0.3, octave_up=0.3)
    assert 0 < len(a) < 40
    assert u.vary_events(ev, 3) == ev
    assert any(n >= 72 for _, n, _ in u.vary_events(ev, 3, octave_up=1.0))
