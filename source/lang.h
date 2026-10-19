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
extern char *gl_close_btn;  // a message dialog's key (source/ui/dialog.c)
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
extern char *gl_save_key;  // SLEEP KEY and MENU KEY when SAVESTATE is the only addon
extern char *gl_load_key;

extern char *gl_settings_title;
extern char *gl_help_title;
extern char *gl_on;
extern char *gl_off;
extern char *gl_theme;
extern char **gl_theme_names;  // dark, light
extern char *gl_accent;
extern char *gl_accent_own;  // the theme's own accent
extern char *gl_dim;
extern char *gl_minutes;

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
extern char *gl_L_A_help;
extern char *gl_LSTART_help;
extern char *gl_online_manual;

extern char *gl_no_game_played;
extern char *gl_list_failed;
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
extern char *gl_delete_ask;    // the L+START question's title, over the file's name
extern char *gl_saving;        // the progress title while a save goes to the card
extern char *gl_format_title;  // FORMAT ALL's progress title
extern char *gl_erasing;       // its status, with the time so far
extern char *gl_done;          // a progress dialog's last status, at 100%
extern char *gl_patching;      // a launch's step after loading, with the addons on
extern char *gl_keep_power;    // FORMAT ALL's text, under its title

// A launch's question while a save waits to be copied (ezkernelnew.c): a
// format, kept a literal so the compiler checks it (the save's file, cut
// to 80 bytes: the dialog shows 4 lines)
#define EN_SAVE_UNSAVED "The save of %.80s isn't on the card yet. Launching loses it. Launch anyway?"

// The firmware update's texts (Ezcard_OP.c). It runs before a language is
// loaded, and the formats stay literals so the compiler checks them.
#define EN_FW_TITLE "Rev.%s firmware update"
#define EN_FW_CRC_ERROR "The firmware in the kernel is damaged (CRC32).\nPress [B] to go back."
#define EN_FW_QUESTION "Installed: V%02d\nNew: V%02d\nPress [A] to update\nPress [B] to cancel"
#define EN_FW_UPDATING "Updating V%02d to V%02d.\nDon't turn the power off."
#define EN_FW_PROGRESS "Updating... %lu%%"
#define EN_FW_DONE "Done. Turn the power off."

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
