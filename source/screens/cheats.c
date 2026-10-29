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
// Decoding: the first rows at once as the list shows (about two frames),
// then the rest a quarter of a frame each frame, so the list never waits.
// Past it, a larger file's next and previous windows are read ahead, a
// part each frame.
#define CS_FIRST_CYCLES (16780000u / 30)
#define CS_SLICE_CYCLES (16780000u / 240)
#define CS_AHEAD_BYTES 1024
// Frames measured after a pick (the rows redraw over the next ones)
#define CS_TOGGLE_FRAMES 3

static const struct ui_card_config cs_card = {4, 4, 4, 1, 0};
_Static_assert(STR_CHEATS_WHY_MASTER - STR_CHEATS_WHY_FULL == CHEATFILE_WHY_MASTER - CHEATFILE_WHY_FULL,
	       "a reason for each");
_Static_assert(CHEATFILE_WHY_MASTER + 1 == CHEATFILE_WHYS, "the reasons' last");
_Static_assert(STR_CHEATS_EDIT_BROKEN - STR_CHEATS_EDIT_FAILED == CHEATFILE_EDIT_FAILED,
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
	ACT_WINDOW,  // the cursor went past a larger file's window: the next one (cs.back: the one before)
};

// The menus' items: SELECT's has Edit code and Delete only
enum {
	MENU_DETAILS,
	MENU_EDIT,
	MENU_RENAME,
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
	u8 back;     // ACT_WINDOW goes to the window before
	u8 progressing;  // cs_progress's dialog is up
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
	*len = (u32)strlen(S(STR_CHEATS_UNNAMED));
	return S(STR_CHEATS_UNNAMED);
}

static int cs_fetch(void *ctx, u32 i, struct ui_item *out)
{
	(void)ctx;
	if (i >= cs.count)
		return UI_ITEM_PENDING;
	struct cheatstore *s = cheatstore();
	const struct cheatfile_cheat *c = row_cheat(i);
	if (!c) {
		out->text = S(STR_CHEATS_ADD_ROW);
		out->ink = cs.add_why == CHEATFILE_EDIT_OK ? UI_ROLE_ACCENT_INK : UI_ROLE_DIM;
		return UI_ITEM_READY;
	}
	u32 len;
	out->text = cheat_name(c, &len);
	out->len = (u16)len;
	// Not decoded yet: as one that can be picked, until the background has it
	int greyed = c->status != CHEATFILE_NEW && cheatfile_why(&s->file, s->rows[i]) != CHEATFILE_PICKABLE;
	if (greyed)
		out->ink = UI_ROLE_DIM;
	if (c->picked) {
		out->mark = ART_ICON8_TICK + 1;
		out->mark_ink = c->status == CHEATFILE_OK || c->status == CHEATFILE_NEW ? 0 : UI_ROLE_DIM;
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
			return S(STR_CHEATS_BIG);
		// The part of the budget that is short: bytes, else cycles
		int bytes = f->bytes + f->more_bytes + c->bytes > CHEATFILE_BYTES_MAX;
		u32 need = bytes ? percent(c->bytes, CHEATFILE_BYTES_MAX) : percent(c->cost, CHEATFILE_CYCLES_MAX);
		u32 used = bytes ? percent(f->bytes + f->more_bytes, CHEATFILE_BYTES_MAX)
				 : percent(f->cost + f->more_cost, CHEATFILE_CYCLES_MAX);
		SF(cs.reason, sizeof(cs.reason), STR_CHEATS_FULL, LN(need), LN(used < 100 ? 100 - used : 0));
		return cs.reason;
	}
	if (why == CHEATFILE_WHY_PATCHES) {
		u32 all = f->patches + f->more_patches;
		u32 left = all < CHEATFILE_PICKED_PATCHES_MAX ? CHEATFILE_PICKED_PATCHES_MAX - all : 0;
		SF(cs.reason, sizeof(cs.reason), STR_CHEATS_PATCHES, LN(c->patches), LN(left));
		return cs.reason;
	}
	if (why >= CHEATFILE_WHYS)
		return NULL;
	return why ? S(STR_CHEATS_WHY_FULL + why - CHEATFILE_WHY_FULL) : "";
}

