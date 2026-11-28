/* det-core: xorshift32 random numbers. Part of det-core 1.1.0 (MIT). */
#ifndef DET_RNG_H
#define DET_RNG_H

#include <stdint.h>
#include "det/types.h"

/*
 * xorshift32 (shifts 13, 17, 5) with its whole state in one uint32_t, so a generator can be copied, compared and
 * hashed like any integer, and a replay needs only the seed. The state is never zero, because zero is the one value
 * xorshift cannot leave: det_rng_seed(0) and every step map zero to DET_RNG_FALLBACK.
 *
 *   det_rng rng = det_rng_seed(1234u);
 *   int32_t die = det_rng_range_i32(&rng, 1, 6);
 *
 * The sequence for a given seed is fixed by this file and will not change within a major version. Do not mix it with
 * another generator when replays must match.
 */
typedef uint32_t det_rng;

#define DET_RNG_FALLBACK 0x6d2b79f5u

/* The generator for a seed. A zero seed gives DET_RNG_FALLBACK. */
static inline det_rng det_rng_seed(uint32_t seed) {
    return seed ? seed : DET_RNG_FALLBACK;
}

/* The state after one step. det_rng_next(det_rng_seed(1)) is 270369. */
static inline det_rng det_rng_next(det_rng rng) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng ? rng : DET_RNG_FALLBACK;
}

/*
 * A number in [0, upper_exclusive), taken from the high bits of the new state (a multiply and shift, so there is no
 * modulo bias worth the name and no division). Advances the generator. A NULL generator or a zero bound gives 0
 * and does not advance.
 */
static inline uint32_t det_rng_bounded(det_rng* rng, uint32_t upper_exclusive) {
    if (!rng || upper_exclusive == 0u) return 0u;
    *rng = det_rng_next(*rng);
    return (uint32_t)(((uint64_t)*rng * upper_exclusive) >> 32);
}

/*
 * A number in [minimum, maximum_inclusive]. If maximum_inclusive <= minimum the result is minimum and the
 * generator is not advanced. The span is computed in unsigned arithmetic, so any pair of int32_t values works,
 * including INT32_MIN to INT32_MAX.
 */
static inline int32_t det_rng_range_i32(det_rng* rng, int32_t minimum, int32_t maximum_inclusive) {
    uint32_t span;
    uint32_t offset;
    if (!rng || maximum_inclusive <= minimum) return minimum;
    span = (uint32_t)maximum_inclusive - (uint32_t)minimum + 1u;
    if (span == 0u) {
        *rng = det_rng_next(*rng);
        offset = *rng;
    } else {
        offset = det_rng_bounded(rng, span);
    }
    return (int32_t)((uint32_t)minimum + offset);
}

/*
 * True with probability numerator / denominator. A zero numerator or denominator is false without advancing the
 * generator; numerator >= denominator is true without advancing it.
 */
static inline uint8_t det_rng_chance(det_rng* rng, uint32_t numerator, uint32_t denominator) {
    if (!rng || denominator == 0u || numerator == 0u) return 0u;
    if (numerator >= denominator) return 1u;
    return det_rng_bounded(rng, denominator) < numerator ? 1u : 0u;
}

#endif
