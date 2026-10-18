// CRC-32 (IEEE 802.3, as zlib computes it).
#ifndef TORII_CRC32_H
#define TORII_CRC32_H

#include <gba_base.h>

u32 crc32(const void *data, u32 size);
// Continues a CRC32: crc32_update(crc32(a), b) is the CRC of a then b.
u32 crc32_update(u32 crc, const void *data, u32 size);

#endif
