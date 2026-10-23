// The save screens' page (screens/save_page.c; docs/design/v0.5/saves-variants.md,
// direction B): a list on the left, the row under the cursor's facts on the
// right, as the list with art shows a game's. A game's slots and their
// backups, and the launch's "Play with", are each one of these.
//
// The page stays up while its caller opens popups beside its row (the
// list's and hint bar's nodes) and dialogs over it; it takes the place of
// the menu's session screen, so call it from a loop outside ui_frame, as the
// rename keyboard is. Anything that takes the whole screen (the keyboard)
// needs the page closed first and opened again after.
#ifndef TORII_SCREENS_SAVE_PAGE_H
#define TORII_SCREENS_SAVE_PAGE_H

#include <gba_base.h>

#include "../ui/ui.h"

#define SAVE_PAGE_ROWS 24  // rows a page lists, at most

struct save_page_row {
	const char *text;  // UTF-8, kept by pointer while the page shows it
	u8 mark;           // ART_ICON8_* + 1 before it (the active slot); 0 none
	u8 ink;            // its text's role, 0: TEXT (a New slot row: ACCENT_INK)
};

// The pane for a row: texts the page keeps by pointer until the next call
struct save_pane {
	const char *head;    // the row's name, in the heading font
	const char *tag;     // a word under it in the accent's ink ("Active"); NULL none
	const char *keys;    // the facts' names for this row; NULL: the page's
	const char *values;  // "\n" between them, beside the keys
	const char *note;    // up to two lines ("\n"), faint, at the pane's foot
};

struct save_page {
	const char *name;   // the view's name, logged as "screen ready: NAME"
	u16 icon16;         // the title bar's ART_ICON16_*
	const char *title;  // the title bar
	const char *right;  // its right-hand text (the game's name); NULL none
	const char *keys;   // the facts' names, "\n" between them, the same for every row
	const struct save_page_row *rows;
	u32 count;   // 1 to SAVE_PAGE_ROWS
	u32 cursor;  // the row to start on
	// Fills the pane for row (called when the cursor moves, in a frame's
	// event: no FatFs, nothing that runs frames)
	void (*pane)(void *ctx, u32 row, struct save_pane *out);
	// The hint bar's keys for row (they change with it: a New slot row's
	// differ); up to 4
	void (*hints)(void *ctx, u32 row, const struct ui_hint **keys, u32 *count);
	void *ctx;
	u16 end_keys;  // keys besides A and B that end save_page_wait (INPUT_START, INPUT_LEFT, INPUT_RIGHT)
};

// Shows the page (the session's screen goes out first); 0 if it can't
// (logged). p is kept by pointer while it is up.
int save_page_open(const struct save_page *p);
// Runs frames until A, B or one of end_keys; returns that key (one bit),
// and the cursor's row in *row. 0 if the page went (the session ended).
u16 save_page_wait(u32 *row);
// Rows changed in place (a rename, a delete): the list shows count rows of
// rows from cursor, and the pane follows
void save_page_rows(const struct save_page_row *rows, u32 count, u32 cursor);
// The pane and hints again for the cursor's row (its facts changed)
void save_page_refresh(void);
// The list's and hint bar's nodes, for a popup or a confirmation beside the row
u8 save_page_list(void);
u8 save_page_hints(void);
// How long ago unix time t was, in the menu's words ("2 days"); with the
// clock unset now, its date ("10-23"); "unknown" (gl_gsaves) for no time
void save_page_ago(char *out, u32 room, u32 t);
// t's month, day, hour and minute ("10-23 18:30")
void save_page_when(char *out, u32 room, u32 t);
// Closes the page (it goes out); nothing shows after
void save_page_close(void);

#endif
