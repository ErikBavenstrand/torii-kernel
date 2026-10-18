// The asset pipeline (source/engine): the BIOS's LZ77 on an asset, PSRAM
// writes and reads, the SD reader job, and the cover cache from ROM and SD
// into sprite VRAM.
//
// PSRAM and the card need a cart: these run in every kernel's Diagnostics
// (a real cart) and in tests/assets.sh (the emulated one); in the plain
// test kernel run (tests/kernel.sh, no cart model) the cart tests log that
// they are skipped. On hardware they never skip. Like test_engine.c, they
// leave Diagnostics' mode 3 screen alone: sprite VRAM below 0x06014000 is
// kept from the cover slots, and the engine is reset afterwards.
#include <string.h>

#include <gba_compression.h>

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

extern FATFS EZcardFs;

#define REG_VCOUNT_A (*(vu16 *)0x04000006)
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
	res_errors_quiet = (u8)quiet;
}

// 1 where PSRAM works: always on hardware (a failure there is a failure); in
// the emulator only with the cart model, where a write reads back.
static int have_psram(void)
{
	if (!tlog_in_emulator())
		return 1;
	static const u16 probe[2] ALIGN(4) = {0xA55A, 0x0FF0};
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

void test_asset_lz77_decode(void)
{
	cycles_init();
	const struct asset_header *h = (const void *)test_asset_small;
	u32 lz_at = ASSET_HEADER_BYTES + asset_palette_bytes(h);
	const u32 *lz = test_asset_small + lz_at / 4;
	CHECK_EQ(asset_check(h, test_asset_small_bytes, *lz), ASSET_OK);
	CHECK_EQ(asset_tile_bytes(h), 128);
	u8 *out = stage_buffer();
	memset(out, 0xEE, 256);
	u32 start = cycles_now();
	LZ77UnCompWram(lz, out);
	u32 cycles = cycles_since(start);
	CHECK_EQ(crc32(out, 128), test_asset_small_crc);
	CHECK_EQ(out[128], 0xEE);  // not a byte past the tiles
	tlog("  BIOS LZ77, 128 bytes of tiles: %lu cycles", (unsigned long)cycles);
	// The same with a wrong size in the LZ77 header: refused before the BIOS sees it
	CHECK_EQ(asset_check(h, test_asset_small_bytes, (*lz & 0xFF) | 0x100000 << 8), ASSET_ERR_LZ77);
}

void test_asset_psram_round_trip(void)
{
	reset_engine(1);
	if (skip_without_cart())
		return;
	u8 *stage = stage_buffer();
	u8 *slot = cover_psram_slot(0);
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
	static const u16 six[3] ALIGN(4) = {0x1111, 0x2222, 0x3333};
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
	id = sd_read_start(&reader, TEST_FILE, psram, COVER_STAGE_BYTES, 2048, JOB_OWNER_NONE);
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

// A copy of the small asset with its LZ77 size wrong, as a broken file would
// be, in the stage (clear of what the loader uses of it)
#define broken_asset ((u32 *)(stage_buffer() + 0x7400))

static void locate(u32 id, struct cover_src *src, char *path, u32 size)
{
	if (id == 1) {
		src->kind = COVER_SRC_ROM;
		src->data = test_asset_small;
		src->bytes = test_asset_small_bytes;
	} else if (id == 2) {
		src->kind = COVER_SRC_FILE;
		strncpy(path, "/torii/no such cover.tas", size);
	} else if (id == 4) {
		src->kind = COVER_SRC_ROM;
		src->data = broken_asset;
		src->bytes = test_asset_small_bytes;
	}
	// id 3: no cover
}

static u32 ready_calls EWRAM_BSS;

static void on_ready(u8 slot, u32 id, int ok)
{
	if (slot == 0 && id == 1 && ok)
		ready_calls++;
}

void test_asset_cover_cache(void)
{
	reset_engine(1);
	if (skip_without_cart())
		return;
	memcpy(broken_asset, test_asset_small, test_asset_small_bytes);
	broken_asset[(ASSET_HEADER_BYTES + 8) / 4] += 0x100;  // the LZ77 header's size
	// Mode 3's frame reaches into sprite VRAM: keep covers above 0x06014000
	u8 keep = res_alloc(RES_OBJ_VRAM, 512, 1, 0, RES_OBJ_UNITS, 0, 0);
	CHECK_EQ(res_start(RES_OBJ_VRAM, keep), 0);
	cover_init(locate, JOB_OWNER_NONE);
	cover_on_ready(on_ready);
	ready_calls = 0;

	cover_wants_begin();
	for (u32 id = 1; id <= 4; id++)
		cover_want(id, (u16)id);
	cover_wants_end();
	CHECK_EQ(jobs_pending(), 1);
	run_jobs(30);
	const struct cover_cache *c = cover_cache();
	CHECK_EQ(c->entry[cc_find(c, 1)].state, COVER_READY);
	CHECK_EQ(c->entry[cc_find(c, 2)].state, COVER_FAILED);
	CHECK_EQ(c->entry[cc_find(c, 3)].state, COVER_FAILED);
	CHECK_EQ(c->entry[cc_find(c, 4)].state, COVER_FAILED);
	CHECK_EQ(cover_stats.decoded, 1);
	u8 *slot = cover_psram_slot(cc_find(c, 1));
	CHECK_EQ(crc32(slot + COVER_SLOT_TILES, 128), test_asset_small_crc);
	CHECK_EQ(((const vu16 *)(slot + COVER_SLOT_PALETTE))[1], 0x4000);

	// Into a sprite slot: tiles, a palette bank, the upload in VBlank
	CHECK(cover_slot_show(0, 1));
	CHECK_EQ(cover_slot_state(0), COVER_SLOT_LOADING);
	cover_update();
	CHECK_EQ(cover_slot_state(0), COVER_SLOT_UPLOADING);
	CHECK_EQ(jobs_pending(), 0);  // nothing more wanted
	while (REG_VCOUNT_A >= 160)
		;
	while (REG_VCOUNT_A < 160)
		;
	gfx_commit_queue(GFX_COMMIT_BYTES, GFX_COMMIT_LAST_LINE);
	cover_update();
	CHECK_EQ(cover_slot_state(0), COVER_SLOT_READY);
	CHECK_EQ(ready_calls, 1);
	u16 attr2 = cover_slot_attr2(0);
	CHECK((attr2 & 0x3FF) >= 512);
	CHECK_EQ(crc32((const void *)(0x06010000 + (attr2 & 0x3FF) * 32), 128), test_asset_small_crc);
	CHECK_EQ(gfx_pal_unfaded[(16 + (attr2 >> 12)) * 16 + 1], 0x4000);
	CHECK_EQ(cover_slot_attr0(0), 0);  // square, 4bpp
	CHECK_EQ(cover_slot_attr1(0), 0x4000);  // 16x16
	CHECK_EQ(cover_slot_width(0), 16);

	// A slot showing a cover that failed
	cover_slot_show(1, 3);
	cover_update();
	CHECK_EQ(cover_slot_state(1), COVER_SLOT_FAILED);

	// Clearing frees the tiles; invalidating forgets the cache
	u16 free_before = pool_free_units(res_pool(RES_OBJ_VRAM), 0);
	cover_slot_clear(0);
	CHECK_EQ(pool_free_units(res_pool(RES_OBJ_VRAM), 0), free_before + 4);
	cover_cache_invalidate();
	CHECK_EQ(cc_count(c, COVER_FREE), COVER_ENTRIES);
	CHECK_EQ(cover_slot_state(1), COVER_SLOT_EMPTY);
	reset_engine(0);
}
