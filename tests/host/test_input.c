// Key repeat, paging and list wrapping (source/engine/input.c).
#include <string.h>

#include "../../source/engine/input.h"
#include "../kernel/torii_test.h"
#include "tests.h"

// Holds keys for frames frames, one step each, and lists the frames
// (counted from 0) on which key fired. Returns how many there were.
static u32 hold(InputState *s, u16 keys, u16 key, u32 frames, u32 *fired, u32 max)
{
	u32 n = 0;
	for (u32 f = 0; f < frames; f++) {
		input_step(s, keys, 1);
		if (input_fired(s, key) && n < max)
			fired[n++] = f;
	}
	return n;
}

static void check_frames(const u32 *got, u32 n, const u32 *want, u32 count)
{
	CHECK(n >= count);
	for (u32 i = 0; i < count && i < n; i++)
		CHECK_EQ(got[i], want[i]);
}

// A steady repeat every 4 frames, as before the ramp: the wrap tests count
// frames on it
static const InputTiming steady = {15, 4, 0, 15, 20, 8};

void test_input_tap(void)
{
	InputState s;
	input_init(&s, NULL);
	CHECK(s.timing == &input_timing_default);
	input_step(&s, INPUT_DOWN, 1);
	CHECK_EQ(s.pressed, INPUT_DOWN);
	CHECK_EQ(s.fire, INPUT_DOWN);  // acts on the press frame
	input_step(&s, INPUT_DOWN, 1);
	CHECK_EQ(s.pressed, 0);
	CHECK_EQ(s.fire, 0);
	input_step(&s, 0, 1);
	CHECK_EQ(s.released, INPUT_DOWN);
	CHECK_EQ(s.fire, 0);
	// Two taps on consecutive frames both act.
	input_step(&s, INPUT_DOWN, 1);
	CHECK(input_fired(&s, INPUT_DOWN));
	input_step(&s, 0, 1);
	input_step(&s, INPUT_DOWN, 1);
	CHECK(input_fired(&s, INPUT_DOWN));
	// Keys outside the d-pad and L/R fire only on the press.
	input_init(&s, NULL);
	u32 fired[8];
	CHECK_EQ(hold(&s, INPUT_A | INPUT_START, INPUT_A | INPUT_START, 300, fired, 8), 1);
	CHECK_EQ(fired[0], 0);
	// Bits past L are ignored.
	input_step(&s, 0xFC00, 1);
	CHECK_EQ(s.held, 0);
}

// The acceleration curve: from a repeat every interval frames to top_speed
// a second over ramp frames, rising smoothly and never falling
void test_input_ramp_curve(void)
{
	const InputTiming *t = &input_timing_default;
	u32 from = INPUT_ONE / t->interval, to = (INPUT_ONE * t->top_speed + 59) / 60;
	CHECK_EQ(input_ramp_rate(t, 0), from);
	CHECK_EQ(input_ramp_rate(t, t->ramp), to);
	CHECK_EQ(input_ramp_rate(t, 1000), to);
	// 30 a second at the top: exactly every other frame, so a 16 px row
	// list glides a whole 8 px every frame (40 alternated 11, 11, 10)
	CHECK_EQ(t->top_speed, 30);
	CHECK_EQ(to, INPUT_ONE / 2);
	static const InputTiming every = {15, 4, 90, 60, 20, 6};
	CHECK_EQ(input_ramp_rate(&every, 90), INPUT_ONE);  // 60 a second: every frame
	// Smoothstep's steepest slope is 1.5 / ramp of the rise: no frame jumps
	// more than that, and the curve starts and ends flat
	u32 most = (to - from) * 3 / (2 * t->ramp) + 2;
	u32 last = input_ramp_rate(t, 0);
	for (u32 f = 1; f <= t->ramp; f++) {
		u32 r = input_ramp_rate(t, f);
		CHECK(r >= last);
		CHECK(r - last <= most);
		if (f == 1 || f == t->ramp)
			CHECK(r - last < most / 8);
		last = r;
	}
	// Halfway it is halfway
	u32 mid = input_ramp_rate(t, t->ramp / 2);
	CHECK(mid > from + (to - from) / 2 - 2 && mid < from + (to - from) / 2 + 2);
	// Out-of-range timings: top speed 0 counts as 1, past 60 as 60;
	// interval 0 as 1; ramp 0 is the top at once
	static const InputTiming odd = {15, 0, 0, 0, 20, 0};
	CHECK_EQ(input_ramp_rate(&odd, 0), (INPUT_ONE + 59) / 60);
	static const InputTiming fast = {15, 4, 30, 200, 20, 6};
	CHECK_EQ(input_ramp_rate(&fast, 30), INPUT_ONE);
	// A top speed under the start speed ramps down
	static const InputTiming down = {15, 2, 10, 6, 20, 6};
	CHECK(input_ramp_rate(&down, 5) < INPUT_ONE / 2);
	CHECK_EQ(input_ramp_rate(&down, 10), (INPUT_ONE * 6 + 59) / 60);
}

