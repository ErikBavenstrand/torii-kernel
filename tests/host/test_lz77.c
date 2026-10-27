// The LZ77 compressor (source/lz77.c), checked against a decompressor that
// works as the GBA BIOS's LZ77UnCompWram does.
#include <stdlib.h>
#include <string.h>

#include "../../source/lz77.h"
#include "../kernel/torii_test.h"
#include "tests.h"

// Returns the decompressed size, or 0 if the data is malformed.
static u32 bios_uncompress(const u8 *in, u32 in_size, u8 *out, u32 out_max)
{
	if (in_size < 4 || in[0] != 0x10)
		return 0;
	u32 size = in[1] | in[2] << 8 | in[3] << 16;
	if (size > out_max)
		return 0;
	u32 i = 4, o = 0;
	while (o < size) {
		if (i >= in_size)
			return 0;
		u8 flags = in[i++];
		for (int bit = 7; bit >= 0 && o < size; bit--) {
			if (flags & (1 << bit)) {
				if (i + 1 >= in_size)
					return 0;
				u32 v = in[i] << 8 | in[i + 1];
				i += 2;
				u32 len = (v >> 12) + 3, dist = (v & 0xFFF) + 1;
				if (dist > o || o + len > size)
					return 0;
				for (u32 k = 0; k < len; k++, o++)
					out[o] = out[o - dist];
			} else {
				out[o++] = in[i++];
			}
		}
	}
	return size;
}

static void round_trip(const u8 *data, u32 n)
{
	u8 *packed = malloc(LZ77_MAX_OUT(n));
	u8 *back = malloc(n + 1);
	u32 size = lz77_compress(data, n, packed);
	CHECK(size <= LZ77_MAX_OUT(n));
	CHECK_EQ(size % 4, 0);
	CHECK_EQ(bios_uncompress(packed, size, back, n + 1), n);
	CHECK(memcmp(back, data, n) == 0);
	// The fast one, as a save state's chunks use it
	if (n <= LZ77_FAST_MAX) {
		static u16 head[LZ77_HASH];
		memset(back, 0xEE, n);
		size = lz77_compress_fast(data, n, packed, head);
		CHECK(size <= LZ77_MAX_OUT(n));
		CHECK_EQ(size % 4, 0);
		CHECK_EQ(bios_uncompress(packed, size, back, n + 1), n);
		CHECK(memcmp(back, data, n) == 0);
	}
	free(back);
	free(packed);
}

void test_lz77_round_trip(void)
{
	u8 *data = calloc(8192, 1);
	round_trip(data, 0);
	memset(data, 7, 8192);  // a run
	round_trip(data, 8192);
	srand(1);
	for (u32 i = 0; i < 8192; i++)  // noise
		data[i] = rand();
	round_trip(data, 8192);
	for (u32 i = 0; i < 8192; i++)  // repeats at many distances
		data[i] = (i % 1000 < 500) ? "torii log"[i % 9] : rand() % 4;
	round_trip(data, 8192);
	for (u32 n = 1; n < 40; n++)  // short inputs
		round_trip(data, n);
	free(data);
	// A save state's chunk: 32 KB of mostly zeros, tiles and a stack
	data = calloc(LZ77_FAST_MAX, 1);
	for (u32 i = 0; i < 0x2000; i++)
		data[0x1000 + i] = (u8)(i * 7 % 13 == 0 ? (u32)rand() : i % 16);
	for (u32 i = 0x6000; i < LZ77_FAST_MAX; i += 4)
		data[i] = (u8)rand();
	round_trip(data, LZ77_FAST_MAX);
	free(data);
}

void test_lz77_compresses(void)
{
	u8 data[4096];
	u8 packed[LZ77_MAX_OUT(4096)];
	for (u32 i = 0; i < sizeof(data); i++)
		data[i] = i % 64;
	CHECK(lz77_compress(data, sizeof(data), packed) < sizeof(data) / 4);
	static u16 head[LZ77_HASH];
	CHECK(lz77_compress_fast(data, sizeof(data), packed, head) < sizeof(data) / 4);
	u8 zeros[4096] = {0};
	CHECK(lz77_compress_fast(zeros, sizeof(zeros), packed, head) < sizeof(zeros) / 8);
}
