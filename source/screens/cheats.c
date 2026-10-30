// The cheat screen (cheats.h), in the game lists' look: the title bar with
// the game's name, which row the cursor is on and how many cheats are on,
// the cheats on a card, and the hint bar, which says why a greyed row can't
// be picked beside its keys. The file, its cheats and the row map stay in
// pReadCache (READCACHE_CHEATS, past the asset jobs' stage), and its text in
// PSRAM (cheatstore.h), while the screen runs, the code keypad, the rename
// keyboard, the Guide page and the Buttons page included: it reads the file once, and A only
// does arithmetic on what decoding found (cheatfile.h). An edit (a code
// added, filled in, edited or deleted) changes the text in place, decodes it
// again and writes the file at once.
#include "cheats.h"

#include <stdio.h>
#include <string.h>

#include <gba_input.h>

#include "keypad.h"
#include "rename.h"
#include "settings_layout.h"
#include "../actions.h"
#include "../cheatstore.h"
#include "../ez_define.h"
#include "../Ezcard_OP.h"
#include "../keypad_edit.h"
#include "../lang.h"
#include "../rename_edit.h"
#include "../tlog.h"
#include "../engine/frame.h"
#include "../engine/text.h"
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
// The Guide page's activation box: its corners' radius, and px it keeps
// clear above its first row and below its last; the divider's inset
#define CS_BOX_RADIUS 3
#define CS_BOX_GAP 1
#define CS_RULE_INSET 0
#define CS_BOX_TEXT 12  // the box's text, past its icon
// The Buttons page: the cheat's name over its rows, the rows (each 16 px),
// the tick's column and the labels', a key chip's narrowest and the gap
// between chips; the capture's hold and its time out (Settings' hotkeys')
#define BP_NAME_Y 24
#define BP_NAME_H 16
#define BP_LIST_Y 40
#define BP_ROW_H 16
#define BP_LIST_H (4 * BP_ROW_H)
#define BP_TICK_X 4
#define BP_TEXT_X 16
#define BP_CHIP_MIN 13
#define BP_CHIP_GAP 2
#define BP_HOLD_FRAMES 18
#define BP_WAIT 300
#define BP_BAR_W 40
#define BP_BAR_H 3
#define BP_BAR_GAP 6
#define BP_REFUSED_FRAMES 120  // a refused combination's reason shows, unless the cursor moves

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
	ACT_GUIDE,  // SELECT on a cheat: its Guide page
	ACT_START,  // START: the menu
	ACT_WINDOW,  // the cursor went past a larger file's window: the next one (cs.back: the one before)
	ACT_GROUP,   // A on a run of variants' row: its choices
};

// START's menu's items
enum {
	MENU_GUIDE,
	MENU_EDIT,
	MENU_RENAME,
	MENU_BUTTONS,
	MENU_EDIT_GUIDE,
	MENU_DELETE,
	MENU_ALL_OFF,
	MENU_ONLY,
	MENU_ITEMS,
};

// The Buttons page's rows
enum {
	BP_ALWAYS,
	BP_HOLD,
	BP_PRESS,
	BP_CODE,
	BP_ROWS,
};
_Static_assert(STR_CHEATS_BUTTONS_CODE - STR_CHEATS_BUTTONS_ALWAYS == BP_CODE, "a label for each row");
_Static_assert(STR_CHEATS_EXPLAIN_CODE - STR_CHEATS_EXPLAIN_ALWAYS == BP_CODE, "a line for each row");

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
	u8 close;    // B on the Guide page or the Buttons page
	u8 back;     // ACT_WINDOW goes to the window before
	u8 progressing;  // cs_progress's dialog is up
	u8 folded;   // a row stands for a run of variants (cheatfile_group)
	// The run of variants whose choices show (its first cheat), and past its
	// last; CHEATFILE_NONE: every cheat. own: the bytes their names share.
	u32 group, group_end, group_own;
	char group_sub[48];  // the title's after-text while it shows
	char detail[48];     // a run's row's right: its pick's own part, or its count
	u16 icon;
	u32 act_row;
	char counter[32];
	char heading[64];
	char pop_title[64];
	char ask[128];
	char reason[48];  // a greyed row's reason with numbers in it
	char note[64];    // the hint bar's line beside the keys: an activation's sentence
	char tag[16];     // a row's activation tag ("L+1"), drawn before the next fetch
	u16 box_from, box_to;  // the Guide page's text: the activation box's bytes
	u16 rule_at;      // and its divider's line (CHEATSTORE_DETAIL: none)
	char new_name[16];  // a new code's name until the user gives one
	struct ui_hint keys[4];
	struct ui_pop_item items[MENU_ITEMS];
	u8 item_ids[MENU_ITEMS];
	struct ui_popup popup;
	struct ui_reading reading;  // the Guide page's
	// The Buttons page: the value as chosen and as it was, the row the
	// cursor is on, whether the cheat is set at launch whatever is chosen,
	// a capture under way (its row) and the hint bar's countdown as drawn
	struct {
		u16 keys, was;
		u8 row, locked, ungated, capturing, bar;  // ungated: Hold and Press can't be kept
		u8 list, hints, name;
		u8 refused;  // frames a refused combination's reason stays
		struct settings_capture cap;
		char value[48];  // From the code's
		char why[64];    // the refused combination's reason
	} bp;
	char name[RENAME_TEXT_MAX + 1];  // a code's name, from the rename keyboard
	char code[KEYPAD_CODE_MAX + 1];  // and the code, from the keypad
} cs EWRAM_BSS;

// The cheat row row shows, or NULL for "+ Add code"
static struct cheatfile_cheat *row_cheat(u32 row)
{
	struct cheatstore *s = cheatstore();
	return row < cs.shown ? &s->cheats[s->rows[row]] : NULL;
}

// 1 if row stands for a run of variants (in the list of all), from *start
// to *end (cheatfile_group)
static int row_group(u32 row, u32 *start, u32 *end)
{
	struct cheatstore *s = cheatstore();
	if (cs.group != CHEATFILE_NONE || cs.only || row >= cs.shown)
		return 0;
	*start = cheatfile_group(&s->file, s->rows[row], end);
	// Folded: the run's first, its other cheats not rows of their own (a
	// run the decode found after cs_rows still has them, until it folds)
	return *end - *start >= 2 && *start == s->rows[row] && (row + 1 >= cs.shown || s->rows[row + 1] >= *end);
}

// n bytes of name into out (size bytes, ended), cut at a character
static void name_copy(char *out, u32 size, const char *name, u32 n)
{
	if (n >= size) {
		n = size - 1;
		while (n && ((u8)name[n] & 0xC0) == 0x80)
			n--;  // not inside a character
	}
	memcpy(out, name, n);
	out[n] = 0;
}

static const char *cheat_name(const struct cheatfile_cheat *c, u32 *len);