void test_input_repeat_schedule(void)
{
	u32 fired[256];
	InputState s;
	input_init(&s, NULL);
	u32 n = hold(&s, INPUT_DOWN, INPUT_DOWN, 180, fired, 256);
	// The press, delay frames on, then a repeat every 4 frames at first
	static const u32 start[] = {0, 15, 19, 23, 27};
	check_frames(fired, n, start, 5);
	// The gaps close up: none is more than a frame longer than one before
	// it (a rate between whole frames alternates them), and once the ramp
	// is over (15 + 54 frames) it repeats 30 times a second, every other
	// frame exactly
	u32 shortest = 4;
	for (u32 i = 2; i < n; i++) {
		u32 gap = fired[i] - fired[i - 1];
		CHECK(gap <= shortest + 1);
		if (gap < shortest)
			shortest = gap;
	}
	u32 top = 0;
	for (u32 i = 1; i < n; i++) {
		if (fired[i] >= 15 + 54 + 2) {
			top++;
			CHECK_EQ(fired[i] - fired[i - 1], 2u);
		}
	}
	CHECK(top >= 54 && top <= 55);  // 109 frames at 30 a second
	// About as many repeats as the curve's area: 20 in the 0.9 s ramp's 54
	// frames (the average of 1/4 and 1/2 a frame, as the curve is symmetric)
	u32 in_ramp = 0;
	for (u32 i = 0; i < n; i++)
		in_ramp += fired[i] > 15 && fired[i] <= 69;
	CHECK(in_ramp >= 19 && in_ramp <= 22);

	// The phase rises between repeats at the rate and drops on each:
	// rows scrolled at phase * row height move at a steady speed
	input_init(&s, NULL);
	CHECK_EQ(input_phase(&s, INPUT_DOWN), 0u);
	u32 last = 0;
	for (u32 f = 0; f < 120; f++) {
		input_step(&s, INPUT_DOWN, 1);
		u32 p = input_phase(&s, INPUT_DOWN), r = input_rate(&s, INPUT_DOWN);
		if (f < 15) {
			CHECK_EQ(p, 0u);  // not repeating yet
			CHECK_EQ(r, 0u);
		} else if (f > 15) {
			CHECK(r >= INPUT_ONE / 4 && r <= INPUT_ONE);
			// phase now = phase before + rate, less one on a repeat
			u32 want = last + r;
			if (input_fired(&s, INPUT_DOWN))
				want -= INPUT_ONE;
			CHECK_EQ(p, want);
		}
		CHECK(p < INPUT_ONE);
		last = p;
	}
	CHECK_EQ(input_phase(&s, INPUT_UP), 0u);
	CHECK_EQ(input_phase(&s, INPUT_DOWN | INPUT_UP), 0u);  // one key at a time
	input_step(&s, 0, 1);
	CHECK_EQ(input_phase(&s, INPUT_DOWN), 0u);  // released

	// Steps of 3 frames fire at most once each, on the first step at or past
	// the due frame, and the rate counts the frames: held 15 (due 15), 21
	// (3/4 of a repeat at 18, 6/4 at 21), 24 (5/4 less 1 carried, plus 3/4)
	input_init(&s, &steady);
	u32 held = 0, count = 0;
	u32 at[8];
	input_step(&s, INPUT_DOWN, 3);  // the press
	CHECK(input_fired(&s, INPUT_DOWN));
	while (held < 24) {
		input_step(&s, INPUT_DOWN, 3);
		held += 3;
		if (input_fired(&s, INPUT_DOWN) && count < 8)
			at[count++] = held;
	}
	static const u32 want3[] = {15, 21, 24};
	CHECK_EQ(count, 3);
	check_frames(at, count, want3, 3);
	// At most one repeat a step, with dt 3 at full speed too
	input_init(&s, NULL);
	for (u32 i = 0; i < 100; i++) {
		input_step(&s, INPUT_DOWN, 3);
		CHECK(input_phase(&s, INPUT_DOWN) < INPUT_ONE);
	}
	// dt past 3 counts as 3.
	input_init(&s, NULL);
	input_step(&s, INPUT_DOWN, 9);
	for (u32 i = 0; i < 4; i++) {
		input_step(&s, INPUT_DOWN, 9);
		CHECK(!input_fired(&s, INPUT_DOWN));
	}
	input_step(&s, INPUT_DOWN, 9);  // held 15
	CHECK(input_fired(&s, INPUT_DOWN));
}

