// The theme.ini parser (theme_ini.h). Hardware-free.
//
// Keys are table-driven: a theme field is one line in `keys` below (its
// section, name, kind, offset in struct ui_theme and range).
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "theme_ini.h"
#include "../engine/asset.h"

const struct theme_view_name theme_view_names[] = {
	// The test kernel's UI demo (tests/kernel/ui_demo.c)
	{"ui-demo", 1},
	{"ui-dialog", 2},
	{"ui-grid", 3},
	// The lists (source/screens/lists.c); the game and NOR game menus have
	// two layouts each (rows), which share an id
	{"sd-list", 10},
	{"nor-list", 11},
	{"recent", 12},
	{"file-menu", 13},
	{"nor-menu", 14},
	{"message", 15},
	{"confirm", 16},
	// SETTINGS and HELP (source/settings.h)
	{"settings1", 20},
	{"settings2", 21},
	{"help", 22},
	// The cheat viewer (source/showcht.c)
	{"cheats", 30},
};
const u32 theme_view_name_count = sizeof(theme_view_names) / sizeof(theme_view_names[0]);

enum section {
	S_TOP,
	S_COLOURS,
	S_FONTS,
	S_LAYOUT,
	S_EFFECTS,
	S_INPUT,
	S_WALLPAPER,
	S_PATCHES,
	S_UNKNOWN,
};

static const char *const section_names[] = {
	[S_TOP] = "",
	[S_COLOURS] = "colours",
	[S_FONTS] = "fonts",
	[S_LAYOUT] = "layout",
	[S_EFFECTS] = "effects",
	[S_INPUT] = "input",
	[S_WALLPAPER] = "wallpaper",
	[S_PATCHES] = "patches",
};

enum kind {
	K_U8,      // a number in [min, max], clamped
	K_COLOUR,  // #RRGGBB to RGB15
	K_FONT,    // "body" or "heading", a built-in font
	K_ROW_H,   // 16 or 32
	K_FILE,    // an asset's file name (struct theme_ini, not the theme)
};

struct key {
	u8 section;
	u8 kind;
	u8 min, max;
	u16 offset;
	const char *name;
};

#define FIELD(f) ((u16)offsetof(struct ui_theme, f))
#define COLOUR(key, c) {S_COLOURS, K_COLOUR, 0, 0, FIELD(colour[c]), key}
#define NUMBER(sec, key, f, lo, hi) {sec, K_U8, lo, hi, FIELD(f), key}

static const struct key keys[] = {
	COLOUR("bg", UI_C_BG),
	COLOUR("panel", UI_C_PANEL),
	COLOUR("text", UI_C_TEXT),
	COLOUR("dim", UI_C_DIM),
	COLOUR("accent", UI_C_ACCENT),
	COLOUR("accent_text", UI_C_ACCENT_TEXT),
	COLOUR("border", UI_C_BORDER),
	COLOUR("track", UI_C_TRACK),
	COLOUR("shadow", UI_C_SHADOW),
	COLOUR("selection", UI_C_SELECTION),
	COLOUR("selection_text", UI_C_SELECTION_TEXT),
	COLOUR("icon0", UI_C_ICON + 0),
	COLOUR("icon1", UI_C_ICON + 1),
	COLOUR("icon2", UI_C_ICON + 2),
	COLOUR("icon3", UI_C_ICON + 3),
	COLOUR("icon4", UI_C_ICON + 4),
	COLOUR("icon5", UI_C_ICON + 5),
	{S_FONTS, K_FONT, 0, 0, FIELD(body), "body"},
	{S_FONTS, K_FONT, 0, 0, FIELD(heading), "heading"},
	{S_LAYOUT, K_ROW_H, 16, 32, FIELD(row_h), "row_h"},
	NUMBER(S_LAYOUT, "pad", pad, 0, 16),
	NUMBER(S_EFFECTS, "modal_dim", modal_dim, 0, 16),
	NUMBER(S_EFFECTS, "idle_dim", idle_dim, 0, 16),
	NUMBER(S_EFFECTS, "fade_frames", fade_frames, 1, 120),
	NUMBER(S_EFFECTS, "slide", slide, 0, 96),
	NUMBER(S_EFFECTS, "marquee_delay", marquee_delay, 0, 255),
	NUMBER(S_EFFECTS, "marquee_speed", marquee_speed, 1, 60),
	NUMBER(S_EFFECTS, "marquee_pause", marquee_pause, 0, 255),
	NUMBER(S_EFFECTS, "marquee_gap", marquee_gap, 0, 120),
	NUMBER(S_EFFECTS, "page_fade", page_fade, 0, 30),
	NUMBER(S_INPUT, "delay", input.delay, 1, 255),
	NUMBER(S_INPUT, "interval", input.interval, 1, 255),
	NUMBER(S_INPUT, "fast_after", input.fast_after, 1, 255),
	NUMBER(S_INPUT, "fast_interval", input.fast_interval, 1, 255),
	NUMBER(S_INPUT, "skip_interval", input.skip_interval, 1, 255),
	NUMBER(S_INPUT, "page_delay", input.page_delay, 1, 255),
	NUMBER(S_INPUT, "page_interval", input.page_interval, 1, 255),
	NUMBER(S_INPUT, "wrap_pause", input.wrap_pause, 0, 255),
	{S_WALLPAPER, K_FILE, 0, 0, (u16)offsetof(struct theme_ini, wallpaper), "file"},
};

