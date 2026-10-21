// Popups, confirmations and the letter card (ui.h): floating cards on
// plane 0 over a page (research override R9).
//
// Each draws into tiles of its own over its node's rectangle, column-major
// as text.h's windows: the whole card while its view isn't shown yet, then
// a band of lines at a time (a moved selection, a new letter), drawn in
// EWRAM and copied in VBlank, so nothing tears. What they need of the
// registers (a window while they open or close, a slide, the dim) goes
// through ui_overlay_set (ui/ui_run.c).
//
// A confirmation over a popup takes its place: the popup goes while the
// question is up (a sliver of it beside the card read as a glitch), and
// comes back dimmed as the answer's dim fades out, through the
// confirmation's map, in a bank of their own whose colours the dim's maths
// darkens or brightens (the dim leaves plane 0 alone). The card itself is
// drawn in a bank that fades in from the dimmed page. When BG block 0 has
// no room for its tiles, it borrows the popup's (pp.share), and the popup
// is drawn again before it shows.
#include <string.h>

#include "art.h"
#include "letter.h"
#include "ui_priv.h"
#include "../engine/frame.h"
#include "../engine/gfx.h"
#include "../engine/res.h"
#include "../engine/text.h"
#include "../lang.h"
#include "../tlog.h"

// --- The look ------------------------------------------------------------
// Every size of the three cards, in px: spec.md's popup section and the
// mockups #8, #9 and #10 (docs/design/v0.4/pages-*.png). The shared sizes
// (card and pill radius, pill height) are the theme's look, and every frame
// count is in its motions.
static const struct popup_look {
	// Floating cards: the card colour (FLOAT) in a hairline, and a shadow
	u8 shadow_x, shadow_y;  // how far the shadow falls right and down
	// The popup (#9)
	u8 pop_x, pop_w;     // its card's left edge and width
	u8 pop_above;        // its top this far above the row's top,
	u8 pop_top_min;      // no higher than this (under the title bar)
	u8 pop_bottom_max;   // and its bottom no lower (above the hint bar)
	u8 pop_notch;        // how far the notch at the row's middle reaches out
	u8 pop_head_h;       // with a title: the band down to the first item
	u8 pop_title_y;      // the title's line top in it
	u8 pop_rule_y;       // the hairline under the title
	u8 pop_rule_in;      // and how far in from the card's sides it stops
	u8 pop_icon_x;       // the title's 8x8 icon
	u8 pop_title_x;      // the title's text, after the icon
	u8 pop_pad_top;      // without a title: above the first item
	u8 pop_pad_bottom;   // below the last item
	u8 pop_pitch;        // an item's height (the body font's line)
	u8 pop_text_x;       // an item's text
	u8 pop_value_r;      // where its value ends, from the card's right
	u8 pop_value_gap;    // at least this between the text and the value
	u8 pop_pill_in;      // the selection pill's inset from the card's sides
	// The field card (the Settings page's Time and Date, #20), in the
	// popup's card
	u8 fld_h;            // its height
	u8 fld_icon_x, fld_icon_y;  // the title's 8x8 icon
	u8 fld_title_x, fld_title_y;  // the title's text, its line top
	u8 fld_rule_y;       // the hairline under the title
	u8 fld_rule_in;      // and how far in from the card's sides it stops
	u8 fld_line_y;       // the fields' line top (the heading font)
	u8 fld_box_pad;      // the active field's tint box: px each side of its text,
	u8 fld_box_h;        // its height, from a px above the line
	u8 fld_box_r;
	u8 fld_arrow_w, fld_arrow_h;  // the arrows above and below it,
	u8 fld_arrow_gap;    // this far from the box
	// The confirmation (#10). Card and shadow fill whole tiles, so over a
	// popup no blank part of its tiles hides the popup.
	u8 conf_x, conf_y, conf_w, conf_h;  // its card
	u8 conf_pad;         // its texts' inset from the sides
	u8 conf_title_y;     // the title's line top
	u8 conf_text_y;      // the text's
	u8 conf_button_y;    // the buttons' top
	u8 conf_button_w, conf_button_h;
	u8 conf_button_gap;  // between the two
	// The letter card (#8), in its node (UI_LETTER_NODE)
	u8 let_x, let_y, let_w, let_h;                  // the card
	u8 let_box_x, let_box_y, let_box_w, let_box_h;  // the letter's solid box in it
	u8 let_box_r;
	u8 let_side_x;  // the letters before and after: in from the card's sides
	u8 let_side_y;  // their line top
} look = {
	.shadow_x = 1,
	.shadow_y = 2,
	.pop_x = 126,
	.pop_w = 104,
	.pop_above = 20,
	.pop_top_min = 21,
	.pop_bottom_max = 144,
	.pop_notch = 3,
	.pop_head_h = 19,
	.pop_title_y = 2,
	.pop_rule_y = 16,
	.pop_rule_in = 5,
	.pop_icon_x = 6,
	.pop_title_x = 16,
	.pop_pad_top = 4,
	.pop_pad_bottom = 3,
	.pop_pitch = 12,
	.pop_text_x = 8,
	.pop_value_r = 8,
	.pop_value_gap = 6,
	.pop_pill_in = 3,
	.fld_h = 60,
	.fld_icon_x = 6,
	.fld_icon_y = 4,
	.fld_title_x = 17,
	.fld_title_y = 2,
	.fld_rule_y = 15,
	.fld_rule_in = 4,
	.fld_line_y = 29,
	.fld_box_pad = 3,
	.fld_box_h = 17,
	.fld_box_r = 3,
	.fld_arrow_w = 5,
	.fld_arrow_h = 3,
	.fld_arrow_gap = 2,
	.conf_x = 32,
	.conf_y = 48,
	.conf_w = 175,
	.conf_h = 62,
	.conf_pad = 8,
	.conf_title_y = 4,
	.conf_text_y = 18,
	.conf_button_y = 39,
	.conf_button_w = 66,
	.conf_button_h = 14,
	.conf_button_gap = 8,
	.let_x = 2,
	.let_y = 2,
	.let_w = 38,
	.let_h = 38,
	.let_box_x = 8,
	.let_box_y = 5,
	.let_box_w = 22,
	.let_box_h = 20,
	.let_box_r = 4,
	.let_side_x = 6,
	.let_side_y = 25,
};

// The hint bar's keys while a popup or confirmation is up
#define KEY_UPDOWN "^v"
#define KEY_LEFTRIGHT "\xC2\xAB\xC2\xBB"  // «»

// --- Pens ----------------------------------------------------------------
//
// Drawing clipped to lines [ya, yb) of a canvas, into a window whose line
// 0 is canvas line line0: the whole canvas, or a band of it.

struct pen {
	const struct text_window *win;
	s32 line0;
	s32 ya, yb;
};

enum {
	PEN_LEFT,
	PEN_CENTRE,  // in [x, x + w)
	PEN_RIGHT,   // ending at x
};

static void pen_fill(const struct pen *p, s32 x, s32 y, s32 w, s32 h, u32 role)
{
	s32 a = y > p->ya ? y : p->ya, b = y + h < p->yb ? y + h : p->yb;
	if (x < 0) {
		w += x;
		x = 0;
	}
	if (w > 0 && a < b)
		text_clear(p->win, (u32)x, (u32)(a - p->line0), (u32)w, (u32)(b - a), role);
}

// A rectangle with its corners rounded by r (the menu's one rounding). Few
// wide fills: a fill costs per tile column it crosses, so the corners'
// rows go as one band between the widest insets, plus their short ends
static void pen_round(const struct pen *p, s32 x, s32 y, s32 w, s32 h, u32 r, u32 role)
{
	if (2 * r > (u32)h)
		r = (u32)h / 2;
	s32 rr = (s32)r, in0 = r ? (s32)ui_round_inset(r, 0) : 0;
	pen_fill(p, x + in0, y, w - 2 * in0, rr, role);
	pen_fill(p, x + in0, y + h - rr, w - 2 * in0, rr, role);
	for (u32 e = 1; e < r; e++) {
		s32 in = (s32)ui_round_inset(r, e);
		if (in >= in0)
			continue;
		pen_fill(p, x + in, y + (s32)e, in0 - in, 1, role);
		pen_fill(p, x + w - in0, y + (s32)e, in0 - in, 1, role);
		pen_fill(p, x + in, y + h - 1 - (s32)e, in0 - in, 1, role);
		pen_fill(p, x + w - in0, y + h - 1 - (s32)e, in0 - in, 1, role);
	}
	pen_fill(p, x, y + rr, w, h - 2 * rr, role);
}

