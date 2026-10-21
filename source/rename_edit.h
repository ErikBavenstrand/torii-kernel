// The rename keyboard's model (screens/rename.c; docs/design/v0.4/
// mockups-b16-b18.md, "Rename keyboard"): the name being typed with its
// caret, the grid's keys and how the selection moves over them.
// Hardware-free: tests/host runs it under ASan and UBSan.
#ifndef TORII_RENAME_EDIT_H
#define TORII_RENAME_EDIT_H

#include <gba_base.h>

// --- The name ------------------------------------------------------------------
//
// UTF-8, one character a code point (é counts once). A prefilled name may be
// longer than the cap or hold characters the keyboard never offers: it keeps
// them until they are deleted.

#define RENAME_TEXT_MAX 255  // bytes of a name (a FAT file name's)

struct rename_edit {
	char text[RENAME_TEXT_MAX + 1];
	u16 len;    // bytes
	u16 caret;  // a byte offset at a character's start
	u8 max;     // characters a key may type up to
};

// The name text (cut at a character to RENAME_TEXT_MAX bytes), the caret at
// its end, typing up to max characters.
void rename_edit_init(struct rename_edit *e, const char *text, u32 max);
// Its characters, and those before the caret
u32 rename_edit_chars(const struct rename_edit *e);
u32 rename_edit_caret_chars(const struct rename_edit *e);
// Types Latin-1 character c (0x20-0xFF) at the caret; 0 if the name is full
int rename_edit_type(struct rename_edit *e, u32 c);
// Deletes the character before the caret; 0 at the start
int rename_edit_delete(struct rename_edit *e);
// Moves the caret a character left (dir -1) or right (1); 0 at that end
int rename_edit_move(struct rename_edit *e, s32 dir);
// The name as OK keeps it, into out (RENAME_TEXT_MAX + 1 bytes): spaces
// at the ends trimmed, control characters dropped, and for a file name
// (file 1) the characters FAT refuses (\ / : * ? " < > |) dropped too.
// Returns its length.
u32 rename_edit_result(const struct rename_edit *e, char *out, int file);

// --- The keys ------------------------------------------------------------------
//
// A 5 x 10 grid: four rows of characters, then the function row, whose keys
// span columns (RENAME_FUNCS). The character rows have two pages (letters
// and digits; Latin-1), each in lower and upper case.

#define RENAME_COLS 10
#define RENAME_CHAR_ROWS 4
#define RENAME_ROWS 5
#define RENAME_FUNC_ROW 4

enum {
	RENAME_PAGE_ABC,
	RENAME_PAGE_LATIN,
	RENAME_PAGES,
};

enum {
	RENAME_KEY_SHIFT,
	RENAME_KEY_LATIN,
	RENAME_KEY_SPACE,
	RENAME_KEY_DELETE,
	RENAME_KEY_CANCEL,
	RENAME_KEY_OK,
	RENAME_FUNCS,
};
// The columns each function key spans, in order
extern const u8 rename_func_span[RENAME_FUNCS];
// The function key over column col, and the first column of key k
u32 rename_func_at(u32 col);
u32 rename_func_col(u32 k);

// The Latin-1 character at (row, col) of a character row, on page in case
// upper
u32 rename_key(u32 page, int upper, u32 row, u32 col);
// 1 if Latin-1 character c is a letter (it has another case)
int rename_is_letter(u32 c);

// The selection: its row and column; on the function row, col is the
// column it came down in (or the key's first, once LEFT or RIGHT moved it
// there), so UP and DOWN through the wide keys come back to the same letter
struct rename_sel {
	u8 row, col;
};
// Moves sel for a d-pad key (INPUT_UP ... from engine/input.h's bits): a
// fresh press (fresh 1) wraps at the edges, a held key's repeats stop there.
// 1 if it moved.
int rename_sel_move(struct rename_sel *sel, u16 key, int fresh);

#endif
