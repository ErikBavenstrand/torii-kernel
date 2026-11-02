// Cheats for the games the built-in emulators run (GB, GBC and NES): the
// decoders of the codes libretro's database holds for them, and the patches
// they make in the game's image in PSRAM before the emulator starts.
// Hardware-free, as tests/host runs it. docs/plans/emulator-cheats.md is the
// design; docs/research/cheats.md the formats and the corpus's counts.
//
// Stage 1 runs Game Genie codes only: a ROM patch, made once at launch,
// whose compare byte (the 9-digit GB form, the 8-letter NES one) picks the
// bank it belongs to. A code of another kind (GameShark and raw RAM writes)
// reads, and is CANT_RUN until the emulators have a table for it.
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
	CHEATEMU_CANT_RUN,    // it reads, but a line is a kind that isn't run yet (a RAM write)
	CHEATEMU_TOO_BIG,     // more lines than max
};

// Decodes the len bytes of one cheat's code, its lines joined by '+' (or
// spaces), for console: up to max lines into out, *n of them. Only Game Genie
// lines give lines; any other kind makes the cheat CANT_RUN (all or nothing).
u8 cheatemu_decode(u32 console, const char *code, u32 len, struct emucheat *out, u32 max, u32 *n);

// A line as a cheat_patch, which is what a decoded cheat keeps in the cheat
// file's patch room (cheatfile.h) and a launch keeps in cart SRAM: its offset
// is the kind above bit 16 and the address, the value is value | compare << 8,
// and the mask is 1 with a compare, 0 without. The patch is never a halfword
// patch, as no run of them ever matches (cheatfile_run_goes_on) by accident:
// each line carries its own offset, and a run of consecutive ones stores and
// gives back the same.
void cheatemu_to_patch(const struct emucheat *e, struct cheat_patch *p);
void cheatemu_from_patch(const struct cheat_patch *p, struct emucheat *e);

// --- Patching the game's image ---------------------------------------------

// The game's image, as the emulator is given it: GB and GBC the file as it is
// (bank 0 first), NES the file with its iNES header kept
struct cheatemu_rom {
	u8 console;
	u8 mapper;    // NES
	u8 banks;     // NES: PRG's 16 KB banks
	u32 size;     // the image's bytes
	u32 prg_at;   // NES: where PRG starts (16, or 528 with a trainer)
};

// Reads what a patch needs of the game: GB, from its size; NES, from the
// first 16 bytes (hdr), size being the file's. 0 if the NES header is no
// iNES one or the file doesn't hold the PRG it names.
int cheatemu_rom_gb(struct cheatemu_rom *rom, u32 size);
int cheatemu_rom_nes(struct cheatemu_rom *rom, const u8 *hdr, u32 size);

typedef u8 (*cheatemu_get)(void *ctx, u32 offset);
typedef void (*cheatemu_set)(void *ctx, u32 offset, u8 value);

struct cheatemu_done {
	u32 patched;   // bytes written
	u32 other;     // places the code's address is, but the compare byte isn't there
	u32 sampled;   // places left alone: the emulator keys its states and palette on them
	u32 limit;     // places left because the launch's patches (max) were all used
	u8 unknown;    // a NES mapper whose banks aren't known, and no compare to tell them
};

// Makes the patches line e stands for in the image (rom, through get and
// set): at most max, added to done. A GB address below 4000 is bank 0's; from
// 4000, every other bank's that holds the compare byte (any, without one),
// as a Game Genie on the bus patches whichever bank is mapped. A NES address
// is a place in a PRG bank by the mapper's bank size (4 and 118/119: 8 KB; 1,
// 2, 71 and 94: 16 KB with the last fixed at C000; 7, 11, 34, 66 and 79: 32
// KB; 0 and 3: fixed), the same way. A byte the emulators' ROM checksum
// samples is never patched: the first 16 KB of GB, of NES PRG, at every 128
// bytes, four bytes each (cheatemu_sampled).
void cheatemu_patch(const struct cheatemu_rom *rom, const struct emucheat *e, cheatemu_get get, cheatemu_set set,
		    void *ctx, u32 max, struct cheatemu_done *done);

// 1 if the byte at offset of the image is one the emulators sum
int cheatemu_sampled(const struct cheatemu_rom *rom, u32 offset);

#endif
