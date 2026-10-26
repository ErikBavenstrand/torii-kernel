// The cheat screen (cheats.h), in the game lists' look: the title bar with
// the game's name, which row the cursor is on and how many cheats are on,
// the cheats on a card, and the hint bar, which says why a greyed row can't
// be picked beside its keys. The file, its cheats and the row map stay in
// pReadCache (READCACHE_CHEATS, past the asset jobs' stage), and its text in
// PSRAM (cheatstore.h), while the screen runs, the code keypad, the rename
// keyboard and the detail view included: it reads the file once, and A only
// does arithmetic on what decoding found (cheatfile.h). An edit (a code
// added, filled in, edited or deleted) changes the text in place, decodes it
// again and writes the file at once.
#include "cheats.h"

#include <stdio.h>
#include <string.h>

#include <gba_input.h>

#include "keypad.h"
#include "rename.h"
#include "../actions.h"
#include "../cheatstore.h"
#include "../ez_define.h"
#include "../Ezcard_OP.h"
#include "../keypad_edit.h"
#include "../lang.h"
#include "../rename_edit.h"
#include "../tlog.h"
#include "../engine/frame.h"
#include "../ui/art_ids.h"
#include "../ui/dialog.h"
#include "../ui/ui.h"
#include "../ui/ui_reading.h"

// --- Look, px: the game lists' (screens/games.c) ----------------------------
#define CS_TITLE_H 24
#define CS_CARD_Y 16
#define CS_CARD_H 128
#define CS_LIST_X 8
#define CS_LIST_Y 24
#define CS_LIST_W 216  // up to the scroll bar's column
#define CS_LIST_H 112  // 8 rows of 14 px
#define CS_LIST_PAD 4
#define CS_HINTS_H 16
// Decoding a file this long (a quarter second) gets a progress dialog
#define CS_QUIET_CYCLES (16780000u / 4)
#define CS_STEP_CYCLES (16780000u / 20)
// Frames measured after a pick (the rows redraw over the next ones)
#define CS_TOGGLE_FRAMES 3

static const struct ui_card_config cs_card = {4, 4, 4, 1, 0};
_Static_assert(sizeof(((struct lang_cheats *)0)->why) / sizeof(const char *) == CHEATFILE_WHYS, "a reason for each");
_Static_assert(sizeof(((struct lang_cheats *)0)->edit_failed) / sizeof(const char *) == CHEATFILE_EDIT_FAILED + 1,
	       "a line for each edit's failure");

enum {
	CS_TITLE,
	CS_HINTS,
	CS_CARD,
	CS_LIST,
	CS_SCROLL,
	CS_NODES,
};

// What the screen leaves its view for
enum {
	ACT_NONE,
	ACT_BACK,   // B: the picks saved, back to the caller (or all the cheats again)
	ACT_ADD,    // A on "+ Add code"
	ACT_FILL,   // A on a placeholder: the keypad, the code filled in
	ACT_MENU,   // SELECT on a code of the user's: Edit code, Delete
	ACT_START,  // START: the menu
};

// The menus' items: SELECT's has Edit code and Delete only
enum {
	MENU_DETAILS,
	MENU_EDIT,
	MENU_DELETE,
	MENU_ALL_OFF,
	MENU_ONLY,
	MENU_ITEMS,
};

static struct {
	u32 count;   // rows: the cheats shown, then "+ Add code" unless only the picked show
	u32 shown;   // the cheats' rows
	u32 cursor;  // the list's, as last seen
	u32 picks;   // the picked cheats
	u8 list, title, hints;
	u8 b_down;   // B went down on this screen: its release leaves
	u8 act;      // ACT_*, and its row
	u8 only;     // only the picked cheats show
	u8 add_why;  // cheatstore_can_add's: + Add code greyed unless CHEATFILE_EDIT_OK
	u8 picked;   // a cheat was picked on this visit: the boot mode is checked on leaving
	u8 measure;  // frames left to measure as "cheats-toggle"
	u8 close;    // B on the detail view
	u16 icon;
	u32 act_row;
	char counter[32];
	char heading[64];
	char pop_title[64];
	char ask[128];
	char reason[48];  // a greyed row's reason with numbers in it
	char new_name[16];  // a new code's name until the user gives one
	struct ui_hint keys[3];
	struct ui_pop_item items[MENU_ITEMS];
	u8 item_ids[MENU_ITEMS];
	struct ui_popup popup;
	struct ui_reading reading;  // the detail view's
	char name[RENAME_TEXT_MAX + 1];  // a code's name, from the rename keyboard
	char code[KEYPAD_CODE_MAX + 1];  // and the code, from the keypad
} cs EWRAM_BSS;

// The cheat row row shows, or NULL for "+ Add code"
static struct cheatfile_cheat *row_cheat(u32 row)
{
	struct cheatstore *s = cheatstore();
	return row < cs.shown ? &s->cheats[s->rows[row]] : NULL;
}

