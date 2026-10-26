// A game's cheat file on the card (v0.7): /torii/cheats/<rom path without
// its extension>.cht, a safe file (safefile.h: NAME and NAME.bak) in
// libretro's format (cheatfile.h). tools/cheats.py writes it; a libretro file
// dropped there by hand, named after the ROM, works too. It moves, is
// renamed and is deleted with its game (actions.c).
//
// The file is read whole into pReadCache (READCACHE_CHEATS, ez_define.h),
// with its cheats and the decoder's work: struct cheatstore below, which
// lasts until another owner borrows that part of pReadCache.
#ifndef TORII_CHEATSTORE_H
#define TORII_CHEATSTORE_H

#include <gba_base.h>

#include "cheatfile.h"
#include "safefile.h"

#define CHEATS_DIR "/torii/cheats"
// The largest copy read: the text and its CRC line
#define CHEATSTORE_FILE_MAX (CHEATFILE_TEXT_MAX + SAFEFILE_CRC_LINE_LEN + 2)

struct cheatstore {
	u32 text[(CHEATSTORE_FILE_MAX + 3) / 4];  // word-aligned for f_read
	struct cheatfile file;
	struct cheatfile_work work;
	struct cheatfile_cheat cheats[CHEATFILE_MAX];
	char path[SAFEFILE_PATH_MAX];  // the file's, as cheatstore_read read it
	u16 rows[CHEATFILE_MAX];       // the cheat screen's: the cheat each row shows
};
struct cheatstore *cheatstore(void);

// The cheat file's path for the ROM at rom_path (from the root, with its
// extension) into out (size bytes); its length, or -1 if it doesn't fit
// SAFEFILE_PATH_MAX with ".bak".
int cheatstore_path(char *out, u32 size, const char *rom_path);
// Reads and parses the game's cheat file into cheatstore(), nothing decoded
// yet: 1 if it has one with a cheat in it. Call only where FatFs calls may
// run.
int cheatstore_read(const char *rom_path);
// Decodes the read file's cheats from cheatstore()->work.next on, until
// budget cycles have passed (0: all of them). 1 once all are decoded.
int cheatstore_decode(u32 budget);
// How many cheats the read file's cheat screen lists (all but master codes,
// which only decoding tells apart): decoding from cheatstore()->work.next
// on for up to budget cycles (0: as long as it takes), unless this session
// has counted the file already (the last file counted, by its path and
// cheat count). -1 if the budget ran out first.
s32 cheatstore_listed(u32 budget);
// Writes the picks back into the read file, power-cut safe, if one changed.
// 1 if it is as the picks say.
int cheatstore_write(void);
// Deletes the game's cheat file (both copies); 1 if none is left
int cheatstore_delete(const char *rom_path);

// A launch of the GBA game at rom_path: its picked cheats' byte writes into
// pCHEAT (gl_cheat_count), the cheats in file order, as many as fit
// CHEAT_MAX (a cheat that doesn't fit is left out whole); gl_cheat_patches
// gets their ROM patches' count. With psram_bytes, the game is in PSRAM
// (that many bytes) and those ROM patches are written into it. A game
// without a cheat file, or with none picked, gets none.
void cheatstore_launch(const char *rom_path, u32 psram_bytes);
extern u32 gl_cheat_patches;

#endif
