// The cheat table the in-game interpreter runs on each VBlank while cheats
// are on (source/cheatrun.s; its layout is in cheatcode.h). A launch fills it
// from the game's picked cheats (cheatstore.h).
#ifndef TORII_CHEAT_H
#define TORII_CHEAT_H

#include <gba_base.h>

#include "cheatcode.h"

// The patch area SetTrimSize keeps at the ROM's end when cheats run: the
// in-game menu's code (0xD6C bytes at v0.7), the interpreter (0x474), the
// table (at most CHEAT_TABLE_MAX, 0x1400) and, with logging on, the menu's
// DEBUG code (0x180), which leaves 0x8A0 bytes for v0.8's overlay
// (test_cheat_cap logs it). A NOR game's patch must fit one 128 KB sector
// with it (SetTrimSize).
#define CHEAT_PATCH_LENGTH 0x3000
extern u32 gl_cheat_table[CHEAT_TABLE_MAX / 4];
extern u32 gl_cheat_bytes;  // the table's, its end word included; 0 for none
extern u32 gl_cheat_count;  // the cheats in it

#endif
