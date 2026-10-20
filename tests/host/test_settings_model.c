// The settings model (source/settings_model.c): the table, the values in
// their slots, the hotkeys, the clock's formatter and field editor.
#include <string.h>

#include "../../source/engine/font.h"
#include "../../source/engine/text.h"
#include "../../source/lang.h"
#include "../../source/settings_model.h"
#include "../kernel/torii_test.h"
#include "tests.h"

// Hint bar text and a section card's row, in px of body text: the screen
// less its 4 px margins, and a 6 px-padded card's half for a label or value
#define EXPLAIN_W 232
#define EXPLAIN_CHARS 38  // docs/research/v0.4/research-v04-ux.md
#define LABEL_W 112
#define VALUE_W 100

static struct settings_values v;

// An erased settings sector, as on a new cart
static void erased(void)
{
	u16 slots[assress_max];
	for (u32 a = 0; a < assress_max; a++)
		slots[a] = 0xFFFF;
	settings_values_init(&v, slots);
}

static u32 width(const char *s)
{
	return text_width(&font_body, s, strlen(s));
}

static void check_text(u32 id, const char *expected)
{
	char text[32];
	settings_value_text(&v, id, text, sizeof(text));
	if (strcmp(text, expected) != 0) {
		tlog_error("FAIL %s: row %u reads \"%s\", expected \"%s\"", __FILE__, (unsigned)id, text, expected);
		++torii_test_failures;
	}
}

static void check_fits(const char *what, const char *s, u32 max)
{
	if (!s || width(s) > max) {
		tlog_error("FAIL %s: %s \"%s\" is %u px, over %u", __FILE__, what, s ? s : "(null)",
			   (unsigned)(s ? width(s) : 0), (unsigned)max);
		++torii_test_failures;
	}
}

void test_settings_table(void)
{
	LoadEnglish();
	erased();
	u32 next = 0;
	u8 used[assress_max] = {0};
	for (u32 s = 0; s < SET_SECTIONS; s++) {
		const struct settings_section_info *sec = &settings_sections[s];
		CHECK_EQ(sec->first, next);  // sections in page order, no row left out
		CHECK(sec->rows >= 1 && sec->rows <= SET_CARD_ROWS);  // T25
		check_fits("section", settings_section_name(s), LABEL_W);
		for (u32 id = sec->first; id < (u32)sec->first + sec->rows; id++)
			CHECK_EQ(settings_rows[id].section, s);
		next += sec->rows;
	}
	CHECK_EQ(next, SET_ROWS);
	for (u32 id = 0; id < SET_ROWS; id++) {
		const struct settings_row *r = &settings_rows[id];
		check_fits("label", settings_label(&v, id), LABEL_W);
		check_fits("explanation", settings_explain(&v, id), EXPLAIN_W);
		CHECK(strlen(settings_explain(&v, id)) <= EXPLAIN_CHARS);
		if (r->slot == SET_NO_SLOT) {
			CHECK(r->kind == SET_TIME || r->kind == SET_DATE || r->kind == SET_ACTION);
			continue;
		}
		u32 n = r->kind == SET_KEYS || r->kind == SET_COLOUR ? 3 : 1;
		for (u32 a = r->slot; a < r->slot + n; a++) {
			CHECK(a < assress_max);
			CHECK(a != 23 && a != 24);  // DS Style's
			CHECK(a != 29);  // ACCENT's until v0.4 (T21)
			CHECK_EQ(used[a]++, 0);      // no two rows share a slot
		}
	}
	for (u32 id = SET_ROWS; id < SET_LABELS; id++) {
		check_fits("label", gl_set_labels[id], LABEL_W);
		check_fits("explanation", gl_set_explain[id], EXPLAIN_W);
		CHECK(strlen(gl_set_explain[id]) <= EXPLAIN_CHARS);
	}
	// The new slots (from 30), and LANGUAGE hidden until v0.9
	CHECK_EQ(settings_rows[SET_CLOCK_24H].slot, 30);
	CHECK_EQ(settings_rows[SET_GAME_VIEW].slot, 31);
	CHECK_EQ(settings_rows[SET_START_ON].slot, 32);
	CHECK_EQ(settings_rows[SET_SHOW_ALL].slot, 33);
	CHECK(!settings_visible(&v, SET_LANGUAGE));
	CHECK_EQ(settings_rows[SET_RESCAN].action, SET_ACTION_RESCAN);
	// Every value of every row has a name that fits
	for (u32 id = 0; id < SET_ROWS; id++) {
		const struct settings_row *r = &settings_rows[id];
		if (r->kind != SET_CHOICE && r->kind != SET_COLOUR && r->kind != SET_ONOFF)
			continue;
		u32 n = id == SET_LANGUAGE ? lang_pack_count : r->count;
		for (u16 value = 0; value < n; value++) {
			char text[32];
			settings_set(&v, id, value);
			CHECK_EQ(settings_get(&v, id), value);
			settings_value_text(&v, id, text, sizeof(text));
			CHECK(text[0]);
			check_fits("value", text, VALUE_W);
		}
	}
}