static const char *add_reason(void)
{
	switch (cs.add_why) {
	case CHEATFILE_EDIT_FULL:
		return S(STR_CHEATS_ADD_FULL);
	case CHEATFILE_EDIT_LONG:
		return S(STR_CHEATS_ADD_LONG);
	default:
		return S(STR_CHEATS_EDIT_FAILED + (cs.add_why <= CHEATFILE_EDIT_FAILED ? cs.add_why : 0));
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
			cs.keys[n++] = (struct ui_hint){"A", S(STR_CHEATS_HINT_ADD)};
		else
			note = add_reason();
	} else {
		// Not decoded yet: as a cheat that can be picked (most are)
		u32 why = c->status == CHEATFILE_NEW ? CHEATFILE_PICKABLE : cheatfile_why(&s->file, s->rows[cs.cursor]);
		if (why == CHEATFILE_PICKABLE)
			cs.keys[n++] =
				(struct ui_hint){"A", c->picked ? S(STR_CHEATS_HINT_DROP) : S(STR_CHEATS_HINT_PICK)};
		else if (why == CHEATFILE_WHY_EDIT)
			cs.keys[n++] = (struct ui_hint){"A", S(STR_CHEATS_HINT_FILL)};
		if (why != CHEATFILE_PICKABLE)
			note = cs_reason(c, why);
		else if (c->status == CHEATFILE_OK && c->patches)
			note = S(STR_CHEATS_NOR);
	}
	if (!note)
		cs.keys[n++] = (struct ui_hint){"START", S(STR_CHEATS_HINT_MENU)};
	cs.keys[n++] = (struct ui_hint){"B", cs.only ? S(STR_CHEATS_HINT_ALL) : S(STR_HINT_BACK)};
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
	// A larger file's place counts the windows before this one
	u32 before = 0, all = cs.shown;
	if (cheatstore_windows()) {
		u32 k = cheatstore_window();
		if (cs.only) {
			for (u32 j = 0; j < k; j++)
				before += cheatstore_window_picks(j);
			all = cs.picks;
		} else {
			before = cheatstore_window_start(k);
			all = cheatstore_window_start(cheatstore_windows());
		}
	}
	if (cs.cursor < cs.shown)
		n = SF(cs.counter, sizeof(cs.counter), STR_CHEATS_POSITION, LN(before + cs.cursor + 1), LN(all));
	if (cs.picks && n + 1 < sizeof(cs.counter)) {
		if (n)
			n += (u32)snprintf(cs.counter + n, sizeof(cs.counter) - n, " \xC2\xB7 ");
		if (n < sizeof(cs.counter))
			SF(cs.counter + n, sizeof(cs.counter) - n, STR_CHEATS_PICKS, LN(cs.picks));
	}
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
	cs.picks = cheatstore_picks();
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
		// Past a larger file's window, either end: the list wrapped in it.
		// The cursor stays, and the screen's loop goes to the next window.
		if (cheatstore_windows() > 1 && !cs.act) {
			// Which way the key went: a wrap moves the other way
			u16 fire = ui_input()->fire;
			int down = (fire & (INPUT_DOWN | INPUT_R)) && e->index < cs.cursor;
			int up = (fire & (INPUT_UP | INPUT_L)) && e->index > cs.cursor;
			if (down || up) {
				cs.back = (u8)up;
				cs.act = ACT_WINDOW;
				ui_list_set_cursor(cs.list, cs.cursor);
				return 1;
			}
		}
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
		cheatstore_decode_to(at);  // A on a row the background hasn't got to
		if (!c->picked && cheatfile_why(&s->file, at) == CHEATFILE_WHY_EDIT) {
			cs.act = ACT_FILL;  // a placeholder: the keypad fills it in
			cs.act_row = e->index;
			return 1;
		}
		if (!cheatstore_toggle(at))
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
			if (c) {
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
		SF(line, sizeof(line), STR_CHEATS_FORMAT, LS(S(STR_KEYPAD_FORMAT_NONE + c->detected)),
		   LS(c->format != CHEAT_FMT_AUTO ? S(STR_CHEATS_DETAIL_SET) : S(STR_CHEATS_DETAIL_DETECTED)));
		detail_line(&n, line);
	} else {
		detail_line(&n, S(STR_CHEATS_DETAIL_UNKNOWN));
	}
	if (c->status == CHEATFILE_OK) {
		if (c->bytes) {
			SF(line, sizeof(line), STR_CHEATS_BUDGET, LN(share(c->bytes, c->cost)));
			detail_line(&n, line);
		} else {
			detail_line(&n, S(STR_CHEATS_DETAIL_RUNS));
		}
		if (c->patches) {
			SF(line, sizeof(line), STR_CHEATS_ROM, LN(c->patches), LS(S(STR_CHEATS_DETAIL_NOR)));
			detail_line(&n, line);
		}
	}
	u32 why = cheatfile_why(f, at);
	const char *reason = why != CHEATFILE_PICKABLE ? cs_reason(c, why) : NULL;
	if (reason)
		detail_line(&n, reason);
	detail_line(&n, c->picked ? S(STR_CHEATS_DETAIL_PICKED) : S(STR_CHEATS_DETAIL_NOT_PICKED));
}

