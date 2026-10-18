// Tweens, springs, easing tables and palette fades (source/engine/motion.c).
#include <stdint.h>

#include "../../source/engine/motion.h"
#include "../kernel/torii_test.h"
#include "tests.h"

// The curve at i/255 as a fraction over 255^3, as tools/ease_lut.py has it.
static uint64_t curve(u32 table, uint64_t i)
{
	const uint64_t n = 255, cube = n * n * n;
	switch (table) {
	case EASE_IN_CUBIC - 1:
		return i * i * i;
	case EASE_OUT_CUBIC - 1:
		return cube - (n - i) * (n - i) * (n - i);
	default:
		return 2 * i < n ? 4 * i * i * i : cube - 4 * (n - i) * (n - i) * (n - i);
	}
}

void test_ease_lut_reproducible(void)
{
	const uint64_t cube = 255ull * 255 * 255;
	for (u32 table = 0; table < EASE_TABLES; table++)
		for (u32 i = 0; i < EASE_LUT_SIZE; i++)
			CHECK_EQ(ease_lut[table][i], (EASE_ONE * curve(table, i) + cube / 2) / cube);
}

void test_ease_endpoints(void)
{
	for (u32 e = 0; e < EASE_COUNT; e++) {
		CHECK_EQ(ease_at((Ease)e, 0), 0);
		CHECK_EQ(ease_at((Ease)e, 0x10000), EASE_ONE);
		CHECK_EQ(ease_at((Ease)e, 0x20000), EASE_ONE);
		u32 last = 0;
		for (u32 pos = 0; pos <= 0x10000; pos += 61) {
			u32 v = ease_at((Ease)e, pos);
			CHECK(v >= last);
			last = v;
		}
	}
	CHECK_EQ(ease_at(EASE_LINEAR, 0x8000), EASE_ONE / 2);
	CHECK_EQ(ease_at(EASE_IN_OUT_CUBIC, 0x8000), EASE_ONE / 2);
	// Out-cubic starts fast, in-cubic slow.
	CHECK(ease_at(EASE_OUT_CUBIC, 0x1000) > ease_at(EASE_LINEAR, 0x1000));
	CHECK(ease_at(EASE_IN_CUBIC, 0x1000) < ease_at(EASE_LINEAR, 0x1000));
}

void test_tween_endpoints(void)
{
	motion_reset();
	for (u32 e = 0; e < EASE_COUNT; e++) {
		for (u32 dt = 1; dt <= 3; dt++) {
			s16 var = -1;
			MotionTarget t = MOTION_TO_S16(&var);
			MotionId id = motion_tween(t, FIX(100), FIX(-60), 10, (Ease)e, 0);
			CHECK(id != MOTION_NONE);
			CHECK(motion_running(id));
			CHECK(!motion_idle());
			CHECK_EQ(var, -1);  // nothing written before the first update
			s16 last = 100;
			u32 frames = 0;
			while (motion_running(id) && frames < 20) {
				CHECK_EQ(motion_update(dt), 1);
				frames += dt;
				CHECK(var <= last);
				last = var;
			}
			CHECK_EQ(frames, (10 + dt - 1) / dt * dt);
			CHECK_EQ(var, -60);
			CHECK_EQ(motion_value(id), FIX(-60));
			CHECK_EQ(motion_update(1), 0);  // at rest, nothing to write
			CHECK(motion_idle());
			motion_free(id);
		}
	}
	// The first update already moves: a press shows on its own frame.
	s32 v = 0;
	MotionId id = motion_tween(MOTION_TO_S32(&v), 0, FIX(10), 8, EASE_OUT_CUBIC, 0);
	motion_update(1);
	CHECK(v > 0 && v < FIX(10));
	// Restarting goes on from where it is.
	s32 mid = v;
	motion_tween_to(id, 0, 4);
	CHECK(motion_running(id));
	motion_update(1);
	CHECK(v < mid && v > 0);
	motion_update(3);
	CHECK_EQ(v, 0);
	motion_reset();
}

// Runs a spring from rest at 0 to dist with steps of dt; checks it never
// overshoots or moves back, and returns the frames it took to settle.
static u32 settle(s32 omega, s32 dist, u32 dt)
{
	motion_reset();
	s32 v = 0;
	MotionId id = motion_spring(MOTION_TO_S32(&v), 0, omega, 0);
	motion_spring_target(id, dist);
	u32 frames = 0;
	s32 last = 0;
	while (motion_running(id) && frames < 600) {
		motion_update(dt);
		frames += dt;
		if (dist > 0)
			CHECK(v >= last && v <= dist);
		else
			CHECK(v <= last && v >= dist);
		last = v;
	}
	CHECK(!motion_running(id));
	CHECK_EQ(v, dist);
	CHECK(motion_idle());
	return frames;
}

