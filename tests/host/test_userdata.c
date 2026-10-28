// The user's lists, the hardware-free half (source/userdata.c).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../source/userdata.h"
#include "../kernel/torii_test.h"
#include "tests.h"

void test_userdata_paths(void)
{
	char path[USERDATA_PATH_MAX + 8], name[300];
	CHECK(userdata_path_ok("/a.gba"));
	CHECK(userdata_path_ok("/GBA/Advance Wars.gba"));
	CHECK(userdata_path_ok("NOR:Advance Wars.gba"));
	CHECK(!userdata_path_ok(""));
	CHECK(!userdata_path_ok("a.gba"));
	CHECK(!userdata_path_ok("/"));
	CHECK(!userdata_path_ok("/GBA/"));
	CHECK(!userdata_path_ok("NOR:"));
	CHECK(!userdata_path_ok("/a\tb.gba"));
	CHECK(userdata_path_ok("/Pok\xc3\xa9mon.gba"));  // UTF-8
	// The longest folder and name the launch path takes, and one more
	memset(name, 'n', USERDATA_NAME_MAX);
	name[USERDATA_NAME_MAX] = 0;
	snprintf(path, sizeof(path), "/%s", name);
	CHECK(userdata_path_ok(path));
	name[USERDATA_NAME_MAX] = 'n';
	name[USERDATA_NAME_MAX + 1] = 0;
	snprintf(path, sizeof(path), "/%s", name);
	CHECK(!userdata_path_ok(path));
	memset(path, 'd', sizeof(path));
	path[0] = '/';
	path[USERDATA_DIR_MAX - 1] = '/';  // a folder of 255 bytes
	snprintf(path + USERDATA_DIR_MAX, sizeof(path) - USERDATA_DIR_MAX, "x.gba");
	CHECK(userdata_path_ok(path));
	path[USERDATA_DIR_MAX - 1] = 'd';
	path[USERDATA_DIR_MAX] = '/';  // 256
	snprintf(path + USERDATA_DIR_MAX + 1, sizeof(path) - USERDATA_DIR_MAX - 1, "x.gba");
	CHECK(!userdata_path_ok(path));
	memset(name, 'n', USERDATA_NOR_NAME_MAX + 1);
	name[USERDATA_NOR_NAME_MAX] = 0;
	snprintf(path, sizeof(path), "NOR:%s", name);
	CHECK(userdata_path_ok(path));
	snprintf(path, sizeof(path), "NOR:%sn", name);
	CHECK(!userdata_path_ok(path));
	// NOR: entries
	CHECK(strcmp(userdata_nor_name("NOR:Advance Wars.gba"), "Advance Wars.gba") == 0);
	CHECK(userdata_nor_name("/NOR:Advance Wars.gba") == NULL);
	CHECK(userdata_nor_name("/GBA/a.gba") == NULL);
	// Joins
	CHECK(userdata_join(path, "/", "a.gba") && strcmp(path, "/a.gba") == 0);
	CHECK(userdata_join(path, "/GBA", "a.gba") && strcmp(path, "/GBA/a.gba") == 0);
	memset(name, 'n', 299);
	name[299] = 0;
	CHECK(!userdata_join(path, name, name) && path[0] == 0);
	// Boot items
	CHECK(userdata_item_ok("gba"));
	CHECK(userdata_item_ok("sd-card_2"));
	CHECK(!userdata_item_ok(""));
	CHECK(!userdata_item_ok("two words"));
	CHECK(!userdata_item_ok("0123456789abcdef"));  // 16: one too many
}

static void check_canonical(const char *in, const char *want)
{
	char path[64];
	snprintf(path, sizeof(path), "%s", in);
	userdata_canonical(path);
	if (strcmp(path, want) != 0)
		tlog_error("FAIL %s: %s reads as %s, expected %s", __FILE__, in, path, want);
	CHECK(strcmp(path, want) == 0);
}

