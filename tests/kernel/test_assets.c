// The asset pipeline (source/engine): an asset's header, PSRAM writes and
// reads, the SD reader job, and the cover cache from SD into PSRAM. Then the
// menu's art (source/ui/art.h): its table, and that the screens' scenes fit
// the sprite VRAM the spec gives them.
//
// PSRAM and the card need a cart: these run in every kernel's Diagnostics
// (a real cart) and in tests/assets.sh (the emulated one); in the plain
// test kernel run (tests/kernel.sh, no cart model) the cart tests log that
// they are skipped. On hardware they never skip. Like test_engine.c, they
// keep to sprite VRAM from 0x06014000 (below it is kept from the cover
// slots), and the engine is reset afterwards.
#include <string.h>

#include "torii_test.h"
#include "tests.h"
#include "ff.h"
#include "../../source/crc32.h"
#include "../../source/engine/asset.h"
#include "../../source/engine/cover.h"
#include "../../source/engine/engine.h"
#include "../../source/engine/psram.h"
#include "../../source/engine/sdread.h"
#include "../../source/engine/stage.h"
#include "../../source/ui/art.h"
#include "../../source/ui/ui.h"

extern FATFS EZcardFs;

#define TEST_FILE "/torii/asset.tmp"
#define TEST_FILE_BYTES 10000

static void reset_engine(int quiet)
{
	cycles_init();  // the tests run before frame_init starts it
	gfx_init();
	gfx_dirty_regs = 0;
	gfx_dirty_pal = 0;
	res_init();
	jobs_init();
	// The jobs are new: the cover loader's is gone
	cover_reset();
	res_errors_quiet = (u8)quiet;
}

// 1 where PSRAM works: always on hardware (a failure there is a failure); in
// the emulator only with the cart model, where a write reads back.
static int have_psram(void)
{
	if (!tlog_in_emulator())
		return 1;
	const u16 probe[2] ALIGN(4) = {0xA55A, 0x0FF0};  // in RAM, as psram_write takes
	vu16 *p = (vu16 *)PSRAM_ASSET_BASE;
	psram_begin();
	psram_write((void *)p, probe, sizeof(probe));
	return p[0] == probe[0] && p[1] == probe[1];
}

// 1 if the card is mounted (mounting it if the kernel hasn't yet).
static int have_card(void)
{
	if (!EZcardFs.fs_type && f_mount(&EZcardFs, "", 1) != FR_OK)
		return 0;
	return EZcardFs.fs_type != 0;
}

static int skip_without_cart(void)
{
	if (have_psram() && have_card())
		return 0;
	CHECK(tlog_in_emulator());  // never skipped on a cart
	tlog("  (no emulated cart: skipped)");
	return 1;
}

static u8 pattern(u32 i)
{
	return (u8)(i * 7 + (i >> 8) * 13 + 1);
}

// Runs one job step at a time until none waits; returns the steps.
static u32 run_jobs(u32 cap)
{
	u32 steps = 0;
	while (jobs_pending() && steps < cap)
		steps += jobs_run_from(cycles_now(), 0);
	return steps;
}

void test_asset_raw(void)
{
	const struct asset_header *h = (const void *)test_asset_small;
	CHECK_EQ(asset_check(h, test_asset_small_bytes), ASSET_OK);
	CHECK_EQ(asset_tile_bytes(h), 128);
	CHECK_EQ(asset_tiles_at(h) + 128, test_asset_small_bytes);
	CHECK_EQ(crc32((const u8 *)test_asset_small + asset_tiles_at(h), 128), test_asset_small_crc);
	CHECK_EQ(asset_check(h, test_asset_small_bytes - 1), ASSET_ERR_SHORT);
}

