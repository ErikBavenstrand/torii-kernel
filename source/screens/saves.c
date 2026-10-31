// The Saves screen (saves.h; docs/design/v0.5/saves-variants.md, direction
// B), two views that take turns: the manager, the NOR page's look (a card
// with what the saves use as a bar and a line of sizes, then the saves
// without a game and Empty trash as rows), and the relink picker, the game
// lists' look (a card with the library's games). Its state lives in
// pReadCache (READCACHE_SAVES) while it runs. After a delete, a relink or
// Empty trash its figures change in place; the card is walked again only
// when saves past the listed ones wait to show.
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "saves.h"
#include "ff.h"
#include "../ez_define.h"
#include "../ezkernel.h"
#include "../gamelist.h"
#include "../lang.h"
#include "../library.h"
#include "../saveorphan.h"
#include "../actions.h"
#include "game_saves.h"
#include "nav.h"
#include "../tlog.h"
#include "../engine/engine.h"
#include "../ui/art_ids.h"
#include "../ui/dialog.h"
#include "../ui/ui.h"

// --- Look, px: the game lists' (screens/games.c) and the NOR page's --------
#define TITLE_H 24
#define CARD_Y 16
#define CARD_H 128
#define LIST_X 8
#define LIST_Y 24
#define LIST_W 216
#define LIST_H 112
#define LIST_PAD 4
#define DETAIL_W 64
#define HINTS_H 16
// The manager's: the storage line and bar, the rows' heading, then 4 rows
#define LINE_Y 24  // the card's body starts here (under the title bar's tiles)
#define LINE_H 16
#define BAR_Y 40
#define BAR_H 8
#define HEAD_Y 56
#define HEAD_H 16
#define ROWS_Y 72
#define ROWS_H 56
static const struct ui_card_config card = {4, 4, 4, 1, 0};

// The picker's nodes
enum {
	N_TITLE,
	N_HINTS,
	N_CARD,
	N_LIST,
	N_SCROLL,
	N_COUNT,
};
// The manager's (title, hints and card as the picker's)
enum {
	M_LINE = N_LIST,
	M_BAR,
	M_HEAD,
	M_SCROLL,
	M_LIST,  // last: its rows draw within the first frame's budget
	M_COUNT
};

enum {
	M_MAIN,
	M_PICK,
	M_GAMES  // Game saves: the library's games with saves (the picker's look)
};
// The manager's rows: the saves without a game, then Empty trash

struct saves {
	struct orphan_scan scan;
	struct orphanpick pick;
	u32 total;      // the library's games, in the picker
	u32 act;        // row + 1 A asked for
	u32 cursor;     // the list's cursor as last seen
	u32 main_cursor;  // the manager's, while the picker shows
	u32 generation;   // the library's, when the picker or Game saves was built
	u32 games_n;      // Game saves' rows
	u16 games[ORPHAN_HAVE];  // their positions in the library's order
	u8 mode, back, list, title, hints, first;
	u8 pick_orphan;  // the orphan being relinked
	char row[272];
	char detail[40];
	char count[24];
	char right[24];
	char status[40];
	char line[96];   // the storage line
	char head[40];   // the rows' heading
	u32 parts[2];    // the bar: saves, backups
	char from[SAVESLOT_ROM_PATH_MAX + 2];
	char target[SAVESLOT_ROM_PATH_MAX + 2];
	char ask[SAVESLOT_ROM_PATH_MAX + 2];
	struct ui_hint keys[2];
	struct ui_popup popup;
	struct ui_pop_item items[2];
};
_Static_assert(sizeof(struct saves) <= READCACHE_SAVES_SIZE, "the Saves screen's state fits READCACHE_SAVES");

#define SV ((struct saves *)(pReadCache + READCACHE_SAVES))

static u32 main_rows(const struct saves *s)
{
	return s->scan.count + 1;
}

static void size_text(char *out, u32 size, u32 bytes)
{
	gamelist_size(out, size, bytes);
}

