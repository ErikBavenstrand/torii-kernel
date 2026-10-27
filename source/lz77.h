// A small LZ77 compressor in the GBA BIOS's format (LZ77UnCompWram, SWI
// 0x11), so Diagnostics can time the BIOS decompressing real data. Kept
// simple rather than tight: it looks back at most LZ77_WINDOW bytes.
#ifndef TORII_LZ77_H
#define TORII_LZ77_H

#include <gba_base.h>

#define LZ77_WINDOW 1024

// The most bytes lz77_compress writes for n bytes of input.
#define LZ77_MAX_OUT(n) (4 + (n) + ((n) + 7) / 8 + 4)

// Compresses n bytes (under 16 MB) of in into out; returns the size written,
// padded to 4 bytes as the BIOS wants.
u32 lz77_compress(const u8 *in, u32 n, u8 *out);

// The same format, fast enough for a save state's 448 KB at boot (v0.8): a
// hash of the next 3 bytes gives one earlier place to try, and distance 2
// (runs) is tried too; greedy, a window of the format's 4 KB. head is
// LZ77_HASH entries of scratch, n at most LZ77_FAST_MAX (32 KB).
#define LZ77_HASH 4096
#define LZ77_FAST_MAX 0x8000
u32 lz77_compress_fast(const u8 *in, u32 n, u8 *out, u16 *head);

#endif
