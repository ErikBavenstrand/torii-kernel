// The cheat table the in-game handler applies: each entry's VAL's low byte is
// stored at address in every interrupt while cheats are on (gba_rts_patch.s).
// A launch fills it from the game's cheat file (cheatstore.h).
#ifndef TORII_CHEAT_H
#define TORII_CHEAT_H

#include <gba_base.h>

typedef struct ST_entry_ {
	u32 address;
	u32 VAL;
} ST_entry;

// The cheat handler and its table share the patch area appended to the ROM:
// the handler takes about 0xD04 bytes, and each entry 8.
#define CHEAT_PATCH_LENGTH 0x2000
#define CHEAT_MAX 600
extern ST_entry pCHEAT[CHEAT_MAX];
extern u32 gl_cheat_count;

#endif
