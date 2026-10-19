// Lists and grids (ui.h): rows drawn from a ui_list_model into a ring of
// row slots on a BG of their own, scrolled pixel by pixel by a spring
// (ui/ring.h has the maths).
//
// - Each row slot is a text window of the row's width x its height in
//   tiles (a 30x2 window for a full-width 16 px row). A row entering the
//   viewport is drawn into a slot that isn't on screen, and its map rows
//   are written straight away: they aren't on screen either. At rest, the
//   rows just above and below the screen are drawn too (spare slots), so a
//   step from rest draws nothing.
// - A held key's repeats make the scroll glide: between repeats it moves at
//   the cursor's speed (input_phase, ring_glide), driving the spring's goal
//   ahead by the spring's lag, so a held d-pad scrolls at a steady speed
//   with the cursor on screen. Let go, it brakes to the next whole row.
// - The selection is a palette bank switch in the selected row's (or
//   cell's) map entries, through the staging area, so it shows in one
//   VBlank: text is never redrawn for it.
// - A jump too far to scroll to in a few frames (a new folder, a wrap from
//   the end to the start) is a page change: the plane is hidden at the
//   next commit, the rows on screen are drawn the frame after (the rest
//   fill in later), and the plane fades in from the next VBlank
//   (ui_plane_reveal).
// - With UI_LIST_MARQUEE, a cut name on the selected row scrolls after the
//   cursor rests: two extra slots, wider than a row, take turns, flipped by
//   map in VBlank. Glyphs slide in and out at both edges: the slots have
//   MQ_MARGIN hidden tile columns each side, where partly visible glyphs
//   land, and the strips outside the name are cleared.
#include <string.h>

#include "ring.h"
#include "ui_priv.h"
#include "../engine/cycles.h"
#include "../engine/frame.h"
#include "../engine/gfx.h"
#include "../engine/res.h"
#include "../engine/text.h"
#include "../tlog.h"

enum {
	PAGE_NONE,
	PAGE_HIDING,  // waiting for the plane to be off screen
	PAGE_DRAW,    // rows assigned: draw the visible ones, then reveal the plane
};

enum {
	MQ_OFF,
	MQ_WAIT,  // the cursor rests; scrolls after mq_wait frames
	MQ_RUN,   // scrolling
	MQ_END,   // at the end, pausing before it starts over
};

#define MQ_MARGIN 2  // hidden tile columns each side of a marquee slot
#define ROW_GUESS 30000  // cycles a row may take, before one has been timed
// The scroll's spring brakes stiffer when a glide ends, so the list stops
// within about a row of where the key was let go
#define SPRING_BRAKE 0x18000  // 1.5
#define GLIDE_AUTO 0x8000  // glide_key of ui_list_autoscroll's moves (no key's bit)

struct list_state {
	struct ui_ring ring;
	u8 cols;    // cells per row: 1 for a list
	u8 tw, rt;  // a row's width and height in tiles
	u8 tiles;   // res handle of the slots
	u16 first;  // first slot's tile number
	u8 page;
	u8 sel_ok;  // the map shows the selection at sel_drawn
	u8 plain;   // UI_LIST_PLAIN: nothing shows as selected
	u8 queued_a;  // A pressed during a page change: dispatched once it is drawn
	u32 count, rows;
	u32 cursor;
	u32 sel_drawn;  // the item whose map entries are in the selected bank
	s32 shown;      // content px at the viewport's top, as the next commit shows it
	s32 base;       // the spring's base, px
	s32 spring;     // the spring's value (16.16, from base), written by motion
	s32 page_target;
	s32 target;     // the scroll the cursor's row wants (ring_scroll_for), px
	u16 glide_key;  // the held key whose repeats move the cursor, 0 none
	s16 glide_step; // rows each repeat moves it
	s32 glide_speed;  // the glide's last speed, px a frame (16.16)
	u32 glide_at;     // InputState.now when a repeat of glide_key last moved the cursor
	u8 braking;       // a glide ended and the spring brakes: the cursor stays on screen
	s16 set_offset[2];  // the view's offset in the scrolls set last frame and this one
	s32 auto_rate;    // ui_list_autoscroll: items a frame (16.16), 0 off
	u32 auto_phase;   // towards its next move
	u8 auto_move;     // set_cursor is making that move
	u32 page_cursor;  // the cursor a page change's rows are drawn for
	MotionId motion;
	u16 slot_row[UI_RING_MAX_SLOTS];
	u16 drawn_row[UI_RING_MAX_SLOTS];  // the row each slot's tiles hold
	u32 dirty;   // slots to draw: the window's dirty-row mask
	u32 fresh;   // slots given a new row: their map rows need writing
	u32 blank;   // slots holding nothing but paper: no clear needed
	u32 lo, hi;  // resident rows
	u32 plo, phi;
	u32 row_cycles;  // what the last row took to draw (hidden drawing's budget)
	InputWrap wrap;
	// Marquee
	u8 mq;
	u8 mq_set;   // the marquee slot the map shows (0 or 1), 0xFF neither
	u8 mq_step;  // draw the next step
	u8 mq_acc;   // sub-pixel progress, in 60ths of a pixel
	u16 mq_wait;
	u16 mq_px;
	u16 mq_width;
};

