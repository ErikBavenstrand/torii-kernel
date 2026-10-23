// The slot store (source/slotstore.c) on the emulated cart's card
// (tests/assets.sh): slots made, renamed, duplicated and deleted, the history
// pushed, pruned and restored, imports and exports, the save type changing,
// the trash purged, and the boot recovery of a journal a power cut left.
// slot_scenario() is the power-cut tests' (tests/slotstore.sh): the test kernel
// started with SELECT and START held runs the scenario the card's
// /SLOT/scenario.txt names, with a cutpoint after each step.
#include <stdio.h>
#include <strings.h>
#include <string.h>

#include <gba_base.h>

#include "ff.h"
#include "../../source/crc32.h"
#include "../../source/ezkernel.h"
#include "../../source/engine/stage.h"
#include "../../source/gamesave.h"
#include "../../source/slotstore.h"
#include "tests.h"
#include "torii_test.h"

extern FATFS EZcardFs;

// Every kernel links these tests (Diagnostics), so their buffers borrow
// pReadCache, clear of the slot store's own (READCACHE_SLOTS) and the stage
#define SCRATCH (pReadCache + 0x18000)
static struct slotstore_slot *slot_list(void)
{
	return (struct slotstore_slot *)SCRATCH;  // SLOTSTORE_MAX_SLOTS of them, up to 0x800
}
static struct slotstore_version *versions(void)
{
	return (struct slotstore_version *)(SCRATCH + 0x800);  // SLOTSTORE_MAX_VERSIONS of them
}
static FILINFO *info(void)
{
	return (FILINFO *)(SCRATCH + 0xC00);
}
static char *scratch_text(void)
{
	return (char *)(SCRATCH + 0x1000);  // 512 bytes
}

#define ROM "/TSL/Game.gba"
#define SAVES "/torii/saves/TSL/Game.gba"
#define BACKUPS "/torii/backups/TSL/Game.gba"

static int card(void)
{
	if (!EZcardFs.fs_type && f_mount(&EZcardFs, "", 1) != FR_OK)
		return 0;
	return EZcardFs.fs_type != 0;
}

