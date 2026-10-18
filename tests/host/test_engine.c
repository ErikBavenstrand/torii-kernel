// The engine's hardware-free parts: the range allocator (source/engine/
// pool.c), the resource arenas (res.c) and the budget checker (budget.c).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../source/engine/budget.h"
#include "../../source/engine/pool.h"
#include "../../source/engine/res.h"
#include "../kernel/torii_test.h"
#include "tests.h"

static struct pool_block blocks[POOL_MAX_BLOCKS];

static struct pool make(u8 cap, u16 units)
{
	struct pool p;
	memset(blocks, 0xEE, sizeof(blocks));  // stale slots must not matter
	pool_init(&p, "test", blocks, cap, units, 32);
	return p;
}

void test_pool_alloc_free_merge(void)
{
	struct pool p = make(16, 100);
	CHECK(pool_valid(&p));
	u8 a = pool_alloc(&p, 10, 1, 0, 100, 0, 0);
	u8 b = pool_alloc(&p, 20, 1, 0, 100, 0, 0);
	u8 c = pool_alloc(&p, 30, 1, 0, 100, 0, 0);
	CHECK_EQ(pool_start(&p, a), 0);
	CHECK_EQ(pool_start(&p, b), 10);
	CHECK_EQ(pool_start(&p, c), 30);
	CHECK_EQ(pool_free_units(&p, 0), 40);
	CHECK(pool_valid(&p));
	// Freeing the middle leaves a hole; its neighbours merge into it
	CHECK(pool_unref(&p, b));
	CHECK(pool_valid(&p));
	CHECK_EQ(pool_largest_free(&p, 0), 40);
	u8 d = pool_alloc(&p, 15, 1, 0, 100, 0, 0);  // first fit: the hole
	CHECK_EQ(pool_start(&p, d), 10);
	CHECK(pool_unref(&p, d));
	CHECK(pool_unref(&p, a));
	CHECK(pool_valid(&p));
	u8 merged = pool_alloc(&p, 30, 1, 0, 100, 0, 0);  // 0-30 is one free block again
	CHECK_EQ(pool_start(&p, merged), 0);
	CHECK(pool_unref(&p, merged));
	CHECK(pool_unref(&p, c));
	CHECK(pool_valid(&p));
	CHECK_EQ(pool_largest_free(&p, 0), 100);
	CHECK_EQ(pool_blocks_in_use(&p), 0);
	// Handles that hold nothing are refused
	CHECK(!pool_unref(&p, c));
	CHECK(!pool_unref(&p, 200));
	CHECK(!pool_ref(&p, c));
}

void test_pool_align_range_top(void)
{
	struct pool p = make(16, 2048);
	u8 odd = pool_alloc(&p, 3, 1, 0, 2048, 0, 0);
	u8 map = pool_alloc(&p, 64, 64, 0, 2048, 0, POOL_TOP);
	CHECK_EQ(pool_start(&p, map), 2048 - 64);
	u8 aligned = pool_alloc(&p, 64, 64, 0, 2048, 0, 0);
	CHECK_EQ(pool_start(&p, aligned), 64);  // past the 3 units, aligned
	u8 even = pool_alloc(&p, 2, 2, 0, 2048, 0, 0);
	CHECK_EQ(pool_start(&p, even), 4);
	// A range: within charblock 1 only
	u8 cb1 = pool_alloc(&p, 10, 1, 512, 1536, 0, 0);
	CHECK_EQ(pool_start(&p, cb1), 512);
	u8 top_cb1 = pool_alloc(&p, 10, 1, 512, 1536, 0, POOL_TOP);
	CHECK_EQ(pool_start(&p, top_cb1), 1526);
	// Too big for the range, though the pool has room
	CHECK_EQ(pool_alloc(&p, 1100, 1, 512, 1536, 0, 0), POOL_NONE);
	CHECK_EQ(p.error, POOL_ERR_FULL);
	CHECK(pool_valid(&p));
	// Bad arguments
	CHECK_EQ(pool_alloc(&p, 0, 1, 0, 2048, 0, 0), POOL_NONE);
	CHECK_EQ(p.error, POOL_ERR_ARGS);
	CHECK_EQ(pool_alloc(&p, 4, 3, 0, 2048, 0, 0), POOL_NONE);
	CHECK_EQ(pool_alloc(&p, 4, 1, 100, 100, 0, 0), POOL_NONE);
	CHECK_EQ(pool_alloc(&p, 4, 1, 3000, 4000, 0, 0), POOL_NONE);  // past the end
	(void)odd;
}

