/* A stand-in for the PS2SDK's tamtypes.h, for the host test of DET_SDK_TYPES only. Its names are the SDK's; the
 * marker macro lets the test prove that det/types.h took the short names from here. */
#ifndef STUB_TAMTYPES_H
#define STUB_TAMTYPES_H
#define STUB_TAMTYPES_INCLUDED 1
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef signed char s8;
typedef signed short s16;
typedef signed int s32;
typedef signed long long s64;
#endif
