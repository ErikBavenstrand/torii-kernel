// The UI's hardware-free parts (source/ui): layouts and anchors, theme
// patches and palettes, the list's ring maths, the view stack and focus.
#include <string.h>

#include "../../source/engine/jobs.h"
#include "../../source/engine/res.h"
#include "../../source/ui/ring.h"
#include "../../source/ui/ui.h"
#include "../kernel/torii_test.h"
#include "tests.h"

// --- Stand-ins for the kernel side ------------------------------------------

static u32 wakes, popped_hooks, cancelled_owner;

void ui_wake(void)
{
	wakes++;
}

void ui_hook_popped(void)
{
	popped_hooks++;
}

void jobs_cancel_owner(u8 owner)
{
	cancelled_owner = owner;
}

// Test widgets: every kind has 12 bytes of state, takes 4 tiles, and kind
// UI_LIST takes keys (consuming DOWN), UI_PANEL consumes A
static u32 inits, init_fail_at = 0xFFFFFFFF;
static u8 last_event_node = UI_NONE;

static int t_measure(u8 node, struct engine_budget *need)
{
	(void)node;
	need->bg_tiles = (u16)(need->bg_tiles + 4);
	return 1;
}

static int t_init(u8 node)
{
	(void)node;
	if (inits++ == init_fail_at) {
		ui_set_error("test init failed");
		return 0;
	}
	// Each node takes a tagless tile block and a motion, freed by the pop
	if (res_bg_tiles(4, 0, 0, 0) == RES_NONE)
		return 0;
	return motion_spring(MOTION_TO_NOTHING, 0, SPRING_FAST, ui_owner(ui_node_view(node))) != MOTION_NONE;
}

static int t_event_list(u8 node, const struct ui_event *e)
{
	if (e->type != UI_EV_KEYS)
		return 0;
	last_event_node = node;
	if (e->keys & INPUT_A)
		return ui_emit(node, UI_EV_ACTIVATE, 7);
	return (e->keys & INPUT_DOWN) != 0;
}

static int t_event_panel(u8 node, const struct ui_event *e)
{
	(void)node;
	return e->type == UI_EV_KEYS && (e->keys & INPUT_SELECT);
}

static const struct ui_ops t_plain = {12, t_measure, t_init, 0, 0, 0};
static const struct ui_ops t_list = {12, t_measure, t_init, t_event_list, 0, 0};
static const struct ui_ops t_panel = {12, t_measure, t_init, t_event_panel, 0, 0};
const struct ui_ops *const ui_kind_ops[UI_KINDS] = {
	[UI_LABEL] = &t_plain, [UI_PANEL] = &t_panel,     [UI_LIST] = &t_list,      [UI_GRID] = &t_list,
	[UI_IMAGE] = &t_plain, [UI_SCROLLBAR] = &t_plain, [UI_PROGRESS] = &t_plain,
};

static void reset_all(void)
{
	res_init();
	motion_reset();
	ui_core_reset();
	ui_theme_select(&ui_theme_dark, UI_ACCENT_THEME);
	inits = 0;
	init_fail_at = 0xFFFFFFFF;
	wakes = popped_hooks = cancelled_owner = 0;
}

// --- Layouts --------------------------------------------------------------

static struct ui_node lay(u8 anchor, s16 x, s16 y, s16 w, s16 h, const struct ui_node *parent)
{
	struct ui_spec s = {UI_LABEL, 0, UI_NONE, 0, anchor, 0, 0, 0, x, y, w, h, 0};
	struct ui_node out;
	memset(&out, 0, sizeof(out));
	ui_layout(&s, 0, parent, &out);
	return out;
}