// The detail view of cheat at in the cheat view's place, until B; then
// nothing shows (the caller shows the cheat view again)
static void cs_detail(u32 at)
{
	struct cheatstore *s = cheatstore();
	if (at >= s->file.count)
		return;
	cheatstore_decode_to(at);
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
	ui_title_set_sub(title, S(STR_CHEATS_DETAILS_SUB));
	cs.keys[0] = (struct ui_hint){"B", S(STR_HINT_BACK)};
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
	cheatstore_decode(CS_FIRST_CYCLES);  // the rest as the list shows (cs_background)
}

// Each frame the list shows: the window's cheats decoded on, their rows drawn
// again as they are; once all are, a larger file's next and previous
// windows read ahead into their slots, so going to either is at once
// The window's cheats decoded so far
static u32 cs_decoded(void)
{
	return cheatstore()->work.next;
}

static void cs_background(void)
{
	struct cheatstore *s = cheatstore();
	if (cs_decoded() < s->file.count) {
		u32 from = cs_decoded();
		cheatstore_decode(CS_SLICE_CYCLES);
		u32 to = cs_decoded();
		if (cs.only)
			ui_list_refresh(cs.list, 0, cs.shown ? cs.shown - 1 : 0);
		else if (from < cs.shown)
			ui_list_refresh(cs.list, from, (to < cs.shown ? to : cs.shown) - 1);
		if (cs.cursor >= from && cs.cursor < to)
			cs_show_hints();
		if (to >= s->file.count) {
			cheatstore_listed();  // decoded: the game popup's count from now on
			cs.add_why = (u8)cheatstore_can_add(cs.new_name);
		}
		return;
	}
	u32 n = cheatstore_windows(), k = cheatstore_window();
	if (n > 1 && cheatstore_ahead((k + 1) % n, CS_AHEAD_BYTES))
		cheatstore_ahead((k + n - 1) % n, CS_AHEAD_BYTES);
}

// After an edit: every cheat decoded again from the first (keys and seeds
// carry), the picks' budget with them
static void cs_redecode(void)
{
	cheatstore_begin();
	cs_decode();
}

// The rows: every cheat (only the picked ones while
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
			own += c->own;
			if (!cs.only || c->picked) {
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
	// + Add code ends the file: a larger one's last window
	u32 last = !cheatstore_windows() || cheatstore_window() + 1 == cheatstore_windows();
	cs.count = cs.shown + (!cs.only && last);
	cs.picks = cheatstore_picks();
	SF(cs.new_name, sizeof(cs.new_name), STR_CHEATS_NAME, LN(own + 1));
	cs.add_why = (u8)cheatstore_can_add(cs.new_name);
	return row != CHEATFILE_NONE ? row : (cs.shown ? cs.shown - 1 : 0);
}

// 1 if A does something on cheat i: picks or drops it, or fills in its
// digits (one not decoded yet counts: most do)
static int cs_actable(u32 i)
{
	if (cheatstore()->cheats[i].status == CHEATFILE_NEW)
		return 1;
	u32 why = cheatfile_why(&cheatstore()->file, i);
	return why == CHEATFILE_PICKABLE || why == CHEATFILE_WHY_EDIT;
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
		ui_title_set_sub(cs.title, S(STR_CHEATS_ONLY_SUB));
	row = row < cs.count ? row : cs.count - 1;
	ui_list_set_cursor(cs.list, row);
	cs_moved(row);
	return 1;
}

