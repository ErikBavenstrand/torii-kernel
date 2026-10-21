// The rename keyboard (rename.h).
//
// Two canvases on plane 1 draw it, over the keyboard's card on plane 2: the
// field (its own card, the name, the caret, the counter and the L and R
// chips) and the keys. Each repaints whole when it changes, into the tile
// set the screen doesn't show (ui.h, UI_CANVAS). Plane 0 stays free for the
// "Discard changes?" confirmation. Keys repeat on the keyboard's own
// schedule (research-v04-ux.md, the held-key curve), counted in the loop
// from the frames each key has been held.
#include <stdio.h>
#include <string.h>

#include "rename.h"
#include "../lang.h"
#include "../rename_edit.h"
#include "../tlog.h"
#include "../engine/frame.h"
#include "../engine/text.h"
#include "../ui/art_ids.h"
#include "../ui/ui.h"

// --- Look (docs/design/v0.4/mockups-b16-b18.md, "Rename keyboard"; screen px) ---
static const struct rename_look {
	u8 field_x, field_y, field_w, field_h;  // the name's card
	u8 lchip_x, rchip_x, chip_y;            // its L and R key chips (the caret's keys)
	u8 chip_min;                            // a chip's narrowest
	u8 text_x, text_y;                      // the name's first pixel, its line's top
	u8 counter_right;                       // where "14/60" ends
	u8 clip_gap;                            // between the name's clip and the counter
	u8 caret_w, caret_h, caret_dy;          // the caret, below the line's top
	u8 caret_gap;                           // px the text after it moves right
	u8 grid_x, grid_y;                      // the first key cell
	u8 col_w, pitch;                        // a cell
	u8 key_inset, key_radius;               // a key's pill in its cell
	u8 ok_radius;                           // the chosen OK's ring
	u8 ok_inset;                            // and its pill inside the ring
	u8 func_text_dy;                        // a function key's word below its cell's top
	u8 shift_dy, shift_gap;                 // the shift arrow below the cell's top; arrow to word
	u8 blink;                               // frames the caret shows, then hides
	u8 counter_blink;                       // frames the counter hides when a key is refused
	u8 message;                             // frames a refusal shows in the hint area
	u8 grid_delay, grid_every;              // held d-pad and A: first repeat, then every
	u8 edit_delay, edit_every;              // held B, L and R
} look = {
	.field_x = 4,
	.field_y = 21,
	.field_w = 232,
	.field_h = 18,
	.lchip_x = 8,
	.rchip_x = 215,
	.chip_y = 25,
	.chip_min = 13,
	.text_x = 25,
	.text_y = 24,
	.counter_right = 210,
	.clip_gap = 6,
	.caret_w = 1,
	.caret_h = 11,
	.caret_dy = 1,
	.caret_gap = 2,
	.grid_x = 15,
	.grid_y = 47,
	.col_w = 21,
	.pitch = 18,
	.key_inset = 1,
	.key_radius = 3,
	.ok_radius = 4,
	.ok_inset = 2,
	.func_text_dy = 3,
	.shift_dy = 5,
	.shift_gap = 3,
	.blink = 30,
	.counter_blink = 6,
	.message = 90,
	.grid_delay = 14,
	.grid_every = 6,
	.edit_delay = 16,
	.edit_every = 4,
};

// The layout's rectangles (whole tiles)
#define TITLE_H 24
#define HINTS_H 16
#define FIELD_Y 16  // the field's canvas: its card and chips
#define FIELD_H 24
#define KEYS_X 8    // the keys' canvas, over the keyboard's card
#define KEYS_Y 40
#define KEYS_W 224
#define KEYS_H 104
static const struct ui_card_config keys_card = {4, 3, 4, 3, 0};  // body 4..236 x 43..141

// Pixel glyphs the fonts lack: '#' inked
static const char *const glyph_shift[] = {"...#...", "..#.#..", ".#...#.", "##...##", ".#...#.", ".#...#.", ".#####."};
static const char *const glyph_shift_on[] = {"...#...", "..###..", ".#####.", "#######",
					     ".#####.", ".#####.", ".#####."};
