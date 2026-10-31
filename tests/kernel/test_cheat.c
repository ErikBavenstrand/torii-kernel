// The cheat table's room (source/cheat.h, GBApatch.c) and a game's cheat file
// on the card at a launch (source/cheatstore.c).
#include <stdio.h>
#include <string.h>

#include <gba_base.h>

#include "ff.h"
#include "../../source/cheat.h"
#include "../../source/cheatstore.h"
#include "../../source/GBApatch.h"
#include "../../source/ingame_abi.h"
#include "../../source/engine/cycles.h"
#include "../../source/engine/stage.h"
#include "torii_test.h"
#include "tests.h"

extern FATFS EZcardFs;

void test_cheat_cap(void)
{
	// The menu's code, the interpreter and a full table fit in the patch
	// area appended to the ROM; what is left is v0.8's
	u32 menu = (u8 *)RTS_ReplaceIRQ_end - (u8 *)RTS_ReplaceIRQ_start;
	u32 code = (u8 *)Cheat_blob_end - (u8 *)Cheat_blob_start;
	u32 used = menu + code + CHEAT_TABLE_MAX;
	tlog("cheat patch: menu 0x%lX, interpreter 0x%lX, table 0x%X bytes: 0x%lX of 0x%X left", menu, code,
	     CHEAT_TABLE_MAX, CHEAT_PATCH_LENGTH - used, CHEAT_PATCH_LENGTH);
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

// A crt0's System-mode stack set up, as Get_spend_address finds it: mov
// r0,#0x1F; msr cpsr_fc,r0; ldr sp,[pc,#8] (the literal at word 6), and a
// second ldr sp of the same literal's value (a game reloading its top,
// Crash Nitro Kart's case)
static u32 spend_rom(u32 *rom, u32 top)
{
	memset(rom, 0, 32 * 4);
	rom[0] = 0xE3A0001F;
	rom[1] = 0xE129F000;
	rom[2] = 0xE59FD000 | 8;
	rom[6] = top;
	rom[3] = 0xE59FD000 | 20;  // its literal at word 10
	rom[10] = top;
	return Get_spend_address(rom);
}

// The spend area takes 0x80 bytes under the game's stack top, and the
// cheat search's live block 0x20 more only under the BIOS's default top:
// Street Fighter Alpha 3 (top 0x03007EC0) crashed with them (v0.9)
void test_spend_lowering(void)
{
	static u32 rom[32];
	CHECK_EQ(spend_rom(rom, SPEND_LIVE_TOP), SPEND_LIVE_TOP - INGAME_SPEND_BYTES - INGAME_LIVE_BYTES);
	CHECK_EQ(spend_live, 1);
	CHECK_EQ(rom[6], SPEND_LIVE_TOP - INGAME_SPEND_BYTES - INGAME_LIVE_BYTES);
	CHECK_EQ(rom[10], rom[6]);
	CHECK_EQ(spend_rom(rom, 0x03007EC0), 0x03007EC0 - INGAME_SPEND_BYTES);
	CHECK_EQ(spend_live, 0);
	CHECK_EQ(rom[10], 0x03007EC0 - INGAME_SPEND_BYTES);
	CHECK_EQ(spend_rom(rom, 0x0203FFFC), 0x0203FFFC - INGAME_SPEND_BYTES);
	CHECK_EQ(spend_live, 0);
	CHECK_EQ(spend_rom(rom, 0x03007E00), 0);  // a top too low: none
	CHECK_EQ(spend_live, 0);
}

void test_menu_code_fits(void)
{
	// The in-game menu's code goes in the space SetTrimSize keeps free with
	// SAVESTATE on: past it, a 32 MB ROM's or a 16 MB EEPROM game's patch
	// would run off the end. (The DEBUG snapshot is the overlay's since
	// v0.8's cleanup, no longer code appended here.)
	u32 size = (u32)RTS_ReplaceIRQ_end - (u32)RTS_ReplaceIRQ_start;
	tlog("in-game menu code: %lu bytes of %u", (unsigned long)size, RTS_PATCH_LENGTH);
	CHECK(size <= RTS_PATCH_LENGTH);
	// The refusal's colours, as the kernel names them
	static const u16 colours[] = INGAME_REFUSED_COLOURS;
	CHECK(!memcmp((const void *)RTS_Refuse_colours, colours, sizeof(colours)));
}

// PATCH/'s cache (.pat): the words of a file Make_pat_file wrote for a 4 MB
// ROM pass, and a crafted or stale one is turned down, so a launch scans the
// ROM again instead of writing where its words say
// pReadCache (all of it is the unit tests', ez_define.h): the words of a .pat
extern u8 pReadCache[];
#define pat_words ((u32 *)(void *)pReadCache)
#define PAT_WORDS_BYTES ((EMax * 2 + 16) * 4)
static void pat_fill(void)
{
	memset(pat_words, 0, PAT_WORDS_BYTES);
	u32 *w = pat_words + EMax * 2;
	pat_words[0] = 0x1000 / 4, pat_words[1] = 0x3007FF4;  // a patched literal
	pat_words[2] = 0x2000 / 4, pat_words[3] = 0x3007FF4;
	w[5] = 2;  // iCount2
	w[6] = 0x3F0010;  // iTrimSize
	w[7] = 0xEA00002E;  // the entry's branch
	w[12] = RTS_PATCH_LENGTH;
	w[13] = 0x400000;  // the ROM's size and head CRC
	w[14] = 0x12345678;
}
void test_pat_cache(void)
{
	u32 *w = pat_words + EMax * 2;
	pat_fill();
	CHECK(pat_cache_check(pat_words, 0x400000, 0x12345678, RTS_PATCH_LENGTH) == NULL);
	// another ROM: its size, or its first bytes
	CHECK(pat_cache_check(pat_words, 0x400004, 0x12345678, RTS_PATCH_LENGTH) != NULL);
	CHECK(pat_cache_check(pat_words, 0x400000, 0x12345679, RTS_PATCH_LENGTH) != NULL);
	CHECK(pat_cache_check(pat_words, 0x400000, 0, RTS_PATCH_LENGTH) != NULL);  // the ROM didn't read
	w[5] = EMax + 1;
	CHECK(pat_cache_check(pat_words, 0x400000, 0x12345678, RTS_PATCH_LENGTH) != NULL);
	pat_fill();
	w[6] = 0x400000 + RTS_PATCH_LENGTH;  // the patch area past the ROM and its room
	CHECK(pat_cache_check(pat_words, 0x400000, 0x12345678, RTS_PATCH_LENGTH) != NULL);
	w[6] = 0x3F0012;  // not word-aligned
	CHECK(pat_cache_check(pat_words, 0x400000, 0x12345678, RTS_PATCH_LENGTH) != NULL);
	pat_fill();
	w[12] = CHEAT_PATCH_LENGTH + 0x10;  // more room than any launch keeps
	CHECK(pat_cache_check(pat_words, 0x400000, 0x12345678, RTS_PATCH_LENGTH) != NULL);
	pat_fill();
	w[6] = 0x1FFF000, w[12] = 0x2000, w[13] = 0x2000000;  // a 32 MB ROM's area past 32 MB
	CHECK(pat_cache_check(pat_words, 0x2000000, 0x12345678, 0x2000) != NULL);
	w[12] = 0x1000;
	CHECK(pat_cache_check(pat_words, 0x2000000, 0x12345678, 0x1000) == NULL);
	pat_fill();
	pat_words[2] = 0x3F0010 / 4;  // a patched word on the patch area
	CHECK(pat_cache_check(pat_words, 0x400000, 0x12345678, RTS_PATCH_LENGTH) != NULL);
	pat_words[2] = 0xFFFFFFFF;  // and far past the ROM
	CHECK(pat_cache_check(pat_words, 0x400000, 0x12345678, RTS_PATCH_LENGTH) != NULL);
	pat_fill();
	w[5] = 1;  // only the counted slots count
	pat_words[2] = 0xFFFFFFFF;
	CHECK(pat_cache_check(pat_words, 0x400000, 0x12345678, RTS_PATCH_LENGTH) == NULL);
	pat_fill();
	w[7] = 0xEA100000;  // the entry past the ROM's end
	CHECK(pat_cache_check(pat_words, 0x400000, 0x12345678, RTS_PATCH_LENGTH) != NULL);
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

// A file of size bytes at path (junk but its last byte); its FIL off the
// test's own frame
static __attribute__((noinline)) int put_size(const char *path, u32 size)
{
	FIL f;
	UINT done = 0;
	if (f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
		return 0;
	int ok = f_lseek(&f, size - 1) == FR_OK && f_write(&f, "\n", 1, &done) == FR_OK && done == 1;
	return (f_close(&f) == FR_OK) & ok;
}

static __attribute__((noinline)) u32 file_size(const char *path)
{
	FILINFO info;
	return f_stat(path, &info) == FR_OK ? (u32)info.fsize : 0;
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
	// The picks alone (v0.10: no cheat not picked for the in-game list); the
	// ROM patch runs at launch only
	CHECK_EQ(gl_cheat_count, 2);
	CHECK_EQ(gl_cheat_bytes, 12 + 20 + 4);
	CHECK_EQ(gl_cheat_table[0], 1 | CHEAT_PROG_PLAIN | 8 << 16);
	CHECK_EQ(gl_cheat_table[1], 0x12001000);  // a halfword write
	CHECK_EQ(gl_cheat_table[2], 0xBEEF);
	CHECK_EQ(gl_cheat_table[3], 2 | CHEAT_PROG_PLAIN | 16 << 16);
	CHECK_EQ(gl_cheat_table[4], 0x03007FF0);  // no longer folded into IWRAM's first 32 KB by a mask
	CHECK_EQ(gl_cheat_table[5], 0xAB);
	CHECK_EQ(gl_cheat_table[6], 0x14000050);
	CHECK_EQ(gl_cheat_table[8], 0);  // the table's end
	CHECK_EQ(gl_cheat_patches, 1);

	// The screen's toggle, written back: only the enable lines change
	CHECK(cheatstore_read(T_ROM));
	CHECK(cheatstore_decode(0));
	struct cheatstore *s = cheatstore();
	CHECK(cheatfile_toggle(&s->file, 0));
	CHECK(cheatfile_toggle(&s->file, 1));
	CHECK(cheatstore_write());
	cheatstore_launch(T_ROM, 0);
	CHECK_EQ(gl_cheat_count, 2);  // the EWRAM write no longer in
	CHECK_EQ(gl_cheat_table[1], 0x02001010);
	CHECK_EQ(gl_cheat_table[2], 0x01);
	CHECK_EQ(gl_cheat_table[4], 0x03007FF0);
	gl_cheat_count = gl_cheat_bytes = gl_cheat_patches = 0;

	// ROM patches: the load takes PSRAM and pReadCache, so a launch keeps them
	// in cart SRAM, CHEATSTORE_ROM_PATCHES at most, a cheat's all or none:
	// one of 2,048 (a cheat's most), one of 1,500 and one of 400 with a RAM
	// write kept, then one of 200 and a RAM write left out whole (never its
	// write without its patches, or some of them), then 148 kept: 4,096. One
	// of 2,049 is too big. Each is a CodeBreaker slide into the ROM; the text
	// is made in the cheat table's room, free until the launch.
	static const u32 sizes[6] = {2048, 1500, 400, 200, 148, 2049};
	char *t = (char *)cheatstore()->cheats;
	u32 n = 0;
	for (u32 c = 0; c < 6; c++)
		n += (u32)snprintf(t + n, 160,
				   "cheat%lu_enable = true\ncheat%lu_code = \"%s4%07lX 4770+0000%04lX 0002\"\n",
				   (unsigned long)c, (unsigned long)c, c == 2 || c == 3 ? "32001000 0001+" : "",
				   (unsigned long)(0x8010000 + c * 0x10000), (unsigned long)sizes[c]);
	CHECK(put(T_CHT, t));
	cheatstore_launch(T_ROM, 0);
	CHECK_EQ(gl_cheat_count, 1);  // cheat 3's RAM write left out with its patches
	CHECK_EQ(gl_cheat_patches, 4096);
	CHECK_EQ(CHEATSTORE_ROM_PATCHES, 4096);
	// One cheat of the most patches a cheat may have: kept whole
	n = (u32)snprintf(t, 128, "cheat0_enable = true\ncheat0_code = \"");
	for (u32 i = 0; i < CHEATFILE_PATCHES_MAX; i++)
		n += (u32)snprintf(t + n, 32, "%s08%06lX:4770", i ? "+" : "", (unsigned long)(0x1000 + i * 2));
	snprintf(t + n, 8, "\"\n");
	CHECK(put(T_CHT, t));
	u32 start = cycles_now();
	cheatstore_launch(T_ROM, 0);
	tlog("cheat test: a cheat of %u ROM patches read, decoded and kept in %lu cycles", CHEATFILE_PATCHES_MAX,
	     (unsigned long)cycles_since(start));
	CHECK_EQ(gl_cheat_patches, CHEATFILE_PATCHES_MAX);
	gl_cheat_count = gl_cheat_bytes = gl_cheat_patches = 0;

	// The game popup's count: kept for the file as it is on the card (its
	// size and date), never read by the popup
	CHECK(put(T_CHT, "cheat0_desc = \"M\"\ncheat0_code = \"0000ABCD 0007+100F68A4 0007\"\n"
			 "cheat1_code = \"32001010+0001\"\ncheat2_code = \"32001011+0002\"\n"));
	s32 listed;
	CHECK(cheatstore_peek(T_ROM, &listed));
	CHECK_EQ(listed, -1);  // not counted yet
	CHECK_EQ(cheatstore_open(T_ROM), CHEATSTORE_READ);
	CHECK_EQ(cheatstore_listed(), 3);  // every cheat, the master code greyed: known once parsed
	CHECK(cheatstore_decode(0));
	CHECK(cheatstore_peek(T_ROM, &listed));
	CHECK_EQ(listed, 3);
	CHECK(cheatfile_toggle(&cheatstore()->file, 1));
	CHECK(cheatstore_write());  // a write keeps the count for the new copy
	CHECK(cheatstore_peek(T_ROM, &listed));
	CHECK_EQ(listed, 3);
	CHECK(put(T_CHT, "cheat0_code = \"32001010+0001\"\n"));  // changed behind its back
	CHECK(cheatstore_peek(T_ROM, &listed));
	CHECK_EQ(listed, -1);
	char *lng = (char *)stage_buffer();
	memset(lng, 'a', 300);
	lng[0] = '/';
	lng[299] = 0;
	CHECK(!cheatstore_peek(lng, &listed));  // a path too long for a cheat file: no Cheats

	// A failed PSRAM write of the text: the file is never written back from
	// it, and an edit says it failed
	CHECK_EQ(cheatstore_open(T_ROM), CHEATSTORE_READ);
	CHECK(cheatstore_decode(0));
	CHECK(cheatfile_toggle(&cheatstore()->file, 0));
	CHECK(!cheatstore()->room_failed);
	cheatstore_put((char *)0x02000000, "ab", 2);  // off PSRAM: psram_write refuses it
	CHECK(cheatstore()->room_failed);
	CHECK(!cheatstore_write());
	u32 at;
	CHECK_EQ(cheatstore_add("N", "32001012 0003", CHEAT_FMT_AUTO, &at), CHEATFILE_EDIT_FAILED);
	CHECK_EQ(cheatstore_open(T_ROM), CHEATSTORE_READ);  // the card's copy as it was
	CHECK_EQ(cheatstore()->file.count, 1);
	CHECK(!cheatstore()->cheats[0].picked);
	CHECK(!cheatstore()->room_failed);

	// A copy over the largest read is too big, not a card error: left alone
	CHECK(put_size(T_CHT, CHEATSTORE_FILE_MAX + 1));
	f_unlink(T_CHT ".bak");
	CHECK_EQ(cheatstore_open(T_ROM), CHEATSTORE_BIG);
	CHECK(!cheatstore_read(T_ROM));
	cheatstore_launch(T_ROM, 0);
	CHECK_EQ(gl_cheat_patches, 0);
	CHECK_EQ(file_size(T_CHT), CHEATSTORE_FILE_MAX + 1);  // as it was

	// No file: nothing
	f_unlink(T_CHT);
	f_unlink(T_CHT ".bak");
	cheatstore_launch(T_ROM, 0);
	CHECK_EQ(gl_cheat_count, 0);
	CHECK(!cheatstore_read(T_ROM));
	f_unlink("/torii/cheats/T19");
}

// The cheat file's i-th cheat's desc is want
static int desc_is(u32 i, const char *want)
{
	const struct cheatfile *f = &cheatstore()->file;
	const struct cheatfile_cheat *c = &cheatstore()->cheats[i];
	return i < f->count && c->desc != CHEATFILE_NONE && c->desc_len == strlen(want) &&
	       !memcmp(f->text + c->desc, want, c->desc_len);
}

// Settings' Clean up cheats on the emulated cart: greyed cheats go, master
// codes too unless a code after reads otherwise without them, codes typed on
// the cart stay; a file with nothing left is deleted
void test_cheatstore_clean(void)
{
	// A key the encrypted code after it needs: kept with it
	CHECK(put(T_CHT, "cheat0_desc = \"M\"\ncheat0_code = \"930CFE0E61A7+5E4E1F94523A+DB3A64369A43\"\n"
			 "cheat1_desc = \"HP\"\ncheat1_code = \"D86098902576+777F4707DA29\"\n"
			 "cheat2_desc = \"Junk\"\ncheat2_code = \"ZZZZ\"\n"
			 "cheat3_desc = \"Typed\"\ncheat3_code = \"ZZZZ\"\ncheat3_user = true\n"
			 "cheat4_desc = \"Nothing\"\ncheat4_code = \"72001000 0001\"\n"));
	CHECK_EQ(cheatstore_clean(T_CHT), 2);
	CHECK_EQ(cheatstore_open(T_ROM), CHEATSTORE_READ);
	CHECK_EQ(cheatstore()->file.count, 3);
	CHECK(desc_is(0, "M"));
	CHECK(desc_is(1, "HP"));
	CHECK(desc_is(2, "Typed"));
	CHECK_EQ(cheatstore()->cheats[2].number, 2);
	cheatstore_decode(0);
	CHECK_EQ(cheatstore()->cheats[1].status, CHEATFILE_OK);
	CHECK_EQ(cheatstore_clean(T_CHT), 0);  // clean already: left as it is

	// No code needs the master code: it goes too, the picks stay
	CHECK(put(T_CHT, "cheat0_desc = \"M\"\ncheat0_code = \"0000ABCD 0007+100F68A4 0007\"\n"
			 "cheat1_desc = \"A\"\ncheat1_code = \"32001010+0001\"\ncheat1_enable = true\n"
			 "cheat2_desc = \"A again\"\ncheat2_code = \"32001010+0001\"\n"));
	CHECK_EQ(cheatstore_clean(T_CHT), 2);
	CHECK_EQ(cheatstore_open(T_ROM), CHEATSTORE_READ);
	CHECK_EQ(cheatstore()->file.count, 1);
	CHECK(desc_is(0, "A"));
	CHECK_EQ(cheatstore()->cheats[0].number, 0);
	CHECK_EQ(cheatstore()->cheats[0].picked, 1);

	// Nothing left: the file goes
	CHECK(put(T_CHT, "cheat0_desc = \"Junk\"\ncheat0_code = \"ZZZZ\"\n"));
	CHECK_EQ(cheatstore_clean(T_CHT), 1);
	CHECK_EQ(file_size(T_CHT), 0);

	// Every cheat file under /torii/cheats
	CHECK(put(T_CHT, "cheat0_code = \"ZZZZ\"\ncheat1_code = \"32001010+0001\"\n"));
	struct cheatstore_sum sum;
	cheatstore_clean_all(NULL, &sum);
	CHECK(sum.files >= 1);
	CHECK_EQ(sum.removed, 1);
	CHECK_EQ(sum.cleaned, 1);
	CHECK_EQ(sum.failed, 0);
	cheatstore_delete(T_ROM);
}