static u32 cs_count(void *ctx)
{
	(void)ctx;
	return cs.count;
}

// A cheat's name: its desc, or "(no name)"
static const char *cheat_name(const struct cheatfile_cheat *c, u32 *len)
{
	if (c->desc != CHEATFILE_NONE && c->desc_len) {
		*len = c->desc_len;
		return cheatstore()->file.text + c->desc;
	}
	*len = (u32)strlen(gl_cheats->unnamed);
	return gl_cheats->unnamed;
}

static int cs_fetch(void *ctx, u32 i, struct ui_item *out)
{
	(void)ctx;
	if (i >= cs.count)
		return UI_ITEM_PENDING;
	struct cheatstore *s = cheatstore();
	const struct cheatfile_cheat *c = row_cheat(i);
	if (!c) {
		out->text = gl_cheats->add_row;
		out->ink = cs.add_why == CHEATFILE_EDIT_OK ? UI_ROLE_ACCENT_INK : UI_ROLE_DIM;
		return UI_ITEM_READY;
	}
	u32 len;
	out->text = cheat_name(c, &len);
	out->len = (u16)len;
	int greyed = cheatfile_why(&s->file, s->rows[i]) != CHEATFILE_PICKABLE;
	if (greyed)
		out->ink = UI_ROLE_DIM;
	if (c->picked) {
		out->mark = ART_ICON8_TICK + 1;
		out->mark_ink = c->status == CHEATFILE_OK ? 0 : UI_ROLE_DIM;
	}
	return UI_ITEM_READY;
}

static const struct ui_list_model cs_model = {cs_count, cs_fetch, 0, 0, 0};

// part of max, as a percentage rounded up
static u32 percent(u32 part, u32 max)
{
	return (part * 100 + max - 1) / max;
}

// A cheat's share of the interpreter's budget, the larger of its bytes' and
// its cycles'
static u32 share(u32 bytes, u32 cost)
{
	u32 b = percent(bytes, CHEATFILE_BYTES_MAX), c = percent(cost, CHEATFILE_CYCLES_MAX);
	return b > c ? b : c;
}

// Why cheat i (c) can't be picked, why being cheatfile_why's: the budget's
// numbers where they say it
static const char *cs_reason(const struct cheatfile_cheat *c, u32 why)
{
	const struct cheatfile *f = &cheatstore()->file;
	if (why == CHEATFILE_WHY_FULL) {
		if (c->status != CHEATFILE_OK)
			return gl_cheats->big;
		// The part of the budget that is short: bytes, else cycles
		int bytes = f->bytes + c->bytes > CHEATFILE_BYTES_MAX;
		u32 need = bytes ? percent(c->bytes, CHEATFILE_BYTES_MAX) : percent(c->cost, CHEATFILE_CYCLES_MAX);
		u32 used = bytes ? percent(f->bytes, CHEATFILE_BYTES_MAX) : percent(f->cost, CHEATFILE_CYCLES_MAX);
		snprintf(cs.reason, sizeof(cs.reason), EN_CHEAT_FULL, (unsigned long)need,
			 (unsigned long)(used < 100 ? 100 - used : 0));
		return cs.reason;
	}
	if (why == CHEATFILE_WHY_PATCHES) {
		u32 left = f->patches < CHEATFILE_PICKED_PATCHES_MAX ? CHEATFILE_PICKED_PATCHES_MAX - f->patches : 0;
		snprintf(cs.reason, sizeof(cs.reason), EN_CHEAT_PATCHES, (unsigned long)c->patches,
			 (unsigned long)left);
		return cs.reason;
	}
	return why < CHEATFILE_WHYS ? gl_cheats->why[why] : NULL;
}

static const char *add_reason(void)
{
	switch (cs.add_why) {
	case CHEATFILE_EDIT_FULL:
		return gl_cheats->add_full;
	case CHEATFILE_EDIT_LONG:
		return gl_cheats->add_long;
	default:
		return gl_cheats->edit_failed[cs.add_why <= CHEATFILE_EDIT_FAILED ? cs.add_why : 0];
	}
}

