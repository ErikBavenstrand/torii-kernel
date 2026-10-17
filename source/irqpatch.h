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
// then that many word offsets into the ROM). Returns the count and points
// *offsets at them, or -1 if the game isn't listed. A code that isn't four
// letters or digits (homebrew often leaves it blank) is never listed.
int Find_irq_table_entry(const u32 *table, u32 words, const u8 code[4], const u32 **offsets);

// The KEYINPUT value while exactly the three keys in slots[] are held (0 A,
// 1 B, 2 SELECT, 3 START, 4 RIGHT, 5 LEFT, 6 UP, 7 DOWN, 8 R, 9 L), as the
// patched IRQ handler compares it. A slot past 9, such as erased NOR's
// 0xFFFF, counts as the key in defaults[], as the settings page shows it.
u16 Hotkey_value(const u16 slots[3], const u16 defaults[3]);

#endif
