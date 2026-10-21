// The rename keyboard's model (source/rename_edit.c): typing, the caret and
// the cap over UTF-8, what OK keeps, the grid's keys, and how the
// selection moves (wrapping on a press, stopping on a held key's repeats).
#include <string.h>

#include "../../source/rename_edit.h"
#include "../kernel/torii_test.h"
#include "tests.h"

#define UP 0x040
#define DOWN 0x080
#define LEFT 0x020
#define RIGHT 0x010

void test_rename_edit(void)
{
	struct rename_edit e;
	rename_edit_init(&e, "Pok\xC3\xA9mon", 10);
	CHECK_EQ(e.len, 8);
	CHECK_EQ(rename_edit_chars(&e), 7);
	CHECK_EQ(rename_edit_caret_chars(&e), 7);
	// The caret steps over é whole, both ways
	for (int i = 0; i < 4; i++)
		CHECK(rename_edit_move(&e, -1));
	CHECK_EQ(e.caret, 3);
	CHECK(rename_edit_move(&e, 1));
	CHECK_EQ(e.caret, 5);
	CHECK(rename_edit_move(&e, -1));
	CHECK(rename_edit_delete(&e));  // the k
	CHECK(!strcmp(e.text, "Po\xC3\xA9mon"));
	CHECK(rename_edit_type(&e, 0xE8));  // è, two bytes
	CHECK(!strcmp(e.text, "Po\xC3\xA8\xC3\xA9mon"));
	CHECK_EQ(e.caret, 4);
	// The cap counts characters, not bytes
	CHECK(rename_edit_type(&e, 'x'));
	CHECK(rename_edit_type(&e, 'y'));
	CHECK(rename_edit_type(&e, 'z'));
	CHECK_EQ(rename_edit_chars(&e), 10);
	CHECK(!rename_edit_type(&e, 'w'));
	CHECK_EQ(rename_edit_chars(&e), 10);
	// The ends
	while (rename_edit_move(&e, -1))
		;
	CHECK_EQ(e.caret, 0);
	CHECK(!rename_edit_delete(&e));
	while (rename_edit_move(&e, 1))
		;
	CHECK_EQ(e.caret, e.len);
	CHECK(!rename_edit_type(&e, 0x1F) && !rename_edit_type(&e, 0x100));

	// A prefilled name past the cap and the bytes keeps what fits, cut at a
	// character
	char long_name[300];
	memset(long_name, 'a', sizeof(long_name));
	long_name[254] = '\xC3';
	long_name[255] = '\xA9';
	long_name[299] = 0;
	rename_edit_init(&e, long_name, 60);
	CHECK_EQ(e.len, 254);
	CHECK(!rename_edit_type(&e, 'b'));  // past the cap already
	CHECK(rename_edit_delete(&e));

	// OK's name: spaces at the ends and control characters go; FAT's
	// refused characters too for a file (out may be the text itself)
	char out[RENAME_TEXT_MAX + 1];
	rename_edit_init(&e, "  a\tb: c?  ", 60);
	CHECK_EQ(rename_edit_result(&e, out, 0), 6);
	CHECK(!strcmp(out, "ab: c?"));
	CHECK_EQ(rename_edit_result(&e, e.text, 1), 4);
	CHECK(!strcmp(e.text, "ab c"));
	rename_edit_init(&e, "   ", 60);
	CHECK_EQ(rename_edit_result(&e, out, 1), 0);
}

void test_rename_keys(void)
{
	CHECK_EQ(rename_key(RENAME_PAGE_ABC, 0, 1, 0), 'q');
	CHECK_EQ(rename_key(RENAME_PAGE_ABC, 1, 1, 0), 'Q');
	CHECK_EQ(rename_key(RENAME_PAGE_ABC, 1, 0, 0), '1');
	CHECK_EQ(rename_key(RENAME_PAGE_LATIN, 0, 0, 9), 0xE9);  // é
	CHECK_EQ(rename_key(RENAME_PAGE_LATIN, 1, 0, 9), 0xC9);  // É
	CHECK_EQ(rename_key(RENAME_PAGE_LATIN, 1, 3, 0), '#');
	CHECK_EQ(rename_key(RENAME_PAGE_ABC, 0, 4, 0), 0);
	// No key types a character FAT refuses in a name
	for (u32 p = 0; p < RENAME_PAGES; p++)
		for (int up = 0; up < 2; up++)
			for (u32 r = 0; r < RENAME_CHAR_ROWS; r++)
				for (u32 c = 0; c < RENAME_COLS; c++) {
					u32 k = rename_key(p, up, r, c);
					CHECK(k >= 0x20 && !strchr("\\/:*?\"<>|", (int)k) && k != 0xD7 && k != 0xF7);
				}
	CHECK(rename_is_letter('a') && rename_is_letter(0xDF) && !rename_is_letter('1') && !rename_is_letter(0xD7));
	// The function row spans the ten columns
	u32 cols = 0;
	for (u32 k = 0; k < RENAME_FUNCS; k++)
		cols += rename_func_span[k];
	CHECK_EQ(cols, RENAME_COLS);
	CHECK_EQ(rename_func_at(0), RENAME_KEY_SHIFT);
	CHECK_EQ(rename_func_at(6), RENAME_KEY_DELETE);
	CHECK_EQ(rename_func_at(9), RENAME_KEY_OK);
	CHECK_EQ(rename_func_col(RENAME_KEY_CANCEL), 7);
}

void test_rename_selection(void)
{
	struct rename_sel s = {1, 0};
	// A press wraps on both axes; a held key's repeats stop at the edge
	CHECK(rename_sel_move(&s, LEFT, 0) == 0 && s.col == 0);
	CHECK(rename_sel_move(&s, LEFT, 1) && s.col == 9);
	CHECK(rename_sel_move(&s, RIGHT, 1) && s.col == 0);
	CHECK(rename_sel_move(&s, UP, 0) && s.row == 0);
	CHECK(!rename_sel_move(&s, UP, 0) && s.row == 0);
	CHECK(rename_sel_move(&s, UP, 1) && s.row == RENAME_FUNC_ROW);
	CHECK(rename_sel_move(&s, DOWN, 1) && s.row == 0);
	// Through the function row the column stays: a zig-zag comes back
	s = (struct rename_sel){3, 5};
	CHECK(rename_sel_move(&s, DOWN, 1) && s.row == RENAME_FUNC_ROW);
	CHECK_EQ(rename_func_at(s.col), RENAME_KEY_SPACE);
	CHECK(rename_sel_move(&s, UP, 1) && s.row == 3 && s.col == 5);
	// On it, LEFT and RIGHT go key by key, to each one's first column
	s = (struct rename_sel){RENAME_FUNC_ROW, 5};
	CHECK(rename_sel_move(&s, RIGHT, 1) && rename_func_at(s.col) == RENAME_KEY_DELETE && s.col == 6);
	CHECK(rename_sel_move(&s, RIGHT, 0) && rename_func_at(s.col) == RENAME_KEY_CANCEL);
	CHECK(rename_sel_move(&s, RIGHT, 0) && rename_func_at(s.col) == RENAME_KEY_OK);
	CHECK(!rename_sel_move(&s, RIGHT, 0) && rename_func_at(s.col) == RENAME_KEY_OK);
	CHECK(rename_sel_move(&s, RIGHT, 1) && rename_func_at(s.col) == RENAME_KEY_SHIFT);
	CHECK(rename_sel_move(&s, LEFT, 1) && rename_func_at(s.col) == RENAME_KEY_OK && s.col == 9);
}