void test_ui_layout_anchors(void)
{
	reset_all();
	struct ui_node n = lay(UI_LEFT | UI_TOP, 8, 16, 64, 24, 0);
	CHECK_EQ(n.x, 8);
	CHECK_EQ(n.y, 16);
	CHECK_EQ(n.w, 64);
	CHECK_EQ(n.h, 24);
	// Right and bottom: x and y are margins from the far edges
	n = lay(UI_RIGHT | UI_BOTTOM, 8, 16, 64, 24, 0);
	CHECK_EQ(n.x, 240 - 8 - 64);
	CHECK_EQ(n.y, 160 - 16 - 24);
	// Fill: w and h are the far margins
	n = lay(UI_FILL, 8, 16, 24, 32, 0);
	CHECK_EQ(n.x, 8);
	CHECK_EQ(n.w, 240 - 8 - 24);
	CHECK_EQ(n.y, 16);
	CHECK_EQ(n.h, 160 - 16 - 32);
	// Centred, then moved
	n = lay(UI_HCENTER | UI_VCENTER, 8, -8, 80, 40, 0);
	CHECK_EQ(n.x, (240 - 80) / 2 + 8);
	CHECK_EQ(n.y, (160 - 40) / 2 - 8);
	// Relative to a parent
	struct ui_node parent = lay(UI_LEFT | UI_TOP, 40, 32, 160, 96, 0);
	n = lay(UI_RIGHT | UI_VFILL, 8, 8, 32, 8, &parent);
	CHECK_EQ(n.x, 40 + 160 - 8 - 32);
	CHECK_EQ(n.y, 32 + 8);
	CHECK_EQ(n.h, 96 - 8 - 8);
}

static const struct ui_theme patched_theme = {
	.name = "Patched",
	.body = &font_body,
	.heading = &font_heading,
	.row_h = 16,
	.patch_count = 5,
	.patches =
		{
			{9, 3, UI_FIELD_X, 0, 24},
			{9, 3, UI_FIELD_ANCHOR, 0, UI_RIGHT},
			{9, 3, UI_FIELD_X, 0, 16},  // the last wins
			{9, 4, UI_FIELDS, 0, 1},    // outside the closed set: ignored
			{8, 3, UI_FIELD_W, 0, 8},   // another view
		},
};

void test_ui_patches(void)
{
	reset_all();
	s16 v = 0;
	CHECK(ui_theme_patch(&patched_theme, 9, 3, UI_FIELD_X, &v));
	CHECK_EQ(v, 16);
	CHECK(!ui_theme_patch(&patched_theme, 9, 4, UI_FIELDS, &v));
	CHECK(!ui_theme_patch(&patched_theme, 9, 3, UI_FIELD_Y, &v));
	CHECK(!ui_theme_patch(&ui_theme_dark, 9, 3, UI_FIELD_X, &v));
	// Applied by the layout: node 3 of view 9 moves to the right edge
	ui_theme_select(&patched_theme, UI_ACCENT_THEME);
	struct ui_spec s = {UI_LABEL, 3, UI_NONE, 0, UI_LEFT, 0, 0, 0, 8, 8, 64, 16, 0};
	struct ui_node out;
	ui_layout(&s, 9, 0, &out);
	CHECK_EQ(out.x, 240 - 16 - 64);
	CHECK_EQ(out.w, 64);
	ui_layout(&s, 8, 0, &out);  // view 8 patches only the width
	CHECK_EQ(out.x, 8);
	CHECK_EQ(out.w, 8);
	s.id = 0;  // no id: never patched
	ui_layout(&s, 9, 0, &out);
	CHECK_EQ(out.x, 8);
	ui_theme_select(&ui_theme_dark, UI_ACCENT_THEME);
}

void test_ui_palette(void)
{
	u16 pal[32];
	const struct ui_theme *t = &ui_theme_dark;
	ui_theme_palette(t, UI_ACCENT_THEME, pal);
	CHECK_EQ(pal[0], t->colour[UI_C_BG]);  // the backdrop
	CHECK_EQ(pal[UI_ROLE_TEXT], t->colour[UI_C_TEXT]);
	CHECK_EQ(pal[UI_ROLE_SHADOW], t->colour[UI_C_SHADOW]);
	CHECK_EQ(pal[UI_ROLE_ICON + 5], t->colour[UI_C_ICON + 5]);
	// Bank 1: the same roles on the selection
	CHECK_EQ(pal[16 + UI_ROLE_BG], t->colour[UI_C_SELECTION]);
	CHECK_EQ(pal[16 + UI_ROLE_PANEL], t->colour[UI_C_SELECTION]);
	CHECK_EQ(pal[16 + UI_ROLE_TEXT], t->colour[UI_C_SELECTION_TEXT]);
	CHECK_EQ(pal[16 + UI_ROLE_TRACK], t->colour[UI_C_TRACK]);
	CHECK(pal[16 + UI_ROLE_DIM] != t->colour[UI_C_SELECTION_TEXT]);
	// An accent swaps the accent and the selection, nothing else
	u16 acc[32];
	ui_theme_palette(t, 0x001F, acc);
	CHECK_EQ(acc[UI_ROLE_ACCENT], 0x001F);
	CHECK_EQ(acc[16 + UI_ROLE_BG], 0x001F);
	CHECK_EQ(acc[UI_ROLE_TEXT], pal[UI_ROLE_TEXT]);
	CHECK_EQ(acc[UI_ROLE_BG], pal[UI_ROLE_BG]);
	for (u32 i = 0; i < ui_accent_count; i++)
		CHECK(ui_accents[i].name && !(ui_accents[i].colour & 0x8000));
	ui_theme_palette(&ui_theme_light, UI_ACCENT_THEME, pal);
	CHECK_EQ(pal[0], ui_theme_light.colour[UI_C_BG]);
}

