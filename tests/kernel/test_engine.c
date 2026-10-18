// The engine core (source/engine): allocators, the VRAM queue's caps, the
// shadow state's commit, jobs within a budget, the cycle counter.
//
// These run in every kernel's Diagnostics too, while its mode 3 screen
// shows: they touch only what mode 3 leaves unused (sprite VRAM past
// 0x06014000, OAM, the sprite palette), never DISPCNT, and leave the
// engine's state reset (engine shadow state not dirty, pools empty). Once a
// screen runs on the engine (T12), run them where its state can be lost.
#include <string.h>

#include "torii_test.h"
#include "tests.h"
#include "../../source/engine/engine.h"

#define REG_VCOUNT_T (*(vu16 *)0x04000006)
#define OAM_T ((vu16 *)0x07000000)
#define OBJ_PAL_T ((vu16 *)0x05000200)
// The last 4 KB of sprite VRAM: free in mode 3 and unused by Diagnostics
#define TEST_VRAM ((u32 *)0x06017000)

// Waits for the start of VBlank (line 160), polling: the test kernel runs
// its tests before interrupts are set up.
static void wait_vblank_start(void)
{
	while (REG_VCOUNT_T >= 160)
		;
	while (REG_VCOUNT_T < 160)
		;
}

// Leaves the engine as tests found it: nothing pending, pools empty. With
// quiet, engine errors aren't logged: a test of the caps expects them, and
// Diagnostics' log on the card shouldn't show them.
static void engine_quiet(int quiet)
{
	gfx_init();
	gfx_dirty_regs = 0;
	gfx_dirty_pal = 0;
	res_init();
	jobs_init();
	res_errors_quiet = (u8)quiet;
}

void test_engine_res_alloc(void)
{
	engine_quiet(1);
	struct pool *bg = res_pool(RES_BG_VRAM);

	// Tiles from the bottom of their charblock, maps from the top of VRAM
	u8 tiles = res_bg_tiles(40, 0, 1, 0);
	CHECK(tiles != RES_NONE);
	CHECK_EQ(res_start(RES_BG_VRAM, tiles), 512);
	CHECK_EQ(res_bg_tile_index(tiles, 0, 1), 0);
	CHECK_EQ((u32)res_bg_addr(tiles), 0x06004000);
	u8 map = res_bg_map(1, 0);
	CHECK_EQ(res_bg_screenblock(map), 31);
	u8 map2 = res_bg_map(2, 0);
	CHECK_EQ(res_bg_screenblock(map2), 29);
	u8 tiles8 = res_bg_tiles(3, 1, 0, 0);
	CHECK_EQ(res_start(RES_BG_VRAM, tiles8), 0);
	CHECK_EQ(res_bg_tile_index(tiles8, 1, 0), 0);
	CHECK(pool_valid(bg));

	// Freeing merges back into one free run
	CHECK(res_release(RES_BG_VRAM, map));
	CHECK(res_release(RES_BG_VRAM, tiles));
	CHECK(res_release(RES_BG_VRAM, tiles8));
	CHECK(res_release(RES_BG_VRAM, map2));
	CHECK(!res_release(RES_BG_VRAM, map2));  // already gone
	CHECK_EQ(pool_free_units(bg, 0), RES_BG_UNITS);
	CHECK_EQ(pool_largest_free(bg, 0), RES_BG_UNITS);
	CHECK(pool_valid(bg));

	// Tagged palettes are shared, then cached and revived
	u8 a = res_palette(RES_OBJ_PAL, 1, 0x1234);
	u8 b = res_palette(RES_OBJ_PAL, 1, 0x1234);
	CHECK(a != RES_NONE);
	CHECK_EQ(a, b);
	CHECK_EQ(pool_refs(res_pool(RES_OBJ_PAL), a), 2);
	res_release(RES_OBJ_PAL, a);
	res_release(RES_OBJ_PAL, b);
	CHECK_EQ(pool_state(res_pool(RES_OBJ_PAL), a), POOL_CACHED);
	u8 c = res_palette(RES_OBJ_PAL, 1, 0x1234);
	CHECK_EQ(c, a);
	CHECK_EQ(pool_state(res_pool(RES_OBJ_PAL), c), POOL_USED);
	res_release(RES_OBJ_PAL, c);

	// Caps: 16 banks, then a readable error
	u8 mark = res_mark();
	u32 got = 0;
	for (u32 i = 0; i < 16; i++)
		got += res_palette(RES_BG_PAL, 1, 0) != RES_NONE;
	CHECK_EQ(got, 16);
	CHECK_EQ(res_palette(RES_BG_PAL, 1, 0), RES_NONE);
	CHECK(strstr(res_error(), "BG palette") != NULL);
	CHECK(strstr(res_error(), "no free run fits") != NULL);
	// The arena gives everything back
	res_release_to(mark);
	CHECK_EQ(res_mark(), mark);
	CHECK_EQ(pool_free_units(res_pool(RES_BG_PAL), 0), RES_PAL_BANKS);

	// 8bpp sprites start on an even tile
	u8 odd = res_obj_tiles(1, 0, 0);
	u8 even = res_obj_tiles(4, 1, 0);
	CHECK_EQ(res_start(RES_OBJ_VRAM, even), 2);
	CHECK_EQ((u32)res_obj_addr(even), 0x06010040);
	res_release(RES_OBJ_VRAM, odd);
	res_release(RES_OBJ_VRAM, even);

	// Affine matrices: 32 of them
	mark = res_mark();
	for (u32 i = 0; i < 32; i++)
		res_affine(0);
	CHECK_EQ(res_affine(0), RES_NONE);
	res_release_to(mark);
	CHECK_EQ(pool_free_units(res_pool(RES_AFFINE), 0), RES_AFFINE_MATS);
	engine_quiet(0);
}

