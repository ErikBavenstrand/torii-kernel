// The recently played list (source/ezkernelnew.c).
#include <stdio.h>
#include <string.h>

#include <gba_base.h>

#include "../../source/ezkernel.h"
#include "torii_test.h"
#include "tests.h"

void test_recently_played(void)
{
	char name[16];
	u32 count = 0;
	for (int i = 0; i < 12; i++) {
		sprintf(name, "/game%d.gba", i);
		count = Recently_play_add(name, count);
	}
	CHECK_EQ(count, 10);  // the 11th and 12th games push the oldest out
	CHECK(strcmp(p_recently_play[0], "/game11.gba") == 0);
	CHECK(strcmp(p_recently_play[9], "/game2.gba") == 0);

	// Playing a listed game moves it to the top without adding a row.
	count = Recently_play_add("/game5.gba", count);
	CHECK_EQ(count, 10);
	CHECK(strcmp(p_recently_play[0], "/game5.gba") == 0);
	CHECK(strcmp(p_recently_play[1], "/game11.gba") == 0);
	CHECK(strcmp(p_recently_play[7], "/game4.gba") == 0);
	CHECK(strcmp(p_recently_play[8], "/game3.gba") == 0);

	count = Recently_play_add("/new.gba", 2);
	CHECK_EQ(count, 3);
	CHECK(strcmp(p_recently_play[0], "/new.gba") == 0);
	CHECK(strcmp(p_recently_play[1], "/game5.gba") == 0);

	// The longest entry the kernel writes, a 255-byte folder and a 99-byte
	// name, fits a row whole.
	char longest[RECENT_LEN];
	memset(longest, 'a', sizeof(longest) - 1);
	longest[0] = '/';
	longest[MAX_path_len - 1] = '/';
	longest[sizeof(longest) - 1] = 0;
	count = Recently_play_add(longest, count);
	CHECK(strcmp(p_recently_play[0], longest) == 0);
	CHECK(strcmp(p_recently_play[1], "/new.gba") == 0);
}

void test_recently_played_nor(void)
{
	// A game started from NOR is listed by its NOR entry's name
	CHECK(strcmp(Recently_play_nor("NOR:Advance Wars.gba"), "Advance Wars.gba") == 0);
	CHECK(Recently_play_nor("/NOR:Advance Wars.gba") == NULL);
	CHECK(Recently_play_nor("/GBA/Advance Wars.gba") == NULL);
	u32 count = Recently_play_add("/GBA/Advance Wars.gba", 0);
	count = Recently_play_add("NOR:Advance Wars.gba", count);
	CHECK_EQ(count, 2);  // the SD file and the NOR copy are two entries
	CHECK(strcmp(p_recently_play[0], "NOR:Advance Wars.gba") == 0);
}

void test_recently_played_remove(void)
{
	// A file deleted from the SD card comes off the list; the rows under it
	// move up, and the NOR copy of a game of that name stays
	u32 count = Recently_play_add("/a.gba", 0);
	count = Recently_play_add("NOR:b.gba", count);
	count = Recently_play_add("/GBA/b.gba", count);
	count = Recently_play_add("/c.gba", count);
	CHECK_EQ(Recently_play_remove("/missing.gba", count), 4);
	count = Recently_play_remove("/GBA/b.gba", count);
	CHECK_EQ(count, 3);
	CHECK(strcmp(p_recently_play[0], "/c.gba") == 0);
	CHECK(strcmp(p_recently_play[1], "NOR:b.gba") == 0);
	CHECK(strcmp(p_recently_play[2], "/a.gba") == 0);
	CHECK_EQ(p_recently_play[3][0], 0);
	count = Recently_play_remove("/a.gba", count);  // the last row
	CHECK_EQ(count, 2);
	CHECK(strcmp(p_recently_play[1], "NOR:b.gba") == 0);
	count = Recently_play_remove("/c.gba", count);  // the first row
	CHECK_EQ(count, 1);
	CHECK(strcmp(p_recently_play[0], "NOR:b.gba") == 0);
}
