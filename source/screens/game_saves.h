// The per-game save manager (screens/game_saves.c; v0.5): a game's slots with
// the current one marked, and for a slot Launch, Set as current, Rename,
// Duplicate, Delete, History (and Restore) and Export; New slot and Import a
// .sav from the card. Reached by "Saves" in the game's Start menu (games, SD
// card and game NOR pages). Every action is slotstore.h's; New slot and
// Rename are slot_flow.h's, the lists are slot_list.h's.
//
// It takes the place of the menu's session screen as the rename keyboard
// does, so call it from a page's deferred action outside ui_frame, with the
// popup closed; when it returns nothing shows and the caller draws what comes
// next (the page again, or the launch). It reads and writes the card, so never
// inside another FatFs operation.
#ifndef TORII_SCREENS_GAME_SAVES_H
#define TORII_SCREENS_GAME_SAVES_H

#include <gba_base.h>

enum {
	GAME_SAVES_BACK = 0,    // B: nothing to start
	GAME_SAVES_LAUNCH = 1,  // "Launch with this slot": it is the current one; start the game
};

// Whether the game at path (a GBA game on the card, or a game NOR entry, as
// action_launch takes it) has saves to manage (action_game_saves); if so the
// next game_saves_run is about it. The popups call it to show "Saves".
int game_saves_probe(const char *path);
// Runs the manager for the game of the last probe, title its name for the
// title bar
int game_saves_run(const char *title);

#endif
