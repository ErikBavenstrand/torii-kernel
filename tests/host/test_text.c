// Text measuring and cutting (source/engine/text_layout.c) and the Latin-1
// reading of UTF-8 (charset_next_latin1), with the menu fonts.
#include <stdlib.h>
#include <string.h>

#include "../../source/charset.h"
#include "../../source/engine/text.h"
#include "../kernel/torii_test.h"
#include "tests.h"

#define G(c) ((c) - TORII_FONT_FIRST)

// Each string is a heap copy of exactly its bytes, without a NUL, so
// AddressSanitizer catches a read past the length
static char *copy(const char *s, u32 len)
{
	char *p = malloc(len ? len : 1);
	memcpy(p, s, len);
	return p;
}

// The code points charset_next_latin1 reads from s, CHARSET_NONE kept
static u32 latin1(const char *s, u32 *out)
{
	u32 len = strlen(s), pos = 0, n = 0;
	char *p = copy(s, len);
	while (pos < len) {
		u32 before = pos;
		out[n++] = charset_next_latin1(p, len, &pos);
		CHECK(pos > before && pos <= len);
	}
	free(p);
	return n;
}

static void check_latin1(const char *s, const u32 *expected, u32 count)
{
	u32 out[64];
	u32 n = latin1(s, out);
	CHECK_EQ(n, count);
	for (u32 i = 0; i < n && i < count; i++)
		CHECK_EQ(out[i], expected[i]);
}

void test_text_utf8(void)
{
	static const u32 pokemon[] = {'P', 'o', 'k', 0xE9, 'm', 'o', 'n'};
	check_latin1("Pok\xC3\xA9mon", pokemon, 7);
	// Latin Extended-A loses its accents; dashes and curly quotes go ASCII
	static const u32 lodz[] = {'L', 0xF3, 'd', 'z', ' ', '-', ' ', '"', 'x', '"', '\''};
	check_latin1("\xC5\x81\xC3\xB3"
		     "d\xC5\xBA \xE2\x80\x94 \xE2\x80\x9Cx\xE2\x80\x9D\xE2\x80\x99",
		     lodz, 11);
	// Cheat names' punctuation: the euro as the currency sign, an ellipsis,
	// a bullet, a thin space, angle quotes
	static const u32 money[] = {0xA4, '5', '.', 0xB7, ' ', '<', '>'};
	check_latin1("\xE2\x82\xAC"
		     "5\xE2\x80\xA6\xE2\x80\xA2\xE2\x80\x89\xE2\x80\xB9\xE2\x80\xBA",
		     money, 7);
	// Other scripts and emoji: one replacement each, whatever their length
	static const u32 other[] = {CHARSET_REPLACEMENT, CHARSET_REPLACEMENT, '.', CHARSET_REPLACEMENT};
	check_latin1("\xE3\x83\x86\xE3\x82\xB9.\xF0\x9F\x98\x80", other, 4);
	// Latin-1 symbols and controls come through as they are
	static const u32 symbols[] = {0xA9, 0xBD, 0x01, 0xA0};
	check_latin1("\xC2\xA9\xC2\xBD\x01\xC2\xA0", symbols, 4);
	// A stray continuation byte draws nothing; a broken sequence one
	// replacement, resyncing at the bad byte
	static const u32 broken[] = {CHARSET_NONE, 't', CHARSET_REPLACEMENT, 'b', CHARSET_REPLACEMENT};
	check_latin1("\xA9t\xE3"
		     "b\xF0\x9F\x98",
		     broken, 5);

	// Glyph indices: Latin-1 its own, anything else the replacement glyph
	u32 pos = 0;
	CHECK_EQ(text_next_glyph(&font_body, "A", 1, &pos), G('A'));
	pos = 0;
	CHECK_EQ(text_next_glyph(&font_body, "\xC3\xBF", 2, &pos), G(0xFF));
	pos = 0;
	CHECK_EQ(text_next_glyph(&font_body, "\x1F", 1, &pos), TORII_FONT_REPLACEMENT);
	pos = 0;
	CHECK_EQ(text_next_glyph(&font_body, "\xE3\x83\x86", 3, &pos), TORII_FONT_REPLACEMENT);
	CHECK_EQ(pos, 3);
	pos = 0;
	CHECK_EQ(text_next_glyph(&font_body, "\x80", 1, &pos), -1);
}

