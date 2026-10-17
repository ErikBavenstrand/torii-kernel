// Save file names, from the game's file name.
#ifndef TORII_SAVENAME_H
#define TORII_SAVENAME_H

#include <gba_base.h>

// Bytes save_name writes at most: a 99-byte .gb name grows by one
#define SAVE_NAME_MAX 101

// Writes the save file name for game into out (SAVE_NAME_MAX bytes): the
// name with its extension's letters replaced, by sav for a GBA game and by
// esv for an emulated one. is_emu is Check_file_type's value: 0 GBA, 1 GBC,
// 2 GB (two letters, so the name grows by one), 3 NES. Only game's first 99
// bytes count, as a list entry holds no more.
void save_name(char *out, const char *game, u32 is_emu);

#endif
