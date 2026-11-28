/* DET_SDK_TYPES, built four times: as a PS2 EE, GameCube and PSP build against the stand-in SDK headers in
 * tests/sdk-stub, and as a plain host build. Each build checks that the short names came from the right place. */
#define DET_SDK_TYPES 1
#include "det/det.h"
#include "check.h"

#if defined(_EE)
#ifndef STUB_TAMTYPES_INCLUDED
#error "an _EE build must take the short names from tamtypes.h"
#endif
#define DET_TEST_PLATFORM "ps2"
#elif defined(GEKKO)
#ifndef STUB_GCTYPES_INCLUDED
#error "a GEKKO build must take the short names from gctypes.h"
#endif
#define DET_TEST_PLATFORM "gamecube"
#elif defined(__PSP__) || defined(__psp__)
#ifndef STUB_PSPTYPES_INCLUDED
#error "a PSP build must take the short names from psptypes.h"
#endif
#define DET_TEST_PLATFORM "psp"
#else
#if defined(STUB_TAMTYPES_INCLUDED) || defined(STUB_GCTYPES_INCLUDED) || defined(STUB_PSPTYPES_INCLUDED)
#error "a host build must not include an SDK header"
#endif
#define DET_TEST_PLATFORM "host"
#endif

#ifndef DET_SHORT_TYPES
#error "DET_SDK_TYPES must imply DET_SHORT_TYPES"
#endif

int main(void) {
    /* These assignments are between pointers to the same type only when the mapping is right; a wrong one is a
       pointer-type warning, which -Werror makes a build error. */
#if defined(_EE)
    s8* signed8 = (i8*)0;
    s16* signed16 = (i16*)0;
    s32* signed32 = (i32*)0;
    s64* signed64 = (i64*)0;
#else
    int8_t* signed8 = (i8*)0;
    int16_t* signed16 = (i16*)0;
    int32_t* signed32 = (i32*)0;
    int64_t* signed64 = (i64*)0;
#endif
#if !defined(_EE) && !defined(GEKKO) && !defined(__PSP__) && !defined(__psp__)
    uint8_t* unsigned8 = (u8*)0;
    uint16_t* unsigned16 = (u16*)0;
    uint32_t* unsigned32 = (u32*)0;
    uint64_t* unsigned64 = (u64*)0;
    CHECK(!unsigned8 && !unsigned16 && !unsigned32 && !unsigned64);
#endif
    u8 a = 255u;
    i8 b = -128;
    u16 c = 65535u;
    i16 d = -32768;
    u32 e = 4294967295u;
    i32 f = INT32_MIN;
    u64 g = UINT64_MAX;
    i64 h = INT64_MIN;
    CHECK(!signed8 && !signed16 && !signed32 && !signed64);
    CHECK((u8)(a + 1u) == 0u && (u16)(c + 1u) == 0u && (u32)(e + 1u) == 0u && (u64)(g + 1u) == 0u);
    CHECK(b == -128 && d == -32768 && f == INT32_MIN && h == INT64_MIN);
    CHECK(det_fx_mul(DET_FX_ONE, (i32)3) == 3);
    return TEST_RESULT("sdk types (" DET_TEST_PLATFORM ")");
}
