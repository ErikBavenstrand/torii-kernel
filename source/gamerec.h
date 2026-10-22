// A game's own record, kept in its folder of saves: its save type, its boot
// mode and the slot it plays in. Plain text (/torii/saves/<rom path>/game.ini,
// see gamesave.h), so it reads and edits on a PC. Hardware-free, so it runs on
// the host (tests/host/test_gamerec.c).
//
//   save_type = 3
//   boot_mode = 1
//   slot = Main
//
// A missing file, line or key means the default; an unknown key is ignored (and
// dropped by the next write); a bad value is the default.
#ifndef TORII_GAMEREC_H
#define TORII_GAMEREC_H

#include <gba_base.h>

#include "saveslot.h"

// The file's name in the game's folder. A slot called "game" is game.sav, so
// no slot can take this name.
#define GAMEREC_FILE "game.ini"
// A record never grows past this (a write is one sector)
#define GAMEREC_BYTES 256

struct gamerec {
	u8 save_type;  // 0 AUTO, 1-5 a type (actions.h's ACTION_SAVE_*)
	u8 boot_mode;  // ACTION_BOOT_* (actions.h)
	char slot[SAVESLOT_NAME_BYTES];  // the current slot, clean; never empty
};

// The defaults: save type 0, boot mode 0, slot Main
void gamerec_init(struct gamerec *r);
// Reads a record of len bytes over r (call gamerec_init first). Only what the
// text says changes: a record is always valid afterwards.
void gamerec_parse(struct gamerec *r, const char *text, u32 len);
// Writes r as text into out (size bytes, GAMEREC_BYTES is enough); returns
// its length, or 0 if it doesn't fit.
u32 gamerec_format(char *out, u32 size, const struct gamerec *r);

#endif