void test_engine_budget(void)
{
	engine_quiet(1);
	struct engine_budget need = {.bg_tiles = 512, .bg_maps = 2, .obj_tiles = 64, .bg_banks = 4, .affine = 2};
	CHECK_EQ(budget_check_layout("test", &need), BUDGET_OK);
	need.bg_maps = 32;  // 64 KB of maps and 16 KB of tiles
	CHECK_EQ(budget_check_layout("test", &need), BUDGET_BG_VRAM);
	CHECK(strstr(res_error(), "layout test is over budget") != NULL);
	need.bg_maps = 1;
	need.obj_banks = 17;
	CHECK_EQ(budget_check_layout("test", &need), BUDGET_OBJ_BANKS);
	// What's allocated counts
	need.obj_banks = 16;
	CHECK_EQ(budget_check_layout("test", &need), BUDGET_OK);
	u8 h = res_palette(RES_OBJ_PAL, 1, 0);
	CHECK_EQ(budget_check_layout("test", &need), BUDGET_OBJ_BANKS);
	res_release(RES_OBJ_PAL, h);
	engine_quiet(0);
}

void test_engine_queue_caps(void)
{
	engine_quiet(1);
	const u32 *rom = (const u32 *)0x08000000;
	for (u32 i = 0; i < 1024; i++)
		TEST_VRAM[i] = 0;

	// Bad copies and a full queue are refused
	CHECK(!gfx_queue_copy((u8 *)TEST_VRAM + 2, rom, 16));
	CHECK(!gfx_queue_copy(TEST_VRAM, rom, 6));
	CHECK(!gfx_queue_fill(TEST_VRAM, 0, 0));
	for (u32 i = 0; i < GFX_QUEUE_CAP; i++)
		CHECK(gfx_queue_fill(TEST_VRAM, 0, 4));
	CHECK(!gfx_queue_fill(TEST_VRAM, 0, 4));
	gfx_queue_clear();

	// The byte cap: 3 KB in two commits, the second finishing the copy
	CHECK(gfx_queue_copy(TEST_VRAM, rom, 3072));
	wait_vblank_start();
	CHECK_EQ(gfx_commit_queue(2048, 228), 2048);
	CHECK_EQ(gfx_queue_len(), 1);
	CHECK_EQ(TEST_VRAM[511], rom[511]);
	CHECK_EQ(TEST_VRAM[512], 0);
	wait_vblank_start();
	CHECK_EQ(gfx_commit_queue(2048, 228), 1024);
	CHECK_EQ(gfx_queue_len(), 0);
	CHECK(memcmp(TEST_VRAM, rom, 3072) == 0);

	// Fills
	CHECK(gfx_queue_fill(TEST_VRAM + 8, 0xA5A55A5A, 32));
	wait_vblank_start();
	CHECK_EQ(gfx_commit_queue(GFX_COMMIT_BYTES, 228), 32);
	CHECK_EQ(TEST_VRAM[7], rom[7]);
	CHECK_EQ(TEST_VRAM[8], 0xA5A55A5A);
	CHECK_EQ(TEST_VRAM[15], 0xA5A55A5A);
	CHECK_EQ(TEST_VRAM[16], rom[16]);

	// VCOUNT: nothing outside VBlank, and nothing at or past the last line
	CHECK(gfx_queue_fill(TEST_VRAM, 0, 4096));
	while (REG_VCOUNT_T >= 160)
		;
	CHECK_EQ(gfx_commit_queue(GFX_COMMIT_BYTES, 228), 0);
	wait_vblank_start();
	CHECK_EQ(gfx_commit_queue(GFX_COMMIT_BYTES, 160), 0);
	// A line or two of VBlank moves some of it, never all
	wait_vblank_start();
	u32 moved = gfx_commit_queue(GFX_COMMIT_BYTES, 162);
	CHECK(moved >= GFX_CHUNK && moved < 4096);
	CHECK_EQ(gfx_queue_len(), 1);
	gfx_queue_clear();
	engine_quiet(0);
}

