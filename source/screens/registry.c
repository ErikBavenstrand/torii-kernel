// The menu's items and where it starts (registry.h). Hardware-free.
#include <string.h>

#include "registry.h"
#include "../library.h"
#include "../settings_model.h"
#include "../ui/art_ids.h"
#include "../ui/palette.h"

_Static_assert((int)NAV_RECENT == ART_ITEM_RECENT && (int)NAV_FAV == ART_ITEM_FAV && (int)NAV_GBA == ART_ITEM_GBA &&
		       (int)NAV_NES == ART_ITEM_NES && (int)NAV_SD == ART_ITEM_SD && (int)NAV_INFO == ART_ITEM_INFO &&
		       (int)NAV_ITEMS == ART_ITEM_COUNT,
	       "the items are the art's, in the carousel's order");

const struct registry_item registry_items[NAV_ITEMS] = {
	[NAV_RECENT] = {"recent", PAL_RECENT, ART_ICON16_RECENT, NAV_NO_CONSOLE, SET_START_RECENT, 1},
	[NAV_FAV] = {"fav", PAL_FAV, ART_ICON16_FAV, NAV_NO_CONSOLE, SET_START_FAVOURITES, 1},
	[NAV_GBA] = {"gba", PAL_GBA, ART_ICON16_GBA, LIB_GBA, SET_START_GBA, 1},
	[NAV_GBC] = {"gbc", PAL_GBC, ART_ICON16_GBC, LIB_GBC, SET_START_GBC, 1},
	[NAV_GB] = {"gb", PAL_GB, ART_ICON16_GB, LIB_GB, SET_START_GB, 1},
	[NAV_NES] = {"nes", PAL_NES, ART_ICON16_NES, LIB_NES, SET_START_NES, 1},
	[NAV_SD] = {"sd", PAL_SD, ART_ICON16_SD, NAV_NO_CONSOLE, SET_START_SD, 0},
	[NAV_NOR] = {"nor", PAL_NOR, ART_ICON16_NOR, NAV_NO_CONSOLE, NAV_NO_START, 0},
	[NAV_SET] = {"settings", PAL_SET, ART_ICON16_SET, NAV_NO_CONSOLE, NAV_NO_START, 0},
	[NAV_INFO] = {"info", PAL_INFO, ART_ICON16_INFO, NAV_NO_CONSOLE, NAV_NO_START, 0},
};

u32 registry_shown(const u32 counts[NAV_ITEMS], u8 shown[NAV_ITEMS])
{
	u32 n = 0;
	for (u32 i = 0; i < NAV_ITEMS; i++)
		if (!registry_items[i].hides || counts[i])
			shown[n++] = (u8)i;
	return n;
}

u32 registry_index(const u8 *shown, u32 count, u32 item)
{
	for (u32 i = 0; i < count; i++)
		if (shown[i] == item)
			return i;
	return count;
}

u32 registry_nearest(const u8 *shown, u32 count, u32 item)
{
	u32 best = 0, best_d = 0xFFFFFFFFu;
	for (u32 i = 0; i < count; i++) {
		u32 d = shown[i] > item ? shown[i] - item : item - shown[i];
		// Equal distances: the later one (shown is in order, so it comes second)
		if (d < best_d || (d == best_d && shown[i] > item)) {
			best = i;
			best_d = d;
		}
	}
	return best;
}

u32 registry_find(const char *word)
{
	for (u32 i = 0; i < NAV_ITEMS; i++)
		if (!strcmp(word, registry_items[i].word))
			return i;
	return NAV_ITEMS;
}

// A page to open at its first row, or home facing the nearest item if it's hidden
static void open_or_face(u32 item, const u8 *shown, u32 count, struct registry_place *out)
{
	u32 at = registry_index(shown, count, item);
	if (at < count) {
		out->item = (u8)item;
		out->open = 1;
	} else {
		out->item = shown[registry_nearest(shown, count, item)];
	}
}

void registry_start(u32 start_on, const struct userdata_boot *boot, const u8 *shown, u32 count,
		    struct registry_place *out)
{
	out->item = shown[0];
	out->open = 0;
	out->row = 0;
	if (start_on == SET_START_HOME)
		return;
	if (start_on != SET_START_LAST) {
		for (u32 i = 0; i < NAV_ITEMS; i++)
			if (registry_items[i].start == start_on)
				open_or_face(i, shown, count, out);
		return;
	}
	if (!boot)
		return;  // no place kept: the first item
	// Home facing the item; an older kernel's "home-" before it is dropped
	const char *word = boot->item;
	u32 prefix = sizeof(REGISTRY_OLD_HOME_PREFIX) - 1;
	if (!strncmp(word, REGISTRY_OLD_HOME_PREFIX, prefix))
		word += prefix;
	u32 item = registry_find(word);
	if (item < NAV_ITEMS)
		out->item = shown[registry_nearest(shown, count, item)];
}
