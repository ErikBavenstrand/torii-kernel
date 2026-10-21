// What the pages do to games and the game NOR (actions.h).
#include <stdio.h>
#include <string.h>

#include "actions.h"
#include "ezkernel.h"
#include "Ezcard_OP.h"
#include "GBApatch.h"
#include "artkey.h"
#include "boxart.h"
#include "gamename.h"
#include "library.h"
#include "NORflash_OP.h"
#include "safefile.h"
#include "tlog.h"
#include "userdata.h"
#include "engine/cycles.h"
#include "ui/dialog.h"
#include "ui/ui.h"

// The launch path's folder and file name (ezkernelnew.c) are the game's
// while it starts; what they held comes back after (the SD card page keeps
// its folder there)
extern TCHAR currentpath[MAX_path_len];
extern FILINFO fileinfo;  // ezkernelnew.c's
static TCHAR saved_path[MAX_path_len] EWRAM_BSS;
// The path asked for, in RAM: it may be in PSRAM (the library's names),
// which a launch overwrites and the cart hides while it reads the card
static char path_copy[USERDATA_PATH_MAX] EWRAM_BSS ALIGN(4);

// Ends the menu's engine session, if one is up: the screen fades to black
// and stays so while the launch runs sessions of its own
static void session_end(void)
{
	if (!ui_active())
		return;
	ui_leave(UI_TR_LEAVE, 0);
	ui_shutdown();
	dialog_blank();
}

static void error(u8 error_num)
{
	const char *msg = Error_message(error_num);
	tlog_error("error %u: %s", error_num, msg);
	tlog_flush();
	dialog_message(NULL, msg, DIALOG_ERROR);
}

static int copy_path(const char *path)
{
	u32 len = strlen(path);
	if (len >= sizeof(path_copy))
		return 0;
	memcpy(path_copy, path, len + 1);
	userdata_canonical(path_copy);  // "/deep/../x.gba" starts, and goes on Recent, as "/x.gba"
	return 1;
}

void action_nor_read(void)
{
	Read_NOR_info();
	game_total_NOR = GetFileListFromNor();
}

static u32 launch_nor_entry(const char *name, u32 hard_reset)
{
	action_nor_read();
	u32 game = NOR_game_named(name);
	if (game >= game_total_NOR) {
		// Deleted, or written over by another kernel: it leaves Recent, as a
		// file gone from the card does
		tlog("launch %s: not on the game NOR", name);
		userdata_forget_nor(name);
		error(1);
		return ACTION_NOTHING;
	}
	action_nor_launch(game, hard_reset);
	return ACTION_NOTHING;
}

// Makes path_copy (an SD file's full path) the launch path's folder and
// file name, keeping what they held; 0 if it isn't one (logged as what)
static int game_enter(const char *what)
{
	const char *slash = strrchr(path_copy, '/');
	u32 dir_len = slash ? (u32)(slash - path_copy) : 0;
	if (!slash || path_copy[0] != '/' || !slash[1] || dir_len >= MAX_path_len ||
	    strlen(slash + 1) > GAME_NAME_MAX) {
		tlog_error("%s %s: not a file's full path", what, path_copy);
		return 0;
	}
	memcpy(saved_path, currentpath, MAX_path_len);
	memset(currentpath, 0, MAX_path_len);
	memcpy(currentpath, path_copy, dir_len ? dir_len : 1);  // the root: "/"
	snprintf(current_filename, sizeof(current_filename), "%s", slash + 1);
	return 1;
}

// What game_enter changed comes back
static void game_leave(void)
{
	memcpy(currentpath, saved_path, MAX_path_len);
	f_chdir(currentpath[0] ? currentpath : "/");
}