void test_asset_psram_round_trip(void)
{
	reset_engine(1);
	if (skip_without_cart())
		return;
	u8 *stage = stage_buffer();
	u8 *slot = (u8 *)PSRAM_ASSET_BASE;
	for (u32 i = 0; i < 4096; i++)
		stage[i] = pattern(i);
	psram_begin();
	u32 start = cycles_now();
	CHECK(psram_write(slot, stage, 4096));
	u32 write = cycles_since(start);
	u32 bad = 0;
	start = cycles_now();
	for (u32 i = 0; i < 4096 / 2; i++)
		bad += ((const vu16 *)slot)[i] != ((const u16 *)stage)[i];
	u32 read = cycles_since(start);
	CHECK_EQ(bad, 0);
	tlog("  PSRAM 4 KB: write %lu cycles (DMA, slow WAITCNT), read %lu (CPU)", (unsigned long)write,
	     (unsigned long)read);

	// 6 bytes in the middle: the halfwords around them keep their values
	// (from RAM: the cart drops a DMA from its own ROM into PSRAM, so the
	// ROM copy below is refused)
	static const u16 six_rom[3] ALIGN(4) = {0x1111, 0x2222, 0x3333};
	u16 six[3] ALIGN(4);
	memcpy(six, six_rom, sizeof(six));
	CHECK(psram_write(slot + 100, six, 6));
	CHECK_EQ(((const vu16 *)slot)[49], ((const u16 *)stage)[49]);
	CHECK_EQ(((const vu16 *)slot)[50], 0x1111);
	CHECK_EQ(((const vu16 *)slot)[52], 0x3333);
	CHECK_EQ(((const vu16 *)slot)[53], ((const u16 *)stage)[53]);

	// Bad writes go nowhere: outside the window, odd, or from PSRAM itself
	CHECK(!psram_write((void *)0x08700000, six, 6));
	CHECK(!psram_write((void *)(PSRAM_WINDOW + PSRAM_WINDOW_BYTES - 2), six, 6));
	CHECK(!psram_write(slot + 1, six, 6));
	CHECK(!psram_write(slot, slot + 64, 6));
	CHECK(!psram_write(slot, six_rom, 6));
	CHECK_EQ(((const vu16 *)slot)[0], ((const u16 *)stage)[0]);
	reset_engine(0);
}

