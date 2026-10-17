// Runs the kernel unit tests at boot in the test kernel, in place of the
// weak, empty torii_run_tests in source/tlog.c, then stops.
#include "torii_test.h"

int torii_test_failures;

void test_check_save_mode(void);
void test_get_save_mode(void);
void test_save_file_size(void);
void test_save_mode_table(void);
void test_nes_classics_use_eeprom(void);
void test_file_type(void);
void test_cheat_parse(void);
void test_cheat_cap(void);
void test_rtc_and_rumble_are_separate(void);
void test_nor_info_stays_in_bounds(void);

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
	{"cheat_parse", test_cheat_parse},
	{"cheat_cap", test_cheat_cap},
	{"rtc_and_rumble_are_separate", test_rtc_and_rumble_are_separate},
	{"nor_info_stays_in_bounds", test_nor_info_stays_in_bounds},
};

void torii_run_tests(void)
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
	for (;;)
		;
}
