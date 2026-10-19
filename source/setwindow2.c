// SETTINGS page 2: auto save, mode B, the LED, the menu's look (THEME,
// ACCENT, DIM), LOGGING and DIAGNOSE (settings.h).
#include <stdio.h>
#include <string.h>
#include <gba_base.h>

#include "ez_define.h"
#include "lang.h"
#include "ezkernel.h"
#include "Ezcard_OP.h"
#include "settings.h"
#include "tlog.h"
#include "ui/sdtheme.h"
#include "ui/theme_ini.h"

static u16 auto_save_sel EWRAM_BSS;
static u16 ModeB_INIT EWRAM_BSS;
static u16 led_open_sel EWRAM_BSS;

static u16 Breathing_R EWRAM_BSS;
static u16 Breathing_G EWRAM_BSS;
static u16 Breathing_B EWRAM_BSS;

static u16 SD_R EWRAM_BSS;
static u16 SD_G EWRAM_BSS;
static u16 SD_B EWRAM_BSS;

static u16 log_sel EWRAM_BSS;     //Torii: LOGGING, TLOG_OFF to TLOG_DEBUG
static u16 theme_sel EWRAM_BSS;   //Torii: THEME option: 0 dark, 1 light, 2 + i themes.names[i] on SD
static u16 accent_sel EWRAM_BSS;  //Torii: ACCENT, 0 the theme's own, else 1 + an index into ui_accents
static u16 dim_sel EWRAM_BSS;     //Torii: DIM, idle minutes, 0 off

extern u16 gl_auto_save_sel;
extern u16 gl_ModeB_init;

extern u16 gl_led_open_sel;
extern u16 gl_Breathing_R;
extern u16 gl_Breathing_G;
extern u16 gl_Breathing_B;

extern u16 gl_SD_R;
extern u16 gl_SD_G;
extern u16 gl_SD_B;

static void save_set2_info(void);
extern void IWRAM_CODE Set_LED_control(u16 status);

// Page 2's settings (select). BREATHING and SD LED are there while the LED
// is on. Torii: they are rows of their own, which UP and DOWN reach; the
// official kernel reached them only from LED's edit mode (DOWN there), so
// the cursor seemed to jump over them. Same settings, saved the same way.
enum {
	SEL_SAVE,
	SEL_MODEB,
	SEL_LED,
	SEL_BREATHING,
	SEL_SD_LED,
	SEL_THEME,
	SEL_ACCENT,
	SEL_DIM,
	SEL_LOGGING,
	SEL_DIAGNOSE,
	SELECTS2
};

// The official kernel's edit state, by the same names. extra_pos is THEME,
// ACCENT and DIM's: 0 the value, 1 OK. led_pos is LED's (0 on or off, 1
// OK), rgb_pos BREATHING's and SD LED's (0-2 R, G and B, 3 OK).
static struct {
	u8 select;
	u8 set_ok;
	u8 auto_save_pos;
	u8 led_pos;
	u8 rgb_pos;
	u8 ModeB_pos;
	u8 log_pos;
	u8 extra_pos;
} p2 EWRAM_BSS;

// DIM steps A cycles through (UP and DOWN step a minute)
static const u8 dim_presets[] = {0, 1, 2, 5, 10, 15, 30, 60};

// The settings' accent, as accent_sel counts
static u16 accent_setting(void)
{
	u16 a = Read_SET_info(assress_accent);
	return a >= 1 && a <= ui_accent_count ? a : 0;
}

// THEME's options: DARK, LIGHT, then the SD themes in /torii/themes
#define THEME_SD_CAP 8
static struct {
	char names[THEME_SD_CAP][THEME_NAME_MAX];
	u8 count;
	u8 initial;  // the option the setting picked as the page opened (THEME_SD_GONE: none)
	u8 touched;  // THEME was changed on this page: save it
	char note[96];  // why a theme didn't load or save, shown in the header until a key
} themes EWRAM_BSS;
// The initial option when THEME is 2 but no SD theme loaded: Dark shows, and
// the stored 2 stays until THEME is changed
#define THEME_SD_GONE 0xFF

