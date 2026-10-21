// The cover cache's bookkeeping: which cover ids have an entry, in which
// state, and which entry to load or evict next. Hardware-free, so the host
// tests check it; engine/cover.h does the loading, and keeps each entry's
// file in PSRAM at the slot with the entry's number.
//
// Priorities: a client says which ids it wants and how far each is from its
// cursor (cover_want, through cc_want here): distance 0 for what is on
// screen, more for neighbours worth prefetching. Each batch of wants starts
// with cc_unwant_all, so ids no longer asked for drop to COVER_DIST_NONE.
// - The loader takes the wanted entry with the smallest distance first.
// - A new id takes a free entry, or evicts the entry with the largest
//   distance (unwanted ones first) if that is further away than the new id.
//   Ties go to entries holding the least: wanted but not loaded, then failed,
//   then loaded. Pinned entries (being copied to VRAM) and the one loading
//   are never evicted.
// - A failed entry stays failed (no retries) until it is evicted or the
//   cache is invalidated.
#ifndef TORII_ENGINE_COVERCACHE_H
#define TORII_ENGINE_COVERCACHE_H

#include <gba_base.h>

#define COVER_ENTRIES 64
#define COVER_NONE 0xFF
#define COVER_DIST_NONE 0xFFFF

// Each entry's PSRAM slot holds its cover's file as read (engine/cover.h
// checks that COVER_FILE_BYTES fits)
#define COVER_SLOT_BYTES 0xCC0
// All the PSRAM the cover cache uses (budget_psram_bytes)
#define COVER_PSRAM_BYTES (COVER_ENTRIES * COVER_SLOT_BYTES)

enum {
	COVER_FREE = 0,
	COVER_WANTED,  // to load
	COVER_LOADING,
	COVER_READY,  // in its PSRAM slot, checked
	COVER_FAILED,
};

struct cover_entry {
	u32 id;
	u16 dist;  // from the cursor; COVER_DIST_NONE: not wanted in this batch
	u8 state;
	u8 pins;  // copies to VRAM under way
};

struct cover_cache {
	struct cover_entry entry[COVER_ENTRIES];
};

void cc_init(struct cover_cache *c);
// The entry holding id, or COVER_NONE.
u8 cc_find(const struct cover_cache *c, u32 id);
// Starts a batch of wants: every entry's distance becomes COVER_DIST_NONE.
void cc_unwant_all(struct cover_cache *c);
// Wants id at dist (the smaller distance wins within a batch). Returns its
// entry, made if need be (as above), or COVER_NONE if every entry is pinned,
// loading or nearer.
u8 cc_want(struct cover_cache *c, u32 id, u16 dist);
// The entry to load next, or COVER_NONE.
u8 cc_next_load(const struct cover_cache *c);
// The loader's steps: start (WANTED to LOADING), give up (back to WANTED,
// when cancelled), finish (READY or FAILED).
void cc_load_start(struct cover_cache *c, u8 index);
void cc_load_abort(struct cover_cache *c, u8 index);
void cc_load_finish(struct cover_cache *c, u8 index, int ok);
// A copy starts or stops using an entry. Pinning fails (0) for a free
// entry or one with 255 pins.
int cc_pin(struct cover_cache *c, u8 index);
void cc_unpin(struct cover_cache *c, u8 index);
// Forgets every entry (PSRAM was overwritten): all free, no pins.
void cc_invalidate(struct cover_cache *c);
// Entries in a state, for tests and Diagnostics.
u32 cc_count(const struct cover_cache *c, u8 state);

#endif
