// The Settings page (settings_page.h).
//
// The page is one list (ui_list) of 12 px slots, settings_layout.h's: a
// section's label, its rows and a gap under its card. The model paints each
// slot whole (ui_list_model.paint): the card's part of the slot (its rounded
// top in the label's slot, its body behind the rows, its bottom and shadow
// in the gap), the selection pill, the label and the value, so the cards,
// their edges and the section labels scroll with the rows as one plane, by
// the pixel, and a row or card the view cuts is clipped by the list's window
// like any list row. The pill is drawn into the selected row (the cards are
// on the list's own plane, so a sprite pill behind it wouldn't show); the
// LED colours' swatches are sprites (ui_list_model.objs) in a palette bank
// of their own, as the BG banks have no room for seven LED colours.
//
// The values are a working copy (struct settings_values, here in EWRAM).
// The theme and the LED show an edit at once (ui_theme_switch, Set_LED_control)
// without staging it. A confirmed value (A on an edit, a hotkey accepted)
// is written soon if it changed: the hint bar says "Saving..." and, once
// that is on screen and the page is still (no scroll, no theme wipe), the
// copy is staged and committed in one go, one NOR write through Save_info,
// the page staying up. Keys act at once meanwhile; the write never runs
// from a key, so it never stalls a move. B waits for it: the write runs,
// then the page goes, so leaving never pauses on black. An edit open as it
// writes goes in with its value from before A. An edit B cancels is put
// back with its preview and writes nothing; so is one the page leaves.
#include <stdio.h>
#include <string.h>

#include "settings_page.h"
#include "settings_layout.h"
#include "../Ezcard_OP.h"
#include "../lang.h"
#include "../library.h"
#include "../namesdb.h"
#include "saves.h"
#include "../settings.h"
#include "../tlog.h"
#include "../engine/frame.h"
#include "../engine/gfx.h"
#include "../engine/res.h"
#include "../engine/text.h"
#include "../ui/art_ids.h"
#include "../ui/ui.h"

// --- Look (mockups-b16-b18.md, "Settings page"), px and frames ----------------
// The layout's rectangles (constant expressions for the node table): the
// title bar, the list's viewport (10 slots of 12 px, under the title bar to
// above the hint bar) and its width (to the scroll bar's column), the scroll
// bar's column, the hint bar
#define TITLE_H 24
#define VIEW_Y 24
#define VIEW_H 120
#define LIST_W 232
#define BAR_X 224
#define BAR_Y 24
#define BAR_H 120
#define HINTS_H 16

static const struct settings_look {
	u8 card_x, card_w;   // a section's card
	u8 card_pad;         // above its first row and below its last
	u8 label_skip;       // a section label's line top, this far above its slot's top
	u8 icon_x;           // the label's 8x8 icon (on the line's caps)
	u8 name_x;           // its name
	u8 text_x;           // a row's label
	u8 value_r;          // where values end (exclusive)
	u8 pill_x, pill_w;   // the selection pill (the theme's height and radius)
	u8 mark_gap;         // between « » and the value they hold
	u8 chip_gap;         // between a hotkey's key chips
	u8 chip_min;         // a key chip's narrowest
	u8 swatch_w, swatch_h, swatch_r;  // an LED colour's swatch
	u8 swatch_gap;       // between swatches while edited
	u8 swatch_text_gap;  // between a swatch and its colour's name
	u8 chevron_gap;      // an action's chevron, this far in from the value edge
	u8 chevron_w, chevron_h;
	u8 bar_w, bar_h, bar_r, bar_gap;  // the capture's countdown in the hint bar
	u8 hold;             // frames a hotkey's keys are held to count
	u16 capture_wait;    // frames with nothing pressed before a capture gives up
	u8 refusal;          // frames a refused hotkey's reason shows
	u8 clock_poll;       // frames between clock reads (the Time and Date rows)
	u8 save_wait;        // frames a write waits at most for the screen to settle
	u8 identify_poll;    // frames between looks at Identify library's progress
	u32 led[8];          // the swatches' colours by value (0: Off, drawn empty)
	u8 led_edge;         // a swatch's edge: its colour this much toward black, in 256ths
} look = {
	.card_x = 4,
	.card_w = 223,
	.card_pad = 2,
	.label_skip = 2,
	.icon_x = 8,
	.name_x = 20,
	.text_x = 13,
	.value_r = 218,
	.pill_x = 7,
	.pill_w = 216,
	.mark_gap = 3,
	.chip_gap = 2,
	.chip_min = 13,
	.swatch_w = 12,
	.swatch_h = 10,
	.swatch_r = 2,
	.swatch_gap = 2,
	.swatch_text_gap = 4,
	.chevron_gap = 4,
	.chevron_w = 3,
	.chevron_h = 5,
	.bar_w = 40,
	.bar_h = 3,
	.bar_r = 1,
	.bar_gap = 6,
	.hold = 18,
	.capture_wait = 300,
	.refusal = 90,
	.clock_poll = 30,
	.save_wait = 30,
	.identify_poll = 15,
	.led = {0, 0x3973E7, 0x39B54A, 0x29BDD6, 0xE73939, 0xD642C6, 0xEFC621, 0xFFFFFF},
	.led_edge = 72,  // 28 %
};

#define SWATCH_COLOURS 7  // the lit ones, 1-7
#define SWATCH_TILES 4    // a 16x16 sprite each
#define TAG_SWATCH 0x5530
#define TAG_SWATCH_PAL 0x5531
#define OBJ_SIZE_16 0x4000  // attr1: 16x16, with the square shape (attr0 0)
#define MARK_L "\xC2\xAB"  // «
#define MARK_R "\xC2\xBB"  // »
#define KEY_UPDOWN "^v"
#define KEY_LEFTRIGHT MARK_L MARK_R
#define LABEL_COLS 12
#define IDENTIFY_IDLE 0xFFFFFFFFu  // a section label's scratch, tile columns

enum {
	MODE_BROWSE,
	MODE_EDIT,     // a value edited in place
	MODE_CAPTURE,  // a hotkey's keys being pressed
};

