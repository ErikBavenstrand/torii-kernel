// Settings as data (settings_model.h).
#include <stdio.h>
#include <string.h>

#include "lang.h"
#include "settings_model.h"

// clang-format off
const struct settings_row settings_rows[SET_ROWS] = {
	[SET_THEME] = {SET_SEC_LOOK, SET_CHOICE, assress_theme, 0, 0, 2, &gl_set_themes, 0},
	[SET_GAME_VIEW] = {SET_SEC_LOOK, SET_CHOICE, assress_game_view, 0, 0, 2, &gl_set_views, 0},
	[SET_START_ON] = {SET_SEC_LOOK, SET_CHOICE, assress_start_on, 0, SET_START_LAST, SET_STARTS, &gl_set_starts, 0},
	[SET_DIM] = {SET_SEC_LOOK, SET_NUMBER, assress_dim, 0, 0, 60, 0, 0},
	[SET_TIME_ROW] = {SET_SEC_CLOCK, SET_TIME, SET_NO_SLOT, 0, 0, 0, 0, 0},
	[SET_DATE_ROW] = {SET_SEC_CLOCK, SET_DATE, SET_NO_SLOT, 0, 0, 0, 0, 0},
	[SET_CLOCK_24H] = {SET_SEC_CLOCK, SET_ONOFF, assress_clock_24h, 0, 1, 2, 0, 0},
	[SET_GAME_CLOCK] = {SET_SEC_CLOCK, SET_ONOFF, assress_game_clock, 0, 1, 2, 0, 0},
	[SET_ENGINE] = {SET_SEC_GAMES, SET_ONOFF, assress_engine_sel, 0, 1, 2, 0, 0},
	[SET_AUTO_SAVE] = {SET_SEC_GAMES, SET_ONOFF, assress_auto_save_sel, 0, 0, 2, 0, 0},
	[SET_RTS] = {SET_SEC_GAMES, SET_ONOFF, assress_v_rts, 0, 0, 2, 0, 0},
	[SET_CHEATS] = {SET_SEC_GAMES, SET_ONOFF, assress_v_cheat, 0, 0, 2, 0, 0},
	[SET_RUMBLE] = {SET_SEC_CART, SET_ONOFF, assress_game_rumble, 0, 1, 2, 0, 0},
	[SET_MODE_B] = {SET_SEC_CART, SET_CHOICE, assress_ModeB_INIT, 0, 2, 3, &gl_set_modes_b, 0},
	[SET_LED] = {SET_SEC_CART, SET_ONOFF, assress_led_open_sel, 0, 1, 2, 0, 0},
	[SET_LED_IDLE] = {SET_SEC_CART, SET_COLOUR, assress_Breathing_R, SET_WITH_LED, 7, 8, &gl_set_colours, 0},
	[SET_LED_SD] = {SET_SEC_CART, SET_COLOUR, assress_SD_R, SET_WITH_LED, 0, 8, &gl_set_colours, 0},
	[SET_RESET_ON] = {SET_SEC_HOTKEYS, SET_ONOFF, assress_v_reset, 0, 0, 2, 0, 0},
	[SET_RESET_KEYS] = {SET_SEC_HOTKEYS, SET_KEYS, assress_edit_rtshotkey_0, 0, 0, 0, 0, 0},
	[SET_SLEEP_ON] = {SET_SEC_HOTKEYS, SET_ONOFF, assress_v_sleep, 0, 0, 2, 0, 0},
	[SET_SLEEP_KEYS] = {SET_SEC_HOTKEYS, SET_KEYS, assress_edit_sleephotkey_0, 0, 0, 0, 0, 0},
	[SET_RESCAN] = {SET_SEC_LIBRARY, SET_ACTION, SET_NO_SLOT, 0, 0, 0, 0, SET_ACTION_RESCAN},
	[SET_SHOW_ALL] = {SET_SEC_LIBRARY, SET_ONOFF, assress_show_all, 0, 0, 2, 0, 0},
	[SET_LOGGING] = {SET_SEC_SYSTEM, SET_CHOICE, assress_logging, 0, 0, 4, &gl_set_log_levels, 0},
	[SET_LANGUAGE] = {SET_SEC_SYSTEM, SET_CHOICE, assress_language, SET_HIDDEN, 0, 0, 0, 0},
};

