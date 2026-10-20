// The names database on the cart (source/namesdb.h): a small names.db and a
// few games on the card. The first scan loads the database; Identify library
// computes the games' CRCs into crc.txt, and the database names them by CRC
// or by game code. Asset tests: run by tests/assets.sh on the emulated cart,
// and never in Diagnostics (they write the card and PSRAM's names).
#include <string.h>

#include "torii_test.h"
#include "tests.h"
#include "../../source/crc32.h"
#include "../../source/ff15/ff.h"
#include "../../source/library.h"
#include "../../source/namesdb.h"
#include "../../source/engine/cycles.h"
#include "../../source/engine/psram.h"
#include "../../source/engine/stage.h"
#include "../../source/waitcnt.h"

extern FATFS EZcardFs;

#define N_DIR "/NamesTest"

static const struct {
	const char *path;
	u8 seed;
	u16 size;
	const char *code;
} files[] = {
	{N_DIR "/Wars.gba", 1, 4096, "AWRE"},  // named by its CRC
	{N_DIR "/Hack.gba", 9, 4096, "BZME"},  // by its code: a hack of a listed game
	{N_DIR "/Plain.gba", 11, 3001, NULL},  // in no table: keeps its file name
	{N_DIR "/Mono.gbc", 5, 2048, NULL},  // a GB game named .gbc: the GB table
	{N_DIR "/Odd.nes", 7, 1234, NULL},
};
#define FILES (sizeof(files) / sizeof(files[0]))

// tests/names/fixture.py's pattern, so a file's bytes follow from its seed
static void fill(u8 *buf, u32 seed, u32 size, const char *code)
{
	for (u32 i = 0; i < size; i++)
		buf[i] = (u8)(i * seed + seed * 7 + (i >> 8));
	if (code)
		memcpy(buf + 0xAC, code, 4);
}

