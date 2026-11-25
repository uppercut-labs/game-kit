import pytest

from uppercut_chiptune_synth import NumpyCompatRng, seed_sequence_state, stdlib_rng

# Values printed by numpy.random.default_rng(seed).random(3) and .uniform(-1.0, 1.0, 2).
KNOWN = {
    1: [0.5118216247002567, 0.9504636963259353, 0.14415961271963373],
    42: [0.7739560485559633, 0.4388784397520523, 0.8585979199113825],
}


@pytest.mark.parametrize("seed", sorted(KNOWN))
def test_stream_matches_numpy_values(seed):
    rng = NumpyCompatRng(seed)
    assert [rng.random() for _ in range(3)] == KNOWN[seed]


def test_uniform_range():
    rng = NumpyCompatRng(9)
    values = [rng.uniform(-0.35, 0.35) for _ in range(2000)]
    assert min(values) >= -0.35 and max(values) < 0.35


def test_negative_seed_is_refused():
    with pytest.raises(ValueError):
        seed_sequence_state(-1)


def test_stdlib_rng_is_random_random():
    import random

    assert stdlib_rng(5).random() == random.Random(5).random()


def test_matches_numpy_when_installed():
    np = pytest.importorskip("numpy")
    for seed in (0, 1, 2, 42, 123456, 2**33 + 5, 2**70 + 3):
        mine, theirs = NumpyCompatRng(seed), np.random.default_rng(seed)
        assert [mine.uniform(-1.0, 1.0) for _ in range(64)] == theirs.uniform(-1.0, 1.0, 64).tolist()
