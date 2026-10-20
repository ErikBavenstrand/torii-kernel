// The UI demo, in the test kernel only: with L held at boot (mgba-shot
// ezkerneltest.gba -k 1:L) after the unit tests, or with L and SELECT
// held on the emulated cart (tests/shots/ui-*.opts) straight away, it shows a
// list of 1000 synthetic items with smooth scrolling, a scroll bar, a
// progress bar and a dialog, a grid, palette fades and theme switching,
// and DIM after 20 s idle. Keys:
//   d-pad, L/R  move and page (wrapping at the ends on a fresh press)
//   A           a dialog for the item (A or B closes it)
//   SELECT      the grid, and back
//   START       the next theme and item accent, through a fade
// It logs "screen ready: ui-demo", "ui-dialog", "ui-grid" (tests/shots
// ui-*.opts) and frame-cycles figures for scrolling, page changes, the
// dialog and switching screens (tests/perf/budget.txt).
#ifdef TORII_TEST_KERNEL
#include <gba_interrupt.h>
#include <stdio.h>
#include <string.h>

#include "torii_test.h"
#include "ui_demo.h"
#include "../../source/cartlog.h"
#include "../../source/engine/engine.h"
#include "../../source/ui/ui.h"
#include "../../source/waitcnt.h"

#define ITEMS 1000

static char name_buf[160] EWRAM_BSS;
static char detail_buf[16] EWRAM_BSS;
static char count_buf[24] EWRAM_BSS;
static char dialog_buf[48] EWRAM_BSS;
static u32 icons[4][32] EWRAM_BSS ALIGN(4);
static struct {
	const char *measure;
	u32 measure_left;
	u32 theme;
	u8 fading;
	u8 next;  // the screen to switch to after this frame: 1 list, 2 grid
	u32 dialog_item;
} demo EWRAM_BSS;

static const char *const words[] = {"Advance", "Castle", "Dragon", "Echo",   "Forest", "Golden",
				    "Harbor",  "Island", "Jungle", "Knight", "Legend", "Moon"};
static const char *const regions[] = {"(Europe)", "(USA)", "(Japan)", "(World)"};

static u32 demo_count(void *ctx)
{
	(void)ctx;
	return ITEMS;
}

// Appends s at *p (a cheap snprintf: newlib's takes thousands of cycles,
// and a real model has its names ready)
static void put(char **p, const char *s)
{
	while (*s)
		*(*p)++ = *s++;
}

static void put_num(char **p, u32 n)
{
	char digits[10];
	u32 count = 0;
	do {
		digits[count++] = (char)('0' + n % 10);
		n /= 10;
	} while (n);
	while (count)
		*(*p)++ = digits[--count];
}

static int demo_fetch(void *ctx, u32 i, struct ui_item *out)
{
	(void)ctx;
	char *p = name_buf;
	put(&p, words[i % 12]);
	put(&p, " ");
	put(&p, words[(i / 12 + 5) % 12]);
	if (i % 7 == 3) {
		put(&p, " - The Long Journey ");
		put(&p, regions[i % 4]);
		put(&p, " (En,Fr,De,Es,It) (Rev 1).gba");
	} else {
		put(&p, " ");
		put_num(&p, i + 1);
		put(&p, " ");
		put(&p, regions[i % 4]);
		put(&p, ".gba");
	}
	*p = 0;
	p = detail_buf;
	put_num(&p, 4u << (i % 4));
	put(&p, " MB");
	*p = 0;
	out->text = name_buf;
	out->detail = detail_buf;
	out->ink = i % 11 == 5 ? UI_ROLE_DIM : 0;
	out->icon = icons[i % 4];
	return UI_ITEM_READY;
}

static int grid_fetch(void *ctx, u32 i, struct ui_item *out)
{
	(void)ctx;
	char *p = name_buf;
	put(&p, words[i % 12]);
	put(&p, " ");
	put_num(&p, i + 1);
	*p = 0;
	out->text = name_buf;
	out->icon = icons[i % 4];
	return UI_ITEM_READY;
}

static const struct ui_list_model demo_model = {demo_count, demo_fetch, 0};
static const struct ui_list_model grid_model = {demo_count, grid_fetch, 0};

// The art's letters: A amber, B purple, C green, D red, E light, F dark, as
// the item's colours until the new icons replace these (ui.h roles)
static const u8 icon_ink[6] = {UI_ROLE_ACCENT, UI_ROLE_ACCENT_INK, UI_ROLE_ACCENT_INK,
			       UI_ROLE_DANGER, UI_ROLE_PANEL,      UI_ROLE_TEXT};

