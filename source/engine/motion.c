#include "motion.h"

#include <stdint.h>
#include <string.h>

#include "../tlog.h"

enum {
	KIND_FREE,
	KIND_TWEEN,
	KIND_SPRING,
};

enum {
	FLAG_RUNNING = 1, // a tween before its end, a spring not yet settled
	FLAG_WRITE = 2,   // write the target on the next update, even at rest
};

// Spring steps per frame. Semi-implicit Euler, critically damped, steps of
// h frames: with u = omega * h, both roots of the step stay real and
// positive while u < 0.5, so a spring from rest never overshoots. Four steps
// a frame keep u under 0.5 up to SPRING_OMEGA_MAX.
#define SPRING_STEPS 4
#define SPRING_STEPS_SQUARED 16
// Settled: closer than 1/16 of the target's smallest step, and slower than
// 1/64 of it per spring step: 1 for whole-unit targets, 1/256 for 8.8 ones.
#define SETTLE_DISTANCE 0x1000
#define SETTLE_SPEED 0x400
// Values must stay within this, so differences fit an s32 (motion.h).
#define VALUE_LIMIT 0x40000000

// A handle is (generation << 4) | slot.
#define SLOT_BITS 4
#define GEN_MAX 0xFFF
_Static_assert(MOTION_CAP <= 1 << SLOT_BITS, "MotionId has 4 bits for the slot");

typedef struct {
	MotionTarget target;
	s32 value;
	s32 goal;  // a tween's end, a spring's target
	s32 start; // a tween's start; a spring's velocity, per step
	union {
		struct {
			u16 elapsed;
			u16 frames;
		} tween;
		s32 omega;
	} u;
	u8 kind;
	u8 flags;
	u8 owner;
	u8 ease;
	u16 gen; // handle generation, never 0 for a live motion
} Motion;

static Motion pool[MOTION_CAP] EWRAM_BSS;

static Motion *find(MotionId id)
{
	u32 slot = id & ((1 << SLOT_BITS) - 1);
	if (slot >= MOTION_CAP)
		return NULL;
	Motion *m = &pool[slot];
	if (m->kind == KIND_FREE || m->gen != id >> SLOT_BITS)
		return NULL;
	return m;
}

static void retire(Motion *m)
{
	m->kind = KIND_FREE;
	// Old handles to this slot go dead; generation 0 is never live.
	m->gen = m->gen >= GEN_MAX ? 1 : m->gen + 1;
}

static MotionId add(MotionTarget target, s32 value, u8 kind, u8 owner)
{
	for (u32 slot = 0; slot < MOTION_CAP; slot++) {
		Motion *m = &pool[slot];
		if (m->kind != KIND_FREE)
			continue;
		u16 gen = m->gen ? m->gen : 1;
		memset(m, 0, sizeof(*m));
		m->target = target;
		m->value = value;
		m->goal = value;
		m->kind = kind;
		m->owner = owner;
		m->gen = gen;
		m->flags = FLAG_WRITE;
		return (MotionId)(gen << SLOT_BITS | slot);
	}
	tlog_error("motion: all %d motions in use", MOTION_CAP);
	return MOTION_NONE;
}

static void check_range(int64_t value)
{
	TLOG_ASSERT(value >= -VALUE_LIMIT && value <= VALUE_LIMIT);
}

static s32 clamp_omega(s32 omega)
{
	if (omega < SPRING_OMEGA_MIN)
		return SPRING_OMEGA_MIN;
	return omega > SPRING_OMEGA_MAX ? SPRING_OMEGA_MAX : omega;
}

static u32 clamp_frames(u32 frames)
{
	if (frames < 1)
		return 1;
	return frames > 0xFFFF ? 0xFFFF : frames;
}

