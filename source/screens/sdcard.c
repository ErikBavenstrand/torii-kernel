// The SD card page (sdcard.h).
//
// The folder shown is currentpath (ezkernelnew.c's, which the launch path
// opens games in): it starts at "/" and stays the page's while the menu
// runs, so a launch that didn't start, or a write to NOR, comes back to the
// folder it left. Its entries are listed into PSRAM (sdlist.h) each time the
// page comes in or the folder changes: a launch fills PSRAM, so nothing in
// it outlives one.
//
// Event handlers only record what to do (nav_defer); the deferred functions
// read the card, open the popup and its confirmation, and call actions.h,
// outside ui_frame. A folder's item count is found by a job (engine/jobs.h)
// while the page rests: one f_readdir at a time, in the frame's spare
// cycles, the rows near the cursor first.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "sdcard.h"
#include "../actions.h"
#include "../ezkernel.h"
#include "../Ezcard_OP.h"
#include "../gamename.h"
#include "../lang.h"
#include "../library.h"
#include "../savename.h"
#include "../sdlist.h"
#include "../settings.h"
#include "../tlog.h"
#include "../userdata.h"
#include "../engine/cycles.h"
#include "../engine/engine.h"
#include "../engine/psram.h"
#include "../ui/art.h"
#include "../ui/art_ids.h"
#include "../ui/dialog.h"
#include "../ui/ui.h"
#include "rename.h"

// --- Look, px (spec.md's grid: title bar 0-18, content 20-143, hints at 147) ---
#define TITLE_H 24    // the title bar's rectangle: whole tiles under its line
#define PATH_X 16     // a folder's path pills, in the title bar after its icon
#define PATH_W 152    // up to the position's room at the right (title_right_w)
#define PATH_H 16     // the pills centred on the heading's line
#define CARD_Y 16     // the card's rectangle; its body starts 4 px in
#define CARD_H 128
#define LIST_X 8      // the list, 8 px in from the screen's edge
#define LIST_Y 24
#define LIST_W 216    // up to the scroll bar's column
#define LIST_H 112    // 8 rows of 14 px: whole rows (B9's contract)
#define LIST_PAD 4    // the icon's inset in its row
#define DETAIL_W 60   // the size or the count ("1,024+ items")
#define NOTE_X 72     // an empty folder's note, in the card's middle (narrow: plane
#define NOTE_Y 72     // 2's tiles are the popups' and confirmations' too)
#define NOTE_W 96
#define NOTE_H 16
#define HINTS_H 16
static const struct ui_card_config card = {4, 4, 4, 1, 0};

// --- Behaviour -----------------------------------------------------------------
#define PAGE_ROWS 8              // LEFT/RIGHT move a screenful (L and R don't: L is the hard reset's)
#define MEMO_FOLDERS 16          // folders whose cursor is remembered
#define COUNT_REST 8             // frames the keys rest before folders are counted
#define COUNT_CYCLES 40000       // cycles a counting step may take (a sector read or two)
#define COUNT_NEAR 8             // rows either side of the cursor counted first
#define ARG_HARD 0x10000u        // nav_defer's arg: L was held (a hard reset)

_Static_assert(PSRAM_WINDOW + PSRAM_NAMES_BYTES <= PSRAM_LIBRARY_SCRATCH, "the listing reaches the library's scratch");
_Static_assert(READCACHE_SDLIST + sizeof(struct sdlist_scratch) <= MAX_pReadCache_size,
	       "the listing's scratch passes its room");
_Static_assert(READCACHE_SDLIST >= READCACHE_USERDATA + READCACHE_USERDATA_SIZE,
	       "the listing's scratch meets the user data's");

extern TCHAR currentpath[MAX_path_len];
extern FILINFO fileinfo;  // ezkernelnew.c's
extern DIR dir;
extern u8 pReadCache[];

// The popup's entries
enum {
	POP_OPEN,
	POP_LAUNCH,
	POP_CLEAN,
	POP_ADDON,
	POP_NOR,
	POP_NOR_ADDON,
	POP_FAV,
	POP_RENAME,
	POP_DELETE,
	POP_INFO,
};