static struct {
	struct settings_values v;  // the working copy
	struct settings_layout lay;
	struct settings_capture cap;
	struct settings_editor ed;  // the Time or Date field card's
	struct ui_fields fields;
	char field_text[UI_FIELDS_MAX][8];
	struct ui_hint field_keys[4];
	u8 first;     // the view's first node
	u8 live;      // entered and not left: what leave commits
	u8 cursor;    // the selected slot
	u8 mode;
	u8 edit_id;   // the row edited or captured
	u8 scanning;  // Rescan library runs
	u8 identify_wait;   // frames before Identify library's progress is looked at again
	u32 identify_done;  // its progress as drawn (games looked at), IDENTIFY_IDLE: not running
	u8 redraw;    // leave ended an edit: draw it all again if still up
	u8 saving;    // frames a confirmed change may still wait to be written, 0: none
	u8 leaving;   // B came while saving: the page goes once it's written
	u8 unsaved;   // the last write failed: its values are staged, not on the NOR
	u8 warned;    // B was refused once for it: the next B leaves, dropping them
	u8 swatch_bank;
	u16 swatch_tile;  // ART_NONE-like 0xFFFF: no swatches
	u16 refused_left;  // frames the refusal's reason stays
	u16 clock_wait;
	u8 bar_fill;  // the countdown as drawn
	char pos[12];
	char refused[48];
	char progress[16];  // Identify library's value while it runs: 12/176
	u16 edit_from[3];        // the edited row's slots as they were before A (B puts them back)
	struct ui_hint hints[4];
} sp EWRAM_BSS;

// The label scratch: a section's name is drawn here, then copied into its
// slot a few lines up (its line starts above the slot)
static u32 label_scratch[LABEL_COLS * 16] EWRAM_BSS ALIGN(4);

enum {
	N_TITLE,
	N_HINTS,
	N_BAR,   // before the list: drawn first, so the list's first draw keeps within its budget
	N_LIST,
	N_NODES
};

static u8 node(u32 n)
{
	return (u8)(sp.first + n);
}

static const struct settings_slot *slot_at(u32 s)
{
	return &sp.lay.slot[s < sp.lay.count ? s : 0];
}

static u32 cursor_id(void)
{
	return slot_at(sp.cursor)->id;
}

static int editing(u32 id)
{
	return sp.mode == MODE_EDIT && sp.edit_id == id;
}

// --- Painting ---------------------------------------------------------------

static void fill(const struct ui_paint *p, s32 x, u32 line, s32 w, u32 lines, u32 role)
{
	if (x < 0) {
		w += x;
		x = 0;
	}
	if (w > 0 && lines)
		text_clear(p->win, p->x + (u32)x, p->y + line, (u32)w, lines, role);
}

// A rounded rectangle of role at (x, y) in the row, w x h, its corners r
static void round_fill(const struct ui_paint *p, s32 x, s32 y, s32 w, s32 h, u32 r, u32 role)
{
	if (2 * r > (u32)h)
		r = (u32)h / 2;
	// Its core in one fill, every line; then the ends, a few px each (a
	// fill costs per tile column it crosses)
	s32 in0 = r ? (s32)ui_round_inset(r, 0) : 0, rr = (s32)r;
	fill(p, x + in0, (u32)y, w - 2 * in0, (u32)h, role);
	if (!in0)
		return;
	fill(p, x, (u32)(y + rr), in0, (u32)(h - 2 * rr), role);
	fill(p, x + w - in0, (u32)(y + rr), in0, (u32)(h - 2 * rr), role);
	for (u32 e = 1; e < r; e++) {
		s32 in = (s32)ui_round_inset(r, e);
		for (u32 side = 0; side < 2 && in < in0; side++) {
			u32 line = (u32)(side ? y + h - 1 - (s32)e : y + (s32)e);
			fill(p, x + in, line, in0 - in, 1, role);
			fill(p, x + w - in0, line, in0 - in, 1, role);
		}
	}
}

// Its 1 px outline: a pixel in from the outside at each row of the corners
// (wider where a corner turns), and the sides
static void round_line(const struct ui_paint *p, s32 x, s32 y, s32 w, s32 h, u32 r, u32 role)
{
	for (u32 e = 0; e < r; e++) {
		s32 in = (s32)ui_round_inset(r, e), out = e ? (s32)ui_round_inset(r, e - 1) : (w + 1) / 2;
		s32 len = out > in + 1 ? out - in : 1;
		for (u32 side = 0; side < 2; side++) {
			u32 row = (u32)(side ? y + h - 1 - (s32)e : y + (s32)e);
			fill(p, x + in, row, len, 1, role);
			fill(p, x + w - in - len, row, len, 1, role);
		}
	}
	fill(p, x, (u32)(y + (s32)r), 1, (u32)(h - 2 * (s32)r), role);
	fill(p, x + w - 1, (u32)(y + (s32)r), 1, (u32)(h - 2 * (s32)r), role);
}

// A section's card, in page lines (px from the page's top): its body
// [top, bottom), its corners' radius
struct card {
	s32 top, bottom, r;
};

static int card_of(u32 sec, u32 rh, struct card *c)
{
	if (sp.lay.first[sec] == SET_LAYOUT_NONE)
		return 0;
	c->top = sp.lay.first[sec] * (s32)rh - look.card_pad;
	c->bottom = (sp.lay.last[sec] + 1) * (s32)rh + look.card_pad;
	c->r = (s32)ui_theme()->look.card_radius;
	return 1;
}

// The body's inset on page line y, -1 if the line isn't in it
static s32 card_inset(const struct card *c, s32 y)
{
	if (y < c->top || y >= c->bottom)
		return -1;
	s32 e = y - c->top < c->bottom - 1 - y ? y - c->top : c->bottom - 1 - y;
	return e < c->r ? (s32)ui_round_inset((u32)c->r, (u32)e) : 0;
}

// Slot s's part of its section's card: the body in PANEL (its straight
// lines in one fill, its rounded ones a fill each), and the shadow under
// it, the body shadow_offset lines down where the body leaves it showing,
// as ui_paper's cards draw it
static void paint_card(const struct ui_paint *p, u32 s)
{
	struct card c;
	if (!card_of(slot_at(s)->section, p->rh, &c))
		return;
	s32 y0 = (s32)(s * p->rh), y1 = y0 + (s32)p->rh, sh = (s32)ui_theme()->look.shadow_offset;
	s32 x0 = look.card_x, w = look.card_w;
	s32 a = c.top > y0 ? c.top : y0, b = c.bottom < y1 ? c.bottom : y1;
	s32 sa = c.top + c.r > a ? c.top + c.r : a, sb = c.bottom - c.r < b ? c.bottom - c.r : b;
	if (sa < sb)
		fill(p, x0, (u32)(sa - y0), w, (u32)(sb - sa), UI_ROLE_PANEL);
	for (s32 y = a; y < b; y++) {
		if (y >= sa && y < sb)
			continue;
		s32 in = card_inset(&c, y);
		fill(p, x0 + in, (u32)(y - y0), w - 2 * in, 1, UI_ROLE_PANEL);
	}
	// The shadow shows only where the body narrows or ends: its last lines
	s32 from = c.bottom - c.r > y0 ? c.bottom - c.r : y0, to = c.bottom + sh < y1 ? c.bottom + sh : y1;
	for (s32 y = from; sh && y < to; y++) {
		s32 up = card_inset(&c, y - sh), in = card_inset(&c, y);
		if (up < 0)
			continue;
		if (in < 0) {
			fill(p, x0 + up, (u32)(y - y0), w - 2 * up, 1, UI_ROLE_SHADOW);
		} else if (in > up) {
			fill(p, x0 + up, (u32)(y - y0), in - up, 1, UI_ROLE_SHADOW);
			fill(p, x0 + w - in, (u32)(y - y0), in - up, 1, UI_ROLE_SHADOW);
		}
	}
}