// The run's name for the title (its shared bytes, else "Pick one") and
// cs.group_own, from start to end
static void group_names(u32 start, u32 end)
{
	struct cheatstore *s = cheatstore();
	u32 len, name = cheatfile_group_name(&s->file, start, end, &cs.group_own);
	const char *p = cheat_name(&s->cheats[start], &len);
	if (name)
		name_copy(cs.group_sub, sizeof(cs.group_sub), p, name);
	else
		snprintf(cs.group_sub, sizeof(cs.group_sub), "%s", S(STR_CHEATS_CHOICES_SUB));
}

// The picked cheat of a run, CHEATFILE_NONE if none is
static u32 group_pick(u32 start, u32 end)
{
	for (u32 i = start; i < end; i++)
		if (cheatstore()->cheats[i].picked)
			return i;
	return CHEATFILE_NONE;
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
	u32 len, start, end;
	if (row_group(i, &start, &end)) {
		// A run: named by what its names share (else as its pick, or its
		// first), its pick's own part or its count at the right
		u32 own, pick = group_pick(start, end);
		u32 name = cheatfile_group_name(&s->file, start, end, &own);
		const struct cheatfile_cheat *shown = &s->cheats[pick != CHEATFILE_NONE && !name ? pick : start];
		out->text = cheat_name(shown, &len);
		out->len = (u16)(name ? name : len);
		if (pick != CHEATFILE_NONE) {
			out->mark = ART_ICON8_TICK + 1;
			if (name) {
				const char *p = cheat_name(&s->cheats[pick], &len);
				u32 k = cheatfile_own_part(p, len, own);
				name_copy(cs.detail, sizeof(cs.detail), p + k, len - k);
				out->detail = cs.detail;
			}
		}
		if (!out->detail) {
			SF(cs.detail, sizeof(cs.detail), STR_N(STR_CHEATS_CHOICES, end - start), LN(end - start));
			out->detail = cs.detail;
		}
		return UI_ITEM_READY;
	}
	out->text = cheat_name(c, &len);
	out->len = (u16)len;
	if (cs.group != CHEATFILE_NONE && cs.group_own) {
		// A choice of a run: its own part of the name
		u32 k = cheatfile_own_part(out->text, len, cs.group_own);
		out->text += k;
		out->len = (u16)(len - k);
	}
	// Not decoded yet: as one that can be picked, until the background has it
	int greyed = c->status != CHEATFILE_NEW && cheatfile_why(&s->file, s->rows[i]) != CHEATFILE_PICKABLE;
	if (greyed)
		out->ink = UI_ROLE_DIM;
	if (c->picked) {
		out->mark = ART_ICON8_TICK + 1;
		out->mark_ink = c->status == CHEATFILE_OK || c->status == CHEATFILE_NEW ? 0 : UI_ROLE_DIM;
	}
	// A Hold or Press cheat's first button at the right, dim ("L+1")
	if (cheatkeys_tag(cheatfile_activation(c), cs.tag, sizeof(cs.tag)))
		out->detail = cs.tag;
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

// The hint bar's line for cheat c, which A may pick (or not decoded yet):
// how it is activated if it takes buttons, else a ROM patch's note, else
// when it is set at launch; NULL for one always on, or unknown
static const char *cs_activation_note(const struct cheatfile_cheat *c)
{
	if (c->status == CHEATFILE_NEW)
		return NULL;
	u16 act = cheatfile_activation(c);
	u32 mode = cheatkeys_mode(act);
	if (mode == CHEATKEYS_HOLD || mode == CHEATKEYS_PRESS || (mode == CHEATKEYS_LAUNCH && !c->patches)) {
		cheatkeys_sentence(act, cs.note, sizeof(cs.note));
		return cs.note;
	}
	if (c->status == CHEATFILE_OK && c->patches)
		return S(STR_CHEATS_NOR);
	return NULL;
}

// The hint bar for the cursor's row: its keys, and beside them a greyed
// row's reason, else how the cheat is activated (cs_activation_note). A
// and B always show; SELECT's guide, then START's menu, give way when the
// line needs their room.
static void cs_show_hints(void)
{
	static const struct ui_hint_note beside = {0, 0, 0, 0, 0, 0, 1};
	struct cheatstore *s = cheatstore();
	const struct cheatfile_cheat *c = row_cheat(cs.cursor);
	const char *note = NULL;
	struct ui_hint a = {0, 0};
	if (cs.cursor >= cs.count)
		return;
	u32 start, end;
	int run = row_group(cs.cursor, &start, &end);
	if (run) {
		a = (struct ui_hint){"A", S(STR_HINT_OPEN)};
	} else if (!c) {
		if (cs.add_why == CHEATFILE_EDIT_OK)
			a = (struct ui_hint){"A", S(STR_CHEATS_HINT_ADD)};
		else
			note = add_reason();
	} else {
		// Not decoded yet: as a cheat that can be picked (most are)
		u32 why = c->status == CHEATFILE_NEW ? CHEATFILE_PICKABLE : cheatfile_why(&s->file, s->rows[cs.cursor]);
		if (why == CHEATFILE_PICKABLE)
			a = (struct ui_hint){"A", c->picked ? S(STR_CHEATS_HINT_DROP) : S(STR_CHEATS_HINT_PICK)};
		else if (why == CHEATFILE_WHY_EDIT)
			a = (struct ui_hint){"A", S(STR_CHEATS_HINT_FILL)};
		note = why != CHEATFILE_PICKABLE ? cs_reason(c, why) : cs_activation_note(c);
	}
	int guide = c != NULL && !run, menu = 1;
	u32 n, room = (u32)ui_node(cs.hints)->w;
	for (;;) {
		n = 0;
		if (a.key)
			cs.keys[n++] = a;
		if (guide)
			cs.keys[n++] = (struct ui_hint){"SELECT", S(STR_CHEATS_HINT_GUIDE)};
		if (menu)
			cs.keys[n++] = (struct ui_hint){"START", S(STR_CHEATS_HINT_MENU)};
		cs.keys[n++] = (struct ui_hint){"B", cs.only ? S(STR_CHEATS_HINT_ALL) : S(STR_HINT_BACK)};
		if (ui_hints_width(cs.keys, n, note) <= room || (!guide && !menu))
			break;
		if (guide)
			guide = 0;
		else
			menu = 0;
	}
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
	if (cheatstore_windows() && cs.group == CHEATFILE_NONE) {
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
		if (cheatstore_windows() > 1 && !cs.act && cs.group == CHEATFILE_NONE) {
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
		u32 start, end;
		if (row_group(e->index, &start, &end)) {
			cs.act = ACT_GROUP;  // its choices
			cs.act_row = e->index;
			return 1;
		}
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
		if (!cheatstore_toggle_one(at))  // one of a run of variants at a time
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
			u32 start, end;
			if (row_cheat(cs.cursor) && !row_group(cs.cursor, &start, &end)) {
				cs.act = ACT_GUIDE;
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

// --- The Guide page: a cheat's name, how it is activated, its guide, then its
// code and what decoding found, read-only -----------------------------------

static int gv_event(struct ui_view *v, const struct ui_event *e)
{
	(void)v;
	if (e->type != UI_EV_KEYS || !(e->keys & ui_input()->pressed & INPUT_B))
		return 0;
	cs.close = 1;
	return 1;
}

// [x, x + w) x [y, y + h) of a painted row in role, its top corners rounded
// by r if top, its bottom ones if bottom
static void row_fill(const struct ui_paint *p, u32 x, u32 y, u32 w, u32 h, u32 r, int top, int bottom, u32 role)
{
	for (u32 e = 0; e < h; e++) {
		u32 in = 0;
		if (top && e < r)
			in = ui_round_inset(r, e);
		else if (bottom && h - 1 - e < r)
			in = ui_round_inset(r, h - 1 - e);
		text_clear(p->win, p->x + x + in, p->y + y + e, w - 2 * in, 1, role);
	}
}

// A row of the Guide page: the cheat's name (a heading), the activation
// box's rows on a tint, the divider under the guide, and plain lines
static int gv_paint(void *ctx, const struct ui_paint *p)
{
	const struct ui_reading *r = ctx;
	if (p->index >= r->count)
		return 0;
	const struct torii_font *font = ui_theme()->body;
	u32 i = p->index, start = r->start[i], len = r->len[i], kind = r->kind[i];
	const char *s = r->text + start;
	u32 top = p->y + (p->rh > font->line_height ? (p->rh - font->line_height) / 2 : 0);
	u32 x = p->x + CS_LIST_PAD, clip = p->w > 2 * CS_LIST_PAD ? p->w - 2 * CS_LIST_PAD : 8;
	u32 ink = UI_ROLE_TEXT;
	if (start == cs.rule_at && !len) {
		text_clear(p->win, p->x + CS_RULE_INSET, p->y + p->rh / 2, p->w - 2 * CS_RULE_INSET, 1, UI_ROLE_BORDER);
		return 1;
	}
	if (start >= cs.box_from && start < cs.box_to) {
		int first = start == cs.box_from, last = i + 1 >= r->count || r->start[i + 1] >= cs.box_to;
		u32 y0 = first ? CS_BOX_GAP : 0, y1 = last ? p->rh - CS_BOX_GAP : p->rh;
		row_fill(p, 0, y0, p->w, y1 - y0, CS_BOX_RADIUS, first, last, UI_ROLE_TINT);
		if (first)
			ui_icon8(p->win, x, top + (font->line_height - 8) / 2, ART_ICON8_KEYS, UI_ROLE_ACCENT_INK);
		x += CS_BOX_TEXT;
		clip = clip > CS_BOX_TEXT + 8 ? clip - CS_BOX_TEXT : 8;
		ink = UI_ROLE_ACCENT_INK;
	} else if (kind == UI_READ_HEADING || kind == UI_READ_HEADING_MORE) {
		ink = UI_ROLE_ACCENT_INK;
		if (kind == UI_READ_HEADING) {
			s += 2;
			len -= 2;
		}
	}
	if (len)
		text_draw(p->win, font, x, top, clip, ink, s, len);
	return 1;
}

// The rows as the reading model has them (gv_paint draws them)
static u32 cs_reading_count(void *ctx)
{
	return ui_reading_model.count(ctx);
}

static int cs_reading_fetch(void *ctx, u32 i, struct ui_item *out)
{
	return ui_reading_model.fetch(ctx, i, out);
}

static const struct ui_list_model gv_model = {cs_reading_count, cs_reading_fetch, 0, gv_paint, 0};
static const struct ui_list_config gv_list = {&gv_model, UI_ROW_PITCH_LIST, UI_LIST_PLAIN | UI_LIST_SCROLL, 0,
					      CS_LIST_PAD};
static const struct ui_spec gv_nodes[CS_NODES] = {
	[CS_TITLE] = {UI_TITLE, 1, UI_NONE, 3, UI_HFILL, 0, 0, 0, 0, 0, 0, CS_TITLE_H, &cs_title},
	[CS_HINTS] = {UI_HINTS, 2, UI_NONE, 3, UI_HFILL | UI_BOTTOM, 0, 0, 0, 0, 0, 0, CS_HINTS_H, &cs_no_hints},
	[CS_CARD] = {UI_CARD, 3, UI_NONE, 2, UI_HFILL, 0, 0, 0, 0, CS_CARD_Y, 0, CS_CARD_H, &cs_card},
	[CS_LIST] = {UI_LIST, 4, UI_NONE, 1, UI_LEFT, UI_FOCUSABLE, 0, 0, CS_LIST_X, CS_LIST_Y, CS_LIST_W, CS_LIST_H,
		     &gv_list},
	[CS_SCROLL] = {UI_SCROLLBAR, 5, UI_NONE, 2, UI_LEFT, 0, 0, UI_ROLE_PANEL, CS_LIST_X + CS_LIST_W, CS_LIST_Y, 8,
		       CS_LIST_H, &cs_bar},
};
static const struct ui_view_spec gv_view = {"cheat-guide", gv_nodes, CS_NODES, CS_LIST, 0, gv_event};

// Appends to the page's text (at *n), cut at its end
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

// A code line: AnyGBA's '^' and '|' spaced out, which the font's '|' needs
// to read apart from '1', and kept with what follows (a no-break space), so
// a long line wraps before its key and the wrapped part starts with '^'
static void code_line(u32 *n, const char *s, u32 len)
{
	u32 from = 0;
	for (u32 i = 0; i < len; i++)
		if (s[i] == '^' || s[i] == '|') {
			detail_add(n, s + from, i - from);
			detail_add(n, s[i] == '^' ? " ^\xC2\xA0" : " |\xC2\xA0", 4);
			from = i + 1;
		}
	detail_add(n, s + from, len - from);
	detail_add(n, "\n", 1);
}

// Cheat at's guide, its lines as the page's own: a tab as a space, and a
// line that starts "# " kept from reading as a heading (a no-break space)
static void guide_add(u32 *n, u32 at)
{
	struct cheatstore *s = cheatstore();
	char *d = s->detail;
	u32 from = *n;
	if (from + 2 >= CHEATSTORE_DETAIL)
		return;
	u32 len = cheatfile_guide(&s->file, at, d + from, CHEATSTORE_DETAIL - 1 - from);
	if (!len || len >= CHEATSTORE_DETAIL - 1 - from)
		return;
	*n = from + len;
	for (u32 i = from; i < *n; i++) {
		if (d[i] == '\t')
			d[i] = ' ';
		if (d[i] == '#' && i + 1 < *n && (i == from || d[i - 1] == '\n') && d[i + 1] == ' ' &&
		    *n + 1 < CHEATSTORE_DETAIL - 1) {
			memmove(d + i + 3, d + i + 2, *n - i - 2);
			d[i + 1] = (char)0xC2;
			d[i + 2] = (char)0xA0;
			(*n)++;
		}
	}
	d[*n] = 0;
	detail_add(n, "\n", 1);
}

// Cheat at's page: its name as the heading; the box with a greyed cheat's
// reason, else how it is activated; its guide and a divider under it; its
// code a line each, its format, its share of the budget, its ROM patches
// and whether it is picked
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
	u32 why = cheatfile_why(f, at);
	const char *box = why != CHEATFILE_PICKABLE ? cs_reason(c, why) : NULL;
	if (!box && cheatkeys_sentence(cheatfile_activation(c), cs.note, sizeof(cs.note)))
		box = cs.note;
	cs.box_from = cs.box_to = (u16)CHEATSTORE_DETAIL;
	cs.rule_at = (u16)CHEATSTORE_DETAIL;
	if (box && *box) {
		cs.box_from = (u16)n;
		detail_line(&n, box);
		cs.box_to = (u16)(n - 1);
	}
	u32 had = n;
	guide_add(&n, at);
	if (n != had) {
		cs.rule_at = (u16)n;
		detail_add(&n, "\n", 1);
	}
	// The code: lines between its '+'s, but a pointer's offset's
	const char *code = f->text + c->code;
	for (u32 i = 0, from = 0; i <= c->code_len; i++)
		if (i == c->code_len ||
		    (code[i] == '+' && !cheat_plus_joins(code + from, c->code_len - from, i - from))) {
			code_line(&n, code + from, i - from);
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
	detail_line(&n, c->picked ? S(STR_CHEATS_DETAIL_PICKED) : S(STR_CHEATS_DETAIL_NOT_PICKED));
}

// Wraps the page's text from byte from up to byte to (its end if sooner;
// to's line break left out) width px wide, onto cs.reading's rows
static void wrap_part(u32 from, u32 to, u32 width)
{
	struct ui_reading *r = &cs.reading;
	char *text = cheatstore()->detail;
	if (from >= CHEATSTORE_DETAIL)
		return;
	u32 len = (u32)strnlen(text + from, CHEATSTORE_DETAIL - from) + from;
	if (from >= len)
		return;
	if (to > len)
		to = len;
	if (to < len && text[to - 1] == '\n')
		to--;
	int cut = to < len && to < CHEATSTORE_DETAIL;
	char kept = cut ? text[to] : 0;
	if (cut)
		text[to] = 0;
	struct ui_reading part = {text + from,        r->start + r->count,      r->len + r->count,
				  r->kind + r->count, (u16)(r->cap - r->count), 0};
	ui_reading_wrap(&part, ui_theme()->body, width, ui_theme()->look.reading_key_w);
	for (u32 i = 0; i < part.count; i++)
		part.start[i] = (u16)(part.start[i] + from);
	r->count = (u16)(r->count + part.count);
	if (cut)
		text[to] = kept;
}

// The Guide page of cheat at in the cheat view's place, until B; then
// nothing shows (the caller shows the cheat view again)
static void cs_guide(u32 at)
{
	struct cheatstore *s = cheatstore();
	if (at >= s->file.count)
		return;
	cheatstore_decode_to(at);
	detail_text(at);
	cs.reading = (struct ui_reading){s->detail,      s->detail_start,        s->detail_len,
					 s->detail_kind, CHEATSTORE_DETAIL_ROWS, 0};
	// The box's lines narrower, past its icon
	u32 w = CS_LIST_W - 2 * CS_LIST_PAD;
	if (cs.box_from < CHEATSTORE_DETAIL) {
		wrap_part(0, cs.box_from, w);
		wrap_part(cs.box_from, cs.box_to + 1u, w - CS_BOX_TEXT);
		wrap_part(cs.box_to + 1u, CHEATSTORE_DETAIL, w);
	} else {
		wrap_part(0, CHEATSTORE_DETAIL, w);
	}
	ui_leave(UI_TR_FADE, 1);
	while (ui_depth())
		ui_pop();
	struct ui_view *v = ui_push(&gv_view, &cs.reading);
	if (!v) {
		tlog_error("cheats: %s", ui_error());
		return;
	}
	u8 title = (u8)(v->first + CS_TITLE);
	ui_title_set(title, cs.heading);
	ui_title_icon_set(title, cs.icon);
	ui_title_set_sub(title, S(STR_CHEATS_GUIDE_SUB));
	cs.keys[0] = (struct ui_hint){"B", S(STR_HINT_BACK)};
	ui_hints_set((u8)(v->first + CS_HINTS), cs.keys, 1);
	tlog_debug("cheats: guide of cheat %lu, %lu rows", (unsigned long)at, (unsigned long)cs.reading.count);
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

static u32 cs_rows(u32 at);

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
		if (cs.only || cs.folded || cs.group != CHEATFILE_NONE)
			ui_list_refresh(cs.list, 0, cs.shown ? cs.shown - 1 : 0);  // rows aren't cheats one to one
		else if (from < cs.shown)
			ui_list_refresh(cs.list, from, (to < cs.shown ? to : cs.shown) - 1);
		u32 at = row_cheat(cs.cursor) ? s->rows[cs.cursor] : CHEATFILE_NONE;
		if (at >= from && at < to)
			cs_show_hints();
		if (to >= s->file.count) {
			cheatstore_listed();  // decoded: the game popup's count from now on
			cs.add_why = (u8)cheatstore_can_add(cs.new_name);
			// Runs of variants are known now: their rows fold in
			u32 count = cs.count;
			if (!cs.only && cs.group == CHEATFILE_NONE) {
				u32 row = cs_rows(at);
				if (cs.count != count || cs.folded) {
					ui_list_reset(cs.list, row);
					cs_moved(row);
				}
			}
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

// The rows: every cheat, a run of variants (cheatfile_group) as one row
// (only the picked ones while cs.only, and all again if none is; only a
// run's while cs.group, and all again once it is no run), then "+ Add code"
// (not while cs.only or cs.group); the picks' count, a new code's name and
// whether one may be added. The row cheat at shows (its run's), or the last
// cheat's.
static u32 cs_rows(u32 at)
{
	struct cheatstore *s = cheatstore();
	u32 row = CHEATFILE_NONE, own = 0;
	for (u32 i = 0; i < s->file.count; i++)
		own += s->cheats[i].own;
	if (cs.group != CHEATFILE_NONE && s->file.count) {
		u32 first = cs.group < s->file.count ? cs.group : s->file.count - 1;
		cs.group = cheatstore_run(first, &cs.group_end);  // decoded again after an edit
		if (cs.group_end - cs.group < 2)
			cs.group = CHEATFILE_NONE;  // an edit left it no run
		else
			group_names(cs.group, cs.group_end);
	}
	cs.folded = 0;
	for (;;) {
		cs.shown = 0;
		u32 from = cs.group != CHEATFILE_NONE ? cs.group : 0;
		u32 to = cs.group != CHEATFILE_NONE ? cs.group_end : s->file.count;
		for (u32 i = from; i < to; i++) {
			const struct cheatfile_cheat *c = &s->cheats[i];
			if (cs.only && !c->picked)
				continue;
			u32 end = i + 1, start = i;
			if (!cs.only && cs.group == CHEATFILE_NONE)
				start = cheatfile_group(&s->file, i, &end);
			if (start == i && end - start >= 2) {
				if (at >= start && at < end)
					row = cs.shown;
				s->rows[cs.shown++] = (u16)i;  // the run's row
				cs.folded = 1;
				i = end - 1;
				continue;
			}
			if (i == at)
				row = cs.shown;
			s->rows[cs.shown++] = (u16)i;
		}
		if (cs.shown || !cs.only)
			break;
		cs.only = 0;
	}
	// + Add code ends the file: a larger one's last window
	u32 last = !cheatstore_windows() || cheatstore_window() + 1 == cheatstore_windows();
	cs.count = cs.shown + (!cs.only && cs.group == CHEATFILE_NONE && last);
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

static void cs_title_sub(void);

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
	cs_title_sub();
	row = row < cs.count ? row : cs.count - 1;
	ui_list_set_cursor(cs.list, row);
	cs_moved(row);
	return 1;
}

static int cs_window(u32 k, u32 row);

// The title's after-text: the run's name while its choices show, "Picked"
// while only the picks do
static void cs_title_sub(void)
{
	ui_title_set_sub(cs.title, cs.group != CHEATFILE_NONE ? cs.group_sub : cs.only ? S(STR_CHEATS_ONLY_SUB) : NULL);
}

// A run of variants' choices in the view (the cursor on its pick, else its
// first), or all the cheats again on its row (start CHEATFILE_NONE)
static void cs_group(u32 start)
{
	u32 at = cs.group;
	if (start != CHEATFILE_NONE) {
		u32 end;
		start = cheatstore_run(start, &end);
		u32 pick = group_pick(start, end);
		at = pick != CHEATFILE_NONE ? pick : start;
	}
	cs.group = start;
	u32 row = cs_rows(at);
	tlog_debug("cheats: %s, %lu rows", cs.group != CHEATFILE_NONE ? "a run's choices" : "all",
		   (unsigned long)cs.count);
	ui_list_reset(cs.list, row);
	cs_title_sub();
	cs_moved(row);
}

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
	cs.group = CHEATFILE_NONE;
	cs.only = (u8)only;
	u32 row = cs_rows(at);
	if (at == CHEATFILE_NONE && !cs.only)
		row = cs.count - 1;  // from + Add code: back on it
	tlog_debug("cheats: %s, %lu rows", cs.only ? "picked only" : "all", (unsigned long)cs.count);
	ui_list_reset(cs.list, row);
	cs_title_sub();
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
		cheatstore_toggle_one(at);
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
		if (!keypad_edit_typable(f->text + c->code, c->code_len)) {
			dialog_message(cs.heading, S(STR_CHEATS_UNTYPABLE), DIALOG_INFO);
			return row;
		}
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
			cs.icon, was, NULL, RENAME_GAME_MAX, 0, name_submit, 0, S(STR_CHEATS_NAME_TITLE), 1, 0};
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

// The rename keyboard for cheat at's name, then the edit
// and the file written. The row to show after.
static u32 cs_rename(u32 at)
{
	u32 row = cs.act_row, len;
	if (at >= cheatstore()->file.count)
		return row;
	const struct cheatfile_cheat *c = &cheatstore()->cheats[at];
	const char *now = cheat_name(c, &len);
	snprintf(cs.name, sizeof(cs.name), "%.*s", (int)len, now);
	char was[sizeof(cs.name)];
	memcpy(was, cs.name, sizeof(was));
	const struct rename_ask name = {
		cs.icon, was, NULL, RENAME_GAME_MAX, 0, name_submit, 0, S(STR_CHEATS_RENAME_TITLE), 0, 0};
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
		cs_title_sub();
		cs_moved(row);
		return;
	}
	cheatstore_all_off();
	tlog("cheats: all turned off");
	if (cs.only)
		cs_set_only(0);
	cs_picks_changed();
}

// --- The guide's keyboard -------------------------------------------------------

// The bytes guide takes in the file (cheatfile_set_guide's): a line break
// as "\n"
static u32 guide_bytes(const char *guide)
{
	u32 n = 0;
	for (; *guide; guide++)
		n += *guide == '\n' ? 2 : 1;
	return n;
}

// OK on the guide's keyboard: the guide, if the file takes it whole
static const char *guide_submit(void *ctx, const char *text)
{
	(void)ctx;
	if (guide_bytes(text) > CHEATFILE_GUIDE_MAX) {
		SF(cs.reason, sizeof(cs.reason), STR_CHEATS_GUIDE_LONG, LN(CHEATFILE_GUIDE_MAX));
		return cs.reason;
	}
	snprintf(cs.name, sizeof(cs.name), "%s", text);
	return NULL;
}

// The keyboard, many lines, for cheat at's guide, then the edit and the file
// written (an empty guide removes it). The row to show after.
static u32 cs_edit_guide(u32 at)
{
	struct cheatstore *s = cheatstore();
	u32 row = cs.act_row;
	char was[sizeof(cs.name)];
	cheatfile_guide(&s->file, at, was, sizeof(was));
	const struct rename_ask ask = {
		cs.icon, was, NULL, CHEATFILE_GUIDE_MAX, 0, guide_submit, 0, S(STR_CHEATS_GUIDE_TITLE), 0, 1};
	if (!rename_run(&ask))
		return row;  // cancelled, or as it was
	u32 r = cheatstore_set_guide(at, cs.name);
	tlog("cheats: guide of cheat %lu %s: %s", (unsigned long)at, cs.name[0] ? "set" : "removed",
	     r == CHEATFILE_EDIT_OK ? "ok" : "refused");
	if (r != CHEATFILE_EDIT_OK) {
		dialog_message(cs.heading, S(STR_CHEATS_EDIT_FAILED + (r <= CHEATFILE_EDIT_FAILED ? r : 0)),
			       DIALOG_ERROR);
		return row;
	}
	cs_redecode();
	cs_write();
	return cs_rows(at);
}

// --- The Buttons page: how a cheat is activated, chosen ---------------------------
//
// Always on, Hold, Press or From the code, a tick on the one chosen. A on
// Hold or Press captures the buttons as Settings' hotkeys do (held a moment,
// then let go); B leaves, the choice written to the file if it changed. A
// cheat that only patches the ROM is set at launch whatever its line says:
// its rows are greyed, From the code ticked "At launch". One that patches
// the ROM and runs in game can't wait on buttons (CHEATFILE_ACT_UNGATED):
// Hold and Press are greyed, its line kept but not used. A combination
// that holds the in-game menu's keys or Sleep's is refused: they would fire
// instead (bp_taken).

// The row bp.keys ticks: BP_ROWS for none (a "launch" line on a cheat that
// runs in game)
static u32 bp_ticked(void)
{
	u32 mode = cheatkeys_mode(cs.bp.keys);
	if (cs.bp.locked || !cs.bp.keys || (cs.bp.ungated && (mode == CHEATKEYS_HOLD || mode == CHEATKEYS_PRESS)))
		return BP_CODE;
	switch (mode) {
	case CHEATKEYS_ALWAYS:
		return BP_ALWAYS;
	case CHEATKEYS_HOLD:
		return BP_HOLD;
	case CHEATKEYS_PRESS:
		return BP_PRESS;
	default:
		return BP_ROWS;
	}
}

static u32 bp_count(void *ctx)
{
	(void)ctx;
	return BP_ROWS;
}

static int bp_fetch(void *ctx, u32 i, struct ui_item *out)
{
	(void)ctx;
	if (i >= BP_ROWS)
		return UI_ITEM_PENDING;
	out->text = S(STR_CHEATS_BUTTONS_ALWAYS + i);
	return UI_ITEM_READY;
}

// A key chip for button b (enum cheatkeys_button) ending at right, centred
// in the row: accent while it is being captured; returns where it starts
static s32 bp_chip(const struct ui_paint *p, s32 right, u32 b, int accent)
{
	const struct ui_look *l = &ui_theme()->look;
	const struct torii_font *font = ui_theme()->body;
	const char *name = S(STR_BUTTON_SHORT_L + b);
	u32 tw = text_width(font, name, (u32)strlen(name)), w = tw + 2u * l->hint_chip_pad;
	w = w < BP_CHIP_MIN ? BP_CHIP_MIN : w;
	s32 x = right - (s32)w;
	if (x < 0)
		return right;
	u32 top = (p->rh - l->hint_chip_h) / 2;
	row_fill(p, (u32)x, top, w, l->hint_chip_h, l->hint_chip_radius, 1, 1, accent ? UI_ROLE_ACCENT : UI_ROLE_TRACK);
	u32 ty = p->y + top + (l->hint_chip_h > font->line_height ? (l->hint_chip_h - font->line_height) / 2 : 0);
	text_draw(p->win, font, p->x + (u32)x + (w - tw + 1) / 2, ty, tw + 1,
		  accent ? UI_ROLE_ACCENT_TEXT : UI_ROLE_DIM, name, (u32)strlen(name));
	return x;
}

// Text ending at right in ink, in the row's text line; returns where it starts
static s32 bp_text_right(const struct ui_paint *p, s32 right, u32 ink, const char *s)
{
	const struct torii_font *font = ui_theme()->body;
	u32 len = (u32)strlen(s), w = text_width(font, s, len);
	s32 x = right - (s32)w;
	if (x < BP_TEXT_X)
		x = BP_TEXT_X;
	u32 top = p->y + (p->rh > font->line_height ? (p->rh - font->line_height) / 2 : 0);
	text_draw(p->win, font, p->x + (u32)x, top, (u32)(right - x) + 1, ink, s, len);
	return x;
}

// The chips of keys' buttons, in screen order, ending at right
static s32 bp_chips(const struct ui_paint *p, s32 right, u16 keys, int accent)
{
	s32 b[CHEATKEYS_BUTTON_COUNT];
	u32 n = 0;
	for (s32 k = cheatkeys_first(keys); k >= 0; k = cheatkeys_next(keys, k))
		b[n++] = k;
	while (n--)
		right = bp_chip(p, right, (u32)b[n], accent) - BP_CHIP_GAP;
	return right + BP_CHIP_GAP;
}

static int bp_paint(void *ctx, const struct ui_paint *p)
{
	(void)ctx;
	const struct ui_look *l = &ui_theme()->look;
	const struct torii_font *font = ui_theme()->body;
	u32 i = p->index;
	if (i >= BP_ROWS)
		return 0;
	if (p->selected)
		row_fill(p, 0, (p->rh - l->pill_h) / 2, p->w, l->pill_h, l->pill_radius, 1, 1, UI_ROLE_TINT);
	u32 top = p->y + (p->rh > font->line_height ? (p->rh - font->line_height) / 2 : 0);
	int on = i == bp_ticked(),
	    dim = (cs.bp.locked && i != BP_CODE) || (cs.bp.ungated && (i == BP_HOLD || i == BP_PRESS));
	if (on)
		ui_icon8(p->win, p->x + BP_TICK_X, top + (font->line_height - 8) / 2, ART_ICON8_TICK,
			 dim ? UI_ROLE_DIM : UI_ROLE_ACCENT_INK);
	// The value at the right: the buttons, a capture's, or the code's own
	s32 right = (s32)p->w - CS_LIST_PAD, left = right;
	if (cs.bp.capturing && cs.bp.row == i) {
		left = cs.bp.cap.held ? bp_chips(p, right, CHEATKEYS(CHEATKEYS_HOLD, cs.bp.cap.held), 1)
				      : bp_text_right(p, right, UI_ROLE_ACCENT_INK, S(STR_CHEATS_BUTTONS_WAITING));
	} else if ((i == BP_HOLD || i == BP_PRESS) && on) {
		left = bp_chips(p, right, cs.bp.keys, 0);
	} else if (i == BP_CODE) {
		left = bp_text_right(p, right, p->selected ? UI_ROLE_DIM : UI_ROLE_FAINT, cs.bp.value);
	}
	const char *label = S(STR_CHEATS_BUTTONS_ALWAYS + i);
	s32 clip = left - 6 - BP_TEXT_X;
	text_draw(p->win, font, p->x + BP_TEXT_X, top, clip > 8 ? (u32)clip : 8, dim ? UI_ROLE_FAINT : UI_ROLE_TEXT,
		  label, (u32)strlen(label));
	return 1;
}

static int bp_event(struct ui_view *v, const struct ui_event *e)
{
	(void)v;
	switch (e->type) {
	case UI_EV_CURSOR:
		cs.bp.row = (u8)e->index;
		cs.close = 3;  // the hint bar's line (the loop)
		return 1;
	case UI_EV_ACTIVATE:
		if (!cs.bp.capturing)
			cs.close = 2;  // the loop acts on the row
		return 1;
	case UI_EV_KEYS:
		if (cs.bp.capturing)
			return 1;  // the capture takes every key
		if (e->keys & ui_input()->pressed & INPUT_B)
			cs.close = 1;
		return 0;
	default:
		return 0;
	}
}

static const struct ui_list_model bp_model = {bp_count, bp_fetch, 0, bp_paint, 0};
static const struct ui_list_config bp_list = {&bp_model, BP_ROW_H, UI_LIST_DRAWN_PILL, 0, CS_LIST_PAD};
static const struct ui_label_config bp_name = {"", UI_FONT_BODY, UI_ALIGN_LEFT, 0, ART_ICON8_KEYS + 1};

enum {
	BP_N_TITLE,
	BP_N_HINTS,
	BP_N_CARD,
	BP_N_NAME,
	BP_N_LIST,
	BP_NODES,
};
static const struct ui_spec bp_nodes[BP_NODES] = {
	[BP_N_TITLE] = {UI_TITLE, 1, UI_NONE, 3, UI_HFILL, 0, 0, 0, 0, 0, 0, CS_TITLE_H, &cs_title},
	[BP_N_HINTS] = {UI_HINTS, 2, UI_NONE, 3, UI_HFILL | UI_BOTTOM, 0, 0, 0, 0, 0, 0, CS_HINTS_H, &cs_no_hints},
	[BP_N_CARD] = {UI_CARD, 3, UI_NONE, 2, UI_HFILL, 0, 0, 0, 0, CS_CARD_Y, 0, CS_CARD_H, &cs_card},
	[BP_N_NAME] = {UI_LABEL, 4, UI_NONE, 0, UI_LEFT, 0, UI_ROLE_ACCENT_INK, 0, CS_LIST_X, BP_NAME_Y, CS_LIST_W,
		       BP_NAME_H, &bp_name},
	[BP_N_LIST] = {UI_LIST, 5, UI_NONE, 1, UI_LEFT, UI_FOCUSABLE, 0, 0, CS_LIST_X, BP_LIST_Y, CS_LIST_W + 8,
		       BP_LIST_H, &bp_list},
};
static const struct ui_view_spec bp_view = {"cheat-buttons", bp_nodes, BP_NODES, BP_N_LIST, 0, bp_event};

// The hint bar: the row's line, or the capture's with its countdown
static void bp_hints(void)
{
	if (cs.bp.capturing) {
		u32 left = settings_capture_left(&cs.bp.cap);
		struct ui_hint_note note = {UI_ROLE_DIM,
					    BP_BAR_W,
					    BP_BAR_H,
					    1,
					    BP_BAR_GAP,
					    (u8)((left * BP_BAR_W + cs.bp.cap.timeout - 1) / cs.bp.cap.timeout),
					    0};
		cs.bp.bar = note.fill;
		ui_hints_note(cs.bp.hints, S(STR_CHEATS_EXPLAIN_CAPTURE), &note);
		return;
	}
	if (cs.bp.refused) {
		static const struct ui_hint_note danger = {UI_ROLE_DANGER, 0, 0, 0, 0, 0, 0};
		ui_hints_note(cs.bp.hints, cs.bp.why, &danger);
		return;
	}
	u32 id = STR_CHEATS_EXPLAIN_ALWAYS + cs.bp.row;
	if (cs.bp.locked)
		id = STR_CHEATS_EXPLAIN_LOCKED;
	else if (cs.bp.ungated && (cs.bp.row == BP_HOLD || cs.bp.row == BP_PRESS))
		id = STR_CHEATS_EXPLAIN_UNGATED;
	ui_hints_text(cs.bp.hints, S(id));
}

// A on the cursor's row: Always and From the code at once, Hold and Press
// once their buttons are captured
static void bp_act(void)
{
	if (cs.bp.locked || (cs.bp.ungated && (cs.bp.row == BP_HOLD || cs.bp.row == BP_PRESS)))
		return;
	cs.bp.refused = 0;
	if (cs.bp.row == BP_ALWAYS || cs.bp.row == BP_CODE) {
		cs.bp.keys = cs.bp.row == BP_ALWAYS ? CHEATKEYS_ALWAYS_ON : 0;
		ui_list_refresh(cs.bp.list, 0, BP_ROWS - 1);
		return;
	}
	cs.bp.capturing = 1;
	ui_focus(cs.bp.name);  // the keys go to the capture, none to the list (L pages, the d-pad moves)
	settings_capture_start(&cs.bp.cap, ui_input()->held, BP_HOLD_FRAMES, BP_WAIT);
	cs.bp.cap.b_ok = 1;  // Hold B is a cheat's to have; a tap of B cancels
	ui_list_refresh(cs.bp.list, cs.bp.row, cs.bp.row);
	bp_hints();
}

// The reason buttons (KEYINPUT bits) are refused, into cs.bp.why: they
// hold the in-game menu's keys or Sleep's (when it is on), which would fire
// instead. 0 if they are free.
static int bp_taken(u16 buttons)
{
	u16 slots[assress_max], menu, sleep;
	for (u32 a = 0; a < assress_max; a++)
		slots[a] = Read_SET_info(a);
	settings_hotkeys(slots, &menu, &sleep);
	u32 id;
	u16 hot;
	if (slots[assress_v_reset] != SET_MENU_OFF && (buttons & menu) == menu) {
		id = STR_CHEATS_TAKEN_MENU;
		hot = menu;
	} else if (slots[assress_v_sleep] == 1 && (buttons & sleep) == sleep) {
		id = STR_CHEATS_TAKEN_SLEEP;
		hot = sleep;
	} else {
		return 0;
	}
	char names[CHEATKEYS_BUTTON_COUNT * 8];
	cheatkeys_names(CHEATKEYS(CHEATKEYS_HOLD, hot), names, sizeof(names));
	SF(cs.bp.why, sizeof(cs.bp.why), id, LS(names));
	return 1;
}

// Once a frame while a capture is under way
static void bp_capture(void)
{
	u16 shown = cs.bp.cap.held;
	u32 r = settings_capture_step(&cs.bp.cap, ui_input()->held, 1);
	if (r == SET_CAPTURE_WAIT) {
		if (cs.bp.cap.held != shown)
			ui_list_refresh(cs.bp.list, cs.bp.row, cs.bp.row);
		u32 left = settings_capture_left(&cs.bp.cap);
		if ((left * BP_BAR_W + cs.bp.cap.timeout - 1) / cs.bp.cap.timeout != cs.bp.bar)
			bp_hints();
		return;
	}
	if (r == SET_CAPTURE_DONE) {
		u32 mode = cs.bp.row == BP_HOLD ? CHEATKEYS_HOLD : CHEATKEYS_PRESS;
		if (bp_taken(cs.bp.cap.best)) {
			cs.bp.refused = BP_REFUSED_FRAMES;
			tlog_debug("cheats: buttons refused, %x: %s", (unsigned)cs.bp.cap.best, cs.bp.why);
		} else {
			cs.bp.keys = CHEATKEYS(mode, cs.bp.cap.best);
			tlog_debug("cheats: buttons captured, %s %x", mode == CHEATKEYS_HOLD ? "hold" : "press",
				   (unsigned)cs.bp.cap.best);
		}
	}
	cs.bp.capturing = 0;
	ui_focus(cs.bp.list);
	ui_list_refresh(cs.bp.list, 0, BP_ROWS - 1);
	bp_hints();
}

// The Buttons page for cheat at, then its line written if
// the choice changed. The row to show after.
static u32 cs_buttons(u32 at)
{
	u32 row = cs.act_row, len;
	if (at >= cheatstore()->file.count)
		return row;
	const struct cheatfile_cheat *c = &cheatstore()->cheats[at];
	cheatstore_decode_to(at);
	memset(&cs.bp, 0, sizeof(cs.bp));
	cs.bp.keys = cs.bp.was = c->keys;
	cs.bp.locked = cheatfile_derived(c) == CHEATKEYS_AT_LAUNCH;
	cs.bp.ungated = (u8)cheatfile_ungated(c);
	cs.bp.row = (u8)bp_ticked();
	if (cs.bp.row >= BP_ROWS)
		cs.bp.row = BP_CODE;
	if (cs.bp.locked)
		snprintf(cs.bp.value, sizeof(cs.bp.value), "%s", S(STR_CHEATS_BUTTONS_LAUNCH));
	else if (!cheatkeys_sentence(cheatfile_derived(c), cs.bp.value, sizeof(cs.bp.value)))
		snprintf(cs.bp.value, sizeof(cs.bp.value), "%s", S(STR_CHEATS_BUTTONS_UNKNOWN));
	const char *name = cheat_name(c, &len);
	snprintf(cs.pop_title, sizeof(cs.pop_title), "%.*s", (int)len, name);
	cs_leave();
	struct ui_view *v = ui_push(&bp_view, NULL);
	if (!v) {
		tlog_error("cheats: %s", ui_error());
		return row;
	}
	u8 title = (u8)(v->first + BP_N_TITLE);
	cs.bp.list = (u8)(v->first + BP_N_LIST);
	cs.bp.hints = (u8)(v->first + BP_N_HINTS);
	cs.bp.name = (u8)(v->first + BP_N_NAME);
	ui_title_set(title, cs.heading);
	ui_title_icon_set(title, cs.icon);
	ui_title_set_sub(title, S(STR_CHEATS_BUTTONS_SUB));
	ui_label_set(cs.bp.name, cs.pop_title);
	ui_list_set_cursor(cs.bp.list, cs.bp.row);
	bp_hints();
	tlog_debug("cheats: buttons of cheat %lu%s", (unsigned long)at, cs.bp.locked ? ", set at launch" : "");
	cs.close = 0;
	while (ui_depth() && cs.close != 1) {
		ui_frame();
		if (cs.bp.refused) {
			cs.bp.refused--;
			if (!cs.bp.refused)
				bp_hints();
		}
		if (cs.bp.capturing) {
			bp_capture();
		} else if (cs.close == 2) {
			cs.close = 0;
			bp_act();
		} else if (cs.close == 3) {
			cs.close = 0;
			cs.bp.refused = 0;
			bp_hints();
		}
	}
	ui_leave(UI_TR_DEEPER, -1);
	while (ui_depth())
		ui_pop();
	if (cs.bp.keys == cs.bp.was)
		return row;
	u32 r = cheatstore_set_keys(at, cs.bp.keys);
	char text[CHEATKEYS_TEXT_MAX];
	cheatkeys_format(cs.bp.keys, text, sizeof(text));
	tlog("cheats: buttons of cheat %lu: %s: %s", (unsigned long)at, cs.bp.keys ? text : "from the code",
	     r == CHEATFILE_EDIT_OK ? "ok" : "refused");
	if (r != CHEATFILE_EDIT_OK) {
		dialog_message(cs.heading, S(STR_CHEATS_EDIT_FAILED + (r <= CHEATFILE_EDIT_FAILED ? r : 0)),
			       DIALOG_ERROR);
		return row;
	}
	cs_redecode();
	cs_write();
	return cs_rows(at);
}

// START's menu on row: on a cheat, its Guide, its Rename (Edit code for a
// code of the user's), Buttons and Edit guide; then Turn all off and Show
// picked only; on a cheat, Delete last. The row to show after, or
// CHEATFILE_NONE to stay on the view.
static u32 cs_menu(u32 row)
{
	struct cheatstore *s = cheatstore();
	u32 start, end;
	const struct cheatfile_cheat *c = row_group(row, &start, &end) ? NULL : row_cheat(row);
	u32 at = c ? s->rows[row] : CHEATFILE_NONE, n = 0;
	if (c) {
		u32 len;
		const char *name = cheat_name(c, &len);
		snprintf(cs.pop_title, sizeof(cs.pop_title), "%.*s", (int)len, name);
		cs.item_ids[n] = MENU_GUIDE;
		cs.items[n++] = (struct ui_pop_item){S(STR_CHEATS_MENU_GUIDE), 0, 0};
		cs.item_ids[n] = c->own ? MENU_EDIT : MENU_RENAME;
		cs.items[n++] =
			(struct ui_pop_item){c->own ? S(STR_CHEATS_MENU_EDIT) : S(STR_CHEATS_MENU_RENAME), 0, 0};
		cs.item_ids[n] = MENU_BUTTONS;
		cs.items[n++] = (struct ui_pop_item){S(STR_CHEATS_MENU_BUTTONS), 0, 0};
		cs.item_ids[n] = MENU_EDIT_GUIDE;
		cs.items[n++] = (struct ui_pop_item){S(STR_CHEATS_MENU_EDIT_GUIDE), 0, 0};
	} else {
		snprintf(cs.pop_title, sizeof(cs.pop_title), "%s", cs.heading);
	}
	cs.item_ids[n] = MENU_ALL_OFF;
	cs.items[n++] = (struct ui_pop_item){S(STR_CHEATS_MENU_ALL_OFF), 0, (u8)(cs.picks ? 0 : UI_POP_DIM)};
	cs.item_ids[n] = MENU_ONLY;
	cs.items[n++] = (struct ui_pop_item){cs.only ? S(STR_CHEATS_MENU_ALL) : S(STR_CHEATS_MENU_ONLY), 0,
					     (u8)(cs.picks || cs.only ? 0 : UI_POP_DIM)};
	if (c) {
		cs.item_ids[n] = MENU_DELETE;
		cs.items[n++] = (struct ui_pop_item){S(STR_CHEATS_MENU_DELETE), 0, UI_POP_DANGER};
	}
	cs.popup = (struct ui_popup){cs.pop_title, 0, cs.items, (u8)n, 0, 0};
	if (!ui_popup_open(cs.list, cs.hints, &cs.popup))
		return CHEATFILE_NONE;
	tlog_debug("screen ready: cheat-menu");
	for (;;) {
		s32 pick = ui_popup_choose();
		if (pick < 0 || (u32)pick >= n)
			return CHEATFILE_NONE;  // B: it has closed
		u32 id = cs.item_ids[pick];
		if (!c && id != MENU_ALL_OFF && id != MENU_ONLY)
			return CHEATFILE_NONE;  // a cheat's item: never listed without one
		switch (id) {
		case MENU_GUIDE:
			ui_popup_close();
			cs_guide(at);
			return row;
		case MENU_EDIT:
			ui_popup_close();
			cs_leave();
			return cs_code(at, 0);
		case MENU_RENAME:
			ui_popup_close();
			cs_leave();
			return cs_rename(at);
		case MENU_BUTTONS:
			ui_popup_close();
			return cs_buttons(at);
		case MENU_EDIT_GUIDE:
			ui_popup_close();
			cs_leave();
			return cs_edit_guide(at);
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
	cs.group = CHEATFILE_NONE;
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
			    cs.group == CHEATFILE_NONE && (ui_input()->fire & (INPUT_UP | INPUT_DOWN))) {
				cs.back = (ui_input()->fire & INPUT_UP) != 0;
				cs.act = ACT_WINDOW;
			}
			if (cs.b_down && (ui_input()->released & INPUT_B)) {
				cs.b_down = 0;
				if (cs.group != CHEATFILE_NONE)
					cs_group(CHEATFILE_NONE);  // B leaves a run's choices first
				else if (cs.only)
					cs_set_only(0);  // B shows all again first
				else
					cs.act = ACT_BACK;
			}
		}
		if (cs.act == ACT_WINDOW) {
			cs_window_step(cs.back);
			continue;
		}
		if (cs.act == ACT_GROUP) {
			cs_group(s->rows[cs.act_row]);
			continue;
		}
		if (cs.act == ACT_GUIDE) {
			cs_guide(s->rows[cs.act_row]);
			up = cs_show(cs.act_row);
			continue;
		}
		if (cs.act == ACT_START) {
			u32 to = cs_menu(cs.act_row);
			if (to != CHEATFILE_NONE)
				up = cs_show(to);  // after an edit, a delete or a page; else the view is still up
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
