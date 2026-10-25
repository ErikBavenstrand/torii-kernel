// A game's cheat file, /torii/cheats/<rom path without extension>.cht, in
// libretro's format (v0.7): a count line, then per cheat
//
//	cheatN_desc = "Infinite lives"
//	cheatN_code = "82001234+0063"
//	cheatN_enable = false
//
// tools/cheats.py writes them; parse_cht there reads them as this file does.
// A line is KEY = VALUE with spaces around either; a value loses one quote at
// each end (a missing one is tolerated); '#' lines are comments; unknown keys,
// CRLF and keys in any order are fine, and the count line is ignored (a file
// whose count disagrees reads its cheats all the same). Cheats are taken in
// the order of their numbers N, the order RetroArch reads them in, and a
// cheat without a code line is left out. When a key repeats, its last line
// counts.
//
// The picks are the cheats' enable lines: writing back (cheatfile_text)
// changes only the values of the enable lines whose pick changed, and adds
// "cheatN_enable = true" at the end for a picked cheat that had none; every
// other byte stays.
//
// Every cheat is decoded with one decoder in file order, the picked ones and
// the others alike, as a type-9 CodeBreaker key or a GameShark seed carries
// from a cheat to the next (cheatcode.h). Hardware-free, as tests/host runs it;
// the caller passes every buffer. The card side is cheatstore.h.
#ifndef TORII_CHEATFILE_H
#define TORII_CHEATFILE_H

#include <gba_base.h>

#include "cheatcode.h"

#define CHEATFILE_TEXT_MAX 0xE000  // the largest file read (libretro's reach 25 KB)
#define CHEATFILE_MAX 1024         // cheats kept, numbered 0 to CHEATFILE_MAX - 1
#define CHEATFILE_WRITES_MAX 600   // byte writes the in-game handler takes (cheat.h's CHEAT_MAX)
#define CHEATFILE_OPS_MAX 256      // a cheat's decoded ops
#define CHEATFILE_PATCHES_MAX 256  // a cheat's ROM patches
#define CHEATFILE_NONE 0xFFFF      // no such line

// What a cheat is, once decoded
enum cheatfile_status {
	CHEATFILE_NEW,          // not decoded yet
	CHEATFILE_OK,           // its writes and ROM patches run
	CHEATFILE_MASTER,       // master codes alone (hook, game ID, key): hidden, nothing to run
	CHEATFILE_V08,          // conditionals, buttons, pointers, add/or/and: v0.8's interpreter
	CHEATFILE_BIG,          // more writes (or ROM patches) than a game takes
	CHEATFILE_UNREADABLE,   // malformed, a code type no decoder knows, or an address off the GBA's map
	CHEATFILE_PLACEHOLDER,  // digits for the user to fill in (00??)
};

// Why A doesn't pick a cheat now (cheatfile_why)
enum cheatfile_why {
	CHEATFILE_PICKABLE,
	CHEATFILE_WHY_V08,
	CHEATFILE_WHY_FULL,  // its writes don't fit beside the picked cheats', or alone
	CHEATFILE_WHY_UNREADABLE,
	CHEATFILE_WHY_EDIT,  // a placeholder
	CHEATFILE_WHYS
};

// One cheat. Spans are offsets into the text, of a line's value without its
// quotes.
struct cheatfile_cheat {
	u16 desc, desc_len;  // CHEATFILE_NONE: no desc line
	u16 code, code_len;
	u16 enable;          // CHEATFILE_NONE: no enable line
	u8 enable_len;
	u8 status;           // enum cheatfile_status
	u16 number;          // its N
	u16 writes;          // byte writes once decoded (OK only)
	u16 patches;         // ROM patches once decoded (OK only)
	u8 picked;           // now
	u8 was;              // as the file has it
};

// Where cheatfile_text has got to, so each call goes on from the last
struct cheatfile_out {
	u32 pos;    // the output's bytes given so far
	u32 t;      // the text's bytes passed so far
	u32 edit;   // the text offset of the next enable value to change, past the text if none
	u16 cheat;  // the cheat whose value is being given, else CHEATFILE_NONE
	u16 added;  // the next cheat to look at for an added enable line
	u8 k, n;    // line's bytes given, and its length
	u8 tail;    // 0 the text, 1 and 3 the added lines (before the first, after), 2 the end
	char line[40];
};

struct cheatfile {
	const char *text;
	u32 len;
	struct cheatfile_cheat *cheats;
	u32 count;
	u32 dropped;  // cheats numbered past the table's end: left out
	u32 crlf;     // the file's lines end in CRLF (an added line does too)
	u32 writes;   // the picked OK cheats' byte writes
	struct cheatfile_out out;
};

// Reads the cheats of len bytes of text (not NUL-terminated) into cheats
// (max entries, max at most CHEATFILE_MAX), every one CHEATFILE_NEW. text
// must stay as it is while f is used.
void cheatfile_parse(struct cheatfile *f, const char *text, u32 len, struct cheatfile_cheat *cheats, u32 max);

// The decoder and the buffers one cheat decodes into. After cheatfile_next,
// writes and patches hold that cheat's lowered writes and patches.
struct cheatfile_work {
	struct cheat_decoder dec;
	u32 next;  // the next cheat to decode
	u32 n_writes, n_patches;
	struct cheat_op ops[CHEATFILE_OPS_MAX];
	struct cheat_write writes[CHEATFILE_WRITES_MAX];
	struct cheat_patch patches[CHEATFILE_PATCHES_MAX];
};

// Starts decoding f's cheats from the first
void cheatfile_begin(struct cheatfile_work *w);
// Decodes the next cheat, sets its status, writes and patches, and returns
// its index; -1 once every cheat is decoded. Its lowered writes and patches
// stay in w until the next call (none unless it is OK). f->writes follows.
s32 cheatfile_next(struct cheatfile *f, struct cheatfile_work *w);

// The cheats a list shows: all but master codes
static inline int cheatfile_shown(const struct cheatfile_cheat *c)
{
	return c->status != CHEATFILE_MASTER;
}

// Why A wouldn't pick cheat i now: CHEATFILE_PICKABLE if it would. A picked
// cheat can always be dropped.
u32 cheatfile_why(const struct cheatfile *f, u32 i);
// A on cheat i: drops it if picked, else picks it if it may be. 1 if its
// pick changed.
int cheatfile_toggle(struct cheatfile *f, u32 i);
// 1 if a pick differs from the file's
int cheatfile_changed(const struct cheatfile *f);
// The picked cheats' count
u32 cheatfile_picks(const struct cheatfile *f);

// The file with the picks written in, as a safefile_fill: up to max bytes
// of it from byte pos into buf, 0 at its end. ctx is the cheatfile.
u32 cheatfile_text(void *ctx, u32 pos, char *buf, u32 max);
// Its length
u32 cheatfile_text_len(const struct cheatfile *f);

#endif
