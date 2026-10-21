// Generic dialogs (dialog.h): one modal view, in four uses, drawn as a
// floating card of the Paper clean look (B9's widgets): the title in the
// heading font, the text, and the key chips or the loading card's bar and
// status line.
#include <gba_input.h>  // KEY_A, KEY_B
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

// --- Look, px: a card centred on the screen ----------------------------------
#define CARD_W 208
#define CARD_H 96
#define TEXT_X 8          // the texts' and the bar's inset from the card's sides
#define TEXT_W (CARD_W - 2 * TEXT_X)
#define TITLE_Y 8
#define TITLE_H 16
#define TEXT_Y 24
#define TEXT_H 48         // a message's or question's text: 4 lines
#define PROGRESS_TEXT_H 40  // a progress dialog's, above its bar
#define BAR_Y 64
#define BAR_H 8
#define FOOT_Y 72         // the key chips, or the progress dialog's status line
#define FOOT_H 16
// A floating card: a hairline round it and its shadow a pixel below
static const struct ui_card_config card = {0, 0, 0, 1, UI_CARD_EDGE};

#define TITLE_MAX 48
#define TEXT_MAX 160
#define STATUS_MAX 48
#define TEXT_LINES 4      // a message's or question's text (48 px)
#define PROGRESS_LINES 3  // a progress dialog's, above its bar (40 px)
#define HINTS_MAX 2

enum {
	N_CARD,
	N_TITLE,
	N_TEXT,
	N_FOOT,  // the key chips, or a progress dialog's status line
	N_BAR,   // a progress dialog's only
	N_COUNT
};

enum {
	USE_NONE,
	USE_MESSAGE,
	USE_CONFIRM,
	USE_PROGRESS,
	USE_NOTICE,
};

static const struct ui_label_config title_label = {"", UI_FONT_HEADING, UI_ALIGN_CENTER, 0, 0};
static const struct ui_label_config text_label = {"", UI_FONT_BODY, UI_ALIGN_CENTER, 0, 0};
static const struct ui_label_config status_label = {"", UI_FONT_BODY, UI_ALIGN_CENTER, 0, 0};
static const struct ui_hints_config no_hints = {0, 0};

// Everything on plane 0 (overlay and dialog text), so the screen under it
// keeps its other planes; each node paints the card's colour behind what it
// draws. Nothing is hidden: a hidden node blanks its rectangle in the
// plane's map, a hole in the card. A title or text left out is an empty
// label, and only the progress dialog has a bar.
#define DIALOG_NODES(text_ink, text_h) \
	[N_CARD] = {.kind = UI_CARD, \
		    .id = 1, \
		    .parent = UI_NONE, \
		    .plane = 0, \
		    .anchor = UI_HCENTER | UI_VCENTER, \
		    .paper = UI_ROLE_FLOAT, \
		    .w = CARD_W, \
		    .h = CARD_H, \
		    .data = &card}, \
	[N_TITLE] = {.kind = UI_LABEL, \
		     .id = 2, \
		     .parent = N_CARD, \
		     .plane = 0, \
		     .anchor = UI_HFILL, \
		     .paper = UI_ROLE_FLOAT, \
		     .x = TEXT_X, \
		     .y = TITLE_Y, \
		     .w = TEXT_X, \
		     .h = TITLE_H, \
		     .data = &title_label}, \
	[N_TEXT] = {.kind = UI_LABEL, \
		    .id = 3, \
		    .parent = N_CARD, \
		    .plane = 0, \
		    .anchor = UI_HFILL, \
		    .ink = (text_ink), \
		    .paper = UI_ROLE_FLOAT, \
		    .x = TEXT_X, \
		    .y = TEXT_Y, \
		    .w = TEXT_X, \
		    .h = (text_h), \
		    .data = &text_label}

#define KEYS_NODE \
	[N_FOOT] = {.kind = UI_HINTS, \
		    .id = 4, \
		    .parent = N_CARD, \
		    .plane = 0, \
		    .anchor = UI_HFILL, \
		    .paper = UI_ROLE_FLOAT, \
		    .x = TEXT_X, \
		    .y = FOOT_Y, \
		    .w = TEXT_X, \
		    .h = FOOT_H, \
		    .data = &no_hints}