// The hint bar for the cursor's row: its keys, and beside them a greyed
// row's reason or a ROM patch's note (with A and B only: the reasons need
// the room)
static void cs_show_hints(void)
{
	static const struct ui_hint_note beside = {0, 0, 0, 0, 0, 0, 1};
	struct cheatstore *s = cheatstore();
	const struct cheatfile_cheat *c = row_cheat(cs.cursor);
	const char *note = NULL;
	u32 n = 0;
	if (cs.cursor >= cs.count)
		return;
	if (!c) {
		if (cs.add_why == CHEATFILE_EDIT_OK)
			cs.keys[n++] = (struct ui_hint){"A", gl_cheats->hint_add};
		else
			note = add_reason();
	} else {
		u32 why = cheatfile_why(&s->file, s->rows[cs.cursor]);
		if (why == CHEATFILE_PICKABLE)
			cs.keys[n++] = (struct ui_hint){"A", c->picked ? gl_cheats->hint_drop : gl_cheats->hint_pick};
		else if (why == CHEATFILE_WHY_EDIT)
			cs.keys[n++] = (struct ui_hint){"A", gl_cheats->hint_fill};
		if (why != CHEATFILE_PICKABLE)
			note = cs_reason(c, why);
		else if (c->status == CHEATFILE_OK && c->patches)
			note = gl_cheats->nor;
	}
	if (!note)
		cs.keys[n++] = (struct ui_hint){"START", gl_cheats->hint_menu};
	cs.keys[n++] = (struct ui_hint){"B", cs.only ? gl_cheats->hint_all : gl_hint_back};
	ui_hints_set(cs.hints, cs.keys, n);
	if (note)
		ui_hints_note(cs.hints, note, &beside);
}

// The title's right: the cursor's cheat of those listed (none on + Add
// code), and how many are on
static void cs_counter(void)
{
	u32 n = 0;
	cs.counter[0] = 0;
	if (cs.cursor < cs.shown)
		n = (u32)snprintf(cs.counter, sizeof(cs.counter), EN_CHEAT_POSITION, (unsigned long)cs.cursor + 1,
				  (unsigned long)cs.shown);
	if (cs.picks && n < sizeof(cs.counter))
		snprintf(cs.counter + n, sizeof(cs.counter) - n, "%s" EN_CHEAT_PICKS, n ? " \xC2\xB7 " : "",
			 (unsigned long)cs.picks);
	ui_title_set_right(cs.title, cs.counter);
}

static void cs_moved(u32 to)
{
	cs.cursor = to;
	cs_counter();
	cs_show_hints();
}

// After picks changed: every row again (greyed rows change as the budget
// left does), the count, + Add code's room, the hints
static void cs_picks_changed(void)
{
	struct cheatstore *s = cheatstore();
	cs.picks = cheatfile_picks(&s->file);
	cs.add_why = (u8)cheatstore_can_add(cs.new_name);
	ui_list_refresh(cs.list, 0, cs.count - 1);
	cs_moved(cs.cursor);
	frame_measure("cheats-toggle");
	cs.measure = CS_TOGGLE_FRAMES - 1;
}

static int cs_event(struct ui_view *v, const struct ui_event *e)
{
	(void)v;
	const InputState *in = ui_input();
	switch (e->type) {
	case UI_EV_CURSOR:
		cs_moved(e->index);
		frame_measure("cheats-move");
		return 1;
	case UI_EV_ACTIVATE: {
		struct cheatstore *s = cheatstore();
		if (e->index >= cs.count || cs.act)
			return 1;
		const struct cheatfile_cheat *c = row_cheat(e->index);
		if (!c) {
			if (cs.add_why == CHEATFILE_EDIT_OK) {
				cs.act = ACT_ADD;
				cs.act_row = e->index;
			}
			return 1;  // greyed: the hint bar says why
		}
		u32 at = s->rows[e->index];
		if (!c->picked && cheatfile_why(&s->file, at) == CHEATFILE_WHY_EDIT) {
			cs.act = ACT_FILL;  // a placeholder: the keypad fills it in
			cs.act_row = e->index;
			return 1;
		}
		if (!cheatfile_toggle(&s->file, at))
			return 1;
		cs.picked |= c->picked;
		tlog_debug("cheats: %s cheat %u, %lu bytes picked", c->picked ? "picked" : "dropped", c->number,
			   (unsigned long)s->file.bytes);
		cs_picks_changed();
		return 1;
	}
	case UI_EV_KEYS:
		if (e->keys & INPUT_B) {
			cs.b_down = 1;
			return 1;
		}
		if ((e->keys & in->pressed & INPUT_START) && !cs.act && !ui_list_paging(cs.list)) {
			cs.act = ACT_START;
			cs.act_row = cs.cursor;
			return 1;
		}
		if ((e->keys & INPUT_SELECT) && (in->pressed & INPUT_SELECT) && !cs.act) {
			const struct cheatfile_cheat *c = row_cheat(cs.cursor);
			if (c && c->own) {
				cs.act = ACT_MENU;
				cs.act_row = cs.cursor;
			}
			return 1;
		}
		return 0;
	default:
		return 0;
	}
}

static const struct ui_title_config cs_title = {"", "", ART_ICON16_GBA};
static const struct ui_hints_config cs_no_hints = {0, 0};
static const struct ui_list_config cs_list = {&cs_model, UI_ROW_PITCH_LIST,
					      UI_LIST_WRAP | UI_LIST_MARQUEE | UI_LIST_MARKS, 0, CS_LIST_PAD};
static const struct ui_scrollbar_config cs_bar = {CS_LIST};