// 16x16 icons: a folder, a cartridge, a handheld, a disc
static void make_icons(void)
{
	static const char *const art[4] = {
		"................"
		".AAAAA.........."
		"AAAAAAA........."
		"AAAAAAAAAAAAAAA."
		"AAAAAAAAAAAAAAA."
		"AAAAAAAAAAAAAAA."
		"AAAAAAAAAAAAAAA."
		"AAAAAAAAAAAAAAA."
		"AAAAAAAAAAAAAAA."
		"AAAAAAAAAAAAAAA."
		"AAAAAAAAAAAAAAA."
		"AAAAAAAAAAAAAAA."
		"AAAAAAAAAAAAAAA."
		".AAAAAAAAAAAAA.."
		"................"
		"................",
		"................"
		"..BBBBBBBBBBBB.."
		"..BBBBBBBBBBBB.."
		"..BEEEEEEEEEEB.."
		"..BEBBBBBBBBEB.."
		"..BEBBBBBBBBEB.."
		"..BEBBBBBBBBEB.."
		"..BEEEEEEEEEEB.."
		"..BBBBBBBBBBBB.."
		"..BBBBBBBBBBBB.."
		"..BBFBFBFBFBBB.."
		"..BBFBFBFBFBBB.."
		"..BBBBBBBBBBBB.."
		"................"
		"................"
		"................",
		"................"
		"...CCCCCCCCCC..."
		"...CEEEEEEEEC..."
		"...CEFFFFFFEC..."
		"...CEFFFFFFEC..."
		"...CEFFFFFFEC..."
		"...CEEEEEEEEC..."
		"...CCCCCCCCCC..."
		"...CCFCCCCDCC..."
		"...CFFFCCDCDC..."
		"...CCFCCCCDCC..."
		"...CCCCCCCCCC..."
		"...CCCCCCCCCC..."
		"....CCCCCCCC...."
		"................"
		"................",
		"................"
		".....DDDDDD....."
		"...DDDDDDDDDD..."
		"..DDDDDDDDDDDD.."
		"..DDDDDEEDDDDD.."
		".DDDDDEEEEDDDDD."
		".DDDDEEFFEEDDDD."
		".DDDDEFFFFEDDDD."
		".DDDDEFFFFEDDDD."
		".DDDDEEFFEEDDDD."
		"..DDDDEEEEDDDD.."
		"..DDDDDEEDDDDD.."
		"...DDDDDDDDDD..."
		".....DDDDDD....."
		"................"
		"................",
	};
	for (u32 k = 0; k < 4; k++) {
		for (u32 t = 0; t < 4; t++) {
			u32 tx = t & 1, ty = t >> 1;
			for (u32 y = 0; y < 8; y++) {
				u32 row = 0;
				for (u32 x = 0; x < 8; x++) {
					char c = art[k][(ty * 8 + y) * 16 + tx * 8 + x];
					u32 index = c == '.' ? 0 : icon_ink[c - 'A'];
					row |= index << (4 * x);
				}
				icons[k][t * 8 + y] = row;
			}
		}
	}
}

static void measure(const char *name, u32 frames)
{
	demo.measure = name;
	demo.measure_left = frames;
	frame_measure(name);  // this frame too
}

// --- The list screen -----------------------------------------------------

static const struct ui_label_config title_label = {"Torii UI", UI_FONT_HEADING, UI_ALIGN_LEFT, 0};
static const struct ui_label_config count_label = {"1 / 1000", UI_FONT_BODY, UI_ALIGN_RIGHT, UI_LABEL_SWAP};
static const struct ui_label_config hint_label = {"A open  SELECT grid  START theme", UI_FONT_BODY, UI_ALIGN_LEFT, 0};
static const struct ui_list_config list_config = {&demo_model, 16, 0, UI_LIST_WRAP | UI_LIST_MARQUEE, 40};
static const struct ui_scrollbar_config bar_config = {3};

enum {
	N_TITLE_BAR,
	N_TITLE,
	N_COUNT,
	N_LIST,
	N_BAR,
	N_STATUS,
	N_HINT,
	N_PROGRESS
};

