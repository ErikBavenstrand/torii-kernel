// The slot flows a game's saves share (slot_flow.c): asking for a slot name
// on the on-screen keyboard, and Rename, with what the slot store's codes
// say. A game's saves page (game_saves.h) and the launch's Play with
// (slot_picker.h) call them.
//
// All of them take the place of the menu's session screen (the keyboard and
// the choice list pop what is up, as rename_run does), so call them from a
// page's loop outside ui_frame, in the menu's session or one opened with
// dialog_open(); when they return nothing shows and the caller draws what
// comes next. They read and write the card (slotstore.h), so never inside
// another FatFs operation. A game is named by its ROM path from the root
// (gamesave.h, as slotstore.h says).
//
// Failures are shown (a dialog) before the call returns: SLOT_FLOW_FAILED
// says it was shown. Cancelling (B, or Cancel on the keyboard, or OK on a
// name that did not change) changes nothing and returns SLOT_FLOW_CANCEL.
#ifndef TORII_SCREENS_SLOT_FLOW_H
#define TORII_SCREENS_SLOT_FLOW_H

#include <gba_base.h>

#include "../saveslot.h"
#include "../slotstore.h"

enum {
	SLOT_FLOW_FAILED = -1,
	SLOT_FLOW_CANCEL = 0,
	SLOT_FLOW_DONE = 1,
};

// Asks for a slot name with title (the keyboard's title bar), starting from
// initial (UTF-8, "" for a new slot). OK is refused, with the reason in the
// hint area and the keyboard staying, for a name with nothing usable once
// cleaned (saveslot_clean_name) and for one another slot of the game has
// (ignoring case); own (NULL for none) is the slot being renamed, which may
// keep its name or change its case. Returns SLOT_FLOW_DONE with the cleaned
// name in out (SAVESLOT_NAME_BYTES), or SLOT_FLOW_CANCEL.
int slot_flow_name_prompt(const char *rom, const char *title, const char *initial, const char *own, char *out);

// Rename: the name prompt with the slot's name, then slotstore_rename (the
// current-slot record follows). DONE puts the new name in renamed
// (SAVESLOT_NAME_BYTES, may be NULL).
int slot_flow_rename(const char *rom, const char *slot, char *renamed);

// What a slotstore_* code says, in a line (gl_slots)
const char *slot_flow_error_text(int code);
// Shows slot_flow_error_text(code) as an error dialog
void slot_flow_error(int code);

// The game's slots, as slotstore_list gives them, in the module's table of
// SLOTSTORE_MAX_SLOTS (valid until the next call here that lists); returns
// the count (at most SLOTSTORE_MAX_SLOTS) or a negative slotstore code.
int slot_flow_slots(const char *rom, const struct slotstore_slot **out);

#endif
