// Text in tiles (source/engine/text.c, text_blit.s): glyphs drawn into RAM
// tiles match a pixel-by-pixel reference, clears are exact, and the
// blitter's cycles per glyph and per 38-glyph row are logged as
// "frame-cycles: text-..." lines (in the test kernel also those of the
// alternatives in text_bench.iwram.c).
#include <string.h>

#include <gba_base.h>
#include <gba_interrupt.h>

#include "../../source/diag.h"
#include "../../source/engine/text.h"
#include "../../source/waitcnt.h"
#include "text_bench.h"
#include "torii_test.h"
#include "tests.h"

const u32 bench_spread_mask = 0x01010101;

// Two windows of 32 x 2 tiles (2 KB each) in the kernel's read cache, which
// is free while tests run
extern u8 pReadCache[];
#define COLUMNS 32
#define ROWS 2
#define CW (ROWS * 8)
#define WORDS (COLUMNS * CW)
#define tiles_a ((u32 *)pReadCache)
#define tiles_b ((u32 *)pReadCache + WORDS)

static void fill(u32 *tiles, u32 seed)
{
	for (u32 i = 0; i < WORDS; i++)
		tiles[i] = seed ^ (i * 0x9E3779B9u);
}

static u32 pixel(const u32 *tiles, u32 x, u32 y)
{
	return tiles[(x >> 3) * CW + y] >> (x & 7) * 4 & 15;
}

static void set_pixel(u32 *tiles, u32 x, u32 y, u32 index)
{
	u32 *w = &tiles[(x >> 3) * CW + y];
	u32 s = (x & 7) * 4;
	*w = (*w & ~(15u << s)) | index << s;
}

// The reference: every inked bit of every row, one pixel at a time
static void ref_draw(u32 *tiles, const struct torii_font *font, const u8 *glyphs, u32 count, u32 x, u32 y, u32 ink)
{
	for (u32 i = 0; i < count; i++) {
		const struct torii_glyph *g = &font->glyphs[glyphs[i]];
		const u8 *row = font->bits + g->offset;
		for (u32 r = 0; r < g->rows; r++, row += font->row_bytes) {
			u32 bits = row[0] | (font->row_bytes > 1 ? row[1] << 8 : 0);
			for (u32 c = 0; c < 16; c++)
				if (bits >> c & 1)
					set_pixel(tiles, x + c, y + g->top + r, ink);
		}
		x += g->advance;
	}
}

typedef void blit_fn(const struct text_run *run);

// Every glyph of the font, in runs that fit the window, at pen offset x0
static int blit_matches(blit_fn *blit, const struct torii_font *font, u32 x0, u32 ink)
{
	u8 glyphs[TORII_FONT_GLYPHS];
	u32 next = 0;
	while (next < TORII_FONT_GLYPHS) {
		u32 count = 0, pen = x0;
		while (next < TORII_FONT_GLYPHS && pen + font->glyphs[next].advance + 16 < COLUMNS * 8) {
			pen += font->glyphs[next].advance;
			glyphs[count++] = next++;
		}
		fill(tiles_a, x0 * 77 + next);
		fill(tiles_b, x0 * 77 + next);
		struct text_run run = {tiles_a, CW, font, glyphs, count, x0, ROWS * 8 - font->line_height, ink};
		blit(&run);
		ref_draw(tiles_b, font, glyphs, count, x0, run.y, ink);
		if (memcmp(tiles_a, tiles_b, WORDS * 4) != 0)
			return 0;
	}
	return 1;
}

static void check_blitter(blit_fn *blit, const char *name)
{
	static const u8 offsets[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 13};
	const struct torii_font *fonts[] = {&font_body, &font_heading};
	for (u32 f = 0; f < 2; f++) {
		for (u32 i = 0; i < sizeof(offsets); i++) {
			if (!blit_matches(blit, fonts[f], offsets[i], 1 + (i + f) % 15)) {
				tlog_error("FAIL %s: %s font at x %u differs from the reference", name,
					   f ? "heading" : "body", offsets[i]);
				++torii_test_failures;
			}
		}
	}
}

