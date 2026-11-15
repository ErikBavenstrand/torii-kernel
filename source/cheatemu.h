// Cheats for the games the built-in emulators run (GB, GBC and NES): the
// decoders of the codes libretro's database holds for them, and the patches
// they make in the game's image in PSRAM before the emulator starts.
// Hardware-free, as tests/host runs it.
// docs/research/cheats.md has the formats and the corpus's counts.
//
// Game Genie codes are ROM patches, made once at launch, whose compare byte
// (the 9-digit GB form, the 8-letter NES one) picks the bank they belong to.
// RAM writes (NES raw codes, GB GameShark) are pokes: the kernel writes them
// into a table inside the emulator's image in PSRAM (struct torii_cheats,
// cheatemu_find_table), which the emulator applies once a frame.
#ifndef TORII_CHEATEMU_H
#define TORII_CHEATEMU_H

#include <gba_base.h>

#include "cheatcode.h"

// The console a cheat file's game is for. GBA is the interpreter's.
enum cheatemu_console {
	CHEATEMU_GBA,
	CHEATEMU_GB,  // GB and GBC: Jagoomba Color
	CHEATEMU_NES,  // PocketNES
};

// One decoded line of a code
enum cheatemu_kind {
	CHEATEMU_GENIE = 1,  // a ROM patch: addr (a CPU address) becomes value, if the byte there is compare
	CHEATEMU_POKE = 2,   // a RAM write each frame: value to addr (compare: the GB WRAM bank, 0: the mapped one)
};

struct emucheat {
	u8 kind;
	u8 value;
	u8 compare;
	u8 has_compare;
	u16 addr;
};

// cheatemu_decode's answers
enum {
	CHEATEMU_OK,
	CHEATEMU_UNREADABLE,  // no decoder reads one of its lines
	CHEATEMU_CANT_RUN,  // it reads, but a line is a kind that isn't run yet (a GameShark code that isn't a WRAM or HRAM write)
	CHEATEMU_TOO_BIG,  // more lines than max
};

// Decodes the len bytes of one cheat's code, its lines joined by '+' (or
// spaces), for console: up to max lines into out, *n of them. A line that
// reads but doesn't run makes the cheat CANT_RUN (all or nothing).
u8 cheatemu_decode(u32 console, const char *code, u32 len, struct emucheat *out, u32 max, u32 *n);

// The console whose codes the len bytes of one cheat's code are shaped as
// (CHEATEMU_GB, CHEATEMU_NES), or CHEATEMU_GBA for none of them: every line must be a
// GB Game Genie (XXX-YYY[-ZZZ]), an 8-digit GameShark line or an Xploder's
// "$" and 8 digits (digits may be '?', a wildcard to fill in), or a NES genie word
// (6 or 8 letters of its alphabet) or "AAAA:VV", and one must be a Game Genie or "AAAA:VV"
// or have a '?' or '$' (an 8-digit line alone could be anything). The cheat file
// clean-up uses it to tell a GB, GBC or NES game's file (its name has no extension) from a
// GBA game's: whether the game is still on the card says nothing.
u32 cheatemu_shape(const char *code, u32 len);

// Why a cheat that decodes CANT_RUN doesn't run: the first of its lines that doesn't, as one of these
enum {
	CHEATEMU_WHY_OTHER,  // no reason to tell apart (a write that waits for a byte)
	CHEATEMU_WHY_SRAM,  // a GameShark write to the cart's RAM (A000-BFFF): a save keeps it
	CHEATEMU_WHY_VRAM,  // to video RAM (8000-9FFF)
	CHEATEMU_WHY_ROM,  // to the ROM (GB 0000-7FFF), OAM or I/O, or a NES ROM, PPU or register address
	CHEATEMU_WHY_TYPE,  // a GameShark type other than 01 and 90-97
};
u32 cheatemu_why_cant(u32 console, const char *code, u32 len);

// 1 if a GB Game Genie line of the code is for a byte the emulator's checksum samples (bank 0, the
// first 16 KB at every 128 bytes, four each): the kernel leaves it unpatched. 0 for a NES code, whose
// PRG offset depends on the mapper's banks.
u32 cheatemu_sampled_lines(u32 console, const char *code, u32 len);

// A line as a cheat_patch, which is what a decoded cheat keeps in the cheat
// file's patch room (cheatfile.h) and a launch keeps in cart SRAM: its offset
// is the kind above bit 16 and the address, the value is value | compare << 8,
// and the mask is 1 with a compare, 0 without. The patch is never a halfword
// patch, as no run of them ever matches (cheatfile_run_goes_on) by accident:
// each line carries its own offset, and a run of consecutive ones stores and
// gives back the same.
void cheatemu_to_patch(const struct emucheat *e, struct cheat_patch *p);
void cheatemu_from_patch(const struct cheat_patch *p, struct emucheat *e);