static u32 width(const struct torii_font *font, const char *s)
{
	u32 len = strlen(s);
	char *p = copy(s, len);
	u32 w = text_width(font, p, len);
	free(p);
	return w;
}

void test_text_width(void)
{
	const struct torii_glyph *g = font_body.glyphs;
	CHECK_EQ(width(&font_body, ""), 0);
	CHECK_EQ(width(&font_body, "A"), g[G('A')].advance);
	CHECK_EQ(width(&font_body, "AH a"),
		 g[G('A')].advance + g[G('H')].advance + g[G(' ')].advance + g[G('a')].advance);
	CHECK_EQ(width(&font_body, "\xC3\xA9"), g[G(0xE9)].advance);
	CHECK_EQ(width(&font_body, "\xE3\x83\x86"), g[TORII_FONT_REPLACEMENT].advance);
	CHECK_EQ(width(&font_body, "\x80"
				   "A"),
		 g[G('A')].advance);  // a stray byte adds nothing
	CHECK_EQ(width(&font_heading, "A"), font_heading.glyphs[G('A')].advance);
	CHECK(width(&font_heading, "MENU") > width(&font_body, "MENU"));
	CHECK_EQ(width(&font_body, "ab\xC2\xAD"
				   "cd"),
		 width(&font_body, "abcd"));  // a soft hyphen draws nothing

	// What text_blit and TEXT_MAX_GLYPHS rely on: 1 byte rows up to 8 px and
	// 2 byte rows (at even offsets) above, glyphs no wider than 16 px, and no
	// advance under 2 px
	const struct torii_font *fonts[] = {&font_body, &font_heading};
	for (u32 f = 0; f < 2; f++) {
		CHECK_EQ(fonts[f]->row_bytes, 2);
		CHECK(fonts[f]->max_width <= 16);
		for (u32 i = 0; i < TORII_FONT_GLYPHS; i++) {
			CHECK(fonts[f]->glyphs[i].advance >= 2);
			CHECK(torii_glyph_row_bytes(&fonts[f]->glyphs[i]) == 1 || fonts[f]->glyphs[i].offset % 2 == 0);
			CHECK(fonts[f]->glyphs[i].width <= fonts[f]->max_width);
			CHECK(fonts[f]->glyphs[i].top + fonts[f]->glyphs[i].rows <= fonts[f]->line_height);
		}
	}
}

static void layout(const struct torii_font *font, const char *s, u32 clip, struct text_line *line)
{
	u32 len = strlen(s);
	char *p = copy(s, len);
	CHECK_EQ(text_layout(font, p, len, clip, line), line->count);
	free(p);
}

// Every inked pixel inside [0, clip), and advance and extent as the glyphs say
static void check_line(const struct torii_font *font, const struct text_line *line, u32 clip)
{
	u32 pen = 0, extent = 0;
	CHECK(line->count <= TEXT_MAX_GLYPHS);
	for (u32 i = 0; i < line->count; i++) {
		const struct torii_glyph *g = &font->glyphs[line->glyphs[i]];
		if (g->rows && pen + g->width > extent)
			extent = pen + g->width;
		pen += g->advance;
	}
	CHECK_EQ(line->advance, pen);
	CHECK_EQ(line->extent, extent);
	CHECK(extent <= clip);
	if (line->truncated && line->count) {
		CHECK(line->count >= 3);
		for (u32 i = line->count - 3; i < line->count; i++)
			CHECK_EQ(line->glyphs[i], G('.'));
	}
}

