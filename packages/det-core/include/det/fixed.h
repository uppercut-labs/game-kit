/* det-core: saturating Q16.16 fixed-point arithmetic in int32_t. Part of det-core 1.1.0 (MIT). */
#ifndef DET_FIXED_H
#define DET_FIXED_H

#include <stdint.h>
#include "det/types.h"

/*
 * A fixed-point value is a signed Q16.16 number stored in an int32_t: 16 integer bits, 16 fraction bits, so
 * 1.0 is 65536. Every operation is integer-only, gives the same answer on every compiler and CPU, and never
 * overflows: a result that does not fit is clamped to INT32_MAX or INT32_MIN.
 *
 * Rounding: det_fx_mul, det_fx_div, det_fx_from_ratio and det_fx_lerp truncate toward zero (they divide a 64-bit
 * product). det_fx_to_int rounds toward negative infinity (floor). det_fx_round_to_int rounds half away from zero.
 */
#define DET_FX_SHIFT 16
#define DET_FX_ONE ((int32_t)1 << DET_FX_SHIFT)
#define DET_FX_HALF (DET_FX_ONE >> 1)
#define DET_FX_PI ((int32_t)205887) /* pi * 65536, rounded */
#define DET_FX_TWO_PI ((int32_t)411775) /* 2 * pi * 65536, rounded (not 2 * DET_FX_PI) */

static inline int32_t det_fx_saturate_i64(int64_t value) {
    if (value > INT32_MAX) return INT32_MAX;
    if (value < INT32_MIN) return INT32_MIN;
    return (int32_t)value;
}

static inline int32_t det_fx_from_int(int32_t value) {
    return det_fx_saturate_i64((int64_t)value * (int64_t)DET_FX_ONE);
}

/* Floor. Written without a right shift of a negative number, whose result C leaves to the implementation. */
static inline int32_t det_fx_to_int(int32_t value) {
    if (value >= 0) return (int32_t)((uint32_t)value >> DET_FX_SHIFT);
    return -(int32_t)((((uint32_t)0 - (uint32_t)value) + (uint32_t)(DET_FX_ONE - 1)) >> DET_FX_SHIFT);
}

/* Nearest integer, halves away from zero. */
static inline int32_t det_fx_round_to_int(int32_t value) {
    const int64_t wide = value;
    if (wide >= 0) return (int32_t)((wide + DET_FX_HALF) / DET_FX_ONE);
    return (int32_t)(-(((-wide) + DET_FX_HALF) / DET_FX_ONE));
}

/* numerator / denominator as a fixed-point value. A zero denominator gives INT32_MAX (INT32_MIN for a negative numerator). */
static inline int32_t det_fx_from_ratio(int32_t numerator, int32_t denominator) {
    if (denominator == 0) return numerator >= 0 ? INT32_MAX : INT32_MIN;
    return det_fx_saturate_i64(((int64_t)numerator * (int64_t)DET_FX_ONE) / denominator);
}

static inline int32_t det_fx_mul(int32_t a, int32_t b) {
    return det_fx_saturate_i64(((int64_t)a * (int64_t)b) / DET_FX_ONE);
}

/* A zero denominator gives INT32_MAX (INT32_MIN for a negative numerator). */
static inline int32_t det_fx_div(int32_t numerator, int32_t denominator) {
    if (denominator == 0) return numerator >= 0 ? INT32_MAX : INT32_MIN;
    return det_fx_saturate_i64(((int64_t)numerator * (int64_t)DET_FX_ONE) / denominator);
}

/* INT32_MIN has no positive counterpart, so its absolute value is INT32_MAX. */
static inline int32_t det_fx_abs(int32_t value) {
    if (value == INT32_MIN) return INT32_MAX;
    return value < 0 ? -value : value;
}

static inline int32_t det_fx_min(int32_t a, int32_t b) {
    return a < b ? a : b;
}

static inline int32_t det_fx_max(int32_t a, int32_t b) {
    return a > b ? a : b;
}

static inline int32_t det_fx_clamp(int32_t value, int32_t minimum, int32_t maximum) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

/* The same clamp for plain integers. */
static inline int32_t det_i32_clamp(int32_t value, int32_t minimum, int32_t maximum) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

/* a + (b - a) * t, with t a fixed-point fraction (0 gives a, DET_FX_ONE gives b). t is not clamped. */
static inline int32_t det_fx_lerp(int32_t a, int32_t b, int32_t t) {
    const int64_t delta = (int64_t)b - (int64_t)a;
    const int64_t scaled = (delta * (int64_t)t) / DET_FX_ONE;
    return det_fx_saturate_i64((int64_t)a + scaled);
}

/* +DET_FX_ONE, -DET_FX_ONE or 0. */
static inline int32_t det_fx_sign(int32_t value) {
    if (value > 0) return DET_FX_ONE;
    if (value < 0) return -DET_FX_ONE;
    return 0;
}

/* floor(sqrt(value)) for any 64-bit input. */
static inline uint32_t det_isqrt_u64(uint64_t value) {
    uint64_t result = 0;
    uint64_t bit = (uint64_t)1 << 62;

    while (bit > value) bit >>= 2;
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)result;
}

/* Square root of a fixed-point value. Zero or negative gives 0. */
static inline int32_t det_fx_sqrt(int32_t value) {
    uint32_t root;
    if (value <= 0) return 0;
    root = det_isqrt_u64(((uint64_t)(uint32_t)value) << DET_FX_SHIFT);
    return root > (uint32_t)INT32_MAX ? INT32_MAX : (int32_t)root;
}

/* sqrt(x * x + y * y), computed in 64 bits so it cannot overflow before the square root. */
static inline int32_t det_fx_hypot(int32_t x, int32_t y) {
    const uint64_t xx = (uint64_t)((int64_t)x * x);
    const uint64_t yy = (uint64_t)((int64_t)y * y);
    const uint32_t root = det_isqrt_u64(xx + yy);
    return root > (uint32_t)INT32_MAX ? INT32_MAX : (int32_t)root;
}

/* A rate per second as a rate per tick (integer division, toward zero). ticks_per_second must not be zero. */
static inline int32_t det_fx_per_tick(int32_t per_second, int32_t ticks_per_second) {
    return per_second / ticks_per_second;
}

#endif