static const char *const field_names[UI_FIELDS] = {
	[UI_FIELD_X] = "x",           [UI_FIELD_Y] = "y",           [UI_FIELD_W] = "w",     [UI_FIELD_H] = "h",
	[UI_FIELD_ANCHOR] = "anchor", [UI_FIELD_HIDDEN] = "hidden", [UI_FIELD_INK] = "ink", [UI_FIELD_PAPER] = "paper",
};

// Role names for ink and paper: palette indices 0-15
static const char *const role_names[16] = {
	"none",  "bg",     "panel", "text",  "dim",   "accent", "accent_text", "border",
	"track", "shadow", "icon0", "icon1", "icon2", "icon3",  "icon4",       "icon5",
};

struct parser {
	struct theme_ini *out;
	u32 line;
	u8 section;
	u8 pass;  // 1: version and base only; 2: the rest
};

static void warn(struct parser *p, const char *format, ...) __attribute__((format(printf, 2, 3)));
static void warn(struct parser *p, const char *format, ...)
{
	struct theme_ini *out = p->out;
	if (out->warnings < 0xFFFF)
		out->warnings++;
	char msg[THEME_MESSAGE_MAX];
	int n = snprintf(msg, sizeof(msg), "line %lu: ", (unsigned long)p->line);
	va_list args;
	va_start(args, format);
	if (n > 0 && (u32)n < sizeof(msg))
		vsnprintf(msg + n, sizeof(msg) - (u32)n, format, args);
	va_end(args);
	if (!out->message[0])
		memcpy(out->message, msg, sizeof(msg));
	if (out->on_warning)
		out->on_warning(msg);
}

static int is_space(char c)
{
	return c == ' ' || c == '\t';
}