void test_pool_tags_cache_revive(void)
{
	struct pool p = make(16, 16);
	u8 a = pool_alloc(&p, 2, 1, 0, 16, 0x10, 0);
	u8 a2 = pool_alloc(&p, 2, 1, 0, 16, 0x10, 0);
	CHECK_EQ(a, a2);
	CHECK_EQ(pool_refs(&p, a), 2);
	CHECK_EQ(pool_alloc(&p, 3, 1, 0, 16, 0x10, 0), POOL_NONE);  // same tag, other size
	CHECK_EQ(p.error, POOL_ERR_TAG_SIZE);
	pool_unref(&p, a);
	pool_unref(&p, a);
	CHECK_EQ(pool_state(&p, a), POOL_CACHED);
	CHECK_EQ(pool_free_units(&p, 0), 14);
	CHECK_EQ(pool_free_units(&p, 1), 16);
	CHECK_EQ(pool_find(&p, 0x10), a);
	// Revived in place, contents and all
	u8 again = pool_alloc(&p, 2, 1, 0, 16, 0x10, 0);
	CHECK_EQ(again, a);
	CHECK_EQ(pool_state(&p, a), POOL_USED);
	CHECK_EQ(pool_refs(&p, a), 1);
	pool_unref(&p, a);

	// Cached blocks go oldest first when space runs out
	u8 b = pool_alloc(&p, 4, 1, 0, 16, 0x20, 0);
	u8 c = pool_alloc(&p, 4, 1, 0, 16, 0x30, 0);
	pool_unref(&p, c);  // cached after a, before b
	pool_unref(&p, b);
	CHECK_EQ(pool_free_units(&p, 0), 6);
	u8 big = pool_alloc(&p, 8, 1, 0, 16, 0, 0);  // the free 6 at the end don't fit
	CHECK(big != POOL_NONE);
	// a went first (no fit yet: it isn't next to the free run), then c;
	// b, cached last, stays
	CHECK_EQ(pool_find(&p, 0x10), POOL_NONE);
	CHECK_EQ(pool_find(&p, 0x30), POOL_NONE);
	CHECK_EQ(pool_find(&p, 0x20), b);
	CHECK_EQ(pool_start(&p, big), 6);
	CHECK(pool_valid(&p));
	pool_flush(&p);
	CHECK_EQ(pool_find(&p, 0x20), POOL_NONE);
	CHECK(pool_valid(&p));
	// The reference cap
	u8 r = pool_alloc(&p, 1, 1, 0, 16, 0x40, 0);
	for (u32 i = 1; i < 255; i++)
		CHECK(pool_ref(&p, r));
	CHECK(!pool_ref(&p, r));
	CHECK_EQ(pool_alloc(&p, 1, 1, 0, 16, 0x40, 0), POOL_NONE);
	CHECK_EQ(p.error, POOL_ERR_REFS);
}

void test_pool_slots(void)
{
	// 4 slots: three 1-unit holes need more blocks than there are
	struct pool p = make(4, 10);
	u8 a = pool_alloc(&p, 1, 1, 0, 10, 0, 0);
	u8 b = pool_alloc(&p, 1, 1, 0, 10, 0, 0);
	u8 c = pool_alloc(&p, 1, 1, 0, 10, 0, 0);
	CHECK(a != POOL_NONE && b != POOL_NONE && c != POOL_NONE);
	CHECK(pool_valid(&p));
	// 4 blocks in use (3 + the free rest): a split now needs a slot
	CHECK_EQ(pool_alloc(&p, 1, 1, 0, 10, 0, 0), POOL_NONE);
	CHECK_EQ(p.error, POOL_ERR_SLOTS);
	// Taking a whole free block needs none
	u8 rest = pool_alloc(&p, 7, 1, 0, 10, 0, 0);
	CHECK(rest != POOL_NONE);
	CHECK(pool_valid(&p));
	CHECK_EQ(pool_free_units(&p, 0), 0);
}

