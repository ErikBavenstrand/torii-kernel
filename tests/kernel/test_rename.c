// An SD file renamed by the menu (action_rename_file, source/actions.c):
// the game's save, save type, save states, cheats, patch cache and custom
// art move with it, its custom name, favourite and Recent entry follow it,
// and a rename that would land on another file is refused before anything
// moves. On the emulated cart (tests/assets.sh), with test lists beside the
// user's, never in Diagnostics: it writes the card.
#include <stdio.h>
#include <string.h>

#include <gba_base.h>

#include "ff.h"
#include "../../source/actions.h"
#include "../../source/gamename.h"
#include "../../source/userdata.h"
#include "../../source/engine/stage.h"
#include "torii_test.h"
#include "tests.h"

#define T_RECENT "/torii/t-rn-recent.txt"
#define T_FAV "/torii/t-rn-fav.txt"
#define T_OLD "/torii/t-rn-old.txt"
#define T_DIR "/T18"

extern FATFS EZcardFs;

static int card(void)
{
	if (!EZcardFs.fs_type && f_mount(&EZcardFs, "", 1) != FR_OK)
		return 0;
	return EZcardFs.fs_type != 0;
}

// Writes text (its bytes, NUL-ended) to path, making its folder
static int put(const char *path, const char *text)
{
	FIL f;
	UINT done = 0;
	u32 len = (u32)strlen(text);
	char dir[48];
	const char *slash = strrchr(path, '/');
	snprintf(dir, sizeof(dir), "%.*s", (int)(slash - path), path);
	// The folders on its way, from the root
	for (char *p = dir + 1; *p; p++)
		if (*p == '/') {
			*p = 0;
			f_mkdir(dir);
			*p = '/';
		}
	f_mkdir(dir);
	memcpy(stage_buffer(), text, len);
	if (f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
		return 0;
	int ok = f_write(&f, stage_buffer(), len, &done) == FR_OK && done == len;
	return (f_close(&f) == FR_OK) & ok;
}

// 1 if path holds exactly text
static int holds(const char *path, const char *text)
{
	FIL f;
	UINT done = 0;
	if (f_open(&f, path, FA_READ) != FR_OK)
		return 0;
	int ok = f_read(&f, stage_buffer(), STAGE_BYTES - 1, &done) == FR_OK;
	f_close(&f);
	stage_buffer()[ok ? done : 0] = 0;
	return ok && !strcmp((const char *)stage_buffer(), text);
}

static int there(const char *path)
{
	static FILINFO info EWRAM_BSS;
	return f_stat(path, &info) == FR_OK;
}

// 1 if the text of path (a safe file: its CRC line comes after) has line
static int has_line(const char *path, const char *line)
{
	FIL f;
	UINT done = 0;
	if (f_open(&f, path, FA_READ) != FR_OK)
		return 0;
	f_read(&f, stage_buffer(), STAGE_BYTES - 1, &done);
	f_close(&f);
	stage_buffer()[done] = 0;
	return strstr((const char *)stage_buffer(), line) != NULL;
}

static const char *const files[] = {
	T_DIR "/Game.gba",
	T_DIR "/New.gba",
	T_DIR "/new.gba",
	T_DIR "/Other.gba",
	"/SAVER/Game.sav",
	"/SAVER/New.sav",
	"/SAVER/new.sav",
	"/SAVER/Busy.sav",
	"/SAVER/Game.mde",
	"/SAVER/New.mde",
	"/SAVER/new.mde",
	"/RTS/Game.rts",
	"/RTS/New.rts",
	"/RTS/new.rts",
	"/CHEAT/Game.cht",
	"/CHEAT/New.cht",
	"/CHEAT/new.cht",
	"/PATCH/Game.pat",
	"/PATCH/New.pat",
	"/PATCH/new.pat",
	"/torii/art/custom/Game.gba.bin",
	"/torii/art/custom/New.gba.bin",
	"/torii/art/custom/new.gba.bin",
	T_DIR "/notes.txt",
	T_DIR "/read me.txt",
	GAMENAME_FILE,
	GAMENAME_FILE ".bak",
	T_RECENT,
	T_RECENT ".bak",
	T_FAV,
	T_FAV ".bak",
};

static void tidy(void)
{
	for (u32 i = 0; i < sizeof(files) / sizeof(*files); i++)
		f_unlink(files[i]);
}

void test_rename_file(void)
{
	CHECK(card());
	if (!card())
		return;
	tidy();
	userdata_test_paths(T_RECENT, T_FAV, T_OLD);
	CHECK(put(T_DIR "/Game.gba", "game"));
	CHECK(put("/SAVER/Game.sav", "save"));
	CHECK(put("/SAVER/Game.mde", "mde"));
	CHECK(put("/RTS/Game.rts", "states"));
	CHECK(put("/CHEAT/Game.cht", "cheats"));
	CHECK(put("/PATCH/Game.pat", "patch"));
	CHECK(put("/PATCH/New.pat", "a deleted game's patch"));
	CHECK(put("/torii/art/custom/Game.gba.bin", "art"));
	CHECK(put(T_DIR "/Other.gba", "other"));
	CHECK(put("/SAVER/Busy.sav", "another game's save"));
	CHECK(gamename_store_set(T_DIR "/Game.gba", "My Game"));
	CHECK(userdata_fav_set(T_DIR "/Game.gba", 1));
	CHECK(userdata_recent_add(T_DIR "/Game.gba"));

	// Refused, with nothing moved: names the card or the menu can't take,
	// another console, a file there, another game's save
	CHECK_EQ(action_rename_file(T_DIR "/Game.gba", "a:b.gba"), ACTION_RENAME_BAD);
	CHECK_EQ(action_rename_file(T_DIR "/Game.gba", ".hidden.gba"), ACTION_RENAME_HIDDEN);
	CHECK_EQ(action_rename_file(T_DIR "/Game.gba", "Game.gb"), ACTION_RENAME_BAD);
	CHECK_EQ(action_rename_file(T_DIR "/Game.gba", "Other.gba"), ACTION_RENAME_TAKEN);
	CHECK_EQ(action_rename_file(T_DIR "/Game.gba", "Busy.gba"), ACTION_RENAME_SIDE_TAKEN);
	CHECK_EQ(action_rename_file(T_DIR "/notes.txt", "notes."), ACTION_RENAME_BAD);  // FatFs drops the dot
	// A game with no save of its own doesn't take a save left there
	CHECK(put(T_DIR "/Lone.gba", "lone"));
	CHECK_EQ(action_rename_file(T_DIR "/Lone.gba", "Busy.gba"), ACTION_RENAME_SIDE_TAKEN);
	CHECK(there(T_DIR "/Lone.gba") && !there(T_DIR "/Busy.gba"));
	f_unlink(T_DIR "/Lone.gba");
	CHECK(there(T_DIR "/Game.gba") && there("/SAVER/Game.sav") && !there(T_DIR "/Busy.gba"));
	CHECK(holds("/SAVER/Busy.sav", "another game's save"));

	// The rename: everything moves, the lists follow
	CHECK_EQ(action_rename_file(T_DIR "/Game.gba", "New.gba"), ACTION_RENAMED);
	CHECK(!there(T_DIR "/Game.gba") && holds(T_DIR "/New.gba", "game"));
	CHECK(!there("/SAVER/Game.sav") && holds("/SAVER/New.sav", "save"));
	CHECK(!there("/SAVER/Game.mde") && holds("/SAVER/New.mde", "mde"));
	CHECK(!there("/RTS/Game.rts") && holds("/RTS/New.rts", "states"));
	CHECK(!there("/CHEAT/Game.cht") && holds("/CHEAT/New.cht", "cheats"));
	CHECK(!there("/PATCH/Game.pat") && holds("/PATCH/New.pat", "patch"));  // the stale one went
	CHECK(!there("/torii/art/custom/Game.gba.bin") && holds("/torii/art/custom/New.gba.bin", "art"));
	CHECK(userdata_fav_has(T_DIR "/New.gba") && !userdata_fav_has(T_DIR "/Game.gba"));
	CHECK(!strcmp(userdata_recent_path(0), T_DIR "/New.gba"));
	CHECK(has_line(GAMENAME_FILE, T_DIR "/New.gba\tMy Game\n"));
	CHECK(!has_line(GAMENAME_FILE, T_DIR "/Game.gba"));
	// What the store reads back after a launch (PSRAM overwritten)
	userdata_invalidate();
	CHECK(userdata_fav_has(T_DIR "/New.gba"));
	CHECK(!strcmp(userdata_recent_path(0), T_DIR "/New.gba"));

	// Only the case changes: the same files, under the new case
	CHECK_EQ(action_rename_file(T_DIR "/New.gba", "new.gba"), ACTION_RENAMED);
	CHECK(holds(T_DIR "/new.gba", "game") && holds("/SAVER/new.sav", "save"));
	static FILINFO fi EWRAM_BSS;
	CHECK(f_stat("/SAVER/new.sav", &fi) == FR_OK && !strcmp(fi.fname, "new.sav"));
	CHECK(userdata_fav_has(T_DIR "/new.gba"));
	// The same name: nothing to do
	CHECK_EQ(action_rename_file(T_DIR "/new.gba", "new.gba"), ACTION_RENAMED);

	// A file that isn't a game: just its name (no save looked for)
	CHECK(put(T_DIR "/notes.txt", "notes"));
	CHECK_EQ(action_rename_file(T_DIR "/notes.txt", "read me.txt"), ACTION_RENAMED);
	CHECK(holds(T_DIR "/read me.txt", "notes") && !there(T_DIR "/notes.txt"));

	tidy();
	f_unlink("/torii/art/custom");
	f_unlink("/torii/art");
	f_unlink("/RTS");
	f_unlink("/PATCH");
	f_unlink(T_DIR);
	userdata_test_paths(NULL, NULL, NULL);
}
