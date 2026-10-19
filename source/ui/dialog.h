// Generic dialogs on the engine (docs/plans/v0.3-decisions.md, T12): a
// message, a yes/no question and a progress dialog, for every screen, ported
// or still in mode 3.
//
// Each call runs frames until it is done (a loop of its own), so call it
// from a screen's loop or from plain code, never from inside ui_frame (an
// event handler or a widget): a handler sets a flag, and the screen's loop
// calls the dialog after ui_frame returns.
//
// - From an engine screen (ui_depth() > 0), the dialog is a modal view
//   pushed on top, which slides in over the dimmed screen, and it is popped
//   before the call returns. The screen's loop goes on as before.
// - From mode 3 code (no view up), the call sets up the engine itself
//   (engine_init, ui_init with the THEME and ACCENT settings) and, when
//   done, puts mode 3 back on a black screen and syncs libgba's key state
//   (scanKeys), so a key that closed the dialog doesn't act again: the
//   caller repaints its screen, as after any engine screen.
//
// dialog_open() and dialog_close() keep that engine session over several
// calls, so a question followed by a progress dialog doesn't flash back to
// mode 3 between them. They nest; the last close ends the session.
//
//	if (dialog_confirm(gl_save_sav, name, DIALOG_INFO)) {  // A: 1, B: 0
//		dialog_progress_begin(gl_save_sav, name);
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
// "dialog" (message and question) and "progress", logged as "screen
// ready: NAME" once shown. DIM is off while a dialog session is open;
// closing it gives an engine screen back the DIM setting.
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
// dialogs started returns to mode 3 (black screen; the caller repaints).
void dialog_close(void);

// Puts the display back as the mode 3 menu has it, after ui_shutdown: a
// black screen in mode 3, and the registers the engine writes cleared. Also
// syncs libgba's key state (scanKeys). dialog_close calls it; Diagnostics
// too, before its unit tests, which expect those registers as mode 3 left
// them.
void dialog_mode3(void);

// Shows text until B is pressed and let go (as the mode 3 menu's
// wait_btn). Hint: gl_close_btn, "[B]OK".
void dialog_message(const char *title, const char *text, u32 kind);
// Asks a yes/no question: A (1) or B (0), on the press. Hint: gl_menu_btn,
// "[B]CANCEL [A]OK".
int dialog_confirm(const char *title, const char *text, u32 kind);

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

#define DIALOG_PROGRESS_FRAMES 12

// The launch's progress line (the mode 3 kernel's name for it): the status
// line of the progress dialog, which it begins if none is up.
void ShowbootProgress(const char *str);

#endif
