// The code keypad (keypad.h).
//
// Two canvases on plane 1 draw it, as the rename keyboard's do: the field
// (the code's card, its lines, the format and what the code reads as) and
// the keys, over the keys' card on plane 2. Plane 0 stays free for the
// "Discard this code?" confirmation. The model is keypad_edit.h's; every
// change checks the code again (keypad_check, in the cheat file's work
// area: the cheat screen decodes the file again after the keypad).
#include "keypad.h"

#include <stdio.h>
#include <string.h>

#include "../cheatstore.h"
#include "../keypad_edit.h"
#include "../lang.h"
#include "../tlog.h"
#include "../engine/frame.h"
#include "../engine/input.h"
#include "../engine/text.h"
#include "../ui/art_ids.h"
#include "../ui/paint.h"
#include "../ui/ui.h"

// --- Look (screen px) -------------------------------------------------------------
static const struct keypad_look {
	u8 card_x, card_y, card_w, card_h;  // the code's card
	u8 line_y, line_h;                  // its first line's top, and the line pitch
	u8 number_right;                    // where a line's number ends
	u8 text_x;                          // a line's first character
	u8 tag_right;                       // where a line's format tag ends
	u8 tag_gap;                         // a line's text to its tag, at least
	u8 caret_room;                      // px past the caret a scrolled line keeps
	u8 mark_pad;                        // px of a stand-in's pill each side of its glyph
	u8 caret_w, caret_h, caret_dy;      // the caret, below the line's top
	u8 info_x, format_y, result_y;      // the format's line and the result's, under the card
	u8 label_gap;                       // "Format" to its value
	u8 grid_x, grid_y;                  // the first key cell
	u8 col_w, pitch;                    // a cell
	u8 key_inset, key_radius;           // a key's pill in its cell
	u8 save_radius, save_inset;         // the chosen Save's ring, and its pill inside it
	u8 func_text_dy;                    // a function key's word below its cell's top
	u8 message;                         // frames a refusal shows in the hint area
	u8 repeat_delay, repeat_every;      // held A and B: frames to the first repeat, then between
} look = {
	.card_x = 4,
	.card_y = 26,
	.card_w = 232,
	.card_h = 37,
	.line_y = 27,
	.line_h = 12,
	.number_right = 18,
	.text_x = 24,
	.tag_right = 228,
	.tag_gap = 6,
	.caret_room = 4,
	.mark_pad = 1,
	.caret_w = 1,
	.caret_h = 11,
	.caret_dy = 1,
	.info_x = 8,
	.format_y = 64,
	.result_y = 76,
	.label_gap = 4,
	.grid_x = 16,
	.grid_y = 94,
	.col_w = 26,
	.pitch = 16,
	.key_inset = 1,
	.key_radius = 3,
	.save_radius = 4,
	.save_inset = 2,
	.func_text_dy = 2,
	.message = 90,
	.repeat_delay = 16,
	.repeat_every = 4,
};

#define VISIBLE 3  // code lines in view

// The layout's rectangles (whole tiles)
#define TITLE_H 24
#define HINTS_H 16
#define FIELD_Y 24
#define FIELD_H 64
#define KEYS_X 8
#define KEYS_Y 88
#define KEYS_W 224
#define KEYS_H 56
static const struct ui_card_config keys_card = {4, 3, 4, 1, 0};  // body 4..236 x 91..143

// The keys: two rows of hex digits, then the function row, whose keys span
// columns. Its symbol key turns the digits into a row of AnyGBA's symbols
// (kp.symbols) and back.
#define COLS 8
#define HEX_ROWS 2
#define ROWS 3
#define FUNC_ROW 2
enum {
	KEY_COLON,
	KEY_LINE,
	KEY_SYMBOLS,
	KEY_DELETE,
	KEY_CANCEL,
	KEY_SAVE,
	FUNCS,
};
static const u8 func_col[FUNCS] = {0, 1, 2, 3, 4, 6};
static const u8 func_span[FUNCS] = {1, 1, 1, 1, 2, 2};
static const char hex[] = "0123456789ABCDEF";
#define SYMBOLS 5
static const char symbol_keys[SYMBOLS + 1] = "[]+^|";

static u32 func_at(u32 col)
{
	u32 k = FUNCS - 1;
	while (k && func_col[k] > col)
		k--;
	return k;
}

// --- State ------------------------------------------------------------------------

enum {
	WANT_NONE,
	WANT_SAVE,
	WANT_CANCEL,
};

