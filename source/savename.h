// Save file names, and the names of a game's other files, from the game's
// file name.
#ifndef TORII_SAVENAME_H
#define TORII_SAVENAME_H

#include <gba_base.h>

// The longest file name the kernel lists and opens: FatFs's name buffer
// (FF_LFN_BUF) holds 255 bytes of UTF-8. A longer name lists as its 8.3 name.
#define GAME_NAME_MAX 255
// Bytes save_name writes at most: a 255-byte .gb name grows by one
#define SAVE_NAME_MAX (GAME_NAME_MAX + 2)

// Writes the save file name for game into out (SAVE_NAME_MAX bytes): the
// name with its extension's letters replaced, by sav for a GBA game and by
// esv for an emulated one. is_emu is Check_file_type's value: 0 GBA, 1 GBC,
// 2 GB (two letters, so the name grows by one), 3 NES. Only game's first
// GAME_NAME_MAX bytes count.
void save_name(char *out, const char *game, u32 is_emu);

// Writes game's name with its last three bytes replaced by ext (three
// letters) into out (GAME_NAME_MAX + 1 bytes), as the kernel names a game's
// patch, save type, cheat and savestate files; a name under three bytes
// gets ext added. Only game's first GAME_NAME_MAX bytes count.
void name_with_ext(char *out, const char *game, const char *ext);

#endif
