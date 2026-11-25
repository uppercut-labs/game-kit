/* det-core: fixed-width types and a compile-time assert. Part of det-core 0.1.0 (MIT). */
#ifndef DET_TYPES_H
#define DET_TYPES_H

#include <stddef.h>
#include <stdint.h>

/* The package version, also read by CMakeLists.txt. */
#define DET_VERSION "0.1.0"

/*
 * A C99 compile-time assert. `name` must be a valid identifier that is unique in its scope, because the assert
 * declares a typedef of that name with a negative array size when the condition is false.
 *
 *   DET_STATIC_ASSERT(FitsInOneByte, sizeof(my_flags) == 1u);
 */
#define DET_STATIC_ASSERT(name, condition) typedef char name[(condition) ? 1 : -1]

/*
 * Short names for the exact-width types (u8 i8 u16 i16 u32 i32 u64 i64). They are opt-in because a project that
 * already has its own typedefs would collide with them: define DET_SHORT_TYPES before the first include.
 */
#ifdef DET_SHORT_TYPES
typedef uint8_t u8;
typedef int8_t i8;
typedef uint16_t u16;
typedef int16_t i16;
typedef uint32_t u32;
typedef int32_t i32;
typedef uint64_t u64;
typedef int64_t i64;

DET_STATIC_ASSERT(DetU8MustBe8Bits, sizeof(u8) == 1u);
DET_STATIC_ASSERT(DetI8MustBe8Bits, sizeof(i8) == 1u);
DET_STATIC_ASSERT(DetU16MustBe16Bits, sizeof(u16) == 2u);
DET_STATIC_ASSERT(DetI16MustBe16Bits, sizeof(i16) == 2u);
DET_STATIC_ASSERT(DetU32MustBe32Bits, sizeof(u32) == 4u);
DET_STATIC_ASSERT(DetI32MustBe32Bits, sizeof(i32) == 4u);
DET_STATIC_ASSERT(DetU64MustBe64Bits, sizeof(u64) == 8u);
DET_STATIC_ASSERT(DetI64MustBe64Bits, sizeof(i64) == 8u);
#endif

#endif
