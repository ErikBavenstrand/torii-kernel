// Settings as data (v0.4): the sections and rows the Settings page draws,
// the values they hold in the kernel NOR's settings slots (ez_define.h), the
// clock's time and date editor and the 12/24-hour formatter. Hardware-free,
// so tests/host runs it; settings.h reads, stages and writes the values.
//
// A page loads the slots into a struct settings_values, changes them there
// with settings_step, settings_set and settings_set_keys, and stages and
// commits them once as it closes (settings.h). What reads the settings
// through Read_SET_info (the theme, as ui_theme_setting does) sees a change
// only once it is staged: a live theme preview stages, or sets the theme
// itself. Slots no row changed keep
// what they held, erased (0xFFFF) ones too, so a visit that changes nothing,
// or changes a value and back, writes nothing.
#ifndef TORII_SETTINGS_MODEL_H
#define TORII_SETTINGS_MODEL_H

#include <gba_base.h>

#include "ez_define.h"

enum settings_section {
	SET_SEC_LOOK,
	SET_SEC_CLOCK,
	SET_SEC_GAMES,
	SET_SEC_CART,
	SET_SEC_HOTKEYS,
	SET_SEC_LIBRARY,
	SET_SEC_SYSTEM,
	SET_SECTIONS
};

// How a row's value is shown and edited
enum settings_kind {
	SET_CHOICE,  // one of options, LEFT/RIGHT step through them
	SET_ONOFF,   // 0 off, 1 on
	SET_NUMBER,  // minutes, LEFT/RIGHT step through presets (0: off)
	SET_TIME,    // the cart's clock: the field editor (settings_editor_open)
	SET_DATE,    // its date: the field editor
	SET_KEYS,    // a key combination (a KEYINPUT mask), set by pressing it
	SET_COLOUR,  // an LED colour, 0-7: red 4, green 2, blue 1 (0: dark)
	SET_ACTION,  // runs something (settings_row.action), holds no value
};

// The rows, by section, in page order
enum settings_id {
	// Look
	SET_THEME,      // 0 dark, 1 light
	SET_GAME_VIEW,  // 0 list, 1 list with art
	SET_BOX_ART,    // list with art shows covers (boxart.h)
	SET_START_ON,   // the carousel item home faces at power-on: enum settings_start
	SET_DIM,        // idle minutes before the screen dims, 0 off
	// Clock
	SET_TIME_ROW,
	SET_DATE_ROW,
	SET_CLOCK_24H,
	SET_GAME_CLOCK,  // games can read the cart's clock
	// Games
	SET_BOOT_MODE,  // how A, Play and Launch start GBA games: enum settings_boot
	SET_ENGINE,     // the built-in table of patch points before scanning the ROM
	SET_AUTO_SAVE,  // a game's save copied to the card without asking
	SET_SAVE_LOCATION,  // enum settings_location: where the saves are kept
	SET_RTS,        // real-time save (save states)
	SET_CHEATS,
	// Cart
	SET_RUMBLE,     // games can use the cart's rumble motor
	SET_MODE_B,     // 0 rumble, 1 RAM, 2 link
	SET_LED,
	SET_LED_IDLE,   // the LED's colour while idle (BREATHING)
	SET_LED_SD,     // its colour while the SD card works
	// Hotkeys
	SET_RESET_ON,   // the in-game reset to the menu
	SET_RESET_KEYS,
	SET_SLEEP_ON,
	SET_SLEEP_KEYS,
	// Library
	SET_RESCAN,
	SET_IDENTIFY,  // CRCs for the names database (namesdb.h)
	SET_SHOW_ALL,  // the SD card page shows hidden files
	// System
	SET_LOGGING,   // TLOG_OFF to TLOG_DEBUG (tlog.h)
	SET_LANGUAGE,  // an index into lang_packs; hidden until v0.9
	SET_ROWS
};

// Explanations past the rows' (gl_set_explain is indexed by row, then
// these): SET_RESET_ON's while Save states or Cheats put the in-game menu on
// the reset keys (GBApatch.c), and SET_START_ON's for a hidden item
enum {
	SET_EXPLAIN_RESET_STATES = SET_ROWS,  // Save states on: the menu in every game
	SET_EXPLAIN_RESET_CHEATS,             // Cheats alone: in a game with a cheat file
	SET_EXPLAIN_START_HIDDEN,             // Start on's item isn't on home now
	SET_EXPLAINS
};

