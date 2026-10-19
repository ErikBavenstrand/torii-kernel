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
	// The longest names the kernel lists: 255 bytes, the .gb one grows to 256
	char game[GAME_NAME_MAX + 1], expected[SAVE_NAME_MAX];
	memset(game, 'x', GAME_NAME_MAX);
	memcpy(game + GAME_NAME_MAX - 3, ".gb", 4);
	memset(expected, 'x', GAME_NAME_MAX - 3);
	memcpy(expected + GAME_NAME_MAX - 3, ".esv", 5);
	check_name(game, 2, expected);
	memcpy(game + GAME_NAME_MAX - 4, ".gba", 5);
	memset(expected, 'x', GAME_NAME_MAX - 4);
	memcpy(expected + GAME_NAME_MAX - 4, ".sav", 5);
	check_name(game, 0, expected);
}

static void check_ext(const char *game, const char *ext, const char *expected)
{
	char *in = malloc(strlen(game) + 1);
	strcpy(in, game);
	char *out = malloc(GAME_NAME_MAX + 1);
	name_with_ext(out, in, ext);
	if (strcmp(out, expected) != 0) {
		tlog_error("FAIL %s: %s with %s is %s, expected %s", __FILE__, game, ext, out, expected);
		++torii_test_failures;
	}
	free(out);
	free(in);
}

void test_name_with_ext(void)
{
	check_ext("Advance Wars.gba", "pat", "Advance Wars.pat");
	check_ext("Dr. Mario.gb", "rts", "Dr. Mariorts");  // the last three bytes, as the kernel always did
	check_ext("ab", "cht", "abcht");
	check_ext("", "mde", "mde");
	char game[GAME_NAME_MAX + 1], expected[GAME_NAME_MAX + 1];
	memset(game, 'y', GAME_NAME_MAX);
	memcpy(game + GAME_NAME_MAX - 4, ".gba", 5);
	memcpy(expected, game, GAME_NAME_MAX + 1);
	memcpy(expected + GAME_NAME_MAX - 3, "rts", 3);
	check_ext(game, "rts", expected);
}
