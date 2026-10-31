// The kernel unit tests. Every kernel has them, for Diagnostics
// (source/diag.c); the test kernel also runs them at boot, in place of the
// weak, empty torii_run_tests in source/tlog.c, then stops.
#include <gba_input.h>

#include "torii_test.h"
#include "tests.h"
#include "ui_demo.h"
#include "../../source/waitcnt.h"

int torii_test_failures;

static const struct {
	const char *name;
	void (*run)(void);
} tests[] = {
	{"check_save_mode", test_check_save_mode},
	{"get_save_mode", test_get_save_mode},
	{"save_file_size", test_save_file_size},
	{"save_mode_table", test_save_mode_table},
	{"nes_classics_use_eeprom", test_nes_classics_use_eeprom},
	{"file_type", test_file_type},
	{"file_type_short_names", test_file_type_short_names},
	{"cheat_cap", test_cheat_cap},
	{"cheatrun_matches_mgba", test_cheatrun_matches_mgba},
	{"cheatrun_cycles", test_cheatrun_cycles},
	{"cheatrun_off", test_cheatrun_off},
	{"cheatrun_once", test_cheatrun_once},
	{"game_hardware", test_game_hardware},
	{"clock_and_rumble_are_separate", test_clock_and_rumble_are_separate},
	{"settings_staged_until_changed", test_settings_staged_until_changed},
	{"settings_model_round_trip", test_settings_model_round_trip},
	{"nor_info_stays_in_bounds", test_nor_info_stays_in_bounds},
	{"nor_write_refused_when_list_full", test_nor_write_refused_when_list_full},
	{"nor_scan_keeps_interrupts", test_nor_scan_keeps_interrupts},
	{"menu_code_fits", test_menu_code_fits},
	{"spend_lowering", test_spend_lowering},
	{"pat_cache", test_pat_cache},
	{"build_id_matches_the_rom", test_build_id_matches_the_rom},
	{"engine_res_alloc", test_engine_res_alloc},
	{"engine_budget", test_engine_budget},
	{"engine_queue_caps", test_engine_queue_caps},
	{"engine_queue_2d", test_engine_queue_2d},
	{"engine_commit", test_engine_commit},
	{"engine_jobs", test_engine_jobs},
	{"engine_cycles", test_engine_cycles},
	{"engine_hold_for_queue", test_engine_hold_for_queue},
	{"text_blit", test_text_blit},
	{"text_draw", test_text_draw},
	{"text_clear", test_text_clear},
	{"text_cycles", test_text_cycles},
	{"fmt_cycles", test_fmt_cycles},
	{"diag_line_cut", test_diag_line_cut},
	{"ui_pool_and_arenas", test_ui_pool_and_arenas},
	{"theme_switch_loads_banks", test_theme_switch_loads_banks},
	{"ui_image_queue_full", test_ui_image_queue_full},
	{"ui_scroll_end", test_ui_scroll_end},
	{"asset_raw", test_asset_raw},
	{"asset_psram_round_trip", test_asset_psram_round_trip},
	{"asset_sd_read", test_asset_sd_read},
	{"asset_cover_cache", test_asset_cover_cache},
	{"art_table", test_art_table},
	{"art_obj_budget", test_art_obj_budget},
	{"art_colours", test_art_colours},
};

// The tests that need the cart (PSRAM, the card), run alone by
// tests/assets.sh on the emulated cart; the plain test kernel run has none
static const struct {
	const char *name;
	void (*run)(void);
} asset_tests[] = {
	{"asset_psram_round_trip", test_asset_psram_round_trip},
	{"asset_sd_read", test_asset_sd_read},
	{"asset_cover_cache", test_asset_cover_cache},
	// The user's lists on the card: on test files, never the user's
	{"safefile", test_safefile},
	{"userdata_recent_store", test_userdata_recent_store},
	{"userdata_fav_store", test_userdata_fav_store},
	// An SD file renamed: its save and its lists follow it
	{"rename_file", test_rename_file},
	// A game's cheat file on the card, and its picks at a launch
	{"cheatstore", test_cheatstore},
	{"cheatstore_clean", test_cheatstore_clean},
	{"slotstore", test_slotstore},
	{"saveorphan", test_saveorphan},
	// Not in Diagnostics: they use the index's PSRAM and write games to the card
	{"library_psram", test_library_psram},
	{"library_boot", test_library_boot},
	{"names_identify", test_names_identify},
	{"names_partial", test_names_partial},
	// Language packs on the card, into PSRAM
	{"lang_packs", test_lang_packs},
};

int torii_unit_tests(void)
{
	int failed_tests = 0;
	unsigned count = sizeof(tests) / sizeof(*tests);
	for (unsigned i = 0; i < count; i++) {
		int before = torii_test_failures;
		tests[i].run();
		int failed = torii_test_failures != before;
		failed_tests += failed;
		tlog("%s %s", failed ? "FAIL" : "PASS", tests[i].name);
	}
	tlog("TESTS DONE: %u passed, %d failed", count - failed_tests, failed_tests);
	return failed_tests;
}

int torii_asset_tests(void)
{
	int failed_tests = 0;
	unsigned count = sizeof(asset_tests) / sizeof(*asset_tests);
	for (unsigned i = 0; i < count; i++) {
		int before = torii_test_failures;
		asset_tests[i].run();
		int failed = torii_test_failures != before;
		failed_tests += failed;
		tlog("%s %s", failed ? "FAIL" : "PASS", asset_tests[i].name);
	}
	tlog("ASSET TESTS DONE: %u passed, %d failed", count - failed_tests, failed_tests);
	return failed_tests;
}

#ifdef TORII_TEST_KERNEL
void torii_run_tests(void)
{
	// SELECT held at power-on: the cart run (tests/assets.sh, on the
	// emulated cart). The other tests assume no
	// cart model: under it the NOR tests write the game NOR, then crash
	// SELECT and L: the UI demo on the cart (tests/shots/ui-*.opts)
	if ((~REG_KEYINPUT & 0x3FF) == (KEY_SELECT | KEY_L))
		ui_demo();
	// SELECT and B: the names bench (tests/names_db.sh bench), alone
	if ((~REG_KEYINPUT & 0x3FF) == (KEY_SELECT | KEY_B)) {
		names_bench();
		return;
	}
	// SELECT and R: the art sheet (tests/shots/art-sheet.opts)
	if ((~REG_KEYINPUT & 0x3FF) == (KEY_SELECT | KEY_R))
		art_sheet();
	// SELECT and A: the power-cut rig's scenario (tests/cutpoint.sh), alone
	if ((~REG_KEYINPUT & 0x3FF) == (KEY_SELECT | KEY_A)) {
		REG_WAITCNT_CART = WAITCNT_MENU;
		cut_scenario();
		return;
	}
	// SELECT and START: a slot store power-cut scenario (tests/slotstore.sh), alone
	if ((~REG_KEYINPUT & 0x3FF) == (KEY_SELECT | KEY_START)) {
		REG_WAITCNT_CART = WAITCNT_MENU;
		if (!orphan_scenario())
			slot_scenario();
		return;
	}
	if ((~REG_KEYINPUT & 0x3FF) == KEY_SELECT) {
		REG_WAITCNT_CART = WAITCNT_MENU;  // as the menu runs
		torii_asset_tests();
	}
	torii_unit_tests();
	// The engine's scene, or with L held (mgba-shot -k 1:L) the UI demo
	if (!(REG_KEYINPUT & KEY_L))
		ui_demo();
	engine_smoke();
}
#endif
