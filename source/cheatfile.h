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
// Torii adds two keys of its own, which other readers ignore and
// tools/cheats.py keeps: "cheatN_format" (cheatfile_format_names) decodes
// that cheat alone in one format, with cheat_decoder_force, as the code
// keypad's override saved it; "cheatN_user = true" marks a code typed on the
// cart, which may be edited and deleted there. The keypad's edits
// (cheatfile_add, _set_code, _remove) change the text in place, through a
// struct cheatfile_room (the card side's text is in PSRAM, which takes no
// byte stores), and read it again.
//
// Every cheat is decoded with one decoder in file order, the picked ones and
// the others alike, as a type-9 CodeBreaker key or a GameShark seed carries
// from a cheat to the next (cheatcode.h). Hardware-free, as tests/host runs it;
// the caller passes every buffer. The card side is cheatstore.h.
#ifndef TORII_CHEATFILE_H
#define TORII_CHEATFILE_H

#include <gba_base.h>

#include "cheatcode.h"

#define CHEATFILE_TEXT_MAX 0x30000  // the largest text read (192 KB; exports reach 90 KB); a longer file is refused
#define CHEATFILE_MAX 1024         // cheats kept, numbered 0 to CHEATFILE_MAX - 1
// The interpreter's budget for all picked cheats together (cheatcode.h):
// its table's bytes (the end word left out) and load (its call left out)
#define CHEATFILE_BYTES_MAX (CHEAT_TABLE_MAX - 4)
#define CHEATFILE_CYCLES_MAX (CHEAT_LOAD_MAX - CHEAT_LOAD(CHEAT_COST_CALL))
#define CHEATFILE_PROG_MAX (CHEATFILE_BYTES_MAX / 4)  // a cheat's table words
#define CHEATFILE_OPS_MAX 256      // a cheat's decoded ops
#define CHEATFILE_PATCHES_MAX 256  // a cheat's ROM patches
// The picked cheats' ROM patches together: what a launch keeps for the game
// (cheatstore.h's CHEATSTORE_ROM_PATCHES), so any one legal cheat fits
#define CHEATFILE_PICKED_PATCHES_MAX CHEATFILE_PATCHES_MAX
#define CHEATFILE_NONE 0xFFFFFFFFu  // no such line (or cheat)
#define CHEATFILE_VALUE_MAX 0xFFFF  // a longer value isn't read: its line counts for nothing

// What a cheat is, once decoded
enum cheatfile_status {
	CHEATFILE_NEW,          // not decoded yet
	CHEATFILE_OK,           // its ops run in game, its ROM patches at launch
	CHEATFILE_MASTER,       // master codes alone (hook, game ID, key): hidden, nothing to run
	CHEATFILE_BIG,          // alone past the interpreter's budget, or too many ROM patches
	CHEATFILE_UNREADABLE,   // malformed, a code type no decoder knows, or an address off the GBA's map
	CHEATFILE_PLACEHOLDER,  // digits for the user to fill in (00??)
	CHEATFILE_CANT_RUN,     // an op Torii can't run as mGBA does (cheatcode.h's CHEAT_LOWER_CANT_RUN)
	CHEATFILE_NOTHING,      // reads, but changes nothing (cheat_has_effect): only conditions, say
	CHEATFILE_SAME,         // runs what an earlier cheat of the file runs, its table part and patches alike
};

// Why A doesn't pick a cheat now (cheatfile_why)
enum cheatfile_why {
	CHEATFILE_PICKABLE,
	CHEATFILE_WHY_FULL,  // it doesn't fit the budget beside the picked cheats, or alone
	CHEATFILE_WHY_UNREADABLE,
	CHEATFILE_WHY_EDIT,  // a placeholder
	CHEATFILE_WHY_CANT_RUN,
	CHEATFILE_WHY_PATCHES,  // its ROM patches don't fit beside the picked cheats' (CHEATFILE_PICKED_PATCHES_MAX)
	CHEATFILE_WHY_ROOM,     // its enable line would take the text past CHEATFILE_TEXT_MAX
	CHEATFILE_WHY_NOTHING,  // it changes nothing
	CHEATFILE_WHY_SAME,     // an earlier cheat does the same
	CHEATFILE_WHY_MASTER,   // a master code: its key or seed sets up the codes after it
	CHEATFILE_WHYS
};

