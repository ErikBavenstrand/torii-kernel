// Parse_cheat_line (source/cheat.c). Lines are heap copies without a NUL, so
// AddressSanitizer catches a read past len.
#include <stdlib.h>
#include <string.h>

#include "../../source/cheat.h"
#include "../kernel/torii_test.h"
#include "tests.h"

static void parse(const char *text)
{
	u32 len = strlen(text);
	char *line = malloc(len ? len : 1);
	memcpy(line, text, len);
	gl_cheat_count = 0;
	Parse_cheat_line(line, len);
	free(line);
}

void test_cheat_parse(void)
{
	parse("3001234,0A,0B;20000,FF");
	CHECK_EQ(gl_cheat_count, 3);
	CHECK_EQ(pCHEAT[0].address, 0x3001234);
	CHECK_EQ(pCHEAT[0].VAL, 0x0A);
	CHECK_EQ(pCHEAT[1].address, 0x3001235);  // later values go to the next address
	CHECK_EQ(pCHEAT[1].VAL, 0x0B);
	CHECK_EQ(pCHEAT[2].address, 0x20000);
	CHECK_EQ(pCHEAT[2].VAL, 0xFF);

	parse(" 2000000 , 1 ; ");  // spaces are skipped
	CHECK_EQ(gl_cheat_count, 1);
	CHECK_EQ(pCHEAT[0].address, 0x2000000);
	CHECK_EQ(pCHEAT[0].VAL, 1);

	parse("");
	CHECK_EQ(gl_cheat_count, 0);

	parse("09afAF09,9f,F9,0a");  // every edge of the hex digit ranges, either case
	CHECK_EQ(gl_cheat_count, 3);
	CHECK_EQ(pCHEAT[0].address, 0x09AFAF09);
	CHECK_EQ(pCHEAT[0].VAL, 0x9F);
	CHECK_EQ(pCHEAT[1].VAL, 0xF9);
	CHECK_EQ(pCHEAT[2].VAL, 0x0A);

	parse("2000000,12,3");  // a short value after a long one doesn't keep its digits
	CHECK_EQ(pCHEAT[1].VAL, 3);
}

void test_cheat_reads_only_len(void)
{
	// The kernel passes a length into a larger buffer: the rest is ignored.
	static const char text[] = "2000000,01;2000004,02";
	char *line = malloc(10);  // only the first 10 bytes, so ASan sees a read past them
	memcpy(line, text, 10);
	gl_cheat_count = 0;
	Parse_cheat_line(line, 10);
	CHECK_EQ(gl_cheat_count, 1);
	CHECK_EQ(pCHEAT[0].VAL, 1);
	free(line);
}

void test_cheat_long_fields(void)
{
	parse("123456789A,BCD");  // fields are cut to their buffers: 8 and 2 digits
	CHECK_EQ(gl_cheat_count, 1);
	CHECK_EQ(pCHEAT[0].address, 0x12345678);
	CHECK_EQ(pCHEAT[0].VAL, 0xBC);

	parse("FFFFFFFF,01,02");  // the next address wraps around
	CHECK_EQ(gl_cheat_count, 2);
	CHECK_EQ(pCHEAT[1].address, 0);
}

void test_cheat_cap(void)
{
	// 700 values for one address: entries past CHEAT_MAX are dropped.
	char *line = malloc(8 + 700 * 3);
	strcpy(line, "2000000");
	for (int i = 0; i < 700; i++)
		strcat(line, ",FF");
	parse(line);
	CHECK_EQ(gl_cheat_count, CHEAT_MAX);
	CHECK_EQ(pCHEAT[CHEAT_MAX - 1].address, 0x2000000 + CHEAT_MAX - 1);
	free(line);

	// A full table takes no more, even from a later line.
	Parse_cheat_line("3000000,01", 10);
	CHECK_EQ(gl_cheat_count, CHEAT_MAX);
}

void test_cheat_garbage(void)
{
	// Random bytes, including NULs and separators, must stay in bounds.
	static const char alphabet[] = "0123456789abcdefABCDEFxyz ,;,;\0\xff";
	char line[64];
	srand(1);
	for (int round = 0; round < 20000; round++) {
		u32 len = rand() % sizeof(line);
		for (u32 i = 0; i < len; i++)
			line[i] = alphabet[rand() % (sizeof(alphabet) - 1)];
		gl_cheat_count = rand() % (CHEAT_MAX + 1);
		Parse_cheat_line(line, len);
		if (gl_cheat_count > CHEAT_MAX) {
			CHECK(gl_cheat_count <= CHEAT_MAX);
			break;
		}
	}
	parse(",,,;;;");
	CHECK(gl_cheat_count <= CHEAT_MAX);
}
