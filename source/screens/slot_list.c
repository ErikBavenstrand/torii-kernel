// A short list of choices on a page of its own (slot_list.h): a title bar, a
// card with the rows, the hint bar. Built like the NOR page's card and list.
#include <string.h>

#include "slot_list.h"
#include "../tlog.h"
#include "../engine/input.h"
#include "../ui/art_ids.h"

// --- Look, px (spec.md's grid: title bar 0-23, content 24-143, hints at 147) ---
#define LIST_PAD 4  // a row's text inset
#define DETAIL_W 48  // a row's size ("32 KB")
static const struct slot_list_look {
	u8 title_h, hints_h;
	u8 card_y;            // the card's rectangle, under the title bar's
	u8 list_x, list_y, list_w;
	u8 list_rows;         // rows the card shows at most; more scroll
	u8 card_pad;          // px of card under the last row, at least
	struct ui_card_config card_inset;
} look = {
	.title_h = 24,
	.hints_h = 16,
	.card_y = 24,
	.list_x = 8,
	.list_y = 32,
	.list_w = 216,
	.list_rows = 4,
	.card_pad = 3,
	.card_inset = {4, 0, 4, 4, 0},
};

enum {
	N_TITLE,
	N_HINTS,
	N_CARD,
	N_SCROLL,
	N_LIST,  // last: its rows draw within the first frame's budget, after the rest
	N_NODES
};

static struct {
	const struct slot_list *list;
	struct ui_card_config card;
	u8 first;
	u8 done;
	u8 result;
	u32 row;
} sl EWRAM_BSS;

static struct ui_spec nodes[N_NODES] EWRAM_BSS;

static u32 model_count(void *ctx)
{
	(void)ctx;
	return sl.list->count;
}

static int model_fetch(void *ctx, u32 i, struct ui_item *out)
{
	(void)ctx;
	if (i >= sl.list->count)
		return UI_ITEM_PENDING;
	out->text = sl.list->rows[i].text;
	out->detail = sl.list->rows[i].detail;
	out->mark = sl.list->rows[i].mark;
	out->more = sl.list->rows[i].more;
	return UI_ITEM_READY;
}

static const struct ui_list_model model = {model_count, model_fetch, 0, 0, 0};
static const struct ui_title_config title_config = {"", 0, ART_ICON16_TORII};
static const struct ui_hints_config hints_config = {0, 0};
static const struct ui_list_config list_config = {&model, UI_ROW_PITCH_LIST, UI_LIST_MARQUEE | UI_LIST_MARKS, DETAIL_W,
						  LIST_PAD};
static const struct ui_scrollbar_config scroll_config = {N_LIST};

static int event(struct ui_view *v, const struct ui_event *e);
// (its name is the caller's, set per run: the golden shots wait for it)
static struct ui_view_spec view_spec EWRAM_BSS;

static void node(u32 i, u8 kind, u8 plane, u8 anchor, u8 flags, s32 x, s32 y, s32 w, s32 h, const void *data)
{
	nodes[i] = (struct ui_spec){kind, (u8)(i + 1), UI_NONE, plane,  anchor, flags, 0,
				    0,    (s16)x,      (s16)y,  (s16)w, (s16)h, data};
}

static void build(void)
{
	const struct slot_list_look *l = &look;
	const struct ui_look *t = &ui_theme()->look;
	u32 rows = sl.list->count > l->list_rows ? l->list_rows : sl.list->count;
	u32 rows_end = l->list_y + rows * t->list_pitch;
	u32 end = (rows_end + l->card_pad + t->shadow_offset + 7u) & ~7u;
	u32 inset = end - rows_end - l->card_pad, most = 8u - t->card_radius;
	sl.card = l->card_inset;
	sl.card.inset_b = (u8)(inset < most ? inset : most);
	TLOG_ASSERT(end <= t->content_bottom + 1u);
	node(N_TITLE, UI_TITLE, 3, UI_HFILL, 0, 0, 0, 0, l->title_h, &title_config);
	node(N_HINTS, UI_HINTS, 3, UI_HFILL | UI_BOTTOM, 0, 0, 0, 0, l->hints_h, &hints_config);
	node(N_CARD, UI_CARD, 3, UI_HFILL, 0, 0, l->card_y, 0, (s32)end - l->card_y, &sl.card);
	nodes[N_CARD].paper = UI_ROLE_PANEL;
	node(N_LIST, UI_LIST, 1, UI_LEFT, UI_FOCUSABLE, l->list_x, l->list_y, l->list_w, l->list_rows * t->list_pitch,
	     &list_config);
	node(N_SCROLL, UI_SCROLLBAR, 2, UI_LEFT, (u8)(sl.list->count > l->list_rows ? 0 : UI_HIDDEN),
	     l->list_x + l->list_w, l->list_y, 8, l->list_rows * t->list_pitch, &scroll_config);
}

static int event(struct ui_view *v, const struct ui_event *e)
{
	(void)v;
	if (e->type == UI_EV_ACTIVATE) {
		sl.row = e->index;
		sl.result = SLOT_LIST_PICK;
		sl.done = 1;
		return 1;
	}
	if (e->type == UI_EV_KEYS) {
		const InputState *in = ui_input();
		u16 pressed = e->keys & in->pressed;
		if (pressed & INPUT_B) {
			sl.result = SLOT_LIST_CANCEL;
			sl.done = 1;
			return 1;
		}
		if ((pressed & INPUT_START) && sl.list->extra) {
			sl.row = ui_list_cursor((u8)(sl.first + N_LIST));
			sl.result = SLOT_LIST_EXTRA;
			sl.done = 1;
			return 1;
		}
	}
	return 0;
}

int slot_list_run(const struct slot_list *list, u32 *row)
{
	memset(&sl, 0, sizeof(sl));
	sl.list = list;
	view_spec = (struct ui_view_spec){list->name, nodes, N_NODES, N_LIST, 0, event};
	build();
	ui_leave(UI_TR_DEEPER, 1);
	while (ui_depth())
		ui_pop();
	struct ui_view *v = ui_push(&view_spec, &sl);
	if (!v) {
		tlog_error("slot list: %s", ui_error());
		*row = 0;
		return SLOT_LIST_CANCEL;
	}
	sl.first = v->first;
	ui_title_set((u8)(sl.first + N_TITLE), list->title);
	if (list->sub)
		ui_title_set_sub((u8)(sl.first + N_TITLE), list->sub);
	ui_hints_set((u8)(sl.first + N_HINTS), list->hints, list->hint_count);
	ui_list_reset((u8)(sl.first + N_LIST), list->cursor < list->count ? list->cursor : 0);
	while (!sl.done && ui_depth())
		ui_frame();
	tlog("%s: %s row %lu", list->name, sl.result == SLOT_LIST_CANCEL ? "cancelled" : "picked",
	     (unsigned long)sl.row);
	ui_leave(UI_TR_DEEPER, -1);
	while (ui_depth())
		ui_pop();
	*row = sl.row;
	return sl.done ? sl.result : SLOT_LIST_CANCEL;
}