void test_userdata_canonical(void)
{
	check_canonical("/deep/../x.gba", "/x.gba");
	check_canonical("/a/./b//c.gba", "/a/b/c.gba");
	check_canonical("/a/b/../../c/x.gba", "/c/x.gba");
	check_canonical("/../x.gba", "/x.gba");  // above the root: the root
	check_canonical("/a/..", "/");
	check_canonical("/", "/");
	check_canonical("//", "/");
	check_canonical("/games/", "/games");
	check_canonical("/..x/.y/x..gba", "/..x/.y/x..gba");  // dots inside names stay
	check_canonical("/GBA/Pokemon - Ruby.gba", "/GBA/Pokemon - Ruby.gba");
	check_canonical("NOR:a/../b", "NOR:a/../b");  // a NOR entry is a name
	check_canonical("", "");
}

void test_userdata_parse(void)
{
	char line[USERDATA_LINE_MAX];
	u32 t = 99;
	char *path;
	struct userdata_boot boot = {{0}};
	snprintf(line, sizeof(line), "1792368000\t/GBA/Advance Wars.gba");
	CHECK_EQ(userdata_parse_recent(line, &t, &path, &boot), USERDATA_GAME);
	CHECK(t == 1792368000u && strcmp(path, "/GBA/Advance Wars.gba") == 0);
	// The v0.3 list's lines, and hand edits: no time, blanks, CRLF
	snprintf(line, sizeof(line), "/a.gba\r\n");
	CHECK_EQ(userdata_parse_recent(line, &t, &path, &boot), USERDATA_GAME);
	CHECK(t == 0 && strcmp(path, "/a.gba") == 0);
	snprintf(line, sizeof(line), "  0   NOR:b.gba  ");
	CHECK_EQ(userdata_parse_recent(line, &t, &path, &boot), USERDATA_GAME);
	CHECK(t == 0 && strcmp(path, "NOR:b.gba") == 0);
	snprintf(line, sizeof(line), "4294967295\t/max.gba");
	CHECK_EQ(userdata_parse_recent(line, &t, &path, &boot), USERDATA_GAME);
	CHECK_EQ(t, 0xFFFFFFFFu);
	// Skipped and bad lines
	const char *skip[] = {"", "   ", "# a comment", "#crc32 1a2b3c4d"};
	for (u32 i = 0; i < sizeof(skip) / sizeof(skip[0]); i++) {
		snprintf(line, sizeof(line), "%s", skip[i]);
		CHECK_EQ(userdata_parse_recent(line, &t, &path, &boot), USERDATA_SKIP);
	}
	const char *bad[] = {"4294967296\t/a.gba", "12/a.gba",   "12\tnot a path", "12",
			     "C:\\a.gba",          "@boot",      "@boot g.ba",     "@boot gba x",
			     "@boot two words 3",  "@bootgba 3", "@where gba 3"};
	for (u32 i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		snprintf(line, sizeof(line), "%s", bad[i]);
		CHECK_EQ(userdata_parse_recent(line, &t, &path, &boot), USERDATA_BAD);
	}
	CHECK(boot.item[0] == 0);
	// The item alone, or an older kernel's "<item> <row>" (the row dropped)
	snprintf(line, sizeof(line), "@boot  favourites\t12 \r");
	CHECK_EQ(userdata_parse_recent(line, &t, &path, &boot), USERDATA_BOOT);
	CHECK(strcmp(boot.item, "favourites") == 0);
	snprintf(line, sizeof(line), "@boot gba\r");
	CHECK_EQ(userdata_parse_recent(line, &t, &path, &boot), USERDATA_BOOT);
	CHECK(strcmp(boot.item, "gba") == 0);
	// favorites.txt
	snprintf(line, sizeof(line), " /GBA/a.gba \r");
	CHECK_EQ(userdata_parse_fav(line, &path), USERDATA_GAME);
	CHECK(strcmp(path, "/GBA/a.gba") == 0);
	snprintf(line, sizeof(line), "# mine");
	CHECK_EQ(userdata_parse_fav(line, &path), USERDATA_SKIP);
	snprintf(line, sizeof(line), "12\t/GBA/a.gba");
	CHECK_EQ(userdata_parse_fav(line, &path), USERDATA_BAD);
}