void test_settings_defaults(void)
{
	LoadEnglish();
	erased();
	static const struct {
		u8 id;
		u16 value;
		const char *text;
	} want[] = {
		{SET_THEME, 0, "Dark"},
		{SET_GAME_VIEW, 0, "List"},
		{SET_START_ON, SET_START_LAST, "Last place"},
		{SET_DIM, 0, "Off"},
		{SET_CLOCK_24H, 1, "On"},
		{SET_GAME_CLOCK, 1, "On"},
		{SET_ENGINE, 1, "On"},
		{SET_AUTO_SAVE, 0, "Off"},
		{SET_RTS, 0, "Off"},
		{SET_CHEATS, 0, "Off"},
		{SET_RUMBLE, 1, "On"},
		{SET_MODE_B, 2, "Link"},
		{SET_LED, 1, "On"},
		{SET_LED_IDLE, 7, "White"},
		{SET_LED_SD, 0, "Off"},
		{SET_RESET_ON, 0, "Off"},
		{SET_RESET_KEYS, 0x0308, "L+R+START"},
		{SET_SLEEP_ON, 0, "Off"},
		{SET_SLEEP_KEYS, 0x0304, "L+R+SELECT"},
		{SET_RESCAN, 0, ""},
		{SET_SHOW_ALL, 0, "Off"},
		{SET_LOGGING, 0, "Off"},
		{SET_LANGUAGE, 0, "ENGLISH"},
	};
	for (u32 i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
		CHECK_EQ(settings_get(&v, want[i].id), want[i].value);
		check_text(want[i].id, want[i].text);
	}
	CHECK_EQ(settings_keys_default(SET_RESET_KEYS), 0x0308);
	CHECK_EQ(settings_keys_default(SET_SLEEP_KEYS), 0x0304);
	CHECK(!settings_dirty(&v));

	// Values from before v0.4: an SD theme (2) shows as dark, odd values as
	// the defaults, the official kernel's 0 and 1 as they are
	v.slot[assress_theme] = 2;
	CHECK_EQ(settings_get(&v, SET_THEME), 0);
	v.slot[assress_theme] = 1;
	CHECK_EQ(settings_get(&v, SET_THEME), 1);
	// ... and choosing Dark over it stores dark, not the SD theme back
	v.slot[assress_theme] = v.loaded[assress_theme] = 2;
	settings_step(&v, SET_THEME, 1);
	settings_step(&v, SET_THEME, 1);
	CHECK_EQ(v.slot[assress_theme], 0);
	v.slot[assress_theme] = v.loaded[assress_theme] = 0xFFFF;
	v.slot[assress_dim] = 61;
	CHECK_EQ(settings_get(&v, SET_DIM), 0);
	v.slot[assress_dim] = 7;
	check_text(SET_DIM, "7 min");
	v.slot[assress_ModeB_INIT] = 3;
	CHECK_EQ(settings_get(&v, SET_MODE_B), 2);
	v.slot[assress_clock_24h] = 0;
	CHECK_EQ(settings_get(&v, SET_CLOCK_24H), 0);
	v.slot[assress_start_on] = SET_STARTS;
	CHECK_EQ(settings_get(&v, SET_START_ON), SET_START_LAST);
	v.slot[assress_Breathing_G] = 0;  // R and B erased: on
	CHECK_EQ(settings_get(&v, SET_LED_IDLE), 5);
	check_text(SET_LED_IDLE, "Magenta");
	v.slot[assress_edit_rtshotkey_1] = 0;  // L, A, and START by default
	check_text(SET_RESET_KEYS, "L+START+A");
	v.slot[assress_language] = 0x1234;  // a dropped language
	CHECK_EQ(settings_get(&v, SET_LANGUAGE), 0);
}

