// The Paper clean widgets (ui.h): cards, the title bar, the hint bar, path
// pills and the storage bar, every size from the theme's look
// (docs/design/v0.4/spec.md). The list's pill and its 14 px rows are in
// ui/ui_list.c; section labels are labels with an 8x8 icon
// (ui/ui_widgets.c).
#include <string.h>

#include "../tlog.h"
#include "art.h"
#include "ui_priv.h"
#include "../engine/gfx.h"
#include "../engine/res.h"
#include "../engine/text.h"

#define OBJ_SIZE_16 0x4000  // attr1: 16x16, with the square shape (attr0 0)
#define TAG_CARD 0x6000     // + a card's insets, edge and paper (card_tag)

static const struct ui_look *look(void)
{
	return &ui_theme()->look;
}

static u32 *tile_addr(u8 node, u32 tile)
{
	return (u32 *)(0x06000000 + ui_plane_cb(ui_node_spec(node)->plane) * 0x4000 + tile * 32u);
}

// --- Painted windows ------------------------------------------------------
//
// Tiles a widget draws its pixels into, a column-major window over tile
// columns [tx, tx + tw) of its rectangle: one set, or two that take turns,
// the one the map doesn't show drawn and then flipped in in VBlank, so a
// change never tears (as UI_LABEL_SWAP's).

struct paint {
	u16 first[2];
	u8 tiles[2];
	u8 cur;     // the set the map shows
	u8 sets;    // 1 or 2
	u8 tx, tw;
	u8 mapped;  // the map shows set cur (not blank: paint_hide)
};

static int paint_init(u8 node, struct paint *p, u32 tx, u32 tw, u32 sets)
{
	u32 h = (u32)ui_node(node)->h / 8;
	p->sets = (u8)sets;
	p->tx = (u8)tx;
	p->tw = (u8)tw;
	for (u32 i = 0; i < sets; i++) {
		p->tiles[i] = ui_tiles(node, tw * h, 0, &p->first[i]);
		if (p->tiles[i] == RES_NONE)
			return 0;
	}
	return 1;
}

// Starts drawing p: *win gets the set to draw into. 0 while the last flip
// hasn't landed (its set may still show): try again next frame.
static int paint_begin(u8 node, struct paint *p, struct text_window *win, u32 *set)
{
	const struct ui_node *n = ui_node(node);
	int flip = p->sets == 2 && ui_node_shown(node) && p->mapped;
	if (flip && ui_map_busy(node, (u32)n->y / 8, (u32)n->h / 8))
		return 0;
	*set = flip ? p->cur ^ 1u : p->cur;
	win->tiles = tile_addr(node, p->first[*set]);
	win->width = p->tw;
	win->height = (u8)(n->h / 8);
	return 1;
}

// Shows the set drawn (the first draw, or a flip); 0 to try the flip again
static int paint_end(u8 node, struct paint *p, u32 set, u32 bank)
{
	const struct ui_node *n = ui_node(node);
	if (set != p->cur || !p->mapped) {
		u32 tx = (u32)n->x / 8 + p->tx, ty = (u32)n->y / 8, h = (u32)n->h / 8;
		u16 *rows = ui_map_rows(node, ty, h);
		if (!rows)
			return 0;
		for (u32 y = 0; y < h; y++)
			for (u32 x = 0; x < p->tw; x++)
				rows[y * 32 + tx + x] = ui_entry(p->first[set] + x * h + y, bank);
	}
	p->cur = (u8)set;
	p->mapped = 1;
	return 1;
}

// A hidden node: its rectangle blank (its paints mapped again when it
// shows); 0 to try again
static int paint_hide(u8 node, struct paint *a, struct paint *b)
{
	if (!ui_map_fill(node, ui_shared_entry(ui_node_spec(node)->plane, UI_TILE_BLANK)))
		return 0;
	a->mapped = 0;
	if (b)
		b->mapped = 0;
	return 1;
}

// A node's map without its paints (a card's): blank while hidden
static int map_hide(u8 node)
{
	return ui_map_fill(node, ui_shared_entry(ui_node_spec(node)->plane, UI_TILE_BLANK));
}

static int measure_tiles(u8 node, struct engine_budget *need, u32 cols_sets)
{
	need->bg_tiles = (u16)(need->bg_tiles + cols_sets * (u32)(ui_node(node)->h / 8));
	return 1;
}

// A rounded bar: [x, x + w) x [y, y + h) of role, its ends rounded by
// radius, only its columns in [cx0, cx1)
static void round_bar(const struct text_window *win, u32 x, u32 y, u32 w, u32 h, u32 radius, u32 role, u32 cx0, u32 cx1)
{
	for (u32 i = 0; i < h; i++) {
		u32 e = i < h - 1 - i ? i : h - 1 - i, inset = ui_round_inset(radius, e);
		u32 a = x + inset, b = x + w - inset;
		a = a < cx0 ? cx0 : a;
		b = b > cx1 ? cx1 : b;
		if (a < b)
			text_clear(win, a, y + i, b - a, 1, role);
	}
}