// The THEME setting, raw: 1 light, UI_THEME_SD (2) the SD theme, else dark
static u16 theme_setting(void)
{
	u16 t = Read_SET_info(assress_theme);
	return t == 1 || t == UI_THEME_SD ? t : 0;
}

// The option the setting picks; THEME_SD_GONE for an SD theme that isn't
// loaded or listed
static u8 theme_option(void)
{
	u16 t = theme_setting();
	if (t != UI_THEME_SD)
		return (u8)t;
	if (ui_theme_sd())
		for (u32 i = 0; i < themes.count; i++)
			if (!strcmp(themes.names[i], ui_theme_sd_folder()))
				return (u8)(2 + i);
	return THEME_SD_GONE;
}

// Shows why a theme didn't load or save in the header, until the next key
static void theme_note(const char *a, const char *b, const char *c)
{
	snprintf(themes.note, sizeof(themes.note), "%s%s%s", a, b, c);
	struct ui_view *v = ui_top();
	if (v)
		ui_label_set((u8)(v->first + 1), themes.note);
}

static const struct ui_theme *theme_of(u32 option)
{
	if (option >= 2 && ui_theme_sd())
		return ui_theme_sd();
	return option == 1 ? &ui_theme_light : &ui_theme_dark;
}

static const char *theme_name(u32 option)
{
	if (option < 2)
		return gl_theme_names[option];
	const struct ui_theme *t = ui_theme_sd();
	// A theme names itself once loaded (theme.ini's name); until then, its folder
	if (t && !strcmp(ui_theme_sd_folder(), themes.names[option - 2]) && t->name && t->name[0])
		return t->name;
	return themes.names[option - 2];
}

// Shows theme_sel and accent_sel at once: a palette change
static void preview_look(void)
{
	ui_theme_set(theme_of(theme_sel), accent_sel ? ui_accents[accent_sel - 1].colour : UI_ACCENT_THEME);
}

// 1 if option can show: a built-in, or an SD theme that is (or now gets) loaded
static int theme_ready(u32 option)
{
	if (option < 2)
		return 1;
	const char *name = themes.names[option - 2];
	if (!strcmp(ui_theme_sd_folder(), name) && ui_theme_sd())
		return 1;
	if (ui_theme_load(name))
		return 1;
	tlog_error("settings: theme %s: %s", name, ui_theme_error());
	theme_note(name, ": ", ui_theme_error());
	return 0;
}

// The next (dir >= 0) or previous THEME option; an SD theme is loaded as it
// is picked, and one that doesn't load is passed over
static void theme_step(int dir)
{
	u32 n = 2u + themes.count;
	for (u32 tries = 0; tries < n; tries++) {
		theme_sel = (u16)((theme_sel + (dir < 0 ? n - 1 : 1)) % n);
		if (theme_ready(theme_sel))
			break;
	}
	themes.touched = 1;
	preview_look();
}

static void page2_enter(struct ui_view *view)
{
	memset(&p2, 0, sizeof(p2));
	p2.auto_save_pos = 1;
	p2.led_pos = 1;
	p2.ModeB_pos = 3;
	p2.log_pos = 1;
	CheckSwitch();

	auto_save_sel = gl_auto_save_sel;
	ModeB_INIT = gl_ModeB_init;

	led_open_sel = gl_led_open_sel;
	Breathing_R = gl_Breathing_R;
	Breathing_G = gl_Breathing_G;
	Breathing_B = gl_Breathing_B;
	SD_R = gl_SD_R;
	SD_G = gl_SD_G;
	SD_B = gl_SD_B;
	log_sel = tlog_level_setting();
	themes.note[0] = 0;
	themes.count = (u8)ui_theme_list(themes.names, THEME_SD_CAP);
	themes.touched = 0;
	themes.initial = theme_option();
	theme_sel = themes.initial == THEME_SD_GONE ? 0 : themes.initial;
	accent_sel = accent_setting();
	dim_sel = (u16)ui_dim_setting();
	settings_header(view, gl_settings_title, "2 / 2");
}

// BREATHING and SD LED are there while the LED is on
static int led_rows(void)
{
	return led_open_sel == 0x1;
}