static const char *const glyph_delete[] = {"...########",  "..#........#", ".#..#...#..#",
					   "#....#.#...#", "#.....#....#", "#....#.#...#",
					   ".#..#...#..#", "..#........#", "...########"};
#define GLYPH_ROWS(g) (sizeof(g) / sizeof(*(g)))

// --- State ---------------------------------------------------------------------

enum {
	WANT_NONE,
	WANT_OK,
	WANT_CANCEL,
};

// The keys that repeat, and their schedules
static const u16 repeat_keys[] = {INPUT_UP, INPUT_DOWN, INPUT_LEFT, INPUT_RIGHT, INPUT_A, INPUT_B, INPUT_L, INPUT_R};
#define REPEATS (sizeof(repeat_keys) / sizeof(*repeat_keys))

static struct {
	const struct rename_ask *ask;
	struct rename_edit e;
	struct rename_sel sel;
	u32 orig_hash;  // the name it started with: Cancel asks only after a change
	u16 orig_len;
	u8 first;  // the view's first node
	u8 page, upper;
	u8 done, result, want;
	u8 edited;  // a key changed the name (B on an empty field never edited cancels)
	u8 blink;   // frames into the caret's cycle
	u8 caret_on;
	u8 counter_off;  // frames the counter stays hidden
	u8 message;      // frames the hint area's refusal stays
	u16 armed;       // repeat keys whose press the page took
	u32 down_at[REPEATS];   // frame_count() when each went down
	u16 held_was[REPEATS];  // and its held frames at the last look
	char counter[12];
	char msg[48];
	struct ui_hint hints[4];
} kb EWRAM_BSS;

static u8 node(u32 n)
{
	return (u8)(kb.first + n);
}

static u32 name_hash(const char *s, u32 len)
{
	u32 h = 2166136261u;  // FNV-1a
	for (u32 i = 0; i < len; i++)
		h = (h ^ (u8)s[i]) * 16777619u;
	return h;
}

static int unchanged(void)
{
	return kb.e.len == kb.orig_len && name_hash(kb.e.text, kb.e.len) == kb.orig_hash;
}

// --- Painting ------------------------------------------------------------------

// [x, x + w) x [y, y + h) of role, its corners rounded by r
static void round_fill(const struct text_window *win, s32 x, s32 y, u32 w, u32 h, u32 r, u32 role)
{
	for (u32 j = 0; j < h; j++) {
		u32 e = j < h - 1 - j ? j : h - 1 - j, in = ui_round_inset(r, e);
		if (y + (s32)j >= 0 && x + (s32)in >= 0 && w > 2 * in)
			text_clear(win, (u32)(x + (s32)in), (u32)(y + (s32)j), w - 2 * in, 1, role);
	}
}

static void glyph(const struct text_window *win, u32 x, u32 y, const char *const *rows, u32 count, u32 role)
{
	for (u32 j = 0; j < count; j++)
		for (u32 i = 0; rows[j][i]; i++)
			if (rows[j][i] == '#')
				text_clear(win, x + i, y + j, 1, 1, role);
}

static u32 glyph_w(const char *const *rows, u32 count)
{
	u32 w = 0;
	for (u32 j = 0; j < count; j++)
		w = strlen(rows[j]) > w ? (u32)strlen(rows[j]) : w;
	return w;
}

static u32 width(const struct torii_font *font, const char *s, u32 len)
{
	return text_width(font, s, len);
}

// A key chip (the hint bar's style) with its top at y
static void chip(const struct text_window *win, u32 x, u32 y, const char *key)
{
	const struct ui_look *l = &ui_theme()->look;
	const struct torii_font *font = ui_theme()->body;
	u32 tw = width(font, key, strlen(key)), w = tw + 2u * l->hint_chip_pad + 1;
	w = w < look.chip_min ? look.chip_min : w;
	round_fill(win, (s32)x, (s32)y, w, l->hint_chip_h, l->hint_chip_radius, UI_ROLE_TRACK);
	s32 top = (s32)y + ((s32)l->hint_chip_h - (s32)font->line_height) / 2;
	text_draw(win, font, x + (w - tw + 1) / 2, top < 0 ? 0 : (u32)top, tw + 1, UI_ROLE_DIM, key, strlen(key));
}

