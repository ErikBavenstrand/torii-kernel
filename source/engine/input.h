// Input: per-key repeat counted in frames (not libgba's shared setRepeat),
// L/R paging on its own schedule, and list cursor moves that wrap only when
// the user means it.
//
// input_step takes the keys held this frame as a bitmask, so tests/host runs
// it; input_poll (input_hw.c) reads them from the hardware.
//
// The d-pad repeats: it fires on the press frame, again once held for delay
// frames, then every interval frames, and every fast_interval frames once a
// repeat would fall at or past fast_after frames held (every skip_interval
// frames instead while the caller says the scroll animation is skipped). Releasing a direction, or
// pressing another, starts the schedule over. L and R fire on the press,
// after page_delay frames, then every page_interval frames, with no speed-up.
// Other keys fire only on the press.
#ifndef TORII_INPUT_H
#define TORII_INPUT_H

#include <gba_base.h>

// Key bits as REG_KEYINPUT has them, inverted (1 is held): libgba's KEY_*.
#define INPUT_A 0x001
#define INPUT_B 0x002
#define INPUT_SELECT 0x004
#define INPUT_START 0x008
#define INPUT_RIGHT 0x010
#define INPUT_LEFT 0x020
#define INPUT_UP 0x040
#define INPUT_DOWN 0x080
#define INPUT_R 0x100
#define INPUT_L 0x200
#define INPUT_DPAD 0x0F0
#define INPUT_PAGE 0x300
#define INPUT_ALL 0x3FF
#define INPUT_KEYS 10

// Timings in frames: layout and theme data, so a view may pass its own.
typedef struct {
	u8 delay;         // press to first repeat
	u8 interval;      // between repeats
	u8 fast_after;    // held this long, repeats speed up
	u8 fast_interval; // between repeats then
	u8 skip_interval; // between repeats then, while the scroll animation is skipped
	u8 page_delay;    // L and R: press to first repeat
	u8 page_interval; // L and R: between repeats
	u8 wrap_pause;    // a held key wraps at a list end after resting there this long
} InputTiming;

// 15, 4, 60 (1 s), 2, 1; L/R 20, 8; wrap pause 15. A held d-pad fires on
// frames 0, 15, 19, ... 59, then 61, 63, ... (or 60, 61, ... while skipped).
extern const InputTiming input_timing_default;

typedef struct {
	const InputTiming *timing;
	u32 now;      // frames counted by input_step
	u16 held;     // held this frame
	u16 pressed;  // went down this frame
	u16 released; // went up this frame
	u16 fire;     // pressed or repeated this frame: act on these
	u32 held_frames[INPUT_KEYS];
	u32 next_fire[INPUT_KEYS]; // held_frames at which the key repeats next
} InputState;

// The wrap gate for one cursor (a list, or one axis of a grid): each owns
// its own, set up by input_wrap_init.
typedef struct {
	u32 since;  // InputState.now when the cursor last moved
	u32 cursor; // where input_move left it
} InputWrap;

// Starts with no key held. timing NULL takes input_timing_default.
void input_init(InputState *s, const InputTiming *timing);

// One frame: keys is the held mask (INPUT_*), dt the frames since the last
// step (clamped to 1..3; repeats fire at most once a step). skip_anim: the
// caller skips the scroll animation at the current repeat speed, which lets
// the d-pad repeat every skip_interval frames once it is past fast_after.
void input_step(InputState *s, u16 keys, u32 dt, int skip_anim);

// Reads the keys from REG_KEYINPUT and steps (kernel only, input_hw.c).
void input_poll(InputState *s, u32 dt, int skip_anim);

// True if key (any of a mask) fired this frame.
static inline int input_fired(const InputState *s, u16 key)
{
	return (s->fire & key) != 0;
}

// Starts a cursor's gate: the cursor counts as having just arrived.
static inline void input_wrap_init(InputWrap *w, const InputState *s, u32 cursor)
{
	w->since = s->now;
	w->cursor = cursor;
}

// Where a list cursor goes when key fires, moving delta items (1, -1 or a
// page) in a list of count. A move past an end first stops at that end. From
// the end itself it wraps to the other end only on a fresh press of key, or
// once the cursor has rested at the end for wrap_pause frames; a repeat that
// arrives sooner leaves it there. A cursor moved by other means counts as
// having just arrived. w is this cursor's gate.
u32 input_move(const InputState *s, InputWrap *w, u16 key, u32 cursor, u32 count, s32 delta);

#endif