// The span [*a, *b) of line row in a rounded rectangle; 0 if it has none
static int round_span(s32 row, s32 x, s32 y, s32 w, s32 h, u32 r, s32 *a, s32 *b)
{
	if (row < y || row >= y + h)
		return 0;
	s32 e = row - y < y + h - 1 - row ? row - y : y + h - 1 - row;
	s32 in = (u32)e < r ? (s32)ui_round_inset(r, (u32)e) : 0;
	*a = x + in;
	*b = x + w - in;
	return 1;
}

// Lines [ya, yb) of a floating card's band of the canvas, drawn: cleared,
// the body (the card colour inside a hairline that follows its corners),
// and its shadow, offset down and right, where the body leaves it showing
static void card_lines(const struct pen *p, s32 x, s32 y, s32 w, s32 h)
{
	u32 r = ui_theme()->look.card_radius;
	s32 rr = (s32)r, sx = look.shadow_x, sy = look.shadow_y;
	pen_fill(p, 0, p->ya, p->win->width * 8, p->yb - p->ya, UI_ROLE_NONE);
	pen_round(p, x + 1, y + 1, w - 2, h - 2, r ? r - 1 : 0, UI_ROLE_FLOAT);
	// The hairline: a pixel in from the outside at each row of the corners
	// (wider where the corner turns), the top and bottom rows, the sides
	for (u32 e = 0; e < r; e++) {
		s32 in = (s32)ui_round_inset(r, e), out = e ? (s32)ui_round_inset(r, e - 1) : (w + 1) / 2;
		s32 len = out > in + 1 ? out - in : 1;
		for (u32 side = 0; side < 2; side++) {
			s32 row = side ? y + h - 1 - (s32)e : y + (s32)e;
			pen_fill(p, x + in, row, len, 1, UI_ROLE_BORDER);
			pen_fill(p, x + w - in - len, row, len, 1, UI_ROLE_BORDER);
		}
	}
	pen_fill(p, x, y + rr, 1, h - 2 * rr, UI_ROLE_BORDER);
	pen_fill(p, x + w - 1, y + rr, 1, h - 2 * rr, UI_ROLE_BORDER);
	// The shadow: a strip down the right where both are straight, and row
	// by row where either turns or the body has ended
	s32 m0 = y + sy + rr, m1 = y + h - rr;
	if (m1 > m0)
		pen_fill(p, x + w, m0, sx, m1 - m0, UI_ROLE_SHADOW);
	for (s32 row = y + sy; row < y + h + sy; row++) {
		s32 sa, sb, ba, bb;
		if ((row >= m0 && row < m1) || row < p->ya || row >= p->yb ||
		    !round_span(row, x + sx, y + sy, w, h, r, &sa, &sb))
			continue;
		if (!round_span(row, x, y, w, h, r, &ba, &bb)) {
			pen_fill(p, sa, row, sb - sa, 1, UI_ROLE_SHADOW);
			continue;
		}
		if (sa < ba)
			pen_fill(p, sa, row, ba - sa, 1, UI_ROLE_SHADOW);
		if (sb > bb)
			pen_fill(p, bb > sa ? bb : sa, row, sb - (bb > sa ? bb : sa), 1, UI_ROLE_SHADOW);
	}
}

#define DMA3SAD (*(vu32 *)0x040000D4)
#define DMA3DAD (*(vu32 *)0x040000D8)
#define DMA3CNT (*(vu32 *)0x040000DC)
#define DMA3_FILL32 0x85000000u  // on, 32-bit, fixed source

// The pen's lines [a + 1, b) as copies of line a, column by column: a fill
// costs about the same in ROM code per call whatever its size, so a card's
// straight middle goes in one fill a tile column
static void pen_copy_line(const struct pen *p, s32 a, s32 b)
{
	u32 column = p->win->height * 8u, n = (u32)(b - a - 1);
	u32 *line = p->win->tiles + (a - p->line0);
	for (u32 c = 0; c < p->win->width; c++, line += column) {
		vu32 src = *line;  // read by the DMA, which halts the CPU till done
		DMA3SAD = (u32)&src;
		DMA3DAD = (u32)(line + 1);
		DMA3CNT = DMA3_FILL32 | n;
	}
}

// Every line of the canvas the pen draws: what lies above and below the
// floating card at (x, y) cleared, and the card. Its middle lines are all
// alike (straight sides, the shadow's strip): one is drawn, then copied.
static void pen_card(const struct pen *p, s32 x, s32 y, s32 w, s32 h)
{
	s32 top = y, bottom = y + h + look.shadow_y, rr = (s32)ui_theme()->look.card_radius;
	s32 m0 = y + look.shadow_y + rr, m1 = y + h - rr;  // the middle
	struct pen q = *p;
	if (top > p->ya) {
		q.yb = top < p->yb ? top : p->yb;
		pen_fill(&q, 0, q.ya, p->win->width * 8, q.yb - q.ya, UI_ROLE_NONE);
	}
	if (bottom < p->yb) {
		q.ya = bottom > p->ya ? bottom : p->ya;
		q.yb = p->yb;
		pen_fill(&q, 0, q.ya, p->win->width * 8, q.yb - q.ya, UI_ROLE_NONE);
	}
	q.ya = top > p->ya ? top : p->ya;
	q.yb = bottom < p->yb ? bottom : p->yb;
	if (q.ya >= q.yb)
		return;
	s32 a = m0 > q.ya ? m0 : q.ya, b = m1 < q.yb ? m1 : q.yb;
	if (b - a < 4) {
		card_lines(&q, x, y, w, h);
		return;
	}
	s32 end = q.yb;
	q.yb = a + 1;  // down to the first middle line,
	card_lines(&q, x, y, w, h);
	pen_copy_line(p, a, b);  // copied to the end of the middle,
	q.ya = b;
	q.yb = end;
	if (q.ya < q.yb)
		card_lines(&q, x, y, w, h);  // and the rest
}

// Text in one line at line top y, cut to clip px; only when the line lies
// wholly in the pen's lines (a band redraws whole lines)
static void pen_text(const struct pen *p, const struct torii_font *font, s32 x, s32 y, u32 clip, u32 ink, const char *s,
		     u32 align)
{
	if (!s || !*s || !clip || y < p->ya || y + font->line_height > p->yb)
		return;
	struct text_line line;
	text_layout(font, s, strlen(s), clip, &line);
	if (align == PEN_CENTRE)
		x += (s32)(clip > line.extent ? (clip - line.extent) / 2 : 0);
	else if (align == PEN_RIGHT)
		x -= (s32)line.extent;
	if (x >= 0)
		text_draw_line(p->win, font, (u32)x, (u32)(y - p->line0), ink, &line);
}

static void pen_icon8(const struct pen *p, s32 x, s32 y, u32 id, u32 ink)
{
	if (x >= 0 && y >= p->ya && y + 8 <= p->yb)
		ui_icon8(p->win, (u32)x, (u32)(y - p->line0), id, ink);
}

// --- Canvases ------------------------------------------------------------

struct canvas {
	u16 first;  // its first tile, the node's rectangle column-major
	u8 tiles;   // res handle
	u8 mapped;  // the map shows it
};

typedef void (*render_fn)(u8 node, const struct pen *p);

// Bands are drawn here and copied in VBlank; reused once every copy is done
#define BAND_WORDS 768
static u32 band_buf[BAND_WORDS] EWRAM_BSS ALIGN(4);
static u32 band_used EWRAM_BSS;
static u32 band_ticket EWRAM_BSS;

static u32 *tile_addr(u8 node, u32 tile)
{
	return (u32 *)(0x06000000 + ui_plane_cb(ui_node_spec(node)->plane) * 0x4000 + tile * 32u);
}

static int canvas_measure(u8 node, struct engine_budget *need)
{
	const struct ui_node *n = ui_node(node);
	need->bg_tiles = (u16)(need->bg_tiles + (u32)(n->w / 8) * (u32)(n->h / 8));
	return 1;
}