// The bytes of the character at s[at]
static u32 char_len(const char *s, u32 at, u32 len)
{
	u32 n = 1;
	while (at + n < len && ((u8)s[at + n] & 0xC0) == 0x80)
		n++;
	return n;
}

static void paint_field(void *ctx, u8 nd, const struct text_window *win)
{
	(void)ctx;
	const struct ui_theme *t = ui_theme();
	const struct torii_font *font = t->body;
	s32 ox = ui_node(nd)->x, oy = ui_node(nd)->y;
	s32 fx = look.field_x - ox, fy = look.field_y - oy;
	round_fill(win, fx, fy + t->look.shadow_offset, look.field_w, look.field_h, t->look.card_radius,
		   UI_ROLE_SHADOW);
	round_fill(win, fx, fy, look.field_w, look.field_h, t->look.card_radius, UI_ROLE_PANEL);
	chip(win, (u32)(look.lchip_x - ox), (u32)(look.chip_y - oy), "L");
	chip(win, (u32)(look.rchip_x - ox), (u32)(look.chip_y - oy), "R");
	u32 y = (u32)(look.text_y - oy), x0 = (u32)(look.text_x - ox);
	// The counter, in danger ink at the cap (and hidden a moment when a key
	// went past it)
	u32 cw = width(font, kb.counter, strlen(kb.counter)), cx = look.counter_right - (u32)ox - cw;
	if (!kb.counter_off)
		text_draw(win, font, cx, y, cw + 1,
			  rename_edit_chars(&kb.e) >= kb.e.max ? UI_ROLE_DANGER : UI_ROLE_FAINT, kb.counter,
			  strlen(kb.counter));
	u32 right = cx - look.clip_gap, avail = right > x0 ? right - x0 : 0;
	const char *s = kb.e.text, *ext = kb.ask->ext ? kb.ask->ext : "";
	u32 caret = kb.e.caret, len = kb.e.len, start = 0;
	u32 before = width(font, s, caret);
	// A name too long for the field scrolls to keep the caret in view: its
	// start gives way to "..."
	if (before + look.caret_gap + look.caret_w > avail) {
		u32 dots = width(font, "...", 3) + 1, room = avail > dots + 2 ? avail - dots - 2 : 0;
		while (start < caret && before > room) {
			u32 n = char_len(s, start, len);
			before -= width(font, s + start, n);
			start += n;
		}
		text_draw(win, font, x0, y, dots, UI_ROLE_FAINT, "...", 3);
		x0 += dots;
	}
	u32 x = text_draw(win, font, x0, y, right > x0 ? right - x0 : 0, UI_ROLE_TEXT, s + start, caret - start);
	if (caret == start)
		x = x0;
	if (kb.caret_on)
		text_clear(win, x, y + look.caret_dy, look.caret_w, look.caret_h, UI_ROLE_ACCENT);
	x += look.caret_gap;
	if (x < right && len > caret)
		x = text_draw(win, font, x, y, right - x, UI_ROLE_TEXT, s + caret, len - caret);
	if (x < right && *ext)
		text_draw(win, font, x, y, right - x, UI_ROLE_FAINT, ext, strlen(ext));
}

// A Latin-1 character as UTF-8 into buf; its bytes
static u32 utf8(u32 c, char *buf)
{
	if (c < 0x80) {
		buf[0] = (char)c;
		return 1;
	}
	buf[0] = (char)(0xC0 | c >> 6);
	buf[1] = (char)(0x80 | (c & 0x3F));
	return 2;
}

