#include "keyq.h"

// Adds keys (counted once, those in again a second time) to the two-deep
// set one/two, each key's count capped at two
static void add(u16 *one, u16 *two, u16 keys, u16 again)
{
	*two |= (u16)((*one & keys) | again);
	*one |= keys;
}

void keyq_clear(struct keyq *q)
{
	q->held[0] = q->held[1] = 0;
	q->queued[0] = q->queued[1] = 0;
}

void keyq_hold(struct keyq *q, u16 fire, u16 pressed)
{
	// Only a fresh press counts twice: a held key's repeats are one move
	q->held[1] |= (u16)(q->held[0] & fire & pressed & KEYQ_TWICE);
	q->held[0] |= fire;
}

void keyq_queue(struct keyq *q, u16 keys)
{
	add(&q->queued[0], &q->queued[1], keys, 0);
}

void keyq_release(struct keyq *q)
{
	add(&q->queued[0], &q->queued[1], q->held[0], q->held[1]);
	q->held[0] = q->held[1] = 0;
}

void keyq_hold_queued(struct keyq *q)
{
	add(&q->held[0], &q->held[1], q->queued[0], q->queued[1]);
	q->queued[0] = q->queued[1] = 0;
}

u16 keyq_take(struct keyq *q)
{
	u16 keys = q->queued[0];
	q->queued[0] = q->queued[1];
	q->queued[1] = 0;
	return keys;
}

void keyq_drop_queued(struct keyq *q)
{
	q->queued[0] = q->queued[1] = 0;
}