static const struct ui_spec message_nodes[N_BAR] = {DIALOG_NODES(UI_ROLE_TEXT, TEXT_H), KEYS_NODE};
static const struct ui_spec error_nodes[N_BAR] = {DIALOG_NODES(UI_ROLE_DANGER, TEXT_H), KEYS_NODE};
static const struct ui_spec progress_nodes[N_COUNT] = {
	DIALOG_NODES(UI_ROLE_TEXT, PROGRESS_TEXT_H),
	[N_FOOT] = {.kind = UI_LABEL,
		    .id = 4,
		    .parent = N_CARD,
		    .plane = 0,
		    .anchor = UI_HFILL,
		    .ink = UI_ROLE_DIM,
		    .paper = UI_ROLE_FLOAT,
		    .x = TEXT_X,
		    .y = FOOT_Y,
		    .w = TEXT_X,
		    .h = FOOT_H,
		    .data = &status_label},
	[N_BAR] = {.kind = UI_PROGRESS,
		   .id = 5,
		   .parent = N_CARD,
		   .plane = 0,
		   .anchor = UI_HFILL,
		   .paper = UI_ROLE_FLOAT,
		   .x = TEXT_X,
		   .y = BAR_Y,
		   .w = TEXT_X,
		   .h = BAR_H},
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
static const struct ui_view_spec notice_view = {.name = "notice",
						.nodes = message_nodes,
						.count = N_BAR,
						.id = 30,
						.focus = UI_NONE,
						.flags = UI_VIEW_MODAL,
						.event = dialog_event};
static const struct ui_view_spec notice_error_view = {.name = "notice",
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
	char status[STATUS_MAX];  // a progress dialog's
	struct ui_hint hints[HINTS_MAX];  // a message's or question's key chips
};

static struct {
	u8 opens;  // dialog_open calls not yet closed
	u8 owned;  // the session is the dialogs': the last close shuts the engine down
	u32 last_pump;
	struct shown message;   // a message or question
	struct shown progress;  // the progress dialog, under a message if both are up
	struct shown notice;    // a notice, under the other two
	u32 value, max;
	u8 bar_changed, status_changed;
	void (*tick)(void);  // dialog_set_tick's
	u32 last_tick;
} dlg EWRAM_BSS;

static int dialog_event(struct ui_view *view, const struct ui_event *e)
{
	if (e->type != UI_EV_KEYS)
		return 0;
	struct shown *s = view == dlg.message.view  ? &dlg.message
			  : view == dlg.notice.view ? &dlg.notice
						    : &dlg.progress;
	s->keys |= e->keys;
	return 1;
}

// --- The session ---------------------------------------------------------

void dialog_open(void)
{
	dlg.opens++;
	if (dlg.opens == 1 && !ui_active()) {
		// From plain code: a black screen while the engine sets up (mode 0
		// with nothing on shows the backdrop), so no frame shows what VRAM
		// held read as tiles
		dialog_blank();
		engine_init();
		ui_init(NULL);
		dlg.owned = 1;
		tlog_debug("dialog: the engine runs the dialogs");
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
	dialog_blank();
	tlog_debug("dialog: engine session over");
}

void dialog_hand_over(void)
{
	if (!dlg.owned || !dlg.opens)
		return;
	// The notice fades to black. A frame first: the boot's work since the
	// last one would count as the fade's time, and it cut out
	if (ui_depth())
		ui_frame();
	ui_leave(UI_TR_LEAVE, 0);
	while (ui_depth())
		ui_pop();
	memset(&dlg.message, 0, sizeof(dlg.message));
	memset(&dlg.progress, 0, sizeof(dlg.progress));
	memset(&dlg.notice, 0, sizeof(dlg.notice));
	dlg.opens = 0;
	dlg.owned = 0;
	// What a new session would read
	if (ui_theme_setting() != ui_theme())
		ui_theme_set(ui_theme_setting());
	ui_dim_set_minutes(ui_dim_setting());
	tlog_debug("dialog: engine session handed over");
}

void dialog_blank(void)
{
	VBlankIntrWait();
	BG_PALETTE_[0] = 0;
	REG_DISPCNT_ = 0;
	// BG control and scroll, windows, mosaic and blending (BG2's and BG3's
	// affine registers only matter in modes 1 and 2, which nothing uses)
	for (vu16 *r = (vu16 *)0x04000008; r < (vu16 *)0x04000020; r++)
		*r = 0;
	for (vu16 *r = (vu16 *)0x04000040; r < (vu16 *)0x04000056; r++)
		*r = 0;
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

void dialog_set_tick(void (*fn)(void))
{
	dlg.tick = fn;
}

// The screen under the dialogs gets its tick, before a frame draws what it set
static void tick(void)
{
	if (dlg.tick && torii_frames - dlg.last_tick >= DIALOG_PROGRESS_FRAMES) {
		dlg.last_tick = torii_frames;
		dlg.tick();
	}
}

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
		tick();
		ui_frame();
		if (!dirty(s) && (s->view->flags & UI_V_READY))
			break;
	}
	dlg.last_pump = torii_frames;
}

// Pushes the dialog view for s with its texts; 0 if it can't. A message
// shows an OK chip, a question a cancel chip and an OK chip
static int show(struct shown *s, u32 use, const char *title, const char *text, u32 kind)
{
	copy(s->title, sizeof(s->title), title);
	wrap(s->text, sizeof(s->text), text ? text : "", use == USE_PROGRESS ? PROGRESS_LINES : TEXT_LINES);
	s->status[0] = 0;
	s->keys = 0;
	s->use = (u8)use;
	const struct ui_view_spec *spec = use == USE_PROGRESS ? &progress_view
					  : use == USE_NOTICE
						  ? (kind == DIALOG_ERROR ? &notice_error_view : &notice_view)
					  : kind == DIALOG_ERROR ? &error_view
								 : &message_view;
	// Over nothing (the boot's notice, the loading card, a launch's
	// questions) it comes in from black rising, as a screen does (E9): a
	// dialog's own fade needs a screen under it
	if (!ui_depth() && !ui_transitioning())
		ui_enter(UI_TR_CARD, 1);
	s->view = ui_push(spec, 0);
	if (!s->view) {
		tlog_error("dialog: %s", ui_error());
		s->use = USE_NONE;
		return 0;
	}
	u8 first = s->view->first;
	ui_label_set((u8)(first + N_TITLE), s->title);
	ui_label_set((u8)(first + N_TEXT), s->text);
	if (use == USE_PROGRESS) {
		ui_label_set((u8)(first + N_FOOT), s->status);
	} else if (use == USE_CONFIRM) {
		s->hints[0] = (struct ui_hint){"B", gl_hint_cancel};
		s->hints[1] = (struct ui_hint){"A", gl_hint_ok};
		ui_hints_set((u8)(first + N_FOOT), s->hints, 2);
	} else if (use == USE_MESSAGE) {
		s->hints[0] = (struct ui_hint){"A", gl_hint_ok};
		ui_hints_set((u8)(first + N_FOOT), s->hints, 1);
	}
	return 1;
}

static void hide(struct shown *s)
{
	if (!s->view)
		return;
	// The top view is s (dialogs pop in the order they were pushed). Over
	// a screen it closes with motion first, keys meanwhile held for it
	TLOG_ASSERT(ui_top() == s->view);
	if (ui_top() == s->view)
		ui_close();
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
		tick();
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
	if (show(&dlg.message, USE_MESSAGE, title, text, kind)) {
		// A or B. A pressed while it is still opening never reaches it
		// (ui_frame), so a quick double A can't close it unseen
		wait_key(&dlg.message, KEY_A | KEY_B, 1);
		hide(&dlg.message);
	}
	dialog_close();
}

int dialog_confirm(const char *title, const char *text, u32 kind)
{
	int yes = 0;
	dialog_open();
	if (show(&dlg.message, USE_CONFIRM, title, text, kind)) {
		yes = (wait_key(&dlg.message, KEY_A | KEY_B, 1) & KEY_A) != 0;
		hide(&dlg.message);
	}
	log_dialog(title, text, yes ? "A, yes" : "B, no");
	dialog_close();
	return yes;
}

// --- Notices -----------------------------------------------------------------

void dialog_notice(const char *title, const char *text, u32 kind)
{
	log_dialog(title, text, NULL);
	if (dlg.notice.view) {
		// Replaced: popped without a frame, so nothing shows in between
		TLOG_ASSERT(ui_top() == dlg.notice.view);
		if (ui_top() == dlg.notice.view)
			ui_pop();
		dlg.notice.view = 0;
	} else {
		dialog_open();
	}
	if (!show(&dlg.notice, USE_NOTICE, title, text, kind)) {
		dialog_close();
		return;
	}
	settle(&dlg.notice, 30);
}

void dialog_notice_end(void)
{
	if (!dlg.notice.view)
		return;
	hide(&dlg.notice);
	dialog_close();
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
	if (!show(&dlg.progress, USE_PROGRESS, title, text, DIALOG_INFO)) {
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
		ui_label_set((u8)(first + N_FOOT), dlg.progress.status);
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
	copy(dlg.progress.status, sizeof(dlg.progress.status), status);
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
	if (status && strncmp(status, dlg.progress.status, sizeof(dlg.progress.status) - 1) != 0) {
		copy(dlg.progress.status, sizeof(dlg.progress.status), status);
		dlg.status_changed = 1;
	}
	if (torii_frames - dlg.last_pump < DIALOG_PROGRESS_FRAMES)
		return;
	// One frame: what it draws lands in the next one's VBlank
	apply();
	tick();
	ui_frame();
	dlg.last_pump = torii_frames;
}

int dialog_progress_pixels(u32 value, struct ui_bar_pixels *out)
{
	if (!dialog_progress_active())
		return 0;
	apply();
	settle(&dlg.progress, 4);  // a frame at least: what apply set is drawn
	return ui_progress_pixels((u8)(dlg.progress.view->first + N_BAR), value, dlg.max, out);
}

void dialog_progress_drew(u32 x)
{
	if (dialog_progress_active())
		ui_progress_drew((u8)(dlg.progress.view->first + N_BAR), x);
}

void dialog_progress_end(void)
{
	if (!dialog_progress_active())
		return;
	hide(&dlg.progress);
	dialog_close();
}
