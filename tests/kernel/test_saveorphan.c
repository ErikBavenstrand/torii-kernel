// The global save manager's card side (source/saveorphan.c) on the emulated
// card (tests/assets.sh): the space the saves, history and trash use, the
// orphans a scan lists, relinking one (with and without a clash), deleting
// one to the trash, emptying the trash.
// orphan_scenario() is the power-cut tests' (tests/saveorphan.sh): the test
// kernel started with SELECT and START held runs the scenario the card's
// /SLOT/scenario.txt names, with a cutpoint after each step.
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <gba_base.h>

#include "ff.h"
#include "../../source/ezkernel.h"
#include "../../source/engine/stage.h"
#include "../../source/gamerec.h"
#include "../../source/gamesave.h"
#include "../../source/saveorphan.h"
#include "../../source/slotstore.h"
#include "tests.h"
#include "torii_test.h"

extern FATFS EZcardFs;

// Borrowed from pReadCache, clear of the orphan manager's own work area
// (READCACHE_ORPHAN) and the stage
#define SCRATCH (pReadCache + 0x18000)
static struct orphan_scan *scan(void)
{
	return (struct orphan_scan *)SCRATCH;  // about 20 KB
}
_Static_assert(sizeof(struct orphan_scan) <= 0x5000, "the scan fits before info()");
static FILINFO *info(void)
{
	return (FILINFO *)(SCRATCH + 0x5000);
}

static int card(void)
{
	if (!EZcardFs.fs_type && f_mount(&EZcardFs, "", 1) != FR_OK)
		return 0;
	return EZcardFs.fs_type != 0;
}

static void make_dirs(const char *path)
{
	char dir[300];
	snprintf(dir, sizeof(dir), "%s", path);
	for (char *p = dir + 1; *p; p++)
		if (*p == '/') {
			*p = 0;
			f_mkdir(dir);
			*p = '/';
		}
}

