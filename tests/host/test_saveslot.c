// Slot names, paths, history, the rename journal and sanity checks
// (source/saveslot.c). Buffers are heap copies of exactly their size, so
// AddressSanitizer catches a write or read past either.
#include <stdlib.h>
#include <string.h>

#include "../../source/saveslot.h"
#include "../kernel/torii_test.h"
#include "tests.h"

static void check_clean(const char *in, const char *expected)
{
	char out[SAVESLOT_NAME_BYTES];
	u32 n = saveslot_clean_name(out, in);
	CHECK(strcmp(out, expected) == 0);
	CHECK_EQ(n, strlen(expected));
	// Idempotent
	char again[SAVESLOT_NAME_BYTES];
	saveslot_clean_name(again, out);
	CHECK(strcmp(again, out) == 0);
}

void test_saveslot_clean(void)
{
	check_clean("Main", "Main");
	check_clean("  Boss  fight  ", "Boss  fight");
	check_clean("a\\b/c:d*e?f\"g<h>i|j", "abcdefghij");
	check_clean("...hidden.", "hidden");
	check_clean(". . ", "");
	check_clean("", "");
	check_clean("///", "");
	check_clean("tab\there\x7f!", "tabhere!");
	check_clean("Caf\xC3\xA9", "Caf\xC3\xA9");
	// A C1 control (U+0085), a lone continuation byte, a cut sequence, an
	// overlong slash and a surrogate go
	check_clean("a\xC2\x85"
		    "b\x80"
		    "c\xE3\x81"
		    "d\xC0\xAF"
		    "e\xED\xA0\x80"
		    "f",
		    "abcdef");
	// 24 characters, not bytes
	check_clean("abcdefghijklmnopqrstuvwxyz", "abcdefghijklmnopqrstuvwx");
	check_clean("\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82"
		    "\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82"
		    "\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82"
		    "\xE3\x81\x82\xE3\x81\x82",
		    "\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82"
		    "\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82"
		    "\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82");
	// Four-byte characters at the limit fill SAVESLOT_NAME_BYTES - 1 bytes
	char emoji[(SAVESLOT_NAME_CHARS + 2) * 4 + 1] = "";
	for (int i = 0; i < SAVESLOT_NAME_CHARS + 2; i++)
		strcat(emoji, "\xF0\x9F\x98\x80");
	char out[SAVESLOT_NAME_BYTES];
	CHECK_EQ(saveslot_clean_name(out, emoji), SAVESLOT_NAME_BYTES - 1);
	// Spaces that fall after the cut are trimmed with it
	check_clean("abcdefghijklmnopqrstuvw      xyz", "abcdefghijklmnopqrstuvw");
}

void test_saveslot_names(void)
{
	CHECK(saveslot_names_equal("Main", "mAIN"));
	CHECK(saveslot_names_equal("\xC3\x89t\xC3\xA9", "\xC3\xA9T\xC3\x89"));
	CHECK(!saveslot_names_equal("Main", "Main2"));
	CHECK(!saveslot_names_equal("Main2", "Main"));
	CHECK(!saveslot_names_equal("\xC3\x97", "\xC3\xB7"));
	// Other scripts compare exactly, and do not all match each other
	CHECK(!saveslot_names_equal("\xE3\x81\x82", "\xE3\x81\x84"));
	CHECK(saveslot_names_equal("\xE3\x81\x82", "\xE3\x81\x82"));
	CHECK(!saveslot_names_equal("\x80", "\x81"));
	const char *list[] = {"Main", "Boss", "\xC3\x89t\xC3\xA9"};
	CHECK(saveslot_name_taken("boss", list, 3, 99));
	CHECK(saveslot_name_taken("\xC3\xA9t\xC3\xA9", list, 3, 99));
	CHECK(!saveslot_name_taken("Boss2", list, 3, 99));
	// A slot may be renamed to its own name in another case
	CHECK(!saveslot_name_taken("BOSS", list, 3, 1));
	CHECK(saveslot_name_taken("BOSS", list, 3, 0));
	CHECK(!saveslot_name_taken("x", list, 0, 0));
}

static void check_path(int mode, const char *rom, const char *slot, const char *expected)
{
	char out[128];
	int n = saveslot_save_path(out, sizeof(out), mode, rom, slot);
	CHECK_EQ(n, (int)strlen(expected));
	CHECK(strcmp(out, expected) == 0);
}