void test_settings_staged_values(void)
{
	LoadEnglish();
	erased();
	// A change and back leaves the slots as loaded: nothing to write
	settings_step(&v, SET_THEME, 1);
	CHECK_EQ(v.slot[assress_theme], 1);
	CHECK(settings_dirty(&v));
	settings_step(&v, SET_THEME, 1);
	CHECK_EQ(v.slot[assress_theme], 0xFFFF);
	CHECK(!settings_dirty(&v));
	// Steps wrap
	settings_step(&v, SET_START_ON, -1);
	CHECK_EQ(settings_get(&v, SET_START_ON), SET_START_NES);
	settings_step(&v, SET_START_ON, 1);
	CHECK_EQ(v.slot[assress_start_on], 0xFFFF);
	settings_step(&v, SET_LOGGING, -1);
	check_text(SET_LOGGING, "Debug");
	settings_step(&v, SET_LED_SD, -1);
	CHECK_EQ(settings_get(&v, SET_LED_SD), 7);
	CHECK_EQ(v.slot[assress_SD_R], 1);
	CHECK_EQ(v.slot[assress_SD_G], 1);
	CHECK_EQ(v.slot[assress_SD_B], 1);
	settings_set(&v, SET_LED_SD, 4);
	check_text(SET_LED_SD, "Red");
	CHECK_EQ(v.slot[assress_SD_R], 1);
	CHECK_EQ(v.slot[assress_SD_G], 0);
	CHECK_EQ(v.slot[assress_SD_B], 0);
	// DIM steps through its presets, from any value
	static const u8 up[] = {1, 2, 5, 10, 15, 30, 60, 0};
	for (u32 i = 0; i < sizeof(up); i++) {
		settings_step(&v, SET_DIM, 1);
		CHECK_EQ(settings_get(&v, SET_DIM), up[i]);
	}
	settings_set(&v, SET_DIM, 7);
	settings_step(&v, SET_DIM, -1);
	CHECK_EQ(settings_get(&v, SET_DIM), 5);
	settings_set(&v, SET_DIM, 7);
	settings_step(&v, SET_DIM, 1);
	CHECK_EQ(settings_get(&v, SET_DIM), 10);
	settings_step(&v, SET_DIM, -1);
	check_text(SET_DIM, "5 min");
	// Values a row can't hold change nothing
	u16 before[assress_max];
	memcpy(before, v.slot, sizeof(before));
	settings_set(&v, SET_THEME, 2);
	settings_set(&v, SET_DIM, 61);
	settings_set(&v, SET_LED_IDLE, 8);
	settings_set(&v, SET_START_ON, SET_STARTS);
	settings_set(&v, SET_LANGUAGE, (u16)lang_pack_count);
	settings_set(&v, SET_TIME_ROW, 1);
	settings_set(&v, SET_RESCAN, 1);
	settings_set(&v, SET_RESET_KEYS, 0x0003);  // keys go through settings_set_keys
	settings_step(&v, SET_RESET_KEYS, 1);
	settings_set(&v, SET_ROWS, 0);
	CHECK(memcmp(before, v.slot, sizeof(before)) == 0);
	// Each row writes only its own slots
	for (u32 id = 0; id < SET_ROWS; id++) {
		erased();
		settings_step(&v, id, 1);
		for (u32 a = 0; a < assress_max; a++) {
			const struct settings_row *r = &settings_rows[id];
			u32 n = r->kind == SET_KEYS || r->kind == SET_COLOUR ? 3 : 1;
			int own = r->slot != SET_NO_SLOT && a >= r->slot && a < r->slot + n;
			if (!own && v.slot[a] != 0xFFFF) {
				tlog_error("FAIL %s: row %u wrote slot %u", __FILE__, (unsigned)id, (unsigned)a);
				++torii_test_failures;
			}
		}
	}
	// The language is stored by its id
	erased();
	settings_set(&v, SET_LANGUAGE, 0);
	CHECK_EQ(v.slot[assress_language], 0xFFFF);  // already the language shown
	v.slot[assress_language] = v.loaded[assress_language] = 0;  // an unknown id reads as the first
	settings_set(&v, SET_LANGUAGE, 0);
	CHECK_EQ(v.slot[assress_language], 0);
	v.slot[assress_language] = lang_packs[0].id;
	settings_set(&v, SET_LANGUAGE, 0);
	CHECK_EQ(v.slot[assress_language], 0);  // back as loaded
}

