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
	M_PICK
};
// The manager's rows: the saves without a game, then Empty trash

struct saves {
	struct orphan_scan scan;
	struct orphanpick pick;
	u32 total;      // the library's games, in the picker
	u32 act;        // row + 1 A asked for
	u32 cursor;     // the list's cursor as last seen
	u32 main_cursor;  // the manager's, while the picker shows
	u32 generation;   // the library's, when the picker was built
	u8 mode, back, list, title, hints, first;
	u8 pick_orphan;  // the orphan being relinked
	char row[272];
	char detail[40];
	char count[24];
	char right[24];
	char status[40];
	char line[96];   // the storage line
	char head[40];   // the rows' heading
	u32 parts[3];    // the bar: saves, backups, trash
	char from[SAVESLOT_ROM_PATH_MAX + 2];
	char target[SAVESLOT_ROM_PATH_MAX + 2];
	char ask[SAVESLOT_ROM_PATH_MAX + 2];
	struct ui_hint keys[2];
	struct ui_popup popup;
	struct ui_pop_item items[2];
};
_Static_assert(sizeof(struct saves) <= READCACHE_SAVES_SIZE, "the Saves screen's state fits READCACHE_SAVES");

#define S ((struct saves *)(pReadCache + READCACHE_SAVES))

static u32 main_rows(const struct saves *s)
{
	return s->scan.count + 1;
}