static int canvas_init(u8 node, struct canvas *c)
{
	const struct ui_node *n = ui_node(node);
	c->tiles = ui_tiles(node, (u32)(n->w / 8) * (u32)(n->h / 8), 0, &c->first);
	c->mapped = 0;
	return c->tiles != RES_NONE;
}

// Draws the whole canvas straight into its tiles (nothing shows them yet)
static void canvas_whole(u8 node, const struct canvas *c, render_fn render)
{
	const struct ui_node *n = ui_node(node);
	struct text_window win = {tile_addr(node, c->first), (u8)(n->w / 8), (u8)(n->h / 8)};
	struct pen p = {&win, 0, 0, n->h};
	render(node, &p);
}

// Draws lines [ya, yb) of the canvas into a band and queues its copy over
// them: 0 to try again next frame
static int canvas_band(u8 node, const struct canvas *c, s32 ya, s32 yb, render_fn render)
{
	const struct ui_node *n = ui_node(node);
	u32 tw = (u32)n->w / 8, th = (u32)n->h / 8;
	ya = ya < 0 ? 0 : ya;
	yb = yb > n->h ? n->h : yb;
	if (ya >= yb)
		return 1;
	u32 ty0 = (u32)ya / 8, bt = ((u32)yb + 7) / 8 - ty0, words = tw * bt * 8;
	if (band_used && gfx_queue_done(band_ticket))
		band_used = 0;
	if (band_used + words > BAND_WORDS || gfx_queue_len() >= GFX_QUEUE_CAP - 2) {
		ui_wake();
		return 0;
	}
	struct text_window win = {band_buf + band_used, (u8)tw, (u8)bt};
	struct pen p = {&win, (s32)ty0 * 8, ya, yb};
	render(node, &p);
	if (!gfx_queue_copy2d(tile_addr(node, c->first) + ya, th * 32, win.tiles + (ya - (s32)ty0 * 8), bt * 32,
			      (u32)(yb - ya) * 4, tw))
		return 0;
	band_used += words;
	band_ticket = gfx_queue_ticket();
	return 1;
}

// Shows the canvas in the node's rectangle (staged while its view shows):
// 0 to try again
static int canvas_map(u8 node, struct canvas *c, u32 bank)
{
	const struct ui_node *n = ui_node(node);
	u32 tx = (u32)n->x / 8, ty = (u32)n->y / 8, tw = (u32)n->w / 8, th = (u32)n->h / 8;
	u16 *rows = ui_map_rows(node, ty, th);
	if (!rows)
		return 0;
	for (u32 y = 0; y < th; y++)
		for (u32 x = 0; x < tw; x++)
			rows[y * 32 + tx + x] = ui_entry(c->first + x * th + y, bank);
	c->mapped = 1;
	return 1;
}

// The whole tiles around a card of w x h at (x, y) and its shadow
static void card_rect(s32 x, s32 y, s32 w, s32 h, s16 *out)
{
	s32 x1 = x + w + look.shadow_x, y1 = y + h + look.shadow_y;
	out[0] = (s16)(x & ~7);
	out[1] = (s16)(y & ~7);
	out[2] = (s16)(((x1 + 7) & ~7) > 240 ? 240 : (x1 + 7) & ~7);
	out[3] = (s16)(((y1 + 7) & ~7) > 160 ? 160 : (y1 + 7) & ~7);
}

// A motion's curve at step of frames, in Q15 (EASE_ONE at the end, and at
// most that: a curve that overshoots would wrap the window and slide maths)
static u32 eased(u32 ease, u32 step, u32 frames)
{
	u32 e = frames && step < frames ? ease_at((Ease)ease, step * 0x10000u / frames) : EASE_ONE;
	return e > EASE_ONE ? EASE_ONE : e;
}

static void measure(const char *name)
{
	if (tlog_in_emulator())
		frame_measure(name);
}

// --- Runs ------------------------------------------------------------------
//
// A popup or a confirmation up: its view, its motion and its answer, and
// the page's hint bar it took over. The calls run frames until it is done.

#define ANSWER_NONE (-2)
#define ANSWER_B (-1)

struct run {
	struct ui_view *view;  // NULL: none up
	u8 node;
	u8 hints;     // the page's hint bar, UI_NONE
	s8 dir;       // 1 opening, -1 closing, 0 open
	u8 waiting;   // its first frame isn't on screen yet
	u8 hinted;    // the hint bar shows its keys
	const struct ui_view_spec *hints_view;  // the hint bar's view
	u8 step;      // frames of the motion running
	u16 open;     // how open it is (Q15, EASE_ONE open)
	u16 from;     // where a close started from
	u32 drawn_frame;  // the frame of its first draw
	s16 answer;
	u16 keys_after;   // keys pressed while it closed: the page's
	struct ui_spec node_spec;
	struct ui_view_spec view_spec;
	struct ui_hint hint[4];
	u8 hint_count;
	const struct ui_hint *old_hints;
	u32 old_count;
	const char *old_text;
};

static struct {
	struct run pop, conf;
	// The popup, or the field card
	const struct ui_popup *menu;
	const struct ui_fields *fields;  // a field card's (menu NULL)
	u16 key;                         // the key ui_fields_key returns, 0 none yet
	s16 top;      // its card's top, screen px
	s16 notch_y;  // the notch's line (the row's middle)
	s16 row_y0, row_y1;  // the row's lines (in the card): the card opens from them
	u16 h;        // its card's height
	u8 cursor;
	u8 titled;    // it shows its title (it has one, and room for it)
	u16 redraw;   // items to draw again, a bit each
	// The confirmation
	const struct ui_confirm *ask;
	u8 sel;            // 0 Cancel, 1 the action
	u8 redraw_buttons;
	u8 card_off;       // answered: its card comes off the map
	u8 over_popup;     // it opened over the popup, which comes back after
	u8 share;          // its card is drawn in the popup's tiles (SHARE_*)
	u32 share_ticket;  // the map change that hid one or the other
	u8 back;           // answered: the popup is back on its map
	u8 bank;           // its two palette banks: the dimmed popup's, then its own
	u16 base[16];      // bank 0 of the theme, which both follow
} pp EWRAM_BSS;

// 1 if the run's view is still up. A screen leaving (ui_leave), a launch
// or ui_shutdown pops it behind the run's back: a later view in its slot
// has another spec, and the run is forgotten.
static int run_live(struct run *r)
{
	if (r->view && (u32)(r->view - ui_view_at(0)) < ui_depth() && r->view->spec == &r->view_spec)
		return 1;
	r->view = 0;
	return 0;
}

// Keys pressed while a run closes that the view under it gets: moves only.
// A, B or SELECT then could act on a page the user can't see yet (B then A
// would play the game under the popup). START, which opens the popup, goes
// on after a popup's close: START and B mashed open and close it, and a
// START dropped let the next B reach the page.
#define KEYS_AFTER (INPUT_DPAD | INPUT_L | INPUT_R)
#define KEYS_AFTER_POPUP (KEYS_AFTER | INPUT_START)

static void run_start(struct run *r, struct ui_view *v, u8 hints)
{
	r->view = v;
	r->node = v->first;
	r->hints = hints;
	r->dir = 1;
	r->waiting = 1;
	r->step = 1;
	r->answer = ANSWER_NONE;
	r->keys_after = 0;
	r->hinted = 0;
	v->opening = 1;  // not still: "screen ready" waits for the motion
}

// One frame of a run's motion: open (opening, over open's frames) or close
// (from where it was, over close's); 0 while its first frame isn't on
// screen. Sets r->open.
static int run_tick(struct run *r, u32 dt, const struct ui_move *open, const struct ui_move *close)
{
	if (r->waiting) {
		if (frame_count() == r->drawn_frame || (gfx_dirty_regs & GFX_DIRTY_DISPCNT) || !r->drawn_frame) {
			ui_wake();
			return 0;
		}
		r->waiting = 0;  // its first step is on screen: the next one now
		if (r->hints != UI_NONE) {
			// Its keys in the hint bar, a frame after its own draw (each
			// draws a lot: they needn't share a frame)
			ui_hints_get(r->hints, &r->old_hints, &r->old_count, &r->old_text);
			r->hints_view = ui_node_view(r->hints)->spec;
			ui_hints_set(r->hints, r->hint, r->hint_count);
			r->hinted = 1;
		}
	}
	if (r->dir > 0) {
		u32 frames = open->frames;
		r->step = (u8)(r->step + dt > frames ? frames : r->step + dt);
		r->open = (u16)eased(open->ease, r->step, frames);
		if (r->step >= frames) {
			r->dir = 0;
			r->view->opening = 0;
		}
	} else if (r->dir < 0) {
		u32 frames = close->frames;
		r->step = (u8)(r->step + dt > frames ? frames : r->step + dt);
		u32 c = eased(close->ease, r->step, frames);
		r->open = (u16)(r->from * (EASE_ONE - c) >> 15);
	} else {
		return 1;
	}
	ui_wake();
	return 1;
}