static int is_rgb(u32 select)
{
	return select == SEL_BREATHING || select == SEL_SD_LED;
}

// Rows on screen
static u32 page2_count(void)
{
	return SELECTS2 - (led_rows() ? 0u : 2u);
}

// The screen row of setting select, and back
static u32 row_of(u32 select)
{
	return select > SEL_LED && !led_rows() ? select - 2 : select;
}

static u32 select_of(u32 row)
{
	return row > SEL_LED && !led_rows() ? row + 2 : row;
}

static u32 page2_cursor(void)
{
	return row_of(p2.select);
}

static void accent_name(char *value, u32 size, u32 accent)
{
	if (!accent) {
		snprintf(value, size, "%s", gl_accent_own);
		return;
	}
	snprintf(value, size, "%s", ui_accents[accent - 1].name);
	for (char *c = value; *c; c++)
		if (*c >= 'a' && *c <= 'z')
			*c = (char)(*c - 'a' + 'A');
}

static void dim_name(char *value, u32 size, u32 minutes)
{
	if (minutes)
		snprintf(value, size, "%lu %s", (unsigned long)minutes, gl_minutes);
	else
		snprintf(value, size, "%s", gl_off);
}

// R, G and B of the BREATHING or SD LED row: the ones on, or in edit mode
// a token for rgb_pos
static void rgb_value(char *value, u32 size, const u16 *rgb[3], u32 select)
{
	static const char *const names[3] = {"R", "G", "B"};
	if (p2.set_ok && p2.select == select) {
		u32 i = p2.rgb_pos;
		if (i < 3)
			settings_token(value, size, names[i], *rgb[i] == 1);
		else
			settings_token(value, size, settings_name(gl_ok_btn), -1);
		return;
	}
	for (u32 i = 0; i < 3; i++)
		if (*rgb[i] == 1)
			settings_add(value, size, " ", names[i]);
	settings_none(value, size);
}

// A THEME, ACCENT or DIM value, or LOGGING's: a token in edit mode
static void cycle_value(char *value, u32 size, u32 select, const char *text, u32 pos)
{
	if (p2.set_ok && p2.select == select)
		settings_token(value, size, pos ? settings_name(gl_ok_btn) : text, -1);
	else
		settings_add(value, size, "", text);
}

static void page2_row(u32 row, const char **name, char *value, u32 size)
{
	char buf[24];
	const char *ok = settings_name(gl_ok_btn);
	u32 select = select_of(row);
	if (select == SEL_BREATHING) {
		*name = settings_name(gl_Breathing_light);
		const u16 *rgb[3] = {&Breathing_R, &Breathing_G, &Breathing_B};
		rgb_value(value, size, rgb, select);
		return;
	}
	if (select == SEL_SD_LED) {
		*name = settings_name(gl_SD_working);
		const u16 *rgb[3] = {&SD_R, &SD_G, &SD_B};
		rgb_value(value, size, rgb, select);
		return;
	}
	int edit = p2.set_ok && p2.select == select;
	switch (select) {
	case SEL_SAVE:
		*name = settings_name(gl_save);
		if (edit)
			settings_token(value, size, p2.auto_save_pos ? ok : gl_auto_save,
				       p2.auto_save_pos ? -1 : auto_save_sel == 0x1);
		else
			settings_add(value, size, "", auto_save_sel == 0x1 ? gl_on : gl_off);
		break;
	case SEL_MODEB: {
		*name = settings_name(gl_modeB_INITstr);
		const char *modes[3] = {gl_modeB_RUMBLE, gl_modeB_RAM, gl_modeB_LINK};
		if (edit)
			settings_token(value, size, p2.ModeB_pos < 3 ? modes[p2.ModeB_pos] : ok,
				       p2.ModeB_pos < 3 ? ModeB_INIT == p2.ModeB_pos : -1);
		else
			settings_add(value, size, "", ModeB_INIT < 3 ? modes[ModeB_INIT] : "");
		break;
	}
	case SEL_LED:
		*name = settings_name(gl_led);
		if (edit)
			settings_token(value, size, p2.led_pos ? ok : gl_led_open,
				       p2.led_pos ? -1 : led_open_sel == 0x1);
		else
			settings_add(value, size, "", led_open_sel == 0x1 ? gl_on : gl_off);
		break;
	case SEL_THEME:
		*name = gl_theme;
		cycle_value(value, size, select, theme_name(theme_sel), p2.extra_pos);
		break;
	case SEL_ACCENT:
		*name = gl_accent;
		accent_name(buf, sizeof(buf), accent_sel);
		cycle_value(value, size, select, buf, p2.extra_pos);
		break;
	case SEL_DIM:
		*name = gl_dim;
		dim_name(buf, sizeof(buf), dim_sel);
		cycle_value(value, size, select, buf, p2.extra_pos);
		break;
	case SEL_LOGGING:
		*name = settings_name(gl_logging);
		cycle_value(value, size, select, gl_log_levels[log_sel <= TLOG_DEBUG ? log_sel : TLOG_OFF], p2.log_pos);
		break;
	default:  // SEL_DIAGNOSE
		*name = settings_name(gl_diagnose);
		settings_add(value, size, "", gl_diag_run);
		break;
	}
}

