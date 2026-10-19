// theme.ini, the text half of an SD theme (v0.3.1; docs/themes.md has the
// full key reference). Hardware-free: tests/host runs it under ASan and
// UBSan, ui/ui_sdtheme.c loads it on the cart, and tools/themecheck.c builds
// it into `just theme-check`.
//
//	version = 1          ; required: the format's version
//	name = Night         ; shown in settings (default: the folder's name)
//	base = dark          ; the built-in theme every missing key keeps
//	[colours]            ; #RRGGBB, rounded down to 15 bits (only ';' starts
//	                     ; a comment after a value, as colours start with '#')
//	bg = #101418
//	[patches]            ; VIEW.NODE.FIELD = VALUE, at most 16
//	ui-demo.4.x = 16
//
// Every key is optional apart from version: a key that is missing, unknown
// or unreadable keeps the base theme's value. A number out of range is
// clamped to it. Each such problem is a warning (counted, the first kept as a
// sentence, all logged by the loader); only a file that isn't a version 1
// theme is refused.
#ifndef TORII_UI_THEME_INI_H
#define TORII_UI_THEME_INI_H

#include <gba_base.h>

#include "ui.h"

#define THEME_VERSION 1
#define THEME_VERSION_NAN (-1000000)  // parse_int reads at most 6 digits
#define THEME_NAME_MAX 24  // bytes with the NUL: a theme's name and its folder's
#define THEME_FILE_MAX 32  // an asset's file name in the theme's folder, with the NUL
#define THEME_INI_MAX 0x2000  // bytes of theme.ini read
#define THEME_LINE_MAX 255  // longer lines are skipped
#define THEME_MESSAGE_MAX 96

enum {
	THEME_OK = 0,
	THEME_ERR_VERSION,  // no "version = 1", or another version
	THEME_ERR_SIZE,     // longer than THEME_INI_MAX
};

struct theme_ini {
	struct ui_theme theme;  // the result; theme.name points at name
	const struct ui_theme *base;  // the built-in it started from
	char name[THEME_NAME_MAX];  // "" if the file names none
	char wallpaper[THEME_FILE_MAX];  // [wallpaper] file, "" none
	u16 warnings;
	s32 version;  // as read, THEME_VERSION_NAN if not a number
	u8 has_version;  // a version line was found
	char message[THEME_MESSAGE_MAX];  // the error, or the first warning ("" none)
	// Called with each warning as it is found (a sentence starting "line N:");
	// may be NULL. theme_ini_parse keeps it.
	void (*on_warning)(const char *message);
};

// Parses len bytes of text (no NUL needed; a UTF-8 BOM, CRLF and CR line
// ends are fine) into out. Returns THEME_OK or why the file is refused, with
// a sentence in out->message. On THEME_OK out->theme is complete: the base
// theme with the file's keys applied.
int theme_ini_parse(const char *text, u32 len, struct theme_ini *out);

// 1 if name can be a theme's folder: 1 to THEME_NAME_MAX - 1 bytes of
// letters, digits, space, '-', '_' and '.', not starting with '.' or a space.
int theme_name_ok(const char *name);

// The wallpaper's budget: a 4bpp pattern of up to this many tiles, one map
// and one palette bank (tools/asset.py reads it from here)
#define THEME_WALLPAPER_TILES 256

// What ui_theme_load (ui/sdtheme.h) logs, after "theme: ". Here so that
// tools/themecheck.c, behind `just theme-check`, prints the same lines.
#define THEME_MSG_BAD_FOLDER "\"%s\" isn't a theme folder name"
#define THEME_MSG_NO_INI "%s has no theme.ini"
#define THEME_MSG_INI_BIG "%s: theme.ini is over 8 KB"
#define THEME_MSG_INI_READ "%s: theme.ini can't be read"
#define THEME_MSG_NO_WALLPAPER "%s: no wallpaper %s; none shown"
#define THEME_MSG_LOADED "loaded %s (%s, %u warnings%s)"

// Checks the wallpaper file of theme folder `folder` (size bytes at data;
// data is not read when size is over ASSET_FILE_MAX) as ui_theme_load does:
// a TAS1 asset, 4bpp, within THEME_WALLPAPER_TILES. Returns 1 if it can be
// used, or 0 with the reason in msg (cap bytes), "FOLDER: wallpaper FILE ...".
// It checks the header and the LZ77 header word only, not the stream.
int theme_wallpaper_check(const char *folder, const char *file, const void *data, u32 size, char *msg, u32 cap);

// A view name patches may use instead of its number (ui_view_spec.id), and
// the table of them. Screens add a line for theirs.
struct theme_view_name {
	const char *name;
	u8 id;
};
extern const struct theme_view_name theme_view_names[];
extern const u32 theme_view_name_count;

#endif