void test_settings_hotkeys(void)
{
	LoadEnglish();
	erased();
	const u16 A = 1 << 0, B = 1 << 1, SELECT = 1 << 2, START = 1 << 3, UP = 1 << 6, R = 1 << 8, L = 1 << 9;
	CHECK_EQ(settings_set_keys(&v, SET_RESET_KEYS, L | R | B), SET_KEYS_OK);
	CHECK_EQ(v.slot[assress_edit_rtshotkey_0], 9);
	CHECK_EQ(v.slot[assress_edit_rtshotkey_1], 8);
	CHECK_EQ(v.slot[assress_edit_rtshotkey_2], 1);
	check_text(SET_RESET_KEYS, "L+R+B");
	// Two keys: the last one twice, as the patches read three slots
	CHECK_EQ(settings_set_keys(&v, SET_SLEEP_KEYS, SELECT | START), SET_KEYS_WAKE);  // they wake it
	CHECK_EQ(settings_set_keys(&v, SET_SLEEP_KEYS, SELECT | UP), SET_KEYS_OK);
	CHECK_EQ(v.slot[assress_edit_sleephotkey_0], 6);
	CHECK_EQ(v.slot[assress_edit_sleephotkey_1], 2);
	CHECK_EQ(v.slot[assress_edit_sleephotkey_2], 2);
	CHECK_EQ(settings_get(&v, SET_SLEEP_KEYS), SELECT | UP);
	check_text(SET_SLEEP_KEYS, "UP+SELECT");
	// One key, four keys, no L, R, START or SELECT, or the other hotkey's,
	// or holding it, or held in it: refused, nothing changes
	CHECK_EQ(settings_set_keys(&v, SET_SLEEP_KEYS, L), SET_KEYS_COUNT);
	CHECK_EQ(settings_set_keys(&v, SET_SLEEP_KEYS, 0), SET_KEYS_COUNT);
	CHECK_EQ(settings_set_keys(&v, SET_SLEEP_KEYS, A | B | L | R), SET_KEYS_COUNT);
	CHECK_EQ(settings_set_keys(&v, SET_SLEEP_KEYS, A | B | UP), SET_KEYS_NO_MODIFIER);
	CHECK_EQ(settings_set_keys(&v, SET_SLEEP_KEYS, L | R | B), SET_KEYS_TAKEN);
	CHECK_EQ(settings_set_keys(&v, SET_SLEEP_KEYS, L | B), SET_KEYS_TAKEN);  // held in L+R+B
	CHECK_EQ(settings_set_keys(&v, SET_THEME, L | R), SET_KEYS_COUNT);
	CHECK_EQ(settings_get(&v, SET_SLEEP_KEYS), SELECT | UP);
	CHECK_EQ(settings_set_keys(&v, SET_RESET_KEYS, UP | SELECT | B), SET_KEYS_TAKEN);  // holds UP+SELECT
	// Within a game's soft reset (A+B+START+SELECT): refused for both
	CHECK_EQ(settings_set_keys(&v, SET_RESET_KEYS, START | SELECT), SET_KEYS_SOFT_RESET);
	CHECK_EQ(settings_set_keys(&v, SET_RESET_KEYS, A | B | START), SET_KEYS_SOFT_RESET);
	CHECK_EQ(settings_set_keys(&v, SET_SLEEP_KEYS, B | SELECT), SET_KEYS_SOFT_RESET);
	CHECK_EQ(settings_set_keys(&v, SET_RESET_KEYS, R | START | SELECT), SET_KEYS_OK);
	// The other hotkey counts even while it is off
	CHECK_EQ(settings_get(&v, SET_SLEEP_ON), 0);
	CHECK_EQ(settings_set_keys(&v, SET_RESET_KEYS, UP | SELECT), SET_KEYS_TAKEN);
	CHECK_EQ(settings_set_keys(&v, SET_RESET_KEYS, L | R | B), SET_KEYS_OK);
	CHECK_EQ(settings_get(&v, SET_RESET_KEYS), L | R | B);
	// Bits past L aren't keys
	CHECK_EQ(settings_set_keys(&v, SET_SLEEP_KEYS, (u16)(UP | A | R | 0x8000)), SET_KEYS_OK);
	CHECK_EQ(settings_get(&v, SET_SLEEP_KEYS), UP | A | R);
	// Back to the default: the slots as loaded
	CHECK_EQ(settings_set_keys(&v, SET_SLEEP_KEYS, L | R | SELECT), SET_KEYS_OK);
	CHECK_EQ(settings_set_keys(&v, SET_RESET_KEYS, L | R | START), SET_KEYS_OK);
	CHECK(!settings_dirty(&v));
	// B on an edit puts back what was loaded, even a combination the
	// rules refuse now (a v0.3 card's A+A+B)
	v.loaded[assress_edit_rtshotkey_0] = 0;
	v.loaded[assress_edit_rtshotkey_1] = 0;
	v.loaded[assress_edit_rtshotkey_2] = 1;
	CHECK_EQ(settings_set_keys(&v, SET_RESET_KEYS, L | R | B), SET_KEYS_OK);
	settings_restore(&v, SET_RESET_KEYS);
	CHECK_EQ(settings_get(&v, SET_RESET_KEYS), A | B);
	CHECK(!settings_dirty(&v));
	settings_set(&v, SET_THEME, 1);
	settings_restore(&v, SET_THEME);
	settings_restore(&v, SET_TIME_ROW);
	settings_restore(&v, SET_ROWS);
	CHECK(!settings_dirty(&v));
	erased();
	u8 keys[3];
	CHECK_EQ(settings_keys_list(L | R | START, keys), 3);
	CHECK_EQ(keys[0], 9);
	CHECK_EQ(keys[1], 8);
	CHECK_EQ(keys[2], 3);
	CHECK(!strcmp(settings_key_name(4), "RIGHT"));
	CHECK(!strcmp(settings_key_name(10), ""));
	// Save states alone make the hotkeys save and load a state
	CHECK(!strcmp(settings_label(&v, SET_SLEEP_KEYS), "Sleep keys"));
	settings_set(&v, SET_RTS, 1);
	CHECK(!strcmp(settings_label(&v, SET_SLEEP_KEYS), "Save state keys"));
	CHECK(!strcmp(settings_label(&v, SET_RESET_KEYS), "Load state keys"));
	CHECK(!strcmp(settings_explain(&v, SET_RESET_KEYS), gl_set_explain[SET_LABEL_LOAD_KEYS]));
	settings_set(&v, SET_SLEEP_ON, 1);
	CHECK(!strcmp(settings_label(&v, SET_SLEEP_KEYS), "Sleep keys"));
}

