// Measuring and cutting text (text.h). Hardware-free, so tests/host runs it.
#include "text.h"

#include "../charset.h"

#define ELLIPSIS_DOTS 3

int text_next_glyph(const char *s, u32 len, u32 *pos)
{
	u32 c = charset_next_latin1(s, len, pos);
	if (c == CHARSET_NONE)
		return -1;
	if (c >= TORII_FONT_FIRST && c <= TORII_FONT_LAST)
		return (int)(c - TORII_FONT_FIRST);
	return TORII_FONT_REPLACEMENT;
}

// text_next_glyph with printable ASCII inline: layout runs per character,
// and two calls from ROM cost more than the rest of the loop
static inline __attribute__((always_inline)) int next_glyph(const char *s, u32 len, u32 *pos)
{
	u8 c = (u8)s[*pos];
	if (c >= TORII_FONT_FIRST && c < 0x80) {
		(*pos)++;
		return c - TORII_FONT_FIRST;
	}
	return text_next_glyph(s, len, pos);
}

u32 text_width(const struct torii_font *font, const char *s, u32 len)
{
	u32 width = 0, pos = 0;
	while (pos < len) {
		int g = next_glyph(s, len, &pos);
		if (g >= 0)
			width += font->glyphs[g].advance;
	}
	return width;
}

u32 text_layout(const struct torii_font *font, const char *s, u32 len, u32 clip, struct text_line *line)
{
	const struct torii_glyph *glyphs = font->glyphs;
	const struct torii_glyph *dot = &glyphs['.' - TORII_FONT_FIRST];
	// "..." drawn at the pen ends its ink here
	u32 dots_ink = (ELLIPSIS_DOTS - 1) * dot->advance + dot->width;
	u32 pen = 0, pos = 0, count = 0, extent = 0;
	u32 cut = 0, cut_pen = 0, cut_extent = 0;  // the longest prefix that leaves room for "..."
	int fits = dots_ink <= clip;

	line->truncated = 0;
	while (pos < len) {
		int g = next_glyph(s, len, &pos);
		if (g < 0)
			continue;
		const struct torii_glyph *glyph = &glyphs[g];
		// A blank glyph (a space) always fits: it has no ink to clip
		if (count == TEXT_MAX_GLYPHS || (glyph->rows && pen + glyph->width > clip)) {
			line->truncated = 1;
			break;
		}
		line->glyphs[count++] = g;
		if (glyph->rows && pen + glyph->width > extent)
			extent = pen + glyph->width;
		pen += glyph->advance;
		if (fits && pen + dots_ink <= clip && count + ELLIPSIS_DOTS <= TEXT_MAX_GLYPHS) {
			cut = count;
			cut_pen = pen;
			cut_extent = extent;
		}
	}
	if (line->truncated) {
		if (!fits) {
			count = pen = extent = 0;
		} else {
			// Blank glyphs (spaces) before the dots go; they add no ink
			while (cut && !glyphs[line->glyphs[cut - 1]].rows)
				cut_pen -= glyphs[line->glyphs[--cut]].advance;
			count = cut;
			pen = cut_pen;
			extent = cut_extent;
			for (u32 i = 0; i < ELLIPSIS_DOTS; i++) {
				line->glyphs[count++] = '.' - TORII_FONT_FIRST;
				extent = pen + dot->width;
				pen += dot->advance;
			}
		}
	}
	line->count = count;
	line->advance = pen;
	line->extent = extent;
	return count;
}

u32 text_layout_window(const struct torii_font *font, const char *s, u32 len, u32 skip, u32 clip,
		       struct text_line *line, u32 *lead)
{
	const struct torii_glyph *glyphs = font->glyphs;
	u32 pen = 0, pos = 0, count = 0, extent = 0, start = 0;
	line->truncated = 0;
	*lead = 0;
	while (pos < len) {
		int g = next_glyph(s, len, &pos);
		if (g < 0)
			continue;
		const struct torii_glyph *glyph = &glyphs[g];
		if (pen < skip) {
			pen += glyph->advance;
			continue;
		}
		if (!count) {
			start = pen;
			*lead = pen - skip;
		}
		u32 x = pen - start;  // from the first glyph's pen
		if (count == TEXT_MAX_GLYPHS || (glyph->rows && *lead + x + glyph->width > clip)) {
			line->truncated = 1;
			break;
		}
		line->glyphs[count++] = (u8)g;
		if (glyph->rows && x + glyph->width > extent)
			extent = x + glyph->width;
		pen += glyph->advance;
	}
	line->count = count;
	line->advance = count ? pen - start : 0;
	line->extent = extent;
	return count;
}

u32 text_layout_span(const struct torii_font *font, const char *s, u32 len, u32 skip, u32 clip, struct text_line *line,
		     s32 *lead)
{
	const struct torii_glyph *glyphs = font->glyphs;
	u32 pen = 0, pos = 0, count = 0, extent = 0, start = 0, end = skip + clip;
	line->truncated = 0;
	*lead = 0;
	while (pos < len) {
		int g = next_glyph(s, len, &pos);
		if (g < 0)
			continue;
		const struct torii_glyph *glyph = &glyphs[g];
		// Ink wholly left of the span (a blank glyph has none: it goes
		// unless it starts inside)
		if (!count && (glyph->rows ? pen + glyph->width <= skip : pen < skip)) {
			pen += glyph->advance;
			continue;
		}
		if (pen >= end || count == TEXT_MAX_GLYPHS) {
			line->truncated = 1;
			break;
		}
		if (!count) {
			start = pen;
			*lead = (s32)pen - (s32)skip;
		}
		u32 x = pen - start;  // from the first glyph's pen
		line->glyphs[count++] = (u8)g;
		if (glyph->rows && x + glyph->width > extent)
			extent = x + glyph->width;
		pen += glyph->advance;
	}
	line->count = count;
	line->advance = count ? pen - start : 0;
	line->extent = extent;
	return count;
}
