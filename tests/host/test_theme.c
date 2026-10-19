// theme.ini parsing (source/ui/theme_ini.c): every key, the refusals, the
// warnings, line endings and comments, the example theme, and random
// garbage. Texts are copied into buffers of their exact length, so ASan
// catches a read past the end.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../source/ui/theme_ini.h"
#include "../kernel/torii_test.h"
#include "tests.h"

static struct theme_ini ini;

static int parse_n(const char *text, u32 len)
{
	char *copy = malloc(len ? len : 1);
	memcpy(copy, text, len);
	ini.on_warning = 0;
	int r = theme_ini_parse(copy, len, &ini);
	free(copy);
	return r;
}

static int parse(const char *text)
{
	return parse_n(text, (u32)strlen(text));
}

#define RGB15(r, g, b) ((u16)((r) >> 3 | ((g) >> 3) << 5 | ((b) >> 3) << 10))

static void check_sane(const struct theme_ini *t)
{
	CHECK(t->theme.patch_count <= UI_PATCH_CAP);
	CHECK(t->theme.row_h == 16 || t->theme.row_h == 32);
	CHECK(t->theme.pad <= 16);
	CHECK(t->theme.modal_dim <= 16 && t->theme.idle_dim <= 16);
	CHECK(t->theme.fade_frames >= 1 && t->theme.fade_frames <= 120);
	CHECK(t->theme.marquee_speed >= 1 && t->theme.marquee_speed <= 60);
	CHECK(t->theme.body == &font_body || t->theme.body == &font_heading);
	CHECK(t->theme.heading == &font_body || t->theme.heading == &font_heading);
	CHECK(memchr(t->name, 0, sizeof(t->name)) != 0);
	CHECK(memchr(t->wallpaper, 0, sizeof(t->wallpaper)) != 0);
	CHECK(memchr(t->message, 0, sizeof(t->message)) != 0);
	CHECK(t->theme.name == t->name);
	for (u32 i = 0; i < t->theme.patch_count; i++) {
		const struct ui_patch *p = &t->theme.patches[i];
		CHECK(p->view && p->node && p->field < UI_FIELDS);
		if (p->field <= UI_FIELD_H)
			CHECK(p->value % 8 == 0 && p->value >= -240 && p->value <= 240);
		if (p->field >= UI_FIELD_INK)
			CHECK(p->value >= 0 && p->value < 16);
	}
	for (u32 i = 0; t->wallpaper[i]; i++)
		CHECK(t->wallpaper[i] != '/' && t->wallpaper[i] != '\\');
}