static u32 extent(const char *s)
{
	struct text_line line;
	text_layout(ui_theme()->body, s, strlen(s), 240, &line);
	return line.extent;
}

// A laid-out line at x, its line's top at the row's
static void line_at(const struct ui_paint *p, s32 x, u32 ink, const struct text_line *line)
{
	if (x >= 0 && line->count)
		text_draw_line(p->win, ui_theme()->body, p->x + (u32)x, p->y, ink, line);
}

// Text at x, cut to clip px; returns the px it ends at
static s32 text_at(const struct ui_paint *p, s32 x, u32 clip, u32 ink, const char *s)
{
	struct text_line line;
	if (!s || !*s || x < 0)
		return x;
	text_layout(ui_theme()->body, s, strlen(s), clip, &line);
	line_at(p, x, ink, &line);
	return x + (s32)line.extent;
}

// Text ending at right (exclusive); returns where it starts
static s32 text_right(const struct ui_paint *p, s32 right, u32 ink, const char *s)
{
	struct text_line line;
	if (!s || !*s)
		return right;
	text_layout(ui_theme()->body, s, strlen(s), 240, &line);
	s32 x = right - (s32)line.extent;
	line_at(p, x, ink, &line);
	return x;
}

// A section's label: its 8x8 icon and name in the item's ink, the line's
// top label_skip px above the slot (the card starts at the slot's end)
static int paint_label(const struct ui_paint *p, u32 sec)
{
	static const u8 icons[SET_SECTIONS] = {
		[SET_SEC_LOOK] = ART_ICON8_LOOK,       [SET_SEC_CLOCK] = ART_ICON8_CLOCK,
		[SET_SEC_GAMES] = ART_ICON8_GAMES,     [SET_SEC_SAVES] = ART_ICON8_SAVE,
		[SET_SEC_CART] = ART_ICON8_CART,       [SET_SEC_HOTKEYS] = ART_ICON8_KEYS,
		[SET_SEC_LIBRARY] = ART_ICON8_LIBRARY, [SET_SEC_SYSTEM] = ART_ICON8_CHIP,
	};
	const struct torii_font *font = ui_theme()->body;
	u32 skip = look.label_skip, lines = p->rh - look.card_pad;
	ui_icon8(p->win, p->x + look.icon_x, p->y, icons[sec], UI_ROLE_ACCENT_INK);
	struct text_line line;
	const char *name = settings_section_name(sec);
	u32 x0 = look.name_x & 7, cols = LABEL_COLS;
	struct text_window scratch = {label_scratch, (u8)cols, 2};
	text_layout(font, name, strlen(name), cols * 8 - x0, &line);
	memset(label_scratch, 0, sizeof(label_scratch));
	text_draw_line(&scratch, font, x0, 0, UI_ROLE_ACCENT_INK, &line);
	u32 used = (x0 + line.extent + 7) / 8, column = p->win->height * 8u;
	u32 *dst = p->win->tiles + ((p->x + look.name_x) / 8) * column + p->y;
	for (u32 c = 0; c < used && (p->x + look.name_x) / 8 + c < p->win->width; c++)
		for (u32 l = 0; l < lines; l++)
			dst[c * column + l] = label_scratch[c * 16 + skip + l];
	return 1;
}

// Key chips for mask's keys, L first, ending at right; accent: the capture's
// (the item's base, on text); returns where they start
static s32 paint_chips(const struct ui_paint *p, s32 right, u16 mask, int accent)
{
	const struct ui_look *l = &ui_theme()->look;
	const char *names[4];  // a chip a key, L first (a capture may hold more: the first four)
	u32 n = 0, total = 0, w[4], tw[4];
	for (u32 bit = 10; bit-- > 0 && n < 4;)
		if (mask >> bit & 1) {
			names[n] = settings_key_name(bit);
			tw[n] = text_width(ui_theme()->body, names[n], strlen(names[n])) - 1;  // less the gap after
			u32 cw = tw[n] + 2u * l->hint_chip_pad;
			w[n] = cw < look.chip_min ? look.chip_min : cw;
			total += w[n] + (n ? look.chip_gap : 0u);
			n++;
		}
	s32 x = right - (s32)total, top = ((s32)p->rh - (s32)l->hint_chip_h) / 2, start = x;
	for (u32 i = 0; i < n; i++) {
		round_fill(p, x, top, (s32)w[i], l->hint_chip_h, l->hint_chip_radius,
			   accent ? UI_ROLE_ACCENT : UI_ROLE_TRACK);
		text_at(p, x + ((s32)w[i] - (s32)tw[i]) / 2, w[i], accent ? UI_ROLE_ACCENT_TEXT : UI_ROLE_DIM,
			names[i]);
		x += (s32)w[i] + look.chip_gap;
	}
	return start;
}

// Where row id's swatches lie: the value's, or all eight while it is edited
// (x in the row, and each one's colour); returns how many
static u32 swatch_layout(u32 id, s32 *xs, u8 *cs)
{
	u32 c = settings_get(&sp.v, id);
	if (editing(id)) {
		s32 right = look.value_r - (s32)extent(MARK_R) - look.mark_gap;
		s32 x = right - (8 * look.swatch_w + 7 * look.swatch_gap);
		for (u32 i = 0; i < 8; i++) {
			xs[i] = x + (s32)i * (look.swatch_w + look.swatch_gap);
			cs[i] = (u8)i;
		}
		return 8;
	}
	xs[0] = look.value_r - (s32)extent(S(STR_SET_VALUE_COLOUR_OFF + c)) - look.swatch_text_gap - look.swatch_w;
	cs[0] = (u8)c;
	return 1;
}