void test_text_layout(void)
{
	struct text_line line;
	const struct torii_glyph *g = font_body.glyphs;
	const struct torii_glyph *dot = &g[G('.')];
	u32 dots = 2 * dot->advance + dot->width;

	// Fits: all of it, just to the last inked pixel
	layout(&font_body, "Hello", 240, &line);
	CHECK_EQ(line.count, 5);
	CHECK(!line.truncated);
	CHECK_EQ(line.advance, width(&font_body, "Hello"));
	u32 extent = line.extent;
	CHECK_EQ(extent, width(&font_body, "Hell") + g[G('o')].width);
	layout(&font_body, "Hello", extent, &line);
	CHECK(!line.truncated);
	CHECK_EQ(line.count, 5);
	// Trailing spaces fit past the last inked pixel: they add no ink
	layout(&font_body, "Hello  ", extent, &line);
	CHECK(!line.truncated);
	CHECK_EQ(line.count, 7);
	CHECK_EQ(line.extent, extent);
	layout(&font_body, "Hello", extent - 1, &line);
	CHECK(line.truncated);
	check_line(&font_body, &line, extent - 1);

	// Cut by character, the space before the cut dropped: "Hello..."
	u32 clip = width(&font_body, "Hello ") + dots;
	layout(&font_body, "Hello World", clip, &line);
	static const u8 hello[] = {G('H'), G('e'), G('l'), G('l'), G('o'), G('.'), G('.'), G('.')};
	CHECK(line.truncated);
	CHECK_EQ(line.count, sizeof(hello));
	for (u32 i = 0; i < sizeof(hello); i++)
		CHECK_EQ(line.glyphs[i], hello[i]);
	CHECK_EQ(line.advance, width(&font_body, "Hello") + 3 * dot->advance);
	check_line(&font_body, &line, clip);
	// One pixel more doesn't fit the "W" yet
	layout(&font_body, "Hello World", clip + 1, &line);
	CHECK_EQ(line.count, sizeof(hello));

	// Accented and other-script characters cut whole
	layout(&font_body, "\xC3\x89t\xC3\xA9 \xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88", 30, &line);
	check_line(&font_body, &line, 30);
	CHECK_EQ(line.glyphs[0], G(0xC9));

	// Only "..." fits; then not even that
	layout(&font_body, "Wonderful", dots, &line);
	CHECK(line.truncated);
	CHECK_EQ(line.count, 3);
	layout(&font_body, "Wonderful", dots - 1, &line);
	CHECK(line.truncated);
	CHECK_EQ(line.count, 0);
	CHECK_EQ(line.advance, 0);
	layout(&font_body, "", 0, &line);
	CHECK(!line.truncated);
	CHECK_EQ(line.count, 0);

	// Every clip: inside it, and whole from the full width
	static const char name[] = "Pok\xC3\xA9mon Mystery Dungeon - Red Rescue Team (Europe).gba";
	const struct torii_font *fonts[] = {&font_body, &font_heading};
	for (u32 f = 0; f < 2; f++) {
		u32 full = width(fonts[f], name);
		for (clip = 0; clip <= full + 2; clip++) {
			layout(fonts[f], name, clip, &line);
			check_line(fonts[f], &line, clip);
			if (clip >= full)
				CHECK(!line.truncated);
		}
	}

	// More glyphs than a line holds: cut at TEXT_MAX_GLYPHS
	char many[201];
	memset(many, 'i', 200);
	many[200] = 0;
	layout(&font_body, many, 1000, &line);
	CHECK(line.truncated);
	CHECK_EQ(line.count, TEXT_MAX_GLYPHS);
	check_line(&font_body, &line, 1000);
}

