// A game's saves (game_saves.h), on the save page (save_page.h; design:
// docs/design/v0.5/saves-variants.md, direction B). Two pages take turns:
// the slots (the active one marked, New slot last) and a slot's backups.
// A slot's other actions are a popup beside its row (START), New slot's
// choices too; the name keyboard and the .sav file browser take the whole
// screen, so the page closes for them and opens again after.
//
// The state lives in pReadCache (READCACHE_GSAVES) while the page runs: no
// other owner borrows that room meanwhile. What a probe learns before it
// (the game's path) waits in a few bytes of EWRAM.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "game_saves.h"
#include "save_page.h"
#include "slot_flow.h"
#include "slot_list.h"
#include "../actions.h"
#include "../ez_define.h"
#include "../ezkernel.h"
#include "../ff15/ff.h"
#include "../gamesave.h"
#include "../lang.h"
#include "../library.h"
#include "../tlog.h"
#include "../ui/art_ids.h"
#include "../ui/dialog.h"
#include "../userdata.h"

extern FILINFO fileinfo;  // ezkernelnew.c's
extern DIR dir;
extern u8 pReadCache[];

#define BACKUP_ROWS SAVE_PAGE_ROWS
#define FILE_ENTRIES 28
#define FILE_NAME 48  // bytes of a listed name; longer ones are not listed
#define PATH_BYTES 288

// What the probe found: the game it was about, until the page runs
static struct {
	char path[PATH_BYTES];
	u32 size;  // the game's save size; 0 until known
	u16 icon;  // its console's ART_ICON16_*
} probe EWRAM_BSS;

// A slot's popup items
enum {
	ACT_PLAY,
	ACT_ACTIVE,
	ACT_BACKUPS,
	ACT_RENAME,
	ACT_COPY,
	ACT_DELETE,
	ACT_COUNT
};
// New slot's
enum {
	NEW_EMPTY,
	NEW_COPY,
	NEW_FILE,
	NEW_COUNT
};

struct gs_state {
	const char *title;
	char rom[GAMESAVE_PATH_BYTES];
	u32 size;
	char active[SAVESLOT_NAME_BYTES];
	char want[SAVESLOT_NAME_BYTES];  // the slot to put the cursor on next
	char slot[SAVESLOT_NAME_BYTES];  // the slot a popup or the backups are about
	struct slotstore_slot slots[SLOTSTORE_MAX_SLOTS];
	u8 backups[SLOTSTORE_MAX_SLOTS];  // each slot's backups (capped at 255)
	u32 count;  // slots
	struct save_page_row rows[SAVE_PAGE_ROWS];
	u32 versions;
	char head[24];
	u8 new_open;  // New slot's popup is up: the pane's heading, above it, goes
	char values[48];
	struct ui_hint hints[4];
	struct ui_pop_item items[ACT_COUNT];
	u8 ids[ACT_COUNT];
	struct ui_popup popup;
	char copy_of[SAVESLOT_NAME_BYTES + 16];
	char text[320];
	char path[PATH_BYTES];
	union {  // never up together
		struct {  // a slot's backups (also counting them, at the slots' read)
			struct slotstore_version v[BACKUP_ROWS];
			char ago[BACKUP_ROWS][20];
		} b;
		struct {  // the .sav file browser (slot_list.h)
			char names[FILE_ENTRIES][FILE_NAME];
			u8 dirs[FILE_ENTRIES];
			struct slot_list_row rows[FILE_ENTRIES + 1];
			char cwd[PATH_BYTES];
		} f;
	} u;
};
_Static_assert(sizeof(struct gs_state) <= READCACHE_GSAVES_SIZE, "a game's saves fit READCACHE_GSAVES");
#define gs (*(struct gs_state *)(pReadCache + READCACHE_GSAVES))

// snprintf, for strings built in pReadCache: the compiler can't tell that
// the destination and the arguments are different fields of the state
static int __attribute__((format(printf, 3, 4))) gs_snprintf(char *dst, u32 size, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(dst, size, fmt, ap);
	va_end(ap);
	return n;
}

