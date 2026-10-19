// The cover demo: the test kernel's carousel of covers streamed through the
// whole asset route, shown after the asset tests when SELECT is held at
// power-on (not reachable from the menu):
//
//	OMEGA_DIR=CART mgba-shot ezkerneltest.gba -k 1:SELECT -k 30:- ...
//
// It writes the demo covers (tests/kernel/asset_data.c) to the card as
// /torii/demo/coverN.tas, then shows five of them in a row: read from SD a
// chunk a step, decoded with the BIOS's LZ77 into PSRAM, uploaded into
// sprite slots by the VBlank queue. Cover 4 comes from ROM instead. The
// middle cover is zoomed with an affine matrix; the others are smaller.
//
// Once the row is loaded it logs "cover-demo: loaded", slides one cover to
// the left over 32 frames (cover 5, prefetched, comes in from the right,
// cover 0 leaves) and logs "screen ready: cover-demo" when it stops. It
// logs frame-cycles figures for tests/perf: the slowest cover decode step
// (cover-decode: LZ77 and the PSRAM write), one cover's PSRAM to VRAM copy
// (cover-upload) and a frame of the slide (cover-demo).
#ifdef TORII_TEST_KERNEL
#include <stdio.h>
#include <string.h>

#include <gba_interrupt.h>

#include "torii_test.h"
#include "tests.h"
#include "ff.h"
#include "../../source/cartlog.h"
#include "../../source/engine/affine.h"
#include "../../source/engine/cover.h"
#include "../../source/engine/engine.h"
#include "../../source/engine/stage.h"

extern FATFS EZcardFs;

#define ROM_COVER 4  // the cover served from ROM
#define SPACING 60  // pixels between covers' centres
#define SLIDE_FRAMES 32
#define JOB_BUDGET 160000  // cycles into the frame the jobs may run to
#define DMA3SAD_D (*(vu32 *)0x040000D4)
#define DMA3DAD_D (*(vu32 *)0x040000D8)
#define DMA3CNT_D (*(vu32 *)0x040000DC)

static void locate(u32 id, struct cover_src *src, char *path, u32 size)
{
	if (id >= DEMO_COVERS)
		return;
	if (id == ROM_COVER) {
		src->kind = COVER_SRC_ROM;
		src->data = demo_covers[id].data;
		src->bytes = demo_covers[id].bytes;
		return;
	}
	src->kind = COVER_SRC_FILE;
	snprintf(path, size, "/torii/demo/cover%lu.tas", (unsigned long)id);
}

