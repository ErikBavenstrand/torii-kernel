// The code keypad's model (screens/keypad.c): the code lines being typed
// with their caret, and what the code reads as. Hardware-free: tests/host
// runs it under ASan and UBSan.
//
// A line holds a code line's characters as typed, without the space a file
// puts in a CodeBreaker line ("82001234 0063" is 820012340063 here): hex
// digits, ':' for VBA's addr:value, AnyGBA's '[', ']', '+' (a pointer's
// offset), '^' and '|', and the stand-ins of a code to fill in ('?', 'X',
// 'Y', 'Z'), which typing replaces one at a time.
#ifndef TORII_KEYPAD_EDIT_H
#define TORII_KEYPAD_EDIT_H

#include <gba_base.h>

#include "cheatfile.h"

#define KEYPAD_LINES 32  // code lines in a cheat typed here
// Characters of a line: [PPPPPPPP]+OOOOOOOO:VVVVVVVV^[KKKKKKKK]+OOOOOOOO
#define KEYPAD_LINE_MAX 48
// A code as a file stores it, at most: lines, a '+' between them, and "+0"
// after a key's pointer that ends one (keypad_edit_code)
#define KEYPAD_CODE_MAX (KEYPAD_LINES * (KEYPAD_LINE_MAX + 3))

struct keypad_edit {
	char text[KEYPAD_LINES][KEYPAD_LINE_MAX + 1];  // NUL-terminated
	u8 len[KEYPAD_LINES];
	u8 lines;  // 1 or more
	u8 line;   // the caret's line
	u8 caret;  // and its place in it, 0 to len
};

// 1 for a digit's stand-in in a code to fill in (cheatcode.c's)
int keypad_is_mark(char c);
// The code len bytes long, as a file stores it ('+' or spaces between
// lines, a '+' in one after a pointer's ']' as cheat_plus_joins says, a
// CodeBreaker line in one or two parts), into e, the caret on its first
// stand-in, else at the end; 0 if it has more lines than KEYPAD_LINES, a
// part longer than KEYPAD_LINE_MAX or a character the keypad can't type (e
// is then empty)
int keypad_edit_init(struct keypad_edit *e, const char *code, u32 len);

// Whether the keypad types every character of code: hex digits, stand-ins,
// ':', AnyGBA's '[', ']', '^' and '|', and separators
int keypad_edit_typable(const char *code, u32 len);
// 1 for a character keypad_edit_type puts in: a hex digit (in any case),
// ':', '[', ']', '+', '^' or '|'
int keypad_edit_key(char c);
// Types c (keypad_edit_key's) at the caret: a digit over a stand-in there
// (the caret then on the next one, else past it), else put in. 0 if the
// line is full, c isn't one of those, or it is a '+' that doesn't follow a
// ']' (within a line, '+' is only a pointer's offset).
int keypad_edit_type(struct keypad_edit *e, char c);
// Deletes the character before the caret; at a line's start, joins it to
// the line before. 0 if nothing changed.
int keypad_edit_delete(struct keypad_edit *e);
// Breaks the line at the caret, the caret at the new line's start; 0 at
// KEYPAD_LINES or on an empty line
int keypad_edit_break(struct keypad_edit *e);
// The caret to the line before (dir -1) or after (1), on its first stand-in
// or at its end; 0 at that end
int keypad_edit_line(struct keypad_edit *e, s32 dir);
// The stand-ins left
u32 keypad_edit_marks(const struct keypad_edit *e);
// Line i's digits so far (stand-ins count: they are digits to come), and
// into *expect the count a whole line of that shape has: 12 for CodeBreaker
// (format, or a line of 12 digits or fewer in CHEAT_FMT_AUTO), 16 for the
// GameShark and Action Replay ones; 0 for a VBA line (with ':', or
// AnyGBA's characters), whose value's width varies
u32 keypad_edit_digits(const struct keypad_edit *e, u32 i, u32 format, u32 *expect);
// The code as a file stores it into out (size bytes, KEYPAD_CODE_MAX + 1
// is room for any): the lines that aren't empty, '+' between them, a
// CodeBreaker or GameShark line in two parts ("82001234 0063"), and "+0"
// after a key's pointer that ends a line with more after it, so the '+'
// before the next one can't read as its offset. Its length.
u32 keypad_edit_code(const struct keypad_edit *e, char *out, u32 size);
// Line i as the screen shows it, a space after its address's 8 digits in a
// line of digits only (out: KEYPAD_LINE_MAX + 2 bytes); its length. *caret
// (if not NULL) gets where the caret falls in it, for the caret's line.
u32 keypad_edit_show(const struct keypad_edit *e, u32 i, char *out, u32 *caret);

// What the typed code reads as
enum keypad_status {
	KEYPAD_EMPTY,     // nothing typed
	KEYPAD_OK,        // it decodes, every op in the GBA's memory: it can be saved
	KEYPAD_SHAPE,     // a line is no code line's shape (its digits so far: check.line)
	KEYPAD_FILL,      // stand-ins left
	KEYPAD_WRONG,     // it doesn't decode in this format
	KEYPAD_ADDRESS,   // it decodes, but to an address off the GBA's map (a wrong format or key)
	KEYPAD_MASTER,    // master codes only: Torii hooks the game itself
	KEYPAD_BIG,       // past a cheat's budget
	KEYPAD_CANT_RUN,  // an op Torii can't run (cheatfile.h's CHEATFILE_CANT_RUN)
	KEYPAD_STATUSES
};

struct keypad_check {
	u8 status;  // enum keypad_status
	u8 format;  // the format it reads in: the forced one, else as decoded (or detected)
	u8 line;    // KEYPAD_SHAPE's line
	s32 score;  // the first line's plausibility as a GameShark format (cheat_detect)
	u32 ops;    // ops decoded; the first is w->ops[0]
	u32 patches;  // ROM patches decoded beside them (w->patches)
};

// Checks the code: in format (CHEAT_FMT_AUTO: detected, decoded with ctx,
// the file's decoder as it is before the cheat's place), into w's buffers,
// the code put together in scratch (KEYPAD_CODE_MAX + 1 bytes)
void keypad_check(const struct keypad_edit *e, u8 format, const struct cheat_decoder *ctx, struct cheatfile_work *w,
		  char *scratch, struct keypad_check *out);

#endif