static void make_dirs(const char *path)
{
	char dir[200];
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
	char dir[200];
	snprintf(dir, sizeof(dir), "%s", path);
	*strrchr(dir, '/') = 0;
	make_dirs(path);
	f_mkdir(dir);
	memset(buf, fill, 512);
	if (f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
		return 0;
	int ok = 1;
	for (u32 off = 0; off < size && ok; off += 512)
		ok = f_write(&f, buf, 512, &done) == FR_OK && done == 512;
	return (f_close(&f) == FR_OK) & ok;
}

// The byte every one of the file's bytes is, or -1 if they differ, -2 if there
// is no file; *size is the file's size
static int fill_of(const char *path, u32 *size)
{
	FIL f;
	UINT done = 0;
	u8 *buf = stage_buffer();
	if (f_open(&f, path, FA_READ) != FR_OK)
		return -2;
	u32 total = (u32)f_size(&f);
	int fill = -3;
	for (u32 off = 0; off < total; off += 512) {
		u32 len = total - off < 512 ? total - off : 512;
		if (f_read(&f, buf, len, &done) != FR_OK || done != len) {
			f_close(&f);
			return -1;
		}
		for (u32 i = 0; i < len; i++) {
			if (fill == -3)
				fill = buf[i];
			else if (fill != buf[i])
				fill = -1;
		}
	}
	f_close(&f);
	if (size)
		*size = total;
	return fill == -3 ? -1 : fill;
}

static int there(const char *path)
{
	return f_stat(path, info()) == FR_OK;
}

// The name FatFs has stored for path (its case), "" if none. f_stat answers
// with the name it was asked for, so the folder is read.
static const char *stored_name(const char *path)
{
	DIR d;
	char dir[200];
	snprintf(dir, sizeof(dir), "%s", path);
	char *slash = strrchr(dir, '/');
	*slash = 0;
	if (f_opendir(&d, dir[0] ? dir : "/") != FR_OK)
		return "";
	const char *found = "";
	while (f_readdir(&d, info()) == FR_OK && info()->fname[0]) {
		const char *a = info()->fname, *b = strrchr(path, '/') + 1;
		u32 n = (u32)strlen(a);
		if (n == strlen(b) && !strncasecmp(a, b, n)) {
			found = a;
			break;
		}
	}
	f_closedir(&d);
	return found;
}

static int is_fill(const char *path, u8 fill)
{
	return fill_of(path, NULL) == fill;
}

// The first entry of a folder (a name into out), 0 if none
static int first_entry(const char *dir, char *out, u32 size)
{
	DIR d;
	if (f_opendir(&d, dir) != FR_OK)
		return 0;
	int ok = f_readdir(&d, info()) == FR_OK && info()->fname[0];
	if (ok) {
		u32 n = (u32)strlen(info()->fname);
		n = n < size - 1 ? n : size - 1;
		memcpy(out, info()->fname, n);
		out[n] = 0;
	}
	f_closedir(&d);
	return ok;
}

static u32 entries(const char *dir)
{
	DIR d;
	u32 n = 0;
	if (f_opendir(&d, dir) != FR_OK)
		return 0;
	while (f_readdir(&d, info()) == FR_OK && info()->fname[0])
		n++;
	f_closedir(&d);
	return n;
}

static int slot_count(const char *rom)
{
	return slotstore_list(rom, slot_list(), SLOTSTORE_MAX_SLOTS);
}

static int has_slot(const char *rom, const char *name)
{
	struct slotstore_slot *list = slot_list();
	int n = slotstore_list(rom, list, SLOTSTORE_MAX_SLOTS);
	for (int i = 0; i < n && i < SLOTSTORE_MAX_SLOTS; i++)
		if (!strcmp(list[i].name, name))
			return 1;
	return 0;
}

static void current_is(const char *rom, const char *want)
{
	char cur[SAVESLOT_NAME_BYTES];
	slotstore_current(rom, cur);
	if (strcmp(cur, want))
		tlog_error("slot: current is %s, expected %s", cur, want);
	CHECK(!strcmp(cur, want));
}

// The journal's text, as slotstore.c writes it (the test makes the state a
// cut leaves)
static int journal(char kind, int mode, const char *a, const char *b, const char *c, int damage)
{
	char *text = scratch_text();
	int n = snprintf(text, 500, "torii-slotop 1\n%c%d\n%s\n%s\n%s\n", kind, mode, a, b, c);
	n += snprintf(text + n, 12, "%08lx\n", (unsigned long)crc32(text, (u32)n));
	if (damage)
		text[20] ^= 1;
	FIL f;
	UINT done = 0;
	f_mkdir("/torii");
	if (f_open(&f, "/torii/slotop.txt", FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
		return 0;
	int ok = f_write(&f, text, (UINT)n, &done) == FR_OK && done == (UINT)n;
	return (f_close(&f) == FR_OK) & ok;
}

static void test_slots_and_current(void)
{
	CHECK_EQ(slot_count(ROM), 0);
	current_is(ROM, "Main");  // a game with no record
	CHECK_EQ(slotstore_set_current(ROM, "Main"), SLOTSTORE_NO_SLOT);
	CHECK(put(SAVES "/Main.sav", 'A', 512));
	CHECK(put(SAVES "/Main.sav.tmp", 'x', 512));  // not a slot
	CHECK(put(SAVES "/Main.sav.new", 'x', 512));
	CHECK(put(SAVES "/.hidden.sav", 'x', 512));  // not a clean name
	CHECK_EQ(slot_count(ROM), 1);
	CHECK(has_slot(ROM, "Main"));
	CHECK_EQ(slotstore_set_current(ROM, "main"), SLOTSTORE_OK);  // case ignored, the file's spelling kept
	current_is(ROM, "Main");
}

static void test_create_and_duplicate(void)
{
	char clean[SAVESLOT_NAME_BYTES];
	u32 size = 0;
	CHECK_EQ(slotstore_create(ROM, " Run 2. ", NULL, 8192, clean), SLOTSTORE_OK);
	CHECK(!strcmp(clean, "Run 2"));
	CHECK(is_fill(SAVES "/Run 2.sav", 0xFF));
	fill_of(SAVES "/Run 2.sav", &size);
	CHECK_EQ(size, 8192);
	CHECK_EQ(slot_count(ROM), 2);
	CHECK_EQ(slotstore_create(ROM, "run 2", NULL, 8192, NULL), SLOTSTORE_TAKEN);
	CHECK_EQ(slotstore_create(ROM, "main", NULL, 8192, NULL), SLOTSTORE_TAKEN);
	CHECK_EQ(slotstore_create(ROM, "/*?", NULL, 8192, NULL), SLOTSTORE_NAME);
	CHECK_EQ(slotstore_create(ROM, "Bad size", NULL, 100, NULL), SLOTSTORE_SIZE);
	CHECK_EQ(slotstore_create(ROM, "Ghost", "Nope", 0, NULL), SLOTSTORE_NO_SLOT);
	CHECK(!there(SAVES "/Bad size.sav"));
	CHECK_EQ(slotstore_duplicate(ROM, "main", "Copy", clean), SLOTSTORE_OK);
	CHECK(is_fill(SAVES "/Copy.sav", 'A'));
	CHECK(is_fill(SAVES "/Main.sav", 'A'));
	CHECK(!there(SAVES "/Copy.sav.new"));
	CHECK_EQ(slot_count(ROM), 3);
	current_is(ROM, "Main");  // creating never changes the current slot
	// A path that would not launch is refused
	char longrom[SAVESLOT_ROM_PATH_MAX + 1];
	memset(longrom, 'x', sizeof(longrom));
	longrom[0] = '/';
	memcpy(longrom + 240, ".gba", 5);
	CHECK_EQ(slotstore_create(longrom, "Main", NULL, 512, NULL), SLOTSTORE_PATH);
}

static u32 history_count(const char *slot)
{
	int n = slotstore_history(ROM, slot, versions(), SLOTSTORE_MAX_VERSIONS);
	return n < 0 ? 0xFFFF : (u32)n;
}

// The fill of the version at index i of the slot's history, newest first
static int version_fill(const char *slot, u32 i, u32 *n)
{
	struct slotstore_version *v = versions();
	int count = slotstore_history(ROM, slot, v, SLOTSTORE_MAX_VERSIONS);
	if (count < 0 || i >= (u32)count)
		return -9;
	char dir[200], name[SAVESLOT_BACKUP_NAME_BYTES], path[260];
	saveslot_backup_dir(dir, sizeof(dir), ROM, slot);
	saveslot_backup_name(name, v[i].n, &v[i].when);
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	if (n)
		*n = v[i].n;
	return fill_of(path, NULL);
}

static void test_history(void)
{
	CHECK_EQ(slotstore_keep(), 5);  // the default of a cart that never set it
	CHECK_EQ(history_count("Main"), 0);
	CHECK_EQ(slotstore_history_push(ROM, "Main"), SLOTSTORE_OK);
	CHECK_EQ(history_count("Main"), 1);
	CHECK_EQ(slotstore_history_push(ROM, "Main"), SLOTSTORE_OK);  // the same: no second version
	CHECK_EQ(history_count("Main"), 1);
	CHECK_EQ(version_fill("Main", 0, NULL), 'A');
	// Different saves, newest first, the counter rising
	const char *dir = BACKUPS "/Main";
	for (u8 f = 'B'; f <= 'H'; f++) {
		CHECK(put(SAVES "/Main.sav", f, 512));
		CHECK_EQ(slotstore_push_file(SAVES "/Main.sav", dir, 5), SLOTSTORE_OK);
	}
	// A..H pushed, 5 kept: D to H
	CHECK_EQ(history_count("Main"), 5);
	u32 n0, n1;
	CHECK_EQ(version_fill("Main", 0, &n0), 'H');
	CHECK_EQ(version_fill("Main", 4, &n1), 'D');
	CHECK_EQ(n0, n1 + 4);
	CHECK_EQ(n0, 8);  // A was 1
	// A smaller maximum gives the newest only
	struct slotstore_version *two = versions();
	CHECK_EQ(slotstore_history(ROM, "Main", two, 2), 5);
	CHECK_EQ(two[0].n, 8);
	CHECK_EQ(two[1].n, 7);
	// A stray .new from a cut push goes the next time the folder is read
	CHECK(put(BACKUPS "/Main/0009-00000000-000000.sav.new", 'x', 512));
	CHECK_EQ(history_count("Main"), 5);
	CHECK(!there(BACKUPS "/Main/0009-00000000-000000.sav.new"));
	// A prune to 2
	CHECK(put(SAVES "/Main.sav", 'I', 512));
	CHECK_EQ(slotstore_push_file(SAVES "/Main.sav", dir, 2), SLOTSTORE_OK);
	CHECK_EQ(history_count("Main"), 2);
	CHECK_EQ(version_fill("Main", 0, &n0), 'I');
	CHECK_EQ(n0, 9);
	// A slot with no file has nothing to push
	CHECK_EQ(slotstore_history_push(ROM, "Nobody"), SLOTSTORE_NO_SLOT);
	CHECK_EQ(slotstore_push_file(SAVES "/Gone.sav", dir, 5), SLOTSTORE_NO_SLOT);
}

static void test_restore(void)
{
	// "Alt": the file is C; the history has A (n 1) and B (n 2)
	const char *dir = BACKUPS "/Alt";
	CHECK(put(SAVES "/Alt.sav", 'A', 512));
	CHECK_EQ(slotstore_push_file(SAVES "/Alt.sav", dir, 0), SLOTSTORE_OK);
	CHECK(put(SAVES "/Alt.sav", 'B', 512));
	CHECK_EQ(slotstore_push_file(SAVES "/Alt.sav", dir, 0), SLOTSTORE_OK);
	CHECK(put(SAVES "/Alt.sav", 'C', 512));
	CHECK_EQ(slotstore_restore(ROM, "Alt", 7), SLOTSTORE_NO_VERSION);
	CHECK_EQ(slotstore_restore(ROM, "Nobody", 1), SLOTSTORE_NO_SLOT);
	CHECK_EQ(slotstore_restore(ROM, "Alt", 1), SLOTSTORE_OK);
	CHECK(is_fill(SAVES "/Alt.sav", 'A'));
	CHECK(!there(SAVES "/Alt.sav.tmp") && !there(SAVES "/Alt.sav.bak"));
	// Version 1 left; C joined B: nothing lost
	CHECK_EQ(history_count("Alt"), 2);
	CHECK_EQ(version_fill("Alt", 0, NULL), 'C');
	CHECK_EQ(version_fill("Alt", 1, NULL), 'B');
	CHECK(!there("/torii/slotop.txt"));
	// Restoring the newest version
	CHECK_EQ(slotstore_restore(ROM, "Alt", 3), SLOTSTORE_OK);
	CHECK(is_fill(SAVES "/Alt.sav", 'C'));
	CHECK_EQ(version_fill("Alt", 0, NULL), 'A');
}

static void test_import_export(void)
{
	u32 size = 0;
	CHECK(put("/TSL/in.sav", 'I', 512));
	CHECK(put("/TSL/big.sav", 'J', 1024));
	CHECK_EQ(slotstore_import(ROM, "Alt", "/TSL/big.sav", 512), SLOTSTORE_SIZE);
	CHECK_EQ(slotstore_import(ROM, "Alt", "/TSL/none.sav", 512), SLOTSTORE_NO_FILE);
	CHECK_EQ(slotstore_import(ROM, "Nobody", "/TSL/in.sav", 512), SLOTSTORE_NO_SLOT);
	CHECK(is_fill(SAVES "/Alt.sav", 'C'));
	u32 before = history_count("Alt");
	CHECK_EQ(slotstore_import(ROM, "Alt", "/TSL/in.sav", 512), SLOTSTORE_OK);
	CHECK(is_fill(SAVES "/Alt.sav", 'I'));
	CHECK(is_fill("/TSL/in.sav", 'I'));  // the source stays
	CHECK_EQ(history_count("Alt"), before + 1);
	CHECK_EQ(version_fill("Alt", 0, NULL), 'C');  // the replaced file is the newest version
	CHECK(!there(SAVES "/Alt.sav.tmp") && !there(SAVES "/Alt.sav.bak"));
	// Export
	CHECK_EQ(slotstore_export(ROM, "Alt", "/TSL/out.sav"), SLOTSTORE_OK);
	CHECK(is_fill("/TSL/out.sav", 'I'));
	fill_of("/TSL/out.sav", &size);
	CHECK_EQ(size, 512);
	CHECK_EQ(slotstore_export(ROM, "Alt", "/TSL/out.sav"), SLOTSTORE_TAKEN);
	CHECK_EQ(slotstore_export(ROM, "Nobody", "/TSL/out2.sav"), SLOTSTORE_NO_SLOT);
	CHECK(!there("/TSL/out.sav.new"));
}

static void test_rename(void)
{
	// Main: current, with a history; renaming moves both and the record
	CHECK_EQ(slotstore_rename(ROM, "Main", "Copy", NULL), SLOTSTORE_TAKEN);
	CHECK_EQ(slotstore_rename(ROM, "Main", "COPY", NULL), SLOTSTORE_TAKEN);
	CHECK_EQ(slotstore_rename(ROM, "Main", "...", NULL), SLOTSTORE_NAME);
	CHECK_EQ(slotstore_rename(ROM, "Nobody", "Other", NULL), SLOTSTORE_NO_SLOT);
	CHECK_EQ(slotstore_rename(ROM, "Main", "Main", NULL), SLOTSTORE_OK);
	u32 hist = history_count("Main");
	CHECK(hist > 0);
	char clean[SAVESLOT_NAME_BYTES];
	CHECK_EQ(slotstore_rename(ROM, "Main", " Start ", clean), SLOTSTORE_OK);
	CHECK(!strcmp(clean, "Start"));
	CHECK(!there(SAVES "/Main.sav"));
	CHECK(is_fill(SAVES "/Start.sav", 'I'));
	CHECK_EQ(history_count("Main"), 0);
	CHECK_EQ(history_count("Start"), hist);
	CHECK(!there(BACKUPS "/Main"));
	current_is(ROM, "Start");
	CHECK(!there("/torii/slotop.txt"));
	// Only the case differs
	CHECK_EQ(slotstore_rename(ROM, "Start", "START", NULL), SLOTSTORE_OK);
	CHECK(!strcmp(stored_name(SAVES "/Start.sav"), "START.sav"));
	CHECK(!strcmp(stored_name(BACKUPS "/Start"), "START"));
	CHECK_EQ(history_count("START"), hist);
	CHECK(is_fill(SAVES "/START.sav", 'I'));
	current_is(ROM, "START");
	CHECK(!there(SAVES "/START.sav.ren") && !there(BACKUPS "/START.ren"));
	// A stale history folder of the new name goes to the trash first
	CHECK(put(BACKUPS "/Fresh/0001-00000000-000000.sav", 'S', 512));
	CHECK_EQ(slotstore_rename(ROM, "Copy", "Fresh", NULL), SLOTSTORE_OK);
	CHECK(is_fill(SAVES "/Fresh.sav", 'A'));
	CHECK_EQ(history_count("Fresh"), 0);
	CHECK(first_entry("/torii/trash", clean, sizeof(clean)));
	CHECK_EQ(slotstore_rename(ROM, "Fresh", "Copy", NULL), SLOTSTORE_OK);
}

// 1 if some folder of the trash holds the file rel (a path from its folder)
// with every byte fill
static int trash_has(const char *rel, u8 fill)
{
	DIR d;
	char path[320];
	if (f_opendir(&d, "/torii/trash") != FR_OK)
		return 0;
	int found = 0;
	while (!found && f_readdir(&d, info()) == FR_OK && info()->fname[0]) {
		snprintf(path, sizeof(path), "/torii/trash/%s%s", info()->fname, rel);
		found = is_fill(path, fill);
	}
	f_closedir(&d);
	return found;
}

static void test_delete(void)
{
	CHECK_EQ(slotstore_delete(ROM, "Nobody"), SLOTSTORE_NO_SLOT);
	int before = slot_count(ROM);
	CHECK(before >= 3);
	// A slot that isn't current
	CHECK(put(BACKUPS "/Run 2/0001-00000000-000000.sav", 'R', 512));
	CHECK_EQ(slotstore_delete(ROM, "Run 2"), SLOTSTORE_OK);
	CHECK_EQ(slot_count(ROM), before - 1);
	CHECK(!there(SAVES "/Run 2.sav") && !there(BACKUPS "/Run 2"));
	CHECK(trash_has(ROM "/Run 2.sav", 0xFF));
	CHECK(trash_has("/backups" ROM "/Run 2/0001-00000000-000000.sav", 'R'));
	CHECK(!there("/torii/slotop.txt"));
	current_is(ROM, "START");
	// The current slot: another becomes current
	CHECK_EQ(slotstore_delete(ROM, "START"), SLOTSTORE_OK);
	CHECK(!has_slot(ROM, "START"));
	char cur[SAVESLOT_NAME_BYTES];
	slotstore_current(ROM, cur);
	CHECK(has_slot(ROM, cur));
	// Down to the last slot: refused
	struct slotstore_slot *list = slot_list();
	int n, guard = 0;
	while ((n = slotstore_list(ROM, list, SLOTSTORE_MAX_SLOTS)) > 1 && guard++ < 20)
		CHECK_EQ(slotstore_delete(ROM, list[0].name), SLOTSTORE_OK);
	CHECK_EQ(n, 1);
	CHECK_EQ(slotstore_delete(ROM, list[0].name), SLOTSTORE_LAST);
	CHECK(has_slot(ROM, list[0].name));
	slotstore_current(ROM, cur);
	CHECK(!strcmp(cur, list[0].name));
}

// The state a cut leaves of a rename or a delete, finished or undone by
// slotstore_recover
static void test_recovery(void)
{
	const char *rom = "/TSL/Rec.gba";
	char s[] = "/torii/saves/TSL/Rec.gba", b[] = "/torii/backups/TSL/Rec.gba";
	char path[300];
	struct gamerec rec;
	// A rename cut after the save moved: the history follows, so does the record
	snprintf(path, sizeof(path), "%s/New.sav", s);
	CHECK(put(path, 'N', 512));
	snprintf(path, sizeof(path), "%s/Old/0001-00000000-000000.sav", b);
	CHECK(put(path, 'H', 512));
	gamerec_init(&rec);
	strcpy(rec.slot, "Old");
	CHECK(gamesave_record_write(rom, &rec));
	CHECK(journal('R', SAVESLOT_CENTRAL, rom, "Old", "New", 0));
	CHECK_EQ(slotstore_recover(), 1);
	CHECK(!there("/torii/slotop.txt"));
	CHECK(!there("/torii/backups/TSL/Rec.gba/Old"));
	CHECK(is_fill("/torii/backups/TSL/Rec.gba/New/0001-00000000-000000.sav", 'H'));
	current_is(rom, "New");
	// Cut before the save moved: nothing renamed, the journal goes
	CHECK(put("/torii/saves/TSL/Rec.gba/Old.sav", 'O', 512));
	CHECK(journal('R', SAVESLOT_CENTRAL, rom, "Old", "Other", 0));
	CHECK_EQ(slotstore_recover(), 1);
	CHECK(!there("/torii/slotop.txt"));
	CHECK(is_fill("/torii/saves/TSL/Rec.gba/Old.sav", 'O'));
	// The new name's history folder in the way: the save goes back
	CHECK(put("/torii/saves/TSL/Rec.gba/Back2.sav", 'B', 512));
	CHECK(put("/torii/backups/TSL/Rec.gba/Back1/0001-00000000-000000.sav", 'P', 512));
	CHECK(put("/torii/backups/TSL/Rec.gba/Back2/0001-00000000-000000.sav", 'Q', 512));
	CHECK(journal('R', SAVESLOT_CENTRAL, rom, "Back1", "Back2", 0));
	CHECK_EQ(slotstore_recover(), 1);
	CHECK(is_fill("/torii/saves/TSL/Rec.gba/Back1.sav", 'B'));
	CHECK(!there("/torii/saves/TSL/Rec.gba/Back2.sav"));
	CHECK(is_fill("/torii/backups/TSL/Rec.gba/Back1/0001-00000000-000000.sav", 'P'));
	CHECK(is_fill("/torii/backups/TSL/Rec.gba/Back2/0001-00000000-000000.sav", 'Q'));
	// A damaged journal is dropped and nothing moves
	CHECK(journal('R', SAVESLOT_CENTRAL, rom, "Back1", "Moved", 1));
	CHECK_EQ(slotstore_recover(), 1);
	CHECK(!there("/torii/slotop.txt"));
	CHECK(is_fill("/torii/saves/TSL/Rec.gba/Back1.sav", 'B'));
	// A delete cut after the save moved: the history follows it
	CHECK(put("/torii/trash/20261001-120000-1/TSL/Rec.gba/Gone.sav", 'G', 512));
	CHECK(put("/torii/backups/TSL/Rec.gba/Gone/0001-00000000-000000.sav", 'V', 512));
	CHECK(journal('D', SAVESLOT_CENTRAL, rom, "Gone", "/torii/trash/20261001-120000-1", 0));
	CHECK_EQ(slotstore_recover(), 1);
	CHECK(!there("/torii/slotop.txt"));
	CHECK(!there("/torii/backups/TSL/Rec.gba/Gone"));
	CHECK(is_fill("/torii/trash/20261001-120000-1/backups/TSL/Rec.gba/Gone/0001-00000000-000000.sav", 'V'));
	CHECK(is_fill("/torii/trash/20261001-120000-1/TSL/Rec.gba/Gone.sav", 'G'));
	// A delete cut before anything moved: it finishes
	CHECK(put("/torii/saves/TSL/Rec.gba/Late.sav", 'L', 512));
	CHECK(journal('D', SAVESLOT_CENTRAL, rom, "Late", "/torii/trash/20261001-130000-1", 0));
	CHECK_EQ(slotstore_recover(), 1);
	CHECK(!there("/torii/saves/TSL/Rec.gba/Late.sav"));
	CHECK(is_fill("/torii/trash/20261001-130000-1/TSL/Rec.gba/Late.sav", 'L'));
	// An install cut between its two renames: the .tmp takes the save's name
	CHECK(put("/torii/saves/TSL/Rec.gba/Inst.sav.tmp", 'T', 512));
	CHECK(put("/torii/saves/TSL/Rec.gba/Inst.sav.bak", 'K', 512));
	CHECK(journal('I', SAVESLOT_CENTRAL, "/torii/saves/TSL/Rec.gba/Inst.sav", "-", "-", 0));
	CHECK_EQ(slotstore_recover(), 1);
	CHECK(is_fill("/torii/saves/TSL/Rec.gba/Inst.sav", 'T'));
	CHECK(!there("/torii/saves/TSL/Rec.gba/Inst.sav.tmp") && !there("/torii/saves/TSL/Rec.gba/Inst.sav.bak"));
	CHECK_EQ(slotstore_recover(), 0);  // nothing left
}

static void test_retire(void)
{
	const char *file = "/torii/saves/TSL/Type.gba/Main.sav";
	const char *dir = "/torii/backups/TSL/Type.gba/Main";
	u32 size = 0;
	CHECK(put(file, 'Z', 8192));
	CHECK_EQ(slotstore_retire_file(file, dir), SLOTSTORE_OK);
	CHECK(!there(file));
	// The old file is in the history untouched, whole
	char name[64], path[300];
	CHECK(first_entry(dir, name, sizeof(name)));
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	CHECK(is_fill(path, 'Z'));
	fill_of(path, &size);
	CHECK_EQ(size, 8192);
	// Again (a launch after a cut): nothing to do, nothing doubled
	CHECK_EQ(slotstore_retire_file(file, dir), SLOTSTORE_OK);
	CHECK_EQ(entries(dir), 1);
	// A cut after the push, before the removal: the file is still there and the
	// repeat adds no second copy
	CHECK(put(file, 'Z', 8192));
	CHECK_EQ(slotstore_retire_file(file, dir), SLOTSTORE_OK);
	CHECK_EQ(entries(dir), 1);
	CHECK(!there(file));
}

static void test_purge(void)
{
	struct saveslot_stamp july = {2026, 7, 2, 12, 0, 0}, unset = {0, 0, 0, 0, 0, 0}, early = {2026, 5, 1, 0, 0, 0};
	CHECK(put("/torii/trash/20260601-120000-1/A/B/deep.sav", 'D', 512));
	CHECK(put("/torii/trash/20260601-120000-1/backups/A/B/Main/0001-00000000-000000.sav", 'D', 512));
	CHECK(put("/torii/trash/20260625-120000-1/A/B/new.sav", 'N', 512));
	CHECK(put("/torii/trash/not-a-date/keep.sav", 'K', 512));
	CHECK_EQ(slotstore_trash_purge(&unset), 0);  // an unset clock purges nothing
	CHECK_EQ(slotstore_trash_purge(&early), 0);  // one before the folder's date, nothing
	CHECK(there("/torii/trash/20260601-120000-1/A/B/deep.sav"));
	CHECK_EQ(slotstore_trash_purge(&july), 1);  // 30 days past June 1; June 25 stays
	CHECK(!there("/torii/trash/20260601-120000-1"));
	CHECK(there("/torii/trash/20260625-120000-1/A/B/new.sav"));
	CHECK(there("/torii/trash/not-a-date/keep.sav"));
	struct saveslot_stamp august = {2026, 8, 1, 0, 0, 0};
	CHECK_EQ(slotstore_trash_purge(&august), 1);
	CHECK(!there("/torii/trash/20260625-120000-1"));
	CHECK(there("/torii/trash/not-a-date/keep.sav"));
}

void test_slotstore(void)
{
	if (!card()) {
		tlog_error("slotstore: no card");
		++torii_test_failures;
		return;
	}
	test_slots_and_current();
	test_create_and_duplicate();
	test_history();
	test_restore();
	test_import_export();
	test_rename();
	test_delete();
	test_recovery();
	test_retire();
	test_purge();
}

/* ---- The power-cut scenarios (tests/slotstore.sh) ---- */

#define G_ROM "/G/Game.gba"
#define G_SAVES "/torii/saves/G/Game.gba"
#define G_IN "/G/in.sav"

// The scenario's name from the first line of /SLOT/scenario.txt
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

void slot_scenario(void)
{
	if (!card()) {
		tlog_error("slot scenario: no card");
		return;
	}
	tlog("SLOT SCENARIO START");
	int r = -99;
	if (scenario_is("push")) {
		r = slotstore_push_file(G_SAVES "/Main.sav", "/torii/backups/G/Game.gba/Main", 3);
	} else if (scenario_is("restore")) {
		r = slotstore_restore(G_ROM, "Main", 1);
	} else if (scenario_is("import")) {
		r = slotstore_import(G_ROM, "Main", G_IN, 512);
	} else if (scenario_is("export")) {
		r = slotstore_export(G_ROM, "Main", "/G/out.sav");
	} else if (scenario_is("create-blank")) {
		r = slotstore_create(G_ROM, "Fresh", NULL, 512, NULL);
	} else if (scenario_is("create-copy")) {
		r = slotstore_create(G_ROM, "Copy", "Main", 0, NULL);
	} else if (scenario_is("rename")) {
		r = slotstore_rename(G_ROM, "Main", "Start", NULL);
	} else if (scenario_is("rename-case")) {
		r = slotstore_rename(G_ROM, "Main", "MAIN", NULL);
	} else if (scenario_is("delete")) {
		r = slotstore_delete(G_ROM, "Extra");
	} else if (scenario_is("delete-current")) {
		r = slotstore_delete(G_ROM, "Main");
	} else if (scenario_is("retire")) {
		r = slotstore_retire_file(G_SAVES "/Main.sav", "/torii/backups/G/Game.gba/Main");
	} else if (scenario_is("purge")) {
		struct saveslot_stamp now = {2026, 8, 1, 0, 0, 0};
		r = slotstore_trash_purge(&now);
	} else if (scenario_is("recover")) {
		r = slotstore_recover();
	}
	tlog("SLOT SCENARIO DONE: %d", r);
}
