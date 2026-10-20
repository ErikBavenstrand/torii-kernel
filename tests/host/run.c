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
	{"library_extensions", test_library_extensions},
	{"library_sort_key", test_library_sort_key},
	{"library_build_and_sort", test_library_build_and_sort},
	{"library_sort_random", test_library_sort_random},
	{"library_cap", test_library_cap},
	{"library_file_round_trip", test_library_file_round_trip},
	{"library_forget_rename", test_library_forget_rename},
	{"library_custom_display", test_library_custom_display},
	{"library_pinned_keys", test_library_pinned_keys},
	{"letter_of", test_letter_of},
	{"letter_sorted", test_letter_sorted},
	{"letter_jump", test_letter_jump},
	{"letter_one", test_letter_one},
	{"colour_bldy", test_colour_bldy},
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
	{"settings_table", test_settings_table},
	{"settings_defaults", test_settings_defaults},
	{"settings_staged_values", test_settings_staged_values},
	{"settings_hotkeys", test_settings_hotkeys},
	{"settings_visible_rows", test_settings_visible_rows},
	{"settings_clock_format", test_settings_clock_format},
	{"settings_clock_editor", test_settings_clock_editor},
	{"settings_clock_apply", test_settings_clock_apply},
	{"text_width", test_text_width},
	{"text_layout", test_text_layout},
	{"text_layout_window", test_text_layout_window},
	{"text_layout_span", test_text_layout_span},
	{"charset_cut", test_charset_cut},
	{"progress_permille", test_progress_permille},
	{"progress_estimate", test_progress_estimate},
	{"progress_copy", test_progress_copy},
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
	{"theme_colour_mix", test_theme_colour_mix},
	{"theme_banks_neutral", test_theme_banks_neutral},
	{"theme_banks_item", test_theme_banks_item},
	{"theme_text_contrast", test_theme_text_contrast},
	{"theme_select", test_theme_select},
	{"ui_ring_setup", test_ui_ring_setup},
	{"ui_ring_vofs", test_ui_ring_vofs},
	{"ui_reading", test_ui_reading},
	{"ui_ring_windows", test_ui_ring_windows},
	{"ui_ring_invariant", test_ui_ring_invariant},
	{"ui_glide_held", test_ui_glide_held},
	{"ui_glide_held_pitches", test_ui_glide_held_pitches},
	{"ui_glide_end", test_ui_glide_end},
	{"ui_glide_taken", test_ui_glide_taken},
	{"ui_glide_pages", test_ui_glide_pages},
	{"ui_stack", test_ui_stack},
	{"rowcache_keys", test_rowcache_keys},
	{"rowcache_eviction", test_rowcache_eviction},
	{"rowcache_copy", test_rowcache_copy},
	{"rowcache_random", test_rowcache_random},
	{"rowcache_two_lists", test_rowcache_two_lists},
	{"ui_focus_and_keys", test_ui_focus_and_keys},
	{"palette_fade", test_palette_fade},
	{"input_tap", test_input_tap},
	{"input_ramp_curve", test_input_ramp_curve},
	{"input_repeat_schedule", test_input_repeat_schedule},
	{"input_accel_reset", test_input_accel_reset},
	{"input_paging", test_input_paging},
	{"input_wrap", test_input_wrap},
	{"keyq_two_presses", test_keyq_two_presses},
	{"keyq_held_repeats", test_keyq_held_repeats},
	{"keyq_carry", test_keyq_carry},
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
	{"gamename_clean_real_names", test_gamename_clean_real_names},
	{"gamename_clean_rules", test_gamename_clean_rules},
	{"gamename_clean_limits", test_gamename_clean_limits},
	{"gamename_dedupe", test_gamename_dedupe},
	{"gamename_custom_parse", test_gamename_custom_parse},
	{"gamename_custom_set", test_gamename_custom_set},
	{"gamename_custom_move", test_gamename_custom_move},
	{"gamename_custom_io", test_gamename_custom_io},
	{"gamename_display", test_gamename_display},
	{"gamename_hidden", test_gamename_hidden},
	{"safefile_crc_line", test_safefile_crc_line},
	{"safefile_scan", test_safefile_scan},
	{"safefile_pick", test_safefile_pick},
	{"safefile_lines", test_safefile_lines},
	{"registry_shown", test_registry_shown},
	{"registry_nearest", test_registry_nearest},
	{"registry_start", test_registry_start},
	{"registry_boot_word", test_registry_boot_word},
	{"userdata_paths", test_userdata_paths},
	{"userdata_parse", test_userdata_parse},
	{"userdata_recent_lines", test_userdata_recent_lines},
	{"userdata_recent_edits", test_userdata_recent_edits},
	{"userdata_favourites", test_userdata_favourites},
	{"userdata_sort_big", test_userdata_sort_big},
	{"userdata_clock", test_userdata_clock},
	{"userdata_ago", test_userdata_ago},
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
