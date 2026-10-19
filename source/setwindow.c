// SETTINGS page 1 (clock, addons, language, patch engine, hot keys, cart
// hardware), and the runner the settings pages share (settings.h).
#include <gba_systemcalls.h>
#include <gba_input.h>
#include <gba_video.h>
#include <stdio.h>
#include <string.h>
#include <gba_base.h>

#include "ez_define.h"
#include "lang.h"
#include "ezkernel.h"
#include "RTC.h"
#include "Ezcard_OP.h"
#include "ui/dialog.h"
#include "settings.h"
#include "tlog.h"
#include "engine/engine.h"

extern u16 gl_engine_sel;
extern u16 gl_show_Thumbnail;
extern u16 gl_game_clock;
extern u16 gl_game_rumble;

static void save_setw_info(void);

u16 SET_info_buffer[0x200] EWRAM_BSS;

// Hot keys, as the settings store them
#define K_A (0)
#define K_B (1)
#define K_SELECT (2)
#define K_START (3)
#define K_R (8)
#define K_L (9)
#define K_COUNT 10
static const char *const key_names[K_COUNT] = {"A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN", "R", "L"};
static const u8 sleep_default[3] = {K_L, K_R, K_SELECT};
static const u8 menu_default[3] = {K_L, K_R, K_START};

static u16 v_reset EWRAM_BSS;
static u16 v_rts EWRAM_BSS;
static u16 v_sleep EWRAM_BSS;
static u16 v_cheat EWRAM_BSS;
static u16 language_sel EWRAM_BSS;
static u16 engine_sel EWRAM_BSS;
static u8 edit_sleephotkey[3] EWRAM_BSS;
static u8 edit_rtshotkey[3] EWRAM_BSS;

// Page 1's rows (select): the official kernel's order
enum {
	ROW_TIME,
	ROW_ADDON,
	ROW_LANGUAGE,
	ROW_ENGINE,
	ROW_SLEEP_KEY,
	ROW_MENU_KEY,
	ROW_CART,
	ROWS1
};

// The official kernel's edit state, by the same names
static struct {
	u8 select;
	u8 set_ok;  // edit mode
	u8 edit_pos;
	u8 addon_sel;
	u8 sleep_pos;
	u8 rtshotkey_pos;
	u8 engine_pos;
	u8 rtc_pos;
	u8 edit_datetime[7];
	u8 datetime[7];  // the clock as last read
	u8 sleep_keys[3], menu_keys[3];  // as saved
	u32 polled;      // frame of the last clock read
} p1 EWRAM_BSS;

// --- The runner ---------------------------------------------------------------

static u32 settings_result EWRAM_BSS;
static u32 row_count EWRAM_BSS;
static u32 row_hash[SETTINGS_ROWS_MAX] EWRAM_BSS;
static char row_value[64] EWRAM_BSS;

const struct ui_label_config settings_title_config = {"", UI_FONT_HEADING, UI_ALIGN_LEFT, 0};
const struct ui_label_config settings_corner_config = {"", UI_FONT_BODY, UI_ALIGN_RIGHT, 0};

static const struct settings_page *const pages[] = {&settings_page1, &settings_page2, &settings_help};

static u32 model_count(void *ctx)
{
	const struct settings_page *p = ctx;
	u32 n = p->count();
	return n < SETTINGS_ROWS_MAX ? n : SETTINGS_ROWS_MAX;
}

// The list draws a row straight after fetching it, so one buffer serves
static int model_fetch(void *ctx, u32 index, struct ui_item *out)
{
	const struct settings_page *p = ctx;
	const char *name = "";
	row_value[0] = 0;
	p->row(index, &name, row_value, sizeof(row_value));
	out->text = name;
	out->detail = row_value;
	return UI_ITEM_READY;
}

const struct ui_list_model settings_model = {model_count, model_fetch, 0};

