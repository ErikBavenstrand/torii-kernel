// The cheat screen (cheats.h), in the game lists' look: the title bar with
// the game's name and which row the cursor is on, the cheats on a card, and
// the hint bar, which says why a greyed row can't be picked. The file, its
// cheats and the row map stay in pReadCache (READCACHE_CHEATS, past the
// asset jobs' stage) while the screen runs: it reads the file once, and A
// only does arithmetic on what decoding found (cheatfile.h). v0.7.1's "Add
// code" row goes after the cheats.
#include "cheats.h"

#include <stdio.h>
#include <string.h>

#include <gba_input.h>

#include "../cheatstore.h"
#include "../lang.h"
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

enum {
	CS_TITLE,
	CS_HINTS,
	CS_CARD,
	CS_LIST,
	CS_SCROLL,
	CS_NODES,
};

static struct {
	u32 count;   // rows
	u32 cursor;  // the list's, as last seen
	u8 list, title, hints;
	u8 b_down;  // B went down on this screen: its release leaves
	char counter[24];
	char heading[64];
	struct ui_hint keys[2];
} cs EWRAM_BSS;

static struct cheatfile_cheat *row_cheat(u32 row)
{
	struct cheatstore *s = cheatstore();
	return &s->cheats[s->rows[row]];
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
// note, else the keys
static void cs_show_hints(void)
{
	struct cheatstore *s = cheatstore();
	if (cs.cursor >= cs.count) {
		ui_hints_set(cs.hints, cs.keys, 2);
		return;
	}
	u32 why = cheatfile_why(&s->file, s->rows[cs.cursor]);
	const struct cheatfile_cheat *c = row_cheat(cs.cursor);
	if (why != CHEATFILE_PICKABLE && why < CHEATFILE_WHYS)
		ui_hints_text(cs.hints, gl_cheats->why[why]);
	else if (c->status == CHEATFILE_OK && c->patches)
		ui_hints_text(cs.hints, gl_cheats->nor);
	else
		ui_hints_set(cs.hints, cs.keys, 2);
}

static void cs_moved(u32 to)
{
	cs.cursor = to;
	snprintf(cs.counter, sizeof(cs.counter), "%lu/%lu", (unsigned long)(to + 1), (unsigned long)cs.count);
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
		if (e->index >= cs.count)
			return 1;
		u32 at = s->rows[e->index];
		if (!cheatfile_toggle(&s->file, at))
			return 1;
		const struct cheatfile_cheat *c = &s->cheats[at];
		tlog_debug("cheats: %s cheat %u, %lu writes picked", c->picked ? "picked" : "dropped", c->number,
			   (unsigned long)s->file.writes);
		// Greyed rows change as the writes picked do: every row again
		ui_list_refresh(cs.list, 0, cs.count - 1);
		cs_show_hints();
		return 1;
	}
	case UI_EV_KEYS:
		if (e->keys & INPUT_B) {
			cs.b_down = 1;
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

void cheats_run(const char *rom_path, const char *title, u16 icon)
{
	memset(&cs, 0, sizeof(cs));
	if (!cheatstore_read(rom_path)) {
		dialog_message(title, gl_cheats->gone, DIALOG_ERROR);
		return;
	}
	cs_decode();
	struct cheatstore *s = cheatstore();
	u32 hidden = 0;
	for (u32 i = 0; i < s->file.count; i++) {
		if (cheatfile_shown(&s->cheats[i]))
			s->rows[cs.count++] = (u16)i;
		else
			hidden++;
	}
	tlog("cheats: %s: %lu cheats, %lu master codes hidden, %lu picked (%lu writes)", s->path,
	     (unsigned long)cs.count, (unsigned long)hidden, (unsigned long)cheatfile_picks(&s->file),
	     (unsigned long)s->file.writes);
	if (!cs.count) {
		dialog_message(title, gl_cheats->none, DIALOG_INFO);
		return;
	}
	snprintf(cs.heading, sizeof(cs.heading), "%s", title);
	struct ui_view *v = ui_push(&cs_view, NULL);
	if (!v) {
		tlog_error("cheats: %s", ui_error());
		return;
	}
	cs.list = (u8)(v->first + CS_LIST);
	cs.title = (u8)(v->first + CS_TITLE);
	cs.hints = (u8)(v->first + CS_HINTS);
	ui_title_set(cs.title, cs.heading);
	ui_title_icon_set(cs.title, icon);
	cs.keys[0] = (struct ui_hint){"A", gl_cheats->hint_pick};
	cs.keys[1] = (struct ui_hint){"B", gl_hint_back};
	ui_list_set_cursor(cs.list, 0);
	cs_moved(0);
	// ui_run logs "screen ready: cheats" once it is on screen and still
	while (ui_depth()) {
		ui_frame();
		if (cs.b_down && (ui_input()->released & INPUT_B))
			break;
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
