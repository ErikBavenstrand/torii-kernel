#ifndef _LANG_H
#define _LANG_H

#include <gba_base.h>

extern char *gl_init_error;
extern char *gl_power_off;
extern char *gl_Loading;
extern char *gl_library_title;
extern char *gl_library_scanning;
extern char *gl_library_found;
extern char *gl_library_saving;
extern char *gl_file_overflow;
extern char *gl_writing;
extern char *gl_loading_game;

// "Played ago" words (userdata.h)
extern struct userdata_ago_words gl_ago;
extern char *gl_list_failed;

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

extern char *gl_NOR_full;
extern char *gl_saving;        // the progress title while a save goes to the card
extern char *gl_format_title;  // FORMAT ALL's progress title
extern char *gl_erasing;       // its status, with the time so far
extern char *gl_done;          // a progress dialog's last status, at 100%
extern char *gl_patching;      // a launch's step after loading, with the addons on
extern char *gl_keep_power;    // FORMAT ALL's text, under its title

// The question while a save waits to be copied (ezkernelnew.c), before a
// launch: a format, kept a literal so the compiler checks it (the save's
// file, cut to at most 80 bytes at a character's start: the dialog shows 4
// lines)
#define EN_SAVE_UNSAVED "The save of %.*s isn't on the card yet. Launching loses it. Launch anyway?"

// The firmware update's texts (Ezcard_OP.c). It runs before a language is
// loaded, and the formats stay literals so the compiler checks them.
#define EN_FW_TITLE "Rev.%s firmware update"
#define EN_FW_CRC_ERROR "The firmware in the kernel is damaged (CRC32).\nPress [B] to go back."
#define EN_FW_QUESTION "Installed: V%02d\nNew: V%02d\nPress [A] to update\nPress [B] to cancel"
#define EN_FW_UPDATING "Updating V%02d to V%02d.\nDon't turn the power off."
#define EN_FW_PROGRESS "Updating... %lu%%"
#define EN_FW_DONE "Done. Turn the power off."

// The menu's items (screens/registry.h) and what they hold ("1 game", then
// a number and "games"), a page's empty frame, and the hint bar's labels
extern char *gl_item_recent;
extern char *gl_item_fav;
extern char *gl_item_gba;
extern char *gl_item_gbc;
extern char *gl_item_gb;
extern char *gl_item_nes;
extern char *gl_item_sd;
extern char *gl_item_nor;
extern char *gl_item_settings;
extern char *gl_item_info;
extern char *gl_count_game;
extern char *gl_count_games;
extern char *gl_count_file;
extern char *gl_count_files;
extern char *gl_count_page;
extern char *gl_page_empty;
extern char *gl_hint_open;
extern char *gl_hint_move;
extern char *gl_hint_back;
extern char *gl_hint_ok;
extern char *gl_hint_cancel;

// The v0.4 Settings page (settings_model.h): by enum settings_section, by
// row (and SET_LABELS' extra labels), and each SET_CHOICE's options by value
extern char **gl_set_sections;
extern char **gl_set_labels;
extern char **gl_set_explain;
extern char **gl_set_onoff;
extern char **gl_set_themes;
extern char **gl_set_views;
extern char **gl_set_starts;
extern char **gl_set_modes_b;
extern char **gl_set_colours;
extern char **gl_set_log_levels;
extern char **gl_set_ampm;
extern char *gl_set_minutes;  // the unit after DIM's minutes

// Popups and confirmations (ui.h): the hint bar's labels while one is up,
// and the confirmation's Cancel button
extern char *gl_pop_choose;
extern char *gl_pop_ok;
extern char *gl_pop_close;
extern char *gl_pop_cancel;
extern char *gl_pop_cancel_button;

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
