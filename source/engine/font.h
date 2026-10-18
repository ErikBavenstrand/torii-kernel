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

struct torii_glyph {
	uint16_t offset;  // into bits[], in bytes
	uint8_t advance;  // pen advance in pixels
	uint8_t top;      // first bitmap row, from the line's top
	uint8_t rows;     // bitmap rows (0 for blank glyphs)
	uint8_t width;    // inked columns from the pen, at most 8 * row_bytes
};

struct torii_font {
	const struct torii_glyph *glyphs;  // TORII_FONT_GLYPHS entries
	const uint8_t *bits;               // rows of row_bytes bytes, bit 0 leftmost
	uint8_t line_height;               // pixels from one line's top to the next
	uint8_t baseline;                  // rows above the baseline
	uint8_t row_bytes;                 // 1, 2 or 4
	uint8_t max_width;                 // widest glyph's inked columns
};

// Galmuri9, body text on a 12 px line, and Galmuri11 Bold, headings on a
// 16 px line (assets/fonts/galmuri, OFL-1.1)
extern const struct torii_font font_body;
extern const struct torii_font font_heading;

#endif
