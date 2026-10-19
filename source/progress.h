// The progress dialog's bar over the steps of one operation (ui/dialog.h): a
// launch checks the save, loads the game and patches it, and each step fills
// its own share of the bar, in thousandths, so the bar only moves forward
// and is full before the dialog closes.
//
//	dialog_progress_begin(gl_Loading, name);
//	progress_step(gl_check_sav, 0, 100);    // the status now, the bar at 10%
//	progress_step(gl_loading_game, 100, 900);
//	for (each block)
//		progress_part(done, total);         // 10% to 90%, "Loading the game 45%"
//	progress_done(NULL);                    // full, drawn now
//	dialog_progress_end();
//
// Each does nothing while no progress dialog is up. The pure arithmetic is in
// progress_calc.c, tested on the host.
#ifndef TORII_PROGRESS_H
#define TORII_PROGRESS_H

#include <gba_base.h>

#define PROGRESS_FULL 1000

// Starts a step: the bar at lo, status shown now; progress_part fills it
// from lo to hi.
void progress_step(const char *status, u32 lo, u32 hi);
// done of total of the current step: the bar and the step's status with its
// percentage. Draws a few times a second (dialog_progress_set), so call it
// between blocks, never while pReadCache holds data still to be used: the
// frame it runs may decode the theme's wallpaper there (ez_define.h).
void progress_part(u32 done, u32 total);
// The bar full and status (NULL: the step's), shown for a fifth of a
// second: the last call before the dialog closes or the game starts.
void progress_done(const char *status);

// lo + (hi - lo) * done / total, clamped to [lo, hi] (hi when total is 0),
// without overflowing for any u32 sizes.
u32 progress_permille(u32 lo, u32 hi, u32 done, u32 total);
// For work whose length is only known roughly (the NOR chip erase): elapsed
// of an expected time fills the bar to 95%; past it the bar crawls on towards
// 99% and never reaches the end, which progress_done draws.
u32 progress_estimate(u32 elapsed, u32 expected);

#endif
