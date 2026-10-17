// NOR game list loading (source/Ezcard_OP.c).
#include <gba_base.h>

#include "torii_test.h"
#include "tests.h"

void Read_NOR_info(void);
extern u16 SET_info_buffer[0x200];

void test_nor_info_stays_in_bounds(void)
{
	// Reading the list used to copy twice its size, running past the end of
	// EWRAM and wrapping onto SET_info_buffer at its start.
	SET_info_buffer[0] = 0xA5A5;
	SET_info_buffer[0x1FF] = 0x5A5A;
	Read_NOR_info();
	CHECK_EQ(SET_info_buffer[0], 0xA5A5);
	CHECK_EQ(SET_info_buffer[0x1FF], 0x5A5A);
}