// FNV-1a of a row's name (by address) and value
static u32 row_digest(const struct settings_page *p, u32 row)
{
	const char *name = "";
	row_value[0] = 0;
	p->row(row, &name, row_value, sizeof(row_value));
	u32 h = 2166136261u ^ (u32)name;
	for (const char *s = row_value; *s; s++)
		h = (h ^ (u8)*s) * 16777619u;
	return h;
}

static void digest_all(const struct settings_page *p)
{
	row_count = model_count((void *)p);
	for (u32 i = 0; i < row_count; i++)
		row_hash[i] = row_digest(p, i);
}

void settings_sync(void)
{
	struct ui_view *v = ui_top();
	const struct settings_page *p = v ? v->ctx : 0;
	if (!p || !p->count)
		return;
	u8 list = (u8)(v->first + SETTINGS_LIST);
	if (model_count((void *)p) != row_count) {
		// Rows came or went (LED's): the list redraws as a new page
		digest_all(p);
		ui_list_reset(list, p->cursor());
		return;
	}
	for (u32 i = 0; i < row_count; i++) {
		u32 h = row_digest(p, i);
		if (h != row_hash[i]) {
			row_hash[i] = h;
			ui_list_refresh(list, i, i);
		}
	}
	if (ui_list_cursor(list) != p->cursor())
		ui_list_set_cursor(list, p->cursor());
}

u16 settings_keys(const struct ui_event *e)
{
	return e->keys & (u16)(~INPUT_PAGE | ui_input()->pressed);
}

void settings_header(struct ui_view *view, const char *title, const char *corner)
{
	ui_label_set((u8)(view->first + 1), title);
	ui_label_set((u8)(view->first + 2), corner);
}

static void open_page(u32 page)
{
	const struct settings_page *p = pages[page];
	struct ui_view *v = ui_push(p->view, (void *)p);
	if (!v) {
		tlog_error("settings: %s", ui_error());
		return;  // nothing on the stack: Settings_run returns
	}
	p->enter(v);
	if (p->count) {
		digest_all(p);
		ui_list_reset((u8)(v->first + SETTINGS_LIST), p->cursor());
	}
}

void settings_open(u32 page)
{
	ui_pop();
	open_page(page);
}

void settings_leave(u32 result)
{
	settings_result = result;
	ui_pop();
}

u32 Settings_run(u32 page)
{
	settings_result = SETTINGS_TO_NOR;
	engine_init();
	ui_init(NULL);
	open_page(page);
	while (ui_depth()) {
		ui_frame();
		const struct ui_view *v = ui_top();
		const struct settings_page *p = v ? v->ctx : 0;
		if (p && p->poll)
			p->poll();
	}
	ui_shutdown();
	dialog_blank();
	return settings_result;
}

// --- Value helpers --------------------------------------------------------------

const char *settings_name(const char *name)
{
	while (*name == ' ')
		name++;
	return name;
}

void settings_token(char *value, u32 size, const char *text, int state)
{
	if (state < 0)
		snprintf(value, size, "\xC2\xAB %s \xC2\xBB", text);
	else
		snprintf(value, size, "\xC2\xAB %s: %s \xC2\xBB", text, state ? gl_on : gl_off);
}

void settings_add(char *value, u32 size, const char *sep, const char *text)
{
	u32 len = strlen(value);
	if (len < size)
		snprintf(value + len, size - len, "%s%s", len ? sep : "", text);
}

void settings_none(char *value, u32 size)
{
	if (!value[0])
		snprintf(value, size, "%s", gl_off);
}

// --- Page 1 ------------------------------------------------------------------------

static const char *weekday(u32 day)
{
	switch (day) {
	case 1:
		return gl_Mon;
	case 2:
		return gl_Tues;
	case 3:
		return gl_Wed;
	case 4:
		return gl_Thur;
	case 5:
		return gl_Fri;
	case 6:
		return gl_Sat;
	default:
		return gl_Sun;
	}
}

