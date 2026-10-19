// Lists and grids (ui.h): rows drawn from a ui_list_model into a ring of
// row slots on a BG of their own, scrolled pixel by pixel by a spring
// (ui/ring.h has the maths).
//
// - Each row slot is a text window of the row's width x its height in
//   tiles (a 30x2 window for a full-width 16 px row). A row entering the
//   viewport is drawn into a slot that isn't on screen, and its map rows
//   are written straight away: they aren't on screen either.
// - The selection is a palette bank switch in the selected row's (or
//   cell's) map entries, through the staging area, so it shows in one
//   VBlank: text is never redrawn for it.
// - A jump too far to scroll a row a frame (a new folder, a wrap from the
//   end to the start) is a page change: the plane is hidden at the next
//   commit, every row is drawn the frame after, and the plane shows again
//   in the VBlank after that.
// - With UI_LIST_MARQUEE, a cut name on the selected row scrolls after the
//   cursor rests: two extra slots take turns, flipped by map in VBlank.
#include <string.h>

#include "ring.h"
#include "ui_priv.h"
#include "../engine/frame.h"
#include "../engine/gfx.h"
#include "../engine/res.h"
#include "../engine/text.h"

enum {
	PAGE_NONE,
	PAGE_HIDING,  // waiting for the plane to be off screen
	PAGE_DRAW,    // rows assigned: draw them all, then show the plane
};

enum {
	MQ_OFF,
	MQ_WAIT,    // the cursor rests; scrolls after mq_wait frames
	MQ_RUN,     // scrolling
	MQ_END,     // at the end, pausing before it starts over
};

#define MQ_STEP_FRAMES 2  // a pixel every two frames: 30 px a second
#define MQ_END_FRAMES 60
#define MQ_GAP 8          // px scrolled past the end before the pause

struct list_state {
	struct ui_ring ring;
	u8 cols;    // cells per row: 1 for a list
	u8 tw, rt;  // a row's width and height in tiles
	u8 tiles;   // res handle of the slots
	u16 first;  // first slot's tile number
	u8 page;
	u8 sel_ok;  // the map shows the selection at sel_drawn
	u32 count, rows;
	u32 cursor;
	u32 sel_drawn;  // the item whose map entries are in the selected bank
	s32 shown;      // content px at the viewport's top, as the next commit shows it
	s32 base;       // the spring's base, px
	s32 spring;     // the spring's value (16.16, from base), written by motion
	s32 page_target;
	u32 page_cursor;  // the cursor a page change's rows are drawn for
	MotionId motion;
	u16 slot_row[UI_RING_MAX_SLOTS];
	u32 dirty;   // slots to draw: the window's dirty-row mask
	u32 fresh;   // slots given a new row: their map rows need writing
	u32 lo, hi;  // resident rows
	u32 plo, phi;
	InputWrap wrap;
	// Marquee
	u8 mq;
	u8 mq_set;    // the marquee slot the map shows (0 or 1), 0xFF neither
	u8 mq_step;   // draw the next step
	u8 pad;
	u16 mq_wait;
	u16 mq_px;
	u16 mq_width;
};

static const struct ui_list_config *config(u8 node)
{
	return ui_node_spec(node)->data;
}

static int marquee(u8 node)
{
	const struct list_state *st = ui_node_state(node);
	return st->cols == 1 && (config(node)->flags & UI_LIST_MARQUEE);
}

static u32 slot_count(u8 node)
{
	const struct list_state *st = ui_node_state(node);
	return st->ring.k + (marquee(node) ? 2u : 0u);
}

