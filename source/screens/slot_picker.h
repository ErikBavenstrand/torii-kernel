// The launch's save slot choice (slot_picker.c): "Play with" on the save
// page, a game's slots with the active one chosen. A plays it (it becomes the
// active slot), B starts nothing. Managing slots is a game's Saves page's.
//
// A game with one slot (or none yet) launches straight in; one with two or
// more asks on every launch, and Select held with A asks whatever the count.
#ifndef TORII_SCREENS_SLOT_PICKER_H
#define TORII_SCREENS_SLOT_PICKER_H

#include <gba_base.h>

enum {
	SLOT_PICKER_ERROR = -1,   // a failure was shown: do not launch
	SLOT_PICKER_CANCEL = 0,   // B: do not launch
	SLOT_PICKER_LAUNCH = 1,   // the slot is the current one now: go on
};

// 1 if a launch of the game at rom_path shows the picker: force (Select held)
// or two or more slots. Reads the card.
int slot_picker_wanted(const char *rom_path, int force);

// Shows the picker for the game at rom_path (title: its name, in the title bar),
// blank_size: the save size a new blank slot gets (slot_flow_new's). Runs in
// the menu's session, or opens one. The result is an enum above; on LAUNCH
// slotstore_current() is the chosen slot.
int slot_picker_run(const char *rom_path, const char *title, u32 blank_size);

#endif