// How ACTION_DEFAULT starts game: its own boot mode, else Settings'; with
// addons, but none on, is a clean boot (nothing to patch in)
static u32 default_how(const struct game_pick *game)
{
	u32 own = game->boot_mode == ACTION_BOOT_ADDONS || game->boot_mode == ACTION_BOOT_CLEAN;
	u32 mode = own                                     ? game->boot_mode
		   : Read_SET_info(assress_boot_mode) == 1 ? ACTION_BOOT_CLEAN
							   : ACTION_BOOT_ADDONS;
	u32 any = Read_SET_info(assress_v_reset) == 1 || Read_SET_info(assress_v_rts) == 1 ||
		  Read_SET_info(assress_v_sleep) == 1 || Read_SET_info(assress_v_cheat) == 1;
	tlog("launch: boot mode %s (%s)%s", mode == ACTION_BOOT_CLEAN ? "clean" : "with addons",
	     own ? "the game's" : "Settings'", mode == ACTION_BOOT_ADDONS && !any ? ", none on" : "");
	return mode == ACTION_BOOT_CLEAN || !any ? ACTION_CLEAN : ACTION_ADDON;
}

u32 action_launch(const char *path, u32 how, u32 save_type, u32 hard_reset)
{
	if (!copy_path(path) || how > ACTION_DEFAULT) {
		tlog_error("launch: can't take %.40s (how %lu)", path, (unsigned long)how);
		return ACTION_NOTHING;
	}
	const char *nor = userdata_nor_name(path_copy);
	if (nor)
		return launch_nor_entry(nor, hard_reset);
	if (!game_enter("launch"))
		return ACTION_NOTHING;

	u32 res = ACTION_NOTHING;
	struct game_pick game = {.name = current_filename};
	u32 ok = Game_pick(&game);
	if (ok && how == ACTION_DEFAULT)
		how = game.is_emu ? ACTION_CLEAN : default_how(&game);
	if (!ok) {
		tlog("launch %s: not a game the cart starts", path_copy);
	} else if (game.is_emu && how >= ACTION_NOR) {
		tlog("launch %s: only GBA games go to the game NOR", path_copy);
	} else if (how >= ACTION_NOR && strlen(game.name) > sizeof(pNorFS[0].filename) - 1) {
		// The game NOR's list keeps 99 bytes of a name: one longer can't be
		// listed there, and a cut one would save under another name
		error(8);
	} else {
		if (game.is_emu)
			how = ACTION_CLEAN;  // GB, GBC and NES games start clean
		else if (save_type < ACTION_SAVE_TYPES)
			game.save_num = (u8)save_type;
		session_end();
		res = Game_launch(&game, how, hard_reset);
		// A write to NOR lists its game in pNorFS but leaves the count:
		// the NOR page and home's count read them
		if (res != ACTION_NOTHING)
			action_nor_read();
	}
	game_leave();
	return res;
}

u32 action_game_facts(const char *path, struct action_facts *out)
{
	memset(out, 0, sizeof(*out));
	out->save_type = ACTION_SAVE_TYPES;
	if (!copy_path(path) || userdata_nor_name(path_copy) || !game_enter("facts"))
		return 0;
	struct game_pick game = {.name = current_filename};
	u32 ok = Game_pick(&game);  // reads the .mde file and looks for a cheat file
	if (ok && !game.is_emu) {
		out->save_type = game.save_num < ACTION_SAVE_TYPES ? game.save_num : 0;
		out->boot_mode = game.boot_mode < ACTION_BOOTS ? game.boot_mode : ACTION_BOOT_DEFAULT;
		out->cheats = game.havecht;
	}
	game_leave();
	return ok;
}

int action_set_save_type(const char *path, u32 save_type)
{
	if (save_type >= ACTION_SAVE_TYPES || !copy_path(path) || userdata_nor_name(path_copy) ||
	    !game_enter("save type"))
		return 0;
	// /SAVER/<name>.mde, read by the next launch's Game_pick (as v0.3's SAVE
	// TYPE line left it when the game started); a new card has no /SAVER yet
	FRESULT made = f_mkdir(SAVER_FOLDER);
	if (made != FR_OK && made != FR_EXIST)
		tlog("save type: %s: FatFs %u", SAVER_FOLDER, (unsigned)made);
	u8 res = Make_mde_file(current_filename, (u8)save_type);
	tlog("save type of %s: %lu (%u)", path_copy, (unsigned long)save_type, res);
	game_leave();
	return res == FR_OK;
}

