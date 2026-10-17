// The recently played list (source/ezkernelnew.c).
#include <stdio.h>
#include <string.h>

#include <gba_base.h>

#include "torii_test.h"

extern char p_recently_play[10][512];
u32 Recently_play_add(const char *entry, u32 count);

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
}
