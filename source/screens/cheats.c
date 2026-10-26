// The cheat screen (cheats.h), in the game lists' look: the title bar with
// the game's name and which row the cursor is on, the cheats on a card, and
// the hint bar, which says why a greyed row can't be picked. The file, its
// cheats and the row map stay in pReadCache (READCACHE_CHEATS, past the
// asset jobs' stage) while the screen runs, the code keypad and the rename
// keyboard included: it reads the file once, and A only does arithmetic on
// what decoding found (cheatfile.h). An edit (a code added, filled in,
// edited or deleted) changes the text in place, decodes it again and writes
// the file at once.
#include "cheats.h"

#include <stdio.h>
#include <string.h>

#include <gba_input.h>

#include "keypad.h"
#include "rename.h"
#include "../cheatstore.h"
#include "../keypad_edit.h"
#include "../lang.h"
#include "../rename_edit.h"
#include "../tlog.h"
#include "../ui/art_ids.h"
#include "../ui/dialog.h"
#include "../ui/ui.h"

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

static const struct ui_card_config cs_card = {4, 4, 4, 1, 0};
_Static_assert(sizeof(((struct lang_cheats *)0)->why) / sizeof(const char *) == CHEATFILE_WHYS, "a reason for each");
_Static_assert(sizeof(((struct lang_cheats *)0)->edit_failed) / sizeof(const char *) == CHEATFILE_EDIT_BAD + 1,
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
	ACT_BACK,  // B: the picks saved, back to the caller
	ACT_ADD,   // A on "+ Add code"
	ACT_FILL,  // A on a placeholder: the keypad, the code filled in
	ACT_MENU,  // SELECT on a code of the user's: Edit code, Delete
};

enum {
	MENU_EDIT,
	MENU_DELETE,
	MENU_ITEMS,
};

static struct {
	u32 count;   // rows: the cheats shown, then "+ Add code"
	u32 cursor;  // the list's, as last seen
	u8 list, title, hints;
	u8 b_down;  // B went down on this screen: its release leaves
	u8 act;     // ACT_*, and its row
	u16 icon;
	u32 act_row;
	char counter[24];
	char heading[64];
	char pop_title[64];
	char ask[80];
	struct ui_hint keys[3];
	struct ui_pop_item items[MENU_ITEMS];
	struct ui_popup popup;
	char name[RENAME_TEXT_MAX + 1];  // a code's name, from the rename keyboard
	char code[KEYPAD_CODE_MAX + 1];  // and the code, from the keypad
} cs EWRAM_BSS;

// The cheat row row shows, or NULL for "+ Add code"
static struct cheatfile_cheat *row_cheat(u32 row)
{
	struct cheatstore *s = cheatstore();
	return row + 1 < cs.count ? &s->cheats[s->rows[row]] : NULL;
}

static u32 cs_count(void *ctx)
{
	(void)ctx;
	return cs.count;
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
		out->ink = UI_ROLE_ACCENT_INK;
		return UI_ITEM_READY;
	}
	if (c->desc != CHEATFILE_NONE && c->desc_len) {
		out->text = s->file.text + c->desc;
		out->len = c->desc_len;
	} else {
		out->text = gl_cheats->unnamed;
	}
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

// The hint bar for the cursor's row: a greyed row's reason, a ROM patch's
// note, else the keys (SELECT's menu on a code of the user's)
static void cs_show_hints(void)
{
	struct cheatstore *s = cheatstore();
	const struct cheatfile_cheat *c = row_cheat(cs.cursor);
	if (cs.cursor >= cs.count)
		return;
	if (!c) {
		cs.keys[0] = (struct ui_hint){"A", gl_cheats->hint_add};
		cs.keys[1] = (struct ui_hint){"B", gl_hint_back};
		ui_hints_set(cs.hints, cs.keys, 2);
		return;
	}
	u32 why = cheatfile_why(&s->file, s->rows[cs.cursor]);
	if (why != CHEATFILE_PICKABLE && why < CHEATFILE_WHYS && !c->own) {
		ui_hints_text(cs.hints, gl_cheats->why[why]);
	} else if (why == CHEATFILE_PICKABLE && c->status == CHEATFILE_OK && c->patches && !c->own) {
		ui_hints_text(cs.hints, gl_cheats->nor);
	} else {
		u32 n = 0;
		if (why == CHEATFILE_PICKABLE)
			cs.keys[n++] = (struct ui_hint){"A", gl_cheats->hint_pick};
		if (c->own)
			cs.keys[n++] = (struct ui_hint){"SELECT", gl_cheats->hint_edit};
		cs.keys[n++] = (struct ui_hint){"B", gl_hint_back};
		ui_hints_set(cs.hints, cs.keys, n);
	}
}

static void cs_moved(u32 to)
{
	cs.cursor = to;
	u32 row = to + 1;
	snprintf(cs.counter, sizeof(cs.counter), "%lu/%lu", (unsigned long)row, (unsigned long)cs.count);
	ui_title_set_right(cs.title, cs.counter);
	cs_show_hints();
}