void test_theme_keys(void)
{
	static const char text[] = "version = 1\n"
				   "name = Night Owl\n"
				   "base = light\n"
				   "[colours]\n"
				   "bg = #102030\n"
				   "panel = 0xFFFFFF\n"
				   "text = 08f8F8\n"
				   "selection_text = #000000\n"
				   "icon5 = #FF0000\n"
				   "[fonts]\n"
				   "body = heading\n"
				   "heading = body\n"
				   "[layout]\n"
				   "row_h = 32\n"
				   "pad = 4\n"
				   "[effects]\n"
				   "modal_dim = 0\n"
				   "idle_dim = 16\n"
				   "fade_frames = 12\n"
				   "slide = 0\n"
				   "marquee_delay = 90\n"
				   "marquee_speed = 30\n"
				   "marquee_pause = 0\n"
				   "marquee_gap = 16\n"
				   "page_fade = 0\n"
				   "[input]\n"
				   "delay = 20\n"
				   "interval = 5\n"
				   "fast_after = 90\n"
				   "fast_interval = 3\n"
				   "skip_interval = 2\n"
				   "page_delay = 25\n"
				   "page_interval = 10\n"
				   "wrap_pause = 0\n"
				   "[wallpaper]\n"
				   "file = wall.tas\n"
				   "[patches]\n"
				   "ui-demo.4.x = 16\n"
				   "1.4.w = 32\n"
				   "7.2.hidden = yes\n"
				   "7.3.anchor = right + bottom\n"
				   "7.4.ink = accent\n"
				   "7.5.paper = none\n"
				   "7.6.anchor = fill\n"
				   "7.7.y = -8\n";
	CHECK_EQ(parse(text), THEME_OK);
	CHECK_EQ(ini.warnings, 0);
	CHECK(!strcmp(ini.message, ""));
	CHECK(!strcmp(ini.theme.name, "Night Owl"));
	CHECK(!strcmp(ini.wallpaper, "wall.tas"));
	const struct ui_theme *t = &ini.theme;
	CHECK_EQ(t->colour[UI_C_BG], RGB15(0x10, 0x20, 0x30));
	CHECK_EQ(t->colour[UI_C_PANEL], 0x7FFF);
	CHECK_EQ(t->colour[UI_C_TEXT], RGB15(0x08, 0xF8, 0xF8));
	CHECK_EQ(t->colour[UI_C_SELECTION_TEXT], 0);
	CHECK_EQ(t->colour[UI_C_ICON + 5], 31);
	// Keys the file leaves out keep the base's (light)
	CHECK_EQ(t->colour[UI_C_DIM], ui_theme_light.colour[UI_C_DIM]);
	CHECK_EQ(t->colour[UI_C_ACCENT], ui_theme_light.colour[UI_C_ACCENT]);
	CHECK(t->body == &font_heading && t->heading == &font_body);
	CHECK_EQ(t->row_h, 32);
	CHECK_EQ(t->pad, 4);
	CHECK_EQ(t->modal_dim, 0);
	CHECK_EQ(t->idle_dim, 16);
	CHECK_EQ(t->fade_frames, 12);
	CHECK_EQ(t->slide, 0);
	CHECK_EQ(t->marquee_delay, 90);
	CHECK_EQ(t->marquee_speed, 30);
	CHECK_EQ(t->marquee_pause, 0);
	CHECK_EQ(t->marquee_gap, 16);
	CHECK_EQ(t->page_fade, 0);
	CHECK_EQ(t->input.delay, 20);
	CHECK_EQ(t->input.interval, 5);
	CHECK_EQ(t->input.fast_after, 90);
	CHECK_EQ(t->input.fast_interval, 3);
	CHECK_EQ(t->input.skip_interval, 2);
	CHECK_EQ(t->input.page_delay, 25);
	CHECK_EQ(t->input.page_interval, 10);
	CHECK_EQ(t->input.wrap_pause, 0);
	CHECK_EQ(t->patch_count, 8);
	s16 v;
	CHECK(ui_theme_patch(t, 1, 4, UI_FIELD_X, &v) && v == 16);
	CHECK(ui_theme_patch(t, 1, 4, UI_FIELD_W, &v) && v == 32);
	CHECK(ui_theme_patch(t, 7, 2, UI_FIELD_HIDDEN, &v) && v == 1);
	CHECK(ui_theme_patch(t, 7, 3, UI_FIELD_ANCHOR, &v) && v == (UI_RIGHT | UI_BOTTOM));
	CHECK(ui_theme_patch(t, 7, 4, UI_FIELD_INK, &v) && v == UI_ROLE_ACCENT);
	CHECK(ui_theme_patch(t, 7, 5, UI_FIELD_PAPER, &v) && v == UI_ROLE_NONE);
	CHECK(ui_theme_patch(t, 7, 6, UI_FIELD_ANCHOR, &v) && v == UI_FILL);
	CHECK(ui_theme_patch(t, 7, 7, UI_FIELD_Y, &v) && v == -8);
	check_sane(&ini);

	// Only a version: the dark theme, unchanged
	CHECK_EQ(parse("version=1"), THEME_OK);
	CHECK_EQ(ini.warnings, 0);
	CHECK(!memcmp(ini.theme.colour, ui_theme_dark.colour, sizeof(ini.theme.colour)));
	CHECK(!memcmp(&ini.theme.input, &ui_theme_dark.input, sizeof(ini.theme.input)));
	// Fields a file leaves out are the built-in's, never 0
	CHECK_EQ(ini.theme.marquee_speed, ui_theme_dark.marquee_speed);
	CHECK_EQ(ini.theme.marquee_pause, ui_theme_dark.marquee_pause);
	CHECK_EQ(ini.theme.marquee_gap, ui_theme_dark.marquee_gap);
	CHECK_EQ(ini.theme.page_fade, ui_theme_dark.page_fade);
	CHECK(ini.theme.marquee_speed > 0);
	CHECK(ini.theme.row_h && ini.theme.fade_frames && ini.theme.body && ini.theme.heading);
	CHECK_EQ(ini.theme.patch_count, 0);
	CHECK(!strcmp(ini.name, "") && !strcmp(ini.wallpaper, ""));
}

