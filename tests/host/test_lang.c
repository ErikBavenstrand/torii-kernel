// The language strings' placeholders (source/lang.c, lang_fill).
#include <string.h>

#include "../../source/lang.h"
#include "../kernel/torii_test.h"
#include "tests.h"

static void fill(u32 room, const char *text, u32 a, u32 b, const char *want)
{
	char out[64];
	memset(out, 'x', sizeof(out));
	lang_fill(out, room, text, a, b);
	CHECK(strcmp(out, want) == 0);
	CHECK(room == sizeof(out) || out[room] == 'x');  // nothing past its room
}

void test_lang_fill(void)
{
	fill(64, "Screenshot {1}/{2}", 3, 5, "Screenshot 3/5");
	fill(64, "{2} of {1}", 1, 4000000000u, "4000000000 of 1");
	fill(64, "{1}{1}", 7, 0, "77");
	fill(64, "no placeholders", 1, 2, "no placeholders");
	fill(64, "{3} {1 {x} {", 9, 9, "{3} {1 {x} {");
	fill(64, "", 1, 2, "");
	// Cut to its room, on a whole character and a whole number
	fill(6, "abcdefgh", 0, 0, "abcde");
	fill(6, "ab{1}", 12345, 0, "ab");
	fill(5, "abc\xC3\xA9", 0, 0, "abc");
	fill(6, "abc\xC3\xA9", 0, 0, "abc\xC3\xA9");
	fill(1, "abc", 0, 0, "");
	// The in-game menu's English (gl_ingame)
	LoadEnglish();
	char out[40];
	lang_fill(out, sizeof(out), gl_ingame->unsaved_shots[1], 2, 0);
	CHECK(strcmp(out, "Not on card yet: 2 screenshots") == 0);
	lang_fill(out, sizeof(out), gl_ingame->shot_count, 1, 5);
	CHECK(strcmp(out, "Screenshot 1/5") == 0);
}