int action_set_boot_mode(const char *path, u32 boot_mode)
{
	if (boot_mode >= ACTION_BOOTS || !copy_path(path) || userdata_nor_name(path_copy) || !game_enter("boot mode"))
		return 0;
	FRESULT made = f_mkdir(SAVER_FOLDER);
	if (made != FR_OK && made != FR_EXIST)
		tlog("boot mode: %s: FatFs %u", SAVER_FOLDER, (unsigned)made);
	u8 res = Make_mde_boot(current_filename, (u8)boot_mode);
	tlog("boot mode of %s: %lu (%u)", path_copy, (unsigned long)boot_mode, res);
	game_leave();
	return res == FR_OK;
}

int action_delete_file(const char *path)
{
	if (!copy_path(path) || path_copy[0] != '/')
		return 0;
	FRESULT res = f_stat(path_copy, &fileinfo);  // the kernel's, off the stack
	if (res == FR_OK && (fileinfo.fattrib & AM_DIR)) {
		tlog("delete %s: a folder, not a file", path_copy);  // f_unlink takes empty ones
		return 0;
	}
	tlog_open_now();  // the file may be the log the background open holds
	res = f_unlink(path_copy);
	if (res == FR_NO_FILE || res == FR_NO_PATH) {
		// Gone already (taken off the card on a PC): it leaves the lists all
		// the same, as a deleted one does
		tlog("delete %s: not on the card", path_copy);
	} else if (res != FR_OK) {
		tlog("delete %s: FatFs %u", path_copy, (unsigned)res);
		return 0;
	}
	tlog("delete: %s", path_copy);
	tlog_card_freed();
	userdata_forget(path_copy);
	library_forget(path_copy);
	return 1;
}

// --- Renaming a file --------------------------------------------------------
//
// A game's other files are named after its file name, in folders at the
// card's root: its save (save_name), save type, save states, cheats and
// patch cache (name_with_ext), and its custom box art (B22). They move with
// it, and the save state and the rest never land on another game's: the
// rename is refused first if one would. A move that fails puts back what
// moved. A journal, /torii/rename.txt (B5's safe file: the old path, a tab,
// the new one), lasts from before the first move to after the lists follow:
// a power cut in between is finished, or undone, at the next boot
// (action_rename_recover).

enum {
	SIDE_SAVE,   // save_name's
	SIDE_EXT,    // name_with_ext's, ext; GBA games only
	SIDE_ART,    // the file name and ".bin"
};
#define RENAME_JOURNAL "/torii/rename.txt"
#define RENAME_JOURNAL_MAX (2 * USERDATA_PATH_MAX + 64)  // its one line, and the CRC line
struct sidecar {
	const char *dir;
	u8 kind;
	u8 cache;  // a cache the next launch makes again: one already under the new name goes
	char ext[4];
};
#define ART_CUSTOM_DIR "/torii/art/custom"  // B22's custom art: <file name>.bin (artkey.h)
#define ART_CUSTOM_EXT ARTKEY_EXT
static const struct sidecar sidecars[] = {
	{SAVER_FOLDER, SIDE_SAVE, 0, ""}, {SAVER_FOLDER, SIDE_EXT, 0, "mde"}, {"/RTS", SIDE_EXT, 0, "rts"},
	{"/CHEAT", SIDE_EXT, 0, "cht"},   {"/PATCH", SIDE_EXT, 1, "pat"},     {ART_CUSTOM_DIR, SIDE_ART, 0, ""},
};
#define SIDECARS (sizeof(sidecars) / sizeof(*sidecars))