static struct {
	struct sdlist list;
	struct ui_view *view;
	u8 first;  // the view's first node
	u8 job;    // the counting job
	u8 show_all;  // the Show all files setting, as the listing read it
	u8 announce;  // log "sdcard: ready" once the list rests
	u8 measure_left;  // frames of a folder change to measure
	u8 hint_note;     // what the hint bar shows: HINTS_*
	u8 find;          // the next listing puts the cursor on the file named sd.ask (a rename's)
	u8 renamed_partly;  // the rename left something behind: said once the page is back
	u32 counting;  // the folder being counted, SDLIST_NONE none
	u32 counted;   // its entries so far
	u32 count_next;  // where the counting goes on past the cursor's rows
	DIR count_dir;
	FILINFO count_fi;
	char count_path[MAX_path_len + GAME_NAME_MAX + 2];
	char full[MAX_path_len + GAME_NAME_MAX + 2] ALIGN(4);  // a file's full path
	char name[GAME_NAME_MAX + 1];  // the popup's entry, in RAM
	char pills[MAX_path_len + 4];  // "SD" and the folder's path
	char position[28];
	char detail[24];
	char note[40];
	char ask[GAME_NAME_MAX + 16];
	char info[320];
	char save[SAVE_NAME_MAX + 8];   // File info's save path
	char back[MAX_path_len];        // the folder a failed open goes back to
	u32 memo_hash[MEMO_FOLDERS];
	u16 memo_cursor[MEMO_FOLDERS];
	u8 memo_next;
	u8 pop_ids[UI_POPUP_ITEMS];
	struct ui_pop_item pop_items[UI_POPUP_ITEMS];
	struct ui_popup popup;
	struct ui_hint hints[3];
	u32 icons[2][8] ALIGN(4);  // the rows' 8x8 tiles: folder (the item's ink), file (ink3)
} sd EWRAM_BSS;

// --- The list model --------------------------------------------------------------

static u32 entries(void)
{
	return sd.list.count;
}

static u32 model_count(void *ctx)
{
	(void)ctx;
	return entries();
}

static int model_fetch(void *ctx, u32 i, struct ui_item *out)
{
	(void)ctx;
	if (i >= entries())
		return UI_ITEM_PENDING;
	const struct sdlist_rec *r = sdlist_at(&sd.list, i);
	out->text = sdlist_name(r);
	if (sdlist_is_dir(r)) {
		out->icon = sd.icons[0];
		sdlist_count_text(sd.detail, sizeof(sd.detail), r->count, gl_sd_item, gl_sd_items, gl_sd_items_none);
	} else {
		out->icon = sd.icons[1];
		sdlist_size_text(sd.detail, sizeof(sd.detail), r->size);
	}
	out->detail = sd.detail;  // drawn before the next fetch
	return UI_ITEM_READY;
}

static const struct ui_list_model model = {model_count, model_fetch, 0, 0, 0};

// --- The layout -------------------------------------------------------------------

enum {
	N_TITLE,
	N_PATH,
	N_HINTS,
	N_CARD,
	N_NOTE,
	N_LIST,
	N_BAR,
	N_NODES
};

static const struct ui_title_config title = {"", "", ART_ICON16_SD};
static const struct ui_hints_config hints_config = {0, 0};  // enter's
static const struct ui_label_config note_label = {"", UI_FONT_BODY, UI_ALIGN_CENTER, 0, 0};
static const struct ui_list_config list = {&model, UI_ROW_PITCH_LIST,
					   UI_LIST_WRAP | UI_LIST_MARQUEE | UI_LIST_ICON8 | UI_LIST_NO_SHOULDERS,
					   DETAIL_W, LIST_PAD};
static const struct ui_scrollbar_config bar = {N_LIST};

static const struct ui_spec nodes[N_NODES] = {
	[N_TITLE] = {UI_TITLE, 1, UI_NONE, 3, UI_HFILL, 0, 0, 0, 0, 0, 0, TITLE_H, &title},
	[N_PATH] = {UI_PATH, 2, UI_NONE, 2, UI_LEFT, UI_HIDDEN, 0, UI_ROLE_BG, PATH_X, 0, PATH_W, PATH_H, 0},
	[N_HINTS] = {UI_HINTS, 3, UI_NONE, 3, UI_HFILL | UI_BOTTOM, 0, 0, 0, 0, 0, 0, HINTS_H, &hints_config},
	[N_CARD] = {UI_CARD, 4, UI_NONE, 2, UI_HFILL, 0, 0, 0, 0, CARD_Y, 0, CARD_H, &card},
	[N_NOTE] = {UI_LABEL, 5, UI_NONE, 2, UI_LEFT, 0, UI_ROLE_FAINT, UI_ROLE_PANEL, NOTE_X, NOTE_Y, NOTE_W, NOTE_H,
		    &note_label},
	[N_LIST] = {UI_LIST, 6, UI_NONE, 1, UI_LEFT, UI_FOCUSABLE, 0, 0, LIST_X, LIST_Y, LIST_W, LIST_H, &list},
	[N_BAR] = {UI_SCROLLBAR, 7, UI_NONE, 2, UI_LEFT, 0, 0, UI_ROLE_PANEL, LIST_X + LIST_W, LIST_Y, 8, LIST_H, &bar},
};

static int sd_event(struct ui_view *v, const struct ui_event *e);
static const struct ui_view_spec view = {"sdcard", nodes, N_NODES, N_LIST, UI_VIEW_LR_PAGES, sd_event};

static int up(void)
{
	return ui_depth() && ui_view_at(0)->spec == &view;
}

static u8 node(u32 n)
{
	return (u8)(sd.first + n);
}

static u32 cursor(void)
{
	return ui_list_cursor(node(N_LIST));
}

static int at_root(void)
{
	return currentpath[0] == 0 || (currentpath[0] == '/' && currentpath[1] == 0);
}