static const struct ui_spec cs_nodes[CS_NODES] = {
	[CS_TITLE] = {UI_TITLE, 1, UI_NONE, 3, UI_HFILL, 0, 0, 0, 0, 0, 0, CS_TITLE_H, &cs_title},
	[CS_HINTS] = {UI_HINTS, 2, UI_NONE, 3, UI_HFILL | UI_BOTTOM, 0, 0, 0, 0, 0, 0, CS_HINTS_H, &cs_no_hints},
	[CS_CARD] = {UI_CARD, 3, UI_NONE, 2, UI_HFILL, 0, 0, 0, 0, CS_CARD_Y, 0, CS_CARD_H, &cs_card},
	[CS_LIST] = {UI_LIST, 4, UI_NONE, 1, UI_LEFT, UI_FOCUSABLE, 0, 0, CS_LIST_X, CS_LIST_Y, CS_LIST_W, CS_LIST_H,
		     &cs_list},
	[CS_SCROLL] = {UI_SCROLLBAR, 5, UI_NONE, 2, UI_LEFT, 0, 0, UI_ROLE_PANEL, CS_LIST_X + CS_LIST_W, CS_LIST_Y, 8,
		       CS_LIST_H, &cs_bar},
};

// The title's icon is the caller's (ui_title_icon_set)
static const struct ui_view_spec cs_view = {"cheats", cs_nodes, CS_NODES, CS_LIST, UI_VIEW_LR_PAGES, cs_event};

// --- The detail view: a cheat's name, code and what decoding found, read-only ---

static int dv_event(struct ui_view *v, const struct ui_event *e)
{
	(void)v;
	if (e->type != UI_EV_KEYS || !(e->keys & ui_input()->pressed & INPUT_B))
		return 0;
	cs.close = 1;
	return 1;
}

static const struct ui_list_config dv_list = {&ui_reading_model, UI_ROW_PITCH_LIST, UI_LIST_PLAIN | UI_LIST_SCROLL, 0,
					      CS_LIST_PAD};
static const struct ui_spec dv_nodes[CS_NODES] = {
	[CS_TITLE] = {UI_TITLE, 1, UI_NONE, 3, UI_HFILL, 0, 0, 0, 0, 0, 0, CS_TITLE_H, &cs_title},
	[CS_HINTS] = {UI_HINTS, 2, UI_NONE, 3, UI_HFILL | UI_BOTTOM, 0, 0, 0, 0, 0, 0, CS_HINTS_H, &cs_no_hints},
	[CS_CARD] = {UI_CARD, 3, UI_NONE, 2, UI_HFILL, 0, 0, 0, 0, CS_CARD_Y, 0, CS_CARD_H, &cs_card},
	[CS_LIST] = {UI_LIST, 4, UI_NONE, 1, UI_LEFT, UI_FOCUSABLE, 0, 0, CS_LIST_X, CS_LIST_Y, CS_LIST_W, CS_LIST_H,
		     &dv_list},
	[CS_SCROLL] = {UI_SCROLLBAR, 5, UI_NONE, 2, UI_LEFT, 0, 0, UI_ROLE_PANEL, CS_LIST_X + CS_LIST_W, CS_LIST_Y, 8,
		       CS_LIST_H, &cs_bar},
};
static const struct ui_view_spec dv_view = {"cheat-details", dv_nodes, CS_NODES, CS_LIST, 0, dv_event};

// Appends to the detail text (at *n), cut at its end
static void detail_add(u32 *n, const char *s, u32 len)
{
	char *d = cheatstore()->detail;
	if (*n + len >= CHEATSTORE_DETAIL)
		len = CHEATSTORE_DETAIL - 1 - *n;
	memcpy(d + *n, s, len);
	*n += len;
	d[*n] = 0;
}

static void detail_line(u32 *n, const char *s)
{
	detail_add(n, s, (u32)strlen(s));
	detail_add(n, "\n", 1);
}

// Cheat at's detail text: its name as the heading, its code a line each,
// its format, its share of the budget, its ROM patches, why it can't be
// picked, whether it is
static void detail_text(u32 at)
{
	struct cheatstore *s = cheatstore();
	const struct cheatfile *f = &s->file;
	const struct cheatfile_cheat *c = &s->cheats[at];
	char line[96];
	u32 n = 0, len;
	const char *name = cheat_name(c, &len);
	detail_add(&n, "# ", 2);
	detail_add(&n, name, len);
	detail_add(&n, "\n", 1);
	// The code: lines between its '+'s
	const char *code = f->text + c->code;
	for (u32 i = 0, from = 0; i <= c->code_len; i++)
		if (i == c->code_len || code[i] == '+') {
			detail_add(&n, code + from, i - from);
			detail_add(&n, "\n", 1);
			from = i + 1;
		}
	detail_add(&n, "\n", 1);
	if (c->detected != CHEAT_FMT_AUTO && c->detected <= CHEAT_FMT_VBA) {
		snprintf(line, sizeof(line), EN_CHEAT_FORMAT, gl_keypad->formats[c->detected],
			 c->format != CHEAT_FMT_AUTO ? gl_cheats->detail_set : gl_cheats->detail_detected);
		detail_line(&n, line);
	} else {
		detail_line(&n, gl_cheats->detail_unknown);
	}
	if (c->status == CHEATFILE_OK) {
		if (c->bytes) {
			snprintf(line, sizeof(line), EN_CHEAT_BUDGET, (unsigned long)share(c->bytes, c->cost));
			detail_line(&n, line);
		} else {
			detail_line(&n, gl_cheats->detail_runs);
		}
		if (c->patches) {
			snprintf(line, sizeof(line), EN_CHEAT_ROM, (unsigned long)c->patches, gl_cheats->detail_nor);
			detail_line(&n, line);
		}
	}
	u32 why = cheatfile_why(f, at);
	const char *reason = why != CHEATFILE_PICKABLE ? cs_reason(c, why) : NULL;
	if (reason)
		detail_line(&n, reason);
	detail_line(&n, c->picked ? gl_cheats->detail_picked : gl_cheats->detail_not_picked);
}