static int cs_window(u32 k, u32 row);

// Only the picked cheats, or all again, in the view as it is: the cursor
// stays on its cheat if that still shows. A larger file whose window shows
// no pick goes to the first window that has one.
static void cs_set_only(int only)
{
	struct cheatstore *s = cheatstore();
	if (only && cheatstore_windows() && !cheatfile_picks(&s->file)) {
		for (u32 k = 0; k < cheatstore_windows(); k++)
			if (cheatstore_window_picks(k) && k != cheatstore_window()) {
				cs.only = 1;
				ui_title_set_sub(cs.title, S(STR_CHEATS_ONLY_SUB));
				cs_window(k, 0);
				return;
			}
	}
	u32 at = row_cheat(cs.cursor) ? s->rows[cs.cursor] : CHEATFILE_NONE;
	cs.only = (u8)only;
	u32 row = cs_rows(at);
	if (at == CHEATFILE_NONE && !cs.only)
		row = cs.count - 1;  // from + Add code: back on it
	tlog_debug("cheats: %s, %lu rows", cs.only ? "picked only" : "all", (unsigned long)cs.count);
	ui_list_reset(cs.list, row);
	ui_title_set_sub(cs.title, cs.only ? S(STR_CHEATS_ONLY_SUB) : NULL);
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
		cheatstore_toggle(at);
		cs.picked = 1;
	}
}

// Writes the file after an edit; a dialog if the card didn't take it
static void cs_write(void)
{
	if (!cheatstore_write())
		dialog_message(cs.heading, S(STR_CHEATS_SAVE_FAILED), DIALOG_ERROR);
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
	struct keypad_ask ask = {
		cs.icon, c ? S(STR_KEYPAD_TITLE_EDIT) : S(STR_KEYPAD_TITLE_ADD), NULL, 0, CHEAT_FMT_AUTO, NULL};
	struct cheat_decoder ctx;
	cheatstore_decoder_before(c ? at : f->count, &ctx);
	ask.ctx = &ctx;
	snprintf(cs.name, sizeof(cs.name), "%s", cs.new_name);
	if (c) {
		if (c->code_len > KEYPAD_CODE_MAX) {
			dialog_message(cs.heading, S(STR_CHEATS_TOO_LONG), DIALOG_INFO);
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
			dialog_message(cs.heading, S(STR_CHEATS_TOO_LONG), DIALOG_INFO);
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
			cs.icon, was, NULL, RENAME_GAME_MAX, 0, name_submit, 0, S(STR_CHEATS_NAME_TITLE), 1};
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
		u32 text = STR_CHEATS_EDIT_FAILED + (r <= CHEATFILE_EDIT_FAILED ? r : 0);
		tlog("cheats: %s: refused: %s", how, lang_en[text]);
		dialog_message(cs.heading, S(text), DIALOG_ERROR);
		return row;
	}
	tlog("cheats: %s cheat %lu: ok", how, (unsigned long)idx);
	cs_redecode();
	pick_new(idx);
	cs_write();
	return cs_rows(idx);
}

// The rename keyboard for cheat c's name (at in the table), then the edit
// and the file written. The row to show after.
static u32 cs_rename(const struct cheatfile_cheat *c, u32 at)
{
	u32 row = cs.act_row, len;
	const char *now = cheat_name(c, &len);
	snprintf(cs.name, sizeof(cs.name), "%.*s", (int)len, now);
	char was[sizeof(cs.name)];
	memcpy(was, cs.name, sizeof(was));
	const struct rename_ask name = {
		cs.icon, was, NULL, RENAME_GAME_MAX, 0, name_submit, 0, S(STR_CHEATS_RENAME_TITLE), 0};
	if (!rename_run(&name))
		return row;  // cancelled, or the name as it was
	u32 r = cheatstore_rename(at, cs.name);
	tlog("cheats: renamed cheat %lu: %s", (unsigned long)at, r == CHEATFILE_EDIT_OK ? "ok" : "refused");
	if (r != CHEATFILE_EDIT_OK) {
		dialog_message(cs.heading, S(STR_CHEATS_EDIT_FAILED + (r <= CHEATFILE_EDIT_FAILED ? r : 0)),
			       DIALOG_ERROR);
		return row;
	}
	cs_redecode();
	cs_write();
	return cs_rows(at);
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
		dialog_message(cs.heading, S(STR_CHEATS_EDIT_FAILED + (r <= CHEATFILE_EDIT_FAILED ? r : 0)),
			       DIALOG_ERROR);
	else if (!cheatfile_empty(&s->file) || !cheatstore_drop())
		cs_write();
	return cs_rows(at);
}

