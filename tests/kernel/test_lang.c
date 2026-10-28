// Language packs on the card (source/lang_load.c): found in /torii/lang,
// loaded into PSRAM, a damaged or stale one refused for English, and a
// launch's strings kept while PSRAM fills. They run on the emulated cart
// (tests/assets.sh), on test packs the test removes again.
#include <string.h>

#include <gba_base.h>

#include "../../source/engine/psram.h"
#include "../../source/engine/stage.h"
#include "../../source/lang.h"
#include "../../source/ui/ui.h"
#include "ff.h"
#include "lang_test_pack.h"
#include "torii_test.h"
#include "tests.h"

extern FATFS EZcardFs;

static const char *const packs[] = {"/torii/lang/t1.lng", "/torii/lang/t2.lng", "/torii/lang/t3.lng"};

static void remove_packs(void)
{
	for (u32 i = 0; i < sizeof(packs) / sizeof(packs[0]); i++)
		f_unlink(packs[i]);
	f_unlink("/torii/lang");  // only if the test made it: an empty folder
}

// Writes size bytes of the stage to path; 1 if it is there
static int put_pack(const char *path, u32 size)
{
	FIL f;
	UINT done = 0;
	f_mkdir("/torii");
	f_mkdir("/torii/lang");
	if (f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
		return 0;
	int ok = f_write(&f, stage_buffer(), size, &done) == FR_OK && done == size;
	return (f_close(&f) == FR_OK) & ok;
}

static u32 index_of(const char *code)
{
	for (u32 i = 1; i < lang_count(); i++)
		if (!strcmp(lang_choice(i)->code, code))
			return i;
	return 0;
}

void test_lang_packs(void)
{
	if (!EZcardFs.fs_type && f_mount(&EZcardFs, "", 1) != FR_OK) {
		CHECK(0);
		return;
	}
	static const struct lang_test_string strings[] = {
		{STR_ITEM_RECENT, "Senaste"},
		{STR_LAUNCH_PATCHING, "Patchar spelet"},
		{STR_FORMAT_DECIMAL, ","},
	};
	u8 *pack = stage_buffer();
	_Static_assert(STAGE_BYTES >= 0x2000, "a test pack fits the stage");
	remove_packs();
	// t1 good, t2 stale (built for other keys), t3 damaged (its header
	// right, a string's byte wrong)
	u32 size = lang_test_pack(pack, 0x2000, "t1", "Test one", strings, 3);
	CHECK(size && put_pack(packs[0], size));
	lang_test_put32(pack + 8, LANG_KEY_HASH ^ 1);
	lang_test_pack_seal(pack, size);
	memcpy(pack + 20, "t2", 2);
	CHECK(put_pack(packs[1], size));
	size = lang_test_pack(pack, 0x2000, "t3", "Test three", strings, 3);
	pack[size - 4] ^= 0x20;
	CHECK(put_pack(packs[2], size));

	// The boot finds t1 and t3 (t3's header reads right); Settings keeps English
	lang_boot();
	CHECK_EQ(lang_count(), 3);
	u32 t1 = index_of("t1"), t3 = index_of("t3");
	CHECK(t1 && t3 && !index_of("t2"));
	CHECK_EQ(lang_current(), 0);
	CHECK(!strcmp(lang_choice(t1)->name, "Test one"));

	// t1 loads into PSRAM; strings it lacks show in English
	CHECK(lang_switch(t1));
	CHECK_EQ(lang_current(), t1);
	CHECK(!strcmp(S(STR_ITEM_RECENT), "Senaste"));
	CHECK(psram_holds(S(STR_ITEM_RECENT), 8));
	CHECK(!strcmp(S(STR_ITEM_FAV), "Favourites"));
	char out[16];
	lang_fill(out, sizeof(out), "{1}", LT(15));
	CHECK(!strcmp(out, "1,5"));

	// t3 fails its whole check: English
	CHECK(!lang_switch(t3));
	CHECK_EQ(lang_current(), 0);
	CHECK(!lang_using_pack());
	CHECK(!strcmp(S(STR_ITEM_RECENT), "Recent"));

	// A launch: its strings stay as PSRAM fills, the rest are English
	// until the menu loads the pack again
	CHECK(lang_switch(t1));
	lang_before_launch();
	memset(pack, 0, 0x2000);
	psram_begin();
	CHECK(psram_write((void *)PSRAM_LANG_BASE, pack, 0x2000));
	CHECK(!strcmp(S(STR_LAUNCH_PATCHING), "Patchar spelet"));
	CHECK(!strcmp(S(STR_ITEM_RECENT), "Recent"));
	lang_poll();
	CHECK(!strcmp(S(STR_ITEM_RECENT), "Senaste"));
	CHECK(!strcmp(S(STR_LAUNCH_PATCHING), "Patchar spelet"));
	// The launch's dialogs start an engine session, which clears the row
	// cache: the strings stay. Something that wants the room back (another
	// borrower here, a list in the menu) takes them away first (English),
	// and the menu's poll loads the pack again
	lang_before_launch();
	ui_list_cache_clear();
	CHECK(!strcmp(S(STR_LAUNCH_PATCHING), "Patchar spelet"));
	ui_list_cache_lend(NULL);
	ui_list_cache_reclaim();
	CHECK(!strcmp(S(STR_LAUNCH_PATCHING), "Patching the game"));
	lang_poll();
	CHECK(!strcmp(S(STR_LAUNCH_PATCHING), "Patchar spelet"));

	// A pack gone from the card since the boot: English
	f_unlink(packs[0]);
	CHECK(!lang_switch(t1));
	CHECK(!strcmp(S(STR_ITEM_RECENT), "Recent"));

	CHECK(lang_switch(0));
	remove_packs();
	lang_list_reset();
}
