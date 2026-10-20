// The home carousel's ring as numbers (screens/carousel.c draws it): where
// each object stands for a position of the ring, and how the ring turns.
// Hardware-free (tests/host/test_carousel.c).
//
// The ring's position is a 16.16 item index: 2.0 faces item 2, 2.5 is half
// way to item 3. Integer k stands for item k modulo the item count, at
// offset o = k - position from the front, at angle o * step on a ring seen
// slightly from above (spec.md, Carousel):
//
//	x     = centre_x + radius_x sin a
//	feet  = feet_y - rise (1 - cos a)
//	scale = scale_far + scale_span cos a  (uniform: never squashed)
//	fog   = fog (1 - cos a), toward the backdrop
//
// A window of at most `slots` offsets shows at rest (T10): -2 to 2 for five
// or more items; fewer items show only that many, nearest first, so nothing
// shows twice (T26): 0, 1, -1, 2 for four. While the ring turns, an object
// leaving the window fogs out over its last item of travel and one coming
// in fogs in, so a ring of four or five can briefly show one item at both
// ends: one copy going, the other coming.
#ifndef TORII_SCREENS_CAROUSEL_RING_H
#define TORII_SCREENS_CAROUSEL_RING_H

#include <gba_base.h>

#include "../engine/motion.h"

struct ring_look {
	u8 centre_x;     // px: the front object's centre
	u8 radius_x;     // px: the ring's half width
	u8 feet_y;       // px: the front object's foot row
	u8 rise;         // px the feet rise from the front to the back
	u16 step;        // the angle between items, 1/65536 turn (36 degrees)
	u16 scale_far;   // scale at 90 degrees, 256ths of the drawn size
	u16 scale_span;  // what the front adds to it, 256ths
	u16 fog;         // fog at 90 degrees, 256ths (a whole 256 is the backdrop)
	u16 scale_min;   // 256ths: smaller objects aren't drawn
	u8 slots;        // offsets shown at rest (5)
};

// The most objects a turning ring shows at once
#define RING_OBJECTS 6

struct ring_object {
	s32 k;        // its integer position (item k modulo the count)
	s32 o;        // its offset from the front, 16.16 items
	s16 x;        // its centre's column
	s16 feet;     // its foot row
	u16 scale;    // 256ths of its drawn size
	u16 fog;      // 0-255: how far it fogs toward the backdrop
};

// The window of offsets [*lo, *hi] shown at rest for count items (count 0:
// *lo > *hi)
void ring_window(const struct ring_look *look, u32 count, s32 *lo, s32 *hi);

// The objects showing at position pos for count items, front first (lowest
// |o|, so the nearest is drawn on top), into out; returns how many. Objects
// fogged out completely or scaled under scale_min are left out.
u32 ring_objects(const struct ring_look *look, u32 count, s32 pos, struct ring_object out[RING_OBJECTS]);

// The affine matrix's pa = pd for scale (256ths): the inverse, rounded
u32 ring_matrix(u32 scale);
// The row of the last texel row of a 64 px tall affine sprite (not double
// size) drawn with pd, counted from its box's centre row: anchor the feet
// from this, the scale actually written, so they never bob against the
// shadow while the scale changes (the hardware samples texel row
// (pd * dy >> 8) + 32 on row dy)
s32 ring_foot_dy(u32 pd);

// item k for count items: k modulo count, never negative
u32 ring_item(s32 k, u32 count);

// --- Turning ----------------------------------------------------------------
// A turn eases the position to its target over frames frames (the theme's
// step). A turn asked while the ring still moves starts from where it is
// with the speed it has (a cubic that keeps the velocity and ends at rest),
// so a held key glides from item to item without stopping at each.

struct ring_motion {
	s32 pos;     // 16.16 items, as shown this frame
	s32 to;      // the target: an integer
	s32 from;    // where this leg started
	s32 m0;      // its start speed, 16.16 items per leg (0: the eased curve)
	s32 vel;     // the last frame's speed, 16.16 items a frame
	u16 frame;   // frames into the leg
	u16 frames;  // the leg's length
	u8 ease;     // Ease, for a leg from rest
	u8 moving;
};

// At rest at item at
void ring_motion_rest(struct ring_motion *m, s32 at);
// Turns by dir items (1 next, -1 previous) over frames frames
void ring_motion_turn(struct ring_motion *m, s32 dir, u32 frames, Ease ease);
// Advances dt frames; returns 1 while it moves (0 once at rest, pos == to)
int ring_motion_step(struct ring_motion *m, u32 dt);
// The target taken into [0, count) and the position with it, by whole laps
// (the same items show); at rest, the position too
void ring_motion_wrap(struct ring_motion *m, u32 count);
// 16.16 items the target is ahead of the position (negative: behind)
static inline s32 ring_motion_lag(const struct ring_motion *m)
{
	return m->to * FIX_ONE - m->pos;
}

#endif