// UP (dir 1), DOWN (-1) or A (0: the next preset) on THEME, ACCENT or DIM
static void extra_step(int dir)
{
	if (p2.select == SEL_THEME) {
		theme_step(dir);
	} else if (p2.select == SEL_ACCENT) {
		u32 n = ui_accent_count + 1;
		accent_sel = (u16)((accent_sel + (dir < 0 ? n - 1 : 1)) % n);
		preview_look();
	} else if (dir) {  // DIM: a minute
		dim_sel = (u16)(dir > 0 ? (dim_sel >= 60 ? 0 : dim_sel + 1) : (dim_sel == 0 ? 60 : dim_sel - 1));
	} else {  // DIM: the next preset
		u32 i = 0;
		while (i < sizeof(dim_presets) && dim_presets[i] <= dim_sel)
			i++;
		dim_sel = i < sizeof(dim_presets) ? dim_presets[i] : 0;
	}
}

static int is_extra(u32 select)
{
	return select == SEL_THEME || select == SEL_ACCENT || select == SEL_DIM;
}

static void page2_edit_keys(u16 keys)
{
	u32 select = p2.select;
	if (keys & INPUT_UP) {
		if (is_extra(select)) {
			if (p2.extra_pos == 0)
				extra_step(1);
		}
	} else if (keys & INPUT_DOWN) {
		if (is_extra(select)) {
			if (p2.extra_pos == 0)
				extra_step(-1);
		}
	} else if (keys & INPUT_RIGHT) {
		if (select == SEL_LOGGING) {
			p2.log_pos = 1;
		} else if (is_extra(select)) {
			p2.extra_pos = 1;
		} else if (select == SEL_SAVE) {
			p2.auto_save_pos = 1;
		} else if (select == SEL_MODEB) {
			if (p2.ModeB_pos < 3)
				p2.ModeB_pos++;
		} else if (select == SEL_LED) {
			p2.led_pos = 1;
		} else if (is_rgb(select)) {
			if (p2.rgb_pos < 3)
				p2.rgb_pos++;
		}
	} else if (keys & INPUT_LEFT) {
		if (select == SEL_LOGGING) {
			p2.log_pos = 0;
		} else if (is_extra(select)) {
			p2.extra_pos = 0;
		} else if (select == SEL_SAVE) {
			p2.auto_save_pos = 0;
		} else if (select == SEL_MODEB) {
			if (p2.ModeB_pos > 0)
				p2.ModeB_pos--;
		} else if (select == SEL_LED) {
			p2.led_pos = 0;
		} else if (is_rgb(select)) {
			if (p2.rgb_pos > 0)
				p2.rgb_pos--;
		}
	} else if (keys & INPUT_A) {
		if (select == SEL_LOGGING) {
			if (p2.log_pos == 0) {
				log_sel = (log_sel + 1) % (TLOG_DEBUG + 1);
			} else {
				save_set2_info();
				p2.set_ok = 0;
				p2.log_pos = 1;
			}
		} else if (is_extra(select)) {
			if (p2.extra_pos == 0) {
				extra_step(0);
			} else {
				save_set2_info();
				p2.set_ok = 0;
			}
		} else if (select == SEL_SAVE) {
			if (p2.auto_save_pos == 0) {
				auto_save_sel = !auto_save_sel;
			} else {
				save_set2_info();
				p2.set_ok = 0;
			}
		} else if (select == SEL_MODEB) {
			if (p2.ModeB_pos < 3) {
				ModeB_INIT = p2.ModeB_pos;
			} else {
				save_set2_info();
				p2.set_ok = 0;
			}
		} else if (select == SEL_LED) {
			if (p2.led_pos == 0) {
				led_open_sel = !led_open_sel;
			} else {
				save_set2_info();
				p2.set_ok = 0;
			}
		} else if (is_rgb(select)) {
			u16 *breathing[3] = {&Breathing_R, &Breathing_G, &Breathing_B};
			u16 *sd[3] = {&SD_R, &SD_G, &SD_B};
			if (p2.rgb_pos < 3) {
				u16 *on = (select == SEL_BREATHING ? breathing : sd)[p2.rgb_pos];
				*on = !*on;
			} else {
				save_set2_info();
				p2.set_ok = 0;
			}
		}
	}
}

