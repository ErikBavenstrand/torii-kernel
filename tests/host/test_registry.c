// The menu's items and where it starts (source/screens/registry.c).
#include <string.h>

#include "../../source/screens/registry.h"
#include "../../source/settings_model.h"
#include "../kernel/torii_test.h"
#include "tests.h"

// The items shown for counts, as their words joined by spaces
static const char *shown_words(const u32 counts[NAV_ITEMS])
{
	static char out[128];
	u8 shown[NAV_ITEMS];
	u32 n = registry_shown(counts, shown);
	out[0] = 0;
	for (u32 i = 0; i < n; i++) {
		if (i)
			strcat(out, " ");
		strcat(out, registry_items[shown[i]].word);
	}
	return out;
}

void test_registry_shown(void)
{
	u32 counts[NAV_ITEMS] = {0};
	// A new card: the four that always show
	CHECK(!strcmp(shown_words(counts), "sd nor settings info"));
	// Recent and Favourites while they hold games; each console with games,
	// in the carousel's order whatever the order of the counts
	counts[NAV_NES] = 3;
	counts[NAV_RECENT] = 1;
	CHECK(!strcmp(shown_words(counts), "recent nes sd nor settings info"));
	counts[NAV_FAV] = 2;
	counts[NAV_GBA] = 176;
	counts[NAV_GBC] = 36;
	counts[NAV_GB] = 41;
	CHECK(!strcmp(shown_words(counts), "recent fav gba gbc gb nes sd nor settings info"));
	// What the always-shown ones hold doesn't hide them
	counts[NAV_SD] = counts[NAV_NOR] = 0;
	CHECK(!strcmp(shown_words(counts), "recent fav gba gbc gb nes sd nor settings info"));
	// Every word finds its item, and an unknown one none
	for (u32 i = 0; i < NAV_ITEMS; i++)
		CHECK_EQ(registry_find(registry_items[i].word), i);
	CHECK_EQ(registry_find("home"), NAV_ITEMS);
	CHECK_EQ(registry_find(""), NAV_ITEMS);
}

void test_registry_nearest(void)
{
	const u8 shown[] = {NAV_RECENT, NAV_GBC, NAV_NES, NAV_SD, NAV_NOR, NAV_SET, NAV_INFO};
	u32 n = sizeof(shown);
	CHECK_EQ(registry_nearest(shown, n, NAV_SD), 3);  // shown: itself
	CHECK_EQ(registry_nearest(shown, n, NAV_GB), 2);  // GBC and NES both 1 away: the later
	CHECK_EQ(registry_nearest(shown, n, NAV_FAV), 0);  // Recent 1 away, GBC 2
	CHECK_EQ(registry_nearest(shown, n, NAV_GBA), 1);  // GBC 1 away, Recent 2
	const u8 tail[] = {NAV_SD, NAV_NOR, NAV_SET, NAV_INFO};
	CHECK_EQ(registry_nearest(tail, 4, NAV_RECENT), 0);
	CHECK_EQ(registry_nearest(tail, 4, NAV_NES), 0);
	CHECK_EQ(registry_index(tail, 4, NAV_NOR), 1);
	CHECK_EQ(registry_index(tail, 4, NAV_GBA), 4);
}

static struct registry_place start(u32 start_on, const char *item, u32 row, const u8 *shown, u32 n)
{
	struct userdata_boot boot;
	memset(&boot, 0, sizeof(boot));
	strcpy(boot.item, item);
	boot.row = row;
	struct registry_place p;
	registry_start(start_on, &boot, shown, n, &p);
	return p;
}

#define CHECK_PLACE(p, i, o, r) \
	do { \
		CHECK_EQ((p).item, (i)); \
		CHECK_EQ((p).open, (o)); \
		CHECK_EQ((p).row, (r)); \
	} while (0)

