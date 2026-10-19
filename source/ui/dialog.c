// Generic dialogs (dialog.h): one modal view, in three uses.
#include <gba_input.h>
#include <gba_systemcalls.h>
#include <stdio.h>
#include <string.h>

#include "dialog.h"
#include "ui.h"
#include "../engine/engine.h"
#include "../engine/text.h"
#include "../lang.h"
#include "../tlog.h"

#define REG_DISPCNT_ (*(vu16 *)0x04000000)
#define BG_PALETTE_ ((vu16 *)0x05000000)
#define DISP_MODE3_BG2 0x0403

#define PANEL_W 208
#define TEXT_W 192  // the labels' width: the panel less 8 px each side
#define TITLE_MAX 48
#define TEXT_MAX 160
#define STATUS_MAX 48
#define TEXT_LINES 4      // a message's or question's text (48 px)
#define PROGRESS_LINES 3  // a progress dialog's, above its bar (40 px)

enum {
	N_PANEL,
	N_TITLE,
	N_TEXT,
	N_HINT,  // the keys, or a progress dialog's status line
	N_BAR,   // a progress dialog's only
	N_COUNT
};

enum {
	USE_NONE,
	USE_MESSAGE,
	USE_CONFIRM,
	USE_PROGRESS,
};

static const struct ui_panel_config border_panel = {UI_PANEL_BORDER};
static const struct ui_label_config title_label = {"", UI_FONT_HEADING, UI_ALIGN_CENTER, 0};
static const struct ui_label_config text_label = {"", UI_FONT_BODY, UI_ALIGN_CENTER, 0};
static const struct ui_label_config hint_label = {"", UI_FONT_BODY, UI_ALIGN_CENTER, 0};

// Everything on plane 0 (overlay and dialog text), so the screen under it
// keeps its other planes. Nothing is hidden: a hidden node blanks its
// rectangle in the plane's map, a hole in the panel. A title or text left
// out is an empty label, and only the progress dialog has a bar.
#define DIALOG_NODES(text_ink, text_h) \
	[N_PANEL] = {.kind = UI_PANEL, \
		     .id = 1, \
		     .parent = UI_NONE, \
		     .plane = 0, \
		     .anchor = UI_HCENTER | UI_VCENTER, \
		     .paper = UI_ROLE_PANEL, \
		     .w = PANEL_W, \
		     .h = 96, \
		     .data = &border_panel}, \
	[N_TITLE] = {.kind = UI_LABEL, \
		     .id = 2, \
		     .parent = N_PANEL, \
		     .plane = 0, \
		     .anchor = UI_HFILL, \
		     .paper = UI_ROLE_PANEL, \
		     .x = 8, \
		     .y = 8, \
		     .w = 8, \
		     .h = 16, \
		     .data = &title_label}, \
	[N_TEXT] = {.kind = UI_LABEL, \
		    .id = 3, \
		    .parent = N_PANEL, \
		    .plane = 0, \
		    .anchor = UI_HFILL, \
		    .ink = (text_ink), \
		    .paper = UI_ROLE_PANEL, \
		    .x = 8, \
		    .y = 24, \
		    .w = 8, \
		    .h = (text_h), \
		    .data = &text_label}, \
	[N_HINT] = {.kind = UI_LABEL, \
		    .id = 4, \
		    .parent = N_PANEL, \
		    .plane = 0, \
		    .anchor = UI_HFILL, \
		    .ink = UI_ROLE_DIM, \
		    .paper = UI_ROLE_PANEL, \
		    .x = 8, \
		    .y = 72, \
		    .w = 8, \
		    .h = 16, \
		    .data = &hint_label}

static const struct ui_spec message_nodes[N_BAR] = {DIALOG_NODES(UI_ROLE_TEXT, 48)};
static const struct ui_spec error_nodes[N_BAR] = {DIALOG_NODES(UI_ROLE_ICON + 3, 48)};  // red
static const struct ui_spec progress_nodes[N_COUNT] = {
	DIALOG_NODES(UI_ROLE_TEXT, 40),
	[N_BAR] = {.kind = UI_PROGRESS,
		   .id = 5,
		   .parent = N_PANEL,
		   .plane = 0,
		   .anchor = UI_HFILL,
		   .x = 8,
		   .y = 64,
		   .w = 8,
		   .h = 8},
};

