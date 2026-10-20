// The arithmetic of progress.h, hardware-free (tests/host/test_progress.c).
#include "progress.h"

u32 progress_permille(u32 lo, u32 hi, u32 done, u32 total)
{
	if (hi < lo)
		hi = lo;
	if (!total || done >= total)
		return hi;
	// Both scaled down until (hi - lo) * done fits in 32 bits: done < total,
	// and hi - lo is at most PROGRESS_FULL (more is clamped)
	u32 span = hi - lo;
	if (span > PROGRESS_FULL)
		span = PROGRESS_FULL;
	while (total > 0x3FFFFF) {
		total >>= 1;
		done >>= 1;
	}
	if (done >= total)  // rounded up to it: not done yet
		done = total - 1;
	return lo + span * done / total;
}

u32 progress_estimate(u32 elapsed, u32 expected)
{
	if (!expected || elapsed >= expected) {
		if (!elapsed)
			return 950;
		u32 left = 40 * (expected < 0x1000000 ? expected : 0x1000000) / elapsed;
		return 990 - (left < 40 ? left : 40);
	}
	return progress_permille(0, 950, elapsed, expected);
}

void progress_copy_plan(struct progress_copy *c, u32 from, u32 to, u32 frames)
{
	if (to < from)
		to = from;
	if (to > 0x7FFF)
		to = 0x7FFF;  // a bar is a screen wide at most: at most 240
	if (from > to)
		from = to;
	if (!frames)
		frames = 1;
	c->at = from << 16;
	c->end = to << 16;
	c->knee = c->at + ((to - from) << 16) / 10 * PROGRESS_COPY_KNEE;
	c->fast = (c->knee - c->at) / frames;
	c->slow = c->fast / PROGRESS_COPY_SLOW;
	if (c->fast && !c->slow)
		c->slow = 1;
}
