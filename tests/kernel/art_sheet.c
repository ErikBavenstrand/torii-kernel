// The art sheet: every piece of the menu's art (source/ui/art.h), at 1:1 on
// the light paper, for the golden shots (tests/shots/art-sheet.opts). The
// test kernel shows it with SELECT and R held at power-on:
//
//	page 1  the 16x16 icons in their banks; the 8x8 icons as BG tiles in
//	        ink and, below, in the first item's colour; the ground shadows;
//	        the GBA placeholder box
//	page 2+ the carousel objects, six a page, each on its suggested shadow
//	then    the placeholder boxes in the light theme's colours, on its
//	        paper, then in the dark theme's on its
//
// A shows the next page (the last wraps to the first). Each page logs
// "screen ready: art-sheet-N" once its tiles are in VRAM.
#ifdef TORII_TEST_KERNEL
#include <gba_input.h>
#include <gba_interrupt.h>

#include "torii_test.h"
#include "tests.h"
#include "../../source/cartlog.h"
#include "../../source/engine/engine.h"
#include "../../source/ui/art.h"

#define TAG_ICON8 0x5401
#define TAG_BOX 0x5402
#define TAG_MAP 0x5403
#define TAG_BG_PAL 0x5404
#define TAG_OBJ_PAL 0x5405  // + the sprite's place on its page

#define PAPER 0x73BE  // #F7EFE7, the light theme's bg
#define INK 0x18A5  // #292931
#define GRAPHITE 0x1063  // #181821, the dark theme's bg
#define SHADOW_TONE 0x5AF7  // a soft grey-brown under the objects
#define PER_PAGE 6
#define SPRITE_PAGES ((ART_SPRITE_COUNT + PER_PAGE - 1) / PER_PAGE)
#define BOX_PAGE (1 + SPRITE_PAGES)  // light, then dark
#define PAGES (BOX_PAGE + 2)
#define BOXES_A_ROW 3

// OBJ attributes: shapes and sizes
#define SQUARE 0x0000
#define WIDE 0x4000
#define SIZE_16 0x4000  // square 16x16
#define SIZE_64 0xC000  // square 64x64
#define SIZE_32X8 0x4000  // wide 32x8
#define HFLIP 0x1000

static u16 map[32 * 32] EWRAM_BSS ALIGN(4);
// The 8x8 icons in two colours, then a blank tile
static u32 icon8_tiles[2 * ART_ICON8_COUNT + 1][8] EWRAM_BSS ALIGN(4);

static struct {
	u8 page;
	u8 mark;
	u16 obj_pal_bank;  // the shadow bank on page 1
	u16 icon_tile[ART_ICON16_COUNT];
	u8 icon_bank[ART_ICON16_COUNT];
	u16 shadow_tile[ART_SHADOW_COUNT];
	u16 sprite_tile[PER_PAGE];
	u8 sprite_bank[PER_PAGE];
	u8 sprites;  // on this page
} sheet EWRAM_BSS;

static u16 must(u16 tile)
{
	TLOG_ASSERT(tile != ART_NONE);
	return tile;
}

// Waits for the VRAM queue to have room for one more copy.
static void queue_room(void)
{
	while (gfx_queue_len() >= GFX_QUEUE_CAP - 1)
		frame_begin();
}

// A sprite bank of colours, with the shadow's tone in ART_SHADOW_INDEX.
static u8 obj_bank(u16 tag, const u16 *colours)
{
	u8 h = res_palette(RES_OBJ_PAL, 1, tag);
	TLOG_ASSERT(h != RES_NONE);
	u8 bank = (u8)res_start(RES_OBJ_PAL, h);
	u16 c[16];
	for (u32 i = 0; i < 16; i++)
		c[i] = colours[i];
	c[ART_SHADOW_INDEX] = SHADOW_TONE;
	gfx_pal_load(16u + bank, c, 16);
	return bank;
}

static void load_shadows(void)
{
	for (u32 i = 0; i < ART_SHADOW_COUNT; i++) {
		queue_room();
		sheet.shadow_tile[i] = must(art_load_shadow(i));
	}
}