// A swatch's BG part (the sprite fills the lit ones): Off is the card
// with a slash, edged in ink3, White's edge is ink3 too; ring: the chosen
// one while edited, ringed in ink
static void paint_swatch(const struct ui_paint *p, s32 x, u32 c, int ring)
{
	s32 w = look.swatch_w, h = look.swatch_h, y = ((s32)p->rh - h) / 2;
	if (ring)
		round_line(p, x - 1, y - 1, w + 2, h + 2, look.swatch_r + 1u, UI_ROLE_TEXT);
	if (c && c < SWATCH_COLOURS)
		return;
	round_line(p, x, y, w, h, look.swatch_r, UI_ROLE_FAINT);
	if (c)
		return;
	for (s32 i = 0; i < w - 2; i++)  // the slash, bottom left to top right
		fill(p, x + 1 + i, (u32)(y + h - 2 - i * (h - 3) / (w - 3)), 1, 1, UI_ROLE_FAINT);
}

// Row id's value ending at right, in ink; returns where it starts
static s32 paint_value(const struct ui_paint *p, u32 id, s32 right, u32 ink)
{
	const struct settings_row *r = &settings_rows[id];
	char text[24];
	switch (r->kind) {
	case SET_ACTION: {
		if (id == SET_RESCAN && sp.scanning)
			return text_right(p, right, ink, S(STR_SET_SCANNING));
		if (id == SET_IDENTIFY && sp.identify_done != IDENTIFY_IDLE)
			return text_right(p, right, ink, sp.progress);
		// A chevron, chevron_gap in from the edge
		s32 x = right - look.chevron_gap - look.chevron_w, y = ((s32)p->rh - look.chevron_h) / 2;
		for (s32 i = 0; i < look.chevron_h; i++) {
			s32 dx = i < look.chevron_h / 2 ? i : look.chevron_h - 1 - i;
			fill(p, x + dx, (u32)(y + i), 1, 1, ink);
		}
		return x;
	}
	case SET_KEYS:
		if (sp.mode == MODE_CAPTURE && sp.edit_id == id) {
			s32 at = right - (s32)extent(MARK_R);
			text_at(p, at, 16, UI_ROLE_ACCENT_INK, MARK_R);
			at -= look.mark_gap;
			at = sp.cap.held ? paint_chips(p, at, sp.cap.held, 1)
					 : text_right(p, at, UI_ROLE_TEXT, S(STR_SET_PRESS));
			return text_right(p, at - look.mark_gap, UI_ROLE_ACCENT_INK, MARK_L);
		}
		return paint_chips(p, right, settings_get(&sp.v, id), 0);
	case SET_COLOUR: {
		s32 xs[8];
		u8 cs[8];
		u32 n = swatch_layout(id, xs, cs), c = settings_get(&sp.v, id);
		if (n == 1) {
			paint_swatch(p, xs[0], c, 0);
			text_right(p, right, ink, S(STR_SET_VALUE_COLOUR_OFF + c));
			return xs[0];
		}
		for (u32 i = 0; i < n; i++)
			paint_swatch(p, xs[i], cs[i], cs[i] == c);
		return xs[0];
	}
	default:
		settings_value_text(&sp.v, id, text, sizeof(text));
		return text_right(p, right, ink, text);
	}
}

static int paint_row(const struct ui_paint *p, u32 id)
{
	const struct ui_look *l = &ui_theme()->look;
	if (p->selected)
		round_fill(p, look.pill_x, ((s32)p->rh - l->pill_h) / 2, look.pill_w, l->pill_h, l->pill_radius,
			   UI_ROLE_TINT);
	s32 right = look.value_r;
	int edit = editing(id) && settings_rows[id].kind != SET_KEYS;
	if (edit) {
		right = text_right(p, right, UI_ROLE_ACCENT_INK, MARK_R) - look.mark_gap;
		s32 left = paint_value(p, id, right, UI_ROLE_TEXT);
		right = text_right(p, left - look.mark_gap, UI_ROLE_ACCENT_INK, MARK_L);
	} else {
		right = paint_value(p, id, right, p->selected ? UI_ROLE_DIM : UI_ROLE_FAINT);
	}
	s32 clip = right - 4 - look.text_x;
	text_at(p, look.text_x, clip > 8 ? (u32)clip : 8, p->selected ? UI_ROLE_TEXT : UI_ROLE_DIM,
		settings_label(&sp.v, id));
	return 1;
}

static int model_paint(void *ctx, const struct ui_paint *p)
{
	(void)ctx;
	if (p->index >= sp.lay.count)
		return 0;
	const struct settings_slot *s = slot_at(p->index);
	paint_card(p, p->index);
	if (s->kind == SET_SLOT_LABEL)
		return paint_label(p, s->section);
	if (s->kind == SET_SLOT_ROW)
		return paint_row(p, s->id);
	return 1;  // the gap: the card's end and its shadow
}

static u32 model_count(void *ctx)
{
	(void)ctx;
	return sp.lay.count;
}

static int model_fetch(void *ctx, u32 i, struct ui_item *out)
{
	(void)ctx;
	if (i >= sp.lay.count)
		return UI_ITEM_PENDING;
	const struct settings_slot *s = slot_at(i);
	out->text = s->kind == SET_SLOT_ROW ? settings_label(&sp.v, s->id) : settings_section_name(s->section);
	return UI_ITEM_READY;
}

// The swatches' sprites, over the rows that show whole
static void model_objs(void *ctx, u8 list, const struct ui_list_objs *o)
{
	(void)ctx;
	if (sp.swatch_tile == 0xFFFF)
		return;
	u32 rh = o->rh, s1 = ((u32)o->scroll + o->h + rh - 1) / rh;
	for (u32 s = (u32)o->scroll / rh; s < s1 && s < sp.lay.count; s++) {
		const struct settings_slot *sl = slot_at(s);
		if (sl->kind != SET_SLOT_ROW || settings_rows[sl->id].kind != SET_COLOUR)
			continue;
		s32 top = o->top + (s32)(s * rh) - o->scroll;
		if (top < o->top || top + (s32)rh > o->top + (s32)o->h)
			continue;  // cut by the view: left out with the row
		s32 xs[8];
		u8 cs[8];
		u32 n = swatch_layout(sl->id, xs, cs);
		s32 y = top + ((s32)rh - look.swatch_h) / 2;
		for (u32 i = 0; i < n; i++) {
			if (!cs[i])
				continue;
			s32 x = o->x + xs[i];
			ui_obj_add(list, (u16)((y & 0xFF) | o->attr0), (u16)(OBJ_SIZE_16 | (x & 0x1FF)),
				   (u16)((sp.swatch_tile + (cs[i] - 1u) * SWATCH_TILES) | (u32)sp.swatch_bank << 12));
		}
	}
}