void test_text_layout_window(void)
{
	const struct torii_font *font = &font_body;
	const char *s = "A long file name that does not fit its row.gba";
	u32 len = (u32)strlen(s), full = text_width(font, s, len);
	struct text_line line, plain;
	u32 lead = 99;
	// From 0 it is text_layout without the ellipsis
	text_layout_window(font, s, len, 0, 100, &line, &lead);
	CHECK_EQ(lead, 0);
	CHECK(line.truncated);
	CHECK(line.extent <= 100);
	text_layout(font, s, len, 1000, &plain);
	// (a kerned pair's left glyph is its variant only when the next one is in the line too)
	for (u32 i = 0; i < line.count; i++)
		CHECK_EQ(torii_kern_base(font, line.glyphs[i]), torii_kern_base(font, plain.glyphs[i]));
	// Every skip: glyphs start at or after skip, ink stays inside the clip
	for (u32 skip = 0; skip < full + 4; skip++) {
		text_layout_window(font, s, len, skip, 100, &line, &lead);
		CHECK(lead + line.extent <= 100);
		if (!line.count)
			continue;
		CHECK(lead < (u32)font->max_width + 2);
		// The first glyph is the first of plain's whose pen is at or past skip
		u32 pen = 0, g = 0;
		for (u32 i = 0; i < plain.count; i++) {
			if (pen >= skip) {
				g = plain.glyphs[i];
				break;
			}
			pen += font->glyphs[plain.glyphs[i]].advance;
		}
		CHECK_EQ(torii_kern_base(font, line.glyphs[0]), torii_kern_base(font, g));
		CHECK_EQ(lead, pen - skip);
	}
	// Past the end: nothing
	text_layout_window(font, s, len, full + 1, 100, &line, &lead);
	CHECK_EQ(line.count, 0);
	CHECK(!line.truncated);
	// A string longer than TEXT_MAX_GLYPHS scrolls to its end
	static char longname[400];
	for (u32 i = 0; i < sizeof(longname) - 1; i++)
		longname[i] = (char)('a' + i % 26);
	u32 llen = sizeof(longname) - 1, lw = text_width(font, longname, llen);
	text_layout_window(font, longname, llen, lw - 50, 100, &line, &lead);
	CHECK(line.count > 0 && !line.truncated);
	CHECK_EQ(torii_kern_base(font, line.glyphs[line.count - 1]), (u32)(longname[llen - 1] - TORII_FONT_FIRST));
}

void test_text_layout_span(void)
{
	const struct torii_font *font = &font_body;
	const char *s = "A long file name that does not fit its row.gba";
	u32 len = (u32)strlen(s), full = text_width(font, s, len);
	struct text_line line, plain;
	s32 lead = 99;
	text_layout(font, s, len, 1000, &plain);
	// Every skip: the glyphs are the run of plain's whose ink meets the span
	for (u32 skip = 0; skip < full + 4; skip++) {
		text_layout_span(font, s, len, skip, 100, &line, &lead);
		CHECK(lead < (s32)font->max_width && lead > -(s32)font->max_width);
		u32 pen = 0, i = 0, first = 0;
		for (; i < plain.count; i++) {
			const struct torii_glyph *g = &font->glyphs[plain.glyphs[i]];
			if (g->rows ? pen + g->width > skip : pen >= skip)
				break;
			pen += g->advance;
		}
		first = i;
		if (first == plain.count) {
			CHECK_EQ(line.count, 0);
			continue;
		}
		CHECK_EQ(lead, (s32)pen - (s32)skip);
		for (u32 j = 0; j < line.count; j++) {
			CHECK_EQ(torii_kern_base(font, line.glyphs[j]), torii_kern_base(font, plain.glyphs[first + j]));
			pen += font->glyphs[plain.glyphs[first + j]].advance;
		}
		// It stops at the first glyph starting past the span, or the end
		CHECK(first + line.count == plain.count || pen >= skip + 100);
		CHECK_EQ(line.truncated, first + line.count < plain.count);
		// The last glyph starts inside the span
		CHECK(lead + (s32)line.advance - (s32)font->glyphs[line.glyphs[line.count - 1]].advance < 100);
	}
	// From 0 it starts at the first glyph, partly visible ones included
	text_layout_span(font, s, len, 0, 20, &line, &lead);
	CHECK_EQ(lead, 0);
	CHECK(line.truncated && line.extent > 20 - (u32)font->max_width);
}

void test_charset_cut(void)
{
	CHECK_EQ(charset_cut("", 4), 0u);
	CHECK_EQ(charset_cut("abc", 4), 3u);
	CHECK_EQ(charset_cut("abcd", 4), 4u);
	CHECK_EQ(charset_cut("abcde", 4), 4u);
	// \xC3\xA9 is one character: not cut between its bytes
	CHECK_EQ(charset_cut("abc\xC3\xA9", 4), 3u);
	CHECK_EQ(charset_cut("abc\xC3\xA9", 5), 5u);
	CHECK_EQ(charset_cut("ab\xE2\x80\x94z", 4), 2u);
	CHECK_EQ(charset_cut("ab\xE2\x80\x94z", 5), 5u);
}