/* ---- Words ---- */

static void size_text(char *out, u32 room, u32 bytes)
{
	if (bytes < 1024)
		SF(out, room, STR_SIZE_B, LN(bytes));
	else
		SF(out, room, STR_SIZE_KB, LN(bytes / 1024));
}

// A unix time's "10-23 18:30", or never/unknown
static void when_text(char *out, u32 room, u32 t, int exists)
{
	if (!exists)
		gs_snprintf(out, room, "%s", S(STR_GSAVES_NEVER));
	else if (!t)
		gs_snprintf(out, room, "%s", S(STR_GSAVES_UNKNOWN));
	else
		save_page_when(out, room, t);
}

static u32 stamp_time(const struct saveslot_stamp *w)
{
	return w->year ? userdata_unix_time(w->year, w->month, w->day, w->hour, w->min, w->sec) : 0;
}

// What a slotstore code says: the flows' lines, and the page's own
static void fail(int code)
{
	tlog_error("gsaves: %d", code);
	switch (code) {
	case SLOTSTORE_LAST:
		dialog_message(NULL, S(STR_GSAVES_LAST), DIALOG_ERROR);
		break;
	case SLOTSTORE_NO_FILE:
		dialog_message(NULL, S(STR_GSAVES_GONE_FILE), DIALOG_ERROR);
		break;
	case SLOTSTORE_NO_VERSION:
		dialog_message(NULL, S(STR_GSAVES_GONE_VERSION), DIALOG_ERROR);
		break;
	case SLOTSTORE_SIZE:
		SF(gs.text, sizeof(gs.text), STR_GSAVES_SIZE, LN(gs.size / 1024));
		dialog_message(NULL, gs.text, DIALOG_ERROR);
		break;
	default:
		dialog_message(NULL, slot_flow_error_text(code), DIALOG_ERROR);
		break;
	}
}

/* ---- The slots ---- */

// The game's slots and the active one, read again (with backups, each
// slot's backups counted too: a folder read each), and the rows for them and
// New slot. Returns the row for gs.want (else the active slot), or a
// negative code.
static int slots_read(int backups)
{
	int n = slotstore_list(gs.rom, gs.slots, SLOTSTORE_MAX_SLOTS);
	if (n < 0)
		return n;
	gs.count = (u32)n < SLOTSTORE_MAX_SLOTS ? (u32)n : SLOTSTORE_MAX_SLOTS;
	slotstore_current(gs.rom, gs.active);
	u32 at = 0;
	for (u32 i = 0; i < gs.count; i++) {
		if (backups) {
			int b = slotstore_history(gs.rom, gs.slots[i].name, gs.u.b.v, 0);
			gs.backups[i] = (u8)(b < 0 ? 0 : b > 255 ? 255 : b);
		}
		int act = !strcmp(gs.slots[i].name, gs.active);
		gs.rows[i] = (struct save_page_row){gs.slots[i].name, (u8)(act ? ART_ICON8_SAVE + 1 : 0), 0};
		if (gs.want[0] ? !strcmp(gs.slots[i].name, gs.want) : act)
			at = i;
	}
	gs.rows[gs.count] = (struct save_page_row){S(STR_GSAVES_NEW_ROW), 0, UI_ROLE_ACCENT_INK};
	return (int)at;
}