// A hot key as stored, the default for anything that isn't a key
static u8 hot_key(u32 slot, u8 def)
{
	u16 v = Read_SET_info(slot);
	return v < K_COUNT ? (u8)v : def;
}

static void read_hot_keys(void)
{
	for (u32 i = 0; i < 3; i++) {
		p1.sleep_keys[i] = hot_key(assress_edit_sleephotkey_0 + i, sleep_default[i]);
		p1.menu_keys[i] = hot_key(assress_edit_rtshotkey_0 + i, menu_default[i]);
	}
}

static void read_clock(void)
{
	rtc_enable();
	rtc_get(p1.datetime);
	rtc_disenable();
	p1.polled = frame_count();
}

// The clock's fields as the official kernel shows them
static void clock_fields(u8 *year, u8 *month, u8 *day, u8 *hh, u8 *mm, u8 *ss, u8 *wkd)
{
	const u8 *d = p1.datetime;
	*hh = UNBCD(d[_HOUR] & 0x3F);
	*mm = UNBCD(d[_MIN] & 0x7F);
	*ss = UNBCD(d[_SEC] & 0x7F);
	if (*hh > 23)
		*hh = 0;
	if (*mm > 59)
		*mm = 0;
	if (*ss > 59)
		*ss = 0;
	*month = UNBCD(d[_MONTH] & 0x1F);
	*day = UNBCD(d[_DAY] & 0x3F);
	if (*month == 0)
		*month = 1;
	if (*day == 0)
		*day = 1;
	*year = UNBCD(d[_YEAR]);
	*wkd = UNBCD(d[_WKD] & 0x7);
}

static void page1_enter(struct ui_view *view)
{
	memset(&p1, 0, sizeof(p1));
	p1.addon_sel = 2;
	p1.engine_pos = 1;
	p1.rtc_pos = 2;
	language_sel = gl_lang;
	v_reset = Read_SET_info(assress_v_reset);
	v_rts = Read_SET_info(assress_v_rts);
	v_sleep = Read_SET_info(assress_v_sleep);
	v_cheat = Read_SET_info(assress_v_cheat);
	if ((v_reset != 0x0) && (v_reset != 0x1))
		v_reset = 0x0;
	if ((v_rts != 0x0) && (v_rts != 0x1))
		v_rts = 0x0;
	if ((v_sleep != 0x0) && (v_sleep != 0x1))
		v_sleep = 0x0;
	if ((v_cheat != 0x0) && (v_cheat != 0x1))
		v_cheat = 0x0;
	engine_sel = gl_engine_sel;
	read_hot_keys();
	read_clock();
	settings_header(view, gl_settings_title, "1 / 2");
}

static u32 page1_count(void)
{
	return ROWS1;
}

static u32 page1_cursor(void)
{
	return p1.select;
}

// Appends field to value, in brackets if it is under the edit cursor
static void field(char *value, u32 size, const char *sep, const char *text, int marked)
{
	char buf[16];
	snprintf(buf, sizeof(buf), marked ? "[%s]" : "%s", text);
	settings_add(value, size, sep, buf);
}

static void clock_value(char *value, u32 size)
{
	int edit = p1.set_ok && p1.select == ROW_TIME;
	u8 year, month, day, hh, mm, ss, wkd;
	clock_fields(&year, &month, &day, &hh, &mm, &ss, &wkd);
	if (edit) {
		const u8 *e = p1.edit_datetime;
		year = e[_YEAR];
		month = e[_MONTH];
		day = e[_DAY];
		hh = e[_HOUR];
		mm = e[_MIN];
		ss = e[_SEC];
		wkd = e[_WKD];
	}
	char f[7][8];
	snprintf(f[0], sizeof(f[0]), "20%02u", year % 100u);
	snprintf(f[1], sizeof(f[1]), "%02u", month);
	snprintf(f[2], sizeof(f[2]), "%02u", day);
	snprintf(f[3], sizeof(f[3]), "%02u", hh);
	snprintf(f[4], sizeof(f[4]), "%02u", mm);
	snprintf(f[5], sizeof(f[5]), "%02u", ss);
	snprintf(f[6], sizeof(f[6]), "%s", weekday(wkd));
	static const char *const seps[7] = {"", "/", "/", " ", ":", ":", " "};
	for (u32 i = 0; i < 7; i++)
		field(value, size, seps[i], f[i], edit && p1.edit_pos == i);
	if (edit)
		field(value, size, "  ", settings_name(gl_ok_btn), p1.edit_pos == 7);
}

