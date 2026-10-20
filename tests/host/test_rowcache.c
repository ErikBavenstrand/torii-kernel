// The list row cache (source/ui/rowcache.c).
#include <stdlib.h>
#include <string.h>

#include "../../source/ui/rowcache.h"
#include "../kernel/torii_test.h"
#include "tests.h"

static u32 arena[ROW_CACHE_BYTES / 4];
static struct row_cache cache;

// A row's pixels, from its key: what it was stored with can be checked
static u32 pixel(u32 owner, u32 item, u32 word)
{
	return owner * 0x9E3779B9u ^ item * 0x85EBCA6Bu ^ word * 0xC2B2AE35u;
}

static void store(u32 owner, u32 item, u32 cols, u32 lines)
{
	u32 *row = row_cache_put(&cache, owner, item, cols, lines);
	CHECK(row != 0);
	if (row)
		for (u32 w = 0; w < cols * lines; w++)
			row[w] = pixel(owner, item, w);
}

// 1 if the row is stored and holds its pixels
static int holds(u32 owner, u32 item, u32 cols, u32 lines)
{
	const u32 *row = row_cache_find(&cache, owner, item, cols, lines);
	if (!row)
		return 0;
	for (u32 w = 0; w < cols * lines; w++)
		if (row[w] != pixel(owner, item, w))
			return -1;
	return 1;
}

void test_rowcache_keys(void)
{
	row_cache_init(&cache, arena, ROW_CACHE_BYTES / 4);
	u32 a = row_cache_owner(&cache), b = row_cache_owner(&cache);
	CHECK(a && b && a != b);
	CHECK_EQ(holds(a, 3, 30, 16), 0);
	store(a, 3, 30, 16);
	store(b, 3, 30, 16);
	CHECK_EQ(holds(a, 3, 30, 16), 1);
	CHECK_EQ(holds(b, 3, 30, 16), 1);
	// Another size, item or owner doesn't match; owner 0 never does
	CHECK_EQ(holds(a, 3, 30, 14), 0);
	CHECK_EQ(holds(a, 4, 30, 16), 0);
	CHECK_EQ(holds(row_cache_owner(&cache), 3, 30, 16), 0);
	CHECK(row_cache_put(&cache, 0, 1, 30, 16) == 0);
	CHECK(row_cache_find(&cache, 0, 1, 30, 16) == 0);
	// Storing an item again replaces its row
	u32 *row = row_cache_put(&cache, a, 3, 30, 16);
	CHECK(row != 0);
	row[0] = 0x12345678;
	CHECK_EQ(row_cache_find(&cache, a, 3, 30, 16)[0], 0x12345678);
	u32 count = 0;
	for (u32 i = 0; i < ROW_CACHE_ENTRIES; i++)
		count += cache.entry[i].owner == a;
	CHECK_EQ(count, 1);
	// Forgetting a range drops only that owner's rows in it
	for (u32 i = 10; i < 14; i++)
		store(a, i, 30, 16);
	row_cache_forget(&cache, a, 11, 12);
	CHECK_EQ(holds(a, 10, 30, 16), 1);
	CHECK_EQ(holds(a, 11, 30, 16), 0);
	CHECK_EQ(holds(a, 12, 30, 16), 0);
	CHECK_EQ(holds(a, 13, 30, 16), 1);
	CHECK_EQ(holds(b, 3, 30, 16), 1);
	// Too big for the arena: refused
	CHECK(row_cache_put(&cache, a, 1, 255, 255) == 0);
	CHECK(row_cache_put(&cache, a, 1, 300, 16) == 0);
	row_cache_clear(&cache);
	CHECK_EQ(holds(a, 10, 30, 16), 0);
	CHECK_EQ(holds(b, 3, 30, 16), 0);
}

void test_rowcache_eviction(void)
{
	row_cache_init(&cache, arena, ROW_CACHE_BYTES / 4);
	u32 a = row_cache_owner(&cache);
	// Full-width 16 px rows: 480 words, 12 of them fit in 6,144 words
	u32 fit = (ROW_CACHE_BYTES / 4) / (30 * 16);
	CHECK_EQ(fit, 12);
	for (u32 i = 0; i < fit; i++)
		store(a, i, 30, 16);
	for (u32 i = 0; i < fit; i++)
		CHECK_EQ(holds(a, i, 30, 16), 1);
	// The next wraps to the start and drops the oldest row only
	store(a, fit, 30, 16);
	CHECK_EQ(holds(a, 0, 30, 16), 0);
	for (u32 i = 1; i <= fit; i++)
		CHECK_EQ(holds(a, i, 30, 16), 1);
	// Small rows: the entry table runs out first, and the oldest go
	row_cache_init(&cache, arena, ROW_CACHE_BYTES / 4);
	a = row_cache_owner(&cache);
	for (u32 i = 0; i < ROW_CACHE_ENTRIES + 5; i++)
		store(a, i, 2, 8);
	for (u32 i = 0; i < 5; i++)
		CHECK_EQ(holds(a, i, 2, 8), 0);
	for (u32 i = 5; i < ROW_CACHE_ENTRIES + 5; i++)
		CHECK_EQ(holds(a, i, 2, 8), 1);
	CHECK(cache.hits >= ROW_CACHE_ENTRIES && cache.misses >= 5);
}

