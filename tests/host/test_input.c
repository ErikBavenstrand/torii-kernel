// Key repeat, paging and list wrapping (source/engine/input.c).
#include <string.h>

#include "../../source/engine/input.h"
#include "../kernel/torii_test.h"
#include "tests.h"

// Holds keys for frames frames, one step each, and lists the frames
// (counted from 0) on which key fired. Returns how many there were.
static u32 hold(InputState *s, u16 keys, u16 key, u32 frames, int skip, u32 *fired, u32 max)
{
	u32 n = 0;
	for (u32 f = 0; f < frames; f++) {
		input_step(s, keys, 1, skip);
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

void test_input_tap(void)
{
	InputState s;
	input_init(&s, NULL);
	CHECK(s.timing == &input_timing_default);
	input_step(&s, INPUT_DOWN, 1, 0);
	CHECK_EQ(s.pressed, INPUT_DOWN);
	CHECK_EQ(s.fire, INPUT_DOWN);  // acts on the press frame
	input_step(&s, INPUT_DOWN, 1, 0);
	CHECK_EQ(s.pressed, 0);
	CHECK_EQ(s.fire, 0);
	input_step(&s, 0, 1, 0);
	CHECK_EQ(s.released, INPUT_DOWN);
	CHECK_EQ(s.fire, 0);
	// Two taps on consecutive frames both act.
	input_step(&s, INPUT_DOWN, 1, 0);
	CHECK(input_fired(&s, INPUT_DOWN));
	input_step(&s, 0, 1, 0);
	input_step(&s, INPUT_DOWN, 1, 0);
	CHECK(input_fired(&s, INPUT_DOWN));
	// Keys outside the d-pad and L/R fire only on the press.
	input_init(&s, NULL);
	u32 fired[8];
	CHECK_EQ(hold(&s, INPUT_A | INPUT_START, INPUT_A | INPUT_START, 300, 0, fired, 8), 1);
	CHECK_EQ(fired[0], 0);
	// Bits past L are ignored.
	input_step(&s, 0xFC00, 1, 0);
	CHECK_EQ(s.held, 0);
}

void test_input_repeat_schedule(void)
{
	// 15 frames, then every 4, every 2 once a repeat would fall at 60 or later.
	static const u32 want[] = {0, 15, 19, 23, 27, 31, 35, 39, 43, 47, 51, 55, 59, 61, 63, 65, 67, 69, 71};
	u32 fired[64];
	InputState s;
	input_init(&s, NULL);
	u32 n = hold(&s, INPUT_DOWN, INPUT_DOWN, 72, 0, fired, 64);
	CHECK_EQ(n, sizeof(want) / sizeof(*want));
	check_frames(fired, n, want, sizeof(want) / sizeof(*want));

	// With the animation skipped: no change before 60 frames, then every frame.
	static const u32 skip[] = {0, 15, 19, 23, 27, 31, 35, 39, 43, 47, 51, 55, 59, 60, 61, 62, 63, 64, 65, 66, 67};
	input_init(&s, NULL);
	n = hold(&s, INPUT_UP, INPUT_UP, 68, 1, fired, 64);
	CHECK_EQ(n, sizeof(skip) / sizeof(*skip));
	check_frames(fired, n, skip, sizeof(skip) / sizeof(*skip));

	// Steps of 3 frames fire at most once each, on the first step at or past
	// the due frame, and the next is due an interval after that step: held
	// 15 (due 15), 21 (due 19), 27 (due 25).
	input_init(&s, NULL);
	u32 held = 0, count = 0;
	u32 at[8];
	input_step(&s, INPUT_DOWN, 3, 0);  // the press
	CHECK(input_fired(&s, INPUT_DOWN));
	while (held < 30) {
		input_step(&s, INPUT_DOWN, 3, 0);
		held += 3;
		if (input_fired(&s, INPUT_DOWN) && count < 8)
			at[count++] = held;
	}
	static const u32 want3[] = {15, 21, 27};
	CHECK_EQ(count, 3);
	check_frames(at, count, want3, 3);
	// dt past 3 counts as 3.
	input_init(&s, NULL);
	input_step(&s, INPUT_DOWN, 9, 0);
	for (u32 i = 0; i < 4; i++) {
		input_step(&s, INPUT_DOWN, 9, 0);
		CHECK(!input_fired(&s, INPUT_DOWN));
	}
	input_step(&s, INPUT_DOWN, 9, 0);  // held 15
	CHECK(input_fired(&s, INPUT_DOWN));
}

void test_input_accel_reset(void)
{
	u32 fired[64];
	InputState s;
	input_init(&s, NULL);
	hold(&s, INPUT_DOWN, INPUT_DOWN, 100, 1, fired, 64);  // at full speed
	// A release starts over.
	input_step(&s, 0, 1, 1);
	static const u32 again[] = {0, 15, 19};
	u32 n = hold(&s, INPUT_DOWN, INPUT_DOWN, 20, 1, fired, 64);
	CHECK_EQ(n, 3);
	check_frames(fired, n, again, 3);

	// A new direction starts over, for it and for the one still held.
	input_init(&s, NULL);
	hold(&s, INPUT_DOWN, INPUT_DOWN, 100, 1, fired, 64);
	u32 down[8], right[8], nd = 0, nr = 0;
	for (u32 f = 0; f < 20; f++) {
		input_step(&s, INPUT_DOWN | INPUT_RIGHT, 1, 1);
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
	// Letting go of the new one leaves the old one on its fresh schedule.
	input_step(&s, INPUT_DOWN, 1, 1);  // frame 20 of the new schedule
	CHECK(!input_fired(&s, INPUT_DOWN));
	// L and R don't reset the d-pad.
	input_init(&s, NULL);
	hold(&s, INPUT_DOWN, INPUT_DOWN, 100, 0, fired, 64);
	input_step(&s, INPUT_DOWN | INPUT_L, 1, 0);
	CHECK(input_fired(&s, INPUT_L));
	n = hold(&s, INPUT_DOWN | INPUT_L, INPUT_DOWN, 8, 0, fired, 64);
	CHECK_EQ(n, 4);  // still every 2 frames
}

void test_input_paging(void)
{
	// 20 frames, then every 8, with no speed-up and no skip.
	static const u32 want[] = {0, 20, 28, 36, 44, 52, 60, 68, 76, 84, 92, 100, 108, 116};
	u32 fired[64];
	InputState s;
	input_init(&s, NULL);
	u32 n = hold(&s, INPUT_L, INPUT_L, 120, 1, fired, 64);
	CHECK_EQ(n, sizeof(want) / sizeof(*want));
	check_frames(fired, n, want, sizeof(want) / sizeof(*want));
	input_init(&s, NULL);
	n = hold(&s, INPUT_R, INPUT_R, 120, 0, fired, 64);
	check_frames(fired, n, want, sizeof(want) / sizeof(*want));
	// Timings come from the view's own table.
	static const InputTiming slow = {30, 10, 200, 5, 1, 40, 20, 12};
	static const u32 slow_want[] = {0, 30, 40, 50};
	static const u32 slow_page[] = {0, 40, 60};
	input_init(&s, &slow);
	n = hold(&s, INPUT_DOWN, INPUT_DOWN, 51, 1, fired, 64);
	CHECK_EQ(n, 4);
	check_frames(fired, n, slow_want, 4);
	input_init(&s, &slow);
	n = hold(&s, INPUT_R, INPUT_R, 61, 1, fired, 64);
	CHECK_EQ(n, 3);
	check_frames(fired, n, slow_page, 3);
	// An interval of 0 counts as 1, so a key can't fire forever on one frame.
	static const InputTiming zero = {0, 0, 0, 0, 0, 0, 0, 0};
	input_init(&s, &zero);
	n = hold(&s, INPUT_DOWN, INPUT_DOWN, 5, 0, fired, 64);
	CHECK_EQ(n, 5);
}

// Holds key, moving a cursor in a list of count from cursor through gate w,
// for frames frames; writes where it is after each frame to at.
static void scroll(InputState *s, InputWrap *w, u16 key, s32 delta, u32 *cursor, u32 count, u32 frames, u32 *at)
{
	for (u32 f = 0; f < frames; f++) {
		input_step(s, key, 1, 0);
		if (input_fired(s, key))
			*cursor = input_move(s, w, key, *cursor, count, delta);
		at[f] = *cursor;
	}
}

void test_input_wrap(void)
{
	InputState s;
	InputWrap w;
	input_init(&s, NULL);
	u32 at[64], cursor = 0;
	input_wrap_init(&w, &s, cursor);
	// Held down a list of 5: repeats stop at the end (frame 23); after
	// resting there 15 frames, the next repeat (frame 39) wraps.
	scroll(&s, &w, INPUT_DOWN, 1, &cursor, 5, 48, at);
	CHECK_EQ(at[0], 1);
	CHECK_EQ(at[14], 1);
	CHECK_EQ(at[15], 2);
	CHECK_EQ(at[19], 3);
	CHECK_EQ(at[23], 4);
	CHECK_EQ(at[27], 4);
	CHECK_EQ(at[35], 4);
	CHECK_EQ(at[38], 4);
	CHECK_EQ(at[39], 0);
	CHECK_EQ(at[43], 1);
	CHECK_EQ(at[47], 2);

	// A fresh press at the end wraps at once, either way.
	input_step(&s, 0, 1, 0);
	cursor = 4;
	scroll(&s, &w, INPUT_DOWN, 1, &cursor, 5, 1, at);
	CHECK_EQ(cursor, 0);
	input_step(&s, 0, 1, 0);
	scroll(&s, &w, INPUT_UP, -1, &cursor, 5, 1, at);
	CHECK_EQ(cursor, 4);

	// Paging stops at the end first, even on a fresh press, then wraps.
	input_step(&s, 0, 1, 0);
	cursor = 3;
	scroll(&s, &w, INPUT_L, -10, &cursor, 20, 1, at);
	CHECK_EQ(cursor, 0);
	input_step(&s, 0, 1, 0);
	scroll(&s, &w, INPUT_L, -10, &cursor, 20, 1, at);
	CHECK_EQ(cursor, 19);
	input_step(&s, 0, 1, 0);
	scroll(&s, &w, INPUT_R, 10, &cursor, 20, 1, at);
	CHECK_EQ(cursor, 0);
	scroll(&s, &w, INPUT_R, 10, &cursor, 20, 21, at);  // held: 10 at frame 20
	CHECK_EQ(cursor, 10);

	// A cursor put at the end by other means has just arrived: a repeat
	// leaves it there.
	input_init(&s, NULL);
	cursor = 0;
	input_wrap_init(&w, &s, cursor);
	scroll(&s, &w, INPUT_DOWN, 1, &cursor, 50, 15, at);  // held, repeating
	CHECK_EQ(cursor, 1);
	cursor = 49;
	scroll(&s, &w, INPUT_DOWN, 1, &cursor, 50, 1, at);  // the repeat at 15
	CHECK_EQ(cursor, 49);
	// So does one whose gate was just set up, however late.
	for (u32 i = 0; i < 100; i++)
		input_step(&s, INPUT_DOWN, 1, 0);
	input_wrap_init(&w, &s, 49);
	cursor = 49;
	scroll(&s, &w, INPUT_DOWN, 1, &cursor, 50, 2, at);  // a repeat in these 2 frames
	CHECK_EQ(cursor, 49);

	// Two cursors keep gates of their own: one resting at its end doesn't
	// let the other wrap the moment it arrives.
	InputWrap rows, cols;
	u32 row = 0, col = 1;
	input_init(&s, NULL);
	input_wrap_init(&rows, &s, row);
	input_wrap_init(&cols, &s, col);
	for (u32 f = 0; f < 40; f++)
		input_step(&s, 0, 1, 0);
	for (u32 f = 0; f < 16; f++) {
		input_step(&s, INPUT_RIGHT, 1, 0);
		if (input_fired(&s, INPUT_RIGHT))
			col = input_move(&s, &cols, INPUT_RIGHT, col, 4, 1);
		if (f == 15)
			row = input_move(&s, &rows, INPUT_RIGHT, row, 4, -1);  // row 0 rested 55 frames: wraps
	}
	CHECK_EQ(col, 3);  // 2 on the press, 3 (the end) on the repeat at 15
	CHECK_EQ(row, 3);
	for (u32 f = 0; f < 4; f++) {  // the repeat at 19, 4 frames after col arrived
		input_step(&s, INPUT_RIGHT, 1, 0);
		if (input_fired(&s, INPUT_RIGHT))
			col = input_move(&s, &cols, INPUT_RIGHT, col, 4, 1);
	}
	CHECK_EQ(col, 3);

	// Edge cases: empty and one-item lists, a cursor past the end.
	input_init(&s, NULL);
	input_wrap_init(&w, &s, 0);
	input_step(&s, INPUT_DOWN, 1, 0);
	CHECK_EQ(input_move(&s, &w, INPUT_DOWN, 5, 0, 1), 0);
	CHECK_EQ(input_move(&s, &w, INPUT_DOWN, 0, 1, 1), 0);
	CHECK_EQ(input_move(&s, &w, INPUT_DOWN, 0, 1, -1), 0);
	CHECK_EQ(input_move(&s, &w, INPUT_DOWN, 9, 3, 1), 0);  // 9 counts as 2, the end: fresh, so it wraps
	CHECK_EQ(input_move(&s, &w, INPUT_DOWN, 1, 3, -0x7FFFFFFF - 1), 0);  // the most negative delta
}