void test_saveslot_paths(void)
{
	check_path(SAVESLOT_CENTRAL, "/GBA/Advance Wars.gba", "Main", "/torii/saves/GBA/Advance Wars.gba/Main.sav");
	check_path(SAVESLOT_BESIDE_ROM, "/GBA/Advance Wars.gba", "Main", "/GBA/Advance Wars.Main.sav");
	check_path(SAVESLOT_BESIDE_ROM, "/Game.gba", "Main", "/Game.Main.sav");
	check_path(SAVESLOT_BESIDE_ROM, "/GBA/.hidden", "Main", "/GBA/.hidden.Main.sav");
	check_path(SAVESLOT_BESIDE_ROM, "/GBA/noext", "Main", "/GBA/noext.Main.sav");
	check_path(SAVESLOT_BESIDE_ROM, "/a.b/Game.v1.gba", "Boss", "/a.b/Game.v1.Boss.sav");

	char out[128];
	CHECK_EQ(saveslot_backup_dir(out, sizeof(out), "/GBA/G.gba", "Main"), 29);
	CHECK(strcmp(out, "/torii/backups/GBA/G.gba/Main") == 0);
	// The history folder of a save file: the same folder, from its path
	char save[128];
	CHECK(saveslot_save_path(save, sizeof(save), SAVESLOT_CENTRAL, "/GBA/G.v1.gba", "Boss Rush") > 0);
	CHECK_EQ(saveslot_backup_dir_of(out, sizeof(out), save), 37);
	CHECK(strcmp(out, "/torii/backups/GBA/G.v1.gba/Boss Rush") == 0);
	CHECK(saveslot_backup_dir_of(out, sizeof(out), "/torii/saves/torii/nor/Game/Main.sav") > 0);
	CHECK(strcmp(out, "/torii/backups/torii/nor/Game/Main") == 0);
	CHECK_EQ(saveslot_backup_dir_of(out, sizeof(out), "/torii/saves/G.sav"), -1);       // no ROM folder
	CHECK_EQ(saveslot_backup_dir_of(out, sizeof(out), "/torii/saves/G.gba/.sav"), -1);  // no slot
	CHECK_EQ(saveslot_backup_dir_of(out, sizeof(out), "/torii/saves/../G.gba/M.sav"), -1);
	CHECK_EQ(saveslot_backup_dir_of(out, sizeof(out), "/torii/savesX/G.gba/M.sav"), -1);
	CHECK_EQ(saveslot_backup_dir_of(out, sizeof(out), "/torii/saves/G.gba/M.sav.tmp"), -1);
	CHECK_EQ(saveslot_backup_dir_of(out, sizeof(out), "/SAVER/G.sav"), -1);
	CHECK_EQ(saveslot_backup_dir_of(out, 20, save), -1);
	CHECK_EQ(out[0], 0);
	CHECK_EQ(saveslot_game_dir(out, sizeof(out), "/GBA/G.gba"), 22);
	CHECK(strcmp(out, "/torii/saves/GBA/G.gba") == 0);
	struct saveslot_stamp when = {2026, 10, 22, 9, 5, 7};
	CHECK(saveslot_trash_path(out, sizeof(out), "/GBA/G.gba", "Main", &when, 3) > 0);
	CHECK(strcmp(out, "/torii/trash/20261022-090507-3/GBA/G.gba/Main.sav") == 0);
	CHECK_EQ(saveslot_trash_path(out, sizeof(out), "/GBA/G.gba", "Main", &when, 0), -1);

	// Bad arguments
	CHECK_EQ(saveslot_save_path(out, sizeof(out), 0, "GBA/G.gba", "Main"), -1);
	CHECK_EQ(saveslot_save_path(out, sizeof(out), 0, "/GBA/../G.gba", "Main"), -1);
	CHECK_EQ(saveslot_save_path(out, sizeof(out), 0, "/GBA//G.gba", "Main"), -1);
	CHECK_EQ(saveslot_save_path(out, sizeof(out), 0, "/", "Main"), -1);
	CHECK_EQ(saveslot_save_path(out, sizeof(out), 0, "/G.gba", ""), -1);
	CHECK_EQ(saveslot_save_path(out, sizeof(out), 0, "/G.gba", ".."), -1);
	CHECK_EQ(saveslot_save_path(out, sizeof(out), 0, "/G.gba", "a/b"), -1);
	CHECK_EQ(saveslot_backup_dir(out, sizeof(out), "/G.gba", ""), -1);
	CHECK_EQ(saveslot_game_dir(out, sizeof(out), "G.gba"), -1);
	CHECK_EQ(out[0], 0);
}

