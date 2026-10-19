// Affine sprite helpers: scale and rotate (covers in a carousel, T6 and
// v0.4). The maths is hardware-free and host-tested; affine_set writes the
// result to the shadow OAM's matrix through gfx_affine_set.
//
// Zoom is 8.8 fixed point, the sprite's size on screen: 0x100 is its own
// size, 0x180 one and a half times, 0x80 half. An affine sprite shown larger
// than its own size needs the double-size flag (attribute 0 bit 9), or its
// corners are cut at the sprite's bounds. Angles are 16 bits per turn
// (0x4000 = 90 degrees), counter-clockwise on screen.
#ifndef TORII_ENGINE_AFFINE_H
#define TORII_ENGINE_AFFINE_H

#include <gba_base.h>
#include "gfx.h"

// sin(angle) in 4.12 fixed point (4096 = 1), interpolated from a 256-step table.
s32 affine_sin(u16 angle);
static inline s32 affine_cos(u16 angle)
{
	return affine_sin((u16)(angle + 0x4000));
}

// The matrix (pa, pb, pc, pd) that shows a sprite zoomed by zoom_x and
// zoom_y (8.8, at least 1) and turned by angle. Each value is clamped to s16.
void affine_rotscale(s16 out[4], u32 zoom_x, u32 zoom_y, u16 angle);

// Sets shadow OAM matrix (0-31) to affine_rotscale's result.
static inline void affine_set(u32 matrix, u32 zoom_x, u32 zoom_y, u16 angle)
{
	s16 m[4];
	affine_rotscale(m, zoom_x, zoom_y, angle);
	gfx_affine_set(matrix, m[0], m[1], m[2], m[3]);
}

#endif
