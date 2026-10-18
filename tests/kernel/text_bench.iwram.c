// The blitters text_blit (source/engine/text_blit.s) was timed against, in
// the test kernel only: tests/kernel/test_text.c checks they draw the same
// and logs the cycles of each. ARM code in IWRAM, like text_blit.
#ifdef TORII_TEST_KERNEL
#include "../../source/engine/text.h"
#include "text_bench.h"

// Bit i of b (8 bits) to bit 4i; m is 0x01010101, read from bench_spread_mask
// in another file so that GCC keeps it in a register
static inline __attribute__((always_inline)) u32 spread(u32 b, u32 m)
{
	b |= b << 12;
	b |= b << 6;
	return (b & m) | ((b & (m << 1)) << 3);
}

static inline __attribute__((always_inline)) u32 put_left(u32 d, u32 e, u32 ink, u32 shift)
{
	return (d & ~((e * 15) << shift)) | ((e * ink) << shift);
}

static inline __attribute__((always_inline)) u32 put_right(u32 d, u32 e, u32 ink, u32 shift)
{
	return (d & ~((e * 15) >> shift)) | ((e * ink) >> shift);
}

// text_blit's design in C: three loops, each word written once
IWRAM_CODE void bench_blit_c(const struct text_run *run)
{
	const struct torii_glyph *glyphs = run->font->glyphs;
	const u8 *bits = run->font->bits;
	const u32 m = bench_spread_mask;
	const u32 ink = run->ink;
	const u32 cw = run->column_words;
	u32 *const base = run->tiles + run->y;
	const u8 *g = run->glyphs, *const end = g + run->count;
	u32 x = run->x;

	for (; g < end; g++) {
		const struct torii_glyph *glyph = &glyphs[*g];
		u32 n = glyph->rows;
		u32 w = glyph->width;
		u32 px = x & 7;
		u32 *dst = base + (x >> 3) * cw + glyph->top;
		const u8 *src = bits + glyph->offset;
		x += glyph->advance;
		if (!n)
			continue;
		u32 ls = px * 4, rs = 32 - ls;
		if (px + w <= 8) {
			do {
				u32 b = *src;
				src += 2;
				if (b)
					*dst = put_left(*dst, spread(b, m), ink, ls);
				dst++;
			} while (--n);
		} else if (w <= 8) {
			do {
				u32 b = *src;
				src += 2;
				if (b) {
					u32 e = spread(b, m);
					dst[0] = put_left(dst[0], e, ink, ls);
					dst[cw] = put_right(dst[cw], e, ink, rs);
				}
				dst++;
			} while (--n);
		} else {
			int third = px + w > 16;
			do {
				u32 b = *(const u16 *)src;
				src += 2;
				if (b) {
					u32 lo = spread(b & 0xFF, m), hi = spread(b >> 8, m);
					u32 mid = lo >> 1 >> (31 - ls) | hi << ls;
					dst[0] = put_left(dst[0], lo, ink, ls);
					dst[cw] = put_left(dst[cw], mid, ink, 0);
					if (third)
						dst[2 * cw] = put_right(dst[2 * cw], hi, ink, rs);
				}
				dst++;
			} while (--n);
		}
	}
}

// A straightforward C port of libtonc's chr4c_drawg_b1cts_fast (MIT licence,
// J. Vijn): each 8 px column strip of a glyph in turn, every row's word
// read-modify-written, and the next tile column's word too whenever the
// strip crosses into it. A glyph wider than 8 px so writes its middle words
// twice. Adapted to the torii_font format (rows from `top`, 2 bytes a row:
// strip s reads byte s), and the second word skipped when x is a multiple of
// 8 (a shift by 32, which ARM code gives as 0, is undefined in C).
IWRAM_CODE void bench_blit_tonc(const struct text_run *run)
{
	const struct torii_glyph *glyphs = run->font->glyphs;
	const u8 *bits = run->font->bits;
	const u32 m = bench_spread_mask;
	const u32 ink = run->ink;
	const u32 cw = run->column_words;
	u32 *const base = run->tiles + run->y;
	u32 x = run->x;

	for (u32 i = 0; i < run->count; i++) {
		const struct torii_glyph *glyph = &glyphs[run->glyphs[i]];
		u32 px = x & 7, lsl = px * 4, lsr = 32 - lsl;
		u32 *column = base + (x >> 3) * cw + glyph->top;
		const u8 *strip = bits + glyph->offset;
		int right = glyph->width + px;
		x += glyph->advance;
		for (int left = glyph->width; left > 0; left -= 8, right -= 8, column += cw, strip++) {
			u32 *dst = column;
			const u8 *src = strip;
			int two = right > 8 && px;
			for (u32 n = glyph->rows; n; n--, src += 2, dst++) {
				u32 raw = *src;
				if (!raw)
					continue;
				u32 e = spread(raw, m);
				dst[0] = put_left(dst[0], e, ink, lsl);
				if (two)
					dst[cw] = put_right(dst[cw], e, ink, lsr);
			}
		}
	}
}
#endif