// Writes the test file; 1 if it is there.
static int write_test_file(void)
{
	f_mkdir("/torii");
	FIL f;
	UINT done = 0;
	u8 *buf = stage_buffer();
	for (u32 i = 0; i < TEST_FILE_BYTES; i++)
		buf[i] = pattern(i);
	int ok = f_open(&f, TEST_FILE, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK;
	if (ok) {
		ok = f_write(&f, buf, TEST_FILE_BYTES, &done) == FR_OK && done == TEST_FILE_BYTES;
		ok = f_close(&f) == FR_OK && ok;
	}
	return ok;
}

// The reader lives in the stage, past what the tests read into, so the
// tests take no EWRAM of their own (every kernel links them)
#define reader (*(struct sd_read *)(stage_buffer() + 0x7000))
static u32 finished EWRAM_BSS;

static void count_finish(struct sd_read *r)
{
	(void)r;
	finished++;
}

void test_asset_sd_read(void)
{
	reset_engine(1);
	if (skip_without_cart())
		return;
	CHECK(write_test_file());

	// Into RAM: an open step, then 4 KB a step
	u8 *ram = stage_buffer() + 0x4000;
	memset(ram, 0, 0x3000);
	finished = 0;
	reader.finish = count_finish;
	u8 id = sd_read_start(&reader, TEST_FILE, ram, 0x3000, 4096, JOB_OWNER_NONE);
	CHECK(id != JOB_NONE);
	CHECK_EQ(run_jobs(20), 4);
	CHECK_EQ(reader.result, SD_READ_OK);
	CHECK_EQ(reader.size, TEST_FILE_BYTES);
	CHECK_EQ(reader.open, 0);
	CHECK_EQ(finished, 1);
	u32 bad = 0;
	for (u32 i = 0; i < TEST_FILE_BYTES; i++)
		bad += ram[i] != pattern(i);
	CHECK_EQ(bad, 0);
	CHECK_EQ(ram[TEST_FILE_BYTES], 0);  // not a byte more

	// Into PSRAM, through the stage, 2 KB a step
	u8 *psram = (u8 *)PSRAM_ASSET_BASE;
	reader.finish = 0;
	id = sd_read_start(&reader, TEST_FILE, psram, 0x4000, 2048, JOB_OWNER_NONE);
	CHECK_EQ(run_jobs(20), 6);
	CHECK_EQ(reader.result, SD_READ_OK);
	bad = 0;
	for (u32 i = 0; i < TEST_FILE_BYTES; i += 2)
		bad += ((const vu16 *)psram)[i / 2] != (pattern(i) | pattern(i + 1) << 8);
	CHECK_EQ(bad, 0);

	// Cancelled after its first read: the file is closed
	finished = 0;
	reader.finish = count_finish;
	id = sd_read_start(&reader, TEST_FILE, ram, 0x3000, 4096, JOB_OWNER_NONE);
	run_jobs(2);
	CHECK_EQ(reader.open, 1);
	CHECK_EQ(reader.done, 4096);
	job_cancel(id);
	CHECK_EQ(reader.open, 0);
	CHECK_EQ(reader.result, SD_READ_CANCELLED);
	CHECK_EQ(finished, 1);
	CHECK_EQ(jobs_pending(), 0);

	// Errors: no file, too big for its room, bad arguments
	reader.finish = 0;
	sd_read_start(&reader, "/torii/no such file.tas", ram, 0x3000, 4096, JOB_OWNER_NONE);
	run_jobs(5);
	CHECK_EQ(reader.result, SD_READ_ERR_OPEN);
	sd_read_start(&reader, TEST_FILE, ram, 4096, 4096, JOB_OWNER_NONE);
	run_jobs(5);
	CHECK_EQ(reader.result, SD_READ_ERR_SIZE);
	CHECK_EQ(reader.open, 0);
	CHECK_EQ(sd_read_start(&reader, TEST_FILE, ram, 0x3000, 100, JOB_OWNER_NONE), JOB_NONE);
	CHECK_EQ(reader.result, SD_READ_ERR_ARGS);
	CHECK_EQ(sd_read_start(&reader, TEST_FILE, ram + 2, 0x3000, 4096, JOB_OWNER_NONE), JOB_NONE);

	f_unlink(TEST_FILE);
	reset_engine(0);
}

// Test covers on the card: a good one (its tiles a pattern, its palette 16
// colours), the small asset (no cover's size), and none
#define COVER_GOOD "/torii/cover-good.bin"
#define COVER_SMALL "/torii/cover-small.bin"

static u32 cover_tile_word(u32 i)
{
	return i * 0x01234567u ^ 0x89ABCDEFu;
}

static u16 cover_colour(u32 i)
{
	return (u16)(i * 0x0421u);  // greys
}

static int write_file(const char *path, const void *data, u32 bytes)
{
	FIL f;
	UINT done = 0;
	int ok = f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK;
	if (ok) {
		ok = f_write(&f, data, bytes, &done) == FR_OK && done == bytes;
		ok = f_close(&f) == FR_OK && ok;
	}
	return ok;
}

static int write_covers(void)
{
	f_mkdir("/torii");
	u8 *buf = stage_buffer();
	struct asset_header h = {ASSET_MAGIC, 4, COVER_TILES_W, COVER_TILES_H, 0, COVER_COLOURS, 0, COVER_TILE_BYTES};
	memcpy(buf, &h, sizeof(h));
	for (u32 i = 0; i < COVER_COLOURS; i++)
		((u16 *)(buf + ASSET_HEADER_BYTES))[i] = cover_colour(i);
	u32 *tiles = (u32 *)(buf + ASSET_HEADER_BYTES + COVER_COLOURS * 2);
	for (u32 i = 0; i < COVER_TILE_BYTES / 4; i++)
		tiles[i] = cover_tile_word(i);
	return write_file(COVER_GOOD, buf, COVER_FILE_BYTES) &&
	       write_file(COVER_SMALL, test_asset_small, test_asset_small_bytes);
}

static u32 cover_paused EWRAM_BSS;

static int locate(u32 id, char *path, u32 size)
{
	if (id == 1 || id >= 10)
		strncpy(path, COVER_GOOD, size);
	else if (id == 2)
		strncpy(path, "/torii/no such cover.bin", size);
	else if (id == 4)
		strncpy(path, COVER_SMALL, size);
	else
		return 0;  // id 3: no cover
	return 1;
}

static int pause_covers(void)
{
	return cover_paused != 0;
}

void test_asset_cover_cache(void)
{
	reset_engine(1);
	if (skip_without_cart())
		return;
	CHECK(write_covers());
	cover_init(locate, pause_covers);
	cover_paused = 0;
	CHECK(cover_active());

	cover_wants_begin();
	for (u32 id = 1; id <= 4; id++)
		cover_want(id, (u16)id);
	cover_wants_end();
	CHECK_EQ(jobs_pending(), 1);
	run_jobs(30);
	CHECK_EQ(cover_state(1), COVER_READY);
	CHECK_EQ(cover_state(2), COVER_FAILED);
	CHECK_EQ(cover_state(3), COVER_FAILED);
	CHECK_EQ(cover_state(4), COVER_FAILED);
	CHECK_EQ(cover_state(5), COVER_FREE);
	CHECK_EQ(cover_stats.loaded, 1);
	CHECK_EQ(cover_stats.failed, 3);
	CHECK(!cover_loading());
	tlog("  slowest cover step: %lu cycles", (unsigned long)cover_stats.step_max);

	// Its tiles and palette as the file has them, word-aligned in PSRAM
	u8 e = cover_pin(1);
	CHECK(e != COVER_NONE);
	CHECK_EQ(cover_pin(2), COVER_NONE);  // failed: nothing to pin
	CHECK_EQ(cover_colours(e), COVER_COLOURS);
	CHECK_EQ(cover_palette(e)[5], cover_colour(5));
	const u32 *tiles = cover_tiles(e);
	CHECK(!((u32)tiles & 3));
	u32 bad = 0;
	for (u32 i = 0; i < COVER_TILE_BYTES / 4; i++)
		bad += tiles[i] != cover_tile_word(i);
	CHECK_EQ(bad, 0);

	// Pinned, it outlives a batch that wants every entry for others
	cover_wants_begin();
	for (u32 i = 0; i < COVER_ENTRIES; i++)
		cover_want(10 + i, 0);
	cover_wants_end();
	run_jobs(400);
	CHECK_EQ(cover_state(1), COVER_READY);
	CHECK_EQ(cover_stats.loaded, COVER_ENTRIES);  // all but one of the new ones
	cover_unpin(e);

	// Paused, the loader starts nothing; resumed, it goes on
	cover_paused = 1;
	cover_wants_begin();
	cover_want(200, 0);
	cover_wants_end();
	CHECK_EQ(jobs_pending(), 0);
	CHECK_EQ(cover_state(200), COVER_WANTED);
	cover_paused = 0;
	cover_wants_end();
	run_jobs(10);
	CHECK_EQ(cover_state(200), COVER_READY);

	// Invalidating (a launch) forgets the cache and stops the loader
	cover_cache_invalidate();
	CHECK(!cover_active());
	CHECK_EQ(cover_state(1), COVER_FREE);
	CHECK_EQ(cc_count(cover_cache(), COVER_FREE), COVER_ENTRIES);
	f_unlink(COVER_GOOD);
	f_unlink(COVER_SMALL);
	reset_engine(0);
}

// --- The menu's art ---

// Sprite VRAM the spec gives the menu's sprites: "OBJ ~16 KB of 32 KB"
#define OBJ_BUDGET_TILES 512
// The carousel's objects in VRAM: five in view and the one coming in
// during a step (research-v04-motion.md)
#define CAROUSEL_SLOTS 6

// Pixel (x, y) of 4bpp tiles in 1D order, w tiles across
static u32 pixel(const u32 *tiles, u32 w, u32 x, u32 y)
{
	return tiles[((y / 8) * w + x / 8) * 8 + y % 8] >> (x % 8 * 4) & 15;
}

void test_art_table(void)
{
	for (u32 i = 0; i < ART_ITEM_COUNT; i++) {
		CHECK(art_items[i].icon16 < ART_ICON16_COUNT);
		CHECK(art_items[i].icon8 < ART_ICON8_COUNT);
		CHECK(art_items[i].sprite < ART_SPRITE_COUNT);
	}
	for (u32 i = 0; i < ART_ICON16_COUNT; i++) {
		CHECK(art_icon16_bank[i] < ART_ICON16_BANKS);
		CHECK(art_icon16_palette(i) == art_icon16_palettes[art_icon16_bank[i]]);
	}
	// At most two icons a bank, and no bank empty
	for (u32 b = 0; b < ART_ICON16_BANKS; b++) {
		u32 n = 0;
		for (u32 i = 0; i < ART_ICON16_COUNT; i++)
			n += art_icon16_bank[i] == b;
		CHECK(n == 1 || n == 2);
	}

	for (u32 s = 0; s < ART_SPRITE_COUNT; s++) {
		const struct art_sprite *a = &art_sprites[s];
		const u32 *t = art_sprite_tiles[s];
		CHECK(a->width && a->width <= ART_SPRITE_SIZE && a->height && a->height <= ART_SPRITE_FOOT + 1);
		CHECK_EQ(art_sprite_palettes[s][ART_SHADOW_INDEX], 0);  // the shadow's, set by the screen
		u32 max = 0, left = ART_SPRITE_SIZE, top = ART_SPRITE_SIZE, bottom = 0;
		for (u32 y = 0; y < ART_SPRITE_SIZE; y++)
			for (u32 x = 0; x < ART_SPRITE_SIZE; x++) {
				u32 p = pixel(t, ART_SPRITE_SIZE / 8, x, y);
				if (p > max)
					max = p;
				if (p && x < left)
					left = x;
				if (p && y < top)
					top = y;
				if (p)
					bottom = y;
			}
		CHECK(max <= ART_SPRITE_COLOURS);
		CHECK_EQ(bottom, ART_SPRITE_FOOT);
		CHECK_EQ(top, ART_SPRITE_FOOT + 1 - a->height);
		CHECK_EQ(left, (ART_SPRITE_SIZE - a->width) / 2u);
		CHECK_EQ(a->shadow, art_shadow_for(a->width));
	}

	for (u32 i = 0; i < ART_SHADOW_COUNT; i++) {
		CHECK(art_shadows[i].width % 2 == 0 && art_shadows[i].width <= 64);
		CHECK(art_shadows[i].height && art_shadows[i].height <= 8);
		if (i)
			CHECK(art_shadows[i].width > art_shadows[i - 1].width);
		CHECK_EQ(art_shadow_for(art_shadows[i].width), i);
		u32 drawn = 0;
		for (u32 y = 0; y < 8; y++)
			for (u32 x = 0; x < 32; x++) {
				u32 p = pixel(art_shadow_tiles[i], 4, x, y);
				CHECK(p == 0 || p == ART_SHADOW_INDEX);
				drawn += p != 0;
			}
		CHECK(drawn > 0);
	}
	CHECK_EQ(art_shadow_for(0), 0);
	CHECK_EQ(art_shadow_for(255), ART_SHADOW_COUNT - 1);

	// The 8x8 icons in a colour of the screen's choosing
	u32 tile[8];
	for (u32 i = 0; i < ART_ICON8_COUNT; i++) {
		art_icon8_tile(i, 0x17, tile);  // only the low four bits count
		u32 bits = 0, set = 0;
		for (u32 y = 0; y < 8; y++) {
			for (u32 x = 0; x < 8; x++) {
				u32 p = tile[y] >> (x * 4) & 15;
				CHECK(p == 0 || p == 7);
				set += p == 7;
			}
			for (u32 v = art_icon8_bits[i][y]; v; v >>= 1)
				bits += v & 1;
		}
		CHECK(set > 0);
		CHECK_EQ(set, bits);
	}

	// Each placeholder box fills its square but the shadow's corners, its
	// colours numbered from 1 with none left out, as the converter writes them
	for (u32 b = 0; b < ART_BOX_COUNT; b++) {
		u32 clear = 0, used = 0, top = 0;
		for (u32 y = 0; y < ART_BOX_SIZE; y++)
			for (u32 x = 0; x < ART_BOX_SIZE; x++) {
				u32 p = pixel(art_box_tiles[b], ART_BOX_SIZE / 8, x, y);
				if (!p)
					clear++;
				used |= 1u << p;
				if (p > top)
					top = p;
			}
		CHECK(clear < ART_BOX_SIZE);
		CHECK_EQ(used | 1u, (2u << top) - 1);
	}
	const struct ui_theme *was = ui_theme();
	ui_theme_select(&ui_theme_light);
	CHECK(art_box_colours(ART_BOX_GBA) == art_box_palettes[ART_BOX_GBA][0]);
	CHECK(art_box_colours(ART_BOX_COUNT) == art_box_palettes[ART_BOX_OTHER][0]);
	ui_theme_select(&ui_theme_dark);
	CHECK(art_box_colours(ART_BOX_NES) == art_box_palettes[ART_BOX_NES][1]);
	ui_theme_select(was);
}

void test_art_obj_budget(void)
{
	reset_engine(1);
	// Keep to sprite VRAM from 0x06014000, as the other engine tests do
	u8 keep = res_alloc(RES_OBJ_VRAM, 512, 1, 0, RES_OBJ_UNITS, 0, 0);
	CHECK_EQ(res_start(RES_OBJ_VRAM, keep), 0);
	struct pool *obj = res_pool(RES_OBJ_VRAM);
	u16 free0 = pool_free_units(obj, 0);

	// The carousel: its objects, every shadow width, the item's label icon
	// and the torii glyph, each upload within one VBlank's commit
	u16 tiles[CAROUSEL_SLOTS];
	for (u32 i = 0; i < CAROUSEL_SLOTS; i++) {
		tiles[i] = art_load_sprite(art_items[i % ART_ITEM_COUNT].sprite);
		CHECK(tiles[i] != ART_NONE && tiles[i] >= 512);
		for (u32 j = 0; j < i; j++)
			CHECK(tiles[i] != tiles[j]);
		gfx_queue_clear();  // the copies would never land: the test commits nothing
	}
	for (u32 i = 0; i < ART_SHADOW_COUNT; i++) {
		CHECK(art_load_shadow(i) != ART_NONE);
		gfx_queue_clear();
	}
	u8 label_bank = 0xFF, torii_bank = 0xFF;
	u16 label = art_load_icon16(art_items[0].icon16, &label_bank);
	u16 torii = art_load_icon16(ART_ICON16_TORII, &torii_bank);
	CHECK(label != ART_NONE && torii != ART_NONE && label_bank < 16 && torii_bank < 16);
	gfx_queue_clear();
	u16 used = free0 - pool_free_units(obj, 0);
	CHECK_EQ(used,
		 CAROUSEL_SLOTS * ART_SPRITE_TILES + ART_SHADOW_COUNT * ART_SHADOW_HALF_TILES + 2 * ART_ICON16_TILES);
	struct engine_budget room = {.obj_tiles = OBJ_BUDGET_TILES, .obj_banks = RES_PAL_BANKS};
	// A bank per object, so each fogs alone, and the icons' banks
	struct engine_budget need = {.obj_tiles = used, .obj_banks = CAROUSEL_SLOTS + 2};
	char why[80];
	CHECK_EQ(budget_check(&need, &room, why, sizeof(why)), BUDGET_OK);
	CHECK(ART_SPRITE_TILES * 32 <= GFX_COMMIT_BYTES);

	// Loading again shares the tiles and the bank
	u8 again_bank = 0xFF;
	CHECK_EQ(art_load_icon16(ART_ICON16_TORII, &again_bank), torii);
	CHECK_EQ(again_bank, torii_bank);
	CHECK_EQ(art_load_sprite(art_items[0].sprite), tiles[0]);
	CHECK_EQ(free0 - pool_free_units(obj, 0), used);
	CHECK_EQ(art_load_sprite(ART_SPRITE_COUNT), ART_NONE);
	CHECK_EQ(art_load_shadow(ART_SHADOW_COUNT), ART_NONE);
	CHECK_EQ(art_load_icon16(ART_ICON16_COUNT, &again_bank), ART_NONE);
	gfx_queue_clear();

	// With the VRAM queue full a load fails and leaves nothing behind; the
	// next one, with room, uploads (not revives an empty block)
	// (fills of the kept VRAM, which nothing commits)
	for (u32 i = 0; i < GFX_QUEUE_CAP && gfx_queue_len() < GFX_QUEUE_CAP; i++)
		CHECK(gfx_queue_fill(res_obj_addr(keep), 0, 4));
	CHECK_EQ(gfx_queue_len(), GFX_QUEUE_CAP);
	u16 before = pool_free_units(obj, 0);
	u32 last = ART_SPRITE_COUNT - 1;
	CHECK_EQ(art_load_sprite(last), ART_NONE);
	CHECK_EQ(pool_free_units(obj, 0), before);
	CHECK_EQ(pool_find(obj, (u16)(ART_TAG_SPRITE | last)), POOL_NONE);
	gfx_queue_clear();
	CHECK(art_load_sprite(last) != ART_NONE);
	CHECK_EQ(gfx_queue_len(), 1);
	gfx_queue_clear();

	// A list with art: the title icon (the placeholder box is BG tiles)
	need.obj_tiles = ART_ICON16_TILES;
	need.obj_banks = 1;
	CHECK_EQ(budget_check(&need, &room, why, sizeof(why)), BUDGET_OK);
	reset_engine(0);
}

// The carousel's ring turned 200 times: each turn drops the object (and
// shadow) leaving the slots and takes the one coming in, unlogged, so the
// engine's allocation log stays as it was however long the ring turns.
void test_art_ring(void)
{
	reset_engine(1);
	u8 keep = res_alloc(RES_OBJ_VRAM, 512, 1, 0, RES_OBJ_UNITS, 0, 0);
	CHECK_EQ(res_start(RES_OBJ_VRAM, keep), 0);
	struct pool *obj = res_pool(RES_OBJ_VRAM);
	u16 free0 = pool_free_units(obj, 1);
	u8 mark = res_mark();
	u8 sprite[CAROUSEL_SLOTS], shadow[CAROUSEL_SLOTS];
	for (u32 i = 0; i < CAROUSEL_SLOTS; i++) {
		const struct art_sprite *a = &art_sprites[art_items[i % ART_ITEM_COUNT].sprite];
		CHECK(art_take_sprite(art_items[i % ART_ITEM_COUNT].sprite, &sprite[i], 1) != ART_NONE);
		CHECK(art_take_shadow(a->shadow, &shadow[i], 1) != ART_NONE);
		gfx_queue_clear();
	}
	for (u32 turn = 0; turn < 200; turn++) {
		u32 slot = turn % CAROUSEL_SLOTS;
		u32 item = (turn + CAROUSEL_SLOTS) % ART_ITEM_COUNT;
		art_drop(sprite[slot]);
		art_drop(shadow[slot]);
		u32 s = art_items[item].sprite;
		CHECK(art_take_sprite(s, &sprite[slot], 1) != ART_NONE);
		CHECK(art_take_shadow(art_sprites[s].shadow, &shadow[slot], 1) != ART_NONE);
		gfx_queue_clear();
		CHECK_EQ(res_mark(), mark);
		// At most every slot's object and shadow held (slots share shadows;
		// dropped blocks are cached, which counts as free)
		CHECK(free0 - pool_free_units(obj, 1) <= CAROUSEL_SLOTS * (ART_SPRITE_TILES + ART_SHADOW_HALF_TILES));
	}
	for (u32 i = 0; i < CAROUSEL_SLOTS; i++) {
		art_drop(sprite[i]);
		art_drop(shadow[i]);
	}
	CHECK_EQ(pool_free_units(obj, 1), free0);
	CHECK_EQ(res_mark(), mark);

	// An object's colours with the ground tone in the shadow's slot
	u16 c[16];
	art_sprite_colours(ART_SPRITE_GBA, 0x1234, c);
	CHECK_EQ(c[ART_SHADOW_INDEX], 0x1234);
	CHECK_EQ(c[1], art_sprite_palettes[ART_SPRITE_GBA][1]);
	reset_engine(0);
}
