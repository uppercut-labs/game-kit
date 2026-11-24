import json
import math
import struct

import pytest

from port_proof_synthetic import silence, tone, wav_bytes
from uppercut_port_proof import WavError, format_stats, parse_wav, read_wav, wav_stats


def test_a_tone_has_the_peak_and_rms_of_a_sine():
    frames = tone(2.0, amp=8000)
    stats = wav_stats(parse_wav(wav_bytes(frames)), name="t.wav")
    assert stats["file"] == "t.wav" and stats["sample_rate"] == 8000 and stats["channels"] == 1
    assert stats["frames"] == 16000 and stats["seconds"] == 2.0 and stats["total_samples"] == 16000
    assert stats["peak"] == max(abs(f[0]) for f in frames) <= 8000
    assert stats["peak_dbfs"] == round(20 * math.log10(stats["peak"] / 32768), 2)
    assert abs(stats["rms_dbfs"] - 20 * math.log10(8000 / math.sqrt(2) / 32768)) < 0.05
    assert stats["full_scale_samples"] == 0 and stats["full_scale_percent"] == 0
    assert stats["one_second_windows"] == 2 and stats["silent_one_second_windows"] == 0
    assert stats["silent_stretches_1s_or_more"] == [] and stats["longest_silence_s"] == 0.0
    assert stats["silence_threshold_dbfs"] == -66.2


def test_silent_stretches_are_found_at_a_tenth_of_a_second():
    # 1.5 s tone, 2.3 s silence, 1.2 s tone: the silence starts at 1.5 s and runs 2.3 s
    frames = tone(1.5) + silence(2.3) + tone(1.2)
    stats = wav_stats(parse_wav(wav_bytes(frames)))
    assert stats["silent_stretches_1s_or_more"] == [{"start_s": 1.5, "length_s": 2.3}]
    assert stats["longest_silence_s"] == 2.3
    assert stats["one_second_windows"] == 5
    # whole one-second windows: [0,1) tone, [1,2) tone then silence, [2,3) silent, [3,4) silent from 3.8 on only: not silent
    assert stats["silent_one_second_windows"] == 1


def test_a_quiet_stretch_under_a_second_is_not_listed():
    stats = wav_stats(parse_wav(wav_bytes(tone(1.0) + silence(0.6) + tone(1.0))))
    assert stats["silent_stretches_1s_or_more"] == [] and stats["longest_silence_s"] == 0.6


def test_silence_db_moves_the_threshold():
    quiet = [(10,)] * 8000 * 2  # peak 10: -70.3 dBFS
    wav = parse_wav(wav_bytes(quiet))
    assert wav_stats(wav)["silent_one_second_windows"] == 2                       # under 16 of 32768
    assert wav_stats(wav, silence_db=-80)["silent_one_second_windows"] == 0       # 10 is above -80 dBFS
    assert wav_stats(wav, silence_db=-80)["silence_threshold_dbfs"] == -80


def test_full_scale_samples_count_both_signs_and_the_minimum():
    frames = [(32767,), (-32767,), (-32768,), (32766,), (0,)]
    stats = wav_stats(parse_wav(wav_bytes(frames)))
    assert stats["full_scale_samples"] == 3 and stats["peak"] == 32768
    assert stats["full_scale_percent"] == 60.0


def test_channels_share_a_window():
    # left silent, right loud for one second: the window is not silent
    frames = [(0, 9000)] * 8000 + [(0, 0)] * 8000
    stats = wav_stats(parse_wav(wav_bytes(frames)))
    assert stats["channels"] == 2 and stats["total_samples"] == 32000 and stats["frames"] == 16000
    assert stats["one_second_windows"] == 2 and stats["silent_one_second_windows"] == 1
    assert stats["silent_stretches_1s_or_more"] == [{"start_s": 1.0, "length_s": 1.0}]


def test_an_empty_file_has_no_peak():
    stats = wav_stats(parse_wav(wav_bytes([])))
    assert stats["frames"] == 0 and stats["peak"] == 0 and stats["peak_dbfs"] == float("-inf")
    assert stats["one_second_windows"] == 0 and stats["longest_silence_s"] == 0.0


def test_a_header_that_was_never_updated_is_repaired_from_the_file_size():
    frames = tone(1.0)
    for lie in (0, 0xFFFFFFFF, 123456789):
        wav = parse_wav(wav_bytes(frames, data_size=lie, riff_size=lie))
        assert wav.repaired and wav.frames == 8000
        stats = wav_stats(wav)
        assert stats["frames"] == 8000 and stats["header_repaired"] is True
    honest = wav_stats(parse_wav(wav_bytes(frames)))
    assert "header_repaired" not in honest


def test_an_honest_data_size_stops_at_the_chunk_even_with_a_chunk_after_it():
    wav = parse_wav(wav_bytes(tone(0.5), tail=b"LIST" + struct.pack("<I", 4) + b"INFO"))
    assert wav.frames == 4000 and not wav.repaired


def test_a_half_frame_at_the_end_is_dropped():
    raw = wav_bytes([(1, 2), (3, 4)], data_size=0) + b"\x07\x00"  # one stray sample of a two-channel file
    assert parse_wav(raw).frames == 2


@pytest.mark.parametrize(
    "raw, text",
    [
        (b"nope" * 8, "not a WAV"),
        (b"RIFF\0\0\0\0WAVE", "no fmt or data"),
        (b"RIFF\0\0\0\0WAVEfmt " + struct.pack("<I", 16) + struct.pack("<HHIIHH", 3, 1, 8000, 32000, 4, 32) + b"data" + struct.pack("<I", 0), "only 16-bit PCM"),
        (b"RIFF\0\0\0\0WAVEfmt " + struct.pack("<I", 16) + struct.pack("<HHIIHH", 1, 1, 8000, 8000, 1, 8) + b"data" + struct.pack("<I", 0), "only 16-bit PCM"),
    ],
)
def test_what_is_not_16_bit_pcm_is_refused(raw, text):
    with pytest.raises(WavError, match=text):
        parse_wav(raw, "x.wav")


def test_read_wav_and_format_stats(tmp_path):
    path = tmp_path / "a.wav"
    path.write_bytes(wav_bytes(tone(1.0) + silence(1.5)))
    stats = wav_stats(read_wav(path), name=str(path))
    text = format_stats(stats)
    assert text.splitlines()[0].startswith(f"{path}: 2.50 s, 8000 Hz, 1 ch; peak ")
    assert "silent stretches (>= 1 s): 1.5 s at 1.0 s" in text.splitlines()[1]
    json.dumps(stats)  # a plain dict