// One line of text at (x, y), cut to clip px; its width
static u32 draw_text(const struct text_window *win, const struct torii_font *font, u32 x, u32 y, u32 clip, u32 ink,
		     const char *s)
{
	struct text_line line;
	if (!s || !*s)
		return 0;
	text_layout(font, s, strlen(s), clip, &line);
	text_draw_line(win, font, x, y, ink, &line);
	return line.advance;
}

// The line top that centres font's line on a band [top, top + h): may be
// above the band (a 12 px line on a 10 px chip), never above 0
static u32 text_top(const struct torii_font *font, u32 top, u32 h)
{
	s32 y = (s32)top + ((s32)h - (s32)font->line_height) / 2;
	return y < 0 ? 0 : (u32)y;
}

u32 ui_key_chip(const struct text_window *win, u32 x, u32 top, const char *key, const char *label, int draw)
{
	const struct ui_look *l = look();
	const struct torii_font *font = ui_theme()->body;
	u32 kw = key ? text_width(font, key, strlen(key)) : 0, cw = kw + 2u * l->hint_chip_pad;
	u32 lw = label && *label ? text_width(font, label, strlen(label)) : 0;
	u32 y = text_top(font, top, l->hint_chip_h), ww = win->width * 8u;
	if (draw && x + cw <= ww) {
		round_bar(win, x, top, cw, l->hint_chip_h, l->hint_chip_radius, UI_ROLE_TRACK, 0, ww);
		draw_text(win, font, x + l->hint_chip_pad, y, kw + 1, UI_ROLE_DIM, key);
		if (lw && x + cw + l->hint_label_gap < ww)
			draw_text(win, font, x + cw + l->hint_label_gap, y, ww - x - cw - l->hint_label_gap,
				  UI_ROLE_FAINT, label);
	}
	return cw + (lw ? l->hint_label_gap + lw : 0);
}

// --- Card -------------------------------------------------------------------

struct card_state {
	u16 first;  // its 9 tiles: the 3x3 classes (corners, edges, middle)
	u8 tiles;
};

static const struct ui_card_config *card_config(u8 node)
{
	static const struct ui_card_config none = {0, 0, 0, 0, 0};
	const struct ui_card_config *c = ui_node_spec(node)->data;
	return c ? c : &none;
}

static int card_measure(u8 node, struct engine_budget *need)
{
	const struct ui_card_config *c = card_config(node);
	const struct ui_node *n = ui_node(node);
	const struct ui_look *l = look();
	u32 r = l->card_radius;
	if (n->w < 16 || n->h < 16 || c->inset_l + r > 8 || c->inset_r + r > 8 || c->inset_t + r > 8 ||
	    c->inset_b + r > 8 || c->inset_b < l->shadow_offset ||
	    (c->inset_l | c->inset_t | c->inset_r | c->inset_b) > 7) {
		ui_set_error("view %s: a card is 2 tiles or more a side, its insets (under 8 px) and corners within "
			     "a tile, the shadow inside",
			     ui_node_view(node)->spec->name);
		return 0;
	}
	need->bg_tiles = (u16)(need->bg_tiles + 9);
	return 1;
}

// A card's body on a 24x24 px grid, a bit a pixel: row y + 1 for y from -1
// (for the edge test above it) to 24, rounded at the corners
static void card_rows(u32 *rows, const struct ui_card_config *c, u32 radius)
{
	s32 x0 = c->inset_l, y0 = c->inset_t, x1 = 24 - c->inset_r, y1 = 24 - c->inset_b;
	for (s32 y = -1; y < 25; y++) {
		u32 m = 0;
		if (y >= y0 && y < y1) {
			u32 dy = (u32)(y - y0 < y1 - 1 - y ? y - y0 : y1 - 1 - y);  // from the nearest side
			s32 in = (s32)ui_round_inset(radius, dy);
			m = ((1u << (x1 - in)) - 1) & ~((1u << (x0 + in)) - 1);
		}
		rows[y + 1] = m;
	}
}

// The 9 tiles of a card's 3x3 classes, from a card drawn on 24x24 px: its
// corners in the corner tiles, its edges in the middle ones (no corner
// reaches them: card_measure), its paper in the middle. Rows are bit masks:
// the launch's loading card draws this at the BIOS's wait states
static void make_card(u32 *tiles, const struct ui_card_config *c, u32 paper)
{
	const struct ui_look *l = look();
	u32 sh = l->shadow_offset;
	u32 rows[26];
	card_rows(rows, c, l->card_radius);
	for (u32 y = 0; y < 24; y++) {
		u32 body = rows[y + 1];
		u32 edge = 0;
		if (c->flags & UI_CARD_EDGE)
			edge = body & ~(body << 1 & body >> 1 & rows[y] & rows[y + 2]);
		u32 shadow = sh && y + 1 >= sh ? rows[y + 1 - sh] & ~body : 0;
		for (u32 tx = 0; tx < 3; tx++) {
			u32 row = 0;
			for (u32 px = 0; px < 8; px++) {
				u32 bit = 1u << (tx * 8 + px);
				u32 role = (edge & bit)     ? UI_ROLE_BORDER
					   : (body & bit)   ? paper
					   : (shadow & bit) ? UI_ROLE_SHADOW
							    : 0;
				row |= role << (4 * px);
			}
			tiles[((y / 8) * 3 + tx) * 8 + y % 8] = row;
		}
	}
}