void test_spring_converges(void)
{
	static const s32 omegas[] = {SPRING_OMEGA_MIN, SPRING_GENTLE, SPRING_SCROLL, SPRING_FAST, SPRING_OMEGA_MAX};
	static const s32 dists[] = {1, FIX(1), FIX(16), FIX(-16), FIX(160), FIX(-2000), FIX(16000), FIX(-16000)};
	for (u32 o = 0; o < sizeof(omegas) / sizeof(*omegas); o++)
		for (u32 d = 0; d < sizeof(dists) / sizeof(*dists); d++)
			for (u32 dt = 1; dt <= 3; dt++)
				CHECK(settle(omegas[o], dists[d], dt) < 600);
	// The figures motion.h quotes for a 16 px jump.
	CHECK_EQ(settle(SPRING_FAST, FIX(16), 1), 9);
	CHECK_EQ(settle(SPRING_SCROLL, FIX(16), 1), 12);
	CHECK_EQ(settle(SPRING_GENTLE, FIX(16), 1), 17);
	// Faster than the cap runs as the cap.
	CHECK_EQ(settle(0x7FFFFFFF, FIX(16), 1), settle(SPRING_OMEGA_MAX, FIX(16), 1));
	motion_reset();
}

void test_spring_dt_clamp(void)
{
	motion_reset();
	s32 v = 0;
	MotionId id = motion_spring(MOTION_TO_S32(&v), 0, SPRING_SCROLL, 0);
	motion_update(1);
	motion_spring_target(id, FIX(200));
	CHECK_EQ(motion_update(0), 0);  // dt 0 does nothing
	CHECK_EQ(v, 0);
	CHECK(motion_running(id));

	// Steps of 1, 3 and 7 (clamped to 3), one spring each: every third frame,
	// all three are in the same place.
	s32 track[3][40];
	static const u32 steps[] = {1, 3, 7};
	for (u32 s = 0; s < 3; s++) {
		motion_reset();
		s32 pos = 0;
		MotionId sid = motion_spring(MOTION_TO_S32(&pos), 0, SPRING_SCROLL, 0);
		motion_spring_target(sid, FIX(200));
		u32 frame = 0;
		for (u32 n = 0; n < 40; n++)
			track[s][n] = 0;
		while (frame < 39) {
			u32 dt = steps[s];
			motion_update(dt);
			frame += dt > 3 ? 3 : dt;
			if (frame < 40)
				track[s][frame] = pos;
		}
	}
	for (u32 frame = 3; frame < 40; frame += 3) {
		CHECK_EQ(track[1][frame], track[0][frame]);
		CHECK_EQ(track[2][frame], track[0][frame]);
	}
	motion_reset();
}

void test_spring_retarget(void)
{
	motion_reset();
	s32 v = 0;
	MotionId id = motion_spring(MOTION_TO_S32(&v), 0, SPRING_SCROLL, 0);
	motion_spring_target(id, FIX(160));
	s32 prev = 0, step = 0;
	for (u32 i = 0; i < 4; i++) {
		motion_update(1);
		step = v - prev;
		prev = v;
	}
	CHECK(step > 0);
	// Turned back mid-flight, it keeps its velocity: a twin jumped to the
	// same place at rest and given the same target trails it, and its next
	// step changes from the last by no more than the new pull allows.
	s32 twin = 0;
	MotionId it = motion_spring(MOTION_TO_S32(&twin), v, SPRING_SCROLL, 0);
	motion_spring_target(it, FIX(-160));
	motion_spring_target(id, FIX(-160));
	CHECK_EQ(motion_goal(id), FIX(-160));
	motion_update(1);
	s32 next = v - prev;
	CHECK(v > twin);
	CHECK(next > step - FIX(320) * 9 / 16);  // omega^2 = 9/16 of the 320 units between
	CHECK(next < step);
	// It then turns once, stops past the target at most once, and settles.
	prev = v;
	u32 frames = 0, turns = 0;
	while (motion_running(id) && frames < 200) {
		motion_update(1);
		frames++;
		s32 d = v - prev;
		prev = v;
		turns += (d > 0) != (next > 0) && d != 0;
		if (d != 0)
			next = d;
	}
	CHECK(turns <= 2);
	CHECK(frames < 200);
	CHECK_EQ(v, FIX(-160));
	// The same target again doesn't wake a settled spring.
	motion_spring_target(id, FIX(-160));
	CHECK(!motion_running(id));
	CHECK(motion_idle());
	motion_reset();
}