// --- The ring -------------------------------------------------------------

void test_ui_ring_setup(void)
{
	struct ui_ring r;
	const char *why;
	CHECK(!ring_setup(&r, 128, 24, &why) && why);
	CHECK(!ring_setup(&r, 12, 16, &why) && why);
	CHECK(!ring_setup(&r, 168, 16, &why) && why);
	CHECK(ring_setup(&r, 128, 16, &why));
	CHECK_EQ(r.visible, 9);  // a row cut at each end
	CHECK_EQ(r.k, 11);
	CHECK_EQ(r.ring_rows, 16);
	CHECK(ring_setup(&r, 160, 16, &why));
	CHECK_EQ(r.k, 13);
	CHECK(ring_setup(&r, 160, 32, &why));
	CHECK_EQ(r.k, 8);
	CHECK_EQ(r.ring_rows, 8);
	CHECK(ring_setup(&r, 120, 16, &why));
	CHECK_EQ(r.visible, 9);
	CHECK_EQ(ring_map_row(&r, 17), 2);  // row 17 is ring row 1: map rows 2-3
	CHECK_EQ(ring_rows_of(10, 3), 4);
	CHECK_EQ(ring_rows_of(0, 3), 0);
}

void test_ui_ring_windows(void)
{
	struct ui_ring r;
	const char *why;
	ring_setup(&r, 128, 16, &why);
	u32 lo, hi;
	ring_resident(&r, 0, 0, 1000, &lo, &hi);
	CHECK_EQ(lo, 0);
	CHECK_EQ(hi, 7);
	ring_resident(&r, 8, 1, 1000, &lo, &hi);  // misaligned, moving down
	CHECK_EQ(lo, 0);
	CHECK_EQ(hi, 9);
	ring_resident(&r, 32, -1, 1000, &lo, &hi);
	CHECK_EQ(lo, 1);
	CHECK_EQ(hi, 9);
	ring_resident(&r, 0, 1, 3, &lo, &hi);  // a short list
	CHECK_EQ(lo, 0);
	CHECK_EQ(hi, 2);
	ring_resident(&r, 0, 0, 0, &lo, &hi);
	CHECK(lo > hi);
	CHECK_EQ(ring_max_scroll(&r, 3), 0);
	CHECK_EQ(ring_max_scroll(&r, 1000), 16000 - 128);
	// Prefetch: k more each side, clamped
	u32 plo, phi;
	ring_prefetch(&r, 2, 10, 1000, &plo, &phi);
	CHECK_EQ(plo, 0);
	CHECK_EQ(phi, 21);
	ring_prefetch(&r, 500, 509, 505, &plo, &phi);
	CHECK_EQ(plo, 489);
	CHECK_EQ(phi, 504);
	// The cursor's scroll keeps a row of context, moving as little as it can
	CHECK_EQ(ring_scroll_for(&r, 3, 0, 1000, 1), 0);
	CHECK_EQ(ring_scroll_for(&r, 7, 0, 1000, 1), 16 * 9 - 128);
	CHECK_EQ(ring_scroll_for(&r, 0, 160, 1000, 1), 0);
	CHECK_EQ(ring_scroll_for(&r, 10, 160, 1000, 1), 144);
	CHECK_EQ(ring_scroll_for(&r, 999, 0, 1000, 1), 16000 - 128);
	// Far: more than visible + 1 rows away
	CHECK(!ring_far(&r, 0, 160));
	CHECK(ring_far(&r, 0, 161));
	CHECK(ring_far(&r, 16000, 0));
	// One row a frame at most
	CHECK_EQ(ring_step(&r, 0, 100), 16);
	CHECK_EQ(ring_step(&r, 100, 0), 84);
	CHECK_EQ(ring_step(&r, 5, 9), 9);
	// Rebasing keeps the spring near its base, in whole rows
	CHECK_EQ(ring_rebase(&r, 2048), 0);
	CHECK_EQ(ring_rebase(&r, -2048), 0);
	CHECK_EQ(ring_rebase(&r, 2056), 2048);
	CHECK_EQ(ring_rebase(&r, -3000), -2992);
}