// The opening's first step, shown with the view's first frame
static void run_first_step(struct run *r, const struct ui_move *open)
{
	r->open = (u16)eased(open->ease, 1, open->frames);
	if (!open->frames) {
		r->dir = 0;
		r->view->opening = 0;
	}
}

// Closes a run: its motion backwards from where it is, then the pop, the
// hint bar given back and the keys pressed meanwhile passed on
static void run_finish(struct run *r, const struct ui_move *close)
{
	if (!run_live(r))
		return;
	r->dir = -1;
	r->from = r->open;
	r->step = 0;
	r->waiting = 0;
	r->view->opening = -1;
	for (u32 guard = 0; guard < 4u * 60 && ui_top() == r->view &&
			    ((r->step < close->frames && r->open) || (r == &pp.conf && pp.card_off && !pp.back));
	     guard++)
		ui_frame();
	u16 keys = r->keys_after;
	TLOG_ASSERT(ui_top() == r->view);
	if (ui_top() == r->view)
		ui_pop();
	r->view = 0;
	ui_overlay_drop(r);
	// The hint bar back, unless its view went meanwhile
	if (r->hinted && r->hints < ui_nodes_used() && ui_node_view(r->hints)->spec == r->hints_view) {
		ui_hints_set(r->hints, r->old_hints, r->old_count);
		if (r->old_text)
			ui_hints_text(r->hints, r->old_text);
	}
	if (keys)
		ui_queue_keys(keys);
}

// A run's view spec: one node of kind over the rectangle
static void run_spec(struct run *r, const char *name, u8 kind, const s16 *rect)
{
	r->node_spec = (struct ui_spec){kind,
					1,
					UI_NONE,
					0,
					UI_LEFT,
					UI_FOCUSABLE,
					0,
					0,
					rect[0],
					rect[1],
					(s16)(rect[2] - rect[0]),
					(s16)(rect[3] - rect[1]),
					0};
	r->view_spec = (struct ui_view_spec){name, &r->node_spec, 1, 0, 0, 0};
}

// --- The popup -------------------------------------------------------------

struct popup_state {
	struct canvas c;
};

// An item's top, screen px
static s32 item_top(u32 i)
{
	return pp.top + (pp.titled ? look.pop_head_h : look.pop_pad_top) + (s32)(i * look.pop_pitch);
}

// The field card's fields at (x, y), the card's top left: centred, the
// active one in its box, with arrows
static void fields_render(const struct pen *p, s32 x, s32 y)
{
	const struct ui_theme *t = ui_theme();
	const struct torii_font *font = t->body, *big = t->heading;
	const struct ui_fields *f = pp.fields;
	s32 w = look.pop_w;
	if (f->icon8)
		pen_icon8(p, x + look.fld_icon_x, y + look.fld_icon_y, f->icon8 - 1u, UI_ROLE_TEXT);
	pen_text(p, font, x + look.fld_title_x, y + look.fld_title_y, (u32)(w - look.fld_title_x - look.fld_rule_in),
		 UI_ROLE_TEXT, f->title, PEN_LEFT);
	pen_fill(p, x + look.fld_rule_in, y + look.fld_rule_y, w - 2 * look.fld_rule_in, 1, UI_ROLE_BORDER);
	u32 n = f->count < UI_FIELDS_MAX ? f->count : UI_FIELDS_MAX, fw[UI_FIELDS_MAX], sw[UI_FIELDS_MAX], total = 0;
	for (u32 i = 0; i < n; i++) {
		fw[i] = f->text[i] ? text_width(big, f->text[i], strlen(f->text[i])) : 0;
		sw[i] = i + 1 < n && f->sep[i] ? text_width(big, f->sep[i], strlen(f->sep[i])) : 0;
		total += fw[i] + sw[i];
	}
	s32 fx = x + (w - (s32)total) / 2, ly = y + look.fld_line_y;
	for (u32 i = 0; i < n; i++) {
		if (i == f->active) {
			s32 bx = fx - look.fld_box_pad, bw = (s32)fw[i] + 2 * look.fld_box_pad, by = ly - 1;
			pen_round(p, bx, by, bw, look.fld_box_h, look.fld_box_r, UI_ROLE_TINT);
			// Arrows: a point above the box, widening down; one below, narrowing
			s32 ax = bx + (bw - look.fld_arrow_w) / 2;
			for (s32 r = 0; r < look.fld_arrow_h; r++) {
				s32 half = r, up = by - look.fld_arrow_gap - look.fld_arrow_h + r;
				s32 down = by + look.fld_box_h + look.fld_arrow_gap + (look.fld_arrow_h - 1 - r);
				s32 cx = ax + look.fld_arrow_w / 2;
				pen_fill(p, cx - half, up, 2 * half + 1, 1, UI_ROLE_ACCENT_INK);
				pen_fill(p, cx - half, down, 2 * half + 1, 1, UI_ROLE_ACCENT_INK);
			}
		}
		pen_text(p, big, fx, ly, fw[i] + 1, i == f->active ? UI_ROLE_TEXT : UI_ROLE_DIM, f->text[i], PEN_LEFT);
		fx += (s32)fw[i];
		if (sw[i])
			pen_text(p, big, fx, ly, sw[i] + 1, UI_ROLE_FAINT, f->sep[i], PEN_LEFT);
		fx += (s32)sw[i];
	}
}

static void popup_render(u8 node, const struct pen *p)
{
	const struct ui_node *n = ui_node(node);
	const struct ui_look *tl = &ui_theme()->look;
	const struct torii_font *font = ui_theme()->body;
	const struct ui_popup *m = pp.menu;
	s32 x = look.pop_x - n->x, y = pp.top - n->y, w = look.pop_w, ny = pp.notch_y - n->y, k = look.pop_notch;
	pen_card(p, x, y, w, pp.h);
	// The notch: a little triangle at the row, its tip k px out, opening
	// into the card through the hairline
	for (s32 i = 1; i <= k; i++) {
		s32 half = k - i;
		pen_fill(p, x - i, ny - half, 1, 2 * half + 1, UI_ROLE_FLOAT);
		pen_fill(p, x - i, ny - half, 1, 1, UI_ROLE_BORDER);
		pen_fill(p, x - i, ny + half, 1, 1, UI_ROLE_BORDER);
	}
	if (k >= 2)
		pen_fill(p, x, ny - (k - 2), 1, 2 * (k - 2) + 1, UI_ROLE_FLOAT);
	if (pp.fields) {
		fields_render(p, x, y);
		return;
	}
	if (pp.titled) {
		s32 tx = look.pop_text_x;
		if (m->icon8) {
			pen_icon8(p, x + look.pop_icon_x, y + look.pop_title_y + ((s32)font->line_height - 8) / 2,
				  m->icon8 - 1u, UI_ROLE_ACCENT_INK);
			tx = look.pop_title_x;
		}
		pen_text(p, font, x + tx, y + look.pop_title_y, (u32)(w - tx - look.pop_rule_in), UI_ROLE_TEXT,
			 m->title, PEN_LEFT);
		pen_fill(p, x + look.pop_rule_in, y + look.pop_rule_y, w - 2 * look.pop_rule_in, 1, UI_ROLE_BORDER);
	}
	for (u32 i = 0; i < m->count; i++) {
		s32 iy = item_top(i) - n->y;
		if (iy + look.pop_pitch <= p->ya || iy >= p->yb)
			continue;
		const struct ui_pop_item *it = &m->items[i];
		if (i == pp.cursor)
			pen_round(p, x + look.pop_pill_in, iy + ((s32)look.pop_pitch - tl->pill_h) / 2,
				  w - 2 * look.pop_pill_in, tl->pill_h, tl->pill_radius, UI_ROLE_TINT);
		s32 right = w - look.pop_value_r, clip = right - look.pop_text_x;
		if (it->value && *it->value) {
			s32 vw = (s32)text_width(font, it->value, strlen(it->value));
			pen_text(p, font, x + right, iy, (u32)vw + 1, UI_ROLE_ACCENT_INK, it->value, PEN_RIGHT);
			clip -= vw + look.pop_value_gap;
		}
		if (clip > 0)
			pen_text(p, font, x + look.pop_text_x, iy, (u32)clip,
				 (it->flags & UI_POP_DANGER) ? UI_ROLE_DANGER
				 : (it->flags & UI_POP_DIM)  ? UI_ROLE_FAINT
				 : i == pp.cursor            ? UI_ROLE_TEXT
							     : UI_ROLE_DIM,
				 it->text, PEN_LEFT);
	}
}

