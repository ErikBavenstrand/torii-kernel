// Runs kernel code that needs no hardware (sorting, cheat parsing, patch tables,
// drawing name characters, motion and input) on the host, under AddressSanitizer and
// UndefinedBehaviorSanitizer (just test-host).
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
	{"sort_by_accessor", test_sort_by_accessor},
	{"cheat_parse", test_cheat_parse},
	{"cheat_reads_only_len", test_cheat_reads_only_len},
	{"cheat_long_fields", test_cheat_long_fields},
	{"cheat_cap", test_cheat_cap},
	{"cheat_garbage", test_cheat_garbage},
	{"cheat_empty_entries", test_cheat_empty_entries},
	{"cheat_whitespace", test_cheat_whitespace},
	{"text_utf8", test_text_utf8},
	{"text_width", test_text_width},
	{"text_layout", test_text_layout},
	{"text_layout_window", test_text_layout_window},
	{"text_layout_span", test_text_layout_span},
	{"progress_permille", test_progress_permille},
	{"progress_estimate", test_progress_estimate},
	{"save_name", test_save_name},
	{"save_name_longest", test_save_name_longest},
	{"name_with_ext", test_name_with_ext},
	{"irq_table_lookup", test_irq_table_lookup},
	{"irq_table_rejects_blank_codes", test_irq_table_rejects_blank_codes},
	{"irq_table_bounds", test_irq_table_bounds},
	{"irq_table_next_entry", test_irq_table_next_entry},
	{"irq_literals", test_irq_literals},
	{"hotkey_value", test_hotkey_value},
	{"rom_patch_table", test_rom_patch_table},
	{"rom_patch_matches", test_rom_patch_matches},
	{"log_ring_keeps_order", test_log_ring_keeps_order},
	{"log_ring_drops_whole_lines", test_log_ring_drops_whole_lines},
	{"log_ring_long_line", test_log_ring_long_line},
	{"log_ring_tail", test_log_ring_tail},
	{"log_ring_take_lines", test_log_ring_take_lines},
	{"log_header", test_log_header},
	{"log_pick_current", test_log_pick_current},
	{"log_next_seq", test_log_next_seq},
	{"log_end", test_log_end},
	{"lz77_round_trip", test_lz77_round_trip},
	{"lz77_compresses", test_lz77_compresses},
	{"nor_list_copies", test_nor_list_copies},
	{"kernel_nor_records", test_kernel_nor_records},
	{"loader_check", test_loader_check},
	{"nor_mark", test_nor_mark},
	{"logcheck_launches", test_logcheck_launches},
	{"logcheck_reports", test_logcheck_reports},
	{"eeprom_probe_patterns", test_eeprom_probe_patterns},
	{"eeprom_probe_verdict", test_eeprom_probe_verdict},
	{"ease_lut_reproducible", test_ease_lut_reproducible},
	{"ease_endpoints", test_ease_endpoints},
	{"tween_endpoints", test_tween_endpoints},
	{"spring_converges", test_spring_converges},
	{"spring_dt_clamp", test_spring_dt_clamp},
	{"spring_retarget", test_spring_retarget},
	{"spring_jump_and_shift", test_spring_jump_and_shift},
	{"motion_targets", test_motion_targets},
	{"motion_cap_and_handles", test_motion_cap_and_handles},
	{"motion_range_before_alloc", test_motion_range_before_alloc},
	{"ui_layout_anchors", test_ui_layout_anchors},
	{"ui_patches", test_ui_patches},
	{"theme_keys", test_theme_keys},
	{"theme_refused", test_theme_refused},
	{"theme_warnings", test_theme_warnings},
	{"theme_formats", test_theme_formats},
	{"theme_names", test_theme_names},
	{"theme_example", test_theme_example},
	{"theme_garbage", test_theme_garbage},
	{"ui_palette", test_ui_palette},
	{"ui_ring_setup", test_ui_ring_setup},
	{"ui_ring_windows", test_ui_ring_windows},
	{"ui_ring_invariant", test_ui_ring_invariant},
	{"ui_stack", test_ui_stack},
	{"ui_focus_and_keys", test_ui_focus_and_keys},
	{"palette_fade", test_palette_fade},
	{"input_tap", test_input_tap},
	{"input_repeat_schedule", test_input_repeat_schedule},
	{"input_accel_reset", test_input_accel_reset},
	{"input_paging", test_input_paging},
	{"input_wrap", test_input_wrap},
	{"pool_alloc_free_merge", test_pool_alloc_free_merge},
	{"pool_align_range_top", test_pool_align_range_top},
	{"pool_tags_cache_revive", test_pool_tags_cache_revive},
	{"pool_slots", test_pool_slots},
	{"pool_random", test_pool_random},
	{"res_arena", test_res_arena},
	{"pool_tag_place", test_pool_tag_place},
	{"pool_slots_exact_fit", test_pool_slots_exact_fit},
	{"pool_age_wrap", test_pool_age_wrap},
	{"budget_check", test_budget_check},
	{"asset_header", test_asset_header},
	{"asset_check_refuses", test_asset_check_refuses},
	{"cover_cache_wants", test_cover_cache_wants},
	{"cover_cache_eviction", test_cover_cache_eviction},
	{"affine_rotscale", test_affine_rotscale},
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