static void write_covers(void)
{
	TLOG_ASSERT(EZcardFs.fs_type || f_mount(&EZcardFs, "", 1) == FR_OK);
	f_mkdir("/torii");
	f_mkdir("/torii/demo");
	for (u32 i = 0; i < DEMO_COVERS; i++) {
		char path[32];
		snprintf(path, sizeof(path), "/torii/demo/cover%lu.tas", (unsigned long)i);
		// f_write from RAM: an SD write can't read ROM
		memcpy(stage_buffer(), demo_covers[i].data, demo_covers[i].bytes);
		FIL f;
		UINT done = 0;
		TLOG_ASSERT(f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
		TLOG_ASSERT(f_write(&f, stage_buffer(), demo_covers[i].bytes, &done) == FR_OK);
		TLOG_ASSERT(done == demo_covers[i].bytes);
		f_close(&f);
	}
}

static u8 mats[COVER_SLOTS];

// Draws the covers near pos (the cursor, 8.8 fixed point in covers), the
// nearest the middle first so it lies on top. The middle one is zoomed.
static void draw(s32 pos)
{
	u8 order[DEMO_COVERS];
	s32 far[DEMO_COVERS];
	u32 n = 0;
	for (u32 id = 0; id < DEMO_COVERS; id++) {
		s32 off = (s32)id * 256 - pos;
		s32 f = off < 0 ? -off : off;
		if (cover_slot_state((u8)id) != COVER_SLOT_READY || f >= 3 * 256)
			continue;
		u32 at = n++;
		while (at && far[at - 1] > f) {
			order[at] = order[at - 1];
			far[at] = far[at - 1];
			at--;
		}
		order[at] = (u8)id;
		far[at] = f;
	}
	gfx_oam_begin();
	for (u32 i = 0; i < n; i++) {
		u8 slot = order[i];
		s32 off = (s32)slot * 256 - pos;
		u32 zoom = 0xC0 + (u32)(far[i] < 256 ? (0xA0 * (256 - far[i])) >> 8 : 0);
		u8 m = (u8)res_start(RES_AFFINE, mats[slot]);
		affine_set(m, zoom, zoom, 0);
		// double size: a 64x64 cover in a 128x128 box
		s32 x = 120 + (off * SPACING >> 8) - 64, y = 80 - 64;
		gfx_oam_add((u16)((y & 0xFF) | 0x0300 | cover_slot_attr0(slot)),
			    (u16)((x & 0x1FF) | m << 9 | cover_slot_attr1(slot)), cover_slot_attr2(slot));
	}
	gfx_oam_end();
}

// Wants the row around cursor and its neighbours, and shows the row.
static void want_row(s32 cursor)
{
	cover_wants_begin();
	for (s32 d = 3; d <= 4; d++) {
		if (cursor + d < DEMO_COVERS)
			cover_want((u32)(cursor + d), (u16)d);
		if (cursor - d >= 0)
			cover_want((u32)(cursor - d), (u16)d);
	}
	cover_wants_end();
	for (s32 id = 0; id < DEMO_COVERS; id++) {
		s32 d = id - cursor;
		if (d >= -2 && d <= 2)
			cover_slot_show((u8)id, (u32)id);
		else
			cover_slot_clear((u8)id);
	}
}

static int row_ready(s32 cursor)
{
	for (s32 id = cursor - 2; id <= cursor + 2; id++)
		if (id >= 0 && id < DEMO_COVERS && cover_slot_state((u8)id) != COVER_SLOT_READY)
			return 0;
	return 1;
}

// One cover's PSRAM to VRAM copy, as the VBlank queue makes it (32-bit
// DMA), timed: the same tiles again, so nothing on screen changes.
static u32 time_upload(u8 slot)
{
	const struct cover_cache *c = cover_cache();
	u8 entry = cc_find(c, cover_slot_id(slot));
	u32 bytes = cover_slot_width(slot) * cover_slot_height(slot) / 2;
	u32 start = cycles_now();
	DMA3SAD_D = (u32)cover_psram_slot(entry);
	DMA3DAD_D = 0x06010000 + (cover_slot_attr2(slot) & 0x3FF) * 32u;
	DMA3CNT_D = 0x84000000u | bytes / 4;
	return cycles_since(start);
}

void cover_demo(void)
{
	irqInit();
	cartlog_install();
	write_covers();
	engine_init();
	cover_init(locate, JOB_OWNER_NONE);
	for (u32 i = 0; i < COVER_SLOTS; i++) {
		mats[i] = res_affine((u16)(0x7A00 + i));
		TLOG_ASSERT(mats[i] != RES_NONE);
	}
	static const u16 backdrop = 0x2884;  // a dark blue-grey
	gfx_pal_load(0, &backdrop, 1);
	gfx_set_dispcnt(0x1000 | 0x0040);  // mode 0, sprites, 1D tiles

	s32 cursor = 2, pos = 2 * 256;
	u32 slide = 0, loaded_at = 0, stopped = 0;
	for (u32 frame = 0;; frame++) {
		frame_begin();
		want_row(cursor);
		cover_update();
		if (!loaded_at && row_ready(cursor)) {
			loaded_at = frame;
			tlog("cover-demo: loaded");
			cycles_report("cover-decode", cover_stats.decode_max);
			cycles_report("cover-upload", time_upload((u8)cursor));
		}
		// The slide starts once the loaded row has been on screen a frame
		if (loaded_at && frame > loaded_at + 1 && slide < SLIDE_FRAMES) {
			slide++;
			// smoothstep from one cover to the next
			s32 t = (s32)(slide * 256 / SLIDE_FRAMES);
			pos = 2 * 256 + (t * t * (3 * 256 - 2 * t) >> 16);
			if (slide == SLIDE_FRAMES / 2) {
				cursor = 3;
				frame_measure("cover-demo");
			}
			frame_wake();
		}
		if (!frame_idle()) {
			jobs_run(JOB_BUDGET);
			draw(pos);
		}
		if (slide == SLIDE_FRAMES && !stopped && row_ready(cursor) && !gfx_pending()) {
			stopped = 1;
			tlog_debug("screen ready: cover-demo");
		}
		frame_end();
	}
}
#endif
