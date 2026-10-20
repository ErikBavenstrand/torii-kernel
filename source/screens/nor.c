// The NOR page (nor.h).
//
// The layout is built when the page opens, into RAM: the games card is as
// tall as the rows it shows (one to four), and the Format all card follows
// it. Planes: the games list alone on plane 1; Format all's texts and the
// scroll bar, transparent, on plane 2, which shares its tiles with the
// popup and the confirmation (plane 0), so little goes there; the title bar,
// the cards, the storage line and bar, the empty NOR's note and the hint bar
// on plane 3, the games card under the title bar's rectangle. Format all's card is
// drawn twice, on the card's paper and lifted in the item's tint (chosen):
// both stay shown, and the one drawn last is the one that shows, so
// choosing it or leaving it draws one card and nothing else.
//
// The cursor is the page's, not the list's: rows 0 to count - 1 are the
// games, row count is Format all. The list takes no keys (no focus); the
// page moves its cursor, and hides its pill while Format all is chosen.
#include <stdio.h>
#include <string.h>

#include "nor.h"
#include "../actions.h"
#include "../ezkernel.h"
#include "../gamename.h"
#include "../lang.h"
#include "../tlog.h"
#include "../engine/frame.h"
#include "../ui/art_ids.h"
#include "../ui/dialog.h"
#include "../ui/ui.h"

#define NOR_TOTAL 0x4000000u  // the game NOR's 64 MB
#define MB 0x100000u
#define KB 0x400u

// --- Look, px (spec.md's grid: title bar 0-18, content 20-143, hints at 147) ---
#define NOR_DETAIL_W 48  // a row's size ("16 MB"): the list's config needs constants
#define NOR_LIST_PAD 4   // a row's text inset
static const struct nor_look {
	u8 title_h;       // the title bar's rectangle: whole tiles under its line
	u8 hints_h;
	u8 card_y;        // the games card's rectangle, under the title bar's (both on plane 3); its
			  // body starts card_inset.inset_t in
	u8 line_x, line_w;  // the storage line (used at the left, free at the right) and its bar
	u8 line_y, line_h;
	u8 bar_y, bar_h;  // the bar is the theme's storage_h tall, in the middle of this
	u8 list_x, list_y, list_w;
	u8 list_rows;     // rows the games card shows at most: 4 (56 px, whole tiles and rows)
	u8 card_pad;      // px of card under the last row, at least
	u8 note_y, note_h;  // "No games on NOR", in the first row's place
	u8 format_h;      // the Format all card's rectangle, under the games card's
	u8 format_text_w;  // its text, from line_x; its detail, right-aligned from format_detail_x
	u8 format_detail_x;  // (narrow: plane 2's tiles are the popup's and the confirmation's too)
	struct ui_card_config card_inset;  // the games card (its bottom inset is worked out)
	struct ui_card_config format_card;
	struct ui_card_config format_lift;  // chosen: 1 px larger all round, in the tint
} look = {
	.title_h = 24,
	.hints_h = 16,
	.card_y = 24,
	.line_x = 8,
	.line_w = 224,
	.line_y = 24,
	.line_h = 16,
	.bar_y = 40,
	.bar_h = 8,
	.list_x = 8,
	.list_y = 56,
	.list_w = 216,
	.list_rows = 4,
	.card_pad = 3,
	.note_y = 56,
	.note_h = 16,
	.format_h = 24,
	.format_text_w = 80,
	.format_detail_x = 120,
	.card_inset = {4, 0, 4, 4, 0},
	.format_card = {4, 2, 4, 4, 0},
	.format_lift = {3, 1, 3, 3, 0},
};

enum {
	N_TITLE,
	N_HINTS,
	N_CARD,
	N_USED,
	N_FREE,
	N_BAR,
	N_NOTE,
	N_SCROLL,
	N_FORMAT_1,  // Format all's two cards, the one shown first last
	N_FORMAT_2,
	N_FORMAT_TEXT,
	N_FORMAT_DETAIL,
	N_LIST,  // last: its rows draw within the first frame's budget, after the rest
	N_NODES
};

#define NAME_SLOTS 4  // rows' names cleaned lately (a row draws straight after its fetch)
#define NAME_NONE 0xFF