static const struct ui_list_model model = {model_count, model_fetch, 0, model_paint, model_objs};

// The swatches' sprite tiles and palette: colour c's 12x10 shape, rounded,
// in index c with its edge in c + 7; White's edge left clear (its BG
// outline shows), so it matches the theme's ink3 as Off's does
static void swatch_load(void)
{
	sp.swatch_tile = 0xFFFF;
	u8 pal = res_palette(RES_OBJ_PAL, 1, TAG_SWATCH_PAL);
	u8 tiles = pal == RES_NONE ? RES_NONE : res_obj_tiles(SWATCH_COLOURS * SWATCH_TILES, 0, TAG_SWATCH);
	if (tiles == RES_NONE) {
		tlog_error("settings: no room for the LED swatches");
		return;
	}
	sp.swatch_bank = (u8)res_start(RES_OBJ_PAL, pal);
	u16 colours[16] = {0};
	for (u32 c = 1; c < 8; c++) {
		u32 rgb = look.led[c], keep = 256u - look.led_edge;
		u32 r = rgb >> 16 & 0xFF, g = rgb >> 8 & 0xFF, b = rgb & 0xFF;
		colours[c] = (u16)(r >> 3 | (g >> 3) << 5 | (b >> 3) << 10);
		colours[c + 7] =
			(u16)((r * keep >> 8) >> 3 | ((g * keep >> 8) >> 3) << 5 | ((b * keep >> 8) >> 3) << 10);
	}
	gfx_pal_load(16u + sp.swatch_bank, colours, 16);
	sp.swatch_tile = res_start(RES_OBJ_VRAM, tiles);
	if (!res_fresh(RES_OBJ_VRAM))
		return;
	u32 *t = res_obj_addr(tiles);
	s32 w = look.swatch_w, h = look.swatch_h;
	u32 r = look.swatch_r;
	for (u32 c = 1; c < 8; c++) {
		u32 *sprite = t + (c - 1) * SWATCH_TILES * 8;
		memset(sprite, 0, SWATCH_TILES * 32);
		for (s32 y = 0; y < h; y++)
			for (s32 x = 0; x < w; x++) {
				// Inside its rounded corners, and on its edge (a neighbour outside)
				int in[5];
				static const s8 d[5][2] = {{0, 0}, {-1, 0}, {1, 0}, {0, -1}, {0, 1}};
				for (u32 k = 0; k < 5; k++) {
					s32 px = x + d[k][0], py = y + d[k][1];
					s32 ey = py < h - 1 - py ? py : h - 1 - py,
					    ex = px < w - 1 - px ? px : w - 1 - px;
					in[k] = px >= 0 && px < w && py >= 0 && py < h &&
						((u32)ey >= r || (u32)ex >= ui_round_inset(r, (u32)ey));
				}
				if (!in[0])
					continue;
				int edge = !(in[1] && in[2] && in[3] && in[4]);
				u32 index = edge ? (c == SWATCH_COLOURS ? 0 : c + 7) : c;
				u32 tile = (u32)(y / 8) * 2 + (u32)(x / 8);
				sprite[tile * 8 + (u32)(y & 7)] |= index << (4 * (x & 7));
			}
	}
}

// --- The page ----------------------------------------------------------------

static int settings_event(struct ui_view *v, const struct ui_event *e);

static const struct ui_title_config title = {"", 0, ART_ICON16_SET};
static const struct ui_hints_config hints_config = {0, 0};
static const struct ui_list_config list_config = {&model, UI_ROW_PITCH_SETTINGS, UI_LIST_DRAWN_PILL, 0, 0};
static const struct ui_scrollbar_config bar = {N_LIST};

static const struct ui_spec nodes[N_NODES] = {
	[N_TITLE] = {UI_TITLE, 1, UI_NONE, 3, UI_HFILL, 0, 0, 0, 0, 0, 0, TITLE_H, &title},
	[N_HINTS] = {UI_HINTS, 2, UI_NONE, 3, UI_HFILL | UI_BOTTOM, 0, 0, 0, 0, 0, 0, HINTS_H, &hints_config},
	[N_BAR] = {UI_SCROLLBAR, 3, UI_NONE, 2, UI_LEFT, 0, 0, 0, BAR_X, BAR_Y, 8, BAR_H, &bar},
	[N_LIST] = {UI_LIST, 4, UI_NONE, 1, UI_LEFT, 0, 0, 0, 0, VIEW_Y, LIST_W, VIEW_H, &list_config},
};

static const struct ui_view_spec view = {"settings", nodes, N_NODES, UI_NONE, 0, settings_event};

// 1 while the page's view is up (at the stack's bottom: popups go over it)
static int up(void)
{
	return ui_depth() && ui_view_at(0)->spec == &view;
}

static void refresh_slot(u32 s)
{
	if (s < sp.lay.count)
		ui_list_refresh(node(N_LIST), s, s);
}

static void refresh_id(u32 id)
{
	refresh_slot(settings_layout_slot_of(&sp.lay, id));
}

// The hint bar: the selected row's explanation, a refusal's reason, or an
// edit's keys
static void hints_show(void)
{
	u8 h = node(N_HINTS);
	u32 id = cursor_id();
	if (sp.saving) {
		ui_hints_text(h, S(STR_SET_SAVING));
	} else if (sp.mode == MODE_EDIT) {
		const char *change = settings_rows[id].kind == SET_COLOUR
					     ? S(STR_SET_VALUE_COLOUR_OFF + settings_get(&sp.v, id))
					     : S(STR_HINT_CHANGE);
		sp.hints[0] = (struct ui_hint){KEY_LEFTRIGHT, change};
		sp.hints[1] = (struct ui_hint){"A", S(STR_HINT_DONE)};
		sp.hints[2] = (struct ui_hint){"B", S(STR_HINT_CANCEL)};
		ui_hints_set(h, sp.hints, 3);
	} else if (sp.mode == MODE_CAPTURE) {
		u32 left = settings_capture_left(&sp.cap);
		struct ui_hint_note note = {UI_ROLE_DIM,
					    look.bar_w,
					    look.bar_h,
					    look.bar_r,
					    look.bar_gap,
					    (u8)((left * look.bar_w + sp.cap.timeout - 1) / sp.cap.timeout),
					    0};
		sp.bar_fill = note.fill;
		ui_hints_note(h, S(STR_SET_PRESS_HINT), &note);
	} else if (sp.refused_left) {
		struct ui_hint_note note = {UI_ROLE_DANGER, 0, 0, 0, 0, 0, 0};
		ui_hints_note(h, sp.refused, &note);
	} else {
		ui_hints_text(h, settings_explain(&sp.v, id));
	}
}

