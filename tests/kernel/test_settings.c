// Settings that decide how a game launches (source/Ezcard_OP.c).
#include <string.h>

#include <gba_base.h>

#include "torii_test.h"

u16 Game_GPIO_status(void);
extern u8 GAMECODE[4];
extern u16 gl_ingame_RTC_open_status;
extern u16 gl_ingame_rumble;

static u16 gpio(const char *code, u16 rtc, u16 rumble)
{
	memcpy(GAMECODE, code, 4);
	gl_ingame_RTC_open_status = rtc;
	gl_ingame_rumble = rumble;
	return Game_GPIO_status();
}

void test_rtc_and_rumble_are_separate(void)
{
	u16 rtc = gl_ingame_RTC_open_status, rumble = gl_ingame_rumble;
	// Turning GAME RTC off leaves rumble on (ez-flash/omega-de-kernel#5).
	CHECK_EQ(gpio("V49E", 0, 1), 1);  // Drill Dozer
	CHECK_EQ(gpio("RZWJ", 0, 1), 1);  // WarioWare: Twisted
	CHECK_EQ(gpio("V49P", 1, 0), 0);  // RUMBLE off
	// Other games follow GAME RTC alone.
	CHECK_EQ(gpio("BPEE", 1, 0), 1);  // Pokémon Emerald
	CHECK_EQ(gpio("BPEE", 0, 1), 0);
	gl_ingame_RTC_open_status = rtc;
	gl_ingame_rumble = rumble;
}
