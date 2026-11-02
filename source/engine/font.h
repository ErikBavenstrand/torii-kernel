// Proportional 1bpp fonts, converted from BDF by tools/font.py (`just font`),
// whose docstring describes the format. The menu's two fonts are generated
// into font_body.c and font_heading.c next to this header and committed;
// tests/font_test.py checks they match their BDFs in assets/fonts.
#ifndef TORII_FONT_H
#define TORII_FONT_H

#include <stdint.h>

#define TORII_FONT_FIRST 0x20
#define TORII_FONT_LAST 0xFF
// glyphs[] index for code points outside U+0020-U+00FF
#define TORII_FONT_REPLACEMENT (TORII_FONT_LAST - TORII_FONT_FIRST + 1)
#define TORII_FONT_GLYPHS (TORII_FONT_REPLACEMENT + 1)
// glyphs[] holds kerned variants of some glyphs from here to the 256th entry
// (see torii_kern); a glyph index from TORII_FONT_SCRIPT on is one of the
// pack's script font (script.h), not in glyphs[] at all
#define TORII_FONT_KERN_FIRST TORII_FONT_GLYPHS
#define TORII_FONT_KERN_MAX (256 - TORII_FONT_KERN_FIRST)
#define TORII_FONT_SCRIPT 256u
// Bytes of a bitmap over the glyphs a kerned glyph may be followed by
#define TORII_FONT_RIGHT_BYTES ((TORII_FONT_REPLACEMENT + 7) / 8)

struct torii_glyph {
	uint16_t offset;  // into bits[], in bytes
	uint8_t advance;  // pen advance in pixels
	uint8_t top;      // first bitmap row, from the line's top
	uint8_t rows;     // bitmap rows (0 for blank glyphs)
	uint8_t width;    // inked columns from the pen, at most 16
};

// A glyph's rows are 1 byte when it is 8 px wide or less, else 2 (at an even
// offset): rows narrower than the font's widest are no bigger than they need to be
static inline uint32_t torii_glyph_row_bytes(const struct torii_glyph *g)
{
	return g->width > 8 ? 2 : 1;
}

struct torii_font {
	const struct torii_glyph *glyphs;  // TORII_FONT_GLYPHS entries, then kern_count variants
	const uint8_t *bits;               // rows of torii_glyph_row_bytes() bytes, bit 0 leftmost
	uint8_t line_height;               // pixels from one line's top to the next
	uint8_t baseline;                  // rows above the baseline
	uint8_t row_bytes;                 // the widest glyph's row: 1 or 2
	uint8_t max_width;                 // widest glyph's inked columns
	uint8_t kern_count;                // kerned variants in glyphs[TORII_FONT_KERN_FIRST...]
	const uint8_t *kern_of;            // TORII_FONT_GLYPHS entries: a glyph's variant, 0 for none
	const uint8_t *kern_left;          // per variant: the glyph it is a copy of
	const uint8_t (*kern_right)[TORII_FONT_RIGHT_BYTES];  // per variant: a bitmap of the glyphs it kerns before
};

// Kerning: a pair of glyphs that sit a pixel closer than their advances say
// (a T before an o). The font has, for each left glyph that kerns, a variant
// of it, the same bitmap with an advance one less; a layout that sees the
// pair swaps the left glyph for its variant, so the blitter and every sum of
// advances need nothing more. Returns the variant of glyph left (a glyph as
// text_next_glyph gave it, never a variant) when right follows it closely, else 0.
static inline uint32_t torii_kern(const struct torii_font *font, uint32_t left, uint32_t right)
{
	if (!font->kern_of || left >= TORII_FONT_REPLACEMENT || right >= TORII_FONT_REPLACEMENT)
		return 0;
	uint32_t v = font->kern_of[left];
	if (v && (font->kern_right[v - TORII_FONT_KERN_FIRST][right >> 3] >> (right & 7) & 1))
		return v;
	return 0;
}

// The glyph a kerned variant copies; any other glyph as it is
static inline uint32_t torii_kern_base(const struct torii_font *font, uint32_t glyph)
{
	if (glyph >= TORII_FONT_KERN_FIRST && glyph < TORII_FONT_SCRIPT)
		return font->kern_left[glyph - TORII_FONT_KERN_FIRST];
	return glyph;
}

// Galmuri9, body text on a 12 px line, and Galmuri11 Bold, headings on a
// 16 px line (assets/fonts/galmuri, OFL-1.1)
extern const struct torii_font font_body;
extern const struct torii_font font_heading;

#endif
