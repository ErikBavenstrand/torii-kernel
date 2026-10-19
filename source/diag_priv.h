// What Diagnostics' files share (source/diag.c, source/diag_render.c): the
// screen's lines, steps, questions and the summary.
#ifndef TORII_DIAG_PRIV_H
#define TORII_DIAG_PRIV_H

#include <gba_base.h>

#include "ui/ui.h"

// A line's colour: a palette role of the theme (ui.h)
#define WHITE UI_ROLE_TEXT
#define GREY UI_ROLE_DIM
#define YELLOW (UI_ROLE_ICON + 0)  // the folder icon's amber
#define GREEN (UI_ROLE_ICON + 2)
#define RED (UI_ROLE_ICON + 3)

// Shows a line in colour (cut to the screen's width) and logs it whole; a
// red line logs as an error.
void diag_say(u8 colour, const char *format, ...) __attribute__((format(printf, 2, 3)));
// Starts the next step: logs it, writes the log to the card and shows a new
// page titled name.
void diag_step(const char *name);
// Runs frames frames of the screen.
void diag_pause(u32 frames);

enum {
	ANSWER_SKIP = -1,
	ANSWER_NO = 0,
	ANSWER_YES = 1
};

enum {
	ASK_CHECK,  // no is a failure, logged as an error
	ASK_PROBE,  // no is a finding, logged as info
	ASK_TRIED,  // a check of something the user may not have tried
};

// Asks a yes/no question about what the user saw: A yes, B no, SELECT skip.
int diag_ask(const char *topic, const char *question, int kind);
// A failed check for the summary; what is a literal.
void diag_add_failure(const char *what);
// A figure slower on the cart than in the emulator, for the summary; what
// is a literal.
void diag_add_slow(const char *what);

// Takes the screen's view down, for a step that shows its own (two lists
// don't fit in BG VRAM together), and puts it back with the page as it was.
void diag_screen_hide(void);
void diag_screen_show(void);

// The renderer step (source/diag_render.c): times the engine's drawing and
// PSRAM paths and compares them with the emulator's figures.
void diag_renderer_step(void);
// The scroll-and-fade demo and its question.
void diag_renderer_demo(void);

#endif
