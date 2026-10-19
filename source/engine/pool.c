// See pool.h.
#include "pool.h"

static u8 next_of(const struct pool_block *b)
{
	return b->link & POOL_LINK_END;
}

static u8 state_of(const struct pool_block *b)
{
	return b->link >> 6;
}

static void set_link(struct pool_block *b, u8 next, u8 state)
{
	b->link = (u8)(state << 6 | next);
}

static void set_state(struct pool_block *b, u8 state)
{
	set_link(b, next_of(b), state);
}

static u8 spare_count(const struct pool *pool)
{
	u8 n = 0;
	for (u8 s = pool->spare; s != POOL_LINK_END && n <= pool->cap; s = next_of(&pool->blocks[s]))
		n++;
	return n;
}

static u8 slot_take(struct pool *pool)
{
	u8 s = pool->spare;
	pool->spare = next_of(&pool->blocks[s]);
	return s;
}

static void slot_put(struct pool *pool, u8 s)
{
	struct pool_block *b = &pool->blocks[s];
	b->start = b->len = b->tag = 0;
	b->refs = 0;
	set_link(b, pool->spare, POOL_FREE);
	pool->spare = s;
}

void pool_init(struct pool *pool, const char *name, struct pool_block *blocks, u8 cap, u16 units, u16 unit_bytes)
{
	if (cap > POOL_MAX_BLOCKS)
		cap = POOL_MAX_BLOCKS;
	if (!cap)
		cap = 1;  // blocks must have room for one
	pool->name = name;
	pool->blocks = blocks;
	pool->units = units;
	pool->unit_bytes = unit_bytes;
	pool->cap = cap;
	pool->age = 0;
	pool->error = POOL_OK;
	pool->fresh = 0;
	pool->spare = POOL_LINK_END;
	for (u8 i = cap; i-- > 1;)
		slot_put(pool, i);
	struct pool_block *first = &blocks[0];
	first->start = 0;
	first->len = units;
	first->tag = 0;
	first->refs = 0;
	set_link(first, POOL_LINK_END, POOL_FREE);
	pool->head = 0;
}

u8 pool_find(const struct pool *pool, u16 tag)
{
	if (!tag)
		return POOL_NONE;
	for (u8 i = pool->head; i != POOL_LINK_END; i = next_of(&pool->blocks[i])) {
		const struct pool_block *b = &pool->blocks[i];
		if (state_of(b) != POOL_FREE && b->tag == tag)
			return i;
	}
	return POOL_NONE;
}

// Makes the block free and merges it with free neighbours.
static void free_block(struct pool *pool, u8 h)
{
	struct pool_block *blocks = pool->blocks;
	struct pool_block *b = &blocks[h];
	u8 prev = POOL_LINK_END;
	for (u8 i = pool->head; i != h; i = next_of(&blocks[i]))
		prev = i;
	b->tag = 0;
	b->refs = 0;
	set_state(b, POOL_FREE);
	u8 next = next_of(b);
	if (next != POOL_LINK_END && state_of(&blocks[next]) == POOL_FREE) {
		b->len += blocks[next].len;
		set_link(b, next_of(&blocks[next]), POOL_FREE);
		slot_put(pool, next);
	}
	if (prev != POOL_LINK_END && state_of(&blocks[prev]) == POOL_FREE) {
		blocks[prev].len += b->len;
		set_link(&blocks[prev], next_of(b), POOL_FREE);
		slot_put(pool, h);
	}
}

// The age counter is about to wrap: renumbers the cached blocks 1 to n,
// oldest first, so reclaiming stays oldest first however long the pool lives.
static void renumber_ages(struct pool *pool)
{
	unsigned long long done = 0;  // slots renumbered
	u8 n = 0;
	for (;;) {
		u8 oldest = POOL_NONE, best = 0;
		for (u8 i = pool->head; i != POOL_LINK_END; i = next_of(&pool->blocks[i])) {
			const struct pool_block *b = &pool->blocks[i];
			u8 age = (u8)(pool->age - b->refs);
			if (state_of(b) == POOL_CACHED && !(done >> i & 1) && (oldest == POOL_NONE || age >= best)) {
				oldest = i;
				best = age;
			}
		}
		if (oldest == POOL_NONE)
			break;
		done |= 1ull << oldest;
		pool->blocks[oldest].refs = ++n;
	}
	pool->age = n;
}

