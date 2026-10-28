// The FPGA firmware update's allowlist: which carts Torii may write FPGA
// firmware to, told by the FPGA's version word (Read_FPGA_ver). Hardware-free:
// Check_FW_update (Ezcard_OP.c) does the cart's side, and About and
// Diagnostics name the board from it.
//
// The word's low byte is the firmware's version and its high nibble the board
// (EZ-Flash's 1.06 source). The only word measured on a real cart is 0xB007,
// a black Revision B (LX16) on firmware 7 (docs/research/omega-de-hardware.md);
// EZ-Flash's source also takes 0xA for LX16 ("0xA000 is initially"). Every
// other word was, in that source, taken as an LX9 Revision A and offered the
// LX9 image, so an unknown board (the blue EZODE-B, a bus that reads 0xFFFF)
// would have been written the wrong firmware and bricked. Here any word not
// on the allowlist is FPGA_BOARD_UNKNOWN, and nothing is written to it.
//
// LX9 stays off the allowlist until a real Revision A's word is measured:
// fpga_board never returns FPGA_BOARD_LX9, and its image and update path in
// Check_FW_update are kept, unreachable.
#ifndef TORII_FPGA_BOARD_H
#define TORII_FPGA_BOARD_H

#include <gba_base.h>

enum fpga_board {
	FPGA_BOARD_UNKNOWN,
	FPGA_BOARD_LX16,  // Revision B: high nibble 0xA or 0xB
	FPGA_BOARD_LX9,   // Revision A: never returned (see the top)
};

enum fpga_board fpga_board(u16 word);
// The firmware version in the word (its low byte)
u16 fpga_firmware(u16 word);
// 1 if the word is an allowlisted board whose firmware is older than
// built_in, the version of the kernel's image for that board
int fpga_update_due(u16 word, u16 built_in);

#endif