static struct {
	u32 count;  // games
	u32 sel;    // the page's cursor: a game, or count for Format all
	u32 next_row;
	u8 next_row_set;  // the row to open on after a delete
	u8 first;   // the view's first node
	u8 lift;    // node of the chosen Format all card; panel: its plain one
	u8 panel;
	u32 parts[MAX_NOR];
	struct ui_card_config card;
	char used[24], free[32], position[12];
	struct ui_hint hints_game[3], hints_format[2];
	// Rows' names and sizes, by game
	u8 name_of[NAME_SLOTS];
	u8 name_next;
	char name[NAME_SLOTS][sizeof(pNorFS[0].filename)];
	char size[NAME_SLOTS][12];
	// The popup and its questions
	struct ui_pop_item items[3];
	struct ui_popup menu;
	char pop_title[sizeof(pNorFS[0].filename)];
	char title[sizeof(pNorFS[0].filename) + 16];
	char text[sizeof(pNorFS[0].filename) + 64];
} nor EWRAM_BSS;

static struct ui_spec nodes[N_NODES] EWRAM_BSS;

// --- Words ---------------------------------------------------------------------

// "16 MB" (to the nearest MB: a game's area holds its completion mark too),
// or "128 KB" under 1 MB
static void size_text(char *out, u32 size, u32 bytes)
{
	if (bytes >= MB)
		snprintf(out, size, "%lu %s", (unsigned long)((bytes + MB / 2) / MB), gl_size_mb);
	else
		snprintf(out, size, "%lu %s", (unsigned long)((bytes + KB / 2) / KB), gl_size_kb);
}

// The storage line: used rounded up and free rounded down, so they add up
// to 64 MB and free never promises room that isn't there
static void storage_text(u32 used)
{
	u32 room = used < NOR_TOTAL ? NOR_TOTAL - used : 0;
	char n[16];
	if (used >= MB)
		snprintf(n, sizeof(n), "%lu %s", (unsigned long)((NOR_TOTAL - room / MB * MB) / MB), gl_size_mb);
	else if (used)
		snprintf(n, sizeof(n), "%lu %s", (unsigned long)((used + KB - 1) / KB), gl_size_kb);
	else
		snprintf(n, sizeof(n), "0 %s", gl_size_mb);
	snprintf(nor.used, sizeof(nor.used), "%s %s", n, gl_nor_used);
	if (room >= MB || !room)
		snprintf(n, sizeof(n), "%lu %s", (unsigned long)(room / MB), gl_size_mb);
	else
		snprintf(n, sizeof(n), "%lu %s", (unsigned long)(room / KB), gl_size_kb);
	snprintf(nor.free, sizeof(nor.free), "%s %s %lu", n, gl_nor_free_of, (unsigned long)(NOR_TOTAL / MB));
}

// Game i's name as the lists show it (gamename.h: the cleaned file name),
// and its size; kept for the last NAME_SLOTS games asked for
static u32 name_slot(u32 i)
{
	for (u32 s = 0; s < NAME_SLOTS; s++)
		if (nor.name_of[s] == i)
			return s;
	u32 s = nor.name_next;
	nor.name_next = (u8)((s + 1) % NAME_SLOTS);
	char clean[GAMENAME_MAX + 1];
	// Never longer than the file name, which the list keeps to 99 bytes
	u32 len = gamename_clean(clean, pNorFS[i].filename);
	if (!len)  // a name that cleans to nothing ("   .gba"): the file name
		len = (u32)snprintf(clean, sizeof(clean), "%s", pNorFS[i].filename);
	len = len < sizeof(nor.name[s]) - 1 ? len : sizeof(nor.name[s]) - 1;
	memcpy(nor.name[s], clean, len);
	nor.name[s][len] = 0;
	size_text(nor.size[s], sizeof(nor.size[s]), pNorFS[i].filesize);
	nor.name_of[s] = (u8)i;
	return s;
}

static const char *save_type_name(u8 savemode)
{
	switch (savemode) {
	case 0x00:
		return gl_nor_saves[NOR_SAVE_NONE];
	case 0x21:
		return gl_nor_saves[NOR_SAVE_EEPROM_512];
	case 0x22:
	case 0x23:
		return gl_nor_saves[NOR_SAVE_EEPROM_8K];
	case 0x31:
		return gl_nor_saves[NOR_SAVE_FLASH_128K];
	case 0x32:
	case 0x33:
		return gl_nor_saves[NOR_SAVE_FLASH_64K];
	default:  // 0x11, and homebrew's 0x10 and unknown types, which get 64 KB of SRAM
		return gl_nor_saves[NOR_SAVE_SRAM];
	}
}

