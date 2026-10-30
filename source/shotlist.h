// A game's screenshots on the card (docs/design/v0.9/shot-viewer.md): how
// many it has, for the game popup's Screenshots, from a count table read
// once a session from /torii/screenshots (shotname.h says which game a
// shot is).
//
// The table lives in PSRAM page 2 (SHOTLIST_PAGE), selected only inside
// these calls and the viewer's (screens/shots.c), with a magic and a CRC:
// a launch that fills PSRAM breaks them, and the next popup reads the
// folder again. Borrows pReadCache from READCACHE_SHOTS_LIST within a call.
#ifndef TORII_SHOTLIST_H
#define TORII_SHOTLIST_H

#include <gba_base.h>

#include "shotname.h"

#define SHOTLIST_DIR "/torii/screenshots"
#define SHOTLIST_PAGE 0x2000  // SetPSRampage's value: page 2, free while the menu runs
// Page 2's map, from the window's start while it is selected
#define SHOTLIST_CACHE_OFFSET 0x000000u  // the viewer's decoded pictures
#define SHOTLIST_STAGE_OFFSET 0x140000u  // the picture being decoded, as RGB15
#define SHOTLIST_NAMES_OFFSET 0x200000u  // the viewer's list's names
#define SHOTLIST_TABLE_OFFSET 0x300000u  // the count table
#define SHOTLIST_GAMES_MAX 2048  // games the count table holds
#define SHOTLIST_FILES_MAX 8192  // shots it counts

// The stem of game: an SD path, or a NOR entry (USERDATA_NOR + its name)
void shotlist_stem(char *out, u32 size, const char *game);

// How many shots game has; the first call of a session (or after
// shotlist_forget) reads the folder. 0 for none, or if the folder can't be read.
u32 shotlist_count(const char *game);

// The folder changed (the viewer's delete): the next count reads it again
void shotlist_forget(void);
// A file at path is deleted or renamed: shotlist_forget if it is in the folder
void shotlist_changed(const char *path);

#endif