static int cs_event(struct ui_view *v, const struct ui_event *e)
{
	(void)v;
	switch (e->type) {
	case UI_EV_CURSOR:
		cs_moved(e->index);
		return 1;
	case UI_EV_ACTIVATE: {
		struct cheatstore *s = cheatstore();
		if (e->index >= cs.count || cs.act)
			return 1;
		const struct cheatfile_cheat *c = row_cheat(e->index);
		if (!c) {
			cs.act = ACT_ADD;
			cs.act_row = e->index;
			return 1;
		}
		u32 at = s->rows[e->index];
		if (!c->picked && cheatfile_why(&s->file, at) == CHEATFILE_WHY_EDIT) {
			cs.act = ACT_FILL;  // a placeholder: the keypad fills it in
			cs.act_row = e->index;
			return 1;
		}
		if (!cheatfile_toggle(&s->file, at))
			return 1;
		tlog_debug("cheats: %s cheat %u, %lu bytes picked", c->picked ? "picked" : "dropped", c->number,
			   (unsigned long)s->file.bytes);
		// Greyed rows change as the budget left does: every row again
		ui_list_refresh(cs.list, 0, cs.count - 1);
		cs_show_hints();
		return 1;
	}
	case UI_EV_KEYS:
		if (e->keys & INPUT_B) {
			cs.b_down = 1;
			return 1;
		}
		if ((e->keys & INPUT_SELECT) && (ui_input()->pressed & INPUT_SELECT) && !cs.act) {
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
static const struct ui_list_config cs_list = {&cs_model, UI_ROW_PITCH_LIST, UI_LIST_MARQUEE | UI_LIST_MARKS, 0,
					      CS_LIST_PAD};
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

// The rows: every cheat but master codes, then "+ Add code". The row cheat
// at shows, or the last cheat's.
static u32 cs_rows(u32 at)
{
	struct cheatstore *s = cheatstore();
	u32 row = CHEATFILE_NONE;
	cs.count = 0;
	for (u32 i = 0; i < s->file.count; i++)
		if (cheatfile_shown(&s->cheats[i])) {
			if (i == at)
				row = cs.count;
			s->rows[cs.count++] = (u16)i;
		}
	cs.count++;  // + Add code
	return row != CHEATFILE_NONE ? row : (cs.count > 1 ? cs.count - 2 : 0);
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
	row = row < cs.count ? row : cs.count - 1;
	ui_list_set_cursor(cs.list, row);
	cs_moved(row);
	return 1;
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
	if (at < s->file.count && !s->cheats[at].picked && cheatfile_why(&s->file, at) == CHEATFILE_PICKABLE)
		cheatfile_toggle(&s->file, at);
}

// Writes the file after an edit; a dialog if the card didn't take it
static void cs_write(void)
{
	if (!cheatstore_write())
		dialog_message(cs.heading, gl_cheats->save_failed, DIALOG_ERROR);
}

// The keypad for a new code (at CHEATFILE_NONE), cheat at's placeholder
// (fill) or a code of the user's, then the rename keyboard for its name
// (not a placeholder's), then the edit into the file and the file written.
// The row to show after.
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
	cs.name[0] = 0;
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
		if (c->desc != CHEATFILE_NONE)
			snprintf(cs.name, sizeof(cs.name), "%.*s", (int)c->desc_len, f->text + c->desc);
	}
	u8 format = ask.format;
	char was[sizeof(cs.name)];
	memcpy(was, cs.name, sizeof(was));
	for (;;) {
		if (!keypad_run(&ask, cs.code, &format))
			return row;  // cancelled: nothing changes
		ask.code = cs.code;  // back here from the keyboard: as typed
		ask.code_len = (u32)strlen(cs.code);
		ask.format = format;
		if (fill)
			break;
		// Its name: a code of the user's keeps its own unless changed;
		// Cancel goes back to the keypad, the code as typed
		const struct rename_ask name = {
			cs.icon, was, NULL, RENAME_GAME_MAX, 0, name_submit, 0, gl_cheats->name_title, (u8)(c != NULL)};
		if (rename_run(&name))
			break;
	}
	u32 r, idx = at;
	char *buf = (char *)s->text;
	if (!c)
		r = cheatfile_add(f, buf, sizeof(s->text), cs.name, cs.code, format, &idx);
	else
		r = cheatfile_set_code(f, buf, sizeof(s->text), at, fill ? NULL : cs.name, cs.code, format);
	tlog("cheats: %s cheat %lu: %s", c ? (fill ? "filled in" : "edited") : "added", (unsigned long)idx,
	     r == CHEATFILE_EDIT_OK ? "ok" : gl_cheats->edit_failed[r < CHEATFILE_EDIT_BAD + 1 ? r : 0]);
	if (r != CHEATFILE_EDIT_OK) {
		dialog_message(cs.heading, gl_cheats->edit_failed[r <= CHEATFILE_EDIT_BAD ? r : 0], DIALOG_ERROR);
		return row;
	}
	cs_redecode();
	pick_new(idx);
	cs_write();
	return cs_rows(idx);
}

// SELECT's menu on a code of the user's: Edit code, or Delete (asked
// first). The row to show after, or CHEATFILE_NONE to stay on the view.
static u32 cs_menu(u32 row)
{
	struct cheatstore *s = cheatstore();
	u32 at = s->rows[row];
	const struct cheatfile_cheat *c = &s->cheats[at];
	if (c->desc != CHEATFILE_NONE && c->desc_len)
		snprintf(cs.pop_title, sizeof(cs.pop_title), "%.*s", (int)c->desc_len, s->file.text + c->desc);
	else
		snprintf(cs.pop_title, sizeof(cs.pop_title), "%s", gl_cheats->unnamed);
	cs.items[MENU_EDIT] = (struct ui_pop_item){gl_cheats->menu_edit, 0, 0};
	cs.items[MENU_DELETE] = (struct ui_pop_item){gl_cheats->menu_delete, 0, UI_POP_DANGER};
	cs.popup = (struct ui_popup){cs.pop_title, 0, cs.items, MENU_ITEMS, 0, 0};
	if (!ui_popup_open(cs.list, cs.hints, &cs.popup))
		return CHEATFILE_NONE;
	tlog_debug("screen ready: cheat-menu");
	for (;;) {
		s32 pick = ui_popup_choose();
		if (pick == MENU_EDIT) {
			ui_popup_close();
			cs_leave();
			return cs_code(at, 0);
		}
		if (pick != MENU_DELETE)
			return CHEATFILE_NONE;  // B: it has closed
		snprintf(cs.ask, sizeof(cs.ask), EN_CHEAT_DELETE, cs.pop_title);
		struct ui_confirm ask = {cs.ask, gl_cheats->delete_text, gl_cheats->delete_button, 1};
		if (!ui_confirm(cs.hints, &ask))
			continue;  // Cancel: back to the menu
		ui_popup_close();
		u32 r = cheatfile_remove(&s->file, (char *)s->text, sizeof(s->text), at);
		tlog("cheats: deleted cheat %lu: %s", (unsigned long)at, r == CHEATFILE_EDIT_OK ? "ok" : "refused");
		cs_leave();
		cs_redecode();
		cs_write();
		return cs_rows(at);
	}
}

void cheats_run(const char *rom_path, const char *title, u16 icon)
{
	memset(&cs, 0, sizeof(cs));
	int opened = cheatstore_open(rom_path);
	if (opened == CHEATSTORE_NONE) {
		dialog_message(title, gl_cheats->gone, DIALOG_ERROR);
		return;
	}
	cs_decode();
	cheatstore_listed(0);  // decoded: the game popup's count from now on
	struct cheatstore *s = cheatstore();
	cs_rows(CHEATFILE_NONE);
	u32 row = 0, hidden = s->file.count - (cs.count - 1);
	tlog("cheats: %s: %lu cheats, %lu master codes hidden, %lu picked (%lu bytes)%s", s->path,
	     (unsigned long)(cs.count - 1), (unsigned long)hidden, (unsigned long)cheatfile_picks(&s->file),
	     (unsigned long)s->file.bytes, opened == CHEATSTORE_NEW ? ", no file yet" : "");
	snprintf(cs.heading, sizeof(cs.heading), "%s", title);
	cs.icon = icon;
	int up = cs_show(row);
	while (up) {
		// ui_run logs "screen ready: cheats" once it is on screen and still
		cs.act = ACT_NONE;
		while (ui_depth() && !cs.act) {
			ui_frame();
			if (cs.b_down && (ui_input()->released & INPUT_B))
				cs.act = ACT_BACK;
		}
		if (cs.act == ACT_MENU) {
			u32 to = cs_menu(cs.act_row);
			if (to != CHEATFILE_NONE)
				up = cs_show(to);  // after an edit or a delete; else the view is still up
			continue;
		}
		if (cs.act != ACT_ADD && cs.act != ACT_FILL)
			break;
		cs_leave();
		row = cs.act == ACT_ADD ? cs_code(CHEATFILE_NONE, 0) : cs_code(s->rows[cs.act_row], 1);
		up = cs_show(row);
	}
	int changed = cheatfile_changed(&s->file);
	tlog("cheats: back, %lu picked%s", (unsigned long)cheatfile_picks(&s->file), changed ? ", changed" : "");
	int saved = cheatstore_write();
	ui_leave(UI_TR_DEEPER, -1);  // back to the caller's screen
	while (ui_depth())
		ui_pop();
	if (!saved)
		dialog_message(cs.heading, gl_cheats->save_failed, DIALOG_ERROR);
}