static int dialog_event(struct ui_view *view, const struct ui_event *e);

static const struct ui_view_spec message_view = {.name = "dialog",
						 .nodes = message_nodes,
						 .count = N_BAR,
						 .id = 30,
						 .focus = UI_NONE,
						 .flags = UI_VIEW_MODAL,
						 .event = dialog_event};
static const struct ui_view_spec error_view = {.name = "dialog",
					       .nodes = error_nodes,
					       .count = N_BAR,
					       .id = 30,
					       .focus = UI_NONE,
					       .flags = UI_VIEW_MODAL,
					       .event = dialog_event};
static const struct ui_view_spec progress_view = {.name = "progress",
						  .nodes = progress_nodes,
						  .count = N_COUNT,
						  .id = 31,
						  .focus = UI_NONE,
						  .flags = UI_VIEW_MODAL,
						  .event = dialog_event};

// A shown dialog: its view and texts (labels draw from these buffers)
struct shown {
	struct ui_view *view;
	u16 keys;  // fired while it was up
	u8 use;
	char title[TITLE_MAX];
	char text[TEXT_MAX];
	char hint[STATUS_MAX];
};

static struct {
	u8 opens;  // dialog_open calls not yet closed
	u8 owned;  // the session is the dialogs': mode 3 comes back at the last close
	u32 last_pump;
	struct shown message;   // a message or question
	struct shown progress;  // the progress dialog, under a message if both are up
	u32 value, max;
	u8 bar_changed, status_changed;
} dlg EWRAM_BSS;

static int dialog_event(struct ui_view *view, const struct ui_event *e)
{
	if (e->type != UI_EV_KEYS)
		return 0;
	struct shown *s = view == dlg.message.view ? &dlg.message : &dlg.progress;
	s->keys |= e->keys;
	return 1;
}

// --- The session ---------------------------------------------------------

void dialog_open(void)
{
	dlg.opens++;
	if (dlg.opens == 1 && ui_depth() == 0) {
		// From mode 3: a black screen while the engine sets up (mode 0 with
		// nothing on shows the backdrop), so no frame shows mode 3's picture
		// read as tiles
		VBlankIntrWait();
		BG_PALETTE_[0] = 0;
		REG_DISPCNT_ = 0;
		engine_init();
		ui_init(NULL);
		dlg.owned = 1;
		tlog_debug("dialog: the engine runs the dialogs (from mode 3)");
	}
	// No dimming over a question or a 4-minute erase, over an engine screen
	// too (dialog_close gives it back the DIM setting)
	if (dlg.opens == 1)
		ui_dim_set_frames(0);
}

void dialog_close(void)
{
	if (!dlg.opens)
		return;
	dlg.opens--;
	if (dlg.opens)
		return;
	if (!dlg.owned) {
		ui_dim_set_minutes(ui_dim_setting());
		return;
	}
	dlg.owned = 0;
	ui_shutdown();
	dialog_mode3();
	tlog_debug("dialog: back to mode 3");
}

void dialog_mode3(void)
{
	VBlankIntrWait();
	BG_PALETTE_[0] = 0;
	REG_DISPCNT_ = 0;
	// The registers the engine writes, as the mode 3 menu has them: BG control
	// and scroll, windows, mosaic and blending (not BG2's affine ones, which
	// mode 3 uses and the engine leaves alone)
	for (vu16 *r = (vu16 *)0x04000008; r < (vu16 *)0x04000020; r++)
		*r = 0;
	for (vu16 *r = (vu16 *)0x04000040; r < (vu16 *)0x04000056; r++)
		*r = 0;
	u32 zero = 0;
	CpuFastSet(&zero, (void *)0x06000000, (240 * 160 * 2 / 4) | 1 << 24);
	VBlankIntrWait();
	REG_DISPCNT_ = DISP_MODE3_BG2;
	// libgba's keysDown and keysUp gather changes until they are read, not
	// until the next scanKeys: the engine read the keys itself, so read them
	// once now, or the menu takes a key that left the screen (a held L) for
	// a new press
	scanKeys();
	(void)keysDown();
	(void)keysUp();
	(void)keysDownRepeat();
}

