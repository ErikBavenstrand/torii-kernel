// The menu's strings (source/lang.c): filling in, English and a pack's
// strings, the pack checks, the dates and numbers, and the language list.
#include <string.h>

#include "../../source/lang.h"
#include "../kernel/lang_test_pack.h"
#include "../kernel/torii_test.h"
#include "tests.h"

static u32 pack_words[0x4000];  // word-aligned, as the kernel reads it
#define pack ((u8 *)pack_words)

static void fill(u32 room, const char *text, u32 a, u32 b, const char *want)
{
	char out[64];
	memset(out, 'x', sizeof(out));
	lang_fill(out, room, text, LN(a), LN(b));
	CHECK(strcmp(out, want) == 0);
	CHECK(room == sizeof(out) || out[room] == 'x');  // nothing past its room
}

void test_lang_fill(void)
{
	lang_use(NULL);
	fill(64, "Screenshot {1}/{2}", 3, 5, "Screenshot 3/5");
	fill(64, "{2} of {1}", 1, 4000000000u, "4000000000 of 1");
	fill(64, "{1}{1}", 7, 0, "77");
	fill(64, "no placeholders", 1, 2, "no placeholders");
	fill(64, "{3} {1 {x} {", 9, 9, "{3} {1 {x} {");  // {3} has no argument: as it is
	fill(64, "", 1, 2, "");
	// Cut to its room, on a whole character
	fill(6, "abcdefgh", 0, 0, "abcde");
	fill(5, "abc\xC3\xA9", 0, 0, "abc");
	fill(6, "abc\xC3\xA9", 0, 0, "abc\xC3\xA9");
	fill(1, "abc", 0, 0, "");
	// The other kinds
	char out[64];
	CHECK_EQ(lang_fill(out, sizeof(out), "{1}|{2}|{3}", LS("str"), LZ(7, 2), LX(0xBEEF, 8)), 15);
	CHECK(!strcmp(out, "str|07|0000BEEF"));
	lang_fill(out, sizeof(out), "{1} {2} {3} {4}", LG(1234567), LG(999), LT(15), LT(1234));
	CHECK(!strcmp(out, "1,234,567 999 1.5 123.4"));
	lang_fill(out, sizeof(out), "[{1}]", LCUT("ab\xC3\xA9z", 3));  // é would be cut in half
	CHECK(!strcmp(out, "[ab]"));
	lang_fill(out, sizeof(out), "[{1}]", LCUT("ab\xC3\xA9z", 4));
	CHECK(!strcmp(out, "[ab\xC3\xA9]"));
	lang_fill(out, sizeof(out), "[{1}]", LCUT("ab", 9));
	CHECK(!strcmp(out, "[ab]"));
	lang_fill(out, sizeof(out), "[{1}]", LS(NULL));
	CHECK(!strcmp(out, "[]"));
	// SF and a count's forms, in English (lang/en.ini)
	SF(out, sizeof(out), STR_INGAME_SHOT_COUNT, LN(1), LN(5));
	CHECK(!strcmp(out, "Screenshot 1/5"));
	SF(out, sizeof(out), STR_N(STR_INGAME_UNSAVED_SHOTS, 2), LN(2));
	CHECK(!strcmp(out, "Not on card yet: 2 screenshots"));
	SF(out, sizeof(out), STR_N(STR_COUNT_GAMES, 1), LN(1));
	CHECK(!strcmp(out, "1 game"));
	CHECK(!strcmp(SN(STR_COUNT_FILES, 0), "{1} files"));
	CHECK(!strcmp(S(STR_COUNT), ""));  // past the ids
}