static const struct ui_spec list_nodes[] = {
	[N_TITLE_BAR] = {UI_PANEL, 1, UI_NONE, 2, UI_HFILL, 0, 0, UI_ROLE_PANEL, 0, 0, 0, 16, 0},
	[N_TITLE] = {UI_LABEL, 2, N_TITLE_BAR, 2, UI_LEFT | UI_VFILL, 0, 0, UI_ROLE_PANEL, 0, 0, 120, 0, &title_label},
	[N_COUNT] = {UI_LABEL, 3, N_TITLE_BAR, 2, UI_RIGHT | UI_VFILL, 0, UI_ROLE_DIM, UI_ROLE_PANEL, 0, 0, 104, 0,
		     &count_label},
	[N_LIST] = {UI_LIST, 4, UI_NONE, 1, UI_FILL, UI_FOCUSABLE, 0, UI_ROLE_BG, 0, 16, 8, 16, &list_config},
	[N_BAR] = {UI_SCROLLBAR, 5, UI_NONE, 2, UI_RIGHT | UI_VFILL, 0, 0, 0, 0, 16, 8, 16, &bar_config},
	[N_STATUS] = {UI_PANEL, 6, UI_NONE, 2, UI_HFILL | UI_BOTTOM, 0, 0, UI_ROLE_PANEL, 0, 0, 0, 16, 0},
	[N_HINT] = {UI_LABEL, 7, N_STATUS, 2, UI_FILL, 0, UI_ROLE_DIM, UI_ROLE_PANEL, 0, 0, 48, 0, &hint_label},
	[N_PROGRESS] = {UI_PROGRESS, 8, UI_NONE, 0, UI_RIGHT | UI_BOTTOM, 0, 0, 0, 8, 0, 40, 16, 0},
};

static int list_event(struct ui_view *v, const struct ui_event *e);
static const struct ui_view_spec list_view = {"ui-demo", list_nodes, 8, 1, N_LIST, 0, list_event};

// --- The dialog ------------------------------------------------------------

static const struct ui_label_config ask_label = {"Open this game?", UI_FONT_HEADING, UI_ALIGN_CENTER, 0};
static const struct ui_label_config name_label = {"", UI_FONT_BODY, UI_ALIGN_CENTER, 0};
static const struct ui_label_config yes_label = {"A open    B cancel", UI_FONT_BODY, UI_ALIGN_CENTER, 0};
static const struct ui_panel_config border_panel = {UI_PANEL_BORDER};

static const struct ui_spec dialog_nodes[] = {
	{UI_PANEL, 1, UI_NONE, 0, UI_HCENTER | UI_VCENTER, 0, 0, UI_ROLE_PANEL, 0, 0, 192, 80, &border_panel},
	{UI_LABEL, 2, 0, 0, UI_HFILL, 0, 0, UI_ROLE_PANEL, 8, 8, 8, 16, &ask_label},
	{UI_LABEL, 3, 0, 0, UI_HFILL, 0, 0, UI_ROLE_PANEL, 8, 32, 8, 16, &name_label},
	{UI_LABEL, 4, 0, 0, UI_HFILL, 0, UI_ROLE_DIM, UI_ROLE_PANEL, 8, 56, 8, 16, &yes_label},
};

static int dialog_event(struct ui_view *v, const struct ui_event *e)
{
	(void)v;
	if (e->type == UI_EV_KEYS && (e->keys & (INPUT_A | INPUT_B))) {
		ui_pop();
		return 1;
	}
	return 0;
}

static const struct ui_view_spec dialog_view = {"ui-dialog", dialog_nodes, 4, 2, UI_NONE, UI_VIEW_MODAL, dialog_event};

// --- The grid ----------------------------------------------------------------

static const struct ui_label_config grid_title = {"Grid", UI_FONT_HEADING, UI_ALIGN_LEFT, 0};
static const struct ui_list_config grid_config = {&grid_model, 32, 48, UI_LIST_WRAP, 0};
static const struct ui_label_config grid_hint = {"SELECT list", UI_FONT_BODY, UI_ALIGN_LEFT, 0};

static const struct ui_spec grid_nodes[] = {
	{UI_PANEL, 1, UI_NONE, 2, UI_HFILL, 0, 0, UI_ROLE_PANEL, 0, 0, 0, 16, 0},
	{UI_LABEL, 2, 0, 2, UI_FILL, 0, 0, UI_ROLE_PANEL, 0, 0, 0, 0, &grid_title},
	{UI_GRID, 3, UI_NONE, 1, UI_FILL, UI_FOCUSABLE, 0, UI_ROLE_BG, 0, 16, 0, 16, &grid_config},
	{UI_PANEL, 4, UI_NONE, 2, UI_HFILL | UI_BOTTOM, 0, 0, UI_ROLE_PANEL, 0, 0, 0, 16, 0},
	{UI_LABEL, 5, 3, 2, UI_FILL, 0, UI_ROLE_DIM, UI_ROLE_PANEL, 0, 0, 0, 0, &grid_hint},
};

