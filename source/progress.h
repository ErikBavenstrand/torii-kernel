// The progress dialog's bar over the steps of one operation (ui/dialog.h): a
// launch checks the save, loads the game and patches it, and each step fills
// its own share of the bar, in thousandths, so the bar shows the whole
// operation, only moves forward and is full before the dialog closes. The
// status line names only the slow steps ("Loading the game"); a quick one
// passes NULL, keeping the status, and its caller logs it.
//
//	dialog_progress_begin(name, NULL);      // the game's name as the title
//	progress_step(S(STR_LAUNCH_LOADING_GAME), 0, 50);  // the status now, the bar at 0%
//	(check the save)
//	progress_step(NULL, 50, 100);           // the bar at 5%, the same status
//	tlog("launch: ...");                    // (a quick step: only logged)
//	progress_cart_copy(fat, size, 950);     // 10% to 95% on an estimate
//	progress_done(NULL);                    // full, drawn now
//	dialog_progress_end();
//
// Each does nothing while no progress dialog is up. The pure arithmetic is in
// progress_calc.c, tested on the host.
#ifndef TORII_PROGRESS_H
#define TORII_PROGRESS_H

#include <gba_base.h>

#define PROGRESS_FULL 1000

// The cart's ROM copy, in bytes a frame, from which progress_cart_copy
// estimates how long it takes: tuned on the cart (docs/release-checklist.md
// reads it off a launch's log line "launch: the cart copied ...").
#define PROGRESS_COPY_RATE 0x2B000  // 176,128: v0.8 measured 178,481 on the cart
// The estimate fills this share (in tenths) of the copy's step by the
// expected time, then slows to 1/PROGRESS_COPY_SLOW of its speed, and stops
// at the step's end until the copy is done.
#define PROGRESS_COPY_KNEE 9
#define PROGRESS_COPY_SLOW 8

// Starts a step: the bar at lo, the status (NULL: keep it) shown now;
// progress_part fills it from lo to hi.
void progress_step(const char *status, u32 lo, u32 hi);
// done of total of the current step. Draws a few times a second
// (dialog_progress_set), so call it between blocks, never while pReadCache
// holds data still to be used (ez_define.h).
void progress_part(u32 done, u32 total);
// The bar full and status (NULL: the step's), shown for a fifth of a
// second: the last call before the dialog closes or the game starts.
void progress_done(const char *status);
// The cart copies the ROM (bytes long) into PSRAM from the FAT table fat
// (Send_FATbuffer, mode 0) as the step from the last one's end to hi. The
// kernel waits in IWRAM meanwhile, and a VBlank interrupt moves the bar on
// an estimate (PROGRESS_COPY_RATE).
// 0 once copied; 1 if Send_FATbuffer refused the table (nothing copied).
u32 progress_cart_copy(u32 *fat, u32 bytes, u32 hi);
// Its VBlank interrupt, in IWRAM.
void IWRAM_CODE progress_copy_vblank(void);
// Draws percent into the progress dialog's percentage from that interrupt,
// with the glyphs progress_cart_copy readied (progress.c)
void IWRAM_CODE progress_percent_paint(u32 percent);

// lo + (hi - lo) * done / total, clamped to [lo, hi] (hi when total is 0),
// without overflowing for any u32 sizes.
u32 progress_permille(u32 lo, u32 hi, u32 done, u32 total);
// For work whose length is only known roughly (the NOR chip erase): elapsed
// of an expected time fills the bar to 95%; past it the bar crawls on towards
// 99% and never reaches the end, which progress_done draws.
u32 progress_estimate(u32 elapsed, u32 expected);

// The copy's estimate, in 1/65536 px: the fill's end at, moving fast a
// frame up to knee, then slow, and never past end.
struct progress_copy {
	u32 at, knee, end, fast, slow;
};
// An estimate from column from to column to over frames frames.
void progress_copy_plan(struct progress_copy *c, u32 from, u32 to, u32 frames);
// A frame of the estimate: the column the fill now ends at. Inlined into
// the IWRAM interrupt (progress.c), which can't call into ROM.
static inline __attribute__((always_inline)) u32 progress_copy_advance(struct progress_copy *c)
{
	u32 at = c->at + (c->at < c->knee ? c->fast : c->slow);
	if (at > c->end)
		at = c->end;
	c->at = at;
	return at >> 16;
}

#endif
