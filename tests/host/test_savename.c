// name_with_ext (source/savename.c). Each game name is a heap copy of exactly its
// bytes plus the NUL, and the output exactly GAME_NAME_MAX + 1 bytes, so
// AddressSanitizer catches a write or read past either.
#include <stdlib.h>
#include <string.h>

#include "../../source/savename.h"
#include "../kernel/torii_test.h"
#include "tests.h"

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