// Geometry from the config and rectangle; 0 with the error recorded
static int geometry(u8 node, struct list_state *st)
{
	const struct ui_list_config *c = config(node);
	const struct ui_node *n = ui_node(node);
	const char *name = ui_node_view(node)->spec->name, *why;
	if (!c || !c->model || !c->model->count || !c->model->fetch) {
		ui_set_error("view %s: a list needs a model with count and fetch", name);
		return 0;
	}
	u32 rh = c->row_h ? c->row_h : ui_theme()->row_h;
	if (!ring_setup(&st->ring, (u32)n->h, rh, &why)) {
		ui_set_error("view %s: list: %s", name, why);
		return 0;
	}
	st->rt = (u8)(rh / 8);
	if ((c->flags & UI_LIST_FIXED) && !c->cell_w)
		st->ring.k = (u8)(n->h / rh);  // a slot per whole row: model_count keeps to them
	if (c->cell_w) {
		if (c->cell_w % 8 || c->cell_w > n->w) {
			ui_set_error("view %s: a grid's cells are whole tiles, no wider than it", name);
			return 0;
		}
		st->cols = (u8)(n->w / c->cell_w);
		st->tw = (u8)(st->cols * c->cell_w / 8);
	} else {
		st->cols = 1;
		st->tw = (u8)(n->w / 8);
	}
	return 1;
}

static int list_measure(u8 node, struct engine_budget *need)
{
	struct list_state *st = ui_node_state(node);
	if (!geometry(node, st))
		return 0;
	need->bg_tiles = (u16)(need->bg_tiles + slot_count(node) * st->tw * st->rt);
	return 1;
}

static u32 *slot_tiles(const struct list_state *st, u32 plane, u32 slot)
{
	u32 tile = st->first + slot * st->tw * st->rt;
	return (u32 *)(0x06000000 + ui_plane_cb(plane) * 0x4000 + tile * 32u);
}

static u32 model_count(u8 node)
{
	u32 n = config(node)->model->count(ui_node_view(node)->ctx);
	if ((config(node)->flags & UI_LIST_FIXED) && config(node)->cell_w == 0) {
		const struct list_state *st = ui_node_state(node);
		if (n > st->ring.k)
			n = st->ring.k;
	}
	return n > UI_LIST_MAX ? UI_LIST_MAX : n;
}

// Gives the resident rows at the shown scroll their slots
static void assign(u8 node, struct list_state *st, int dir)
{
	u32 lo, hi;
	ring_resident(&st->ring, st->shown, dir, st->rows, &lo, &hi);
	for (u32 r = lo; r <= hi && lo <= hi; r++) {
		u32 s = ring_slot(&st->ring, r);
		if (st->slot_row[s] != r) {
			st->slot_row[s] = (u16)r;
			st->dirty |= 1u << s;
			st->fresh |= 1u << s;
		}
	}
	if (lo != st->lo || hi != st->hi) {
		st->lo = lo;
		st->hi = hi;
		u32 plo, phi;
		ring_prefetch(&st->ring, lo, hi, st->rows, &plo, &phi);
		const struct ui_list_model *m = config(node)->model;
		if (lo <= hi && m->prefetch && (plo != st->plo || phi != st->phi)) {
			st->plo = plo;
			st->phi = phi;
			u32 last = phi * st->cols + st->cols - 1;
			m->prefetch(ui_node_view(node)->ctx, plo * st->cols, last < st->count ? last : st->count - 1);
		}
	}
}

static void forget_slots(struct list_state *st)
{
	for (u32 s = 0; s < UI_RING_MAX_SLOTS; s++)
		st->slot_row[s] = UI_ITEM_NONE;
	st->lo = 1;
	st->hi = 0;
	st->plo = st->phi = 0xFFFFFFFF;
	st->dirty = st->fresh = 0;
}

static s32 goal_px(const struct list_state *st)
{
	return st->base + FIX_ROUND(motion_goal(st->motion));
}

static void marquee_reset(u8 node, struct list_state *st)
{
	st->mq = marquee(node) ? MQ_WAIT : MQ_OFF;
	st->mq_wait = ui_theme()->marquee_delay;
	st->mq_px = 0;
	st->mq_step = 0;
	if (st->mq_set != 0xFF) {
		st->mq_set = 0xFF;
		st->sel_ok = 0;  // the row's map points at a marquee slot: restage it
	}
}

