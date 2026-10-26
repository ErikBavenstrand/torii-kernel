// The cheat table's room (source/cheat.h, GBApatch.c) and a game's cheat file
// on the card at a launch (source/cheatstore.c).
#include <stdio.h>
#include <string.h>

#include <gba_base.h>

#include "ff.h"
#include "../../source/cheat.h"
#include "../../source/cheatstore.h"
#include "../../source/GBApatch.h"
#include "../../source/engine/stage.h"
#include "torii_test.h"
#include "tests.h"

extern FATFS EZcardFs;

void test_cheat_cap(void)
{
	// The menu's code, the interpreter, a full table and DEBUG's code fit in
	// the patch area appended to the ROM; what is left is v0.8's
	u32 menu = (u8 *)RTS_ReplaceIRQ_end - (u8 *)RTS_ReplaceIRQ_start;
	u32 code = (u8 *)Cheat_blob_end - (u8 *)Cheat_blob_start;
	u32 debug = (u8 *)Debug_blob_end - (u8 *)Debug_blob_start;
	u32 used = menu + code + CHEAT_TABLE_MAX + debug;
	tlog("cheat patch: menu 0x%lX, interpreter 0x%lX, table 0x%X, DEBUG 0x%lX bytes: 0x%lX of 0x%X left", menu,
	     code, CHEAT_TABLE_MAX, debug, CHEAT_PATCH_LENGTH - used, CHEAT_PATCH_LENGTH);
	CHECK(used <= CHEAT_PATCH_LENGTH);
	CHECK_EQ(code % 4, 0);
	CHECK_EQ(menu % 4, 0);
	CHECK_EQ(CHEATFILE_BYTES_MAX + 4, CHEAT_TABLE_MAX);

	// A cheat file's path: the ROM's less its extension, as long as a save's
	char path[SAFEFILE_PATH_MAX];
	CHECK_EQ(cheatstore_path(path, sizeof(path), "/GBA/RPG/Golden Sun.gba"), 36);
	CHECK(!strcmp(path, "/torii/cheats/GBA/RPG/Golden Sun.cht"));
	CHECK(cheatstore_path(path, sizeof(path), "/Hello.gba") > 0 && !strcmp(path, "/torii/cheats/Hello.cht"));
	CHECK(cheatstore_path(path, sizeof(path), "/.hidden") > 0 && !strcmp(path, "/torii/cheats/.hidden.cht"));
	char rom[256];
	memset(rom, 'a', sizeof(rom));
	rom[0] = '/';
	memcpy(rom + 240, ".gba", 5);  // a 244-byte path, near the longest a save takes
	CHECK(cheatstore_path(path, sizeof(path), rom) == 13 + 240 + 4);
	memcpy(rom + 251, ".gba", 5);
	rom[255] = 0;
	CHECK(cheatstore_path(path, sizeof(path), rom) > 0);
}

void test_menu_code_fits(void)
{
	// The in-game menu's code, DEBUG entry included, goes in the space
	// SetTrimSize keeps free with SAVESTATE on: past it, a 32 MB ROM's or
	// a 16 MB EEPROM game's patch would run off the end.
	u32 size = (u32)RTS_ReplaceIRQ_end - (u32)RTS_ReplaceIRQ_start;
	u32 debug = (u32)Debug_blob_end - (u32)Debug_blob_start;
	tlog("in-game menu code: %lu bytes, and %lu for DEBUG, of %u", (unsigned long)size, (unsigned long)debug,
	     RTS_PATCH_LENGTH);
	CHECK(size + debug <= RTS_PATCH_LENGTH - 0x100);  // with room to spare
	CHECK_EQ(debug % 4, 0);
}

#define T_ROM "/T19/Cheat Test.gba"
#define T_CHT "/torii/cheats/T19/Cheat Test.cht"

