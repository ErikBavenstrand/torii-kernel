// The in-game menu's ABI (source/ingame_abi.h) and the in-game probe's
// report (source/ingameprobe.h): the numbers the stub in gba_rts_patch.s, the
// overlay, the probe ROM and the tests copy by hand.
#include "../../source/ingame_abi.h"
#include "../../source/ingameprobe.h"
#include "../kernel/torii_test.h"
#include "tests.h"

void test_ingame_abi(void)
{
	// gba_rts_patch.s: SP_HDR's 24 bytes hold the header up to obj_bytes
	CHECK_EQ(offsetof(struct ingame_image, obj_bytes) + 4, 24u);
	CHECK_EQ(sizeof(struct ingame_image), 32u);
	CHECK_EQ(INGAME_IMAGE_MAGIC, 0x31564F54u);  // "TOV1", as the stub and hook_watch.lua have it
	CHECK_EQ(INGAME_BACKUP_EWRAM, 0x20000u);
	CHECK_EQ(INGAME_BACKUP_OBJ, 0x28000u);
	CHECK_EQ(INGAME_BACKUP_OAM, 0x2C000u);
	CHECK_EQ(INGAME_BACKUP_OBJ_PAL, 0x2C400u);
	CHECK_EQ(INGAME_SRAM_IMAGE, 0xB0000u);
	CHECK_EQ(INGAME_ACT_SLEEP, 4u);
	// A text line: 12 rows of 128 px, and the head the overlay reads
	CHECK_EQ(sizeof(struct ingame_line), 196u);
	CHECK(INGAME_SESSION_HEAD < 256);
	CHECK(INGAME_SESSION_TEXT(INGAME_TEXTS) <= INGAME_SESSION_OUT);
	CHECK(sizeof(struct ingame_session) <= INGAME_SESSION_MAX);
	// The probe: its report fits its 256 bytes of page 0xF0, the scratch
	// sector's place in it is what the kernel writes (offset 16), and its
	// bytes are tests/ingame_probe.sh's
	CHECK(sizeof(struct ingame_probe) <= 0x100);
	CHECK_EQ(offsetof(struct ingame_probe, scratch_lba), 16u);
	CHECK_EQ(ingame_probe_byte(0, INGAME_PROBE_SEED_KERNEL), 0x5Au);
	CHECK_EQ(ingame_probe_byte(9, INGAME_PROBE_SEED_KERNEL), (uint8_t)((9 * 7 + 0x5A) ^ 1));
}
