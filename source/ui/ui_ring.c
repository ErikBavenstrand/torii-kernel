// See ring.h.
#include "ring.h"

int ring_setup(struct ui_ring *r, u32 h, u32 rh, const char **why)
{
	*why = 0;
	if (rh != 16 && rh != 32) {
		*why = "a row is 16 or 32 px tall";
		return 0;
	}
	if (h < rh || h > 160 || h % 8) {
		*why = "a list's viewport is a multiple of 8 px, from one row to 160 px";
		return 0;
	}
	r->h = (u16)h;
	r->rh = (u8)rh;
	r->visible = (u8)((h - 1) / rh + 2);
	r->k = (u8)(r->visible + 2);
	r->ring_rows = (u8)(256 / rh);
	if (r->k > UI_RING_MAX_SLOTS || r->k > r->ring_rows) {
		*why = "the viewport needs more row slots than the ring holds";
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
	s32 delta = want - shown;
	if (delta > r->rh)
		delta = r->rh;
	else if (delta < -(s32)r->rh)
		delta = -(s32)r->rh;
	return shown + delta;
}

int ring_far(const struct ui_ring *r, s32 shown, s32 target)
{
	s32 d = target - shown;
	if (d < 0)
		d = -d;
	return d > (s32)((r->visible + 1) * r->rh);
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
