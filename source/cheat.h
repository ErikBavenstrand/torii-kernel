// The cheat table the in-game interpreter runs on each VBlank while cheats
// are on (source/cheatrun.s; its layout is in cheatcode.h). A launch fills it
// from the game's picked cheats (cheatstore.h).
#ifndef TORII_CHEAT_H
#define TORII_CHEAT_H

#include <gba_base.h>

#include "cheatcode.h"

// The patch area SetTrimSize keeps at the ROM's end when cheats run: the
// in-game menu's stub, the interpreter, its off mask (8 bytes), the table
// (at most CHEAT_TABLE_MAX, 0x1400) and, with logging on, the menu's DEBUG
// code (test_cheat_cap logs what is left). A NOR game's patch must fit one
// 128 KB sector with it (SetTrimSize).
#define CHEAT_PATCH_LENGTH 0x3000
extern u32 gl_cheat_table[CHEAT_TABLE_MAX / 4];
extern u32 gl_cheat_bytes;  // the table's, its end word included; 0 for none
extern u32 gl_cheat_count;  // the cheats in it: the picks', then the extras
// The picked cheats in it (the first gl_cheat_picks) and their bytes, its
// end word left out. The rest are extras (v0.8): cheats not picked, off, for
// the in-game list to turn on.
extern u32 gl_cheat_picks;
extern u32 gl_cheat_pick_bytes;
// The interpreter's off mask: bit n for the table's cheat n, set for the
// extras (cheatrun.s)
extern u32 gl_cheat_off[2];

#endif