// The detail view of cheat at in the cheat view's place, until B; then
// nothing shows (the caller shows the cheat view again)
static void cs_detail(u32 at)
{
	struct cheatstore *s = cheatstore();
	if (at >= s->file.count)
		return;
	detail_text(at);
	cs.reading = (struct ui_reading){s->detail,      s->detail_start,        s->detail_len,
					 s->detail_kind, CHEATSTORE_DETAIL_ROWS, 0};
	ui_reading_wrap(&cs.reading, ui_theme()->body, CS_LIST_W - 2 * CS_LIST_PAD, ui_theme()->look.reading_key_w);
	ui_leave(UI_TR_FADE, 1);
	while (ui_depth())
		ui_pop();
	struct ui_view *v = ui_push(&dv_view, &cs.reading);
	if (!v) {
		tlog_error("cheats: %s", ui_error());
		return;
	}
	u8 title = (u8)(v->first + CS_TITLE);
	ui_title_set(title, cs.heading);
	ui_title_icon_set(title, cs.icon);
	ui_title_set_sub(title, gl_cheats->details_sub);
	cs.keys[0] = (struct ui_hint){"B", gl_hint_back};
	ui_hints_set((u8)(v->first + CS_HINTS), cs.keys, 1);
	tlog_debug("cheats: details of cheat %lu, %lu rows", (unsigned long)at, (unsigned long)cs.reading.count);
	cs.close = 0;
	while (ui_depth() && !cs.close)
		ui_frame();
	ui_leave(UI_TR_FADE, -1);
	while (ui_depth())
		ui_pop();
}

// --- Rows, edits, menus --------------------------------------------------------

// Decodes the read file: a long one behind a progress dialog, so the screen
// doesn't look frozen
static void cs_decode(void)
{
	struct cheatstore *s = cheatstore();
	if (cheatstore_decode(CS_QUIET_CYCLES))
		return;
	dialog_progress_begin(gl_cheats->reading, gl_cheats->reading_text);
	do
		dialog_progress_set(s->work.next, s->file.count, NULL);
	while (!cheatstore_decode(CS_STEP_CYCLES));
	dialog_progress_end();
}

// After an edit: every cheat decoded again from the first (keys and seeds
// carry), the picks' budget with them
static void cs_redecode(void)
{
	cheatfile_begin(&cheatstore()->work);
	cs_decode();
}

// The rows: every cheat but master codes (only the picked ones while
// cs.only, and all again if none is), then "+ Add code" (not while
// cs.only); the picks' count, a new code's name and whether one may be
// added. The row cheat at shows, or the last cheat's.
static u32 cs_rows(u32 at)
{
	struct cheatstore *s = cheatstore();
	u32 row = CHEATFILE_NONE, own = 0;
	for (;;) {
		cs.shown = 0;
		for (u32 i = 0; i < s->file.count; i++) {
			const struct cheatfile_cheat *c = &s->cheats[i];
			own += cheatfile_shown(c) && c->own;
			if (cheatfile_shown(c) && (!cs.only || c->picked)) {
				if (i == at)
					row = cs.shown;
				s->rows[cs.shown++] = (u16)i;
			}
		}
		if (cs.shown || !cs.only)
			break;
		cs.only = 0;
		own = 0;
	}
	cs.count = cs.shown + !cs.only;
	cs.picks = cheatfile_picks(&s->file);
	snprintf(cs.new_name, sizeof(cs.new_name), EN_CHEAT_NAME, (unsigned long)own + 1);
	cs.add_why = (u8)cheatstore_can_add(cs.new_name);
	return row != CHEATFILE_NONE ? row : (cs.shown ? cs.shown - 1 : 0);
}