void test_saveslot_path_bounds(void)
{
	// The exact fit works, one byte less truncates (out left empty), at every size
	const char *rom = "/GBA/Game.gba";
	const char *expected = "/torii/saves/GBA/Game.gba/Main.sav";
	u32 need = (u32)strlen(expected) + 1;
	for (u32 size = 0; size <= need + 2; size++) {
		char *out = malloc(size ? size : 1);
		int n = saveslot_save_path(out, size, SAVESLOT_CENTRAL, rom, "Main");
		if (size >= need) {
			CHECK_EQ(n, (int)strlen(expected));
			CHECK(strcmp(out, expected) == 0);
		} else {
			CHECK_EQ(n, -1);
			if (size)
				CHECK_EQ(out[0], 0);
		}
		n = saveslot_save_path(out, size, SAVESLOT_BESIDE_ROM, rom, "Main");
		CHECK((n == -1) == (size < strlen("/GBA/Game.Main.sav") + 1));
		n = saveslot_backup_dir(out, size, rom, "Main");
		CHECK((n == -1) == (size < strlen("/torii/backups/GBA/Game.gba/Main") + 1));
		free(out);
	}
}

void test_saveslot_longest(void)
{
	// The pending-save record (ezkernel.h's SAV_INFO_NAME_MAX) holds 251 bytes
	enum {
		RECORD_NAME_MAX = 0x80 * 2 - 4 - 1
	};
	u32 longest = saveslot_longest_save_path(SAVESLOT_ROM_PATH_MAX);
	CHECK_EQ(longest, 12 + 255 + 1 + 96 + 4);
	// So a central path can outgrow the record: 368 bytes against 251
	CHECK(longest > RECORD_NAME_MAX);
	// Build it and check the formula against the builder
	char rom[SAVESLOT_ROM_PATH_MAX + 1], slot[SAVESLOT_NAME_BYTES], *out = malloc(512);
	memset(rom, 'r', SAVESLOT_ROM_PATH_MAX);
	rom[0] = '/';
	rom[SAVESLOT_ROM_PATH_MAX] = 0;
	memset(slot, 's', SAVESLOT_NAME_BYTES - 1);
	slot[SAVESLOT_NAME_BYTES - 1] = 0;
	CHECK_EQ(saveslot_save_path(out, 512, SAVESLOT_CENTRAL, rom, slot), (int)longest);
	// What fits the record: a ROM path of 210 bytes with a 24-letter slot
	CHECK_EQ(saveslot_max_rom_path_for(RECORD_NAME_MAX, SAVESLOT_NAME_CHARS), 210);
	CHECK_EQ(saveslot_max_rom_path_for(10, 4), 0);
	free(out);
}

static void check_backup(const char *name, int valid, u32 n)
{
	u32 got = 0;
	struct saveslot_stamp when;
	int ok = saveslot_parse_backup_name(name, &got, &when);
	CHECK_EQ(ok, valid);
	if (valid)
		CHECK_EQ(got, n);
}