static int grid_event(struct ui_view *v, const struct ui_event *e)
{
	(void)v;
	if (e->type == UI_EV_CURSOR) {
		measure("ui-grid-scroll", 16);
		return 1;
	}
	if (e->type == UI_EV_KEYS && (e->keys & (INPUT_SELECT | INPUT_B))) {
		demo.next = 1;
		return 1;
	}
	return 0;
}

static const struct ui_view_spec grid_view = {"ui-grid", grid_nodes, 5, 3, 2, 0, grid_event};

// --- Events ------------------------------------------------------------------

static const struct {
	const struct ui_theme *theme;
	u32 item;  // whose accent (enum pal_item)
} looks[] = {
	{&ui_theme_dark, UI_ITEM_DEFAULT},
	{&ui_theme_light, UI_ITEM_DEFAULT},
	{&ui_theme_dark, PAL_RECENT},
	{&ui_theme_light, PAL_GBC},
};

static int list_event(struct ui_view *v, const struct ui_event *e)
{
	switch (e->type) {
	case UI_EV_CURSOR: {
		static u32 last;
		u32 jump = e->index > last ? e->index - last : last - e->index;
		last = e->index;
		if (jump > 9)
			measure("ui-page-change", 4);
		else
			measure("ui-list-scroll", 16);
		char *p = count_buf;
		put_num(&p, e->index + 1);
		put(&p, " / ");
		put_num(&p, ITEMS);
		*p = 0;
		ui_label_set((u8)(v->first + N_COUNT), count_buf);
		ui_progress_set((u8)(v->first + N_PROGRESS), e->index + 1, ITEMS);
		return 1;
	}
	case UI_EV_ACTIVATE: {
		struct ui_item item = {0};
		demo_fetch(0, e->index, &item);
		snprintf(dialog_buf, sizeof(dialog_buf), "%s", item.text);
		struct ui_view *d = ui_push(&dialog_view, 0);
		if (d)
			ui_label_set((u8)(d->first + 2), dialog_buf);
		measure("ui-dialog-open", 3);
		return 1;
	}
	case UI_EV_KEYS:
		if (e->keys & INPUT_START) {
			if (!demo.fading) {
				ui_fade(FADE_BLACK, FADE_MAX, 6);
				demo.fading = 1;
			}
			return 1;
		}
		if (e->keys & INPUT_SELECT) {
			demo.next = 2;
			return 1;
		}
		return 0;
	default:
		return 0;
	}
}

// Fades out, switches the look, fades back in
static void theme_step(void)
{
	if (!demo.fading || ui_fading())
		return;
	demo.theme = (demo.theme + 1) % (sizeof(looks) / sizeof(looks[0]));
	ui_theme_set(looks[demo.theme].theme);
	ui_item_set(looks[demo.theme].item);
	ui_fade(FADE_BLACK, 0, 6);
	demo.fading = 0;
	tlog("ui-demo: theme %s, item %lu", ui_theme()->name, (unsigned long)ui_item());
}

static void switch_screen(void)
{
	u8 next = demo.next;
	demo.next = 0;
	ui_pop();
	if (!ui_push(next == 2 ? &grid_view : &list_view, 0))
		tlog_error("ui-demo: %s", ui_error());
	measure("ui-screen-switch", 3);
}

void ui_demo(void)
{
	REG_WAITCNT_CART = WAITCNT_MENU;  // as the menu runs
	irqInit();
	cartlog_install();
	engine_init();
	ui_init(&ui_theme_dark);
	ui_item_set(UI_ITEM_DEFAULT);
	memset(&demo, 0, sizeof(demo));
	make_icons();
	ui_dim_set_frames(20 * 60);
	if (!ui_push(&list_view, 0))
		tlog_error("ui-demo: %s", ui_error());
	for (;;) {
		if (demo.measure_left) {
			frame_measure(demo.measure);
			demo.measure_left--;
		}
		ui_frame();
		theme_step();
		if (demo.next)
			switch_screen();
	}
}
#endif
