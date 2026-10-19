// A held key scrolling a list (source/ui/ui_list.c's glide, on ring.h's
// maths, input.c's repeat ramp and motion.c's spring): a model of the
// list's update, frame by frame, checked for what the user sees.
#include "../../source/engine/input.h"
#include "../../source/engine/motion.h"
#include "../../source/ui/ring.h"
#include "../kernel/torii_test.h"
#include "tests.h"

struct sim {
	struct ui_ring r;
	InputState in;
	MotionId m;
	s32 spring, base, shown, target, speed;
	u32 rows, cursor;
	u16 key;  // the key held
	s32 delta;  // rows it moves
	u16 glide;  // as list_state.glide_key
	s32 step;
	u32 far;    // page changes a move would have made
	u32 glide_at;  // in.now when a repeat last moved the cursor
	int taken;     // a view above takes the key: the cursor doesn't move
	int braking;
};

static void sim_init(struct sim *s, u32 h, u32 rows, u32 cursor, u16 key, s32 delta)
{
	const char *why;
	CHECK(ring_setup(&s->r, h, 16, &why));
	input_init(&s->in, NULL);
	if (key & (INPUT_LEFT | INPUT_RIGHT))
		input_page_keys(&s->in, INPUT_LEFT | INPUT_RIGHT);
	motion_reset();
	s->spring = 0;
	s->rows = rows;
	s->cursor = cursor;
	s->target = s->base = s->shown = ring_scroll_for(&s->r, cursor, 0, rows, 1);
	s->m = motion_spring(MOTION_TO_S32(&s->spring), 0, SPRING_SCROLL, 0);
	s->key = key;
	s->delta = delta;
	s->glide = 0;
	s->speed = 0;
	s->far = 0;
	s->taken = 0;
	s->braking = 0;
}

// One frame, as ui_frame runs it: keys, motion, the list's update
static void sim_frame(struct sim *s, int held)
{
	InputState *in = &s->in;
	input_step(in, held ? s->key : 0, 1);
	if (input_fired(in, s->key) && !s->taken) {
		s32 to = (s32)s->cursor + s->delta;
		to = to < 0 ? 0 : to >= (s32)s->rows ? (s32)s->rows - 1 : to;
		if ((u32)to != s->cursor) {
			s32 step = to - (s32)s->cursor;
			// set_cursor
			s->glide = (in->fire & ~in->pressed & in->repeating & s->key) ? s->key : 0;
			s->step = step;
			s->cursor = (u32)to;
			s->glide_at = in->now;
			s->braking = 0;
			motion_spring_omega(s->m, SPRING_SCROLL);
			s->target = ring_scroll_for(&s->r, s->cursor, s->target, s->rows, 1);
			if (ring_far(&s->r, s->shown, s->target))
				s->far++;
			if (!s->glide)
				motion_spring_target(s->m, FIX(s->target - s->base));
		}
	}
	motion_update(1);
	// list_update
	if (s->glide) {
		int stalled = (in->fire & s->glide) && s->glide_at != in->now;
		u32 rate = (in->held & s->glide) && !stalled ? input_rate(in, s->glide) : 0;
		if (!rate) {
			s32 at = s->base + FIX_ROUND(s->spring);
			s->target = ring_glide_stop(&s->r, s->cursor, s->target, at, s->speed, 0x18000, s->rows);
			s->glide = 0;
			s->braking = 1;
			motion_spring_omega(s->m, 0x18000);
			motion_spring_target(s->m, FIX(s->target - s->base));
		} else {
			struct ring_glide g;
			ring_glide(&s->r, s->cursor, s->step, s->target, s->rows, input_phase(in, s->glide), rate,
				   SPRING_SCROLL, &g);
			s->speed = g.speed;
			motion_spring_target(s->m, FIX(g.goal - s->base));
		}
	}
	s32 want = s->base + FIX_ROUND(s->spring), max = ring_max_scroll(&s->r, s->rows);
	s->braking = s->braking && motion_running(s->m);
	if (s->glide || s->braking)
		want = ring_keep_on_screen(&s->r, s->cursor, want);
	want = want < 0 ? 0 : want > max ? max : want;
	s->shown = ring_step(&s->r, s->shown, want);
}

static int on_screen(const struct sim *s)
{
	s32 top = (s32)(s->cursor * 16) - s->shown;
	return top >= 0 && top + 16 <= (s32)s->r.h;
}

