// The FPGA firmware update's allowlist (source/fpga_board.c), over every word.
#include "../../source/fpga_board.h"
#include "../kernel/torii_test.h"
#include "tests.h"

void test_fpgaboard(void)
{
	// The one word measured on a real cart, and EZ-Flash's other LX16 nibble
	CHECK_EQ(fpga_board(0xB007), FPGA_BOARD_LX16);
	CHECK_EQ(fpga_board(0xA000), FPGA_BOARD_LX16);
	CHECK_EQ(fpga_firmware(0xB007), 7);
	CHECK(!fpga_update_due(0xB007, 7));
	CHECK(fpga_update_due(0xB006, 7));
	CHECK(fpga_update_due(0xA000, 7));
	// What EZ-Flash's source took as LX9, and a floating bus
	CHECK_EQ(fpga_board(0x0004), FPGA_BOARD_UNKNOWN);
	CHECK_EQ(fpga_board(0xFFFF), FPGA_BOARD_UNKNOWN);
	CHECK(!fpga_update_due(0x0004, 0xFF));
	// Every word: LX9 never, LX16 only for nibbles A and B, and an update only
	// to those, only below the built-in version
	u32 lx16 = 0, due = 0, lx9 = 0;
	for (u32 w = 0; w <= 0xFFFF; w++) {
		enum fpga_board b = fpga_board((u16)w);
		u32 nibble = w >> 12;
		lx9 += b == FPGA_BOARD_LX9;
		lx16 += b == FPGA_BOARD_LX16;
		CHECK_EQ(b == FPGA_BOARD_LX16, nibble == 0xA || nibble == 0xB);
		int d = fpga_update_due((u16)w, 7);
		due += d;
		CHECK_EQ(d, (nibble == 0xA || nibble == 0xB) && (w & 0xFF) < 7);
	}
	CHECK_EQ(lx9, 0);
	CHECK_EQ(lx16, 0x2000);
	CHECK_EQ(due, 2 * 16 * 7);
}
