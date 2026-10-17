// charset_ascii (source/charset.c): every byte draws as a glyph the menu font has.
#include "../../source/charset.h"
#include "../kernel/torii_test.h"
#include "tests.h"

void test_charset_ascii(void)
{
	// ASCII passes through
	for (u32 c = 0; c < 0x80; c++)
		CHECK_EQ(charset_ascii(c), c);
	// The font has 128 glyphs; draw only printable ones for the upper half
	for (u32 c = 0x80; c < 0x100; c++) {
		char a = charset_ascii(c);
		CHECK(a >= ' ' && a <= '~');
	}
	// Code page 850: e acute, A ring, n tilde, u umlaut, sharp s, o slash
	CHECK_EQ(charset_ascii(0x82), 'e');
	CHECK_EQ(charset_ascii(0x8F), 'A');
	CHECK_EQ(charset_ascii(0xA4), 'n');
	CHECK_EQ(charset_ascii(0x81), 'u');
	CHECK_EQ(charset_ascii(0xE1), 's');
	CHECK_EQ(charset_ascii(0x9B), 'o');
	// Box drawing has no ASCII form
	CHECK_EQ(charset_ascii(0xB3), '?');
}