// A word centred in a cell's width, its line top dy below the cell's top
static void centre_text(const struct text_window *win, const struct torii_font *font, s32 x, s32 y, u32 w, u32 dy,
			const char *s, u32 len, u32 role)
{
	u32 tw = width(font, s, len);
	s32 at = x + ((s32)w - (s32)tw + 1) / 2;
	text_draw(win, font, at < 0 ? 0 : (u32)at, (u32)(y + (s32)dy), w, role, s, len);
}

static void paint_keys(void *ctx, u8 n, const struct text_window *win)
{
	(void)ctx;
	const struct ui_theme *t = ui_theme();
	s32 ox = ui_node(n)->x, oy = ui_node(n)->y;
	u32 in = look.key_inset;
	for (u32 r = 0; r < RENAME_CHAR_ROWS; r++)
		for (u32 c = 0; c < RENAME_COLS; c++) {
			s32 x = look.grid_x + (s32)(c * look.col_w) - ox, y = look.grid_y + (s32)(r * look.pitch) - oy;
			if (kb.sel.row == r && kb.sel.col == c)
				round_fill(win, x + (s32)in, y + (s32)in, look.col_w - 2 * in, look.pitch - 2 * in,
					   look.key_radius, UI_ROLE_TINT);
			char buf[2];
			u32 ch = rename_key(kb.page, kb.upper, r, c);
			centre_text(win, t->heading, x, y, look.col_w, 0, buf, utf8(ch, buf),
				    rename_is_letter(ch) ? UI_ROLE_TEXT : UI_ROLE_DIM);
		}
	// The function row: paper keys, OK solid in the item's colour
	u32 chosen = kb.sel.row == RENAME_FUNC_ROW ? rename_func_at(kb.sel.col) : RENAME_FUNCS;
	s32 y = look.grid_y + (s32)(RENAME_FUNC_ROW * look.pitch) - oy;
	const struct lang_rename *L = gl_rename;
	for (u32 k = 0; k < RENAME_FUNCS; k++) {
		s32 x = look.grid_x + (s32)(rename_func_col(k) * look.col_w) - ox;
		u32 w = rename_func_span[k] * look.col_w, h = look.pitch;
		int on = k == chosen;
		if (k == RENAME_KEY_OK) {
			u32 i = on ? look.ok_inset : in;
			if (on)
				round_fill(win, x, y, w, h, look.ok_radius, UI_ROLE_TEXT);
			round_fill(win, x + (s32)i, y + (s32)i, w - 2 * i, h - 2 * i, look.key_radius, UI_ROLE_ACCENT);
			centre_text(win, t->body, x, y, w, look.func_text_dy, L->key_ok, strlen(L->key_ok),
				    UI_ROLE_ACCENT_TEXT);
			continue;
		}
		int open = k == RENAME_KEY_LATIN && kb.page == RENAME_PAGE_LATIN;
		round_fill(win, x + (s32)in, y + (s32)in, w - 2 * in, h - 2 * in, look.key_radius,
			   on     ? UI_ROLE_TINT
			   : open ? UI_ROLE_TRACK
				  : UI_ROLE_BG);
		switch (k) {
		case RENAME_KEY_SHIFT: {
			// The arrow, filled while upper case is on, and the case
			const char *const *g = kb.upper ? glyph_shift_on : glyph_shift;
			const char *word = kb.upper ? L->key_upper : L->key_lower;
			u32 gw = glyph_w(g, GLYPH_ROWS(glyph_shift)), lw = width(t->body, word, strlen(word));
			s32 gx = x + ((s32)w - (s32)(gw + look.shift_gap + lw) + 1) / 2;
			u32 role = kb.upper ? UI_ROLE_TEXT : UI_ROLE_DIM;
			glyph(win, (u32)gx, (u32)(y + look.shift_dy), g, GLYPH_ROWS(glyph_shift), role);
			text_draw(win, t->body, (u32)gx + gw + look.shift_gap, (u32)(y + look.func_text_dy), lw + 1,
				  role, word, strlen(word));
			break;
		}
		case RENAME_KEY_LATIN:
			centre_text(win, t->body, x, y, w, look.func_text_dy, L->key_latin, strlen(L->key_latin),
				    open ? UI_ROLE_TEXT : UI_ROLE_DIM);
			break;
		case RENAME_KEY_SPACE:
			centre_text(win, t->body, x, y, w, look.func_text_dy, L->key_space, strlen(L->key_space),
				    UI_ROLE_DIM);
			break;
		case RENAME_KEY_DELETE: {
			u32 gw = glyph_w(glyph_delete, GLYPH_ROWS(glyph_delete)), gh = GLYPH_ROWS(glyph_delete);
			glyph(win, (u32)(x + ((s32)w - (s32)gw + 1) / 2), (u32)(y + ((s32)h - (s32)gh) / 2 + 1),
			      glyph_delete, gh, UI_ROLE_DIM);
			break;
		}
		default:
			centre_text(win, t->body, x, y, w, look.func_text_dy, L->key_cancel, strlen(L->key_cancel),
				    UI_ROLE_DIM);
			break;
		}
	}
}

