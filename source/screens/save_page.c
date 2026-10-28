// The save screens' page (save_page.h): two cards, the list on the left one
// and the row's facts on the right one, in labels as the list with art's
// facts are. Plane 1 holds the list alone; the cards and labels are on plane
// 3 with the title, as the NOR page's, so plane 2 (block 0) keeps its tiles
// for a popup beside the row and a confirmation or dialog over it.
#include <stdio.h>
#include <string.h>

#include "save_page.h"
#include "../lang.h"
#include "../userdata.h"
#include "../tlog.h"
#include "../engine/input.h"
#include "../ui/art_ids.h"

// --- Look, px (spec.md's grid: title bar 0-18, content 20-143, hints at 147) ---
#define TITLE_H 24
#define HINTS_H 16
#define CARD_Y 24  // both cards' rectangles, under the title bar's tiles
#define CARD_H 120
#define LCARD_W 112  // the list's card: body x 4-108
#define RCARD_X 112  // the pane's card: body x 112-236
#define RCARD_W 128
#define LIST_X 8
#define LIST_Y 24
#define LIST_W 88
#define LIST_H 112  // 8 rows of 14
#define LIST_PAD 4
#define BAR_X (LIST_X + LIST_W)
#define PANE_X 120  // the pane's texts, 8 px into its card
#define PANE_W 112
#define HEAD_Y 24  // the name, then the tag's line under it
#define TAG_Y 40
#define FACTS_Y 56
#define FACTS_H 32  // two lines
#define KEYS_W 56
#define NOTE_Y 104
#define NOTE_H 32  // two lines

enum {
	N_TITLE,
	N_HINTS,
	N_LCARD,
	N_RCARD,
	N_BAR,
	N_HEAD,
	N_TAG,
	N_KEYS,
	N_VALUES,
	N_NOTE,
	N_LIST,  // last: its rows draw within the first frame's budget, after the rest
	N_COUNT
};

static struct {
	const struct save_page *p;
	struct save_pane pane;
	u8 first;  // the view's first node; 0 while no page is up
	u8 up;
	u16 key;   // the key that ended the wait, 0 none yet
	u32 cursor;
	const struct save_page_row *rows;
	u32 count;
} pg EWRAM_BSS;

static u8 node(u32 i)
{
	return (u8)(pg.first + i);
}

/* ---- The list ---- */

static u32 model_count(void *ctx)
{
	(void)ctx;
	return pg.count;
}

static int model_fetch(void *ctx, u32 i, struct ui_item *out)
{
	(void)ctx;
	if (i >= pg.count)
		return UI_ITEM_PENDING;
	out->text = pg.rows[i].text;
	out->mark = pg.rows[i].mark;
	out->ink = pg.rows[i].ink;
	return UI_ITEM_READY;
}

static const struct ui_list_model model = {model_count, model_fetch, 0, 0, 0};
static const struct ui_list_config list_config = {&model, UI_ROW_PITCH_LIST,
						  UI_LIST_WRAP | UI_LIST_MARQUEE | UI_LIST_MARKS, 0, LIST_PAD};
static const struct ui_title_config title_config = {"", "", ART_ICON16_TORII};
static const struct ui_hints_config hints_config = {0, 0};
static const struct ui_card_config lcard = {4, 0, 4, 4, 0};
static const struct ui_card_config rcard = {0, 0, 4, 4, 0};
static const struct ui_scrollbar_config bar = {N_LIST};
static const struct ui_label_config head_label = {"", UI_FONT_HEADING, UI_ALIGN_LEFT, UI_LABEL_SWAP, 0};
// One tile set for the texts that change seldom (the tag, the keys, the
// note): plane 3's tiles are the title's and hints' too
static const struct ui_label_config tag_label = {"", UI_FONT_BODY, UI_ALIGN_LEFT, 0, 0};
static const struct ui_label_config keys_label = {"", UI_FONT_BODY, UI_ALIGN_LEFT, 0, 0};
static const struct ui_label_config values_label = {"", UI_FONT_BODY, UI_ALIGN_RIGHT, UI_LABEL_SWAP, 0};
static const struct ui_label_config note_label = {"", UI_FONT_BODY, UI_ALIGN_LEFT, 0, 0};