static u16 card_tag(const struct ui_card_config *c, u32 paper)
{
	u32 v = ((u32)(c->inset_l & 7) * 8 + (c->inset_t & 7)) * 8 + (c->inset_r & 7);
	v = ((v * 8 + (c->inset_b & 7)) * 2 + ((c->flags & UI_CARD_EDGE) != 0)) * 2 + (paper == UI_ROLE_FLOAT);
	return (u16)(TAG_CARD + v);  // under 0x2000 + TAG_CARD
}

static int card_init(u8 node)
{
	struct card_state *st = ui_node_state(node);
	const struct ui_card_config *c = card_config(node);
	u32 paper = ui_paper(node, UI_ROLE_PANEL);
	st->tiles = ui_tiles(node, 9, card_tag(c, paper), &st->first);
	if (st->tiles == RES_NONE)
		return 0;
	// Nothing shows a new block yet: drawn straight into VRAM
	if (res_fresh(RES_BG_VRAM))
		make_card(tile_addr(node, st->first), c, paper);
	return 1;
}

static void card_draw(u8 node)
{
	struct card_state *st = ui_node_state(node);
	struct ui_node *n = ui_node(node);
	if (n->flags & UI_N_HIDDEN) {
		if (!map_hide(node))
			ui_dirty(node, 0xFF);
		return;
	}
	u32 tx = (u32)n->x / 8, ty = (u32)n->y / 8, w = (u32)n->w / 8, h = (u32)n->h / 8;
	u16 *rows = ui_map_rows(node, ty, h);
	if (!rows) {
		ui_dirty(node, 0xFF);
		return;
	}
	for (u32 y = 0; y < h; y++) {
		u32 cy = y == 0 ? 0 : y == h - 1 ? 2 : 1;
		for (u32 x = 0; x < w; x++) {
			u32 cx = x == 0 ? 0 : x == w - 1 ? 2 : 1;
			rows[y * 32 + tx + x] = ui_entry(st->first + cy * 3 + cx, UI_BANK_NORMAL);
		}
	}
}

const struct ui_ops ui_card_ops = {sizeof(struct card_state), card_measure, card_init, 0, 0, card_draw};

// --- Title bar ----------------------------------------------------------------

enum {
	TITLE_HEADING = 1,
	TITLE_RIGHT = 2,
};

// A heading (and its after-text) too long for its room scrolls, as a list's
// cut name does (ui_theme's marquee_*): after marquee_delay frames it moves
// left to its end, rests marquee_pause frames, and starts over from its place
enum {
	TQ_WAIT,  // resting at its place
	TQ_RUN,
	TQ_END,
	TQ_OFF,  // not cut: still until the text changes
};

struct title_state {
	struct paint heading, right;
	const char *text, *right_text, *sub;
	u16 icon_tile;  // ART_NONE: no icon
	u8 icon_bank;
	s16 shift;  // px the heading is drawn right of its place (ui_title_offset)
	u32 seen;   // the heading's and after-text's hash: the marquee starts over when it changes
	u16 mq_px, mq_wait;
	u8 mq, mq_acc;
};

static const struct ui_title_config *title_config(u8 node)
{
	static const struct ui_title_config none = {"", 0, UI_TITLE_NO_ICON};
	const struct ui_title_config *c = ui_node_spec(node)->data;
	return c ? c : &none;
}

// The heading's tile columns and the right-hand text's: the heading's from
// the left edge, so it can slide in from there (an open's zoom)
static void title_columns(u8 node, u32 *hx, u32 *hw, u32 *rx, u32 *rw)
{
	u32 w = (u32)ui_node(node)->w / 8, right = (look()->title_right_w + 7u) / 8;
	*rw = right < w ? right : w;
	*rx = w - *rw;
	*hx = 0;
	*hw = *rx;
}

static int title_measure(u8 node, struct engine_budget *need)
{
	u32 hx, hw, rx, rw;
	title_columns(node, &hx, &hw, &rx, &rw);
	if (title_config(node)->icon != UI_TITLE_NO_ICON) {
		need->obj_tiles = (u16)(need->obj_tiles + ART_ICON16_TILES);
		need->obj_banks++;
	}
	return measure_tiles(node, need, 2 * (hw + rw));
}

static void title_seen(struct title_state *st);

static int title_init(u8 node)
{
	struct title_state *st = ui_node_state(node);
	const struct ui_title_config *c = title_config(node);
	u32 hx, hw, rx, rw;
	title_columns(node, &hx, &hw, &rx, &rw);
	if ((hw && !paint_init(node, &st->heading, hx, hw, 2)) || !paint_init(node, &st->right, rx, rw, 2))
		return 0;
	st->text = c->text ? c->text : "";
	st->right_text = c->right;
	st->sub = 0;
	st->seen = 0;
	title_seen(st);
	st->icon_tile = ART_NONE;
	if (c->icon != UI_TITLE_NO_ICON) {
		st->icon_tile = art_load_icon16(c->icon, &st->icon_bank);
		if (st->icon_tile == ART_NONE) {
			ui_set_error("view %s: no room for the title's icon", ui_node_view(node)->spec->name);
			return 0;
		}
		ui_view_uses_objs(ui_node_view(node), 0);
	}
	return 1;
}