const struct settings_section_info settings_sections[SET_SECTIONS] = {
	[SET_SEC_LOOK] = {SET_ICON_LOOK, SET_THEME, 4},
	[SET_SEC_CLOCK] = {SET_ICON_CLOCK, SET_TIME_ROW, 4},
	[SET_SEC_GAMES] = {SET_ICON_GAMES, SET_ENGINE, 4},
	[SET_SEC_CART] = {SET_ICON_CART, SET_RUMBLE, 5},
	[SET_SEC_HOTKEYS] = {SET_ICON_KEYS, SET_RESET_ON, 4},
	[SET_SEC_LIBRARY] = {SET_ICON_LIBRARY, SET_RESCAN, 2},
	[SET_SEC_SYSTEM] = {SET_ICON_SYSTEM, SET_LOGGING, 2},
};
// clang-format on

// The hotkeys as the in-game patches read them (GBApatch.c's defaults):
// three KEYINPUT bits, a slot past 9 standing for its default's
#define KEY_BITS 10
static const u8 reset_default[3] = {9, 8, 3};  // L + R + START
static const u8 sleep_default[3] = {9, 8, 2};  // L + R + SELECT
// A hotkey holds one of these, so a game's own single presses can't fire
// it: SELECT, START, R and L
#define KEYS_MODIFIERS 0x030C
#define KEYS_WAKE 0x000C  // START+SELECT wake a game from sleep
#define KEYS_SOFT_RESET 0x000F  // A+B+START+SELECT, most games' own reset
static const char *const key_names[KEY_BITS] = {"A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN", "R", "L"};

// DIM's steps
static const u8 dim_presets[] = {0, 1, 2, 5, 10, 15, 30, 60};

static const struct settings_row *row_of(u32 id)
{
	return id < SET_ROWS ? &settings_rows[id] : 0;
}

void settings_values_init(struct settings_values *v, const u16 *slots)
{
	memcpy(v->slot, slots, sizeof(v->slot));
	memcpy(v->loaded, slots, sizeof(v->loaded));
}

int settings_dirty(const struct settings_values *v)
{
	return memcmp(v->slot, v->loaded, sizeof(v->slot)) != 0;
}

static u32 slots_of(const struct settings_row *r)
{
	if (!r || r->slot == SET_NO_SLOT)
		return 0;
	return r->kind == SET_KEYS || r->kind == SET_COLOUR ? 3 : 1;
}

static u16 keys_mask(const u16 *slot, const u8 *def)
{
	u16 mask = 0;
	for (u32 i = 0; i < 3; i++)
		mask |= (u16)(1u << (slot[i] < KEY_BITS ? slot[i] : def[i]));
	return mask;
}

static const u8 *keys_default_of(u32 id)
{
	return id == SET_SLEEP_KEYS ? sleep_default : reset_default;
}

static u32 language_index(u16 stored)
{
	for (u32 i = 0; i < lang_pack_count; i++)
		if (lang_packs[i].id == stored)
			return i;
	return 0;
}

// A row's value from slots (v->slot or v->loaded)
static u16 value_in(const u16 *slots, u32 id)
{
	const struct settings_row *r = row_of(id);
	if (!r || r->slot == SET_NO_SLOT)
		return 0;
	const u16 *s = &slots[r->slot];
	switch (r->kind) {
	case SET_KEYS:
		return keys_mask(s, keys_default_of(id));
	case SET_COLOUR: {
		u16 c = 0;
		for (u32 i = 0; i < 3; i++) {
			u32 bit = 2 - i;  // R, G, B slots
			u32 on = s[i] <= 1 ? s[i] : (r->def >> bit) & 1;
			c |= (u16)(on << bit);
		}
		return c;
	}
	case SET_CHOICE:
		if (id == SET_LANGUAGE)
			return (u16)language_index(*s);
		return *s < r->count ? *s : r->def;
	case SET_NUMBER:  // DIM: 1 to count minutes, else off
		return *s >= 1 && *s <= r->count ? *s : 0;
	default:  // SET_ONOFF
		return *s <= 1 ? *s : r->def;
	}
}