// The rename's RAM, in pReadCache (READCACHE_RENAME): what lasts while it
// calls the names file, the user's lists and the library, which borrow
// shares before it
struct rename_work {
	char old_path[USERDATA_PATH_MAX];  // the journal's, at boot
	char new_path[USERDATA_PATH_MAX];
	char name[GAME_NAME_MAX + 1];  // the old file name
	char from[GAME_NAME_MAX + 24];  // a file's path in its folder
	char to[GAME_NAME_MAX + 24];
	u8 has[SIDECARS];  // the old one is there
	u8 taken;          // ACTION_RENAME_SIDE_TAKEN: the sidecar in the way (ACTION_FILE_*)
};
_Static_assert(sizeof(struct rename_work) <= READCACHE_RENAME_SIZE, "the rename's share of pReadCache");
#define RW ((struct rename_work *)(void *)(pReadCache + READCACHE_RENAME))

// out gets sidecar s's path for the game file name (the folder's GAME_NAME_MAX
// + 24 bytes); 0 if it has none for console, or it is too long to keep
static int side_path(char *out, const struct sidecar *s, const char *name, u32 console)
{
	char file[SAVE_NAME_MAX + 8];
	if (s->kind == SIDE_SAVE)
		save_name(file, name, console);
	else if (s->kind == SIDE_EXT && console == LIB_GBA)
		name_with_ext(file, name, s->ext);
	else if (s->kind == SIDE_ART && strlen(name) + sizeof(ART_CUSTOM_EXT) <= sizeof(file))
		snprintf(file, sizeof(file), "%s%s", name, ART_CUSTOM_EXT);
	else
		return 0;
	u32 n = (u32)snprintf(out, GAME_NAME_MAX + 24, "%s/%s", s->dir, file);
	return n < GAME_NAME_MAX + 24;
}

static int there(const char *path)
{
	return f_stat(path, &fileinfo) == FR_OK;
}

// The UTF-8 character at *s, moving *s past it (a broken byte as itself)
static u32 next_char(const char **s)
{
	const u8 *p = (const u8 *)*s;
	u32 c = *p++, more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
	c &= more ? 0x3Fu >> more : 0xFFu;
	for (; more && (*p & 0xC0) == 0x80; more--)
		c = c << 6 | (*p++ & 0x3F);
	*s = (const char *)p;
	return c;
}

// 1 if two names are one name to the card: equal but for case, as FatFs
// compares them (ff_wtoupper), so a name and its new case are one file
static int same_name(const char *a, const char *b)
{
	while (*a && *b)
		if (ff_wtoupper(next_char(&a)) != ff_wtoupper(next_char(&b)))
			return 0;
	return !*a && !*b;
}

// 1 if another game in the library has the save file save (path's own is
// left out): its records' file names, a save name each
static int there_shared(const char *path, const char *save)
{
	struct rename_work *w = RW;
	for (u32 rec = 0, n = library_records(); rec < n; rec++) {
		u32 flags = library_flags(rec);
		if (flags & LIB_F_GONE)
			continue;
		save_name(w->from, library_name(rec), flags & LIB_F_CONSOLE);
		if (same_name(w->from, save) && library_path(rec, w->to, sizeof(w->to)) && strcmp(w->to, path) != 0)
			return 1;
	}
	return 0;
}

// The walk for another game with the save (share_walk): the folders open on
// the way down, the entry read and the path of the folder it's in
#define SHARE_DEPTH 8  // as the library's walk; a deeper folder can't be checked
struct share_walk {
	DIR dirs[SHARE_DEPTH];  // first: word-aligned
	FILINFO fi;
	char path[USERDATA_PATH_MAX];
	char save[SAVE_NAME_MAX];
	u16 len[SHARE_DEPTH];
};
_Static_assert(sizeof(struct share_walk) <= READCACHE_SHARE_SIZE, "the share walk's part of pReadCache");
_Static_assert(READCACHE_SHARE + READCACHE_SHARE_SIZE <= READCACHE_RENAME, "the share walk reaches the rename's");
#define SW ((struct share_walk *)(void *)(pReadCache + READCACHE_SHARE))

