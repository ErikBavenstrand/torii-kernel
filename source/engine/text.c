// Drawing text into window tiles (text.h); the blitter is text_blit.s.
#include "text.h"

#include <gba_systemcalls.h>

#include "../tlog.h"

#include <stddef.h>

// text_blit.s hard-codes these
_Static_assert(offsetof(struct text_run, tiles) == 0, "RUN_TILES");
_Static_assert(offsetof(struct text_run, column_words) == 4, "RUN_COLUMN_WORDS");
_Static_assert(offsetof(struct text_run, font) == 8, "RUN_FONT");
_Static_assert(offsetof(struct text_run, glyphs) == 12, "RUN_GLYPHS");
_Static_assert(offsetof(struct text_run, count) == 16, "RUN_COUNT");
_Static_assert(offsetof(struct text_run, x) == 20, "RUN_X");
_Static_assert(offsetof(struct text_run, y) == 24, "RUN_Y");
_Static_assert(offsetof(struct text_run, ink) == 28, "RUN_INK");
_Static_assert(offsetof(struct torii_font, glyphs) == 0, "FONT_GLYPHS");
_Static_assert(offsetof(struct torii_font, bits) == 4, "FONT_BITS");
_Static_assert(sizeof(struct torii_glyph) == 6, "the glyph table's stride");
_Static_assert(offsetof(struct torii_glyph, offset) == 0, "GLYPH_OFFSET");
_Static_assert(offsetof(struct torii_glyph, advance) == 2, "GLYPH_ADVANCE");
_Static_assert(offsetof(struct torii_glyph, top) == 3, "GLYPH_TOP");
_Static_assert(offsetof(struct torii_glyph, rows) == 4, "GLYPH_ROWS");
_Static_assert(offsetof(struct torii_glyph, width) == 5, "GLYPH_WIDTH");

int text_line_fits(const struct text_window *win, const struct torii_font *font, u32 x, u32 y,
		   const struct text_line *line)
{
	return y + font->line_height <= win->height * 8u && x + line->extent <= win->width * 8u && font->row_bytes == 2;
}

u32 text_draw_line(const struct text_window *win, const struct torii_font *font, u32 x, u32 y, u32 ink,
		   const struct text_line *line)
{
	if (!line->count)
		return x;
	if (!text_line_fits(win, font, x, y, line)) {
		tlog_error("assert failed: text_draw_line: line at %lu,%lu outside the window", (unsigned long)x,
			   (unsigned long)y);
		return x;
	}
	struct text_run run = {
		.tiles = win->tiles,
		.column_words = win->height * 8u,
		.font = font,
		.glyphs = line->glyphs,
		.count = line->count,
		.x = x,
		.y = y,
		.ink = ink & 15,
	};
	text_blit(&run);
	return x + line->advance;
}

u32 text_draw(const struct text_window *win, const struct torii_font *font, u32 x, u32 y, u32 clip, u32 ink,
	      const char *s, u32 len)
{
	u32 right = win->width * 8u;
	if (x >= right)
		return x;
	if (clip > right - x)
		clip = right - x;
	struct text_line line;
	text_layout(font, s, len, clip, &line);
	return text_draw_line(win, font, x, y, ink, &line);
}

// h words of each of n tile columns, column_words apart, to fill
static void fill_columns(u32 *p, u32 n, u32 h, u32 column_words, u32 fill)
{
	if (h == column_words) {
		// One run: the BIOS's CpuFastSet stores 8 words at a time
		// (src, not fill, has its address taken, or the stores below
		// would reload fill after each other)
		u32 words = n * h, blocks = words & ~7u, src = fill;
		if (blocks)
			CpuFastSet(&src, p, blocks | 1 << 24);
		for (u32 i = blocks; i < words; i++)
			p[i] = fill;
		return;
	}
	for (; n; n--, p += column_words) {
		u32 *q = p, *end = p + h;
		for (; q + 4 <= end; q += 4) {
			q[0] = fill;
			q[1] = fill;
			q[2] = fill;
			q[3] = fill;
		}
		while (q < end)
			*q++ = fill;
	}
}

// Pixels [first, last) of h words of a tile column (last - first 1 to 7)
static void fill_part(u32 *p, u32 first, u32 last, u32 h, u32 fill)
{
	u32 mask = (0xFFFFFFFFu >> (32 - 4 * (last - first))) << (4 * first);
	u32 bits = fill & mask;
	for (u32 i = 0; i < h; i++)
		p[i] = (p[i] & ~mask) | bits;
}

void text_clear(const struct text_window *win, u32 x, u32 y, u32 w, u32 h, u32 index)
{
	u32 right = win->width * 8u, bottom = win->height * 8u;
	if (x >= right || y >= bottom)
		return;
	if (w > right - x)
		w = right - x;
	if (h > bottom - y)
		h = bottom - y;
	if (!w || !h)
		return;
	u32 fill = (index & 15) * 0x11111111u;
	u32 column_words = win->height * 8u;
	u32 *column = win->tiles + (x >> 3) * column_words + y;
	u32 end = x + w;
	if (x & 7) {
		// The first tile column, partly
		u32 last = end - (x & ~7u) < 8 ? end & 7 : 8;
		fill_part(column, x & 7, last, h, fill);
		x = (x & ~7u) + 8;
		column += column_words;
	}
	if (x >= end)
		return;
	u32 whole = (end - x) / 8;
	fill_columns(column, whole, h, column_words, fill);
	if (end & 7)
		fill_part(column + whole * column_words, 0, end & 7, h, fill);
}

void text_clear_rows(const struct text_window *win, u32 y, u32 h)
{
	text_clear(win, 0, y, win->width * 8u, h, 0);
}