void test_text_blit(void)
{
	check_blitter(text_blit, "text_blit");
#ifdef TORII_TEST_KERNEL
	check_blitter(bench_blit_c, "bench_blit_c");
	check_blitter(bench_blit_tonc, "bench_blit_tonc");
#endif

	// 'A' at x 6, ink 3, on blank tiles (font_body.c pictures it)
	memset(tiles_a, 0, WORDS * 4);
	u8 a = 'A' - TORII_FONT_FIRST;
	struct text_run run = {tiles_a, CW, &font_body, &a, 1, 6, 0, 3};
	text_blit(&run);
	CHECK_EQ(tiles_a[1], 0);           // row 1: above the glyph's top (2)
	CHECK_EQ(tiles_a[CW + 2], 0x3);    // row 2, "..#..": pixel 8
	CHECK_EQ(tiles_a[2], 0);
	CHECK_EQ(tiles_a[7], 0x33000000);  // row 7, "#####": pixels 6-10
	CHECK_EQ(tiles_a[CW + 7], 0x333);
	CHECK_EQ(tiles_a[10], 0x03000000); // row 10, "#...#": pixels 6 and 10
	CHECK_EQ(tiles_a[CW + 10], 0x300);
	CHECK_EQ(tiles_a[11], 0);          // below the glyph
	CHECK_EQ(tiles_a[2 * CW + 7], 0);  // the next column untouched
}

void test_text_draw(void)
{
	static const char name[] = "Pok\xC3\xA9mon - Ruby Version (Europe) (Rev 1).gba";
	struct text_window win = {tiles_a, COLUMNS, ROWS};
	struct text_line line;

	// Cut to 90 px from x 5: the same pixels as the reference, none past the clip
	fill(tiles_a, 1);
	fill(tiles_b, 1);
	u32 end = text_draw(&win, &font_body, 5, 3, 90, 7, name, sizeof(name) - 1);
	text_layout(&font_body, name, sizeof(name) - 1, 90, &line);
	CHECK(line.truncated);
	CHECK_EQ(end, 5 + line.advance);
	ref_draw(tiles_b, &font_body, line.glyphs, line.count, 5, 3, 7);
	CHECK(memcmp(tiles_a, tiles_b, WORDS * 4) == 0);
	fill(tiles_b, 1);
	u32 changed_outside = 0;
	for (u32 y = 0; y < ROWS * 8; y++)
		for (u32 x = 0; x < COLUMNS * 8; x++)
			if ((x < 5 || x >= 95) && pixel(tiles_a, x, y) != pixel(tiles_b, x, y))
				changed_outside++;
	CHECK_EQ(changed_outside, 0);

	// The window's right edge clips too, and a line too low draws nothing
	fill(tiles_a, 2);
	fill(tiles_b, 2);
	text_draw(&win, &font_body, COLUMNS * 8 - 20, 0, 200, 9, name, sizeof(name) - 1);
	text_layout(&font_body, name, sizeof(name) - 1, 20, &line);
	ref_draw(tiles_b, &font_body, line.glyphs, line.count, COLUMNS * 8 - 20, 0, 9);
	CHECK(memcmp(tiles_a, tiles_b, WORDS * 4) == 0);

	// Lines outside the window: text_draw_line refuses them (and logs an
	// assert, so they're only checked here), text_draw past the right edge
	// draws nothing
	text_layout(&font_body, "Hi", 2, 100, &line);
	u32 low = ROWS * 8 - font_body.line_height;
	CHECK(text_line_fits(&win, &font_body, 0, low, &line));
	CHECK(!text_line_fits(&win, &font_body, 0, low + 1, &line));
	CHECK(text_line_fits(&win, &font_body, COLUMNS * 8 - line.extent, 0, &line));
	CHECK(!text_line_fits(&win, &font_body, COLUMNS * 8 - line.extent + 1, 0, &line));
	fill(tiles_a, 3);
	fill(tiles_b, 3);
	CHECK_EQ(text_draw(&win, &font_body, COLUMNS * 8, 0, 50, 9, "Hi", 2), COLUMNS * 8);
	CHECK(memcmp(tiles_a, tiles_b, WORDS * 4) == 0);
}

