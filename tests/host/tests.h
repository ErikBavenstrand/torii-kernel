// The host tests, each a void function run by tests/host/run.c.
#ifndef TORII_HOST_TESTS_H
#define TORII_HOST_TESTS_H

void test_sort_matches_kernel(void);
void test_sort_is_stable(void);
void test_sort_limits(void);
void test_sort_largest_list(void);
void test_cheat_parse(void);
void test_cheat_reads_only_len(void);
void test_cheat_long_fields(void);
void test_cheat_cap(void);
void test_cheat_garbage(void);
void test_cheat_empty_entries(void);
void test_cheat_whitespace(void);
void test_charset_ascii(void);
void test_charset_latin(void);
void test_charset_other_scripts(void);
void test_charset_broken(void);
void test_save_name(void);
void test_save_name_longest(void);
void test_irq_table_lookup(void);
void test_irq_table_rejects_blank_codes(void);
void test_irq_table_bounds(void);
void test_irq_table_next_entry(void);
void test_irq_literals(void);
void test_hotkey_value(void);
void test_rom_patch_table(void);
void test_rom_patch_matches(void);
void test_log_ring_keeps_order(void);
void test_log_ring_drops_whole_lines(void);
void test_log_ring_long_line(void);
void test_log_ring_tail(void);
void test_log_ring_take_lines(void);
void test_log_header(void);
void test_log_pick_current(void);
void test_log_find_end(void);
void test_lz77_round_trip(void);
void test_lz77_compresses(void);
void test_logcheck_launches(void);
void test_logcheck_reports(void);
void test_eeprom_probe_patterns(void);
void test_eeprom_probe_verdict(void);

#endif
