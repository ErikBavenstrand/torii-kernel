// The asset pipeline's hardware-free parts (source/engine): the asset
// header check, the cover cache's bookkeeping and eviction, and the affine
// matrices.
#include <stdlib.h>
#include <string.h>

#include "../../source/crc32.h"
#include "../../source/engine/affine.h"
#include "../../source/engine/asset.h"
#include "../../source/engine/covercache.h"
#include "../kernel/torii_test.h"
#include "../kernel/tests.h"
#include "tests.h"

// --- Assets ---------------------------------------------------------------

// Decodes LZ77 as the BIOS does, writing at most out_max bytes; returns the
// size, or 0 if malformed.
static u32 lz77_decode(const u8 *in, u32 in_size, u8 *out, u32 out_max)
{
	if (in_size < 4 || in[0] != 0x10)
		return 0;
	u32 size = in[1] | in[2] << 8 | (u32)in[3] << 16, i = 4, o = 0;
	if (size > out_max)
		return 0;
	while (o < size) {
		if (i >= in_size)
			return 0;
		u8 flags = in[i++];
		for (int bit = 7; bit >= 0 && o < size; bit--) {
			if (flags & (1 << bit)) {
				if (i + 1 >= in_size)
					return 0;
				u32 v = (u32)in[i] << 8 | in[i + 1];
				i += 2;
				u32 len = (v >> 12) + 3, dist = (v & 0xFFF) + 1;
				// The VRAM decoder (SWI 0x12) can't read distance 1
				if (dist < 2 || dist > o || o + len > size)
					return 0;
				for (u32 k = 0; k < len; k++, o++)
					out[o] = out[o - dist];
			} else {
				if (i >= in_size)
					return 0;
				out[o++] = in[i++];
			}
		}
	}
	return size;
}

static void check_test_asset(const u32 *data, u32 bytes)
{
	const struct asset_header *h = (const void *)data;
	u32 lz_at = ASSET_HEADER_BYTES + asset_palette_bytes(h);
	CHECK_EQ(asset_check(h, bytes, data[lz_at / 4]), ASSET_OK);
	u8 tiles[ASSET_TILE_BYTES_MAX];
	CHECK_EQ(lz77_decode((const u8 *)data + lz_at, h->lz_bytes, tiles, sizeof(tiles)), asset_tile_bytes(h));
}

void test_asset_header(void)
{
	// tools/asset.py's output: the header is right and the tiles decode
	check_test_asset(test_asset_small, test_asset_small_bytes);
	const struct asset_header *small = (const void *)test_asset_small;
	CHECK_EQ(small->bpp, 4);
	CHECK_EQ(small->width, 2);
	CHECK_EQ(small->colours, 4);
	u8 tiles[128];
	u32 lz_at = ASSET_HEADER_BYTES + asset_palette_bytes(small);
	lz77_decode((const u8 *)test_asset_small + lz_at, small->lz_bytes, tiles, sizeof(tiles));
	CHECK_EQ(crc32(tiles, sizeof(tiles)), test_asset_small_crc);
}