// Scrolling at random, one ring_step a frame: the rows on screen before a
// frame and the rows resident after it never share a slot or a ring row,
// so a row is only ever drawn into tiles and map rows not on screen.
void test_ui_ring_invariant(void)
{
	static const u32 heights[] = {128, 160, 120, 64, 16};
	static const u32 rhs[] = {16, 16, 16, 32, 16};
	u32 seed = 12345;
	for (u32 g = 0; g < 5; g++) {
		struct ui_ring r;
		const char *why;
		CHECK(ring_setup(&r, heights[g], rhs[g], &why));
		u32 rows = 700;
		s32 max = ring_max_scroll(&r, rows), shown = 0, want = 0;
		u16 slot_row[UI_RING_MAX_SLOTS];
		for (u32 s = 0; s < UI_RING_MAX_SLOTS; s++)
			slot_row[s] = UI_ITEM_NONE;
		for (u32 frame = 0; frame < 20000; frame++) {
			seed = seed * 1103515245 + 12345;
			if (!(seed >> 16 & 15))
				want = (s32)((seed >> 4) % (u32)(max + 1));
			// The rows on screen now (before this frame's commit)
			u32 on_lo = (u32)shown / r.rh, on_hi = ((u32)shown + r.h - 1) / r.rh;
			s32 next = ring_step(&r, shown, want);
			int dir = next > shown ? 1 : next < shown ? -1 : 0;
			u32 lo, hi;
			ring_resident(&r, next, dir, rows, &lo, &hi);
			CHECK(hi - lo < r.k);
			for (u32 row = lo; row <= hi; row++) {
				u32 s = ring_slot(&r, row);
				if (slot_row[s] != row) {
					// A row given a slot now: the slot's old row and
					// its ring row must be off screen
					u32 old = slot_row[s];
					CHECK(old == UI_ITEM_NONE || old < on_lo || old > on_hi);
					for (u32 on = on_lo; on <= on_hi; on++)
						CHECK(on == row || ring_map_row(&r, on) != ring_map_row(&r, row));
					slot_row[s] = (u16)row;
				}
			}
			shown = next;
			if (torii_test_failures > 20)
				return;
		}
	}
}

// --- The stack ------------------------------------------------------------

static int view_events, view_activate_index;
static u8 view_keys;

static int on_event(struct ui_view *v, const struct ui_event *e)
{
	(void)v;
	view_events++;
	if (e->type == UI_EV_ACTIVATE) {
		view_activate_index = (int)e->index;
		ui_pop();  // waits for the end of the event phase
		return 1;
	}
	if (e->type == UI_EV_KEYS)
		view_keys = (u8)e->keys;
	return e->type == UI_EV_KEYS && (e->keys & INPUT_B);
}

static const struct ui_spec main_nodes[] = {
	{UI_PANEL, 1, UI_NONE, 2, UI_FILL, 0, 0, 0, 0, 0, 0, 0, 0},
	{UI_LABEL, 2, 0, 2, UI_HFILL, 0, 0, 0, 8, 0, 8, 16, 0},
	{UI_LIST, 3, 0, 1, UI_FILL, UI_FOCUSABLE, 0, 0, 0, 16, 0, 16, 0},
	{UI_LIST, 4, 0, 3, UI_LEFT, UI_FOCUSABLE | UI_HIDDEN, 0, 0, 0, 0, 8, 8, 0},
	{UI_GRID, 5, 0, 3, UI_LEFT, UI_FOCUSABLE, 0, 0, 0, 0, 8, 8, 0},
};
static const struct ui_view_spec main_view = {"main", main_nodes, 5, 1, 2, 0, on_event};