// --- Texts ------------------------------------------------------------------------

// The title bar's right: the cursor's place in the folder ("4/14")
static const char *position_text(u32 at)
{
	char a[12], b[12];
	u32 n = entries();
	if (!n) {
		sd.position[0] = 0;
		return sd.position;
	}
	sdlist_number(a, sizeof(a), at < n ? at + 1 : n);
	sdlist_number(b, sizeof(b), n);
	snprintf(sd.position, sizeof(sd.position), "%s/%s", a, b);
	return sd.position;
}

// The folder's title: "SD card" at the root; in a folder, the path's pills
// in the heading's place ("SD", "GBA", "RPG", the last tinted)
static void show_folder(void)
{
	int root = at_root();
	ui_title_set(node(N_TITLE), root ? gl_item_sd : "");
	snprintf(sd.pills, sizeof(sd.pills), "SD%s", root ? "" : currentpath);
	ui_path_set(node(N_PATH), sd.pills);
	ui_show(node(N_PATH), !root);
	ui_label_set(node(N_NOTE), entries() ? "" : gl_sd_empty);  // a label hidden would cut a hole in the card
}

// The hint bar: the keys; only B in an empty folder (A and START do
// nothing there); or on a truncated folder's last entry the note that
// more weren't listed (the end, where it is wondered about)
enum {
	HINTS_KEYS,
	HINTS_NOTE,
	HINTS_EMPTY,
	HINTS_NONE
};
static void hint_for(u32 at)
{
	u8 state = !entries() ? HINTS_EMPTY : sd.list.truncated && at + 1 >= entries() ? HINTS_NOTE : HINTS_KEYS;
	if (state == sd.hint_note)
		return;  // a change redraws the bar: not on every move
	sd.hint_note = state;
	if (state == HINTS_NOTE)
		ui_hints_text(node(N_HINTS), sd.note);
	else if (state == HINTS_EMPTY)
		ui_hints_set(node(N_HINTS), &sd.hints[2], 1);  // B back
	else
		ui_hints_set(node(N_HINTS), sd.hints, 3);
}

// --- Listing ----------------------------------------------------------------------

static void put_psram(void *dst, const void *src, u32 bytes)
{
	psram_write(dst, src, bytes);
}

static int show_all_setting(void)
{
	u16 slots[assress_max];
	for (u32 a = 0; a < assress_max; a++)
		slots[a] = Read_SET_info(a);
	struct settings_values v;
	settings_values_init(&v, slots);
	return settings_get(&v, SET_SHOW_ALL) != 0;
}

static u32 entry_flags(const FILINFO *fi)
{
	return ((fi->fattrib & AM_DIR) ? GAMENAME_DIR : 0) |
	       ((fi->fattrib & (AM_HID | AM_SYS)) ? GAMENAME_ATTR_HIDDEN : 0) |
	       (library_console_of(fi->fname) != LIB_CONSOLE_NONE ? GAMENAME_OPENABLE : 0);
}

static void count_stop(void)
{
	if (job_running(sd.job))
		job_cancel(sd.job);
	sd.job = JOB_NONE;
	sd.counting = SDLIST_NONE;
}

// Lists currentpath into PSRAM: folders, then files, the hidden rule's
// entries left out unless Show all files is on. 0 if the folder didn't open
// (the list is empty then).
static int list_folder(void)
{
	count_stop();
	u32 start = cycles_now();
	sd.show_all = (u8)show_all_setting();
	psram_begin();
	sdlist_begin(&sd.list, (u8 *)PSRAM_WINDOW, PSRAM_NAMES_BYTES, put_psram,
		     (struct sdlist_scratch *)(void *)(pReadCache + READCACHE_SDLIST));
	FRESULT res = f_opendir(&dir, currentpath);
	if (res == FR_OK) {
		for (;;) {
			res = f_readdir(&dir, &fileinfo);
			if (res != FR_OK || fileinfo.fname[0] == 0)
				break;
			if (fileinfo.fattrib & 0x08)
				continue;  // a volume label
			if (gamename_hidden(currentpath, fileinfo.fname, entry_flags(&fileinfo), sd.show_all))
				continue;
			u32 size =
				fileinfo.fsize > 0xFFFFFFFFu ? 0xFFFFFFFFu : (u32)fileinfo.fsize;  // exFAT's past 4 GB
			// Past the files' cap, folders still list: read on until
			// nothing more fits
			if (!sdlist_add(&sd.list, fileinfo.fname, size, (fileinfo.fattrib & AM_DIR) != 0) &&
			    sd.list.full)
				break;
		}
		f_closedir(&dir);
	}
	sdlist_finish(&sd.list);
	char n[12];
	u32 files = entries() - sd.list.folders, by_files = files >= SDLIST_MAX || sd.list.folders < SDLIST_MAX;
	sdlist_number(n, sizeof(n), by_files ? files : sd.list.folders);
	snprintf(sd.note, sizeof(sd.note), "%s %s %s", gl_sd_first, n, by_files ? gl_count_files : gl_sd_folders);
	sd.count_next = 0;
	u32 ms = cycles_since(start) / (ENGINE_FRAME_CYCLES * 60u / 1000u);
	tlog_debug("sdcard: listed %s: %lu folders, %lu files%s (%lu ms)", currentpath, (unsigned long)sd.list.folders,
		   (unsigned long)(entries() - sd.list.folders), sd.list.truncated ? ", truncated" : "",
		   (unsigned long)ms);
	if (res != FR_OK)
		tlog("sdcard: %s: FatFs %u", currentpath, (unsigned)res);
	return res == FR_OK;
}