void test_settings_visible_rows(void)
{
	LoadEnglish();
	erased();
	u8 ids[SET_CARD_ROWS];
	CHECK_EQ(settings_section_rows(&v, SET_SEC_CART, ids), 5);
	CHECK_EQ(ids[3], SET_LED_IDLE);
	settings_set(&v, SET_LED, 0);
	CHECK(!settings_visible(&v, SET_LED_IDLE));
	CHECK_EQ(settings_section_rows(&v, SET_SEC_CART, ids), 3);
	CHECK_EQ(ids[2], SET_LED);
	CHECK_EQ(settings_section_rows(&v, SET_SEC_SYSTEM, ids), 1);  // LANGUAGE hidden
	CHECK_EQ(ids[0], SET_LOGGING);
	CHECK_EQ(settings_section_rows(&v, SET_SEC_LIBRARY, ids), 2);
	CHECK_EQ(ids[0], SET_RESCAN);
	CHECK_EQ(settings_section_rows(&v, SET_SECTIONS, ids), 0);
	CHECK(!settings_visible(&v, SET_ROWS));
}

static void check_time(u32 hour, u32 minute, int h24, const char *expected)
{
	char text[16];
	settings_format_time(text, sizeof(text), hour, minute, h24);
	if (strcmp(text, expected) != 0) {
		tlog_error("FAIL %s: %02u:%02u (%s) reads \"%s\", expected \"%s\"", __FILE__, (unsigned)hour,
			   (unsigned)minute, h24 ? "24" : "12", text, expected);
		++torii_test_failures;
	}
}

