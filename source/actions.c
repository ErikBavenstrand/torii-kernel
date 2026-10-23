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
#include "gamesave.h"
#include "lang.h"
#include "library.h"
#include "NORflash_OP.h"
#include "safefile.h"
#include "saveslot.h"
#include "tlog.h"
#include "userdata.h"
#include "engine/cycles.h"
#include "engine/input.h"
#include "screens/slot_picker.h"
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

// Which slot the launch plays (screens/slot_picker.h): the picker if the game
// has two or more, or Select is held (a one-slot game goes straight in). rom
// is the game's save path name (gamesave.h), bytes its save's size (0: it has
// no save, so no slots), title the picker's. 1 if the launch stops (B, or a
// failure shown); the menu's session is over then, as after a failed launch.
static int slot_choose(const char *rom, u32 bytes, const char *title)
{
	if (!bytes)
		return 0;
	int force = ui_active() && (ui_input()->held & INPUT_SELECT) != 0;
	if (!slot_picker_wanted(rom, force))
		return 0;
	if (slot_picker_run(rom, title, bytes) == SLOT_PICKER_LAUNCH)
		return 0;
	tlog("launch: no slot chosen, nothing started");
	session_end();
	return 1;
}

static void launch_nor_entry(const char *name, u32 hard_reset)
{
	action_nor_read();
	u32 game = NOR_game_named(name);
	if (game >= game_total_NOR) {
		// Deleted, or written over by another kernel: it leaves Recent, as a
		// file gone from the card does
		tlog("launch %s: not on the game NOR", name);
		userdata_forget_nor(name);
		error(1);
		return;
	}
	action_nor_launch(game, hard_reset);
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

void action_launch(const char *path, u32 how, u32 save_type, u32 hard_reset)
{
	if (!copy_path(path) || how > ACTION_DEFAULT) {
		tlog_error("launch: can't take %.40s (how %lu)", path, (unsigned long)how);
		return;
	}
	const char *nor = userdata_nor_name(path_copy);
	if (nor) {
		launch_nor_entry(nor, hard_reset);
		return;
	}
	if (!game_enter("launch"))
		return;

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
		if (how < ACTION_NOR) {  // WRITE TO NOR doesn't load a save to the cart
			char rom[GAMESAVE_PATH_BYTES];
			if (gamesave_rom_path_in(rom, sizeof(rom), currentpath, game.name) >= 0 &&
			    slot_choose(rom, Game_save_bytes(&game), game.name)) {
				game_leave();
				return;
			}
		}
		session_end();
		// A write to NOR lists its game in pNorFS but leaves the count:
		// the NOR page and home's count read them
		if (Game_launch(&game, how, hard_reset))
			action_nor_read();
	}
	game_leave();
}

u32 action_game_facts(const char *path, struct action_facts *out)
{
	memset(out, 0, sizeof(*out));
	out->save_type = ACTION_SAVE_TYPES;
	if (!copy_path(path) || userdata_nor_name(path_copy) || !game_enter("facts"))
		return 0;
	struct game_pick game = {.name = current_filename};
	u32 ok = Game_pick(&game);  // reads the game's record and looks for a cheat file
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
	// The game's record (gamerec.h), read by the next launch's Game_pick
	u8 res = Make_mde_file(current_filename, (u8)save_type);
	tlog("save type of %s: %lu (%u)", path_copy, (unsigned long)save_type, res);
	game_leave();
	return res == 0;
}