static void write_file(const char *path, const void *data, u32 size)
{
	FIL f;
	UINT done = 0;
	CHECK(f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
	CHECK(f_write(&f, data, size, &done) == FR_OK && done == size);
	CHECK(f_close(&f) == FR_OK);
}

static u32 crc_of(u32 i)
{
	u8 *buf = stage_buffer();
	fill(buf, files[i].seed, files[i].size, files[i].code);
	return crc32(buf, files[i].size);
}

static void put_entry(u8 *db, u32 *at, u32 key, u32 name)
{
	struct namesdb_entry e = {key, name};
	memcpy(db + *at, &e, sizeof(e));
	*at += sizeof(e);
}

// names.db with Wars's CRC (GBA), Mono's (GB) and the code BZME
static u32 make_db(u8 *db)
{
	static const char pool[] = "Advance Wars (USA)\0Mono Game (Japan)\0Minish Cap (USA)\0\0\0";
	struct namesdb_header h;
	memset(&h, 0, sizeof(h));
	h.magic = NAMESDB_MAGIC;
	h.version = NAMESDB_VERSION;
	h.header_bytes = sizeof(h);
	memcpy(h.dat_version, "2026.01.02", 10);
	h.crc_count[LIB_GBA] = 1;
	h.crc_count[LIB_GB] = 1;
	h.code_count = 1;
	h.pool_bytes = sizeof(pool) - 1;  // 56, a multiple of 4
	u32 at = sizeof(h);
	put_entry(db, &at, crc_of(0), 0);
	put_entry(db, &at, crc_of(3), 19);
	put_entry(db, &at, namesdb_code((const u8 *)"BZME"), 37);
	memcpy(db + at, pool, h.pool_bytes);
	h.file_bytes = at + h.pool_bytes;
	h.body_crc = crc32(db + sizeof(h), h.file_bytes - sizeof(h));
	h.header_crc = namesdb_header_crc(&h);
	memcpy(db, &h, sizeof(h));
	return h.file_bytes;
}

static const char *name_of(u32 i)
{
	return names_lookup(files[i].path, files[i].size, library_console_of(files[i].path));
}

void test_names_identify(void)
{
	u16 probe[2] ALIGN(4) = {0x5AA5, 0xF00F};
	psram_begin();
	psram_write((u8 *)PSRAM_NAMES_CACHE_BASE, probe, sizeof(probe));
	if (((const vu16 *)PSRAM_NAMES_CACHE_BASE)[1] != probe[1]) {
		tlog("  skipped: no PSRAM");
		return;
	}
	if (!EZcardFs.fs_type && f_mount(&EZcardFs, "", 1) != FR_OK) {
		tlog("  skipped: no card");
		return;
	}
	f_mkdir(N_DIR);
	f_mkdir("/torii");
	for (u32 i = 0; i < FILES; i++) {
		u8 *buf = stage_buffer();
		fill(buf, files[i].seed, files[i].size, files[i].code);
		write_file(files[i].path, buf, files[i].size);
	}
	u8 *db = stage_buffer() + 0x2000;
	write_file(NAMESDB_FILE, db, make_db(db));
	f_unlink("/torii/crc.txt");
	f_unlink("/torii/crc.txt.bak");
	f_unlink("/torii/library.idx");
	f_unlink("/torii/library.bak");

	// The first scan loads names.db; nothing has a CRC yet
	library_boot();
	CHECK(library_ready());
	const char *v = names_dat_version();
	CHECK(v && !strcmp(v, "2026.01.02"));
	for (u32 i = 0; i < FILES; i++)
		CHECK(name_of(i) == NULL);

	// Identify library: every game's CRC, and the names they find
	u32 start = cycles_now();
	names_identify();
	names_identify_now();
	tlog("  identified %u games in %lu cycles", (unsigned)FILES, (unsigned long)cycles_since(start));
	CHECK(!names_identifying(NULL, NULL));
	const char *wars = name_of(0), *hack = name_of(1), *mono = name_of(3);
	CHECK(wars && !strcmp(wars, "Advance Wars (USA)"));
	CHECK(hack && !strcmp(hack, "Minish Cap (USA)"));
	CHECK(name_of(2) == NULL);
	CHECK(mono && !strcmp(mono, "Mono Game (Japan)"));
	CHECK(name_of(4) == NULL);
	CHECK(names_lookup(files[0].path, files[0].size + 1, LIB_GBA) == NULL);  // another size
	FILINFO fi;
	CHECK(f_stat("/torii/crc.txt", &fi) == FR_OK && f_stat("/torii/crc.txt.bak", &fi) == FR_OK);
	// A second run reads nothing again; crc.txt survives a launch
	names_identify();
	names_identify_now();
	library_before_launch();
	CHECK(names_dat_version() == NULL);
	library_boot();  // loads the index: the names wait for its walk
	CHECK(name_of(0) == NULL);
	f_unlink("/torii/library.idx");
	f_unlink("/torii/library.bak");
	library_boot();  // a first scan again, which loads them
	wars = name_of(0);
	CHECK(wars && !strcmp(wars, "Advance Wars (USA)"));
	// What a walk changed (here it took Wars for changed) isn't written in
	// the way of a launch: the next walk finds the file as it is
	CHECK(names_walk_seen(files[0].path, files[0].size, 0x12345678, LIB_GBA) == NULL);
	CHECK(name_of(0) == NULL);
	library_before_launch();
	f_unlink("/torii/library.idx");
	f_unlink("/torii/library.bak");
	library_boot();
	wars = name_of(0);
	CHECK(wars && !strcmp(wars, "Advance Wars (USA)"));
	hack = name_of(1);
	CHECK(hack && !strcmp(hack, "Minish Cap (USA)"));

	library_before_launch();
	for (u32 i = 0; i < FILES; i++)
		f_unlink(files[i].path);
	f_unlink(N_DIR);
	f_unlink(NAMESDB_FILE);
	f_unlink("/torii/crc.txt");
	f_unlink("/torii/crc.txt.bak");
	f_unlink("/torii/library.idx");
	f_unlink("/torii/library.bak");
}

// tests/names_db.sh bench: Identify library over the card's games (1,000 of
// 32 KB there), timed; the first scan before it loads the index. Runs
// alone (SELECT+B held at power-on), then stops.
void names_bench(void)
{
	REG_WAITCNT_CART = WAITCNT_MENU;  // as the menu runs
	cycles_init();
	if (f_mount(&EZcardFs, "", 1) != FR_OK) {
		tlog("NAMES BENCH DONE: no card");
		return;
	}
	library_boot();
	u32 start = cycles_now(), games = 0;
	for (u32 c = 0; c < LIB_CONSOLES; c++)
		games += library_count(c);
	names_identify();
	names_identify_now();
	u32 ms = cycles_since(start) / 16780u;
	tlog("NAMES BENCH DONE: %lu games identified in %lu ms, %lu ms a game", (unsigned long)games, (unsigned long)ms,
	     (unsigned long)(games ? ms / games : 0));
}
