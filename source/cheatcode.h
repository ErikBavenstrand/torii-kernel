// Cheat codes for GBA games: decoders for the real devices' formats (GameShark
// v1/v2 and Action Replay v1/v2, Action Replay v3 and GameShark SP, CodeBreaker,
// VBA's raw addr:value), Torii's model of what they decode to, and the pass
// that turns what v0.7 runs into byte writes and ROM patches. Free of hardware
// access and allocation, so tests/host runs it on the host; the caller passes
// every buffer.
//
// The decoders (source/cheatcode.c) are ported from mGBA and keep its MPL-2.0
// licence; this header and source/cheatlower.c are Torii's.
#ifndef TORII_CHEATCODE_H
#define TORII_CHEATCODE_H

#include <gba_base.h>

// A code's format. The GameShark ones keep mGBA's numbers (GBA_GS_*).
enum cheat_format {
	CHEAT_FMT_AUTO = 0,       // not known yet: each line is detected, as mGBA does
	CHEAT_FMT_GSAV1 = 1,      // GameShark v1/v2, Action Replay v1/v2, encrypted
	CHEAT_FMT_GSAV1_RAW = 2,  // the same, unencrypted
	CHEAT_FMT_PARV3 = 3,      // Action Replay v3, GameShark SP, encrypted
	CHEAT_FMT_PARV3_RAW = 4,  // the same, unencrypted
	CHEAT_FMT_CODEBREAKER = 5,  // plain, or encrypted after a type-9 key line
	CHEAT_FMT_VBA = 6,        // AAAAAAAA:VV, :VVVV or :VVVVVVVV
};

// What one file's cheats share, carried from each cheat to the next as mGBA's
// copyProperties does: the GameShark version and seeds (a DEADFACE line
// reseeds them), and CodeBreaker's key from a type-9 line.
struct cheat_decoder {
	u8 forced;      // enum cheat_format: CHEAT_FMT_AUTO, or the only format accepted
	u8 gs_version;  // CHEAT_FMT_GSAV1..PARV3_RAW once detected or forced, else AUTO
	u8 last;        // the format of the last decoded cheat's first code line, else AUTO
	u8 last_alone;  // the last cheat was read alone: without the key, or its format detected again
	u8 cb_table[0x30];
	u16 cb_key2;    // the key line's second half
	u32 gs_seeds[4];
	u32 cb_master;  // the key line's first half; 0 while CodeBreaker codes are plain
	u32 cb_seeds[4];
	u32 cb_rng;
};

// Starts a file: every format detected, no key, the default seeds.
void cheat_decoder_init(struct cheat_decoder *d);

// Starts a file whose codes are all of one format (CHEAT_FMT_AUTO detects).
// A forced GameShark format skips detection; lines of other formats are
// unreadable.
void cheat_decoder_force(struct cheat_decoder *d, u8 format);

enum cheat_op_type {
	// width bytes of value at address, count times; after each write address
	// grows by addr_step and value by value_step (a fill or a slide). A plain
	// write has count 1; count 0 writes nothing. IO writes are writes too.
	CHEAT_OP_WRITE,
	// width bytes of value at (the u32 at address) + addr_step
	CHEAT_OP_WRITE_PTR,
	// the width bytes at address become themselves plus, or or and, value
	CHEAT_OP_ADD,
	CHEAT_OP_OR,
	CHEAT_OP_AND,
	// Conditionals read width bytes at address (zero-extended, as mGBA does),
	// compare them with value, and cover the count ops after them: when false
	// those are skipped. When else_count is set, the else_count ops after
	// those run only when false (Action Replay v3's ELSE). Ops are counted as
	// laid out, ROM patches included (they run at launch whatever the
	// condition, as in mGBA).
	CHEAT_OP_IF_EQ,
	CHEAT_OP_IF_NE,
	CHEAT_OP_IF_LT,   // signed, as s32
	CHEAT_OP_IF_GT,   // signed, as s32
	CHEAT_OP_IF_ULT,  // unsigned
	CHEAT_OP_IF_UGT,  // unsigned
	CHEAT_OP_IF_AND,  // true when read & value is not 0
	CHEAT_OP_IF_NAND, // true when read & value is 0: CodeBreaker's key test on KEYINPUT
	CHEAT_OP_IF_BUTTON,  // true while the device's own button is held (no address)
	CHEAT_OP_IF_NEVER,   // always false (Action Replay v3's width-8 conditionals)
	// width bytes (1, 2 or 4) of value written once into the ROM at address
	// (0x08000000-based), at launch
	CHEAT_OP_ROM_PATCH,
	// Action Replay v3's slowdown; value holds the code's low 24 bits. Kept so
	// a cheat that uses it reads; nothing runs it yet.
	CHEAT_OP_SLOWDOWN,
};

