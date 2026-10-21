// The full art pack, /torii/art.pak: box art for every No-Intro game of the
// consoles it was built for that libretro-thumbnails has, made on the PC by
// `just art-pack SD` (tools/art_pack.py). Box art is the publishers' work:
// the pack stays on the user's card and is never published.
//
// boxart.c looks a game up here last: custom/<file>.bin, then
// art/<CRC32>.bin, then the pack. The listing job reads the pack's index
// into PSRAM (PSRAM_ARTPACK_BASE) once, so a lookup is a binary search in
// PSRAM and never touches the card; only the cover itself is read, at its
// record's offset.
//
// A file, little-endian:
//
//	offset size
//	0      4    magic "TAPK"
//	4      2    version, ARTPACK_VERSION
//	6      2    0 (reserved)
//	8      4    index entries (CRCs), sorted by CRC, no duplicates
//	12     4    images
//	16     4    bytes of each image record, ARTPACK_RECORD_BYTES
//	20     4    0 (reserved)
//	24          the index: per entry the CRC32 (u32) and its image's number
//	            (u32, below images); many CRCs share one image
//	...         the images, each an asset (engine/asset.h) of COVER_TILES_W x
//	            COVER_TILES_H 4bpp tiles, zero-padded to the record's size,
//	            from offset 24 + entries * 8
//
// A pack whose header is wrong (magic, version, record size), whose index
// holds more entries than ARTPACK_MAX or whose size isn't what its counts
// give, is no pack: logged once, and the box shows the placeholder.
#ifndef TORII_ARTPACK_H
#define TORII_ARTPACK_H

#include <gba_base.h>

#define ARTPACK_PATH "/torii/art.pak"
#define ARTPACK_MAGIC 0x4B504154u  // "TAPK" read as a little-endian word
#define ARTPACK_VERSION 1
#define ARTPACK_HEADER_BYTES 24
#define ARTPACK_ENTRY_BYTES 8
// An image record: a cover's asset with a full 16-colour palette
#define ARTPACK_RECORD_BYTES (16 + 16 * 2 + 10 * 10 * 32)
// Entries the PSRAM index holds (PSRAM_ARTPACK_BYTES / 8)
#define ARTPACK_MAX 16384u
#define ARTPACK_INDEX_MAX_BYTES (ARTPACK_MAX * ARTPACK_ENTRY_BYTES)
// Words of the pack's cluster link map (FatFs fast seek): its first word
// is the size, then two words a fragment and a 0, so 15 fragments at most
#define ARTPACK_MAP_WORDS 32

struct artpack_header {
	u32 magic;
	u16 version;
	u16 reserved0;
	u32 entries;
	u32 images;
	u32 record_bytes;
	u32 reserved1;
};

struct artpack_entry {
	u32 crc;
	u32 image;
};

// 1 if h is a pack this kernel reads, of file_bytes bytes.
int artpack_check(const struct artpack_header *h, u32 file_bytes);
// The image number of crc in the n sorted entries at index, or -1.
s32 artpack_find(const struct artpack_entry *index, u32 n, u32 crc);
// The byte offset of image in a pack of entries index entries.
u32 artpack_image_offset(u32 entries, u32 image);

#endif
