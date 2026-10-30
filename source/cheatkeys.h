// How a cheat is activated (docs/plans/cheat-guides.md): a compact u16 that
// the cheat file's cheatN_keys line sets, or that the decoded code implies.
//
//	bits 0-9    the buttons, as KEYINPUT's bits (KEY_A bit 0 ... KEY_L bit 9),
//	            a bit set for a button held
//	bits 12-14  the mode (enum cheatkeys_mode)
//
// 0 is "not set" (CHEATKEYS_NONE): no cheatN_keys line, an invalid one, or a
// code whose buttons can't be told. Always and Launch carry no buttons; Hold
// and Press carry at least one. Any other value is invalid
// (cheatkeys_valid).
//
// The file's text: "always", "launch", or "hold" or "press" then buttons
// joined by '+' ("hold L+Select"). Words and buttons in any case, spaces
// allowed around them; the buttons are A B Select Start Up Down Left Right
// L R. Anything else reads as not set.
//
// Screens show a value in the button order of enum cheatkeys_button (L, R,
// SELECT, START, A, B, Up, Down, Left, Right), with its names from
// lang/*.ini ([button], [button.short]) and its sentence from [activation]:
// "Hold L+SELECT". cheatkeys_next walks the held buttons in that order for a
// screen that builds its own text.
//
// Hardware-free (tests/host). cheatkeys_parse, cheatkeys_format and
// cheatkeys_derive are in cheatfile.c, which reads and writes the lines:
// tools/cheats.py builds cheatfile.c on its own for its patch tool. The rest
// is cheatkeys.c's.
#ifndef TORII_CHEATKEYS_H
#define TORII_CHEATKEYS_H

#include <gba_base.h>

#include "cheatcode.h"

enum cheatkeys_mode {
	CHEATKEYS_NONE,    // not set, or unknown: nothing shown
	CHEATKEYS_ALWAYS,  // on whenever the game runs
	CHEATKEYS_HOLD,    // works while its buttons are held
	CHEATKEYS_PRESS,   // a press of its buttons does it (from the file or the cart only, never derived)
	CHEATKEYS_LAUNCH,  // ROM patches only: set when the game starts
	CHEATKEYS_MODES
};

#define CHEATKEYS_BUTTONS 0x03FFu  // the buttons' bits (KEYINPUT's)
#define CHEATKEYS_MODE_SHIFT 12
#define CHEATKEYS_MODE_MASK (7u << CHEATKEYS_MODE_SHIFT)
// A value of mode with buttons (KEYINPUT bits)
#define CHEATKEYS(mode, buttons) ((u16)((u32)(mode) << CHEATKEYS_MODE_SHIFT | ((u32)(buttons) & CHEATKEYS_BUTTONS)))
#define CHEATKEYS_ALWAYS_ON CHEATKEYS(CHEATKEYS_ALWAYS, 0)
#define CHEATKEYS_AT_LAUNCH CHEATKEYS(CHEATKEYS_LAUNCH, 0)

// The buttons in the order screens show them; lang/en.ini's [button] and
// [button.short] keep this order (a _Static_assert in cheatkeys.c checks)
enum cheatkeys_button {
	CHEATKEYS_L,
	CHEATKEYS_R,
	CHEATKEYS_SELECT,
	CHEATKEYS_START,
	CHEATKEYS_A,
	CHEATKEYS_B,
	CHEATKEYS_UP,
	CHEATKEYS_DOWN,
	CHEATKEYS_LEFT,
	CHEATKEYS_RIGHT,
	CHEATKEYS_BUTTON_COUNT
};
// Button b's KEYINPUT bit (b an enum cheatkeys_button): a nibble each of
// its bit number, L's 9 lowest
static inline u32 cheatkeys_bit(u32 b)
{
	return 1u << (u32)((0x4576103289ull >> (4 * b)) & 0xF);
}

static inline u32 cheatkeys_mode(u16 v)
{
	return (v & CHEATKEYS_MODE_MASK) >> CHEATKEYS_MODE_SHIFT;
}

static inline u32 cheatkeys_buttons(u16 v)
{
	return v & CHEATKEYS_BUTTONS;
}

// 1 if v is a value this layout allows: 0, Always or Launch without buttons,
// Hold or Press with some, nothing in the unused bits
static inline int cheatkeys_valid(u16 v)
{
	u32 mode = cheatkeys_mode(v);
	int held = cheatkeys_buttons(v) != 0;
	if (v & ~(CHEATKEYS_MODE_MASK | CHEATKEYS_BUTTONS))
		return 0;
	if (mode == CHEATKEYS_HOLD || mode == CHEATKEYS_PRESS)
		return held;
	return mode < CHEATKEYS_MODES && !held;
}

// A cheatN_keys value (len bytes, without its quotes) as a value; 0 if it
// isn't one
u16 cheatkeys_parse(const char *s, u32 len);
// v as the file writes it ("hold L+Select", "always"): its length, the text
// in out (size bytes, ended; CHEATKEYS_TEXT_MAX takes any); 0 and "" for 0
// or an invalid v
#define CHEATKEYS_TEXT_MAX 64
u32 cheatkeys_format(u16 v, char *out, u32 size);
// What a cheat's decoded ops (n of them, cheat_decode's) say of how it is
// activated, with whether it has ROM patches and runs anything in game
// (cheat_lower made table words for it):
// - its conditions on KEYINPUT (CHEAT_KEYINPUT): an IF_EQ of width 2 needs
//   the clear bits of its value's low 10 held together, an IF_NAND (any
//   width, at 0x04000130, or width 1 at 0x04000131) its value's bits held,
//   whatever else is; IF_BUTTON needs CHEAT_BUTTON_KEYS. One combination
//   across the cheat: Hold it. Different combinations, any other condition
//   on KEYINPUT, one with an ELSE, or one that needs nothing held: 0.
// - no condition on KEYINPUT, and it runs in game: Always (other conditions
//   test the game's state, which needs no buttons);
// - only ROM patches: Launch;
// - else 0.
u16 cheatkeys_derive(const struct cheat_op *ops, u32 n, int has_patches, int runs_in_game);

// The buttons v holds (0 for Always, Launch and 0)
u32 cheatkeys_count(u16 v);
// The first held button after button `after` (enum cheatkeys_button; -1
// starts), in screen order; -1 after the last. A walk:
//	for (s32 b = cheatkeys_next(v, -1); b >= 0; b = cheatkeys_next(v, b))
s32 cheatkeys_next(u16 v, s32 after);
// The first held button in screen order, -1 if none
static inline s32 cheatkeys_first(u16 v)
{
	return cheatkeys_next(v, -1);
}

// --- Text for screens, from lang/*.ini (the launch sections: the in-game
// menu's lines are drawn at launch with them) -------------------------------

// v's buttons by their [button] names joined by [button] join ("L+SELECT"),
// cut to room bytes on a whole character; the bytes written. "" without
// buttons.
u32 cheatkeys_names(u16 v, char *out, u32 room);
// v's sentence: [activation]'s always, hold, press or launch ("Hold
// L+SELECT", "Always on", "Set when the game starts"); "" for 0. The bytes
// written.
u32 cheatkeys_sentence(u16 v, char *out, u32 room);
// A list row's short tag for a Hold or Press value: its first button's
// [button.short] name, alone or with [button] more's count of the others
// ("SEL", "L+1"); "" for any other value. The bytes written.
u32 cheatkeys_tag(u16 v, char *out, u32 room);

#endif
