// The Saves screen (saves.h), in the game lists' look (the cheat screen's
// layout: title bar, a card with the list, scroll bar, hint bar). One list
// shows two things in turn: the manager's rows, and the relink picker's
// games. Its state lives in pReadCache (READCACHE_SAVES) while it runs.
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

// --- Look, px: the game lists' (screens/games.c) ----------------------------
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
static const struct ui_card_config card = {4, 4, 4, 1, 0};

enum {
	N_TITLE,
	N_HINTS,
	N_CARD,
	N_LIST,
	N_SCROLL,
	N_COUNT,
};

enum {
	M_MAIN,
	M_PICK
};
// The manager's rows: three of space, Empty trash, the heading (or "none"), then the orphans
enum {
	ROW_SAVES,
	ROW_HISTORY,
	ROW_TRASH,
	ROW_EMPTY,
	ROW_HEAD,
	ROW_FIRST
};

struct saves {
	struct orphan_scan scan;
	struct orphanpick pick;
	u32 total;      // the library's games, in the picker
	u32 act;        // row + 1 A asked for
	u32 cursor;     // the list's cursor as last seen
	u32 main_cursor;  // the manager's, while the picker shows
	u32 generation;   // the library's, when the picker was built
	u8 mode, back, list, title, hints, keys_acts;
	u8 kept[ORPHAN_MAX];
	u8 pick_orphan;  // the orphan being relinked
	char row[272];
	char detail[40];
	char count[24];
	char right[24];
	char status[40];
	char from[SAVESLOT_ROM_PATH_MAX + 2];
	char target[SAVESLOT_ROM_PATH_MAX + 2];
	char ask[SAVESLOT_ROM_PATH_MAX + 2];
	struct ui_hint keys[2];
	struct ui_popup popup;
	struct ui_pop_item items[3];
};
_Static_assert(sizeof(struct saves) <= READCACHE_SAVES_SIZE, "the Saves screen's state fits READCACHE_SAVES");

#define S ((struct saves *)(pReadCache + READCACHE_SAVES))