static void slot_pane(void *ctx, u32 row, struct save_pane *out)
{
	(void)ctx;
	if (row >= gs.count) {  // New slot
		gs_snprintf(gs.head, sizeof(gs.head), "%s", gs.new_open ? "" : S(STR_SLOTS_NEW_TITLE));
		out->head = gs.head;
		out->keys = "";
		out->note = S(STR_GSAVES_NOTE_NEW);
		return;
	}
	const struct slotstore_slot *s = &gs.slots[row];
	int act = !strcmp(s->name, gs.active);
	char when[20], backups[8];
	save_page_ago(when, sizeof(when), s->saved);
	if (gs.backups[row])
		SF(backups, sizeof(backups), STR_GSAVES_BACKUPS, LN(gs.backups[row]));
	else
		gs_snprintf(backups, sizeof(backups), "%s", S(STR_GSAVES_NONE));
	gs_snprintf(gs.values, sizeof(gs.values), "%s\n%s", when, backups);
	out->head = s->name;
	out->tag = act ? S(STR_GSAVES_ACTIVE) : NULL;
	out->values = gs.values;
	out->note = act ? S(STR_GSAVES_NOTE_ACTIVE) : S(STR_GSAVES_NOTE_OTHER);
}

static void slot_hints(void *ctx, u32 row, const struct ui_hint **keys, u32 *count)
{
	(void)ctx;
	u32 n = 0;
	if (row >= gs.count) {
		gs.hints[n++] = (struct ui_hint){"A", S(STR_GSAVES_HINT_NEW)};
	} else {
		gs.hints[n++] = (struct ui_hint){"A", S(STR_GSAVES_HINT_PLAY)};
		gs.hints[n++] = (struct ui_hint){"START", S(STR_GSAVES_HINT_MENU)};
		gs.hints[n++] = (struct ui_hint){"\xC2\xBB", S(STR_GSAVES_HINT_BACKUPS)};
	}
	gs.hints[n++] = (struct ui_hint){"B", S(STR_HINT_BACK)};
	*keys = gs.hints;
	*count = n;
}

static struct save_page slots_page;

// The slots' page, its cursor on gs.want (else the active slot); 0 if it
// can't show
static int slots_open(void)
{
	int at = slots_read(1);
	if (at < 0) {
		fail(at);
		return 0;
	}
	slots_page = (struct save_page){"game-saves",
					probe.icon,
					S(STR_GSAVES_TITLE),
					gs.title,
					S(STR_GSAVES_KEYS),
					gs.rows,
					gs.count + 1,
					(u32)at,
					slot_pane,
					slot_hints,
					0,
					INPUT_START | INPUT_RIGHT};
	if (save_page_open(&slots_page))
		return 1;
	dialog_message(NULL, S(STR_GSAVES_NO_ROOM), DIALOG_ERROR);  // logged: the tiles it lacked
	return 0;
}

// The rows again after a change the page shows: the active slot (backups
// as they were), a delete (counted again)
static void slots_again(int backups)
{
	int at = slots_read(backups);
	if (at < 0) {
		fail(at);
		return;
	}
	save_page_rows(gs.rows, gs.count + 1, (u32)at);
}

/* ---- Backups ---- */

static int backups_read(void)
{
	int n = slotstore_history(gs.rom, gs.slot, gs.u.b.v, BACKUP_ROWS);
	if (n < 0)
		return n;
	gs.versions = (u32)n < BACKUP_ROWS ? (u32)n : BACKUP_ROWS;
	for (u32 i = 0; i < gs.versions; i++) {
		// When it was made, else (no clock then) its number
		u32 t = stamp_time(&gs.u.b.v[i].when);
		if (t)
			when_text(gs.u.b.ago[i], sizeof(gs.u.b.ago[i]), t, 1);
		else
			SF(gs.u.b.ago[i], sizeof(gs.u.b.ago[i]), STR_GSAVES_BACKUP_N, LN(gs.u.b.v[i].n));
		gs.rows[i] = (struct save_page_row){gs.u.b.ago[i], 0, 0};
	}
	return n;
}