// --- The poke table in the emulator's image ---------------------------------

// What the emulators' torii_cheats holds (emulators/*/src/torii_cheats.c: a
// const block in .rodata the kernel finds by its magic and fills in PSRAM
// after the emulator is copied there; the emulator reads it every frame).
#define TORII_CHEATS_MAGIC "TORIICHT"
#define TORII_CHEATS_MAX 64
#define CHEATEMU_LINES_SCAN 64  // lines cheatemu_sampled_lines looks at
struct torii_cheat {
	u8 kind;  // 1: a poke
	u8 bank;  // GB: the WRAM bank (0: the one D000 maps to)
	u16 addr;
	u8 value;
	u8 pad[3];
};
struct torii_cheats {
	char magic[8];
	u32 version;  // 1
	u16 count;  // entries used
	u16 max;  // TORII_CHEATS_MAX: the emulator never reads past it
	struct torii_cheat e[TORII_CHEATS_MAX];
};

// The byte offset of the one torii_cheats in the n bytes of image (4-byte
// aligned, as the image is), or 0xFFFFFFFF if there is none or more than one
u32 cheatemu_find_table(const u8 *image, u32 n);

// 1 if a NES address is one a poke may write, and its place in PocketNES's
// RAM view: 0000-1FFF is the 2 KB of RAM mirrored (folded to 0000-07FF),
// 6000-7FFF the PRG-RAM; others are the ROM's, the PPU's and the registers
u32 cheatemu_nes_poke_addr(u32 addr, u16 *folded);

// --- Patching the game's image ---------------------------------------------

// The game's image, as the emulator is given it: GB and GBC the file as it is
// (bank 0 first), NES the file with its iNES header kept
struct cheatemu_rom {
	u8 console;
	u8 mapper;  // NES
	u8 banks;  // NES: PRG's 16 KB banks
	u32 size;  // the image's bytes
	u32 prg_at;  // NES: where PRG starts (16, or 528 with a trainer)
	// Where the image starts in PSRAM (after the emulator): the kernel loads it in 8 MB
	// pages, each block at base + its offset in the page, so a block that would end past
	// the page's 8 MB (the last 80 KB or so of a ROM of 8 MB) went out of the window: a byte
	// there isn't known to be in the game's copy, and isn't patched
	u32 base;
};

// Reads what a patch needs of the game: GB, from its size; NES, from the
// first 16 bytes (hdr), size being the file's. 0 if the NES header is no
// iNES one or the file doesn't hold the PRG it names.
int cheatemu_rom_gb(struct cheatemu_rom *rom, u32 size);
int cheatemu_rom_nes(struct cheatemu_rom *rom, const u8 *hdr, u32 size);

typedef u8 (*cheatemu_get)(void *ctx, u32 offset);
typedef void (*cheatemu_set)(void *ctx, u32 offset, u8 value);

struct cheatemu_done {
	u32 patched;  // bytes written
	u32 other;  // places the code's address is, but the compare byte isn't there
	u32 sampled;  // places left alone: the emulator keys its states and palette on them
	u32 limit;  // places left because the launch's patches (max) were all used
	u32 unmapped;  // places left: the loader wrote them past its page's window (cheatemu_rom's base)
	u8 unknown;  // a NES mapper whose banks aren't known, and no compare to tell them
};

// Makes the patches line e stands for in the image (rom, through get and
// set): at most max, added to done. A GB address below 4000 is bank 0's; from
// 4000, every other bank's that holds the compare byte (any, without one),
// as a Game Genie on the bus patches whichever bank is mapped. A NES address
// is a place in a PRG bank by the mapper's bank size (4 and 118/119: 8 KB, E000
// the last bank, C000 the one before, 8000 the others; 1, 2, 71 and 94: 16 KB, C000
// the last bank, 8000 the others; 7, 11, 34, 66 and 79: 32 KB; 0 and 3: fixed), the
// same way: a code is never made for a fixed bank's byte through a switchable window. A byte the emulators' ROM checksum
// samples is never patched: the first 16 KB of GB, of NES PRG, at every 128
// bytes, four bytes each (cheatemu_sampled).
void cheatemu_patch(const struct cheatemu_rom *rom, const struct emucheat *e, cheatemu_get get, cheatemu_set set,
		    void *ctx, u32 max, struct cheatemu_done *done);

// 1 if the byte at offset of the image is one the emulators sum
int cheatemu_sampled(const struct cheatemu_rom *rom, u32 offset);

#endif
