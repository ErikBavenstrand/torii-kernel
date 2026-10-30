// How a cheat is activated, for screens (cheatkeys.h). Its file text and the
// code's reading are cheatfile.c's.
#include "cheatkeys.h"

#include <string.h>

#include "lang.h"

_Static_assert(STR_BUTTON_RIGHT - STR_BUTTON_L == CHEATKEYS_RIGHT, "[button] in enum cheatkeys_button's order");
_Static_assert(STR_BUTTON_SHORT_RIGHT - STR_BUTTON_SHORT_L == CHEATKEYS_RIGHT,
	       "[button.short] in enum cheatkeys_button's order");
_Static_assert(CHEATKEYS_MODES - 1 <= CHEATKEYS_MODE_MASK >> CHEATKEYS_MODE_SHIFT, "the modes fit their bits");

u32 cheatkeys_count(u16 v)
{
	u32 n = 0;
	for (u32 held = cheatkeys_buttons(v); held; held &= held - 1)
		n++;
	return n;
}

s32 cheatkeys_next(u16 v, s32 after)
{
	for (s32 b = after < 0 ? 0 : after + 1; b < CHEATKEYS_BUTTON_COUNT; b++)
		if (v & cheatkeys_bit((u32)b))
			return b;
	return -1;
}

u32 cheatkeys_names(u16 v, char *out, u32 room)
{
	u32 n = 0;
	if (!room)
		return 0;
	out[0] = 0;
	for (s32 b = cheatkeys_first(v); b >= 0; b = cheatkeys_next(v, b))
		n += lang_fill(out + n, room - n, n ? "{1}{2}" : "{2}", LS(S(STR_BUTTON_JOIN)),
			       LS(S(STR_BUTTON_L + (u32)b)));
	return n;
}

u32 cheatkeys_sentence(u16 v, char *out, u32 room)
{
	static const u16 ids[CHEATKEYS_MODES] = {
		[CHEATKEYS_ALWAYS] = STR_ACTIVATION_ALWAYS,
		[CHEATKEYS_HOLD] = STR_ACTIVATION_HOLD,
		[CHEATKEYS_PRESS] = STR_ACTIVATION_PRESS,
		[CHEATKEYS_LAUNCH] = STR_ACTIVATION_LAUNCH,
	};
	char names[CHEATKEYS_BUTTON_COUNT * 24];
	if (!room)
		return 0;
	out[0] = 0;
	if (!v || !cheatkeys_valid(v))
		return 0;
	cheatkeys_names(v, names, sizeof(names));
	return SF(out, room, ids[cheatkeys_mode(v)], LS(names));
}

u32 cheatkeys_tag(u16 v, char *out, u32 room)
{
	u32 mode = cheatkeys_mode(v);
	if (!room)
		return 0;
	out[0] = 0;
	if ((mode != CHEATKEYS_HOLD && mode != CHEATKEYS_PRESS) || !cheatkeys_valid(v))
		return 0;
	const char *first = S(STR_BUTTON_SHORT_L + (u32)cheatkeys_first(v));
	u32 more = cheatkeys_count(v) - 1;
	if (!more)
		return lang_fill(out, room, "{1}", LS(first));
	return SF(out, room, STR_BUTTON_MORE, LS(first), LN(more));
}
