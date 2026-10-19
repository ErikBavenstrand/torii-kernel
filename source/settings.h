// SETTINGS (two pages) and HELP on the v0.3 engine: setwindow.c runs them
// and draws page 1, setwindow2.c page 2, helpwindow.c HELP.
//
// A settings page is a header and a list with a row per setting: its name
// at the left, its value at the right. The keys are the official kernel's:
// UP and DOWN pick a setting, A edits it, and in edit mode LEFT and RIGHT
// move between its options and OK, A toggles the option under the cursor or,
// on OK, saves (only OK leaves edit mode). The value of the row being edited
// shows the option under the cursor between guillemets ("« RESET: ON »"),
// or, for the clock and the hot keys, the whole value with that field in
// brackets. L and R turn the page outside edit mode. Settings are saved
// only through save_setw_info and save_set2_info, the official writers,
// which stage them (Stage_SET_info): Settings_run writes the kernel NOR once
// as it returns, if a setting changed.
#ifndef TORII_SETTINGS_H
#define TORII_SETTINGS_H

#include <gba_base.h>

#include "ui/ui.h"

enum {
	SETTINGS_PAGE1,
	SETTINGS_PAGE2,
	SETTINGS_HELP,
	SETTINGS_AT_DIAGNOSE,  // for Settings_run: page 2, its cursor on DIAGNOSE
};

// What Settings_run returns: where the menu goes next
#define SETTINGS_TO_NOR 0        // L on page 1: the NOR list
#define SETTINGS_TO_DIAGNOSE 2   // DIAGNOSE on page 2

// Runs SETTINGS and HELP from page until the user leaves them. Over an
// engine session (the menu's; pop its views first) it runs in it and
// returns with no view up and the screen gone (ui_leave), for the caller's
// next one to come in; otherwise in one of its own, returning on a black
// screen (dialog_blank).
u32 Settings_run(u32 page);

// --- For the pages (setwindow.c, setwindow2.c, helpwindow.c) --------------

// A page. Its view's ctx is the page; the list (pages with rows) is node
// SETTINGS_LIST of its layout, under the header (SETTINGS_HEADER).
struct settings_page {
	const struct ui_view_spec *view;
	void (*enter)(struct ui_view *view);  // reads the settings, as it opens
	u32 (*count)(void);                   // rows (NULL: no list)
	// Row row's name and value, the value into value (size bytes)
	void (*row)(u32 row, const char **name, char *value, u32 size);
	u32 (*cursor)(void);  // the row the cursor is on
	void (*poll)(void);   // once a frame (NULL: none)
};
extern const struct settings_page settings_page1, settings_page2, settings_help;
// 1 while page 2 is to open with its cursor on DIAGNOSE (SETTINGS_AT_DIAGNOSE):
// its enter takes it
extern u8 settings_at_diagnose;

#define SETTINGS_LIST 3
#define SETTINGS_ROWS_MAX 12
#define SETTINGS_ROW_H 16
#define SETTINGS_NAME_W 64  // px of a row a name may use

// The header, the first three nodes of each page: a panel, the title at its
// left and a label at its right (set them with settings_header).
// clang-format off
#define SETTINGS_HEADER \
	{.kind = UI_PANEL, .id = 1, .parent = UI_NONE, .plane = 2, .anchor = UI_HFILL, \
	 .paper = UI_ROLE_PANEL, .h = 16}, \
	{.kind = UI_LABEL, .id = 2, .parent = 0, .plane = 2, .anchor = UI_LEFT, .ink = UI_ROLE_TEXT, \
	 .paper = UI_ROLE_PANEL, .w = 160, .h = 16, .data = &settings_title_config}, \
	{.kind = UI_LABEL, .id = 3, .parent = 0, .plane = 2, .anchor = UI_RIGHT, .ink = UI_ROLE_DIM, \
	 .paper = UI_ROLE_PANEL, .w = 80, .h = 16, .data = &settings_corner_config}
// clang-format on
extern const struct ui_label_config settings_title_config, settings_corner_config;
extern const struct ui_list_model settings_model;

// A key event's keys, L and R only on the frame they go down: the engine
// repeats held L and R (for paging lists), which would turn pages on and
// leave SETTINGS
u16 settings_keys(const struct ui_event *e);
// Sets the header's title and right-hand label (kept, as ui_label_set keeps them)
void settings_header(struct ui_view *view, const char *title, const char *corner);
// From a view's handler: replaces it with page, after the frame
void settings_open(u32 page);
// From a view's handler: pops it after the frame, and Settings_run returns result
void settings_leave(u32 result);
// Redraws the rows whose name or value changed and moves the list's
// cursor to the page's (after a key, or when a value changes by itself)
void settings_sync(void);

// Value helpers: name without the official kernel's padding spaces; "« TEXT »"
// (state -1) or "« TEXT: ON »" for the option under the edit cursor; and
// appending to a value: ", "-separated names, then settings_none
const char *settings_name(const char *name);
void settings_token(char *value, u32 size, const char *text, int state);
void settings_add(char *value, u32 size, const char *sep, const char *text);
void settings_none(char *value, u32 size);  // OFF if value is still empty

#endif
