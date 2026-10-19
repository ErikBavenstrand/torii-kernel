#ifndef _LANG_H
#define _LANG_H

#include <gba_base.h>

extern char *gl_init_error;
extern char *gl_power_off;
extern char *gl_init_ok;
extern char *gl_Loading;
extern char *gl_theme_error;
extern char *gl_file_overflow;

extern char *gl_menu_btn;
extern char *gl_lastest_game;

extern char *gl_writing;
extern char *gl_time;
extern char *gl_Mon;
extern char *gl_Tues;
extern char *gl_Wed;
extern char *gl_Thur;
extern char *gl_Fri;
extern char *gl_Sat;
extern char *gl_Sun;

extern char *gl_addon;
extern char *gl_reset;
extern char *gl_rts;
extern char *gl_sleep;
extern char *gl_cheat;

extern char *gl_hot_key;
extern char *gl_hot_key2;

extern char *gl_language;

extern char *gl_set_btn;
extern char *gl_ok_btn;

extern char *gl_formatnor_info;

extern char *gl_check_sav;
extern char *gl_make_sav;

extern char *gl_check_RTS;
extern char *gl_make_RTS;

extern char *gl_check_pat;
extern char *gl_make_pat;

extern char *gl_loading_game;
extern char *gl_engine;
extern char *gl_use_engine;

extern char *gl_recently_play;

extern char *gl_START_help;
extern char *gl_SELECT_help;
extern char *gl_L_A_help;
extern char *gl_LSTART_help;
extern char *gl_online_manual;

extern char *gl_no_game_played;
extern char *gl_nor_title;

extern char *gl_cart_str;
extern char *gl_clock_str;
extern char *gl_rumble_str;

extern char *gl_logging;
extern char **gl_log_levels;  // TLOG_OFF to TLOG_DEBUG
extern char *gl_diagnose;
extern char *gl_diag_run;

extern char *gl_error_0;
extern char *gl_error_1;
extern char *gl_error_2;
extern char *gl_error_3;
extern char *gl_error_4;
extern char *gl_error_5;
extern char *gl_error_6;
extern char *gl_error_7;
extern char *gl_error_8;
extern char *gl_error_9;

extern char *gl_save_sav;
extern char *gl_save_ing;

extern char *gl_save;
extern char *gl_auto_save;

extern char *gl_modeB_INITstr;
extern char *gl_modeB_RUMBLE;
extern char *gl_modeB_RAM;
extern char *gl_modeB_LINK;

extern char *gl_led;
extern char *gl_led_open;

extern char *gl_Breathing_light;
extern char *gl_SD_working;

extern char *gl_NOR_full;
extern char *gl_save_loaded;
extern char *gl_save_saved;
extern char *gl_file_exist;
extern char *gl_file_noexist;

extern char **gl_rom_menu;
extern char **gl_nor_op;

extern u32 gl_lang; // index of the current language in lang_packs

void LoadEnglish(void);

// A language: the id SETTINGS stores, its name and what loads its strings
typedef struct {
	u16 id;
	const char *name;
	void (*load)(void);
} LANG_PACK;

extern const LANG_PACK lang_packs[];
extern const u32 lang_pack_count;

#endif
