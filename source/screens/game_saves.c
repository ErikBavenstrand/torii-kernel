// The per-game save manager (game_saves.h).
#include <stdio.h>
#include <string.h>

#include "game_saves.h"
#include "slot_flow.h"
#include "slot_list.h"
#include "../actions.h"
#include "../ff15/ff.h"
#include "../gamesave.h"
#include "../ezkernel.h"
#include "../lang.h"
#include "../library.h"
#include "../tlog.h"
#include "../ui/art_ids.h"
#include "../ui/dialog.h"
#include "../userdata.h"

extern FILINFO fileinfo;  // ezkernelnew.c's
extern DIR dir;

#define EXPORT_DIR "/torii/export"
#define HIST_ROWS SLOT_LIST_MAX
#define FILE_ENTRIES 28
#define FILE_NAME 48  // bytes of a listed name; longer ones are not listed
#define PATH_BYTES 288

enum {
	ACT_LAUNCH,
	ACT_SET,
	ACT_RENAME,
	ACT_DUP,
	ACT_DELETE,
	ACT_HISTORY,
	ACT_EXPORT,
	ACT_COUNT
};

static struct {
	const char *title;
	char rom[GAMESAVE_PATH_BYTES];
	u32 size;  // the game's save size
	char current[SAVESLOT_NAME_BYTES];
	char want[SAVESLOT_NAME_BYTES];  // the slot to put the cursor on next
	char slot[SAVESLOT_NAME_BYTES];  // the slot the action list is about
	char detail[SLOTSTORE_MAX_SLOTS][12];
	struct slot_list_row rows[SLOTSTORE_MAX_SLOTS + 2];
	struct ui_hint hints[3];
	char text[320];
	char path[PATH_BYTES];
	union {  // never up together
		struct {
			struct slotstore_version v[HIST_ROWS];
			char text[HIST_ROWS][32];
			char size[HIST_ROWS][12];
			struct slot_list_row rows[HIST_ROWS];
		} h;
		struct {
			char name[FILE_ENTRIES][FILE_NAME];
			char size[FILE_ENTRIES][12];
			u8 dir[FILE_ENTRIES];
			struct slot_list_row rows[FILE_ENTRIES + 1];
			char dirpath[PATH_BYTES];
		} f;
	} u;
} gs EWRAM_BSS;

static void size_text(char *out, u32 bytes)
{
	if (bytes < 1024)
		snprintf(out, 12, "%lu B", (unsigned long)bytes);
	else
		snprintf(out, 12, "%lu KB", (unsigned long)bytes / 1024);
}

// What a slotstore code says: the flows' lines, and the manager's own
static void fail(int code)
{
	tlog_error("gsaves: %d", code);
	switch (code) {
	case SLOTSTORE_LAST:
		dialog_message(NULL, gl_gsaves->last, DIALOG_ERROR);
		break;
	case SLOTSTORE_NO_FILE:
		dialog_message(NULL, gl_gsaves->gone_file, DIALOG_ERROR);
		break;
	case SLOTSTORE_NO_VERSION:
		dialog_message(NULL, gl_gsaves->gone_version, DIALOG_ERROR);
		break;
	case SLOTSTORE_SIZE:
		snprintf(gs.text, sizeof(gs.text), EN_GS_SIZE, (unsigned long)gs.size / 1024);
		dialog_message(NULL, gs.text, DIALOG_ERROR);
		break;
	default:
		dialog_message(NULL, slot_flow_error_text(code), DIALOG_ERROR);
		break;
	}
}

static void hints_set(const char *a, const char *b)
{
	gs.hints[0] = (struct ui_hint){"A", a};
	gs.hints[1] = (struct ui_hint){"B", b};
}

/* ---- History ---- */

static void stamp_text(char *out, u32 n, const struct saveslot_stamp *w, u32 room)
{
	if (!w->year)
		snprintf(out, room, "#%lu  %s", (unsigned long)n, gl_gsaves->unknown);
	else
		snprintf(out, room, "#%lu  %04u-%02u-%02u %02u:%02u", (unsigned long)n, w->year, w->month, w->day,
			 w->hour, w->min);
}