// --- The layout ------------------------------------------------------------------

enum {
	N_TITLE,
	N_HINTS,
	N_CARD,
	N_FIELD,
	N_KEYS,
	N_NODES
};

static const struct ui_title_config title_config = {"", 0, ART_ICON16_GBA};  // the icon: the page's
static const struct ui_hints_config no_hints = {0, 0};
static const struct ui_canvas_config field_canvas = {paint_field};
static const struct ui_canvas_config keys_canvas = {paint_keys};

static const struct ui_spec nodes[N_NODES] = {
	[N_TITLE] = {UI_TITLE, 1, UI_NONE, 3, UI_HFILL, 0, 0, 0, 0, 0, 0, TITLE_H, &title_config},
	[N_HINTS] = {UI_HINTS, 2, UI_NONE, 3, UI_HFILL | UI_BOTTOM, 0, 0, 0, 0, 0, 0, HINTS_H, &no_hints},
	[N_CARD] = {UI_CARD, 3, UI_NONE, 2, UI_HFILL, 0, 0, 0, 0, KEYS_Y, 0, KEYS_H, &keys_card},
	[N_FIELD] = {UI_CANVAS, 4, UI_NONE, 1, UI_HFILL, 0, 0, 0, 0, FIELD_Y, 0, FIELD_H, &field_canvas},
	[N_KEYS] = {UI_CANVAS, 5, UI_NONE, 1, UI_LEFT, 0, 0, 0, KEYS_X, KEYS_Y, KEYS_W, KEYS_H, &keys_canvas},
};

static int rename_event(struct ui_view *v, const struct ui_event *e);
static const struct ui_view_spec view = {"rename", nodes, N_NODES, 0, UI_NONE, 0, rename_event};

// --- Changes ---------------------------------------------------------------------

static void field_changed(void)
{
	snprintf(kb.counter, sizeof(kb.counter), "%lu/%u", (unsigned long)rename_edit_chars(&kb.e), kb.e.max);
	kb.blink = 0;  // the caret shows at once after any key
	kb.caret_on = 1;
	ui_canvas_redraw(node(N_FIELD));
}

static void keys_changed(void)
{
	ui_canvas_redraw(node(N_KEYS));
}

static void show_hints(void)
{
	const struct lang_rename *L = gl_rename;
	kb.hints[0] = (struct ui_hint){"A", L->hint_type};
	kb.hints[1] = (struct ui_hint){"B", L->hint_delete};
	kb.hints[2] = (struct ui_hint){"SELECT", L->hint_case};
	kb.hints[3] = (struct ui_hint){"START", L->hint_ok};
	ui_hints_set(node(N_HINTS), kb.hints, 4);
}

// A refusal in the hint area, in danger ink, for look.message frames
static void message(const char *text)
{
	static const struct ui_hint_note danger = {UI_ROLE_DANGER, 0, 0, 0, 0, 0};
	snprintf(kb.msg, sizeof(kb.msg), "%s", text);
	ui_hints_note(node(N_HINTS), kb.msg, &danger);
	kb.message = look.message;
	tlog_debug("rename: %s", kb.msg);
}

