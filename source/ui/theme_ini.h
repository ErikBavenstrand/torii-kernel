// theme.ini, the text half of an SD theme (v0.3.1, docs/plans/v0.3-decisions.md
// "T14 Themes" has the full key reference). Hardware-free: tests/host runs
// it under ASan and UBSan, and ui/ui_sdtheme.c loads it on the cart.
//
//	version = 1          ; required: the format's version
//	name = Night         ; shown in settings (default: the folder's name)
//	base = dark          ; the built-in theme every missing key keeps
//	[colours]            ; #RRGGBB, rounded down to 15 bits
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
	s32 version;  // as read; 0 if missing, -1 if not a number
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

// A view name patches may use instead of its number (ui_view_spec.id), and
// the table of them. Screens add a line for theirs.
struct theme_view_name {
	const char *name;
	u8 id;
};
extern const struct theme_view_name theme_view_names[];
extern const u32 theme_view_name_count;

#endif