// --- Remembered cursors ----------------------------------------------------------

static u32 path_hash(const char *s)
{
	u32 h = 2166136261u;  // FNV-1a
	while (*s)
		h = (h ^ (u8)*s++) * 16777619u;
	return h;
}

static void memo_store(void)
{
	u32 h = path_hash(currentpath), at = sd.memo_next;
	for (u32 i = 0; i < MEMO_FOLDERS; i++)
		if (sd.memo_hash[i] == h)
			at = i;
	if (at == sd.memo_next)
		sd.memo_next = (u8)((sd.memo_next + 1) % MEMO_FOLDERS);
	sd.memo_hash[at] = h;
	sd.memo_cursor[at] = (u16)cursor();
}

// The cursor folder path was left with, or SDLIST_NONE
static u32 memo_find(const char *path)
{
	u32 h = path_hash(path);
	for (u32 i = 0; i < MEMO_FOLDERS; i++)
		if (sd.memo_hash[i] == h)
			return sd.memo_cursor[i];
	return SDLIST_NONE;
}

static u32 memo_get(void)
{
	u32 at = memo_find(currentpath);
	return at == SDLIST_NONE ? 0 : at;
}

// --- Counting folders' entries ---------------------------------------------------

// The next folder to count: an uncounted one near the cursor, else the
// next in order; SDLIST_NONE when all are
static u32 count_pick(void)
{
	u32 c = cursor(), lo = c > COUNT_NEAR ? c - COUNT_NEAR : 0, hi = c + COUNT_NEAR;
	for (u32 i = lo; i <= hi && i < sd.list.folders; i++)
		if (sdlist_at(&sd.list, i)->count == SDLIST_COUNT_UNKNOWN)
			return i;
	while (sd.count_next < sd.list.folders && sdlist_at(&sd.list, sd.count_next)->count != SDLIST_COUNT_UNKNOWN)
		sd.count_next++;
	return sd.count_next < sd.list.folders ? sd.count_next : SDLIST_NONE;
}

static void count_done(u32 count)
{
	u32 i = sd.counting;
	sd.counting = SDLIST_NONE;
	psram_begin();
	sdlist_set_count(&sd.list, i, count);
	ui_list_refresh(node(N_LIST), i, i);
}

// Starts counting folder i: 0 if it didn't open
static int count_open(u32 i)
{
	const char *name = sdlist_name(sdlist_at(&sd.list, i));
	u32 dl = (u32)strlen(currentpath), nl = (u32)strlen(name), sep = !at_root();
	sd.counting = i;
	sd.counted = 0;
	if (dl + sep + nl >= MAX_path_len) {
		count_done(SDLIST_COUNT_FAILED);  // too deep to open (the page can't either)
		return 0;
	}
	memcpy(sd.count_path, currentpath, dl);
	sd.count_path[dl] = '/';
	memcpy(sd.count_path + dl + sep, name, nl + 1);
	if (f_opendir(&sd.count_dir, sd.count_path) != FR_OK) {
		count_done(SDLIST_COUNT_FAILED);
		return 0;
	}
	return 1;
}

static int count_step(struct job *job)
{
	if (job->state == JOB_CANCEL) {
		if (sd.counting != SDLIST_NONE)
			f_closedir(&sd.count_dir);  // nothing to free with FF_FS_LOCK 0, but tidy
		sd.counting = SDLIST_NONE;
		return JOB_DONE;
	}
	// What the user does keeps its frames: the job waits while keys act, a
	// screen or folder changes, or a popup or dialog is up
	if (!up() || ui_top() != sd.view || ui_idle_frames() < COUNT_REST || ui_transitioning() ||
	    !ui_list_still(node(N_LIST)))
		return JOB_WAIT;
	u32 start = cycles_now();
	while (cycles_since(start) < COUNT_CYCLES) {
		if (sd.counting == SDLIST_NONE) {
			u32 i = count_pick();
			if (i == SDLIST_NONE) {
				tlog_debug("sdcard: counted %s", currentpath);
				return JOB_DONE;
			}
			if (!count_open(i))
				continue;
		}
		FRESULT res = f_readdir(&sd.count_dir, &sd.count_fi);
		if (res != FR_OK) {
			count_done(SDLIST_COUNT_FAILED);
			continue;
		}
		if (!sd.count_fi.fname[0]) {
			f_closedir(&sd.count_dir);
			count_done(sd.counted);
			continue;
		}
		if (!(sd.count_fi.fattrib & 0x08) &&
		    !gamename_hidden(sd.count_path, sd.count_fi.fname, entry_flags(&sd.count_fi), sd.show_all) &&
		    ++sd.counted > SDLIST_MAX) {
			f_closedir(&sd.count_dir);
			count_done(sd.counted);  // "1,024+ items": the rest go unread
		}
	}
	return JOB_MORE;
}