// The screen's view, the cursor on row
static int cs_show(u32 row)
{
	struct ui_view *v = ui_push(&cs_view, NULL);
	if (!v) {
		tlog_error("cheats: %s", ui_error());
		return 0;
	}
	cs.list = (u8)(v->first + CS_LIST);
	cs.title = (u8)(v->first + CS_TITLE);
	cs.hints = (u8)(v->first + CS_HINTS);
	cs.b_down = 0;
	ui_title_set(cs.title, cs.heading);
	ui_title_icon_set(cs.title, cs.icon);
	if (cs.only)
		ui_title_set_sub(cs.title, gl_cheats->only_sub);
	row = row < cs.count ? row : cs.count - 1;
	ui_list_set_cursor(cs.list, row);
	cs_moved(row);
	return 1;
}

// Only the picked cheats, or all again, in the view as it is: the cursor
// stays on its cheat if that still shows
static void cs_set_only(int only)
{
	struct cheatstore *s = cheatstore();
	u32 at = row_cheat(cs.cursor) ? s->rows[cs.cursor] : CHEATFILE_NONE;
	cs.only = (u8)only;
	u32 row = cs_rows(at);
	if (at == CHEATFILE_NONE && !cs.only)
		row = cs.count - 1;  // from + Add code: back on it
	tlog_debug("cheats: %s, %lu rows", cs.only ? "picked only" : "all", (unsigned long)cs.count);
	ui_list_reset(cs.list, row);
	ui_title_set_sub(cs.title, cs.only ? gl_cheats->only_sub : NULL);
	cs_moved(row);
}

// The screen's view goes, for the keypad or the keyboard in its place
static void cs_leave(void)
{
	ui_leave(UI_TR_DEEPER, 1);
	while (ui_depth())
		ui_pop();
}

// OK on the rename keyboard: the code's name
static const char *name_submit(void *ctx, const char *name)
{
	(void)ctx;
	snprintf(cs.name, sizeof(cs.name), "%s", name);
	return NULL;
}

// Picks cheat at once decoded, if it may be: a code just typed is for using
static void pick_new(u32 at)
{
	struct cheatstore *s = cheatstore();
	if (at < s->file.count && !s->cheats[at].picked && cheatfile_why(&s->file, at) == CHEATFILE_PICKABLE) {
		cheatfile_toggle(&s->file, at);
		cs.picked = 1;
	}
}

// Writes the file after an edit; a dialog if the card didn't take it
static void cs_write(void)
{
	if (!cheatstore_write())
		dialog_message(cs.heading, gl_cheats->save_failed, DIALOG_ERROR);
}

// The keypad for a new code (at CHEATFILE_NONE), cheat at's placeholder
// (fill) or a code of the user's, then the rename keyboard for its name
// (not a placeholder's; a new code's starts as "Code N", which OK keeps),
// then the edit into the file and the file written. The row to show after.
static u32 cs_code(u32 at, int fill)
{
	struct cheatstore *s = cheatstore();
	struct cheatfile *f = &s->file;
	const struct cheatfile_cheat *c = at != CHEATFILE_NONE ? &s->cheats[at] : NULL;
	u32 row = cs.act_row;
	struct keypad_ask ask = {cs.icon, c ? gl_keypad->title_edit : gl_keypad->title_add, NULL, 0, CHEAT_FMT_AUTO,
				 NULL};
	struct cheat_decoder ctx;
	cheatstore_decoder_before(c ? at : f->count, &ctx);
	ask.ctx = &ctx;
	snprintf(cs.name, sizeof(cs.name), "%s", cs.new_name);
	if (c) {
		if (c->code_len > KEYPAD_CODE_MAX) {
			dialog_message(cs.heading, gl_cheats->too_long, DIALOG_INFO);
			return row;
		}
		memcpy(cs.code, f->text + c->code, c->code_len);
		cs.code[c->code_len] = 0;
		ask.code = cs.code;
		ask.code_len = c->code_len;
		ask.format = c->format;
		cs.name[0] = 0;
		if (c->desc != CHEATFILE_NONE)
			snprintf(cs.name, sizeof(cs.name), "%.*s", (int)c->desc_len, f->text + c->desc);
	}
	u8 format = ask.format;
	char was[sizeof(cs.name)];
	memcpy(was, cs.name, sizeof(was));
	for (;;) {
		int ran = keypad_run(&ask, cs.code, &format);
		if (ran < 0)
			dialog_message(cs.heading, gl_cheats->too_long, DIALOG_INFO);
		if (ran <= 0)
			return row;  // cancelled: nothing changes
		ask.code = cs.code;  // back here from the keyboard: as typed
		ask.code_len = (u32)strlen(cs.code);
		ask.format = format;
		if (fill)
			break;
		// Its name: a code of the user's keeps its own unless changed, a
		// new one its "Code N"; Cancel goes back to the keypad, the code
		// as typed
		const struct rename_ask name = {
			cs.icon, was, NULL, RENAME_GAME_MAX, 0, name_submit, 0, gl_cheats->name_title, 1};
		if (rename_run(&name))
			break;
	}
	u32 r, idx = at;
	if (!c)
		r = cheatstore_add(cs.name, cs.code, format, &idx);
	else
		r = cheatstore_set_code(at, fill ? NULL : cs.name, cs.code, format);
	const char *how = c ? (fill ? "filled in" : "edited") : "added";
	if (r != CHEATFILE_EDIT_OK) {
		tlog("cheats: %s: refused: %s", how, gl_cheats->edit_failed[r <= CHEATFILE_EDIT_FAILED ? r : 0]);
		dialog_message(cs.heading, gl_cheats->edit_failed[r <= CHEATFILE_EDIT_FAILED ? r : 0], DIALOG_ERROR);
		return row;
	}
	tlog("cheats: %s cheat %lu: ok", how, (unsigned long)idx);
	cs_redecode();
	pick_new(idx);
	cs_write();
	return cs_rows(idx);
}