// The registers for the popup open by e (Q15): a window from the row's
// lines up and down to the card's (so no sliver of the row shows above or
// below it), and the card slid right by what's left
static void popup_overlay(u32 e)
{
	s32 slide = (s32)((ui_theme()->motion.popup_slide * (EASE_ONE - e) + EASE_ONE / 2) >> 15);
	s32 top = pp.top, bottom = pp.top + pp.h + look.shadow_y, r0 = pp.row_y0, r1 = pp.row_y1;
	struct ui_overlay o = {
		(s16)(look.pop_x - look.pop_notch + slide),
		(s16)(r0 - (((r0 - top) * (s32)e + EASE_ONE / 2) >> 15)),
		(s16)(look.pop_x + look.pop_w + look.shadow_x + slide),
		(s16)(r1 + (((bottom - r1) * (s32)e + EASE_ONE / 2) >> 15)),
		(s16)slide,
		(u8)(e < EASE_ONE),
		0,
		&pp.pop,
	};
	if (o.y1 <= o.y0)
		o.y0 = o.y1 = 160;  // nothing
	ui_overlay_set(&o);
}

static int popup_measure(u8 node, struct engine_budget *need)
{
	// An item's band is redrawn alone: its pill and its text line stay in it
	const struct ui_theme *t = ui_theme();
	if (t->look.pill_h > look.pop_pitch || t->body->line_height > look.pop_pitch) {
		ui_set_error("popup: its pitch (%u px) is under the pill's height or the body font's line",
			     look.pop_pitch);
		return 0;
	}
	return canvas_measure(node, need);
}

static int popup_init(u8 node)
{
	struct popup_state *st = ui_node_state(node);
	return canvas_init(node, &st->c);
}

static int popup_event(u8 node, const struct ui_event *e)
{
	(void)node;
	struct run *r = &pp.pop;
	if (e->type != UI_EV_KEYS || !r->view)
		return 0;
	if (r->dir < 0) {
		r->keys_after |= e->keys & KEYS_AFTER_POPUP;  // for the page, once it has closed
		return 1;
	}
	if (pp.fields) {
		// The first of B, A (once open), then the d-pad
		static const u16 order[] = {INPUT_B, INPUT_A, INPUT_UP, INPUT_DOWN, INPUT_LEFT, INPUT_RIGHT};
		for (u32 i = 0; i < sizeof(order) / sizeof(order[0]) && !pp.key; i++)
			if ((e->keys & order[i]) && (order[i] != INPUT_A || !r->dir))
				pp.key = order[i];
		return 1;
	}
	if (e->keys & INPUT_B) {
		r->answer = ANSWER_B;
	} else if (e->keys & INPUT_A) {
		if (!r->dir)  // not while it opens: a quick double press isn't a pick
			r->answer = pp.cursor;
	} else if (e->keys & (INPUT_UP | INPUT_DOWN)) {
		u16 key = e->keys & INPUT_UP ? INPUT_UP : INPUT_DOWN;
		u32 to = input_move(ui_input(), key, pp.cursor, pp.menu->count, key == INPUT_UP ? -1 : 1);
		if (to != pp.cursor) {
			pp.redraw |= (u16)(1u << pp.cursor | 1u << to);
			pp.cursor = (u8)to;
			ui_dirty(r->node, 1);
		}
	}
	return 1;  // nothing reaches the page meanwhile
}

static void popup_update(u8 node, u32 dt)
{
	struct run *r = &pp.pop;
	if (!r->view || node != r->node || r->view != ui_node_view(node) || !ui_node_shown(node))
		return;
	int moving = r->dir != 0;
	if (run_tick(r, dt, &ui_theme()->motion.popup_open, &ui_theme()->motion.popup_close) && moving) {
		popup_overlay(r->open);  // open: no window, plane 0 everywhere
		measure("popup-motion");
	}
}

static void popup_draw(u8 node)
{
	struct popup_state *st = ui_node_state(node);
	if (!st->c.mapped) {
		canvas_whole(node, &st->c, popup_render);
		canvas_map(node, &st->c, UI_BANK_NORMAL);  // not shown yet: straight into the map
		pp.pop.drawn_frame = frame_count();
		pp.redraw = 0;
		measure("popup-draw");
		return;
	}
	if (pp.fields) {
		// The fields' band: from the arrow above the box to the one below
		s32 a = pp.top + look.fld_line_y - 1 - look.fld_arrow_gap - look.fld_arrow_h - ui_node(node)->y;
		s32 b = a + look.fld_arrow_h + look.fld_arrow_gap + look.fld_box_h + look.fld_arrow_gap +
			look.fld_arrow_h;
		if (!pp.redraw)
			return;
		if (canvas_band(node, &st->c, a, b, popup_render)) {
			pp.redraw = 0;
			measure("fields-move");
		} else {
			ui_dirty(node, 1);
		}
		return;
	}
	for (u32 i = 0; i < UI_POPUP_ITEMS; i++) {
		if (!(pp.redraw >> i & 1))
			continue;
		if (!canvas_band(node, &st->c, item_top(i) - ui_node(node)->y,
				 item_top(i) + look.pop_pitch - ui_node(node)->y, popup_render)) {
			ui_dirty(node, 1);
			break;
		}
		pp.redraw &= (u16) ~(1u << i);
		measure("popup-move");
	}
}

const struct ui_ops ui_popup_ops = {
	sizeof(struct popup_state), popup_measure, popup_init, popup_event, popup_update, popup_draw};

// Opens the popup's card, h px tall, beside list's selected row (pp.menu
// or pp.fields set): 0 if it can't
static int card_open(u8 list, u8 hints, u32 h)
{
	TLOG_ASSERT(hints == UI_NONE || ui_node_spec(hints)->kind == UI_HINTS);
	// Beside the row where it will rest, its top clamped under the title bar
	// and over the hint bar
	s32 row, top;
	u32 rh;
	ui_list_cursor_row(list, &row, &rh);
	row += ui_node_view(list)->offset;
	u32 r = ui_theme()->look.card_radius, k = look.pop_notch;
	top = row - look.pop_above;
	top = top > (s32)look.pop_bottom_max - (s32)h ? (s32)look.pop_bottom_max - (s32)h : top;
	top = top < look.pop_top_min ? look.pop_top_min : top;
	s32 mid = row + (s32)rh / 2, lo = top + (s32)(r + k), hi = top + (s32)h - 1 - (s32)(r + k);
	pp.top = (s16)top;
	pp.h = (u16)h;
	pp.notch_y = (s16)(mid < lo ? lo : mid > hi ? hi : mid);
	s32 r0 = row < top ? top : row, r1 = row + (s32)rh > top + (s32)h ? top + (s32)h : row + (s32)rh;
	pp.row_y0 = (s16)(r0 < r1 ? r0 : pp.notch_y);
	pp.row_y1 = (s16)(r0 < r1 ? r1 : pp.notch_y);
	pp.redraw = 0;
	pp.key = 0;
	s16 rect[4];
	card_rect(look.pop_x - (s32)k, top, look.pop_w + (s32)k, (s32)h, rect);
	run_spec(&pp.pop, "popup", UI_POPUP, rect);
	struct ui_view *v = ui_push(&pp.pop.view_spec, 0);
	if (!v) {
		tlog_error("popup: %s", ui_error());
		return 0;
	}
	pp.pop.drawn_frame = 0;
	run_start(&pp.pop, v, hints);
	run_first_step(&pp.pop, &ui_theme()->motion.popup_open);
	popup_overlay(pp.pop.open);
	return 1;
}

