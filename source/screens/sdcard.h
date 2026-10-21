// The SD card page (v0.4 decisions, B14; mockups #14 and #15): the card's
// folders as they are, raw file names, folders first, with sizes and item
// counts, and path pills in the title bar inside a folder.
//
// Keys: UP/DOWN move, L/R and LEFT/RIGHT page, A opens a folder or starts a
// game (L held: a hard reset), START opens the file popup (Launch, Write to
// NOR, Write to NOR with addon while an addon is on, Add to favourites,
// Delete, File info), B goes up a folder, and from the card's root home.
#ifndef TORII_SCREENS_SDCARD_H
#define TORII_SCREENS_SDCARD_H

#include "nav.h"

extern const struct nav_page sdcard_page;

#endif
