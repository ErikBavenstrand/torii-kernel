// The settings model's values on the cart (settings.h): read from the
// kernel NOR's settings slots and the RTC, staged, and written once.
#include <string.h>
#include <gba_base.h>

#include "Ezcard_OP.h"
#include "RTC.h"
#include "ez_define.h"
#include "ezkernel.h"
#include "settings.h"
#include "tlog.h"
#include "ui/ui.h"

extern u16 SET_info_buffer[0x200];

void settings_load(struct settings_values *v)
{
	for (u32 a = 0; a < assress_max; a++)
		v->slot[a] = v->loaded[a] = Read_SET_info(a);
	settings_read_clock(&v->clock);
}

void settings_stage(const struct settings_values *v)
{
	Stage_SET_info(v->slot);
}

u32 settings_commit(void)
{
	if (!Commit_SET_info(SET_info_buffer))
		return 0;
	// What the settings decide outside the page: the launches' switches and
	// the LED, the strings, DIM and LOGGING (the page shows THEME as it
	// changes, and the menu's next session reads it)
	CheckSwitch();
	CheckLanguage();
	ui_dim_set_minutes(ui_dim_setting());
	u32 level = tlog_level_setting();
	if (level != tlog_level) {
		// Logged at the higher of the two levels, so a change to off is too
		if (level > tlog_level)
			tlog_set_level(level);
		tlog("settings: LOGGING is %s", tlog_level_name(level));
		tlog_set_level(level);
	}
	return 1;
}

void settings_read_clock(struct settings_clock *c)
{
	u8 bcd[7];
	rtc_enable();
	rtc_get(bcd);
	rtc_disenable();
	settings_clock_from_rtc(c, bcd);
}

void settings_write_clock(const struct settings_clock *c)
{
	u8 dec[7];
	settings_clock_to_rtc(c, dec);
	rtc_enable();
	rtc_set(dec);
	rtc_disenable();
	delay(0x200);  // as the official kernel waits after setting it
	tlog("settings: clock set to %04u-%02u-%02u %02u:%02u:%02u", (unsigned)c->year, (unsigned)c->month,
	     (unsigned)c->day, (unsigned)c->hour, (unsigned)c->minute, (unsigned)c->second);
}

void settings_write_edit(const struct settings_editor *e)
{
	struct settings_clock now;
	settings_read_clock(&now);
	settings_editor_apply(e, &now);
	settings_write_clock(&now);
}

void settings_log(void)
{
	struct settings_values v;
	char view[16], start[16], h24[8], all[8];
	u16 slots[assress_max];
	for (u32 a = 0; a < assress_max; a++)
		slots[a] = Read_SET_info(a);
	settings_values_init(&v, slots);  // no clock: nothing here shows it
	settings_value_text(&v, SET_GAME_VIEW, view, sizeof(view));
	settings_value_text(&v, SET_START_ON, start, sizeof(start));
	settings_value_text(&v, SET_CLOCK_24H, h24, sizeof(h24));
	settings_value_text(&v, SET_SHOW_ALL, all, sizeof(all));
	tlog_debug("settings: 24-hour clock %s, game view %s, start on %s, show all files %s", h24, view, start, all);
}