void test_saveslot_backup_names(void)
{
	char out[SAVESLOT_BACKUP_NAME_BYTES];
	struct saveslot_stamp when = {2026, 10, 22, 13, 4, 59};
	CHECK_EQ(saveslot_backup_name(out, 7, &when), 24);
	CHECK(strcmp(out, "0007-20261022-130459.sav") == 0);
	CHECK(saveslot_backup_name(out, 123456, &when) > 0);
	CHECK(strcmp(out, "123456-20261022-130459.sav") == 0);
	CHECK_EQ(saveslot_backup_name(out, 0, &when), 0);
	CHECK_EQ(saveslot_backup_name(out, SAVESLOT_COUNTER_MAX + 1, &when), 0);
	CHECK(saveslot_backup_name(out, SAVESLOT_COUNTER_MAX, &when) > 0);
	CHECK(strlen(out) < SAVESLOT_BACKUP_NAME_BYTES);

	struct saveslot_stamp unset = {0, 0, 0, 0, 0, 0}, back;
	saveslot_backup_name(out, 12, &unset);
	CHECK(strcmp(out, "0012-00000000-000000.sav") == 0);
	u32 n;
	CHECK(saveslot_parse_backup_name(out, &n, &back));
	CHECK_EQ(n, 12);
	CHECK_EQ(back.year, 0);
	saveslot_backup_name(out, 9, &when);
	CHECK(saveslot_parse_backup_name(out, &n, &back));
	CHECK(back.year == 2026 && back.month == 10 && back.day == 22 && back.hour == 13 && back.min == 4 &&
	      back.sec == 59);

	// A kept backup: the mark before .sav; it parses, and pruning skips it
	CHECK_EQ(saveslot_backup_name_kept(out, 7, &when), 29);
	CHECK(strcmp(out, "0007-20261022-130459-kept.sav") == 0);
	CHECK(saveslot_backup_kept(out));
	CHECK(saveslot_parse_backup_name(out, &n, &back) && n == 7);
	CHECK(saveslot_backup_name_kept(out, SAVESLOT_COUNTER_MAX, &when) > 0);
	CHECK(strlen(out) < SAVESLOT_BACKUP_NAME_BYTES);
	CHECK(!saveslot_backup_kept("0007-20261022-130459.sav"));
	check_backup("0007-20261022-130459-kep.sav", 0, 0);
	check_backup("0007-20261022-130459kept.sav", 0, 0);
	{
		const char *mixed[] = {"0001-20261022-130459-kept.sav", "0002-20261022-130459.sav",
				       "0003-20261022-130459.sav", "0004-20261022-130459.sav"};
		u32 idx[4];
		CHECK_EQ(saveslot_prune(mixed, 4, 1, idx), 2);  // 2 and 3 go; 1 is kept
		CHECK_EQ(idx[0], 1);
		CHECK_EQ(idx[1], 2);
		CHECK_EQ(saveslot_prune(mixed, 4, 0, idx), 3);
	}

	check_backup("0001-20261022-130459.sav", 1, 1);
	check_backup("000123456-20261022-130459.sav", 1, 123456);
	check_backup("001-20261022-130459.sav", 0, 0);       // fewer than 4 digits
	check_backup("0000-20261022-130459.sav", 0, 0);      // counter 0
	check_backup("1234567890-20261022-130459.sav", 0, 0); // too many digits
	check_backup("0001-20261322-130459.sav", 0, 0);      // month 13
	check_backup("0001-20261022-250459.sav", 0, 0);
	check_backup("0001-20261022-130459.SAV", 0, 0);
	check_backup("0001-20261022-130459.sav ", 0, 0);
	check_backup("0001-20261022-13045.sav", 0, 0);
	check_backup("0001-2026102x-130459.sav", 0, 0);
	check_backup("0001_20261022-130459.sav", 0, 0);
	check_backup("20261022-130459.sav", 0, 0);
	check_backup("", 0, 0);
	check_backup("Main.sav", 0, 0);
}

void test_saveslot_history(void)
{
	const char *none[] = {"junk", "Main.sav"};
	CHECK_EQ(saveslot_next_counter(none, 0), 1);
	CHECK_EQ(saveslot_next_counter(none, 2), 1);

	// A clock that jumped back, stood still or was never set: the counter orders
	const char *names[] = {
		"0003-20260101-000000.sav", // newest by n, oldest by clock
		"0001-20991231-235959.sav",     "0002-00000000-000000.sav", "notes.txt", "0004-bad.sav",
		"0005-20261022-130459.sav", // newest
		"0005-20261022-130459.sav.tmp",
	};
	CHECK_EQ(saveslot_next_counter(names, 7), 6);
	u32 out[7];
	CHECK_EQ(saveslot_prune(names, 7, 5, out), 0);
	CHECK_EQ(saveslot_prune(names, 7, 4, out), 0);
	CHECK_EQ(saveslot_prune(names, 7, 3, out), 1);
	CHECK_EQ(out[0], 1);
	CHECK_EQ(saveslot_prune(names, 7, 1, out), 3);
	CHECK_EQ(out[0], 1);
	CHECK_EQ(out[1], 2);
	CHECK_EQ(out[2], 0);
	CHECK_EQ(saveslot_prune(names, 7, 0, out), 4);
	CHECK_EQ(out[3], 5);

	// Equal counters (a copied file): the name breaks the tie, none vanishes unasked
	const char *twins[] = {"0002-20260102-000000.sav", "0002-20260101-000000.sav", "0001-20260103-000000.sav"};
	CHECK_EQ(saveslot_prune(twins, 3, 1, out), 2);
	CHECK_EQ(out[0], 2);
	CHECK_EQ(out[1], 1);
	CHECK_EQ(saveslot_next_counter(twins, 3), 3);

	// The counter running out
	const char *last[] = {"999999999-20260101-000000.sav"};
	CHECK_EQ(saveslot_next_counter(last, 1), 0);
}