// Random allocations and frees against a unit map, checking the invariants
// and that no two blocks overlap after every step.
void test_pool_random(void)
{
	enum {
		UNITS = 512,
		LIVE = 40
	};
	struct pool p = make(POOL_MAX_BLOCKS, UNITS);
	u8 live[LIVE];
	u32 nlive = 0;
	u32 seed = 12345;
	for (u32 step = 0; step < 20000; step++) {
		seed = seed * 1103515245 + 12345;
		u32 r = seed >> 8;
		if (nlive < LIVE && (r & 3) != 0) {
			u16 len = (u16)(1 + (r >> 4) % 40);
			u16 align = (u16)(1u << ((r >> 10) % 4));
			u16 tag = (r >> 14) % 4 == 0 ? (u16)(1 + (r >> 16) % 12) : 0;
			u8 h = pool_alloc(&p, len, align, 0, UNITS, tag, (r >> 20) & 1 ? POOL_TOP : 0);
			if (h != POOL_NONE) {
				CHECK_EQ(pool_start(&p, h) % align == 0 || tag, 1);
				live[nlive++] = h;
			}
		} else if (nlive) {
			u32 i = (r >> 4) % nlive;
			CHECK(pool_unref(&p, live[i]));
			live[i] = live[--nlive];
		}
		if (!pool_valid(&p)) {
			CHECK(pool_valid(&p));
			return;
		}
		u8 owner[UNITS];
		memset(owner, 0xFF, sizeof(owner));
		for (u32 i = 0; i < nlive; i++) {
			u8 h = live[i];
			CHECK_EQ(pool_state(&p, h), POOL_USED);
			for (u32 u = pool_start(&p, h); u < (u32)pool_start(&p, h) + pool_len(&p, h); u++) {
				if (owner[u] != 0xFF && owner[u] != h) {
					CHECK(!"blocks overlap");
					return;
				}
				owner[u] = h;
			}
		}
	}
}

void test_res_arena(void)
{
	res_errors_quiet = 1;
	res_init();
	u8 global = res_obj_tiles(8, 0, 0);
	u8 mark = res_mark();
	u8 pal = res_palette(RES_BG_PAL, 1, 0x77);
	u8 tiles = res_bg_tiles(100, 0, 0, 0);
	u8 map = res_bg_map(1, 0);
	CHECK(pal != RES_NONE && tiles != RES_NONE && map != RES_NONE);
	CHECK_EQ(res_mark(), mark + 3);
	CHECK(res_fresh(RES_BG_VRAM));
	CHECK_EQ(res_palette(RES_BG_PAL, 1, 0x77), pal);  // shared: not fresh
	CHECK(!res_fresh(RES_BG_PAL));
	CHECK(res_release(RES_BG_PAL, pal));
	// Released out of order, below a view's mark: a gap, and the view
	// above still releases exactly its own
	u8 inner = res_mark();
	u8 child = res_obj_tiles(2, 0, 0);
	CHECK(res_release(RES_BG_VRAM, tiles));
	CHECK_EQ(res_mark(), inner + 1);
	res_release_to(inner);
	CHECK_EQ(pool_state(res_pool(RES_OBJ_VRAM), child), POOL_FREE);
	CHECK_EQ(pool_refs(res_pool(RES_BG_VRAM), map), 1);
	// Releasing the newest entry trims gaps under it
	CHECK(res_release(RES_BG_VRAM, map));
	CHECK_EQ(res_mark(), mark + 1);
	res_release_to(mark);
	CHECK_EQ(res_mark(), mark);
	CHECK_EQ(pool_free_units(res_pool(RES_BG_VRAM), 0), RES_BG_UNITS);
	CHECK_EQ(pool_state(res_pool(RES_BG_PAL), pal), POOL_CACHED);  // tagged: kept
	CHECK_EQ(pool_refs(res_pool(RES_OBJ_VRAM), global), 1);  // below the mark: kept
	// The log's cap
	u32 got = 0;
	for (u32 i = 0; i < RES_LOG_CAP + 4; i++)
		got += res_palette(RES_OBJ_PAL, 1, 0x99) != RES_NONE;
	CHECK_EQ(got, RES_LOG_CAP - 1);
	CHECK(strstr(res_error(), "allocation log is full") != NULL);
	res_release_to(0);
	CHECK_EQ(pool_state(res_pool(RES_OBJ_PAL), pool_find(res_pool(RES_OBJ_PAL), 0x99)), POOL_CACHED);
	// A readable failure
	CHECK_EQ(res_bg_tiles(1025, 0, 0, 0), RES_NONE);
	CHECK(strstr(res_error(), "BG VRAM: can't allocate 1025 units") != NULL);
	res_errors_quiet = 0;
}

