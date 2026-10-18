// The unit tests, each a void function run by tests/kernel/run.c.
#ifndef TORII_TESTS_H
#define TORII_TESTS_H

void test_check_save_mode(void);
void test_get_save_mode(void);
void test_save_file_size(void);
void test_save_mode_table(void);
void test_nes_classics_use_eeprom(void);
void test_file_type(void);
void test_file_type_short_names(void);
void test_cheat_parse(void);
void test_cheat_cap(void);
void test_game_hardware(void);
void test_clock_and_rumble_are_separate(void);
void test_sort_by_name(void);
void test_sort_full_list(void);
void test_recently_played(void);
void test_recently_played_nor(void);
void test_nor_info_stays_in_bounds(void);
void test_nor_write_refused_when_list_full(void);
void test_nor_scan_keeps_interrupts(void);
void test_menu_code_fits(void);
void test_build_id_matches_the_rom(void);
void test_engine_res_alloc(void);
void test_engine_budget(void);
void test_engine_queue_caps(void);
void test_engine_commit(void);
void test_engine_jobs(void);
void test_engine_cycles(void);
void test_engine_hold_for_queue(void);

// The test kernel's engine scene, shown after the tests (engine_smoke.c).
void engine_smoke(void) __attribute__((noreturn));

#endif
