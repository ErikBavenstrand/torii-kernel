// The menu's items (v0.4 decisions, Navigation; ticket B10): one table with
// each item's word, accent, icon, console and Start on value, which items
// show on home, and where the menu starts. Hardware-free (host tests:
// tests/host/test_registry.c); screens/nav.c holds the names, counts and
// pages.
//
// Home lists the items in the carousel's order. Recent and Favourites hide
// while empty, and each console while the library found none of its games;
// SD card, NOR, Settings and Info always show.
#ifndef TORII_SCREENS_REGISTRY_H
#define TORII_SCREENS_REGISTRY_H

#include <gba_base.h>

#include "../userdata.h"

// The items, in the carousel's order (ui/art_ids.h's ART_ITEM_*)
enum nav_item {
	NAV_RECENT,
	NAV_FAV,
	NAV_GBA,
	NAV_GBC,
	NAV_GB,
	NAV_NES,
	NAV_SD,
	NAV_NOR,
	NAV_SET,
	NAV_INFO,
	NAV_ITEMS,
};

#define NAV_NO_CONSOLE 0xFF
#define NAV_NO_START 0xFF

struct registry_item {
	const char *word;  // its name in recent.txt's @boot line (userdata.h)
	u8 accent;         // enum pal_item: its colour (ui/palette.h)
	u8 icon16;         // ART_ICON16_*: the title bar's icon (ui/art_ids.h)
	u8 console;        // LIB_GBA...: the library's console, NAV_NO_CONSOLE for none
	u8 start;          // the Start on value that opens it (enum settings_start), NAV_NO_START none
	u8 hides;          // 1: hidden while its count is 0
};
extern const struct registry_item registry_items[NAV_ITEMS];

// The items home shows, from each item's count (games, files; ignored for
// items that never hide), in order into shown; returns how many (at least 4).
u32 registry_shown(const u32 counts[NAV_ITEMS], u8 shown[NAV_ITEMS]);
// Where item is in shown (count of them), or count if it isn't there.
u32 registry_index(const u8 *shown, u32 count, u32 item);
// The shown item nearest item in the carousel's order: item itself if it
// shows; else the nearest on either side, the later one on a tie. Returns
// its index in shown.
u32 registry_nearest(const u8 *shown, u32 count, u32 item);
// The item whose word is word, NAV_ITEMS if none.
u32 registry_find(const char *word);

// A place in the menu: home facing an item, or an item's page open at row
#define REGISTRY_HOME_PREFIX "home-"
struct registry_place {
	u8 item;  // enum nav_item
	u8 open;  // 1: its page is open
	u32 row;  // the page's row (open only)
};
// Where the menu starts (T19), from the Start on setting start_on (enum
// settings_start) and the boot position boot (Last place): a page that
// can't open because its item is hidden gives home facing the nearest
// item, and a place never set gives home facing the first.
void registry_start(u32 start_on, const struct userdata_boot *boot, const u8 *shown, u32 count,
		    struct registry_place *out);
// The boot position's item word for place p, into out (USERDATA_ITEM_MAX
// bytes): the item's word on its page, REGISTRY_HOME_PREFIX and it on home.
void registry_boot_word(const struct registry_place *p, char *out, u32 size);

#endif