// Frees the block cached longest ago; 0 if none is cached.
static int reclaim_oldest(struct pool *pool)
{
	u8 oldest = POOL_NONE, best = 0;
	for (u8 i = pool->head; i != POOL_LINK_END; i = next_of(&pool->blocks[i])) {
		const struct pool_block *b = &pool->blocks[i];
		u8 age = (u8)(pool->age - b->refs);
		if (state_of(b) == POOL_CACHED && (oldest == POOL_NONE || age >= best)) {
			oldest = i;
			best = age;
		}
	}
	if (oldest == POOL_NONE)
		return 0;
	free_block(pool, oldest);
	return 1;
}

// Finds a free block and a start for the allocation; POOL_NONE if no free
// run fits. A run whose split needs more block slots than are spare is only
// a fallback, taken when no run fits without (pool_alloc then reclaims).
static u8 place(const struct pool *pool, u32 len, u32 align, u32 lo, u32 hi, u8 flags, u32 *at_out)
{
	u8 found = POOL_NONE, fallback = POOL_NONE, spare = spare_count(pool);
	u32 fallback_at = 0;
	for (u8 i = pool->head; i != POOL_LINK_END; i = next_of(&pool->blocks[i])) {
		const struct pool_block *b = &pool->blocks[i];
		if (state_of(b) != POOL_FREE)
			continue;
		u32 bend = (u32)b->start + b->len;
		u32 start = b->start > lo ? b->start : lo;
		u32 end = bend < hi ? bend : hi;
		if (end < start + len)
			continue;
		u32 at;
		if (flags & POOL_TOP) {
			at = (end - len) & ~(align - 1);
			if (at < start)
				continue;
		} else {
			at = (start + align - 1) & ~(align - 1);
			if (at + len > end)
				continue;
		}
		u8 need = (at > b->start) + (at + len < bend);
		if (need > spare) {
			if (fallback == POOL_NONE || (flags & POOL_TOP)) {
				fallback = i;
				fallback_at = at;
			}
			continue;
		}
		found = i;
		*at_out = at;
		if (!(flags & POOL_TOP))
			return i;
	}
	if (found == POOL_NONE && fallback != POOL_NONE) {
		*at_out = fallback_at;
		return fallback;
	}
	return found;
}

u8 pool_alloc(struct pool *pool, u16 len, u16 align, u16 lo, u16 hi, u16 tag, u8 flags)
{
	if (hi > pool->units)
		hi = pool->units;
	pool->fresh = 0;
	if (!len || !align || (align & (align - 1)) || lo >= hi) {
		pool->error = POOL_ERR_ARGS;
		return POOL_NONE;
	}
	u8 h = pool_find(pool, tag);
	if (h != POOL_NONE) {
		struct pool_block *b = &pool->blocks[h];
		int fits = b->start >= lo && (u32)b->start + len <= hi && !(b->start & (align - 1));
		if (state_of(b) == POOL_CACHED && (b->len != len || !fits)) {
			// Cached where the caller can't use it: start over
			free_block(pool, h);
			h = POOL_NONE;
		} else if (b->len != len) {
			pool->error = POOL_ERR_TAG_SIZE;
			return POOL_NONE;
		} else if (!fits) {
			pool->error = POOL_ERR_TAG_PLACE;
			return POOL_NONE;
		}
	}
	if (h != POOL_NONE) {
		struct pool_block *b = &pool->blocks[h];
		if (state_of(b) == POOL_CACHED) {
			set_state(b, POOL_USED);
			b->refs = 1;
		} else if (b->refs == 0xFF) {
			pool->error = POOL_ERR_REFS;
			return POOL_NONE;
		} else {
			b->refs++;
		}
		pool->error = POOL_OK;
		pool->fresh = 0;
		return h;
	}
	for (;;) {
		u32 at = 0;
		u8 i = place(pool, len, align, lo, hi, flags, &at);
		if (i != POOL_NONE) {
			struct pool_block *b = &pool->blocks[i];
			u32 end = (u32)b->start + b->len;
			u8 need = (at > b->start) + (at + len < end);
			if (spare_count(pool) >= need) {
				u8 used = i;
				if (at > b->start) {
					// b keeps the free run before; the allocation goes after it
					used = slot_take(pool);
					struct pool_block *u = &pool->blocks[used];
					u->start = (u16)at;
					set_link(u, next_of(b), POOL_FREE);
					b->len = (u16)(at - b->start);
					set_link(b, used, POOL_FREE);
				}
				struct pool_block *u = &pool->blocks[used];
				if (at + len < end) {
					u8 rest = slot_take(pool);
					struct pool_block *r = &pool->blocks[rest];
					r->start = (u16)(at + len);
					r->len = (u16)(end - at - len);
					r->tag = 0;
					r->refs = 0;
					set_link(r, next_of(u), POOL_FREE);
					set_link(u, rest, POOL_FREE);
				}
				u->len = len;
				u->tag = tag;
				u->refs = 1;
				set_state(u, POOL_USED);
				pool->error = POOL_OK;
				pool->fresh = 1;
				return used;
			}
			if ((flags & POOL_KEEP) || !reclaim_oldest(pool)) {
				pool->error = POOL_ERR_SLOTS;
				return POOL_NONE;
			}
			continue;
		}
		if ((flags & POOL_KEEP) || !reclaim_oldest(pool)) {
			pool->error = POOL_ERR_FULL;
			return POOL_NONE;
		}
	}
}