int ui_popup_open(u8 list, u8 hints, const struct ui_popup *popup)
{
	TLOG_ASSERT(!ui_dispatching);
	if (run_live(&pp.pop))
		ui_popup_close();
	if (!popup || !popup->count || popup->count > UI_POPUP_ITEMS || list == UI_NONE ||
	    ui_node_spec(list)->kind != UI_LIST) {
		ui_set_error("popup: 1 to %d items, beside a list", UI_POPUP_ITEMS);
		tlog_error("%s", ui_error());
		return 0;
	}
	pp.menu = popup;
	pp.fields = 0;
	pp.cursor = popup->cursor < popup->count ? popup->cursor : 0;
	pp.pop.hint[0] = (struct ui_hint){KEY_UPDOWN, gl_pop_choose};
	pp.pop.hint[1] = (struct ui_hint){"A", gl_pop_ok};
	pp.pop.hint[2] = (struct ui_hint){"B", gl_pop_close};
	pp.pop.hint_count = 3;
	// A title only where the card still fits between the bars with it (8
	// items do; the SD file popup's 9 go without, the row beside it named)
	u32 items = popup->count * look.pop_pitch + look.pop_pad_bottom;
	pp.titled = popup->title && look.pop_head_h + items <= (u32)(look.pop_bottom_max - look.pop_top_min);
	return card_open(list, hints, (pp.titled ? look.pop_head_h : look.pop_pad_top) + items);
}

int ui_fields_open(u8 list, u8 hints, const struct ui_fields *fields, const struct ui_hint *keys, u32 key_count)
{
	TLOG_ASSERT(!ui_dispatching);
	if (run_live(&pp.pop))
		ui_popup_close();
	if (!fields || !fields->count || fields->count > UI_FIELDS_MAX || list == UI_NONE ||
	    ui_node_spec(list)->kind != UI_LIST) {
		ui_set_error("fields: 1 to %d fields, beside a list", UI_FIELDS_MAX);
		tlog_error("%s", ui_error());
		return 0;
	}
	pp.menu = 0;
	pp.fields = fields;
	u32 n = key_count < 4 ? key_count : 4;
	for (u32 i = 0; i < n; i++)
		pp.pop.hint[i] = keys[i];
	pp.pop.hint_count = (u8)n;
	return card_open(list, hints, look.fld_h);
}

u16 ui_fields_key(void)
{
	TLOG_ASSERT(!ui_dispatching);
	struct run *r = &pp.pop;
	if (!run_live(r) || !pp.fields)
		return 0;
	pp.key = 0;
	while (!pp.key && ui_top() == r->view)
		ui_frame();
	return ui_top() == r->view ? pp.key : 0;
}

void ui_fields_changed(void)
{
	if (!run_live(&pp.pop) || !pp.fields)
		return;
	pp.redraw = 1;
	ui_dirty(pp.pop.node, 1);
}

s32 ui_popup_choose(void)
{
	TLOG_ASSERT(!ui_dispatching);
	struct run *r = &pp.pop;
	if (!run_live(r) || !pp.menu)
		return -1;
	r->answer = ANSWER_NONE;
	while (r->answer == ANSWER_NONE && ui_top() == r->view)
		ui_frame();
	if (r->answer < 0) {
		tlog_debug("popup: closed");  // tests/shots' walks wait for these
		ui_popup_close();
		return -1;
	}
	tlog_debug("popup: item %ld", (long)r->answer);
	return r->answer;
}

void ui_popup_changed(u32 item)
{
	if (!run_live(&pp.pop) || !pp.menu || item >= pp.menu->count)
		return;
	pp.redraw |= (u16)(1u << item);
	ui_dirty(pp.pop.node, 1);
}

void ui_popup_close(void)
{
	TLOG_ASSERT(!ui_dispatching);
	run_finish(&pp.pop, &ui_theme()->motion.popup_close);
}

// --- The confirmation ------------------------------------------------------

enum {
	SHARE_NO,      // its own tiles
	SHARE_HIDE,    // the popup's: the popup's map entries go first,
	SHARE_HIDING,  // and once that has landed, the card is drawn over its tiles
	SHARE_DRAWN,
	SHARE_BACK,    // answered: the card's map entries went; once that has
		       // landed, the popup is drawn back in its tiles
};

struct confirm_state {
	struct canvas c;
	u8 banks;  // res handle of pp.bank's two
};

static void confirm_render(u8 node, const struct pen *p)
{
	const struct ui_node *n = ui_node(node);
	const struct ui_look *tl = &ui_theme()->look;
	const struct torii_font *font = ui_theme()->body;
	const struct ui_confirm *c = pp.ask;
	s32 x = look.conf_x - n->x, y = look.conf_y - n->y, w = look.conf_w, pad = look.conf_pad;
	pen_card(p, x, y, w, look.conf_h);
	pen_text(p, font, x + pad, y + look.conf_title_y, (u32)(w - 2 * pad), UI_ROLE_TEXT, c->title, PEN_CENTRE);
	pen_text(p, font, x + pad, y + look.conf_text_y, (u32)(w - 2 * pad), UI_ROLE_FAINT, c->text, PEN_CENTRE);
	// Cancel, then the action; the one chosen solid
	s32 bw = look.conf_button_w, bh = look.conf_button_h, by = y + look.conf_button_y;
	s32 bx = x + (w - 2 * bw - look.conf_button_gap) / 2;
	u32 r = tl->button_radius;
	for (u32 b = 0; b < 2; b++, bx += bw + look.conf_button_gap) {
		int danger = b && c->danger, chosen = pp.sel == b;
		u32 ink;
		if (chosen) {
			pen_round(p, bx, by, bw, bh, r, danger ? UI_ROLE_DANGER : UI_ROLE_ACCENT);
			ink = danger ? UI_ROLE_FLOAT : UI_ROLE_ACCENT_TEXT;
		} else {
			pen_round(p, bx, by, bw, bh, r, UI_ROLE_BORDER);
			pen_round(p, bx + 1, by + 1, bw - 2, bh - 2, r ? r - 1 : 0, UI_ROLE_FLOAT);
			ink = danger ? UI_ROLE_DANGER : UI_ROLE_TEXT;
		}
		pen_text(p, font, bx + 2, by + (bh - (s32)font->line_height) / 2, (u32)(bw - 4), ink,
			 b ? c->action : gl_pop_cancel_button, PEN_CENTRE);
	}
}

// The colours for the confirmation open by e (Q15): the dim, the popup
// under it dimmed by the same maths, and its own fading in from the dimmed
// page
static void confirm_colours(u32 e)
{
	const struct ui_theme *t = ui_theme();
	u32 evy = (t->confirm_dim * e + EASE_ONE / 2) >> 15, amount = (e * 256u + EASE_ONE / 2) >> 15;
	int up = t->confirm_brighten;
	u16 colours[32];
	u16 under = ui_colour_bldy(pp.base[UI_ROLE_PANEL], evy, up);
	for (u32 i = 0; i < 16; i++) {
		colours[i] = ui_colour_bldy(pp.base[i], evy, up);
		colours[16 + i] = ui_colour_mix(under, pp.base[i], amount);
	}
	gfx_pal_load(pp.bank, colours, 32);
	struct ui_overlay o = {0, 0, 0, 0, 0, 0, (u8)evy, &pp.conf};
	ui_overlay_set(&o);
}

static int confirm_measure(u8 node, struct engine_budget *need)
{
	need->bg_banks = (u8)(need->bg_banks + 2);
	return pp.share ? 1 : canvas_measure(node, need);
}

// The popup's canvas, which a sharing confirmation draws over
static struct canvas *popup_canvas(void)
{
	return &((struct popup_state *)ui_node_state(pp.pop.node))->c;
}