// --- Kerning ---------------------------------------------------------------

// The ink of glyph g as (x, y) pixels in line coordinates
static u32 ink_of(const struct torii_font *font, u32 g, u8 xs[], u8 ys[])
{
	const struct torii_glyph *gl = &font->glyphs[g];
	u32 stride = torii_glyph_row_bytes(gl), n = 0;
	for (u32 r = 0; r < gl->rows; r++) {
		const u8 *row = font->bits + gl->offset + r * stride;
		u32 bits = row[0] | (stride > 1 ? row[1] << 8 : 0);
		for (u32 x = 0; x < 16; x++)
			if (bits >> x & 1) {
				xs[n] = (u8)x;
				ys[n++] = (u8)(gl->top + r);
			}
	}
	return n;
}

void test_text_kern(void)
{
	const struct torii_font *fonts[] = {&font_body, &font_heading};
	for (u32 f = 0; f < 2; f++) {
		const struct torii_font *font = fonts[f];
		CHECK(font->kern_count > 0 && font->kern_count <= TORII_FONT_KERN_MAX);
		u32 pairs = 0;
		for (u32 v = 0; v < font->kern_count; v++) {
			u32 left = font->kern_left[v], index = TORII_FONT_KERN_FIRST + v;
			// A variant is its glyph with an advance one less, and finds its way back
			CHECK_EQ(font->kern_of[left], index);
			CHECK_EQ(torii_kern_base(font, index), left);
			const struct torii_glyph *a = &font->glyphs[left], *b = &font->glyphs[index];
			CHECK(a->offset == b->offset && a->top == b->top && a->rows == b->rows && a->width == b->width);
			CHECK_EQ(b->advance + 1, a->advance);
			for (u32 right = 0; right < TORII_FONT_REPLACEMENT; right++) {
				if (torii_kern(font, left, right) != index)
					continue;
				pairs++;
				// Closer by a pixel, no two inked pixels are within one of each
				// other (the spacing of ordinary neighbours, a blank column)
				u8 lx[200], ly[200], rx[200], ry[200];
				u32 ln = ink_of(font, left, lx, ly), rn = ink_of(font, right, rx, ry);
				CHECK(ln && rn);
				for (u32 i = 0; i < ln; i++)
					for (u32 j = 0; j < rn; j++) {
						int dx = (int)rx[j] + (int)b->advance - (int)lx[i];
						int dy = (int)ry[j] - (int)ly[i];
						CHECK((dx < 0 ? -dx : dx) >= 2 || (dy < 0 ? -dy : dy) >= 2);
					}
			}
			// No glyph past Latin-1's, and no variant, kerns
			CHECK_EQ(torii_kern(font, left, TORII_FONT_REPLACEMENT), 0);
			CHECK_EQ(torii_kern(font, index, G('o')), 0);
		}
		CHECK(pairs > 20);
		CHECK_EQ(torii_kern(font, G('T'), G('o')), font->kern_of[G('T')]);
		CHECK(font->kern_of[G('T')] != 0);
		CHECK_EQ(torii_kern(font, G('T'), G('H')), 0);
		CHECK_EQ(torii_kern(font, G('T'), G(' ')), 0);
		CHECK_EQ(torii_kern(font, G('n'), G('o')), 0);

		// A pair is a pixel narrower, in the width and in the line
		const struct torii_glyph *g = font->glyphs;
		CHECK_EQ(width(font, "To"), g[G('T')].advance + g[G('o')].advance - 1);
		CHECK_EQ(width(font, "TH"), g[G('T')].advance + g[G('H')].advance);
		struct text_line line;
		layout(font, "To", 100, &line);
		CHECK_EQ(line.count, 2);
		CHECK_EQ(line.glyphs[0], font->kern_of[G('T')]);
		CHECK_EQ(line.glyphs[1], G('o'));
		CHECK_EQ(line.advance, width(font, "To"));
		// Each pair kerns on its own: "ToTo" is one pixel a pair narrower
		CHECK_EQ(width(font, "ToTo"),
			 width(font, "To") * 2 - 0 - (u32)(torii_kern(font, G('o'), G('T')) ? 1 : 0));
	}
}