// --- The list's rows --------------------------------------------------------------

static u32 model_count(void *ctx)
{
	(void)ctx;
	return nor.count;
}

static int model_fetch(void *ctx, u32 i, struct ui_item *out)
{
	(void)ctx;
	if (i >= nor.count)
		return UI_ITEM_PENDING;
	u32 s = name_slot(i);
	out->text = nor.name[s];
	out->detail = nor.size[s];
	return UI_ITEM_READY;
}

static const struct ui_list_model model = {model_count, model_fetch, 0, 0, 0};

// --- The layout -------------------------------------------------------------------

static const struct ui_title_config title_config = {"", 0, ART_ICON16_NOR};
static const struct ui_hints_config hints_config = {0, 0};
static const struct ui_label_config line_left = {"", UI_FONT_BODY, UI_ALIGN_LEFT, 0, 0};
static const struct ui_label_config line_right = {"", UI_FONT_BODY, UI_ALIGN_RIGHT, 0, 0};
static const struct ui_label_config note_label = {"", UI_FONT_BODY, UI_ALIGN_CENTER, 0, 0};
static const struct ui_label_config format_label = {"", UI_FONT_BODY, UI_ALIGN_LEFT, 0, ART_ICON8_CHIP + 1};
static const struct ui_list_config list_config = {&model, UI_ROW_PITCH_LIST, UI_LIST_MARQUEE, NOR_DETAIL_W,
						  NOR_LIST_PAD};
static const struct ui_scrollbar_config scroll_config = {N_LIST};

static int nor_event(struct ui_view *v, const struct ui_event *e);
static const struct ui_view_spec view = {"nor", nodes, N_NODES, 0, UI_NONE, 0, nor_event};

static void node(u32 i, u8 kind, u8 plane, u8 anchor, u8 flags, u8 ink, u8 paper, s32 x, s32 y, s32 w, s32 h,
		 const void *data)
{
	nodes[i] = (struct ui_spec){kind,  (u8)(i + 1), UI_NONE, plane,  anchor, flags, ink,
				    paper, (s16)x,      (s16)y,  (s16)w, (s16)h, data};
}