enum {
	SHARE_NONE,
	SHARE_FOUND,
	SHARE_UNSURE
};

// Whether a game file other than path has the save file save, on the whole
// card: hidden folders and games the index doesn't hold too (past its cap,
// names too long). SHARE_UNSURE when a folder can't be read or is too deep.
static u32 share_walk(const char *path, const char *save)
{
	struct share_walk *sw = SW;
	u32 start = cycles_now(), files = 0, result = SHARE_NONE;
	s32 depth = 0;
	if (f_opendir(&sw->dirs[0], "/") != FR_OK)
		return SHARE_UNSURE;
	sw->path[0] = 0;
	sw->len[0] = 0;
	for (;;) {
		DIR *dir = &sw->dirs[depth], *sub = depth + 1 < SHARE_DEPTH ? &sw->dirs[depth + 1] : NULL;
		FRESULT fr = f_readdir_sub(dir, &sw->fi, sub);
		const char *name = sw->fi.fname;
		if (fr != FR_OK) {
			tlog("rename: %s can't be read (FatFs %u): its games' saves can't be checked",
			     depth ? sw->path : "/", (unsigned)fr);
			result = SHARE_UNSURE;
			break;
		}
		if (!name[0]) {  // the folder is done
			f_closedir(dir);
			if (depth-- == 0)
				break;
			sw->path[sw->len[depth]] = 0;
			continue;
		}
		if (sw->fi.fattrib & 0x08)
			continue;  // a volume label
		u32 n = (u32)strlen(name), len = sw->len[depth];
		if (sw->fi.fattrib & AM_DIR) {
			if (!sub || !sub->obj.fs || len + 1 + n >= sizeof(sw->path)) {
				if (sub && sub->obj.fs)
					f_closedir(sub);
				tlog("rename: %s/%s is too deep to check for its save", sw->path, name);
				result = SHARE_UNSURE;
				break;
			}
			sw->path[len] = '/';
			memcpy(sw->path + len + 1, name, n + 1);
			sw->len[++depth] = (u16)(len + 1 + n);
			continue;
		}
		u32 console = library_console_of(name);
		if (console == LIB_CONSOLE_NONE)
			continue;
		files++;
		save_name(sw->save, name, console);
		if (!same_name(sw->save, save))
			continue;
		// The file itself: its folder and name are path's
		const char *slash = strrchr(path, '/');
		if ((u32)(slash - path) == len && !strncmp(path, sw->path, len) && same_name(slash + 1, name))
			continue;
		tlog("rename %s: %s/%s has its save %s", path, sw->path, name, save);
		result = SHARE_FOUND;
		break;
	}
	while (result != SHARE_NONE && depth >= 0)
		f_closedir(&sw->dirs[depth--]);  // the folders the walk left open
	tlog("rename: %lu games on the card checked for the save in %lu ms", (unsigned long)files,
	     (unsigned long)(cycles_since(start) / 16780u));
	return result;
}

// 1 if a new name for a file is one the card and the menu keep
static u32 name_check(const char *name)
{
	u32 len = (u32)strlen(name);
	if (!len || len > GAME_NAME_MAX)
		return ACTION_RENAME_BAD;
	for (const char *c = name; *c; c++)
		if ((u8)*c < 0x20 || *c == 0x7F || strchr("\\/:*?\"<>|", *c))
			return ACTION_RENAME_BAD;
	if (name[0] == '.')
		return ACTION_RENAME_HIDDEN;  // the hidden rule would take it off the lists
	if (name[len - 1] == '.' || name[len - 1] == ' ')
		return ACTION_RENAME_BAD;  // FatFs drops them: the file would get another name than the lists
	return ACTION_RENAMED;
}

struct journal_text {
	const char *from, *to;
};