static void count_start(void)
{
	count_stop();
	if (sd.list.folders)
		sd.job = job_start(count_step, 0, ui_owner(sd.view));
}

// --- Folders ----------------------------------------------------------------------

// The folder was listed again (a new folder, a delete): the list comes in
// with the cursor on at, as a page change, and the title with it
static void relist(u32 at)
{
	u8 l = node(N_LIST);
	ui_list_reset(l, at);
	show_folder();
	ui_list_page_label(l, node(N_TITLE), position_text(ui_list_cursor(l)));
	hint_for(ui_list_cursor(l));
	count_start();
	sd.announce = 1;
	sd.measure_left = 16;
}

static void error(u8 num)
{
	const char *msg = Error_message(num);
	tlog_error("error %u: %s", num, msg);
	dialog_message(NULL, msg, DIALOG_ERROR);
}

// Copies entry i's name into sd.name (it is in PSRAM, which a launch
// overwrites) and its full path into sd.full; 0 if the path doesn't fit
static int take_entry(u32 i)
{
	const char *name = sdlist_name(sdlist_at(&sd.list, i));
	u32 nl = (u32)strnlen(name, GAME_NAME_MAX), dl = (u32)strlen(currentpath), sep = !at_root();
	memcpy(sd.name, name, nl);
	sd.name[nl] = 0;
	if (dl + sep + nl >= sizeof(sd.full))
		return 0;
	memcpy(sd.full, currentpath, dl);
	sd.full[dl] = '/';
	memcpy(sd.full + dl + sep, sd.name, nl + 1);
	return 1;
}

static void list_failed(u32 arg)
{
	(void)arg;
	error(0);
}

static void folder_in(u32 i)
{
	if (!take_entry(i) || strlen(sd.full) >= MAX_path_len) {
		error(9);  // a path past MAX_path_len can't be opened; cutting it would open another
		return;
	}
	tlog_debug("sdcard: open %s", sd.full);
	memo_store();
	memcpy(sd.back, currentpath, MAX_path_len);
	memcpy(currentpath, sd.full, strlen(sd.full) + 1);
	if (!list_folder()) {
		// The folder it was in comes back before the message: the list
		// under it reads its rows from the listing
		memcpy(currentpath, sd.back, MAX_path_len);
		list_folder();
		relist(i);
		error(0);
		return;
	}
	relist(memo_get());
}

static void folder_up(u32 arg)
{
	(void)arg;
	char *slash = strrchr(currentpath, '/');
	if (!slash || at_root())
		return;
	memo_store();
	snprintf(sd.name, sizeof(sd.name), "%s", slash + 1);  // the folder left: the cursor goes on it
	*slash = 0;
	if (!currentpath[0])
		memcpy(currentpath, "/", 2);
	tlog_debug("sdcard: up to %s", currentpath);
	int ok = list_folder();
	u32 at = sdlist_find(&sd.list, sd.name);
	relist(at != SDLIST_NONE ? at : memo_get());
	if (!ok)
		error(0);
}

// --- Rename -----------------------------------------------------------------------
// The file's name before its extension, which stays: the keyboard starts
// from it, and OK renames the file and what goes with it (actions.h). The
// page comes back with the cursor on the file.

static const char *rename_submit(void *ctx, const char *stem)
{
	(void)ctx;
	const char *dot = strrchr(sd.name, '.');
	const char *ext = dot && dot != sd.name ? dot : "";
	if ((u32)snprintf(sd.ask, sizeof(sd.ask), "%s%s", stem, ext) > GAME_NAME_MAX)
		return gl_rename->bad;
	tlog_debug("sdcard: rename %s to %s", sd.full, sd.ask);
	switch (action_rename_file(sd.full, sd.ask)) {
	case ACTION_RENAMED:
		return NULL;
	case ACTION_RENAME_PARTLY:
		sd.renamed_partly = 1;
		return NULL;
	case ACTION_RENAME_TAKEN:
		return gl_rename->taken;
	case ACTION_RENAME_BAD:
		return gl_rename->bad;
	case ACTION_RENAME_HIDDEN:
		return gl_rename->hidden;
	case ACTION_RENAME_SIDE_TAKEN:
		return gl_rename->side_taken[action_rename_taken() % ACTION_FILES];
	case ACTION_RENAME_PENDING:
		return gl_rename->pending;
	case ACTION_RENAME_SHARED:
		return gl_rename->shared;
	default:
		return gl_rename->failed;
	}
}

static struct ui_view *sd_enter(u32 item, u32 row);