static int page2_event(struct ui_view *view, const struct ui_event *e)
{
	if (e->type != UI_EV_KEYS)
		return 0;
	u16 keys = settings_keys(e);
	if (themes.note[0] && keys) {
		ui_label_set((u8)(view->first + 1), gl_settings_title);  // the title back
		themes.note[0] = 0;
	}
	if (p2.set_ok) {
		page2_edit_keys(keys);
	} else if ((keys & INPUT_A) && (p2.select == SEL_DIAGNOSE)) {
		settings_leave(SETTINGS_TO_DIAGNOSE);
		return 1;
	} else if (keys & INPUT_A) {
		p2.set_ok = 1;
		// Every edit opens on the value, MODE B's on the mode it has
		if (p2.select == SEL_SAVE)
			p2.auto_save_pos = 0;
		else if (p2.select == SEL_MODEB)
			p2.ModeB_pos = ModeB_INIT < 3 ? (u8)ModeB_INIT : 0;
		else if (p2.select == SEL_LED)
			p2.led_pos = 0;
		else if (is_rgb(p2.select))
			p2.rgb_pos = 0;
		else if (p2.select == SEL_LOGGING)
			p2.log_pos = 0;
		else if (is_extra(p2.select))
			p2.extra_pos = 0;
	} else if (keys & INPUT_DOWN) {  // by screen rows: BREATHING and SD LED only while there
		if (row_of(p2.select) + 1 < page2_count())
			p2.select = (u8)select_of(row_of(p2.select) + 1);
	} else if (keys & INPUT_UP) {
		if (p2.select)
			p2.select = (u8)select_of(row_of(p2.select) - 1);
	} else if (keys & INPUT_L) {
		settings_open(SETTINGS_PAGE1);
		return 1;
	} else if (keys & INPUT_R) {
		settings_open(SETTINGS_HELP);
		return 1;
	} else {
		return 0;
	}
	settings_sync();
	return 1;
}

static const struct ui_list_config page2_list = {&settings_model, SETTINGS_ROW_H, 0, 0, 232 - 2 * 6 - SETTINGS_NAME_W};
static const struct ui_scrollbar_config page2_bar = {SETTINGS_LIST};

static const struct ui_spec page2_nodes[] = {
	SETTINGS_HEADER,
	{.kind = UI_LIST,
	 .id = 4,
	 .parent = UI_NONE,
	 .plane = 1,
	 .anchor = UI_HFILL,
	 .paper = UI_ROLE_BG,
	 .y = 16,
	 .w = 8,
	 .h = 144,
	 .data = &page2_list},
	{.kind = UI_SCROLLBAR,
	 .id = 5,
	 .parent = UI_NONE,
	 .plane = 2,
	 .anchor = UI_RIGHT,
	 .y = 16,
	 .w = 8,
	 .h = 144,
	 .data = &page2_bar},
};

