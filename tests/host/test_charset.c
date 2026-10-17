// charset_next (source/charset.c): UTF-8 names drawn in the ASCII menu font.
// Each name is a heap copy of exactly its bytes, without a NUL, so
// AddressSanitizer catches a read past the length.
#include <stdlib.h>
#include <string.h>

#include "../../source/charset.h"
#include "../kernel/torii_test.h"
#include "tests.h"

// Draws a name the way DrawHZText12 does: one glyph per character, none for 0
static void glyphs(const char *name, u32 len, char *out)
{
	char *copy = malloc(len ? len : 1);
	memcpy(copy, name, len);
	u32 pos = 0, n = 0;
	while (pos < len) {
		u32 before = pos;
		char c = charset_next(copy, len, &pos);
		CHECK(pos > before && pos <= len);
		if (c)
			out[n++] = c;
	}
	out[n] = 0;
	free(copy);
}

static void check_glyphs(const char *name, const char *expected)
{
	char out[128];
	glyphs(name, strlen(name), out);
	if (strcmp(out, expected) != 0) {
		tlog_error("FAIL %s: \"%s\" draws as \"%s\", expected \"%s\"", __FILE__, name, out, expected);
		++torii_test_failures;
	}
}

void test_charset_ascii(void)
{
	char name[2] = {0, 0};
	for (u32 c = 1; c < 0x80; c++) {
		name[0] = c;
		check_glyphs(name, name);
	}
}

void test_charset_latin(void)
{
	check_glyphs("Pokémon Ñandú Æsir.gba", "Pokemon Nandu Asir.gba");
	check_glyphs("Crème brûlée", "Creme brulee");
	check_glyphs("Łódź Straße Œuvre", "Lodz Strase Ouvre");
	check_glyphs("Ünïcödé — “test” ‘½’", "Unicode - \"test\" '?'");
}

void test_charset_other_scripts(void)
{
	// One '?' per character, whatever its length in UTF-8
	check_glyphs("テスト ゲーム.gba", "??? ???.gba");
	check_glyphs("Ελληνικά", "????????");
	check_glyphs("emoji 😀.gba", "emoji ?.gba");
}

void test_charset_broken(void)
{
	// A scrolled name can start mid-character: its continuation bytes draw nothing
	check_glyphs("\xa9t\xa9", "t");
	// A sequence cut short, or a bad byte, draws one '?' and resyncs at the next byte
	check_glyphs("a\xe3\x83", "a?");
	check_glyphs("\xe3"
		     "b",
		     "?b");
	check_glyphs("\xff\xfe"
		     "c",
		     "??c");
	check_glyphs("\xf0\x9f\x98", "?");
}