static void keys_value(char *value, u32 size, const u8 *keys, int edit, u32 pos)
{
	for (u32 i = 0; i < 3; i++)
		field(value, size, " + ", key_names[keys[i] < K_COUNT ? keys[i] : 0], edit && pos == i);
	if (edit)
		field(value, size, "  ", settings_name(gl_ok_btn), pos == 3);
}

static void page1_row(u32 row, const char **name, char *value, u32 size)
{
	int edit = p1.set_ok && p1.select == row;
	const char *ok = settings_name(gl_ok_btn);
	// SAVESTATE alone turns the hot keys into save and load keys
	int save_keys = (v_rts == 1) && (v_cheat == 0) && (v_reset == 0) && (v_sleep == 0);
	switch (row) {
	case ROW_TIME:
		*name = settings_name(gl_time);
		clock_value(value, size);
		break;
	case ROW_ADDON: {
		*name = settings_name(gl_addon);
		// addon_sel: RESET, SAVESTATE, OK, SLEEP, CHEAT
		const char *names[5] = {gl_reset, gl_rts, ok, gl_sleep, gl_cheat};
		const u16 on[5] = {v_reset, v_rts, 0, v_sleep, v_cheat};
		if (edit && p1.addon_sel < 5) {
			settings_token(value, size, names[p1.addon_sel],
				       p1.addon_sel == 2 ? -1 : on[p1.addon_sel] == 1);
		} else {
			for (u32 i = 0; i < 5; i++)
				if (i != 2 && on[i] == 1)
					settings_add(value, size, "  ", names[i]);
			settings_none(value, size);
		}
		break;
	}
	case ROW_LANGUAGE:
		*name = settings_name(gl_language);
		if (edit)
			settings_token(value, size, lang_packs[language_sel].name, -1);
		else
			settings_add(value, size, "", lang_packs[language_sel].name);
		break;
	case ROW_ENGINE:
		*name = settings_name(gl_engine);
		if (edit)
			settings_token(value, size, p1.engine_pos ? ok : gl_use_engine,
				       p1.engine_pos ? -1 : engine_sel == 1);
		else
			settings_add(value, size, "", engine_sel == 1 ? gl_on : gl_off);
		break;
	case ROW_SLEEP_KEY:
		*name = save_keys ? gl_save_key : settings_name(gl_hot_key);
		keys_value(value, size, edit ? edit_sleephotkey : p1.sleep_keys, edit, p1.sleep_pos);
		break;
	case ROW_MENU_KEY:
		*name = save_keys ? gl_load_key : settings_name(gl_hot_key2);
		keys_value(value, size, edit ? edit_rtshotkey : p1.menu_keys, edit, p1.rtshotkey_pos);
		break;
	default: {  // ROW_CART
		*name = settings_name(gl_cart_str);
		// rtc_pos: CLOCK, RUMBLE, OK
		const char *names[3] = {gl_clock_str, gl_rumble_str, ok};
		const u16 on[3] = {gl_game_clock, gl_game_rumble, 0};
		if (edit && p1.rtc_pos < 3) {
			settings_token(value, size, names[p1.rtc_pos], p1.rtc_pos == 2 ? -1 : on[p1.rtc_pos] == 1);
		} else {
			for (u32 i = 0; i < 2; i++)
				if (on[i] == 1)
					settings_add(value, size, "  ", names[i]);
			settings_none(value, size);
		}
		break;
	}
	}
}