static void history(const char *slot)
{
	u32 at = 0;
	for (;;) {
		int n = slotstore_history(gs.rom, slot, gs.u.h.v, HIST_ROWS);
		if (n < 0) {
			fail(n);
			return;
		}
		if (n > HIST_ROWS)
			n = HIST_ROWS;
		if (n == 0) {
			dialog_message(NULL, gl_gsaves->no_history, DIALOG_INFO);
			return;
		}
		for (int i = 0; i < n; i++) {
			stamp_text(gs.u.h.text[i], gs.u.h.v[i].n, &gs.u.h.v[i].when, sizeof(gs.u.h.text[i]));
			size_text(gs.u.h.size[i], gs.u.h.v[i].size);
			gs.u.h.rows[i] = (struct slot_list_row){gs.u.h.text[i], gs.u.h.size[i], 0};
		}
		hints_set(gl_gsaves->hint_restore, gl_hint_back);
		const struct slot_list list = {"game-saves-history",
					       gl_gsaves->history_title,
					       slot,
					       gs.u.h.rows,
					       (u32)n,
					       at < (u32)n ? at : 0,
					       gs.hints,
					       2,
					       0};
		u32 row;
		if (slot_list_run(&list, &row) != SLOT_LIST_PICK)
			return;
		u32 ver = gs.u.h.v[row].n;
		char date[40];
		stamp_text(date, ver, &gs.u.h.v[row].when, sizeof(date));
		snprintf(gs.text, sizeof(gs.text), EN_GS_RESTORE, (unsigned long)ver);
		char body[96];
		snprintf(body, sizeof(body), "%s\n%s", date, gl_gsaves->restore_text);
		if (!dialog_confirm(gs.text, body, DIALOG_INFO)) {
			at = row;
			continue;
		}
		int r = slotstore_restore(gs.rom, slot, ver);
		tlog("gsaves: restore %s version %lu: %d", slot, (unsigned long)ver, r);
		if (r != SLOTSTORE_OK)
			fail(r);
		at = 0;
	}
}

/* ---- Export ---- */

static void export_slot(const char *slot)
{
	f_mkdir("/torii");
	f_mkdir(EXPORT_DIR);
	const char *base = strrchr(gs.rom, '/');
	base = base ? base + 1 : gs.rom;
	u32 stem = (u32)strlen(base);
	const char *dot = strrchr(base, '.');
	if (dot && dot != base)
		stem = (u32)(dot - base);
	int r = SLOTSTORE_TAKEN;
	for (u32 k = 1; k <= 9 && r == SLOTSTORE_TAKEN; k++) {
		if (k == 1)
			snprintf(gs.path, sizeof(gs.path), EXPORT_DIR "/%.*s - %s.sav", (int)stem, base, slot);
		else
			snprintf(gs.path, sizeof(gs.path), EXPORT_DIR "/%.*s - %s (%lu).sav", (int)stem, base, slot,
				 (unsigned long)k);
		r = slotstore_export(gs.rom, slot, gs.path);
	}
	tlog("gsaves: export %s to %s: %d", slot, gs.path, r);
	if (r != SLOTSTORE_OK) {
		fail(r);
		return;
	}
	snprintf(gs.text, sizeof(gs.text), "%s\n%s", gl_gsaves->exported, gs.path);
	dialog_message(NULL, gs.text, DIALOG_INFO);
}

/* ---- Import: a minimal file picker ---- */

static int is_sav(const char *name)
{
	u32 n = (u32)strlen(name);
	return n > 4 && (name[n - 4] == '.') && (name[n - 3] | 32) == 's' && (name[n - 2] | 32) == 'a' &&
	       (name[n - 1] | 32) == 'v';
}

// Lists dir (folders, then .sav files) into the table; returns the count, or
// -1 if the folder can't be read
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
			snprintf(gs.u.f.name[n], FILE_NAME, "%.*s", FILE_NAME - 1, fileinfo.fname);
			gs.u.f.dir[n] = (u8)isdir;
			if (isdir)
				gs.u.f.size[n][0] = 0;
			else
				size_text(gs.u.f.size[n], (u32)fileinfo.fsize);
			n++;
		}
		f_closedir(&dir);
	}
	return (int)n;
}

// out = dir + "/" + name (out may be dir); the caller checked the room
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

// Picks a .sav on the card, starting in the ROM's folder; the path goes to
// out (PATH_BYTES). 1 if one was picked.
static int pick_file(char *out)
{
	char *cwd = gs.u.f.dirpath;
	// The game's folder, else the root
	const char *rom = gs.rom[0] == '/' ? gs.rom + 1 : gs.rom;
	const char *slash = strrchr(rom, '/');
	snprintf(cwd, PATH_BYTES, "/%.*s", slash ? (int)(slash - rom) : 0, rom);
	char left[FILE_NAME];
	left[0] = 0;
	for (;;) {
		int n = dir_list(cwd);
		if (n < 0 && strcmp(cwd, "/") != 0) {
			snprintf(cwd, PATH_BYTES, "/");
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
			gs.u.f.rows[count] =
				(struct slot_list_row){gs.u.f.name[i], gs.u.f.size[i],
						       (u8)((gs.u.f.dir[i] ? ART_ICON8_FOLDER : ART_ICON8_FILE) + 1)};
			if (left[0] && !strcmp(left, gs.u.f.name[i]))
				at = count;
		}
		if (count == 0)
			gs.u.f.rows[count++] = (struct slot_list_row){gl_gsaves->empty, 0, 0};
		hints_set(gl_hint_open, gl_slots->hint_cancel);
		const struct slot_list list = {
			"game-saves-files", gl_gsaves->import_title, cwd, gs.u.f.rows, count, at, gs.hints, 2, 0};
		u32 row;
		if (slot_list_run(&list, &row) != SLOT_LIST_PICK)
			return 0;
		left[0] = 0;
		if (up && row == 0) {  // up: the folder we left is where the cursor lands
			char *s = strrchr(cwd, '/');
			snprintf(left, sizeof(left), "%s", s + 1);
			if (s == cwd)
				s[1] = 0;
			else
				*s = 0;
			continue;
		}
		u32 i = row - up;
		if (i >= (u32)n)
			continue;  // the empty row
		u32 len = (u32)strlen(cwd);
		if (len + FILE_NAME + 2 >= PATH_BYTES) {  // too deep to build a path
			fail(SLOTSTORE_PATH);
			continue;
		}
		if (gs.u.f.dir[i]) {
			join(cwd, cwd, gs.u.f.name[i]);
			continue;
		}
		join(out, cwd, gs.u.f.name[i]);
		return 1;
	}
}