void test_settings_clock_format(void)
{
	LoadEnglish();
	check_time(0, 0, 1, "00:00");
	check_time(0, 0, 0, "12:00 AM");  // midnight
	check_time(0, 59, 0, "12:59 AM");
	check_time(1, 5, 0, "1:05 AM");
	check_time(11, 59, 0, "11:59 AM");
	check_time(12, 0, 1, "12:00");
	check_time(12, 0, 0, "12:00 PM");  // noon
	check_time(13, 5, 0, "1:05 PM");
	check_time(14, 32, 1, "14:32");
	check_time(14, 32, 0, "2:32 PM");
	check_time(23, 59, 1, "23:59");
	check_time(23, 59, 0, "11:59 PM");
	char text[8];  // too small: cut, still terminated
	settings_format_time(text, 4, 14, 32, 1);
	CHECK(!strcmp(text, "14:"));

	struct settings_clock c = {2026, 10, 20, 14, 32, 5};
	char date[16];
	settings_format_date(date, sizeof(date), &c);
	CHECK(!strcmp(date, "2026-10-20"));
	erased();
	v.clock = c;
	check_text(SET_TIME_ROW, "14:32");
	check_text(SET_DATE_ROW, "2026-10-20");
	settings_set(&v, SET_CLOCK_24H, 0);
	check_text(SET_TIME_ROW, "2:32 PM");

	CHECK_EQ(settings_days_in(2, 2024), 29);
	CHECK_EQ(settings_days_in(2, 2026), 28);
	CHECK_EQ(settings_days_in(2, 2000), 29);  // divisible by 400
	CHECK_EQ(settings_days_in(2, 2100), 28);  // by 100
	CHECK_EQ(settings_days_in(4, 2026), 30);
	CHECK_EQ(settings_days_in(12, 2026), 31);
	CHECK_EQ(settings_days_in(13, 2026), 31);
	CHECK_EQ(settings_weekday(2026, 10, 20), 2);  // a Tuesday
	CHECK_EQ(settings_weekday(2000, 1, 1), 6);    // a Saturday
	CHECK_EQ(settings_weekday(2024, 2, 29), 4);   // a Thursday
	CHECK_EQ(settings_weekday(2099, 12, 31), 4);

	// The RTC's BCD, and what rtc_set takes
	const u8 bcd[7] = {0x26, 0x10, 0x20, 0x02, 0x14, 0x32, 0x05};
	struct settings_clock r;
	settings_clock_from_rtc(&r, bcd);
	CHECK_EQ(r.year, 2026);
	CHECK_EQ(r.month, 10);
	CHECK_EQ(r.day, 20);
	CHECK_EQ(r.hour, 14);
	CHECK_EQ(r.minute, 32);
	CHECK_EQ(r.second, 5);
	const u8 junk[7] = {0xFF, 0x13, 0x32, 0x07, 0x24, 0x60, 0x7A};
	settings_clock_from_rtc(&r, junk);
	CHECK_EQ(r.year, 2000);
	CHECK_EQ(r.month, 1);
	CHECK_EQ(r.day, 1);
	CHECK_EQ(r.hour, 0);
	CHECK_EQ(r.minute, 0);
	CHECK_EQ(r.second, 0);
	const u8 feb30[7] = {0x26, 0x02, 0x30, 0, 0x08, 0, 0};
	settings_clock_from_rtc(&r, feb30);
	CHECK_EQ(r.day, 1);
	u8 dec[7];
	settings_clock_to_rtc(&c, dec);
	CHECK_EQ(dec[_YEAR], 26);
	CHECK_EQ(dec[_MONTH], 10);
	CHECK_EQ(dec[_DAY], 20);
	CHECK_EQ(dec[_WKD], 2);
	CHECK_EQ(dec[_HOUR], 14);
	CHECK_EQ(dec[_MIN], 32);
	CHECK_EQ(dec[_SEC], 5);
}