static void backup_pane(void *ctx, u32 row, struct save_pane *out)
{
	(void)ctx;
	if (row >= gs.versions)
		return;
	const struct slotstore_version *v = &gs.u.b.v[row];
	char when[20], size[12];
	save_page_ago(when, sizeof(when), stamp_time(&v->when));
	size_text(size, sizeof(size), v->size);
	gs_snprintf(gs.values, sizeof(gs.values), "%s\n%s", when, size);
	out->head = gs.u.b.ago[row];
	out->tag = v->kept ? S(STR_GSAVES_KEPT) : NULL;
	out->keys = S(STR_GSAVES_BACKUP_KEYS);
	out->values = gs.values;
	out->note = v->kept ? S(STR_GSAVES_NOTE_KEPT) : S(STR_GSAVES_NOTE_BACKUP);
}

static void backup_hints(void *ctx, u32 row, const struct ui_hint **keys, u32 *count)
{
	(void)ctx;
	(void)row;
	gs.hints[0] = (struct ui_hint){"A", S(STR_GSAVES_HINT_RESTORE)};
	gs.hints[1] = (struct ui_hint){"B", S(STR_HINT_BACK)};
	*keys = gs.hints;
	*count = 2;
}

static struct save_page backups_page;

// A slot's backups, until B or LEFT goes back to the slots
static void backups(void)
{
	int n = backups_read();
	if (n <= 0) {
		if (n < 0)
			fail(n);
		return;
	}
	backups_page = (struct save_page){"game-saves-backups",
					  probe.icon,
					  S(STR_GSAVES_BACKUPS_TITLE),
					  gs.slot,
					  S(STR_GSAVES_BACKUP_KEYS),
					  gs.rows,
					  gs.versions,
					  0,
					  backup_pane,
					  backup_hints,
					  0,
					  INPUT_LEFT};
	if (!save_page_open(&backups_page))
		return;
	for (;;) {
		u32 row;
		u16 key = save_page_wait(&row);
		if (key != INPUT_A || row >= gs.versions)
			break;
		u32 ver = gs.u.b.v[row].n;
		char when[24];
		when_text(when, sizeof(when), stamp_time(&gs.u.b.v[row].when), 1);
		SF(gs.text, sizeof(gs.text), STR_GSAVES_RESTORE, LS(gs.slot), LS(when));
		struct ui_confirm ask = {S(STR_GSAVES_RESTORE_TITLE), gs.text, S(STR_GSAVES_RESTORE_BUTTON), 0};
		if (!ui_confirm(save_page_hints(), &ask))
			continue;
		int r = slotstore_restore(gs.rom, gs.slot, ver);
		tlog("gsaves: restore %s version %lu: %d", gs.slot, (unsigned long)ver, r);
		if (r != SLOTSTORE_OK)
			fail(r);
		n = backups_read();
		if (n <= 0)
			break;
		save_page_rows(gs.rows, gs.versions, 0);
	}
	save_page_close();
	gs_snprintf(gs.want, sizeof(gs.want), "%s", gs.slot);
}

/* ---- New slot from a .sav file: a minimal file browser ---- */

static int is_sav(const char *name)
{
	u32 n = (u32)strlen(name);
	return n > 4 && (name[n - 4] == '.') && (name[n - 3] | 32) == 's' && (name[n - 2] | 32) == 'a' &&
	       (name[n - 1] | 32) == 'v';
}

// Lists a folder (folders, then .sav files) into the table; returns the
// count, or -1 if it can't be read
static int dir_list(const char *folder)
{
	u32 n = 0;
	for (int pass = 0; pass < 2; pass++) {
		if (f_opendir(&dir, folder) != FR_OK)
			return -1;
		while (n < FILE_ENTRIES && f_readdir(&dir, &fileinfo) == FR_OK && fileinfo.fname[0]) {
			int isdir = (fileinfo.fattrib & AM_DIR) != 0;
			if (isdir != (pass == 0) || fileinfo.fname[0] == '.' ||
			    (fileinfo.fattrib & (AM_HID | AM_SYS)) || strlen(fileinfo.fname) >= FILE_NAME ||
			    (!isdir && !is_sav(fileinfo.fname)))
				continue;
			gs_snprintf(gs.u.f.names[n], FILE_NAME, "%.*s", FILE_NAME - 1, fileinfo.fname);
			gs.u.f.dirs[n] = (u8)isdir;
			n++;
		}
		f_closedir(&dir);
	}
	return (int)n;
}