// One cheat. Spans are offsets into the text, of a line's value without its
// quotes.
struct cheatfile_cheat {
	u32 desc;            // CHEATFILE_NONE: no desc line
	u32 code;
	u32 enable;          // CHEATFILE_NONE: no enable line
	u16 desc_len, code_len;
	u8 enable_len;
	u8 status;           // enum cheatfile_status
	u16 number;          // its N
	u16 bytes;           // its share of the interpreter's table once decoded (OK only)
	u16 cost;            // its cycles a frame, at most (OK only)
	u16 patches;         // ROM patches once decoded (OK only)
	u32 hash;            // its table part's and patches' hash once decoded (OK only; else 0)
	u8 picked : 1;       // now
	u8 was : 1;          // as the text has it (what writing back changes from)
	u8 saved : 1;        // as the card has it: read, or written since (cheatfile_changed)
	u8 own : 1;          // cheatN_user = true: typed on the cart, so it may be edited and deleted
	u8 format : 3;       // cheatN_format's enum cheat_format, CHEAT_FMT_AUTO without one
	u8 renamed : 1;      // cheatN_renamed = true: named on the cart, a name `just cheats` keeps
	u8 detected;         // once decoded: the format its first code line read in (format, if set), else AUTO
};

// Where cheatfile_text has got to, so each call goes on from the last
struct cheatfile_out {
	u32 pos;    // the output's bytes given so far
	u32 t;      // the text's bytes passed so far
	u32 edit;   // the text offset of the next enable value to change, past the text if none
	u32 cheat;  // the cheat whose value is being given, else CHEATFILE_NONE
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
	u32 cut;      // the text was over CHEATFILE_TEXT_MAX and is read cut: never write it back
	u32 bytes;    // the picked OK cheats' table bytes
	u32 cost;     // and cycles
	u32 patches;  // and ROM patches
	// A window's: the same for the picks in the file's other windows, which
	// the budget counts too (cheatstore.h sets them after a parse)
	u32 more_bytes, more_cost, more_patches;
	u32 out_len;  // cheatfile_text's length, kept as picks change
	u16 n_added;  // the picked cheats that get an enable line at the end
	u32 count_at;  // the "cheats = N" line's value, CHEATFILE_NONE if none
	u16 count_len;
	u16 next;     // the number past the highest cheatN_ key's (a new cheat's), at most base + max
	u16 max;      // the table's entries
	u16 base;     // the first number the table takes: cheat N is entry N - base (0 but in a window)
	u8 part;      // a window of a larger file: its count line (in the first) is left alone
	u8 edited;    // an edit changed the text: write it back though no pick changed
	struct cheatfile_out out;
};

// Reads the cheats of len bytes of text (not NUL-terminated) into cheats
// (max entries, max at most CHEATFILE_MAX), every one CHEATFILE_NEW. text
// must stay as it is while f is used. Past CHEATFILE_TEXT_MAX bytes, only
// those are read, and f->cut says so.
void cheatfile_parse(struct cheatfile *f, const char *text, u32 len, struct cheatfile_cheat *cheats, u32 max);
// The same for a window of a larger file (cheatstore.h): numbers base to
// base + max - 1 go in the table, any other is dropped; part says it is one
// of several, so its count line is never changed.
void cheatfile_parse_part(struct cheatfile *f, const char *text, u32 len, struct cheatfile_cheat *cheats, u32 max,
			  u32 base, int part);
// What a line (len bytes, its end left out) of a cheat file is: 1 if its key
// is a cheat's ("cheatN_..."), with N in *number and *on set if it is an
// enable line whose value is true; else 0. For a reader that sees the text a
// line at a time.
int cheatfile_line_key(const char *line, u32 len, u32 *number, int *on);