void test_engine_commit(void)
{
	engine_quiet(1);
	// Setters mark only real changes
	gfx_set_scroll(1, 5, 7);
	CHECK_EQ(gfx_dirty_regs, GFX_DIRTY_BGOFS);
	CHECK_EQ(gfx_regs.bgofs[2], 5);
	CHECK_EQ(gfx_regs.bgofs[3], 7);
	gfx_dirty_regs = 0;
	gfx_set_scroll(1, 5, 7);
	CHECK_EQ(gfx_dirty_regs, 0);
	CHECK(!gfx_pending());

	// Palettes: the dirty banks, from the buffer in use
	static const u16 colours[16] = {0x001F, 0x03E0, 0x7C00, 0x7FFF};
	u16 kept[16];
	for (u32 i = 0; i < 16; i++)
		kept[i] = OBJ_PAL_T[15 * 16 + i];
	gfx_pal_load(31, colours, 16);
	CHECK_EQ(gfx_dirty_pal, 1u << 31);
	gfx_commit();
	CHECK_EQ(OBJ_PAL_T[15 * 16 + 2], 0x7C00);
	CHECK_EQ(gfx_dirty_pal, 0);
	gfx_pal_faded[31 * 16 + 2] = 0x1234;
	gfx_pal_use_faded(1);
	CHECK_EQ(gfx_dirty_pal, 0xFFFFFFFF);
	gfx_dirty_pal = 1u << 31;  // keep to the sprite bank mode 3 doesn't use
	gfx_commit();
	CHECK_EQ(OBJ_PAL_T[15 * 16 + 2], 0x1234);
	for (u32 i = 0; i < 16; i++)
		OBJ_PAL_T[15 * 16 + i] = kept[i];

	// OAM: nothing is copied while a build is open
	OAM_T[0] = 0x1234;
	gfx_oam_begin();
	CHECK_EQ(gfx_oam_add(0x0010, 0x0020, 0x0030), 0);
	CHECK_EQ(gfx_oam_add(0x0040, 0x0050, 0x0060), 1);
	gfx_dirty_regs |= GFX_DIRTY_OAM;
	gfx_commit();
	CHECK_EQ(OAM_T[0], 0x1234);
	CHECK_EQ(gfx_dirty_regs, GFX_DIRTY_OAM);  // still waiting
	gfx_oam_end();
	gfx_affine_set(1, 0x200, 0, 0, 0x80);
	gfx_commit();
	CHECK_EQ(gfx_dirty_regs, 0);
	CHECK_EQ(OAM_T[0], 0x0010);
	CHECK_EQ(OAM_T[4 + 2], 0x0060);
	CHECK_EQ(OAM_T[8], GFX_OBJ_HIDDEN);
	CHECK_EQ(OAM_T[4 * 4 + 3], 0x200);  // matrix 1's pa
	CHECK_EQ(OAM_T[7 * 4 + 3], 0x80);  // and its pd

	// A rebuild with one sprite hides the other; the same build marks nothing
	gfx_oam_begin();
	gfx_oam_add(0x0010, 0x0020, 0x0030);
	gfx_oam_end();
	CHECK_EQ(gfx_dirty_regs, GFX_DIRTY_OAM);
	gfx_commit();
	CHECK_EQ(OAM_T[4], GFX_OBJ_HIDDEN);
	CHECK_EQ(OAM_T[4 * 4 + 3], 0x200);  // matrices stay
	gfx_oam_begin();
	gfx_oam_add(0x0010, 0x0020, 0x0030);
	gfx_oam_end();
	CHECK_EQ(gfx_dirty_regs, 0);

	// The cap: entry 129 is refused
	gfx_oam_begin();
	for (u32 i = 0; i < GFX_OAM_ENTRIES; i++)
		gfx_oam_add(GFX_OBJ_HIDDEN, 0, 0);
	CHECK_EQ(gfx_oam_add(GFX_OBJ_HIDDEN, 0, 0), GFX_OAM_NONE);
	gfx_oam_end();
	gfx_dirty_regs |= GFX_DIRTY_OAM;
	gfx_init();
	gfx_dirty_regs = GFX_DIRTY_OAM;
	gfx_dirty_pal = 0;
	gfx_commit();  // leaves every entry hidden
	CHECK_EQ(OAM_T[0], GFX_OBJ_HIDDEN);
	engine_quiet(0);
}

