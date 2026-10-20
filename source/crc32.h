// CRC-32 (IEEE 802.3, as zlib computes it).
#ifndef TORII_CRC32_H
#define TORII_CRC32_H

#include <gba_base.h>

// It reads words where it can: never point it at cart SRAM (0x0E000000),
// whose 8-bit bus returns one byte for a word load. Copy SRAM to RAM first.
u32 crc32(const void *data, u32 size);
// Continues a CRC32: crc32_update(crc32(a), b) is the CRC of a then b.
u32 crc32_update(u32 crc, const void *data, u32 size);

#endif