void test_rowcache_copy(void)
{
	// A 14 px row of 3 columns into tiles whose columns are 3 tiles (24
	// lines) tall, 5 lines down, and back
	u32 row[3 * 14], tiles[3 * 24], back[3 * 14];
	for (u32 i = 0; i < 3 * 14; i++)
		row[i] = i + 1;
	memset(tiles, 0, sizeof(tiles));
	row_cache_copy_out(row, tiles + 5, 3, 14, 24);
	for (u32 x = 0; x < 3; x++)
		for (u32 y = 0; y < 24; y++)
			CHECK_EQ(tiles[x * 24 + y], y >= 5 && y < 19 ? x * 14 + (y - 5) + 1 : 0);
	row_cache_copy_in(back, tiles + 5, 3, 14, 24);
	CHECK(memcmp(back, row, sizeof(row)) == 0);
	// Whole tiles: one block
	u32 slot[2 * 16];
	row_cache_copy_out(row, slot, 2, 16, 16);
	CHECK(memcmp(slot, row, sizeof(slot)) == 0);
}

void test_rowcache_random(void)
{
	// Random stores, finds and forgets of mixed sizes: a row found always
	// holds what was stored for it, and nothing overlaps
	row_cache_init(&cache, arena, ROW_CACHE_BYTES / 4);
	u32 owners[3] = {row_cache_owner(&cache), row_cache_owner(&cache), row_cache_owner(&cache)};
	static const u8 sizes[][2] = {{30, 16}, {30, 14}, {29, 12}, {28, 32}, {4, 8}};
	srand(7);
	u32 found = 0;
	for (u32 step = 0; step < 100000; step++) {
		u32 o = owners[rand() % 3], item = (u32)rand() % 40;
		const u8 *sz = sizes[(o + item) % 5];  // an item keeps its size
		switch (rand() % 8) {
		case 0:
			row_cache_forget(&cache, o, item, item + (u32)rand() % 4);
			break;
		case 1:
		case 2:
		case 3:
			store(o, item, sz[0], sz[1]);
			break;
		default: {
			int h = holds(o, item, sz[0], sz[1]);
			CHECK(h >= 0);
			found += h == 1;
			break;
		}
		}
		if (step % 1000 == 0) {
			for (u32 i = 0; i < ROW_CACHE_ENTRIES; i++) {
				const struct row_cache_entry *e = &cache.entry[i];
				if (!e->owner)
					continue;
				CHECK(e->at + e->words <= cache.words);
				for (u32 j = i + 1; j < ROW_CACHE_ENTRIES; j++) {
					const struct row_cache_entry *f = &cache.entry[j];
					CHECK(!f->owner || f->at >= e->at + e->words || e->at >= f->at + f->words);
					CHECK(!f->owner || f->owner != e->owner || f->item != e->item);
				}
			}
		}
	}
	CHECK(found > 1000);
}

void test_rowcache_two_lists(void)
{
	// Two owners' rows that share a hint: the later one is found, the
	// earlier one is a miss (drawn again), never the wrong row
	row_cache_init(&cache, arena, ROW_CACHE_BYTES / 4);
	u32 a = row_cache_owner(&cache), b = row_cache_owner(&cache);
	for (u32 i = 0; i < 8; i++)
		store(a, i, 30, 16);
	u32 clash = 0;
	while ((clash + b * 13) % ROW_CACHE_HINTS != (3 + a * 13) % ROW_CACHE_HINTS)
		clash++;
	store(b, clash, 30, 16);
	CHECK_EQ(holds(b, clash, 30, 16), 1);
	CHECK_EQ(holds(a, 3, 30, 16), 0);
	for (u32 i = 0; i < 8; i++)
		CHECK_EQ(holds(a, i, 30, 16), i != 3);
	// Storing it again makes it the one found
	store(a, 3, 30, 16);
	CHECK_EQ(holds(a, 3, 30, 16), 1);
	CHECK_EQ(holds(b, clash, 30, 16), 0);
}
