// NOR game list loading (source/Ezcard_OP.c).
#include <gba_base.h>

#include "../../source/ez_define.h"
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

u32 Loadfile2NOR(char *filename, u32 NORaddress, u16 have_patch, u8 SAVEMODE);
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