// The pairs of a long string in every window, clip and cut: the line's glyphs
// always sum to its advance, and the ellipsis follows a glyph that kerns with it
void test_text_kern_cuts(void)
{
	static const char s[] = "To Ty AV LT Yo Te. Wa, ToTaTeTo.Tr Ty.TA LYTr.Pa.Fa,Ry.Pe";
	const struct torii_font *fonts[] = {&font_body, &font_heading};
	for (u32 f = 0; f < 2; f++) {
		const struct torii_font *font = fonts[f];
		u32 len = sizeof(s) - 1, full = text_width(font, s, len);
		struct text_line line, plain;
		text_layout(font, s, len, 2000, &plain);
		check_line(font, &plain, 2000);
		CHECK_EQ(plain.advance, full);
		for (u32 clip = 0; clip <= full + 2; clip++) {
			layout(font, s, clip, &line);
			check_line(font, &line, clip);
			if (line.truncated && line.count >= 4) {
				// "..." follows: the glyph before it never kerns for a
				// glyph that was cut, only for the dots
				CHECK_EQ(line.glyphs[line.count - 1], G('.'));
				u32 last = torii_kern_base(font, line.glyphs[line.count - 4]);
				CHECK_EQ(line.glyphs[line.count - 4],
					 torii_kern(font, last, G('.')) ? font->kern_of[last] : last);
			}
		}
		u32 lead;
		s32 slead;
		for (u32 skip = 0; skip < full + 2; skip += 3) {
			text_layout_window(font, s, len, skip, 90, &line, &lead);
			u32 pen = lead;
			for (u32 i = 0; i < line.count; i++)
				pen += font->glyphs[line.glyphs[i]].advance;
			CHECK(!line.count || pen - lead == line.advance);
			text_layout_span(font, s, len, skip, 90, &line, &slead);
			CHECK(!line.count || (s32)line.advance + slead >= 0);
		}
	}
}

// --- Script fonts ------------------------------------------------------------

static void put16(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
}

static void put32(u8 *p, u32 v)
{
	put16(p, v);
	put16(p + 2, v >> 16);
}

// A blob (script.h) of one font: U+0141, U+0391-0392 and U+0414, 4 glyphs
static u32 make_blob(u8 *b, u32 fonts)
{
	memset(b, 0, 256);
	memcpy(b, "TSCF", 4);
	put16(b + 4, 1);
	put16(b + 6, fonts);
	u8 *rec = b + 24;
	put16(rec, 3);  // ranges
	put16(rec + 2, 4);  // glyphs
	put32(rec + 4, 32);  // glyph table
	put32(rec + 8, 56);  // bitmaps
	put16(rec + 12, 0x141), put16(rec + 14, 0x141), put16(rec + 16, 0);
	put16(rec + 18, 0x391), put16(rec + 20, 0x392), put16(rec + 22, 1);
	put16(rec + 24, 0x414), put16(rec + 26, 0x414), put16(rec + 28, 3);
	// offset (in 4 byte units), advance, top, rows, width
	static const u8 table[4][5] = {{0, 6, 2, 2, 5}, {1, 7, 3, 2, 5}, {2, 7, 3, 2, 5}, {3, 11, 2, 2, 9}};
	for (u32 i = 0; i < 4; i++) {
		u8 *e = rec + 32 + 6 * i;
		put16(e, table[i][0]);
		memcpy(e + 2, table[i] + 1, 4);
	}
	rec[56] = 0x1F, rec[57] = 0x11;  // glyph 0: 1 byte rows
	rec[60] = 0x0E, rec[61] = 0x0E;
	rec[64] = 0x1F, rec[65] = 0x0A;
	put16(rec + 68, 0x101), put16(rec + 70, 0x1FF);  // glyph 3: 2 byte rows
	u32 bytes = 24 + 72;
	put32(b + 12, bytes);
	put32(b + 16, 24);
	put32(b + 20, fonts == 2 ? 24 : 0);
	return bytes;
}

