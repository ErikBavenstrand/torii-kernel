// A reading view: a long text (an Info page's) shown in a list with
// UI_LIST_PLAIN | UI_LIST_SCROLL, UP and DOWN scrolling it a line at a time.
// The text is wrapped once into lines that fit the list's width; each line
// is a row. Hardware-free, so tests/host runs it.
//
// The text's own lines ("\n") are of three kinds:
//   "# Home"            a section heading, in the item's ink
//   "A\topen the item"  a key chip (the hint bar's), then its text at the
//                       look's reading_key_w; wrapped lines keep to that column
//   anything else       plain text; "" an empty line
//
//	static u16 starts[64]; static u8 lens[64], kinds[64];
//	static struct ui_reading controls = {en_info_controls, starts, lens, kinds, 64};
//	ui_reading_wrap(&controls, ui_theme()->body, 216 - 2 * pad, ui_theme()->look.reading_key_w);
//	... a list over ui_reading_model, the view's ctx &controls
#ifndef TORII_UI_READING_H
#define TORII_UI_READING_H

#include <gba_base.h>

#include "../engine/font.h"

enum {
	UI_READ_PLAIN,
	UI_READ_HEADING,
	UI_READ_KEY,   // a key line's first: its key, a tab, its text
	UI_READ_MORE,  // a key line wrapped on: text at the key column
	UI_READ_HEADING_MORE,  // a heading wrapped on: no "# " of its own
};

struct ui_reading {
	const char *text;  // UTF-8
	u16 *start;  // each row's first byte in text (cap entries)
	u8 *len;     // its bytes (a key row's: the key, the tab and its text)
	u8 *kind;    // UI_READ_*
	u16 cap;     // rows the arrays hold: more are dropped (ui_reading_wrap says)
	u16 count;   // rows
};

// Wraps r->text into rows width px wide in font (key rows' text key_w px
// in): at the last space that fits, or inside a word longer than a row.
// Returns the rows, or 0 if they didn't all fit in r->cap (the first cap
// are kept) or the text passes 64 KB.
u32 ui_reading_wrap(struct ui_reading *r, const struct torii_font *font, u32 width, u32 key_w);

// The list model over a struct ui_reading, which must be the view's ctx
// (ui_push): a row's text, a key row's key (ui_item.key), a heading in the
// item's ink.
extern const struct ui_list_model ui_reading_model;

#endif