static u32 main_rows(const struct saves *s)
{
	return ROW_FIRST + s->scan.count;
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
	const struct lang_saves *g = gl_saves;
	switch (i) {
	case ROW_SAVES:
		nav_count_words(s->count, sizeof(s->count), sc->games, g->games[0], g->games[1]);
		snprintf(s->row, sizeof(s->row), "%s \xC2\xB7 %s", g->row_saves, s->count);
		size_text(s->detail, sizeof(s->detail), sc->saves_bytes);
		break;
	case ROW_HISTORY:
		nav_count_words(s->count, sizeof(s->count), sc->backups_files, g->versions[0], g->versions[1]);
		snprintf(s->row, sizeof(s->row), "%s \xC2\xB7 %s", g->row_history, s->count);
		size_text(s->detail, sizeof(s->detail), sc->backups_bytes);
		break;
	case ROW_TRASH:
		nav_count_words(s->count, sizeof(s->count), sc->trash_files, g->files[0], g->files[1]);
		snprintf(s->row, sizeof(s->row), "%s \xC2\xB7 %s", g->row_trash, s->count);
		size_text(s->detail, sizeof(s->detail), sc->trash_bytes);
		break;
	case ROW_EMPTY:
		out->text = g->empty_trash;
		out->ink = sc->trash_files ? UI_ROLE_DANGER : UI_ROLE_FAINT;
		return UI_ITEM_READY;
	case ROW_HEAD:
		out->text = sc->count ? g->heading : g->none;
		out->ink = sc->count ? UI_ROLE_DIM : UI_ROLE_FAINT;
		if (sc->more) {
			snprintf(s->detail, sizeof(s->detail), "+%u", (unsigned)sc->more);
			out->detail = s->detail;
		}
		return UI_ITEM_READY;
	default: {
		u32 o = i - ROW_FIRST;
		if (o >= sc->count)
			return UI_ITEM_PENDING;
		snprintf(s->row, sizeof(s->row), "%s", saveorphan_path(sc, o));
		if (s->kept[o]) {
			out->ink = UI_ROLE_FAINT;
			snprintf(s->detail, sizeof(s->detail), "%s", g->kept);
		} else {
			char size[16];
			size_text(size, sizeof(size), sc->bytes[o]);
			snprintf(s->detail, sizeof(s->detail), EN_SAVES_SLOTS, (unsigned)sc->slots[o], size);
		}
		out->text = s->row;
		out->detail = s->detail;
		return UI_ITEM_READY;
	}
	}
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

static const struct ui_spec nodes[N_COUNT] = {
	[N_TITLE] = {UI_TITLE, 1, UI_NONE, 3, UI_HFILL, 0, 0, 0, 0, 0, 0, TITLE_H, &title_config},
	[N_HINTS] = {UI_HINTS, 2, UI_NONE, 3, UI_HFILL | UI_BOTTOM, 0, 0, 0, 0, 0, 0, HINTS_H, &no_hints},
	[N_CARD] = {UI_CARD, 3, UI_NONE, 2, UI_HFILL, 0, 0, 0, 0, CARD_Y, 0, CARD_H, &card},
	[N_LIST] = {UI_LIST, 4, UI_NONE, 1, UI_LEFT, UI_FOCUSABLE, 0, 0, LIST_X, LIST_Y, LIST_W, LIST_H, &list_config},
	[N_SCROLL] = {UI_SCROLLBAR, 5, UI_NONE, 2, UI_LEFT, 0, 0, UI_ROLE_PANEL, LIST_X + LIST_W, LIST_Y, 8, LIST_H,
		      &bar},
};

static const struct ui_view_spec view = {"saves", nodes, N_COUNT, N_LIST, UI_VIEW_LR_PAGES, on_event};

/* ---- Showing ---- */

// A does something on the rows of Empty trash and of an orphan, and in the
// picker; the totals and the "Orphaned saves" heading take no key, so their
// hint bar shows B alone
static int row_acts(const struct saves *s, u32 row)
{
	return s->mode == M_PICK || row == ROW_EMPTY || row >= ROW_FIRST;
}

static void show_keys(struct saves *s)
{
	u32 n = 0;
	if (row_acts(s, s->cursor))
		s->keys[n++] = (struct ui_hint){"A", s->mode == M_PICK ? gl_saves->hint_relink : gl_saves->hint_open};
	s->keys[n++] = (struct ui_hint){"B", gl_hint_back};
	ui_hints_set(s->hints, s->keys, n);
	s->keys_acts = (u8)row_acts(s, s->cursor);
}

static void show_title(struct saves *s)
{
	if (s->mode == M_PICK) {
		ui_title_set(s->title, gl_saves->pick_title);
		ui_title_set_right(s->title, "");
		return;
	}
	ui_title_set(s->title, gl_saves->title);
	size_text(s->right, sizeof(s->right), s->scan.saves_bytes + s->scan.backups_bytes + s->scan.trash_bytes);
	ui_title_set_right(s->title, s->right);
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
	memset(s->kept, 0, sizeof(s->kept));
}

// The manager's rows again after a scan
static void rescan(struct saves *s)
{
	scan(s);
	if (s->mode == M_MAIN) {
		u32 rows = main_rows(s);
		s->cursor = s->cursor < rows ? s->cursor : rows - 1;
		ui_list_recount(s->list, s->cursor);
		show_title(s);
	}
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

static void leave_pick(struct saves *s)
{
	s->mode = M_MAIN;
	show_title(s);
	show_keys(s);
	ui_list_reset(s->list, s->main_cursor < main_rows(s) ? s->main_cursor : 0);
	s->cursor = ui_list_cursor(s->list);
}

static void enter_pick(struct saves *s, u32 o)
{
	library_load_now();
	const u8 *slot = library_or_null();
	if (!slot) {
		dialog_message(NULL, gl_saves->no_library, DIALOG_ERROR);
		return;
	}
	u32 total = library_games(slot);
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
	show_title(s);
	show_keys(s);
	ui_list_reset(s->list, 0);
	s->cursor = 0;
	tlog_debug("saves: picker for %s, %lu games, %lu with its name", s->from, (unsigned long)total,
		   (unsigned long)s->pick.nn);
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
	rescan(s);
}

static void orphan_menu(struct saves *s, u32 o)
{
	const struct lang_saves *g = gl_saves;
	snprintf(s->ask, sizeof(s->ask), "%s", saveorphan_path(&s->scan, o));
	s->items[0] = (struct ui_pop_item){g->pop_relink, 0, 0};
	s->items[1] = (struct ui_pop_item){g->pop_keep, 0, 0};
	s->items[2] = (struct ui_pop_item){g->pop_delete, 0, UI_POP_DANGER};
	s->popup = (struct ui_popup){base_of(s->ask), 0, s->items, 3, 0};
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
		if (pick == 1) {
			s->kept[o] = 1;
			ui_popup_close();
			ui_list_refresh(s->list, ROW_FIRST + o, ROW_FIRST + o);
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
	if (r)
		dialog_message(NULL, error_text(r), DIALOG_ERROR);
	rescan(s);
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
	} else if (renamed) {
		char text[96];
		snprintf(text, sizeof(text), EN_SAVES_RENAMED, (unsigned)renamed);
		dialog_message(NULL, text, DIALOG_INFO);
	}
	s->mode = M_MAIN;
	show_keys(s);
	s->main_cursor = 0;
	rescan(s);
	leave_pick(s);
}

static void act(struct saves *s, u32 row)
{
	if (s->mode == M_PICK) {
		pick_choose(s, row);
	} else if (row == ROW_EMPTY) {
		empty_trash(s);
	} else if (row >= ROW_FIRST && row - ROW_FIRST < s->scan.count) {
		orphan_menu(s, row - ROW_FIRST);
	}
}

/* ---- The screen ---- */

void saves_run(void)
{
	struct saves *s = S;
	memset(s, 0, sizeof(*s));
	scan(s);  // over the page that opened it
	ui_leave(UI_TR_DEEPER, 1);
	while (ui_depth())
		ui_pop();
	struct ui_view *v = ui_push(&view, s);
	if (!v) {
		tlog_error("saves: %s", ui_error());
		return;
	}
	s->list = (u8)(v->first + N_LIST);
	s->title = (u8)(v->first + N_TITLE);
	s->hints = (u8)(v->first + N_HINTS);
	show_title(s);
	show_keys(s);
	ui_list_reset(s->list, 0);
	tlog("saves: %lu orphans", (unsigned long)s->scan.count);
	while (ui_depth()) {
		ui_frame();
		if (s->keys_acts != row_acts(s, s->cursor))
			show_keys(s);
		if (s->act) {
			u32 row = s->act - 1;
			s->act = 0;
			act(s, row);
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