void test_userdata_recent_lines(void)
{
	static struct userdata_recent list[2] = {{1792368000, "/GBA/a.gba"}, {0, "NOR:b.gba"}};
	struct userdata_boot boot = {{0}};
	char buf[USERDATA_LINE_MAX];
	CHECK_EQ(userdata_recent_line(list, 2, &boot, 0, buf, sizeof(buf)), 22);
	CHECK(strcmp(buf, "1792368000\t/GBA/a.gba\n") == 0);
	userdata_recent_line(list, 2, &boot, 1, buf, sizeof(buf));
	CHECK(strcmp(buf, "0\tNOR:b.gba\n") == 0);
	CHECK_EQ(userdata_recent_line(list, 2, &boot, 2, buf, sizeof(buf)), 0);
	snprintf(boot.item, sizeof(boot.item), "gba");
	userdata_recent_line(list, 2, &boot, 0, buf, sizeof(buf));
	CHECK(strcmp(buf, "@boot gba\n") == 0);
	userdata_recent_line(list, 2, &boot, 2, buf, sizeof(buf));
	CHECK(strcmp(buf, "0\tNOR:b.gba\n") == 0);
	CHECK_EQ(userdata_recent_line(list, 2, &boot, 3, buf, sizeof(buf)), 0);
	CHECK_EQ(userdata_recent_line(list, 0, &boot, 1, buf, sizeof(buf)), 0);
	// A line that doesn't fit is cut, not overrun
	CHECK_EQ(userdata_recent_line(list, 2, &boot, 1, buf, 8), 7);
	// Each line written parses back to what it was
	userdata_recent_line(list, 2, &boot, 1, buf, sizeof(buf));
	u32 t;
	char *path;
	struct userdata_boot back = {{0}};
	CHECK_EQ(userdata_parse_recent(buf, &t, &path, &back), USERDATA_GAME);
	CHECK(t == list[0].time && strcmp(path, list[0].path) == 0);
	userdata_recent_line(list, 2, &boot, 0, buf, sizeof(buf));
	CHECK_EQ(userdata_parse_recent(buf, &t, &path, &back), USERDATA_BOOT);
	CHECK(strcmp(back.item, "gba") == 0);
}