static u32 journal_fill(void *ctx, u32 pos, char *buf, u32 max)
{
	const struct journal_text *j = ctx;
	u32 a = (u32)strlen(j->from), b = (u32)strlen(j->to), len = a + b + 2, n = 0;
	for (; pos + n < len && n < max; n++) {
		u32 at = pos + n;
		buf[n] = at < a ? j->from[at] : at == a ? '\t' : at < len - 1 ? j->to[at - a - 1] : '\n';
	}
	return n;
}

static int journal_write(const char *from, const char *to)
{
	struct journal_text j = {from, to};
	return safefile_write(RENAME_JOURNAL, RENAME_JOURNAL_MAX, journal_fill, &j);
}

static void journal_clear(void)
{
	f_unlink(RENAME_JOURNAL);
	f_unlink(RENAME_JOURNAL ".bak");
}

// Moves sidecar i of game name to game to_name, for console; FR_OK too when
// it has none
u8 action_rename_fail_file;

static FRESULT side_move(u32 i, const char *name, const char *to_name, u32 console)
{
	struct rename_work *w = RW;
	if (!side_path(w->from, &sidecars[i], name, console) || !side_path(w->to, &sidecars[i], to_name, console))
		return FR_OK;
	FRESULT res = action_rename_fail_file == i + 1 ? FR_DENIED : f_rename(w->from, w->to);
	tlog("rename: %s to %s (FatFs %u)", w->from, w->to, (unsigned)res);
	return res;
}

