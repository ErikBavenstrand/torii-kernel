// File type detection (source/ezkernelnew.c).
#include <gba_base.h>

#include "torii_test.h"

u32 Check_file_type(char *pfilename);

void test_file_type(void)
{
	char gba[] = "Pokemon Emerald.gba", upper[] = "GAME.GBA", gbc[] = "zelda.gbc", gb[] = "tetris.gb",
	     nes[] = "mario.nes", txt[] = "notes.txt";
	CHECK_EQ(Check_file_type(gba), 0);
	CHECK_EQ(Check_file_type(upper), 0);
	CHECK_EQ(Check_file_type(gbc), 1);
	CHECK_EQ(Check_file_type(gb), 2);
	CHECK_EQ(Check_file_type(nes), 3);
	CHECK_EQ(Check_file_type(txt), 0xff);
}
