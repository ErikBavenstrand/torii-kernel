// What the pages do to games and the game NOR (actions.h).
#include <stdio.h>
#include <string.h>

#include "actions.h"
#include "ezkernel.h"
#include "library.h"
#include "NORflash_OP.h"
#include "tlog.h"
#include "userdata.h"
#include "ui/dialog.h"
#include "ui/ui.h"

// The launch path's folder and file name (ezkernelnew.c) are the game's
// while it starts; what they held comes back after (the SD card page keeps
// its folder there)
extern TCHAR currentpath[MAX_path_len];
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

u32 action_launch(const char *path, u32 how, u32 save_type, u32 hard_reset)
{
	if (!copy_path(path) || how >= ACTION_HOWS) {
		tlog_error("launch: can't take %.40s (how %lu)", path, (unsigned long)how);
		return ACTION_NOTHING;
	}
	const char *nor = userdata_nor_name(path_copy);
	if (nor)
		return launch_nor_entry(nor, hard_reset);
	const char *slash = strrchr(path_copy, '/');
	u32 dir_len = slash ? (u32)(slash - path_copy) : 0;
	if (!slash || path_copy[0] != '/' || !slash[1] || dir_len >= MAX_path_len || strlen(slash + 1) > GAME_NAME_MAX) {
		tlog_error("launch %s: not a file's full path", path_copy);
		return ACTION_NOTHING;
	}
	memcpy(saved_path, currentpath, MAX_path_len);
	memset(currentpath, 0, MAX_path_len);
	memcpy(currentpath, path_copy, dir_len ? dir_len : 1);  // the root: "/"
	snprintf(current_filename, sizeof(current_filename), "%s", slash + 1);

	u32 res = ACTION_NOTHING;
	struct game_pick game = {.name = current_filename};
	if (!Game_pick(&game)) {
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
	}
	memcpy(currentpath, saved_path, MAX_path_len);
	f_chdir(currentpath[0] ? currentpath : "/");
	return res;
}

int action_delete_file(const char *path)
{
	if (!copy_path(path) || path_copy[0] != '/')
		return 0;
	tlog_open_now();  // the file may be the log the background open holds
	FRESULT res = f_unlink(path_copy);
	if (res != FR_OK) {
		tlog("delete %s: FatFs %u", path_copy, (unsigned)res);
		return 0;
	}
	tlog("delete: %s", path_copy);
	tlog_card_freed();
	userdata_forget(path_copy);
	library_forget(path_copy);
	return 1;
}

void action_nor_launch(u32 index, u32 hard_reset)
{
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