// A progress dialog for decoding a larger file's windows, up once one is
// needed (cheatstore_progress_fn)
static void cs_progress(u32 done, u32 total)
{
	if (!cs.progressing) {
		dialog_progress_begin(S(STR_CHEATS_READING), S(STR_CHEATS_READING_TEXT));
		cs.progressing = 1;
	}
	dialog_progress_set(done, total, NULL);
}

static void cs_progress_end(void)
{
	if (cs.progressing)
		dialog_progress_end();
	cs.progressing = 0;
}

// A larger file's window k on the screen, the cursor on row (CHEATFILE_NONE:
// its last), decoded; 0 if it couldn't be read
static int cs_window(u32 k, u32 row)
{
	u32 from = cheatstore_window();
	int ok = cheatstore_window_go(k);
	cs_progress_end();
	if (!ok) {
		// The file changed on the card since its index: the index is gone,
		// so the next visit reads the file anew
		tlog_error("cheats: window %lu couldn't be read", (unsigned long)k + 1);
		dialog_message(cs.heading, S(STR_CHEATS_GONE), DIALOG_ERROR);
		cheatstore_window_go(from);
	}
	cs_decode();
	cs_rows(CHEATFILE_NONE);
	if (row == CHEATFILE_NONE || row >= cs.count)
		row = cs.count ? cs.count - 1 : 0;
	tlog_debug("cheats: window %lu of %lu, %lu rows", (unsigned long)cheatstore_window() + 1,
		   (unsigned long)cheatstore_windows(), (unsigned long)cs.count);
	ui_list_reset(cs.list, row);
	cs_moved(row);
	return ok;
}

// The cursor went past the window's end (back: its start): the next window
// (the one before), round the file's ends, past windows with no picks while
// only the picked show. Each starts from its index's decoder, so any is a
// read away (or none, read ahead already).
static void cs_window_step(int back)
{
	u32 n = cheatstore_windows(), k = cheatstore_window();
	do
		k = back ? (k + n - 1) % n : (k + 1) % n;
	while (cs.only && k != cheatstore_window() && !cheatstore_window_picks(k));
	cs_window(k, back ? CHEATFILE_NONE : 0);
}

// Turns every picked cheat off, a larger file's in every window
static void cs_all_off(void)
{
	struct cheatstore *s = cheatstore();
	if (cheatstore_windows()) {
		u32 row = cs.cursor;
		cheatstore_all_off();
		tlog("cheats: all turned off");
		cs.only = 0;
		cs_decode();
		row = cs_rows(row < cs.shown ? s->rows[row] : CHEATFILE_NONE);
		ui_list_reset(cs.list, row);
		ui_title_set_sub(cs.title, NULL);
		cs_moved(row);
		return;
	}
	cheatstore_all_off();
	tlog("cheats: all turned off");
	if (cs.only)
		cs_set_only(0);
	cs_picks_changed();
}

