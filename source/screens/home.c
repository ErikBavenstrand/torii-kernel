// Home: the menu's items on the carousel (home.h, screens/carousel.h).
#include <string.h>

#include "home.h"
#include "carousel.h"
#include "nav.h"
#include "../lang.h"
#include "../tlog.h"
#include "../ui/art.h"
#include "../ui/ui.h"

// --- Look, px (spec.md's grid: title bar 0-18, hints at 147) ---------------------
#define TITLE_H 24  // the title bar's rectangle: whole tiles under its line
#define HINTS_H 16

_Static_assert(NAV_ITEMS <= CAROUSEL_MAX, "the carousel takes every item");

static struct {
	u8 items[NAV_ITEMS];
	u8 count;
	u8 first;  // the view's first node
	u8 facing;  // the item it faced when last asked, for when it isn't up
	struct carousel_entry entries[NAV_ITEMS];
	struct ui_hint hints[2];
} home EWRAM_BSS;

enum {
	N_CAROUSEL,
	N_TITLE,
	N_HINTS,
	N_NODES
};

static const struct ui_title_config title = {"", 0, ART_ICON16_TORII};
static const struct ui_hints_config hints_config = {0, 0};  // home_enter's

// The carousel on plane 3 (its backdrop) with everything else in sprites;
// the title and hint bars over it on plane 2. Plane 0 is left to dialogs.
static const struct ui_spec nodes[N_NODES] = {
	[N_CAROUSEL] = {UI_CAROUSEL, 1, UI_NONE, 3, UI_HFILL | UI_VFILL, UI_FOCUSABLE, 0, 0, 0, 0, 0, 0, 0},
	[N_TITLE] = {UI_TITLE, 2, UI_NONE, 2, UI_HFILL, 0, 0, 0, 0, 0, 0, TITLE_H, &title},
	[N_HINTS] = {UI_HINTS, 3, UI_NONE, 2, UI_HFILL | UI_BOTTOM, 0, 0, 0, 0, 0, 0, HINTS_H, &hints_config},
};

static int home_event(struct ui_view *v, const struct ui_event *e);
static const struct ui_view_spec view = {"home", nodes, N_NODES, N_CAROUSEL, 0, home_event};

// 1 while home's view is up (at the stack's bottom: dialogs go over it)
static int up(void)
{
	return ui_depth() && ui_view_at(0)->spec == &view;
}

static int home_event(struct ui_view *v, const struct ui_event *e)
{
	(void)v;
	switch (e->type) {
	case UI_EV_CURSOR:
		// A turn came to rest: tests/nav.sh waits for it
		if (e->index < home.count)
			tlog_debug("home: facing %s", registry_items[home.items[e->index]].word);
		return 1;
	case UI_EV_ACTIVATE:
		if (e->index < home.count)
			nav_open(home.items[e->index]);
		return 1;
	default:
		return 0;
	}
}

// The carousel's entries for the items, with their names and counts
static void entries(const u8 *items, u32 count)
{
	memcpy(home.items, items, count);
	home.count = (u8)count;
	for (u32 i = 0; i < count; i++) {
		u32 item = items[i];
		home.entries[i] =
			(struct carousel_entry){nav_item_name(item), nav_item_count(item), art_items[item].sprite,
						registry_items[item].icon16, registry_items[item].accent};
	}
}

struct ui_view *home_enter(const u8 *items, u32 count, u32 facing)
{
	struct ui_view *v = ui_push(&view, &home);
	if (!v)
		return 0;
	home.first = v->first;
	entries(items, count);
	home.facing = items[facing < count ? facing : 0];
	carousel_set((u8)(v->first + N_CAROUSEL), home.entries, home.count, facing);
	home.hints[0] = (struct ui_hint){"\xC2\xAB\xC2\xBB", gl_hint_turn};  // «»
	home.hints[1] = (struct ui_hint){"A", gl_hint_open};
	ui_hints_set((u8)(v->first + N_HINTS), home.hints, 2);
	ui_title_set_right((u8)(v->first + N_TITLE), nav_clock());
	return v;
}

u32 home_facing(void)
{
	if (up() && home.count) {
		u32 at = carousel_facing((u8)(home.first + N_CAROUSEL));
		home.facing = home.items[at < home.count ? at : 0];
	}
	return home.facing;
}

void home_items(const u8 *items, u32 count, u32 face)
{
	u32 item = home_facing();
	int same = count == home.count && !memcmp(items, home.items, count) && (face >= NAV_ITEMS || face == item);
	if (face < NAV_ITEMS && registry_index(items, count, face) < count)
		item = face;
	entries(items, count);
	u32 at = registry_nearest(items, count, item);
	tlog_debug("home: %lu items, facing %s", (unsigned long)count, registry_items[items[at]].word);
	if (!up())
		return;
	// The same items: only their counts changed, and a turn goes on
	if (same)
		carousel_refresh((u8)(home.first + N_CAROUSEL));
	else
		carousel_set((u8)(home.first + N_CAROUSEL), home.entries, home.count, at);
}

int home_zoom_open(u32 item, struct ui_zoom *zoom)
{
	if (!up() || !home.count || home.items[carousel_facing((u8)(home.first + N_CAROUSEL)) % home.count] != item)
		return 0;
	return carousel_zoom((u8)(home.first + N_CAROUSEL), zoom);
}

void home_zoom_close(u32 item, struct ui_zoom *zoom)
{
	const struct carousel_entry e = {nav_item_name(item), nav_item_count(item), art_items[item].sprite,
					 registry_items[item].icon16, registry_items[item].accent};
	carousel_zoom_rest(&e, zoom);
}

void home_clock(const char *text)
{
	if (up())
		ui_title_set_right((u8)(home.first + N_TITLE), text);
}
