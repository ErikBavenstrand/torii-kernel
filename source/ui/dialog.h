// Generic dialogs on the engine (docs/plans/v0.3-decisions.md, T12, T13): a
// message, a yes/no question, a notice and a progress dialog, for every
// screen and for the plain code between screens (the boot, the firmware
// update, a launch).
//
// Each call runs frames until it is done (a loop of its own), so call it
// from a screen's loop or from plain code, never from inside ui_frame (an
// event handler or a widget): a handler sets a flag, and the screen's loop
// calls the dialog after ui_frame returns.
//
// - From an engine screen (ui_depth() > 0), the dialog is a modal view
//   pushed on top: it fades in and slides up as the screen under it dims
//   (theme dialog_in), and closes the same way backwards (dialog_out)
//   before the call returns. The screen's loop goes on as before.
// - From plain code (no view up), the call sets up the engine itself
//   (engine_init, ui_init with the THEME setting) and, when
//   done, shuts it down on a black screen (dialog_blank).
//
// dialog_open() and dialog_close() keep that engine session over several
// calls, so a question followed by a progress dialog doesn't flash black
// between them. They nest; the last close ends the session.
//
//	if (dialog_confirm(gl_save_sav, name, DIALOG_INFO)) {  // A: 1, B: 0
//		dialog_progress_begin(gl_saving, name);
//		dialog_progress_status(gl_save_ing);              // drawn at once
//		for (each block) {
//			dialog_progress_set(done, total, status_text);  // a few times a second
//			...
//		}
//		dialog_progress_end();
//	}
//	dialog_message(NULL, gl_error_1, DIALOG_ERROR);       // B closes it
//
// Text: title in the heading font (NULL: none), text in the body font,
// centred, with "\n" between lines; a line too wide wraps (at a space if
// it can), and lines past the fourth (the third in a progress dialog) are
// cut. Both are copied, so a stack buffer is fine. The view names are
// "dialog" (message and question), "notice" and "progress", logged as
// "screen ready: NAME" once shown. DIM is off while a dialog session is
// open; closing it gives an engine screen back the DIM setting.
#ifndef TORII_UI_DIALOG_H
#define TORII_UI_DIALOG_H

#include <gba_base.h>

// Message kinds: the text's colour
enum {
	DIALOG_INFO = 0,   // the theme's text colour
	DIALOG_ERROR = 1,  // red (the theme's icon colour 3)
};

// Keeps (or starts) the engine session dialogs run in; see the top.
void dialog_open(void);
// Ends what dialog_open started: after the last close, a session the
// dialogs started shuts the engine down on a black screen.
void dialog_close(void);

// Ends a session the dialogs started (the boot's) without ending the engine:
// the screen goes to black (ui_leave), every dialog goes, and the session
// is the caller's from then on (the menu's), with the THEME and DIM
// settings applied as a new session would. Nothing if no such session runs.
void dialog_hand_over(void);

// The screen between two engine sessions, after ui_shutdown: black (mode 0
// with no layer on, a black backdrop), and the registers the engine writes
// (BG control and scroll, windows, mosaic, blending) cleared, so the next
// session, or a game, starts from a blank display. Every screen that ends
// its session calls it, and Diagnostics before its unit tests, which expect
// those registers cleared. The next session's ui_init counts keys still
// held as held, so a key that closed a screen doesn't act again.
void dialog_blank(void);

// Shows text until A or B is pressed and let go, with an "A OK" key chip.
void dialog_message(const char *title, const char *text, u32 kind);
// Asks a yes/no question: A (1) or B (0), once the key is released, with
// "B cancel" and "A OK" key chips.
int dialog_confirm(const char *title, const char *text, u32 kind);

// A notice: a message with no keys, which stays up while the caller works
// (the boot's "Loading...") until dialog_notice_end. Showing another while
// one is up replaces it. Messages, questions and a progress dialog shown
// meanwhile go on top of it; take those down first.
void dialog_notice(const char *title, const char *text, u32 kind);
void dialog_notice_end(void);

// A progress dialog: title and text above, a status line and a bar below.
// It stays up, with no keys, until dialog_progress_end; the work runs in
// between, and the calls below draw it. Messages and questions shown
// meanwhile go on top of it.
void dialog_progress_begin(const char *title, const char *text);
// Sets the status line and draws it now (a frame or two): for a new phase
// of the work ("CHECKING SAV FILE").
void dialog_progress_status(const char *status);
// Fills the bar to value of max and sets the status line (NULL: keep it).
// Cheap to call often: it draws at most every DIALOG_PROGRESS_FRAMES
// VBlanks (5 times a second), waiting at most one frame for it, so a
// loop over 128 KB blocks loses about 3% of its time to it.
void dialog_progress_set(u32 value, u32 max, const char *status);
// Takes the progress dialog down (and ends the session if it started it).
// Does nothing if none is up, so a caller can end one that may not have
// begun.
void dialog_progress_end(void);
int dialog_progress_active(void);
// For filling the bar from an interrupt while the kernel waits in IWRAM
// (progress.c, during the cart's ROM copy): draws what is pending, then
// gives the bar's pixels (ui_progress_pixels) with the column value (of the
// last dialog_progress_set's max) fills to. 0 if no progress dialog is up
// or its bar isn't drawn.
struct ui_bar_pixels;
int dialog_progress_pixels(u32 value, struct ui_bar_pixels *out);
// That fill now ends at column x: the bar goes on from there.
void dialog_progress_drew(u32 x);

#define DIALOG_PROGRESS_FRAMES 12

// A function the dialogs call every DIALOG_PROGRESS_FRAMES or so while they
// hold the screen (NULL: none), for the screen under them, whose own loop
// waits meanwhile: the menu keeps its clock going under a question or a
// 4-minute erase. It runs before a frame, from the dialogs' loops, never
// from inside ui_frame; it must check that its screen is still up.
void dialog_set_tick(void (*tick)(void));

#endif