// START's menu on row (full: Details, the user's code's Edit code, Turn
// all off, Show picked only, Delete) or SELECT's on a cheat (the user's
// code's Edit code, Delete). The row to show after, or CHEATFILE_NONE to
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
		cs.items[n++] = (struct ui_pop_item){S(STR_CHEATS_MENU_DETAILS), 0, 0};
	}
	if (c && c->own) {
		cs.item_ids[n] = MENU_EDIT;
		cs.items[n++] = (struct ui_pop_item){S(STR_CHEATS_MENU_EDIT), 0, 0};
	} else if (c) {
		cs.item_ids[n] = MENU_RENAME;
		cs.items[n++] = (struct ui_pop_item){S(STR_CHEATS_MENU_RENAME), 0, 0};
	}
	if (full) {
		cs.item_ids[n] = MENU_ALL_OFF;
		cs.items[n++] = (struct ui_pop_item){S(STR_CHEATS_MENU_ALL_OFF), 0, (u8)(cs.picks ? 0 : UI_POP_DIM)};
		cs.item_ids[n] = MENU_ONLY;
		cs.items[n++] = (struct ui_pop_item){cs.only ? S(STR_CHEATS_MENU_ALL) : S(STR_CHEATS_MENU_ONLY), 0,
						     (u8)(cs.picks || cs.only ? 0 : UI_POP_DIM)};
	}
	if (c) {
		cs.item_ids[n] = MENU_DELETE;
		cs.items[n++] = (struct ui_pop_item){S(STR_CHEATS_MENU_DELETE), 0, UI_POP_DANGER};
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
		case MENU_RENAME:
			ui_popup_close();
			cs_leave();
			return cs_rename(c, at);
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
			SF(cs.ask, sizeof(cs.ask), STR_CHEATS_DELETE_ASK, LS(cs.pop_title));
			struct ui_confirm ask = {cs.ask, S(STR_CHEATS_DELETE_TEXT), S(STR_CHEATS_DELETE_BUTTON), 1};
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
	cheatstore_progress(cs_progress);  // a larger file read whole the first time
	int opened = cheatstore_open(rom_path);
	cs_progress_end();
	if (opened != CHEATSTORE_READ && opened != CHEATSTORE_NEW) {
		// Why the file is left alone: never a screen without it
		const char *why = opened == CHEATSTORE_BIG          ? S(STR_CHEATS_TOO_BIG)
				  : opened == CHEATSTORE_OTHER_DUMP ? S(STR_CHEATS_OTHER_DUMP)
								    : S(STR_CHEATS_GONE);
		cheatstore_progress(NULL);
		tlog("cheats: %s: no cheat screen (%d)", rom_path, opened);
		dialog_message(title, why, DIALOG_ERROR);
		return;
	}
	cs_decode();
	cheatstore_listed();  // decoded: the game popup's count from now on
	struct cheatstore *s = cheatstore();
	cs_rows(CHEATFILE_NONE);
	// The cursor starts on the first cheat A does something on (a pick, or
	// a placeholder's digits), past the master codes a file starts with
	u32 row = 0, greyed = 0;
	for (u32 r = 0; r < cs.shown; r++)
		greyed += s->cheats[s->rows[r]].status != CHEATFILE_NEW &&
			  cheatfile_why(&s->file, s->rows[r]) != CHEATFILE_PICKABLE;
	while (row < cs.shown && !cs_actable(s->rows[row]))
		row++;
	if (row == cs.shown)
		row = 0;
	tlog("cheats: %s: %lu cheats, %lu greyed, %lu picked (%lu bytes)%s", s->path, (unsigned long)cs.shown,
	     (unsigned long)greyed, (unsigned long)cs.picks, (unsigned long)s->file.bytes,
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
			cs_background();
			// A window of one row: the list has nowhere to move, so says nothing
			if (cheatstore_windows() > 1 && cs.count == 1 && !cs.act && ui_depth() &&
			    (ui_input()->fire & (INPUT_UP | INPUT_DOWN))) {
				cs.back = (ui_input()->fire & INPUT_UP) != 0;
				cs.act = ACT_WINDOW;
			}
			if (cs.b_down && (ui_input()->released & INPUT_B)) {
				cs.b_down = 0;
				if (cs.only)
					cs_set_only(0);  // B shows all again first
				else
					cs.act = ACT_BACK;
			}
		}
		if (cs.act == ACT_WINDOW) {
			cs_window_step(cs.back);
			continue;
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
	cs_progress_end();
	cheatstore_progress(NULL);
	cheatstore_keep_listed();  // the popup's count after a power-off too
	ui_leave(UI_TR_DEEPER, -1);  // back to the caller's screen
	while (ui_depth())
		ui_pop();
	if (!saved) {
		dialog_message(cs.heading, S(STR_CHEATS_SAVE_FAILED), DIALOG_ERROR);
	} else if (picks && cs.picked && boots_clean(rom_path)) {
		// Picks that won't run: said once, as the user leaves with them
		tlog("cheats: %lu picked, and the game boots clean", (unsigned long)picks);
		SF(cs.ask, sizeof(cs.ask), STR_CHEATS_CLEAN, LN(picks));
		dialog_message(cs.heading, cs.ask, DIALOG_INFO);
	}
}