static void type(u32 c)
{
	// A capital typed at the start or after a space: lower case again
	int word_start = kb.e.caret == 0 || kb.e.text[kb.e.caret - 1] == ' ';
	if (!rename_edit_type(&kb.e, c)) {
		char text[sizeof(kb.msg)];
		snprintf(text, sizeof(text), EN_RENAME_TOO_LONG, kb.e.max);
		message(text);
		kb.counter_off = look.counter_blink;
		field_changed();
		return;
	}
	kb.edited = 1;
	if (kb.upper && word_start && rename_is_letter(c)) {
		kb.upper = 0;
		keys_changed();
	}
	field_changed();
}

static void delete_back(int fresh)
{
	if (rename_edit_delete(&kb.e)) {
		kb.edited = 1;
		field_changed();
	} else if (fresh && !kb.e.len && !kb.edited) {
		kb.want = WANT_CANCEL;  // an empty name never edited: B leaves
	}
}

// A on the selected key (fresh: pressed, not a repeat)
static void press_key(int fresh)
{
	if (kb.sel.row < RENAME_CHAR_ROWS) {
		type(rename_key(kb.page, kb.upper, kb.sel.row, kb.sel.col));
		return;
	}
	switch (rename_func_at(kb.sel.col)) {
	case RENAME_KEY_SHIFT:
		if (fresh) {
			kb.upper = !kb.upper;
			keys_changed();
		}
		break;
	case RENAME_KEY_LATIN:
		if (fresh) {
			kb.page = kb.page == RENAME_PAGE_LATIN ? RENAME_PAGE_ABC : RENAME_PAGE_LATIN;
			keys_changed();
		}
		break;
	case RENAME_KEY_SPACE:
		type(' ');
		break;
	case RENAME_KEY_DELETE:
		delete_back(0);
		break;
	case RENAME_KEY_CANCEL:
		if (fresh)
			kb.want = WANT_CANCEL;
		break;
	default:
		if (fresh)
			kb.want = WANT_OK;
		break;
	}
}

// What key does, pressed (fresh) or repeating
static void act(u16 key, int fresh)
{
	switch (key) {
	case INPUT_UP:
	case INPUT_DOWN:
	case INPUT_LEFT:
	case INPUT_RIGHT:
		if (rename_sel_move(&kb.sel, key, fresh))
			keys_changed();
		break;
	case INPUT_A:
		press_key(fresh);
		break;
	case INPUT_B:
		delete_back(fresh);
		break;
	case INPUT_L:
	case INPUT_R:
		rename_edit_move(&kb.e, key == INPUT_L ? -1 : 1);
		field_changed();
		break;
	case INPUT_SELECT:
		kb.upper = !kb.upper;
		keys_changed();
		break;
	case INPUT_START:
		kb.want = WANT_OK;
		break;
	default:
		break;
	}
	frame_measure("rename-key");
}

static int rename_event(struct ui_view *v, const struct ui_event *e)
{
	(void)v;
	if (e->type != UI_EV_KEYS)
		return 0;
	u16 pressed = e->keys & ui_input()->pressed;
	if (kb.want || kb.done)
		return 1;
	for (u32 b = 0; b < INPUT_KEYS; b++) {
		u16 key = (u16)(1u << b);
		if (!(pressed & key))
			continue;
		act(key, 1);
		// Only the newest direction repeats
		if (key & INPUT_DPAD)
			kb.armed &= (u16)~INPUT_DPAD;
		for (u32 i = 0; i < REPEATS; i++)
			if (repeat_keys[i] == key) {
				kb.armed |= key;
				kb.down_at[i] = frame_count();
				kb.held_was[i] = 0;
			}
	}
	return 1;
}

// --- The loop --------------------------------------------------------------------

// Repeats due before frame f of a hold: the first at delay, then every
static u32 repeats_by(u32 f, u32 delay, u32 every)
{
	return f < delay ? 0 : 1 + (f - delay) / every;
}