void test_spring_jump_and_shift(void)
{
	motion_reset();
	s32 a = 0, b = 0;
	MotionId ia = motion_spring(MOTION_TO_S32(&a), 0, SPRING_FAST, 0);
	MotionId ib = motion_spring(MOTION_TO_S32(&b), 0, SPRING_FAST, 0);
	CHECK(!motion_idle());  // a new spring writes its target once
	CHECK_EQ(motion_update(1), 2);
	CHECK(motion_idle());
	motion_spring_target(ia, FIX(100));
	motion_spring_target(ib, FIX(100));
	motion_update(1);
	motion_update(1);
	// Shifting b keeps its velocity: it then runs exactly 50 units off a.
	motion_spring_shift(ib, FIX(50));
	CHECK_EQ(motion_goal(ib), FIX(150));
	while (motion_running(ia)) {
		motion_update(1);
		CHECK_EQ(b - a, FIX(50));
	}
	CHECK(!motion_running(ib));
	// A jump lands at once, at rest, and writes on the next update.
	motion_spring_jump(ia, FIX(-7));
	CHECK(!motion_running(ia));
	CHECK(!motion_idle());
	CHECK_EQ(motion_update(1), 1);
	CHECK_EQ(a, FIX(-7));
	CHECK_EQ(motion_goal(ia), FIX(-7));
	// A jump mid-flight drops the velocity.
	motion_spring_target(ib, 0);
	motion_update(1);
	motion_spring_jump(ib, FIX(20));
	motion_update(1);
	CHECK_EQ(b, FIX(20));
	CHECK(!motion_running(ib));
	motion_reset();
}

typedef struct {
	u32 calls;
	s32 last;
} Recorder;

static void record(void *ctx, s32 value)
{
	Recorder *r = ctx;
	r->calls++;
	r->last = value;
}

void test_motion_targets(void)
{
	motion_reset();
	s16 scroll = 0, scale = 0;
	s32 raw = 0;
	Recorder rec = {0, 0};
	MotionTarget ts = MOTION_TO_S16(&scroll);
	MotionTarget t8 = MOTION_TO_S16_8(&scale);
	MotionTarget tc = MOTION_TO_CALL(record, &rec);
	MotionTarget none = MOTION_TO_NOTHING;
	MotionId is = motion_spring(ts, FIX(5) + 0x8000, SPRING_FAST, 0);
	MotionId i8 = motion_spring(t8, FIX(1) + 0x80, SPRING_FAST, 0);
	MotionId ir = motion_spring(MOTION_TO_S32(&raw), 0x12345, SPRING_FAST, 0);
	MotionId ic = motion_spring(tc, FIX(12), SPRING_FAST, 0);
	MotionId in = motion_spring(none, FIX(3), SPRING_FAST, 0);
	CHECK_EQ(motion_update(1), 5);
	CHECK_EQ(scroll, 6);  // halves round up
	CHECK_EQ(scale, 0x101);
	CHECK_EQ(raw, 0x12345);
	CHECK_EQ(rec.calls, 1);
	CHECK_EQ(rec.last, FIX(12));
	CHECK_EQ(motion_value(in), FIX(3));
	motion_spring_jump(is, -FIX(5) - 0x8000);
	motion_spring_jump(i8, -0x180);
	motion_update(1);
	CHECK_EQ(scroll, -5);
	CHECK_EQ(scale, -1);
	CHECK_EQ(motion_update(1), 0);
	CHECK_EQ(rec.calls, 1);  // at rest: no more calls
	(void)ir;
	(void)ic;

	// An 8.8 spring (scale 1.0 to 2.0) settles to within its own step: its
	// last frame moves the s16 by at most 1.
	motion_spring_jump(i8, FIX(1));
	motion_update(1);
	motion_spring_target(i8, FIX(2));
	s16 last = scale;
	while (motion_running(i8)) {
		last = scale;
		motion_update(1);
	}
	CHECK_EQ(scale, 0x200);
	CHECK(0x200 - last <= 1);
	motion_reset();
}