// Points the list's columns of the whole ring map at the blank tile, so no
// map row shows a slot it no longer owns (a shorter list after a reset).
// Only while the list isn't on screen.
static void blank_map(u8 node, const struct list_state *st)
{
	u16 *map = ui_map(node), blank = ui_shared_entry(ui_node_spec(node)->plane, UI_TILE_BLANK);
	for (u32 y = 0; y < 32; y++)
		for (u32 x = 0; x < st->tw; x++)
			map[y * 32 + x] = blank;
}

// Starts a page change to scroll target (or does it now while hidden)
static void page_change(u8 node, struct list_state *st, s32 target)
{
	if (!ui_node_shown(node)) {
		st->shown = st->base = target;
		motion_spring_jump(st->motion, 0);
		forget_slots(st);
		if (ui_node_view(node)->flags & UI_V_CLEARED)
			blank_map(node, st);  // else the first draw clears it
		st->page_cursor = st->cursor;
		assign(node, st, 0);
		ui_dirty(node, 1);
		return;
	}
	st->page = PAGE_HIDING;
	st->page_target = target;
	ui_plane_hide(ui_node_spec(node)->plane, 1);
}

static void set_cursor(u8 node, struct list_state *st, u32 cursor)
{
	if (cursor >= st->count)
		cursor = st->count ? st->count - 1 : 0;
	st->cursor = cursor;
	marquee_reset(node, st);
	st->sel_ok = st->sel_ok && st->sel_drawn == cursor;
	s32 target = ring_scroll_for(&st->ring, cursor / st->cols, goal_px(st), st->rows, 1);
	if (st->page != PAGE_NONE)
		st->page_target = target;
	else if (ring_far(&st->ring, st->shown, target))
		page_change(node, st, target);
	else if (ui_skipping()) {
		// Fast repeat: no animation, one row a frame (ring_step's cap)
		st->base = target;
		motion_spring_jump(st->motion, 0);
	} else {
		motion_spring_target(st->motion, FIX(target - st->base));
	}
	ui_dirty(node, 1);
}

static int list_init(u8 node)
{
	struct list_state *st = ui_node_state(node);
	if (!geometry(node, st))
		return 0;
	st->tiles = ui_tiles(node, slot_count(node) * st->tw * st->rt, 0, &st->first);
	if (st->tiles == RES_NONE)
		return 0;
	st->motion = motion_spring(MOTION_TO_S32(&st->spring), 0, SPRING_SCROLL, ui_owner(ui_node_view(node)));
	if (st->motion == MOTION_NONE) {
		ui_set_error("view %s: no motion left for a list's scroll", ui_node_view(node)->spec->name);
		return 0;
	}
	st->mq_set = 0xFF;
	input_wrap_init(&st->wrap, ui_input(), 0);
	ui_list_reset(node, 0);
	return 1;
}

void ui_list_reset(u8 node, u32 cursor)
{
	struct list_state *st = ui_node_state(node);
	st->count = model_count(node);
	st->rows = ring_rows_of(st->count, st->cols);
	if (cursor >= st->count)
		cursor = st->count ? st->count - 1 : 0;
	st->cursor = cursor;
	st->sel_ok = 0;
	marquee_reset(node, st);
	s32 target = ring_scroll_for(&st->ring, cursor / st->cols, 0, st->rows, 1);
	if (ui_node_shown(node)) {
		forget_slots(st);
		st->page = PAGE_NONE;
	}
	page_change(node, st, target);
}

void ui_list_refresh(u8 node, u32 lo, u32 hi)
{
	struct list_state *st = ui_node_state(node);
	for (u32 s = 0; s < st->ring.k; s++) {
		u32 r = st->slot_row[s];
		if (r != UI_ITEM_NONE && r * st->cols <= hi && r * st->cols + st->cols > lo)
			st->dirty |= 1u << s;
	}
	if (st->mq_set != 0xFF && st->cursor >= lo && st->cursor <= hi)
		marquee_reset(node, st);
	ui_dirty(node, 1);
}

