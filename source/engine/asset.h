// The asset format: tiles compressed with the BIOS's LZ77, plus a palette.
// tools/asset.py writes it (from a PNG, or a generated test pattern); the
// cover streamer (engine/cover.h) reads it from SD or ROM. Hardware-free, so
// the host tests check it.
//
// A file, little-endian:
//
//	offset size
//	0      4    magic "TAS1"
//	4      1    bits per pixel: 4 or 8
//	5      1    width in 8x8 tiles, 1-32
//	6      1    height in tiles, 1-32
//	7      1    flags, 0 (reserved)
//	8      2    palette colours: 0-16 for 4bpp, 0-256 for 8bpp
//	10     2    0 (reserved)
//	12     4    LZ77 bytes (a multiple of 4)
//	16          the palette, BGR555 halfwords, padded to 4 bytes
//	...         the tiles, LZ77 (BIOS type 0x10), whose header gives
//	            width * height * bpp * 8 bytes
//
// The tiles are in the order a sprite with 1D mapping reads them: left to
// right, then top to bottom. Colour 0 is transparent in a sprite. A 4bpp
// asset's palette fills one palette bank; an 8bpp asset's palette is
// whatever the 256-colour sprite palette holds (see engine/cover.h).
//
// The file's size is 16 + the padded palette + the LZ77 bytes, at most
// ASSET_FILE_MAX. v0.4 (covers) and T14/T15 (theme assets) may add kinds
// through the flags byte; a reader rejects flags it doesn't know.
#ifndef TORII_ENGINE_ASSET_H
#define TORII_ENGINE_ASSET_H

#include <gba_base.h>

#define ASSET_MAGIC 0x31534154u  // "TAS1" read as a little-endian word
#define ASSET_HEADER_BYTES 16
#define ASSET_MAX_TILES_PER_SIDE 32
// Decoded tiles: a 64x64 8bpp sprite is 4 KB; 8 KB leaves room for two
#define ASSET_TILE_BYTES_MAX 0x2000
#define ASSET_FILE_MAX 0x4000

struct asset_header {
	u32 magic;
	u8 bpp;
	u8 width;  // tiles
	u8 height;  // tiles
	u8 flags;
	u16 colours;
	u16 reserved;
	u32 lz_bytes;
};

// Why asset_check refused
enum {
	ASSET_OK = 0,
	ASSET_ERR_SHORT,  // fewer bytes than a header, or than the header says
	ASSET_ERR_MAGIC,
	ASSET_ERR_FORMAT,  // bpp, size, flags or colours out of range
	ASSET_ERR_TOO_BIG,  // decoded tiles over ASSET_TILE_BYTES_MAX, or file over ASSET_FILE_MAX
	ASSET_ERR_LZ77,  // the LZ77 header isn't type 0x10 with the tiles' size
};

// Bytes of the palette in the file, padded to 4.
static inline u32 asset_palette_bytes(const struct asset_header *h)
{
	return ((u32)h->colours * 2 + 3) & ~3u;
}

// Bytes of decoded tiles.
static inline u32 asset_tile_bytes(const struct asset_header *h)
{
	return (u32)h->width * h->height * h->bpp * 8;
}

// Checks a header against the file's size and the first word of its LZ77
// data (lz_word, the BIOS header: type 0x10, then the decoded size).
// Returns ASSET_OK or why not; a file that passes decodes within
// ASSET_TILE_BYTES_MAX, so the BIOS never writes past a buffer that size.
int asset_check(const struct asset_header *h, u32 file_bytes, u32 lz_word);
// Words for asset_check's result, for messages.
const char *asset_error_text(int error);

#endif