// The heading's and its after-text's width, as drawn
static u32 title_width(const struct title_state *st)
{
	const struct ui_theme *t = ui_theme();
	u32 w = text_width(t->heading, st->text, strlen(st->text));
	if (st->sub && *st->sub)
		w += t->look.title_sub_gap + text_width(t->body, st->sub, strlen(st->sub));
	return w;
}

// The marquee's step: frames counted down, then px moved at the theme's speed
static void title_marquee(u8 node, struct title_state *st, u32 dt)
{
	const struct ui_theme *t = ui_theme();
	u32 room = st->heading.tw * 8u > t->look.title_text_x ? st->heading.tw * 8u - t->look.title_text_x : 0;
	if (st->mq == TQ_OFF || st->shift || ui_node_view(node) != ui_top() || !room)
		return;  // not cut, sliding in, or under a popup: held where it is
	if (st->mq_wait > dt) {
		st->mq_wait = (u16)(st->mq_wait - dt);
		ui_wake();
		return;
	}
	st->mq_wait = 0;
	u32 width = title_width(st);
	if (st->mq == TQ_WAIT) {
		if (width <= room) {
			st->mq = TQ_OFF;
			return;
		}
		st->mq = TQ_RUN;
		st->mq_px = 0;
		st->mq_acc = 0;
	} else if (st->mq == TQ_RUN) {
		u32 speed = t->marquee_speed > 60 ? 60 : t->marquee_speed ? t->marquee_speed : 1;
		u32 acc = st->mq_acc + speed * dt;
		if (acc >= 60) {
			st->mq_px = (u16)(st->mq_px + acc / 60);
			ui_dirty(node, TITLE_HEADING);
		}
		st->mq_acc = (u8)(acc % 60);
		if (st->mq_px + room >= width + t->marquee_gap) {
			st->mq = TQ_END;
			st->mq_wait = t->marquee_pause;
		}
	} else {
		st->mq = TQ_WAIT;  // back at its place, to wait again
		st->mq_px = 0;
		st->mq_wait = t->marquee_delay;
		ui_dirty(node, TITLE_HEADING);
	}
	ui_wake();
}

// A changed heading or after-text: the marquee starts over from its place
static void title_seen(struct title_state *st)
{
	u32 h = 5381;
	for (const char *p = st->text; *p; p++)
		h = h * 33 ^ (u8)*p;
	for (const char *p = st->sub ? st->sub : ""; *p; p++)
		h = h * 33 ^ (u8)*p;
	h ^= st->sub ? 1 : 0;
	if (h == st->seen)
		return;
	st->seen = h;
	st->mq = TQ_WAIT;
	st->mq_px = 0;
	st->mq_wait = ui_theme()->marquee_delay;
}

static void title_update(u8 node, u32 dt)
{
	struct title_state *st = ui_node_state(node);
	const struct ui_node *n = ui_node(node);
	const struct ui_view *v = ui_node_view(node);
	s32 shift, dy;
	ui_title_offset(v, &shift, &dy);
	if (shift != st->shift) {
		st->shift = (s16)shift;  // the heading drawn there, this frame
		ui_dirty(node, TITLE_HEADING);
	}
	if (st->heading.tw && !(n->flags & UI_N_HIDDEN))
		title_marquee(node, st, dt);
	if (st->icon_tile == ART_NONE || (n->flags & UI_N_HIDDEN) || !ui_node_shown(node) || !ui_plane_owned(node))
		return;
	s32 x = n->x + ui_view_dx(v) + shift + look()->title_icon_x, y = n->y + dy + look()->title_icon_y;
	if (y <= -16 || y >= 160 || x <= -16 || x >= 240)
		return;  // off screen (attr0's y would wrap to the top)
	ui_obj_add(node, (u16)(y & 0xFF), (u16)(OBJ_SIZE_16 | (x & 0x1FF)), (u16)(st->icon_tile | st->icon_bank << 12));
}

// One line of text at (x, y), cut at the window's right edge, and at its
// left one where x is left of it (whole glyphs left out: the blitter has no
// left clip); the pen after the text
static s32 slid_text(const struct text_window *win, const struct torii_font *font, s32 x, u32 y, u32 ink, const char *s)
{
	s32 ww = win->width * 8;
	if (!s || !*s || x >= ww)
		return x;
	if (x >= 0)
		return x + (s32)draw_text(win, font, (u32)x, y, (u32)(ww - x), ink, s);
	struct text_line line;
	u32 lead, len = strlen(s);
	text_layout_window(font, s, len, (u32)-x, (u32)ww, &line, &lead);
	if (line.count)
		text_draw_line(win, font, lead, y, ink, &line);
	return x + (s32)text_width(font, s, len);
}

