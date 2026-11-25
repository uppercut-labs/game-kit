#define DET_SHORT_TYPES 1
#include "det/det.h"
#include "check.h"

DET_STATIC_ASSERT(TestAssertCompiles, sizeof(u32) == 4u);

int main(void) {
    u8 a = 255u;
    i8 b = -128;
    u16 c = 65535u;
    i16 d = -32768;
    u32 e = 4294967295u;
    i32 f = INT32_MIN;
    u64 g = UINT64_MAX;
    i64 h = INT64_MIN;
    CHECK(sizeof(a) == 1u && sizeof(b) == 1u && sizeof(c) == 2u && sizeof(d) == 2u);
    CHECK(sizeof(e) == 4u && sizeof(f) == 4u && sizeof(g) == 8u && sizeof(h) == 8u);
    CHECK((u8)(a + 1u) == 0u && (u16)(c + 1u) == 0u && (u32)(e + 1u) == 0u);
    CHECK(f == INT32_MIN && h == INT64_MIN && g == UINT64_MAX && b == -128 && d == -32768);
    CHECK(DET_VERSION[0] != '\0');
    return TEST_RESULT("types");
}
