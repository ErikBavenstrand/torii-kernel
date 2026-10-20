// See ring.h.
#include "ring.h"

#include <stdint.h>

int ring_setup(struct ui_ring *r, u32 h, u32 rh, int fixed, const char **why)
{
	*why = 0;
	if (rh < UI_RING_MIN_ROW || rh > UI_RING_MAX_ROW) {
		*why = "a row is 8 to 32 px tall";
		return 0;
	}
	if (h < rh || h > 160 || h % 8) {
		*why = "a list's viewport is a multiple of 8 px, from one row to 160 px";
		return 0;
	}
	r->h = (u16)h;
	r->rh = (u8)rh;
	r->visible = (u8)((h - 1) / rh + 2);
	r->step = (u8)(32 / rh);
	u32 k = fixed ? (h + rh - 1) / rh : r->visible + 1u + r->step;  // fixed: the viewport's rows, a cut one too
	while (k * rh % 8)
		k++;  // whole tile rows: at most 3 more (rh * 4 is a multiple of 8)
	r->k = (u8)k;
	r->period = (u16)(k * rh);
	if (k > UI_RING_MAX_SLOTS || r->period + h > UI_RING_MAP_LINES) {
		*why = "the viewport needs more row slots than a list may have";
		return 0;
	}
	return 1;
}

s32 ring_max_scroll(const struct ui_ring *r, u32 rows)
{
	s32 content = (s32)(rows * r->rh);
	return content > r->h ? content - r->h : 0;
}

void ring_resident(const struct ui_ring *r, s32 scroll, int dir, u32 rows, u32 *lo, u32 *hi)
{
	if (!rows) {
		*lo = 1;
		*hi = 0;
		return;
	}
	if (scroll < 0)
		scroll = 0;
	u32 first = (u32)scroll / r->rh;
	u32 last = ((u32)scroll + r->h - 1) / r->rh;
	if (dir > 0)
		last++;
	else if (dir < 0 && first)
		first--;
	if (last >= rows)
		last = rows - 1;
	if (first > last)
		first = last;
	*lo = first;
	*hi = last;
}

s32 ring_scroll_for(const struct ui_ring *r, u32 row, s32 scroll, u32 rows, u32 margin)
{
	u32 whole = r->h / r->rh;  // rows wholly on screen
	u32 most = whole >= 1 ? (whole - 1) / 2 : 0;
	if (margin > most)
		margin = most;
	s32 top = (s32)(row * r->rh);
	s32 above = top - (s32)(margin * r->rh);
	s32 below = top + (s32)((margin + 1) * r->rh) - r->h;
	if (above < scroll)
		scroll = above;
	else if (below > scroll)
		scroll = below;
	s32 max = ring_max_scroll(r, rows);
	if (scroll > max)
		scroll = max;
	return scroll < 0 ? 0 : scroll;
}

s32 ring_step(const struct ui_ring *r, s32 shown, s32 want)
{
	s32 delta = want - shown, most = (s32)(r->step * r->rh);
	if (delta > most)
		delta = most;
	else if (delta < -most)
		delta = -most;
	return shown + delta;
}

int ring_far(const struct ui_ring *r, s32 shown, s32 target)
{
	s32 d = target - shown, near = (s32)((r->visible + 1) * r->rh);
	if (d < 0)
		d = -d;
	if (near < 2 * (s32)r->h)
		near = 2 * (s32)r->h;
	return d > near;
}

void ring_glide(const struct ui_ring *r, u32 row, s32 step, s32 target, u32 rows, u32 phase, u32 rate, s32 omega,
		struct ring_glide *g)
{
	s32 next = (s32)row + step;
	if (next < 0)
		next = 0;
	if (rows && next >= (s32)rows)
		next = (s32)rows - 1;
	s32 span = ring_scroll_for(r, (u32)next, target, rows, 1) - target;
	g->want = target + (s32)(((int64_t)span * phase) >> 16);
	g->speed = (s32)((int64_t)span * rate);  // px a frame, 16.16
	s32 lag = omega > 0 ? (s32)(((int64_t)g->speed * 2) / omega) : 0;  // px
	s32 goal = g->want + lag, max = ring_max_scroll(r, rows);
	g->goal = goal < 0 ? 0 : goal > max ? max : goal;
}

s32 ring_glide_stop(const struct ui_ring *r, u32 row, s32 target, s32 at, s32 speed, s32 omega, u32 rows)
{
	s32 rh = r->rh, ahead = omega > 0 ? speed / omega : 0;  // px: both are 16.16
	s32 from = at + ahead;
	// Row-aligned as target is: target + n rows
	s32 stop = target + (from - target) / rh * rh;
	if (speed > 0 && stop < from)
		stop += rh;
	else if (speed < 0 && stop > from)
		stop -= rh;
	s32 max = ring_max_scroll(r, rows);
	if (stop > max)
		stop = max;
	if (stop < 0)
		stop = 0;
	return ring_scroll_for(r, row, stop, rows, 1);
}

s32 ring_keep_on_screen(const struct ui_ring *r, u32 row, s32 scroll)
{
	s32 top = (s32)(row * r->rh), lo = top + r->rh - (s32)r->h;
	if (scroll > top)
		scroll = top;
	if (scroll < lo)
		scroll = lo;
	return scroll < 0 ? 0 : scroll;
}

s32 ring_rebase(const struct ui_ring *r, s32 goal_px)
{
	if (goal_px <= UI_RING_REBASE_PX && goal_px >= -UI_RING_REBASE_PX)
		return 0;
	return goal_px / r->rh * r->rh;
}

void ring_prefetch(const struct ui_ring *r, u32 lo, u32 hi, u32 rows, u32 *plo, u32 *phi)
{
	*plo = lo > r->k ? lo - r->k : 0;
	*phi = hi + r->k;
	if (rows && *phi >= rows)
		*phi = rows - 1;
}
