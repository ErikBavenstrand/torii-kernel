// The NOR page (v0.4 decisions, Pages; ticket B15; mockups #16 and #17):
// what the game NOR holds and the games on it, in the order they were
// written, and Format all.
//
// One card holds the storage line (used, and free of 64 MB) over a bar of
// each game's share, then the games, four rows of 14 px at a time; the
// Format all row is a card of its own under it. UP and DOWN move through
// the games and on to Format all (wrapping at the ends on a fresh press).
// On a game, A launches it (L held: a hard reset, as in v0.3) and START
// opens its popup: Launch, Game info and Delete. Only the latest game can
// be deleted (the NOR keeps its games back to back): Delete on another
// says so. On Format all, A asks first (Cancel preselected), then erases
// the NOR. B goes back to home.
#ifndef TORII_SCREENS_NOR_H
#define TORII_SCREENS_NOR_H

#include "nav.h"

extern const struct nav_page nor_page;

#endif
