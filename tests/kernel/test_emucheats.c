// The poke tables in the built-in emulators' images (source/emulators.s):
// each emulator that runs RAM cheats has exactly one, empty in the shipped
// image (the kernel fills it in PSRAM at a launch), found by its magic.
#include "../../source/cheatemu.h"
#include "../../source/emulators.h"
#include "torii_test.h"
#include "tests.h"

static void check_image(const u8 *image, u32 size)
{
	u32 at = cheatemu_find_table(image, size);
	CHECK(at != 0xFFFFFFFFu);
	if (at == 0xFFFFFFFFu)
		return;
	const struct torii_cheats *t = (const struct torii_cheats *)(const void *)(image + at);
	CHECK_EQ(t->version, 1);
	CHECK_EQ(t->count, 0);
	CHECK_EQ(t->max, TORII_CHEATS_MAX);
	CHECK_EQ(t->e[0].kind, 0);
}

void test_emucheats_tables(void)
{
	check_image(pocketnes_gba, pocketnes_gba_size);
}