void test_text_script(void)
{
	static const struct torii_font other_font = {0};
	static u8 blob[256] ALIGN(4);
	u32 bytes = make_blob(blob, 1);
	u32 pos = 0;

	// No script font: Latin Extended-A folds, other scripts draw the box
	CHECK(script_of(&font_body) == NULL);
	CHECK_EQ(text_next_glyph(&font_body, "\xC5\x81", 2, &pos), G('L'));
	pos = 0;
	CHECK_EQ(text_next_glyph(&font_body, "\xCE\x91", 2, &pos), TORII_FONT_REPLACEMENT);
	CHECK_EQ(script_use(blob, bytes - 4), 0);

	CHECK_EQ(script_use(blob, bytes), 1);
	const struct torii_script *sc = script_of(&font_body);
	CHECK(sc != NULL && script_of(&font_heading) != NULL && script_of(&other_font) == NULL);
	CHECK_EQ(sc->dy, 0);
	CHECK_EQ(script_of(&font_heading)->dy, (int)font_heading.baseline - (int)font_body.baseline);
	CHECK_EQ(script_of(&font_heading)->glyphs, sc->glyphs);
	// Alpha, beta, a Cyrillic De, the Polish L (the font's own, not the fold), and a Cyrillic letter it lacks
	CHECK_EQ(script_glyph(sc, 0x391), TORII_FONT_SCRIPT + 1);
	CHECK_EQ(script_glyph(sc, 0x392), TORII_FONT_SCRIPT + 2);
	CHECK_EQ(script_glyph(sc, 0x414), TORII_FONT_SCRIPT + 3);
	CHECK_EQ(script_glyph(sc, 0x141), TORII_FONT_SCRIPT);
	CHECK_EQ(script_glyph(sc, 0x393), -1);
	CHECK_EQ(script_glyph(sc, 0x413), -1);
	CHECK_EQ(script_glyph(sc, 0x140), -1);
	CHECK_EQ(script_glyph(sc, 0x10414), -1);
	pos = 0;
	CHECK_EQ(text_next_glyph(&font_body, "\xCE\x92", 2, &pos), (int)TORII_FONT_SCRIPT + 2);
	pos = 0;
	CHECK_EQ(text_next_glyph(&font_body, "\xC5\x81", 2, &pos), (int)TORII_FONT_SCRIPT);
	pos = 0;
	CHECK_EQ(text_next_glyph(&font_body, "\xC5\x82", 2, &pos), G('l'));  // not in it: folded
	pos = 0;
	CHECK_EQ(text_next_glyph(&font_body, "\xCE\x93", 2, &pos), TORII_FONT_REPLACEMENT);
	CHECK_EQ(torii_font_glyph(&font_body, TORII_FONT_SCRIPT + 3)->advance, 11);
	CHECK_EQ(torii_font_glyph(&font_body, G('A'))->advance, font_body.glyphs[G('A')].advance);
	CHECK_EQ(torii_font_glyph(&font_body, TORII_FONT_SCRIPT + 50), &font_body.glyphs[TORII_FONT_REPLACEMENT]);

	// A line of them: widths, and cuts that count the script's advances
	const char *s = "A\xCE\x91\xCE\x92\xD0\x94z";
	u32 len = (u32)strlen(s);
	CHECK_EQ(text_width(&font_body, s, len),
		 font_body.glyphs[G('A')].advance + 7 + 7 + 11 + font_body.glyphs[G('z')].advance);
	struct text_line line;
	layout(&font_body, s, 1000, &line);
	CHECK_EQ(line.count, 5);
	CHECK_EQ(line.glyphs[1], TORII_FONT_SCRIPT + 1);
	CHECK_EQ(line.glyphs[3], TORII_FONT_SCRIPT + 3);
	CHECK_EQ(line.advance, text_width(&font_body, s, len));
	for (u32 clip = 0; clip < 70; clip++) {
		layout(&font_body, s, clip, &line);
		u32 pen = 0, extent = 0;
		for (u32 i = 0; i < line.count; i++) {
			const struct torii_glyph *g = torii_font_glyph(&font_body, line.glyphs[i]);
			if (g->rows && pen + g->width > extent)
				extent = pen + g->width;
			pen += g->advance;
		}
		CHECK_EQ(pen, line.advance);
		CHECK(extent <= clip);
	}
	u32 lead;
	text_layout_window(&font_body, s, len, 9, 40, &line, &lead);
	CHECK(line.count > 0);

	// A blob that isn't one changes nothing: each field wrong in turn
	static u8 bad[256] ALIGN(4);
	static const struct {
		u32 at, size, value;
	} spots[] = {
		{0, 1, 'X'},  // magic
		{4, 2, 2},    // version
		{6, 2, 3},    // fonts
		{12, 4, 92},  // size
		{12, 4, 99},
		{16, 4, 0},   // the body font's record
		{16, 4, 20},
		{24, 2, 0},  // ranges, glyphs
		{26, 2, 0},
		{26, 2, 3},
		{28, 4, 100},  // the table's, the bitmaps'
		{28, 4, 34},
		{32, 4, 200},
		{32, 4, 40},
		{36, 2, 0xFF},  // a range: first (not past Latin-1), end, base
		{38, 2, 0x140},
		{40, 2, 1},
		{42, 2, 0x141},  // not sorted
		{56 + 12, 2, 0x300},  // a glyph's bitmap past the end
		{56 + 12 + 4, 1, 40},  // rows
		{56 + 12 + 5, 1, 20},  // width
	};
	for (u32 i = 0; i < sizeof(spots) / sizeof(spots[0]); i++) {
		memcpy(bad, blob, sizeof(blob));
		u32 at = spots[i].at;
		if (spots[i].size == 4)
			put32(bad + at, spots[i].value);
		else if (spots[i].size == 2)
			put16(bad + at, spots[i].value);
		else
			bad[at] = (u8)spots[i].value;
		CHECK_EQ(script_use(bad, bytes), 0);
		CHECK_EQ(script_of(&font_body)->glyph_count, 4);  // the good one is still in use
	}
	// The heading's own record, when the blob has one
	u32 two = make_blob(blob, 2);
	CHECK_EQ(script_use(blob, two), 1);
	CHECK_EQ(script_of(&font_heading)->dy, 0);

	script_drop();
	CHECK(script_of(&font_body) == NULL);
	pos = 0;
	CHECK_EQ(text_next_glyph(&font_body, "\xCE\x92", 2, &pos), TORII_FONT_REPLACEMENT);
	CHECK_EQ(text_width(&font_body, s, len), font_body.glyphs[G('A')].advance +
							 3 * font_body.glyphs[TORII_FONT_REPLACEMENT].advance +
							 font_body.glyphs[G('z')].advance);
}

void test_charset_raw(void)
{
	u32 pos = 0;
	CHECK_EQ(charset_next("\xE3\x83\x86", 3, &pos), 0x30C6);
	CHECK_EQ(pos, 3);
	pos = 0;
	CHECK_EQ(charset_next("\xF0\x9F\x98\x80", 4, &pos), 0x1F600);
	pos = 0;
	CHECK_EQ(charset_next("\xC5\x81", 2, &pos), 0x141);  // not folded
	pos = 0;
	CHECK_EQ(charset_next("\xC2\xAD", 2, &pos), CHARSET_NONE);
	pos = 0;
	CHECK_EQ(charset_next("\xE3\x83", 2, &pos), CHARSET_REPLACEMENT);
	CHECK_EQ(charset_fold_latin1(0x141), 'L');
	CHECK_EQ(charset_fold_latin1(0x30C6), CHARSET_REPLACEMENT);
	CHECK_EQ(charset_fold_latin1(0x2014), '-');
	CHECK_EQ(charset_fold_latin1(0xE9), 0xE9);
}