void test_asset_check_refuses(void)
{
	struct asset_header h = {ASSET_MAGIC, 4, 8, 8, 0, 16, 0, 512};
	u32 lz = 0x10 | 2048u << 8, file = 16 + 32 + 512;
	CHECK_EQ(asset_check(&h, file, lz), ASSET_OK);
	CHECK_EQ(asset_check(&h, 15, lz), ASSET_ERR_SHORT);
	CHECK_EQ(asset_check(&h, file - 1, lz), ASSET_ERR_SHORT);
	CHECK_EQ(asset_check(&h, file, lz + 0x100), ASSET_ERR_LZ77);
	CHECK_EQ(asset_check(&h, file, (lz & ~0xFFu) | 0x11), ASSET_ERR_LZ77);
	struct asset_header bad = h;
	bad.magic ^= 1;
	CHECK_EQ(asset_check(&bad, file, lz), ASSET_ERR_MAGIC);
	bad = h;
	bad.bpp = 2;
	CHECK_EQ(asset_check(&bad, file, lz), ASSET_ERR_FORMAT);
	bad = h;
	bad.colours = 17;  // over a 4bpp bank
	CHECK_EQ(asset_check(&bad, file, lz), ASSET_ERR_FORMAT);
	bad = h;
	bad.flags = 1;
	CHECK_EQ(asset_check(&bad, file, lz), ASSET_ERR_FORMAT);
	bad = h;
	bad.width = 0;
	CHECK_EQ(asset_check(&bad, file, lz), ASSET_ERR_FORMAT);
	bad = h;
	bad.lz_bytes = 510;  // not words
	CHECK_EQ(asset_check(&bad, file, lz), ASSET_ERR_FORMAT);
	// 8bpp 64x128 is 8 KB, the most; 64x136 is too big for the stage
	bad = h;
	bad.bpp = 8;
	bad.height = 16;
	CHECK_EQ(asset_check(&bad, file, 0x10 | 8192u << 8), ASSET_OK);
	bad.height = 17;
	CHECK_EQ(asset_check(&bad, file, 0x10 | 8704u << 8), ASSET_ERR_TOO_BIG);
	bad = h;
	CHECK_EQ(asset_check(&bad, ASSET_FILE_MAX + 4, lz), ASSET_ERR_TOO_BIG);
	CHECK(strcmp(asset_error_text(ASSET_ERR_LZ77), "the LZ77 data isn't the tiles' size") == 0);
}

// --- The cover cache ------------------------------------------------------

void test_cover_cache_wants(void)
{
	struct cover_cache c;
	cc_init(&c);
	CHECK_EQ(cc_count(&c, COVER_FREE), COVER_ENTRIES);
	CHECK_EQ(cc_next_load(&c), COVER_NONE);

	// Wants make entries; the nearest loads first
	cc_unwant_all(&c);
	u8 far = cc_want(&c, 100, 5);
	u8 near = cc_want(&c, 200, 1);
	u8 mid = cc_want(&c, 300, 3);
	CHECK(far != COVER_NONE && near != COVER_NONE && mid != COVER_NONE);
	CHECK_EQ(cc_find(&c, 200), near);
	CHECK_EQ(cc_next_load(&c), near);
	// Wanting again keeps the nearer distance
	CHECK_EQ(cc_want(&c, 100, 7), far);
	CHECK_EQ(c.entry[far].dist, 5);
	CHECK_EQ(cc_want(&c, 100, 0), far);
	CHECK_EQ(cc_next_load(&c), far);

	// Loading: one at a time, then ready or failed
	cc_load_start(&c, far);
	CHECK_EQ(c.entry[far].state, COVER_LOADING);
	CHECK_EQ(cc_next_load(&c), near);
	cc_load_abort(&c, far);
	CHECK_EQ(c.entry[far].state, COVER_WANTED);
	cc_load_start(&c, far);
	cc_load_finish(&c, far, 1);
	CHECK_EQ(c.entry[far].state, COVER_READY);
	cc_load_start(&c, near);
	cc_load_finish(&c, near, 0);
	CHECK_EQ(c.entry[near].state, COVER_FAILED);
	CHECK_EQ(cc_next_load(&c), mid);
	// Finishing what isn't loading changes nothing
	cc_load_finish(&c, mid, 1);
	CHECK_EQ(c.entry[mid].state, COVER_WANTED);

	// A new batch: what isn't wanted again isn't loaded
	cc_unwant_all(&c);
	CHECK_EQ(cc_next_load(&c), COVER_NONE);
	cc_want(&c, 300, 2);
	CHECK_EQ(cc_next_load(&c), mid);

	cc_invalidate(&c);
	CHECK_EQ(cc_count(&c, COVER_FREE), COVER_ENTRIES);
	CHECK_EQ(cc_find(&c, 100), COVER_NONE);
}

