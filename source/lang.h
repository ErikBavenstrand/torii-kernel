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
#define EN_SAVE_MISMATCH "The save in the cart doesn't look like %.*s. Write it over that file anyway?"
#define EN_SAVE_TYPE_CHANGED \
	"The save type of %.*s changed. Its old save (%lu bytes) is kept in History, and the game starts with a new " \
	"one."
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
// a number and "games"), and the hint bar's labels
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
extern char *gl_hint_open;
extern char *gl_hint_turn;
extern char *gl_hint_move;
extern char *gl_hint_back;
extern char *gl_hint_ok;
extern char *gl_hint_cancel;

// The v0.4 Settings page (settings_model.h): by enum settings_section, by
// row (explanations to SET_EXPLAINS, with its extra ones), and each SET_CHOICE's options by value
extern char **gl_set_sections;
extern char **gl_set_labels;
extern char **gl_set_explain;
extern char **gl_set_onoff;
extern char **gl_set_themes;
extern char **gl_set_views;
extern char **gl_set_boots;
extern char **gl_set_locations;
extern char **gl_set_history;
extern char **gl_set_starts;
extern char **gl_set_modes_b;
extern char **gl_set_colours;
extern char **gl_set_log_levels;
extern char **gl_set_ampm;
extern char *gl_set_minutes;  // the unit after DIM's minutes
// The page itself (screens/settings_page.c): an action's value while it
// runs, the hotkey capture's texts, why a combination was refused (by
// settings_set_keys' answer; SET_KEYS_TAKEN's is followed by the other
// row's label), and the hint bar's labels while a value is edited
extern char *gl_set_scanning;
extern char *gl_set_saving;
extern char *gl_set_save_failed;  // the hint bar when a settings write failed
extern char *gl_set_press;
extern char *gl_set_press_hint;
extern char **gl_set_refused;
extern char *gl_hint_change;
extern char *gl_hint_done;
extern char *gl_hint_field;
extern char *gl_hint_set;

// Popups and confirmations (ui.h): the hint bar's labels while one is up,
// and the confirmation's Cancel button
extern char *gl_pop_choose;
extern char *gl_pop_ok;
extern char *gl_pop_close;
extern char *gl_pop_cancel;
extern char *gl_pop_cancel_button;

// The Info page (screens/info.h): by enum info_string
extern char **gl_info;

// The NOR page (screens/nor.h): sizes' units ("16 MB"), the storage line
// ("40 MB used", "24 MB free of 64"), its rows, hints, popup and questions,
// and Game info's save types (enum nor_save)
extern char *gl_size_mb;
extern char *gl_size_kb;
extern char *gl_nor_used;
extern char *gl_nor_free_of;
extern char *gl_nor_empty;
extern char *gl_nor_format;
extern char *gl_nor_format_detail;
extern char *gl_hint_launch;
extern char *gl_hint_menu;
extern char *gl_hint_format;
extern char *gl_nor_launch;
extern char *gl_nor_info;
extern char *gl_nor_delete;
extern char *gl_nor_delete_text;
extern char *gl_nor_deleting;
extern char *gl_nor_latest_only;
extern char *gl_nor_format_ask;
extern char *gl_nor_format_one;
extern char *gl_nor_format_many;
extern char *gl_nor_format_time;
extern char *gl_nor_format_takes;
extern char *gl_nor_format_button;
extern char *gl_nor_states;
extern char *gl_nor_changed;
enum nor_save {
	NOR_SAVE_NONE,
	NOR_SAVE_SRAM,
	NOR_SAVE_EEPROM_512,
	NOR_SAVE_EEPROM_8K,
	NOR_SAVE_FLASH_64K,
	NOR_SAVE_FLASH_128K,
	NOR_SAVES,
};
extern char **gl_nor_saves;