static struct {
	const struct keypad_ask *ask;
	struct keypad_edit e;
	struct keypad_check check;
	struct cheat_op first;  // the code's first op, once it reads
	u32 orig;               // the code it started with, and its format: Cancel asks only after a change
	u8 format;              // enum cheat_format: CHEAT_FMT_AUTO, or SELECT's
	u8 row, col;            // the selection (on the function row, the column it came down in)
	u8 symbols;             // the digits' rows show the symbols' one
	u8 only;                // the field's next paint: this line alone (0xFF: all of them)
	u8 keys_later;          // the keys to paint on the next frame (the field took this one)
	u8 drawn_line, drawn_lines, drawn_top;  // the caret's line, the lines and the top as last painted
	u8 top;                 // the first line in view
	u8 first_node;
	u8 done, result, want;
	u8 message;  // frames the hint area's refusal stays
	u8 armed;    // A (1) and B (2) pressed here: they repeat while held
	u8 logged_status, logged_format;  // what recheck last logged: a line only when they change
	u32 down_at[2];  // frame_count() when each went down
	u32 repeats[2];  // and the repeats it has had
	char scratch[KEYPAD_CODE_MAX + 1];
	char counter[32];
	char msg[48];
	struct ui_hint hints[4];
} kp EWRAM_BSS;

static u8 node(u32 n)
{
	return (u8)(kp.first_node + n);
}

// The code as it is now, with its format
static u32 code_hash(void)
{
	u32 len = keypad_edit_code(&kp.e, kp.scratch, sizeof(kp.scratch));
	u32 h = 2166136261u;  // FNV-1a
	for (u32 i = 0; i < len; i++)
		h = (h ^ (u8)kp.scratch[i]) * 16777619u;
	return (h ^ kp.format) * 16777619u;
}

// --- What the code reads as ----------------------------------------------------------

// A value at its width
static void value_text(const struct cheat_op *op, char *out, u32 size)
{
	int digits = op->width ? (int)op->width * 2 : 2;
	snprintf(out, size, "%0*lX", digits, (unsigned long)op->value);
}

// An op's verb ("Write", "If")
static const char *verb_of(u32 type)
{
	if (type <= CHEAT_OP_AND)
		return S(STR_KEYPAD_VERB_WRITE + type - CHEAT_OP_WRITE);
	switch (type) {
	case CHEAT_OP_IF_NEVER:
		return S(STR_KEYPAD_VERB_NEVER);
	case CHEAT_OP_ROM_PATCH:
		return S(STR_KEYPAD_VERB_PATCH);
	case CHEAT_OP_SLOWDOWN:
		return S(STR_KEYPAD_VERB_SLOWDOWN);
	case CHEAT_OP_IF_BUTTON:
		return "";
	case CHEAT_OP_KEYED:  // describe_op tells its kinds apart
		return S(STR_KEYPAD_VERB_WRITE);
	default:
		return type >= CHEAT_OP_IF_EQ && type <= CHEAT_OP_IF_NAND ? S(STR_KEYPAD_VERB_IF) : "";
	}
}

_Static_assert(STR_KEYPAD_VERB_AND - STR_KEYPAD_VERB_WRITE == CHEAT_OP_AND - CHEAT_OP_WRITE, "keypad's verbs in order");
_Static_assert(STR_KEYPAD_FORMAT_VBA - STR_KEYPAD_FORMAT_NONE == CHEAT_FMT_VBA, "keypad's formats in order");
_Static_assert(STR_KEYPAD_TAG_VBA - STR_KEYPAD_TAG_NONE == CHEAT_FMT_VBA, "keypad's tags in order");
_Static_assert(STR_KEYPAD_STATUS_CANT_RUN - STR_KEYPAD_STATUS_ADDRESS == KEYPAD_CANT_RUN - KEYPAD_ADDRESS,
	       "keypad's results in order");

