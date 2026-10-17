// Cheat parsing and the cheat table's cap (source/cheat.c, source/GBApatch.c).
#include <string.h>

#include <gba_base.h>

#include "../../source/cheat.h"
#include "torii_test.h"
#include "tests.h"

// From source/gba_rts_patch.s: the in-game handler, then the cheat table.
void RTS_ReplaceIRQ_start(void);
void no_CHEAT_end(void);

static void parse(const char *line)
{
	gl_cheat_count = 0;
	Parse_cheat_line(line, strlen(line));
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

	parse("20000,01;");  // a trailing ';' adds no empty entry
	CHECK_EQ(gl_cheat_count, 1);

	parse("123456789AB,1234");  // overlong fields are cut, not overflowed
	CHECK_EQ(gl_cheat_count, 1);
	CHECK_EQ(pCHEAT[0].address, 0x12345678);
	CHECK_EQ(pCHEAT[0].VAL, 0x12);
}

void test_cheat_cap(void)
{
	// 700 one-byte writes: more than fit in the patch area.
	static char line[8 + 3 * 700 + 1];
	char *p = line;
	memcpy(p, "20000,00", 8);
	p += 8;
	for (int i = 1; i < 700; i++, p += 3)
		memcpy(p, ",01", 3);
	*p = 0;
	parse(line);
	CHECK_EQ(gl_cheat_count, CHEAT_MAX);
	CHECK_EQ(pCHEAT[CHEAT_MAX - 1].address, 0x20000 + CHEAT_MAX - 1);

	// The handler and a full table fit in the patch area appended to the ROM.
	u32 handler = (u8 *)no_CHEAT_end - (u8 *)RTS_ReplaceIRQ_start;
	tlog("cheat handler: 0x%lX bytes, room for %lu cheats", handler, (CHEAT_PATCH_LENGTH - handler) / 8);
	CHECK(handler + CHEAT_MAX * 8 <= CHEAT_PATCH_LENGTH);
	gl_cheat_count = 0;
}
