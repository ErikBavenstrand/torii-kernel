// The FPGA firmware update's allowlist (fpga_board.h)
#include "fpga_board.h"

enum fpga_board fpga_board(u16 word)
{
	switch (word & 0xF000) {
	case 0xA000:
	case 0xB000:
		return FPGA_BOARD_LX16;
	default:
		return FPGA_BOARD_UNKNOWN;
	}
}

u16 fpga_firmware(u16 word)
{
	return word & 0xFF;
}

int fpga_update_due(u16 word, u16 built_in)
{
	return fpga_board(word) != FPGA_BOARD_UNKNOWN && fpga_firmware(word) < built_in;
}