static u8 days_in(u32 month, u32 year)
{
	static const u8 days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	if (month == 2 && year % 4 == 0)
		return 29;
	return month >= 1 && month <= 12 ? days[month - 1] : 31;
}

// UP (dir 1) or DOWN (-1) on the clock field under the edit cursor
static void clock_step(int dir)
{
	u8 *e = p1.edit_datetime;
	static const u8 index[7] = {_YEAR, _MONTH, _DAY, _HOUR, _MIN, _SEC, _WKD};
	static const u8 lo[7] = {0, 1, 1, 0, 0, 0, 0};
	u8 hi[7] = {99, 12, days_in(e[_MONTH], e[_YEAR]), 23, 59, 59, 6};
	u32 pos = p1.edit_pos;
	if (pos > 6)
		return;
	u8 *v = &e[index[pos]];
	if (dir > 0)
		*v = *v >= hi[pos] ? lo[pos] : *v + 1;
	else
		*v = *v <= lo[pos] ? hi[pos] : *v - 1;
	// A shorter month (or February out of a leap year) keeps a day it has
	u8 last = days_in(e[_MONTH], e[_YEAR]);
	if (e[_DAY] > last)
		e[_DAY] = last;
}

// UP (dir 1) or DOWN (-1) on hot key pos: the next key the other two don't use
static void key_step(u8 *keys, u32 pos, int dir)
{
	if (pos > 2)
		return;
	u8 k = keys[pos];
	for (u32 tries = 0; tries < K_COUNT; tries++) {
		k = (u8)((k + K_COUNT + dir) % K_COUNT);
		if (k != keys[(pos + 1) % 3] && k != keys[(pos + 2) % 3])
			break;
	}
	keys[pos] = k;
}

// A in normal mode: edit the selected row (the official kernel's set-up)
static void page1_start_edit(void)
{
	u8 year, month, day, hh, mm, ss, wkd;
	read_clock();
	clock_fields(&year, &month, &day, &hh, &mm, &ss, &wkd);
	p1.set_ok = 1;
	u8 *e = p1.edit_datetime;
	e[_HOUR] = hh;
	e[_MIN] = mm;
	e[_SEC] = ss;
	e[_DAY] = day;
	e[_MONTH] = month;
	e[_YEAR] = year;
	e[_WKD] = wkd;
	if (p1.select == ROW_ADDON)
		p1.addon_sel = 0;
	else if (p1.select == ROW_ENGINE)
		p1.engine_pos = 0;
	else if (p1.select == ROW_CART)
		p1.rtc_pos = 0;
	read_hot_keys();
	memcpy(edit_sleephotkey, p1.sleep_keys, 3);
	memcpy(edit_rtshotkey, p1.menu_keys, 3);
}