// Builds the layout for count games, Format all's chosen card shown if the
// page opens on it
static void build(void)
{
	const struct nor_look *l = &look;
	const struct ui_look *t = &ui_theme()->look;
	u32 rows = nor.count < 1 ? 1 : nor.count > l->list_rows ? l->list_rows : nor.count;
	u32 rows_end = l->list_y + rows * t->list_pitch;
	// The card ends on the first tile line with room for card_pad and its
	// shadow under the rows; what is left over goes in its bottom inset (up
	// to the most a card takes), the rest under the rows
	u32 end = (rows_end + l->card_pad + t->shadow_offset + 7u) & ~7u;
	u32 inset = end - rows_end - l->card_pad, most = 8u - t->card_radius;
	nor.card = l->card_inset;
	nor.card.inset_b = (u8)(inset < most ? inset : most);
	TLOG_ASSERT(end + l->format_h <= t->content_bottom + 1u);

	node(N_TITLE, UI_TITLE, 3, UI_HFILL, 0, 0, 0, 0, 0, 0, l->title_h, &title_config);
	node(N_HINTS, UI_HINTS, 3, UI_HFILL | UI_BOTTOM, 0, 0, 0, 0, 0, 0, l->hints_h, &hints_config);
	node(N_CARD, UI_CARD, 3, UI_HFILL, 0, 0, 0, 0, l->card_y, 0, (s32)end - l->card_y, &nor.card);
	node(N_USED, UI_LABEL, 3, UI_LEFT, 0, UI_ROLE_TEXT, UI_ROLE_PANEL, l->line_x, l->line_y, l->line_w / 2,
	     l->line_h, &line_left);
	node(N_FREE, UI_LABEL, 3, UI_LEFT, 0, UI_ROLE_DIM, UI_ROLE_PANEL, l->line_x + l->line_w / 2, l->line_y,
	     l->line_w / 2, l->line_h, &line_right);
	node(N_BAR, UI_STORAGE, 3, UI_LEFT, 0, 0, UI_ROLE_PANEL, l->line_x, l->bar_y, l->line_w, l->bar_h, 0);
	// (A hidden node on the card's plane would leave a hole in it: the note
	// stays, empty while there are games, and the scroll bar is in front)
	node(N_NOTE, UI_LABEL, 3, UI_LEFT, 0, UI_ROLE_FAINT, UI_ROLE_PANEL, l->list_x, l->note_y, l->list_w, l->note_h,
	     &note_label);
	node(N_LIST, UI_LIST, 1, UI_LEFT, (u8)(nor.count ? 0 : UI_HIDDEN), 0, 0, l->list_x, l->list_y, l->list_w,
	     l->list_rows * t->list_pitch, &list_config);
	node(N_SCROLL, UI_SCROLLBAR, 2, UI_LEFT, (u8)(nor.count > l->list_rows ? 0 : UI_HIDDEN), 0, 0,
	     l->list_x + l->list_w, l->list_y, 8, l->list_rows * t->list_pitch, &scroll_config);
	// The card shown first is drawn last
	int on_format = nor.sel == nor.count;
	nor.panel = (u8)(on_format ? N_FORMAT_1 : N_FORMAT_2);
	nor.lift = (u8)(on_format ? N_FORMAT_2 : N_FORMAT_1);
	node(nor.panel, UI_CARD, 3, UI_HFILL, 0, 0, UI_ROLE_PANEL, 0, (s32)end, 0, l->format_h, &l->format_card);
	node(nor.lift, UI_CARD, 3, UI_HFILL, 0, 0, UI_ROLE_TINT, 0, (s32)end, 0, l->format_h, &l->format_lift);
	node(N_FORMAT_TEXT, UI_LABEL, 2, UI_LEFT, 0, UI_ROLE_DANGER, 0, l->line_x, (s32)end, l->format_text_w,
	     l->format_h, &format_label);
	node(N_FORMAT_DETAIL, UI_LABEL, 2, UI_LEFT, 0, UI_ROLE_DIM, 0, l->format_detail_x, (s32)end,
	     l->line_x + l->line_w - l->format_detail_x, l->format_h, &line_right);
}

// --- The cursor -----------------------------------------------------------------

static u8 at(u32 n)
{
	return (u8)(nor.first + n);
}

static void hints_show(void)
{
	if (nor.sel < nor.count)
		ui_hints_set(at(N_HINTS), nor.hints_game, 3);
	else
		ui_hints_set(at(N_HINTS), nor.hints_format, 2);
}

static void position_show(void)
{
	snprintf(nor.position, sizeof(nor.position), "%lu/%lu", (unsigned long)nor.sel + 1,
		 (unsigned long)nor.count + 1);
	ui_title_set_right(at(N_TITLE), nor.position);
}

// Shows Format all's card shown over other (the same rectangle): it draws
// alone, so it is the one drawn last; a draw of other still pending (a
// quick move back and forth while a map copy waited) is dropped
static void card_show(u8 shown, u8 other)
{
	ui_node(at(other))->flags &= (u8)~UI_N_DIRTY;
	ui_dirty(at(shown), 0xFF);
}

// Moves the page's cursor to row to, by key (a move: the list glides on a
// held key; 0: a jump)
static void choose(u32 to, u16 key)
{
	u32 from = nor.sel;
	if (to == from || to > nor.count)
		return;
	nor.sel = to;
	if (to < nor.count) {
		if (from == nor.count) {
			ui_list_mark(at(N_LIST), 1);
			card_show(nor.panel, nor.lift);
			ui_list_set_cursor(at(N_LIST), to);
		} else {
			ui_list_move(at(N_LIST), to, key);
		}
		tlog_debug("nor: row %lu", (unsigned long)to);
	} else {
		ui_list_mark(at(N_LIST), 0);
		card_show(nor.lift, nor.panel);
		tlog_debug("nor: row format");
	}
	if ((from == nor.count) != (to == nor.count))
		hints_show();
	position_show();
	frame_measure("nor-move");
}

// --- What runs after the frame (nav_defer) ------------------------------------------

static u32 hard_reset(void)
{
	return (ui_input()->held & INPUT_L) != 0;  // L held: as in v0.3
}

