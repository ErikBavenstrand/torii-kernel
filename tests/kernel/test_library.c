// The library index on the cart (source/library.h): a slot built, sorted
// and changed in PSRAM reads back as on the host, and the boot's first scan,
// the write before a launch and the next boot's load agree. Asset tests: run
// by tests/assets.sh on the emulated cart, with an empty card, and never in
// Diagnostics: they overwrite the index in PSRAM and write games to the card.
#include <string.h>

#include "torii_test.h"
#include "tests.h"
#include "../../source/ff15/ff.h"
#include "../../source/library.h"
#include "../../source/engine/psram.h"
#include "../../source/engine/stage.h"

extern FATFS EZcardFs;

static void put(void *dst, const void *src, u32 bytes)
{
	CHECK(psram_write(dst, src, bytes));
}

// 1 where PSRAM keeps what is written (always on hardware; in mGBA only
// with the cart model)
static int have_psram(u8 *at)
{
	u16 probe[2] ALIGN(4) = {0x5AA5, 0xF00F};
	psram_begin();
	psram_write(at, probe, sizeof(probe));
	if (((const vu16 *)at)[0] == probe[0] && ((const vu16 *)at)[1] == probe[1])
		return 1;
	tlog("  skipped: no PSRAM");
	return 0;
}

void test_library_psram(void)
{
	u8 *slot = (u8 *)PSRAM_LIBRARY_BASE + LIB_SLOT_BYTES;  // the second slot
	if (!have_psram(slot))
		return;
	// The builder and the sort's buffer in the stage: no EWRAM of the tests' own
	struct lib_build *b = (struct lib_build *)stage_buffer();
	struct lib_sort *s = (struct lib_sort *)(stage_buffer() + 0x1000);
	u32 *seen = (u32 *)(stage_buffer() + 0x2000);
	static const char *const names[] = {"Mega Man 10.gba",  "Mega Man 2.gba", "Zelda.gbc",
					    "advance wars.gba", "Tetris.gb",      "Pok\xC3\xA9mon.gba"};
	lib_build_begin(b, slot, put);
	u16 root = lib_build_folder(b, "/");
	u16 sub = lib_build_folder(b, "/GBA");
	for (u32 i = 0; i < 6; i++)
		CHECK(lib_build_game(b, i & 1 ? sub : root, names[i], NULL, 100 + i, library_console_of(names[i]),
				     i == 0 ? LIB_F_SAVE : 0));
	struct lib_header h;
	lib_build_header(b, &h, 1, 2, 6);
	lib_sort_begin(s, slot, (u8 *)PSRAM_LIBRARY_SCRATCH + 0x8000, 6, put);
	while (!lib_sort_step(s, 2))
		;
	CHECK(lib_body_ok(slot, seen));
	CHECK_EQ(lib_hdr(slot)->counts[LIB_GBA], 4);
	static const char *const gba[] = {"advance wars.gba", "Mega Man 2.gba", "Mega Man 10.gba",
					  "Pok\xC3\xA9mon.gba"};
	for (u32 i = 0; i < 4; i++)
		CHECK(!strcmp(lib_name(slot, lib_nth(slot, LIB_GBA, i)), gba[i]));
	char path[64];
	CHECK(lib_path(slot, lib_nth(slot, LIB_GBA, 1), path, sizeof(path)));
	CHECK(!strcmp(path, "/GBA/Mega Man 2.gba"));
	CHECK_EQ(lib_find(slot, "/Mega Man 10.gba"), 0);
	CHECK(lib_rec_at(slot, 0)->flags & LIB_F_SAVE);
	// Changes through psram_write
	CHECK(lib_forget(slot, put, lib_find(slot, "/GBA/Mega Man 2.gba")));
	CHECK(lib_rename(slot, put, 0, "Zoo.gba", b));
	CHECK(lib_body_ok(slot, seen));
	CHECK(!strcmp(lib_name(slot, lib_nth(slot, LIB_GBA, 2)), "Zoo.gba"));
	CHECK_EQ(lib_hdr(slot)->counts[LIB_GBA], 3);
}

