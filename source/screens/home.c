// Home as a plain list of the items (home.h), until B12's carousel.
#include <string.h>

#include "home.h"
#include "nav.h"
#include "../lang.h"
#include "../tlog.h"
#include "../engine/engine.h"
#include "../ui/art_ids.h"
#include "../ui/ui.h"

// --- Look, px (spec.md's grid: title bar 0-18, content 20-143, hints at 147) ---
#define TITLE_H 24      // the title bar's rectangle: whole tiles under its line
#define CARD_Y 16       // the card's rectangle; its body starts 4 px in
#define CARD_H 128
#define LIST_X 8        // the list, 8 px in from the screen's edge
#define LIST_Y 24
#define LIST_W 216      // up to the scroll bar's column
#define LIST_H 112      // 8 rows of 14 px: whole rows (B9's contract)
#define LIST_PAD 4      // the text's inset in its row
#define DETAIL_W 72     // the count ("1,204 files")
#define HINTS_H 16
static const struct ui_card_config card = {4, 4, 4, 1, 0};

static struct {
	u8 items[NAV_ITEMS];
	u8 count;
	u8 first;  // the view's first node
	u32 facing;  // index in items
	struct ui_hint hints[2];
} home EWRAM_BSS;

static u32 model_count(void *ctx)
{
	(void)ctx;
	return home.count;
}

static int model_fetch(void *ctx, u32 i, struct ui_item *out)
{
	(void)ctx;
	if (i >= home.count)
		return UI_ITEM_PENDING;
	out->text = nav_item_name(home.items[i]);
	out->detail = nav_item_count(home.items[i]);
	return UI_ITEM_READY;
}

static const struct ui_list_model model = {model_count, model_fetch, 0};

enum {
	N_TITLE,
	N_HINTS,
	N_CARD,
	N_LIST,
	N_BAR,
	N_NODES
};

static const struct ui_title_config title = {"", 0, ART_ICON16_TORII};
static const struct ui_hints_config hints_config = {0, 0};  // home_enter's
static const struct ui_list_config list = {&model, UI_ROW_PITCH_LIST, UI_LIST_WRAP | UI_LIST_MARQUEE, DETAIL_W,
					   LIST_PAD};
static const struct ui_scrollbar_config bar = {N_LIST};

static const struct ui_spec nodes[N_NODES] = {
	[N_TITLE] = {UI_TITLE, 1, UI_NONE, 3, UI_HFILL, 0, 0, 0, 0, 0, 0, TITLE_H, &title},
	[N_HINTS] = {UI_HINTS, 2, UI_NONE, 3, UI_HFILL | UI_BOTTOM, 0, 0, 0, 0, 0, 0, HINTS_H, &hints_config},
	[N_CARD] = {UI_CARD, 3, UI_NONE, 2, UI_HFILL, 0, 0, 0, 0, CARD_Y, 0, CARD_H, &card},
	[N_LIST] = {UI_LIST, 4, UI_NONE, 1, UI_LEFT, UI_FOCUSABLE, 0, 0, LIST_X, LIST_Y, LIST_W, LIST_H, &list},
	[N_BAR] = {UI_SCROLLBAR, 5, UI_NONE, 2, UI_LEFT, 0, 0, UI_ROLE_PANEL, LIST_X + LIST_W, LIST_Y, 8, LIST_H, &bar},
};

static int home_event(struct ui_view *v, const struct ui_event *e);
static const struct ui_view_spec view = {"home", nodes, N_NODES, 0, N_LIST, 0, home_event};

// 1 while home's view is up (at the stack's bottom: dialogs go over it)
static int up(void)
{
	return ui_depth() && ui_view_at(0)->spec == &view;
}

// The screen takes the item's accent: the pill, the scroll bar
static void face(u32 at)
{
	home.facing = at < home.count ? at : 0;
	ui_item_set(registry_items[home.items[home.facing]].accent);
}

static int home_event(struct ui_view *v, const struct ui_event *e)
{
	(void)v;
	switch (e->type) {
	case UI_EV_CURSOR:
		face(e->index);
		frame_measure("home-move");
		return 1;
	case UI_EV_ACTIVATE:
		if (e->index < home.count)
			nav_open(home.items[e->index]);
		return 1;
	default:
		return 0;
	}
}

struct ui_view *home_enter(const u8 *items, u32 count, u32 facing)
{
	memcpy(home.items, items, count);
	home.count = (u8)count;
	struct ui_view *v = ui_push(&view, &home);
	if (!v)
		return 0;
	home.first = v->first;
	face(facing);
	home.hints[0] = (struct ui_hint){"^v", gl_hint_move};
	home.hints[1] = (struct ui_hint){"A", gl_hint_open};
	ui_hints_set((u8)(v->first + N_HINTS), home.hints, 2);
	ui_title_set_right((u8)(v->first + N_TITLE), nav_clock());
	ui_list_reset((u8)(v->first + N_LIST), home.facing);
	return v;
}

u32 home_facing(void)
{
	return home.items[home.facing < home.count ? home.facing : 0];
}

void home_items(const u8 *items, u32 count)
{
	u32 item = home_facing();
	memcpy(home.items, items, count);
	home.count = (u8)count;
	u32 at = registry_nearest(items, count, item);
	tlog_debug("home: %lu items, facing %s", (unsigned long)count, registry_items[items[at]].word);
	if (!up())
		return;
	face(at);
	ui_list_reset((u8)(home.first + N_LIST), at);
}

void home_clock(const char *text)
{
	if (up())
		ui_title_set_right((u8)(home.first + N_TITLE), text);
}