// SET_START_ON's values: a code per carousel item, stored as it is. The
// codes never change; a new item takes the next one. Start on offers Last
// place and the items home shows (settings_values.start_shown), in the
// carousel's order (settings_step)
enum settings_start {
	SET_START_LAST,  // the carousel item last faced (recent.txt's @boot line)
	SET_START_RECENT,
	SET_START_HOME,  // older kernels' Home: the first item shown; no longer offered
	SET_START_FAVOURITES,
	SET_START_SD,
	SET_START_GBA,
	SET_START_GBC,
	SET_START_GB,
	SET_START_NES,
	SET_START_NOR,
	SET_START_SETTINGS,
	SET_START_INFO,
	SET_STARTS
};
_Static_assert(SET_STARTS <= 16, "Start on's codes fit a u16 mask");
// Start on's choices in the order LEFT/RIGHT step through them: Last place,
// then the carousel's (tests/host/test_registry.c checks it against
// screens/registry.c's items)
extern const u8 settings_start_order[SET_STARTS - 1];
// Every code home can show (bit per enum settings_start)
#define SET_STARTS_ALL ((u16)(((1u << SET_STARTS) - 1) & ~(1u << SET_START_HOME)))

// SET_BOOT_MODE's values, stored as they are
enum settings_boot {
	SET_BOOT_ADDONS,  // with the addons the Games rows turn on (v0.4's A)
	SET_BOOT_CLEAN,
	SET_BOOTS
};

// SET_SAVE_LOCATION's values, stored as they are
enum settings_location {
	SET_LOC_TORII,  // /torii/saves/<rom path>/<slot>.sav
	SET_LOC_BESIDE_ROM,  // in the ROM's folder, <rom stem>.<slot>.sav
	SET_LOCS
};

enum settings_action {
	SET_ACTION_NONE,
	SET_ACTION_RESCAN,  // rescans the library index
	SET_ACTION_IDENTIFY,  // names_identify: Identify library
};

// Row flags
#define SET_HIDDEN 1   // not shown (LANGUAGE until v0.9)
#define SET_WITH_LED 2  // shown only while SET_LED is on

#define SET_NO_SLOT 0xFF
// At most this many rows in a section card (T25)
#define SET_CARD_ROWS 6

struct settings_row {
	u8 section;  // enum settings_section
	u8 kind;     // enum settings_kind
	u8 slot;     // its first settings slot, SET_NO_SLOT for none
	u8 flags;
	// The value an erased or unknown slot reads as; SET_COLOUR: its bits,
	// SET_KEYS: unused (settings_keys_default)
	u16 def;
	// SET_CHOICE: options (the language's: lang_pack_count); SET_NUMBER: the
	// highest value; SET_ONOFF, SET_COLOUR: 2 and 8
	u16 count;
	char ***options;  // SET_CHOICE's names (lang.c); NULL for the language's
	u8 action;        // SET_ACTION: enum settings_action
};

// A section card (its 8x8 icon is settings_page.c's)
struct settings_section_info {
	u8 first;  // its first row (enum settings_id)
	u8 rows;   // how many rows, hidden ones too
};

extern const struct settings_row settings_rows[SET_ROWS];
extern const struct settings_section_info settings_sections[SET_SECTIONS];

// The cart's clock, as the editor and formatter take it
struct settings_clock {
	u16 year;  // 2000-2099
	u8 month;  // 1-12
	u8 day;    // 1-31
	u8 hour;   // 0-23
	u8 minute;
	u8 second;
};

// A page's working copy of the settings
struct settings_values {
	u16 slot[assress_max];    // as they are to be staged
	u16 loaded[assress_max];  // as they were loaded
	// The clock as last read, for SET_TIME_ROW and SET_DATE_ROW: a page
	// reads it again (settings_read_clock) every few frames to keep them
	// current
	struct settings_clock clock;
	// SET_START_ON: the items home shows now, a bit per enum
	// settings_start (SET_STARTS_ALL until the page sets it): the choices
	// Start on offers. A stored item home doesn't show stays stored, and
	// power-on falls back to Last place (registry_start)
	u16 start_shown;
};

// Starts values from slots (assress_max of them, as Read_SET_info reads them)
void settings_values_init(struct settings_values *v, const u16 *slots);
// 1 if a slot differs from the one loaded
int settings_dirty(const struct settings_values *v);

// A row's value (SET_KEYS: the KEYINPUT mask; no value: 0)
u16 settings_get(const struct settings_values *v, u32 id);
// Sets a row's value; a value it can't hold is ignored. A value equal to
// the loaded one puts back the slots as loaded.
void settings_set(struct settings_values *v, u32 id, u16 value);
// LEFT (dir < 0) or RIGHT on a row: the previous or next value, wrapping
void settings_step(struct settings_values *v, u32 id, int dir);
// Puts back a row's slots as loaded (B on an edit), whatever they held
void settings_restore(struct settings_values *v, u32 id);