static const struct ui_spec layout[N_COUNT] = {
	[N_TITLE] = {UI_TITLE, 1, UI_NONE, 3, UI_HFILL, 0, 0, 0, 0, 0, 0, TITLE_H, &title_config},
	[N_HINTS] = {UI_HINTS, 2, UI_NONE, 3, UI_HFILL | UI_BOTTOM, 0, 0, 0, 0, 0, 0, HINTS_H, &hints_config},
	[N_LCARD] = {UI_CARD, 3, UI_NONE, 3, UI_LEFT, 0, 0, 0, 0, CARD_Y, LCARD_W, CARD_H, &lcard},
	[N_RCARD] = {UI_CARD, 4, UI_NONE, 3, UI_LEFT, 0, 0, 0, RCARD_X, CARD_Y, RCARD_W, CARD_H, &rcard},
	[N_BAR] = {UI_SCROLLBAR, 5, UI_NONE, 2, UI_LEFT, 0, 0, UI_ROLE_PANEL, BAR_X, LIST_Y, 8, LIST_H, &bar},
	[N_HEAD] = {UI_LABEL, 6, UI_NONE, 3, UI_LEFT, 0, UI_ROLE_TEXT, UI_ROLE_PANEL, PANE_X, HEAD_Y, PANE_W, 16,
		    &head_label},
	[N_TAG] = {UI_LABEL, 7, UI_NONE, 3, UI_LEFT, 0, UI_ROLE_ACCENT_INK, UI_ROLE_PANEL, PANE_X, TAG_Y, PANE_W, 16,
		   &tag_label},
	[N_KEYS] = {UI_LABEL, 8, UI_NONE, 3, UI_LEFT, 0, UI_ROLE_FAINT, UI_ROLE_PANEL, PANE_X, FACTS_Y, KEYS_W, FACTS_H,
		    &keys_label},
	[N_VALUES] = {UI_LABEL, 9, UI_NONE, 3, UI_LEFT, 0, UI_ROLE_DIM, UI_ROLE_PANEL, PANE_X + KEYS_W, FACTS_Y,
		      PANE_W - KEYS_W, FACTS_H, &values_label},
	[N_NOTE] = {UI_LABEL, 10, UI_NONE, 3, UI_LEFT, 0, UI_ROLE_FAINT, UI_ROLE_PANEL, PANE_X, NOTE_Y, PANE_W, NOTE_H,
		    &note_label},
	[N_LIST] = {UI_LIST, 11, UI_NONE, 1, UI_LEFT, UI_FOCUSABLE, 0, 0, LIST_X, LIST_Y, LIST_W, LIST_H, &list_config},
};

/* ---- The pane ---- */

static void show_row(void)
{
	const struct save_page *p = pg.p;
	memset(&pg.pane, 0, sizeof(pg.pane));
	if (p->pane && pg.cursor < pg.count)
		p->pane(p->ctx, pg.cursor, &pg.pane);
	ui_label_set(node(N_HEAD), pg.pane.head ? pg.pane.head : "");
	ui_label_set(node(N_TAG), pg.pane.tag ? pg.pane.tag : "");
	ui_label_set(node(N_KEYS), pg.pane.keys ? pg.pane.keys : p->keys ? p->keys : "");
	ui_label_set(node(N_VALUES), pg.pane.values ? pg.pane.values : "");
	ui_label_set(node(N_NOTE), pg.pane.note ? pg.pane.note : "");
	const struct ui_hint *keys = 0;
	u32 n = 0;
	if (p->hints)
		p->hints(p->ctx, pg.cursor, &keys, &n);
	ui_hints_set(node(N_HINTS), keys, n > 4 ? 4 : n);
}

static int event(struct ui_view *v, const struct ui_event *e)
{
	(void)v;
	switch (e->type) {
	case UI_EV_CURSOR:
		pg.cursor = e->index;
		show_row();
		return 1;
	case UI_EV_ACTIVATE:
		pg.cursor = e->index;
		pg.key = INPUT_A;
		return 1;
	case UI_EV_KEYS: {
		u16 pressed = e->keys & ui_input()->pressed;
		if (pressed & INPUT_B) {
			pg.key = INPUT_B;
			return 1;
		}
		u16 k = pressed & pg.p->end_keys;
		if (k && !ui_list_paging(node(N_LIST))) {
			pg.key = (u16)(k & -k);  // the lowest: one key
			return 1;
		}
		return 0;
	}
	default:
		return 0;
	}
}

