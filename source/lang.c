#include "lang.h"
#include "settings_model.h"

char *gl_init_error;
char *gl_power_off;
char *gl_Loading;
char *gl_file_overflow;

char *gl_menu_btn;
char *gl_close_btn;
char *gl_lastest_game;

char *gl_writing;

char *gl_time;
char *gl_Mon;
char *gl_Tues;
char *gl_Wed;
char *gl_Thur;
char *gl_Fri;
char *gl_Sat;
char *gl_Sun;

char *gl_addon;
char *gl_reset;
char *gl_rts;
char *gl_sleep;
char *gl_cheat;

char *gl_hot_key;
char *gl_hot_key2;
char *gl_save_key EWRAM_BSS;
char *gl_load_key EWRAM_BSS;

char *gl_settings_title EWRAM_BSS;
char *gl_help_title EWRAM_BSS;
char *gl_on EWRAM_BSS;
char *gl_off EWRAM_BSS;
char *gl_theme EWRAM_BSS;
char **gl_theme_names EWRAM_BSS;
char *gl_dim EWRAM_BSS;
char *gl_minutes EWRAM_BSS;

char *gl_language;
char *gl_set_btn;
char *gl_ok_btn;

char *gl_formatnor_info;

char *gl_loading_game;

char *gl_engine;
char *gl_use_engine;

char *gl_recently_play;

char *gl_START_help;
char *gl_L_A_help;
char *gl_LSTART_help;
char *gl_online_manual;

char *gl_no_game_played;
char *gl_list_failed;
char *gl_nor_title;

char *gl_cart_str;
char *gl_clock_str;
//char* gl_offRTC_powersave;
char *gl_rumble_str;

char *gl_logging;
char **gl_log_levels;
char *gl_diagnose;
char *gl_diag_run;

char *gl_error_0;
char *gl_error_1;
char *gl_error_2;
char *gl_error_3;
char *gl_error_4;
char *gl_error_5;
char *gl_error_6;
char *gl_error_7;
char *gl_error_8;
char *gl_error_9;

char *gl_save_sav;
char *gl_save_ing;

char *gl_save;
char *gl_auto_save;

char *gl_modeB_INITstr;
char *gl_modeB_RUMBLE;
char *gl_modeB_RAM;
char *gl_modeB_LINK;

char *gl_led;
char *gl_led_open;

char *gl_Breathing_light;
char *gl_SD_working;

char *gl_NOR_full;
char *gl_save_loaded;
char *gl_save_saved;
char *gl_file_exist;
char *gl_file_noexist;
char *gl_delete_ask EWRAM_BSS;
char *gl_nor_delete_ask EWRAM_BSS;
char *gl_no_game_nor EWRAM_BSS;
char *gl_saving EWRAM_BSS;
char *gl_format_title EWRAM_BSS;
char *gl_erasing EWRAM_BSS;
char *gl_done EWRAM_BSS;
char *gl_patching EWRAM_BSS;
char *gl_keep_power EWRAM_BSS;
//--
char **gl_rom_menu;
char **gl_nor_op;

// The v0.4 Settings page (settings_model.h)
char **gl_set_sections EWRAM_BSS;
char **gl_set_labels EWRAM_BSS;
char **gl_set_explain EWRAM_BSS;
char **gl_set_onoff EWRAM_BSS;
char **gl_set_themes EWRAM_BSS;
char **gl_set_views EWRAM_BSS;
char **gl_set_starts EWRAM_BSS;
char **gl_set_modes_b EWRAM_BSS;
char **gl_set_colours EWRAM_BSS;
char **gl_set_log_levels EWRAM_BSS;
char **gl_set_ampm EWRAM_BSS;
char *gl_set_minutes EWRAM_BSS;

//英文
const char en_init_error[] = "microSD card error";
const char en_power_off[] = "Turn the power off";
const char en_Loading[] = "Loading...";
const char en_file_overflow[] = "Too big to start from the SD card (over 32 MB). Write it to NOR instead.";