struct cheat_op {
	u32 address;
	u32 value;       // masked to width
	u32 count;       // writes: how many; conditionals: ops covered when true
	s32 addr_step;   // writes: the address step; WRITE_PTR: the pointer's offset
	s32 value_step;  // writes: the value step
	u16 else_count;  // conditionals: the ELSE block's ops
	u8 type;         // enum cheat_op_type
	u8 width;        // 1, 2 or 4 (0 for IF_BUTTON, IF_NEVER and SLOWDOWN)
};

enum cheat_status {
	CHEAT_OK,           // ops hold the cheat
	CHEAT_MASTER_ONLY,  // only master lines (hook, game ID, key, seed, marker): nothing to run
	CHEAT_PLACEHOLDER,  // digits stand-ins such as 00?? or 000X, for the user to fill
	CHEAT_UNREADABLE,   // malformed, or a code type no decoder knows
	CHEAT_TOO_BIG,      // more than ops_max ops; *n_ops says how many it needs
};

// Decodes one cheat's code as libretro stores it: lines joined by '+' or
// whitespace, a CodeBreaker line as "XXXXXXXX YYYY", "XXXXXXXX+YYYY" or
// "XXXXXXXXYYYY", a GameShark one as 16 digits in two halves or one. Reads
// len bytes of code, which need not end in a NUL, without its quotes.
// Returns an enum cheat_status; *n_ops is the ops decoded. Master lines (hook,
// game ID, CodeBreaker key, DEADFACE seed, 001DC0DE marker) are dropped: Torii
// hooks the IRQ itself. A key or seed carries to the file's later cheats
// unless the cheat is a placeholder or doesn't split into code lines.
// libretro's files mix plain and encrypted codes: a cheat with no key or seed
// line of its own whose ops fall outside the GBA's memory (cheat_op_address_ok)
// is read again without the CodeBreaker key, or with its GameShark format
// detected again, and that reading is kept when it fits (d->last_alone says
// so); the file's own state carries on either way.
u8 cheat_decode(struct cheat_decoder *d, const char *code, u32 len, struct cheat_op *ops, u32 ops_max, u32 *n_ops);

// The format one typed line looks like (CHEAT_FMT_AUTO for none), digit
// stand-ins counted as digits. A 16-digit line gets the likeliest of the four
// GameShark formats by mGBA's plausibility score, which *score (if not NULL)
// receives: 0x100 or more is certain, below 0 unlikely. Other formats score
// 0x100: their shape alone tells them apart.
u8 cheat_detect(const char *line, u32 len, s32 *score);

// One byte the cheat handler writes again and again
struct cheat_write {
	u32 address;
	u8 value;
};

// Written once into the ROM at launch: the halfword at offset (from
// 0x08000000, even) becomes (old & ~mask) | (value & mask)
struct cheat_patch {
	u32 offset;
	u16 value;
	u16 mask;
};

enum cheat_lower_status {
	CHEAT_LOWER_OK,
	CHEAT_LOWER_NEEDS_INTERPRETER,  // conditional, button, pointer, add/or/and or slowdown
	CHEAT_LOWER_TOO_MANY_WRITES,    // more than writes_max writes
	CHEAT_LOWER_TOO_MANY_PATCHES,   // more than patches_max patches
	CHEAT_LOWER_BAD_ADDRESS,        // an op outside the GBA's memory: likely a misread code
};

// Turns one cheat's ops into byte writes (16- and 32-bit ones split little-
// endian, fills expanded) and ROM patches, appended after the *n_writes and
// *n_patches entries already there, so several cheats can share the tables.
// Writes to the ROM (0x08000000-0x09FFFFFF) become patches; other targets keep
// their addresses. On any status but OK, nothing is appended. An address is
// good in EWRAM or IWRAM (mirrors too, as the hardware has them), IO, palette,
// VRAM, OAM, the ROM's first 32 MB mapping or SRAM; other mirrors and regions
// are bad, which catches most codes decoded with the wrong key or format.
// Addresses are checked before anything else.
u8 cheat_lower(const struct cheat_op *ops, u32 n_ops, struct cheat_write *writes, u32 writes_max, u32 *n_writes,
	       struct cheat_patch *patches, u32 patches_max, u32 *n_patches);

// Whether every address op touches is good, as cheat_lower checks it: a
// pointer write's pointer, a fill's whole range, a ROM patch in the ROM.
int cheat_op_address_ok(const struct cheat_op *op);

#endif