void test_saveslot_history_random(void)
{
	// Any order of names prunes the same ones, and the survivors are the newest
	srand(7);
	for (int round = 0; round < 200; round++) {
		u32 count = 1 + (u32)rand() % 12;
		char store[12][SAVESLOT_BACKUP_NAME_BYTES];
		const char *names[12];
		u32 counters[12];
		for (u32 i = 0; i < count; i++) {
			struct saveslot_stamp t = {(u16)(rand() % 3 ? 2000 + rand() % 100 : 0), 1, 1, 0, 0, 0};
			if (t.year == 0)
				t.month = t.day = 0;
			counters[i] = i + 1;
			saveslot_backup_name(store[i], counters[i], &t);
			names[i] = store[i];
		}
		for (u32 i = count; i > 1; i--) {
			u32 j = (u32)rand() % i;
			const char *tmp = names[i - 1];
			names[i - 1] = names[j];
			names[j] = tmp;
		}
		u32 keep = (u32)rand() % 14, out[12];
		u32 pruned = saveslot_prune(names, count, keep, out);
		CHECK_EQ(pruned, count > keep ? count - keep : 0);
		for (u32 k = 0; k < pruned; k++) {
			u32 n;
			saveslot_parse_backup_name(names[out[k]], &n, NULL);
			CHECK_EQ(n, k + 1);
		}
		CHECK_EQ(saveslot_next_counter(names, count), count + 1);
	}
}

void test_saveslot_trash_expiry(void)
{
	struct saveslot_stamp then = {2026, 10, 1, 12, 0, 0};
	struct saveslot_stamp now = {2026, 10, 31, 12, 0, 0};
	CHECK(!saveslot_trash_expired(&then, &now));
	now.sec = 1;
	CHECK(saveslot_trash_expired(&then, &now));
	now = (struct saveslot_stamp){2027, 1, 1, 0, 0, 0};
	CHECK(saveslot_trash_expired(&then, &now));
	// Across a leap day (30 days exactly is not yet expired)
	struct saveslot_stamp feb = {2028, 2, 20, 0, 0, 0}, mar = {2028, 3, 20, 0, 0, 0};
	CHECK(!saveslot_trash_expired(&feb, &mar)); // 29 days
	mar.day = 22;
	CHECK(saveslot_trash_expired(&feb, &mar));
	// A clock that is unset, or behind, empties nothing
	struct saveslot_stamp unset = {0, 0, 0, 0, 0, 0}, past = {2000, 1, 1, 0, 0, 0};
	CHECK(!saveslot_trash_expired(&then, &unset));
	CHECK(!saveslot_trash_expired(&unset, &now));
	CHECK(!saveslot_trash_expired(&then, &past));
	// What the purge does: an untrusted clock touches nothing; a folder dated
	// by an unset clock or one ahead takes now's date
	struct saveslot_stamp wild = {2099, 1, 1, 0, 0, 0}, ahead = {2027, 3, 1, 0, 0, 0};
	CHECK_EQ(saveslot_trash_state(&then, &unset), SAVESLOT_TRASH_KEEP);
	CHECK_EQ(saveslot_trash_state(&then, &wild), SAVESLOT_TRASH_KEEP);
	CHECK_EQ(saveslot_trash_state(&unset, &now), SAVESLOT_TRASH_REDATE);
	CHECK_EQ(saveslot_trash_state(&ahead, &now), SAVESLOT_TRASH_REDATE);
	CHECK_EQ(saveslot_trash_state(&now, &now), SAVESLOT_TRASH_KEEP);
	CHECK(!saveslot_clock_plausible(&unset) && !saveslot_clock_plausible(&wild) && saveslot_clock_plausible(&now));
}