// The code's first op in words
static void describe_op(const struct cheat_op *op, char *out, u32 size)
{
	static const char *const symbols[] = {"=", "!=", "<", ">", "<", ">", "&", "&!"};
	char value[12];
	value_text(op, value, sizeof(value));
	const char *verb = verb_of(op->type);
	char at[24];
	if (op->type == CHEAT_OP_WRITE_PTR || (op->type == CHEAT_OP_KEYED && (op->flags & CHEAT_KEYED_TARGET_PTR)))
		lang_fill(at, sizeof(at), "[{1}]+{2}", LX(op->address, 8), LX((u32)op->addr_step, 1));
	else
		lang_fill(at, sizeof(at), "{1}", LX(op->address, 8));
	switch (op->type) {
	case CHEAT_OP_WRITE:
		if (op->count > 1)
			SF(out, size, STR_KEYPAD_FILL, LS(verb), LS(value), LS(at), LN(op->count));
		else
			SF(out, size, STR_KEYPAD_OP, LS(verb), LS(value), LS(at));
		break;
	case CHEAT_OP_IF_EQ:
	case CHEAT_OP_IF_NE:
	case CHEAT_OP_IF_LT:
	case CHEAT_OP_IF_GT:
	case CHEAT_OP_IF_ULT:
	case CHEAT_OP_IF_UGT:
	case CHEAT_OP_IF_AND:
	case CHEAT_OP_IF_NAND:
		SF(out, size, STR_KEYPAD_IF, LS(verb), LS(at), LS(symbols[op->type - CHEAT_OP_IF_EQ]), LS(value));
		break;
	case CHEAT_OP_IF_BUTTON:
		snprintf(out, size, "%s", S(STR_KEYPAD_BUTTON));
		break;
	case CHEAT_OP_IF_NEVER:
	case CHEAT_OP_SLOWDOWN:
		snprintf(out, size, "%s", verb);
		break;
	case CHEAT_OP_WRITE_PTR:  // the pointer and its offset say "via pointer"
		SF(out, size, STR_KEYPAD_OP, LS(S(STR_KEYPAD_VERB_WRITE)), LS(value), LS(at));
		break;
	case CHEAT_OP_KEYED:
		SF(out, size, STR_KEYPAD_OP,
		   LS(op->flags & CHEAT_KEYED_KEY  ? S(STR_KEYPAD_VERB_KEYED)
		      : op->flags & CHEAT_KEYED_OR ? S(STR_KEYPAD_VERB_OR)
						   : S(STR_KEYPAD_VERB_WRITE)),
		   LS(value), LS(at));
		break;
	default:  // add, or, and, ROM patches
		SF(out, size, STR_KEYPAD_OP, LS(verb), LS(value), LS(at));
		break;
	}
}

// The result line, and its ink
static u32 result_text(char *out, u32 size)
{
	const struct keypad_check *c = &kp.check;
	switch (c->status) {
	case KEYPAD_OK: {
		char op[56];
		describe_op(&kp.first, op, sizeof(op));
		if (c->ops > 1)
			SF(out, size, STR_KEYPAD_MORE, LS(op), LN(c->ops - 1));
		else
			snprintf(out, size, "%s", op);
		return UI_ROLE_TEXT;
	}
	case KEYPAD_SHAPE:
		SF(out, size, STR_KEYPAD_SHAPE, LN(c->line + 1), LN(kp.e.len[c->line]));
		return c->line == kp.e.line ? UI_ROLE_DIM : UI_ROLE_DANGER;  // the line being typed: not done yet
	case KEYPAD_FILL:
		SF(out, size, STR_KEYPAD_FILL_IN, LN(keypad_edit_marks(&kp.e)));
		return UI_ROLE_DIM;
	case KEYPAD_WRONG:
		SF(out, size, STR_KEYPAD_WRONG,
		   LS(S(STR_KEYPAD_FORMAT_NONE + (c->format <= CHEAT_FMT_VBA ? c->format : 0))));
		return UI_ROLE_DANGER;
	case KEYPAD_EMPTY:
		snprintf(out, size, "%s", S(STR_KEYPAD_STATUS_EMPTY));
		return UI_ROLE_DIM;
	default:
		snprintf(out, size, "%s",
			 c->status >= KEYPAD_ADDRESS && c->status < KEYPAD_STATUSES
				 ? S(STR_KEYPAD_STATUS_ADDRESS + c->status - KEYPAD_ADDRESS)
				 : "");
		return UI_ROLE_DANGER;
	}
}

// The format's line: SELECT's, else as detected
static void format_text(char *out, u32 size)
{
	u32 f = kp.check.format <= CHEAT_FMT_VBA ? kp.check.format : 0;
	if (kp.format != CHEAT_FMT_AUTO)
		SF(out, size, STR_KEYPAD_SET, LS(S(STR_KEYPAD_FORMAT_NONE + kp.format)));
	else if (f == CHEAT_FMT_AUTO)
		snprintf(out, size, "%s", S(STR_KEYPAD_FORMAT_NONE));
	else if (f <= CHEAT_FMT_PARV3_RAW && kp.check.score < 0x100)
		SF(out, size, STR_KEYPAD_GUESS, LS(S(STR_KEYPAD_FORMAT_NONE + f)));  // by the plausibility score
	else
		SF(out, size, STR_KEYPAD_DETECTED, LS(S(STR_KEYPAD_FORMAT_NONE + f)));
}

// A format's name in the log: the file's, or "auto"
static const char *format_name(u32 f)
{
	return f >= CHEAT_FMT_GSAV1 && f <= CHEAT_FMT_VBA ? cheatfile_format_names[f] : "auto";
}

static const char *const status_names[KEYPAD_STATUSES] = {
	"empty", "ok", "shape", "fill", "wrong", "address", "master", "big", "can't run",
};