int pool_ref(struct pool *pool, u8 handle)
{
	if (handle >= pool->cap)
		return 0;
	struct pool_block *b = &pool->blocks[handle];
	if (state_of(b) != POOL_USED || b->refs == 0xFF)
		return 0;
	b->refs++;
	return 1;
}

int pool_unref(struct pool *pool, u8 handle)
{
	if (handle >= pool->cap)
		return 0;
	struct pool_block *b = &pool->blocks[handle];
	if (state_of(b) != POOL_USED)
		return 0;
	if (--b->refs)
		return 1;
	if (b->tag) {
		if (pool->age == 0xFF)
			renumber_ages(pool);
		set_state(b, POOL_CACHED);
		b->refs = ++pool->age;
	} else {
		free_block(pool, handle);
	}
	return 1;
}

void pool_flush(struct pool *pool)
{
	while (reclaim_oldest(pool))
		;
}

int pool_drop(struct pool *pool, u8 handle)
{
	if (handle >= pool->cap || state_of(&pool->blocks[handle]) != POOL_CACHED)
		return 0;
	free_block(pool, handle);
	return 1;
}

u16 pool_free_units(const struct pool *pool, int with_cached)
{
	u32 n = 0;
	for (u8 i = pool->head; i != POOL_LINK_END; i = next_of(&pool->blocks[i])) {
		const struct pool_block *b = &pool->blocks[i];
		u8 s = state_of(b);
		if (s == POOL_FREE || (with_cached && s == POOL_CACHED))
			n += b->len;
	}
	return (u16)n;
}

u16 pool_largest_free(const struct pool *pool, int with_cached)
{
	u32 best = 0, run = 0;
	for (u8 i = pool->head; i != POOL_LINK_END; i = next_of(&pool->blocks[i])) {
		const struct pool_block *b = &pool->blocks[i];
		u8 s = state_of(b);
		if (s == POOL_FREE || (with_cached && s == POOL_CACHED)) {
			run += b->len;
			if (run > best)
				best = run;
		} else {
			run = 0;
		}
	}
	return (u16)best;
}

u8 pool_blocks_in_use(const struct pool *pool)
{
	u8 n = 0;
	for (u8 i = pool->head; i != POOL_LINK_END; i = next_of(&pool->blocks[i]))
		n += state_of(&pool->blocks[i]) != POOL_FREE;
	return n;
}

int pool_valid(const struct pool *pool)
{
	u32 pos = 0;
	u8 visited = 0, prev_free = 0;
	for (u8 i = pool->head; i != POOL_LINK_END; i = next_of(&pool->blocks[i])) {
		const struct pool_block *b = &pool->blocks[i];
		u8 s = state_of(b);
		if (i >= pool->cap || ++visited > pool->cap || b->start != pos || !b->len || s > POOL_CACHED)
			return 0;
		if (s == POOL_FREE && prev_free)
			return 0;
		if (s == POOL_USED && !b->refs)
			return 0;
		prev_free = s == POOL_FREE;
		pos += b->len;
	}
	return pos == pool->units && visited + spare_count(pool) == pool->cap;
}

const char *pool_error_text(u8 error)
{
	static const char *const text[] = {
		"no error",
		"no free run fits",
		"out of block slots",
		"bad arguments",
		"tag in use with another size",
		"tag in use outside the range asked for",
		"too many references",
	};
	return error < sizeof(text) / sizeof(*text) ? text[error] : "unknown error";
}
