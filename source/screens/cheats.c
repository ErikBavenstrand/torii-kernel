// The cheat screen (cheats.h), in the game lists' look: the title bar with
// where the list is (the heading the cursor is under, the game's name faint
// after it), which row the cursor is on and how many cheats are on, the
// cheats on a card, and the hint bar, which says why a greyed row can't be
// picked beside its keys.
//
// One flat list of the whole file, whatever its size (v0.10): the cheats
// under no heading first, then each heading (cheatN_folder) in the order the
// file first names it, a row of its own the cursor passes by, its cheats
// under it; a run of variants (the same cheat with other values) is one row
// that opens its choices. L and R (and LEFT and RIGHT) go to the heading
// before and after. The rows come from the file's list (cheatlist.h), built
// once and kept on the card, so the screen never waits on decoding and
// nothing moves once it shows; the file itself, its cheats' model and the
// decoder's work stay in pReadCache (READCACHE_CHEATS) and its text in PSRAM
// (cheatstore.h). A on a cheat of a larger file's window not read now reads
// that window first (read ahead as the cursor rests). An edit (a code
// added, filled in, edited or deleted) changes the text in place, decodes
// its window again, and writes the file and the list at once.
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
	ACT_PICK,   // A on a cheat: picked or dropped, or a placeholder's digits filled in
	ACT_GUIDE,  // SELECT on a cheat: its Guide page
	ACT_START,  // START: the menu
	ACT_GROUP,  // A on a run of variants' row: its choices
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

// What a row shows (cheatlist_row's value): a cheat (its entry), a run of
// variants folded into one row (its first entry), a heading (its id + 1),
// or + Add code
enum {
	ROW_CHEAT,
	ROW_RUN,
	ROW_HEADING,
	ROW_ADD,
};
#define ROW(kind, at) ((u32)(kind) << 30 | (u32)(at))
#define ROW_KIND(v) ((u32)(v) >> 30)
#define ROW_AT(v) ((u32)(v) & 0x3FFFFFFFu)