static void title_draw(u8 node)
{
	struct title_state *st = ui_node_state(node);
	struct ui_node *n = ui_node(node);
	if (n->flags & UI_N_HIDDEN) {
		if (!paint_hide(node, &st->heading, &st->right))
			ui_dirty(node, 0xFF);
		return;
	}
	const struct ui_theme *t = ui_theme();
	const struct ui_look *l = &t->look;
	u32 paper = ui_paper(node, UI_ROLE_NONE), h = (u32)n->h;
	u8 todo = st->right.mapped ? n->dirty : (u8)(TITLE_HEADING | TITLE_RIGHT);
	n->dirty = 0;  // what fails sets its bit again
	struct text_window win;
	u32 set;
	if ((todo & TITLE_HEADING) && st->heading.tw) {
		// Its window starts at the left edge; the heading at title_text_x, or
		// shifted left of it (cut at the edge) while it slides in
		if (!paint_begin(node, &st->heading, &win, &set)) {
			ui_dirty(node, todo);  // the last flip hasn't landed: both next frame
			return;
		}
		s32 x = (s32)l->title_text_x - st->heading.tx * 8 + st->shift - st->mq_px, ww = st->heading.tw * 8;
		text_clear(&win, 0, 0, (u32)ww, h, paper);
		if (l->title_text_y + t->heading->line_height <= h && x < ww) {
			x = slid_text(&win, t->heading, x, l->title_text_y, ui_ink(node, UI_ROLE_TEXT), st->text);
			// The after-text, faint in the body font on the heading's baseline
			const struct torii_font *b = t->body;
			s32 sx = x + l->title_sub_gap, sy = l->title_text_y + t->heading->baseline - b->baseline;
			if (st->sub && sx < ww && (u32)sy + b->line_height <= h)
				slid_text(&win, b, sx, (u32)sy, UI_ROLE_FAINT, st->sub);
			if (st->mq_px)  // scrolled: cut at its place, under the icon's sprite
				text_clear(&win, 0, 0, l->title_text_x, h, paper);
		}
		if (!paint_end(node, &st->heading, set, UI_BANK_NORMAL)) {
			ui_dirty(node, todo);  // the flip next frame, with the right-hand text's
			return;
		}
	}
	if (todo & TITLE_RIGHT) {
		if (!paint_begin(node, &st->right, &win, &set)) {
			ui_dirty(node, TITLE_RIGHT);
			return;
		}
		u32 x0 = st->right.tx * 8u, ww = st->right.tw * 8u, end = l->title_right > x0 ? l->title_right - x0 : 0;
		text_clear(&win, 0, 0, ww, h, paper);
		const struct torii_font *font = t->body;
		if (st->right_text && end && l->title_right_y + font->line_height <= h) {
			struct text_line line;
			text_layout(font, st->right_text, strlen(st->right_text), end, &line);
			text_draw_line(&win, font, end - line.extent, l->title_right_y, UI_ROLE_DIM, &line);
		}
		if (!paint_end(node, &st->right, set, UI_BANK_NORMAL))
			ui_dirty(node, TITLE_RIGHT);
	}
}

void ui_title_set(u8 node, const char *text)
{
	struct title_state *st = ui_node_state(node);
	st->text = text ? text : "";
	title_seen(st);
	ui_dirty(node, TITLE_HEADING);
}

void ui_title_set_sub(u8 node, const char *text)
{
	struct title_state *st = ui_node_state(node);
	st->sub = text;
	title_seen(st);
	ui_dirty(node, TITLE_HEADING);
}

void ui_title_set_right(u8 node, const char *text)
{
	struct title_state *st = ui_node_state(node);
	st->right_text = text;
	ui_dirty(node, TITLE_RIGHT);
}

int ui_title_icon_set(u8 node, u32 icon)
{
	struct title_state *st = ui_node_state(node);
	if (st->icon_tile == ART_NONE || icon >= ART_ICON16_COUNT)
		return 0;  // a title laid out without an icon has no room for one
	u8 bank;
	u16 tile = art_load_icon16(icon, &bank);
	if (tile == ART_NONE)
		return 0;
	st->icon_tile = tile;
	st->icon_bank = bank;
	return 1;
}

const struct ui_ops ui_title_ops = {sizeof(struct title_state), title_measure, title_init, 0, title_update, title_draw};

// --- Hint bar ------------------------------------------------------------------

struct hints_state {
	struct paint paint;
	const struct ui_hint *hints;
	const char *text;  // in place of the chips, NULL none
	u8 count;
	struct ui_hint_note note;  // the text's look
};

static int hints_measure(u8 node, struct engine_budget *need)
{
	return measure_tiles(node, need, 2u * (u32)(ui_node(node)->w / 8));
}

static int hints_init(u8 node)
{
	struct hints_state *st = ui_node_state(node);
	const struct ui_hints_config *c = ui_node_spec(node)->data;
	if (c) {
		st->hints = c->hints;
		st->count = c->count;
	}
	return paint_init(node, &st->paint, 0, (u32)ui_node(node)->w / 8, 2);
}