// The ROM the file was made for: 1 with its CRC32 in *crc if a line of the
// len bytes of text is "# for crc32 XXXXXXXX" (just cheats writes it first;
// the first such line counts), else 0: a file written by hand is for any dump.
int cheatfile_rom_crc(const char *text, u32 len, u32 *crc);

// 1 if f holds no cheat and nothing else worth keeping: blank lines, the
// count line and "# for crc32" lines only (the cheat screen deletes such a
// file once its last code is deleted)
int cheatfile_empty(const struct cheatfile *f);

// The decoder and the buffers one cheat decodes into. After cheatfile_next,
// prog and patches hold that cheat's part of the table and its patches.
#define CHEATFILE_SEEN_SLOTS (2 * CHEATFILE_MAX)  // a power of 2

struct cheatfile_work {
	struct cheat_decoder dec;
	u32 next;  // the next cheat to decode
	u32 n_prog, n_patches;  // prog's words, patches
	u32 cost;
	// The hashes of the OK cheats' tables and patches decoded so far, open
	// addressed (0 empty): a later cheat with one of them is CHEATFILE_SAME
	u32 seen[CHEATFILE_SEEN_SLOTS];
	struct cheat_op ops[CHEATFILE_OPS_MAX];
	u32 prog[CHEATFILE_PROG_MAX];
	struct cheat_patch patches[CHEATFILE_PATCHES_MAX];
};

// Starts decoding f's cheats from the first
void cheatfile_begin(struct cheatfile_work *w);
// Decodes the next cheat, sets its status, bytes, cost and patches, and
// returns its index; -1 once every cheat is decoded. Its table part and
// patches stay in w until the next call (none unless it is OK). f->bytes,
// f->cost and f->patches follow.
s32 cheatfile_next(struct cheatfile *f, struct cheatfile_work *w);

// Decodes one code (len bytes) with dec, as cheatfile_next decodes a
// cheat's: its enum cheatfile_status, with its ops (*ops of them, as
// cheat_decode counts), table part and patches in w, and cheat_lower's
// status in *lowered (CHEATFILE_NOT_LOWERED if it didn't get that far).
// The code keypad's check of a typed code.
#define CHEATFILE_NOT_LOWERED 0xFF
u8 cheatfile_decode(struct cheatfile_work *w, struct cheat_decoder *dec, const char *code, u32 len, u32 *ops,
		    u8 *lowered);

// The cheats the in-game list and the game popup's count take: all but
// master codes. The cheat screen lists every cheat, one that can't be picked
// greyed with cheatfile_why's reason.
static inline int cheatfile_shown(const struct cheatfile_cheat *c)
{
	return c->status != CHEATFILE_MASTER;
}

// Sets each cheat's pick from on (a larger file's picks, cheatstore.h), as
// saved: before the cheats are decoded (the budget counts them as decoded)
void cheatfile_picks_from(struct cheatfile *f, int (*on)(void *ctx, u32 number), void *ctx);
// Why A wouldn't pick cheat i now: CHEATFILE_PICKABLE if it would. A pick
// must fit the interpreter's budget, the ROM patches a launch keeps, and the
// cap on the text (the enable line it changes or adds counted), so a pick
// taken is one a launch runs whole and the card can be written with. A
// picked cheat can always be dropped.
u32 cheatfile_why(const struct cheatfile *f, u32 i);
// A on cheat i: drops it if picked, else picks it if it may be. 1 if its
// pick changed.
int cheatfile_toggle(struct cheatfile *f, u32 i);
// 1 if a pick differs from the card's (as read, or as last written), or an
// edit changed the text: the file needs writing
int cheatfile_changed(const struct cheatfile *f);
// The picked cheats' count
u32 cheatfile_picks(const struct cheatfile *f);

// The file with the picks written in, as a safefile_fill: up to max bytes
// of it from byte pos into buf, 0 at its end. ctx is the cheatfile. The
// text itself stays as read (or edited): the picks are written in each time.
u32 cheatfile_text(void *ctx, u32 pos, char *buf, u32 max);
// Its length (kept as picks change: no pass over the cheats)
u32 cheatfile_text_len(const struct cheatfile *f);
// After cheatfile_text was written to the card: the card has the picks
void cheatfile_written(struct cheatfile *f);

