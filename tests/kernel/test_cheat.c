// Cheat parsing and the cheat table's cap (source/cheat.c, source/GBApatch.c).
#include <string.h>

#include <gba_base.h>

#include "../../source/cheat.h"
#include "../../source/GBApatch.h"
#include "torii_test.h"
#include "tests.h"

// From source/gba_rts_patch.s: the in-game handler, then the cheat table.
void RTS_ReplaceIRQ_start(void);
void no_CHEAT_end(void);

extern u8 pReadCache[];

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
	// 2.1 KB: borrows the kernel's read cache, which is free while tests run
	// (IWRAM is short, and every kernel has the tests).
	char *line = (char *)pReadCache + 0x1000;
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

void test_menu_code_fits(void)
{
	// The in-game menu's code, DEBUG entry included, goes in the space
	// SetTrimSize keeps free with SAVESTATE on: past it, a 32 MB ROM's or
	// a 16 MB EEPROM game's patch would run off the end.
	u32 size = (u32)no_CHEAT_end - (u32)RTS_ReplaceIRQ_start;
	u32 debug = (u32)Debug_blob_end - (u32)Debug_blob_start;
	tlog("in-game menu code: %lu bytes, and %lu for DEBUG, of %u", (unsigned long)size, (unsigned long)debug,
	     RTS_PATCH_LENGTH);
	CHECK(size + debug <= RTS_PATCH_LENGTH - 0x100);  // with room to spare
	CHECK_EQ(debug % 4, 0);
}
