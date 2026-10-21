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
	T_DIR "/Fwd2.gba",
	"/SAVER/Fwd.sav",
	"/SAVER/Fwd2.sav",
	T_DIR "/Back.gba",
	"/SAVER/Back.sav",
	"/SAVER/Back2.sav",
	"/torii/rename.txt",
	"/torii/rename.txt.bak",
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
	T_DIR "/Share.gba",
	T_DIR "/Shared.gba",
	"/SAVER/Share.sav",
	"/SAVER/Shared.sav",
	"/.T18h/Share.gba",
	T_DIR "/Roll.gba",
	T_DIR "/Rolled.gba",
	"/SAVER/Roll.sav",
	"/SAVER/Rolled.sav",
	"/SAVER/Roll.mde",
	"/SAVER/Rolled.mde",
	"/RTS/Roll.rts",
	"/RTS/Rolled.rts",
	T_DIR "/Two.gba",
	T_DIR "/Two2.gba",
	"/SAVER/Two.sav",
	"/SAVER/Two2.sav",
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
	CHECK(!there("/torii/rename.txt") && !there("/torii/rename.txt.bak"));  // the journal went

	// A rename the power cut short, at the next boot: the game had moved, so
	// what is left moves and the lists follow
	CHECK(put(T_DIR "/Fwd2.gba", "fwd"));
	CHECK(put("/SAVER/Fwd.sav", "fwd save"));
	CHECK(userdata_fav_set(T_DIR "/Fwd.gba", 1));
	CHECK(put("/torii/rename.txt", T_DIR "/Fwd.gba\t" T_DIR "/Fwd2.gba\n"));
	action_rename_recover();
	CHECK(!there("/SAVER/Fwd.sav") && holds("/SAVER/Fwd2.sav", "fwd save"));
	CHECK(userdata_fav_has(T_DIR "/Fwd2.gba") && !userdata_fav_has(T_DIR "/Fwd.gba"));
	CHECK(!there("/torii/rename.txt"));
	// The game hadn't moved: what had goes back
	CHECK(put(T_DIR "/Back.gba", "back"));
	CHECK(put("/SAVER/Back2.sav", "back save"));
	CHECK(put("/torii/rename.txt", T_DIR "/Back.gba\t" T_DIR "/Back2.gba\n"));
	action_rename_recover();
	CHECK(!there("/SAVER/Back2.sav") && holds("/SAVER/Back.sav", "back save") && there(T_DIR "/Back.gba"));
	CHECK(!there("/torii/rename.txt"));
	action_rename_recover();  // no journal: nothing
	// Both names there as two files (the new name another game's: a cut
	// after f_rename refused it): nothing moves either way
	CHECK(put(T_DIR "/Two.gba", "two"));
	CHECK(put(T_DIR "/Two2.gba", "another game"));
	CHECK(put("/SAVER/Two.sav", "two's save"));
	CHECK(put("/torii/rename.txt", T_DIR "/Two.gba\t" T_DIR "/Two2.gba\n"));
	action_rename_recover();
	CHECK(holds(T_DIR "/Two.gba", "two") && holds(T_DIR "/Two2.gba", "another game"));
	CHECK(holds("/SAVER/Two.sav", "two's save") && !there("/SAVER/Two2.sav"));
	CHECK(!there("/torii/rename.txt"));

	// Another game with its save in a hidden folder, which the index never
	// holds: the save stays with it
	CHECK(put(T_DIR "/Share.gba", "share"));
	CHECK(put("/SAVER/Share.sav", "share's save"));
	CHECK(put("/.T18h/Share.gba", "a hidden game"));
	CHECK_EQ(action_rename_file(T_DIR "/Share.gba", "Shared.gba"), ACTION_RENAME_SHARED);
	CHECK(there(T_DIR "/Share.gba") && holds("/SAVER/Share.sav", "share's save") && !there(T_DIR "/Shared.gba"));
	f_unlink("/.T18h/Share.gba");
	CHECK_EQ(action_rename_file(T_DIR "/Share.gba", "Shared.gba"), ACTION_RENAMED);
	CHECK(holds("/SAVER/Shared.sav", "share's save"));

	// A move the card refuses (the save states, after the save and save
	// type moved): everything goes back, the lists stay
	CHECK(put(T_DIR "/Roll.gba", "roll"));
	CHECK(put("/SAVER/Roll.sav", "roll save"));
	CHECK(put("/SAVER/Roll.mde", "roll mde"));
	CHECK(put("/RTS/Roll.rts", "roll states"));
	CHECK(gamename_store_set(T_DIR "/Roll.gba", "Rolling"));
	CHECK(userdata_fav_set(T_DIR "/Roll.gba", 1));
	action_rename_fail_file = ACTION_FILE_STATES + 1;
	CHECK_EQ(action_rename_file(T_DIR "/Roll.gba", "Rolled.gba"), ACTION_RENAME_FAILED);
	action_rename_fail_file = 0;
	CHECK(holds(T_DIR "/Roll.gba", "roll") && !there(T_DIR "/Rolled.gba"));
	CHECK(holds("/SAVER/Roll.sav", "roll save") && !there("/SAVER/Rolled.sav"));
	CHECK(holds("/SAVER/Roll.mde", "roll mde") && !there("/SAVER/Rolled.mde"));
	CHECK(holds("/RTS/Roll.rts", "roll states") && !there("/RTS/Rolled.rts"));
	CHECK(has_line(GAMENAME_FILE, T_DIR "/Roll.gba\tRolling\n"));
	CHECK(userdata_fav_has(T_DIR "/Roll.gba") && !userdata_fav_has(T_DIR "/Rolled.gba"));
	CHECK(!there("/torii/rename.txt") && !there("/torii/rename.txt.bak"));
	// The first move refused: nothing had moved but the game
	action_rename_fail_file = ACTION_FILE_SAVE + 1;
	CHECK_EQ(action_rename_file(T_DIR "/Roll.gba", "Rolled.gba"), ACTION_RENAME_FAILED);
	action_rename_fail_file = 0;
	CHECK(holds(T_DIR "/Roll.gba", "roll") && !there(T_DIR "/Rolled.gba"));
	CHECK(holds("/SAVER/Roll.sav", "roll save") && !there("/SAVER/Rolled.sav"));
	// And with nothing refused it renames
	CHECK_EQ(action_rename_file(T_DIR "/Roll.gba", "Rolled.gba"), ACTION_RENAMED);
	CHECK(holds("/RTS/Rolled.rts", "roll states") && holds("/SAVER/Rolled.sav", "roll save"));

	tidy();
	f_unlink("/torii/art/custom");
	f_unlink("/torii/art");
	f_unlink("/RTS");
	f_unlink("/PATCH");
	f_unlink(T_DIR);
	f_unlink("/.T18h");
	userdata_test_paths(NULL, NULL, NULL);
}