static void page1_edit_keys(u16 keys)
{
	u32 select = p1.select;
	if (keys & (INPUT_UP | INPUT_DOWN)) {
		int dir = (keys & INPUT_UP) ? 1 : -1;
		if (select == ROW_TIME) {
			clock_step(dir);
		} else if (select == ROW_ADDON) {
			if (dir > 0 && p1.addon_sel > 2)
				p1.addon_sel -= 3;
			else if (dir < 0 && p1.addon_sel < 2)
				p1.addon_sel += 3;
		} else if (select == ROW_SLEEP_KEY) {
			key_step(edit_sleephotkey, p1.sleep_pos, dir);
		} else if (select == ROW_MENU_KEY) {
			key_step(edit_rtshotkey, p1.rtshotkey_pos, dir);
		}
	} else if (keys & INPUT_RIGHT) {
		if (select == ROW_TIME) {
			p1.edit_pos = p1.edit_pos == 7 ? 0 : p1.edit_pos + 1;
		} else if (select == ROW_ADDON) {
			if (p1.addon_sel == 3 || p1.addon_sel < 2)
				p1.addon_sel++;
		} else if (select == ROW_LANGUAGE) {
			if ((language_sel + 1u < lang_pack_count) && (language_sel < 1))
				language_sel++;
		} else if (select == ROW_ENGINE) {
			p1.engine_pos = 1;
		} else if (select == ROW_SLEEP_KEY) {
			if (p1.sleep_pos < 3)
				p1.sleep_pos++;
		} else if (select == ROW_MENU_KEY) {
			if (p1.rtshotkey_pos < 3)
				p1.rtshotkey_pos++;
		} else if (select == ROW_CART) {
			if (p1.rtc_pos < 2)
				p1.rtc_pos++;
		}
	} else if (keys & INPUT_LEFT) {
		if (select == ROW_TIME) {
			p1.edit_pos = p1.edit_pos == 0 ? 7 : p1.edit_pos - 1;
		} else if (select == ROW_ADDON) {
			if (p1.addon_sel == 4 || (p1.addon_sel > 0 && p1.addon_sel < 3))
				p1.addon_sel--;
		} else if (select == ROW_LANGUAGE) {
			if (language_sel)
				language_sel--;
		} else if (select == ROW_ENGINE) {
			p1.engine_pos = 0;
		} else if (select == ROW_SLEEP_KEY) {
			if (p1.sleep_pos)
				p1.sleep_pos--;
		} else if (select == ROW_MENU_KEY) {
			if (p1.rtshotkey_pos)
				p1.rtshotkey_pos--;
		} else if (select == ROW_CART) {
			if (p1.rtc_pos)
				p1.rtc_pos--;
		}
	} else if (keys & INPUT_A) {
		if (select == ROW_TIME && p1.edit_pos == 7) {
			rtc_enable();
			rtc_set(p1.edit_datetime);
			rtc_disenable();
			delay(0x200);
			p1.set_ok = 0;
			read_clock();
		} else if (select == ROW_ADDON) {
			switch (p1.addon_sel) {
			case 0:
				v_reset = !v_reset;
				break;
			case 1:
				v_rts = !v_rts;
				break;
			case 3:
				v_sleep = !v_sleep;
				break;
			case 4:
				v_cheat = !v_cheat;
				break;
			default:  // OK
				save_setw_info();
				CheckSwitch();  //read again
				p1.set_ok = 0;
				break;
			}
		} else if (select == ROW_LANGUAGE) {
			save_setw_info();
			CheckLanguage();  //read again
			p1.set_ok = 0;
		} else if (select == ROW_ENGINE) {
			if (p1.engine_pos == 0) {
				engine_sel = !engine_sel;
			} else {
				save_setw_info();
				p1.set_ok = 0;
				gl_engine_sel = Read_SET_info(assress_engine_sel);
				if ((gl_engine_sel != 0x0) && (gl_engine_sel != 0x1))
					gl_engine_sel = 0x1;
			}
		} else if ((select == ROW_SLEEP_KEY && p1.sleep_pos == 3) ||
			   (select == ROW_MENU_KEY && p1.rtshotkey_pos == 3)) {
			save_setw_info();
			p1.set_ok = 0;
		} else if (select == ROW_CART) {
			switch (p1.rtc_pos) {
			case 0:
				gl_game_clock = !gl_game_clock;
				break;
			case 1:
				gl_game_rumble = !gl_game_rumble;
				break;
			default:  // OK
				save_setw_info();
				p1.set_ok = 0;
				gl_game_clock = Read_SET_info(assress_game_clock);
				if ((gl_game_clock != 0x0) && (gl_game_clock != 0x1))
					gl_game_clock = 0x1;
				gl_game_rumble = Read_SET_info(assress_game_rumble);
				if ((gl_game_rumble != 0x0) && (gl_game_rumble != 0x1))
					gl_game_rumble = 0x1;
				break;
			}
		}
		if (!p1.set_ok)
			read_hot_keys();
	}
}