u32 action_rename_file(const char *path, const char *new_name)
{
	struct rename_work *w = RW;
	u32 why = name_check(new_name);
	const char *slash = strrchr(path, '/');
	if (why != ACTION_RENAMED || path[0] != '/' || !slash || !slash[1] || strlen(slash + 1) > GAME_NAME_MAX)
		return why != ACTION_RENAMED ? why : ACTION_RENAME_BAD;
	u32 dir_len = (u32)(slash - path), console = library_console_of(slash + 1);
	memcpy(w->name, slash + 1, strlen(slash + 1) + 1);
	if (library_console_of(new_name) != console ||
	    (u32)snprintf(w->new_path, sizeof(w->new_path), "%.*s/%s", (int)dir_len, path, new_name) >=
		    sizeof(w->new_path) ||
	    !userdata_path_ok(w->new_path))
		return ACTION_RENAME_BAD;
	if (!strcmp(w->name, new_name))
		return ACTION_RENAMED;  // nothing to do
	if (f_stat(path, &fileinfo) == FR_OK && (fileinfo.fattrib & AM_DIR))
		return ACTION_RENAME_BAD;  // a folder: what is in it wouldn't follow
	tlog_open_now();  // the file may be the log the background open holds
	int case_only = same_name(w->name, new_name);
	if (console != LIB_CONSOLE_NONE) {
		// Its save must keep the record's name: the copy from the cart
		// would come back under it
		// Its save must keep the record's name, and no other save may be
		// waiting under the new one: the copy from the cart comes back
		// under the record's name
		char save[SAVE_NAME_MAX];
		save_name(save, new_name, console);
		if (strlen(save) > SAV_INFO_NAME_MAX)
			return ACTION_RENAME_BAD;  // the pending-save record couldn't name it
		int pending = pending_save_named(save);
		save_name(save, w->name, console);
		if (pending || pending_save_named(save)) {
			tlog("rename %s: a save still waits to be copied", path);
			return ACTION_RENAME_PENDING;
		}
		int any = 0;
		for (u32 i = 0; i < SIDECARS; i++) {
			w->has[i] = side_path(w->from, &sidecars[i], w->name, console) && there(w->from);
			any |= w->has[i] && !sidecars[i].cache;
		}
		// A save another game shares (the same name in another folder, or
		// a .gb and .gbc of one name) stays with it: moving it, or its save
		// type or states, would take them away. The index first, then the
		// whole card (hidden folders, games past the index's cap); when a
		// folder can't be checked, nothing moves
		u32 shared = SHARE_NONE;
		if (!case_only && any)
			shared = there_shared(path, save) ? SHARE_FOUND : share_walk(path, save);
		if (shared != SHARE_NONE) {
			tlog("rename %s: %s its save %s", path,
			     shared == SHARE_FOUND ? "another game uses" : "can't check who else uses", save);
			return ACTION_RENAME_SHARED;
		}
		// Nothing lands on another game's file, its own or one a deleted
		// game left (a new case of its own name keeps its own files)
		for (u32 i = 0; i < SIDECARS; i++) {
			const struct sidecar *s = &sidecars[i];
			if (s->cache || case_only || !side_path(w->to, s, new_name, console))
				continue;
			if (there(w->to)) {
				tlog("rename %s: %s is there already", path, w->to);
				w->taken = (u8)i;
				return ACTION_RENAME_SIDE_TAKEN;
			}
		}
	}
	if (!journal_write(path, w->new_path)) {
		tlog("rename %s: the journal can't be written", path);
		return ACTION_RENAME_FAILED;
	}
	FRESULT res = f_rename(path, w->new_path);
	if (res != FR_OK) {
		tlog("rename %s to %s: FatFs %u", path, new_name, (unsigned)res);
		journal_clear();
		return res == FR_EXIST          ? ACTION_RENAME_TAKEN
		       : res == FR_INVALID_NAME ? ACTION_RENAME_BAD
						: ACTION_RENAME_FAILED;
	}
	tlog("rename: %s to %s", path, new_name);
	for (u32 i = 0; console != LIB_CONSOLE_NONE && i < SIDECARS; i++) {
		const struct sidecar *s = &sidecars[i];
		// A cache under the new name, another game's (a deleted one's),
		// goes: the next launch makes it again
		if (s->cache && !case_only && side_path(w->to, s, new_name, console) && there(w->to)) {
			res = f_unlink(w->to);
			tlog("rename: %s goes (FatFs %u)", w->to, (unsigned)res);
		}
		if (!w->has[i] || side_move(i, w->name, new_name, console) == FR_OK)
			continue;
		// It didn't move: what did goes back, the game first-to-last undone
		while (i-- > 0)
			if (w->has[i])
				side_move(i, new_name, w->name, console);
		res = f_rename(w->new_path, path);
		tlog("rename %s: undone (FatFs %u)", path, (unsigned)res);
		journal_clear();
		return ACTION_RENAME_FAILED;
	}
	if (console != LIB_CONSOLE_NONE && w->has[SIDECARS - 1])
		boxart_forget();  // its listing of the card's art follows
	u32 partly = !gamename_store_move(path, w->new_path);
	partly |= !userdata_rename(path, w->new_path);
	journal_clear();
	if (console != LIB_CONSOLE_NONE)
		library_renamed(path, new_name);
	else
		library_changed();  // the card's files: the walk counts them again
	return partly ? ACTION_RENAME_PARTLY : ACTION_RENAMED;
}

// 1 if path was a second name of keep's file (a cut inside f_rename), now
// removed without touching the file
static int drop_alias(const char *path, const char *keep)
{
	FRESULT res = f_unlink_alias(path, keep);
	if (res == FR_OK)
		tlog("rename: %s was a second name of %s: it goes, the file stays", path, keep);
	else if (res != FR_DENIED)
		tlog_error("rename: %s: can't drop a second name (FatFs %u)", path, (unsigned)res);
	return res == FR_OK;
}

u32 action_rename_taken(void)
{
	return RW->taken;
}

