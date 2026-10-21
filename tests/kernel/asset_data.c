// Test assets for tests/kernel, written by `uv run tools/asset.py demo`:
// don't edit by hand. Generated patterns, no real cover art.
#include "tests.h"

// A 16x16 4bpp checkerboard, for the asset unit tests (in every kernel)
// 152 bytes; the tiles' CRC-32 is 5A797904
const u32 test_asset_small[38] = {
	0x32534154, 0x00020204, 0x00000004, 0x00000080, 0x40000000, 0x001F03FF,
	0x22221113, 0x22221131, 0x22221311, 0x22223111, 0x11132222, 0x11312222,
	0x13112222, 0x31112222, 0x22221111, 0x22221111, 0x22221111, 0x22221111,
	0x11112222, 0x11112222, 0x11112222, 0x11112222, 0x22221111, 0x22221111,
	0x22221111, 0x22221111, 0x11112222, 0x11112222, 0x11112222, 0x11112222,
	0x22221113, 0x22221131, 0x22221311, 0x22223111, 0x11132222, 0x11312222,
	0x13112222, 0x31112222,
};
const u32 test_asset_small_bytes = 152;
const u32 test_asset_small_crc = 0x5A797904;