const char en_menu_btn[] = "[B]CANCEL    [A]OK";
const char en_close_btn[] = "[A]OK";
const char en_writing[] = "Writing to NOR";
const char en_lastest_game[] = "Only the last game in NOR can be deleted";

const char en_time[] = "     TIME";
const char en_Mon[] = "MON";
const char en_Tues[] = "TUE";
const char en_Wed[] = "WED";
const char en_Thur[] = "THU";
const char en_Fri[] = "FRI";
const char en_Sat[] = "SAT";
const char en_Sun[] = "SUN";

const char en_addon[] = "    ADDON";
const char en_reset[] = "RESET";
const char en_rts[] = "SAVESTATE";
const char en_sleep[] = "SLEEP";
const char en_cheat[] = "CHEAT";

const char en_hot_key[] = "SLEEP KEY";
const char en_hot_key2[] = " MENU KEY";

const char en_language[] = " LANGUAGE";
const char en_lang[] = "ENGLISH";
const char en_set_btn[] = "SET";
const char en_ok_btn[] = " OK";

const char en_formatnor_info[] = "Erase every game in NOR? It takes about 4 minutes.";

const char en_loading_game[] = "Loading the game";

const char en_engine[] = "   ENGINE";
const char en_use_engine[] = "FAST PATCH ENGINE";

const char en_recently_play[] = "RECENTLY PLAYED";

const char en_START_help[] = "Open recently played list";
const char en_L_A_help[] = "Multiboot";
const char en_LSTART_help[] = "Delete file";
const char en_online_manual[] = "Online manual";

const char en_no_game_played[] = "No game played yet";
const char en_list_failed[] = "Can't show the list. It is tried again once this closes.";
const char en_nor_title[] = "NOR";  // the NOR list's title bar

const char en_cart[] = "     CART";
const char en_clock[] = "CLOCK";
const char en_rumble[] = "RUMBLE";

const char en_logging[] = "  LOGGING";
const char *en_log_levels[4] = {"OFF", "ERRORS", "INFO", "DEBUG"};
const char en_diagnose[] = " DIAGNOSE";
const char en_diag_run[] = "RUN TESTS AND PROBES";

const char en_save_key[] = "SAVE KEY";
const char en_load_key[] = "LOAD KEY";
const char en_settings_title[] = "SETTINGS";
const char en_help_title[] = "HELP";
const char en_on[] = "ON";
const char en_off[] = "OFF";
const char en_theme[] = "THEME";
const char *en_theme_names[2] = {"DARK", "LIGHT"};
const char en_dim[] = "DIM";
const char en_minutes[] = "MIN";

const char en_error_0[] = "Can't open the folder";
const char en_error_1[] = "Can't open the file";
const char en_error_2[] = "Can't open the SAVER folder";
const char en_error_3[] = "Can't write the save";
const char en_error_4[] = "Can't read the save";
const char en_error_5[] = "Can't create the save file";
const char en_error_6[] = "Can't create the save state file";
const char en_error_7[] = "The file is too fragmented. Copy it to the card again.";
const char en_error_8[] = "The file name is too long";
const char en_error_9[] = "The folder path is too long";

const char en_save_sav[] = "Save to SD card?";
const char en_save_ing[] = "Saving";
const char en_save[] = "     SAVE";
const char en_auto_save[] = "AUTO SAVE";

const char en_modeB_INITstr[] = "   MODE B";
const char en_modeB_RUMBLE[] = "RUMBLE";
const char en_modeB_RAM[] = "RAM";
const char en_modeB_LINK[] = "LINK";

const char en_led[] = "      LED";
const char en_led_open[] = "ENABLE LED";
const char en_Breathing_light[] = "BREATHING";
const char en_SD_working[] = "   SD LED";