// out = base + "/" + name (out may be base); the caller checked the room
static void join(char *out, const char *base, const char *name)
{
	u32 len = (u32)strlen(base), n = (u32)strlen(name);
	if (len + n + 2 > PATH_BYTES)
		n = PATH_BYTES - len - 2;
	if (out != base)
		memcpy(out, base, len);
	if (len > 1)
		out[len++] = '/';
	memcpy(out + len, name, n);
	out[len + n] = 0;
}

// Picks a .sav on the card, starting in the ROM's folder; its path goes to
// out (PATH_BYTES). 1 if one was picked.
static int pick_file(char *out)
{
	char *cwd = gs.u.f.cwd;
	const char *rom = gs.rom[0] == '/' ? gs.rom + 1 : gs.rom;
	const char *slash = strrchr(rom, '/');
	gs_snprintf(cwd, PATH_BYTES, "/%.*s", slash ? (int)(slash - rom) : 0, rom);
	char left[FILE_NAME];
	left[0] = 0;
	const struct ui_hint hints[2] = {{"A", S(STR_HINT_OPEN)}, {"B", S(STR_GSAVES_HINT_CANCEL)}};
	for (;;) {
		int n = dir_list(cwd);
		if (n < 0 && strcmp(cwd, "/") != 0) {
			gs_snprintf(cwd, PATH_BYTES, "/");
			continue;
		}
		if (n < 0) {
			fail(SLOTSTORE_CARD);
			return 0;
		}
		u32 up = strcmp(cwd, "/") != 0, count = 0, at = 0;
		if (up)
			gs.u.f.rows[count++] = (struct slot_list_row){"..", 0, ART_ICON8_FOLDER + 1};
		for (int i = 0; i < n && count < FILE_ENTRIES + 1; i++, count++) {
			gs.u.f.rows[count] = (struct slot_list_row){
				gs.u.f.names[i], 0, (u8)((gs.u.f.dirs[i] ? ART_ICON8_FOLDER : ART_ICON8_FILE) + 1)};
			if (left[0] && !strcmp(left, gs.u.f.names[i]))
				at = count;
		}
		if (count == 0)
			gs.u.f.rows[count++] = (struct slot_list_row){S(STR_GSAVES_EMPTY), 0, 0};
		const struct slot_list list = {
			"game-saves-files", S(STR_GSAVES_FILE_TITLE), cwd, gs.u.f.rows, count, at, hints, 2, 0};
		u32 row;
		if (slot_list_run(&list, &row) != SLOT_LIST_PICK)
			return 0;
		left[0] = 0;
		if (up && row == 0) {  // up: the folder left is where the cursor lands
			char *s = strrchr(cwd, '/');
			gs_snprintf(left, sizeof(left), "%s", s + 1);
			if (s == cwd)
				s[1] = 0;
			else
				*s = 0;
			continue;
		}
		u32 i = row - up;
		if (i >= (u32)n)
			continue;  // the empty row
		if (strlen(cwd) + FILE_NAME + 2 >= PATH_BYTES) {  // too deep to build a path
			fail(SLOTSTORE_PATH);
			continue;
		}
		if (gs.u.f.dirs[i]) {
			join(cwd, cwd, gs.u.f.names[i]);
			continue;
		}
		join(out, cwd, gs.u.f.names[i]);
		return 1;
	}
}

enum {
	DONE_STAY,    // the page is up as it was
	DONE_REOPEN,  // the page closed: open it again
	DONE_PLAY,    // play the slot (it is the active one now)
};

// Slot row's backups, if it has any (else a word over the page, which stays)
static int open_backups(u32 row)
{
	if (!gs.backups[row]) {
		dialog_message(NULL, S(STR_GSAVES_NO_BACKUPS), DIALOG_INFO);
		return DONE_STAY;
	}
	gs_snprintf(gs.slot, sizeof(gs.slot), "%s", gs.slots[row].name);
	save_page_close();
	backups();
	return DONE_REOPEN;
}

