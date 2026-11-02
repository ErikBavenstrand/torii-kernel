// Measuring and cutting text (text.h). Hardware-free, so tests/host runs it.
#include "text.h"

#include "../charset.h"

#define ELLIPSIS_DOTS 3
// The glyph before the first one: nothing to kern with
#define NO_GLYPH TORII_FONT_REPLACEMENT

int text_next_glyph(const struct torii_font *font, const char *s, u32 len, u32 *pos)
{
	u32 c = charset_next(s, len, pos);
	if (c == CHARSET_NONE)
		return -1;
	if (c >= TORII_FONT_FIRST && c <= TORII_FONT_LAST)
		return (int)(c - TORII_FONT_FIRST);
	if (c >= 0x100) {
		const struct torii_script *sc = script_of(font);
		if (sc) {
			int g = script_glyph(sc, c);
			if (g >= 0)
				return g;
		}
		c = charset_fold_latin1(c);
		if (c >= TORII_FONT_FIRST && c <= TORII_FONT_LAST)
			return (int)(c - TORII_FONT_FIRST);
	}
	return TORII_FONT_REPLACEMENT;
}

// text_next_glyph with printable ASCII inline: layout runs per character,
// and two calls from ROM cost more than the rest of the loop
static inline __attribute__((always_inline)) int next_glyph(const struct torii_font *font, const char *s, u32 len,
							    u32 *pos)
{
	u8 c = (u8)s[*pos];
	if (c >= TORII_FONT_FIRST && c < 0x80) {
		(*pos)++;
		return c - TORII_FONT_FIRST;
	}
	return text_next_glyph(font, s, len, pos);
}

// What a layout loop knows of kerning, hoisted out of it: the loop's pairs cost
// a table read each (torii_kern in font.h, as a layout needs it)
struct kerning {
	const struct torii_font *font;
	const uint8_t *of;  // the font's kern_of, or a table with no variants
	uint32_t prev;  // the glyph before, as a kern_of index: TORII_FONT_REPLACEMENT for none or a script glyph
};

static const uint8_t no_kern[TORII_FONT_GLYPHS];

static inline void kerning_start(struct kerning *k, const struct torii_font *font)
{
	k->font = font;
	k->of = font->kern_of ? font->kern_of : no_kern;
	k->prev = NO_GLYPH;
}

// The variant of the glyph before g if g follows it closely, else 0; g is the next glyph
static inline __attribute__((always_inline)) uint32_t kern_next(struct kerning *k, uint32_t g)
{
	uint32_t v = k->of[k->prev];
	k->prev = g < TORII_FONT_REPLACEMENT ? g : NO_GLYPH;
	if (__builtin_expect(v != 0, 0) && g < TORII_FONT_REPLACEMENT &&
	    (k->font->kern_right[v - TORII_FONT_KERN_FIRST][g >> 3] >> (g & 7) & 1))
		return v;
	return 0;
}

// Glyph g's entry, in a font with script glyphs sc (NULL: none)
static inline __attribute__((always_inline)) const struct torii_glyph *entry(const struct torii_font *font,
									     const struct torii_script *sc, u32 g)
{
	if (g < TORII_FONT_SCRIPT)
		return &font->glyphs[g];
	if (!sc)  // a layout makes script glyphs only with a script: here for a stale line
		return &font->glyphs[TORII_FONT_REPLACEMENT];
	return &sc->glyphs[g - TORII_FONT_SCRIPT];
}

u32 text_width(const struct torii_font *font, const char *s, u32 len)
{
	const struct torii_script *sc = script_of(font);
	u32 width = 0, pos = 0;
	struct kerning k;
	kerning_start(&k, font);
	while (pos < len) {
		int g = next_glyph(font, s, len, &pos);
		if (g < 0)
			continue;
		width += entry(font, sc, (u32)g)->advance;
		if (kern_next(&k, (u32)g))
			width--;
	}
	return width;
}