static void hints_draw(u8 node)
{
	struct hints_state *st = ui_node_state(node);
	struct ui_node *n = ui_node(node);
	if (n->flags & UI_N_HIDDEN) {
		if (!paint_hide(node, &st->paint, 0))
			ui_dirty(node, 0xFF);
		return;
	}
	struct text_window win;
	u32 set;
	if (!paint_begin(node, &st->paint, &win, &set)) {
		ui_dirty(node, 0xFF);
		return;
	}
	const struct ui_look *l = look();
	// The chips at the look's hint_y in the page's bar; in a bar elsewhere
	// (a dialog's), centred in its height
	u32 w = (u32)n->w, h = (u32)n->h, margin = l->margin;
	u32 top = l->hint_y >= n->y && l->hint_y + l->hint_chip_h <= n->y + h ? l->hint_y - (u32)n->y
									      : (h - l->hint_chip_h) / 2;
	text_clear(&win, 0, 0, w, (u32)n->h, ui_paper(node, UI_ROLE_NONE));
	if (top + l->hint_chip_h <= (u32)n->h && w > 2 * margin) {
		u32 count = st->count < l->hint_max ? st->count : l->hint_max, chips = 0;
		if (st->text && st->note.beside && count) {
			// The keys at the right as ever, the line left of them
			for (u32 i = 0; i < count; i++)
				chips += ui_key_chip(&win, 0, top, st->hints[i].key, st->hints[i].label, 0) +
					 l->hint_gap;
			u32 x = chips + margin < w ? w - margin - chips + l->hint_gap : margin;
			for (u32 i = 0; i < count; i++)
				x += ui_key_chip(&win, x, top, st->hints[i].key, st->hints[i].label, 1) + l->hint_gap;
		}
		if (st->text) {
			// The line centred (left of the keys beside it), after its bar if
			// it has one
			const struct torii_font *font = ui_theme()->body;
			const struct ui_hint_note *nt = &st->note;
			struct text_line line;
			u32 room = chips + 2 * margin < w ? w - 2 * margin - chips : 8u;
			u32 bar = nt->bar_w ? nt->bar_w + nt->bar_gap : 0u;
			text_layout(font, st->text, strlen(st->text), room > bar + 8 ? room - bar : 8, &line);
			u32 total = bar + line.extent;
			u32 x = margin + (total < room && !chips ? (room - total) / 2 : 0);
			if (nt->bar_w) {
				u32 bh = nt->bar_h < l->hint_chip_h ? nt->bar_h : l->hint_chip_h;
				u32 by = top + (l->hint_chip_h - bh) / 2;
				round_bar(&win, x, by, nt->bar_w, bh, nt->bar_r, UI_ROLE_TRACK, 0, w);
				if (nt->fill)
					round_bar(&win, x, by, nt->bar_w, bh, nt->bar_r, UI_ROLE_ACCENT, 0,
						  x + (nt->fill < nt->bar_w ? nt->fill : nt->bar_w));
				x += bar;
			}
			text_draw_line(&win, font, x, text_top(font, top, l->hint_chip_h),
				       nt->ink ? nt->ink : UI_ROLE_DIM, &line);
		} else {
			// From the right edge in: the hints' widths first
			u32 total = 0;
			for (u32 i = 0; i < count; i++)
				total += ui_key_chip(&win, 0, top, st->hints[i].key, st->hints[i].label, 0) +
					 (i ? l->hint_gap : 0u);
			u32 x = total + margin < w ? w - margin - total : margin;
			for (u32 i = 0; i < count; i++)
				x += ui_key_chip(&win, x, top, st->hints[i].key, st->hints[i].label, 1) + l->hint_gap;
		}
	}
	if (!paint_end(node, &st->paint, set, UI_BANK_NORMAL))
		ui_dirty(node, 0xFF);
}

u32 ui_hints_width(const struct ui_hint *hints, u32 count, const char *note)
{
	const struct ui_look *l = look();
	static const struct text_window none = {0, 32, 2};  // measured only: never drawn into
	u32 w = 2u * l->margin;
	count = count < l->hint_max ? count : l->hint_max;
	for (u32 i = 0; i < count; i++)
		w += ui_key_chip(&none, 0, 0, hints[i].key, hints[i].label, 0) + l->hint_gap;
	if (note && *note)
		w += text_width(ui_theme()->body, note, strlen(note));
	else if (count)
		w -= l->hint_gap;
	return w;
}

void ui_hints_set(u8 node, const struct ui_hint *hints, u32 count)
{
	struct hints_state *st = ui_node_state(node);
	st->hints = hints;
	st->count = (u8)(hints ? count : 0);
	st->text = 0;
	ui_dirty(node, 0xFF);
}

void ui_hints_text(u8 node, const char *text)
{
	ui_hints_note(node, text, 0);
}

void ui_hints_note(u8 node, const char *text, const struct ui_hint_note *note)
{
	struct hints_state *st = ui_node_state(node);
	st->text = text;
	if (note)
		st->note = *note;
	else
		memset(&st->note, 0, sizeof(st->note));
	ui_dirty(node, 0xFF);
}

