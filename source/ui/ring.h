// The maths of a list's ring of rows (ui/ui_list.c), hardware-free so
// tests/host runs it.
//
// A list shows rows of rh pixels (8 to 32) in a viewport h pixels tall, on
// a BG of its own. Only k rows have tiles: their slots lie back to back in
// one column-major canvas of tiles, slot s on canvas lines [s * rh, s * rh +
// rh), and content row r draws into slot r % k. The canvas is period = k *
// rh lines tall, a whole number of tile rows (k is rounded up for that), so
// content line y is canvas line y % period, and a row never straddles the
// canvas's end. The list's map never changes: map row m shows canvas tile
// row m % (period / 8), and the BG's vertical scroll puts content line
// scroll at the viewport's top (ring_vofs). Rows at any pitch thus scroll
// by the BG scroll alone; rows share tiles, never a pixel row (a word).
//
// Rows enter one ahead of the scroll, off-screen; a slot is only reused by a
// row k rows away, which isn't on screen. The shown scroll moves at most
// step rows a frame (ring_step; step is 32 / rh, so 24-32 px a frame).
// Before a frame's commit at most visible rows are on screen (visible: the
// most rows partly on screen at once); after it, the rows resident at the
// new scroll reach at most step rows past them, plus one ahead. So at most
// visible + step + 1 distinct rows are on screen or being drawn, and with k
// >= visible + 1 + step slots no row is drawn into a slot on screen. A jump
// further than ring_far allows is a page change, drawn while the list is
// hidden.
#ifndef TORII_UI_RING_H
#define TORII_UI_RING_H

#include <gba_base.h>

#define UI_RING_MAX_SLOTS 20  // slots one list may have (u32 slot masks; 12 px rows in 160 px need 18)
#define UI_RING_MIN_ROW 8     // row heights a list takes, px
#define UI_RING_MAX_ROW 32
// The map a list's plane gets: 32 x 64 tiles, so the lines a viewport shows
// (ring_vofs) never pass its 512 px wrap
#define UI_RING_MAP_LINES 512
#define UI_ITEM_NONE 0xFFFF   // a slot showing no row
#define UI_LIST_MAX 0xFFFF    // rows (and items) a list may have
// The scroll spring is kept within this many pixels of its base (motion
// values must stay within +-16384 units): past it, the base moves.
#define UI_RING_REBASE_PX 2048

struct ui_ring {
	u16 h;          // viewport height, px
	u16 period;     // canvas lines: k * rh, a multiple of 8
	u8 rh;          // row height, px
	u8 visible;     // the most rows partly on screen at once: (h - 1) / rh + 2
	u8 k;           // slots: visible + 1 + step, rounded up so period is whole tiles
	u8 step;        // rows the shown scroll may move a frame: 32 / rh
};

// Sets r up; 0 and a sentence in *why if the geometry can't work (a row
// height outside 8-32, or a viewport too tall for the ring). fixed: a list
// that never scrolls (UI_LIST_FIXED) gets a slot per row the viewport shows.
int ring_setup(struct ui_ring *r, u32 h, u32 rh, int fixed, const char **why);

static inline u32 ring_slot(const struct ui_ring *r, u32 row)
{
	return r->k ? row % r->k : 0;
}

// The canvas line a slot starts on.
static inline u32 ring_slot_line(const struct ui_ring *r, u32 slot)
{
	return slot * r->rh;
}

// The BG's vertical scroll that puts content line scroll at screen line top
// (the viewport's top): the visible lines then lie in [scroll % period,
// scroll % period + h), inside the map (ring_setup keeps period + h within
// UI_RING_MAP_LINES).
static inline u32 ring_vofs(const struct ui_ring *r, s32 scroll, s32 top)
{
	s32 p = (s32)r->period, line = scroll % p;
	if (line < 0)
		line += p;
	return (u32)(line - top) & (UI_RING_MAP_LINES - 1);
}

// 1 if canvas lines [line, line + n) show in a viewport whose top shows
// canvas line top (the lines it shows wrap at the period).
static inline int ring_lines_show(const struct ui_ring *r, u32 top, u32 line, u32 n)
{
	u32 p = r->period, d = (line + p - top % p) % p;
	return d < r->h || d + n > p;
}

// The largest scroll: the last row's bottom at the viewport's bottom.
s32 ring_max_scroll(const struct ui_ring *r, u32 rows);

// The rows with tiles at scroll (content px at the viewport's top): those on
// screen, plus one ahead in direction dir (1 down, -1 up, 0 none). Empty
// (lo > hi) when rows is 0. hi - lo < k.
void ring_resident(const struct ui_ring *r, s32 scroll, int dir, u32 rows, u32 *lo, u32 *hi);

// The scroll that shows row with margin rows of context above and below it
// (fewer if the viewport is small), moving scroll as little as possible.
s32 ring_scroll_for(const struct ui_ring *r, u32 row, s32 scroll, u32 rows, u32 margin);

// One frame towards want from shown, at most step rows (24-32 px).
s32 ring_step(const struct ui_ring *r, s32 shown, s32 want);

// 1 if moving the shown scroll from shown to target needs a page change:
// more than two viewports away (and more than visible + 1 rows), too far to
// scroll to in about ten frames. Nearer, the scroll catches up at ring_step's
// speed: quick taps of a page key never blank the list.
int ring_far(const struct ui_ring *r, s32 shown, s32 target);

// Gliding: while a held key's repeats move the cursor step rows at a time,
// the scroll moves at the cursor's speed between repeats instead of in
// steps. target is the scroll for the cursor's row (ring_scroll_for), phase
// how far the key is towards its next repeat and rate its repeats a frame
// (both 16.16, input_phase and input_rate).
struct ring_glide {
	s32 want;   // the scroll now: phase of the way to the next row's target
	s32 speed;  // its speed, px a frame (16.16; negative going up)
	s32 goal;   // a spring's goal: want plus the lag of a spring of stiffness
		    // omega chasing a goal moving at speed (2 speed / omega), so
		    // the spring runs at want, not behind it; within the scroll's range
};
void ring_glide(const struct ui_ring *r, u32 row, s32 step, s32 target, u32 rows, u32 phase, u32 rate, s32 omega,
		struct ring_glide *g);

// Where a glide that ended with the scroll at at, moving at speed (16.16 px
// a frame), comes to rest: the first scroll in its direction, row-aligned
// as target is, at least speed / omega on (a spring of stiffness omega gets
// there from that speed without overshooting); row stays on screen.
s32 ring_glide_stop(const struct ui_ring *r, u32 row, s32 target, s32 at, s32 speed, s32 omega, u32 rows);

// scroll moved as little as it takes for row to be wholly on screen.
s32 ring_keep_on_screen(const struct ui_ring *r, u32 row, s32 scroll);

// How far (px, a multiple of rh) to move a spring's base so its goal,
// goal_px from the base, stays within UI_RING_REBASE_PX; 0 if it is.
s32 ring_rebase(const struct ui_ring *r, s32 goal_px);

// The rows to prefetch around the resident ones: k more on each side.
void ring_prefetch(const struct ui_ring *r, u32 lo, u32 hi, u32 rows, u32 *plo, u32 *phi);

#endif