static void launch(u32 arg)
{
	u32 game = arg & 0xFF, hard = arg >> 16 & 1;
	tlog("nor: launch %lu%s", (unsigned long)game, hard ? ", hard reset" : "");
	action_nor_launch(game, hard);  // returns only if it didn't start: the shell shows the page again
}

static void info(u32 game)
{
	const FM_NOR_FS *e = &pNorFS[game];
	char size[12];
	size_text(size, sizeof(size), e->filesize);
	int n = snprintf(nor.text, sizeof(nor.text), "%s\n%s \xC2\xB7 %s", e->filename, size,
			 save_type_name(e->savemode));
	if (e->have_RTS && n > 0 && (u32)n < sizeof(nor.text))
		snprintf(nor.text + n, sizeof(nor.text) - (u32)n, "\n%s", gl_nor_states);
	snprintf(nor.title, sizeof(nor.title), "%s", nor.name[name_slot(game)]);
	dialog_message(nor.title, nor.text, DIALOG_INFO);
}

static void delete_latest(u32 game)
{
	tlog("nor: delete %lu", (unsigned long)game);
	// action_nor_delete takes the latest game of the list as the NOR holds
	// it now: only if that is still the game the user chose
	char name[sizeof(pNorFS[0].filename)];
	memcpy(name, pNorFS[game].filename, sizeof(name));
	action_nor_read();
	if (game + 1 != game_total_NOR || strncmp(name, pNorFS[game].filename, sizeof(name)) != 0) {
		tlog_error("nor: delete %lu: the NOR's list changed (%lu games): nothing deleted", (unsigned long)game,
			   (unsigned long)game_total_NOR);
		dialog_message(NULL, gl_nor_changed, DIALOG_ERROR);
		nor.next_row = game;
		nor.next_row_set = 1;
		nav_reshow();
		return;
	}
	dialog_notice(NULL, gl_nor_deleting, DIALOG_INFO);
	int deleted = action_nor_delete();
	dialog_notice_end();
	// On the game before it, or Format all if none is left
	nor.next_row = deleted && game ? game - 1 : game;
	nor.next_row_set = 1;
	nav_reshow();
}

enum {
	POP_LAUNCH,
	POP_INFO,
	POP_DELETE,
};

static void popup(u32 game)
{
	if (game >= nor.count)
		return;
	// Its own copy: the list's rows reuse the name slots while it is up
	snprintf(nor.pop_title, sizeof(nor.pop_title), "%s", nor.name[name_slot(game)]);
	nor.items[POP_LAUNCH] = (struct ui_pop_item){gl_nor_launch, 0, 0};
	nor.items[POP_INFO] = (struct ui_pop_item){gl_nor_info, 0, 0};
	nor.items[POP_DELETE] = (struct ui_pop_item){gl_nor_delete, 0, UI_POP_DANGER};
	nor.menu = (struct ui_popup){nor.pop_title, ART_ICON8_NOR + 1, nor.items, 3, 0};
	if (!ui_popup_open(at(N_LIST), at(N_HINTS), &nor.menu))
		return;
	for (;;) {
		s32 pick = ui_popup_choose();
		if (pick == POP_LAUNCH) {
			u32 hard = hard_reset();
			ui_popup_close();
			launch(game | hard << 16);
			return;
		}
		if (pick == POP_INFO) {
			ui_popup_close();
			info(game);
			return;
		}
		if (pick != POP_DELETE)
			break;  // B: it has closed
		if (game + 1 != nor.count) {
			// The NOR keeps its games back to back: only the last one goes
			ui_popup_close();
			dialog_message(NULL, gl_nor_latest_only, DIALOG_INFO);
			return;
		}
		snprintf(nor.title, sizeof(nor.title), "%s %s?", gl_nor_delete, nor.pop_title);
		struct ui_confirm ask = {nor.title, gl_nor_delete_text, gl_nor_delete, 1};
		if (ui_confirm(at(N_HINTS), &ask)) {
			ui_popup_close();
			delete_latest(game);
			return;
		}
		tlog("nor: delete %lu: cancelled", (unsigned long)game);  // back to the popup
	}
	ui_popup_close();
}