static void rename_file(void)
{
	const char *dot = strrchr(sd.name, '.');
	u32 stem = dot && dot != sd.name ? (u32)(dot - sd.name) : (u32)strlen(sd.name);
	snprintf(sd.info, sizeof(sd.info), "%.*s", (int)stem, sd.name);
	const struct rename_ask ask = {ART_ICON16_SD, sd.info, sd.name + stem, RENAME_STEM_MAX, 1, rename_submit, 0};
	memo_store();  // the cursor the page comes back with if nothing changes
	count_stop();
	sd.renamed_partly = 0;
	sd.find = (u8)rename_run(&ask);
	// The page again in the menu's session, listed afresh
	if (!ui_active() || ui_depth()) {
		sd.find = 0;
		return;
	}
	if (!sd_enter(NAV_SD, 0)) {
		tlog_error("sdcard: %s", ui_error());
		ui_shutdown();
		dialog_blank();
		return;
	}
	if (sd.renamed_partly)
		dialog_message(NULL, gl_rename->partly, DIALOG_ERROR);
}

// --- Actions ----------------------------------------------------------------------

// 1 if Settings turns any BOOT WITH ADDON switch on, from the slots as the
// launch reads them (the gl_*_on copies are only read at a launch)
static int any_addon(void)
{
	return Read_SET_info(assress_v_reset) == 1 || Read_SET_info(assress_v_rts) == 1 ||
	       Read_SET_info(assress_v_sleep) == 1 || Read_SET_info(assress_v_cheat) == 1;
}

// 1 if Launch starts the game at sd.full clean: its own boot mode says so,
// or it has none and Settings' GBA boot mode does
static int boots_clean(void)
{
	struct action_facts facts;
	if (!action_game_facts(sd.full, &facts))
		return 0;
	if (facts.boot_mode != ACTION_BOOT_DEFAULT)
		return facts.boot_mode == ACTION_BOOT_CLEAN;
	return Read_SET_info(assress_boot_mode) == 1;
}

static const char *const how_words[ACTION_HOWS + 1] = {"launch", "launch with addon", "write to NOR",
						       "write to NOR with addon", "launch by boot mode"};

// Starts the game at sd.full (actions.h): returns only if it didn't start,
// with the engine left; the shell brings the page back
static void launch(u32 how, u32 hard)
{
	memo_store();  // the cursor the page comes back with if no game starts
	tlog_debug("sdcard: %s %s%s", how_words[how], sd.full, hard ? " (hard reset)" : "");
	action_launch(sd.full, how, ACTION_SAVE_KEEP, hard);
}

// Appends to [p, end), never past it; returns where the text ends
__attribute__((format(printf, 3, 4))) static char *put_text(char *p, char *end, const char *format, ...)
{
	if (p >= end)
		return end;
	va_list args;
	va_start(args, format);
	int n = vsnprintf(p, (size_t)(end - p), format, args);
	va_end(args);
	return n < 0 ? p : n >= end - p ? end - 1 : p + n;
}

static void info(u32 i)
{
	const struct sdlist_rec *r = sdlist_at(&sd.list, i);
	char *p = sd.info, *end = sd.info + sizeof(sd.info);
	u32 console = sdlist_console(r);
	const char *const consoles[LIB_CONSOLES] = {gl_item_gba, gl_item_gbc, gl_item_gb, gl_item_nes};
	if (sdlist_is_dir(r))
		p = put_text(p, end, "%s", gl_sd_kind_folder);
	else if (console < LIB_CONSOLES)
		p = put_text(p, end, "%s %s", consoles[console], gl_sd_kind_game);
	else
		p = put_text(p, end, "%s", gl_sd_kind_file);
	char a[24], b[16];
	if (sdlist_is_dir(r)) {
		sdlist_count_text(a, sizeof(a), r->count, gl_sd_item, gl_sd_items, gl_sd_items_none);
		if (a[0] && p < end)
			p = put_text(p, end, "\n%s", a);
	} else if (p < end) {
		sdlist_size_text(a, sizeof(a), r->size);
		sdlist_number(b, sizeof(b), r->size);
		p = put_text(p, end, "\n%s (%s %s)", a, b, gl_sd_bytes);
	}
	if (p < end && f_stat(sd.full, &fileinfo) == FR_OK && fileinfo.fdate) {
		unsigned d = fileinfo.fdate, t = fileinfo.ftime;
		p = put_text(p, end, "\n%s %04u-%02u-%02u %02u:%02u", gl_sd_changed, (d >> 9) + 1980, (d >> 5) & 15,
			     d & 31, t >> 11, (t >> 5) & 63);
	}
	if (p < end && console < LIB_CONSOLES) {
		memcpy(sd.save, "/SAVER/", 7);
		save_name(sd.save + 7, sd.name, console);
		if (f_stat(sd.save, &fileinfo) == FR_OK)
			snprintf(p, (size_t)(end - p), "\n%s: %s", gl_sd_save, sd.save + 7);
		else
			snprintf(p, (size_t)(end - p), "\n%s", gl_sd_no_save);
	}
	dialog_message(sd.name, sd.info, DIALOG_INFO);
}