void test_saveslot_journal(void)
{
	char buf[SAVESLOT_JOURNAL_BYTES], old_name[SAVESLOT_NAME_BYTES], new_name[SAVESLOT_NAME_BYTES];
	u32 len = saveslot_journal_write(buf, sizeof(buf), "Boss", "Fin\xC3\xA9");
	CHECK(len > 0);
	CHECK_EQ(len, strlen(buf));
	CHECK(saveslot_journal_read(buf, len, old_name, new_name));
	CHECK(strcmp(old_name, "Boss") == 0);
	CHECK(strcmp(new_name, "Fin\xC3\xA9") == 0);

	// Longest names fit the journal
	char big_a[SAVESLOT_NAME_BYTES], big_b[SAVESLOT_NAME_BYTES];
	memset(big_a, 'a', SAVESLOT_NAME_CHARS);
	memset(big_b, 'b', SAVESLOT_NAME_CHARS);
	big_a[SAVESLOT_NAME_CHARS] = big_b[SAVESLOT_NAME_CHARS] = 0;
	len = saveslot_journal_write(buf, sizeof(buf), big_a, big_b);
	CHECK(len > 0);
	CHECK(saveslot_journal_read(buf, len, old_name, new_name));

	// Names that are not clean are refused on both sides
	CHECK_EQ(saveslot_journal_write(buf, sizeof(buf), "a/b", "c"), 0);
	CHECK_EQ(saveslot_journal_write(buf, sizeof(buf), "a", ""), 0);
	CHECK_EQ(saveslot_journal_write(buf, sizeof(buf), "a", " b"), 0);

	// A cut at any length, and any flipped byte, is refused (the file may also be
	// a prefix that is only missing its last newline)
	len = saveslot_journal_write(buf, sizeof(buf), "Boss", "Final");
	char *copy = malloc(len);
	for (u32 cut = 0; cut < len; cut++) {
		memcpy(copy, buf, cut);
		CHECK(!saveslot_journal_read(copy, cut, old_name, new_name));
	}
	for (u32 i = 0; i < len; i++) {
		memcpy(copy, buf, len);
		copy[i] ^= 0x01;
		CHECK(!saveslot_journal_read(copy, len, old_name, new_name));
	}
	free(copy);
	// Out too small, at every size
	for (u32 size = 0; size < len + 1; size++) {
		char *small = malloc(size ? size : 1);
		CHECK_EQ(saveslot_journal_write(small, size, "Boss", "Final"), size > len ? len : 0);
		free(small);
	}
	CHECK(!saveslot_journal_read("", 0, old_name, new_name));
	CHECK(!saveslot_journal_read("garbage\nmore\nlines\nhere\n", 24, old_name, new_name));
}

// The two renames, as a set of the four files
static u32 files_after(u32 exists, enum saveslot_recovery action)
{
	if (action == SAVESLOT_RECOVER_FINISH) {
		exists = (exists & ~SAVESLOT_OLD_HIST) | SAVESLOT_NEW_HIST;
	} else if (action == SAVESLOT_RECOVER_UNDO) {
		exists = (exists & ~SAVESLOT_NEW_SAVE) | SAVESLOT_OLD_SAVE;
	}
	return exists;
}