static int slots_get(const struct slotstore_slot **slots)
{
	int n = slot_flow_slots(gs.rom, slots);
	if (n < 0)
		fail(n);
	return n;
}

static void import_file(void)
{
	const struct slotstore_slot *slots;
	int n = slots_get(&slots);
	if (n < 0)
		return;
	if (n == 0) {
		dialog_message(NULL, gl_gsaves->no_slots, DIALOG_INFO);
		return;
	}
	if (!pick_file(gs.path))
		return;
	u32 at = 0;
	slotstore_current(gs.rom, gs.current);
	for (int i = 0; i < n; i++)
		if (!strcmp(slots[i].name, gs.current))
			at = (u32)i;
	if (n > 1) {
		for (int i = 0; i < n; i++) {
			size_text(gs.detail[i], slots[i].size);
			gs.rows[i] = (struct slot_list_row){slots[i].name, gs.detail[i], 0};
		}
		hints_set(gl_hint_ok, gl_slots->hint_cancel);
		const struct slot_list list = {
			"game-saves-target", gl_gsaves->into_title, gs.title, gs.rows, (u32)n, at, gs.hints, 2, 0};
		u32 row;
		if (slot_list_run(&list, &row) != SLOT_LIST_PICK)
			return;
		at = row;
	}
	char slot[SAVESLOT_NAME_BYTES];
	snprintf(slot, sizeof(slot), "%s", slots[at].name);
	const char *file = strrchr(gs.path, '/') + 1;
	snprintf(gs.text, sizeof(gs.text), EN_GS_IMPORT, slot);
	char body[160];
	snprintf(body, sizeof(body), "%s\n%s", file, gl_gsaves->import_text);
	if (!dialog_confirm(gs.text, body, DIALOG_INFO))
		return;
	int r = slotstore_import(gs.rom, slot, gs.path, gs.size);
	tlog("gsaves: import %s into %s: %d", gs.path, slot, r);
	if (r != SLOTSTORE_OK)
		fail(r);
	else
		snprintf(gs.want, sizeof(gs.want), "%s", slot);
}

/* ---- A slot's actions ---- */

// 1 if the game launches (the slot is the current one now)
static int slot_actions(const char *name)
{
	snprintf(gs.slot, sizeof(gs.slot), "%s", name);
	const char *const text[ACT_COUNT] = {gl_gsaves->act_launch, gl_gsaves->act_set,    gl_gsaves->act_rename,
					     gl_gsaves->act_dup,    gl_gsaves->act_delete, gl_gsaves->act_history,
					     gl_gsaves->act_export};
	for (u32 i = 0; i < ACT_COUNT; i++)
		gs.rows[i] = (struct slot_list_row){text[i], 0, 0};
	hints_set(gl_hint_ok, gl_hint_back);
	u32 at = 0;
	for (;;) {
		const struct slot_list list = {
			"game-saves-slot", gl_gsaves->slot_title, gs.slot, gs.rows, ACT_COUNT, at, gs.hints, 2, 0};
		u32 row;
		if (slot_list_run(&list, &row) != SLOT_LIST_PICK)
			return 0;
		at = row;
		int r;
		char out[SAVESLOT_NAME_BYTES];
		const struct slotstore_slot *slots;
		switch (row) {
		case ACT_LAUNCH:
		case ACT_SET:
			r = slotstore_set_current(gs.rom, gs.slot);
			tlog("gsaves: current %s: %d", gs.slot, r);
			if (r != SLOTSTORE_OK) {
				fail(r);
				return 0;
			}
			return row == ACT_LAUNCH;
		case ACT_RENAME:
			if (slot_flow_rename(gs.rom, gs.slot, out) == SLOT_FLOW_DONE) {
				snprintf(gs.slot, sizeof(gs.slot), "%s", out);
				snprintf(gs.want, sizeof(gs.want), "%s", out);
			}
			return 0;
		case ACT_DUP:
			if (slot_flow_name_prompt(gs.rom, gl_gsaves->dup_title, "", NULL, out) != SLOT_FLOW_DONE)
				continue;
			r = slotstore_duplicate(gs.rom, gs.slot, out, gs.want);
			tlog("gsaves: duplicate %s as %s: %d", gs.slot, out, r);
			if (r != SLOTSTORE_OK) {
				fail(r);
				gs.want[0] = 0;
			}
			return 0;
		case ACT_DELETE:
			if (slots_get(&slots) == 1) {
				fail(SLOTSTORE_LAST);
				continue;
			}
			snprintf(gs.text, sizeof(gs.text), EN_GS_DELETE, gs.slot);
			if (!dialog_confirm(gs.text, gl_gsaves->delete_text, DIALOG_ERROR))
				continue;
			r = slotstore_delete(gs.rom, gs.slot);
			tlog("gsaves: delete %s: %d", gs.slot, r);
			if (r != SLOTSTORE_OK)
				fail(r);
			return 0;
		case ACT_HISTORY:
			history(gs.slot);
			continue;
		default:
			export_slot(gs.slot);
			continue;
		}
	}
}

