// The NOR game list (source/NORflash_OP.c).
#include <gba_base.h>
#include <gba_interrupt.h>
#include <string.h>

#include "../../source/ez_define.h"
#include "../../source/ezkernel.h"
#include "../../source/NORflash_OP.h"
#include "torii_test.h"
#include "tests.h"

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

extern u32 game_total_NOR;

void test_nor_write_refused_when_list_full(void)
{
	// The 65th game written to NOR went into pNorFS[64], past the list's end
	// in EWRAM, however much NOR space was left. It now counts as NOR full.
	char name[] = "Small.gba";
	u32 saved = game_total_NOR;
	game_total_NOR = MAX_NOR;
	CHECK_EQ(Loadfile2NOR(name, 0, 0, 0), 2);
	game_total_NOR = saved;
}

extern u32 gl_norOffset;

void test_nor_scan_keeps_interrupts(void)
{
	// A list whose games reach the end of the 64 MB returned from the scan
	// with interrupts still off. One older kernel's entry of 64 MB, named
	// as whatever the game NOR's first header holds, fills it.
	memset(pNorFS, 0, sizeof(FM_NOR_FS) * MAX_NOR);
	memcpy(pNorFS[0].gamename, (const void *)(FlashBase_S98 + 0xA0), sizeof(pNorFS[0].gamename));
	pNorFS[0].filesize = 0x4000000;
	u16 ime = REG_IME;
	REG_IME = 1;
	CHECK_EQ(GetFileListFromNor(), 1);
	CHECK_EQ(REG_IME, 1);
	CHECK_EQ(gl_norOffset, 0x4000000);
	REG_IME = ime;
	// Diagnostics runs these on a real cart: the real list back
	Read_NOR_info();
	game_total_NOR = GetFileListFromNor();
}
