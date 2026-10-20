// The home carousel's ring (source/screens/carousel_ring.c): the window of
// items, where objects stand, the feet's anchor, and the turning motion.
#include <stdlib.h>

#include "../../source/screens/carousel_ring.h"
#include "../kernel/torii_test.h"
#include "tests.h"

// spec.md's ring, as source/screens/carousel.c's look has it
static const struct ring_look look = {120, 98, 100, 30, 6554, 108, 148, 243, 24, 5};

static u32 window_size(u32 count)
{
	s32 lo, hi;
	ring_window(&look, count, &lo, &hi);
	return (u32)(hi - lo + 1);
}

void test_carousel_window(void)
{
	s32 lo, hi;
	// Five or more: two each side
	for (u32 n = 5; n <= 14; n++) {
		ring_window(&look, n, &lo, &hi);
		CHECK(lo == -2 && hi == 2);
	}
	// Fewer: that many, nearest first, the next to the right before its mirror
	ring_window(&look, 4, &lo, &hi);
	CHECK(lo == -1 && hi == 2);
	ring_window(&look, 3, &lo, &hi);
	CHECK(lo == -1 && hi == 1);
	ring_window(&look, 2, &lo, &hi);
	CHECK(lo == 0 && hi == 1);
	ring_window(&look, 1, &lo, &hi);
	CHECK(lo == 0 && hi == 0);
	CHECK_EQ(window_size(0), 0);
	CHECK_EQ(ring_item(-1, 7), 6);
	CHECK_EQ(ring_item(-15, 7), 6);
	CHECK_EQ(ring_item(9, 7), 2);
	CHECK_EQ(ring_item(3, 0), 0);
}

void test_carousel_rest(void)
{
	struct ring_object o[RING_OBJECTS];
	for (u32 n = 1; n <= 14; n++) {
		for (s32 at = -3; at < 20; at++) {
			u32 count = ring_objects(&look, n, at * FIX_ONE, o);
			// At rest the window shows, every item once (T26), front first
			CHECK_EQ(count, window_size(n));
			for (u32 i = 0; i < count; i++)
				for (u32 j = i + 1; j < count; j++)
					CHECK(ring_item(o[i].k, n) != ring_item(o[j].k, n));
			for (u32 i = 1; i < count; i++)
				CHECK(abs(o[i - 1].o) <= abs(o[i].o));
			// The front: in the middle, lowest, whole size, no fog
			CHECK(o[0].k == at && o[0].x == 120 && o[0].feet == 100 && o[0].scale == 256 && o[0].fog == 0);
			// The others higher, smaller and fogged, the same each side
			for (u32 i = 1; i < count; i++) {
				CHECK(o[i].feet < 100 && o[i].scale < 256 && o[i].fog > 0);
				for (u32 j = 1; j < count; j++)
					if (o[j].o == -o[i].o)
						CHECK(o[j].x == 240 - o[i].x && o[j].feet == o[i].feet &&
						      o[j].scale == o[i].scale && o[j].fog == o[i].fog);
			}
		}
	}
	// spec.md's numbers at 36 and 72 degrees
	ring_objects(&look, 10, 0, o);
	for (u32 i = 0; i < 5; i++) {
		if (o[i].o == FIX_ONE)
			CHECK(o[i].x == 178 && o[i].feet == 94 && o[i].scale == 228);
		if (o[i].o == 2 * FIX_ONE)
			CHECK(o[i].x == 213 && o[i].feet == 79 && o[i].scale == 154);
	}
}

void test_carousel_turning(void)
{
	struct ring_object o[RING_OBJECTS], prev[RING_OBJECTS];
	for (u32 n = 2; n <= 12; n++) {
		u32 prev_count = ring_objects(&look, n, 0, prev);
		for (s32 pos = 1; pos <= 3 * FIX_ONE; pos += 0x400) {
			u32 count = ring_objects(&look, n, pos, o);
			CHECK(count <= RING_OBJECTS);
			// Six or more items: never one twice
			if (n >= 6)
				for (u32 i = 0; i < count; i++)
					for (u32 j = i + 1; j < count; j++)
						CHECK(ring_item(o[i].k, n) != ring_item(o[j].k, n));
			// Each object glides: a small step of the ring moves it a little,
			// and one comes or goes only almost fogged out
			for (u32 i = 0; i < count; i++) {
				u32 j = 0;
				while (j < prev_count && prev[j].k != o[i].k)
					j++;
				if (j == prev_count) {
					CHECK(o[i].fog > 230);
					continue;
				}
				CHECK(abs(o[i].x - prev[j].x) <= 2 && abs(o[i].feet - prev[j].feet) <= 1);
				CHECK(abs((s32)o[i].fog - (s32)prev[j].fog) <= 8);
			}
			for (u32 j = 0; j < prev_count; j++) {
				u32 i = 0;
				while (i < count && o[i].k != prev[j].k)
					i++;
				if (i == count)
					CHECK(prev[j].fog > 230);
			}
			for (u32 i = 0; i < count; i++)
				prev[i] = o[i];
			prev_count = count;
		}
	}
}