// --- Text ----------------------------------------------------------------

// Copies text into out (size bytes), wrapping lines wider than the labels at
// a space (or anywhere), at most max_lines lines
static void wrap(char *out, u32 size, const char *text, u32 max_lines)
{
	const struct torii_font *font = ui_theme()->body;
	u32 o = 0, lines = 1;
	const char *line = text;
	while (*line && o + 1 < size) {
		// The longest part of this line that fits
		u32 len = 0, fit = 0, space = 0;
		while (line[len] && line[len] != '\n') {
			u32 next = len + 1;
			while ((line[next] & 0xC0) == 0x80)
				next++;  // a whole UTF-8 character
			if (text_width(font, line, next) > TEXT_W - 2)
				break;
			if (line[len] == ' ')
				space = len;
			len = next;
			fit = len;
		}
		u32 take = fit, skip = 0;
		if (line[fit] && line[fit] != '\n') {
			if (space) {
				take = space;  // break at the space, which goes
				skip = 1;
			}
			while (!take || (line[take] & 0xC0) == 0x80)
				take++;  // not even a character fits: one goes anyway
		} else if (line[fit] == '\n') {
			skip = 1;
		}
		if (take > size - 1 - o)
			take = size - 1 - o;
		memcpy(out + o, line, take);
		o += take;
		line += take + skip;
		if (!*line)
			break;
		if (lines == max_lines || o + 2 > size)
			break;
		out[o++] = '\n';
		lines++;
	}
	out[o] = 0;
}

static void copy(char *out, u32 size, const char *s)
{
	snprintf(out, size, "%s", s ? s : "");
}

// --- Frames ----------------------------------------------------------------

static int dirty(const struct shown *s)
{
	if (!s->view)
		return 0;
	if (!(s->view->flags & UI_V_SHOWN))
		return 1;
	for (u32 i = 0; i < s->view->count; i++)
		if (ui_node((u8)(s->view->first + i))->flags & UI_N_DIRTY)
			return 1;
	return 0;
}

// Runs frames until s is drawn, on screen and has logged "screen ready" (at
// most frames frames)
static void settle(const struct shown *s, u32 frames)
{
	while (frames--) {
		ui_frame();
		if (!dirty(s) && (s->view->flags & UI_V_READY))
			break;
	}
	dlg.last_pump = torii_frames;
}

// Pushes the dialog view for s with its texts; 0 if it can't
static int show(struct shown *s, u32 use, const char *title, const char *text, u32 kind, const char *hint)
{
	copy(s->title, sizeof(s->title), title);
	wrap(s->text, sizeof(s->text), text ? text : "", use == USE_PROGRESS ? PROGRESS_LINES : TEXT_LINES);
	copy(s->hint, sizeof(s->hint), hint);
	s->keys = 0;
	s->use = (u8)use;
	const struct ui_view_spec *spec = use == USE_PROGRESS    ? &progress_view
					  : kind == DIALOG_ERROR ? &error_view
								 : &message_view;
	s->view = ui_push(spec, 0);
	if (!s->view) {
		tlog_error("dialog: %s", ui_error());
		s->use = USE_NONE;
		return 0;
	}
	u8 first = s->view->first;
	ui_label_set((u8)(first + N_TITLE), s->title);
	ui_label_set((u8)(first + N_TEXT), s->text);
	ui_label_set((u8)(first + N_HINT), s->hint);
	return 1;
}

static void hide(struct shown *s)
{
	if (!s->view)
		return;
	// The top view is s (dialogs pop in the order they were pushed)
	TLOG_ASSERT(ui_top() == s->view);
	if (ui_top() == s->view)
		ui_pop();
	s->view = 0;
	s->use = USE_NONE;
	ui_frame();  // the view under it comes back
}

