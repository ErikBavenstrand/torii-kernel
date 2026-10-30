// Cheat codes for GBA games: decoders for the real devices' formats (GameShark
// v1/v2 and Action Replay v1/v2, Action Replay v3 and GameShark SP, CodeBreaker,
// VBA's raw addr:value with AnyGBA's extensions), Torii's model of what
// they decode to, and the pass
// that turns it into the in-game interpreter's table and ROM patches. Free of hardware
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
	// AAAAAAAA:VV, :VVVV or :VVVVVVVV; and AnyGBA's raw extensions, which no
	// device has: [PPPPPPPP]+O for a target through a pointer, VALUE^KEY
	// (KEY an address or a pointer) to XOR the value with what's there, and
	// TARGET|VALUE to set bits (CHEAT_OP_KEYED)
	CHEAT_FMT_VBA = 6,
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

// cheat_decoder_force for one cheat of a file whose decoder is file: when
// file reads GameShark codes of that format, its seeds carry over (a
// DEADFACE line reseeds the device for every code after it).
void cheat_decoder_force_in(struct cheat_decoder *d, const struct cheat_decoder *file, u8 format);

// After d (from cheat_decoder_force_in) read a cheat: a DEADFACE in it, seen
// as d's seeds no longer seeds_before, reseeds file's later cheats when file
// reads codes of d's format or hasn't seen any GameShark codes yet (and then
// reads that format from now on), as one device reads the whole file.
void cheat_decoder_force_out(struct cheat_decoder *file, const struct cheat_decoder *d, const u32 *seeds_before);

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
	// condition, as in mGBA). A conditional covering no ops tests nothing, as
	// in mGBA: the decoders put a step that does nothing (a write of count 0)
	// in an empty THEN.
	CHEAT_OP_IF_EQ,
	CHEAT_OP_IF_NE,
	CHEAT_OP_IF_LT,   // signed at the width, as the devices compare (mGBA zero-extends)
	CHEAT_OP_IF_GT,   // signed at the width
	CHEAT_OP_IF_ULT,  // unsigned
	CHEAT_OP_IF_UGT,  // unsigned
	CHEAT_OP_IF_AND,  // true when read & value is not 0
	CHEAT_OP_IF_NAND, // true when read & value is 0: CodeBreaker's key test on KEYINPUT
	CHEAT_OP_IF_BUTTON,  // true while the device's own button is held (no address)
	CHEAT_OP_IF_NEVER,   // always false (Action Replay v3's width-8 conditionals)
	// width bytes (1, 2 or 4) of value written once into the ROM at address
	// (0x08000000-based), at launch
	CHEAT_OP_ROM_PATCH,
	// A slowdown code (Action Replay v3's, GameShark v1's on its button);
	// value holds the code's loops. Kept so a cheat that uses it reads; it
	// does nothing, as in mGBA.
	CHEAT_OP_SLOWDOWN,
	// AnyGBA's raw extensions (the VBA format's [P]+O, ^ and |): width bytes
	// of value, XORed with the width bytes at the key when there is one
	// (CHEAT_KEYED_KEY), written at the target, or OR-ed into it
	// (CHEAT_KEYED_OR). The target is address, or (CHEAT_KEYED_TARGET_PTR)
	// the u32 at address, plus addr_step; the key is key, or
	// (CHEAT_KEYED_KEY_PTR) the u32 at key, plus key_offset. One element
	// each frame (count is 1; no fill): for save data a game keeps encrypted
	// with a key of its own and moves about (Pokémon's money and bag counts).
	CHEAT_OP_KEYED,
};

#define CHEAT_KEYED_TARGET_PTR 1u
#define CHEAT_KEYED_KEY_PTR 2u
#define CHEAT_KEYED_KEY 4u
#define CHEAT_KEYED_OR 8u