static int page1_event(struct ui_view *view, const struct ui_event *e)
{
	(void)view;
	if (e->type != UI_EV_KEYS)
		return 0;
	u16 keys = settings_keys(e);
	if (p1.set_ok) {
		page1_edit_keys(keys);
	} else if (keys & INPUT_A) {
		page1_start_edit();
	} else if (keys & INPUT_DOWN) {
		if (p1.select < ROWS1 - 1)
			p1.select++;
	} else if (keys & INPUT_UP) {
		if (p1.select)
			p1.select--;
	} else if (keys & INPUT_L) {
		settings_leave(SETTINGS_TO_NOR);
		return 1;
	} else if (keys & INPUT_R) {
		settings_open(SETTINGS_PAGE2);
		return 1;
	} else {
		return 0;
	}
	settings_sync();
	return 1;
}

// The clock row follows the cart's clock, outside its edit mode
static void page1_poll(void)
{
	if (p1.set_ok && p1.select == ROW_TIME)
		return;
	if (frame_count() - p1.polled < 8)
		return;
	u8 old[7];
	memcpy(old, p1.datetime, sizeof(old));
	read_clock();
	if (memcmp(old, p1.datetime, sizeof(old)) != 0)
		settings_sync();
}

static const struct ui_list_config page1_list = {&settings_model, SETTINGS_ROW_H, 0, 0, 240 - 2 * 6 - SETTINGS_NAME_W};

static const struct ui_spec page1_nodes[] = {
	SETTINGS_HEADER,
	{.kind = UI_LIST,
	 .id = 4,
	 .parent = UI_NONE,
	 .plane = 1,
	 .anchor = UI_HFILL,
	 .paper = UI_ROLE_BG,
	 .y = 16,
	 .h = 144,
	 .data = &page1_list},
};

static const struct ui_view_spec page1_view = {
	"settings1", page1_nodes, sizeof(page1_nodes) / sizeof(page1_nodes[0]), 20, UI_NONE, 0, page1_event};

const struct settings_page settings_page1 = {&page1_view, page1_enter,  page1_count,
					     page1_row,   page1_cursor, page1_poll};

//---------------------------------------------------------------------------------
static void save_setw_info(void)
{
	u32 address;
	SET_info_buffer[assress_language] = lang_packs[language_sel].id;
	SET_info_buffer[assress_v_reset] = v_reset;
	SET_info_buffer[assress_v_rts] = v_rts;
	SET_info_buffer[assress_v_sleep] = v_sleep;
	SET_info_buffer[assress_v_cheat] = v_cheat;

	SET_info_buffer[assress_edit_sleephotkey_0] = edit_sleephotkey[0];
	SET_info_buffer[assress_edit_sleephotkey_1] = edit_sleephotkey[1];
	SET_info_buffer[assress_edit_sleephotkey_2] = edit_sleephotkey[2];
	SET_info_buffer[assress_edit_rtshotkey_0] = edit_rtshotkey[0];
	SET_info_buffer[assress_edit_rtshotkey_1] = edit_rtshotkey[1];
	SET_info_buffer[assress_edit_rtshotkey_2] = edit_rtshotkey[2];

	SET_info_buffer[assress_engine_sel] = engine_sel;

	SET_info_buffer[assress_show_Thumbnail] = gl_show_Thumbnail;

	SET_info_buffer[assress_game_clock] = gl_game_clock;

	for (address = 14; address < assress_max; address++) {
		SET_info_buffer[address] = Read_SET_info(address);
	}
	SET_info_buffer[assress_game_rumble] = gl_game_rumble;
	//save to nor
	Save_SET_info(SET_info_buffer, 0x200);
}