void test_text_clear(void)
{
	struct text_window win = {tiles_a, COLUMNS, ROWS};
	static const struct {
		u8 x, y, w, h;
	} rects[] = {{3, 5, 21, 7}, {0, 0, 8, 16}, {9, 1, 3, 2}, {16, 4, 64, 8}, {250, 10, 40, 40}};
	for (u32 i = 0; i < sizeof(rects) / sizeof(*rects); i++) {
		u32 x0 = rects[i].x, y0 = rects[i].y, w = rects[i].w, h = rects[i].h;
		fill(tiles_a, i);
		fill(tiles_b, i);
		text_clear(&win, x0, y0, w, h, 2 + i);
		u32 wrong = 0;
		for (u32 y = 0; y < ROWS * 8; y++)
			for (u32 x = 0; x < COLUMNS * 8; x++) {
				int inside = x >= x0 && x < x0 + w && y >= y0 && y < y0 + h;
				wrong += pixel(tiles_a, x, y) != (inside ? 2 + i : pixel(tiles_b, x, y));
			}
		CHECK_EQ(wrong, 0);
	}
	// A row, and the whole window (one run of whole columns)
	fill(tiles_a, 9);
	fill(tiles_b, 9);
	text_clear_rows(&win, 8, 8);
	u32 wrong = 0;
	for (u32 y = 0; y < ROWS * 8; y++)
		for (u32 x = 0; x < COLUMNS * 8; x++)
			wrong += pixel(tiles_a, x, y) != (y >= 8 ? 0 : pixel(tiles_b, x, y));
	CHECK_EQ(wrong, 0);
	text_clear(&win, 0, 0, 999, 999, 5);
	for (u32 i = 0; i < WORDS; i++)
		wrong += tiles_a[i] != 0x55555555;
	CHECK_EQ(wrong, 0);
	CHECK_EQ(tiles_a[WORDS], tiles_b[0]);  // tiles_b, right after, untouched
}

// --- Cycles ---------------------------------------------------------------

// Fewest cycles of a few runs, less the timer's own, with IRQs off and the
// cart at the menu's wait states
static u32 cycles(blit_fn *blit, const struct text_run *run)
{
	u16 ime = REG_IME, waitcnt = REG_WAITCNT_CART;
	REG_IME = 0;
	REG_WAITCNT_CART = WAITCNT_MENU;
	diag_timer_start();
	u32 overhead = diag_timer_stop();
	u32 best = 0xFFFFFFFF;
	for (int i = 0; i < 4; i++) {
		diag_timer_start();
		blit(run);
		u32 t = diag_timer_stop() - overhead;
		if (t < best)
			best = t;
	}
	REG_WAITCNT_CART = waitcnt;
	REG_IME = ime;
	return best;
}

// A list row's worth of text: 38 glyphs, none wider than 8 px
static const char row38[] = "Super Mario Advance 4 - Mario Bros. 3!";