static void check_field(const struct settings_editor *e, u32 pos, const char *expected)
{
	char text[8];
	settings_editor_text(e, pos, text, sizeof(text));
	if (strcmp(text, expected) != 0) {
		tlog_error("FAIL %s: field %u reads \"%s\", expected \"%s\"", __FILE__, (unsigned)pos, text, expected);
		++torii_test_failures;
	}
}

void test_settings_clock_editor(void)
{
	LoadEnglish();
	struct settings_editor e;
	const struct settings_clock now = {2024, 1, 31, 23, 0, 41};

	// 24 hours: hour and minute, wrapping both ways; the seconds start at 0
	settings_editor_open(&e, SET_TIME, &now, 1);
	CHECK_EQ(e.fields, 2);
	CHECK_EQ(e.clock.second, 0);
	check_field(&e, 0, "23");
	check_field(&e, 1, "00");
	check_field(&e, 2, "");
	settings_editor_step(&e, 1);
	CHECK_EQ(e.clock.hour, 0);
	settings_editor_step(&e, -1);
	CHECK_EQ(e.clock.hour, 23);
	settings_editor_move(&e, -1);  // wraps to the minute
	CHECK_EQ(e.field, 1);
	settings_editor_step(&e, -1);
	CHECK_EQ(e.clock.minute, 59);
	settings_editor_step(&e, 1);
	CHECK_EQ(e.clock.minute, 0);
	settings_editor_move(&e, 1);
	CHECK_EQ(e.field, 0);
	CHECK_EQ(e.clock.day, 31);  // the date stays

	// 12 hours: 12 AM is midnight, the hour keeps its half of the day,
	// AM/PM moves it by 12
	struct settings_clock midnight = now;
	midnight.hour = 0;
	settings_editor_open(&e, SET_TIME, &midnight, 0);
	CHECK_EQ(e.fields, 3);
	check_field(&e, 0, "12");
	check_field(&e, 2, "AM");
	settings_editor_step(&e, 1);
	CHECK_EQ(e.clock.hour, 1);
	settings_editor_step(&e, -1);
	settings_editor_step(&e, -1);
	CHECK_EQ(e.clock.hour, 11);
	check_field(&e, 0, "11");
	settings_editor_move(&e, -1);
	CHECK_EQ(e.field, 2);
	settings_editor_step(&e, 1);
	CHECK_EQ(e.clock.hour, 23);
	check_field(&e, 2, "PM");
	settings_editor_move(&e, 1);
	settings_editor_step(&e, 1);  // 11 PM to 12 PM: noon
	CHECK_EQ(e.clock.hour, 12);
	check_field(&e, 0, "12");
	settings_editor_move(&e, -1);
	settings_editor_step(&e, -1);
	CHECK_EQ(e.clock.hour, 0);

	// The date: year, month, day; the day keeps within its month
	settings_editor_open(&e, SET_DATE, &now, 1);
	CHECK_EQ(e.fields, 3);
	CHECK_EQ(e.clock.second, 41);  // the time isn't edited here
	check_field(&e, 0, "2024");
	check_field(&e, 1, "01");
	check_field(&e, 2, "31");
	settings_editor_move(&e, 1);
	settings_editor_step(&e, 1);  // January 31 to February: 29 in 2024
	CHECK_EQ(e.clock.month, 2);
	CHECK_EQ(e.clock.day, 29);
	settings_editor_move(&e, -1);
	settings_editor_step(&e, 1);  // 2025: 28
	CHECK_EQ(e.clock.year, 2025);
	CHECK_EQ(e.clock.day, 28);
	settings_editor_move(&e, -1);  // wraps to the day
	CHECK_EQ(e.field, 2);
	settings_editor_step(&e, 1);
	CHECK_EQ(e.clock.day, 1);
	settings_editor_step(&e, -1);
	CHECK_EQ(e.clock.day, 28);
	settings_editor_move(&e, 1);
	e.clock.year = 2000;
	settings_editor_step(&e, -1);
	CHECK_EQ(e.clock.year, 2099);  // wraps under 2000
	settings_editor_step(&e, 1);
	CHECK_EQ(e.clock.year, 2000);
	settings_editor_move(&e, 1);
	settings_editor_step(&e, -1);  // February to January
	CHECK_EQ(e.clock.month, 1);
	settings_editor_step(&e, -1);
	CHECK_EQ(e.clock.month, 12);
	settings_editor_step(&e, 1);
	CHECK_EQ(e.clock.month, 1);
	check_field(&e, 3, "");
}