static char lower(char c)
{
	return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

// Case-insensitive comparison of an ASCII word with a NUL-terminated one
static int same(const char *a, const char *b)
{
	while (*a && *b && lower(*a) == lower(*b)) {
		a++;
		b++;
	}
	return !*a && !*b;
}

static char *trim(char *s)
{
	while (is_space(*s))
		s++;
	char *end = s + strlen(s);
	while (end > s && is_space(end[-1]))
		*--end = 0;
	return s;
}

// A decimal number, optionally signed, in *out; 0 if s isn't one. At most
// 6 digits, so it can't overflow.
static int parse_int(const char *s, s32 *out)
{
	int neg = 0;
	if (*s == '-' || *s == '+')
		neg = *s++ == '-';
	s32 v = 0;
	u32 digits = 0;
	if (*s == '0') {
		// Leading zeros don't count towards the limit
		while (s[1] == '0')
			s++;
	}
	for (; *s >= '0' && *s <= '9'; s++) {
		if (++digits > 6)
			return 0;
		v = v * 10 + (*s - '0');
	}
	if (!digits || *s)
		return 0;
	*out = neg ? -v : v;
	return 1;
}

static int hex_digit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	c = lower(c);
	return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

// #RRGGBB, 0xRRGGBB or RRGGBB to RGB15 (each channel's top 5 bits, as
// tools/asset.py rounds)
static int parse_colour(const char *s, u16 *out)
{
	if (*s == '#')
		s++;
	else if (s[0] == '0' && lower(s[1]) == 'x')
		s += 2;
	u32 rgb = 0;
	for (u32 i = 0; i < 6; i++) {
		int d = hex_digit(s[i]);
		if (d < 0)
			return 0;
		rgb = rgb << 4 | (u32)d;
	}
	if (s[6])
		return 0;
	u32 r = rgb >> 16 & 0xFF, g = rgb >> 8 & 0xFF, b = rgb & 0xFF;
	*out = (u16)(r >> 3 | (g >> 3) << 5 | (b >> 3) << 10);
	return 1;
}

static int parse_bool(const char *s, s32 *out)
{
	static const char *const yes[] = {"1", "yes", "true", "on"}, *const no[] = {"0", "no", "false", "off"};
	for (u32 i = 0; i < 4; i++) {
		if (same(s, yes[i])) {
			*out = 1;
			return 1;
		}
		if (same(s, no[i])) {
			*out = 0;
			return 1;
		}
	}
	return 0;
}

int theme_name_ok(const char *name)
{
	u32 n = 0;
	for (const char *s = name; *s; s++, n++) {
		char c = *s;
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' ||
		      c == '-' || c == '_' || c == '.'))
			return 0;
	}
	// FAT drops a trailing dot or space, so such a name opens another folder
	return n > 0 && n < THEME_NAME_MAX && name[0] != '.' && name[0] != ' ' && name[n - 1] != '.' &&
	       name[n - 1] != ' ';
}

// Copies a display name: printable bytes (UTF-8 above 0x7F kept), cut at a
// character boundary to fit
static void copy_name(struct parser *p, const char *value)
{
	char *dst = p->out->name;
	u32 n = 0;
	for (const char *s = value; *s; s++) {
		if ((u8)*s < 0x20 || *s == 0x7F) {
			warn(p, "name has a control character; ignored");
			dst[0] = 0;
			return;
		}
	}
	for (; n < THEME_NAME_MAX - 1 && value[n]; n++)
		dst[n] = value[n];
	if (value[n]) {
		// Back off to the start of a UTF-8 character
		while (n > 0 && ((u8)value[n] & 0xC0) == 0x80)
			n--;
		warn(p, "name is longer than %d bytes; cut", THEME_NAME_MAX - 1);
	}
	dst[n] = 0;
}

// Rounds a rectangle value to a multiple of 8 (towards zero) and clamps it
static s16 rect_value(struct parser *p, const char *key, s32 v, s32 lo, s32 hi)
{
	if (v < lo || v > hi) {
		warn(p, "%s = %ld is out of range %ld to %ld; clamped", key, (long)v, (long)lo, (long)hi);
		v = v < lo ? lo : hi;
	}
	if (v % 8) {
		s32 r = v - v % 8;
		warn(p, "%s = %ld isn't a multiple of 8; %ld used", key, (long)v, (long)r);
		v = r;
	}
	return (s16)v;
}

#define NO 0xFF
static int parse_anchor(char *s, s32 *out)
{
	s32 n;
	if (parse_int(s, &n)) {
		if (n < 0 || n > 15)
			return 0;
		*out = n;
		return 1;
	}
	static const struct {
		const char *name;
		u8 h, v;  // NO: unchanged
	} words[] = {
		{"left", UI_LEFT, NO},        {"right", UI_RIGHT, NO},
		{"hfill", UI_HFILL, NO},      {"hcenter", UI_HCENTER, NO},
		{"top", NO, UI_TOP},          {"bottom", NO, UI_BOTTOM},
		{"vfill", NO, UI_VFILL},      {"vcenter", NO, UI_VCENTER},
		{"fill", UI_HFILL, UI_VFILL}, {"center", UI_HCENTER, UI_VCENTER},
	};
	s32 h = UI_LEFT, v = UI_TOP;
	u32 count = 0;
	while (*s) {
		while (*s == '+' || *s == '|' || *s == ',' || is_space(*s))
			s++;
		if (!*s)
			break;
		char *word = s;
		while (*s && *s != '+' && *s != '|' && *s != ',' && !is_space(*s))
			s++;
		char end = *s;
		*s = 0;
		u32 i;
		for (i = 0; i < sizeof(words) / sizeof(words[0]); i++)
			if (same(word, words[i].name))
				break;
		if (i == sizeof(words) / sizeof(words[0]))
			return 0;
		if (words[i].h != NO)
			h = words[i].h;
		if (words[i].v != NO)
			v = words[i].v;
		count++;
		*s = end;
	}
	if (!count)
		return 0;
	*out = h | v;
	return 1;
}
#undef NO