// Delete, after the confirmation: the file goes, its save stays (T11), and
// the folder is listed again with the cursor where it was
static int delete_file(u32 i)
{
	tlog_debug("sdcard: delete %s", sd.full);
	if (!action_delete_file(sd.full)) {
		dialog_message(NULL, gl_sd_delete_failed, DIALOG_ERROR);
		return 0;
	}
	list_folder();
	relist(i);
	return 1;
}

static void pop_add(u32 id, const char *text, u8 flags)
{
	u32 n = sd.popup.count;
	sd.pop_ids[n] = (u8)id;
	sd.pop_items[n] = (struct ui_pop_item){text, 0, flags};
	sd.popup.count = (u8)(n + 1);
}

static const char *fav_text(void)
{
	return userdata_fav_has(sd.full) ? gl_sd_fav_remove : gl_sd_fav_add;
}

// START (or A on a file the cart can't start): the entry's popup
static void file_popup(u32 arg)
{
	u8 l = node(N_LIST);
	while (up() && ui_list_paging(l))
		ui_frame();  // beside the row where it will rest
	u32 i = arg & 0xFFFF;
	if (!up() || i >= entries() || i != cursor())
		return;
	if (!take_entry(i)) {
		error(8);
		return;
	}
	const struct sdlist_rec *r = sdlist_at(&sd.list, i);
	u32 console = sdlist_console(r);
	sd.popup = (struct ui_popup){sd.name, (u8)((sdlist_is_dir(r) ? ART_ICON8_FOLDER : ART_ICON8_FILE) + 1),
				     sd.pop_items, 0, 0};
	if (sdlist_is_dir(r)) {
		pop_add(POP_OPEN, gl_sd_open, 0);
	} else if (console != LIB_CONSOLE_NONE) {
		pop_add(POP_LAUNCH, gl_sd_launch, 0);
		// The other way to start it than Launch's: clean (a game patches
		// break can always start clean), or a GBA game that boots clean with
		// its addons (a popup has no room for both)
		if (any_addon() && console == LIB_GBA && boots_clean())
			pop_add(POP_ADDON, gl_games->pop[LANG_POP_ADDON], 0);
		else if (any_addon())
			pop_add(POP_CLEAN, gl_sd_clean, 0);
		if (console == LIB_GBA) {  // only GBA games go to the game NOR
			pop_add(POP_NOR, gl_sd_nor, 0);
			if (any_addon())
				pop_add(POP_NOR_ADDON, gl_sd_nor_addon, 0);
		}
		pop_add(POP_FAV, fav_text(), 0);
	}
	if (!sdlist_is_dir(r)) {
		pop_add(POP_RENAME, gl_rename->title, 0);
		pop_add(POP_DELETE, gl_sd_delete, UI_POP_DANGER);
	}
	pop_add(POP_INFO, gl_sd_info, 0);
	if (!ui_popup_open(l, node(N_HINTS), &sd.popup))
		return;
	tlog_debug("sdcard: popup %s", sd.full);
	for (;;) {
		s32 pick = ui_popup_choose();
		if (pick < 0)
			return;  // B: it has closed
		u32 id = sd.pop_ids[pick];
		u32 hard = (ui_input()->held & INPUT_L) != 0;
		if (id == POP_FAV) {
			int on = !userdata_fav_has(sd.full);
			tlog_debug("sdcard: favourite %s %s", sd.full, on ? "on" : "off");
			if (!userdata_fav_set(sd.full, on)) {
				ui_popup_close();
				dialog_message(NULL, gl_sd_fav_failed, DIALOG_ERROR);
				return;
			}
			sd.pop_items[pick].text = fav_text();
			ui_popup_changed((u32)pick);
			continue;  // it stays up
		}
		if (id == POP_DELETE) {
			snprintf(sd.ask, sizeof(sd.ask), "%s %s?", gl_sd_delete_ask, sd.name);
			struct ui_confirm ask = {sd.ask, console != LIB_CONSOLE_NONE ? gl_sd_delete_save : NULL,
						 gl_sd_delete, 1};
			if (!ui_confirm(node(N_HINTS), &ask))
				continue;  // Cancel: back to the popup
			ui_popup_close();
			delete_file(i);
			return;
		}
		ui_popup_close();
		switch (id) {
		case POP_OPEN:
			folder_in(i);
			break;
		case POP_LAUNCH:
			launch(ACTION_DEFAULT, hard);
			break;
		case POP_CLEAN:
			launch(ACTION_CLEAN, hard);
			break;
		case POP_ADDON:
			launch(ACTION_ADDON, hard);
			break;
		case POP_NOR:
			launch(ACTION_NOR, hard);
			break;
		case POP_NOR_ADDON:
			launch(ACTION_NOR_ADDON, hard);
			break;
		case POP_RENAME:
			rename_file();
			break;
		default:
			info(i);
			break;
		}
		return;
	}
}