// Checks the code again
static void recheck(void)
{
	struct cheatstore *cs = cheatstore();
	keypad_check(&kp.e, kp.format, kp.ask->ctx, &cs->work, kp.scratch, &kp.check);
	if (kp.check.status == KEYPAD_OK && kp.check.ops)
		kp.first = cs->work.ops[0];
	// Only when what the code reads as changes, not a line per digit: the
	// card's log filled with them (v0.8's cart test)
	if (kp.check.status == kp.logged_status && kp.check.format == kp.logged_format)
		return;
	kp.logged_status = (u8)kp.check.status;
	kp.logged_format = (u8)kp.check.format;
	tlog_debug("keypad: %s: %s as %s", kp.scratch,
		   kp.check.status < KEYPAD_STATUSES ? status_names[kp.check.status] : "?",
		   format_name(kp.check.format));
}

// --- Painting ---------------------------------------------------------------------

// A line's format tag: its shape's
static const char *line_tag(u32 i)
{
	u8 shape = cheat_detect(kp.e.text[i], kp.e.len[i], NULL);
	return S(STR_KEYPAD_TAG_NONE + (shape <= CHEAT_FMT_VBA ? shape : 0));
}

// Text from x with its line's top at y, clipped to right; where it ended
static u32 words(const struct text_window *win, u32 x, u32 y, u32 right, u32 ink, const char *s)
{
	return text_draw(win, ui_theme()->body, x, y, right > x ? right - x : 0, ink, s, strlen(s));
}

// A hex digit (the rest a line holds are symbols, stand-ins and spaces)
static int is_digit_char(char c)
{
	return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
}

// Code line i at y: its number, its characters (stand-ins marked), the
// caret on the caret's line, and its tag
static void paint_line(const struct text_window *win, u32 ox, u32 i, u32 y)
{
	const struct torii_font *font = ui_theme()->body;
	struct paint_label lb;
	char number[4];
	snprintf(number, sizeof(number), "%lu", (unsigned long)i + 1);
	paint_label_text(font, number, &lb);
	paint_blit(win, font, look.number_right - ox - lb.advance, y, UI_ROLE_FAINT, &lb);
	char shown[KEYPAD_LINE_MAX + 2];
	u32 caret = 0xFF;
	u32 n = keypad_edit_show(&kp.e, i, shown, &caret);
	struct paint_label tag;
	paint_label_text(font, line_tag(i), &tag);
	u32 right = look.tag_right - ox - tag.advance - look.tag_gap;
	// A line too long for the card: the caret's scrolls to keep it in view,
	// the others show their start
	u32 first = 0;
	if (i == kp.e.line && caret != 0xFF) {
		u32 w = 0;
		for (u32 c = caret; c > 0; c--) {
			paint_label_char(font, (u8)shown[c - 1], &lb);
			if (look.text_x - ox + w + lb.advance + look.caret_room > right) {
				first = c;
				break;
			}
			w += lb.advance;
		}
	}
	// Characters a run of one ink to a blit: digits in the text's, the
	// symbols between them dim (AnyGBA's '|' reads apart from a '1'), a
	// stand-in on its own pill
	u32 x = look.text_x - ox, caret_x = x, run_x = x, run_ink = UI_ROLE_TEXT;
	struct paint_label run;
	run.count = 0;
	run.advance = 0;
	for (u32 c = first; c <= n; c++) {
		if (c == caret)
			caret_x = x;
		if (c == n)
			break;
		paint_label_char(font, (u8)shown[c], &lb);
		if (x + lb.advance > right)
			break;
		int mark = keypad_is_mark(shown[c]);
		u32 ink = keypad_edit_key(shown[c]) && !is_digit_char(shown[c]) ? UI_ROLE_DIM : UI_ROLE_TEXT;
		if (mark || ink != run_ink || run.count == PAINT_LABEL_MAX) {
			paint_blit(win, font, run_x, y, run_ink, &run);
			run.count = 0;
			run.advance = 0;
		}
		if (mark) {
			paint_round_fill(win, x - look.mark_pad, y + 1, lb.advance + 2u * look.mark_pad,
					 look.line_h - 1, 2, UI_ROLE_TINT);
			paint_blit(win, font, x, y, UI_ROLE_ACCENT_INK, &lb);
		} else {
			if (!run.count) {
				run_x = x;
				run_ink = ink;
			}
			run.glyphs[run.count++] = lb.glyphs[0];
			run.advance = (u8)(run.advance + lb.advance);
		}
		x += lb.advance;
	}
	paint_blit(win, font, run_x, y, run_ink, &run);
	if (i == kp.e.line)
		paint_fill(win, caret_x > 0 ? caret_x - 1 : 0, y + look.caret_dy, look.caret_w, look.caret_h,
			   UI_ROLE_ACCENT);
	paint_blit(win, font, look.tag_right - ox - tag.advance, y, UI_ROLE_FAINT, &tag);
}