void test_budget_check(void)
{
	struct engine_budget have, need = {0};
	char msg[100];
	budget_capacity(&have);
	CHECK_EQ(budget_check(&need, &have, msg, sizeof(msg)), BUDGET_OK);
	CHECK_EQ(msg[0], 0);
	need.bg_tiles = 1024;
	need.bg_maps = 16;  // 32 KB + 32 KB: exactly full
	CHECK_EQ(budget_check(&need, &have, msg, sizeof(msg)), BUDGET_OK);
	need.bg_maps = 17;
	CHECK_EQ(budget_check(&need, &have, msg, sizeof(msg)), BUDGET_BG_VRAM);
	CHECK(strcmp(msg, "needs 67584 bytes of BG VRAM (tiles and maps), 65536 available") == 0);
	need.bg_maps = 1;
	need.obj_tiles = 1025;
	CHECK_EQ(budget_check(&need, &have, msg, sizeof(msg)), BUDGET_OBJ_TILES);
	need.obj_tiles = 0;
	need.affine = 33;
	CHECK_EQ(budget_check(&need, &have, NULL, 0), BUDGET_AFFINE);
	need.affine = 0;
	need.psram_bytes = (8u << 20) + 1;
	CHECK_EQ(budget_check(&need, &have, msg, sizeof(msg)), BUDGET_PSRAM);
	CHECK(strstr(msg, "PSRAM") != NULL);
}

void test_pool_tag_place(void)
{
	struct pool p = make(16, 64);
	u8 a = pool_alloc(&p, 4, 1, 0, 32, 0x10, 0);
	CHECK_EQ(pool_start(&p, a), 0);
	// In use, but outside the range asked for: refused
	CHECK_EQ(pool_alloc(&p, 4, 1, 32, 64, 0x10, 0), POOL_NONE);
	CHECK_EQ(p.error, POOL_ERR_TAG_PLACE);
	// Cached there: dropped, and allocated again where it can be used
	pool_unref(&p, a);
	u8 b = pool_alloc(&p, 4, 1, 32, 64, 0x10, 0);
	CHECK(b != POOL_NONE);
	CHECK_EQ(pool_start(&p, b), 32);
	CHECK_EQ(pool_refs(&p, b), 1);
	pool_unref(&p, b);
	// Cached at another size: the same
	u8 c = pool_alloc(&p, 8, 1, 0, 64, 0x10, 0);
	CHECK(c != POOL_NONE);
	CHECK_EQ(pool_len(&p, c), 8);
	CHECK(pool_valid(&p));
	CHECK_EQ(pool_free_units(&p, 1), 56);
}

// A split needing a slot the pool lacks mustn't hide a run that fits
// exactly, nor cost the cache (found by the adversarial review).
void test_pool_slots_exact_fit(void)
{
	struct pool p = make(5, 40);
	u8 x = pool_alloc(&p, 5, 1, 0, 40, 0, 0);
	u8 f1 = pool_alloc(&p, 15, 1, 0, 40, 0, 0);
	u8 z = pool_alloc(&p, 10, 1, 0, 40, 0, 0);
	u8 f2 = pool_alloc(&p, 5, 1, 0, 40, 0x77, 0);
	u8 w = pool_alloc(&p, 5, 1, 0, 40, 0, 0);
	CHECK(x != POOL_NONE && f1 != POOL_NONE && z != POOL_NONE && f2 != POOL_NONE && w != POOL_NONE);
	pool_unref(&p, f1);  // free 5-20; all 5 slots in use
	CHECK_EQ(pool_alloc(&p, 3, 1, 0, 40, 0, 0), POOL_NONE);  // a split needs a slot
	CHECK_EQ(p.error, POOL_ERR_SLOTS);
	pool_unref(&p, f2);  // cached 30-35
	u8 exact = pool_alloc(&p, 15, 1, 0, 40, 0, 0);  // fits 5-20 exactly, no split
	CHECK_EQ(pool_start(&p, exact), 5);
	CHECK_EQ(pool_find(&p, 0x77), f2);  // the cache survived
	CHECK(pool_valid(&p));
}

// Reclaiming stays oldest first after the age counter wraps.
void test_pool_age_wrap(void)
{
	struct pool p = make(16, 16);
	u8 young = pool_alloc(&p, 1, 1, 0, 16, 0x02, 0);  // 0-1
	pool_unref(&p, young);
	u8 old = pool_alloc(&p, 1, 1, 0, 16, 0x01, 0);  // 1-2, next to the free run
	pool_unref(&p, old);
	for (u32 i = 0; i < 600; i++) {  // revive and cache the other many times
		u8 h = pool_alloc(&p, 1, 1, 0, 16, 0x02, 0);
		CHECK_EQ(h, young);
		pool_unref(&p, h);
	}
	// 15 units fit once the oldest (0x01, at 1-2) goes; 0x02 stays
	u8 big = pool_alloc(&p, 15, 1, 0, 16, 0, 0);
	CHECK_EQ(pool_start(&p, big), 1);
	CHECK_EQ(pool_find(&p, 0x01), POOL_NONE);
	CHECK_EQ(pool_find(&p, 0x02), young);
	CHECK(pool_valid(&p));
}
