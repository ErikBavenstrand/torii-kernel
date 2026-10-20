// A cache of drawn list rows, keyed by item (ui/ui_list.c keeps one in
// EWRAM). Hardware-free, so tests/host runs it.
//
// A row is stored as its 4bpp pixels, tile column after tile column, each
// column its lines from the top, 4 bytes (8 pixels) a line: cols x lines
// words. That is how a list slot's tiles lie when a row fills whole tiles
// (a column of rt tiles is rt * 8 lines in a row), and it doesn't depend on
// it: a row of any height copies into any column stride (row_cache_copy_out),
// so rows drawn at pixel offsets into shared tile rows can use it too.
//
// Rows are keyed by an owner (a list and the model it shows: a new one each
// time the model changes, so old rows never match) and the item. Space is
// a ring: a new row takes the space after the last one, and rows it
// overlaps (the oldest) are dropped, as are the oldest past
// ROW_CACHE_ENTRIES. A lookup reads one entry, the last row stored under
// its key's hint: a list's rows are consecutive items, which never share
// one (two lists' rows may, and the older is then drawn again).
#ifndef TORII_UI_ROWCACHE_H
#define TORII_UI_ROWCACHE_H

#include <gba_base.h>

// Bytes of row pixels the kernel's cache holds: about 12 full-width 16 px
// rows (1,920 bytes each), or 14 of B9's 14 px ones
#define ROW_CACHE_BYTES (24 * 1024)
#define ROW_CACHE_ENTRIES 32
#define ROW_CACHE_HINTS 32  // a power of two

struct row_cache_entry {
	u32 owner;   // 0: free
	u32 item;
	u32 at;      // first word in the arena
	u16 words;   // cols x lines
	u8 cols, lines;
	u32 age;     // when it was stored: the oldest goes first
};

struct row_cache {
	u32 *arena;
	u32 words;   // the arena's size
	u32 head;    // where the next row goes
	u32 owners;  // the last owner handed out
	u32 stores;  // rows stored, for ages
	u32 hits, misses;  // for Diagnostics and tests
	struct row_cache_entry entry[ROW_CACHE_ENTRIES];
	u8 hint[ROW_CACHE_HINTS];  // a key's entry + 1, 0 none
};

// Sets c up over arena (words long), empty.
void row_cache_init(struct row_cache *c, u32 *arena, u32 words);
// Forgets every row (a theme change: rows may draw differently).
void row_cache_clear(struct row_cache *c);
// A new owner: no row stored so far matches it.
u32 row_cache_owner(struct row_cache *c);
// owner's row for item, if stored at this size: its pixels (cols x lines
// words, as above); NULL if not. Counts a hit or a miss.
const u32 *row_cache_find(struct row_cache *c, u32 owner, u32 item, u32 cols, u32 lines);
// Room for owner's row for item, cols x lines words, replacing older rows
// (and any row stored for the same item): the caller writes the pixels
// there before anything else uses the cache. NULL if it can't fit.
u32 *row_cache_put(struct row_cache *c, u32 owner, u32 item, u32 cols, u32 lines);
// Drops owner's rows for items lo to hi (they changed).
void row_cache_forget(struct row_cache *c, u32 owner, u32 lo, u32 hi);
// Copies a stored row to dst, column col at dst + col * stride words
// (stride >= lines), or from src into a row's space (row_cache_copy_in).
void row_cache_copy_out(const u32 *row, u32 *dst, u32 cols, u32 lines, u32 stride);
void row_cache_copy_in(u32 *row, const u32 *src, u32 cols, u32 lines, u32 stride);

#endif