void ui_hints_get(u8 node, const struct ui_hint **hints, u32 *count, const char **text, struct ui_hint_note *note)
{
	const struct hints_state *st = ui_node_state(node);
	*hints = st->hints;
	*count = st->count;
	*text = st->text;
	*note = st->note;
}

const struct ui_ops ui_hints_ops = {sizeof(struct hints_state), hints_measure, hints_init, 0, 0, hints_draw};

// --- Path pills -------------------------------------------------------------------

struct path_state {
	struct paint paint;
	const char *path;
};

static int path_measure(u8 node, struct engine_budget *need)
{
	return measure_tiles(node, need, 2u * (u32)(ui_node(node)->w / 8));
}

static int path_init(u8 node)
{
	struct path_state *st = ui_node_state(node);
	st->path = "";
	return paint_init(node, &st->paint, 0, (u32)ui_node(node)->w / 8, 2);
}

// A path segment's pill: its text width plus the chip's padding
static u32 seg_width(const struct torii_font *font, const char *s, u32 len)
{
	return text_width(font, s, len) + 2u * look()->hint_chip_pad;
}

static void path_draw(u8 node)
{
	struct path_state *st = ui_node_state(node);
	struct ui_node *n = ui_node(node);
	if (n->flags & UI_N_HIDDEN) {
		if (!paint_hide(node, &st->paint, 0))
			ui_dirty(node, 0xFF);
		return;
	}
	struct text_window win;
	u32 set;
	if (!paint_begin(node, &st->paint, &win, &set)) {
		ui_dirty(node, 0xFF);
		return;
	}
	const struct ui_look *l = look();
	const struct torii_font *font = ui_theme()->body;
	u32 w = (u32)n->w, h = (u32)n->h, ch = l->hint_chip_h < h ? l->hint_chip_h : h, top = (h - ch) / 2;
	u32 y = text_top(font, top, ch), pad = ui_theme()->pad, sep = text_width(font, "/", 1);
	u32 step = sep + 2u * l->path_gap;  // between pills
	text_clear(&win, 0, 0, w, h, ui_paper(node, UI_ROLE_PANEL));
	// The last SEGS segments, their starts and lengths (a ring: deeper
	// paths keep their current folder; the ones before give way to "...")
	enum {
		SEGS = 16
	};
	const char *ring[SEGS];
	u32 ring_len[SEGS], total_segs = 0;
	for (const char *s = st->path; *s;) {
		while (*s == '/')
			s++;
		if (!*s)
			break;
		const char *e = s;
		while (*e && *e != '/')
			e++;
		ring[total_segs % SEGS] = s;
		ring_len[total_segs % SEGS] = (u32)(e - s);
		total_segs++;
		s = e;
	}
	u32 count = total_segs < SEGS ? total_segs : SEGS, skipped = total_segs - count;
	const char *seg[SEGS];
	u32 len[SEGS];
	for (u32 i = 0; i < count; i++) {
		seg[i] = ring[(skipped + i) % SEGS];
		len[i] = ring_len[(skipped + i) % SEGS];
	}
	// Leading folders give way to "..." until the rest fits
	u32 first = 0, ell = text_width(font, "...", 3);
	for (;;) {
		u32 total = first || skipped ? ell + step : 0;
		for (u32 i = first; i < count; i++)
			total += seg_width(font, seg[i], len[i]) + (i + 1 < count ? step : 0u);
		if (total + 2 * pad <= w || first + 1 >= count)
			break;
		first++;
	}
	u32 x = pad;
	if (first || skipped) {
		draw_text(&win, font, x, y, ell + 1, UI_ROLE_FAINT, "...");
		x += ell + l->path_gap;
		draw_text(&win, font, x, y, sep + 1, UI_ROLE_FAINT, "/");
		x += sep + l->path_gap;
	}
	for (u32 i = first; i < count && x < w; i++) {
		int current = i + 1 == count;
		u32 sw = seg_width(font, seg[i], len[i]);
		if (x + sw > w - pad)
			sw = w - pad > x ? w - pad - x : 0;
		if (sw > 2u * l->hint_chip_pad) {
			round_bar(&win, x, top, sw, ch, l->hint_chip_radius, current ? UI_ROLE_TINT : UI_ROLE_BG, 0, w);
			struct text_line line;
			text_layout(font, seg[i], len[i], sw - 2u * l->hint_chip_pad, &line);
			text_draw_line(&win, font, x + l->hint_chip_pad, y, current ? UI_ROLE_TEXT : UI_ROLE_FAINT,
				       &line);
		}
		x += sw;
		if (!current && x + step < w) {
			draw_text(&win, font, x + l->path_gap, y, sep + 1, UI_ROLE_FAINT, "/");
			x += step;
		}
	}
	if (!paint_end(node, &st->paint, set, UI_BANK_NORMAL))
		ui_dirty(node, 0xFF);
}

void ui_path_set(u8 node, const char *path)
{
	struct path_state *st = ui_node_state(node);
	st->path = path ? path : "";
	ui_dirty(node, 0xFF);
}

const struct ui_ops ui_path_ops = {sizeof(struct path_state), path_measure, path_init, 0, 0, path_draw};

