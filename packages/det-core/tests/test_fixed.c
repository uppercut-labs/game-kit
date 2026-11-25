#include "det/fixed.h"
#include "check.h"

/* a small fixed generator so the sweeps are the same everywhere */
static uint32_t lcg_state = 12345u;
static int32_t next_value(void) {
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return (int32_t)lcg_state;
}

static int32_t floor_div_65536(int32_t v) {
    int32_t q = v / 65536;
    if (v % 65536 < 0) q -= 1;
    return q;
}

int main(void) {
    unsigned i;

    CHECK(DET_FX_ONE == 65536 && DET_FX_HALF == 32768 && DET_FX_SHIFT == 16);
    CHECK(DET_FX_PI == 205887 && DET_FX_TWO_PI == 411775);

    /* conversions */
    CHECK(det_fx_from_int(3) == 3 * 65536 && det_fx_from_int(-3) == -3 * 65536);
    CHECK(det_fx_from_int(40000) == INT32_MAX && det_fx_from_int(-40000) == INT32_MIN);
    CHECK(det_fx_to_int(3 * 65536 + 100) == 3 && det_fx_to_int(65535) == 0);
    CHECK(det_fx_to_int(-1) == -1 && det_fx_to_int(-65536) == -1 && det_fx_to_int(-65537) == -2);
    CHECK(det_fx_to_int(INT32_MIN) == -32768 && det_fx_to_int(INT32_MAX) == 32767);
    for (i = 0u; i < 20000u; i++) {
        const int32_t v = next_value();
        CHECK(det_fx_to_int(v) == floor_div_65536(v));
    }
    CHECK(det_fx_round_to_int(DET_FX_HALF) == 1 && det_fx_round_to_int(DET_FX_HALF - 1) == 0);
    CHECK(det_fx_round_to_int(-DET_FX_HALF) == -1 && det_fx_round_to_int(-DET_FX_HALF + 1) == 0);
    CHECK(det_fx_round_to_int(INT32_MAX) == 32768 && det_fx_round_to_int(INT32_MIN) == -32768);

    /* multiply and divide: truncating, saturating */
    CHECK(det_fx_mul(DET_FX_ONE, DET_FX_ONE) == DET_FX_ONE);
    CHECK(det_fx_mul(3 * DET_FX_ONE, DET_FX_HALF) == 3 * DET_FX_HALF);
    CHECK(det_fx_mul(1, 1) == 0);
    CHECK(det_fx_mul(-1, 1) == 0 && det_fx_mul(-3, DET_FX_HALF) == -1);
    CHECK(det_fx_mul(INT32_MAX, INT32_MAX) == INT32_MAX);
    CHECK(det_fx_mul(INT32_MIN, INT32_MAX) == INT32_MIN);
    CHECK(det_fx_mul(INT32_MIN, INT32_MIN) == INT32_MAX);
    CHECK(det_fx_div(DET_FX_ONE, 2 * DET_FX_ONE) == DET_FX_HALF);
    CHECK(det_fx_div(-DET_FX_ONE, 2 * DET_FX_ONE) == -DET_FX_HALF);
    CHECK(det_fx_div(1, 3 * DET_FX_ONE) == 0);
    CHECK(det_fx_div(DET_FX_ONE, 0) == INT32_MAX && det_fx_div(-DET_FX_ONE, 0) == INT32_MIN);
    CHECK(det_fx_div(0, 0) == INT32_MAX);
    CHECK(det_fx_div(INT32_MAX, 1) == INT32_MAX && det_fx_div(INT32_MIN, 1) == INT32_MIN);
    CHECK(det_fx_div(INT32_MIN, -1) == INT32_MAX);
    CHECK(det_fx_from_ratio(1, 4) == DET_FX_ONE / 4 && det_fx_from_ratio(-1, 4) == -DET_FX_ONE / 4);
    CHECK(det_fx_from_ratio(7, 0) == INT32_MAX && det_fx_from_ratio(-7, 0) == INT32_MIN);
    CHECK(det_fx_from_ratio(1, 3) == 21845);
    for (i = 0u; i < 20000u; i++) {
        const int32_t a = next_value() >> 8;
        const int32_t b = next_value() >> 8;
        const int64_t ref = ((int64_t)a * b) / 65536;
        const int32_t want = ref > INT32_MAX ? INT32_MAX : ref < INT32_MIN ? INT32_MIN : (int32_t)ref;
        CHECK(det_fx_mul(a, b) == want);
    }

    /* abs, min, max, clamp, sign */
    CHECK(det_fx_abs(-5) == 5 && det_fx_abs(5) == 5 && det_fx_abs(0) == 0);
    CHECK(det_fx_abs(INT32_MIN) == INT32_MAX && det_fx_abs(INT32_MAX) == INT32_MAX);
    CHECK(det_fx_min(1, 2) == 1 && det_fx_max(1, 2) == 2 && det_fx_min(-1, -2) == -2);
    CHECK(det_fx_clamp(5, 0, 3) == 3 && det_fx_clamp(-5, 0, 3) == 0 && det_fx_clamp(2, 0, 3) == 2);
    CHECK(det_i32_clamp(5, 0, 3) == 3 && det_i32_clamp(-5, 0, 3) == 0 && det_i32_clamp(2, 0, 3) == 2);
    CHECK(det_fx_sign(9) == DET_FX_ONE && det_fx_sign(-9) == -DET_FX_ONE && det_fx_sign(0) == 0);

    /* lerp */
    CHECK(det_fx_lerp(0, 100 * DET_FX_ONE, 0) == 0);
    CHECK(det_fx_lerp(0, 100 * DET_FX_ONE, DET_FX_ONE) == 100 * DET_FX_ONE);
    CHECK(det_fx_lerp(0, 100 * DET_FX_ONE, DET_FX_HALF) == 50 * DET_FX_ONE);
    CHECK(det_fx_lerp(10 * DET_FX_ONE, 0, DET_FX_HALF) == 5 * DET_FX_ONE);
    CHECK(det_fx_lerp(INT32_MIN, INT32_MAX, DET_FX_ONE) == INT32_MAX);
    CHECK(det_fx_lerp(INT32_MIN, INT32_MAX, INT32_MIN) == INT32_MIN);
    CHECK(det_fx_lerp(INT32_MIN, INT32_MAX, INT32_MAX) == INT32_MAX);
    CHECK(det_fx_lerp(0, DET_FX_ONE, 2 * DET_FX_ONE) == 2 * DET_FX_ONE);

    /* integer and fixed-point square roots */
    CHECK(det_isqrt_u64(0u) == 0u && det_isqrt_u64(1u) == 1u && det_isqrt_u64(3u) == 1u && det_isqrt_u64(4u) == 2u);
    CHECK(det_isqrt_u64(UINT64_MAX) == 4294967295u);
    CHECK(det_isqrt_u64((uint64_t)4294967295u * 4294967295u) == 4294967295u);
    CHECK(det_isqrt_u64((uint64_t)4294967295u * 4294967295u - 1u) == 4294967294u);
    for (i = 0u; i < 5000u; i++) {
        const uint64_t v = ((uint64_t)(uint32_t)next_value() << 32) | (uint32_t)next_value();
        const uint64_t r = det_isqrt_u64(v);
        CHECK(r * r <= v);
        CHECK(r == 4294967295u || (r + 1u) * (r + 1u) > v);
    }
    CHECK(det_fx_sqrt(4 * DET_FX_ONE) == 2 * DET_FX_ONE && det_fx_sqrt(DET_FX_ONE) == DET_FX_ONE);
    CHECK(det_fx_sqrt(0) == 0 && det_fx_sqrt(-DET_FX_ONE) == 0 && det_fx_sqrt(INT32_MIN) == 0);
    CHECK(det_fx_sqrt(2 * DET_FX_ONE) == 92681);
    CHECK(det_fx_sqrt(INT32_MAX) == 11863283);
    CHECK(det_fx_hypot(3 * DET_FX_ONE, 4 * DET_FX_ONE) == 5 * DET_FX_ONE);
    CHECK(det_fx_hypot(-3 * DET_FX_ONE, 4 * DET_FX_ONE) == 5 * DET_FX_ONE);
    CHECK(det_fx_hypot(0, 0) == 0);
    CHECK(det_fx_hypot(INT32_MAX, INT32_MAX) == INT32_MAX);
    CHECK(det_fx_hypot(INT32_MIN, 0) == INT32_MAX);

    /* per tick */
    CHECK(det_fx_per_tick(60 * DET_FX_ONE, 60) == DET_FX_ONE);
    CHECK(det_fx_per_tick(-90, 60) == -1 && det_fx_per_tick(59, 60) == 0);
    return TEST_RESULT("fixed");
}