// The field: its card once a set, then the lines in view, the format and the
// result
static void paint_field(void *ctx, u8 nd, const struct text_window *win, u32 set, int fresh)
{
	(void)ctx;
	(void)set;
	const struct ui_theme *t = ui_theme();
	u32 ox = (u32)ui_node(nd)->x, oy = (u32)ui_node(nd)->y;
	if (fresh)
		paint_card_fill(win, look.card_x - ox, look.card_y - oy, look.card_w, look.card_h, t->look.card_radius,
				t->look.shadow_offset, UI_ROLE_PANEL);
	u32 top = look.line_y - oy;
	if (!fresh && kp.only < kp.e.lines) {  // a change within the caret's line: it alone
		u32 y = top + (kp.only - kp.top) * look.line_h;
		text_clear(win, look.card_x + 2 - ox, y, look.card_w - 4, look.line_h, UI_ROLE_PANEL);
		paint_line(win, ox, kp.only, y);
	} else {
		text_clear(win, look.card_x + 2 - ox, top, look.card_w - 4, VISIBLE * look.line_h, UI_ROLE_PANEL);
		for (u32 k = 0; k < VISIBLE && kp.top + k < kp.e.lines; k++)
			paint_line(win, ox, kp.top + k, top + k * look.line_h);
	}
	// Under the card, on the page
	u32 info = look.format_y - oy, right = 240 - look.info_x - ox;
	text_clear(win, 0, info, 240, FIELD_Y + FIELD_H - look.format_y, 0);
	char text[80];
	u32 x = words(win, look.info_x - ox, info, right, UI_ROLE_DIM, S(STR_KEYPAD_FORMAT_LABEL));
	format_text(text, sizeof(text));
	words(win, x + look.label_gap, info, right, UI_ROLE_TEXT, text);
	u32 ink = result_text(text, sizeof(text));
	words(win, look.info_x - ox, look.result_y - oy, right, ink, text);
}

// A word centred in a key's cell
static void key_word(const struct text_window *win, u32 x, u32 y, u32 w, const char *word, u32 role)
{
	struct paint_label lb;
	paint_label_text(ui_theme()->body, word, &lb);
	paint_blit_centred(win, ui_theme()->body, x, y + look.func_text_dy, w, role, &lb);
}

// The keys: all of them, on each change
static void paint_keys(void *ctx, u8 nd, const struct text_window *win, u32 set, int fresh)
{
	(void)ctx;
	(void)set;
	const struct torii_font *heading = ui_theme()->heading;
	u32 ox = (u32)ui_node(nd)->x, oy = (u32)ui_node(nd)->y, in = look.key_inset;
	if (!fresh)
		text_clear(win, look.grid_x - ox, look.grid_y - oy, COLS * look.col_w, ROWS * look.pitch, 0);
	for (u32 r = 0; r < HEX_ROWS; r++)
		for (u32 c = 0; c < COLS; c++) {
			char ch = kp.symbols ? (r == 0 && c < SYMBOLS ? symbol_keys[c] : 0) : hex[r * COLS + c];
			if (!ch)
				continue;
			u32 x = look.grid_x + c * look.col_w - ox, y = look.grid_y + r * look.pitch - oy;
			if (kp.row == r && kp.col == c)
				paint_round_fill(win, x + in, y + in, look.col_w - 2 * in, look.pitch - 2 * in,
						 look.key_radius, UI_ROLE_TINT);
			struct paint_label lb;
			paint_label_char(heading, (u8)ch, &lb);
			paint_blit_centred(win, heading, x, y, look.col_w, UI_ROLE_TEXT, &lb);
		}
	u32 chosen = kp.row == FUNC_ROW ? func_at(kp.col) : FUNCS;
	u32 y = look.grid_y + FUNC_ROW * look.pitch - oy, h = look.pitch;
	for (u32 k = 0; k < FUNCS; k++) {
		u32 x = look.grid_x + func_col[k] * look.col_w - ox, w = func_span[k] * look.col_w;
		int on = k == chosen;
		if (k == KEY_SAVE) {
			u32 i = on ? look.save_inset : in;
			if (on)
				paint_round_fill(win, x, y, w, h, look.save_radius, UI_ROLE_TEXT);
			paint_round_fill(win, x + i, y + i, w - 2 * i, h - 2 * i, look.key_radius, UI_ROLE_ACCENT);
			key_word(win, x, y, w, S(STR_KEYPAD_KEY_SAVE), UI_ROLE_ACCENT_TEXT);
			continue;
		}
		paint_round_fill(win, x + in, y + in, w - 2 * in, h - 2 * in, look.key_radius,
				 on ? UI_ROLE_TINT : UI_ROLE_BG);
		if (k == KEY_COLON) {
			struct paint_label lb;
			paint_label_char(heading, ':', &lb);
			paint_blit_centred(win, heading, x, y, w, UI_ROLE_TEXT, &lb);
		} else if (k == KEY_SYMBOLS) {
			key_word(win, x, y, w, kp.symbols ? S(STR_KEYPAD_KEY_DIGITS) : S(STR_KEYPAD_KEY_SYMBOLS),
				 UI_ROLE_TEXT);
		} else {
			key_word(win, x, y, w,
				 k == KEY_LINE     ? S(STR_KEYPAD_KEY_LINE)
				 : k == KEY_DELETE ? S(STR_KEYPAD_KEY_DELETE)
						   : S(STR_KEYPAD_KEY_CANCEL),
				 UI_ROLE_DIM);
		}
	}
}

