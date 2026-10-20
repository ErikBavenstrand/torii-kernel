// See ui_reading.h.
#include "ui_reading.h"

#include "ui.h"
#include "../engine/text.h"

// The bytes of s[0, len) that fit width px: up to the last space that does
// (the space dropped), or as many characters as fit if no space does (at
// least one); *next gets where the next row starts
static u32 fit(const struct torii_font *font, const char *s, u32 len, u32 width, u32 *next)
{
	if (text_width(font, s, len) <= width) {
		*next = len;
		return len;
	}
	u32 pos = 0, last_space = 0, last_fit = 0;
	int spaced = 0;
	while (pos < len) {
		u32 at = pos;
		text_next_glyph(s, len, &pos);
		if (text_width(font, s, pos) > width)
			break;
		last_fit = pos;
		if (s[at] == ' ' && at) {
			last_space = at;
			spaced = 1;
		}
	}
	if (spaced) {
		u32 skip = last_space;
		while (skip < len && s[skip] == ' ')
			skip++;
		*next = skip;
		return last_space;
	}
	if (!last_fit)
		text_next_glyph(s, len, &last_fit);  // one character, wider than the row
	*next = last_fit;
	return last_fit;
}

// Adds a row; 0 if the arrays are full
static int add_row(struct ui_reading *r, u32 start, u32 len, u32 kind)
{
	if (r->count >= r->cap || start > 0xFFFF)
		return 0;
	r->start[r->count] = (u16)start;
	r->len[r->count] = (u8)(len > 255 ? 255 : len);
	r->kind[r->count] = (u8)kind;
	r->count++;
	return 1;
}

u32 ui_reading_wrap(struct ui_reading *r, const struct torii_font *font, u32 width, u32 key_w)
{
	r->count = 0;
	const char *text = r->text ? r->text : "";
	for (u32 at = 0;;) {
		u32 end = at;
		while (text[end] && text[end] != '\n')
			end++;
		const char *line = text + at;
		u32 len = end - at, kind = UI_READ_PLAIN, body = 0, w = width;
		if (len >= 2 && line[0] == '#' && line[1] == ' ') {
			kind = UI_READ_HEADING;
			body = 2;
		} else {
			for (u32 i = 0; i < len; i++)
				if (line[i] == '\t') {
					kind = UI_READ_KEY;
					body = i + 1;
					w = width > key_w ? width - key_w : 1;
					break;
				}
		}
		if (body >= len) {
			if (!add_row(r, at, len, kind))
				return 0;
		} else {
			// The first row keeps the line's head (the "# " or the key), the
			// rest only their text
			u32 pos = body, first = 1;
			while (pos < len) {
				u32 next, n = fit(font, line + pos, len - pos, w, &next);
				u32 row_kind = first                     ? kind
					       : kind == UI_READ_KEY     ? UI_READ_MORE
					       : kind == UI_READ_HEADING ? UI_READ_HEADING_MORE
									 : kind;
				u32 start = first ? at : at + pos, row_len = first ? pos + n : n;
				if (row_len > 255)
					row_len = 255;  // a row's length is a byte: longer never fits a row
				if (!add_row(r, start, row_len, row_kind))
					return 0;
				pos += next;
				first = 0;
			}
		}
		if (!text[end])
			break;
		at = end + 1;
	}
	return r->count;
}

static u32 reading_count(void *ctx)
{
	return ((const struct ui_reading *)ctx)->count;
}

static int reading_fetch(void *ctx, u32 i, struct ui_item *out)
{
	const struct ui_reading *r = ctx;
	if (i >= r->count)
		return UI_ITEM_PENDING;
	const char *s = r->text + r->start[i];
	u32 len = r->len[i];
	switch (r->kind[i]) {
	case UI_READ_HEADING:
		out->text = s + 2;
		out->len = (u16)(len - 2);
		out->ink = UI_ROLE_ACCENT_INK;
		break;
	case UI_READ_KEY: {
		u32 tab = 0;
		while (tab < len && s[tab] != '\t')
			tab++;
		out->key = s;
		out->key_len = (u8)tab;
		out->text = s + tab + 1;
		out->len = (u16)(len - tab - 1);
		break;
	}
	case UI_READ_HEADING_MORE:
		out->text = s;
		out->len = (u16)len;
		out->ink = UI_ROLE_ACCENT_INK;
		break;
	case UI_READ_MORE:
		out->key = s;  // the key column, no chip
		out->key_len = 0;
		out->text = s;
		out->len = (u16)len;
		break;
	default:
		out->text = s;
		out->len = (u16)len;
		break;
	}
	if (!out->len)
		out->text = " ";  // an empty row is drawn (blank), not waited for
	if (!out->len)
		out->len = 1;
	return UI_ITEM_READY;
}

const struct ui_list_model ui_reading_model = {reading_count, reading_fetch, 0};