// A on an entry: a folder opens, a game starts (ACTION_DEFAULT), anything else
// shows its popup
static void activate(u32 arg)
{
	u32 i = arg & 0xFFFF;
	if (!up() || i >= entries())
		return;
	const struct sdlist_rec *r = sdlist_at(&sd.list, i);
	if (sdlist_is_dir(r)) {
		folder_in(i);
	} else if (sdlist_console(r) == LIB_CONSOLE_NONE) {
		file_popup(i);
	} else if (!take_entry(i)) {
		error(8);
	} else {
		launch(ACTION_DEFAULT, (arg & ARG_HARD) != 0);
	}
}

// --- Events -----------------------------------------------------------------------

static int sd_event(struct ui_view *v, const struct ui_event *e)
{
	(void)v;
	u8 l = node(N_LIST);
	u32 hard = (ui_input()->held & INPUT_L) ? ARG_HARD : 0;
	switch (e->type) {
	case UI_EV_CURSOR:
		ui_title_set_right(node(N_TITLE), position_text(e->index));
		hint_for(e->index);
		frame_measure("sd-move");
		return 1;
	case UI_EV_ACTIVATE:
		if (e->index < entries())
			nav_defer(activate, e->index | hard);
		return 1;
	case UI_EV_KEYS: {
		u16 pressed = e->keys & ui_input()->pressed;
		if (pressed & INPUT_B) {
			if (at_root()) {
				memo_store();  // the cursor, for the next visit from home
				nav_back();
			} else
				nav_defer(folder_up, 0);
			return 1;
		}
		if (pressed & INPUT_START) {
			if (cursor() < entries())
				nav_defer(file_popup, cursor());
			return 1;
		}
		if (e->keys & (INPUT_LEFT | INPUT_RIGHT)) {
			u32 n = model_count(0), c = cursor();
			if (!n)
				return 1;
			u16 key = e->keys & INPUT_LEFT ? INPUT_LEFT : INPUT_RIGHT;
			u32 to = key == INPUT_LEFT ? (c > PAGE_ROWS ? c - PAGE_ROWS : 0)
						   : (c + PAGE_ROWS < n ? c + PAGE_ROWS : n - 1);
			if (to != c) {
				ui_list_move(l, to, key);
				ui_emit(l, UI_EV_CURSOR, to);
			}
			return 1;
		}
		return 0;
	}
	default:
		return 0;
	}
}

// --- The page (nav.h) -------------------------------------------------------------

static struct ui_view *sd_enter(u32 item, u32 row)
{
	(void)item;
	sd.job = JOB_NONE;
	sd.counting = SDLIST_NONE;
	int listed;
	art_icon8_tile(ART_ICON8_FOLDER, UI_ROLE_ACCENT_INK, sd.icons[0]);
	art_icon8_tile(ART_ICON8_FILE, UI_ROLE_FAINT, sd.icons[1]);
	listed = list_folder();
	struct ui_view *v = ui_push(&view, &sd);
	if (!v)
		return 0;
	sd.view = v;
	sd.first = v->first;
	sd.hints[0] = (struct ui_hint){"A", gl_hint_open};
	sd.hints[1] = (struct ui_hint){"START", gl_hint_menu};
	sd.hints[2] = (struct ui_hint){"B", gl_hint_back};
	sd.hint_note = HINTS_NONE;  // hint_for sets the bar
	// The cursor the folder was left with (from home, or back from a
	// launch that didn't start); else the place's row (Last place, at the
	// card's root, as the menu starts there)
	u32 at = memo_find(currentpath);
	if (at == SDLIST_NONE)
		at = row;
	if (sd.find) {
		// Back from a rename: on the file under its new name
		u32 found = sdlist_find(&sd.list, sd.ask);
		at = found != SDLIST_NONE ? found : at;
		sd.find = 0;
	}
	ui_list_reset(node(N_LIST), at);
	show_folder();
	ui_title_set_right(node(N_TITLE), position_text(ui_list_cursor(node(N_LIST))));
	hint_for(ui_list_cursor(node(N_LIST)));
	count_start();
	sd.announce = 1;
	if (!listed)
		nav_defer(list_failed, 0);  // after the frame: no dialog while the shell pushes the page
	return v;
}

// For Last place: the root's row, as the next power-on starts at the root
// (inside a folder, the row the root was left on)
static u32 sd_row(void)
{
	if (!up())
		return 0;
	if (at_root())
		return cursor();
	u32 at = memo_find("/");
	return at == SDLIST_NONE ? 0 : at;
}

static void sd_poll(void)
{
	if (!up())
		return;
	if (sd.measure_left) {
		frame_measure("sd-folder");
		sd.measure_left--;
	}
	if (sd.announce && ui_settled() && ui_list_still(node(N_LIST))) {
		sd.announce = 0;
		tlog_debug("sdcard: ready %s", currentpath);
	}
}

const struct nav_page sdcard_page = {sd_enter, sd_row, 0, sd_poll, 0};