void test_saveslot_recovery(void)
{
	// All 16 states: one recovery action settles each, and a second finds nothing to do
	for (u32 exists = 0; exists < 16; exists++) {
		enum saveslot_recovery first = saveslot_recover(exists);
		u32 settled = files_after(exists, first);
		CHECK_EQ(saveslot_recover(settled), SAVESLOT_RECOVER_NOTHING);
		// Nothing is lost: a file present before is present (or renamed) after
		CHECK_EQ(!!(exists & (SAVESLOT_OLD_SAVE | SAVESLOT_NEW_SAVE)),
			 !!(settled & (SAVESLOT_OLD_SAVE | SAVESLOT_NEW_SAVE)));
		CHECK_EQ(!!(exists & (SAVESLOT_OLD_HIST | SAVESLOT_NEW_HIST)),
			 !!(settled & (SAVESLOT_OLD_HIST | SAVESLOT_NEW_HIST)));
		// Only an action on a state where the save is under the new name alone
		if (first != SAVESLOT_RECOVER_NOTHING)
			CHECK_EQ(exists & (SAVESLOT_OLD_SAVE | SAVESLOT_NEW_SAVE), SAVESLOT_NEW_SAVE);
	}
	// Named states
	CHECK_EQ(saveslot_recover(SAVESLOT_OLD_SAVE | SAVESLOT_OLD_HIST), SAVESLOT_RECOVER_NOTHING); // not begun
	CHECK_EQ(saveslot_recover(SAVESLOT_NEW_SAVE | SAVESLOT_OLD_HIST), SAVESLOT_RECOVER_FINISH);  // cut between
	CHECK_EQ(saveslot_recover(SAVESLOT_NEW_SAVE | SAVESLOT_NEW_HIST), SAVESLOT_RECOVER_NOTHING); // done
	CHECK_EQ(saveslot_recover(SAVESLOT_NEW_SAVE), SAVESLOT_RECOVER_NOTHING);                      // no history
	CHECK_EQ(saveslot_recover(SAVESLOT_NEW_SAVE | SAVESLOT_OLD_HIST | SAVESLOT_NEW_HIST), SAVESLOT_RECOVER_UNDO);
	CHECK_EQ(saveslot_recover(SAVESLOT_OLD_SAVE | SAVESLOT_NEW_SAVE | SAVESLOT_OLD_HIST), SAVESLOT_RECOVER_NOTHING);

	// A cut after every step of the sequence, with and without a history, both
	// in order and with a power cut that recovers twice
	for (int has_hist = 0; has_hist < 2; has_hist++) {
		u32 state = SAVESLOT_OLD_SAVE | (has_hist ? SAVESLOT_OLD_HIST : 0);
		u32 states[3];
		states[0] = state;
		state = (state & ~SAVESLOT_OLD_SAVE) | SAVESLOT_NEW_SAVE;
		states[1] = state;
		state = (state & ~SAVESLOT_OLD_HIST) | (has_hist ? SAVESLOT_NEW_HIST : 0);
		states[2] = state;
		for (int cut = 0; cut < 3; cut++) {
			u32 settled = files_after(states[cut], saveslot_recover(states[cut]));
			// It lands on the old name (undone) or the new one (done), never half of each
			int old_done = !!(settled & SAVESLOT_OLD_SAVE), new_done = !!(settled & SAVESLOT_NEW_SAVE);
			CHECK(old_done != new_done);
			if (new_done)
				CHECK(!(settled & SAVESLOT_OLD_HIST));
		}
	}
}

