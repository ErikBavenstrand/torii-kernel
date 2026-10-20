// Lists (ui.h): rows drawn from a ui_list_model into a ring of row slots on
// a BG of their own, scrolled pixel by pixel by a spring (ui/ring.h has the
// maths).
//
// - The slots lie back to back in one column-major canvas of tiles, the
//   row's width by period / 8 tile rows, at any row pitch from 8 to 32 px:
//   slot s is canvas lines [s * rh, s * rh + rh). The list's map, written
//   once, shows the canvas over and over down its 64 rows, and the BG's
//   vertical scroll alone scrolls it (ring_vofs): no map entry changes while
//   the list scrolls. Rows at a pitch that isn't a multiple of 8 share
//   tiles with their neighbours, never a word (a pixel row).
// - A row entering the viewport is drawn into a slot that isn't on screen.
//   At rest, the rows just above and below the screen are drawn too (spare
//   slots), so a step from rest draws nothing.
// - A held key's repeats make the scroll glide: between repeats it moves at
//   the cursor's speed (input_phase, ring_glide), driving the spring's goal
//   ahead by the spring's lag, so a held d-pad scrolls at a steady speed
//   with the cursor on screen. Let go, it brakes to the next whole row.
// - The selection: a list with no paper (its spec's paper 0) draws its rows
//   transparent over the card behind it, and the selection is a pill of
//   sprites between the two (ui_obj_add; the pill's OBJ priority sits under
//   the list's plane), which slides to the cursor's row on a spring of its
//   own: text is never redrawn for it. A list with a paper (the v0.3
//   screens, menus on a panel) draws its rows on it, and the selected row
//   with the pill drawn under its text, redrawn when the cursor moves.
// - A row redrawn while it may be on screen (ui_list_refresh, the drawn
//   pill, a marquee step) is drawn into a pool in EWRAM and copied over its
//   slot in VBlank, a strided copy of its lines (gfx_queue_copy2d): drawn
//   in place, it tore while the screen scanned it out (the tear watch, -T).
// - A jump too far to scroll to in a few frames (a new folder, a wrap from
//   the end to the start) is a page change: the old page fades out
//   (ui_plane_fade_out) while the new page's rows are drawn ahead into the
//   row cache, a few each frame; then the plane is hidden, the rows on
//   screen are copied in from the cache (or drawn) the frame after, within
//   UI_PAGE_BUDGET (the rest fill in later), and the plane fades in from the
//   next VBlank (ui_plane_reveal).
// - Drawn rows are kept in a cache keyed by item (ui/rowcache.h), in EWRAM:
//   while the list rests, its rows are copied there from their slots, one
//   a frame (frames that would otherwise do nothing: scrolling never pays
//   for it), and a row scrolled back to is copied in instead of drawn. A
//   model says when an item's row changes (ui_list_refresh, or
//   ui_list_reset for a new model), which drops it.
// - With UI_LIST_MARQUEE, a cut name on the selected row scrolls after the
//   cursor rests: each step is drawn into the pool, wider than the row by
//   MQ_MARGIN hidden tile columns each side where partly visible glyphs
//   land (the strips outside the name are cleared), and copied over the
//   row's slot like a refresh.
#include <gba_systemcalls.h>
#include <string.h>

#include "ring.h"
#include "rowcache.h"
#include "ui_priv.h"
#include "../engine/cycles.h"
#include "../engine/frame.h"
#include "../engine/gfx.h"
#include "../engine/res.h"
#include "../engine/text.h"
#include "../tlog.h"

enum {
	PAGE_NONE,
	PAGE_FADING,  // the old page fades out
	PAGE_HIDING,  // waiting for the plane to be off screen
	PAGE_DRAW,    // rows assigned: draw the visible ones, then reveal the plane
};

enum {
	MQ_OFF,
	MQ_WAIT,  // the cursor rests; scrolls after mq_wait frames
	MQ_RUN,   // scrolling
	MQ_END,   // at the end, pausing before it starts over
};

#define MQ_MARGIN 2  // hidden tile columns each side of a marquee step
#define ROW_GUESS 30000  // cycles a row may take, before one has been timed
// Cycles a row copied from the cache into a slot takes, at most: a 14 px
// row of 28 columns is 28 runs of 14 words, copied a word at a time
#define ROW_COPY 7000
// The scroll's spring brakes stiffer when a glide ends, so the list stops
// within about a row of where the key was let go
#define SPRING_BRAKE 0x18000  // 1.5
#define GLIDE_AUTO 0x8000  // glide_key of ui_list_autoscroll's moves (no key's bit)
#define TAG_PILL 0x5520    // the pill's sprite tiles, shared by every list

// The pill's sprites: 32x16 pieces (a left cap, middles, a right cap), its
// rows the top pill_h of their 16
#define PILL_W 32
#define PILL_TILES 24  // three pieces of 4 x 2 tiles
#define OBJ_WIDE 0x4000      // attr0: a wide shape
#define OBJ_SEMI 0x0400      // attr0: semi-transparent (blends as a first target)
#define OBJ_SIZE_32X16 0x8000  // attr1, with OBJ_WIDE

struct list_state {
	struct ui_ring ring;
	u8 tw, ct;  // a row's width and the canvas's height, in tiles
	u8 tiles;   // res handle of the canvas
	u8 pill;    // res handle of the pill's sprite tiles (RES_NONE: no sprite pill)
	u16 first;  // the canvas's first tile number
	u16 pill_tile;  // the pill's first sprite tile
	u8 page;
	u8 sel_ok;  // the rows show the selection at sel_drawn
	u8 plain;   // UI_LIST_PLAIN: nothing shows as selected
	u8 unmarked;  // ui_list_mark(node, 0): the sprite pill is hidden for now
	u8 opaque;  // rows on their paper, the pill drawn under the selected one's text
	u8 queued_a;  // A pressed during a page change: dispatched once it is drawn
	u8 predrawn;  // a page change's rows are all in the cache
	u8 keep_next; // the next slot keep_resting looks at; ring.k: done
	u8 canvas_ok; // the canvas is cleared and the map points at it
	u32 owner;    // its rows' owner in the row cache
	u32 clears;   // cache_clears when its slots were last checked (keep_resting)
	u32 count, rows;
	u32 cursor;
	u32 sel_drawn;  // the item drawn as selected (opaque lists)
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
	const char *page_text;  // ui_list_page_label's text, for label page_label
	u8 page_label, page_labelled;  // page_labelled: one waits for the page
	MotionId motion;
	MotionId pill_motion;
	s32 pill_off;   // the pill's offset from the cursor's row (16.16 px), written by its spring
	s16 pill_y;     // where the pill was last shown, screen px (held while the old page fades)
	u16 slot_row[UI_RING_MAX_SLOTS];
	u16 drawn_row[UI_RING_MAX_SLOTS];  // the row each slot's tiles hold
	u32 dirty;   // slots to draw
	u32 fresh;   // slots given a new row while off screen: drawn in place
	u32 blank;   // slots holding nothing but paper: no clear needed
	u32 stale;   // slots whose row changed since it was drawn (ui_list_refresh)
	u32 picked;  // slots drawn with the pill under their text (opaque lists)
	u32 lo, hi;  // resident rows
	u32 plo, phi;
	u32 row_cycles;  // what rows lately took to draw at most (hidden drawing's budget)
	// Marquee
	u8 mq;
	u8 mq_step;  // draw the next step
	u8 mq_acc;   // sub-pixel progress, in 60ths of a pixel
	u16 mq_wait;
	u16 mq_px;
	u16 mq_width;
	u16 mq_clip;  // the name's width on its row (item_clip)
	u16 mq_row;  // the row whose slot shows a marquee step, UI_ITEM_NONE none
};