void test_theme_refused(void)
{
	CHECK_EQ(parse(""), THEME_ERR_VERSION);
	CHECK(strstr(ini.message, "no \"version = 1\"") != 0);
	CHECK_EQ(parse("[colours]\nbg = #000000\n"), THEME_ERR_VERSION);
	CHECK_EQ(parse("version = 2\n"), THEME_ERR_VERSION);
	CHECK(strstr(ini.message, "version 2") != 0);
	CHECK_EQ(parse("version = one\n"), THEME_ERR_VERSION);
	CHECK_EQ(parse("version = 0\n"), THEME_ERR_VERSION);
	CHECK(strstr(ini.message, "version 0") != 0);
	CHECK_EQ(parse("version = -1\n"), THEME_ERR_VERSION);
	CHECK(strstr(ini.message, "version -1") != 0);
	CHECK(strstr((parse("version = x\n"), ini.message), "isn't a number") != 0);
	// A refused file reports no warnings, not even through the callback
	ini.on_warning = 0;
	CHECK_EQ(parse("base = blue\nversion = 2\n"), THEME_ERR_VERSION);
	CHECK_EQ(ini.warnings, 0);
	// A version inside a section isn't the file's
	CHECK_EQ(parse("[layout]\nversion = 1\n"), THEME_ERR_VERSION);
	// The last version line counts
	CHECK_EQ(parse("version = 1\nversion = 3\n"), THEME_ERR_VERSION);
	CHECK_EQ(parse("version = 3\nversion = 1\n"), THEME_OK);
	static char big[THEME_INI_MAX + 1];
	memset(big, '\n', sizeof(big));
	memcpy(big, "version = 1", 11);
	CHECK_EQ(parse_n(big, THEME_INI_MAX), THEME_OK);
	CHECK_EQ(parse_n(big, THEME_INI_MAX + 1), THEME_ERR_SIZE);
	CHECK(ini.message[0]);
}

static u32 warnings_seen;
static void count_warning(const char *msg)
{
	CHECK(!strncmp(msg, "line ", 5));
	warnings_seen++;
}