void test_lang_dates(void)
{
	lang_use(NULL);
	char out[24];
	CHECK_EQ(lang_number(out, sizeof(out), 1204), 5);
	CHECK(!strcmp(out, "1,204"));
	lang_date(out, sizeof(out), 28, 10);
	CHECK(!strcmp(out, "28 Oct"));
	lang_date(out, sizeof(out), 1, 13);  // no such month: the day alone
	CHECK(!strcmp(out, "1 "));
	lang_date_numeric(out, sizeof(out), 3, 9);
	CHECK(!strcmp(out, "09-03"));
	// A pack's formats: the month's name and the day's order, the separators
	static const struct lang_test_string sv[] = {
		{STR_FORMAT_DECIMAL, ","},          {STR_FORMAT_THOUSANDS, "\xC2\xA0"},  // a no-break space
		{STR_FORMAT_DATE, "{day} {month}"}, {STR_FORMAT_DATE_NUMERIC, "{day}/{month}"}, {STR_MONTH_OCT, "okt"},
	};
	u32 size = lang_test_pack(pack, sizeof(pack_words), "sv", "Svenska", sv, 5);
	CHECK_EQ(lang_pack_check(pack, size), LANG_PACK_OK);
	lang_use(pack);
	lang_date(out, sizeof(out), 28, 10);
	CHECK(!strcmp(out, "28 okt"));
	lang_date(out, sizeof(out), 28, 11);
	CHECK(!strcmp(out, "28 Nov"));  // a month it lacks: English's
	lang_date_numeric(out, sizeof(out), 3, 9);
	CHECK(!strcmp(out, "03/09"));
	lang_number(out, sizeof(out), 1204);
	CHECK(!strcmp(out, "1\xC2\xA0"
			   "204"));
	lang_fill(out, sizeof(out), "{1} MB", LT(15));
	CHECK(!strcmp(out, "1,5 MB"));
	lang_use(NULL);
}

void test_lang_pack(void)
{
	static const struct lang_test_string strings[] = {
		{STR_ITEM_RECENT, "Senaste"},
		{STR_COUNT_GAMES, "1 spel"},
		{STR_COUNT_GAMES_OTHER, "{1} spel"},
		{STR_COUNT - 1, "last"},
	};
	u32 size = lang_test_pack(pack, sizeof(pack_words), "sv", "Svenska", strings, 4);
	CHECK(size > LANG_HEADER);
	CHECK_EQ(lang_pack_check(pack, size), LANG_PACK_OK);
	CHECK_EQ(lang_pack_check_header(pack), LANG_PACK_OK);  // the header alone
	CHECK_EQ(lang_pack_check(pack, LANG_HEADER), LANG_PACK_SHORT);  // a file of the header alone
	char code[LANG_CODE_BYTES], name[LANG_NAME_BYTES];
	lang_pack_code(pack, code);
	lang_pack_name(pack, name);
	CHECK(!strcmp(code, "sv") && !strcmp(name, "Svenska"));
	// Its strings, and English for one it lacks
	lang_use(pack);
	CHECK(lang_using_pack());
	CHECK(!strcmp(S(STR_ITEM_RECENT), "Senaste"));
	CHECK(!strcmp(S(STR_ITEM_FAV), "Favourites"));
	CHECK(!strcmp(S(STR_COUNT - 1), "last"));
	char out[24];
	SF(out, sizeof(out), STR_N(STR_COUNT_GAMES, 3), LG(3));
	CHECK(!strcmp(out, "3 spel"));
	lang_use(NULL);
	CHECK(!lang_using_pack());
	CHECK(!strcmp(S(STR_ITEM_RECENT), "Recent"));

	// Damaged and stale packs
	CHECK_EQ(lang_pack_check(pack, size - 2), LANG_PACK_SHORT);  // cut short
	CHECK_EQ(lang_pack_check(pack, 10), LANG_PACK_SHORT);
	pack[LANG_HEADER + 2 * STR_COUNT + 2] ^= 1;  // a string's byte
	CHECK_EQ(lang_pack_check(pack, size), LANG_PACK_CRC);
	pack[LANG_HEADER + 2 * STR_COUNT + 2] ^= 1;
	CHECK_EQ(lang_pack_check(pack, size), LANG_PACK_OK);
	pack[0] = 'X';
	CHECK_EQ(lang_pack_check(pack, size), LANG_PACK_MAGIC);
	pack[0] = 'T';
	lang_test_put32(pack + 8, LANG_KEY_HASH ^ 1);  // built for other keys
	CHECK_EQ(lang_pack_check(pack, size), LANG_PACK_STALE);
	CHECK_EQ(lang_pack_check_header(pack), LANG_PACK_STALE);
	lang_test_put32(pack + 8, LANG_KEY_HASH);
	lang_test_put16(pack + 4, LANG_FORMAT + 1);  // another format
	CHECK_EQ(lang_pack_check(pack, size), LANG_PACK_STALE);
	lang_test_put16(pack + 4, LANG_FORMAT);
	lang_test_put16(pack + 6, STR_COUNT - 1);  // another count
	CHECK_EQ(lang_pack_check(pack, size), LANG_PACK_STALE);
	lang_test_put16(pack + 6, STR_COUNT);
	CHECK_EQ(lang_pack_check(pack, size), LANG_PACK_OK);
	// An offset past the strings, even with its CRC right
	lang_test_put16(pack + LANG_HEADER, size);
	lang_test_pack_seal(pack, size);
	CHECK_EQ(lang_pack_check(pack, size), LANG_PACK_BAD);
	// Strings with no end
	size = lang_test_pack(pack, sizeof(pack_words), "sv", "Svenska", strings, 1);
	pack[size - 2] = pack[size - 1] = 'x';
	lang_test_pack_seal(pack, size);
	CHECK_EQ(lang_pack_check(pack, size), LANG_PACK_BAD);
	// A size past a pack's room in PSRAM
	size = lang_test_pack(pack, sizeof(pack_words), "sv", "Svenska", strings, 1);
	lang_test_put32(pack + 12, LANG_PACK_MAX + 2);
	CHECK_EQ(lang_pack_check_header(pack), LANG_PACK_BAD);
}

