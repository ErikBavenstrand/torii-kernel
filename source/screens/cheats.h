// The cheat screen (v0.7): a GBA game's cheats from its cheat file
// (cheatstore.h), opened from the game popup's Cheats. One row a cheat
// (master codes hidden), a tick on the picked ones: A picks or drops one, B
// saves the picks (only if one changed) and goes back. The title bar says
// which cheat the cursor is on and how many are on. Rows A can't pick are
// greyed, their reason on the hint bar beside its keys: too much to run
// beside the picked ones (the budget's numbers), can't read, can't run on
// this cart, edit to use. Every launch with addons applies the saved picks
// (cheatstore_launch); leaving with a new pick for a game that boots clean
// says they won't run.
//
// "+ Add code" ends the list: the code keypad (keypad.h), then the rename
// keyboard for its name ("Code N" unless changed), and the code goes into
// the file as the user's (made on this first save for a game that had
// none); it is greyed, with why, when the file has no room. A on a greyed
// code with digits to fill in opens the keypad with it.
//
// Each row shows how its cheat is activated (docs/plans/cheat-guides.md):
// a Hold or Press cheat's first button as a dim tag at its right, and the
// cursor's cheat's sentence ("Hold L+SELECT") on the hint bar. SELECT opens
// a cheat's Guide page: its name, its activation (or why it is greyed) in a
// tinted box, its guide, then its code, format and budget, read-only.
// START's menu has Guide, Rename (Edit code on a code of the user's),
// Buttons (Always on, Hold, Press or From the code; Hold and Press capture
// the buttons as Settings' hotkeys do), Edit guide (the rename keyboard's
// text of lines), Turn all off, Show picked only (B shows all again) and
// Delete. Each edit is written at once; deleting the last code deletes the
// file too.
#ifndef TORII_SCREENS_CHEATS_H
#define TORII_SCREENS_CHEATS_H

#include <gba_base.h>

// Runs the screen for the game at rom_path, titled title with the 16x16
// icon icon (ART_ICON16_*), in the menu's session (its views popped first),
// and returns with no view up and the screen gone (ui_leave).
void cheats_run(const char *rom_path, const char *title, u16 icon);

#endif