// The editors set only what they edit over the clock read as it is set
void test_settings_clock_apply(void)
{
	struct settings_editor e;
	const struct settings_clock opened = {2026, 10, 20, 23, 59, 30};
	struct settings_clock now = {2026, 10, 21, 0, 4, 12};  // read again, past midnight
	settings_editor_open(&e, SET_TIME, &opened, 1);
	settings_editor_move(&e, 1);
	settings_editor_step(&e, -1);  // 23:58
	settings_editor_apply(&e, &now);
	CHECK_EQ(now.year, 2026);
	CHECK_EQ(now.month, 10);
	CHECK_EQ(now.day, 21);  // the new day stays
	CHECK_EQ(now.hour, 23);
	CHECK_EQ(now.minute, 58);
	CHECK_EQ(now.second, 0);
	struct settings_clock later = {2026, 10, 21, 0, 9, 45};
	settings_editor_open(&e, SET_DATE, &opened, 1);
	settings_editor_step(&e, 1);  // 2027-10-20
	settings_editor_apply(&e, &later);
	CHECK_EQ(later.year, 2027);
	CHECK_EQ(later.month, 10);
	CHECK_EQ(later.day, 20);
	CHECK_EQ(later.hour, 0);  // the time keeps running
	CHECK_EQ(later.minute, 9);
	CHECK_EQ(later.second, 45);
}