static void setup_icons(void)
{
	for (u32 i = 0; i < ART_ICON16_COUNT; i++) {
		queue_room();
		sheet.icon_tile[i] = must(art_load_icon16(i, &sheet.icon_bank[i]));
	}
	static const u16 shadow_bank[16] = {0};
	sheet.obj_pal_bank = obj_bank(TAG_OBJ_PAL, shadow_bank);
	load_shadows();

	// BG0: the 8x8 icons in ink (colour 1) and in the first item's colour
	// (colour 2), and the box (BG bank 1)
	for (u32 i = 0; i < ART_ICON8_COUNT; i++) {
		art_icon8_tile(i, 1, icon8_tiles[i]);
		art_icon8_tile(i, 2, icon8_tiles[ART_ICON8_COUNT + i]);
	}
	for (u32 i = 0; i < 8; i++)
		icon8_tiles[2 * ART_ICON8_COUNT][i] = 0;
	u8 icons = res_bg_tiles(2 * ART_ICON8_COUNT + 1, 0, 0, TAG_ICON8);
	u8 box = res_bg_tiles(ART_BOX_TILES, 0, 0, TAG_BOX);
	u8 m = res_bg_map(1, TAG_MAP);
	u8 pal = res_palette(RES_BG_PAL, 2, TAG_BG_PAL);
	TLOG_ASSERT(icons != RES_NONE && box != RES_NONE && m != RES_NONE && pal != RES_NONE);
	u16 bank = res_start(RES_BG_PAL, pal);
	u16 first = res_bg_tile_index(icons, 0, 0), box_first = res_bg_tile_index(box, 0, 0);
	// Three rows of eight, a blank column between, in ink at rows 4-8 and
	// in the item's colour at rows 10-14; the box at tile (19, 4)
	for (u32 i = 0; i < 32 * 32; i++)
		map[i] = (u16)(first + 2 * ART_ICON8_COUNT);
	for (u32 i = 0; i < ART_ICON8_COUNT; i++) {
		u32 x = 1 + (i % 8) * 2, y = 4 + (i / 8) * 2;
		map[y * 32 + x] = (u16)(bank << 12 | (first + i));
		map[(y + 6) * 32 + x] = (u16)(bank << 12 | (first + ART_ICON8_COUNT + i));
	}
	for (u32 y = 0; y < 10; y++)
		for (u32 x = 0; x < 10; x++)
			map[(4 + y) * 32 + 19 + x] = (u16)((bank + 1) << 12 | (box_first + y * 10 + x));
	queue_room();
	TLOG_ASSERT(gfx_queue_copy(res_bg_addr(icons), icon8_tiles, sizeof(icon8_tiles)));
	queue_room();
	TLOG_ASSERT(gfx_queue_copy(res_bg_addr(box), art_box_tiles[ART_BOX_GBA], sizeof(art_box_tiles[0])));
	queue_room();
	TLOG_ASSERT(gfx_queue_copy(res_bg_addr(m), map, sizeof(map)));
	const u16 neutrals[3] = {0, INK, 0x627E};  // 2: the fav ink, a stand-in item colour
	gfx_pal_load(bank, neutrals, 3);
	static const u16 paper = PAPER;
	gfx_pal_load(0, &paper, 1);  // the backdrop
	gfx_pal_load(bank + 1u, art_box_palettes[ART_BOX_GBA][0], 16);
	gfx_set_bgcnt(0, (u16)(res_bg_screenblock(m) << 8));
	gfx_set_dispcnt(0x0100 | 0x1000 | 0x0040);  // mode 0, BG0, sprites, 1D tiles
}

// Every placeholder box at 1:1, three a row, in a theme's colours (dark 0
// or 1) on its paper
static void setup_boxes(u32 dark)
{
	u8 box = res_bg_tiles(ART_BOX_COUNT * ART_BOX_TILES + 1, 0, 0, TAG_BOX);  // + a blank
	u8 m = res_bg_map(1, TAG_MAP);
	u8 pal = res_palette(RES_BG_PAL, ART_BOX_COUNT, TAG_BG_PAL);
	TLOG_ASSERT(box != RES_NONE && m != RES_NONE && pal != RES_NONE);
	u16 bank = res_start(RES_BG_PAL, pal), first = res_bg_tile_index(box, 0, 0);
	u8 *tiles = res_bg_addr(box);
	u16 blank = (u16)(first + ART_BOX_COUNT * ART_BOX_TILES);
	static const u32 blank_tile[8] = {0};
	queue_room();
	TLOG_ASSERT(gfx_queue_copy(tiles + (blank - first) * 32u, blank_tile, sizeof(blank_tile)));
	for (u32 i = 0; i < 32 * 32; i++)
		map[i] = blank;
	for (u32 b = 0; b < ART_BOX_COUNT; b++) {
		u32 x0 = (b % BOXES_A_ROW) * 10, y0 = (b / BOXES_A_ROW) * 10;
		for (u32 y = 0; y < 10; y++)
			for (u32 x = 0; x < 10; x++)
				map[(y0 + y) * 32 + x0 + x] =
					(u16)((bank + b) << 12 | (first + b * ART_BOX_TILES + y * 10 + x));
		queue_room();
		TLOG_ASSERT(gfx_queue_copy(tiles + b * sizeof(art_box_tiles[0]), art_box_tiles[b],
					   sizeof(art_box_tiles[0])));
		gfx_pal_load(bank + b, art_box_palettes[b][dark], 16);
	}
	queue_room();
	TLOG_ASSERT(gfx_queue_copy(res_bg_addr(m), map, sizeof(map)));
	static const u16 paper[2] = {PAPER, GRAPHITE};
	gfx_pal_load(0, &paper[dark], 1);
	gfx_set_bgcnt(0, (u16)(res_bg_screenblock(m) << 8));
	gfx_set_dispcnt(0x0100 | 0x0040);  // mode 0, BG0
}

