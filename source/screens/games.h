// The game lists (v0.4 decisions, Pages; ticket B13): one page for each
// console, Recent and Favourites. A list card of 8 rows of 14 px: the name
// the game shows (B4's, or a custom one), a star for a favourite, a dot for
// a save, and the size; Recent's and Favourites' rows lead with the
// console's mark, and Recent's end with when the game was played instead
// of its size. The consoles and Favourites list A-Z, Recent newest first.
// With Settings' "list with art" view the card keeps the placeholder box
// and two facts beside a narrower list.
//
// Keys: UP/DOWN move, L/R page by a screenful, LEFT/RIGHT jump to the
// previous or next first letter with the letter card (A-Z lists only), A
// plays (with the addons Settings turns on; L held: a hard reset), START
// opens the game popup (Play, Favourite, Clean boot, Boot with addon, Game
// info, Delete), B goes back home. SELECT does nothing (T8).
#ifndef TORII_SCREENS_GAMES_H
#define TORII_SCREENS_GAMES_H

#include "nav.h"

// The page nav.c opens for Recent, Favourites and each console
extern const struct nav_page games_page;

#endif