void test_input_accel_reset(void)
{
	u32 fired[64];
	InputState s;
	input_init(&s, NULL);
	hold(&s, INPUT_DOWN, INPUT_DOWN, 150, fired, 64);  // at full speed
	// A release starts over.
	input_step(&s, 0, 1);
	static const u32 again[] = {0, 15, 19};
	u32 n = hold(&s, INPUT_DOWN, INPUT_DOWN, 20, fired, 64);
	CHECK_EQ(n, 3);
	check_frames(fired, n, again, 3);

	// A new direction starts over, for it and for the one still held.
	input_init(&s, NULL);
	hold(&s, INPUT_DOWN, INPUT_DOWN, 150, fired, 64);
	u32 down[8], right[8], nd = 0, nr = 0;
	for (u32 f = 0; f < 20; f++) {
		input_step(&s, INPUT_DOWN | INPUT_RIGHT, 1);
		if (input_fired(&s, INPUT_DOWN) && nd < 8)
			down[nd++] = f;
		if (input_fired(&s, INPUT_RIGHT) && nr < 8)
			right[nr++] = f;
	}
	static const u32 r_want[] = {0, 15, 19};
	static const u32 d_want[] = {15, 19};
	CHECK_EQ(nr, 3);
	check_frames(right, nr, r_want, 3);
	CHECK_EQ(nd, 2);
	check_frames(down, nd, d_want, 2);
	// Letting go of the new one starts the old one's schedule over again:
	// its next repeat comes delay frames after the release.
	input_step(&s, INPUT_DOWN, 1);
	CHECK(!input_fired(&s, INPUT_DOWN));
	CHECK_EQ(input_phase(&s, INPUT_DOWN), 0u);
	n = hold(&s, INPUT_DOWN, INPUT_DOWN, 20, fired, 64);
	CHECK_EQ(n, 2);
	static const u32 after_release[] = {14, 18};
	check_frames(fired, n, after_release, 2);
	// L and R don't reset the d-pad.
	input_init(&s, NULL);
	hold(&s, INPUT_DOWN, INPUT_DOWN, 150, fired, 64);
	input_step(&s, INPUT_DOWN | INPUT_L, 1);
	CHECK(input_fired(&s, INPUT_L));
	n = hold(&s, INPUT_DOWN | INPUT_L, INPUT_DOWN, 8, fired, 64);
	CHECK_EQ(n, 4);  // still at top speed, every other frame
}