// --- The layout ---------------------------------------------------------------------

enum {
	N_TITLE,
	N_HINTS,
	N_CARD,
	N_FIELD,
	N_KEYS,
	N_NODES
};

static const struct ui_title_config title_config = {"", 0, ART_ICON16_GBA};  // the icon: the caller's
static const struct ui_hints_config no_hints = {0, 0};
// What a whole paint of each costs, about (frame-cycles: keypad-key)
#define FIELD_PAINT_CYCLES 150000  // three lines of up to 30 characters
#define KEYS_PAINT_CYCLES 70000
static const struct ui_canvas_config field_canvas = {paint_field, FIELD_PAINT_CYCLES};
static const struct ui_canvas_config keys_canvas = {paint_keys, KEYS_PAINT_CYCLES};

static const struct ui_spec nodes[N_NODES] = {
	[N_TITLE] = {UI_TITLE, 1, UI_NONE, 3, UI_HFILL, 0, 0, 0, 0, 0, 0, TITLE_H, &title_config},
	[N_HINTS] = {UI_HINTS, 2, UI_NONE, 3, UI_HFILL | UI_BOTTOM, 0, 0, 0, 0, 0, 0, HINTS_H, &no_hints},
	[N_CARD] = {UI_CARD, 3, UI_NONE, 2, UI_HFILL, 0, 0, 0, 0, KEYS_Y, 0, KEYS_H, &keys_card},
	[N_FIELD] = {UI_CANVAS, 4, UI_NONE, 1, UI_HFILL, 0, 0, 0, 0, FIELD_Y, 0, FIELD_H, &field_canvas},
	[N_KEYS] = {UI_CANVAS, 5, UI_NONE, 1, UI_LEFT, 0, 0, 0, KEYS_X, KEYS_Y, KEYS_W, KEYS_H, &keys_canvas},
};

static int keypad_event(struct ui_view *v, const struct ui_event *e);
static const struct ui_view_spec view = {"keypad", nodes, N_NODES, UI_NONE, 0, keypad_event};

// --- Changes ------------------------------------------------------------------------

static void show_hints(void)
{
	// A types: the keys say so; four chips don't fit
	kp.hints[0] = (struct ui_hint){"B", S(STR_KEYPAD_HINT_DELETE)};
	kp.hints[1] = (struct ui_hint){"SELECT", S(STR_KEYPAD_HINT_FORMAT)};
	kp.hints[2] = (struct ui_hint){"START", S(STR_KEYPAD_HINT_SAVE)};
	ui_hints_set(node(N_HINTS), kp.hints, 3);
}

// A refusal in the hint area, in danger ink, for look.message frames
static void message(const char *text)
{
	static const struct ui_hint_note danger = {UI_ROLE_DANGER, 0, 0, 0, 0, 0, 0};
	snprintf(kp.msg, sizeof(kp.msg), "%s", text);
	ui_hints_note(node(N_HINTS), kp.msg, &danger);
	kp.message = look.message;
	tlog_debug("keypad: %s", kp.msg);
}

// The code or its format changed: checked again, the caret's line in view
static void code_changed(void)
{
	if (kp.e.line < kp.top)
		kp.top = kp.e.line;
	else if (kp.e.line >= kp.top + VISIBLE)
		kp.top = (u8)(kp.e.line - VISIBLE + 1);
	// Typing changes the caret's line alone: it alone is painted again
	int same = kp.e.line == kp.drawn_line && kp.e.lines == kp.drawn_lines && kp.top == kp.drawn_top;
	kp.only = same ? kp.e.line : 0xFF;
	kp.drawn_line = kp.e.line;
	kp.drawn_lines = kp.e.lines;
	kp.drawn_top = kp.top;
	recheck();
	u32 expect;
	u32 digits = keypad_edit_digits(&kp.e, kp.e.line, kp.format, &expect);
	// The title's corner holds about 9 characters: the digits while the
	// line is being typed, else the line
	if (digits && digits < expect)
		SF(kp.counter, sizeof(kp.counter), STR_KEYPAD_COUNTER_DIGITS, LN(digits), LN(expect));
	else
		SF(kp.counter, sizeof(kp.counter), STR_KEYPAD_COUNTER, LN(kp.e.line + 1), LN(kp.e.lines));
	ui_title_set_right(node(N_TITLE), kp.counter);
	ui_canvas_redraw(node(N_FIELD));
}

