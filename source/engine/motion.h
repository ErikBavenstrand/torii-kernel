// Motion: eased tweens and critically damped springs that drive menu
// properties (BG scroll, sprite position, affine scale and rotation, blend
// alpha, palette fade factors), and the palette fade maths.
//
// A fixed pool of MOTION_CAP motions. Each writes its value to a target every
// frame it moves (motion_update, in the frame's update phase), then once more
// when it ends, and then rests. Values are s32 16.16 fixed point (FIX) and
// must stay within +-16384 units (a list scrolled further rebases with
// motion_spring_shift). No floats and no hardware access: tests/host runs all
// of it.
//
// Tweens have a fixed duration and an easing curve: fades and fixed
// transitions. Springs are critically damped and drive anything the user can
// interrupt (list scroll, cursor, panel slides): changing a spring's target
// keeps its velocity, so a press during motion retargets it with no hitch.
#ifndef TORII_MOTION_H
#define TORII_MOTION_H

#include <gba_base.h>
#include <stddef.h>

// 16.16 fixed point.
#define FIX_ONE 0x10000
#define FIX(n) ((s32)(n) * FIX_ONE)
// Nearest integer, halves rounded up (towards +infinity).
#define FIX_ROUND(f) ((s32)(((f) + 0x8000) >> 16))

// Pool size. Hitting it logs an error and the new motion is MOTION_NONE.
#define MOTION_CAP 16

// Frames per update are clamped to this (the frame loop's dt cap).
#define MOTION_MAX_DT 3

// A handle: slot and generation, so a freed motion's handle stays dead when
// its slot is reused. MOTION_NONE is never a live motion, and every call
// ignores it (queries return 0).
typedef u16 MotionId;
#define MOTION_NONE 0

// Easing curves for tweens.
typedef enum {
	EASE_LINEAR,
	EASE_IN_CUBIC,
	EASE_OUT_CUBIC,
	EASE_IN_OUT_CUBIC,
	EASE_COUNT,
} Ease;

// The tables behind every curve but EASE_LINEAR (source/engine/ease_lut.c,
// written by tools/ease_lut.py): entry i is the curve at t = i/255, in Q15.
#define EASE_LUT_SIZE 256
#define EASE_TABLES (EASE_COUNT - 1)
#define EASE_ONE 0x8000
extern const u16 ease_lut[EASE_TABLES][EASE_LUT_SIZE];

// The curve at t = pos/0x10000 (0 to 0x10000), in Q15 (0 to EASE_ONE): table
// entries interpolated linearly. pos past 0x10000 counts as 0x10000.
u32 ease_at(Ease ease, u32 pos);

// Where a motion writes its value.
typedef enum {
	MOTION_TARGET_NONE,  // nothing: read it with motion_value
	MOTION_TARGET_S16,   // s16, the value rounded to an integer (BG scroll, sprite position, angle)
	MOTION_TARGET_S16_8, // s16 in 8.8 fixed point, rounded (affine scale: 0x100 is 1.0)
	MOTION_TARGET_S32,   // s32, the 16.16 value as it is
	MOTION_TARGET_CALL,  // set(ctx, value): blend alpha, fade factors, anything else
} MotionTargetKind;

typedef void (*MotionSetFn)(void *ctx, s32 value);

typedef struct {
	u8 kind;         // MotionTargetKind
	void *ptr;       // the variable, or ctx for MOTION_TARGET_CALL
	MotionSetFn set; // MOTION_TARGET_CALL only
} MotionTarget;

static inline MotionTarget motion_target(u8 kind, void *ptr, MotionSetFn set)
{
	MotionTarget t = {kind, ptr, set};
	return t;
}
#define MOTION_TO_NOTHING motion_target(MOTION_TARGET_NONE, NULL, NULL)
#define MOTION_TO_S16(p) motion_target(MOTION_TARGET_S16, (s16 *)(p), NULL)
#define MOTION_TO_S16_8(p) motion_target(MOTION_TARGET_S16_8, (s16 *)(p), NULL)
#define MOTION_TO_S32(p) motion_target(MOTION_TARGET_S32, (s32 *)(p), NULL)
#define MOTION_TO_CALL(fn, ctx) motion_target(MOTION_TARGET_CALL, (ctx), (fn))

// Spring stiffness: omega, the natural frequency in radians per frame, 16.16.
// Larger is faster; the time to cover 90% of a jump from rest doesn't depend
// on its length. Frames to 90%, and to settle on a 16 px jump:
#define SPRING_FAST 0x10000   // 1.0: 4, 9 (cursor, selection highlight)
#define SPRING_SCROLL 0xC000  // 0.75: 6, 12 (list scroll)
#define SPRING_GENTLE 0x8000  // 0.5: 8, 17 (panel slides)
#define SPRING_OMEGA_MAX 0x1E000 // 1.875: faster ones are clamped to it, as the steps go unstable near 2
#define SPRING_OMEGA_MIN 0x1000  // 1/16: slower ones are clamped to it

// Clears the pool: every handle goes dead.
void motion_reset(void);

// Advances every motion by dt frames (clamped to 1..MOTION_MAX_DT; 0 does
// nothing) and writes the targets of those that moved. Returns how many
// wrote, so the caller knows what is dirty. A spring advances in fixed
// steps, so dt 3 lands exactly where three updates of dt 1 do.
u32 motion_update(u32 dt);

// True when no tween runs and every spring has settled: the frame loop may
// sleep as far as motion is concerned.
int motion_idle(void);

// Live motions, running or resting.
u32 motion_count(void);

// A tween from from to to over frames frames (at least 1). The first update
// after it starts moves it: started in the input phase, it shows on that
// frame. It rests at to once it ends, until freed or restarted. owner tags
// it for motion_free_owner (0: none).
MotionId motion_tween(MotionTarget target, s32 from, s32 to, u32 frames, Ease ease, u8 owner);

// Restarts a tween from where it is now, towards to over frames frames.
void motion_tween_to(MotionId id, s32 to, u32 frames);

// A spring at rest at value, with stiffness omega (SPRING_*). It writes its
// target once on the next update.
MotionId motion_spring(MotionTarget target, s32 value, s32 omega, u8 owner);

// Moves a spring's target, keeping its position and velocity.
void motion_spring_target(MotionId id, s32 target);

// Puts a spring at rest at value at once (no animation: the user skipped it,
// or the view changed). It writes its target on the next update.
void motion_spring_jump(MotionId id, s32 value);

// Shifts a spring's position and target by delta together, keeping its
// velocity: to rebase a ring buffer's scroll, or keep values small.
void motion_spring_shift(MotionId id, s32 delta);

void motion_spring_omega(MotionId id, s32 omega);

// The current value (0 for a dead handle), and where it is going.
s32 motion_value(MotionId id);
s32 motion_goal(MotionId id);

// True while a tween runs or a spring hasn't settled.
int motion_running(MotionId id);

// Frees a motion: its handle goes dead and the target is left as it is.
void motion_free(MotionId id);
// Frees every motion tagged owner (a view, when it pops).
void motion_free_owner(u8 owner);

// Palette fades. Writes count colours to dst: each colour of src moved
// towards color by factor/256 (0 leaves src, 256 gives color), channel by
// channel. factor over 256 counts as 256. dst may be src.
void palette_fade(u16 *dst, const u16 *src, u32 count, u16 color, u32 factor);

#define FADE_BLACK 0x0000
#define FADE_WHITE 0x7FFF
#define FADE_MAX 256

#endif
