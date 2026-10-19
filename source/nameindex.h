// The SD list's file names, kept whole in PSRAM rather than EWRAM, so names
// of any length FatFs gives (up to GAME_NAME_MAX bytes) list, open, launch
// and save (docs/plans/v0.3-decisions.md, Text).
//
// The names live in the first 4 MB of PSRAM page 0 (engine/psram.h), read
// through the 0x08800000 window: a handle is a name's offset there. Listing
// a folder starts over (names_reset), and each name is copied in once, at
// the slow WAITCNT, between FatFs calls (the cart hides PSRAM while SD mode
// is on).
//
// A game launch fills PSRAM with the game, so the names are gone after one:
// whoever lists them lists the folder again before reading a name, and the
// launch path works from a copy in RAM (current_filename).
#ifndef TORII_NAMEINDEX_H
#define TORII_NAMEINDEX_H

#include <gba_base.h>

#include "engine/psram.h"

#define NAMES_BASE PSRAM_WINDOW
#define NAMES_BYTES 0x00400000u  // up to PSRAM_ASSET_BASE, where covers start
#define NAMES_NONE 0xFFFFFFFFu

// Forgets every name (a new folder).
void names_reset(void);
// Copies name (NUL-terminated, at most GAME_NAME_MAX bytes; RAM) into PSRAM
// and returns its handle, or NAMES_NONE if it is too long or the area is full.
u32 names_add(const char *name);
// The name with handle h, in PSRAM: read it, never write it.
static inline const char *names_get(u32 h)
{
	return (const char *)(NAMES_BASE + h);
}
// Bytes the names take.
u32 names_used(void);

#endif