static void keys_changed(void)
{
	ui_canvas_redraw(node(N_KEYS));
}

// The keys' columns on row r: the symbol row's fewer, its second row none
static u32 row_cols(u32 r)
{
	if (r == FUNC_ROW || !kp.symbols)
		return COLS;
	return r == 0 ? SYMBOLS : 0;
}

// The selection moved by a d-pad key: a fresh press wraps at the edges
static void move(u16 key, int fresh)
{
	u32 row = kp.row, col = kp.col;
	if (key == INPUT_UP || key == INPUT_DOWN) {
		s32 to = (s32)row;
		do {
			to += key == INPUT_UP ? -1 : 1;
			if (to < 0 || to >= ROWS) {
				if (!fresh)
					return;
				to = to < 0 ? ROWS - 1 : 0;
			}
		} while (!row_cols((u32)to));
		row = (u32)to;
		if (col >= row_cols(row))
			col = row_cols(row) - 1;
	} else if (row == FUNC_ROW) {
		s32 k = (s32)func_at(col) + (key == INPUT_LEFT ? -1 : 1);
		if (k < 0 || k >= FUNCS) {
			if (!fresh)
				return;
			k = k < 0 ? FUNCS - 1 : 0;
		}
		col = func_col[k];
	} else {
		s32 to = (s32)col + (key == INPUT_LEFT ? -1 : 1), cols = (s32)row_cols(row);
		if (to < 0 || to >= cols) {
			if (!fresh)
				return;
			to = to < 0 ? cols - 1 : 0;
		}
		col = (u32)to;
	}
	kp.row = (u8)row;
	kp.col = (u8)col;
	keys_changed();
}

static void type(char c)
{
	if (keypad_edit_type(&kp.e, c))
		code_changed();
	else if (c == '+')
		message(S(STR_KEYPAD_PLUS));  // only a pointer's offset: Line starts a line
}

// The digits' rows, or the symbols' row (the selection kept on a key);
// later: painted the frame after, as the field is painted on this one
static void show_symbols(int on, int later)
{
	kp.symbols = (u8)on;
	if (kp.row < HEX_ROWS && !row_cols(kp.row))
		kp.row = 0;
	if (kp.row < HEX_ROWS && kp.col >= row_cols(kp.row))
		kp.col = (u8)(row_cols(kp.row) - 1);
	if (later)
		kp.keys_later = 1;
	else
		keys_changed();
}

static void delete_back(int fresh)
{
	if (keypad_edit_delete(&kp.e))
		code_changed();
	else if (fresh && !kp.e.len[0] && kp.e.lines == 1)
		kp.want = WANT_CANCEL;  // an empty code: B leaves
}

// L+B: every line goes, the format stays (Cancel still asks before the
// code it started with is lost)
static void clear_all(void)
{
	if (kp.e.lines == 1 && !kp.e.len[0])
		return;
	keypad_edit_init(&kp.e, "", 0);
	tlog_debug("keypad: cleared");
	code_changed();
}

// The next format SELECT gives
static void step_format(void)
{
	kp.format = (u8)((kp.format + 1) % (CHEAT_FMT_VBA + 1));
	code_changed();
}

// A on the selected key (fresh: pressed, not a repeat)
static void press_key(int fresh)
{
	if (kp.row < HEX_ROWS && kp.symbols) {
		char c = symbol_keys[kp.col];
		type(c);
		// Digits come after '[', '+' and '|'; ']' and '^' are followed by more symbols as often
		if (c == '[' || c == '+' || c == '|')
			show_symbols(0, 1);
		return;
	}
	if (kp.row < HEX_ROWS) {
		type(hex[kp.row * COLS + kp.col]);
		return;
	}
	switch (func_at(kp.col)) {
	case KEY_COLON:
		type(':');
		break;
	case KEY_SYMBOLS:
		if (fresh)
			show_symbols(!kp.symbols, 0);
		break;
	case KEY_LINE:
		if (fresh && keypad_edit_break(&kp.e))
			code_changed();
		break;
	case KEY_DELETE:
		delete_back(0);
		break;
	case KEY_CANCEL:
		if (fresh)
			kp.want = WANT_CANCEL;
		break;
	default:
		if (fresh)
			kp.want = WANT_SAVE;
		break;
	}
}

static void act(u16 key, int fresh)
{
	switch (key) {
	case INPUT_UP:
	case INPUT_DOWN:
	case INPUT_LEFT:
	case INPUT_RIGHT:
		move(key, fresh);
		break;
	case INPUT_A:
		press_key(fresh);
		break;
	case INPUT_B:
		// L+B: the whole code goes (L held; its own press moved a line)
		if (fresh && (ui_input()->held & INPUT_L)) {
			clear_all();
			break;
		}
		delete_back(fresh);
		break;
	case INPUT_L:
	case INPUT_R:
		if (keypad_edit_line(&kp.e, key == INPUT_L ? -1 : 1))
			code_changed();
		break;
	case INPUT_SELECT:
		if (fresh)
			step_format();
		break;
	case INPUT_START:
		if (fresh)
			kp.want = WANT_SAVE;
		break;
	default:
		break;
	}
	frame_measure("keypad-key");
}