u16 settings_get(const struct settings_values *v, u32 id)
{
	return value_in(v->slot, id);
}

// 1 if value is one a row holds
static int holds(const struct settings_row *r, u32 id, u16 value)
{
	switch (r->kind) {
	case SET_CHOICE:
		return value < (id == SET_LANGUAGE ? lang_pack_count : r->count);
	case SET_NUMBER:
		return value <= r->count;
	case SET_ONOFF:
		return value <= 1;
	case SET_COLOUR:
		return value <= 7;
	default:
		return 0;
	}
}

// Writes a value the row holds into v->slot: the slots as loaded if they
// read as value, so a change and back leaves nothing to write
static void store(struct settings_values *v, u32 id, u16 value)
{
	const struct settings_row *r = row_of(id);
	u32 n = slots_of(r);
	if (!n || r->slot + n > assress_max)
		return;
	if (value_in(v->loaded, id) == value) {
		memcpy(&v->slot[r->slot], &v->loaded[r->slot], n * sizeof(u16));
		return;
	}
	u16 *s = &v->slot[r->slot];
	switch (r->kind) {
	case SET_KEYS: {
		u8 keys[3];
		u32 count = settings_keys_list(value, keys);
		if (!count)
			return;
		for (u32 i = 0; i < 3; i++)  // two keys: the last one twice
			s[i] = keys[i < count ? i : count - 1];
		break;
	}
	case SET_COLOUR:
		for (u32 i = 0; i < 3; i++)
			s[i] = (value >> (2 - i)) & 1;
		break;
	case SET_CHOICE:
		*s = id == SET_LANGUAGE ? lang_packs[value].id : value;
		break;
	default:
		*s = value;
		break;
	}
}

void settings_set(struct settings_values *v, u32 id, u16 value)
{
	const struct settings_row *r = row_of(id);
	if (r && slots_of(r) && holds(r, id, value))
		store(v, id, value);
}

static u16 dim_step(u16 minutes, int dir)
{
	const u32 n = sizeof(dim_presets);
	if (dir > 0) {
		for (u32 i = 0; i < n; i++)
			if (dim_presets[i] > minutes)
				return dim_presets[i];
		return dim_presets[0];
	}
	for (u32 i = n; i-- > 0;)
		if (dim_presets[i] < minutes)
			return dim_presets[i];
	return dim_presets[n - 1];
}

void settings_step(struct settings_values *v, u32 id, int dir)
{
	const struct settings_row *r = row_of(id);
	if (!r || !slots_of(r) || r->kind == SET_KEYS)
		return;
	u16 value = settings_get(v, id);
	if (r->kind == SET_NUMBER) {
		value = dim_step(value, dir);
	} else {
		u32 n = r->kind == SET_ONOFF    ? 2
			: r->kind == SET_COLOUR ? 8
			: id == SET_LANGUAGE    ? lang_pack_count
						: r->count;
		if (n == 0)
			return;
		value = (u16)(dir < 0 ? (value + n - 1) % n : (value + 1) % n);
	}
	store(v, id, value);
}

void settings_restore(struct settings_values *v, u32 id)
{
	const struct settings_row *r = row_of(id);
	u32 n = slots_of(r);
	if (n && r->slot + n <= assress_max)
		memcpy(&v->slot[r->slot], &v->loaded[r->slot], n * sizeof(u16));
}

u32 settings_keys_list(u16 mask, u8 keys[3])
{
	u32 n = 0;
	for (u32 bit = KEY_BITS; bit-- > 0 && n < 3;)
		if (mask & (1u << bit))
			keys[n++] = (u8)bit;
	return n;
}

