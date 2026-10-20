// The menu's navigation shell (v0.4 decisions, Navigation; ticket B10):
// home and a page per item (screens/registry.h), opening and going back
// between them, where the menu starts (the Start on setting), the page
// frame, and the menu's loop, which also runs the library's background
// walk, writes the log and the boot position while the menu waits, and
// keeps the clock going.
//
// Keys: on home LEFT/RIGHT turn the carousel and A opens the item it
// faces; on a page B goes back home, facing its item.
// Opening and going back are plain fades until B19.
//
// A page is a struct nav_page: it pushes its views when the shell opens it
// and reads its keys in its own handlers. Its handlers ask for what can't
// run inside a frame (going back, an action from actions.h) with nav_back
// and nav_defer; the loop does it after the frame. Pages whose ticket
// hasn't landed show an empty frame: the title bar, an empty card and the
// hint bar.
#ifndef TORII_SCREENS_NAV_H
#define TORII_SCREENS_NAV_H

#include <gba_base.h>

#include "registry.h"

struct ui_view;

struct nav_page {
	// Pushes the page's views for item with its cursor on row (as Last
	// place left it, or 0: recent.txt may hold any number, so the page
	// clamps it to its rows); returns the page's first view, or NULL if it
	// didn't fit (the reason in ui_error)
	struct ui_view *(*enter)(u32 item, u32 row);
	// The row the page is on, for Last place (NULL: 0)
	u32 (*row)(void);
	// The clock's text changed (empty when the clock isn't set, T4): for a
	// title bar that shows it (NULL: none)
	void (*clock)(const char *text);
	// Each pass of the menu's loop, after the frame (NULL: none)
	void (*poll)(void);
};

// The menu, from the boot (ezkernelnew.c); never returns. Launches and NOR
// writes run engine sessions of their own: the shell leaves the engine for
// them and comes back after.
void nav_run(void) __attribute__((noreturn));

// From a page's handler: back to home, facing the page's item, after the
// frame
void nav_back(void);
// From a handler: runs fn(arg) after the frame, outside ui_frame, where it
// may show dialogs, read the card or call actions.h. If an action ended the
// engine session (a launch that didn't start), the shell brings the page
// back afterwards. One at a time: a second before the first ran replaces it.
void nav_defer(void (*fn)(u32 arg), u32 arg);
// From a deferred fn whose action changed what its page shows (a NOR game
// deleted): the place fades out and is shown again from scratch, its page's
// enter called with the row it is on, once the fn returns
void nav_reshow(void);

// For home and the pages: an item's name, and what it holds ("176 games",
// "1,204 files", "one page"; "" for Info)
const char *nav_item_name(u32 item);
const char *nav_item_count(u32 item);
// n with thousands separators ("1,204"), then many ("1,204 files"), or one
// for 1 ("1 file"); NULL one or many: the number alone
void nav_count_words(char *out, u32 size, u32 n, const char *one, const char *many);
// The clock as the title bar shows it ("14:32", "2:32 PM"; "" while it
// isn't set)
const char *nav_clock(void);

// Tests: tests/nav.sh's hook. mgba-shot writes a path into nav_test_path
// and then an action into nav_test_action (NAV_TEST_*, with bits 8-15 the
// save type plus 1, 0 for the game's own, bit 16 a hard reset, and bits
// 24-31 NAV_TEST_MAGIC); the menu runs it through actions.h once it is up
// and still, as a page would. Both stay 0 on a cart, and the menu takes
// the hook only in the emulator, with the magic: a stray write to EWRAM
// can't erase the user's NOR.
#define NAV_TEST_MAGIC 0x7Eu
enum {
	NAV_TEST_NONE,
	NAV_TEST_CLEAN,      // action_launch(path, ACTION_CLEAN)
	NAV_TEST_ADDON,      // ... ACTION_ADDON
	NAV_TEST_NOR,        // ... ACTION_NOR
	NAV_TEST_NOR_ADDON,  // ... ACTION_NOR_ADDON
	NAV_TEST_DELETE,     // action_delete_file(path)
	NAV_TEST_NOR_LAUNCH,  // action_nor_launch(bits 8-15)
	NAV_TEST_NOR_DELETE,  // action_nor_delete()
	NAV_TEST_NOR_FORMAT,  // action_nor_format()
	NAV_TEST_ACTIONS,
};
extern volatile u32 nav_test_action;
extern char nav_test_path[];

#endif
