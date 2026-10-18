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

u32 text_width(const struct torii_font *font, const char *s, u32 len)
{
	u32 width = 0, pos = 0;
	while (pos < len) {
		int g = text_next_glyph(s, len, &pos);
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
		int g = text_next_glyph(s, len, &pos);
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
