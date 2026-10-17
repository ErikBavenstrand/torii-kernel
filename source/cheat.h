// The cheat table the in-game handler applies, and the parser that fills it
// from .cht lines. Free of hardware access, so tests/host runs it on the host.
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

// Parses one cheat's value, "ADDR,VV,VV;ADDR,VV", into pCHEAT: each value
// after the first goes to the next address. Reads len bytes of line, which
// need not end in a NUL. Writes past CHEAT_MAX are dropped.
void Parse_cheat_line(const char *line, u32 len);

#endif