/* ---- New slot ---- */

// New slot's choice (a popup beside its row), then its name, for which the
// page closes (DONE_REOPEN); DONE_STAY if nothing was chosen
static int new_slot(void)
{
	if (gs.count >= SLOTSTORE_MAX_SLOTS) {
		fail(SLOTSTORE_FULL);
		return DONE_STAY;
	}
	// A copy needs a slot with a file: the active one, else the first
	const char *from = gs.count ? gs.slots[0].name : NULL;
	for (u32 i = 0; i < gs.count; i++)
		if (!strcmp(gs.slots[i].name, gs.active))
			from = gs.slots[i].name;
	u32 n = 0;
	gs.items[n] = (struct ui_pop_item){S(STR_GSAVES_NEW_EMPTY), 0, 0};
	gs.ids[n++] = NEW_EMPTY;
	if (from) {
		SF(gs.copy_of, sizeof(gs.copy_of), STR_GSAVES_COPY_OF, LS(from));
		gs.items[n] = (struct ui_pop_item){gs.copy_of, 0, 0};
		gs.ids[n++] = NEW_COPY;
	}
	// A .sav file is a GBA game's save: a GB, GBC or NES slot is in its
	// emulator's format instead
	if (probe.icon == ART_ICON16_GBA || probe.icon == ART_ICON16_NOR) {
		gs.items[n] = (struct ui_pop_item){S(STR_GSAVES_NEW_FILE), 0, 0};
		gs.ids[n++] = NEW_FILE;
	}
	gs.popup = (struct ui_popup){S(STR_SLOTS_NEW_TITLE), ART_ICON8_SAVE + 1, gs.items, (u8)n, 0, 0};
	gs.new_open = 1;
	save_page_refresh();
	if (!ui_popup_open(save_page_list(), save_page_hints(), &gs.popup)) {
		gs.new_open = 0;
		save_page_refresh();
		return DONE_STAY;
	}
	tlog_debug("screen ready: game-saves-new");
	s32 pick = ui_popup_choose();
	ui_popup_close();
	gs.new_open = 0;
	save_page_refresh();
	if (pick < 0)
		return DONE_STAY;
	u32 how = gs.ids[pick];
	char copy[SAVESLOT_NAME_BYTES], name[SAVESLOT_NAME_BYTES], initial[SAVESLOT_NAME_BYTES];
	initial[0] = 0;
	if (from)
		gs_snprintf(copy, sizeof(copy), "%s", from);
	save_page_close();
	if (how == NEW_FILE) {
		if (!pick_file(gs.path))
			return DONE_REOPEN;
		// The file's name, less .sav, as the slot's first guess
		const char *base = strrchr(gs.path, '/') + 1;
		gs_snprintf(initial, sizeof(initial), "%.*s", (int)(strlen(base) - 4), base);
	}
	if (slot_flow_name_prompt(gs.rom, how == NEW_COPY ? S(STR_SLOTS_COPY_TITLE) : S(STR_SLOTS_NEW_TITLE), initial,
				  NULL, name) != SLOT_FLOW_DONE)
		return DONE_REOPEN;
	dialog_notice(S(STR_GSAVES_TITLE), S(STR_GSAVES_COPYING), DIALOG_INFO);  // a copy takes a few seconds
	int r = how == NEW_FILE   ? slotstore_create_file(gs.rom, name, gs.path, gs.size, gs.want)
		: how == NEW_COPY ? slotstore_create(gs.rom, name, copy, 0, gs.want)
				  : slotstore_create(gs.rom, name, NULL, gs.size, gs.want);
	dialog_notice_end();
	tlog("gsaves: new %s (%lu): %d", name, (unsigned long)how, r);
	if (r != SLOTSTORE_OK) {
		gs.want[0] = 0;
		fail(r);
	}
	return DONE_REOPEN;
}