static void make_game(const char *path)
{
	FIL f;
	UINT done = 0;
	u8 *buf = stage_buffer();
	memset(buf, 0x5A, 600);
	CHECK(f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
	CHECK(f_write(&f, buf, 600, &done) == FR_OK && done == 600);
	CHECK(f_close(&f) == FR_OK);
}

// The games it adds, in a folder of its own (the cover demo's card has others)
#define T_DIR "/LibraryTest"
// Gamma's save is in its folder under /torii/saves; Alpha and Beta have none
#define T_SAVES "/torii/saves" T_DIR "/Old/Gamma.gba"
static const char *const test_games[] = {T_DIR "/Beta.gba", T_DIR "/Old/Alpha.gbc", T_DIR "/Old/Gamma.gba",
					 T_SAVES "/Main.sav"};
#define TEST_GAMES (sizeof(test_games) / sizeof(*test_games))

void test_library_boot(void)
{
	if (!have_psram((u8 *)PSRAM_LIBRARY_BASE))
		return;
	if (!EZcardFs.fs_type && f_mount(&EZcardFs, "", 1) != FR_OK) {
		tlog("  skipped: no card");
		return;
	}
	f_mkdir(T_DIR);
	f_mkdir(T_DIR "/Old");
	f_mkdir("/torii");
	f_mkdir("/torii/saves");
	f_mkdir("/torii/saves" T_DIR);
	f_mkdir("/torii/saves" T_DIR "/Old");
	f_mkdir(T_SAVES);
	for (u32 i = 0; i < TEST_GAMES; i++)
		make_game(test_games[i]);
	f_unlink("/torii/library.idx");
	f_unlink("/torii/library.bak");
	library_boot();
	CHECK(library_ready());
	u32 gba = library_count(LIB_GBA), files = library_total_files();
	CHECK(gba >= 2 && library_count(LIB_GBC) >= 1 && files >= 3);
	u32 gamma = library_find(T_DIR "/Old/Gamma.gba"), beta = library_find(T_DIR "/Beta.gba");
	CHECK(gamma != LIBRARY_NONE && beta != LIBRARY_NONE);
	CHECK(library_find(T_DIR "/Old/Alpha.gbc") != LIBRARY_NONE);
	CHECK(!strcmp(library_name(gamma), "Gamma.gba"));
	CHECK(library_flags(gamma) & LIB_F_SAVE);
	CHECK(!(library_flags(beta) & LIB_F_SAVE));
	CHECK(!(library_flags(library_find(T_DIR "/Old/Alpha.gbc")) & LIB_F_SAVE));
	CHECK_EQ(library_size(gamma), 600);
	char path[64];
	CHECK(library_path(gamma, path, sizeof(path)));
	CHECK(!strcmp(path, T_DIR "/Old/Gamma.gba"));
	// The first scan writes the index before the menu; a launch forgets it
	FILINFO fi;
	CHECK(f_stat("/torii/library.idx", &fi) == FR_OK);
	CHECK(!library_busy());  // a first scan needs no walk after it
	library_before_launch();
	CHECK(!library_ready());
	// The next boot loads it
	library_boot();
	CHECK(library_ready());
	CHECK_EQ(library_count(LIB_GBA), gba);
	CHECK_EQ(library_total_files(), files);
	CHECK_EQ(library_find(T_DIR "/Beta.gba"), beta);
	// The menu deletes a game: it leaves the lists at once
	u32 gen = library_generation();
	library_forget(T_DIR "/Beta.gba");
	CHECK(library_generation() != gen);
	CHECK_EQ(library_count(LIB_GBA), gba - 1);
	CHECK_EQ(library_find(T_DIR "/Beta.gba"), LIBRARY_NONE);
	library_before_launch();
	for (u32 i = 0; i < TEST_GAMES; i++)
		f_unlink(test_games[i]);
	f_unlink(T_SAVES);
	f_unlink("/torii/saves" T_DIR "/Old");
	f_unlink("/torii/saves" T_DIR);
	f_unlink(T_DIR "/Old");
	f_unlink(T_DIR);
	f_unlink("/torii/library.idx");
	f_unlink("/torii/library.bak");
}