void test_userdata_recent_edits(void)
{
	static struct userdata_recent list[USERDATA_RECENT_MAX + 1];
	char path[32];
	u32 count = 0;
	// Newest first, the cap dropping the oldest
	for (u32 i = 1; i <= USERDATA_RECENT_MAX + 5; i++) {
		snprintf(path, sizeof(path), "/game%lu.gba", (unsigned long)i);
		count = userdata_recent_put(list, count, path, i);
	}
	CHECK_EQ(count, USERDATA_RECENT_MAX);
	CHECK(strcmp(list[0].path, "/game37.gba") == 0 && list[0].time == 37);
	CHECK(strcmp(list[USERDATA_RECENT_MAX - 1].path, "/game6.gba") == 0);
	// One already there moves up, with its new time; nothing else moves
	count = userdata_recent_put(list, count, "/game20.gba", 100);
	CHECK_EQ(count, USERDATA_RECENT_MAX);
	CHECK(strcmp(list[0].path, "/game20.gba") == 0 && list[0].time == 100);
	CHECK(strcmp(list[1].path, "/game37.gba") == 0);
	CHECK(strcmp(list[17].path, "/game21.gba") == 0);
	CHECK(strcmp(list[18].path, "/game19.gba") == 0);
	CHECK(strcmp(list[USERDATA_RECENT_MAX - 1].path, "/game6.gba") == 0);
	// Drops: one path, NOR games by name, all NOR games
	count = userdata_recent_put(list, 0, "/a.gba", 1);
	count = userdata_recent_put(list, count, "NOR:b.gba", 2);
	count = userdata_recent_put(list, count, "/GBA/b.gba", 3);
	count = userdata_recent_put(list, count, "NOR:c.gba", 4);
	count = userdata_recent_put(list, count, "/c.gba", 5);
	CHECK_EQ(userdata_recent_drop(list, count, "/missing.gba"), 5);
	count = userdata_recent_drop(list, count, "/GBA/b.gba");
	CHECK_EQ(count, 4);
	CHECK(strcmp(list[0].path, "/c.gba") == 0 && strcmp(list[3].path, "/a.gba") == 0);
	count = userdata_recent_drop_nor(list, count, "b.gba");
	CHECK_EQ(count, 3);
	CHECK(strcmp(list[1].path, "NOR:c.gba") == 0 && list[1].time == 4);
	count = userdata_recent_put(list, count, "NOR:d.gba", 6);
	count = userdata_recent_drop_nor(list, count, NULL);
	CHECK_EQ(count, 2);
	CHECK(strcmp(list[0].path, "/c.gba") == 0 && strcmp(list[1].path, "/a.gba") == 0);
	// Renames keep the place and time, and drop an entry for the new name
	count = userdata_recent_put(list, count, "/new.gba", 7);  // new, c, a
	count = userdata_recent_rename(list, count, "/c.gba", "/d.gba");
	CHECK_EQ(count, 3);
	CHECK(strcmp(list[1].path, "/d.gba") == 0 && list[1].time == 5);
	count = userdata_recent_rename(list, count, "/a.gba", "/new.gba");
	CHECK_EQ(count, 2);
	CHECK(strcmp(list[0].path, "/d.gba") == 0 && strcmp(list[1].path, "/new.gba") == 0 && list[1].time == 1);
	CHECK_EQ(userdata_recent_rename(list, count, "/gone.gba", "/x.gba"), 2);
	CHECK_EQ(userdata_recent_drop(list, 0, "/a.gba"), 0);
}

static const char *strings[8];

static const char *str_of(u32 h)
{
	return strings[h];
}

void test_userdata_favourites(void)
{
	u32 list[8];
	int found;
	strings[0] = "/b.gba";
	strings[1] = "/a.gba";
	strings[2] = "NOR:z.gba";
	strings[3] = "/GBA/c.gba";
	strings[4] = "/a.gba";
	strings[5] = "/GBA/b.gba";
	for (u32 i = 0; i < 6; i++)
		list[i] = i;
	CHECK(!userdata_in_order(list, 6, str_of));
	u32 count = userdata_sort(list, 6, str_of);
	CHECK_EQ(count, 5);  // /a.gba once
	CHECK(userdata_in_order(list, count, str_of));
	CHECK(strcmp(str_of(list[0]), "/GBA/b.gba") == 0);  // byte order: capitals first
	CHECK(strcmp(str_of(list[4]), "NOR:z.gba") == 0);
	CHECK_EQ(userdata_find(list, count, "/a.gba", str_of, &found), 2);
	CHECK(found);
	CHECK_EQ(userdata_find(list, count, "/aa.gba", str_of, &found), 3);
	CHECK(!found);
	CHECK_EQ(userdata_find(list, count, "~", str_of, &found), 5);
	CHECK_EQ(userdata_find(list, 0, "/a.gba", str_of, &found), 0);
	CHECK(!found);
	CHECK_EQ(userdata_sort(list, 0, str_of), 0);
	CHECK(userdata_in_order(list, 1, str_of));
}

// A random big list sorts into order, as the store sorts a hand-edited file
static char big[USERDATA_FAV_MAX][16];

static const char *big_of(u32 h)
{
	return big[h];
}

void test_userdata_sort_big(void)
{
	static u32 list[USERDATA_FAV_MAX];
	srand(5);
	for (u32 i = 0; i < USERDATA_FAV_MAX; i++) {
		snprintf(big[i], sizeof(big[i]), "/%08x", (unsigned)rand() % (USERDATA_FAV_MAX / 2));  // some twice
		list[i] = i;
	}
	u32 count = userdata_sort(list, USERDATA_FAV_MAX, big_of);
	CHECK(count < USERDATA_FAV_MAX && count > USERDATA_FAV_MAX / 4);
	CHECK(userdata_in_order(list, count, big_of));
	// Every name is still there
	for (u32 i = 0; i < USERDATA_FAV_MAX; i += 97) {
		int found;
		userdata_find(list, count, big[i], big_of, &found);
		CHECK(found);
	}
}