const char en_NOR_full[] = "Not enough room in NOR";
const char en_save_loaded[] = "Save loaded";
const char en_save_saved[] = "Save written to the SD card";
const char en_file_exist[] = "There is a save file already. Replace it?";
const char en_file_noexist[] = "No save file found";
const char en_delete_ask[] = "Delete this file?";
const char en_nor_delete_ask[] = "Delete this game from NOR?";
const char en_no_game_nor[] = "No games in NOR";
const char en_saving[] = "Saving to SD card";
const char en_format_title[] = "Erasing NOR";
const char en_erasing[] = "Erasing";
const char en_done[] = "Done";
const char en_patching[] = "Patching the game";
const char en_keep_power[] = "Don't turn the power off.";

const char *en_rom_menu[] = {
	"CLEAN BOOT", "BOOT WITH ADDON", "WRITE TO NOR CLEAN", "WRITE TO NOR ADDON", "SAVE TYPE", "CHEAT",
};
const char *en_nor_op[5] = {
	"DIRECT BOOT", "DELETE", "FORMAT ALL", "LOAD SAVE", "BACK UP SAVE",
};

// The v0.4 Settings page: section names, then each row's label and its
// one-line explanation, which shows in the hint bar (at most 38 characters
// and 232 px of body text, tests/host/test_settings_model.c)
const char *en_set_sections[SET_SECTIONS] = {
	[SET_SEC_LOOK] = "Look",     [SET_SEC_CLOCK] = "Clock",     [SET_SEC_GAMES] = "Games",
	[SET_SEC_CART] = "Cart",     [SET_SEC_HOTKEYS] = "Hotkeys", [SET_SEC_LIBRARY] = "Library",
	[SET_SEC_SYSTEM] = "System",
};
const char *en_set_labels[SET_LABELS] = {
	[SET_THEME] = "Theme",
	[SET_GAME_VIEW] = "Game view",
	[SET_START_ON] = "Start on",
	[SET_DIM] = "Dim screen",
	[SET_TIME_ROW] = "Time",
	[SET_DATE_ROW] = "Date",
	[SET_CLOCK_24H] = "24-hour clock",
	[SET_GAME_CLOCK] = "Clock for games",
	[SET_ENGINE] = "Fast patching",
	[SET_AUTO_SAVE] = "Auto save",
	[SET_RTS] = "Save states",
	[SET_CHEATS] = "Cheats",
	[SET_RUMBLE] = "Rumble for games",
	[SET_MODE_B] = "Mode B",
	[SET_LED] = "LED",
	[SET_LED_IDLE] = "LED colour",
	[SET_LED_SD] = "SD card light",
	[SET_RESET_ON] = "Reset hotkey",
	[SET_RESET_KEYS] = "Reset keys",
	[SET_SLEEP_ON] = "Sleep",
	[SET_SLEEP_KEYS] = "Sleep keys",
	[SET_RESCAN] = "Rescan library",
	[SET_SHOW_ALL] = "Show all files",
	[SET_LOGGING] = "Logging",
	[SET_LANGUAGE] = "Language",
	[SET_LABEL_SAVE_KEYS] = "Save state keys",
	[SET_LABEL_LOAD_KEYS] = "Load state keys",
};
const char *en_set_explain[SET_LABELS] = {
	[SET_THEME] = "Light paper or dark graphite.",
	[SET_GAME_VIEW] = "Game lists: names alone, or box art.",
	[SET_START_ON] = "Where the menu opens at power-on.",
	[SET_DIM] = "Dims the screen after idle minutes.",
	[SET_TIME_ROW] = "The cart's clock. A sets it.",
	[SET_DATE_ROW] = "The cart's date. A sets it.",
	[SET_CLOCK_24H] = "14:32 when on, 2:32 PM when off.",
	[SET_GAME_CLOCK] = "Lets games read the cart's clock.",
	[SET_ENGINE] = "Patches from a built-in table first.",
	[SET_AUTO_SAVE] = "Copies saves to the card, no question.",
	[SET_RTS] = "Save and load a game's state in game.",
	[SET_CHEATS] = "Applies a game's cheat file.",
	[SET_RUMBLE] = "Lets games use the cart's rumble.",
	[SET_MODE_B] = "What the cart is in a DS (mode B).",
	[SET_LED] = "The cart's light.",
	[SET_LED_IDLE] = "The light's colour while idle.",
	[SET_LED_SD] = "The light while the SD card works.",
	[SET_RESET_ON] = "The keys leave a game for the menu.",
	[SET_RESET_KEYS] = "Held together in game. A sets them.",
	[SET_SLEEP_ON] = "The keys put a game to sleep.",
	[SET_SLEEP_KEYS] = "Held together in game. A sets them.",
	[SET_RESCAN] = "Looks for games on the card again.",
	[SET_SHOW_ALL] = "The SD card page shows hidden files.",
	[SET_LOGGING] = "Writes a log to /torii/logs.",
	[SET_LANGUAGE] = "The menu's language.",
	[SET_LABEL_SAVE_KEYS] = "Saves a state in game. A sets them.",
	[SET_LABEL_LOAD_KEYS] = "Loads the state in game. A sets them.",
};
const char *en_set_onoff[2] = {"Off", "On"};
const char *en_set_themes[2] = {"Dark", "Light"};
const char *en_set_views[2] = {"List", "List + art"};
const char *en_set_starts[SET_STARTS] = {
	[SET_START_LAST] = "Last place", [SET_START_RECENT] = "Recent",
	[SET_START_HOME] = "Home",       [SET_START_FAVOURITES] = "Favourites",
	[SET_START_SD] = "SD card",      [SET_START_GBA] = "GBA",
	[SET_START_GBC] = "GBC",         [SET_START_GB] = "GB",
	[SET_START_NES] = "NES",
};
const char *en_set_modes_b[3] = {"Rumble", "RAM", "Link"};
// By value: red 4, green 2, blue 1
const char *en_set_colours[8] = {"Off", "Blue", "Green", "Cyan", "Red", "Magenta", "Yellow", "White"};
const char *en_set_log_levels[4] = {"Off", "Errors", "Info", "Debug"};
const char *en_set_ampm[2] = {"AM", "PM"};
const char en_set_minutes[] = "min";