static u32 bits(u16 mask)
{
	u32 n = 0;
	for (; mask; mask &= (u16)(mask - 1))
		n++;
	return n;
}

u32 settings_set_keys(struct settings_values *v, u32 id, u16 mask)
{
	const struct settings_row *r = row_of(id);
	if (!r || r->kind != SET_KEYS)
		return SET_KEYS_COUNT;
	mask &= (1u << KEY_BITS) - 1;
	u32 n = bits(mask);
	if (n < 2 || n > 3)
		return SET_KEYS_COUNT;
	if (!(mask & KEYS_MODIFIERS))
		return SET_KEYS_NO_MODIFIER;
	// Sleep wakes on START+SELECT (gba_sleep_patch.s), so it can't sleep on them
	if (id == SET_SLEEP_KEYS && (mask & KEYS_WAKE) == mask)
		return SET_KEYS_WAKE;
	// Inside a game's own soft reset, A+B+START+SELECT, it would fire first
	if ((mask & KEYS_SOFT_RESET) == mask)
		return SET_KEYS_SOFT_RESET;
	// The other hotkey's, or one holding it or held in it: the smaller fires
	// first. Checked even while the other is off, as turning it on checks
	// nothing
	u32 other = id == SET_SLEEP_KEYS ? SET_RESET_KEYS : SET_SLEEP_KEYS;
	u16 taken = settings_get(v, other);
	if ((taken & mask) == taken || (taken & mask) == mask)
		return SET_KEYS_TAKEN;
	store(v, id, mask);
	return SET_KEYS_OK;
}

u16 settings_keys_default(u32 id)
{
	const u8 *d = keys_default_of(id);
	return (u16)((1u << d[0]) | (1u << d[1]) | (1u << d[2]));
}

const char *settings_key_name(u32 bit)
{
	return bit < KEY_BITS ? key_names[bit] : "";
}

int settings_visible(const struct settings_values *v, u32 id)
{
	const struct settings_row *r = row_of(id);
	if (!r || (r->flags & SET_HIDDEN))
		return 0;
	return !(r->flags & SET_WITH_LED) || settings_get(v, SET_LED);
}

u32 settings_section_rows(const struct settings_values *v, u32 section, u8 *ids)
{
	if (section >= SET_SECTIONS)
		return 0;
	const struct settings_section_info *s = &settings_sections[section];
	u32 n = 0;
	for (u32 id = s->first; id < (u32)s->first + s->rows && n < SET_CARD_ROWS; id++)
		if (settings_visible(v, id))
			ids[n++] = (u8)id;
	return n;
}

const char *settings_section_name(u32 section)
{
	return section < SET_SECTIONS ? gl_set_sections[section] : "";
}

// Real-time save alone turns the hotkeys into save and load keys (GBApatch.c)
static int save_keys(const struct settings_values *v)
{
	return settings_get(v, SET_RTS) && !settings_get(v, SET_CHEATS) && !settings_get(v, SET_RESET_ON) &&
	       !settings_get(v, SET_SLEEP_ON);
}

static u32 label_of(const struct settings_values *v, u32 id)
{
	if (save_keys(v) && id == SET_SLEEP_KEYS)
		return SET_LABEL_SAVE_KEYS;
	if (save_keys(v) && id == SET_RESET_KEYS)
		return SET_LABEL_LOAD_KEYS;
	return id;
}

const char *settings_label(const struct settings_values *v, u32 id)
{
	return id < SET_ROWS ? gl_set_labels[label_of(v, id)] : "";
}

const char *settings_explain(const struct settings_values *v, u32 id)
{
	return id < SET_ROWS ? gl_set_explain[label_of(v, id)] : "";
}

int settings_clock_24h(const struct settings_values *v)
{
	return settings_get(v, SET_CLOCK_24H) != 0;
}