void test_cover_cache_eviction(void)
{
	struct cover_cache c;
	cc_init(&c);
	cc_unwant_all(&c);
	// Fill it: id i at distance i
	for (u32 i = 0; i < COVER_ENTRIES; i++) {
		u8 e = cc_want(&c, 1000 + i, (u16)i);
		CHECK(e != COVER_NONE);
		cc_load_start(&c, e);
		cc_load_finish(&c, e, 1);
	}
	CHECK_EQ(cc_count(&c, COVER_READY), COVER_ENTRIES);
	// A nearer id evicts the furthest
	u8 last = cc_find(&c, 1000 + COVER_ENTRIES - 1);
	CHECK_EQ(cc_want(&c, 5000, 3), last);
	CHECK_EQ(cc_find(&c, 1000 + COVER_ENTRIES - 1), COVER_NONE);
	CHECK_EQ(c.entry[last].state, COVER_WANTED);
	// One as far as everything left evicts nothing
	CHECK_EQ(cc_want(&c, 5001, COVER_ENTRIES - 2), COVER_NONE);

	// Pinned and loading entries are never evicted, however far
	u8 pinned = cc_find(&c, 1000 + COVER_ENTRIES - 2);
	CHECK(cc_pin(&c, pinned));
	u8 loading = cc_find(&c, 1000 + COVER_ENTRIES - 3);
	c.entry[loading].state = COVER_WANTED;
	cc_load_start(&c, loading);
	u8 got = cc_want(&c, 5002, 0);
	CHECK(got != pinned && got != loading && got != COVER_NONE);
	CHECK_EQ(got, cc_find(&c, 5002));
	CHECK_EQ(cc_find(&c, 1000 + COVER_ENTRIES - 4), COVER_NONE);  // the next furthest went
	cc_load_start(&c, got);
	cc_load_finish(&c, got, 1);

	// Unwanted entries go first; among equals, the one holding least
	cc_unwant_all(&c);
	for (u32 i = 0; i < 4; i++)
		cc_want(&c, 1000 + i, (u16)i);  // wanted again
	u8 failed = cc_find(&c, 1010), wanted = cc_find(&c, 5000);
	c.entry[failed].state = COVER_FAILED;
	CHECK_EQ(c.entry[wanted].state, COVER_WANTED);
	CHECK_EQ(cc_want(&c, 6000, 1), wanted);  // nothing loaded yet: dropped first
	CHECK_EQ(cc_want(&c, 6001, 1), failed);  // then a failure
	u8 next = cc_want(&c, 6002, 1);  // then a ready one, not one wanted
	CHECK(next != COVER_NONE && c.entry[next].dist == 1);
	CHECK(cc_find(&c, 1000) != COVER_NONE && cc_find(&c, 1003) != COVER_NONE);

	// Pins
	cc_unpin(&c, pinned);
	CHECK_EQ(c.entry[pinned].pins, 0);
	cc_unpin(&c, pinned);  // not below 0
	CHECK_EQ(c.entry[pinned].pins, 0);
	CHECK(!cc_pin(&c, COVER_NONE));
	struct cover_cache empty;
	cc_init(&empty);
	CHECK(!cc_pin(&empty, 0));  // a free entry
}

// --- Affine matrices ---------------------------------------------------

void test_affine_rotscale(void)
{
	CHECK_EQ(affine_sin(0), 0);
	CHECK_EQ(affine_sin(0x4000), 4096);
	CHECK_EQ(affine_sin(0x8000), 0);
	CHECK_EQ(affine_sin(0xC000), -4096);
	CHECK_EQ(affine_cos(0), 4096);
	CHECK_EQ(affine_sin(0x2000), 2896);  // 45 degrees
	// Interpolated between steps, and odd about 0
	s32 a = affine_sin(0x0080);
	CHECK(a > 0 && a < 101);
	CHECK_EQ(affine_sin((u16)-0x0080), -a);

	s16 m[4];
	affine_rotscale(m, 0x100, 0x100, 0);
	CHECK(m[0] == 0x100 && m[1] == 0 && m[2] == 0 && m[3] == 0x100);
	affine_rotscale(m, 0x200, 0x80, 0);  // twice as wide, half as tall
	CHECK(m[0] == 0x80 && m[3] == 0x200);
	affine_rotscale(m, 0x100, 0x100, 0x4000);  // a quarter turn
	CHECK(m[0] == 0 && m[1] == -0x100 && m[2] == 0x100 && m[3] == 0);
	// Tiny zooms clamp rather than wrap, and zoom 0 doesn't divide by 0
	affine_rotscale(m, 1, 1, 0);
	CHECK_EQ(m[0], 0x7FFF);
	affine_rotscale(m, 0, 0, 0x8000);
	CHECK_EQ(m[0], -0x8000);
}