int action_set_boot_mode(const char *path, u32 boot_mode)
{
	if (boot_mode >= ACTION_BOOTS || !copy_path(path) || userdata_nor_name(path_copy) || !game_enter("boot mode"))
		return 0;
	u8 res = Make_mde_boot(current_filename, (u8)boot_mode);
	tlog("boot mode of %s: %lu (%u)", path_copy, (unsigned long)boot_mode, res);
	game_leave();
	return res == 0;
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
// A game's other files are named after it. Its saves are in folders named
// after its path (saveslot.h): its folder of slots and record under
// /torii/saves and its history under /torii/backups, which the rename moves
// as they are (the folder's name is the game's file name, so one rename each),
// or, with the Save location setting at "next to the ROM", the files
// <stem>.<slot>.sav in its own folder, as many as it has. Its save states and
// cheats (name_with_ext) and patch cache are in folders at the card's root,
// and its custom box art (B22) is in /torii/art. They move with it, and the
// save state and the rest never land on another game's: the rename is
// refused first if one would. A move that fails puts back what
// moved. A journal, /torii/rename.txt (B5's safe file: the old path, a tab,
// the new one), lasts from before the first move to after the lists follow:
// a power cut in between is finished, or undone, at the next boot
// (action_rename_recover).

enum {
	SIDE_DIR,     // dir and the game's path: a folder of its own (saves, history)
	SIDE_EXT,     // name_with_ext's, ext; GBA games only
	SIDE_ART,     // the file name and ".bin"
	SIDE_BESIDE,  // the .sav files next to the ROM (any number)
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
// In the order of actions.h's ACTION_FILE_*
static const struct sidecar sidecars[] = {
	{SAVESLOT_SAVES_DIR, SIDE_DIR, 0, ""},
	{SAVESLOT_BACKUPS_DIR, SIDE_DIR, 0, ""},
	{"/RTS", SIDE_EXT, 0, "rts"},
	{"/CHEAT", SIDE_EXT, 0, "cht"},
	{"/PATCH", SIDE_EXT, 1, "pat"},
	{ART_CUSTOM_DIR, SIDE_ART, 0, ""},
	{"", SIDE_BESIDE, 0, ""},
};
#define SIDECARS (sizeof(sidecars) / sizeof(*sidecars))
#define SIDE_ART_AT 5  // the custom art's place in sidecars

// The rename's RAM, in pReadCache (READCACHE_RENAME): what lasts while it
// calls the names file, the user's lists and the library, which borrow
// shares before it
#define SIDE_PATH_MAX (USERDATA_PATH_MAX + 24)  // a sidecar's path: its folder, a game's path in it
struct rename_work {
	char old_path[USERDATA_PATH_MAX];  // the journal's, at boot
	char new_path[USERDATA_PATH_MAX];
	char name[GAME_NAME_MAX + 1];  // the old file name
	char dir[USERDATA_PATH_MAX];  // its folder ("" for the root)
	char from[SIDE_PATH_MAX];  // a file's path
	char to[SIDE_PATH_MAX];
	u8 has[SIDECARS];  // the old one is there
	u8 taken;          // ACTION_RENAME_SIDE_TAKEN: the sidecar in the way (ACTION_FILE_*)
};
_Static_assert(sizeof(struct rename_work) <= READCACHE_RENAME_SIZE, "the rename's share of pReadCache");
#define RW ((struct rename_work *)(void *)(pReadCache + READCACHE_RENAME))

static FRESULT drop_alias(const char *path, const char *keep);

// out gets sidecar s's path for the game file name (SIDE_PATH_MAX bytes);
// 0 if it has none for console, or it is too long to keep. A SIDE_DIR's
// takes the game's folder (RW->dir), a SIDE_BESIDE has no single path.
static int side_path(char *out, const struct sidecar *s, const char *name, u32 console)
{
	char file[GAME_NAME_MAX + 8];
	if (s->kind == SIDE_DIR)
		return (u32)snprintf(out, SIDE_PATH_MAX, "%s%s/%s", s->dir, RW->dir, name) < SIDE_PATH_MAX;
	if (s->kind == SIDE_BESIDE)
		return 0;
	if (s->kind == SIDE_EXT && console == LIB_GBA)
		name_with_ext(file, name, s->ext);
	else if (s->kind == SIDE_ART && strlen(name) + sizeof(ART_CUSTOM_EXT) <= sizeof(file))
		snprintf(file, sizeof(file), "%s%s", name, ART_CUSTOM_EXT);
	else
		return 0;
	u32 n = (u32)snprintf(out, SIDE_PATH_MAX, "%s/%s", s->dir, file);
	return n < SIDE_PATH_MAX;
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

// --- Saves beside their ROM: <stem>.<slot>.sav in the game's folder (RW->dir)

#define BESIDE_MAX 16  // the most a rename moves; a game has far fewer slots
#define BESIDE_SUFFIX 112  // ".<slot>.sav": a slot's name is up to 96 bytes
struct beside_list {
	u32 count;
	char suffix[BESIDE_MAX][BESIDE_SUFFIX];
};
_Static_assert(sizeof(struct beside_list) <= READCACHE_SHARE_SIZE, "the beside list's part of pReadCache");
#define BL ((struct beside_list *)(void *)(pReadCache + READCACHE_SHARE))

// Bytes of name that are stem (stem_len bytes) as the card compares them, or
// 0 if name doesn't start with it
static u32 prefix_len(const char *name, const char *stem, u32 stem_len)
{
	const char *a = name, *b = stem, *end = stem + stem_len;
	while (b < end) {
		if (!*a || ff_wtoupper(next_char(&a)) != ff_wtoupper(next_char(&b)))
			return 0;
	}
	return (u32)(a - name);
}

static u32 stem_bytes(const char *name)
{
	const char *dot = strrchr(name, '.');
	return dot ? (u32)(dot - name) : (u32)strlen(name);
}

// Lists into BL the suffixes (".Main.sav") of the .sav files in the game's
// folder that start with name's stem and a dot; returns how many
static u32 beside_collect(const char *name)
{
	struct beside_list *l = BL;
	u32 stem = stem_bytes(name);
	DIR dir;
	l->count = 0;
	if (!stem || f_opendir(&dir, RW->dir[0] ? RW->dir : "/") != FR_OK)
		return 0;
	while (f_readdir(&dir, &fileinfo) == FR_OK && fileinfo.fname[0]) {
		const char *f = fileinfo.fname;
		u32 len = (u32)strlen(f), pre = (fileinfo.fattrib & (AM_DIR | 0x08)) ? 0 : prefix_len(f, name, stem);
		if (!pre || f[pre] != '.' || len - pre < 6 || f[len - 4] != '.' || (f[len - 3] | 32) != 's' ||
		    (f[len - 2] | 32) != 'a' || (f[len - 1] | 32) != 'v')
			continue;
		if (l->count < BESIDE_MAX && len - pre < BESIDE_SUFFIX)
			memcpy(l->suffix[l->count++], f + pre, len - pre + 1);
		else
			tlog("rename: %s is beyond the saves a rename moves beside %s", f, name);
	}
	f_closedir(&dir);
	return l->count;
}

// Writes the path of the save beside the ROM name with suffix into out
static int beside_path(char *out, const char *name, const char *suffix)
{
	u32 d = (u32)strlen(RW->dir), n = stem_bytes(name), x = (u32)strlen(suffix);
	if (d + 1 + n + x >= SIDE_PATH_MAX)
		return 0;
	memcpy(out, RW->dir, d);
	out[d] = '/';
	memcpy(out + d + 1, name, n);
	memcpy(out + d + 1 + n, suffix, x + 1);
	return 1;
}

// Moves the saves beside the ROM name to to_name's stem, each keeping its
// slot's name. With alias, one found under both names (a cut inside its
// f_rename) loses the name it isn't going to keep; FR_OK once all moved
static FRESULT beside_move(const char *name, const char *to_name, int alias)
{
	struct beside_list *l = BL;
	char *from = RW->from, *to = RW->to;
	if (!beside_collect(name))
		return FR_OK;
	for (u32 i = 0; i < l->count; i++) {
		if (!beside_path(from, name, l->suffix[i]) || !beside_path(to, to_name, l->suffix[i]))
			return FR_INVALID_NAME;
		if (alias && there(to)) {
			drop_alias(from, to);  // (two files: the one under the new name is another's, it stays)
			continue;
		}
		FRESULT res = f_rename(from, to);
		tlog("rename: %s to %s (FatFs %u)", from, to, (unsigned)res);
		if (res != FR_OK)
			return res;
	}
	return FR_OK;
}

// 1 if the game name has sidecar i's files (any, for the saves beside it)
static int side_exists(u32 i, const char *name, u32 console)
{
	struct rename_work *w = RW;
	if (sidecars[i].kind == SIDE_BESIDE)
		return beside_collect(name) != 0;
	return side_path(w->from, &sidecars[i], name, console) && there(w->from);
}

// 1 if a folder of sidecar i's has nothing in it and goes
static int side_empty_dir_gone(const char *path)
{
	DIR dir;
	if (f_opendir(&dir, path) != FR_OK)
		return 0;
	int empty = f_readdir(&dir, &fileinfo) == FR_OK && !fileinfo.fname[0];
	f_closedir(&dir);
	return empty && f_unlink(path) == FR_OK;
}

// 1 if sidecar i's files for the new name are there already, another game's
// (or a deleted one's): a folder with nothing in it, which a cut or a save
// that was never made left, is not
static int side_taken(u32 i, const char *to_name, u32 console)
{
	struct rename_work *w = RW;
	if (sidecars[i].kind == SIDE_BESIDE)
		return beside_collect(to_name) != 0;
	if (!side_path(w->to, &sidecars[i], to_name, console) || !there(w->to))
		return 0;
	return !(sidecars[i].kind == SIDE_DIR && (fileinfo.fattrib & AM_DIR) && side_empty_dir_gone(w->to));
}

// 1 if the pending-save record (a save not copied from the cart yet) is for
// any save of the game dir/name, saved in either place
static int pending_for(const char *dir, const char *name)
{
	char rom[GAMESAVE_PATH_BYTES], prefix[GAMESAVE_PATH_BYTES];
	if (gamesave_rom_path_in(rom, sizeof(rom), dir, name) < 0)
		return 0;
	int n = saveslot_game_dir(prefix, sizeof(prefix), rom);
	if (n > 0 && (u32)n + 2 < sizeof(prefix)) {
		prefix[n] = '/';
		prefix[n + 1] = 0;
		if (pending_save_under(prefix))
			return 1;
	}
	n = saveslot_save_path(prefix, sizeof(prefix), SAVESLOT_BESIDE_ROM, rom, "x");
	if (n > 5) {
		prefix[n - 5] = 0;  // "<folder>/<stem>." less "x.sav"
		return pending_save_under(prefix);
	}
	return 0;
}

// out gets name less its extension: what a game's files that are named after
// it, whatever its console, share (a Game.gba and a Game.gbc have the saves
// beside them, and Game.sav and the like, in common)
static void stem_name(char *out, const char *name)
{
	u32 n = stem_bytes(name);
	memcpy(out, name, n);
	out[n] = 0;
}

// 1 if another game in the library has the stem save (path's own is
// left out): its records' file names, a stem each
static int there_shared(const char *path, const char *save)
{
	struct rename_work *w = RW;
	for (u32 rec = 0, n = library_records(); rec < n; rec++) {
		u32 flags = library_flags(rec);
		if (flags & LIB_F_GONE)
			continue;
		stem_name(w->from, library_name(rec));
		if (same_name(w->from, save) && library_path(rec, w->to, sizeof(w->to)) && strcmp(w->to, path) != 0)
			return 1;
	}
	return 0;
}

// The walk for another game with the save (share_walk): the folders open on
// the way down, the entry read and the path of the folder it's in
#define SHARE_DIALOG_CYCLES (16780000u / 3)  // a walk this long (0.33 s) gets a progress dialog
#define SHARE_DEPTH 16  // twice the library's walk; a deeper folder can't be checked
struct share_walk {
	DIR dirs[SHARE_DEPTH];  // first: word-aligned
	FILINFO fi;
	char path[USERDATA_PATH_MAX];
	char save[GAME_NAME_MAX + 1];
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

// Whether a game file other than path has the save file save or save2
// (the name it would move to), on the whole card: hidden folders and games
// the index doesn't hold too (past its cap, names too long). SHARE_UNSURE
// when a folder can't be read or is too deep.
static u32 share_walk(const char *path, const char *save, const char *save2)
{
	struct share_walk *sw = SW;
	u32 start = cycles_now(), files = 0, result = SHARE_NONE, shown = 0;
	s32 depth = 0;
	if (f_opendir(&sw->dirs[0], "/") != FR_OK)
		return SHARE_UNSURE;
	sw->path[0] = 0;
	sw->len[0] = 0;
	for (;;) {
		DIR *dir = &sw->dirs[depth], *sub = depth + 1 < SHARE_DEPTH ? &sw->dirs[depth + 1] : NULL;
		FRESULT fr = f_readdir_sub(dir, &sw->fi, sub);
		const char *name = sw->fi.fname;
		if (fr == FR_INT_ERR && depth > 0) {
			// A damaged folder, which the library's walk leaves out too:
			// skipped, so one can't block every rename on the card
			tlog("rename: %s can't be read (FatFs %u): its games aren't checked for the save", sw->path,
			     (unsigned)fr);
			f_closedir(dir);
			sw->path[sw->len[--depth]] = 0;
			continue;
		}
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
		if (!(files & 31)) {
			// A long walk says so, so the screen doesn't look frozen; a
			// short one (a small card) shows nothing
			u32 total = library_records() + 1;
			if (!shown && cycles_since(start) > SHARE_DIALOG_CYCLES) {
				dialog_progress_begin(gl_rename->checking, gl_rename->checking_text);
				shown = 1;
			}
			if (shown)
				dialog_progress_set(files < total ? files : total, total, NULL);
		}
		stem_name(sw->save, name);
		if (!same_name(sw->save, save) && !same_name(sw->save, save2))
			continue;
		// The file itself: its folder and name are path's
		const char *slash = strrchr(path, '/');
		if ((u32)(slash - path) == len && !strncmp(path, sw->path, len) && same_name(slash + 1, name))
			continue;
		tlog("rename %s: %s/%s has the save %s", path, sw->path, name, sw->save);
		result = SHARE_FOUND;
		break;
	}
	while (result != SHARE_NONE && depth >= 0)
		f_closedir(&sw->dirs[depth--]);  // the folders the walk left open
	if (shown)
		dialog_progress_end();
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
	if (action_rename_fail_file == i + 1)
		return FR_DENIED;
	if (sidecars[i].kind == SIDE_BESIDE)
		return beside_move(name, to_name, 0);
	if (!side_path(w->from, &sidecars[i], name, console) || !side_path(w->to, &sidecars[i], to_name, console))
		return FR_OK;
	FRESULT res = f_rename(w->from, w->to);
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
	if (dir_len >= sizeof(w->dir))
		return ACTION_RENAME_BAD;
	memcpy(w->dir, path, dir_len);
	w->dir[dir_len] = 0;  // "" for the root
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
		// Its saves must keep the record's name, and no other save may be
		// waiting under the new one: the copy from the cart comes back
		// under the path the record holds
		char rom[GAMESAVE_PATH_BYTES], save_path[GAMESAVE_PATH_BYTES];
		if (gamesave_rom_path_in(rom, sizeof(rom), w->dir, new_name) < 0 ||
		    saveslot_save_path(save_path, sizeof(save_path), SAVESLOT_CENTRAL, rom, SAVESLOT_MAIN) < 0 ||
		    strlen(save_path) > SAV_INFO_NAME_MAX)
			return ACTION_RENAME_BAD;  // the pending-save record couldn't hold its save's path
		char save[GAME_NAME_MAX + 1], save_to[GAME_NAME_MAX + 1];
		stem_name(save_to, new_name);
		stem_name(save, w->name);
		if (pending_for(w->dir, w->name) || pending_for(w->dir, new_name)) {
			tlog("rename %s: a save still waits to be copied", path);
			return ACTION_RENAME_PENDING;
		}
		int any = 0;
		for (u32 i = 0; i < SIDECARS; i++) {
			w->has[i] = side_exists(i, w->name, console);
			// (a game's own folders are its path's: no other game has them)
			any |= w->has[i] && !sidecars[i].cache && sidecars[i].kind != SIDE_DIR;
		}
		// A save another game shares (the same name in another folder, or
		// a .gb and .gbc of one name) stays with it: moving it, or its save
		// type or states, would take them away; and one moved to the save
		// name of another game (that has none yet) would be that game's too.
		// The index first, then the whole card (hidden folders, games past
		// the index's cap); when a folder can't be checked, nothing moves
		if (!case_only && there(w->new_path)) {  // its folder has the name: say that, not the save
			tlog("rename %s: %s is there already", path, w->new_path);
			return ACTION_RENAME_TAKEN;
		}
		u32 shared = SHARE_NONE;
		if (!case_only && any)
			shared = there_shared(path, save) || there_shared(path, save_to)
					 ? SHARE_FOUND
					 : share_walk(path, save, save_to);
		if (shared != SHARE_NONE) {
			tlog("rename %s: %s %s or %s", path,
			     shared == SHARE_FOUND ? "another game uses the save"
						   : "can't check who else uses the save",
			     save, save_to);
			return ACTION_RENAME_SHARED;
		}
		// Nothing lands on another game's file, its own or one a deleted
		// game left (a new case of its own name keeps its own files)
		for (u32 i = 0; i < SIDECARS; i++) {
			if (sidecars[i].cache || case_only || !side_taken(i, new_name, console))
				continue;
			tlog("rename %s: the %s files of the new name are there already", path, sidecars[i].dir);
			w->taken = (u8)i;
			return ACTION_RENAME_SIDE_TAKEN;
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
	if (console != LIB_CONSOLE_NONE && w->has[SIDE_ART_AT])
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

// Drops path, a second name of keep's file (a cut inside f_rename), without
// touching the file: FR_OK once dropped, FR_DENIED if they are two files
static FRESULT drop_alias(const char *path, const char *keep)
{
	FRESULT res = f_unlink_alias(path, keep);
	if (res == FR_OK)
		tlog("rename: %s was a second name of %s: it goes, the file stays", path, keep);
	else if (res != FR_DENIED)
		tlog_error("rename: %s: can't drop a second name (FatFs %u)", path, (unsigned)res);
	return res;
}

// The same after a case-only rename, whose two names a path can't tell
// apart: the names in path's folder that are path's but for case, on its
// file's chain, go. 0 on a card error
static int drop_case_aliases(const char *path)
{
	char dir[USERDATA_PATH_MAX];
	const char *slash = strrchr(path, '/');
	u32 len = slash ? (u32)(slash - path) : 0;
	if (!slash || len >= sizeof(dir))
		return 1;
	memcpy(dir, path, len);
	dir[len ? len : 1] = 0;
	if (!len)
		dir[0] = '/';
	FRESULT res = f_unlink_case_aliases(dir, slash + 1, &fileinfo);
	if (res == FR_OK)
		tlog("rename: another case of %s was a second name of it: it goes, the file stays", path);
	else if (res != FR_NO_FILE && res != FR_NO_PATH)  // (no such folder: nothing there)
		tlog_error("rename: %s: can't drop a second name (FatFs %u)", path, (unsigned)res);
	return res == FR_OK || res == FR_NO_FILE || res == FR_NO_PATH;
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
	u32 old_dir = (u32)(a - w->old_path);
	if (old_dir >= sizeof(w->dir)) {
		tlog_error("rename: %s can't be read: it goes", RENAME_JOURNAL);
		journal_clear();
		return;
	}
	memcpy(w->dir, w->old_path, old_dir);
	w->dir[old_dir] = 0;
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
		FRESULT res = drop_alias(w->old_path, w->new_path);
		if (res == FR_DENIED) {
			tlog("rename: %s and %s are two files: nothing was renamed", w->old_path, w->new_path);
			journal_clear();
		}
		if (res != FR_OK)
			return;  // a card error: the journal stays for the next boot
		old_there = 0;
	}
	if (case_only && !drop_case_aliases(w->new_path))
		return;
	int forward = !old_there || case_only, failed = 0, kept = 1;
	tlog("rename: a rename of %s to %s was cut short: %s", w->old_path, b + 1, forward ? "finished" : "undone");
	// Each file still to move moves (forward), or each that moved goes back;
	// one left under both names (a cut inside its f_rename) loses the name
	// it isn't going to
	for (u32 i = 0; console != LIB_CONSOLE_NONE && i < SIDECARS && !failed; i++) {
		if (sidecars[i].cache)
			continue;
		if (sidecars[i].kind == SIDE_BESIDE) {
			// Each save beside the ROM still under the name it leaves
			if (beside_move(forward ? w->name : b + 1, forward ? b + 1 : w->name, !case_only) != FR_OK)
				failed = forward;
			continue;
		}
		if (!side_path(w->from, &sidecars[i], w->name, console) ||
		    !side_path(w->to, &sidecars[i], b + 1, console))
			continue;
		if (case_only) {
			kept &= drop_case_aliases(w->to);
			continue;
		}
		int from = there(w->from), to = there(w->to);
		if (from && to) {
			FRESULT res = drop_alias(forward ? w->from : w->to, forward ? w->to : w->from);
			kept &= res == FR_OK || res == FR_DENIED;
		} else if ((forward ? from && !to : to && !from) &&
			   side_move(i, forward ? w->name : b + 1, forward ? b + 1 : w->name, console) != FR_OK) {
			failed = forward;  // going back, what stays is the game's still
		}
	}
	if (failed) {
		// A file that won't move (a full card): the rename is undone, as a
		// live rename's is, so the game and its save keep one name
		for (u32 i = 0; i < SIDECARS; i++) {
			if (sidecars[i].cache)
				continue;
			if (sidecars[i].kind == SIDE_BESIDE)
				beside_move(b + 1, w->name, 0);
			else if (side_path(w->from, &sidecars[i], w->name, console) &&
				 side_path(w->to, &sidecars[i], b + 1, console) && there(w->to) && !there(w->from))
				side_move(i, b + 1, w->name, console);
		}
		FRESULT res = f_rename(w->new_path, w->old_path);
		tlog_error("rename: finishing %s's rename failed: undone (FatFs %u)", w->old_path, (unsigned)res);
		if (res == FR_OK)
			journal_clear();
		return;
	}
	if (forward) {
		gamename_store_move(w->old_path, w->new_path);
		userdata_rename(w->old_path, w->new_path);
	}
	if (kept)
		journal_clear();  // else a card error: the next boot looks again
}

void action_nor_launch(u32 index, u32 hard_reset)
{
	if (index >= game_total_NOR) {
		tlog_error("launch: no NOR game %lu of %lu", (unsigned long)index, (unsigned long)game_total_NOR);
		return;
	}
	char rom[GAMESAVE_PATH_BYTES];
	if (gamesave_nor_path(rom, sizeof(rom), pNorFS[index].filename) >= 0 &&
	    slot_choose(rom, Get_savefilesize(pNorFS[index].savemode), pNorFS[index].filename))
		return;
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