static const struct ui_view_spec page2_view = {
	"settings2", page2_nodes, sizeof(page2_nodes) / sizeof(page2_nodes[0]), 21, UI_NONE, 0, page2_event};

const struct settings_page settings_page2 = {&page2_view, page2_enter, page2_count, page2_row, page2_cursor, 0};

//---------------------------------------------------------------------------------
extern u16 SET_info_buffer[0x200] EWRAM_BSS;
static void save_set2_info(void)
{
	u32 address;
	for (address = 0; address < assress_max; address++) {
		SET_info_buffer[address] = Read_SET_info(address);
	}
	SET_info_buffer[assress_auto_save_sel] = auto_save_sel;
	SET_info_buffer[assress_ModeB_INIT] = ModeB_INIT;
	SET_info_buffer[assress_led_open_sel] = led_open_sel;
	SET_info_buffer[assress_Breathing_R] = Breathing_R;
	SET_info_buffer[assress_Breathing_G] = Breathing_G;
	SET_info_buffer[assress_Breathing_B] = Breathing_B;
	SET_info_buffer[assress_SD_R] = SD_R;
	SET_info_buffer[assress_SD_G] = SD_G;
	SET_info_buffer[assress_SD_B] = SD_B;
	if (log_sel != tlog_level_setting())  // an erased slot stays erased until changed
		SET_info_buffer[assress_logging] = log_sel;
	//Torii: the menu's look, the same way
	int theme = themes.touched && theme_sel != themes.initial;
	// theme.txt is written now, not with the NOR as SETTINGS closes: a
	// failure is told here, on its row, and the boot reads theme.txt only
	// while slot 28 says SD, so a power cut before the NOR write can at most
	// bring in early an SD theme picked over another SD theme
	if (theme && theme_sel >= 2 && !ui_theme_choice_write(themes.names[theme_sel - 2])) {
		// The card holds the SD theme's name: without it, THEME stays as it was
		tlog_error("settings: THEME not saved: " THEME_CHOICE_FILE " couldn't be written");
		theme_note("THEME not saved: can't write ", THEME_CHOICE_FILE, "");
		theme = 0;
	}
	int look = theme || accent_sel != accent_setting() || dim_sel != ui_dim_setting();
	if (theme)
		SET_info_buffer[assress_theme] = theme_sel >= 2 ? UI_THEME_SD : theme_sel;
	if (accent_sel != accent_setting())
		SET_info_buffer[assress_accent] = accent_sel;
	if (dim_sel != ui_dim_setting())
		SET_info_buffer[assress_dim] = dim_sel;

	Stage_SET_info(SET_info_buffer);  //Torii: written as SETTINGS closes (Commit_SET_info)
	if (theme) {
		themes.initial = (u8)theme_sel;
		themes.touched = 0;
	} else if (themes.touched && theme_sel != themes.initial) {
		// Not saved: back to the theme the setting keeps
		theme_sel = themes.initial == THEME_SD_GONE ? 0 : themes.initial;
		if (!theme_ready(theme_sel))
			theme_sel = 0;
		preview_look();
	}
	CheckSwitch();  //Torii: the launch reads AUTO SAVE and MODE B from these
	u16 led_status = (led_open_sel << 7) | (Breathing_R << 5) | (Breathing_G << 4) | (Breathing_B << 3) |
			 (SD_R << 2) | (SD_G << 1) | (SD_B);
	Set_LED_control(led_status);
	ui_dim_set_minutes(dim_sel);
	if (look) {
		char accent[24];
		accent_name(accent, sizeof(accent), accent_sel);
		tlog("settings: THEME %s, ACCENT %s, DIM %u", theme_name(theme_sel), accent, dim_sel);
	}
	if (log_sel != tlog_level) {
		// At the higher of the two levels, so turning logging off is logged too
		if (log_sel > tlog_level)
			tlog_set_level(log_sel);
		tlog("settings: LOGGING is %s", gl_log_levels[log_sel]);
		tlog_flush_soon();  // opening the log on the card, if it must, over the next frames
		tlog_set_level(log_sel);
	}
}