static const struct ui_spec dialog_nodes[] = {
	{UI_PANEL, 1, UI_NONE, 0, UI_HCENTER | UI_VCENTER, 0, 0, 0, 0, 0, 160, 64, 0},
	{UI_LABEL, 2, 0, 0, UI_HFILL, 0, 0, 0, 8, 8, 8, 16, 0},
};
static const struct ui_view_spec dialog_view = {"dialog", dialog_nodes, 2, 2, UI_NONE, UI_VIEW_MODAL, on_event};

static const struct ui_spec bad_rect_nodes[] = {
	{UI_LABEL, 1, UI_NONE, 0, UI_LEFT, 0, 0, 0, 4, 0, 16, 16, 0},
};
static const struct ui_view_spec bad_rect_view = {"bad-rect", bad_rect_nodes, 1, 0, UI_NONE, 0, 0};
static const struct ui_spec bad_parent_nodes[] = {
	{UI_LABEL, 1, 1, 0, UI_LEFT, 0, 0, 0, 0, 0, 16, 16, 0},
	{UI_LABEL, 2, UI_NONE, 0, UI_LEFT, 0, 0, 0, 0, 0, 16, 16, 0},
};
static const struct ui_view_spec bad_parent_view = {"bad-parent", bad_parent_nodes, 2, 0, UI_NONE, 0, 0};

void test_ui_stack(void)
{
	reset_all();
	res_errors_quiet = 1;
	u8 mark0 = res_mark();
	u16 free0 = pool_free_units(res_pool(RES_BG_VRAM), 0);
	struct ui_view *v = ui_core_push(&main_view, 0);
	CHECK(v != 0);
	CHECK_EQ(ui_depth(), 1);
	CHECK_EQ(ui_nodes_used(), 5);
	CHECK_EQ(ui_arena_used(), 5 * 12);
	CHECK_EQ(v->planes, 0x0E);
	CHECK(v->maps[0] == RES_NONE && v->maps[1] != RES_NONE && v->maps[3] != RES_NONE);
	CHECK_EQ(motion_count(), 5);
	// Layout: the list fills the panel under a 16 px header
	struct ui_node *list = ui_node(2);
	CHECK_EQ(list->y, 16);
	CHECK_EQ(list->h, 160 - 32);
	CHECK_EQ(list->parent, 0);
	CHECK_EQ(ui_node(0)->child, 1);
	CHECK_EQ(ui_node(1)->next, 2);
	CHECK_EQ(ui_node(4)->next, UI_NONE);
	CHECK(ui_node(3)->flags & UI_N_HIDDEN);
	CHECK_EQ(v->focus, 2);
	CHECK(list->flags & UI_N_FOCUS);
	CHECK_EQ(ui_find(v, 5), 4);
	CHECK_EQ(ui_find(v, 99), UI_NONE);
	CHECK_EQ(ui_node_spec(4)->kind, UI_GRID);
	CHECK(ui_node_state(1) == (u8 *)ui_node_state(0) + 12);

	// A modal dialog on top, then popped: everything it took comes back
	u8 mark1 = res_mark();
	u32 arena1 = ui_arena_used();
	struct ui_view *d = ui_core_push(&dialog_view, 0);
	CHECK(d != 0);
	CHECK_EQ(ui_top(), d);
	CHECK_EQ(ui_node(5)->x, (240 - 160) / 2);
	CHECK_EQ(ui_node(6)->w, 160 - 16);
	CHECK_EQ(d->focus, UI_NONE);
	CHECK_EQ(motion_count(), 7);
	ui_pop();
	CHECK_EQ(popped_hooks, 1);
	CHECK_EQ(cancelled_owner, ui_owner(d));
	CHECK_EQ(ui_depth(), 1);
	CHECK_EQ(res_mark(), mark1);
	CHECK_EQ(ui_arena_used(), arena1);
	CHECK_EQ(motion_count(), 5);

	// Caps: a failed push leaves nothing behind
	for (u32 i = 1; i < UI_VIEW_CAP; i++)
		CHECK(ui_core_push(&dialog_view, 0) != 0);
	CHECK_EQ(ui_core_push(&dialog_view, 0), 0);
	CHECK(strstr(ui_error(), "stack is full") != 0);
	while (ui_depth() > 1)
		ui_core_pop();
	CHECK_EQ(ui_core_push(&bad_rect_view, 0), 0);
	CHECK(strstr(ui_error(), "whole tiles") != 0);
	CHECK_EQ(ui_core_push(&bad_parent_view, 0), 0);
	CHECK(strstr(ui_error(), "parents come first") != 0);
	init_fail_at = inits + 1;  // the dialog's second node fails
	CHECK_EQ(ui_core_push(&dialog_view, 0), 0);
	CHECK(strstr(ui_error(), "test init failed") != 0);
	CHECK_EQ(ui_depth(), 1);
	CHECK_EQ(res_mark(), mark1);
	CHECK_EQ(ui_arena_used(), arena1);
	CHECK_EQ(ui_nodes_used(), 5);
	CHECK_EQ(motion_count(), 5);
	init_fail_at = 0xFFFFFFFF;
	// Nodes: 5 used, so 19 dialogs' worth of nodes don't fit
	static struct ui_spec many[UI_NODE_CAP];
	for (u32 i = 0; i < UI_NODE_CAP; i++) {
		struct ui_spec s = {UI_LABEL, 0, UI_NONE, 0, UI_LEFT, 0, 0, 0, 0, 0, 8, 8, 0};
		many[i] = s;
	}
	static const struct ui_view_spec many_view = {"many", many, UI_NODE_CAP, 0, UI_NONE, 0, 0};
	CHECK_EQ(ui_core_push(&many_view, 0), 0);
	CHECK(strstr(ui_error(), "nodes") != 0);

	ui_core_pop();
	CHECK_EQ(ui_depth(), 0);
	CHECK_EQ(res_mark(), mark0);
	CHECK_EQ(pool_free_units(res_pool(RES_BG_VRAM), 0), free0);
	CHECK_EQ(motion_count(), 0);
	res_errors_quiet = 0;
}

