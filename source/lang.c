#include "lang.h"
#include "settings_model.h"
#include "screens/info.h"

#include "userdata.h"

char *gl_init_error;
char *gl_power_off;
char *gl_Loading;
char *gl_library_title;
char *gl_library_scanning;
char *gl_library_found;
char *gl_library_saving;
char *gl_file_overflow;
char *gl_writing;
char *gl_loading_game;
char *gl_list_failed;

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

char *gl_NOR_full;
char *gl_saving EWRAM_BSS;
char *gl_format_title EWRAM_BSS;
char *gl_erasing EWRAM_BSS;
char *gl_done EWRAM_BSS;
char *gl_patching EWRAM_BSS;
char *gl_keep_power EWRAM_BSS;

// The menu's items, what they hold, and the hint bar's labels (screens/nav.c)
char *gl_item_recent EWRAM_BSS;
char *gl_item_fav EWRAM_BSS;
char *gl_item_gba EWRAM_BSS;
char *gl_item_gbc EWRAM_BSS;
char *gl_item_gb EWRAM_BSS;
char *gl_item_nes EWRAM_BSS;
char *gl_item_sd EWRAM_BSS;
char *gl_item_nor EWRAM_BSS;
char *gl_item_settings EWRAM_BSS;
char *gl_item_info EWRAM_BSS;
char *gl_count_game EWRAM_BSS;
char *gl_count_games EWRAM_BSS;
char *gl_count_file EWRAM_BSS;
char *gl_count_files EWRAM_BSS;
char *gl_count_page EWRAM_BSS;
char *gl_page_empty EWRAM_BSS;
char *gl_hint_open EWRAM_BSS;
char *gl_hint_move EWRAM_BSS;
char *gl_hint_back EWRAM_BSS;
char *gl_hint_ok EWRAM_BSS;
char *gl_hint_cancel EWRAM_BSS;

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

// Popups and confirmations (ui.h)
char *gl_pop_choose EWRAM_BSS;
char *gl_pop_ok EWRAM_BSS;
char *gl_pop_close EWRAM_BSS;
char *gl_pop_cancel EWRAM_BSS;
char *gl_pop_cancel_button EWRAM_BSS;

// The Info page (screens/info.h)
char **gl_info EWRAM_BSS;

//英文
const char en_init_error[] = "microSD card error";
const char en_power_off[] = "Turn the power off";
const char en_Loading[] = "Loading...";
// The library's first scan (library_scan.c): title, text, and after a count
const char en_library_title[] = "Library";
const char en_library_scanning[] = "Looking for games on the card";
const char en_library_found[] = "games found";
// ... and its write before a launch, when the menu hadn't rested long enough
const char en_library_saving[] = "Saving the game list";
const char en_file_overflow[] = "Too big to start from the SD card (over 32 MB). Write it to NOR instead.";
const char en_writing[] = "Writing to NOR";
const char en_lang[] = "ENGLISH";
const char en_loading_game[] = "Loading the game";
// A screen that didn't fit (screens/nav.c), which comes in again after this
const char en_list_failed[] = "Can't show the list. It is tried again once this closes.";

// How long ago a game was played (userdata_ago): whole texts, or the
// words after a number
const char en_ago_now[] = "just now";
const char en_ago_minutes[] = "m ago";
const char en_ago_hours[] = "h ago";
const char en_ago_yesterday[] = "yesterday";
const char en_ago_days[] = " days";
const char en_ago_week[] = "1 week";
const char en_ago_weeks[] = " weeks";
const char en_ago_month[] = "1 month";
const char en_ago_months[] = " months";
const char en_ago_year[] = "1 year";
const char en_ago_years[] = " years";
struct userdata_ago_words gl_ago EWRAM_BSS;

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

const char en_NOR_full[] = "Not enough room in NOR";
const char en_saving[] = "Saving to SD card";
const char en_format_title[] = "Erasing NOR";
const char en_erasing[] = "Erasing";
const char en_done[] = "Done";
const char en_patching[] = "Patching the game";
const char en_keep_power[] = "Don't turn the power off.";

// The menu's items (screens/registry.h), in the carousel's order; what an
// item holds (a number, then the word: "1,204 files"); the empty frame a
// page shows until its ticket lands; and the hint bar's labels, after a key
// chip ("A open")
const char en_item_recent[] = "Recent";
const char en_item_fav[] = "Favourites";
const char en_item_gba[] = "Game Boy Advance";
const char en_item_gbc[] = "Game Boy Color";
const char en_item_gb[] = "Game Boy";
const char en_item_nes[] = "NES";
const char en_item_sd[] = "SD card";
const char en_item_nor[] = "NOR";
const char en_item_settings[] = "Settings";
const char en_item_info[] = "Info";
const char en_count_game[] = "1 game";
const char en_count_games[] = "games";
const char en_count_file[] = "1 file";
const char en_count_files[] = "files";
const char en_count_page[] = "one page";
const char en_page_empty[] = "Coming in a later build";
const char en_hint_open[] = "open";
const char en_hint_move[] = "move";
const char en_hint_back[] = "back";
const char en_hint_ok[] = "OK";
const char en_hint_cancel[] = "cancel";

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
	[SET_IDENTIFY] = "Identify library",
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
	[SET_IDENTIFY] = "Looks up each game in names.db.",
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

