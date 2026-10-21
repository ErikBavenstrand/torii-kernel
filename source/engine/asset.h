// The asset format: raw tiles and a palette, made on the PC so the GBA only
// copies them (no decoding). tools/asset.py writes it (from a PNG, or a
// generated test pattern) and tools/art.py writes box art in it; the cover
// cache (engine/cover.h) reads box art from SD. Hardware-free, so the host
// tests check it.
//
// A file, little-endian:
//
//	offset size
//	0      4    magic "TAS2"
//	4      1    bits per pixel: 4 or 8
//	5      1    width in 8x8 tiles, 1-32
//	6      1    height in tiles, 1-32
//	7      1    flags, 0 (reserved)
//	8      2    palette colours: 0-16 for 4bpp, 0-256 for 8bpp
//	10     2    0 (reserved)
//	12     4    tile bytes: width * height * bpp * 8
//	16          the palette, BGR555 halfwords, padded to 4 bytes
//	...         the tiles
//
// The tiles are in row order (left to right, then top to bottom): the order
// a sprite with 1D mapping reads them, and the order a BG map lists them.
// Colour 0 is transparent, in a sprite and on a BG alike. A 4bpp asset's
// palette fills one palette bank.
//
// The file's size is 16 + the padded palette + the tile bytes, at most
// ASSET_FILE_MAX. ("TAS1", v0.3's, held LZ77 tiles; nothing reads it.)
#ifndef TORII_ENGINE_ASSET_H
#define TORII_ENGINE_ASSET_H

#include <gba_base.h>

#define ASSET_MAGIC 0x32534154u  // "TAS2" read as a little-endian word
#define ASSET_HEADER_BYTES 16
#define ASSET_MAX_TILES_PER_SIDE 32
// Tiles: a 64x64 8bpp sprite is 4 KB; 8 KB leaves room for two
#define ASSET_TILE_BYTES_MAX 0x2000
#define ASSET_FILE_MAX 0x2400

struct asset_header {
	u32 magic;
	u8 bpp;
	u8 width;  // tiles
	u8 height;  // tiles
	u8 flags;
	u16 colours;
	u16 reserved;
	u32 tile_bytes;
};

// Why asset_check refused
enum {
	ASSET_OK = 0,
	ASSET_ERR_SHORT,  // fewer bytes than a header, or than the header says
	ASSET_ERR_MAGIC,
	ASSET_ERR_FORMAT,  // bpp, size, flags, colours or tile bytes out of range
	ASSET_ERR_TOO_BIG,  // tiles over ASSET_TILE_BYTES_MAX, or file over ASSET_FILE_MAX
};

// Bytes of the palette in the file, padded to 4.
static inline u32 asset_palette_bytes(const struct asset_header *h)
{
	return ((u32)h->colours * 2 + 3) & ~3u;
}

// Bytes of tiles its size gives.
static inline u32 asset_tile_bytes(const struct asset_header *h)
{
	return (u32)h->width * h->height * h->bpp * 8;
}

// Where the tiles start in the file.
static inline u32 asset_tiles_at(const struct asset_header *h)
{
	return ASSET_HEADER_BYTES + asset_palette_bytes(h);
}

// Checks a header against the file's size. Returns ASSET_OK or why not.
int asset_check(const struct asset_header *h, u32 file_bytes);
// Words for asset_check's result, for messages.
const char *asset_error_text(int error);

#endif