void test_theme_warnings(void)
{
	static const char text[] = "version = 1\n"          // 1
				   "base = sepia\n"         // 2: dark used
				   "colour = red\n"         // 3: unknown top key
				   "[colours]\n"            // 4
				   "bg = #12345\n"          // 5: 5 digits
				   "text = #GG0000\n"       // 6
				   "chartreuse = #00FF00\n" // 7: unknown key
				   "[layout]\n"             // 8
				   "pad = 99\n"             // 9: clamped to 16
				   "row_h = 20\n"           // 10: ignored
				   "[effects]\n"            // 11
				   "idle_dim = -3\n"        // 12: clamped to 0
				   "fade_frames = 0\n"      // 13: clamped to 1
				   "slide = 1234567\n"      // 14: not a number (too many digits)
				   "[fonts]\n"              // 15
				   "body = comic sans\n"    // 16
				   "[sounds]\n"             // 17: unknown section
				   "beep = yes\n"           // 18: ignored without a warning
				   "[wallpaper]\n"          // 19
				   "file = ../evil.tas\n"   // 20
				   "file = a/b.tas\n"       // 21
				   "just words\n"           // 22: no '='
				   "[patches\n"             // 23: broken section line
				   "x = 1\n";               // 24: ignored under it
	ini.on_warning = count_warning;
	warnings_seen = 0;
	char *copy = malloc(sizeof(text) - 1);
	memcpy(copy, text, sizeof(text) - 1);
	CHECK_EQ(theme_ini_parse(copy, sizeof(text) - 1, &ini), THEME_OK);
	free(copy);
	CHECK_EQ(ini.warnings, 16);
	CHECK_EQ(warnings_seen, 16);
	CHECK(!strncmp(ini.message, "line 2: base = sepia", 20));
	CHECK(!memcmp(ini.theme.colour, ui_theme_dark.colour, sizeof(ini.theme.colour)));
	CHECK_EQ(ini.theme.pad, 16);
	CHECK_EQ(ini.theme.row_h, 16);
	CHECK_EQ(ini.theme.idle_dim, 0);
	CHECK_EQ(ini.theme.fade_frames, 1);
	CHECK_EQ(ini.theme.slide, ui_theme_dark.slide);
	CHECK(ini.theme.body == &font_body);
	CHECK(!strcmp(ini.wallpaper, ""));
	check_sane(&ini);

	// Patches: bad ones are dropped, rectangles rounded and clamped, a
	// repeated target replaced, and the 17th distinct one refused
	char buf[2048];
	u32 n = (u32)snprintf(buf, sizeof(buf),
			      "version=1\n[patches]\n"
			      "nosuchview.1.x = 8\n"  // unknown view
			      "1.0.x = 8\n"           // node 0
			      "1.1.colour = 8\n"      // unknown field
			      "1.1.ink = 16\n"        // not a role
			      "1.1.anchor = sideways\n"
			      "1.1.hidden = maybe\n"
			      "1.1 = 3\n"             // not VIEW.NODE.FIELD
			      "1.1.x = 13\n"          // rounded to 8
			      "1.1.y = 999\n"         // clamped to 240
			      "1.1.x = 16\n"          // replaces x
			      "1.1.w = -8\n");        // clamped to 0
	for (u32 i = 2; i <= 16; i++)
		n += (u32)snprintf(buf + n, sizeof(buf) - n, "1.%lu.hidden = 1\n", (unsigned long)i);
	CHECK(n < sizeof(buf));
	CHECK_EQ(parse_n(buf, n), THEME_OK);
	CHECK_EQ(ini.theme.patch_count, 16);
	CHECK_EQ(ini.warnings, 7 + 3 + 2);
	s16 v;
	CHECK(ui_theme_patch(&ini.theme, 1, 1, UI_FIELD_X, &v) && v == 16);
	CHECK(ui_theme_patch(&ini.theme, 1, 1, UI_FIELD_Y, &v) && v == 240);
	CHECK(ui_theme_patch(&ini.theme, 1, 1, UI_FIELD_W, &v) && v == 0);
	CHECK(ui_theme_patch(&ini.theme, 1, 14, UI_FIELD_HIDDEN, &v) && v == 1);
	CHECK(!ui_theme_patch(&ini.theme, 1, 15, UI_FIELD_HIDDEN, &v));
	CHECK(!ui_theme_patch(&ini.theme, 1, 16, UI_FIELD_HIDDEN, &v));
	check_sane(&ini);
}

void test_theme_formats(void)
{
	// A BOM, CRLF, tabs, comments, an inline comment, no final newline
	static const char crlf[] = "\xEF\xBB\xBF; a comment\r\n"
				   "# another\r\n"
				   "\tVERSION\t=\t1\t\r\n"
				   "\r\n"
				   "[ Colors ]\r\n"
				   "BG = #ffffff ; white\r\n"
				   "panel=#000000;not a comment\r\n"
				   "[LAYOUT]\r\n"
				   "Pad = 2";
	CHECK_EQ(parse(crlf), THEME_OK);
	CHECK_EQ(ini.theme.colour[UI_C_BG], 0x7FFF);
	CHECK_EQ(ini.theme.colour[UI_C_PANEL], ui_theme_dark.colour[UI_C_PANEL]);  // "#000000;not..." isn't a colour
	CHECK_EQ(ini.theme.pad, 2);
	CHECK_EQ(ini.warnings, 1);
	// Old Mac line ends
	CHECK_EQ(parse("version = 1\r[layout]\rpad = 3\r"), THEME_OK);
	CHECK_EQ(ini.theme.pad, 3);
	CHECK_EQ(ini.warnings, 0);
	// A line too long is skipped, the next one read; a NUL spoils one line
	char buf[600];
	u32 n = (u32)snprintf(buf, sizeof(buf), "version = 1\n[layout]\n; ");
	memset(buf + n, 'x', 300);
	n += 300;
	n += (u32)snprintf(buf + n, sizeof(buf) - n, "\npad = 5\nrow_h = 32");
	buf[n++] = 0;
	n += (u32)snprintf(buf + n, sizeof(buf) - n, "\n");
	CHECK_EQ(parse_n(buf, n), THEME_OK);
	CHECK_EQ(ini.theme.pad, 5);
	CHECK_EQ(ini.theme.row_h, 16);
	CHECK_EQ(ini.warnings, 2);
	// A name too long is cut at a character boundary (é is 2 bytes)
	CHECK_EQ(parse("version = 1\nname = \xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9"
		       "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\n"),
		 THEME_OK);
	CHECK_EQ(strlen(ini.name), 22);
	CHECK_EQ(ini.warnings, 1);
	CHECK_EQ(parse("version = 1\nname = a\x01b\n"), THEME_OK);
	CHECK(!strcmp(ini.name, ""));
	// A comment after a section line; leading zeros
	CHECK_EQ(parse("version = 001\n[layout] ; rows\nrow_h = 0000032\n[effects] # more\nslide = 8\n"), THEME_OK);
	CHECK_EQ(ini.theme.row_h, 32);
	CHECK_EQ(ini.theme.slide, 8);
	CHECK_EQ(ini.warnings, 0);
	// Only ';' ends a value
	CHECK_EQ(parse("version = 1\n[layout]\npad = 4 # four\n"), THEME_OK);
	CHECK_EQ(ini.warnings, 1);
}