static int parse_role(const char *s, s32 *out)
{
	s32 n;
	if (parse_int(s, &n)) {
		if (n < 0 || n > 15)
			return 0;
		*out = n;
		return 1;
	}
	for (u32 i = 0; i < 16; i++)
		if (same(s, role_names[i])) {
			*out = (s32)i;
			return 1;
		}
	return 0;
}

// [patches] VIEW.NODE.FIELD = VALUE
static void patch(struct parser *p, char *key, char *value)
{
	char *dot2 = strrchr(key, '.');
	char *dot1 = dot2 ? dot2 : 0;
	if (dot2) {
		*dot2 = 0;
		dot1 = strrchr(key, '.');
		*dot2 = '.';
	}
	if (!dot1) {
		warn(p, "patch %s isn't VIEW.NODE.FIELD; ignored", key);
		return;
	}
	*dot1 = 0;
	*dot2 = 0;
	const char *view_s = trim(key), *node_s = trim(dot1 + 1), *field_s = trim(dot2 + 1);
	s32 view = -1, node, v;
	if (!parse_int(view_s, &view)) {
		for (u32 i = 0; i < theme_view_name_count; i++)
			if (same(view_s, theme_view_names[i].name))
				view = theme_view_names[i].id;
	}
	if (view < 1 || view > 255) {
		warn(p, "patch: no view %s; ignored", view_s);
		return;
	}
	if (!parse_int(node_s, &node) || node < 1 || node > 255) {
		warn(p, "patch: node %s isn't a number from 1 to 255; ignored", node_s);
		return;
	}
	u32 field;
	for (field = 0; field < UI_FIELDS; field++)
		if (same(field_s, field_names[field]))
			break;
	if (field == UI_FIELDS) {
		warn(p, "patch: no field %s (x, y, w, h, anchor, hidden, ink, paper); ignored", field_s);
		return;
	}
	int ok;
	switch (field) {
	case UI_FIELD_X:
	case UI_FIELD_Y:
		ok = parse_int(value, &v);
		if (ok)
			v = rect_value(p, field_s, v, -240, 240);
		break;
	case UI_FIELD_W:
	case UI_FIELD_H:
		ok = parse_int(value, &v);
		if (ok)
			v = rect_value(p, field_s, v, 0, 240);
		break;
	case UI_FIELD_ANCHOR:
		ok = parse_anchor(value, &v);
		break;
	case UI_FIELD_HIDDEN:
		ok = parse_bool(value, &v);
		break;
	default:
		ok = parse_role(value, &v);
		break;
	}
	if (!ok) {
		warn(p, "patch: %s = %s isn't a value it takes; ignored", field_s, value);
		return;
	}
	struct ui_theme *t = &p->out->theme;
	u32 i;
	for (i = 0; i < t->patch_count; i++) {
		struct ui_patch *q = &t->patches[i];
		if (q->view == view && q->node == node && q->field == field)
			break;
	}
	if (i == UI_PATCH_CAP) {
		warn(p, "more than %d patches; this one is ignored", UI_PATCH_CAP);
		return;
	}
	t->patches[i] = (struct ui_patch){(u8)view, (u8)node, (u8)field, 0, (s16)v};
	if (i == t->patch_count)
		t->patch_count++;
}

