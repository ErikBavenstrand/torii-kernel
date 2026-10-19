#include "input.h"

#include <stdint.h>
#include <string.h>

const InputTiming input_timing_default = {
	.delay = 15,
	.interval = 4,
	.ramp = 90,
	.top_speed = 40,
	.page_delay = 20,
	.page_interval = 6,
	.wrap_pause = 15,
};

void input_init(InputState *s, const InputTiming *timing)
{
	memset(s, 0, sizeof(*s));
	s->timing = timing ? timing : &input_timing_default;
	s->page_keys = INPUT_PAGE;
}

void input_page_keys(InputState *s, u16 keys)
{
	s->page_keys = (u16)((keys & (INPUT_DPAD | INPUT_PAGE)) | INPUT_PAGE);
}

static u32 per_frame(u32 interval)
{
	// Rounded up, so a repeat every interval frames is never a frame late
	if (!interval)
		interval = 1;
	return (INPUT_ONE + interval - 1) / interval;
}

u32 input_ramp_rate(const InputTiming *t, u32 ramp)
{
	u32 top = t->top_speed > 60 ? 60 : t->top_speed ? t->top_speed : 1;
	s32 from = (s32)per_frame(t->interval), to = (s32)((top * INPUT_ONE + 59) / 60);
	if (!t->ramp || ramp >= t->ramp)
		return (u32)to;
	// Smoothstep: no jolt where the ramp starts or ends
	uint64_t u = (ramp << 16) / t->ramp;  // 0..1, 16.16 (ramp < 256)
	uint64_t curve = u * u * ((uint64_t)3 * INPUT_ONE - 2 * u) >> 32;  // 3u^2 - 2u^3, 16.16
	return (u32)(from + (s32)((int64_t)(to - from) * (int64_t)curve >> 16));
}

u32 input_phase(const InputState *s, u16 key)
{
	for (u32 k = 0; k < INPUT_KEYS; k++)
		if (key == 1u << k)
			return (s->repeating & key) ? s->phase[k] : 0;
	return 0;
}

u32 input_rate(const InputState *s, u16 key)
{
	for (u32 k = 0; k < INPUT_KEYS; k++)
		if (key == 1u << k)
			return (s->repeating & key) ? s->rate[k] : 0;
	return 0;
}

void input_step(InputState *s, u16 keys, u32 dt)
{
	const InputTiming *t = s->timing;
	if (dt < 1)
		dt = 1;
	if (dt > 3)
		dt = 3;
	keys &= INPUT_ALL;
	s->now += dt;
	s->pressed = keys & ~s->held;
	s->released = s->held & ~keys;
	s->held = keys;
	s->fire = s->pressed;
	// A new direction, or letting one go, starts every held direction's
	// schedule over (from delay, without firing): speed-up resets on a
	// release or a change of direction.
	u16 restart = (s->pressed | s->released) & INPUT_DPAD ? INPUT_DPAD : 0;
	for (u32 k = 0; k < INPUT_KEYS; k++) {
		u16 bit = (u16)(1 << k);
		if (!(keys & bit) || !(bit & (INPUT_DPAD | INPUT_PAGE)) || ((s->pressed | restart) & bit)) {
			s->held_frames[k] = 0;
			s->repeating &= (u16)~bit;
			continue;
		}
		int page = (s->page_keys & bit) != 0;
		u32 held = s->held_frames[k] + dt;
		// Past about 2^31 frames (a year), hold the count.
		if (held > 0x7FFFFFFF)
			held = s->held_frames[k];
		s->held_frames[k] = held;
		if (!(s->repeating & bit)) {
			if (held >= (page ? t->page_delay : t->delay)) {
				s->fire |= bit;
				s->repeating |= bit;
				s->phase[k] = 0;
				s->ramp_frames[k] = 0;
				s->rate[k] = page ? per_frame(t->page_interval) : input_ramp_rate(t, 0);
			}
			continue;
		}
		if (page) {
			s->rate[k] = per_frame(t->page_interval);
		} else {
			u32 r = s->ramp_frames[k] + dt;
			s->ramp_frames[k] = (u16)(r > t->ramp ? t->ramp : r);
			s->rate[k] = input_ramp_rate(t, s->ramp_frames[k]);
		}
		// At most one repeat a step; what is left over carries on
		u32 phase = s->phase[k] + s->rate[k] * dt;
		if (phase >= INPUT_ONE) {
			s->fire |= bit;
			phase -= INPUT_ONE;
			if (phase >= INPUT_ONE)
				phase = INPUT_ONE - 1;
		}
		s->phase[k] = phase;
	}
}

u32 input_move(const InputState *s, InputWrap *w, u16 key, u32 cursor, u32 count, s32 delta)
{
	if (count == 0)
		return 0;
	if (cursor >= count)
		cursor = count - 1;
	if (cursor != w->cursor)
		w->since = s->now;
	u32 step = delta < 0 ? 0u - (u32)delta : (u32)delta;
	u32 moved = cursor;
	if (delta < 0 ? step <= cursor : step < count - cursor) {
		moved = delta < 0 ? cursor - step : cursor + step;
	} else {
		u32 edge = delta < 0 ? 0 : count - 1;
		int fresh = (s->pressed & key) != 0;
		if (cursor != edge)
			moved = edge;
		else if (fresh || s->now - w->since >= s->timing->wrap_pause)
			moved = delta < 0 ? count - 1 : 0;
	}
	if (moved != cursor)
		w->since = s->now;
	w->cursor = moved;
	return moved;
}