/* ---- A slot's popup ---- */

static int make_active(const char *slot)
{
	int r = slotstore_set_current(gs.rom, slot);
	tlog("gsaves: active %s: %d", slot, r);
	if (r != SLOTSTORE_OK)
		fail(r);
	return r == SLOTSTORE_OK;
}

static int slot_menu(u32 row)
{
	gs_snprintf(gs.slot, sizeof(gs.slot), "%s", gs.slots[row].name);
	int act = !strcmp(gs.slot, gs.active);
	static const u8 order[ACT_COUNT] = {ACT_PLAY, ACT_ACTIVE, ACT_BACKUPS, ACT_RENAME, ACT_COPY, ACT_DELETE};
	const char *const text[ACT_COUNT] = {S(STR_GSAVES_ACT_PLAY),    S(STR_GSAVES_ACT_ACTIVE),
					     S(STR_GSAVES_ACT_BACKUPS), S(STR_GSAVES_ACT_RENAME),
					     S(STR_GSAVES_ACT_COPY),    S(STR_GSAVES_ACT_DELETE)};
	char count[8];
	SF(count, sizeof(count), STR_GSAVES_BACKUPS, LN(gs.backups[row]));
	u32 n = 0;
	for (u32 k = 0; k < ACT_COUNT; k++) {
		u32 a = order[k];
		if (a == ACT_ACTIVE && act)
			continue;
		gs.items[n] = (struct ui_pop_item){text[a], a == ACT_BACKUPS && gs.backups[row] ? count : 0,
						   (u8)(a == ACT_DELETE ? UI_POP_DANGER : 0)};
		gs.ids[n++] = (u8)a;
	}
	gs.popup = (struct ui_popup){gs.slot, ART_ICON8_SAVE + 1, gs.items, (u8)n, 0, 0};
	if (!ui_popup_open(save_page_list(), save_page_hints(), &gs.popup))
		return DONE_STAY;
	tlog_debug("screen ready: game-saves-slot %s", gs.slot);
	for (;;) {
		s32 pick = ui_popup_choose();
		if (pick < 0)
			return DONE_STAY;
		u32 a = gs.ids[pick];
		if (a == ACT_DELETE) {
			if (gs.count < 2) {
				ui_popup_close();
				fail(SLOTSTORE_LAST);
				return DONE_STAY;
			}
			SF(gs.text, sizeof(gs.text), STR_GSAVES_DELETE_ASK, LS(gs.slot));
			struct ui_confirm ask = {gs.text, S(STR_GSAVES_DELETE_TEXT), S(STR_GSAVES_ACT_DELETE), 1};
			if (!ui_confirm(save_page_hints(), &ask))
				continue;  // Cancel: back to the popup
			ui_popup_close();
			int r = slotstore_delete(gs.rom, gs.slot);
			tlog("gsaves: delete %s: %d", gs.slot, r);
			if (r != SLOTSTORE_OK)
				fail(r);
			gs.want[0] = 0;
			slots_again(1);
			return DONE_STAY;
		}
		ui_popup_close();
		char out[SAVESLOT_NAME_BYTES];
		switch (a) {
		case ACT_PLAY:
			return make_active(gs.slot) ? DONE_PLAY : DONE_STAY;
		case ACT_ACTIVE:
			if (make_active(gs.slot)) {
				gs_snprintf(gs.want, sizeof(gs.want), "%s", gs.slot);
				slots_again(0);
			}
			return DONE_STAY;
		case ACT_BACKUPS:
			return open_backups(row);
		case ACT_RENAME:
			save_page_close();
			if (slot_flow_rename(gs.rom, gs.slot, out) == SLOT_FLOW_DONE)
				gs_snprintf(gs.want, sizeof(gs.want), "%s", out);
			else
				gs_snprintf(gs.want, sizeof(gs.want), "%s", gs.slot);
			return DONE_REOPEN;
		case ACT_COPY: {
			save_page_close();
			gs_snprintf(gs.want, sizeof(gs.want), "%s", gs.slot);
			if (slot_flow_name_prompt(gs.rom, S(STR_SLOTS_COPY_TITLE), "", NULL, out) != SLOT_FLOW_DONE)
				return DONE_REOPEN;
			dialog_notice(S(STR_GSAVES_TITLE), S(STR_GSAVES_COPYING), DIALOG_INFO);
			int r = slotstore_duplicate(gs.rom, gs.slot, out, gs.want);
			dialog_notice_end();
			tlog("gsaves: copy %s as %s: %d", gs.slot, out, r);
			if (r != SLOTSTORE_OK) {
				fail(r);
				gs_snprintf(gs.want, sizeof(gs.want), "%s", gs.slot);
			}
			return DONE_REOPEN;
		}
		default:
			return DONE_STAY;
		}
	}
}