// DOWN held 3 s in 302 rows: once the cursor reaches the screen's bottom
// row of context the list scrolls with it, its speed rising smoothly to a
// row a frame, the cursor always on screen; let go, it brakes within two
// rows to a whole row and stops, never backing up
void test_ui_glide_held(void)
{
	static const u16 keys[] = {INPUT_DOWN, INPUT_UP};
	for (u32 k = 0; k < 2; k++) {
		struct sim s;
		s32 dir = k ? -1 : 1;
		sim_init(&s, 144, 302, k ? 301 : 0, keys[k], dir);
		s32 last_v = 0, top_v = 0, prev = s.shown;
		u32 moving = 0;
		for (u32 f = 0; f < 180; f++) {
			sim_frame(&s, 1);
			s32 v = (s.shown - prev) * dir;
			CHECK(v >= 0);         // never backwards
			CHECK(on_screen(&s));  // the cursor never leaves the screen
			if (v)
				moving++;
			// No jolt: the speed changes by a few px a frame at most
			CHECK(v - last_v <= 5 && last_v - v <= 5);
			if (v > top_v)
				top_v = v;
			last_v = v;
			prev = s.shown;
		}
		CHECK(moving > 120);
		CHECK_EQ(top_v, 16);  // a row a frame at the top of the ramp
		CHECK_EQ(last_v, 16);
		CHECK_EQ(s.far, 0u);
		// Let go
		s32 at = s.shown;
		for (u32 f = 0; f < 30; f++) {
			sim_frame(&s, 0);
			s32 v = (s.shown - prev) * dir;
			CHECK(v >= 0);
			CHECK(v <= last_v || v <= 1);  // only slows down (but for the spring's last pixel)
			CHECK(on_screen(&s));
			last_v = v;
			prev = s.shown;
		}
		CHECK_EQ(last_v, 0);
		CHECK((s.shown - at) * dir <= 2 * 16);  // stops within two rows
		CHECK_EQ(s.shown % 16, 0);  // a whole row (144 is 9 rows)
		CHECK(!motion_running(s.m));
	}
}

// The key still held but the cursor no longer moving (a view above took
// it): the list brakes and rests rather than gliding back and forth
void test_ui_glide_taken(void)
{
	struct sim s;
	sim_init(&s, 144, 302, 0, INPUT_DOWN, 1);
	for (u32 f = 0; f < 120; f++)
		sim_frame(&s, 1);
	s.taken = 1;
	s32 prev = s.shown;
	for (u32 f = 0; f < 60; f++) {
		sim_frame(&s, 1);
		CHECK(s.shown >= prev);
		CHECK(on_screen(&s));
		prev = s.shown;
	}
	CHECK(!s.glide);
	CHECK(!motion_running(s.m));
	CHECK_EQ(s.shown % 16, 0);
}

// Held to the end: the list stops at its last row with the cursor on it
void test_ui_glide_end(void)
{
	struct sim s;
	sim_init(&s, 144, 60, 0, INPUT_DOWN, 1);
	for (u32 f = 0; f < 240; f++) {
		sim_frame(&s, 1);
		CHECK(on_screen(&s));
		CHECK(s.shown <= ring_max_scroll(&s.r, 60));
	}
	CHECK_EQ(s.cursor, 59u);
	CHECK_EQ(s.shown, ring_max_scroll(&s.r, 60));
	for (u32 f = 0; f < 30; f++)
		sim_frame(&s, 0);
	CHECK_EQ(s.shown, ring_max_scroll(&s.r, 60));
	// A short list: nothing scrolls
	sim_init(&s, 144, 5, 0, INPUT_DOWN, 1);
	for (u32 f = 0; f < 100; f++) {
		sim_frame(&s, 1);
		CHECK_EQ(s.shown, 0);
	}
	CHECK_EQ(s.cursor, 4u);
}

// RIGHT held pages of 9 on the paging schedule: the list streams at a
// steady speed under ring_step's, never needing a page change, and the
// cursor is on screen but for the few frames the scroll catches up when
// the repeats start
void test_ui_glide_pages(void)
{
	struct sim s;
	sim_init(&s, 144, 302, 0, INPUT_RIGHT, 9);
	u32 off = 0, steady = 0;
	s32 prev = s.shown;
	for (u32 f = 0; f < 240; f++) {
		sim_frame(&s, 1);
		off += !on_screen(&s);
		s32 v = s.shown - prev;
		CHECK(v >= 0 && v <= 32);
		if (f > 40 && s.cursor + 9 < 302)
			steady += v == 24;  // 144 px every 6 frames
		prev = s.shown;
	}
	CHECK_EQ(s.far, 0u);
	CHECK(off <= 6);
	CHECK(steady > 60);
	CHECK_EQ(s.cursor, 301u);
	// Quick taps (a page every 4 frames) catch up by scrolling, not with a
	// page change
	sim_init(&s, 144, 302, 0, INPUT_RIGHT, 9);
	for (u32 f = 0; f < 40; f++)
		sim_frame(&s, (f & 3) < 2);
	CHECK_EQ(s.far, 0u);
	for (u32 f = 0; f < 30; f++)
		sim_frame(&s, 0);
	CHECK(on_screen(&s));
}