void test_lang_launch(void)
{
	static const struct lang_test_string strings[] = {
		{STR_LAUNCH_PATCHING, "Patchar spelet"},
		{STR_INGAME_RESUME, "Fortsätt"},
		{STR_ITEM_RECENT, "Senaste"},
	};
	CHECK(STR_INGAME_RESUME < STR_LAUNCH_END && STR_ITEM_RECENT >= STR_LAUNCH_END);
	u32 size = lang_test_pack(pack, sizeof(pack_words), "sv", "Svenska", strings, 3);
	CHECK_EQ(lang_pack_check(pack, size), LANG_PACK_OK);
	lang_use(pack);
	static u32 room[LANG_LAUNCH_MAX / 4];
	CHECK(lang_keep_launch(room, sizeof(room)));
	memset(pack, 0, sizeof(pack_words));  // the launch fills PSRAM
	CHECK(!strcmp(S(STR_LAUNCH_PATCHING), "Patchar spelet"));
	CHECK(!strcmp(S(STR_INGAME_RESUME), "Fortsätt"));
	CHECK(!strcmp(S(STR_INGAME_SAVE), "Save state"));  // the pack lacks it
	CHECK(!strcmp(S(STR_ITEM_RECENT), "Recent"));  // not a launch's: English
	// No room: English for all
	size = lang_test_pack(pack, sizeof(pack_words), "sv", "Svenska", strings, 3);
	lang_use(pack);
	CHECK(!lang_keep_launch(room, 2 * STR_LAUNCH_END + 4));
	CHECK(!lang_using_pack());
	CHECK(!strcmp(S(STR_LAUNCH_PATCHING), "Patching the game"));
	lang_use(NULL);
}

void test_lang_list(void)
{
	lang_list_reset();
	CHECK_EQ(lang_count(), 1);
	CHECK(!strcmp(lang_choice(0)->name, "English"));
	CHECK_EQ(lang_choice(0)->id, LANG_ENGLISH_ID);
	// tools/lang.py's code_id gives the same ids
	CHECK_EQ(lang_code_id("sv"), 0x564E);
	CHECK_EQ(lang_code_id("en-XA"), 0x53C4);
	CHECK(lang_list_add("sv", "Svenska"));
	CHECK(!lang_list_add("sv", "Svenska again"));  // its id is there
	CHECK_EQ(lang_count(), 2);
	CHECK_EQ(lang_index_of(lang_code_id("sv")), 1);
	CHECK_EQ(lang_index_of(LANG_ENGLISH_ID), 0);
	CHECK_EQ(lang_index_of(0x1234), 0);  // unknown: English
	CHECK(!strcmp(lang_choice(1)->code, "sv"));
	CHECK(!strcmp(lang_choice(99)->code, "en"));
	static const char *const codes[] = {"aa", "bb", "cc", "dd", "ee", "ff", "gg"};
	u32 added = 0;
	for (u32 i = 0; i < sizeof(codes) / sizeof(codes[0]); i++)
		added += (u32)lang_list_add(codes[i], codes[i]);
	CHECK_EQ(lang_count(), LANG_LIST_MAX);
	CHECK_EQ(added, LANG_LIST_MAX - 2);
	lang_list_reset();
}
