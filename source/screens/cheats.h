// The cheat screen (v0.7): a GBA game's cheats from its cheat file
// (cheatstore.h), opened from the game popup's Cheats. One row a cheat
// (master codes hidden), a tick on the picked ones: A picks or drops one, B
// saves the picks (only if one changed) and goes back. Rows A can't pick are
// greyed, their reason on the hint bar: too many writes (beside the picked
// ones), can't read, can't run on this cart, edit to use. Every launch with
// addons applies the saved picks (cheatstore_launch).
//
// "+ Add code" ends the list: the code keypad (keypad.h), then the rename
// keyboard for its name, and the code goes into the file as the user's
// (made on this first save for a game that had none). A on a greyed code
// with digits to fill in opens the keypad with it; SELECT on a code of the
// user's opens Edit code and Delete. Each edit is written at once.
#ifndef TORII_SCREENS_CHEATS_H
#define TORII_SCREENS_CHEATS_H

#include <gba_base.h>

// Runs the screen for the game at rom_path, titled title with the 16x16
// icon icon (ART_ICON16_*), in the menu's session (its views popped first),
// and returns with no view up and the screen gone (ui_leave).
void cheats_run(const char *rom_path, const char *title, u16 icon);

#endif