/* ---- The game's saves ---- */

int game_saves_probe(const char *path)
{
	// No card access for a ROM (the popup opens on START): its facts are
	// read once Saves is picked. A NOR entry's are in NOR.
	probe.size = 0;
	probe.path[0] = 0;
	if (strlen(path) >= sizeof(probe.path))
		return 0;
	if (userdata_nor_name(path)) {
		char rom[GAMESAVE_PATH_BYTES];
		probe.icon = ART_ICON16_NOR;
		if (!action_game_saves(path, rom, sizeof(rom), &probe.size))
			return 0;
	} else {
		// A GB, GBC or NES game's slots hold its emulator's 64 KB of cart
		// SRAM (the game's save, the emulator's settings and save states)
		static const u16 icons[] = {[LIB_GBA] = ART_ICON16_GBA,
					    [LIB_GBC] = ART_ICON16_GBC,
					    [LIB_GB] = ART_ICON16_GB,
					    [LIB_NES] = ART_ICON16_NES};
		const char *name = strrchr(path, '/');
		u32 console = name ? library_console_of(name + 1) : LIB_CONSOLE_NONE;
		if (console >= sizeof(icons) / sizeof(icons[0]) || !icons[console])
			return 0;
		probe.icon = icons[console];
	}
	memcpy(probe.path, path, strlen(path) + 1);
	return 1;
}

int game_saves_run(const char *title)
{
	int own = !ui_active();
	if (own)
		dialog_open();
	int result = GAME_SAVES_BACK;
	// A save still on the chip goes to the card before any slot moves
	if (!pending_save_flush())
		goto out;
	memset(&gs, 0, sizeof(gs));
	gs.title = title;
	gs.size = probe.size;
	if (!probe.path[0] || !action_game_saves(probe.path, gs.rom, sizeof(gs.rom), &gs.size)) {
		dialog_message(NULL, S(STR_GSAVES_NO_SAVE), DIALOG_INFO);
		goto out;
	}
	for (;;) {
		if (!slots_open())
			break;
		int again = 0;
		while (!again) {
			u32 row;
			u16 key = save_page_wait(&row);
			if (!key || key == INPUT_B) {
				save_page_close();
				goto out;
			}
			if (row >= gs.count) {  // New slot
				if (key == INPUT_A)
					again = new_slot() == DONE_REOPEN;
				continue;
			}
			gs_snprintf(gs.slot, sizeof(gs.slot), "%s", gs.slots[row].name);
			if (key == INPUT_A) {
				if (make_active(gs.slot)) {
					result = GAME_SAVES_LAUNCH;
					save_page_close();
					goto out;
				}
				continue;
			}
			if (key == INPUT_RIGHT) {
				again = open_backups(row) == DONE_REOPEN;
				continue;
			}
			int done = slot_menu(row);
			if (done == DONE_PLAY) {
				result = GAME_SAVES_LAUNCH;
				save_page_close();
				goto out;
			}
			again = done == DONE_REOPEN;
		}
	}
out:
	if (own)
		dialog_close();
	return result;
}