void ui_list_set_cursor(u8 node, u32 cursor)
{
	struct list_state *st = ui_node_state(node);
	if (cursor != st->cursor)
		set_cursor(node, st, cursor);
}

u32 ui_list_cursor(u8 node)
{
	return ((const struct list_state *)ui_node_state(node))->cursor;
}

void ui_list_scroll_info(u8 node, u32 *content, u32 *view, u32 *scroll)
{
	const struct list_state *st = ui_node_state(node);
	*content = st->rows * st->ring.rh;
	*view = st->ring.h;
	*scroll = st->shown > 0 ? (u32)st->shown : 0;
}

static int list_event(u8 node, const struct ui_event *e)
{
	struct list_state *st = ui_node_state(node);
	if (e->type != UI_EV_KEYS)
		return 0;
	if (st->page == PAGE_HIDING)
		return (e->keys & (INPUT_DPAD | INPUT_PAGE | INPUT_A)) != 0;
	const InputState *in = ui_input();
	u32 page = st->ring.h / st->ring.rh;
	s32 cols = st->cols;
	if (e->keys & INPUT_A) {
		if (st->count)
			ui_emit(node, UI_EV_ACTIVATE, st->cursor);
		return 1;
	}
	static const u16 keys[] = {INPUT_UP, INPUT_DOWN, INPUT_LEFT, INPUT_RIGHT, INPUT_L, INPUT_R};
	for (u32 i = 0; i < 6; i++) {
		u16 key = keys[i];
		if (!(e->keys & key))
			continue;
		s32 delta;
		if (key == INPUT_UP || key == INPUT_DOWN)
			delta = cols;
		else if (key == INPUT_LEFT || key == INPUT_RIGHT)
			delta = cols > 1 ? 1 : 0;
		else
			delta = (s32)page * cols;
		if (key == INPUT_UP || key == INPUT_LEFT || key == INPUT_L)
			delta = -delta;
		if (!delta || !st->count)
			continue;
		u32 to;
		if (config(node)->flags & UI_LIST_WRAP) {
			to = input_move(in, &st->wrap, key, st->cursor, st->count, delta);
		} else {
			s32 t = (s32)st->cursor + delta;
			to = t < 0 ? 0 : (u32)t >= st->count ? st->count - 1 : (u32)t;
		}
		if (to != st->cursor) {
			set_cursor(node, st, to);
			ui_emit(node, UI_EV_CURSOR, to);
		}
		return 1;
	}
	return 0;
}

// Where a list row's name starts: after the icon column, if it has one
static u32 text_x(u8 node)
{
	u32 pad = ui_theme()->pad;
	return (config(node)->flags & UI_LIST_ICONS) ? UI_LIST_ICON_W + pad / 2 : pad;
}

// Pixels a list row's name may use
static u32 text_clip(u8 node, const struct list_state *st)
{
	u32 w = st->tw * 8u, used = text_x(node) + ui_theme()->pad + config(node)->detail_w;
	return w > used + 8 ? w - used : 8;
}

// Draws a 16x16 icon (2x2 tiles in row order) at the left of a list row's
// slot, over its paper: colour 0 lets the paper show
static void row_icon(u32 *tiles, u32 rt, const u32 *icon)
{
	u32 ty0 = (rt - 2) / 2;
	for (u32 t = 0; t < 4; t++) {
		u32 *dst = &tiles[((t & 1) * rt + ty0 + (t >> 1)) * 8];
		const u32 *src = &icon[t * 8];
		for (u32 y = 0; y < 8; y++) {
			u32 px = src[y];
			u32 set = (px | px >> 1 | px >> 2 | px >> 3) & 0x11111111u;
			dst[y] = (dst[y] & ~(set * 0xF)) | px;
		}
	}
}

