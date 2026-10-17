// Runs kernel code that needs no hardware (sorting, cheat parsing, patch tables) on the
// host, under AddressSanitizer and UndefinedBehaviorSanitizer (just test-host).
// Prints one PASS or FAIL line per test, like the kernel unit tests.
#include <stdarg.h>
#include <stdio.h>

#include "../../source/cheat.h"
#include "../kernel/torii_test.h"
#include "tests.h"

int torii_test_failures;

// The kernel defines these in source/showcht.c, which needs the hardware.
ST_entry pCHEAT[CHEAT_MAX];
u32 gl_cheat_count;

void tlog(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
	putchar('\n');
}

void tlog_error(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	printf("  ");
	vprintf(format, args);
	va_end(args);
	putchar('\n');
}

static const struct {
	const char *name;
	void (*run)(void);
} tests[] = {
	{"sort_matches_kernel", test_sort_matches_kernel},
	{"sort_is_stable", test_sort_is_stable},
	{"sort_limits", test_sort_limits},
	{"sort_largest_list", test_sort_largest_list},
	{"cheat_parse", test_cheat_parse},
	{"cheat_reads_only_len", test_cheat_reads_only_len},
	{"cheat_long_fields", test_cheat_long_fields},
	{"cheat_cap", test_cheat_cap},
	{"cheat_garbage", test_cheat_garbage},
	{"cheat_empty_entries", test_cheat_empty_entries},
	{"cheat_whitespace", test_cheat_whitespace},
	{"irq_table_lookup", test_irq_table_lookup},
	{"irq_table_rejects_blank_codes", test_irq_table_rejects_blank_codes},
	{"irq_table_bounds", test_irq_table_bounds},
	{"irq_literals", test_irq_literals},
	{"hotkey_value", test_hotkey_value},
	{"rom_patch_table", test_rom_patch_table},
	{"rom_patch_matches", test_rom_patch_matches},
};

int main(void)
{
	// Line by line, so the lines before a sanitizer abort aren't lost.
	setvbuf(stdout, NULL, _IOLBF, 0);
	int failed_tests = 0;
	unsigned count = sizeof(tests) / sizeof(*tests);
	for (unsigned i = 0; i < count; i++) {
		int before = torii_test_failures;
		tests[i].run();
		int failed = torii_test_failures != before;
		failed_tests += failed;
		printf("%s %s\n", failed ? "FAIL" : "PASS", tests[i].name);
	}
	printf("HOST TESTS DONE: %d passed, %d failed\n", (int)count - failed_tests, failed_tests);
	return failed_tests != 0;
}
