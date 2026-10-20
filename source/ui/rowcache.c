// See rowcache.h.
#include <string.h>

#include "rowcache.h"

#ifdef __arm__
#include <gba_systemcalls.h>
#endif

// Copies words (a whole row's columns at once, or one column)
static void copy_words(u32 *dst, const u32 *src, u32 words)
{
#ifdef __arm__
	// The BIOS's copy of 8 words at a time: several times memcpy's speed
	// from ROM
	if (!(words & 7)) {
		CpuFastSet(src, dst, words);
		return;
	}
#endif
	memcpy(dst, src, words * 4);
}

void row_cache_init(struct row_cache *c, u32 *arena, u32 words)
{
	memset(c, 0, sizeof(*c));
	c->arena = arena;
	c->words = words;
}

void row_cache_clear(struct row_cache *c)
{
	for (u32 i = 0; i < ROW_CACHE_ENTRIES; i++)
		c->entry[i].owner = 0;
	memset(c->hint, 0, sizeof(c->hint));
	c->head = 0;
}

static u32 hint_of(u32 owner, u32 item)
{
	return (item + owner * 13) & (ROW_CACHE_HINTS - 1);
}

u32 row_cache_owner(struct row_cache *c)
{
	if (!++c->owners) {
		// After 4 billion owners, start over with an empty cache
		row_cache_clear(c);
		c->owners = 1;
	}
	return c->owners;
}

const u32 *row_cache_find(struct row_cache *c, u32 owner, u32 item, u32 cols, u32 lines)
{
	u32 h = c->hint[hint_of(owner, item)];
	if (h) {
		const struct row_cache_entry *e = &c->entry[h - 1];
		if (owner && e->owner == owner && e->item == item && e->cols == cols && e->lines == lines) {
			c->hits++;
			return c->arena + e->at;
		}
	}
	c->misses++;
	return 0;
}

u32 *row_cache_put(struct row_cache *c, u32 owner, u32 item, u32 cols, u32 lines)
{
	u32 words = cols * lines;
	if (!owner || !words || words > c->words || cols > 0xFF || lines > 0xFF)
		return 0;
	if (c->head + words > c->words)
		c->head = 0;  // wrap: rows never straddle the arena's end
	u32 at = c->head, end = at + words;
	// Drop the rows it overlaps and the item's old row; take a free entry,
	// or the oldest
	struct row_cache_entry *slot = 0;
	for (u32 i = 0; i < ROW_CACHE_ENTRIES; i++) {
		struct row_cache_entry *e = &c->entry[i];
		if (e->owner && ((e->at < end && at < e->at + e->words) || (e->owner == owner && e->item == item)))
			e->owner = 0;
		if (!slot || (slot->owner && (!e->owner || e->age < slot->age)))
			slot = e;
	}
	slot->owner = owner;
	slot->item = item;
	slot->at = at;
	slot->words = (u16)words;
	slot->cols = (u8)cols;
	slot->lines = (u8)lines;
	slot->age = c->stores++;
	c->hint[hint_of(owner, item)] = (u8)(slot - c->entry + 1);
	c->head = end;
	return c->arena + at;
}

void row_cache_forget(struct row_cache *c, u32 owner, u32 lo, u32 hi)
{
	for (u32 i = 0; i < ROW_CACHE_ENTRIES; i++) {
		struct row_cache_entry *e = &c->entry[i];
		if (e->owner == owner && e->item >= lo && e->item <= hi)
			e->owner = 0;
	}
}

void row_cache_copy_out(const u32 *row, u32 *dst, u32 cols, u32 lines, u32 stride)
{
	if (stride == lines) {
		copy_words(dst, row, cols * lines);
		return;
	}
	for (u32 x = 0; x < cols; x++)
		copy_words(dst + x * stride, row + x * lines, lines);
}

void row_cache_copy_in(u32 *row, const u32 *src, u32 cols, u32 lines, u32 stride)
{
	if (stride == lines) {
		copy_words(row, src, cols * lines);
		return;
	}
	for (u32 x = 0; x < cols; x++)
		copy_words(row + x * lines, src + x * stride, lines);
}
