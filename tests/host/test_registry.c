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

static struct registry_place start(u32 start_on, const char *item, const u8 *shown, u32 n)
{
	struct userdata_boot boot;
	memset(&boot, 0, sizeof(boot));
	strcpy(boot.item, item);
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

	// An older kernel's Home: facing the first item, whatever the boot position
	p = start(SET_START_HOME, "gba", all, 10);
	CHECK_PLACE(p, NAV_RECENT, 0, 0);
	p = start(SET_START_HOME, "", fresh, 4);
	CHECK_PLACE(p, NAV_SD, 0, 0);
	// A chosen item: home facing it, never its page
	p = start(SET_START_RECENT, "gba", all, 10);
	CHECK_PLACE(p, NAV_RECENT, 0, 0);
	p = start(SET_START_SD, "", fresh, 4);
	CHECK_PLACE(p, NAV_SD, 0, 0);
	p = start(SET_START_FAVOURITES, "", all, 10);
	CHECK_PLACE(p, NAV_FAV, 0, 0);
	p = start(SET_START_GBC, "", all, 10);
	CHECK_PLACE(p, NAV_GBC, 0, 0);
	p = start(SET_START_NES, "", all, 10);
	CHECK_PLACE(p, NAV_NES, 0, 0);
	p = start(SET_START_NOR, "gba", all, 10);
	CHECK_PLACE(p, NAV_NOR, 0, 0);
	p = start(SET_START_SETTINGS, "", fresh, 4);
	CHECK_PLACE(p, NAV_SET, 0, 0);
	p = start(SET_START_INFO, "", fresh, 4);
	CHECK_PLACE(p, NAV_INFO, 0, 0);
	// ... and one home doesn't show: Last place, else the first item
	p = start(SET_START_NES, "gba", gba, 5);
	CHECK_PLACE(p, NAV_GBA, 0, 0);
	p = start(SET_START_NES, "nor", gba, 5);
	CHECK_PLACE(p, NAV_NOR, 0, 0);
	p = start(SET_START_RECENT, "", gba, 5);
	CHECK_PLACE(p, NAV_GBA, 0, 0);
	p = start(SET_START_FAVOURITES, "fav", fresh, 4);
	CHECK_PLACE(p, NAV_SD, 0, 0);
	// Last place: home facing the item last faced, never its page (an older
	// kernel's "home-" before the word is dropped)
	p = start(SET_START_LAST, "gba", all, 10);
	CHECK_PLACE(p, NAV_GBA, 0, 0);
	p = start(SET_START_LAST, "home-settings", all, 10);
	CHECK_PLACE(p, NAV_SET, 0, 0);
	p = start(SET_START_LAST, "info", fresh, 4);
	CHECK_PLACE(p, NAV_INFO, 0, 0);
	p = start(SET_START_LAST, "recent", all, 10);
	CHECK_PLACE(p, NAV_RECENT, 0, 0);
	// ... hidden since, never set or unknown: the first item
	p = start(SET_START_LAST, "fav", gba, 5);
	CHECK_PLACE(p, NAV_GBA, 0, 0);
	p = start(SET_START_LAST, "gb", gba, 5);
	CHECK_PLACE(p, NAV_GBA, 0, 0);
	p = start(SET_START_LAST, "home-recent", fresh, 4);
	CHECK_PLACE(p, NAV_SD, 0, 0);
	p = start(SET_START_LAST, "", all, 10);
	CHECK_PLACE(p, NAV_RECENT, 0, 0);
	p = start(SET_START_LAST, "home-", all, 10);
	CHECK_PLACE(p, NAV_RECENT, 0, 0);
	p = start(SET_START_LAST, "carousel", all, 10);
	CHECK_PLACE(p, NAV_RECENT, 0, 0);
	// A Start on value from a later kernel: Last place
	p = start(SET_STARTS + 3, "gba", all, 10);
	CHECK_PLACE(p, NAV_GBA, 0, 0);
	// Each item has its own code, and the codes Settings offers are the
	// items shown
	for (u32 i = 0; i < NAV_ITEMS; i++) {
		CHECK(registry_items[i].start < SET_STARTS && registry_items[i].start != SET_START_HOME &&
		      registry_items[i].start != SET_START_LAST);
		CHECK_EQ(registry_of_start(registry_items[i].start), i);
	}
	CHECK_EQ(registry_of_start(SET_START_LAST), NAV_ITEMS);
	// Settings steps through them in the carousel's order, after Last place
	CHECK_EQ(settings_start_order[0], SET_START_LAST);
	for (u32 i = 0; i < NAV_ITEMS; i++)
		CHECK_EQ(settings_start_order[i + 1], registry_items[i].start);
	CHECK_EQ(registry_start_shown(all, 10), SET_STARTS_ALL & ~(1u << SET_START_LAST));
	CHECK_EQ(registry_start_shown(gba, 5), (1u << SET_START_GBA) | (1u << SET_START_SD) | (1u << SET_START_NOR) |
						       (1u << SET_START_SETTINGS) | (1u << SET_START_INFO));
}

void test_registry_boot_word(void)
{
	// Every item's word is one recent.txt keeps, and Last place reads it
	// back as home facing that item
	u8 all[NAV_ITEMS];
	for (u32 k = 0; k < NAV_ITEMS; k++)
		all[k] = (u8)k;
	for (u32 i = 0; i < NAV_ITEMS; i++) {
		struct registry_place back;
		struct userdata_boot boot;
		memset(&boot, 0, sizeof(boot));
		CHECK(userdata_item_ok(registry_items[i].word));
		strcpy(boot.item, registry_items[i].word);
		registry_start(SET_START_LAST, &boot, all, NAV_ITEMS, &back);
		CHECK_EQ(back.item, i);
		CHECK_EQ(back.open, 0);
	}
}
