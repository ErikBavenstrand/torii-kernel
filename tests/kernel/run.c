// The kernel unit tests. Every kernel has them, for Diagnostics
// (source/diag.c); the test kernel also runs them at boot, in place of the
// weak, empty torii_run_tests in source/tlog.c, then stops.
#include "torii_test.h"
#include "tests.h"

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
	{"cheat_parse", test_cheat_parse},
	{"cheat_cap", test_cheat_cap},
	{"game_hardware", test_game_hardware},
	{"clock_and_rumble_are_separate", test_clock_and_rumble_are_separate},
	{"sort_by_name", test_sort_by_name},
	{"sort_full_list", test_sort_full_list},
	{"recently_played", test_recently_played},
	{"nor_info_stays_in_bounds", test_nor_info_stays_in_bounds},
	{"nor_write_refused_when_list_full", test_nor_write_refused_when_list_full},
	{"menu_code_fits", test_menu_code_fits},
	{"build_id_matches_the_rom", test_build_id_matches_the_rom},
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

#ifdef TORII_TEST_KERNEL
void torii_run_tests(void)
{
	torii_unit_tests();
	for (;;)
		;
}
#endif
