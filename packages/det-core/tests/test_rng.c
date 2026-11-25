#include "det/rng.h"
#include "check.h"

int main(void) {
    /* xorshift32 (13, 17, 5) from seed 1: the textbook sequence, pinned */
    static const uint32_t expected[] = { 0x00042021u, 0x04080601u, 0x9dcca8c5u, 0x1255994fu,
                                         0x8ef917d1u, 0x2c6f5bd0u, 0x25b2331au, 0x19f91cb2u };
    det_rng rng = det_rng_seed(1u);
    unsigned i;
    CHECK(rng == 1u);
    for (i = 0u; i < sizeof(expected) / sizeof(expected[0]); i++) {
        rng = det_rng_next(rng);
        CHECK(rng == expected[i]);
    }
    CHECK(det_rng_next(det_rng_seed(1u)) == 270369u);

    {
        /* the state after 10000 steps pins the long run */
        det_rng long_run = det_rng_seed(1u);
        for (i = 0u; i < 10000u; i++) long_run = det_rng_next(long_run);
        CHECK(long_run == 0x6b3fb2f0u);
    }
    {
        /* zero is never a state: seed 0 and a step from zero both land on the fallback */
        CHECK(det_rng_seed(0u) == DET_RNG_FALLBACK);
        CHECK(det_rng_next(0u) == DET_RNG_FALLBACK);
        CHECK(det_rng_seed(7u) == 7u);
    }
    {
        /* bounded draws from the fallback seed, pinned */
        static const uint32_t want[] = { 2u, 5u, 6u, 3u, 0u, 2u };
        det_rng r = det_rng_seed(0u);
        for (i = 0u; i < 6u; i++) CHECK(det_rng_bounded(&r, 10u) == want[i]);
    }
    {
        /* a zero bound and a NULL generator give 0 and leave the generator alone */
        det_rng r = det_rng_seed(99u);
        CHECK(det_rng_bounded(&r, 0u) == 0u && r == 99u);
        CHECK(det_rng_bounded(NULL, 10u) == 0u);
        CHECK(det_rng_bounded(&r, 1u) == 0u && r == det_rng_next(99u));
    }
    {
        /* dice, pinned, and always in range */
        static const int32_t dice[] = { 1, 3, 6, 5, 6, 4 };
        det_rng r = det_rng_seed(1234u);
        for (i = 0u; i < 6u; i++) CHECK(det_rng_range_i32(&r, 1, 6) == dice[i]);
        CHECK(r == 0x83c939cfu);
        for (i = 0u; i < 5000u; i++) {
            const int32_t v = det_rng_range_i32(&r, -3, 3);
            CHECK(v >= -3 && v <= 3);
        }
    }
    {
        /* an empty or reversed range gives the minimum and does not advance */
        det_rng r = det_rng_seed(5u);
        CHECK(det_rng_range_i32(&r, 4, 4) == 4 && r == 5u);
        CHECK(det_rng_range_i32(&r, 9, 2) == 9 && r == 5u);
        CHECK(det_rng_range_i32(NULL, 1, 2) == 1);
    }
    {
        /* the widest range does not overflow: the span is 2^32, which wraps to 0 in 32 bits */
        det_rng r = det_rng_seed(1u);
        const int32_t v = det_rng_range_i32(&r, INT32_MIN, INT32_MAX);
        CHECK(r == 270369u);
        CHECK((uint32_t)v == 270369u + 0x80000000u);
        r = det_rng_seed(3u);
        for (i = 0u; i < 1000u; i++) {
            const int32_t w = det_rng_range_i32(&r, INT32_MAX - 2, INT32_MAX);
            CHECK(w >= INT32_MAX - 2);
        }
        for (i = 0u; i < 1000u; i++) {
            const int32_t w = det_rng_range_i32(&r, INT32_MIN, INT32_MIN + 2);
            CHECK(w <= INT32_MIN + 2);
        }
    }
    {
        /* chance: the edges are exact and do not advance, the middle is about right */
        det_rng r = det_rng_seed(11u);
        unsigned hits = 0u;
        CHECK(det_rng_chance(&r, 0u, 5u) == 0u && r == 11u);
        CHECK(det_rng_chance(&r, 5u, 0u) == 0u && r == 11u);
        CHECK(det_rng_chance(&r, 5u, 5u) == 1u && r == 11u);
        CHECK(det_rng_chance(&r, 9u, 5u) == 1u && r == 11u);
        CHECK(det_rng_chance(NULL, 1u, 2u) == 0u);
        for (i = 0u; i < 10000u; i++) hits += det_rng_chance(&r, 1u, 4u);
        CHECK(hits > 2300u && hits < 2700u);
    }
    {
        /* spread: every face of a six-sided die shows up and none dominates */
        unsigned counts[6] = { 0u, 0u, 0u, 0u, 0u, 0u };
        det_rng r = det_rng_seed(2024u);
        for (i = 0u; i < 6000u; i++) counts[det_rng_bounded(&r, 6u)]++;
        for (i = 0u; i < 6u; i++) CHECK(counts[i] > 850u && counts[i] < 1150u);
    }
    return TEST_RESULT("rng");
}