u32 ease_at(Ease ease, u32 pos)
{
	if (pos > 0x10000)
		pos = 0x10000;
	if (ease <= EASE_LINEAR || ease >= EASE_COUNT)
		return pos >> 1;
	const u16 *lut = ease_lut[ease - 1];
	u32 p = pos * (EASE_LUT_SIZE - 1);
	u32 i = p >> 16;
	if (i >= EASE_LUT_SIZE - 1)
		return lut[EASE_LUT_SIZE - 1];
	u32 frac = (p >> 8) & 0xFF;
	return lut[i] + (((lut[i + 1] - lut[i]) * frac) >> 8);
}

static void write_target(const Motion *m)
{
	const MotionTarget *t = &m->target;
	switch (t->kind) {
	case MOTION_TARGET_S16:
		*(s16 *)t->ptr = (s16)FIX_ROUND(m->value);
		break;
	case MOTION_TARGET_S16_8:
		*(s16 *)t->ptr = (s16)((m->value + 0x80) >> 8);
		break;
	case MOTION_TARGET_S32:
		*(s32 *)t->ptr = m->value;
		break;
	case MOTION_TARGET_CALL:
		if (t->set)
			t->set(t->ptr, m->value);
		break;
	default:
		break;
	}
}

static void step_tween(Motion *m, u32 dt)
{
	u32 elapsed = m->u.tween.elapsed + dt;
	if (elapsed >= m->u.tween.frames) {
		m->u.tween.elapsed = m->u.tween.frames;
		m->value = m->goal;
		m->flags &= ~FLAG_RUNNING;
		return;
	}
	m->u.tween.elapsed = elapsed;
	u32 e = ease_at(m->ease, (elapsed << 16) / m->u.tween.frames);
	m->value = m->start + (s32)(((int64_t)m->goal - m->start) * e >> 15);
}

static void step_spring(Motion *m, u32 dt)
{
	s32 omega = m->u.omega;
	// v += (omega^2 (goal - x) - 2 omega v) h; x += v h, with v kept per step
	// (v h), so: v += omega^2 h^2 (goal - x) - 2 omega h v; x += v.
	s32 k = (s32)(((int64_t)omega * omega >> 16) / SPRING_STEPS_SQUARED);
	s32 damp = 2 * omega / SPRING_STEPS;
	s32 x = m->value, v = m->start;
	u32 fine = m->target.kind == MOTION_TARGET_S16_8 ? 8 : 0;
	s32 near = SETTLE_DISTANCE >> fine, slow = SETTLE_SPEED >> fine;
	for (u32 frame = 0; frame < dt; frame++) {
		for (u32 i = 0; i < SPRING_STEPS; i++) {
			int64_t pull = (int64_t)k * ((int64_t)m->goal - x) >> 16;
			int64_t drag = (int64_t)damp * v >> 16;
			v += (s32)(pull - drag);
			x += v;
		}
		// Checked once a frame, so dt 3 matches three frames of dt 1.
		s32 off = m->goal - x;
		if (off > -near && off < near && v > -slow && v < slow) {
			x = m->goal;
			v = 0;
			m->flags &= ~FLAG_RUNNING;
			break;
		}
	}
	m->value = x;
	m->start = v;
}

void motion_reset(void)
{
	for (u32 slot = 0; slot < MOTION_CAP; slot++)
		if (pool[slot].kind != KIND_FREE)
			retire(&pool[slot]);
}

u32 motion_update(u32 dt)
{
	if (dt == 0)
		return 0;
	if (dt > MOTION_MAX_DT)
		dt = MOTION_MAX_DT;
	u32 written = 0;
	for (u32 slot = 0; slot < MOTION_CAP; slot++) {
		Motion *m = &pool[slot];
		if (m->kind == KIND_FREE || !m->flags)
			continue;
		if (m->flags & FLAG_RUNNING) {
			if (m->kind == KIND_TWEEN)
				step_tween(m, dt);
			else
				step_spring(m, dt);
		}
		m->flags &= ~FLAG_WRITE;
		write_target(m);
		written++;
	}
	return written;
}

int motion_idle(void)
{
	for (u32 slot = 0; slot < MOTION_CAP; slot++)
		if (pool[slot].kind != KIND_FREE && pool[slot].flags)
			return 0;
	return 1;
}