// The SD card page (screens/sdcard.c): gl_sd_first is followed by a number
// and "files" or gl_sd_folders, gl_sd_delete_ask by a name and "?", and
// gl_sd_kind_game follows a console's name
extern char *gl_sd_item;
extern char *gl_sd_items;
extern char *gl_sd_items_none;
extern char *gl_sd_first;
extern char *gl_sd_empty;
extern char *gl_sd_open;
extern char *gl_sd_launch;
extern char *gl_sd_clean;
extern char *gl_sd_folders;
extern char *gl_sd_nor;
extern char *gl_sd_nor_addon;
extern char *gl_sd_fav_add;
extern char *gl_sd_fav_remove;
extern char *gl_sd_delete;
extern char *gl_sd_info;
extern char *gl_sd_delete_ask;
extern char *gl_sd_delete_save;
extern char *gl_sd_delete_failed;
extern char *gl_sd_fav_failed;
extern char *gl_sd_kind_game;
extern char *gl_sd_kind_file;
extern char *gl_sd_kind_folder;
extern char *gl_sd_bytes;
extern char *gl_sd_changed;
extern char *gl_sd_save;
extern char *gl_sd_no_save;

// The game lists (screens/games.c), their popup and Game info card, and the
// cheat screen (showcht.c)
enum {
	LANG_POP_PLAY,
	LANG_POP_FAV,
	LANG_POP_CLEAN,
	LANG_POP_ADDON,
	LANG_POP_RENAME,
	LANG_POP_INFO,
	LANG_POP_DELETE,
	LANG_POP_ITEMS,
};
enum {
	LANG_INFO_CONSOLE,
	LANG_INFO_SIZE,
	LANG_INFO_FOLDER,
	LANG_INFO_PLAYED,
	LANG_INFO_SAVE_TYPE,
	LANG_INFO_BOOT_MODE,
	LANG_INFO_HARD_RESET,
	LANG_INFO_KEYS,
};
struct lang_games {
	const char *empty;  // a list with no game (left)
	const char *hint_play, *hint_menu, *hint_letter, *hint_pick;
	const char *pop[LANG_POP_ITEMS];  // the game popup's items
	const char *off, *on;  // Favourite's value
	const char *delete_text;  // under the Delete confirmation's "Delete <name>?" (EN_GAME_DELETE)
	const char *delete_button;
	const char *save_type_failed;  // Game info's save type couldn't be written
	const char *boot_mode_failed;  // nor its boot mode
	const char *info_keys[LANG_INFO_KEYS];  // Game info's lines
	const char *consoles[4];  // by LIB_GBA...: Game info's Console
	const char *nor;  // a game NOR game's folder
	const char *hard_reset;  // Game info's Hard reset line: how
	const char *save_types[6];  // by save type (actions.h: 0 AUTO ...)
	const char *boot_modes[3];  // by a game's boot mode (actions.h: ACTION_BOOT_*)
	const char *size_kb, *size_mb;
	const char *fact_size, *fact_played;  // list with art: the facts under the box
};
extern const struct lang_games *gl_games;
#define EN_GAME_DELETE "Delete %s?"

// The rename keyboard (screens/rename.c) and what a rename says
struct lang_rename {
	const char *title;  // its title bar
	const char *hint_type, *hint_delete, *hint_case, *hint_ok;  // A, B, SELECT, START
	const char *key_lower, *key_upper, *key_latin, *key_space, *key_cancel, *key_ok;  // the function row
	const char *empty, *taken, *bad, *hidden, *pending, *shared, *failed;  // OK refused
	const char *side_taken[7];  // a file in the way, by actions.h's ACTION_FILE_*
	const char *partly;       // renamed, but not everything followed (a dialog after)
	const char *name_failed;  // a game's name couldn't be written (a dialog after)
	const char *names_full;   // ... as names.txt is too large or holds too many names
	const char *discard, *discard_button;  // Cancel with changes: the confirmation
	const char *checking, *checking_text;  // the progress dialog while the card is checked for a shared save
};
extern const struct lang_rename *gl_rename;
// A key past the cap says so, with the cap
#define EN_RENAME_TOO_LONG "Names stop at %u letters."

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
