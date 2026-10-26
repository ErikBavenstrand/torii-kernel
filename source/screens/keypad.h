// The code keypad (v0.7): a page in the menu's session where a cheat's code
// is typed in hex, opened from the cheat screen's "+ Add code", a code of
// the user's (Edit code) and a code with digits to fill in (00??).
//
// The code's lines sit in a card, three in view, the caret's line scrolled
// to; each line has its format's tag at its right, and the digits a
// placeholder leaves to fill in are marked. Under the card, the format the
// code reads in (detected as it is typed, by cheatcode.h's cheat_detect and
// the decoding itself, or the one SELECT set) and what it decodes to, or
// why it doesn't read. Under those the keys: 0-F, ':' (VBA's
// address:value), Line (a new code line), Del, Cancel and Save.
//
// Keys: the d-pad moves the selection (a press wraps at the edges), A
// presses the key, B deletes before the caret (on an empty code, leaves;
// held, A and B repeat), L
// and R go to the line before and after, SELECT steps the format (auto,
// GameShark v1, its raw, Action Replay v3, its raw, CodeBreaker, VBA),
// START saves. Save takes only a code that reads; Cancel asks first when
// the code changed.
#ifndef TORII_SCREENS_KEYPAD_H
#define TORII_SCREENS_KEYPAD_H

#include <gba_base.h>

#include "../cheatcode.h"

struct keypad_ask {
	u16 icon16;         // the title bar's 16x16 icon (ART_ICON16_*)
	const char *title;  // the title bar
	const char *code;   // the code to start from, as a file stores it (NULL: none)
	u32 code_len;
	u8 format;          // the format to start in (CHEAT_FMT_AUTO: detected)
	// The file's decoder as it is before the cheat's place: an AUTO code
	// decodes as the file will read it (a GameShark version, a key)
	const struct cheat_decoder *ctx;
};

// Runs the keypad in the menu's session (its views popped first) until
// Save (1: the code, as a file stores it, in out, KEYPAD_CODE_MAX + 1
// bytes, and its format in *format, CHEAT_FMT_AUTO unless SELECT set one)
// or Cancel (0), or -1 if ask->code can't be edited here (keypad_edit_init
// refused it) and nothing was shown. Returns with no view up. ask->code may
// be out.
int keypad_run(const struct keypad_ask *ask, char *out, u8 *format);

#endif