void settings_value_text(const struct settings_values *v, u32 id, char *out, u32 size)
{
	const struct settings_row *r = row_of(id);
	if (!size)
		return;
	out[0] = 0;
	if (!r)
		return;
	u16 value = settings_get(v, id);
	switch (r->kind) {
	case SET_CHOICE:
		snprintf(out, size, "%s", id == SET_LANGUAGE ? lang_packs[value].name : (*r->options)[value]);
		break;
	case SET_COLOUR:
		snprintf(out, size, "%s", (*r->options)[value]);
		break;
	case SET_ONOFF:
		snprintf(out, size, "%s", gl_set_onoff[value]);
		break;
	case SET_NUMBER:
		if (value)
			snprintf(out, size, "%u %s", (unsigned)value, gl_set_minutes);
		else
			snprintf(out, size, "%s", gl_set_onoff[0]);
		break;
	case SET_TIME:
		settings_format_time(out, size, v->clock.hour, v->clock.minute, settings_clock_24h(v));
		break;
	case SET_DATE:
		settings_format_date(out, size, &v->clock);
		break;
	case SET_KEYS: {
		u8 keys[3];
		u32 n = settings_keys_list(value, keys);
		u32 len = 0;
		for (u32 i = 0; i < n && len < size; i++)
			len += (u32)snprintf(out + len, size - len, "%s%s", i ? "+" : "", key_names[keys[i]]);
		break;
	}
	default:  // SET_ACTION
		break;
	}
}

// --- The clock ------------------------------------------------------------------

void settings_format_time(char *out, u32 size, u32 hour, u32 minute, int h24)
{
	hour %= 24;
	minute %= 60;
	if (h24)
		snprintf(out, size, "%02u:%02u", (unsigned)hour, (unsigned)minute);
	else
		snprintf(out, size, "%u:%02u %s", (unsigned)(hour % 12 ? hour % 12 : 12), (unsigned)minute,
			 gl_set_ampm[hour >= 12]);
}

void settings_format_date(char *out, u32 size, const struct settings_clock *c)
{
	snprintf(out, size, "%04u-%02u-%02u", (unsigned)c->year, (unsigned)c->month, (unsigned)c->day);
}

u32 settings_days_in(u32 month, u32 year)
{
	static const u8 days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	if (month < 1 || month > 12)
		return 31;
	int leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
	return month == 2 && leap ? 29 : days[month - 1];
}

u32 settings_weekday(u32 year, u32 month, u32 day)
{
	// Sakamoto's method
	static const u8 t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
	if (month < 1 || month > 12)
		return 0;
	if (month < 3)
		year--;
	return (year + year / 4 - year / 100 + year / 400 + t[month - 1] + day) % 7;
}

static u8 unbcd(u8 b, u8 lo, u8 hi)
{
	u8 v = (u8)((b & 0xF) + (b >> 4) * 10);
	return (b & 0xF) > 9 || v < lo || v > hi ? lo : v;
}

void settings_clock_from_rtc(struct settings_clock *c, const u8 *bcd)
{
	c->year = (u16)(2000 + unbcd(bcd[_YEAR], 0, 99));
	c->month = unbcd(bcd[_MONTH] & 0x1F, 1, 12);
	c->day = unbcd(bcd[_DAY] & 0x3F, 1, 31);
	if (c->day > settings_days_in(c->month, c->year))
		c->day = 1;
	c->hour = unbcd(bcd[_HOUR] & 0x3F, 0, 23);
	c->minute = unbcd(bcd[_MIN] & 0x7F, 0, 59);
	c->second = unbcd(bcd[_SEC] & 0x7F, 0, 59);
}

void settings_clock_to_rtc(const struct settings_clock *c, u8 *dec)
{
	dec[_YEAR] = (u8)(c->year % 100);
	dec[_MONTH] = c->month;
	dec[_DAY] = c->day;
	dec[_WKD] = (u8)settings_weekday(c->year, c->month, c->day);
	dec[_HOUR] = c->hour;
	dec[_MIN] = c->minute;
	dec[_SEC] = c->second;
}

