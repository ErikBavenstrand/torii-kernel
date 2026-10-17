// save_name (source/savename.c). Each game name is a heap copy of exactly its
// bytes plus the NUL, and the output exactly SAVE_NAME_MAX bytes, so
// AddressSanitizer catches a write or read past either.
#include <stdlib.h>
#include <string.h>

#include "../../source/savename.h"
#include "../kernel/torii_test.h"
#include "tests.h"

static void check_name(const char *game, u32 is_emu, const char *expected)
{
	char *in = malloc(strlen(game) + 1);
	strcpy(in, game);
	char *out = malloc(SAVE_NAME_MAX);
	save_name(out, in, is_emu);
	if (strcmp(out, expected) != 0) {
		tlog_error("FAIL %s: %s (%u) saves as %s, expected %s", __FILE__, game, (unsigned)is_emu, out,
			   expected);
		++torii_test_failures;
	}
	free(out);
	free(in);
}

void test_save_name(void)
{
	check_name("Advance Wars.gba", 0, "Advance Wars.sav");
	check_name("Tetris DX.gbc", 1, "Tetris DX.esv");
	check_name("Dr. Mario.gb", 2, "Dr. Mario.esv");
	check_name("Super Mario Bros.nes", 3, "Super Mario Bros.esv");
	check_name("テスト ゲーム.gba", 0, "テスト ゲーム.sav");
}

void test_save_name_longest(void)
{
	// The longest names a list entry holds: 99 bytes, the .gb one grows to 100
	char game[100], expected[101];
	memset(game, 'x', 99);
	memcpy(game + 96, ".gb", 4);
	memset(expected, 'x', 96);
	memcpy(expected + 96, ".esv", 5);
	check_name(game, 2, expected);
	memcpy(game + 95, ".gba", 5);
	memset(expected, 'x', 95);
	memcpy(expected + 95, ".sav", 5);
	check_name(game, 0, expected);
}
