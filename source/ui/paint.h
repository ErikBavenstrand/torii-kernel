// Painting helpers for canvases (ui.h's UI_CANVAS) that draw their own
// shapes and short labels: the rename keyboard's and the code keypad's.
//
// Tiles are column-major (engine/text.h), so a fill costs a call a tile
// column: a rounded shape is one block fill with its corners cut back.
// Text goes through the blitter in glyph runs laid out here, not
// text_draw, whose layout (clipping, an ellipsis) costs several times the
// blit of a letter. Inline, as they were where they began (screens/
// rename.c), so a paint's cost doesn't change.
#ifndef TORII_UI_PAINT_H
#define TORII_UI_PAINT_H

#include <string.h>

#include "ui.h"
#include "../engine/text.h"

// [x, x + w) x [y, y + h) in index c, a tile column at a time (whole ones
// by plain stores): for small shapes, whose pixels cost less than
// text_clear's setup (it pays off from whole tile columns of 8 rows on,
// which it fills by DMA)
static inline void paint_fill(const struct text_window *win, u32 x, u32 y, u32 w, u32 h, u32 c)
{
	u32 cw = win->height * 8u, v = (c & 15) * 0x11111111u, end = x + w;
	while (x < end) {
		u32 a = x & 7, start = x & ~7u, b = end - start < 8 ? end - start : 8;
		u32 *p = win->tiles + (x >> 3) * cw + y, *q = p + h;
		if (b - a == 8) {
			while (p < q)
				*p++ = v;
		} else {
			u32 m = (0xFFFFFFFFu >> (32 - 4 * (b - a))) << (4 * a);
			for (; p < q; p++)
				*p = (*p & ~m) | (v & m);
		}
		x = start + 8;
	}
}

// The rows of a shape [x, x + w) x [y, y + h) with its corners rounded by r:
// the rows between the corners in one block, a fill each for the rest. The
// pixels the rounding leaves out stay as they were.
static inline void paint_round_fill(const struct text_window *win, u32 x, u32 y, u32 w, u32 h, u32 r, u32 role)
{
	u32 rows = r < h / 2 ? r : h / 2;
	for (u32 e = 0; e < rows; e++) {
		u32 in = ui_round_inset(r, e);
		paint_fill(win, x + in, y + e, w - 2 * in, 1, role);
		paint_fill(win, x + in, y + h - 1 - e, w - 2 * in, 1, role);
	}
	if (h - 2 * rows >= 8)
		text_clear(win, x, y + rows, w, h - 2 * rows, role);  // whole columns by DMA
	else
		paint_fill(win, x, y + rows, w, h - 2 * rows, role);
}

// A page card (spec.md: r4, a shadow below): its shadow, the body's shape
// moved off px down, then the body of paper over it. Its middle rows by
// text_clear: a card spans the canvas.
static inline void paint_card_fill(const struct text_window *win, u32 x, u32 y, u32 w, u32 h, u32 r, u32 off, u32 paper)
{
	u32 rows = r < h / 2 ? r : h / 2;
	for (u32 k = h - rows; k < h + off; k++) {
		u32 e = h + off - 1 - k, in = e < r ? ui_round_inset(r, e) : 0;
		paint_fill(win, x + in, y + k, w - 2 * in, 1, UI_ROLE_SHADOW);
	}
	for (u32 e = 0; e < rows; e++) {
		u32 in = ui_round_inset(r, e);
		paint_fill(win, x + in, y + e, w - 2 * in, 1, paper);
		paint_fill(win, x + in, y + h - 1 - e, w - 2 * in, 1, paper);
	}
	text_clear(win, x, y + rows, w, h - 2 * rows, paper);
}

#define PAINT_LABEL_MAX 16  // glyphs in a label

// Text as the blitter takes it: glyph indices and their pen advance
struct paint_label {
	u8 glyphs[PAINT_LABEL_MAX];
	u8 count;
	u8 advance;  // px
};

// One glyph of font
static inline void paint_label_glyph(const struct torii_font *font, u32 g, struct paint_label *l)
{
	l->glyphs[0] = (u8)g;
	l->count = 1;
	l->advance = font->glyphs[g].advance;
}

// A Latin-1 character's glyph (the menu's fonts start at U+0020)
static inline void paint_label_char(const struct torii_font *font, u32 c, struct paint_label *l)
{
	paint_label_glyph(font, c - TORII_FONT_FIRST, l);
}

// A short UTF-8 label's glyphs (cut at PAINT_LABEL_MAX)
static inline void paint_label_text(const struct torii_font *font, const char *s, struct paint_label *l)
{
	u32 len = (u32)strlen(s), pos = 0, adv = 0;
	l->count = 0;
	while (pos < len && l->count < PAINT_LABEL_MAX) {
		int g = text_next_glyph(s, len, &pos);
		if (g < 0)
			continue;
		l->glyphs[l->count++] = (u8)g;
		adv += font->glyphs[g].advance;
	}
	l->advance = (u8)(adv < 255 ? adv : 255);
}

// Draws a label with its pen at x and its line's top at y; every inked pixel
// must fall inside the window (the look puts them there)
static inline void paint_blit(const struct text_window *win, const struct torii_font *font, u32 x, u32 y, u32 ink,
			      const struct paint_label *l)
{
	struct text_run run = {win->tiles, win->height * 8u, font, l->glyphs, l->count, x, y, ink & 15};
	if (l->count)
		text_blit(&run);
}

// A label centred in [x, x + w), its line's top at y
static inline void paint_blit_centred(const struct text_window *win, const struct torii_font *font, u32 x, u32 y, u32 w,
				      u32 ink, const struct paint_label *l)
{
	paint_blit(win, font, x + (w - l->advance + 1) / 2, y, ink, l);
}

#endif