void test_registry_start(void)
{
	const u8 all[] = {NAV_RECENT, NAV_FAV, NAV_GBA, NAV_GBC, NAV_GB, NAV_NES, NAV_SD, NAV_NOR, NAV_SET, NAV_INFO};
	const u8 fresh[] = {NAV_SD, NAV_NOR, NAV_SET, NAV_INFO};
	const u8 gba[] = {NAV_GBA, NAV_SD, NAV_NOR, NAV_SET, NAV_INFO};
	struct registry_place p;

	// Home: facing the first item, whatever the boot position
	p = start(SET_START_HOME, "gba", 7, all, 10);
	CHECK_PLACE(p, NAV_RECENT, 0, 0);
	p = start(SET_START_HOME, "", 0, fresh, 4);
	CHECK_PLACE(p, NAV_SD, 0, 0);
	// A chosen item opens its page at its first row
	p = start(SET_START_RECENT, "gba", 7, all, 10);
	CHECK_PLACE(p, NAV_RECENT, 1, 0);
	p = start(SET_START_SD, "", 0, fresh, 4);
	CHECK_PLACE(p, NAV_SD, 1, 0);
	p = start(SET_START_FAVOURITES, "", 0, all, 10);
	CHECK_PLACE(p, NAV_FAV, 1, 0);
	p = start(SET_START_GBC, "", 0, all, 10);
	CHECK_PLACE(p, NAV_GBC, 1, 0);
	p = start(SET_START_NES, "", 0, all, 10);
	CHECK_PLACE(p, NAV_NES, 1, 0);
	// ... and a hidden one gives home facing the nearest item that shows
	p = start(SET_START_RECENT, "", 0, gba, 5);
	CHECK_PLACE(p, NAV_GBA, 0, 0);
	p = start(SET_START_FAVOURITES, "", 0, fresh, 4);
	CHECK_PLACE(p, NAV_SD, 0, 0);
	p = start(SET_START_NES, "", 0, gba, 5);
	CHECK_PLACE(p, NAV_SD, 0, 0);
	p = start(SET_START_GB, "", 0, gba, 5);
	CHECK_PLACE(p, NAV_SD, 0, 0);  // GBA and SD both 2 away: the later
	// Last place: a page and its row, or home facing an item
	p = start(SET_START_LAST, "gba", 7, all, 10);
	CHECK_PLACE(p, NAV_GBA, 1, 7);
	p = start(SET_START_LAST, "home-settings", 0, all, 10);
	CHECK_PLACE(p, NAV_SET, 0, 0);
	p = start(SET_START_LAST, "info", 3, fresh, 4);
	CHECK_PLACE(p, NAV_INFO, 1, 3);
	// ... hidden since: home facing the nearest; never set or unknown: the first
	p = start(SET_START_LAST, "fav", 4, gba, 5);
	CHECK_PLACE(p, NAV_GBA, 0, 0);
	p = start(SET_START_LAST, "home-recent", 0, fresh, 4);
	CHECK_PLACE(p, NAV_SD, 0, 0);
	p = start(SET_START_LAST, "", 0, all, 10);
	CHECK_PLACE(p, NAV_RECENT, 0, 0);
	p = start(SET_START_LAST, "home-", 0, all, 10);
	CHECK_PLACE(p, NAV_RECENT, 0, 0);
	p = start(SET_START_LAST, "carousel", 9, all, 10);
	CHECK_PLACE(p, NAV_RECENT, 0, 0);
	// A Start on value from a later kernel: home
	p = start(SET_STARTS + 3, "gba", 2, all, 10);
	CHECK_PLACE(p, NAV_RECENT, 0, 0);
}

void test_registry_boot_word(void)
{
	char word[USERDATA_ITEM_MAX];
	struct registry_place p = {NAV_FAV, 1, 12};
	registry_boot_word(&p, word, sizeof(word));
	CHECK(!strcmp(word, "fav"));
	p.open = 0;
	registry_boot_word(&p, word, sizeof(word));
	CHECK(!strcmp(word, "home-fav"));
	// Every word, on home too, is one recent.txt keeps and reads back
	for (u32 i = 0; i < NAV_ITEMS; i++) {
		for (u32 open = 0; open < 2; open++) {
			struct registry_place q = {(u8)i, (u8)open, 0}, back;
			registry_boot_word(&q, word, sizeof(word));
			CHECK(userdata_item_ok(word));
			struct userdata_boot boot = {{0}, 5};
			strcpy(boot.item, word);
			u8 all[NAV_ITEMS];
			for (u32 k = 0; k < NAV_ITEMS; k++)
				all[k] = (u8)k;
			registry_start(SET_START_LAST, &boot, all, NAV_ITEMS, &back);
			CHECK_EQ(back.item, i);
			CHECK_EQ(back.open, open);
		}
	}
}