// Popups and confirmations: the hint bar's labels, and the Cancel button
const char en_pop_choose[] = "choose";
const char en_pop_ok[] = "ok";
const char en_pop_close[] = "close";
const char en_pop_cancel[] = "cancel";
const char en_pop_cancel_button[] = "Cancel";

// The Info page (screens/info.h): its rows and what each holds, and the
// reading pages. A reading page's lines: "# " starts a heading, a tab puts
// a key chip before its text (ui/ui_reading.h); "%s" is what the page
// fills in, in order. Keys as the hint bar shows them: "^v" UP/DOWN, "«»"
// LEFT/RIGHT.
#define LR "\xC2\xAB\xC2\xBB"  // «»
const char *en_info[INFO_STRINGS] = {
	[INFO_S_CONTROLS] = "Controls",
	[INFO_S_INGAME] = "In game",
	[INFO_S_CART] = "Your cart",
	[INFO_S_ABOUT] = "About",
	[INFO_S_DIAG] = "Diagnostics",
	[INFO_S_CONTROLS_DETAIL] = "buttons, page by page",
	[INFO_S_INGAME_DETAIL] = "hotkeys while playing",
	[INFO_S_CART_DETAIL] = "Omega DE, %s MB NOR",
	[INFO_S_ABOUT_DETAIL] = "Torii v%s",
	[INFO_S_DIAG_DETAIL] = "run the checks",
	[INFO_S_SCROLL] = "scroll",
	[INFO_S_CONTROLS_TEXT] =
		"# Home\n" LR "\tturn the carousel\n"
		"A\topen the item\n"
		"# Every page\n"
		"B\tback, towards the carousel\n"
		"L R\tpage up, page down\n"
		"# Game lists\n"
		"^v\tmove\n" LR "\tjump to the next or previous letter\n"
		"A\tplay the game\n"
		"START\tthe game's menu\n"
		"L\thold it as you press A: the game starts with the GBA's boot screen\n"
		"# SD card\n"
		"A\topen the folder, or start the game\n"
		"START\tthe file's menu\n"
		"B\tup a folder; at the top, back to the carousel\n"
		"# NOR\n"
		"A\tstart the game\n"
		"START\tthe game's menu\n"
		"# Settings\n"
		"A\tchange the value: " LR " picks, A keeps it, B puts it back\n"
		"Time and date: A, then " LR " picks a field and ^v changes it.\n"
		"Hotkeys: A, then hold the new keys together.\n"
		"# Menus\n"
		"^v\tchoose\n"
		"A\tpick\n"
		"B\tclose\n"
		"In a question, " LR " chooses and B cancels.\n"
		"# At power-on\n"
		"Hold L, R and SELECT as you switch on to run Diagnostics, even when the menu won't start.",
	[INFO_S_INGAME_START] = "# Hotkeys\n"
				"They work in GBA games started with their addons on (Boot with addon). Settings, "
				"Hotkeys sets them; Save states and Cheats are in Settings, Games. A game on the NOR "
				"keeps the hotkeys it was written with.\n",
	[INFO_S_INGAME_STATES] = "# Save states\n"
				 "Hold %s to save the game's state, and %s to load it. Save before you load: with "
				 "no state saved, a game can crash.\n",
	[INFO_S_INGAME_MENU] = "# The in-game menu\n"
			       "Hold %s to open it: back to the menu, save or load a state, and the game's cheats.\n",
	[INFO_S_INGAME_RESET] = "# Back to the menu\n"
				"Hold %s to leave the game for the menu.\n",
	[INFO_S_INGAME_RESET_OFF] = "# Back to the menu\n"
				    "Off: turn Reset hotkey on in Settings, or switch the GBA off and on.\n",
	[INFO_S_INGAME_CHEATS] = "In a game with a cheat file, %s still opens the in-game menu, with its cheats.\n",
	[INFO_S_INGAME_SLEEP] = "# Sleep\n"
				"Hold %s to put the game to sleep: the screen goes off. START and SELECT wake it.\n",
	[INFO_S_INGAME_SLEEP_OFF] = "# Sleep\n"
				    "Off: turn Sleep on in Settings.\n",
	[INFO_S_INGAME_END] = "# Saves\n"
			      "The game saves to the cart. The next time Torii starts, it copies the save to the card "
			      "(without asking with Auto save on).\n",
	[INFO_S_CART_START] = "# The cart\n"
			      "An EZ-Flash Omega DE. Games start from the SD card, or from its %s MB NOR, where "
			      "Write to NOR puts them.\n",
	[INFO_S_CART_GAMES] = "# Games on the card\n",
	[INFO_S_CART_CONSOLE] = "%s: %s\n",
	[INFO_S_CART_FILES] = "%s on the card, games or not\n",
	[INFO_S_CART_COUNTING] = "Still counting them: open this page again in a moment.\n",
	[INFO_S_CART_NOR] = "# On the NOR\n"
			    "%s, %s MB of %s MB used\n",
	[INFO_S_CART_NOR_EMPTY] = "# On the NOR\n"
				  "No games. Write to NOR in a game's menu copies one there.\n",
	[INFO_S_CART_LISTS] = "# Your lists\n"
			      "Favourites: %s\n"
			      "Recent: %s, of the last %s played\n",
	[INFO_S_CART_END] = "# Torii's folders\n"
			    "/torii keeps the game list, your lists, names and logs, and /SAVER the saves.\n",
	[INFO_S_ABOUT_TORII] = "# Torii\n"
			       "Version %s, build %s\n"
			       "A custom kernel for the EZ-Flash Omega DE, built on EZ-Flash's open source kernel "
			       "1.06. Not made by EZ-Flash.\n",
	[INFO_S_ABOUT_CART] = "# The cart\n"
			      "%s, FPGA firmware %s\n",
	[INFO_S_ABOUT_REV_A] = "Revision A (LX9)",
	[INFO_S_ABOUT_REV_B] = "Revision B (LX16)",
	[INFO_S_ABOUT_CARD] = "# The SD card\n"
			      "%s, %s free, %s\n",
	[INFO_S_ABOUT_CARD_NO_FREE] = "# The SD card\n"
				      "%s, %s; its free space isn't recorded on the card.\n",
	[INFO_S_ABOUT_CARD_UNKNOWN] = "# The SD card\n"
				      "Its size didn't read.\n",
	[INFO_S_ABOUT_CREDITS] = "# Credits\n"
				 "Fonts: Galmuri by Lee Minseo, under the SIL Open Font License 1.1.\n"
				 "Console pictures: drawn on KyleBing's retro game console icons, CC BY 4.0.\n",
	[INFO_S_ABOUT_NAMES] = "Game names: No-Intro's, from libretro-database, CC BY-SA 4.0.\n",
};
#undef LR

