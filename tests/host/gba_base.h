// Stands in for libgba's gba_base.h when kernel code builds for the host,
// and for cppcheck (just analyze):
// its types, and the memory section attributes as no-ops.
#ifndef TORII_HOST_GBA_BASE_H
#define TORII_HOST_GBA_BASE_H

#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef volatile u8 vu8;
typedef volatile u16 vu16;
typedef volatile u32 vu32;

#define IWRAM_CODE
#define EWRAM_CODE
#define IWRAM_DATA
#define EWRAM_DATA
#define EWRAM_BSS
#define ALIGN(m) __attribute__((aligned(m)))
#define BIT(number) (1 << (number))

#endif
