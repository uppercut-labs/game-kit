/* det-core: fixed-width types and a compile-time assert. Part of det-core 1.1.0 (MIT). */
#ifndef DET_TYPES_H
#define DET_TYPES_H

#include <stddef.h>
#include <stdint.h>

/* The package version, also read by CMakeLists.txt. */
#define DET_VERSION "1.1.0"

/*
 * A C99 compile-time assert. `name` must be a valid identifier that is unique in its scope, because the assert
 * declares a typedef of that name with a negative array size when the condition is false.
 *
 *   DET_STATIC_ASSERT(FitsInOneByte, sizeof(my_flags) == 1u);
 */
#define DET_STATIC_ASSERT(name, condition) typedef char name[(condition) ? 1 : -1]

/*
 * Short names for the exact-width types (u8 i8 u16 i16 u32 i32 u64 i64). They are opt-in because a project that
 * already has its own typedefs would collide with them. Define one of these before the first include:
 *
 *   DET_SHORT_TYPES  every short name is the <stdint.h> type (u32 is uint32_t, i32 is int32_t).
 *   DET_SDK_TYPES    the short names follow the console SDK, so they match the types in the SDK's own headers and a
 *                    hardware interface type-checks. It implies DET_SHORT_TYPES.
 *
 * With DET_SDK_TYPES:
 *
 *   PlayStation 2 EE (_EE)   u8..u64 and s8..s64 come from <tamtypes.h>; i8..i64 are s8..s64.
 *   GameCube and Wii (GEKKO) u8..u64 come from <gctypes.h>; i8..i64 are the <stdint.h> types.
 *   PSP (__PSP__ or __psp__) u8..u64 come from <psptypes.h>; i8..i64 are the <stdint.h> types.
 *   anything else            every short name is the <stdint.h> type, as with DET_SHORT_TYPES.
 *
 * On those consoles the SDK header defines the unsigned names itself, so a file that also includes that header sees
 * one definition, not two.
 */
#if defined(DET_SDK_TYPES) && !defined(DET_SHORT_TYPES)
#define DET_SHORT_TYPES 1
#endif

#ifdef DET_SHORT_TYPES
#if defined(DET_SDK_TYPES) && defined(_EE)
#include <tamtypes.h>
typedef s8 i8;
typedef s16 i16;
typedef s32 i32;
typedef s64 i64;
#elif defined(DET_SDK_TYPES) && defined(GEKKO)
#include <gctypes.h>
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
#elif defined(DET_SDK_TYPES) && (defined(__PSP__) || defined(__psp__))
#include <psptypes.h>
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
#else
typedef uint8_t u8;
typedef int8_t i8;
typedef uint16_t u16;
typedef int16_t i16;
typedef uint32_t u32;
typedef int32_t i32;
typedef uint64_t u64;
typedef int64_t i64;
#endif

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
