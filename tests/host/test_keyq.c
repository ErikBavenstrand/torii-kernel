// Keys between views (source/ui/keyq.c), stepped as ui_frame steps them.
#include "../../source/engine/input.h"
#include "../../source/ui/keyq.h"
#include "../kernel/torii_test.h"
#include "tests.h"

// One frame of ui_frame's routing: keys held this frame; gone, no view takes
// keys (a screen going out). Returns the keys dispatched.
static u16 frame(InputState *s, struct keyq *q, u16 keys, int gone)
{
	input_step(s, keys, 1);
	if (gone) {
		keyq_hold(q, s->fire, s->pressed);
		return 0;
	}
	return (u16)(s->fire | keyq_take(q));
}

// Two presses of one key while a screen goes out are two moves on the next
// screen, a frame apart (they were one before v0.3.2).
void test_keyq_two_presses(void)
{
	InputState s;
	struct keyq q;
	input_init(&s, 0);
	keyq_clear(&q);
	CHECK_EQ(frame(&s, &q, INPUT_DOWN, 1), 0);
	CHECK_EQ(frame(&s, &q, 0, 1), 0);
	CHECK_EQ(frame(&s, &q, INPUT_DOWN, 1), 0);
	CHECK_EQ(frame(&s, &q, 0, 1), 0);
	CHECK_EQ(frame(&s, &q, INPUT_DOWN, 1), 0);  // a third: still two
	CHECK_EQ(frame(&s, &q, 0, 1), 0);
	keyq_release(&q);  // the next screen is up
	CHECK_EQ(frame(&s, &q, 0, 0), INPUT_DOWN);
	CHECK_EQ(frame(&s, &q, 0, 0), INPUT_DOWN);
	CHECK_EQ(frame(&s, &q, 0, 0), 0);

	// Two keys, one pressed twice: both go first, the second press after.
	keyq_clear(&q);
	frame(&s, &q, INPUT_A, 1);
	frame(&s, &q, INPUT_A | INPUT_R, 1);  // R, and A still held: A counts once
	frame(&s, &q, 0, 1);
	frame(&s, &q, INPUT_R, 1);
	frame(&s, &q, 0, 1);
	keyq_release(&q);
	CHECK_EQ(frame(&s, &q, 0, 0), INPUT_R | INPUT_A);
	CHECK_EQ(frame(&s, &q, 0, 0), INPUT_R);
	CHECK_EQ(frame(&s, &q, 0, 0), 0);

	// A, B and START count once, however often pressed: a second A would
	// act on a screen not seen yet.
	keyq_clear(&q);
	for (u32 i = 0; i < 2; i++) {
		frame(&s, &q, INPUT_A | INPUT_B | INPUT_START, 1);
		frame(&s, &q, 0, 1);
	}
	keyq_release(&q);
	CHECK_EQ(frame(&s, &q, 0, 0), INPUT_A | INPUT_B | INPUT_START);
	CHECK_EQ(frame(&s, &q, 0, 0), 0);
}

// A held key's repeats while the screen goes out are one move, as before.
void test_keyq_held_repeats(void)
{
	InputState s;
	struct keyq q;
	input_init(&s, 0);
	keyq_clear(&q);
	for (u32 f = 0; f < 60; f++)
		frame(&s, &q, INPUT_DOWN, 1);  // the press and repeats from frame 15
	keyq_release(&q);
	CHECK_EQ(keyq_take(&q), INPUT_DOWN);
	CHECK_EQ(keyq_take(&q), 0);
}

// The queue carries over screens: a screen that goes before it had its
// keys hands them to the next (ui_leave), counts kept; a pop forgets only
// what was queued for the view that went.
void test_keyq_carry(void)
{
	InputState s;
	struct keyq q;
	input_init(&s, 0);
	keyq_clear(&q);
	frame(&s, &q, INPUT_R, 1);
	frame(&s, &q, 0, 1);
	frame(&s, &q, INPUT_R, 1);
	keyq_release(&q);
	CHECK_EQ(frame(&s, &q, 0, 0), INPUT_R);  // the NOR list: R to SETTINGS...
	keyq_hold_queued(&q);                    // ...which goes out at once
	keyq_release(&q);
	CHECK_EQ(frame(&s, &q, 0, 0), INPUT_R);  // SETTINGS gets the second R
	CHECK_EQ(frame(&s, &q, 0, 0), 0);

	// Queued keys go whatever fires that frame: ui_frame no longer drops
	// them while DIM is on (it did before v0.3.2), and they wake it.
	keyq_clear(&q);
	keyq_queue(&q, INPUT_A);
	CHECK_EQ(frame(&s, &q, 0, 0), INPUT_A);
	keyq_queue(&q, INPUT_A);
	keyq_queue(&q, INPUT_A);
	keyq_queue(&q, INPUT_A);  // up to two
	CHECK_EQ(keyq_take(&q), INPUT_A);
	CHECK_EQ(keyq_take(&q), INPUT_A);
	CHECK_EQ(keyq_take(&q), 0);

	// A pop forgets what was queued, not what waits for the next view.
	keyq_clear(&q);
	keyq_queue(&q, INPUT_B);
	frame(&s, &q, INPUT_A, 1);
	keyq_drop_queued(&q);
	keyq_release(&q);
	CHECK_EQ(keyq_take(&q), INPUT_A);
	CHECK_EQ(keyq_take(&q), 0);
}
