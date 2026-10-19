// The UI's node pool and view arenas with the real widgets (source/ui).
//
// Like the engine's tests, these run in Diagnostics while its mode 3
// screen shows, so they never write BG VRAM: views are pushed with
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

static const struct ui_list_model t_model = {t_count, t_fetch, 0};
static const struct ui_list_config t_list = {&t_model, 16, 0, UI_LIST_WRAP, 0};
static const struct ui_list_config t_grid = {&t_model, 32, 48, 0, 0};
static const struct ui_label_config t_title = {"Title", UI_FONT_HEADING, UI_ALIGN_LEFT, UI_LABEL_SWAP};
static const struct ui_scrollbar_config t_bar = {2};

static const struct ui_spec main_nodes[] = {
	{UI_PANEL, 1, UI_NONE, 2, UI_HFILL, 0, 0, 0, 0, 0, 0, 16, 0},
	{UI_LABEL, 2, 0, 2, UI_FILL, 0, 0, UI_ROLE_PANEL, 0, 0, 0, 0, &t_title},
	{UI_LIST, 3, UI_NONE, 1, UI_FILL, UI_FOCUSABLE, 0, 0, 0, 16, 8, 16, &t_list},
	{UI_SCROLLBAR, 4, UI_NONE, 2, UI_RIGHT | UI_VFILL, 0, 0, 0, 0, 16, 8, 16, &t_bar},
	{UI_PROGRESS, 5, UI_NONE, 0, UI_HFILL | UI_BOTTOM, 0, 0, 0, 8, 0, 8, 8, 0},
};
static const struct ui_view_spec main_view = {"test-main", main_nodes, 5, 0, 2, 0, 0};

static const struct ui_spec grid_nodes[] = {
	{UI_GRID, 1, UI_NONE, 3, UI_FILL, UI_FOCUSABLE, 0, 0, 0, 16, 0, 0, &t_grid},
};
static const struct ui_view_spec grid_view = {"test-grid", grid_nodes, 1, 0, 0, 0, 0};

static const struct ui_label_config t_text = {"Open?", UI_FONT_BODY, UI_ALIGN_CENTER, 0};
static const struct ui_spec dialog_nodes[] = {
	{UI_PANEL, 1, UI_NONE, 0, UI_HCENTER | UI_VCENTER, 0, 0, 0, 0, 0, 128, 48, 0},
	{UI_LABEL, 2, 0, 0, UI_FILL, 0, 0, UI_ROLE_PANEL, 8, 8, 8, 8, &t_text},
};
static const struct ui_view_spec dialog_view = {"test-dialog", dialog_nodes, 2, 0, UI_NONE, UI_VIEW_MODAL, 0};

static const struct ui_list_config bad_rows = {&t_model, 24, 0, 0, 0};
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
	ui_theme_select(&ui_theme_dark, UI_ACCENT_THEME);
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
	CHECK_EQ(motion_count(), 1);  // the list's scroll spring
	// Tiles: the list's 11 slots of 29x2 tiles in character block 1, the
	// two label sets, the bars, and three maps
	u16 used = (u16)(free0 - pool_free_units(bg, 0));
	CHECK(used >= 11 * 29 * 2 + 3 * 64);
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
	CHECK_EQ(motion_count(), 1);
	CHECK_EQ(pool_free_units(bg, 0), (u16)(free0 - used));

	// A grid of 32 px rows doesn't fit on top of the list: refused with a
	// readable error, leaving nothing behind
	CHECK_EQ(ui_core_push(&grid_view, 0), 0);
	CHECK(ui_error()[0] != 0);
	CHECK_EQ(res_mark(), mark1);
	CHECK_EQ(motion_count(), 1);

	// A list whose rows can't work is refused, readably, and leaves nothing
	CHECK_EQ(ui_core_push(&bad_view, 0), 0);
	CHECK(strstr(ui_error(), "16 or 32") != 0);
	CHECK_EQ(res_mark(), mark1);

	// The stack's cap
	while (ui_core_push(&dialog_view, 0))
		;
	CHECK_EQ(ui_depth(), UI_VIEW_CAP);
	CHECK(strstr(ui_error(), "stack is full") != 0);
	while (ui_depth())
		ui_core_pop();
	// The grid alone fits
	CHECK(ui_core_push(&grid_view, 0) != 0);
	CHECK_EQ(motion_count(), 1);
	ui_core_pop();
	CHECK_EQ(ui_depth(), 0);
	CHECK_EQ(pool_free_units(bg, 0), free0);
	CHECK_EQ(motion_count(), 0);
	gfx_queue_clear();
	reset(0);
}