// --- Storage bar ----------------------------------------------------------------

struct storage_state {
	struct paint paint;
	const u32 *parts;
	u32 total;
	u8 count;
};

static int storage_measure(u8 node, struct engine_budget *need)
{
	return measure_tiles(node, need, 2u * (u32)(ui_node(node)->w / 8));
}

static int storage_init(u8 node)
{
	struct storage_state *st = ui_node_state(node);
	return paint_init(node, &st->paint, 0, (u32)ui_node(node)->w / 8, 2);
}

static void storage_draw(u8 node)
{
	struct storage_state *st = ui_node_state(node);
	struct ui_node *n = ui_node(node);
	if (n->flags & UI_N_HIDDEN) {
		if (!paint_hide(node, &st->paint, 0))
			ui_dirty(node, 0xFF);
		return;
	}
	struct text_window win;
	u32 set;
	if (!paint_begin(node, &st->paint, &win, &set)) {
		ui_dirty(node, 0xFF);
		return;
	}
	const struct ui_look *l = look();
	u32 w = (u32)n->w, h = (u32)n->h, bh = l->storage_h < h ? l->storage_h : h, top = (h - bh) / 2, r = bh / 2;
	text_clear(&win, 0, 0, w, h, ui_paper(node, UI_ROLE_PANEL));
	round_bar(&win, 0, top, w, bh, r, UI_ROLE_TRACK, 0, w);
	// Each part from where the last ended, clipped to the bar's rounded ends
	static const u8 shades[3] = {UI_ROLE_NOR_FILL, UI_ROLE_NOR_FILL_MID, UI_ROLE_NOR_FILL_LIGHT};
	unsigned long long sum = 0;
	u32 x = 0;
	for (u32 i = 0; i < st->count && st->total; i++) {
		sum += st->parts[i];
		u32 end = sum >= st->total ? w : (u32)(sum * w / st->total), shown = end;
		// A part under a pixel still shows one (a 128 KB game of 64 MB); the
		// next part starts where this one really ends, so they don't add up
		if (st->parts[i] && shown <= x && x < w)
			shown = x + 1;
		if (shown > x)
			round_bar(&win, 0, top, w, bh, r, shades[i % 3], x, shown);
		x = end;
	}
	if (!paint_end(node, &st->paint, set, UI_BANK_ALERT))
		ui_dirty(node, 0xFF);
}

void ui_storage_set(u8 node, const u32 *parts, u32 count, u32 total)
{
	struct storage_state *st = ui_node_state(node);
	st->parts = parts;
	st->count = (u8)(parts ? count : 0);
	st->total = total;
	ui_dirty(node, 0xFF);
}

const struct ui_ops ui_storage_ops = {sizeof(struct storage_state), storage_measure, storage_init, 0, 0, storage_draw};

// --- Canvas ----------------------------------------------------------------------

struct canvas_state {
	struct paint paint;
	u8 painted;  // bit s: set s has been painted
};

static int canvas_measure(u8 node, struct engine_budget *need)
{
	const struct ui_canvas_config *c = ui_node_spec(node)->data;
	if (!c || !c->paint) {
		ui_set_error("view %s: a canvas needs its paint", ui_node_view(node)->spec->name);
		return 0;
	}
	return measure_tiles(node, need, 2u * (u32)(ui_node(node)->w / 8));
}

static int canvas_init(u8 node)
{
	struct canvas_state *st = ui_node_state(node);
	st->painted = 0;
	return paint_init(node, &st->paint, 0, (u32)ui_node(node)->w / 8, 2);
}

static void canvas_draw(u8 node)
{
	struct canvas_state *st = ui_node_state(node);
	struct ui_node *n = ui_node(node);
	if (n->flags & UI_N_HIDDEN) {
		if (!paint_hide(node, &st->paint, 0))
			ui_dirty(node, 0xFF);
		return;
	}
	struct text_window win;
	u32 set;
	if (!paint_begin(node, &st->paint, &win, &set)) {
		ui_dirty(node, 0xFF);
		return;
	}
	const struct ui_canvas_config *c = ui_node_spec(node)->data;
	int fresh = !(st->painted >> set & 1);
	// A whole paint that would pass the frame's budget waits for the next
	// frame (a first draw then takes two, as a list's rows may)
	if (fresh && !ui_draw_room(c->cycles, UI_DRAW_BUDGET)) {
		ui_dirty(node, 0xFF);
		return;
	}
	if (fresh)
		text_clear(&win, 0, 0, (u32)n->w, (u32)n->h, ui_paper(node, UI_ROLE_NONE));
	c->paint(ui_node_view(node)->ctx, node, &win, set, fresh);
	st->painted |= (u8)(1u << set);
	if (!paint_end(node, &st->paint, set, UI_BANK_NORMAL))
		ui_dirty(node, 0xFF);
}

void ui_canvas_redraw(u8 node)
{
	ui_dirty(node, 0xFF);
}

const struct ui_ops ui_canvas_ops = {sizeof(struct canvas_state), canvas_measure, canvas_init, 0, 0, canvas_draw};