static u32 steps_a, steps_b, cancelled;

static int step_count(struct job *job)
{
	if (job->state == JOB_CANCEL) {
		cancelled++;
		return JOB_DONE;
	}
	u32 *n = job->ctx;
	++*n;
	return ++job->state >= 3 ? JOB_DONE : JOB_MORE;
}

// Takes about 2000 cycles a step, forever.
static int step_slow(struct job *job)
{
	if (job->state == JOB_CANCEL) {
		cancelled++;
		return JOB_DONE;
	}
	u32 t = cycles_now();
	while (cycles_since(t) < 2000)
		;
	++*(u32 *)job->ctx;
	return JOB_MORE;
}

void test_engine_jobs(void)
{
	engine_quiet(1);
	steps_a = steps_b = cancelled = 0;
	cycles_init();
	u8 a = job_start(step_count, &steps_a, 1);
	u8 b = job_start(step_count, &steps_b, 2);
	CHECK(a != JOB_NONE && b != JOB_NONE && a != b);
	CHECK_EQ(jobs_pending(), 2);

	// A spent budget still runs one step, round-robin
	CHECK_EQ(jobs_run_from(cycles_now() - 100000, 0), 1);
	CHECK_EQ(jobs_run_from(cycles_now() - 100000, 0), 1);
	CHECK_EQ(steps_a, 1);
	CHECK_EQ(steps_b, 1);

	// Plenty of budget: both finish, and their ids go stale
	jobs_run_from(cycles_now(), 100000);
	CHECK_EQ(steps_a, 3);
	CHECK_EQ(steps_b, 3);
	CHECK(!job_running(a));
	CHECK(!job_running(b));
	CHECK_EQ(jobs_pending(), 0);
	CHECK_EQ(jobs_run_from(cycles_now(), 100000), 0);

	// The budget bounds slow steps: 10000 cycles is about 5 of them
	u32 slow = 0;
	u8 s = job_start(step_slow, &slow, 3);
	u32 ran = jobs_run_from(cycles_now(), 10000);
	CHECK(ran >= 4 && ran <= 6);
	CHECK_EQ(slow, ran);

	// Cancelling calls the step once with JOB_CANCEL
	job_cancel(s);
	CHECK_EQ(cancelled, 1);
	CHECK(!job_running(s));
	job_cancel(s);  // stale: nothing happens
	CHECK_EQ(cancelled, 1);
	u32 n1 = 0, n2 = 0, n3 = 0;
	job_start(step_count, &n1, 5);
	job_start(step_count, &n2, 5);
	u8 keep = job_start(step_count, &n3, 6);
	jobs_cancel_owner(5);
	CHECK_EQ(cancelled, 3);
	CHECK_EQ(jobs_pending(), 1);
	CHECK(job_running(keep));

	// The cap
	for (u32 i = 1; i < JOB_CAP; i++)
		CHECK(job_start(step_count, &n1, 7) != JOB_NONE);
	CHECK_EQ(job_start(step_count, &n1, 7), JOB_NONE);
	engine_quiet(0);
}

void test_engine_cycles(void)
{
	cycles_init();
	u32 t0 = cycles_now();
	CHECK(cycles_since(t0) < 1000);
	// Two frames by VCOUNT: 2 x 280896 cycles, to within a few lines
	wait_vblank_start();
	u32 start = cycles_now();
	wait_vblank_start();
	wait_vblank_start();
	u32 two = cycles_since(start);
	CHECK(two > 2 * ENGINE_FRAME_CYCLES - 4000 && two < 2 * ENGINE_FRAME_CYCLES + 4000);
	// cycles_init leaves a running counter alone
	cycles_init();
	CHECK(cycles_since(start) >= two);
}