static void set_key(struct parser *p, const struct key *k, const char *value)
{
	u8 *theme = (u8 *)&p->out->theme;
	s32 n;
	switch (k->kind) {
	case K_U8:
		if (!parse_int(value, &n)) {
			warn(p, "%s = %s isn't a number; ignored", k->name, value);
			return;
		}
		if (n < k->min || n > k->max) {
			warn(p, "%s = %ld is out of range %u to %u; clamped", k->name, (long)n, k->min, k->max);
			n = n < k->min ? k->min : k->max;
		}
		theme[k->offset] = (u8)n;
		return;
	case K_COLOUR: {
		u16 c;
		if (!parse_colour(value, &c)) {
			warn(p, "%s = %s isn't a colour (#RRGGBB); ignored", k->name, value);
			return;
		}
		memcpy(theme + k->offset, &c, sizeof(c));
		return;
	}
	case K_FONT: {
		const struct torii_font *f = same(value, "body")      ? &font_body
					     : same(value, "heading") ? &font_heading
								      : 0;
		if (!f) {
			warn(p, "%s = %s isn't a font (body or heading); ignored", k->name, value);
			return;
		}
		// A pointer field: offsetof keeps it aligned
		*(const struct torii_font **)(void *)(theme + k->offset) = f;
		return;
	}
	case K_ROW_H:
		if (!parse_int(value, &n) || (n != 16 && n != 32)) {
			warn(p, "%s = %s isn't 16 or 32; ignored", k->name, value);
			return;
		}
		theme[k->offset] = (u8)n;
		return;
	case K_FILE: {
		char *dst = (char *)p->out + k->offset;
		u32 len = (u32)strlen(value);
		int ok = len > 0 && len < THEME_FILE_MAX && value[0] != '.' && value[len - 1] != '.';
		for (const char *s = value; ok && *s; s++)
			ok = (u8)*s > 0x20 && *s != '/' && *s != '\\' && *s != ':' && (u8)*s < 0x7F;
		if (!ok) {
			warn(p, "%s = %s isn't a file name in the theme's folder; ignored", k->name, value);
			return;
		}
		memcpy(dst, value, len + 1);
		return;
	}
	default:
		return;
	}
}

static void top_key(struct parser *p, const char *key, const char *value)
{
	struct theme_ini *out = p->out;
	if (same(key, "version")) {
		if (p->pass == 1) {
			s32 n;
			out->version = parse_int(value, &n) ? n : THEME_VERSION_NAN;
			out->has_version = 1;
		}
		return;
	}
	if (same(key, "base")) {
		int dark = same(value, "dark"), light = same(value, "light");
		if (p->pass == 1)
			out->base = light ? &ui_theme_light : &ui_theme_dark;
		else if (!dark && !light)
			warn(p, "base = %s isn't dark or light; dark used", value);
		return;
	}
	if (p->pass != 2)
		return;
	if (same(key, "name"))
		copy_name(p, value);
	else
		warn(p, "unknown key %s; ignored", key);
}