static void marquee_tick(u8 node, struct list_state *st, u32 dt)
{
	if (st->mq == MQ_OFF || st->page != PAGE_NONE || st->shown != st->base + FIX_ROUND(st->spring))
		return;
	if (st->mq_wait > dt) {
		st->mq_wait = (u16)(st->mq_wait - dt);
		ui_wake();
		return;
	}
	switch (st->mq) {
	case MQ_WAIT: {
		// Only a cut name scrolls
		struct ui_item item = {0};
		u32 r = st->cursor;
		if (r < st->lo || r > st->hi || !config(node)->model->fetch(ui_node_view(node)->ctx, r, &item) ||
		    !item.text) {
			st->mq = MQ_OFF;
			return;
		}
		u32 len = item.len ? item.len : strlen(item.text);
		st->mq_width = (u16)text_width(ui_theme()->body, item.text, len);
		u32 clip = text_clip(node, st);
		if (st->mq_width <= clip) {
			st->mq = MQ_OFF;
			return;
		}
		st->mq = MQ_RUN;
		st->mq_px = 0;
		st->mq_wait = MQ_STEP_FRAMES;
		break;
	}
	case MQ_RUN: {
		u32 clip = text_clip(node, st);
		st->mq_px++;
		st->mq_step = 1;
		st->mq_wait = MQ_STEP_FRAMES;
		if (st->mq_px + clip >= (u32)st->mq_width + MQ_GAP) {
			st->mq = MQ_END;
			st->mq_wait = MQ_END_FRAMES;
		}
		ui_dirty(node, 2);
		break;
	}
	default:  // MQ_END: back to the start (the normal slot), which waits again
		marquee_reset(node, st);
		ui_dirty(node, 2);
		break;
	}
	ui_wake();
}

static void list_update(u8 node, u32 dt)
{
	struct list_state *st = ui_node_state(node);
	const struct ui_spec *spec = ui_node_spec(node);
	const struct ui_node *n = ui_node(node);
	const struct ui_view *v = ui_node_view(node);
	if (n->flags & UI_N_HIDDEN)
		return;  // its plane is off (ui_run.c); rows stay as they are
	if (st->page == PAGE_HIDING) {
		if (!ui_plane_hidden_now(spec->plane)) {
			ui_wake();
			return;
		}
		// Off screen now: every row can be drawn where it lands
		st->shown = st->base = st->page_target;
		motion_spring_jump(st->motion, 0);
		forget_slots(st);
		blank_map(node, st);
		st->page_cursor = st->cursor;
		assign(node, st, 0);
		st->page = PAGE_DRAW;
		ui_dirty(node, 1);
	} else if (st->page == PAGE_NONE) {
		s32 want = st->base + FIX_ROUND(st->spring);
		s32 next = ring_step(&st->ring, st->shown, want);
		int dir = next > st->shown ? 1 : next < st->shown ? -1 : (want > next) - (want < next);
		st->shown = next;
		s32 shift = ring_rebase(&st->ring, goal_px(st) - st->base);
		if (shift) {
			st->base += shift;
			motion_spring_shift(st->motion, -FIX(shift));
		}
		assign(node, st, dir);
		if (st->shown != want)
			ui_wake();
	}
	// Not while a view above shows its own plane there
	if (ui_plane_owned(node))
		gfx_set_scroll(spec->plane, (u16)(-n->x), (u16)(st->shown - n->y - v->offset));
	if (st->dirty || !st->sel_ok)
		ui_dirty(node, 1);
	marquee_tick(node, st, dt);
}

// The map entry for column tx, tile row ty of slot s, for item in bank
static u16 entry(const struct list_state *st, u32 s, u32 tx, u32 ty, u32 bank)
{
	return ui_entry(st->first + s * st->tw * st->rt + tx * st->rt + ty, bank);
}