struct cheat_op {
	u32 address;
	u32 value;       // masked to width, but for an unsigned bound one past it (0x10000: GameShark's <= and >=)
	u32 count;       // writes: how many; conditionals: ops covered when true
	s32 addr_step;   // writes: the address step; WRITE_PTR and KEYED: the pointer's offset
	s32 value_step;  // writes: the value step
	u16 else_count;  // conditionals: the ELSE block's ops
	u8 type;         // enum cheat_op_type
	u8 width;        // 1, 2 or 4 (0 for IF_BUTTON, IF_NEVER and SLOWDOWN)
	u32 key;         // KEYED: the key's address, or its pointer's
	u16 key_offset;  // KEYED: the key's offset (the raw format's is at most FFFF)
	u8 flags;        // KEYED: CHEAT_KEYED_*
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
// so); the file's own state carries on either way. A cheat of more than a
// line holding a raw CodeBreaker key line other than the file's key takes it
// as the file's new key when the old key reads the cheat as nonsense and the
// new one doesn't.
u8 cheat_decode(struct cheat_decoder *d, const char *code, u32 len, struct cheat_op *ops, u32 ops_max, u32 *n_ops);

// Whether the '+' at line[i] is part of the line that starts at line (len
// bytes from there to the code's end), not between lines: a pointer's
// offset ([PPPPPPPP]+O, AnyGBA's), a target's always, a key's when its
// digits end the line. Everything that splits a code into lines asks this,
// a line at a time.
int cheat_plus_joins(const char *line, u32 len, u32 i);

// The format one typed line looks like (CHEAT_FMT_AUTO for none), digit
// stand-ins counted as digits. A 16-digit line gets the likeliest of the four
// GameShark formats by mGBA's plausibility score, which *score (if not NULL)
// receives: 0x100 or more is certain, below 0 unlikely. Other formats score
// 0x100: their shape alone tells them apart.
u8 cheat_detect(const char *line, u32 len, s32 *score);

// Written once into the ROM at launch: the halfword at offset (from
// 0x08000000, even) becomes (old & ~mask) | (value & mask)
struct cheat_patch {
	u32 offset;
	u16 value;
	u16 mask;
};

// --- The interpreter's table ------------------------------------------------
// What the in-game interpreter (source/cheatrun.s) runs each frame: per
// cheat a header word, then its ops; a 0 word ends the table. cheatrun.s
// describes the layout in full; these are its numbers.
#define CHEAT_PROG_PLAIN 0x8000u   // header: no conditionals, ops run in order
#define CHEAT_PROG_OPS_MAX 0x1FFFu // header: ops of a cheat
// header: the gate's ops at the cheat's start (cheatfile.c: a Hold or Press
// line's conditions, 1 or 2), which a run once steps past
#define CHEAT_PROG_GATE_SHIFT 13
#define CHEAT_PROG_GATE (3u << CHEAT_PROG_GATE_SHIFT)
#define CHEAT_PROG_K_EXT 3u        // op kinds 3-5: an op of that many words
#define CHEAT_PROG_K_RUN 6u        // op kinds 6-8: a run of writes (plain cheats)
#define CHEAT_PROG_COUNT_MAX 0xFFFFu  // an op's count
#define CHEAT_PROG_KEYED_SHIFT 6      // a keyed op's CHEAT_KEYED_* in its info word

enum cheat_prog_type {
	CHEAT_PROG_WRITE = 0,
	CHEAT_PROG_PTR = 1,
	CHEAT_PROG_ADD = 2,
	CHEAT_PROG_OR = 3,
	CHEAT_PROG_AND = 4,
	CHEAT_PROG_NOP = 5,
	CHEAT_PROG_KEYED = 6,  // CHEAT_OP_KEYED: its flags at info bit CHEAT_PROG_KEYED_SHIFT
	CHEAT_PROG_NEVER = 7,
	CHEAT_PROG_EQ = 8,
	CHEAT_PROG_NE = 9,
	CHEAT_PROG_LT = 10,
	CHEAT_PROG_GT = 11,
	CHEAT_PROG_ULT = 12,
	CHEAT_PROG_UGT = 13,
	CHEAT_PROG_AND_TRUE = 14,  // true when read & value is not 0
	CHEAT_PROG_NAND = 15,      // true when read & value is 0
};

// The device's own button (GameShark's and Action Replay's "button" codes,
// IF_BUTTON) is these GBA keys held together: R and SELECT. A flash cart has
// no button of its own; KEYINPUT reads 0 for a held key, so the op is
// "if (KEYINPUT & mask) == 0", as CodeBreaker's own key codes test it.
#define CHEAT_BUTTON_KEYS 0x0104u  // KEY_R | KEY_SELECT
#define CHEAT_KEYINPUT 0x04000130u
// The keys as the interpreter saw them (KEYINPUT's bits, 0 for held), which
// a cheat's Press gate tests (cheatfile.c): at its run, a halfword in IWRAM
// the BIOS's SoftReset clears (beside the in-game menu's words, ingame_abi.h),
// and at the run before, which the run puts in CheatRAN's high halfword
// (gba_rts_patch.s) for its own length. cheatrun.s writes both.
#define CHEAT_KEYS_NOW 0x03007FE2u
#define CHEAT_KEYS_BEFORE 0x03007FE6u

// The interpreter's budget. Its cycles are an upper bound, every op run
// (conditions all true), measured on the emulated cart with the game's ROM
// at the BIOS's default wait states, the slowest a game has
// (tests/kernel/test_cheatrun.c checks the model against the clock).
#define CHEAT_COST_CALL 300      // the hook's call (with the run once byte), and the table's end
#define CHEAT_COST_CHEAT 130     // a cheat's header, its off bit tested
#define CHEAT_COST_SINGLE 140    // a write of its own
#define CHEAT_COST_RUN 150       // a run's header, in a plain cheat
#define CHEAT_COST_RUN_WRITE 36  // a write in a run
#define CHEAT_COST_EXT 430       // a longer op, its first element
#define CHEAT_COST_FILL 48       // each further write of a slide (a value step)
#define CHEAT_COST_FILL_SAME 20  // each further write of a fill of one value
#define CHEAT_COST_GEN 200       // each further element of a pointer write, add, or, and
#define CHEAT_COST_KEYED 520     // a keyed write (its two pointers, key and OR)
#define CHEAT_COST_COND 450      // a conditional, and the walk past what it skips
#define CHEAT_COST_SKIP 105      // a cheat the off mask skips (cheatrun.s)
#define CHEAT_COST_WALK 20       // each op a false conditional walks past
// The caps, all picked cheats together: about 10% of a frame (280,896
// cycles) for what runs every frame, and about 23% with every condition
// true, inside VBlank's 30%: work a condition (a button, say) guards may
// take the more
#define CHEAT_CYCLES_MAX 28000
#define CHEAT_CYCLES_GATED_MAX 64000
// Both caps in one number, a cheat's load: max(its cycles that run whatever
// the conditions say, scaled so CHEAT_CYCLES_MAX is CHEAT_LOAD_MAX, and
// its cycles with every condition true). Loads that fit CHEAT_LOAD_MAX
// together fit both caps. Cycles that always run (the call, a skipped
// cheat) count as CHEAT_LOAD(cycles).
#define CHEAT_LOAD_MAX CHEAT_CYCLES_GATED_MAX
#define CHEAT_LOAD(cycles) (((cycles) * (u32)CHEAT_LOAD_MAX + CHEAT_CYCLES_MAX - 1) / CHEAT_CYCLES_MAX)
// The table's room in the patch area, the end word included (cheat.h)
#define CHEAT_TABLE_MAX 0x1400

enum cheat_lower_status {
	CHEAT_LOWER_OK,
	CHEAT_LOWER_TOO_BIG,          // over prog_max words or CHEAT_CYCLES_MAX, or an op's count over 0xFFFF
	CHEAT_LOWER_TOO_MANY_PATCHES, // more than patches_max patches
	CHEAT_LOWER_BAD_ADDRESS,      // an op outside the GBA's memory: likely a misread code
	CHEAT_LOWER_CANT_RUN,         // a ROM write under a condition, or an add, or or and on the ROM
};

// Turns one cheat's ops into its part of the interpreter's table (header
// and ops, at most prog_max words, *prog_words of them; none when nothing
// runs in game) with its load in *cost (CHEAT_LOAD_MAX; 0 if none): what a
// conditional without an ELSE covers counts as guarded, the rest as always
// run. And its ROM patches
// (*n_patches of them). Writes to the ROM (0x08000000-0x09FFFFFF) and ROM
// patch ops become patches (fills expanded, bytes merged into halfwords),
// applied at launch; ROM patch ops whatever a condition says, as mGBA
// applies its ROM patches. A plain write to the ROM in a cheat with
// conditionals can't run (CHEAT_LOWER_CANT_RUN): mGBA writes it only while
// its condition holds, and the game's PSRAM can't be written in game. Nor
// can an add, or or and on the ROM (0x09xxxxxx holds the cart's command
// registers), checked at each element's address. A cheat without conditionals runs its ops in order, writes of a
// width in a row as one run; one with conditionals keeps every op in its
// place (ROM patches and slowdown codes as nothing), so a condition's count
// covers the ops it covers in mGBA. IF_BUTTON tests CHEAT_BUTTON_KEYS.
// Slowdown codes do nothing. An address is good in EWRAM or IWRAM (mirrors
// too, as the hardware has them), IO, palette, VRAM, OAM, the ROM's first
// 32 MB mapping or SRAM; other mirrors and regions are bad, which catches
// most codes decoded with the wrong key or format. Addresses are checked
// before anything else. On any status but OK, the outputs are 0.
u8 cheat_lower(const struct cheat_op *ops, u32 n_ops, u32 *prog, u32 prog_max, u32 *prog_words, u32 *cost,
	       struct cheat_patch *patches, u32 patches_max, u32 *n_patches);

// Whether every address op touches is good, as cheat_lower checks it: a
// pointer write's pointer, a fill's or a counted add's whole range, a ROM
// patch in the ROM.
int cheat_op_address_ok(const struct cheat_op *op);

// Whether ops change anything: a write (a ROM write is a patch), pointer
// write, add, or or and of some count, or a ROM patch. Conditionals and
// slowdown codes alone do nothing.
int cheat_has_effect(const struct cheat_op *ops, u32 n_ops);

#ifdef CHEAT_RAW_LINES
// For tools on the host only (tools/cheat_patches.c builds with it): each
// line of the decoding cheat_decode kept, as it read it, decrypted. format
// is the line's as written plain (CHEAT_FMT_GSAV1_RAW or _PARV3_RAW for a
// GameShark line, CHEAT_FMT_CODEBREAKER, or CHEAT_FMT_VBA for one it can't
// write); drop marks a line that only sets a key or seed. Written plain in
// order, less the dropped, the lines decode as the cheat did with no key.
#define CHEAT_RAW_MAX 512
struct cheat_raw {
	u32 n;
	struct {
		u8 format, drop;
		u32 op1, op2;
	} line[CHEAT_RAW_MAX];
};
extern struct cheat_raw cheat_raw;
#endif

#endif