void test_motion_cap_and_handles(void)
{
	motion_reset();
	CHECK_EQ(motion_count(), 0);
	MotionTarget none = MOTION_TO_NOTHING;
	MotionId ids[MOTION_CAP];
	for (u32 i = 0; i < MOTION_CAP; i++) {
		ids[i] = motion_spring(none, FIX(i), SPRING_FAST, (u8)(i & 1 ? 7 : 0));
		CHECK(ids[i] != MOTION_NONE);
	}
	CHECK_EQ(motion_count(), MOTION_CAP);
	tlog("  (an error line about the pool is expected next)");
	CHECK_EQ(motion_spring(none, 0, SPRING_FAST, 0), MOTION_NONE);
	// Calls on MOTION_NONE do nothing.
	motion_spring_target(MOTION_NONE, FIX(1));
	motion_free(MOTION_NONE);
	CHECK_EQ(motion_value(MOTION_NONE), 0);
	CHECK(!motion_running(MOTION_NONE));

	// A freed handle stays dead when its slot is reused.
	MotionId old = ids[2];
	motion_free(old);
	CHECK_EQ(motion_count(), MOTION_CAP - 1);
	MotionId fresh = motion_tween(none, 0, FIX(1), 5, EASE_LINEAR, 0);
	CHECK(fresh != MOTION_NONE && fresh != old);
	CHECK_EQ(motion_value(old), 0);
	motion_spring_target(old, FIX(99));
	motion_free(old);
	CHECK_EQ(motion_count(), MOTION_CAP);
	CHECK_EQ(motion_goal(fresh), FIX(1));
	// Tween calls on a spring and spring calls on a tween do nothing.
	motion_tween_to(ids[0], FIX(5), 3);
	CHECK(!motion_running(ids[0]));
	motion_spring_target(fresh, FIX(9));
	CHECK_EQ(motion_goal(fresh), FIX(1));

	// Freeing by owner frees only that owner's; owner 0 is no owner.
	motion_free_owner(0);
	CHECK_EQ(motion_count(), MOTION_CAP);
	motion_free_owner(7);
	CHECK_EQ(motion_count(), MOTION_CAP - MOTION_CAP / 2);
	CHECK_EQ(motion_value(ids[1]), 0);
	CHECK_EQ(motion_value(ids[4]), FIX(4));

	// A reset kills every handle, even after the slots fill again.
	motion_reset();
	CHECK_EQ(motion_count(), 0);
	for (u32 i = 0; i < MOTION_CAP; i++)
		motion_spring(none, FIX(100), SPRING_FAST, 0);
	for (u32 i = 0; i < MOTION_CAP; i++)
		if (i != 2)
			CHECK_EQ(motion_value(ids[i]), 0);
	CHECK_EQ(motion_value(fresh), 0);

	// A handle stays dead through many reuses of its slot.
	motion_reset();
	MotionId first = motion_spring(none, FIX(1), SPRING_FAST, 0);
	motion_free(first);
	for (u32 i = 0; i < 1000; i++) {
		MotionId again = motion_spring(none, FIX(2), SPRING_FAST, 0);
		CHECK(again != first);
		CHECK_EQ(motion_value(first), 0);
		motion_free(again);
	}
	motion_reset();
}

void test_palette_fade(void)
{
	static const u16 src[] = {0x0000, 0x7FFF, 0x001F, 0x03E0, 0x7C00, 0x1234, 0x8000 | 0x4210};
	u16 out[7];
	palette_fade(out, src, 7, FADE_BLACK, 0);
	for (u32 i = 0; i < 6; i++)
		CHECK_EQ(out[i], src[i]);
	CHECK_EQ(out[6], 0x4210);  // bit 15 isn't a colour bit
	palette_fade(out, src, 7, FADE_BLACK, FADE_MAX);
	for (u32 i = 0; i < 7; i++)
		CHECK_EQ(out[i], 0);
	palette_fade(out, src, 7, FADE_WHITE, 999);  // over 256 counts as 256
	for (u32 i = 0; i < 7; i++)
		CHECK_EQ(out[i], 0x7FFF);
	palette_fade(out, src, 7, FADE_BLACK, 128);
	CHECK_EQ(out[1], 15 | 15 << 5 | 15 << 10);
	CHECK_EQ(out[2], 15);
	palette_fade(out, src, 7, FADE_WHITE, 128);
	CHECK_EQ(out[0], 15 | 15 << 5 | 15 << 10);
	CHECK_EQ(out[2], 31 | 15 << 5 | 15 << 10);
	// Towards a colour: each channel on its own.
	palette_fade(out, src, 1, 0x7C1F, 64);  // magenta, a quarter
	CHECK_EQ(out[0], 7 | 0 << 5 | 7 << 10);
	// Every channel moves monotonically towards the target as the factor grows.
	for (u32 color = 0; color < 0x8000; color += 0x0421) {
		u16 c = (u16)color, prev = 0x1234;
		for (u32 f = 0; f <= FADE_MAX; f++) {
			u16 o;
			palette_fade(&o, &prev, 1, c, f);
			for (u32 sh = 0; sh < 15; sh += 5) {
				s32 from = 0x1234 >> sh & 31, to = c >> sh & 31, got = o >> sh & 31;
				CHECK(from <= to ? got >= from && got <= to : got <= from && got >= to);
			}
			if (f) {
				u16 before;
				palette_fade(&before, &prev, 1, c, f - 1);
				for (u32 sh = 0; sh < 15; sh += 5) {
					s32 from = 0x1234 >> sh & 31, b = before >> sh & 31, got = o >> sh & 31;
					s32 to = c >> sh & 31;
					CHECK(from <= to ? got >= b : got <= b);
				}
			}
		}
	}
	// In place.
	u16 pal[2] = {0x7FFF, 0x001F};
	palette_fade(pal, pal, 2, FADE_BLACK, FADE_MAX);
	CHECK_EQ(pal[0], 0);
	CHECK_EQ(pal[1], 0);
}