// What drawing a row needs, looked up once per draw
struct row_ctx {
	const struct ui_list_model *model;
	void *ctx;
	const struct torii_font *font;
	u32 w, h, cw;  // a row's size and a cell's width, px
	u32 y;         // a list row's text top
	u32 pad;       // the inset from a row's edges
	u32 x;         // where a list row's name starts: pad, or after the icon column
	u32 clip;      // the name's width
	int icons;     // UI_LIST_ICONS
	u32 paper, ink;
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

// Tiles of the ring's slots, and of the two marquee slots after them
static u32 tile_count(u8 node)
{
	const struct list_state *st = ui_node_state(node);
	u32 mq = marquee(node) ? 2u * (st->tw + 2u * MQ_MARGIN) * st->rt : 0;
	return st->ring.k * st->tw * st->rt + mq;
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
	need->bg_tiles = (u16)(need->bg_tiles + tile_count(node));
	return 1;
}

static u32 *tile_addr(u32 plane, u32 tile)
{
	return (u32 *)(0x06000000 + ui_plane_cb(plane) * 0x4000 + tile * 32u);
}

static u32 slot_tile(const struct list_state *st, u32 slot)
{
	return st->first + slot * st->tw * st->rt;
}

// The first tile of marquee slot set (0 or 1)
static u32 mq_tile(const struct list_state *st, u32 set)
{
	return slot_tile(st, st->ring.k) + set * (st->tw + 2u * MQ_MARGIN) * st->rt;
}

static u32 model_count(u8 node)
{
	u32 n = config(node)->model->count(ui_node_view(node)->ctx);
	if ((config(node)->flags & UI_LIST_FIXED) && !config(node)->cell_w) {
		// Content within the viewport never scrolls, so each row keeps a slot
		const struct list_state *st = ui_node_state(node);
		if (n > st->ring.k)
			n = st->ring.k;
	}
	return n > UI_LIST_MAX ? UI_LIST_MAX : n;
}

// Gives row r its slot, to draw (no check that it is off screen)
static void give_slot(struct list_state *st, u32 r)
{
	u32 s = ring_slot(&st->ring, r);
	if (st->slot_row[s] != r) {
		st->slot_row[s] = (u16)r;
		st->dirty |= 1u << s;
		st->fresh |= 1u << s;
	}
}

// Gives the resident rows at the shown scroll their slots
static void assign(u8 node, struct list_state *st, int dir)
{
	u32 lo, hi;
	ring_resident(&st->ring, st->shown, dir, st->rows, &lo, &hi);
	for (u32 r = lo; r <= hi && lo <= hi; r++)
		give_slot(st, r);
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

// At rest: the row below the screen, then the one above, get their slots
// (one a frame), so the next step needs no drawing. They aren't on screen,
// and with k >= visible + 2 their slots are no resident row's (ring.h).
static void fill_spare(struct list_state *st)
{
	if (st->lo > st->hi)
		return;
	if (st->hi + 1 < st->rows && st->slot_row[ring_slot(&st->ring, st->hi + 1)] != st->hi + 1)
		give_slot(st, st->hi + 1);
	else if (st->lo && st->slot_row[ring_slot(&st->ring, st->lo - 1)] != st->lo - 1)
		give_slot(st, st->lo - 1);
}

static void forget_slots(struct list_state *st)
{
	for (u32 s = 0; s < UI_RING_MAX_SLOTS; s++)
		st->slot_row[s] = st->drawn_row[s] = UI_ITEM_NONE;
	st->lo = 1;
	st->hi = 0;
	st->plo = st->phi = 0xFFFFFFFF;
	st->dirty = st->fresh = 0;
}

static s32 goal_px(const struct list_state *st)
{
	return st->base + FIX_ROUND(motion_goal(st->motion));
}

// The held key whose repeat moved the cursor from old to cursor this frame:
// the scroll then glides (0: a press, or a move from code)
static u16 glide_key(u32 old, u32 cursor)
{
	const InputState *in = ui_input();
	u16 keys = in->fire & ~in->pressed & in->repeating;
	if (cursor > old)
		keys &= INPUT_DOWN | INPUT_RIGHT | INPUT_R;
	else if (cursor < old)
		keys &= INPUT_UP | INPUT_LEFT | INPUT_L;
	else
		keys = 0;
	return (u16)(keys & -keys);  // one key
}

// One frame of a glide: the spring's goal runs with the cursor's speed; once
// the key is let go (or another direction restarts it), it brakes
static void glide(struct list_state *st)
{
	const InputState *in = ui_input();
	u16 key = st->glide_key;
	u32 rate, phase, row = st->cursor / st->cols;
	if (key == GLIDE_AUTO) {
		rate = (u32)(st->auto_rate < 0 ? -st->auto_rate : st->auto_rate);
		phase = st->auto_phase;
	} else {
		// A repeat that didn't move the cursor (the list's end, or a view
		// above took the key) ends the glide as letting go does
		int stalled = (in->fire & key) && st->glide_at != in->now;
		rate = (in->held & key) && !stalled ? input_rate(in, key) : 0;
		phase = input_phase(in, key);
	}
	if (!rate) {
		s32 at = st->base + FIX_ROUND(st->spring);
		st->target = ring_glide_stop(&st->ring, row, st->target, at, st->glide_speed, SPRING_BRAKE, st->rows);
		st->glide_key = 0;
		st->braking = 1;
		motion_spring_omega(st->motion, SPRING_BRAKE);
		motion_spring_target(st->motion, FIX(st->target - st->base));
		return;
	}
	struct ring_glide g;
	ring_glide(&st->ring, row, st->glide_step, st->target, st->rows, phase, rate, SPRING_SCROLL, &g);
	st->glide_speed = g.speed;
	motion_spring_target(st->motion, FIX(g.goal - st->base));
}

static void marquee_reset(u8 node, struct list_state *st)
{
	st->mq = marquee(node) ? MQ_WAIT : MQ_OFF;
	st->mq_wait = ui_theme()->marquee_delay;
	st->mq_px = 0;
	st->mq_acc = 0;
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
	st->target = target;
	st->glide_key = 0;
	st->braking = 0;
	motion_spring_omega(st->motion, SPRING_SCROLL);
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
	u32 old = st->cursor;
	st->cursor = cursor;
	marquee_reset(node, st);
	st->sel_ok = st->sel_ok && st->sel_drawn == cursor;
	// A repeat glides on; a press or a move from code springs
	s32 step = (s32)(cursor / st->cols) - (s32)(old / st->cols);
	st->glide_key = st->page != PAGE_NONE || !step ? 0 : st->auto_move ? GLIDE_AUTO : glide_key(old, cursor);
	st->glide_step = (s16)step;
	st->glide_at = ui_input()->now;
	st->braking = 0;
	motion_spring_omega(st->motion, SPRING_SCROLL);  // after a glide's brake
	// During a page change, from where its page will be
	s32 from = st->page != PAGE_NONE ? st->page_target : st->target;
	s32 target = ring_scroll_for(&st->ring, cursor / st->cols, from, st->rows, 1);
	if (st->page != PAGE_NONE)
		st->page_target = target;
	else if (ring_far(&st->ring, st->shown, target))
		page_change(node, st, target);
	else {
		st->target = target;
		if (!st->glide_key)
			motion_spring_target(st->motion, FIX(target - st->base));
	}
	ui_dirty(node, 1);
}

static int list_init(u8 node)
{
	struct list_state *st = ui_node_state(node);
	if (!geometry(node, st))
		return 0;
	st->tiles = ui_tiles(node, tile_count(node), 0, &st->first);
	if (st->tiles == RES_NONE)
		return 0;
	st->motion = motion_spring(MOTION_TO_S32(&st->spring), 0, SPRING_SCROLL, ui_owner(ui_node_view(node)));
	if (st->motion == MOTION_NONE) {
		ui_set_error("view %s: no motion left for a list's scroll", ui_node_view(node)->spec->name);
		return 0;
	}
	st->mq_set = 0xFF;
	st->plain = (config(node)->flags & UI_LIST_PLAIN) != 0;
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

void ui_list_autoscroll(u8 node, s32 rate)
{
	struct list_state *st = ui_node_state(node);
	if (rate && !st->auto_rate)
		st->auto_phase = 0;
	st->auto_rate = rate;
	ui_wake();
}

// One frame of ui_list_autoscroll: the cursor moves an item each time the
// phase passes a whole one; at the list's end it stops
static void autoscroll(u8 node, struct list_state *st, u32 dt)
{
	if (!st->auto_rate || st->page != PAGE_NONE)
		return;
	ui_wake();
	u32 rate = (u32)(st->auto_rate < 0 ? -st->auto_rate : st->auto_rate);
	st->auto_phase += rate * dt;
	if (st->auto_phase < INPUT_ONE)
		return;
	st->auto_phase -= INPUT_ONE;
	if (st->auto_phase >= INPUT_ONE)
		st->auto_phase = INPUT_ONE - 1;
	u32 cols = st->cols;
	if (st->auto_rate > 0 ? st->cursor + cols >= st->count : st->cursor < cols) {
		st->auto_rate = 0;  // the end: glide() brakes
		return;
	}
	st->auto_move = 1;
	set_cursor(node, st, st->auto_rate > 0 ? st->cursor + cols : st->cursor - cols);
	st->auto_move = 0;
}

u32 ui_list_cursor(u8 node)
{
	return ((const struct list_state *)ui_node_state(node))->cursor;
}

int ui_list_paging(u8 node)
{
	return ((const struct list_state *)ui_node_state(node))->page != PAGE_NONE;
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
	const InputState *in = ui_input();
	u32 page = st->ring.h / st->ring.rh;
	s32 cols = st->cols;
	if (e->keys & INPUT_A) {
		// Moves during a page change only move the page it draws (set_cursor);
		// A waits until that page is up
		if (st->page != PAGE_NONE)
			st->queued_a = 1;
		else if (st->count)
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

// Draws a 16x16 icon (2x2 tiles in row order) at tile column 0 of a row's
// tiles, over its paper: colour 0 lets the paper show
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
	const struct ui_theme *t = ui_theme();
	if (st->mq_wait > dt) {
		st->mq_wait = (u16)(st->mq_wait - dt);
		ui_wake();
		return;
	}
	st->mq_wait = 0;
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
		st->mq_width = (u16)text_width(t->body, item.text, len);
		if (st->mq_width <= text_clip(node, st)) {
			st->mq = MQ_OFF;
			return;
		}
		// The cut name without its "...", from where it starts
		st->mq = MQ_RUN;
		st->mq_px = 0;
		st->mq_acc = 0;
		st->mq_step = 1;
		ui_dirty(node, 2);
		break;
	}
	case MQ_RUN: {
		// speed px a second, a pixel at most a frame (dt 1), in 60ths
		u32 speed = t->marquee_speed > 60 ? 60 : t->marquee_speed ? t->marquee_speed : 1;
		u32 acc = st->mq_acc + speed * dt;
		if (acc >= 60) {
			st->mq_px = (u16)(st->mq_px + acc / 60);
			st->mq_step = 1;
			ui_dirty(node, 2);
		}
		st->mq_acc = (u8)(acc % 60);
		if (st->mq_px + text_clip(node, st) >= (u32)st->mq_width + t->marquee_gap) {
			st->mq = MQ_END;
			st->mq_wait = t->marquee_pause;
		}
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
		// Off screen now: every row can be drawn where it lands. Only the
		// rows on screen (dir 0): the spare ones fill in once it shows
		st->shown = st->base = st->target = st->page_target;
		motion_spring_jump(st->motion, 0);
		forget_slots(st);
		blank_map(node, st);
		st->page_cursor = st->cursor;
		assign(node, st, 0);
		st->page = PAGE_DRAW;
		ui_dirty(node, 1);
	} else if (st->page == PAGE_NONE) {
		autoscroll(node, st, dt);
		if (st->glide_key)
			glide(st);
		s32 want = st->base + FIX_ROUND(st->spring), max = ring_max_scroll(&st->ring, st->rows);
		st->braking = st->braking && motion_running(st->motion);
		if (st->glide_key || st->braking)
			want = ring_keep_on_screen(&st->ring, st->cursor / st->cols, want);
		want = want < 0 ? 0 : want > max ? max : want;
		s32 next = ring_step(&st->ring, st->shown, want);
		int dir = next > st->shown ? 1 : next < st->shown ? -1 : (want > next) - (want < next);
		int still = next == st->shown && next == want;
		st->shown = next;
		s32 shift = ring_rebase(&st->ring, goal_px(st) - st->base);
		if (shift) {
			st->base += shift;
			motion_spring_shift(st->motion, -FIX(shift));
		}
		assign(node, st, dir);
		if (st->shown != want)
			ui_wake();
		else if (still && !st->dirty && ui_node_shown(node))
			fill_spare(st);
	}
	// Not while a view above shows its own plane there
	if (ui_plane_owned(node))
		gfx_set_scroll(spec->plane, (u16)(-n->x), (u16)(st->shown - n->y - v->offset));
	st->set_offset[0] = st->set_offset[1];
	st->set_offset[1] = v->offset;
	if (st->dirty || !st->sel_ok)
		ui_dirty(node, 1);
	marquee_tick(node, st, dt);
}

// Writes row r's map entries into rows, the map's rows from the row's
// first: slot s's tiles (a marquee slot's for the selected item while one
// shows), with item sel in the selected bank
static void row_entries(const struct list_state *st, u16 *rows, u32 r, u32 s, u32 sel)
{
	if (st->plain)
		sel = 0xFFFFFFFFu;  // no item
	u32 rt = st->rt, cw = st->tw / st->cols, item = r * st->cols;
	u32 tile = slot_tile(st, s);
	for (u32 col = 0; col < st->cols; col++, item++, tile += cw * rt, rows += cw) {
		u32 t = tile, bank = 0;
		if (item == sel && item < st->count) {
			bank = UI_BANK_SELECTED << 12;
			if (st->mq_set != 0xFF)
				t = mq_tile(st, st->mq_set) + MQ_MARGIN * rt;
		}
		t |= bank;
		u16 *p = rows, *end = rows + cw;
		if (rt == 2) {
			// 16 px rows: a tile column is two map rows
			for (; p < end; p++, t += 2) {
				p[0] = (u16)t;
				p[32] = (u16)(t + 1);
			}
			continue;
		}
		for (; p < end; p++)
			for (u32 ty = 0; ty < rt; ty++)
				p[ty * 32] = (u16)t++;
	}
}

static void row_ctx_init(u8 node, const struct list_state *st, struct row_ctx *rc)
{
	const struct ui_theme *t = ui_theme();
	rc->model = config(node)->model;
	rc->ctx = ui_node_view(node)->ctx;
	rc->font = t->body;
	rc->w = st->tw * 8u;
	rc->h = st->rt * 8u;
	rc->cw = rc->w / st->cols;
	rc->y = (rc->h - rc->font->line_height) / 2;
	rc->pad = t->pad;
	rc->x = text_x(node);
	rc->icons = (config(node)->flags & UI_LIST_ICONS) != 0;
	rc->clip = text_clip(node, st);
	rc->paper = ui_paper(node, UI_ROLE_BG);
	rc->ink = ui_ink(node, UI_ROLE_TEXT);
}

// The detail, right-aligned in the row, its window starting x0 px left of
// the row (line is scratch)
static void draw_detail(const struct text_window *win, const struct row_ctx *rc, u32 x0, const char *detail,
			struct text_line *line)
{
	text_layout(rc->font, detail, strlen(detail), rc->w, line);
	if (!line->truncated && line->advance + rc->pad < rc->w)
		text_draw_line(win, rc->font, x0 + rc->w + 1 - rc->pad - line->advance, rc->y, UI_ROLE_DIM, line);
}

// 1 if map row mr (in tiles) of a row rh px tall shows on screen now, as the
// last commit left the list's plane (its scroll, and whether it is on)
static int map_row_live(u8 node, const struct list_state *st, u32 mr)
{
	const struct ui_spec *spec = ui_node_spec(node);
	if (!(gfx_live_dispcnt & (0x100u << spec->plane)) || (gfx_live_dispcnt & 0x80))
		return 0;
	const struct ui_node *n = ui_node(node);
	// The scroll the last commit wrote went out with last frame's offset (a
	// dialog sliding in moves it); another view may own the plane
	if (!ui_plane_owned(node) || st->set_offset[0] != st->set_offset[1])
		return 0;
	u32 top = (u32)(gfx_live_bgofs[spec->plane * 2 + 1] + n->y + st->set_offset[0]) & 255;
	u32 d = (mr * 8 - top) & 255;
	return d < st->ring.h || d > 256u - st->ring.rh;
}

// Draws row r into a slot's tiles, cleared first unless blank (they hold
// only paper); 1 if anything but paper was drawn
static int draw_row(const struct list_state *st, const struct row_ctx *rc, u32 *tiles, u32 r, int blank)
{
	const struct torii_font *font = rc->font;
	struct text_window win = {tiles, st->tw, st->rt};
	struct text_line line;
	int drew = 0;
	if (!blank)
		text_clear(&win, 0, 0, rc->w, rc->h, rc->paper);
	for (u32 col = 0, i = r * st->cols; col < st->cols && i < st->count; col++, i++) {
		struct ui_item item = {0};
		if (!rc->model->fetch(rc->ctx, i, &item) || !item.text)
			continue;  // drawn when ui_list_refresh says it loaded
		u32 len = item.len ? item.len : strlen(item.text);
		u32 ink = item.ink ? item.ink : rc->ink;
		drew = 1;
		if (st->cols == 1) {
			if (item.icon && rc->icons && st->rt >= 2)
				row_icon(tiles, st->rt, item.icon);
			text_layout(font, item.text, len, rc->clip, &line);
			text_draw_line(&win, font, rc->x, rc->y, ink, &line);
			if (item.detail)
				draw_detail(&win, rc, 0, item.detail, &line);
			continue;
		}
		// A grid cell: an icon at the top if it is tall enough, the name below
		u32 x0 = col * rc->cw, y = rc->y;
		if (item.icon && st->rt >= 4 && rc->cw >= 16) {
			u32 tx0 = x0 / 8 + (rc->cw / 8 - 2) / 2;
			for (u32 ty = 0; ty < 2; ty++)
				for (u32 tx = 0; tx < 2; tx++)
					memcpy(&tiles[((tx0 + tx) * st->rt + ty) * 8], &item.icon[(ty * 2 + tx) * 8],
					       32);
			y = 16 + (rc->h - 16 - font->line_height) / 2;
		}
		text_layout(font, item.text, len, rc->cw - 4, &line);
		text_draw_line(&win, font, x0 + (rc->cw - line.extent) / 2, y, ink, &line);
	}
	return drew;
}

// Draws the selected row's name from px skip of it into a marquee slot:
// glyphs partly in the name's span drawn whole into the slot's hidden
// margins and the clip strips, which are then cleared, so they slide in
// and out at both edges; the detail last
static void draw_marquee(const struct list_state *st, const struct row_ctx *rc, u32 *tiles, u32 r, u32 skip)
{
	const struct torii_font *font = rc->font;
	u32 m = MQ_MARGIN * 8, left = m + rc->x, right = left + rc->clip;
	struct text_window win = {tiles, (u8)(st->tw + 2 * MQ_MARGIN), st->rt};
	u32 ww = win.width * 8u;
	text_clear(&win, 0, 0, ww, rc->h, rc->paper);
	struct ui_item item = {0};
	if (!rc->model->fetch(rc->ctx, r, &item) || !item.text)
		return;
	struct text_line line;
	s32 lead;
	u32 len = item.len ? item.len : strlen(item.text);
	text_layout_span(font, item.text, len, skip, rc->clip, &line, &lead);
	text_draw_line(&win, font, (u32)((s32)left + lead), rc->y, item.ink ? item.ink : rc->ink, &line);
	text_clear(&win, 0, 0, left, rc->h, rc->paper);
	text_clear(&win, right, 0, ww - right, rc->h, rc->paper);
	if (item.icon && rc->icons && st->rt >= 2)
		row_icon(tiles + MQ_MARGIN * st->rt * 8, st->rt, item.icon);  // the row's tile column 0
	if (item.detail)
		draw_detail(&win, rc, m, item.detail, &line);
}

static void list_draw(u8 node)
{
	struct list_state *st = ui_node_state(node);
	const struct ui_spec *spec = ui_node_spec(node);
	if (st->page == PAGE_HIDING || (ui_node(node)->flags & UI_N_HIDDEN))
		return;
	struct row_ctx rc;
	row_ctx_init(node, st, &rc);
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
		u32 bit = 1u << s;
		if (!(st->dirty & bit))
			continue;
		if (hidden && !ui_draw_room(st->row_cycles ? st->row_cycles : ROW_GUESS)) {
			ui_dirty(node, 1);  // the rest next frame
			return;
		}
		u32 r = st->slot_row[s];
		if (r != UI_ITEM_NONE) {
			// The ring's promise (ring.h): a row given a slot is drawn where
			// the screen doesn't show it, its tiles nor its map rows
			if (!hidden && (st->fresh & bit) && st->ring.k < st->ring.ring_rows) {
				TLOG_ASSERT(!map_row_live(node, st, ring_map_row(&st->ring, r)));
				if (st->drawn_row[s] != UI_ITEM_NONE)
					TLOG_ASSERT(!map_row_live(node, st, ring_map_row(&st->ring, st->drawn_row[s])));
			}
			st->drawn_row[s] = (u16)r;
			u32 start = cycles_now();
			if (draw_row(st, &rc, tile_addr(spec->plane, slot_tile(st, s)), r, (st->blank & bit) != 0))
				st->blank &= ~bit;
			else
				st->blank |= bit;
			if (st->fresh & bit)
				row_entries(st, map + ring_map_row(&st->ring, r) * 32, r, s, sel);
			st->row_cycles = cycles_since(start);
		}
		st->dirty &= ~bit;
		st->fresh &= ~bit;
	}
	st->dirty = st->fresh = 0;
	if (st->page == PAGE_DRAW) {
		st->page = PAGE_NONE;
		st->sel_drawn = st->page_cursor;
		st->sel_ok = st->page_cursor == st->cursor;
		ui_plane_reveal(spec->plane);
		gfx_hold_for_queue(GFX_DIRTY_DISPCNT);
		if (st->queued_a) {
			st->queued_a = 0;
			ui_queue_keys(INPUT_A);
		}
		if (!st->sel_ok) {
			// Keys moved the cursor while the page drew: scroll on to it
			st->page_cursor = st->cursor;
			s32 target = ring_scroll_for(&st->ring, st->cursor / st->cols, st->shown, st->rows, 1);
			if (ring_far(&st->ring, st->shown, target)) {
				page_change(node, st, target);
			} else {
				st->target = target;
				motion_spring_target(st->motion, FIX(target - st->base));
			}
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
			draw_marquee(st, &rc, tile_addr(spec->plane, mq_tile(st, set)), r, st->mq_px);
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
			u32 s = ring_slot(&st->ring, r);
			if (r < st->lo || r > st->hi || st->slot_row[s] != r) {
				// Not resident (so not on screen): it is drawn afresh,
				// map rows and all, when it is
				if (st->slot_row[s] == r)
					st->slot_row[s] = UI_ITEM_NONE;
				continue;
			}
			u16 *rows = ui_map_rows(node, ring_map_row(&st->ring, r), st->rt);
			if (!rows)
				return;  // staging is full: next frame
			row_entries(st, rows, r, s, st->cursor);
		}
		st->sel_drawn = st->cursor;
		st->sel_ok = 1;
	}
}

const struct ui_ops ui_list_ops = {
	sizeof(struct list_state), list_measure, list_init, list_event, list_update, list_draw};
