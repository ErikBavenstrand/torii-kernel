// The launch's save slot picker (slot_picker.c; v0.5 decisions): a page that
// lists a game's slots, the last-used one chosen, with a "New slot..." row.
// A confirms (the slot becomes the game's current one), START renames the
// highlighted slot (the GBA has no X key), B cancels. New slot and Rename are
// slot_flow.h's.
//
// A game with one slot launches straight in, a game with two or more shows
// the picker on every launch, and Select held with A on a game shows it
// whatever the count (so New slot is always reachable).
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
