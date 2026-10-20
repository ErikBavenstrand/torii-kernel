// The user's lists on the card and in PSRAM (source/userdata_store.c) and
// safe writes (source/safefile.c), on test files beside the user's. They
// run on the emulated cart (tests/assets.sh), not in Diagnostics.
#include <stdio.h>
#include <string.h>

#include <gba_base.h>

#include "../../source/crc32.h"
#include "../../source/engine/stage.h"
#include "ff.h"
#include "../../source/safefile.h"
#include "../../source/userdata.h"
#include "torii_test.h"
#include "tests.h"

#define T_RECENT "/torii/t-recent.txt"
#define T_FAV "/torii/t-fav.txt"
#define T_OLD "/torii/t-old.txt"
#define T_SAFE "/torii/t-safe.txt"

static const char *const test_files[] = {T_RECENT, T_RECENT ".bak", T_FAV, T_FAV ".bak", T_OLD, T_SAFE, T_SAFE ".bak"};

extern FATFS EZcardFs;

// The card is mounted (these tests run on the emulated cart: tests/assets.sh)
static int have_card(void)
{
	if (!EZcardFs.fs_type && f_mount(&EZcardFs, "", 1) != FR_OK)
		return 0;
	return EZcardFs.fs_type != 0;
}

static void remove_files(void)
{
	for (u32 i = 0; i < sizeof(test_files) / sizeof(test_files[0]); i++)
		f_unlink(test_files[i]);
}