void test_userdata_clock(void)
{
	CHECK_EQ(userdata_unix_time(2000, 1, 1, 0, 0, 0), 946684800u);
	CHECK_EQ(userdata_unix_time(2026, 10, 1, 0, 0, 0), USERDATA_CLOCK_FROM);
	CHECK_EQ(userdata_unix_time(2024, 2, 29, 12, 34, 56), 1709210096u);
	CHECK_EQ(userdata_unix_time(2099, 12, 31, 23, 59, 59), 4102444799u);
	CHECK_EQ(userdata_unix_time(2023, 2, 29, 0, 0, 0), 0);
	CHECK_EQ(userdata_unix_time(2000, 2, 29, 0, 0, 0), 951782400u);  // 2000 is a leap year
	CHECK_EQ(userdata_unix_time(2026, 13, 1, 0, 0, 0), 0);
	CHECK_EQ(userdata_unix_time(2026, 4, 31, 0, 0, 0), 0);
	CHECK_EQ(userdata_unix_time(2026, 1, 1, 24, 0, 0), 0);
	CHECK_EQ(userdata_unix_time(1999, 12, 31, 0, 0, 0), 0);
	// A clock that was never set (2000-01-01 after a dead battery) reads as 0 (T4)
	CHECK_EQ(userdata_clock_time(946684800u), 0);
	CHECK_EQ(userdata_clock_time(USERDATA_CLOCK_FROM - 1), 0);
	CHECK_EQ(userdata_clock_time(USERDATA_CLOCK_FROM), USERDATA_CLOCK_FROM);
}

void test_userdata_ago(void)
{
	// English's words (lang/en.ini)
	const struct userdata_ago_words w = userdata_ago_words_now();
	const u32 now = 1800000000u, m = 60, h = 3600, d = 86400;
	static const struct {
		u32 ago;
		const char *text;
	} cases[] = {
		{0, "just now"},
		{59, "just now"},
		{m, "1m ago"},
		{59 * m + 59, "59m ago"},
		{h, "1h ago"},
		{2 * h, "2h ago"},
		{23 * h + 59 * m, "23h ago"},
		{d, "yesterday"},
		{2 * d - 1, "yesterday"},
		{2 * d, "2 days"},
		{3 * d, "3 days"},
		{7 * d - 1, "6 days"},
		{7 * d, "1 week"},
		{14 * d - 1, "1 week"},
		{14 * d, "2 weeks"},
		{29 * d, "4 weeks"},
		{30 * d, "1 month"},
		{60 * d, "2 months"},
		{364 * d, "12 months"},
		{365 * d, "1 year"},
		{730 * d, "2 years"},
		{3650 * d, "10 years"},
	};
	char out[24];
	for (u32 i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		userdata_ago(out, sizeof(out), now - cases[i].ago, now, &w);
		if (strcmp(out, cases[i].text) != 0)
			tlog_error("%lu s ago: '%s', expected '%s'", (unsigned long)cases[i].ago, out, cases[i].text);
		CHECK(strcmp(out, cases[i].text) == 0);
	}
	// Nothing for a time the clock didn't have, or one ahead of it
	userdata_ago(out, sizeof(out), 0, now, &w);
	CHECK(out[0] == 0);
	userdata_ago(out, sizeof(out), now, 0, &w);
	CHECK(out[0] == 0);
	userdata_ago(out, sizeof(out), now + 1, now, &w);
	CHECK(out[0] == 0);
	userdata_ago(out, 4, now - 2 * h, now, &w);
	CHECK(strcmp(out, "2h ") == 0);  // cut to fit
}