void settings_editor_open(struct settings_editor *e, u32 kind, const struct settings_clock *now, int h24)
{
	memset(e, 0, sizeof(*e));
	e->clock = *now;
	e->kind = (u8)kind;
	e->h24 = (u8)(h24 != 0);
	if (kind == SET_DATE) {
		e->fields = 3;
		e->field_ids[0] = SET_FIELD_YEAR;
		e->field_ids[1] = SET_FIELD_MONTH;
		e->field_ids[2] = SET_FIELD_DAY;
	} else {
		e->clock.second = 0;
		e->fields = e->h24 ? 2 : 3;
		e->field_ids[0] = SET_FIELD_HOUR;
		e->field_ids[1] = SET_FIELD_MINUTE;
		e->field_ids[2] = SET_FIELD_AMPM;
	}
}

void settings_editor_move(struct settings_editor *e, int dir)
{
	if (!e->fields)
		return;
	e->field = (u8)(dir < 0 ? (e->field + e->fields - 1) % e->fields : (e->field + 1) % e->fields);
}

// v one up (dir > 0) or down within lo-hi, wrapping
static u32 wrap(u32 v, u32 lo, u32 hi, int dir)
{
	if (dir > 0)
		return v >= hi ? lo : v + 1;
	return v <= lo ? hi : v - 1;
}

void settings_editor_step(struct settings_editor *e, int dir)
{
	struct settings_clock *c = &e->clock;
	if (e->field >= e->fields)
		return;
	switch (e->field_ids[e->field]) {
	case SET_FIELD_HOUR:
		if (e->h24) {
			c->hour = (u8)wrap(c->hour, 0, 23, dir);
		} else {  // 1-12 within the half of the day
			u32 pm = c->hour >= 12;
			u32 h12 = wrap(c->hour % 12 ? c->hour % 12 : 12, 1, 12, dir);
			c->hour = (u8)(h12 % 12 + (pm ? 12 : 0));
		}
		break;
	case SET_FIELD_MINUTE:
		c->minute = (u8)wrap(c->minute, 0, 59, dir);
		break;
	case SET_FIELD_AMPM:
		c->hour = (u8)((c->hour + 12) % 24);
		break;
	case SET_FIELD_YEAR:
		c->year = (u16)wrap(c->year, 2000, 2099, dir);
		break;
	case SET_FIELD_MONTH:
		c->month = (u8)wrap(c->month, 1, 12, dir);
		break;
	default:  // SET_FIELD_DAY
		c->day = (u8)wrap(c->day, 1, settings_days_in(c->month, c->year), dir);
		break;
	}
	// A shorter month, or February out of a leap year, keeps a day it has
	u32 last = settings_days_in(c->month, c->year);
	if (c->day > last)
		c->day = (u8)last;
}

void settings_editor_text(const struct settings_editor *e, u32 pos, char *out, u32 size)
{
	const struct settings_clock *c = &e->clock;
	if (!size)
		return;
	out[0] = 0;
	if (pos >= e->fields)
		return;
	switch (e->field_ids[pos]) {
	case SET_FIELD_HOUR:
		if (e->h24)
			snprintf(out, size, "%02u", (unsigned)c->hour);
		else
			snprintf(out, size, "%u", (unsigned)(c->hour % 12 ? c->hour % 12 : 12));
		break;
	case SET_FIELD_MINUTE:
		snprintf(out, size, "%02u", (unsigned)c->minute);
		break;
	case SET_FIELD_AMPM:
		snprintf(out, size, "%s", gl_set_ampm[c->hour >= 12]);
		break;
	case SET_FIELD_YEAR:
		snprintf(out, size, "%04u", (unsigned)c->year);
		break;
	case SET_FIELD_MONTH:
		snprintf(out, size, "%02u", (unsigned)c->month);
		break;
	default:
		snprintf(out, size, "%02u", (unsigned)c->day);
		break;
	}
}

void settings_editor_apply(const struct settings_editor *e, struct settings_clock *now)
{
	if (e->kind == SET_DATE) {
		now->year = e->clock.year;
		now->month = e->clock.month;
		now->day = e->clock.day;
	} else {
		now->hour = e->clock.hour;
		now->minute = e->clock.minute;
		now->second = 0;
	}
}