void test_input_paging(void)
{
	// 20 frames, then every 6, with no speed-up.
	static const u32 want[] = {0, 20, 26, 32, 38, 44, 50, 56, 62, 68, 74, 80, 86, 92, 98, 104, 110, 116};
	u32 fired[64];
	InputState s;
	input_init(&s, NULL);
	u32 n = hold(&s, INPUT_L, INPUT_L, 120, fired, 64);
	CHECK_EQ(n, sizeof(want) / sizeof(*want));
	check_frames(fired, n, want, sizeof(want) / sizeof(*want));
	input_init(&s, NULL);
	n = hold(&s, INPUT_R, INPUT_R, 120, fired, 64);
	check_frames(fired, n, want, sizeof(want) / sizeof(*want));
	// The phase gives the scroll between pages
	CHECK_EQ(input_rate(&s, INPUT_R), (INPUT_ONE + 5) / 6);
	// LEFT and RIGHT page where a screen says so, on the same schedule,
	// with no effect on UP and DOWN
	input_init(&s, NULL);
	input_page_keys(&s, INPUT_LEFT | INPUT_RIGHT | INPUT_A);
	CHECK_EQ(s.page_keys, INPUT_PAGE | INPUT_LEFT | INPUT_RIGHT);
	n = hold(&s, INPUT_RIGHT, INPUT_RIGHT, 120, fired, 64);
	CHECK_EQ(n, sizeof(want) / sizeof(*want));
	check_frames(fired, n, want, sizeof(want) / sizeof(*want));
	input_step(&s, 0, 1);
	static const u32 dpad[] = {0, 15, 19};
	n = hold(&s, INPUT_DOWN, INPUT_DOWN, 20, fired, 64);
	check_frames(fired, n, dpad, 3);
	input_page_keys(&s, 0);
	CHECK_EQ(s.page_keys, INPUT_PAGE);
	// Timings come from the view's own table.
	static const InputTiming slow = {30, 10, 0, 6, 40, 20};
	static const u32 slow_want[] = {0, 30, 40, 50};
	static const u32 slow_page[] = {0, 40, 60};
	input_init(&s, &slow);
	n = hold(&s, INPUT_DOWN, INPUT_DOWN, 51, fired, 64);
	CHECK_EQ(n, 4);
	check_frames(fired, n, slow_want, 4);
	input_init(&s, &slow);
	n = hold(&s, INPUT_R, INPUT_R, 61, fired, 64);
	CHECK_EQ(n, 3);
	check_frames(fired, n, slow_page, 3);
	// Intervals of 0 count as 1, so a key can't fire forever on one frame.
	static const InputTiming zero = {0, 0, 0, 60, 0, 0};
	input_init(&s, &zero);
	n = hold(&s, INPUT_DOWN, INPUT_DOWN, 5, fired, 64);
	CHECK_EQ(n, 5);
	input_init(&s, &zero);
	n = hold(&s, INPUT_L, INPUT_L, 5, fired, 64);
	CHECK_EQ(n, 5);
}

// Holds key, moving a cursor in a list of count from cursor, for frames
// frames; writes where it is after each frame to at.
static void scroll(InputState *s, u16 key, s32 delta, u32 *cursor, u32 count, u32 frames, u32 *at)
{
	for (u32 f = 0; f < frames; f++) {
		input_step(s, key, 1);
		if (input_fired(s, key))
			*cursor = input_move(s, key, *cursor, count, delta);
		at[f] = *cursor;
	}
}

