// Settings that decide how a game launches (source/Ezcard_OP.c).
#include <string.h>

#include <gba_base.h>

#include "torii_test.h"

u16 Game_GPIO_status(void);
extern u8 GAMECODE[4];
extern u16 gl_game_clock;
extern u16 gl_game_rumble;

static u16 gpio(const char *code, u16 clock, u16 rumble)
{
	memcpy(GAMECODE, code, 4);
	gl_game_clock = clock;
	gl_game_rumble = rumble;
	return Game_GPIO_status();
}

void test_clock_and_rumble_are_separate(void)
{
	u16 clock = gl_game_clock, rumble = gl_game_rumble;
	// Turning CLOCK off leaves rumble on (ez-flash/omega-de-kernel#5).
	CHECK_EQ(gpio("V49E", 0, 1), 1);  // Drill Dozer
	CHECK_EQ(gpio("RZWJ", 0, 1), 1);  // WarioWare: Twisted
	CHECK_EQ(gpio("V49P", 1, 0), 0);  // RUMBLE off
	// Every other game follows CLOCK alone.
	CHECK_EQ(gpio("BPEE", 1, 0), 1);  // Pokémon Emerald
	CHECK_EQ(gpio("BPEE", 0, 1), 0);
	gl_game_clock = clock;
	gl_game_rumble = rumble;
}
