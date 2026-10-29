// A larger cheat file's index (cheatstore.h): where its windows are, and
// what the kernel needs to show or launch one without reading the rest. It
// lives beside the cheat file, /torii/cheats/<rom path less its
// extension>.cdx, a safe file (safefile.h) of text lines:
//
//	torii-cheat-index 1
//	file SIZE                        the cheat file's bytes (its main copy's)
//	w AT LEN FIRST COUNT CRC STATE   a window: its bytes, first cheat's number,
//	                                 cheats, the CRC32 of its bytes, and the
//	                                 decoder at its first cheat (hex)
//	p NUMBER HASH BYTES COST PATCHES a picked cheat: its number, its table
//	                                 part's hash (cheatfile_cheat.hash) and
//	                                 what it takes of the budget
//
// Every number is hex. tools/cheats.py writes one with each larger file it
// writes (tools/cheat_patches.c --index), and the kernel builds one when a
// file has none that fits. An index fits only while the file has its size and
// each window read has its CRC; else the file is read whole again. A pick is
// matched to its cheat by number, and by hash once the file has changed
// (`just cheats` writes it anew), so a pick made on the cart outlives that.
//
// The cut: a window ends before the line that starts a cheat once it holds
// CHEATINDEX_CHEATS cheats, its first and that cheat's numbers are as far
// apart, or it holds CHEATINDEX_BYTES bytes. A cheat's lines must come
// together, its number past the cheat's before, but for an enable line of
// an earlier cheat of the same window (the kernel adds a pick's at its
// window's end). Hardware-free: tests/host runs it.
#ifndef TORII_CHEATINDEX_H
#define TORII_CHEATINDEX_H

#include <gba_base.h>

#include "cheatcode.h"

#define CHEATINDEX_MAGIC "torii-cheat-index 1"
#define CHEATINDEX_EXT ".cdx"
#define CHEATINDEX_CHEATS 250
#define CHEATINDEX_BYTES 0xA000
#define CHEATINDEX_NUMBER_MAX 0xFC00  // a window's table (CHEATFILE_MAX) past its first stays under 0x10000
#define CHEATINDEX_LINE_MAX 320       // the longest line, its end included

struct cheatindex_window {
	u32 at, len, crc;
	u16 first, count;
	struct cheat_decoder dec;
};

// The cut, fed a line at a time: the window being filled, and the line's
// cheat. window counts the windows cut so far (0: the first is being filled).
struct cheatindex_cut {
	u32 window;
	u32 at;      // where the window being filled starts
	u32 number;  // the cheat whose lines come now (0xFFFFFFFF before the first)
	u32 first, count, picks;
	u8 any, counted;
};
enum {
	CHEATINDEX_LINE,  // the line goes in the window being filled
	CHEATINDEX_NEW,   // a new window starts at the line (the one before ends there)
	CHEATINDEX_BAD,   // the file can't be cut: a cheat's lines apart, or numbers past the limit
};
void cheatindex_cut_begin(struct cheatindex_cut *c);
// A line of the file at offset line_at, its first len bytes in line (a
// line's key fits in 48), its end left out
int cheatindex_cut_line(struct cheatindex_cut *c, const char *line, u32 len, u32 line_at);

// The index's lines, into out (CHEATINDEX_LINE_MAX bytes), '\n' ended: their
// lengths
u32 cheatindex_head(char *out, u32 file_size);
u32 cheatindex_window_line(char *out, const struct cheatindex_window *w);
u32 cheatindex_pick_line(char *out, u32 number, u32 hash, u32 bytes, u32 cost, u32 patches);
// A line of the index (its end left out): what it is, its fields read
enum {
	CHEATINDEX_OTHER,
	CHEATINDEX_MAGIC_LINE,
	CHEATINDEX_FILE,
	CHEATINDEX_WINDOW,
	CHEATINDEX_PICK,
};
struct cheatindex_line {
	u32 size, number, hash, bytes, cost, patches;
	struct cheatindex_window w;
};
int cheatindex_read_line(const char *line, u32 len, struct cheatindex_line *out);

#endif
