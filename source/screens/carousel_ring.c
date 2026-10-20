// See carousel_ring.h.
#include "carousel_ring.h"
#include <stdint.h>

#include "../engine/affine.h"

// v / 4096 (a 4.12 product back to units), rounded to nearest: a shift,
// not a division, as it runs for every object every frame the ring moves
static s32 round12(s32 v)
{
	return (v + 2048) >> 12;
}

void ring_window(const struct ring_look *look, u32 count, s32 *lo, s32 *hi)
{
	s32 n = (s32)(count < look->slots ? count : look->slots);
	// Nearest first: the front, then one each side, the next one to the
	// right (the later item) before its mirror on the left
	*lo = -((n - 1) / 2);
	*hi = *lo + n - 1;
}

u32 ring_item(s32 k, u32 count)
{
	if (!count)
		return 0;
	s32 m = k % (s32)count;
	return (u32)(m < 0 ? m + (s32)count : m);
}

static s32 abs32(s32 v)
{
	return v < 0 ? -v : v;
}

u32 ring_objects(const struct ring_look *look, u32 count, s32 pos, struct ring_object out[RING_OBJECTS])
{
	if (!count)
		return 0;
	s32 lo, hi;
	ring_window(look, count, &lo, &hi);
	u32 n = 0;
	// Every k whose offset k - pos is within one item of the window
	for (s32 k = ((pos + (lo - 1) * FIX_ONE) >> 16) + 1; k * FIX_ONE - pos < (hi + 1) * FIX_ONE; k++) {
		s32 o = k * FIX_ONE - pos;
		s32 dist = o < lo * FIX_ONE ? lo * FIX_ONE - o : o > hi * FIX_ONE ? o - hi * FIX_ONE : 0;
		if (dist >= FIX_ONE || n == RING_OBJECTS)
			continue;
		u16 angle = (u16)(((o >> 4) * (s32)look->step) >> 12);  // 32-bit: o is a few items
		s32 s = affine_sin(angle), c = affine_cos(angle);
		s32 scale = look->scale_far + round12((s32)look->scale_span * c);
		if (scale < (s32)look->scale_min)
			continue;
		s32 natural = round12((s32)look->fog * (4096 - c));
		natural = natural > 256 ? 256 : natural;
		// Leaving or entering the window: fogged out over the item outside
		s32 shown = (FIX_ONE - dist) >> 8;
		s32 fog = 256 - (((256 - natural) * shown) >> 8);
		if (fog >= 256)
			continue;
		struct ring_object obj = {
			k,
			o,
			(s16)(look->centre_x + round12((s32)look->radius_x * s)),
			(s16)(look->feet_y - round12((s32)look->rise * (4096 - c))),
			(u16)scale,
			(u16)fog,
		};
		// Front first: insertion by |o|
		u32 at = n++;
		while (at && abs32(out[at - 1].o) > abs32(o)) {
			out[at] = out[at - 1];
			at--;
		}
		out[at] = obj;
	}
	return n;
}

u32 ring_matrix(u32 scale)
{
	if (!scale)
		return 0x7FFF;
	u32 pd = (0x10000u + scale / 2) / scale;
	return pd > 0x7FFF ? 0x7FFF : pd;
}

s32 ring_foot_dy(u32 pd)
{
	// The last row dy (from the centre) whose texel row (pd * dy >> 8) + 32
	// is still inside the sprite's 64
	return pd ? (s32)((32u * 256u - 1u) / pd) : 31;
}

void ring_motion_rest(struct ring_motion *m, s32 at)
{
	m->pos = m->from = at * FIX_ONE;
	m->to = at;
	m->m0 = m->vel = 0;
	m->frame = m->frames = 0;
	m->moving = 0;
}

void ring_motion_turn(struct ring_motion *m, s32 dir, u32 frames, Ease ease)
{
	m->to += dir;
	m->from = m->pos;
	m->frame = 0;
	m->frames = (u16)(frames ? frames : 1);
	m->ease = (u8)ease;
	s32 d = m->to * FIX_ONE - m->from;
	// Already moving: start with the speed it has, never faster than the
	// eased curve starts (3d, ease-out cubic's), never backwards
	s32 m0 = m->moving ? m->vel * (s32)m->frames : 0;
	if ((d > 0 && m0 < 0) || (d < 0 && m0 > 0) || !d)
		m0 = 0;
	if (abs32(m0) > abs32(3 * d))
		m0 = 3 * d;
	m->m0 = m0;
	m->moving = 1;
}

int ring_motion_step(struct ring_motion *m, u32 dt)
{
	if (!m->moving || !dt)
		return m->moving;
	s32 prev = m->pos;
	m->frame = (u16)(m->frame + dt);
	if (m->frame >= m->frames) {
		m->pos = m->from = m->to * FIX_ONE;
		m->m0 = m->vel = 0;
		m->moving = 0;
		return 0;
	}
	s32 d = m->to * FIX_ONE - m->from;
	int64_t s = (int64_t)(((u32)m->frame << 16) / m->frames);  // 0-1, 16.16
	if (m->m0) {
		// Hermite from (from, m0) to (to, 0): h01 = 3s^2 - 2s^3, h10 = s^3 - 2s^2 + s
		int64_t s2 = (s * s) >> 16, s3 = (s2 * s) >> 16;
		int64_t h01 = 3 * s2 - 2 * s3, h10 = s3 - 2 * s2 + s;
		m->pos = m->from + (s32)(((int64_t)d * h01) >> 16) + (s32)(((int64_t)m->m0 * h10) >> 16);
	} else {
		m->pos = m->from + (s32)(((int64_t)d * ease_at((Ease)m->ease, (u32)s)) >> 15);
	}
	m->vel = (m->pos - prev) / (s32)dt;
	return 1;
}

void ring_motion_wrap(struct ring_motion *m, u32 count)
{
	if (!count)
		return;
	// Whole laps off everything, so the numbers stay small however long
	// the ring turns; what it shows doesn't change
	s32 laps = m->to - (s32)ring_item(m->to, count);
	m->to -= laps;
	m->pos -= laps * FIX_ONE;
	m->from -= laps * FIX_ONE;
}
