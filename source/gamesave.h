// Where a game's saves are on the card, and the record it keeps there (v0.5).
//
// A game's saves are its slots, the .sav files of its folder,
// /torii/saves/<rom path>/<slot>.sav (saveslot.h); every game's first is
// "Main", made at its first launch. The game's record, game.ini (gamerec.h:
// save type, boot mode, current slot), is in the same folder. The current slot
// is the one the next launch loads and its save is copied to. The slots a game
// has are its .sav files; a slot with no file yet (the current one before the
// first launch) is made by the launch.
//
// <rom path> is the ROM's path from the root with its extension, as
// saveslot.h says. Nothing here changes while a launch is under way.
#ifndef TORII_GAMESAVE_H
#define TORII_GAMESAVE_H

#include <gba_base.h>

#include "gamerec.h"

// The room a path built here needs
#define GAMESAVE_PATH_BYTES 288

// A NOR game has no folder: its saves are kept as for a ROM at this path
// (which no real ROM has: /torii isn't listed), whatever the setting.
#define GAMESAVE_NOR_DIR "/torii/nor/"
int gamesave_nor_path(char *out, u32 size, const char *name);
// Writes the path of file name in the folder dir (currentpath, or as f_getcwd
// gives it: "/" or "" for the root, perhaps with a volume prefix) into out
// (size bytes). Returns its length, or -1 if it doesn't fit.
int gamesave_rom_path_in(char *out, u32 size, const char *dir, const char *name);

// Makes every folder of path's parents (not path itself), as mkdir -p: one
// that is there already is fine. A cut leaves some made, which hold nothing
// and count as no save. 1 if the last parent is a folder now.
int gamesave_make_parents(const char *path);

// Reads the game's record into r: the defaults for a game with none.
void gamesave_record_read(const char *rom_path, struct gamerec *r);
// Writes the record, making its folder. 1 if written.
int gamesave_record_write(const char *rom_path, const struct gamerec *r);

// The current slot's save file for the game into out
// (GAMESAVE_PATH_BYTES). Returns its length, or -1 if the ROM path or slot
// can't make a path. The caller checks it against SAV_INFO_NAME_MAX.
int gamesave_current_path(char *out, u32 size, const char *rom_path, const struct gamerec *r);

#endif