static struct {
	u32 count;   // rows: headings, cheats and runs, then "+ Add code" in the list of all
	u32 sel;     // of them, the ones the cursor stops on (all but headings)
	u32 cursor;  // the list's, as last seen
	u32 picks;   // the picked cheats
	u8 list, title, hints;
	u8 b_down;   // B went down on this screen: its release leaves
	u8 act;      // ACT_*, and its row and entry
	u8 only;     // only the picked cheats show
	u8 add_why;  // cheatstore_can_add's: + Add code greyed unless CHEATFILE_EDIT_OK
	u8 picked;   // a cheat was picked on this visit: the boot mode is checked on leaving
	u8 measure;  // frames left to measure as "cheats-toggle"
	u8 close;    // B on the Guide page or the Buttons page
	u8 progressing;  // cs_progress's dialog is up
	u8 place;    // the heading (id + 1) the cursor is under, as the title names it; 0 none
	u8 n_heads;  // heading rows
	u8 has_add;  // the last row is "+ Add code"
	u32 cur_value;  // the cursor's row's value (cheatlist_row), as cs_moved last read it
	u16 head_rows[CHEATLIST_HEADINGS];  // each heading row's place, in order
	u8 head_ids[CHEATLIST_HEADINGS];    // and its heading (id + 1)
	// Each heading's (id + 1; 0: none) cheats and picks
	u16 sec_cheats[CHEATLIST_HEADINGS + 1], sec_on[CHEATLIST_HEADINGS + 1];
	// The run of variants whose choices show (its first entry), and past its
	// last; CHEATFILE_NONE: the list. own: the bytes their names share.
	u32 group, group_end, group_own;
	char group_sub[48];  // the title's text while it shows
	char place_name[CHEATFILE_FOLDER_MAX + 1];
	char detail[48];     // a run's or a heading's row's right
	char text[CHEATLIST_NAME_MAX + 1];  // a row's name, as fetched
	u16 icon;
	u32 act_row, act_g;
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

static u32 row_value(u32 row)
{
	return row < cs.count ? cheatlist_row(row) : ROW(ROW_ADD, 0);
}

// The entry row shows if it is a cheat's (not a run's), else CHEATFILE_NONE
static u32 row_entry(u32 row)
{
	u32 v = row_value(row);
	return ROW_KIND(v) == ROW_CHEAT ? ROW_AT(v) : CHEATFILE_NONE;
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

// An entry as the cheat model's: what the activation and the reasons need
static void as_cheat(const struct cheatlist_entry *e, struct cheatfile_cheat *c)
{
	memset(c, 0, sizeof(*c));
	c->status = e->status;
	c->keys = e->keys;
	c->act = e->act;
	c->bytes = e->bytes;
	c->cost = e->cost;
	c->patches = e->patches;
	c->picked = (e->flags & CHEATLIST_PICKED) != 0;
	c->number = e->number;
}

// Entry g's name into cs.text, its bytes in *len; "(no name)" without one
static const char *entry_name(const struct cheatlist_entry *e, u32 *len)
{
	if (!e->name_len) {
		*len = (u32)strlen(S(STR_CHEATS_UNNAMED));
		return S(STR_CHEATS_UNNAMED);
	}
	*len = cheatlist_name(e, cs.text, sizeof(cs.text));
	return cs.text;
}

// Where a name (len bytes) of entry e starts under its heading: past the
// heading's name it starts with ("Items: Master Ball"); 0 under none
static u32 heading_part(const struct cheatlist_entry *e, const char *name, u32 len)
{
	u32 hlen;
	const char *h = cheatlist_heading(e->heading, &hlen);
	return h ? cheatfile_folder_part(name, len, h, hlen) : 0;
}

// Why A wouldn't pick entry g (e): the model's reason for a cheat of the
// window read now, which knows the file's room too; else the list's
static u32 entry_why(u32 g, const struct cheatlist_entry *e)
{
	struct cheatstore *s = cheatstore();
	u32 start = cheatstore_window_start(cheatstore_window());
	if (g >= start && g < start + s->file.count)
		return cheatfile_why(&s->file, g - start);
	return cheatlist_why(e);
}

// The bytes run start to end's names share, up to the end of a word, as
// cheatfile_group_name finds them (0 if under 3), *own where each name's
// own part starts; kept in its first entry once found
static u32 run_name(u32 start, u32 end, u32 *own)
{
	struct cheatlist_entry first, e;
	cheatlist_get(start, &first);
	if (first.flags & CHEATLIST_NAMED) {
		*own = first.pad >> 8;
		return first.pad & 0xFF;
	}
	char a[CHEATLIST_NAME_MAX + 1];
	u32 n = cheatlist_name(&first, a, sizeof(a)), name = 0;
	*own = 0;
	int named = first.name_len != 0;
	for (u32 g = start + 1; g < end && n && named; g++) {
		cheatlist_get(g, &e);
		named = e.name_len != 0;
		u32 len = cheatlist_name(&e, cs.text, sizeof(cs.text)), k = 0;
		while (k < n && k < len && a[k] == cs.text[k])
			k++;
		n = k;
	}
	if (named) {
		// Back to a word's end: every name ends there, or goes on past a break
		int whole = 1;
		for (u32 g = start; g < end && whole; g++) {
			cheatlist_get(g, &e);
			u32 len = cheatlist_name(&e, cs.text, sizeof(cs.text));
			whole = len == n || cheatfile_name_break(cs.text[n]);
		}
		if (!whole)
			while (n && !cheatfile_name_break(a[n - 1]))
				n--;
		u32 part = n;
		while (n && (cheatfile_name_break(a[n - 1]) || a[n - 1] == ')' || a[n - 1] == ']'))
			n--;
		if (n >= 3) {
			name = n;
			*own = part;
		}
	}
	first.pad = (u16)(name | *own << 8);
	first.flags |= CHEATLIST_NAMED;
	cheatlist_put(start, &first);
	return name;
}

// The model's cheat c's name: its desc, or "(no name)"
static const char *cheat_name(const struct cheatfile_cheat *c, u32 *len)
{
	if (c->desc != CHEATFILE_NONE && c->desc_len) {
		*len = c->desc_len;
		return cheatstore()->file.text + c->desc;
	}
	*len = (u32)strlen(S(STR_CHEATS_UNNAMED));
	return S(STR_CHEATS_UNNAMED);
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
	u32 v = row_value(i), len;
	struct cheatlist_entry e;
	switch (ROW_KIND(v)) {
	case ROW_ADD:
		out->text = S(STR_CHEATS_ADD_ROW);
		out->ink = cs.add_why == CHEATFILE_EDIT_OK ? UI_ROLE_ACCENT_INK : UI_ROLE_DIM;
		return UI_ITEM_READY;
	case ROW_HEADING: {
		// A heading: its name in the accent, how many of its cheats are on
		// (else how many it has) at the right; the cursor passes it by
		u32 h = ROW_AT(v);
		out->text = cheatlist_heading(h, &len);
		out->len = (u16)len;
		out->ink = UI_ROLE_ACCENT_INK;
		out->detail = cs.detail;
		if (cs.sec_on[h])
			SF(cs.detail, sizeof(cs.detail), STR_CHEATS_PICKS, LN(cs.sec_on[h]));
		else
			SF(cs.detail, sizeof(cs.detail), STR_N(STR_CHEATS_FOLDER, cs.sec_cheats[h]),
			   LN(cs.sec_cheats[h]));
		return UI_ITEM_READY;
	}
	case ROW_RUN: {
		// A run: named by what its names share (else as its pick, or its
		// first), past its heading's name; its pick's own part, or how
		// many choices, at the right
		u32 start = ROW_AT(v), end = start + cheatlist_row_aux(i), own;
		u32 name = run_name(start, end, &own), pick = cheatlist_run_pick(start, end);
		cheatlist_get(pick != CHEATFILE_NONE && !name ? pick : start, &e);
		const char *t = entry_name(&e, &len);
		if (name)
			len = name < len ? name : len;
		u32 skip = heading_part(&e, t, len);
		out->text = t + skip;
		out->len = (u16)(len - skip);
		out->more = 1;
		out->detail = cs.detail;
		if (pick != CHEATFILE_NONE) {
			out->mark = ART_ICON8_TICK + 1;
			if (name) {
				// After the name's own copy: cs.text holds the row's name
				char p[CHEATLIST_NAME_MAX + 1];
				cheatlist_get(pick, &e);
				u32 n = cheatlist_name(&e, p, sizeof(p));
				u32 k = cheatfile_own_part(p, n, own);
				name_copy(cs.detail, sizeof(cs.detail), p + k, n - k);
				return UI_ITEM_READY;
			}
		}
		SF(cs.detail, sizeof(cs.detail), STR_N(STR_CHEATS_CHOICES, end - start), LN(end - start));
		return UI_ITEM_READY;
	}
	default:
		break;
	}
	u32 g = ROW_AT(v);
	cheatlist_get(g, &e);
	out->text = entry_name(&e, &len);
	u32 k = cs.group != CHEATFILE_NONE && cs.group_own ? cheatfile_own_part(out->text, len, cs.group_own)
							   : heading_part(&e, out->text, len);
	out->text += k;
	out->len = (u16)(len - k);
	if (entry_why(g, &e) != CHEATFILE_PICKABLE)
		out->ink = UI_ROLE_DIM;
	struct cheatfile_cheat c;
	as_cheat(&e, &c);
	if (c.picked) {
		out->mark = ART_ICON8_TICK + 1;
		out->mark_ink = c.status == CHEATFILE_OK ? 0 : UI_ROLE_DIM;
	}
	// A Hold or Press cheat's first button at the right, dim ("L+1")
	if (cheatkeys_tag(cheatfile_activation(&c), cs.tag, sizeof(cs.tag)))
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

// Why cheat c can't be picked, why being cheatfile_why's: the budget's
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

// The hint bar's line for cheat c, which A may pick: how it is activated if
// it takes buttons, else a ROM patch's note, else when it is set at launch;
// NULL for one always on
static const char *cs_activation_note(const struct cheatfile_cheat *c)
{
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
	const char *note = NULL;
	struct ui_hint a = {0, 0};
	if (cs.cursor >= cs.count)
		return;
	u32 v = cs.cur_value;
	int guide = 0, menu = 1;
	if (ROW_KIND(v) == ROW_RUN) {
		a = (struct ui_hint){"A", S(STR_HINT_OPEN)};
	} else if (ROW_KIND(v) == ROW_ADD) {
		if (cs.add_why == CHEATFILE_EDIT_OK)
			a = (struct ui_hint){"A", S(STR_CHEATS_HINT_ADD)};
		else
			note = add_reason();
	} else if (ROW_KIND(v) == ROW_CHEAT) {
		struct cheatlist_entry e;
		struct cheatfile_cheat c;
		cheatlist_get(ROW_AT(v), &e);
		as_cheat(&e, &c);
		u32 why = entry_why(ROW_AT(v), &e);
		if (why == CHEATFILE_PICKABLE)
			a = (struct ui_hint){"A", c.picked ? S(STR_CHEATS_HINT_DROP) : S(STR_CHEATS_HINT_PICK)};
		else if (why == CHEATFILE_WHY_EDIT)
			a = (struct ui_hint){"A", S(STR_CHEATS_HINT_FILL)};
		note = why != CHEATFILE_PICKABLE ? cs_reason(&c, why) : cs_activation_note(&c);
		guide = 1;
	}
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

// The heading rows above row: how many, and the last's heading (id + 1, 0 none)
static u32 heads_above(u32 row, u32 *h)
{
	u32 k = 0;
	*h = 0;
	while (k < cs.n_heads && cs.head_rows[k] < row)
		k++;
	if (k)
		*h = cs.head_ids[k - 1];
	return k;
}

// The title's right: the cursor's cheat or run of those listed (none on +
// Add code), and how many are on
static void cs_counter(void)
{
	u32 n = 0, h, all = cs.sel - cs.has_add;
	cs.counter[0] = 0;
	u32 kind = ROW_KIND(cs.cur_value);
	if (kind == ROW_CHEAT || kind == ROW_RUN)
		n = SF(cs.counter, sizeof(cs.counter), STR_CHEATS_POSITION,
		       LN(cs.cursor - heads_above(cs.cursor, &h) + 1), LN(all));
	if (cs.picks && n + 1 < sizeof(cs.counter)) {
		if (n)
			n += (u32)snprintf(cs.counter + n, sizeof(cs.counter) - n, " \xC2\xB7 ");
		if (n < sizeof(cs.counter))
			SF(cs.counter + n, sizeof(cs.counter) - n, STR_CHEATS_PICKS, LN(cs.picks));
	}
	ui_title_set_right(cs.title, cs.counter);
}

static void cs_retitle(void);

static void cs_moved(u32 to)
{
	u32 h;
	cs.cursor = to;
	cs.cur_value = row_value(to);
	heads_above(to, &h);
	if (ROW_KIND(cs.cur_value) == ROW_ADD)
		h = 0;  // + Add code ends the list, under no heading: the game's name
	if (cs.group == CHEATFILE_NONE && !cs.only && h != cs.place) {
		cs.place = (u8)h;
		cs_retitle();  // the heading the cursor is under names the place
	}
	cs_counter();
	cs_show_hints();
}

// Each heading's picks, as the picks are now
static void count_picks(void)
{
	struct cheatlist *l = &cheatstore()->list;
	struct cheatlist_entry *buf = (struct cheatlist_entry *)cheatstore()->part;
	const u32 per = sizeof(cheatstore()->part) / sizeof(*buf);
	memset(cs.sec_on, 0, sizeof(cs.sec_on));
	for (u32 g = 0; g < l->count; g += per) {
		u32 n = l->count - g < per ? l->count - g : per;
		cheatlist_read(g, n, buf);
		for (u32 i = 0; i < n; i++)
			cs.sec_on[buf[i].heading] += (buf[i].flags & CHEATLIST_PICKED) != 0;
	}
}

// After picks changed: the list's picks, every row again (greyed rows change
// as the budget left does), the count, + Add code's room, the hints
static void cs_picks_changed(void)
{
	cheatlist_sync_picks();
	count_picks();
	cs.picks = cheatstore_picks();
	cs.add_why = (u8)cheatstore_can_add(cs.new_name);
	ui_list_refresh(cs.list, 0, cs.count - 1);
	cs_moved(cs.cursor);
	frame_measure("cheats-toggle");
	cs.measure = CS_TOGGLE_FRAMES - 1;
}

// A pick or drop of entry g, one of a run of variants at a time: a pick
// drops the run's others first, and puts them back if g doesn't fit even
// so. 1 if its pick changed; 0 too if its window couldn't be read.
static int pick_one(u32 g)
{
	struct cheatstore *s = cheatstore();
	struct cheatlist_entry e;
	u32 end, start = cheatlist_run(g, &end), i;
	u32 dropped[8], n = 0;
	cheatlist_get(g, &e);
	if (!(e.flags & CHEATLIST_PICKED) && end - start >= 2) {
		for (u32 k = start; k < end && n < 8; k++) {
			if (k == g || !cheatstore_picked(k))
				continue;
			i = cheatstore_focus(k);
			if (i != CHEATFILE_NONE && cheatstore_toggle(i))
				dropped[n++] = k;
		}
	}
	i = cheatstore_focus(g);
	int ok = i != CHEATFILE_NONE && cheatstore_toggle(i);
	if (ok) {
		cs.picked |= s->cheats[i].picked;
		tlog_debug("cheats: %s cheat %u, %lu bytes picked", s->cheats[i].picked ? "picked" : "dropped",
			   s->cheats[i].number, (unsigned long)s->file.bytes + s->file.more_bytes);
	} else {
		for (u32 k = 0; k < n; k++) {
			i = cheatstore_focus(dropped[k]);
			if (i != CHEATFILE_NONE)
				cheatstore_toggle(i);
		}
	}
	return ok;
}

// The next selectable row from row (a heading's: past it), down or up, round the ends
static u32 past_heading(u32 row, int down)
{
	for (u32 k = 0; k < cs.count && ROW_KIND(row_value(row)) == ROW_HEADING; k++)
		row = down ? (row + 1) % cs.count : (row + cs.count - 1) % cs.count;
	return row;
}

// L or R (and LEFT or RIGHT): the next heading's first row, or the start
// of this one's (else the one before's); without headings, a page
static u32 section_jump(int down)
{
	u32 page = CS_LIST_H / ui_theme()->look.list_pitch, to;
	if (!cs.n_heads || cs.group != CHEATFILE_NONE || cs.only) {
		if (down)
			to = cs.cursor + page < cs.count ? cs.cursor + page : cs.count - 1;
		else
			to = cs.cursor > page ? cs.cursor - page : 0;
		return past_heading(to, down);
	}
	u32 h, k = heads_above(cs.cursor, &h);
	if (down) {
		if (k < cs.n_heads)
			return past_heading(cs.head_rows[k], 1);
		return cs.count - 1;  // past the last heading: the list's end
	}
	u32 first = k ? cs.head_rows[k - 1] + 1u : 0;
	if (cs.cursor > first && ROW_KIND(row_value(first)) != ROW_HEADING)
		return first;  // the start of this one
	if (k >= 2)
		return past_heading(cs.head_rows[k - 2], 1);
	return past_heading(0, 1);  // the list's start
}

static int cs_event(struct ui_view *v, const struct ui_event *e)
{
	(void)v;
	const InputState *in = ui_input();
	switch (e->type) {
	case UI_EV_CURSOR: {
		u32 to = e->index;
		if (ROW_KIND(row_value(to)) == ROW_HEADING) {
			// A heading: the cursor goes on past it, the way the key went
			u16 fire = in->fire;
			int down = (fire & INPUT_DOWN) ? 1 : (fire & INPUT_UP) ? 0 : to > cs.cursor;
			to = past_heading(to, down);
			ui_list_set_cursor(cs.list, to);
		}
		cs_moved(to);
		frame_measure("cheats-move");
		return 1;
	}
	case UI_EV_ACTIVATE: {
		if (e->index >= cs.count || cs.act)
			return 1;
		u32 val = row_value(e->index);
		cs.act_row = e->index;
		cs.act_g = ROW_AT(val);
		switch (ROW_KIND(val)) {
		case ROW_RUN:
			cs.act = ACT_GROUP;  // its choices
			return 1;
		case ROW_ADD:
			if (cs.add_why == CHEATFILE_EDIT_OK)
				cs.act = ACT_ADD;
			return 1;  // greyed: the hint bar says why
		case ROW_CHEAT:
			cs.act = ACT_PICK;  // its window read first, if it isn't
			return 1;
		default:
			return 1;
		}
	}
	case UI_EV_KEYS:
		if (e->keys & INPUT_B) {
			cs.b_down = 1;
			return 1;
		}
		if ((e->keys & in->pressed & INPUT_START) && !cs.act && !ui_list_paging(cs.list)) {
			cs.act = ACT_START;
			cs.act_row = cs.cursor;
			cs.act_g = row_entry(cs.cursor);
			return 1;
		}
		if ((e->keys & INPUT_SELECT) && (in->pressed & INPUT_SELECT) && !cs.act) {
			if (row_entry(cs.cursor) != CHEATFILE_NONE) {
				cs.act = ACT_GUIDE;
				cs.act_row = cs.cursor;
				cs.act_g = row_entry(cs.cursor);
			}
			return 1;
		}
		if (e->keys & (INPUT_L | INPUT_R | INPUT_LEFT | INPUT_RIGHT)) {
			if (cs.act || !cs.count)
				return 1;
			int down = (e->keys & (INPUT_R | INPUT_RIGHT)) != 0;
			u32 to = section_jump(down);
			if (to != cs.cursor) {
				ui_list_set_cursor(cs.list, to);  // springs there: a held key's glide would run on
				cs_moved(to);
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
					      UI_LIST_WRAP | UI_LIST_MARQUEE | UI_LIST_MARKS | UI_LIST_NO_SHOULDERS, 0,
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
static const struct ui_view_spec cs_view = {"cheats", cs_nodes, CS_NODES, CS_LIST, 0, cs_event};

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
	u32 code_len = cheatfile_code_len(c);
	for (u32 i = 0, from = 0; i <= code_len; i++)
		if (i == code_len || (code[i] == '+' && !cheat_plus_joins(code + from, code_len - from, i - from))) {
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
static void page_title(u8 title, const char *name);

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
	page_title(title, S(STR_CHEATS_GUIDE_SUB));
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

// A progress dialog for a long read or decode (the list built the first
// time, a window decoded after an edit), up once one is needed
// (cheatstore_progress_fn)
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

// Each frame the list rests: the window the cursor's cheat is in read ahead
// into its slot, a part a frame, so A on it is at once (never while the
// list moves or draws a page: the card's reads would take its rows' time)
static void cs_background(void)
{
	u32 g = row_entry(cs.cursor), n = cheatstore_windows(), k = 0;
	if (n < 2 || g == CHEATFILE_NONE || !ui_list_still(cs.list))
		return;
	while (k + 1 < n && g >= cheatstore_window_start(k + 1))
		k++;
	if (k != cheatstore_window())
		cheatstore_ahead(k, CS_AHEAD_BYTES);
}

// A run of n variants from entry start, whose row is row: its length kept
// beside the row and its name found, so the row's fetch is quick
static void run_done(u32 row, u32 start, u32 n)
{
	u32 own;
	cheatlist_row_aux_put(row, n);
	run_name(start, start + n, &own);
}

// The rows, from the list (cheatlist.h): the cheats under no heading first,
// then each heading's in the order the file first names them, each
// heading's row before its cheats, a run of variants folded into one row;
// only the picked ones (no runs folded) while cs.only, and all again if
// none is; only a run's choices while cs.group; then "+ Add code" (only in
// the list of all). Each heading's cheats and picks counted. The row entry
// at shows on (its run's), else CHEATFILE_NONE.
static u32 cs_rows(u32 at)
{
	struct cheatstore *s = cheatstore();
	struct cheatlist *l = &s->list;
	struct cheatlist_entry *buf = (struct cheatlist_entry *)s->part;
	const u32 per = sizeof(s->part) / sizeof(*buf);
	u32 row = CHEATFILE_NONE, own = 0, own_codes = 0;
	u8 order[CHEATLIST_HEADINGS + 1], n_order = 0, seen[CHEATLIST_HEADINGS + 1];
	u16 rows_in[CHEATLIST_HEADINGS + 1];
	if (cs.group != CHEATFILE_NONE) {
		// An edit may have left it no run
		u32 first = cs.group < l->count ? cs.group : l->count - 1;
		cs.group = l->count ? cheatlist_run(first, &cs.group_end) : CHEATFILE_NONE;
		if (cs.group != CHEATFILE_NONE && cs.group_end - cs.group < 2)
			cs.group = CHEATFILE_NONE;
	}
	for (;;) {
		memset(cs.sec_cheats, 0, sizeof(cs.sec_cheats));
		memset(cs.sec_on, 0, sizeof(cs.sec_on));
		memset(rows_in, 0, sizeof(rows_in));
		memset(seen, 0, sizeof(seen));
		n_order = 0;
		own_codes = 0;
		order[n_order++] = 0;  // no heading: first
		for (u32 g = 0; g < l->count; g += per) {
			u32 n = l->count - g < per ? l->count - g : per;
			cheatlist_read(g, n, buf);
			for (u32 i = 0; i < n; i++) {
				const struct cheatlist_entry *e = &buf[i];
				u32 h = e->heading, on = (e->flags & CHEATLIST_PICKED) != 0;
				cs.sec_cheats[h]++;
				cs.sec_on[h] += on;
				own_codes += (e->flags & CHEATLIST_OWN) != 0;
				rows_in[h] += cs.only ? on : !(e->flags & CHEATLIST_VARIANT);
				if (h && !seen[h]) {
					seen[h] = 1;
					order[n_order++] = (u8)h;
				}
			}
		}
		if (!cs.only || cheatstore_picks())
			break;
		cs.only = 0;  // none picked: all again
	}
	cs.n_heads = 0;
	u32 r = 0;
	if (cs.group != CHEATFILE_NONE) {
		// A run's choices
		for (u32 g = cs.group; g < cs.group_end; g++) {
			if (g == at)
				row = r;
			cheatlist_row_put(r++, ROW(ROW_CHEAT, g));
		}
		run_name(cs.group, cs.group_end, &own);
		cs.group_own = own;
	} else {
		// Where each heading's rows start, its row before them
		u16 base[CHEATLIST_HEADINGS + 1];
		for (u32 k = 0; k < n_order; k++) {
			u32 h = order[k];
			if (!rows_in[h])
				continue;
			if (h) {
				cs.head_ids[cs.n_heads] = (u8)h;
				cs.head_rows[cs.n_heads++] = (u16)r;
				cheatlist_row_put(r++, ROW(ROW_HEADING, h));
			}
			base[h] = (u16)r;
			r += rows_in[h];
		}
		// The rows in file order, each into its heading's place; a run's
		// first takes its row, its others fold into it (its length beside
		// the row, its name found now: a row's fetch stays quick)
		u32 run_row = CHEATFILE_NONE, run_start = 0, run_n = 0;
		for (u32 g = 0; g < l->count; g += per) {
			u32 n = l->count - g < per ? l->count - g : per;
			cheatlist_read(g, n, buf);
			for (u32 i = 0; i < n; i++) {
				const struct cheatlist_entry *e = &buf[i];
				u32 h = e->heading;
				if (cs.only) {
					if (!(e->flags & CHEATLIST_PICKED))
						continue;
					if (g + i == at)
						row = base[h];
					cheatlist_row_put(base[h]++, ROW(ROW_CHEAT, g + i));
					continue;
				}
				if (e->flags & CHEATLIST_VARIANT) {
					if (g + i == at)
						row = run_row;
					run_n++;
					continue;
				}
				if (run_n > 1)
					run_done(run_row, run_start, run_n);
				// A run starts here if the next entry is its variant
				struct cheatlist_entry next;
				int run = 0;
				if (i + 1 < n)
					run = (buf[i + 1].flags & CHEATLIST_VARIANT) != 0;
				else if (g + i + 1 < l->count) {
					cheatlist_get(g + i + 1, &next);
					run = (next.flags & CHEATLIST_VARIANT) != 0;
				}
				run_row = base[h];
				run_start = g + i;
				run_n = 1;
				if (g + i == at)
					row = run_row;
				cheatlist_row_put(base[h]++, ROW(run ? ROW_RUN : ROW_CHEAT, g + i));
			}
		}
		if (!cs.only && run_n > 1)
			run_done(run_row, run_start, run_n);
		if (!cs.only)
			cheatlist_row_put(r++, ROW(ROW_ADD, 0));
	}
	cs.has_add = cs.group == CHEATFILE_NONE && !cs.only;
	cs.count = r;
	cs.sel = r - cs.n_heads;
	cs.picks = cheatstore_picks();
	SF(cs.new_name, sizeof(cs.new_name), STR_CHEATS_NAME, LN(own_codes + 1));
	cs.add_why = (u8)cheatstore_can_add(cs.new_name);
	return row;
}

static u32 first_actable(u32 row);

// The rows again (cs_rows), and the row to show after an edit: entry at's,
// else the first A does something on (an empty file: + Add code)
static u32 rows_at(u32 at)
{
	u32 row = cs_rows(at);
	return row != CHEATFILE_NONE ? row : first_actable(0);
}

// The first row A does something on (a pick, a placeholder's digits, a
// run's choices) from row on, past greyed cheats such as the master codes a
// file starts with; row itself (past a heading) if none
static u32 first_actable(u32 row)
{
	for (u32 r = row; r < cs.count; r++) {
		u32 v = row_value(r);
		if (ROW_KIND(v) == ROW_RUN)
			return r;
		if (ROW_KIND(v) == ROW_CHEAT) {
			struct cheatlist_entry e;
			cheatlist_get(ROW_AT(v), &e);
			u32 why = entry_why(ROW_AT(v), &e);
			if (why == CHEATFILE_PICKABLE || why == CHEATFILE_WHY_EDIT)
				return r;
		}
	}
	return past_heading(row < cs.count ? row : 0, 1);
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
	row = row < cs.count ? row : cs.count - 1;
	row = past_heading(row, 1);
	if (cs.group == CHEATFILE_NONE) {
		u32 h;
		heads_above(row, &h);
		cs.place = (u8)h;  // the place before the title
	}
	cs_retitle();
	ui_list_set_cursor(cs.list, row);
	cs_moved(row);
	return 1;
}

// Where the list is, as the title names it: the run's name while its
// choices show, "Picked" while only the picks do, the heading the cursor is
// under, else the game's; and the place one up (faint after it, as Info's
// pages have), NULL at the top. A long game name would cut the after-text
// off, so the place, the part that changes, is the heading.
static const char *cs_place(const char **up)
{
	u32 len;
	const char *h = cheatlist_heading(cs.place, &len);
	if (h)
		name_copy(cs.place_name, sizeof(cs.place_name), h, len);
	*up = cs.heading;
	if (cs.group != CHEATFILE_NONE) {
		if (h && strcmp(cs.place_name, cs.group_sub) != 0)
			*up = cs.place_name;  // the heading, unless it is the run's name too
		return cs.group_sub;
	}
	if (cs.only)
		return S(STR_CHEATS_ONLY_SUB);
	if (h)
		return cs.place_name;
	*up = NULL;
	return cs.heading;
}

static void cs_retitle(void)
{
	const char *up, *place = cs_place(&up);
	ui_title_set(cs.title, place);
	ui_title_set_sub(cs.title, up);
	// Under a heading, the folder's icon; else the game's
	int inner = !cs.only && cs.place;
	if (!ui_title_icon_set(cs.title, inner ? ART_ICON16_FOLDER : cs.icon))
		tlog_error("cheats: no room for the title's icon");
}

// A page's title (the Guide, Buttons): its name, the list's place faint after it
static void page_title(u8 title, const char *name)
{
	const char *up;
	ui_title_set(title, name);
	ui_title_icon_set(title, cs.icon);
	ui_title_set_sub(title, cs_place(&up));
}

// The list again in the view, the cursor on entry at's row (its run's)
static void cs_relist(u32 at)
{
	u32 row = cs_rows(at);
	if (row == CHEATFILE_NONE)
		row = first_actable(0);
	ui_list_reset(cs.list, row);
	if (cs.group == CHEATFILE_NONE) {
		u32 h;
		heads_above(row, &h);
		cs.place = (u8)h;  // a run's choices keep its heading's
	}
	cs_retitle();
	cs_moved(row);
}

// A run of variants' choices in the view (the cursor on its pick, else its
// first), or the list again on its row (start CHEATFILE_NONE)
static void cs_group(u32 start)
{
	u32 at = cs.group;
	if (start != CHEATFILE_NONE) {
		u32 end, own;
		start = cheatlist_run(start, &end);
		u32 pick = cheatlist_run_pick(start, end);
		at = pick != CHEATFILE_NONE ? pick : start;
		// Its name for the title: what the names share, past the heading's
		struct cheatlist_entry e;
		u32 name = run_name(start, end, &own), len;
		cheatlist_get(start, &e);
		const char *t = entry_name(&e, &len);
		u32 k = name ? heading_part(&e, t, name) : 0;
		if (name)
			name_copy(cs.group_sub, sizeof(cs.group_sub), t + k, name - k);
		else
			snprintf(cs.group_sub, sizeof(cs.group_sub), "%s", S(STR_CHEATS_CHOICES_SUB));
	}
	cs.group = start;
	tlog_debug("cheats: %s", cs.group != CHEATFILE_NONE ? "a run's choices" : "all");
	cs_relist(at);
	tlog_debug("cheats: %lu rows", (unsigned long)cs.count);
}

// Only the picked cheats, or all again, in the view as it is: the cursor
// stays on its cheat if that still shows
static void cs_set_only(int only)
{
	u32 at = row_entry(cs.cursor);
	if (ROW_KIND(row_value(cs.cursor)) == ROW_RUN)
		at = ROW_AT(row_value(cs.cursor));
	cs.group = CHEATFILE_NONE;
	cs.only = (u8)only;
	u32 row = cs_rows(at);
	if (row == CHEATFILE_NONE)
		row = at == CHEATFILE_NONE && !cs.only ? cs.count - 1
						       : first_actable(0);  // from + Add code: back on it
	tlog_debug("cheats: %s, %lu rows", cs.only ? "picked only" : "all", (unsigned long)cs.count);
	ui_list_reset(cs.list, row);
	u32 h;
	heads_above(row, &h);
	cs.place = (u8)h;
	cs_retitle();
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

// Entry g's cheat in the model: its window read now (a read from the card
// if it isn't in its slot), its index there; CHEATFILE_NONE, said, if the
// window can't be read (the file changed on the card)
static u32 cs_focus(u32 g)
{
	u32 i = cheatstore_focus(g);
	if (i == CHEATFILE_NONE) {
		tlog_error("cheats: cheat %lu's part of the file couldn't be read", (unsigned long)g);
		dialog_message(cs.heading, S(STR_CHEATS_GONE), DIALOG_ERROR);
	}
	return i;
}

// The entry of the model's cheat i
static u32 cs_global(u32 i)
{
	return cheatstore_window_start(cheatstore_window()) + i;
}

// After an edit to the model (old cheats before it): decoded again whole,
// a long one behind a progress dialog, and the list follows
static void cs_redecode(u32 old)
{
	cheatstore_begin();
	while (!cheatstore_decode(16780000u / 20))
		cs_progress(cheatstore()->work.next, cheatstore()->file.count);
	cs_progress_end();
	cheatlist_from_model(cheatstore_window_start(cheatstore_window()), old);
}

// Picks entry g at once decoded, if it may be: a code just typed is for using
static void pick_new(u32 g)
{
	struct cheatlist_entry e;
	if (g >= cheatstore()->list.count)
		return;
	cheatlist_get(g, &e);
	if (!(e.flags & CHEATLIST_PICKED) && entry_why(g, &e) == CHEATFILE_PICKABLE && pick_one(g))
		cs.picked = 1;
}

// Writes the file after an edit, and the list's cache; a dialog if the card
// didn't take it
static void cs_write(void)
{
	if (!cheatstore_write())
		dialog_message(cs.heading, S(STR_CHEATS_SAVE_FAILED), DIALOG_ERROR);
	cheatlist_sync_picks();
	cheatlist_save();
}

// The keypad for a new code (g CHEATFILE_NONE), entry g's placeholder
// (fill) or a code of the user's, then the rename keyboard for its name
// (not a placeholder's; a new code's starts as "Code N", which OK keeps),
// then the edit into the file and the file written. The row to show after.
static u32 cs_code(u32 g, int fill)
{
	struct cheatstore *s = cheatstore();
	struct cheatfile *f = &s->file;
	u32 row = cs.act_row;
	// The cheat's window, or the last one: a new code ends the file
	u32 at = g != CHEATFILE_NONE ? cs_focus(g) : CHEATFILE_NONE;
	if (g != CHEATFILE_NONE && at == CHEATFILE_NONE)
		return row;
	if (g == CHEATFILE_NONE && s->list.count && cs_focus(s->list.count - 1) == CHEATFILE_NONE)
		return row;
	const struct cheatfile_cheat *c = at != CHEATFILE_NONE ? &s->cheats[at] : NULL;
	struct keypad_ask ask = {
		cs.icon, c ? S(STR_KEYPAD_TITLE_EDIT) : S(STR_KEYPAD_TITLE_ADD), NULL, 0, CHEAT_FMT_AUTO, NULL};
	struct cheat_decoder ctx;
	cheatstore_decoder_before(c ? at : f->count, &ctx);
	ask.ctx = &ctx;
	snprintf(cs.name, sizeof(cs.name), "%s", cs.new_name);
	if (c) {
		if (!keypad_edit_typable(f->text + c->code, cheatfile_code_len(c))) {
			dialog_message(cs.heading, S(STR_CHEATS_UNTYPABLE), DIALOG_INFO);
			return row;
		}
		if (cheatfile_code_len(c) > KEYPAD_CODE_MAX) {
			dialog_message(cs.heading, S(STR_CHEATS_TOO_LONG), DIALOG_INFO);
			return row;
		}
		memcpy(cs.code, f->text + c->code, cheatfile_code_len(c));
		cs.code[cheatfile_code_len(c)] = 0;
		ask.code = cs.code;
		ask.code_len = cheatfile_code_len(c);
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
			cs.icon, was, NULL, RENAME_GAME_MAX, 0, name_submit, 0, S(STR_CHEATS_NAME_TITLE), 1, 0, 0};
		if (rename_run(&name))
			break;
	}
	u32 r, idx = at, old = f->count;
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
	tlog("cheats: %s cheat %lu: ok", how, (unsigned long)cs_global(idx));
	cs_redecode(old);
	pick_new(cs_global(idx));
	cs_write();
	return rows_at(cs_global(idx));
}

// The rename keyboard for entry g's name, then the edit and the file
// written. The row to show after.
static u32 cs_rename(u32 g)
{
	u32 row = cs.act_row, at = cs_focus(g);
	if (at == CHEATFILE_NONE)
		return row;
	const struct cheatfile_cheat *c = &cheatstore()->cheats[at];
	const struct cheatfile *f = &cheatstore()->file;
	cs.name[0] = 0;
	if (c->desc != CHEATFILE_NONE)
		snprintf(cs.name, sizeof(cs.name), "%.*s", (int)c->desc_len, f->text + c->desc);
	char was[sizeof(cs.name)];
	memcpy(was, cs.name, sizeof(was));
	const struct rename_ask name = {
		cs.icon, was, NULL, RENAME_GAME_MAX, 0, name_submit, 0, S(STR_CHEATS_RENAME_TITLE), 0, 0, 0};
	if (!rename_run(&name))
		return row;  // cancelled, or the name as it was
	u32 old = f->count, r = cheatstore_rename(at, cs.name);
	tlog("cheats: renamed cheat %lu: %s", (unsigned long)g, r == CHEATFILE_EDIT_OK ? "ok" : "refused");
	if (r != CHEATFILE_EDIT_OK) {
		dialog_message(cs.heading, S(STR_CHEATS_EDIT_FAILED + (r <= CHEATFILE_EDIT_FAILED ? r : 0)),
			       DIALOG_ERROR);
		return row;
	}
	cs_redecode(old);
	cs_write();
	return rows_at(g);
}

// Deletes entry g (asked first by the caller), and the file with it if
// nothing worth keeping is left. The row to show after.
static u32 cs_delete(u32 g)
{
	struct cheatstore *s = cheatstore();
	u32 at = cs_focus(g);
	if (at == CHEATFILE_NONE)
		return cs.act_row;
	u32 old = s->file.count, r = cheatstore_remove(at);
	tlog("cheats: deleted cheat %lu: %s", (unsigned long)g, r == CHEATFILE_EDIT_OK ? "ok" : "refused");
	cs_leave();
	cs_redecode(old);
	if (r != CHEATFILE_EDIT_OK)
		dialog_message(cs.heading, S(STR_CHEATS_EDIT_FAILED + (r <= CHEATFILE_EDIT_FAILED ? r : 0)),
			       DIALOG_ERROR);
	else if (cheatstore_windows() || !cheatfile_empty(&s->file) || !cheatstore_drop())
		cs_write();
	// The cheat after it now has its place (or the one before, at the end)
	u32 to = g < s->list.count ? g : s->list.count ? s->list.count - 1 : CHEATFILE_NONE;
	return rows_at(to);
}

// Turns every picked cheat off, in every window
static void cs_all_off(void)
{
	cheatstore_all_off();
	tlog("cheats: all turned off");
	if (cs.only) {
		cheatlist_sync_picks();
		cs_set_only(0);
		return;
	}
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
		cs.icon, was, NULL, CHEATFILE_GUIDE_MAX, 0, guide_submit, 0, S(STR_CHEATS_GUIDE_TITLE), 0, 1, 0};
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
	u32 g = cs_global(at);
	cs_redecode(cheatstore()->file.count);
	cs_write();
	return rows_at(g);
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
static const struct ui_label_config bp_name = {"", UI_FONT_BODY, UI_ALIGN_LEFT, UI_LABEL_MARQUEE, ART_ICON8_KEYS + 1};

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
	page_title(title, S(STR_CHEATS_BUTTONS_SUB));
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
	u32 g = cs_global(at);
	cs_redecode(cheatstore()->file.count);
	cs_write();
	return rows_at(g);
}

// START's menu on row: on a cheat, its Guide, its Rename (Edit code for a
// code of the user's), Buttons and Edit guide; then Turn all off and Show
// picked only; on a cheat, Delete last. The row to show after, or
// CHEATFILE_NONE to stay on the view.
static u32 cs_menu(u32 row)
{
	struct cheatstore *s = cheatstore();
	u32 g = row_entry(row), at = g != CHEATFILE_NONE ? cheatstore_focus(g) : CHEATFILE_NONE, n = 0;
	const struct cheatfile_cheat *c = at != CHEATFILE_NONE ? &s->cheats[at] : NULL;
	if (c) {
		struct cheatlist_entry e;
		u32 len;
		cheatlist_get(g, &e);
		const char *name = entry_name(&e, &len);
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
			return cs_code(g, 0);
		case MENU_RENAME:
			ui_popup_close();
			cs_leave();
			return cs_rename(g);
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
			return cs_delete(g);
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

// A on cheat entry g: picks or drops it (its window read first, if it
// isn't), or the keypad for a placeholder's digits. The row to show after,
// or CHEATFILE_NONE to stay on the view.
static u32 cs_pick(u32 g)
{
	struct cheatstore *s = cheatstore();
	u32 at = cs_focus(g);
	if (at == CHEATFILE_NONE)
		return cs.act_row;
	if (!s->cheats[at].picked && cheatfile_why(&s->file, at) == CHEATFILE_WHY_EDIT) {
		cs_leave();
		return cs_code(g, 1);  // a placeholder: the keypad fills it in
	}
	if (!pick_one(g))
		return CHEATFILE_NONE;  // greyed: the hint bar says why
	cs_picks_changed();
	if (cs.group != CHEATFILE_NONE && cheatstore_picked(g)) {
		u32 start = cs.group;
		cs.group = CHEATFILE_NONE;  // one of a run's: chosen, as a menu's item is
		cs_relist(start);
	}
	return CHEATFILE_NONE;
}

void cheats_run(const char *rom_path, const char *title, u16 icon)
{
	memset(&cs, 0, sizeof(cs));
	cs.group = CHEATFILE_NONE;
	snprintf(cs.heading, sizeof(cs.heading), "%s", title);
	cs.icon = icon;
	cheatstore_progress(cs_progress);  // a larger file read whole, and the list built, the first time
	int opened = cheatstore_open(rom_path);
	int listed = (opened == CHEATSTORE_READ || opened == CHEATSTORE_NEW) && cheatlist_open();
	cs_progress_end();
	if (!listed) {
		// Why the file is left alone: never a screen without it
		const char *why = opened == CHEATSTORE_BIG          ? S(STR_CHEATS_TOO_BIG)
				  : opened == CHEATSTORE_OTHER_DUMP ? S(STR_CHEATS_OTHER_DUMP)
								    : S(STR_CHEATS_GONE);
		cheatstore_progress(NULL);
		tlog("cheats: %s: no cheat screen (%d)", rom_path, opened);
		dialog_message(title, why, DIALOG_ERROR);
		return;
	}
	cheatstore_listed();  // decoded: the game popup's count from now on
	struct cheatstore *s = cheatstore();
	cs_rows(CHEATFILE_NONE);
	// The cursor starts on the first row A does something on
	u32 row = first_actable(0), greyed = 0;
	for (u32 g = 0; g < s->list.count; g++) {
		struct cheatlist_entry e;
		cheatlist_get(g, &e);
		greyed += entry_why(g, &e) != CHEATFILE_PICKABLE;
	}
	tlog("cheats: %s: %lu cheats, %lu greyed, %lu picked (%lu bytes), %lu rows%s", s->path,
	     (unsigned long)s->list.count, (unsigned long)greyed, (unsigned long)cs.picks,
	     (unsigned long)s->file.bytes + s->file.more_bytes, (unsigned long)cs.count,
	     opened == CHEATSTORE_NEW ? ", no file yet" : "");
	if (cs.add_why != CHEATFILE_EDIT_OK)
		tlog("cheats: + Add code greyed: %s", add_reason());
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
		if (cs.act == ACT_GROUP) {
			cs_group(cs.act_g);
			continue;
		}
		if (cs.act == ACT_PICK) {
			u32 to = cs_pick(cs.act_g);
			if (to != CHEATFILE_NONE)
				up = cs_show(to);  // after the keypad
			continue;
		}
		if (cs.act == ACT_GUIDE) {
			u32 at = cs_focus(cs.act_g);
			if (at != CHEATFILE_NONE) {
				cs_guide(at);
				up = cs_show(cs.act_row);
			}
			continue;
		}
		if (cs.act == ACT_START) {
			u32 to = cs_menu(cs.act_row);
			if (to != CHEATFILE_NONE)
				up = cs_show(to);  // after an edit, a delete or a page; else the view is still up
			continue;
		}
		if (cs.act != ACT_ADD)
			break;
		cs_leave();
		row = cs_code(CHEATFILE_NONE, 0);
		up = cs_show(row);
	}
	int changed = cheatfile_changed(&s->file) || s->picks_changed;
	u32 picks = cheatstore_picks();
	tlog("cheats: back, %lu picked%s", (unsigned long)picks, changed ? ", changed" : "");
	int saved = cheatstore_write();
	cheatlist_save();
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