// --- Edits (the code keypad's) ------------------------------------------------------
//
// Each changes f's text in place, in room (whose base is f->text), then
// reads it again: every cheat CHEATFILE_NEW until decoded again from the first
// (cheatfile_begin), the picks kept as they were (not yet written), and
// f->edited set. The count line follows the cheats' numbers ("cheats = N",
// one added at the top if the file had none). A desc loses its control
// characters and has '"' written as '\''. New lines end as the file's do.

enum cheatfile_edit {
	CHEATFILE_EDIT_OK,
	CHEATFILE_EDIT_FULL,  // a new cheat's number would be CHEATFILE_MAX (or the table's end)
	CHEATFILE_EDIT_LONG,  // the text, picks written in, would pass CHEATFILE_TEXT_MAX or the room
	CHEATFILE_EDIT_BAD,   // no such cheat, or a code with a quote or a control character
	CHEATFILE_EDIT_FAILED,  // the room's writes failed (cheatstore.h's PSRAM): the text can't be trusted
};

// Where the text lives, for the edits: cap bytes from base, read as plain
// memory and written only through put (n bytes from src, which may be
// anywhere readable but not in [dst, dst + n), to dst at any alignment) and
// move (memmove's, within the room). tests/host's are memcpy and memmove;
// the card side's write PSRAM (cheatstore.c).
struct cheatfile_room {
	char *base;
	u32 cap;
	void (*put)(char *dst, const char *src, u32 n);
	void (*move)(char *dst, const char *src, u32 n);
};

// The cheatN_format values, by enum cheat_format (CHEAT_FMT_AUTO: none)
extern const char *const cheatfile_format_names[CHEAT_FMT_VBA + 1];

// Appends a cheat of the user's (cheatN_user = true) after the others:
// desc, code ("+" between lines), cheatN_enable = false (picked once it
// decodes, by the caller), and cheatN_format unless format is
// CHEAT_FMT_AUTO. *index gets its index, the last.
u32 cheatfile_add(struct cheatfile *f, const struct cheatfile_room *room, const char *desc, const char *code, u8 format,
		  u32 *index);
// Whether cheatfile_add would take a new cheat named desc now, with a short
// code (CHEATFILE_SHORT_CODE), in a room of cap bytes: CHEATFILE_EDIT_OK, or
// why not (CHEATFILE_EDIT_FULL: no number is left; CHEATFILE_EDIT_LONG: the
// file is at its cap), so the cheat screen refuses before anything is
// typed. A longer code may still be refused by the add itself.
#define CHEATFILE_SHORT_CODE "00000000 0000"
u32 cheatfile_can_add(const struct cheatfile *f, u32 cap, const char *desc);
// Replaces cheat i's code and format line (none for CHEAT_FMT_AUTO), and
// its desc unless desc is NULL; its other lines stay.
u32 cheatfile_set_code(struct cheatfile *f, const struct cheatfile_room *room, u32 i, const char *desc,
		       const char *code, u8 format);
// Names cheat i desc: its desc line replaced (or added), and, for a cheat
// not the user's own, a cheatN_renamed line so `just cheats` keeps the name
// when it writes the file again.
u32 cheatfile_rename(struct cheatfile *f, const struct cheatfile_room *room, u32 i, const char *desc);
// Removes every cheatN_ line of cheat i (and a blank line before them),
// and numbers the cheats after it one lower, as RetroArch reads a file's
// cheats from 0 to its count.
u32 cheatfile_remove(struct cheatfile *f, const struct cheatfile_room *room, u32 i);
// Removes every cheat whose table entry's bit is set in drop (a bit per
// entry, entry i at bit i & 7 of byte i >> 3), in one pass; the cheats
// after each are numbered lower to close the gaps, their picks kept, and
// the text read again (to decode again). How many went.
u32 cheatfile_remove_many(struct cheatfile *f, const struct cheatfile_room *room, const u8 *drop);

#endif