static void line(struct parser *p, char *s)
{
	s = trim(s);
	if (!*s || *s == ';' || *s == '#')
		return;
	if (*s == '[') {
		char *end = strchr(s, ']');
		char *after = end ? trim(end + 1) : 0;
		if (!end || (after[0] && after[0] != ';' && after[0] != '#')) {
			if (p->pass == 2)
				warn(p, "a section line is [name]; keys up to the next section ignored");
			p->section = S_UNKNOWN;
			return;
		}
		*end = 0;
		char *name = trim(s + 1);
		p->section = S_UNKNOWN;
		for (u32 i = S_COLOURS; i < S_UNKNOWN; i++)
			if (same(name, section_names[i]))
				p->section = (u8)i;
		if (same(name, "colors"))
			p->section = S_COLOURS;
		if (p->section == S_UNKNOWN && p->pass == 2)
			warn(p, "unknown section [%s]; its keys are ignored", name);
		return;
	}
	char *eq = strchr(s, '=');
	if (!eq) {
		if (p->pass == 2)
			warn(p, "no '=' in this line; ignored");
		return;
	}
	*eq = 0;
	char *key = trim(s), *value = eq + 1;
	// A comment after the value: whitespace, then ';'
	for (char *c = value; *c; c++)
		if (*c == ';' && (c == value || is_space(c[-1]))) {
			*c = 0;
			break;
		}
	value = trim(value);
	if (p->section == S_TOP) {
		top_key(p, key, value);
		return;
	}
	if (p->pass != 2 || p->section == S_UNKNOWN)
		return;
	if (p->section == S_PATCHES) {
		patch(p, key, value);
		return;
	}
	for (u32 i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
		if (keys[i].section == p->section && same(key, keys[i].name)) {
			set_key(p, &keys[i], value);
			return;
		}
	warn(p, "unknown key %s in [%s]; ignored", key, section_names[p->section]);
}

static void walk(struct parser *p, const char *text, u32 len)
{
	u32 pos = 0;
	if (len >= 3 && (u8)text[0] == 0xEF && (u8)text[1] == 0xBB && (u8)text[2] == 0xBF)
		pos = 3;
	p->line = 0;
	p->section = S_TOP;
	char buf[THEME_LINE_MAX + 1];
	while (pos < len) {
		u32 start = pos, end = pos;
		while (end < len && text[end] != '\n' && text[end] != '\r')
			end++;
		pos = end;
		if (pos < len && text[pos] == '\r')
			pos++;
		if (pos < len && text[pos] == '\n')
			pos++;
		p->line++;
		u32 n = end - start;
		if (n > THEME_LINE_MAX) {
			if (p->pass == 2)
				warn(p, "longer than %d bytes; ignored", THEME_LINE_MAX);
			continue;
		}
		if (memchr(text + start, 0, n)) {
			if (p->pass == 2)
				warn(p, "has a NUL byte; ignored");
			continue;
		}
		memcpy(buf, text + start, n);
		buf[n] = 0;
		line(p, buf);
	}
}

int theme_ini_parse(const char *text, u32 len, struct theme_ini *out)
{
	void (*on_warning)(const char *) = out->on_warning;
	memset(out, 0, sizeof(*out));
	out->on_warning = on_warning;
	out->base = &ui_theme_dark;
	out->theme = ui_theme_dark;
	out->theme.name = out->name;
	if (len > THEME_INI_MAX) {
		snprintf(out->message, sizeof(out->message), "theme.ini is over %d bytes", THEME_INI_MAX);
		return THEME_ERR_SIZE;
	}
	struct parser p = {out, 0, S_TOP, 1};
	walk(&p, text, len);
	if (!out->has_version || out->version != THEME_VERSION) {
		if (!out->has_version)
			snprintf(out->message, sizeof(out->message), "theme.ini has no \"version = %d\" line",
				 THEME_VERSION);
		else if (out->version == THEME_VERSION_NAN)
			snprintf(out->message, sizeof(out->message), "theme.ini's version isn't a number");
		else
			snprintf(out->message, sizeof(out->message), "theme.ini is version %ld; this kernel reads %d",
				 (long)out->version, THEME_VERSION);
		out->warnings = 0;
		return THEME_ERR_VERSION;
	}
	out->theme = *out->base;
	out->theme.name = out->name;
	out->theme.patch_count = 0;
	p.pass = 2;
	walk(&p, text, len);
	return THEME_OK;
}

int theme_wallpaper_check(const char *folder, const char *file, const void *data, u32 size, char *msg, u32 cap)
{
	if (size > ASSET_FILE_MAX) {
		snprintf(msg, cap, "%s: wallpaper %s is over 16 KB", folder, file);
		return 0;
	}
	const u8 *bytes = data;
	struct asset_header h;
	u32 lz_word = 0;
	if (size >= sizeof(h)) {
		memcpy(&h, bytes, sizeof(h));
		u32 at = ASSET_HEADER_BYTES + asset_palette_bytes(&h);
		if (at + 4 <= size)
			memcpy(&lz_word, bytes + at, 4);
	} else
		memset(&h, 0, sizeof(h));
	int check = asset_check(&h, size, lz_word);
	if (check != ASSET_OK) {
		snprintf(msg, cap, "%s: wallpaper %s: %s", folder, file, asset_error_text(check));
		return 0;
	}
	if (h.bpp != 4) {
		snprintf(msg, cap, "%s: wallpaper %s is %ubpp; it must be 4bpp", folder, file, h.bpp);
		return 0;
	}
	struct engine_budget need = {0}, have = {0};
	need.bg_tiles = (u16)(h.width * h.height);
	need.bg_maps = 1;
	need.bg_banks = 1;
	have.bg_tiles = THEME_WALLPAPER_TILES;
	have.bg_maps = 1;
	have.bg_banks = 1;
	char why[80];
	if (budget_check(&need, &have, why, sizeof(why)) != BUDGET_OK) {
		snprintf(msg, cap, "%s: wallpaper %s is over budget: %s", folder, file, why);
		return 0;
	}
	return 1;
}
