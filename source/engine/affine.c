// See affine.h.
#include "affine.h"

// round(4096 * sin(i * 90 degrees / 64)), i = 0-64: a quarter turn
static const s16 sin_quarter[65] = {
	0,    101,  201,  301,  401,  501,  601,  700,  799,  897,  995,  1092, 1189, 1285, 1380, 1474, 1567,
	1660, 1751, 1842, 1931, 2019, 2106, 2191, 2276, 2359, 2440, 2520, 2598, 2675, 2751, 2824, 2896, 2967,
	3035, 3102, 3166, 3229, 3290, 3349, 3406, 3461, 3513, 3564, 3612, 3659, 3703, 3745, 3784, 3822, 3857,
	3889, 3920, 3948, 3973, 3996, 4017, 4036, 4052, 4065, 4076, 4085, 4091, 4095, 4096,
};

s32 affine_sin(u16 angle)
{
	// From the first quarter by symmetry, so sin(-x) is exactly -sin(x)
	int negative = angle >= 0x8000;
	u32 a = angle & 0x7FFF;
	if (a > 0x4000)
		a = 0x8000 - a;
	u32 step = a >> 8, frac = a & 0xFF;
	s32 v = sin_quarter[step];
	if (frac)
		v += ((sin_quarter[step + 1] - v) * (s32)frac + 128) >> 8;
	return negative ? -v : v;
}

static s16 clamp16(s32 v)
{
	return (s16)(v > 0x7FFF ? 0x7FFF : v < -0x8000 ? -0x8000 : v);
}

void affine_rotscale(s16 out[4], u32 zoom_x, u32 zoom_y, u16 angle)
{
	// The matrix maps screen pixels to texture pixels: the inverse zoom
	s32 inv_x = (s32)(0x10000 / (zoom_x ? zoom_x : 1));
	s32 inv_y = (s32)(0x10000 / (zoom_y ? zoom_y : 1));
	s32 s = affine_sin(angle), c = affine_cos(angle);
	out[0] = clamp16((c * inv_x) >> 12);
	out[1] = clamp16((-s * inv_x) >> 12);
	out[2] = clamp16((s * inv_y) >> 12);
	out[3] = clamp16((c * inv_y) >> 12);
}
