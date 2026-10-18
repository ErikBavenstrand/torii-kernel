// Text in mode 0 BG tiles: UTF-8 strings drawn in a proportional font
// (font.h) straight into a window's 4bpp tiles, in palette-index colours.
//
// A window is a rectangle of tiles in column-major order: tile (tx, ty) is
// tiles[tx * height + ty], so each tile column is one run of height * 8
// words, a word per pixel row (low nibble leftmost). Its map shows tile
// tx * height + ty at column tx, row ty. The tiles may be in VRAM, shown or
// hidden, or in RAM: nothing here touches a register or the map.
//
// Text is drawn with a transparent background: only inked pixels change, to
// the ink index, so a panel or selection under the text shows through.
// Clear a rectangle first (text_clear) to replace old text.
#ifndef TORII_TEXT_H
#define TORII_TEXT_H

#include <gba_base.h>

#include "font.h"

struct text_window {
	u32 *tiles;  // first tile, word aligned (VRAM or RAM)
	u8 width;    // in tiles
	u8 height;   // in tiles
};

// Most glyphs one line holds: a 256 px line of 2 px glyphs (fonts here have
// no narrower advance); text_layout stops there.
#define TEXT_MAX_GLYPHS 128

// The glyph a UTF-8 string draws next: reads the character at s[*pos] and
// moves *pos past it (charset_next_latin1). Returns its index in
// font->glyphs, or -1 to draw nothing (a stray continuation byte).
int text_next_glyph(const char *s, u32 len, u32 *pos);

// Pen advance of a UTF-8 string, in pixels: the sum of its glyphs'
// advances, which includes the gap after the last one (1 px in the menu
// fonts).
u32 text_width(const struct torii_font *font, const char *s, u32 len);

// What text_draw draws: glyph indices into font->glyphs and where it ends.
struct text_line {
	u8 glyphs[TEXT_MAX_GLYPHS];
	u32 count;
	u32 advance;    // pen advance of the glyphs
	u32 extent;     // pixels from the pen to the right of the last inked pixel
	u8 truncated;   // the string didn't fit and ends in an ellipsis
};

// Lays out a UTF-8 string to fit clip pixels: every inked pixel lands in
// [0, clip). A string too wide is cut after its last character that leaves
// room for "..." (blank glyphs before the cut dropped) and ends in "...";
// if not even "..." fits, nothing is drawn. Returns line->count.
u32 text_layout(const struct torii_font *font, const char *s, u32 len, u32 clip, struct text_line *line);

// Draws a UTF-8 string with its line's top at pixel (x, y) of the window,
// in palette index ink (1-15), cut with an ellipsis to fit clip pixels and
// the window's right edge. The whole line must fit the window's height
// (y + font->line_height <= height * 8), or nothing is drawn. Returns the
// pen position after the text.
u32 text_draw(const struct text_window *win, const struct torii_font *font, u32 x, u32 y, u32 clip, u32 ink,
	      const char *s, u32 len);

// Draws a laid-out line (text_layout) at (x, y), as text_draw does.
u32 text_draw_line(const struct text_window *win, const struct torii_font *font, u32 x, u32 y, u32 ink,
		   const struct text_line *line);

// Fills the rectangle [x, x + w) x [y, y + h) of the window, clipped to it,
// with palette index index (0, transparent, erases text). Opaque: every
// pixel inside changes.
void text_clear(const struct text_window *win, u32 x, u32 y, u32 w, u32 h, u32 index);

// Erases the pixel rows [y, y + h) across the whole window.
void text_clear_rows(const struct text_window *win, u32 y, u32 h);

// --- The blitter (text_blit.s), for text.c and the tests --------------------

// A run of glyphs at one pen position, every inked pixel inside the window.
struct text_run {
	u32 *tiles;          // the window's first tile
	u32 column_words;    // words in a tile column: window height * 8
	const struct torii_font *font;  // row_bytes 2
	const u8 *glyphs;    // indices into font->glyphs
	u32 count;
	u32 x;               // pen position of the first glyph
	u32 y;               // line top
	u32 ink;             // palette index, 0-15
};

// Draws a run into 4bpp column-major tiles. ARM code in IWRAM.
void text_blit(const struct text_run *run);
#ifdef TORII_TEST_KERNEL
// The same for a font with 1 byte rows, for the tests' benchmark
void text_blit_rows1(const struct text_run *run);
#endif

#endif