// Writes row r's entries (from slot s, or the marquee slot) into rows, the
// map's rows from the row's first, with item sel selected
static void row_entries(const struct list_state *st, u16 *rows, u32 r, u32 s, u32 sel)
{
	u32 cw = st->tw / st->cols;
	for (u32 ty = 0; ty < st->rt; ty++) {
		for (u32 tx = 0; tx < st->tw; tx++) {
			u32 item = r * st->cols + tx / cw;
			u32 bank = item == sel && item < st->count ? UI_BANK_SELECTED : UI_BANK_NORMAL;
			u32 slot = s;
			if (item == sel && st->mq_set != 0xFF)
				slot = st->ring.k + st->mq_set;
			rows[ty * 32 + tx] = entry(st, slot, tx, ty, bank);
		}
	}
}

// Draws row r into slot s's tiles (or tiles, for the marquee)
static void draw_row(u8 node, struct list_state *st, u32 *tiles, u32 r, u32 skip)
{
	const struct ui_list_config *c = config(node);
	const struct ui_theme *t = ui_theme();
	const struct torii_font *font = t->body;
	struct text_window win = {tiles, st->tw, st->rt};
	u32 w = st->tw * 8u, h = st->rt * 8u, cw = w / st->cols;
	text_clear(&win, 0, 0, w, h, ui_paper(node, UI_ROLE_BG));
	u32 ink_def = ui_ink(node, UI_ROLE_TEXT);
	for (u32 col = 0; col < st->cols; col++) {
		u32 i = r * st->cols + col;
		if (i >= st->count)
			break;
		struct ui_item item = {0};
		if (!c->model->fetch(ui_node_view(node)->ctx, i, &item) || !item.text)
			continue;  // drawn when ui_list_refresh says it loaded
		u32 len = item.len ? item.len : strlen(item.text);
		u32 ink = item.ink ? item.ink : ink_def;
		if (st->cols == 1) {
			u32 y = (h - font->line_height) / 2, clip = text_clip(node, st), x = text_x(node);
			if (item.icon && (c->flags & UI_LIST_ICONS) && st->rt >= 2)
				row_icon(tiles, st->rt, item.icon);
			if (item.detail) {
				u32 dlen = strlen(item.detail);
				u32 dw = text_width(font, item.detail, dlen);
				if (dw + t->pad < w)
					text_draw(&win, font, w - t->pad - dw + 1, y, dw, UI_ROLE_DIM, item.detail,
						  dlen);
			}
			if (skip) {
				struct text_line line;
				u32 lead;
				text_layout_window(font, item.text, len, skip, clip, &line, &lead);
				text_draw_line(&win, font, x + lead, y, ink, &line);
			} else {
				text_draw(&win, font, x, y, clip, ink, item.text, len);
			}
			continue;
		}
		// A grid cell: an icon at the top if it is tall enough, the name below
		u32 x0 = col * cw, y = (h - font->line_height) / 2;
		if (item.icon && st->rt >= 4 && cw >= 16) {
			u32 tx0 = x0 / 8 + (cw / 8 - 2) / 2;
			for (u32 ty = 0; ty < 2; ty++)
				for (u32 tx = 0; tx < 2; tx++)
					memcpy(&tiles[((tx0 + tx) * st->rt + ty) * 8], &item.icon[(ty * 2 + tx) * 8],
					       32);
			y = 16 + (h - 16 - font->line_height) / 2;
		}
		struct text_line line;
		text_layout(font, item.text, len, cw - 4, &line);
		text_draw_line(&win, font, x0 + (cw - line.extent) / 2, y, ink, &line);
	}
}