// The title's position, "5/24"
static void position_show(void)
{
	SF(sp.pos, sizeof(sp.pos), STR_NAV_POSITION, LN(settings_layout_position(&sp.lay, sp.cursor)), LN(sp.lay.rows));
	ui_title_set_right(node(N_TITLE), sp.pos);
}

static void moved(void)
{
	tlog_debug("settings: row %lu", (unsigned long)cursor_id());  // tests/nav.sh's nav_setting waits for it
	sp.refused_left = 0;
	position_show();
	hints_show();
	frame_measure("settings-move");
}

// The LED as the working copy has it (as CheckSwitch sets it from the NOR)
static void led_show(void)
{
	u32 idle = settings_get(&sp.v, SET_LED_IDLE), sd = settings_get(&sp.v, SET_LED_SD);
	Set_LED_control((u16)(settings_get(&sp.v, SET_LED) << 7 | (idle & 7) << 3 | (sd & 7)));
}

// The rows shown changed (LED on or off): laid out again, the cursor kept
// on its row and the scroll where it was
static void relayout(void)
{
	u32 id = cursor_id();
	settings_layout_build(&sp.lay, &sp.v);
	u32 s = settings_layout_slot_of(&sp.lay, id);
	sp.cursor = (u8)(s != SET_LAYOUT_NONE ? s : settings_layout_row_at(&sp.lay, sp.cursor));
	ui_list_recount(node(N_LIST), sp.cursor);
	position_show();
}

// What a value's change shows beyond its row: the theme and the LED at once,
// the rows it lays out, labels or texts
static void previews(u32 id, int ui)
{
	switch (id) {
	case SET_THEME:
		// The theme wipe (B19): asked again mid-way, its edge goes back
		ui_theme_switch(settings_get(&sp.v, SET_THEME) == 1 ? &ui_theme_light : &ui_theme_dark);
		break;
	case SET_LED:
		led_show();
		if (ui)
			relayout();
		break;
	case SET_LED_IDLE:
	case SET_LED_SD:
		led_show();
		break;
	case SET_CLOCK_24H:
		if (ui)
			refresh_id(SET_TIME_ROW);
		break;
	default:
		break;
	}
}

static void changed(u32 id)
{
	char text[24];
	settings_value_text(&sp.v, id, text, sizeof(text));
	tlog_debug("settings: row %lu set to %s", (unsigned long)id, text);
	refresh_id(id);
	previews(id, 1);
	hints_show();
	frame_measure("settings-edit");
}

static void edit_slots(u32 id, int back);

// The working copy to the kernel NOR, one write if a value differs from
// what it holds (settings_commit, through Save_info); an edit still open
// goes in with its value from before A, not its preview
static void save_now(void)
{
	u16 preview[assress_max];
	int edit = sp.mode == MODE_EDIT;
	sp.saving = 0;
	ui_wipe_finish();  // the write blocks: a wipe still running ends first
	if (edit) {
		memcpy(preview, sp.v.slot, sizeof(preview));
		edit_slots(sp.edit_id, 1);
	}
	u32 level = tlog_level;
	settings_stage(&sp.v);
	u32 done = settings_commit();
	if (done == SET_COMMIT_FAILED) {
		// The old values stay on the NOR, these staged: the next confirm,
		// or leaving, tries again (loaded still differs)
		snprintf(sp.refused, sizeof(sp.refused), "%s", S(STR_SET_SAVE_FAILED));
		sp.refused_left = look.refusal;
		sp.unsaved = 1;
		tlog_error("settings: not saved, tried again at the next confirm or on leaving");
	} else {
		if (done == SET_COMMIT_WRITTEN && level == TLOG_OFF && tlog_level != TLOG_OFF)
			tlog_flush_soon();  // the log file is made a sector a frame, the menu still answering
		memcpy(sp.v.loaded, sp.v.slot, sizeof(sp.v.loaded));
		sp.unsaved = 0;
		sp.warned = 0;
	}
	if (edit)
		memcpy(sp.v.slot, preview, sizeof(preview));
}

// B with a failed write: tried once more, and if that fails too the page
// stays, saying so (save_now); a second B leaves, dropping the values.
// 1 if the page stays
static int keep_unsaved(void)
{
	if (!sp.unsaved || sp.warned)
		return 0;
	save_now();
	hints_show();
	if (!sp.unsaved)
		return 0;
	sp.warned = 1;
	return 1;
}

// A value was confirmed: if it changed anything, the hint bar says so and
// settings_poll writes it once that is on screen
static void save_soon(void)
{
	if (!memcmp(sp.v.slot, sp.v.loaded, sizeof(sp.v.loaded)))
		return;
	sp.saving = look.save_wait;
	tlog_debug("settings: saving");
}

// The edited row's slots: kept as A starts the edit (to), or put back (B)
static void edit_slots(u32 id, int back)
{
	const struct settings_row *r = &settings_rows[id];
	u32 n = r->kind == SET_COLOUR ? 3 : 1;
	if (r->slot == SET_NO_SLOT || r->slot + n > assress_max)
		return;
	if (back)
		memcpy(&sp.v.slot[r->slot], sp.edit_from, n * sizeof(u16));
	else
		memcpy(sp.edit_from, &sp.v.slot[r->slot], n * sizeof(u16));
}

// Language, kept: it takes effect at once. The page leaves (written now,
// settings_commit loads the language: CheckLanguage) and comes back drawn
// in it, as home does next
static void language_apply(u32 arg)
{
	(void)arg;
	settings_page.leave();
	nav_reshow();
}

static void edit_end(int keep)
{
	u32 id = sp.edit_id;
	sp.mode = MODE_BROWSE;
	if (keep) {
		save_soon();
		if (id == SET_LANGUAGE && sp.saving)
			nav_defer(language_apply, 0);
	} else {
		edit_slots(id, 1);
		previews(id, 1);
	}
	refresh_id(id);
	hints_show();
}