static void size_text(char *out, u32 size, u32 bytes)
{
	gamelist_size(out, size, bytes, gl_games->size_kb, gl_games->size_mb);
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
	return s->mode == M_PICK ? s->pick.total : main_rows(s);
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

static int model_fetch(void *ctx, u32 i, struct ui_item *out)
{
	struct saves *s = ctx;
	if (s->mode == M_PICK)
		return fetch_pick(s, i, out);
	const struct orphan_scan *sc = &s->scan;
	if (i == sc->count) {  // Empty trash, the trash's size beside it
		size_text(s->detail, sizeof(s->detail), sc->trash_bytes);
		out->text = gl_saves->empty_trash;
		out->ink = sc->trash_files ? UI_ROLE_DANGER : UI_ROLE_FAINT;
		out->detail = s->detail;
		return UI_ITEM_READY;
	}
	if (i > sc->count)
		return UI_ITEM_PENDING;
	snprintf(s->row, sizeof(s->row), "%s", saveorphan_path(sc, i));
	nav_count_words(s->detail, sizeof(s->detail), sc->slots[i], gl_saves->slots[0], gl_saves->slots[1]);
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
static const struct ui_label_config head_label = {"", UI_FONT_BODY, UI_ALIGN_LEFT, UI_LABEL_SWAP, ART_ICON8_FOLDER + 1};
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

/* ---- Showing ---- */

static void show_keys(struct saves *s)
{
	u32 n = 0;
	const char *a = s->mode == M_PICK            ? gl_saves->hint_relink
			: s->cursor == s->scan.count ? gl_saves->hint_empty
						     : gl_saves->hint_open;
	s->keys[n++] = (struct ui_hint){"A", a};
	s->keys[n++] = (struct ui_hint){"B", gl_hint_back};
	ui_hints_set(s->hints, s->keys, n);
}

// The title, the storage line and bar, and the rows' heading, from the figures
static void show_figures(struct saves *s)
{
	const struct orphan_scan *sc = &s->scan;
	if (s->mode == M_PICK) {
		ui_title_set(s->title, gl_saves->pick_title);
		ui_title_set_right(s->title, base_of(s->from));
		return;
	}
	ui_title_set(s->title, gl_saves->title);
	size_text(s->right, sizeof(s->right), sc->saves_bytes + sc->backups_bytes + sc->trash_bytes);
	ui_title_set_right(s->title, s->right);
	snprintf(s->line, sizeof(s->line), EN_SAVES_LEGEND, (unsigned long)(sc->saves_bytes + 1023) / 1024,
		 (unsigned long)(sc->backups_bytes + 1023) / 1024, (unsigned long)(sc->trash_bytes + 1023) / 1024);
	ui_label_set((u8)(s->first + M_LINE), s->line);
	s->parts[0] = sc->saves_bytes;
	s->parts[1] = sc->backups_bytes;
	s->parts[2] = sc->trash_bytes;
	u32 total = s->parts[0] + s->parts[1] + s->parts[2];
	ui_storage_set((u8)(s->first + M_BAR), s->parts, 3, total ? total : 1);
	u32 n = sc->count + sc->more;
	if (n)
		snprintf(s->head, sizeof(s->head), EN_SAVES_HEADING, gl_saves->heading, (unsigned long)n);
	else
		snprintf(s->head, sizeof(s->head), "%s", gl_saves->none);
	ui_label_set((u8)(s->first + M_HEAD), s->head);
}

// Pushes the manager's view or the picker's (the other goes), its list on
// cursor; 0 if it can't
static int show(struct saves *s, u32 cursor)
{
	while (ui_depth())
		ui_pop();
	struct ui_view *v = ui_push(s->mode == M_PICK ? &pick_view : &main_view, s);
	if (!v) {
		tlog_error("saves: %s", ui_error());
		return 0;
	}
	s->first = v->first;
	s->list = (u8)(v->first + (s->mode == M_PICK ? N_LIST : M_LIST));
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
	snprintf(s->status, sizeof(s->status), "%s \xC2\xB7 %u", gl_saves->phase[phase < ORPHAN_PHASES ? phase : 0],
		 (unsigned)entries);
	dialog_progress_set(phase, ORPHAN_PHASES, s->status);
}

static void scan(struct saves *s)
{
	dialog_progress_begin(gl_saves->scanning, gl_saves->scanning_text);
	dialog_progress_status(gl_saves->phase[0]);
	saveorphan_scan(&s->scan, progress, s);
	dialog_progress_end();
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
		return gl_saves->err_source;
	case ORPHAN_NO_TARGET:
		return gl_saves->err_target;
	case ORPHAN_PATH:
		return gl_saves->err_path;
	case ORPHAN_STOPPED:
		return gl_saves->err_stopped;
	default:
		return gl_saves->err_card;
	}
}

/* ---- Actions ---- */

static void enter_pick(struct saves *s, u32 o)
{
	library_load_now();
	const u8 *slot = library_or_null();
	u32 total = slot ? library_games(slot) : 0;
	if (!total) {
		dialog_message(NULL, gl_saves->no_library, DIALOG_ERROR);
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
	dialog_notice(gl_saves->moving, gl_saves->moving_text, DIALOG_INFO);
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
	const struct lang_saves *g = gl_saves;
	snprintf(s->ask, sizeof(s->ask), "%s", saveorphan_path(&s->scan, o));
	s->items[0] = (struct ui_pop_item){g->pop_relink, 0, 0};
	s->items[1] = (struct ui_pop_item){g->pop_delete, 0, UI_POP_DANGER};
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
		struct ui_confirm ask = {g->ask_delete, g->ask_delete_text, g->ask_delete_button, 1};
		if (!ui_confirm(s->hints, &ask))
			continue;
		ui_popup_close();
		do_delete(s, o);
		return;
	}
}

static void empty_trash(struct saves *s)
{
	if (!s->scan.trash_files) {
		dialog_message(NULL, gl_saves->trash_empty, DIALOG_INFO);
		return;
	}
	struct ui_confirm ask = {gl_saves->ask_empty, gl_saves->ask_empty_text, gl_saves->ask_empty_button, 1};
	if (!ui_confirm(s->hints, &ask))
		return;
	dialog_notice(gl_saves->emptying, gl_saves->emptying_text, DIALOG_INFO);
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
	struct ui_confirm ask = {gl_saves->ask_relink, base_of(s->target), gl_saves->ask_relink_button, 0};
	if (!ui_confirm(s->hints, &ask))
		return;
	dialog_notice(gl_saves->moving, gl_saves->moving_text, DIALOG_INFO);
	u32 renamed = 0;
	int r = saveorphan_relink(s->from, s->target, &renamed);
	dialog_notice_end();
	if (r) {
		dialog_message(NULL, error_text(r), DIALOG_ERROR);
	} else {
		orphan_gone(s, s->pick_orphan, 0);
		if (renamed) {
			char text[96];
			snprintf(text, sizeof(text), EN_SAVES_RENAMED, (unsigned)renamed);
			dialog_message(NULL, text, DIALOG_INFO);
		}
	}
	s->mode = M_MAIN;
	s->cursor = s->main_cursor;
	after(s, r != 0);
}

static void act(struct saves *s, u32 row)
{
	if (s->mode == M_PICK)
		pick_choose(s, row);
	else if (row == s->scan.count)
		empty_trash(s);
	else if (row < s->scan.count)
		orphan_menu(s, row);
}

/* ---- The screen ---- */

void saves_run(void)
{
	struct saves *s = S;
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
			act(s, row);
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
	ui_leave(UI_TR_DEEPER, -1);
	while (ui_depth())
		ui_pop();
}
