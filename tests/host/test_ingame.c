// The in-game menu's ABI (source/ingame_abi.h) and the in-game probe's
// report (source/ingameprobe.h): the numbers the stub in gba_rts_patch.s, the
// overlay, the probe ROM and the tests copy by hand.
#include "../../source/ingame_abi.h"
#include "../../source/ingameprobe.h"
#include "../kernel/torii_test.h"
#include "tests.h"

void test_ingame_abi(void)
{
	// gba_rts_patch.s: SP_HDR's 32 bytes hold the header, the check word last
	CHECK_EQ(offsetof(struct ingame_image, obj_bytes) + 4, 24u);
	CHECK_EQ(offsetof(struct ingame_image, check), 28u);
	CHECK_EQ(sizeof(struct ingame_image), 32u);
	// The stub's check loop: add r0,word,r0,ror #27 (a rotate left by 5)
	const uint32_t words[3] = {1, 0x80000000u, 0x12345678u};
	CHECK_EQ(ingame_image_check(words, 0), 0u);
	CHECK_EQ(ingame_image_check(words, 1), 1u);
	CHECK_EQ(ingame_image_check(words, 2), 0x80000020u);
	CHECK_EQ(ingame_image_check(words, 3), ((0x80000020u << 5 | 0x80000020u >> 27) + 0x12345678u));
	// The stub's refusal record: "TINR", "FED2", reason, WAITCNT, detail,
	// what the check saw, VCOUNT and tries; and its screen's colour by reason
	CHECK_EQ(INGAME_REFUSED_MAGIC0, 0x524E4954u);
	CHECK_EQ(INGAME_REFUSED_MAGIC1, 0x32444546u);
	CHECK_EQ(INGAME_REFUSED_IMAGE, 3u);
	CHECK_EQ(INGAME_REFUSED_SIZES, 4u);
	CHECK_EQ(INGAME_REFUSED_IWRAM + sizeof(struct ingame_refused), 0x03007E00u);  // under SoftReset's 0x200
	const uint16_t colours[] = INGAME_REFUSED_COLOURS;
	CHECK_EQ(colours[ingame_refused_colour(INGAME_REFUSED_NO_IMAGE, 0)], 0x7C00u);
	CHECK_EQ(colours[ingame_refused_colour(INGAME_REFUSED_BACKUP, 1)], 0x001Fu);
	CHECK_EQ(colours[ingame_refused_colour(INGAME_REFUSED_BACKUP, 2)], 0x03FFu);
	CHECK_EQ(colours[ingame_refused_colour(INGAME_REFUSED_BACKUP, 3)], 0x7C1Fu);
	CHECK_EQ(colours[ingame_refused_colour(INGAME_REFUSED_IMAGE, 0)], 0x03E0u);
	CHECK_EQ(colours[ingame_refused_colour(INGAME_REFUSED_SIZES, 0)], 0x7FE0u);
	CHECK_EQ(ingame_refused_colour(INGAME_REFUSED_BACKUP, 9), 0u);
	// Each a colour of its own, but EWRAM's red at 2 (never shown: BACKUP has a copy)
	for (unsigned i = 1; i < 8; i++)
		for (unsigned j = i + 1; j < 8; j++)
			CHECK(i == 2 || colours[i] != colours[j]);
	CHECK_EQ(INGAME_IMAGE_MAGIC, 0x31564F54u);  // "TOV1", as the stub and hook_watch.lua have it
	CHECK_EQ(INGAME_BACKUP_EWRAM, 0x20000u);
	CHECK_EQ(INGAME_BACKUP_OBJ, 0x28000u);
	CHECK_EQ(INGAME_BACKUP_OAM, 0x2C000u);
	CHECK_EQ(INGAME_BACKUP_OBJ_PAL, 0x2C400u);
	CHECK_EQ(INGAME_SRAM_IMAGE, 0xB0000u);
	CHECK_EQ(INGAME_ACT_SLEEP, 4u);
	// A text line: 12 rows of 176 px (the menu's title and footer; the lists' rows use 160), and the head the overlay reads
	CHECK_EQ(sizeof(struct ingame_line), 268u);
	// The cheat list (P2): an entry's fields after its line as the overlay
	// reads them (two words from cost), the toggles past the BIOS's SVC stack
	// and before its words at 0x3007FF0 (cheatrun.s reads them from the
	// mirror at 0x4000000 - 0x18 and - 0x14), and a reason in its bits
	CHECK_EQ(offsetof(struct ingame_cheat, cost), sizeof(struct ingame_line));
	CHECK_EQ(offsetof(struct ingame_cheat, flags) - offsetof(struct ingame_cheat, cost), 6u);
	CHECK_EQ(sizeof(struct ingame_cheat), 276u);
	CHECK_EQ(INGAME_CHEAT_TOGGLES, 0x03007FE8u);
	CHECK_EQ(0x04000000u - 0x18 - 0x00FF8000u, INGAME_CHEAT_TOGGLES);
	CHECK(INGAME_CHEAT_TOGGLES + 8 <= 0x03007FF0u && INGAME_CHEAT_TOGGLES >= INGAME_CHEAT_ONOFF + 8);
	CHECK(INGAME_WHYS - 1 <= INGAME_CHEAT_WHY);
	CHECK(INGAME_SESSION_CHEAT(INGAME_CHEATS) <= INGAME_SESSION_PATH);
	// The state slots (P3): the stub's TAG_PAGE and the autoload byte's
	// place in its window (gba_rts_patch.s), the buffer's size as the old
	// /RTS files have it, and the slots' rows in the session block
	CHECK_EQ(INGAME_SRAM_STATE_TAG >> 12, 0xAFu);
	CHECK_EQ((INGAME_SRAM_STATE_TAG + offsetof(struct ingame_state_tag, autoload)) & 0xFFF, 0xFE6u);
	CHECK_EQ(INGAME_STATE_BYTES, 0x70000u);
	CHECK_EQ(INGAME_SRAM_STATE + INGAME_STATE_BYTES, INGAME_SRAM_IMAGE);
	CHECK_EQ(INGAME_SESSION_STATE(2, 1) - INGAME_SESSION_STATE(1, 1), sizeof(struct ingame_line));
	CHECK(INGAME_SESSION_STATE(INGAME_STATE_SLOTS, 0) + sizeof(struct ingame_line) <= INGAME_SESSION_SAVE_SLOT);
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
