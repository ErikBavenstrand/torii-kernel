// What the log on the card shows happened since the last Diagnostics run,
// for checking the checklist's answers (source/diag.c). Hardware-free, so
// tests/host runs it.
#ifndef TORII_LOGCHECK_H
#define TORII_LOGCHECK_H

#include <gba_base.h>

enum {
	SEEN_GBA = 1 << 0,  // a GBA game launched (SD or NOR)
	SEEN_MENU = 1 << 1,  // a BOOT WITH ADDON launch with RESET or SAVESTATE on
	SEEN_SAVESTATE = 1 << 2,  // a launch with SAVESTATE on
	SEEN_SLEEP = 1 << 3,  // a launch with SLEEP on
	SEEN_GB = 1 << 4,  // a GB or GBC game launched (Jagoomba Color)
	SEEN_NES = 1 << 5,  // a NES game launched (PocketNES)
	SEEN_NOR = 1 << 6,  // a game launched from NOR
	SEEN_DEBUG = 1 << 7,  // the in-game menu's DEBUG snapshot reached the log
	SEEN_EEPROM_PROBE = 1 << 8,  // an EEPROM probe report's verdict: its width works
	SEEN_EEPROM_PROBE_FAILED = 1 << 9,  // an EEPROM probe report's verdict: anything else
};

struct logcheck {
	u32 seen;  // SEEN_* flags
	u32 pending;  // the current launch's, until it reaches the game
};

// Reads one more log line (its text, without the newline). A launch counts
// once "launch: starting the game" follows it; the end of a Diagnostics run
// starts over. Start from a zeroed struct.
void logcheck_line(struct logcheck *c, const char *line);

#endif