void action_rename_recover(void)
{
	struct rename_work *w = RW;
	char *line = (char *)pReadCache;  // the stage: nothing runs beside the boot
	if (safefile_open(RENAME_JOURNAL, RENAME_JOURNAL_MAX) == SAFEFILE_NONE)
		return;
	int n = safefile_line(line, 2 * USERDATA_PATH_MAX);
	safefile_close();
	char *tab = n > 0 ? memchr(line, '\t', (u32)n) : NULL;
	u32 la = tab ? (u32)(tab - line) : 0, lb = tab ? (u32)n - la - 1 : 0;
	w->old_path[0] = w->new_path[0] = 0;
	if (tab && la < USERDATA_PATH_MAX && lb < USERDATA_PATH_MAX) {
		memcpy(w->old_path, line, la);
		w->old_path[la] = 0;
		memcpy(w->new_path, tab + 1, lb);
		w->new_path[lb] = 0;
	}
	const char *a = strrchr(w->old_path, '/'), *b = strrchr(w->new_path, '/');
	if (!a || !b || strlen(a + 1) > GAME_NAME_MAX || strlen(b + 1) > GAME_NAME_MAX) {
		tlog_error("rename: %s can't be read: it goes", RENAME_JOURNAL);
		journal_clear();
		return;
	}
	memcpy(w->name, a + 1, strlen(a + 1) + 1);
	u32 console = library_console_of(w->name);
	int old_there = there(w->old_path), new_there = there(w->new_path);
	int case_only = same_name(w->name, b + 1);  // one entry: both paths find it
	if (!old_there && !new_there) {
		tlog("rename: %s, cut short, is gone: the journal goes", w->old_path);
		journal_clear();
		return;
	}
	if (old_there && new_there && !case_only) {
		// Both names: the cut came inside f_rename, which writes the new
		// entry before it removes the old, so they share one cluster chain
		// (f_unlink would free it under the new name); or two files, the
		// new name another game's, and nothing moved
		if (!drop_alias(w->old_path, w->new_path)) {
			tlog("rename: %s and %s are two files: nothing was renamed", w->old_path, w->new_path);
			journal_clear();
			return;
		}
		old_there = 0;
	}
	int forward = !old_there || case_only;
	tlog("rename: a rename of %s to %s was cut short: %s", w->old_path, b + 1, forward ? "finished" : "undone");
	// Each file still to move moves (forward), or each that moved goes back;
	// one left under both names (a cut inside its f_rename) loses the name
	// it isn't going to
	for (u32 i = 0; console != LIB_CONSOLE_NONE && i < SIDECARS; i++) {
		if (sidecars[i].cache || !side_path(w->from, &sidecars[i], w->name, console) ||
		    !side_path(w->to, &sidecars[i], b + 1, console))
			continue;
		int from = there(w->from), to = there(w->to);
		if (from && to && !case_only)
			drop_alias(forward ? w->from : w->to, forward ? w->to : w->from);
		else if (forward ? from && !to : to && !from)
			side_move(i, forward ? w->name : b + 1, forward ? b + 1 : w->name, console);
	}
	if (forward) {
		gamename_store_move(w->old_path, w->new_path);
		userdata_rename(w->old_path, w->new_path);
	}
	journal_clear();
}

void action_nor_launch(u32 index, u32 hard_reset)
{
	if (index >= game_total_NOR) {
		tlog_error("launch: no NOR game %lu of %lu", (unsigned long)index, (unsigned long)game_total_NOR);
		return;
	}
	session_end();
	NOR_launch(index, hard_reset);  // returns only if it failed
}

int action_nor_delete(void)
{
	action_nor_read();
	u32 had = game_total_NOR;
	if (!had)
		return 0;
	// The game leaves Recent too, unless another of its name is still on
	// the NOR
	char name[sizeof(pNorFS[0].filename)];
	snprintf(name, sizeof(name), "%s", pNorFS[had - 1].filename);
	Delete_latest_NOR();
	if (game_total_NOR < had && NOR_game_named(name) >= game_total_NOR)
		userdata_forget_nor(name);
	return game_total_NOR < had;
}

void action_nor_format(void)
{
	FormatNor();  // behind its progress dialog
	userdata_forget_nor(NULL);  // no NOR game is left
	action_nor_read();
}
