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
void test_settings_model_round_trip(void);
void test_settings_staged_until_changed(void);
void test_safefile(void);
void cut_scenario(void);
void test_userdata_recent_store(void);
void test_userdata_fav_store(void);
void test_rename_file(void);
void test_slotstore(void);
void slot_scenario(void);
void test_saveorphan(void);
int orphan_scenario(void);
void test_nor_info_stays_in_bounds(void);
void test_nor_write_refused_when_list_full(void);
void test_nor_scan_keeps_interrupts(void);
void test_menu_code_fits(void);
void test_build_id_matches_the_rom(void);
void test_text_blit(void);
void test_text_draw(void);
void test_text_clear(void);
void test_text_cycles(void);
void test_diag_line_cut(void);
void test_engine_res_alloc(void);
void test_engine_budget(void);
void test_engine_queue_caps(void);
void test_engine_queue_2d(void);
void test_engine_commit(void);
void test_engine_jobs(void);
void test_engine_cycles(void);
void test_engine_hold_for_queue(void);
void test_ui_pool_and_arenas(void);
void test_theme_switch_loads_banks(void);
void test_ui_image_queue_full(void);
void test_ui_scroll_end(void);
void test_asset_raw(void);
void test_asset_psram_round_trip(void);
void test_asset_sd_read(void);
void test_asset_cover_cache(void);
void test_art_table(void);
void test_art_obj_budget(void);
void test_art_colours(void);
void test_library_psram(void);
void test_library_boot(void);
void test_names_identify(void);
void test_names_partial(void);
void names_bench(void);

// Test assets (asset_data.c, written by tools/asset.py demo)
#include <gba_base.h>
extern const u32 test_asset_small[];
extern const u32 test_asset_small_bytes;
extern const u32 test_asset_small_crc;

// The asset tests alone, with the cart (tests/assets.sh): returns how many failed.
int torii_asset_tests(void);

// The test kernel's sheet of the menu's art (art_sheet.c).
void art_sheet(void) __attribute__((noreturn));

// The test kernel's engine scene, shown after the tests (engine_smoke.c).
void engine_smoke(void) __attribute__((noreturn));

#endif
