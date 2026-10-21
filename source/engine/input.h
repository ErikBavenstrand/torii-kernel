// Input: per-key repeat counted in frames (not libgba's shared setRepeat),
// paging keys on their own schedule, and list cursor moves that wrap only
// on a fresh press.
//
// input_step takes the keys held this frame as a bitmask, so tests/host runs
// it; input_poll (input_hw.c) reads them from the hardware.
//
// The d-pad repeats: it fires on the press frame, again once held for delay
// frames, then on a smooth ramp: from one repeat every interval frames, the
// rate rises along a smoothstep curve over ramp frames to top_speed repeats
// a second, and stays there. A repeat is due when an accumulator, advanced
// by the rate each frame, passes a whole repeat; input_phase gives what it
// holds towards the next one, so a list can scroll at the cursor's speed
// between repeats rather than in steps. Pressing a direction, or releasing
// one, starts every held direction's schedule over: one still held fires
// again only after delay frames.
//
// Paging keys (L and R, plus any input_page_keys adds, such as LEFT and
// RIGHT where they move a page) fire on the press, after page_delay frames,
// then every page_interval frames, with no speed-up. Other keys fire only on
// the press.
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
	u8 interval;      // between repeats at the start of the ramp
	u8 ramp;          // first repeat to top speed
	u8 top_speed;     // repeats a second at top speed, 1-60 (60: every frame)
	u8 page_delay;    // paging keys: press to first repeat
	u8 page_interval; // paging keys: between repeats
} InputTiming;

// 15, 4, 54, 30; paging 20, 6. A held d-pad fires on frames
// 0 and 15, then about 4 frames apart, closing up smoothly to 30 a second
// (every other frame) by frame 69 (1.15 s). 30 a second moves a 16 px row
// list exactly 8 px every frame, where 40 alternated 11, 11 and 10 px
// steps (v0.3.2's scroll feel). The built-in
// themes take the same figures.
#define INPUT_TIMING_DEFAULT {15, 4, 54, 30, 20, 6}
extern const InputTiming input_timing_default;

// Repeat rates are 16.16 repeats a frame.
#define INPUT_ONE 0x10000u

typedef struct {
	const InputTiming *timing;
	u32 now;      // frames counted by input_step
	u16 held;     // held this frame
	u16 pressed;  // went down this frame
	u16 released; // went up this frame
	u16 fire;     // pressed or repeated this frame: act on these
	u16 page_keys; // keys on the paging schedule: INPUT_PAGE unless input_page_keys says
	u16 repeating; // held keys past their first repeat
	u32 held_frames[INPUT_KEYS];
	u32 phase[INPUT_KEYS];  // towards the next repeat (INPUT_ONE is one), once repeating
	u32 rate[INPUT_KEYS];   // the repeat rate this frame, once repeating
	u16 ramp_frames[INPUT_KEYS]; // since the first repeat, up to ramp
} InputState;

// Starts with no key held. timing NULL takes input_timing_default.
void input_init(InputState *s, const InputTiming *timing);

// Which keys page (INPUT_PAGE and any of the d-pad's): they repeat on the
// paging schedule from the next step (one already repeating switches rate).
void input_page_keys(InputState *s, u16 keys);

// One frame: keys is the held mask (INPUT_*), dt the frames since the last
// step (clamped to 1..3; repeats fire at most once a step).
void input_step(InputState *s, u16 keys, u32 dt);

// Reads the keys from REG_KEYINPUT and steps (kernel only, input_hw.c).
void input_poll(InputState *s, u32 dt);

// The repeat rate of key at the curve's point ramp frames after its first
// repeat: 16.16 repeats a frame (hardware-free, for tests and tuning).
u32 input_ramp_rate(const InputTiming *t, u32 ramp);

// What key has gathered towards its next repeat (0 to INPUT_ONE - 1), and
// its repeat rate this frame; both 0 unless key is held past its first
// repeat. key is a single bit.
u32 input_phase(const InputState *s, u16 key);
u32 input_rate(const InputState *s, u16 key);

// True if key (any of a mask) fired this frame.
static inline int input_fired(const InputState *s, u16 key)
{
	return (s->fire & key) != 0;
}

// Where a list cursor goes when key fires, moving delta items (1, -1 or a
// page) in a list of count. A move past an end stops at that end. From the
// end itself a fresh press of key wraps to the other end; a held key's
// repeats stay there. Every list and menu moves this way
// (docs/design/engine.md, Motion and input).
u32 input_move(const InputState *s, u16 key, u32 cursor, u32 count, s32 delta);

#endif