static void refuse(u32 why, u32 id)
{
	if (why == SET_KEYS_TAKEN)
		SF(sp.refused, sizeof(sp.refused), STR_SET_KEYS_TAKEN,
		   LS(settings_label(&sp.v, id == SET_SLEEP_KEYS ? SET_RESET_KEYS : SET_SLEEP_KEYS)));
	else
		snprintf(sp.refused, sizeof(sp.refused), "%s",
			 why && why <= SET_KEYS_SOFT_RESET ? S(STR_SET_KEYS_COUNT + why - SET_KEYS_COUNT) : "");
	_Static_assert(STR_SET_KEYS_SOFT_RESET - STR_SET_KEYS_COUNT == SET_KEYS_SOFT_RESET - SET_KEYS_COUNT,
		       "the refusals in settings_set_keys' order");
	sp.refused_left = look.refusal;
	tlog_debug("settings: keys refused: %s", sp.refused);
}

static void capture_end(void)
{
	sp.mode = MODE_BROWSE;
	refresh_id(sp.edit_id);
	hints_show();
}

// The Time or Date row's field card, after the frame (nav_defer): UP/DOWN
// change a field, LEFT/RIGHT pick one, A sets the clock, B leaves it
static void fields_text(void)
{
	struct ui_fields *f = &sp.fields;
	for (u32 i = 0; i < sp.ed.fields; i++) {
		settings_editor_text(&sp.ed, i, sp.field_text[i], sizeof(sp.field_text[i]));
		f->text[i] = sp.field_text[i];
	}
	f->active = sp.ed.field;
}

static void time_edit(u32 id)
{
	if (!up())
		return;
	u32 kind = settings_rows[id].kind;
	settings_read_clock(&sp.v.clock);
	settings_editor_open(&sp.ed, kind, &sp.v.clock, settings_clock_24h(&sp.v));
	struct ui_fields *f = &sp.fields;
	memset(f, 0, sizeof(*f));
	f->title = settings_label(&sp.v, id);
	f->icon8 = ART_ICON8_CLOCK + 1;
	f->count = sp.ed.fields;
	f->sep[0] = kind == SET_DATE ? "-" : " : ";
	f->sep[1] = kind == SET_DATE ? "-" : " ";
	fields_text();
	struct ui_hint *keys = sp.field_keys;
	keys[0] = (struct ui_hint){KEY_UPDOWN, S(STR_HINT_CHANGE)};
	keys[1] = (struct ui_hint){KEY_LEFTRIGHT, S(STR_HINT_FIELD)};
	keys[2] = (struct ui_hint){"A", S(STR_HINT_SET)};
	keys[3] = (struct ui_hint){"B", S(STR_HINT_CANCEL)};
	if (!ui_fields_open(node(N_LIST), node(N_HINTS), f, keys, 4))
		return;
	for (;;) {
		u16 k = ui_fields_key(library_poll);  // the walk and a Rescan go on meanwhile
		if (!k || k == INPUT_B)
			break;
		if (k == INPUT_A) {
			settings_write_edit(&sp.ed);
			settings_read_clock(&sp.v.clock);
			refresh_id(SET_TIME_ROW);
			refresh_id(SET_DATE_ROW);
			break;
		}
		if (k & (INPUT_UP | INPUT_DOWN))
			settings_editor_step(&sp.ed, k == INPUT_UP ? 1 : -1);
		else
			settings_editor_move(&sp.ed, k == INPUT_LEFT ? -1 : 1);
		fields_text();
		ui_fields_changed();
	}
	ui_popup_close();
}

// Settings > Saves > Game saves or Clean up saves: the screen takes the
// page's place, and the page comes back when it closes (or a game played
// from Game saves didn't start)
static void saves_open(u32 id)
{
	settings_page.leave();  // a change still waiting is written first
	if (id == SET_GAME_SAVES)
		saves_games_run();
	else
		saves_run();
	nav_reshow();
}

static void activate(u32 id)
{
	const struct settings_row *r = &settings_rows[id];
	switch (r->kind) {
	case SET_TIME:
	case SET_DATE:
		nav_defer(time_edit, id);
		break;
	case SET_KEYS:
		sp.mode = MODE_CAPTURE;
		sp.edit_id = (u8)id;
		sp.refused_left = 0;
		settings_capture_start(&sp.cap, ui_input()->held, look.hold, look.capture_wait);
		refresh_id(id);
		hints_show();
		break;
	case SET_ACTION:
		if (r->action == SET_ACTION_RESCAN && !sp.scanning) {
			tlog("settings: rescan library");
			library_rescan();
			sp.scanning = 1;
			refresh_id(id);
		} else if (r->action == SET_ACTION_IDENTIFY && sp.identify_done == IDENTIFY_IDLE) {
			tlog("settings: identify library");
			names_identify();
			sp.identify_wait = 0;  // its progress at once
		} else if (r->action == SET_ACTION_SAVES || r->action == SET_ACTION_GAME_SAVES) {
			nav_defer(saves_open, id);
		}
		break;
	default:
		sp.mode = MODE_EDIT;
		sp.edit_id = (u8)id;
		edit_slots(id, 0);
		refresh_id(id);
		hints_show();
		break;
	}
}

static void move_to(u32 s, u16 key)
{
	if (s == sp.cursor || s >= sp.lay.count)
		return;
	sp.cursor = (u8)s;
	if (key)
		ui_list_move(node(N_LIST), s, key);
	else
		ui_list_set_cursor(node(N_LIST), s);
	moved();
}

static int settings_event(struct ui_view *v, const struct ui_event *e)
{
	(void)v;
	if (e->type != UI_EV_KEYS)
		return 0;
	const InputState *in = ui_input();
	u16 k = e->keys;
	u32 id = cursor_id();
	if (sp.mode == MODE_CAPTURE)
		return 1;  // the keys are the combination (settings_poll)
	if (sp.leaving)
		return 1;  // the page goes once the write is done (settings_poll)
	if (sp.mode == MODE_EDIT) {
		if (k & INPUT_B) {
			edit_end(0);
		} else if (k & INPUT_A) {
			edit_end(1);
		} else if (k & (INPUT_LEFT | INPUT_RIGHT)) {
			u16 key = k & INPUT_LEFT ? INPUT_LEFT : INPUT_RIGHT;
			// Only number rows step on a held key's repeats
			if (settings_rows[id].kind == SET_NUMBER || (in->pressed & key)) {
				settings_step(&sp.v, id, key == INPUT_LEFT ? -1 : 1);
				changed(id);
			}
		}
		return 1;
	}
	if (k & INPUT_B) {
		if (sp.saving)
			sp.leaving = 1;  // the write first, from settings_poll
		else if (!keep_unsaved())
			nav_back();
	} else if (k & INPUT_A) {
		if (!ui_list_paging(node(N_LIST)) && (in->pressed & INPUT_A))
			activate(id);
	} else if (k & (INPUT_UP | INPUT_DOWN)) {
		u16 key = k & INPUT_UP ? INPUT_UP : INPUT_DOWN;
		// A fresh press wraps from an end; a held key's repeats stop there
		move_to(settings_layout_step(&sp.lay, sp.cursor, key == INPUT_UP ? -1 : 1, (in->pressed & key) != 0),
			key);
	} else if (k & (INPUT_L | INPUT_R)) {
		move_to(settings_layout_section(&sp.lay, sp.cursor, k & INPUT_L ? -1 : 1), 0);
	}
	return 1;
}