// The keys that repeat while held, on the keypad's own schedule (the
// engine repeats only the d-pad)
static const u16 held_keys[2] = {INPUT_A, INPUT_B};

static int keypad_event(struct ui_view *v, const struct ui_event *e)
{
	(void)v;
	if (e->type != UI_EV_KEYS)
		return 0;
	if (kp.want || kp.done)
		return 1;
	u16 fresh = ui_input()->pressed;
	for (u32 b = 0; b < INPUT_KEYS; b++) {
		u16 key = (u16)(1u << b);
		if (!(e->keys & key))
			continue;
		act(key, (fresh & key) != 0);
		for (u32 i = 0; i < 2; i++)
			if ((fresh & key) && key == held_keys[i]) {
				kp.armed |= (u8)(1u << i);
				kp.down_at[i] = frame_count();
				kp.repeats[i] = 0;
			}
	}
	return 1;
}

// --- The loop -----------------------------------------------------------------------

static void save(void)
{
	if (kp.check.status != KEYPAD_OK) {
		message(S(STR_KEYPAD_CANT_SAVE));
		return;
	}
	kp.done = kp.result = 1;
}

static void cancel(void)
{
	if (code_hash() != kp.orig) {
		const struct ui_confirm ask = {S(STR_KEYPAD_DISCARD), NULL, S(STR_KEYPAD_DISCARD_BUTTON), 1};
		if (!ui_confirm(node(N_HINTS), &ask))
			return;
	}
	kp.done = 1;
}

// Each frame after ui_frame: a refusal's time, and Save or Cancel (which may
// ask)
static void tick(struct ui_view *v)
{
	if (ui_top() != v || ui_transitioning())
		return;
	if (kp.message) {
		kp.message--;
		if (!kp.message)
			show_hints();
	}
	if (kp.keys_later) {
		kp.keys_later = 0;
		keys_changed();
	}
	// A and B repeat while held: a digit typed again, the code deleted back
	const InputState *in = ui_input();
	for (u32 i = 0; i < 2 && !kp.want; i++) {
		if (!(in->held & held_keys[i]))
			kp.armed &= (u8) ~(1u << i);
		if (!(kp.armed & (1u << i)))
			continue;
		u32 f = frame_count() - kp.down_at[i];
		u32 due = f < look.repeat_delay ? 0 : 1 + (f - look.repeat_delay) / look.repeat_every;
		if (due > kp.repeats[i]) {
			kp.repeats[i] = due;
			act(held_keys[i], 0);
		}
	}
	u8 want = kp.want;
	kp.want = WANT_NONE;
	if (want == WANT_SAVE)
		save();
	else if (want == WANT_CANCEL)
		cancel();
}

int keypad_run(const struct keypad_ask *ask, char *out, u8 *format)
{
	memset(&kp, 0, sizeof(kp));
	kp.logged_status = 0xFF;  // the first check is logged
	kp.ask = ask;
	if (!keypad_edit_init(&kp.e, ask->code ? ask->code : "", ask->code ? ask->code_len : 0)) {
		tlog("keypad: the code can't be edited here");
		return -1;
	}
	kp.format = ask->format <= CHEAT_FMT_VBA ? ask->format : CHEAT_FMT_AUTO;
	kp.orig = code_hash();
	ui_leave(UI_TR_DEEPER, 1);
	while (ui_depth())
		ui_pop();
	struct ui_view *v = ui_push(&view, &kp);
	if (!v) {
		tlog_error("keypad: %s", ui_error());
		return 0;
	}
	kp.first_node = v->first;
	ui_title_set(node(N_TITLE), ask->title);
	ui_title_icon_set(node(N_TITLE), ask->icon16);
	show_hints();
	code_changed();
	keys_changed();
	tlog_debug("keypad: from %s", kp.scratch);
	// ui_run logs "screen ready: keypad" once it is on screen and still
	while (!kp.done && ui_depth()) {
		ui_frame();
		tick(v);
	}
	ui_leave(UI_TR_DEEPER, -1);
	while (ui_depth())
		ui_pop();
	if (kp.result) {
		keypad_edit_code(&kp.e, out, KEYPAD_CODE_MAX + 1);
		*format = kp.format;
		tlog("keypad: saved %s, format %s", out, format_name(kp.format));
	} else {
		keypad_edit_code(&kp.e, kp.scratch, sizeof(kp.scratch));
		tlog("keypad: cancelled %s", kp.scratch);
	}
	return kp.result;
}