//---------------------------------------------------------------------------------
void LoadEnglish(void)
{
	gl_init_error = (char *)en_init_error;
	gl_power_off = (char *)en_power_off;
	gl_Loading = (char *)en_Loading;
	gl_library_title = (char *)en_library_title;
	gl_library_scanning = (char *)en_library_scanning;
	gl_library_found = (char *)en_library_found;
	gl_library_saving = (char *)en_library_saving;
	gl_file_overflow = (char *)en_file_overflow;

	gl_writing = (char *)en_writing;

	gl_loading_game = (char *)en_loading_game;

	gl_list_failed = (char *)en_list_failed;

	gl_ago = (struct userdata_ago_words){en_ago_now,    en_ago_minutes, en_ago_hours, en_ago_yesterday,
					     en_ago_days,   en_ago_week,    en_ago_weeks, en_ago_month,
					     en_ago_months, en_ago_year,    en_ago_years};

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

	gl_NOR_full = (char *)en_NOR_full;
	gl_saving = (char *)en_saving;
	gl_format_title = (char *)en_format_title;
	gl_erasing = (char *)en_erasing;
	gl_done = (char *)en_done;
	gl_patching = (char *)en_patching;
	gl_keep_power = (char *)en_keep_power;
	//
	gl_item_recent = (char *)en_item_recent;
	gl_item_fav = (char *)en_item_fav;
	gl_item_gba = (char *)en_item_gba;
	gl_item_gbc = (char *)en_item_gbc;
	gl_item_gb = (char *)en_item_gb;
	gl_item_nes = (char *)en_item_nes;
	gl_item_sd = (char *)en_item_sd;
	gl_item_nor = (char *)en_item_nor;
	gl_item_settings = (char *)en_item_settings;
	gl_item_info = (char *)en_item_info;
	gl_count_game = (char *)en_count_game;
	gl_count_games = (char *)en_count_games;
	gl_count_file = (char *)en_count_file;
	gl_count_files = (char *)en_count_files;
	gl_count_page = (char *)en_count_page;
	gl_page_empty = (char *)en_page_empty;
	gl_hint_open = (char *)en_hint_open;
	gl_hint_move = (char *)en_hint_move;
	gl_hint_back = (char *)en_hint_back;
	gl_hint_ok = (char *)en_hint_ok;
	gl_hint_cancel = (char *)en_hint_cancel;
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
	gl_pop_choose = (char *)en_pop_choose;
	gl_pop_ok = (char *)en_pop_ok;
	gl_pop_close = (char *)en_pop_close;
	gl_pop_cancel = (char *)en_pop_cancel;
	gl_pop_cancel_button = (char *)en_pop_cancel_button;
	//
	gl_info = (char **)en_info;
}
//---------------------------------------------------------------------------------
// The languages Settings offers. A new language adds its strings and a
// Load function above, and a row here with an id no other row uses.
const LANG_PACK lang_packs[] = {
	{0xE1E1, en_lang, LoadEnglish},
};
const u32 lang_pack_count = sizeof(lang_packs) / sizeof(lang_packs[0]);