void test_ui_focus_and_keys(void)
{
	reset_all();
	struct ui_view *v = ui_core_push(&main_view, 0);
	CHECK(v != 0);
	// Focus moves over focusable shown nodes only, wrapping
	CHECK_EQ(ui_focus_next(1), 4);  // node 3 is hidden
	CHECK(!(ui_node(2)->flags & UI_N_FOCUS));
	CHECK(ui_node(4)->flags & UI_N_FOCUS);
	CHECK_EQ(ui_focus_next(1), 2);
	CHECK_EQ(ui_focus_next(-1), 4);
	ui_show(3, 1);
	CHECK_EQ(ui_focus_next(-1), 3);
	ui_focus(2);
	CHECK_EQ(v->focus, 2);

	// Keys: the focused list takes DOWN; the panel (its parent) SELECT; the
	// view's handler gets the rest
	view_events = 0;
	CHECK(ui_dispatch_keys(INPUT_DOWN));
	CHECK_EQ(last_event_node, 2);
	CHECK_EQ(view_events, 0);
	CHECK(ui_dispatch_keys(INPUT_SELECT));
	CHECK_EQ(view_events, 0);
	CHECK(ui_dispatch_keys(INPUT_B));
	CHECK_EQ(view_events, 1);
	CHECK_EQ(view_keys, INPUT_B);
	CHECK(!ui_dispatch_keys(INPUT_START));
	CHECK(!ui_dispatch_keys(0));
	// Only the top view gets keys: a dialog without focus sends them to
	// its handler
	CHECK(ui_core_push(&dialog_view, 0) != 0);
	last_event_node = UI_NONE;
	ui_dispatch_keys(INPUT_DOWN);
	CHECK_EQ(last_event_node, UI_NONE);
	ui_core_pop();
	// A pop asked for during dispatch waits for its end
	CHECK(ui_core_push(&dialog_view, 0) != 0);
	ui_core_pop();
	CHECK(ui_dispatch_keys(INPUT_A));  // the list emits ACTIVATE 7, the handler pops
	CHECK_EQ(view_activate_index, 7);
	CHECK_EQ(ui_depth(), 1);
	ui_apply_pops();
	CHECK_EQ(ui_depth(), 0);
	CHECK_EQ(motion_count(), 0);
}