// Writes text to path as it is (no CRC line); 1 if it is there
static int put_file(const char *path, const char *text, u32 len)
{
	FIL f;
	UINT done = 0;
	memcpy(stage_buffer(), text, len);
	f_mkdir("/torii");
	if (f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
		return 0;
	int ok = f_write(&f, stage_buffer(), len, &done) == FR_OK && done == len;
	return (f_close(&f) == FR_OK) & ok;
}

// The file at path into the stage, NUL-terminated; its size, or -1 if it isn't there
static int get_file(const char *path)
{
	FIL f;
	UINT done = 0;
	if (f_open(&f, path, FA_READ) != FR_OK)
		return -1;
	int ok = f_read(&f, stage_buffer(), STAGE_BYTES - 1, &done) == FR_OK;
	f_close(&f);
	stage_buffer()[ok ? done : 0] = 0;
	return ok ? (int)done : -1;
}

static int exists(const char *path)
{
	FILINFO info;
	return f_stat(path, &info) == FR_OK;
}

// 1 if both copies of path hold text, the same, and it ends in its CRC line
static int both_copies(const char *path, const char *text)
{
	static char main_copy[1024] EWRAM_BSS;
	char bak[48], line[24];
	int n = get_file(path);
	if (n < 0 || (u32)n >= sizeof(main_copy))
		return 0;
	memcpy(main_copy, stage_buffer(), (u32)n + 1);
	snprintf(bak, sizeof(bak), "%s.bak", path);
	if (get_file(bak) != n || memcmp(main_copy, stage_buffer(), (u32)n) != 0)
		return 0;
	u32 len = (u32)strlen(text);
	safefile_crc_line(line, crc32(text, len));
	if (memcmp(main_copy, text, len) != 0 || strcmp(main_copy + len, line) != 0) {
		tlog_error("%s holds: %s", path, main_copy);
		return 0;
	}
	return 1;
}

void test_safefile(void)
{
	CHECK(have_card());
	if (!have_card())
		return;
	char line[32];
	remove_files();
	CHECK_EQ(safefile_open(T_SAFE, 4096), SAFEFILE_NONE);
	// The main copy: its lines, without the CRC line
	static const char text[] = "one\ntwo\n";
	u32 len = sizeof(text) - 1;
	char sealed[64];
	memcpy(sealed, text, len);
	safefile_crc_line(sealed + len, crc32(text, len));
	CHECK(put_file(T_SAFE, sealed, (u32)strlen(sealed)));
	CHECK_EQ(safefile_open(T_SAFE, 4096), SAFEFILE_MAIN);
	CHECK_EQ(safefile_line(line, sizeof(line)), 3);
	CHECK(strcmp(line, "one") == 0);
	CHECK_EQ(safefile_line(line, sizeof(line)), 3);
	CHECK(strcmp(line, "two") == 0);
	CHECK_EQ(safefile_line(line, sizeof(line)), SAFEFILE_END);
	safefile_close();
	// A write cut short leaves NAME empty: NAME.bak is read
	CHECK(put_file(T_SAFE ".bak", sealed, (u32)strlen(sealed)));
	CHECK(put_file(T_SAFE, "", 0));
	CHECK_EQ(safefile_open(T_SAFE, 4096), SAFEFILE_BAK);
	CHECK_EQ(safefile_line(line, sizeof(line)), 3);
	safefile_close();
	// Zeros (a cut that left a size) are no text either
	CHECK(put_file(T_SAFE, "\0\0\0\0\0\0\0\0", 8));
	CHECK_EQ(safefile_open(T_SAFE, 4096), SAFEFILE_BAK);
	safefile_close();
	// Edited by hand: a BOM, no CRC line, CRLF, a line too long for the buffer
	CHECK(put_file(T_SAFE, "\xEF\xBB\xBFmine\r\nan overlong line here\r\nlast", 36));
	CHECK_EQ(safefile_open(T_SAFE, 35), SAFEFILE_BAK);  // over the size given: as missing
	safefile_close();
	CHECK_EQ(safefile_open(T_SAFE, 4096), SAFEFILE_HAND);
	CHECK_EQ(safefile_line(line, sizeof(line)), 4);
	CHECK(strcmp(line, "mine") == 0);
	CHECK_EQ(safefile_line(line, 8), SAFEFILE_LONG);
	CHECK_EQ(safefile_line(line, sizeof(line)), 4);
	CHECK(strcmp(line, "last") == 0);
	CHECK_EQ(safefile_line(line, sizeof(line)), SAFEFILE_END);
	safefile_close();
	remove_files();
}

void test_userdata_recent_store(void)
{
	CHECK(have_card());
	if (!have_card())
		return;
	remove_files();
	userdata_test_paths(T_RECENT, T_FAV, T_OLD);
	CHECK_EQ(userdata_recent_count(), 0);
	CHECK(strcmp(userdata_recent_path(0), "") == 0);
	CHECK(!exists(T_RECENT));
	// The v0.3 list is read while there is no recent.txt; its bad lines skipped
	static const char old[] = "/a.gba\nNOR:b.gba\nnot a path\n/a.gba\n/Games/c.gb\n";
	CHECK(put_file(T_OLD, old, sizeof(old) - 1));
	userdata_invalidate();
	CHECK_EQ(userdata_recent_count(), 3);
	CHECK(strcmp(userdata_recent_path(0), "/a.gba") == 0);
	CHECK(strcmp(userdata_recent_path(1), "NOR:b.gba") == 0);
	CHECK(strcmp(userdata_recent_path(2), "/Games/c.gb") == 0);
	CHECK_EQ(userdata_recent_time(1), 0);
	CHECK(!exists(T_RECENT) && exists(T_OLD));  // reading it writes nothing
	// The first write takes it over: both copies, the old list gone
	CHECK(userdata_recent_add_file("/Games", "c.gb"));
	CHECK_EQ(userdata_recent_count(), 3);
	CHECK(strcmp(userdata_recent_path(0), "/Games/c.gb") == 0);
	u32 now = userdata_recent_time(0);
	CHECK(now == 0 || now >= USERDATA_CLOCK_FROM);
	CHECK(!exists(T_OLD));
	char want[160];
	snprintf(want, sizeof(want), "%lu\t/Games/c.gb\n0\t/a.gba\n0\tNOR:b.gba\n", (unsigned long)now);
	CHECK(both_copies(T_RECENT, want));
	// Read back from the card after a launch: the same
	userdata_invalidate();
	CHECK_EQ(userdata_recent_count(), 3);
	CHECK(strcmp(userdata_recent_path(2), "NOR:b.gba") == 0);
	CHECK_EQ(userdata_recent_time(0), now);
	// The boot position, written with the list
	CHECK(strcmp(userdata_boot_get()->item, "") == 0);
	userdata_boot_set("gba", 12);
	userdata_boot_set("not a word", 1);  // refused
	userdata_invalidate();  // a place not yet written survives a read
	CHECK(strcmp(userdata_boot_get()->item, "gba") == 0 && userdata_boot_get()->row == 12);
	CHECK(userdata_flush());
	snprintf(want, sizeof(want), "@boot gba 12\n%lu\t/Games/c.gb\n0\t/a.gba\n0\tNOR:b.gba\n", (unsigned long)now);
	CHECK(both_copies(T_RECENT, want));
	userdata_test_paths(T_RECENT, T_FAV, T_OLD);  // forgets the place: read it back
	CHECK(strcmp(userdata_boot_get()->item, "gba") == 0 && userdata_boot_get()->row == 12);
	// Forgetting: a file, a NOR game, every NOR game
	CHECK(userdata_forget("/a.gba"));
	CHECK_EQ(userdata_recent_count(), 2);
	CHECK(userdata_recent_add("NOR:d.gba"));
	CHECK(userdata_forget_nor("b.gba"));
	CHECK_EQ(userdata_recent_count(), 2);
	CHECK(strcmp(userdata_recent_path(0), "NOR:d.gba") == 0);
	CHECK(userdata_forget_nor(NULL));
	CHECK_EQ(userdata_recent_count(), 1);
	CHECK(strcmp(userdata_recent_path(0), "/Games/c.gb") == 0);
	// A rename follows the file
	CHECK(userdata_rename("/Games/c.gb", "/Games/d.gb"));
	CHECK(strcmp(userdata_recent_path(0), "/Games/d.gb") == 0);
	CHECK_EQ(userdata_recent_time(0), now);
	// The cap
	char path[24];
	for (u32 i = 0; i < USERDATA_RECENT_MAX + 2; i++) {
		snprintf(path, sizeof(path), "/g%02lu.gba", (unsigned long)i);
		userdata_recent_add(path);
	}
	CHECK_EQ(userdata_recent_count(), USERDATA_RECENT_MAX);
	userdata_invalidate();
	CHECK_EQ(userdata_recent_count(), USERDATA_RECENT_MAX);
	CHECK(strcmp(userdata_recent_path(0), "/g33.gba") == 0);
	CHECK(strcmp(userdata_recent_path(USERDATA_RECENT_MAX - 1), "/g02.gba") == 0);
	// Paths the launch path can't take are refused
	CHECK(!userdata_recent_add("C:/x.gba"));
	// A read that fails (here a name FatFs refuses, as a card error would
	// fail it) leaves a list that is never written back, and the v0.3 list
	// isn't taken in its place
	static const char old_list[] = "/a.gba\n";
	CHECK(put_file(T_OLD, old_list, sizeof(old_list) - 1));
	userdata_test_paths("/torii/t-bad?.txt", "/torii/t-bad?.fav", T_OLD);
	CHECK_EQ(userdata_recent_count(), 0);
	CHECK(!userdata_recent_add("/b.gba"));
	CHECK(!userdata_fav_set("/b.gba", 1));
	CHECK(exists(T_OLD));
	userdata_test_paths(NULL, NULL, NULL);
	remove_files();
}

void test_userdata_fav_store(void)
{
	CHECK(have_card());
	if (!have_card())
		return;
	remove_files();
	userdata_test_paths(T_RECENT, T_FAV, T_OLD);
	CHECK_EQ(userdata_fav_count(), 0);
	CHECK(userdata_fav_set("/z.gba", 1));
	CHECK(userdata_fav_set("/a.gba", 1));
	CHECK(userdata_fav_set("NOR:n.gba", 1));
	CHECK(userdata_fav_set("/a.gba", 1));  // twice is once
	CHECK_EQ(userdata_fav_count(), 3);
	CHECK(strcmp(userdata_fav_path(0), "/a.gba") == 0);
	CHECK(strcmp(userdata_fav_path(1), "/z.gba") == 0);
	CHECK(strcmp(userdata_fav_path(2), "NOR:n.gba") == 0);
	CHECK(both_copies(T_FAV, "/a.gba\n/z.gba\nNOR:n.gba\n"));
	CHECK(userdata_fav_has("/z.gba") && !userdata_fav_has("/y.gba"));
	// Off, a rename, a NOR game gone, a deleted file
	CHECK(userdata_fav_set("/z.gba", 0));
	CHECK(!userdata_fav_has("/z.gba"));
	userdata_invalidate();  // the rename reads the favourites itself
	CHECK(userdata_rename("/a.gba", "/b.gba"));
	CHECK_EQ(userdata_fav_count(), 2);
	CHECK(userdata_fav_has("/b.gba") && !userdata_fav_has("/a.gba") && userdata_fav_has("NOR:n.gba"));
	userdata_invalidate();
	CHECK(userdata_rename("/x.gba", "/y.gba"));  // not a favourite: nothing changes
	CHECK_EQ(userdata_fav_count(), 2);
	CHECK(!userdata_fav_has("/y.gba"));
	CHECK(userdata_forget_nor(NULL));
	CHECK_EQ(userdata_fav_count(), 1);
	CHECK(userdata_fav_set(userdata_fav_path(0), 0));  // a path from the store itself
	CHECK_EQ(userdata_fav_count(), 0);
	CHECK(both_copies(T_FAV, ""));
	// Edited by hand: out of order, twice, blanks; read in order, and the
	// next write puts its CRC line on
	static const char hand[] = "/c.gba\n\n  /a.gba\r\n# mine\n/c.gba\nbad line\n";
	CHECK(put_file(T_FAV, hand, sizeof(hand) - 1));
	userdata_invalidate();
	userdata_load();
	CHECK_EQ(userdata_fav_count(), 2);
	CHECK(strcmp(userdata_fav_path(0), "/a.gba") == 0 && strcmp(userdata_fav_path(1), "/c.gba") == 0);
	CHECK(userdata_forget("/c.gba"));
	CHECK(both_copies(T_FAV, "/a.gba\n"));
	userdata_test_paths(NULL, NULL, NULL);
	remove_files();
}