// The layout as a page shows it: its name is the caller's, and when the rows
// fit, the scroll bar's place is the card's paper (a hidden node on the
// card's plane would leave a hole in it)
static struct ui_view_spec view_spec EWRAM_BSS;
static struct ui_spec nodes[N_COUNT] EWRAM_BSS;

int save_page_open(const struct save_page *p)
{
	memset(&pg, 0, sizeof(pg));
	pg.p = p;
	pg.rows = p->rows;
	pg.count = p->count < SAVE_PAGE_ROWS ? p->count : SAVE_PAGE_ROWS;
	pg.cursor = p->cursor < pg.count ? p->cursor : 0;
	memcpy(nodes, layout, sizeof(nodes));
	if (pg.count <= LIST_H / ui_theme()->look.list_pitch)  // no bar: the card's paper there
		nodes[N_BAR] = (struct ui_spec){UI_PANEL,      5,     UI_NONE, 2, UI_LEFT, 0, 0,
						UI_ROLE_PANEL, BAR_X, LIST_Y,  8, LIST_H,  0};
	view_spec = (struct ui_view_spec){p->name, nodes, N_COUNT, N_LIST, 0, event};
	ui_leave(UI_TR_DEEPER, 1);
	while (ui_depth())
		ui_pop();
	struct ui_view *v = ui_push(&view_spec, 0);
	if (!v) {
		tlog_error("save page: %s", ui_error());
		return 0;
	}
	pg.first = v->first;
	pg.up = 1;
	ui_title_set(node(N_TITLE), p->title);
	ui_title_set_right(node(N_TITLE), p->right ? p->right : "");
	ui_title_icon_set(node(N_TITLE), p->icon16);
	ui_list_reset(node(N_LIST), pg.cursor);
	show_row();
	return 1;
}

u16 save_page_wait(u32 *row)
{
	pg.key = 0;
	while (!pg.key && pg.up && ui_depth())
		ui_frame();
	if (!ui_depth())
		pg.up = 0;
	*row = pg.cursor;
	if (pg.key)
		tlog_debug("%s: key %04x row %lu", pg.p->name, pg.key, (unsigned long)pg.cursor);
	return pg.up ? pg.key : 0;
}

void save_page_rows(const struct save_page_row *rows, u32 count, u32 cursor)
{
	if (!pg.up)
		return;
	pg.rows = rows;
	pg.count = count < SAVE_PAGE_ROWS ? count : SAVE_PAGE_ROWS;
	pg.cursor = cursor < pg.count ? cursor : 0;
	ui_list_recount(node(N_LIST), pg.cursor);
	ui_list_refresh(node(N_LIST), 0, pg.count ? pg.count - 1 : 0);
	show_row();
}

void save_page_refresh(void)
{
	if (pg.up)
		show_row();
}

u8 save_page_list(void)
{
	return node(N_LIST);
}

u8 save_page_hints(void)
{
	return node(N_HINTS);
}

void save_page_close(void)
{
	if (!pg.up)
		return;
	pg.up = 0;
	ui_leave(UI_TR_DEEPER, -1);
	while (ui_depth())
		ui_pop();
}

// Unix time t's month and day, and its time of day in seconds
static u32 civil(u32 t, u32 *month, u32 *day)
{
	// Days to a civil date (Howard Hinnant's civil_from_days)
	u32 days = t / 86400u, secs = t % 86400u;
	s32 z = (s32)days + 719468, era = z / 146097;
	u32 doe = (u32)(z - era * 146097), yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	u32 doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
	*day = doy - (153 * mp + 2) / 5 + 1;
	*month = mp < 10 ? mp + 3 : mp - 9;
	return secs;
}

void save_page_when(char *out, u32 room, u32 t)
{
	u32 m, d, secs = civil(t, &m, &d);
	char date[16], time[12];
	lang_date_numeric(date, sizeof(date), d, m);
	snprintf(time, sizeof(time), "%02lu:%02lu", (unsigned long)(secs / 3600 % 24), (unsigned long)(secs / 60 % 60));
	SF(out, room, STR_GSAVES_WHEN, LS(date), LS(time));
}

void save_page_ago(char *out, u32 room, u32 t)
{
	out[0] = 0;
	if (!t) {
		snprintf(out, room, "%s", S(STR_GSAVES_UNKNOWN));
		return;
	}
	userdata_ago_text(out, room, t);
	if (!out[0]) {
		u32 m, d;
		civil(t, &m, &d);
		lang_date_numeric(out, room, d, m);
	}
}