// Waits for a key in mask pressed while s is up; with release, also for it
// to be let go. Returns the key.
static u16 wait_key(struct shown *s, u16 mask, int release)
{
	u16 key = 0;
	for (;;) {
		ui_frame();
		if (!key && (s->keys & mask))
			key = s->keys & mask;
		if (key && (!release || !(ui_input()->held & key)))
			return key;
	}
}

// --- Messages and questions -------------------------------------------------

// Logs what a dialog says, on one line, and how it was answered
static void log_dialog(const char *title, const char *text, const char *answer)
{
	char line[TEXT_MAX];
	copy(line, sizeof(line), text);
	for (char *c = line; *c; c++)
		if (*c == '\n')
			*c = ' ';
	tlog("dialog: %s%s%s%s%s", title ? title : "", title ? ": " : "", line, answer ? ": " : "",
	     answer ? answer : "");
}

void dialog_message(const char *title, const char *text, u32 kind)
{
	log_dialog(title, text, NULL);
	dialog_open();
	if (show(&dlg.message, USE_MESSAGE, title, text, kind, gl_close_btn)) {
		wait_key(&dlg.message, KEY_B, 1);
		hide(&dlg.message);
	}
	dialog_close();
}

int dialog_confirm(const char *title, const char *text, u32 kind)
{
	int yes = 0;
	dialog_open();
	if (show(&dlg.message, USE_CONFIRM, title, text, kind, gl_menu_btn)) {
		yes = (wait_key(&dlg.message, KEY_A | KEY_B, 1) & KEY_A) != 0;
		hide(&dlg.message);
	}
	log_dialog(title, text, yes ? "A, yes" : "B, no");
	dialog_close();
	return yes;
}

// --- Progress --------------------------------------------------------------

void dialog_progress_begin(const char *title, const char *text)
{
	if (dlg.progress.use == USE_PROGRESS) {
		tlog_error("dialog: a progress dialog is already up");
		return;
	}
	dialog_open();
	dlg.value = dlg.max = 0;
	dlg.bar_changed = dlg.status_changed = 0;
	if (!show(&dlg.progress, USE_PROGRESS, title, text, DIALOG_INFO, NULL)) {
		dialog_close();
		return;
	}
	settle(&dlg.progress, 30);
}

int dialog_progress_active(void)
{
	return dlg.progress.use == USE_PROGRESS;
}

static void apply(void)
{
	u8 first = dlg.progress.view->first;
	if (dlg.status_changed)
		ui_label_set((u8)(first + N_HINT), dlg.progress.hint);
	if (dlg.bar_changed)
		ui_progress_set((u8)(first + N_BAR), dlg.value, dlg.max);
	dlg.status_changed = dlg.bar_changed = 0;
}

void dialog_progress_status(const char *status)
{
	if (!dialog_progress_active())
		dialog_progress_begin(NULL, NULL);
	if (!dialog_progress_active())
		return;
	copy(dlg.progress.hint, sizeof(dlg.progress.hint), status);
	dlg.status_changed = 1;
	apply();
	settle(&dlg.progress, 4);
}

void dialog_progress_set(u32 value, u32 max, const char *status)
{
	if (!dialog_progress_active())
		return;
	if (value != dlg.value || max != dlg.max) {
		dlg.value = value;
		dlg.max = max;
		dlg.bar_changed = 1;
	}
	if (status && strncmp(status, dlg.progress.hint, sizeof(dlg.progress.hint) - 1) != 0) {
		copy(dlg.progress.hint, sizeof(dlg.progress.hint), status);
		dlg.status_changed = 1;
	}
	if (torii_frames - dlg.last_pump < DIALOG_PROGRESS_FRAMES)
		return;
	// One frame: what it draws lands in the next one's VBlank
	apply();
	ui_frame();
	dlg.last_pump = torii_frames;
}

void dialog_progress_end(void)
{
	if (!dialog_progress_active())
		return;
	hide(&dlg.progress);
	dialog_close();
}

void ShowbootProgress(const char *str)
{
	dialog_progress_status(str);
}