static int confirm_init(u8 node)
{
	struct confirm_state *st = ui_node_state(node);
	if (pp.share) {
		st->c.first = popup_canvas()->first;
		st->c.tiles = RES_NONE;  // the popup's
		st->c.mapped = 0;
	} else if (!canvas_init(node, &st->c)) {
		return 0;
	}
	st->banks = res_palette(RES_BG_PAL, 2, 0);
	if (st->banks == RES_NONE) {
		ui_set_error("view %s: no palette banks left for a confirmation", ui_node_view(node)->spec->name);
		return 0;
	}
	pp.bank = (u8)res_start(RES_BG_PAL, st->banks);
	u16 banks[UI_BANKS * 16];
	ui_theme_banks(ui_theme(), ui_item(), banks);
	memcpy(pp.base, banks, sizeof(pp.base));
	return 1;
}

static int confirm_event(u8 node, const struct ui_event *e)
{
	(void)node;
	struct run *r = &pp.conf;
	if (e->type != UI_EV_KEYS || !r->view)
		return 0;
	if (r->dir < 0) {
		r->keys_after |= e->keys & KEYS_AFTER;
		return 1;
	}
	if (e->keys & INPUT_B) {
		r->answer = 0;
	} else if (e->keys & INPUT_A) {
		if (!r->dir)
			r->answer = pp.sel;
	} else if (e->keys & (INPUT_LEFT | INPUT_RIGHT)) {
		u8 sel = e->keys & INPUT_LEFT ? 0 : 1;
		if (sel != pp.sel) {
			pp.sel = sel;
			pp.redraw_buttons = 1;
			ui_dirty(r->node, 1);
		}
	}
	return 1;
}

static void confirm_update(u8 node, u32 dt)
{
	struct run *r = &pp.conf;
	if (!r->view || node != r->node || r->view != ui_node_view(node) || !ui_node_shown(node))
		return;
	int moving = r->dir != 0;
	if (run_tick(r, dt, &ui_theme()->motion.popup_open, &ui_theme()->motion.popup_close) && moving) {
		confirm_colours(r->open);
		measure("confirm-motion");
	}
}

// The popup's map entries in the dimmed bank, into map rows [ty0, ty1) of
// to (to's first row is ty0), from its canvas (its own map may be staged)
static void popup_entries(u16 *to, s32 ty0, s32 ty1)
{
	const struct ui_node *pn = ui_node(pp.pop.node);
	const struct canvas *pc = popup_canvas();
	s32 px = pn->x / 8, py = pn->y / 8, pw = pn->w / 8, ph = pn->h / 8;
	for (s32 y = py > ty0 ? py : ty0; y < py + ph && y < ty1; y++)
		for (s32 x = px; x < px + pw; x++)
			to[(y - ty0) * 32 + x] = ui_entry(pc->first + (u32)((x - px) * ph + (y - py)), pp.bank);
}

// Answered: the card's entries off the confirmation's map (card), and the
// popup's on it, dimmed (popup); 0 to try again
static int card_off_map(u8 node, int card, int popup)
{
	const struct ui_node *n = ui_node(node), *pn = ui_node(pp.pop.node);
	popup = popup && pp.over_popup && run_live(&pp.pop);
	s32 ty0 = n->y / 8, ty1 = (n->y + n->h) / 8;
	if (popup) {
		ty0 = pn->y / 8 < ty0 ? pn->y / 8 : ty0;
		ty1 = (pn->y + pn->h) / 8 > ty1 ? (pn->y + pn->h) / 8 : ty1;
	}
	u16 *rows = ui_map_rows(node, (u32)ty0, (u32)(ty1 - ty0));
	if (!rows)
		return 0;
	if (card) {
		u16 blank = ui_shared_entry(0, UI_TILE_BLANK);
		for (s32 y = n->y / 8; y < (n->y + n->h) / 8; y++)
			for (s32 x = n->x / 8; x < (n->x + n->w) / 8; x++)
				rows[(y - ty0) * 32 + x] = blank;
	}
	if (popup)
		popup_entries(rows, ty0, ty1);
	return 1;
}

// 1 once the map change of pp.share_ticket has landed; else asks for
// another frame, whose draw (a whole canvas) is heavy
static int share_landed(u8 node)
{
	if (gfx_queue_done(pp.share_ticket))
		return 1;
	ui_dirty(node, 1);
	ui_draw_heavy();
	return 0;
}

static void confirm_draw(u8 node)
{
	struct confirm_state *st = ui_node_state(node);
	const struct ui_node *n = ui_node(node);
	if (pp.card_off) {
		// Answered: the card goes at once, and the dim fades out alone over
		// the popup, which comes back dimmed
		if (pp.share == SHARE_DRAWN) {
			// Its tiles hold the card: the card's entries go first
			if (!card_off_map(node, 1, 0)) {
				ui_dirty(node, 1);
				return;
			}
			st->c.mapped = 0;
			pp.share = SHARE_BACK;
			pp.share_ticket = gfx_queue_ticket();
			ui_dirty(node, 1);
			ui_draw_heavy();
			return;
		}
		if (pp.share == SHARE_BACK) {
			// Then the popup drawn back in its tiles, unseen: no map shows
			// them now
			if (!share_landed(node))
				return;
			canvas_whole(pp.pop.node, popup_canvas(), popup_render);
			pp.share = SHARE_HIDING;
		}
		if (pp.share == SHARE_HIDING) {
			// Its tiles hold it: on its map again
			if (!canvas_map(pp.pop.node, popup_canvas(), UI_BANK_NORMAL)) {
				ui_dirty(node, 1);
				return;
			}
		}
		pp.share = SHARE_NO;
		if (pp.back)
			return;
		if (!card_off_map(node, st->c.mapped, 1)) {
			ui_dirty(node, 1);
			return;
		}
		st->c.mapped = 0;
		pp.back = 1;
		return;
	}
	if (!st->c.mapped) {
		if (pp.share == SHARE_HIDE) {
			// The popup's entries off its map first: its tiles are about to
			// hold the card
			if (!ui_map_fill(pp.pop.node, ui_shared_entry(0, UI_TILE_BLANK))) {
				ui_dirty(node, 1);
				return;
			}
			pp.share = SHARE_HIDING;
			pp.share_ticket = gfx_queue_ticket();
			ui_dirty(node, 1);
			return;
		}
		if (pp.share == SHARE_HIDING) {
			if (!share_landed(node))
				return;
			pp.share = SHARE_DRAWN;
		}
		canvas_whole(node, &st->c, confirm_render);
		canvas_map(node, &st->c, pp.bank + 1u);  // not shown yet: straight into the map
		pp.conf.drawn_frame = frame_count();
		measure("confirm-draw");
		return;
	}
	if (pp.redraw_buttons) {
		s32 by = look.conf_y + look.conf_button_y - n->y;
		if (!canvas_band(node, &st->c, by, by + look.conf_button_h, confirm_render)) {
			ui_dirty(node, 1);
			return;
		}
		pp.redraw_buttons = 0;
		measure("confirm-move");
	}
}

const struct ui_ops ui_confirm_ops = {
	sizeof(struct confirm_state), confirm_measure, confirm_init, confirm_event, confirm_update, confirm_draw};

