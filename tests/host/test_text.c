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
	CHECK_EQ(text_next_glyph("A", 1, &pos), G('A'));
	pos = 0;
	CHECK_EQ(text_next_glyph("\xC3\xBF", 2, &pos), G(0xFF));
	pos = 0;
	CHECK_EQ(text_next_glyph("\x1F", 1, &pos), TORII_FONT_REPLACEMENT);
	pos = 0;
	CHECK_EQ(text_next_glyph("\xE3\x83\x86", 3, &pos), TORII_FONT_REPLACEMENT);
	CHECK_EQ(pos, 3);
	pos = 0;
	CHECK_EQ(text_next_glyph("\x80", 1, &pos), -1);
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
	CHECK_EQ(width(&font_body, "AV a"),
		 g[G('A')].advance + g[G('V')].advance + g[G(' ')].advance + g[G('a')].advance);
	CHECK_EQ(width(&font_body, "\xC3\xA9"), g[G(0xE9)].advance);
	CHECK_EQ(width(&font_body, "\xE3\x83\x86"), g[TORII_FONT_REPLACEMENT].advance);
	CHECK_EQ(width(&font_body, "\x80"
				   "A"),
		 g[G('A')].advance);  // a stray byte adds nothing
	CHECK_EQ(width(&font_heading, "A"), font_heading.glyphs[G('A')].advance);
	CHECK(width(&font_heading, "MENU") > width(&font_body, "MENU"));

	// What text_blit and TEXT_MAX_GLYPHS rely on: 2 byte rows, glyphs no
	// wider than 16 px, and no advance under 2 px
	const struct torii_font *fonts[] = {&font_body, &font_heading};
	for (u32 f = 0; f < 2; f++) {
		CHECK_EQ(fonts[f]->row_bytes, 2);
		CHECK(fonts[f]->max_width <= 16);
		for (u32 i = 0; i < TORII_FONT_GLYPHS; i++) {
			CHECK(fonts[f]->glyphs[i].advance >= 2);
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
	CHECK(memcmp(line.glyphs, hello, sizeof(hello)) == 0);
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
	for (u32 i = 0; i < line.count; i++)
		CHECK_EQ(line.glyphs[i], plain.glyphs[i]);
	// Every skip: glyphs start at or after skip, ink stays inside the clip
	for (u32 skip = 0; skip < full + 4; skip++) {
		text_layout_window(font, s, len, skip, 100, &line, &lead);
		CHECK(lead + line.extent <= 100);
		if (!line.count)
			continue;
		CHECK(lead < (u32)font->max_width + 2);
		// The first glyph is the first whose pen is at or past skip
		u32 pen = 0, pos = 0, g = 0;
		for (;;) {
			int gi = text_next_glyph(s, len, &pos);
			if (pen >= skip) {
				g = (u32)gi;
				break;
			}
			pen += font->glyphs[gi].advance;
		}
		CHECK_EQ(line.glyphs[0], g);
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
	CHECK_EQ(line.glyphs[line.count - 1], (u32)(longname[llen - 1] - TORII_FONT_FIRST));
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
			CHECK_EQ(line.glyphs[j], plain.glyphs[first + j]);
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
