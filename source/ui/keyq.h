// Keys between views: what the frame loop (ui_run.c) does with keys that
// fire while no view takes them (a screen going out, a dialog closing) and
// with keys held back for a later frame. They wait as a count per key, up
// to two: a second press of a key that already waits goes the frame after
// the first, so two quick presses while a screen goes out are two moves,
// not one. Only keys that move count twice (the d-pad, L and R): a second
// A, B or START would act on a screen the user hasn't seen yet (a double A
// on a folder would open its first game). Repeats of a held key count once,
// as before. Hardware-free, so tests/host runs it.
#ifndef TORII_KEYQ_H
#define TORII_KEYQ_H

#include <gba_base.h>

#define KEYQ_TWICE 0x3F0  // the d-pad, R and L (input.h's bits)

struct keyq {
	u16 held[2];    // fired while no view took keys: [1] those pressed again
	u16 queued[2];  // for the view on top: [0] next frame, [1] the frame after
	u16 seconds;    // of queued[0]: second presses moved up by keyq_take
};

void keyq_clear(struct keyq *q);
// A frame in which no view takes keys: fire waits for the next view. A key
// of pressed that already waits counts a second time, if it is one of
// KEYQ_TWICE.
void keyq_hold(struct keyq *q, u16 fire, u16 pressed);
// Keys for the view on top, next frame (ui_queue_keys); one already queued
// goes again the frame after.
void keyq_queue(struct keyq *q, u16 keys);
// The next view is up: what waited for it is queued for it.
void keyq_release(struct keyq *q);
// The view on top goes before it had its queued keys: they wait for the
// next one. A second press whose first the view already had is dropped: the
// first may have been part of what made it go (DOWN, then L back to the SD
// list, both in one frame), and the order of the two against other keys
// is lost.
void keyq_hold_queued(struct keyq *q);
// The queued keys due this frame; those behind them move up a frame.
u16 keyq_take(struct keyq *q);
// Forgets what is queued for the view on top (it popped), not what waits
// for the next view.
void keyq_drop_queued(struct keyq *q);

#endif