// What settings_set_keys says about a combination
enum {
	SET_KEYS_OK,
	SET_KEYS_COUNT,        // not 2 or 3 keys (the patches read three)
	SET_KEYS_NO_MODIFIER,  // none of L, R, START and SELECT
	SET_KEYS_TAKEN,        // the other hotkey's, or holding it or held in it
	SET_KEYS_WAKE,         // Sleep on START+SELECT, the keys that wake it
	SET_KEYS_SOFT_RESET,   // within A+B+START+SELECT, games' own soft reset
};
// Sets a SET_KEYS row to the keys held (a KEYINPUT mask, 1 = held); a
// combination it refuses changes nothing
u32 settings_set_keys(struct settings_values *v, u32 id, u16 mask);
// The combination a SET_KEYS row starts with: L+R+START or L+R+SELECT
u16 settings_keys_default(u32 id);
// Both hotkeys as stored in slots and as every reader uses them (KEYINPUT
// masks): one Settings wouldn't accept, or a pair that clashes (written by
// v0.3 or by hand), falls back to the default. Returns SET_HOTKEYS_*_FIXED
// bits for what fell back
#define SET_HOTKEYS_RESET_FIXED 1u
#define SET_HOTKEYS_SLEEP_FIXED 2u
u32 settings_hotkeys(const u16 *slots, u16 *reset, u16 *sleep);
// A key's name by its KEYINPUT bit (0 A ... 9 L), "" past them
const char *settings_key_name(u32 bit);
// Writes mask's keys into keys, L first (the order the hotkeys read), and
// returns how many (at most 3)
u32 settings_keys_list(u16 mask, u8 keys[3]);

// 1 if a row shows: not hidden, and a SET_WITH_LED row only while the LED is on
int settings_visible(const struct settings_values *v, u32 id);
// A section's visible rows into ids (SET_CARD_ROWS of room); returns how many
u32 settings_section_rows(const struct settings_values *v, u32 section, u8 *ids);

// A section's name; a row's label and its one-line explanation (lang.c)
const char *settings_section_name(u32 section);
const char *settings_label(const struct settings_values *v, u32 id);
const char *settings_explain(const struct settings_values *v, u32 id);
// Save states on: their in-game menu takes the reset keys in every game
// (GBApatch.c), so Back to menu shows On and can't be changed
int settings_reset_armed(const struct settings_values *v);
// A row's value in plain words: "On", "Light", "5 min", "14:32",
// "2026-10-20", "L+R+START"; "" for an action
void settings_value_text(const struct settings_values *v, u32 id, char *out, u32 size);
// 1 if the clock shows 24 hours (SET_CLOCK_24H)
int settings_clock_24h(const struct settings_values *v);

// --- The clock ------------------------------------------------------------------

// "14:32", or "2:32 PM" (midnight "12:00 AM", noon "12:00 PM")
void settings_format_time(char *out, u32 size, u32 hour, u32 minute, int h24);
// "2026-10-20"
void settings_format_date(char *out, u32 size, const struct settings_clock *c);
// Days in month of year (Gregorian; 2000-2099 leap every 4 years)
u32 settings_days_in(u32 month, u32 year);
// The day of the week, 0 Sunday to 6 Saturday, as the RTC counts
u32 settings_weekday(u32 year, u32 month, u32 day);
// From the RTC's seven BCD bytes (rtc_get: year, month, day, weekday,
// hour, minute, second); a field out of range reads as its lowest value
void settings_clock_from_rtc(struct settings_clock *c, const u8 *bcd);
// To the seven decimal bytes rtc_set takes, with the weekday worked out
void settings_clock_to_rtc(const struct settings_clock *c, u8 *dec);

// The field editor for SET_TIME_ROW (hour, minute, and AM/PM in 12-hour
// mode) and SET_DATE_ROW (year, month, day): LEFT/RIGHT pick a field,
// UP/DOWN change it, both wrapping; the day keeps within its month. In
// 12-hour mode the hour cycles 12, 1 ... 11 within its half of the day
// (11 AM, UP: 12 AM) and AM/PM moves it by 12.
enum settings_field {
	SET_FIELD_HOUR,
	SET_FIELD_MINUTE,
	SET_FIELD_AMPM,
	SET_FIELD_YEAR,
	SET_FIELD_MONTH,
	SET_FIELD_DAY,
};
struct settings_editor {
	struct settings_clock clock;  // as edited; the time's seconds are 0
	u8 kind;   // SET_TIME or SET_DATE
	u8 h24;
	u8 field;  // the position picked, 0 to fields - 1
	u8 fields;
	u8 field_ids[3];  // enum settings_field, left to right
};
void settings_editor_open(struct settings_editor *e, u32 kind, const struct settings_clock *now, int h24);
void settings_editor_move(struct settings_editor *e, int dir);  // LEFT (< 0) or RIGHT
void settings_editor_step(struct settings_editor *e, int dir);  // DOWN (< 0) or UP
// Field pos's text: "14", "02", "PM", "2026", "10"
void settings_editor_text(const struct settings_editor *e, u32 pos, char *out, u32 size);
// Copies what e edits into now, the clock read again as it is set: the
// time editor's hour and minute (seconds 0), the date editor's date, so
// the rest isn't put back by however long the editor was open
void settings_editor_apply(const struct settings_editor *e, struct settings_clock *now);

#endif