static void setup_sprites(u32 page)
{
	sheet.sprites = 0;
	load_shadows();
	for (u32 i = 0; i < PER_PAGE; i++) {
		u32 s = (page - 1) * PER_PAGE + i;
		if (s >= ART_SPRITE_COUNT)
			break;
		queue_room();
		sheet.sprite_tile[i] = must(art_load_sprite(s));
		sheet.sprite_bank[i] = obj_bank((u16)(TAG_OBJ_PAL + 1 + i), art_sprite_palettes[s]);
		sheet.sprites++;
	}
	static const u16 paper = PAPER;
	gfx_pal_load(0, &paper, 1);
	gfx_set_dispcnt(0x1000 | 0x0040);  // sprites only
}

// A shadow centred on (cx, foot): its left half, then the flipped right one.
static void add_shadow(u32 shadow, s32 cx, s32 foot, u32 bank)
{
	s32 y = foot - ART_SHADOW_RISE;
	u16 a2 = (u16)(bank << 12 | 1 << 10 | sheet.shadow_tile[shadow]);
	gfx_oam_add((u16)(WIDE | (y & 0xFF)), (u16)(SIZE_32X8 | ((cx - 32) & 0x1FF)), a2);
	gfx_oam_add((u16)(WIDE | (y & 0xFF)), (u16)(SIZE_32X8 | HFLIP | (cx & 0x1FF)), a2);
}

static void draw(void)
{
	gfx_oam_begin();
	if (sheet.page >= BOX_PAGE) {
		// BG only
	} else if (sheet.page == 0) {
		for (u32 i = 0; i < ART_ICON16_COUNT; i++)
			gfx_oam_add((u16)(SQUARE | 8), (u16)(SIZE_16 | (6 + i * 18)),
				    (u16)(sheet.icon_bank[i] << 12 | sheet.icon_tile[i]));
		// Two rows of three along the bottom
		for (u32 i = 0; i < ART_SHADOW_COUNT; i++)
			add_shadow(i, 40 + (s32)(i % 3) * 80, 136 + (s32)(i / 3) * 14, sheet.obj_pal_bank);
	} else {
		for (u32 i = 0; i < sheet.sprites; i++) {
			u32 s = (sheet.page - 1u) * PER_PAGE + i;
			s32 x = 6 + (s32)(i % 3) * 78, y = 8 + (s32)(i / 3) * 76;
			gfx_oam_add((u16)(SQUARE | (y & 0xFF)), (u16)(SIZE_64 | (x & 0x1FF)),
				    (u16)(sheet.sprite_bank[i] << 12 | sheet.sprite_tile[i]));
			add_shadow(art_sprites[s].shadow, x + 32, y + ART_SPRITE_FOOT, sheet.sprite_bank[i]);
		}
	}
	gfx_oam_end();
}

static void show(u32 page)
{
	res_release_to(sheet.mark);
	gfx_queue_clear();
	sheet.page = (u8)page;
	if (page == 0)
		setup_icons();
	else if (page >= BOX_PAGE)
		setup_boxes(page - BOX_PAGE);
	else
		setup_sprites(page);
	draw();
}

void art_sheet(void)
{
	irqInit();
	cartlog_install();
	engine_init();
	sheet.mark = res_mark();
	show(0);
	u32 ready = 0;
	u16 held = 0;
	for (;;) {
		frame_begin();
		u16 keys = ~REG_KEYINPUT & 0x3FF;
		if (keys & ~held & KEY_A) {
			show((sheet.page + 1u) % PAGES);
			ready = 0;
		}
		held = keys;
		if (!ready && !gfx_pending()) {
			tlog_debug("screen ready: art-sheet-%u", sheet.page + 1u);
			ready = 1;
		}
		frame_end();
	}
}
#endif