static const char *base_of(const char *path)
{
	const char *slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

/* ---- The library's order, for the picker ---- */

static const u8 *library_or_null(void)
{
	return library_ready() ? library_slot() : NULL;
}

static u32 library_games(const u8 *slot)
{
	const struct lib_header *h = lib_hdr(slot);
	u32 n = 0;
	for (u32 c = 0; c < LIBRARY_CONSOLE_SLOTS; c++)
		n += h->counts[c];
	return n;
}

/* ---- The list ---- */

static u32 model_count(void *ctx)
{
	struct saves *s = ctx;
	return s->mode == M_PICK ? s->pick.total : s->mode == M_GAMES ? s->games_n : main_rows(s);
}

static int fetch_pick(struct saves *s, u32 i, struct ui_item *out)
{
	const u8 *slot = library_or_null();
	if (!slot || i >= s->pick.total || s->generation != library_generation())
		return UI_ITEM_PENDING;
	u32 rec = lib_order(slot)[orphanpick_pos(&s->pick, i)];
	if (!lib_path(slot, rec, s->row, sizeof(s->row)))
		return UI_ITEM_PENDING;
	out->text = s->row;
	static const u8 ink[3] = {UI_ROLE_ACCENT_INK, UI_ROLE_TEXT, UI_ROLE_DIM};
	out->ink = ink[orphanpick_group(&s->pick, i)];
	return UI_ITEM_READY;
}

static int fetch_game(struct saves *s, u32 i, struct ui_item *out)
{
	const u8 *slot = library_or_null();
	if (!slot || i >= s->games_n || s->generation != library_generation())
		return UI_ITEM_PENDING;
	out->text = lib_display(slot, lib_order(slot)[s->games[i]]);
	out->more = 1;  // A opens its saves
	return UI_ITEM_READY;
}

static int model_fetch(void *ctx, u32 i, struct ui_item *out)
{
	struct saves *s = ctx;
	if (s->mode == M_PICK)
		return fetch_pick(s, i, out);
	if (s->mode == M_GAMES)
		return fetch_game(s, i, out);
	const struct orphan_scan *sc = &s->scan;
	if (i == sc->count) {  // Empty trash, the trash's size beside it
		size_text(s->detail, sizeof(s->detail), sc->trash_bytes);
		out->text = S(STR_SAVES_EMPTY_TRASH);
		out->ink = sc->trash_files ? UI_ROLE_DANGER : UI_ROLE_FAINT;
		out->detail = s->detail;
		return UI_ITEM_READY;
	}
	if (i > sc->count)
		return UI_ITEM_PENDING;
	snprintf(s->row, sizeof(s->row), "%s", saveorphan_path(sc, i));
	nav_count_words(s->detail, sizeof(s->detail), sc->slots[i], STR_SAVES_SLOTS);
	out->text = s->row;
	out->detail = s->detail;
	return UI_ITEM_READY;
}

static const struct ui_list_model model = {model_count, model_fetch, 0, 0, 0};

static int on_event(struct ui_view *v, const struct ui_event *e)
{
	struct saves *s = v->ctx;
	switch (e->type) {
	case UI_EV_CURSOR:
		s->cursor = e->index;
		return 1;
	case UI_EV_ACTIVATE:
		s->act = e->index + 1;
		return 1;
	case UI_EV_KEYS:
		if (e->keys & ui_input()->pressed & INPUT_B) {
			s->back = 1;
			return 1;
		}
		return 0;
	default:
		return 0;
	}
}

static const struct ui_title_config title_config = {"", "", ART_ICON16_SET};
static const struct ui_hints_config no_hints = {0, 0};
static const struct ui_list_config list_config = {&model, UI_ROW_PITCH_LIST, UI_LIST_MARQUEE, DETAIL_W, LIST_PAD};
static const struct ui_scrollbar_config bar = {N_LIST};

static const struct ui_spec pick_nodes[N_COUNT] = {
	[N_TITLE] = {UI_TITLE, 1, UI_NONE, 3, UI_HFILL, 0, 0, 0, 0, 0, 0, TITLE_H, &title_config},
	[N_HINTS] = {UI_HINTS, 2, UI_NONE, 3, UI_HFILL | UI_BOTTOM, 0, 0, 0, 0, 0, 0, HINTS_H, &no_hints},
	[N_CARD] = {UI_CARD, 3, UI_NONE, 2, UI_HFILL, 0, 0, 0, 0, CARD_Y, 0, CARD_H, &card},
	[N_LIST] = {UI_LIST, 4, UI_NONE, 1, UI_LEFT, UI_FOCUSABLE, 0, 0, LIST_X, LIST_Y, LIST_W, LIST_H, &list_config},
	[N_SCROLL] = {UI_SCROLLBAR, 5, UI_NONE, 2, UI_LEFT, 0, 0, UI_ROLE_PANEL, LIST_X + LIST_W, LIST_Y, 8, LIST_H,
		      &bar},
};

static const struct ui_label_config line_label = {"", UI_FONT_BODY, UI_ALIGN_LEFT, UI_LABEL_SWAP, 0};
static const struct ui_label_config head_label = {"", UI_FONT_BODY, UI_ALIGN_LEFT, UI_LABEL_MARQUEE,
						  ART_ICON8_FOLDER + 1};
static const struct ui_scrollbar_config main_bar = {M_LIST};
static const struct ui_list_config main_list = {&model, UI_ROW_PITCH_LIST, UI_LIST_MARQUEE, DETAIL_W, LIST_PAD};

// On plane 3 with the title, as the NOR page's: plane 2's tiles stay for a
// popup, a confirmation and a dialog over it
static const struct ui_card_config main_card = {4, 0, 4, 4, 0};
static const struct ui_spec main_nodes[M_COUNT] = {
	[N_TITLE] = {UI_TITLE, 1, UI_NONE, 3, UI_HFILL, 0, 0, 0, 0, 0, 0, TITLE_H, &title_config},
	[N_HINTS] = {UI_HINTS, 2, UI_NONE, 3, UI_HFILL | UI_BOTTOM, 0, 0, 0, 0, 0, 0, HINTS_H, &no_hints},
	[N_CARD] = {UI_CARD, 3, UI_NONE, 3, UI_HFILL, 0, 0, 0, 0, TITLE_H, 0, 144 - TITLE_H, &main_card},
	[M_LINE] = {UI_LABEL, 4, UI_NONE, 3, UI_LEFT, 0, UI_ROLE_DIM, UI_ROLE_PANEL, LIST_X, LINE_Y, LIST_W, LINE_H,
		    &line_label},
	[M_BAR] = {UI_STORAGE, 5, UI_NONE, 3, UI_LEFT, 0, 0, UI_ROLE_PANEL, LIST_X, BAR_Y, LIST_W, BAR_H, 0},
	[M_HEAD] = {UI_LABEL, 6, UI_NONE, 3, UI_LEFT, 0, UI_ROLE_ACCENT_INK, UI_ROLE_PANEL, LIST_X, HEAD_Y, LIST_W,
		    HEAD_H, &head_label},
	[M_SCROLL] = {UI_SCROLLBAR, 7, UI_NONE, 2, UI_LEFT, 0, 0, UI_ROLE_PANEL, LIST_X + LIST_W, ROWS_Y, 8, ROWS_H,
		      &main_bar},
	[M_LIST] = {UI_LIST, 8, UI_NONE, 1, UI_LEFT, UI_FOCUSABLE, 0, 0, LIST_X, ROWS_Y, LIST_W, ROWS_H, &main_list},
};

static const struct ui_view_spec main_view = {"saves", main_nodes, M_COUNT, M_LIST, UI_VIEW_LR_PAGES, on_event};
static const struct ui_view_spec pick_view = {"saves-pick", pick_nodes, N_COUNT, N_LIST, UI_VIEW_LR_PAGES, on_event};
static const struct ui_view_spec games_view = {"saves-games", pick_nodes, N_COUNT, N_LIST, UI_VIEW_LR_PAGES, on_event};

/* ---- Showing ---- */

// A empty trash with nothing in it does nothing: its row is faint, 0 KB
// beside it, and the hints offer only B
static void show_keys(struct saves *s)
{
	u32 n = 0;
	int trash = s->mode == M_MAIN && s->cursor == s->scan.count;
	const char *a = s->mode == M_PICK    ? S(STR_SAVES_HINT_RELINK)
			: s->mode == M_GAMES ? S(STR_SAVES_HINT_GAMES)
			: trash              ? S(STR_SAVES_HINT_EMPTY)
					     : S(STR_SAVES_HINT_OPEN);
	if (!trash || s->scan.trash_files)
		s->keys[n++] = (struct ui_hint){"A", a};
	s->keys[n++] = (struct ui_hint){"B", S(STR_HINT_BACK)};
	ui_hints_set(s->hints, s->keys, n);
}

// The title, the storage line and bar, and the rows' heading, from the figures
static void show_figures(struct saves *s)
{
	const struct orphan_scan *sc = &s->scan;
	if (s->mode == M_PICK) {
		ui_title_set(s->title, S(STR_SAVES_PICK_TITLE));
		ui_title_set_right(s->title, base_of(s->from));
		return;
	}
	if (s->mode == M_GAMES) {
		ui_title_set(s->title, S(STR_SAVES_GAMES_TITLE));
		nav_count_words(s->right, sizeof(s->right), s->games_n, STR_SAVES_GAMES);
		ui_title_set_right(s->title, s->right);
		return;
	}
	ui_title_set(s->title, S(STR_SAVES_TITLE));
	size_text(s->right, sizeof(s->right), sc->saves_bytes + sc->backups_bytes + sc->trash_bytes);
	ui_title_set_right(s->title, s->right);
	char saves[16], backups[16];
	size_text(saves, sizeof(saves), sc->saves_bytes);
	size_text(backups, sizeof(backups), sc->backups_bytes);
	SF(s->line, sizeof(s->line), STR_SAVES_LEGEND, LS(saves), LS(backups));
	ui_label_set((u8)(s->first + M_LINE), s->line);
	// Two shades, as the line above names two figures; the trash's size is
	// on its own row (Empty trash)
	s->parts[0] = sc->saves_bytes;
	s->parts[1] = sc->backups_bytes;
	u32 total = s->parts[0] + s->parts[1];
	ui_storage_set((u8)(s->first + M_BAR), s->parts, 2, total ? total : 1);
	u32 n = sc->count + sc->more;
	if (n)
		SF(s->head, sizeof(s->head), STR_SAVES_HEADING_COUNT, LN(n));
	else
		snprintf(s->head, sizeof(s->head), "%s", S(STR_SAVES_NONE));
	ui_label_set((u8)(s->first + M_HEAD), s->head);
}

// Pushes the manager's view or the picker's (the other goes), its list on
// cursor; 0 if it can't
static int show(struct saves *s, u32 cursor)
{
	while (ui_depth())
		ui_pop();
	struct ui_view *v = ui_push(s->mode == M_PICK ? &pick_view : s->mode == M_GAMES ? &games_view : &main_view, s);
	if (!v) {
		tlog_error("saves: %s", ui_error());
		return 0;
	}
	s->first = v->first;
	s->list = (u8)(v->first + (s->mode == M_MAIN ? M_LIST : N_LIST));
	s->title = (u8)(v->first + N_TITLE);
	s->hints = (u8)(v->first + N_HINTS);
	ui_list_reset(s->list, cursor);
	s->cursor = ui_list_cursor(s->list);
	show_figures(s);
	show_keys(s);
	return 1;
}

/* ---- The card walk ---- */

static void progress(void *ctx, u32 phase, u32 entries)
{
	struct saves *s = ctx;
	SF(s->status, sizeof(s->status), STR_SAVES_PHASE,
	   LS(S(STR_SAVES_PHASE_SAVES + (phase < ORPHAN_PHASES ? phase : 0))), LN(entries));
	dialog_progress_set(phase, ORPHAN_PHASES, s->status);
}

// ORPHAN_OK, or ORPHAN_CARD when the card didn't answer
static int scan(struct saves *s)
{
	dialog_progress_begin(S(STR_SAVES_SCANNING), S(STR_SAVES_SCANNING_TEXT));
	dialog_progress_status(S(STR_SAVES_PHASE_SAVES));
	int r = saveorphan_scan(&s->scan, progress, s);
	dialog_progress_end();
	return r;
}

// Orphan o is no longer one (relinked or in the trash): it leaves the list,
// and with trashed its bytes move to the trash's figures
static void orphan_gone(struct saves *s, u32 o, int trashed)
{
	struct orphan_scan *sc = &s->scan;
	if (o >= sc->count)
		return;
	if (trashed) {
		u32 hist = sc->hist[o], own = sc->bytes[o] - hist;
		sc->saves_bytes -= own < sc->saves_bytes ? own : sc->saves_bytes;
		sc->backups_bytes -= hist < sc->backups_bytes ? hist : sc->backups_bytes;
		sc->trash_bytes += sc->bytes[o];
		sc->trash_files += sc->slots[o] + 1u;
	}
	for (u32 k = o + 1; k < sc->count; k++) {
		sc->off[k - 1] = sc->off[k];
		sc->slots[k - 1] = sc->slots[k];
		sc->bytes[k - 1] = sc->bytes[k];
		sc->hist[k - 1] = sc->hist[k];
	}
	sc->count--;
}

// The manager again after an action changed the figures: in place, or a new
// walk when orphans past the listed ones wait to show (or the action failed,
// which may have moved part of them)
static void after(struct saves *s, int walk)
{
	if (walk || s->scan.more)
		scan(s);
	u32 rows = main_rows(s);
	show(s, s->cursor < rows ? s->cursor : rows - 1);
}

static const char *error_text(int r)
{
	switch (r) {
	case ORPHAN_NO_SOURCE:
		return S(STR_SAVES_ERR_SOURCE);
	case ORPHAN_NO_TARGET:
		return S(STR_SAVES_ERR_TARGET);
	case ORPHAN_PATH:
		return S(STR_SAVES_ERR_PATH);
	case ORPHAN_STOPPED:
		return S(STR_SAVES_ERR_STOPPED);
	default:
		return S(STR_SAVES_ERR_CARD);
	}
}

/* ---- Actions ---- */

static void enter_pick(struct saves *s, u32 o)
{
	library_load_now();
	const u8 *slot = library_or_null();
	u32 total = slot ? library_games(slot) : 0;
	if (!total) {
		dialog_message(NULL, S(STR_SAVES_NO_LIBRARY), DIALOG_ERROR);
		return;
	}
	snprintf(s->from, sizeof(s->from), "%s", saveorphan_path(&s->scan, o));
	const char *base = base_of(s->from);
	u32 console = library_console_of(base), cs = 0, ce = 0;
	if (console < LIB_CONSOLES) {
		cs = lib_console_start(slot, console);
		ce = cs + lib_hdr(slot)->counts[console];
	}
	orphanpick_begin(&s->pick, total, cs, ce);
	char first = (char)(base[0] | 0x20);
	for (u32 pos = 0; pos < total && s->pick.nn < ORPHANPICK_NAMES; pos++) {
		const char *name = lib_name(slot, lib_order(slot)[pos]);
		if ((char)(name[0] | 0x20) == first && !strcasecmp(name, base))
			orphanpick_add(&s->pick, pos);
	}
	s->total = total;
	s->generation = library_generation();
	s->pick_orphan = (u8)o;
	s->main_cursor = s->cursor;
	s->mode = M_PICK;
	show(s, 0);
	tlog_debug("saves: picker for %s, %lu games, %lu with its name", s->from, (unsigned long)total,
		   (unsigned long)s->pick.nn);
}

static void leave_pick(struct saves *s)
{
	s->mode = M_MAIN;
	show(s, s->main_cursor < main_rows(s) ? s->main_cursor : 0);
}

static void do_delete(struct saves *s, u32 o)
{
	char path[SAVESLOT_ROM_PATH_MAX + 2];
	snprintf(path, sizeof(path), "%s", saveorphan_path(&s->scan, o));
	dialog_notice(S(STR_SAVES_MOVING), S(STR_SAVES_MOVING_TEXT), DIALOG_INFO);
	int r = saveorphan_trash(path);
	dialog_notice_end();
	if (r)
		dialog_message(NULL, error_text(r), DIALOG_ERROR);
	else
		orphan_gone(s, o, 1);
	after(s, r != 0);
}

static void orphan_menu(struct saves *s, u32 o)
{
	snprintf(s->ask, sizeof(s->ask), "%s", saveorphan_path(&s->scan, o));
	s->items[0] = (struct ui_pop_item){S(STR_SAVES_POP_RELINK), 0, 0};
	s->items[1] = (struct ui_pop_item){S(STR_SAVES_POP_DELETE), 0, UI_POP_DANGER};
	s->popup = (struct ui_popup){base_of(s->ask), ART_ICON8_FOLDER + 1, s->items, 2, 0, 0};
	if (!ui_popup_open(s->list, s->hints, &s->popup))
		return;
	tlog_debug("saves: popup %s", s->ask);
	for (;;) {
		s32 pick = ui_popup_choose();
		if (pick < 0)
			return;
		if (pick == 0) {
			ui_popup_close();
			enter_pick(s, o);
			return;
		}
		struct ui_confirm ask = {S(STR_SAVES_ASK_DELETE), S(STR_SAVES_ASK_DELETE_TEXT),
					 S(STR_SAVES_ASK_DELETE_BUTTON), 1};
		if (!ui_confirm(s->hints, &ask))
			continue;
		ui_popup_close();
		do_delete(s, o);
		return;
	}
}

static void empty_trash(struct saves *s)
{
	if (!s->scan.trash_files)
		return;
	struct ui_confirm ask = {S(STR_SAVES_ASK_EMPTY), S(STR_SAVES_ASK_EMPTY_TEXT), S(STR_SAVES_ASK_EMPTY_BUTTON), 1};
	if (!ui_confirm(s->hints, &ask))
		return;
	dialog_notice(S(STR_SAVES_EMPTYING), S(STR_SAVES_EMPTYING_TEXT), DIALOG_INFO);
	u32 removed = 0;
	int r = saveorphan_trash_empty(NULL, NULL, &removed);
	dialog_notice_end();
	if (r) {
		dialog_message(NULL, error_text(r), DIALOG_ERROR);
	} else {
		s->scan.trash_bytes = 0;
		s->scan.trash_files = 0;
	}
	after(s, r != 0);
}

static void pick_choose(struct saves *s, u32 row)
{
	const u8 *slot = library_or_null();
	if (!slot || row >= s->pick.total || s->generation != library_generation()) {
		leave_pick(s);
		return;
	}
	u32 rec = lib_order(slot)[orphanpick_pos(&s->pick, row)];
	if (!lib_path(slot, rec, s->target, sizeof(s->target)))
		return;
	struct ui_confirm ask = {S(STR_SAVES_ASK_RELINK), base_of(s->target), S(STR_SAVES_ASK_RELINK_BUTTON), 0};
	if (!ui_confirm(s->hints, &ask))
		return;
	dialog_notice(S(STR_SAVES_MOVING), S(STR_SAVES_MOVING_TEXT), DIALOG_INFO);
	u32 renamed = 0;
	int r = saveorphan_relink(s->from, s->target, &renamed);
	dialog_notice_end();
	if (r) {
		dialog_message(NULL, error_text(r), DIALOG_ERROR);
	} else {
		orphan_gone(s, s->pick_orphan, 0);
		if (renamed) {
			char text[96];
			SF(text, sizeof(text), STR_SAVES_RENAMED, LN(renamed));
			dialog_message(NULL, text, DIALOG_INFO);
		}
	}
	s->mode = M_MAIN;
	s->cursor = s->main_cursor;
	after(s, r != 0);
}

/* ---- Game saves ---- */

// Game saves' rows: the library's games, in its order, that the walk saw
// saves of; 0 if the library isn't there (said)
static int build_games(struct saves *s)
{
	library_load_now();
	const u8 *slot = library_or_null();
	if (!slot) {
		dialog_message(NULL, S(STR_SAVES_NO_LIBRARY), DIALOG_ERROR);
		return 0;
	}
	u32 total = library_games(slot);
	s->games_n = 0;
	for (u32 pos = 0; pos < total && s->games_n < ORPHAN_HAVE; pos++)
		if (lib_path(slot, lib_order(slot)[pos], s->row, sizeof(s->row)) && saveorphan_has(&s->scan, s->row))
			s->games[s->games_n++] = (u16)pos;
	s->generation = library_generation();
	tlog_debug("saves: %lu games with saves, %lu listed", (unsigned long)s->scan.have_count,
		   (unsigned long)s->games_n);
	return 1;
}

// A walk of the card for Game saves' rows; 0 if there are none (said)
static int games_scan(struct saves *s)
{
	memset(s, 0, sizeof(*s));
	s->mode = M_GAMES;
	if (scan(s) != ORPHAN_OK) {
		dialog_message(NULL, S(STR_SAVES_ERR_CARD), DIALOG_ERROR);
		return 0;
	}
	if (!build_games(s))
		return 0;
	if (!s->games_n) {
		// A walk cut short (a card error part way) may have missed them
		dialog_message(NULL, s->scan.partial ? S(STR_SAVES_ERR_CARD) : S(STR_SAVES_GAMES_NONE),
			       s->scan.partial ? DIALOG_ERROR : DIALOG_INFO);
		return 0;
	}
	if (s->scan.have_more)
		tlog("saves: %lu more games with saves than Game saves lists", (unsigned long)s->scan.have_more);
	return 1;
}

static int games_show(struct saves *s, u32 cursor)
{
	return games_scan(s) && show(s, cursor < s->games_n ? cursor : s->games_n - 1);
}

// A on a game: its Saves page in this one's place (it shares this screen's
// memory, so the card is walked again after); a slot played there starts
// the game. 0 once nothing is left to show.
static int open_game(struct saves *s, u32 row)
{
	const u8 *slot = library_or_null();
	if (!slot || row >= s->games_n || s->generation != library_generation())
		return games_show(s, row);
	u32 rec = lib_order(slot)[s->games[row]];
	char path[SAVESLOT_ROM_PATH_MAX + 2], title[96];
	if (!lib_path(slot, rec, path, sizeof(path)))
		return 1;
	snprintf(title, sizeof(title), "%s", lib_display(slot, rec));
	if (!game_saves_probe(path)) {
		// A record (game.ini) without a slot, or another game's key
		tlog("saves: %s has no saves to manage", path);
		dialog_message(NULL, S(STR_GSAVES_NO_SAVE), DIALOG_INFO);
		return 1;
	}
	tlog("saves: game saves of %s", path);
	// Its page takes this screen's memory (READCACHE_GSAVES is
	// READCACHE_SAVES): this one goes first, so no frame of the page's
	// coming in reads it
	ui_leave(UI_TR_DEEPER, 1);
	while (ui_depth())
		ui_pop();
	if (game_saves_run(title) == GAME_SAVES_LAUNCH) {
		action_slot_chosen(1);
		action_launch_title(title);
		action_launch(path, ACTION_DEFAULT, ACTION_SAVE_KEEP, 0);
		action_launch_title(NULL);
		action_slot_chosen(0);  // it returned: nothing started
		if (!ui_active())
			return 0;  // the session ended: Settings comes back
	}
	return games_show(s, row);
}

static int act(struct saves *s, u32 row)
{
	if (s->mode == M_GAMES)
		return open_game(s, row);
	if (s->mode == M_PICK)
		pick_choose(s, row);
	else if (row == s->scan.count)
		empty_trash(s);
	else if (row < s->scan.count)
		orphan_menu(s, row);
	return 1;
}

/* ---- The screen ---- */

// Runs the screen shown until B leaves it (from the picker: to the list)
static void run(struct saves *s)
{
	u32 seen = s->cursor;
	while (ui_depth()) {
		ui_frame();
		if (s->cursor != seen) {
			seen = s->cursor;
			show_keys(s);
		}
		if (s->act) {
			u32 row = s->act - 1;
			s->act = 0;
			if (!act(s, row))
				break;
			seen = s->cursor;
		}
		if (s->back) {
			s->back = 0;
			if (s->mode == M_PICK)
				leave_pick(s);
			else
				break;
		}
	}
	if (!ui_active())
		return;  // a game played from Game saves didn't start: its session ended
	ui_leave(UI_TR_DEEPER, -1);
	while (ui_depth())
		ui_pop();
}

void saves_run(void)
{
	struct saves *s = SV;
	if (!pending_save_flush())  // a save still on the chip first
		return;
	memset(s, 0, sizeof(*s));
	scan(s);  // over the page that opened it
	ui_leave(UI_TR_DEEPER, 1);
	if (!show(s, 0)) {
		while (ui_depth())
			ui_pop();
		return;
	}
	tlog("saves: %lu orphans", (unsigned long)s->scan.count);
	run(s);
}

void saves_games_run(void)
{
	struct saves *s = SV;
	if (!pending_save_flush())
		return;
	if (!games_scan(s))  // over the page that opened it
		return;
	ui_leave(UI_TR_DEEPER, 1);
	if (!show(s, 0)) {
		while (ui_depth())
			ui_pop();
		return;
	}
	run(s);
}