// What drawing a row needs, looked up once per draw
struct row_ctx {
	const struct ui_list_model *model;
	void *ctx;
	const struct torii_font *font;
	u32 w, rh;     // a row's size, px
	u32 y;         // a row's text top, from the row's top
	u32 pad;       // the inset from a row's edges
	u32 x;         // where a row's name starts: pad, or after the icon column
	u32 clip;      // the name's width
	int icons;     // UI_LIST_ICONS
	int icon8;     // UI_LIST_ICON8
	u16 flags;     // the config's UI_LIST_*
	u32 paper, ink;
	u32 sel;       // the row drawn with the pill (opaque lists), UI_ITEM_NONE none
};

static const struct ui_list_config *config(u8 node)
{
	return ui_node_spec(node)->data;
}

// The row cache every list shares
static u32 cache_arena[ROW_CACHE_BYTES / 4] EWRAM_BSS ALIGN(4);
static struct row_cache cache EWRAM_BSS;
static u32 cache_clears EWRAM_BSS;  // ui_list_cache_clear calls

static struct row_cache *row_cache(void)
{
	if (!cache.arena)
		row_cache_init(&cache, cache_arena, ROW_CACHE_BYTES / 4);
	return &cache;
}

void ui_list_cache_clear(void)
{
	row_cache_clear(row_cache());
	cache_clears++;
}

// A row's draw took cycles: the budget's estimate of the next is the
// largest lately (decaying), so a long name after short ones doesn't
// overrun it
static void note_row_cycles(struct list_state *st, u32 cycles)
{
	u32 decayed = st->row_cycles - st->row_cycles / 8;
	st->row_cycles = cycles > decayed ? cycles : decayed;
}

// The cached pixels of row r, if any; never for a row drawn with the pill
static const u32 *cached_row(const struct list_state *st, u32 r, u32 sel)
{
	if (st->opaque && r == sel)
		return 0;
	return row_cache_find(row_cache(), st->owner, r, st->tw, st->ring.rh);
}

static int marquee(u8 node)
{
	return (config(node)->flags & UI_LIST_MARQUEE) != 0;
}

static u32 row_pitch(u8 node)
{
	const struct ui_look *look = &ui_theme()->look;
	u32 rh = config(node)->row_h;
	return rh == UI_ROW_PITCH_LIST       ? look->list_pitch
	       : rh == UI_ROW_PITCH_SETTINGS ? look->settings_pitch
	       : rh                          ? rh
					     : ui_theme()->row_h;
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
	if (!ring_setup(&st->ring, (u32)n->h, row_pitch(node), (c->flags & UI_LIST_FIXED) != 0, &why)) {
		ui_set_error("view %s: list: %s", name, why);
		return 0;
	}
	st->tw = (u8)(n->w / 8);
	st->ct = (u8)(st->ring.period / 8);
	st->opaque = ui_paper(node, UI_ROLE_NONE) != UI_ROLE_NONE || (c->flags & UI_LIST_DRAWN_PILL);
	st->plain = (c->flags & UI_LIST_PLAIN) != 0;
	return 1;
}

static int list_measure(u8 node, struct engine_budget *need)
{
	struct list_state *st = ui_node_state(node);
	if (!geometry(node, st))
		return 0;
	// B9's contract for layouts: a list's plane holds nothing else (its
	// scroll moves the whole plane), and a list over a card shows whole
	// rows (no cut row peeks out at the viewport's edge)
	const struct ui_view *v = ui_node_view(node);
	for (u32 i = 0; i < v->count; i++)
		TLOG_ASSERT(v->first + i == node ||
			    ui_node_spec((u8)(v->first + i))->plane != ui_node_spec(node)->plane);
	TLOG_ASSERT(st->opaque || st->plain || (config(node)->flags & UI_LIST_FIXED) || st->ring.h % st->ring.rh == 0);
	// Bank 1 has no tint for a drawn pill: a consoles list's rows lie over a card
	TLOG_ASSERT(!(st->opaque && (config(node)->flags & UI_LIST_CONSOLES)));
	need->bg_tiles = (u16)(need->bg_tiles + st->tw * st->ct);
	// Its plane's map is two screenblocks (UI_RING_MAP_LINES): one more
	need->bg_maps++;
	ui_node_view(node)->tall |= (u8)(1u << ui_node_spec(node)->plane);
	if (!st->opaque && !st->plain)
		need->obj_tiles = (u16)(need->obj_tiles + PILL_TILES);
	return 1;
}

static u32 *tile_addr(u32 plane, u32 tile)
{
	return (u32 *)(0x06000000 + ui_plane_cb(plane) * 0x4000 + tile * 32u);
}

// The canvas as a text window
static struct text_window canvas(u8 node, const struct list_state *st)
{
	struct text_window w = {tile_addr(ui_node_spec(node)->plane, st->first), st->tw, st->ct};
	return w;
}

// The first word of slot s's lines in the canvas (column 0)
static u32 *slot_words(u8 node, const struct list_state *st, u32 s)
{
	return tile_addr(ui_node_spec(node)->plane, st->first) + ring_slot_line(&st->ring, s);
}

