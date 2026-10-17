// Hardware-free parts of the in-game patches (source/GBApatch.c): the
// built-in table of IRQ handler stores, and the hotkeys the patched IRQ
// handler checks. tests/host runs them on the host.
#ifndef TORII_IRQPATCH_H
#define TORII_IRQPATCH_H

#include <gba_base.h>

// True for the addresses games keep their IRQ handler at, which the kernel
// patches to 0x3007FF4 so its hook can sit at 0x3007FFC.
int Is_irq_literal(u32 word);

// Finds a game in the built-in table (source/reset_table.h: game code, count,
// then that many word offsets into the ROM), from word *pos on (start at 0).
// Returns the count, points *offsets at them and moves *pos past the entry,
// so a second call finds the next entry for the same code; -1 if there is no
// more. A code that isn't four letters or digits (homebrew often leaves it
// blank) is never listed.
int Find_irq_table_entry(const u32 *table, u32 words, const u8 code[4], u32 *pos, const u32 **offsets);

// The KEYINPUT value while exactly the three keys in slots[] are held (0 A,
// 1 B, 2 SELECT, 3 START, 4 RIGHT, 5 LEFT, 6 UP, 7 DOWN, 8 R, 9 L), as the
// patched IRQ handler compares it. A slot past 9, such as erased NOR's
// 0xFFFF, counts as the key in defaults[], as the settings page shows it.
u16 Hotkey_value(const u16 slots[3], const u16 defaults[3]);

// A code patch for one exact ROM, for games the general patching can't hook
// (see rom_patches in irqpatch.c). It applies only to the ROM whose header
// has this game code, revision and complement check, and only if the word
// at offset (bytes, word-aligned, in the first 8 MB) is expected.
typedef struct {
	char code[4];  // header 0xAC
	u8 version;    // header 0xBC
	u8 checksum;   // header 0xBD
	u32 offset;
	u32 expected;
	u32 value;
} ST_rom_patch;

extern const ST_rom_patch rom_patches[];
extern const u32 rom_patch_count;

// True if header (the ROM's first 0xC0 bytes) is the ROM patch p is for.
int Rom_patch_matches(const ST_rom_patch *p, const u8 *header);

#endif