// Writes size bytes of fill to path, making its folders
static int put(const char *path, u8 fill, u32 size)
{
	FIL f;
	UINT done = 0;
	u8 *buf = stage_buffer();
	make_dirs(path);
	memset(buf, fill, 512);
	if (f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
		return 0;
	int ok = 1;
	for (u32 off = 0; off < size && ok; off += 512)
		ok = f_write(&f, buf, 512, &done) == FR_OK && done == 512;
	return (f_close(&f) == FR_OK) & ok;
}

static int text(const char *path, const char *s)
{
	FIL f;
	UINT done = 0;
	make_dirs(path);
	if (f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
		return 0;
	int ok = f_write(&f, s, (UINT)strlen(s), &done) == FR_OK;
	return (f_close(&f) == FR_OK) & ok;
}

static int there(const char *path)
{
	return f_stat(path, info()) == FR_OK;
}

// 1 if the file's first byte is fill
static int is_fill(const char *path, u8 fill)
{
	FIL f;
	UINT done = 0;
	u8 b = 0;
	if (f_open(&f, path, FA_READ) != FR_OK)
		return 0;
	FRESULT r = f_read(&f, &b, 1, &done);
	f_close(&f);
	return r == FR_OK && done == 1 && b == fill;
}

static int find_orphan(const char *rom)
{
	struct orphan_scan *s = scan();
	for (u32 i = 0; i < s->count; i++)
		if (!strcmp(saveorphan_path(s, i), rom))
			return (int)i;
	return -1;
}

static void do_scan(void)
{
	CHECK_EQ(saveorphan_scan(scan(), NULL, NULL), ORPHAN_OK);
}

/* ---- The scan ---- */

static void test_scan(void)
{
	do_scan();
	struct {
		u32 saves_bytes, saves_files, backups_bytes, backups_files, trash_bytes, games, count, have;
	} before = {scan()->saves_bytes, scan()->saves_files, scan()->backups_bytes, scan()->backups_files,
		    scan()->trash_bytes, scan()->games,       scan()->count,         scan()->have_count};
	// Two orphans (one with two slots and a history, one a record only); a live
	// game; a NOR game's pseudo path; a folder with only another folder in it
	CHECK(put("/TOS/Live.gba", 'R', 512));
	CHECK(put("/torii/saves/TOS/Live.gba/Main.sav", 'L', 1024));
	CHECK(put("/torii/saves/TOS/Gone.gba/Main.sav", 'A', 2048));
	CHECK(put("/torii/saves/TOS/Gone.gba/Boss.sav", 'B', 512));
	CHECK(put("/torii/backups/TOS/Gone.gba/Main/0001-20260101-000000.sav", 'H', 1536));
	CHECK(text("/torii/saves/TOS/Rec.gb/game.ini", "slot = Main\n"));
	CHECK(put("/torii/saves/torii/nor/Pseudo/Main.sav", 'N', 512));
	CHECK(put("/torii/trash/20260101-000000-9/TOS/Dead.gba/Main.sav", 'T', 4096));
	do_scan();
	struct orphan_scan *s = scan();
	CHECK_EQ(s->saves_bytes - before.saves_bytes, 1024 + 2048 + 512 + 12 + 512);
	CHECK_EQ(s->saves_files - before.saves_files, 5);
	CHECK_EQ(s->backups_bytes - before.backups_bytes, 1536);
	CHECK_EQ(s->backups_files - before.backups_files, 1);
	CHECK_EQ(s->trash_bytes - before.trash_bytes, 4096);
	CHECK_EQ(s->games - before.games, 4);
	CHECK(find_orphan("/TOS/Live.gba") < 0);
	CHECK(find_orphan("/torii/nor/Pseudo") < 0);
	int g = find_orphan("/TOS/Gone.gba");
	CHECK(g >= 0);
	if (g >= 0) {
		CHECK_EQ(s->slots[g], 2);
		CHECK_EQ(s->bytes[g], 2048 + 512 + 1536);  // its saves and its history
	}
	int r = find_orphan("/TOS/Rec.gb");
	CHECK(r >= 0);
	if (r >= 0) {
		CHECK_EQ(s->slots[r], 0);
		CHECK_EQ(s->bytes[r], 12);
	}
	CHECK_EQ(s->count - before.count, 2);
	// Game saves' list: the live game, in any case; never an orphan or a NOR game
	CHECK_EQ(s->have_count - before.have, 1);
	CHECK(saveorphan_has(s, "/TOS/Live.gba"));
	CHECK(saveorphan_has(s, "/tos/LIVE.GBA"));
	CHECK(!saveorphan_has(s, "/TOS/Gone.gba"));
	CHECK(!saveorphan_has(s, "/torii/nor/Pseudo"));
	for (u32 i = 1; i < s->have_count; i++)
		CHECK(s->have[i - 1] <= s->have[i]);  // sorted, for saveorphan_has's search
	// The ROM comes back: no longer an orphan, and listed
	CHECK(put("/TOS/Gone.gba", 'R', 512));
	do_scan();
	CHECK(find_orphan("/TOS/Gone.gba") < 0);
	CHECK(saveorphan_has(scan(), "/TOS/Gone.gba"));
	f_unlink("/TOS/Gone.gba");
}

/* ---- Relink ---- */

static void test_relink_plain(void)
{
	CHECK(put("/TOS/New.gba", 'R', 512));
	CHECK(put("/torii/saves/TOS/Old.gba/Main.sav", 'A', 512));
	CHECK(put("/torii/saves/TOS/Old.gba/Boss.sav", 'B', 512));
	CHECK(put("/torii/saves/TOS/Old.gba/Boss.sav.bak", 'K', 512));
	CHECK(text("/torii/saves/TOS/Old.gba/game.ini", "save_type = 3\nboot_mode = 0\nslot = Boss\n"));
	CHECK(put("/torii/backups/TOS/Old.gba/Main/0001-20260101-000000.sav", 'H', 512));
	CHECK(put("/torii/backups/TOS/Old.gba/Boss/0002-20260101-000000.sav", 'J', 512));
	u32 renamed = 99;
	CHECK_EQ(saveorphan_relink("/TOS/Old.gba", "/TOS/New.gba", &renamed), ORPHAN_OK);
	CHECK_EQ(renamed, 0);
	CHECK(is_fill("/torii/saves/TOS/New.gba/Main.sav", 'A'));
	CHECK(is_fill("/torii/saves/TOS/New.gba/Boss.sav", 'B'));
	CHECK(is_fill("/torii/saves/TOS/New.gba/Boss.sav.bak", 'K'));
	CHECK(is_fill("/torii/backups/TOS/New.gba/Main/0001-20260101-000000.sav", 'H'));
	CHECK(is_fill("/torii/backups/TOS/New.gba/Boss/0002-20260101-000000.sav", 'J'));
	struct gamerec rec;
	gamesave_record_read("/TOS/New.gba", &rec);
	CHECK_EQ(rec.save_type, 3);
	CHECK(!strcmp(rec.slot, "Boss"));
	CHECK(!there("/torii/saves/TOS/Old.gba"));
	CHECK(!there("/torii/backups/TOS/Old.gba"));
	CHECK(!there("/torii/orphanop.txt"));
	// The slot store sees its slots
	struct slotstore_slot slots[SLOTSTORE_MAX_SLOTS];
	CHECK_EQ(slotstore_list("/TOS/New.gba", slots, SLOTSTORE_MAX_SLOTS), 2);
}

static void test_relink_clash(void)
{
	CHECK(put("/TOS/Dest.gba", 'R', 512));
	// The target has Main (with a history) and a "Boss 2" history folder only
	CHECK(put("/torii/saves/TOS/Dest.gba/Main.sav", 'T', 512));
	CHECK(text("/torii/saves/TOS/Dest.gba/game.ini", "save_type = 2\nboot_mode = 0\nslot = Main\n"));
	CHECK(put("/torii/backups/TOS/Dest.gba/Main/0001-20260101-000000.sav", 'U', 512));
	CHECK(put("/torii/backups/TOS/Dest.gba/Boss/0001-20260101-000000.sav", 'V', 512));
	// The orphan has Main, Boss (both clash: Boss by its history folder), Extra
	CHECK(put("/torii/saves/TOS/Src.gba/Main.sav", 'A', 512));
	CHECK(put("/torii/saves/TOS/Src.gba/Boss.sav", 'B', 512));
	CHECK(put("/torii/saves/TOS/Src.gba/Extra.sav", 'E', 512));
	CHECK(text("/torii/saves/TOS/Src.gba/game.ini", "save_type = 4\nboot_mode = 1\nslot = Boss\n"));
	CHECK(put("/torii/backups/TOS/Src.gba/Main/0001-20260101-000000.sav", 'H', 512));
	u32 renamed = 0;
	CHECK_EQ(saveorphan_relink("/TOS/Src.gba", "/TOS/Dest.gba", &renamed), ORPHAN_OK);
	CHECK_EQ(renamed, 2);
	// The target's own are as they were
	CHECK(is_fill("/torii/saves/TOS/Dest.gba/Main.sav", 'T'));
	CHECK(is_fill("/torii/backups/TOS/Dest.gba/Main/0001-20260101-000000.sav", 'U'));
	CHECK(is_fill("/torii/backups/TOS/Dest.gba/Boss/0001-20260101-000000.sav", 'V'));
	struct gamerec rec;
	gamesave_record_read("/TOS/Dest.gba", &rec);
	CHECK_EQ(rec.save_type, 2);  // the target's record, not the orphan's
	CHECK(!strcmp(rec.slot, "Main"));
	// The orphan's took the first free names, with their history
	CHECK(is_fill("/torii/saves/TOS/Dest.gba/Main 2.sav", 'A'));
	CHECK(is_fill("/torii/backups/TOS/Dest.gba/Main 2/0001-20260101-000000.sav", 'H'));
	CHECK(is_fill("/torii/saves/TOS/Dest.gba/Boss 2.sav", 'B'));
	CHECK(is_fill("/torii/saves/TOS/Dest.gba/Extra.sav", 'E'));
	CHECK(!there("/torii/saves/TOS/Src.gba"));
	CHECK(!there("/torii/backups/TOS/Src.gba"));
	CHECK(!there("/torii/orphanop.txt"));
}

static void test_relink_refused(void)
{
	CHECK(put("/TOS/Here.gba", 'R', 512));
	CHECK(put("/torii/saves/TOS/Lost.gba/Main.sav", 'A', 512));
	CHECK_EQ(saveorphan_relink("/TOS/Lost.gba", "/TOS/lost.gba", NULL), ORPHAN_SAME);
	CHECK_EQ(saveorphan_relink("/TOS/Lost.gba", "/TOS/Missing.gba", NULL), ORPHAN_NO_TARGET);
	CHECK_EQ(saveorphan_relink("/TOS/Lost.gba", "/TOS", NULL), ORPHAN_NO_TARGET);  // a folder
	CHECK_EQ(saveorphan_relink("/TOS/Nothing.gba", "/TOS/Here.gba", NULL), ORPHAN_NO_SOURCE);
	CHECK_EQ(saveorphan_relink("/TOS/Lost.gba", "/torii/saves/x.gba", NULL), ORPHAN_PATH);
	CHECK_EQ(saveorphan_relink("TOS/Lost.gba", "/TOS/Here.gba", NULL), ORPHAN_PATH);
	CHECK_EQ(saveorphan_relink("/TOS/Lost.gba", "/TOS/../Here.gba", NULL), ORPHAN_PATH);
	CHECK(is_fill("/torii/saves/TOS/Lost.gba/Main.sav", 'A'));
	CHECK(!there("/torii/saves/TOS/Here.gba"));
	// A slot whose name makes the target's save path too long to launch: refused
	// before anything moves
	char long_rom[256], long_slot[100];
	memset(long_slot, 'x', 60);
	long_slot[60] = 0;
	snprintf(long_rom, sizeof(long_rom), "/TOS/");
	while (strlen(long_rom) < 200)
		strcat(long_rom, "d");
	strcat(long_rom, ".gba");
	char path[400];
	CHECK(put(long_rom, 'R', 512));
	snprintf(path, sizeof(path), "/torii/saves/TOS/Wide.gba/%s.sav", long_slot);
	CHECK(put(path, 'W', 512));
	CHECK_EQ(saveorphan_relink("/TOS/Wide.gba", long_rom, NULL), ORPHAN_PATH);
	CHECK(is_fill(path, 'W'));
	f_unlink(long_rom);
}

/* ---- Delete, empty the trash ---- */

static void test_trash(void)
{
	CHECK(put("/torii/saves/TOS/Bin.gba/Main.sav", 'A', 512));
	CHECK(put("/torii/saves/TOS/Bin.gba/game.ini", 'I', 32));
	CHECK(put("/torii/backups/TOS/Bin.gba/Main/0001-20260101-000000.sav", 'H', 512));
	do_scan();
	u32 trash_before = scan()->trash_bytes;
	CHECK_EQ(saveorphan_trash("/TOS/Bin.gba"), ORPHAN_OK);
	CHECK(!there("/torii/saves/TOS/Bin.gba"));
	CHECK(!there("/torii/backups/TOS/Bin.gba"));
	CHECK(!there("/torii/orphanop.txt"));
	do_scan();
	CHECK_EQ(scan()->trash_bytes - trash_before, 512 + 512 + 512);  // game.ini is 512 too: put writes whole blocks
	CHECK(find_orphan("/TOS/Bin.gba") < 0);
	// Under a day's folder, in the slot store's layout (the folder's stamp is
	// the clock's, zeros while it is unset)
	DIR d;
	char name[300] = "", path[400];
	CHECK(f_opendir(&d, "/torii/trash") == FR_OK);
	while (f_readdir(&d, info()) == FR_OK && info()->fname[0]) {
		char day[300];
		snprintf(day, sizeof(day), "%s", info()->fname);  // f_stat below reuses info()
		snprintf(path, sizeof(path), "/torii/trash/%s/TOS/Bin.gba/Main.sav", day);
		if (day[0] != '.' && there(path)) {
			snprintf(name, sizeof(name), "%s", day);
			break;
		}
	}
	f_closedir(&d);
	snprintf(path, sizeof(path), "/torii/trash/%s/TOS/Bin.gba/Main.sav", name);
	CHECK(is_fill(path, 'A'));
	snprintf(path, sizeof(path), "/torii/trash/%s/backups/TOS/Bin.gba/Main/0001-20260101-000000.sav", name);
	CHECK(is_fill(path, 'H'));
	CHECK_EQ(saveorphan_trash("/TOS/Bin.gba"), ORPHAN_NO_SOURCE);
	CHECK_EQ(saveorphan_trash("/torii/x"), ORPHAN_PATH);
	// Empty it
	u32 removed = 0;
	CHECK_EQ(saveorphan_trash_empty(NULL, NULL, &removed), ORPHAN_OK);
	CHECK(removed >= 2);
	CHECK(there("/torii/trash"));
	do_scan();
	CHECK_EQ(scan()->trash_bytes, 0);
	CHECK_EQ(scan()->trash_files, 0);
	CHECK_EQ(saveorphan_trash_empty(NULL, NULL, &removed), ORPHAN_OK);
	CHECK_EQ(removed, 0);
}

/* ---- A folder that holds games' folders ---- */

// A stray .sav in an ancestor of games' folders (/torii/saves/TOB, beside
// TOB/Kid.gba): never an orphan, and neither relink nor delete takes it, so
// the games below keep their saves
static void test_ancestor(void)
{
	CHECK(put("/TOA/Kid.gba", 'R', 512));
	CHECK(put("/torii/saves/TOA/Kid.gba/Main.sav", 'K', 512));
	CHECK(put("/torii/saves/TOA/Stray.sav", 'S', 512));
	CHECK(put("/torii/saves/TOB/Stray.sav", 'S', 512));
	CHECK(put("/torii/saves/TOB/Kid.gba/Main.sav", 'K', 512));
	do_scan();
	CHECK(find_orphan("/TOA") < 0);
	CHECK(find_orphan("/TOB") < 0);
	CHECK(find_orphan("/TOB/Kid.gba") >= 0);  // its ROM is gone: an orphan of its own
	CHECK_EQ(saveorphan_trash("/TOB"), ORPHAN_PATH);
	u32 renamed = 0;
	CHECK_EQ(saveorphan_relink("/TOA", "/TOA/Kid.gba", &renamed), ORPHAN_PATH);
	CHECK(is_fill("/torii/saves/TOB/Kid.gba/Main.sav", 'K'));
	CHECK(is_fill("/torii/saves/TOA/Kid.gba/Main.sav", 'K'));
	CHECK(!there("/torii/orphanop.txt"));
	const char *gone[] = {"/torii/saves/TOB/Kid.gba/Main.sav",
			      "/torii/saves/TOB/Kid.gba",
			      "/torii/saves/TOB/Stray.sav",
			      "/torii/saves/TOB",
			      "/torii/saves/TOA/Kid.gba/Main.sav",
			      "/torii/saves/TOA/Kid.gba",
			      "/torii/saves/TOA/Stray.sav",
			      "/torii/saves/TOA",
			      "/TOA/Kid.gba",
			      "/TOA"};
	for (u32 i = 0; i < sizeof(gone) / sizeof(gone[0]); i++)
		f_unlink(gone[i]);
}

void test_saveorphan(void)
{
	if (!card()) {
		tlog_error("saveorphan: no card");
		++torii_test_failures;
		return;
	}
	test_scan();
	test_relink_plain();
	test_relink_clash();
	test_relink_refused();
	test_trash();
	test_ancestor();
}

/* ---- The power-cut scenarios (tests/saveorphan.sh) ---- */

#define G_OLD "/G/Old.gba"
#define G_NEW "/G/New.gba"

static int scenario_is(const char *want)
{
	u32 buf[8];
	char *line = (char *)buf;
	FIL f;
	UINT got = 0;
	if (f_open(&f, "/SLOT/scenario.txt", FA_READ) != FR_OK)
		return 0;
	f_read(&f, line, sizeof(buf) - 1, &got);
	f_close(&f);
	line[got] = 0;
	u32 n = (u32)strlen(want);
	return !strncmp(line, want, n) && (line[n] == '\n' || line[n] == 0);
}

int orphan_scenario(void)
{
	if (!card())
		return 0;
	int r = -99;
	if (scenario_is("orphan-relink") || scenario_is("orphan-relink-clash")) {
		tlog("SLOT SCENARIO START");
		r = saveorphan_relink(G_OLD, G_NEW, NULL);
	} else if (scenario_is("orphan-trash")) {
		tlog("SLOT SCENARIO START");
		r = saveorphan_trash(G_OLD);
	} else if (scenario_is("orphan-empty")) {
		tlog("SLOT SCENARIO START");
		r = saveorphan_trash_empty(NULL, NULL, NULL);
	} else {
		return 0;
	}
	tlog("SLOT SCENARIO DONE: %d", r);
	return 1;
}
