// The UI's node pool and view arenas with the real widgets (source/ui).
//
// Like the engine's tests, these run in Diagnostics with its page held on screen,
// and they never write BG VRAM: views are pushed with
// ui_core_push (nodes, arena, allocations, motions), not ui_push, and
// nothing is drawn. They leave the engine and the UI reset.
#include <string.h>

#include "torii_test.h"
#include "tests.h"
#include "../../source/engine/engine.h"
#include "../../source/engine/motion.h"
#include "../../source/ui/ui.h"

static u32 model_items = 1000;

static u32 t_count(void *ctx)
{
	(void)ctx;
	return model_items;
}

static int t_fetch(void *ctx, u32 index, struct ui_item *out)
{
	(void)ctx;
	(void)index;
	out->text = "Item";
	return UI_ITEM_READY;
}

static const struct ui_list_model t_model = {t_count, t_fetch, 0, 0, 0};
static const struct ui_list_config t_list = {&t_model, 16, UI_LIST_WRAP, 0, 0};
static const struct ui_list_config t_tall = {&t_model, 32, 0, 0, 0};
static const struct ui_label_config t_title = {"Title", UI_FONT_HEADING, UI_ALIGN_LEFT, UI_LABEL_SWAP, 0};
static const struct ui_scrollbar_config t_bar = {2};

static const struct ui_spec main_nodes[] = {
	{UI_PANEL, 1, UI_NONE, 2, UI_HFILL, 0, 0, 0, 0, 0, 0, 16, 0},
	{UI_LABEL, 2, 0, 2, UI_FILL, 0, 0, UI_ROLE_PANEL, 0, 0, 0, 0, &t_title},
	{UI_LIST, 3, UI_NONE, 1, UI_FILL, UI_FOCUSABLE, 0, 0, 0, 16, 8, 16, &t_list},
	{UI_SCROLLBAR, 4, UI_NONE, 2, UI_RIGHT | UI_VFILL, 0, 0, 0, 0, 16, 8, 16, &t_bar},
	{UI_PROGRESS, 5, UI_NONE, 0, UI_HFILL | UI_BOTTOM, 0, 0, 0, 8, 0, 8, 8, 0},
};
static const struct ui_view_spec main_view = {"test-main", main_nodes, 5, 0, 2, 0, 0};

static const struct ui_spec tall_nodes[] = {
	{UI_LIST, 1, UI_NONE, 3, UI_FILL, UI_FOCUSABLE, 0, 0, 0, 32, 0, 0, &t_tall},
};
static const struct ui_view_spec tall_view = {"test-tall", tall_nodes, 1, 0, 0, 0, 0};

static const struct ui_label_config t_text = {"Open?", UI_FONT_BODY, UI_ALIGN_CENTER, 0, 0};
static const struct ui_spec dialog_nodes[] = {
	{UI_PANEL, 1, UI_NONE, 0, UI_HCENTER | UI_VCENTER, 0, 0, 0, 0, 0, 128, 48, 0},
	{UI_LABEL, 2, 0, 0, UI_FILL, 0, 0, UI_ROLE_PANEL, 8, 8, 8, 8, &t_text},
};
static const struct ui_view_spec dialog_view = {"test-dialog", dialog_nodes, 2, 0, UI_NONE, UI_VIEW_MODAL, 0};

static const u32 t_image_tiles[4][8] = {{0x11111111}, {0x22222222}, {0x33333333}, {0x44444444}};
static const struct ui_image_config t_image = {&t_image_tiles[0][0], 0, 0x5601};
static const struct ui_spec image_nodes[] = {
	{UI_IMAGE, 1, UI_NONE, 1, 0, 0, 0, 0, 8, 8, 16, 16, &t_image},
};
static const struct ui_view_spec image_view = {"test-image", image_nodes, 1, 0, UI_NONE, 0, 0};

static const struct ui_list_config bad_rows = {&t_model, 40, 0, 0, 0};
static const struct ui_spec bad_nodes[] = {
	{UI_LIST, 1, UI_NONE, 1, UI_FILL, 0, 0, 0, 0, 0, 0, 0, &bad_rows},
};
static const struct ui_view_spec bad_view = {"test-bad", bad_nodes, 1, 0, 0, 0, 0};

static void reset(int quiet)
{
	gfx_init();
	gfx_dirty_regs = 0;
	gfx_dirty_pal = 0;
	res_init();
	jobs_init();
	motion_reset();
	ui_core_reset();
	ui_theme_select(&ui_theme_dark);
	res_errors_quiet = (u8)quiet;
}