void test_saveslot_sanity(void)
{
	u8 *p = malloc(1024);
	memset(p, 0xFF, 1024);
	CHECK(saveslot_is_blank(p, 1024));
	p[1023] = 0;
	CHECK(!saveslot_is_blank(p, 1024));
	memset(p, 0, 1024);
	CHECK(saveslot_is_blank(p, 1024));
	p[0] = 1;
	CHECK(!saveslot_is_blank(p, 1024));
	p[0] = 0xFF;
	CHECK(!saveslot_is_blank(p, 1024)); // 0xFF then zeros is neither
	CHECK(saveslot_is_blank(p, 1));
	CHECK(saveslot_is_blank(p, 0));

	// In chunks, as a file is read
	memset(p, 0xFF, 1024);
	u32 state = SAVESLOT_BLANK_START;
	for (u32 i = 0; i < 1024; i += 128)
		state = saveslot_blank_scan(state, p + i, 128);
	CHECK(state != 0);
	p[900] = 0x12;
	state = SAVESLOT_BLANK_START;
	for (u32 i = 0; i < 1024; i += 128)
		state = saveslot_blank_scan(state, p + i, 128);
	CHECK_EQ(state, 0);

	u8 *q = malloc(1024);
	memcpy(q, p, 1024);
	CHECK(saveslot_identical(p, q, 1024));
	q[1023] ^= 1;
	CHECK(!saveslot_identical(p, q, 1024));
	CHECK(saveslot_identical(p, q, 1023));
	CHECK(saveslot_identical(p, q, 0));
	free(q);
	free(p);

	// Does the chip look like the file
	{
		u8 f[1024], c[1024];
		struct saveslot_match m = {0, 0};
		memset(f, 0xFF, sizeof(f));
		memset(c, 0xFF, sizeof(c));
		saveslot_match_scan(&m, f, c, sizeof(f));
		CHECK(saveslot_match_ok(&m)); // blank file: nothing to compare
		for (int k = 0; k < 400; k++)
			f[k] = (u8)(k % 200 + 1);
		memcpy(c, f, sizeof(c));
		m = (struct saveslot_match){0, 0};
		saveslot_match_scan(&m, f, c, sizeof(f));
		CHECK_EQ(m.data, 400);
		CHECK_EQ(m.same, 400);
		CHECK(saveslot_match_ok(&m));
		for (int k = 0; k < 400; k += 2)
			c[k] ^= 0x55; // a game rewrote half
		m = (struct saveslot_match){0, 0};
		saveslot_match_scan(&m, f, c, 512); // in chunks
		saveslot_match_scan(&m, f + 512, c + 512, 512);
		CHECK(saveslot_match_ok(&m));
		memset(c, 0xFF, sizeof(c)); // another game's save
		for (int k = 0; k < 900; k++)
			c[k] = (u8)(k % 190 + 20);
		for (int k = 0; k < 400; k++)
			c[k] = (u8)(f[k] % 200 + 2);
		m = (struct saveslot_match){0, 0};
		saveslot_match_scan(&m, f, c, sizeof(f));
		CHECK(!saveslot_match_ok(&m));
		for (int k = 0; k < 10; k++)
			f[k] = 0xFF;
		for (int k = 10; k < 400; k++)
			f[k] = 0xFF; // under 16 data bytes left
		f[0] = 5;
		m = (struct saveslot_match){0, 0};
		saveslot_match_scan(&m, f, c, sizeof(f));
		CHECK(saveslot_match_ok(&m));
	}
}

void test_saveslot_import_size(void)
{
	// A known save type takes its size and no other
	CHECK(saveslot_import_size_ok(0x8000, 0x8000));
	CHECK(!saveslot_import_size_ok(0x10000, 0x8000));
	CHECK(!saveslot_import_size_ok(0x200, 0x2000));
	CHECK(!saveslot_import_size_ok(0, 0x200));
	// An unknown one takes the sizes a chip has
	u32 ok[] = {512, 8192, 32768, 65536, 131072};
	for (u32 i = 0; i < sizeof(ok) / sizeof(*ok); i++)
		CHECK(saveslot_import_size_ok(ok[i], 0));
	CHECK(!saveslot_import_size_ok(0, 0));
	CHECK(!saveslot_import_size_ok(1000, 0));
	CHECK(!saveslot_import_size_ok(262144, 0));
}

void test_saveslot_stamp_from_rtc(void)
{
	struct saveslot_stamp a, b;
	// 27 Oct 2026, a Tuesday, 14:03:09; the hour's PM flag (0x40) masked
	const u8 t[7] = {0x26, 0x10, 0x27, 0x02, 0x54, 0x03, 0x09};
	CHECK(saveslot_stamp_from_rtc(t, &a));
	CHECK(a.year == 2026 && a.month == 10 && a.day == 27 && a.hour == 14 && a.min == 3 && a.sec == 9);
	const u8 bad[][7] = {
		{0x26, 0x13, 0x27, 0, 0x14, 0, 0},  // month 13
		{0x26, 0x10, 0x00, 0, 0x14, 0, 0},  // day 0
		{0x26, 0x10, 0x27, 0, 0x24, 0, 0},  // hour 24
		{0x26, 0x10, 0x27, 0, 0x14, 0x60, 0},  // minute 60
		{0x2A, 0x10, 0x27, 0, 0x14, 0, 0},  // not BCD
		{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},  // a port that reads all ones
	};
	for (u32 i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		CHECK(!saveslot_stamp_from_rtc(bad[i], &b));
		CHECK(b.year == 0);
	}
	b = a;
	CHECK(saveslot_stamp_cmp(&a, &b) == 0);
	b.sec = 10;
	CHECK(saveslot_stamp_cmp(&a, &b) < 0 && saveslot_stamp_cmp(&b, &a) > 0);
	b = a;
	b.year = 2025;
	b.month = 12;
	CHECK(saveslot_stamp_cmp(&b, &a) < 0);
}