void test_carousel_feet(void)
{
	// The row ring_foot_dy gives shows the last texel row, the one under it
	// nothing: the hardware samples row (pd * dy >> 8) + 32
	for (u32 scale = 24; scale <= 256; scale++) {
		u32 pd = ring_matrix(scale);
		s32 dy = ring_foot_dy(pd);
		CHECK(((s32)pd * dy >> 8) + 32 <= 63);
		CHECK(((s32)pd * (dy + 1) >> 8) + 32 >= 64);
	}
	CHECK_EQ(ring_matrix(256), 256);
	CHECK_EQ(ring_matrix(128), 512);
	CHECK_EQ(ring_foot_dy(256), 31);
}

void test_carousel_motion(void)
{
	struct ring_motion m;
	ring_motion_rest(&m, 3);
	CHECK(m.pos == 3 * FIX_ONE && !m.moving && ring_motion_lag(&m) == 0);
	// One step: ease-out cubic over 10 frames, forward only, landing exactly
	ring_motion_turn(&m, 1, 10, EASE_OUT_CUBIC);
	s32 last = m.pos;
	for (u32 f = 1; f <= 10; f++) {
		int moving = ring_motion_step(&m, 1);
		CHECK(moving == (f < 10));
		CHECK(m.pos > last && m.pos <= 4 * FIX_ONE);
		if (f == 5)  // 1 - 0.5^3
			CHECK(abs(m.pos - (3 * FIX_ONE + FIX_ONE * 7 / 8)) < 0x200);
		last = m.pos;
	}
	CHECK(m.pos == 4 * FIX_ONE && m.to == 4 && !m.moving);
	// A held key: a turn every 8 frames keeps going with the speed it has,
	// never stopping or going back, never past the target
	ring_motion_turn(&m, -1, 10, EASE_OUT_CUBIC);
	last = m.pos;
	s32 last_vel = 0;
	for (u32 f = 1; f <= 80; f++) {
		if (f % 8 == 0 && f < 64)
			ring_motion_turn(&m, -1, 10, EASE_OUT_CUBIC);
		ring_motion_step(&m, 1);
		CHECK(m.pos <= last && m.pos >= m.to * FIX_ONE);
		// Turns after the first don't kick the speed up by more than a
		// frame's easing would
		if (f > 8 && f < 64)
			CHECK(abs(m.vel - last_vel) < FIX_ONE / 12);
		last = m.pos;
		last_vel = m.vel;
	}
	CHECK(!m.moving && m.pos == m.to * FIX_ONE && m.to == 4 - 8);
	// Reversing mid-step goes back from where it is, landing on the target
	ring_motion_rest(&m, 0);
	ring_motion_turn(&m, 1, 10, EASE_OUT_CUBIC);
	ring_motion_step(&m, 1);
	ring_motion_step(&m, 1);
	ring_motion_turn(&m, -1, 10, EASE_OUT_CUBIC);
	for (u32 f = 0; f < 10; f++)
		ring_motion_step(&m, 1);
	CHECK(!m.moving && m.pos == 0 && m.to == 0);
	// Frames dropped: it lands all the same
	ring_motion_turn(&m, 1, 10, EASE_OUT_CUBIC);
	ring_motion_step(&m, 3);
	ring_motion_step(&m, 3);
	ring_motion_step(&m, 3);
	CHECK(m.moving);
	CHECK(!ring_motion_step(&m, 3) && m.pos == FIX_ONE);
	// Wrapping takes whole laps off, the same item showing, mid-turn too
	ring_motion_rest(&m, 13);
	ring_motion_turn(&m, 1, 10, EASE_OUT_CUBIC);
	ring_motion_step(&m, 1);
	s32 frac = m.pos - 13 * FIX_ONE;
	ring_motion_wrap(&m, 5);
	CHECK(m.to == 4 && m.pos == 3 * FIX_ONE + frac && m.moving);
	ring_motion_rest(&m, -2);
	ring_motion_wrap(&m, 5);
	CHECK(m.to == 3 && m.pos == 3 * FIX_ONE);
}
