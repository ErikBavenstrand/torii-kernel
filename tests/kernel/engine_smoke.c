// The engine smoke scene: after the unit tests, the test kernel shows a
// mode 0 scene drawn only through the engine (allocators, the VRAM queue,
// shadow registers, palettes and OAM, the frame loop), so a screenshot shows
// the whole path works:
//
//	emu/build-linux/mgba-shot ezkerneltest.gba -u 'engine-smoke: idle' -s ...
//
// A checked BG0 scrolls and an affine sprite (scaled 2x) moves for 60
// frames, then stops; the scene logs "screen ready: engine-smoke", one
// measured frame ("frame-cycles: engine-smoke N"), and "engine-smoke: idle"
// once frames have gone idle (no draw, nothing to commit).
#ifdef TORII_TEST_KERNEL
#include <gba_interrupt.h>

#include "torii_test.h"
#include "tests.h"
#include "../../source/cartlog.h"
#include "../../source/engine/engine.h"

// 4bpp tiles, a nibble per pixel: plain, a dot, a frame, a ball for the sprite
static const u32 bg_tiles[3][8] = {
	{0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111, 0x11111111},
	{0x22222222, 0x22222222, 0x22333322, 0x22333322, 0x22333322, 0x22333322, 0x22222222, 0x22222222},
	{0x44444444, 0x41111114, 0x41111114, 0x41111114, 0x41111114, 0x41111114, 0x41111114, 0x44444444},
};
static const u32 obj_tiles[4][8] = {
	// a 16x16 ball: top left, top right, bottom left, bottom right
	{0x00000000, 0x11000000, 0x22110000, 0x22221000, 0x22222100, 0x22222100, 0x22222210, 0x22222210},
	{0x00000000, 0x00000011, 0x00001122, 0x00012222, 0x00122222, 0x00122222, 0x01222222, 0x01222222},
	{0x22222210, 0x22222210, 0x22222100, 0x22222100, 0x22221000, 0x22110000, 0x11000000, 0x00000000},
	{0x01222222, 0x01222222, 0x00122222, 0x00122222, 0x00012222, 0x00001122, 0x00000011, 0x00000000},
};
static const u16 bg_colours[16] = {0x0000, 0x2108, 0x4E73, 0x7FFF, 0x7E00};
static const u16 obj_colours[16] = {0x0000, 0x0010, 0x03FF};

#define TAG_TILES 0x5301
#define TAG_MAP 0x5302
#define TAG_BG_PAL 0x5303
#define TAG_OBJ 0x5304
#define TAG_OBJ_PAL 0x5305
#define TAG_MAT 0x5306

// The map: rows of plain and dotted tiles, framed every fourth row
static u16 map_rows[4][32] EWRAM_BSS ALIGN(4);

void engine_smoke(void)
{
	irqInit();
	cartlog_install();
	engine_init();

	u8 tiles = res_bg_tiles(3, 0, 0, TAG_TILES);
	u8 map = res_bg_map(1, TAG_MAP);
	u8 bg_pal = res_palette(RES_BG_PAL, 1, TAG_BG_PAL);
	u8 obj = res_obj_tiles(4, 0, TAG_OBJ);
	u8 obj_pal = res_palette(RES_OBJ_PAL, 1, TAG_OBJ_PAL);
	u8 mat = res_affine(TAG_MAT);
	TLOG_ASSERT(tiles != RES_NONE && map != RES_NONE && bg_pal != RES_NONE);
	TLOG_ASSERT(obj != RES_NONE && obj_pal != RES_NONE && mat != RES_NONE);

	u16 first = res_bg_tile_index(tiles, 0, 0);
	u16 bank = res_start(RES_BG_PAL, bg_pal);
	for (u32 x = 0; x < 32; x++) {
		u16 pal = (u16)(bank << 12);
		map_rows[0][x] = pal | (u16)(first + ((x & 1) ? 1 : 0));
		map_rows[1][x] = pal | (u16)(first + ((x & 1) ? 0 : 1));
		map_rows[2][x] = pal | (u16)first;
		map_rows[3][x] = pal | (u16)(first + 2);
	}
	gfx_queue_copy(res_bg_addr(tiles), bg_tiles, sizeof(bg_tiles));
	u16 *map_vram = res_bg_addr(map);
	for (u32 y = 0; y < 32; y += 4)
		gfx_queue_copy(map_vram + y * 32, map_rows, sizeof(map_rows));
	gfx_queue_copy(res_obj_addr(obj), obj_tiles, sizeof(obj_tiles));
	gfx_pal_load(bank, bg_colours, 16);
	gfx_pal_load(16 + res_start(RES_OBJ_PAL, obj_pal), obj_colours, 16);

	gfx_set_bgcnt(0, (u16)(res_bg_screenblock(map) << 8));  // char base 0, 4bpp, 32x32
	gfx_set_dispcnt(0x0000 | 0x0100 | 0x1000 | 0x0040);  // mode 0, BG0, sprites, 1D tiles
	u16 m = res_start(RES_AFFINE, mat);
	gfx_affine_set(m, 0x80, 0, 0, 0x80);  // 2x

	u32 idle = 0, ready = 0;
	for (u32 frame = 0;; frame++) {
		frame_begin();
		u32 t = frame < 60 ? frame : 60;
		if (frame <= 60)
			frame_wake();
		if (frame == 62) {
			// the last state was drawn in frame 60 and committed in 61
			tlog("screen ready: engine-smoke");
			frame_measure("engine-smoke");
			frame_wake();  // a measured frame that draws (the same picture)
			ready = 1;
		}
		if (!frame_idle()) {
			gfx_set_scroll(0, (u16)(t * 2), (u16)t);
			gfx_oam_begin();
			// affine, double size: 32x32 on screen
			gfx_oam_add((u16)(0x0300 | (40 + t / 2)), (u16)(0x4000 | m << 9 | (40 + t * 2)),
				    (u16)(res_start(RES_OBJ_PAL, obj_pal) << 12 | res_start(RES_OBJ_VRAM, obj)));
			gfx_oam_end();
		}
		if (ready && frame > 63 && frame_idle() && ++idle == 10)
			tlog("engine-smoke: idle");
		frame_end();
	}
}
#endif
