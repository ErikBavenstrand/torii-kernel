// Box art's keys and the set of art files on the card (boxart.h), apart
// from the hardware so the host tests check them.
//
// The card's layout (decisions doc, Art):
//
//	/torii/art/<CRC32>.bin           matched art by the whole file's CRC32,
//	                                 8 hex digits (B23's scraper writes it)
//	/torii/art/custom/<name>.bin     custom art by the game's file name,
//	                                 extension included ("Foo (USA).gba.bin",
//	                                 `just art-add`)
//
// Names match without ASCII case, as FAT does. Each file is one id: a hash
// of its kind and key, never 0, so the cover cache (engine/cover.h) holds a
// cover once however many games use it. The set is an open-addressed table
// of ids in PSRAM (ARTSET_SLOTS words, 0 for empty), written through a put
// callback, as the cart writes PSRAM only through psram_write.
#ifndef TORII_ARTKEY_H
#define TORII_ARTKEY_H

#include <gba_base.h>

#define ARTKEY_EXT ".bin"
#define ARTSET_SLOTS 16384u  // a power of 2
#define ARTSET_MAX 8192u     // files at most: the table stays half empty

enum {
	ARTKEY_NONE = 0,
	ARTKEY_CUSTOM,
	ARTKEY_CRC,
};

// The kind of a file name in /torii/art (ARTKEY_CRC) with its key, the
// CRC32, in *key, or ARTKEY_NONE for any other name (art by GBA game code,
// <CODE>.bin, which v0.4.1's scraper wrote, isn't read since C4).
u32 artkey_parse(const char *name, u32 *key);
// The length of a custom art file's game name: name's length less ".bin"
// (any case), or 0 if it doesn't end so or is nothing else.
u32 artkey_custom_len(const char *name);
// A custom art file's key: the FNV-1a hash of the game's file name (len
// bytes), ASCII case folded.
u32 artkey_name(const char *name, u32 len);
// The id of a kind and key: never 0.
u32 artkey_id(u32 kind, u32 key);

typedef void (*artset_put_fn)(u32 *slot, u32 id);

struct artset {
	u32 *slots;  // ARTSET_SLOTS words, all 0 to start
	artset_put_fn put;
	u32 count;
};

// 1 if the set holds id.
int artset_has(const u32 *slots, u32 id);
// Adds id (an id already there counts once). 0 when the set holds
// ARTSET_MAX ids and id isn't one of them.
int artset_add(struct artset *s, u32 id);

#endif