static void format(u32 arg)
{
	(void)arg;
	if (nor.count == 1)
		snprintf(nor.text, sizeof(nor.text), "%s %s", gl_nor_format_one, gl_nor_format_time);
	else if (nor.count)
		snprintf(nor.text, sizeof(nor.text), "%lu %s %s", (unsigned long)nor.count, gl_nor_format_many,
			 gl_nor_format_time);
	else
		snprintf(nor.text, sizeof(nor.text), "%s", gl_nor_format_takes);
	struct ui_confirm ask = {gl_nor_format_ask, nor.text, gl_nor_format_button, 1};
	if (!ui_confirm(at(N_HINTS), &ask)) {
		tlog("nor: format: cancelled");
		return;
	}
	tlog("nor: format");
	action_nor_format();  // behind its progress dialog
	nor.next_row = 0;
	nor.next_row_set = 1;
	nav_reshow();
}

// --- Keys -------------------------------------------------------------------------

static int nor_event(struct ui_view *v, const struct ui_event *e)
{
	(void)v;
	if (e->type != UI_EV_KEYS)
		return 0;
	const InputState *in = ui_input();
	u16 pressed = e->keys & in->pressed;
	if (pressed & INPUT_B) {
		nor.next_row_set = 0;
		nav_back();
		return 1;
	}
	if (e->keys & (INPUT_UP | INPUT_DOWN)) {
		u16 key = (e->keys & INPUT_UP) ? INPUT_UP : INPUT_DOWN;
		choose(input_move(in, key, nor.sel, nor.count + 1, key == INPUT_UP ? -1 : 1), key);
		return 1;
	}
	if (pressed & INPUT_A) {
		if (nor.sel == nor.count)
			nav_defer(format, 0);
		else  // the page's cursor, even while the list's page change draws it
			nav_defer(launch, nor.sel | hard_reset() << 16);
		return 1;
	}
	if ((pressed & INPUT_START) && nor.sel < nor.count)
		nav_defer(popup, nor.sel);
	return 1;
}

// --- The page ---------------------------------------------------------------------

static struct ui_view *nor_enter(u32 item, u32 row)
{
	(void)item;
	// pNorFS is the game NOR's list as the boot read it, and every action
	// that changes it reads it again (actions.h): reading it here, its two
	// copies' CRCs, would cost the page's first frame most of its budget
	nor.count = game_total_NOR < MAX_NOR ? game_total_NOR : MAX_NOR;
	if (nor.next_row_set)
		row = nor.next_row;
	nor.next_row_set = 0;  // used once, by this enter
	nor.sel = row < nor.count ? row : nor.count;  // Last place may hold any row: past the games, Format all
	memset(nor.name_of, NAME_NONE, sizeof(nor.name_of));
	u32 used = 0;
	for (u32 i = 0; i < nor.count; i++) {
		nor.parts[i] = pNorFS[i].filesize;
		used += pNorFS[i].filesize;
	}
	storage_text(used);
	build();
	struct ui_view *v = ui_push(&view, &nor);
	if (!v)
		return 0;
	nor.first = v->first;
	ui_title_set(at(N_TITLE), gl_item_nor);
	position_show();
	ui_label_set(at(N_USED), nor.used);
	ui_label_set(at(N_FREE), nor.free);
	ui_storage_set(at(N_BAR), nor.parts, nor.count, NOR_TOTAL);
	ui_label_set(at(N_NOTE), nor.count ? "" : gl_nor_empty);
	ui_label_set(at(N_FORMAT_TEXT), gl_nor_format);
	ui_label_set(at(N_FORMAT_DETAIL), gl_nor_format_detail);
	nor.hints_game[0] = (struct ui_hint){"A", gl_hint_launch};
	nor.hints_game[1] = (struct ui_hint){"START", gl_hint_menu};
	nor.hints_game[2] = (struct ui_hint){"B", gl_hint_back};
	nor.hints_format[0] = (struct ui_hint){"A", gl_hint_format};
	nor.hints_format[1] = (struct ui_hint){"B", gl_hint_back};
	hints_show();
	if (nor.count) {
		ui_list_reset(at(N_LIST), nor.sel < nor.count ? nor.sel : nor.count - 1);
		ui_list_mark(at(N_LIST), nor.sel < nor.count);
	}
	return v;
}

static u32 nor_row(void)
{
	return nor.sel;
}

const struct nav_page nor_page = {nor_enter, nor_row, 0, 0, 0};