// Deletes cheat at (asked first by the caller), and the file with it if
// nothing worth keeping is left. The row to show after.
static u32 cs_delete(u32 at)
{
	struct cheatstore *s = cheatstore();
	u32 r = cheatstore_remove(at);
	tlog("cheats: deleted cheat %lu: %s", (unsigned long)at, r == CHEATFILE_EDIT_OK ? "ok" : "refused");
	cs_leave();
	cs_redecode();
	if (r != CHEATFILE_EDIT_OK)
		dialog_message(cs.heading, gl_cheats->edit_failed[r <= CHEATFILE_EDIT_FAILED ? r : 0], DIALOG_ERROR);
	else if (!cheatfile_empty(&s->file) || !cheatstore_drop())
		cs_write();
	return cs_rows(at);
}

// Turns every picked cheat off
static void cs_all_off(void)
{
	struct cheatstore *s = cheatstore();
	for (u32 i = 0; i < s->file.count; i++)
		if (s->cheats[i].picked)
			cheatfile_toggle(&s->file, i);
	tlog("cheats: all turned off");
	if (cs.only)
		cs_set_only(0);
	cs_picks_changed();
}

// START's menu on row (full: Details, the user's code's Edit code and
// Delete, Turn all off, Show picked only) or SELECT's on a code of the
// user's (Edit code, Delete). The row to show after, or CHEATFILE_NONE to
// stay on the view.
static u32 cs_menu(u32 row, int full)
{
	struct cheatstore *s = cheatstore();
	const struct cheatfile_cheat *c = row_cheat(row);
	u32 at = c ? s->rows[row] : CHEATFILE_NONE, n = 0;
	if (c) {
		u32 len;
		const char *name = cheat_name(c, &len);
		snprintf(cs.pop_title, sizeof(cs.pop_title), "%.*s", (int)len, name);
	} else {
		snprintf(cs.pop_title, sizeof(cs.pop_title), "%s", cs.heading);
	}
	if (c && full) {
		cs.item_ids[n] = MENU_DETAILS;
		cs.items[n++] = (struct ui_pop_item){gl_cheats->menu_details, 0, 0};
	}
	if (c && c->own) {
		cs.item_ids[n] = MENU_EDIT;
		cs.items[n++] = (struct ui_pop_item){gl_cheats->menu_edit, 0, 0};
	}
	if (full) {
		cs.item_ids[n] = MENU_ALL_OFF;
		cs.items[n++] = (struct ui_pop_item){gl_cheats->menu_all_off, 0, (u8)(cs.picks ? 0 : UI_POP_DIM)};
		cs.item_ids[n] = MENU_ONLY;
		cs.items[n++] = (struct ui_pop_item){cs.only ? gl_cheats->menu_all : gl_cheats->menu_only, 0,
						     (u8)(cs.picks || cs.only ? 0 : UI_POP_DIM)};
	}
	if (c && c->own) {
		cs.item_ids[n] = MENU_DELETE;
		cs.items[n++] = (struct ui_pop_item){gl_cheats->menu_delete, 0, UI_POP_DANGER};
	}
	if (!n)
		return CHEATFILE_NONE;
	cs.popup = (struct ui_popup){cs.pop_title, 0, cs.items, (u8)n, 0, 0};
	if (!ui_popup_open(cs.list, cs.hints, &cs.popup))
		return CHEATFILE_NONE;
	tlog_debug("screen ready: cheat-menu");
	for (;;) {
		s32 pick = ui_popup_choose();
		if (pick < 0 || (u32)pick >= n)
			return CHEATFILE_NONE;  // B: it has closed
		switch (cs.item_ids[pick]) {
		case MENU_DETAILS:
			ui_popup_close();
			cs_detail(at);
			return row;
		case MENU_EDIT:
			ui_popup_close();
			cs_leave();
			return cs_code(at, 0);
		case MENU_ALL_OFF:
			if (!cs.picks)
				continue;  // greyed: nothing to turn off
			ui_popup_close();
			cs_all_off();
			return CHEATFILE_NONE;
		case MENU_ONLY:
			if (!cs.picks && !cs.only)
				continue;
			ui_popup_close();
			cs_set_only(!cs.only);
			return CHEATFILE_NONE;
		default: {
			snprintf(cs.ask, sizeof(cs.ask), EN_CHEAT_DELETE, cs.pop_title);
			struct ui_confirm ask = {cs.ask, gl_cheats->delete_text, gl_cheats->delete_button, 1};
			if (!ui_confirm(cs.hints, &ask))
				continue;  // Cancel: back to the menu
			ui_popup_close();
			return cs_delete(at);
		}
		}
	}
}