void test_text_cycles(void)
{
	// Hidden tiles in VRAM: the sprite tiles from 0x06014000, unused by
	// the mode 3 menu
	u32 *vram = (u32 *)0x06014000;
	struct text_window win = {vram, 30, 2};
	struct text_line line;
	text_clear(&win, 0, 0, 240, 16, 0);

	text_layout(&font_body, row38, sizeof(row38) - 1, 240, &line);
	CHECK_EQ(line.count, 38);
	CHECK(!line.truncated);
	struct text_run run = {vram, 16, &font_body, line.glyphs, line.count, 1, 2, 1};
	u32 row = cycles(text_blit, &run);
	struct text_run one = {vram, 16, &font_body, line.glyphs + 1, 1, 3, 2, 1};  // 'u' at x 3
	u32 glyph = cycles(text_blit, &one);
	tlog("frame-cycles: text-glyph %lu", (unsigned long)glyph);
	tlog("frame-cycles: text-row38 %lu (%lu a glyph)", (unsigned long)row, (unsigned long)(row / 38));

	// A heading's worth: 20 glyphs of the 16 px font, most over 8 px wide
	static const char heading[] = "SETTINGS AND OPTIONS";
	text_layout(&font_heading, heading, sizeof(heading) - 1, 240, &line);
	struct text_run head = {vram, 16, &font_heading, line.glyphs, line.count, 1, 0, 1};
	u32 wide = cycles(text_blit, &head);
	tlog("frame-cycles: text-heading20 %lu (%lu a glyph)", (unsigned long)wide, (unsigned long)(wide / line.count));

	// Clearing a 16 px list row of 30 tiles, and an 8 px one
	u16 ime = REG_IME, waitcnt = REG_WAITCNT_CART;
	REG_IME = 0;
	REG_WAITCNT_CART = WAITCNT_MENU;
	diag_timer_start();
	text_clear_rows(&win, 0, 16);
	u32 clear = diag_timer_stop();
	diag_timer_start();
	text_clear_rows(&win, 4, 8);
	u32 clear8 = diag_timer_stop();
	REG_WAITCNT_CART = waitcnt;
	REG_IME = ime;
	tlog("frame-cycles: text-clear-row %lu", (unsigned long)clear);
	tlog("frame-cycles: text-clear-row8-unaligned %lu", (unsigned long)clear8);

#ifdef TORII_TEST_KERNEL
	text_layout(&font_body, row38, sizeof(row38) - 1, 240, &line);
	run.glyphs = line.glyphs;
	tlog("frame-cycles: text-row38-c %lu", (unsigned long)cycles(bench_blit_c, &run));
	tlog("frame-cycles: text-row38-tonc %lu", (unsigned long)cycles(bench_blit_tonc, &run));
	tlog("frame-cycles: text-heading20-c %lu", (unsigned long)cycles(bench_blit_c, &head));
	tlog("frame-cycles: text-heading20-tonc %lu", (unsigned long)cycles(bench_blit_tonc, &head));

	// The body font with 1 byte rows (its 8 px glyphs only) against 2 byte
	// rows, both copied to EWRAM after the test windows so that the bitmaps
	// are read from the same memory
	u32 size = 0;
	for (u32 i = 0; i < TORII_FONT_GLYPHS; i++) {
		const struct torii_glyph *g = &font_body.glyphs[i];
		if (g->offset + g->rows * 2u > size)
			size = g->offset + g->rows * 2u;
	}
	struct torii_glyph *glyphs1 = (struct torii_glyph *)(pReadCache + 0x2000);
	struct torii_glyph *glyphs2 = (struct torii_glyph *)(pReadCache + 0x2800);
	u8 *bits1 = pReadCache + 0x3000, *bits2 = pReadCache + 0x4000;
	memcpy(glyphs2, font_body.glyphs, sizeof(*glyphs2) * TORII_FONT_GLYPHS);
	memcpy(glyphs1, font_body.glyphs, sizeof(*glyphs1) * TORII_FONT_GLYPHS);
	for (u32 i = 0; i < TORII_FONT_GLYPHS; i++)
		glyphs1[i].offset /= 2;
	memcpy(bits2, font_body.bits, size);
	for (u32 i = 0; i < size / 2; i++)
		bits1[i] = font_body.bits[2 * i];
	struct torii_font body1 = font_body, body2 = font_body;
	body1.glyphs = glyphs1;
	body1.bits = bits1;
	body1.row_bytes = 1;
	body2.glyphs = glyphs2;
	body2.bits = bits2;
	run.font = &body1;
	tlog("frame-cycles: text-row38-rows1-ewram %lu", (unsigned long)cycles(text_blit_rows1, &run));
	run.font = &body2;
	tlog("frame-cycles: text-row38-rows2-ewram %lu", (unsigned long)cycles(text_blit, &run));
#endif
}
