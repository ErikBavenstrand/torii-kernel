#include "input.h"

#include <string.h>

const InputTiming input_timing_default = {
	.delay = 15,
	.interval = 4,
	.fast_after = 60,
	.fast_interval = 2,
	.skip_interval = 1,
	.page_delay = 20,
	.page_interval = 8,
	.wrap_pause = 15,
};

void input_init(InputState *s, const InputTiming *timing)
{
	memset(s, 0, sizeof(*s));
	s->timing = timing ? timing : &input_timing_default;
}

static u32 next_interval(const InputTiming *t, u16 bit, u32 held, int skip_anim)
{
	u32 frames;
	if (bit & INPUT_PAGE)
		frames = t->page_interval;
	else if (held + t->interval < t->fast_after)
		frames = t->interval;
	else
		frames = skip_anim ? t->skip_interval : t->fast_interval;
	return frames ? frames : 1;
}

void input_step(InputState *s, u16 keys, u32 dt, int skip_anim)
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
		if (!(keys & bit) || !(bit & (INPUT_DPAD | INPUT_PAGE))) {
			s->held_frames[k] = 0;
			continue;
		}
		if ((s->pressed | restart) & bit) {
			s->held_frames[k] = 0;
			s->next_fire[k] = bit & INPUT_PAGE ? t->page_delay : t->delay;
			continue;
		}
		u32 held = s->held_frames[k] + dt;
		// Past about 2^31 frames (a year), hold the count.
		if (held > 0x7FFFFFFF)
			held = s->held_frames[k];
		s->held_frames[k] = held;
		if (held >= s->next_fire[k]) {
			s->fire |= bit;
			s->next_fire[k] = held + next_interval(t, bit, held, skip_anim);
		}
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