static struct ui_view *settings_enter(u32 item, u32 row)
{
	// What led here goes to the card first, in case the page hangs (v0.3.2)
	tlog_flush();
	memset(&sp, 0, sizeof(sp));
	settings_load(&sp.v);
	sp.v.start_shown = nav_start_shown();  // Start on offers what home shows
	settings_layout_build(&sp.lay, &sp.v);
	struct ui_view *v = ui_push(&view, &sp);
	if (!v)
		return 0;
	sp.first = v->first;
	swatch_load();
	u32 s = row < SET_ROWS ? settings_layout_slot_of(&sp.lay, row) : SET_LAYOUT_NONE;
	sp.cursor = (u8)(s != SET_LAYOUT_NONE ? s : settings_layout_row_at(&sp.lay, 0));
	sp.live = 1;
	sp.identify_done = IDENTIFY_IDLE;
	ui_title_set(node(N_TITLE), nav_item_name(item));
	ui_list_reset(node(N_LIST), sp.cursor);
	position_show();
	hints_show();
	return v;
}

static u32 settings_row(void)
{
	return cursor_id();
}

// Once a frame: the capture's keys, a refusal's time, the rescan's end, the
// clock's rows
static void settings_poll(void)
{
	if (!up())
		return;
	u8 list = node(N_LIST);
	if (sp.redraw) {
		// leave ended an edit (perhaps put the LED back on) and an action
		// left the page up: laid out and drawn again
		sp.redraw = 0;
		relayout();
		hints_show();
	}
	if (sp.saving) {
		// Once "Saving..." is on screen and the page is still, or after
		// save_wait frames with no key held: the write, the page staying up.
		// B asked to leave: at once (a wipe ends first), then the page goes
		if (!ui_input()->held)
			sp.saving--;
		if (sp.leaving || !sp.saving || (ui_settled() && !ui_wiping())) {
			save_now();
			hints_show();
		}
	}
	if (sp.leaving && !sp.saving) {
		sp.leaving = 0;
		if (!keep_unsaved())
			nav_back();
	}
	if (sp.mode == MODE_CAPTURE) {
		u16 shown = sp.cap.held;
		u32 r = settings_capture_step(&sp.cap, ui_input()->held, 1);
		if (r == SET_CAPTURE_DONE) {
			u32 why = settings_set_keys(&sp.v, sp.edit_id, sp.cap.best);
			if (why != SET_KEYS_OK) {
				refuse(why, sp.edit_id);
			} else {
				tlog_debug("settings: keys set");
				save_soon();
			}
			capture_end();
		} else if (r == SET_CAPTURE_CANCEL) {
			capture_end();
		} else {
			if (sp.cap.held != shown)
				refresh_id(sp.edit_id);
			u32 left = settings_capture_left(&sp.cap);
			if ((left * look.bar_w + sp.cap.timeout - 1) / sp.cap.timeout != sp.bar_fill)
				hints_show();
		}
		// B pressed on the frame the keys were let go: the page's B, which
		// leaves it (above, next frame; a save first)
		if (r != SET_CAPTURE_WAIT && (sp.cap.after & INPUT_B))
			sp.leaving = 1;
	} else if (sp.refused_left) {
		sp.refused_left--;
		if (!sp.refused_left)
			hints_show();  // the explanation back
	}
	if (sp.scanning && !library_busy()) {
		sp.scanning = 0;
		refresh_id(SET_RESCAN);
	}
	// Identify library's progress, looked at every identify_poll frames
	if (sp.identify_wait) {
		sp.identify_wait--;
	} else {
		sp.identify_wait = look.identify_poll;
		u32 done = 0, total = 0, now = names_identifying(&done, &total) ? done : IDENTIFY_IDLE;
		if (now != sp.identify_done) {
			sp.identify_done = now;
			SF(sp.progress, sizeof(sp.progress), STR_SET_PROGRESS, LN(done), LN(total));
			refresh_id(SET_IDENTIFY);
		}
	}
	if (sp.clock_wait) {
		sp.clock_wait--;
	} else {
		sp.clock_wait = look.clock_poll;
		struct settings_clock c;
		settings_read_clock(&c);
		if (c.minute != sp.v.clock.minute || c.hour != sp.v.clock.hour || c.day != sp.v.clock.day ||
		    c.month != sp.v.clock.month || c.year != sp.v.clock.year) {
			sp.v.clock = c;
			refresh_id(SET_TIME_ROW);
			refresh_id(SET_DATE_ROW);
		}
	}
	if (!ui_list_still(list))
		frame_measure("settings-scroll");
}

// Leaving (nav's leave): an edit or a capture still open is dropped, its
// preview put back (nothing is written for it), and a confirmed change still
// waiting is written
static void settings_leave(void)
{
	if (!sp.live)
		return;
	if (sp.mode == MODE_EDIT) {
		edit_slots(sp.edit_id, 1);
		previews(sp.edit_id, 0);
	}
	if (sp.mode != MODE_BROWSE)
		sp.redraw = 1;
	sp.mode = MODE_BROWSE;
	if (sp.saving || sp.unsaved) {
		save_now();
		sp.redraw = 1;  // the hint bar's explanation back, if the page stays up
		if (sp.unsaved) {
			// Failed (its last try): nothing may stay staged past the
			// page (a launch reuses EWRAM), so the NOR's values are back
			Discard_SET_info();
			tlog_error("settings: not saved, the page left");
		}
	}
}

const struct nav_page settings_page = {settings_enter, settings_row, 0, settings_poll, settings_leave};