// 1 if the game at rom_path boots clean when played (its own boot mode, else
// Settings'): its picked cheats don't run then (actions.c's default_how)
static int boots_clean(const char *rom_path)
{
	struct action_facts facts;
	if (!action_game_facts(rom_path, &facts))
		return 0;
	u32 mode = facts.boot_mode;
	if (mode != ACTION_BOOT_ADDONS && mode != ACTION_BOOT_CLEAN)
		mode = Read_SET_info(assress_boot_mode) == 1 ? ACTION_BOOT_CLEAN : ACTION_BOOT_ADDONS;
	return mode == ACTION_BOOT_CLEAN;
}

void cheats_run(const char *rom_path, const char *title, u16 icon)
{
	memset(&cs, 0, sizeof(cs));
	int opened = cheatstore_open(rom_path);
	if (opened != CHEATSTORE_READ && opened != CHEATSTORE_NEW) {
		// Why the file is left alone: never a screen without it
		const char *why = opened == CHEATSTORE_BIG          ? gl_cheats->too_big
				  : opened == CHEATSTORE_OTHER_DUMP ? gl_cheats->other_dump
								    : gl_cheats->gone;
		tlog("cheats: %s: no cheat screen (%d)", rom_path, opened);
		dialog_message(title, why, DIALOG_ERROR);
		return;
	}
	cs_decode();
	cheatstore_listed();  // decoded: the game popup's count from now on
	struct cheatstore *s = cheatstore();
	cs_rows(CHEATFILE_NONE);
	u32 row = 0, hidden = s->file.count - cs.shown;
	tlog("cheats: %s: %lu cheats, %lu master codes hidden, %lu picked (%lu bytes)%s", s->path,
	     (unsigned long)cs.shown, (unsigned long)hidden, (unsigned long)cs.picks, (unsigned long)s->file.bytes,
	     opened == CHEATSTORE_NEW ? ", no file yet" : "");
	if (cs.add_why != CHEATFILE_EDIT_OK)
		tlog("cheats: + Add code greyed: %s", add_reason());
	snprintf(cs.heading, sizeof(cs.heading), "%s", title);
	cs.icon = icon;
	int up = cs_show(row);
	while (up) {
		// ui_run logs "screen ready: cheats" once it is on screen and still
		cs.act = ACT_NONE;
		while (ui_depth() && !cs.act) {
			if (cs.measure) {
				frame_measure("cheats-toggle");
				cs.measure--;
			}
			ui_frame();
			if (cs.b_down && (ui_input()->released & INPUT_B)) {
				cs.b_down = 0;
				if (cs.only)
					cs_set_only(0);  // B shows all again first
				else
					cs.act = ACT_BACK;
			}
		}
		if (cs.act == ACT_MENU || cs.act == ACT_START) {
			u32 to = cs_menu(cs.act_row, cs.act == ACT_START);
			if (to != CHEATFILE_NONE)
				up = cs_show(to);  // after an edit, a delete or the details; else the view is still up
			continue;
		}
		if (cs.act != ACT_ADD && cs.act != ACT_FILL)
			break;
		cs_leave();
		row = cs.act == ACT_ADD ? cs_code(CHEATFILE_NONE, 0) : cs_code(s->rows[cs.act_row], 1);
		up = cs_show(row);
	}
	int changed = cheatfile_changed(&s->file);
	u32 picks = cheatfile_picks(&s->file);
	tlog("cheats: back, %lu picked%s", (unsigned long)picks, changed ? ", changed" : "");
	int saved = cheatstore_write();
	cheatstore_keep_listed();  // the popup's count after a power-off too
	ui_leave(UI_TR_DEEPER, -1);  // back to the caller's screen
	while (ui_depth())
		ui_pop();
	if (!saved) {
		dialog_message(cs.heading, gl_cheats->save_failed, DIALOG_ERROR);
	} else if (picks && cs.picked && boots_clean(rom_path)) {
		// Picks that won't run: said once, as the user leaves with them
		tlog("cheats: %lu picked, and the game boots clean", (unsigned long)picks);
		snprintf(cs.ask, sizeof(cs.ask), EN_CHEAT_CLEAN, (unsigned long)picks);
		dialog_message(cs.heading, cs.ask, DIALOG_INFO);
	}
}