// Held keys' repeats, on the keyboard's schedule
static void repeat(void)
{
	const InputState *in = ui_input();
	for (u32 i = 0; i < REPEATS; i++) {
		u16 key = repeat_keys[i];
		if (!(kb.armed & key))
			continue;
		if (!(in->held & key)) {
			kb.armed &= (u16)~key;
			continue;
		}
		int edit = key == INPUT_B || key == INPUT_L || key == INPUT_R;
		u32 delay = edit ? look.edit_delay : look.grid_delay, every = edit ? look.edit_every : look.grid_every;
		u32 f = frame_count() - kb.down_at[i];
		f = f > 0xFFFF ? 0xFFFF : f;
		if (repeats_by(f, delay, every) > repeats_by(kb.held_was[i], delay, every))
			act(key, 0);
		kb.held_was[i] = (u16)f;
	}
}

// OK: the name trimmed (in place, so the field shows what was asked for),
// then the page's submit
static void ok(void)
{
	u32 n = rename_edit_result(&kb.e, kb.e.text, kb.ask->file);
	if (n != kb.e.len) {
		kb.e.len = (u16)n;
		kb.e.caret = (u16)n;
	}
	field_changed();
	if (!n) {
		message(gl_rename->empty);
	} else if (rename_edit_chars(&kb.e) > kb.e.max) {
		char text[sizeof(kb.msg)];
		snprintf(text, sizeof(text), EN_RENAME_TOO_LONG, kb.e.max);
		message(text);
	} else if (unchanged()) {
		kb.done = 1;  // nothing to keep
	} else {
		const char *why = kb.ask->submit(kb.ask->ctx, kb.e.text);
		if (why)
			message(why);
		else
			kb.done = kb.result = 1;
	}
}

static void cancel(void)
{
	if (!unchanged()) {
		const struct ui_confirm ask = {gl_rename->discard, NULL, gl_rename->discard_button, 1};
		if (!ui_confirm(node(N_HINTS), &ask))
			return;
	}
	kb.done = 1;
}

// Each frame after ui_frame: repeats, the caret's blink, a refusal's time,
// and OK or Cancel (which may read the card or ask)
static void tick(struct ui_view *v)
{
	if (ui_top() != v || ui_transitioning())
		return;
	repeat();
	if (++kb.blink >= look.blink) {
		kb.blink = 0;
		kb.caret_on = !kb.caret_on;
		ui_canvas_redraw(node(N_FIELD));
	}
	if (kb.counter_off && !--kb.counter_off)
		ui_canvas_redraw(node(N_FIELD));
	if (kb.message && !--kb.message)
		show_hints();
	u8 want = kb.want;
	kb.want = WANT_NONE;
	if (want == WANT_OK)
		ok();
	else if (want == WANT_CANCEL)
		cancel();
}

int rename_run(const struct rename_ask *ask)
{
	memset(&kb, 0, sizeof(kb));
	kb.ask = ask;
	rename_edit_init(&kb.e, ask->name, ask->max);
	kb.orig_len = kb.e.len;
	kb.orig_hash = name_hash(kb.e.text, kb.e.len);
	kb.sel.row = 1;  // on q
	kb.caret_on = 1;
	ui_leave(UI_TR_DEEPER, 1);
	while (ui_depth())
		ui_pop();
	struct ui_view *v = ui_push(&view, &kb);
	if (!v) {
		tlog_error("rename: %s", ui_error());
		return 0;
	}
	kb.first = v->first;
	ui_title_set(node(N_TITLE), gl_rename->title);
	ui_title_icon_set(node(N_TITLE), ask->icon16);
	show_hints();
	field_changed();
	while (!kb.done && ui_depth()) {
		ui_frame();
		tick(v);
	}
	tlog("rename: %s", kb.result ? "kept" : "cancelled");
	ui_leave(UI_TR_DEEPER, -1);
	while (ui_depth())
		ui_pop();
	return kb.result;
}