/* ---- The manager ---- */

// The rows: the slots, New slot, Import. Returns the slot count and puts the
// cursor row in *at (the slot gs.want names, else the current one)
static int rows_build(u32 *at)
{
	const struct slotstore_slot *slots;
	int n = slot_flow_slots(gs.rom, &slots);
	if (n < 0)
		return n;
	slotstore_current(gs.rom, gs.current);
	*at = 0;
	for (int i = 0; i < n; i++) {
		size_text(gs.detail[i], slots[i].size);
		int cur = !strcmp(slots[i].name, gs.current);
		gs.rows[i] = (struct slot_list_row){slots[i].name, gs.detail[i], (u8)(cur ? ART_ICON8_SAVE + 1 : 0)};
		if (gs.want[0] ? !strcmp(slots[i].name, gs.want) : cur)
			*at = (u32)i;
	}
	gs.rows[n] = (struct slot_list_row){gl_slots->new_row, 0, 0};
	gs.rows[n + 1] = (struct slot_list_row){gl_gsaves->import_row, 0, 0};
	return n;
}

int game_saves_probe(const char *path)
{
	// No card access for a ROM (the popup opens on START): its facts are
	// read once Saves is picked. A NOR entry's are in NOR.
	gs.size = 0;
	if (userdata_nor_name(path))
		return action_game_saves(path, gs.rom, sizeof(gs.rom), &gs.size);
	const char *name = strrchr(path, '/');
	if (!name || library_console_of(name + 1) != LIB_GBA || strlen(path) >= sizeof(gs.path))
		return 0;
	memcpy(gs.path, path, strlen(path) + 1);  // fits: checked above
	return 1;
}

int game_saves_run(const char *title)
{
	int own = !ui_active();
	if (own)
		dialog_open();
	gs.title = title;
	gs.want[0] = 0;
	int result = GAME_SAVES_BACK;
	// A save still on the chip goes to the card before any slot moves
	if (!pending_save_flush()) {
		if (own)
			dialog_close();
		return result;
	}
	if (!gs.size && !action_game_saves(gs.path, gs.rom, sizeof(gs.rom), &gs.size)) {
		dialog_message(NULL, gl_gsaves->no_save, DIALOG_INFO);
		if (own)
			dialog_close();
		return result;
	}
	for (;;) {
		u32 at;
		int n = rows_build(&at);
		if (n < 0) {
			fail(n);
			break;
		}
		hints_set(gl_hint_open, gl_hint_back);
		const struct slot_list list = {"game-saves", gl_gsaves->title, title, gs.rows, (u32)n + 2,
					       at,           gs.hints,         2,     0};
		u32 row;
		if (slot_list_run(&list, &row) != SLOT_LIST_PICK)
			break;
		gs.want[0] = 0;
		if (row < (u32)n) {
			char name[SAVESLOT_NAME_BYTES];
			snprintf(name, sizeof(name), "%s", gs.rows[row].text);
			snprintf(gs.want, sizeof(gs.want), "%s", name);
			if (slot_actions(name)) {
				result = GAME_SAVES_LAUNCH;
				break;
			}
		} else if (row == (u32)n) {
			if (slot_flow_new(gs.rom, gs.size, gs.want) != SLOT_FLOW_DONE)
				gs.want[0] = 0;
		} else {
			import_file();
		}
	}
	if (own)
		dialog_close();
	return result;
}
