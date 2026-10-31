// A short list of choices on a page of its own (screens/slot_list.c): the
// launch's slot picker and the New slot choice (Blank or a copy). It takes the
// place of whatever the menu's session shows, as the rename keyboard does, and
// when it returns nothing shows: the caller draws what comes next.
//
// Keys: UP and DOWN move, A picks the row, B cancels, START (the GBA has no X)
// picks the row for `extra` (the picker's rename).
#ifndef TORII_SCREENS_SLOT_LIST_H
#define TORII_SCREENS_SLOT_LIST_H

#include <gba_base.h>

#include "../ui/ui.h"

#define SLOT_LIST_MAX 20

struct slot_list_row {
	const char *text;    // UTF-8, copied by pointer: it must outlive the call
	const char *detail;  // right-aligned, faint (a size); NULL none
	u8 mark;             // ART_ICON8_* + 1 before the text; 0 none
	u8 more;             // 1: A opens it (a folder): a chevron at the right (ui_item.more)
};

struct slot_list {
	const char *name;   // the view's name, logged as "screen ready: NAME"
	const char *title;  // the title bar
	const char *sub;    // after it, faint (the game's name); NULL none
	const struct slot_list_row *rows;
	u32 count;  // 1 to SLOT_LIST_MAX
	u32 cursor;  // the row to start on
	const struct ui_hint *hints;
	u32 hint_count;  // up to 4
	u8 extra;        // 1: START ends the list too (result SLOT_LIST_EXTRA)
};

enum {
	SLOT_LIST_CANCEL = 0,  // B
	SLOT_LIST_PICK = 1,    // A on *row
	SLOT_LIST_EXTRA = 2,   // START on *row
};

// Runs the page until a key ends it; the row the cursor is on goes to *row.
// Call it from a loop outside ui_frame, in the menu's session (or one the
// caller opened with dialog_open).
int slot_list_run(const struct slot_list *list, u32 *row);

#endif