static u32 model_count(u8 node)
{
	u32 n = config(node)->model->count(ui_node_view(node)->ctx);
	if (config(node)->flags & UI_LIST_FIXED) {
		// Content within the viewport never scrolls, so each row keeps a slot
		const struct list_state *st = ui_node_state(node);
		u32 whole = st->ring.h / st->ring.rh;
		if (n > whole)
			n = whole;
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
		st->keep_next = 0;  // once it rests, keep its rows again
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
			m->prefetch(ui_node_view(node)->ctx, plo, phi < st->count ? phi : st->count - 1);
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

// Forgets which rows the slots hold, for a page drawn while hidden: slots
// that held anything are cleared when they are drawn
static void forget_slots(struct list_state *st)
{
	for (u32 s = 0; s < UI_RING_MAX_SLOTS; s++)
		st->slot_row[s] = st->drawn_row[s] = UI_ITEM_NONE;
	st->lo = 1;
	st->hi = 0;
	st->plo = st->phi = 0xFFFFFFFF;
	u32 all = st->ring.k >= 32 ? 0xFFFFFFFFu : (1u << st->ring.k) - 1;
	st->dirty = all & ~st->blank;  // cleared (drawn as nothing) unless blank
	st->fresh = st->dirty;
	st->stale = st->picked = 0;
	st->mq_row = UI_ITEM_NONE;
	st->keep_next = 0;
}

static s32 goal_px(const struct list_state *st)
{
	return st->base + FIX_ROUND(motion_goal(st->motion));
}

// key if its repeat moved the cursor from old to cursor this frame, in its
// own direction: the scroll then glides (0: a press, a move from code, a
// wrap)
static u16 glide_key(u16 key, u32 old, u32 cursor)
{
	const InputState *in = ui_input();
	if (!(key & in->fire & ~in->pressed & in->repeating))
		return 0;
	u16 down = INPUT_DOWN | INPUT_RIGHT | INPUT_R, up = INPUT_UP | INPUT_LEFT | INPUT_L;
	return cursor > old ? key & down : cursor < old ? key & up : 0;
}

// One frame of a glide: the spring's goal runs with the cursor's speed; once
// the key is let go (or another direction restarts it), it brakes
static void glide(struct list_state *st)
{
	const InputState *in = ui_input();
	u16 key = st->glide_key;
	u32 rate, phase, row = st->cursor;
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

// The resident slot of row r, or -1
static int resident_slot(const struct list_state *st, u32 r)
{
	if (r == UI_ITEM_NONE || r < st->lo || r > st->hi)
		return -1;
	u32 s = ring_slot(&st->ring, r);
	return st->slot_row[s] == r ? (int)s : -1;
}

// Redraws row r's slot (if it holds r) as the model has it now
static void redraw_row(struct list_state *st, u32 r)
{
	int s = resident_slot(st, r);
	if (s >= 0) {
		st->dirty |= 1u << s;
		st->stale |= 1u << s;
	}
}

static void marquee_reset(u8 node, struct list_state *st)
{
	st->mq = marquee(node) ? MQ_WAIT : MQ_OFF;
	st->mq_wait = ui_theme()->marquee_delay;
	st->mq_px = 0;
	st->mq_acc = 0;
	st->mq_step = 0;
	if (st->mq_row != UI_ITEM_NONE) {
		redraw_row(st, st->mq_row);  // its slot shows a step: back to the name's start
		st->mq_row = UI_ITEM_NONE;
	}
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
		motion_spring_jump(st->pill_motion, 0);
		forget_slots(st);
		st->page_cursor = st->cursor;
		assign(node, st, 0);
		ui_dirty(node, 1);
		return;
	}
	// The old page fades out first, unless it can't: then it goes at once
	u32 plane = ui_node_spec(node)->plane;
	st->page_target = target;
	st->predrawn = 0;
	if (ui_plane_fade_out(plane)) {
		st->page = PAGE_FADING;
	} else {
		st->page = PAGE_HIDING;
		ui_plane_hide(plane, 1);
	}
	ui_dirty(node, 1);
}

// The scroll that shows cursor: the row with a row of context (UI_LIST_SCROLL:
// the row at the top)
static s32 scroll_for(u8 node, const struct list_state *st, u32 cursor, s32 from)
{
	if (config(node)->flags & UI_LIST_SCROLL) {
		s32 to = (s32)(cursor * st->ring.rh), max = ring_max_scroll(&st->ring, st->rows);
		return to > max ? max : to;
	}
	return ring_scroll_for(&st->ring, cursor, from, st->rows, 1);
}

// Where the cursor can go: a row of the list (UI_LIST_SCROLL: no further
// than the top row of the last scroll)
static u32 cursor_limit(u8 node, const struct list_state *st, u32 cursor)
{
	if (cursor >= st->count)
		cursor = st->count ? st->count - 1 : 0;
	if (config(node)->flags & UI_LIST_SCROLL) {
		u32 rh = st->ring.rh, last = ((u32)ring_max_scroll(&st->ring, st->rows) + rh - 1) / rh;
		cursor = cursor > last ? last : cursor;
	}
	return cursor;
}

static void set_cursor(u8 node, struct list_state *st, u32 cursor, u16 key)
{
	cursor = cursor_limit(node, st, cursor);
	u32 old = st->cursor;
	st->cursor = cursor;
	marquee_reset(node, st);
	st->sel_ok = st->sel_ok && st->sel_drawn == cursor;
	// A repeat glides on; a press or a move from code springs
	s32 step = (s32)cursor - (s32)old;
	st->glide_key = st->page != PAGE_NONE || !step ? 0 : st->auto_move ? GLIDE_AUTO : glide_key(key, old, cursor);
	st->glide_step = (s16)step;
	st->glide_at = ui_input()->now;
	st->braking = 0;
	motion_spring_omega(st->motion, SPRING_SCROLL);  // after a glide's brake
	// During a page change, from where its page will be
	s32 from = st->page != PAGE_NONE ? st->page_target : st->target;
	s32 target = scroll_for(node, st, cursor, from);
	if (st->page != PAGE_NONE) {
		st->page_target = target;
		st->predrawn = 0;
	} else if (ring_far(&st->ring, st->shown, target)) {
		page_change(node, st, target);
	} else {
		st->target = target;
		if (!st->glide_key)
			motion_spring_target(st->motion, FIX(target - st->base));
		// The pill slides from the old row to the new (within a viewport),
		// unless the theme's pill_spring is 0: then it jumps
		if (ui_theme()->motion.pill_spring) {
			s32 off = st->pill_off + FIX(((s32)old - (s32)cursor) * st->ring.rh), most = FIX(st->ring.h);
			off = off > most ? most : off < -most ? -most : off;
			motion_spring_shift(st->pill_motion, off - st->pill_off);
			motion_spring_target(st->pill_motion, 0);
		}
	}
	ui_dirty(node, 1);
}

// The pill's sprite tiles: three 32x16 pieces (left cap, middle, right
// cap) in 1D order, their top pill_h rows in the tint, the caps rounded
static void make_pill(u32 *tiles)
{
	const struct ui_look *look = &ui_theme()->look;
	u32 h = look->pill_h < 16 ? look->pill_h : 16;
	for (u32 piece = 0; piece < 3; piece++) {
		for (u32 t = 0; t < 8; t++) {
			u32 tx = t & 3, ty = t >> 2;
			for (u32 y = 0; y < 8; y++) {
				u32 py = ty * 8 + y, row = 0;
				u32 e = py < h ? (py < h - 1 - py ? py : h - 1 - py) : 0;
				u32 inset = py < h ? ui_round_inset(look->pill_radius, e) : 0;
				for (u32 x = 0; x < 8; x++) {
					u32 px = tx * 8 + x;
					int on = py < h && (piece != 0 || px >= inset) &&
						 (piece != 2 || px + inset < PILL_W);
					row |= (on ? (u32)UI_ROLE_TINT : 0u) << (4 * x);
				}
				tiles[(piece * 8 + t) * 8 + y] = row;
			}
		}
	}
}

static int list_init(u8 node)
{
	struct list_state *st = ui_node_state(node);
	if (!geometry(node, st))
		return 0;
	st->tiles = ui_tiles(node, (u32)st->tw * st->ct, 0, &st->first);
	if (st->tiles == RES_NONE)
		return 0;
	u8 owner = ui_owner(ui_node_view(node));
	st->motion = motion_spring(MOTION_TO_S32(&st->spring), 0, SPRING_SCROLL, owner);
	if (st->motion == MOTION_NONE) {
		ui_set_error("view %s: no motion left for a list's scroll", ui_node_view(node)->spec->name);
		return 0;
	}
	st->pill = RES_NONE;
	if (!st->opaque && !st->plain) {
		// The sprite pill: its tiles (shared by every list, drawn once:
		// no sprite shows them yet) and its slide
		st->pill = res_obj_tiles(PILL_TILES, 0, TAG_PILL);
		if (st->pill == RES_NONE)
			return 0;
		st->pill_tile = res_start(RES_OBJ_VRAM, st->pill);
		if (res_fresh(RES_OBJ_VRAM))
			make_pill(res_obj_addr(st->pill));
		s32 omega = ui_theme()->motion.pill_spring;
		st->pill_motion =
			motion_spring(MOTION_TO_S32(&st->pill_off), 0, omega ? omega : SPRING_OMEGA_MAX, owner);
		if (st->pill_motion == MOTION_NONE) {
			ui_set_error("view %s: no motion left for a list's pill", ui_node_view(node)->spec->name);
			return 0;
		}
		ui_view_uses_objs(ui_node_view(node), ui_node_spec(node)->plane);
	}
	st->mq_row = UI_ITEM_NONE;
	ui_list_reset(node, 0);
	return 1;
}

// A new model (ui_list_reset), or rows added or removed (ui_list_recount):
// the scroll for cursor is reckoned from from
static void reset(u8 node, u32 cursor, s32 from)
{
	struct list_state *st = ui_node_state(node);
	st->owner = row_cache_owner(row_cache());  // a new model: no row drawn yet
	st->clears = cache_clears;
	st->count = model_count(node);
	st->rows = st->count;
	if (cursor >= st->count)
		cursor = st->count ? st->count - 1 : 0;
	st->cursor = cursor;
	st->sel_ok = 0;
	marquee_reset(node, st);
	s32 target = scroll_for(node, st, cursor, from);
	if (ui_node_shown(node)) {
		forget_slots(st);
		st->page = PAGE_NONE;
	}
	page_change(node, st, target);
}

void ui_list_reset(u8 node, u32 cursor)
{
	reset(node, cursor, 0);
}

void ui_list_recount(u8 node, u32 cursor)
{
	const struct list_state *st = ui_node_state(node);
	reset(node, cursor, st->page != PAGE_NONE ? st->page_target : st->target);
}

void ui_list_refresh(u8 node, u32 lo, u32 hi)
{
	struct list_state *st = ui_node_state(node);
	row_cache_forget(row_cache(), st->owner, lo, hi);
	st->predrawn = 0;
	st->keep_next = 0;
	for (u32 s = 0; s < st->ring.k; s++) {
		u32 r = st->slot_row[s];
		if (r != UI_ITEM_NONE && r >= lo && r <= hi)
			st->dirty |= 1u << s;
		if (st->drawn_row[s] != UI_ITEM_NONE && st->drawn_row[s] >= lo && st->drawn_row[s] <= hi)
			st->stale |= 1u << s;
	}
	if (st->mq_row != UI_ITEM_NONE && st->cursor >= lo && st->cursor <= hi)
		marquee_reset(node, st);
	ui_dirty(node, 1);
}

void ui_list_set_cursor(u8 node, u32 cursor)
{
	struct list_state *st = ui_node_state(node);
	if (cursor != st->cursor)
		set_cursor(node, st, cursor, 0);
}

void ui_list_move(u8 node, u32 cursor, u16 key)
{
	struct list_state *st = ui_node_state(node);
	if (cursor != st->cursor)
		set_cursor(node, st, cursor, key);
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
// phase passes a whole one, and the view hears of it as of a key's move; at
// the list's end it stops
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
	if (st->auto_rate > 0 ? st->cursor + 1 >= st->count : !st->cursor) {
		st->auto_rate = 0;  // the end: glide() brakes
		return;
	}
	st->auto_move = 1;
	set_cursor(node, st, st->auto_rate > 0 ? st->cursor + 1 : st->cursor - 1, 0);
	st->auto_move = 0;
	// In the update phase: pops the handler asks for wait until it ends
	u8 was = ui_dispatching;
	ui_dispatching = 1;
	ui_emit(node, UI_EV_CURSOR, st->cursor);
	ui_dispatching = was;
}

void ui_list_mark(u8 node, int shown)
{
	struct list_state *st = ui_node_state(node);
	st->unmarked = !shown;
	// The row's name scrolls only while it shows as chosen: back to its start
	marquee_reset(node, st);
	if (st->unmarked)
		st->mq = MQ_OFF;
	ui_wake();  // the pill's sprites change with the next update
}

u32 ui_list_cursor(u8 node)
{
	return ((const struct list_state *)ui_node_state(node))->cursor;
}

int ui_list_paging(u8 node)
{
	return ((const struct list_state *)ui_node_state(node))->page != PAGE_NONE;
}

// A page's label: a label's text, or a title bar's right-hand text
static void page_text_set(u8 label, const char *text)
{
	if (ui_node_spec(label)->kind == UI_TITLE)
		ui_title_set_right(label, text);
	else
		ui_label_set(label, text);
}

void ui_list_page_label(u8 node, u8 label, const char *text)
{
	struct list_state *st = ui_node_state(node);
	if (st->page != PAGE_FADING && st->page != PAGE_HIDING) {
		page_text_set(label, text);
		return;
	}
	st->page_label = label;
	st->page_text = text;
	st->page_labelled = 1;
}

int ui_list_still(u8 node)
{
	const struct list_state *st = ui_node_state(node);
	return st->page == PAGE_NONE && !motion_running(st->motion) && st->shown == st->target &&
	       !motion_running(st->pill_motion);
}

void ui_list_scroll_info(u8 node, u32 *content, u32 *view, u32 *scroll)
{
	const struct list_state *st = ui_node_state(node);
	*content = st->rows * st->ring.rh;
	*view = st->ring.h;
	*scroll = st->shown > 0 ? (u32)st->shown : 0;
}

void ui_list_cursor_row(u8 node, s32 *y, u32 *h)
{
	const struct list_state *st = ui_node_state(node);
	s32 rest = st->page == PAGE_NONE ? st->target : st->page_target;
	*y = ui_node(node)->y + (s32)(st->cursor * st->ring.rh) - rest;
	*h = st->ring.rh;
}

static int list_event(u8 node, const struct ui_event *e)
{
	struct list_state *st = ui_node_state(node);
	if (e->type != UI_EV_KEYS)
		return 0;
	const InputState *in = ui_input();
	s32 page = (s32)(st->ring.h / st->ring.rh);
	if (e->keys & INPUT_A) {
		// Moves during a page change only move the page it draws (set_cursor);
		// A waits until that page is up
		if (st->page != PAGE_NONE)
			st->queued_a = 1;
		else if (st->count)
			ui_emit(node, UI_EV_ACTIVATE, st->cursor);
		return 1;
	}
	static const u16 keys[] = {INPUT_UP, INPUT_DOWN, INPUT_L, INPUT_R};
	for (u32 i = 0; i < 4; i++) {
		u16 key = keys[i];
		if (!(e->keys & key))
			continue;
		s32 delta = key == INPUT_UP || key == INPUT_DOWN ? 1 : page;
		if (key == INPUT_UP || key == INPUT_L)
			delta = -delta;
		if (!st->count)
			continue;
		u32 to;
		if (config(node)->flags & UI_LIST_WRAP) {
			to = input_move(in, key, st->cursor, st->count, delta);
		} else {
			s32 t = (s32)st->cursor + delta;
			to = t < 0 ? 0 : (u32)t >= st->count ? st->count - 1 : (u32)t;
		}
		// A reading view already at its last scroll doesn't move, and its
		// view hears of no move
		to = cursor_limit(node, st, to);
		if (to != st->cursor) {
			set_cursor(node, st, to, key);
			ui_emit(node, UI_EV_CURSOR, to);
		}
		return 1;
	}
	return 0;
}

// The text inset of a row: the config's, or the theme's
static u32 row_pad(u8 node)
{
	u32 pad = config(node)->pad;
	return pad ? pad : ui_theme()->pad;
}

// Where a list row's name starts: after the icon or mark column, if it has
// one
static u32 text_x(u8 node)
{
	u32 pad = row_pad(node), flags = config(node)->flags;
	if (flags & UI_LIST_ICON8)
		return pad + UI_LIST_ICON8_W + ui_theme()->look.section_icon_gap;
	if (flags & UI_LIST_ICONS)
		return UI_LIST_ICON_W + pad / 2;
	return flags & UI_LIST_MARKS ? pad + 8u + ui_theme()->look.row_mark_gap : pad;
}

// Pixels a list row's name may use
static u32 text_clip(u8 node, const struct list_state *st)
{
	u32 w = st->tw * 8u, used = text_x(node) + row_pad(node) + config(node)->detail_w;
	return w > used + 8 ? w - used : 8;
}

// Draws a 16x16 icon (2x2 tiles in row order) at x 0 and line y of a
// window, over what is there: colour 0 lets it show
static void row_icon(const struct text_window *win, u32 y, const u32 *icon)
{
	u32 column = win->height * 8u;
	for (u32 t = 0; t < 4; t++) {
		u32 *dst = win->tiles + (t & 1) * column + y + (t >> 1) * 8;
		const u32 *src = &icon[t * 8];
		for (u32 i = 0; i < 8; i++) {
			u32 px = src[i];
			u32 set = (px | px >> 1 | px >> 2 | px >> 3) & 0x11111111u;
			dst[i] = (dst[i] & ~(set * 0xF)) | px;
		}
	}
}

// Draws an 8x8 icon (one 4bpp tile) at pixel (x, y) of a window, over what
// is there: colour 0 lets it show
static void row_icon8(const struct text_window *win, u32 x, u32 y, const u32 *icon)
{
	u32 column = win->height * 8u, c = x / 8, shift = (x % 8) * 4;
	if (c >= win->width)
		return;
	for (u32 i = 0; i < 8; i++) {
		u32 px = icon[i], set = ((px | px >> 1 | px >> 2 | px >> 3) & 0x11111111u) * 0xF;
		u32 *dst = win->tiles + c * column + y + i;
		*dst = (*dst & ~(set << shift)) | px << shift;
		if (shift && c + 1 < win->width) {
			dst += column;
			*dst = (*dst & ~(set >> (32 - shift))) | px >> (32 - shift);
		}
	}
}

// Pixels item's name may use of clip, the row's: less its badge's room
static u32 item_clip(u32 clip, const struct ui_item *item)
{
	u32 badge = item->badge ? 8u + ui_theme()->look.row_badge_gap : 0;
	return clip > badge + 8 ? clip - badge : 8;
}

static void marquee_tick(u8 node, struct list_state *st, u32 dt)
{
	if (st->mq == MQ_OFF || st->unmarked || st->page != PAGE_NONE || st->shown != st->base + FIX_ROUND(st->spring))
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
		st->mq_clip = (u16)item_clip(text_clip(node, st), &item);
		if (st->mq_width <= st->mq_clip) {
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
		if (st->mq_px + st->mq_clip >= (u32)st->mq_width + t->marquee_gap) {
			st->mq = MQ_END;
			st->mq_wait = t->marquee_pause;
		}
		break;
	}
	default:  // MQ_END: back to the start (the plain row), which waits again
		marquee_reset(node, st);
		ui_dirty(node, 2);
		break;
	}
	ui_wake();
}

static int refresh_pending(u8 node, u32 s);

// While the list rests: the next row a slot holds as the model has it now
// goes into the row cache, if it isn't there yet. One a frame, one pass
// over the slots (so rows that don't all fit never take turns).
static void keep_resting(u8 node, struct list_state *st)
{
	u32 rh = st->ring.rh;
	if (st->clears != cache_clears) {
		// The cache was cleared (a theme change): the rows on screen may
		// show the old look, so none goes in until it is drawn again
		st->clears = cache_clears;
		for (u32 s = 0; s < st->ring.k; s++)
			if (st->drawn_row[s] != UI_ITEM_NONE)
				st->stale |= 1u << s;
	}
	while (st->keep_next < st->ring.k) {
		u32 s = st->keep_next++, r = st->drawn_row[s], bit = 1u << s;
		if (r == UI_ITEM_NONE || st->slot_row[s] != r ||
		    ((st->blank | st->stale | st->dirty | st->picked) & bit) || r == st->mq_row ||
		    refresh_pending(node, s) || cached_row(st, r, UI_ITEM_NONE))
			continue;
		u32 *row = row_cache_put(row_cache(), st->owner, r, st->tw, rh);
		if (row)
			row_cache_copy_in(row, slot_words(node, st, s), st->tw, rh, st->ct * 8u);
		return;  // the next one next frame
	}
}

// 1 if the pill shows: a sprite pill on a list that shows its page
static int pill_shown(u8 node, const struct list_state *st)
{
	return st->pill != RES_NONE && st->count && !st->unmarked && ui_node_shown(node) && ui_plane_owned(node) &&
	       (st->page == PAGE_NONE || st->page == PAGE_FADING);
}

// The pill's sprites for this frame (the update, after the scroll's): at
// the cursor's row as the next commit shows it, plus its slide, within the
// viewport; faded with the plane while its page fades in or out
static void pill_objs(u8 node, struct list_state *st)
{
	if (!pill_shown(node, st))
		return;
	const struct ui_node *n = ui_node(node);
	const struct ui_view *v = ui_node_view(node);
	const struct ui_look *look = &ui_theme()->look;
	s32 top = n->y + v->offset, h = st->ring.h, ph = look->pill_h;
	if (st->page == PAGE_NONE) {
		s32 y = top + (s32)(st->cursor * st->ring.rh) - st->shown + ((s32)st->ring.rh - ph) / 2 +
			FIX_ROUND(st->pill_off);
		y = y < top ? top : y > top + h - ph ? top + h - ph : y;
		st->pill_y = (s16)y;
	}
	s32 x0 = n->x + ui_view_dx(v) - look->pill_out_l, w = n->w + look->pill_out_l + look->pill_out_r;
	u16 attr0 =
		(u16)(OBJ_WIDE | (st->pill_y & 0xFF) | (ui_plane_blending(ui_node_spec(node)->plane) ? OBJ_SEMI : 0));
	u32 pieces = (u32)(w + PILL_W - 1) / PILL_W;
	if (pieces < 2)
		pieces = 2;
	for (u32 i = 0; i < pieces; i++) {
		u32 last = i + 1 == pieces;
		s32 x = last ? x0 + w - PILL_W : x0 + (s32)i * PILL_W;
		u32 piece = !i ? 0 : last ? 2 : 1;
		if (!ui_obj_add(node, attr0, (u16)(OBJ_SIZE_32X16 | (x & 0x1FF)),
				(u16)(st->pill_tile + piece * 8 + (ui_obj_bank() << 12))))
			return;
	}
}

// The model's sprites (ui_list_model.objs), while the list shows its page
static void model_objs(u8 node, const struct list_state *st)
{
	const struct ui_list_model *m = config(node)->model;
	if (!m->objs || !st->count || !ui_node_shown(node) || !ui_plane_owned(node) ||
	    (st->page != PAGE_NONE && st->page != PAGE_FADING))
		return;
	const struct ui_node *n = ui_node(node);
	const struct ui_view *v = ui_node_view(node);
	struct ui_list_objs o = {n->x + ui_view_dx(v),
				 n->y + v->offset,
				 st->shown,
				 st->ring.h,
				 st->ring.rh,
				 (u16)(ui_plane_blending(ui_node_spec(node)->plane) ? OBJ_SEMI : 0)};
	m->objs(v->ctx, node, &o);
}

static void list_update(u8 node, u32 dt)
{
	struct list_state *st = ui_node_state(node);
	const struct ui_spec *spec = ui_node_spec(node);
	const struct ui_node *n = ui_node(node);
	const struct ui_view *v = ui_node_view(node);
	if (n->flags & UI_N_HIDDEN)
		return;  // its plane is off (ui_run.c); rows stay as they are
	// The new page's rows are drawn ahead (list_draw) until they all are
	if (st->page != PAGE_NONE && !st->predrawn)
		ui_dirty(node, 1);
	if (st->page == PAGE_FADING) {
		if (ui_plane_fading_out(spec->plane)) {
			ui_wake();
		} else {
			// Faded out: off screen from this frame's regs
			st->page = PAGE_HIDING;
			ui_plane_hide(spec->plane, 1);
		}
	}
	if (st->page == PAGE_HIDING) {
		if (!ui_plane_hidden_now(spec->plane)) {
			ui_wake();
			return;
		}
		// Off screen now: every row can be drawn where it lands. Only the
		// rows on screen (dir 0): the spare ones fill in once it shows
		st->shown = st->base = st->target = st->page_target;
		motion_spring_jump(st->motion, 0);
		motion_spring_jump(st->pill_motion, 0);
		forget_slots(st);
		st->page_cursor = st->cursor;
		assign(node, st, 0);
		st->page = PAGE_DRAW;
		ui_dirty(node, 1);
		// A label that changes with the page draws in this frame too: its
		// tiles queue before the page shows (list_draw holds DISPCNT for them)
		if (st->page_labelled) {
			st->page_labelled = 0;
			page_text_set(st->page_label, st->page_text);
		}
	} else if (st->page == PAGE_NONE) {
		autoscroll(node, st, dt);
		if (st->glide_key)
			glide(st);
		s32 want = st->base + FIX_ROUND(st->spring), max = ring_max_scroll(&st->ring, st->rows);
		st->braking = st->braking && motion_running(st->motion);
		if (st->glide_key || st->braking)
			want = ring_keep_on_screen(&st->ring, st->cursor, want);
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
		if (st->shown != want || motion_running(st->pill_motion)) {
			ui_wake();
		} else if (still && !st->dirty && ui_node_shown(node)) {
			fill_spare(st);
			// Every row drawn: they go into the row cache
			if (!st->dirty)
				keep_resting(node, st);
		}
	}
	// Not while a view above shows its own plane there
	if (ui_plane_owned(node))
		gfx_set_scroll(spec->plane, (u16)(-n->x), (u16)ring_vofs(&st->ring, st->shown, n->y + v->offset));
	st->set_offset[0] = st->set_offset[1];
	st->set_offset[1] = v->offset;
	if (st->dirty || (!st->sel_ok && st->opaque && !st->plain))
		ui_dirty(node, 1);
	marquee_tick(node, st, dt);
	pill_objs(node, st);
	model_objs(node, st);
}

static void row_ctx_init(u8 node, const struct list_state *st, struct row_ctx *rc, u32 sel)
{
	const struct ui_theme *t = ui_theme();
	rc->model = config(node)->model;
	rc->ctx = ui_node_view(node)->ctx;
	rc->font = t->body;
	rc->w = st->tw * 8u;
	rc->rh = st->ring.rh;
	rc->y = rc->rh > rc->font->line_height ? (rc->rh - rc->font->line_height) / 2 : 0;
	rc->pad = row_pad(node);
	rc->x = text_x(node);
	rc->icons = (config(node)->flags & UI_LIST_ICONS) != 0;
	rc->icon8 = (config(node)->flags & UI_LIST_ICON8) != 0;
	rc->flags = config(node)->flags;
	rc->clip = text_clip(node, st);
	rc->paper = ui_paper(node, UI_ROLE_NONE);
	rc->ink = ui_ink(node, UI_ROLE_TEXT);
	rc->sel = st->opaque && !st->plain ? sel : UI_ITEM_NONE;
}

// The detail, right-aligned in the row, its window starting x0 px left of
// the row (line is scratch); returns where its text starts
static u32 draw_detail(const struct text_window *win, const struct row_ctx *rc, u32 x0, u32 y, const char *detail,
		       struct text_line *line)
{
	text_layout(rc->font, detail, strlen(detail), rc->w, line);
	if (line->truncated || line->advance + rc->pad >= rc->w)
		return x0 + rc->w - rc->pad;
	u32 x = x0 + rc->w + 1 - rc->pad - line->advance;
	text_draw_line(win, rc->font, x, y, UI_ROLE_DIM, line);
	return x;
}

// 1 if canvas lines of slot s show on screen now, as the last commit left
// the list's plane (its scroll, and whether it is on)
static int slot_live(u8 node, const struct list_state *st, u32 s)
{
	const struct ui_spec *spec = ui_node_spec(node);
	if (!(gfx_live_dispcnt & (0x100u << spec->plane)) || (gfx_live_dispcnt & 0x80))
		return 0;
	const struct ui_node *n = ui_node(node);
	// The scroll the last commit wrote went out with last frame's offset (a
	// dialog sliding in moves it); another view may own the plane
	if (!ui_plane_owned(node) || st->set_offset[0] != st->set_offset[1])
		return 0;
	u32 bg = (u32)(gfx_live_bgofs[spec->plane * 2 + 1] + n->y + st->set_offset[0]) & (UI_RING_MAP_LINES - 1);
	return ring_lines_show(&st->ring, bg % st->ring.period, ring_slot_line(&st->ring, s), st->ring.rh);
}

// The pill drawn under a row's text (opaque lists), the row's width from x0,
// only its columns in [cx0, cx1)
static void draw_pill(const struct text_window *win, const struct row_ctx *rc, u32 x0, u32 y0, u32 cx0, u32 cx1)
{
	const struct ui_look *look = &ui_theme()->look;
	u32 h = look->pill_h < rc->rh ? look->pill_h : rc->rh, top = y0 + (rc->rh - h) / 2;
	for (u32 i = 0; i < h; i++) {
		u32 inset = ui_round_inset(look->pill_radius, i < h - 1 - i ? i : h - 1 - i);
		u32 a = x0 + inset, b = x0 + rc->w - inset;
		a = a < cx0 ? cx0 : a;
		b = b > cx1 ? cx1 : b;
		if (a < b)
			text_clear(win, a, top + i, b - a, 1, UI_ROLE_TINT);
	}
}

// Draws row r at line y0 of a window, the row starting at x0: 0, or a
// marquee step's margin, which draws the name from px skip of it on (and
// not cut with "..."); cleared first unless blank (it holds only paper). 1
// if anything but paper was drawn.
static int draw_row(const struct list_state *st, const struct row_ctx *rc, const struct text_window *win, u32 x0,
		    u32 y0, u32 r, int blank, u32 skip)
{
	const struct torii_font *font = rc->font;
	struct text_line line;
	u32 ww = win->width * 8u;
	if (!blank)
		text_clear(win, 0, y0, ww, rc->rh, rc->paper);
	int picked = r != UI_ITEM_NONE && r == rc->sel;
	if (rc->model->paint) {
		// The model draws the row whole, the selection too
		if (r >= st->count)
			return 0;
		struct ui_paint p = {win, x0, y0, rc->w, rc->rh, r, (u8)picked};
		return rc->model->paint(rc->ctx, &p) || picked;
	}
	if (picked)
		draw_pill(win, rc, x0, y0, 0, ww);
	struct ui_item item = {0};
	if (r >= st->count || !rc->model->fetch(rc->ctx, r, &item) || !item.text)
		return picked;  // drawn when ui_list_refresh says it loaded
	u32 len = item.len ? item.len : strlen(item.text);
	u32 ink = item.ink ? item.ink : rc->ink, y = y0 + rc->y, tx = x0 + rc->x, clip = item_clip(rc->clip, &item);
	const struct ui_look *look = &ui_theme()->look;
	u32 icon_y = y + (font->line_height > 8 ? (font->line_height - 8) / 2 : 0), badge_x;
	if (item.key) {
		// A reading row: its key's chip, the text at the key column
		if (item.key_len) {
			char key[16];
			u32 n = item.key_len < sizeof(key) - 1 ? item.key_len : sizeof(key) - 1;
			memcpy(key, item.key, n);
			key[n] = 0;
			u32 top = rc->rh > look->hint_chip_h ? (rc->rh - look->hint_chip_h) / 2 : 0;
			ui_key_chip(win, tx, y0 + top, key, 0, 1);
		}
		tx += look->reading_key_w;
		clip = clip > look->reading_key_w + 8u ? clip - look->reading_key_w : 8u;
	}
	if (!x0) {
		text_layout(font, item.text, len, clip, &line);
		text_draw_line(win, font, tx, y, ink, &line);
		badge_x = tx + line.advance;
	} else {
		// A marquee step: glyphs partly in the name's span drawn whole,
		// into the margins and the strips beside the name, which are then
		// cleared back to what is under the name, so glyphs slide in and
		// out at both edges
		s32 lead;
		u32 left = x0 + rc->x, right = left + clip;
		badge_x = right;
		text_layout_span(font, item.text, len, skip, clip, &line, &lead);
		text_draw_line(win, font, (u32)((s32)left + lead), y, ink, &line);
		text_clear(win, 0, y0, left, rc->rh, rc->paper);
		text_clear(win, right, y0, ww - right, rc->rh, rc->paper);
		if (picked) {
			draw_pill(win, rc, x0, y0, 0, left);
			draw_pill(win, rc, x0, y0, right, ww);
		}
	}
	if (item.icon && rc->icon8 && rc->rh >= 8)
		row_icon8(win, x0 + rc->pad, y0 + (rc->rh - 8) / 2, item.icon);
	if (item.icon && rc->icons && rc->rh >= 16) {
		struct text_window at = *win;
		at.tiles += (x0 / 8) * win->height * 8u;
		row_icon(&at, y0 + (rc->rh - 16) / 2, item.icon);
	}
	// The 8x8 icons, after the marquee's clears: the mark in its column, the
	// badge after the name, the dot left of the detail
	if (item.mark && (rc->flags & UI_LIST_MARKS))
		ui_icon8(win, x0 + rc->pad, icon_y, item.mark - 1u, item.mark_ink ? item.mark_ink : UI_ROLE_ACCENT_INK);
	if (item.badge)
		ui_icon8(win, badge_x + look->row_badge_gap, icon_y, item.badge - 1u,
			 item.badge_ink ? item.badge_ink : UI_ROLE_ACCENT_INK);
	u32 detail_x = x0 + rc->w - rc->pad;
	if (item.detail)
		detail_x = draw_detail(win, rc, x0, y, item.detail, &line) - look->row_dot_gap;
	if (item.dot)
		ui_icon8(win, detail_x - 8u, icon_y, item.dot - 1u, item.dot_ink ? item.dot_ink : UI_ROLE_DIM);
	return 1;
}

// Puts row r into slot s's lines in the canvas: copied from the row cache
// (cached: row_cache_find's answer), or drawn, cleared first unless blank.
static void render_slot(u8 node, struct list_state *st, const struct row_ctx *rc, u32 s, u32 r, const u32 *cached)
{
	u32 bit = 1u << s;
	if (cached) {
		row_cache_copy_out(cached, slot_words(node, st, s), st->tw, st->ring.rh, st->ct * 8u);
		st->blank &= ~bit;
		st->picked &= ~bit;
		return;
	}
	u32 start = cycles_now();
	struct text_window win = canvas(node, st);
	int drew = draw_row(st, rc, &win, 0, ring_slot_line(&st->ring, s), r, (st->blank & bit) != 0, 0);
	if (r != UI_ITEM_NONE)
		note_row_cycles(st, cycles_since(start));
	st->blank = drew ? st->blank & ~bit : st->blank | bit;
	st->picked = r != UI_ITEM_NONE && r == rc->sel ? st->picked | bit : st->picked & ~bit;
}

// Rows drawn ahead into the row cache (a page change's) whose height isn't
// whole tiles are drawn here first: the blitter takes whole tile columns,
// the cache keeps rh lines a column
#define SCRATCH_WORDS (30 * 3 * 8)  // a full-width row up to 24 px
static u32 row_scratch[SCRATCH_WORDS] EWRAM_BSS ALIGN(4);

// Draws row r (unselected) into the cache's row; 0 if nothing but paper
// was drawn (not loaded yet) or it can't
static int draw_cached(const struct list_state *st, const struct row_ctx *rc, u32 *row, u32 r)
{
	u32 rh = st->ring.rh, rt = (rh + 7) / 8;
	if (!(rh & 7)) {
		struct text_window win = {row, st->tw, (u8)rt};
		return draw_row(st, rc, &win, 0, 0, r, 0, 0);
	}
	if ((u32)st->tw * rt * 8 > SCRATCH_WORDS)
		return 0;
	struct text_window win = {row_scratch, st->tw, (u8)rt};
	int drew = draw_row(st, rc, &win, 0, 0, r, 0, 0);
	row_cache_copy_in(row, row_scratch, st->tw, rh, rt * 8);
	return drew;
}

// While a page change fades the old page out and hides it, the new page's
// rows are drawn into the row cache, within the budget, so its first hidden
// frame only copies them in. Not when the page doesn't fit in the cache.
static void predraw(u8 node, struct list_state *st, const struct row_ctx *rc)
{
	u32 lo, hi, rh = st->ring.rh;
	ring_resident(&st->ring, st->page_target, 0, st->rows, &lo, &hi);
	// Rows never straddle the arena's end, so up to a row's words less fit
	u32 words = st->tw * rh;
	if (lo > hi || (hi - lo + 1) * words > ROW_CACHE_BYTES / 4 - words + 1) {
		st->predrawn = 1;
		return;
	}
	struct row_ctx plain = *rc;
	plain.sel = UI_ITEM_NONE;  // rows drawn with a pill are never cached
	for (u32 r = lo; r <= hi; r++) {
		if ((st->opaque && !st->plain && r == st->cursor) || cached_row(st, r, UI_ITEM_NONE))
			continue;
		if (!ui_draw_room(st->row_cycles ? st->row_cycles : ROW_GUESS, UI_PAGE_BUDGET)) {
			ui_dirty(node, 1);  // the rest next frame
			return;
		}
		u32 *row = row_cache_put(row_cache(), st->owner, r, st->tw, rh);
		if (!row)
			break;
		u32 start = cycles_now();
		if (!draw_cached(st, &plain, row, r))
			row_cache_forget(row_cache(), st->owner, r, r);  // not loaded yet
		note_row_cycles(st, cycles_since(start));
	}
	st->predrawn = 1;
}

// A row redrawn in its slot while the list shows (ui_list_refresh, the
// drawn pill, a marquee step) is drawn here, then copied over the slot's
// lines in VBlank with the frame's other changes (a strided copy, one run
// a tile column). The pool holds two full-width 16 px rows, or a marquee
// step of a 32 px row, for every list; rows past it wait a frame, and it
// empties once its copies have landed.
#define REFRESH_WORDS 1280
#define REFRESH_ROWS 4
static u32 refresh_tiles[REFRESH_WORDS] EWRAM_BSS ALIGN(4);
static u32 refresh_ticket EWRAM_BSS;  // the last copy queued
static u16 refresh_used EWRAM_BSS;    // words of the pool queued
static u8 refresh_count EWRAM_BSS;
static u8 refresh_owner[REFRESH_ROWS] EWRAM_BSS;  // a queued row's node + 1
static u8 refresh_slot[REFRESH_ROWS] EWRAM_BSS;

// Empties the pool once its copies have landed
static void refresh_reap(void)
{
	if (refresh_count && gfx_queue_done(refresh_ticket))
		refresh_count = refresh_used = 0;
}

// 1 while a copy queued for node's slot s hasn't landed
static int refresh_pending(u8 node, u32 s)
{
	refresh_reap();
	for (u32 i = 0; i < refresh_count; i++)
		if (refresh_owner[i] == node + 1u && refresh_slot[i] == s)
			return 1;
	return 0;
}

// Draws row r (or, marq, the marquee's step of it) into the pool and
// queues it over slot s; 0 to try again next frame (the pool or the queue
// full)
static int refresh_row(u8 node, struct list_state *st, const struct row_ctx *rc, u32 s, u32 r, int marq)
{
	refresh_reap();
	u32 rh = st->ring.rh, rt = (rh + 7) / 8, margin = marq ? MQ_MARGIN : 0;
	u32 cols = st->tw + 2 * margin, words = cols * rt * 8, bit = 1u << s;
	if (refresh_count == REFRESH_ROWS || refresh_used + words > REFRESH_WORDS ||
	    gfx_queue_len() >= GFX_QUEUE_CAP - 2)
		return 0;
	u32 *tiles = refresh_tiles + refresh_used;
	const u32 *cached = marq || r == UI_ITEM_NONE ? 0 : cached_row(st, r, rc->sel);
	int drew;
	if (cached) {
		row_cache_copy_out(cached, tiles, st->tw, rh, rt * 8);
		drew = 1;
	} else {
		struct text_window win = {tiles, (u8)cols, (u8)rt};
		drew = draw_row(st, rc, &win, margin * 8, 0, r, 0, st->mq_px);
	}
	if (!gfx_queue_copy2d(slot_words(node, st, s), st->ct * 32u, tiles + margin * rt * 8, rt * 32, rh * 4, st->tw))
		return 0;
	st->blank = drew ? st->blank & ~bit : st->blank | bit;
	st->picked = r != UI_ITEM_NONE && r == rc->sel && !cached ? st->picked | bit : st->picked & ~bit;
	st->stale &= ~bit;
	refresh_ticket = gfx_queue_ticket();
	refresh_used = (u16)(refresh_used + words);
	refresh_owner[refresh_count] = (u8)(node + 1);
	refresh_slot[refresh_count++] = (u8)s;
	st->drawn_row[s] = (u16)r;
	if (marq)
		st->mq_row = (u16)r;
	else if (st->mq_row == r)
		st->mq_row = UI_ITEM_NONE;
	return 1;
}

// The first draw (the list not on screen yet): the map pointed at the
// canvas, map row m at canvas tile row m % ct, over all its 64 rows. The
// canvas holds whatever its VRAM held: every slot is drawn or cleared
// (forget_slots marked them all, none blank), so a view drawn again over
// the same VRAM while the screen shows it (gfx_freeze: Diagnostics after
// its unit tests) never shows a cleared canvas
static void canvas_setup(u8 node, struct list_state *st)
{
	// The first period's rows, then copies of them (the list has its plane
	// to itself: whole rows copy)
	u16 *map = ui_map(node), *row = map;  // from column 0: the BG scroll puts it at the node's x
	u32 tw = st->tw, ct = st->ct;
	for (u32 t = 0; t < ct; t++, row += 32) {
		u32 e = ui_entry(st->first + t,
				 (config(node)->flags & UI_LIST_CONSOLES) ? UI_BANK_CONSOLES : UI_BANK_NORMAL);
		for (u32 x = 0; x < tw; x++, e += ct)
			row[x] = (u16)e;
	}
	for (u32 m = ct; m < UI_RING_MAP_LINES / 8; m += ct) {
		u32 rows = UI_RING_MAP_LINES / 8 - m < ct ? UI_RING_MAP_LINES / 8 - m : ct;
		CpuFastSet(map, map + m * 32, rows * 16);  // 16 words a row, a multiple of 8
	}
	st->canvas_ok = 1;
}

static void list_draw(u8 node)
{
	struct list_state *st = ui_node_state(node);
	if (ui_node(node)->flags & UI_N_HIDDEN)
		return;
	// A page's rows show the cursor it started with; one that moved since
	// is drawn once the page is up
	u32 sel = st->page == PAGE_DRAW || !ui_node_shown(node) ? st->page_cursor : st->cursor;
	struct row_ctx rc;
	row_ctx_init(node, st, &rc, sel);
	if (st->page == PAGE_FADING || st->page == PAGE_HIDING) {
		if (!st->predrawn)
			predraw(node, st, &rc);
		return;
	}
	// While nothing of it is on screen (a page change, a view not shown
	// yet) a whole page may take more than a frame: it draws within the
	// budget and goes on next frame
	int hidden = st->page == PAGE_DRAW || !ui_node_shown(node);
	if (!st->canvas_ok) {
		TLOG_ASSERT(!ui_node_shown(node));
		canvas_setup(node, st);
	}
	// Rows given new slots: their lines aren't on screen. Rows redrawn in
	// their slots (ui_list_refresh, the drawn pill) may be: they go through
	// the refresh pool
	u32 keep = 0;
	for (u32 s = 0; s < st->ring.k; s++) {
		u32 bit = 1u << s;
		if (!(st->dirty & bit))
			continue;
		u32 r = st->slot_row[s];
		const u32 *cached = r != UI_ITEM_NONE ? cached_row(st, r, rc.sel) : 0;
		u32 need = cached ? ROW_COPY : st->row_cycles ? st->row_cycles : ROW_GUESS;
		u32 budget = st->page == PAGE_DRAW ? UI_PAGE_BUDGET : UI_DRAW_BUDGET;
		if (hidden && !ui_draw_room(need, budget)) {
			ui_dirty(node, 1);  // the rest next frame
			return;
		}
		int pending = refresh_pending(node, s);
		if (pending || (!hidden && !(st->fresh & bit))) {
			// Its slot's refresh copy hasn't landed, or it is one to make
			if (pending || !refresh_row(node, st, &rc, s, r, 0)) {
				keep |= bit;
				ui_dirty(node, 1);
			}
			continue;
		}
		// The ring's promise (ring.h): a row given a slot is drawn where
		// the screen doesn't show it
		if (!hidden)
			TLOG_ASSERT(!slot_live(node, st, s));
		st->drawn_row[s] = (u16)r;
		st->stale &= ~bit;
		if (r == st->mq_row)
			st->mq_row = UI_ITEM_NONE;
		render_slot(node, st, &rc, s, r, cached);
		st->dirty &= ~bit;
		st->fresh &= ~bit;
	}
	st->dirty = keep;
	st->fresh &= keep;
	if (keep && hidden)
		return;  // a slot waits for its refresh copy: the page goes on next frame
	u32 plane = ui_node_spec(node)->plane;
	if (st->page == PAGE_DRAW) {
		st->page = PAGE_NONE;
		st->sel_drawn = st->page_cursor;
		st->sel_ok = st->page_cursor == st->cursor;
		ui_plane_reveal(plane);
		gfx_hold_for_queue(GFX_DIRTY_DISPCNT);
		if (st->queued_a) {
			st->queued_a = 0;
			ui_queue_keys(INPUT_A);
		}
		if (!st->sel_ok) {
			// Keys moved the cursor while the page drew: scroll on to it
			st->page_cursor = st->cursor;
			s32 target = scroll_for(node, st, st->cursor, st->shown);
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
	// The marquee: its next step over the selected row's slot, once the
	// last one has landed
	if (st->mq_step) {
		u32 r = st->cursor;
		int s = resident_slot(st, r);
		if (s < 0) {
			st->mq_step = 0;
		} else if (!refresh_pending(node, (u32)s) && !(st->dirty & (1u << s))) {
			if (refresh_row(node, st, &rc, (u32)s, r, 1))
				st->mq_step = 0;
			else
				ui_dirty(node, 2);  // try again next frame
		} else {
			ui_dirty(node, 2);
		}
	}
	// The drawn pill (opaque lists): the rows of the old and new selected
	// items are drawn again
	if (!st->sel_ok) {
		if (st->opaque && !st->plain) {
			redraw_row(st, st->sel_drawn);
			redraw_row(st, st->cursor);
			if (st->dirty)
				ui_dirty(node, 1);
		}
		st->sel_drawn = st->cursor;
		st->sel_ok = 1;
	}
}

const struct ui_ops ui_list_ops = {
	sizeof(struct list_state), list_measure, list_init, list_event, list_update, list_draw};