// One rule for every list and menu (v0.3.2): a held key stops at the end
// and stays there however long it rests; a fresh press at the end wraps.
void test_input_wrap(void)
{
	InputState s;
	input_init(&s, &steady);
	u32 at[64], cursor = 0;
	// Held down a list of 5: repeats stop at the end (frame 23) and stay
	// there (v0.3's menus wrapped after 15 frames' rest, at frame 39).
	scroll(&s, INPUT_DOWN, 1, &cursor, 5, 64, at);
	CHECK_EQ(at[0], 1);
	CHECK_EQ(at[14], 1);
	CHECK_EQ(at[15], 2);
	CHECK_EQ(at[19], 3);
	CHECK_EQ(at[23], 4);
	CHECK_EQ(at[39], 4);
	CHECK_EQ(at[63], 4);
	// Held up, the same at the top.
	input_step(&s, 0, 1);
	scroll(&s, INPUT_UP, -1, &cursor, 5, 64, at);
	CHECK_EQ(at[0], 3);
	CHECK_EQ(at[27], 0);
	CHECK_EQ(at[63], 0);

	// A fresh press at the end wraps at once, either way.
	input_step(&s, 0, 1);
	cursor = 4;
	scroll(&s, INPUT_DOWN, 1, &cursor, 5, 1, at);
	CHECK_EQ(cursor, 0);
	input_step(&s, 0, 1);
	scroll(&s, INPUT_UP, -1, &cursor, 5, 1, at);
	CHECK_EQ(cursor, 4);
	// And only the key pressed counts as fresh: UP held, DOWN's press
	// doesn't let UP's repeat wrap.
	input_init(&s, &steady);
	cursor = 0;
	for (u32 f = 0; f < 16; f++)
		input_step(&s, INPUT_UP, 1);  // the repeat at 15: UP fires, not freshly
	CHECK(input_fired(&s, INPUT_UP) && !(s.pressed & INPUT_UP));
	CHECK_EQ(input_move(&s, INPUT_UP, cursor, 5, -1), 0);
	input_step(&s, INPUT_UP | INPUT_DOWN, 1);  // DOWN pressed
	CHECK_EQ(input_move(&s, INPUT_UP, cursor, 5, -1), 0);
	CHECK_EQ(input_move(&s, INPUT_DOWN, 4, 5, 1), 0);

	// Paging stops at the end first, even on a fresh press, then wraps.
	input_init(&s, &steady);
	input_step(&s, 0, 1);
	cursor = 3;
	scroll(&s, INPUT_L, -10, &cursor, 20, 1, at);
	CHECK_EQ(cursor, 0);
	input_step(&s, 0, 1);
	scroll(&s, INPUT_L, -10, &cursor, 20, 1, at);
	CHECK_EQ(cursor, 19);
	input_step(&s, 0, 1);
	scroll(&s, INPUT_R, 10, &cursor, 20, 1, at);
	CHECK_EQ(cursor, 0);
	scroll(&s, INPUT_R, 10, &cursor, 20, 60, at);  // held: 10 at frame 19, 19 at 27, and there
	CHECK_EQ(at[19], 10);
	CHECK_EQ(at[27], 19);
	CHECK_EQ(cursor, 19);

	// Edge cases: empty and one-item lists, a cursor past the end.
	input_init(&s, &steady);
	input_step(&s, INPUT_DOWN, 1);
	CHECK_EQ(input_move(&s, INPUT_DOWN, 5, 0, 1), 0);
	CHECK_EQ(input_move(&s, INPUT_DOWN, 0, 1, 1), 0);
	CHECK_EQ(input_move(&s, INPUT_DOWN, 0, 1, -1), 0);
	CHECK_EQ(input_move(&s, INPUT_DOWN, 9, 3, 1), 0);  // 9 counts as 2, the end: fresh, so it wraps
	CHECK_EQ(input_move(&s, INPUT_DOWN, 1, 3, -0x7FFFFFFF - 1), 0);  // the most negative delta
	for (u32 f = 0; f < 15; f++)
		input_step(&s, INPUT_DOWN, 1);  // the repeat at 15
	CHECK(input_fired(&s, INPUT_DOWN) && !(s.pressed & INPUT_DOWN));
	CHECK_EQ(input_move(&s, INPUT_DOWN, 9, 3, 1), 2);  // held: stays at the end
}