int ui_confirm(u8 hints, const struct ui_confirm *confirm)
{
	TLOG_ASSERT(!ui_dispatching);
	if (run_live(&pp.conf) || !confirm)
		return 0;
	pp.ask = confirm;
	pp.sel = 0;
	pp.redraw_buttons = 0;
	TLOG_ASSERT(hints == UI_NONE || ui_node_spec(hints)->kind == UI_HINTS);
	pp.over_popup = run_live(&pp.pop) && ui_top() == pp.pop.view;
	pp.share = SHARE_NO;
	pp.back = 0;
	s16 rect[4];
	card_rect(look.conf_x, look.conf_y, look.conf_w, look.conf_h, rect);
	run_spec(&pp.conf, "confirm", UI_CONFIRM, rect);
	u8 quiet = res_errors_quiet;
	res_errors_quiet = pp.over_popup ? 1 : quiet;  // a first try that may not fit
	struct ui_view *v = ui_push(&pp.conf.view_spec, 0);
	res_errors_quiet = quiet;
	const struct ui_node *pn = pp.over_popup ? ui_node(pp.pop.node) : 0;
	if (!v && pn && (pn->w / 8) * (pn->h / 8) >= ((rect[2] - rect[0]) / 8) * ((rect[3] - rect[1]) / 8)) {
		// No room beside the popup (a tall one on a page whose tiles fill
		// block 0): the card borrows the popup's tiles
		tlog("confirm: no room beside the popup: it takes the popup's tiles");
		pp.share = SHARE_HIDE;
		run_spec(&pp.conf, "confirm", UI_CONFIRM, rect);
		v = ui_push(&pp.conf.view_spec, 0);
		if (!v)
			pp.share = SHARE_NO;
	}
	if (!v && pp.over_popup) {
		// Still none: the popup goes first, and the answer comes back to
		// the page, not to it
		tlog("confirm: no room over the popup: it closes first");
		ui_popup_close();
		pp.over_popup = 0;
		run_spec(&pp.conf, "confirm", UI_CONFIRM, rect);
		v = ui_push(&pp.conf.view_spec, 0);
	}
	if (!v) {
		tlog_error("confirm: %s", ui_error());
		return 0;
	}
	pp.conf.hint[0] = (struct ui_hint){KEY_LEFTRIGHT, gl_pop_choose};
	pp.conf.hint[1] = (struct ui_hint){"A", gl_pop_ok};
	pp.conf.hint[2] = (struct ui_hint){"B", gl_pop_cancel};
	pp.conf.hint_count = 3;
	pp.conf.drawn_frame = 0;
	run_start(&pp.conf, v, hints);
	run_first_step(&pp.conf, &ui_theme()->motion.popup_open);
	confirm_colours(pp.conf.open);
	while (pp.conf.answer == ANSWER_NONE && ui_top() == v)
		ui_frame();
	s32 answer = pp.conf.answer;
	tlog_debug("confirm: %s", answer == 1 ? "yes" : "cancel");
	if (ui_top() == v) {
		pp.card_off = 1;
		ui_dirty(pp.conf.node, 1);
	}
	run_finish(&pp.conf, &ui_theme()->motion.popup_close);
	pp.card_off = 0;
	return answer == 1;
}

// --- The letter card ---------------------------------------------------------

enum {
	LET_OFF,
	LET_DRAW,   // to draw and show
	LET_UP,     // shown, staying
	LET_CLOSE,  // closing
	LET_UNMAP,  // closed: to take off the map
};

struct letter_state {
	struct canvas c;
	u8 letter, prev, next;
	u8 phase;     // LET_*
	u8 left;      // frames it stays up
	u8 step;      // frames of its close
	u8 windowed;  // the overlay's window is its close's
	u32 ticket;   // the map change that took it off
};

static void letter_render(u8 node, const struct pen *p)
{
	const struct letter_state *st = ui_node_state(node);
	const struct ui_theme *t = ui_theme();
	s32 x = look.let_x, y = look.let_y, w = look.let_w;
	pen_card(p, x, y, w, look.let_h);
	s32 bx = x + look.let_box_x, by = y + look.let_box_y;
	pen_round(p, bx, by, look.let_box_w, look.let_box_h, look.let_box_r, UI_ROLE_ACCENT);
	pen_text(p, t->heading, bx, by + ((s32)look.let_box_h - t->heading->line_height) / 2, look.let_box_w,
		 UI_ROLE_ACCENT_TEXT, letter_text(st->letter), PEN_CENTRE);
	u32 side = (u32)(w / 2 - look.let_side_x);
	pen_text(p, t->body, x + look.let_side_x, y + look.let_side_y, side, UI_ROLE_FAINT, letter_text(st->prev),
		 PEN_LEFT);
	pen_text(p, t->body, x + w - look.let_side_x, y + look.let_side_y, side, UI_ROLE_FAINT, letter_text(st->next),
		 PEN_RIGHT);
}

static int letter_measure(u8 node, struct engine_budget *need)
{
	const struct ui_node *n = ui_node(node);
	if (look.let_x + look.let_w + look.shadow_x > n->w || look.let_y + look.let_h + look.shadow_y > n->h ||
	    ui_node_spec(node)->plane) {
		ui_set_error("view %s: the letter card is on plane 0, in a node that holds it (UI_LETTER_NODE)",
			     ui_node_view(node)->spec->name);
		return 0;
	}
	return canvas_measure(node, need);
}

static int letter_init(u8 node)
{
	struct letter_state *st = ui_node_state(node);
	st->phase = LET_OFF;
	return canvas_init(node, &st->c);
}

// Lets go of the overlay's window, if it is still the card's (a popup over
// it may have set its own)
static void letter_unwindow(struct letter_state *st)
{
	ui_overlay_drop(st);
	st->windowed = 0;
}

static void letter_update(u8 node, u32 dt)
{
	struct letter_state *st = ui_node_state(node);
	if (st->phase == LET_OFF) {
		if (st->windowed && gfx_queue_done(st->ticket))
			letter_unwindow(st);  // off the map now
		return;
	}
	if (!ui_node_shown(node))
		return;  // shown with the page
	if (!ui_plane_owned(node)) {
		// Under a popup or a dialog: gone at once
		letter_unwindow(st);
		if (st->phase != LET_UNMAP) {
			st->phase = st->c.mapped ? LET_UNMAP : LET_OFF;
			ui_dirty(node, 1);
		}
		return;
	}
	const struct ui_move *close = &ui_theme()->motion.letter_close;
	if (st->phase == LET_UP) {
		if (st->left > dt) {
			st->left = (u8)(st->left - dt);
		} else {
			st->phase = LET_CLOSE;
			st->step = 0;
			st->windowed = 1;
		}
		ui_wake();
	}
	if (st->phase == LET_CLOSE) {
		st->step = (u8)(st->step + dt > close->frames ? close->frames : st->step + dt);
		u32 e = EASE_ONE - eased(close->ease, st->step, close->frames);
		// The card shuts towards its middle line
		const struct ui_node *n = ui_node(node);
		s32 top = n->y + look.let_y, bottom = top + look.let_h + look.shadow_y, mid = (top + bottom) / 2;
		s32 half = (s32)(((bottom - mid) * (s32)e + EASE_ONE / 2) >> 15);
		s32 x0 = n->x + look.let_x;
		struct ui_overlay o = {(s16)x0,
				       (s16)(half ? mid - half : 160),
				       (s16)(x0 + look.let_w + look.shadow_x),
				       (s16)(half ? mid + half : 160),
				       0,
				       1,
				       0,
				       st};
		ui_overlay_set(&o);
		if (st->step >= close->frames) {
			st->phase = LET_UNMAP;
			ui_dirty(node, 1);
		}
		ui_wake();
	}
}

static void letter_draw(u8 node)
{
	struct letter_state *st = ui_node_state(node);
	if (st->phase == LET_DRAW) {
		const struct ui_node *n = ui_node(node);
		if (!canvas_band(node, &st->c, 0, n->h, letter_render) ||
		    (!st->c.mapped && !canvas_map(node, &st->c, UI_BANK_NORMAL))) {
			ui_dirty(node, 1);
			return;
		}
		st->phase = LET_UP;
		measure("letter-show");
	} else if (st->phase == LET_UNMAP) {
		if (!ui_map_fill(node, ui_shared_entry(0, UI_TILE_BLANK))) {
			ui_dirty(node, 1);
			return;
		}
		st->c.mapped = 0;
		st->ticket = gfx_queue_ticket();
		st->phase = LET_OFF;
		ui_wake();
	}
}

const struct ui_ops ui_letter_ops = {
	sizeof(struct letter_state), letter_measure, letter_init, 0, letter_update, letter_draw};

void ui_letter_show(u8 node, u32 letter, u32 prev, u32 next)
{
	struct letter_state *st = ui_node_state(node);
	st->letter = (u8)letter;
	st->prev = (u8)prev;
	st->next = (u8)next;
	st->left = ui_theme()->motion.letter_linger;
	st->phase = LET_DRAW;
	letter_unwindow(st);  // closing: open again at once
	ui_dirty(node, 1);
}

void ui_letter_hide(u8 node)
{
	struct letter_state *st = ui_node_state(node);
	if (st->phase == LET_OFF)
		return;
	// Off the map; a closing card's window goes once that has landed
	st->phase = st->c.mapped ? LET_UNMAP : LET_OFF;
	ui_dirty(node, 1);
}