void test_theme_names(void)
{
	CHECK(theme_name_ok("example"));
	CHECK(theme_name_ok("Night Owl-2_b.v1"));
	CHECK(theme_name_ok("12345678901234567890123"));
	CHECK(!theme_name_ok("123456789012345678901234"));
	CHECK(!theme_name_ok(""));
	CHECK(!theme_name_ok(".hidden"));
	CHECK(!theme_name_ok(" lead"));
	CHECK(!theme_name_ok("a/b"));
	CHECK(!theme_name_ok("a\\b"));
	CHECK(!theme_name_ok(".."));
	CHECK(!theme_name_ok("caf\xC3\xA9"));
	CHECK(!theme_name_ok("Night."));
	CHECK(!theme_name_ok("Night "));
	CHECK(theme_name_ok("v1.2b"));
}

void test_theme_example(void)
{
	FILE *f = fopen("themes/example/theme.ini", "rb");
	CHECK(f != 0);
	if (!f)
		return;
	static char text[THEME_INI_MAX + 1];
	u32 n = (u32)fread(text, 1, sizeof(text), f);
	fclose(f);
	CHECK_EQ(parse_n(text, n), THEME_OK);
	CHECK_EQ(ini.warnings, 0);
	if (ini.warnings)
		printf("  %s\n", ini.message);
	CHECK(!strcmp(ini.name, "Example"));
	CHECK(!strcmp(ini.wallpaper, "wallpaper.tas"));
	check_sane(&ini);
}

static u32 seed = 12345;
static u32 rnd(void)
{
	seed = seed * 1103515245u + 12345u;
	return seed >> 8;
}

void test_theme_garbage(void)
{
	// Random bytes, then random text made of the file's own pieces
	static const char *const pieces[] = {
		"version", "=",      "1",       " ",         "\t",           "\n",
		"\r",      "\r\n",   "[",       "]",         "colours",      "patches",
		"layout",  "input",  "effects", "wallpaper", "fonts",        "#",
		";",       ".",      "bg",      "pad",       "row_h",        "x",
		"w",       "anchor", "ink",     "file",      "name",         "base",
		"light",   "-",      "999999",  "32",        "16",           "#A0B0C0",
		"ui-demo", "4",      "fill",    "+",         "\xEF\xBB\xBF", "\xC3\xA9",
		"\0",      "hidden", "yes",     "/",         "..",
	};
	static char buf[THEME_INI_MAX];
	for (u32 iter = 0; iter < 3000; iter++) {
		u32 len = rnd() % (iter < 1000 ? 600 : sizeof(buf));
		if (iter % 2) {
			for (u32 i = 0; i < len; i++)
				buf[i] = (char)rnd();
		} else {
			u32 n = 0;
			if (iter % 4 == 0) {
				memcpy(buf, "version=1\n", 10);
				n = 10;
			}
			while (n < len) {
				u32 k = rnd() % (sizeof(pieces) / sizeof(pieces[0]));
				u32 l = k == 42 ? 1 : (u32)strlen(pieces[k]);
				if (n + l > len)
					break;
				memcpy(buf + n, pieces[k], l);
				n += l;
			}
			len = n;
		}
		int r = parse_n(buf, len);
		CHECK(r == THEME_OK || r == THEME_ERR_VERSION);
		check_sane(&ini);
	}
}