u32 motion_count(void)
{
	u32 count = 0;
	for (u32 slot = 0; slot < MOTION_CAP; slot++)
		count += pool[slot].kind != KIND_FREE;
	return count;
}

MotionId motion_tween(MotionTarget target, s32 from, s32 to, u32 frames, Ease ease, u8 owner)
{
	MotionId id = add(target, from, KIND_TWEEN, owner);
	Motion *m = find(id);
	if (!m)
		return MOTION_NONE;
	check_range(from);
	check_range(to);
	m->ease = (u8)ease;
	m->start = from;
	m->goal = to;
	m->u.tween.frames = (u16)clamp_frames(frames);
	m->flags = FLAG_RUNNING;
	return id;
}

void motion_tween_to(MotionId id, s32 to, u32 frames)
{
	Motion *m = find(id);
	if (!m || m->kind != KIND_TWEEN)
		return;
	check_range(to);
	m->start = m->value;
	m->goal = to;
	m->u.tween.elapsed = 0;
	m->u.tween.frames = (u16)clamp_frames(frames);
	m->flags |= FLAG_RUNNING;
}

MotionId motion_spring(MotionTarget target, s32 value, s32 omega, u8 owner)
{
	MotionId id = add(target, value, KIND_SPRING, owner);
	Motion *m = find(id);
	if (m)
		m->u.omega = clamp_omega(omega);
	check_range(value);
	return id;
}

void motion_spring_target(MotionId id, s32 target)
{
	Motion *m = find(id);
	if (!m || m->kind != KIND_SPRING || m->goal == target)
		return;
	check_range(target);
	m->goal = target;
	m->flags |= FLAG_RUNNING;
}

void motion_spring_jump(MotionId id, s32 value)
{
	Motion *m = find(id);
	if (!m || m->kind != KIND_SPRING)
		return;
	check_range(value);
	m->value = value;
	m->goal = value;
	m->start = 0;
	m->flags = FLAG_WRITE;
}

void motion_spring_shift(MotionId id, s32 delta)
{
	Motion *m = find(id);
	if (!m || m->kind != KIND_SPRING || delta == 0)
		return;
	check_range((int64_t)m->value + delta);
	check_range((int64_t)m->goal + delta);
	m->value += delta;
	m->goal += delta;
	m->flags |= FLAG_WRITE;
}

void motion_spring_omega(MotionId id, s32 omega)
{
	Motion *m = find(id);
	if (m && m->kind == KIND_SPRING)
		m->u.omega = clamp_omega(omega);
}

s32 motion_value(MotionId id)
{
	const Motion *m = find(id);
	return m ? m->value : 0;
}

s32 motion_goal(MotionId id)
{
	const Motion *m = find(id);
	return m ? m->goal : 0;
}

int motion_running(MotionId id)
{
	const Motion *m = find(id);
	return m && (m->flags & FLAG_RUNNING);
}

void motion_free(MotionId id)
{
	Motion *m = find(id);
	if (m)
		retire(m);
}

void motion_free_owner(u8 owner)
{
	if (owner == 0)
		return;
	for (u32 slot = 0; slot < MOTION_CAP; slot++)
		if (pool[slot].kind != KIND_FREE && pool[slot].owner == owner)
			retire(&pool[slot]);
}

void palette_fade(u16 *dst, const u16 *src, u32 count, u16 color, u32 factor)
{
	if (factor > FADE_MAX)
		factor = FADE_MAX;
	s32 f = (s32)factor;
	s32 tr = color & 31, tg = color >> 5 & 31, tb = color >> 10 & 31;
	for (u32 i = 0; i < count; i++) {
		s32 c = src[i];
		s32 r = c & 31, g = c >> 5 & 31, b = c >> 10 & 31;
		r += (tr - r) * f >> 8;
		g += (tg - g) * f >> 8;
		b += (tb - b) * f >> 8;
		dst[i] = (u16)(r | g << 5 | b << 10);
	}
}