void test_ui_pool_and_arenas(void)
{
	reset(1);
	struct pool *bg = res_pool(RES_BG_VRAM);
	u16 free0 = pool_free_units(bg, 0);
	struct ui_view *v = ui_core_push(&main_view, 0);
	CHECK(v != 0);
	if (!v) {
		tlog_error("  %s", ui_error());
		reset(0);
		return;
	}
	CHECK_EQ(ui_nodes_used(), 5);
	CHECK(ui_arena_used() > 0 && ui_arena_used() <= UI_ARENA_BYTES);
	CHECK_EQ(motion_count(), 2);  // the list's scroll spring and its pill's
	// Tiles: the list's canvas of 29x24 tiles in character block 1 (12
	// slots of 16 px), the two label sets, the bars, and the maps (the
	// list's two screenblocks)
	u16 used = (u16)(free0 - pool_free_units(bg, 0));
	CHECK(used >= 29 * 24 + 4 * 64);
	CHECK_EQ(ui_list_cursor(2), 0);
	u32 content, view, scroll;
	ui_list_scroll_info(2, &content, &view, &scroll);
	CHECK_EQ(content, 1000 * 16);
	CHECK_EQ(view, 128);
	CHECK_EQ(scroll, 0);
	u32 arena1 = ui_arena_used();
	u8 mark1 = res_mark();

	// A label view on top, popped: its nodes, state and tiles go back
	struct ui_view *d = ui_core_push(&dialog_view, 0);
	CHECK(d != 0);
	CHECK_EQ(ui_nodes_used(), 7);
	ui_core_pop();
	CHECK_EQ(ui_arena_used(), arena1);
	CHECK_EQ(res_mark(), mark1);
	CHECK_EQ(ui_nodes_used(), 5);
	for (u32 i = 0; i < 50; i++) {
		CHECK(ui_core_push(&dialog_view, 0) != 0);
		ui_core_pop();
	}
	CHECK_EQ(res_mark(), mark1);
	CHECK_EQ(motion_count(), 2);
	CHECK_EQ(pool_free_units(bg, 0), (u16)(free0 - used));

	// A list of 32 px rows doesn't fit on top of the list: refused with a
	// readable error, leaving nothing behind
	CHECK_EQ(ui_core_push(&tall_view, 0), 0);
	CHECK(ui_error()[0] != 0);
	CHECK_EQ(res_mark(), mark1);
	CHECK_EQ(motion_count(), 2);

	// A list whose rows can't work is refused, readably, and leaves nothing
	CHECK_EQ(ui_core_push(&bad_view, 0), 0);
	CHECK(strstr(ui_error(), "8 to 32") != 0);
	CHECK_EQ(res_mark(), mark1);

	// The stack's cap
	while (ui_core_push(&dialog_view, 0))
		;
	CHECK_EQ(ui_depth(), UI_VIEW_CAP);
	CHECK(strstr(ui_error(), "stack is full") != 0);
	while (ui_depth())
		ui_core_pop();
	// The list of 32 px rows alone fits
	CHECK(ui_core_push(&tall_view, 0) != 0);
	CHECK_EQ(motion_count(), 2);
	ui_core_pop();
	CHECK_EQ(ui_depth(), 0);
	CHECK_EQ(pool_free_units(bg, 0), free0);
	CHECK_EQ(motion_count(), 0);
	gfx_queue_clear();
	reset(0);
}

// An image pushed while the VRAM queue is full is refused and leaves no
// block behind; pushed again with room, its tiles are queued, not revived
// empty from the cache.
void test_ui_image_queue_full(void)
{
	reset(1);
	struct pool *bg = res_pool(RES_BG_VRAM);
	// (fills nothing commits)
	for (u32 i = 0; i < GFX_QUEUE_CAP && gfx_queue_len() < GFX_QUEUE_CAP; i++)
		CHECK(gfx_queue_fill((void *)0x06000000, 0, 4));
	CHECK_EQ(ui_core_push(&image_view, 0), 0);
	CHECK_EQ(pool_find(bg, t_image.tag), POOL_NONE);
	CHECK_EQ(res_mark(), 0);
	gfx_queue_clear();
	CHECK(ui_core_push(&image_view, 0) != 0);
	CHECK(pool_find(bg, t_image.tag) != POOL_NONE);
	CHECK_EQ(gfx_queue_len(), 1);
	ui_core_pop();
	reset(0);
}

// A reading view (UI_LIST_SCROLL) at its last scroll: DOWN moves nothing
// and its view hears of no move (B9's review: it heard of one)
static u32 scroll_moves;
static int scroll_event(struct ui_view *v, const struct ui_event *e)
{
	(void)v;
	if (e->type != UI_EV_CURSOR)
		return 0;
	scroll_moves++;
	return 1;
}
static const struct ui_list_config t_scroll = {&t_model, 16, UI_LIST_PLAIN | UI_LIST_SCROLL, 0, 0};
static const struct ui_spec scroll_nodes[] = {
	{UI_LIST, 1, UI_NONE, 1, UI_LEFT, UI_FOCUSABLE, 0, 0, 0, 16, 64, 64, &t_scroll},
};
static const struct ui_view_spec scroll_view = {"test-scroll", scroll_nodes, 1, 0, 0, 0, scroll_event};

void test_ui_scroll_end(void)
{
	reset(1);
	model_items = 6;  // 4 rows show: the last scroll's top row is 2
	struct ui_view *v = ui_core_push(&scroll_view, 0);
	CHECK(v != 0);
	if (v) {
		u8 list = v->first;
		ui_list_set_cursor(list, 5);
		CHECK_EQ(ui_list_cursor(list), 2);
		scroll_moves = 0;
		ui_dispatch_keys(INPUT_DOWN);
		CHECK_EQ(scroll_moves, 0);
		CHECK_EQ(ui_list_cursor(list), 2);
		ui_dispatch_keys(INPUT_UP);
		CHECK_EQ(scroll_moves, 1);
		CHECK_EQ(ui_list_cursor(list), 1);
		ui_core_pop();
	}
	model_items = 1000;
	gfx_queue_clear();
	reset(0);
}
