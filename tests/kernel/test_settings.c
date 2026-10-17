// Settings that decide how a game launches (source/Ezcard_OP.c,
// source/ezkernelnew.c).
#include <gba_base.h>

#include "../../source/Ezcard_OP.h"
#include "torii_test.h"

u32 Game_hardware(u32 is_EMU, const u8 *gamecode, u8 gb_cart_type);
extern u16 gl_game_clock;
extern u16 gl_game_rumble;

void test_game_hardware(void)
{
	CHECK_EQ(Game_hardware(0, (const u8 *) "V49E", 0), GAME_USES_RUMBLE);  // Drill Dozer
	CHECK_EQ(Game_hardware(0, (const u8 *) "RZWJ", 0), GAME_USES_RUMBLE);  // WarioWare: Twisted!
	CHECK_EQ(Game_hardware(0, (const u8 *) "BPEE", 0), GAME_USES_CLOCK);   // Pokémon Emerald
	CHECK_EQ(Game_hardware(1, (const u8 *) "\0\0\0\0", 0x10), GAME_USES_CLOCK);  // MBC3+TIMER: Pokémon Crystal
	CHECK_EQ(Game_hardware(1, (const u8 *) "\0\0\0\0", 0x1E), GAME_USES_RUMBLE);  // MBC5+RUMBLE: Pokémon Pinball
	CHECK_EQ(Game_hardware(2, (const u8 *) "\0\0\0\0", 0x00), 0);  // ROM only: Tetris
	CHECK_EQ(Game_hardware(3, (const u8 *) "\0\0\0\0", 0), GAME_USES_CLOCK);  // NES
}

static u16 gpio(u32 uses, u16 clock, u16 rumble)
{
	gl_game_uses = uses;
	gl_game_clock = clock;
	gl_game_rumble = rumble;
	return Game_GPIO_status();
}

void test_clock_and_rumble_are_separate(void)
{
	u16 clock = gl_game_clock, rumble = gl_game_rumble;
	u32 uses = gl_game_uses;
	// Turning CLOCK off leaves rumble on (ez-flash/omega-de-kernel#5).
	CHECK_EQ(gpio(GAME_USES_RUMBLE, 0, 1), 1);
	CHECK_EQ(gpio(GAME_USES_RUMBLE, 1, 0), 0);
	CHECK_EQ(gpio(GAME_USES_CLOCK, 1, 0), 1);
	CHECK_EQ(gpio(GAME_USES_CLOCK, 0, 1), 0);
	CHECK_EQ(gpio(0, 1, 1), 0);  // a game that uses neither
	gl_game_clock = clock;
	gl_game_rumble = rumble;
	gl_game_uses = uses;
}