// Writes text to path through the stage (no EWRAM of its own: every kernel
// links the tests)
static int put(const char *path, const char *text)
{
	FIL f;
	u8 *buf = stage_buffer();
	UINT done = 0;
	u32 len = (u32)strlen(text);
	if (len > STAGE_BYTES)
		return 0;
	memcpy(buf, text, len);
	f_mkdir("/torii");
	f_mkdir("/torii/cheats");
	f_mkdir("/torii/cheats/T19");
	if (f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
		return 0;
	int ok = f_write(&f, buf, len, &done) == FR_OK && done == len;
	return (f_close(&f) == FR_OK) & ok;
}

// On the emulated cart (tests/assets.sh): a cheat file's picks reach the
// interpreter's table at their own addresses and widths, IWRAM, EWRAM and IO
// alike, in file order; the picks are written back and read again
void test_cheatstore(void)
{
	if (!EZcardFs.fs_type && f_mount(&EZcardFs, "", 1) != FR_OK)
		return;
	CHECK(put(T_CHT, "cheats = 4\n"
			 "cheat0_desc = \"EWRAM\"\n"
			 "cheat0_code = \"82001000+BEEF\"\n"
			 "cheat0_enable = true\n"
			 "cheat1_desc = \"Not picked\"\n"
			 "cheat1_code = \"32001010+0001\"\n"
			 "cheat1_enable = false\n"
			 "cheat2_desc = \"IWRAM top and IO\"\n"
			 "cheat2_code = \"03007FF0:AB+04000050:0000\"\n"
			 "cheat2_enable = true\n"
			 "cheat3_desc = \"ROM patch\"\n"
			 "cheat3_code = \"08001000:4770\"\n"
			 "cheat3_enable = true\n"));
	cheatstore_launch(T_ROM, 0);
	CHECK_EQ(gl_cheat_count, 2);  // the ROM patch runs at launch only
	CHECK_EQ(gl_cheat_bytes, 12 + 20 + 4);
	CHECK_EQ(gl_cheat_table[0], 1 | CHEAT_PROG_PLAIN | 8 << 16);
	CHECK_EQ(gl_cheat_table[1], 0x12001000);  // a halfword write
	CHECK_EQ(gl_cheat_table[2], 0xBEEF);
	CHECK_EQ(gl_cheat_table[3], 2 | CHEAT_PROG_PLAIN | 16 << 16);
	CHECK_EQ(gl_cheat_table[4], 0x03007FF0);  // no longer folded into IWRAM's first 32 KB by a mask
	CHECK_EQ(gl_cheat_table[5], 0xAB);
	CHECK_EQ(gl_cheat_table[6], 0x14000050);
	CHECK_EQ(gl_cheat_table[8], 0);
	CHECK_EQ(gl_cheat_patches, 1);

	// The screen's toggle, written back: only the enable lines change
	CHECK(cheatstore_read(T_ROM));
	CHECK(cheatstore_decode(0));
	struct cheatstore *s = cheatstore();
	CHECK(cheatfile_toggle(&s->file, 0));
	CHECK(cheatfile_toggle(&s->file, 1));
	CHECK(cheatstore_write());
	cheatstore_launch(T_ROM, 0);
	CHECK_EQ(gl_cheat_count, 2);
	CHECK_EQ(gl_cheat_table[1], 0x02001010);
	CHECK_EQ(gl_cheat_table[2], 0x01);
	CHECK_EQ(gl_cheat_table[4], 0x03007FF0);
	gl_cheat_count = gl_cheat_bytes = gl_cheat_patches = 0;

	// A cheat of 130 ROM patches: the load takes PSRAM and pReadCache, so the
	// first CHEATSTORE_ROM_PATCHES are kept for it in EWRAM, the rest left out
	// (the text is made in the cheat table's room, free until the launch)
	char *t = (char *)cheatstore()->cheats;
	u32 n = (u32)snprintf(t, 128, "cheat0_desc = \"Patches\"\ncheat0_enable = true\ncheat0_code = \"");
	for (u32 i = 0; i < 130; i++)
		n += (u32)snprintf(t + n, 32, "%s08%06lX:4770", i ? "+" : "", (unsigned long)(0x1000 + i * 2));
	snprintf(t + n, 8, "\"\n");
	CHECK(put(T_CHT, t));
	cheatstore_launch(T_ROM, 0);
	CHECK_EQ(gl_cheat_count, 0);
	CHECK_EQ(gl_cheat_patches, CHEATSTORE_ROM_PATCHES);
	gl_cheat_count = gl_cheat_bytes = gl_cheat_patches = 0;

	// No file: nothing
	f_unlink(T_CHT);
	f_unlink(T_CHT ".bak");
	cheatstore_launch(T_ROM, 0);
	CHECK_EQ(gl_cheat_count, 0);
	CHECK(!cheatstore_read(T_ROM));
	f_unlink("/torii/cheats/T19");
}
