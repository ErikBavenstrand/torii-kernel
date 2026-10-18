// A range allocator over a space of units: BG VRAM, sprite tiles, palette
// banks and affine matrices are each one pool (engine/res.h sets them up).
// Hardware-free, so the host tests run it under ASan and UBSan.
//
// The space is a list of blocks in address order that covers it exactly.
// Each block is free, used (with a reference count) or cached:
// - Allocating takes the first free run that fits (or the last, with
//   POOL_TOP), within a unit range and at an alignment.
// - A tagged allocation (tag != 0) shares any block with the same tag, used
//   or cached: a reference more, and its contents are still there. A cached
//   block of another length, or outside the range or alignment asked for,
//   is freed and the allocation starts over; a used one is an error.
// - Dropping the last reference to a tagged block caches it instead of
//   freeing it (deferred removal), so showing it again is free. An untagged
//   block is freed at once, since nothing could find it again.
// - When no free run fits, cached blocks are reclaimed, oldest first, until
//   one does. Freed blocks merge with free neighbours.
//
// Handles are block slot numbers: they stay the same while the block is used
// or cached, whatever the blocks around it do.
#ifndef TORII_ENGINE_POOL_H
#define TORII_ENGINE_POOL_H

#include <gba_base.h>

#define POOL_NONE 0xFF
#define POOL_MAX_BLOCKS 63  // slot numbers fit in 6 bits

// pool_block.link: the next block's slot in the low 6 bits, the state above
#define POOL_LINK_END 0x3F
enum {
	POOL_FREE = 0,
	POOL_USED = 1,
	POOL_CACHED = 2,
};

struct pool_block {
	u16 start;
	u16 len;
	u16 tag;
	u8 refs;  // POOL_USED: references; POOL_CACHED: the age it was cached at
	u8 link;
};

// Why the last pool_alloc failed
enum {
	POOL_OK = 0,
	POOL_ERR_FULL,  // no free run fits, even with every cached block reclaimed
	POOL_ERR_SLOTS,  // a free run fits but splitting it needs block slots the pool lacks
	POOL_ERR_ARGS,  // a zero length, an alignment that isn't a power of two, an empty range
	POOL_ERR_TAG_SIZE,  // the tag is in use by a block of another length
	POOL_ERR_TAG_PLACE,  // the tag is in use by a block outside the range or alignment asked for
	POOL_ERR_REFS,  // the block has 255 references
};

// pool_alloc flags
#define POOL_TOP 1  // take the highest fitting run, not the lowest

struct pool {
	const char *name;
	struct pool_block *blocks;
	u16 units;
	u16 unit_bytes;  // for messages only
	u8 cap;
	u8 head;  // first block in address order
	u8 spare;  // first unused slot, chained through link
	u8 age;  // bumped each time a block is cached
	u8 error;  // POOL_OK or why the last pool_alloc failed
};

// Sets the pool up as one free block of units; blocks has cap slots (at
// most POOL_MAX_BLOCKS). name and unit_bytes are only for messages.
void pool_init(struct pool *pool, const char *name, struct pool_block *blocks, u8 cap, u16 units, u16 unit_bytes);

// Allocates len units at a multiple of align (a power of two) inside
// [lo, hi), sharing or reviving a block if tag is set. Returns its handle,
// or POOL_NONE with pool->error set.
u8 pool_alloc(struct pool *pool, u16 len, u16 align, u16 lo, u16 hi, u16 tag, u8 flags);
// The used or cached block with this tag, or POOL_NONE; it takes no reference.
u8 pool_find(const struct pool *pool, u16 tag);
// One reference more to a used block; 0 if handle isn't one or has 255.
int pool_ref(struct pool *pool, u8 handle);
// One reference less: at none, the block is cached (tagged) or freed. 0 if
// handle isn't a used block.
int pool_unref(struct pool *pool, u8 handle);
// Frees every cached block.
void pool_flush(struct pool *pool);

static inline u16 pool_start(const struct pool *pool, u8 handle)
{
	return pool->blocks[handle].start;
}
static inline u16 pool_len(const struct pool *pool, u8 handle)
{
	return pool->blocks[handle].len;
}
static inline u8 pool_state(const struct pool *pool, u8 handle)
{
	return pool->blocks[handle].link >> 6;
}
static inline u8 pool_refs(const struct pool *pool, u8 handle)
{
	return pool_state(pool, handle) == POOL_USED ? pool->blocks[handle].refs : 0;
}

// Units in free blocks, plus cached ones if with_cached.
u16 pool_free_units(const struct pool *pool, int with_cached);
// The longest run of free units (counting cached blocks if with_cached).
u16 pool_largest_free(const struct pool *pool, int with_cached);
// Blocks in use: used and cached.
u8 pool_blocks_in_use(const struct pool *pool);
// Checks the block list's invariants (tests): it covers the space in order,
// no two free blocks touch, the slot count adds up. 1 if all hold.
int pool_valid(const struct pool *pool);
// Words for pool->error, for messages.
const char *pool_error_text(u8 error);

#endif
