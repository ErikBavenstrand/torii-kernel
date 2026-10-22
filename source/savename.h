// The names of a game's other files (patch, cheat, save state) from the
// game's file name.
#ifndef TORII_SAVENAME_H
#define TORII_SAVENAME_H

#include <gba_base.h>

// The longest file name the kernel lists and opens: FatFs's name buffer
// (FF_LFN_BUF) holds 255 bytes of UTF-8. A longer name lists as its 8.3 name.
#define GAME_NAME_MAX 255
// Writes game's name with its last three bytes replaced by ext (three
// letters) into out (GAME_NAME_MAX + 1 bytes), as the kernel names a game's
// patch, save type, cheat and savestate files; a name under three bytes
// gets ext added. Only game's first GAME_NAME_MAX bytes count.
void name_with_ext(char *out, const char *game, const char *ext);

#endif