static void list_draw(u8 node)
{
	struct list_state *st = ui_node_state(node);
	const struct ui_spec *spec = ui_node_spec(node);
	if (st->page == PAGE_HIDING || (ui_node(node)->flags & UI_N_HIDDEN))
		return;
	u16 *map = ui_map(node);
	// A page's rows show the cursor it started with; one that moved since
	// is restaged once the page is up
	u32 sel = st->page == PAGE_DRAW || !ui_node_shown(node) ? st->page_cursor : st->cursor;
	// While nothing of it is on screen (a page change, a view not shown
	// yet) a whole page may take more than a frame: it draws within the
	// budget and goes on next frame
	int hidden = st->page == PAGE_DRAW || !ui_node_shown(node);
	// Rows given new slots: neither their tiles nor their map rows are on screen
	for (u32 s = 0; s < st->ring.k; s++) {
		if (!(st->dirty >> s & 1))
			continue;
		if (hidden && frame_elapsed() > UI_DRAW_BUDGET) {
			ui_dirty(node, 1);  // the rest next frame
			return;
		}
		u32 r = st->slot_row[s];
		if (r != UI_ITEM_NONE) {
			draw_row(node, st, slot_tiles(st, spec->plane, s), r, 0);
			if (st->fresh >> s & 1)
				row_entries(st, map + ring_map_row(&st->ring, r) * 32, r, s, sel);
		}
		st->dirty &= ~(1u << s);
		st->fresh &= ~(1u << s);
	}
	st->dirty = st->fresh = 0;
	if (st->page == PAGE_DRAW) {
		st->page = PAGE_NONE;
		st->sel_drawn = st->page_cursor;
		st->sel_ok = st->page_cursor == st->cursor;
		ui_plane_hide(spec->plane, 0);
		gfx_hold_for_queue(GFX_DIRTY_DISPCNT);
		if (!st->sel_ok) {
			// Keys moved the cursor while the page drew: scroll on to it
			st->page_cursor = st->cursor;
			s32 target = ring_scroll_for(&st->ring, st->cursor / st->cols, st->shown, st->rows, 1);
			if (ring_far(&st->ring, st->shown, target))
				page_change(node, st, target);
			else
				motion_spring_target(st->motion, FIX(target - st->base));
			ui_dirty(node, 1);
		}
		return;
	}
	if (!ui_node_shown(node)) {
		st->sel_drawn = st->page_cursor;
		st->sel_ok = st->page_cursor == st->cursor;
		return;
	}
	// The marquee: its next step into the slot the map doesn't show, then a
	// flip of the row's map entries to it
	if (st->mq_step) {
		u32 r = st->cursor / st->cols;
		if (ui_map_busy(node, ring_map_row(&st->ring, r), st->rt)) {
			ui_dirty(node, 2);  // the last step's flip hasn't landed
		} else if (r >= st->lo && r <= st->hi && st->slot_row[ring_slot(&st->ring, r)] == r) {
			u32 set = st->mq_set == 0 ? 1 : 0;
			draw_row(node, st, slot_tiles(st, spec->plane, st->ring.k + set), r, st->mq_px);
			u8 old = st->mq_set;
			st->mq_set = (u8)set;
			u16 *rows = ui_map_rows(node, ring_map_row(&st->ring, r), st->rt);
			if (rows) {
				row_entries(st, rows, r, ring_slot(&st->ring, r), st->cursor);
				st->mq_step = 0;
			} else {
				st->mq_set = old;  // try again next frame
				ui_dirty(node, 2);
			}
		} else {
			st->mq_step = 0;
		}
	}
	// The selection: restage the rows of the old and new selected items
	if (!st->sel_ok) {
		u32 rows_to[2] = {st->sel_drawn / st->cols, st->cursor / st->cols};
		u32 n = rows_to[0] == rows_to[1] ? 1 : 2;
		for (u32 i = 0; i < n; i++) {
			u32 r = rows_to[i];
			if (r < st->lo || r > st->hi || st->slot_row[ring_slot(&st->ring, r)] != r)
				continue;  // not resident: its map rows are written when it is
			u16 *rows = ui_map_rows(node, ring_map_row(&st->ring, r), st->rt);
			if (!rows)
				return;  // staging is full: next frame
			row_entries(st, rows, r, ring_slot(&st->ring, r), st->cursor);
		}
		st->sel_drawn = st->cursor;
		st->sel_ok = 1;
	}
}

const struct ui_ops ui_list_ops = {
	sizeof(struct list_state), list_measure, list_init, list_event, list_update, list_draw};