//---------------------------------------------------------------------------------
void LoadEnglish(void)
{
	gl_init_error = (char *)en_init_error;
	gl_power_off = (char *)en_power_off;
	gl_Loading = (char *)en_Loading;
	gl_file_overflow = (char *)en_file_overflow;

	gl_menu_btn = (char *)en_menu_btn;
	gl_close_btn = (char *)en_close_btn;
	gl_writing = (char *)en_writing;
	gl_lastest_game = (char *)en_lastest_game;

	gl_time = (char *)en_time;
	gl_Mon = (char *)en_Mon;
	gl_Tues = (char *)en_Tues;
	gl_Wed = (char *)en_Wed;
	gl_Thur = (char *)en_Thur;
	gl_Fri = (char *)en_Fri;
	gl_Sat = (char *)en_Sat;
	gl_Sun = (char *)en_Sun;
	gl_addon = (char *)en_addon;
	gl_reset = (char *)en_reset;
	gl_rts = (char *)en_rts;
	gl_sleep = (char *)en_sleep;
	gl_cheat = (char *)en_cheat;

	gl_hot_key = (char *)en_hot_key;
	gl_hot_key2 = (char *)en_hot_key2;

	gl_language = (char *)en_language;
	gl_set_btn = (char *)en_set_btn;
	gl_ok_btn = (char *)en_ok_btn;
	gl_formatnor_info = (char *)en_formatnor_info;

	gl_loading_game = (char *)en_loading_game;

	gl_engine = (char *)en_engine;
	gl_use_engine = (char *)en_use_engine;

	gl_recently_play = (char *)en_recently_play;

	gl_START_help = (char *)en_START_help;
	gl_L_A_help = (char *)en_L_A_help;
	gl_LSTART_help = (char *)en_LSTART_help;
	gl_online_manual = (char *)en_online_manual;

	gl_no_game_played = (char *)en_no_game_played;
	gl_list_failed = (char *)en_list_failed;

	gl_cart_str = (char *)en_cart;
	gl_clock_str = (char *)en_clock;
	//gl_offRTC_powersave = (char*)en_offRTC_powersave;
	gl_rumble_str = (char *)en_rumble;

	gl_logging = (char *)en_logging;
	gl_log_levels = (char **)en_log_levels;
	gl_diagnose = (char *)en_diagnose;
	gl_diag_run = (char *)en_diag_run;

	gl_save_key = (char *)en_save_key;
	gl_load_key = (char *)en_load_key;
	gl_settings_title = (char *)en_settings_title;
	gl_help_title = (char *)en_help_title;
	gl_on = (char *)en_on;
	gl_off = (char *)en_off;
	gl_theme = (char *)en_theme;
	gl_theme_names = (char **)en_theme_names;
	gl_dim = (char *)en_dim;
	gl_minutes = (char *)en_minutes;

	gl_error_0 = (char *)en_error_0;
	gl_error_1 = (char *)en_error_1;
	gl_error_2 = (char *)en_error_2;
	gl_error_3 = (char *)en_error_3;
	gl_error_4 = (char *)en_error_4;
	gl_error_5 = (char *)en_error_5;
	gl_error_6 = (char *)en_error_6;
	gl_error_7 = (char *)en_error_7;
	gl_error_8 = (char *)en_error_8;
	gl_error_9 = (char *)en_error_9;

	gl_save_sav = (char *)en_save_sav;
	gl_save_ing = (char *)en_save_ing;
	gl_save = (char *)en_save;
	gl_auto_save = (char *)en_auto_save;

	gl_modeB_INITstr = (char *)en_modeB_INITstr;
	gl_modeB_RUMBLE = (char *)en_modeB_RUMBLE;
	gl_modeB_RAM = (char *)en_modeB_RAM;
	gl_modeB_LINK = (char *)en_modeB_LINK;

	gl_led = (char *)en_led;
	gl_led_open = (char *)en_led_open;
	gl_Breathing_light = (char *)en_Breathing_light;
	gl_SD_working = (char *)en_SD_working;

	gl_NOR_full = (char *)en_NOR_full;
	gl_save_loaded = (char *)en_save_loaded;
	gl_save_saved = (char *)en_save_saved;
	gl_file_exist = (char *)en_file_exist;
	gl_file_noexist = (char *)en_file_noexist;
	gl_delete_ask = (char *)en_delete_ask;
	gl_nor_delete_ask = (char *)en_nor_delete_ask;
	gl_no_game_nor = (char *)en_no_game_nor;
	gl_saving = (char *)en_saving;
	gl_format_title = (char *)en_format_title;
	gl_erasing = (char *)en_erasing;
	gl_done = (char *)en_done;
	gl_patching = (char *)en_patching;
	gl_keep_power = (char *)en_keep_power;
	//
	gl_rom_menu = (char **)en_rom_menu;
	gl_nor_op = (char **)en_nor_op;
	//
	gl_set_sections = (char **)en_set_sections;
	gl_set_labels = (char **)en_set_labels;
	gl_set_explain = (char **)en_set_explain;
	gl_set_onoff = (char **)en_set_onoff;
	gl_set_themes = (char **)en_set_themes;
	gl_set_views = (char **)en_set_views;
	gl_set_starts = (char **)en_set_starts;
	gl_set_modes_b = (char **)en_set_modes_b;
	gl_set_colours = (char **)en_set_colours;
	gl_set_log_levels = (char **)en_set_log_levels;
	gl_set_ampm = (char **)en_set_ampm;
	gl_set_minutes = (char *)en_set_minutes;
	gl_nor_title = (char *)en_nor_title;
}
//---------------------------------------------------------------------------------
// The languages offered in SETTINGS. A new language adds its strings and a
// Load function above, and a row here with an id no other row uses.
const LANG_PACK lang_packs[] = {
	{0xE1E1, en_lang, LoadEnglish},
};
const u32 lang_pack_count = sizeof(lang_packs) / sizeof(lang_packs[0]);