u32 text_layout(const struct torii_font *font, const char *s, u32 len, u32 clip, struct text_line *line)
{
	const struct torii_glyph *glyphs = font->glyphs;
	const struct torii_script *sc = script_of(font);
	const struct torii_glyph *dot = &glyphs['.' - TORII_FONT_FIRST];
	// "..." drawn at the pen ends its ink here
	u32 dots_ink = (ELLIPSIS_DOTS - 1) * dot->advance + dot->width;
	u32 pen = 0, pos = 0, count = 0, extent = 0;
	u32 cut = 0, cut_pen = 0, cut_extent = 0;  // the longest prefix that leaves room for "..."
	int fits = dots_ink <= clip;
	struct kerning k;
	kerning_start(&k, font);

	line->truncated = 0;
	while (pos < len) {
		int g = next_glyph(font, s, len, &pos);
		if (g < 0)
			continue;
		const struct torii_glyph *glyph = entry(font, sc, (u32)g);
		// A kerned pair sits a pixel closer: the left glyph becomes its variant
		u32 kern = kern_next(&k, (u32)g);
		u32 at = pen - (kern ? 1 : 0);
		// A blank glyph (a space) always fits: it has no ink to clip
		if (count == TEXT_MAX_GLYPHS || (glyph->rows && at + glyph->width > clip)) {
			line->truncated = 1;
			break;
		}
		if (kern && count) {
			line->glyphs[count - 1] = (u16)kern;
			pen = at;
		}
		line->glyphs[count++] = (u16)g;
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
			while (cut && !entry(font, sc, line->glyphs[cut - 1])->rows)
				cut_pen -= entry(font, sc, line->glyphs[--cut])->advance;
			count = cut;
			pen = cut_pen;
			extent = cut_extent;
			// The last glyph kept was kerned for the one that was cut: it
			// is kerned for the dots instead, if they kern with it
			u32 last = NO_GLYPH;
			if (count) {
				last = torii_kern_base(font, line->glyphs[count - 1]);
				line->glyphs[count - 1] = (u16)last;
			}
			u32 kern = torii_kern(font, last, '.' - TORII_FONT_FIRST);
			if (kern && count) {
				line->glyphs[count - 1] = (u16)kern;
				pen--;
			}
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
	const struct torii_script *sc = script_of(font);
	u32 pen = 0, pos = 0, count = 0, extent = 0, start = 0;
	struct kerning k;
	kerning_start(&k, font);
	line->truncated = 0;
	*lead = 0;
	while (pos < len) {
		int g = next_glyph(font, s, len, &pos);
		if (g < 0)
			continue;
		const struct torii_glyph *glyph = entry(font, sc, (u32)g);
		// The pair kerns even when its left glyph is out of the window
		u32 kern = kern_next(&k, (u32)g);
		u32 at = pen - (kern ? 1 : 0);
		if (at < skip) {
			pen = at + glyph->advance;
			continue;
		}
		if (!count) {
			start = at;
			*lead = at - skip;
		}
		u32 x = at - start;  // from the first glyph's pen
		if (count == TEXT_MAX_GLYPHS || (glyph->rows && *lead + x + glyph->width > clip)) {
			line->truncated = 1;
			break;
		}
		if (kern && count)
			line->glyphs[count - 1] = (u16)kern;
		line->glyphs[count++] = (u16)g;
		if (glyph->rows && x + glyph->width > extent)
			extent = x + glyph->width;
		pen = at + glyph->advance;
	}
	line->count = count;
	line->advance = count ? pen - start : 0;
	line->extent = extent;
	return count;
}

u32 text_layout_span(const struct torii_font *font, const char *s, u32 len, u32 skip, u32 clip, struct text_line *line,
		     s32 *lead)
{
	const struct torii_script *sc = script_of(font);
	u32 pen = 0, pos = 0, count = 0, extent = 0, start = 0, end = skip + clip;
	struct kerning k;
	kerning_start(&k, font);
	line->truncated = 0;
	*lead = 0;
	while (pos < len) {
		int g = next_glyph(font, s, len, &pos);
		if (g < 0)
			continue;
		const struct torii_glyph *glyph = entry(font, sc, (u32)g);
		u32 kern = kern_next(&k, (u32)g);
		u32 at = pen - (kern ? 1 : 0);
		// Ink wholly left of the span (a blank glyph has none: it goes
		// unless it starts inside)
		if (!count && (glyph->rows ? at + glyph->width <= skip : at < skip)) {
			pen = at + glyph->advance;
			continue;
		}
		if (at >= end || count == TEXT_MAX_GLYPHS) {
			line->truncated = 1;
			break;
		}
		if (!count) {
			start = at;
			*lead = (s32)at - (s32)skip;
		}
		u32 x = at - start;  // from the first glyph's pen
		if (kern && count)
			line->glyphs[count - 1] = (u16)kern;
		line->glyphs[count++] = (u16)g;
		if (glyph->rows && x + glyph->width > extent)
			extent = x + glyph->width;
		pen = at + glyph->advance;
	}
	line->count = count;
	line->advance = count ? pen - start : 0;
	line->extent = extent;
	return count;
}
